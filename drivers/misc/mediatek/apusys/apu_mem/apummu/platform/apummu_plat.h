/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 MediaTek Inc.
 */


#ifndef __APUSYS_APUMMU_PLAT_H__
#define __APUSYS_APUMMU_PLAT_H__

#include <linux/platform_device.h>	/* rodin 4-8: 6.18 头瘦身，struct platform_device 不完整 */
/* apummu paltform data */
struct apummu_plat {
	unsigned int slb_wait_time;
	bool is_general_SLB_support;
	bool alloc_DRAM_FB_in_session_create;
};

int apummu_plat_init(struct platform_device *pdev);

#endif
