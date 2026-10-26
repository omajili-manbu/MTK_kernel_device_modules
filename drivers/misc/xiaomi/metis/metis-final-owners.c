// SPDX-License-Identifier: GPL-2.0-only
/* Genuine METIS state owners. Defaults come from stock loaded .data bytes
 * and the ELF's zero-initialized .bss, not substitute policy defaults.
 * State that only served the closed-scheduler call sites absent on 6.18
 * (private LB/TP slots, WALT registries, sched-doctor histories, stock
 * islands, pick-side queue bookkeeping) is dropped. */
#include <linux/cache.h>
#include "metis-final-owners.h"

/* Link state. */
s32 link_debug;
u32 mi_lock_blocked_pid;
struct proc_dir_entry *mi_art_mutex_entry;
u64 mi_art_mutex_set_times, mi_art_mutex_unset_times;
s32 mi_art_mutex_owner_pid;
DEFINE_SPINLOCK(mi_art_mutex_lock);

/* Cluster affinity. */
u32 minor_window_app, tsched_debug;
u32 bug_detect = 1;
u32 cluaff_control = 1;
s32 vip_task_max_cpu_nr = 7;
struct cpumask viptask_use_cpumask;
struct ctl_table_header *clu_aff_header;
u32 mi_rebind_task[4][5];
unsigned long rebind_task_mlist[512], rebind_task_llist[512];
unsigned long rebind_task_blist[512];
struct cpumask cluster_affinity_cpumask[4];
unsigned long cluster_affinity_uid_mlist[3125];
unsigned long cluster_affinity_uid_llist[3125];
unsigned long cluster_affinity_uid_blist[3125];
u8 cluster_core_init_suc;
struct cpumask cluster_core_mask[4];

/* Frequency boost. */
u32 mi_boost_duration = 40;
unsigned long thermal_limit_freq[3] = { ~0UL, ~0UL, ~0UL };
s32 mi_freq_enable = 1;
unsigned long max_limit_freq[3] = { ~0UL, ~0UL, ~0UL };
u32 user_min_freq[3] = { 0, 1000000, 1100000 };
struct notifier_block init_fboost_notifier = {
	.notifier_call = metis_freq_qos_request_init,
};
unsigned int n_freq = 3;
u32 min_cluster_freqs[3] = { 1324800, 1785600, 1708800 };
unsigned int n_user_freq = 3;
DECLARE_WORK(finish_fboost_work, finish_fboost_workfn);
s32 fboost_debug;
u32 in_perf_mod;
struct metis_fboost_execution fboost_exe_mode;
DEFINE_RAW_SPINLOCK(fboost_pending_lock);
u8 fboost_pending;
struct metis_cluster_boost_state bcluster_stat[3] __aligned(8);
s32 cluster_states[3];
struct task_struct *fboost_thread;
unsigned long cpus_visit_0; /* Original local cpus_visit.0. */
struct metis_cluster_boost_timer boost_timers[3];
struct metis_cluster_adjust_work rem_cluster_works[3];
struct workqueue_struct *metis_wq;
struct metis_cluster_max_notifier nb_max_limit[3];

/* FAS control surface. */
u32 binder_boost_msec = 10;
u32 fperiod = 8;
u32 task_enqueue_time_toolong = 3000;
char metis_version_str[64] = "4.0";	/* rodin 6.18：原名 version 与 mtkcam cam_cal_config.o 撞全局，改名保 internal 链接 */
s32 show_traced_uid;
unsigned int metis_debug;
s32 mi_fboost_enable;
u32 limit_bgtask_sched;
s32 thermal_break_enable;
atomic_t mi_sdk_eboost_vip_num = ATOMIC_INIT(0);
unsigned long mi_link_boost_jif;
s32 speed_touch_producer_in_boost, speed_touch_consumer_in_boost;
s32 speed_touch_enable, trace_motion_event;
s64 touch_up_start;
atomic_t mi_dynamic_vip_num = ATOMIC_INIT(0);
unsigned int ui_last_cpu, ren_last_cpu, sf_last_cpu, hwc_last_cpu;
unsigned int foreground_uid;
struct proc_dir_entry *proc_fas_file;
struct metis_vip_detect_slot vip_task_detect[3];
s64 vtask_monitor_beg_us;
s64 frame_boost_start;
u32 mi_viptask[3];
s64 vtask_monitor_end_us;
s32 last_traced_uid;
struct workqueue_struct *my_wq;
s32 comm_monitor_debug;
s32 set_static_vip;

/* Cold start/locks. */
s32 protect_cs = 6, cs_protect_dur = 1000;
u32 cs_stat_type = 15;
u32 coldstart_set_mmlock_holder_vip = 1;
u32 set_cold_start_render_vip = 1;
u32 override_schedboost_in_coldstart = 1;
s32 set_possible_notifythr_vip = 1;
u32 rwsem_normal_spin_thres = 15000, rwsem_vip_spin_thres = 40000;
u32 mutex_normal_spin_thres = 60000, mutex_vip_spin_thres = 100000;
u32 read_trylock_reserved_jif = 1, ui_blocked_reader_jif = 2;
u32 reader_biased_jif = 1;
unsigned long rtg_handoff_min_inter = 1;
s32 cold_start_sserver_tgid, cold_start_app_tgid;
s32 home_tgid, systemui_tgid, systemserver_tgid, sf_tgid;
void *cold_start_app_mmlock, *cold_start_sserver_mmlock;
void *home_mmlock, *systemui_mmlock, *systemserver_mmlock, *sf_mmlock;
s32 last_ui_forked_pid;
unsigned long cold_start_ui_futex_wait;
u32 reader_biased_jif_for_vip;
unsigned long rtg_rwsem_handoff_jif;
s32 cs_debug;
unsigned long last_ui_forked_jif, coldstart_end_jif, coldstart_begin_jif;
s32 metis_wakeup_enable;
u32 metis_wakeup_debug;
char app_wakeup_task[240];

/* Persist VIP. */
DEFINE_SPINLOCK(persist_vip_lock);
s32 persist_vip_pids[8];

/* All original .data..percpu+0..0x303 bytes are zero. */
DEFINE_PER_CPU(struct freq_qos_request, metis_req_min);
DEFINE_PER_CPU(struct freq_qos_request, metis_req_max);
