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
		pr_info("metis-link: pcpu_rwsem lock wq add, cs_stat_type: %d\n", (s32)stat);
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
