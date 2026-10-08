/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */

#include <linux/device.h>
#include "xiaomi_touch.h"

#define SENSITIVE_EVENT_BUF_SIZE    (10)
#define TOUCH_ID    0
#define MAX_BUF_SIZE (sizeof(common_data_t))

enum MI_TP_LOG_LEVEL current_log_level = MI_TP_LOG_INFO;

typedef struct abnormal_event {
	u16 type;
	u16 code;
	u16 value;
} abnormal_event_t;

struct {
	int top;
	int bottom;
	bool full_flag;
	abnormal_event_t abnormal_event[SENSITIVE_EVENT_BUF_SIZE];
} abnormal_event_buf;
#ifdef TOUCH_STYLUS_SUPPORT
static DEFINE_MUTEX(stylus_connect_status_mutex);
#endif
#ifdef TOUCH_FOD_SUPPORT
static DEFINE_MUTEX(fod_press_status_mutex);
#endif
static DEFINE_MUTEX(abnormal_event_mutex);
static DEFINE_MUTEX(palm_mutex);

static DEFINE_MUTEX(thp_ic_mutex);
static DEFINE_MUTEX(thp_ic_read_data_mutex);
static DEFINE_MUTEX(thp_ic_write_data_mutex);

static struct device *xiaomi_touch_dev = NULL;
static struct class *xiaomi_touch_class = NULL;
static struct attribute_group xiaomi_touch_attrs;

#ifdef TOUCH_FOD_SUPPORT
static int fod_press_status_value = 0;
static int fod_attn_test = 2;
#endif

#ifdef TOUCH_STYLUS_SUPPORT
static int stylus_connect_status_value = 0;
#endif

static int doze_analysis_result;
static int is_enable_touchraw = 1;
static int palm_value;
static u64 ic_buffer_addr;
static int touch_finger_status = 0;

static common_data_t thp_ic_cmd_data_common_data;
static int vendor_input = 1;

#define CREATE_ATTR(name, show_func, store_func) \
	static ssize_t show_##name(struct device *dev, struct device_attribute *attr, char *buf)show_func \
	static ssize_t store_##name(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)store_func \
	static DEVICE_ATTR(name, (S_IRUGO | S_IWUSR | S_IWGRP), show_##name, store_##name);

static int analy_poll_data(char* poll_data_buf, char* buf)
{
	int count = 0;
	xiaomi_touch_data_t* xiaomi_touch_data = get_xiaomi_touch_data(TOUCH_ID);

	if (!poll_data_buf || !buf)
		return -1;
	memset(buf, 0x00, PAGE_SIZE);
	memcpy((char* )xiaomi_touch_data->poll_data, poll_data_buf, sizeof(struct htc_ic_polldata));

	count += snprintf(buf, PAGE_SIZE, "protocol_version:    %hu\n", xiaomi_touch_data->poll_data->protocol_version);
	count += snprintf(buf + count, PAGE_SIZE, "frame_len:    %hu\n", xiaomi_touch_data->poll_data->frame_len);
	count += snprintf(buf + count, PAGE_SIZE, "ic_fw_v:    %hu\n", xiaomi_touch_data->poll_data->ic_fw_v);
	count += snprintf(buf + count, PAGE_SIZE, "ic_name:    %hu\n", xiaomi_touch_data->poll_data->ic_name);
	count += snprintf(buf + count, PAGE_SIZE, "ic_project_name:    %hu\n", xiaomi_touch_data->poll_data->ic_project_name);
	count += snprintf(buf + count, PAGE_SIZE, "ic_supplier_name:    %hu\n", xiaomi_touch_data->poll_data->ic_supplier_name);
	count += snprintf(buf + count, PAGE_SIZE, "pitch_size_y:    %d\n", xiaomi_touch_data->poll_data->pitch_size_y);
	count += snprintf(buf + count, PAGE_SIZE, "pitch_size_x:    %d\n", xiaomi_touch_data->poll_data->pitch_size_x);
	count += snprintf(buf + count, PAGE_SIZE, "numCol:    %d\n", xiaomi_touch_data->poll_data->numCol);
	count += snprintf(buf + count, PAGE_SIZE, "numRow:    %d\n", xiaomi_touch_data->poll_data->numRow);
	count += snprintf(buf + count, PAGE_SIZE, "packaging_factory:    %hu\n", xiaomi_touch_data->poll_data->packaging_factory);
	count += snprintf(buf + count, PAGE_SIZE, "wafe_factory:    %hu\n", xiaomi_touch_data->poll_data->wafe_factory);
	count += snprintf(buf + count, PAGE_SIZE, "x_resolution:    %hu\n", xiaomi_touch_data->poll_data->x_resolution);
	count += snprintf(buf + count, PAGE_SIZE, "y_resolution:    %hu\n", xiaomi_touch_data->poll_data->y_resolution);
	count += snprintf(buf + count, PAGE_SIZE, "lockdown_info:    %llu\n", xiaomi_touch_data->poll_data->lockdown_info);
	count += snprintf(buf + count, PAGE_SIZE, "frame_data_type:    %d\n", xiaomi_touch_data->poll_data->frame_data_type);
	count += snprintf(buf + count, PAGE_SIZE, "mutual_len:    %hu\n", xiaomi_touch_data->poll_data->mutual_len);
	count += snprintf(buf + count, PAGE_SIZE, "slef1_len:    %hu\n", xiaomi_touch_data->poll_data->slef1_len);
	count += snprintf(buf + count, PAGE_SIZE, "slef2_len:    %hu\n", xiaomi_touch_data->poll_data->slef2_len);

	return count;
}
#ifdef TOUCH_STYLUS_SUPPORT
int update_stylus_connect_status_value(int value)
{
	mutex_lock(&stylus_connect_status_mutex);

	if (stylus_connect_status_value != value) {
		LOG_INFO("value: %d", value);
		stylus_connect_status_value = value;
		/* notify surfaceflinger */
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "pen_connect_strategy");
	}

	mutex_unlock(&stylus_connect_status_mutex);
	return 0;
}
#endif
#ifdef TOUCH_FOD_SUPPORT
int update_fod_press_status_common(int value)
{
	mutex_lock(&fod_press_status_mutex);

	if (value != fod_press_status_value) {
		LOG_INFO("value:%d", value);
		fod_press_status_value = value;
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "fod_press_status");
	}

	mutex_unlock(&fod_press_status_mutex);
	return 0;
}
EXPORT_SYMBOL(update_fod_press_status_common);
#endif // TOUCH_FOD_SUPPORT

int update_palm_sensor_value_common(int value)
{
	mutex_lock(&palm_mutex);
	if (value != palm_value) {
		LOG_ERROR("value:%d", value);
		palm_value = value;
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "palm_sensor");
	}

	mutex_unlock(&palm_mutex);
	return 0;
}
EXPORT_SYMBOL(update_palm_sensor_value_common);

/* _b581-XT③：update_palm_sensor_value_second_panel / palm_value_1 为 donor 残留——
 * blob def/dynsym 面均无该符号（blob 仅 update_palm_sensor_value_common @0x754c +
 * 单一全局 palm_value@bss+0x8b48），且树内已无调用者（mode.c SET_CMD_FOR_DRIVER 已按 blob
 * 收口为单面板调用）；rodin 无 panel1 的 palm_sensor_1 节点。按 blob 删除，不动导出面。 */

/* _b583-XT2（跨侧定论：fts 侧 A1 双证后已删其 2 处调用）：
 * `update_weak_doubletap_value` 与串 "weak_doubletap_sensor" 在 blob **确无** ——
 *   ① blob 符号面（框架 def/dynsym + fts UND）0 命中；
 *   ② blob .strings/.rostr 0 命中 "weak_doubletap_sensor"；
 *   ③ blob dev_attr_* 全量 = 17 项，无该节点（树侧本就无其 DEVICE_ATTR 定义，
 *      sysfs_notify 目标不存在 ⇒ 原为恒无效通知）。
 * ⇒ 函数 + weak_doubletap_mutex/weak_doubletap_value 一并删除（导出面本无 EXPORT）。 */

int update_abnormal_event(u16 type, u16 code, u16 value)
{
	abnormal_event_t temp_event;
	temp_event.type = type;
	temp_event.code = code;
	temp_event.value = value;

	mutex_lock(&abnormal_event_mutex);
	memcpy(&abnormal_event_buf.abnormal_event[abnormal_event_buf.top], &temp_event, sizeof(abnormal_event_t));
	abnormal_event_buf.top++;
	if (abnormal_event_buf.top >= SENSITIVE_EVENT_BUF_SIZE) {
		abnormal_event_buf.full_flag = true;
		abnormal_event_buf.top = 0;
	}
	mutex_unlock(&abnormal_event_mutex);
	sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "abnormal_event");
	return 0;
}

struct class *get_xiaomi_touch_class_common(void)
{
	return xiaomi_touch_class;
}
EXPORT_SYMBOL(get_xiaomi_touch_class_common);

#ifdef TOUCH_STYLUS_SUPPORT
CREATE_ATTR(pen_connect_strategy, {
		return snprintf(buf, PAGE_SIZE, "%d\n", stylus_connect_status_value);
	},
	{
		return count;
	});
#endif
#ifdef TOUCH_FOD_SUPPORT
CREATE_ATTR(fod_press_status, {
		return snprintf(buf, PAGE_SIZE, "%d\n", fod_press_status_value);
	},
	{
		return count;
	});
#endif

CREATE_ATTR(panel_vendor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_vendor_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_vendor_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(panel_color, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_color_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_color_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(panel_display, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.panel_display_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.panel_display_read()) : 0;
	},
	{
		return count;
	});

CREATE_ATTR(touch_vendor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		return (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.touch_vendor_read) ?
			snprintf(buf, PAGE_SIZE, "%c", xiaomi_touch_driver_param->hardware_operation.touch_vendor_read()) : 0;
	},
	{
		return count;
	});


CREATE_ATTR(touch_doze_analysis, {
		return snprintf(buf, PAGE_SIZE, "%d\n", doze_analysis_result);
	},
	{
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
		int input;
		s8 touch_id = 0;
		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		for (touch_id = 0; touch_id < MAX_TOUCH_PANEL_COUNT; touch_id++) {
			xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
			if (xiaomi_touch_driver_param == NULL)
				continue;
			if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.touch_doze_analysis) {
				doze_analysis_result = xiaomi_touch_driver_param->hardware_operation.touch_doze_analysis(input);
			}
		}

		LOG_INFO("value:%d", input);
		return count;
	});

CREATE_ATTR(touch_ic_buffer, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		u8 *tmp_buf = NULL;
		int n = 0;
		LOG_INFO("get ic buffer from addr: 0x%08llX", ic_buffer_addr);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.get_touch_ic_buffer) {
			tmp_buf = kzalloc_retry(PAGE_SIZE, 3);
			xiaomi_touch_driver_param->hardware_operation.get_touch_ic_buffer(ic_buffer_addr, tmp_buf);
			n = snprintf(buf, PAGE_SIZE, "%s", tmp_buf);
			kzalloc_free(tmp_buf);
		}
		return n;
	},
	{
		if (sscanf(buf, "0x%llX", &ic_buffer_addr) < 0) {
			LOG_INFO("write addr error format. current addr is 0x%08llX", ic_buffer_addr);
			return -EINVAL;
		}
		LOG_INFO("write addr: 0x%08llX", ic_buffer_addr);
		return count;
	});

CREATE_ATTR(resolution_factor, {
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		int factor = 1;
		if (xiaomi_touch_driver_param)
			factor = xiaomi_touch_driver_param->hardware_param.super_resolution_factor;

		return snprintf(buf, PAGE_SIZE, "%d", factor);
	},
	{
		return count;
	});

CREATE_ATTR(abnormal_event, {
		int struct_abnormal_event_size = sizeof(abnormal_event_t);
		mutex_lock(&abnormal_event_mutex);
		if (abnormal_event_buf.bottom == abnormal_event_buf.top && !abnormal_event_buf.full_flag) {
			LOG_ERROR("buf is empty");
			mutex_unlock(&abnormal_event_mutex);
			return -1;
		}
		memcpy(buf, &abnormal_event_buf.abnormal_event[abnormal_event_buf.bottom], struct_abnormal_event_size);
		abnormal_event_buf.bottom++;
		if (abnormal_event_buf.bottom >= SENSITIVE_EVENT_BUF_SIZE) {
			abnormal_event_buf.full_flag = false;
			abnormal_event_buf.bottom = 0;
		}
		if (abnormal_event_buf.top > abnormal_event_buf.bottom || abnormal_event_buf.full_flag)
			sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "abnormal_event");
		mutex_unlock(&abnormal_event_mutex);

		return struct_abnormal_event_size;
	},
	{
		abnormal_event_t *temp_event = (abnormal_event_t *)buf;
		int struct_abnormal_event_size = sizeof(abnormal_event_t);
		if (count != struct_abnormal_event_size) {
			LOG_ERROR("fail! size = %zu, %d", count, struct_abnormal_event_size);
			return -ENODEV;
		}

		mutex_lock(&abnormal_event_mutex);
		memcpy(&abnormal_event_buf.abnormal_event[abnormal_event_buf.top], temp_event, struct_abnormal_event_size);
		abnormal_event_buf.top++;
		if (abnormal_event_buf.top >= SENSITIVE_EVENT_BUF_SIZE) {
			abnormal_event_buf.full_flag = true;
			abnormal_event_buf.top = 0;
		}
		mutex_unlock(&abnormal_event_mutex);
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "abnormal_event");
		return count;
	});

CREATE_ATTR(enable_touch_raw, {
		return snprintf(buf, PAGE_SIZE, "%d\n", is_enable_touchraw);
	},
	{
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(0);
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		LOG_ERROR("enable touch raw %d", input);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.enable_touch_raw)
			xiaomi_touch_driver_param->hardware_operation.enable_touch_raw(input);

		is_enable_touchraw = input;
		return count;
	});

CREATE_ATTR(palm_sensor, {
		return snprintf(buf, PAGE_SIZE, "%d\n", palm_value);
	},
	{
		int input;
		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;

		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.palm_sensor_write)
			xiaomi_touch_driver_param->hardware_operation.palm_sensor_write(!!input);

		/* _b580-A74①（站点5）：blob 0x8398-0x83d0 第二段 = 对 touch_id 1 再取一次
		 * driver_param（blob 取 [x+0x1b0] = palm_sensor_write；6.18 pahole =
		 * driver_param+0x1e0，树侧编译偏移实测 0x1e0）并以同一布尔值直通；
		 * 两段顺序在 common-data 推送（0x83d4-0x83e8，mode=26 len=1）之前。 */
		xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(1);
		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.palm_sensor_write)
			xiaomi_touch_driver_param->hardware_operation.palm_sensor_write(!!input);

		add_common_data_to_buf_common(TOUCH_ID, SET_CUR_VALUE, DATA_MODE_26, 1, &input);
		LOG_INFO("value:%d", input);
		return count;
	});



CREATE_ATTR(touch_thp_ic_cmd, {
		return 0;
	},
	{
		/* _b580-A74①（站点1）：blob 0x89c0-0x8c5c 全形重建（三处代差：
		 *   ① 解析 = blob 内联十进制解析 0x8a10-0x8a98（数字累乘入 input[i]；','/' ' 分隔；
		 *      前一字符为数字才推进槽位；首字符非数字/分隔即止），**无** kstrtoint/strsep/
		 *      kzalloc+kfree（树侧 donor 形态的四处调用全部消失）；
		 *   ② 出口 = blob 只做 common-data 推送（0x8ba4 / 0x8be4 两处
		 *      add_common_data_to_buf_common），**不**直调 htc_ic_setModeValue/getModeValue、
		 *      也**不**填 thp_ic_cmd_data_common_data；
		 *   ③ 校验/分支/串按 blob：unsupport cmd!!(390) / input format is error!!(396) /
		 *      data format is error!!(418) / data mode is error!!(409) 四串四分支。
		 * 推送形参来源：touch_id 恒 0（blob mov w0,wzr）、cmd=input[0]、mode=input[1]、
		 * data=&input[2]；长度 = para_cnt-3（IC_MODE_44 支）/ input[3]（非 44 支）。 */
		s32 input[CMD_DATA_BUF_SIZE];
		int i = 0;
		int para_cnt = 0;
		int prev_digit = 0;
		int overflow = 0;
		unsigned char c;
		size_t n = 0;
		ssize_t ret = count;

		mutex_lock(&thp_ic_mutex);

		memset(input, 0x00, sizeof(int) * CMD_DATA_BUF_SIZE);

		/* blob 0x8a48-0x8a98 解析循环；上界用 count（6.18 kernfs
		 * fs/kernfs/file.c:337 `buf[len] = '\0'` 保证串终止 ⇒ blob 的"读到 NUL 为止"
		 * 与本写法逐字符等价，且不再依赖跨出 count 的读）。
		 * 【安全偏离】blob 对槽位 i >= CMD_DATA_BUF_SIZE 走 brk #0x5512 陷阱
		 * （0x8a5c cmp w8,#0x100 + 0x8c50），此处改为 overflow→format 检查拒绝；
		 * 合法输入 i <= 255 语义不变。 */
		for (n = 0; n < count; n++) {
			c = (unsigned char)buf[n];
			if (c >= '0' && c <= '9') {
				if (i >= CMD_DATA_BUF_SIZE) {
					overflow = 1;
					break;
				}
				input[i] = input[i] * 10 + (c - '0');
				if (!prev_digit)
					++para_cnt;
				prev_digit = 1;
			} else if (c == ',' || c == ' ') {
				if (prev_digit)
					++i;
				prev_digit = 0;
			} else {
				break;
			}
		}

		LOG_INFO("user_cmd:%d, mode:%d, data:%d, data_len:%d", input[0], input[1], input[2], input[3]);

		if (input[0] != SET_THP_IC_CUR_VALUE && input[0] != GET_THP_IC_CUR_VALUE) {
			LOG_ERROR("unsupport cmd!!");
			ret = -1;
			goto out;
		}

		if (para_cnt < 4 || para_cnt > CMD_DATA_BUF_SIZE || overflow) {
			LOG_ERROR("input format is error!!");
			ret = -1;
			goto out;
		}

		if (input[1] == IC_MODE_44) {
			/* 传输模式：仅 cmd=SET_THP_IC_CUR_VALUE 推送，且 data_len 必须 == 载荷个数 */
			if (input[0] == SET_THP_IC_CUR_VALUE) {
				if ((para_cnt - 3) == input[para_cnt - 1])
					add_common_data_to_buf_common(TOUCH_ID, input[0], input[1], para_cnt - 3, &input[2]);
				else {
					LOG_ERROR("data format is error!!");
					ret = -1;
				}
			}
		} else if (input[1] < 0) {
			/* blob 0x8b28（tbnz w2,#0x1f）：负 mode 放行（不推送、不报错，ret=count） */
		} else {
			if (para_cnt > 4 || (para_cnt - 3) == input[para_cnt - 1]) {
				LOG_ERROR("data mode is error!!");
				ret = -1;
			} else if (input[1] == IC_MODE_49 || input[3] == 2) {
				add_common_data_to_buf_common(TOUCH_ID, input[0], input[1], input[3], &input[2]);
			} else {
				LOG_ERROR("data mode is error!!");
				ret = -1;
			}
		}
out:
		mutex_unlock(&thp_ic_mutex);
		return ret;
	});

void update_get_ic_current_value(common_data_t *common_data) {
	memcpy(&thp_ic_cmd_data_common_data, common_data, sizeof(common_data_t));
}

CREATE_ATTR(touch_thp_ic_cmd_data, {
		int i = 0;
		int count = 0;
		int mode = thp_ic_cmd_data_common_data.mode;
		char *data_buf = (char *)&thp_ic_cmd_data_common_data.data_buf[1];
		u16 data_len = thp_ic_cmd_data_common_data.data_len;

		LOG_INFO("mode:%d, cmd:%d\n", mode, thp_ic_cmd_data_common_data.cmd);

		mutex_lock(&thp_ic_read_data_mutex);
		if (thp_ic_cmd_data_common_data.cmd != GET_THP_IC_CUR_VALUE) {
			LOG_ERROR("error, need to read data for ic!");
			mutex_unlock(&thp_ic_read_data_mutex);
			return count;
		}

		for (i = 0; i < data_len; i++) {
			LOG_INFO("buf[%d]:%x", i, data_buf[i]);
		}
		if (mode > THP_IC_CMD_BASE) {
			if (mode != IC_MODE_49) {
				for (i = 0; i < data_len; ++i) {
					count += snprintf(buf + count, PAGE_SIZE - count, "%x", data_buf[i]);
				}
				count += snprintf(buf + count, PAGE_SIZE - count, "\n");
			} else {
				count = analy_poll_data(data_buf, buf);
			}
		} else {
			memcpy(buf, data_buf, data_len);
			count = data_len;
		}

		mutex_unlock(&thp_ic_read_data_mutex);
		return count;
	},
	{
		if (count > MAX_BUF_SIZE) {
			LOG_ERROR("%s memory out of range:%d\n", __func__, (int)count);
			return count;
		}

		mutex_lock(&thp_ic_write_data_mutex);
		memcpy((char* )&thp_ic_cmd_data_common_data, buf, count);
		mutex_unlock(&thp_ic_write_data_mutex);

		return count;
	});


/* blob 还原：show = snprintf("%s\n", scp_tp_mistouch_close ? "close" : "open")；
 * store = sscanf %d → printk(E 门控) → scp_tp_mistouch_close = (val != 0) →
 * param ops 的 scp_mistouch_enable 回调（blob param+0x178 KCFI 指针，ops 布局
 * 漂移记账：popsicle ops 结构与 blob 不同代，落为命名成员）。 */
CREATE_ATTR(scp_tp_mistouch_enable, {
		return snprintf(buf, PAGE_SIZE, "%s\n",
				scp_tp_mistouch_close ? "close" : "open");
	},
	{
		int input;
		xiaomi_touch_driver_param_t *param;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		if (current_log_level)
			printk(KERN_INFO "[MI_TP_E][%s:%d]: scp_tp_mistouch_enable %d",
			       __func__, __LINE__, input);
		scp_tp_mistouch_close = (input != 0);
		param = get_xiaomi_touch_driver_param(TOUCH_ID);
		if (param && param->hardware_operation.scp_mistouch_enable)
			param->hardware_operation.scp_mistouch_enable(&input);
		return count;
	});

CREATE_ATTR(touch_finger_status, {
		return snprintf(buf, PAGE_SIZE, "%d\n", touch_finger_status);
	},
	{
		int input;

		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		if (touch_finger_status == input)
			return count;
		touch_finger_status = input;
		if (touch_finger_status == 1)
			touch_irq_boost(0);
		else
			touch_irq_boost_release(0);
		sysfs_notify(&xiaomi_touch_dev->kobj, NULL, "touch_finger_status");

		return count;
	});

#ifdef TOUCH_FOD_SUPPORT
CREATE_ATTR(fod_test, {
		return 0;
	},
	{
		int value;
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(TOUCH_ID);

		if (sscanf(buf, "%d", &value) < 0)
			return -EINVAL;

		if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.xiaomi_touch_fod_test)
			xiaomi_touch_driver_param->hardware_operation.xiaomi_touch_fod_test(value);

		return count;
	});


#endif

#ifdef DFS_DEBUG_TEST
CREATE_ATTR(touch_dfs_test,
	{
		return 0;
	},
	{
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
		int input;
		s8 touch_id = 0;
		if (sscanf(buf, "%d", &input) < 0)
			return -EINVAL;
		for (touch_id = 0; touch_id < MAX_TOUCH_PANEL_COUNT; touch_id++) {
			xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
			if (xiaomi_touch_driver_param && xiaomi_touch_driver_param->hardware_operation.touch_dfs_test) {
                		xiaomi_touch_driver_param->hardware_operation.touch_dfs_test(input);
                	}
		}
		return count;
	});
#endif

CREATE_ATTR(touch_log_level, {
		return snprintf(buf, PAGE_SIZE,
				"echo [value0] [value1] > touch_log_level\n"
				"[value0]: control xiaomi_touch log level, 0:Always,1:Error,2:Warning,3:Info,4:Debug,5:Verbose\n"
				"[value1]: control vendor driver log level, 0:Always,1:Error,2:Warning,3:Info,4:Debug,5:Verbose\n"
				"current xiaomi_touch log_level = %d, vendor log_level = %d\n",
				current_log_level, vendor_input);
	},
	{
		int input;
		xiaomi_touch_driver_param_t *xiaomi_touch_driver_param =
			get_xiaomi_touch_driver_param(TOUCH_ID);

		/* _b581-XT①（站点1）：blob @0x8dac 形态 =
		 *   p = get_xiaomi_touch_driver_param(0)           （单次取参数，w0=0=TOUCH_ID）
		 *   sscanf(buf, "%d %d"(0x2c11), &input@sp+4, &vendor_input 全局@.data+0x528)
		 *   current_log_level = input                      （str w8→current_log_level）
		 *   if (p && p->ops[+0x1f0]=touch_log_level_control_v2) op(vendor_input)
		 * 树侧原为双面板 for 循环（get_xiaomi_touch_driver_param×2）+ 先试
		 * touch_log_level_control(vendor_input>=MI_TP_LOG_DEBUG?true:false)、
		 * 失败再试 v2——blob 无 touch_log_level_control 调用点，按 blob 收口。 */
		if (sscanf(buf, "%d %d", &input, &vendor_input) < 0)
			return -EINVAL;
		current_log_level = input;
		if (xiaomi_touch_driver_param &&
				xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2)
			xiaomi_touch_driver_param->hardware_operation.touch_log_level_control_v2(vendor_input);
		return count;
	});

static struct attribute *touch_attr_group[] = {
#ifdef TOUCH_FOD_SUPPORT
	&dev_attr_fod_press_status.attr,
	&dev_attr_fod_test.attr,
		#endif
#ifdef TOUCH_STYLUS_SUPPORT
	&dev_attr_pen_connect_strategy.attr,
#endif
	&dev_attr_panel_vendor.attr,
	&dev_attr_panel_color.attr,
	&dev_attr_panel_display.attr,
	&dev_attr_touch_vendor.attr,
	&dev_attr_touch_doze_analysis.attr,
	&dev_attr_touch_ic_buffer.attr,
	&dev_attr_resolution_factor.attr,
	&dev_attr_abnormal_event.attr,
	&dev_attr_enable_touch_raw.attr,
	&dev_attr_palm_sensor.attr,
			&dev_attr_touch_thp_ic_cmd_data.attr,
	&dev_attr_touch_thp_ic_cmd.attr,
	&dev_attr_touch_finger_status.attr,
	&dev_attr_scp_tp_mistouch_enable.attr,
	&dev_attr_touch_log_level.attr,
#ifdef DFS_DEBUG_TEST
	&dev_attr_touch_dfs_test.attr,
#endif
	NULL,
};

int xiaomi_touch_sys_init(void)
{
	int ret = 0;
	LOG_ALWAYS("enter");
	xiaomi_touch_class = class_create("touch");

	if (!xiaomi_touch_class) {
		LOG_ERROR("[Probe Failed] create device class err");
		return -1;
	}

	xiaomi_touch_dev = device_create(xiaomi_touch_class, NULL, 'T', NULL, "touch_dev");
	if (!xiaomi_touch_dev) {
		LOG_ERROR("[Probe Failed] create device dev err");
		return -1;
	}

	xiaomi_touch_attrs.attrs = touch_attr_group;
	ret = sysfs_create_group(&xiaomi_touch_dev->kobj, &xiaomi_touch_attrs);
	if (ret)
		LOG_ERROR("[Probe Failed] Cannot create sysfs structure!:%d", ret);

	return ret;
}

int xiaomi_touch_sys_remove(void)
{

	if (xiaomi_touch_dev) {
		sysfs_remove_group(&xiaomi_touch_dev->kobj, &xiaomi_touch_attrs);
		device_destroy(xiaomi_touch_class, 'T');
		xiaomi_touch_dev = NULL;
	}

	if (xiaomi_touch_class) {
		class_destroy(xiaomi_touch_class);
		xiaomi_touch_class = NULL;
	}

	return 0;
}
