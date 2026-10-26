// SPDX-License-Identifier: GPL-2.0-only
/* Eight remaining stock boost lifecycle/controller bodies. Genuine state
 * definitions and static notifier/work initializers belong to integration. */
#include <linux/bitops.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include "metis-final-boost.h"
#include "metis-final-owners.h"

static __always_inline void metis_boost_bug(void)
{
	asm volatile("brk #0x5512");
	__builtin_unreachable();
}

/* Stock inlines find-next/first over the low 32 bits, with sentinel 32.
 * Access the current canonical cpumask bitmap, not its old byte offsets. */
static __always_inline u32 metis_boost_mask32(const struct cpumask *mask)
{
	return (u32)READ_ONCE(cpumask_bits(mask)[0]);
}

static __always_inline unsigned int metis_boost_next32(
		const struct cpumask *mask, unsigned int first)
{
	u32 bits = metis_boost_mask32(mask) & (u32)(~0UL << first);

	return bits ? __ffs(bits) : 32;
}

static void metis_boost_remove_requests(void)
{
	struct freq_qos_request *request;
	unsigned int cpu, first = 0;

	for (;;) {
		cpu = metis_boost_next32(cpu_present_mask, first);
		if (cpu >= 32)
			return;
		request = per_cpu_ptr(&metis_req_min, cpu);
		if (request && freq_qos_request_active(request))
			freq_qos_remove_request(request);
		request = per_cpu_ptr(&metis_req_max, cpu);
		if (request && freq_qos_request_active(request))
			freq_qos_remove_request(request);
		if (cpu >= 31)
			return;
		first = cpu + 1;
	}
}

/* Stock mandatory perflock providers are gone on 6.18 (no in-tree
 * registrants), so the mi_switch_enable perflock hand-off branch and its
 * helper are dropped with them. */

int fboost_run(void *data)
{
	unsigned long flags;
	unsigned int cluster;
	int mode, action;
	u32 remaining;
	struct delayed_work *adjust;

	(void)data;
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		raw_spin_lock_irqsave(&fboost_pending_lock, flags);
		if (!fboost_pending) {
			raw_spin_unlock_irqrestore(&fboost_pending_lock, flags);
			schedule();
			/* This check exists only after the sleep, not on busy rounds. */
			if (kthread_should_stop())
				return 0;
			raw_spin_lock_irqsave(&fboost_pending_lock, flags);
		}
		set_current_state(TASK_RUNNING);
		fboost_pending = 0;
		raw_spin_unlock_irqrestore(&fboost_pending_lock, flags);

		if (READ_ONCE(fboost_exe_mode.execute)) {
			/* rodin 6.18：cluster 0 也参与 boost/restore（stock 只扫 1..2，
			 * cluster 0 被 boost 后永不恢复，属保留缺陷，此处对齐对称） */
			for (cluster = 0; cluster < 3; cluster++) {
				mode = READ_ONCE(bcluster_stat[cluster].mode);
				action = READ_ONCE(bcluster_stat[cluster].action);
				if (!mode && !action)
					continue;
				if (READ_ONCE(fboost_debug))
					pr_err("metis-fboost: metis __do_cluster_boost start...%d %d %d, %d %llu",
						cluster, action, min_cluster_freqs[cluster],
						0x1b9, (unsigned long long)ktime_to_us(ktime_get()));
				action = READ_ONCE(bcluster_stat[cluster].action);
				if (action == 2)
					metis_update_policy_online(cluster, 0, 0);
				else if (action == 1)
					metis_update_policy_online(cluster,
						min_cluster_freqs[cluster], 0);
				/* The original uses one eight-byte clear for this private pair. */
				WRITE_ONCE(*(u64 *)&bcluster_stat[cluster], 0);
				adjust = &rem_cluster_works[cluster].work;
				if (delayed_work_pending(adjust))
					cancel_delayed_work(adjust);
				remaining = READ_ONCE(mi_boost_duration);
				remaining -= get_fboost_duration();
				if (READ_ONCE(fboost_debug))
					pr_info("metis-fboost: metis remain_duration ...%d %d",
						(s32)remaining, 0x1d8);
				queue_delayed_work(metis_wq, adjust,
						   msecs_to_jiffies(remaining));
			}
		}

		if (READ_ONCE(fboost_exe_mode.delete)) {
			for (cluster = 0; cluster < 3; cluster++) {
				if (hrtimer_active(&boost_timers[cluster].timer))
					hrtimer_cancel(&boost_timers[cluster].timer);
				metis_update_policy_online(cluster, 0, -1);
				if (READ_ONCE(fboost_debug))
					pr_err("metis-fboost: metis boost_cluster end...%d %llu",
						0x118, (unsigned long long)ktime_to_us(ktime_get()));
				thermal_limit_freq[cluster] = ULONG_MAX;
				adjust = &rem_cluster_works[cluster].work;
				if (delayed_work_pending(adjust))
					cancel_delayed_work(adjust);
			}
		}
		/* Unlocked clears can discard execution requests submitted this round. */
		WRITE_ONCE(fboost_exe_mode.execute, 0);
		WRITE_ONCE(fboost_exe_mode.delete, 0);
	}
}

int metis_fboost_init(void)
{
	struct sched_param parameter = { .sched_priority = 99 };
	struct cpufreq_policy *policy;
	struct task_struct *thread;
	unsigned int cpu, cluster, first = 0;
	int ret;

	raw_spin_lock_init(&fboost_pending_lock);
	thread = kthread_create(fboost_run, NULL, "fboost_thread");
	if (IS_ERR(thread)) {
		fboost_thread = thread;
		return PTR_ERR(thread);
	}
	wake_up_process(thread);
	fboost_thread = thread;
	sched_setscheduler_nocheck(thread, SCHED_FIFO, &parameter);
	cpus_visit_0 = READ_ONCE(cpumask_bits(cpu_possible_mask)[0]);

	for (;;) {
		cpu = metis_boost_next32(cpu_present_mask, first);
		if (cpu >= 32)
			break;
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("metis-fboost: %s: Failed to get cpufreq policy for cpu%d\n",
				"metis_freq_qos_request_init_first", cpu);
			goto register_notifier;
		}
		ret = freq_qos_add_request(&policy->constraints,
				per_cpu_ptr(&metis_req_min, cpu), FREQ_QOS_MIN,
				FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("metis-fboost: [ST-SCHED-TF-METIS]%s: Failed to add min freq constraint (%d)\n",
				"metis_freq_qos_request_init_first", ret);
			cpufreq_cpu_put(policy);
			goto register_notifier;
		}
		ret = freq_qos_add_request(&policy->constraints,
				per_cpu_ptr(&metis_req_max, cpu), FREQ_QOS_MAX,
				FREQ_QOS_MAX_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("metis-fboost: [ST-SCHED-TF-METIS]%s: Failed to add max freq constraint (%d)\n",
				"metis_freq_qos_request_init_first", ret);
			cpufreq_cpu_put(policy);
			goto register_notifier;
		}
		cpufreq_cpu_put(policy);
		if (cpu >= 31)
			break;
		first = cpu + 1;
	}
	goto initialize_controller;

register_notifier:
	metis_boost_remove_requests();
	ret = cpufreq_register_notifier(&init_fboost_notifier,
					CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("metis-fboost: metis freq init failed %d\n", 0x2f6);
		return ret;
	}

initialize_controller:
	for (cluster = 0; cluster < 3; cluster++) {
		/* rodin 6.18：hrtimer_init 改名 hrtimer_setup（fn 内联传参） */
		hrtimer_setup(&boost_timers[cluster].timer, queue_for_boost_cluster,
			      CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		boost_timers[cluster].cluster = cluster;
		INIT_DELAYED_WORK(&rem_cluster_works[cluster].work,
				  do_adjust_freq_delayed);
		rem_cluster_works[cluster].cluster = cluster;
	}
	metis_wq = alloc_workqueue("metis_wq", WQ_HIGHPRI, 0);
	/* Stock reports EFAULT for an allocation failure and keeps the thread. */
	ret = metis_wq ? 0 : -EFAULT;
	pr_err("metis-fboost: metis init, lineno.:%d ...\n", 0x30c);
	return ret;
}

void metis_fboost_exit(void)
{
	__flush_workqueue(metis_wq);
	destroy_workqueue(metis_wq);
	metis_boost_remove_requests();
	pr_err("metis-fboost: metis exit, lineno.: %d ...\n", 0x318);
	/* Stock leaves the thread, hrtimers and all notifiers live. */
}

void metis_update_policy_online(int cluster, int minimum, int restore_maximum)
{
	struct cpumask group;
	struct cpufreq_policy *policy;
	unsigned int cpu, first = 0, group_first, kind, debug_cpu;
	u32 maximum, online;
	s32 ordinal = 0;
	unsigned long debug_limit;
	unsigned int policy_cpu;

	cpumask_clear(&group);
	cpus_read_lock();
	for (;;) {
		cpu = metis_boost_next32(cpu_online_mask, first);
		if (cpu >= 32)
			break;
		cpumask_clear(&group);
		mi_cpu_coregroup_mask(cpu, &group);
		online = metis_boost_mask32(&group) & metis_boost_mask32(cpu_online_mask);
		group_first = online ? __ffs(online) : 32;
		if (group_first == cpu) {
			if (ordinal == cluster) {
				policy = cpufreq_cpu_get(cpu);
				ordinal = (s32)((u32)cluster + 1U);
				if (policy) {
					/* rodin 6.18：先取 cpu 快照再 put（stock put 后读
					 * policy 属 UAF，此处修复） */
					policy_cpu = policy->cpu;
					if (policy_cpu >= 32)
						metis_boost_bug();
					freq_qos_update_request(per_cpu_ptr(&metis_req_min,
								 policy_cpu), minimum);
					maximum = restore_maximum ? U32_MAX :
						(u32)get_metis_limit(policy_cpu);
					/* Original +0x28 is cpuinfo.max_freq, not policy.max. */
					maximum = min(maximum, policy->cpuinfo.max_freq);
					if (maximum)
						freq_qos_update_request(per_cpu_ptr(&metis_req_max,
									 policy_cpu), (s32)maximum);
					if (READ_ONCE(fboost_debug)) {
						debug_cpu = policy_cpu;
						debug_limit = get_metis_limit(debug_cpu);
						kind = get_cluster_type(policy_cpu);
						if (kind >= 4)
							kind = 0;
						if (kind >= 3)
							metis_boost_bug();
						printk("metis_max ...%d %d %d %lu %lu\n",
							minimum, (s32)maximum, debug_cpu, debug_limit,
							thermal_limit_freq[kind]);
					}
					cpufreq_cpu_put(policy);
					cpufreq_update_policy(policy_cpu);
					break;
				}
			} else {
				ordinal++;
			}
		}
		if (cpu >= 31)
			break;
		first = cpu + 1;
	}
	cpus_read_unlock();
}

int metis_freq_qos_request_init(struct notifier_block *notifier,
			      unsigned long event, void *data)
{
	struct cpufreq_policy *policy;
	unsigned int cpu, cluster, first = 0;
	int ret;

	(void)notifier;
	(void)data;
	if (event != CPUFREQ_CREATE_POLICY)
		return NOTIFY_DONE;
	for (cluster = 0; cluster < 3; cluster++) {
		nb_max_limit[cluster].notifier.notifier_call = cpufreq_notifier_max;
		nb_max_limit[cluster].cluster = cluster;
		/* In particular, registered is not reset by this callback. */
	}
	for (;;) {
		cpu = metis_boost_next32(cpu_present_mask, first);
		if (cpu >= 32)
			break;
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("metis-fboost: %s: Failed to get cpufreq policy for cpu%d\n",
				"metis_freq_qos_request_init", cpu);
			ret = -EAGAIN;
			goto remove_requests;
		}
		cluster = get_cluster_type(policy->cpu);
		if (cluster >= 4)
			cluster = 0;
		if (cluster >= 3)
			metis_boost_bug();
		if (!nb_max_limit[cluster].registered) {
			nb_max_limit[cluster].registered = 1;
			/* Registration errors do not roll back registered. */
			freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MAX,
					      &nb_max_limit[cluster].notifier);
		}
		cpus_visit_0 = (u32)cpus_visit_0 &
				~metis_boost_mask32(policy->related_cpus);
		ret = freq_qos_add_request(&policy->constraints,
				per_cpu_ptr(&metis_req_min, cpu), FREQ_QOS_MIN,
				FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("metis-fboost: [ST-SCHED-TF-METIS]%s: Failed to add min freq constraint (%d)\n",
				"metis_freq_qos_request_init", ret);
			cpufreq_cpu_put(policy);
			goto remove_requests;
		}
		ret = freq_qos_add_request(&policy->constraints,
				per_cpu_ptr(&metis_req_max, cpu), FREQ_QOS_MAX,
				FREQ_QOS_MAX_DEFAULT_VALUE);
		if (ret < 0) {
			pr_err("metis-fboost: [ST-SCHED-TF-METIS]%s: Failed to add max freq constraint (%d)\n",
				"metis_freq_qos_request_init", ret);
			cpufreq_cpu_put(policy);
			goto remove_requests;
		}
		cpufreq_cpu_put(policy);
		if (cpu >= 31)
			break;
		first = cpu + 1;
	}
	pr_info("metis-fboost: metis_freq_qos_request_init sucess!");
	if (!(u32)cpus_visit_0)
		queue_work(system_wq, &finish_fboost_work);
	return NOTIFY_DONE;

remove_requests:
	metis_boost_remove_requests();
	return ret;
}
