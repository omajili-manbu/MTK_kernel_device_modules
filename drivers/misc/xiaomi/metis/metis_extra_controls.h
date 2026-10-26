/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_EXTRA_CONTROLS_H
#define _XIAOMI_METIS_EXTRA_CONTROLS_H
#include "metis_common.h"
struct kernel_param;

/* Original state; full initialization/controller ownership remains absent. */
extern u32 mi_boost_duration;

bool is_home_tasks(struct task_struct *task);
bool is_systemui_tasks(struct task_struct *task);
bool is_systemserver_tasks(struct task_struct *task);
bool is_sf_tasks(struct task_struct *task);
void clear_preemption_protect(struct task_struct *task);
int set_speed_touch_enable(const char *value, const struct kernel_param *param);
int set_trace_motion_event(const char *value, const struct kernel_param *param);
void get_frame_boost_end(unsigned long *duration);
void mi_restore_rt_binder_free(struct task_struct *task);
#endif
