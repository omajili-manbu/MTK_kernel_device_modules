// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/isolation.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/seq_file.h>
#include <linux/energy_model.h>
#include <linux/topology.h>
#include <linux/sched/topology.h>
#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/cpu.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include "sched_sys_common.h"
#include <linux/types.h>
#include "eas_plus.h"
#include "common.h"	/* mtk_task_migratable(): 6.18 delayed 僵尸判定 */
#include "eas_trace.h"

#ifdef CONFIG_HOTPLUG_CPU

static DEFINE_RAW_SPINLOCK(sched_pause_lock);
static DEFINE_RAW_SPINLOCK(drain_pending_lock);

struct pause_thread_data {
	cpumask_t cpus_to_drain;
};

struct pause_thread_data drain_data = {
	.cpus_to_drain = { CPU_BITS_NONE }
};

struct task_struct *pause_drain_thread;

/*
 * 「@p 是否允许在 @cpu 上运行」= 6.18 core is_cpu_allowed() 的同构判定。
 *
 * core 的实现是 kernel/sched/core.c 里的 static inline（厂商侧不可见），故按同构
 * 重写；逐条语义一致，唯一差别是钩子调用点直接写 trace_android_rvh_is_cpu_allowed()
 * ——core 的 is_cpu_allowed() 也正是这么调的 ⇒ 任何挂在该钩子上的模块（含本文件
 * 的 hook_rvh_is_cpus_allowed）的行为都会同步反映到这里，判据只此一份。
 *
 * 暂停语义下本判定的作用：普通任务在暂停 CPU 上 allowed=false ⇒ 必须离开本 CPU；
 * per-CPU kthread 恒为 cpu_online(cpu) ⇒ 允许留驻（与 balance_push() 对 per-CPU
 * kthread 的处理完全一致）。
 */
static inline bool mtk_cpu_allowed(struct task_struct *p, int cpu)
{
	bool allowed = true;

	/* 掩码里没有本 CPU 的（改掩码后尚未迁走的）也必须离开。 */
	if (!task_allowed_on_cpu(p, cpu))
		return false;

	/* migration disabled 的任务必须允许其跑完。 */
	if (is_migration_disabled(p))
		return cpu_online(cpu);

	trace_android_rvh_is_cpu_allowed(p, cpu, &allowed);

	/* 普通任务：仅在 active 且未被暂停时允许。 */
	if (!(p->flags & PF_KTHREAD))
		return cpu_active(cpu) && allowed;

	/*
	 * rodin 6.18（A-116/S4）：is_per_cpu_kthread() 只按
	 * PF_KTHREAD + nr_cpus_allowed==1 判，会把"被临时绑到单 CPU 的普通
	 * kthread"误当成 per-CPU 线程允许留驻暂停核。6.18 的判据是
	 * kthread_is_per_cpu()（查 KTHREAD_IS_PER_CPU，core.c:2668 同源）。
	 */
	if (kthread_is_per_cpu(p))
		return cpu_online(cpu);

	if (!allowed)
		return false;

	if (cpu_dying(cpu))
		return false;

	return cpu_online(cpu);
}

/*
 * 排空面之一：CFS/IDLE（rq->cfs_tasks；core 的 fair.c 与厂商 fair.c 均以此为
 * 本 rq 的任务链表，表项即 p->se.group_node）。
 *
 * 每轮都从链头重新派生、不留缓存指针——搬迁必定放开 rq->lock（见 drain_push），
 * 期间别的 CPU 的负载均衡可能把某个任务摘走，缓存下来的指针会立刻失效。
 */
static struct task_struct *drain_pick_cfs(struct rq *rq)
{
	struct task_struct *p;

	lockdep_assert_rq_held(rq);

	list_for_each_entry(p, &rq->cfs_tasks, se.group_node) {
		if (task_on_cpu(rq, p))
			continue;	/* 正在本 CPU 上跑；本处置只可能是 stopper */
		if (!mtk_task_migratable(p))
			continue;	/* delayed 僵尸：交 core 的唤醒/阻塞路径收尾 */
		if (mtk_cpu_allowed(p, rq->cpu))
			continue;	/* per-CPU kthread 等：允许留驻本 CPU */
		return p;
	}

	return NULL;
}

/*
 * 排空面之二：RT（rq->rt.active 优先级队列）。
 *
 * GKI 强制 RT_GROUP_SCHED=n（本树 .config 亦为 n）：此时 rt_se->rt_rq 恒等于
 * &rq->rt，队列表项就是任务自己的 sched_rt_entity ⇒ 可直接 container_of 回
 * task_struct（内核 rt.c 在 !CONFIG_RT_GROUP_SCHED 分支下的 rt_task_of() 正是
 * 这一句）。RT_GROUP_SCHED=y 时顶层实体是组实体，本函数不适用——该配置下 RT
 * 的带宽与推送另有整套语义，Android GKI 不支持。
 */
static struct task_struct *drain_pick_rt(struct rq *rq)
{
	struct rt_rq *rt_rq = &rq->rt;
	struct sched_rt_entity *rt_se;
	struct task_struct *p;
	int prio;

	lockdep_assert_rq_held(rq);

	for_each_set_bit(prio, rt_rq->active.bitmap, MAX_RT_PRIO) {
		list_for_each_entry(rt_se, &rt_rq->active.queue[prio], run_list) {
			p = container_of(rt_se, struct task_struct, rt);

			if (task_on_cpu(rq, p) || !mtk_task_migratable(p) ||
			    mtk_cpu_allowed(p, rq->cpu))
				continue;

			return p;
		}
	}

	return NULL;
}

/*
 * 排空面之三：DL（dl 类的 pick_task()）。
 *
 * 不能直接遍历 rq->dl：其 rb-tree 里可能坐着 fair/ext 带宽服务器
 * （rq->fair_server / rq->ext_server），它们的 dl_se 不属于任何 task_struct，
 * dl_task_of() 的 container_of 会算到垃圾地址。dl 类的 pick_task() 自身识别
 * 服务器（服务器无候选时会 dl_server_stop() 后重取），返回的必是真实任务。
 *
 * DL 按 deadline 单链有序、内核没有「取下一个」接口，故候选留驻即停止
 * （Android 用户态不使用 SCHED_DEADLINE，此为完备性兜底而非主路径）。
 * pick_task() 可能与 fair 服务器交互（服务器即 CFS 的带宽代理）而 repin
 * rq->lock ⇒ 传入本层真正的 @rf，与 core 的 __pick_next_task() 同一做法。
 */
static struct task_struct *drain_pick_dl(struct rq *rq, struct rq_flags *rf)
{
	struct task_struct *p;

	lockdep_assert_rq_held(rq);

	if (!sched_dl_runnable(rq))
		return NULL;

	p = dl_sched_class.pick_task(rq, rf);
	if (!p || p == RETRY_TASK)
		return NULL;

	if (task_on_cpu(rq, p) || !mtk_task_migratable(p) ||
	    mtk_cpu_allowed(p, rq->cpu))
		return NULL;

	return p;
}

/* 取 @rq 上「必须离开本 CPU」的下一个候选；调用者须持 rq->lock。 */
static struct task_struct *drain_pick_task(struct rq *rq, struct rq_flags *rf)
{
	struct task_struct *p;

	p = drain_pick_cfs(rq);
	if (p)
		return p;

	p = drain_pick_rt(rq);
	if (p)
		return p;

	return drain_pick_dl(rq, rf);
}

static inline void
rq_relock(struct rq *rq, struct rq_flags *rf)
	__acquires(rq->lock)
{
	raw_spin_rq_lock(rq);
	rq_repin_lock(rq, rf);
}

/* drain_push() 的三种归宿。 */
enum drain_act {
	DRAIN_MOVED,	/* 已推到别的 CPU */
	DRAIN_GONE,	/* 候选已失效（已离开本 rq / 转为可留驻）：重新派生即可 */
	DRAIN_BLOCKED,	/* 仍须离开但没有可用目的地：停止排空，避免自旋 */
};

/*
 * 把 @p 推离本 CPU。与 __balance_push_cpu_stop() 同构：pi_lock + rq->lock 双持下
 * select_fallback_rq() 选目的地、__migrate_task() 搬迁。
 *
 * 锁序：内核惯例是 pi_lock 在外、rq->lock 在内（见 task_rq_lock()），故先放开
 * rq->lock 再取 pi_lock，然后复锁并复核——复核前任务可能已被 core 或被别的迁移器
 * 搬走。__migrate_task() 会把锁挪到目的地 rq 上，回来时按入口快照 @orf 复位：
 * rf 里的 pin cookie 与时钟标记已被目的地那次加/解锁改写。
 *
 * @rqp 就地更新（搬迁后 rq 指针必换），调用者据此继续遍历。
 */
static enum drain_act drain_push(struct rq **rqp, struct rq_flags *rf,
				 const struct rq_flags *orf, struct task_struct *p)
{
	struct rq *dead_rq = *rqp;
	struct rq *next_rq;
	int dest_cpu;
	enum drain_act act = DRAIN_MOVED;

	rq_unlock(dead_rq, rf);
	raw_spin_lock(&p->pi_lock);
	rq_relock(dead_rq, rf);

	if (task_rq(p) != dead_rq || !mtk_task_migratable(p) ||
	    task_on_cpu(dead_rq, p)) {
		act = DRAIN_GONE;
		goto out_unlock;
	}

	/*
	 * 目的地同样经 android_rvh_is_cpu_allowed 钩子（is_cpu_allowed() 内部）⇒
	 * 天然避开暂停中的 CPU，无需在此另做过滤。
	 */
	dest_cpu = select_fallback_rq(cpu_of(dead_rq), p);
	if (dest_cpu == cpu_of(dead_rq)) {
		/*
		 * 无可用目的地（调用方可能把其它 CPU 也一并暂停了）：留驻本 CPU。
		 * 不能重试——下一轮派生的还是同一个任务。
		 */
		act = DRAIN_BLOCKED;
		goto out_unlock;
	}

	next_rq = __migrate_task(dead_rq, rf, p, dest_cpu);
	if (next_rq == dead_rq) {
		/* 目的地在此刻失去资格（并发下线等）：本轮放弃，避免自旋。 */
		act = DRAIN_BLOCKED;
		goto out_unlock;
	}

	rq_unlock(next_rq, rf);
	*rf = *orf;
	*rqp = dead_rq;
	rq_relock(dead_rq, rf);

out_unlock:
	raw_spin_unlock(&p->pi_lock);
	return act;
}

/*
 * 排空 @rq：把「不允许在本 CPU 运行」的任务逐个推走。
 *
 * 与 v5.10 血统实现（for_each_active_class + class->pick_task()，把不可迁移的
 * 任务摘出 rq、收尾再统一挂回）的区别：
 *
 *   - pick_task() 的遍历区间含 idle 类、兜底会返回 rq->idle；而 idle 任务没有
 *     ->enqueue_task，摘出后重挂必然解引用 NULL（CFI 读 [NULL-4]，即本次 CPU7
 *     事故根因）。
 *   - per-CPU kthread / migration disabled 任务按 6.18 语义本就「允许留驻」，
 *     摘出再挂回只是徒劳动它的 on_rq 记账——正是把摘出处的问题推迟到收尾处才
 *     爆的放大器。
 *
 * 这里改为按类直接遍历各排空面、只推必须离开者，全程不摘挂 rq 上的任何任务：
 * 每个候选要么被推走、要么下一轮不再匹配（判据与 drain_push() 复核同源），
 * 故循环必然收敛。
 */
static void drain_rq(struct rq *rq, struct rq_flags *rf)
{
	struct rq_flags orf = *rf;
	struct task_struct *p;

	/*
	 * 推送路径上 __migrate_task() 以 DEQUEUE_NOCLOCK 出队、activate_task(0)
	 * 入队，沿途记账都依赖 rq->clock[_task] 已推到当前。
	 */
	update_rq_clock(rq);

#ifdef CONFIG_SCHED_DEBUG
	/* note the clock update in orf */
	orf.clock_update_flags |= RQCF_UPDATED;
#endif

	for (;;) {
		p = drain_pick_task(rq, rf);
		if (!p)
			break;

		if (drain_push(&rq, rf, &orf, p) == DRAIN_BLOCKED)
			break;
	}
}

int drain_rq_cpu_stop(void *data)
{
	struct rq *rq = this_rq();
	struct rq_flags rf;

	rq_lock_irqsave(rq, &rf);
	drain_rq(rq, &rf);
	rq_unlock_irqrestore(rq, &rf);

	return 0;
}

int cpu_drain_rq(unsigned int cpu)
{
	if (!cpu_online(cpu) || !cpu_active(cpu))
		return 0;

	if (available_idle_cpu(cpu))
		return 0;

	return stop_one_cpu(cpu, drain_rq_cpu_stop, NULL);
}

int __ref try_drain_rqs(void *data)
{
	cpumask_t *cpus_ptr = &((struct pause_thread_data *)data)->cpus_to_drain;
	int cpu;
	unsigned long flags;

	while (!kthread_should_stop()) {
		raw_spin_lock_irqsave(&drain_pending_lock, flags);
		if (cpumask_weight(cpus_ptr)) {
			cpumask_t local_cpus;

			cpumask_copy(&local_cpus, cpus_ptr);
			raw_spin_unlock_irqrestore(&drain_pending_lock, flags);

			for_each_cpu(cpu, &local_cpus)
				cpu_drain_rq(cpu);

			raw_spin_lock_irqsave(&drain_pending_lock, flags);
			cpumask_andnot(cpus_ptr, cpus_ptr, &local_cpus);
		}
		raw_spin_unlock_irqrestore(&drain_pending_lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		set_current_state(TASK_RUNNING);
	}

	return 0;
}

int pause_cpus(struct cpumask *cpus)
{
	int err = 0;
	int cpu;
	cpumask_t requested_cpus;
	u64 start_time = sched_clock();
	unsigned long flags;
#if IS_ENABLED(CONFIG_MTK_THERMAL_INTERFACE)
	cpumask_t unpaused;
#endif

	raw_spin_lock_irqsave(&sched_pause_lock, flags);
	cpumask_copy(&requested_cpus, cpus);

	cpumask_andnot(cpus, cpus, cpu_pause_mask);

	/* No cpu need to pause */
	if (cpumask_empty(cpus)) {
		err = 1;
		goto unlock;
	}

	for_each_cpu(cpu, cpus) {
		/* Update cpu pause mask */
		cpumask_set_cpu(cpu, cpu_pause_mask);
	}

	raw_spin_lock_irqsave(&drain_pending_lock, flags);
	cpumask_or(&drain_data.cpus_to_drain, &drain_data.cpus_to_drain, cpus);
	raw_spin_unlock_irqrestore(&drain_pending_lock, flags);

	if (!IS_ERR(pause_drain_thread))
		wake_up_process(pause_drain_thread);

#if IS_ENABLED(CONFIG_MTK_THERMAL_INTERFACE)
	cpumask_andnot(&unpaused, cpu_possible_mask, cpu_pause_mask);
	set_cpu_active_bitmask(cpumask_bits(&unpaused)[0]);
#endif

unlock:
	raw_spin_unlock_irqrestore(&sched_pause_lock, flags);
	if (trace_sched_pause_cpus_enabled())
		trace_sched_pause_cpus(&requested_cpus, cpus, start_time, 1, err, cpu_pause_mask);

	return err;
}

int resume_cpus(struct cpumask *cpus)
{
	int err = 0;
	int cpu;
	cpumask_t requested_cpus;
	unsigned long flags;
	u64 start_time = sched_clock();
#if IS_ENABLED(CONFIG_MTK_THERMAL_INTERFACE)
	cpumask_t unpaused;
#endif

	raw_spin_lock_irqsave(&sched_pause_lock, flags);
	cpumask_copy(&requested_cpus, cpus);

	cpumask_and(cpus, cpus, cpu_pause_mask);

	/* No cpu need to resume */
	if (cpumask_empty(cpus)) {
		err = 1;
		goto unlock;
	}

	for_each_cpu(cpu, cpus) {
		/* Update cpu pause mask */
		cpumask_clear_cpu(cpu, cpu_pause_mask);
	}

#if IS_ENABLED(CONFIG_MTK_THERMAL_INTERFACE)
	cpumask_andnot(&unpaused, cpu_possible_mask, cpu_pause_mask);
	set_cpu_active_bitmask(cpumask_bits(&unpaused)[0]);
#endif

unlock:
	raw_spin_unlock_irqrestore(&sched_pause_lock, flags);
	if (trace_sched_pause_cpus_enabled())
		trace_sched_pause_cpus(&requested_cpus, cpus, start_time, 0, err, cpu_pause_mask);

	return err;
}
EXPORT_SYMBOL(resume_cpus);

int sched_pause_cpu(int cpu)
{
	int err = 0;
	struct cpumask cpu_pause_req;
	struct device *cpu_dev;
	cpumask_t avail_cpus;

	if ((cpu >= nr_cpu_ids) || (cpu < 0)) {
		err = -EINVAL;
		return err;
	}

	if (!cpu_online(cpu) || !cpu_active(cpu)) {
		err = -EBUSY;
		return err;
	}

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		err = -ENODEV;
		return err;
	}

	/* Prevent to pause the last 32 bit CPU */
	if (cpu_dev->offline_disabled == true) {
		err = -EBUSY;
		return err;
	}

	cpumask_andnot(&avail_cpus, cpu_active_mask, cpu_pause_mask);

	/* We cannot pause ALL cpus in the system */
	if (cpumask_weight(&avail_cpus) == 1) {
		err = -EBUSY;
		return err;
	}

	cpumask_clear(&cpu_pause_req);
	cpumask_set_cpu(cpu, &cpu_pause_req);

	err = pause_cpus(&cpu_pause_req);

	return err;
}
EXPORT_SYMBOL(sched_pause_cpu);

int sched_resume_cpu(int cpu)
{
	int err = 0;
	struct cpumask cpu_resume_req;

	if ((cpu >= nr_cpu_ids) || (cpu < 0)) {
		err = -EINVAL;
		return err;
	}

	if (!cpu_online(cpu) || !cpu_active(cpu)) {
		err = -EBUSY;
		return err;
	}

	cpumask_clear(&cpu_resume_req);
	cpumask_set_cpu(cpu, &cpu_resume_req);

	err = resume_cpus(&cpu_resume_req);

	return err;
}
EXPORT_SYMBOL(sched_resume_cpu);

void hook_rvh_is_cpus_allowed(void *unused, struct task_struct *p, int cpu, bool *allowed)
{
	if (cpu_paused(cpu)) {
		/* rodin 6.18（S4）：同 mtk_cpu_allowed()，按 KTHREAD_IS_PER_CPU 判。 */
		if (kthread_is_per_cpu(p))
			*allowed = true;
		else
			*allowed = false;
	}
}

void hook_rvh_set_cpus_allowed_by_task(void __always_unused *data,
			const struct cpumask *cpu_valid_mask, const struct cpumask *new_mask,
			struct task_struct *p, unsigned int *dest_cpu)
{
	cpumask_t avail_cpus, valid_mask, new;
	int best_cpu;

	if (cpu_paused(*dest_cpu)) {
		cpumask_andnot(&avail_cpus, cpu_valid_mask, cpu_pause_mask);
		best_cpu = cpumask_any_and_distribute(&avail_cpus, new_mask);

		if (best_cpu < nr_cpu_ids)
			*dest_cpu = best_cpu;

		if (trace_sched_set_cpus_allowed_enabled()) {
			cpumask_copy(&valid_mask, cpu_valid_mask);
			cpumask_copy(&new, new_mask);
			trace_sched_set_cpus_allowed(p, dest_cpu, &new, &valid_mask,
						cpu_pause_mask);
		}

	}
}

void hook_rvh_rto_next_cpu(void __always_unused *data,
			int rto_cpu, struct cpumask *rto_mask, int *cpu)
{
	cpumask_t avail_cpus;

	if (cpu_paused(*cpu)) {
		cpumask_andnot(&avail_cpus, rto_mask, cpu_pause_mask);
		*cpu = cpumask_next(rto_cpu, &avail_cpus);
	}
}

void hook_rvh_get_nohz_timer_target(void __always_unused *data,
					int *cpu, bool *done)
{
	struct root_domain *rd = cpu_rq(smp_processor_id())->rd;
	struct perf_domain *pd;
	int i, default_cpu = -1;
	const struct cpumask *hk_mask;
	int no_pau_idle = -1, no_pau = -1, on_hk_cpu = -1, on_cpu = -1;

	*done = true;

	/* 1. Choose this cpu & non-pause & hk_mask & non-idle */
	if (housekeeping_cpu(*cpu, HK_TYPE_TIMER) && !cpu_paused(*cpu)) {
		/*
		 * Use available_idle_cpu() instead of idle_cpu().
		 * Need to pay attention to the difference between
		 * available_idle_cpu() and idle_cpu() when migration.
		 */
		if (!available_idle_cpu(*cpu))
			return;

		/* Keep default_cpu = this cpu & non-pause & hk_mask */
		default_cpu = *cpu;
	}

	hk_mask = housekeeping_cpumask(HK_TYPE_TIMER);

	rcu_read_lock();
	pd = rcu_dereference(rd->pd);
	if (!pd)
		goto unlock;

	/* 2. Choose same gear of this cpu & non-pause & hk_mask & non-idle */
	for (; pd; pd = pd->next) {
		if (!cpumask_test_cpu(*cpu, perf_domain_span(pd)))
			continue;

		for_each_cpu_and(i, perf_domain_span(pd), hk_mask) {
			if (*cpu == i)
				continue;

			if (!available_idle_cpu(i) && !cpu_paused(i)) {
				*cpu = i;
				goto unlock;
			}
		}
	}

	/* this cpu is paused, choose other cpu */
	if (default_cpu == -1) {
		for_each_cpu_and(i, cpu_online_mask, hk_mask) {
			if (!cpu_paused(i)) {
				if (no_pau == -1)
					no_pau = i;

				if (!available_idle_cpu(i) && (no_pau_idle == -1))
					no_pau_idle = i;
			}
		}

		/* 4. Choose online & hk_mask & non-pause & non-idle first CPU */
		if (no_pau_idle >= 0) {
			*cpu = no_pau_idle;
			goto unlock;
		}

		/* 5. Choose online & hk_mask & non-pause first CPU */
		if (no_pau >= 0) {
			*cpu = no_pau;
			goto unlock;
		}

		/* 6. Choose online & hk_mask */
		on_hk_cpu = cpumask_any_and(cpu_online_mask, hk_mask);
		if (on_hk_cpu < nr_cpu_ids) {
			*cpu = on_hk_cpu;
			goto unlock;
		}

		/* 7. Choose online */
		on_cpu = cpumask_any(cpu_online_mask);
		if (on_cpu < nr_cpu_ids) {
			*cpu = on_cpu;
			goto unlock;
		}
	}

	/* 3. Choose this_cpu & non-pause & hk_mask */
	*cpu = default_cpu;
unlock:
	rcu_read_unlock();
}

void hook_rvh_can_migrate_task(void __always_unused *data,
				struct task_struct *p, int dst_cpu,
				int *can_migrate)
{
	if (cpu_paused(dst_cpu))
		*can_migrate = false;

}

void hook_rvh_find_busiest_queue(void *data, int dst_cpu, struct sched_group *group,
			struct cpumask *env_cpus, struct rq **busiest,
			int *done)
{
	if (cpu_paused(dst_cpu))
		*done = 1;
}

void hook_rvh_find_new_ilb(void *data, struct cpumask *nohz_idle_cpus_mask, int *ilb)
{
	int cpu;
	const struct cpumask *hk_mask;

	hk_mask = housekeeping_cpumask(HK_TYPE_MISC);

	for_each_cpu_and(cpu, nohz_idle_cpus_mask, hk_mask) {

		if (cpu == smp_processor_id())
			continue;

		if (cpu_paused(cpu))
			continue;

		if (available_idle_cpu(cpu)) {
			*ilb = cpu;
			return;
		}
	}

	*ilb = nr_cpu_ids;
}

void sched_pause_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

	pause_drain_thread = kthread_run(try_drain_rqs, &drain_data, "pause_drain_rqs");
	if (IS_ERR(pause_drain_thread)) {
		printk_deferred("Error creating pause_drain_rqs\n");
		return;
	}

	sched_setscheduler_nocheck(pause_drain_thread, SCHED_FIFO, &param);

	register_trace_android_rvh_is_cpu_allowed(hook_rvh_is_cpus_allowed, NULL);
	register_trace_android_rvh_set_cpus_allowed_by_task(
				hook_rvh_set_cpus_allowed_by_task, NULL);
	register_trace_android_rvh_rto_next_cpu(
				hook_rvh_rto_next_cpu, NULL);
	register_trace_android_rvh_get_nohz_timer_target(
				hook_rvh_get_nohz_timer_target,	NULL);
	register_trace_android_rvh_can_migrate_task(hook_rvh_can_migrate_task, NULL);
	register_trace_android_rvh_find_busiest_queue(hook_rvh_find_busiest_queue, NULL);
	register_trace_android_rvh_find_new_ilb(hook_rvh_find_new_ilb, NULL);
}

static ssize_t show_sched_core_pause_info(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	unsigned int len = 0;
	unsigned int max_len = 4096;

	len += snprintf(buf+len, max_len-len,
			"cpu_pause_mask=0x%lx\n",
			__cpu_pause_mask.bits[0]);

	return len;
}

struct kobj_attribute sched_core_pause_info_attr =
__ATTR(sched_core_pause_info, 0400, show_sched_core_pause_info, NULL);

#endif /* CONFIG_HOTPLUG_CPU */
