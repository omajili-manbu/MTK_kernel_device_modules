// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#include <linux/init.h>
#include <linux/vseq.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/seq_file.h>
#include <linux/energy_model.h>
#include <linux/jump_label.h>
#include <trace/events/sched.h>
#include <trace/events/task.h>
#include <trace/hooks/sched.h>
//#include <trace/hooks/hung_task.h>
#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include "common.h"
#include "eas_plus.h"
#include "sched_sys_common.h"
#include "sugov/cpufreq.h"
#include "eas_adpf.h"
#if IS_ENABLED(CONFIG_MTK_GEARLESS_SUPPORT)
#include "mtk_energy_model/v3/energy_model.h"
#else
#include "mtk_energy_model/v1/energy_model.h"
#endif
#include "sugov/dsu_interface.h"
#include "vip.h"
#include <mt-plat/mtk_irq_mon.h>
#if IS_ENABLED(CONFIG_MTK_SCHED_FAST_LOAD_TRACKING)
#include "flt_init.h"
#include "flt_api.h"
#include "group.h"
#include "flt_cal.h"
#include <linux/of.h>
#include <linux/platform_device.h>
#endif
#include "grp_awr.h"

#define CREATE_TRACE_POINTS
#include "eas_trace.h"

#define TAG "EAS_IOCTL"

int mtk_sched_asym_cpucapacity  =  1;
unsigned int dn_pct = -1;
unsigned int up_pct = -1;
int gear_start = -1;
int num_gear = -1;
int reverse = -1;

static inline void sched_asym_cpucapacity_init(void)
{
	struct perf_domain *pd;
	struct root_domain *rd;
	int pd_count = 0;

	preempt_disable();
	rd = cpu_rq(smp_processor_id())->rd;

	rcu_read_lock();
	pd = rcu_dereference(rd->pd);
	for (; pd; pd = pd->next)
		pd_count++;

	rcu_read_unlock();
	preempt_enable();
	if (pd_count <= 1)
		mtk_sched_asym_cpucapacity = 0;
}

static void sched_task_util_hook(void *data, struct sched_entity *se)
{
	if (!get_eas_hook())
		return;

	if (trace_sched_task_util_enabled()) {
		struct task_struct *p;
		struct sched_avg *sa;

		if (!entity_is_task(se))
			return;

		p = container_of(se, struct task_struct, se);
		sa = &se->avg;

		trace_sched_task_util(p->pid,
				sa->util_avg, sa->util_est);
	}
}

static void sched_task_uclamp_hook(void *data, struct sched_entity *se)
{
	if (!get_eas_hook())
		return;

	if (trace_sched_task_uclamp_enabled()) {
		struct task_struct *p;
		struct uclamp_se *uc_min_req, *uc_max_req;
		unsigned long util;

		if (!entity_is_task(se))
			return;

		p = container_of(se, struct task_struct, se);
		util = READ_ONCE(se->avg.util_est);
		util = max(util, READ_ONCE(se->avg.util_avg));
		uc_min_req = &p->uclamp_req[UCLAMP_MIN];
		uc_max_req = &p->uclamp_req[UCLAMP_MAX];

		trace_sched_task_uclamp(p->pid, util,
				p->uclamp[UCLAMP_MIN].active,
				p->uclamp[UCLAMP_MIN].value, p->uclamp[UCLAMP_MAX].value,
				uc_min_req->user_defined, uc_min_req->value,
				uc_max_req->user_defined, uc_max_req->value);
	}
}

static void sched_select_task_rq_fair_hook(void *ignore, struct task_struct *p,
							int prev_cpu, int sd_flag,
							int wake_flags, int *target_cpu)
{
	struct root_domain *rd;
	int sync, new_cpu;

	if (trace_sched_domain_flags_enabled())
		trace_sched_domain_flags(p, prev_cpu, sd_flag, wake_flags, *target_cpu);

	/*
	 * rodin 6.18（S9/P-13）：只在普通唤醒路径接管。fork/exec 与
	 * WF_CURRENT_CPU 由 core 自己处理；sd_flag 与 wake_flags 共用低半字节
	 * （6.18 fair.c:8880-8881）。本钩子的投递点 core.c:8886-8889 在
	 * p->pi_lock 内、IRQs off，且钩子可写 target_cpu 后立即返回。
	 */
	if (!(wake_flags & WF_TTWU) || (wake_flags & WF_CURRENT_CPU) ||
	    (sd_flag & (SD_BALANCE_FORK | SD_BALANCE_EXEC)))
		return;

	if (!get_eas_hook())	/* 现成的运行期总开关 = kill switch */
		return;

	rd = cpu_rq(smp_processor_id())->rd;

	/*
	 * 过载时不接管，交 core 做负载均衡 —— 对应 6.18 fair.c:8902
	 * !is_rd_overutilized() 里 rd->overutilized 的那一半。
	 * 不并入其 !sched_energy_enabled() 半边：真机绑 sugov_ext，
	 * cpufreq_ready_for_eas() 指针比较直接判 false（cpufreq.c:3078），
	 * sched_energy_enabled() 恒为 false，补上这一条等于把本接管永久关死。
	 * 代价是该字段只在 EAS 开启时才被维护（fair.c:7135 set_rd_overutilized()
	 * 首行早退），当前配置下恒为 0，过载门实际不生效。
	 */
	if (READ_ONCE(rd->overutilized))
		return;

	/*
	 * vendor EAS 的能量扫描以 rd->pd 为输入（fair.c:2212/2222）；rd->pd 只有
	 * 上游 EAS 域建起来时才非空（topology.c:419 build_perf_domains()）。
	 * 没有 perf domain 时 mtk_find_energy_efficient_cpu() 会落进 LB_FAIL 的
	 * 备份选择（wake_affine + idle 选核），那条路与 core 自己的选核重复且
	 * 更激进 —— 此时直接交还 core，不做接管。
	 */
	if (!rcu_access_pointer(rd->pd))
		return;

	sync = (wake_flags & WF_SYNC) && !(current->flags & PF_EXITING);
	new_cpu = -1;
	mtk_find_energy_efficient_cpu(NULL, p, prev_cpu, sync, &new_cpu);

	/* 结果硬校验：任一不满足就放弃接管，让 core 走原路 */
	if (new_cpu < 0 || new_cpu >= nr_cpu_ids || !cpu_active(new_cpu) ||
	    !cpumask_test_cpu(new_cpu, p->cpus_ptr))
		return;

	*target_cpu = new_cpu;
}

static void sched_rq_load_hook(void *data, struct cfs_rq *rq)
{
	if (trace_sched_rq_load_enabled())
		trace_sched_rq_load(rq);
}

static int enqueue;
static int dequeue;
static void sched_queue_task_hook(void *data, struct rq *rq, struct task_struct *p, int flags)
{
	int cpu = rq->cpu;
	int type = *(int *)data;
	struct sugov_rq_data *sugov_data_ptr;

	if (!get_eas_hook())
		return;

	irq_log_store();

	/*
	 * rodin 6.18：flt enqueue/dequeue 记账不再经本钩子——本钩子整体在
	 * get_eas_hook() 门后（6.18 上恒死），记账被连坐。两钩已改在
	 * flt_register_kernel_hooks() 直挂（android_rvh_enqueue/dequeue_task）
	 * 与门解耦；此处保持摘除，防将来门复活时对同一事件双计。
	 */

	if (type == enqueue) {
		struct uclamp_se *uc_min_req, *uc_max_req;

		sugov_data_ptr = &per_cpu(rq_data, rq->cpu)->sugov_data;
		WRITE_ONCE(sugov_data_ptr->enq_ing, true);

		uc_min_req = &p->uclamp_req[UCLAMP_MIN];
		uc_max_req = &p->uclamp_req[UCLAMP_MAX];

		if (uc_min_req->user_defined || uc_max_req->user_defined) {
			bool uclamp_diff = false;

			uclamp_diff = (READ_ONCE(sugov_data_ptr->uclamp[UCLAMP_MIN])
				!= READ_ONCE(rq->uclamp[UCLAMP_MIN].value));

			uclamp_diff = (uclamp_diff
				|| (READ_ONCE(sugov_data_ptr->uclamp[UCLAMP_MAX])
				!= READ_ONCE(rq->uclamp[UCLAMP_MAX].value)));

			if (uclamp_diff) {
				int this_cpu = smp_processor_id();

				sugov_data_ptr = &per_cpu(rq_data, this_cpu)->sugov_data;
				WRITE_ONCE(sugov_data_ptr->enq_dvfs, true);
			}
		}
	}

	if (trace_sched_queue_task_enabled()) {
		unsigned long util = READ_ONCE(rq->cfs.avg.util_avg);

		util = max_t(unsigned long, util,
			     READ_ONCE(rq->cfs.avg.util_est));

		trace_sched_queue_task(cpu, p->pid, type, util,
				rq->uclamp[UCLAMP_MIN].value, rq->uclamp[UCLAMP_MAX].value,
				p->uclamp[UCLAMP_MIN].value, p->uclamp[UCLAMP_MAX].value,
				p->cpus_ptr->bits[0]);
	}

	irq_log_store();
}


#if IS_ENABLED(CONFIG_DETECT_HUNG_TASK)

struct migration_arg {
	struct task_struct		*task;
	int				dest_cpu;
	struct set_affinity_pending	*pending;
};

/*
 * @refs: number of wait_for_completion()
 * @stop_pending: is @stop_work in use
 */
struct set_affinity_pending {
	refcount_t		refs;
	unsigned int		stop_pending;
	struct completion	done;
	struct cpu_stop_work	stop_work;
	struct migration_arg	arg;
};
/*
 * rodin 6.18（S6/P-42）：本函数从未注册 —— 对应注册点在
 * register_sched_hooks() 里被注释掉（见文件末尾的
 * "register_trace_android_vh_check_uninterrupt_tasks" 注释行），因此是死代码。
 * 它涉及的 struct set_affinity_pending / migration_pending 是 core 私有实现
 * 细节，6.18 未导出；保留在此仅供排障参考，新增调用前必须先确认 6.18 的
 * 迁移收尾语义（set_cpus_allowed_ptr 的 pending 协议已改写）。
 */
//static void mtk_check_d_tasks(void *data, struct task_struct *p,
void mtk_check_d_tasks(void *data, struct task_struct *p,
				unsigned long t, bool *need_check)
{
	unsigned long pending_stime = 0;
	unsigned long switch_count = p->nvcsw + p->nivcsw;
	struct mig_task_struct *migts = &(mtk_task_of(p))->mig_task;
	struct cpumask cpus;
	struct set_affinity_pending *my_pending = NULL;
	struct task_struct *dest_task;
	struct migration_arg *arg;
	int dest_cpu;

	*need_check = true;

	/*
	 * Reset the migration_pending start time
	 */
	if (!p->migration_pending || switch_count != p->last_switch_count) {
		migts->pending_rec = 0;
		return;
	}

	/*check the cpu online mask */
	if (p->migration_pending) {
		if (!cpumask_and(&cpus, &p->cpus_mask, cpu_online_mask) ||
			is_migration_disabled(p)) {
			migts->pending_rec = 0;
			*need_check = false;
			return;
		}
	}

	/*
	 * Record the migration_pending start time
	 */
	if (p->migration_pending && migts->pending_rec == 0) {
		migts->pending_rec = jiffies;
		*need_check = false;
		return;
	}
	/*
	 * Check the whether migration time is time out or not
	 */
	pending_stime = migts->pending_rec;
	if (time_is_after_jiffies(pending_stime + t * HZ)) {
		*need_check = false;
	} else {
		*need_check = true;
		my_pending = p->migration_pending;
		if (my_pending) {
			arg = &my_pending->arg;
			dest_cpu = arg->dest_cpu;
			dest_task = cpu_curr(dest_cpu);
			pr_info(" pending %-15.15s %d %d %d %d\n",
				dest_task->comm, dest_task->pid, dest_cpu,
				my_pending->stop_pending, my_pending->done.done);
		}
		pr_info("flags:0x%x m_flags:%d %d %d %d %d 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx 0x%lx\n",
			p->flags, p->migration_flags, is_migration_disabled(p), p->on_rq, p->on_cpu,
			p->nr_cpus_allowed, p->cpus_mask.bits[0], p->cpus_ptr->bits[0],
			cpu_active_mask->bits[0], cpu_online_mask->bits[0],
			cpu_possible_mask->bits[0], cpu_pause_mask->bits[0]);
	}
}
#endif

void mtk_setscheduler_uclamp(void *data, struct task_struct *tsk,
	int clamp_id, unsigned int value)
{
	if (!get_eas_hook())
		return;

	if (trace_sched_set_uclamp_enabled())
		trace_sched_set_uclamp(tsk->pid,
		task_cpu(tsk), task_on_rq_queued(tsk), clamp_id, value);
}

static void mtk_sched_pelt_multiplier(void *data, unsigned int old_pelt,
				      unsigned int new_pelt, int *ret)
{
	int pelt_weight __maybe_unused = 0, pelt_sum __maybe_unused = 0;

	switch (new_pelt) {
	/* pelt 32 */
	case 1:
		pelt_weight = 1002;
		pelt_sum = 47742;
		break;
	/* pelt 16 */
	case 2:
		pelt_weight = 981;
		pelt_sum = 24125;
		break;
	/* pelt 8 */
	case 4:
		pelt_weight = 939;
		pelt_sum = 12329;
		break;
	default:
		break;
	}
	/*
	 * rodin 6.18（A-147 第三批）：update_pelt_data 调用点随 wl 链 dormant
	 * （is_wl_support() 在 rodin DT 无 wl-info 下恒 false）删除；pelt8
	 * 模型权重取值逻辑保留（值仍被上层的 sched_pelt_ctrl 接口面消费）。
	 */
	(void)pelt_weight;
	(void)pelt_sum;
}

static void mtk_post_init_entity_util_avg(void *data, struct sched_entity *se)
{
	struct cfs_rq *cfs_rq = cfs_rq_of(se);
	struct sched_avg *sa = &se->avg;
	int cpu = cpu_of(rq_of(cfs_rq));
	unsigned long desired_cpu_scale = 0, ori = 0, desired_cpufreq;
	unsigned long long desired_util_avg = 0;
	struct task_struct *p = NULL;
	struct cgroup_subsys_state *css = NULL;
	bool post_init_util_ctl = sched_post_init_util_enable_get();
	int freq;

	if (!post_init_util_ctl || !get_eas_hook())
		return;

	if (likely(entity_is_task(se)))
		p = task_of(se);
	else
		return;

	/* vip & ls don't hack*/
	if (
#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
		task_is_vip(p, NOT_VIP)  ||
#endif
		is_task_latency_sensitive(p))
		return;

	/* TA don't hack*/
	css = task_css(p, cpu_cgrp_id);
	if (css) {
		if (!strcmp(css->cgroup->kn->name, "top-app"))
			return;
	}

	/*other hack...*/
	freq = pd_opp2freq(cpu, 0, false, 0);
	desired_cpufreq = (freq >> 2) + (freq >> 3);
	desired_cpu_scale = pd_get_freq_util(cpu, desired_cpufreq);
	desired_util_avg = (desired_cpu_scale * 819) >> 10;

	/* suppressed to desired freq when fork*/
	if (sa->util_avg > (unsigned long)desired_util_avg) {
		ori = sa->util_avg;
		sa->util_avg = desired_util_avg;
		if (trace_sched_post_init_entity_util_avg_enabled()) {
			trace_sched_post_init_entity_util_avg(p, ori, sa->util_avg,
				se->load.weight, freq, desired_cpufreq, cpu);
		}
		sa->runnable_avg = sa->util_avg;
	}
}

static void mtk_set_cpus_allowed_ptr(void *data, struct task_struct *p,
	struct affinity_context *ctx, bool *skip_user_ptr)
{
	struct cpumask *kernel_allowed_mask = &(mtk_task_of(p))->kernel_allowed_mask;
	cpumask_t new_mask;

	// not set or invalid cpu mask
	if (cpumask_empty(kernel_allowed_mask)){
		return;
	}

	if (!strcmp("ndroid.systemui", p->group_leader->comm) && task_uid(p).val == 1000)
	{
		return;
	}

	/*
	 * rodin 6.18（A-59）：本钩子在 core 取 task_rq_lock() 之前触发，因此
	 * 不得碰任何 rq 状态 —— rq->scratch_mask 的注释明写 "under rq_lock"，
	 * core 自己紧接着就在锁内写它；而 task_rq(p) 在锁外解析还可能拿到已经
	 * 过期的 rq。改成把我们自己持有的掩码交给 core：ctx->new_mask 此后只被
	 * 读取（cpumask_copy / cpumask_weight / mm_set_cpus_allowed / trace），
	 * 且 __do_set_cpus_allowed() 只在 SCA_MIGRATE_* 时把它写进 p->cpus_ptr，
	 * 下面这个分支已把 SCA_MIGRATE_* / SCA_USER 全部排除 ⇒ 直接指向任务
	 * 自持的 kernel_allowed_mask 是安全的。也免去 scratch 中转：原来
	 * cpumask_and() 的结果随即被 cpumask_copy() 覆盖，取非空即可。
	 */
	if (p->user_cpus_ptr &&
		!(ctx->flags & (SCA_USER | SCA_MIGRATE_ENABLE | SCA_MIGRATE_DISABLE)) &&
		cpumask_intersects(ctx->new_mask, p->user_cpus_ptr)) {
		*skip_user_ptr = true;
		ctx->new_mask = kernel_allowed_mask;
		}
	if (trace_sched_skip_user_enabled() && p->user_cpus_ptr && !cpumask_empty(kernel_allowed_mask)){
		cpumask_copy(&new_mask, ctx->new_mask);
		trace_sched_skip_user(p, *skip_user_ptr, p->user_cpus_ptr, kernel_allowed_mask, &new_mask);
	}
}

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
static void sched_irq_mon_init(void)
{
	return;
	// 8205591: [ALPS08534549] sched: irq too long log | https://gerrit.mediatek.inc/c/quark/kernel-mainline/+/8205591
	//mtk_register_irq_log_store(__irq_log_store);
}
#endif

static void mtk_sched_trace_init(void)
{
	int ret = 0;

	enqueue = 1;
	dequeue = -1;

	ret = register_trace_android_rvh_enqueue_task(sched_queue_task_hook, &enqueue);
	if (ret)
		pr_info("register android_rvh_enqueue_task failed!\n");
	ret = register_trace_android_rvh_dequeue_task(sched_queue_task_hook, &dequeue);
	if (ret)
		pr_info("register android_rvh_dequeue_task failed!\n");

	ret = register_trace_pelt_se_tp(sched_task_util_hook, NULL);
	if (ret)
		pr_info("register sched_task_util_hook failed!\n");

	ret = register_trace_pelt_se_tp(sched_task_uclamp_hook, NULL);
	if (ret)
		pr_info("register sched_task_uclamp_hook failed!\n");

	ret = register_trace_sched_util_est_cfs_tp(sched_rq_load_hook, NULL);
	if (ret)
		pr_info("register sched_rq_load_hook failed!\n");

	ret = register_trace_android_rvh_select_task_rq_fair(
			sched_select_task_rq_fair_hook, NULL);
	if (ret)
		pr_info("register sched_select_task_rq_fair_hook failed!\n");
}

static void mtk_sched_trace_exit(void)
{
	unregister_trace_pelt_se_tp(sched_task_util_hook, NULL);
	unregister_trace_pelt_se_tp(sched_task_uclamp_hook, NULL);
	unregister_trace_sched_util_est_cfs_tp(sched_rq_load_hook, NULL);
}

static unsigned long easctl_copy_from_user(void *pvTo,
		const void __user *pvFrom, unsigned long ulBytes)
{
	if (access_ok(pvFrom, ulBytes))
		return __copy_from_user(pvTo, pvFrom, ulBytes);

	return ulBytes;
}

static unsigned long easctl_copy_to_user(void __user *pvTo,
		const void *pvFrom, unsigned long ulBytes)
{
	if (access_ok(pvTo, ulBytes))
		return __copy_to_user(pvTo, pvFrom, ulBytes);

	return ulBytes;
}

/*--------------------SYNC------------------------*/
static int eas_show(struct seq_file *m, void *v)
{
	return 0;
}

static int eas_open(struct inode *inode, struct file *file)
{
	return single_open(file, eas_show, inode->i_private);
}

static long eas_ioctl_impl(struct file *filp,
		unsigned int cmd, unsigned long arg, void *pKM)
{
	ssize_t ret = 0;

	unsigned int sync;
	unsigned int val;
	int i;
	struct cpumask mask;

	struct SA_task SA_task_args = {
		.pid = -1,
		.mask = 0
	};

#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
	int grp_id;
	struct gas_ctrl gas_ctrl_args = {
		.val = 0,
		.force_ctrl = 0
	};

	struct gas_thr gas_thr_args = {
		.grp_id = -1,
		.val = -1
	};

	struct gas_margin_thr gas_margin_args = {
		.gear_id = -1,
		.group_id = -1,
		.margin = -1,
		.converge_thr = -1
	};
#endif

	cpumask_clear(&mask);
	cpumask_copy(&mask, cpu_possible_mask);
	SA_task_args.mask = mask.bits[0];

	switch (cmd) {
	case EAS_IGNORE_IDLE_UTIL_CTRL:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_ignore_idle_ctrl(val);
		break;
	case EAS_SYNC_SET:
		if (easctl_copy_from_user(&sync, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_wake_sync(sync);
		break;
	case EAS_SYNC_GET:
		sync = get_wake_sync();
		if (easctl_copy_to_user((void *)arg, &sync, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_PERTASK_LS_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_uclamp_min_ls(val);
		break;
	case EAS_PERTASK_LS_GET:
		val = get_uclamp_min_ls();
		if (easctl_copy_to_user((void *)arg, &val, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_ACTIVE_MASK_GET:
		val = __cpu_active_mask.bits[0];
		if (easctl_copy_to_user((void *)arg, &val, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_NEWLY_IDLE_BALANCE_INTERVAL_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_newly_idle_balance_interval_us(val);
		break;
	case EAS_NEWLY_IDLE_BALANCE_INTERVAL_GET:
		val = get_newly_idle_balance_interval_us();
		if (easctl_copy_to_user((void *)arg, &val, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_GET_THERMAL_HEADROOM_INTERVAL_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_get_thermal_headroom_interval_tick(val);
		break;
	case EAS_GET_THERMAL_HEADROOM_INTERVAL_GET:
		val = get_thermal_headroom_interval_tick();
		if (easctl_copy_to_user((void *)arg, &val, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_SET_CPUMASK_TA:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_top_app_cpumask(val);
		break;
	case EAS_SET_CPUMASK_BACKGROUND:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_foreground_cpumask(val);
		break;
	case EAS_SET_CPUMASK_FOREGROUND:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_background_cpumask(val);
		break;
	case EAS_SET_TASK_LS:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_task_ls(val);
		break;
	case EAS_UNSET_TASK_LS:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_task_ls(val);
		break;
	case EAS_SET_TASK_LS_PREFER_CPUS:
		if (easctl_copy_from_user(&SA_task_args, (void *)arg, sizeof(struct SA_task)))
			return -1;
		set_task_ls_prefer_cpus(SA_task_args.pid, SA_task_args.mask);
		break;
	case EAS_SET_TASK_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_task_basic_vip(val);
		break;
	case EAS_UNSET_TASK_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_task_basic_vip(val);
		break;
	case EAS_SET_TA_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_top_app_vip(val);
		break;
	case EAS_UNSET_TA_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_top_app_vip();
		break;
	case EAS_SET_FG_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_foreground_vip(val);
		break;
	case EAS_UNSET_FG_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_foreground_vip();
		break;
	case EAS_SET_BG_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_background_vip(val);
		break;
	case EAS_UNSET_BG_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_background_vip();
		break;
	case EAS_SET_LS_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_ls_task_vip(val);
		break;
	case EAS_UNSET_LS_VIP:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_ls_task_vip();
		break;
	case EAS_GEAR_MIGR_DN_PCT:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		dn_pct = val;
		break;
	case EAS_GEAR_MIGR_UP_PCT:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		up_pct = val;
		break;
	case EAS_GEAR_MIGR_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		if (!set_updown_migration_pct(val, dn_pct, up_pct))
			return -1;
		dn_pct = up_pct = 0; //set success, reset local value
		break;
	case EAS_GEAR_MIGR_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		if (!unset_updown_migration_pct(val))
			return -1;
		break;
	case EAS_TASK_GEAR_HINTS_START:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		gear_start = (int) val;
		break;
	case EAS_TASK_GEAR_HINTS_NUM:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		num_gear = (int) val;
		break;
	case EAS_TASK_GEAR_HINTS_REVERSE:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		reverse = (int) val;
		break;
	case EAS_TASK_GEAR_HINTS_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		if (!set_gear_indices(val, gear_start, num_gear, reverse))
			return -1;
		gear_start = num_gear = reverse = -1; //set success, reset local value
		break;
	case EAS_TASK_GEAR_HINTS_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		if (!unset_gear_indices(val))
			return -1;
		break;
	case EAS_RT_AGGRE_PREEMPT_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_rt_aggre_preempt(val);
		break;
	case EAS_RT_AGGRE_PREEMPT_GET:
		val = get_rt_aggre_preempt();
		if (easctl_copy_to_user((void *)arg, &val, sizeof(unsigned int)))
			return -1;
		break;
	case EAS_SBB_ALL_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_ALL, 0, true);
		break;
	case EAS_SBB_ALL_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_ALL, 0, false);
		break;
	case EAS_SBB_GROUP_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_GROUP, val, true);
		break;
	case EAS_SBB_GROUP_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_GROUP, val, false);
		break;
	case EAS_SBB_TASK_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_TASK, val, true);
		break;
	case EAS_SBB_TASK_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb(SBB_TASK, val, false);
		break;
	case EAS_SBB_ACTIVE_RATIO:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_sbb_active_ratio(val);
		break;
	case EAS_TURN_POINT_UTIL_C0:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_turn_point_freq(0, val);
		break;
	case EAS_TURN_POINT_UTIL_C1:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_turn_point_freq(1, val);
		break;
	case EAS_TURN_POINT_UTIL_C2:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_turn_point_freq(2, val);
		break;
	case EAS_TARGET_MARGIN_C0:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		for(i = 0; i < MAX_NR_CPUS; i++) {
			if(topology_cluster_id(i) == 0)
				set_target_margin(i, val);
		}
		break;
	case EAS_TARGET_MARGIN_C1:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		for(i = 0; i < MAX_NR_CPUS; i++) {
			if(topology_cluster_id(i) == 1)
				set_target_margin(i, val);
		}
		break;
	case EAS_TARGET_MARGIN_C2:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		for(i = 0; i < MAX_NR_CPUS; i++) {
			if(topology_cluster_id(i) == 2)
				set_target_margin(i, val);
		}
		break;
	case EAS_UTIL_EST_CONTROL:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_util_est_ctrl(val);
		break;
#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
	case EAS_SET_GAS_CTRL:
		if (easctl_copy_from_user(&gas_ctrl_args, (void *)arg, sizeof(struct gas_ctrl)))
			return -1;
		set_top_grp_aware(gas_ctrl_args.val, gas_ctrl_args.force_ctrl);
		break;
	case EAS_SET_GAS_THR:
		if (easctl_copy_from_user(&gas_thr_args, (void *)arg, sizeof(struct gas_thr)))
			return -1;
		group_set_threshold(gas_thr_args.grp_id, gas_thr_args.val);
		break;
	case EAS_RESET_GAS_THR:
		if (easctl_copy_from_user(&grp_id, (void *)arg, sizeof(int)))
			return -1;
		group_reset_threshold(grp_id);
		break;
	case EAS_SET_GAS_MARG_THR:
		if (easctl_copy_from_user(&gas_margin_args, (void *)arg,
				sizeof(struct gas_margin_thr)))
			return -1;
		set_grp_awr_thr(gas_margin_args.gear_id, gas_margin_args.group_id,
			gas_margin_args.converge_thr);
		set_grp_awr_min_opp_margin(gas_margin_args.gear_id, gas_margin_args.group_id,
			gas_margin_args.margin);
		break;
	case EAS_RESET_GAS_MARG_THR:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(int)))
			return -1;
		if(reset_grp_awr_margin() == -1)
			return -1;
		break;
	case EAS_DPT_CTRL:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(int)))
			return -1;
		if(change_dpt_support_driver_hook)
			change_dpt_support_driver_hook(val);
		else
			pr_info("dpt ctrl hook is not ready!!!\n");
		break;
	case EAS_SET_DSU_IDLE:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_dsu_idle_enable(val);
		break;
	case EAS_UNSET_DSU_IDLE:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_dsu_idle_enable();
		break;
#endif
	case EAS_RUNNABLE_BOOST_SET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		set_runnable_boost_enable(val);
		break;
	case EAS_RUNNABLE_BOOST_UNSET:
		if (easctl_copy_from_user(&val, (void *)arg, sizeof(unsigned int)))
			return -1;
		unset_runnable_boost_enable();
		break;
	default:
		pr_debug(TAG "%s %d: unknown cmd %x\n",
			__FILE__, __LINE__, cmd);
		ret = -1;
		goto ret_ioctl;
	}

ret_ioctl:
	return ret;
}

static long eas_ioctl(struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	return eas_ioctl_impl(filp, cmd, arg, NULL);
}

static const struct proc_ops eas_Fops = {
	.proc_ioctl = eas_ioctl,
#if IS_ENABLED(CONFIG_COMPAT)
	.proc_compat_ioctl = eas_ioctl,
#endif
	.proc_open = eas_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

#if IS_ENABLED(CONFIG_MTK_SCHED_FAST_LOAD_TRACKING)

static int flt_resume_cb(struct device *dev)
{
	flt_resume_notify();
	return 0;
}

static int flt_suspend_cb(struct device *dev)
{
	flt_suspend_notify();
	return 0;
}

static const struct dev_pm_ops flt_pm_ops = {
	.resume_noirq = flt_resume_cb,
	.suspend_noirq = flt_suspend_cb,
};

struct tag_chipid {
	u32 size;
	u32 hw_code;
	u32 hw_subcode;
	u32 hw_ver;
	u32 sw_ver;
};

static void flt_set_chip_ver(void)
{
	struct device_node *node;
	struct tag_chipid *chip_id = NULL;
	int len;

	node = of_find_node_by_path("/chosen");
	if (!node)
		node = of_find_node_by_path("/chosen@0");
	if (node) {
		chip_id = (struct tag_chipid *)of_get_property(node, "atag,chipid", &len);
		if (!chip_id)
			pr_info("could not found atag,chipid in chosen\n");
	} else {
		pr_info("chosen node not found in device tree\n");
	}
	if (chip_id)
		flt_set_chip_sw_ver(chip_id->sw_ver);
}

static int platform_flt_probe(struct platform_device *pdev)
{
	int ret = 0, retval = 0;
	u32 flt_mode = FLT_MODE_0, version = 0;
	struct device_node *node;

	if (!of_have_populated_dt())
		goto flt_exit;

	node = pdev->dev.of_node;

	ret = of_property_read_u32(node, "mode", &retval);
	if (!ret)
		flt_mode = retval;
	ret = of_property_read_u32(node, "version", &retval);
	if (!ret)
		version = retval;
flt_exit:
	pr_info("FLT flt_mode=%u version=%u", flt_mode, version);
	flt_set_mode(flt_mode);
	flt_set_version(version);
	flt_set_chip_ver();
	return 0;
}

static const struct of_device_id platform_flt_match[] = {
	{ .compatible = "mediatek,flt", },
	{},
};

static const struct platform_device_id platform_flt_id_table[] = {
	{ "flt", 0},
	{ },
};

static struct platform_driver mtk_platform_flt_driver = {
	.probe = platform_flt_probe,
	.driver = {
		.name = "FLT",
		.owner = THIS_MODULE,
		.of_match_table = platform_flt_match,
		.pm = &flt_pm_ops,
	},
	.id_table = platform_flt_id_table,
};

void init_flt_platform(void)
{
	platform_driver_register(&mtk_platform_flt_driver);
}

void exit_flt_platform(void)
{
	platform_driver_unregister(&mtk_platform_flt_driver);
}
#endif


/*
 * rodin 6.18：native EAS bridge（sugov_ext 下放行 6.18 native EAS 资格）。
 *
 * 6.18 的 EAS 资格路径（kernel/sched/topology.c sched_is_eas_possible()）
 * 第一步就是 android_rvh_build_perf_domains：回调置 true 即直接判定可用；
 * 置 false 则继续走标准检查。标准检查里 cpufreq_ready_for_eas() 的
 * governor 身份门（sugov_is_governor() 与内核 schedutil_gov 指针比较）
 * 使本平台恒失败——三 policy 的 governor 是 vendor sugov_ext（mtk_gov），
 * perf domain 无法建立（sched_energy_aware 读为 0 字节）。
 *
 * 这里注册该回调做 fail-closed 的资格复核，只有下列条件全部成立才置
 * true（即 hook 绕过的全部前置条件）：
 *  1) 不对称容量拓扑：online CPU 有 >=2 个不同 capacity 值，且
 *     sd_asym_cpucapacity 已建立；
 *  2) 无 SMT；
 *  3) 频率不变性：cpufreq FIE 生效、per-cpu freq scale 因子健全
 *     （mtk_arch_set_freq_scale 经 android_vh_arch_set_freq_scale 覆写，
 *      载体仍是 arch_freq_scale）；
 *  4) policy 覆盖全集：每个 online CPU 的 policy 都是 mtk-cpufreq-hw +
 *     governor=sugov_ext（等价于被绕过的 cpufreq_ready_for_eas 语义，
 *     按 MTK governor 放行——这是本实验的有意识 override 点）；
 *  5) EM 完整：每 CPU 有真实硬件表（非 artificial、MICROWATTS），状态数
 *     >=2、频率严格递增、功率非退化；
 *  6) 回调无状态：不缓存 cpu mask，每次重建路径实时读；不读写 rd->pd，
 *     域的建立与 sched_energy_present 仍由 kernel build_perf_domains()/
 *     sched_energy_set() 原路径完成。
 *
 * 任何一项不成立保持 false，落回标准路径——标准路径在内核 schedutil
 * governor 下自行成立，本回调对 schedutil 场景是纯加法、无否决权。
 * 触发面：内核 schedutil 在 governor 变更（sugov_init/sugov_exit）调用
 * em_rebuild_sched_domains()，用户态把 policy 切到 sugov_ext 即触发重建；
 * CPU hotplug / cpuset partition 变更经 partition_sched_domains_locked()
 * 同样进入 build_perf_domains()。
 */
#if IS_BUILTIN(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
extern struct cpufreq_governor mtk_gov;	/* sugov/cpufreq_sugov_main.c */

static void mtk_eas_bridge_build_perf_domains(void *unused, bool *eas_check)
{
	const char *drv;
	struct cpufreq_policy *policy;
	struct em_perf_domain *em;
	struct em_perf_table *table;
	unsigned long cap0 = 0;
	static bool logged;
	bool cap_asym = false, asym_ptr = false;
	int cpu, i, nr;

	if (!eas_check || *eas_check)
		return;

	for_each_online_cpu(cpu) {
		unsigned long cap = topology_get_cpu_scale(cpu);

		if (!cap0) {
			cap0 = cap;
			continue;
		}
		if (cap != cap0) {
			cap_asym = true;
			break;
		}
	}
	if (!cap_asym)
		return;

	for_each_online_cpu(cpu) {
		if (rcu_access_pointer(per_cpu(sd_asym_cpucapacity, cpu))) {
			asym_ptr = true;
			break;
		}
	}
	if (!asym_ptr)
		return;

	if (sched_smt_active())
		return;

	if (!arch_scale_freq_invariant())
		return;
	for_each_online_cpu(cpu) {
		unsigned long fs = topology_get_freq_scale(cpu);

		if (!fs || fs > SCHED_CAPACITY_SCALE)
			return;
	}

	rcu_read_lock();

	/* policy 覆盖全集：三 policy 全部 governor=sugov_ext 且
	 * driver=mtk-cpufreq-hw——等价于被绕过的 cpufreq_ready_for_eas
	 * 语义按 MTK governor 放行，这是本实验的有意识 override 点。
	 * （本树 cpufreq_driver 指针是 cpufreq.c 私有 static，走
	 *  cpufreq_get_current_driver() 取驱动名。） */
	drv = cpufreq_get_current_driver();
	if (!drv || strcmp(drv, "mtk-cpufreq-hw")) {
		rcu_read_unlock();
		return;
	}
	for_each_online_cpu(cpu) {
		policy = cpufreq_cpu_get_raw(cpu);

		if (!policy || policy->governor != &mtk_gov) {
			rcu_read_unlock();
			return;
		}
	}

	for_each_online_cpu(cpu) {
		em = em_cpu_get(cpu);

		if (!em ||
		    (em->flags & EM_PERF_DOMAIN_ARTIFICIAL) ||
		    !(em->flags & EM_PERF_DOMAIN_MICROWATTS)) {
			rcu_read_unlock();
			return;
		}
		nr = em_pd_nr_perf_states(em);
		if (nr < 2) {
			rcu_read_unlock();
			return;
		}
		table = rcu_dereference(em->em_table);
		if (!table) {
			rcu_read_unlock();
			return;
		}
		for (i = 0; i < nr; i++) {
			if (i && table->state[i].frequency <=
				 table->state[i - 1].frequency) {
				rcu_read_unlock();
				return;
			}
			if (!table->state[i].power ||
			    table->state[i].power > EM_MAX_POWER) {
				rcu_read_unlock();
				return;
			}
		}
	}

	rcu_read_unlock();

	*eas_check = true;
	if (!logged) {
		logged = true;
		pr_info("rodin-sched: native EAS bridge pass: sugov_ext@all policy, mtk-cpufreq-hw, EM complete\n");
	}
}
#endif

static int __init mtk_scheduler_init(void)
{
	struct proc_dir_entry *pe, *parent;
	int ret = 0;

	/* rodin 6.18（分区 A）：mtk_task 的载体 = GKI 动态 task_struct 尾部
	 * （CONFIG_GKI_DYNAMIC_TASK_STRUCT_SIZE=y，android_task_vendor_data()）。
	 * 静态断言只能断"需求 <= 尾部上限"（sizeof(struct mtk_task)=352 <= 1280）；
	 * 真实可用尾部尺寸是运行期量（boot cmdline android_arch_task_struct_size，
	 * 本树 gki_defconfig 内置 512）⇒ 紧随其后的运行期守卫；mtk_tg 仍在
	 * task_group 静态槽内（24B <= 32B，断言保留）。
	 */
	MTK_VENDOR_DATA_SIZE_MAX_TEST(struct mtk_task);
	MTK_VENDOR_DATA_SIZE_TEST(struct mtk_tg, struct task_group);
	/* 运行期守卫：尾部不足 ⇒ pr_err 报明 + 本族（scheduler.o 全部钩子/节点）不注册。 */
	if (MTK_TASK_DATA_ABSENT()) {
		mtk_task_data_guard_fail("mtk_sched");
		return -ENODEV;
	}

	/* build cpu_array for hints-based gear search*/
	init_cpu_array();
	build_cpu_array();
	init_gear_hints();

	init_updown_migration();
	init_percore_l3_bw();
	init_dsu_pwr_enable();
	ret = init_sched_common_sysfs();
	if (ret)
		return ret;

	ret = init_share_buck();
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_MTK_SCHED_VIP_TASK)
	vip_init();
#endif

	init_skip_hiIRQ();
	init_rt_aggre_preempt();

#if IS_ENABLED(CONFIG_MTK_SCHED_FAST_LOAD_TRACKING)
	init_flt_platform();
	flt_init_res();
#endif

#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
	group_init();
#endif
	sched_asym_cpucapacity_init();

	get_most_powerful_pd_and_util_Th();

	mtk_sched_trace_init();

	ret = adpf_register_callback(sched_adpf_callback);
	if (ret)
		pr_info("register adpf_register_callback failed\n");

#if IS_ENABLED(CONFIG_MTK_CORE_PAUSE)
	sched_pause_init();
#endif

#if IS_ENABLED(CONFIG_MTK_IRQ_MONITOR_DEBUG)
	sched_irq_mon_init();
#endif


#if IS_ENABLED(CONFIG_MTK_EAS)
	mtk_freq_limit_notifier_register();

	soft_affinity_init();

	/*
	 * rodin 6.18：init_sram_info 失败不再终止本 init——它只影响
	 * thermal-info SRAM 映射（sram_base_addr 树内只写不读），原实现
	 * 的硬失败会跳过其后的全部 hook 注册与 /proc/easmgr 节点创建。
	 */
	ret = init_sram_info();
	if (ret)
		pr_info("init_sram_info failed, ret=%d (thermal-info SRAM unmapped, continue)\n", ret);

	/*
	 * rodin 6.18：android_rvh_find_busiest_group 与 android_rvh_find_energy_efficient_cpu
	 * 是 port 树内核兼容层（7747f66eeaf2）新加进 include/trace/hooks/sched.h 的 MTK 私有
	 * rvh 钩子：本树内核（纯上游 android17-6.18-lts）既无声明也无调用点（调用点在 port 树
	 * kernel/sched/fair.c:11744 / 8680）⇒ 注册函数不存在、回调在本树内永不会被触发，
	 * 此处不再注册（本树内行为等效：调用点缺失时回调同样是死码）。回调本体保留：
	 * mtk_find_energy_efficient_cpu 仍被 vip.c:1343 直接调用，mtk_find_busiest_group 作为
	 * EAS 组选择实现留待钩子可用时复用。
	 */
	ret = register_trace_android_rvh_can_migrate_task(
			mtk_can_migrate_task, NULL);
	if (ret)
		pr_info("register android_rvh_can_migrate_task failed\n");

	ret = register_trace_android_rvh_cpu_overutilized(
			mtk_cpu_overutilized, NULL);
	if (ret)
		pr_info("register trace_android_rvh_cpu_overutilized failed\n");


	ret = register_trace_android_rvh_tick_entry(
			mtk_tick_entry, NULL);
	if (ret)
		pr_info("register android_rvh_tick_entry failed\n");


	ret = register_trace_android_vh_set_wake_flags(
			mtk_set_wake_flags, NULL);
	if (ret)
		pr_info("register android_vh_set_wake_flags failed\n");

	ret = register_trace_pelt_rt_tp(mtk_pelt_rt_tp, NULL);
	if (ret)
		pr_info("register mtk_pelt_rt_tp hooks failed, returned %d\n", ret);

	ret = register_trace_android_rvh_schedule(mtk_sched_switch, NULL);
	if (ret)
		pr_info("register mtk_sched_switch hooks failed, returned %d\n", ret);

	ret = register_trace_android_rvh_update_misfit_status(mtk_update_misfit_status, NULL);
	if (ret)
		pr_info("register mtk_update_misfit_status hooks failed, returned %d\n", ret);

#if IS_ENABLED(CONFIG_MTK_NEWIDLE_BALANCE)
	ret = register_trace_android_rvh_sched_newidle_balance(
			mtk_sched_newidle_balance, NULL);
	if (ret)
		pr_info("register android_rvh_sched_newidle_balance failed\n");
#endif

	pr_debug(TAG"Start to init eas_ioctl driver\n");
	parent = proc_mkdir("easmgr", NULL);
	pe = proc_create("eas_ioctl", 0660, parent, &eas_Fops);
	if (!pe) {
		pr_debug(TAG"%s failed with %d\n",
				"Creating file node ",
				ret);
		ret = -ENOMEM;
		goto out_wq;
	}

#endif

	ret = register_trace_android_rvh_cpu_util_cfs_boost(mtk_cpu_util_cfs_boost_hook, NULL);
	if (ret)
		pr_info("register mtk_cpu_util_cfs_boost_hook hooks failed, returned %d\n", ret);

	ret = register_trace_android_vh_scheduler_tick(hook_scheduler_tick, NULL);
	if (ret)
		pr_info("scheduler: register scheduler_tick hooks failed, returned %d\n", ret);

	ret = register_trace_android_rvh_after_enqueue_task(mtk_hook_after_enqueue_task, NULL);
	if (ret)
		pr_info("register android_rvh_after_enqueue_task failed, returned %d\n", ret);

	ret = register_trace_android_rvh_select_task_rq_rt(mtk_select_task_rq_rt, NULL);
	if (ret)
		pr_info("register mtk_select_task_rq_rt hooks failed, returned %d\n", ret);


	ret = register_trace_android_rvh_find_lowest_rq(mtk_find_lowest_rq, NULL);
	if (ret)
		pr_info("register find_lowest_rq hooks failed, returned %d\n", ret);

	ret = register_trace_android_vh_sched_pelt_multiplier(mtk_sched_pelt_multiplier, NULL);
	if (ret)
		pr_info("register mtk_sched_pelt_multiplier hooks failed, returned %d\n", ret);

	ret = register_trace_android_vh_dump_throttled_rt_tasks(throttled_rt_tasks_debug, NULL);
	if (ret)
		pr_info("register dump_throttled_rt_tasks hooks failed, returned %d\n", ret);

	ret = register_trace_android_rvh_post_init_entity_util_avg(
		mtk_post_init_entity_util_avg, NULL);
	if (ret)
		pr_info("register mtk_post_init_entity_util_avg hooks failed, returned %d\n", ret);

#if IS_ENABLED(CONFIG_DETECT_HUNG_TASK)
	//ret = register_trace_android_vh_check_uninterrupt_tasks(mtk_check_d_tasks, NULL);
	if (ret)
		pr_info("register mtk_check_d_tasks hooks failed, returned %d\n", ret);
#endif
	ret = register_trace_android_vh_setscheduler_uclamp(mtk_setscheduler_uclamp, NULL);
	if (ret)
		pr_info("register mtk_setscheduler_uclamp hooks failed, returned %d\n", ret);

	ret = register_trace_android_rvh_set_cpus_allowed_ptr(mtk_set_cpus_allowed_ptr, NULL);
	if (ret)
		pr_info("register mtk_set_cpus_allowed_ptr hooks failed, returned %d\n", ret);

#if IS_BUILTIN(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
	ret = register_trace_android_rvh_build_perf_domains(
			mtk_eas_bridge_build_perf_domains, NULL);
	if (ret)
		pr_info("register android_rvh_build_perf_domains failed, returned %d\n", ret);
#endif

out_wq:
	return ret;

}

static void __exit mtk_scheduler_exit(void)
{
	mtk_sched_trace_exit();
	unregister_trace_android_vh_scheduler_tick(hook_scheduler_tick, NULL);
	cleanup_sched_common_sysfs();
#if IS_ENABLED(CONFIG_MTK_SCHED_FAST_LOAD_TRACKING)
	exit_flt_platform();
#endif

#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
	group_exit();
#endif
	free_cpu_array();
}

#if IS_BUILTIN(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
vseq_late_initcall(mtk_scheduler_init);
#else
vseq_module_init(mtk_scheduler_init);
#endif
module_exit(mtk_scheduler_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek scheduler");
MODULE_AUTHOR("MediaTek Inc.");
