// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/notifier.h>
#include <linux/platform_device.h> /* rodin r25: 6.18 header pruning */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>

#include "clk-mtk.h"
#include "clk-gate.h"
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include "../../misc/mediatek/mminfra/mtk-mminfra-debug.h"

/* mtk-mminfra-debug.h defines the same pair; guard to avoid redefinition. */
#ifndef MTK_POLL_HWV_VOTE_CNT
#define MTK_POLL_HWV_VOTE_CNT		(2500)
#endif
#ifndef MTK_POLL_HWV_VOTE_US
#define MTK_POLL_HWV_VOTE_US		2
#endif
#define MTK_POLL_DELAY_US		10
#define MTK_POLL_300MS_TIMEOUT		(300 * USEC_PER_MSEC)
/* rodin 6.18（2026-11-03）：常量名 1S 与旧值 5s 名实不符——挂起中止后
 * mminfra voter 的 VCP-ready 忙轮以此预算关中断空转（VCP 已醒但其
 * ready 投票须经仍在半上电的 mminfra hwv voter 落地，只会白烧满档），
 * 实测 4.7s 停滞触发多核 hard lockup 检查并连锁 panic。对齐真值 1s
 * （mtk-scpsys.c 同名常量与 stock scpsys/hwv 计数环均为 1s 档），
 * 超时后既有显式唤醒+短轮询兜底接力，最坏关中断停滞收敛到 ~1s。 */
#define MTK_POLL_1S_TIMEOUT		USEC_PER_SEC
/* rodin 6.18 (2026-11-03): short re-poll budget after an explicit VCP wake
 * request, mirroring the scpsys WAIT_VCP handling. */
#define MTK_POLL_VCP_WAKE_TIMEOUT	(50 * USEC_PER_MSEC)

#define MMINFRA_DONE_STA		BIT(0)
#define VCP_READY_STA			BIT(1)
#define MMINFRA_DURING_OFF_STA		BIT(2)
#define VCP_VOTE_READY_STA		BIT(3)

static DEFINE_SPINLOCK(mminfra_vote_lock);
static ATOMIC_NOTIFIER_HEAD(mtk_clk_notifier_list);
static struct ipi_callbacks *g_clk_cb;
static struct mtk_hwv_domain mminfra_hwv_domain;

static const struct sp_clk_ops *sp_clk_ops;

static bool mmproc_sspm_vote_sync_bits_support;

void set_sp_clk_ops(const struct sp_clk_ops *ops)
{
	sp_clk_ops = ops;
}
EXPORT_SYMBOL_GPL(set_sp_clk_ops);

int sp_merge_clk_control(struct mtk_clk_mux *mux, u8 index, u32 mask)
{
	if (sp_clk_ops == NULL || sp_clk_ops->sp_clk_control == NULL)
		return 1;

	return sp_clk_ops->sp_clk_control(mux, index, mask);
}
EXPORT_SYMBOL_GPL(sp_merge_clk_control);

int register_mtk_clk_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&mtk_clk_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_mtk_clk_notifier);

int unregister_mtk_clk_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&mtk_clk_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_mtk_clk_notifier);

int mtk_clk_notify(struct regmap *regmap, struct regmap *hwv_regmap,
		const char *name, u32 ofs, u32 id, u32 shift, int event_type)
{
	struct clk_event_data clke;

	clke.event_type = event_type;
	clke.regmap = regmap;
	clke.hwv_regmap = hwv_regmap;
	clke.name = name;
	clke.ofs = ofs;
	clke.id = id;
	clke.shift = shift;

	atomic_notifier_call_chain(&mtk_clk_notifier_list, 0, &clke);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_clk_notify);

void mtk_clk_register_ipi_callback(struct ipi_callbacks *clk_cb)
{
	g_clk_cb = clk_cb;
}
EXPORT_SYMBOL_GPL(mtk_clk_register_ipi_callback);

struct ipi_callbacks *mtk_clk_get_ipi_cb(void)
{
	return g_clk_cb;
}
EXPORT_SYMBOL_GPL(mtk_clk_get_ipi_cb);

static int mtk_vcp_is_ready(struct mtk_hwv_domain *hwvd)
{
	u32 val = 0, mask = 0;

	if (mmproc_sspm_vote_sync_bits_support)
		mask = VCP_READY_STA | VCP_VOTE_READY_STA;
	else
		mask = VCP_READY_STA;

	regmap_read(hwvd->regmap, hwvd->data->done_ofs, &val);

	if ((val & mask) == mask)
		return 1;

	return 0;
}

static int mtk_mminfra_hwv_is_enable_done(struct mtk_hwv_domain *hwvd)
{
	u32 val = 0, mask = 0;

	if (mmproc_sspm_vote_sync_bits_support)
		mask = MMINFRA_DONE_STA | VCP_READY_STA | VCP_VOTE_READY_STA;
	else
		mask = MMINFRA_DONE_STA | VCP_READY_STA;

	regmap_read(hwvd->regmap, hwvd->data->done_ofs, &val);
	if (val == mask)
		return 1;

	return 0;
}

/*
 * rodin 6.18（2026-11-03）：ready 轮询冷却闸——满预算失败后 1s 冷却窗内
 * 直接快败。风暴期 N 个 hwv clk 串行 disable 的 N×1.05s 关中断轮询
 * （CCF enable_lock 窗内）塌缩为每秒至多一次全量轮询；任一次轮询成功
 * 即清闸自愈。
 */
static unsigned long mtk_vcp_ready_cooldown;

static bool mtk_vcp_ready_in_cooldown(void)
{
	return time_before(jiffies, READ_ONCE(mtk_vcp_ready_cooldown));
}

static void mtk_vcp_ready_arm_cooldown(void)
{
	WRITE_ONCE(mtk_vcp_ready_cooldown, jiffies + HZ);
}

/*
 * rodin 6.18 (2026-11-03): gate/mux CCF callbacks reach the voter from
 * atomic context, so the VCP-ready wait keeps the busy-poll flavor, but
 * it must not hold the vote spinlock while polling (up to 5s with local
 * IRQs masked collides with CPU idle entry and the watchdog). Skip the
 * wait while the suspend window has VCP intentionally asleep, and
 * request an explicit wake once before giving up.
 */
static int mtk_clk_wait_vcp_ready(struct mtk_hwv_domain *hwvd)
{
	int ret, tmp;

	if (mtk_mminfra_vcp_sleep_prepared())
		return 0;

	if (mtk_vcp_ready_in_cooldown())
		return -ETIMEDOUT;

	ret = readx_poll_timeout_atomic(mtk_vcp_is_ready, hwvd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_1S_TIMEOUT);
	if (!ret) {
		WRITE_ONCE(mtk_vcp_ready_cooldown, 0);
		return 0;
	}

	mtk_mminfra_vcp_wake_request();

	ret = readx_poll_timeout_atomic(mtk_vcp_is_ready, hwvd, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_VCP_WAKE_TIMEOUT);
	if (ret < 0)
		mtk_vcp_ready_arm_cooldown();
	return ret;
}

static int __mminfra_hwvoter_power_ctrl(struct mtk_hwv_domain *hwvd,
			unsigned int vote_msk, bool onoff)
{
	u32 en_ofs;
	u32 vote_ofs;
	u32 vote_ack;
	u32 val = 0;
	unsigned long flags = 0;
	int ret = 0;
	int tmp = 0;
	int i = 0;
	bool locked = false;

	/* rodin 6.18 (2026-11-03): the VCP-ready wait only reads hwv status;
	 * keep it outside the vote spinlock so a slow VCP no longer holds
	 * local IRQs off for seconds (lock now covers just the vote write and
	 * its ack, bounded by MTK_POLL_HWV_VOTE_CNT). */
	ret = mtk_clk_wait_vcp_ready(hwvd);
	if (ret < 0)
		goto err_hwv_prepare;

	spin_lock_irqsave(&mminfra_vote_lock, flags);
	locked = true;

	en_ofs = hwvd->data->en_ofs;
	if (onoff) {
		vote_ofs = hwvd->data->set_ofs;
		vote_ack = vote_msk;
	} else {
		vote_ofs = hwvd->data->clr_ofs;
		vote_ack = 0x0;
	}
	/* write twice to prevent clk idle */
	regmap_write(hwvd->regmap, vote_ofs, vote_msk);
	do {
		regmap_write(hwvd->regmap, vote_ofs, vote_msk);
		regmap_read(hwvd->regmap, en_ofs, &val);
		if ((val & vote_msk) == vote_ack)
			break;

		if (i > MTK_POLL_HWV_VOTE_CNT)
			goto err_hwv_vote;

		udelay(MTK_POLL_HWV_VOTE_US);
		i++;
	} while (1);

	if (onoff) {
		/* wait until VOTER_ACK = 1 */
		ret = readx_poll_timeout_atomic(mtk_mminfra_hwv_is_enable_done, hwvd, tmp, tmp > 0,
				MTK_POLL_DELAY_US, MTK_POLL_300MS_TIMEOUT);
		if (ret < 0)
			goto err_hwv_done;
	}

	spin_unlock_irqrestore(&mminfra_vote_lock, flags);
	locked = false;

	return 0;

err_hwv_done:
	regmap_read(hwvd->regmap, hwvd->data->done_ofs, &val);
	dev_err(hwvd->dev, "Failed to hwv done timeout %s(%x)\n", hwvd->data->name, val);
err_hwv_vote:
	regmap_read(hwvd->regmap, en_ofs, &val);
	dev_err(hwvd->dev, "Failed to hwv vote %s timeout %s(%d %x %x)\n", onoff ? "on" : "off",
			hwvd->data->name, ret, vote_msk, val);
err_hwv_prepare:
	regmap_read(hwvd->regmap, hwvd->data->done_ofs, &val);
	dev_err(hwvd->dev, "Failed to vcp boot-up timeout %s(%x)\n", hwvd->data->name, val);

	if (locked)
		spin_unlock_irqrestore(&mminfra_vote_lock, flags);

	mtk_clk_notify(NULL, hwvd->regmap, hwvd->data->name,
			hwvd->data->en_ofs, 0,
			onoff, CLK_EVT_MMINFRA_HWV_TIMEOUT);

	return ret;
}

void mtk_clk_polling_vcp_ready(void)
{
	u32 val = 0;
	int ret = 0;
	int tmp = 0;

	if (mtk_mminfra_vcp_sleep_prepared())
		return;

	if (mtk_vcp_ready_in_cooldown())
		return;

	/* wait until VCP_READY_ACK = 1 */
	ret = readx_poll_timeout_atomic(mtk_vcp_is_ready, &mminfra_hwv_domain, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_1S_TIMEOUT);
	if (!ret) {
		WRITE_ONCE(mtk_vcp_ready_cooldown, 0);
		return;
	}

	/* rodin 6.18 (2026-11-03): request an explicit wake and re-poll once
	 * before reporting the timeout (mirrors the scpsys WAIT_VCP handling). */
	mtk_mminfra_vcp_wake_request();
	ret = readx_poll_timeout_atomic(mtk_vcp_is_ready, &mminfra_hwv_domain, tmp, tmp > 0,
			MTK_POLL_DELAY_US, MTK_POLL_VCP_WAKE_TIMEOUT);
	if (ret < 0) {
		mtk_vcp_ready_arm_cooldown();
		regmap_read(mminfra_hwv_domain.regmap, mminfra_hwv_domain.data->done_ofs, &val);
		dev_err(mminfra_hwv_domain.dev, "Failed to vcp boot-up timeout %s(%x)\n",
				mminfra_hwv_domain.data->name, val);

		mtk_clk_notify(NULL, mminfra_hwv_domain.regmap, mminfra_hwv_domain.data->name,
				mminfra_hwv_domain.data->en_ofs, 0,
				0, CLK_EVT_MMINFRA_HWV_TIMEOUT);
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_polling_vcp_ready);

int mtk_clk_mminfra_hwv_power_ctrl(bool onoff)
{
	if (!mminfra_hwv_domain.data)
		return 0;

	return __mminfra_hwvoter_power_ctrl(&mminfra_hwv_domain,
		BIT(mminfra_hwv_domain.data->en_shift), onoff);
}
EXPORT_SYMBOL_GPL(mtk_clk_mminfra_hwv_power_ctrl);


int mtk_clk_mminfra_hwv_power_ctrl_optional(bool onoff, u8 bit)
{
	if (!mminfra_hwv_domain.data || (bit > 31))
		return 0;

	return __mminfra_hwvoter_power_ctrl(&mminfra_hwv_domain, BIT(bit), onoff);
}
EXPORT_SYMBOL_GPL(mtk_clk_mminfra_hwv_power_ctrl_optional);

int mtk_clk_register_mminfra_hwv_data(const struct mtk_hwv_data *data,
			struct regmap *regmap, struct device *dev)
{
	mminfra_hwv_domain.data = data;
	mminfra_hwv_domain.regmap = regmap;
	mminfra_hwv_domain.dev = dev;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_clk_register_mminfra_hwv_data);

struct clk_onecell_data *mtk_alloc_clk_data(unsigned int clk_num)
{
	int i;
	struct clk_onecell_data *clk_data;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return NULL;

	clk_data->clks = kcalloc(clk_num, sizeof(*clk_data->clks), GFP_KERNEL);
	if (!clk_data->clks)
		goto err_out;

	clk_data->clk_num = clk_num;

	for (i = 0; i < clk_num; i++)
		clk_data->clks[i] = ERR_PTR(-ENOENT);

	return clk_data;
err_out:
	kfree(clk_data);

	return NULL;
}
EXPORT_SYMBOL_GPL(mtk_alloc_clk_data);

void mtk_free_clk_data(struct clk_onecell_data *clk_data)
{
	if (!clk_data)
		return;

	kfree(clk_data->clks);
	kfree(clk_data);
}
EXPORT_SYMBOL_GPL(mtk_free_clk_data);

void mtk_clk_register_fixed_clks(const struct mtk_fixed_clk *clks,
		int num, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_clk *rc = &clks[i];

		if (clk_data && !IS_ERR_OR_NULL(clk_data->clks[rc->id]))
			continue;

		clk = clk_register_fixed_rate(NULL, rc->name, rc->parent, 0,
					      rc->rate);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					rc->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[rc->id] = clk;
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_register_fixed_clks);

void mtk_clk_register_factors(const struct mtk_fixed_factor *clks,
		int num, struct clk_onecell_data *clk_data)
{
	int i;
	struct clk *clk;

	for (i = 0; i < num; i++) {
		const struct mtk_fixed_factor *ff = &clks[i];

		if (clk_data && !IS_ERR_OR_NULL(clk_data->clks[ff->id]))
			continue;

		clk = clk_register_fixed_factor(NULL, ff->name, ff->parent_name,
				ff->flags, ff->mult, ff->div);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					ff->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[ff->id] = clk;
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_register_factors);

int mtk_clk_register_gates_with_dev(struct device_node *node,
		const struct mtk_gate *clks,
		int num, struct clk_onecell_data *clk_data,
		struct device *dev)
{
	int i;
	struct clk *clk;
	struct regmap *regmap, *hw_voter_regmap, *hwv_mult_regmap = NULL;

	if (!clk_data)
		return -ENOMEM;

	regmap = syscon_node_to_regmap(node);
	if (IS_ERR(regmap)) {
		pr_err("Cannot find regmap for %pOF: %ld\n", node,
				PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	hw_voter_regmap = syscon_regmap_lookup_by_phandle(node, "hw-voter-regmap");
	if (IS_ERR_OR_NULL(hw_voter_regmap))
		hw_voter_regmap = NULL;

	for (i = 0; i < num; i++) {
		const struct mtk_gate *gate = &clks[i];

		if (!IS_ERR_OR_NULL(clk_data->clks[gate->id]))
			continue;

		if (gate->hwv_comp) {
			hwv_mult_regmap = syscon_regmap_lookup_by_phandle(node,
					gate->hwv_comp);
			if (IS_ERR(hwv_mult_regmap))
				hwv_mult_regmap = NULL;
			hw_voter_regmap = hwv_mult_regmap;
		}

		if (hw_voter_regmap && gate->flags & CLK_USE_HW_VOTER)
			clk = mtk_clk_register_gate_hwv(gate,
					regmap,
					hw_voter_regmap,
					dev);
		else
			clk = mtk_clk_register_gate(gate, regmap, dev);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					gate->name, PTR_ERR(clk));
			continue;
		}

		clk_data->clks[gate->id] = clk;
	}

	return 0;
}
EXPORT_SYMBOL(mtk_clk_register_gates_with_dev);

int mtk_clk_register_gates(struct device_node *node,
		const struct mtk_gate *clks,
		int num, struct clk_onecell_data *clk_data)
{
	return mtk_clk_register_gates_with_dev(node,
		clks, num, clk_data, NULL);
}
EXPORT_SYMBOL_GPL(mtk_clk_register_gates);

struct clk *mtk_clk_register_composite(const struct mtk_composite *mc,
		void __iomem *base, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *div_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL, *div_ops = NULL;
	const char * const *parent_names;
	const char *parent;
	int num_parents;
	int ret;

	if (mc->mux_shift >= 0) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = base + mc->mux_reg;
		mux->mask = BIT(mc->mux_width) - 1;
		mux->shift = mc->mux_shift;
		mux->lock = lock;
		mux->flags = mc->mux_flags;
		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;

		parent_names = mc->parent_names;
		num_parents = mc->num_parents;
	} else {
		parent = mc->parent;
		parent_names = &parent;
		num_parents = 1;
	}

	if (mc->gate_shift >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			ret = -ENOMEM;
			goto err_out;
		}

		gate->reg = base + mc->gate_reg;
		gate->bit_idx = mc->gate_shift;
		gate->flags = CLK_GATE_SET_TO_DISABLE;
		gate->lock = lock;

		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	if (mc->divider_shift >= 0) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div) {
			ret = -ENOMEM;
			goto err_out;
		}

		div->reg = base + mc->divider_reg;
		div->shift = mc->divider_shift;
		div->width = mc->divider_width;
		div->lock = lock;

		div_hw = &div->hw;
		div_ops = &clk_divider_ops;
	}

	clk = clk_register_composite(NULL, mc->name, parent_names, num_parents,
		mux_hw, mux_ops,
		div_hw, div_ops,
		gate_hw, gate_ops,
		mc->flags);

	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_out;
	}

	return clk;
err_out:
	kfree(div);
	kfree(gate);
	kfree(mux);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL(mtk_clk_register_composite);

void mtk_clk_register_composites(const struct mtk_composite *mcs,
		int num, void __iomem *base, spinlock_t *lock,
		struct clk_onecell_data *clk_data)
{
	struct clk *clk;
	int i;

	for (i = 0; i < num; i++) {
		const struct mtk_composite *mc = &mcs[i];

		if (clk_data && !IS_ERR_OR_NULL(clk_data->clks[mc->id]))
			continue;

		clk = mtk_clk_register_composite(mc, base, lock);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
					mc->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[mc->id] = clk;
	}
}
EXPORT_SYMBOL_GPL(mtk_clk_register_composites);

void mtk_clk_register_dividers(const struct mtk_clk_divider *mcds,
			int num, void __iomem *base, spinlock_t *lock,
				struct clk_onecell_data *clk_data)
{
	struct clk *clk;
	int i;

	for (i = 0; i <  num; i++) {
		const struct mtk_clk_divider *mcd = &mcds[i];

		if (clk_data && !IS_ERR_OR_NULL(clk_data->clks[mcd->id]))
			continue;

		clk = clk_register_divider(NULL, mcd->name, mcd->parent_name,
			mcd->flags, base +  mcd->div_reg, mcd->div_shift,
			mcd->div_width, mcd->clk_divider_flags, lock);

		if (IS_ERR(clk)) {
			pr_err("Failed to register clk %s: %ld\n",
				mcd->name, PTR_ERR(clk));
			continue;
		}

		if (clk_data)
			clk_data->clks[mcd->id] = clk;
	}
}
EXPORT_SYMBOL(mtk_clk_register_dividers);

int mtk_clk_simple_probe(struct platform_device *pdev)
{
	const struct mtk_clk_desc *mcd;
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *vcp_node;
	int support = 0;
	int r;

	vcp_node = of_find_node_by_name(NULL, "vcp");
	if (vcp_node == NULL)
		pr_info("failed to find vcp_node @ %s\n", __func__);
	else {
		r = of_property_read_u32(vcp_node, "warmboot-support", &support);

		if (r || support == 0) {
			mmproc_sspm_vote_sync_bits_support = false;
		} else {
			pr_info("%s mmproc_sspm_vote_sync_bits_support is enabled: %d\n",
				__func__, r);
			mmproc_sspm_vote_sync_bits_support = true;
		}
	}

	mcd = of_device_get_match_data(&pdev->dev);
	if (!mcd)
		return -EINVAL;

	clk_data = mtk_alloc_clk_data(mcd->num_clks);
	if (!clk_data)
		return -ENOMEM;

	r = mtk_clk_register_gates(node, mcd->clks, mcd->num_clks, clk_data);
	if (r)
		return r;

	return of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}
EXPORT_SYMBOL(mtk_clk_simple_probe);

MODULE_LICENSE("GPL");
