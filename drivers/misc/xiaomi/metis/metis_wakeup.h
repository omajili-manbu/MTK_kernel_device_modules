/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_WAKEUP_H
#define _XIAOMI_METIS_WAKEUP_H
#include "metis_coldstart.h"
#include "metis_common.h"
struct kernel_param;

extern u32 metis_wakeup_debug;
/* .bss+0x3bea4, 0xf0 bytes: PR_SET_NAME-matched thread name table. */
extern char app_wakeup_task[240];
bool metis_wakeup_futex_plist_insert(int *priority);
void set_coldstart_wakeup_task(struct task_struct *task);
void wake_up_new_task_hook(void *unused, struct task_struct *task);
void metis_wakeup_try_to_wakeup(struct task_struct *task);
void metis_wakeup_syscall_prctl_finished_hook(void *unused, int option,
		struct task_struct *task);
void metis_wakeup_futex_wait_hook(void *unused, u32 __user *uaddr, int cmd, unsigned int *flags,
		u32 __user *uaddr2);
int set_metis_wakeup_task(const char *value, const struct kernel_param *param);
int metis_wakeup_init(void);
void metis_wakeup_exit(void);
#endif
