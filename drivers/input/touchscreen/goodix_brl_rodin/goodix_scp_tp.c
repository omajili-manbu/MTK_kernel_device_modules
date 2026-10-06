// SPDX-License-Identifier: GPL-2.0
/*
 * goodix_scp_tp.c — 小米 rodin goodix (GT9916/S) 侧 SCP-TP glue 重建
 *
 * 来源：自机 blob goodix_core_rodin.ko（小米官方 GPL 发布模块）逐指令反汇编重建。
 *       反汇编稿：tools/_b567_touch/blob/goodix_core_rodin.disr
 *       每个函数头注释里的 (blob 0xADDR, SIZE) 即该函数在 blob .text 中的地址/字节数；
 *       "行 N" = blob 内联 __LINE__（printk 实参）真值，重建后行号自然变化，仅作定位用。
 *
 * 逐函数对应（blob 符号 -> 本文件）：
 *   scp_tp_get_reserve_mem (0x22e90,208)   scp_tp_ipi_send (0x22f64,172)
 *   scp_tp_sendparam (0x23014,424)         scp_tp_init (0x231c0,312)
 *   scp_tp_gesture_process_work_fun (0x232fc,92) [LOCAL]
 *   scp_tp_sendparam_work_fun (0x2335c,204)      [LOCAL]
 *   scp_tp_ipi_handler (0x2342c,584)             [LOCAL]
 *   scp_tp_exit (0x23678,80)               scp_tp_switch (0x236cc,784)
 *   scp_tp_scp_ready_notifier_call (0x239e0,404) [LOCAL]
 *   goodix_scp_gesture (0x93a0,156)        goodix_gesture_10diff_write (0x16dcc,180)
 *   goodix_readprint_debuginfo (0x16244,248)
 *
 * 纪律（b567）：
 *   - blob 中本文件符号全部为 GLOBAL 定义且两 blob 无 __ksymtab（无 EXPORT_SYMBOL）
 *     -> 重建为非 static 全局、不加 EXPORT_SYMBOL。
 *   - blob 中 LOCAL 的 4 个（两个 *_fun、ipi_handler、notifier_call）保持 static。
 *   - =y 撞名：goodix 侧保留原名；focaltech 侧同族实现见 focaltech_scp_tp.c（全部 fts_ 前缀）。
 *   - 消费面（scp_A_register_notify / scp_A_unregister_notify / scp_get_reserve_mem_{phys,virt,size}
 *     / scp_ipidev / mtk_ipi_register / mtk_ipi_send）来自已 =y 的 scp.ko / mtk_tinysys_ipi，
 *     只调用不重建 —— 见下面“SCP 消费面”块。
 *
 * 需同步补齐的骨架字段（见 scp_tp_recon.md §接线指引）：
 *   struct goodix_ts_core 增加两个 pinctrl 状态（blob 偏移 0x4b0/0x4b8，DT 名
 *   "touch_mode_ap"/"touch_mode_scp"）：pin_sta_touch_mode_ap / pin_sta_touch_mode_scp；
 *   goodix_ts_pinctrl_init() 内对应补两次 pinctrl_lookup_state()。
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/pm_qos.h>
#include <linux/pinctrl/consumer.h>
#include <linux/soc/mediatek/mtk_tinysys_ipi.h>

#include "goodix_ts_core.h"
#include <scp_rv.h>   /* scp_ipidev/scp_A_*_notify/scp_get_reserve_mem_*/
static void scp_tp_gesture_process_work_fun(struct work_struct *work);
static void scp_tp_sendparam_work_fun(struct work_struct *work);
static int scp_tp_ipi_handler(unsigned int ipi_id, void *prdata, void *data, unsigned int len);
void goodix_scp_gesture(void);
int goodix_gesture_10diff_write(struct goodix_ts_core *cd, u8 type);
void goodix_readprint_debuginfo(struct goodix_ts_core *cd);

/* ==================================================================== *
 *  SCP 消费面（已 =y 的 scp.ko / mtk_tinysys_ipi 提供；勿重建）
 *  若 Makefile 的 ccflags 带上 -I$(srctree)/drivers/misc/mediatek/scp/include，
 *  可直接 #include "scp_rv.h" 取代下面这一块（符号名/原型逐一取自该头）。
 * ==================================================================== */

/* scp_rv.h 的 enum：IPI_OUT_SCP_TP = 54（AP->SCP，mtk_ipi_send 用），
 *                   IPI_IN_SCP_TP  = 55（SCP->AP，mtk_ipi_register 用）。
 * blob 常数验证：scp_tp_ipi_send/sendparam/switch 里 mtk_ipi_send 的 w1 = 0x36 = 54；
 *               scp_tp_init 里 mtk_ipi_register 的 w1 = 0x37 = 55。 */
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
/* blob 常数为 0x16 = 22；本树 scp_rv.h: SCP_THP_MEM_ID = 22 */
#ifndef SCP_THP_MEM_ID
#define SCP_THP_MEM_ID		22
#endif

/* 骨架 goodix_ts_core.c 里的两个 GLOBAL（未进 goodix_ts_core.h，故此处 extern） */
extern struct goodix_ts_core *get_goodix_core_data(void);	/* :1644 */
extern struct goodix_module goodix_modules;			/* :58 */

/* ==================================================================== *
 *  scp_tp_param —— 112B 参数缓冲（blob .bss+0x5ad0，大小 112 = 0x70）
 *  字段含义由 ipi_handler/sendparam/sendparam_work_fun/switch/notifier 的读写反推：
 *    +0x00 u32 param0  状态机：0 空闲 / 1 已发参数待 SCP 回执 / 2 SCP 已 GETPARAM /
 *                      3 SCP 已接管（SCP 工作态）/ 4 已交回 AP
 *    +0x08,+0x10       仅 sendparam 的 verbose 打印读取（blob 内无写入点，语义未知）
 *    +0x28 u32 type    SCP->AP 数据通道：类型（ipi_handler 从 ddr+0x28 抄入）
 *    +0x2c u32 len     数据长度（≤0x40；ipi_handler 的 memcpy fortify 上限 0x41 = 64B）
 *    +0x30 u8[64] data 数据（手势/FOD 上报体；goodix_gesture_ist 从这里 memcpy 出去）
 *  112 = 0x30 + 0x40 精确吻合，见 scp_tp_recon.md §共享模板分析。
 * ==================================================================== */
/* struct scp_tp_params 已上移 goodix_ts_core.h（core.c 的 scp_debug 节点要用，_b571） */

struct scp_tp_params scp_tp_param;		/* blob .bss+0x5ad0, 112B */
u8 goodix_scp_tp_mistouch_close;   /* 框架同名全局为 u8，=y 单符号空间统一 */			/* blob .bss+0x5b44, 1B（=y 撞名后续改为
						 * goodix_scp_tp_mistouch_close，见 recon §接线）
						 */
bool wait_scp_ack;				/* blob .bss+0x5b40, 1B（两 IC 各持一份，
						 * focaltech 侧 = fts_wait_scp_ack）
						 */
u64 ddr_phys_addr;				/* blob .bss+0x5b48, 8B */
u64 ddr_virt_addr;				/* blob .bss+0x5b50, 8B */
u64 ddr_phys_addr_size;				/* blob .bss+0x5b58, 8B */

static wait_queue_head_t ap_wait_queue;		/* blob .bss+0x5b68, 24B [LOCAL] */
struct work_struct scp_tp_gesture_process_work;	/* blob .bss+0x5b80, 48B [GLOBAL] */
struct delayed_work scp_tp_sendparam_work;	/* blob .bss+0x5bb0, 136B [GLOBAL] */
static u32 notify_payload[3];			/* blob .bss+0x5c38, 12B [LOCAL]（mtk_ipi_register
						 * 的第 5 参 void *msg） */
static int scp_tp_scp_ready_notifier_call(struct notifier_block *nb,
					  unsigned long action, void *v);
static struct notifier_block scp_tp_scp_ready_notifier = {
	.notifier_call = scp_tp_scp_ready_notifier_call,	/* blob .data+0xa28+0x0 ABS64 实证（_b571 修） */
};

/* ==================================================================== *
 *  SCP 预留内存（ddr_*）取自 scp.ko；地址经 .bss reloc 逐条确认
 *  blob 0x22e90, 208B；printk 行 33/35
 * ==================================================================== */
void scp_tp_get_reserve_mem(void)
{
	ddr_phys_addr = scp_get_reserve_mem_phys(SCP_THP_MEM_ID);	/* mov w0,#0x16 */
	ddr_virt_addr = scp_get_reserve_mem_virt(SCP_THP_MEM_ID);
	ddr_phys_addr_size = scp_get_reserve_mem_size(SCP_THP_MEM_ID);

	if (debug_log_level >= 3)
		ts_info("ddr_phys_addr=%llx,ddr_virt_addr=%llx,ddr_phys_addr_size=%llu",
			ddr_phys_addr, ddr_virt_addr, ddr_phys_addr_size);

	/* blob: cmp size,#0 / ccmp virt,#0 / ccmp phys,#0 -> 任一为 0 则告警 */
	if (!ddr_phys_addr_size || !ddr_virt_addr || !ddr_phys_addr) {
		if (debug_log_level)
			ts_err("scp_tp_get_reserve_mem fail");
	}
}

/* ==================================================================== *
 *  IPI 发送：4 个字 {arg0,arg1,arg2,arg3} -> IPI_OUT_SCP_TP，len=4 槽，wait=80ms
 *  blob 0x22f64, 172B；printk 行 51
 * ==================================================================== */
int scp_tp_ipi_send(u32 arg0, u32 arg1, u32 arg2, u32 arg3)
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
			ts_err("scp_tp_ipi_send error, ret = %d", ret);
	}
	return ret;
}

/* ==================================================================== *
 *  sendparam：把 112B param 拷贝到 ddr 预留区，通告 SCP 读参数
 *  blob 0x23014, 424B；printk 行 104/109/110（110 与 51 共用 scp_tp_ipi_send 的串）
 * ==================================================================== */
int scp_tp_sendparam(void)
{
	if (!ddr_phys_addr) {
		if (debug_log_level)
			ts_err("ddr_phys_addr is null");
		return 1;
	}

	if (debug_log_level >= 3) {
		ts_info("ddr_virt_addr=0x%llx, ddr_phys_addr=0x%llx, ddr_phys_addr_size=%llx, sizeof(struct scp_tp_params)=%lu",
			ddr_virt_addr, ddr_phys_addr, ddr_phys_addr_size,
			sizeof(struct scp_tp_params));
		ts_info("scp_tp_param: 0x%x, %d\n",
			scp_tp_param.field_08, scp_tp_param.field_10);
	}

	/* blob 23064..230c0：112B 逐 ldp/stp 拷贝（= memcpy，目标 ddr_virt_addr） */
	memcpy((void *)ddr_virt_addr, &scp_tp_param, sizeof(scp_tp_param));

	/* blob 230c8..230d0：{1, (u32)phys, (u32)size, 0} 且错误打印落在 scp_tp_ipi_send 体内
	 * （同串同行 51），判为编译器把 scp_tp_ipi_send() 内联进本函数 —— 语义即调用。 */
	return scp_tp_ipi_send(1, (u32)ddr_phys_addr,
			       (u32)ddr_phys_addr_size, 0);
}

/* ==================================================================== *
 *  init/exit：注册 55 号接收通道、取预留内存、挂 SCP ready notifier
 *  blob 0x231c0, 312B；printk 行 174/182
 * ==================================================================== */
int scp_tp_init(void)
{
	int ret;

	if (debug_log_level >= 3)
		ts_info("scp_tp_init in probe");

	/* blob 231e0..231f8 = init_waitqueue_head(&ap_wait_queue) 展开：
	 * __init_waitqueue_head(&ap_wait_queue, "&ap_wait_queue", &scp_tp_init.__key) */
	init_waitqueue_head(&ap_wait_queue);

	/* blob 231fc..2322c = INIT_WORK(&scp_tp_gesture_process_work, *_fun) 展开 */
	INIT_WORK(&scp_tp_gesture_process_work, scp_tp_gesture_process_work_fun);

	/* blob 23230..23260 = INIT_DELAYED_WORK(&scp_tp_sendparam_work, *_fun) 展开
	 * （含 init_timer_key(&work->timer, delayed_work_timer_fn, 0x200000, NULL, NULL)） */
	INIT_DELAYED_WORK(&scp_tp_sendparam_work, scp_tp_sendparam_work_fun);

	ret = mtk_ipi_register(&scp_ipidev, IPI_IN_SCP_TP, scp_tp_ipi_handler,
			       NULL, notify_payload);
	if (ret) {
		if (debug_log_level)
			ts_err("touch_ipi_register error, ret = %d", ret);
	} else {
		scp_tp_get_reserve_mem();
		scp_A_register_notify(&scp_tp_scp_ready_notifier);
	}
	return ret;
}

/* blob 0x23678, 80B；printk 行 193 */
void scp_tp_exit(void)
{
	if (debug_log_level >= 3)
		ts_info("scp_tp_exit in");

	scp_A_unregister_notify(&scp_tp_scp_ready_notifier);
}

/* ==================================================================== *
 *  手势 work（blob 232fc, 92B；printk 行 121）
 *  blob 23318 调用 IC 侧 goodix_scp_gesture()（本文件下方）
 * ==================================================================== */
static void scp_tp_gesture_process_work_fun(struct work_struct *work)
{
	/* blob 23328..23350：取 param+0x28/+0x2c/+0x30 打 len/type/%*ph */
	if (debug_log_level >= 3)
		ts_info("gesture len=%d, type=%d, data_buf: %*ph\n",
			scp_tp_param.gesture_len, scp_tp_param.gesture_type,
			scp_tp_param.gesture_len, scp_tp_param.gesture_data);

	goodix_scp_gesture();
}

/* ==================================================================== *
 *  sendparam 重试 work（blob 2335c, 204B；printk 行 128/134）
 *  最多重试 5 次（retrycount 0..4 -> 判定 ">4 停"），间隔 = 250 jiffies
 *  （HZ=250 @rodin -> 1s；blob 常数 0xfa，见 recon §不确定点）
 * ==================================================================== */
static void scp_tp_sendparam_work_fun(struct work_struct *work)
{
	/* blob .bss+0x5c44 = scp_tp_sendparam_work_fun.retrycount */
	static int retrycount;

	if (debug_log_level >= 3)
		ts_info("scp_tp_sendparam_work_fun enter");

	scp_tp_sendparam();
	usleep_range(15000, 16000);	/* blob: usleep_range_state(15000,16000,2) */

	if (scp_tp_param.param0 == 1 && retrycount <= 4) {
		retrycount++;
		if (debug_log_level >= 3)
			ts_info("scp_tp_sendparam_work_fun retry %d", retrycount);
		schedule_delayed_work(&scp_tp_sendparam_work, msecs_to_jiffies(1000));
	}
}

/* ==================================================================== *
 *  IPI 接收回调（mtk_ipi_register 的 mbox_pin_cb_t：id/prdata/data/len）
 *  blob 0x2342c, 584B；printk 行 60/64/75/67/69/94
 *  switch(msg[0])，跳表 .rodata+0xb7a = {00,00,20,00,27,31}（base=0x23488）：
 *    0x11/0x12/0x14 -> 0x23488 手势数据；0x13 -> 0x23508 GETPARAM；
 *    0x15 -> 0x23524 ack；0x16 -> 0x2354c REQPARAM；0xf1 -> 0x234d8 TEST0
 * ==================================================================== */
static int scp_tp_ipi_handler(unsigned int ipi_id, void *prdata, void *data,
			      unsigned int len)
{
	u32 *msg = data;

	if (debug_log_level >= 3)
		ts_info("[scp-tp]: working status:%d, event:%d",
			scp_tp_param.param0, msg[0]);

	switch (msg[0]) {
	case 0x11:	/* 手势/FOD 数据（三个事件共用同一 body：跳表 0/1/3 项 = base） */
	case 0x12:
	case 0x14:
		/* blob 23488..234d4：ddr+0x28/+0x2c -> param+0x28/+0x2c，
		 * memcpy(&param+0x30, ddr+0x30, len)（fortify 上限 0x40 -> 只读不写超界） */
		scp_tp_param.gesture_type = *(u32 *)(ddr_virt_addr + 0x28);
		scp_tp_param.gesture_len = *(u32 *)(ddr_virt_addr + 0x2c);
		memcpy(scp_tp_param.gesture_data,
		       (void *)(ddr_virt_addr + 0x30),
		       scp_tp_param.gesture_len);
		queue_work(system_wq, &scp_tp_gesture_process_work);
		break;

	case 0x13:	/* CMD_SCP2AP_GETPARAM：SCP 已取走参数 */
		if (debug_log_level >= 3)
			ts_info("get CMD_SCP2AP_GETPARAM\n");
		scp_tp_param.param0 = 2;
		break;

	case 0x15:	/* ack：唤醒 scp_tp_switch 的等待 */
		wait_scp_ack = true;
		wake_up(&ap_wait_queue);
		break;

	case 0x16:	/* CMD_SCP2AP_REQPARAM：SCP 请求参数 */
		if (debug_log_level >= 3)
			ts_info("get CMD_SCP2AP_REQPARAM, cur_state=%d\n",
				scp_tp_param.param0);
		/* blob: param0-3 / cmn #3 / b.hi -> param0∈{1,2} 时跳过 */
		if (scp_tp_param.param0 == 1 || scp_tp_param.param0 == 2)
			break;
		if (debug_log_level >= 3)
			ts_info("send param in CMD_SCP2AP_REQPARAM");
		scp_tp_param.param0 = 1;
		schedule_delayed_work(&scp_tp_sendparam_work, 0);
		break;

	case 0xf1:	/* CMD_SCP2AP_TEST0（仅点名） */
		if (debug_log_level >= 3)
			ts_info("get CMD_SCP2AP_TEST0\n");
		break;

	default:
		if (debug_log_level >= 3)
			ts_info("unknow event from SCP: %*ph\n", len, data);
		break;
	}
	return 0;
}

/* ==================================================================== *
 *  AP/SCP 控制权切换（blob 0x236cc, 784B；printk 行 202/208/210/224/235）
 *  mode&1 == 1：交 SCP -> pinctrl touch_mode_scp + IPI{2,1,gesture,0}
 *  mode&1 == 0：收回 AP -> IPI{2,0,gesture,0} -> 等 ack(timeout 13 jiffies)
 *                          -> pinctrl touch_mode_ap
 *  说明：本函数 CFG 有多处 tail-merge（blob 2384c "scp not ready" 块被两条主路径
 *  共用、0x23960 错误打印被两处 pinctrl 失败共用），下面按块地址逐块写出等价语义；
 *  原源码 if/else 的具体措辞不可完全复原（[TODO-VERIFY]，见 recon §不确定点）。
 * ==================================================================== */
int scp_tp_switch(u32 mode)
{
	struct goodix_ts_core *core_data = get_goodix_core_data();
	int ret = 0;

	/* blob 23730..2374c：打印 (mode & 1, core_data->gesture_enabled[0x684]) */
	if (debug_log_level >= 3)
		ts_info("[scp-tp]: switch to %d, gesture=0x%x",
			mode & 0x1, core_data->gesture_enabled);

	if (mode & 0x1) {
		/* blob 23714..2372c：SCP 工作时间片 -> 置 3；否则落到 2384c */
		if (scp_tp_param.param0 >= 2)
			scp_tp_param.param0 = 3;
		else
			goto not_ready;
		goto to_scp;
	}

	/* ---- blob 23754：请求 SCP 交回（IPI{2,0,gesture,0}，内联 scp_tp_ipi_send；
	 *      blob 里该 IPI 的返回值只用于错误打印，不参与后续判断） */
	scp_tp_ipi_send(2, 0, (u32)core_data->gesture_enabled, 0);

	/* blob 23798：ack 已到？否则 wait_event_interruptible_timeout(13 jiffies) */
	if (wait_scp_ack) {
		if (debug_log_level >= 3)
			ts_info("wait scp ack back, switch to ap");
	} else {
		if (wait_event_interruptible_timeout(ap_wait_queue, wait_scp_ack,
						     msecs_to_jiffies(50)) <= 0) {
			if (debug_log_level)
				ts_err("scp_tp_ipi_send error, switch to ap");
		} else {
			if (debug_log_level >= 3)
				ts_info("wait scp ack back, switch to ap");
		}
	}

	/* ---- blob 2382c：state>1 -> 4 并落 AP 支路；否则 2384c */
	wait_scp_ack = false;
	if (scp_tp_param.param0 > 1) {
		scp_tp_param.param0 = 4;
		goto to_ap;
	}

not_ready:
	/* blob 2384c / 23930（tail-merge，两主路径共用） */
	if (debug_log_level)
		ts_err("scp not ready, don't change state");
	if (!(mode & 0x1))
		goto to_ap;

to_scp:
	/* blob 23858：touch_mode_scp + IPI{2,1,gesture,0} */
	ret = pinctrl_select_state(core_data->pinctrl,
				   core_data->pin_sta_touch_mode_scp);
	if (ret < 0)
		goto pin_fail;
	return scp_tp_ipi_send(2, 1, (u32)core_data->gesture_enabled, 0);

to_ap:
	/* blob 2394c：touch_mode_ap */
	ret = pinctrl_select_state(core_data->pinctrl,
				   core_data->pin_sta_touch_mode_ap);
	if (ret >= 0)
		return ret;

pin_fail:
	/* blob 23960 / 239b8：两处 pinctrl 失败共用（行 235） */
	if (debug_log_level)
		ts_err("Failed to select default pinstate, ret:%d", ret);
	return ret;
}

/* ==================================================================== *
 *  SCP ready/stop notifier（scp_A_register_notify 回调）
 *  blob 0x239e0, 404B；printk 行 149/154/158/162
 *  注意：与 focaltech 版不同构（focal 版无 core_data、STOP 支路改调
 *  focal_select_touchmode(0)）——见 recon §两份差异函数全文。
 * ==================================================================== */
static int scp_tp_scp_ready_notifier_call(struct notifier_block *nb,
					  unsigned long action, void *v)
{
	struct goodix_ts_core *core_data = get_goodix_core_data();	/* blob 239f8 */

	if (action == SCP_EVENT_READY) {
		/* blob 23aac */
		goodix_scp_tp_mistouch_close = 0;
		if (scp_tp_param.param0 != 1) {
			if (debug_log_level >= 3)
				ts_info("send param in SCP_EVENT_READY");
			scp_tp_param.param0 = 1;
			schedule_delayed_work(&scp_tp_sendparam_work, 0);
		}
		if (debug_log_level >= 3)
			ts_info("get SCP_EVENT_READY");
		return 0;
	}

	if (action == SCP_EVENT_STOP) {
		/* blob 23a08 */
		goodix_scp_tp_mistouch_close = 1;
		cancel_delayed_work(&scp_tp_sendparam_work);
		if (scp_tp_param.param0 == 3) {
			if (debug_log_level >= 3)
				ts_info("switch to ap in SCP_EVENT_STOP");
			/* blob 23a48/23a54/23a7c：pin 切回 AP + 开中断 +
			 * 打开 IRQ wake（hw_ops->irq_enable = +0x30 的间接调用） */
			pinctrl_select_state(core_data->pinctrl,
					     core_data->pin_sta_touch_mode_ap);
			core_data->hw_ops->irq_enable(core_data, true);
			irq_set_irq_wake(core_data->irq, 1);
		}
		if (debug_log_level >= 3)
			ts_info("get SCP_EVENT_STOP");
		return 0;
	}
	return 0;
}

/* ==================================================================== *
 *  IC 侧胶水（goodix_ts_gesture/gesture_10diff/debug）
 * ==================================================================== */

/*  blob 0x93a0, 156B
 *  SCP 接管期间 SCP 报手势 -> 在 work 上下文里重放 IC 手势解析并上报。
 *  blob 用全局 goodix_core_data（本文件用骨架的 get_goodix_core_data()）+
 *  core->work_status(+0x680) == TP_GESTURE(=1) + goodix_gesture_ist()==1 才上报；
 *  mutex 是 goodix_modules.mutex（骨架 struct goodix_module，+0x18）。
 *  qos 对象 core->pm_qos_req_irq（blob 偏移 0x5e0）。 */
void goodix_scp_gesture(void)
{
	struct goodix_ts_core *core_data = get_goodix_core_data();

	pm_stay_awake(core_data->bus->dev);			/* [core+0x138]->[+8] */
	cpu_latency_qos_add_request(&core_data->pm_qos_req_irq, 0);

	mutex_lock(&goodix_modules.mutex);
	if (core_data->work_status == TP_GESTURE &&
	    goodix_gesture_ist(core_data) == 1) {
		mutex_unlock(&goodix_modules.mutex);
		cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
	} else {
		mutex_unlock(&goodix_modules.mutex);
	}
	pm_relax(core_data->bus->dev);
}

/*  blob 0x16dcc, 180B；printk 行 344
 *  10diff 手势命令：hw_ops->send_cmd(cd, &cmd)，cmd = {00 00 05 13 <type==0> ...}
 *  （GOODIX_GESTURE_10DIFF_CMD = 0x13，len 5；data[0] = (type & 0xff) == 0）
 *  失败仅告警，恒返回 0（blob 16e4c: mov w0,wzr）。 */
int goodix_gesture_10diff_write(struct goodix_ts_core *cd, u8 type)
{
	struct goodix_ts_cmd cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.len = 5;				/* blob: strh #0x1305 -> len=5, cmd=0x13 */
	cmd.cmd = GOODIX_GESTURE_10DIFF_CMD;
	cmd.data[0] = ((type & 0xff) == 0) ? 1 : 0;

	ret = cd->hw_ops->send_cmd(cd, &cmd);
	if (ret) {
		if (debug_log_level)
			ts_err("[GESTURE CMD ERROR]failed send gesture_10diff cmd");
	}
	return 0;
}

/*  blob 0x16244, 248B；printk 行 62/65
 *  仅 debug_log_level >= 4 时才读 IC 调试信息：read(cd, 0x10218, buf 80B)，
 *  成功再 ts_debug 打明文（"GF_DEBUG: %*ph"）。blob 内无任何调用点（死代码），
 *  但符号为 GLOBAL，重建保留以便按需接线。 */
void goodix_readprint_debuginfo(struct goodix_ts_core *cd)
{
	u8 buf[80];
	int ret;

	if (debug_log_level < 4)
		return;

	memset(buf, 0, sizeof(buf));
	ret = cd->hw_ops->read(cd, 0x10218, buf, sizeof(buf));
	if (ret) {
		if (debug_log_level)
			ts_err("failed get debug info\n");
	} else {
		if (debug_log_level >= 4)
			ts_debug("GF_DEBUG: %*ph\n", (int)sizeof(buf), buf);
	}
}
