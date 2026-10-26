// SPDX-License-Identifier: GPL-2.0-only
/* Cold-start wakeup marking: PR_SET_NAME table matching, fork-time VIP
 * inheritance, futex-wait cleanup and futex plist priority. The stock
 * private select-rq/enqueue/dequeue hooks are dropped: their callers were
 * closed-scheduler call sites absent on 6.18. */
#include <linux/errno.h>
#include <linux/futex.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/sys.h>
#include "metis_state.h"
#include "metis_wakeup.h"

#include "metis-final-trace.h"

bool metis_wakeup_futex_plist_insert(int *priority)
{
	struct task_struct *task;
	bool active;

	if (!metis_wakeup_enable)
		return false;
	task = current;
	active = in_coldstart_period();
	if (task && active && task->pid == cold_start_app_tgid) {
		*priority = 99;
		return true;
	}
	return false;
}

void set_coldstart_wakeup_task(struct task_struct *task)
{
	struct mi_task_struct *state;
	char first, *pattern;
	size_t remaining, length;
	u32 old, marked, inherited;
	bool active;

	if (!metis_wakeup_enable)
		return;
	active = in_coldstart_period();
	if (!task || !active || task->tgid != cold_start_app_tgid)
		return;
	first = task->comm[0];
	state = get_mi_task_struct(task);
	if (!state)
		return;
	if (first == 'R' && !strcmp(task->comm, "RendererThread")) {
		state->unknown_c8 |= 0x100;
		trace_mi_wakeup_link(task, "set wakeup vip task by fork");
		return;
	}
	pattern = app_wakeup_task;
	remaining = sizeof(app_wakeup_task);
	do {
		/* Stock FORTIFY uses remaining table size, not a 16-byte slot. */
		length = strnlen(pattern, remaining);
		if (length > remaining)
			BUG();		/* stock fortify_panic("strnlen") */
		if (length >= remaining)
			BUG();	/* stock fortify_panic("__fortify_strlen") */
		if (!length)
			break;
		if ((u8)first <= (u8)pattern[0]) {
			if ((u8)first < (u8)pattern[0])
				break;
			if (strstr(task->comm, pattern)) {
				old = state->unknown_c8;
				marked = (old & 0xffffffd7U) | 8;
				state->unknown_c8 = marked;
				if (first == 'S' && strstr(task->comm, "SharedPreferenc")) {
					marked = (old & 0xffffffd7U) | 0x18;
					inherited = 0x30;
					state->unknown_c8 = marked;
				} else {
					inherited = 0x20;
				}
				if (old & 4) {
					state->unknown_c8 = marked | 0x40;
					trace_mi_wakeup_link(task, "set wakeup crit task by fork");
				} else if (old & 2) {
					state->unknown_c8 = marked | inherited;
					trace_mi_wakeup_link(task, "set wakeup task by fork");
				}
				state->unknown_c8 &= 0xfffffff8U;
				if (metis_wakeup_debug)
					pr_info("metis-fas: metis wakeup debug: set wakeup task - %s(%d)\n",
						task->comm, task->pid);
				return;
			}
		}
		remaining -= 16;
		pattern += 16;
	} while (remaining);
	state->unknown_c8 &= 0xffffffd8U;
}

void wake_up_new_task_hook(void *unused, struct task_struct *task)
{
	struct mi_task_struct *state;
	u32 inherited;
	bool active;

	if (!metis_wakeup_enable)
		return;
	active = in_coldstart_period();
	if (!task || !active || task->tgid != cold_start_app_tgid)
		return;
	state = get_mi_task_struct(task);
	if (!state || !metis_wakeup_enable)
		return;
	if (cold_start_ui_task(current)) {
		inherited = 0x24;
	} else {
		if (!cold_start_wakeup_task(current))
			return;
		inherited = 0x22;
	}
	state->unknown_c8 |= inherited;
}

void metis_wakeup_try_to_wakeup(struct task_struct *task)
{
	struct task_struct *waker;
	struct mi_task_struct *state;
	bool active;

	if (!metis_wakeup_enable)
		return;
	waker = current;
	active = in_coldstart_period();
	if (waker && active && waker->pid == cold_start_app_tgid) {
		state = get_mi_task_struct(task);
		if (state && (state->unknown_c8 & 8)) {
			state->unknown_c8 |= 0x40;
			trace_mi_wakeup_link(task, "set wakeup crit task by link");
		}
		return;
	}
	if (!metis_wakeup_enable ||
	    !(cold_start_wakeup_link_task(waker) || cold_start_ui_pipeline(waker)))
		return;
	if (cold_start_ui_task(task)) {
		state = get_mi_task_struct(waker);
		if (state) {
			state->unknown_c8 &= 0xffffffdfU;
			trace_mi_wakeup_link(waker, "clear wakeup task by ui");
		}
		return;
	}
	state = get_mi_task_struct(task);
	if (!state || !(state->unknown_c8 & 8))
		return;
	if (state->unknown_c8 & 0x10) {
		state->unknown_c8 |= 0x40;
		trace_mi_wakeup_link(task, "set wakeup crit task by link");
	} else {
		state->unknown_c8 |= 0x20;
		trace_mi_wakeup_link(task, "set wakeup task by link");
	}
	state = get_mi_task_struct(waker);
	if (state) {
		state->unknown_c8 &= 0xffffffdfU;
		trace_mi_wakeup_link(waker, "clear wakeup task by link");
	}
}

void metis_wakeup_syscall_prctl_finished_hook(void *unused, int option,
		struct task_struct *task)
{
	if (option == 15)
		set_coldstart_wakeup_task(task);
}

void metis_wakeup_futex_wait_hook(void *unused, u32 __user *uaddr, int cmd,
		unsigned int *flags, u32 __user *uaddr2)
{
	struct task_struct *task;
	struct mi_task_struct *state;

	if (cmd || uaddr2 || !metis_wakeup_enable || !in_coldstart_period())
		return;
	task = current;
	state = get_mi_task_struct(task);
	if (!state || !(state->unknown_c8 & 0xe0))
		return;
	state = get_mi_task_struct(task);
	if (state) {
		state->unknown_c8 &= 0xffffff1fU;
		trace_mi_wakeup_link(task, "clear wakeup all task by futex wait");
	}
}

int set_metis_wakeup_task(const char *value, const struct kernel_param *param)
{
	char *allocation, *cursor, *token;
	int tid = 0, tokens_left = 16;
	u32 slot = 0;
	size_t length;

	allocation = kstrdup(value, GFP_KERNEL);
	if (!allocation)
		return -ENOMEM;
	cursor = allocation;
	token = strsep(&cursor, ",");
	if (!token || sscanf(token, "%d ", &tid) != 1) {
		kfree(allocation);
		return -EINVAL;
	}
	if (tid > 0) {
		if (metis_wakeup_debug)
			pr_info("metis-fas: metis wakeup debug: ui thread pid = %d\n", tid);
		token = strsep(&cursor, ",");
		while (token) {
			if (!--tokens_left)
				break;
			if (*token && strlen(token) < 16 && (s32)slot < 15) {
				BUG_ON(slot > 15);
				length = strlen(token);
				if (240UL - (unsigned long)slot * 16 < length + 1)
					BUG();		/* stock fortify_panic("strcpy") */
				memcpy(app_wakeup_task + slot * 16, token, length + 1);
				if (metis_wakeup_debug)
					pr_info("metis-fas: metis wakeup debug: child thread %d: %s\n", slot,
						app_wakeup_task + slot * 16);
				slot++;
			}
			token = strsep(&cursor, ",");
		}
	}
	kfree(allocation);
	return set_cold_start_app_tgid(tid);
}

int metis_wakeup_init(void)
{
	register_trace_android_vh_syscall_prctl_finished(
		metis_wakeup_syscall_prctl_finished_hook, NULL);
	register_trace_android_rvh_wake_up_new_task(wake_up_new_task_hook, NULL);
	register_trace_android_vh_do_futex(metis_wakeup_futex_wait_hook, NULL);
	return 0;
}

void metis_wakeup_exit(void)
{
	unregister_trace_android_vh_syscall_prctl_finished(
		metis_wakeup_syscall_prctl_finished_hook, NULL);
	unregister_trace_android_vh_do_futex(metis_wakeup_futex_wait_hook, NULL);
	tracepoint_synchronize_unregister();
}
