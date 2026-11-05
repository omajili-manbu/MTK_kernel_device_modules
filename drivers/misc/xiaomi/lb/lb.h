/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _XIAOMI_LB_H
#define _XIAOMI_LB_H

#include <linux/types.h>
#include <linux/seq_file.h>
#include <linux/printk.h>

/* Stock .text mi_lb_ioctl+0x0..0x884: all command numbers encode 264. */
struct lb_command {
	u32 uid;
	s32 pid;
	char name[256];
};
struct lb_size_command {
	struct lb_command app;
	u64 size;
};
struct lb_info_command {
	struct lb_command app;
	u32 file_count;
	u32 reserved;
	u64 size;
	u8 files[];
};
struct lb_wire_file {
	s64 mtime_sec;
	s64 mtime_nsec;
	u64 ino;
	u64 pages;
	char path[256];
	u32 bitmap_words;
	u32 reserved;
	u64 bitmap[];
};
struct lb_app_names {
	u32 count;
	struct lb_command apps[100];
};

extern int g_lb_msg_level;
extern bool g_atrace_enable, g_debug_verbose;

/* All stock diagnostics carry KERN_NOTICE, including the E/D/I labels. */
#define LB_MSG(level, kind, function, line, fmt, ...) do { \
	if (g_lb_msg_level >= (level)) \
		printk(KERN_NOTICE "[LB " kind "]:%s %d: " fmt, \
		       function, line, ##__VA_ARGS__); \
} while (0)
#define LB_E(function, line, fmt, ...) LB_MSG(0, "E", function, line, fmt, ##__VA_ARGS__)
#define LB_D(function, line, fmt, ...) LB_MSG(3, "D", function, line, fmt, ##__VA_ARGS__)
#define LB_I(function, line, fmt, ...) LB_MSG(2, "I", function, line, fmt, ##__VA_ARGS__)

#endif
