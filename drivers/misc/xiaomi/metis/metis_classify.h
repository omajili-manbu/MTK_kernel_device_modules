/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_CLASSIFY_H
#define _XIAOMI_METIS_CLASSIFY_H
#include "metis_state.h"

/* Missing controller-owned state; no defaults, definitions or init here. */
extern s32 protect_cs;
extern s32 cs_protect_dur;
extern s32 cs_debug;
extern s32 cold_start_app_tgid, cold_start_sserver_tgid;
extern s32 home_tgid, systemui_tgid, systemserver_tgid, sf_tgid;
extern unsigned long coldstart_begin_jif, coldstart_end_jif;
/* Only equality/reset is recovered: keep these pointer-sized owners opaque. */
extern void *cold_start_app_mmlock, *cold_start_sserver_mmlock;
extern void *home_mmlock, *systemui_mmlock, *systemserver_mmlock, *sf_mmlock;

bool is_dynamic_vip(struct task_struct *task);
bool is_mmaplk_vip_task(struct task_struct *task);
void set_mmaplk_vip_task(struct mi_task_struct *state);
void clean_mmaplk_vip_task(struct task_struct *task);
#endif
