/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM metis

#if !defined(_XIAOMI_METIS_FINAL_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _XIAOMI_METIS_FINAL_TRACE_H
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include "metis_common.h"
#include "metis-final-control.h"

/* Events with live emitters only. Field types/lengths/signedness and print
 * formats come from relocated original .data descriptors; assignment paths
 * are recovered from actual raw-event bodies. The stock pick-side, deadline
 * and iowait events died with their only emitters and are dropped.
 * Fixed-width string reads and unterminated arrays retain stock behavior. */

/* Original raw 0011215c; .data fields0x478, print0x5b0. */
TRACE_EVENT(metis_set_biner_alloc_new_buf_vip,
	TP_PROTO(struct task_struct *task, struct task_struct *other, int duration, u32 flags),
	TP_ARGS(task, other, duration, flags),
	TP_STRUCT__entry(
		__field(pid_t, curr_pid)
		__array(char, curr_comm, TASK_COMM_LEN)
		__field(pid_t, t_tsk_pid)
		__array(char, t_tsk_comm, TASK_COMM_LEN)
		__field(unsigned int, msec_jif)
		__field(int, flag)
	),
	TP_fast_assign(
		__entry->curr_pid = task->pid;
		memcpy(__entry->curr_comm, task->comm, TASK_COMM_LEN);
		__entry->t_tsk_pid = other->pid;
		memcpy(__entry->t_tsk_comm, other->comm, TASK_COMM_LEN);
		__entry->msec_jif = duration;
		__entry->flag = flags;
	),
	TP_printk("metis_set_biner_alloc_new_buf_vip : curr_pid=%d curr_comm=%s t_tsk_pid=%d t_tsk_comm=%s boost_ms_jif=%d flag=%x", __entry->curr_pid, __entry->curr_comm, __entry->t_tsk_pid, __entry->t_tsk_comm, __entry->msec_jif, __entry->flag)
);

/* Original raw 001125d4; .data fields0x880, print0x9b8. */
TRACE_EVENT(lock_spin_dur,
	TP_PROTO(struct task_struct *task, bool taken, u64 elapsed, const char *type, bool vip),
	TP_ARGS(task, taken, elapsed, type, vip),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__array(char, type, TASK_COMM_LEN)
		__field(bool, taken)
		__field(u64, dur)
		__field(bool, vip)
	),
	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		memcpy(__entry->type, type, TASK_COMM_LEN);
		__entry->taken = taken;
		__entry->dur = elapsed;
		__entry->vip = vip;
	),
	TP_printk("[vip %d]tsk %d %s spin finished with lock taken %d, lock type %s, dur %llu ", __entry->vip, __entry->pid, __entry->comm, __entry->taken, __entry->type, __entry->dur)
);

/* Moved from the dropped metis-lowlt-trace.h (CREATE_TRACE_POINTS now here).
 * Original raw event metadata; stock reserves str_data but never assigns
 * or prints it, and keeps the fixed 64-byte string read for type != 'C'. */
TRACE_EVENT(metis_tracing_mark_write,
	TP_PROTO(char type, int tgid, const char *str, int cpu, int data),
	TP_ARGS(type, tgid, str, cpu, data),
	TP_STRUCT__entry(
		__field(char, type)
		__field(int, tgid)
		__array(char, str, 64 + 1)
		__field(int, str_data)
		__field(char, sep)
		__field(int, data)
	),
	TP_fast_assign(
		__entry->type = type;
		__entry->tgid = tgid;
		if (type == 'C') {
			snprintf(__entry->str, 64, "Cpu%d ", cpu);
			strlcat(__entry->str, str, 64 + 1);
			__entry->sep = '|';
		} else {
			memcpy(__entry->str, str, 64);
			__entry->sep = ':';
		}
		__entry->str[64] = '\0';
		__entry->data = data;
	),
	TP_printk("%c|%d|%s%c%d", __entry->type, __entry->tgid,
		  __entry->str, __entry->sep, __entry->data)
);

/* Original raw 00113fc0; .data fields0x23f0, print0x24b0. */
TRACE_EVENT(mi_wakeup_link,
	TP_PROTO(struct task_struct *task, const char *type),
	TP_ARGS(task, type),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__array(char, type, 64)
	),
	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		strscpy(__entry->type, type, 64);
	),
	TP_printk("comm = %s pid = %d type = %s", __entry->comm, __entry->pid, __entry->type)
);

/* Original raw 001146f8; .data fields0x2870, print0x2980. */
TRACE_EVENT(mi_set_binder_vip_task,
	TP_PROTO(struct task_struct *task, struct task_struct *owner, u64 mask),
	TP_ARGS(task, owner, mask),
	TP_STRUCT__entry(
		__field(pid_t, cur_pid)
		__field(pid_t, tsk_pid)
		__field(u64, mask)
		__field(int, cur_prio)
		__field(int, tsk_prio)
	),
	TP_fast_assign(
		__entry->cur_pid = task->pid;
		__entry->tsk_pid = owner->pid;
		__entry->mask = mask;
		__entry->cur_prio = task->prio;
		__entry->tsk_prio = owner->prio;
	),
	TP_printk("%d [%d] set %d [%d] binder vip task, mask %llu", __entry->cur_pid, __entry->cur_prio, __entry->tsk_pid, __entry->tsk_prio, __entry->mask)
);

/* Original raw 001148e4; .data fields0x2a78, print0x2b88. */
TRACE_EVENT(mi_clean_binder_vip_task,
	TP_PROTO(struct task_struct *task, struct task_struct *owner, u64 mask),
	TP_ARGS(task, owner, mask),
	TP_STRUCT__entry(
		__field(pid_t, cur_pid)
		__field(pid_t, tsk_pid)
		__field(u64, mask)
		__field(int, cur_prio)
		__field(int, tsk_prio)
	),
	TP_fast_assign(
		__entry->cur_pid = task->pid;
		__entry->tsk_pid = owner->pid;
		__entry->mask = mask;
		__entry->cur_prio = task->prio;
		__entry->tsk_prio = owner->prio;
	),
	TP_printk("%d [%d] clean %d [%d] binder vip task, mask %llu", __entry->cur_pid, __entry->cur_prio, __entry->tsk_pid, __entry->tsk_prio, __entry->mask)
);

/* Original raw 00114ad0; .data fields0x2c80, print0x2d18. */
TRACE_EVENT(mi_get_futex_owner_pid,
	TP_PROTO(int pid, int owner_pid),
	TP_ARGS(pid, owner_pid),
	TP_STRUCT__entry(
		__field(pid_t, cur)
		__field(pid_t, owner)
	),
	TP_fast_assign(
		__entry->cur = pid;
		__entry->owner = owner_pid;
	),
	TP_printk("curr %d get futex_owner %d from uaddr2", __entry->cur, __entry->owner)
);

/* Original raw 00115044; .data fields0x31e8, print0x32d0. */
TRACE_EVENT(mi_set_mutex_vip_task,
	TP_PROTO(struct task_struct *task, struct task_struct *owner),
	TP_ARGS(task, owner),
	TP_STRUCT__entry(
		__field(pid_t, cur_pid)
		__field(int, cur_prio)
		__field(pid_t, tsk_pid)
		__field(int, tsk_prio)
	),
	TP_fast_assign(
		__entry->cur_pid = task->pid;
		__entry->cur_prio = task->prio;
		__entry->tsk_pid = owner->pid;
		__entry->tsk_prio = owner->prio;
	),
	TP_printk("%d [%d] set %d [%d] mutex vip task", __entry->cur_pid, __entry->cur_prio, __entry->tsk_pid, __entry->tsk_prio)
);

/* Original raw 00115218; .data fields0x33b0, print0x3510. */
TRACE_EVENT(mi_lock_info,
	TP_PROTO(struct task_struct *task, bool vip, const char *type, int owner_pid, int owner_prio, bool owner_vip),
	TP_ARGS(task, vip, type, owner_pid, owner_prio, owner_vip),
	TP_STRUCT__entry(
		__field(pid_t, pid)
		__array(char, comm, TASK_COMM_LEN)
		__field(bool, curr_is_vip)
		__array(char, block_type, 32)
		__field(int, owner_pid)
		__field(int, owner_prio)
		__field(bool, owner_is_vip)
	),
	TP_fast_assign(
		__entry->pid = task->pid;
		memcpy(__entry->comm, task->comm, TASK_COMM_LEN);
		__entry->curr_is_vip = vip;
		memcpy(__entry->block_type, type, 32);
		__entry->owner_pid = owner_pid;
		__entry->owner_prio = owner_prio;
		__entry->owner_is_vip = owner_vip;
	),
	TP_printk("current tsk:%s[%d][%d] blocked by %s. lock owner pid:%d, prio:%d, is vip:%d", __entry->comm, __entry->pid, __entry->curr_is_vip, __entry->block_type, __entry->owner_pid, __entry->owner_prio, __entry->owner_is_vip)
);

/* Original raw 00115b80; .data fields0x3ba8, print0x3cb8. */
TRACE_EVENT(mi_restore_lock_vip_task,
	TP_PROTO(struct task_struct *task, struct task_struct *owner, const char *type),
	TP_ARGS(task, owner, type),
	TP_STRUCT__entry(
		__field(pid_t, cur_pid)
		__field(int, cur_prio)
		__field(pid_t, tsk_pid)
		__field(int, tsk_prio)
		__array(char, type, 64)
	),
	TP_fast_assign(
		__entry->cur_pid = task->pid;
		__entry->cur_prio = task->prio;
		__entry->tsk_pid = owner->pid;
		__entry->tsk_prio = owner->prio;
		memcpy(__entry->type, type, 64);
	),
	TP_printk("%d [%d] restore %d [%d] lock vip task, type %s", __entry->cur_pid, __entry->cur_prio, __entry->tsk_pid, __entry->tsk_prio, __entry->type)
);

#endif
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE metis-final-trace
#include <trace/define_trace.h>
