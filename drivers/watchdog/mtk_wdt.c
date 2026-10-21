// SPDX-License-Identifier: GPL-2.0+
/*
 * Mediatek Watchdog Driver
 *
 * Copyright (C) 2014 Matthias Brugger
 *
 * Matthias Brugger <matthias.bgg@gmail.com>
 *
 * Based on sunxi_wdt.c
 */

#include <dt-bindings/reset/mt2712-resets.h>
#include <linux/vseq.h>
#include <dt-bindings/reset/mt8183-resets.h>
#include <dt-bindings/reset/mt8192-resets.h>
#include <dt-bindings/reset/mt8195-resets.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/types.h>
#include <linux/watchdog.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/smp.h>
#include <linux/sched/debug.h>


#define WDT_MAX_TIMEOUT		31
#define WDT_MIN_TIMEOUT		2
#define WDT_LENGTH_TIMEOUT(n)	((n) << 5)

#define WDT_LENGTH		0x04
#define WDT_LENGTH_KEY		0x8

#define WDT_RST			0x08
#define WDT_RST_RELOAD		0x1971

#define WDT_MODE		0x00
#define WDT_MODE_EN		(1 << 0)
#define WDT_MODE_EXT_POL_LOW	(0 << 1)
#define WDT_MODE_EXT_POL_HIGH	(1 << 1)
#define WDT_MODE_EXRST_EN	(1 << 2)
#define WDT_MODE_IRQ_EN		(1 << 3)
#define WDT_MODE_AUTO_START	(1 << 4)
#define WDT_MODE_DUAL_EN	(1 << 6)
#define WDT_MODE_KEY		0x22000000

#if IS_ENABLED(CONFIG_GRT_HYPERVISOR)
#define WDT_STATUS		0x0C
#define WDT_STATUS_SWWDT_RST	(1 << 30)

#define WDT_NONRST_REG		0x20
#endif

#define WDT_SWRST		0x14
#define WDT_SWRST_KEY		0x1209

#define WDT_SWSYSRST		0x18U
#define WDT_SWSYS_RST_KEY	0x88000000

#define DRV_NAME		"mtk-wdt"
#define DRV_VERSION		"1.0"

#if IS_ENABLED(CONFIG_GRT_HYPERVISOR)
static void __iomem *toprgu_base;
#endif
static bool nowayout = WATCHDOG_NOWAYOUT;
static bool disable_extrst;
static unsigned int timeout;

struct mtk_wdt_dev {
	struct watchdog_device wdt_dev;
	void __iomem *wdt_base;
	spinlock_t lock; /* protects WDT_SWSYSRST reg */
	struct reset_controller_dev rcdev;
};

/*
 * Bark forensic hook：本板 DT 不带 watchdog bark IRQ（probe 打印
 * "IRQ index 0 not found"），但 bootloader 起狗时置了 IRQ_EN|DUAL_EN，
 * WDT 第一次到期时 bark 在 GIC 上挂起无人应答，第二次到期由 EXRST
 * 硬复位——整机级 wedge 因此完全无声。 bark SPI 号不在任何本地源里，
 * 故 ISR 以模块参数在运行期补注册：先用 WDT_STATUS 超时位拒绝非本
 * 设备的误触发，再异步 IPI 探测各核可响应性（关中断自旋的核不会置
 * 位），最后打印 D 态任务栈，全部落入 console-ramoops 供死后取证。
 * arming: echo <spi> > /sys/module/mtk_wdt/parameters/bark_irq_spi
 * 极性 bark_level_low 先设；闸门误判可用 bark_gate_disable 逃生。
 */
static struct mtk_wdt_dev *bark_wdt;	/* 全板唯一 toprgu */
static atomic_t bark_alive = ATOMIC_INIT(0);

/* WDT_STATUS(0x0C) bit0: 定时器超时发生（bark/bite 待处理） */
#define WDT_STATUS_REG		0x0C
#define WDT_STATUS_TOUT_STA	(1 << 0)

struct mtk_wdt_data {
	int toprgu_sw_rst_num;
};

static const struct mtk_wdt_data mt2712_data = {
	.toprgu_sw_rst_num = MT2712_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8183_data = {
	.toprgu_sw_rst_num = MT8183_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8192_data = {
	.toprgu_sw_rst_num = MT8192_TOPRGU_SW_RST_NUM,
};

static const struct mtk_wdt_data mt8195_data = {
	.toprgu_sw_rst_num = MT8195_TOPRGU_SW_RST_NUM,
};

static int toprgu_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	unsigned int tmp;
	unsigned long flags;
	struct mtk_wdt_dev *data =
		 container_of(rcdev, struct mtk_wdt_dev, rcdev);

	spin_lock_irqsave(&data->lock, flags);

	tmp = readl(data->wdt_base + WDT_SWSYSRST);
	if (assert)
		tmp |= BIT(id);
	else
		tmp &= ~BIT(id);
	tmp |= WDT_SWSYS_RST_KEY;
	writel(tmp, data->wdt_base + WDT_SWSYSRST);

	spin_unlock_irqrestore(&data->lock, flags);

	return 0;
}

static int toprgu_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return toprgu_reset_update(rcdev, id, true);
}

static int toprgu_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return toprgu_reset_update(rcdev, id, false);
}

static int toprgu_reset(struct reset_controller_dev *rcdev,
			unsigned long id)
{
	int ret;

	ret = toprgu_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return toprgu_reset_deassert(rcdev, id);
}

static const struct reset_control_ops toprgu_reset_ops = {
	.assert = toprgu_reset_assert,
	.deassert = toprgu_reset_deassert,
	.reset = toprgu_reset,
};

static int toprgu_register_reset_controller(struct platform_device *pdev,
					    int rst_num)
{
	int ret;
	struct mtk_wdt_dev *mtk_wdt = platform_get_drvdata(pdev);

	spin_lock_init(&mtk_wdt->lock);

	mtk_wdt->rcdev.owner = THIS_MODULE;
	mtk_wdt->rcdev.nr_resets = rst_num;
	mtk_wdt->rcdev.ops = &toprgu_reset_ops;
	mtk_wdt->rcdev.of_node = pdev->dev.of_node;
	ret = devm_reset_controller_register(&pdev->dev, &mtk_wdt->rcdev);
	if (ret != 0)
		dev_err(&pdev->dev,
			"couldn't register wdt reset controller: %d\n", ret);
	return ret;
}

static int mtk_wdt_restart(struct watchdog_device *wdt_dev,
			   unsigned long action, void *data)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base;

	wdt_base = mtk_wdt->wdt_base;

	while (1) {
		writel(WDT_SWRST_KEY, wdt_base + WDT_SWRST);
		mdelay(5);
	}

	return 0;
}

static int mtk_wdt_ping(struct watchdog_device *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;

	iowrite32(WDT_RST_RELOAD, wdt_base + WDT_RST);
	pr_info("[wdtk] kick watchdog\n");

	return 0;
}

static int mtk_wdt_set_timeout(struct watchdog_device *wdt_dev,
				unsigned int timeout)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	wdt_dev->timeout = timeout;
	/*
	 * In dual mode, irq will be triggered at timeout / 2
	 * the real timeout occurs at timeout
	 */
	if (wdt_dev->pretimeout)
		wdt_dev->pretimeout = timeout / 2;

	/*
	 * One bit is the value of 512 ticks
	 * The clock has 32 KHz
	 */
	reg = WDT_LENGTH_TIMEOUT((timeout - wdt_dev->pretimeout) << 6)
			| WDT_LENGTH_KEY;
	iowrite32(reg, wdt_base + WDT_LENGTH);

	mtk_wdt_ping(wdt_dev);

	return 0;
}

static void mtk_wdt_init(struct watchdog_device *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base;

	wdt_base = mtk_wdt->wdt_base;

	if (readl(wdt_base + WDT_MODE) & WDT_MODE_EN) {
		u32 reg = ioread32(wdt_base + WDT_MODE);

		set_bit(WDOG_HW_RUNNING, &wdt_dev->status);
		/*
		 * The bootloader may have started the WDT without the external
		 * reset output armed. With no bark IRQ in DT, an expired
		 * timeout produces no visible effect unless EXRST_EN is set.
		 */
		if (!disable_extrst)
			reg |= WDT_MODE_EXRST_EN;
		reg |= WDT_MODE_KEY;
		iowrite32(reg, wdt_base + WDT_MODE);
		mtk_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	}
}

static int mtk_wdt_stop(struct watchdog_device *wdt_dev)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg;

	reg = readl(wdt_base + WDT_MODE);
	reg &= ~WDT_MODE_EN;
	reg |= WDT_MODE_KEY;
	iowrite32(reg, wdt_base + WDT_MODE);

	clear_bit(WDOG_HW_RUNNING, &wdt_dev->status);

	return 0;
}

static int mtk_wdt_start(struct watchdog_device *wdt_dev)
{
	u32 reg;
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdt_dev);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	int ret;

	ret = mtk_wdt_set_timeout(wdt_dev, wdt_dev->timeout);
	if (ret < 0)
		return ret;

	reg = ioread32(wdt_base + WDT_MODE);
	if (wdt_dev->pretimeout)
		reg |= (WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
	else
		reg &= ~(WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
	if (!disable_extrst)
		reg |= WDT_MODE_EXRST_EN;
	reg |= (WDT_MODE_EN | WDT_MODE_KEY);
	iowrite32(reg, wdt_base + WDT_MODE);

	set_bit(WDOG_HW_RUNNING, &wdt_dev->status);

	return 0;
}

static int mtk_wdt_set_pretimeout(struct watchdog_device *wdd,
				  unsigned int timeout)
{
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdd);
	void __iomem *wdt_base = mtk_wdt->wdt_base;
	u32 reg = ioread32(wdt_base + WDT_MODE);

	if (timeout && !wdd->pretimeout) {
		wdd->pretimeout = wdd->timeout / 2;
		reg |= (WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
	} else if (!timeout && wdd->pretimeout) {
		wdd->pretimeout = 0;
		reg &= ~(WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN);
	} else {
		return 0;
	}

	reg |= WDT_MODE_KEY;
	iowrite32(reg, wdt_base + WDT_MODE);

	return mtk_wdt_set_timeout(wdd, wdd->timeout);
}

static irqreturn_t mtk_wdt_isr(int irq, void *arg)
{
	struct watchdog_device *wdd = arg;

	watchdog_notify_pretimeout(wdd);

	return IRQ_HANDLED;
}

static const struct watchdog_info mtk_wdt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_info mtk_wdt_pt_info = {
	.identity	= DRV_NAME,
	.options	= WDIOF_SETTIMEOUT |
			  WDIOF_PRETIMEOUT |
			  WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
};

static const struct watchdog_ops mtk_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= mtk_wdt_start,
	.stop		= mtk_wdt_stop,
	.ping		= mtk_wdt_ping,
	.set_timeout	= mtk_wdt_set_timeout,
	.set_pretimeout	= mtk_wdt_set_pretimeout,
	.restart	= mtk_wdt_restart,
};

#if IS_ENABLED(CONFIG_GRT_HYPERVISOR)
void mtk_wdt_set_sw_rst_status(void)
{
	u32 reg;

	if (!toprgu_base) {
		pr_info("%s: get toprgu base failed\n", __func__);
		return;
	}

	reg = ioread32(toprgu_base + WDT_STATUS);
	reg |= WDT_STATUS_SWWDT_RST;
	iowrite32(reg, toprgu_base + WDT_NONRST_REG);
}
EXPORT_SYMBOL(mtk_wdt_set_sw_rst_status);
#endif

static int mtk_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_wdt_dev *mtk_wdt;
	const struct mtk_wdt_data *wdt_data;
	int err, irq;

	mtk_wdt = devm_kzalloc(dev, sizeof(*mtk_wdt), GFP_KERNEL);
	if (!mtk_wdt)
		return -ENOMEM;

	platform_set_drvdata(pdev, mtk_wdt);

	mtk_wdt->wdt_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mtk_wdt->wdt_base))
		return PTR_ERR(mtk_wdt->wdt_base);

#if IS_ENABLED(CONFIG_GRT_HYPERVISOR)
	toprgu_base = mtk_wdt->wdt_base;
#endif

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		err = devm_request_irq(&pdev->dev, irq, mtk_wdt_isr, 0, "wdt_bark",
				       &mtk_wdt->wdt_dev);
		if (err)
			return err;

		mtk_wdt->wdt_dev.info = &mtk_wdt_pt_info;
		mtk_wdt->wdt_dev.pretimeout = WDT_MAX_TIMEOUT / 2;
	} else {
		if (irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		mtk_wdt->wdt_dev.info = &mtk_wdt_info;
	}

	mtk_wdt->wdt_dev.ops = &mtk_wdt_ops;
	mtk_wdt->wdt_dev.timeout = WDT_MAX_TIMEOUT;
	mtk_wdt->wdt_dev.max_hw_heartbeat_ms = WDT_MAX_TIMEOUT * 1000;
	mtk_wdt->wdt_dev.min_timeout = WDT_MIN_TIMEOUT;
	mtk_wdt->wdt_dev.parent = dev;

	watchdog_init_timeout(&mtk_wdt->wdt_dev, timeout, dev);
	watchdog_set_nowayout(&mtk_wdt->wdt_dev, nowayout);
	watchdog_set_restart_priority(&mtk_wdt->wdt_dev, 128);

	watchdog_set_drvdata(&mtk_wdt->wdt_dev, mtk_wdt);
	bark_wdt = mtk_wdt;

	mtk_wdt_init(&mtk_wdt->wdt_dev);

#if defined(CONFIG_MEDIATEK_WATCHDOG_STOP_ON_REBOOT)
	watchdog_stop_on_reboot(&mtk_wdt->wdt_dev);
#endif
	err = devm_watchdog_register_device(dev, &mtk_wdt->wdt_dev);
	if (unlikely(err))
		return err;

	dev_info(dev,
		 "Watchdog enabled (timeout=%d sec, nowayout=%d, mode=0x%08x, len=0x%08x)\n",
		 mtk_wdt->wdt_dev.timeout, nowayout,
		 ioread32(mtk_wdt->wdt_base + WDT_MODE),
		 ioread32(mtk_wdt->wdt_base + WDT_LENGTH));

	wdt_data = of_device_get_match_data(dev);
	if (wdt_data) {
		err = toprgu_register_reset_controller(pdev,
						       wdt_data->toprgu_sw_rst_num);
		if (err)
			return err;
	}
	return 0;
}

#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_MEDIATEK_WATCHDOG_PM)
static int mtk_wdt_suspend(struct device *dev)
{
	struct mtk_wdt_dev *mtk_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&mtk_wdt->wdt_dev))
		mtk_wdt_stop(&mtk_wdt->wdt_dev);

	return 0;
}

static int mtk_wdt_resume(struct device *dev)
{
	struct mtk_wdt_dev *mtk_wdt = dev_get_drvdata(dev);

	if (watchdog_active(&mtk_wdt->wdt_dev)) {
		mtk_wdt_start(&mtk_wdt->wdt_dev);
		mtk_wdt_ping(&mtk_wdt->wdt_dev);
	}

	return 0;
}

static const struct dev_pm_ops mtk_wdt_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_wdt_suspend,
				mtk_wdt_resume)
};
#endif

static const struct of_device_id mtk_wdt_dt_ids[] = {
	{ .compatible = "mediatek,mt2712-wdt", .data = &mt2712_data },
	{ .compatible = "mediatek,mt6589-wdt" },
	{ .compatible = "mediatek,mt8183-wdt", .data = &mt8183_data },
	{ .compatible = "mediatek,mt8192-wdt", .data = &mt8192_data },
	{ .compatible = "mediatek,mt8195-wdt", .data = &mt8195_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mtk_wdt_dt_ids);

static struct platform_driver mtk_wdt_driver = {
	.probe		= mtk_wdt_probe,
	.driver		= {
		.name		= DRV_NAME,
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_MEDIATEK_WATCHDOG_PM)
		.pm		= &mtk_wdt_pm_ops,
#endif
		.of_match_table	= mtk_wdt_dt_ids,
	},
};

vseq_module_platform_driver(mtk_wdt_driver);

static void bark_ipi_mark(void *info)
{
	atomic_or(1 << smp_processor_id(), &bark_alive);
}

static int bark_irq_spi = -1;
static bool bark_level_low = true;
static bool bark_gate_disable;
static bool bark_armed;

static irqreturn_t mtk_wdt_bark_isr(int irq, void *arg)
{
	struct watchdog_device *wdd = arg;
	struct mtk_wdt_dev *mtk_wdt = watchdog_get_drvdata(wdd);
	u32 status;

	if (!mtk_wdt)
		return IRQ_NONE;
	status = ioread32(mtk_wdt->wdt_base + WDT_STATUS_REG);
	if (!bark_gate_disable && !(status & WDT_STATUS_TOUT_STA))
		return IRQ_NONE;

	pr_emerg("wdt_bark: cpu%d status=0x%08x online=%*pbl\n",
		 smp_processor_id(), status, cpumask_pr_args(cpu_online_mask));

	atomic_set(&bark_alive, 0);
	smp_call_function(bark_ipi_mark, NULL, 0);
	mdelay(100);
	pr_emerg("wdt_bark: responsive cpus=0x%02x (bits set = cores taking IPIs)\n",
		 atomic_read(&bark_alive));
	show_state_filter(TASK_UNINTERRUPTIBLE);
	pr_emerg("wdt_bark: forensic dump done, EXRST reset due in <=31s\n");

	return IRQ_HANDLED;
}

static int mtk_wdt_bark_arm(void)
{
	struct mtk_wdt_dev *mtk_wdt = bark_wdt;
	unsigned long flags = bark_level_low ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH;
	struct device_node *gic_np;
	struct irq_domain *dom;
	int virq, ret;
	u32 reg;

	if (bark_armed)
		return 0;
	if (!mtk_wdt || bark_irq_spi < 0 || bark_irq_spi > 1023)
		return -EINVAL;
	/* r143: NULL 域在 6.18 GICv3 下拿不到默认 domain（真机 map SPI failed），经 toprgu DT 的 interrupt-parent 显式解析 GIC domain。 */
	gic_np = of_irq_find_parent(mtk_wdt->wdt_dev.parent->of_node);
	if (!gic_np) {
		pr_err("mtk-wdt: bark arm: no interrupt-parent\n");
		return -EINVAL;
	}
	dom = irq_find_host(gic_np);
	of_node_put(gic_np);
	if (!dom) {
		pr_err("mtk-wdt: bark arm: GIC domain not found\n");
		return -EINVAL;
	}
	virq = irq_create_mapping(dom, 32 + bark_irq_spi);
	if (!virq) {
		pr_err("mtk-wdt: bark arm: map SPI %d failed\n", bark_irq_spi);
		return -EINVAL;
	}
	ret = request_irq(virq, mtk_wdt_bark_isr, flags, "wdt_bark_forensic",
			  &mtk_wdt->wdt_dev);
	if (ret == -ENOSYS)
		/* GIC 不编程极性；该映射 chip 无 set_type（-ENOSYS），0 flags 默认 level 型即可送达 */
		ret = request_irq(virq, mtk_wdt_bark_isr, 0, "wdt_bark_forensic",
				  &mtk_wdt->wdt_dev);
	if (ret) {
		pr_err("mtk-wdt: bark arm: request_irq %d failed %d\n", virq, ret);
		return ret;
	}
	bark_armed = true;
	/*
	 * 补开 bark 输出位：无 DT IRQ ⇒ info 无 PRETIMEOUT ⇒ start()/init()
	 * 从不置 IRQ_EN|DUAL_EN，只挂 handler 则自然 wedge 时 bark 永不发声。
	 */
	reg = ioread32(mtk_wdt->wdt_base + WDT_MODE);
	reg |= WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN | WDT_MODE_KEY;
	iowrite32(reg, mtk_wdt->wdt_base + WDT_MODE);
	pr_info("mtk-wdt: bark forensic hook armed: SPI %d virq %d %s gate=%d mode=0x%08x\n",
		bark_irq_spi, virq,
		bark_level_low ? "level-low" : "level-high", !bark_gate_disable,
		ioread32(mtk_wdt->wdt_base + WDT_MODE));
	return 0;
}

static int bark_irq_spi_set(const char *val, const struct kernel_param *kp)
{
	bool was = bark_armed;
	int ret;

	ret = param_set_int(val, kp);
	if (ret)
		return ret;
	if (!was && bark_irq_spi >= 0) {
		ret = mtk_wdt_bark_arm();
		if (ret)
			bark_armed = false;
	}
	return ret;
}

static const struct kernel_param_ops bark_irq_spi_ops = {
	.set = bark_irq_spi_set,
	.get = param_get_int,
};
module_param_cb(bark_irq_spi, &bark_irq_spi_ops, &bark_irq_spi, 0644);
MODULE_PARM_DESC(bark_irq_spi,
		 "GIC SPI index of toprgu bark (DT lacks it); set >0 at runtime to arm");
module_param(bark_level_low, bool, 0644);
MODULE_PARM_DESC(bark_level_low, "bark IRQ polarity (default level-low)");
module_param(bark_gate_disable, bool, 0644);
MODULE_PARM_DESC(bark_gate_disable, "skip WDT_STATUS gate in bark ISR (escape hatch)");

/*
 * Bark active probe v2（分相，首跑 +6.0s 整机挂死事故拆嫌产物）：
 * phase1（echo 1）= bark 良性/TOUT 真值判定：LENGTH 压 6s 制造 bark，
 *   全程只碰 toprgu 自身寄存器、零 GICD 访问，8s 时限回写踢狗。若整机
 *   在 +6s 挂死，ramoops 止于 arming 界标行 ⇒ bark 断言本身 = 平台级
 *   wedge 触发器（比 WDT 复位路径更深，长按才可救）；若活着且 TOUT 置位
 *   ⇒ bark 良性、TOUT bit0 真。pr_emerg 界标直落 console-ramoops。
 * phase2（echo 2，须 phase1 活着）= SPI 定案：LENGTH 压 6s，等 TOUT 或
 *   +6.3s 兜底（线已 assert）后读 GICD_ISPENDR 前后 diff；≤6 窗取 ≥3
 *   命中轮交集滤巧合活跃线；TOUT 佐证下连续 3 轮 TOUT+零新增 = "bark 不
 *   进 GIC" 判死；交集唯一且 ≥1 轮 TOUT 佐证才自动喂 bark_irq_spi arm。
 * usage: echo 1|2 > /sys/module/mtk_wdt/parameters/bark_probe
 */
#define BARK_PEND_WORDS		24	/* GICD_ISPENDR word0..23 = SPI 0..767 */
#define BARK_PROBE_SECS		6	/* bark 延时；bite = 2×此值 */
#define BARK_PROBE_CAP_MS	8000	/* 单窗时限 < bite（bark+6s）：无触发也安全回退 */
#define BARK_PROBE_BARK_AT_MS	6300	/* TOUT 假时的兜底等待点（线已 assert） */
#define BARK_PROBE_MAX_ATTEMPTS	6
#define BARK_PROBE_HIT_ROUNDS	3

static u32 bark_pend_and[BARK_PEND_WORDS];	/* 轮间交集累积 */
static int bark_runs;
static int bark_tout_rounds;
static int bark_zero_rounds;
static bool bark_phase1_alive;
static atomic_t bark_probe_busy = ATOMIC_INIT(0);
static void __iomem *bark_gicd;

static void bark_gicd_map(void)
{
	struct device_node *gic_np;
	struct resource res;

	if (bark_gicd || !bark_wdt || !bark_wdt->wdt_dev.parent ||
	    !bark_wdt->wdt_dev.parent->of_node)
		return;
	gic_np = of_irq_find_parent(bark_wdt->wdt_dev.parent->of_node);
	if (!gic_np)
		return;
	if (!of_address_to_resource(gic_np, 0, &res))
		bark_gicd = ioremap(res.start, resource_size(&res));
	of_node_put(gic_np);
}

static void bark_ispendr_read(u32 *words)
{
	int i;

	for (i = 0; i < BARK_PEND_WORDS; i++)
		words[i] = readl(bark_gicd + 0x200 + 4 * i);
}

static int bark_probe_phase1(void)
{
	struct mtk_wdt_dev *mtk_wdt = bark_wdt;
	void __iomem *base;
	u32 mode, length, status = 0;
	unsigned long deadline;
	bool tout = false;
	int rc;

	if (!mtk_wdt)
		return -ENODEV;
	if (atomic_cmpxchg(&bark_probe_busy, 0, 1))
		return -EBUSY;
	base = mtk_wdt->wdt_base;

	mode = ioread32(base + WDT_MODE);
	length = ioread32(base + WDT_LENGTH);

	/* 界标行直落 console-ramoops：若 bark 即挂死，此行 = 死亡界标 */
	pr_emerg("mtk-wdt: bark probe phase1: arm short(6s), bark due +6s, no GICD access, mode=0x%08x\n",
		 mode);
	iowrite32(WDT_LENGTH_TIMEOUT(BARK_PROBE_SECS << 6) |
		  WDT_LENGTH_KEY, base + WDT_LENGTH);
	iowrite32((mode | WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN |
		   WDT_MODE_EXRST_EN) | WDT_MODE_KEY, base + WDT_MODE);
	iowrite32(WDT_RST_RELOAD, base + WDT_RST);

	deadline = jiffies + msecs_to_jiffies(BARK_PROBE_CAP_MS);
	while (time_before(jiffies, deadline)) {
		usleep_range(2000, 4000);
		status = ioread32(base + WDT_STATUS_REG);
		if (status & WDT_STATUS_TOUT_STA) {
			tout = true;
			break;
		}
	}

	/* 回写原 LENGTH 踢狗——bite 在 bark+6s，8s 时限恒在其前 */
	iowrite32(length | WDT_LENGTH_KEY, base + WDT_LENGTH);
	iowrite32(WDT_RST_RELOAD, base + WDT_RST);

	bark_phase1_alive = true;
	pr_emerg("mtk-wdt: bark probe phase1 done: tout=%d status=0x%08x (alive; bark %s)\n",
		 tout, status,
		 tout ? "asserted benignly" : "asserted, TOUT_STA silent");
	rc = tout ? 1 : 0;
	atomic_set(&bark_probe_busy, 0);
	return rc;
}

static int bark_probe_phase2(void)
{
	struct mtk_wdt_dev *mtk_wdt = bark_wdt;
	void __iomem *base;
	u32 before[BARK_PEND_WORDS], after[BARK_PEND_WORDS];
	u32 diff[BARK_PEND_WORDS] = { 0 }, mode, length;
	int round, i, hits, armed_ok = 0;

	if (!mtk_wdt)
		return -ENODEV;
	if (!bark_phase1_alive) {
		pr_warn("mtk-wdt: bark probe: run phase1 first (echo 1 > bark_probe)\n");
		return -EPERM;
	}
	if (bark_armed) {
		pr_info("mtk-wdt: bark probe: already armed SPI %d\n",
			bark_irq_spi);
		return 0;
	}
	if (atomic_cmpxchg(&bark_probe_busy, 0, 1))
		return -EBUSY;

	bark_gicd_map();
	base = mtk_wdt->wdt_base;
	if (!bark_gicd) {
		atomic_set(&bark_probe_busy, 0);
		return -ENXIO;
	}

	mode = ioread32(base + WDT_MODE);
	for (round = 0; round < BARK_PROBE_MAX_ATTEMPTS && !armed_ok; round++) {
		unsigned long deadline, t0 = jiffies;
		bool tout_round;

		length = ioread32(base + WDT_LENGTH);
		bark_ispendr_read(before);

		iowrite32(WDT_LENGTH_TIMEOUT(BARK_PROBE_SECS << 6) |
			  WDT_LENGTH_KEY, base + WDT_LENGTH);
		iowrite32((mode | WDT_MODE_IRQ_EN | WDT_MODE_DUAL_EN |
			   WDT_MODE_EXRST_EN) | WDT_MODE_KEY, base + WDT_MODE);
		iowrite32(WDT_RST_RELOAD, base + WDT_RST);

		/* 等到 bark 过后：TOUT 真则提前，假则 +6.3s 兜底（线已 assert） */
		deadline = jiffies + msecs_to_jiffies(BARK_PROBE_CAP_MS);
		while (time_before(jiffies, deadline)) {
			usleep_range(2000, 4000);
			if (ioread32(base + WDT_STATUS_REG) &
			    WDT_STATUS_TOUT_STA)
				break;
			if (time_after(jiffies, t0 +
			    msecs_to_jiffies(BARK_PROBE_BARK_AT_MS)))
				break;
		}
		tout_round = !!(ioread32(base + WDT_STATUS_REG) &
				WDT_STATUS_TOUT_STA);
		if (tout_round)
			bark_tout_rounds++;
		msleep(20);
		bark_ispendr_read(after);

		/* 回写原 LENGTH 踢狗——bite 在 bark+6s，8s 时限恒在其前 */
		iowrite32(length | WDT_LENGTH_KEY, base + WDT_LENGTH);
		iowrite32(WDT_RST_RELOAD, base + WDT_RST);

		hits = 0;
		for (i = 0; i < BARK_PEND_WORDS; i++)
			diff[i] = after[i] & ~before[i];
		for (i = 0; i < BARK_PEND_WORDS; i++) {
			u32 m = diff[i];

			while (m) {
				pr_info("mtk-wdt: bark probe r%d: new pending SPI %d\n",
					round + 1, i * 32 + ffs(m) - 1);
				m &= m - 1;
				hits++;
			}
		}
		if (!hits) {
			if (tout_round) {
				bark_zero_rounds++;
				if (bark_zero_rounds >= BARK_PROBE_HIT_ROUNDS) {
					pr_warn("mtk-wdt: bark probe: TOUT 佐证 %d 轮零新增 pending —— bark 不进 GIC，此路判死\n",
						bark_zero_rounds);
					break;
				}
				pr_info("mtk-wdt: bark probe r%d: TOUT 佐证但零新增 pending（%d/%d）\n",
					round + 1, bark_zero_rounds,
					BARK_PROBE_HIT_ROUNDS);
			} else {
				pr_info("mtk-wdt: bark probe r%d: 零新增 pending（疑时窗未逮或踢狗推迟），重试\n",
					round + 1);
			}
			msleep(2000);
			continue;
		}
		bark_zero_rounds = 0;
		bark_runs++;
		if (bark_runs == 1)
			memcpy(bark_pend_and, diff, sizeof(diff));
		else
			for (i = 0; i < BARK_PEND_WORDS; i++)
				bark_pend_and[i] &= diff[i];
		hits = 0;
		for (i = 0; i < BARK_PEND_WORDS; i++)
			hits += hweight32(bark_pend_and[i]);
		pr_info("mtk-wdt: bark probe: %d 轮交集剩 %d 候选\n",
			bark_runs, hits);
		if (hits == 1 && bark_runs >= BARK_PROBE_HIT_ROUNDS &&
		    bark_tout_rounds > 0) {
			for (i = 0; i < BARK_PEND_WORDS; i++)
				if (bark_pend_and[i])
					break;
			bark_irq_spi = i * 32 + ffs(bark_pend_and[i]) - 1;
			pr_info("mtk-wdt: bark probe: SPI %d 交集唯一且 TOUT 佐证，自动 arm\n",
				bark_irq_spi);
			armed_ok = mtk_wdt_bark_arm() == 0;
		}
		msleep(2000);
	}
	pr_info("mtk-wdt: bark probe phase2 done: hits=%d(tout %d) armed=%d\n",
		bark_runs, bark_tout_rounds, bark_armed);
	atomic_set(&bark_probe_busy, 0);
	return armed_ok ? 0 : (bark_runs ? -EAGAIN : 0);
}

static int bark_probe_set(const char *val, const struct kernel_param *kp)
{
	int v, ret;

	ret = kstrtoint(val, 0, &v);
	if (ret)
		return ret;
	if (v == 1)
		return bark_probe_phase1();
	if (v == 2)
		return bark_probe_phase2();
	return -EINVAL;
}

static int bark_probe_get(char *buf, const struct kernel_param *kp)
{
	return scnprintf(buf, PAGE_SIZE, "phase1_alive=%d runs=%d\n",
			 bark_phase1_alive, bark_runs);
}

static const struct kernel_param_ops bark_probe_ops = {
	.set = bark_probe_set,
	.get = bark_probe_get,
};
module_param_cb(bark_probe, &bark_probe_ops, NULL, 0644);
MODULE_PARM_DESC(bark_probe,
		 "1: phase1 bark benignity/TOUT truth (no GICD access); 2: phase2 SPI discovery (needs phase1 alive); read: status");

module_param(timeout, uint, 0);
MODULE_PARM_DESC(timeout, "Watchdog heartbeat in seconds");

module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

module_param(disable_extrst, bool, 0644);
MODULE_PARM_DESC(disable_extrst,
		 "Do not set WDT_MODE_EXRST_EN (external reset output) when starting the watchdog");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Matthias Brugger <matthias.bgg@gmail.com>");
MODULE_DESCRIPTION("Mediatek WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
