/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */
#ifndef _TASK_TURBO_H_
#define _TASK_TURBO_H_

#include <linux/list.h>
#include <linux/smp.h>
#include <linux/list_sort.h>
#include <common.h>

#define get_task_turbo_t(p)	\
	(&((struct mtk_task *)&(p)->android_vendor_data1)->turbo_data)
#define get_inherit_task(parent)	\
	((struct task_struct *)((u64)(parent)->android_vendor_data1))
#define get_vip_t(p)	\
	(&((struct mtk_task *)&(p)->android_vendor_data1)->vip_task)
#define TOUCH_DOWN 1
#define TOUCH_SUSTAIN_MS 2000
#define INVALID_TGID -1
#define INVALID_VAL -1
#define INVALID_LOADING -1

struct list_head;

enum {
	START_INHERIT = -1,
	RWSEM_INHERIT = 0,
	BINDER_INHERIT,
	END_INHERIT,
};

enum {
	SUB_FEAT_LOCK		= 1U << 0,
	SUB_FEAT_BINDER		= 1U << 1,
	SUB_FEAT_SCHED		= 1U << 2,
	SUB_FEAT_FLAVOR_BIGCORE = 1U << 3,
};

enum rwsem_waiter_type {
	RWSEM_WAITING_FOR_WRITE,
	RWSEM_WAITING_FOR_READ
};

struct rwsem_waiter {
	struct list_head list;
	struct task_struct *task;
	enum rwsem_waiter_type type;
	unsigned long timeout;
	bool handoff_set;
};

struct hmp_domain {
	struct cpumask cpus;
	struct cpumask possible_cpus;
	struct list_head hmp_domains;
};

struct cluster_info {
	struct hmp_domain *hmpd;
	unsigned long cpu_perf;
	int cpu;
};

struct cpu_time {
	u64 time;
};

struct cpu_info {
	int *cpu_loading;
};

extern int (*task_turbo_enforce_ct_to_vip_fp)(int val, int caller_id);
extern inline int get_vip_task_prio(struct task_struct *p);

/*
 * Nice levels are multiplicative, with a gentle 10% change for every
 * nice level changed. I.e. when a CPU-bound task goes from nice 0 to
 * nice 1, it will get ~10% less CPU time than another CPU-bound task
 * that remained on nice 0.
 *
 * The "10% effect" is relative and cumulative: from _any_ nice level,
 * if you go up 1 level, it's -10% CPU usage, if you go down 1 level
 * it's +10% CPU usage. (to achieve that we use a multiplier of 1.25.
 * If a task goes up by ~10% and another task goes down by ~10% then
 * the relative distance between them is ~25%.)
 */
/*
 * rodin 6.9：原先在 CONFIG_ARM64 分支里**定义**了这两张表（#else 分支才用 extern）。
 * 6.18 已在 kernel/sched/core.c:11372/11391 定义同名同型数组并 EXPORT_SYMBOL_GPL，
 * 内建同图必 multiple definition ⇒ 统一改走内核版。
 * 逐值比对（本次实测，全 40 项）：weight 与 wmult 两表与 6.18 core.c 的数组
 * **完全一致**（weight: 88761…15；wmult: 48388…286331153）⇒ 零语义损失。
 */
extern const int sched_prio_to_weight[40];

/*
 * Inverse (2^32/x) values of the sched_prio_to_weight[] array, precalculated.
 *
 * In cases where the weight does not change often, we can use the
 * precalculated inverse to speed up arithmetics by turning divisions
 * into multiplications:
 */
extern const u32 sched_prio_to_wmult[40];

#endif /* _PERF_TRACKER_H */
