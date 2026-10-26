// SPDX-License-Identifier: GPL-2.0-only
/* Dynamic-VIP renewal on enqueue. The stock sched-doctor tick monitor and
 * its run-length histories are dropped: they duplicated native runtime
 * accounting (PELT/schedstat) and their writers had no 6.18 callers. */
#include <linux/jiffies.h>
#include "metis_state.h"
#include "metis_monitor.h"

void monitor_task_enqueue(struct mi_task_struct *state)
{
	struct task_struct *task, *leader;
	struct mi_task_struct *leader_state, *saved_leader;
	u8 *record;
	u32 index, remaining;

	if (!state)
		return;
	task = READ_ONCE(state->task);
	if (!task)
		return;
	leader = READ_ONCE(task->group_leader);
	if (!leader)
		return;
	saved_leader = get_mi_task_struct(leader);
	if (!saved_leader)
		return;
	index = MI_FIELD(state, u32, 0x2f8);
	if (index == UINT_MAX) {
		task = READ_ONCE(state->task);
		if (!task)
			return;
		leader_state = get_mi_task_struct(READ_ONCE(task->group_leader));
		if (!leader_state)
			return;
		index = MI_FIELD(leader_state, u32, 0x2fc);
	}
	if (index >= 7)
		return;
	record = important_tasks[index].raw;
	if (!(unsigned long)record || !(record[0x18] & 4))
		return;
	/* Stock re-reads the owner without another task NULL guard here. */
	task = READ_ONCE(state->task);
	leader = READ_ONCE(task->group_leader);
	if (!leader)
		return;
	leader_state = get_mi_task_struct(leader);
	if (!leader_state || !(leader_state->flags_0xc0 & 0x800))
		return;
	/* Only low32 deadline/jiffies participate; the delta is signed. */
	remaining = MI_FIELD(saved_leader, u32, 0x290) - (u32)jiffies;
	if ((s32)remaining >= 1)
		set_mi_dynamic_vip_task(READ_ONCE(state->task), remaining);
}
