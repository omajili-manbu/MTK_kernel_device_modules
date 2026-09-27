/*
* reader shoule NEVER block !!!, if no data is avaliable, just return zero
* writer will block if there is no space, and wakend up by reader or force exit
* there is 1 writer and 1 reader for each buffer, so no lock is used
* writer shoule read rd_index to check if free size is enought, and  then fill this buffer  update wr_index
* reader shoule read wr_index to check if avaliable size is enought, and then read the buffer and update rd_index
* empty: wr_index==rd_index
* full: (wr_index +1) % BUFFER_SIZE == rd_index
* total avaliable size is BUFFER_SIZE -1
*/

#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>


#define MIN(x, y) ((x) < (y) ? (x) : (y))


int write_rb_sih(const char *data, int32_t size);
int read_rb_sih(char *data, int32_t size) ;
int get_rb_free_size_sih(void);
int get_rb_max_size_sih(void);
void rb_force_exit_sih(void);
void rb_end_sih(void);
int rb_shoule_exit_sih(void);
int create_rb_sih(void) ;
void rb_init_sih(void);
int release_rb_sih(void);
int get_rb_avalible_size_sih(void);
#endif
