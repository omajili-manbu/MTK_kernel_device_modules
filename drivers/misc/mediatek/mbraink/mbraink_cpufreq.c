// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#include <linux/pm_qos.h>
#include <linux/workqueue.h> /* rodin b515 #115 */
#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>

#include "mbraink_cpufreq.h"

struct h_node {
	unsigned long addr;
	char symbol[KSYM_SYMBOL_LEN];
	struct hlist_node node;
};

static DECLARE_HASHTABLE(tbl, 5);
static int is_inited;
static int is_hooked;
static struct notifier_block *freq_qos_max_notifier, *freq_qos_min_notifier;
static int cluster_num;

/* rodin b515 #115: cpufreq policy 就绪前不建 freq qos hooks（=y 后本函数在 module_init
 * 1.12s 执行 ⇒ cluster_num=0 ⇒ kcalloc(0)+hooks 永久漏注册；6.6 靠 .ko 装载序 3.57s
 * 天然就绪）。200ms×50=10s 有界重试。 */
static void mbraink_cpufreq_init_retry(struct work_struct *work);
static DECLARE_DELAYED_WORK(mbraink_cpufreq_init_work, mbraink_cpufreq_init_retry);
static int mbraink_cpufreq_init_tries;

/*spinlock for mbraink cpufreq notify event*/
static DEFINE_SPINLOCK(cpufreq_lock);
struct mbraink_cpufreq_notify_data cpufreq_notify_data[CPU_CLUSTER_SZ];

static char *find_and_get_symobls(unsigned long caller_addr)
{
	struct h_node *cur_node = NULL;
	struct h_node *new_node = NULL;
	char *cur_symbol = NULL;
	unsigned int cur_key = 0;

	cur_key = (unsigned int) caller_addr & 0x1f;
	// Try to find symbols from history records
	hash_for_each_possible(tbl, cur_node, node, cur_key) {
		if (cur_node->addr == caller_addr) {
			cur_symbol = cur_node->symbol;
			break;
		}
	}
	// Symbols not found. Add new records
	if (!cur_symbol) {
		new_node = kzalloc(sizeof(struct h_node), GFP_KERNEL);
		if (!new_node)
			return NULL;
		new_node->addr = caller_addr;
		sprint_symbol(new_node->symbol, caller_addr);
		cur_symbol = new_node->symbol;
		hash_add(tbl, &new_node->node, cur_key);
	}
	return cur_symbol;
}

static int search_string_index(char *input, char start_symbol,
				char end_symbol, unsigned int *len)
{
	int idx = 0, idx2 = 0;

	for (idx = 0; idx < KSYM_SYMBOL_LEN; idx++)
		if (input[idx] == start_symbol)
			break;

	if (idx == KSYM_SYMBOL_LEN)
		return 0;

	for (idx2 = idx; idx2 < KSYM_SYMBOL_LEN; idx2++)
		if (input[idx2] == end_symbol)
			break;

	if (idx2 == KSYM_SYMBOL_LEN)
		return 0;

	(*len) = idx2 - idx + 1;

	return idx;
}

static int freq_qos_max_notifier_call(struct notifier_block *nb,
					unsigned long freq_limit_max, void *ptr)
{
	int cid = nb - freq_qos_max_notifier;
	unsigned long flags;
	int idx = 0, start_idx = 0, len = MAX_FREQ_SZ;
	int cid_idx = 0, drv_idx = 0;
	struct timespec64 tv = { 0 };
	char *caller_info = find_and_get_symobls(
		(unsigned long)__builtin_return_address(2));
	char *caller_info2 = find_and_get_symobls(
		(unsigned long)__builtin_return_address(3));

	if (caller_info) {
		spin_lock_irqsave(&cpufreq_lock, flags);
		idx = cpufreq_notify_data[cid % CPU_CLUSTER_SZ].notify_count;
		cid_idx = cid % CPU_CLUSTER_SZ;
		drv_idx = idx % CPUFREQ_NOTIFY_SZ;
		ktime_get_real_ts64(&tv);
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].timestamp =
							(tv.tv_sec*1000)+(tv.tv_nsec/1000000);
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].cid = cid;
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].qos_type = FREQ_QOS_MAX;
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].freq_limit =
							(unsigned int) (freq_limit_max);
		memset(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller, 0, MAX_FREQ_SZ);
		start_idx = search_string_index(caller_info, '[', ']', &len);
		if (start_idx != 0) {
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info[start_idx], len);
		} else if (caller_info2) {
			start_idx = search_string_index(caller_info2, '[', ']', &len);
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info2[start_idx], len);
		} else {
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info[start_idx], len);
		}

		cpufreq_notify_data[cid_idx].drv_data[drv_idx].dirty = true;
		/**********************************************************************
		 * pr_info("count = %lu, timestamp= %lld, cid = %d,	\
		 *	idx=%d, freq_qos_type=%u, freq_limit_max = %d, caller=%s !!!\n",
		 *	cpufreq_notify_data[cid % 3].notify_count,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].timestamp,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].cid,
		 *	idx,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].qos_type,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].freq_limit,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].caller);
		 **********************************************************************/

		cpufreq_notify_data[cid_idx].notify_count++;

		spin_unlock_irqrestore(&cpufreq_lock, flags);
	}
	return 0;
}

static int freq_qos_min_notifier_call(struct notifier_block *nb,
					unsigned long freq_limit_min, void *ptr)
{
	int cid = nb - freq_qos_min_notifier;
	unsigned long flags;
	int idx = 0, start_idx = 0, len = MAX_FREQ_SZ;
	int cid_idx = 0, drv_idx = 0;
	struct timespec64 tv = { 0 };
	char *caller_info = find_and_get_symobls(
		(unsigned long)__builtin_return_address(2));
	char *caller_info2 = find_and_get_symobls(
		(unsigned long)__builtin_return_address(3));

	if (caller_info) {
		spin_lock_irqsave(&cpufreq_lock, flags);
		idx = cpufreq_notify_data[cid % CPU_CLUSTER_SZ].notify_count;
		cid_idx = cid % CPU_CLUSTER_SZ;
		drv_idx = idx % CPUFREQ_NOTIFY_SZ;
		ktime_get_real_ts64(&tv);
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].timestamp =
							(tv.tv_sec*1000)+(tv.tv_nsec/1000000);
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].cid = cid;
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].qos_type = FREQ_QOS_MIN;
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].freq_limit =
							(unsigned int) (freq_limit_min);
		memset(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller, 0, MAX_FREQ_SZ);
		start_idx = search_string_index(caller_info, '[', ']', &len);
		if (start_idx != 0) {
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info[start_idx], len);
		} else if (caller_info2) {
			start_idx = search_string_index(caller_info2, '[', ']', &len);
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info2[start_idx], len);
		} else {
			memcpy(cpufreq_notify_data[cid_idx].drv_data[drv_idx].caller,
				&caller_info[start_idx], len);
		}
		cpufreq_notify_data[cid_idx].drv_data[drv_idx].dirty = true;
		/**************************************************************************
		 * pr_info("count = %lu, timestamp= %lld, cid = %d,	\
		 *	idx = %d, freq_qos_type=%u, freq_limit_min = %d, caller=%s !!!\n",
		 *	cpufreq_notify_data[cid % 3].notify_count,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].timestamp,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].cid,
		 *	idx,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].qos_type,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].freq_limit,
		 *	cpufreq_notify_data[cid % 3].drv_data[drv_idx].caller);
		 ***************************************************************************/

		cpufreq_notify_data[cid_idx].notify_count++;
		spin_unlock_irqrestore(&cpufreq_lock, flags);
	}
	return 0;
}

void
mbraink_get_cpufreq_notifier_info(unsigned short current_cluster_idx,
				unsigned short current_idx,
				struct mbraink_cpufreq_notify_struct_data *cpufreq_notify_buffer)
{
	int i = 0, j = 0;
	int ret = 0;
	unsigned long flags;
	unsigned short notify_cnt = 0;

	spin_lock_irqsave(&cpufreq_lock, flags);

	memset(cpufreq_notify_buffer, 0, sizeof(struct mbraink_cpufreq_notify_struct_data));

	for (i = current_cluster_idx; i < CPU_CLUSTER_SZ; i++) {
		for (j = current_idx; j < CPUFREQ_NOTIFY_SZ; j++) {
			if (cpufreq_notify_data[i].drv_data[j].dirty  == false)
				continue;
			else {
				notify_cnt = cpufreq_notify_buffer->notify_count;
				if (notify_cnt < MAX_NOTIFY_CPUFREQ_NUM) {
					cpufreq_notify_buffer->drv_data[notify_cnt].timestamp =
						cpufreq_notify_data[i].drv_data[j].timestamp;
					cpufreq_notify_buffer->drv_data[notify_cnt].cid =
						cpufreq_notify_data[i].drv_data[j].cid;
					cpufreq_notify_buffer->drv_data[notify_cnt].qos_type =
						cpufreq_notify_data[i].drv_data[j].qos_type;
					cpufreq_notify_buffer->drv_data[notify_cnt].freq_limit =
						cpufreq_notify_data[i].drv_data[j].freq_limit;
					memcpy(cpufreq_notify_buffer->drv_data[notify_cnt].caller,
						cpufreq_notify_data[i].drv_data[j].caller,
						MAX_FREQ_SZ);
					/*pr_info("read (%d, %d) timestamp=%lld !!!\n",
					 *i, j,
					 *cpufreq_notify_buffer->drv_data[notify_cnt].timestamp);
					 */
					cpufreq_notify_buffer->notify_count++;
					cpufreq_notify_data[i].drv_data[j].dirty  = false;
				} else {
					ret = -1;
					cpufreq_notify_buffer->notify_cluster_idx = i;
					cpufreq_notify_buffer->notify_idx = j;
					break;
				}
			}
		}
		if (ret == -1)
			break;
		current_idx = 0;
	}
	pr_info("%s: current_cluster_idx = %u, current_idx= %u, count = %u\n",
			__func__, cpufreq_notify_buffer->notify_cluster_idx,
			cpufreq_notify_buffer->notify_idx,
			cpufreq_notify_buffer->notify_count);
	spin_unlock_irqrestore(&cpufreq_lock, flags);
}

/*
 * rodin 6.9：这两个帮助函数与 perf_common/perf_freq_tracker.c 的同名全局撞名
 * （mtk_mbraink 与 mtk_perf_common 在 6.6 是两个独立 .ko，内建同图即 duplicate）
 * ⇒ 加 mbraink_ 前缀（项目「双内建共存改名范式」）。函数体零改动。
 */
int mbraink_insert_freq_qos_hook(void)
{
	struct cpufreq_policy *policy;
	int cpu;
	int cid = 0;
	int ret = -1;

	if (is_hooked || !is_inited)
		return ret;

	for_each_possible_cpu(cpu) {
		if (cid >= cluster_num) {
			pr_info("[%s] cid over-index\n", __func__);
			break;
		}
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MAX,
							freq_qos_max_notifier + cid);
			if (ret) {
				pr_info("[%s] max_notifier failed\n", __func__);
				goto register_failed;
			}

			ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MIN,
							freq_qos_min_notifier + cid);
			if (ret) {
				pr_info("[%s] min_notifier failed\n", __func__);
				goto register_failed;
			}
			cid++;
			cpu = cpumask_last(policy->related_cpus);
			cpufreq_cpu_put(policy);
		}
	}
	is_hooked = 1;
	return ret;

register_failed:
	mbraink_remove_freq_qos_hook();
	return ret;
}

void mbraink_remove_freq_qos_hook(void)
{
	struct cpufreq_policy *policy;
	int cpu;
	int cid = 0;
	int ret;

	is_hooked = 0;

	for_each_possible_cpu(cpu) {
		if (cid >= cluster_num) {
			pr_info("[%s] cid over-index\n", __func__);
			break;
		}
		policy = cpufreq_cpu_get(cpu);

		if (policy) {
			ret = freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MAX,
							freq_qos_max_notifier + cid);
			if (ret)
				pr_info("[%s] max_notifier failed\n", __func__);
			ret = freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MIN,
							freq_qos_min_notifier + cid);
			if (ret)
				pr_info("[%s] min_notifier failed\n", __func__);
			cid++;
			cpu = cpumask_last(policy->related_cpus);
			cpufreq_cpu_put(policy);
		}
	}
}

static void init_freq_qos_notifier(void)
{
	struct cpufreq_policy *policy;
	int cpu, cid;

	cluster_num = 0;
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			cluster_num++;
			cpu = cpumask_last(policy->related_cpus);
			cpufreq_cpu_put(policy);
		}
	}

	if (cluster_num != CPU_CLUSTER_SZ)
		pr_info("Warning:  CPU_CLUSTER_SZ is not aligned with cluster_num, %d, %d\n",
			CPU_CLUSTER_SZ, cluster_num);

	freq_qos_max_notifier = kcalloc(cluster_num, sizeof(struct notifier_block), GFP_KERNEL);
	freq_qos_min_notifier = kcalloc(cluster_num, sizeof(struct notifier_block), GFP_KERNEL);

	for (cid = 0; cid < cluster_num; cid++) {
		freq_qos_max_notifier[cid].notifier_call = freq_qos_max_notifier_call;
		freq_qos_min_notifier[cid].notifier_call = freq_qos_min_notifier_call;
	}
}

static void clear_freq_qos_notifier(void)
{
	kfree(freq_qos_max_notifier);
	kfree(freq_qos_min_notifier);
}

/* rodin b515 #115: 数 policy 集群数；==CPU_CLUSTER_SZ 才算就绪（防半程注册只挂部分簇）。 */
static int mbraink_cpufreq_policies_ready(void)
{
	struct cpufreq_policy *policy;
	int cpu, n = 0;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (policy) {
			n++;
			cpu = cpumask_last(policy->related_cpus);
			cpufreq_cpu_put(policy);
		}
	}
	return n;
}

static void mbraink_cpufreq_init_retry(struct work_struct *work)
{
	mbraink_cpufreq_notify_init();
}

int mbraink_cpufreq_notify_init(void)
{
	is_hooked = 0;

	/* rodin b515 #115: policy 未齐 ⇒ defer；超限按原行为继续并保留原警告作诊断
	 * （init_freq_qos_notifier 的 CPU_CLUSTER_SZ not aligned 打印即诊断锚）。 */
	if (mbraink_cpufreq_policies_ready() != CPU_CLUSTER_SZ) {
		if (mbraink_cpufreq_init_tries++ < 50) {
			pr_info("[MBK] %s: cpufreq policy not ready, defer freq qos hooks\n",
				__func__);
			schedule_delayed_work(&mbraink_cpufreq_init_work, msecs_to_jiffies(200));
			return 0;
		}
		pr_notice("[MBK] %s: cpufreq policy still not ready after 10s, install with current count\n",
			  __func__);
	}

	memset(cpufreq_notify_data, 0, sizeof(struct mbraink_cpufreq_notify_data) * 3);

	init_freq_qos_notifier();
	// Initialize hash table
	hash_init(tbl);
	is_inited = 1;

	mbraink_insert_freq_qos_hook();
	pr_info("[MBK] %s: freq qos hooks installed (cluster_num=%d)\n", __func__, cluster_num);
	return 0;
}

void mbraink_cpufreq_notify_exit(void)
{
	int bkt = 0;
	struct h_node *cur = NULL;
	struct hlist_node *tmp = NULL;

	cancel_delayed_work_sync(&mbraink_cpufreq_init_work); /* rodin b515 #115 */
	mbraink_remove_freq_qos_hook();
	clear_freq_qos_notifier();
	// Remove hash table
	hash_for_each_safe(tbl, bkt, tmp, cur, node) {
		hash_del(&cur->node);
		kfree(cur);
	}
	is_inited = 0;
}
