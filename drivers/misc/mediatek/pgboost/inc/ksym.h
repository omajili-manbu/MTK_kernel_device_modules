/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

#ifndef _TOOLS_KSYM_H_
#define _TOOLS_KSYM_H_

#include <linux/types.h> // for phys_addr_t
#include <linux/random.h>

#include <linux/err.h>

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/memory.h>   /* rodin 4-3: kaslr_offset() 定义处 */
#include <asm/sections.h> /* rodin 4-3: _text 声明处 */

/* rodin 4-3: 6.18 删 arm64 kimage_vaddr 变量（KASLR 重构）。
 * 6.18 的 kaslr_offset() 定义即 (u64)&_text - KIMAGE_VADDR，
 * 故 _text 的运行时 VA 与旧 kimage_vaddr 完全等价（重定位后取值）。
 * pgboost 用它在 kallsyms 里比对 kallsyms_relative_base（存 _text）。 */
#ifndef kimage_vaddr
#define kimage_vaddr ((unsigned long)&_text)
#endif

#define KV		(kimage_vaddr+64*1024)
#define S_MAX		SZ_128M
#define SM_SIZE		28
#define TT_SIZE		256
#define NAME_LEN	128

int __init tools_ka_init(void);
unsigned long __init tools_addr_find(const char *name);
#endif /* _TOOLS_KSYM_H */
