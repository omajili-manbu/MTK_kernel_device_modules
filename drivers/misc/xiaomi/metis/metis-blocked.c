// SPDX-License-Identifier: GPL-2.0-only
/* PARTIAL blocked/OEM owner state. No registrations, exports or lifecycle. */
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/cpu.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/tracepoint.h>
#include "metis_blocked.h"
#include "metis_classify.h"
#include "metis-final-trace.h"

/* Only this event is defined here; the shared systrace event lives in
 * metis-final-trace.h. */
#define CREATE_TRACE_POINTS
#include "metis-blocked-trace.h"
#undef CREATE_TRACE_POINTS

#define METIS_RWSEM_TAG_UNKNOWN 0xab9f19ab00000000ULL

/* rodin 6.18（2026-10-27 定案）：mutex oem payload 的读者全树死门控
 * （mutex_zero_unknown 全零门 + protect_cs 分支 + (u64)jiffies>>63 恒零，
 * unknown_04 零读者、0x149f 值零比较；OSS 原形同构）——"VIP 锁握手接力"
 * 的 mutex 侧从未活体，活体接力只在 rwsem 侧（16B 原生槽 magic 门）。
 * 锁侧 payload 与 1024 桶侧表（全局 spin_lock_irqsave + freelist +
 * lock_task_exit 清槽钩）整体退役；对应崩类随之结构性消失。
 * 内核槽 android_oem_data1 高 u32 = xr_qi waiter 计数（qi_mutex_init
 * 整槽清零自负），低 u32 退役后无主。 */

static struct metis_rwsem_oem_unknown *rwsem_oem_unknown(struct rw_semaphore *sem)
{
	return sem ? (void *)sem->android_oem_data1 : NULL;
}

/* Load/validate RWSEM payload. Initial tagging happens only when protect_cs
 * is nonzero at this later read; callers do not all gate on protect_cs. */
static bool rwsem_word_unknown(struct metis_rwsem_oem_unknown *data, u64 *word)
{
	if (!data)
		return false;
	*word = data->unknown_00;
	if (*word)
		return (*word >> 32) == 0xab9f19ab;
	if (data->deadline_08)
		return false;
	if (cs_debug)
		pr_info("metis-cs: %s: rwsem oem data is zero\n",
			"__mi_rwsem_data_valid");
	if (protect_cs) {
		data->deadline_08 = 0;
		*word = METIS_RWSEM_TAG_UNKNOWN;
	} else {
		*word = data->unknown_00;
	}
	return true;
}

/* Stock jump-table key +0x2b0 first gates on mi_rwsem_blocked_info;
 * only then does key +0x2c0 gate the systrace E. These are separate gates. */
static void rwsem_clear_trace_unknown(void)
{
	if (tracepoint_enabled(mi_rwsem_blocked_info)) {
		int tgid = current->tgid;

		if (tracepoint_enabled(metis_tracing_mark_write) &&
		    cpu_online(raw_smp_processor_id()))
			trace_metis_tracing_mark_write('E', tgid, "", 0, 0);
	}
}

/* Static-key tail omitted by Ghidra: it deliberately dereferences sem even
 * when the functional state path accepted a NULL sem. No lifetime fix added. */
static void rwsem_record_trace_unknown(struct rw_semaphore *sem, bool reader)
{
	struct task_struct *owner;
	unsigned long owner_word;
	char owner_type;
	char buffer[64] = { 0 };
	bool vip, spinnable;
	int pid;

	if (!tracepoint_enabled(mi_rwsem_blocked_info))
		return;
	owner_word = atomic_long_read(&sem->owner);
	owner = (struct task_struct *)(owner_word & ~3UL);
	owner_type = owner_word & 1 ? 'R' : owner ? 'W' : 'N';
	vip = mi_vip_task(current);
	if (!vip)
		vip = mi_link_vip_task(current);
	spinnable = !(owner_word & 2);
	pid = !(owner_word & 1) && owner ? owner->pid : 0;
	snprintf(buffer, 63,
		 "D: rwsem ownerpid %d type %c cur vip %d r %d sp %d %p",
		 pid, owner_type, vip, reader, spinnable, sem);
	/* Stock reloads owner pid after formatting, without taking a reference. */
	pid = !(owner_word & 1) && owner ? owner->pid : 0;
	if (tracepoint_enabled(mi_rwsem_blocked_info) &&
	    cpu_online(raw_smp_processor_id())) {
		trace_mi_rwsem_blocked_info(pid, owner_type, reader, spinnable, vip);
	}
	if (tracepoint_enabled(metis_tracing_mark_write) &&
	    cpu_online(raw_smp_processor_id()))
		trace_metis_tracing_mark_write('B', current->tgid, buffer, 0, 0);
}

void record_mutex_blocked_info(struct task_struct *task, struct mutex *lock)
{
	struct mi_task_struct *state;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct mutex *, 0x2d8) = lock;
	MI_FIELD(state, u32, 0x2b4) |= 0x100;
	if (!mi_vip_task(task) && !mi_link_vip_task(task))
		return;
	MI_FIELD(state, u32, 0x2b4) |= 0x200;
}

void clear_mutex_blocked_info(struct task_struct *task, struct mutex *lock)
{
	struct mi_task_struct *state;
	u32 flags;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	flags = MI_FIELD(state, u32, 0x2b4);
	MI_FIELD(state, struct mutex *, 0x2d8) = NULL;
	MI_FIELD(state, u32, 0x2b4) = flags & ~0x100;
	if (!(flags & 0x200))
		return;
	/* rodin 6.18（2026-10-27）：锁侧 payload 已退役，仅保留 per-task 面。 */
	MI_FIELD(state, u32, 0x2b4) = flags & ~0x300;
}

void set_rwsem_vip_blocked(struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	if (!rwsem_word_unknown(data, &word))
		return;
	data->unknown_00 = (word & 0xffffffffffff0000ULL) | 1;
	data->deadline_08 = jiffies + 7;
}

void record_rwsem_blocked_info(struct task_struct *task,
			      struct rw_semaphore *sem,
			      metis_rwsem_reader_flag_unknown reader)
{
	struct mi_task_struct *state;
	bool vip;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct rw_semaphore *, 0x2d0) = sem;
	MI_FIELD(state, u32, 0x2b4) |= 0x40;
	vip = mi_vip_task(task);
	if (!vip)
		vip = mi_link_vip_task(task);
	if (sem && vip)
		set_rwsem_vip_blocked(sem);
	rwsem_record_trace_unknown(sem, reader);
}

void clear_rwsem_blocked_info(struct task_struct *task, struct rw_semaphore *sem)
{
	struct mi_task_struct *state;
	struct metis_rwsem_oem_unknown *data;
	u64 word;
	bool vip;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct rw_semaphore *, 0x2d0) = NULL;
	MI_FIELD(state, u32, 0x2b4) &= ~0x40;
	vip = mi_vip_task(task);
	if (!vip)
		vip = mi_link_vip_task(task);
	if (!sem || !vip)
		goto out_trace;
	data = rwsem_oem_unknown(sem);
	word = data->unknown_00;
	if (!word) {
		if (data->deadline_08)
			goto out_trace;
		if (cs_debug)
			pr_info("metis-cs: %s: rwsem oem data is zero\n",
				"__mi_rwsem_data_valid");
	} else if ((word >> 32) != 0xab9f19ab) {
		goto out_trace;
	}
	/* Clear drops writer-owned and all other low bits, keeping only tag.
	 * Unlike rwsem_word_unknown, it does not recheck protect_cs here. */
	data->unknown_00 = METIS_RWSEM_TAG_UNKNOWN;
	data->deadline_08 = 0;
out_trace:
	rwsem_clear_trace_unknown();
}

void mi_record_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data;
	u64 word;

	/* Current task is checked before the sem NULL test, as in stock. */
	if (mi_task_exiting(current) || !sem || (current->flags & PF_EXITING))
		return;
	data = rwsem_oem_unknown(sem);
	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word | 0x10000;
}

void mi_clear_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word & ~0x10000ULL;
}

void mi_record_rwsem_reader_owned_hook(void *unused, struct rw_semaphore *sem,
				      struct list_head *wlist)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	/* Stock ignores wlist and clears its writer-owned bit; no reader count. */
	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word & ~0x10000ULL;
}

void mi_mutex_wait_finish_hook(void *unused, struct mutex *lock)
{
	clear_mutex_blocked_info(current, lock);
}

void mi_rwsem_read_wait_finish_hook(void *unused, struct rw_semaphore *sem)
{
	clear_rwsem_blocked_info(current, sem);
}
