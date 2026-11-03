// SPDX-License-Identifier: GPL-2.0
/*
 * rodin 6.18（2026-11-03）：唤醒 IRQ 中止挂起的冷却闸。
 *
 * 6.18 的挂起重试节拍远快于 6.6（中止后 ~250ms 内即再入 noirq），wlan
 * FW 的断言风暴尚未平息即被再次拉起：每次中止都在 noirq 窗内留下电平
 * 残留，并让 GPU/VCP 参与 abort-unwind 的电源翻转，GPU MCU 状态机在
 * 反复翻转中楔死触发 KE 政策重启。本闸在 pm_system_irq_wakeup（内核
 * 因唤醒 IRQ 中止挂起的唯一入口）上挂 kprobe：每次中止后持住一个
 * wakeup source gate_ms 毫秒，期间新的挂起尝试在进入设备挂起之前即被
 * pm_wakeup_pending 短路（零设备参与、零 GPU/VCP 翻转），风暴平息后
 * 闸自然松开，下一次挂起走完整路径。
 */
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/timer.h>

static unsigned int gate_ms = 2000;
module_param_named(gate_ms, gate_ms, uint, 0644);
MODULE_PARM_DESC(gate_ms, "wakeup-IRQ abort cooldown in ms (0 disables)");

static struct wakeup_source *storm_ws;
static struct timer_list storm_timer;
static struct kprobe storm_kp;

static void storm_timer_fn(struct timer_list *t)
{
	__pm_relax(storm_ws);
}

static int storm_kp_pre(struct kprobe *p, struct pt_regs *regs)
{
	if (!gate_ms)
		return 0;

	mod_timer(&storm_timer, jiffies + msecs_to_jiffies(gate_ms));
	__pm_stay_awake(storm_ws);
	return 0;
}

static int __init rodin_suspend_gate_init(void)
{
	int ret;

	storm_ws = wakeup_source_register(NULL, "rodin_storm_gate");
	if (!storm_ws)
		return -ENOMEM;

	timer_setup(&storm_timer, storm_timer_fn, 0);

	storm_kp.symbol_name = "pm_system_irq_wakeup";
	storm_kp.pre_handler = storm_kp_pre;
	ret = register_kprobe(&storm_kp);
	if (ret) {
		wakeup_source_unregister(storm_ws);
		pr_notice("rodin-suspend-gate: kprobe register failed %d\n", ret);
		return ret;
	}

	pr_notice("rodin-suspend-gate: armed (gate_ms=%u)\n", gate_ms);
	return 0;
}
device_initcall(rodin_suspend_gate_init);

MODULE_LICENSE("GPL");
