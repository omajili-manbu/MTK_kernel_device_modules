// SPDX-License-Identifier: GPL-2.0
/*
 * xiaomi_touch_boost.c — xiaomi_touch_rodin 触摸 IRQ boost 六件族重建（b567）
 *
 * 来源性质：blob 逐指令还原（无任何公开 donor 含此代实现；zenin1504 仅存
 * extern 声明与调用点旁证）。重建证据 = tools/_b567_touch/boost_recon.md
 * （全部结论指回 blob 反汇编行号；槽布局 0x2A78 步进与 0x59C0 对象闭合）。
 *
 * 函数集（blob 可见性原样）：
 *   EXPORT_SYMBOL   init_touch_irq / touch_irq_boost_switch / touch_irq_cpumask /
 *                   remove_touch_irq_boost
 *   非导出          touch_irq_boost / touch_irq_boost_release（由 touch_finger_status
 *                   sysfs store 驱动）
 *   全局非 static   get_xiaomi_touch_boost_data（blob @0x3f8 164B，16 处内联 + 1 out-of-line）
 *   static          xiaomi_touch_set_cpumask（blob @0x4984 284B）
 *   函数内 static   gamemode（blob symtab: touch_irq_boost_switch.gamemode）
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/vseq.h>

#include "xiaomi_touch.h"

/* core.c 定义（blob .bss+0x8 值全局；evdev.c 的同名 static 指针为其 TU 内遮蔽形态） */
extern xiaomi_touch_t xiaomi_touch;

/* blob：init_touch_irq_cpumask（行935-962）与 init_touch_irq_boost（行973-1110）
 * 两个 static 函数被整体内联进 init_touch_irq（4208B 单体）——保持 static 让
 * 编译器自行内联，才能复现 blob 的调用序列形状。 */

/* blob 串面锚：日志门限 E=!=0 / I=>=3 / D=>=4 / V=>=5（boost_recon.md §3.12） */
#define TOUCH_ERR(fmt, args...) \
	do { if (current_log_level) printk(KERN_INFO "[MI_TP_E][%s:%d]: " fmt, \
	     __func__, __LINE__, ##args); } while (0)
#define TOUCH_INFO(fmt, args...) \
	do { if (current_log_level >= 3) printk(KERN_INFO "[MI_TP_I][%s:%d]: " fmt, \
	     __func__, __LINE__, ##args); } while (0)
#define TOUCH_DBG(fmt, args...) \
	do { if (current_log_level >= 4) printk(KERN_INFO "[MI_TP_D][%s:%d]: " fmt, \
	     __func__, __LINE__, ##args); } while (0)
#define TOUCH_VRB(fmt, args...) \
	do { if (current_log_level >= 5) printk(KERN_INFO "[MI_TP_V][%s:%d]: " fmt, \
	     __func__, __LINE__, ##args); } while (0)

/* current_log_level 定义在 xiaomi_touch_core.c（blob GLOBAL .data+0x150） */

/* _b583-XT2：boost 子状态视图 —— blob 里它就是 xiaomi_touch_data_t 的尾部（同体！），
 * 证据三重：① blob 的"boost 槽访问器"就是 get_xiaomi_touch_data @0x3f8（实参/串全同，
 * b567 因 =y 符号撞名才让名 _boost；本文件现直接复用之）；② 元素步长 0x2A78 == 本槽步长
 * 0x2A78（get_xiaomi_touch_data umaddl #0x2a78；两端闭合 &param[1]=.bss+0x2a90、
 * driver_param=.bss+0x5500）；③ 元素 ready 单槽 @0x2A20 后紧接 enable @0x2A24。
 * ⇒ 树侧原独立数组 xiaomi_touch_irq_slot[2]（重复 2×0x2A78）删除，改为元素尾部视图，
 * 视图起点 = offsetof(xiaomi_touch_data_t, boost_state)=0x2A24（下方断言）。 */
struct __packed __aligned(4) xiaomi_touch_irq_boost {
	u8  enable;                      /* 元素+0x2A24 */
	u8  normal_boost_support;        /* +0x2A25 */
	u8  game_boost_support;          /* +0x2A26 */
	u8  need_boost;                  /* +0x2A27 */
	u8  cpu_mask_valid;              /* +0x2A28 */
	u8  cpu_mask_applied;            /* +0x2A29 */
	u8  boosting;                    /* +0x2A2A */
	u8  pad_2a2b;                    /* +0x2A2B */
	int count;                       /* +0x2A2C */
	int normal_freq_num;             /* +0x2A30 */
	int game_freq_num;               /* +0x2A34 */
	int policy_num;                  /* +0x2A38 */
	u32 pad_2a3c;                    /* +0x2A3C */
	u32 *freq;                       /* +0x2A40 */
	u32 *normal_freq;                /* +0x2A48 */
	u32 *game_freq;                  /* +0x2A50 */
	struct cpumask *cpu_mask;        /* +0x2A58 */
	struct cpumask cpu_mask_normal;  /* +0x2A60 */
	struct cpumask cpu_mask_game;    /* +0x2A68 */
	struct freq_qos_request *qos;    /* +0x2A70 */
}; /* __packed __aligned(4)：blob 指针成员相对基址 0x1C/0x24/...（基址 0x2A24 ≡4 mod 8，
      绝对地址 8 对齐）；sizeof == 0x54（0x2A24..0x2A78），下方断言由编译器验证 */

static_assert(sizeof(struct xiaomi_touch_irq_boost) == 0x54);
static_assert(offsetof(xiaomi_touch_data_t, boost_state) == 0x2A24,
	      "boost 视图须落在元素 +0x2A24");
static_assert(sizeof(xiaomi_touch_data_t) == offsetof(xiaomi_touch_data_t, boost_state) + 0x54,
	      "boost 视图须铺满元素尾部至 0x2A78");

/* blob 里无独立 boost 访问器：两侧共用 get_xiaomi_touch_data @0x3f8（其两道串
 * "touch id %d hasn't select, return!"/"panel in touch id %d hasn't register, return!"
 * 即本函数原串，b567 已逐字对齐）。此处仅做类型视图转换，不再自带日志
 * ⇒ 串面 tree-only 项 `get_xiaomi_touch_boost_data` 随之清零。 */
struct xiaomi_touch_irq_boost *get_xiaomi_touch_boost_data(int id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(id);

	if (!xiaomi_touch_data)
		return NULL;
	return (struct xiaomi_touch_irq_boost *)xiaomi_touch_data->boost_state;
}

/* blob xiaomi_touch_set_cpumask @0x4984：LOCAL 284B；上界字面 8（blob 0x49e0），
 * DTS 传入掩码（normal_cpu_mask=0x0f/game_cpu_mask=0x0f）均在低 8 位 */
static void xiaomi_touch_set_cpumask(u32 mask, struct cpumask *out)
{
	int i;

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                   /* 行918 */
	cpumask_clear(out);                                       /* 0x49d0 */
	for (i = 0; i < 8; i++) {                                 /* 0x49dc */
		if (!(mask & BIT(i)))
			continue;
		cpumask_set_cpu(i, out);                              /* ldxr/stxr */
		if (current_log_level >= 3)
			TOUCH_INFO("set cpu[%d] mask", i);                /* 行923 */
	}
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                    /* 行926 */
}

/* blob 内联体 A（行935-962）：static 保持全内联 */
static void init_touch_irq_cpumask(int id, struct device_node *np)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(id);
	u32 val = 0;

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                   /* 行935 */
	if (!boost) {
		if (current_log_level)
			TOUCH_ERR("Invalid params");                      /* 行937 */
		return;
	}

	if (of_property_read_u32(np, "normal_cpu_mask", &val) || !val) {
		if (current_log_level)
			TOUCH_ERR("no normal_cpumask or zero");           /* 行944 */
		/* blob 0x2cec：8 字节单次存 = BITMAP_LAST_WORD_MASK(nr_cpu_ids)
		 * （语义 = 前 nr_cpu_ids 位全 1；非 cpumask_setall，其按 nr_cpumask_bits 补位） */
		boost->cpu_mask_normal.bits[0] = BITMAP_LAST_WORD_MASK(nr_cpu_ids);
	} else {
		if (current_log_level >= 3)
			TOUCH_INFO("normal cpumask %x", val);             /* 行946 */
		boost->cpu_mask_valid = 1;                            /* 0x2A28 */
		xiaomi_touch_set_cpumask(val, &boost->cpu_mask_normal);
	}

	if (of_property_read_u32(np, "game_cpu_mask", &val) || !val) {
		if (current_log_level)
			TOUCH_ERR("no game_cpumask or zero");             /* 行954 */
		boost->cpu_mask_game.bits[0] = BITMAP_LAST_WORD_MASK(nr_cpu_ids);
	} else {
		if (current_log_level >= 3)
			TOUCH_INFO("game cpumask %x", val);               /* 行956 */
		boost->cpu_mask_valid = 1;                            /* 0x2A28 同字节 */
		xiaomi_touch_set_cpumask(val, &boost->cpu_mask_game);
	}

	boost->cpu_mask_applied = 0;                              /* 0x2A29 清 0 */
	boost->cpu_mask = &boost->cpu_mask_normal;                /* 0x2A58 */
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                    /* 行962 */
}

/* blob 内联体 B（行973-1110）：static 保持全内联 */
static void init_touch_irq_boost(int id, struct device_node *np)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(id);
	struct cpufreq_policy *policy;
	int num_policy = 0, i, cpu, cnt, ret;

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                   /* 行973 */
	if (!boost) {
		if (current_log_level)
			TOUCH_ERR("Invalid params");                      /* 行975 */
		return;
	}

	boost->normal_boost_support =
		!!of_find_property(np, "normal_boost_support", NULL); /* 0x2A25 */
	boost->game_boost_support =
		!!of_find_property(np, "game_boost_support", NULL);   /* 0x2A26 */
	if (!boost->normal_boost_support && !boost->game_boost_support) {
		if (current_log_level)
			TOUCH_ERR("not support irq boost");               /* 行982 */
		return;
	}
	if (current_log_level >= 3)
		TOUCH_INFO("normal boost: %d, game boost: %d",
			   boost->normal_boost_support, boost->game_boost_support); /* 行987 */

	/* 数 distinct cpufreq policy（blob 0x3050..0x306c：跳过同簇 last cpu） */
	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		if (current_log_level >= 3)
			TOUCH_INFO("policy[%d]: first:%d, min:%d, max:%d",
				   num_policy, cpu, policy->min, policy->max);   /* 行993 */
		cpu = cpumask_last(policy->related_cpus);
		if (current_log_level >= 3)
			TOUCH_INFO("last cpu: %d", cpu);                  /* 行996 */
		cpufreq_cpu_put(policy);
		num_policy++;
	}
	if (num_policy == 0) {
		boost->need_boost = 0;                                 /* 0x2A27 */
		boost->normal_boost_support = 0;                       /* 0x2A25 */
		boost->game_boost_support = 0;                         /* 0x2A26 */
		if (current_log_level)
			TOUCH_ERR("cpu no policy");                        /* 行1005 */
		return;
	}

	/* normal 频点表 */
	cnt = of_property_count_u32_elems(np, "normal_irq_boost_target");
	if (cnt < 1) {
		if (current_log_level)
			TOUCH_ERR("no need normal touch boost");           /* 行1028 */
		boost->normal_boost_support = 0;                       /* 0x2A25 */
	} else {
		if (current_log_level >= 3)
			TOUCH_INFO("normal boost freq num: %d", cnt);      /* 行1011 */
		boost->normal_freq_num = min(cnt, num_policy);         /* 0x2A30 */
		boost->normal_freq = kcalloc(boost->normal_freq_num,
					     sizeof(u32), GFP_KERNEL);
		if (!boost->normal_freq) {
			if (current_log_level)
				TOUCH_ERR("kcalloc normal freq mem failed!");  /* 行1024 */
			boost->normal_boost_support = 0;
		} else {
			ret = of_property_read_u32_array(np, "normal_irq_boost_target",
					boost->normal_freq, boost->normal_freq_num);
			if (current_log_level >= 3)
				TOUCH_INFO("normal target freq: %d",
					   boost->normal_freq[0]);         /* 行1020 */
			if (ret < 0)
				memset(boost->normal_freq, 0,
				       boost->normal_freq_num * sizeof(u32));
		}
	}

	/* game 频点表（对称） */
	cnt = of_property_count_u32_elems(np, "game_irq_boost_target");
	if (cnt < 1) {
		if (current_log_level)
			TOUCH_ERR("no need game touch boost");             /* 行1051 */
		boost->game_boost_support = 0;                         /* 0x2A26 */
	} else {
		if (current_log_level >= 3)
			TOUCH_INFO("game boost freq num: %d", cnt);        /* 行1034 */
		boost->game_freq_num = min(cnt, num_policy);           /* 0x2A34 */
		boost->game_freq = kcalloc(boost->game_freq_num,
					   sizeof(u32), GFP_KERNEL);
		if (!boost->game_freq) {
			if (current_log_level)
				TOUCH_ERR("kcalloc game freq mem failed!");    /* 行1048 */
			boost->game_boost_support = 0;
		} else {
			ret = of_property_read_u32_array(np, "game_irq_boost_target",
					boost->game_freq, boost->game_freq_num);
			if (current_log_level >= 3)
				TOUCH_INFO("game target freq: %d",
					   boost->game_freq[0]);           /* 行1043 */
			if (ret < 0)
				memset(boost->game_freq, 0,
				       boost->game_freq_num * sizeof(u32));
		}
	}

	/* qos 请求表 */
	boost->policy_num = max(boost->normal_freq_num, boost->game_freq_num); /* 0x2A38 */
	if (boost->policy_num == 0) {
		if (current_log_level)
			TOUCH_ERR("policy_num is 0, disable boost_support"); /* 行1061 */
		goto disable;
	}
	boost->qos = kcalloc(boost->policy_num,
			     sizeof(struct freq_qos_request), GFP_KERNEL);
	if (!boost->qos) {
		if (current_log_level)
			TOUCH_ERR("qos kcalloc failed, disable touch boost"); /* 行1076 */
		goto disable;
	}

	/* _b583-XT③：blob 0x376c-0x37a8 —— qos kcalloc 成功且 current_log_level>=3
	 * （0x3770 `cmp w8,#0x3`、0x3778 `b.hs 0x39f8`）时打一条 I 级日志：
	 *   0x3a08 `ldr w4,[x22,#0x2a38]`（boost->policy_num）
	 *   0x3a0c 串 = .rodata.str1.1+0x14a3 = "[MI_TP_I][%s:%d]: cpu: %d, policy num: %d"
	 *   0x3a1c `mov w2,#0x442`（源行 1090）
	 *   0x3a20 `mov w3,w20` —— w20 = 上一 for_each_possible_cpu 的退出值
	 *        （0x31f0 `mov w20,#0x20`），即被引用的循环变量 cpu
	 * 打完 0x3a28 `b 0x377c` 才 `mov w20,wzr`(i=0) 进入 qos 注册循环
	 * ⇒ 本日志位于「policy 计数循环」与「qos 注册循环」之间，无尾 \n。 */
	if (current_log_level >= 3)
		TOUCH_INFO("cpu: %d, policy num: %d", cpu, boost->policy_num);

	i = 0;
	for_each_possible_cpu(cpu) {
		if (i >= boost->policy_num)                            /* blob 0x37f4 */
			break;
		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		freq_qos_add_request(&policy->constraints, &boost->qos[i],
				     FREQ_QOS_MIN, 0);
		cpu = cpumask_last(policy->related_cpus);
		if (current_log_level >= 3)
			TOUCH_INFO("last cpu: %d", cpu);                   /* 行1101 */
		cpufreq_cpu_put(policy);
		i++;
	}

	/* 生效配置 = normal 模式 */
	boost->count      = boost->normal_freq_num;                /* 0x2A2C */
	boost->freq       = boost->normal_freq;                    /* 0x2A40 */
	boost->need_boost = boost->normal_boost_support;           /* 0x2A27 */
	if (current_log_level >= 3)
		TOUCH_INFO("normal boost num %d, game boost num: %d, policy num: %d",
			   boost->normal_freq_num, boost->game_freq_num,
			   boost->policy_num);                             /* 行1109 */
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                     /* 行1110 */
	return;

disable:
	boost->need_boost = 0;                                     /* 0x2A27 */
	boost->normal_boost_support = 0;                           /* 0x2A25 */
	boost->game_boost_support = 0;                             /* 0x2A26 */
	kfree(boost->normal_freq);
	kfree(boost->game_freq);
	boost->normal_freq = NULL;
	boost->game_freq = NULL;
}

void init_touch_irq(int id, struct device_node *np)
{
	init_touch_irq_cpumask(id, np);
	init_touch_irq_boost(id, np);
}
EXPORT_SYMBOL(init_touch_irq);

void touch_irq_boost_switch(int irq, int enable)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(irq);
	static u8 gamemode;   /* blob symtab: touch_irq_boost_switch.gamemode（函数内 static） */
	u8 mode = enable & 1;

	if (current_log_level >= 5)
		TOUCH_VRB("enabled0:%d, enabled:%d, normal:%d, game:%d",
			  gamemode,
			  boost ? mode : -1,
			  boost ? boost->normal_boost_support : -1,
			  boost ? boost->game_boost_support : -1);         /* 行1130 */
	/* blob 0x3c58 store 先于 0x3c64 NULL 判（clang UB 重排，boost_recon.md §3.3）；
	 * 重建取安全形态：先判空。其余五件 blob 均为先判空形态。 */
	if (!boost)
		return;
	boost->enable = mode;                                       /* 0x2A24 */
	if (boost->enable == gamemode)
		return;
	if (!boost->normal_boost_support && !boost->game_boost_support)
		return;

	if (mode) {
		boost->need_boost = boost->game_boost_support;   /* 0x2A27 = 0x2A26 */
		boost->cpu_mask   = &boost->cpu_mask_game;       /* 0x2A58 = &0x2A68 */
		boost->count      = boost->game_freq_num;        /* 0x2A2C = 0x2A34 */
		boost->freq       = boost->game_freq;            /* 0x2A40 = 0x2A50 */
		if (boost->game_boost_support && current_log_level >= 4)
			TOUCH_DBG("switch game mode, target freq: %d",
				  boost->freq[0]);                          /* 行1142 */
	} else {
		boost->need_boost = boost->normal_boost_support; /* 0x2A27 = 0x2A25 */
		boost->cpu_mask   = &boost->cpu_mask_normal;     /* 0x2A58 = &0x2A60 */
		boost->count      = boost->normal_freq_num;      /* 0x2A2C = 0x2A30 */
		boost->freq       = boost->normal_freq;          /* 0x2A40 = 0x2A48 */
		if (boost->normal_boost_support && current_log_level >= 4)
			TOUCH_DBG("switch normal mode, target freq: %d",
				  boost->freq[0]);                          /* 行1149 */
	}
	boost->cpu_mask_applied = 0;                                /* 0x2A29 */
	gamemode = boost->enable;
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                      /* 行1153 */
}
EXPORT_SYMBOL(touch_irq_boost_switch);

void remove_touch_irq_boost(int irq)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(irq);
	int i;

	if (current_log_level >= 5)
		TOUCH_VRB("need_boost:%d", boost ? boost->need_boost : -1); /* 行1163 */
	if (!boost)
		return;
	if (!boost->need_boost)                                     /* 0x2A27 */
		return;
	for (i = 0; i < boost->policy_num; i++)                     /* 上界=policy_num */
		freq_qos_remove_request(&boost->qos[i]);
	kfree(boost->normal_freq);
	kfree(boost->game_freq);
	kfree(boost->qos);
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                      /* 行1176 */
}
EXPORT_SYMBOL(remove_touch_irq_boost);

void touch_irq_cpumask(int irq)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(irq);

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                     /* 行1184 */
	if (!boost)
		return;
	if (!boost->cpu_mask_valid)                                 /* 0x2A28 */
		return;
	if (!boost->cpu_mask)                                       /* 0x2A58 */
		return;
	if (boost->cpu_mask_applied) {                              /* 0x2A29 */
		if (current_log_level >= 5)
			TOUCH_VRB("exit");                                  /* 行1193 */
		return;
	}
	set_cpus_allowed_ptr(current, boost->cpu_mask);
	boost->cpu_mask_applied = 1;
	if (current_log_level >= 4)
		TOUCH_DBG("set tp irq cpu mask");                       /* 行1191 */
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                      /* 行1193 */
}
EXPORT_SYMBOL(touch_irq_cpumask);

/* 非导出（blob 原样）：由 store_touch_finger_status 调用 */
void touch_irq_boost(int irq)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(irq);
	int i;

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                     /* 行1202 */
	if (!boost)
		goto invalid;
	if (!boost->need_boost)                                     /* 0x2A27 */
		goto invalid;
	if (boost->boosting) {                                      /* 0x2A2A */
		if (current_log_level >= 5)
			TOUCH_VRB("exit");                                  /* 行1215 */
		return;
	}
	boost->boosting = 1;
	for (i = 0; i < boost->count; i++) {                        /* 上界=count */
		freq_qos_update_request(&boost->qos[i], boost->freq[i]);
		if (current_log_level >= 4)
			TOUCH_DBG("boost cpu, target freq: %d", boost->freq[i]); /* 行1212 */
	}
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                      /* 行1215 */
	return;
invalid:
	if (current_log_level >= 5)
		TOUCH_VRB("Invalid params");                            /* 行1204 */
}

void touch_irq_boost_release(int irq)
{
	struct xiaomi_touch_irq_boost *boost = get_xiaomi_touch_boost_data(irq);
	int i;

	if (current_log_level >= 5)
		TOUCH_VRB("enter");                                     /* 行1223 */
	if (!boost) {
		if (current_log_level >= 5)
			TOUCH_VRB("Invalid params");                        /* 行1225 */
		return;
	}
	if (boost->boosting) {                                      /* 0x2A2A */
		boost->boosting = 0;
		for (i = 0; i < boost->policy_num; i++)                 /* 上界=policy_num */
			freq_qos_update_request(&boost->qos[i], 0);
		if (current_log_level >= 4)
			TOUCH_DBG("boost cpu release");                     /* 行1233 */
	}
	if (current_log_level >= 5)
		TOUCH_VRB("exit");                                      /* 行1235 */
}

/* blob def：scp_tp_mistouch_close（框架侧副本，1B u8；goodix/focaltech 各持改名副本）。
 * show/store 语义 = open/close 串切换（blob .rodata.str1.1+0xaaf/0x193e）。 */
u8 scp_tp_mistouch_close;
