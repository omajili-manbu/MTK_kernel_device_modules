// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <asm/errno.h>
#include <linux/vseq.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cpuidle.h>

#include <lpm_plat_apmcu.h>
#include <lpm_module.h>
#include "lpm_plat.h"
#include "lpm_plat_suspend.h"

static unsigned int lpm_pwr_state;

/*
 * Please make sure the race condition protection upfront
 * when calling mtk_lp_plat_do_mcusys_prepare_pdn()
 */
int lpm_do_mcusys_prepare_pdn(unsigned int status,
					   unsigned int *resource_req)
{
	lpm_pwr_state |= (status | PLAT_GIC_MASKED
				| PLAT_MCUSYSOFF_PREPARED);
	return 0;
}

/*
 * Please make sure the race condition protection upfront
 * when calling mtk_lp_plat_do_mcusys_prepare_on()
 */
static int __lpm_do_mcusys_prepare_on(unsigned int clr_status)
{
	lpm_pwr_state &= ~(clr_status | PLAT_GIC_MASKED
				  | PLAT_MCUSYSOFF_PREPARED);

	return 0;
}

int lpm_do_mcusys_prepare_on(void)
{
	unsigned int status = lpm_pwr_state;

	return __lpm_do_mcusys_prepare_on(status);
}

static int __init lpm_early_initcall(void)
{
	lpm_plat_apmcu_early_init();
	return 0;
}

#ifndef MTK_LPM_MODE_MODULE
vseq_subsys_initcall(lpm_early_initcall);
#endif

static int __init lpm_device_initcall(void)
{
	return 0;
}

/*
 * rodin 6.18（2026-11-01）：VALIDATE SET 延迟 40s。
 * 6.6 路径 = vendor 模块 ~40s 加载后才发本 SMC（MCUPM fw 已 ready，
 * BL31 DEPD 握手顺利 → 首次 mcusysoff 39.4s）。6.18 内建后
 * late_initcall ~6s 就打（MCUPM TASK_STA 仍 0），BL31 validate 状态
 * 被提前写坏 → CPU_SUSPEND 全拒（-2 风暴）。延迟到 40s 与 6.6 时序
 * 对齐；40s 后 MCUPM fw 稳定运行，SET 到达即通。
 */
static void lpm_validate_delay_fn(struct work_struct *work)
{
	cpuidle_pause_and_lock();
	lpm_smc_cpu_pm(VALIDATE_PWR_STATE_CTRL, MT_LPM_SMC_ACT_SET,
			PSCI_E_SUCCESS, 0);
	cpuidle_resume_and_unlock();
	pr_info("[name:mtk_lpm][P] - validate SET sent (delayed 40s)\n");
}
static DECLARE_DELAYED_WORK(lpm_validate_delay, lpm_validate_delay_fn);

static int __init lpm_late_initcall(void)
{
	lpm_plat_apmcu_init();
	lpm_model_suspend_init();

	schedule_delayed_work(&lpm_validate_delay, 40 * HZ);

	return 0;
}
#ifndef MTK_LPM_MODE_MODULE
vseq_late_initcall_sync(lpm_late_initcall);
#endif

static int __init lpm_plat_init(void)
{
	int ret = 0;
#ifdef MTK_LPM_MODE_MODULE
	ret = lpm_early_initcall();
#endif
	if (ret)
		goto lpm_plat_init_fail;

	ret = lpm_device_initcall();

	if (ret)
		goto lpm_plat_init_fail;

#ifdef MTK_LPM_MODE_MODULE
	ret = lpm_late_initcall();
#endif

	if (ret)
		goto lpm_plat_init_fail;

	return 0;

lpm_plat_init_fail:
	return -EAGAIN;
}

static void __exit lpm_plat_exit(void)
{
}

vseq_module_init(lpm_plat_init);
module_exit(lpm_plat_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTK low power platform module");
MODULE_AUTHOR("MediaTek Inc.");
