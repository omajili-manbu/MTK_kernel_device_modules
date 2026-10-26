/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_COMMON_H
#define _XIAOMI_METIS_COMMON_H
#include <linux/atomic.h>
#include "metis_classify.h"
#include "metis_link.h"
struct kernel_param;

/* Stock-owned objects. Full state initialization/control remains absent. */
extern s32 speed_touch_enable, trace_motion_event;
extern s64 touch_up_start;
extern unsigned long mi_link_boost_jif;
extern s32 link_debug;
extern s32 mi_fboost_enable;
extern atomic_t mi_dynamic_vip_num, mi_sdk_eboost_vip_num;
/* Wakeup-owned state; its initialization/producers remain absent. */
extern s32 set_possible_notifythr_vip, last_ui_forked_pid;
extern unsigned long last_ui_forked_jif, cold_start_ui_futex_wait;
bool task_maybe_notify_thread(struct task_struct *task);
void hook_dynamic_vip_setting(struct task_struct *task, bool set);

void mi_resched_vip_task(struct task_struct *task);
void set_mi_vip_task_common(struct task_struct *task, unsigned long mask,
			  unsigned long duration);
void set_mi_vip_task_common_noresched(struct task_struct *task,
				    unsigned long mask, unsigned long duration);
void clean_mi_vip_task_common(struct task_struct *task, unsigned long mask);
void set_mi_vip_task(struct task_struct *task, u32 duration, unsigned long mask);
s32 mi_task_uid(struct mi_task_struct *state);
void set_mi_dynamic_vip_task(struct task_struct *task, u32 duration);
void clean_mi_dynamic_vip_task(struct task_struct *task);
bool mi_dynamic_vip_task(struct task_struct *task);
void mi_dynamic_vtask_prio_sched_start(struct task_struct *task, int milliseconds);
void mi_dynamic_vtask_prio_sched_end(struct task_struct *task);
void metis_clean_vip_thread(struct task_struct *task);
bool metis_xxx_task(struct task_struct *task, unsigned long mask);
bool metis_vvip_task(struct task_struct *task);
u8 *get_important_task_info(struct mi_task_struct *state);
u32 get_process_config_index(struct task_struct *task);

int get_link_vip_enable(char *buffer, const struct kernel_param *param);
int set_link_vip_enable(const char *value, const struct kernel_param *param);
int get_async_binder_link_vip_enable(char *buffer, const struct kernel_param *param);
int set_async_binder_link_vip_enable(const char *value,
				     const struct kernel_param *param);
int get_fboost_enable(char *buffer, const struct kernel_param *param);
int set_fboost_enable(const char *value, const struct kernel_param *param);
int get_mi_fboost_enable(void);
#endif
