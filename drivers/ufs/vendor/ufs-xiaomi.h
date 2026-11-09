/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 小米 UFS ABI 面（6.6 blob 兼容）——共享声明头
 *
 * ufs-xiaomi.c（drivers/ufs/vendor/，2026-10-04 还原的提供面）与 mi_memory.ko 的
 * 内建重建（drivers/memory/xiaomi/）共用。mi_memory 的消费面（6.6 blob 机器码
 * 实测）：+0xc..+0x64（u32×23 计数组，show_hba 逐项打印）与 +0x68（u64，
 * err_state_show）。这些字段在 6.6 stock 由闭源侧写入（ufshcd_update_err_state/
 * ufshcd_err_handler 写 err_state 与 err_reason、ufshcd_update_uic_error_cnt 写
 * 计数、ufshcd_probe_hba/wb_* 写 +0x8 标志、ufsfbo_* 与 ufseua_* 写各自状态），
 * 本树未复刻这些镜像，相应读面呈现恒零，仅展示差异。
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
void ufshcd_xiaomi_config_sdev(struct scsi_device *sdev);

/*
 * 6.6 ko 里 ufs_xiaomi 是 1168 字节 .data 对象：0x5c..0x1af 为 mi_memory 的
 * 读面（stock 有闭源写手，本树恒零），0x470 起是静态 completion（fill 到
 * 0x490 收尾，与 6.6 .data 自指重定位对完全一致）。
 */
struct ufs_xiaomi_ctx {
	u8 __reserved_1[0x5c];
	u32 dl_pa_init_err_cnt;		/* +0x5c */
	u32 dl_pa_error_ind_received;	/* +0x60 */
	u32 dme_err_cnt;		/* +0x64 */
	u64 err_state;			/* +0x68 */
	u8 err_reason[10][32];		/* +0x70..+0x1af */
	u8 __reserved_2[0x1b8 - 0x1b0];	/* +0x1b0：stock ufsfbo_init 指针位 */
	struct scsi_device *lun2_sdev;	/* +0x1b8：stock slave_configure(lun==2) 存 sdev */
	u8 __reserved_3[0x3b0 - 0x1c0];
	struct scsi_device *lun2_sdev_shadow;	/* +0x3b0：stock 同位第二 sdev 槽 */
	u8 __reserved_4[0x470 - 0x3b8];
	struct completion ready;
};

#endif /* _UFS_XIAOMI_H */
