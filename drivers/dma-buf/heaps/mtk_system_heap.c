// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 MediaTek Inc.
 *
 * DMABUF System heap exporter
 *
 */

#define pr_fmt(fmt) "dma_heap: system "fmt

#include <linux/dma-buf.h>
#include <linux/vseq.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h> /* rodin r25: 6.18 header pruning */
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
/* rodin b262 (A-113): reserve pool face (rebuilt against stock
 * system_heap.ko, whose GPL drop predates the feature) */
#include <linux/kthread.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/ktime.h>
#include <linux/mmzone.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/sched.h>

#include "mtk_page_pool.h"
#include "mtk-deferred-free-helper.h"
#include <uapi/linux/dma-buf.h>

#include <linux/iommu.h>
#include "mtk_heap_priv.h"
#include "mtk_heap.h"
#include "mtk_page_pool.h"
#include "mtk-smmu-v3.h"

#define MAP_FAILED_DUMP_RS_INTERVAL	(2 * HZ)
#define MAP_FAILED_DUMP_RS_BURST	(1)
#define RUN_TIMEOUT_TO_LOG		(100000000ULL)

atomic64_t dma_heap_normal_total = ATOMIC64_INIT(0);
EXPORT_SYMBOL(dma_heap_normal_total);

static dmaheap_slc_callback slc_callback;

dmabuf_rbtree_dump_cb dmabuf_rbtree_dump_by_domain;
EXPORT_SYMBOL_GPL(dmabuf_rbtree_dump_by_domain);

struct system_heap_buffer {
	struct dma_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;
	bool uncached;
	/* helper function */
	int (*show)(const struct dma_buf *dmabuf, struct seq_file *s);

	/* system heap will not strore sgtable here */
	struct list_head	 iova_caches;
	struct mutex             map_lock; /* map iova lock */
	pid_t                    pid;
	pid_t                    tid;
	char                     pid_name[TASK_COMM_LEN];
	char                     tid_name[TASK_COMM_LEN];
	unsigned long long       ts; /* us */

	int gid; /* slc */

	/* private part for system heap */
	struct mtk_deferred_freelist_item deferred_free;
};

/*
 * rodin b262 (A-113): reserve_pool face, rebuilt against the stock
 * system_heap.ko machine code (Desktop system_heap.ko.asm:
 * need_free_to_reserve_pool 0xE0E4 / mtk_sys_heap_reserve_pool_init 0xE1E4 /
 * reserve_pool_refill_kthread 0xE570 / mtk_extend_sys_heap_alloc_largest_available
 * 0xEAEC / clear_pid_from_pool_pid 0xF650 / sysfs block 0xF6E8..0x102C8).
 * The vendor GPL drop of this file (commit bc2dfc0) predates the feature, so
 * the stock ko is the only truth.  6.6 -> 6.18 drift rewritten with 6.18
 * idioms (never "make it run"):
 *   - tracepoint_probe_register      -> register_trace_android_vh_mmput
 *   - raw task_struct offsets        -> named fields: group_leader / pid /
 *     comm / flags(PF_EXITING) / signal->oom_score_adj
 *     (blob 6.6 offsets 0x658/0x618/0x830/0x44 bit2/0x878+0x3CA via pahole)
 *   - raw pglist_data offsets        -> NODE_DATA(0)->node_zones[],
 *     wmark_pages(), lowmem_reserve[], nr_reserved_highatomic,
 *     zone_page_state_snapshot() (blob vm_stat[0] + per-cpu vm_stat_diff[0]
 *     sum with percpu_drift_mark shortcut) and NR_FREE_CMA_PAGES
 *     (blob vm_stat[10]); free_area scan kept (order>0 pools)
 *   - gfp flag arithmetic            -> (__GFP_NORETRY mask | __GFP_RECLAIM)
 *   - sched_setattr import           -> still EXPORT_SYMBOL_GPL in 6.18
 * Blob bits without any consumer are not carried: can_do_shrink (dead
 * export) and the cached `totalram` global (replaced by totalram_pages()).
 */

#define RESERVE_INVALID_ADJ		(-1000)

/* numeric truth lives in mtk_page_pool.h next to the shared externs */

static int g_reserve_config[2][5] = {
	/* { lowmark[order8], lowmark[order4], lowmark[order0], timeout_sec, slow_alloc_us } */
	{ 15, 3200, 150, 100, 1000 },	/* totalram <= 8G */
	{ 15, 3200, 300, 100, 1000 },	/* totalram  > 8G */
};

static int g_camera_reserve_config[2][5] = {
	{ 15, 3200, 200, 100, 500 },
	{ 15, 3200, 300, 100, 500 },
};

struct mtk_reserve_extend_info {
	u8 enabled;
	u32 debug_mask;
	u32 timeout_sec;
	u32 slow_alloc_us;
	int scene;			/* 1 = camera, 2 = default */
	u32 stat_plain_alloc;		/* order-0 fallback allocs, not whitelisted */
	u32 stat_hit_alloc;		/* order-0 fallback allocs, whitelisted */
	u32 stat_slow_alloc;
	u32 used_order[NUM_ORDERS];	/* chunks served out of the reserve pools */
	int lowmark[NUM_ORDERS];
	u32 refill_count[NUM_ORDERS];
	u32 refill_ok[NUM_ORDERS];
	u32 refill_last[NUM_ORDERS];
	u64 last_use_ms;		/* last order-0 reserve consumption (ms) */
};

static struct mtk_reserve_extend_info reserve_extend_info;
static struct mtk_dmabuf_page_pool *system_reserve_pools[NUM_ORDERS];
bool reserve_pool_enable;	/* blob ksymtab face: read by the page_pool shrinker floors */
static u8 g_mem_type_index;
static int pool_pid[3] = { -1, -1, -1 };
static int pool_pid_usable_index;
static bool use_name_cmp;
static struct mtk_dmabuf_page_pool **config_show_pools;
static struct task_struct *reserve_refill_task;

static int reserve_slot_of_order(unsigned int order)
{
	/* blob form: orders[0]/orders[1] compare, otherwise the last slot */
	if (orders[0] == order)
		return 0;
	if (orders[1] == order)
		return 1;
	return 2;
}

static int reserve_pool_total_chunks(struct mtk_dmabuf_page_pool *pool)
{
	int total;

	if (!pool)
		return 0;
	spin_lock(&pool->lock);
	total = pool->count[POOL_LOWPAGE] + pool->count[POOL_HIGHPAGE];
	spin_unlock(&pool->lock);
	return total;
}

/*
 * blob 0xE0E4: called from system_heap_buf_free with the pool slot index;
 * true when the freed chunk should be diverted into system_reserve_pools[]
 * instead of the dynamic pool it came from.
 */
static bool need_free_to_reserve_pool(int index)
{
	struct mtk_dmabuf_page_pool *pool;
	int total;

	if (!reserve_extend_info.enabled || index >= NUM_ORDERS)
		return false;

	pool = system_reserve_pools[index];
	if (!pool)
		return false;

	total = reserve_pool_total_chunks(pool);
	if (total >= reserve_extend_info.lowmark[reserve_slot_of_order(pool->order)])
		return false;

	if (reserve_extend_info.debug_mask & BIT(5))
		printk(KERN_INFO "%s order[%d %d]\n",
		       "need_free_to_reserve_pool", index, pool->order);
	return true;
}

/*
 * blob 0xE570, per zone gate of the refill pass: every zone from
 * gfp_zone(gfp) down to ZONE 0 (DMA32 skipped) must be above its watermark
 * accounting, and for order>0 must hold an allocatable free_area chunk.
 */
static bool reserve_pool_watermark_ok(struct mtk_dmabuf_page_pool *pool, gfp_t gfp)
{
	int zone_idx = gfp_zone(gfp);
	unsigned int order = pool->order;
	int z, i, t;

	for (z = zone_idx; z >= 0; z--) {
		struct zone *zone = &NODE_DATA(0)->node_zones[z];
		unsigned long avail, need;

		if (!strcmp(zone->name, "DMA32"))
			continue;

		/* blob: free - nr_reserved_highatomic - (1 << order) - CMA
		 *       > lowmem_reserve[gfp_zone] + (1 << order) + high + boost */
		avail = zone_page_state_snapshot(zone, NR_FREE_PAGES) -
			zone->nr_reserved_highatomic -
			zone_page_state(zone, NR_FREE_CMA_PAGES);
		need = zone->lowmem_reserve[zone_idx] + wmark_pages(zone, WMARK_HIGH);
		if (avail + (1UL << order) <= need + (1UL << order))
			return false;

		/* order>0 only: a ready free_area chunk must exist (blob scans
		 * free_area[order..9]) */
		if (order > 0) {
			bool has_chunk = false;

			for (i = order; i < RESERVE_FREE_AREA_SCAN_MAX; i++) {
				if (!zone->free_area[i].nr_free)
					continue;
				for (t = 0; t < MIGRATE_TYPES; t++) {
					if (!list_empty(&zone->free_area[i].free_list[t])) {
						has_chunk = true;
						break;
					}
				}
				if (has_chunk)
					break;
			}
			if (!has_chunk)
				return false;
		}
	}
	return true;
}

/* blob 0xF3F8 / 0xF4AC: nudge both refill kthreads after a served alloc */
static void reserve_pool_wake_refill(struct mtk_dmabuf_page_pool *dyn,
				     struct mtk_dmabuf_page_pool *rsv, bool hit)
{
	int total, slot;

	if (dyn && dyn->refill_kthread && dyn->order != 0 &&
	    reserve_extend_info.scene != 1) {
		total = reserve_pool_total_chunks(dyn);
		slot = reserve_slot_of_order(dyn->order);
		if (total < reserve_extend_info.lowmark[slot]) {
			if (reserve_extend_info.debug_mask & BIT(1))
				printk(KERN_INFO
				       "system_pool_lowmark: current_proc-order<%d, %d> wakeupsystem_pool-2\n",
				       dyn->order, total);
			wake_up_process(dyn->refill_kthread);
		}
	}

	if (hit && rsv && rsv->refill_kthread && rsv->order != 0 &&
	    reserve_extend_info.scene != 1) {
		total = reserve_pool_total_chunks(rsv);
		slot = reserve_slot_of_order(rsv->order);
		if (total < reserve_extend_info.lowmark[slot]) {
			if (reserve_extend_info.debug_mask & BIT(1))
				printk(KERN_INFO
				       "reserve_pool_lowmark: current_proc-order<%d, %d> wakeupreserve_pool-1\n",
				       rsv->order, total);
			wake_up_process(rsv->refill_kthread);
		}
	}
}

static void reserve_pool_counts(struct mtk_dmabuf_page_pool *pool, int *lo, int *hi)
{
	*lo = *hi = 0;
	if (!pool)
		return;
	spin_lock(&pool->lock);
	*lo = pool->count[POOL_LOWPAGE];
	*hi = pool->count[POOL_HIGHPAGE];
	spin_unlock(&pool->lock);
}

/* blob 0xEAEC */
static struct page *mtk_extend_sys_heap_alloc_largest_available(
	struct mtk_dmabuf_page_pool **page_pools, unsigned long size,
	unsigned int max_order)
{
	struct task_struct *leader = current->group_leader;
	bool hit = false;
	struct page *page;
	int i;

	/* only "unkillable" (oom_score_adj -1000) tasks may consume the
	 * reserve pools; the pid/comm whitelist is fed from userspace via
	 * /sys/kernel/reserve_pool/pid */
	if (leader->signal->oom_score_adj == RESERVE_INVALID_ADJ) {
		if (use_name_cmp) {
			if (pool_pid[0] == leader->pid || pool_pid[1] == leader->pid ||
			    pool_pid[2] == leader->pid)
				hit = true;
		} else if ((!strncmp(leader->comm, "camerahalserver", 15) &&
			    pool_pid[0] == leader->pid) ||
			   (!strncmp(leader->comm, "android.hardwar", 15) &&
			    pool_pid[1] == leader->pid) ||
			   (!strncmp(leader->comm, "composer@3.2-se", 15) &&
			    pool_pid[2] == leader->pid)) {
			hit = true;
		}
	}

	for (i = 0; i < NUM_ORDERS; i++) {
		struct mtk_dmabuf_page_pool *pool, *rsv_pool;
		unsigned int order = orders[i];
		bool rsv_empty;
		int slo, shi, rlo, rhi;
		u64 t1;

		if (order > max_order)
			continue;
		if ((PAGE_SIZE << order) > size)
			continue;
		if (!page_pools)
			return alloc_pages(order_flags[i], orders[i]);

		pool = page_pools[i];
		rsv_pool = system_reserve_pools[i];
		page = NULL;

		/* 1st: the dynamic pool (blob 0xED44; HIGHPAGE list first) */
		spin_lock(&pool->lock);
		if (pool->count[POOL_HIGHPAGE])
			page = mtk_dmabuf_page_pool_remove_locked(pool, POOL_HIGHPAGE);
		else if (pool->count[POOL_LOWPAGE])
			page = mtk_dmabuf_page_pool_remove_locked(pool, POOL_LOWPAGE);
		spin_unlock(&pool->lock);

		/* 2nd: reserve pool fallback (blob 0xED84..0xF01C).  High
		 * orders first try a plain alloc (blob 0xEDA0, debug bit0
		 * print), then all orders share the reserve chain. */
		if (!page && order != 0) {
			page = alloc_pages(pool->gfp_mask, pool->order);
			if (reserve_extend_info.debug_mask & BIT(0)) {
				reserve_pool_counts(pool, &slo, &shi);
				reserve_pool_counts(rsv_pool, &rlo, &rhi);
				printk(KERN_INFO
				       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
				       "high order alloc", page, leader->comm,
				       current->comm, current->pid, order,
				       slo, shi, rlo, rhi, hit);
			}
		}

		rsv_empty = !system_reserve_pools[0] || !system_reserve_pools[1] ||
			    !system_reserve_pools[2];
		if (!page && rsv_empty) {
			printk(KERN_ERR "%s: global reserve pool have NULL, order=%d\n",
			       "reserve_pool_empty", i);
		} else if (!page && hit) {
			u64 elapsed_ms = 0;
			bool cold = false, timeout = false;

			if (rsv_pool && rsv_pool->order == 0) {
				if (!reserve_extend_info.last_use_ms)
					cold = true;
				else
					elapsed_ms = div_u64(ktime_get(), NSEC_PER_MSEC) -
						     reserve_extend_info.last_use_ms;
				timeout = elapsed_ms >= (u64)reserve_extend_info.timeout_sec;
			}
			if (cold) {
				/* idle beyond the service window: leave it full */
			} else if (timeout) {
				reserve_extend_info.last_use_ms = 0;
				if (reserve_extend_info.debug_mask & BIT(2)) {
					reserve_pool_counts(pool, &slo, &shi);
					reserve_pool_counts(rsv_pool, &rlo, &rhi);
					printk(KERN_INFO
					       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
					       "used reserve pool timeout", NULL,
					       leader->comm, current->comm, current->pid,
					       order, slo, shi, rlo, rhi, hit);
				}
			} else if (rsv_pool) {
				spin_lock(&rsv_pool->lock);
				if (rsv_pool->count[POOL_HIGHPAGE])
					page = mtk_dmabuf_page_pool_remove_locked(rsv_pool, POOL_HIGHPAGE);
				else if (rsv_pool->count[POOL_LOWPAGE])
					page = mtk_dmabuf_page_pool_remove_locked(rsv_pool, POOL_LOWPAGE);
				spin_unlock(&rsv_pool->lock);
				if (page) {
					reserve_extend_info.used_order[i]++;
					if (reserve_extend_info.debug_mask & BIT(1)) {
						reserve_pool_counts(pool, &slo, &shi);
						reserve_pool_counts(rsv_pool, &rlo, &rhi);
						printk(KERN_INFO
						       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
						       "reserve pool", page,
						       leader->comm, current->comm, current->pid,
						       order, slo, shi, rlo, rhi, hit);
					}
				}
			}
		}

		/* 3rd: plain alloc with slow-path instrumentation (blob 0xF034) */
		if (!page) {
			t1 = ktime_get();
			page = alloc_pages(pool->gfp_mask, pool->order);
			if (hit && div_u64(ktime_get() - t1, NSEC_PER_USEC) >
				   (u64)reserve_extend_info.slow_alloc_us) {
				reserve_extend_info.last_use_ms =
					div_u64(ktime_get(), NSEC_PER_MSEC);
				reserve_extend_info.stat_slow_alloc++;
				if (reserve_extend_info.debug_mask & BIT(2)) {
					reserve_pool_counts(pool, &slo, &shi);
					reserve_pool_counts(rsv_pool, &rlo, &rhi);
					printk(KERN_INFO
					       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
					       "reserve-sp", page,
					       leader->comm, current->comm, current->pid,
					       order, slo, shi, rlo, rhi, hit);
				}
			}
			if (reserve_extend_info.debug_mask & BIT(4)) {
				reserve_pool_counts(pool, &slo, &shi);
				reserve_pool_counts(rsv_pool, &rlo, &rhi);
				printk(KERN_INFO
				       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
				       "All alloc", page,
				       leader->comm, current->comm, current->pid,
				       order, slo, shi, rlo, rhi, hit);
			}
			if (order == 0) {
				if (hit)
					reserve_extend_info.stat_hit_alloc++;
				else
					reserve_extend_info.stat_plain_alloc++;
			}
		}

		if (!page)
			continue;

		if (reserve_extend_info.debug_mask & BIT(3)) {
			reserve_pool_counts(pool, &slo, &shi);
			reserve_pool_counts(rsv_pool, &rlo, &rhi);
			printk(KERN_INFO
			       "%s: page %p %s[%s %d] order %d spool[%d %d] reserve_pool[%d %d] served_app %d\n",
			       "All", page,
			       leader->comm, current->comm, current->pid, order,
			       slo, shi, rlo, rhi, hit);
		}

		reserve_pool_wake_refill(pool, rsv_pool, hit);
		return page;
	}
	return NULL;
}

/* blob 0xE570; wakes only when a reserve pool drops below its lowmark */
static int reserve_pool_refill_kthread(void *data)
{
	struct mtk_dmabuf_page_pool **pools = data;
	int i = 0;

	while (i < NUM_ORDERS) {
		struct mtk_dmabuf_page_pool *pool = pools[i];
		int lowmark, total;

		/* init 建池失败链会把失败槽连同此前已建池一并置 NULL 后
		 * continue，kthread 随后照起（blob 同名线程同样不判空、blob
		 * init 失败分支同形）——此处为防御性加固，空槽跳过本轮 */
		if (!pool)
			goto next_pool;

		lowmark = reserve_extend_info.lowmark[reserve_slot_of_order(pool->order)];
		total = reserve_pool_total_chunks(pool);

		if (total < lowmark && pool->order == 0 && reserve_extend_info.enabled) {
			gfp_t gfp = (pool->gfp_mask & ~__GFP_NORETRY) | __GFP_RECLAIM;
			bool session_start = true;

			do {
				struct page *page;
				int slot = reserve_slot_of_order(pool->order);

				total = reserve_pool_total_chunks(pool);
				if (total >= lowmark)
					break;
				if (ktime_get() - pool->refill_time <
				    RESERVE_REFILL_INTERVAL_NS)
					break;
				if (session_start) {
					reserve_extend_info.refill_count[slot]++;
					reserve_extend_info.refill_last[slot] = 0;
					session_start = false;
				}
				if (!reserve_pool_watermark_ok(pool, pool->gfp_mask)) {
					pool->refill_time = ktime_get();
					break;
				}
				page = alloc_pages(gfp, pool->order);
				if (!page)
					break;
				reserve_extend_info.refill_last[slot]++;
				reserve_extend_info.refill_ok[slot]++;
				mtk_dmabuf_page_pool_add(pool, page);
			} while (reserve_extend_info.enabled);
		}

		if (reserve_extend_info.debug_mask & BIT(1)) {
			int lo, hi;

			spin_lock(&pool->lock);
			lo = pool->count[POOL_LOWPAGE];
			hi = pool->count[POOL_HIGHPAGE];
			spin_unlock(&pool->lock);
			printk(KERN_INFO
			       "%s : pools<%d>  = POOL_LOWPAGE : %d + POOL_HIGHPAGE : %d\n",
			       "reserve_pool_lowmark", pool->order, lo, hi);
		}

next_pool:
		i++;
		if (i >= NUM_ORDERS) {
			set_current_state(TASK_INTERRUPTIBLE);
			smp_mb();
			if (kthread_should_stop()) {
				__set_current_state(TASK_RUNNING);
				break;
			}
			schedule();
			__set_current_state(TASK_RUNNING);
			i = 0;
		}
	}
	return 0;
}

/* blob 0xF650: android_vh_mmput -- release the whitelist slot of a dying
 * process; 6.6 blob keyed on group_leader->flags & PF_EXITING */
static void clear_pid_from_pool_pid(void *unused, struct mm_struct *mm)
{
	struct task_struct *leader = current->group_leader;
	int i;

	if (!reserve_extend_info.enabled)
		return;
	if (!(leader->flags & PF_EXITING))
		return;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (pool_pid[i] == leader->pid) {
			printk(KERN_INFO "pid %d from list\n", pool_pid[i]);
			pool_pid[i] = -1;
			break;
		}
	}
}

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", reserve_extend_info.enabled);
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t len)
{
	struct mtk_dmabuf_page_pool *pool;
	struct page *page, *tmp;
	int val, ret, i, n;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	if (val) {
		reserve_extend_info.enabled = 1;
		reserve_pool_enable = 1;
		return len;
	}

	reserve_extend_info.enabled = 0;
	reserve_pool_enable = 0;

	for (i = 0; i < NUM_ORDERS; i++) {
		LIST_HEAD(free_list);

		pool = system_reserve_pools[i];
		if (!pool)
			continue;

		spin_lock(&pool->lock);
		for (;;) {
			page = mtk_dmabuf_page_pool_remove_locked(pool, POOL_LOWPAGE);
			if (!page)
				page = mtk_dmabuf_page_pool_remove_locked(pool, POOL_HIGHPAGE);
			if (!page)
				break;
			list_add_tail(&page->lru, &free_list);
		}
		spin_unlock(&pool->lock);

		n = 1;
		list_for_each_entry_safe(page, tmp, &free_list, lru) {
			list_del(&page->lru);
			if (reserve_extend_info.debug_mask & BIT(6))
				printk(KERN_INFO "reserve pool free %d order %d\n",
				       n, pool->order);
			__free_pages(page, pool->order);
			n++;
		}
	}
	return len;
}

static ssize_t debug_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", reserve_extend_info.debug_mask);
}

static ssize_t debug_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t len)
{
	int val, ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;
	reserve_extend_info.debug_mask = val;
	return len;
}

static const char * const reserve_config_show_name[] = {
	"source_pool_free_order",
	"reserve_pool_used_order",
	"reserve_pool_free_order",
	"reserve_pool_reserve_order",
	"reserve_pool_refill_count",
	"reserve_pool_refill_acount",
	"reserve_pool_refill_last_amount",
};

static ssize_t config_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	int n = 0;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(reserve_config_show_name); i++) {
		int v[3] = { 0, 0, 0 };

		for (j = 0; j < NUM_ORDERS; j++) {
			switch (i) {
			case 0:
				v[j] = reserve_pool_total_chunks(config_show_pools ?
								 config_show_pools[j] : NULL);
				break;
			case 1:
				v[j] = reserve_extend_info.used_order[j];
				break;
			case 2:
				v[j] = reserve_pool_total_chunks(system_reserve_pools[j]);
				break;
			case 3:
				v[j] = reserve_extend_info.lowmark[j];
				break;
			case 4:
				v[j] = reserve_extend_info.refill_count[j];
				break;
			case 5:
				v[j] = reserve_extend_info.refill_ok[j];
				break;
			case 6:
				v[j] = reserve_extend_info.refill_last[j];
				break;
			}
		}
		n += scnprintf(buf + n, PAGE_SIZE - n, "%s[%d %d %d]\n",
			       reserve_config_show_name[i], v[0], v[1], v[2]);
	}

	n += scnprintf(buf + n, PAGE_SIZE - n,
		       "reserve_slowpath[%d]\nreserve_long_slowpath[%d]\nslowpath[%d]\n"
		       "pool_service_time[%d]\npool_start_service_alloc_time[%d]\n"
		       "used_reserve_pool %d\n",
		       reserve_extend_info.stat_hit_alloc,
		       reserve_extend_info.stat_slow_alloc,
		       reserve_extend_info.stat_plain_alloc,
		       reserve_extend_info.timeout_sec,
		       reserve_extend_info.slow_alloc_us,
		       reserve_extend_info.enabled);
	return n;
}

static ssize_t config_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t len)
{
	int lm0, lm1, lm2, to, slow;

	if (sscanf(buf, "%d %d %d %d %d", &lm0, &lm1, &lm2, &to, &slow) != 5) {
		printk(KERN_INFO
		       "usage: total five args, three reserve, one used time and one alloc time\n");
		return -EIO;
	}

	reserve_extend_info.lowmark[0] = lm0;
	reserve_extend_info.lowmark[1] = lm1;
	reserve_extend_info.lowmark[2] = lm2;
	reserve_extend_info.timeout_sec = to;
	reserve_extend_info.slow_alloc_us = slow;
	return len;
}

static ssize_t pid_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	/* blob arg order kept: slot1 slot2 slot0 and the inverted cmp flag */
	return scnprintf(buf, PAGE_SIZE, "%d %d %d cmp:%d\n",
			 pool_pid[1], pool_pid[2], pool_pid[0],
			 1 & !use_name_cmp);
}

static bool update_pool_pid(int new_pid)
{
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		bool alive;

		rcu_read_lock();
		alive = pool_pid[i] >= 0 && find_task_by_vpid(pool_pid[i]);
		rcu_read_unlock();
		if (alive)
			continue;
		printk(KERN_INFO "pid update %d -> %d\n", pool_pid[i], new_pid);
		pool_pid[i] = new_pid;
		return true;
	}
	printk(KERN_INFO "no find index for %d\n", new_pid);
	return false;
}

static ssize_t pid_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t len)
{
	struct task_struct *tsk, *leader;
	int pid, ret;

	ret = kstrtoint(buf, 0, &pid);
	if (ret)
		return ret;
	if (pid < 1)
		return len;

	rcu_read_lock();
	tsk = find_task_by_vpid(pid);
	rcu_read_unlock();
	if (!tsk)
		return -ESRCH;

	get_task_struct(tsk);
	leader = tsk->group_leader;
	printk(KERN_INFO "caller pid %d %s ppid %d\n",
	       pid, current->comm, task_pid_nr(current));

	if (pool_pid_usable_index <= 2) {
		pool_pid[pool_pid_usable_index] = pid;
		pool_pid_usable_index++;
		printk(KERN_INFO "pid %d %s %d %s %d\n",
		       pid, tsk->comm, leader->pid, leader->comm,
		       pool_pid_usable_index);
	} else {
		printk(KERN_INFO "pid OVF! %d %s %d\n",
		       pid, leader->comm, 3);
		if (!update_pool_pid(pid))
			printk(KERN_INFO "warning pid %d %s %d %s %d\n",
			       pid, tsk->comm, leader->pid, leader->comm,
			       pool_pid_usable_index);
	}
	put_task_struct(tsk);

	use_name_cmp = pool_pid[0] && pool_pid[1] && pool_pid[2];
	return len;
}

static ssize_t scene_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", reserve_extend_info.scene);
}

static ssize_t scene_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t len)
{
	const int *cfg;
	int scene, ret;

	ret = kstrtoint(buf, 0, &scene);
	if (ret)
		return ret;

	if (scene == 1)
		cfg = g_camera_reserve_config[g_mem_type_index];
	else if (scene == 2)
		cfg = g_reserve_config[g_mem_type_index];
	else {
		printk(KERN_INFO "error\n");
		return len;
	}

	reserve_extend_info.scene = scene;
	reserve_extend_info.lowmark[0] = cfg[0];
	reserve_extend_info.lowmark[1] = cfg[1];
	reserve_extend_info.lowmark[2] = cfg[2];
	reserve_extend_info.timeout_sec = cfg[3];
	reserve_extend_info.slow_alloc_us = cfg[4];
	return len;
}

static struct kobj_attribute kobj_attr_enable =
	__ATTR(enable, 0644, enable_show, enable_store);
static struct kobj_attribute kobj_attr_debug =
	__ATTR(debug, 0644, debug_show, debug_store);
static struct kobj_attribute kobj_attr_config =
	__ATTR(config, 0644, config_show, config_store);
static struct kobj_attribute kobj_attr_pid =
	__ATTR(pid, 0644, pid_show, pid_store);
static struct kobj_attribute kobj_attr_scene =
	__ATTR(scene, 0644, scene_show, scene_store);

static struct attribute *reserve_pool_attrs[] = {
	&kobj_attr_enable.attr,
	&kobj_attr_debug.attr,
	&kobj_attr_config.attr,
	&kobj_attr_pid.attr,
	&kobj_attr_scene.attr,
	NULL,
};

static const struct attribute_group reserve_pool_attr_group = {
	.attrs = reserve_pool_attrs,
};

/*
 * blob 0xE1E4: three standalone pools at orders[]/order_flags[] plus the
 * /sys/kernel/reserve_pool face and the reserve-refill kthread.  Called from
 * mtk_heap_create("system"), i.e. inside the vseq replay wave, which keeps
 * the stock 0.53s creation phase.
 */
static void mtk_sys_heap_reserve_pool_init(struct mtk_dmabuf_page_pool **page_pools)
{
	struct sched_attr attr = {
		.sched_policy = SCHED_NORMAL,
		.sched_nice = 10,
	};
	const int *cfg;
	struct kobject *reserve_kobj;
	int i, j, ret;

	g_mem_type_index = totalram_pages() > RESERVE_BIGRAM_PAGES;

	for (i = 0; i < NUM_ORDERS; i++) {
		system_reserve_pools[i] = mtk_dmabuf_page_pool_create(order_flags[i],
								     orders[i]);
		if (!system_reserve_pools[i]) {
			printk(KERN_ERR
			       "%s: page pool creation failed for the order %u pool!\n",
			       __func__, orders[i]);
			for (j = 0; j < i; j++) {
				mtk_dmabuf_page_pool_destroy(system_reserve_pools[j]);
				system_reserve_pools[j] = NULL;
			}
			continue;
		}
		printk(KERN_INFO "%s: order<%d> reserve page pool create success!\n",
		       __func__, orders[i]);
		system_reserve_pools[i]->is_reserve_pool = true;
		system_reserve_pools[i]->pool_list = system_reserve_pools;
	}

	cfg = g_reserve_config[g_mem_type_index];
	reserve_extend_info.debug_mask = 0;
	reserve_extend_info.lowmark[0] = cfg[0];
	reserve_extend_info.lowmark[1] = cfg[1];
	reserve_extend_info.lowmark[2] = cfg[2];
	reserve_extend_info.timeout_sec = cfg[3];
	reserve_extend_info.slow_alloc_us = cfg[4];
	reserve_extend_info.scene = 2;

	config_show_pools = page_pools;

	/* blob ignored the register result; same here */
	register_trace_android_vh_mmput(clear_pid_from_pool_pid, NULL);

	reserve_extend_info.enabled = 1;
	reserve_pool_enable = 1;

	reserve_kobj = kobject_create_and_add("reserve_pool", kernel_kobj);
	if (!reserve_kobj) {
		printk(KERN_ERR "failed to create reserve pool node.\n");
		goto kthread;
	}
	ret = sysfs_create_group(reserve_kobj, &reserve_pool_attr_group);
	if (ret) {
		printk(KERN_ERR "failed to create reserve pool attrs.\n");
		kobject_put(reserve_kobj);
	}

kthread:
	reserve_refill_task = kthread_create_on_node(reserve_pool_refill_kthread,
						     system_reserve_pools,
						     NUMA_NO_NODE, "reserve-refill");
	if (IS_ERR(reserve_refill_task)) {
		printk(KERN_ERR "%s: failed to create reserve-refill: %ld\n",
		       __func__, PTR_ERR(reserve_refill_task));
		reserve_refill_task = NULL;
		return;
	}
	wake_up_process(reserve_refill_task);
	ret = sched_setattr(reserve_refill_task, &attr);
	if (ret) {
		printk(KERN_WARNING "%s: failed to set task priority for ret = %d\n",
		       __func__, ret);
		kthread_stop(reserve_refill_task);
	}
	for (i = 0; i < NUM_ORDERS; i++) {
		if (system_reserve_pools[i])
			system_reserve_pools[i]->refill_kthread = reserve_refill_task;
	}
}

static bool smmu_v3_enable;
static const struct dma_map_ops *smmu_dma_ops;
static struct iova_cache_data *get_iova_cache(struct system_heap_buffer *buffer,
					      u64 tab_id, bool coherent)
{
	struct iova_cache_data *cache_data;

	list_for_each_entry(cache_data, &buffer->iova_caches, iova_caches) {
		if ((cache_data->tab_id == tab_id) &&
		    (cache_data->coherent == coherent))
			return cache_data;
	}
	return NULL;
}


/* function declare */
static int system_buf_priv_dump(const struct dma_buf *dmabuf,
				struct seq_file *s);

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sgtable_sg(table, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static struct sg_table *dup_sg_table_by_range(struct sg_table *table,
					unsigned int offset, unsigned int len)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;
	unsigned int sg_offset = 0, sg_len;
	unsigned int contig_size = 0, found_start = 0;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	new_table->nents = 0;

	for_each_sg(table->sgl, sg, table->orig_nents, i) {
		if (!found_start) {
			sg_offset += sg->length;
			if (sg_offset <= offset)
				continue;
			found_start = 1;
			sg_len = len + sg->length - (sg_offset - offset);
		}

		if (contig_size >= sg_len)
			break;

		memcpy(new_sg, sg, sizeof(*sg));
		contig_size += sg->length;
		new_sg = sg_next(new_sg);
		new_table->nents++;
	}

	return new_table;
}

/* source copy to dest, no memory alloc */
static int copy_sg_table(struct sg_table *source, struct sg_table *dest)
{
	int i;
	struct scatterlist *sgl, *dest_sgl;

	if (source->orig_nents != dest->orig_nents) {
		pr_info("nents not match %d-%d\n",
			source->orig_nents, dest->orig_nents);

		return -EINVAL;
	}

	/* copy mapped nents */
	dest->nents = source->nents;

	dest_sgl = dest->sgl;
	for_each_sg(source->sgl, sgl, source->orig_nents, i) {
		if (!dest_sgl || !sgl) {
			pr_info("%s error: dest_sgl or sgl is null\n", __func__);
			break;
		}

		memcpy(dest_sgl, sgl, sizeof(*sgl));
		dest_sgl = sg_next(dest_sgl);
	}

	return 0;
};

/*
 * must check domain info before call fill_buffer_info
 * @Return 0: pass
 */
static int fill_buffer_info(struct system_heap_buffer *buffer,
			    struct sg_table *table,
			    struct dma_buf_attachment *a,
			    enum dma_data_direction dir,
			    u64 tab_id, unsigned int dom_id)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(a->dev);
	struct sg_table *new_table = NULL;
	struct iova_cache_data *cache_data;
	bool coherent = dev_is_dma_coherent(a->dev);
	int ret = 0;

	/*
	 * devices without iommus attribute,
	 * use common flow, skip set buf_info
	 */
	if (!smmu_v3_enable &&
	   (!fwspec || tab_id >= MTK_M4U_TAB_NR_MAX ||
	    dom_id >= MTK_M4U_DOM_NR_MAX))
		return 0;

	/* smmu only suuport one domain */
	if (smmu_v3_enable && !fwspec)
		return 0;

	cache_data = get_iova_cache(buffer, tab_id, coherent);
	if (cache_data != NULL && cache_data->mapped[dom_id]) {
		struct device *dev = cache_data->dev_info[dom_id].dev;

		pr_info("%s already mapped for dev:%s, tab_id:%llu, dom_id:%u",
			__func__, dev_name(dev), tab_id, dom_id);
		return -EINVAL;
	}

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return -ENOMEM;

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		pr_info("%s err: sg_alloc_table failed\n", __func__);
		kfree(new_table);
		return -ENOMEM;
	}

	ret = copy_sg_table(table, new_table);
	if (ret) {
		kfree(new_table);
		return ret;
	}

	if (!cache_data) {
		cache_data = kzalloc(sizeof(*cache_data), GFP_KERNEL);
		if (!cache_data) {
			kfree(new_table);
			return -ENOMEM;
		}
		cache_data->tab_id = tab_id;
		cache_data->coherent = coherent;
		list_add(&cache_data->iova_caches, &buffer->iova_caches);
	}

	cache_data->mapped_table[dom_id] = new_table;
	cache_data->mapped[dom_id] = true;
	cache_data->dev_info[dom_id].dev = a->dev;
	cache_data->dev_info[dom_id].direction = dir;
	/* TODO: check map_attrs affect??? */
	cache_data->dev_info[dom_id].map_attrs = a->dma_map_attrs;

	return 0;
}

static void mtk_query_smmu_dma_ops(struct device *dev)
{
	if (likely(!smmu_v3_enable || smmu_dma_ops))
		return;

	if (dev && get_smmu_device(dev) && dev->dma_ops)
		smmu_dma_ops = dev->dma_ops;
}

static bool is_cache_sync_dev(struct device *dev)
{
	if (dev && !dev_is_dma_coherent(dev) &&
	    (!dev->dma_ops || (dev->dma_ops == smmu_dma_ops)))
		return true;

	return false;
}

static int dma_coherent_check(struct device *dev, bool uncached)
{
#if IS_ENABLED(CONFIG_MTK_IOMMU_DEBUG)
	if (dev && dev_is_dma_coherent(dev) && uncached) {
		pr_info("%s fail, coherent dev:%s use uncached buffer!",
			__func__, dev_name(dev));
		return -EINVAL;
	}
#endif
	return 0;
}

static int system_heap_attach(struct dma_buf *dmabuf,
			      struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *table;
	u64 tm1, tm2, tm_mid;
	int ret;

	ret = dma_coherent_check(attachment->dev, buffer->uncached);
	if (ret)
		return ret;

	tm1 = sched_clock();
	DMABUF_TRACE_BEGIN("%s(%lu,%u)", __func__, buffer->len, buffer->sg_table.nents);
	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a) {
		DMABUF_TRACE_END();
		return -ENOMEM;
	}

	DMABUF_TRACE_BEGIN("dup_sg_table");
	table = dup_sg_table(&buffer->sg_table);
	DMABUF_TRACE_END();
	if (IS_ERR(table)) {
		kfree(a);
		DMABUF_TRACE_END();
		return -ENOMEM;
	}

	tm_mid = sched_clock();
	dmabuf_name_check(dmabuf, attachment->dev);
	mtk_query_smmu_dma_ops(attachment->dev);

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;
	a->uncached = buffer->uncached;
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	tm2 = sched_clock();
	if (tm2 - tm1 > RUN_TIMEOUT_TO_LOG)
		pr_info("dmabuf-time-attach:%llums,%llums,size:%lu,nent:%u\n",
			(tm2 - tm1)/1000000, (tm_mid - tm1)/1000000,
			buffer->len, buffer->sg_table.nents);

	DMABUF_TRACE_END();
	return 0;
}

static void system_heap_detach(struct dma_buf *dmabuf,
			       struct dma_buf_attachment *attachment)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *mtk_mm_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction direction)
{
	static DEFINE_RATELIMIT_STATE(dump_rs, MAP_FAILED_DUMP_RS_INTERVAL,
				      MAP_FAILED_DUMP_RS_BURST);
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int attr = attachment->dma_map_attrs;
	struct iova_cache_data *cache_data;
	u64 tm1, tm2, tm_mid;
	int ret;

	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(attachment->dev);
	unsigned int dom_id = 0;
	u64 tab_id = 0;
	struct system_heap_buffer *buffer = attachment->dmabuf->priv;
	bool coherent = dev_is_dma_coherent(attachment->dev);

	if (a->uncached)
		attr |= DMA_ATTR_SKIP_CPU_SYNC;

	tm1 = sched_clock();
	DMABUF_TRACE_BEGIN("%s(%lu,%u)", __func__, buffer->len, buffer->sg_table.nents);
	dmabuf_log_map(attachment->dmabuf);
	mutex_lock(&buffer->map_lock);

	if (fwspec && !smmu_v3_enable) {
		dom_id = MTK_M4U_TO_DOM(fwspec->ids[0]);
		tab_id = MTK_M4U_TO_TAB(fwspec->ids[0]);
		cache_data = get_iova_cache(buffer, tab_id, coherent);
	} else if (fwspec && smmu_v3_enable) {
		tab_id = get_smmu_tab_id(attachment->dev);
		cache_data = get_iova_cache(buffer, tab_id, coherent);
		dom_id = 0;
	}

	/* device with iommus attribute AND mapped before */
	if (fwspec && cache_data != NULL && cache_data->mapped[dom_id]) {
		/* mapped before, return saved table */
		ret = copy_sg_table(cache_data->mapped_table[dom_id], table);

		mutex_unlock(&buffer->map_lock);
		tm_mid = sched_clock();
		if (ret) {
			DMABUF_TRACE_END();
			return ERR_PTR(-EINVAL);
		}

		a->mapped = true;

		if (!(attr & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_sgtable_for_device(attachment->dev, table, direction);

		tm2 = sched_clock();
		if (tm2 - tm1 > RUN_TIMEOUT_TO_LOG)
			pr_info("dmabuf-time-map cache:%llums,%llums,size:%lu,nent:%u\n",
				(tm2 - tm1)/1000000, (tm2 - tm_mid)/1000000,
				buffer->len, buffer->sg_table.nents);
		DMABUF_TRACE_END();
		return table;
	}

	/* first map OR device without iommus attribute */
	if (dma_map_sgtable(attachment->dev, table, direction, attr)) {
		pr_info("%s map fail tab:%llu, dom:%d, dev:%s\n",
			__func__, tab_id, dom_id, dev_name(attachment->dev));
		mutex_unlock(&buffer->map_lock);

		DMABUF_TRACE_END("%s(fail)", __func__);
		if (!__ratelimit(&dump_rs))
			return ERR_PTR(-ENOMEM);

		if (dmabuf_rbtree_dump_by_domain)
			dmabuf_rbtree_dump_by_domain(tab_id, dom_id);

		return ERR_PTR(-ENOMEM);
	}

	tm_mid = sched_clock();
	DMABUF_TRACE_BEGIN("fill_buffer_info");
	ret = fill_buffer_info(buffer, table,
			       attachment, direction, tab_id, dom_id);
	DMABUF_TRACE_END();
	if (ret) {
		mutex_unlock(&buffer->map_lock);
		DMABUF_TRACE_END();
		return ERR_PTR(ret);
	}
	mutex_unlock(&buffer->map_lock);
	a->mapped = true;

	tm2 = sched_clock();
	if (tm2 - tm1 > RUN_TIMEOUT_TO_LOG)
		pr_info("dmabuf-time-map:%llums,%llums,size:%lu,nent:%u\n",
			(tm2 - tm1)/1000000, (tm2 - tm_mid)/1000000,
			buffer->len, buffer->sg_table.nents);
	DMABUF_TRACE_END();
	return table;
}

static struct sg_table *system_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int attr = attachment->dma_map_attrs;
	int ret;

	if (a->uncached)
		attr |= DMA_ATTR_SKIP_CPU_SYNC;

	ret = dma_map_sgtable(attachment->dev, table, direction, attr);
	if (ret)
		return ERR_PTR(ret);

	a->mapped = true;
	return table;
}

static void system_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	int attr = attachment->dma_map_attrs;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(attachment->dev);
	struct dma_buf *buf = attachment->dmabuf;

	if (a->uncached)
		attr |= DMA_ATTR_SKIP_CPU_SYNC;
	a->mapped = false;

	/*
	 * mtk_mm heap: for devices with iommus attribute,
	 * unmap iova when release dma-buf.
	 * system heap: unmap it every time
	 */
	if (is_mtk_mm_heap_dmabuf(buf) && fwspec) {
		if (!(attr & DMA_ATTR_SKIP_CPU_SYNC))
			dma_sync_sgtable_for_cpu(attachment->dev, table, direction);
		return;
	}

	dma_unmap_sgtable(attachment->dev, table, direction, attr);
}

static int system_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	bool synced = false;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;

			if (synced && is_cache_sync_dev(a->dev))
				continue;

			dma_sync_sgtable_for_cpu(a->dev, a->table, direction);
			if (!synced)
				synced = is_cache_sync_dev(a->dev);
		}
	}
	mutex_unlock(&buffer->lock);
	dmabuf_log_begin_cpu(dmabuf);

	return 0;
}

static int system_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	bool synced = false;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;

			if (synced && is_cache_sync_dev(a->dev))
				continue;

			dma_sync_sgtable_for_device(a->dev, a->table, direction);
			if (!synced)
				synced = is_cache_sync_dev(a->dev);
		}
	}
	mutex_unlock(&buffer->lock);
	dmabuf_log_end_cpu(dmabuf);

	return 0;
}

static int mtk_slc_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	//pr_info("%s slc_callback 0x%lx\n", __func__, (unsigned long)slc_callback);
	if (slc_callback)
		slc_callback(dmabuf);

	return 0;
}

static int system_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

	if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	for_each_sgtable_page(table, &piter, vma->vm_pgoff) {
		struct page *page = sg_page_iter_page(&piter);

		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret) {
			pr_info("%s-%d error: %d\n", __func__, __LINE__, ret);
			return ret;
		}
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static int mtk_mm_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	struct scatterlist *sg;
	unsigned int i;
	int ret;

	if (buffer->uncached)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page;
		unsigned long remainder = vma->vm_end - addr;
		unsigned long len;

		if (!sg) {
			pr_info("%s error: sg is null\n", __func__);
			break;
		}
		page = sg_page(sg);
		len = sg->length;

		if (offset >= sg->length) {
			offset -= sg->length;
			continue;
		} else if (offset) {
			page += offset / PAGE_SIZE;
			len = sg->length - offset;
			offset = 0;
		}
		len = min(len, remainder);
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), len,
				      vma->vm_page_prot);
		if (ret) {
			pr_info("%s-%d error: %d\n", __func__, __LINE__, ret);
			return ret;
		}
		addr += len;
		if (addr >= vma->vm_end)
			break;
	}
	dmabuf_log_mmap(dmabuf);

	return 0;
}

static void *system_heap_do_vmap(struct system_heap_buffer *buffer)
{
	struct sg_table *table = &buffer->sg_table;
	struct page **pages = NULL;
	int npages = 0;
	struct page **tmp = pages;
	struct sg_page_iter piter;
	pgprot_t pgprot = PAGE_KERNEL;
	void *vaddr;

	/*
	 * in 32bit project compile the arithmetic division, the "/" will
	 * cause the __aeabi_uldivmod error.
	 *
	 * use DO_DMA_BUFFER_COMMON_DIV and DO_DMA_BUFFER_COMMON_MOD to
	 * intead "/".
	 *
	 * original code is
	 * int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	 */
	npages = DO_DMA_BUFFER_COMMON_DIV(PAGE_ALIGN(buffer->len), PAGE_SIZE);

	pages = vmalloc(sizeof(struct page *) * npages);
	tmp = pages;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	if (buffer->uncached)
		pgprot = pgprot_writecombine(PAGE_KERNEL);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	vaddr = vmap(pages, npages, VM_MAP, pgprot);
	vfree(pages);

	if (!vaddr) {
		pr_info("%s vmap %d pages fail!", __func__, npages);
		return ERR_PTR(-ENOMEM);
	}

	return vaddr;
}

static int system_heap_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		iosys_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = system_heap_do_vmap(buffer);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto out;
	}

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
	iosys_map_set_vaddr(map, buffer->vaddr);
out:
	mutex_unlock(&buffer->lock);

	return ret;
}

static void system_heap_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	iosys_map_clear(map);
}

static int system_heap_zero_buffer(struct system_heap_buffer *buffer)
{
	struct sg_table *sgt = &buffer->sg_table;
	struct sg_page_iter piter;
	struct page *p;
	void *vaddr;
	int ret = 0;

	for_each_sgtable_page(sgt, &piter, 0) {
		p = sg_page_iter_page(&piter);
		vaddr = kmap_local_page(p);
		memset(vaddr, 0, PAGE_SIZE);
		kunmap_local(vaddr);
	}

	return ret;
}

static void system_heap_buf_free(struct mtk_deferred_freelist_item *item,
				 enum mtk_df_reason reason)
{
	struct mtk_dmabuf_page_pool **page_pools;
	struct mtk_heap_priv_info *heap_priv;
	struct system_heap_buffer *buffer;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;

	buffer = container_of(item, struct system_heap_buffer, deferred_free);
	heap_priv = dma_heap_get_drvdata(buffer->heap);
	page_pools = heap_priv ? heap_priv->page_pools : NULL;

	/* Zero the buffer pages before adding back to the pool */
	if (reason == MTK_DF_NORMAL)
		if (system_heap_zero_buffer(buffer))
			reason = MTK_DF_UNDER_PRESSURE; // On failure, just free

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page;

		if (!sg) {
			pr_info("%s error: sg is null\n", __func__);
			break;
		}
		page = sg_page(sg);

		if (reason == MTK_DF_UNDER_PRESSURE || !page_pools) {
			__free_pages(page, compound_order(page));
		} else {
			struct mtk_dmabuf_page_pool *pool;
			int total;

			for (j = 0; j < NUM_ORDERS; j++) {
				if (compound_order(page) == orders[j])
					break;
			}

			if (j >= NUM_ORDERS) {
				pr_info("%s error order:%u\n",
					__func__, compound_order(page));
			} else {
				pool = page_pools[j];
				spin_lock(&pool->lock);
				total = pool->count[POOL_LOWPAGE] +
					pool->count[POOL_HIGHPAGE];
				spin_unlock(&pool->lock);

				/* rodin b262 (A-113): blob system_heap_buf_free
				 * 0x1F18-0x1F34（0x6400 lsr order 后 cmp，b.lt
				 * 归动态池）-- 动态池达 100MB cap 时溢出的释放
				 * chunk 才转投 reserve pool（且其 lowmark 仍有
				 * 余量）；低于 cap 归还原动态池 */
				if (total >= (RESERVE_POOL_CAP_CHUNKS >> orders[j]) &&
				    need_free_to_reserve_pool(j))
					mtk_dmabuf_page_pool_free(system_reserve_pools[j],
								  page);
				else
					mtk_dmabuf_page_pool_free(pool, page);
			}
		}
	}

	if (page_pools) {
		struct mtk_dmabuf_page_pool *pool = page_pools[0];

		if (pool->refill_kthread && !pool->recycling &&
		    pool->need_recycle && pool->need_recycle(pool))
			wake_up_process(pool->refill_kthread);
	}

	sg_free_table(table);
	kfree(buffer);
}

static void mtk_mm_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	int npages = 0;
	unsigned long buf_len = buffer->len;
	struct iova_cache_data *cache_data, *temp_data;
	struct mtk_heap_dev_info dev_info;
	struct sg_table *table;
	unsigned long attrs;
	int k;

	/*
	 * in 32bit project compile the arithmetic division, the "/" will
	 * cause the __aeabi_uldivmod error.
	 *
	 * use DO_DMA_BUFFER_COMMON_DIV and DO_DMA_BUFFER_COMMON_MOD to
	 * intead "/".
	 *
	 * original code is
	 * int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	 */
	npages = DO_DMA_BUFFER_COMMON_DIV(PAGE_ALIGN(buffer->len), PAGE_SIZE);

	spin_lock(&dmabuf->name_lock);
	pr_debug("%s: inode:%lu, size:%lu, name:%s\n", __func__,
		 file_inode(dmabuf->file)->i_ino, buffer->len,
		 dmabuf->name?:"NULL");
	spin_unlock(&dmabuf->name_lock);

	dmabuf_release_check(dmabuf);

	/* unmap all domains' iova */
	list_for_each_entry_safe(cache_data, temp_data, &buffer->iova_caches, iova_caches) {
		for (k = 0; k < MTK_M4U_DOM_NR_MAX; k++) {
			table = cache_data->mapped_table[k];
			dev_info = cache_data->dev_info[k];
			attrs = dev_info.map_attrs;

			if (buffer->uncached)
				attrs |= DMA_ATTR_SKIP_CPU_SYNC;

			if (!cache_data->mapped[k])
				continue;

			dma_unmap_sgtable(dev_info.dev, table, dev_info.direction, attrs);
			cache_data->mapped[k] = false;
			sg_free_table(table);
			kfree(table);
		}
		list_del(&cache_data->iova_caches);
		kfree(cache_data);
	}

	/* free buffer memory */
	mtk_deferred_free(&buffer->deferred_free, system_heap_buf_free, npages);

	if (atomic64_sub_return(buf_len, &dma_heap_normal_total) < 0) {
		pr_info("warn: %s, total memory underflow, 0x%llx!!, reset as 0\n",
			__func__, atomic64_read(&dma_heap_normal_total));
		atomic64_set(&dma_heap_normal_total, 0);
	}
}

static void system_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct mtk_dmabuf_page_pool **page_pools;
	struct mtk_heap_priv_info *heap_priv;
	unsigned long buf_len = buffer->len;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;

	dmabuf_release_check(dmabuf);

	heap_priv = dma_heap_get_drvdata(buffer->heap);
	page_pools = heap_priv ? heap_priv->page_pools : NULL;

	/* Zero the buffer pages before adding back to the pool */
	system_heap_zero_buffer(buffer);

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page;

		if (!sg) {
			pr_info("%s error: sg is null\n", __func__);
			break;
		}
		page = sg_page(sg);

		for (j = 0; j < NUM_ORDERS; j++) {
			if (compound_order(page) == orders[j])
				break;
		}
		if (j < NUM_ORDERS) {
			if (page_pools)
				mtk_dmabuf_page_pool_free(page_pools[j], page);
			else
				__free_pages(page, compound_order(page));
		} else {
			pr_info("%s error: order %u\n", __func__, compound_order(page));
		}
	}
	sg_free_table(table);
	kfree(buffer);

	if (atomic64_sub_return(buf_len, &dma_heap_normal_total) < 0) {
		pr_info("warn: %s, total memory underflow, 0x%llx!!, reset as 0\n",
			__func__, atomic64_read(&dma_heap_normal_total));
		atomic64_set(&dma_heap_normal_total, 0);
	}
}

static int system_heap_dma_buf_get_flags(struct dma_buf *dmabuf, unsigned long *flags)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	*flags = buffer->uncached;

	return 0;
}

static int dmabuf_check_user_args(unsigned int offset, unsigned int len,
					unsigned long buf_len)
{
	if (!len || len > buf_len || offset >= buf_len ||
	    offset + len > buf_len)
		return -EINVAL;

	return 0;
}

static int mtk_mm_heap_dma_buf_begin_cpu_access_partial(struct dma_buf *dmabuf,
					enum dma_data_direction direction,
					unsigned int offset, unsigned int len)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *sgt_tmp;
	bool synced = false;
	int ret;

	mutex_lock(&buffer->lock);

	ret = dmabuf_check_user_args(offset, len, buffer->len);
	if (ret) {
		pr_err("%s: invalid args, off:0x%x, len:0x%x, buf_len:0x%lx\n",
		       __func__, offset, len, buffer->len);
		mutex_unlock(&buffer->lock);
		return ret;
	}

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr + offset, len);


	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;

			if (synced && is_cache_sync_dev(a->dev))
				continue;

			sgt_tmp = dup_sg_table_by_range(a->table, offset, len);
			if (IS_ERR(sgt_tmp)) {
				pr_err("%s: dup sg_table failed!\n", __func__);
				mutex_unlock(&buffer->lock);
				return -ENOMEM;
			}

			dma_sync_sg_for_cpu(a->dev, sgt_tmp->sgl,
					    sgt_tmp->nents, direction);
			if (!synced)
				synced = is_cache_sync_dev(a->dev);

			sg_free_table(sgt_tmp);
			kfree(sgt_tmp);
		}
	}

	mutex_unlock(&buffer->lock);
	dmabuf_log_begin_cpu_partial(dmabuf, len);

	return 0;
}

static int mtk_mm_heap_dma_buf_end_cpu_access_partial(struct dma_buf *dmabuf,
					enum dma_data_direction direction,
					unsigned int offset, unsigned int len)
{
	struct system_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *sgt_tmp;
	bool synced = false;
	int ret;

	mutex_lock(&buffer->lock);

	ret = dmabuf_check_user_args(offset, len, buffer->len);
	if (ret) {
		pr_err("%s: invalid args, off:0x%x, len:0x%x, buf_len:0x%lx\n",
		       __func__, offset, len, buffer->len);
		mutex_unlock(&buffer->lock);
		return ret;
	}

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr + offset, len);

	if (!buffer->uncached) {
		list_for_each_entry(a, &buffer->attachments, list) {
			if (!a->mapped)
				continue;

			if (synced && is_cache_sync_dev(a->dev))
				continue;

			sgt_tmp = dup_sg_table_by_range(a->table, offset, len);
			if (IS_ERR(sgt_tmp)) {
				pr_err("%s: dup sg_table failed!\n", __func__);
				mutex_unlock(&buffer->lock);
				return -ENOMEM;
			}

			dma_sync_sg_for_device(a->dev, sgt_tmp->sgl,
					       sgt_tmp->nents, direction);
			if (!synced)
				synced = is_cache_sync_dev(a->dev);

			sg_free_table(sgt_tmp);
			kfree(sgt_tmp);
		}
	}

	mutex_unlock(&buffer->lock);
	dmabuf_log_end_cpu_partial(dmabuf, len);

	return 0;
}

static const struct dma_buf_ops mtk_mm_heap_buf_ops = {
	/* 1 attachment can only map 1 iova */
	.attach = system_heap_attach,
	.detach = system_heap_detach,
	.map_dma_buf = mtk_mm_heap_map_dma_buf,
	.unmap_dma_buf = system_heap_unmap_dma_buf,
	.begin_cpu_access = system_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = system_heap_dma_buf_end_cpu_access,
	.begin_cpu_access_partial =
			mtk_mm_heap_dma_buf_begin_cpu_access_partial,
	.end_cpu_access_partial = mtk_mm_heap_dma_buf_end_cpu_access_partial,
	.mmap = mtk_mm_heap_mmap,
	.vmap = system_heap_vmap,
	.vunmap = system_heap_vunmap,
	.release = mtk_mm_heap_dma_buf_release,
	.get_flags = system_heap_dma_buf_get_flags,
};

static const struct dma_buf_ops system_heap_buf_ops = {
	/* 1 attachment can only map 1 iova */
	.attach = system_heap_attach,
	.detach = system_heap_detach,
	.map_dma_buf = system_heap_map_dma_buf,
	.unmap_dma_buf = system_heap_unmap_dma_buf,
	.begin_cpu_access = system_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = system_heap_dma_buf_end_cpu_access,
	.mmap = system_heap_mmap,
	.vmap = system_heap_vmap,
	.vunmap = system_heap_vunmap,
	.release = system_heap_dma_buf_release,
	.get_flags = system_heap_dma_buf_get_flags,
};

static const struct dma_buf_ops mtk_slc_heap_buf_ops = {
	/* 1 attachment can only map 1 iova */
	.attach = system_heap_attach,
	.detach = system_heap_detach,
	.map_dma_buf = system_heap_map_dma_buf,
	.unmap_dma_buf = system_heap_unmap_dma_buf,
	.begin_cpu_access = system_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = mtk_slc_dma_buf_end_cpu_access,
	.mmap = system_heap_mmap,
	.vmap = system_heap_vmap,
	.vunmap = system_heap_vunmap,
	.release = system_heap_dma_buf_release,
	.get_flags = system_heap_dma_buf_get_flags,
};

static struct page *alloc_largest_available(unsigned long size,
					    unsigned int max_order,
					    struct mtk_dmabuf_page_pool **page_pools)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size <  (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		if (likely(page_pools)) {
			page = mtk_dmabuf_page_pool_alloc(page_pools[i]);
			if (!page)
				continue;

			if (page_pools && page_pools[i]->refill_kthread &&
				!page_pools[i]->refilling && page_pools[i]->need_refill &&
				page_pools[i]->need_refill(page_pools[i]))
				wake_up_process(page_pools[i]->refill_kthread);
		} else {
			page = alloc_pages(order_flags[i], orders[i]);
		}

		return page;
	}
	return NULL;
}

static struct dma_buf *system_heap_do_allocate(struct dma_heap *heap,
					       unsigned long len,
					       u32 fd_flags,
					       u64 heap_flags,
					       bool uncached,
					       const struct dma_buf_ops *ops)
{
	struct system_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;
	struct task_struct *task = current->group_leader;
	struct mtk_dmabuf_page_pool **page_pools;
	struct mtk_heap_priv_info *heap_priv = dma_heap_get_drvdata(heap);
	u64 tm1, tm2, tm_mid;

	tm1 = sched_clock();
	DMABUF_TRACE_BEGIN("%s(%s,%lu)", __func__, dma_heap_get_name(heap), len);
	page_pools = heap_priv ? heap_priv->page_pools : NULL;

	/*
	 * in 32bit project compile the arithmetic division, the "/" will
	 * cause the __aeabi_uldivmod error.
	 *
	 * use DO_DMA_BUFFER_COMMON_DIV and DO_DMA_BUFFER_COMMON_MOD to
	 * intead "/".
	 *
	 * original code is
	 * if (len / PAGE_SIZE > totalram_pages()) {
	 */
	if (DO_DMA_BUFFER_COMMON_DIV(len, PAGE_SIZE) > totalram_pages()) {
		pr_info("%s error: len %ld is more than %ld\n",
			__func__, len, totalram_pages() * PAGE_SIZE);
		DMABUF_TRACE_END();
		return ERR_PTR(-ENOMEM);
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer) {
		DMABUF_TRACE_END();
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&buffer->attachments);
	INIT_LIST_HEAD(&buffer->iova_caches);
	mutex_init(&buffer->lock);
	buffer->heap = heap;
	buffer->len = len;
	buffer->uncached = uncached;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			pr_info("%s, %d, %s, current process fatal_signal_pending\n",
				__func__, __LINE__, dma_heap_get_name(heap));
			goto free_buffer;
		}

		/* rodin b262 (A-113): blob 0x8BF4 -- when the reserve face is on,
		 * allocations route through the reserve-pool-aware allocator
		 * (whitelist gate + reserve fallback + slow-path instrumentation) */
		if (reserve_extend_info.enabled)
			page = mtk_extend_sys_heap_alloc_largest_available(
					page_pools, size_remaining, max_order);
		else
			page = alloc_largest_available(size_remaining, max_order,
						       page_pools);
		if (!page) {
			if (fatal_signal_pending(current)) {
				ret = -EINTR;
				pr_info("%s, %d, %s, current process fatal_signal_pending\n",
					__func__, __LINE__, dma_heap_get_name(heap));
			}
			goto free_buffer;
		}

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	tm_mid = sched_clock();
	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	mutex_init(&buffer->map_lock);
	/* add alloc pid & tid info */
	get_task_comm(buffer->pid_name, task);
	get_task_comm(buffer->tid_name, current);
	buffer->pid = task_pid_nr(task);
	buffer->tid = task_pid_nr(current);

	/*
	 * in 32bit project compile the arithmetic division, the "/" will
	 * cause the __aeabi_uldivmod error.
	 *
	 * use DO_DMA_BUFFER_COMMON_DIV and DO_DMA_BUFFER_COMMON_MOD to
	 * intead "/".
	 *
	 * original code is
	 * buffer->ts  = sched_clock() / 1000;
	 */
	buffer->ts  = DO_DMA_BUFFER_COMMON_DIV(sched_clock(), 1000);
	buffer->show = system_buf_priv_dump;
	buffer->gid = -1;

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	DMABUF_TRACE_BEGIN("dma_buf_export");
	dmabuf = dma_buf_export(&exp_info);
	DMABUF_TRACE_END();
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	/*
	 * For uncached buffers, we need to initially flush cpu cache, since
	 * the __GFP_ZERO on the allocation means the zeroing was done by the
	 * cpu and thus it is likely cached. Map (and implicitly flush) and
	 * unmap it now so we don't get corruption later on.
	 */
	if (buffer->uncached) {
		dma_map_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
		dma_unmap_sgtable(dma_heap_get_dev(heap), table, DMA_BIDIRECTIONAL, 0);
	}

	atomic64_add(dmabuf->size, &dma_heap_normal_total);

	tm2 = sched_clock();
	dmabuf_log_allocate(heap, tm2 - tm1, i, len);
	if (tm2 - tm1 > RUN_TIMEOUT_TO_LOG)
		pr_info("dmabuf-time-alloc %llums,%llums,size:%lu,nent:%u,heap:%s,pool:%lu\n",
			(tm2 - tm1)/1000000, (tm_mid - tm1)/1000000,
			buffer->len, buffer->sg_table.nents,
			dma_heap_get_name(heap), mtk_dmabuf_page_pool_size(heap)>>10);

	DMABUF_TRACE_END("%s(nents:%d)", __func__, i);
	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *p = sg_page(sg);

		__free_pages(p, compound_order(p));
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, compound_order(page));
	kfree(buffer);

	pr_info("%s, %d return error:%d\n", __func__, __LINE__, ret);
	DMABUF_TRACE_END();
	return ERR_PTR(ret);
}

static struct dma_buf *system_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    u32 fd_flags,
					    u64 heap_flags)
{
	struct mtk_heap_priv_info *heap_priv = dma_heap_get_drvdata(heap);

	return system_heap_do_allocate(heap, len, fd_flags, heap_flags,
				       heap_priv ? heap_priv->uncached : false,
				       &system_heap_buf_ops);
}

static struct dma_buf *mtk_mm_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    u32 fd_flags,
					    u64 heap_flags)
{
	struct mtk_heap_priv_info *heap_priv = dma_heap_get_drvdata(heap);

	return system_heap_do_allocate(heap, len, fd_flags, heap_flags,
				       heap_priv ? heap_priv->uncached : false,
				       &mtk_mm_heap_buf_ops);
}

static struct dma_buf *mtk_slc_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    u32 fd_flags,
					    u64 heap_flags)
{
	return system_heap_do_allocate(heap, len, fd_flags, heap_flags, false,
				       &mtk_slc_heap_buf_ops);
}

static __maybe_unused long mtk_get_pool_size(struct dma_heap *heap)
{
	struct mtk_heap_priv_info *heap_priv;

	heap_priv = heap ? dma_heap_get_drvdata(heap) : NULL;
	if (heap_priv && !heap_priv->uncached)
		return mtk_dmabuf_page_pool_size(heap);

	return 0;
}

static const struct dma_heap_ops system_heap_ops = {
	.allocate = system_heap_allocate,
};

static const struct dma_heap_ops mtk_mm_heap_ops = {
	.allocate = mtk_mm_heap_allocate,
};

static const struct dma_heap_ops mtk_slc_heap_ops = {
	.allocate = mtk_slc_heap_allocate,
};

static int system_buf_priv_dump(const struct dma_buf *dmabuf,
				struct seq_file *s)
{
	int k = 0;
	struct system_heap_buffer *buf = NULL;
	struct list_head *cache_node;
	struct iova_cache_data *cache_data;

	if (WARN_ON(!dmabuf || !dmabuf->priv))
		return 0;

	buf = dmabuf->priv;

	dmabuf_dump(s, "\tbuf_priv: uncached:%d alloc_pid:%d(%s)tid:%d(%s) alloc_time:%lluus\n",
		    !!buf->uncached,
		    buf->pid, buf->pid_name,
		    buf->tid, buf->tid_name,
		    buf->ts);

	if (is_system_heap_dmabuf(dmabuf))
		return 0;

	mutex_lock(&buf->map_lock);
	list_for_each(cache_node, &buf->iova_caches) {
		cache_data = list_entry(cache_node, struct iova_cache_data, iova_caches);
		for (k = 0; k < MTK_M4U_DOM_NR_MAX; k++) {
			bool mapped = cache_data->mapped[k];
			struct device *dev = cache_data->dev_info[k].dev;
			struct sg_table *sgt = cache_data->mapped_table[k];

			if (!mapped || !sgt || !sgt->sgl || !dev || !dev_iommu_fwspec_get(dev))
				continue;

			dmabuf_dump(s,
				    "\tbuf_priv: tab:%-2llu dom:%-2d map:%d iova:0x%-12lx attr:0x%-4lx dir:%-2d dev:%s\n",
				    cache_data->tab_id, k, mapped,
				    (unsigned long)sg_dma_address(sgt->sgl),
				    cache_data->dev_info[k].map_attrs,
				    cache_data->dev_info[k].direction,
				    dev_name(dev));
		}
	}
	mutex_unlock(&buf->map_lock);

	return 0;
}

/**
 * return none-zero value means dump fail.
 *       maybe the input dmabuf isn't this heap buffer, no need dump
 *
 * return 0 means dump pass
 */
static int system_heap_buf_priv_dump(const struct dma_buf *dmabuf,
				     struct dma_heap *heap,
				     void *priv)
{
	struct seq_file *s = priv;
	struct system_heap_buffer *buf = dmabuf->priv;

	if (!is_system_heap_dmabuf(dmabuf) && !is_mtk_mm_heap_dmabuf(dmabuf))
		return -EINVAL;

	if (heap != buf->heap)
		return -EINVAL;

	if (buf->show)
		return buf->show(dmabuf, s);

	return -EINVAL;
}


static int set_heap_dev_dma(struct device *heap_dev)
{
	if (!heap_dev)
		return -EINVAL;

	dma_coerce_mask_and_coherent(heap_dev, DMA_BIT_MASK(64));

	if (!heap_dev->dma_parms) {
		heap_dev->dma_parms = devm_kzalloc(heap_dev,
						   sizeof(*heap_dev->dma_parms),
						   GFP_KERNEL);
		if (!heap_dev->dma_parms)
			return -ENOMEM;

		/* rodin r25: 6.18 dma_set_max_seg_size() returns void */
		dma_set_max_seg_size(heap_dev, (unsigned int)DMA_BIT_MASK(64));
	}

	return 0;
}

static int mtk_heap_create(const char *name, const struct dma_heap_ops *ops,
			   bool uncached, bool pool)
{
	struct dma_heap_export_info exp_info;
	struct mtk_heap_priv_info *heap_priv;
	struct mtk_heap_priv_info *heap_priv_uncache;
	struct dma_heap *heap;
	char *name_uncache;
	int ret;

	heap_priv = kzalloc(sizeof(*heap_priv), GFP_KERNEL);
	if (!heap_priv) {
		ret = -ENOMEM;
		goto out;
	}

	exp_info.priv = heap_priv;
	exp_info.name = name;
	exp_info.ops = ops;

	if (pool) {
		heap_priv->page_pools = dynamic_page_pools_create(orders, NUM_ORDERS);
		if (IS_ERR(heap_priv->page_pools)) {
			ret = -ENOMEM;
			goto out_free_heap;
		}
	}

	/* rodin b262 (A-113): blob 0x869C -- the "system" heap owns the
	 * global reserve pools; running inside mtk_system_heap_create keeps
	 * the stock 0.53s creation phase (6.6 dmesg anchor). */
	if (!strcmp(name, "system"))
		mtk_sys_heap_reserve_pool_init(heap_priv->page_pools);
	heap_priv->uncached = false;
	heap_priv->buf_priv_dump = system_heap_buf_priv_dump;

	heap = dma_heap_add(&exp_info);
	if (IS_ERR(heap)) {
		ret = -ENOMEM;
		goto out_free_pools;
	}
	ret = set_heap_dev_dma(dma_heap_get_dev(heap));
	if (ret)
		goto out_free_pools;

	pr_info("%s add heap[%s] success\n", __func__, exp_info.name);
	/* Add uncached heap if need */
	if (uncached) {
		name_uncache = kasprintf(GFP_KERNEL, "%s-uncached", name);
		if (!name_uncache) {
			ret = -ENOMEM;
			goto out_free_pools;
		}
		heap_priv_uncache = kzalloc(sizeof(*heap_priv), GFP_KERNEL);
		if (!heap_priv_uncache) {
			ret = -ENOMEM;
			goto out_free_heap_1;
		}

		exp_info.priv = heap_priv_uncache;
		exp_info.name = name_uncache;
		exp_info.ops = ops;

		heap_priv_uncache->page_pools = heap_priv->page_pools;
		heap_priv_uncache->uncached = true;
		heap_priv_uncache->buf_priv_dump = system_heap_buf_priv_dump;

		heap = dma_heap_add(&exp_info);
		if (IS_ERR(heap)) {
			ret = -ENOMEM;
			goto out_free_heap_2;
		}

		ret = set_heap_dev_dma(dma_heap_get_dev(heap));
		if (ret)
			goto out_free_heap_2;

		pr_info("%s add heap[%s] success\n", __func__, exp_info.name);
	}

	return 0;

out_free_heap_2:
	kfree(heap_priv_uncache);
out_free_heap_1:
	kfree(name_uncache);
out_free_pools:
	dynamic_page_pools_free(heap_priv->page_pools, NUM_ORDERS);
out_free_heap:
	kfree(heap_priv);
out:
	pr_info("%s: failed to create heap %s with error %d\n",
		__func__, name, ret);
	return ret;
}

int mtk_dma_heap_config_parse(struct device *dev, struct mtk_dma_heap_config *heap_config)
{
	const struct mtk_dma_heap_match_data *dmaheap_data = of_device_get_match_data(dev);
	const struct device_node *node = dev->of_node;
	const char *heap_name;
	u32 trusted_mem_type;
	u32 max_align;
	int ret;

	if (!heap_config)
		return -EINVAL;

	ret = of_property_read_string(node, "heap-name", &heap_name);
	if (ret) {
		pr_info("%s has no heap-name node:%s\n", __func__, node->name);
		return -EINVAL;
	}

	heap_config->heap_type = dmaheap_data->dmaheap_type;
	heap_config->heap_name = heap_name;
	heap_config->heap_uncached = of_property_read_bool(node, "heap-uncached");

	ret = of_property_read_string(node, "region-heap-align-name", &heap_name);
	if (!ret)
		heap_config->region_heap_align_name = heap_name;

	ret = of_property_read_u32(node, "trusted-mem-type", &trusted_mem_type);
	if (!ret)
		heap_config->trusted_mem_type = trusted_mem_type;

	ret = of_property_read_u32(node, "heap-max-align", &max_align);
	if (!ret)
		heap_config->max_align = max_align;

	heap_config->dev = dev;
	pr_info("%s node:%s,type:%d,name:%s,uncached:%d,trusted:%d,dev:%s\n",
		__func__, node->name, heap_config->heap_type,
		heap_config->heap_name?:"NULL", heap_config->heap_uncached?1:0,
		heap_config->trusted_mem_type,
		heap_config->dev?dev_name(heap_config->dev):"NULL");

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_dma_heap_config_parse);

static int mtk_dma_heap_config_probe(struct platform_device *pdev)
{
	struct mtk_dma_heap_config heap_config = {0};
	int ret;

	ret = mtk_dma_heap_config_parse(&pdev->dev, &heap_config);
	if (ret)
		return ret;

	if (heap_config.heap_type == DMA_HEAP_MTK_SLC)
		mtk_heap_create(heap_config.heap_name, &mtk_slc_heap_ops, false, false);

	return 0;
}

static const struct mtk_dma_heap_match_data dmaheap_data_mtk_slc = {
	.dmaheap_type = DMA_HEAP_MTK_SLC,
};

static const struct of_device_id mtk_dma_heap_match_table[] = {
	{.compatible = "mediatek,dmaheap-mtk-slc", .data = &dmaheap_data_mtk_slc},
	{},
};

static struct platform_driver mtk_dma_heap_config_driver = {
	.probe = mtk_dma_heap_config_probe,
	.driver = {
		.name = "mtk-dma-heap-slc",
		.of_match_table = mtk_dma_heap_match_table,
	},
};

static int mtk_system_heap_create(void)
{
	int ret;

	// smmu support 4K/2M/1G granule
	smmu_v3_enable = smmu_v3_enabled();
	if (smmu_v3_enable) {
#if IS_ENABLED(CONFIG_ARM64_16K_PAGES)
		orders[0] = 11;
		orders[1] = 7;
#else
		orders[0] = 9;
#endif
	}

	mtk_dmabuf_page_pool_init_shrinker();
	mtk_deferred_freelist_init();

	mtk_heap_create("system", &system_heap_ops, true, true);
	mtk_heap_create("mtk_mm", &mtk_mm_heap_ops, true, true);

	ret = platform_driver_register(&mtk_dma_heap_config_driver);
	if (ret < 0) {
		pr_info("%s fail to register mtk_slc heap: %d\n", __func__, ret);
		return ret;
	}

	mtk_cache_pool_init();
	return 0;
}

static void mtk_system_heap_exit(void)
{
	platform_driver_unregister(&mtk_dma_heap_config_driver);
}

const char *refill_heap_names[] = {
	"mtk_mm",
	NULL,
};

const char **mtk_refill_heap_names(void)
{
	return &refill_heap_names[0];
}
EXPORT_SYMBOL_GPL(mtk_refill_heap_names);

/* ref code: dma_buf.c, dma_buf_set_name */
long mtk_dma_buf_set_name(struct dma_buf *dmabuf, const char *buf)
{
	char *name = kstrndup(buf, DMA_BUF_NAME_LEN, GFP_KERNEL);
	long ret = 0;

	if (IS_ERR(name))
		return PTR_ERR(name);

	/* different with dma_buf.c, always enable setting name */
	spin_lock(&dmabuf->name_lock);
	kfree(dmabuf->name);
	dmabuf->name = name;
	spin_unlock(&dmabuf->name_lock);

	return ret;
} EXPORT_SYMBOL_GPL(mtk_dma_buf_set_name);

int is_mtk_mm_heap_dmabuf(const struct dma_buf *dmabuf)
{
	if (dmabuf && dmabuf->ops == &mtk_mm_heap_buf_ops)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(is_mtk_mm_heap_dmabuf);

int is_system_heap_dmabuf(const struct dma_buf *dmabuf)
{
	if (dmabuf && dmabuf->ops == &system_heap_buf_ops)
		return 1;
	return 0;
}
EXPORT_SYMBOL_GPL(is_system_heap_dmabuf);

int is_uncached_dmabuf(const struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer;

	if (IS_ERR_OR_NULL(dmabuf))
		return 0;

	buffer = dmabuf->priv;
	if (IS_ERR_OR_NULL(buffer))
		return 0;

	return buffer->uncached;
}
EXPORT_SYMBOL_GPL(is_uncached_dmabuf);

hang_dump_cb hang_dump_proc;
EXPORT_SYMBOL_GPL(hang_dump_proc);

int mtk_dmaheap_register_slc_callback(dmaheap_slc_callback cb)
{
	//pr_info("%s slc_callback:0x%lx cb:0x%lx\n", __func__,
	//	(unsigned long)slc_callback, (unsigned long)cb);
	if (slc_callback)
		return -EPERM;

	slc_callback = cb;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_dmaheap_register_slc_callback);

int mtk_dmaheap_unregister_slc_callback(void)
{
	slc_callback = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(mtk_dmaheap_unregister_slc_callback);

int dma_buf_set_gid(struct dma_buf *dmabuf, int gid)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	if (IS_ERR_OR_NULL(buffer))
		return -EINVAL;

	buffer->gid = gid;
	return 0;
}
EXPORT_SYMBOL_GPL(dma_buf_set_gid);

int dma_buf_get_gid(struct dma_buf *dmabuf)
{
	struct system_heap_buffer *buffer = dmabuf->priv;

	if (IS_ERR_OR_NULL(buffer))
		return -EINVAL;

	return buffer->gid;
}
EXPORT_SYMBOL_GPL(dma_buf_get_gid);

vseq_module_init(mtk_system_heap_create);
module_exit(mtk_system_heap_exit);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
