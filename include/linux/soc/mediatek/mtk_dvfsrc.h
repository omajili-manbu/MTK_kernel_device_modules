/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (c) 2018 MediaTek Inc.
 */
#ifndef __SOC_MTK_DVFSRC_H
#define __SOC_MTK_DVFSRC_H

/*
 * rodin b247（分区 B · 6.18 对齐）：vendor × 内核同名实现 → 链接期 duplicate symbol。
 * 内核树 drivers/soc/mediatek/mtk-dvfsrc.c（纯上游）也定义并导出
 * mtk_dvfsrc_send_request / mtk_dvfsrc_query_info，其 of_match 只覆盖
 * mt6893/mt8183/mt8195（mt6899 节点匹配不到 ⇒ 对 rodin 是死代码，但 .o 在
 * vmlinux.a 里照样与 vendor 这份撞名）；且两侧的 cmd 编号体系完全不同
 * （内核 = enum mtk_dvfsrc_cmd，vendor = MTK_DVFSRC_CMD_*_REQUEST），
 * 绝不可让任何一方落到对方实现。内核树零改动 ⇒ vendor 侧改名分流：
 * 本树这两个符号改为 *_rodin，两侧并存、各用各的实现。
 * 覆盖面 = 全部引用方都包含本头（mtk-dvfsrc-regulator/-sysfs/-helper/-start、
 * mtk-dvfsrc.c、mtk-emibus-icc.c、mtk-emi.c、mtk-dvfsrc-devfreq.c）⇒ 此处即
 * 唯一改名点；收尾必 grep 旧名残留（新名在 vmlinux 里应只由 vendor 侧提供）。
 */
#define mtk_dvfsrc_send_request		mtk_dvfsrc_send_request_rodin
#define mtk_dvfsrc_query_info		mtk_dvfsrc_query_info_rodin


#define MTK_DVFSRC_CMD_BW_REQUEST	0
#define MTK_DVFSRC_CMD_OPP_REQUEST	1
#define MTK_DVFSRC_CMD_VCORE_REQUEST	2
#define MTK_DVFSRC_CMD_DRAM_REQUEST	3
#define MTK_DVFSRC_CMD_HRTBW_REQUEST	4
#define MTK_DVFSRC_CMD_VSCP_REQUEST	5
#define MTK_DVFSRC_CMD_PEAK_BW_REQUEST	6
#define MTK_DVFSRC_CMD_EMICLK_REQUEST	7

#define MTK_DVFSRC_CMD_FORCEOPP_REQUEST	16


#define MTK_DVFSRC_CMD_VCORE_LEVEL_QUERY 0
#define MTK_DVFSRC_CMD_VSCP_LEVEL_QUERY	1
#define MTK_DVFSRC_CMD_DRAM_LEVEL_QUERY	2
#define MTK_DVFSRC_CMD_CURR_LEVEL_QUERY	3

#if IS_ENABLED(CONFIG_MTK_DVFSRC)
void mtk_dvfsrc_send_request(const struct device *dev, u32 cmd, u64 data);
int mtk_dvfsrc_query_info(const struct device *dev, u32 cmd, int *data);
#else
static inline void mtk_dvfsrc_send_request(const struct device *dev, u32 cmd,
					   u64 data)
{ }
static inline int mtk_dvfsrc_query_info(const struct device *dev, u32 cmd,
					int *data)
{ return -ENODEV; }

#endif /* CONFIG_MTK_DVFSRC */

#endif
