/* SPDX-License-Identifier: GPL-2.0 */
/* rodin stage2: the 6.18 kernel mtk_sip_svc.h shadows the vendor copy and
 * lacks the EMI MPU SIP id used by the whole EMI family. Define it here with
 * the vendor value (r25 rodin_sip_66.h precedent; vendor
 * include/linux/soc/mediatek/mtk_sip_svc.h -> MTK_SIP_SMC_CMD(0x50B)). */
#ifndef RODIN_SIP_EMIMPU_H
#define RODIN_SIP_EMIMPU_H

#include <linux/soc/mediatek/mtk_sip_svc.h>

#ifndef MTK_SIP_EMIMPU_CONTROL
#define MTK_SIP_EMIMPU_CONTROL		MTK_SIP_SMC_CMD(0x50B)
#endif

#endif /* RODIN_SIP_EMIMPU_H */
