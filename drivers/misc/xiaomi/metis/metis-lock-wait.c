// SPDX-License-Identifier: GPL-2.0-only
/* PARTIAL: eleven stock lock-wait producer bodies, no registrations/owners. */
#include <linux/cgroup-defs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/percpu-rwsem.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/tracepoint.h>
/* Canonical current native definition; needs the core kernel include path,
 * as existing metis sources already need for sched/sched.h. No copied layout. */
#include <locking/mutex.h>
#include "../xr_qi/qi_provider.h"
#include "metis_blocked.h"
#include "metis_coldstart.h"
#include "metis_common.h"
#include "metis_lock_wait.h"

#include "metis-final-trace.h"

/* rodin 6.18（2026-11-05）：stock 由 mi_locking 注册本钩，6.18 无该模块，
 * 改由 metis 自挂——metis_link_init 注册 android_vh_alter_mutex_list_add
 * 使本插队体实际接活。stock 首步的 mutex_reset_data_no_waiter 写
 * android_oem_data1，与 xr_qi waiter 计数共租冲突（2026-10-27 定案），
 * 不重放；VIP 插队主体为纯链表手术，保留。 */
void mi_mutex_list_add_hook(void *unused, struct mutex *lock,
			    struct mutex_waiter *waiter,
			    struct list_head *list, bool *already_on_list)
{
	struct list_head *cursor, *next, *prev, *new;
	struct mutex_waiter *other;
	struct task_struct *task;
	bool vip, link_vip;
	bool inserted = false;

	/* Original dereferences this result pointer before all other guards. */
	if (READ_ONCE(*already_on_list))
		return;
	if ((is_qi_feat_enabled(3) & 1) && is_qos_task(current))
		return;
	/* Original accesses only the low byte at this callsite. */
	if (READ_ONCE(*(u8 *)&cs_stat_type) & BIT(1))
		protect_pre_cs_task(current);
	if (!lock || !waiter || !list)
		return;
	if (!mi_vip_task(current) && !mi_link_vip_task(current))
		return;
	new = &waiter->list;
	/* rodin 6.18（2026-11-03）：重入守卫——alter 钩子对同一 waiter 可能
	 * 二次触发，无守卫重走插入路径会邻位自撞落硬化报告分支、中位重链
	 * 撕裂链表；扫链命中本 waiter 即置 already 返回（链上实达型）。 */
	{
		struct list_head *cur = READ_ONCE(list->prev);

		while (cur && cur != list) {
			if (cur == new) {
				WRITE_ONCE(*already_on_list, true);
				return;
			}
			cur = READ_ONCE(cur->prev);
		}
	}
	cursor = READ_ONCE(list->next);
	for (;;) {
		if (cursor == list) {
			prev = READ_ONCE(list->prev);
			if (new == list || prev == new || READ_ONCE(prev->next) != list) {
				__list_add_valid_or_report(new, prev, list);
			} else {
				WRITE_ONCE(list->prev, new);
				WRITE_ONCE(new->next, list);
				WRITE_ONCE(new->prev, prev);
				WRITE_ONCE(prev->next, new);
				inserted = true;
			}
			break;
		}
		if (!cursor)
			return;
		other = list_entry(cursor, struct mutex_waiter, list);
		task = READ_ONCE(other->task);
		if (!task)
			return;
		/* Stock captures next before predicates, then reloads other->task for
 		 * the second predicate even when the first result was already true. */
		next = READ_ONCE(cursor->next);
		vip = mi_vip_task(task);
		link_vip = mi_link_vip_task(READ_ONCE(other->task));
		if (!vip && !link_vip) {
			prev = READ_ONCE(cursor->prev);
			next = READ_ONCE(prev->next);
			if (READ_ONCE(next->prev) != prev || prev == new || next == new) {
				__list_add_valid_or_report(new, prev, next);
			} else {
				WRITE_ONCE(next->prev, new);
				WRITE_ONCE(new->next, next);
				WRITE_ONCE(new->prev, prev);
				WRITE_ONCE(prev->next, new);
				inserted = true;
			}
			break;
		}
		cursor = next;
	}
	/* rodin 6.18（2026-11-03）：仅实际落链后置 already；硬化报告分支
	 * 未落链不置，放行核心 list_add_tail——静默丢 waiter 会令核心在
	 * 空链取假 waiter（container_of(&lock->wait_list)，字段落在
	 * android_vendor/oem_data 上），与 rwsem 侧 NULL->blocked_lock 同形。 */
	if (inserted)
		WRITE_ONCE(*already_on_list, true);
}

void mi_mutex_wait_start_hook(void *unused, struct mutex *lock)
{
	struct task_struct *task, *owner;
	bool vip, owner_vip;
	int owner_pid, owner_prio;

	if ((is_qi_feat_enabled(3) & 1) && is_qos_task(current))
		return;
	task = current;
	record_mutex_blocked_info(task, lock);
	if (mi_vip_task(task) || mi_link_vip_task(task)) {
		owner = lock ? (void *)((unsigned long)atomic_long_read(&lock->owner) & ~7UL) : NULL;
		if (do_set_lock_vip(owner, task, "mutex") & 1)
			trace_mi_set_mutex_vip_task(task, owner);
	}
	/* Ghidra omitted this entire independent static-key branch. */
	if (!trace_mi_lock_info_enabled())
		return;
	vip = mi_vip_task(task);
	if (!vip)
		vip = mi_link_vip_task(task);
	if (!lock)
		return;
	owner = (void *)((unsigned long)atomic_long_read(&lock->owner) & ~7UL);
	if (!owner || !refcount_read(&owner->usage))
		return;
	/* Preserve the separate usage test followed by unconditional increment. */
	get_task_struct(owner);
	owner_vip = mi_vip_task(owner);
	if (!owner_vip)
		owner_vip = mi_link_vip_task(owner);
	owner_pid = owner->pid;
	owner_prio = owner->prio;
	put_task_struct(owner);
	trace_mi_lock_info(task, vip, "mutex", owner_pid, owner_prio, owner_vip);
}

void mi_rwsem_write_wait_start_hook(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *task = current;
	u64 word;

	if (is_qos_task(task))
		return;
	record_rwsem_blocked_info(task, sem, false);
	if (!sem || (!mi_vip_task(task) && !mi_link_vip_task(task)))
		return;
	if (!((unsigned long)atomic_long_read(&sem->count) & 1))
		return;
	word = READ_ONCE(sem->android_oem_data1[0]);
	if ((word & 0xffffffff00010000ULL) != 0xab9f19ab00010000ULL)
		return;
	do_set_lock_vip((void *)((unsigned long)atomic_long_read(&sem->owner) & ~3UL),
			task, "rwsem");
}

void mi_rwsem_read_wait_start_hook(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *task = current;
	u64 word;

	if (is_qos_task(task))
		return;
	record_rwsem_blocked_info(task, sem, true);
	if (!sem || (!mi_vip_task(task) && !mi_link_vip_task(task)))
		return;
	if (!((unsigned long)atomic_long_read(&sem->count) & 1))
		return;
	word = READ_ONCE(sem->android_oem_data1[0]);
	if ((word & 0xffffffff00010000ULL) != 0xab9f19ab00010000ULL)
		return;
	do_set_lock_vip((void *)((unsigned long)atomic_long_read(&sem->owner) & ~3UL),
			task, "rwsem");
}

/* rodin 6.18：starttime 钩子面无 → 替代钩子传 (sem) 单参，start 语义位消失 */
void mi_rwsem_lock_starttime(void *unused, struct rw_semaphore *sem)
{
	/* rodin 6.18：替代钩子仅在等待起点触发 ⇒ start 恒真（undo 分支由 wait_finish 面覆盖） */
	const unsigned long start = 1;
	u32 stat;
	bool qos;
	struct task_struct *task;

	if (!protect_cs)
		return;
	if (is_qi_feat_enabled(5) & 1) {
		qos = is_qos_task(current);
		if (start && qos)
			return;
	}
	stat = READ_ONCE(cs_stat_type);
	if (!(stat & BIT(0)))
		return;
	if (link_debug > 1)
		pr_info("metis-link: record rwsem lock, cs_stat_type: %d\n", (s32)stat);
	if (sem && (READ_ONCE(cold_start_app_mmlock) == sem ||
		    READ_ONCE(cold_start_sserver_mmlock) == sem ||
		    is_mmaplk_vip_task(current))) {
		task = current;
		if (start)
			coldstart_mark_mmlock_holder(task);
		else
			coldstart_unmark_mmlock_holder(task);
		clean_mmaplk_vip_task(task);
		return;
	}
	mi_record_lock_starttime(current, start, "rwsem");
}

/* rodin 6.18（2026-11-05）：android_vh_rwsem_write_wait_start 双 handler 合并本
 * 入口单注册，序即原注册序（先 mi_rwsem_write_wait_start_hook 后
 * mi_rwsem_lock_starttime），每事件省一次 tracepoint 分派。 */
void mi_rwsem_write_wait_start(void *unused, struct rw_semaphore *sem)
{
	mi_rwsem_write_wait_start_hook(unused, sem);
	mi_rwsem_lock_starttime(unused, sem);
}

void mi_mutex_lock_starttime(void *unused, struct mutex *lock)
{
	const unsigned long start = 1;
	struct task_struct *task;
	u32 stat;

	if (!protect_cs)
		return;
	if (start && (is_qi_feat_enabled(3) & 1) && is_qos_task(current))
		return;
	stat = READ_ONCE(cs_stat_type);
	if (!(stat & BIT(1)))
		return;
	if (link_debug > 1)
		pr_info("metis-link: record mutex lock, cs_stat_type: %d\n", (s32)stat);
	task = current;
	mi_record_lock_starttime(task, start, "mutex");
	/* rodin 6.18（2026-10-27）：vip_blocked_on_mutex 读者死门控（payload 无消费
	 * 者），其后 0x2000/50-tick 接力块整体退役 —— stock 本就从未活体。 */
}

/* rodin 6.18（2026-11-05）：android_vh_mutex_wait_start 双 handler 合并本
 * 入口单注册，序即原注册序（先 mi_mutex_wait_start_hook 后
 * mi_mutex_lock_starttime），每事件省一次 tracepoint 分派。 */
void mi_mutex_wait_start(void *unused, struct mutex *lock)
{
	mi_mutex_wait_start_hook(unused, lock);
	mi_mutex_lock_starttime(unused, lock);
}

void mi_rtmutex_lock_starttime(void *unused, struct rt_mutex *lock)
{
	const unsigned long start = 1;
	u32 stat;

	if (!protect_cs)
		return;
	stat = READ_ONCE(cs_stat_type);
	if (!(stat & BIT(2)))
		return;
	if (link_debug > 1)
		pr_info("metis-link: record rtmutex lock, cs_stat_type: %d\n", (s32)stat);
	mi_record_lock_starttime(current, start, "rtmutex");
}

/* rodin 6.18：替代钩子 = pcpu_rwsem_lock_acquired((sem))，start 语义位消失 */
void mi_pcpu_rwsem_lock_starttime(void *unused, struct percpu_rw_semaphore *sem)
{
	const unsigned long start = 1;
	u32 stat;

	if (!protect_cs)
		return;
	stat = READ_ONCE(cs_stat_type);
	if (!(stat & BIT(3)))
		return;
	if (link_debug > 1)
		pr_info("metis-link: record pcpu_rwsem lock, cs_stat_type: %d\n", (s32)stat);
	mi_record_lock_starttime(current, start, "pcpu_rwsem");
}

void mi_pcpu_rwsem_wq_add(void *unused, struct percpu_rw_semaphore *sem, bool reader)
{
	u32 stat;

	if ((is_qi_feat_enabled(7) & 1) && is_qos_task(current))
		return;
	stat = READ_ONCE(cs_stat_type);
	if (!(stat & BIT(3)))
		return;
	if (link_debug > 1)
		pr_info("metis-link: pcpu_rwsem lock wq add, cs_stat_type: %d\n", (s32)stat);	if (sem == &cgroup_threadgroup_rwsem)
		mark_task_low_lt(get_mi_task_struct(current));
}

void mi_rwsem_write_wait_finish_hook(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *task = current;

	clear_rwsem_blocked_info(task, sem);
	if (is_important_mmlock(sem))
		coldstart_mark_mmlock_holder(task);
}

void mi_lock_rwsem_wake_finish_hook(void *unused, struct rw_semaphore *sem)
{
	struct task_struct *task = current;

	mi_lock_vip_finish_common(task);
	if (is_important_mmlock(sem))
		coldstart_unmark_mmlock_holder(task);
}
