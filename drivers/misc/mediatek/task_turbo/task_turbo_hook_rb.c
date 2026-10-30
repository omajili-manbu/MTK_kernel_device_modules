// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Copyright (c) 2026 rodin project.
 *
 * rodin 6.18（2026-10-31）：task_turbo 算法层 blob 的 hook 胶水
 * （task_turbo_hook_no_cfi.o_shipped, 6.4KB, 单函数）全量逆向重建。
 * 基准 = E:\tools\_turbo_rebuild_20261031\hook_no_cfi.dis（15 对 hook
 * 指针赋值，顺序与 blob 逐位一致）。
 *
 * ⚠ 内核换代漂移警告：本文件的语义 = 「api 实现 ↔ 动作函数」的静态
 * 接线表。两侧任一侧符号改名/原型变化都会在此处显式断链（链接期即
 * 报错，不会静默）——若未来引入按 hook 指针热切换的形态，本文件
 * 必须重审。
 */

#include <linux/module.h>
#include "task_turbo_api.h"

void init_task_turbo_hook(void)
{
	tt_vip_algo_hook = tt_vip_algo;
	disable_tt_vip_hook = disable_tt_vip;
	wi_add_tgid_hook = wi_add_tgid;
	wi_del_tgid_hook = wi_del_tgid;
	is_target_found_hook = is_target_found;
	set_td_hook = set_task_basic_vip;
	unset_td_hook = unset_task_basic_vip;
	set_tgd_hook = set_tgid_vip;
	unset_tgd_hook = unset_tgid_vip;
	turn_on_tgd_hook = turn_on_tgid_vip;
	turn_off_tgd_hook = turn_off_tgid_vip;
	set_tdtgd_hook = set_tid_tgid_vip;
	unset_tdtgd_hook = unset_tid_tgid_vip;
	binder_start_vip_inherit_hook = tt_binder_start_vip_inherit;
	binder_stop_vip_inherit_hook = tt_binder_stop_vip_inherit;
}
