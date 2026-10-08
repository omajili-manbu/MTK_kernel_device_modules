/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <uapi/linux/sched/types.h>
#include <linux/cpumask.h>
#include <linux/backlight.h>
#include <linux/debugfs.h>
#include <linux/pinctrl/devinfo.h>
#include <linux/pinctrl/consumer.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 38)
#include <linux/input/mt.h>
#define INPUT_TYPE_B_PROTOCOL
#endif

#include "goodix_ts_core.h"
#include <linux/vseq.h>
/* b567：warsaw 骨架用旧枚举名（Touch_Mode_NUM=42/Touch_Aod_Enable=11/...），
 * rodin blob = popsicle DATA_MODE 体系（数值两代一致：fod=10/aod=11/doubletap=14）。
 * 模式数组维度随 blob 45 通道（driver_update_touch_mode_common 按 mask 位拷贝）。 */
#define Touch_Mode_NUM		DATA_MODE_45
#define Touch_Fod_Enable	DATA_MODE_10
#define Touch_Aod_Enable	DATA_MODE_11
#define Touch_Doubletap_Mode	DATA_MODE_14

#define GOODIX_DEFAULT_CFG_NAME 	"goodix_cfg_group.cfg"
#define GOOIDX_INPUT_PHYS			"goodix_ts/input0"
#define PINCTRL_STATE_ACTIVE		"pmx_ts_active"
#define PINCTRL_STATE_SUSPEND		"pmx_ts_suspend"
#define PINCTRL_STATE_BOOT			"pmx_ts_boot"

/* #228（b581）blob .rodata.str1.1+0x315 = "rodin_gtp_thp_config.ini"
 * （引用点：goodix_start_later_init L19681 处 memcpy 进 hardware_param；
 * 原树 warsaw 名为偏离） */
#define HTC_PROJECT_CFG_NAME		"rodin_gtp_thp_config.ini"

#ifdef CONFIG_TOUCH_BOOST
#define EVENT_INPUT 0x1
extern void lpm_disable_for_dev(bool on, char event_dev);
#endif

#ifdef TOUCH_TRUSTED_SUPPORT
const char *qts_touch_type = NULL;
struct qts_vendor_data qts_vendor_data;
#endif // TOUCH_TRUSTED_SUPPORT

extern struct device *global_spi_parent_device;
extern struct device_node *gf_spi_dp;
struct goodix_module goodix_modules;
int core_module_prob_sate = CORE_MODULE_UNPROBED;
struct goodix_ts_core *goodix_core_data;
static int goodix_send_ic_config(struct goodix_ts_core *cd, int type);
static int goodix_resume_suspend(bool is_resume, u8 gesture_type);
static int goodix_update_cfg(struct goodix_ts_core *cd, bool enable);
int goodix_ts_get_lockdown_info(struct goodix_ts_core *cd);
int goodix_ts_power_off(struct goodix_ts_core *cd);
int goodix_ts_power_on(struct goodix_ts_core *cd);
static int goodix_set_thermal_temp(int temp, bool force);

/**
 * __do_register_ext_module - register external module
 * to register into touch core modules structure
 * return 0 on success, otherwise return < 0
 */
static int __do_register_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct list_head *insert_point = &goodix_modules.head;

	/* prority level *must* be set */
	if (module->priority == EXTMOD_PRIO_RESERVED) {
		ts_err("Priority of module [%s] needs to be set",
			module->name);
		return -EINVAL;
	}
	mutex_lock(&goodix_modules.mutex);
	/* find insert point for the specified priority */
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module == module) {
				ts_info("Module [%s] already exists",
					module->name);
				mutex_unlock(&goodix_modules.mutex);
				return 0;
			}
		}

		/* smaller priority value with higher priority level */
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module->priority >= module->priority) {
				insert_point = &ext_module->list;
				break;
			}
		}
	}

	if (module->funcs && module->funcs->init) {
		if (module->funcs->init(goodix_modules.core_data,
					module) < 0) {
			ts_err("Module [%s] init error",
				module->name ? module->name : " ");
			mutex_unlock(&goodix_modules.mutex);
			return -EFAULT;
		}
	}

	list_add(&module->list, insert_point->prev);
	mutex_unlock(&goodix_modules.mutex);

	ts_info("Module [%s] registered,priority:%u", module->name,
		module->priority);
	return 0;
}

static void goodix_register_ext_module_work(struct work_struct *work) {
	struct goodix_ext_module *module =
			container_of(work, struct goodix_ext_module, work);

	ts_info("module register work IN");

	/* driver probe failed */
	if (core_module_prob_sate != CORE_MODULE_PROB_SUCCESS) {
		ts_err("Can't register ext_module core error");
		return;
	}

	if (__do_register_ext_module(module))
		ts_err("failed register module: %s", module->name);
	else
		ts_info("success register module: %s", module->name);
}

static void goodix_core_module_init(void)
{
	if (goodix_modules.initilized)
		return;
	goodix_modules.initilized = true;
	INIT_LIST_HEAD(&goodix_modules.head);
	mutex_init(&goodix_modules.mutex);
}

/**
 * goodix_register_ext_module - interface for register external module
 * to the core. This will create a workqueue to finish the real register
 * work and return immediately. The user need to check the final result
 * to make sure registe is success or fail.
 *
 * @module: pointer to external module to be register
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module(struct goodix_ext_module *module)
{
	if (!module)
		return -EINVAL;

	ts_info("goodix_register_ext_module IN");

	goodix_core_module_init();
	INIT_WORK(&module->work, goodix_register_ext_module_work);
	schedule_work(&module->work);

	ts_info("goodix_register_ext_module OUT");
	return 0;
}

/**
 * goodix_register_ext_module_no_wait
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module_no_wait(struct goodix_ext_module *module)
{
	if (!module)
		return -EINVAL;
	ts_info("goodix_register_ext_module_no_wait IN");
	goodix_core_module_init();
	/* driver probe failed */
	if (core_module_prob_sate != CORE_MODULE_PROB_SUCCESS) {
		ts_err("Can't register ext_module core error");
		return -EINVAL;
	}
	return __do_register_ext_module(module);
}

/**
 * goodix_unregister_ext_module - interface for external module
 * to unregister external modules
 *
 * @module: pointer to external module
 * return: 0 ok, <0 failed
 */
int goodix_unregister_ext_module(struct goodix_ext_module *module)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	bool found = false;

	if (!module)
		return -EINVAL;

	if (!goodix_modules.initilized)
		return -EINVAL;

	if (!goodix_modules.core_data)
		return -ENODEV;

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (ext_module == module) {
				found = true;
				break;
			}
		}
	} else {
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	if (!found) {
		ts_debug("Module [%s] never registed",
				module->name);
		mutex_unlock(&goodix_modules.mutex);
		return 0;
	}

	list_del(&module->list);
	mutex_unlock(&goodix_modules.mutex);

	if (module->funcs && module->funcs->exit)
		module->funcs->exit(goodix_modules.core_data, module);

	ts_info("Moudle [%s] unregistered",
		module->name ? module->name : " ");
	return 0;
}

static void goodix_ext_sysfs_release(struct kobject *kobj)
{
	ts_info("Kobject released!");
}

#define to_ext_module(kobj)	container_of(kobj,\
				struct goodix_ext_module, kobj)
#define to_ext_attr(attr)	container_of(attr,\
				struct goodix_ext_attribute, attr)

static ssize_t goodix_ext_sysfs_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct goodix_ext_module *module = to_ext_module(kobj);
	struct goodix_ext_attribute *ext_attr = to_ext_attr(attr);

	if (ext_attr->show)
		return ext_attr->show(module, buf);

	return -EIO;
}

static ssize_t goodix_ext_sysfs_store(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t count)
{
	struct goodix_ext_module *module = to_ext_module(kobj);
	struct goodix_ext_attribute *ext_attr = to_ext_attr(attr);

	if (ext_attr->store)
		return ext_attr->store(module, buf, count);

	return -EIO;
}

static const struct sysfs_ops goodix_ext_ops = {
	.show = goodix_ext_sysfs_show,
	.store = goodix_ext_sysfs_store
};

static struct kobj_type goodix_ext_ktype = {
	.release = goodix_ext_sysfs_release,
	.sysfs_ops = &goodix_ext_ops,
};

struct kobj_type *goodix_get_default_ktype(void)
{
	return &goodix_ext_ktype;
}

struct kobject *goodix_get_default_kobj(void)
{
	struct kobject *kobj = NULL;

	if (goodix_modules.core_data &&
			goodix_modules.core_data->pdev)
		kobj = &goodix_modules.core_data->pdev->dev.kobj;
	return kobj;
}

/* _b582-GXI：blob 全 ko 无 test_stage/do_inspect/diff_shift_start/diff_shift_end 四
 * 个 sysfs 节点名串，也无 g_test_stage 全局 ⇒ warsaw diff-shift 测试面入口按 blob
 * 删除（框架侧零引用；inspect 侧对应函数同轮删除）。 */

/* show driver infomation */
static ssize_t goodix_ts_driver_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "DriverVersion:%s\n",
			GOODIX_DRIVER_VERSION);
}

/* show chip infoamtion */
static ssize_t goodix_ts_chip_info_show(struct device  *dev,
		struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_fw_version chip_ver;
	u8 temp_pid[8] = {0};
	int ret;
	int cnt = -EINVAL;

	if (hw_ops->read_version) {
		ret = hw_ops->read_version(core_data, &chip_ver);
		if (!ret) {
			memcpy(temp_pid, chip_ver.rom_pid, sizeof(chip_ver.rom_pid));
			cnt = snprintf(&buf[0], PAGE_SIZE,
				"rom_pid:%s\nrom_vid:%02x%02x%02x\n",
				temp_pid, chip_ver.rom_vid[0],
				chip_ver.rom_vid[1], chip_ver.rom_vid[2]);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"patch_pid:%s\npatch_vid:%02x%02x%02x%02x\n",
				chip_ver.patch_pid, chip_ver.patch_vid[0],
				chip_ver.patch_vid[1], chip_ver.patch_vid[2],
				chip_ver.patch_vid[3]);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"sensorid:%d\n", chip_ver.sensor_id);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(core_data, &core_data->ic_info);
		if (!ret) {
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"config_id:%x\n", core_data->ic_info.version.config_id);
			cnt += snprintf(&buf[cnt], PAGE_SIZE,
				"config_version:%x\n", core_data->ic_info.version.config_version);
		}
	}

	return cnt;
}

/* reset chip */
static ssize_t goodix_ts_reset_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] != '0')
		hw_ops->reset(core_data, GOODIX_NORMAL_RESET_DELAY_MS);
	return count;
}

/* read config */
static ssize_t goodix_ts_read_cfg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;
	int i;
	int offset;
	char *cfg_buf = NULL;

	cfg_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!cfg_buf)
		return -ENOMEM;

	if (hw_ops->read_config)
		ret = hw_ops->read_config(core_data, cfg_buf, PAGE_SIZE);
	else
		ret = -EINVAL;

	if (ret > 0) {
		offset = 0;
		for (i = 0; i < 200; i++) {
			offset += snprintf(&buf[offset], PAGE_SIZE - offset,
					"%02x,", cfg_buf[i]);
			if ((i + 1) % 20 == 0)
				buf[offset++] = '\n';
		}
	}

	kfree(cfg_buf);
	if (ret <= 0)
		return ret;

	return offset;
}

static u8 ascii2hex(u8 a)
{
	s8 value = 0;

	if (a >= '0' && a <= '9')
		value = a - '0';
	else if (a >= 'A' && a <= 'F')
		value = a - 'A' + 0x0A;
	else if (a >= 'a' && a <= 'f')
		value = a - 'a' + 0x0A;
	else
		value = 0xff;

	return value;
}

static int goodix_ts_convert_0x_data(const u8 *buf, int buf_size,
				u8 *out_buf, int *out_buf_len)
{
	int i, m_size = 0;
	int temp_index = 0;
	u8 high, low;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] == 'x' || buf[i] == 'X')
			m_size++;
	}

	if (m_size <= 1) {
		ts_err("cfg file ERROR, valid data count:%d", m_size);
		return -EINVAL;
	}
	*out_buf_len = m_size;

	for (i = 0; i < buf_size; i++) {
		if (buf[i] != 'x' && buf[i] != 'X')
			continue;

		if (temp_index >= m_size) {
			ts_err("exchange cfg data error, overflow,"
					"temp_index:%d,m_size:%d",
					temp_index, m_size);
			return -EINVAL;
		}
		high = ascii2hex(buf[i + 1]);
		low = ascii2hex(buf[i + 2]);
		if (high == 0xff || low == 0xff) {
			ts_err("failed convert: 0x%x, 0x%x",
				buf[i + 1], buf[i + 2]);
			return -EINVAL;
		}
		out_buf[temp_index++] = (high << 4) + low;
	}
	return 0;
}

/* send config */
static ssize_t goodix_ts_send_cfg_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ic_config *config = NULL;
	const struct firmware *cfg_img = NULL;
	int en;
	int ret;

	if (sscanf(buf, "%d", &en) != 1)
		return -EINVAL;

	if (en != 1)
		return -EINVAL;

	hw_ops->irq_enable(core_data, false);

	ret = request_firmware(&cfg_img, GOODIX_DEFAULT_CFG_NAME, dev);
	if (ret < 0) {
		ts_err("cfg file [%s] not available,errno:%d",
			GOODIX_DEFAULT_CFG_NAME, ret);
		goto exit;
	} else {
		ts_info("cfg file [%s] is ready", GOODIX_DEFAULT_CFG_NAME);
	}

	config = kzalloc(sizeof(*config), GFP_KERNEL);
	if (!config)
		goto exit;

	if (goodix_ts_convert_0x_data(cfg_img->data, cfg_img->size,
			config->data, &config->len)) {
		ts_err("convert config data FAILED");
		goto exit;
	}

	if (hw_ops->send_config) {
		ret = hw_ops->send_config(core_data, config->data, config->len);
		if (ret < 0)
			ts_err("send config failed");
	}

exit:
	hw_ops->irq_enable(core_data, true);
	kfree(config);
	if (cfg_img)
		release_firmware(cfg_img);

	return count;
}

/* reg read/write */
static u32 rw_addr;
static u32 rw_len;
static u8 rw_flag;
static u8 store_buf[32];
static u8 show_buf[PAGE_SIZE];
static ssize_t goodix_ts_reg_rw_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int ret;

	if (!rw_addr || !rw_len) {
		ts_err("address(0x%x) and length(%d) can't be null",
			rw_addr, rw_len);
		return -EINVAL;
	}

	if (rw_flag != 1) {
		ts_err("invalid rw flag %d, only support [1/2]", rw_flag);
		return -EINVAL;
	}

	ret = hw_ops->read(core_data, rw_addr, show_buf, rw_len);
	if (ret < 0) {
		ts_err("failed read addr(%x) length(%d)", rw_addr, rw_len);
		return snprintf(buf, PAGE_SIZE, "failed read addr(%x), len(%d)\n",
			rw_addr, rw_len);
	}

	return snprintf(buf, PAGE_SIZE, "0x%x,%d {%*ph}\n",
		rw_addr, rw_len, rw_len, show_buf);
}

static ssize_t goodix_ts_reg_rw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	char *pos = NULL;
	char *token = NULL;
	long result = 0;
	int ret;
	int i;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return -EINVAL;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (!buf || !count) {
		ts_err("invalid parame");
		goto err_out;
	}

	if (buf[0] == 'r') {
		rw_flag = 1;
	} else if (buf[0] == 'w') {
		rw_flag = 2;
	} else {
		ts_err("string must start with 'r/w'");
		goto err_out;
	}

	/* get addr */
	pos = (char *)buf;
	pos += 2;
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid address info");
		goto err_out;
	} else {
		if (kstrtol(token, 16, &result)) {
			ts_err("failed get addr info");
			goto err_out;
		}
		rw_addr = (u32)result;
		ts_info("rw addr is 0x%x", rw_addr);
	}

	/* get length */
	token = strsep(&pos, ":");
	if (!token) {
		ts_err("invalid length info");
		goto err_out;
	} else {
		if (kstrtol(token, 0, &result)) {
			ts_err("failed get length info");
			goto err_out;
		}
		rw_len = (u32)result;
		ts_info("rw length info is %d", rw_len);
		if (rw_len > sizeof(store_buf)) {
			ts_err("data len > %lu", sizeof(store_buf));
			goto err_out;
		}
	}

	if (rw_flag == 1)
		return count;

	for (i = 0; i < rw_len; i++) {
		token = strsep(&pos, ":");
		if (!token) {
			ts_err("invalid data info");
			goto err_out;
		} else {
			if (kstrtol(token, 16, &result)) {
				ts_err("failed get data[%d] info", i);
				goto err_out;
			}
			store_buf[i] = (u8)result;
			ts_info("get data[%d]=0x%x", i, store_buf[i]);
		}
	}
	ret = hw_ops->write(core_data, rw_addr, store_buf, rw_len);
	if (ret < 0) {
		ts_err("failed write addr(%x) data %*ph", rw_addr,
			rw_len, store_buf);
		goto err_out;
	}

	ts_info("%s write to addr (%x) with data %*ph",
		"success", rw_addr, rw_len, store_buf);

	return count;
err_out:
	snprintf(show_buf, PAGE_SIZE, "%s\n",
		"invalid params, format{r/w:4100:length:[41:21:31]}");
	return -EINVAL;

}

/* show irq infomation */
static ssize_t goodix_ts_irq_info_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct irq_desc *desc;
	size_t offset = 0;
	int r;

	r = snprintf(&buf[offset], PAGE_SIZE, "irq:%u\n", core_data->irq);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "state:%s\n",
			atomic_read(&core_data->irq_enabled) ?
			"enabled" : "disabled");
	if (r < 0)
		return -EINVAL;

	desc = irq_to_desc(core_data->irq);
	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "disable-depth:%d\n",
			desc->depth);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset, "trigger-count:%zu\n",
		core_data->irq_trig_cnt);
	if (r < 0)
		return -EINVAL;

	offset += r;
	r = snprintf(&buf[offset], PAGE_SIZE - offset,
			"echo 0/1 > irq_info to disable/enable irq\n");
	if (r < 0)
		return -EINVAL;

	offset += r;
	return offset;
}

/* enable/disable irq */
static ssize_t goodix_ts_irq_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		hw_ops->irq_enable(core_data, true);
	else
		hw_ops->irq_enable(core_data, false);
	return count;
}

/* show esd status */
static ssize_t goodix_ts_esd_info_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "state:%s\n",
			atomic_read(&ts_esd->esd_on) ?
			"enabled" : "disabled");

	return r;
}

/* enable/disable esd */
static ssize_t goodix_ts_esd_info_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0')
		goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	else
		goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);
	return count;
}

/* debug level show */
static ssize_t goodix_ts_debug_log_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "log_level: %d", debug_log_level);
	return r;
}

/* debug level store */
static ssize_t goodix_ts_debug_log_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] == 'u' && goodix_core_data) {
		goodix_core_data->need_update_cfg = 1;
		goodix_core_data->cfg_cloud_state = false;
		ts_info("test for update cfg, mode = 0");
	} else if (buf[0] == 'v' && goodix_core_data) {
		goodix_core_data->need_update_cfg = 1;
		goodix_core_data->cfg_cloud_state = true;
		ts_info("test for update cfg, mode = 1");
	}

	debug_log_level = buf[0] - '0';
	/* _b573 scp 联动：blob 0xc2d4-0xc2e4 = !mistouch_close 时把日志等级同步给 SCP */
	if (!goodix_scp_tp_mistouch_close)
		scp_tp_ipi_send(6, debug_log_level, 0, 0);
	/* #228（b581）blob goodix_ts_debug_log_store 打印点 L849/L853/L861：
	 * L861 = ts_info("scp set log level = %d")（__func__="goodix_ts_debug_log_store"），
	 * 树侧原缺该打印（callcmp only-blob _printk:1） */
	ts_info("scp set log level = %d", debug_log_level);
	return count;
}

static void set_touch_mode(int mode, int value)
{
	int touch_mode[Touch_Mode_NUM];
	long update_mode_mask = 0;
	if (mode < 0 || mode >= Touch_Mode_NUM || value < 0)
		return;
	touch_mode[mode] = value;
	update_mode_mask |= 1 << mode;
	driver_update_touch_mode_common(TOUCH_ID, touch_mode, update_mode_mask);
}

/* double tap gesture show */
static ssize_t goodix_ts_double_tap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode_common(TOUCH_ID, Touch_Doubletap_Mode);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* double tap gesture store */
static ssize_t goodix_ts_double_tap_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;
	set_touch_mode(Touch_Aod_Enable, buf[0] != '0');
	return count;
}

/* aod gesture show */
static ssize_t goodix_ts_aod_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode_common(TOUCH_ID, Touch_Aod_Enable);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* aod gesture_store */
static ssize_t goodix_ts_aod_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;
	set_touch_mode(Touch_Aod_Enable, buf[0] != '0');
	return count;
}

#ifdef TOUCH_FOD_SUPPORT
/* fod gesture show */
static ssize_t goodix_ts_fod_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int value = driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable);
	return snprintf(buf, PAGE_SIZE, "state:%s\n",
			value ? "enabled" : "disabled");
}

/* fod gesture_store */
static ssize_t goodix_ts_fod_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	if (!buf || count <= 0)
		return -EINVAL;

	set_touch_mode(Touch_Fod_Enable, buf[0] != '0');
	return count;
}
#endif

/* report_rate show */
static ssize_t goodix_report_rate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int r = 0;

	r = snprintf(buf, PAGE_SIZE, "touch report rate::%s\n",
			goodix_core_data->report_rate == 240 ?
			"240HZ" : "480HZ");

	return r;
}

/* report_rate_store */
static ssize_t goodix_report_rate_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;

	if (buf[0] != '0') {
		goodix_core_data->report_rate = 480;
		core_data->hw_ops->switch_report_rate(core_data, true);
	} else {
		goodix_core_data->report_rate = 240;
		core_data->hw_ops->switch_report_rate(core_data, false);
	}
	return count;
}

#ifdef TOUCH_THP_SUPPORT
static ssize_t goodix_ts_scan_freq_index_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct goodix_ts_core *core_data = dev_get_drvdata(dev);

	if (!buf || count <= 0)
		return -EINVAL;
	if (buf[0] - '0' < core_data->ic_info.parm.mutual_freq_num) {
		ts_info("set scan freq index: %d", buf[0] - '0');
		goodix_htc_set_scan_freq(buf[0] - '0');
	} else
		ts_err("out of scan freq num!");
	return count;
}
#endif

/* ==================== _b571 缺件重建：scp_debug（blob 机器码）==================== */
static ssize_t goodix_ts_scp_debug_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "ENABLE_SCP_TP, state=%d\n",
			scp_tp_param.param0);
}

static ssize_t goodix_ts_scp_debug_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct goodix_ts_core *cd = dev_get_drvdata(dev);
	int ret = -EINVAL;

	if (!buf || count <= 0)
		return ret;			/* 0xc864/0xc86c → 0xcaa8 */

	if (buf[0] < '4' || buf[0] > 'a')
		return count;			/* 0xc880 b.hi → 0xcaa4 */

	switch (buf[0]) {			/* 跳表 .rodata+0x540，46 项，default=0x80 */
	case '4':
		scp_tp_ipi_send(0xf1, 0, 0, 0);
		ts_info("scp test0");				/* 760 */
		break;
	case '5':
		ts_info("scp_tp_sendparam0");			/* 763 */
		/* blob 0xc8f4：ldur w8,[x21(cd),#0xd3] → str w8,[scp_tp_param+8]
		 * cd+0xd3 = cd->ic_info.misc.touch_data_addr（见 §3 字段判定）
		 * [TODO-VERIFY-6] */
		scp_tp_param.field_08 = cd->ic_info.misc.touch_data_addr;
		scp_tp_sendparam();				/* 0xc900 */
		ts_info("scp_tp_sendparam1");			/* 766 */
		break;
	case '6':
		scp_tp_switch(0);				/* 0xc92c */
		cd->hw_ops->irq_enable(cd, 1);			/* 0xc930，树侧写法 ts_test_irq_enable 不可用 */
		ts_info("change to ap");			/* 771 */
		break;
	case '7':
		scp_tp_switch(1);				/* 0xc984 */
		cd->hw_ops->irq_enable(cd, 0);			/* 0xc988 */
		ts_info("change to scp");			/* 776 */
		break;
	case '8':
		scp_tp_ipi_send(0xf2, buf[1] - '0', 0, 0);	/* 0xc9d8 */
		ts_info("scp test1 set nonui = %d", buf[1] - '0');	/* 780 */
		break;
	case '9':
		scp_tp_ipi_send(0x6, buf[1] - '0', 0, 0);	/* 0xca1c */
		ts_info("scp set log level = %d", buf[1] - '0');	/* 784 */
		break;
	case 'a':
		scp_tp_ipi_send(0xf4, 0, 0, 0);			/* 0xca68 */
		ts_info("scp test3 clean data_statistic");	/* 788 */
		break;
	default:
		return ret;			/* 表内 0x80 → 0xcaa8：x0 仍为入口 -EINVAL */
	}

	return count;				/* 0xcaa4: mov x0, x19 */
}

static DEVICE_ATTR(scp_debug, 0664,
		goodix_ts_scp_debug_show, goodix_ts_scp_debug_store);

static DEVICE_ATTR(driver_info, 0444, goodix_ts_driver_info_show, NULL);
static DEVICE_ATTR(chip_info, 0444, goodix_ts_chip_info_show, NULL);
static DEVICE_ATTR(reset, 0220, NULL, goodix_ts_reset_store);
static DEVICE_ATTR(send_cfg, 0220, NULL, goodix_ts_send_cfg_store);
static DEVICE_ATTR(read_cfg, 0444, goodix_ts_read_cfg_show, NULL);
static DEVICE_ATTR(reg_rw, 0664, goodix_ts_reg_rw_show, goodix_ts_reg_rw_store);
static DEVICE_ATTR(irq_info, 0664, goodix_ts_irq_info_show, goodix_ts_irq_info_store);
static DEVICE_ATTR(esd_info, 0664, goodix_ts_esd_info_show, goodix_ts_esd_info_store);
static DEVICE_ATTR(debug_log, 0664, goodix_ts_debug_log_show, goodix_ts_debug_log_store);
static DEVICE_ATTR(double_tap_enable, 0664, goodix_ts_double_tap_show, goodix_ts_double_tap_store);
static DEVICE_ATTR(aod_enable, 0664, goodix_ts_aod_show, goodix_ts_aod_store);
static DEVICE_ATTR(switch_report_rate, 0664, goodix_report_rate_show, goodix_report_rate_store);
static DEVICE_ATTR(fod_enable, 0664, goodix_ts_fod_show, goodix_ts_fod_store);
#ifdef TOUCH_THP_SUPPORT
static DEVICE_ATTR(scan_freq_index, 0220, NULL, goodix_ts_scan_freq_index_store);
#endif

static struct attribute *sysfs_attrs[] = {
	&dev_attr_driver_info.attr,
	&dev_attr_chip_info.attr,
	&dev_attr_reset.attr,
	&dev_attr_send_cfg.attr,
	&dev_attr_read_cfg.attr,
	&dev_attr_reg_rw.attr,
	&dev_attr_irq_info.attr,
	&dev_attr_esd_info.attr,
	&dev_attr_debug_log.attr,
	&dev_attr_double_tap_enable.attr,
	&dev_attr_aod_enable.attr,
	&dev_attr_switch_report_rate.attr,
	&dev_attr_fod_enable.attr,
#ifdef TOUCH_THP_SUPPORT
	&dev_attr_scan_freq_index.attr,
#endif
	&dev_attr_scp_debug.attr,
	NULL,
};

static const struct attribute_group sysfs_group = {
	.attrs = sysfs_attrs,
};

static int goodix_ts_sysfs_init(struct goodix_ts_core *core_data)
{
	int ret;

	ret = sysfs_create_group(&core_data->pdev->dev.kobj, &sysfs_group);
	if (ret) {
		ts_err("failed create core sysfs group");
		return ret;
	}

	return ret;
}

static void goodix_ts_sysfs_exit(struct goodix_ts_core *core_data)
{
	sysfs_remove_group(&core_data->pdev->dev.kobj, &sysfs_group);
}

static int goodix_ic_data_collect(char *buf, int *length)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *cd = goodix_core_data;
	int tx;
	int rx;
	int ret;
	int i;
	int index;
	int buf_size = PAGE_SIZE * 3;
	int cnt = 0;

	if (!cd) {
		ts_err("rawdata_proc_show, input null ptr");
		return -EIO;
	}

	if (!buf || !length) {
		ts_err("invalid params");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc rawdata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_capacitance_data(cd, info);
	if (ret < 0) {
		ts_err("failed to get_capacitance_data, exit!");
		goto exit;
	}

	rx = info->buff[0];
	tx = info->buff[1];
	cnt += snprintf(buf + cnt, buf_size - cnt, "TX:%d  RX:%d\n", tx, rx);
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_rawdata:\n");
	index = 2;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_diffdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%3d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	cnt += snprintf(buf + cnt, buf_size - cnt, "mutual_refdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		cnt += snprintf(buf + cnt, buf_size - cnt, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			cnt += snprintf(buf + cnt, buf_size - cnt, "\n");
	}
	*length = cnt;

exit:
	kfree(info);
	return ret;
}

/* prosfs create */
static int rawdata_proc_show(struct seq_file *m, void *v)
{
	struct ts_rawdata_info *info;
	struct goodix_ts_core *cd = !m ? NULL : m->private;
	int tx;
	int rx;
	int ret;
	int i;
	int index;

	if (!m || !v || !cd) {
		ts_err("rawdata_proc_show, input null ptr");
		return -EIO;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc rawdata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_capacitance_data(cd, info);
	if (ret < 0) {
		ts_err("failed to get_capacitance_data, exit!");
		goto exit;
	}

	rx = info->buff[0];
	tx = info->buff[1];
	seq_printf(m, "TX:%d  RX:%d\n", tx, rx);
	seq_printf(m, "mutual_rawdata:\n");
	index = 2;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "mutual_diffdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%3d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "mutual_refdata:\n");
	index += tx * rx;
	for (i = 0; i < tx * rx; i++) {
		seq_printf(m, "%5d,", info->buff[index + i]);
		if ((i + 1) % tx == 0)
			seq_printf(m, "\n");
	}

exit:
	kfree(info);
	return ret;
}

static int rawdata_proc_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, rawdata_proc_show, pde_data(inode), PAGE_SIZE * 10);
}

static const struct proc_ops rawdata_proc_fops = {
	.proc_open = rawdata_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int framedata_proc_show(struct seq_file *m, void *v)
{
	struct goodix_ts_core *cd = m->private;
	struct ts_framedata *info;
	int ret;
	int i;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ts_err("Failed to alloc framedata info memory");
		return -ENOMEM;
	}

	ret = cd->hw_ops->get_frame_data(cd, info);
	if (ret < 0 || info->used_size == 0)
		goto exit;

	for (i = 0; i < info->used_size; i++) {
		seq_printf(m, "0x%02x,", info->buff[i]);
		if ((i + 1) % 32 == 0)
			seq_printf(m, "\n");
	}
	seq_printf(m, "\n");

exit:
	kfree(info);
	return 0;
}

static int framedata_proc_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, framedata_proc_show, pde_data(inode), PAGE_SIZE * 10);
}

static const struct proc_ops framedata_proc_fops = {
	.proc_open = framedata_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void goodix_ts_procfs_init(struct goodix_ts_core *core_data)
{
	if (!proc_mkdir("goodix_ts", NULL))
		return;
	proc_create_data("goodix_ts/tp_capacitance_data",
			0666, NULL, &rawdata_proc_fops, core_data);

	if (core_data->bus->ic_type == IC_TYPE_BERLIN_D) {
		proc_create_data("goodix_ts/get_frame_data",
				0666, NULL, &framedata_proc_fops, core_data);
	}
}

static void goodix_ts_procfs_exit(struct goodix_ts_core *core_data)
{
	remove_proc_entry("goodix_ts/tp_capacitance_data", NULL);
	if (core_data->bus->ic_type == IC_TYPE_BERLIN_D)
		remove_proc_entry("goodix_ts/get_frame_data", NULL);
	remove_proc_entry("goodix_ts", NULL);
}

/* event notifier */
static BLOCKING_NOTIFIER_HEAD(ts_notifier_list);
/**
 * goodix_ts_register_client - register a client notifier
 * @nb: notifier block to callback on events
 *  see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_register_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ts_notifier_list, nb);
}

/**
 * goodix_ts_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 *	see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_unregister_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&ts_notifier_list, nb);
}

/**
 * fb_notifier_call_chain - notify clients of fb_events
 *	see enum ts_notify_event in goodix_ts_core.h
 */
int goodix_ts_blocking_notify(enum ts_notify_event evt, void *v)
{
	int ret;

	ret = blocking_notifier_call_chain(&ts_notifier_list,
			(unsigned long)evt, v);
	return ret;
}

#ifdef CONFIG_OF
/**
 * goodix_parse_dt_resolution - parse resolution from dt
 * @node: devicetree node
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt_resolution(struct device_node *node,
		struct goodix_ts_board_data *board_data)
{
	int ret;

	ret = of_property_read_u32(node, "goodix,panel-max-x",
				&board_data->panel_max_x);
	if (ret) {
		ts_err("failed get panel-max-x");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-y",
				&board_data->panel_max_y);
	if (ret) {
		ts_err("failed get panel-max-y");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,super-resolution-factor",
				&board_data->super_resolution_factor);
	if (ret < 0) {
		ts_err("Failed get super-resolution-factor property");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-w",
				&board_data->panel_max_w);
	if (ret) {
		ts_err("failed get panel-max-w");
		return ret;
	}

	ret = of_property_read_u32(node, "goodix,panel-max-p",
				&board_data->panel_max_p);
	if (ret) {
		ts_err("failed get panel-max-p, use default");
		board_data->panel_max_p = GOODIX_PEN_MAX_PRESSURE;
	}

	return 0;
}

/**
 * goodix_parse_dt - parse board data from dt
 * @dev: pointer to device
 * @board_data: pointer to board data structure
 * return: 0 - no error, <0 error
 */
static int goodix_parse_dt(struct device_node *node,
	struct goodix_ts_board_data *board_data)
{
	const char *name_tmp;
	int r;

	if (!board_data) {
		ts_err("invalid board data");
		return -EINVAL;
	}

	r = of_get_named_gpio(node, "goodix,avdd-gpio", 0);
	if (r < 0) {
		ts_info("can't find avdd-gpio, use other power supply");
		board_data->avdd_gpio = 0;
	} else {
		ts_info("get avdd-gpio[%d] from dt", r);
		board_data->avdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,iovdd-gpio", 0);
	if (r < 0) {
		ts_info("can't find iovdd-gpio, use other power supply");
		board_data->iovdd_gpio = 0;
	} else {
		ts_info("get iovdd-gpio[%d] from dt", r);
		board_data->iovdd_gpio = r;
	}

	r = of_get_named_gpio(node, "goodix,reset-gpio", 0);
	if (r < 0) {
		ts_err("invalid reset-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get reset-gpio[%d] from dt", r);
	board_data->reset_gpio = r;

	r = of_get_named_gpio(node, "goodix,irq-gpio", 0);
	if (r < 0) {
		ts_err("invalid irq-gpio in dt: %d", r);
		return -EINVAL;
	}
	ts_info("get irq-gpio[%d] from dt", r);
	board_data->irq_gpio = r;

	r = of_property_read_u32(node, "goodix,irq-flags",
			&board_data->irq_flags);
	if (r) {
		ts_err("invalid irq-flags");
		return -EINVAL;
	}

	memset(board_data->avdd_name, 0, sizeof(board_data->avdd_name));
	r = of_property_read_string(node, "goodix,avdd-name", &name_tmp);
	if (!r) {
		ts_info("avdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->avdd_name))
			strncpy(board_data->avdd_name,
				name_tmp, sizeof(board_data->avdd_name));
		else
			ts_info("invalied avdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->avdd_name));
	}

	memset(board_data->iovdd_name, 0, sizeof(board_data->iovdd_name));
	r = of_property_read_string(node, "goodix,iovdd-name", &name_tmp);
	if (!r) {
		ts_info("iovdd name from dt: %s", name_tmp);
		if (strlen(name_tmp) < sizeof(board_data->iovdd_name))
			strncpy(board_data->iovdd_name,
				name_tmp, sizeof(board_data->iovdd_name));
		else
			ts_info("invalied iovdd name length: %ld > %ld",
				strlen(name_tmp),
				sizeof(board_data->iovdd_name));
	}

	/* get firmware file name */
	r = of_property_read_string(node, "goodix,firmware-name", &name_tmp);
	if (!r) {
		ts_info("firmware name from dt: %s", name_tmp);
		strncpy(board_data->fw, name_tmp, sizeof(board_data->fw));
	} else {
		ts_info("can't find firmware name, use default: %s", TS_DEFAULT_FIRMWARE);
		strncpy(board_data->fw_name, TS_DEFAULT_FIRMWARE, sizeof(board_data->fw_name));
	}

	/* get config file name */
	r = of_property_read_string(node, "goodix,config-name", &name_tmp);
	if (!r) {
		ts_info("config name from dt: %s", name_tmp);
		strncpy(board_data->cfg_bin, name_tmp, sizeof(board_data->cfg_bin));
	} else {
		ts_info("can't find config name, use default: %s", TS_DEFAULT_CFG_BIN);
		strncpy(board_data->cfg_bin_name, TS_DEFAULT_CFG_BIN, sizeof(board_data->cfg_bin_name));
	}

	r = of_property_count_u32_elems(node, "goodix,touch-expert-array");
	if (r == GAME_ARRAY_LEN * GAME_ARRAY_SIZE) {
		of_property_read_u32_array(node,
						"goodix,touch-expert-array",
						board_data->touch_expert_array,
						r);
	} else {
		ts_err("Failed to parse touch-expert-array:%d", r);
	}

	/* get xyz resolutions */
	r = goodix_parse_dt_resolution(node, board_data);
	if (r) {
		ts_err("Failed to parse resolutions:%d", r);
		return r;
	}

#ifdef TOUCH_TRUSTED_SUPPORT
	goodix_core_data->qts_en = of_property_read_bool(node, "goodix,qts_en");
	if (goodix_core_data->qts_en) {
		ts_info("QTS enabled");
	}

	r = of_property_read_string(node, "goodix,touch-type", &qts_touch_type);
	if (!r) {
		ts_debug("qts touch type from dt: %s", qts_touch_type);
	} else {
		ts_err("fail to get touch type(%s)", qts_touch_type);
		return r;
	}
#endif // TOUCH_TRUSTED_SUPPORT

	/*get pen-enable switch and pen keys, must after "key map"*/
	board_data->pen_enable = of_property_read_bool(node,
					"goodix,pen-enable");
	if (board_data->pen_enable)
		ts_info("goodix pen enabled");

	ts_info("[DT]x:%d, y:%d, w:%d, p:%d", board_data->panel_max_x,
		board_data->panel_max_y, board_data->panel_max_w,
		board_data->panel_max_p);

	return 0;
}
#endif

static void goodix_ts_report_pen(struct input_dev *dev,
		struct goodix_pen_data *pen_data)
{
	int i;

	mutex_lock(&dev->mutex);

	if (pen_data->coords.status == TS_TOUCH) {
		input_report_key(dev, BTN_TOUCH, 1);
		input_report_key(dev, pen_data->coords.tool_type, 1);
		input_report_abs(dev, ABS_X, pen_data->coords.x);
		input_report_abs(dev, ABS_Y, pen_data->coords.y);
		input_report_abs(dev, ABS_PRESSURE, pen_data->coords.p);
		input_report_abs(dev, ABS_TILT_X, pen_data->coords.tilt_x);
		input_report_abs(dev, ABS_TILT_Y, pen_data->coords.tilt_y);
		ts_debug("pen_data:x %d, y %d, p%d, tilt_x %d tilt_y %d key[%d %d]",
				pen_data->coords.x, pen_data->coords.y,
				pen_data->coords.p, pen_data->coords.tilt_x,
				pen_data->coords.tilt_y, pen_data->keys[0].status == TS_TOUCH ? 1 : 0,
				pen_data->keys[1].status == TS_TOUCH ? 1 : 0);
	} else {
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, pen_data->coords.tool_type, 0);
	}
	/* report pen button */
	for (i = 0; i < GOODIX_MAX_PEN_KEY; i++) {
		if (pen_data->keys[i].status == TS_TOUCH)
			input_report_key(dev, pen_data->keys[i].code, 1);
		else
			input_report_key(dev, pen_data->keys[i].code, 0);
	}
	input_sync(dev);
	mutex_unlock(&dev->mutex);
}

void goodix_ts_report_finger(struct input_dev *dev,
		struct goodix_touch_data *touch_data)
{
	unsigned int touch_num = touch_data->touch_num;
	int i;
	static int pre_finger_num;

#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw)
		return;
#endif

	mutex_lock(&dev->mutex);

#ifdef TOUCH_FOD_SUPPORT
	if ((goodix_core_data->eventsdata & 0x08) && ((driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable) == 1) ||
						(driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable) == 2) ||
						(driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable) == 3))
			&& (!goodix_core_data->fod_finger)) {
		goodix_core_data->fod_finger = true;
		if (((driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable) == 3) && (goodix_core_data->super_wallpaper == 0))
			|| goodix_core_data->fod_down_before_suspend) {
			ts_info("skip report fod");
			goto finger_pos;
		}
		ts_info("ts fod down, overlay: %d, fod id: %d", touch_data->overlay, touch_data->fod_id);
		input_report_key(dev, BTN_INFO, 1);
		input_sync(dev);
		update_fod_press_status_common(1);
		goto finger_pos;
	} else if ((goodix_core_data->eventsdata & 0x08) != 0x08 && goodix_core_data->fod_finger) {
		ts_info("ts fod up, overlay: %d", touch_data->overlay);
		goodix_core_data->fod_down_before_suspend = false;
		input_report_key(dev, BTN_INFO, 0);
		input_report_abs(dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(dev, ABS_MT_WIDTH_MINOR, 0);
		input_sync(dev);
		update_fod_press_status_common(0);
		goodix_core_data->fod_finger = false;
		goto finger_pos;
	}
finger_pos:
#endif

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		if (touch_data->coords[i].status == TS_TOUCH) {
			touch_data->coords[i].x *= goodix_core_data->board_data.super_resolution_factor;
			touch_data->coords[i].y *= goodix_core_data->board_data.super_resolution_factor;
			ts_debug("report: id %d, x %d, y %d, w %d, factor %d", i,
				touch_data->coords[i].x, touch_data->coords[i].y,
				touch_data->coords[i].w,
				goodix_core_data->board_data.super_resolution_factor);
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, true);
			input_report_abs(dev, ABS_MT_POSITION_X,
					touch_data->coords[i].x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					touch_data->coords[i].y);
#ifdef TOUCH_FOD_SUPPORT
			if ((goodix_core_data->eventsdata & 0x08) != 0x08 || !driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable))
				touch_data->overlay = 0;
#endif
			input_report_abs(dev, ABS_MT_WIDTH_MAJOR, touch_data->overlay);
			input_report_abs(dev, ABS_MT_WIDTH_MINOR, touch_data->overlay);
			if (!__test_and_set_bit(i, &goodix_core_data->touch_id))
				ts_info("Finger ID[%d] Down", i);
		} else {
			input_mt_slot(dev, i);
			input_mt_report_slot_state(dev, MT_TOOL_FINGER, false);
			if (__test_and_clear_bit(i, &goodix_core_data->touch_id)
				&& touch_data->coords[i].status == TS_RELEASE)
				ts_info("Finger ID[%d] Up", i);
		}
	}

	if (touch_num && !pre_finger_num) {	/*first touch down */
		input_report_key(dev, BTN_TOUCH, 1);
		input_report_key(dev, BTN_TOOL_FINGER, 1);
	} else if (!touch_num && pre_finger_num) {	/*last touch up */
		goodix_core_data->fod_down_before_suspend = false;
		input_report_key(dev, BTN_TOUCH, 0);
		input_report_key(dev, BTN_TOOL_FINGER, 0);
		if (goodix_core_data->fod_finger) {
			ts_info("ts force fod up!");
			input_report_key(dev, BTN_INFO, 0);
			input_report_abs(dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(dev);
			update_fod_press_status_common(0);
			goodix_core_data->fod_finger = false;
		}
	}
	pre_finger_num = touch_num;

	input_sync(dev);
	mutex_unlock(&dev->mutex);
}

static int goodix_ts_request_handle(struct goodix_ts_core *cd,
	struct goodix_ts_event *ts_event)
{
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = -1;

	if (ts_event->request_code == REQUEST_TYPE_CONFIG)
		ret = goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
	else if (ts_event->request_code == REQUEST_TYPE_RESET)
		ret = hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	else
		ts_info("can not handle request type 0x%x",
			ts_event->request_code);
	if (ret)
		ts_err("failed handle request 0x%x",
			ts_event->request_code);
	else
		ts_info("success handle ic request 0x%x",
			ts_event->request_code);
	return ret;
}

static void goodix_sched_sethigh(struct task_struct *p)
{
	struct sched_param sp = { .sched_priority = MAX_RT_PRIO - 1 };
	WARN_ON_ONCE(sched_setscheduler_nocheck(p, SCHED_FIFO, &sp) != 0);
}

struct goodix_ts_core *get_goodix_core_data(void) {
	return goodix_core_data;
}



/**
 * goodix_ts_threadirq_func - Bottom half of interrupt
 * This functions is excuted in thread context,
 * sleep in this function is permit.
 *
 * @data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static irqreturn_t goodix_ts_threadirq_func(int irq, void *data)
{
	struct goodix_ts_core *core_data = data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_event *ts_event = &core_data->ts_event;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;
	int ret;

	/* improve irq thread priority, bindcpu affinity and set VIP (once) */
	if (!core_data->irq_priority_high) {
		/* _b573 boost wiring: blob 0xb000-0xb008 = touch_irq_cpumask(0)；
		 * DT 配置 normal_cpu_mask=<0x0f>（xiaomi_rodin_mt6899_touch.dtsi），
		 * 树版原硬编码 0-2 会覆盖 DT 配置，改回框架调用 */
		touch_irq_cpumask(TOUCH_ID);

		goodix_sched_sethigh(current);

		core_data->irq_priority_high = true;
		/* #228（b581）blob goodix_ts_threadirq_func 厂商源行 1777 的打印串为
		 * "set goodix_irq priority"（.rodata.str1.1+0xc13e，无参数）；
		 * 树侧多出的 "cpu affinity: %d" 形式为偏离，按 blob 收口 */
		ts_info("set goodix_irq priority");
	}

	ts_esd->irq_status = true;
	core_data->irq_trig_cnt++;

	pm_stay_awake(core_data->bus->dev);

#ifdef CONFIG_PM
	if (core_data->tp_pm_suspend) {
		ts_info("device in suspend, wait to resume");
		ret = wait_for_completion_timeout(&core_data->pm_resume_completion, msecs_to_jiffies(150));
		if (!ret) {
			pm_relax(core_data->bus->dev);
			ts_err("system can't finished resuming procedure");
			return IRQ_HANDLED;
		}
	}
#endif

#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(true, EVENT_INPUT);
#endif

	cpu_latency_qos_add_request(&core_data->pm_qos_req_irq, 0);

	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	list_for_each_entry_safe(ext_module, next,
				&goodix_modules.head, list) {
		if (!ext_module->funcs->irq_event)
			continue;
		ret = ext_module->funcs->irq_event(core_data, ext_module);
		if (ret == EVT_CANCEL_IRQEVT) {
			mutex_unlock(&goodix_modules.mutex);
#ifdef CONFIG_TOUCH_BOOST
			lpm_disable_for_dev(false, EVENT_INPUT);
#endif
			cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
			pm_relax(core_data->bus->dev);
			return IRQ_HANDLED;
		}
	}

	if (core_data->work_status == TP_GESTURE) {
		ret = goodix_gesture_ist(core_data);
		if (ret == EVT_CANCEL_IRQEVT) {
			mutex_unlock(&goodix_modules.mutex);
#ifdef CONFIG_TOUCH_BOOST
			lpm_disable_for_dev(false, EVENT_INPUT);
#endif
			cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
				pm_relax(core_data->bus->dev);
				return IRQ_HANDLED;
			}
			/* #228（b581）blob 无 "gesture_ist not cancel, ret=%d, continu"
			 * 串（strcmp only-tree），此路径静默继续，按 blob 去掉 */
		}
		mutex_unlock(&goodix_modules.mutex);

	/* read touch data from touch device */
	ret = hw_ops->event_handler(core_data, ts_event);
	if (likely(!ret)) {
		if (ts_event->event_type == EVENT_TOUCH) {
			/* report touch */
			goodix_ts_report_finger(core_data->input_dev,
					&ts_event->touch_data);
		}
		if (core_data->board_data.pen_enable &&
				ts_event->event_type == EVENT_PEN) {
			goodix_ts_report_pen(core_data->pen_dev,
					&ts_event->pen_data);
		}
		if (ts_event->event_type == EVENT_REQUEST) {
			goodix_ts_request_handle(core_data, ts_event);
		}
	}
	/* #228（b581）blob 无 "event_handler failed, ret=%d" 串（strcmp only-tree），
	 * 该失败路径静默，按 blob 去掉打印 */

	ts_event->retry = 0;
#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(false, EVENT_INPUT);
#endif

	cpu_latency_qos_remove_request(&core_data->pm_qos_req_irq);
	pm_relax(core_data->bus->dev);
	return IRQ_HANDLED;
}

/**
 * goodix_ts_init_irq - Requset interrput line from system
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_irq_setup(struct goodix_ts_core *core_data)
{
	const struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int ret;

	/* if ts_bdata-> irq is invalid */
	core_data->irq = gpio_to_irq(ts_bdata->irq_gpio);
	if (core_data->irq < 0) {
		ts_err("failed get irq num %d", core_data->irq);
		return -EINVAL;
	}

	ts_info("IRQ:%u,flags:%d", core_data->irq, (int)ts_bdata->irq_flags);
	ret = devm_request_threaded_irq(&core_data->pdev->dev,
					core_data->irq, NULL,
					goodix_ts_threadirq_func,
					ts_bdata->irq_flags | IRQF_ONESHOT,
					"xiaomi_tp" GOODIX_CORE_DRIVER_NAME,
					core_data);
	if (ret < 0)
		ts_err("Failed to requeset threaded irq:%d", ret);
	else
		atomic_set(&core_data->irq_enabled, 1);

	return ret;
}

/**
 * goodix_ts_power_init - Get regulator for touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_power_init(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct device *dev = core_data->bus->dev;
	int ret = 0;

	ts_info("Power init");
	if (strlen(ts_bdata->avdd_name)) {
		core_data->avdd = devm_regulator_get(dev,
				ts_bdata->avdd_name);
		if (IS_ERR_OR_NULL(core_data->avdd)) {
			ret = PTR_ERR(core_data->avdd);
			ts_err("Failed to get regulator avdd:%d", ret);
			core_data->avdd = NULL;
			return ret;
		}
		/* #228（b581）blob 反汇编复原：goodix_ts_power_init 内联在
		 * goodix_ts_probe+0x850/+0x87c，确有 regulator_set_voltage(avdd,
		 * 3300000, 3300000)（0xd83c 立即数 0x325aa0 = 3,300,000），失败打
		 * "set avdd voltage failed"（厂商源行 1842）后 return ret；
		 * 原树整块注释掉属行为偏离，按 blob 落地 */
		ret = regulator_set_voltage(core_data->avdd, 3300000, 3300000);
		if (ret < 0) {
			ts_err("set avdd voltage failed");
			return ret;
		}
	} else {
		ts_info("Avdd name is NULL");
	}
	ts_info("avdd end.");
	if (strlen(ts_bdata->iovdd_name)) {
		core_data->iovdd = devm_regulator_get(dev,
				ts_bdata->iovdd_name);
		if (IS_ERR_OR_NULL(core_data->iovdd)) {
			ret = PTR_ERR(core_data->iovdd);
			ts_err("Failed to get regulator iovdd:%d", ret);
			core_data->iovdd = NULL;
		}
		/* #228（b581）同上按 blob 复原：goodix_ts_probe+0xa0c 立即数
		 * 0x1b7740 = 1,800,000；失败打 "set iovdd voltage failed"
		 * （厂商源行 1859）后 return ret。注意 blob 在 iovdd 取失败
		 * （IS_ERR_OR_NULL）时不提前返回，仍以 NULL 调用 set_voltage
		 * （regulator_set_voltage 对 NULL 内部即返 0）——与树同形保留 */
		ret = regulator_set_voltage(core_data->iovdd, 1800000, 1800000);
		if (ret < 0) {
			ts_err("set iovdd voltage failed");
			return ret;
		}
	} else {
		ts_info("iovdd name is NULL");
	}
	/* #228（b581）blob 0xda24 行 1865 ts_info("iovdd end.")：树缺，补齐 */
	ts_info("iovdd end.");

	return ret;
}
/**
 * goodix_ts_pinctrl_init - Get pinctrl handler and pinctrl_state
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_pinctrl_init(struct goodix_ts_core *core_data)
{
	int r = 0;

	/* get pinctrl handler from of node */
	core_data->pinctrl = devm_pinctrl_get(core_data->bus->dev);
	if (IS_ERR_OR_NULL(core_data->pinctrl)) {
		ts_info("Failed to get pinctrl handler[need confirm]");
		core_data->pinctrl = NULL;
		return -EINVAL;
	}
	/* active state */
	core_data->pin_sta_active = pinctrl_lookup_state(core_data->pinctrl,
				PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(core_data->pin_sta_active)) {
		r = PTR_ERR(core_data->pin_sta_active);
		ts_err("Failed to get pinctrl state:%s, r:%d",
				PINCTRL_STATE_ACTIVE, r);
		core_data->pin_sta_active = NULL;
		goto exit_pinctrl_put;
	}

	/* suspend state */
	core_data->pin_sta_suspend = pinctrl_lookup_state(core_data->pinctrl,
				PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(core_data->pin_sta_suspend)) {
		r = PTR_ERR(core_data->pin_sta_suspend);
		ts_err("Failed to get pinctrl state:%s, r:%d",
				PINCTRL_STATE_SUSPEND, r);
		core_data->pin_sta_suspend = NULL;
		goto exit_pinctrl_put;
	}

	/* #228（b581）blob goodix_ts_pinctrl_init 0xe4ec 逐点复原：共 5 次
	 * lookup（active/suspend/touch_mode_ap/touch_mode_scp/pmx_gt_spi_mode），
	 * 五次失败全部走 exit_pinctrl_put（devm_pinctrl_put + 返回 r）＝致命。
	 * 原树此处的 ap/scp 块重复了两遍（7 次 lookup）且失败降为容忍，属偏离，
	 * 按 blob 收口：单份、致命；错误串统一为
	 * "Failed to get pinctrl state:%s, r:%d"（0xe698 格式串 0x70a3，
	 * x3=状态名），仅 pmx_gt_spi_mode 用 "Failed to get %s, r: %d"
	 * （0xe754 区、厂商源行 1933） */
	core_data->pin_sta_touch_mode_ap = pinctrl_lookup_state(core_data->pinctrl,
				"touch_mode_ap");
	if (IS_ERR_OR_NULL(core_data->pin_sta_touch_mode_ap)) {
		r = PTR_ERR(core_data->pin_sta_touch_mode_ap);
		ts_err("Failed to get pinctrl state:%s, r:%d", "touch_mode_ap", r);
		core_data->pin_sta_touch_mode_ap = NULL;
		goto exit_pinctrl_put;
	}
	core_data->pin_sta_touch_mode_scp = pinctrl_lookup_state(core_data->pinctrl,
				"touch_mode_scp");
	if (IS_ERR_OR_NULL(core_data->pin_sta_touch_mode_scp)) {
		r = PTR_ERR(core_data->pin_sta_touch_mode_scp);
		ts_err("Failed to get pinctrl state:%s, r:%d", "touch_mode_scp", r);
		core_data->pin_sta_touch_mode_scp = NULL;
		goto exit_pinctrl_put;
	}

	/* #228（b581）pmx_gt_spi_mode（主 SPI 三组引脚）：blob 0xe5a0 lookup →
	 * 0xe668 失败即 0xe678 devm_pinctrl_put，属致命；格式串 "Failed to get
	 * %s, r: %d"，厂商源行 1933 */
	core_data->pin_sta_spi_mode = pinctrl_lookup_state(core_data->pinctrl,
				"pmx_gt_spi_mode");
	if (IS_ERR_OR_NULL(core_data->pin_sta_spi_mode)) {
		r = PTR_ERR(core_data->pin_sta_spi_mode);
		ts_err("Failed to get %s, r: %d", "pmx_gt_spi_mode", r);
		core_data->pin_sta_spi_mode = NULL;
		goto exit_pinctrl_put;
	}
	ts_info("success get pinctrl state");

	return 0;
exit_pinctrl_put:
	devm_pinctrl_put(core_data->pinctrl);
	core_data->pinctrl = NULL;
	return r;
}

/**
 * goodix_ts_power_on - Turn on power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
int goodix_ts_power_on(struct goodix_ts_core *cd)
{
	int ret = 0;

	ts_info("power on");
	if (cd->power_on)
		return 0;

	ret = cd->hw_ops->power_on(cd, true);
	if (!ret)
		cd->power_on = 1;
	else
		ts_err("failed power on, %d", ret);
	return ret;
}

/**
 * goodix_ts_power_off - Turn off power to the touch device
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
int goodix_ts_power_off(struct goodix_ts_core *cd)
{
	int ret;

	ts_info("Device power off");
	if (!cd->power_on)
		return 0;

	ret = cd->hw_ops->power_on(cd, false);
	if (!ret)
		cd->power_on = 0;
	else
		ts_err("failed power off, %d", ret);

	return ret;
}

/**
 * goodix_ts_gpio_setup - Request gpio resources from GPIO subsysten
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_gpio_setup(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	int r = 0;

	ts_info("GPIO setup,reset-gpio:%d, irq-gpio:%d",
		ts_bdata->reset_gpio, ts_bdata->irq_gpio);
	/*
	 * after kenerl3.13, gpio_ api is deprecated, new
	 * driver should use gpiod_ api.
	 */
	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->reset_gpio,
				  GPIOF_OUT_INIT_LOW, "ts_reset_gpio");
	if (r < 0) {
		ts_err("Failed to request reset gpio, r:%d", r);
		return r;
	}

	r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->irq_gpio,
				  GPIOF_IN, "ts_irq_gpio");
	if (r < 0) {
		ts_err("Failed to request irq gpio, r:%d", r);
		return r;
	}

	if (ts_bdata->avdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->avdd_gpio,
				GPIOF_OUT_INIT_LOW, "ts_avdd_gpio");
		if (r < 0) {
			ts_err("Failed to request avdd-gpio, r:%d", r);
			return r;
		}
	}

	if (ts_bdata->iovdd_gpio > 0) {
		r = devm_gpio_request_one(&core_data->pdev->dev, ts_bdata->iovdd_gpio,
				GPIOF_OUT_INIT_LOW, "ts_iovdd_gpio");
		if (r < 0) {
			ts_err("Failed to request iovdd-gpio, r:%d", r);
			return r;
		}
	}

	ts_info("[%s] end.", __func__);

	return 0;
}

/**
 * goodix_ts_input_dev_config - Requset and config a input device
 *  then register it to input sybsystem.
 * @core_data: pointer to touch core data
 * return: 0 ok, <0 failed
 */
static int goodix_ts_input_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct input_dev *input_dev = NULL;
	int r;

	input_dev = input_allocate_device();
	if (!input_dev) {
		ts_err("Failed to allocated input device");
		return -ENOMEM;
	}

	core_data->input_dev = input_dev;
	input_set_drvdata(input_dev, core_data);

	input_dev->name = GOODIX_CORE_DRIVER_NAME;
	input_dev->phys = GOOIDX_INPUT_PHYS;
	input_dev->id.product = 0xDEAD;
	input_dev->id.vendor = 0xBEEF;
	input_dev->id.version = 10427;

	__set_bit(EV_SYN, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(BTN_INFO, input_dev->keybit);
	__set_bit(KEY_WAKEUP, input_dev->keybit);
	__set_bit(KEY_GOTO, input_dev->keybit);
	__set_bit(BTN_TOOL_FINGER, input_dev->keybit);

#ifdef INPUT_PROP_DIRECT
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
#endif

	/* set input parameters */
	input_set_abs_params(input_dev, ABS_MT_POSITION_X,
				0, ts_bdata->panel_max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
				0, ts_bdata->panel_max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
				0, ts_bdata->panel_max_w, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR,
				0, 100, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR,
				0, 100, 0, 0);
#ifdef INPUT_TYPE_B_PROTOCOL
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 7, 0)
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(input_dev, GOODIX_MAX_TOUCH);
#endif
#endif

	input_set_capability(input_dev, EV_KEY, KEY_POWER);
	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
	input_set_capability(input_dev, EV_KEY, KEY_GOTO);
	input_set_capability(input_dev, EV_KEY, BTN_INFO);
	r = input_register_device(input_dev);
	if (r < 0) {
		ts_err("Unable to register input device");
		input_free_device(input_dev);
		return r;
	}

	return 0;
}

static int goodix_ts_pen_dev_config(struct goodix_ts_core *core_data)
{
	struct goodix_ts_board_data *ts_bdata = board_data(core_data);
	struct input_dev *pen_dev = NULL;
	int r;

	pen_dev = input_allocate_device();
	if (!pen_dev) {
		ts_err("Failed to allocated pen device");
		return -ENOMEM;
	}

	core_data->pen_dev = pen_dev;
	input_set_drvdata(pen_dev, core_data);

	pen_dev->name = GOODIX_PEN_DRIVER_NAME;
	pen_dev->id.product = 0xDEAD;
	pen_dev->id.vendor = 0xBEEF;
	pen_dev->id.version = 10427;

	pen_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	__set_bit(ABS_X, pen_dev->absbit);
	__set_bit(ABS_Y, pen_dev->absbit);
	__set_bit(ABS_TILT_X, pen_dev->absbit);
	__set_bit(ABS_TILT_Y, pen_dev->absbit);
	__set_bit(BTN_STYLUS, pen_dev->keybit);
	__set_bit(BTN_STYLUS2, pen_dev->keybit);
	__set_bit(BTN_TOUCH, pen_dev->keybit);
	__set_bit(BTN_TOOL_PEN, pen_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, pen_dev->propbit);
	input_set_abs_params(pen_dev, ABS_X, 0, ts_bdata->panel_max_x, 0, 0);
	input_set_abs_params(pen_dev, ABS_Y, 0, ts_bdata->panel_max_y, 0, 0);
	input_set_abs_params(pen_dev, ABS_PRESSURE, 0,
			ts_bdata->panel_max_p, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_X,
			-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);
	input_set_abs_params(pen_dev, ABS_TILT_Y,
			-GOODIX_PEN_MAX_TILT, GOODIX_PEN_MAX_TILT, 0, 0);

	r = input_register_device(pen_dev);
	if (r < 0) {
		ts_err("Unable to register pen device");
		input_free_device(pen_dev);
		return r;
	}

	return 0;
}

void goodix_ts_input_dev_remove(struct goodix_ts_core *core_data)
{
	if (!core_data->input_dev)
		return;
	input_unregister_device(core_data->input_dev);
	input_free_device(core_data->input_dev);
	core_data->input_dev = NULL;
}

void goodix_ts_pen_dev_remove(struct goodix_ts_core *core_data)
{
	if (!core_data->pen_dev)
		return;
	input_unregister_device(core_data->pen_dev);
	input_free_device(core_data->pen_dev);
	core_data->pen_dev = NULL;
}

/**
 * goodix_ts_esd_work - check hardware status and recovery
 *  the hardware if needed.
 */
static void goodix_ts_esd_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct goodix_ts_esd *ts_esd = container_of(dwork,
			struct goodix_ts_esd, esd_work);
	struct goodix_ts_core *cd = container_of(ts_esd,
			struct goodix_ts_core, ts_esd);
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	int ret = 0;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->tui_process) {
		if (wait_for_completion_interruptible(&cd->tui_finish)) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (ts_esd->irq_status)
		goto exit;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	if (!hw_ops->esd_check)
		return;

	ret = hw_ops->esd_check(cd);
	if (ret) {
		ts_err("esd check failed");
		goodix_ts_power_off(cd);
		goodix_ts_power_on(cd);
	}

exit:
	ts_esd->irq_status = false;
	if (atomic_read(&ts_esd->esd_on))
		schedule_delayed_work(&ts_esd->esd_work, 3 * HZ);
}

/**
 * goodix_ts_esd_on - turn on esd protection
 */
void goodix_ts_esd_on(struct goodix_ts_core *cd)
{
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!misc->esd_addr)
		return;

	if (atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 1);
	if (!schedule_delayed_work(&ts_esd->esd_work, 3 * HZ)) {
		ts_info("esd work already in workqueue");
	}
	ts_info("esd on");
}

/**
 * goodix_ts_esd_off - turn off esd protection
 */
void goodix_ts_esd_off(struct goodix_ts_core *cd)
{
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;
	int ret;

	if (!atomic_read(&ts_esd->esd_on))
		return;

	atomic_set(&ts_esd->esd_on, 0);
	ret = cancel_delayed_work_sync(&ts_esd->esd_work);
	ts_info("Esd off, esd work state %d", ret);
}

/**
 * goodix_esd_notifier_callback - notification callback
 *  under certain condition, we need to turn off/on the esd
 *  protector, we use kernel notify call chain to achieve this.
 *
 *  for example: before firmware update we need to turn off the
 *  esd protector and after firmware update finished, we should
 *  turn on the esd protector.
 */
static int goodix_esd_notifier_callback(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct goodix_ts_esd *ts_esd = container_of(nb,
			struct goodix_ts_esd, esd_notifier);

	switch (action) {
	case NOTIFY_FWUPDATE_START:
	case NOTIFY_SUSPEND:
	case NOTIFY_ESD_OFF:
		goodix_ts_esd_off(ts_esd->ts_core);
		break;
	case NOTIFY_FWUPDATE_FAILED:
	case NOTIFY_FWUPDATE_SUCCESS:
	case NOTIFY_RESUME:
	case NOTIFY_ESD_ON:
		goodix_ts_esd_on(ts_esd->ts_core);
		break;
	default:
		break;
	}

	return 0;
}

/**
 * goodix_ts_esd_init - initialize esd protection
 */
int goodix_ts_esd_init(struct goodix_ts_core *cd)
{
	struct goodix_ic_info_misc *misc = &cd->ic_info.misc;
	struct goodix_ts_esd *ts_esd = &cd->ts_esd;

	if (!cd->hw_ops->esd_check || !misc->esd_addr) {
		ts_info("missing key info for esd check");
		return 0;
	}

	INIT_DELAYED_WORK(&ts_esd->esd_work, goodix_ts_esd_work);
	ts_esd->ts_core = cd;
	atomic_set(&ts_esd->esd_on, 0);
	ts_esd->esd_notifier.notifier_call = goodix_esd_notifier_callback;
	goodix_ts_register_notifier(&ts_esd->esd_notifier);
	goodix_ts_esd_on(cd);

	return 0;
}

static void goodix_ts_release_connects(struct goodix_ts_core *core_data)
{
	struct input_dev *input_dev = core_data->input_dev;
	int i;

	mutex_lock(&input_dev->mutex);
	core_data->fod_down_before_suspend = false;
	if (core_data->fod_finger) {
		core_data->fod_finger = false;
		core_data->fod_down_before_suspend = true;
		input_report_key(input_dev, BTN_INFO, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_sync(input_dev);
		update_fod_press_status_common(0);
		ts_info("ts fod up for suspend");
	}

	for (i = 0; i < GOODIX_MAX_TOUCH; i++) {
		input_mt_slot(input_dev, i);
		input_mt_report_slot_state(input_dev,
				MT_TOOL_FINGER, false);
	}
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_mt_sync_frame(input_dev);
	input_sync(input_dev);

	/* _b583b-GX/T3：blob release_connects（resume_suspend 内联，站点
	 * 0x103e0/0x10478，0x10488-0x10628）在 mt_slot 环+sync 后**无**
	 * fingerprint_authstate 分支与第二次 update_fod 站点，直接 unlock。
	 * 树原 authstate if/else（含 'fingerprint_authstate: %d ...' 两条
	 * tree-only 串与第 3 个 update_fod 站点）为 warsaw 残项，按 blob 删 */

	mutex_unlock(&input_dev->mutex);
}

static void goodix_cmd_fifo_get(u64 address, u8 *buf)
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	struct goodix_ic_info_misc *misc = &goodix_core_data->ic_info.misc;

	int node_data_size = 256;
	int ret;
	int pos = 0;
	int i;
	int j;
	u8 *read_buff = NULL;

	read_buff = (u8*)kzalloc(node_data_size, GFP_KERNEL);

	if (!read_buff) {
		/* _b583b-GX：blob 无 'fail memory' 串（strcmp only-tree），删打印留守卫 */
		return;
	}
	if (!buf) {
		goto end;
	}
	ret = hw_ops->read(goodix_core_data, misc->frame_data_addr,
		read_buff, node_data_size);
	if (ret) {
		ts_err("failed get frame data");
		goto end;
	}

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 16; j++) {
			pos += snprintf(buf + pos, PAGE_SIZE, "%02x ",
			     read_buff[i * 16 + j]);
		}
		buf[pos++] = '\n';
	}
end:
	if (read_buff)
		kfree(read_buff);
}

static int goodix_touch_doze_analysis(int input)
{
	int error =  0;
	int irq_status = 0;
	int update_flag = UPDATE_MODE_FORCE | UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;
	const struct goodix_ts_board_data *ts_bdata = NULL;

	ts_info("input: %d", input);
	if (!goodix_core_data) {
		ts_err("cd is null");
		return -EIO;
	}
	ts_bdata = board_data(goodix_core_data);
	switch(input) {
		case POWER_RESET:
			goodix_core_data->doze_test = 1;
			goodix_resume_suspend(false, xiaomi_get_gesture_type_common(TOUCH_ID));
			goodix_resume_suspend(true, xiaomi_get_gesture_type_common(TOUCH_ID));
			goodix_core_data->doze_test = 0;
		break;
		case RELOAD_FW:
			error = goodix_do_fw_update(NULL, update_flag);
			if (!error)
				ts_info("success do update work");
		break;
		case ENABLE_IRQ:
			enable_irq(goodix_core_data->irq);
		break;
		case DISABLE_IRQ:
			disable_irq(goodix_core_data->irq);
		break;
		case REGISTER_IRQ:
			error = devm_request_threaded_irq(&goodix_core_data->pdev->dev,
							goodix_core_data->irq, NULL,
							goodix_ts_threadirq_func,
							ts_bdata->irq_flags | IRQF_ONESHOT,
							"xiaomi_tp" GOODIX_CORE_DRIVER_NAME,
							goodix_core_data);
			if (error < 0)
				ts_err("Failed to requeset threaded irq:%d", error);
			else
				enable_irq(goodix_core_data->irq);
		break;
		case IRQ_PIN_LEVEL:
			irq_status = gpio_get_value(ts_bdata->irq_gpio) == 0 ? 0 : 1;
		break;
		case ENTER_SUSPEND:
			schedule_resume_suspend_work_common(TOUCH_ID, false);
		break;
		case ENTER_RESUME:
			schedule_resume_suspend_work_common(TOUCH_ID, true);
		break;
		case POWER_ON:
			error = goodix_ts_power_on(goodix_core_data);
			if (error < 0)
				ts_err("Failed to enable regulators");
		break;
		case POWER_OFF:
			error = goodix_ts_power_off(goodix_core_data);
			if (error < 0)
				ts_err("Failed to disable regulators");
		break;
		default:
			ts_err("don't support touch doze analysis");
			break;
	}
	return irq_status;
}

#ifdef CONFIG_FACTORY_BUILD
static bool cmd_flag = true;
void ts_test_cmd_enable(bool en) {
	cmd_flag = en;
}
#endif

static int goodix_htc_ic_setModeValue(common_data_t *common_data)
{
	int data_len = common_data->data_len -1;
	unsigned int addr = common_data->data_buf[0];
	unsigned char *data_buf = (char *)&common_data->data_buf[1];
	int ret = 0;
	bool flag = false;

	ts_debug("setModeValue in, cmd = %d, mode = %d, addr = 0x%x", common_data->cmd, common_data->mode, addr);	/* #228：blob 等级 D */
	if (!goodix_core_data) {
		ts_err("not inited");
		return -EFAULT;
	}
	if (data_len > sizeof(s32) * CMD_DATA_BUF_SIZE) {
		ts_err("data length is over the limit");
		return -EFAULT;
	}

	flag = goodix_get_ic_self_test_mode();
	if (flag) {
		ts_info("tp is in open/short test");
		return 0;
	}

	/* _b583b-GX/T6 裁决：blob goodix_htc_ic_setModeValue（0x11438，488B）
	 * 全函数无 suspended 提前返回——suspended 期照发 hw_ops->write；
	 * 等价性判据见 evidence_b583b.txt。树原 'tp is suspend, skip setModeValue'
	 * 为 warsaw 残项，删 */
	ret = goodix_core_data->hw_ops->write(goodix_core_data, addr, data_buf, data_len);
	if (ret < 0) {
		ts_err("failed write addr(%x) data 0x%*ph", addr,
			data_len, data_buf);
	}
	ts_debug("setModeValue out, cmd = %d, mode = %d, value:0x%*ph", common_data->cmd, common_data->mode, data_len, data_buf);	/* #228：blob 等级 D */
	return 0;
}

static int goodix_htc_ic_getModeValue(common_data_t *common_data)
{
	int data_len = common_data->data_len;
	unsigned int addr = common_data->data_buf[0];
	unsigned char *data_buf = (char *)&common_data->data_buf[1];
	int ret;
	ts_info("getModeValue in, addr = 0x%x, data length = %d", addr, data_len);
	if (!goodix_core_data) {
		ts_err("not inited");
		return -EFAULT;
	}
	if (data_len > sizeof(s32) * (CMD_DATA_BUF_SIZE - 1)) {
		ts_err("data length is over the limit");
		return -EFAULT;
	}
	/* _b583b-GX/T6 裁决：blob goodix_htc_ic_getModeValue（0x11624，348B）
	 * 全函数无 suspended 提前返回——suspended 期照发 hw_ops->read；
	 * 树原 'tp is suspend, skip getModeValue' 为 warsaw 残项，删 */

	ret = goodix_core_data->hw_ops->read(goodix_core_data, addr, data_buf, data_len);
	if (ret) {
		/* _b583-GX：blob L2513（站点 0x11740）无参数；L2517（0x1175c）不带 addr */
		ts_err("can't get, error");
		return -EINVAL;
	}

	ts_info("getModeValue out, value:0x%*ph", data_len, data_buf);
	return 0;
}

/**
 * goodix_ts_suspend - Touchscreen suspend function
 * Called by PM/FB/EARLYSUSPEN module to put the device to  sleep
 */
static int goodix_ts_suspend(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
#ifdef TOUCH_THP_SUPPORT
	struct input_dev *input_dev = core_data->input_dev;
#endif
	int error = 0;
	int ret;
#ifdef TOUCH_TRUSTED_SUPPORT
	struct qts_data *qts_data = NULL;
	qts_data = get_qts_data_helper(&qts_vendor_data);

	qts_ts_suspend(qts_data);
#endif // TOUCH_TRUSTED_SUPPORT

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			atomic_read(&core_data->suspended))
		return 0;
	mutex_lock(&core_data->core_mutex);
	ts_info("Suspend start, scptp_cur_state=%d", scp_tp_param.param0);
	atomic_set(&core_data->suspended, 1);
	enable_temperature_detection_func(TOUCH_ID, false);
	/* _b573 scp 联动：blob 0x10040-0x1008c = suspend 时 FOD 值==3 则推送模式数组
	 * （touch_mode[Touch_Fod_Enable]=1，掩码 BIT(Touch_Fod_Enable)=0x400）保持指纹通道 */
	if (driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable) == 3) {
		int touch_mode[DATA_MODE_45] = { 0 };

		touch_mode[Touch_Fod_Enable] = 1;
		driver_update_touch_mode_common(TOUCH_ID, touch_mode, BIT(Touch_Fod_Enable));
	}
	core_data->irq_trig_cnt = 0;

	/*
	 * notify suspend event, inform the esd protector
	 * and charger detector to turn off the work
	 */
	goodix_ts_blocking_notify(NOTIFY_SUSPEND, NULL);

	hw_ops->irq_enable(core_data, false);
	/* inform external module */
	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->before_suspend)
				continue;
			ret = ext_module->funcs->before_suspend(core_data,ext_module);
			if (ret == EVT_CANCEL_SUSPEND) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}

	if(core_data->doze_test == 1) {
		error = goodix_ts_power_off(core_data);
		if (error < 0)
			ts_err("ERROR Failed to enable regulators\n");
	}
	mutex_unlock(&goodix_modules.mutex);

	if (core_data->gesture_enabled) {
		gsx_gesture_before_suspend(core_data);
		goto out;
	}

	core_data->work_status = TP_SLEEP;
	/* enter sleep mode or power off */
	if (hw_ops->suspend)
		hw_ops->suspend(core_data);

	/*if (core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
				core_data->pin_sta_suspend);
		if (ret < 0)
			ts_err("Failed to select active pinstate, ret:%d", ret);
	}*/

	/* inform exteranl modules */
	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->after_suspend)
				continue;

			ret = ext_module->funcs->after_suspend(core_data, ext_module);
			if (ret == EVT_CANCEL_SUSPEND) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

out:
	ts_info("tp work status: %d", core_data->work_status);
#ifdef CONFIG_TOUCH_BOOST
	lpm_disable_for_dev(false, EVENT_INPUT);
#endif

#ifdef TOUCH_THP_SUPPORT
	if (core_data->enable_touch_raw) {
		if (core_data->fod_finger) {
			mutex_lock(&input_dev->mutex);
			core_data->fod_finger = false;
			input_report_key(input_dev, BTN_INFO, 0);
			input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(input_dev);
			/* _b583b-GX/T3：blob 该 THP 块（站点 A，0x103e0）update_fod(0)
			 * 无 fingerprint_authstate 门（blob 全 ko 无 authstate 串），
			 * 树原 if/else 两打印为 warsaw 残项 */
			update_fod_press_status_common(0);
			ts_info("ts fod up for suspend");
			mutex_unlock(&input_dev->mutex);
		}
		core_data->fod_finger = false;
	}
#endif

	goodix_ts_release_connects(core_data);

	/* _b573 scp 联动：blob 0x1062c-0x10680 = suspend 释指后把手势移交 SCP
	 * （防误触关时改为写 10diff 复位） */
	if (core_data->gesture_enabled) {
		if (goodix_scp_tp_mistouch_close) {
			goodix_gesture_10diff_write(core_data, 0);
		} else if (scp_tp_param.param0 >= 2) {
			ret = scp_tp_switch(1);
			if (ret)
				ts_err("scp_tp_switch fail, ret=%d", ret);	/* blob ts_suspend:2656 */
			/* _b583b-GX/T5：blob 0x1077c（ts_suspend:2660，E 级）在切换后
			 * param0<=1 时补打当前状态（\n 结尾，strcmp 工具盲区但 blob 有）
			 * ——blob 0x10684-0x10694 对成败两路均判 param0<=1 */
			if (scp_tp_param.param0 <= 1)
				ts_err("scp_tp_switch fail, scptp_cur_state=%d\n", scp_tp_param.param0);
		}
	}

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	goodix_ts_power_off(core_data);
#endif

	ts_info("Suspend end");
	mutex_unlock(&core_data->core_mutex);
	return 0;
}

static void goodix_reset_charge_state(struct goodix_ts_core *core_data, int state)
{
	//add_common_data_to_buf_common(0, SET_CUR_VALUE, THP_HAL_CHARGING_STATUS, 1, &state);
	if (state)
		core_data->hw_ops->charger_on(goodix_core_data, true);
	//else
	//	core_data->hw_ops->charger_on(goodix_core_data, false);
}

#if defined(TOUCH_THP_SUPPORT) && defined(TOUCH_DUMP_TIC_SUPPORT)
static void goodix_reset_ic_dump_state(struct goodix_ts_core *core_data)
{
	if (core_data->dump_type <= DUMP_OFF)
		return;

	if (!goodix_htc_enable_ic_dump(1))
		ts_debug("restore dump state as %d", core_data->dump_type);
}
#endif /* TOUCH_THP_SUPPORT */ /* TOUCH_DUMP_TIC_SUPPORT */

/**
 * goodix_ts_resume - Touchscreen resume function
 * Called by PM/FB/EARLYSUSPEN module to wakeup device
 */
static int goodix_ts_resume(struct goodix_ts_core *core_data)
{
	struct goodix_ext_module *ext_module = NULL;
	struct goodix_ext_module *next = NULL;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	int error = 0;
	int ret;
#ifdef TOUCH_TRUSTED_SUPPORT
	struct qts_data *qts_data = NULL;
	qts_data = get_qts_data_helper(&qts_vendor_data);

	qts_ts_resume(qts_data);
#endif // TOUCH_TRUSTED_SUPPORT

	if (core_data->init_stage < CORE_INIT_STAGE2 ||
			!atomic_read(&core_data->suspended)) {
		ts_err("already resumed!");
		return 0;
	}
	mutex_lock(&core_data->core_mutex);
	ts_info("Resume start, scptp_cur_state=%d", scp_tp_param.param0);
	/* _b573 scp 联动：blob 0xfef0-0xff28 = resume 时从 SCP 收回手势
	 * （param0∈{2,3} 且（param0==3 或未关防误触）） */
	if (scp_tp_param.param0 >= 2 &&
	    (scp_tp_param.param0 == 3 || !goodix_scp_tp_mistouch_close)) {
		ret = scp_tp_switch(0);
		if (ret)
			ts_info("scp_tp_switch fail, ret=%d", ret);
	}
	atomic_set(&core_data->suspended, 0);
	core_data->irq_trig_cnt = 0;

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	goodix_ts_power_on(core_data);
#endif

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->before_resume)
				continue;

			ret = ext_module->funcs->before_resume(core_data,
					ext_module);
			if (ret == EVT_CANCEL_RESUME) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	if (core_data->gesture_enabled || core_data->fod_finger) {
		gsx_gesture_before_resume(core_data);
		goto out;
	}

	if (core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_active);
		if (ret < 0)
			ts_err("Failed to select active pinstate, ret:%d", ret);
	}
	if(core_data->doze_test == 1) {
		error = goodix_ts_power_on(core_data);
		if (error < 0)
			ts_err("ERROR Failed to enable regulators\n");
	}

	/* reset device or power on*/
	if (hw_ops->resume)
		hw_ops->resume(core_data);

	//if (core_data->low_attn_status) {
	//	core_data->hw_ops->fod_attn(core_data, 3);
	//	ts_info("set low forever attn after reset");
	//} else {
	//	if (core_data->thp_fod_status == 0 && core_data->fod_finger== 0) {
	//		core_data->hw_ops->fod_attn(core_data, 5);
	//		ts_info("set low attn after reset");
	//	} else {
	//		core_data->hw_ops->fod_attn(core_data, 4);
	//		ts_info("set high attn after reset");
	//	}
	//}

	mutex_lock(&goodix_modules.mutex);
	if (!list_empty(&goodix_modules.head)) {
		list_for_each_entry_safe(ext_module, next,
					&goodix_modules.head, list) {
			if (!ext_module->funcs->after_resume)
				continue;

			ret = ext_module->funcs->after_resume(core_data,ext_module);
			if (ret == EVT_CANCEL_RESUME) {
				mutex_unlock(&goodix_modules.mutex);
				ts_info("Canceled by module:%s",
					ext_module->name);
				goto out;
			}
		}
	}
	mutex_unlock(&goodix_modules.mutex);

	out:
		core_data->work_status = TP_NORMAL;
		enable_temperature_detection_func(TOUCH_ID, true);
		/* #228（b581）blob goodix_resume_suspend 无 goodix_set_thermal_temp 调用
		 * （callcmp only-tree 项），按 blob 去掉 */
	
#if defined(TOUCH_THP_SUPPORT) && defined(TOUCH_DUMP_TIC_SUPPORT)
	goodix_reset_ic_dump_state(core_data);
#endif /* TOUCH_THP_SUPPORT */ /* TOUCH_DUMP_TIC_SUPPORT */

	/* enable palm sensor */
	if (core_data->palm_status) {
		ret = hw_ops->palm_on(core_data, core_data->palm_status);
	}
	/* check if cfg needs to be updated */
	if (core_data->need_update_cfg > 0) {
		ts_info("update cfg of state:%d, retry %d",
			core_data->cfg_cloud_state, core_data->need_update_cfg);
		core_data->need_update_cfg++;
		ret = goodix_update_cfg(core_data, core_data->cfg_cloud_state);
		if (!ret || core_data->need_update_cfg > UPDATE_CFG_MAX_NUM) {
			core_data->need_update_cfg = 0;
		}
	}
	/* enable irq */
	hw_ops->irq_enable(core_data, true);
	/* open esd */
	goodix_ts_blocking_notify(NOTIFY_RESUME, NULL);
	/* update ic_info */
	hw_ops->get_ic_info(core_data, &core_data->ic_info);
	goodix_reset_charge_state(core_data, core_data->charger_status);
	ts_info("Resume end");
	mutex_unlock(&core_data->core_mutex);
	return 0;
}

static int goodix_resume_suspend(bool is_resume, u8 gesture_type)
{
	if (is_resume) {
		return goodix_ts_resume(goodix_core_data);
	}
	return goodix_ts_suspend(goodix_core_data);
}

#ifdef CONFIG_FB
/**
 * goodix_ts_fb_notifier_callback - Framebuffer notifier callback
 * Called by kernel during framebuffer blanck/unblank phrase
 */
int goodix_ts_fb_notifier_callback(struct notifier_block *self,
	unsigned long event, void *data)
{
	struct goodix_ts_core *core_data =
		container_of(self, struct goodix_ts_core, fb_notifier);
	struct fb_event *fb_event = data;

	if (fb_event && fb_event->data && core_data) {
		if (event == FB_EARLY_EVENT_BLANK) {
			/* before fb blank */
		} else if (event == FB_EVENT_BLANK) {
			int *blank = fb_event->data;
			if (*blank == FB_BLANK_UNBLANK)
				goodix_ts_resume(core_data);
			else if (*blank == FB_BLANK_POWERDOWN)
				goodix_ts_suspend(core_data);
		}
	}

	return 0;
}
#endif


#ifdef CONFIG_PM
/**
 * goodix_ts_pm_suspend - PM suspend function
 * Called by kernel during system suspend phrase
 */
static int goodix_ts_pm_suspend(struct device *dev)
{
	struct goodix_ts_core *core_data =
		dev_get_drvdata(dev);

	ts_info("enter");

	if (device_may_wakeup(dev) && core_data->gesture_enabled) {
		enable_irq_wake(core_data->irq);
	}
	core_data->tp_pm_suspend = true;
	reinit_completion(&core_data->pm_resume_completion);
	return 0;
}
/**
 * goodix_ts_pm_resume - PM resume function
 * Called by kernel during system wakeup
 */
static int goodix_ts_pm_resume(struct device *dev)
{
	struct goodix_ts_core *core_data =
		dev_get_drvdata(dev);
	ts_info("enter");

	if (device_may_wakeup(dev) && core_data->gesture_enabled) {
		disable_irq_wake(core_data->irq);
	}
	core_data->tp_pm_suspend = false;
	complete(&core_data->pm_resume_completion);
	return 0;
}
#endif

/**
 * goodix_generic_noti_callback - generic notifier callback
 *  for goodix touch notification event.
 */
static int goodix_generic_noti_callback(struct notifier_block *self,
		unsigned long action, void *data)
{
	struct goodix_ts_core *cd = container_of(self,
			struct goodix_ts_core, ts_notifier);
	const struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	if (cd->init_stage < CORE_INIT_STAGE2)
		return 0;

	ts_info("notify event type 0x%x", (unsigned int)action);
	switch (action) {
	case NOTIFY_FWUPDATE_START:
		hw_ops->irq_enable(cd, 0);
		break;
	case NOTIFY_FWUPDATE_SUCCESS:
	case NOTIFY_FWUPDATE_FAILED:
		if (hw_ops->read_version(cd, &cd->fw_version))
			ts_info("failed read fw version info[ignore]");
		hw_ops->irq_enable(cd, 1);
		break;
	default:
		break;
	}
	return 0;
}

#ifdef TOUCH_THP_SUPPORT
static void goodix_set_fod_downup(struct goodix_ts_core *cd, int enable)
{
	if (enable) {
		ts_info("ts fod down");
		goodix_core_data->fod_finger = true;
		input_report_key(cd->input_dev, BTN_INFO, 1);
		update_fod_press_status_common(1);
		input_sync(cd->input_dev);
	} else {
		ts_info("ts fod up");
		goodix_core_data->fod_finger = false;
		input_report_key(cd->input_dev, BTN_INFO, 0);
		input_report_abs(cd->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(cd->input_dev, ABS_MT_WIDTH_MINOR, 0);
		update_fod_press_status_common(0);
		input_sync(cd->input_dev);
	}
}

static void goodix_thp_signal_work(struct work_struct *work)
{
	struct goodix_ts_core *cd = container_of(work, struct goodix_ts_core, thp_signal_work.work);

	if (!cd) {
		ts_err("core data not init");
		return;
	}
	if (!cd->enable_touch_raw) {
		ts_info("not enable touch raw");
		return;
	}
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	{
		int fod_en = 1;
		ts_info("notify fod enable to hal");
		add_common_data_to_buf_common(0, SET_CUR_VALUE, Touch_Fod_Enable, 1, &fod_en);
	}
#endif
}
#endif


static void goodix_self_check(struct work_struct *work)
{
	struct goodix_ts_core *cd =
			container_of(work, struct goodix_ts_core, self_check_work);
	u32 fw_state_addr = cd->ic_info.misc.fw_state_addr;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST | UPDATE_MODE_FORCE;
	u8 cur_cycle_cnt = 0;
	u8 pre_cycle_cnt = 0;
	int err_cnt = 0;
	int retry = 5;

	while (retry--) {
		cd->hw_ops->read(cd, fw_state_addr, &cur_cycle_cnt, 1);
		if (cur_cycle_cnt == pre_cycle_cnt)
			err_cnt++;
		pre_cycle_cnt = cur_cycle_cnt;
		msleep(20);
	}
	if (err_cnt > 1) {
		ts_err("Warning! The firmware maybe running abnormal, need upgrade.");
		goodix_get_config_proc(cd);
		goodix_do_fw_update(cd->ic_configs[CONFIG_TYPE_NORMAL], update_flag);
		cd->hw_ops->read_version(cd, &cd->fw_version);
		cd->hw_ops->get_ic_info(cd, &cd->ic_info);
	}
}


#ifdef TOUCH_TRUSTED_SUPPORT
struct spi_device *qts_spi = NULL;
void goodix_set_spi_device(struct spi_device *spi) {
	if (!spi) {
		ts_err("spi is NULL.");
	}
	qts_spi = spi;
}

static int goodix_enable_touch_irq(void *client_data, bool enable)
{
	struct goodix_ts_core *core_data = client_data;
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return core_data->hw_ops->irq_enable(core_data, enable);
}

static int goodix_get_irq_num(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return core_data->irq;
}

static int goodix_pre_la_tui_enable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	int ret = 0;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	goodix_htc_enable(0);

	ret = goodix_htc_set_active_scan_rate(0x01); /* 120Hz, must be after goodix_htc_enable, otherwise setting will fail */
	if (ret)
		ts_info("Failed to set report rate, ret:%d", ret);

	core_data->tui_process = true;
	reinit_completion(&core_data->tui_finish);
	return 0;
}

static int goodix_post_la_tui_disable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;
	int ret = 0;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	goodix_htc_enable(1);

	ret = goodix_htc_set_active_scan_rate(0x00); /* Restore default 240Hz */
	if (ret)
		ts_info("Failed to set report rate, ret:%d", ret);

	complete_all(&core_data->tui_finish);
	core_data->tui_process = false;
	return 0;
}

static int goodix_pre_la_tui_disable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return 0;
}

static int goodix_post_la_tui_enable(void *client_data)
{
	struct goodix_ts_core *core_data = client_data;

	ts_debug("enter");
	if (IS_ERR_OR_NULL(core_data))
		return -EINVAL;
	return 0;
}

static void goodix_fill_qts_vendor_data(struct qts_vendor_data *qts_vendor_data,
		struct goodix_ts_core *core_data)
{
	struct spi_device *spi = NULL;
	struct device_node *node = NULL;
	const char *touch_type;
	int rc = 0;

	ts_debug("enter");

	if (!qts_vendor_data || !qts_spi) {
		ts_err("Params is NULL.\n");
		return;
	}

	spi = qts_spi;
	node = spi->dev.of_node;

	if ((qts_touch_type != NULL && strlen(qts_touch_type) == 0) || (qts_touch_type == NULL)) {
		ts_err("No touch type, try again\n");
		rc = of_property_read_string(node, "goodix,touch-type", &touch_type);
		if (rc) {
			ts_err("No touch type\n");
			return;
		}
	} else {
		ts_err("touch type:%s\n", qts_touch_type);
		touch_type = qts_touch_type;
	}

	if (!strcmp(touch_type, "primary")) {
		ts_debug("primary");
		qts_vendor_data->client_type = QTS_CLIENT_PRIMARY_TOUCH;
	} else {
		ts_debug("secondary");
		qts_vendor_data->client_type = QTS_CLIENT_SECONDARY_TOUCH;
	}

	qts_vendor_data->client = NULL;
	qts_vendor_data->spi = spi;
	qts_vendor_data->bus_type = QTS_BUS_TYPE_SPI;

	qts_vendor_data->vendor_data = core_data;
	qts_vendor_data->qts_vendor_ops.enable_touch_irq = goodix_enable_touch_irq;
	qts_vendor_data->qts_vendor_ops.get_irq_num = goodix_get_irq_num;
	qts_vendor_data->qts_vendor_ops.pre_la_tui_enable = goodix_pre_la_tui_enable;
	qts_vendor_data->qts_vendor_ops.post_la_tui_enable = goodix_post_la_tui_enable;
	qts_vendor_data->qts_vendor_ops.pre_la_tui_disable = goodix_pre_la_tui_disable;
	qts_vendor_data->qts_vendor_ops.post_la_tui_disable = goodix_post_la_tui_disable;
}
#endif // TOUCH_TRUSTED_SUPPORT

int goodix_ts_stage2_init(struct goodix_ts_core *cd)
{
	int ret;

	/*init report mutex lock */
	mutex_init(&cd->report_mutex);
	/* alloc/config/register input device */
	ret = goodix_ts_input_dev_config(cd);
	if (ret < 0) {
		ts_err("failed set input device");
		return ret;
	}

	if (cd->board_data.pen_enable) {
		ret = goodix_ts_pen_dev_config(cd);
		if (ret < 0) {
			ts_err("failed set pen device");
			goto err_finger;
		}
	}
	/* request irq line */
	ret = goodix_ts_irq_setup(cd);
	if (ret < 0) {
		ts_info("failed set irq");
		goto exit;
	}
	ts_info("success register irq");

#ifdef CONFIG_FB
	cd->fb_notifier.notifier_call = goodix_ts_fb_notifier_callback;
	if (fb_register_client(&cd->fb_notifier))
		ts_err("Failed to register fb notifier client:%d", ret);
#endif

#ifdef TOUCH_THP_SUPPORT
	INIT_DELAYED_WORK(&cd->thp_signal_work, goodix_thp_signal_work);
#endif

	/* get ts lockdown info */
	goodix_ts_get_lockdown_info(cd);

	/* create sysfs files */
	goodix_ts_sysfs_init(cd);

	/* create procfs files */
	goodix_ts_procfs_init(cd);

	/* esd protector */
	goodix_ts_esd_init(cd);

	/* gesture init */
	gesture_module_init();

	/* inspect init */
	inspect_module_init(cd);

	/* Do self check on first boot */
	INIT_WORK(&cd->self_check_work, goodix_self_check);
	schedule_work(&cd->self_check_work);

#ifdef TOUCH_FOD_SUPPORT
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	set_touch_mode(Touch_Fod_Enable, 1);
#else
	set_touch_mode(Touch_Fod_Enable, 0);
#endif
#endif
	ts_info("finished");
	return 0;

exit:
	goodix_ts_pen_dev_remove(cd);
err_finger:
	goodix_ts_input_dev_remove(cd);
	return ret;
}

/* try send the config specified with type */
static int goodix_send_ic_config(struct goodix_ts_core *cd, int type)
{
	u32 config_id;
	struct goodix_ic_config *cfg;
	int res = 0;

	if (type >= GOODIX_MAX_CONFIG_GROUP) {
		ts_err("unsupproted config type %d", type);
		return -EINVAL;
	}

	cfg = cd->ic_configs[type];
	if (!cfg || cfg->len <= 0) {
		ts_info("no valid normal config found");
		return -EINVAL;
	}

	config_id = goodix_get_file_config_id(cfg->data);
	if (cd->ic_info.version.config_id == config_id) {
		ts_info("config id is equal 0x%x, skiped", config_id);
		return 0;
	}

	res = goodix_get_self_config_state(config_id, cd->ic_info.version.config_id);
	if (res != SELF_UNKNOWN && cd->need_update_cfg == 0) {
		ts_info("just update waterproof cfg (current: 0x%x) when power on, skipped",
			cd->ic_info.version.config_id);
		return 0;
	} else if (res == SELF_UNKNOWN && cd->need_update_cfg > 0) {
		ts_info("not the cfg (current: 0x%x) required for cloud control, skipped",
			cd->ic_info.version.config_id);
		return 0;
	}

	ts_info("try send config, id=0x%x", config_id);
	return cd->hw_ops->send_config(cd, cfg->data, cfg->len);
}

static int goodix_match_panel(struct goodix_ts_core *cd)
{
	char name_buf[GOODIX_MAX_STR_LABLE_LEN];

	snprintf(name_buf, GOODIX_MAX_STR_LABLE_LEN, "%s.bin", cd->board_data.cfg_bin);

	strncpy(cd->board_data.cfg_bin_name, name_buf, GOODIX_MAX_STR_LABLE_LEN);

	snprintf(name_buf, GOODIX_MAX_STR_LABLE_LEN, "%s.bin", cd->board_data.fw);
	strncpy(cd->board_data.fw_name, name_buf, GOODIX_MAX_STR_LABLE_LEN);

	strncpy(cd->board_data.limit_csv_name, TS_DEFAULT_LIMIT_CSV, GOODIX_MAX_STR_LABLE_LEN);

	return 0;
}

static int goodix_lockdown_info_read(u8 *lockdown_info_buf)
{
	if (!goodix_core_data)
		return -1;

	for (int i = 0; i < GOODIX_LOCKDOWN_SIZE; i++)
		lockdown_info_buf[i] = goodix_core_data->lockdown_info[i];

	return 0;
}

static int goodix_fw_version_info_read(char *buf)
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	struct goodix_fw_version chip_ver;
	int ret = 0;
	int cnt = 0;

	if (!hw_ops)
		return -1;
	if (hw_ops->read_version) {
		ret = hw_ops->read_version(goodix_core_data, &chip_ver);
		if (!ret) {
			cnt = snprintf(&buf[cnt], 64 - cnt,
					"patch_pid:%s\n",
					chip_ver.patch_pid);
			cnt += snprintf(&buf[cnt], 64 - cnt,
					"patch_vid:%02x%02x%02x%02x\n",
					chip_ver.patch_vid[0], chip_ver.patch_vid[1],
					chip_ver.patch_vid[2], chip_ver.patch_vid[3]);
		} else {
			ts_err("read fw version failed, cnt value: %d", cnt);
		}
	}

	if (hw_ops->get_ic_info) {
		ret = hw_ops->get_ic_info(goodix_core_data, &goodix_core_data->ic_info);
		if (!ret) {
			cnt += snprintf(&buf[cnt], 64 - cnt,
					"config_version:%x\n", goodix_core_data->ic_info.version.config_version);
		} else {
			ts_err("get ic info failed, cnt value: %d", cnt);
		}
	}
	return 0;
}

static int goodix_get_limit_csv_version(char *buf)
{
	/* _b583b-GX：blob 无 'get_limit_csv_file_version failed' 串，删打印 */
	get_limit_csv_file_version(goodix_core_data, buf);
	return 0;
}

// static int goodix_short_open_test(void)
// {
// 	struct ts_rawdata_info *info = NULL;
// 	int test_result;

// 	info = vzalloc(sizeof(*info));
// 	if (!info) {
// 		ts_err("Failed to alloc rawdata info memory");
// 		return GTP_RESULT_INVALID;
// 	}

// 	if (goodix_get_rawdata(&goodix_core_data->pdev->dev, info)) {
// 		ts_err("Factory_test FAIL");
// 		test_result = GTP_RESULT_INVALID;
// 		goto exit;
// 	}

// 	if (80 == (*(info->result + 1))) {
// 		ts_info("test PASS!");
// 		test_result = GTP_RESULT_PASS;
// 	} else {
// 		ts_err("test FAILED!");
// 		test_result = GTP_RESULT_FAIL;
// 	}

// exit:
// 	ts_info("resultInfo: %s", info->result);
// 	/* ret = snprintf(buf, PAGE_SIZE, "resultInfo: %s", info->result); */

// 	vfree(info);
// 	return test_result;
// }

//BEGIN: fix echo open/short > proc/tp_selftest can not open csv
static bool ic_self_test_flag = false;
bool goodix_get_ic_self_test_mode(void)
{
	/* #228（b581）blob goodix_get_ic_self_test_mode @0xa4d0：读 .bss+0x1b11
	 * 后 ts_debug("enter ic_self_test_flag %d")（厂商源行 3228，level D 门限
	 * debug>=4），树侧原缺此打印 */
	ts_debug("enter ic_self_test_flag %d", ic_self_test_flag);
	return ic_self_test_flag;
}
//END:fix echo open/short > proc/tp_selftest can not open csv

/* #228（b581）blob goodix_short_open_test 为独立源函数（内联进 ic_self_test 后
 * __func__ 串 'goodix_short_open_test' 仍保留在 blob .rodata —— 树侧保持独立
 * static 函数（clang 单调用点自动内联，符号不落 .o，串随之保留），勿并入 */
static int goodix_short_open_test(struct goodix_ts_core *cd)
{
	struct ts_rawdata_info *info;
	int ret = 0;

	info = vzalloc(sizeof(*info));
	if (!info) {
		ts_err("alloc rawdata info memory failed");	/* 厂商源行 3198 */
		return ret;
	}

	if (goodix_get_rawdata(cd->bus->dev, info)) {
		ts_err("Factory_test FAIL");			/* 厂商源行 3203 */
		vfree(info);
		return ret;
	}

	if (info->result[1] == 'P') {	/* blob 0xf3b4: ldrb [info+0x36b5] == 0x50 */
		ts_info("test PASS!");				/* 厂商源行 3209 */
		ret = GTP_RESULT_PASS;
	} else {
		ts_err("test FAILED!");				/* 厂商源行 3212 */
		ret = GTP_RESULT_FAIL;
	}
	ts_info("resultInfo: %s", info->result);		/* 厂商源行 3217 */
	vfree(info);

	return ret;
}

/* _b583b-GX：blob goodix_ic_self_test（0xf26c，848B）主体与树 b581 版同形，
 * 差异点：① !goodix_core_data 时置 *result=0（blob 0xf2a0→0xf47c）；
 * ② chip_ver 清零（0xf2c4-0xf2cc）；③ resultInfo 打印门 I 级（0xf578）；
 * ④ miev 上报以 !pass 门控（0xf58c，与 retval!=PASS 等价） */
static int goodix_ic_self_test(char *type, int *result)
{
	struct goodix_fw_version chip_ver = {0};
	struct goodix_ts_hw_ops *hw_ops;
	int retval = 0;

	if (!goodix_core_data) {
		*result = GTP_RESULT_INVALID;
		return 0;
	}
	hw_ops = goodix_core_data->hw_ops;

	ic_self_test_flag = true;
	ts_debug("enter ic_self_test_flag %d", ic_self_test_flag);

	if (!strncmp("short", type, 5) || !strncmp("open", type, 4)) {
		/* blob 0xf328 起 goodix_short_open_test 内联体 */
		retval = goodix_short_open_test(goodix_core_data);
#if IS_ENABLED(CONFIG_MIEV)
		if (retval != GTP_RESULT_PASS) {
			if (!strncmp("open", type, 4))
				xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_OPENTEST_FAIL, 0, "TpOpenTestFail", "goodix", retval);
			if (!strncmp("short", type, 5))
				xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_SHORTTEST_FAIL, 0, "TpShortTestFail", "goodix", retval);
		}
#endif
	} else if (!strncmp("i2c", type, 3)) {
		hw_ops->read_version(goodix_core_data, &chip_ver);
		if (chip_ver.sensor_id == 255)
			retval = GTP_RESULT_PASS;
		else
			retval = GTP_RESULT_FAIL;
	}
	ic_self_test_flag = false;
	ts_debug("exit ic_self_test_flag %d", ic_self_test_flag);

	*result = retval;

	return 0;
}

int goodix_ts_get_lockdown_info(struct goodix_ts_core *cd)
{
	int ret = 0;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	ret = hw_ops->read(cd, TS_LOCKDOWN_REG,
				cd->lockdown_info, GOODIX_LOCKDOWN_SIZE);
	if (ret) {
		ts_err("can't get lockdown");
		return -EINVAL;
	}

	ts_info("lockdown is:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x",
			cd->lockdown_info[0], cd->lockdown_info[1],
			cd->lockdown_info[2], cd->lockdown_info[3],
			cd->lockdown_info[4], cd->lockdown_info[5],
			cd->lockdown_info[6], cd->lockdown_info[7]);
	return 0;
}

#ifdef TOUCH_FOD_SUPPORT
static void  goodix_xiaomi_touch_fod_test(int value)
{
	struct input_dev *input_dev = goodix_core_data->input_dev;
	/* _b583b-GX/T4：blob goodix_xiaomi_touch_fod_test（0x11ab0，380B）无入口打印
	 * （calldiff only-tree _printk:1；'fod_test enter' 串 blob 无），删 */
	if (value) {
		input_report_key(input_dev, BTN_INFO, 1);
		update_fod_press_status_common(1);
		input_sync(input_dev);
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_key(input_dev, BTN_TOOL_FINGER, 1);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, 60900);
		input_report_abs(input_dev, ABS_MT_POSITION_Y, 243700);
		input_sync(input_dev);
	} else {
		input_mt_slot(input_dev, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, -1);
		input_report_key(input_dev, BTN_INFO, 0);
		update_fod_press_status_common(0);
		input_sync(input_dev);
	}
}

#if 0
#define GOODIX_FOD_ATTN 0xD2
static void goodix_xiaomi_touch_fod_attn(int value)
{
	ts_info("enter fod_attn");

	if (value == 0) {
		/* force low attn */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 0);
		ts_info("force low attn");
	} else if (value == 1) {
		/* force high attn */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 1);
		ts_info("force high attn");
	} else if (value == 2) {
		/* low attn for no fod, high attn for fod press */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 2);
		ts_info("low attn for no fod, high attn for fod press");
	} else if (value == 3) {
		/* low attn forever */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 3);
		ts_info("low attn forever");
	} else if (value == 4) {
		/* high attn for press in fod when screen on */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 4);
		ts_info("high attn for press in fod");
	} else if (value == 5) {
		/* low attn for no press in fod when screen on */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 5);
		ts_info("low attn for no press in fod");
	} 
	else {
		/* error */
		ts_err("invalid value = %d\n", value);
	}
}

static void goodix_xiaomi_touch_fod_low_attn(int value)
{
	ts_info("enter fod_low_attn");

	if (value == 0) {
		/* low attn forever */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 3);
		goodix_core_data->low_attn_status = 1;
		ts_info("enter low attn forever, low_attn_status = 1");
	} else if (value == 1) {
		/* low attn forever exit */
		goodix_core_data->hw_ops->fod_attn(goodix_core_data, 2);
		goodix_core_data->low_attn_status = 0;
		ts_info("low attn forever exit, low_attn_status = 0");
	} else {
		/* error */
		ts_err("invalid value = %d\n", value);
	}
}
#endif
#endif

#ifdef GOODIX_XIAOMI_TOUCHFEATURE
static hardware_operation_t hardware_operation;
static hardware_param_t hardware_param;

static void goodix_sleep_to_gesture(struct goodix_ts_core *cd)
{
	int ret;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;
	ts_info("ic is in sleep already, need to reset");
	hw_ops->reset(cd, GOODIX_NORMAL_RESET_DELAY_MS);
	cd->work_status = TP_GESTURE;
	ret = hw_ops->gesture(cd, cd->gesture_enabled);
	if (ret)
		ts_err("failed enter gesture mode");
	else
		ts_info("enter gesture mode");

	hw_ops->irq_enable(cd, true);
	enable_irq_wake(cd->irq);
}

void goodix_ic_switch_mode(u8 _gesture_type)
{
	struct goodix_ts_core *core_data = goodix_core_data;
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	int gesture_type = 0;
	int ret;

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	/* _b573 scp 联动：blob 0xa654-0xa684 = SCP 托管手势（param0==3）时把手势类型同步给
	 * SCP（ipi cmd 5）；Nonui 模式当前值作第 3 参一并下发 */
	if (scp_tp_param.param0 == 3) {
		ts_info("[scp-tp]: goodix_ic_switch_mode, cur_gesture=0x%x", gesture_type);
		scp_tp_ipi_send(5, gesture_type,
				driver_get_touch_mode_common(TOUCH_ID, Touch_Nonui_Mode), 0);
	}

	pm_stay_awake(core_data->bus->dev);
	if (core_data->tp_pm_suspend) {
		ts_info("device in suspend, wait to resume");
		ret = wait_for_completion_timeout(&core_data->pm_resume_completion, msecs_to_jiffies(150));
		if (!ret) {
			pm_relax(core_data->bus->dev);
			ts_err("system can't finished resuming procedure");
			return;
		}
	}
	ts_debug("fod is 0x%x", driver_get_touch_mode_common(TOUCH_ID, Touch_Fod_Enable));
	ts_debug("gesture enabled is 0x%x", core_data->gesture_enabled);
	mutex_lock(&core_data->core_mutex);
	if (_gesture_type & GESTURE_SINGLETAP_EVENT)
		gesture_type |= SINGLE_TAP_EN;
	if (_gesture_type & GESTURE_DOUBLETAP_EVENT)
		gesture_type |= DOUBLE_TAP_EN;
	if (_gesture_type & GESTURE_LONGPRESS_EVENT)
		gesture_type |= FOD_EN;

	if (core_data->gesture_enabled != gesture_type) {
		ts_info("gesture enable changed from 0x%x to 0x%x", core_data->gesture_enabled, gesture_type);
		core_data->gesture_enabled = gesture_type;

		/* _b573 scp 联动：blob 0xa6e8-0xa71c = SCP 托管（param0==3）时跳过 IC 手势命令 */
		if (scp_tp_param.param0 == 3) {
			ts_info("scp working don't process!");
			goto out;
		}

		if (0 == atomic_read(&core_data->suspended)) {
			ts_debug("tp is in resume state, wait suspend to send cmd!");
			goto out;
		}

		if (core_data->fod_finger) {
			ts_info("fod has already pressed!");
			goto out;
		}

		if (core_data->gesture_enabled && core_data->work_status == TP_SLEEP) {
			goodix_sleep_to_gesture(core_data);
			/* _b573 scp 联动：blob 0xa81c-0xa84c = sleep_to_gesture 后未关防误触且
			 * param0∈{2,4} 时切 scp 手势通道 */
			if (!goodix_scp_tp_mistouch_close &&
			    (scp_tp_param.param0 == 2 || scp_tp_param.param0 == 4)) {
				ts_info("sleep_to_gesture, switch to scp");
				scp_tp_switch(1);
			}
		} else {
			hw_ops->gesture(core_data, core_data->gesture_enabled);
		}
	}
out:
	pm_relax(core_data->bus->dev);
	mutex_unlock(&core_data->core_mutex);
	return;
}

void goodix_game_mode_update(long mode_update_flag, int mode_value[Touch_Mode_NUM])
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	u8 data0 = 0;
	u8 data1 = 0;
	bool on = false;
	u8 temp_value = 0;
	int ret = 0;
	int i = 0;
	static bool expert_mode = false;

	if (goodix_core_data->work_status == TP_SLEEP) {
		ts_warn("suspended, skip");
		return;
	}

	if (!mode_update_flag) {
		ts_info("no need update mode value");
		return;
	}

#ifdef TOUCH_THP_SUPPORT
	if (goodix_core_data->enable_touch_raw) {
		// not for P11,不用先使能游戏模式再切报点率
		// mutex_lock(&goodix_core_data->core_mutex);
		// temp_value = driver_get_touch_mode_common(TOUCH_ID, Touch_Game_Mode);
		// ts_info("game mode status: %s   %d", temp_value ? "ON" : "OFF", temp_value);
	
		// ret = goodix_htc_set_game_mode(temp_value);
		// if (ret < 0) {
		// 	ts_info("failed to send game mode: %d, ret=%d", temp_value, ret);
		// }
		// mutex_unlock(&goodix_core_data->core_mutex);
		return;
	}
#endif


	mutex_lock(&goodix_core_data->core_mutex);

	expert_mode = mode_update_flag & (1 << Touch_Expert_Mode);

	for (i = 0; i <= Touch_Panel_Orientation; i++) {
		switch (i) {
		case Touch_Game_Mode:
			temp_value = mode_value[Touch_Game_Mode];
			on = !!temp_value;
			break;
		case Touch_Active_MODE:
			break;
		case Touch_UP_THRESHOLD:
			temp_value = mode_value[Touch_UP_THRESHOLD];
			data0 &= 0xF8;
			data0 |= temp_value;
			break;
		case Touch_Tolerance:
			temp_value = mode_value[Touch_Tolerance];
			data0 &= 0xC7;
			data0 |= (temp_value << 3);
			break;
		case Touch_Panel_Orientation:
			temp_value = mode_value[Touch_Panel_Orientation];
			if (PANEL_ORIENTATION_DEGREE_90 == temp_value)
				temp_value = 1;
			else if (PANEL_ORIENTATION_DEGREE_270 == temp_value)
				temp_value = 2;
			else
				temp_value = 0;
			data0 &= 0x3F;
			data0 |= (temp_value << 6);
			break;
		case Touch_Aim_Sensitivity:
			temp_value = mode_value[Touch_Aim_Sensitivity];
			data1 &= 0xC7;
			data1 |= (temp_value << 3);
			break;
		case Touch_Tap_Stability:
			temp_value = mode_value[Touch_Tap_Stability];
			data1 &= 0xF8;
			data1 |= temp_value;
			break;
		case Touch_Edge_Filter:
			temp_value = mode_value[Touch_Edge_Filter];
			data1 &= 0x3F;
			data1 |= (temp_value << 6);
			break;
		case Touch_Expert_Mode:
			temp_value = mode_value[Touch_Expert_Mode];
			temp_value = temp_value - 1;
			if (expert_mode) {
				data0 &= 0xF8;
				data0 |= (u8)goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 1];
				data0 &= 0xC7;
				data0 |= (u8)(goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN] << 3);
				data1 &= 0xC7;
				data1 |= (u8)(goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 2] << 3);
				data1 &= 0xF8;
				data1 |= (u8)goodix_core_data->board_data.touch_expert_array[temp_value * GAME_ARRAY_LEN + 3];
			}
			break;
		default:
			ts_err("not support mode, mode(%d)", i);
			break;
		};
	}

	ret = hw_ops->game(goodix_core_data, data0, data1, !!on);

	if (ret < 0) {
		ts_err("send game mode fail");
	}
	mutex_unlock(&goodix_core_data->core_mutex);
	return;
}

static int goodix_update_cfg(struct goodix_ts_core *cd, bool enable) {
	int ret;
	char name_buf[GOODIX_MAX_STR_LABLE_LEN];

	if (enable) { //WithSelf
		snprintf(name_buf, GOODIX_MAX_STR_LABLE_LEN, "%s.bin", cd->board_data.cfg_bin);
	} else {
		snprintf(name_buf, GOODIX_MAX_STR_LABLE_LEN, "%s_SelfDis.bin", cd->board_data.cfg_bin);
	}
	strncpy(cd->board_data.cfg_bin_name, name_buf, GOODIX_MAX_STR_LABLE_LEN);
	ts_info("config name from: %s", cd->board_data.cfg_bin_name);

	ret = goodix_get_config_proc(cd);
	if (ret) {
		ts_err("no valid ic config found");
		return ret;
	} else {
		ts_debug("get valid ic config successfully");
	}

	ret = goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
	if (ret) {
		ts_err("send ic config fail");
	} else {
		ts_debug("send ic config successfully");
	}

	return ret;
}

static void goodix_set_cur_value(int mode, int *value)
{
	int gtp_mode = mode;
	int gtp_value = value[0];
	bool flag = false;

	/* _b583b-GX：blob 0xfad4 只此一处 mode/value 打印（I 级，行 3589）；
	 * 树原第二条（suspended 检查后）与 FACTORY cmd_flag 门 blob 均无 */
	ts_info("mode:%d, value:%d", gtp_mode, gtp_value);

	if (!goodix_core_data || goodix_core_data->init_stage != CORE_INIT_STAGE2) {
		ts_err("initialization not completed, return");
		return;
	}

	flag = goodix_get_ic_self_test_mode();
	if (flag) {
		ts_info("tp is in open/short test");
		return;
	}

#ifdef TOUCH_TRUSTED_SUPPORT
	if (goodix_core_data->tui_process) {
		if (wait_for_completion_interruptible(&goodix_core_data->tui_finish) ) {
			ts_err("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		ts_info("wait finished, its time to go ahead");
	}
#endif // TOUCH_TRUSTED_SUPPORT

	if (gtp_value < 0) {
		ts_err("invalid mode value");
		return;
	}
	/* _b583b-GX/T6 裁决：blob 0xf9bc-0xfb3c 全函数无 suspended 提前返回
	 * （suspended 期照发 SPI 写，等价性判据见 evidence_b583b.txt），删除之 */
	switch(gtp_mode) {
		case TOUCH_CLOUD_MODE_WATER_PROOF:
			goodix_core_data->need_update_cfg = 1;
			goodix_core_data->cfg_cloud_state = !!gtp_value;
			break;
		case TOUCH_CLOUD_MODE_CLOSE_SCP_TP_MISTOUCH:
			/* _b583b-GX：blob 0xfbd4-0xfbec + 打印 0xfd64（I 级行 3627）：
			 * csel(open/close by value==0) 后写 mistouch_close=(value==0)。
			 * 树原缺 case（blob 跳表 103 → 专用块） */
			ts_info("%s SCP_TP_MISTOUCH", gtp_value ? "close" : "open");
			goodix_scp_tp_mistouch_close = (gtp_value == 0);
			break;
		case Touch_Boost_EN: {
			/* _b583-GX：blob 0xfb58-0xfb7c（跳转表 mode 200 → 块 0xfb58）=
			 * ts_info("notify hal to boost")（L3635）+ add_common_data_to_buf_common(
			 * 0, SET_CUR_VALUE, 10001, 1, &out)（w2=0x2711，out=-1 见 0xfa00）；
			 * 树原为 warsaw 死面打印（该串 blob 无） */
			int boost_out = -1;

			ts_info("notify hal to boost");
			add_common_data_to_buf_common(TOUCH_ID, SET_CUR_VALUE, DATA_MODE_178,
					1, &boost_out);
			break;
		}
		case Touch_Empty_Int:
			goodix_htc_enable_empty_int(!!gtp_value);
			break;
		case Touch_Super_Report:
			brl_switch_report_rate(goodix_core_data, !!gtp_value);
			break;
		case THP_LOCK_SCAN_MODE:
			ts_info("THP enable doze mode [%d]", gtp_value);	/* _b583-GX：blob L3645（0xfd98） */
			goodix_htc_enter_idle(value);
			break;
		case THP_FOD_DOWNUP_CTL:
			if (goodix_core_data->enable_touch_raw)
				goodix_set_fod_downup(goodix_core_data, gtp_value);
			break;
		case THP_HAL_INIT_READY:
			ts_info("hal init ready.");
			schedule_delayed_work(&goodix_core_data->thp_signal_work, 1 * HZ);
			break;
		case THP_HAL_REPORT_RATE:
			ts_info("set report rate: %d", gtp_value);
			goodix_core_data->report_rate = gtp_value;
			goodix_htc_set_active_scan_rate(gtp_value);
			break;
		case THP_NORMALIZE_STUDY_SCAN:
			ts_info("B array is not reasonable, need scan freq...");	/* _b583-GX：blob L3662（0xfdfc） */
			goodix_htc_start_calibration();
			break;
		case THP_NORMALIZE_B_REQUEST:
			ts_info("THP request B array");	/* _b583-GX：blob L3666（0xfe18） */
			goodix_htc_enable_b_array();
			break;
		case THP_IDLE_BASALINE_UPDATE:
			ts_debug("THP update idle baseline");	/* _b583-GX：blob L3678（0xfe34，D 级） */
			goodix_htc_update_idle_baseline();
			break;
#if defined(TOUCH_THP_SUPPORT) && defined(TOUCH_DUMP_TIC_SUPPORT)
		case Touch_THP_Dump:
			/* _b583b-GX：blob 0xfcac-0xfce0（mode 1076）：value>1 落
			 * default；enable_ic_dump 失败不改 dump_type。树同形 */
			if ((gtp_value == DUMP_OFF || gtp_value == DUMP_ON) && goodix_core_data->dump_type != gtp_value) {
				if (!goodix_htc_enable_ic_dump(gtp_value)) {
					ts_debug("change dump state(%d) as %d",
						goodix_core_data->dump_type, gtp_value);
					goodix_core_data->dump_type = gtp_value;
				}
			}
			break;
#endif //TOUCH_DUMP_TIC_SUPPORT
		case THP_IDLE_THD:
			goodix_htc_set_idle_threshold(gtp_value);
			break;
		case THP_GLOVE_STATUS:
			goodix_htc_enter_glove(value);
			break;
		case THP_IC_FREQ_HOPPING:
			goodix_htc_set_freq_hopping(gtp_value);
			break;
		case THP_IC_SOFT_RESET:
			goodix_htc_set_soft_reset(gtp_value);
			break;
		case THP_IC_DOUBLE_SCAN:
			goodix_htc_set_double_scan(gtp_value);
			break;
		case THP_IC_NORMALIZE_STUDY:
			goodix_htc_set_normalize_study(value);
			break;
		case THP_IC_GESTRUE_FEEDBACK:
			goodix_htc_set_gesture_feedback(gtp_value);
			break;
		default:
			/* _b583b-GX：blob 0xfbf4-0xfc04 = ts_warn（W 级、无 mode 实参，
			 * 跳表 1000-1103 之外与其内 1002/1003/1005-1010/1012-1015 全落此；
			 * 1012=DISPLAY_FPS/1105=IDLE_RATE/1106=REF_HOPPING 亦然）。
			 * 树原 ts_err("not support mode, mode(%d)!") 为 warsaw 残项 */
			ts_warn("not support mode!");
			break;
	}
}

static u8 goodix_panel_color_read(void)
{
	/* _b583b-GX：blob 0x10d84 = 28B 纯转发（cbz 判空 + ldrb [cd+0x612]），
	 * 无任何打印；树原 'core data is null'/'read info is %c' 为 warsaw 残项 */
	if (!goodix_core_data)
		return 0;
	return goodix_core_data->lockdown_info[2];
}

static u8 goodix_panel_vendor_read(void)
{
	/* _b583b-GX：blob 0x10d64 同上（ldrb [cd+0x610]） */
	if (!goodix_core_data)
		return 0;
	return goodix_core_data->lockdown_info[0];
}

static u8 goodix_panel_display_read(void)
{
	/* _b583b-GX：blob 0x10da4 同上（ldrb [cd+0x611]） */
	if (!goodix_core_data)
		return 0;
	return goodix_core_data->lockdown_info[1];
}

static char goodix_touch_vendor_read(void)
{
	/* _b583b-GX：blob 0x10dc4 = 8B（mov w0,#0x32; ret），无打印 */
	return '2';
}

static int goodix_palm_sensor_write(int value)
{
	struct goodix_ts_hw_ops *hw_ops = goodix_core_data->hw_ops;
	int ret = 0;

	ts_info("palm sensor value : %d", value);
	if (!goodix_core_data) {
		ts_err("goodix core data os NULL");
		return -EINVAL;
	}

	goodix_core_data->palm_status = value;
	if (goodix_core_data->work_status == TP_NORMAL)
		ret = hw_ops->palm_on(goodix_core_data, !!value);

	return ret;
}

static void goodix_set_charge_state(int state)
{
	if (!goodix_core_data || goodix_core_data->init_stage < CORE_INIT_STAGE2) {
		ts_err("%s not inited", __func__);
		return;
	} else {
		ts_info("%s state:%d", __func__, state);
	}

#ifdef CONFIG_TOUCH_FACTORY_BUILD
	ts_info("cmd_flag:%d", cmd_flag);
	if (!cmd_flag) {
		ts_err("tp self test in progress, reject charge state:%d", state);
		return;
	}
#endif

	goodix_core_data->charger_status = state;
	if (atomic_read(&goodix_core_data->suspended))
		return;
	if (state)
		goodix_core_data->hw_ops->charger_on(goodix_core_data, true);
	else
		goodix_core_data->hw_ops->charger_on(goodix_core_data, false);
}

static int goodix_log_level_control(int value)
{
	if (!goodix_core_data || goodix_core_data->init_stage != CORE_INIT_STAGE2) {
		ts_err("initialization not completed, return");
		return -1;
	}
	ts_info("debug level: %d", value);
	debug_log_level = value;
	/* _b573 scp 联动：blob 0x118b4-0x118c8 = !mistouch_close 时同步日志等级给 SCP */
	if (!goodix_scp_tp_mistouch_close)
		scp_tp_ipi_send(6, value, 0, 0);
	ts_info("scp set log level = %d", debug_log_level);
	return value;
}

/* #228（b581）ops 槽 set_thermal_temp 收口：blob .text+0x11c30
 * goodix_set_thermal_temp 全函数体仅 8 字节 ——
 *   11c30: mov w0, wzr
 *   11c34: ret
 * 即 stock（rodin 6.6）该回调为**空实现**：不发 GOODIX_TEMPCMD、不取 bms 温度、
 * 不上报 THP_TEMPERATURE_STATUS；且 blob 全模块无 "temp: %d"/"failed send temp cmd"
 * 串（strcmp：both only-tree），blob goodix_resume_suspend 亦无对本函数的调用。
 * 原树 warsaw 变体的实现（get_bms_temp_common + add_common_data_to_buf_common +
 * hw_ops->send_cmd(GOODIX_TEMPCMD)）会让 IC 收到 stock 未发的温度命令，
 * 按 blob 收口为空实现（该槽位仍按 blob 挂 goodix_set_thermal_temp，见 ops 表）。 */
static int goodix_set_thermal_temp(int temp, bool force)
{
	return 0;
}

#endif

#ifdef GOODIX_DEBUGFS_ENABLE
/* _b583-GX：blob tpdbg_suspend（LOCAL FUNC @0x1204c, 100B）单形参 = enable；
 * 0x12090-0x120a8 = ts_info("enter schedule_resume_suspend_work_common  enable %d", enable)
 * （I 级，源行 3839；__func__ = "tpdbg_suspend"），0x12070-0x1207c =
 * schedule_resume_suspend_work_common(0, !enable)。树原为 2 形参且无打印（被内联掉）。 */
static void tpdbg_suspend(bool enable)
{
	ts_info("enter schedule_resume_suspend_work_common  enable %d", enable);
	schedule_resume_suspend_work_common(TOUCH_ID, !enable);
}

static int tpdbg_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t tpdbg_read(struct file *file, char __user *buf, size_t size,
		loff_t *ppos)
{
	const char *str = "cmd support as below:\n \
			\necho \"irq-disable\" or \"irq-enable\" to ctrl irq\n \
			\necho \"tp-suspend-en\" or \"tp-suspend-off\" to ctrl panel in or off suspend status\n \
			\necho \"tp-sd-en\" or \"tp-sd-off\" to ctrl panel in or off sleep status\n";

	loff_t pos = *ppos;
	int len = strlen(str);

	if (pos < 0)
		return -EINVAL;
	if (pos >= len)
		return 0;

	if (copy_to_user(buf, str, len))
		return -EFAULT;

	*ppos = pos + len;

	return len;
}

static ssize_t tpdbg_write(struct file *file, const char __user *buf,
		size_t size, loff_t *ppos)
{
	struct goodix_ts_core *core_data = file->private_data;
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	char *cmd = kzalloc(size + 1, GFP_KERNEL);
	int ret = size;

	if (!cmd)
		return -ENOMEM;

	if (core_data->init_stage < CORE_INIT_STAGE2) {
		ts_err("initialization not completed");
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(cmd, buf, size)) {
		ret = -EFAULT;
		goto out;
	}

	cmd[size] = '\0';

	if (!strncmp(cmd, "irq-disable", 11))
		hw_ops->irq_enable(core_data, false);
	else if (!strncmp(cmd, "irq-enable", 10))
		hw_ops->irq_enable(core_data, true);
	else if (!strncmp(cmd, "tp-sd-en", 8))
		tpdbg_suspend(true);
	else if (!strncmp(cmd, "tp-sd-off", 9))
		tpdbg_suspend(false);
	else if (!strncmp(cmd, "tp-suspend-en", 13))
		tpdbg_suspend(true);
	else if (!strncmp(cmd, "tp-suspend-off", 14))
		tpdbg_suspend(false);
out:
	kfree(cmd);

	return ret;
}

static int tpdbg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

#if IS_ENABLED(CONFIG_MIEV)
static void goodix_touch_dfs_test(int value){

	switch(value){
		case TOUCH_EVENT_TRANSFER_ERR:
			xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_TRANSFER_ERR, 0, "TpTransferErr", "goodix");
			break;
		case TOUCH_EVENT_FWLOAD_ERR:
			xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_FWLOAD_ERR, 0, "TpFirmwareLoadFail", "goodix");
			break;
		case TOUCH_EVENT_PARAM_ERR:
			xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0, "TpParamParseFail", "goodix", ERROR_GPIO_REQUEST);
			break;
		case TOUCH_EVENT_OPENTEST_FAIL:
			xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_OPENTEST_FAIL, 0, "TpOpenTestFail", "goodix", 0);
			break;
		case TOUCH_EVENT_SHORTTEST_FAIL:
			xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_SHORTTEST_FAIL, 0, "TpShortTestFail", "goodix", 0);
			break;
		default:
			ts_err("don't support touch dfs test");
			break;
	}
}
#endif

static const struct file_operations tpdbg_operations = {
	.owner = THIS_MODULE,
	.open = tpdbg_open,
	.read = tpdbg_read,
	.write = tpdbg_write,
	.release = tpdbg_release,
};
#endif

/**
 * goodix_start_later_init - init IC fw and config
 * @data: point to goodix_ts_core
 *
 * This function respond for get fw version and try upgrade fw and config.
 * Note: when init encounter error, need release all resource allocated here.
 */
static int goodix_start_later_init(struct goodix_ts_core *ts_core)
{
	int ret, i;
	int update_flag = UPDATE_MODE_BLOCK | UPDATE_MODE_SRC_REQUEST;
	struct goodix_ts_board_data *ts_bdata = board_data(ts_core);
	struct goodix_ts_core *cd = ts_core;
	struct goodix_ts_hw_ops *hw_ops = cd->hw_ops;

	/* step 1: read version */
	// return 0;
	ret = cd->hw_ops->read_version(cd, &cd->fw_version);
	if (ret < 0) {
		ts_err("failed to get version info, try to upgrade");
		update_flag |= UPDATE_MODE_FORCE;
		goto upgrade;
	}
	/* _b583b-GX：blob 无 'get version info!!!!!!' 串（strcmp only-tree），删 */
	/* get ts lockdown info */
	goodix_ts_get_lockdown_info(cd);
	goodix_match_panel(cd);

	/* setp 2: get config data from config bin */
	ret = goodix_get_config_proc(cd);
	if (ret)
		ts_info("no valid ic config found");
	else
		ts_info("success get valid ic config");

	upgrade:
		/* #228（b581）blob 厂商源行 4003（goodix_start_later_init，升级段起手）：
		 * 先取一次 ic info，失败即打 "failed to get ic info, try to upgrade"
		 * 并置 FORCE 升级位（仅 blob 有该打印，callcmp only-blob _printk:1） */
		if (cd->hw_ops->get_ic_info(cd, &cd->ic_info) < 0) {
			ts_err("failed to get ic info, try to upgrade");
			update_flag |= UPDATE_MODE_FORCE;
		}

		/* setp 3: init fw struct add try do fw upgrade */
		ret = goodix_fw_update_init(cd);
	if (ret) {
		ts_err("failed init fw update module");
		goto err_out;
	}

	ts_info("update flag: 0x%X", update_flag);
	ret = goodix_do_fw_update(cd->ic_configs[CONFIG_TYPE_NORMAL],update_flag);
	if (ret) {
		ts_err("failed do fw update");
#if IS_ENABLED(CONFIG_MIEV)
		xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_FWLOAD_ERR, 0, "TpFirmwareLoadFail", "goodix");
#endif
	}
	/* setp3: get fw version and ic_info
	 * at this step we believe that the ic is in normal mode,
	 * if the version info is invalid there must have some
	 * problem we cann't cover so exit init directly.
	 */
	ret = hw_ops->read_version(cd, &cd->fw_version);
	if (ret) {
		ts_err("invalid fw version, abort");
		goto uninit_fw;
	}
		ret = hw_ops->get_ic_info(cd, &cd->ic_info);
		if (ret) {
			/* #228（b581）blob 该路径（goodix_fw_update_init 内联体
			 * L28185 "invalid ic info, abort"）**无** TpTransferErr
			 * mievent 上报；blob goodix_start_later_init 仅 1 次
			 * mievent str（TpFirmwareLoadFail，L3997 处）。
			 * 原树多一次上报（callcmp only-tree），按 blob 去掉 */
			ts_err("invalid ic info, abort");
			goto uninit_fw;
		}

	/* the recomend way to update ic config is throuth ISP,
	 * if not we will send config with interactive mode
	 */
	goodix_send_ic_config(cd, CONFIG_TYPE_NORMAL);
    /*
#ifdef TOUCH_THP_SUPPORT
	if (cd->enable_touch_raw && cd->ic_configs[CONFIG_TYPE_NORMAL]
		&& cd->ic_configs[CONFIG_TYPE_NORMAL]->len) {
		// must have a config for update K coeffi
		if (goodix_normalize_coeffi_update(cd)) {
			ts_err("failed update normalize coeffi!");
			goto uninit_fw;
		}
	} else {
		ts_info("no config data, skip update normalize coeffi");
	}
#endif
    */ //n11u driver no longer need to send K-matrix to fw

	/* init other resources */
	ret = goodix_ts_stage2_init(cd);
	if (ret) {
		ts_err("stage2 init failed");
		goto uninit_fw;
	}

#ifdef GOODIX_DEBUGFS_ENABLE
	cd->debugfs = debugfs_create_dir("tp_debug", NULL);
	if (cd->debugfs) {
		debugfs_create_file("switch_state", 0660, cd->debugfs, cd,
					&tpdbg_operations);
	}
#endif

	if (cd->goodix_tp_class == NULL) {
#ifdef GOODIX_XIAOMI_TOUCHFEATURE
		cd->goodix_tp_class = get_xiaomi_touch_class_common();
#else
		cd->goodix_tp_class = class_create(THIS_MODULE, "touch");
#endif
	}

#ifdef GOODIX_XIAOMI_TOUCHFEATURE

	goodix_core_data->sync_mode = DIFF_AUTO_SYNC;  //AUTO_SYNC
	hardware_param.x_resolution = goodix_get_x_resolution();
	hardware_param.y_resolution = goodix_get_y_resolution();
	hardware_param.rx_num = (u16) goodix_get_rx_num();
	hardware_param.tx_num = (u16) goodix_get_tx_num();
	hardware_param.super_resolution_factor = ts_bdata->super_resolution_factor;
#ifdef TOUCH_DUMP_TIC_SUPPORT
	hardware_param.frame_data_page_size = 2;
#else
	hardware_param.frame_data_page_size = 1;
#endif //TOUCH_DUMP_TIC_SUPPORT
	hardware_param.frame_data_buf_size = 10;
	hardware_param.raw_data_page_size = 5;
	hardware_param.raw_data_buf_size = 5;
	hardware_param.temp_change_value = 2;
	memset(hardware_param.config_file_name, 0, 64);
	memcpy(hardware_param.config_file_name, HTC_PROJECT_CFG_NAME, strlen(HTC_PROJECT_CFG_NAME));
	memset(hardware_param.driver_version, 0, 64);
	memcpy(hardware_param.driver_version, GOODIX_DRIVER_VERSION, strlen(GOODIX_DRIVER_VERSION));
	/* #228（b581）blob 0xed04-0xed44：逐字节从 core_data->lockdown_info
	 * （blob core_data+0x610，8B）拷进 hardware_param.lockdown_info
	 * （blob .bss+0x2b4d）——是内联拷贝循环而非 goodix_lockdown_info_read()
	 * 调用（该 helper 在 blob 仅作 ops 槽 ic_get_lockdown_info 的地址存放）；
	 * 框架侧 register 时也会经该回调回填 driver_param->hardware_param. */
	for (i = 0; i < GOODIX_LOCKDOWN_SIZE; i++)
		hardware_param.lockdown_info[i] = goodix_core_data->lockdown_info[i];
	goodix_fw_version_info_read(hardware_param.fw_version);

	memset(&hardware_operation, 0, sizeof(hardware_operation_t));
	hardware_operation.ic_self_test = goodix_ic_self_test;
	hardware_operation.ic_data_collect = goodix_ic_data_collect;
	hardware_operation.ic_get_lockdown_info = goodix_lockdown_info_read;
	hardware_operation.ic_get_fw_version = goodix_fw_version_info_read;
	hardware_operation.get_limit_csv_version = goodix_get_limit_csv_version;
	hardware_operation.set_mode_value = goodix_set_cur_value;
	hardware_operation.ic_switch_mode = goodix_ic_switch_mode;
	hardware_operation.cmd_update_func = goodix_game_mode_update;
	hardware_operation.set_mode_long_value = NULL;
	hardware_operation.ic_resume_suspend = goodix_resume_suspend;
	hardware_operation.palm_sensor_write = goodix_palm_sensor_write;
#ifdef TOUCH_THP_SUPPORT
	ts_info("enable thp");
	hardware_operation.enable_touch_raw = goodix_htc_enable;
	goodix_core_data->enable_touch_raw = 1;
#ifdef TOUCH_DUMP_TIC_SUPPORT
	goodix_core_data->dump_type = DUMP_OFF;
#endif //TOUCH_DUMP_TIC_SUPPORT
#endif
	hardware_operation.panel_vendor_read = goodix_panel_vendor_read;
	hardware_operation.panel_color_read = goodix_panel_color_read;
	hardware_operation.panel_display_read = goodix_panel_display_read;
	hardware_operation.touch_vendor_read = goodix_touch_vendor_read;
	hardware_operation.get_touch_ic_buffer = goodix_cmd_fifo_get;
	hardware_operation.touch_doze_analysis = goodix_touch_doze_analysis;
	hardware_operation.htc_ic_setModeValue = goodix_htc_ic_setModeValue;
	hardware_operation.htc_ic_getModeValue = goodix_htc_ic_getModeValue;
	hardware_operation.ic_set_charge_state = goodix_set_charge_state;
	hardware_operation.touch_log_level_control_v2 = goodix_log_level_control;
	hardware_operation.set_thermal_temp = goodix_set_thermal_temp;
#if IS_ENABLED(CONFIG_MIEV)
	hardware_operation.touch_dfs_test = goodix_touch_dfs_test;
#endif
#ifdef TOUCH_FOD_SUPPORT
	hardware_operation.xiaomi_touch_fod_test = goodix_xiaomi_touch_fod_test;
	hardware_operation.xiaomi_touch_fod_attn_test = NULL;
	hardware_operation.xiaomi_touch_fod_low_attn = NULL;
#endif

	register_touch_panel_common(cd->bus->dev, TOUCH_ID, &hardware_param, &hardware_operation);
	xiaomi_register_panel_notifier_common(cd->bus->dev, TOUCH_ID);
#endif

#ifdef TOUCH_TRUSTED_SUPPORT
	if (cd->qts_en) {
		ts_info("enable QTS");
		goodix_fill_qts_vendor_data(&qts_vendor_data, cd);
		ret = qts_client_register(qts_vendor_data);
		if (ret)
			ts_err("qts client register failed, rc %d\n", ret);
	}
	init_completion(&cd->tui_finish);
	cd->tui_process = false;
#endif // TOUCH_TRUSTED_SUPPORT

	cd->init_stage = CORE_INIT_STAGE2;

	return 0;

uninit_fw:
	goodix_fw_update_uninit();
err_out:
	ts_err("stage2 init failed");
	cd->init_stage = CORE_INIT_FAIL;
	for (i = 0; i < GOODIX_MAX_CONFIG_GROUP; i++) {
		if (cd->ic_configs[i])
			kfree(cd->ic_configs[i]);
		cd->ic_configs[i] = NULL;
	}
	return ret;
}

/**
 * goodix_ts_probe - called by kernel when Goodix touch
 *  platform driver is added.
 */
static int goodix_ts_probe(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = NULL;
	struct goodix_bus_interface *bus_interface;
	int ret;

	ts_info("goodix_ts_probe IN THP");
	/* _b582-INTC：blob 该串唯一引用点 = .init.text init_module+0xe4（disr L47124，
	 * reloc .rodata.str1.1+0x16b4，门 `cmp w8,#4; b.hs` = ts_debug 级），位于 DET1
	 * （面板 gpio 639，"TP is not goodix!" 判非）的 goodix 分支内、goodix_spi_bus_init
	 * 之前（0x4c-0x58）。树侧 DET1 门由 A-78 移至 goodix_spi_probe（initcall 期
	 * gpiochip 未注册），该门通过才 platform_device_register ⇒ 本探测是 DET1==1 之后
	 * goodix 侧首个入口，条件等价（残留：所属函数 init_module vs goodix_ts_probe）。 */
	ts_debug("TP is goodix, panel is CSOT.");
	bus_interface = pdev->dev.platform_data;
	if (!bus_interface) {
		ts_err("Invalid touch device");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENODEV;
	}

	core_data = devm_kzalloc(&pdev->dev,
			sizeof(struct goodix_ts_core), GFP_KERNEL);
	if (!core_data) {
		/* _b582-INTD（A4-1）：blob = .rodata.str1.1+0xb85
		 * b'\0016[GTP_E][%s:%d]: allocate core data memory failed'
		 * （goodix_core_rodin.disr goodix_ts_probe 0xd314/0xd328，行 0x1049=4169，
		 * __func__ "goodix_ts_probe"；与 0xd2f8 "Invalid touch device"（行 4161）
		 * 同段相邻 ⇒ 同位置换文，非仅前缀/词序差） */
		ts_err("allocate core data memory failed");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -ENOMEM;
	}
	goodix_core_data = core_data;
	if (IS_ENABLED(CONFIG_OF) && bus_interface->dev->of_node) {
		/* parse devicetree property */
		ret = goodix_parse_dt(bus_interface->dev->of_node,
				&core_data->board_data);
		if (ret) {
#if IS_ENABLED(CONFIG_MIEV)
			xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0, "TpParamParseFail", "goodix", ERROR_DTS_PARSE);
#endif
			ts_err("failed parse device info form dts, %d", ret);
			return -EINVAL;
		}
	} else {
		ts_err("no valid device tree node found");
		return -ENODEV;
	}

	core_data->hw_ops = goodix_get_hw_ops();
	if (!core_data->hw_ops) {
		ts_err("hw ops is NULL");
		core_module_prob_sate = CORE_MODULE_PROB_FAILED;
		return -EINVAL;
	}
	mutex_init(&core_data->core_mutex);
	goodix_core_module_init();
	/* touch core layer is a platform driver */
	core_data->pdev = pdev;
	core_data->bus = bus_interface;
	platform_set_drvdata(pdev, core_data);

	/* get GPIO resource */
	ret = goodix_ts_gpio_setup(core_data);
	if (ret) {
#if IS_ENABLED(CONFIG_MIEV)
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0, "TpParamParseFail", "goodix", ERROR_GPIO_REQUEST);
#endif
		ts_err("failed init gpio");
		goto err_out;
	}

	/* #228（b581）blob 厂商源行 4213 ts_err("begin goodix_ts_power_init")
	 * （树缺），失败串 4219 为 "fail to init power(regulator)"（树原写
	 * "failed init power"）——按 blob 收口 */
	ts_err("begin goodix_ts_power_init");
	ret = goodix_ts_power_init(core_data);
	if (ret) {
#if IS_ENABLED(CONFIG_MIEV)
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0, "TpParamParseFail", "goodix", ERROR_REGULATOR_INIT);
#endif
		ts_err("fail to init power(regulator)");
		goto err_out;
	}
	ts_err("end goodix_ts_power_init");

	/*set pinctrl */
	ret = goodix_ts_pinctrl_init(core_data);
	if (!ret && core_data->pinctrl) {
		ret = pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_active);
		if (ret < 0)
			ts_err("Failed to select active pinstate, r:%d", ret);
		/* #228（b581）blob goodix_ts_probe 0xde6c：active 之后**无条件**
		 * select pmx_gt_spi_mode（blob 无 pin_sta_spi_mode NULL 判据，
		 * NULL 时 select 内部返 -EINVAL 并打印），失败打印
		 * "Failed to select %s pinstate %d"（厂商源行 4235），不阻断
		 * probe。主 SPI 三组引脚 mux 进 SPI 功能态，位于第一次 SPI
		 * 事务（dev_confirm）之前 */
		ret = pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_spi_mode);
		if (ret < 0)
			ts_err("Failed to select %s pinstate %d",
					"pmx_gt_spi_mode", ret);
	}

	ts_err("begin goodix_ts_power_on");
	ret = goodix_ts_power_on(core_data);
	if (ret) {
#if IS_ENABLED(CONFIG_MIEV)
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0, "TpParamParseFail", "goodix", ERROR_REGULATOR_INIT);
#endif
		ts_err("failed power on");
		goto err_out;
	}
	/* #228（b581）blob 厂商源行 4248 ts_err("end goodix_ts_power_on")（树缺） */
	ts_err("end goodix_ts_power_on");

	/* confirm it's goodix touch dev or not */
	ret = core_data->hw_ops->dev_confirm(core_data);
	if (ret) {
#if IS_ENABLED(CONFIG_MIEV)
		xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_TRANSFER_ERR, 0, "TpTransferErr", "goodix");
#endif
		ts_err("goodix device confirm failed");
		/*goto err_out;*/
	}

	ts_err("end dev_confirm");

	/* generic notifier callback */
	core_data->ts_notifier.notifier_call = goodix_generic_noti_callback;
	goodix_ts_register_notifier(&core_data->ts_notifier);

	device_init_wakeup(core_data->bus->dev, 1);

	/* debug node init */
	goodix_tools_init();
	core_data->tp_pm_suspend = false;

	init_completion(&core_data->pm_resume_completion);
	device_init_wakeup(&pdev->dev, 1);
	core_data->init_stage = CORE_INIT_STAGE1;
	core_data->report_rate = 240;
	goodix_modules.core_data = core_data;
	core_module_prob_sate = CORE_MODULE_PROB_SUCCESS;

	ts_info("goodix_ts_core probe success");

	/* Try start a thread to get config-bin info */
	ret = goodix_start_later_init(core_data);
	if (ret) {
		/* #228（b581）blob 厂商源行 4284 无 "[DIS-TF-TOUCH] " 前缀 */
		ts_err("Failed start cfg_bin_proc, %d", ret);
		goto err_out;
	}

	/* _b573 boost wiring: blob 0xe000-0xe008 = init_touch_irq(0, bus_interface->dev->of_node) */
	init_touch_irq(TOUCH_ID, bus_interface->dev->of_node);

	/* _b573 scp 接线：blob 0xe00c-0xe044 = 探针尾填 scp_tp_param 缺省后 scp_tp_init()。
	 * blob 从 core_data 0xa7/0xbf/0xd3 读的三字节全模块零写入（kzalloc 后恒 0），按 0 种子化。 */
	scp_tp_param.param0 = 1;
	scp_tp_param.unknown_04 = 0;
	scp_tp_param.field_08 = 0;
	scp_tp_param.unknown_0c = 32;
	scp_tp_param.unknown_14[2] = 0;
	scp_tp_param.unknown_14[3] = 2500;

	/* _b582-INTD（A4-2）：blob goodix_ts_probe 0xe040 `cmp w12,#3; b.hs 0xe15c`（ts_info 级），
	 * 0xe15c/0xe160 = adrp/add .rodata.str1.1+0x2db2 =
	 * b'\0016[GTP_I][%s:%d]: mtk_scp_touch_init in probe'（行 0x10dc=4316，
	 * __func__ 0x6fe0 "goodix_ts_probe"），printk 后 0xe174 `b 0xe044` 回到
	 * scp_tp_init 调用点 ⇒ 位置：scp_tp_param 填充之后、scp_tp_init 之前。 */
	ts_info("mtk_scp_touch_init in probe");

	scp_tp_init();

	return 0;

err_out:
	/* _b573 boost wiring: blob 0xdbcc-0xdbd0 = err 清理最前 remove_touch_irq_boost(0) */
	remove_touch_irq_boost(TOUCH_ID);
	core_data->init_stage = CORE_INIT_FAIL;
	core_module_prob_sate = CORE_MODULE_PROB_FAILED;
	if (core_data->pinctrl) {
			pinctrl_select_state(core_data->pinctrl,
					core_data->pin_sta_suspend);
			devm_pinctrl_put(core_data->pinctrl);
		}
	core_data->pinctrl = NULL;
	ts_err("goodix_ts_core failed, ret:%d", ret);
	return ret;
}

static void goodix_ts_remove(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = platform_get_drvdata(pdev);
	struct goodix_ts_hw_ops *hw_ops = core_data->hw_ops;
	struct goodix_ts_esd *ts_esd = &core_data->ts_esd;

	/* _b573 scp 接线：blob 0xe1b0-0xe1cc = remove 起手 scp_tp_exit + 框架双反注册。
	 * _b583b-GX/T4：blob ts_remove（0xe198，532B）无 ts_notifier 反注册
	 * （0xe250 的 blocking_notifier_chain_unregister 仅 esd_notifier 一处，
	 * calldiff only-tree blocking_notifier_chain_unregister:1），删之 */
	scp_tp_exit();
	xiaomi_unregister_panel_notifier_common(core_data->bus->dev, TOUCH_ID);
	unregister_touch_panel_common(TOUCH_ID);
	goodix_tools_exit();

	if (core_data->init_stage >= CORE_INIT_STAGE2) {
		gesture_module_exit();
		inspect_module_exit();
		hw_ops->irq_enable(core_data, false);
	#ifdef CONFIG_FB
		fb_unregister_client(&core_data->fb_notifier);
	#endif
		core_module_prob_sate = CORE_MODULE_REMOVED;
		if (atomic_read(&core_data->ts_esd.esd_on))
			goodix_ts_esd_off(core_data);
		goodix_ts_unregister_notifier(&ts_esd->esd_notifier);

		goodix_fw_update_uninit();
		goodix_ts_input_dev_remove(core_data);
		goodix_ts_pen_dev_remove(core_data);
		goodix_ts_sysfs_exit(core_data);
		goodix_ts_procfs_exit(core_data);
		goodix_ts_power_off(core_data);
	}
	/* _b573 boost wiring: blob 0xe34c-0xe350 = remove 末尾 remove_touch_irq_boost(0) */
	remove_touch_irq_boost(TOUCH_ID);
}

static void goodix_ts_shutdown(struct platform_device *pdev)
{
	struct goodix_ts_core *core_data = platform_get_drvdata(pdev);
	ts_info("goodix_ts_shutdown enter");
	//disable irq -> [500us] -> power off -> unregister
	core_data->hw_ops->irq_enable(core_data, false);
	usleep_range(500, 510);
	if (core_data) {
		goodix_ts_power_off(core_data);
	}
	xiaomi_unregister_panel_notifier_common(core_data->bus->dev, TOUCH_ID);
	unregister_touch_panel_common(TOUCH_ID);
}



#ifdef CONFIG_PM
static const struct dev_pm_ops dev_pm_ops = {
	.suspend = goodix_ts_pm_suspend,
	.resume = goodix_ts_pm_resume,
};
#endif

static const struct platform_device_id ts_core_ids[] = {
	{.name = GOODIX_CORE_DRIVER_NAME},
	{}
};
MODULE_DEVICE_TABLE(platform, ts_core_ids);

static struct platform_driver goodix_ts_driver = {
	.driver = {
		.name = GOODIX_CORE_DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &dev_pm_ops,
#endif
	},
	.probe = goodix_ts_probe,
	.remove = goodix_ts_remove,
	.shutdown = goodix_ts_shutdown,
	.id_table = ts_core_ids,
};

static int __init goodix_ts_core_init(void)
{
	int ret;

	ts_info("Core layer init:%s", GOODIX_DRIVER_VERSION);
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	ret = goodix_spi_bus_init();
#else
	ret = goodix_i2c_bus_init();
#endif
	if (ret) {
		ts_err("failed add bus driver");
		return ret;
	}
	return platform_driver_register(&goodix_ts_driver);
}

static void __exit goodix_ts_core_exit(void)
{
	ts_info("Core layer exit");
	platform_driver_unregister(&goodix_ts_driver);
#ifdef CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
	goodix_spi_bus_exit();
#else
	goodix_i2c_bus_exit();
#endif
}

/* _b580：触控框架 probe 时序倒挂修复（vseq 化），与 focaltech_rodin 同因。
 * 原厂本单元 = goodix_core.ko，6.6 装载位 vendor_dlkm modules.load 行 212
 * （框架 211 之后、focaltech 213 之前）。按 vseq.h 层级对应改写：.ko 语境下
 * late_initcall 折叠为 module_init，vseq_*_initcall 全族同义、级别仅用于
 * 单元内排序（本单元单入口），故 1:1 保留 late 形态；重放按 seq 1254 排。 */
vseq_late_initcall(goodix_ts_core_init);
module_exit(goodix_ts_core_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Core Module");
/* MODULE_IMPORT_NS(VFS_internal_...) — 6.18 该 NS 归属 fs/ 子系统，=y 内建不需要 */
MODULE_AUTHOR("Goodix, Inc.");
MODULE_LICENSE("GPL v2");

