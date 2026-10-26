// SPDX-License-Identifier: GPL-2.0-only
/* Stock METIS cold-start spin control. Native API/CFI/layout/runtime UNTESTED. */
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/sched.h>
#include "../xr_qi/qi_provider.h"
#include "metis_blocked.h"
#include "metis_coldstart.h"
#include "metis_common.h"
#include "metis-final-trace.h"
#include "metis-final-link.h"
#include "metis-final-link-trace.h"

/* Existing metis_blocked.h records these as Xiaomi OEM payloads, never native
 * mutex/rwsem layouts. Validation quirks match stock and are not repaired. */
/* rodin 6.18：mutex 16B 载体改走 metis-blocked.c 的模块级侧表（查无即 NULL） */
struct metis_mutex_oem_unknown *metis_mutex_side_lookup(struct mutex *lock);
struct metis_mutex_oem_unknown *mutex_oem_attach(struct mutex *lock);
struct metis_mutex_oem_unknown *mutex_oem_clear(struct mutex *lock);
static struct metis_mutex_oem_unknown *spin_mutex_data(struct mutex *lock)
{
	return metis_mutex_side_lookup(lock);
}

static struct metis_rwsem_oem_unknown *spin_rwsem_data(struct rw_semaphore *sem)
{
	return sem ? (void *)sem->android_oem_data1 : NULL;
}

static bool spin_mutex_valid(struct metis_mutex_oem_unknown *data)
{
	if (!data || data->unknown_00 || data->deadline_08)
		return false;
	if (cs_debug)
		pr_info("metis-cs: %s: mutex oem data is zero\n", "__mi_mutex_data_valid");
	return true;
}

static bool spin_rwsem_valid(struct metis_rwsem_oem_unknown *data, u64 *word)
{
	if (!data)
		return false;
	*word = data->unknown_00;
	if (*word)
		return (*word >> 32) == 0xab9f19ab;
	if (data->deadline_08)
		return false;
	if (cs_debug)
		pr_info("metis-cs: %s: rwsem oem data is zero\n", "__mi_rwsem_data_valid");
	if (protect_cs) {
		data->unknown_00 = 0xab9f19ab00000000ULL;
		data->deadline_08 = 0;
		*word = data->unknown_00;
	} else {
		*word = data->unknown_00;
	}
	return true;
}

static void spin_rwsem_reset(struct metis_rwsem_oem_unknown *data)
{
	data->unknown_00 = 0xab9f19ab00000000ULL;
	data->deadline_08 = 0;
}

/* .text+0x1ae88, virtual 001238b0. The first write is 32 bits, but resetting
 * an empty wait_list writes all 64 bits to a different word value. */
void mutex_reset_data_no_waiter(struct mutex *lock)
{
	struct metis_mutex_oem_unknown *data = spin_mutex_data(lock);

	if (!spin_mutex_valid(data))
		return;
	if (protect_cs) {
		data->deadline_08 = 0;
		data->unknown_00 = 0x149f194f;
	}
	if (list_empty(&lock->wait_list)) {
		data->unknown_00 = 0;
		data->unknown_04 = 0x149f194f;
		data->deadline_08 = 0;
	}
}

/* .text+0x1bd24, virtual 0012474c; native dtask.h fixes unused probe data. */
void mi_mutex_can_spin_on_owner(void *unused, struct mutex *lock, int *ret)
{
	struct metis_mutex_oem_unknown *data;
	unsigned long deadline;

	if (!protect_cs || ((is_qi_feat_enabled(9) & 1) && is_qos_task(current)))
		return;
	if (!*ret || !lock || current->prio < 100)
		return;
	data = spin_mutex_data(lock);
	if (!spin_mutex_valid(data))
		return;
	if (protect_cs) {
		data->deadline_08 = 0;
		data->unknown_00 = 0x149f194f;
		deadline = 0;
	} else {
		if (!(data->unknown_00 & 0xffff))
			return;
		deadline = data->deadline_08;
	}
	if ((s64)(jiffies - deadline) < 0)
		*ret = 0;
}

/* .text+0x1c294, virtual 00124cbc. No lock NULL check exists in stock. */
void mi_mutex_init(void *unused, struct mutex *lock)
{
	struct metis_mutex_oem_unknown *data;

	if (!protect_cs)
		return;
	/* rodin 6.18 侧表形态：init 时点 = 锁全新未入表，stock 内嵌槽语义
	 * = 对新锁预置 payload ⇒ attach（查无即建）而非查无即 NULL。 */
	data = mutex_oem_attach(lock);
	if (!data)
		return;
	data->deadline_08 = 0;
	data->unknown_00 = 0x149f194f;
}

/* rodin：退出任务侧表槽回收（169 轮挂账闭合）。futex_exit/perf_event 两 mutex
 * 是侧表仅有的每任务条目源；android_vh_lock_task_exit 在 do_exit 早期
 * （task_struct 未释放、地址未复用）触发，此刻清槽回 freelist，表规模
 * 有界于活任务数（对齐 stock 内嵌槽随对象消亡；条目永不 kfree，锁外
 * 持 payload 指针无 UAF）。 */
static void mi_mutex_task_exit(void *unused, struct task_struct *task)
{
	if (!protect_cs)
		return;
	mutex_oem_clear(&task->futex_exit_mutex);
	mutex_oem_clear(&task->perf_event_mutex);
}

/* .text+0x1bbf0, virtual 00124618; native hook proves ignored lock/taken. */
void mi_mutex_opt_spin_finish(void *unused, struct mutex *lock, bool taken)
{
	struct mi_task_struct *state;

	if (!protect_cs || ((is_qi_feat_enabled(9) & 1) && is_qos_task(current)))
		return;
	state = get_mi_task_struct(current);
	if (!state || !MI_FIELD(state, u64, 0x2b8))
		return;
	/* Both the duration work and emission have separate original static
 * keys. The first predicate calls may expire and clear Xiaomi VIP state. */
	if (trace_lock_spin_dur_enabled()) {
		u64 elapsed = sched_clock();
		bool vip;

		elapsed -= READ_ONCE(MI_FIELD(state, u64, 0x2b8));
		vip = mi_vip_task(current) || mi_link_vip_task(current);

		trace_lock_spin_dur(current, taken, elapsed, "mutex", vip);
	}
	MI_FIELD(state, u64, 0x2b8) = 0;
}

/* .text+0x1ba84, virtual 001244ac; stock treats native signed count as wrapping
 * W arithmetic. Count zero stores clock and returns without incrementing it. */
void mi_mutex_opt_spin_start(void *unused, struct mutex *lock, bool *time_out,
		int *count)
{
	struct mi_task_struct *state;
	struct metis_mutex_oem_unknown *data;
	unsigned long deadline;
	u64 elapsed, start;
	s32 threshold;
	u32 next;

	if (!protect_cs || ((is_qi_feat_enabled(9) & 1) && is_qos_task(current)))
		return;
	state = get_mi_task_struct(current);
	if (!state)
		return;
	data = spin_mutex_data(lock);
	if (spin_mutex_valid(data)) {
		if (protect_cs) {
			data->deadline_08 = 0;
			data->unknown_00 = 0x149f194f;
			deadline = 0;
		} else {
			if (!(data->unknown_00 & 0xffff))
				goto count_spin;
			deadline = data->deadline_08;
		}
		if ((s64)(jiffies - deadline) < 0)
			goto timeout;
	}
count_spin:
	if (!*count) {
		MI_FIELD(state, u64, 0x2b8) = sched_clock();
		return;
	}
	next = (u32)*count + 1U;
	*count = (s32)next;
	if (next & 15U)
		return;
	start = MI_FIELD(state, u64, 0x2b8);
	if (!start)
		return;
	elapsed = sched_clock();
	elapsed -= READ_ONCE(MI_FIELD(state, u64, 0x2b8));
	threshold = mi_vip_task(current) || mi_link_vip_task(current) ?
		mutex_vip_spin_thres : mutex_normal_spin_thres;
	if (elapsed <= (u64)(s64)threshold)
		return;
timeout:
	*time_out = true;
}

/* .text+0x1c0b8, virtual 00124ae0. OEM validation/reset occurs before current
 * app/MM classification; tagged expiry resets even with concurrent disable. */
void mi_rwsem_can_spin_on_owner(void *unused, struct rw_semaphore *sem, bool *ret)
{
	struct metis_rwsem_oem_unknown *data;
	u64 word;
	s32 active = protect_cs;

	if (!active || !*ret || current->prio < 100)
		return;
	data = spin_rwsem_data(sem);
	if (!data) {
		active = 1;
	} else {
		word = data->unknown_00;
		if (!word) {
			if (data->deadline_08) {
				active = 1;
				goto app_check;
			}
			/* Unlike opt_spin_start, the raw can-spin body retains its
 * entry enable value unless its debug printk forces a fresh load. */
			if (cs_debug) {
				pr_info("metis-cs: %s: rwsem oem data is zero\n",
					"__mi_rwsem_data_valid");
				active = protect_cs;
				if (!active) {
					word = data->unknown_00;
					goto flags_check;
				}
			}
			spin_rwsem_reset(data);
			goto app_check;
		}
		if ((word >> 32) != 0xab9f19ab) {
			active = 1;
			goto app_check;
		}
flags_check:
		if (word & 0xffff) {
			if ((s64)(jiffies - data->deadline_08) < 0)
				goto no_spin;
			if ((word >> 32) == 0xab9f19ab)
				spin_rwsem_reset(data);
		}
	}
app_check:
	if (current->tgid != cold_start_app_tgid || !current->tgid || !active)
		return;
	if ((s64)(coldstart_end_jif - coldstart_begin_jif) >= 0 ||
	    (s64)(jiffies - coldstart_begin_jif - 1000UL) >= 0) {
		cold_start_sserver_tgid = 0;
		cold_start_app_tgid = 0;
		home_tgid = 0;
		systemui_tgid = 0;
		systemserver_tgid = 0;
		sf_tgid = 0;
		cold_start_app_mmlock = NULL;
		cold_start_sserver_mmlock = NULL;
		home_mmlock = NULL;
		systemui_mmlock = NULL;
		systemserver_mmlock = NULL;
		sf_mmlock = NULL;
		return;
	}
	if (!current->mm || &current->mm->mmap_lock != sem ||
	    mi_vip_task(current) || mi_link_vip_task(current))
		return;
no_spin:
	*ret = false;
}

/* The stock rwsem direct-rsteal and optimistic-rspin overrides re-walked the
 * native rwsem fast path (owner spin loop, sem->count/owner/osq hand-edits);
 * 6.18 owns that path, so they are dropped instead of being kept as
 * registered no-op placeholders. */

/* .text+0x1c2b8, virtual 00124ce0. */
void mi_rwsem_init(void *unused, struct rw_semaphore *sem)
{
	if (protect_cs)
		spin_rwsem_reset(spin_rwsem_data(sem));
}

/* .text+0x1bf9c, virtual 001249c4; native dtask.h proves lock/taken formals. */
void mi_rwsem_opt_spin_finish(void *unused, struct rw_semaphore *sem, bool taken)
{
	struct mi_task_struct *state;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(current);
	if (!state || !MI_FIELD(state, u64, 0x2b8))
		return;
	if (trace_lock_spin_dur_enabled()) {
		u64 elapsed = sched_clock();
		bool vip;

		elapsed -= READ_ONCE(MI_FIELD(state, u64, 0x2b8));
		vip = mi_vip_task(current) || mi_link_vip_task(current);

		trace_lock_spin_dur(current, taken, elapsed, "rwsem", vip);
	}
	MI_FIELD(state, u64, 0x2b8) = 0;
}

/* .text+0x1be10, virtual 00124838. */
void mi_rwsem_opt_spin_start(void *unused, struct rw_semaphore *sem,
		bool *time_out, int *count, bool check_only)
{
	struct mi_task_struct *state;
	struct metis_rwsem_oem_unknown *data;
	u64 word, start, elapsed;
	u32 next;
	s32 threshold;

	if (!protect_cs)
		return;
	state = get_mi_task_struct(current);
	if (!state)
		return;
	data = spin_rwsem_data(sem);
	if (spin_rwsem_valid(data, &word) && (word & 0xffff)) {
		if ((s64)(jiffies - data->deadline_08) < 0)
			goto timeout;
		if ((word >> 32) == 0xab9f19ab)
			spin_rwsem_reset(data);
	}
	if (!*count) {
		if (!check_only)
			MI_FIELD(state, u64, 0x2b8) = sched_clock();
		return;
	}
	next = (u32)*count + 1U;
	*count = (s32)next;
	if (next & 15U)
		return;
	start = MI_FIELD(state, u64, 0x2b8);
	if (!start)
		return;
	elapsed = sched_clock();
	elapsed -= READ_ONCE(MI_FIELD(state, u64, 0x2b8));
	threshold = mi_vip_task(current) || mi_link_vip_task(current) ?
		rwsem_vip_spin_thres : rwsem_normal_spin_thres;
	if (elapsed <= (u64)(s64)threshold)
		return;
timeout:
	*time_out = true;
}

/* .text+0x1b930, virtual 00124358; typed native wrappers preserve call order
 * and ignored errors. No cast of a mismatched callback is introduced. */
void register_cs_hooks(void)
{
	register_trace_android_vh_mutex_opt_spin_start(mi_mutex_opt_spin_start, NULL);
	register_trace_android_vh_mutex_opt_spin_finish(mi_mutex_opt_spin_finish, NULL);
	register_trace_android_vh_mutex_can_spin_on_owner(mi_mutex_can_spin_on_owner, NULL);
	register_trace_android_vh_rwsem_opt_spin_start(mi_rwsem_opt_spin_start, NULL);
	register_trace_android_vh_rwsem_opt_spin_finish(mi_rwsem_opt_spin_finish, NULL);
	register_trace_android_vh_rwsem_can_spin_on_owner(mi_rwsem_can_spin_on_owner, NULL);
	register_trace_android_vh_mutex_init(mi_mutex_init, NULL);
	register_trace_android_vh_lock_task_exit(mi_mutex_task_exit, NULL);
	register_trace_android_vh_rwsem_init(mi_rwsem_init, NULL);
	register_trace_android_vh_record_rwsem_writer_owned(mi_record_rwsem_writer_owned_hook, NULL);
	register_trace_android_vh_clear_rwsem_writer_owned(mi_clear_rwsem_writer_owned_hook, NULL);
	register_trace_android_vh_record_rwsem_reader_owned(mi_record_rwsem_reader_owned_hook, NULL);
}

/* .text+0x1c8ec, virtual 00125314. No tracepoint_synchronize_unregister. */
void unregister_cs_hooks(void)
{
	unregister_trace_android_vh_mutex_opt_spin_start(mi_mutex_opt_spin_start, NULL);
	unregister_trace_android_vh_mutex_opt_spin_finish(mi_mutex_opt_spin_finish, NULL);
	unregister_trace_android_vh_mutex_can_spin_on_owner(mi_mutex_can_spin_on_owner, NULL);
	unregister_trace_android_vh_rwsem_opt_spin_start(mi_rwsem_opt_spin_start, NULL);
	unregister_trace_android_vh_rwsem_opt_spin_finish(mi_rwsem_opt_spin_finish, NULL);
	unregister_trace_android_vh_rwsem_can_spin_on_owner(mi_rwsem_can_spin_on_owner, NULL);
	unregister_trace_android_vh_mutex_init(mi_mutex_init, NULL);
	unregister_trace_android_vh_lock_task_exit(mi_mutex_task_exit, NULL);
	unregister_trace_android_vh_rwsem_init(mi_rwsem_init, NULL);
	unregister_trace_android_vh_record_rwsem_writer_owned(mi_record_rwsem_writer_owned_hook, NULL);
	unregister_trace_android_vh_clear_rwsem_writer_owned(mi_clear_rwsem_writer_owned_hook, NULL);
	unregister_trace_android_vh_record_rwsem_reader_owned(mi_record_rwsem_reader_owned_hook, NULL);
}

/* .text+0x1dd0c, virtual 00126734. */
int metis_cs_init(void)
{
	register_cs_hooks();
	pr_info("metis-cs: cs init!!\n");
	return 0;
}

/* .text+0x1dd54, virtual 0012677c. Cold-start RT hook is not unregistered. */
void metis_cs_exit(void)
{
	unregister_cs_hooks();
}

