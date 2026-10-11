/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */

/*
 * rodin stage1: this file is intentionally empty.  Its former content (the
 * vendor SMI family helper types and hooks) never reached any consumer: the
 * kernel tree's same-named header wins the include race, and both files
 * shared the MTK_IOMMU_SMI_H guard.  The content now lives in
 * include/soc/mediatek/mtk_smi_internal.h, which the SMI family includes
 * explicitly.  The file is kept so that tooling or old scripts referencing
 * the path do not break.
 */
