/*
 *
 * FocalTech ftxxxx TouchScreen driver.
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
* File Name: focaltech_ex_mode.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-31
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

/*****************************************************************************
* 2.Private constant and macro definitions using #define
*****************************************************************************/

/*****************************************************************************
* 3.Private enumerations, structures and unions using typedef
*****************************************************************************/
enum _ex_mode {
    MODE_GLOVE = 0,
    MODE_COVER,
    MODE_CHARGER,
};

/*****************************************************************************
* 4.Static variables
*****************************************************************************/

/*****************************************************************************
* 5.Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* 6.Static function prototypes
*******************************************************************************/
/* _b582-INTA：A-80④② ex_mode helper 化 —— blob 的 MODE_CHARGER 走 fts_charger_on
 * （blob fts_charger_mode_store+0x54/+0x94 = `bl fts_charger_on`，实参 (fts_data, 1/0)；
 * 串 "MODE_CHARGER switch to %d fail" 的 __func__ = "fts_ex_mode_switch" 证明该 switch
 * 仍是源码里的独立函数），且 blob 全 ko **无** fts_ex_mode_switch 符号（3 个 store +
 * recovery 里全内联）⇒ 此处
 *   ① MODE_CHARGER 分支 fts_write_reg(0x8B,m_val) → fts_charger_on(fts_data, m_val)；
 *   ② 与 core.c `fts_ts_suspend/resume` 同款加 always_inline，使 4 个调用点的被调面
 *      （fts_write_reg ×2 / fts_charger_on / 各 E 打印）与 blob 一致。
 * 证据：tools/_b581_fts/callface_post.txt fts_charger_mode_store
 *   only-blob={_printk:2, fts_charger_on:2} / only-tree={fts_ex_mode_switch:2}；
 *   fts_cover/glove_mode_store only-blob={_printk:2, fts_write_reg:2}。 */
static __attribute__((always_inline)) inline int fts_ex_mode_switch(enum _ex_mode mode, u8 value)
{
    int ret = 0;
    u8 m_val = 0;

    if (value)
        m_val = 0x01;
    else
        m_val = 0x00;

    switch (mode) {
    case MODE_GLOVE:
        ret = fts_write_reg(FTS_REG_GLOVE_MODE_EN, m_val);
        if (ret < 0) {
            FTS_ERROR("MODE_GLOVE switch to %d fail", m_val);
        }
        break;
    case MODE_COVER:
        ret = fts_write_reg(FTS_REG_COVER_MODE_EN, m_val);
        if (ret < 0) {
            FTS_ERROR("MODE_COVER switch to %d fail", m_val);
        }
        break;
    case MODE_CHARGER:
        /* blob：0x8B 由 fts_charger_on 统一下发（同一寄存器 + D 级成功打印） */
        ret = fts_charger_on(fts_data, m_val);
        if (ret < 0) {
            FTS_ERROR("MODE_CHARGER switch to %d fail", m_val);
        }
        break;
    default:
        FTS_ERROR("mode(%d) unsupport", mode);
        ret = -EINVAL;
        break;
    }

    return ret;
}

static ssize_t fts_glove_mode_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);
    struct input_dev *input_dev = ts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_read_reg(FTS_REG_GLOVE_MODE_EN, &val);
    count = snprintf(buf + count, PAGE_SIZE, "Glove Mode:%s\n",
                     ts_data->glove_mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Glove Reg(0xC0):%d\n", val);
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_glove_mode_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    if (FTS_SYSFS_ECHO_ON(buf)) {
        if (!ts_data->glove_mode) {
            FTS_DEBUG("enter glove mode");
            ret = fts_ex_mode_switch(MODE_GLOVE, ENABLE);
            if (ret >= 0) {
                ts_data->glove_mode = ENABLE;
            }
        }
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        if (ts_data->glove_mode) {
            FTS_DEBUG("exit glove mode");
            ret = fts_ex_mode_switch(MODE_GLOVE, DISABLE);
            if (ret >= 0) {
                ts_data->glove_mode = DISABLE;
            }
        }
    }

    FTS_DEBUG("glove mode:%d", ts_data->glove_mode);
    return count;
}


static ssize_t fts_cover_mode_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);
    struct input_dev *input_dev = ts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_read_reg(FTS_REG_COVER_MODE_EN, &val);
    count = snprintf(buf + count, PAGE_SIZE, "Cover Mode:%s\n",
                     ts_data->cover_mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Cover Reg(0xC1):%d\n", val);
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_cover_mode_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    if (FTS_SYSFS_ECHO_ON(buf)) {
        if (!ts_data->cover_mode) {
            FTS_DEBUG("enter cover mode");
            ret = fts_ex_mode_switch(MODE_COVER, ENABLE);
            if (ret >= 0) {
                ts_data->cover_mode = ENABLE;
            }
        }
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        if (ts_data->cover_mode) {
            FTS_DEBUG("exit cover mode");
            ret = fts_ex_mode_switch(MODE_COVER, DISABLE);
            if (ret >= 0) {
                ts_data->cover_mode = DISABLE;
            }
        }
    }

    FTS_DEBUG("cover mode:%d", ts_data->cover_mode);
    return count;
}

static ssize_t fts_charger_mode_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);
    struct input_dev *input_dev = ts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_read_reg(FTS_REG_CHARGER_MODE_EN, &val);
    count = snprintf(buf + count, PAGE_SIZE, "Charger Mode:%s\n",
                     ts_data->charger_mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Charger Reg(0x8B):%d\n", val);
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_charger_mode_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    int ret = 0;
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    if (FTS_SYSFS_ECHO_ON(buf)) {
        if (!ts_data->charger_mode) {
            FTS_DEBUG("enter charger mode");
            ret = fts_ex_mode_switch(MODE_CHARGER, ENABLE);
            if (ret >= 0) {
                ts_data->charger_mode = ENABLE;
            }
        }
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        if (ts_data->charger_mode) {
            FTS_DEBUG("exit charger mode");
            ret = fts_ex_mode_switch(MODE_CHARGER, DISABLE);
            if (ret >= 0) {
                ts_data->charger_mode = DISABLE;
            }
        }
    }

    FTS_DEBUG("charger mode:%d", ts_data->glove_mode);
    return count;
}


/* read and write charger mode
 * read example: cat fts_glove_mode        ---read  glove mode
 * write example:echo 1 > fts_glove_mode   ---write glove mode to 01
 */

static DEVICE_ATTR_RW(fts_glove_mode);
static DEVICE_ATTR_RW(fts_cover_mode);
static DEVICE_ATTR_RW(fts_charger_mode);

static struct attribute *fts_touch_mode_attrs[] = {
    &dev_attr_fts_glove_mode.attr,
    &dev_attr_fts_cover_mode.attr,
    &dev_attr_fts_charger_mode.attr,
    NULL,
};

static struct attribute_group fts_touch_mode_group = {
    .attrs = fts_touch_mode_attrs,
};

int fts_ex_mode_recovery(struct fts_ts_data *ts_data)
{
    if (ts_data->glove_mode) {
        fts_ex_mode_switch(MODE_GLOVE, ENABLE);
    }

    if (ts_data->cover_mode) {
        fts_ex_mode_switch(MODE_COVER, ENABLE);
    }

    if (ts_data->charger_mode) {
        fts_ex_mode_switch(MODE_CHARGER, ENABLE);
    }

    return 0;
}

int fts_ex_mode_init(struct fts_ts_data *ts_data)
{
    int ret = 0;

    ts_data->glove_mode = DISABLE;
    ts_data->cover_mode = DISABLE;
    ts_data->charger_mode = DISABLE;

    ret = sysfs_create_group(&ts_data->dev->kobj, &fts_touch_mode_group);
    if (ret < 0) {
        FTS_ERROR("create sysfs(ex_mode) fail");
        sysfs_remove_group(&ts_data->dev->kobj, &fts_touch_mode_group);
        return ret;
    }

    FTS_DEBUG("create sysfs(ex_mode) successfully");

    return 0;
}

int fts_ex_mode_exit(struct fts_ts_data *ts_data)
{
    sysfs_remove_group(&ts_data->dev->kobj, &fts_touch_mode_group);
    return 0;
}
