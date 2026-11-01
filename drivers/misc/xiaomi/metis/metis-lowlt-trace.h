/* SPDX-License-Identifier: GPL-2.0-only */
/* rodin 6.18：stock 本头自带 TRACE_EVENT(tracing_mark_write)，与 mtk
 * apu/mml/mali 三家同名事件符号冲突（且 6.18 无内核 systrace 事件）；
 * 事件族已并入 metis-final-trace.h（systrace 件更名
 * metis_tracing_mark_write），本头保留为低 LT 面的兼容入口。 */
#ifndef _XIAOMI_METIS_LOWLT_TRACE_H
#define _XIAOMI_METIS_LOWLT_TRACE_H
#include "metis-final-trace.h"
#endif
