// SPDX-License-Identifier: GPL-2.0-only
/* Native interface tables: two misc devices, /proc faces, the clu-aff
 * sysctl and the module parameters of the surviving policy surfaces.
 * Parameters of dropped subsystems (stock pick-side, LB/TP, schlat,
 * sched-doctor histories, io/deadline) are gone with them. */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include "metis-final-owners.h"

static const struct file_operations metis_link_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = metis_link_ioctl,
	.compat_ioctl = metis_link_ioctl,
	.mmap = metis_link_mmap,
	.open = metis_link_open,
	.release = metis_link_release,
};

struct miscdevice metis_link_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "metis_link",
	.fops = &metis_link_fops,
};

const struct proc_ops mi_art_mutex_debug_proc_ops = {
	.proc_open = mi_art_mutex_debug_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct file_operations metis_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = metis_ioctl,
	.compat_ioctl = metis_ioctl,
	.mmap = metis_mmap,
	.open = metis_open,
	.release = metis_release,
};

struct miscdevice metis_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "metis",
	.fops = &metis_fops,
};

const struct proc_ops proc_fas_op = {
	.proc_open = proc_open_fas,
	.proc_read = seq_read,
	.proc_write = proc_write_fas,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/* Original table .data+0x60: one real item and a zero terminator. */
/* rodin 6.18：proc_handler 首参为 const struct ctl_table * ⇒ 表亦须 const */
const struct ctl_table clu_aff_table[2] = {
	{
		.procname = "viptask_maxcpu_nr",
		.data = &vip_task_max_cpu_nr,
		.maxlen = sizeof(vip_task_max_cpu_nr),
		.mode = 0644,
		.proc_handler = proc_viptask_cpus_set,
	},
	{ },
};

static const struct kernel_param_ops param_ops_rebind_task = {
	.set = set_rebind_task,
	.get = get_rebind_task,
};

static const struct kernel_param_ops param_ops_clus_affinity_uidlist = {
	.set = set_clus_affinity_uidlist,
	.get = get_clus_affinity_uidlist,
};

static const struct kernel_param_ops param_ops_trace_motion_event = {
	.set = set_trace_motion_event,
	.get = param_get_int,
};

static const struct kernel_param_ops param_ops_speed_touch_enable = {
	.set = set_speed_touch_enable,
	.get = param_get_int,
};

static const struct kernel_param_ops param_ops_mi_viptask = {
	.set = set_mi_vip_task_req,
	.get = get_mi_viptask,
};

static const struct kernel_param_ops param_ops_efboost = {
	.set = set_efboost,
	.get = NULL,
};

static const struct kernel_param_ops param_ops_therm_break = {
	.set = set_therm_break_enable,
	.get = get_therm_break_enable,
};

static const struct kernel_param_ops param_ops_fboost = {
	.set = set_fboost_enable,
	.get = get_fboost_enable,
};

static const struct kernel_param_ops param_ops_set_static_vip = {
	.set = func_set_static_vip,
	.get = NULL,
};

static const struct kernel_param_ops param_ops_sserver_tgid = {
	.set = set_sserver_tgid,
	.get = get_sserver_tgid,
};

static const struct kernel_param_ops param_ops_metis_wakeup_task = {
	.set = set_metis_wakeup_task,
	.get = NULL,
};

static const struct kernel_param_ops param_ops_common_persist_vip_task = {
	.set = set_common_persist_vip_task,
	.get = get_common_persist_vip_task,
};

static const struct kernel_param_ops param_ops_clear_persist_vip_task = {
	.set = set_clear_persist_vip_task,
	.get = NULL,
};

module_param_cb(link_debug, &param_ops_uint, &link_debug, 0644);
__MODULE_PARM_TYPE(link_debug, "uint");
module_param_cb(mi_lock_blocked_pid, &param_ops_uint, &mi_lock_blocked_pid, 0644);
__MODULE_PARM_TYPE(mi_lock_blocked_pid, "uint");
module_param_cb(minor_window_app, &param_ops_uint, &minor_window_app, 0644);
__MODULE_PARM_TYPE(minor_window_app, "uint");
module_param_cb(tsched_debug, &param_ops_uint, &tsched_debug, 0644);
__MODULE_PARM_TYPE(tsched_debug, "uint");
module_param_cb(bug_detect, &param_ops_uint, &bug_detect, 0644);
__MODULE_PARM_TYPE(bug_detect, "uint");
module_param_cb(cluaff_control, &param_ops_uint, &cluaff_control, 0644);
__MODULE_PARM_TYPE(cluaff_control, "uint");
module_param_cb(add_rebind_task_big, &param_ops_rebind_task, NULL, 0644);
module_param_cb(del_rebind_task_big, &param_ops_rebind_task, NULL, 0644);
module_param_cb(add_rebind_task_mid, &param_ops_rebind_task, NULL, 0644);
module_param_cb(del_rebind_task_mid, &param_ops_rebind_task, NULL, 0644);
module_param_cb(add_rebind_task_lit, &param_ops_rebind_task, NULL, 0644);
module_param_cb(del_rebind_task_lit, &param_ops_rebind_task, NULL, 0644);
module_param_cb(show_rebind_task_big, &param_ops_rebind_task, NULL, 0644);
module_param_cb(show_rebind_task_mid, &param_ops_rebind_task, NULL, 0644);
module_param_cb(show_rebind_task_lit, &param_ops_rebind_task, NULL, 0644);
module_param_cb(reset_rebind_task, &param_ops_rebind_task, NULL, 0644);
module_param_cb(add_lclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(add_mclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(add_bclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(reset_clus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(del_lclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(del_mclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(del_bclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(show_lclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(show_mclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(show_bclus_affinity_uidlist, &param_ops_clus_affinity_uidlist, NULL, 0644);
module_param_cb(mi_boost_duration, &param_ops_uint, &mi_boost_duration, 0644);
__MODULE_PARM_TYPE(mi_boost_duration, "uint");
module_param_array(min_cluster_freqs, uint, &n_freq, 0644);
module_param_cb(fboost_debug, &param_ops_uint, &fboost_debug, 0644);
__MODULE_PARM_TYPE(fboost_debug, "uint");
module_param_cb(mi_freq_enable, &param_ops_uint, &mi_freq_enable, 0644);
__MODULE_PARM_TYPE(mi_freq_enable, "uint");
module_param_cb(in_perf_mod, &param_ops_uint, &in_perf_mod, 0644);
__MODULE_PARM_TYPE(in_perf_mod, "uint");
module_param_array(user_min_freq, uint, &n_user_freq, 0644);
module_param_cb(binder_boost_msec, &param_ops_uint, &binder_boost_msec, 0644);
__MODULE_PARM_TYPE(binder_boost_msec, "uint");
module_param_cb(fperiod, &param_ops_uint, &fperiod, 0644);
__MODULE_PARM_TYPE(fperiod, "uint");
module_param_cb(metis_debug, &param_ops_uint, &metis_debug, 0644);
__MODULE_PARM_TYPE(metis_debug, "uint");
module_param_cb(mi_fboost_enable, &param_ops_uint, &mi_fboost_enable, 0644);
__MODULE_PARM_TYPE(mi_fboost_enable, "uint");
module_param_cb(limit_bgtask_sched, &param_ops_uint, &limit_bgtask_sched, 0644);
__MODULE_PARM_TYPE(limit_bgtask_sched, "uint");
module_param_cb(thermal_break_enable, &param_ops_uint, &thermal_break_enable, 0644);
__MODULE_PARM_TYPE(thermal_break_enable, "uint");
extern char metis_version_str[64];
module_param_string(version, metis_version_str, 64, 0644);
module_param_cb(task_enqueue_time_toolong, &param_ops_uint, &task_enqueue_time_toolong, 0644);
__MODULE_PARM_TYPE(task_enqueue_time_toolong, "uint");
module_param_cb(trace_motion_event, &param_ops_trace_motion_event, &trace_motion_event, 0644);
module_param_cb(speed_touch_enable, &param_ops_speed_touch_enable, &speed_touch_enable, 0644);
module_param_cb(mi_viptask, &param_ops_mi_viptask, NULL, 0644);
module_param_cb(add_mi_viptask_sched_priority, &param_ops_efboost, NULL, 0644);
module_param_cb(del_mi_viptask_sched_priority, &param_ops_efboost, NULL, 0644);
module_param_cb(add_mi_viptask_enqueue_boost, &param_ops_efboost, NULL, 0644);
module_param_cb(del_mi_viptask_enqueue_boost, &param_ops_efboost, NULL, 0644);
module_param_cb(add_mi_viptask_sched_lit_core, &param_ops_efboost, NULL, 0644);
module_param_cb(del_mi_viptask_sched_lit_core, &param_ops_efboost, NULL, 0644);
module_param_cb(is_break_enable, &param_ops_therm_break, NULL, 0644);
module_param_cb(mpc_fboost_enable, &param_ops_fboost, NULL, 0644);
module_param_cb(comm_monitor_debug, &param_ops_uint, &comm_monitor_debug, 0644);
__MODULE_PARM_TYPE(comm_monitor_debug, "uint");
module_param_cb(set_static_vip, &param_ops_set_static_vip, NULL, 0644);
module_param_cb(protect_cs, &param_ops_uint, &protect_cs, 0644);
__MODULE_PARM_TYPE(protect_cs, "uint");
module_param_cb(cs_protect_dur, &param_ops_uint, &cs_protect_dur, 0644);
__MODULE_PARM_TYPE(cs_protect_dur, "uint");
module_param_cb(cs_stat_type, &param_ops_uint, &cs_stat_type, 0644);
__MODULE_PARM_TYPE(cs_stat_type, "uint");
module_param_cb(coldstart_set_mmlock_holder_vip, &param_ops_uint, &coldstart_set_mmlock_holder_vip, 0644);
__MODULE_PARM_TYPE(coldstart_set_mmlock_holder_vip, "uint");
module_param_cb(cold_start_sserver_tgid, &param_ops_uint, &cold_start_sserver_tgid, 0644);
__MODULE_PARM_TYPE(cold_start_sserver_tgid, "uint");
module_param_cb(set_cold_start_render_vip, &param_ops_uint, &set_cold_start_render_vip, 0644);
__MODULE_PARM_TYPE(set_cold_start_render_vip, "uint");
module_param_cb(override_schedboost_in_coldstart, &param_ops_uint, &override_schedboost_in_coldstart, 0644);
__MODULE_PARM_TYPE(override_schedboost_in_coldstart, "uint");
module_param_cb(set_possible_notifythr_vip, &param_ops_uint, &set_possible_notifythr_vip, 0644);
__MODULE_PARM_TYPE(set_possible_notifythr_vip, "uint");
module_param_cb(rwsem_normal_spin_thres, &param_ops_uint, &rwsem_normal_spin_thres, 0644);
__MODULE_PARM_TYPE(rwsem_normal_spin_thres, "uint");
module_param_cb(rwsem_vip_spin_thres, &param_ops_uint, &rwsem_vip_spin_thres, 0644);
__MODULE_PARM_TYPE(rwsem_vip_spin_thres, "uint");
module_param_cb(mutex_normal_spin_thres, &param_ops_uint, &mutex_normal_spin_thres, 0644);
__MODULE_PARM_TYPE(mutex_normal_spin_thres, "uint");
module_param_cb(mutex_vip_spin_thres, &param_ops_uint, &mutex_vip_spin_thres, 0644);
__MODULE_PARM_TYPE(mutex_vip_spin_thres, "uint");
module_param_cb(read_trylock_reserved_jif, &param_ops_uint, &read_trylock_reserved_jif, 0644);
__MODULE_PARM_TYPE(read_trylock_reserved_jif, "uint");
module_param_cb(ui_blocked_reader_jif, &param_ops_uint, &ui_blocked_reader_jif, 0644);
__MODULE_PARM_TYPE(ui_blocked_reader_jif, "uint");
module_param_cb(reader_biased_jif, &param_ops_uint, &reader_biased_jif, 0644);
__MODULE_PARM_TYPE(reader_biased_jif, "uint");
module_param_cb(reader_biased_jif_for_vip, &param_ops_uint, &reader_biased_jif_for_vip, 0644);
__MODULE_PARM_TYPE(reader_biased_jif_for_vip, "uint");
module_param_cb(rtg_handoff_min_inter, &param_ops_ulong, &rtg_handoff_min_inter, 0644);
__MODULE_PARM_TYPE(rtg_handoff_min_inter, "ulong");
module_param_cb(cs_debug, &param_ops_uint, &cs_debug, 0644);
__MODULE_PARM_TYPE(cs_debug, "uint");
module_param_cb(sserver_tgid, &param_ops_sserver_tgid, NULL, 0644);
module_param_cb(metis_wakeup_enable, &param_ops_uint, &metis_wakeup_enable, 0644);
__MODULE_PARM_TYPE(metis_wakeup_enable, "uint");
module_param_cb(metis_wakeup_debug, &param_ops_uint, &metis_wakeup_debug, 0644);
__MODULE_PARM_TYPE(metis_wakeup_debug, "uint");
module_param_cb(metis_wakeup_task, &param_ops_metis_wakeup_task, NULL, 0644);
module_param_cb(common_persist_vip_task, &param_ops_common_persist_vip_task, NULL, 0644);
module_param_cb(clear_persist_vip_task, &param_ops_clear_persist_vip_task, NULL, 0200);
