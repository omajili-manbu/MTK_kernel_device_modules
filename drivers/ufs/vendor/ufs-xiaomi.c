// SPDX-License-Identifier: GPL-2.0
/*
 * 小米 UFS ABI 面（6.6 blob 兼容）
 *
 * 6.6 的 ufs-mediatek-mod.ko 内嵌闭源 drivers/ufs/xiaomi/ 子树并导出三个
 * 符号，唯一的树外消费者是永久 blob mi_memory.ko（vendorboot 装载）。6.18
 * 内建化只编开源 MTK 胶水，闭源子树连同导出一起消失，mi_memory 解析失败
 * （err -2），init 加载失败 exit(127) 触发 panic。本文件按 6.6 ko 反汇编
 * 逐指令还原该 ABI 面，不引入任何桩：
 *
 *  - get_ufs_xiaomi()：返回 &ufs_xiaomi（1168 字节 .data 单例）。6.6 机器码
 *    普查：单例是闭源子树的全局状态银行——ufshcd_init 存 hba（+0x0）、
 *    ufshcd_update_err_state/ufshcd_err_handler 写 err_state（+0x68）与
 *    err_reason（+0x70..+0x1af）、ufshcd_update_uic_error_cnt 写计数
 *    （+0xc/+0x24/+0x64）、ufsfbo_* 与 ufseua_* 写各自状态；本树未复刻这些
 *    镜像，mi_memory 读面相应呈现恒零。+0x470 是静态初始化的 struct
 *    completion（done=0、等待队列自指）。
 *
 *  - ufshcd_query_descriptor_retry_xm()：与 stock ufshcd_query_descriptor_retry
 *    逐字同构（3 次尝试，0 / -EINVAL 即止），直接转发。
 *
 *  - ufshcd_read_desc_param_sel()：6.6 stock ufshcd_read_desc_param 带回
 *    selector 参数（上游 6.1 起删参后小米闭源侧保留的变体），语义按 6.6 ko
 *    反汇编还原：desc_id >= QUERY_DESC_IDN_MAX 或 param_size == 0 拒绝；
 *    offset==0 且 size==255 整缓冲快路径直查调用者缓冲；否则 kzalloc(255)
 *    全量查询后按 min(param_size, 描述符长度 - offset) 拷贝。与 6.18 stock
 *    的唯一差异是 selector 透传（stock 硬编码 0）。
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <ufs/ufs.h>
#include <ufs/ufshcd.h>

/*
 * ufshcd_query_descriptor_retry 自 6.18 起只导出无声明（core 内部使用），
 * 6.6 头文件原型按原样补回。
 */
int ufshcd_query_descriptor_retry(struct ufs_hba *hba, enum query_opcode opcode,
				  enum desc_idn idn, u8 index, u8 selector,
				  u8 *desc_buf, int *buf_len);

#include "ufs-xiaomi.h"

static struct ufs_xiaomi_ctx ufs_xiaomi = {
	.ready = COMPLETION_INITIALIZER(ufs_xiaomi.ready),
};

static_assert(sizeof(struct ufs_xiaomi_ctx) == 0x490);
static_assert(offsetof(struct ufs_xiaomi_ctx, ready) == 0x470);

struct ufs_xiaomi_ctx *get_ufs_xiaomi(void)
{
	return &ufs_xiaomi;
}
EXPORT_SYMBOL(get_ufs_xiaomi);

int ufshcd_query_descriptor_retry_xm(struct ufs_hba *hba, enum query_opcode opcode,
				     enum desc_idn idn, u8 index, u8 selector,
				     u8 *desc_buf, int *buf_len)
{
	return ufshcd_query_descriptor_retry(hba, opcode, idn, index, selector,
					     desc_buf, buf_len);
}
EXPORT_SYMBOL_GPL(ufshcd_query_descriptor_retry_xm);

int ufshcd_read_desc_param_sel(struct ufs_hba *hba, enum desc_idn desc_id,
			       u8 desc_index, u8 selector, u8 param_offset,
			       u8 *param_read_buf, u8 param_size)
{
	u8 *desc_buf;
	int ret;
	int buff_len = QUERY_DESC_MAX_SIZE;
	bool is_kmalloc;

	/* safety check */
	if (desc_id >= QUERY_DESC_IDN_MAX || !param_size)
		return -EINVAL;

	/* check whether we need temp memory */
	is_kmalloc = param_offset != 0 || param_size < QUERY_DESC_MAX_SIZE;
	if (is_kmalloc) {
		desc_buf = kzalloc(QUERY_DESC_MAX_SIZE, GFP_KERNEL | __GFP_ZERO);
		if (!desc_buf)
			return -ENOMEM;
	} else {
		desc_buf = param_read_buf;
	}

	ret = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					    desc_id, desc_index, selector,
					    desc_buf, &buff_len);
	if (ret) {
		dev_err(hba->dev, "%s: Failed reading descriptor. desc_id %d, desc_index %d, param_offset %d, ret %d\n",
			__func__, desc_id, desc_index, param_offset, ret);
		goto out;
	}

	/* update descriptor length */
	buff_len = desc_buf[QUERY_DESC_LENGTH_OFFSET];

	if (param_offset >= buff_len) {
		dev_err(hba->dev, "%s: Invalid offset 0x%x in descriptor IDN 0x%x, length 0x%x\n",
			__func__, param_offset, desc_id, buff_len);
		ret = -EINVAL;
		goto out;
	}

	/* sanity check */
	if (desc_buf[QUERY_DESC_DESC_TYPE_OFFSET] != desc_id) {
		dev_err(hba->dev, "%s: invalid desc_id %d in descriptor header\n",
			__func__, desc_buf[QUERY_DESC_DESC_TYPE_OFFSET]);
		ret = -EINVAL;
		goto out;
	}

	if (is_kmalloc) {
		/* make sure we don't copy more data than available */
		memcpy(param_read_buf, &desc_buf[param_offset],
		       min_t(u32, param_size, buff_len - param_offset));
	}
out:
	if (is_kmalloc)
		kfree(desc_buf);
	return ret;
}
EXPORT_SYMBOL_GPL(ufshcd_read_desc_param_sel);

/*
 * 6.6 stock ufshcd_slave_configure 末尾的三段同守卫块（机器码还原为单守卫）：
 * 仅 lun 2 的 scsi_device 触发——scan 按 lun 升序，配到 lun 2 时 lun 0 已在
 * host 链上，mi_memory 随后的 scsi_device_lookup(SCSI_LUN=0) 必中。complete
 * 一次性唤醒停靠的 ufs_info_init_work，completion 粘滞，此后零常驻开销。
 */
void ufshcd_xiaomi_config_sdev(struct scsi_device *sdev)
{
	if (sdev->lun == 2) {
		ufs_xiaomi.lun2_sdev = sdev;
		ufs_xiaomi.lun2_sdev_shadow = sdev;
		complete(&ufs_xiaomi.ready);
	}
}
