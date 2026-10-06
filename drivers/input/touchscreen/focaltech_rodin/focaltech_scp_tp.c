// SPDX-License-Identifier: GPL-2.0
/*
 * focaltech_scp_tp.c — 小米 rodin focaltech (FT5672) 侧 SCP-TP glue + 配套缺口函数重建
 *
 * 来源：自机 blob focaltech_touch_rodin.ko（小米官方 GPL 发布模块）逐指令反汇编重建。
 *       反汇编稿：tools/_b567_touch/blob/focaltech_touch_rodin.disr
 *       函数头注释 (blob 0xADDR, SIZE) = blob .text 地址/字节数。
 *
 * 逐函数对应（blob 符号 -> 本文件）：
 *   scp_tp_get_reserve_mem (0x30b64,208)  scp_tp_ipi_send (0x30c38,172)
 *   scp_tp_sendparam (0x30ce8,424)        scp_tp_init (0x30e94,312)
 *   scp_tp_gesture_process_work_fun (0x30fd0,92) [LOCAL]
 *   scp_tp_sendparam_work_fun (0x31030,204)      [LOCAL]
 *   scp_tp_ipi_handler (0x31100,584)             [LOCAL]
 *   scp_tp_exit (0x3134c,80)              scp_tp_switch (0x313a0,860)
 *   scp_tp_scp_ready_notifier_call (0x31700,324) [LOCAL]
 *   focal_select_touchmode (0x10d0,412)   focal_scp_gesture (0x1270,160)
 *   focal_get_ic_self_test_mode (0x14d90,88)
 *   fts_gesture_10diff_reg_write (0xf100,404)   fts_htc_set_double_scan (0x2114,164)
 *   fts_charger_on (0x1950,232)           fts_palm_on (0x1a3c,164)
 *   fts_test_write_command (0x111c8,88)   wait_state_update (0x11f10,492)
 *
 * 纪律（b567）：
 *   - =y 撞名：本文件所有框架符号用 fts_ 前缀（scp_tp_* -> fts_scp_tp_*、
 *     wait_scp_ack -> fts_wait_scp_ack、ddr_* -> fts_ddr_*、ap_wait_queue ->
 *     fts_ap_wait_queue、scp_tp_param -> fts_scp_tp_param、
 *     scp_tp_mistouch_close -> fts_scp_tp_mistouch_close）；IC 内部调用点同步改名。
 *   - debug_log_level 已由骨架 focaltech_core.c 定义；本文件只用骨架宏
 *     FTS_INFO/FTS_ERROR/FTS_DEBUG（宏内引用 debug_log_level）。若做 =y 撞名改名
 *     （debug_log_level -> fts_debug_log_level），需同步改 focaltech_common.h 的宏
 *     与 focaltech_core.c 的定义，或在本文件前加
 *     ``。
 *   - blob 中 LOCAL 的 4 个（两个 *_fun/ipi_handler/notifier_call）保持 static。
 *   - 消费面（scp_A_register_notify/.../mtk_ipi_register/mtk_ipi_send）来自已 =y 的
 *     scp.ko / mtk_tinysys_ipi，只调用不重建。
 *
 * 与 goodix 同构性：scp_tp_init/exit/ipi_send/sendparam/get_reserve_mem/两个 *_fun/
 *   ipi_handler 与 goodix 版逐指令同构（仅串面前缀/[行号]/少数字段名不同，见
 *   recon §共享模板分析）；scp_tp_switch 与 notifier_call 两版不同构。
 *
 * 需同步补齐的骨架字段/声明（见 scp_tp_recon.md §接线指引）：
 *   struct fts_ts_data 增加两个 pinctrl 状态（blob 偏移 0xb40/0xb48，DT 名
 *   "touch_mode_ap"/"touch_mode_scp"）：pinctrl_touch_mode_ap / pinctrl_touch_mode_scp；
 *   fts_power_source_init()（blob 名）内补两次 pinctrl_lookup_state()。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/pinctrl/consumer.h>
#include <linux/soc/mediatek/mtk_tinysys_ipi.h>

#include "focaltech_core.h"
#include <scp_rv.h>   /* scp_ipidev/scp_A_*_notify/scp_get_reserve_mem_*/
static void fts_scp_tp_gesture_process_work_fun(struct work_struct *work);
static void fts_scp_tp_sendparam_work_fun(struct work_struct *work);
static int fts_scp_tp_ipi_handler(unsigned int ipi_id, void *prdata, void *data, unsigned int len);
void focal_scp_gesture(void);
int focal_select_touchmode(u32 mode);

/* =y 撞名纪律（b567 §84）：goodix 保留 debug_log_level 原名，focaltech 侧
 * 静态重命名到 fts_debug_log_level（本 TU 生效）；骨架 focaltech_core.c 的
 * debug_log_level 定义同步改名（见 apply_scp_tp_wire2 的 core.c 补丁）。 */
#define debug_log_level fts_debug_log_level

/* =y 撞名纪律（b567 §84）：goodix 保留 debug_log_level 原名，focaltech 侧
 * 静态重命名到 fts_debug_log_level（本 TU 生效）；骨架 focaltech_core.c 的
 * debug_log_level 定义同步改名（见 apply_scp_tp_wire2 的 core.c 补丁）。 */


/* =y 撞名纪律（b567 §84）：goodix 保留 debug_log_level 原名，focaltech 侧
 * 静态重命名到 fts_debug_log_level（本 TU 生效）；骨架 focaltech_core.c 的
 * debug_log_level 定义同步改名（见 apply_scp_tp_wire2 的 core.c 补丁）。 */

#include "focaltech_test/focaltech_test.h"	/* struct fts_test / FTS_TEST_* 宏 */

/* ==================================================================== *
 *  SCP 消费面（已 =y 的 scp.ko / mtk_tinysys_ipi 提供；勿重建）
 * ==================================================================== */

/* scp_rv.h: IPI_OUT_SCP_TP = 54 / IPI_IN_SCP_TP = 55
 * blob 常数：mtk_ipi_send 的 w1 = 0x36 = 54；mtk_ipi_register 的 w1 = 0x37 = 55 */
#ifndef IPI_OUT_SCP_TP
#define IPI_OUT_SCP_TP		54
#endif
#ifndef IPI_IN_SCP_TP
#define IPI_IN_SCP_TP		55
#endif
/* scp_rv.h: enum SCP_NOTIFY_EVENT { SCP_EVENT_READY = 0, SCP_EVENT_STOP, ... } */
#ifndef SCP_EVENT_READY
#define SCP_EVENT_READY		0
#define SCP_EVENT_STOP		1
#endif
/* blob 常数 0x16 = 22；本树 scp_rv.h: SCP_THP_MEM_ID = 22 */
#ifndef SCP_THP_MEM_ID
#define SCP_THP_MEM_ID		22
#endif

/* ==================================================================== *
 *  fts_scp_tp_param —— 112B 参数缓冲（blob .bss+0x5c8，大小 112 = 0x70）
 *  字段语义与 goodix 版完全一致（同协议），见 goodix_scp_tp.c 注释；focaltech 侧
 *  额外用途：fts_read_and_report_foddata() 在 fts_scp_tp_param.param0 == 3（SCP 接管）
 *  时直接从 +0x30 取 9 字节 FOD 数据（blob 0x1368..0x1380），不碰 SPI。
 * ==================================================================== */
struct scp_tp_params fts_scp_tp_param;		/* blob .bss+0x5c8, 112B */
bool fts_scp_tp_mistouch_close;			/* blob .bss+0x63c, 1B（原名归框架） */
bool fts_wait_scp_ack;				/* blob .bss+0x638, 1B */
u64 fts_ddr_phys_addr;				/* blob .bss+0x640, 8B */
u64 fts_ddr_virt_addr;				/* blob .bss+0x648, 8B */
u64 fts_ddr_phys_addr_size;			/* blob .bss+0x650, 8B */

static wait_queue_head_t fts_ap_wait_queue;	/* blob .bss+0x660, 24B [LOCAL] */
struct work_struct fts_scp_tp_gesture_process_work;	/* blob .bss+0x678, 48B */
struct delayed_work fts_scp_tp_sendparam_work;	/* blob .bss+0x6a8, 136B */
static u32 fts_notify_payload[3];		/* blob .bss+0x730, 12B [LOCAL] */
static int fts_scp_tp_scp_ready_notifier_call(struct notifier_block *nb,
					      unsigned long action, void *v);
static struct notifier_block fts_scp_tp_scp_ready_notifier = {
	.notifier_call = fts_scp_tp_scp_ready_notifier_call,	/* blob .data+0x2858+0x0 ABS64 实证（_b571 修） */
};

/* ==================================================================== *
 *  预留内存（blob 0x30b64, 208B；printk 行 31/33）
 * ==================================================================== */
void fts_scp_tp_get_reserve_mem(void)
{
	fts_ddr_phys_addr = scp_get_reserve_mem_phys(SCP_THP_MEM_ID);
	fts_ddr_virt_addr = scp_get_reserve_mem_virt(SCP_THP_MEM_ID);
	fts_ddr_phys_addr_size = scp_get_reserve_mem_size(SCP_THP_MEM_ID);

	if (debug_log_level >= 3)
		FTS_INFO("ddr_phys_addr=%llx,ddr_virt_addr=%llx,ddr_phys_addr_size=%llu",
			 fts_ddr_phys_addr, fts_ddr_virt_addr, fts_ddr_phys_addr_size);

	if (!fts_ddr_phys_addr_size || !fts_ddr_virt_addr || !fts_ddr_phys_addr) {
		if (debug_log_level)
			FTS_ERROR("scp_tp_get_reserve_mem fail");
	}
}

/* blob 0x30c38, 172B；printk 行 49 */
int fts_scp_tp_ipi_send(u32 arg0, u32 arg1, u32 arg2, u32 arg3)
{
	u32 arg[4];
	int ret;

	arg[0] = arg0;
	arg[1] = arg1;
	arg[2] = arg2;
	arg[3] = arg3;

	ret = mtk_ipi_send(&scp_ipidev, IPI_OUT_SCP_TP, 0, arg, sizeof(arg) / 4, 80);
	if (ret) {
		if (debug_log_level)
			FTS_ERROR("scp_tp_ipi_send error, ret = %d", ret);
	}
	return ret;
}

/* blob 0x30ce8, 424B；printk 行 102/107/108 */
int fts_scp_tp_sendparam(void)
{
	if (!fts_ddr_phys_addr) {
		if (debug_log_level)
			FTS_ERROR("ddr_phys_addr is null");
		return 1;
	}

	if (debug_log_level >= 3) {
		FTS_INFO("ddr_virt_addr=0x%llx, ddr_phys_addr=0x%llx, ddr_phys_addr_size=%llx, sizeof(struct scp_tp_params)=%lu",
			 fts_ddr_virt_addr, fts_ddr_phys_addr, fts_ddr_phys_addr_size,
			 sizeof(struct scp_tp_params));
		FTS_INFO("scp_tp_param: 0x%x, %d\n",
			 fts_scp_tp_param.field_08, fts_scp_tp_param.field_10);
	}

	memcpy((void *)fts_ddr_virt_addr, &fts_scp_tp_param,
	       sizeof(fts_scp_tp_param));

	return fts_scp_tp_ipi_send(1, (u32)fts_ddr_phys_addr,
				   (u32)fts_ddr_phys_addr_size, 0);
}

/* blob 0x30e94, 312B；printk 行 167/175 */
int fts_scp_tp_init(void)
{
	int ret;

	if (debug_log_level >= 3)
		FTS_INFO("scp_tp_init in probe");

	init_waitqueue_head(&fts_ap_wait_queue);
	INIT_WORK(&fts_scp_tp_gesture_process_work,
		  fts_scp_tp_gesture_process_work_fun);
	INIT_DELAYED_WORK(&fts_scp_tp_sendparam_work,
			  fts_scp_tp_sendparam_work_fun);

	ret = mtk_ipi_register(&scp_ipidev, IPI_IN_SCP_TP, fts_scp_tp_ipi_handler,
			       NULL, fts_notify_payload);
	if (ret) {
		if (debug_log_level)
			FTS_ERROR("touch_ipi_register error, ret = %d", ret);
	} else {
		fts_scp_tp_get_reserve_mem();
		scp_A_register_notify(&fts_scp_tp_scp_ready_notifier);
	}
	return ret;
}

/* blob 0x3134c, 80B；printk 行 186 */
void fts_scp_tp_exit(void)
{
	if (debug_log_level >= 3)
		FTS_INFO("scp_tp_exit in");

	scp_A_unregister_notify(&fts_scp_tp_scp_ready_notifier);
}

/* blob 0x30fd0, 92B；printk 行 114
 * blob 0x30fec 调用 IC 侧 focal_scp_gesture()（本文件下方） */
static void fts_scp_tp_gesture_process_work_fun(struct work_struct *work)
{
	if (debug_log_level >= 3)
		FTS_INFO("gesture len=%d, type=%d, data_buf: %*ph\n",
			 fts_scp_tp_param.gesture_len,
			 fts_scp_tp_param.gesture_type,
			 fts_scp_tp_param.gesture_len,
			 fts_scp_tp_param.gesture_data);

	focal_scp_gesture();
}

/* blob 0x31030, 204B；printk 行 121/127
 * 重试间隔 blob 常数 = 250 jiffies（HZ=250 -> 1s，见 recon §不确定点） */
static void fts_scp_tp_sendparam_work_fun(struct work_struct *work)
{
	static int retrycount;		/* blob .bss+0x73c */

	if (debug_log_level >= 3)
		FTS_INFO("scp_tp_sendparam_work_fun enter");

	fts_scp_tp_sendparam();
	usleep_range(15000, 16000);

	if (fts_scp_tp_param.param0 == 1 && retrycount <= 4) {
		retrycount++;
		if (debug_log_level >= 3)
			FTS_INFO("scp_tp_sendparam_work_fun retry %d", retrycount);
		schedule_delayed_work(&fts_scp_tp_sendparam_work,
				      msecs_to_jiffies(1000));
	}
}

/* blob 0x31100, 584B；printk 行 58/62/73/65/67/92
 * switch(msg[0])，跳表 .rodata+0xca4 = {00,00,20,00,27,31}（base 0x3115c）：
 *   0x11/0x12/0x14 手势数据；0x13 GETPARAM；0x15 ack；0x16 REQPARAM；0xf1 TEST0 */
static int fts_scp_tp_ipi_handler(unsigned int ipi_id, void *prdata, void *data,
				  unsigned int len)
{
	u32 *msg = data;

	if (debug_log_level >= 3)
		FTS_INFO("[scp-tp]: working status:%d, event:%d",
			 fts_scp_tp_param.param0, msg[0]);

	switch (msg[0]) {
	case 0x11:
	case 0x12:
	case 0x14:
		fts_scp_tp_param.gesture_type = *(u32 *)(fts_ddr_virt_addr + 0x28);
		fts_scp_tp_param.gesture_len = *(u32 *)(fts_ddr_virt_addr + 0x2c);
		memcpy(fts_scp_tp_param.gesture_data,
		       (void *)(fts_ddr_virt_addr + 0x30),
		       fts_scp_tp_param.gesture_len);
		queue_work(system_wq, &fts_scp_tp_gesture_process_work);
		break;

	case 0x13:	/* CMD_SCP2AP_GETPARAM */
		if (debug_log_level >= 3)
			FTS_INFO("get CMD_SCP2AP_GETPARAM\n");
		fts_scp_tp_param.param0 = 2;
		break;

	case 0x15:	/* ack */
		fts_wait_scp_ack = true;
		wake_up(&fts_ap_wait_queue);
		break;

	case 0x16:	/* CMD_SCP2AP_REQPARAM */
		if (debug_log_level >= 3)
			FTS_INFO("get CMD_SCP2AP_REQPARAM, cur_state=%d\n",
				 fts_scp_tp_param.param0);
		if (fts_scp_tp_param.param0 == 1 || fts_scp_tp_param.param0 == 2)
			break;
		if (debug_log_level >= 3)
			FTS_INFO("send param in CMD_SCP2AP_REQPARAM");
		fts_scp_tp_param.param0 = 1;
		schedule_delayed_work(&fts_scp_tp_sendparam_work, 0);
		break;

	case 0xf1:	/* CMD_SCP2AP_TEST0 */
		if (debug_log_level >= 3)
			FTS_INFO("get CMD_SCP2AP_TEST0\n");
		break;

	default:
		if (debug_log_level >= 3)
			FTS_INFO("unknow event from SCP: %*ph\n", len, data);
		break;
	}
	return 0;
}

/* ==================================================================== *
 *  AP/SCP 控制权切换 —— focaltech 版（blob 0x313a0, 860B）
 *  与 goodix 版差异（recon §两份差异函数全文）：
 *    (1) 进入时先用 driver_get_touch_mode_common() 组 SCP 手势掩码（blob 313c4..31420）；
 *    (2) pin/IRQ 切换走 focal_select_touchmode()（内含 IRQ 开关 + irq_set_irq_wake
 *        + pinctrl），不是 goodix 的 pinctrl_select_state + hw_ops->irq_enable 组合；
 *    (3) IPI 载荷里的 gesture 用 (1) 的掩码（goodix 用 core->gesture_enabled）。
 *  其余骨架（param0 判定/等待 ack/wait_event 13 jiffies/295B 增量）与 goodix 版一致。
 *  printk 行 206/212/214/227/232
 * ==================================================================== */
int fts_scp_tp_switch(u32 mode)
{
	u32 gesture_type = 0;
	int ret = 0;

	/* blob 313c4..31420：按 touch_mode 组合掩码（常数与反汇编逐条一致）
	 *   DATA_MODE_11 (aod)        -> bit0
	 *   DATA_MODE_14 (doubletap)  -> bit1
	 *   DATA_MODE_10 (fod)        -> bit2；且 DATA_MODE_16 非 0 时再置 bit0
	 * [TODO-VERIFY] 这组位序与 xiaomi_touch.h 的 GESTURE_*_EVENT 名字
	 * （0x01 LONGPRESS / 0x02 SINGLETAP / 0x04 DOUBLETAP）不对应，按 blob 常数写。 */
	if (driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_11))
		gesture_type |= 0x01;
	if (driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_14))
		gesture_type |= 0x02;
	if (driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10)) {
		gesture_type |= 0x04;
		if (driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_16))
			gesture_type |= 0x01;
	}

	/* blob 31464..31484：打印 (mode & 1, gesture_type) */
	if (debug_log_level >= 3)
		FTS_INFO("[scp-tp]: switch to %d, gesture=0x%x",
			 mode & 0x1, gesture_type);

	if (mode & 0x1) {
		/* blob 31438..31460：SCP 工作时间片 -> param0=3 + 切 pin，否则 3158c */
		if (fts_scp_tp_param.param0 > 1) {
			fts_scp_tp_param.param0 = 3;
			ret = focal_select_touchmode(mode & 0x1);
			if (ret < 0)
				goto pin_fail;
			goto to_scp;
		}
		goto not_ready;
	}

	/* ---- blob 31488：请求 SCP 交回（IPI{2,0,gesture_type,0}，内联 ipi_send） */
	fts_scp_tp_ipi_send(2, 0, gesture_type, 0);

	/* blob 314c8：ack 已到？否则 wait_event_interruptible_timeout(13 jiffies) */
	if (fts_wait_scp_ack) {
		if (debug_log_level >= 3)
			FTS_INFO("wait scp ack back, switch to ap");
	} else {
		if (wait_event_interruptible_timeout(fts_ap_wait_queue,
						     fts_wait_scp_ack,
						     msecs_to_jiffies(50)) <= 0) {
			if (debug_log_level)
				FTS_ERROR("scp_tp_ipi_send error, switch to ap");
		} else {
			if (debug_log_level >= 3)
				FTS_INFO("wait scp ack back, switch to ap");
		}
	}

	/* blob 3155c：param0>1 -> 4 并切回 AP（focal_select_touchmode(0)）；否则 3158c */
	fts_wait_scp_ack = false;
	if (fts_scp_tp_param.param0 > 1) {
		fts_scp_tp_param.param0 = 4;
		ret = focal_select_touchmode(0);
		if (ret < 0)
			goto pin_fail;
		return ret;
	}

not_ready:
	/* blob 3158c / 316a0（tail-merge） */
	if (debug_log_level)
		FTS_ERROR("scp not ready, don't change state");
	ret = focal_select_touchmode(mode & 0x1);
	if (ret < 0)
		goto pin_fail;
	if (!(mode & 0x1))
		return ret;

to_scp:
	/* blob 315a8：IPI{2,1,gesture_type,0} */
	return fts_scp_tp_ipi_send(2, 1, gesture_type, 0);

pin_fail:
	/* blob 31600 / 316d8：两处 focal_select_touchmode 失败共用（行 232） */
	if (debug_log_level)
		FTS_ERROR("Failed to select default pinstate, ret:%d", ret);
	return ret;
}

/* ==================================================================== *
 *  SCP ready/stop notifier —— focaltech 版（blob 0x31700, 324B）
 *  printk 行 144/147/151/155
 *  与 goodix 版差异：不取 core_data；STOP 支路只调 focal_select_touchmode(0)
 *  （goodix 版是 pinctrl_select_state + hw_ops->irq_enable + irq_set_irq_wake）。
 * ==================================================================== */
static int fts_scp_tp_scp_ready_notifier_call(struct notifier_block *nb,
					      unsigned long action, void *v)
{
	if (action == SCP_EVENT_READY) {
		fts_scp_tp_mistouch_close = 0;
		if (fts_scp_tp_param.param0 != 1) {
			if (debug_log_level >= 3)
				FTS_INFO("send param in SCP_EVENT_READY");
			fts_scp_tp_param.param0 = 1;
			schedule_delayed_work(&fts_scp_tp_sendparam_work, 0);
		}
		if (debug_log_level >= 3)
			FTS_INFO("get SCP_EVENT_READY");
		return 0;
	}

	if (action == SCP_EVENT_STOP) {
		fts_scp_tp_mistouch_close = 1;
		cancel_delayed_work(&fts_scp_tp_sendparam_work);
		if (fts_scp_tp_param.param0 == 3) {
			if (debug_log_level >= 3)
				FTS_INFO("switch to ap in SCP_EVENT_STOP");
			focal_select_touchmode(0);	/* blob 31754 */
		}
		if (debug_log_level >= 3)
			FTS_INFO("get SCP_EVENT_STOP");
		return 0;
	}
	return 0;
}

/* ==================================================================== *
 *  IC 侧胶水
 * ==================================================================== */

/* blob 0x10d0, 412B；printk 行 402/417（FTS_FUNC_ENTER 由 fts_irq_* 内联带入）、
 * 1572/1577（ts_debug 打印 irq_set_irq_wake 失败，行号属 focal_select_touchmode）
 * mode bit0 == 1：SCP 态 —— fts_irq_disable() + irq_set_irq_wake(irq, 0)
 *                        + pinctrl_select_state(touch_mode_scp)
 * mode bit0 == 0：AP 态  —— fts_irq_enable()  + irq_set_irq_wake(irq, 1)
 *                        + pinctrl_select_state(touch_mode_ap)
 * 返回 pinctrl_select_state() 的返回值（调用方判 <0）。 */
int focal_select_touchmode(u32 mode)
{
	struct fts_ts_data *ts_data = fts_data;

	if (mode & 0x1) {
		fts_irq_disable();			/* blob 10f8..1134（内联） */
		if (irq_set_irq_wake(ts_data->irq, 0))
			if (debug_log_level >= 4)
				FTS_DEBUG("disable_irq_wake(irq:%d) fail",
					  ts_data->irq);
		return pinctrl_select_state(ts_data->pinctrl,
					   ts_data->pinctrl_touch_mode_scp);
	} else {
		fts_irq_enable();			/* blob 1168..11a0（内联） */
		if (irq_set_irq_wake(ts_data->irq, 1))
			if (debug_log_level >= 4)
				FTS_DEBUG("enable_irq_wake(irq:%d) fail",
					  ts_data->irq);
		return pinctrl_select_state(ts_data->pinctrl,
					   ts_data->pinctrl_touch_mode_ap);
	}
}

/* blob 0x1270, 160B；printk 行 1590/1594
 * SCP 报手势后的解析入口：FOD 数据 + 手势数据两条。
 * [TODO-VERIFY] 同 TU 依赖：blob 中 fts_read_and_report_foddata 是 LOCAL(static)
 * （骨架 focaltech_core.c:1008，且缺 blob 的 scp_tp_param.param0==3 分支），
 * 本函数是 GLOBAL —— 两者在 blob 里同 TU。接法二选一（recon §接线指引）：
 *   (a) 把本函数放进 focaltech_core.c；或
 *   (b) 去掉 fts_read_and_report_foddata 的 static、在 focaltech_core.h 声明，
 *       并按 recon 给 fts_read_and_report_foddata 补 SCP 数据分支。 */
extern int fts_read_and_report_foddata(struct fts_ts_data *data);

void focal_scp_gesture(void)
{
	struct fts_ts_data *ts_data = fts_data;

	pm_stay_awake(ts_data->dev);			/* blob: [fts_data+0x10] */

	if (fts_read_and_report_foddata(ts_data))
		FTS_ERROR("fail to get fod data in scp handler");

	if (fts_gesture_readdata(ts_data, NULL))
		FTS_ERROR("fail to get gesture data in scp handler");

	pm_relax(ts_data->dev);
}

/* blob 0x14d90, 88B；printk 行 2728
 * 读 ic_self_test_flag（blob .bss+0x5b8, 1B [LOCAL]）。
 * 该 flag 的写入点在 fts_ic_self_test()（blob 0x14ec8，骨架 focaltech_core.c:3643），
 * blob 里两者同 TU 才static；本重建文件与写点不同 TU，故这里提为全局
 * （[TODO-VERIFY] 若把本函数放回 focaltech_core.c，可还原为 static）。 */
bool ic_self_test_flag;

u8 focal_get_ic_self_test_mode(void)
{
	if (debug_log_level >= 4)
		FTS_DEBUG("enter ic_self_test_flag %d", ic_self_test_flag);

	return ic_self_test_flag;
}

/* blob 0xf100, 404B；printk 行 497/500
 * 写 0xBF（FACTROY_REG_CB_BUF_SEL）后读回校验，最多 5 次（写-睡 1ms-读-比对）。 */
int fts_gesture_10diff_reg_write(u8 value)
{
	u8 read_buf = 0;
	int ret = 0;
	int i;

	for (i = 0; i < 5; i++) {			/* blob: 5 次展开 */
		fts_write_reg(FACTROY_REG_CB_BUF_SEL, value);
		msleep(1);
		fts_read_reg(FACTROY_REG_CB_BUF_SEL, &read_buf);
		if (read_buf == value)
			break;
	}

	if (read_buf == value) {
		if (debug_log_level >= 3)
			FTS_INFO("[GESTURE]sucessed send gesture_10diff cmd!\n");
		return 0;
	}
	if (debug_log_level)
		FTS_ERROR("[GESTURE CMD ERROR]failed send gesture_10diff cmd!\n");
	return -EIO;
}

/* blob 0x2114, 164B；printk 行 3007
 * 写 {0x9D, value} 两字节（0x9D = double-scan 寄存器，骨架无宏 [TODO-VERIFY]）。 */
int fts_htc_set_double_scan(u8 value)
{
	u8 buf[2];
	int ret;

	buf[0] = 0x9D;
	buf[1] = value;

	ret = fts_write(buf, sizeof(buf));
	if (ret < 0) {
		if (debug_log_level)
			FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
				  buf[0], buf[1], ret);
	}
	return ret;
}

/* blob 0x1950, 232B；printk 行 2526/2529/2531
 * 充电器在线状态 -> 0x8B（FTS_REG_CHARGER_MODE_EN）。
 * [TODO-VERIFY] 首参按骨架 hardware_operation.ic_set_charge_state 的形状保留
 * (struct fts_ts_data *)，blob 只做 NULL/IS_ERR 校验后打 ts_data->charger_status
 * （blob 偏移 0x1a8，按骨架字段名 charger_status 对应）。 */
int fts_charger_on(struct fts_ts_data *ts_data, bool on)
{
	int ret;

	if (!ts_data || IS_ERR(ts_data))
		return -EINVAL;

	if (debug_log_level >= 3)
		FTS_INFO("charger usb %s", (on & 0x1) ? "in" : "out");

	ret = fts_write_reg(FTS_REG_CHARGER_MODE_EN, on & 0x1);
	if (ret < 0) {
		if (debug_log_level)
			FTS_ERROR("failed to set power supply status:%d",
				  fts_data->charger_status);
	} else {
		if (debug_log_level >= 4)
			FTS_DEBUG("success to set power supply status:%d",
				  fts_data->charger_status);
	}
	return ret;
}

/* blob 0x1a3c, 164B；printk 行 2556/2558
 * 手掌感应开关 -> 0x9A（骨架无宏 [TODO-VERIFY]），on=5 / off=0。
 * blob 内无任何调用点/表项（死代码），符号 GLOBAL，重建保留。 */
int fts_palm_on(struct fts_ts_data *ts_data, bool on)
{
	int ret;

	if (!ts_data || IS_ERR(ts_data))
		return -EINVAL;

	ret = fts_write_reg(0x9A, (on & 0x1) ? 5 : 0);
	if (ret < 0) {
		if (debug_log_level)
			FTS_ERROR("Set palm sensor switch failed!\n");
	} else {
		if (debug_log_level >= 3)
			FTS_INFO("Set palm sensor switch: %d\n", on & 0x1);
	}
	return ret;
}

/* blob 0x111c8, 88B（无 printk）
 * 单字节命令写：return min(fts_write(&cmd,1), 0) —— blob 用
 * `and w0, w0, w0, asr #31` 实现（等价 ret > 0 ? 0 : ret）。 */
int fts_test_write_command(u8 cmd)
{
	int ret = fts_write(&cmd, 1);

	return ret > 0 ? 0 : ret;
}

/* wait_state_update：donor focaltech_test.c 版承接（blob 同族通用轮询胶水，
 * 18ms×50/INCELL 寄存器选择一致；focaltech_test.c 定义即 blob def 原名）。 */
