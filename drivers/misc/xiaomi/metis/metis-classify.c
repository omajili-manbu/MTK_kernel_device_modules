/* SPDX-License-Identifier: GPL-2.0-only */
/* CS-protect/dynamic-VIP/mmap-lock classification predicates. The low-LT
 * faces and the deadline trace helpers are dropped with their only readers
 * (stock pick-side queues and never-emitted trace events). */
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include "metis_classify.h"

bool is_dynamic_vip(struct task_struct *task)
{
	struct mi_task_struct *state;
	if (!task)
		return false;
	state = get_mi_task_struct(task);
	return state && (state->flags_0xc0 & 0x800);
}

bool is_mmaplk_vip_task(struct task_struct *task)
{
	struct mi_task_struct *state = get_mi_task_struct(task);
	return state && (MI_FIELD(state, u32, 0x2b4) & 0x800);
}

void set_mmaplk_vip_task(struct mi_task_struct *state)
{
	if (!state)
		return;
	MI_FIELD(state, u32, 0x2b4) |= 0x800;
	if (cs_debug)
		pr_info("metis-cs: mmaplk:%d set mmaplk vip task\n", state->task->pid);
}

void clean_mmaplk_vip_task(struct task_struct *task)
{
	struct mi_task_struct *state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, u32, 0x2b4) &= 0xfffff7ff;
	if (cs_debug)
		pr_info("metis-cs: mmaplk:%d clean mmaplk vip task\n", task->pid);
}
