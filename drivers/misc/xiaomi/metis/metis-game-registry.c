// SPDX-License-Identifier: GPL-2.0-only
/*
 * metis-game-registry.c - game_migt slot family rebuilt for 6.18.
 *
 * Stock resolved these provider slots inside the closed scheduler kernel.
 * The dash OSS tree only carries the migt-side registrations (UNTESTED,
 * never linked). This file rebuilds the slot storage and register/unregister
 * surfaces verbatim to the stock ABI shapes recorded in
 * drivers/misc/xiaomi/migt/subsystem_lifecycle.h, so migt links =y and its
 * handlers live. The consumer side (closed-scheduler call sites) is
 * bridged to live 6.18 GKI vendor hooks in metis-game-bridge.c.
 *
 * rodin 6.18 (2026-11-02): user-approved active-bridge decision. Slots sit
 * idle (NULL) until migt inits; every bridge reads the slot under
 * READ_ONCE and keeps stock default gating (exclusion disabled) intact.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>

#include "metis-game-registry.h"
#include "metis_registry.h"

/* Slot storage. Signatures = stock closed-kernel contracts. */
metis_skip_cpus_fn game_migt_skip_cpus_hook;
migt_select_cpu_fn game_migt_select_cpu_hook;
migt_task_cpu_fn game_migt_should_skip_this_cpu_rt_hook;
migt_task_cpu_fn game_migt_should_skip_migrate_hook;
migt_task_cpu_fn game_migt_skip_sched_tick_migrate_hook;
migt_cpu_fn game_migt_should_skip_newidle_balance_hook;
migt_mask_fn game_migt_skip_lowest_rt_hook;
migt_mask_fn game_migt_skip_vip_cpus_hook;
migt_affinity_fn game_migt_should_skip_affinity_task_hook;
migt_group_fn game_migt_group_dep_hook;
migt_oem_freq_fn oem_freq_hook;

#define MIGT_SLOT_REG(name, slot, slot_type)				\
void register_##name(slot_type hook)					\
{									\
	if (hook)							\
		WRITE_ONCE(slot, hook);					\
}									\
EXPORT_SYMBOL_GPL(register_##name);					\
void unregister_##name(void)						\
{									\
	WRITE_ONCE(slot, NULL);						\
}									\
EXPORT_SYMBOL_GPL(unregister_##name)

MIGT_SLOT_REG(game_migt_skip_cpus_hook, game_migt_skip_cpus_hook, metis_skip_cpus_fn);
MIGT_SLOT_REG(game_migt_select_cpu_hook, game_migt_select_cpu_hook, migt_select_cpu_fn);
MIGT_SLOT_REG(game_migt_should_skip_this_cpu_rt_hook, game_migt_should_skip_this_cpu_rt_hook, migt_task_cpu_fn);
MIGT_SLOT_REG(game_migt_should_skip_migrate_hook, game_migt_should_skip_migrate_hook, migt_task_cpu_fn);
MIGT_SLOT_REG(game_migt_skip_sched_tick_migrate_hook, game_migt_skip_sched_tick_migrate_hook, migt_task_cpu_fn);
MIGT_SLOT_REG(game_migt_should_skip_newidle_balance_hook, game_migt_should_skip_newidle_balance_hook, migt_cpu_fn);
MIGT_SLOT_REG(game_migt_skip_lowest_rt_hook, game_migt_skip_lowest_rt_hook, migt_mask_fn);
MIGT_SLOT_REG(game_migt_skip_vip_cpus_hook, game_migt_skip_vip_cpus_hook, migt_mask_fn);
MIGT_SLOT_REG(game_migt_should_skip_affinity_task_hook, game_migt_should_skip_affinity_task_hook, migt_affinity_fn);
MIGT_SLOT_REG(game_migt_group_dep_hook, game_migt_group_dep_hook, migt_group_fn);
MIGT_SLOT_REG(oem_freq_hook, oem_freq_hook, migt_oem_freq_fn);
