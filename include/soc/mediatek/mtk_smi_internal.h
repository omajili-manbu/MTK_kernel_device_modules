/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */

/*
 * rodin stage1 (SMI/IOMMU align): vendor SMI family internal header.
 *
 * The vendor include/soc/mediatek/smi.h never reached its consumers: kbuild
 * puts the kernel ./include before every vendor -I path, and both files
 * carried the same MTK_IOMMU_SMI_H guard, so every <soc/mediatek/smi.h>
 * include resolved to the kernel tree's 30-line upstream header (whose
 * content is additionally gated behind CONFIG_MTK_SMI=n).  The vendor
 * content now lives here, on a non-colliding path with its own guard; the
 * legacy file is kept as an empty shell.  Vendor sources must include this
 * header, not <soc/mediatek/smi.h>.
 */
#ifndef MTK_SMI_INTERNAL_H
#define MTK_SMI_INTERNAL_H

#include <linux/bitops.h>
#include <linux/device.h>
#include <dt-bindings/memory/mtk-smi-user.h>

/*
 * rodin stage1: vendor dt-bindings increment.  The kernel tree's
 * mtk-memory-port.h (which wins the -I race) caps MTK_LARB_NR_MAX at 32 and
 * lacks the multi-domain M4U extensions; vendor semantics need 64 (the larb
 * scan loops and every larb table are bounded by it).
 * The #undef/#define pair must stay AFTER the include below: above it, the
 * header's own define would trip -Wmacro-redefined (-Werror).
 */
#include <dt-bindings/memory/mtk-memory-port.h>

#undef MTK_LARB_NR_MAX
#define MTK_LARB_NR_MAX			64

#ifndef MTK_M4U_DOM_NR_MAX
#define MTK_M4U_DOM_NR_MAX		16
#endif
#ifndef MTK_M4U_TAB_NR_MAX
#define MTK_M4U_TAB_NR_MAX		3
#endif
#ifndef TAB_ID
#define TAB_ID				(0)
#endif
#ifndef MTK_M4U_PORT_ID
#define MTK_M4U_PORT_ID(tab, dom, larb, port)	(((tab & 0x3) << 20) | ((dom & 0xf) << 16) |\
						((larb & 0x3f) << 5) | (port & 0x1f))
#endif
#ifndef MTK_M4U_DOM_ID
#define MTK_M4U_DOM_ID(dom, larb, port)	MTK_M4U_PORT_ID(TAB_ID, dom, larb, port)
#endif
#ifndef MTK_M4U_TO_DOM
#define MTK_M4U_TO_DOM(id)		(((id) >> 16) & 0xf)
#endif
#ifndef MTK_M4U_TO_TAB
#define MTK_M4U_TO_TAB(id)		(((id) >> 20) & 0x3)
#endif

struct mtk_smi_lock {
	spinlock_t lock;
	unsigned long flags;
};

enum smi_user {
	SMI_MMINFRA = 0,
	SMI_VENC,
	SMI_VDEC,
	SMI_DISP,
	SMI_MML,
	SMI_DIP1,
	SMI_TRAW,
	SMI_USER_NR,
};

enum smi_real_time_type {
	ALWAYS_HRT = 0,
	ALWAYS_SRT,
	HRT_SRT_SWITCH,
	NON_APMCU,
};

extern struct mtk_smi_lock smi_lock;

#if IS_ENABLED(CONFIG_DEVICE_MODULES_MTK_SMI)

/*
 * rodin stage1: the layout must stay field-by-field identical to the
 * kernel-tree copy and to both vendor fallbacks (bank[32], sizeof 48).  The
 * guard makes the first definition win, so mtk_iommu.h / mtk-smmu-v3.h
 * fallbacks never define a second copy in the same TU.
 */
#ifndef MTK_SMI_LARB_IOMMU_RODIN_FALLBACK
#define MTK_SMI_LARB_IOMMU_RODIN_FALLBACK
struct mtk_smi_larb_iommu {
	struct device *dev;
	unsigned int   mmu;
	unsigned char  bank[32];
};
#endif

/* Own guard: the fallback in mtk_iommu.h defines the same macro. */
#ifndef MTK_SMI_MMU_EN
#define MTK_SMI_MMU_EN(port)		BIT(port)
#endif

#define TRIGGER_SMI_HANG_DETECT		(0xff)
#define TRIGGER_SMI_FORCE_ALL_ON	(0xfe)
#define TRIGGER_SMI_FORCE_ALL_PUT	(0xfd)

int mtk_smi_driver_register_notifier(struct notifier_block *nb);
int mtk_smi_driver_unregister_notifier(struct notifier_block *nb);
void mtk_smi_common_bw_set(struct device *dev, const u32 port, const u32 val);
void mtk_smi_common_ostdl_set(struct device *dev, const u32 port, bool is_write, const u32 val);
void mtk_smi_larb_bw_set(struct device *dev, const u32 port, const u32 val);
void mtk_smi_set_hrt_perm(struct device *dev, const u32 port, bool is_hrt);
int mtk_smi_larb_bw_thr(struct device *larbdev, const u32 port, bool is_bw_thr);
void mtk_smi_larb_port_dis_ultra(struct device *dev, const u32 port, bool is_dis_ultra);
s32 mtk_smi_dbg_hang_detect(char *user);
void mtk_smi_dbg_hang_detect_force_dump(char *user, u64 larb_skip_id, u64 comm_skip_id);
void mtk_smi_dbg_dump_for_isp_fast(u32 isp_id);
void mtk_smi_dbg_dump_for_disp(void);
void mtk_smi_dbg_dump_for_mml(void);
void mtk_smi_dbg_dump_for_venc(void);
void mtk_smi_dbg_dump_for_vdec(void);
void mtk_smi_dbg_dump_for_mminfra(void);
void mtk_smi_init_power_off(void);
void mtk_smi_dump_last_pd(const char *user);
void mtk_smi_larb_clamp_and_lock(struct device *larbdev, bool on);
s32 smi_sysram_enable(struct device *larbdev, const u32 master_id,
			const bool enable, const char *user);
s32 mtk_smi_sysram_set(struct device *larbdev, const u32 master_id,
			u32 set_val, const char *user);
s32 mtk_smi_dbg_cg_status(void);
void mtk_smi_check_comm_ref_cnt(struct device *dev);
void mtk_smi_check_larb_ref_cnt(struct device *dev);
int mtk_smi_larb_ultra_dis(struct device *larbdev, bool is_dis);
int mtk_smi_larb_enable(struct device *larbdev, u32 smi_user_id);
int mtk_smi_larb_disable(struct device *larbdev, u32 smi_user_id);
s32 mtk_smi_golden_set(bool enable, bool is_larb, u32 id, u32 port);
int smi_ut_dump_get(const char *val, const struct kernel_param *kp);
int mtk_smi_set_ostdl_type(struct device *larbdev, u32 ostdl_type);
int mtk_smi_set_larb_value(struct device *larbdev, u32 offset, u32 value);
int mtk_smi_set_comm_value(struct device *dev, u32 offset, u32 value);
int mtk_smi_clear_larb_set_value(struct device *larbdev);
int mtk_smi_clear_comm_set_value(struct device *dev);
int mtk_smi_dump_all_setting(struct device *dev, bool is_larb);
#else

static inline int mtk_smi_driver_register_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline int mtk_smi_driver_unregister_notifier(struct notifier_block *nb)
{
	return 0;
}

static inline void
mtk_smi_common_bw_set(struct device *dev, const u32 port, const u32 val) { }
static inline void
mtk_smi_common_ostdl_set(struct device *dev, const u32 port, bool is_write, const u32 val) { }
static inline void
mtk_smi_larb_bw_set(struct device *dev, const u32 port, const u32 val) { }
static inline void
mtk_smi_set_hrt_perm(struct device *dev, const u32 port, bool is_hrt) { }
static inline
int mtk_smi_larb_bw_thr(struct device *larbdev, const u32 port, bool is_bw_thr) { }
static inline void
mtk_smi_larb_port_dis_ultra(struct device *dev, const u32 port, bool is_dis_ultra) { }
static inline s32 mtk_smi_dbg_hang_detect(char *user)
{
	return 0;
}
static inline void
mtk_smi_dbg_hang_detect_force_dump(char *user, u64 larb_skip_id, u64 comm_skip_id) { }
static inline void mtk_smi_dbg_dump_for_isp_fast(u32 isp_id) { }

static inline void mtk_smi_dbg_dump_for_disp(void) { }

static inline void mtk_smi_dbg_dump_for_mml(void) { }

static inline s32 mtk_smi_dbg_cg_status(void)
{
	return 0;
}
static inline  void mtk_smi_check_comm_ref_cnt(struct device *dev) {}
static inline  void mtk_smi_check_larb_ref_cnt(struct device *dev) {}

static inline void mtk_smi_init_power_off(void) { }

static inline void mtk_smi_dump_last_pd(const char *user) { }

static inline void mtk_smi_larb_clamp_and_lock(struct device *larbdev, bool on) { }

static inline
s32 smi_sysram_enable(struct device *larbdev, const u32 master_id,
			const bool enable, const char *user)
{
	return 0;
}

static inline
s32 mtk_smi_sysram_set(struct device *larbdev, const u32 master_id,
			u32 set_val, const char *user)
{
	return 0;
}

static inline
int mtk_smi_larb_ultra_dis(struct device *larbdev, bool is_dis)
{
	return 0;
}

s32 mtk_smi_golden_set(bool enable, bool is_larb, u32 id, u32 port)
{
	return 0;
}

static inline
int mtk_smi_larb_enable(struct device *larbdev, u32 smi_user_id)
{
	return 0;
}
static inline
int mtk_smi_larb_disable(struct device *larbdev, u32 smi_user_id)
{
	return 0;
}

int smi_ut_dump_get(const char *val, const struct kernel_param *kp)
{
	return 0;
}
static inline
int mtk_smi_set_ostdl_type(struct device *larbdev, u32 ostdl_type)
{
	return 0;
}
static inline
int mtk_smi_set_larb_value(struct device *larbdev, u32 offset, u32 value)
{
	return 0;
}
static inline
int mtk_smi_set_comm_value(struct device *dev, u32 offset, u32 value)
{
	return 0;
}
static inline
int mtk_smi_clear_larb_set_value(struct device *larbdev)
{
	return 0;
}
static inline
int mtk_smi_clear_comm_set_value(struct device *dev)
{
	return 0;
}
static inline
int mtk_smi_dump_all_setting(struct device *dev, bool is_larb)
{
	return 0;
}

#endif

#endif /* MTK_SMI_INTERNAL_H */
