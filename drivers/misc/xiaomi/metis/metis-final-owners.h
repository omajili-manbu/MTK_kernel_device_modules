/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_FINAL_OWNERS_H
#define _XIAOMI_METIS_FINAL_OWNERS_H

#include "metis-final-control.h"
#include "metis-final-link.h"
#include "metis-final-boost.h"
#include "metis-final-module.h"
#include "metis_wakeup.h"

/* Genuine original objects not previously declared by the recovered slices.
 * Their parameter ops are retained independently of inferred C signedness. */
extern u32 mi_lock_blocked_pid;
extern unsigned int n_freq, n_user_freq;
extern char metis_version_str[64];

/* Cluster-affinity state shared by the sysctl/param policy faces. */
extern u32 minor_window_app, tsched_debug, bug_detect, cluaff_control;
extern s32 vip_task_max_cpu_nr;
extern struct cpumask viptask_use_cpumask;
extern struct ctl_table_header *clu_aff_header;
extern u32 mi_rebind_task[4][5];
extern unsigned long rebind_task_mlist[512], rebind_task_llist[512];
extern unsigned long rebind_task_blist[512];
extern struct cpumask cluster_affinity_cpumask[4];
extern unsigned long cluster_affinity_uid_mlist[3125];
extern unsigned long cluster_affinity_uid_llist[3125];
extern unsigned long cluster_affinity_uid_blist[3125];
extern u8 cluster_core_init_suc;
extern struct cpumask cluster_core_mask[4];
int uid_type(unsigned int uid);
int clu_aff_init(void);
void clu_aff_exit(void);
void mi_cpu_coregroup_mask(unsigned int cpu, struct cpumask *mask);
extern const struct ctl_table clu_aff_table[2];

#endif
