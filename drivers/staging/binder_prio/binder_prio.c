// SPDX-License-Identifier: GPL-2.0
/*
 * binder_prio —— rodin =y 内建重建（T2①，#164）
 *
 * 基底 = dash-w-oss 官方源 drivers/staging/binder_prio（6.6 基，含 Liella 同源
 * 实证）。本文件按 rodin 设备 blob（vendor_dlkm/lib/modules/binder_prio.ko，
 * 15,656B）机器码逐指令对齐，与 dash 源的 5 处代差全部收口到 blob 真值：
 *   1. trace_binder_prio_proc_transaction_finish 为活调用（dash :203 注释）；
 *   2. 不注册 android_vh_binder_restore_priority（blob UND 面无该钩子）；
 *   3. 无 home_saved_priority / binder_prio_proc_default_prio 事件（blob 无
 *      8 字节全局、trans handler 无 trace 调用）；
 *   4. lunch_name_tid = 5 行（.rela.rodata 15 指针逐项重建，少 surfaceflinger 行）；
 *   5. 分配器名单 6 名（+ewhome:launcher/rsonalassistant）、cits_sensor_pid
 *      sysctl 第 4 项、cwb_dump/binder: 检查块（blob 独有）；dash 的
 *      surfaceflinger group_leader + passBlur 组在 blob 代不存在（rodata 无
 *      passBlur 串、机器码无对应比较块），不落。
 * 证据：blob .ko symtab+disasm（llvm-readelf/-objdump）；register_sysctl_sz
 * 立即数 5（4 真+终止）、loop bound 0x78=5 行、handler 尺寸 828/360/136B、
 * init/exit 164/124B。
 *
 * 6.18 适配（不触行为）：
 *   - kernel-6.6 相对路径 include 改 -I$(srctree)/drivers/android（mi_log 同法）；
 *   - dash 的 <kernel-6.6/kernel/sched/sched.h> 改 <sched.h> +
 *     -I$(srctree)/kernel/sched：fair_policy/rt_policy 的真身（6.18 在
 *     kernel/sched/sched.h:208/225，include/linux/sched/prio.h 无）；
 *   - __assign_str 单参化（6.18 stage6 契约）；
 *   - module_init -> vseq_module_init（T1 同款 VSEQ 化）。
 */
#include <linux/swap.h>
#include "linux/sysctl.h"
#include <linux/sysfs.h>
#include "linux/types.h"
#include <linux/module.h>
#include <linux/vseq.h>
#include <trace/hooks/binder.h>
#include <uapi/linux/android/binder.h>
#include <uapi/linux/sched/types.h>
#include <linux/sched/prio.h>
#include <linux/sched/cputime.h>
#include <sched.h>
#include <binder_internal.h>
#include <linux/string.h>

#define CREATE_TRACE_POINTS
#include "binder_prio_trace.h"

static struct ctl_table_header *sysctl_header;
static pid_t allocator_pid = 0;
static pid_t composer_pid = 0;
static pid_t arm_mali_pid = 0;
/* rodin blob 独有第 4 参（sysctl 表 320B=5 项、.rela.data 第 4 项实证） */
static pid_t cits_sensor_pid = 0;

static const char * const task_name[] = {
	"com.miui.home",
	"ndroid.systemui",
	"surfaceflinger",
	".globallauncher",
};

/* 分配器名单（rodin blob 6 名：机器码 0x4c4-0x55c 六连 strncmp；前 4 与
 * task_name 同序，尾部两位 rodin 特化；dash 代此名单为 4 名且另有
 * group_leader=="com.miui.home" 检查，blob 代无该检查） */
static const char * const proc_task_name[] = {
	"com.miui.home",
	"ndroid.systemui",
	"surfaceflinger",
	".globallauncher",
	"ewhome:launcher",
	"rsonalassistant",
};

static const char * const task_name_tid[] = {
	"wmshell.main",
	"ll.splashscreen",
};

/* rodin blob = 5 行（.rela.rodata 0x10-0x88 共 15 指针逐项重建；
 * dash 第 6 行 {surfaceflinger, surfaceflinger, com.miui.home} 不在 blob 代） */
static const char * const lunch_name_tid[][3] = {
        {"ndroid.systemui", "wmshell.main", "com.miui.home"},
        {"ndroid.systemui", "ll.splashscreen", "system_server"},
        {"system_server", "android.anim", "ndroid.systemui"},
        {"system_server", "binder:", "ndroid.systemui"},
        {"system_server", "binder:", "com.miui.home"},
};

static int to_userspace_prio(int policy, int kernel_priority) {
	if (fair_policy(policy))
		return PRIO_TO_NICE(kernel_priority);
	else
		return MAX_RT_PRIO - 1 - kernel_priority;
}

static inline bool taskname_in_list(char* name, const char * const list[], int size) {
	int i;
	for(i = 0; i < size; i++) {
		if (strncmp(name, list[i], strlen(list[i])) == 0) {
			return true;
		}
	}
	return false;
}

static bool set_binder_rt_allocator(struct binder_transaction *t, struct task_struct* traget) {

	if (unlikely(allocator_pid == 0)) return false;
	if (unlikely(t->from->proc == NULL || t->to_proc->tsk == NULL)) return false;
	if (likely(traget->group_leader->pid != allocator_pid)) return false;

	if(taskname_in_list(t->from->proc->tsk->comm, proc_task_name,
		sizeof(proc_task_name)/sizeof(proc_task_name[0]))) {
		return true;
	}

	if (composer_pid > 0 && t->from->proc->tsk->pid == composer_pid) return true;

	return false;
}

static bool set_binder_rt_task(struct binder_transaction *t, struct task_struct* traget) {
	if (unlikely(!t || !t->from || !t->from->task || !traget || traget->group_leader == NULL)) {
		return false;
	}

	if (t->flags & TF_ONE_WAY) {
		return false;
	}

	if (unlikely(set_binder_rt_allocator(t, traget))) {
		return true;
	}

	/* cits 块（rodin blob 独有，机器码 0x33c-0x5d4）：目标进程 tgid==composer_pid
	 * 且发送进程 pid==cits_sensor_pid、发送线程为相机 dump（cwb_dump 前缀）或
	 * binder: 线程时提升 RT */
	if (composer_pid && t->from->proc && t->to_proc->tsk && traget->tgid == composer_pid) {
		if (cits_sensor_pid > 0 && t->from->proc->tsk->pid == cits_sensor_pid &&
			(strncmp(t->from->task->comm, "cwb_dump", strlen("cwb_dump")) == 0 ||
			 strstr(t->from->task->comm, "binder:") != NULL)) {
			return true;
		}
	}

	if (!rt_policy(t->from->task->policy)) {
		return false;
	}

	if (traget->pid == traget->tgid) {
		if (strncmp(traget->comm, task_name[3], strlen(task_name[3])) == 0) {
			return true;
		}
		if (arm_mali_pid > 0 && traget->pid == arm_mali_pid) {
			return true;
		}
	}

	if (t->from->task->pid == t->from->task->tgid) {
		if(taskname_in_list(t->from->task->comm, task_name, sizeof(task_name)/sizeof(task_name[0]))) {
			return true;
		}
	} else {
		if(taskname_in_list(t->from->task->comm, task_name_tid, sizeof(task_name_tid)/sizeof(task_name_tid[0]))) {
			return true;
		}
	}
	return false;
}

static void extend_surfacefinger_binder_set_priority_handler(void *data, struct binder_transaction *t, struct task_struct *task) {
	struct sched_param params;
	struct binder_priority desired;
	unsigned int policy;
	struct binder_node *target_node = t->buffer->target_node;

	desired.prio = target_node->min_priority;
	desired.sched_policy = target_node->sched_policy;
	policy = desired.sched_policy;
	if (set_binder_rt_task(t, task)) {
		desired.sched_policy = SCHED_FIFO;
		desired.prio = 98;
		policy = desired.sched_policy;
	}
	if (rt_policy(policy) && task->policy != policy) {
		params.sched_priority = to_userspace_prio(policy, desired.prio);
		sched_setscheduler_nocheck(task, policy | SCHED_RESET_ON_FORK, &params);
	}
}

static void extend_surfacefinger_binder_trans_handler(void *data, struct binder_proc *target_proc,
    struct binder_proc *proc, struct binder_thread *thread, struct binder_transaction_data *tr) {
	if (target_proc && target_proc->tsk) {
		if (strncmp(target_proc->tsk->comm, "surfaceflinger", strlen("surfaceflinger")) == 0) {
			if (thread && proc && tr && thread->transaction_stack
				&& (!(thread->transaction_stack->flags & TF_ONE_WAY))) {
				target_proc->default_priority.sched_policy = SCHED_FIFO;
				target_proc->default_priority.prio = 98;
			}
		}
	}
}

static void extend_android_vh_binder_proc_transaction_finish(void *data, struct binder_proc *proc, struct binder_transaction *t,
                     struct task_struct *binder_th_task, bool pending_async, bool sync) {
	struct task_struct* call = current;

	unsigned int policy = call->policy;
	struct sched_param param = {};

	if (likely(sync || !rt_policy(call->policy))) return;
	if (unlikely(binder_th_task == NULL || call->group_leader == NULL)) return;
	if (unlikely(proc == NULL || proc->tsk == NULL)) return;

	for (int i = 0; i < sizeof(lunch_name_tid)/sizeof(lunch_name_tid[0]); i ++) {
		if (unlikely(strcmp(lunch_name_tid[i][0], call->group_leader->comm) == 0
			&& (strcmp(lunch_name_tid[i][1], call->comm) == 0 || strstr(call->comm, lunch_name_tid[i][1]) != NULL)
			&& strcmp(lunch_name_tid[i][2], proc->tsk->comm) == 0)) {

			param.sched_priority = to_userspace_prio((int) policy, call->prio);
			if (policy != binder_th_task->policy || param.sched_priority != binder_th_task->prio) {
				trace_binder_prio_proc_transaction_finish(binder_th_task->comm);
				sched_setscheduler_nocheck(binder_th_task, SCHED_FIFO | SCHED_RESET_ON_FORK, &param);
			}
		}
	}
}

struct ctl_table binder_prio_table[] = {
	{
		.procname       = "gallocator_pid",
		.data           = &allocator_pid,
		.maxlen         = sizeof(pid_t),
		.mode           = 0666,
		.proc_handler   = proc_dointvec,
		.extra1         = SYSCTL_ZERO,
	},
	{
		.procname       = "composer_pid",
		.data           = &composer_pid,
		.maxlen         = sizeof(pid_t),
		.mode           = 0666,
		.proc_handler   = proc_dointvec,
		.extra1         = SYSCTL_ZERO,
	},
	{
		.procname       = "arm_mali_pid",
		.data           = &arm_mali_pid,
		.maxlen         = sizeof(pid_t),
		.mode           = 0666,
		.proc_handler   = proc_dointvec,
		.extra1         = SYSCTL_ZERO,
	},
	{
		.procname       = "cits_sensor_pid",
		.data           = &cits_sensor_pid,
		.maxlen         = sizeof(pid_t),
		.mode           = 0666,
		.proc_handler   = proc_dointvec,
		.extra1         = SYSCTL_ZERO,
	},
	{ },
};

int __init binder_prio_init(void)
{
	pr_info("binder_prio: module init!");
	register_trace_android_vh_binder_set_priority(extend_surfacefinger_binder_set_priority_handler, NULL);
	register_trace_android_vh_binder_trans(extend_surfacefinger_binder_trans_handler, NULL);

	register_trace_android_vh_binder_proc_transaction_finish(extend_android_vh_binder_proc_transaction_finish, NULL);

	sysctl_header = register_sysctl("binder_prio", binder_prio_table);
	if (!sysctl_header) {
		pr_err("binder_prio: register_sysctl_table failed\n");
	}
	return 0;
}

void __exit binder_prio_exit(void)
{
	unregister_trace_android_vh_binder_set_priority(extend_surfacefinger_binder_set_priority_handler, NULL);
	unregister_trace_android_vh_binder_trans(extend_surfacefinger_binder_trans_handler, NULL);

	unregister_trace_android_vh_binder_proc_transaction_finish(extend_android_vh_binder_proc_transaction_finish, NULL);

	if (sysctl_header) {
		unregister_sysctl_table(sysctl_header);
	}

	pr_info("binder_prio: module exit!");
}

vseq_module_init(binder_prio_init);
module_exit(binder_prio_exit);
MODULE_LICENSE("GPL");
