// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 MediaTek Inc.
 */

#include <linux/module.h>       /* needed by all modules */
#include <linux/init.h>         /* needed by module macros */
#include <linux/fs.h>           /* needed by file_operations* */
#include <linux/miscdevice.h>   /* needed by miscdevice* */
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/device.h>       /* needed by device_* */
#include <linux/vmalloc.h>      /* needed by vmalloc */
#include <linux/uaccess.h>      /* needed by copy_to_user */
#include <linux/fs.h>           /* needed by file_operations* */
#include <linux/slab.h>         /* needed by kmalloc */
#include <linux/poll.h>         /* needed by poll */
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_fdt.h>
#include <linux/ioport.h>
#include <linux/io.h>
//#include <mt-plat/sync_write.h>
#include <mt-plat/aee.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/irqflags.h>
#include <linux/processor.h>
#include <linux/smp.h>
#include "scp_feature_define.h"
#include "scp_ipi_pin.h"
#include "scp_helper.h"
#include "scp_excep.h"
#include "scp_dvfs.h"
#include "scp.h"
#include "sap.h"

#define SCP_SECURE_DUMP_MEASURE 0
#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM) && SCP_SECURE_DUMP_MEASURE
static struct cal {
	uint64_t start, end;
} scpdump_cal[3];
#endif

struct mutex scp_awake_mutexs[SCP_CORE_TOTAL];

/* ==== rodin freeze-line fault injection (scp_fi) ====
 * 靶点：scp_awake_spinlock 临界区（锁内 2 发 EL3 SMC + 最长 100ms 原子轮询，
 * 见摸排报告《6.18-冻结线内建固件面全量摸排-20261026》§7.2 #1）的
 * "固件偶发慢/不返回 → 持锁核丢失 → 其余核关中断自旋蔓延"竞态。
 * root 下经 /sys/kernel/debug/scp_fi/ 触发：
 *   arm    写毫秒数 = organic one-shot（下一次 SCP IPI pre_cb 慢速路径中招）；
 *          写 forever / -1 = 永久档；写 0 = 解除
 *   direct 写毫秒数/forever = 写入核立即持真锁 stall 一次（确定性复现）
 *   trace  0/1 = 本文件 F6SMC 门控打点开关（默认关；本路径不在 F6SMC 17 通道内）
 *   status 只读：armed/trig_cnt/last_cpu/last_ms/trace
 * 注入路径全程关中断忙等，零睡眠原语（POLLING IPI 语境可进 atomic）。
 * 173 轮 SMC 外移后语义：organic 钩子在锁外 = 修复验收面（forever 注入仅丢
 * 调用核，其余核存活 + RCU/HLD 背踪留证）；direct 档刻意持真锁 stall =
 * 阴性对照（仍复现全核自旋蔓延楔死，证明楔点 = 锁本体）。
 */
#define SCP_FI_FOREVER		0xFFFFFFFFu
/* MTK_SIP_TINYSYS_SCP_CONTROL = ARM_SMCCC_CALL_VAL(FAST, SMC64, SIP, 0x528) */
#define SCP_FI_SIP_SCP_CTRL	0xC2000528UL

static u32 scp_fi_armed_ms;	/* 0=off, SCP_FI_FOREVER=forever, else one-shot ms */
static unsigned int scp_fi_trace;
static atomic_t scp_fi_trig_cnt = ATOMIC_INIT(0);
static u32 scp_fi_last_ms;
static unsigned int scp_fi_last_cpu;

static noinline void scp_fi_stall(u32 ms, const char *mode)
{
	unsigned long start = jiffies;

	scp_fi_last_ms = ms;
	scp_fi_last_cpu = raw_smp_processor_id();
	atomic_inc(&scp_fi_trig_cnt);

	pr_info("scp_fi: STALL enter cpu=%u mode=%s ms=%u irqs=%d\n",
		scp_fi_last_cpu, mode, ms, irqs_disabled());

	if (ms == SCP_FI_FOREVER) {
		for (;;)
			cpu_relax();
	}

	while (time_before(jiffies, start + msecs_to_jiffies(ms)))
		cpu_relax();

	pr_info("scp_fi: STALL exit cpu=%u ms=%u\n", raw_smp_processor_id(), ms);
}

/* F6SMC 门控打点：格式对齐既有 F6SMC corpus（死亡帧"末条 ent 无 ret"即点名本路径） */
#define scp_fi_ent(a1, a2)							\
	do {									\
		if (unlikely(READ_ONCE(scp_fi_trace)))				\
			pr_info("F6SMC ent a0=0x%08lx a1=0x%08lx a2=0x%08lx\n",	\
				SCP_FI_SIP_SCP_CTRL, (unsigned long)(a1),	\
				(unsigned long)(a2));				\
	} while (0)
#define scp_fi_ret(r)								\
	do {									\
		if (unlikely(READ_ONCE(scp_fi_trace)))				\
			pr_info("F6SMC ret a0=0x%08lx\n", (unsigned long)(r));	\
	} while (0)
#define scp_fi_ev(fmt, ...)							\
	do {									\
		if (unlikely(READ_ONCE(scp_fi_trace)))				\
			pr_info("scp_fi: " fmt, ##__VA_ARGS__);			\
	} while (0)

static int scp_fi_parse_ms(const char __user *ubuf, size_t len, u32 *out)
{
	char kbuf[24];
	char *p;
	long v;
	int ret;

	if (len >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;
	kbuf[len] = '\0';
	p = strim(kbuf);

	if (sysfs_streq(p, "forever")) {
		*out = SCP_FI_FOREVER;
		return 0;
	}
	ret = kstrtol(p, 0, &v);
	if (ret)
		return ret;
	if (v < 0) {			/* 负数按 forever 处理 */
		*out = SCP_FI_FOREVER;
		return 0;
	}
	if (v > 3600000)		/* 有界档上限 1h，防误写 */
		return -EINVAL;
	*out = (u32)v;
	return 0;
}

static ssize_t scp_fi_arm_write(struct file *filp, const char __user *ubuf,
				size_t len, loff_t *off)
{
	u32 ms;
	int ret;

	ret = scp_fi_parse_ms(ubuf, len, &ms);
	if (ret)
		return ret;
	xchg(&scp_fi_armed_ms, ms);
	if (ms == SCP_FI_FOREVER)
		pr_notice("scp_fi: organic armed FOREVER (next awake_lock slow path takes the fall)\n");
	else if (ms)
		pr_notice("scp_fi: organic armed one-shot ms=%u\n", ms);
	else
		pr_notice("scp_fi: organic disarmed\n");
	return len;
}

static ssize_t scp_fi_direct_write(struct file *filp, const char __user *ubuf,
				   size_t len, loff_t *off)
{
	unsigned long flags;
	u32 ms;
	int ret;

	ret = scp_fi_parse_ms(ubuf, len, &ms);
	if (ret)
		return ret;
	if (!ms)
		return len;
	pr_notice("scp_fi: direct stall cpu=%u ms=%u begin\n",
		  raw_smp_processor_id(), ms);
	spin_lock_irqsave(&scp_awake_spinlock, flags);
	scp_fi_stall(ms, "direct");
	spin_unlock_irqrestore(&scp_awake_spinlock, flags);
	return len;
}

static ssize_t scp_fi_trace_write(struct file *filp, const char __user *ubuf,
				  size_t len, loff_t *off)
{
	char kbuf[8];
	unsigned int v;

	if (len >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;
	kbuf[len] = '\0';
	if (kstrtouint(strim(kbuf), 0, &v))
		return -EINVAL;
	WRITE_ONCE(scp_fi_trace, v ? 1u : 0u);
	pr_notice("scp_fi: trace %s\n", v ? "on" : "off");
	return len;
}

static ssize_t scp_fi_status_read(struct file *filp, char __user *ubuf,
				  size_t len, loff_t *off)
{
	char buf[192];
	u32 armed = READ_ONCE(scp_fi_armed_ms);
	int n;

	n = scnprintf(buf, sizeof(buf),
		      "armed_ms=%u (%s)\ntrig_cnt=%d\nlast_cpu=%u last_ms=%u\ntrace=%u\ntimeout_reg=%u\n",
		      armed,
		      armed == SCP_FI_FOREVER ? "FOREVER" :
		      armed ? "ONE-SHOT" : "off",
		      atomic_read(&scp_fi_trig_cnt),
		      scp_fi_last_cpu, scp_fi_last_ms,
		      scp_fi_trace, scp_awake_timeout);

	return simple_read_from_buffer(ubuf, len, off, buf, n);
}

static const struct file_operations scp_fi_arm_fops = {
	.owner = THIS_MODULE,
	.write = scp_fi_arm_write,
};
static const struct file_operations scp_fi_direct_fops = {
	.owner = THIS_MODULE,
	.write = scp_fi_direct_write,
};
static const struct file_operations scp_fi_trace_fops = {
	.owner = THIS_MODULE,
	.write = scp_fi_trace_write,
};
static const struct file_operations scp_fi_status_fops = {
	.owner = THIS_MODULE,
	.read = scp_fi_status_read,
};

static int __init scp_fi_debugfs_init(void)
{
	struct dentry *dir;

	dir = debugfs_create_dir("scp_fi", NULL);
	if (IS_ERR(dir)) {
		pr_notice("scp_fi: debugfs dir create fail\n");
		return 0;
	}
	debugfs_create_file("arm", 0200, dir, NULL, &scp_fi_arm_fops);
	debugfs_create_file("direct", 0200, dir, NULL, &scp_fi_direct_fops);
	debugfs_create_file("trace", 0600, dir, NULL, &scp_fi_trace_fops);
	debugfs_create_file("status", 0400, dir, NULL, &scp_fi_status_fops);
	pr_notice("scp_fi: debugfs ready /sys/kernel/debug/scp_fi/\n");
	return 0;
}
device_initcall(scp_fi_debugfs_init);


/*
 * acquire scp lock flag, keep scp awake
 * @param scp_core_id: scp core id
 * return  0 :get lock success
 *        -1 :get lock timeout
 */
int scp_awake_lock(void *_scp_id)
{
	enum scp_core_id scp_id = (enum scp_core_id) _scp_id;
	unsigned long spin_flags;
	char *core_id;
	int *scp_awake_count;
	int count = 0;
	int ret = -1;
	unsigned int tmp;

	if (scp_id >= SCP_CORE_TOTAL) {
		pr_notice("%s: SCP ID >= SCP_CORE_TOTAL\n", __func__);
		return ret;
	}

	scp_awake_count = (int *)&scp_awake_counts[scp_id];
	core_id = core_ids_scp[scp_id];

	if (is_scp_ready(scp_id) == 0) {
		pr_notice("%s: %s not enabled\n", __func__, core_id);
		return ret;
	}

	/* scp unlock awake */
	spin_lock_irqsave(&scp_awake_spinlock, spin_flags);
	scp_fi_ev("LOCK acq cpu=%u cnt=%d (%s)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	if (*scp_awake_count > 0) {
		*scp_awake_count = *scp_awake_count + 1;
		scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s fast)\n",
			  raw_smp_processor_id(), *scp_awake_count, __func__);
		spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);
		return 0;
	}
	scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s smc-gap)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);

	/* rodin scp_fi: organic one-shot —— 模拟首发 SMC 不返回
	 * （173 轮 SMC 外移后本钩子在锁外：forever 注入仅丢调用核，其余核存活） */
	{
		u32 fi_ms = xchg(&scp_fi_armed_ms, 0);

		if (unlikely(fi_ms))
			scp_fi_stall(fi_ms, "organic");
	}

	/*set a direct IPI to awake SCP */
	/*pr_debug("scp_awake_lock: try to awake %s\n", core_id);*/
	scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_INFRA_REQ, 0);
	scp_fi_ret(scp_lpm_req_infra());

	/* 173 轮 SMC 外移：重入锁只为寄存器窗口（临界区内零固件调用） */
	spin_lock_irqsave(&scp_awake_spinlock, spin_flags);
	scp_fi_ev("LOCK acq cpu=%u cnt=%d (%s window)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	if (*scp_awake_count > 0) {
		*scp_awake_count = *scp_awake_count + 1;
		scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s window-fast)\n",
			  raw_smp_processor_id(), *scp_awake_count, __func__);
		spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);
		scp_lpm_rel_infra();
		return 0;
	}
	if (scpreg.scpsys_regmap_en)
		regmap_write(scpreg.scpsys_regmap,
				INFRA_IRQ_SET_OFS, 0xA0 | (1 << AP_AWAKE_LOCK));
	else
		writel(0xA0 | (1 << AP_AWAKE_LOCK), INFRA_IRQ_SET);

	count = 0;
	while (++count != SCP_AWAKE_TIMEOUT) {
#if SCP_RECOVERY_SUPPORT
		if (atomic_read(&scp_reset_status) != RESET_STATUS_STOP) {
			pr_notice("%s: resetting scp, break\n", __func__);
			break;
		}
#endif  // SCP_RECOVERY_SUPPORT

		if (scp_wdt_pending_check(0)) {
			pr_notice("%s: wdt, break(us=%d)\n",__func__, count*10);
			break;
		}

		if (scpreg.scpsys_regmap_en) {

			if(scpreg.read_infra_irq_sta_en)
				regmap_read(scpreg.scpsys_regmap,
						INFRA_IRQ_STA_OFS, &tmp);
			else
				regmap_read(scpreg.scpsys_regmap,
						INFRA_IRQ_SET_OFS, &tmp);

		} else {

			if(scpreg.read_infra_irq_sta_en)
				tmp = readl(INFRA_IRQ_STA);
			else
				tmp = readl(INFRA_IRQ_SET);

		}

		if ((tmp & 0xA0) != 0xA0) {
			pr_notice("%s: INFRA_IRQ_SET %x\n", __func__, tmp);
			break;
		}
		if (!((tmp & 0x0f) & (1 << AP_AWAKE_LOCK))) {
			ret = 0;
			break;
		}
		udelay(10);

	}
	/* clear status */
	if (scpreg.scpsys_regmap_en)
		regmap_write(scpreg.scpsys_regmap,
				INFRA_IRQ_CLEAR_OFS, 0xA0 | (1 << AP_AWAKE_LOCK));
	else
		writel(0xA0 | (1 << AP_AWAKE_LOCK), INFRA_IRQ_CLEAR);

	/* scp lock awake success*/
	if (ret != -1)
		*scp_awake_count = *scp_awake_count + 1;
	scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);

	/* 173 轮 SMC 外移：AWAKE_CTRL 在解锁后发（SCP 已被寄存器握手唤醒，
	 * ctrl 仅向固件记账；失败路径原先也在锁外，语义对齐） */
	if (ret != -1) {
		scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_AWAKE_CTRL, IS_AWAKE_LOCK);
		scp_fi_ret(scp_smc_awake_ctrl(IS_AWAKE_LOCK));
	}

	if (ret == -1) {
		pr_notice("%s: awake %s fail.., %dus\n", __func__, core_id, count*10);
		scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_AWAKE_CTRL, IS_AWAKE_FAIL);
		scp_fi_ret(scp_smc_awake_ctrl(IS_AWAKE_FAIL));
#if SCP_RECOVERY_SUPPORT
		/*
		 * It's OK without critical section for below code flow,
		 * recovery machine already consider the race condition
		 * of "scp_reset_status".
		 */
		if (scp_set_reset_status() == RESET_STATUS_STOP) {
			/*
			 * Since SCP may not be accessible in bus hang issue,
			 * then we backup some information before APMCU halt SCP.
			 */
			scp_reousrce_dump();
			dump_u1u2_clock();
			pr_notice("%s: start to reset scp...\n", __func__);
			/*
			 * Based on SCP DE suggestion, force PLL CG enable in order to avoid
			 * be disabled by others.
			 */
			if (scp_dvfs_feature_enable()) {
				pr_notice("[SCP]%s: Enable PLL\n", __func__);
				scp_pll_ctrl_set(PLL_ENABLE, CLK_26M);
			}
#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM)
			if (scpreg.secure_dump) {
				scp_do_halt_set();
			} else {
#else
			{
#endif
			/* trigger halt isr, force scp enter wfi */
			writel(B_GIPC4_SETCLR_0, R_GIPC_IN_SET);
			}

			scp_send_reset_wq(RESET_TYPE_AWAKE);
		} else
			pr_notice("%s: scp resetting\n", __func__);
#endif
	}
	scp_lpm_rel_infra();
	return ret;
}
EXPORT_SYMBOL_GPL(scp_awake_lock);

/*
 * release scp awake lock flag
 * @param scp_core_id: scp core id
 * return  0 :release lock success
 *        -1 :release lock fail
 */
int scp_awake_unlock(void *_scp_id)
{
	enum scp_core_id scp_id = (enum scp_core_id) _scp_id;
	unsigned long spin_flags;
	int *scp_awake_count;
	char *core_id;
	int count = 0;
	int ret = -1;
	unsigned int tmp;

	if (scp_id >= SCP_CORE_TOTAL) {
		pr_notice("%s: SCP ID >= SCP_CORE_TOTAL\n", __func__);
		return -1;
	}

	scp_awake_count = (int *)&scp_awake_counts[scp_id];
	core_id = core_ids_scp[scp_id];

	if (is_scp_ready(scp_id) == 0) {
		pr_notice("%s: %s not enabled\n", __func__, core_id);
		return -1;
	}

	/* scp unlock awake */
	spin_lock_irqsave(&scp_awake_spinlock, spin_flags);
	scp_fi_ev("LOCK acq cpu=%u cnt=%d (%s)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	if (*scp_awake_count > 1) {
		*scp_awake_count = *scp_awake_count - 1;
		scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s fast)\n",
			  raw_smp_processor_id(), *scp_awake_count, __func__);
		spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);
		return 0;
	}
	scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s smc-gap)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);

	/* WE1: set a direct IPI to release awake SCP */
	/*pr_debug("scp_awake_lock: try to awake %s\n", core_id);*/
	scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_INFRA_REQ, 0);
	scp_fi_ret(scp_lpm_req_infra());

	/* 173 轮 SMC 外移：重入锁只为寄存器窗口（临界区内零固件调用） */
	spin_lock_irqsave(&scp_awake_spinlock, spin_flags);
	scp_fi_ev("LOCK acq cpu=%u cnt=%d (%s window)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	if (*scp_awake_count > 1) {
		*scp_awake_count = *scp_awake_count - 1;
		scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s window-fast)\n",
			  raw_smp_processor_id(), *scp_awake_count, __func__);
		spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);
		scp_lpm_rel_infra();
		return 0;
	}
	if (scpreg.scpsys_regmap_en)
		regmap_write(scpreg.scpsys_regmap,
				INFRA_IRQ_SET_OFS, 0xA0 | (1 << AP_AWAKE_UNLOCK));
	else
		writel(0xA0 | (1 << AP_AWAKE_UNLOCK), INFRA_IRQ_SET);

	count = 0;
	while (++count != SCP_AWAKE_TIMEOUT) {
#if SCP_RECOVERY_SUPPORT
		if (atomic_read(&scp_reset_status) != RESET_STATUS_STOP) {
			pr_notice("%s: scp is being reset, break\n", __func__);
			break;
		}

#endif  // SCP_RECOVERY_SUPPORT
		if (scp_wdt_pending_check(0)) {
			pr_notice("%s: wdt, break(us=%d)\n",__func__, count*10);
			break;
		}

		if (scpreg.scpsys_regmap_en) {

			if(scpreg.read_infra_irq_sta_en)
				regmap_read(scpreg.scpsys_regmap,
						INFRA_IRQ_STA_OFS, &tmp);
			else
				regmap_read(scpreg.scpsys_regmap,
						INFRA_IRQ_SET_OFS, &tmp);

		} else {

			if(scpreg.read_infra_irq_sta_en)
				tmp = readl(INFRA_IRQ_STA);
			else
				tmp = readl(INFRA_IRQ_SET);

		}

		if ((tmp & 0xA0) != 0xA0) {
			pr_notice("%s: INFRA7_IRQ_SET %x\n", __func__, tmp);
			break;
		}
		if (!((tmp & 0x0f) & (1 << AP_AWAKE_UNLOCK))) {
			ret = 0;
			break;
		}
		udelay(10);
	}
	/* clear status */
	if (scpreg.scpsys_regmap_en)
		regmap_write(scpreg.scpsys_regmap,
				INFRA_IRQ_CLEAR_OFS, 0xA0 | (1 << AP_AWAKE_UNLOCK));
	else
		writel(0xA0 | (1 << AP_AWAKE_UNLOCK), INFRA_IRQ_CLEAR);

	/* scp unlock awake success*/
	if (ret != -1) {
		if (*scp_awake_count <= 0)
			pr_err("%s:%s awake_count=%d NOT SYNC!\n", __func__,
			core_id, *scp_awake_count);

		if (*scp_awake_count > 0)
			*scp_awake_count = *scp_awake_count - 1;
	}
	scp_fi_ev("LOCK rel cpu=%u cnt=%d (%s)\n",
		  raw_smp_processor_id(), *scp_awake_count, __func__);
	spin_unlock_irqrestore(&scp_awake_spinlock, spin_flags);

	/* 173 轮 SMC 外移：AWAKE_CTRL 在解锁后发（解锁寄存器握手已完成） */
	if (ret != -1) {
		scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_AWAKE_CTRL, IS_AWAKE_UNLOCK);
		scp_fi_ret(scp_smc_awake_ctrl(IS_AWAKE_UNLOCK));
	}

	if (ret == -1) {
		pr_notice("%s: awake %s fail.., %dus\n", __func__, core_id, count*10);
		scp_fi_ent(MTK_TINYSYS_SCP_KERNEL_OP_AWAKE_CTRL, IS_AWAKE_FAIL);
		scp_fi_ret(scp_smc_awake_ctrl(IS_AWAKE_FAIL));
#if SCP_RECOVERY_SUPPORT
		/*
		 * It's OK without critical section for below code flow,
		 * recovery machine already consider the race condition
		 * of "scp_reset_status".
		 */
		if (scp_set_reset_status() == RESET_STATUS_STOP) {
			/*
			 * Since SCP may not be accessible in bus hang issue,
			 * then we backup some information before APMCU halt SCP.
			 */
			scp_reousrce_dump();
			dump_u1u2_clock();
			pr_notice("%s: start to reset scp...\n", __func__);
			/*
			 * Based on SCP DE suggestion, force PLL CG enable in order to avoid
			 * be disabled by others.
			 */
			if (scp_dvfs_feature_enable()) {
				pr_notice("[SCP]%s: Enable PLL\n", __func__);
				scp_pll_ctrl_set(PLL_ENABLE, CLK_26M);
			}
#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM)
			if (scpreg.secure_dump) {
				scp_do_halt_set();
			} else {
#else
			{
#endif
			/* trigger halt isr, force scp enter wfi */
			writel(B_GIPC4_SETCLR_0, R_GIPC_IN_SET);
			}

			scp_send_reset_wq(RESET_TYPE_AWAKE);
		} else
			pr_notice("%s: scp resetting\n", __func__);
#endif
	}
	scp_lpm_rel_infra();
	return ret;
}
EXPORT_SYMBOL_GPL(scp_awake_unlock);

/*
 * scp_sys_reset, reset scp
 */
int scp_sys_full_reset(void)
{
	void *tmp;
#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM) && SCP_SECURE_DUMP_MEASURE
	int idx;
#endif
#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM)
	uint64_t restore_start, restore_end;
#endif

	pr_notice("[SCP] %s\n", __func__);

#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM)
	restore_start = ktime_get_boottime_ns();
	if (scpreg.secure_dump) {
#if SCP_SECURE_DUMP_MEASURE
		memset(scpdump_cal, 0x0, sizeof(scpdump_cal));
		scpdump_cal[0].start = ktime_get_boottime_ns();
		scpdump_cal[1].start = ktime_get_boottime_ns();
#endif

		scp_restore_l2tcm();

#if SCP_SECURE_DUMP_MEASURE
		scpdump_cal[1].end = ktime_get_boottime_ns();
#endif
	} else {
#else
	{
#endif
	/* clear whole TCM */
	memset_io(SCP_TCM, 0, SCP_TCM_SIZE);
	/*copy loader to scp sram*/
	memcpy_to_scp(SCP_TCM, (const void *)(size_t)scp_loader_virt
		, scp_region_info_copy.ap_loader_size);
	/*set info to sram*/
	memcpy_to_scp(scp_region_info, (const void *)&scp_region_info_copy
			, sizeof(scp_region_info_copy));
	sap_restore_l2tcm();
	}

#if SCP_RESERVED_MEM && IS_ENABLED(CONFIG_OF_RESERVED_MEM)
	if (scpreg.secure_dump) {
#if SCP_SECURE_DUMP_MEASURE
		scpdump_cal[2].start = ktime_get_boottime_ns();
#endif
		tmp = (void *)(scp_ap_dram_virt +
			ROUNDUP(scp_region_info_copy.ap_dram_size, 1024)
			* scpreg.core_nums);
		scp_restore_dram();
#if SCP_SECURE_DUMP_MEASURE
		scpdump_cal[2].end = ktime_get_boottime_ns();
		scpdump_cal[0].end = ktime_get_boottime_ns();
		for (idx = 0; idx < 3; idx++) {
			pr_notice("MDebug SCP Cal:%d %lldns\n", idx,
					(scpdump_cal[idx].end - scpdump_cal[idx].start));
		}
#endif
		restore_end = ktime_get_boottime_ns();
		pr_notice("[SCP] Restore: %lld ns\n", (restore_end - restore_start));
	} else {
#else
	{
#endif
	/* reset dram from dram back */
	if ((int)(scp_region_info_copy.ap_dram_size) > 0) {
		tmp = (void *)(scp_ap_dram_virt +
			ROUNDUP(scp_region_info_copy.ap_dram_size, 1024)
			* scpreg.core_nums);
		memset(scp_ap_dram_virt, 0,
			ROUNDUP(scp_region_info_copy.ap_dram_size, 1024)
			* scpreg.core_nums);
		memcpy(scp_ap_dram_virt, tmp,
			scp_region_info_copy.ap_dram_size);
	}
	}
	return 0;
}

int scp_clr_spm_reg(void *unused)
{
	/* AP side write 0x1 to SCP2SPM_IPC_CLR to clear
	 * scp side write 0x1 to SCP2SPM_IPC_SET to set SPM reg
	 * scp set        bit[0]
	 */
	if (scpreg.cfgreg_ap_en)
		writel(0x1, (scpreg.cfgreg_ap + 0x0018));
	else
		writel(0x1, SCP_TO_SPM_REG);

	if(scpreg.ipc_wa)
		scp_do_gpr_clear(7);
	return 0;
}
EXPORT_SYMBOL_GPL(scp_clr_spm_reg);

