// SPDX-License-Identifier: GPL-2.0
/*
 * rodin 6.18（2026-11-01）：MCUPM DEPD 握手观测节点。
 * 背景：PSCI CPU_SUSPEND power-down 全被 BL31 以 PSCI_E_INVALID_PARAMS 拒绝
 * （readiness 闸：DEV_INIT 要求 MCUPM mbox slot8 TASK_STA==1，40s 兜底在
 * DEV_INIT 未过时不可达，与公开 cpcv5_4 的 cpupm_pwr_state_valid 同构）。
 * 本节点只读三个候选 slot 窗口 + TCM 全域稀疏扫描，不写任何 MCUPM 状态。
 * rodin 6.18（2026-11-03）：默认关闭，需 cmdline lpm_mcupm_dep=1 才建节点
 * （TCM 全扫 80k 次 readl，MCUPM 域掉电时可能挂总线）；ioremap 在 init
 * 一次完成，失败路径逐个 iounmap。
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <mtk_lpm_sysfs.h>
#include <mtk_lp_sysfs.h>
#include <lpm_dbg_fs_common.h>

static bool lpm_dbg_mcupm_dep_enabled;

#define MCUPM_TCM_PA		0x0C070000UL
#define TCM_SZ			0x50000UL
/* 内核侧 APMCU_MCUPM mbox 窗（DT mbox0_base = TCM+0x6F600），slot8=TASK_STA */
#define WIN_SLOTS_PA		(MCUPM_TCM_PA + 0x6F600)
/* 公开 TF-A mcupm_cfg.h 的 CPU_EB_MBOX3_OFFSET 两种代际 */
#define WIN_MBOX3_V23_PA	(MCUPM_TCM_PA + 0x7E0)
#define WIN_MBOX3_V1_PA		(MCUPM_TCM_PA + 0xCE0)

#define WIN_SLOTS_N		40	/* 覆盖 slot0/4/7/8 足够 */
#define WIN_MBOX3_N		8

static void __iomem *win_slots, *win_v23, *win_v1;
static void __iomem *tcm_base;

static size_t win_dump(char *p, size_t sz, const char *tag,
		       void __iomem *base, unsigned int pa, int n)
{
	size_t used = 0;
	int i;

	if (!base) {
		used += scnprintf(p, sz, "%s: (unmapped)\n", tag);
		return used;
	}
	used += scnprintf(p, sz, "%s @0x%08X:\n", tag, pa);
	for (i = 0; i < n; i++)
		used += scnprintf(p + used, sz - used,
				  "  slot[%2d] = 0x%08x\n", i, readl(base + 4 * i));
	return used;
}

static void lpm_dbg_mcupm_dep_unmap(void)
{
	if (tcm_base) {
		iounmap(tcm_base);
		tcm_base = NULL;
	}
	if (win_v1) {
		iounmap(win_v1);
		win_v1 = NULL;
	}
	if (win_v23) {
		iounmap(win_v23);
		win_v23 = NULL;
	}
	if (win_slots) {
		iounmap(win_slots);
		win_slots = NULL;
	}
}

static int __init lpm_dbg_mcupm_dep_map(void)
{
	win_slots = ioremap(WIN_SLOTS_PA, WIN_SLOTS_N * 4);
	win_v23 = ioremap(WIN_MBOX3_V23_PA, WIN_MBOX3_N * 4);
	win_v1 = ioremap(WIN_MBOX3_V1_PA, WIN_MBOX3_N * 4);
	tcm_base = ioremap(MCUPM_TCM_PA, TCM_SZ);

	if (!win_slots || !win_v23 || !win_v1 || !tcm_base) {
		lpm_dbg_mcupm_dep_unmap();
		return -ENOMEM;
	}

	return 0;
}

static ssize_t mcupm_dep_read(char *ToUserBuf, size_t sz, void *priv)
{
	char *p = ToUserBuf;
	size_t used = 0;

	if (!p)
		return -EINVAL;

	if (!lpm_dbg_mcupm_dep_enabled || !win_slots || !tcm_base)
		return -ENODEV;

	used += scnprintf(p + used, sz - used,
		"==== MCUPM DEPD handshake windows (read-only) ====\n");
	used += win_dump(p + used, sz - used, "kernel-slot window",
			 win_slots, WIN_SLOTS_PA, WIN_SLOTS_N);
	used += scnprintf(p + used, sz - used,
		"  (slot0=AP_READY slot4=PWR_CTRL_EN slot7=ARMPLL slot8=TASK_STA;"
		" TASK_STA: 0=uninit 1=init 2=init_finish 3=wait 4=run)\n");
	used += win_dump(p + used, sz - used, "tf-a mbox3 v2/v3",
			 win_v23, WIN_MBOX3_V23_PA, WIN_MBOX3_N);
	used += win_dump(p + used, sz - used, "tf-a mbox3 v1",
			 win_v1, WIN_MBOX3_V1_PA, WIN_MBOX3_N);

	/* TCM 全域稀疏扫描：只列非零字（全零 = 固件未运行），上限 60 行 */
	{
		unsigned int off, nz = 0, shown = 0;
		u32 prev = 0;

		used += scnprintf(p + used, sz - used,
			"==== TCM full sweep (non-zero words, cap 60) ====\n");
		for (off = 0; off < TCM_SZ; off += 4) {
			u32 val = readl(tcm_base + off);

			if (!val)
				continue;
			nz++;
			if (shown >= 60 || val == prev)
				continue;
			used += scnprintf(p + used, sz - used,
				"off=0x%05X val=0x%08x\n", off, val);
			shown++;
			prev = val;
		}
		used += scnprintf(p + used, sz - used,
			"==== nonzero words total: %u / %u ====\n",
			nz, (unsigned int)(TCM_SZ / 4));
	}

	return used;
}

static const struct mtk_lp_sysfs_op mcupm_dep_op = {
	.fs_read = mcupm_dep_read,
};

static struct mtk_lp_sysfs_handle mcupm_dep_node;

static int __init lpm_dbg_mcupm_dep_setup(char *str)
{
	lpm_dbg_mcupm_dep_enabled = true;
	return 1;
}
__setup("lpm_mcupm_dep=", lpm_dbg_mcupm_dep_setup);

static int __init lpm_dbg_mcupm_dep_init(void)
{
	int ret;

	if (!lpm_dbg_mcupm_dep_enabled)
		return 0;

	ret = lpm_dbg_mcupm_dep_map();
	if (ret)
		return ret;

	ret = mtk_lpm_sysfs_entry_node_add("mcupm_dep", 0444,
					   &mcupm_dep_op, &mcupm_dep_node);
	if (ret) {
		pr_err("lpm_dbg_mcupm_dep: node add failed %d\n", ret);
		lpm_dbg_mcupm_dep_unmap();
		return ret;
	}

	return 0;
}
late_initcall(lpm_dbg_mcupm_dep_init);

static void __exit lpm_dbg_mcupm_dep_exit(void)
{
	if (!lpm_dbg_mcupm_dep_enabled)
		return;

	mtk_lpm_sysfs_entry_node_remove(&mcupm_dep_node);
	lpm_dbg_mcupm_dep_unmap();
}
module_exit(lpm_dbg_mcupm_dep_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rodin MCUPM DEPD handshake observability (read-only)");
