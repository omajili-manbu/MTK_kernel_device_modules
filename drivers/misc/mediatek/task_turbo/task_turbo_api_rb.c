// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Copyright (c) 2026 rodin project.
 *
 * rodin 6.18（2026-10-31）：本文件是 task_turbo 算法层 blob
 * （MTK_kernel_modules/task_turbo_cus/src/task_turbo_api_no_cfi.o_shipped,
 * 31KB, 10 函数）的全量逆向重建。基准 = llvm-objdump -dr 逐函数反汇编
 * 对账（E:\tools\_turbo_rebuild_20261031\api_no_cfi.dis），接口签名以
 * task_turbo_api.h 为真值；数据布局以 .rela.data 4 条 ABS64 自指重定位
 * 定标（wi_head/cur_wi_head 为 24B 哨兵容器，list 内嵌 @+0x8）。
 *
 * 背景：本算法层在 6.18 内核从未链接（blob 走 kmod obj-m 死形态），
 * task_turbo.c 的 tt_vip hook 指针恒 NULL —— update_win_pid_status /
 * tt_vip_algo / set_tdtgd 等通道全部假绿（retval=0 但什么都没做）。
 * 本重建 + task_turbo_hook_rb.c 接线使该链路在 6.18 首次活体。
 *
 * 与 blob 的语义逐位对齐（2026-10-31 曾注入 uclamp.min，实测无效果已撤除；
 * 本文件 = 纯 blob 等价重建 + 6.18 语义对齐，无新增功能）。
 *
 * ⚠ 内核换代漂移警告（LTS merge / 6.19+ 必读）：
 *  1) 对账基准是 6.6 blob 机器码。上游 list_debug/kmalloc_trace/trace
 *     展开一旦变化，重建体机器码即漂移——merge 后必须重跑对账
 *     （工具与判据在 tools/_turbo_rebuild_20261031/plan.md §门禁）。
 *  2) blob 含 __list_del_entry_valid_or_report/__list_add_valid_or_report
 *     调用 = 6.6 编译配置 CONFIG_DEBUG_LIST=y 的产物；6.18 树
 *     CONFIG_DEBUG_LIST=n，本文件 list_del/list_add_tail 展开无该调用，
 *     属已知已归因差异（正常路径语义等价，坏链检测退化为无）。
 *  4) turn_on/off_tgid_vip 等外部动作为本树 sched/eas 侧符号，其 6.18
 *     语义变化会整体改变本模块效果，换内核时一并复核。
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include "task_turbo_api.h"

/* .data 布局对齐 blob：0x0/0x18 哨兵、0x30/0x34 cam tgid 初值 -1。
 * struct win_info 与 hook 指针本体见 task_turbo_api.h / task_turbo.c，
 * 本文件经 extern 引用，不重复定义。 */
struct win_info wi_head = { .list = LIST_HEAD_INIT(wi_head.list) };
struct win_info cur_wi_head = { .list = LIST_HEAD_INIT(cur_wi_head.list) };
unsigned long win_info_size;
unsigned long cur_win_info_size;
bool cam_3rd_hal_vip_status;
bool cam_3rd_svr_vip_status;
bool touch_ssid_vip_status;
bool touch_sui_vip_status;
bool sf_vip_status;
bool win_vip_status;
int cur_inputDispatcher_tgid;
int cur_systemui_tgid;
int cur_surfaceFlinger_tgid;
int cam_hal_tgid = -1;
int cam_svr_tgid = -1;

/* binder 继承算法层实现：活体 task_turbo.c 已有同名不同签名（bool +
 * task_struct* 钩子入口），blob 形态的 int 接口改名 tt_ 前缀避免撞名。 */
void tt_binder_start_vip_inherit(int to_pid, int inherited_vip_prio);
void tt_binder_stop_vip_inherit(int pid, int inherited_vip_prio);

/*
 * 把 cur_wi_head 快照重建为 wi_head 目标表（快照语义）。
 * blob 中 kmalloc(24, GFP_KERNEL=0xcc0)；失败直接返回（快照保持部分更新，
 * 与 blob 行为一致，勿"修复"）。
 */
static void update_cur_win_info(struct win_info *cur, struct win_info *target)
{
	struct win_info *pos, *n;

	list_for_each_entry_safe(pos, n, &cur->list, list) {
		list_del(&pos->list);
		kfree(pos);
	}
	list_for_each_entry(pos, &target->list, list) {
		struct win_info *wi = kmalloc(sizeof(*wi), GFP_KERNEL);

		if (!wi)
			return;
		wi->tgid = pos->tgid;
		INIT_LIST_HEAD(&wi->list);
		list_add_tail(&wi->list, &cur->list);
	}
	cur_win_info_size = win_info_size;
}

int wi_add_tgid(int tgid)
{
	struct win_info *wi = kmalloc(sizeof(*wi), GFP_KERNEL);

	if (!wi)
		return -ENOMEM;
	wi->tgid = tgid;
	INIT_LIST_HEAD(&wi->list);
	list_add_tail(&wi->list, &wi_head.list);
	win_info_size++;
	return 0;
}

int wi_del_tgid(int tgid)
{
	struct win_info *pos, *n;

	list_for_each_entry_safe(pos, n, &wi_head.list, list) {
		if (pos->tgid != tgid)
			continue;
		unset_task_basic_vip(tgid);
		unset_tgid_vip(pos->tgid);
		exp_trace_turbo_vip("delete win: window unset_tgid_vip:",
				    pos->tgid);
		list_del(&pos->list);
		kfree(pos);
		win_info_size--;
		update_cur_win_info(&cur_wi_head, &wi_head);
		return 0;
	}
	return -3;
}

/*
 * tt_vip 状态机（blob 0x2d4-0x8e0）。段注释的 0xNNN 为 blob 内偏移，
 * 供机器码对账定位。
 */
int tt_vip_algo(int ct_vip_qualified, int inputDispatcher_tgid,
		int systemui_tgid, int surfaceFlinger_tgid, bool touching)
{
	int cam_status = get_cam_status_for_task_turbo();
	int turn_off_done = 0;
	int changed;
	int have_targets;
	struct win_info *pos, *n;

	/* 0x30c-0x394：do-work 门 */
	if (!cam_3rd_hal_vip_status && !cam_3rd_svr_vip_status &&
	    !touch_ssid_vip_status && !touch_sui_vip_status &&
	    !sf_vip_status) {
		if (ct_vip_qualified == 0) {
			/* 0x8bc：无 vip 活体 */
			if (win_vip_status != 1)
				return 0;
			if (list_empty(&cur_wi_head.list))
				return 0;
		} else {
			/* 0x354-0x394 */
			if (list_empty(&wi_head.list)) {
				if (list_empty(&cur_wi_head.list))
					return 0;
				if (win_vip_status != 1)
					return 0;
			}
		}
	}

	/* 0x398-0x3ec：turn_off 判定 */
	if (ct_vip_qualified == 0 && cur_win_info_size &&
	    win_vip_status == 1) {
		turn_off_tgid_vip();
		turn_off_done = 1;
	}

	/* 0x3f0-0x444：changed = 计数差或逐 tgid 双链对比 */
	if (win_info_size != cur_win_info_size) {
		changed = 1;
	} else {
		struct win_info *a, *b;

		changed = 0;
		for (a = list_first_entry_or_null(&wi_head.list,
						  struct win_info, list),
		    b = list_first_entry_or_null(&cur_wi_head.list,
						 struct win_info, list);
		     a && b;
		     a = list_is_last(&a->list, &wi_head.list) ?
				 NULL :
				 list_next_entry(a, list),
		    b = list_is_last(&b->list, &cur_wi_head.list) ?
				 NULL :
				 list_next_entry(b, list)) {
			if (a->tgid != b->tgid) {
				changed = 1;
				break;
			}
		}
	}

	/* 0x448-0x454 */
	have_targets = (ct_vip_qualified != 0) && (win_info_size != 0);

	/* 0x454-0x4b4：win 清场 */
	if (turn_off_done || (cur_win_info_size && changed &&
			      win_vip_status)) {
		win_vip_status = false;
		list_for_each_entry_safe(pos, n, &cur_wi_head.list, list) {
			unset_task_basic_vip(pos->tgid);
			unset_tgid_vip(pos->tgid);
			exp_trace_turbo_vip(
				"ct_vip_unqualified: window unset_tgid_vip:",
				pos->tgid);
		}
	}

	/* 0x4b8-0x504：cam_hal 清场 */
	if (cam_3rd_hal_vip_status && (turn_off_done || !cam_status)) {
		cam_3rd_hal_vip_status = false;
		if (cam_hal_tgid >= 1) {
			unset_task_basic_vip(cam_hal_tgid);
			unset_tgid_vip(cam_hal_tgid);
			exp_trace_turbo_vip(
				"ct_vip_unqualified: cam_hal unset_tgid_vip:",
				cam_hal_tgid);
			cam_hal_tgid = -1;
		}
	}

	/* 0x508-0x54c：cam_svr 清场 */
	if (cam_3rd_svr_vip_status && (turn_off_done || !cam_status)) {
		cam_3rd_svr_vip_status = false;
		if (cam_svr_tgid >= 1) {
			unset_task_basic_vip(cam_svr_tgid);
			unset_tgid_vip(cam_svr_tgid);
			exp_trace_turbo_vip(
				"ct_vip_unqualified: cam_server unset_tgid_vip:",
				cam_svr_tgid);
			cam_svr_tgid = -1;
		}
	}

	/* 0x550-0x59c：touch ssid 清场（缓存值与参数一致且仍在触摸则保持） */
	if (touch_ssid_vip_status &&
	    (turn_off_done || !touching ||
	     cur_inputDispatcher_tgid != inputDispatcher_tgid)) {
		touch_ssid_vip_status = false;
		unset_task_basic_vip(cur_inputDispatcher_tgid);
		unset_tgid_vip(cur_inputDispatcher_tgid);
		exp_trace_turbo_vip(
			"ct_vip_unqualified: ssid unset_tgid_vip:",
			cur_inputDispatcher_tgid);
	}

	/* 0x5a0-0x5ec：touch sui 清场 */
	if (touch_sui_vip_status &&
	    (turn_off_done || !touching ||
	     cur_systemui_tgid != systemui_tgid)) {
		touch_sui_vip_status = false;
		unset_task_basic_vip(cur_systemui_tgid);
		unset_tgid_vip(cur_systemui_tgid);
		exp_trace_turbo_vip(
			"ct_vip_unqualified: sui unset_tgid_vip:",
			cur_systemui_tgid);
	}

	/* 0x5f0-0x63c：sf 清场 */
	if (sf_vip_status &&
	    (turn_off_done ||
	     cur_surfaceFlinger_tgid != surfaceFlinger_tgid)) {
		sf_vip_status = false;
		unset_task_basic_vip(cur_surfaceFlinger_tgid);
		unset_tgid_vip(cur_surfaceFlinger_tgid);
		exp_trace_turbo_vip(
			"ct_vip_unqualified: sf unset_tgid_vip:",
			cur_surfaceFlinger_tgid);
	}

	/* 0x63c-0x6d4：目标表 set 段 */
	if (have_targets && (changed || win_vip_status != 1)) {
		win_vip_status = true;
		list_for_each_entry_safe(pos, n, &wi_head.list, list) {
			int ret = set_tgid_vip(pos->tgid);

			if (!ret) {
				set_task_basic_vip(pos->tgid);
				turn_on_tgid_vip();
				exp_trace_turbo_vip(
					"ct_vip_qualified: window set_tgid_vip:",
					pos->tgid);
			} else {
				exp_trace_turbo_vip(
					"TGID NOT FOUND when set_tgid_vip:",
					pos->tgid);
				wi_del_tgid(pos->tgid);
			}
		}
	}

	/* 0x6d8-0x788：cam_hal/cam_svr set 段 */
	if (cam_status) {
		if (cam_hal_tgid <= 0)
			cam_hal_tgid = get_cam_hal_pid_for_task_turbo();
		if ((unsigned int)(cam_hal_tgid - 1) < 0x8000 &&
		    !cam_3rd_hal_vip_status) {
			cam_3rd_hal_vip_status = true;
			set_task_basic_vip(cam_hal_tgid);
			set_tgid_vip(cam_hal_tgid);
			turn_on_tgid_vip();
			exp_trace_turbo_vip(
				"ct_vip_qualified: cam_hal set_tgid_vip:",
				cam_hal_tgid);
		}
		if (cam_svr_tgid <= 0)
			cam_svr_tgid = get_cam_server_pid_for_task_turbo();
		if ((unsigned int)(cam_svr_tgid - 1) < 0x8000 &&
		    !cam_3rd_svr_vip_status) {
			cam_3rd_svr_vip_status = true;
			set_task_basic_vip(cam_svr_tgid);
			set_tgid_vip(cam_svr_tgid);
			turn_on_tgid_vip();
			exp_trace_turbo_vip(
				"ct_vip_qualified: cam_server set_tgid_vip:",
				cam_svr_tgid);
		}
	}

	/* 0x788-0x7d8：touching → inputDispatcher(ssid) set */
	if (touching && inputDispatcher_tgid >= 1 &&
	    !touch_ssid_vip_status) {
		touch_ssid_vip_status = true;
		set_task_basic_vip(inputDispatcher_tgid);
		set_tgid_vip(inputDispatcher_tgid);
		turn_on_tgid_vip();
		exp_trace_turbo_vip(
			"ct_vip_qualified: ssid set_tgid_vip:",
			inputDispatcher_tgid);
		cur_inputDispatcher_tgid = inputDispatcher_tgid;
	}

	/* 0x7e0-0x830：systemui set */
	if (systemui_tgid >= 1 && !touch_sui_vip_status) {
		touch_sui_vip_status = true;
		set_task_basic_vip(systemui_tgid);
		set_tgid_vip(systemui_tgid);
		turn_on_tgid_vip();
		exp_trace_turbo_vip(
			"ct_vip_qualified: sui set_tgid_vip:",
			systemui_tgid);
		cur_systemui_tgid = systemui_tgid;
	}

	/* 0x834-0x880：surfaceflinger set */
	if (surfaceFlinger_tgid >= 1 && !sf_vip_status) {
		sf_vip_status = true;
		set_task_basic_vip(surfaceFlinger_tgid);
		set_tgid_vip(surfaceFlinger_tgid);
		turn_on_tgid_vip();
		exp_trace_turbo_vip(
			"ct_vip_qualified: sf set_tgid_vip:",
			surfaceFlinger_tgid);
		cur_surfaceFlinger_tgid = surfaceFlinger_tgid;
	}

	/* 0x880-0x890：快照同步 */
	update_cur_win_info(&cur_wi_head, &wi_head);
	return 0;
}

int disable_tt_vip(u64 enforced_qualified_mask)
{
	struct win_info *pos, *n;

	turn_off_tgid_vip();

	/* 0x900-0x954：win 清场 */
	if (enforced_qualified_mask != 0 || win_vip_status == 1) {
		win_vip_status = false;
		list_for_each_entry_safe(pos, n, &cur_wi_head.list, list) {
			unset_task_basic_vip(pos->tgid);
			unset_tgid_vip(pos->tgid);
			exp_trace_turbo_vip(
				"disable tt_vip: window unset_tgid_vip:",
				pos->tgid);
		}
	}

	/* 0x958-0x99c：cam_hal */
	if (cam_3rd_hal_vip_status == 1) {
		cam_3rd_hal_vip_status = false;
		if (cam_hal_tgid >= 1) {
			unset_task_basic_vip(cam_hal_tgid);
			unset_tgid_vip(cam_hal_tgid);
			exp_trace_turbo_vip(
				"disable tt_vip: cam_hal unset_tgid_vip:",
				cam_hal_tgid);
			cam_hal_tgid = -1;
		}
	}

	/* 0x9a0-0x9e4：cam_svr */
	if (cam_3rd_svr_vip_status == 1) {
		cam_3rd_svr_vip_status = false;
		if (cam_svr_tgid >= 1) {
			unset_task_basic_vip(cam_svr_tgid);
			unset_tgid_vip(cam_svr_tgid);
			exp_trace_turbo_vip(
				"disable tt_vip: cam_server unset_tgid_vip:",
				cam_svr_tgid);
			cam_svr_tgid = -1;
		}
	}

	/* 0x9e8-0xa1c：ssid */
	if (touch_ssid_vip_status == 1) {
		touch_ssid_vip_status = false;
		unset_task_basic_vip(cur_inputDispatcher_tgid);
		unset_tgid_vip(cur_inputDispatcher_tgid);
		exp_trace_turbo_vip(
			"disable tt_vip: ssid unset_tgid_vip:",
			cur_inputDispatcher_tgid);
	}

	/* 0xa20-0xa54：sui */
	if (touch_sui_vip_status == 1) {
		touch_sui_vip_status = false;
		unset_task_basic_vip(cur_systemui_tgid);
		unset_tgid_vip(cur_systemui_tgid);
		exp_trace_turbo_vip(
			"disable tt_vip: sui unset_tgid_vip:",
			cur_systemui_tgid);
	}

	/* 0xa58-0xa8c：sf */
	if (sf_vip_status == 1) {
		sf_vip_status = false;
		unset_task_basic_vip(cur_surfaceFlinger_tgid);
		unset_tgid_vip(cur_surfaceFlinger_tgid);
		exp_trace_turbo_vip(
			"disable tt_vip: sf unset_tgid_vip:",
			cur_surfaceFlinger_tgid);
	}

	return 0;
}

int is_target_found(const char *comm, int target)
{
	static const char *const targets[] = {
		"system_server", "systemui", "surfaceflinger"
	};

	/* blob 0xaa8：无 paciasp 前导的叶函数快速路径（target-1 <= 2 unsigned） */
	if ((unsigned int)(target - 1) > 2)
		return 0;
	return strstr(comm, targets[target - 1]) != NULL;
}

void set_tid_tgid_vip(int pid)
{
	set_task_basic_vip(pid);
	set_tgid_vip(pid);
	turn_on_tgid_vip();
}

void unset_tid_tgid_vip(int pid)
{
	unset_task_basic_vip(pid);
	unset_tgid_vip(pid);
	turn_off_tgid_vip();
}

void tt_binder_start_vip_inherit(int to_pid, int inherited_vip_prio)
{
	if (inherited_vip_prio >= 1 && inherited_vip_prio <= 3)
		set_task_priority_based_vip_and_throttle(to_pid,
							 inherited_vip_prio,
							 12);
	else if (inherited_vip_prio == 4)
		set_task_vvip_and_throttle(to_pid, 12);
	else if (inherited_vip_prio == 0)
		set_task_basic_vip_and_throttle(to_pid, 12);
}

void tt_binder_stop_vip_inherit(int pid, int inherited_vip_prio)
{
	if (inherited_vip_prio >= 1 && inherited_vip_prio <= 3)
		unset_task_priority_based_vip(pid);
	else if (inherited_vip_prio == 4)
		unset_task_vvip(pid);
	else if (inherited_vip_prio == 0)
		unset_task_basic_vip(pid);
}
