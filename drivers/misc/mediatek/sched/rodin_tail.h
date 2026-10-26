/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rodin GKI 动态 task_struct 尾部双段布局声明（vendor 树内共享头）。
 *   [0, sizeof(struct mtk_task)) = struct mtk_task（sched 模块，见 common.h）
 *   [sizeof(mtk_task), +16)      = struct rodin_mi_task_slot（mi_schedule 模块）
 * 消费者：sched/common.h（mtk_task_of）、mi_schedule.c、metis/metis-vip-queue.c。
 * 只依赖 <linux/sched/task.h>（android_task_vendor_data），不依赖内核私有
 * kernel/sched/sched.h —— 独立构建单元可安全引入。
 */
#ifndef _RODIN_TAIL_H
#define _RODIN_TAIL_H

#include <linux/sched/task.h>
#include <linux/types.h>

/* mtk_task 尺寸常量：common.h 侧 static_assert(sizeof(struct mtk_task) ==
 * RODIN_TAIL_MTK_SIZE) 双向锁定；此处不 include common.h（私有头链）。 */
#define RODIN_TAIL_MTK_SIZE 352

struct rodin_mi_task_slot {
	u64 magic;	/* MI_TASK_OEM_MAGIC = 有效标志 */
	u64 ptr;	/* struct mi_task_struct *（mi_schedule 自有 cachep） */
};

static __always_inline struct rodin_mi_task_slot *mi_task_slot_of(
	const struct task_struct *p)
{
	return (struct rodin_mi_task_slot *)
		((char *)android_task_vendor_data((struct task_struct *)p) +
		 RODIN_TAIL_MTK_SIZE);
}

static __always_inline bool mi_task_slot_ok(void)
{
	extern int arch_task_struct_size;

	return arch_task_struct_size >=
	       (int)(sizeof(struct task_struct) + RODIN_TAIL_MTK_SIZE +
		     sizeof(struct rodin_mi_task_slot));
}

#endif /* _RODIN_TAIL_H */
