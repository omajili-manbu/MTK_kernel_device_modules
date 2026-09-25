/* SPDX-License-Identifier: GPL-2.0 */
/* rodin stage2: kernel mtk_sip_svc.h shadows the vendor copy and lacks the
 * SDA SIP id; define locally with the vendor value (rodin_sip_66.h r25
 * precedent, vendor mtk_sip_svc.h 0x525). */
#ifndef RODIN_SIP_SDA_H
#define RODIN_SIP_SDA_H

#include <linux/soc/mediatek/mtk_sip_svc.h>

#ifndef MTK_SIP_SDA_CONTROL
#define MTK_SIP_SDA_CONTROL		MTK_SIP_SMC_CMD(0x525)
#endif

#endif /* RODIN_SIP_SDA_H */
