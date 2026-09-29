/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#ifndef _SCHED_COMMON_H
#define _SCHED_COMMON_H

/* rodin 6.9：vendor 用 arch_scale_cpu_capacity()（替代 6.18 已删的
 * capacity_orig_of()）与 topology_cluster_id()，而 common.h 被 sched/fair、
 * sched/eas、sched/sugov、performance/fpsgo_v3、cache-auditor 各簇共同包含
 * ⇒ 在共同头里一次性引入声明（6.18 头瘦身后这些不再被隐式带入）。 */
#include <linux/topology.h>
#include <linux/sched/topology.h>

#define MTK_VENDOR_DATA_SIZE_TEST(mstruct, kstruct)		\
	BUILD_BUG_ON(sizeof(mstruct) > (sizeof(u64) *		\
		ARRAY_SIZE(((kstruct *)0)->android_vendor_data1)))

#define GEAR_HINT_UNSET -1
#define MTK_TASK_GROUP_FLAG 1
#define MTK_TASK_FLAG 9
#define RAVG_HIST_SIZE_MAX (5)
#define FLT_NR_CPUS CONFIG_MAX_NR_CPUS

struct task_gear_hints {
	int gear_start;
	int num_gear;
	int reverse;
};

struct vip_task_struct {
	struct list_head		vip_list;
	u64				sum_exec_snapshot;
	u64				total_exec;
	int				vip_prio;
	bool			basic_vip;
	bool			vvip;
	bool			faster_compute_eng;
	int				priority_based_prio;
	unsigned int	throttle_time;
};

struct soft_affinity_task {
	bool latency_sensitive;
	struct cpumask soft_cpumask;
};

struct gp_task_struct {
	struct grp __rcu	*grp;
	bool customized;
};

struct sbb_task_struct {
	int set_task;
	int set_group;
};

struct curr_uclamp_hint {
	int hint;
};

struct rot_task_struct {
	u64 ktime_ns;
};

struct cc_task_struct {
	u64 over_type;
};

struct task_turbo_t {
	unsigned char turbo:1;
	unsigned char render:1;
	unsigned short inherit_cnt:14;
	short nice_backup;
	atomic_t inherit_types;
	int vip_prio_backup;
	unsigned int throttle_time_backup;
};

struct flt_task_struct {
	u64	last_update_time;
	u64	mark_start;
	u32	sum;
	u32	util_sum;
	u32	demand;
	u32	util_demand;
	u32	sum_history[RAVG_HIST_SIZE_MAX];
	u32	util_sum_history[RAVG_HIST_SIZE_MAX];
	u32	util_avg_history[RAVG_HIST_SIZE_MAX];
	u64	active_time;
	u32	init_load_pct;
	u32	curr_window_cpu[FLT_NR_CPUS];
	u32	prev_window_cpu[FLT_NR_CPUS];
	u32	curr_window;
	u32	prev_window;
	int	prev_on_rq;
	int	prev_on_rq_cpu;
};

struct cpuqos_task_struct {
	int pd;
	int rank;
};

struct mig_task_struct {
	unsigned long pending_rec;
};

struct mtk_task {
	struct vip_task_struct	vip_task;
	struct soft_affinity_task sa_task;
	struct gp_task_struct	gp_task;
	struct task_gear_hints  gear_hints;
	struct sbb_task_struct sbb_task;
	struct curr_uclamp_hint cu_hint;
	struct rot_task_struct rot_task;
	struct cc_task_struct cc_task;
	struct task_turbo_t turbo_data;
	struct flt_task_struct flt_task;
	struct cpuqos_task_struct cpuqos_task;
	struct mig_task_struct mig_task;
	struct cpumask kernel_allowed_mask;
};

struct soft_affinity_tg {
	struct cpumask soft_cpumask;
};

struct cgrp_tg {
	bool colocate;
	int groupid;
};

struct vip_task_group {
	unsigned int threshold;
};

struct mtk_tg {
	struct soft_affinity_tg	sa_tg;
	struct cgrp_tg		cgrp_tg;
	struct vip_task_group vtg;
};

struct sugov_rq_data {
	short int uclamp[UCLAMP_CNT];
	bool enq_dvfs;
	bool enq_ing;
	bool enq_update_dsu_freq;
};

struct mtk_rq {
	struct sugov_rq_data sugov_data;
};

extern int num_sched_clusters;
extern cpumask_t __read_mostly ***cpu_array;
extern void init_cpu_array(void);
extern void build_cpu_array(void);
extern void free_cpu_array(void);
extern void mtk_get_gear_indicies(struct task_struct *p, int *order_index, int *end_index,
			int *reverse);
extern bool sched_gear_hints_enable_get(void);
extern void init_gear_hints(void);
extern bool sched_dsu_pwr_enable_get(void);
extern void init_dsu_pwr_enable(void);


extern bool sched_updown_migration_enable_get(void);
extern void init_updown_migration(void);

extern bool sched_post_init_util_enable_get(void);

extern int set_gear_indices(int pid, int gear_start, int num_gear, int reverse);
extern int unset_gear_indices(int pid);
extern int get_gear_indices(int pid, int *gear_start, int *num_gear, int *reverse);
extern int set_updown_migration_pct(int gear_idx, int dn_pct, int up_pct);
extern int unset_updown_migration_pct(int gear_idx);
extern int get_updown_migration_pct(int gear_idx, int *dn_pct, int *up_pct);

struct util_rq {
	unsigned long util_cfs;
	unsigned long dl_util;
	unsigned long irq_util;
	unsigned long rt_util;
	unsigned long bw_dl_util;
	bool base;
};

#if IS_ENABLED(CONFIG_NONLINEAR_FREQ_CTL)
extern void mtk_map_util_freq(void *data, unsigned long util,
			struct cpumask *cpumask, unsigned long *next_freq);
#else
#define mtk_map_util_freq(data, util, cap, next_freq)
#endif /* CONFIG_NONLINEAR_FREQ_CTL */

/*
 * rodin 6.9：6.18 删除了 kernel/sched/sched.h 里的 enum cpu_util_type（把
 * FREQUENCY_UTIL/ENERGY_UTIL 双模式拆成 effective_cpu_util() 的 min/max 出参 +
 * sugov_effective_cpu_perf()）。vendor 的 mtk_cpu_util() 是自实现、语义直接依赖
 * 该枚举 ⇒ 在本地恢复，这是把 6.6 语义完整搬过来，不是降级。
 * 注：sched/fair.c 里三处调用**内核** effective_cpu_util() 的点位于 `#else`
 * 分支（外层 IS_ENABLED(CONFIG_MTK_CPUFREQ_SUGOV_EXT)），本配置下不参与编译。
 */
enum cpu_util_type {
	FREQUENCY_UTIL,
	ENERGY_UTIL,
};

#if IS_ENABLED(CONFIG_MTK_CPUFREQ_SUGOV_EXT)
DECLARE_PER_CPU(int, cpufreq_idle_cpu);
DECLARE_PER_CPU(spinlock_t, cpufreq_idle_cpu_lock);
unsigned long mtk_cpu_util(unsigned int cpu, unsigned long util_rq,
				enum cpu_util_type type,
				struct task_struct *p,
				unsigned long min_cap, unsigned long max_cap);
int dequeue_idle_cpu(int cpu);
#endif
__always_inline
unsigned long mtk_uclamp_rq_util_with(struct rq *rq, unsigned long util,
				  struct task_struct *p,
				  unsigned long min_cap, unsigned long max_cap,
				  unsigned long *__min_util, unsigned long *__max_util,
				  bool record_uclamp);

#if IS_ENABLED(CONFIG_RT_GROUP_SCHED)
static inline int rt_rq_throttled(struct rt_rq *rt_rq)
{
	return rt_rq->rt_throttled && !rt_rq->rt_nr_boosted;
}
#else /* !CONFIG_RT_GROUP_SCHED */
/*
 * rodin 6.9 降级论证（本簇唯一一处跟随上游的语义变化）：
 * 6.18 把 rt_throttled / rt_time / rt_runtime 整体移入 #ifdef CONFIG_RT_GROUP_SCHED
 * （kernel/sched/sched.h:854-864，上游 5f6bd380c7bd "sched/rt: Remove default
 * bandwidth control"），非 group 构建下该字段不存在，原样引用必然编不过。
 * 上游自身在非 group 构建下同样直接判定"未节流"（kernel/sched/rt.c:946-949 的
 * #else 支 `return false;`）⇒ 此处与上游语义对齐。
 * 行为影响：调用点（sched/fair.c、sched/eas/vip.c）用
 * `!rt_rq_throttled(&rq->rt)` 判断"RT 未被节流才允许 CFS 抢核"；退化后恒为
 * "未节流" ⇒ CFS 抢占更激进。这不是丢功能，而是 6.18 删掉默认 RT 带宽控制后的
 * 既定语义（终验已在重载 RT 场景观察功耗/延迟）。
 */
static inline int rt_rq_throttled(struct rt_rq *rt_rq)
{
	return false;
}
#endif

extern int set_target_margin(int gearid, int margin);
extern int set_target_margin_low(int gearid, int margin);
extern int set_turn_point_freq(int gearid, unsigned long freq);

#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
extern bool sysctl_util_est;
#endif

static inline bool is_util_est_enable(void)
{
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
	return sysctl_util_est;
#else
	return true;
#endif
}

void set_dsu_idle_enable(bool boost_ctrl);
void unset_dsu_idle_enable(void);
bool is_dsu_idle_enable(void);

void set_runnable_boost_enable(bool boost_ctrl);
void unset_runnable_boost_enable(void);
bool is_runnable_boost_enable(void);

unsigned long mtk_cpu_util_next(int cpu, struct task_struct *p, int dst_cpu, int boost);

unsigned long mtk_cpu_util_cfs(int cpu);
unsigned long mtk_cpu_util_cfs_boost(int cpu);
void mtk_cpu_util_cfs_boost_hook(void *data, int cpu, unsigned long *util);

#define EAS_NODE_NAME "eas_info"
#define EAS_PROP_CSRAM "csram-base"
#define EAS_PROP_OFFS_CAP "offs-cap"
#define EAS_PROP_OFFS_THERMAL_S "offs-thermal-limit"

struct eas_info {
	unsigned int csram_base;
	unsigned int offs_cap;
	unsigned int offs_thermal_limit_s;
	bool available;
};

void parse_eas_data(struct eas_info *info);

#endif /* _SCHED_COMMON_H */
