/* SPDX-License-Identifier: GPL-2.0 */
/* rodin stage2: kernel mtk_sip_svc.h shadows the vendor copy and lacks the
 * KERNEL DCM SIP id; define locally with the vendor value
 * (vendor include/linux/soc/mediatek/mtk_sip_svc.h 0x230, rodin_sip_66.h
 * r25 precedent). */
#ifndef RODIN_SIP_DCM_H
#define RODIN_SIP_DCM_H

#include <linux/soc/mediatek/mtk_sip_svc.h>

#ifndef MTK_SIP_KERNEL_DCM
#define MTK_SIP_KERNEL_DCM		MTK_SIP_SMC_CMD(0x230)
#endif

#endif /* RODIN_SIP_DCM_H */
