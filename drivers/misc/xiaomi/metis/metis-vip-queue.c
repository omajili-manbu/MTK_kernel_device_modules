// SPDX-License-Identifier: GPL-2.0-only
/* Stock VIP enqueue-side task policy, driven by the native GKI fair-class
 * hooks (android_rvh_enqueue_task_fair / android_rvh_dequeue_task_fair).
 * The stock pick-side queue stack (private vruntime rbtree, pick_next,
 * preempt ladder, prio-sched gate, deadline tree, low-LT lists) is dropped:
 * its readers were closed-scheduler call sites that do not exist on 6.18,
 * and placement/preemption is owned by the native EAS/uclamp core. */
#include <linux/ktime.h>
#include <linux/sched.h>
#include <sched/sched.h>
#include "metis_state.h"
#include "metis_common.h"
#include "metis_coldstart.h"
#include "metis_vip_queue.h"

#include "metis-final-trace.h"

/* Four repeated stock cluster-transfer blocks share the same algorithm.
 * This source helper is not an additional recovered stock function body. */
static void speed_touch_move(struct task_struct *task, unsigned int cpu,
			     unsigned int *last_cpu, s32 *enabled, const char *type)
{
	unsigned int old_cluster = cpu_to_cluster(*last_cpu);
	unsigned int new_cluster = cpu_to_cluster(cpu);

	if (old_cluster != new_cluster && *enabled) {
		restore_boost_cluster(old_cluster);
		boost_cluster(new_cluster, 1, 1, 1);
		if (metis_debug)
			pr_info("metis-fas: speed touch task[%s] %s %d boost cid from %d to %d freq now\n",
				type, task->comm, task->pid, old_cluster, new_cluster);
	}
	*last_cpu = cpu;
}

void before_enqueue_vip_task(void *unused, struct rq *rq, struct task_struct *task, int flags)
{
	struct mi_task_struct *state = get_mi_task_struct(task), *lookup;
	unsigned int cpu, old_cluster, new_cluster;
	s64 boost_start;
	unsigned long duration;
	bool frame_active;
	s32 uid, previous_cluster;

	if (!state)
		return;
	monitor_task_enqueue(state);
	if (flags & 1) {
		/* ENQUEUE_WAKEUP: drop any stale futex owner pid and mark the
		 * freshly woken task so is_mmaplk_vip_task skips it once. */
		MI_FIELD(state, u32, 0x2b0) = 0;
		MI_FIELD(state, u8, 0xba) = 1;
	}
	if ((state->flags_0xc0 & 0x40) && state->task) {
		lookup = get_mi_task_struct(state->task);
		if (lookup) {
			uid = mi_task_uid(lookup);
			if (foreground_uid && uid == foreground_uid) {
				if (state->flags_0xc0 & 0x40)
					state->flags_0xc0 =
						(state->flags_0xc0 & 0xffffff9fU) | 0x20;
				if (metis_debug)
					pr_info("metis-fas: tsk %d flag is %d\n",
						state->task->pid, state->flags_0xc0);
			}
		}
	}
	if (metis_xxx_task(task, 0x100UL)) {
		cpu = cpu_of(rq);
		old_cluster = cpu_to_cluster(ui_last_cpu);
		new_cluster = cpu_to_cluster(cpu);
		if (old_cluster != new_cluster) {
			frame_active = false;
			boost_start = frame_boost_start;
			if (boost_start > vtask_monitor_beg_us &&
			    vtask_monitor_beg_us > vtask_monitor_end_us) {
				duration = 0;
				get_frame_boost_end(&duration);
				/* Raw code tests the sign of wrapping arithmetic,
				 * including duration*1000, rather than time_after. */
				frame_active = (s64)((u64)(ktime_to_ns(ktime_get()) / 1000) -
						   (u64)boost_start - duration * 1000UL) < 0;
			}
			if (frame_active) {
				restore_boost_cluster(old_cluster);
				boost_cluster(new_cluster, 1, 1, 1);
			} else if (speed_touch_consumer_in_boost) {
				restore_boost_cluster(old_cluster);
				boost_cluster(new_cluster, 1, 1, 1);
				if (metis_debug)
					pr_info("metis-fas: speed touch task[ui] %s %d boost cid from %d to %d freq now\n",
						task->comm, task->pid, old_cluster, new_cluster);
			}
		}
		ui_last_cpu = cpu;
	}
	if (metis_xxx_task(task, 0x400UL))
		speed_touch_move(task, cpu_of(rq), &ren_last_cpu,
				 &speed_touch_consumer_in_boost, "ren");
	lookup = get_mi_task_struct(task);
	if (lookup && (lookup->flags_0xc0 & 0x800000))
		speed_touch_move(task, cpu_of(rq), &sf_last_cpu,
				 &speed_touch_producer_in_boost, "sf");
	lookup = get_mi_task_struct(task);
	if (lookup && (lookup->flags_0xc0 & 0x1000000))
		speed_touch_move(task, cpu_of(rq), &hwc_last_cpu,
				 &speed_touch_producer_in_boost, "hwc");
	lookup = get_mi_task_struct(task);
	if (lookup && (lookup->flags_0xc0 & 0x200000)) {
		previous_cluster = MI_FIELD(state, s32, 0x298);
		new_cluster = cpu_to_cluster(cpu_of(rq));
		if (metis_debug)
			pr_info("metis-fas: task %d boost cid from %d to %d freq now\n",
				task->pid, previous_cluster, new_cluster);
		/* The old boost is restored even if the cluster has not changed.
		 * Stock then skips reboosting when both clusters are equal. */
		if (previous_cluster != -1)
			restore_boost_cluster(previous_cluster);
		if (previous_cluster != new_cluster) {
			boost_cluster(new_cluster, 1, 1, 1);
			MI_FIELD(state, s32, 0x298) = new_cluster;
		}
	}
	if (metis_debug)
		pr_info("metis-fas: task %d enqueue vip\n", task->pid);
}

void dequeue_vip_task(struct rq *rq, struct task_struct *task)
{
	struct mi_task_struct *state = get_mi_task_struct(task);

	if (state && (state->flags_0xc0 & 0x80))
		state->flags_0xc0 &= 0xffffff5fU;
}

void mi_dequeue_task_fair_hook(void *unused, struct rq *rq, struct task_struct *task, int flags)
{
	mi_restore_rt_binder_free(task);
	dequeue_vip_task(rq, task);
	/* The task went off-CPU again; clear the woken marker so a later
	 * mmap-lock wait can qualify for VIP again. */
	if (task) {
		struct mi_task_struct *state = get_mi_task_struct(task);

		if (state)
			MI_FIELD(state, u8, 0xba) = 0;
	}
}
