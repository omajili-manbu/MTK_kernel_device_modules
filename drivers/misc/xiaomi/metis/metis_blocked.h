/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_METIS_BLOCKED_H
#define _XIAOMI_METIS_BLOCKED_H
#include <linux/types.h>
#include "metis_state.h"
#include "metis_link.h"
struct mutex;
struct rw_semaphore;
struct list_head;
/* Recovered OEM rwsem payload, not a native rwsem layout. Original private
 * struct/member spelling remains unknown. (rodin 174: mutex payload retired.) */
struct metis_rwsem_oem_unknown {
	u64 unknown_00;
	u64 deadline_08;
};
/* W2 is used as a low-bit reader flag in the raw static-key trace tail.
 * Original formal C/CFI spelling is unknown; bool is the observed payload. */
typedef bool metis_rwsem_reader_flag_unknown;
void record_mutex_blocked_info(struct task_struct *task, struct mutex *lock);
void clear_mutex_blocked_info(struct task_struct *task, struct mutex *lock);
void record_rwsem_blocked_info(struct task_struct *task,
			      struct rw_semaphore *sem,
			      metis_rwsem_reader_flag_unknown reader);
void clear_rwsem_blocked_info(struct task_struct *task, struct rw_semaphore *sem);
void set_rwsem_vip_blocked(struct rw_semaphore *sem);
/* Native trace/hooks/dtask.h and rwsem.h signatures include probe data.
 * Reader-owned's native wlist argument is ignored by the stock body. */
void mi_mutex_wait_finish_hook(void *unused, struct mutex *lock);
void mi_rwsem_read_wait_finish_hook(void *unused, struct rw_semaphore *sem);
void mi_record_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem);
void mi_clear_rwsem_writer_owned_hook(void *unused, struct rw_semaphore *sem);
void mi_record_rwsem_reader_owned_hook(void *unused, struct rw_semaphore *sem,
				      struct list_head *wlist);
#endif
