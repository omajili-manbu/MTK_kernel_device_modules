// SPDX-License-Identifier: GPL-2.0-only
/* Cold-start tgid predicates, speed-touch parameters and the rt-binder-free
 * VIP pair. The stock EAS-injection/after-enqueue tracking faces are
 * dropped: their readers (private placement, ui/render CPU trackers) were
 * closed-scheduler call sites absent on 6.18. */
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include "metis_state.h"
#include "metis_extra_controls.h"
#include "metis_coldstart.h"
#include "metis_common.h"
#include "metis_vip_queue.h"

bool is_home_tasks(struct task_struct *task)
{
	return task->tgid == home_tgid;
}

bool is_systemui_tasks(struct task_struct *task)
{
	return task->tgid == systemui_tgid;
}

bool is_systemserver_tasks(struct task_struct *task)
{
	return task->tgid == systemserver_tgid;
}

bool is_sf_tasks(struct task_struct *task)
{
	return task->tgid == sf_tgid;
}

void clear_preemption_protect(struct task_struct *task)
{
	struct mi_task_struct *state = get_mi_task_struct(task);

	if (state) {
		MI_FIELD(state, u64, 0x2b8) = 0;
		MI_FIELD(state, u32, 0x2b4) &= 0xffffffe7U;
	}
}

int set_speed_touch_enable(const char *value, const struct kernel_param *param)
{
	u32 enabled = 0;

	if (sscanf(value, "%u\n", &enabled) != 1)
		return -EINVAL;
	speed_touch_enable = enabled;
	if (!enabled) {
		speed_touch_producer_in_boost = 0;
		speed_touch_consumer_in_boost = 0;
	}
	return 0;
}

int set_trace_motion_event(const char *value, const struct kernel_param *param)
{
	u32 event = 0;

	if (sscanf(value, "%u\n", &event) != 1)
		return -EINVAL;
	trace_motion_event = event;
	if (event == 2)
		touch_up_start = ktime_to_ns(ktime_get()) / 1000;
	return 0;
}

void get_frame_boost_end(unsigned long *duration)
{
	/* Stock LDR W zero-extends the four-byte duration into the eight-byte store. */
	*duration = mi_boost_duration;
}

void mi_restore_rt_binder_free(struct task_struct *task)
{
	struct mi_task_struct *state;

	if (!task || !(task->__state & 1))
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	/* The reference reads jiffies even though it discards the result. */
	(void)jiffies;
	if (state->flags_0xc0 & 0x8000)
		clean_mi_vip_task_common(task, 0x8000UL);
}
