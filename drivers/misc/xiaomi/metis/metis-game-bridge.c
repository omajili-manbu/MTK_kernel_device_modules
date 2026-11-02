// SPDX-License-Identifier: GPL-2.0-only
/*
 * metis-game-bridge.c - 6.18 consumer side of the rebuilt game_migt slots.
 *
 * Stock consumed these slots from closed-scheduler call sites (RT/Fair
 * pick, migrate, newidle, affinity and sugov mapping). On 6.18 the live
 * equivalents are GKI vendor hooks; this bridge attaches them and forwards
 * to the migt handlers registered in metis-game-registry.c. Every hook
 * keeps native semantics: the bridge only narrows or vetoes when the
 * handler (stock-gated, default off) answers.
 *
 * rodin 6.18 (2026-11-02): user-approved active-bridge decision. Slot
 * reads are single READ_ONCE; handlers are stock policy functions with
 * their own parameter gates, so an unconfigured migt leaves every path at
 * native behavior.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/cpufreq.h>
#include "kernel/sched/sched.h"

#include "metis-game-registry.h"
#include "../mi_schedule/mi_schedule.h"

/* Stock: fair pick asks for the game task's exclusive CPU after EAS.
 * Bridge: a non-negative answer inside the task's mask wins. */
void metis_bridge_select_task_rq_fair(struct task_struct *p,
		int prev_cpu, int sd_flag, int wake_flags, int *new_cpu)
{
	migt_select_cpu_fn hook = READ_ONCE(game_migt_select_cpu_hook);
	metis_skip_cpus_fn skip = READ_ONCE(game_migt_skip_cpus_hook);
	int cpu;

	if (!hook && !skip)
		return;

	if (hook) {
		cpu = hook(p);
		if (cpu >= 0 && cpu < nr_cpu_ids &&
		    cpumask_test_cpu(cpu, p->cpus_ptr) && cpu_online(cpu)) {
			*new_cpu = cpu;
			return;
		}
	}

	if (skip && *new_cpu >= 0 && *new_cpu < nr_cpu_ids) {
		struct cpumask reserved;

		cpumask_clear(&reserved);
		skip(&reserved);
		if (cpumask_test_cpu(*new_cpu, &reserved) &&
		    !cpumask_test_cpu(prev_cpu, &reserved) &&
		    cpu_online(prev_cpu) && cpumask_test_cpu(prev_cpu, p->cpus_ptr))
			*new_cpu = prev_cpu;
		/* No compliant fallback: keep the native pick rather than
		 * inventing a candidate the closed scheduler would have had. */
	}
}

/* Stock: RT pick honors reserve exclusions and VIP-cpu skips.
 * Bridge: same fallback shape as the fair path. */
void metis_bridge_select_task_rq_rt(struct task_struct *p,
		int prev_cpu, int sd_flag, int wake_flags, int *new_cpu)
{
	migt_task_cpu_fn skip_rt = READ_ONCE(game_migt_should_skip_this_cpu_rt_hook);
	migt_mask_fn vip = READ_ONCE(game_migt_skip_vip_cpus_hook);
	bool veto = false;

	if (!skip_rt && !vip)
		return;

	if (skip_rt && *new_cpu >= 0 && *new_cpu < nr_cpu_ids)
		veto = skip_rt(p, *new_cpu);

	if (!veto && vip && *new_cpu >= 0 && *new_cpu < nr_cpu_ids) {
		struct cpumask reserved;

		cpumask_clear(&reserved);
		vip(&reserved);
		veto = cpumask_test_cpu(*new_cpu, &reserved);
	}

	if (veto && prev_cpu >= 0 && prev_cpu < nr_cpu_ids &&
	    cpu_online(prev_cpu) && cpumask_test_cpu(prev_cpu, p->cpus_ptr))
		*new_cpu = prev_cpu;
}

/* Stock: load balance consults skip-migrate before pulling a game task.
 * Bridge: either handler (pull path or tick path) vetoes the migration. */
void metis_bridge_can_migrate_task(struct task_struct *p,
		int dst_cpu, int *can_migrate)
{
	migt_task_cpu_fn skip = READ_ONCE(game_migt_should_skip_migrate_hook);
	migt_task_cpu_fn tick = READ_ONCE(game_migt_skip_sched_tick_migrate_hook);

	if (!skip && !tick)
		return;
	if (!*can_migrate)
		return;
	if ((skip && skip(p, dst_cpu)) || (tick && tick(p, dst_cpu)))
		*can_migrate = 0;
}

/* Stock: newidle balance is suppressed on reserved CPUs.
 * Bridge: fair.c answers done=1 straight out of the hook (fair.c:13247),
 * so handler=true short-circuits the attempt with pulled_task=0. */
static void metis_bridge_newidle_balance(void *unused, struct rq *this_rq,
		struct rq_flags *rf, int *pulled_task, int *done)
{
	migt_cpu_fn hook = READ_ONCE(game_migt_should_skip_newidle_balance_hook);

	if (!hook || *done)
		return;
	if (hook(cpu_of(this_rq))) {
		*done = 1;
		*pulled_task = 0;
	}
}

/* Stock: RT lowest-rq search removes skip_lowest_rt CPUs from the mask.
 * Bridge: handler rewrites the mask in place (stock contract); when the
 * native answer falls out of the rewritten mask, fall back to the first
 * remaining candidate instead of keeping a reserved CPU. */
static void metis_bridge_find_lowest_rq(void *unused, struct task_struct *sched_ctx,
		struct task_struct *exec_ctx, struct cpumask *lowest_mask,
		int ret, int *cpu)
{
	migt_mask_fn hook = READ_ONCE(game_migt_skip_lowest_rt_hook);

	if (!hook || !ret || !lowest_mask)
		return;
	if (*cpu < 0 || *cpu >= nr_cpu_ids)
		return;
	hook(lowest_mask);
	if (!cpumask_test_cpu(*cpu, lowest_mask)) {
		int first = cpumask_first(lowest_mask);

		if (first < nr_cpu_ids)
			*cpu = first;
		/* Empty mask: keep the native answer; stock left the search
		 * result untouched when exclusion cleared everything. */
	}
}

/* Stock: the closed RT push-pull honored second-group dependencies.
 * Bridge: handler=true forces honor so sync wakeups stay group-local. */
static void metis_bridge_should_honor_rt_sync(void *unused, struct rq *rq,
		struct task_struct *p, bool sync, bool *honor)
{
	migt_group_fn hook = READ_ONCE(game_migt_group_dep_hook);

	if (!hook || *honor)
		return;
	if (hook(p->pid))
		*honor = true;
}

/* Stock: MTK affinity layer skipped re-binding VIP-affinity tasks. The
 * 6.18 MTK re-binder lives in mtk_set_cpus_allowed_ptr (fpsgo-fed
 * kernel_allowed_mask); since rodin 6.18 (2026-11-03) the migt skip
 * verdict is honored there, and this bridge remains a syscalls-entry
 * observation point only. */
static void metis_bridge_setaffinity_early(void *unused, struct task_struct *p,
		const struct cpumask *new_mask, bool *retval)
{
	migt_affinity_fn hook = READ_ONCE(game_migt_should_skip_affinity_task_hook);

	if (hook && hook())
		trace_printk("metis-game: skip affinity task %d observed\n", p->pid);
}

/* Stock: sugov mapping consulted the game-load OEM freq hook. The 6.18
 * android_vh_map_util_freq carries the exact stock signature, so this is
 * a pure forward: migt registers set_next_freq and the bridge relays. */
static void metis_bridge_map_util_freq(void *data, unsigned long util,
		unsigned long freq, unsigned long cap, unsigned long *next_freq,
		struct cpufreq_policy *policy, bool *need_freq_update)
{
	migt_oem_freq_fn hook = READ_ONCE(oem_freq_hook);

	if (hook)
		hook(data, util, freq, cap, next_freq, policy, need_freq_update);
}

/* Stock fed update_pkg_load (and through the sched_load_update slot,
 * migt_hook) from closed-scheduler per-event accounting. The 6.18 live
 * equivalent producer is the cputime tick hook: delta_exec is rebuilt by
 * differencing sum_exec_runtime against the mi_task tail field (0x4e0,
 * zero readers tree-wide, verified). The event argument is pass-through in
 * every consumer (migt_hook never branches on it), so the constant tick
 * event 0 is lossless. */
static void metis_bridge_account_task_time(void *unused, struct task_struct *p,
		struct rq *rq, int user_tick)
{
	struct mi_task_struct *t;
	u64 now_exec, delta;

	t = get_mi_task_struct(p);
	if (!t)
		return;
	now_exec = p->se.sum_exec_runtime;
	delta = now_exec - t->last_exec_runtime;
	t->last_exec_runtime = now_exec;
	if ((s64)delta <= 0)
		return;
	update_pkg_load(p, cpu_of(rq), 0, get_jiffies_64(), delta);
}

int metis_game_bridge_init(void)
{
	/* rodin 6.18（2026-11-03）：select_task_rq_fair/rt、can_migrate 三槽
	 * 单属主化，本三实现改由 mtk scheduler 合并钩子直调（sched_main.c）。 */
	register_trace_android_rvh_sched_newidle_balance(metis_bridge_newidle_balance, NULL);
	register_trace_android_rvh_find_lowest_rq(metis_bridge_find_lowest_rq, NULL);
	register_trace_android_rvh_should_honor_rt_sync(metis_bridge_should_honor_rt_sync, NULL);
	register_trace_android_vh_sched_setaffinity_early(metis_bridge_setaffinity_early, NULL);
	register_trace_android_vh_map_util_freq(metis_bridge_map_util_freq, NULL);
	register_trace_android_vh_account_task_time(metis_bridge_account_task_time, NULL);
	pr_info("metis-game: vendor hook bridge inited\n");
	return 0;
}

/* rodin 6.18：android_rvh_* 为 RESTRICTED_HOOK（TP_PROTO 带 1 参尾注），
 * 只生成 register 不生成 unregister；内建生命周期 = 常驻，exit 仅拆
 * 非受限 DECLARE_HOOK 两件。 */
void metis_game_bridge_exit(void)
{
	unregister_trace_android_vh_sched_setaffinity_early(metis_bridge_setaffinity_early, NULL);
	unregister_trace_android_vh_map_util_freq(metis_bridge_map_util_freq, NULL);
	unregister_trace_android_vh_account_task_time(metis_bridge_account_task_time, NULL);
	tracepoint_synchronize_unregister();
}
