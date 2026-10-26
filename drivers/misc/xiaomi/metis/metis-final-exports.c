// SPDX-License-Identifier: GPL-2.0-only
/* METIS export surface. The stock 148-symbol table mirrored call sites in
 * Xiaomi's closed scheduler; this 6.18 tree has none of them, so only the
 * symbols with in-tree consumers (the module's own subsystem boundaries)
 * remain exported. */
#include <linux/export.h>
#include "metis_common.h"
#include "metis_state.h"
#include "metis_coldstart.h"
#include "metis_extra_controls.h"
#include "metis_link.h"
#include "metis_lock_wait.h"
#include "metis_vip_queue.h"
#include "metis_cluster_boost.h"
#include "metis_wakeup.h"
#include "metis-final-control.h"
#include "metis-final-link.h"
#include "metis-final-boost.h"
#include "metis-final-owners.h"

EXPORT_SYMBOL_GPL(mi_resched_vip_task);
EXPORT_SYMBOL_GPL(mi_link_vip_task);
EXPORT_SYMBOL_GPL(metis_link_init);
EXPORT_SYMBOL_GPL(metis_link_exit);
EXPORT_SYMBOL_GPL(clu_aff_init);
EXPORT_SYMBOL_GPL(clu_aff_exit);
EXPORT_SYMBOL_GPL(cpu_to_cluster);
EXPORT_SYMBOL_GPL(del_cluster);
EXPORT_SYMBOL_GPL(boost_cluster);
EXPORT_SYMBOL_GPL(restore_boost_cluster);
EXPORT_SYMBOL_GPL(metis_fboost_init);
EXPORT_SYMBOL_GPL(metis_fboost_exit);
EXPORT_SYMBOL_GPL(mi_task_uid);
EXPORT_SYMBOL_GPL(mi_dynamic_vip_task);
EXPORT_SYMBOL_GPL(mi_dynamic_vtask_prio_sched_start);
EXPORT_SYMBOL_GPL(mi_set_fas_req);
EXPORT_SYMBOL_GPL(mi_vip_task);
EXPORT_SYMBOL_GPL(register_vip_task_detect);
EXPORT_SYMBOL_GPL(unregister_vip_task_detect);
EXPORT_SYMBOL_GPL(coldstart_app_tasks);
EXPORT_SYMBOL_GPL(coldstart_sserver_tasks);
EXPORT_SYMBOL_GPL(metis_comm_monitor_init);
EXPORT_SYMBOL_GPL(metis_comm_monitor_exit);
EXPORT_SYMBOL_GPL(get_metis_limit);
EXPORT_SYMBOL_GPL(get_fboost_duration);
