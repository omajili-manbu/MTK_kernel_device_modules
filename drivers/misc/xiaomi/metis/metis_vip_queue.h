/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_VIP_QUEUE_H
#define _XIAOMI_METIS_VIP_QUEUE_H

#include "metis_cluster_boost.h"
#include "metis_monitor.h"

struct rq;
struct task_struct;

/* Frame-boost window (ioctl-fed) and speed-touch cluster-transfer trackers
 * (both driven from the metis ioctl / speed-touch params). */
extern unsigned int ui_last_cpu, ren_last_cpu, sf_last_cpu, hwc_last_cpu;
extern s32 speed_touch_consumer_in_boost, speed_touch_producer_in_boost;
extern s64 frame_boost_start, vtask_monitor_beg_us, vtask_monitor_end_us;

/* Stock pick-side machinery (private VIP rbtree, pick_next, preempt ladder,
 * prio-sched gate) is dropped: its only readers were the closed-scheduler
 * call sites that do not exist on 6.18, and preemption/placement policy is
 * owned by the native EAS/uclamp core. What remains is the enqueue-side task
 * policy that runs on live GKI fair-class hooks. */
void before_enqueue_vip_task(void *unused, struct rq *rq, struct task_struct *task, int flags);
/* flags is unused by this body, but belongs to its real fair callback ABI. */
void mi_dequeue_task_fair_hook(void *unused, struct rq *rq, struct task_struct *task, int flags);
void dequeue_vip_task(struct rq *rq, struct task_struct *task);
#endif
