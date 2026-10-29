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
#include <linux/sched/task.h>	/* android_task_vendor_data(): GKI 动态 task_struct 尾部载体 */
#include <linux/minmax.h>

#define MTK_VENDOR_DATA_SIZE_TEST(mstruct, kstruct)		\
	BUILD_BUG_ON(sizeof(mstruct) > (sizeof(u64) *		\
		ARRAY_SIZE(((kstruct *)0)->android_vendor_data1)))


/* rodin 6.18（A-62/A-75 口径）：钩子账里的"真在队列上"判定。语义 =
 * task_on_rq_queued(p) && !p->se.sched_delayed；此处按
 * kernel/sched/sched.h:2366 的实现原样内联，以免 const 形参在
 * task_on_rq_queued()（形参非 const）上丢限定符。
 *
 * 6.18 core 对阻塞任务默认走 delayed dequeue：dequeue_task_fair() 拒绝出队、
 * 置 se->sched_delayed = 1 后提前返回，block_task() 于是跳过下线 ⇒ 任务保持
 * p->on_rq == TASK_ON_RQ_QUEUED 却已不在运行队列语义里（下称僵尸）。
 *
 * 这是记账口径，不含搬家资格语义：PROXY_EXEC 下 blocked_on 在身的 QUEUED
 * 任务（阻塞链在册成员，donor 代等、唤醒后才清 blocked_on）在本判据下为真
 * ——flt 的 enqueue/dequeue 钩子（flt_cal.c，只认 sched_delayed 不认
 * blocked_on）正是这样计它的 ⇒ group_nr_running 的 add/remove 修正
 * （group.c）与 nice 备份的 SAVE/RESTORE 周期（task_turbo.c）必须用本判据、
 * 不得换成 mtk_task_migratable()：修正掺进搬家门即与钩子失配——blocked 时
 * 移出组会永久漏一个 -1（任务离组后钩子不再为它发 dequeue），
 * group_nr_running 从此虚高，grp_awr 的目标 util 随之被带偏。
 */
static inline bool mtk_task_on_rq_active(const struct task_struct *p)
{
	return READ_ONCE(p->on_rq) == TASK_ON_RQ_QUEUED && !p->se.sched_delayed;
}

/* 搬家资格 = mtk_task_on_rq_active() + blocked_on 门，只给搬家判定点用。
 *
 * A-75：core 自己所有"把任务当活任务搬/激活"的地方都显式排除
 * se.sched_delayed（can_migrate_task() kernel/sched/fair.c:9846 直接
 * return 0；__set_next_task_fair()/switched_to_fair()/try_to_wake_up() 均带
 * WARN_ON_ONCE(se->sched_delayed) 断言；pick_next_entity() 对选中的 delayed
 * 实体先 dequeue_entities(DEQUEUE_SLEEP|DEQUEUE_DELAYED) 收尾再返回 NULL）。
 * 厂商迁移器原先只看 task_on_rq_queued()：会把僵尸 deactivate_task() 真出队
 * （flags 不带 DEQUEUE_DELAYED ⇒ finish_delayed_dequeue_entity() 不执行、
 * sched_delayed 残留；也不带 DEQUEUE_SLEEP ⇒ h_nr_runnable 走"不算 runnable"
 * 分支），随后再重新入树，造成 se->on_rq / p->on_rq / sched_delayed /
 * h_nr_runnable 四者裂脑，与真唤醒或任务自身 block 并发时崩在
 * dequeue_entity()→update_entity_lag() 的 WARN_ON_ONCE(!se->on_rq)。所有
 * "挑任务迁移/激活"的判定点统一走本函数。
 *
 * rodin 2026-10-29 冻结线修复（rq convoy 定案）：PROXY_EXEC 下 blocked_on
 * 在身的 QUEUED 任务是阻塞链在册成员（donor 代等、唤醒后才清 blocked_on），
 * 核心自身的迁移路径从不对它做裸搬移（can_migrate_task() 里
 * task_is_blocked() 一票否决；balance_push 只推 curr，阻塞者原地等链解）；
 * 厂商搬家点（vip push / fair 迁移 ×3 / core_pause drain）经本判据放行时
 * 绕过 proxy 链维护 → find_proxy_task/activate_blocked_waiters 走链不终止
 * → 持 rq 锁永挂 → 其余核 rq convoy 逐核饿死（两份死亡帧多核 rq 队列级联
 * + expdb CPU0 slowpath ×102 实证）。补上核心同款门：blocked_on 非空一律
 * 不可裸迁移；拒绝时打点留证（现场窗口本就瞬时，ratelimited 足够）。
 */
static inline bool mtk_task_migratable(const struct task_struct *p)
{
	if (READ_ONCE(p->blocked_on.lock)) {
		pr_info_ratelimited("mtkmig: refuse blocked-on %s[%d]\n",
				    p->comm, p->pid);
		return false;
	}
	return mtk_task_on_rq_active(p);
}

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

/* rodin 6.18（分区 A · 载体迁移）：per-task 载体从 task_struct 静态槽
 * （android_vendor_data1，6.18 上游缩到 6×u64=48B）迁到 GKI 动态尾部：
 * CONFIG_GKI_DYNAMIC_TASK_STRUCT_SIZE=y 时 task_struct 按 arch_task_struct_size
 * 分配，尾部由 android_task_vendor_data(p) 取得（init_task 特例 = vendor_data_pad[]）。
 * 尾部大小 = boot cmdline `android_arch_task_struct_size=N`（本树 gki_defconfig 内置
 * 512），上限 CONFIG_GKI_TASK_STRUCT_VENDOR_SIZE_MAX（1280）。
 * 编译期只可断"需求 <= 上限"；真实可用量是运行期量 ⇒ 见 mtk_task_data_ok() 与各 init 守卫。
 */
/* 注：BUILD_BUG_ON 展开含 do{}while，只能用在函数内；头文件级用 static_assert()。 */
#define MTK_VENDOR_DATA_SIZE_MAX_TEST(mstruct) \
	static_assert(sizeof(mstruct) <= CONFIG_GKI_TASK_STRUCT_VENDOR_SIZE_MAX, \
		      #mstruct " 超过 GKI 尾部上界（CONFIG_GKI_TASK_STRUCT_VENDOR_SIZE_MAX）")

MTK_VENDOR_DATA_SIZE_MAX_TEST(struct mtk_task);

static __always_inline struct mtk_task *mtk_task_of(const struct task_struct *p)
{
	return (struct mtk_task *)android_task_vendor_data((struct task_struct *)p);
}

/* 运行期实配校验：尾部（cmdline android_arch_task_struct_size）是否 >= 需求。
 * 不足时调用方不得注册任何会触碰 mtk_task 的钩子（否则越界写 slab 邻区）。
 */
static __always_inline bool mtk_task_data_ok(void)
{
	return arch_task_struct_size >=
	       (int)(sizeof(struct task_struct) + sizeof(struct mtk_task));
}

/* 载体不可用（= 尾部不足）：init 处统一 pr_err 报明原因；入口处静默退化。 */
#define MTK_TASK_DATA_ABSENT()	unlikely(!mtk_task_data_ok())

/* 尾部双段布局声明拆至 rodin_tail.h（metis/mi_schedule 独立单元共享，
 * 避开 kernel/sched/sched.h 私有依赖链）。 */
#include "rodin_tail.h"

/* 尾段偏移对偶锁：rodin_tail.h 的 mi 槽起址 = mtk_task 实际尺寸 */
static_assert(sizeof(struct mtk_task) == RODIN_TAIL_MTK_SIZE,
		"rodin_tail.h RODIN_TAIL_MTK_SIZE 与 struct mtk_task 尺寸漂移");

#define mtk_task_data_guard_fail(tag)							\
do {												\
	pr_err("%s: task_struct 尾部实配 %d 字节 < 需求 %d 字节 —— boot cmdline 需含 "\
	       "android_arch_task_struct_size=%d（上限 %d）；本族钩子不注册（安全降级）\n", \
	       (tag), arch_task_struct_size,				\
	       (int)(sizeof(struct task_struct) + sizeof(struct mtk_task)),\
	       (int)(sizeof(struct task_struct) + sizeof(struct mtk_task)),\
	       CONFIG_GKI_TASK_STRUCT_VENDOR_SIZE_MAX);		\
} while (0)

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
				unsigned long min_cap, unsigned long max_cap,
				unsigned long *min_out, unsigned long *max_out);
#endif

/*
 * rodin 6.18（A-116/B2）：消费面出口的包络夹取原语 —— 6.18
 * sugov_effective_cpu_perf()（kernel/sched/cpufreq_schedutil.c:216-231）去掉
 * map_util_perf() headroom 的本树等价形：
 *
 *	max(min, min(util, max))   ←  max(min, min(1.25 * util, max)) 去掉 1.25
 *
 * 本树 headroom（adaptive_margin / util_scale）归 mtk_map_util_freq()，在
 * CAP→FREQ 之前统一施加 ⇒ 此处不得再乘 1.25（否则双计 1.5625×）。
 * [min, max] 来自 mtk_cpu_util() 的 min_out/max_out（出参非空路径）。
 */
static inline unsigned long mtk_eff_perf(unsigned long util,
					 unsigned long min, unsigned long max)
{
	return max(min, min(util, max));
}
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
