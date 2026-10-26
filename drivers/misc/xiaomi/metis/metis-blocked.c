// SPDX-License-Identifier: GPL-2.0-only
/* PARTIAL blocked/OEM owner state. No registrations, exports or lifecycle. */
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/hashtable.h>
#include <linux/cpu.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/tracepoint.h>
#include "metis_blocked.h"
#include "metis_classify.h"
#include "metis-final-trace.h"

/* Only this event is defined here; the shared systrace event lives in
 * metis-final-trace.h. */
#define CREATE_TRACE_POINTS
#include "metis-blocked-trace.h"
#undef CREATE_TRACE_POINTS

#define METIS_RWSEM_TAG_UNKNOWN 0xab9f19ab00000000ULL

/* rodin 6.18：mutex 的 android_oem_data1 只有单 u64，装不下 16B payload
 * ⇒ 载体改模块级侧表（lock 指针 -> payload），查表带 magic 校验、
 * clear 时删槽。语义与 stock（lock 内嵌槽）等价：查无即 NULL。
 * rodin 并发口径：所有访问都在 mutex_side_lock 内；条目 clear 后进
 * freelist 由 attach 复用、永不 kfree —— 调用方在锁外持 payload 指针
 * 也不存在 UAF（锁复用语义与 stock 内嵌槽随锁对象复用一致）。 */
#define METIS_MUTEX_SIDE_BUCKETS 1024
static struct hlist_head mutex_side_table[METIS_MUTEX_SIDE_BUCKETS];
static struct hlist_head mutex_side_free_list;
static DEFINE_SPINLOCK(mutex_side_lock);

struct metis_mutex_side_entry {
	struct hlist_node node;
	void *lock_key;
	struct metis_mutex_oem_unknown payload;
};

/* Caller must hold mutex_side_lock. */
static struct metis_mutex_side_entry *mutex_side_find(struct mutex *lock)
{
	struct metis_mutex_side_entry *e;

	hash_for_each_possible(mutex_side_table, e, node, (unsigned long)lock)
		if (e->lock_key == (void *)lock)
			return e;
	return NULL;
}

static struct metis_mutex_oem_unknown *mutex_oem_unknown(struct mutex *lock)
{
	struct metis_mutex_side_entry *e;
	unsigned long flags;

	if (!lock)
		return NULL;
	spin_lock_irqsave(&mutex_side_lock, flags);
	e = mutex_side_find(lock);
	spin_unlock_irqrestore(&mutex_side_lock, flags);
	return e ? &e->payload : NULL;
}

struct metis_mutex_oem_unknown *metis_mutex_side_lookup(struct mutex *lock)
{
	return mutex_oem_unknown(lock);
}

struct metis_mutex_oem_unknown *mutex_oem_attach(struct mutex *lock)
{
	struct metis_mutex_side_entry *e;
	unsigned long flags;

	if (!lock)
		return NULL;
	spin_lock_irqsave(&mutex_side_lock, flags);
	e = mutex_side_find(lock);
	if (!e) {
		e = hlist_entry_safe(mutex_side_free_list.first,
				     struct metis_mutex_side_entry, node);
		if (e)
			hlist_del(&e->node);
	}
	if (e) {
		e->lock_key = (void *)lock;
		memset(&e->payload, 0, sizeof(e->payload));
		hash_add(mutex_side_table, &e->node, (unsigned long)lock);
	} else {
		spin_unlock_irqrestore(&mutex_side_lock, flags);
		e = kzalloc(sizeof(*e), GFP_ATOMIC);
		if (!e)
			return NULL;
		spin_lock_irqsave(&mutex_side_lock, flags);
		e->lock_key = (void *)lock;
		hash_add(mutex_side_table, &e->node, (unsigned long)lock);
	}
	spin_unlock_irqrestore(&mutex_side_lock, flags);
	return &e->payload;
}

void mutex_oem_clear(struct mutex *lock)
{
	struct metis_mutex_side_entry *e;
	unsigned long flags;

	spin_lock_irqsave(&mutex_side_lock, flags);
	e = mutex_side_find(lock);
	if (e) {
		hash_del(&e->node);
		hlist_add_head(&e->node, &mutex_side_free_list);
	}
	spin_unlock_irqrestore(&mutex_side_lock, flags);
}

static struct metis_rwsem_oem_unknown *rwsem_oem_unknown(struct rw_semaphore *sem)
{
	return sem ? (void *)sem->android_oem_data1 : NULL;
}

/* Stock accepts only zero lower 32 bits and zero deadline. It does not
 * recognize the 0x149f tag that it subsequently writes. Preserve this. */
static bool mutex_zero_unknown(struct metis_mutex_oem_unknown *data)
{
	if (!data || data->unknown_00 || data->deadline_08)
		return false;
	if (cs_debug)
		pr_info("metis-cs: %s: mutex oem data is zero\n",
			"__mi_mutex_data_valid");
	return true;
}

/* Load/validate RWSEM payload. Initial tagging happens only when protect_cs
 * is nonzero at this later read; callers do not all gate on protect_cs. */
static bool rwsem_word_unknown(struct metis_rwsem_oem_unknown *data, u64 *word)
{
	if (!data)
		return false;
	*word = data->unknown_00;
	if (*word)
		return (*word >> 32) == 0xab9f19ab;
	if (data->deadline_08)
		return false;
	if (cs_debug)
		pr_info("metis-cs: %s: rwsem oem data is zero\n",
			"__mi_rwsem_data_valid");
	if (protect_cs) {
		data->deadline_08 = 0;
		*word = METIS_RWSEM_TAG_UNKNOWN;
	} else {
		*word = data->unknown_00;
	}
	return true;
}

/* Stock jump-table key +0x2b0 first gates on mi_rwsem_blocked_info;
 * only then does key +0x2c0 gate the systrace E. These are separate gates. */
static void rwsem_clear_trace_unknown(void)
{
	if (tracepoint_enabled(mi_rwsem_blocked_info)) {
		int tgid = current->tgid;

		if (tracepoint_enabled(metis_tracing_mark_write) &&
		    cpu_online(raw_smp_processor_id()))
			trace_metis_tracing_mark_write('E', tgid, "", 0, 0);
	}
}

/* Static-key tail omitted by Ghidra: it deliberately dereferences sem even
 * when the functional state path accepted a NULL sem. No lifetime fix added. */
static void rwsem_record_trace_unknown(struct rw_semaphore *sem, bool reader)
{
	struct task_struct *owner;
	unsigned long owner_word;
	char owner_type;
	char buffer[64] = { 0 };
	bool vip, spinnable;
	int pid;

	if (!tracepoint_enabled(mi_rwsem_blocked_info))
		return;
	owner_word = atomic_long_read(&sem->owner);
	owner = (struct task_struct *)(owner_word & ~3UL);
	owner_type = owner_word & 1 ? 'R' : owner ? 'W' : 'N';
	vip = mi_vip_task(current);
	if (!vip)
		vip = mi_link_vip_task(current);
	spinnable = !(owner_word & 2);
	pid = !(owner_word & 1) && owner ? owner->pid : 0;
	snprintf(buffer, 63,
		 "D: rwsem ownerpid %d type %c cur vip %d r %d sp %d %p",
		 pid, owner_type, vip, reader, spinnable, sem);
	/* Stock reloads owner pid after formatting, without taking a reference. */
	pid = !(owner_word & 1) && owner ? owner->pid : 0;
	if (tracepoint_enabled(mi_rwsem_blocked_info) &&
	    cpu_online(raw_smp_processor_id())) {
		trace_mi_rwsem_blocked_info(pid, owner_type, reader, spinnable, vip);
	}
	if (tracepoint_enabled(metis_tracing_mark_write) &&
	    cpu_online(raw_smp_processor_id()))
		trace_metis_tracing_mark_write('B', current->tgid, buffer, 0, 0);
}

void set_mutex_vip_blocked(struct mutex *lock)
{
	struct metis_mutex_oem_unknown *data = mutex_oem_attach(lock);

	if (!data || !mutex_zero_unknown(data))
		return;
	data->unknown_00 = protect_cs ? 0x149f194f : data->unknown_00 | 1;
	data->deadline_08 = jiffies + 7;
}

void record_mutex_blocked_info(struct task_struct *task, struct mutex *lock)
{
	struct mi_task_struct *state;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct mutex *, 0x2d8) = lock;
	MI_FIELD(state, u32, 0x2b4) |= 0x100;
	if (!mi_vip_task(task) && !mi_link_vip_task(task))
		return;
	MI_FIELD(state, u32, 0x2b4) |= 0x200;
	set_mutex_vip_blocked(lock);
}

void clear_mutex_blocked_info(struct task_struct *task, struct mutex *lock)
{
	struct mi_task_struct *state;
	struct metis_mutex_oem_unknown *data;
	u32 flags;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	flags = MI_FIELD(state, u32, 0x2b4);
	MI_FIELD(state, struct mutex *, 0x2d8) = NULL;
	MI_FIELD(state, u32, 0x2b4) = flags & ~0x100;
	if (!(flags & 0x200))
		return;
	MI_FIELD(state, u32, 0x2b4) = flags & ~0x300;
	data = mutex_oem_unknown(lock);
	if (!mutex_zero_unknown(data))
		return;
	data->unknown_00 = protect_cs ? 0x149f0000 :
					 data->unknown_00 & 0xffff0000;
	data->deadline_08 = 0;
	mutex_oem_clear(lock);
}

metis_mutex_blocked_result_unknown vip_blocked_on_mutex(struct mutex *lock)
{
	struct metis_mutex_oem_unknown *data = mutex_oem_unknown(lock);

	if (!mutex_zero_unknown(data))
		return 0;
	if (protect_cs) {
		data->deadline_08 = 0;
		data->unknown_00 = 0x149f194f;
	} else if (!(u16)data->unknown_00) {
		return 0;
	}
	/* SUB then LSR #63 in stock, with wrapping unsigned subtraction. */
	return ((u64)jiffies - data->deadline_08) >> 63;
}

void set_rwsem_vip_blocked(struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	if (!rwsem_word_unknown(data, &word))
		return;
	data->unknown_00 = (word & 0xffffffffffff0000ULL) | 1;
	data->deadline_08 = jiffies + 7;
}

void record_rwsem_blocked_info(struct task_struct *task,
			      struct rw_semaphore *sem,
			      metis_rwsem_reader_flag_unknown reader)
{
	struct mi_task_struct *state;
	bool vip;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct rw_semaphore *, 0x2d0) = sem;
	MI_FIELD(state, u32, 0x2b4) |= 0x40;
	vip = mi_vip_task(task);
	if (!vip)
		vip = mi_link_vip_task(task);
	if (sem && vip)
		set_rwsem_vip_blocked(sem);
	rwsem_record_trace_unknown(sem, reader);
}

void clear_rwsem_blocked_info(struct task_struct *task, struct rw_semaphore *sem)
{
	struct mi_task_struct *state;
	struct metis_rwsem_oem_unknown *data;
	u64 word;
	bool vip;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(task);
	if (!state)
		return;
	MI_FIELD(state, struct rw_semaphore *, 0x2d0) = NULL;
	MI_FIELD(state, u32, 0x2b4) &= ~0x40;
	vip = mi_vip_task(task);
	if (!vip)
		vip = mi_link_vip_task(task);
	if (!sem || !vip)
		goto out_trace;
	data = rwsem_oem_unknown(sem);
	word = data->unknown_00;
	if (!word) {
		if (data->deadline_08)
			goto out_trace;
		if (cs_debug)
			pr_info("metis-cs: %s: rwsem oem data is zero\n",
				"__mi_rwsem_data_valid");
	} else if ((word >> 32) != 0xab9f19ab) {
		goto out_trace;
	}
	/* Clear drops writer-owned and all other low bits, keeping only tag.
	 * Unlike rwsem_word_unknown, it does not recheck protect_cs here. */
	data->unknown_00 = METIS_RWSEM_TAG_UNKNOWN;
	data->deadline_08 = 0;
out_trace:
	rwsem_clear_trace_unknown();
}

void mi_record_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data;
	u64 word;

	/* Current task is checked before the sem NULL test, as in stock. */
	if (mi_task_exiting(current) || !sem || (current->flags & PF_EXITING))
		return;
	data = rwsem_oem_unknown(sem);
	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word | 0x10000;
}

void mi_clear_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word & ~0x10000ULL;
}

void mi_record_rwsem_reader_owned_hook(void *unused, struct rw_semaphore *sem,
				      struct list_head *wlist)
{
	struct metis_rwsem_oem_unknown *data = rwsem_oem_unknown(sem);
	u64 word;

	/* Stock ignores wlist and clears its writer-owned bit; no reader count. */
	if (rwsem_word_unknown(data, &word))
		data->unknown_00 = word & ~0x10000ULL;
}

void mi_mutex_wait_finish_hook(void *unused, struct mutex *lock)
{
	clear_mutex_blocked_info(current, lock);
}

void mi_rwsem_read_wait_finish_hook(void *unused, struct rw_semaphore *sem)
{
	clear_rwsem_blocked_info(current, sem);
}
