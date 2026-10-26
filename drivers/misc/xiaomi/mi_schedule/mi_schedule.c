// SPDX-License-Identifier: GPL-2.0
/* Task-state carrier for the Xiaomi policy modules: per-task mi_task_struct
 * slots in the GKI task tail, cluster typing and 32-bit compat UID tracking.
 * The stock per-UID/per-cluster runtime accounting is dropped: 6.18 owns
 * load tracking (PELT/schedstat), and the stock producers (fork and runtime
 * feeds) were closed-scheduler call sites. Fork production now attaches to
 * the native android_rvh_sched_fork hook. */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/unaligned.h>
#include <trace/hooks/sched.h>
#include "kernel/sched/sched.h"
#include <linux/vseq.h>
#include "../../mediatek/sched/rodin_tail.h" /* rodin 尾部双段：mi_task_slot_of() */
#include "mi_schedule.h"

#define UID_LIMIT 100000
#define STOCK_NR_CPUS 32
static int cpu_cluster_map[STOCK_NR_CPUS + 1];
static DECLARE_BITMAP(package_uid_32B, UID_LIMIT);
static struct kmem_cache *mi_task_struct_cachep;

bool mi_task_exiting(struct task_struct *p)
{
	return !p || p->exit_state || READ_ONCE(p->__state) == TASK_DEAD;
}
EXPORT_SYMBOL(mi_task_exiting);

static struct mi_task_struct *get_mi_task_struct_unsafe(struct task_struct *p)
{
	if (!p || mi_task_slot_of(p)->magic != MI_TASK_OEM_MAGIC)
		return NULL;
	return (void *)mi_task_slot_of(p)->ptr;
}

struct mi_task_struct *get_mi_task_struct(struct task_struct *p)
{
	if (mi_task_exiting(p))
		return NULL;
	return get_mi_task_struct_unsafe(p);
}
EXPORT_SYMBOL(get_mi_task_struct);

/* Cluster typing follows the native arch_topology cluster_sibling mask;
 * stock scanned cpu_topology[].cluster_id itself, which is the same data. */
static void mi_cpu_cluster_map_init(void)
{
	int i, j, cluster = 1;

	for_each_possible_cpu(i) {
		if (i >= STOCK_NR_CPUS)
			break;
		if (cpu_cluster_map[i])
			continue;
		for_each_possible_cpu(j) {
			if (j >= STOCK_NR_CPUS)
				break;
			if (cpumask_test_cpu(j, topology_cluster_cpumask(i)))
				cpu_cluster_map[j] = cluster;
		}
		cluster++;
	}
	cpu_cluster_map[STOCK_NR_CPUS] = 1;
}

int get_cluster_type(unsigned int cpu)
{
	int mapped;

	if (!cpu_cluster_map[STOCK_NR_CPUS])
		mi_cpu_cluster_map_init();
	/* Keep the stock three-cluster cap: anything past cluster 2 folds to 0. */
	mapped = cpu <= STOCK_NR_CPUS ? cpu_cluster_map[cpu] : 0;
	return mapped > 3 ? 0 : mapped - 1;
}
EXPORT_SYMBOL(get_cluster_type);

u32 is_32B_user(int uid)
{
	/* Stock deliberately had no lower-bound check; the proc writer that
	 * could feed negative UIDs is gone, and callers pass cached s32 UIDs
	 * that may be -1, so the guard is required for a sane bitmap index. */
	if (uid < 0 || uid >= UID_LIMIT)
		return 0;
	return test_bit(uid, package_uid_32B);
}
EXPORT_SYMBOL(is_32B_user);

/* Only the bytes written by stock are initialized: .text+0x241c..0x2580,
 * .text+0x2ec0..0x3208. A blanket memset would change metis state. */
static void init_mi_task(struct mi_task_struct *t, struct task_struct *p)
{
	static const u16 zero64[] = {
		0x230, 0x238, 0x240, 0x248, 0x250,
		0x1b0, 0x1b8, 0x1c0, 0x1d8, 0x1e0, 0x1e8, 0x1f0,
		0x1f8, 0x200, 0x278, 0x280, 0x288, 0x290,
		0x2b0, 0x2b8, 0x2d0, 0x2d8, 0x328,
	};
	int i;
	t->magic = MI_STATE_MAGIC;
	t->guard = 0xdcbadcbadcbadcbaULL;
	t->user_linked = 0;
	t->work_pending = 0;
	t->unknown_ba = 0;
	put_unaligned(0xffffffffULL, (u64 *)(t->raw + 0xbc));
	t->task = p;
	t->initial_exec_runtime = p->se.sum_exec_runtime;
	memset(t->raw + 0x110, 0, 0x90);
	MI_FIELD(t, u32, 0x1a0) = 0;
	MI_FIELD(t, u32, 0x1c8) = 0;
	MI_FIELD(t, u32, 0x208) = 0;
	MI_FIELD(t, u32, 0x298) = 0xffffffff;
	t->unknown_c8 = 0;
	t->rb_magic_270 = 0;
	RB_CLEAR_NODE(&t->rb_node_258);
	INIT_LIST_HEAD(&t->node_2c0);
	MI_FIELD(t, u64, 0x2f8) = ~0ULL;
	for (i = 0; i < ARRAY_SIZE(zero64); i++)
		MI_FIELD(t, u64, zero64[i]) = 0;
}

/* rodin 6.18：fork 生产者 = android_rvh_sched_fork（kernel/sched/core.c
 * copy_process 路径）。stock 由闭源调度器直接调用，6.18 换原生挂点。 */
void mi_task_fork(void *unused, struct task_struct *p)
{
	struct mi_task_struct *t;

	if (!p)
		return;
	mi_task_slot_of(p)->ptr = 0;
	mi_task_slot_of(p)->magic = 0;
	t = in_interrupt() ? NULL : kmem_cache_alloc(mi_task_struct_cachep, GFP_KERNEL);
	if (!t) {
		pr_err("mi-sched: tsk %s %d alloc mi_task failed\n", p->comm, p->pid);
		return;
	}
	init_mi_task(t, p);
	if (test_tsk_thread_flag(p, TIF_32BIT) && task_uid(p).val < UID_LIMIT)
		set_bit(task_uid(p).val, package_uid_32B);
	mi_task_slot_of(p)->ptr = (unsigned long)t;
	mi_task_slot_of(p)->magic = MI_TASK_OEM_MAGIC;
}

void free_mi_task_struct(void *unused, struct task_struct *p)
{
	struct mi_task_struct *t;

	if (mi_task_slot_of(p)->magic != MI_TASK_OEM_MAGIC)
		return;
	t = (void *)mi_task_slot_of(p)->ptr;
	mi_task_slot_of(p)->magic = 0;
	if (t->magic != MI_STATE_MAGIC)
		return;
	t->magic = 0;
	kmem_cache_free(mi_task_struct_cachep, t);
}

/* One-shot sweep for tasks that predate the fork hook (initcall time). */
static void mi_task_world_init(void)
{
	struct task_struct *g, *p;
	struct mi_task_struct *t;
	int failed = 0, count = 0;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		if (test_tsk_thread_flag(p, TIF_32BIT)) {
			u32 uid = task_uid(p).val;

			if (uid < UID_LIMIT)
				set_bit(uid, package_uid_32B);
		}
		if (mi_task_slot_of(p)->magic == MI_TASK_OEM_MAGIC && mi_task_slot_of(p)->ptr) {
			count++;
			continue;
		}
		t = in_interrupt() ? NULL : kmem_cache_alloc(mi_task_struct_cachep, GFP_ATOMIC);
		if (t) {
			init_mi_task(t, p);
			mi_task_slot_of(p)->ptr = (unsigned long)t;
			mi_task_slot_of(p)->magic = MI_TASK_OEM_MAGIC;
			count++;
			continue;
		}
		pr_err("mi-sched: tsk %s %d alloc mi_task failed\n", p->comm, p->pid);
		failed++;
	}
	rcu_read_unlock();
	pr_info("mi-sched: Mi-task world init suc %d, fai %d\n", count, failed);
}

static int __init mi_sched_init(void)
{
	pr_err("in %s\n", __func__);
	if (!mi_task_slot_ok()) {
		pr_err("mi-sched: task tail slot unavailable (android_arch_task_struct_size too small); carrier disabled\n");
		return 0;
	}
	mi_task_struct_cachep = kmem_cache_create("mi_task_struct", MI_TASK_STATE_SIZE, 0,
		SLAB_ACCOUNT | SLAB_PANIC | SLAB_HWCACHE_ALIGN, NULL);
	mi_cpu_cluster_map_init();
	register_trace_android_vh_free_task(free_mi_task_struct, NULL);
	register_trace_android_rvh_sched_fork(mi_task_fork, NULL);
	mi_task_world_init();
	return 0;
}
static void __exit mi_sched_exit(void)
{
	pr_err("in %s\n", __func__);
	kmem_cache_destroy(mi_task_struct_cachep);
}
vseq_module_init(mi_sched_init);
module_exit(mi_sched_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mi task-state carrier (cluster type, 32B uid map, per-task slots)");
