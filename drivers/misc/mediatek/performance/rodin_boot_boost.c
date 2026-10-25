// SPDX-License-Identifier: GPL-2.0-only
/*
 * rodin boot boost
 *
 * Boot is latency-bound: the vseq replay wave, init mounts/vold and the
 * zygote preload are short CPU bursts that schedutil ramps through too
 * slowly to matter, so the whole ~24s boot runs near the cluster floor
 * OPPs.  For one window after the cpufreq policies appear this pins a
 * FREQ_QOS_MIN floor at each cluster's top hw OPP (2.1/3.0/3.25 GHz on
 * rodin), then hands the machine back the moment the userspace thermal
 * manager starts clamping max (85C and window expiry are backstops): a
 * thermal-follow law walks the floors down stepwise in the meantime,
 * so stock thermal never gets a floor it has to fight and its
 * cpu_isolate fallback stays out of reach.
 *
 * Safety rails, in order of authority:
 *  - MIN requests structurally lose to any MAX cap: the cpufreq core
 *    resolves min > max in favour of max everywhere (cpufreq_set_policy,
 *    cpufreq_driver_resolve_freq, fast_switch).  When policy->max drops
 *    below our floor (thermal, or the MIUI powerkeeper cap that lands
 *    right after boot completes) we yield a step on our own instead of
 *    leaning on that clamp; cur lagging a freshly armed floor is a ramp
 *    in progress, never a cap.
 *  - soc_max temperature bands drive the same stepwise walk, with
 *    asymmetric persistence (2 ticks to step down, 3 to step back up).
 *  - the normal stop is a handover: the userspace thermal manager
 *    (mi_thermald) starts clamping policy->max within seconds of boot,
 *    and the first sustained sight of a max below our floor releases
 *    the floors outright instead of sawtoothing against its clamps.
 *  - a read-failure streak on the thermal zone or the window expiry
 *    releases everything; the requests stay in place at hw_min so that
 *    policy teardown/rebuild cannot race them.  Thermal zones register
 *    later than the cpufreq policies on this platform, so blindness
 *    before the first successful read is not a fault and only the
 *    window bounds it.  Once stopped the boost never re-arms, so a
 *    late REMOVE/CREATE policy pair must land on hw_min.  The 30s
 *    window only backstops a boot where the handover never arrives.
 *
 * A/B: rodin_boot_boost=off on the cmdline, or the sysfs knobs below.
 */

#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#define RBB_NR_CLUSTER	3
#define RBB_TICK_MS	150
#define RBB_WINDOW_MS	30000
#define RBB_TEMP_L1	78000
#define RBB_TEMP_L2	82000
#define RBB_TEMP_L3	85000
#define RBB_BLIND_FAILS	3
#define RBB_DOWN_TICKS	2
#define RBB_UP_TICKS	3

/* L0..L2 keep floors; RELEASED pushes every cluster back to hw_min. */
enum { RBB_L0, RBB_L1, RBB_L2, RBB_RELEASED };
enum { RBB_WAIT, RBB_ACTIVE, RBB_DONE };

struct rbb_cluster {
	unsigned int first_cpu;
	unsigned int hw_min;
	unsigned int floor[RBB_RELEASED];	/* kHz per level */
	bool attached;
};

static struct rbb_cluster rbb_clusters[RBB_NR_CLUSTER] = {
	{ .first_cpu = 0, .hw_min =  300000,
	  .floor = { 2100000, 1600000, 1200000 } },
	{ .first_cpu = 4, .hw_min =  400000,
	  .floor = { 3000000, 2400000, 1800000 } },
	{ .first_cpu = 7, .hw_min = 1000000,
	  .floor = { 3250000, 2600000, 2000000 } },
};

static struct freq_qos_request rbb_req[RBB_NR_CLUSTER];

static DEFINE_MUTEX(rbb_lock);
static struct delayed_work rbb_work;
static struct kobject *rbb_kobj;
static struct thermal_zone_device *rbb_tzd;

static bool rbb_enabled = true;
static bool rbb_boot_off;
static int rbb_state = RBB_WAIT;
static int rbb_level = RBB_RELEASED;
static unsigned int rbb_window_ms = RBB_WINDOW_MS;
static unsigned long rbb_deadline;
static unsigned int rbb_heat_cnt, rbb_cool_cnt, rbb_cap_cnt, rbb_blind_cnt;
static long rbb_last_temp = -1;
static bool rbb_temp_ever_read;
static bool rbb_zone_diag_done;

static const char *const rbb_level_name[] = { "L0", "L1", "L2", "released" };
static const char *const rbb_state_name[] = { "wait", "active", "done" };

static int rbb_cluster_of(struct cpufreq_policy *policy)
{
	unsigned int first = cpumask_first(policy->related_cpus);
	int i;

	for (i = 0; i < RBB_NR_CLUSTER; i++)
		if (rbb_clusters[i].first_cpu == first)
			return i;
	return -1;
}

static bool rbb_attached_all(void)
{
	int i, n = 0;

	for (i = 0; i < RBB_NR_CLUSTER; i++)
		n += rbb_clusters[i].attached;
	return n == RBB_NR_CLUSTER;
}

static void rbb_apply_level(int level)
{
	struct rbb_cluster *c;
	int i;

	for (i = 0; i < RBB_NR_CLUSTER; i++) {
		c = &rbb_clusters[i];
		if (c->attached)
			freq_qos_update_request(&rbb_req[i],
				level == RBB_RELEASED ? c->hw_min :
							c->floor[level]);
	}
}

static void rbb_goto_level(int level, const char *why)
{
	if (level == rbb_level)
		return;

	rbb_apply_level(level);
	pr_info("rodin_boot_boost: %s <- %s (%s)\n",
		rbb_level_name[level], rbb_level_name[rbb_level], why);
	rbb_level = level;
}

static void rbb_arm(void)
{
	rbb_deadline = jiffies + msecs_to_jiffies(rbb_window_ms);
	rbb_heat_cnt = 0;
	rbb_cool_cnt = 0;
	rbb_cap_cnt = 0;
	rbb_blind_cnt = 0;
	rbb_temp_ever_read = false;
	rbb_state = RBB_ACTIVE;
	rbb_goto_level(RBB_L0, "arm");
	schedule_delayed_work(&rbb_work, 0);
}

static void rbb_attach_one(int idx, struct cpufreq_policy *policy)
{
	struct rbb_cluster *c = &rbb_clusters[idx];
	int ret;

	if (c->attached)
		return;

	ret = freq_qos_add_request(&policy->constraints, &rbb_req[idx],
				   FREQ_QOS_MIN,
				   (rbb_enabled && rbb_state != RBB_DONE) ?
					c->floor[RBB_L0] : c->hw_min);
	if (ret < 0) {
		pr_warn("rodin_boot_boost: cpu%u qos add failed (%d)\n",
			c->first_cpu, ret);
		return;
	}
	c->attached = true;

	if (!rbb_attached_all())
		return;

	pr_info("rodin_boot_boost: policies up, floors %u/%u/%u kHz, window %ums%s\n",
		rbb_clusters[0].floor[RBB_L0], rbb_clusters[1].floor[RBB_L0],
		rbb_clusters[2].floor[RBB_L0], rbb_window_ms,
		rbb_enabled ? "" : " (disabled)");
	/* once the window has expired this boot never re-boosts: a late
	 * REMOVE/CREATE policy pair (hotplug depth changes) must not
	 * re-arm a fresh window */
	if (rbb_enabled && rbb_state != RBB_DONE)
		rbb_arm();
	else
		rbb_state = RBB_DONE;
}

static void rbb_detach_one(int idx)
{
	struct rbb_cluster *c = &rbb_clusters[idx];

	if (!c->attached)
		return;

	if (freq_qos_request_active(&rbb_req[idx]))
		freq_qos_remove_request(&rbb_req[idx]);
	c->attached = false;

	/* policy went away; the next CREATE_POLICY re-arms a fresh window */
	if (rbb_state == RBB_ACTIVE) {
		rbb_state = RBB_WAIT;
		cancel_delayed_work(&rbb_work);
	}
}

static void rbb_scan_policies(void)
{
	struct cpufreq_policy *policy;
	int i;

	for (i = 0; i < RBB_NR_CLUSTER; i++) {
		if (rbb_clusters[i].attached)
			continue;
		policy = cpufreq_cpu_get(rbb_clusters[i].first_cpu);
		if (!policy)
			continue;
		rbb_attach_one(i, policy);
		cpufreq_cpu_put(policy);
	}
}

static long rbb_read_temp(void)
{
	int temp;
	int ret;

	/* the name lookup returns ERR_PTR(-ENODEV), never NULL, on a miss;
	 * a cached error pointer would fail every later tick, so only a
	 * successful lookup is ever stored and misses are retried */
	if (IS_ERR_OR_NULL(rbb_tzd)) {
		rbb_tzd = thermal_zone_get_zone_by_name("soc_max");
		if (IS_ERR_OR_NULL(rbb_tzd))
			rbb_tzd = thermal_zone_get_zone_by_name("cpu-dsu0");
		if (IS_ERR_OR_NULL(rbb_tzd)) {
			rbb_tzd = NULL;
			if (!rbb_zone_diag_done) {
				rbb_zone_diag_done = true;
				pr_info("rodin_boot_boost: thermal zone not found yet (tried soc_max, cpu-dsu0), retrying each tick; list /sys/class/thermal to fix names\n");
			}
			return -1;
		}
	}

	ret = thermal_zone_get_temp(rbb_tzd, &temp);
	return ret ? -1 : temp;
}

static void rbb_tick(struct work_struct *work)
{
	struct cpufreq_policy *policy;
	struct rbb_cluster *c;
	int capped = 0;
	long temp;
	int target;
	int i;

	mutex_lock(&rbb_lock);

	if (rbb_state != RBB_ACTIVE || !rbb_enabled)
		goto out;

	if (time_after_eq(jiffies, rbb_deadline)) {
		rbb_goto_level(RBB_RELEASED, "window");
		rbb_state = RBB_DONE;
		goto out;
	}

	temp = rbb_read_temp();
	rbb_last_temp = temp;
	if (temp < 0) {
		/* zones register later than the policies: before the first
		 * successful read, blindness is expected rather than a
		 * fault, and the window expiry still bounds the floors */
		if (rbb_temp_ever_read &&
		    ++rbb_blind_cnt >= RBB_BLIND_FAILS) {
			rbb_goto_level(RBB_RELEASED, "temp unreadable");
			rbb_state = RBB_DONE;
			goto out;
		}
	} else {
		rbb_blind_cnt = 0;
		rbb_temp_ever_read = true;

		target = temp >= RBB_TEMP_L3 ? RBB_RELEASED :
			 temp >= RBB_TEMP_L2 ? RBB_L2 :
			 temp >= RBB_TEMP_L1 ? RBB_L1 : RBB_L0;
		if (target > rbb_level) {
			if (++rbb_heat_cnt >= RBB_DOWN_TICKS) {
				rbb_goto_level(target, "heat");
				rbb_heat_cnt = 0;
				rbb_cool_cnt = 0;
				rbb_cap_cnt = 0;
			}
		} else if (target < rbb_level) {
			if (++rbb_cool_cnt >= RBB_UP_TICKS) {
				rbb_goto_level(target, "cool");
				rbb_heat_cnt = 0;
				rbb_cool_cnt = 0;
			}
		} else {
			rbb_heat_cnt = 0;
			rbb_cool_cnt = 0;
		}
	}

	/*
	 * a real cap (thermal, or MIUI's own max clamp after boot
	 * completes) lands on policy->max; cur lagging a freshly armed
	 * floor is a ramp in progress, never a cap.  A max sustained below
	 * our floor means the userspace thermal manager has taken over
	 * frequency management (mi_thermald clamps max within seconds of
	 * boot): hand over outright instead of sawtoothing against its
	 * clamps -- the window only backstops a boot where the handover
	 * never arrives.
	 */
	if (rbb_level != RBB_RELEASED) {
		for (i = 0; i < RBB_NR_CLUSTER; i++) {
			c = &rbb_clusters[i];
			if (!c->attached)
				continue;
			policy = cpufreq_cpu_get(c->first_cpu);
			if (!policy)
				continue;
			if (policy->max && policy->max < c->floor[rbb_level])
				capped++;
			cpufreq_cpu_put(policy);
		}

		if (capped) {
			if (++rbb_cap_cnt >= RBB_DOWN_TICKS) {
				rbb_goto_level(RBB_RELEASED, "handover");
				rbb_state = RBB_DONE;
				goto out;
			}
		} else {
			rbb_cap_cnt = 0;
		}
	}

	schedule_delayed_work(&rbb_work, msecs_to_jiffies(RBB_TICK_MS));
out:
	mutex_unlock(&rbb_lock);
}

static int rbb_policy_notify(struct notifier_block *nb, unsigned long event,
			     void *data)
{
	struct cpufreq_policy *policy = data;
	int idx;

	mutex_lock(&rbb_lock);

	if (event == CPUFREQ_CREATE_POLICY) {
		idx = rbb_cluster_of(policy);
		if (idx >= 0)
			rbb_attach_one(idx, policy);
	} else if (event == CPUFREQ_REMOVE_POLICY) {
		idx = rbb_cluster_of(policy);
		if (idx >= 0)
			rbb_detach_one(idx);
	}

	mutex_unlock(&rbb_lock);
	return NOTIFY_DONE;
}

static struct notifier_block rbb_policy_nb = {
	.notifier_call = rbb_policy_notify,
};

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	return sysfs_emit(buf, "%d\n", rbb_enabled);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	mutex_lock(&rbb_lock);
	rbb_enabled = val;

	if (!val) {
		if (rbb_state == RBB_ACTIVE) {
			rbb_goto_level(RBB_RELEASED, "disabled");
			rbb_state = RBB_DONE;
			cancel_delayed_work(&rbb_work);
		}
	} else if (rbb_attached_all()) {
		rbb_arm();
	}
	mutex_unlock(&rbb_lock);

	return count;
}

static ssize_t window_ms_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "%u\n", rbb_window_ms);
}

static ssize_t window_ms_store(struct kobject *kobj, struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned int val;

	if (kstrtouint(buf, 0, &val))
		return -EINVAL;
	val = clamp(val, 5000u, 120000u);

	mutex_lock(&rbb_lock);
	rbb_window_ms = val;
	if (rbb_state == RBB_ACTIVE)
		rbb_deadline = jiffies + msecs_to_jiffies(val);
	mutex_unlock(&rbb_lock);

	return count;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	struct cpufreq_policy *policy;
	struct rbb_cluster *c;
	ssize_t off = 0;
	unsigned long remain = 0;
	int i;

	mutex_lock(&rbb_lock);
	if (rbb_state == RBB_ACTIVE && time_before(jiffies, rbb_deadline))
		remain = jiffies_to_msecs(rbb_deadline - jiffies);

	off += sysfs_emit(buf,
		"enable=%d state=%s level=%s window_ms=%u remain_ms=%lu temp=%ld\n",
		rbb_enabled, rbb_state_name[rbb_state], rbb_level_name[rbb_level],
		rbb_window_ms, remain, rbb_last_temp);

	for (i = 0; i < RBB_NR_CLUSTER; i++) {
		c = &rbb_clusters[i];
		policy = cpufreq_cpu_get(c->first_cpu);
		off += sysfs_emit_at(buf, off,
			"cpu%u floor=%u cur=%u attached=%d\n",
			c->first_cpu,
			rbb_level == RBB_RELEASED ? c->hw_min :
						    c->floor[rbb_level],
			policy ? policy->cur : 0, c->attached);
		if (policy)
			cpufreq_cpu_put(policy);
	}
	mutex_unlock(&rbb_lock);

	return off;
}

static struct kobj_attribute rbb_enable_attr = __ATTR_RW(enable);
static struct kobj_attribute rbb_window_ms_attr = __ATTR_RW(window_ms);
static struct kobj_attribute rbb_status_attr = __ATTR_RO(status);

static struct attribute *rbb_attrs[] = {
	&rbb_enable_attr.attr,
	&rbb_window_ms_attr.attr,
	&rbb_status_attr.attr,
	NULL,
};

static const struct attribute_group rbb_attr_group = {
	.attrs = rbb_attrs,
};

static int __init rbb_cmdline(char *s)
{
	if (!strcmp(s, "off"))
		rbb_boot_off = true;
	return 1;
}
__setup("rodin_boot_boost=", rbb_cmdline);

static int __init rbb_init(void)
{
	int ret;

	if (rbb_boot_off) {
		rbb_enabled = false;
		pr_info("rodin_boot_boost: disabled by cmdline\n");
	}

	INIT_DELAYED_WORK(&rbb_work, rbb_tick);

	rbb_kobj = kobject_create_and_add("rodin_boot_boost", kernel_kobj);
	if (!rbb_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(rbb_kobj, &rbb_attr_group);
	if (ret)
		goto err_kobj;

	ret = cpufreq_register_notifier(&rbb_policy_nb, CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		goto err_sysfs;

	/* policies the driver created before we registered still get floors */
	rbb_scan_policies();

	return 0;

err_sysfs:
	sysfs_remove_group(rbb_kobj, &rbb_attr_group);
err_kobj:
	kobject_put(rbb_kobj);
	return ret;
}
late_initcall(rbb_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rodin boot-window all-core freq QoS floor with thermal follow");
