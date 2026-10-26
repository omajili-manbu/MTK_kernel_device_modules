/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XIAOMI_MI_SCHEDULE_H
#define _XIAOMI_MI_SCHEDULE_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>

struct task_struct;

#define MI_TASK_OEM_MAGIC 0xabcdabcdacbdadbcULL
#define MI_STATE_MAGIC 0xacbdacbdULL
#define MI_TASK_STATE_SIZE 0x4e8
/* Unnamed stock fields retain their offsets instead of invented semantics. */
#define MI_FIELD(object, type, offset) \
	(*(type *)((u8 *)(object) + (offset)))

/* Provider initialization: mi_task_fork .text+0x241c..0x2580. The layout is
 * the cross-module stock contract consumed by metis (flags_0xc0, deadlines,
 * lock back-pointers); unnamed storage keeps its offsets. The per-UID user
 * accounting struct and the private per-CPU mi_rq are dropped with their
 * dead accounting chain. */
struct mi_task_struct {
	union {
		u8 raw[MI_TASK_STATE_SIZE];
		struct {
			u64 magic;                     /* 0x000 */
			u64 guard;                     /* 0x008 */
			u8 unknown_010[0xa8];
			u8 user_linked;                /* 0x0b8 */
			u8 work_pending;               /* 0x0b9 */
			u8 unknown_ba;
			u8 unknown_bb;
			u32 uid;                       /* 0x0bc */
			u32 flags_0xc0;
			u32 unknown_c4;
			u32 unknown_c8;
			u32 unknown_cc;
			u64 initial_exec_runtime;      /* 0x0d0 */
			u64 unknown_d8;
			struct task_struct *task;      /* 0x0e0 */
			u8 unknown_0e8[0x28];
			u64 sup_runtime[6];            /* 0x110 */
			u64 big_runtime[6];            /* 0x140 */
			u64 little_runtime[6];         /* 0x170 */
			u8 unknown_1a0[0xb8];
			struct rb_node rb_node_258;
			u32 rb_magic_270;
			u32 unknown_274;
			u8 unknown_278[0x18];
			u64 deadline_0x290;
			u8 unknown_298[0x28];
			struct list_head node_2c0;
			u8 unknown_2d0[0x218];
		};
	};
};

struct mi_task_struct *get_mi_task_struct(struct task_struct *task);
bool mi_task_exiting(struct task_struct *task);
int get_cluster_type(unsigned int cpu);
u32 is_32B_user(int uid);
#endif
