/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 *
 * rodin =y 重建（T2①，#164）：按 blob 真值只保留 binder_prio_proc_transaction_finish
 * 一个事件（blob def 面四件套唯一事件；dash 头里的 binder_prio_restore /
 * binder_prio_proc_default_prio 两事件在 rodin blob 代不存在，不落）。
 * 6.18 适配：__assign_str 单参化（6.18 stage6_event_callback.h 契约）。
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM binder_prio

#if !defined(_BINDER_PRIO_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _BINDER_PRIO_TRACE_H_

#include <linux/tracepoint.h>

TRACE_EVENT(binder_prio_proc_transaction_finish,
	TP_PROTO(char *msg),

	TP_ARGS(msg),

	TP_STRUCT__entry(
		__string(msg, msg)
	),

	TP_fast_assign(
		__assign_str(msg);
	),

	TP_printk("%s", __get_str(msg))
);

#endif /* _BINDER_PRIO_TRACE_H_ */

/* 源文件随模块目录内建（devmod drivers/staging/binder_prio），经 Makefile
 * ccflags -I$(src) 命中；dash 的 ../../ 相对路径在本树布局不可复现。 */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE binder_prio_trace
#include <trace/define_trace.h>
