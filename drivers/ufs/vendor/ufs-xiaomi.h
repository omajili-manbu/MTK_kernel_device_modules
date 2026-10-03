/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 小米 UFS ABI 面（6.6 blob 兼容）——共享声明头
 *
 * ufs-xiaomi.c（drivers/ufs/vendor/，#150 还原的提供面）与 mi_memory.ko 的
 * 内建重建（drivers/memory/xiaomi/，#166）共用。mi_memory 的消费面（6.6
 * blob 机器码实测）：
 *   - +0x5c/+0x60/+0x64（u32）：show_hba 尾三行计数器（恒零）
 *   - +0x68（u64）：err_state（恒零）
 *   - +0x70..+0x1a0（10×32B）：err_reason 字符串组（恒零）
 *   - +0x470：struct completion ready（6.6 无人 complete，工作项永久停靠）
 */
#ifndef _UFS_XIAOMI_H
#define _UFS_XIAOMI_H

#include <linux/completion.h>
#include <linux/types.h>
#include <linux/stddef.h>

int ufshcd_query_descriptor_retry_xm(struct ufs_hba *hba, enum query_opcode opcode,
				     enum desc_idn idn, u8 index, u8 selector,
				     u8 *desc_buf, int *buf_len);
int ufshcd_read_desc_param_sel(struct ufs_hba *hba, enum desc_idn desc_id,
			       u8 desc_index, u8 selector, u8 param_offset,
			       u8 *param_read_buf, u8 param_size);
struct ufs_xiaomi_ctx *get_ufs_xiaomi(void);

/*
 * 6.6 ko 里 ufs_xiaomi 是 1168 字节 .data 对象：0x5c..0x1a0 为 mi_memory 的
 * 读面（6.6 恒零值），0x470 起是静态 completion（fill 到 0x490 收尾，与
 * 6.6 .data 自指重定位对完全一致）。
 */
struct ufs_xiaomi_ctx {
	u8 __reserved_1[0x5c];
	u32 dl_pa_init_err_cnt;		/* +0x5c */
	u32 dl_pa_error_ind_received;	/* +0x60 */
	u32 dme_err_cnt;		/* +0x64 */
	u64 err_state;			/* +0x68 */
	u8 err_reason[10][32];		/* +0x70..+0x1af */
	u8 __reserved_2[0x470 - 0x1b0];
	struct completion ready;
};

#endif /* _UFS_XIAOMI_H */
