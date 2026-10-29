/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */
#include "sugov/cpufreq.h"
#include "sugov/dsu_interface.h"
#include "sugov/sched_version_ctrl.h"
#include "eas_trace.h"
#if IS_ENABLED(CONFIG_MTK_THERMAL_INTERFACE)
#include <thermal_interface.h>
#endif

unsigned long (*get_freq_volt_hook)(int cpu, unsigned long freq, int quant, int wl);
EXPORT_SYMBOL(get_freq_volt_hook);

unsigned long pd_get_freq_volt(int cpu, unsigned long freq, int quant, int wl)
{
	if (get_freq_volt_hook)
		return get_freq_volt_hook(cpu, freq, quant, wl);

	return 0;
}

unsigned long (*get_cpu_power_hook)(unsigned int mtk_em, unsigned int get_lkg,
	int quant, int wl, int *dpt_pwr_eff_val, int *val_s, int val_m, int r_o,
	int this_cpu, int *cpu_temp, int opp, unsigned int cpumask_val,
	unsigned long *data, unsigned long *output);
EXPORT_SYMBOL(get_cpu_power_hook);

unsigned long get_cpu_power(unsigned int mtk_em, unsigned int get_lkg,
	int quant, int wl, int *val_s, int r_o, int caller,
	int this_cpu, int *cpu_temp, int opp, unsigned int cpumask_val,
	unsigned long *data, unsigned long *output, struct em_perf_domain *pd,
	unsigned long freq, unsigned long max_util)
{
	unsigned long sum_util;
	struct em_perf_state *states;
	unsigned long cost;
	int i;

	if (get_cpu_power_hook) {
		unsigned long result;
		int dpt_pwr_eff_val[5];

		result = get_cpu_power_hook(mtk_em, get_lkg, quant, wl,
				dpt_pwr_eff_val, val_s, val_m, r_o,
				this_cpu, cpu_temp, opp, cpumask_val, data, output);

		record_sched_pd_opp2pwr_eff(this_cpu, opp, quant, wl,
			dpt_pwr_eff_val[0], dpt_pwr_eff_val[1], dpt_pwr_eff_val[2],
			dpt_pwr_eff_val[3], dpt_pwr_eff_val[4], val_s, r_o, caller);

		return result;
	}

	sum_util = data[0];
	/* rodin 6.9：6.18 的 em_pd_get_efficient_state() 从 6 参收成 3 参，
	 * min/max 性能态由函数自己从 pd->min_perf_state/max_perf_state 读
	 * （include/linux/energy_model.h:200-206），struct em_perf_domain 也已无
	 * min_ps/max_ps 成员 ⇒ 按 mainline 调用形态改写（同头 :267 样例）。 */
	/*
	 * rodin 6.18（2026-10-16）：em_table 是 __rcu 指针（EM 表可热更新），读者
	 * 必须持 rcu_read_lock() 并经 em_perf_state_from_pd() 取表；cost 在
	 * 锁内读成标量，锁外不再触碰表内存。
	 */
	rcu_read_lock();
	states = em_perf_state_from_pd(pd);
	i = em_pd_get_efficient_state(states, pd, max_util);
	cost = states[i].cost;
	rcu_read_unlock();

	return cost * sum_util;
}

unsigned long (*get_cpu_pwr_eff_hook)(int cpu, unsigned long pd_freq, int quant, int wl,
	int *dpt_opp_val, int *dpt_pwr_eff_val, int *val_s, int val_m, int r_o,
	int temperature, unsigned long extern_volt, unsigned long *output);
EXPORT_SYMBOL(get_cpu_pwr_eff_hook);

unsigned long get_cpu_pwr_eff(int cpu, unsigned long pd_freq,
	int quant, int wl, int *val_s, int r_o, int caller,
	int temperature, unsigned long extern_volt, unsigned long *output)
{
	if (get_cpu_pwr_eff_hook) {
		unsigned long result;
		int dpt_opp_val[3];
		int dpt_pwr_eff_val[5];

		result = get_cpu_pwr_eff_hook(cpu, pd_freq, quant, wl,
				dpt_opp_val, dpt_pwr_eff_val, val_s, val_m, r_o,
				temperature, extern_volt, output);

		record_sched_pd_opp2cap(cpu, output[0], quant, wl,
			dpt_opp_val[0], dpt_opp_val[1], dpt_opp_val[2], val_s, r_o, caller);

		record_sched_pd_opp2pwr_eff(cpu, output[0], quant, wl,
			dpt_pwr_eff_val[0], dpt_pwr_eff_val[1], dpt_pwr_eff_val[2],
			dpt_pwr_eff_val[3], dpt_pwr_eff_val[4], val_s, r_o, caller);

		return result;
	}

	return 0;
}

int (*dsu_freq_changed_hook)(void *private);
EXPORT_SYMBOL(dsu_freq_changed_hook);

int dsu_freq_changed(void *private)
{
	if (dsu_freq_changed_hook)
		return dsu_freq_changed_hook(private);

	return 0;
}

void (*eenv_dsu_init_hook)(void *private, int quant, unsigned int wl,
		int PERCORE_L3_BW, unsigned int cpumask_val,
		void __iomem *base, void __iomem *dsu_ctrl_base, unsigned long *pd_base_freq,
		unsigned int dsu_ceiling_freq, int dsu_temp,
		unsigned int *val, unsigned int *output);
EXPORT_SYMBOL(eenv_dsu_init_hook);

void eenv_dsu_init(void *private, int quant, unsigned int wl,
		int PERCORE_L3_BW, unsigned int cpumask_val, unsigned long *pd_base_freq,
		unsigned int *val, unsigned int *output)
{
	/* rodin 6.18（2026-10-26）：hook 恒 NULL（全树无赋值者）⇒ 调用体删除；
	 * 函数壳与指针保留（EXPORT 面，blob ABI）。
	 */
	if (eenv_dsu_init_hook) {
	}
}

unsigned long (*update_dsu_status_hook)(void *private, int quant, unsigned int wl,
		unsigned int gear_idx, unsigned long freq,
		int this_cpu, int dst_cpu, unsigned int *output);
EXPORT_SYMBOL(update_dsu_status_hook);

unsigned long update_dsu_status(struct energy_env *eenv, int quant,
		unsigned long freq, int this_cpu, int dst_cpu)
{
	if (update_dsu_status_hook) {
		unsigned long dsu_volt;
		unsigned int output[6];

		dsu_volt = update_dsu_status_hook(eenv->android_vendor_data1, quant,
			eenv->wl_dsu, eenv->gear_idx, freq, this_cpu, dst_cpu, output);

		if (trace_sched_dsu_freq_enabled())
			trace_sched_dsu_freq(eenv->gear_idx, dst_cpu, output[0], output[1],
					output[2], output[3], (unsigned long)output[4],
					(unsigned long)output[5], dsu_volt);

		return dsu_volt;
	}

	return 0;
}

bool PERCORE_L3_BW;

void init_percore_l3_bw(void)
{
	PERCORE_L3_BW = sched_percore_l3_bw_get();
}

unsigned long (*mtk_get_dsu_pwr_hook)(int wl, int dst_cpu, unsigned long task_util,
		unsigned long total_util, void *private, unsigned int extern_volt,
		int dsu_pwr_enable, int PERCORE_L3_BW, void __iomem *base, int *data);
EXPORT_SYMBOL(mtk_get_dsu_pwr_hook);
unsigned long get_dsu_pwr(int wl, int dst_cpu, unsigned long task_util,
		unsigned long total_util, void *private, unsigned int extern_volt,
		int dsu_pwr_enable)
{
	if (mtk_get_dsu_pwr_hook) {
		unsigned long ret;
		int data[8];

		/* rodin 6.18（2026-10-26）：get_clkg_sram_base_addr() 调用点随
		 * hook 恒 NULL 分支删除；data[8] 填零保持 trace 面类型完整。
		 */
		memset(data, 0, sizeof(data));
		ret = 0;

		if (trace_dsu_pwr_cal_enabled()) {
			trace_dsu_pwr_cal(dst_cpu, task_util, total_util, data[0],
					data[1], data[2], data[3], data[4], extern_volt,
					data[5], data[6], data[7]);
		}

	return ret;
	}
	return 0;
}
