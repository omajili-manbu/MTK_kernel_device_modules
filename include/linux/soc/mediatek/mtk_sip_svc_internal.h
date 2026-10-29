/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

/*
 * rodin (SMI/IOMMU fixwave): vendor SIP SMC-id internal header.
 *
 * The vendor include/linux/soc/mediatek/mtk_sip_svc.h never reached any
 * consumer: kbuild puts the kernel ./include before every vendor -I path, so
 * <linux/soc/mediatek/mtk_sip_svc.h> always resolved to the kernel tree's
 * header, which is a strict subset (DVFSRC VCOREFS + KERNEL_IOMMU_CONTROL
 * only).  Every vendor-only SMC id was therefore unreachable and the build
 * face failed with "use of undeclared identifier MTK_SIP_..." across the
 * SCP/LPM/CCCI/CMDQ/ADSP/AUDIO/VCP/APUSYS/... consumers.
 *
 * Layout: the kernel header is included first, so every same-name symbol
 * keeps the kernel value (both copies were preprocessed and diffed: the 8
 * common macros are token-identical, incl. MTK_SIP_DVFSRC_VCOREFS_CONTROL =
 * MTK_SIP_SMC_CMD(0x506); MTK_IOMMU_SECURE_CONTROL below even carries the
 * same 0x514 as the kernel-only MTK_SIP_KERNEL_IOMMU_CONTROL name).  Each
 * vendor-only id is then carried over verbatim from the vendor header, in
 * the vendor file's order, behind its own #ifndef guard.
 *
 * Vendor sources needing a vendor-only SMC id must include this header, not
 * <linux/soc/mediatek/mtk_sip_svc.h>.
 */
#ifndef MTK_SIP_SVC_INTERNAL_H
#define MTK_SIP_SVC_INTERNAL_H

#include <linux/kconfig.h>			/* IS_ENABLED() for the GIC dump id */
#include <linux/soc/mediatek/mtk_sip_svc.h>	/* kernel copy: common ids (subset) */

/* Vendor-only ids follow, verbatim from the vendor mtk_sip_svc.h. */

#ifndef MTK_SIP_EMIMPU_CONTROL
#define MTK_SIP_EMIMPU_CONTROL \
	MTK_SIP_SMC_CMD(0x50B)
#endif
#ifndef MTK_SIP_SDA_CONTROL
#define MTK_SIP_SDA_CONTROL \
	MTK_SIP_SMC_CMD(0x525)
#endif
#ifndef MTK_SIP_KERNEL_GIC_DUMP
#if IS_ENABLED(CONFIG_MTK_IRQ_DBG_LEGACY)
#define MTK_SIP_KERNEL_GIC_DUMP \
	MTK_SIP_SMC_CMD(0x201)
#elif IS_ENABLED(CONFIG_MTK_IRQ_DBG)
#define MTK_SIP_KERNEL_GIC_DUMP \
	MTK_SIP_SMC_CMD(0x526)
#endif
#endif
#ifndef MTK_SIP_KERNEL_WDT
#define MTK_SIP_KERNEL_WDT \
	MTK_SIP_SMC_CMD(0x200)
#endif
#ifndef MTK_SIP_KERNEL_TIME_SYNC
#define MTK_SIP_KERNEL_TIME_SYNC \
	MTK_SIP_SMC_CMD(0x202)
#endif
#ifndef MTK_SIP_KERNEL_ATF_DEBUG
#define MTK_SIP_KERNEL_ATF_DEBUG \
	MTK_SIP_SMC_CMD(0x204)
#endif
#ifndef MTK_SIP_KERNEL_CCCI_GET_INFO
#define MTK_SIP_KERNEL_CCCI_GET_INFO \
	MTK_SIP_SMC_CMD(0x206)
#endif
#ifndef MTK_SIP_KERNEL_DCM
#define MTK_SIP_KERNEL_DCM \
	MTK_SIP_SMC_CMD(0x230)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_FREE_ADDR
#define MTK_SIP_KERNEL_AMMS_GET_FREE_ADDR \
	MTK_SIP_SMC_CMD(0x250)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_FREE_LENGTH
#define MTK_SIP_KERNEL_AMMS_GET_FREE_LENGTH \
	MTK_SIP_SMC_CMD(0x251)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_PENDING
#define MTK_SIP_KERNEL_AMMS_GET_PENDING \
	MTK_SIP_SMC_CMD(0x252)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_ACK_PENDING
#define MTK_SIP_KERNEL_AMMS_ACK_PENDING \
	MTK_SIP_SMC_CMD(0x253)
#endif
#ifndef MTK_SIP_KERNEL_AMMS_GET_SEQ_ID
#define MTK_SIP_KERNEL_AMMS_GET_SEQ_ID \
	MTK_SIP_SMC_CMD(0x258)
#endif
#ifndef MTK_SIP_KERNEL_DEVMPU_VIO_GET
#define MTK_SIP_KERNEL_DEVMPU_VIO_GET \
	MTK_SIP_SMC_CMD(0x264)
#endif
#ifndef MTK_SIP_KERNEL_DEVMPU_PERM_GET
#define MTK_SIP_KERNEL_DEVMPU_PERM_GET \
	MTK_SIP_SMC_CMD(0x265)
#endif
#ifndef MTK_SIP_KERNEL_DEVMPU_VIO_CLR
#define MTK_SIP_KERNEL_DEVMPU_VIO_CLR \
	MTK_SIP_SMC_CMD(0x268)
#endif
#ifndef MTK_SIP_KERNEL_GET_RND
#define MTK_SIP_KERNEL_GET_RND \
	MTK_SIP_SMC_CMD(0x26A)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_PERM_GET
#define MTK_SIP_KERNEL_DAPC_PERM_GET \
	MTK_SIP_SMC_CMD(0x26B)
#endif
#ifndef MTK_SIP_KERNEL_CLR_SRAMROM_VIO
#define MTK_SIP_KERNEL_CLR_SRAMROM_VIO \
	MTK_SIP_SMC_CMD(0x26C)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_CAM_CONTROL
#define MTK_SIP_KERNEL_DAPC_CAM_CONTROL \
	MTK_SIP_SMC_CMD(0x52D)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_MMUP_CONTROL
#define MTK_SIP_KERNEL_DAPC_MMUP_CONTROL \
	MTK_SIP_SMC_CMD(0x52E)
#endif
#ifndef MTK_SIP_KERNEL_DAPC_SUBSYS_GET
#define MTK_SIP_KERNEL_DAPC_SUBSYS_GET \
	MTK_SIP_SMC_CMD(0x531)
#endif
#ifndef MTK_SIP_KERNEL_CCCI_CONTROL
#define MTK_SIP_KERNEL_CCCI_CONTROL \
	MTK_SIP_SMC_CMD(0x505)
#endif
#ifndef MTK_SIP_AUDIO_CONTROL
#define MTK_SIP_AUDIO_CONTROL \
	MTK_SIP_SMC_CMD(0x517)
#endif
#ifndef MTK_SIP_CMDQ_CONTROL
#define MTK_SIP_CMDQ_CONTROL \
	MTK_SIP_SMC_CMD(0x518)
#endif
#ifndef MTK_SIP_APUSYS_MNOC_CONTROL
#define MTK_SIP_APUSYS_MNOC_CONTROL \
	MTK_SIP_SMC_CMD(0x519)
#endif
#ifndef MTK_SIP_IMGSYS_CONTROL
#define MTK_SIP_IMGSYS_CONTROL \
	MTK_SIP_SMC_CMD(0x532)
#endif
#ifndef MTK_SIP_AIE_CONTROL
#define MTK_SIP_AIE_CONTROL \
	MTK_SIP_SMC_CMD(0x53B)
#endif
#ifndef MTK_SIP_MTK_LPM_CONTROL
#define MTK_SIP_MTK_LPM_CONTROL \
	MTK_SIP_SMC_CMD(0x507)
#endif
#ifndef MTK_SIP_MTK_SSC_CONTROL
#define MTK_SIP_MTK_SSC_CONTROL \
	MTK_SIP_SMC_CMD(0x529)
#endif
#ifndef MTK_SIP_MMSRAM_CONTROL
#define MTK_SIP_MMSRAM_CONTROL \
	MTK_SIP_SMC_CMD(0x51D)
#endif
#ifndef MTK_SIP_APUSYS_CONTROL
#define MTK_SIP_APUSYS_CONTROL \
	MTK_SIP_SMC_CMD(0x51E)
#endif
#ifndef MTK_SIP_SCP_DVFS_CONTROL
#define MTK_SIP_SCP_DVFS_CONTROL \
	MTK_SIP_SMC_CMD(0x232)
#endif
#ifndef MTK_IOMMU_SECURE_CONTROL
#define MTK_IOMMU_SECURE_CONTROL \
	MTK_SIP_SMC_CMD(0x514)
#endif
#ifndef MTK_SIP_APUPWR_CONTROL
#define MTK_SIP_APUPWR_CONTROL \
	MTK_SIP_SMC_CMD(0x526)
#endif
#ifndef MTK_SIP_KERNEL_USB_CONTROL
#define MTK_SIP_KERNEL_USB_CONTROL \
	MTK_SIP_SMC_CMD(0x527)
#endif
#ifndef MTK_SIP_KERNEL_CCU_CONTROL
#define MTK_SIP_KERNEL_CCU_CONTROL	\
	MTK_SIP_SMC_CMD(0x52A)
#endif
#ifndef MTK_SIP_KERNEL_ADSP_CONTROL
#define MTK_SIP_KERNEL_ADSP_CONTROL \
	MTK_SIP_SMC_CMD(0x52B)
#endif
#ifndef MTK_SIP_TINYSYS_SCP_CONTROL
#define MTK_SIP_TINYSYS_SCP_CONTROL \
	MTK_SIP_SMC_CMD(0x528)
#endif
#ifndef MTK_SIP_TINYSYS_VCP_CONTROL
#define MTK_SIP_TINYSYS_VCP_CONTROL \
		MTK_SIP_SMC_CMD(0x52C)
#endif
#ifndef MTK_SIP_KERNEL_PCIE_CONTROL
#define MTK_SIP_KERNEL_PCIE_CONTROL \
	MTK_SIP_SMC_CMD(0x52F)
#endif
#ifndef MTK_SIP_KERNEL_GPUEB_CONTROL
#define MTK_SIP_KERNEL_GPUEB_CONTROL \
	MTK_SIP_SMC_CMD(0x530)
#endif
#ifndef MTK_SIP_KERNEL_CONNSYS_CONTROL
#define MTK_SIP_KERNEL_CONNSYS_CONTROL \
	MTK_SIP_SMC_CMD(0x534)
#endif
#ifndef MTK_SIP_KERNEL_WLAN_CONTROL
#define MTK_SIP_KERNEL_WLAN_CONTROL \
	MTK_SIP_SMC_CMD(0x535)
#endif
#ifndef MTK_SIP_KERNEL_BT_CONTROL
#define MTK_SIP_KERNEL_BT_CONTROL \
	MTK_SIP_SMC_CMD(0x536)
#endif
#ifndef MTK_SIP_KERNEL_GPS_CONTROL
#define MTK_SIP_KERNEL_GPS_CONTROL \
	MTK_SIP_SMC_CMD(0x537)
#endif
#ifndef MTK_SIP_KERNEL_FM_CONTROL
#define MTK_SIP_KERNEL_FM_CONTROL \
	MTK_SIP_SMC_CMD(0x538)
#endif
#ifndef MTK_SIP_KERNEL_RGU_CONTROL
#define MTK_SIP_KERNEL_RGU_CONTROL \
	MTK_SIP_SMC_CMD(0x53A)
#endif
#ifndef MTK_SIP_TINYSYS_SSPM_CONTROL
#define MTK_SIP_TINYSYS_SSPM_CONTROL \
	MTK_SIP_SMC_CMD(0x53C)
#endif
#ifndef MTK_SIP_KERNEL_ISE_CONTROL
#define MTK_SIP_KERNEL_ISE_CONTROL \
	MTK_SIP_SMC_CMD(0x53D)
#endif
#ifndef MTK_SIP_KERNEL_SLBC_CONTROL
#define MTK_SIP_KERNEL_SLBC_CONTROL \
	MTK_SIP_SMC_CMD(0x53E)
#endif
#ifndef MTK_SIP_KERNEL_HWCCF_CONTROL
#define MTK_SIP_KERNEL_HWCCF_CONTROL \
	MTK_SIP_SMC_CMD(0x540)
#endif
#ifndef MTK_SIP_KERNEL_ISP_CONTROL
#define MTK_SIP_KERNEL_ISP_CONTROL \
	MTK_SIP_SMC_CMD(0x541)
#endif
#ifndef MTK_SIP_KERNEL_SPI_CONTROL
#define MTK_SIP_KERNEL_SPI_CONTROL \
	MTK_SIP_SMC_CMD(0x542)
#endif
#ifndef MTK_SIP_HYP_PKVM_CONTROL
#define MTK_SIP_HYP_PKVM_CONTROL \
	MTK_SIP_SMC_CMD(0x834)
#endif

#endif /* MTK_SIP_SVC_INTERNAL_H */
