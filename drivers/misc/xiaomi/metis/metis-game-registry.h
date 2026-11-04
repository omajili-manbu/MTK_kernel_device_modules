/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_GAME_REGISTRY_H
#define _XIAOMI_METIS_GAME_REGISTRY_H

#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>

struct task_struct;

/* Stock closed-kernel slot contracts, recorded in migt's
 * subsystem_lifecycle.h (dash OS3.0.305 reconstruction). */
typedef int (*migt_select_cpu_fn)(struct task_struct *task);
typedef bool (*migt_task_cpu_fn)(struct task_struct *task, unsigned int cpu);
typedef bool (*migt_cpu_fn)(unsigned int cpu);
typedef void (*migt_mask_fn)(struct cpumask *mask);
typedef bool (*migt_affinity_fn)(void);
typedef bool (*migt_group_fn)(int pid);
typedef void (*migt_oem_freq_fn)(void *data, unsigned long util,
		unsigned long mapped_freq, unsigned long max, unsigned long *freq,
		struct cpufreq_policy *policy, bool *need_update);

struct cpumask;
typedef bool (*metis_skip_cpus_fn)(struct cpumask *mask);
extern metis_skip_cpus_fn game_migt_skip_cpus_hook;
extern migt_select_cpu_fn game_migt_select_cpu_hook;
extern migt_task_cpu_fn game_migt_should_skip_this_cpu_rt_hook;
extern migt_task_cpu_fn game_migt_should_skip_migrate_hook;
extern migt_task_cpu_fn game_migt_skip_sched_tick_migrate_hook;
extern migt_cpu_fn game_migt_should_skip_newidle_balance_hook;
extern migt_mask_fn game_migt_skip_lowest_rt_hook;
extern migt_mask_fn game_migt_skip_vip_cpus_hook;
extern migt_affinity_fn game_migt_should_skip_affinity_task_hook;
extern migt_group_fn game_migt_group_dep_hook;
extern migt_oem_freq_fn oem_freq_hook;

void register_game_migt_skip_cpus_hook(metis_skip_cpus_fn hook);
void unregister_game_migt_skip_cpus_hook(void);
void register_game_migt_select_cpu_hook(migt_select_cpu_fn hook);
void unregister_game_migt_select_cpu_hook(void);
void register_game_migt_should_skip_this_cpu_rt_hook(migt_task_cpu_fn hook);
void unregister_game_migt_should_skip_this_cpu_rt_hook(void);
void register_game_migt_should_skip_migrate_hook(migt_task_cpu_fn hook);
void unregister_game_migt_should_skip_migrate_hook(void);
void register_game_migt_skip_sched_tick_migrate_hook(migt_task_cpu_fn hook);
void unregister_game_migt_skip_sched_tick_migrate_hook(void);
void register_game_migt_should_skip_newidle_balance_hook(migt_cpu_fn hook);
void unregister_game_migt_should_skip_newidle_balance_hook(void);
void register_game_migt_skip_lowest_rt_hook(migt_mask_fn hook);
void unregister_game_migt_skip_lowest_rt_hook(void);
void register_game_migt_skip_vip_cpus_hook(migt_mask_fn hook);
void unregister_game_migt_skip_vip_cpus_hook(void);
void register_game_migt_should_skip_affinity_task_hook(migt_affinity_fn hook);
void unregister_game_migt_should_skip_affinity_task_hook(void);
void register_game_migt_group_dep_hook(migt_group_fn hook);
void unregister_game_migt_group_dep_hook(void);
void register_oem_freq_hook(migt_oem_freq_fn hook);
void unregister_oem_freq_hook(void);

int metis_game_bridge_init(void);
void metis_game_bridge_exit(void);

/* Combined android_vh_sched_setaffinity_early entry; per-hook single
 * registration owned by metis-game-bridge.c. */
void mi_setaffinity_early(void *unused, struct task_struct *p,
		const struct cpumask *new_mask, bool *retval);

#endif
