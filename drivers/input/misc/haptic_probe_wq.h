/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HAPTIC_PROBE_WQ_H
#define _HAPTIC_PROBE_WQ_H

struct work_struct;

/*
 * 马达三供应商驱动（aw/si/aac）探针注册串行队列入口：入队即按 vseq 表序
 * 串行执行，前一 probe 结束后才放行下一个，详见 haptic_probe_wq.c。
 */
void haptic_probe_queue_work(struct work_struct *work);

#endif /* _HAPTIC_PROBE_WQ_H */
