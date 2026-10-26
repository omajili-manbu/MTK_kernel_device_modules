/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_COLDSTART_H
#define _XIAOMI_METIS_COLDSTART_H
#include "metis_link.h"
struct kernel_param;
struct rw_semaphore;

/* Cold-start window controller-owned globals (owners in metis-final-owners.c). */
extern u32 set_cold_start_render_vip;
extern u32 override_schedboost_in_coldstart;
extern u32 coldstart_set_mmlock_holder_vip;

/* .text+0xd0: x1 mask, x2 duration added to jiffies; no fake policy helper. */
extern void set_mi_vip_task_common_noresched(struct task_struct *task,
		unsigned long mask, unsigned long duration_jiffies);

bool coldstart_app_ren_task(struct task_struct *task);
bool coldstart_app_tasks(struct task_struct *task);
bool coldstart_app_ui_task(struct task_struct *task);
void coldstart_mark_mmlock_holder(struct task_struct *task);
bool coldstart_sserver_tasks(struct task_struct *task);
void coldstart_tsk_ready_to_run(struct task_struct *task);
void coldstart_unmark_mmlock_holder(struct task_struct *task);
int get_coldstart_app_tgid(void);
int get_sserver_tgid(char *buffer, const struct kernel_param *param);
bool is_important_mmlock(struct rw_semaphore *mmlock);
void protect_cs_task(struct task_struct *task);
void protect_pre_cs_task(struct task_struct *task);
int set_cold_start_app_tgid(int tid);
int set_sserver_tgid(const char *buffer, const struct kernel_param *param);

/* Wakeup-side classifiers (producers live in metis-wakeup.c hooks). */
bool cold_start_ui_pipeline(struct task_struct *task);
bool cold_start_ui_task(struct task_struct *task);
bool cold_start_wakeup_link_task(struct task_struct *task);
bool cold_start_wakeup_task(struct task_struct *task);
#endif
