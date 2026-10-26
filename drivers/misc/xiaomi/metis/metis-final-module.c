// SPDX-License-Identifier: GPL-2.0-only
/* METIS module lifecycle. Subsystem hooks now attach to the live GKI
 * vendor-hook surface (fair enqueue/dequeue, binder allocation); the stock
 * private registries that had no 6.18 reader are gone. */
#include <linux/cpumask.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/string.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/sched.h>
#include "metis-final-module.h"
#include "metis-final-link.h"
#include "metis-final-control.h"
#include "metis-final-boost.h"
#include "metis_state.h"
#include <linux/vseq.h>
#include "metis_vip_queue.h"
#include "metis-final-owners.h"

/* FAS control surface: /proc/fas plus the fair-class enqueue policy. */
int fas_init(void)
{
	proc_fas_file = proc_create("fas", 0644, NULL, &proc_fas_op);
	if (!proc_fas_file)
		printk("\0013metis-fas: [ST-SCHED-TF-METIS]proc_fas file create failed.\n");
	register_trace_android_rvh_enqueue_task_fair(before_enqueue_vip_task, NULL);
	register_trace_android_rvh_dequeue_task_fair(mi_dequeue_task_fair_hook, NULL);
	printk("\0013metis-fas: FAS: %s: inited!\n", "fas_init");
	return 0;
}

void fas_exit(void)
{
	if (proc_fas_file)
		proc_remove(proc_fas_file);
	tracepoint_synchronize_unregister();
	printk("\0013metis-fas: FAS: %s: exit!\n", "fas_exit");
}

static int __init metis_module_init(void)
{
	int ret;

	register_trace_android_vh_binder_alloc_new_buf_locked(
		android_vh_binder_alloc_new_buf_locked, NULL);
	register_vip_task_detect(1, metis_vvip_task);
	metis_link_init();
	printk("\0016metis-fas: metis_init, after metis_link_init!\n");
	clu_aff_init();
	printk("\0016metis-fas: metis_init, after clu_aff_init!\n");
	metis_fboost_init();
	fas_init();
	metis_comm_monitor_init();
	metis_cs_init();
	metis_wakeup_init();
	ret = misc_register(&metis_misc);
	frame_boost_start = ktime_to_us(ktime_get());
	printk("\0016metis-fas: metis init!\n");
	return ret;
}

static void __exit metis_module_exit(void)
{
	metis_wakeup_exit();
	metis_cs_exit();
	metis_comm_monitor_exit();
	fas_exit();
	metis_fboost_exit();
	clu_aff_exit();
	metis_link_exit();
	unregister_vip_task_detect(1);
	unregister_trace_android_vh_binder_alloc_new_buf_locked(
		android_vh_binder_alloc_new_buf_locked, NULL);
	tracepoint_synchronize_unregister();
	misc_deregister(&metis_misc);
}
vseq_module_init(metis_module_init);
module_exit(metis_module_exit);
MODULE_LICENSE("GPL");
