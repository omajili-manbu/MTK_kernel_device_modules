// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h> /* rodin b515 #115 */
#include <mbraink_modules_ops_def.h>
#include "mbraink_v6899_memory.h"

#include <swpm_module_psp.h>
#include <dvfsrc-mb.h>

struct device *mbraink_v6899_device;

/* rodin b515 #115: ufsnotify 注册重试。=y probe(1.12s) 早于 UFS probe（6.6 靠 .ko
 * 装载序 3.57s >> 1.44s 保证 drvdata 就绪）；ufs_mb_register 返回 -EPROBE_DEFER 时
 * 200ms×50=10s 有界重试，就绪后完整注册。 */
#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
static void mbraink_v6899_ufs_reg_retry(struct work_struct *work);
static DECLARE_DELAYED_WORK(mbraink_v6899_ufs_reg_work, mbraink_v6899_ufs_reg_retry);
static int mbraink_v6899_ufs_reg_tries;
#endif

static int mbraink_v6899_memory_getDdrInfo(struct mbraink_memory_ddrInfo *pMemoryDdrInfo)
{
	int ret = 0;
	int i, j;
	int32_t ddr_freq_num = 0, ddr_bc_ip_num = 0;
	int32_t ddr_freq_check = 0, ddr_bc_ip_check = 0;

	struct ddr_act_times *ddr_act_times_ptr = NULL;
	struct ddr_sr_pd_times *ddr_sr_pd_times_ptr = NULL;
	struct ddr_ip_bc_stats *ddr_ip_stats_ptr = NULL;

	if (pMemoryDdrInfo == NULL) {
		pr_notice("pMemoryDdrInfo is NULL");
		ret = -1;
		goto End;
	}

	ddr_freq_num = get_ddr_freq_num();
	ddr_bc_ip_num = get_ddr_data_ip_num();

	ddr_act_times_ptr = kmalloc_array(ddr_freq_num, sizeof(struct ddr_act_times), GFP_KERNEL);
	if (!ddr_act_times_ptr) {
		ret = -1;
		goto End;
	}

	ddr_sr_pd_times_ptr = kmalloc(sizeof(struct ddr_sr_pd_times), GFP_KERNEL);
	if (!ddr_sr_pd_times_ptr) {
		ret = -1;
		goto End;
	}

	ddr_ip_stats_ptr = kmalloc_array(ddr_bc_ip_num,
								sizeof(struct ddr_ip_bc_stats),
								GFP_KERNEL);
	if (!ddr_ip_stats_ptr) {
		ret = -1;
		goto End;
	}

	for (i = 0; i < ddr_bc_ip_num; i++)
		ddr_ip_stats_ptr[i].bc_stats = kmalloc_array(ddr_freq_num,
								sizeof(struct ddr_bc_stats),
								GFP_KERNEL);

	sync_latest_data();

	get_ddr_act_times(ddr_freq_num, ddr_act_times_ptr);
	get_ddr_sr_pd_times(ddr_sr_pd_times_ptr);
	get_ddr_freq_data_ip_stats(ddr_bc_ip_num, ddr_freq_num, ddr_ip_stats_ptr);

	if (ddr_freq_num > MAX_DDR_FREQ_NUM) {
		pr_notice("ddr_freq_num over (%d)", MAX_DDR_FREQ_NUM);
		ddr_freq_check = MAX_DDR_FREQ_NUM;
	} else
		ddr_freq_check = ddr_freq_num;

	if (ddr_bc_ip_num > MAX_DDR_IP_NUM) {
		pr_notice("ddr_bc_ip_num over (%d)", MAX_DDR_IP_NUM);
		ddr_bc_ip_check = MAX_DDR_IP_NUM;
	} else
		ddr_bc_ip_check = ddr_bc_ip_num;

	pMemoryDdrInfo->srTimeInMs = ddr_sr_pd_times_ptr->sr_time;
	pMemoryDdrInfo->pdTimeInMs = ddr_sr_pd_times_ptr->pd_time;
	pMemoryDdrInfo->totalDdrFreqNum = ddr_freq_check;
	pMemoryDdrInfo->totalDdrIpNum = ddr_bc_ip_check;

	for (i = 0; i < ddr_freq_check; i++) {
		pMemoryDdrInfo->ddrActiveInfo[i].freqInMhz =
			ddr_act_times_ptr[i].freq;
		pMemoryDdrInfo->ddrActiveInfo[i].totalActiveTimeInMs =
			ddr_act_times_ptr[i].active_time;
		for (j = 0; j < ddr_bc_ip_check; j++) {
			pMemoryDdrInfo->ddrActiveInfo[i].totalIPActiveTimeInMs[j] =
				ddr_ip_stats_ptr[j].bc_stats[i].value;
		}
	}

End:
	if (ddr_act_times_ptr != NULL)
		kfree(ddr_act_times_ptr);

	if (ddr_sr_pd_times_ptr != NULL)
		kfree(ddr_sr_pd_times_ptr);

	if (ddr_ip_stats_ptr != NULL) {
		for (i = 0; i < ddr_bc_ip_num; i++) {
			if (ddr_ip_stats_ptr[i].bc_stats != NULL)
				kfree(ddr_ip_stats_ptr[i].bc_stats);
		}
		kfree(ddr_ip_stats_ptr);
	}

	return ret;
}

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)

int ufs2mbrain_event_notify(struct ufs_mbrain_event *event)
{
	char netlink_buf[NETLINK_EVENT_MESSAGE_SIZE] = {'\0'};
	int n __maybe_unused = 0;

	if (!event) {
		pr_info("[%s] event is null\n", __func__);
		return -1;
	}

	n = snprintf(netlink_buf, NETLINK_EVENT_MESSAGE_SIZE,
		"%s:%d:%d:%llu:%d:%d:%d",
		NETLINK_EVENT_UFS_NOTIFY,
		(unsigned int)event->ver,
		(unsigned int)event->data->event,
		(unsigned long long)event->data->mb_ts,
		(unsigned int)event->data->val,
		(unsigned int)event->data->gear_rx,
		(unsigned int)event->data->gear_tx
	);

	if (n < 0 || n > NETLINK_EVENT_MESSAGE_SIZE)
		pr_info("%s : snprintf error n = %d\n", __func__, n);
	else
		mbraink_netlink_send_msg(netlink_buf);

	return 0;
}

#endif

static int mbraink_v6899_memory_getMdvInfo(struct mbraink_memory_mdvInfo  *pMemoryMdv)
{
	int ret = 0;
	int i = 0;
	struct mtk_dvfsrc_header srcHeader;

	if (pMemoryMdv == NULL) {
		ret = -1;
		goto End;
	}

	if (MAX_MDV_SZ != MAX_DATA_SIZE) {
		pr_notice("mdv data sz mis-match");
		ret = -1;
		goto End;
	}

	memset(&srcHeader, 0, sizeof(struct mtk_dvfsrc_header));
	dvfsrc_get_data(&srcHeader);
	pMemoryMdv->mid = srcHeader.module_id;
	pMemoryMdv->ver = srcHeader.version;
	pMemoryMdv->pos = srcHeader.data_offset;
	pMemoryMdv->size = srcHeader.data_length;
	for (i = 0; i < MAX_MDV_SZ; i++)
		pMemoryMdv->raw[i] = srcHeader.data[i];

End:
	return ret;
}

static int mbraink_v6899_get_ufs_info(struct mbraink_ufs_info  *ufs_info)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
	struct ufs_mbrain_dev_info mb_dev_info;
	struct device_node *phy_node = NULL;
	struct platform_device *phy_pdev = NULL;
#endif

	if (ufs_info == NULL) {
		ret = -1;
		goto End;
	}

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)

	if (mbraink_v6899_device) {
		memset(&mb_dev_info, 0, sizeof(struct ufs_mbrain_dev_info));
		phy_node = of_parse_phandle(mbraink_v6899_device->of_node, "ufsnotify", 0);
		if (phy_node) {
			phy_pdev = of_find_device_by_node(phy_node);
			if (phy_pdev) {
				ret = ufs_mb_get_info(phy_pdev, &mb_dev_info);
				if (ret == 0) {
					if (mb_dev_info.model)
						memcpy(ufs_info->model,
						    mb_dev_info.model, SCSI_MODEL_LEN);

					if (mb_dev_info.rev)
						memcpy(ufs_info->rev,
						    mb_dev_info.rev, SCSI_REV_LEN);
				}
			}
		}
	}
#endif

End:
	return ret;
}

static struct mbraink_memory_ops mbraink_v6899_memory_ops = {
	.getDdrInfo = mbraink_v6899_memory_getDdrInfo,
	.getMdvInfo = mbraink_v6899_memory_getMdvInfo,
	.get_ufs_info = mbraink_v6899_get_ufs_info,
};

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
static void mbraink_v6899_ufs_reg_retry(struct work_struct *work)
{
	struct device_node *phy_node = NULL;
	struct platform_device *phy_pdev = NULL;
	int ret = -ENODEV;

	if (mbraink_v6899_device) {
		phy_node = of_parse_phandle(mbraink_v6899_device->of_node, "ufsnotify", 0);
		if (phy_node) {
			phy_pdev = of_find_device_by_node(phy_node);
			if (phy_pdev) {
				ret = ufs_mb_register(phy_pdev, ufs2mbrain_event_notify);
				platform_device_put(phy_pdev);
			}
			of_node_put(phy_node);
		}
	}

	if (ret == 0) {
		pr_info("[MBK_v6899] %s: ufsnotify registered\n", __func__);
		return;
	}
	if (mbraink_v6899_ufs_reg_tries++ < 50) {
		schedule_delayed_work(&mbraink_v6899_ufs_reg_work, msecs_to_jiffies(200));
		return;
	}
	pr_notice("[MBK_v6899] %s: ufsnotify register gave up after 10s (ret=%d)\n",
		  __func__, ret);
}
#endif

int mbraink_v6899_memory_init(struct device *dev)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
	struct device_node *phy_node = NULL;
	struct platform_device *phy_pdev = NULL;
#endif

	ret = register_mbraink_memory_ops(&mbraink_v6899_memory_ops);

	if (dev) {
		mbraink_v6899_device = dev;

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
		phy_node = of_parse_phandle(dev->of_node, "ufsnotify", 0);
		if (phy_node) {
			phy_pdev = of_find_device_by_node(phy_node);
			if (phy_pdev) {
				int ufs_ret = ufs_mb_register(phy_pdev, ufs2mbrain_event_notify);

				/* rodin b515 #115: UFS 未 probe ⇒ defer，交给重试 work；
				 * 其余错误码保持 6.6 语义（丢弃、不影响 probe 返回值）。 */
				if (ufs_ret == -EPROBE_DEFER) {
					pr_info("[MBK_v6899] %s: ufsnotify register deferred, will retry\n",
						__func__);
					mbraink_v6899_ufs_reg_tries = 0;
					schedule_delayed_work(&mbraink_v6899_ufs_reg_work,
							      msecs_to_jiffies(200));
				}
				platform_device_put(phy_pdev);
			}
		}
#endif
	} else
		pr_notice("memory init dev is NULL");

	return ret;
}

int mbraink_v6899_memory_deinit(struct device *dev)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
	struct device_node *phy_node = NULL;
	struct platform_device *phy_pdev = NULL;
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
	cancel_delayed_work_sync(&mbraink_v6899_ufs_reg_work); /* rodin b515 #115: 防去注册与重试竞争 */
#endif
	ret = unregister_mbraink_memory_ops();

#if IS_ENABLED(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)
	if (dev) {
		phy_node = of_parse_phandle(dev->of_node, "ufsnotify", 0);
		if (phy_node) {
			phy_pdev = of_find_device_by_node(phy_node);
			if (phy_pdev)
				ufs_mb_unregister(phy_pdev);
		}
	}
#endif
	mbraink_v6899_device = NULL;

	return ret;
}

