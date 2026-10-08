/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*****************************************************************************
*
* File Name: focaltech_gestrue.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_core.h"

/******************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define KEY_GESTURE_U                           KEY_U
#define KEY_GESTURE_UP                          KEY_UP
#define KEY_GESTURE_DOWN                        KEY_DOWN
#define KEY_GESTURE_LEFT                        KEY_LEFT
#define KEY_GESTURE_RIGHT                       KEY_RIGHT
#define KEY_GESTURE_O                           KEY_O
#define KEY_GESTURE_E                           KEY_E
#define KEY_GESTURE_M                           KEY_M
#define KEY_GESTURE_L                           KEY_L
#define KEY_GESTURE_W                           KEY_W
#define KEY_GESTURE_S                           KEY_S
#define KEY_GESTURE_V                           KEY_V
#define KEY_GESTURE_C                           KEY_C
#define KEY_GESTURE_Z                           KEY_Z

#define GESTURE_LEFT                            0x20
#define GESTURE_RIGHT                           0x21
#define GESTURE_UP                              0x22
#define GESTURE_DOWN                            0x23
#define GESTURE_DOUBLECLICK                     0x24
#define GESTURE_SINGLETAP                       0x25
#define GESTURE_O                               0x30
#define GESTURE_W                               0x31
#define GESTURE_M                               0x32
#define GESTURE_E                               0x33
#define GESTURE_L                               0x44
#define GESTURE_S                               0x46
#define GESTURE_V                               0x54
#define GESTURE_Z                               0x41
#define GESTURE_C                               0x34
#define GESTURE_WEAKDOUBLECLICK                 0x50

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
/*
* gesture_id    - mean which gesture is recognised
* point_num     - points number of this gesture
* coordinate_x  - All gesture point x coordinate
* coordinate_y  - All gesture point y coordinate
* mode          - gesture enable/disable, need enable by host
*               - 1:enable gesture function(default)  0:disable
* active        - gesture work flag,
*                 always set 1 when suspend, set 0 when resume
*/
struct fts_gesture_st {
    u8 gesture_id;
    u8 point_num;
    u16 coordinate_x[FTS_GESTURE_POINTS_MAX];
    u16 coordinate_y[FTS_GESTURE_POINTS_MAX];
};

/*****************************************************************************
* Static variables
*****************************************************************************/
static struct fts_gesture_st fts_gesture_data;

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static ssize_t fts_gesture_mode_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    mutex_lock(&ts_data->input_dev->mutex);
    fts_read_reg(FTS_REG_GESTURE_EN, &val);
    count = snprintf(buf, PAGE_SIZE, "Gesture Mode:%s\n",
                     ts_data->gesture_support ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Reg(0xD0)=%d\n", val);
    mutex_unlock(&ts_data->input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_mode_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    mutex_lock(&ts_data->input_dev->mutex);
    if (FTS_SYSFS_ECHO_ON(buf)) {
        FTS_DEBUG("enable gesture");
        ts_data->gesture_support = ENABLE;
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        FTS_DEBUG("disable gesture");
        ts_data->gesture_support = DISABLE;
    }
    mutex_unlock(&ts_data->input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_buf_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    int i = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);
    struct input_dev *input_dev = ts_data->input_dev;
    struct fts_gesture_st *gesture = &fts_gesture_data;

    mutex_lock(&input_dev->mutex);
    count = snprintf(buf, PAGE_SIZE, "Gesture ID:%d\n", gesture->gesture_id);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture PointNum:%d\n",
                      gesture->point_num);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture Points Buffer:\n");

    /* save point data,max:6 */
    for (i = 0; i < FTS_GESTURE_POINTS_MAX; i++) {
        count += snprintf(buf + count, PAGE_SIZE, "%3d(%4d,%4d) ", i,
                          gesture->coordinate_x[i], gesture->coordinate_y[i]);
        if ((i + 1) % 4 == 0)
            count += snprintf(buf + count, PAGE_SIZE, "\n");
    }
    count += snprintf(buf + count, PAGE_SIZE, "\n");
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_buf_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    return -EPERM;
}

static ssize_t fts_gesture_bm_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    mutex_lock(&ts_data->input_dev->mutex);
    count = snprintf(buf, PAGE_SIZE, "gesture bmode:%d\n",
                     ts_data->gesture_bmode);
    mutex_unlock(&ts_data->input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_bm_store(
	struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct fts_ts_data *ts_data = dev_get_drvdata(dev);
	int value = 0xFF;

	mutex_lock(&ts_data->input_dev->mutex);
	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	FTS_DEBUG("gesture bmode:%d->%d", ts_data->gesture_bmode, value);
	ts_data->gesture_bmode = value;
	mutex_unlock(&ts_data->input_dev->mutex);
	return count;
}

/* sysfs gesture node
 *   read example: cat  fts_gesture_mode       ---read gesture mode
 *   write example:echo 1 > fts_gesture_mode   --- write gesture mode to 1
 *
 */
static DEVICE_ATTR_RW(fts_gesture_mode);
/*
 *   read example: cat fts_gesture_buf        --- read gesture buf
 */

static DEVICE_ATTR_RW(fts_gesture_buf);
static DEVICE_ATTR_RW(fts_gesture_bm);

static struct attribute *fts_gesture_mode_attrs[] = {
    &dev_attr_fts_gesture_mode.attr,
    &dev_attr_fts_gesture_buf.attr,
    &dev_attr_fts_gesture_bm.attr,
    NULL,
};

static struct attribute_group fts_gesture_group = {
    .attrs = fts_gesture_mode_attrs,
};

static int fts_create_gesture_sysfs(struct device *dev)
{
    int ret = 0;

    ret = sysfs_create_group(&dev->kobj, &fts_gesture_group);
    if (ret) {
        FTS_ERROR("gesture sys node create fail");
        sysfs_remove_group(&dev->kobj, &fts_gesture_group);
        return ret;
    }

    return 0;
}

static void fts_gesture_report(struct input_dev *input_dev, int gesture_id)
{
    int gesture;

    /* _b583-FTS（A2）：blob fts_gesture_readdata 内联 fts_gesture_report 段
	 * （0xE9CC..0xE9E8，__func__ = 0xEEFE "fts_gesture_report"）只有 "Gesture Code=%d"
	 * 一条 DEBUG；"gesture_id:0x%x" 为树侧独有串（classify_intD ③树）⇒ 删。 */
    switch (gesture_id) {
    case GESTURE_LEFT:
        gesture = KEY_GESTURE_LEFT;
        break;
    case GESTURE_RIGHT:
        gesture = KEY_GESTURE_RIGHT;
        break;
    case GESTURE_UP:
        gesture = KEY_GESTURE_UP;
        break;
    case GESTURE_DOWN:
        gesture = KEY_GESTURE_DOWN;
        break;

    case GESTURE_DOUBLECLICK:
	gesture = KEY_WAKEUP;
	break;
    case GESTURE_SINGLETAP:
	gesture = KEY_GOTO;
	break;
    case GESTURE_O:
        gesture = KEY_GESTURE_O;
        break;
    case GESTURE_W:
        gesture = KEY_GESTURE_W;
        break;
    case GESTURE_M:
        gesture = KEY_GESTURE_M;
        break;
    case GESTURE_E:
        gesture = KEY_GESTURE_E;
        break;
    case GESTURE_L:
        gesture = KEY_GESTURE_L;
        break;
    case GESTURE_S:
        gesture = KEY_GESTURE_S;
        break;
    case GESTURE_V:
        gesture = KEY_GESTURE_V;
        break;
    case GESTURE_Z:
        gesture = KEY_GESTURE_Z;
        break;
    case  GESTURE_C:
        gesture = KEY_GESTURE_C;
        break;
    default:
        gesture = -1;
        break;
    }
    /* report event key */
    if (gesture != -1) {
        FTS_DEBUG("Gesture Code=%d", gesture);
        input_report_key(input_dev, gesture, 1);
        input_sync(input_dev);
        input_report_key(input_dev, gesture, 0);
        input_sync(input_dev);
    }
}

/*****************************************************************************
* Name: fts_gesture_readdata
* Brief: Read information about gesture: enable flag/gesture points..., if ges-
*        ture enable, save gesture points' information, and report to OS.
*        It will be called this function every intrrupt when FTS_GESTURE_EN = 1
*
*        gesture data length: 1(enable) + 1(reserve) + 2(header) + 6 * 4
* Input: ts_data - global struct data
*        data    - gesture data buffer
* Output:
* Return: 0 - read gesture data successfully, the report data is gesture data
*         1 - tp not in suspend/gesture not enable in TP FW
*         -Exx - error
*****************************************************************************/
int fts_gesture_readdata(struct fts_ts_data *ts_data, u8 *touch_buf)
{
	int ret = 0;
	int i = 0;
	int index = 0;
	u8 buf[FTS_GESTURE_DATA_LEN] = { 0 };
	struct input_dev *input_dev = ts_data->input_dev;
	struct fts_gesture_st *gesture = &fts_gesture_data;
	if (!ts_data->gesture_support) {
		FTS_ERROR("gesture no support");
		return -EINVAL;
	}
	/* _b583-FTS（A2）：blob 0xE5CC 头部按 IDA/objdump 逐点补齐：
	 *   0xE61C `cmp w8,#3; b.hs 0xE780` → 0xE780 = _printk(.rodata.str1.1+0x6065
	 *     b'\x016[FTS_TS_I][%s:%d]: scptp_cur_state=%d\n'（classify ②b：blob 有树无，
	 *     它在 blob 全模块**只**被本函数引用，refs 0xE784/0xE788）实参 = scp_tp_param.param0；
	 *   0xE628 `ldr w8,[scp_tp_param]; cmp w8,#3` → 0xE634-0xE658 从 scp_tp_param+0x30
	 *     （= gesture_data）拷 26B（ldp+ldp+ldrh = 0x1A）到 buf；
	 *   0xE674 `cbz x20, 0xE6BC` = touch_buf == NULL 时跳过拷贝（树侧 focal_scp_gesture
	 *     正是以 NULL 调用，原码会解引用 touch_buf+0xC30）。 */
	FTS_INFO("scptp_cur_state=%d\n", fts_scp_tp_param.param0);
	if (fts_scp_tp_param.param0 == 3) {
		memcpy(buf, fts_scp_tp_param.gesture_data, FTS_GESTURE_DATA_LEN);
	} else if (ts_data->gesture_bmode == GESTURE_BM_TOUCH) {
		if (touch_buf)
			memcpy(buf, touch_buf + FTS_TOUCH_DATA_LEN, FTS_GESTURE_DATA_LEN);
	} else {
		buf[2] = FTS_REG_GESTURE_OUTPUT_ADDRESS;
		ret = fts_read(&buf[2], 1, &buf[2], FTS_GESTURE_DATA_LEN - 2);
		if (ret < 0) {
			FTS_ERROR("read gesture header data fail");
			return ret;
		}
	}
	/* init variable before read gesture point */
	memset(gesture->coordinate_x, 0, FTS_GESTURE_POINTS_MAX * sizeof(u16));
	memset(gesture->coordinate_y, 0, FTS_GESTURE_POINTS_MAX * sizeof(u16));
	gesture->gesture_id = buf[2];
	gesture->point_num = buf[3];
	if (gesture->gesture_id == GESTURE_DOUBLECLICK && !(ts_data->gesture_status & 0x01)) {
		/* _b583-FTS（A2）：blob 串 .rodata.str1.1+0xB08F =
		 * b'\x016[FTS_TS_I][%s:%d]: double click is not enabled!'（无 ", gesture_status:%d"、
		 * 无第 3 实参；classify ④ 对体 r=0.76）⇒ 逐字节收口。 */
		FTS_INFO("double click is not enabled!");
		return 1;
	}
	if (gesture->gesture_id == GESTURE_SINGLETAP && !(ts_data->gesture_status & 0x02)) {
		/* _b583-FTS（A2）：blob 0xE71C-0xE7BC 逐点 ——
		 *   0xE724/0xE72C `mov w0,#0; mov w1,#0xA / #0x11; bl driver_get_touch_mode_common`
		 *     ⇒ fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10)（新查框架，
		 *     不是读 ts_data->fod_status 缓存）；
		 *   0xE738 第二个调用 = nonui_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_17)；
		 *   0xE740 `cbz w0`（nonui == 0 才继续）、0xE7A0 `cmn w20,#1 / cmp w20,#0x64`
		 *     ⇒ 只排除 -1 与 100（**无** !=0 项）；
		 *   0xEA0C = _printk(.rodata.str1.1+0x173E b'…: FOD on support single tap')，无参数；
		 *   0xE7BC 之后树侧独有两处一并删（classify ③树）：
		 *     "gesture_id=%x; DoubleClick:0x24  SingleTap:0x25 WeakDoubleClick:0x50"
		 *     与 update_weak_doubletap_value(1)（blob 符号面 UND 表 + 全 ko 串面均无
		 *     update_weak_doubletap_value）。 */
		int fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
		int nonui_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_17);

		if (nonui_status == 0 && fod_status != -1 && fod_status != 100) {
			FTS_INFO("FOD on support single tap");
		} else {
			FTS_INFO("single tap is not enabled!");
			return 1;
		}
	}
	/* save point data,max:6 */
	for (i = 0; i < FTS_GESTURE_POINTS_MAX; i++) {
		index = 4 * i + 4;
		gesture->coordinate_x[i] = (u16)(((buf[0 + index] & 0x0F) << 8)
										 + buf[1 + index]);
		gesture->coordinate_y[i] = (u16)(((buf[2 + index] & 0x0F) << 8)
										 + buf[3 + index]);
	}
	/* report gesture to OS */
	fts_gesture_report(input_dev, gesture->gesture_id);
	return 0;
}


void fts_gesture_recovery(struct fts_ts_data *ts_data)
{
    if (ts_data->gesture_support && ts_data->suspended) {
        FTS_DEBUG("gesture recovery...");
        // fts_write_reg(0xD1, 0xFF);
        // fts_write_reg(0xD2, 0xFF);
        // fts_write_reg(0xD5, 0xFF);
        // fts_write_reg(0xD6, 0xFF);
        // fts_write_reg(0xD7, 0xFF);
        // fts_write_reg(0xD8, 0xFF);
        fts_write_reg(FTS_GESTURE_CTRL, ts_data->gesture_cmd);
        fts_write_reg(FTS_REG_GESTURE_EN, ENABLE);
#ifdef FTS_TOUCHSCREEN_FOD
        if (ts_data->fod_status != -1 && ts_data->fod_status != 0) {
            fts_fod_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, true);
        }
#endif
    }
}

void fts_fod_recovery(void)
{
	FTS_FUNC_ENTER();
	if (fts_data->suspended) {
		/* _b583-FTS（A1）：按 blob / IDA 0xEDA0 收口 —— 0xEDDC = `mov w0,#1 / mov w1,#1 /
		 * bl fts_gesture_reg_write` 之后**直接** fts_fod_reg_write（0xEDF0），全函数**无**
		 * fts_write_reg（callface only-tree={'fts_write_reg':1}）；串实证
		 * .rodata.str1.1+0x3B21 = b'\x016[FTS_TS_I][%s:%d]: %s, tp is in suspend mode,
		 * write 0xD0 to 1'（无 "0xD1 to 0x%x" 尾段、无第 3 实参 w4）⇒ 删 0xD1 写 + 串收短。 */
		FTS_INFO("%s, tp is in suspend mode, write 0xD0 to 1", __func__);
		fts_gesture_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, true);
	}
	fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, true);
	FTS_FUNC_EXIT();
}

int fts_fod_reg_write(u8 mask, bool enable)
{
	int i;
	u8 state;
	u8 reg_value;
	u8 reg_value_last_time;
	for (i = 0; i < 5; i++) {
		fts_read_reg(FTS_REG_GESTURE_SUPPORT, &reg_value);
		reg_value_last_time = reg_value;
		if (enable)
			reg_value |= mask;
		else
			reg_value &= ~mask;
		/* If the value in the register is equal to the modified value, skip writing to the register */
		if (reg_value == reg_value_last_time) {
			/* _b582-INTA：族对齐 D→I（blob 0x11965 '\0016[FTS_TS_I][%s:%d]: reg 0xCF
			 * do not need to be modified, reg_value = %02X'，blob 侧为死串（无引用）） */
			FTS_INFO("reg 0xCF do not need to be modified, reg_value = %02X", reg_value);
			return 0;
		}
		fts_write_reg(FTS_REG_GESTURE_SUPPORT, reg_value);
		msleep(1);
		fts_read_reg(FTS_REG_GESTURE_SUPPORT, &state);
		if (state == reg_value)
			break;
	}
	if (i >= 5) {
		FTS_ERROR("[GESTURE]Write fod reg failed! fod reg status: %d\n", enable);
		return -EIO;
	}
	/* _b582-INTA：族对齐 D→I（blob 0x27c '\0016[FTS_TS_I][%s:%d]: [GESTURE]Write fod reg
	 * success! fod reg status: %d\n'，引用点 fts_fod_reg_write+0x25c） */
	FTS_INFO("[GESTURE]Write fod reg success! fod reg status: %d\n", enable);
	return 0;
}

int fts_gesture_reg_write(u8 mask, bool enable)
{
	int i;
	u8 state;
	u8 reg_value;
	for (i = 0; i < 5; i++) {
		fts_read_reg(FTS_REG_GESTURE_EN, &reg_value);
		if (enable)
			reg_value |= mask;
		else
			reg_value &= ~mask;
		fts_write_reg(FTS_REG_GESTURE_EN, reg_value);
		msleep(1);
		fts_read_reg(FTS_REG_GESTURE_EN, &state);
		if (state == reg_value)
			break;
	}
	if (i >= 5) {
		FTS_ERROR("[GESTURE]Write gesture reg failed!\n");
		return -EIO;
	}
	FTS_INFO("[GESTURE]Write gesture reg success!\n");
	return 0;
}

int fts_gesture_suspend(struct fts_ts_data *ts_data)
{
	int i = 0;
	int ret;
	u8 state = 0xFF;
    FTS_FUNC_ENTER();
	/* _b583-FTS（A5）：blob 0xF2D4 起 = **无条件** `LDR W0,[X19,#0x2A8]; MOV W1,#1;
	 * BL irq_set_irq_wake` + 失败打印（0xF42C，门 debug>=4，串 "enable_irq_wake(irq:%d) fail"）
	 * —— 全 ko 无 ts_data->irq_wake 读写（0x2DA..0x2DD 布尔区实证：0x2DA=fw_loading、
	 * 0x2DB=irq_disabled、0x2DC=power_disabled，无第四槽）⇒ 删护栏与成员（见 core.h B9）。 */
	if (irq_set_irq_wake(ts_data->irq, 1)) {
		if (fts_debug_log_level >= 4)
			FTS_DEBUG("enable_irq_wake(irq:%d) fail", ts_data->irq);
	}

	for (i = 0; i < 5; i++) {
		/* _b583-FTS（A5）：blob 循环体 0xF2F4-0xF358 = 6 次 0xFF 写 0xD1/0xD2/0xD5/0xD6/
		 * 0xD7/0xD8 + fts_write_reg(FTS_REG_GESTURE_EN(0xD0), ENABLE) + msleep(1) +
		 * fts_read_reg(0xD0,&state)（callface blob fts_write_reg=7 / msleep=1 / read=1，
		 * 树侧因把 6 写注释掉且改 0xD1←gesture_cmd 而 10/5/5）⇒ 按 blob 恢复 6 写、
		 * 删除 gesture_cmd 写。 */
		fts_write_reg(0xD1, 0xFF);
		fts_write_reg(0xD2, 0xFF);
		fts_write_reg(0xD5, 0xFF);
		fts_write_reg(0xD6, 0xFF);
		fts_write_reg(0xD7, 0xFF);
		fts_write_reg(0xD8, 0xFF);
		fts_write_reg(FTS_REG_GESTURE_EN, ENABLE);
		msleep(1);
		fts_read_reg(FTS_REG_GESTURE_EN, &state);
		if (state == ENABLE)
			break;
	}
#ifdef FTS_TOUCHSCREEN_FOD
	if (ts_data->fod_status != -1 && ts_data->fod_status != 0) {
		ret = fts_fod_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, true);
		if (ret) {
			FTS_ERROR("[GESTURE]Enter into gesture(suspend) failed!\n");
			// fts_gesture_data.active = DISABLE;
			return -EIO;
		}
	}
#endif
	if (i >= 5)
		FTS_ERROR("make IC enter into gesture(suspend) fail,state:%x", state);
	else
		FTS_INFO("Enter into gesture(suspend) successfully");
	FTS_FUNC_EXIT();
	return 0;
}

int fts_gesture_resume(struct fts_ts_data *ts_data)
{
	int i = 0;
	int ret;
	u8 state = 0xFF;
	FTS_FUNC_ENTER();
	/* _b583-FTS（A5b）：blob 0x184B8 起 = 无条件 `LDR W0,[X19,#0x2A8]; MOV W1,#0;
	 * BL irq_set_irq_wake` + 失败打印 0x18684（串 "disable_irq_wake(irq:%d) fail"，门 >=4）
	 * —— 这是 callface `fts_gesture_resume only-blob={_printk:1}` 的唯一来源 ⇒ 按 blob 收口。 */
	if (irq_set_irq_wake(ts_data->irq, 0)) {
		if (fts_debug_log_level >= 4)
			FTS_DEBUG("disable_irq_wake(irq:%d) fail", ts_data->irq);
	}

	for (i = 0; i < 5; i++) {
		fts_write_reg(FTS_REG_GESTURE_EN, DISABLE);
		msleep(1);
		fts_read_reg(FTS_REG_GESTURE_EN, &state);
		if (state == DISABLE)
			break;
	}
#ifdef FTS_TOUCHSCREEN_FOD
	ret = fts_fod_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, false);
	if (ret) {
		FTS_ERROR("[GESTURE]resume from gesture(suspend) failed!\n");
		return -EIO;
	}
#endif
	if (i >= 5)
		FTS_ERROR("make IC exit gesture(resume) fail,state:%x", state);
	else
		FTS_INFO("resume from gesture successfully");
	FTS_FUNC_EXIT();
	return 0;
}


int fts_gesture_init(struct fts_ts_data *ts_data)
{
    struct input_dev *input_dev = ts_data->input_dev;

    FTS_FUNC_ENTER();
    input_set_capability(input_dev, EV_KEY, KEY_POWER);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_U);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_L);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_C);

    __set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
    __set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
    __set_bit(KEY_GESTURE_UP, input_dev->keybit);
    __set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
    __set_bit(KEY_GESTURE_U, input_dev->keybit);
    __set_bit(KEY_GESTURE_O, input_dev->keybit);
    __set_bit(KEY_GESTURE_E, input_dev->keybit);
    __set_bit(KEY_GESTURE_M, input_dev->keybit);
    __set_bit(KEY_GESTURE_W, input_dev->keybit);
    __set_bit(KEY_GESTURE_L, input_dev->keybit);
    __set_bit(KEY_GESTURE_S, input_dev->keybit);
    __set_bit(KEY_GESTURE_V, input_dev->keybit);
    __set_bit(KEY_GESTURE_C, input_dev->keybit);
    __set_bit(KEY_GESTURE_Z, input_dev->keybit);

    fts_create_gesture_sysfs(ts_data->dev);

    memset(&fts_gesture_data, 0, sizeof(struct fts_gesture_st));
    ts_data->gesture_bmode = GESTURE_BM_REG;
    ts_data->gesture_support = FTS_GESTURE_EN;

    if ((ts_data->ic_info.ids.type <= 0x25)
        || (ts_data->ic_info.ids.type == 0x87)
        || (ts_data->ic_info.ids.type == 0x88)) {
        FTS_INFO("ic type:0x%02x,GESTURE_BM_TOUCH", ts_data->ic_info.ids.type);
        ts_data->touch_size += FTS_GESTURE_DATA_LEN;
        ts_data->gesture_bmode = GESTURE_BM_TOUCH;
    }

    FTS_FUNC_EXIT();
    return 0;
}

int fts_gesture_exit(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    sysfs_remove_group(&ts_data->dev->kobj, &fts_gesture_group);
    FTS_FUNC_EXIT();
    return 0;
}
