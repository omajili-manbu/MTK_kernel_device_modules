/* SPDX-License-Identifier: GPL-2.0 */
/* rodin r25: vendor SMC call ids live in the vendor kernel's mtk_sip_svc.h on
 * 6.6; the 6.18 upstream header resolves first and lacks them, so keep them
 * here (values from MTK_kernel_device_modules include/linux/soc/mediatek). */
#ifndef _RODIN_SIP_66_H
#define _RODIN_SIP_66_H

/* SDA */
#define MTK_SIP_SDA_CONTROL \
	MTK_SIP_SMC_CMD(0x525)

#define MTK_SIP_KERNEL_GIC_DUMP \
	MTK_SIP_SMC_CMD(0x526)

/* Debug feature and ATF related (aed) */
#define MTK_SIP_KERNEL_WDT \
	MTK_SIP_SMC_CMD(0x200)

#endif
