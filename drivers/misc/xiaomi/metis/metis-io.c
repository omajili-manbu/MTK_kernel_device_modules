// SPDX-License-Identifier: GPL-2.0-only
/* PARTIAL: four stock IO bodies; subsystem helpers are not invoked here. */
#include <linux/kallsyms.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include <linux/workqueue.h>
#include <trace/events/workqueue.h>
#include "metis_common.h"
#include "metis_io.h"

/* rodin 6.18：fortify_panic 宏改 5 参且无 1 参符号；stock 不可达越界守卫
 * 改走本地 BUG 陷阱，语义 = fortify 越界即停。 */
static __cold __noreturn void metis_fortify_trap(const char *func)
{
	pr_crit("metis-io: fortify trap %s\n", func);
	BUG();
}

#include "metis-final-trace.h"

void metis_io_schedule_hook(struct task_struct *task)
{
	struct mi_task_struct *state;

	if (!task)
		return;
	state = get_mi_task_struct(task);
	if (!state || !metis_vip_task(task) || !task->in_iowait)
		return;
	vip_iowait_time = (u64)(ktime_to_ns(ktime_get()) / 1000000);
	/* This producer deliberately has no metis_io_enabled gate. */
	if (trace_metis_iowait_status_update_enabled())
		trace_metis_iowait_status_update(task, MI_FIELD(state, u32, 0xc0));
}

void probe_workqueue_execute_start(void *unused, struct work_struct *work)
{
	char symbol[666];
	u64 elapsed;
	size_t length;
	int comparison;

	if (!work || !metis_io_enabled)
		return;
	memset(symbol, 0, sizeof(symbol));
	elapsed = (u64)(ktime_to_ns(ktime_get()) / 1000000) - vip_iowait_time;
	if (elapsed > 4)
		return;
	sprint_symbol(symbol, (unsigned long)work->func);
	length = strnlen(VERITY_WORK_NAME, sizeof(VERITY_WORK_NAME));
	if (length > sizeof(VERITY_WORK_NAME))
		metis_fortify_trap("strnlen");
	if (length == sizeof(VERITY_WORK_NAME))
		metis_fortify_trap("__fortify_strlen");
	comparison = strncmp(symbol, VERITY_WORK_NAME, length);
	if (!length || comparison)
		return;
	trace_metis_verity_work_start(current, elapsed, 4);
	set_mi_vip_task_common_noresched(current, 0x4000000UL, 1UL);
}

void metis_io_init(void)
{
	/* Original ignores tracepoint_probe_register's return value. */
	register_trace_workqueue_execute_start(probe_workqueue_execute_start, NULL);
}

void metis_io_exit(void)
{
	/* Stock .text+0x20408..0x2040c is a literal RET: no unregister. */
}
