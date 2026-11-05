// SPDX-License-Identifier: GPL-2.0
/*
 * 马达三供应商驱动（aw/si/aac）共享的探针注册串行队列。
 *
 * 三家 i2c 器件在 DT 互为占位且共用 pio21(irq)/pio22(reset)，本机仅 aac@0x5F
 * 有实体器件。并发注册时无器件方 probe 会先抢双脚进入 i2c NACK 重试窗
 * （aw ~40ms / si ~25ms），另两家在窗内 gpio/pinctrl 申请 EBUSY 出局且无重探，
 * 马达整次开机失联。ordered 队列 max_active=1 全串行：按 vseq 表序入队即按
 * aw→si→aac 依次执行，前一 probe 结束（含失败释放）才放行下一个，
 * vseq 重放线立即返回的时序收益不变。
 */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/workqueue.h>

static struct workqueue_struct *haptic_probe_wq;

void haptic_probe_queue_work(struct work_struct *work)
{
	/* 队列建立失败的降级路径：退回系统队列并发注册 */
	if (unlikely(!haptic_probe_wq)) {
		schedule_work(work);
		return;
	}

	queue_work(haptic_probe_wq, work);
}
EXPORT_SYMBOL_GPL(haptic_probe_queue_work);

static int __init haptic_probe_wq_init(void)
{
	haptic_probe_wq = alloc_ordered_workqueue("haptic_probe_wq", 0);

	return haptic_probe_wq ? 0 : -ENOMEM;
}
fs_initcall(haptic_probe_wq_init);
