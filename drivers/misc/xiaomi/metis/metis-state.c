// SPDX-License-Identifier: GPL-2.0-only
/* VIP detection aggregation and state predicates. The stock fas suspend
 * clock and the foreground-uid C API are gone: 6.18 callers feed the uid
 * through /proc/fas and the metis ioctl instead. */
#include <linux/kernel.h>
#include <linux/sched.h>
#include "metis_state.h"

unsigned int cpu_to_cluster(unsigned int cpu)
{
	unsigned int cluster = get_cluster_type(cpu);

	return cluster < 4 ? cluster : 0;
}

bool mi_vip_task(struct task_struct *task)
{
	metis_vip_detect_fn detect;
	unsigned int flags = 0;
	int i;

	/* All three callbacks run; a true earlier result does not short-circuit.
	 * Stock's callback KCFI family is 0x26f27c13, also used by is_qos_task. */
	for (i = 0; i < 3; i++) {
		detect = vip_task_detect[i].detect;
		if (detect)
			flags |= detect(task);
	}
	return flags & 1;
}

void mi_set_fas_req(struct mi_task_struct *state)
{
	struct task_struct *task = state->task;

	/* No invented NULL guard: stock dereferences the state on entry. */
	if (!(state->flags_0xc0 & 0x40) && !mi_vip_task(task)) {
		if (metis_debug)
			pr_info("metis-fas: Task %s %d request to mark as vip\n",
				task->comm, task->pid);
		state->flags_0xc0 |= 0x40;
		return;
	}
	if (metis_debug)
		pr_info("metis-fas: Task %s %d alredy request to mark as vip\n",
			task->comm, task->pid);
}

static __always_inline void metis_invalid_slot(void)
{
	/* Negative signed IDs pass the first check, then stock traps unsigned. */
	asm volatile("brk #0x5512");
	__builtin_unreachable();
}

void register_vip_task_detect(unsigned int module, metis_vip_detect_fn detect)
{
	if ((int)module < 3) {
		pr_info("metis-fas: %d, %s now\n", module, "register_vip_task_detect");
		if (module >= 3)
			metis_invalid_slot();
		vip_task_detect[module].detect = detect;
	} else {
		pr_err("metis-fas: invalid mod register\n");
	}
}

void unregister_vip_task_detect(unsigned int module)
{
	if ((int)module < 3) {
		pr_info("metis-fas: %d, %s now\n", module, "unregister_vip_task_detect");
		if (module >= 3)
			metis_invalid_slot();
		vip_task_detect[module].detect = NULL;
	} else {
		pr_err("metis-fas: invalid mod unregister\n");
	}
}

static unsigned int metis_request_index(struct mi_task_struct *state)
{
	struct mi_task_struct *leader;
	unsigned int index;

	if (!state)
		return UINT_MAX;
	index = MI_FIELD(state, u32, 0x2f8);
	if (index != UINT_MAX)
		return index;
	if (!state->task)
		return UINT_MAX;
	leader = get_mi_task_struct(state->task->group_leader);
	if (!leader)
		return UINT_MAX;
	return MI_FIELD(leader, u32, 0x2fc);
}

bool is_static_vip(struct mi_task_struct *state)
{
	unsigned int index = metis_request_index(state);

	return index < 7 && (important_tasks[index].raw[0x18] & 2);
}

bool is_top_app_vip(struct mi_task_struct *state)
{
	unsigned int index = metis_request_index(state);

	return index < 7 && (important_tasks[index].raw[0x18] & 1);
}

const char *mi_link_mask_to_type(long mask)
{
	switch (mask) {
	case 0x2000: return "lock";
	case 0x4000: return "binder";
	case 0x8000: return "rt-binder-free";
	case 0x20000: return "art-mutex";
	case 0x4000000: return "flag_io";
	case 0x8000000: return "mmap_lock";
	case 0x0c0ae000: return "link";
	default: return "unknown";
	}
}
