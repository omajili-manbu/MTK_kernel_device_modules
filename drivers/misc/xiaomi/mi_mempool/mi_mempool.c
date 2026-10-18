// SPDX-License-Identifier: GPL-2.0-only
/*
 * mi_mempool - reserve page pools served to whitelisted core processes on
 * allocation failure / direct-reclaim throttle (rodin 6.18 rebuild).
 *
 * Provenance: rebuilt against the rodin device blob
 * vendor_dlkm/lib/modules/mi_mempool.ko (vermagic 6.6.77-android15-8,
 * author zhouwenhao, single TU, depends=) function by function.  The blob's
 * own build path records
 * ../kernel_device_modules-6.6/drivers/staging/mi-perf/mi_mempool/mi-mempool.c,
 * so the sm8550 tree copy of that same file served as the skeleton donor and
 * the CCC/dash OS3.0.305 reconstruction as the generational reference; the
 * rodin blob's machine code / .data / .rodata is the sole truth for every
 * struct offset, tunable, whitelist entry and hook registration below
 * (evidence: tools/_b260_mempool, desktop mi_mempool.ko.asm).  The
 * _Static_asserts fail the build on layout drift against those offsets.
 *
 * 6.6 -> 6.18 rewrites (deliberate, semantics preserving):
 *   - __alloc_pages(gfp, order, 0, NULL)    -> alloc_pages(gfp, order)
 *   - blob's local copy of the kernel's mte_zero_clear_page_tags()
 *       -> the kernel helper itself (EXPORTless but built-in linkable;
 *       identical dc gzva/stz2g body)
 *   - tracing_mark_write() ftrace marker    -> trace_puts()
 *   - hand-rolled GFP_ZONE_TABLE decode     -> gfp_zone()
 *   - MAX_ORDER                             -> MAX_PAGE_ORDER
 *   - 6.6 alloc_flags mask 0x238 (ALLOC_HARDER|ALLOC_HIGH|ALLOC_CPUSET|
 *       ALLOC_NON_BLOCK) -> 6.18 ALLOC_MIN_RESERVE|ALLOC_NON_BLOCK (0x30;
 *       mm/internal.h private values, this tree verified)
 *   - per-cpu vm_stat_diff snapshot loop    -> zone_page_state_snapshot()
 *       (blob loops CPUs 0..31, a stock CONFIG_NR_CPUS=32 artifact)
 *   - tracepoint_probe_register/unregister  -> register_trace_/unregister_trace_
 * Blob quirks kept verbatim (stock truth): the stale "Failed to register
 * mmput hook." text on the free_unref_page_bypass path, order==11 in
 * alloc_oom aliasing oom_use_failure_count[11] onto whitelist_check_num,
 * and the dead exported invalid_pid_whitelist_free() helper (unreferenced
 * in the blob; no behavioral surface, not carried over).
 */

#include <linux/atomic.h>
#include <linux/gfp.h>
#include <linux/kasan-enabled.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/sysfs.h>
#include <linux/vmstat.h>
#include <asm/mte.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/sched.h>
#include <linux/vseq.h>

/* blob .rodata.cst16 / .bss truth */
#define POOL_COUNT		4
#define COMM_COUNT		7			/* comm_whitelist entries */
#define PID_COUNT		1			/* single pid slot */
#define PROCESS_COUNT		(COMM_COUNT + PID_COUNT)	/* ids 1..8 */
#define ORDER_COUNT		(MAX_PAGE_ORDER + 1)		/* bounds: order < 11 */
#define NUM_LIMITED_ORDER	2

#define CONFIG_USAGE		BIT(0)
#define CONFIG_DEBUG		BIT(1)
#define CONFIG_TREE_DEBUG	BIT(2)

#define MI_DYNAMIC_POOL_LOW_MARK_PERCENT		40
#define MI_DYNAMIC_POOL_REFILL_MIN_INTERVAL_MS		300
#define MI_DYNAMIC_POOL_REFILL_MIN_CHECK_INTERVAL_MS	10
#define MI_DYNAMIC_POOL_OOM_DEFAULT_LOW_PERCENT		10
#define MI_DYNAMIC_POOL_MAX_FIRST_REFILL_RETRIES	16
#define MI_DYNAMIC_POOL_DEFAULT_DIRECT_RELCAIM_THRESHOLD_MB 11
#define INVALID_ADJ					-2000

#define BYTES_PER_MB (1024UL * 1024UL)

#define MI_MEMPOOL_ATTR_RO(_name)	\
	struct kobj_attribute kobj_attr_##_name =	\
		__ATTR(_name, 0440, _name##_show, NULL)
#define MI_MEMPOOL_ATTR_RW(_name)	\
	struct kobj_attribute kobj_attr_##_name =	\
		__ATTR(_name, 0660, _name##_show, _name##_store)

/* both formulas match the blob's .rodata.cst16 constants bit for bit */
#define HIGH_ORDER_GFP	((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN | \
			  __GFP_NORETRY) & ~__GFP_RECLAIM)
#define LOW_ORDER_GFP	(GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN)

/*
 * 6.18 replacement for the blob's alloc_flags & 0x238 test (6.6
 * ALLOC_HARDER|ALLOC_HIGH|ALLOC_CPUSET|ALLOC_NON_BLOCK; ALLOC_CPUSET no
 * longer exists).  mm/internal.h is private; values verified against this
 * tree (ALLOC_NON_BLOCK 0x10, ALLOC_MIN_RESERVE 0x20).
 */
#define MI_ALLOC_RESERVES 0x30

struct mi_process_stat {
	atomic_t reclaim_success;
	atomic_t reclaim_failure;
	atomic_t oom_success;
	atomic_t oom_failure;
};

struct mi_dynamic_page_pool {
	/* offsets pinned to blob; asserted below */
	spinlock_t lock;			/* +0x00 */
	gfp_t gfp_mask;				/* +0x04 */
	int order;				/* +0x08 */
	struct list_head list;			/* +0x10 */
	atomic_t count;				/* +0x20 */
	atomic_t reclaim_use_count;		/* +0x24 */
	atomic_t oom_use_count;			/* +0x28 */
	int low_mark;				/* +0x2c */
	int high_mark;				/* +0x30 */
	int direct_reclaim_mark;		/* +0x34 */
	ktime_t last_check_watermark_time;	/* +0x38 */
	int oom_low_mark[PROCESS_COUNT];	/* +0x40 */
	bool first_filled;			/* +0x60 */
	int first_fill_retries;			/* +0x64 */
	struct mi_process_stat stats[PROCESS_COUNT];	/* +0x68 */
};

struct mi_mempool {
	struct mi_dynamic_page_pool **pools;	/* +0x00 */
	struct task_struct *refill_worker;	/* +0x08 */
	atomic_long_t last_refill_time;		/* +0x10, ms */
	int last_refill_count;			/* +0x18 */
	int refill_count;			/* +0x1c */
	unsigned long try_to_free_pages_threshold;	/* +0x20, pages */
	unsigned long direct_reclaim_threshold;		/* +0x28, pages, -1 = auto */
	bool refill_pending;			/* +0x30 */
	bool free_page_wakeup;			/* +0x31 */
	spinlock_t wakeup_lock;			/* +0x34 */
	bool thread_woken;			/* +0x38 */
	int config;				/* +0x3c */
	struct kobject *kobj;			/* +0x40 */
	atomic_t total_page_num;		/* +0x48 */
	atomic_t oom_use_failure_count[ORDER_COUNT];	/* +0x4c */
	atomic_t whitelist_check_num;		/* +0x78 */
	atomic_t proc_node_alloc_failure;	/* +0x7c */
	int proc_tree_size;			/* +0x80 */
};

struct proc_node {
	struct rb_node rb_node;
	pid_t pid;				/* +0x18 */
	int process_id;				/* +0x1c */
};

_Static_assert(sizeof(struct mi_dynamic_page_pool) == 0xe8, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, list) == 0x10, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, count) == 0x20, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, low_mark) == 0x2c, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, direct_reclaim_mark) == 0x34, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, oom_low_mark) == 0x40, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, first_filled) == 0x60, "pool layout drift vs blob");
_Static_assert(offsetof(struct mi_dynamic_page_pool, stats) == 0x68, "pool layout drift vs blob");
/* blob bss: g_mi_mempool occupies 0x88 (content to +0x84, 4 bytes tail pad) */
_Static_assert(sizeof(struct mi_mempool) == 0x88, "mempool layout drift vs blob");
_Static_assert(offsetof(struct mi_mempool, try_to_free_pages_threshold) == 0x20, "mempool layout drift vs blob");
_Static_assert(offsetof(struct mi_mempool, wakeup_lock) == 0x34, "mempool layout drift vs blob");
_Static_assert(offsetof(struct mi_mempool, config) == 0x3c, "mempool layout drift vs blob");
_Static_assert(offsetof(struct mi_mempool, oom_use_failure_count) == 0x4c, "mempool layout drift vs blob");
_Static_assert(offsetof(struct mi_mempool, whitelist_check_num) == 0x78, "mempool layout drift vs blob");
_Static_assert(sizeof(struct proc_node) == 0x20, "proc_node layout drift vs blob");
_Static_assert(offsetof(struct proc_node, pid) == 0x18, "proc_node layout drift vs blob");
/*
 * blob 的阈值 sysfs 以“页数右移 8”当 MB 输出（4K 页语义）；这里按 PAGE_SHIFT
 * 推导同一换算，4K 下与 blob 逐位等价，16K/64K 下仍是页数 -> MB。
 */
#define MI_PAGES_TO_MB_SHIFT (20 - PAGE_SHIFT)
_Static_assert(MI_PAGES_TO_MB_SHIFT >= 0, "page size exceeds 1MB");

static struct mi_mempool g_mi_mempool;
static struct mi_mempool *pg_mi_mempool;
static struct rb_root proc_tree = RB_ROOT;
static DEFINE_SPINLOCK(proc_tree_lock);
static struct kmem_cache *proc_node_cachep;

/* blob .rodata/.init truth */
static const gfp_t order_flags[POOL_COUNT] = {
	LOW_ORDER_GFP, HIGH_ORDER_GFP, HIGH_ORDER_GFP, HIGH_ORDER_GFP,
};
static const unsigned int orders[POOL_COUNT] = { 0, 1, 4, 5 };
static const unsigned int high_mark_mb[POOL_COUNT] = { 64, 1, 1, 1 };
static const unsigned int direct_reclaim_mark_mb[POOL_COUNT] = { 32, 1, 1, 1 };
static unsigned int order_map[ORDER_COUNT];
static const unsigned int refill_limited_order[NUM_LIMITED_ORDER] = { 4, 5 };
static char *comm_whitelist[COMM_COUNT] = {
	"composer@3.3-se", "surfaceflinger", "com.miui.home",
	"ndroid.systemui", "system_server", ".globallauncher", "app_main",
};
static int comm_whitelist_sizes[COMM_COUNT];
static const int adj_whitelist[COMM_COUNT] = {
	-1000, -1000, INVALID_ADJ, INVALID_ADJ, -900, INVALID_ADJ, INVALID_ADJ,
};
static int pid_whitelist[PID_COUNT];
static bool invalid_pid_whitelist[PID_COUNT];
static const struct {
	int order;
	int process_id;		/* -1: every whitelisted process */
	int oom_low_mark_percent;
} oom_low_mark_policys[] = {
	{ 1, -1, MI_DYNAMIC_POOL_LOW_MARK_PERCENT },
	{ 0, 4, 60 },
	{ 4, -1, 100 },
	{ 5, -1, 100 },
};

/* Preserve the stock 32-bit shift / sign extend / page-shift sequence. */
static unsigned long mb_to_pages(unsigned int mb)
{
	return (unsigned long)(long)(s32)((u32)mb << 20) >> PAGE_SHIFT;
}

static struct proc_node *get_proc_node_by_pid_locked(pid_t pid)
{
	struct rb_node *node = proc_tree.rb_node;
	struct proc_node *ret = NULL;

	while (node) {
		ret = rb_entry(node, struct proc_node, rb_node);
		if (ret->pid > pid)
			node = node->rb_right;
		else if (ret->pid < pid)
			node = node->rb_left;
		else
			return ret;
	}
	return NULL;
}

static struct proc_node *insert_proc_node_by_pid_locked(struct proc_node *p_node)
{
	struct rb_node **p = &proc_tree.rb_node;
	struct rb_node *parent = NULL;
	struct proc_node *ret = NULL;

	while (*p) {
		parent = *p;
		ret = rb_entry(*p, struct proc_node, rb_node);
		if (ret->pid > p_node->pid)
			p = &parent->rb_right;
		else if (ret->pid < p_node->pid)
			p = &parent->rb_left;
		else
			return ret;
	}
	rb_link_node(&p_node->rb_node, parent, p);
	rb_insert_color(&p_node->rb_node, &proc_tree);
	pg_mi_mempool->proc_tree_size++;
	return NULL;
}

static struct proc_node *del_proc_node_by_pid_locked(pid_t pid)
{
	struct proc_node *node = get_proc_node_by_pid_locked(pid);

	if (node) {
		rb_erase(&node->rb_node, &proc_tree);
		pg_mi_mempool->proc_tree_size--;
	}
	return node;
}

static inline struct proc_node *proc_node_alloc(gfp_t gfp)
{
	return proc_node_cachep != NULL ? kmem_cache_alloc(proc_node_cachep, gfp) : NULL;
}

static void proc_node_free(struct proc_node *proc_node)
{
	if (proc_node_cachep != NULL)
		kmem_cache_free(proc_node_cachep, proc_node);
}

static bool proc_node_init(void)
{
	proc_node_cachep = kmem_cache_create("proc_node", sizeof(struct proc_node),
					     0, 0, NULL);

	return proc_node_cachep != NULL;
}

static void insert_new_proc_node(pid_t pid, int process_id)
{
	struct proc_node *node = proc_node_alloc(GFP_ATOMIC);

	if (node) {
		unsigned long flags;

		node->pid = pid;
		node->process_id = process_id;
		spin_lock_irqsave(&proc_tree_lock, flags);
		if (insert_proc_node_by_pid_locked(node))
			proc_node_free(node);
		spin_unlock_irqrestore(&proc_tree_lock, flags);
	} else if (pg_mi_mempool->config & CONFIG_TREE_DEBUG) {
		atomic_inc(&pg_mi_mempool->proc_node_alloc_failure);
	}
}

static void reset_proc_tree(void)
{
	struct proc_node *pos = NULL, *n = NULL;
	unsigned long flags;

	spin_lock_irqsave(&proc_tree_lock, flags);
	rbtree_postorder_for_each_entry_safe(pos, n, &proc_tree, rb_node) {
		proc_node_free(pos);
	}
	proc_tree = RB_ROOT;
	spin_unlock_irqrestore(&proc_tree_lock, flags);
}

static void free_all_proc_nodes_and_caches(void)
{
	reset_proc_tree();
	kmem_cache_destroy(proc_node_cachep);
}

static inline void mi_dynamic_page_pool_add(struct mi_dynamic_page_pool *pool,
					    struct page *page)
{
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	list_add(&page->lru, &pool->list);
	mod_node_page_state(NODE_DATA(0), NR_KERNEL_MISC_RECLAIMABLE,
			    1L << pool->order);
	spin_unlock_irqrestore(&pool->lock, flags);
	atomic_inc(&pool->count);
	atomic_add(1 << pool->order, &pg_mi_mempool->total_page_num);
}

static inline struct page *mi_dynamic_page_pool_remove(struct mi_dynamic_page_pool *pool)
{
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	page = list_first_entry_or_null(&pool->list, struct page, lru);
	if (page) {
		list_del(&page->lru);
		mod_node_page_state(NODE_DATA(0), NR_KERNEL_MISC_RECLAIMABLE,
				    -(1L << pool->order));
		spin_unlock_irqrestore(&pool->lock, flags);
		atomic_dec(&pool->count);
		atomic_sub(1 << pool->order, &pg_mi_mempool->total_page_num);
	} else {
		spin_unlock_irqrestore(&pool->lock, flags);
	}
	return page;
}

static inline struct mi_dynamic_page_pool *order_to_pool(int order)
{
	struct mi_dynamic_page_pool *pool = NULL;

	if (order >= 0 && order < ORDER_COUNT) {
		int pool_id = order_map[order];

		if (pool_id != -1)
			pool = pg_mi_mempool->pools[pool_id];
	}
	return pool;
}

static void trace_comm_whitelist(void)
{
	struct task_struct *leader = current->group_leader;
	char trace[256] = { 0 };

	snprintf(trace, sizeof(trace),
		 "I|%d|current->comm: %s, current->group_leader->comm: %s, "
		 "current->pid: %d, current->tgid: %d\n",
		 current->pid, current->comm, leader ? leader->comm : "",
		 current->pid, current->tgid);
	trace_puts(trace);
}

/*
 * Blob .text+0xd70.  Rodin form: 7 comm entries + 1 pid slot (ids 1..8);
 * the dash-generation rescue flag and count-threshold shortcuts do not
 * exist here.  Cache invalidation happens only in mi_mempool_free_task.
 */
static int check_whitelist(void)
{
	struct proc_node *node;
	struct task_struct *leader = current->group_leader;
	unsigned long flags;
	int process_id = 0;
	int i;

	if (pg_mi_mempool->config & CONFIG_TREE_DEBUG)
		atomic_inc(&pg_mi_mempool->whitelist_check_num);
	if ((pg_mi_mempool->config & CONFIG_DEBUG) && leader)
		trace_comm_whitelist();

	spin_lock_irqsave(&proc_tree_lock, flags);
	node = leader ? get_proc_node_by_pid_locked(leader->pid) : NULL;
	if (node) {
		process_id = node->process_id;
		spin_unlock_irqrestore(&proc_tree_lock, flags);
		return process_id;
	}
	spin_unlock_irqrestore(&proc_tree_lock, flags);

	for (i = 0; i < COMM_COUNT; i++) {
		if (adj_whitelist[i] != INVALID_ADJ &&
		    (!leader || leader->signal->oom_score_adj != adj_whitelist[i]))
			continue;
		if (leader &&
		    strncmp(leader->comm, comm_whitelist[i],
			    comm_whitelist_sizes[i]) == 0) {
			insert_new_proc_node(leader->pid, i + 1);
			if (pg_mi_mempool->config & CONFIG_DEBUG)
				trace_comm_whitelist();
			return i + 1;
		}
	}

	if (!invalid_pid_whitelist[0] && leader &&
	    leader->pid == pid_whitelist[0]) {
		insert_new_proc_node(leader->pid, COMM_COUNT + 1);
		if (pg_mi_mempool->config & CONFIG_DEBUG)
			trace_comm_whitelist();
		return COMM_COUNT + 1;
	}

	if (leader)
		insert_new_proc_node(leader->pid, 0);
	return 0;
}

static bool check_last_refill_time_interval(void)
{
	s64 delta;

	delta = ktime_to_ms(ktime_get()) -
		atomic_long_read(&pg_mi_mempool->last_refill_time);
	return delta < MI_DYNAMIC_POOL_REFILL_MIN_INTERVAL_MS;
}

/*
 * Blob inlines the stock __zone_watermark_ok shape: mark = low watermark +
 * watermark_boost + (1 << order); unusable = highatomic + (1 << order) - 1 +
 * CMA free; snapshot below the percpu_drift_mark; high orders additionally
 * need a free page in one of the first four migratetype lists; DMA32 is
 * skipped by name.  Returns true when refilling must stop.
 */
static bool check_watermark(struct mi_dynamic_page_pool *pool)
{
	enum zone_type classzone_idx = gfp_zone(pool->gfp_mask);
	struct zone *zone;
	int mark, z, o;

	for (z = classzone_idx; z >= 0; z--) {
		long free_pages;

		zone = &NODE_DATA(0)->node_zones[z];
		if (!strcmp(zone->name, "DMA32"))
			continue;
		free_pages = zone_page_state(zone, NR_FREE_PAGES);
		if (zone->percpu_drift_mark &&
		    free_pages < (long)zone->percpu_drift_mark)
			free_pages = zone_page_state_snapshot(zone, NR_FREE_PAGES);
		mark = low_wmark_pages(zone) + zone->watermark_boost +
		       (1 << pool->order);
		free_pages -= zone->nr_reserved_highatomic +
			      ((1 << pool->order) - 1) +
			      zone_page_state(zone, NR_FREE_CMA_PAGES);
		if (free_pages <= mark + zone->lowmem_reserve[classzone_idx]) {
			if (pg_mi_mempool->config & CONFIG_DEBUG)
				pr_info("mi_mempool %s name=%s pool->order=%d "
					"mark=%d free_pages=%d highatomic=%d cma=%d\n",
					"check_watermark", zone->name, pool->order,
					mark, (int)zone_page_state(zone, NR_FREE_PAGES),
					(int)zone->nr_reserved_highatomic,
					(int)zone_page_state(zone, NR_FREE_CMA_PAGES));
			return true;
		}
		if (!pool->order)
			continue;
		for (o = pool->order; o < ORDER_COUNT; o++) {
			struct free_area *area = &zone->free_area[o];
			int mt;

			if (!area->nr_free)
				continue;
			for (mt = 0; mt < MIGRATE_PCPTYPES; mt++)
				if (!list_empty(&area->free_list[mt]))
					goto next_zone;
		}
		/* high order, but no page of a suitable size is free */
		return true;
next_zone:
		;
	}
	return false;
}

/*
 * Blob .text+0x5764.  MTE gating is pure gfp, as in the blob: ZEROTAGS
 * together with SKIP_KASAN runs the tag-zeroing path, SKIP_KASAN alone only
 * clears the stale page flags and kasan tags, with neither nothing is done.
 */
static void prepare_pool_page(struct page *page, unsigned int order, gfp_t gfp)
{
	int i;

	if (gfp & __GFP_SKIP_KASAN) {
		clear_bit(PG_mte_lock, &page->flags.f);
		clear_bit(PG_mte_tagged, &page->flags.f);
		for (i = 0; i < (1 << order); i++) {
			struct page *p = page + i;

			if ((gfp & (__GFP_ZEROTAGS | __GFP_SKIP_KASAN)) ==
			    (__GFP_ZEROTAGS | __GFP_SKIP_KASAN)) {
				bool tagged = try_page_mte_tagging(p);

				WARN_ON(!tagged);
				/* blob carried a private copy of this kernel helper */
				mte_zero_clear_page_tags(page_address(p));
				set_page_mte_tagged(p);
			}
			page_kasan_tag_reset(p);
		}
	}
	if (order && (gfp & __GFP_COMP))
		/* EXPORT_SYMBOL_GPL in this kernel; the blob imported it too */
		prep_compound_page(page, order);
}

static struct page *mi_dynamic_page_pool_alloc(unsigned int order,
					       struct mi_dynamic_page_pool *pool,
					       gfp_t gfp_mask, bool oom)
{
	struct page *page;

	if (current == pg_mi_mempool->refill_worker ||
	    gfp_zone(gfp_mask) < gfp_zone(pool->gfp_mask))
		return NULL;
	page = mi_dynamic_page_pool_remove(pool);
	if (page) {
		atomic_inc(oom ? &pool->oom_use_count : &pool->reclaim_use_count);
		prepare_pool_page(page, order, gfp_mask);
	}
	if (pg_mi_mempool->refill_worker &&
	    atomic_read(&pool->count) <= pool->low_mark &&
	    !check_last_refill_time_interval() &&
	    !READ_ONCE(pg_mi_mempool->thread_woken)) {
		unsigned long flags;

		spin_lock_irqsave(&pg_mi_mempool->wakeup_lock, flags);
		if (!READ_ONCE(pg_mi_mempool->thread_woken)) {
			WRITE_ONCE(pg_mi_mempool->thread_woken, true);
			wake_up_process(pg_mi_mempool->refill_worker);
		}
		spin_unlock_irqrestore(&pg_mi_mempool->wakeup_lock, flags);
	}
	return page;
}

/*
 * Blob snapshots the per-cpu vm_stat_diff for CPUs 0..31 (stock
 * CONFIG_NR_CPUS=32 artifact); zone_page_state_snapshot() is the 6.18
 * native form of the same computation.
 */
static unsigned long zone_can_reclaimable_pages(struct zone *zone)
{
	unsigned long nr;

	nr = zone_page_state_snapshot(zone, NR_ZONE_INACTIVE_FILE) +
	     zone_page_state_snapshot(zone, NR_ZONE_ACTIVE_FILE);
	if (get_nr_swap_pages() > 0)
		nr += zone_page_state_snapshot(zone, NR_ZONE_INACTIVE_ANON) +
		      zone_page_state_snapshot(zone, NR_ZONE_ACTIVE_ANON);
	return nr;
}

/* Blob .text+0x50c8 (mi_mempool_alloc_reclaim). */
static void mi_mempool_alloc_reclaim(void *unused, gfp_t gfp_mask, int order,
				     int alloc_flags, int migratetype,
				     struct page **page)
{
	struct mi_dynamic_page_pool *pool = NULL;
	int process_id, process_index, count, minimum = 0;
	int free_pages = 0, snapshot_free = 0, cma_free = 0, highatomic = 0;
	int threshold1, threshold2, wmark_result;
	bool direct_mark_ok, low_mark_suited;
	char trace[256] = { 0 };
	struct zone *zone;
	int z;

	if (order >= 0 && order < ORDER_COUNT)
		pool = order_to_pool(order);
	if (pg_mi_mempool->config & CONFIG_DEBUG) {
		snprintf(trace, sizeof(trace), "I|%d|order: %d",
			 task_tgid_nr(current), order);
		trace_puts(trace);
	}
	process_id = check_whitelist();
	if (!process_id || !pool)
		return;
	/* blob: kthread and fatal-signal tasks leave with no else branch */
	if ((current->flags & PF_KTHREAD) || fatal_signal_pending(current))
		return;

	for (z = 0; z < 2; z++) {
		zone = &NODE_DATA(0)->node_zones[z];

		if (!zone_can_reclaimable_pages(zone))
			continue;
		minimum += (int)zone->_watermark[WMARK_MIN] +
			   (int)zone->watermark_boost;
		free_pages += (int)zone_page_state(zone, NR_FREE_PAGES);
		if (!(gfp_mask & __GFP_CMA))
			cma_free += (int)zone_page_state(zone, NR_FREE_CMA_PAGES);
		if (!(alloc_flags & MI_ALLOC_RESERVES))
			highatomic += (int)zone->nr_reserved_highatomic;
		if (pg_mi_mempool->config & CONFIG_DEBUG)
			snapshot_free += (int)zone_page_state_snapshot(zone,
							NR_FREE_PAGES);
	}
	threshold1 = (int)pg_mi_mempool->try_to_free_pages_threshold;
	/* blob adds 0xa00 pages (min_plus_10mb) before taking the max */
	threshold2 = max(minimum + 0xa00, threshold1);
	if ((int)pg_mi_mempool->direct_reclaim_threshold != -1)
		threshold2 = (int)pg_mi_mempool->direct_reclaim_threshold;
	wmark_result = free_pages - (cma_free + highatomic) <= threshold2;
	if (free_pages <= threshold1) {
		direct_mark_ok = true;
		wmark_result = 2;
	} else {
		direct_mark_ok = atomic_read(&pool->count) >
				 pool->direct_reclaim_mark;
	}
	if (pg_mi_mempool->config & CONFIG_DEBUG) {
		snprintf(trace, sizeof(trace),
			 "I|%d|mi_pool: check_wmark_result:%d o:%d free:%d sfree:%d "
			 "cma_free:%d high_free:%d threshold1:%d threshold2:%d",
			 task_tgid_nr(current), wmark_result, order, free_pages,
			 snapshot_free, cma_free, highatomic, threshold1,
			 threshold2);
		trace_puts(trace);
	}
	if (!wmark_result)
		return;
	if (wmark_result != 1)
		direct_mark_ok = true;

	process_index = process_id - 1;
	if (process_index < 0 || process_index >= PROCESS_COUNT)
		return;	/* blob traps here; whitelist ids are bounded */
	count = atomic_read(&pool->count);
	low_mark_suited = count > pool->oom_low_mark[process_index];
	if (direct_mark_ok && low_mark_suited)
		*page = mi_dynamic_page_pool_alloc(order, pool, gfp_mask, false);
	if (pg_mi_mempool->config & CONFIG_DEBUG) {
		snprintf(trace, sizeof(trace),
			 "I|%d|mi_pool: o:%d wmark_check_result:%d dr_mark_ok:%d "
			 "count:%d dr_mark:%d low_mark_suited:%d scucess:%d",
			 task_tgid_nr(current), order, wmark_result, direct_mark_ok,
			 atomic_read(&pool->count), pool->direct_reclaim_mark,
			 low_mark_suited, !!*page);
		trace_puts(trace);
	}
	if (pg_mi_mempool->config & CONFIG_USAGE) {
		snprintf(trace, sizeof(trace),
			 "I|%d|atomic_read(&pool->count): %d, pool->oom_low_mark[%d]: %d",
			 task_tgid_nr(current), atomic_read(&pool->count),
			 process_index, pool->oom_low_mark[process_index]);
		trace_puts(trace);
		atomic_inc(*page ?
			   &pool->stats[process_index].reclaim_success :
			   &pool->stats[process_index].reclaim_failure);
	}
}

/* Blob .text+0x5bac (mi_mempool_alloc_oom). */
static void mi_mempool_alloc_oom(void *unused, gfp_t gfp_mask, int order,
				 int alloc_flags, int migratetype,
				 struct page **page)
{
	struct mi_dynamic_page_pool *pool = NULL;
	int process_id;

	if (gfp_mask & __GFP_NOWARN)
		return;
	if (order >= 0 && order < ORDER_COUNT) {
		pool = order_to_pool(order);
		if (pool) {
			if (pg_mi_mempool->config & CONFIG_DEBUG)
				pr_info("mi_mempool alloc_oom comm=%s pid=%d "
					"order=%d gfp_mask:%#x(%pGg)\n",
					current->comm, current->pid, order,
					gfp_mask, &gfp_mask);
			*page = mi_dynamic_page_pool_alloc(order, pool, gfp_mask,
							   true);
			if (pg_mi_mempool->config & CONFIG_USAGE) {
				process_id = check_whitelist();

				if (process_id) {
					if (*page)
						atomic_inc(&pool->stats[process_id - 1].oom_success);
					else
						atomic_inc(&pool->stats[process_id - 1].oom_failure);
				}
			}
		}
	}
	if (!*page) {
		/* stock bound permits order 11, aliasing whitelist_check_num */
		if (order == ORDER_COUNT)
			atomic_inc(&pg_mi_mempool->whitelist_check_num);
		else if (order >= 0 && order < ORDER_COUNT)
			atomic_inc(&pg_mi_mempool->oom_use_failure_count[order]);
	}
}

/*
 * Blob .text+0x609c: pure refill-worker wakeup on page frees.  This
 * generation does no tree maintenance here (mi_mempool_free_task owns it),
 * and the stock error message still says "mmput hook" from the older
 * mmput-based generation; kept verbatim.
 */
static void mi_mempool_refill_check(void *unused, struct page *page, int order,
				    int migratetype, bool *bypass)
{
	unsigned long flags;

	if (!check_last_refill_time_interval() || !pg_mi_mempool->refill_pending ||
	    READ_ONCE(pg_mi_mempool->thread_woken))
		return;
	spin_lock_irqsave(&pg_mi_mempool->wakeup_lock, flags);
	if (!READ_ONCE(pg_mi_mempool->thread_woken)) {
		WRITE_ONCE(pg_mi_mempool->free_page_wakeup, true);
		WRITE_ONCE(pg_mi_mempool->thread_woken, true);
		wake_up_process(pg_mi_mempool->refill_worker);
	}
	spin_unlock_irqrestore(&pg_mi_mempool->wakeup_lock, flags);
}

/* Blob .text+0x1144: cache invalidation on group-leader task free. */
static void mi_mempool_free_task(void *unused, struct task_struct *task)
{
	struct proc_node *node;
	unsigned long flags;

	if (task->pid != task->tgid)
		return;
	spin_lock_irqsave(&proc_tree_lock, flags);
	node = del_proc_node_by_pid_locked(task->pid);
	spin_unlock_irqrestore(&proc_tree_lock, flags);
	if (node) {
		proc_node_free(node);
		if (pg_mi_mempool->config & CONFIG_DEBUG)
			pr_info("mi_mempool free_task comm=%s pid=%d\n",
				task->comm, task->pid);
	}
}

/* Main loop of the refill worker (blob .text+0x6fe0). */
static int mi_mempool_refill_work(void *unused)
{
	struct mi_dynamic_page_pool **pools = pg_mi_mempool->pools;
	char trace[300] = { 0 };
	unsigned long flags;
	int i, len, added;

	for (;;) {
		int config = pg_mi_mempool->config;

		atomic_long_set(&pg_mi_mempool->last_refill_time,
				ktime_to_ms(ktime_get()));
		pg_mi_mempool->refill_pending = false;
		pg_mi_mempool->last_refill_count = 0;
		pg_mi_mempool->refill_count++;
		len = 0;
		if (config & CONFIG_DEBUG)
			len = scnprintf(trace, sizeof(trace), "I|%d|mi_pool: refill ",
					task_tgid_nr(current));
		for (i = 0; i < POOL_COUNT; i++) {
			struct mi_dynamic_page_pool *pool = pools[i];

			added = 0;
			if (pool->first_filled &&
			    atomic_read(&pool->count) > pool->low_mark)
				goto trace_order;
			while (atomic_read(&pool->count) < pool->high_mark) {
				struct page *page;

				if (ktime_to_ns(ktime_get()) -
				    pool->last_check_watermark_time >=
				    (u64)MI_DYNAMIC_POOL_REFILL_MIN_CHECK_INTERVAL_MS *
				    NSEC_PER_MSEC) {
					if (!check_watermark(pool))
						break;
					pool->last_check_watermark_time = ktime_get();
				}
				page = alloc_pages(pool->gfp_mask, pool->order);
				if (!page)
					break;
				mi_dynamic_page_pool_add(pool, page);
				added++;
			}
			if (!pool->first_filled) {
				if (atomic_read(&pool->count) < pool->high_mark) {
					pg_mi_mempool->refill_pending = true;
					pool->first_fill_retries++;
					if (pool->first_fill_retries >=
					    MI_DYNAMIC_POOL_MAX_FIRST_REFILL_RETRIES)
						pool->first_filled = true;
				} else {
					pool->first_filled = true;
				}
			} else if (atomic_read(&pool->count) <= pool->low_mark) {
				pg_mi_mempool->refill_pending = true;
			}
			added <<= pool->order;
			pg_mi_mempool->last_refill_count += added;
trace_order:
			if ((config & CONFIG_DEBUG) && len < (int)sizeof(trace))
				len += scnprintf(trace + len, sizeof(trace) - len,
						 "order-%d:%d ", pool->order, added);
		}
		if (config & CONFIG_DEBUG) {
			len += scnprintf(trace + len, sizeof(trace) - len,
					 " cost_ms=%lld wakeup=%d",
					 ktime_to_ms(ktime_get()) -
					 atomic_long_read(&pg_mi_mempool->last_refill_time),
					 pg_mi_mempool->free_page_wakeup);
			if (len >= (int)sizeof(trace))
				len = sizeof(trace) - 1;
			trace[len] = '\0';
			trace_puts(trace);
		}
		WRITE_ONCE(pg_mi_mempool->free_page_wakeup, false);
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			set_current_state(TASK_RUNNING);
			break;
		}
		spin_lock_irqsave(&pg_mi_mempool->wakeup_lock, flags);
		WRITE_ONCE(pg_mi_mempool->thread_woken, false);
		spin_unlock_irqrestore(&pg_mi_mempool->wakeup_lock, flags);
		schedule();
		set_current_state(TASK_RUNNING);
	}
	return 0;
}

static struct mi_dynamic_page_pool *mi_dynamic_page_pool_create(gfp_t gfp_mask,
								int order,
								unsigned int mb,
								unsigned int dr_mb)
{
	struct mi_dynamic_page_pool *pool;
	int i;

	pool = kmalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;
	memset(pool, 0, sizeof(*pool));
	spin_lock_init(&pool->lock);
	pool->gfp_mask = gfp_mask;
	pool->order = order;
	INIT_LIST_HEAD(&pool->list);
	pool->high_mark = mb_to_pages(mb) >> order;
	pool->low_mark = pool->high_mark * MI_DYNAMIC_POOL_LOW_MARK_PERCENT / 100;
	pool->direct_reclaim_mark = mb_to_pages(dr_mb) >> order;
	atomic_set(&pool->count, 0);
	atomic_set(&pool->reclaim_use_count, 0);
	atomic_set(&pool->oom_use_count, 0);
	for (i = 0; i < PROCESS_COUNT; i++)
		pool->oom_low_mark[i] = pool->high_mark *
			MI_DYNAMIC_POOL_OOM_DEFAULT_LOW_PERCENT / 100;
	return pool;
}

static void mi_dynamic_page_pool_destory(struct mi_dynamic_page_pool *pool)
{
	struct page *page;

	while ((page = mi_dynamic_page_pool_remove(pool)))
		__free_pages(page, pool->order);
	kfree(pool);
}

static void init_mi_dynamic_page_pools_oom_low_mark_policy(struct mi_dynamic_page_pool **pools)
{
	struct mi_dynamic_page_pool *pool;
	int mark, i, j;

	for (i = 0; i < ARRAY_SIZE(oom_low_mark_policys); i++) {
		int pool_id = order_map[oom_low_mark_policys[i].order];
		int process_id = oom_low_mark_policys[i].process_id;
		int percent = oom_low_mark_policys[i].oom_low_mark_percent;

		pool = pools[pool_id];
		mark = pool->high_mark * percent / 100;
		if (process_id == -1) {
			for (j = 0; j < PROCESS_COUNT; j++)
				pool->oom_low_mark[j] = mark;
		} else {
			pool->oom_low_mark[process_id] = mark;
		}
	}
}

static int mi_dynamic_page_pools_create(void)
{
	int i;

	pg_mi_mempool->pools = kmalloc(sizeof(*pg_mi_mempool->pools) * POOL_COUNT,
				       GFP_KERNEL | __GFP_ZERO);
	if (!pg_mi_mempool->pools)
		/* stock leaves the published sysfs object behind on this failure */
		return -1;
	memset(order_map, 0xff, sizeof(order_map));
	for (i = 0; i < POOL_COUNT; i++) {
		pg_mi_mempool->pools[i] = mi_dynamic_page_pool_create(order_flags[i],
								      orders[i],
								      high_mark_mb[i],
								      direct_reclaim_mark_mb[i]);
		order_map[orders[i]] = i;
		if (!pg_mi_mempool->pools[i])
			goto pool_failure;
	}
	for (i = 0; i < NUM_LIMITED_ORDER; i++) {
		int pool_id = order_map[refill_limited_order[i]];

		if (pool_id != -1)
			pg_mi_mempool->pools[pool_id]->low_mark = -1;
	}
	init_mi_dynamic_page_pools_oom_low_mark_policy(pg_mi_mempool->pools);
	return 0;

pool_failure:
	for (i = 0; i < POOL_COUNT; i++) {
		struct mi_dynamic_page_pool *pool = pg_mi_mempool->pools[i];

		if (pool)
			mi_dynamic_page_pool_destory(pool);
	}
	kfree(pg_mi_mempool->pools);
	/* stock does not kobject_put here either */
	return -1;
}

static void mi_dynamic_page_pools_destory(void)
{
	int i;

	for (i = 0; i < POOL_COUNT; i++)
		mi_dynamic_page_pool_destory(pg_mi_mempool->pools[i]);
	/* stock does not free the pools array or clear the pointer */
}

static ssize_t process_use_count_show(struct kobject *kobj, struct kobj_attribute *attr,
				      char *buf)
{
	int len = 0, p, i;

	for (p = 0; p < PROCESS_COUNT; p++) {
		if (p < COMM_COUNT)
			len += snprintf(buf + len, PAGE_SIZE - len, "comm:%s\n",
					comm_whitelist[p]);
		else
			len += snprintf(buf + len, PAGE_SIZE - len, "comm:pid-%d\n",
					pid_whitelist[p - COMM_COUNT]);
		for (i = 0; i < POOL_COUNT; i++) {
			struct mi_dynamic_page_pool *pool = pg_mi_mempool->pools[i];
			struct mi_process_stat *stat = &pool->stats[p];

			len += snprintf(buf + len, PAGE_SIZE - len,
					"order-%d:%d %d %d %d %d\n",
					pool->order,
					atomic_read(&stat->reclaim_success),
					atomic_read(&stat->reclaim_failure),
					atomic_read(&stat->oom_success),
					atomic_read(&stat->oom_failure),
					pool->oom_low_mark[p]);
		}
	}
	return len;
}

static ssize_t general_stat_info_show(struct kobject *kobj, struct kobj_attribute *attr,
				      char *buf)
{
	int len = 0, i, failures = 0;

	len += snprintf(buf + len, PAGE_SIZE - len, "total_page_num: %d\n",
			atomic_read(&pg_mi_mempool->total_page_num));
	len += snprintf(buf + len, PAGE_SIZE - len, "last_refill: %lu %d %d\n",
			(unsigned long)atomic_long_read(&pg_mi_mempool->last_refill_time),
			pg_mi_mempool->last_refill_count, pg_mi_mempool->refill_count);
	for (i = 0; i < POOL_COUNT; i++) {
		struct mi_dynamic_page_pool *pool = pg_mi_mempool->pools[i];

		len += snprintf(buf + len, PAGE_SIZE - len, "order-%d: %d %d %d %d %d %d\n",
				pool->order, atomic_read(&pool->count),
				atomic_read(&pool->reclaim_use_count),
				atomic_read(&pool->oom_use_count), pool->low_mark,
				pool->direct_reclaim_mark, pool->high_mark);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "rb_tree: %d %d %d\n",
			atomic_read(&pg_mi_mempool->whitelist_check_num),
			pg_mi_mempool->proc_tree_size,
			atomic_read(&pg_mi_mempool->proc_node_alloc_failure));
	for (i = 0; i < ORDER_COUNT; i++) {
		int value = atomic_read(&pg_mi_mempool->oom_use_failure_count[i]);

		failures += value;
		len += snprintf(buf + len, PAGE_SIZE - len,
				i ? " %d:%d" : "oom_use_failure_count: %d:%d", i, value);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, " %d\n", failures);
	return len;
}

static ssize_t config_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pg_mi_mempool->config);
}

static ssize_t config_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	int value = 0;

	if (kstrtoint(buf, 0, &value) < 0)
		return -EINVAL;
	pg_mi_mempool->config = value;
	return count;
}

static ssize_t pids_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int len;

	len = snprintf(buf, PAGE_SIZE, "%d-%d ", pid_whitelist[0],
		       invalid_pid_whitelist[0]);
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}

static ssize_t pids_store(struct kobject *kobj, struct kobj_attribute *attr,
			  const char *buf, size_t count)
{
	int pid = 0;

	if (kstrtoint(buf, 0, &pid) < 0)
		return -EINVAL;
	if (pid < 1) {
		pr_info("pid OVF! pid:%d\n", pid);
		return count;
	}
	if (invalid_pid_whitelist[0]) {
		pid_whitelist[0] = pid;
		invalid_pid_whitelist[0] = false;
		pr_info("pid:%d\n", pid);
	}
	/* stock returns silently when the single slot is still occupied */
	return count;
}

static ssize_t direct_reclaim_threshold_mb_show(struct kobject *kobj,
						struct kobj_attribute *attr, char *buf)
{
	int minimum = 0, low = 0, high = 0, len, z;
	int direct = (int)pg_mi_mempool->direct_reclaim_threshold;

	/* blob samples node_zones[0..1], boosting each watermark once */
	for (z = 0; z < 2; z++) {
		struct zone *zone = &NODE_DATA(0)->node_zones[z];

		if (!zone_can_reclaimable_pages(zone))
			continue;
		minimum += (int)zone->_watermark[WMARK_MIN] +
			   (int)zone->watermark_boost;
		low += (int)zone->_watermark[WMARK_LOW] +
		       (int)zone->watermark_boost;
		high += (int)zone->_watermark[WMARK_HIGH] +
			(int)zone->watermark_boost;
	}
	len = snprintf(buf, PAGE_SIZE,
		"# echo format: echo 'id threshold_mb' > direct_reclaim_threshold_mb\n"
		"# id = 0, modify try_to_free_pages_threshold, id = 1, modify direct_reclaim_threshold\n"
		"# if try_to_free_pages_threshold == -1, then use max(try_to_free_pages_threshold, min_plus_10mb)\n");
	len += snprintf(buf + len, PAGE_SIZE - len, "try_to_free_pages_threshold:%dmb\n",
			(int)(pg_mi_mempool->try_to_free_pages_threshold >> MI_PAGES_TO_MB_SHIFT));
	if (direct >= 0)
		direct = (pg_mi_mempool->direct_reclaim_threshold >> MI_PAGES_TO_MB_SHIFT) & 0xffffff;
	len += snprintf(buf + len, PAGE_SIZE - len, "direct_reclaim_threshold:%dmb\n",
			direct);
	len += snprintf(buf + len, PAGE_SIZE - len,
			"min:%dmb, low:%dmb, high:%dmb, min_plus_10mb:%dmb\n",
			minimum >> MI_PAGES_TO_MB_SHIFT, low >> MI_PAGES_TO_MB_SHIFT,
			high >> MI_PAGES_TO_MB_SHIFT,
			(minimum + (10 << MI_PAGES_TO_MB_SHIFT)) >> MI_PAGES_TO_MB_SHIFT);
	return len;
}

static ssize_t direct_reclaim_threshold_mb_store(struct kobject *kobj,
						 struct kobj_attribute *attr,
						 const char *buf, size_t count)
{
	int id = 0, mb = 0;

	if (sscanf(buf, "%d %d", &id, &mb) != 2)
		return -EINVAL;
	if (id == 1) {
		if (mb < -1 || (mb >= 0 && mb <
		    (int)((pg_mi_mempool->try_to_free_pages_threshold >> MI_PAGES_TO_MB_SHIFT) & 0xfffffffffffUL)))
			return -EINVAL;
		pg_mi_mempool->direct_reclaim_threshold = mb == -1 ?
			~0UL : mb_to_pages(mb);
	} else if (id == 0) {
		if (mb < 0)
			return -EINVAL;
		pg_mi_mempool->try_to_free_pages_threshold = mb_to_pages(mb);
	}
	/* unknown ids succeed without changing either threshold, as in stock */
	return count;
}

static MI_MEMPOOL_ATTR_RO(process_use_count);
static MI_MEMPOOL_ATTR_RO(general_stat_info);
static MI_MEMPOOL_ATTR_RW(config);
static MI_MEMPOOL_ATTR_RW(direct_reclaim_threshold_mb);
static MI_MEMPOOL_ATTR_RW(pids);
static const struct attribute *attrs[] = {
	&kobj_attr_process_use_count.attr, &kobj_attr_general_stat_info.attr,
	&kobj_attr_config.attr, &kobj_attr_direct_reclaim_threshold_mb.attr,
	&kobj_attr_pids.attr, NULL,
};

static struct kobject *mi_mempool_sysfs_create(void)
{
	int err;
	struct kobject *kobj = kobject_create_and_add("mi_mempool", kernel_kobj);

	if (!kobj) {
		pr_err("failed to create mi_mempool node.\n");
		return NULL;
	}
	err = sysfs_create_files(kobj, attrs);
	if (err) {
		pr_err("failed to create mi_mempool attrs.\n");
		kobject_put(kobj);
		return NULL;
	}
	return kobj;
}

static void mi_mempool_sysfs_destory(void)
{
	if (pg_mi_mempool->kobj) {
		kobject_put(pg_mi_mempool->kobj);
		pg_mi_mempool->kobj = NULL;
	}
}

static inline void whitelist_init(void)
{
	int i;

	for (i = 0; i < COMM_COUNT; i++)
		comm_whitelist_sizes[i] = strlen(comm_whitelist[i]);
	memset(invalid_pid_whitelist, 1, sizeof(invalid_pid_whitelist));
}

static int vendor_hook_register(void)
{
	int ret;

	ret = register_trace_android_vh_alloc_pages_reclaim_bypass(
		mi_mempool_alloc_reclaim, NULL);
	if (ret) {
		pr_err("Failed to register alloc_pages_reclaim_bypass hook.\n");
		return ret;
	}
	ret = register_trace_android_vh_alloc_pages_failure_bypass(
		mi_mempool_alloc_oom, NULL);
	if (ret) {
		unregister_trace_android_vh_alloc_pages_reclaim_bypass(
			mi_mempool_alloc_reclaim, NULL);
		pr_err("Failed to register alloc_pages_failure_bypass hook.\n");
		return ret;
	}
	/* stock message kept verbatim (older generation registered mmput here) */
	ret = register_trace_android_vh_free_unref_page_bypass(
		mi_mempool_refill_check, NULL);
	if (ret) {
		unregister_trace_android_vh_alloc_pages_failure_bypass(
			mi_mempool_alloc_oom, NULL);
		unregister_trace_android_vh_alloc_pages_reclaim_bypass(
			mi_mempool_alloc_reclaim, NULL);
		pr_err("Failed to register mmput hook.\n");
		return ret;
	}
	ret = register_trace_android_vh_free_task(mi_mempool_free_task, NULL);
	if (ret) {
		unregister_trace_android_vh_free_unref_page_bypass(
			mi_mempool_refill_check, NULL);
		unregister_trace_android_vh_alloc_pages_failure_bypass(
			mi_mempool_alloc_oom, NULL);
		unregister_trace_android_vh_alloc_pages_reclaim_bypass(
			mi_mempool_alloc_reclaim, NULL);
		pr_err("Failed to register free_task hook.\n");
		return ret;
	}
	return 0;
}

static void vendor_hook_unregister(void)
{
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(
		mi_mempool_alloc_reclaim, NULL);
	unregister_trace_android_vh_alloc_pages_failure_bypass(
		mi_mempool_alloc_oom, NULL);
	unregister_trace_android_vh_free_unref_page_bypass(
		mi_mempool_refill_check, NULL);
	unregister_trace_android_vh_free_task(mi_mempool_free_task, NULL);
}

static int __init mi_mempool_init(void)
{
	struct task_struct *thread;
	int i;

	pg_mi_mempool = &g_mi_mempool;
	/* stock publishes sysfs before pools and the slab cache exist */
	pg_mi_mempool->kobj = mi_mempool_sysfs_create();
	if (!pg_mi_mempool->kobj)
		return -1;
	atomic_set(&pg_mi_mempool->total_page_num, 0);
	pg_mi_mempool->refill_count = 0;
	pg_mi_mempool->refill_pending = false;
	pg_mi_mempool->free_page_wakeup = false;
	pg_mi_mempool->thread_woken = true;
	pg_mi_mempool->config = 0;
	atomic_set(&pg_mi_mempool->whitelist_check_num, 0);
	atomic_set(&pg_mi_mempool->proc_node_alloc_failure, 0);
	pg_mi_mempool->proc_tree_size = 0;
	for (i = 0; i < ORDER_COUNT; i++)
		atomic_set(&pg_mi_mempool->oom_use_failure_count[i], 0);
	whitelist_init();
	pg_mi_mempool->try_to_free_pages_threshold = BYTES_PER_MB *
		MI_DYNAMIC_POOL_DEFAULT_DIRECT_RELCAIM_THRESHOLD_MB / PAGE_SIZE;
	pg_mi_mempool->direct_reclaim_threshold = ~0UL;
	if (mi_dynamic_page_pools_create())
		return -1;

	if (!proc_node_init()) {
		mi_mempool_sysfs_destory();
		mi_dynamic_page_pools_destory();
		pr_err("failed to init proc_node mem_cache.\n");
		return -1;
	}

	thread = kthread_create_on_node(mi_mempool_refill_work, NULL,
					NUMA_NO_NODE, "mi_mempool");
	if (IS_ERR(thread)) {
		free_all_proc_nodes_and_caches();
		mi_mempool_sysfs_destory();
		mi_dynamic_page_pools_destory();
		pr_err("failed to start mi_mempool thread\n");
		return -1;
	}
	/* stock wakes the thread before publishing the recursion-guard pointer */
	wake_up_process(thread);
	pg_mi_mempool->refill_worker = thread;

	if (vendor_hook_register()) {
		free_all_proc_nodes_and_caches();
		mi_mempool_sysfs_destory();
		mi_dynamic_page_pools_destory();
		if (pg_mi_mempool->refill_worker)
			kthread_stop(pg_mi_mempool->refill_worker);
		return -1;
	}

	pr_info("mi_mempool init ok\n");
	return 0;
}

static void __exit mi_mempool_exit(void)
{
	vendor_hook_unregister();
	free_all_proc_nodes_and_caches();
	mi_mempool_sysfs_destory();
	mi_dynamic_page_pools_destory();

	if (pg_mi_mempool->refill_worker)
		kthread_stop(pg_mi_mempool->refill_worker);
}

vseq_module_init(mi_mempool_init);
module_exit(mi_mempool_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("zhouwenhao");
MODULE_DESCRIPTION("A moudle to preserve some pages for some core apps"
	" or memory allocation failures.");
