/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2020, FocalTech Systems, Ltd., all rights reserved.
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
* File Name: focaltech_core.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract: entrance for focaltech ts driver
*
* Version: V1.0
*
*****************************************************************************/

/*****************************************************************************
* Included header files
*****************************************************************************/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include "focaltech_core.h"

#include <linux/power_supply.h>
#include <uapi/linux/sched/types.h>

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_DRIVER_NAME                     "focaltech_ts"
#define FTS_DRIVER_PEN_NAME                 "fts_ts,pen"
#define INTERVAL_READ_REG                   200  /* unit:ms */
#define TIMEOUT_READ_REG                    1000 /* unit:ms */
#if FTS_POWER_SOURCE_CUST_EN
#define FTS_VTG_MIN_UV                      2800000
#define FTS_VTG_MAX_UV                      3300000
#define FTS_I2C_VTG_MIN_UV                  1800000
#define FTS_I2C_VTG_MAX_UV                  1800000
#endif
/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
struct fts_ts_data *fts_data;
enum FTS_LOG_LEVEL fts_debug_log_level = FTS_LOG_INFO;

#define FOCALTECH_RX_NUM                    9
#define FOCALTECH_TX_NUM                    14
#define SUPER_RESOLUTION_FACOTR             100
/*struct device_node *gf_spi_dp;*/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static int fts_ts_suspend(struct device *dev);
static int fts_ts_resume(struct device *dev);

#ifdef FTS_XIAOMI_TOUCHFEATURE
int fts_ic_data_collect(char *buf, int *length);
int fts_read_and_report_foddata(struct fts_ts_data *data);
static void fts_game_mode_recovery(struct fts_ts_data *ts_data);
static void fts_charger_status_recovery(struct fts_ts_data *ts_data);
static void fts_fod_status_recovery(struct fts_ts_data *ts_data);
static void fts_report_rate_recovery(struct fts_ts_data *ts_data);
static void fts_game_idle_high_refresh_recovery(struct fts_ts_data *ts_data);
/*static int fts_change_fps(void *data);*/
static void fts_recover_gesture_from_sleep(struct fts_ts_data *data);
// extern void touch_irq_boost(void);
#ifdef CONFIG_TOUCH_BOOST
extern void lpm_disable_for_dev(bool on, char event_dev);
#define LPM_EVENT_INPUT 0x1
#endif
#define ORIENTATION_0_OR_180	0	/* anticlockwise 0 or 180 degrees */
#define NORMAL_ORIENTATION_90	1	/* anticlockwise 90 degrees in normal */
#define NORMAL_ORIENTATION_270	2	/* anticlockwise 270 degrees in normal */
#define GAME_ORIENTATION_90	3	/* anticlockwise 90 degrees in game */
#define GAME_ORIENTATION_270	4	/* anticlockwise 270 degrees in game */
#endif

#ifdef CRC_CHECK
#if defined(SOC_LITTLE_ENDIAN) && SOC_LITTLE_ENDIAN
/*static u32 buf_len[37] = {1,1,2,4,4,4,4,1,1,1,1,1,1,2,2,2,2,2,2,2,4,2,2,1,1,2,2,1,1,1,2,1,2,2,((ROW_NUM_MAX +2) * COL_NUM_NAX * 2 + 4 * (ROW_NUM_MAX + COL_NUM_NAX + 2)),24,24};*/
static u32 buf_len[91] = {1,1,2,4,4,4,4,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,1,1,1,1,1,1,2,2,((ROW_NUM_MAX +2) * COL_NUM_MAX * 2 + 4 * (ROW_NUM_MAX + COL_NUM_MAX + 2)),\
                          1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
#endif

#define CRC32_POLYNOMIAL         0xE89061DB
#endif

#define TPDEBUG_IN_D
#ifndef TPDEBUG_IN_D
static struct proc_dir_entry *touch_debug;
#endif /* TPDEBUG_IN_D */

static int htc_ic_mode = 0;
static bool ic_in_selftest = 0;

int fts_check_cid(struct fts_ts_data *ts_data, u8 id_h)
{
        int i = 0;
        struct ft_chip_id_t *cid = &ts_data->ic_info.cid;
        u8 cid_h = 0x0;

        if (cid->type == 0)
                return -ENODATA;

        for (i = 0; i < FTS_MAX_CHIP_IDS; i++) {
                cid_h = ((cid->chip_ids[i] >> 8) & 0x00FF);
                if (cid_h && (id_h == cid_h)) {
                        return 0;
        }
    }

        return -ENODATA;
}

#ifdef CRC_CHECK
int32_t thp_crc32_check(char *s32_message, int s32_len)
{
    int i;
//    int j;
    int k;
    int s32_remainder;
    unsigned char u8_byteData;
    s32_remainder = 0UL;

    for (i = 0; i < s32_len; i++) {
//        for (j = 3; j >= 0; j--) {
//            u8_byteData = s32_message[i] >> (j * 8);
		    u8_byteData = s32_message[i] ;//>> (j * 8);
            s32_remainder ^= (u8_byteData << 24);
            for (k = 8; k > 0; --k) {
                //Try to divide the current data bit
                if (s32_remainder & (1UL << 31)) {
                    s32_remainder = (s32_remainder << 1) ^ CRC32_POLYNOMIAL;
                } else {
                    s32_remainder = (s32_remainder << 1);
                }
            }
//        }
    }
    return s32_remainder;
}

static int32_t tp_thp_crc32_check_int(int s32_message[], int s32_len)
{
    int i;
    int j;
    int k;
    int s32_remainder;
    unsigned char u8_byteData;
    s32_remainder = 0UL;
    for (i = 0; i < s32_len; i++) {
        for (j = 3; j >= 0; j--) {
            //Get the correct byte ordering
            u8_byteData = s32_message[i] >> (j * 8);
            //Bring the next byte into the remainder
            s32_remainder ^= (u8_byteData << 24);
            //Perform modulo-2 division, a bit at a time
            for (k = 8; k > 0; --k) {
                //Try to divide the current data bit
                if (s32_remainder & (1UL << 31)) {
                    s32_remainder = (s32_remainder << 1) ^ CRC32_POLYNOMIAL;
                } else {
                    s32_remainder = (s32_remainder << 1);
                }
            }
        }
    }
    return s32_remainder;
}

static bool fts_frame_parse_data(struct fts_ts_data *ts_data, struct frame_afe_data *thp_data, bool crc_result)
{
    int last_ap_crc = 0;
    if(!thp_data) {
        FTS_ERROR("touch data NULL");
        return TOUCH_ERROR;
    }

    ts_data->frame_data.protocol_type = thp_data->protocol_type;
    ts_data->frame_data.protocol_version = thp_data->protocol_version;
    ts_data->frame_data.head_cnt = thp_data->head_cnt;
    ts_data->frame_data.crc = thp_data->crc;
    ts_data->frame_data.crc_len = thp_data->crc_len;
    ts_data->frame_data.n_crc = thp_data->n_crc;
    ts_data->frame_data.n_crc_len = thp_data->n_crc_len;
    ts_data->frame_data.scan_saturation_state = thp_data->scan_saturation_state;
    ts_data->frame_data.data_type = thp_data->data_type;
    ts_data->frame_data.event_info = thp_data->event_info;
    ts_data->frame_data.noise_lvl = thp_data->noise_lvl;
    ts_data->frame_data.scan_mode = thp_data->scan_mode;
    ts_data->frame_data.scan_rate = thp_data->scan_rate;
    ts_data->frame_data.scan_freq = thp_data->scan_freq;
    ts_data->frame_data.frame_no = thp_data->frame_no;
    ts_data->frame_data.drop_frame_no = thp_data->drop_frame_no;
    ts_data->frame_data.noise_r0 = thp_data->noise_r0;
    ts_data->frame_data.noise_r1 = thp_data->noise_r1;
    ts_data->frame_data.noise_r2 = thp_data->noise_r2;
    ts_data->frame_data.noise_r3 = thp_data->noise_r3;
    ts_data->frame_data.noise_r4 = thp_data->noise_r4;
    ts_data->frame_data.noise_r5 = thp_data->noise_r5;
    ts_data->frame_data.cur_frame_len = thp_data->cur_frame_len;
    ts_data->frame_data.next_frame_len = thp_data->next_frame_len;
    ts_data->frame_data.numCol = thp_data->numCol;
    ts_data->frame_data.numRow = thp_data->numRow;
    ts_data->frame_data.ic_ms_time = thp_data->ic_ms_time;
    ts_data->frame_data.reserved_scan_rate = thp_data->reserved_scan_rate;
    ts_data->frame_data.reserved_scan_freq = thp_data->reserved_scan_freq;
    ts_data->frame_data.write_cmd_cnt = thp_data->write_cmd_cnt;
    ts_data->frame_data.frame_data_type = thp_data->frame_data_type;
    ts_data->frame_data.flg_buf_low = thp_data->flg_buf_low;
    ts_data->frame_data.flg_buf_high = thp_data->flg_buf_high;
    ts_data->frame_data.debug_buf_size = thp_data->debug_buf_size;
    ts_data->frame_data.reserved_big_buf_size = thp_data->reserved_big_buf_size;
    ts_data->frame_data.write_cmd = thp_data->write_cmd;

    memcpy(ts_data->frame_data.mc_data, thp_data->mc_data, (TX_NUM * RX_NUM * 2));
    memcpy(ts_data->frame_data.scap_raw, thp_data->scap_raw, ((TX_NUM + RX_NUM) * 2));
    memcpy(ts_data->frame_data.scap2_raw, thp_data->scap2_raw, ((TX_NUM + RX_NUM) * 2));
    memcpy(ts_data->frame_data.debug_buf, thp_data->debug_buf, DEBUG_BUF_SIZE);
    memcpy(ts_data->frame_data.reserved_big_buf, thp_data->reserved_big_buf, RESERVES_BIG_BUF_SIZE);

    if(crc_result) {
        /*ts_data->frame_data.crc = thp_crc32_check(&ts_data->frame_data.scan_saturation_state, (sizeof(struct frame_thp_data) - 20));
        FTS_INFO("length: %d", (sizeof(struct frame_thp_data)));*/
        /*crc result for thp check*/
        last_ap_crc = tp_thp_crc32_check_int(&((int *)(&(ts_data->frame_data)))[5], (sizeof(struct frame_thp_data)) / 4 - 5);
        ts_data->frame_data.crc = last_ap_crc;
        ts_data->frame_data.n_crc = ~ts_data->frame_data.crc;
        /*FTS_INFO("length of frame_thp_data: %d, frame_data.crc: %d, frame_data.n_crc: %d", (sizeof(struct frame_thp_data)),ts_data->frame_data.crc,ts_data->frame_data.n_crc);*/
    }

    return 0;
}
#endif
/*****************************************************************************
*  Name: fts_wait_tp_to_valid
*  Brief: Read chip id until TP FW become valid(Timeout: TIMEOUT_READ_REG),
*         need call when reset/power on/resume...
*  Input:
*  Output:
*  Return: return 0 if tp valid, otherwise return error code
*****************************************************************************/
int fts_wait_tp_to_valid(void)
{
    int ret = 0;
    int cnt = 0;
    u8 idh = 0;
    struct fts_ts_data *ts_data = fts_data;
    u8 chip_idh = ts_data->ic_info.ids.chip_idh;

    do {
        ret = fts_read_reg(FTS_REG_CHIP_ID, &idh);
        if ((idh == chip_idh) || (fts_check_cid(ts_data, idh) == 0)) {
            FTS_DEBUG("TP Ready,Device ID:0x%02x", idh);
            return 0;
        } else
            FTS_ERROR("TP Not Ready,ReadData:0x%02x,ret:%d", idh, ret);

        cnt++;
        msleep(INTERVAL_READ_REG);
    } while ((cnt * INTERVAL_READ_REG) < TIMEOUT_READ_REG);

    return -EIO;
}

/*****************************************************************************
*  Name: fts_tp_state_recovery
*  Brief: Need execute this function when reset
*  Input:
*  Output:
*  Return:
*****************************************************************************/
void fts_tp_state_recovery(struct fts_ts_data *ts_data)
{
	FTS_FUNC_ENTER();
	/* wait tp stable */
	fts_wait_tp_to_valid();
	/* recover TP charger state 0x8B */
	fts_charger_status_recovery(ts_data);
	/* recover TP glove state 0xC0 */
	/* recover TP cover state 0xC1 */
	/*fts_ex_mode_recovery(ts_data);*/
	/* recover TP gesture state 0xD0 */
	fts_gesture_recovery(ts_data);
	/* recover TP report_rate state 0x92 */
	fts_report_rate_recovery(ts_data);
#ifdef FTS_XIAOMI_TOUCHFEATURE
	/* recover TP game mode state */
	fts_game_mode_recovery(ts_data);
	/* recover TP idle refresh state 0x8E */
	fts_game_idle_high_refresh_recovery(ts_data);
	/* recover TP fod state 0xCF */
	fts_fod_status_recovery(ts_data);
#endif
	FTS_FUNC_EXIT();
}

int fts_reset_proc(int hdelayms)
{
	FTS_DEBUG("tp reset in");
	fts_write_reg(SET_ID_G_HOST_RST_FLAG, 0x01);
	msleep(20);
	gpio_direction_output(fts_data->pdata->reset_gpio, 0);
	msleep(1);
	gpio_direction_output(fts_data->pdata->reset_gpio, 1);
	if (hdelayms)
		msleep(hdelayms);
	FTS_DEBUG("tp reset out");
	return 0;
}

void fts_reset_for_upgrade(void)
{
    gpio_direction_output(fts_data->pdata->reset_gpio, 0);
    msleep(1);
    gpio_direction_output(fts_data->pdata->reset_gpio, 1);
}

int fts_recover_after_reset(void)
{
	int i = 0;
	u8 id = 0;
	for (i = 0; i < 20; i++) {
		fts_read_reg(0xA3, &id);
		if(id == FTS_CHIP_TYPE_ID) {
			break;
		}
		msleep(10);
	}
	if(i >= 20) {
		FTS_ERROR("wait tp fw valid timeout");
	}

#ifdef FTS_TOUCHSCREEN_FOD
	fts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	if (fts_data->fod_status != -1 && fts_data->fod_status != 0) {
		FTS_INFO("fod_status = %d\n", fts_data->fod_status);
		fts_fod_recovery();
	}
#endif

	if (fts_data->report_rate_status == 120) {
		FTS_INFO("report_rate = %d\n", fts_data->report_rate_status);
		/*reset Report_Rate to 120HZ*/
		if (fts_write_reg(0x92, 1) < 0) {
			FTS_ERROR("Failed to switch Report_Rate to 120HZ");
		}
	}
	return 0;
}

void fts_irq_disable(void)
{
    unsigned long irqflags;

    FTS_FUNC_ENTER();
    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (!fts_data->irq_disabled) {
        disable_irq_nosync(fts_data->irq);
        fts_data->irq_disabled = true;
    }

    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
    FTS_FUNC_EXIT();
}

void fts_irq_enable(void)
{
    unsigned long irqflags = 0;

    FTS_FUNC_ENTER();
    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (fts_data->irq_disabled) {
        enable_irq(fts_data->irq);
        fts_data->irq_disabled = false;
    }

    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
    FTS_FUNC_EXIT();
}

void fts_hid2std(void)
{
    int ret = 0;
    u8 buf[3] = {0xEB, 0xAA, 0x09};

    if (fts_data->bus_type != BUS_TYPE_I2C)
        return;

    ret = fts_write(buf, 3);
    if (ret < 0) {
        FTS_ERROR("hid2std cmd write fail");
    } else {
        msleep(10);
        buf[0] = buf[1] = buf[2] = 0;
        ret = fts_read(NULL, 0, buf, 3);
        if (ret < 0) {
            FTS_ERROR("hid2std cmd read fail");
        } else if ((buf[0] == 0xEB) && (buf[1] == 0xAA) && (buf[2] == 0x08)) {
            FTS_DEBUG("hidi2c change to stdi2c successful");
        } else {
            FTS_DEBUG("hidi2c change to stdi2c not support or fail");
        }
    }
}
static void fts_recover_sleep_from_gesture(struct fts_ts_data *data)
{
	int ret = 0;

	FTS_FUNC_ENTER();
	fts_irq_disable();
	if (fts_data->irq_wake) {
		disable_irq_wake(fts_data->irq);
		fts_data->irq_wake = false;
    }
	ret = fts_write_reg(FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP);
	if (ret < 0)
		FTS_ERROR("set TP to sleep mode fail, ret=%d", ret);
	FTS_FUNC_EXIT();
}
static void fts_recover_gesture_from_sleep(struct fts_ts_data *data)
{
	FTS_FUNC_ENTER();
	fts_reset_proc(50);
	fts_tp_state_recovery(data);
	fts_irq_enable();
	if (!fts_data->irq_wake) {
		enable_irq_wake(fts_data->irq);
		fts_data->irq_wake = true;
	}
	FTS_FUNC_EXIT();
}

static int fts_match_cid(struct fts_ts_data *ts_data,
                         u16 type, u8 id_h, u8 id_l, bool force)
{
#ifdef FTS_CHIP_ID_MAPPING
    u32 i = 0;
    u32 j = 0;
    struct ft_chip_id_t chip_id_list[] = FTS_CHIP_ID_MAPPING;
    u32 cid_entries = sizeof(chip_id_list) / sizeof(struct ft_chip_id_t);
    u16 id = (id_h << 8) + id_l;

    memset(&ts_data->ic_info.cid, 0, sizeof(struct ft_chip_id_t));
    for (i = 0; i < cid_entries; i++) {
        if (!force && (type == chip_id_list[i].type)) {
            break;
        } else if (force && (type == chip_id_list[i].type)) {
            FTS_INFO("match cid,type:0x%x", (int)chip_id_list[i].type);
            ts_data->ic_info.cid = chip_id_list[i];
            return 0;
        }
    }

    if (i >= cid_entries) {
        return -ENODATA;
    }

    for (j = 0; j < FTS_MAX_CHIP_IDS; j++) {
        if (id == chip_id_list[i].chip_ids[j]) {
            FTS_DEBUG("cid:%x==%x", id, chip_id_list[i].chip_ids[j]);
            FTS_INFO("match cid,type:0x%x", (int)chip_id_list[i].type);
            ts_data->ic_info.cid = chip_id_list[i];
            return 0;
        }
    }

    return -ENODATA;
#else
    return -EINVAL;
#endif
}


static int fts_get_chip_types(
    struct fts_ts_data *ts_data,
    u8 id_h, u8 id_l, bool fw_valid)
{
    u32 i = 0;
    struct ft_chip_t ctype[] = FTS_CHIP_TYPE_MAPPING;
    u32 ctype_entries = sizeof(ctype) / sizeof(struct ft_chip_t);

    if ((id_h == 0x0) || (id_l == 0x0)) {
        FTS_ERROR("id_h/id_l is 0");
        return -EINVAL;
    }

    FTS_INFO("verify id:0x%02x%02x", id_h, id_l);
    for (i = 0; i < ctype_entries; i++) {
        if (fw_valid == VALID) {
            if (((id_h == ctype[i].chip_idh) && (id_l == ctype[i].chip_idl))
                || (!fts_match_cid(ts_data, ctype[i].type, id_h, id_l, 0)))
                break;
        } else {
            if (((id_h == ctype[i].rom_idh) && (id_l == ctype[i].rom_idl))
                || ((id_h == ctype[i].pb_idh) && (id_l == ctype[i].pb_idl))
                || ((id_h == ctype[i].bl_idh) && (id_l == ctype[i].bl_idl))) {
                break;
            }
        }
    }

    if (i >= ctype_entries) {
        return -ENODATA;
    }

    fts_match_cid(ts_data, ctype[i].type, id_h, id_l, 1);
    ts_data->ic_info.ids = ctype[i];
    return 0;
}

static int fts_read_bootid(struct fts_ts_data *ts_data, u8 *id)
{
    int ret = 0;
    u8 chip_id[2] = { 0 };
    u8 id_cmd[4] = { 0 };
    u32 id_cmd_len = 0;

    id_cmd[0] = FTS_CMD_START1;
    id_cmd[1] = FTS_CMD_START2;
    ret = fts_write(id_cmd, 2);
    if (ret < 0) {
        FTS_ERROR("start cmd write fail");
        return ret;
    }

    msleep(FTS_CMD_START_DELAY);
    id_cmd[0] = FTS_CMD_READ_ID;
    id_cmd[1] = id_cmd[2] = id_cmd[3] = 0x00;
    if (ts_data->ic_info.is_incell)
        id_cmd_len = FTS_CMD_READ_ID_LEN_INCELL;
    else
        id_cmd_len = FTS_CMD_READ_ID_LEN;
    ret = fts_read(id_cmd, id_cmd_len, chip_id, 2);
    if ((ret < 0) || (chip_id[0] == 0x0) || (chip_id[1] == 0x0)) {
        FTS_ERROR("read boot id fail,read:0x%02x%02x", chip_id[0], chip_id[1]);
        return -EIO;
    }

    id[0] = chip_id[0];
    id[1] = chip_id[1];
    return 0;
}

/*****************************************************************************
* Name: fts_get_ic_information
* Brief: read chip id to get ic information, after run the function, driver w-
*        ill know which IC is it.
*        If cant get the ic information, maybe not focaltech's touch IC, need
*        unregister the driver
* Input:
* Output:
* Return: return 0 if get correct ic information, otherwise return error code
*****************************************************************************/
static int fts_get_ic_information(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int cnt = 0;
    u8 chip_id[2] = { 0 };

    ts_data->ic_info.is_incell = FTS_CHIP_IDC;
    ts_data->ic_info.hid_supported = FTS_HID_SUPPORTTED;


    do {
        ret = fts_read_reg(FTS_REG_CHIP_ID, &chip_id[0]);
        ret = fts_read_reg(FTS_REG_CHIP_ID2, &chip_id[1]);
        if ((ret < 0) || (0x0 == chip_id[0]) || (0x0 == chip_id[1])) {
            FTS_DEBUG("chip id read invalid, read:0x%02x%02x",
                      chip_id[0], chip_id[1]);
        } else {
            ret = fts_get_chip_types(ts_data, chip_id[0], chip_id[1], VALID);
            if (!ret)
                break;
            else
                FTS_DEBUG("TP not ready, read:0x%02x%02x",
                          chip_id[0], chip_id[1]);
        }

        cnt++;
        msleep(INTERVAL_READ_REG);
    } while ((cnt * INTERVAL_READ_REG) < TIMEOUT_READ_REG);

    if ((cnt * INTERVAL_READ_REG) >= TIMEOUT_READ_REG) {
        FTS_INFO("fw is invalid, need read boot id");
        if (ts_data->ic_info.hid_supported) {
            fts_hid2std();
        }

        ret = fts_read_bootid(ts_data, &chip_id[0]);
        if (ret <  0) {
            FTS_ERROR("read boot id fail");
            return ret;
        }

        ret = fts_get_chip_types(ts_data, chip_id[0], chip_id[1], INVALID);
        if (ret < 0) {
            FTS_ERROR("can't get ic informaton");
            return ret;
        }
    }

    FTS_INFO("get ic information, chip id = 0x%02x%02x(cid type=0x%x)",
             ts_data->ic_info.ids.chip_idh, ts_data->ic_info.ids.chip_idl,
             ts_data->ic_info.cid.type);

    return 0;
}

/*****************************************************************************
*  char lockdown[8];//maximum:8
*****************************************************************************/
int fts_get_lockdown_information(struct fts_ts_data *ts_data)
{
	int ret = 0;
	int i = 0;
	int count = 0;
	u8 temp_lockdown[256] = {0};
	ret = fts_read_lockdown_info_proc(ts_data->lockdown_info);
	if (ret) {
		FTS_ERROR("lockdown_info init fail");
		return -EIO;
	}
	for(i = 0; i < 8; i++) {
		count += sprintf(temp_lockdown + count, " %02x ", ts_data->lockdown_info[i]);
	}
	FTS_INFO("lockdown information before formatting:%s", ts_data->lockdown_info);
	FTS_INFO("lockdown information after formatting:%s", temp_lockdown);
	return 0;
}

/*****************************************************************************
*  Reprot related
*****************************************************************************/
static void fts_show_touch_buffer(u8 *data, u32 datalen)
{
    u32 i = 0;
    u32 count = 0;
    char *tmpbuf = NULL;

    tmpbuf = kzalloc(1024, GFP_KERNEL);
    if (!tmpbuf) {
        FTS_ERROR("tmpbuf zalloc fail");
        return;
    }

    for (i = 0; i < datalen; i++) {
        count += snprintf(tmpbuf + count, 1024 - count, "%02X,", data[i]);
        if (count >= 1024)
            break;
    }
    FTS_DEBUG("touch_buf:%s", tmpbuf);


    kfree(tmpbuf);
    tmpbuf = NULL;

}

void fts_release_all_finger(void)
{
    struct fts_ts_data *ts_data = fts_data;
    struct input_dev *input_dev = ts_data->input_dev;
#if FTS_MT_PROTOCOL_B_EN
    u32 finger_count = 0;
    u32 max_touches = ts_data->pdata->max_touch_number;
#endif
#ifdef FTS_TOUCHSCREEN_FOD
	fts_data->finger_in_fod = false;
	fts_data->overlap_area = 0;
	FTS_INFO("%s : finger_in_fod = %d", __func__, fts_data->finger_in_fod);
#endif

    mutex_lock(&ts_data->report_mutex);
#if FTS_MT_PROTOCOL_B_EN
    for (finger_count = 0; finger_count < max_touches; finger_count++) {
        input_mt_slot(input_dev, finger_count);
        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
        // last_touch_events_collect_common(finger_count, 0);
    }
#else
    input_mt_sync(input_dev);
#endif
    input_report_key(input_dev, BTN_TOUCH, 0);
#ifdef CONFIG_TOUCH_BOOST
    lpm_disable_for_dev(false, LPM_EVENT_INPUT);
#endif
    input_sync(input_dev);

#if FTS_PEN_EN
    input_report_key(ts_data->pen_dev, BTN_TOOL_PEN, 0);
    input_report_key(ts_data->pen_dev, BTN_TOUCH, 0);
#ifdef CONFIG_TOUCH_BOOST
    lpm_disable_for_dev(false, LPM_EVENT_INPUT);
#endif
    input_sync(ts_data->pen_dev);
#endif

    ts_data->touch_points = 0;
    ts_data->key_state = 0;
    mutex_unlock(&ts_data->report_mutex);
}

static void set_touch_mode(int mode, int value)
{
	int touch_mode[DATA_MODE_45];
	long update_mode_mask = 0;
	if (mode < 0 || mode >= DATA_MODE_45 || value < 0)
		return;
	touch_mode[mode] = value;
	update_mode_mask |= 1 << mode;
	driver_update_touch_mode_common(TOUCH_ID, touch_mode, update_mode_mask);
}

#ifdef TOUCH_THP_SUPPORT
static void fts_thp_signal_work(struct work_struct *work)
{
	struct fts_ts_data *ts_data = container_of(work, struct fts_ts_data, thp_signal_work.work);

	if (!ts_data) {
		FTS_ERROR("core data not init");
		return;
	}
	if (!ts_data->enable_touch_raw) {
		FTS_INFO("not enable touch raw");
		return;
	}
#ifdef CONFIG_FACTORY_BUILD
	{
		int fod_en = 1;
		FTS_INFO("notify fod enable to hal");
		add_common_data_to_buf_common(0, SET_CUR_VALUE, DATA_MODE_10, 1, &fod_en);
	}
#endif
}
#endif

/*****************************************************************************
* Name: fts_input_report_key
* Brief: process key events,need report key-event if key enable.
*        if point's coordinate is in (x_dim-50,y_dim-50) ~ (x_dim+50,y_dim+50),
*        need report it to key event.
*        x_dim: parse from dts, means key x_coordinate, dimension:+-50
*        y_dim: parse from dts, means key y_coordinate, dimension:+-50
* Input:
* Output:
* Return: return 0 if it's key event, otherwise return error code
*****************************************************************************/
static int fts_input_report_key(struct fts_ts_data *ts_data, struct ts_event *kevent)
{
    int i = 0;
    int x = kevent->x;
    int y = kevent->y;
    int *x_dim = &ts_data->pdata->key_x_coords[0];
    int *y_dim = &ts_data->pdata->key_y_coords[0];

    if (!ts_data->pdata->have_key)
        return -EINVAL;
    for (i = 0; i < ts_data->pdata->key_number; i++) {
        if ((x >= x_dim[i] - FTS_KEY_DIM) && (x <= x_dim[i] + FTS_KEY_DIM) &&
            (y >= y_dim[i] - FTS_KEY_DIM) && (y <= y_dim[i] + FTS_KEY_DIM)) {
            if (EVENT_DOWN(kevent->flag)
                && !(ts_data->key_state & (1 << i))) {
                input_report_key(ts_data->input_dev, ts_data->pdata->keys[i], 1);
                ts_data->key_state |= (1 << i);
                FTS_DEBUG("Key%d(%d,%d) DOWN!", i, x, y);
            } else if (EVENT_UP(kevent->flag)
                       && (ts_data->key_state & (1 << i))) {
                input_report_key(ts_data->input_dev, ts_data->pdata->keys[i], 0);
                ts_data->key_state &= ~(1 << i);
                FTS_DEBUG("Key%d(%d,%d) Up!", i, x, y);
            }
            return 0;
        }
    }
    return -EINVAL;
}

#if FTS_MT_PROTOCOL_B_EN
static int fts_input_report_b(struct fts_ts_data *ts_data, struct ts_event *events)
{
    int i = 0;
    int touch_down_point_cur = 0;
    int touch_point_pre = ts_data->touch_points;
    u32 max_touch_num = ts_data->pdata->max_touch_number;
    bool touch_event_coordinate = false;
    struct input_dev *input_dev = ts_data->input_dev;

    for (i = 0; i < ts_data->touch_event_num; i++) {
        if (fts_input_report_key(ts_data, &events[i]) == 0)
            continue;

        touch_event_coordinate = true;
        if (EVENT_DOWN(events[i].flag)) {
            input_mt_slot(input_dev, events[i].id);
            input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
#if FTS_REPORT_PRESSURE_EN
            input_report_abs(input_dev, ABS_MT_PRESSURE, events[i].p);
#endif
            /*FTS_DEBUG("fod_finger_skip%d,overlap_area%d,", ts_data->fod_finger_skip, ts_data->overlap_area);*/
            if (!ts_data->fod_finger_skip && ts_data->overlap_area == 100 && !ts_data->suspended) {
                /*be useful when panel has been resumed */
                update_fod_press_status_common(1);
                FTS_INFO("Report_0x152 resume DOWN");
                /* mi_disp_set_fod_queue_work(1, true); */
	    }
	    input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, ts_data->overlap_area);
            input_report_abs(input_dev, ABS_MT_TOUCH_MINOR, events[i].minor);
	    /*input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, ts_data->overlap_area);*/
            /*input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, events[i].area);*/
            input_report_abs(input_dev, ABS_MT_POSITION_X, events[i].x * fts_get_super_resolution_factor() / ts_data->pdata->super_resolution_factors);
            input_report_abs(input_dev, ABS_MT_POSITION_Y, events[i].y * fts_get_super_resolution_factor() / ts_data->pdata->super_resolution_factors);

            touch_down_point_cur |= (1 << events[i].id);
            touch_point_pre |= (1 << events[i].id);

            if ((ts_data->log_level >= 3) ||
                ((ts_data->log_level >= 1) && (events[i].flag == FTS_TOUCH_DOWN))) {
                FTS_DEBUG("[B]P%d(%d, %d)[p:%d,tm:%d] DOWN!",
                          events[i].id, events[i].x, events[i].y,
                          events[i].p, events[i].area);
            }
            // last_touch_events_collect_common(events[i].id, 1);
        } else {
            input_mt_slot(input_dev, events[i].id);
            input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
            touch_point_pre &= ~(1 << events[i].id);
            if (ts_data->log_level >= 1)
                    FTS_DEBUG("[B]P%d UP!", events[i].id);
            // last_touch_events_collect_common(events[i].id, 0);
        }
    }

    if (unlikely(touch_point_pre ^ touch_down_point_cur)) {
        for (i = 0; i < max_touch_num; i++)  {
            if ((1 << i) & (touch_point_pre ^ touch_down_point_cur)) {
                if (ts_data->log_level >= 1)
                        FTS_DEBUG("[B]P%d UP!", i);
                input_mt_slot(input_dev, i);
                input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
                // last_touch_events_collect_common(i, 0);
            }
        }
    }

    if (touch_down_point_cur)
        input_report_key(input_dev, BTN_TOUCH, 1);
    else if (touch_event_coordinate || ts_data->touch_points) {
        if (ts_data->touch_points && (ts_data->log_level >= 1))
            FTS_DEBUG("[B]Points All Up!");
        input_report_key(input_dev, BTN_TOUCH, 0);
#ifdef CONFIG_TOUCH_BOOST
        lpm_disable_for_dev(false, LPM_EVENT_INPUT);
#endif
    }

    ts_data->touch_points = touch_down_point_cur;
    input_sync(input_dev);
    return 0;
}
#else
static int fts_input_report_a(struct fts_ts_data *ts_data, struct ts_event *events)
{
    int i = 0;
    int touch_down_point_num_cur = 0;
    bool touch_event_coordinate = false;
    struct input_dev *input_dev = ts_data->input_dev;

    for (i = 0; i < ts_data->touch_event_num; i++) {
        if (fts_input_report_key(ts_data, &events[i]) == 0) {
            continue;
        }

        touch_event_coordinate = true;
        if (EVENT_DOWN(events[i].flag)) {
            input_report_abs(input_dev, ABS_MT_TRACKING_ID, events[i].id);
#if FTS_REPORT_PRESSURE_EN
            input_report_abs(input_dev, ABS_MT_PRESSURE, events[i].p);
#endif
            input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, events[i].area);
            input_report_abs(input_dev, ABS_MT_POSITION_X, events[i].x);
            input_report_abs(input_dev, ABS_MT_POSITION_Y, events[i].y);
            input_mt_sync(input_dev);

            touch_down_point_num_cur++;
            if ((ts_data->log_level >= 2) ||
                ((ts_data->log_level == 1) && (events[i].flag == FTS_TOUCH_DOWN))) {
                FTS_DEBUG("[A]P%d(%d, %d)[p:%d,tm:%d] DOWN!",
                          events[i].id, events[i].x, events[i].y,
                          events[i].p, events[i].area);
            }
        }
    }

    if (touch_down_point_num_cur)
        input_report_key(input_dev, BTN_TOUCH, 1);
    else if (touch_event_coordinate || ts_data->touch_points) {
        if (ts_data->touch_points && (ts_data->log_level >= 1))
            FTS_DEBUG("[A]Points All Up!");
        input_report_key(input_dev, BTN_TOUCH, 0);
        input_mt_sync(input_dev);
    }

    ts_data->touch_points = touch_down_point_num_cur;
    input_sync(input_dev);
    return 0;
}
#endif

#if FTS_PEN_EN
static int fts_input_pen_report(struct fts_ts_data *ts_data, u8 *pen_buf)
{
    struct input_dev *pen_dev = ts_data->pen_dev;
    struct pen_event *pevt = &ts_data->pevent;

    /*get information of stylus*/
    pevt->inrange = (pen_buf[2] & 0x20) ? 1 : 0;
    pevt->tip = (pen_buf[2] & 0x01) ? 1 : 0;
    pevt->flag = pen_buf[3] >> 6;
    pevt->id = pen_buf[5] >> 4;
    pevt->x = ((pen_buf[3] & 0x0F) << 8) + pen_buf[4];
    pevt->y = ((pen_buf[5] & 0x0F) << 8) + pen_buf[6];
    pevt->p = ((pen_buf[7] & 0x0F) << 8) + pen_buf[8];
    pevt->tilt_x = (short)((pen_buf[9] << 8) + pen_buf[10]);
    pevt->tilt_y = (short)((pen_buf[11] << 8) + pen_buf[12]);
    pevt->azimuth = ((pen_buf[13] << 8) + pen_buf[14]);
    pevt->tool_type = BTN_TOOL_PEN;

    input_report_key(pen_dev, BTN_STYLUS, !!(pen_buf[2] & 0x02));
    input_report_key(pen_dev, BTN_STYLUS2, !!(pen_buf[2] & 0x08));

    switch (ts_data->pen_etype) {
    case STYLUS_DEFAULT:
        if (pevt->tip && pevt->p) {
            if ((ts_data->log_level >= 2) || (!pevt->down))
                FTS_DEBUG("[PEN]x:%d,y:%d,p:%d,tip:%d,flag:%d,tilt:%d,%d DOWN",
                          pevt->x, pevt->y, pevt->p, pevt->tip, pevt->flag,
                          pevt->tilt_x, pevt->tilt_y);
            input_report_abs(pen_dev, ABS_X, pevt->x);
            input_report_abs(pen_dev, ABS_Y, pevt->y);
            input_report_abs(pen_dev, ABS_PRESSURE, pevt->p);
            input_report_abs(pen_dev, ABS_TILT_X, pevt->tilt_x);
            input_report_abs(pen_dev, ABS_TILT_Y, pevt->tilt_y);
            input_report_key(pen_dev, BTN_TOUCH, 1);
            input_report_key(pen_dev, BTN_TOOL_PEN, 1);
            pevt->down = 1;
        } else if (!pevt->tip && pevt->down) {
            FTS_DEBUG("[PEN]x:%d,y:%d,p:%d,tip:%d,flag:%d,tilt:%d,%d UP",
                      pevt->x, pevt->y, pevt->p, pevt->tip, pevt->flag,
                      pevt->tilt_x, pevt->tilt_y);
            input_report_abs(pen_dev, ABS_X, pevt->x);
            input_report_abs(pen_dev, ABS_Y, pevt->y);
            input_report_abs(pen_dev, ABS_PRESSURE, pevt->p);
            input_report_key(pen_dev, BTN_TOUCH, 0);
            input_report_key(pen_dev, BTN_TOOL_PEN, 0);
            pevt->down = 0;
        }
        input_sync(pen_dev);
        break;
    case STYLUS_HOVER:
        if (ts_data->log_level >= 1)
            FTS_DEBUG("[PEN][%02X]x:%d,y:%d,p:%d,tip:%d,flag:%d,tilt:%d,%d,%d",
                      pen_buf[2], pevt->x, pevt->y, pevt->p, pevt->tip,
                      pevt->flag, pevt->tilt_x, pevt->tilt_y, pevt->azimuth);
        input_report_abs(pen_dev, ABS_X, pevt->x);
        input_report_abs(pen_dev, ABS_Y, pevt->y);
        input_report_abs(pen_dev, ABS_Z, pevt->azimuth);
        input_report_abs(pen_dev, ABS_PRESSURE, pevt->p);
        input_report_abs(pen_dev, ABS_TILT_X, pevt->tilt_x);
        input_report_abs(pen_dev, ABS_TILT_Y, pevt->tilt_y);
        input_report_key(pen_dev, BTN_TOOL_PEN, EVENT_DOWN(pevt->flag));
        input_report_key(pen_dev, BTN_TOUCH, pevt->tip);
        input_sync(pen_dev);
        break;
    default:
        FTS_ERROR("Unknown stylus event");
        break;
    }

    return 0;
}
#endif

/* blob 0x8d5c fts_thp_signal_work：HAL 就绪信号 work（纯日志，无业务动作）。
 * 串实证："not enable touch raw"(行4484) / "core data not init"(行4480)。 */
static void fts_thp_signal_work(struct work_struct *work)
{
	struct fts_ts_data *ts_data = container_of(work, struct fts_ts_data,
						   thp_signal_work.work);

	if (!ts_data) {
		if (fts_debug_log_level)
			FTS_ERROR("core data not init");
		return;
	}
	if (!ts_data->enable_touch_raw) {
		if (fts_debug_log_level >= 3)
			FTS_INFO("not enable touch raw");
		return;
	}
}

int fts_read_and_report_foddata(struct fts_ts_data *data)
{
	u8 buf[9] = { 0 };
#ifdef CONFIG_FOCAL_HWINFO
	char ch[64] = {0x00,};
#endif
	int ret;
	int x, y, z;
	data->touch_fod_addr = FTS_REG_FOD_OUTPUT_ADDRESS;
	data->touch_fod_size = 9;
	if (fts_scp_tp_param.param0 == 3) {
		/* SCP 接管态：FOD 数据从 SCP param 区取（blob 0x1368..0x1380），不碰 SPI */
		memcpy(buf, fts_scp_tp_param.gesture_data, data->touch_fod_size);
	} else {
		ret = fts_read(&data->touch_fod_addr, 1, buf, data->touch_fod_size);
		if (ret < 0) {
			FTS_ERROR("read fod failed, ret:%d", ret);
			return ret;
		}
	}
	/*
	 * buf[0]: point id
	 * buf[1]:event type， 0x24 is doubletap, 0x25 is single tap, 0x26 is fod pointer event
	 * buf[2]: touch area/fod sensor area
	 * buf[3]: touch area
	 * buf[4-7]: x,y position
	 * buf[8]:pointer up or down, 0 is down, 1 is up
	 */
	switch (buf[1]) {
	case 0x24:
		FTS_INFO("DoubleClick Gesture detected, Wakeup panel\n");
		input_report_key(data->input_dev, KEY_WAKEUP, 1);
		input_sync(data->input_dev);
		input_report_key(data->input_dev, KEY_WAKEUP, 0);
		input_sync(data->input_dev);
#ifdef CONFIG_FOCAL_HWINFO
		data->dbclick_count++;
		snprintf(ch, sizeof(ch), "%d", data->dbclick_count);
		update_hw_monitor_info(HWMON_CONPONENT_NAME, HWMON_KEY_DBCLICK_COUNT, ch);
#endif
		break;
	case 0x25:
		data->nonui_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_17);
		if (data->nonui_status != 0) {
			FTS_INFO("nonui_status is one/two, don't report key goto\n");
			return 0;
		}
		FTS_INFO("FOD status report KEY_GOTO\n");
		input_report_key(data->input_dev, KEY_GOTO, 1);
		input_sync(data->input_dev);
		input_report_key(data->input_dev, KEY_GOTO, 0);
		input_sync(data->input_dev);
		break;
	case 0x26:
		x = (buf[4] << 8) | buf[5];
		y = (buf[6] << 8) | buf[7];
		x *= SUPER_RESOLUTION_FACOTR;
		y *= SUPER_RESOLUTION_FACOTR;
		z = buf[3];
		FTS_INFO("FTS:read fod data: 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x anxis_x: %d anxis_y: %d factor:%d\n",
		buf[0], buf[1], buf[2], buf[3], buf[8], x, y, SUPER_RESOLUTION_FACOTR);
		if (buf[8] == 0) {
			mutex_lock(&data->report_mutex);
			if (!data->fod_finger_skip)
				data->overlap_area = 100;
			if (data->old_point_id != buf[0]) {
				if (data->old_point_id == 0xff)
					data->old_point_id = buf[0];
				else
					data->point_id_changed = true;
			}
			data->finger_in_fod = true;
			data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
			if (data->suspended && data->fod_status == 0) {
				FTS_INFO("Panel off and fod status : %d, don't report touch down event\n", data->fod_status);
				mutex_unlock(&data->report_mutex);
				return 0;
			}
			data->nonui_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_17);
			if (data->nonui_status == 2) {
				FTS_INFO("nonui_status is two, don't report 152\n");
				mutex_unlock(&data->report_mutex);
				return 0;
			}
			if (!data->fod_finger_skip && data->finger_in_fod) {
				input_mt_slot(data->input_dev, buf[0]);
				input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, 1);
				update_fod_press_status_common(1);
				/* mi_disp_set_fod_queue_work(1, true); */
				input_report_key(data->input_dev, BTN_TOUCH, 1);
				input_report_key(data->input_dev, BTN_TOOL_FINGER, 1);
				input_report_abs(data->input_dev, ABS_MT_POSITION_X, x);
				input_report_abs(data->input_dev, ABS_MT_POSITION_Y, y);
				input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, z);
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, data->overlap_area);
				input_report_abs(data->input_dev, ABS_MT_WIDTH_MINOR, data->overlap_area);
				input_report_abs(data->input_dev, ABS_MT_PRESSURE, z);
				input_sync(data->input_dev);
				FTS_INFO("Report_0x152 suspend DOWN report_area %d sucess for miui", data->overlap_area);
			}
			mutex_unlock(&data->report_mutex);
		} else {
			update_fod_press_status_common(0);
			/*mi_disp_set_fod_queue_work(0, true);*/
			data->finger_in_fod = false;
			data->fod_finger_skip = false;
			data->old_point_id = 0xff;
			data->point_id_changed = false;
			FTS_INFO("Report_0x152 UP for FingerPrint\n");
			data->overlap_area = 0;
			if (!data->suspended) {
				data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
				if (!data->fod_status) {
					/*Turn off FOD enable in the CF register after fingerprint unlocking is successful and FOD finger is up*/
					fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, false);
					FTS_INFO("Turn off FOD enable in the CF register successfully!\n");
                                }
				FTS_INFO("FTS:touch is not in suspend state, report x,y value by touch nomal report\n");
				return -EINVAL;
			}
			mutex_lock(&data->report_mutex);
			input_mt_slot(data->input_dev, buf[0]);
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER, 0);
			input_report_key(data->input_dev, BTN_TOUCH, 0);
			input_report_key(data->input_dev, BTN_TOOL_FINGER, 0);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, -1);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MAJOR, 0);
			input_report_abs(data->input_dev, ABS_MT_WIDTH_MINOR, 0);
			input_sync(data->input_dev);
			mutex_unlock(&data->report_mutex);
		}
		break;
	default:
		data->overlap_area = 0;
		if (data->suspended)
			return 0;
		else
			return -EINVAL;
		break;
	}
	return 0;
}

static int fts_read_touchdata(struct fts_ts_data *ts_data, u8 *buf)
{
	int ret = 0;
	int temp;
	ts_data->touch_addr = 0x01;
	ret = fts_read(&ts_data->touch_addr, 1, buf, ts_data->touch_size);

	if (ts_data->suspended) {
		temp = fts_read_and_report_foddata(ts_data);
		if (ret < 0) {
			FTS_ERROR("touch data(%x) abnormal,ret:%d", buf[1], ret);
			return ret;
		}
	}
	FTS_DEBUG("FTS:touch is not in suspend state, skip read fod data\n");
	return 0;
}

static int fts_read_parse_touchdata(struct fts_ts_data *ts_data, u8 *touch_buf)
{
    int ret = 0;
    u8 gesture_en = 0xFF;

#ifdef CRC_CHECK
#if defined(SOC_LITTLE_ENDIAN) && SOC_LITTLE_ENDIAN
    u32 buf_size = 0;
    u32 temp_len = 0;
    u8 i = 0;
    u32 j = 0;
    char temp = 0;
#endif
    int32_t crc_value_cur = 0;
    int32_t crc_value = 0;
    int32_t crc_value_not = 0;
    bool crc_result = false;
#endif

    memset(touch_buf, 0xFF, FTS_MAX_TOUCH_BUF);
    ts_data->ta_size = ts_data->touch_size;

    /*read touch data*/
    ret = fts_read_touchdata(ts_data, touch_buf);
    if (ret < 0) {
        FTS_ERROR("read touch data fails");
        return TOUCH_ERROR;
    }

#ifdef CRC_CHECK
    crc_value_cur = thp_crc32_check(&touch_buf[20],(ts_data->touch_size - 20));
    crc_value = (touch_buf[4]<<24) + (touch_buf[5]<<16) + (touch_buf[6]<<8) + touch_buf[7];
    crc_value_not = (touch_buf[12]<<24) + (touch_buf[13]<<16) + (touch_buf[14]<<8) + touch_buf[15];
    if((crc_value_cur == crc_value) && (crc_value + crc_value_not == 0xFFFFFFFF))
        crc_result = true;

#if defined(SOC_LITTLE_ENDIAN) && SOC_LITTLE_ENDIAN
    buf_size = sizeof(buf_len)/sizeof(u32);
    j = 0;
    for(i = 0;i < buf_size;i++){
        if((buf_len[i] == 2) || (buf_len[i] > 4)) {
            temp_len = buf_len[i];
            while ( temp_len) {
                temp = touch_buf[j];
                touch_buf[j] = touch_buf[j + 1];
                touch_buf[j +1] = temp;
                j = j + 2;
                temp_len = temp_len - 2;
            }
        }else if(buf_len[i] == 4){
            temp = touch_buf[j];
            touch_buf[j] = touch_buf[j + 3];
            touch_buf[j + 3] = temp;
            temp = touch_buf[j + 1];
            touch_buf[j + 1] = touch_buf[j + 2];
            touch_buf[j + 2] = temp;
            j = j + 4;
        }else if(buf_len[i] == 1){
           j = j + 1;
        }else{
             FTS_ERROR("buffer len error,buf_len[%d] = %d!!!",i,buf_len[i]);
             return TOUCH_ERROR;
        }
    }

#endif

    fts_frame_parse_data(ts_data,(struct frame_afe_data *) touch_buf,crc_result);
    /*fts_frame_parse_data(ts_data,(struct frame_afe_data *) touch_buf);*/
#endif

    if (ts_data->log_level >= 3)
        fts_show_touch_buffer(touch_buf, ts_data->ta_size);

    if (ret)
        return TOUCH_IGNORE;

    /*gesture*/
    if (ts_data->suspended && ts_data->gesture_support) {
        ret = fts_read_reg(FTS_REG_GESTURE_EN, &gesture_en);
        if ((ret >= 0) && (gesture_en == ENABLE))
            return TOUCH_GESTURE;
        FTS_DEBUG("gesture not enable in fw, don't process gesture");
    }

    if ((touch_buf[1] == 0xFF) && (touch_buf[2] == 0xFF)
        && (touch_buf[3] == 0xFF) && (touch_buf[4] == 0xFF)) {
        FTS_INFO("touch buff is 0xff, need recovery state");
        return TOUCH_FW_INIT;
    }

    return ((touch_buf[FTS_TOUCH_E_NUM] >> 4) & 0x0F);
}

static int fts_read_framedata(struct fts_ts_data *ts_data, u8 *frame_buf, u32 len)
{
	int ret = 0;
	ts_data->touch_addr = FTS_FRAME_DATA_ADDR;
	ret = fts_read(&ts_data->touch_addr, 1, frame_buf, len);
	if (ret)
		FTS_ERROR("read frame failed, ret: %d", ret);
	return ret;
}

#ifdef TOUCH_DUMP_TIC_SUPPORT
#define SINGLE_LOG_MAX_LEN	1024 /* A single log can print 18 * 6 rawdata */
#define TX_PRINT_MAX_NUM	6
static void show_raw(u8 *data, u64 cnt, int tx, int rx)
{
	int size = tx * rx;
	char str[SINGLE_LOG_MAX_LEN] = "";
	int i, j, col;
	size_t copy_num = sizeof(u16);
	u16 raw_data = 0;
	size_t offset = 0;
	uint16_t frame_no = 0;
	if (!data || !rx || !tx)
		return;
	memcpy(&frame_no, data, sizeof(uint16_t));
	offset = offsetof(struct ST_RepotDbgBufThp, frame_data);
	sprintf(str + strlen(str), "[FTS]");
	for (i = 0, j = 0, col = 0; i < size; i++) {
		memcpy(&raw_data, data + offset + copy_num * (j++ * tx + col), copy_num);
		sprintf(str + strlen(str), "%5d,", (int)raw_data);
		if (j == rx) {
			j = 0;
			col++;
			if (col % TX_PRINT_MAX_NUM == 0) {
				FTS_DEBUG("TX%d ~ TX%d (cnt:%llu, frame_no:%hu):\n%s",
						(col - TX_PRINT_MAX_NUM), col - 1, cnt, frame_no, str);
				memset(str, 0, sizeof(str));
				sprintf(str + strlen(str), "[FTS]");
			} else if (i == size - 1) {
				FTS_DEBUG("TX%d ~ TX%d (cnt:%llu, frame_no:%hu):\n%s",
						((col / TX_PRINT_MAX_NUM) * TX_PRINT_MAX_NUM), col - 1, cnt, frame_no, str);
			} else {
				sprintf(str + strlen(str), "\n[FTS]");
			}
		}
	}
}

static int fts_htc_dump_tic(struct fts_ts_data *ts_data, struct tp_frame *tp_frame)
{
	int ret;

	tp_frame->dump_type = ts_data->dump_type;
	memset(tp_frame->thp_dbg_buf, 0, sizeof(struct ST_RepotDbgBufThp));
	if (!ts_data->enable_touch_raw || !ts_data->dump_type) {
		return 0;
	}
	ts_data->touch_addr = FTS_DEBUG_DATA_ADDR;
	ret = fts_read(&ts_data->touch_addr, 1,
			tp_frame->thp_dbg_buf, sizeof(struct ST_RepotDbgBufThp));
	if (ret) {
		FTS_ERROR("failed get tic raw data, ret: %d", ret);
		return ret;
	}
	if (fts_debug_log_level >= FTS_LOG_VERBOSE) {
		show_raw(&tp_frame->thp_dbg_buf[0], tp_frame->frame_cnt,
				fts_get_tx_num(), fts_get_rx_num());
	}
	return ret;
}
#endif /* TOUCH_DUMP_TIC_SUPPORT */

static int fts_irq_read_report(struct fts_ts_data *ts_data)
{
	int i = 0;
	int max_touch_num = ts_data->pdata->max_touch_number;
	int touch_etype = 0;
	u8 event_num = 0;
	u8 finger_num = 0;
	u8 pointid = 0;
	u8 base = 0;
	u8 *touch_buf = ts_data->touch_buf;
	struct ts_event *events = ts_data->events;

	int ret = 0;
	struct tp_frame *tp_frame = NULL;
	static u64 frame_cnt = 0;
	struct timespec64 ts;
	struct rtc_time tm;
	int ic_head_cnt = 0;
	int cur_frame_len = 0;
	uint16_t crc;
	uint16_t crc_len;
	uint16_t crc_r;
	uint16_t crc_r_len;
	if (ts_data->enable_touch_raw && !ts_data->suspended) {
		tp_frame = (struct tp_frame *)get_raw_data_base_common(TOUCH_ID);
		if (tp_frame == NULL)
			return -EINVAL;
		ret = fts_read_framedata(ts_data, (u8 *)tp_frame->thp_frame_buf, ts_data->touch_size);
		if (ret == 0) {
			crc =  (tp_frame->thp_frame_buf[6] << 8) + (tp_frame->thp_frame_buf[7]);
			crc_len =  (tp_frame->thp_frame_buf[10] << 8) + (tp_frame->thp_frame_buf[11]);
			crc_r =  (tp_frame->thp_frame_buf[14] << 8) + (tp_frame->thp_frame_buf[15]);
			crc_r_len =  (tp_frame->thp_frame_buf[18] << 8) + (tp_frame->thp_frame_buf[19]);
			cur_frame_len = (tp_frame->thp_frame_buf[46] << 8) + (tp_frame->thp_frame_buf[47]);
			if (cur_frame_len == 0x0000 || cur_frame_len == 0xFFFF || crc_r != ((uint16_t)(~crc)) || crc_len != ((uint16_t)(~crc_r_len)))	{
				FTS_ERROR("get frame length:0x%04x, crc: 0x%04x,  crc_len: 0x%04x,  crc_r: 0x%4x,  crc_r_len: 0x%04x, skip notify hal!",
						cur_frame_len, crc, crc_len, crc_r, crc_r_len);
				return 0;
			}
			ktime_get_real_ts64(&ts);
			tp_frame->time_ns = timespec64_to_ns(&ts);
			rtc_time64_to_tm(ts.tv_sec, &tm);
			tp_frame->frame_cnt = frame_cnt++;
			tp_frame->fod_pressed = ts_data->finger_in_fod;
			tp_frame->fod_trackingId = 0;
#ifdef TOUCH_DUMP_TIC_SUPPORT
			fts_htc_dump_tic(ts_data, tp_frame);
#endif /* TOUCH_DUMP_TIC_SUPPORT */
			notify_raw_data_update_common(TOUCH_ID);
			rtc_time64_to_tm(ts.tv_sec, &tm);
			ic_head_cnt = (tp_frame->thp_frame_buf[2] << 8) + (tp_frame->thp_frame_buf[3]);
			FTS_DEBUG("frame size: %d, frame data index: %d", ts_data->touch_size, ic_head_cnt);
			FTS_DEBUG("frame_head %px", (u8 *)&tp_frame->thp_frame_buf);
			return 0;
		} else {
			FTS_ERROR("get frame data failed!, ret: %d", ret);
			return -EINVAL;
		}
	}
	touch_etype = fts_read_parse_touchdata(ts_data, touch_buf);

    /*adjust log_level to output touch_buf when necessary*/
    if (ts_data->log_level >= 2) {
        fts_show_touch_buffer(touch_buf, ts_data->touch_size);
    }
    switch (touch_etype) {

    case TOUCH_DEFAULT:
        finger_num = touch_buf[FTS_TOUCH_E_NUM] & 0x0F;
        if (finger_num > max_touch_num) {
            FTS_ERROR("invalid point_num(%d)", finger_num);
            return -EIO;
        }

        for (i = 0; i < max_touch_num; i++) {
            base = FTS_ONE_TCH_LEN * i + 2;
            pointid = (touch_buf[FTS_TOUCH_OFF_ID_YH + base]) >> 4;
            if (pointid >= FTS_MAX_ID)
                break;
            else if (pointid >= max_touch_num) {
                FTS_ERROR("ID(%d) beyond max_touch_number", pointid);
                return -EINVAL;
            }

            events[i].id = pointid;
            events[i].flag = touch_buf[FTS_TOUCH_OFF_E_XH + base] >> 6;

	if (ts_data->pdata->super_resolution_factors == 10) {
		events[i].x = ((touch_buf[FTS_TOUCH_OFF_E_XH + base] & 0x0F) << 11)
			+ ((touch_buf[FTS_TOUCH_OFF_XL + base] & 0xFF) << 3)
			+ (((touch_buf[FTS_TOUCH_OFF_PRE + base] & 0xC0) >> 6) << 1)
			+ ((touch_buf[FTS_TOUCH_OFF_E_XH + base] & 0x20) >> 5);
		events[i].y = ((touch_buf[FTS_TOUCH_OFF_ID_YH + base] & 0x0F) << 11)
			+ ((touch_buf[FTS_TOUCH_OFF_YL + base] & 0xFF) << 3)
			+ (((touch_buf[FTS_TOUCH_OFF_PRE + base] & 0x30) >> 4) << 1)
			+ ((touch_buf[FTS_TOUCH_OFF_ID_YH + base] & 0x10) >> 4);
		events[i].area = touch_buf[FTS_TOUCH_OFF_AREA + base] & 0x7F;
		events[i].p =  touch_buf[FTS_TOUCH_OFF_PRE + base] & 0x0F;
	} else {
		events[i].x = ((touch_buf[FTS_TOUCH_OFF_E_XH + base] & 0x0F) << 8)
			+ (touch_buf[FTS_TOUCH_OFF_XL + base] & 0xFF);
		events[i].y = ((touch_buf[FTS_TOUCH_OFF_ID_YH + base] & 0x0F) << 8)
			+ (touch_buf[FTS_TOUCH_OFF_YL + base] & 0xFF);
		events[i].p =  touch_buf[FTS_TOUCH_OFF_PRE + base];
		events[i].area = touch_buf[FTS_TOUCH_OFF_AREA + base];
	}
	FTS_DEBUG("x:%d,y:%d", events[i].x, events[i].y);


            if (events[i].p <= 0)
                    events[i].p = 0x3F;
            if (events[i].area <= 0)
                    events[i].area = 0x09;

            event_num++;
            if (EVENT_DOWN(events[i].flag) && (finger_num == 0)) {
                FTS_INFO("abnormal touch data from fw");
                return -EIO;
            }
        }

        if (event_num == 0) {
            FTS_INFO("no touch point information(%02x)", touch_buf[2]);
            return -EIO;
        }
        ts_data->touch_event_num = event_num;

        mutex_lock(&ts_data->report_mutex);
#if FTS_MT_PROTOCOL_B_EN
        fts_input_report_b(ts_data, events);
#else
        fts_input_report_a(ts_data, events);
#endif
        mutex_unlock(&ts_data->report_mutex);
        break;

#if FTS_PEN_EN
    case TOUCH_PEN:
        mutex_lock(&ts_data->report_mutex);
        fts_input_pen_report(ts_data, touch_buf);
        mutex_unlock(&ts_data->report_mutex);
        break;
#endif

    case TOUCH_PROTOCOL_v2:

	event_num = touch_buf[FTS_TOUCH_E_NUM] & 0x0F;
	if (!event_num || (event_num > max_touch_num)) {
		FTS_ERROR("invalid touch event num(%d)", event_num);
		return -EIO;
	}

	ts_data->touch_event_num = event_num;

	for (i = 0; i < event_num; i++) {
		/* base = FTS_ONE_TCH_LEN_V2 * i + 4;
		 pointid = (touch_buf[FTS_TOUCH_OFF_ID_YH + base]) >> 4;
		 if (pointid >= FTS_MAX_ID)
			 break;
		 else if (pointid >= max_touch_num) {
			 FTS_ERROR("ID(%d) beyond max_touch_number", pointid);
			 return -EINVAL;
		 }*/

		base = FTS_ONE_TCH_LEN_V2 * i + 4;
		pointid = (touch_buf[FTS_TOUCH_OFF_ID_YH + base]) >> 4;
		if (pointid >= max_touch_num) {
			FTS_ERROR("touch point ID(%d) beyond max_touch_number(%d)",
					  pointid, max_touch_num);
			return -EINVAL;
		}

		events[i].id = pointid;
		events[i].flag = touch_buf[FTS_TOUCH_OFF_E_XH + base] >> 6;

		events[i].x = ((touch_buf[FTS_TOUCH_OFF_E_XH + base] & 0x0F) << 12) \
					  + ((touch_buf[FTS_TOUCH_OFF_XL + base] & 0xFF) << 4) \
					  + ((touch_buf[FTS_TOUCH_OFF_PRE + base] >> 4) & 0x0F);

		events[i].y = ((touch_buf[FTS_TOUCH_OFF_ID_YH + base] & 0x0F) << 12) \
					  + ((touch_buf[FTS_TOUCH_OFF_YL + base] & 0xFF) << 4) \
					  + (touch_buf[FTS_TOUCH_OFF_PRE + base] & 0x0F);

		events[i].x = events[i].x *16 / FTS_HI_RES_X_MAX;
		events[i].y = events[i].y *16 / FTS_HI_RES_X_MAX;
		events[i].area = touch_buf[FTS_TOUCH_OFF_AREA + base];
		events[i].minor = touch_buf[FTS_TOUCH_OFF_MINOR + base];
		events[i].p = 0x3F;

		if (events[i].area <= 0) events[i].area = 0x09;
		if (events[i].minor <= 0) events[i].minor = 0x09;

	}

	mutex_lock(&ts_data->report_mutex);

#if FTS_MT_PROTOCOL_B_EN
        fts_input_report_b(ts_data, events);
#else
        fts_input_report_a(ts_data, events);
#endif
        mutex_unlock(&ts_data->report_mutex);
        break;

    case TOUCH_EXTRA_MSG:
        if (!ts_data->touch_analysis_support) {
            FTS_ERROR("touch_analysis is disabled");
            return -EINVAL;
        }

        event_num = touch_buf[FTS_TOUCH_E_NUM] & 0x0F;
        if (!event_num || (event_num > max_touch_num)) {
            FTS_ERROR("invalid touch event num(%d)", event_num);
            return -EIO;
        }

        ts_data->touch_event_num = event_num;
        for (i = 0; i < event_num; i++) {
            base = FTS_ONE_TCH_LEN * i + 4;
            pointid = (touch_buf[FTS_TOUCH_OFF_ID_YH + base]) >> 4;
            if (pointid >= max_touch_num) {
                FTS_ERROR("touch point ID(%d) beyond max_touch_number(%d)",
                          pointid, max_touch_num);
                return -EINVAL;
            }

            events[i].id = pointid;
            events[i].flag = touch_buf[FTS_TOUCH_OFF_E_XH + base] >> 6;
            events[i].x = ((touch_buf[FTS_TOUCH_OFF_E_XH + base] & 0x0F) << 8) +
                    (touch_buf[FTS_TOUCH_OFF_XL + base] & 0xFF);
            events[i].y = ((touch_buf[FTS_TOUCH_OFF_ID_YH + base] & 0x0F) << 8) +
                    (touch_buf[FTS_TOUCH_OFF_YL + base] & 0xFF);
            events[i].p =  touch_buf[FTS_TOUCH_OFF_PRE + base];
            events[i].area = touch_buf[FTS_TOUCH_OFF_AREA + base];
            if (events[i].p <= 0)
                    events[i].p = 0x3F;
            if (events[i].area <= 0)
                    events[i].area = 0x09;
        }

        mutex_lock(&ts_data->report_mutex);
#if FTS_MT_PROTOCOL_B_EN
        fts_input_report_b(ts_data, events);
#else
        fts_input_report_a(ts_data, events);
#endif
        mutex_unlock(&ts_data->report_mutex);
        break;

    case TOUCH_GESTURE:
        if (fts_gesture_readdata(ts_data, touch_buf) == 0) {
            FTS_INFO("succuss to get gesture data in irq handler");
        }
        break;

    case TOUCH_FW_INIT:
        fts_release_all_finger();
        fts_tp_state_recovery(ts_data);
        break;

    case TOUCH_IGNORE:
    case TOUCH_ERROR:
        break;

    default:
        FTS_INFO("unknown touch event(%d)", touch_etype);
        break;
    }

    return 0;
}

static irqreturn_t fts_irq_handler(int irq, void *data)
{
    struct fts_ts_data *ts_data = fts_data;
    static struct task_struct *touch_task = NULL;
    struct sched_param par = { .sched_priority = MAX_RT_PRIO - 1};
    int ret;

    if (touch_task == NULL) {
        touch_task = current;
	sched_setscheduler_nocheck(touch_task, SCHED_FIFO, &par);
    }
#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
    // touch_irq_boost();
    if ((ts_data->suspended) && (ts_data->pm_suspend)) {
        if (!wait_for_completion_timeout(
                  &ts_data->pm_completion,
                  msecs_to_jiffies(FTS_TIMEOUT_COMERR_PM))) {
            FTS_ERROR("Bus don't resume from pm(deep),timeout,skip irq");
            return IRQ_HANDLED;
        }
    }
// #else
//     // touch_irq_boost();
#endif

    ret = dev_pm_qos_add_request(ts_data->dev,
                        &ts_data->dev_pm_qos_req_irq,
                        DEV_PM_QOS_RESUME_LATENCY,
                        0);
    if(ret < 0){
        FTS_ERROR("Touch dev_pm_qos_add_request fail \n");
    }

    ts_data->intr_jiffies = jiffies;
    fts_prc_queue_work(ts_data);
#ifdef CONFIG_TOUCH_BOOST
    lpm_disable_for_dev(true, LPM_EVENT_INPUT);
#endif
    fts_irq_read_report(ts_data);
    if (ts_data->touch_analysis_support && ts_data->ta_flag) {
        ts_data->ta_flag = 0;
        if (ts_data->ta_buf && ts_data->ta_size)
            memcpy(ts_data->ta_buf, ts_data->touch_buf, ts_data->ta_size);
        wake_up_interruptible(&ts_data->ts_waitqueue);
    }
    dev_pm_qos_remove_request(&ts_data->dev_pm_qos_req_irq);

    return IRQ_HANDLED;
}

static int fts_irq_registration(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;

    ts_data->irq = gpio_to_irq(pdata->irq_gpio);
    pdata->irq_gpio_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
    FTS_INFO("irq:%d, flag:%x", ts_data->irq, pdata->irq_gpio_flags);
    ret = request_threaded_irq(ts_data->irq, NULL, fts_irq_handler,
                               pdata->irq_gpio_flags,
                               "xiaomi_tp"  FTS_DRIVER_NAME, ts_data);

    return ret;
}

#if FTS_PEN_EN
static int fts_input_pen_init(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct input_dev *pen_dev;
    struct fts_ts_platform_data *pdata = ts_data->pdata;

    FTS_FUNC_ENTER();
    pen_dev = input_allocate_device();
    if (!pen_dev) {
        FTS_ERROR("Failed to allocate memory for input_pen device");
        return -ENOMEM;
    }

    pen_dev->dev.parent = ts_data->dev;
    pen_dev->name = FTS_DRIVER_PEN_NAME;
    pen_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
    __set_bit(ABS_X, pen_dev->absbit);
    __set_bit(ABS_Y, pen_dev->absbit);
    __set_bit(BTN_STYLUS, pen_dev->keybit);
    __set_bit(BTN_STYLUS2, pen_dev->keybit);
    __set_bit(BTN_TOUCH, pen_dev->keybit);
    __set_bit(BTN_TOOL_PEN, pen_dev->keybit);
    __set_bit(INPUT_PROP_DIRECT, pen_dev->propbit);
    input_set_abs_params(pen_dev, ABS_X, pdata->x_min, pdata->x_max, 0, 0);
    input_set_abs_params(pen_dev, ABS_Y, pdata->y_min, pdata->y_max, 0, 0);
    input_set_abs_params(pen_dev, ABS_PRESSURE, 0, 4096, 0, 0);
    input_set_abs_params(pen_dev, ABS_TILT_X, -9000, 9000, 0, 0);
    input_set_abs_params(pen_dev, ABS_TILT_Y, -9000, 9000, 0, 0);
    input_set_abs_params(pen_dev, ABS_Z, 0, 36000, 0, 0);

    ret = input_register_device(pen_dev);
    if (ret) {
        FTS_ERROR("Input device registration failed");
        input_free_device(pen_dev);
        pen_dev = NULL;
        return ret;
    }

    ts_data->pen_dev = pen_dev;
    ts_data->pen_etype = STYLUS_DEFAULT;
    FTS_FUNC_EXIT();
    return 0;
}
#endif

static int fts_input_init(struct fts_ts_data *ts_data)
{
#if 0
    int ret = 0;
    int key_num = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;
#endif
    struct input_dev *input_dev;

    FTS_FUNC_ENTER();
#if 1
    input_dev = register_xiaomi_input_dev(TOUCH_ID, fts_get_x_resolution()*fts_get_super_resolution_factor() -1, fts_get_y_resolution()*fts_get_super_resolution_factor() -1, FOCAL_SEC);
    if (!input_dev) {
        FTS_ERROR("Failed to allocate memory for input device");
        return -ENOMEM;
    }
    input_set_drvdata(input_dev, ts_data);
#else
    input_dev = input_allocate_device();
    if (!input_dev) {
        FTS_ERROR("Failed to allocate memory for input device");
        return -ENOMEM;
    }

    /* Init and register Input device */
    input_dev->name = FTS_DRIVER_NAME;
    if (ts_data->bus_type == BUS_TYPE_I2C)
        input_dev->id.bustype = BUS_I2C;
    else
        input_dev->id.bustype = BUS_SPI;
    input_dev->dev.parent = ts_data->dev;

    input_set_drvdata(input_dev, ts_data);

    __set_bit(EV_SYN, input_dev->evbit);
    __set_bit(EV_ABS, input_dev->evbit);
    __set_bit(EV_KEY, input_dev->evbit);
    __set_bit(BTN_TOUCH, input_dev->keybit);
    __set_bit(BTN_TOOL_FINGER, input_dev->keybit);
    __set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

    if (pdata->have_key) {
        FTS_INFO("set key capabilities");
        for (key_num = 0; key_num < pdata->key_number; key_num++)
            input_set_capability(input_dev, EV_KEY, pdata->keys[key_num]);
    }

#if FTS_MT_PROTOCOL_B_EN
    input_mt_init_slots(input_dev, pdata->max_touch_number, INPUT_MT_DIRECT);
#else
    input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 0x0F, 0, 0);
#endif
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, pdata->x_min, pdata->x_max - 1, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, pdata->y_min, pdata->y_max - 1, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);
#if FTS_REPORT_PRESSURE_EN
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 0xFF, 0, 0);
#endif
    input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
    input_set_capability(input_dev, EV_KEY, KEY_GOTO);
#ifdef FTS_TOUCHSCREEN_FOD
    input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, pdata->x_min, pdata->x_max - 1, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR, pdata->x_min, pdata->x_max - 1, 0, 0);
#endif

    ret = input_register_device(input_dev);
    if (ret) {
        FTS_ERROR("Input device registration failed");
        input_set_drvdata(input_dev, NULL);
        input_free_device(input_dev);
        input_dev = NULL;
        return ret;
    }
#endif
#if FTS_PEN_EN
    ret = fts_input_pen_init(ts_data);
    if (ret) {
        FTS_ERROR("Input-pen device registration failed");
        input_set_drvdata(input_dev, NULL);
        input_free_device(input_dev);
        input_dev = NULL;
        return ret;
    }
#endif

    ts_data->input_dev = input_dev;
    FTS_FUNC_EXIT();
    return 0;
}

static int fts_buffer_init(struct fts_ts_data *ts_data)
{
    ts_data->touch_buf = kzalloc(FTS_MAX_TOUCH_BUF, GFP_KERNEL);
    if (!ts_data->touch_buf) {
        FTS_ERROR("failed to alloc memory for touch buf");
        return -ENOMEM;
    }

    ts_data->touch_size = FTS_TOUCH_DATA_LEN_V2;


    ts_data->touch_analysis_support = 0;
    ts_data->ta_flag = 0;
    ts_data->ta_size = 0;

    return 0;
}

#if FTS_POWER_SOURCE_CUST_EN
/*****************************************************************************
* Power Control
*****************************************************************************/
#if FTS_PINCTRL_EN
static int fts_pinctrl_init(struct fts_ts_data *ts)
{
	int ret = 0;

	ts->pinctrl = devm_pinctrl_get(ts->dev);
	if (IS_ERR_OR_NULL(ts->pinctrl)) {
		FTS_ERROR("Failed to get pinctrl, please check dts");
		ret = PTR_ERR(ts->pinctrl);
		goto err_pinctrl_get;
	}

	ts->pins_active = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(ts->pins_active)) {
		FTS_ERROR("Pin state[active] not found");
		ret = PTR_ERR(ts->pins_active);
		goto err_pinctrl_lookup;
	}

	ts->pins_suspend = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(ts->pins_suspend)) {
		FTS_ERROR("Pin state[suspend] not found");
		ret = PTR_ERR(ts->pins_suspend);
		goto err_pinctrl_lookup;
	}

	ts->pins_release = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_release");
	if (IS_ERR_OR_NULL(ts->pins_release)) {
		/*FTS_ERROR("Pin state[release] not found");*/
		ret = PTR_ERR(ts->pins_release);
		FTS_INFO("Pin state[release] not found %d, no need for pmx_ts_release", ret);
	}

	ts->pinctrl_state_spimode = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_spi_mode");
	if (IS_ERR_OR_NULL(ts->pinctrl_state_spimode)) {
		ret = PTR_ERR(ts->pinctrl_state_spimode);
		/*FTS_ERROR("Can not lookup pinctrl_spi_mode pinstate %d\n", ret);*/
		FTS_INFO("Can not lookup pinctrl_spi_mode pinstate %d, no need for pmx_ts_spi_mode", ret);
		/* goto err_pinctrl_lookup; */
	}
	ts->pinctrl_touch_mode_ap = pinctrl_lookup_state(ts->pinctrl, "touch_mode_ap");
	if (IS_ERR_OR_NULL(ts->pinctrl_touch_mode_ap)) {
		ret = PTR_ERR(ts->pinctrl_touch_mode_ap);
		FTS_INFO("Can not lookup touch_mode_ap pinstate %d", ret);
	}
	ts->pinctrl_touch_mode_scp = pinctrl_lookup_state(ts->pinctrl, "touch_mode_scp");
	if (IS_ERR_OR_NULL(ts->pinctrl_touch_mode_scp)) {
		ret = PTR_ERR(ts->pinctrl_touch_mode_scp);
		FTS_INFO("Can not lookup touch_mode_scp pinstate %d", ret);
	}
	ts->pinctrl_dvdd_enable = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_dvdd_enable");
	if (IS_ERR_OR_NULL(ts->pinctrl_dvdd_enable)) {
		ret = PTR_ERR(ts->pinctrl_dvdd_enable);
		ts->pinctrl_dvdd_enable = NULL;
		/*FTS_ERROR("Can not lookup pmx_ts_dvdd_enable  pinstate %d\n", ret);*/
		FTS_INFO("Can not lookup pmx_ts_dvdd_enable  pinstate %d, no need for pmx_ts_dvdd_enable", ret);
	}
	ts->pinctrl_dvdd_disable = pinctrl_lookup_state(ts->pinctrl, "pmx_ts_dvdd_disable");
	if (IS_ERR_OR_NULL(ts->pinctrl_dvdd_disable)) {
		ret = PTR_ERR(ts->pinctrl_dvdd_disable);
		ts->pinctrl_dvdd_disable = NULL;
		/*FTS_ERROR("Can not lookup pmx_ts_dvdd_disable pinstate %d\n", ret);*/
		FTS_INFO("Can not lookup pmx_ts_dvdd_disable pinstate %d, no need for pmx_ts_dvdd_disable", ret);
	}

	return 0;
err_pinctrl_lookup:
	if (ts->pinctrl) {
		devm_pinctrl_put(ts->pinctrl);
	}
err_pinctrl_get:
	ts->pinctrl = NULL;
	ts->pins_release = NULL;
	ts->pins_suspend = NULL;
	ts->pins_active = NULL;
	ts->pinctrl_state_spimode = NULL;
	ts->pinctrl_dvdd_enable = NULL;
	ts->pinctrl_dvdd_disable = NULL;
	return ret;
}

static int fts_pinctrl_select_normal(struct fts_ts_data *ts)
{
    int ret = 0;

    if (ts->pinctrl && ts->pins_active) {
        ret = pinctrl_select_state(ts->pinctrl, ts->pins_active);
        if (ret < 0) {
            FTS_ERROR("Set normal pin state error:%d", ret);
        }
    }

    return ret;
}

static int fts_pinctrl_select_release(struct fts_ts_data *ts)
{
    int ret = 0;

    if (ts->pinctrl) {
        if (IS_ERR_OR_NULL(ts->pins_release)) {
            devm_pinctrl_put(ts->pinctrl);
            ts->pinctrl = NULL;
        } else {
            ret = pinctrl_select_state(ts->pinctrl, ts->pins_release);
            if (ret < 0)
                FTS_ERROR("Set gesture pin state error:%d", ret);
        }
    }

    return ret;
}

static int fts_pinctrl_select_spimode(struct fts_ts_data *ts)
{
	int ret = 0;
	if (ts->pinctrl) {
		if (IS_ERR_OR_NULL(ts->pinctrl_state_spimode)) {
			devm_pinctrl_put(ts->pinctrl);
			ts->pinctrl = NULL;
		} else {
			ret = pinctrl_select_state(ts->pinctrl, ts->pinctrl_state_spimode);
			if (ret < 0)
				FTS_ERROR("Set gesture pin state error:%d", ret);
		}
	}
	return ret;
}

#endif /* FTS_PINCTRL_EN */

static int fts_power_source_ctrl(struct fts_ts_data *ts_data, int enable)
{
	int ret = 0;
	if (IS_ERR_OR_NULL(ts_data->avdd)) {
		FTS_ERROR("avdd is invalid");
		return -EINVAL;
	}
	FTS_FUNC_ENTER();
	if (enable) {
		if (ts_data->power_disabled) {
			FTS_DEBUG("regulator enable !");
			gpio_direction_output(ts_data->pdata->reset_gpio, 0);
			msleep(1);
			if (ts_data->pinctrl && ts_data->pinctrl_dvdd_enable) {
				ret = pinctrl_select_state(ts_data->pinctrl,
				ts_data->pinctrl_dvdd_enable);
				if (ret)
					FTS_ERROR("%s: Failed to enable dvdd, error= %d\n", __func__, ret);
				else
					FTS_INFO("%s: successs to enable dvdd\n", __func__);
			}
			if (!IS_ERR_OR_NULL(ts_data->avdd)) {
				ret = regulator_enable(ts_data->avdd);
				if (ret)
					FTS_ERROR("enable avdd regulator failed,ret=%d", ret);
			}
			if (!IS_ERR_OR_NULL(ts_data->iovdd)) {
				ret = regulator_enable(ts_data->iovdd);
				if (ret)
					FTS_ERROR("enable iovdd regulator failed,ret=%d", ret);
			}
			if (!IS_ERR_OR_NULL(ts_data->iovdd_source)) {
				ret = regulator_enable(ts_data->iovdd_source);
				if (ret)
					FTS_ERROR("enable iovdd_source regulator failed,ret=%d", ret);
			}
			if (!IS_ERR_OR_NULL(ts_data->avdd_source)) {
				ret = regulator_enable(ts_data->avdd_source);
				if (ret)
					FTS_ERROR("enable avdd_source regulator failed,ret=%d", ret);
			}
			FTS_INFO("successs to enable avdd && iovdd\n");
			ts_data->power_disabled = false;
		}
	} else {
		if (!ts_data->power_disabled) {
			FTS_DEBUG("regulator disable !");
			// for Preventing AVDD overvoltage during RST low
			fts_write_reg(SET_ID_G_HOST_RST_FLAG, 0x01);
			msleep(20);
			gpio_direction_output(ts_data->pdata->reset_gpio, 0);
			msleep(1);
			if (!IS_ERR_OR_NULL(ts_data->avdd)) {
				ret = regulator_disable(ts_data->avdd);
				if (ret)
					FTS_ERROR("disable avdd regulator failed,ret=%d", ret);
			}
			if (ts_data->pinctrl && ts_data->pinctrl_dvdd_disable) {
				ret = pinctrl_select_state(ts_data->pinctrl, ts_data->pinctrl_dvdd_disable);
				if (ret) {
					FTS_ERROR("%s: Failed to disable dvdd, error= %d\n",
							__func__, ret);
				} else
					FTS_INFO("%s: successs to disable dvdd\n", __func__);
			}
			if (!IS_ERR_OR_NULL(ts_data->iovdd)) {
				ret = regulator_disable(ts_data->iovdd);
				if (ret)
					FTS_ERROR("disable iovdd regulator failed,ret=%d", ret);
			}
			if (!IS_ERR_OR_NULL(ts_data->iovdd_source)) {
				ret = regulator_disable(ts_data->iovdd_source);
				if (ret)
					FTS_ERROR("disable iovdd_source regulator failed,ret=%d", ret);
			}
			if (!IS_ERR_OR_NULL(ts_data->avdd_source)) {
				ret = regulator_disable(ts_data->avdd_source);
				if (ret)
					FTS_ERROR("disable avdd_source regulator failed,ret=%d", ret);
			}
			ts_data->power_disabled = true;
		}
	}
	FTS_FUNC_EXIT();
	return ret;
}

/*****************************************************************************
* Name: fts_power_source_init
* Brief: Init regulator power:vdd/vcc_io(if have), generally, no vcc_io
*        vdd---->vdd-supply in dts, kernel will auto add "-supply" to parse
*        Must be call after fts_gpio_configure() execute,because this function
*        will operate reset-gpio which request gpio in fts_gpio_configure()
* Input:
* Output:
* Return: return 0 if init power successfully, otherwise return error code
*****************************************************************************/
static int fts_power_source_init(struct fts_ts_data *ts_data)
{
    int ret = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;

    FTS_FUNC_ENTER();
    /* avdd */
    if (pdata->avdd_reg_name != NULL && *pdata->avdd_reg_name != 0) {
        ts_data->avdd = regulator_get(ts_data->dev, pdata->avdd_reg_name);
        if (IS_ERR_OR_NULL(ts_data->avdd)) {
            ret = PTR_ERR(ts_data->avdd);
            ts_data->avdd = NULL;
            FTS_ERROR("get avdd regulator failed,ret=%d", ret);
            return ret;
        }

        if (regulator_count_voltages(ts_data->avdd) > 0) {
            ret = regulator_set_voltage(ts_data->avdd, FTS_VTG_MIN_UV,
                                        FTS_VTG_MAX_UV);
            if (ret) {
                FTS_ERROR("avdd regulator set_vtg failed ret=%d", ret);
                regulator_put(ts_data->avdd);
                ts_data->avdd = NULL;
                return ret;
            }
        }
    }
    /* iovdd */
    if (pdata->iovdd_reg_name != NULL && *pdata->iovdd_reg_name != 0) {
        ts_data->iovdd = regulator_get(ts_data->dev, pdata->iovdd_reg_name);
        if (IS_ERR_OR_NULL(ts_data->iovdd)) {
            ret = PTR_ERR(ts_data->iovdd);
            ts_data->iovdd = NULL;
            FTS_ERROR("get iovdd regulator failed,ret=%d", ret);
            return ret;
        }

        if (regulator_count_voltages(ts_data->iovdd) > 0) {
            ret = regulator_set_voltage(ts_data->iovdd, FTS_I2C_VTG_MIN_UV,
                                        FTS_I2C_VTG_MAX_UV);
            if (ret) {
                FTS_ERROR("iovdd regulator set_vtg failed ret=%d", ret);
                regulator_put(ts_data->iovdd);
                ts_data->iovdd = NULL;
                return ret;
            }
        }
    }

    /* front supply for iovdd, not nessesary */
    if (pdata->iovdd_source_reg_name != NULL && *pdata->iovdd_source_reg_name != 0) {
        ts_data->iovdd_source = regulator_get(ts_data->dev, pdata->iovdd_source_reg_name);
        if (IS_ERR_OR_NULL(ts_data->iovdd_source)) {
            ret = PTR_ERR(ts_data->iovdd_source);
            FTS_ERROR("get iovdd_source regulator failed,ret=%d", ret);
            ts_data->iovdd_source = NULL;
            return ret;
        }

        if (regulator_count_voltages(ts_data->iovdd_source) > 0) {
            ret = regulator_set_voltage(ts_data->iovdd_source, 2704000,
                                        3296000);
            if (ret) {
                FTS_ERROR("iovdd_source regulator set_vtg failed ret=%d", ret);
                regulator_put(ts_data->iovdd_source);
                ts_data->iovdd_source = NULL;
                return ret;
            }
        }
    }

	/* front supply for avdd, not nessesary */
    if (pdata->avdd_source_reg_name != NULL && *pdata->avdd_source_reg_name != 0) {
        ts_data->avdd_source = regulator_get(ts_data->dev, pdata->avdd_source_reg_name);
        if (IS_ERR_OR_NULL(ts_data->avdd_source)) {
            ret = PTR_ERR(ts_data->avdd_source);
            FTS_ERROR("get avdd_source regulator failed,ret=%d", ret);
            ts_data->avdd_source = NULL;
            return ret;
        }

        if (regulator_count_voltages(ts_data->avdd_source) > 0) {
            ret = regulator_set_voltage(ts_data->avdd_source, 3400000,
                                        3400000);
            if (ret) {
                FTS_ERROR("avdd_source regulator set_vtg failed ret=%d", ret);
                regulator_put(ts_data->avdd_source);
                ts_data->avdd_source = NULL;
                return ret;
            }
        }
    }

#if FTS_PINCTRL_EN
    fts_pinctrl_init(ts_data);
    fts_pinctrl_select_normal(ts_data);
    fts_pinctrl_select_spimode(ts_data);
#endif

    ts_data->power_disabled = true;
    ret = fts_power_source_ctrl(ts_data, ENABLE);
    if (ret) {
        FTS_ERROR("fail to enable power(regulator)");
    }

    FTS_FUNC_EXIT();
    return ret;
}

static int fts_power_source_exit(struct fts_ts_data *ts_data)
{
#if FTS_PINCTRL_EN
    fts_pinctrl_select_release(ts_data);
#endif

    fts_power_source_ctrl(ts_data, DISABLE);

    if (!IS_ERR_OR_NULL(ts_data->avdd)) {
        if (regulator_count_voltages(ts_data->avdd) > 0)
            regulator_set_voltage(ts_data->avdd, 0, FTS_VTG_MAX_UV);
        regulator_put(ts_data->avdd);
    }

    if (!IS_ERR_OR_NULL(ts_data->iovdd)) {
        if (regulator_count_voltages(ts_data->iovdd) > 0)
            regulator_set_voltage(ts_data->iovdd, 0, FTS_I2C_VTG_MAX_UV);
        regulator_put(ts_data->iovdd);
    }

    if (!IS_ERR_OR_NULL(ts_data->iovdd_source)) {
        if (regulator_count_voltages(ts_data->iovdd_source) > 0)
            regulator_set_voltage(ts_data->iovdd_source, 0, FTS_VTG_MAX_UV);
        regulator_put(ts_data->iovdd_source);
    }

    if (!IS_ERR_OR_NULL(ts_data->avdd_source)) {
        if (regulator_count_voltages(ts_data->avdd_source) > 0)
            regulator_set_voltage(ts_data->avdd_source, 0, 3400000);
        regulator_put(ts_data->avdd_source);
    }

    return 0;
}
#endif /* FTS_POWER_SOURCE_CUST_EN */

static int fts_gpio_configure(struct fts_ts_data *data)
{
    int ret = 0;

    FTS_FUNC_ENTER();
    /* request irq gpio */
    if (gpio_is_valid(data->pdata->irq_gpio)) {
        ret = gpio_request(data->pdata->irq_gpio, "fts_irq_gpio");
        if (ret) {
            FTS_ERROR("[GPIO]irq gpio request failed");
            goto err_irq_gpio_req;
        }

        ret = gpio_direction_input(data->pdata->irq_gpio);
        if (ret) {
            FTS_ERROR("[GPIO]set_direction for irq gpio failed");
            goto err_irq_gpio_dir;
        }
    }

    /* request reset gpio */
    if (gpio_is_valid(data->pdata->reset_gpio)) {
        ret = gpio_request(data->pdata->reset_gpio, "fts_reset_gpio");
        if (ret) {
            FTS_ERROR("[GPIO]reset gpio request failed");
            goto err_irq_gpio_dir;
        }

        /*
        ret = gpio_direction_output(data->pdata->reset_gpio, 1);
        if (ret) {
            FTS_ERROR("[GPIO]set_direction for reset gpio failed");
            goto err_reset_gpio_dir;
        }*/

    }

    FTS_FUNC_EXIT();
    return 0;

err_irq_gpio_dir:
    if (gpio_is_valid(data->pdata->irq_gpio))
        gpio_free(data->pdata->irq_gpio);
err_irq_gpio_req:
    FTS_FUNC_EXIT();
    return ret;
}

static int fts_get_dt_coords(struct device *dev, char *name,
                             struct fts_ts_platform_data *pdata)
{
    int ret = 0;
    u32 coords[FTS_COORDS_ARR_SIZE] = { 0 };
    struct property *prop;
    struct device_node *np = dev->of_node;
    int coords_size;

    prop = of_find_property(np, name, NULL);
    if (!prop)
        return -EINVAL;
    if (!prop->value)
        return -ENODATA;

    coords_size = prop->length / sizeof(u32);
    if (coords_size != FTS_COORDS_ARR_SIZE) {
        FTS_ERROR("invalid:%s, size:%d", name, coords_size);
        return -EINVAL;
    }

    ret = of_property_read_u32_array(np, name, coords, coords_size);
    if (ret < 0) {
        FTS_ERROR("Unable to read %s, please check dts", name);
        pdata->x_min = FTS_X_MIN_DISPLAY_DEFAULT;
        pdata->y_min = FTS_Y_MIN_DISPLAY_DEFAULT;
        pdata->x_max = FTS_X_MAX_DISPLAY_DEFAULT;
        pdata->y_max = FTS_Y_MAX_DISPLAY_DEFAULT;
        return -ENODATA;
    } else {
        pdata->x_min = coords[0];
        pdata->y_min = coords[1];
        pdata->x_max = coords[2];
        pdata->y_max = coords[3];
    }

    FTS_INFO("display x(%d %d) y(%d %d)", pdata->x_min, pdata->x_max,
             pdata->y_min, pdata->y_max);
    return 0;
}

static int fts_parse_dt(struct device *dev, struct fts_ts_platform_data *pdata)
{
    int ret = 0;
    struct device_node *np = dev->of_node;
    u32 temp_val = 0;
    const char *name;

    FTS_FUNC_ENTER();

    ret = fts_get_dt_coords(dev, "focaltech,display-coords", pdata);
    if (ret < 0)
        FTS_ERROR("Unable to get display-coords");

    /* key */
    pdata->have_key = of_property_read_bool(np, "focaltech,have-key");
    if (pdata->have_key) {
        ret = of_property_read_u32(np, "focaltech,key-number", &pdata->key_number);
        if (ret < 0)
            FTS_ERROR("Key number undefined!");

        ret = of_property_read_u32_array(np, "focaltech,keys",
                                         pdata->keys, pdata->key_number);
        if (ret < 0)
            FTS_ERROR("Keys undefined!");
        else if (pdata->key_number > FTS_MAX_KEYS)
            pdata->key_number = FTS_MAX_KEYS;

        ret = of_property_read_u32_array(np, "focaltech,key-x-coords",
                                         pdata->key_x_coords,
                                         pdata->key_number);
        if (ret < 0)
            FTS_ERROR("Key Y Coords undefined!");

        ret = of_property_read_u32_array(np, "focaltech,key-y-coords",
                                         pdata->key_y_coords,
                                         pdata->key_number);
        if (ret < 0)
            FTS_ERROR("Key X Coords undefined!");

        FTS_INFO("VK Number:%d, key:(%d,%d,%d), "
                 "coords:(%d,%d),(%d,%d),(%d,%d)",
                 pdata->key_number,
                 pdata->keys[0], pdata->keys[1], pdata->keys[2],
                 pdata->key_x_coords[0], pdata->key_y_coords[0],
                 pdata->key_x_coords[1], pdata->key_y_coords[1],
                 pdata->key_x_coords[2], pdata->key_y_coords[2]);
    }

    /* regulator info */
    ret = of_property_read_string(np, "focaltech,iovdd_source-name", &name);
    if (ret < 0) {
        FTS_ERROR("focaltech,iovdd_source-name undefined");
        pdata->iovdd_source_reg_name = NULL;
    } else
        pdata->iovdd_source_reg_name = name;

    ret = of_property_read_string(np, "focaltech,avdd_source-name", &name);
    if (ret < 0) {
        FTS_ERROR("focaltech,avdd_source-name undefined");
        pdata->avdd_source_reg_name = NULL;
    } else
        pdata->avdd_source_reg_name = name;

    ret = of_property_read_string(np, "focaltech,iovdd-name", &name);
    if (ret < 0) {
        FTS_ERROR("focaltech,iovdd-name undefined");
        pdata->iovdd_reg_name = NULL;
    } else
        pdata->iovdd_reg_name = name;

    ret = of_property_read_string(np, "focaltech,avdd-name", &name);
    if (ret < 0) {
        FTS_ERROR("focaltech,avdd-name undefined");
        pdata->avdd_reg_name = NULL;
    } else
        pdata->avdd_reg_name = name;

    /* reset, irq gpio info */
    pdata->reset_gpio = of_get_named_gpio(np, "focaltech,reset-gpio", 0);
    if (pdata->reset_gpio < 0)
        FTS_ERROR("Unable to get reset_gpio");

    pdata->irq_gpio = of_get_named_gpio(np, "focaltech,irq-gpio", 0);
    if (pdata->irq_gpio < 0)
        FTS_ERROR("Unable to get irq_gpio");
	else
		pdata->irq_gpio_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;

    ret = of_property_read_u32(np, "focaltech,super-resolution-factors", &temp_val);
    if (ret < 0) {
	    FTS_ERROR("Unable to get super-resolution-factors, please use default");
	    pdata->super_resolution_factors = 1;
    }  else
	    pdata->super_resolution_factors = temp_val;

    ret = of_property_read_u32(np, "focaltech,max-touch-number", &temp_val);
    if (ret < 0) {
        FTS_ERROR("Unable to get max-touch-number, please check dts");
        pdata->max_touch_number = FTS_MAX_POINTS_SUPPORT;
    } else {
        if (temp_val < 2)
            pdata->max_touch_number = 2; /* max_touch_number must >= 2 */
        else if (temp_val > FTS_MAX_POINTS_SUPPORT)
            pdata->max_touch_number = FTS_MAX_POINTS_SUPPORT;
        else
            pdata->max_touch_number = temp_val;
    }

    FTS_INFO("max touch number:%d, irq gpio:%d, reset gpio:%d",
             pdata->max_touch_number, pdata->irq_gpio, pdata->reset_gpio);


#ifdef FTS_XIAOMI_TOUCHFEATURE
	ret = of_property_read_u32_array(np, "focaltech,touch-def-array",
						pdata->touch_def_array, 4);
	if (ret < 0) {
		FTS_ERROR("Unable to get touch default array, please check dts");
		return ret;
	}
	ret = of_property_read_u32_array(np, "focaltech,touch-range-array",
						pdata->touch_range_array, 5);
	if (ret < 0) {
		FTS_ERROR("Unable to get touch range array, please check dts");
		return ret;
	}
	ret = of_property_read_u32_array(np, "focaltech,touch-expert-array",
						pdata->touch_expert_array, 4 * EXPERT_ARRAY_SIZE);
	if (ret < 0) {
		FTS_ERROR("Unable to get touch expert array, please check dts");
		return ret;
	}
#endif

    FTS_FUNC_EXIT();
    return 0;
}

/**
 * @brief Write 1/0 to Touch IC 0x8B register depending on whether it is in charge state
 */
static void fts_set_charge_state(int status)
{
	int ret;
	if (status == fts_data->charger_status) {
		FTS_INFO("last charger status id %d, equal, skip", status);
	}

	fts_data->charger_status = status;
	if(status) {
		FTS_INFO("charger usb in");
		ret = fts_write_reg(FTS_REG_CHARGER_MODE_EN, true);
		if (ret < 0) {
			FTS_ERROR("failed to set power supply status:%d", fts_data->charger_status);
		} else {
			FTS_INFO("success to set power supply status:%d", fts_data->charger_status);
		}
	}else {
		FTS_INFO("charger usb out");
		ret = fts_write_reg(FTS_REG_CHARGER_MODE_EN, false);
		if (ret < 0) {
			FTS_ERROR("failed to set power supply status:%d", fts_data->charger_status);
		} else {
			FTS_INFO("success to set power supply status:%d", fts_data->charger_status);
		}
	}
}
#ifdef FTS_TOUCHSCREEN_FOD
static void fts_xiaomi_touch_fod_test(int value)
{
	struct input_dev *input_dev = fts_data->input_dev;

	if (value) {
		update_fod_press_status_common(1);
		/* mi_disp_set_fod_queue_work(1, true); */
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_key(input_dev, BTN_TOOL_FINGER, 1);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, 60900);	//609*16 =9744
		input_report_abs(input_dev, ABS_MT_POSITION_Y, 243700);	//2437*16 = 38992
		input_sync(input_dev);
	} else {
		input_mt_slot(input_dev, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, -1);
		update_fod_press_status_common(0);
		/* mi_disp_set_fod_queue_work(0, true); */
		input_sync(input_dev);
	}
}
#endif
#ifdef FTS_XIAOMI_TOUCHFEATURE

hardware_operation_t hardware_operation;
hardware_param_t hardware_param;
/*static struct xiaomi_touch_interface xiaomi_touch_interfaces;*/

static void fts_update_gesture_state(struct fts_ts_data *ts_data, int bit, bool enable)
{
	u8 cmd_shift = 0;
	if (bit == GESTURE_DOUBLETAP)
		cmd_shift = FTS_GESTURE_DOUBLETAP;
	else if (bit == GESTURE_AOD)
		cmd_shift = FTS_GESTURE_AOD;
	else if (bit == GESTURE_WEAK_DOUBLETAP)
		cmd_shift = FTS_GESTURE_WEAK_DOUBLETAP;
	mutex_lock(&ts_data->input_dev->mutex);
	if (enable) {
		ts_data->gesture_status |= 1 << bit;
		ts_data->gesture_cmd |= 1 << cmd_shift;
	} else {
		ts_data->gesture_status &= ~(1 << bit);
		ts_data->gesture_cmd &= ~(1 << cmd_shift);
	}

	FTS_DEBUG("AOD: %d DoubleClick: %d ", ts_data->gesture_status>>1 & 0x01, ts_data->gesture_status & 0x01);
	FTS_INFO("gesture state:0x%02X, write cmd:0x%02X", ts_data->gesture_status, ts_data->gesture_cmd);
	ts_data->gesture_support = ts_data->gesture_status != 0 ? ENABLE : DISABLE;
	mutex_unlock(&ts_data->input_dev->mutex);
}

// static void fts_restore_mode_value(int mode, int value_type)
// {
// 	// touch_mode[mode][SET_CUR_VALUE] = touch_mode[mode][value_type];
// }

// static void fts_restore_normal_mode(void)
// {
// 	int i;
// 	for (i = 0; i <= DATA_MODE_8; i++) {
// 		if (i != DATA_MODE_8)
// 			fts_restore_mode_value(i, GET_DEF_VALUE);
// 	}
// }

/*
 *static void fts_write_touchfeature_reg(int mode)
 *{
 *	int ret = 0;
 *	u8 temp_value = (u8)touch_mode[mode][SET_CUR_VALUE];
 *
 *	switch (mode) {
 *	case DATA_MODE_0:
 *		if (temp_value && touch_mode[DATA_MODE_8][GET_CUR_VALUE] == 1) {
 *			temp_value = 3;
 *			ret = fts_write_reg(FTS_REG_EDGE_FILTER_EN, temp_value);
 *		} else if (temp_value && touch_mode[DATA_MODE_8][GET_CUR_VALUE] == 2) {
 *			temp_value = 4;
 *			ret = fts_write_reg(FTS_REG_EDGE_FILTER_EN, temp_value);
 *		} else if (!temp_value)
 *			fts_restore_normal_mode();
 *		break;
 *	case DATA_MODE_2:
 *		ret = fts_write_reg(FTS_REG_SENSIVITY, temp_value);
 *		break;
 *	case DATA_MODE_3:
 *		ret = fts_write_reg(FTS_REG_THDIFF, 3 - temp_value);
 *		break;
 *	case DATA_MODE_4:
 *		ret = fts_write_reg();
 *		break;
 *	case DATA_MODE_5:
 *		ret = fts_write_reg();
 *		break;
 *	case DATA_MODE_7:
 *		ret = fts_write_reg(FTS_REG_EDGE_FILTER_LEVEL, temp_value);
 *		break;
 *	case DATA_MODE_8:
 *		if (temp_value == PANEL_ORIENTATION_DEGREE_90 && touch_mode[DATA_MODE_0][GET_CUR_VALUE] == 0)
 *			temp_value = 1;
 *		else if (temp_value == PANEL_ORIENTATION_DEGREE_270 && touch_mode[DATA_MODE_0][GET_CUR_VALUE] == 0)
 *			temp_value = 2;
 *		else if (temp_value == PANEL_ORIENTATION_DEGREE_90 && touch_mode[DATA_MODE_0][GET_CUR_VALUE] == 1)
 *			temp_value = 3;
 *		else if (temp_value == PANEL_ORIENTATION_DEGREE_270 && touch_mode[DATA_MODE_0][GET_CUR_VALUE] == 1)
 *			temp_value = 4;
 *		else
 *			temp_value = 0;
 *		ret = fts_write_reg(FTS_REG_EDGE_FILTER_EN, temp_value);
 *		break;
 *	case DATA_MODE_1:
 *		break;
 *	default:
 *		ret = -1;
 *		break;
 *	}
 *	if (ret < 0) {
 *		FTS_ERROR("write mode:%d reg failed", mode);
 *	} else {
 *		FTS_INFO("write mode:%d value:%d success", mode, temp_value);
 *		touch_mode[mode][GET_CUR_VALUE] = temp_value;
 *	}
 *}
 */

static void fts_set_fod_downup(struct fts_ts_data *ts_data, int enable)
{
	if (enable) {
		FTS_INFO("fod down");
		update_fod_press_status_common(1);
	} else {
		FTS_INFO("fod up");
		update_fod_press_status_common(0);
		if (ts_data->finger_in_fod) {
			FTS_INFO("reset finger_in_fod to false");
			ts_data->finger_in_fod = false;
		}
	}
}

int fts_switch_report_rate(struct fts_ts_data *ts_data, bool on)
{
	int ret = 0;
	FTS_INFO("on: %d, set Report_Rate_status:%s", on,  (on == true) ? "240HZ" : "120HZ");
	if (on == 0) {
		ts_data->report_rate_status = 120;
		/*update Report_Rate to 120HZ*/
		ret = fts_write_reg(0x92, 1);
		if (ret < 0) {
			FTS_ERROR("Failed to switch Report_Rate to 120HZ, ret=%d", ret);
			return ret;
		}
	}
	if (on == 1) {
		ts_data->report_rate_status = 240;
		/*update Report_Rate to 240HZ*/
		ret = fts_write_reg(0x92, 0);
		if (ret < 0) {
			FTS_ERROR("Failed to switch Report_Rate to 240HZ, ret=%d", ret);
			return ret;
		}
	}

	return ret;
}

int fts_enable_idle_high_refresh(int en)
{
	u8 buf_temp[3] = {0};
	buf_temp[0] = SET_IDLE_HIGH_BASE_EN_TYPE;
	buf_temp[1] = 0x00;
	if(en)
		buf_temp[2] = 0x01;
	else
		buf_temp[2] = 0x00;
	return fts_write(buf_temp, 3);
}

int fts_enable_idle_high_refresh_cycle(int *value)
{
	u8 writebuf_temp[3] ={0};
	writebuf_temp[0] = SET_IDLE_HIGH_BASE_T_TYPE;
	writebuf_temp[1] =  ((value[1] >> 8) & 0xFF);
	writebuf_temp[2] =  (value[1] & 0xFF);
	return fts_write(writebuf_temp, 3);
}

#if 0
int fts_htc_enter_idle(int *value)
{
	u8 writebuf[3] = { 0 };
	int en_status = value[0];
	FTS_INFO("idle data0[en_status]: %d, data1: %d, data2: %d", en_status, value[1], value[2]);
	writebuf[0] = 0x2c;
	if(en_status)
	{
		/*enter idle*/
		if (value[1])
		{
			/*game mode for idle time*/
			fts_enable_idle_high_refresh(1);
			fts_enable_idle_high_refresh_cycle(value);
			writebuf[0] = 0x37;
			writebuf[1] =  ((value[2] >> 8) & 0xFF);
			writebuf[2] =  (value[2] & 0xFF);
		} else {
			/*nomal mode for idle*/
			writebuf[1] = 0x00;
			writebuf[2] = 0x02;
		}
	} else {
		/*exit idle*/
		writebuf[1] = 0x00;
		writebuf[2] = 0x01;
	}

	FTS_INFO("writebuf[0]:%d, writebuf[1]:%d, writebuf[2]:%d", writebuf[0], writebuf[1], writebuf[2]);
	return fts_write(writebuf, 3);
}
#endif

int fts_set_idle_high_refresh_mode(u8 mode, int value)
{
        u8 buf_temp[3] = {0};
        bool modeSupport = true;
        switch(mode){
          case SET_IDLE_HIGH_BASE_EN_TYPE:
            buf_temp[0] = mode;
            buf_temp[1] = 0x00;
            if (value)
              buf_temp[2] = 0x01;
            else
              buf_temp[2] = 0x00;
            break;
          case SET_IDLE_HIGH_BASE_T_TYPE:
            buf_temp[0] = mode;
            buf_temp[1] = ((value >> 8) & 0xFF);
            buf_temp[2] = (value & 0xFF);
            break;
          case SET_IDLE_HIGH_BASE_KEEP_TIME_TYPE:
            buf_temp[0] = mode;
            buf_temp[1] = ((value >> 8) & 0xFF);
            buf_temp[2] = (value & 0xFF);
            break;
          default:
            modeSupport = false;
            FTS_INFO("not support mode!");
            break;
        }

        FTS_DEBUG("idle mode: 0x%x, value: %d ", mode, value);

        return modeSupport == false ? -1 : fts_write(buf_temp, 3);
}


int fts_send_camera_report_rate(int value)
{
	int ret = 0;
	u8 writebuf[2] = { 0 };

	if (value == 256) {
		FTS_INFO("value[%d]: exit camera,report to 240hz ", value);
		writebuf[1] = 0x00; /* 240Hz */
	} else if (value == 257) {
		FTS_INFO("value[%d]: enter camera, report to 135hz ", value);
		writebuf[1] = 0x01; /* 135Hz */
	} else {
		FTS_INFO("value[%d], return", value);
		return 0;
	}

	writebuf[0] = SET_CAMERA_STATUS_REPORT_RATE;
	ret = fts_write(writebuf, 2);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
			writebuf[0], writebuf[1], ret);
	}

	return ret;
}

#define P_ACTIVE	0
#define P_MONITOR	1
int fts_htc_enter_idle(int *value)
{
	int ret;
	int idle_status = value[0];
	int idle_scan_cycle = 0;
	int idle_scan_time = 0;

	/* enable idle high refresh mode */
	ret = fts_set_idle_high_refresh_mode(SET_IDLE_HIGH_BASE_EN_TYPE, idle_status == P_ACTIVE ? ENABLE : DISABLE);
	if (ret < 0)
		FTS_ERROR("fail send send SET_IDLE_HIGH_BASE_EN_TYPE cmd, ret= %d !", ret);
	if (idle_status == P_ACTIVE) {
		idle_scan_cycle = value[1];
		idle_scan_time = value[2];
		/* set idle high refresh scan sycle */
		ret = fts_set_idle_high_refresh_mode(SET_IDLE_HIGH_BASE_T_TYPE, idle_scan_cycle);
		if (ret < 0)
			FTS_ERROR("fail send send SET_IDLE_HIGH_BASE_T_TYPE cmd, ret= %d !", ret);
		/* set idle high refresh keep time*/
		ret = fts_set_idle_high_refresh_mode(SET_IDLE_HIGH_BASE_KEEP_TIME_TYPE, idle_scan_time);
		if (ret < 0)
			FTS_ERROR("fail send send SET_IDLE_HIGH_BASE_KEEP_TIME_TYPE cmd, ret= %d !", ret);
	}
	/* set work mode always 1*/
	idle_status = P_MONITOR;
	ret = fts_thp_ic_write_interfaces(SET_IC_WORK_MODE_TYPE, (s32 *)&idle_status, 1);
	if (ret < 0)
		FTS_ERROR("fail send send idle cmd, ret= %d", ret);
	FTS_DEBUG("idle send suscess, data0:%d idle_scan_cycle:%d idle_scan_time:%d", idle_status, idle_scan_cycle, idle_scan_time);
	return ret;
}

/*  00: no reduce(100%)
    01: reduce 10%
    ... 
    09: reduce 90% */
int fts_set_idle_threshold(int thresh)
{
	u8 writebuf[3] = { 0 };
	writebuf[0] = SET_IDLE_PERCENTAGE_THD_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = (u8)(thresh & 0xFF);
	return fts_write(writebuf, 3);
}

int fts_htc_update_idle_baseline(void)
{
	u8 writebuf[3] = { 0 };
	writebuf[0] = SET_IDLE_BASE_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = 0x01;
	return fts_write(writebuf, 3);
}

/*
set ic freq hopping value 
0-7 bit:freq;
7 bit 0x80:freq hopping enable
*/

int fts_htc_set_freq_hopping(int value)
{
	int ret = 0;
	u8 writebuf[3] = { 0 };
	writebuf[0] = SET_SCAN_FREQ_NUM_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = (u8)(value & 0x7F);
	ret = fts_write(writebuf, 3);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
			writebuf[0], writebuf[2], ret);
	}
	//7bit
	writebuf[0] = SET_SCAN_FREQ_HOPPING_EN_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = (u8)(value & 0x80);
	ret = fts_write(writebuf, 3);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
			writebuf[0], writebuf[2], ret);
	}
	return ret;
}

/*
set ic calibration value 
0-3 bit mc calibra:0 close,1 enable; 
4-7 bit sc calibra:0 close,1 enable
*/
int fts_htc_set_calibration(int value)
{
	int ret = 0;
	u8 writebuf[3] = { 0 };

	if (value == 0x11) {
		writebuf[0] = SET_BASE_REFRESH_EN_TYPE;
		writebuf[1] = 0x00;
		writebuf[2] = 0x01;
		ret = fts_write(writebuf, 3);
		if (ret < 0) {
			FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
					writebuf[0], writebuf[2], ret);
		}
		return ret;
	}

	//0-3bit:
	writebuf[0] = SET_MC_CALIBRATION_EN_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = (u8)(value & 0x0F);
	ret = fts_write(writebuf, 3);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
			writebuf[0], writebuf[2], ret);
	}
	//4-7bit
	writebuf[0] = SET_SC_CALIBRATION_EN_TYPE;
	writebuf[1] = 0x00;
	writebuf[2] = (u8)(value & 0xF0);
	ret = fts_write(writebuf, 3);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
			writebuf[0], writebuf[2], ret);
	}
	return ret;
}

/*
set ic gesture_baseline_feedback,
value 1:success
*/
int fts_htc_set_gesture_feedback(int value)
{
	int ret = 0;
	u8 writebuf[2] = { 0 };

	writebuf[0] = SET_IC_GESTRUE_FEEDBACK;
	writebuf[1] = (u8)(value & 0xFF);
	ret = fts_write(writebuf, 2);
	if (ret < 0) {
		FTS_ERROR("data write(addr:%x) fail,value:%x,ret:%d",
				writebuf[0], writebuf[1], ret);
	}
	return ret;
}

static int fts_htc_enable_empty_int(bool en)
{
	int ret = -1;

	ret = fts_write_reg(SET_EMPTY_INT_EN_TYPE, (en == true) ? 0x1 : 0x0);
	FTS_INFO("%s send empty int cmd %d, ret %d",
		ret ? "failed" : "success", en, ret);

	return ret;
}

static void fts_set_cur_value(int mode_input, int *value_input)
{
	int mode = mode_input;
	int value = value_input[0];
	int ret = 0;
	if (!fts_data || mode < 0) {
		FTS_ERROR("Error, fts_data is NULL or the parameter is incorrect");
		return;
	}
	if (fts_data->suspended && mode_input != DATA_MODE_138) {
		FTS_INFO("tp is suspend, skip set_cur_value: touch mode:%d, value:%d", mode, value);
		return;
	}
	if (ic_in_selftest) {
		FTS_INFO("tp is in selftest, skip set_cur_value: touch mode:%d, value:%d", mode, value);
		return;
	}

	if (mode_input != DATA_MODE_153)
		FTS_INFO("touch mode:%d, value:%d", mode, value);
	else
		FTS_DEBUG("touch mode:%d, value:%d", mode, value);
	if (mode == DATA_MODE_9) {
		FTS_INFO("Mode:DATA_MODE_9  Report_Rate_status = %d", value);
		if (value == 0) {
			fts_data->report_rate_status = 120;
			/*update Report_Rate to 120HZ*/
			ret = fts_write_reg(0x92, 1);
			if (ret < 0) {
				FTS_ERROR("Failed to switch Report_Rate to 120HZ, ret=%d", ret);
				return;
			}
		}
		if (value == 1) {
			fts_data->report_rate_status = 240;
			/*update Report_Rate to 240HZ*/
			ret = fts_write_reg(0x92, 0);
			if (ret < 0) {
				FTS_ERROR("Failed to switch Report_Rate to 240HZ, ret=%d", ret);
				return;
			}
		}
		return;
	}
	// for camera 
	if (mode == DATA_MODE_25) {
		fts_send_camera_report_rate(value);
		return;
	}
	/*for thp cmd*/
	if(mode == DATA_MODE_53) {
		fts_htc_enable_empty_int(!!value);
	}
	if(mode == DATA_MODE_52) {
		int touch_boost = -1;
		FTS_INFO("notify hal to boost");
		add_common_data_to_buf_common(0, SET_CUR_VALUE, DATA_MODE_178, 1, &touch_boost);
		return;
	}
	if(mode == DATA_MODE_73) {
		fts_data->report_rate_status = value;
		FTS_INFO("ic report rate skip write");
		return;
	}
	if (mode == DATA_MODE_63) {
		if (fts_data->enable_touch_raw)
			fts_set_fod_downup(fts_data, value);
		return;
	}
	if (mode == DATA_MODE_66) {
		if (fts_data->enable_touch_raw) {
			FTS_INFO("hal init ready.");
			schedule_delayed_work(&fts_data->thp_signal_work, 1 * HZ);
		}
		return;
	}
	if (mode == DATA_MODE_54) {
		fts_switch_report_rate(fts_data, !!value);
		return;
    }
	if (mode == DATA_MODE_62) {
		/*TO DO: ENRER IDLE*/
		fts_htc_enter_idle(value_input);
		return;
	}
	if (mode == DATA_MODE_146) {
		/*TO DO: SET_DOZE_WAKEUP_THRESHOLD*/
		if (fts_data->enable_touch_raw)
			fts_set_idle_threshold(value);
		return;
	}
	if (mode == DATA_MODE_133) {
		/*TO DO: UPDATE_IDLE_BASELINE*/
		fts_htc_update_idle_baseline();
		return;
	}
#if defined(TOUCH_DUMP_TIC_SUPPORT)
	if (mode == DATA_MODE_138) {
		if (value == DUMP_OFF || value == DUMP_ON) {
			FTS_DEBUG("change dump state(%d) as %d", fts_data->dump_type, value);
			fts_data->dump_type = value;
		}
	}
#endif
	if (mode == DATA_MODE_160) {
		fts_htc_set_freq_hopping(value);
		return;
	}
	if (mode == DATA_MODE_161) {
		fts_htc_set_calibration(value);
		return;
	}
	if (mode == DATA_MODE_165) {
		fts_htc_set_gesture_feedback(value);
		return;
	}
	if (mode == DATA_MODE_177) {
		update_weak_doubletap_value(value);
		return;
	}
	return;
}

static void fts_ic_switch_mode(u8 _gesture_type)
{
	struct fts_ts_data *ts_data = fts_data;
	int value = 0;
	static u8 last_gesture_type = 0;
	static u8 last_sensor_tap_status = 0;

#if IS_ENABLED(CONFIG_MITEE_TUI_SUPPORT)
	if (atomic_read(&ts_data->tui_process)) {
		if (wait_for_completion_interruptible(&ts_data->tui_finish) ) {
			FTS_ERROR("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		FTS_INFO("wait finished, its time to go ahead");
	}
#endif
	ts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	ts_data->aod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_11);
	ts_data->doubletap_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_14);
	ts_data->sensor_tap_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_44);
	FTS_INFO("fod_status:%d, doubletap_status:%d, aod_status:%d, sensor_tap_status:%d", ts_data->fod_status, ts_data->doubletap_status, ts_data->aod_status, ts_data->sensor_tap_status);

	if ((_gesture_type & GESTURE_SINGLETAP_EVENT) || (last_gesture_type & GESTURE_SINGLETAP_EVENT)) { /* DATA_MODE_11 */
		value = _gesture_type & GESTURE_SINGLETAP_EVENT;
		if ((_gesture_type & GESTURE_SINGLETAP_EVENT) ^ (last_gesture_type & GESTURE_SINGLETAP_EVENT)) {/* when aod gesture state change */
			FTS_DEBUG("need to update aod status, value = %d", value);
			fts_update_gesture_state(ts_data, GESTURE_AOD, value != 0 ? true : false);
		}
	}

	if ((_gesture_type & GESTURE_DOUBLETAP_EVENT) || (last_gesture_type & GESTURE_DOUBLETAP_EVENT)) { /* DATA_MODE_14 */
		value = _gesture_type & GESTURE_DOUBLETAP_EVENT;
		if ((_gesture_type & GESTURE_DOUBLETAP_EVENT) ^ (last_gesture_type & GESTURE_DOUBLETAP_EVENT)) {/* when double tap gesture state change */
			FTS_DEBUG("need to update double tap status");
			fts_update_gesture_state(fts_data, GESTURE_DOUBLETAP, value != 0 ? true : false);
			/* when doubletap turn off, turn off sehsor tap */
			if (ts_data->sensor_tap_status && value == 0) {
				FTS_INFO("turn off sensor tap status");
				ts_data->sensor_tap_status = 0;
				fts_update_gesture_state(fts_data, GESTURE_WEAK_DOUBLETAP, false);
				last_sensor_tap_status = ts_data->sensor_tap_status;
			}
		}
	}

	if (ts_data->sensor_tap_status ^ last_sensor_tap_status) {/* when sensor tap gesture state change */
		FTS_DEBUG("need to update sensor tap status");
		/* only enable sensor tap when doubletap enabled */
		if (ts_data->doubletap_status) {
			fts_update_gesture_state(fts_data, GESTURE_WEAK_DOUBLETAP, ts_data->sensor_tap_status != 0 ? true : false);
			last_sensor_tap_status = ts_data->sensor_tap_status;
		} else {
			if (!ts_data->sensor_tap_status) {
				fts_update_gesture_state(fts_data, GESTURE_WEAK_DOUBLETAP, false);
				last_sensor_tap_status = ts_data->sensor_tap_status;
			} else {
				FTS_INFO("skip update sensor tap status");
				return;
			}
		}
	}

	if ((_gesture_type & GESTURE_LONGPRESS_EVENT) || (last_gesture_type & GESTURE_LONGPRESS_EVENT)) { /* Touch_Fod_Mode */
		value = _gesture_type & GESTURE_LONGPRESS_EVENT;
		if ((_gesture_type & GESTURE_LONGPRESS_EVENT) ^ (last_gesture_type & GESTURE_LONGPRESS_EVENT)) {/* when FOD state change */
			FTS_DEBUG("need to update fod status");
			fts_update_gesture_state(ts_data, GESTURE_FOD, value != 0 ? true : false);
		}
	}

	/* change IC status when suspended */
	if (ts_data->suspended) {
		// sleep -> gesture
		if (ts_data->poweroff_on_sleep && ts_data->gesture_support) {
			fts_recover_gesture_from_sleep(ts_data);
			ts_data->poweroff_on_sleep = false;
		} else if (!ts_data->poweroff_on_sleep && !ts_data->gesture_support) {
		// gesture -> sleep
			fts_recover_sleep_from_gesture(ts_data);
			ts_data->poweroff_on_sleep = true;
		} else if (!ts_data->poweroff_on_sleep && ts_data->gesture_support) {
		// gesture -> gesture
			fts_write_reg(FTS_GESTURE_CTRL, ts_data->gesture_cmd);
			if (ts_data->fod_status) {
				if (ts_data->gesture_status & (1 << GESTURE_FOD))
					fts_fod_recovery();
				else
					fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, false);
			}
		}
	}

	last_gesture_type = _gesture_type;
}

static void fts_game_mode_update(long mode_update_flag, int mode_value[DATA_MODE_45])
{
	int mode = 0;
	s32 temp_value = 0;
	int ret = 0;
	u8 cmd[7] = {0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int i = 0;

	if (!mode_update_flag) {
		FTS_INFO("no need update mode value");
		return;
	}
	if (fts_data && fts_data->suspended) {
		FTS_INFO("tp is suspend, skip game_mode_update: mode_update_flag = %ld", mode_update_flag);
		return;
	}

#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
	if (fts_data && fts_data->pm_suspend) {
		FTS_ERROR("SYSTEM is in suspend mode, don't set touch mode data");
		return;
	}
#endif

	pm_stay_awake(fts_data->dev);
	mutex_lock(&fts_data->cmd_update_mutex);


#ifdef TOUCH_THP_SUPPORT
	if (fts_data->enable_touch_raw) {
		temp_value = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_0);
		FTS_INFO("game mode status: %s   %d", temp_value ? "ON" : "OFF", temp_value);
		ret = fts_thp_ic_write_interfaces(SET_GAME_MODE_EN_TYPE, &temp_value, 1);
		if (ret < 0) {
			FTS_ERROR("failed to send game mode: %d, addr:%d, ret=%d", temp_value, SET_GAME_MODE_EN_TYPE, ret);
		}
		mutex_unlock(&fts_data->cmd_update_mutex);
		pm_relax(fts_data->dev);
		return;
	}
#endif

	// addr:0xC1, cmd 7, mode 0~6
	for (i = 0; i <= DATA_MODE_6; i++) {
		switch (i) {
		case DATA_MODE_0:
			temp_value = mode_value[DATA_MODE_0];
			cmd[1] = (u8)(temp_value);
			FTS_INFO("DATA_MODE_0, value=%d, cmd=%d", temp_value, cmd[1]);
			break;
		case DATA_MODE_1:
			temp_value = mode_value[DATA_MODE_1];
			cmd[2] = (u8)(temp_value ? 30 : 3);
			FTS_INFO("DATA_MODE_1, value=%d, cmd=%d", temp_value, cmd[2]);
			break;
		case DATA_MODE_2:
			temp_value = mode_value[DATA_MODE_2];
			cmd[4] = (u8)(*(fts_data->pdata->touch_range_array + temp_value - 1));
			FTS_INFO("DATA_MODE_2, value=%d, cmd=%d", temp_value, cmd[4]);
			break;
		case DATA_MODE_3:
			temp_value = mode_value[DATA_MODE_3];
			cmd[3] = (u8)(*(fts_data->pdata->touch_range_array + temp_value - 1));
			FTS_INFO("DATA_MODE_3, value=%d, cmd=%d", temp_value, cmd[3]);
			break;
		case DATA_MODE_4:
			temp_value = mode_value[DATA_MODE_4];
			cmd[5] = (u8)(*(fts_data->pdata->touch_range_array + temp_value - 1));
			FTS_INFO("DATA_MODE_4, value=%d, cmd=%d", temp_value, cmd[5]);
			break;
		case DATA_MODE_5:
			temp_value = mode_value[DATA_MODE_5];
			cmd[6] = (u8)(*(fts_data->pdata->touch_range_array + temp_value - 1));
			FTS_INFO("DATA_MODE_5, value=%d, cmd=%d", temp_value, cmd[6]);
			break;
		case DATA_MODE_6:
			temp_value = mode_value[DATA_MODE_0];
			cmd[1] = (u8)(temp_value);
			temp_value = mode_value[DATA_MODE_1];
			cmd[2] = (u8)(temp_value ? 30 : 3);
			if (mode_update_flag & (1 << DATA_MODE_6)) {
				temp_value = mode_value[DATA_MODE_6];
				cmd[3] = (u8)(*(fts_data->pdata->touch_expert_array + (temp_value - 1) * 4));
				cmd[4] = (u8)(*(fts_data->pdata->touch_expert_array + (temp_value - 1) * 4 + 1));
				cmd[5] = (u8)(*(fts_data->pdata->touch_expert_array + (temp_value - 1) * 4 + 2));
				cmd[6] = (u8)(*(fts_data->pdata->touch_expert_array + (temp_value - 1) * 4 + 3));
				FTS_INFO("DATA_MODE_6, value=%d, cmd=%*ph", temp_value, 7, cmd);
			}
			break;
		default:
			FTS_ERROR("not support mode, mode(%d)", i);
			break;
		}
	}
	ret = fts_write(cmd, sizeof(cmd));
	if (ret < 0) {
		FTS_ERROR("write game mode parameter failed\n");
	} else {
		FTS_INFO("update game mode cmd: %02X,%02X,%02X,%02X,%02X,%02X,%02X",
				cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);
	}
	// addr:0x8d, mode:7
	if (mode_update_flag & (1 << DATA_MODE_7)) {
		temp_value = mode_value[DATA_MODE_7];
		ret = fts_write_reg(FTS_REG_EDGE_FILTER_LEVEL, temp_value);
		if (ret < 0) {
			FTS_ERROR("write DATA_MODE_7 mode:%d reg failed", mode);
		} else {
			FTS_INFO("write DATA_MODE_7 value: %d, addr:0x%02X",
				temp_value, FTS_REG_EDGE_FILTER_LEVEL);
		}
	}
	// addr:0x8c mode 8
	if (mode_update_flag & (1 << DATA_MODE_8)) {
		temp_value = mode_value[DATA_MODE_8];
		if (PANEL_ORIENTATION_DEGREE_90 == temp_value) {
			if (!!mode_value[DATA_MODE_0])
				temp_value = GAME_ORIENTATION_90;
			else
				temp_value = NORMAL_ORIENTATION_90;
		} else if (PANEL_ORIENTATION_DEGREE_270 == temp_value) {
			if (!!mode_value[DATA_MODE_0]) 
				temp_value = GAME_ORIENTATION_270;
			else
				temp_value = NORMAL_ORIENTATION_270;
		} else
			temp_value = ORIENTATION_0_OR_180;
		ret = fts_write_reg(FTS_REG_ORIENTATION, temp_value);
		if (ret < 0) {
			FTS_ERROR("write DATA_MODE_8 mode:%d reg failed", mode);
		}
		FTS_INFO("DATA_MODE_8 change to %d(addr:%02x), gamemode:%d", 
						temp_value, FTS_REG_ORIENTATION, !!mode_value[DATA_MODE_0]);
	}

	mutex_unlock(&fts_data->cmd_update_mutex);
	pm_relax(fts_data->dev);
}

int fts_enable_touch_raw(int en)
{
	int ret = 0, retry = 3;
	u8 read_value;
	u8 write_value = en ? TYPE_THP : TYPE_TIC;

	FTS_DEBUG("type: %s", en ? "Enable" : "Disable");

	while (retry--) {
		ret = fts_write_reg(SET_THP_MODE_EN_TYPE, write_value);
		if (ret < 0) {
			FTS_ERROR("enable touch raw failed, en:%d\n", en);
			msleep(1);
			continue;
		}

		ret = fts_read_reg(SET_THP_MODE_EN_TYPE, &read_value);
		if (ret < 0) {
			FTS_ERROR("read failed, remain retry:%d\n", retry);
			msleep(1);
		} else if (write_value != read_value) {
			FTS_ERROR("write:0x%x, read:0x%x, remain retry:%d", write_value, read_value, retry);
			ret = -EINVAL;
			msleep(1);
		} else {
			break;
		}
	}

	if (ret >= 0)
		fts_data->enable_touch_raw = en ? true : false;

	return ret;
}

/*some type has not been achieve */
static int fts_touch_doze_analysis(int value)
{
	int result = 0;
	//struct force_update_flag force_burn;

	if (fts_data->suspended) {
		FTS_INFO("%s touch in suspend, return\n", __func__);
		return result;
	}
	switch(value) {
		case POWER_RESET:
			break;
		case RELOAD_FW:
			queue_work(fts_data->ts_workqueue, &fts_data->fwupg_work);
			break;
		case ENABLE_IRQ:
			fts_irq_enable();
			break;
		case DISABLE_IRQ:
			fts_irq_disable();
			break;
		case REGISTER_IRQ:
			fts_irq_disable();
			free_irq(fts_data->irq, fts_data);
			if(!request_threaded_irq(fts_data->irq, NULL, fts_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, FTS_DRIVER_NAME, fts_data)) {
				FTS_INFO("%s Request irq successfully\n", __func__);
				fts_irq_enable();
			}
			break;
		case IRQ_PIN_LEVEL:
			result = gpio_get_value(fts_data->pdata->irq_gpio) == 0 ? 0 : 1;
			break;
		default:
			FTS_INFO("%s don't support touch doze analysis\n", __func__);
			break;
	}

	return result;
}

int fts_touch_log_level_control(int value)
{
	fts_debug_log_level = value;
	return 0;
}

static void fts_charger_status_recovery(struct fts_ts_data *ts_data)
{
	if (ts_data->charger_status) {
		FTS_DEBUG("%s, recover charger mode to usb_in\n", __func__);
		fts_write_reg(FTS_REG_CHARGER_MODE_EN, true);
	} else {
		FTS_DEBUG("%s, recover charger mode to usb_out\n", __func__);
		fts_write_reg(FTS_REG_CHARGER_MODE_EN, false);
	}
}

static void fts_game_idle_high_refresh_recovery(struct fts_ts_data *ts_data)
{
	if (driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_0)) {
		FTS_DEBUG("%s, gamemode enabled, recover game_idle_high_refresh\n", __func__);
		fts_write_reg(0x8E, true);
	}
}
static void fts_report_rate_recovery(struct fts_ts_data *ts_data)
{
	if (ts_data->report_rate_status == 120) {
		FTS_DEBUG("%s, recover report_rate to %d\n", __func__, ts_data->report_rate_status);
		/*recover Report_Rate to 120HZ*/
		if (fts_write_reg(0x92, 1) < 0) {
			FTS_ERROR("%s, Failed to switch Report_Rate to 120HZ",  __func__);
		}
	}
}
static void fts_fod_status_recovery(struct fts_ts_data *ts_data)
{
	ts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	if (ts_data->fod_status != -1 && ts_data->fod_status != 0) {
		FTS_DEBUG("%s, fod_status = %d, enable CF register\n",  __func__, ts_data->fod_status);
		if (ts_data->suspended) {
			FTS_DEBUG("%s, tp is in suspend mode, write 0xD0 to 1", __func__);
			fts_gesture_reg_write(0x01, true);
		}
		fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, true);
	}
}

static void fts_game_mode_recovery(struct fts_ts_data *ts_data)
{
	/*use default value*/
	// touch_mode[DATA_MODE_0][GET_CUR_VALUE] = touch_mode[DATA_MODE_0][GET_DEF_VALUE];
	// touch_mode[DATA_MODE_8][GET_CUR_VALUE] = touch_mode[DATA_MODE_8][GET_DEF_VALUE];
	// touch_mode[DATA_MODE_7][GET_CUR_VALUE] = touch_mode[DATA_MODE_7][GET_DEF_VALUE];
	FTS_ERROR("this is null !!!!!");
}

static void fts_init_touchmode_data(struct fts_ts_data *ts_data)
{
	// struct fts_ts_platform_data *pdata = ts_data->pdata;
	/* Touch Game Mode Switch */
/*	touch_mode[DATA_MODE_0][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_0][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_0][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_0][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_0][GET_CUR_VALUE] = 0;*/
	/* Acitve Mode */
/*	touch_mode[DATA_MODE_1][GET_MAX_VALUE] = 1;
	touch_mode[DATA_MODE_1][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_1][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_1][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_1][GET_CUR_VALUE] = 0;*/
	/* UP_THRESHOLD */
/*	touch_mode[DATA_MODE_2][GET_MAX_VALUE] = 5;
	touch_mode[DATA_MODE_2][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_2][GET_DEF_VALUE] = pdata->touch_def_array[0];
	touch_mode[DATA_MODE_2][SET_CUR_VALUE] = pdata->touch_def_array[0];
	touch_mode[DATA_MODE_2][GET_CUR_VALUE] = pdata->touch_def_array[0];*/
	/*  Tolerance */
/*	touch_mode[DATA_MODE_3][GET_MAX_VALUE] = 5;
	touch_mode[DATA_MODE_3][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_3][GET_DEF_VALUE] = pdata->touch_def_array[1];
	touch_mode[DATA_MODE_3][SET_CUR_VALUE] = pdata->touch_def_array[1];
	touch_mode[DATA_MODE_3][GET_CUR_VALUE] = pdata->touch_def_array[1];*/
	/*  Aim_Sensitivity */
/*	touch_mode[DATA_MODE_4][GET_MAX_VALUE] = 5;
	touch_mode[DATA_MODE_4][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_4][GET_DEF_VALUE] = pdata->touch_def_array[2];
	touch_mode[DATA_MODE_4][SET_CUR_VALUE] = pdata->touch_def_array[2];
	touch_mode[DATA_MODE_4][GET_CUR_VALUE] = pdata->touch_def_array[2];*/
	/*  Tap_Stability */
/*	touch_mode[DATA_MODE_5][GET_MAX_VALUE] = 5;
	touch_mode[DATA_MODE_5][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_5][GET_DEF_VALUE] = pdata->touch_def_array[3];
	touch_mode[DATA_MODE_5][SET_CUR_VALUE] = pdata->touch_def_array[3];
	touch_mode[DATA_MODE_5][GET_CUR_VALUE] = pdata->touch_def_array[3];*/
	/* panel orientation*/
/*	touch_mode[DATA_MODE_8][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_8][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_8][GET_DEF_VALUE] = 0;
	touch_mode[DATA_MODE_8][SET_CUR_VALUE] = 0;
	touch_mode[DATA_MODE_8][GET_CUR_VALUE] = 0;*/
	/* Expert_Mode*/
/*	touch_mode[DATA_MODE_6][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_6][GET_MIN_VALUE] = 1;
	touch_mode[DATA_MODE_6][GET_DEF_VALUE] = 1;
	touch_mode[DATA_MODE_6][SET_CUR_VALUE] = 1;
	touch_mode[DATA_MODE_6][GET_CUR_VALUE] = 1;*/
	/* edge filter area*/
/*	touch_mode[DATA_MODE_7][GET_MAX_VALUE] = 3;
	touch_mode[DATA_MODE_7][GET_MIN_VALUE] = 0;
	touch_mode[DATA_MODE_7][GET_DEF_VALUE] = 2;
	touch_mode[DATA_MODE_7][SET_CUR_VALUE] = 2;
	touch_mode[DATA_MODE_7][GET_CUR_VALUE] = 2;*/
	FTS_INFO("touchfeature value init done");
}

static u8 fts_panel_vendor_read(void)
{
	u8 info = 0;
	if (!fts_data) {
		FTS_ERROR("fts data is null");
		return 0;
	}

	if (fts_data->lockdown_info[0] == 0x71 || fts_data->lockdown_info[0] == 0x46)
		info = 0x46;

	FTS_INFO("read info is %c", info);
	return info;
}

static u8 fts_panel_color_read(void)
{
	u8 info = 0;
	if (!fts_data) {
		FTS_ERROR("fts data is null");
		return 0;
	}

	info = fts_data->lockdown_info[2];
	FTS_INFO("read info is %c", info);
	return info;
}

static u8 fts_panel_display_read(void)
{
	u8 info = 0;
	if (!fts_data) {
		FTS_ERROR("fts data is null");
		return 0;
	}

	info = fts_data->lockdown_info[1];
	FTS_INFO("read info is %c", info);
	return info;
}

static char fts_touch_vendor_read(void)
{
	FTS_INFO("return info is %c", '3');
	return '3';
}

static void tpdbg_shutdown(struct fts_ts_data *ts_data, bool enable)
{
	if (enable)
		fts_data->poweroff_on_sleep = true;

	schedule_resume_suspend_work_common(TOUCH_ID, !enable);
}

static void tpdbg_suspend(struct fts_ts_data *ts_data, bool enable)
{
	schedule_resume_suspend_work_common(TOUCH_ID, !enable);
}

static int tpdbg_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t tpdbg_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	const char *str = "cmd support as below:\n"
		"\n echo \"irq-disable\" or \"irq-enable\" to ctrl irq\n"
		"\n echo \"tp-suspend-en\" or \"tp-suspend-off\" to ctrl panel in or off suspend status\n"
		"\n echo \"tp-sd-en\" or \"tp-sd-off\" to ctrl panel in or off sleep status\n";
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

static ssize_t tpdbg_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	struct fts_ts_data *ts_data = file->private_data;
	char *cmd = kzalloc(size, GFP_KERNEL);
	int ret = size;
	if (!cmd)
		return -ENOMEM;
	if (copy_from_user(cmd, buf, size)) {
		ret = -EFAULT;
		goto out;
	}
	if (!strncmp(cmd, "irq-disable", 11))
		fts_irq_disable();
	else if (!strncmp(cmd, "irq-enable", 10))
		fts_irq_enable();
	else if (!strncmp(cmd, "tp-suspend-en", 13))
		tpdbg_suspend(ts_data, true);
	else if (!strncmp(cmd, "tp-suspend-off", 14))
		tpdbg_suspend(ts_data, false);
	else if (!strncmp(cmd, "tp-sd-en", 8))
		tpdbg_shutdown(ts_data, true);
	else if (!strncmp(cmd, "tp-sd-off", 9))
		tpdbg_shutdown(ts_data, false);
out:
	kfree(cmd);
	return ret;
}

static int tpdbg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

#ifdef TPDEBUG_IN_D
static const struct file_operations tpdbg_operations = {
	.owner = THIS_MODULE,
	.open = tpdbg_open,
	.read = tpdbg_read,
	.write = tpdbg_write,
	.release = tpdbg_release,
};
#else
static const struct proc_ops tpdbg_operations = {
	/*.owner = THIS_MODULE,*/
	.proc_open = tpdbg_open,
	.proc_read = tpdbg_read,
	.proc_write = tpdbg_write,
	.proc_release = tpdbg_release,
};

int fts_proc_init(void)
{
	struct proc_dir_entry *entry;
	touch_debug = proc_mkdir_data("tp_debug", 0777, NULL, NULL);
	if (IS_ERR_OR_NULL(touch_debug))
		return -ENOMEM;
	entry = proc_create("switch_state", 0644, touch_debug, &tpdbg_operations);
	if (IS_ERR_OR_NULL(entry)) {
		FTS_ERROR("create node fail");
		remove_proc_entry("tp_debug", NULL);
		return -ENOMEM;
	}
	return 0;
}
void fts_proc_remove(void)
{
	remove_proc_entry("switch_state", touch_debug);
	remove_proc_entry("tp_debug", NULL);
}
#endif
/*add hardware_param/hardware_operation interface*/
int fts_get_x_resolution(void)
{
	return FTS_X_MAX_DISPLAY_DEFAULT;
}

int fts_get_y_resolution(void)
{
	return FTS_Y_MAX_DISPLAY_DEFAULT;
}

int fts_get_rx_num(void)
{
	return FOCALTECH_RX_NUM;
}

int fts_get_tx_num(void)
{
	return FOCALTECH_TX_NUM;
}

u8 fts_get_super_resolution_factor(void) 
{
	return (u8)SUPER_RESOLUTION_FACOTR;
}

int fts_ic_fw_version(char *fw_version_buf)
{
	int ret = 0;
	u8 fwver = 0;
	mutex_lock(&fts_data->input_dev->mutex);
#if FTS_ESDCHECK_EN
	fts_esdcheck_proc_busy(1);
#endif
	ret = fts_read_reg(FTS_REG_FW_VER, &fwver);
#if FTS_ESDCHECK_EN
	fts_esdcheck_proc_busy(0);
#endif
	mutex_unlock(&fts_data->input_dev->mutex);
	if ((ret < 0) || (fwver == 0xFF) || (fwver == 0x00))
	{
		FTS_INFO("get tp fw version fail!\n");
		return -1;
	} else {
		return snprintf(fw_version_buf, 64, "%02x", fwver);
	}
}

int fts_ic_self_test(char *type, int *result)
{
	int retval = 0;//0 invalid; 1 fail; 2 pass

	ic_in_selftest = 1;

	if ((!strncmp("short", type, 5) || !strncmp("open", type, 4))) {
		if (!strncmp("short", type, 5)) {
			retval = fts_ftest->func->short_test();
		} else {
			retval = fts_ftest->func->open_test();
		}
	} else if (!strncmp("i2c", type, 3) || !strncmp("spi", type, 3)) {
		retval = fts_ftest->func->spi_test();
	}

	ic_in_selftest = 0;

	*result = retval;

	if (retval == 2) {
		FTS_INFO("tp selftest pass");
	} else if (retval == 1) {
		FTS_ERROR("tp selftest fail");
	} else if (retval == 0) {
		FTS_ERROR("tp selftest invalid");
	} else {
		FTS_ERROR("tp selftest error");
	}

	return 0;
}

int fts_resume_suspend(bool resume, u8 gesture_type)
{
	if(resume)
		fts_ts_resume(fts_data->dev);
	else
		fts_ts_suspend(fts_data->dev);
	return 0;
}

/*to do*/
int fts_get_system_info(char *buf)
{
	return 0;
}

int fts_get_ito_raw(char *data_dump_buf)
{
	return 0;
}

int fts_get_mutual_raw(char *data_dump_buf)
{
	return 0;
}

int fts_get_mutual_raw_lp(char *data_dump_buf)
{
	return 0;
}

int fts_get_ss_raw(char *data_dump_buf)
{
	return 0;
}

int fts_get_ss_raw_lp(char *data_dump_buf)
{
	return 0;
}

int fts_get_mutual_cx_lp(char *data_dump_buf)
{
	return 0;
}

int fts_get_ss_ix(char *data_dump_buf)
{
	return 0;
}

static inline void fts_read_data_swap(char* data, int len)
{
    int i = 0;
    char c = 0;
    char *buf = (char *)data;
    /*Byte order transposition*/
    while (i < len) {
        c = buf[i];
        buf[i] = buf[i + 1];
        buf[i + 1] = c;
        i = i + 2;
    }
}

void converhex(u8 hex[4], int data)
{
    int value = data;
    hex[0] = (value & 0xFF);
    hex[1] = ((value >> 8) & 0xFF);
    hex[2] = ((value >> 16) & 0xFF);
    hex[3] = ((value >> 24) & 0xFF);
}

static u32 buf_len_temp[] = {2, 2, 2, 2, 2, 2, 1 ,1, 1, 1, 2, 2, 2, 2, 8, 1, 2, 2, 2};
static inline int fts_poll_data_convert(char *tpframe)
{
	u32 buf_size = 0;
	u32 temp_len = 0;
	u8 i = 0;
	u32 j = 0;
	char temp = 0;
	char *touch_buf = tpframe;
	buf_size = sizeof(buf_len_temp)/sizeof(u32);

	for (i = 0; i < buf_size; i++){
		if ((buf_len_temp[i] == 2) || (buf_len_temp[i] > 4)) {
			temp_len = buf_len_temp[i];
			while (temp_len) {
				temp = touch_buf[j];
				touch_buf[j] = touch_buf[j + 1];
				touch_buf[j +1] = temp;
				j = j + 2;
				temp_len = temp_len - 2;
			}
		} else if (buf_len_temp[i] == 4) {
			temp = touch_buf[j];
			touch_buf[j] = touch_buf[j + 3];
			touch_buf[j + 3] = temp;
			temp = touch_buf[j + 1];
			touch_buf[j + 1] = touch_buf[j + 2];
			touch_buf[j + 2] = temp;
			j = j + 4;
		} else if (buf_len_temp[i] == 1) {
		   j = j + 1;
		} else {
			FTS_INFO("buffer len error,buf_len_temp[%d] = %d!!!",i,buf_len_temp[i]);
			return -EIO;
		}
	}

	return 0;
}

int fts_thp_ic_write_interfaces_reg(u8 addr, u8 value)
{
    int ret = 0;
    u8 val = 0;

    switch(addr){
      case SET_GESTURE_EN_TYPE:
        ret = fts_write_reg(addr, value);
        break;
      case SET_DOUBLE_AND_CHLICK_GESTURE_EN_TYPE:
        ret = fts_read_reg(addr, &val);
        if(ret < 0){
          FTS_ERROR("read %d fail!",addr);
        }
        val |= (value & 0x90);
        ret = fts_write_reg(addr, val);
        break;
      case SET_IC_WORK_MODE_TYPE:
        if (value == 2)
          value = 0;
        ret = fts_write_reg(addr, value);
        break;
      case SET_FOD_EN_TYPE:
        ret = fts_write_reg(addr, value);
        break;
      case SET_GAME_MODE_EN_TYPE:
        ret = fts_write_reg(addr, value);
        break;
      case SET_CHARGING_STATUS_EN_TYPE:
        ret = fts_write_reg(addr, value);
        break;
      default:
        FTS_ERROR("not define cmd!");
        return -1;
        break;
    }

    if (ret < 0)
      FTS_ERROR("set mode: 0x%x failed!", addr);
    return ret;
}

#if 0
int fts_thp_ic_write_interfaces(u8 addr, s32* value, int value_len)
{
    u8 hex[4];
    u8 writebuf[512];
    int input = value[0];
    writebuf[0] = addr;
    if (htc_ic_mode == IC_MODE_44) {
        int i = 0;
        int index = 0;
        unsigned int* input_data = value;
        /*analy_open_data(value,input_data);*/
        /*input_data = value;*/
        for (i = 0; i < value_len; i++) {
            FTS_INFO("open_data:input_data[index:%d]:%d\n", i, input_data[i]);
            converhex(hex, input_data[i]);
            writebuf[index] = hex[0];
            FTS_INFO("open_data:writebuf[index:%d]:%x\n", index, writebuf[index]);
            ++index;
        }
        return fts_write(writebuf, value_len);
    } else {
        /*input = integer_conver(value);*/
        converhex(hex, input);
        if (input <= 255) {
            writebuf[1] = 0x00;
            writebuf[2] = hex[0];
            FTS_INFO("mode:%d, writebuf[0]:%x, writebuf[1]:%x, writebuf[2]:%x\n", htc_ic_mode, writebuf[0], writebuf[1], writebuf[2]);
        } else {
            writebuf[1] = hex[1];
            writebuf[2] = hex[0];
            FTS_INFO("mode:%d, writebuf[0]:%x, writebuf[1]:%x, writebuf[2]:%x\n", htc_ic_mode, writebuf[0], writebuf[1], writebuf[2]);
        }
        return fts_write(writebuf, 3);
    }
    return -1;
}

int fts_thp_ic_read_interfaces(u8 addr, u8* value, int value_len)
{
    int ret;
    ret = fts_read(&addr, sizeof(u8), value, value_len);
    if (ret < 0) {
        FTS_ERROR("thp ic read is failed!!\n");
        return -1;
    }
    FTS_INFO("BIG_SMALL_CHANGE before  mode:%d, addr:%x, readbuf[0]:%x, readbuf[1]:%x\n", htc_ic_mode, addr, value[0], value[1]);
#ifdef BIG_SMALL_CHANGE
    if (htc_ic_mode == IC_MODE_49)
        fts_poll_data_convert((char* )value);
        //fts_read_data_swap(value, value_len);
    else
        fts_read_data_swap(value, value_len);
#endif
    FTS_INFO("BIG_SMALL_CHANGE after  mode:%d, addr:%x, readbuf[0]:%x, readbuf[1]:%x\n", htc_ic_mode, addr, value[0], value[1]);
    return 0;
}
#endif

int fts_thp_ic_write_interfaces(u8 addr, s32* value, int value_len)
{
    u8 hex[4];
    u8 writebuf[512];
    int input = value[0];
    writebuf[0] = addr;
    if (htc_ic_mode == IC_MODE_44) {
        int i = 0;
        int index = 1;
        unsigned char *input_data=(char *)&value[1];
        writebuf[0] = value[0] & 0xFF;
        /*analy_open_data(value,input_data);*/
        for (i = 0; i < value_len; i++) {
            FTS_INFO("open_data before:input_data[index:%d]:%x\n", i, input_data[i]);
            //converhex(hex, input_data[i]);
            writebuf[index]=(input_data[i]) & 0xFF;
            FTS_INFO("open_data after:writebuf[index:%d]:%x\n", index, writebuf[index]);
            ++index;
        }
        for (i = 0; i < value_len+1; i++) {
            FTS_INFO("mode:3045, writebuf[i:%d]:%x", i, writebuf[i]);
        }
        return fts_write(writebuf, value_len + 1);
    } else {
        /*input = integer_conver(value);*/
        converhex(hex, input);
        /*mode for set_cur_value*/
        if((addr == SET_GESTURE_EN_TYPE) || (addr == SET_DOUBLE_AND_CHLICK_GESTURE_EN_TYPE) || (addr == SET_CHARGING_STATUS_EN_TYPE) \
                || (addr == SET_IC_WORK_MODE_TYPE) || (addr == SET_FOD_EN_TYPE) || (addr == SET_GAME_MODE_EN_TYPE)){
            return fts_thp_ic_write_interfaces_reg(addr, hex[0]);
        }
        /*mode for thp_ic_cmd*/
        if (input <= 255) {
            writebuf[1] = 0x00;
            writebuf[2] = hex[0];
            FTS_INFO("mode:%d, writebuf[0]:%x, writebuf[1]:%x, writebuf[2]:%x\n", htc_ic_mode, writebuf[0], writebuf[1], writebuf[2]);
        } else {
            writebuf[1] = hex[1];
            writebuf[2] = hex[0];
            FTS_INFO("mode:%d, writebuf[0]:%x, writebuf[1]:%x, writebuf[2]:%x\n", htc_ic_mode, writebuf[0], writebuf[1], writebuf[2]);
        }
        return fts_write(writebuf, 3);
    }
    return -1;
}

int fts_thp_ic_read_interfaces(u8 addr, u8* value, int value_len)
{
    int ret;
    ret = fts_read(&addr, sizeof(u8), value, value_len);
    if (ret < 0) {
        FTS_ERROR("thp ic read is failed!!\n");
        return -1;
    }
    FTS_INFO("BIG_SMALL_CHANGE before  mode:%d, addr:%x, readbuf[0]:%x, readbuf[1]:%x\n", htc_ic_mode, addr, value[0], value[1]);
#ifdef BIG_SMALL_CHANGE
    if (htc_ic_mode == IC_MODE_49)
        fts_poll_data_convert((char* )value);
        //fts_read_data_swap(value, value_len);
    else
        fts_read_data_swap(value, value_len);
#endif
    FTS_INFO("BIG_SMALL_CHANGE after  mode:%d, addr:%x, readbuf[0]:%x, readbuf[1]:%x\n", htc_ic_mode, addr, value[0], value[1]);
    return 0;
}

int fts_htc_ic_setModeValue(common_data_t *common_data)
{
        return 0;
}

int fts_htc_ic_getModeValue(common_data_t *common_data)
{
        return 0;
}

int fts_lockdown_info_read(u8 *lockdown_info_buf)
{
	int i = 0;
	int ret = 0;
	bool need_read_flash = true;

	if (!fts_data)
		return -1;

	for (i = 0; i < FTS_LOCKDOWN_INFO_SIZE; i++) {
		lockdown_info_buf[i] = fts_data->lockdown_info[i];
		if (lockdown_info_buf[i] != 0 && lockdown_info_buf[i] != 0xFF)
			need_read_flash = false;
	}

	if (need_read_flash) {
		FTS_INFO("read lockdown info from flash\n");
		ret = fts_get_lockdown_information(fts_data);
		if (ret != 0) {
			FTS_ERROR("get lockdown info error\n");
			return 0;
		}
		for (i = 0; i < FTS_LOCKDOWN_INFO_SIZE; i++) {
			lockdown_info_buf[i] = fts_data->lockdown_info[i];
		}
	}

	return 0;
}

#ifdef TOUCH_MULTI_PANEL_NOTIFIER_SUPPORT
/**
 * @brief Check and recover touch IC status
 *
 * This function checks if the touch firmware is valid, attempts recovery if not,
 * and restores gesture functionality after successful recovery.
 */
static void focal_ic_status_check(void)
{
	u8 reg_gesture_en = 0;
	u8 reg_gesture_ctrl = 0;
	int ret = 0;
	/* Check if touch IC is valid */
	ret = fts_wait_tp_to_valid();
	if (ret < 0) {
		FTS_ERROR("Touch firmware invalid, attempting reset...");
		/* First recovery attempt */
		fts_reset_proc(200);
		ret = fts_wait_tp_to_valid();
		if (ret < 0) {
			FTS_ERROR("Touch firmware still invalid after reset");
		}
		/* Recovery successful, restore gestures */
		FTS_INFO("Touch firmware recovered, restoring gestures...");
		fts_gesture_recovery(fts_data);
		return;
    } else {
		FTS_DEBUG("Touch firmware is valid");
	}
	/* Check if gesture is enabled */
	if (fts_data->gesture_support && fts_data->suspended) {
		fts_read_reg(FTS_REG_GESTURE_EN, &reg_gesture_en);
		fts_read_reg(FTS_GESTURE_CTRL, &reg_gesture_ctrl);
		if (reg_gesture_en == 0) {
			FTS_ERROR("need recover gesture, gesture_en=%d, gesture_ctrl=%d\n", reg_gesture_en, reg_gesture_ctrl);
			fts_gesture_recovery(fts_data);
		}
	}
}
static void fts_set_panel_notifier_status(enum suspend_state panel_status[])
{
	if (panel_status == NULL)
		return;

	switch (panel_status[0]) {
		case XIAOMI_TOUCH_RESUME:
			FTS_DEBUG("first panel is on\n");
			if (fts_data->suspended) {
				FTS_INFO("need check ic status\n");
				focal_ic_status_check();
			}
			break;
		case XIAOMI_TOUCH_SUSPEND:
			FTS_DEBUG("first panel is off\n");
			break;
		default:
			FTS_INFO("first panel status is unknown, first panel_status = %d\n", panel_status[0]);
			break;
	}
}
#endif

void fts_init_hardware_param(void)
{
	/* hardware_param */
	hardware_param.x_resolution = fts_get_x_resolution();
	hardware_param.y_resolution = fts_get_y_resolution();
	hardware_param.rx_num = fts_get_rx_num();
	hardware_param.tx_num = fts_get_tx_num();
	hardware_param.super_resolution_factor = fts_get_super_resolution_factor();
#ifdef TOUCH_DUMP_TIC_SUPPORT
	hardware_param.frame_data_page_size = 2;
#else  /* TOUCH_DUMP_TIC_SUPPORT */
	hardware_param.frame_data_page_size = 1;
#endif /* TOUCH_DUMP_TIC_SUPPORT */

	hardware_param.frame_data_buf_size = 10;
	hardware_param.raw_data_page_size = 5;
	hardware_param.raw_data_buf_size = 5;
	memset(hardware_param.config_file_name, 0, 64);
	memcpy(hardware_param.config_file_name, "pandora_focal_thp_config.ini", strlen("pandora_focal_thp_config.ini"));
	memset(hardware_param.driver_version, 0, 64);
	memcpy(hardware_param.driver_version, FOCAL_DRIVER_VERSION, strlen(FOCAL_DRIVER_VERSION));
	/* save lockdown type: u8 */
	/* init lockdown_info/fw_version in fw_upgrade_work for the right data*/
	fts_lockdown_info_read(hardware_param.lockdown_info);
	fts_ic_fw_version(hardware_param.fw_version);
}

void fts_init_xiaomi_touchfeature_v3(struct fts_ts_data *ts_data)
{
	mutex_init(&ts_data->cmd_update_mutex);
	/* hardware_param */
	fts_init_hardware_param();
	/* hardware_operation */
	memset(&hardware_operation, 0, sizeof(hardware_operation_t));
	hardware_operation.ic_self_test = fts_ic_self_test;
	hardware_operation.ic_data_collect = fts_ic_data_collect;
	hardware_operation.ic_get_lockdown_info = fts_lockdown_info_read;
	hardware_operation.ic_get_fw_version = NULL;

	hardware_operation.set_mode_value = fts_set_cur_value;
	hardware_operation.ic_switch_mode = fts_ic_switch_mode;
	hardware_operation.cmd_update_func = fts_game_mode_update;
	hardware_operation.set_mode_long_value = NULL;

	hardware_operation.palm_sensor_write = NULL;
	hardware_operation.enable_touch_raw = fts_enable_touch_raw;
	hardware_operation.panel_vendor_read = fts_panel_vendor_read;
	hardware_operation.panel_color_read = fts_panel_color_read;
	hardware_operation.panel_display_read = fts_panel_display_read;
	hardware_operation.touch_vendor_read = fts_touch_vendor_read;
	hardware_operation.get_touch_ic_buffer = NULL;
	hardware_operation.touch_doze_analysis = fts_touch_doze_analysis;
	hardware_operation.touch_log_level_control = NULL;
	hardware_operation.touch_log_level_control_v2 = fts_touch_log_level_control;
	hardware_operation.set_nfc_to_touch_event = NULL;
	hardware_operation.htc_ic_setModeValue = fts_htc_ic_setModeValue;
	hardware_operation.htc_ic_getModeValue = fts_htc_ic_getModeValue;

	hardware_operation.ic_resume_suspend = fts_resume_suspend;
	hardware_operation.ic_set_charge_state = fts_set_charge_state;
#ifdef TOUCH_FOD_SUPPORT
	hardware_operation.xiaomi_touch_fod_test = fts_xiaomi_touch_fod_test;
#endif
#ifdef TOUCH_MULTI_PANEL_NOTIFIER_SUPPORT
	hardware_operation.set_panel_notifier_status = fts_set_panel_notifier_status;
#endif

#ifdef TOUCH_DUMP_TIC_SUPPORT
	fts_data->dump_type = DUMP_OFF;
#endif /* TOUCH_DUMP_TIC_SUPPORT */
	INIT_DELAYED_WORK(&fts_data->thp_signal_work, fts_thp_signal_work);

	/* init struct touchmode for reg setting */
	fts_init_touchmode_data(ts_data);
	register_touch_panel_common(ts_data->dev, TOUCH_ID, &hardware_param, &hardware_operation);
	xiaomi_register_panel_notifier_common(fts_data->dev, TOUCH_ID);
#ifdef TOUCH_GESTURE_ALWAYSON_SUPPORT
#ifndef CONFIG_TOUCH_FACTORY_BUILD
	set_touch_mode(DATA_MODE_14, 1);
#endif
#endif
}

#endif

static int fts_ts_probe_entry(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int pdata_size = sizeof(struct fts_ts_platform_data);

    FTS_FUNC_ENTER();
    FTS_INFO("%s", FTS_DRIVER_VERSION);
    ts_data->pdata = kzalloc(pdata_size, GFP_KERNEL);
    if (!ts_data->pdata) {
        FTS_ERROR("allocate memory for platform_data fail");
        return -ENOMEM;
    }

    if (ts_data->dev->of_node) {
        ret = fts_parse_dt(ts_data->dev, ts_data->pdata);
        if (ret) {
            FTS_ERROR("device-tree parse fail");
        }
    } else {
        if (ts_data->dev->platform_data) {
            memcpy(ts_data->pdata, ts_data->dev->platform_data, pdata_size);
        } else {
            FTS_ERROR("platform_data is null");
            return -ENODEV;
        }
    }

    ts_data->ts_workqueue = create_singlethread_workqueue("fts_wq");
    if (!ts_data->ts_workqueue) {
        FTS_ERROR("create fts workqueue fail");
    }

    spin_lock_init(&ts_data->irq_lock);
    mutex_init(&ts_data->report_mutex);
    mutex_init(&ts_data->bus_lock);
    init_waitqueue_head(&ts_data->ts_waitqueue);

#ifdef FTS_TOUCHSCREEN_FOD
	mutex_init(&ts_data->fod_mutex);
#endif
    ts_data->doubletap_status = 0;
    ts_data->aod_status = 0;
    ts_data->sensor_tap_status = 0;
    ts_data->report_rate_status = 240;
    /* Init communication interface */
    ret = fts_bus_init(ts_data);
    if (ret) {
        FTS_ERROR("bus initialize fail");
        goto err_bus_init;
    }

    ret = fts_input_init(ts_data);
    if (ret) {
        FTS_ERROR("input initialize fail");
        goto err_input_init;
    }

    ret = fts_buffer_init(ts_data);
    if (ret) {
        FTS_ERROR("buffer init fail");
        goto err_buffer_init;
    }

    ret = fts_gpio_configure(ts_data);
    if (ret) {
        FTS_ERROR("[DIS-TF-TOUCH] configure the gpios fail");
        goto err_gpio_config;
    }

#if FTS_POWER_SOURCE_CUST_EN
    ret = fts_power_source_init(ts_data);
    if (ret) {
        FTS_ERROR("fail to get power(regulator)");
        goto err_power_init;
    }
#endif

#if (!FTS_CHIP_IDC)
    /*fts_reset_proc(200);*/
    msleep(1);
    ret = gpio_direction_output(fts_data->pdata->reset_gpio, 1);
    if (ret) {
        FTS_ERROR("set gpio reset to high failed");
    }
    msleep(100);
#ifdef FTS_TOUCHSCREEN_FOD
    if (fts_data->fod_status != -1) {
        FTS_INFO("fod_status = %d\n", fts_data->fod_status);
        fts_fod_recovery();
    }
#endif
#endif

    ret = fts_get_ic_information(ts_data);
    if (ret) {
        FTS_ERROR("not focal IC, unregister driver");
        goto err_irq_req;
    }

    ret = fts_create_apk_debug_channel(ts_data);
    if (ret) {
        FTS_ERROR("create apk debug node fail");
    }

    ret = fts_create_proc(ts_data);
    if (ret)
	FTS_ERROR("create proc node fail");

    ret = fts_create_sysfs(ts_data);
    if (ret) {
        FTS_ERROR("create sysfs node fail");
    }

#ifdef TPDEBUG_IN_D
    ts_data->tpdbg_dentry = debugfs_create_dir("tp_debug_1", NULL);
    if (IS_ERR_OR_NULL(ts_data->tpdbg_dentry))
		FTS_ERROR("create tp_debug_1 dir fail");

    if (IS_ERR_OR_NULL(debugfs_create_file("switch_state", 0660,
			ts_data->tpdbg_dentry, ts_data, &tpdbg_operations)))
		FTS_ERROR("create switch_state fail");
#else
    ret = fts_proc_init();
    if (ret)
	FTS_ERROR("create debug proc failed");
#endif

    ret = fts_point_report_check_init(ts_data);
    if (ret) {
        FTS_ERROR("init point report check fail");
    }

    ret = fts_ex_mode_init(ts_data);
    if (ret) {
        FTS_ERROR("init glove/cover/charger fail");
    }

    ret = fts_gesture_init(ts_data);
    if (ret) {
        FTS_ERROR("init gesture fail");
    }

#ifdef TOUCH_FOD_SUPPORT
#ifdef CONFIG_TOUCH_FACTORY_BUILD
	set_touch_mode(DATA_MODE_10, 1);
	ts_data->fod_status = 1;
#else
	set_touch_mode(DATA_MODE_10, 0);
	ts_data->fod_status = -1;
#endif
#endif

#if FTS_TEST_EN
    ret = fts_test_init(ts_data);
    if (ret) {
        FTS_ERROR("init host test fail");
    }
#endif

    ret = fts_esdcheck_init(ts_data);
    if (ret) {
        FTS_ERROR("init esd check fail");
    }

    ret = fts_irq_registration(ts_data);
    if (ret) {
        FTS_ERROR("request irq failed");
        goto err_irq_req;
    }

    ret = fts_fwupg_init(ts_data);
    if (ret) {
        FTS_ERROR("init fw upgrade fail");
    }

if (ts_data->fts_tp_class == NULL) {
#ifdef FTS_XIAOMI_TOUCHFEATURE
	ts_data->fts_tp_class = get_xiaomi_touch_class_common();
#else
	ts_data->fts_tp_class = class_create(THIS_MODULE, "touch");
#endif
}

#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
	device_init_wakeup(ts_data->dev, 1);
	init_completion(&ts_data->pm_completion);
	ts_data->pm_suspend = false;
#endif

	ts_data->charger_status = -1;

	/*fts_init_xiaomi_touchfeature_v3 in fw_upgrade_work*/
	/*fts_init_xiaomi_touchfeature_v3(ts_data);
	fts_enable_touch_raw(1);*/

	FTS_FUNC_EXIT();
	return 0;

err_irq_req:
#if FTS_POWER_SOURCE_CUST_EN
err_power_init:
	fts_power_source_exit(ts_data);
#endif
	if (gpio_is_valid(ts_data->pdata->reset_gpio))
		gpio_free(ts_data->pdata->reset_gpio);
	if (gpio_is_valid(ts_data->pdata->irq_gpio))
		gpio_free(ts_data->pdata->irq_gpio);
err_gpio_config:
	kfree_safe(ts_data->touch_buf);
err_buffer_init:
#if 1
	unregister_xiaomi_input_dev(TOUCH_ID);
#else
	input_unregister_device(ts_data->input_dev);
#endif
#if FTS_PEN_EN
	input_unregister_device(ts_data->pen_dev);
#endif
err_input_init:
	if (ts_data->ts_workqueue)
		destroy_workqueue(ts_data->ts_workqueue);
err_bus_init:
	kfree_safe(ts_data->bus_tx_buf);
	kfree_safe(ts_data->bus_rx_buf);
	kfree_safe(ts_data->pdata);

	FTS_FUNC_EXIT();
	return ret;
}

static int fts_ts_remove_entry(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();

    fts_point_report_check_exit(ts_data);
    fts_release_apk_debug_channel(ts_data);
#ifdef TPDEBUG_IN_D
	debugfs_remove(ts_data->tpdbg_dentry);
#else
    fts_proc_remove();
#endif
    fts_remove_sysfs(ts_data);
    fts_ex_mode_exit(ts_data);

    fts_fwupg_exit(ts_data);

#if FTS_TEST_EN
    fts_test_exit(ts_data);
#endif

    fts_esdcheck_exit(ts_data);

    fts_gesture_exit(ts_data);

    free_irq(ts_data->irq, ts_data);

    fts_bus_exit(ts_data);

    input_unregister_device(ts_data->input_dev);
#if FTS_PEN_EN
    input_unregister_device(ts_data->pen_dev);
#endif

    if (ts_data->ts_workqueue)
        destroy_workqueue(ts_data->ts_workqueue);

    if (gpio_is_valid(ts_data->pdata->reset_gpio))
        gpio_free(ts_data->pdata->reset_gpio);

    if (gpio_is_valid(ts_data->pdata->irq_gpio))
        gpio_free(ts_data->pdata->irq_gpio);

#if FTS_POWER_SOURCE_CUST_EN
    fts_power_source_exit(ts_data);
#endif

    kfree_safe(ts_data->touch_buf);
    kfree_safe(ts_data->pdata);
    kfree_safe(ts_data);

    FTS_FUNC_EXIT();

    return 0;
}

static int fts_ts_suspend(struct device *dev)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;
	FTS_FUNC_ENTER();
	if (ts_data->suspended) {
		FTS_INFO("Already in suspend state");
		return 0;
	}
	if (ts_data->fw_loading) {
		FTS_INFO("fw upgrade in process, can't suspend");
		return 0;
	}
#ifdef FTS_XIAOMI_TOUCHFEATURE
	ts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	ts_data->aod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_11);
	ts_data->doubletap_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_14);
	FTS_INFO("fod_status:%d, doubletap_status:%d, aod_status:%d", ts_data->fod_status, ts_data->doubletap_status, ts_data->aod_status);
	if ((ts_data->fod_status != 0 && ts_data->fod_status != -1 && ts_data->fod_status != 100) || ts_data->doubletap_status || ts_data->aod_status) {
		ts_data->gesture_support = 1;
	}
	/*Fod_status is 0 when the fingerprint is closed after locking the screen*/
	if (!ts_data->doubletap_status && !ts_data->aod_status && !ts_data->fod_status) {
		ts_data->gesture_support = 0;
	}
#endif
#ifdef FTS_TOUCHSCREEN_FOD
	if ((ts_data->fod_status == -1 || ts_data->fod_status == 100)) {
		FTS_INFO("clear CF reg");
		 ret = fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, false);
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);
	}
	if ((ts_data->fod_status != -1 && ts_data->fod_status != 100 && ts_data->fod_status != 0)) {
		FTS_INFO("write CF reg");
		ret = fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, true);
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);
		fts_gesture_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, true);
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);
	}
#endif
	fts_esdcheck_suspend(ts_data);
#ifdef CONFIG_FACTORY_BUILD
	ts_data->poweroff_on_sleep = true;
#endif

	FTS_INFO("gesture_support:%d  poweroff_on_sleep:%d ", ts_data->gesture_support, ts_data->poweroff_on_sleep);
	if (ts_data->gesture_support && !ts_data->poweroff_on_sleep)
		fts_gesture_suspend(ts_data);
	else {
		fts_irq_disable();
		FTS_INFO("make TP enter into sleep mode");
		ret = fts_write_reg(FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP);
		ts_data->poweroff_on_sleep = true;
		if (ret < 0)
			FTS_ERROR("set TP to sleep mode fail, ret=%d", ret);
	}
#ifdef CONFIG_FACTORY_BUILD
	FTS_INFO("[%s] factory disable power", __func__);
	fts_power_source_ctrl(ts_data, DISABLE);
#endif
	ts_data->finger_in_fod = false;

	fts_release_all_finger();
	ts_data->suspended = true;
	/*notify thp for suspend state*/
	FTS_FUNC_EXIT();
	return 0;
}

static int fts_ts_resume(struct device *dev)
{
	/*u8 id = 0;*/
	/*int i = 0;*/
	struct fts_ts_data *ts_data = fts_data;
	FTS_FUNC_ENTER();
	if (!ts_data->suspended) {
		FTS_DEBUG("Already in awake state");
		return 0;
	}
	ts_data->suspended = false;
#ifndef CONFIG_FACTORY_BUILD
#ifdef FTS_TOUCHSCREEN_FOD
	FTS_INFO("%s finger_in_fod:%d fod_finger_skip:%d\n", __func__, ts_data->finger_in_fod, ts_data->fod_finger_skip);
	if (!ts_data->finger_in_fod && !ts_data->fod_finger_skip) {
		FTS_INFO("resume reset");
		fts_reset_proc(40);
		fts_recover_after_reset();
		fts_release_all_finger();
	}
#endif
#else
	FTS_INFO("[%s] factory enable power.", __func__);
	fts_power_source_ctrl(ts_data, ENABLE);
#endif
	if (!ts_data->ic_info.is_incell) {
#ifdef CONFIG_FACTORY_BUILD
		gpio_direction_output(fts_data->pdata->reset_gpio, 1);
		fts_reset_proc(40);
		fts_recover_after_reset();
		fts_release_all_finger();
#endif
	}
	fts_ex_mode_recovery(ts_data);
	fts_esdcheck_resume(ts_data);
	/* enable charger mode */
	if (ts_data->charger_status)
		fts_write_reg(FTS_REG_CHARGER_MODE_EN, true);
	if (ts_data->gesture_support && !ts_data->poweroff_on_sleep)
		fts_gesture_resume(ts_data);
	fts_irq_enable();
	ts_data->poweroff_on_sleep = false;
#ifdef FTS_TOUCHSCREEN_FOD
	fts_gesture_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, false);
#endif
	FTS_FUNC_EXIT();
	return 0;
}


#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
static int fts_pm_suspend(struct device *dev)
{
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    FTS_INFO("system enters into pm_suspend");
    ts_data->pm_suspend = true;
    reinit_completion(&ts_data->pm_completion);
    return 0;
}

static int fts_pm_resume(struct device *dev)
{
    struct fts_ts_data *ts_data = dev_get_drvdata(dev);

    FTS_INFO("system resumes from pm_suspend");
    ts_data->pm_suspend = false;
    complete(&ts_data->pm_completion);
    return 0;
}

static const struct dev_pm_ops fts_dev_pm_ops = {
    .suspend = fts_pm_suspend,
    .resume = fts_pm_resume,
};
#endif

/*****************************************************************************
* TP Driver
*****************************************************************************/
static int fts_ts_probe(struct spi_device *spi)
{
    int ret = 0;
    struct fts_ts_data *ts_data = NULL;

    FTS_INFO("Touch Screen(SPI BUS) driver probe...");

#if (FTS_CHIP_TYPE == _FT8719) || (FTS_CHIP_TYPE == _FT8615) || (FTS_CHIP_TYPE == _FT8006P) || (FTS_CHIP_TYPE == _FT7120)
    spi->mode = SPI_MODE_1;
#else
    spi->mode = SPI_MODE_0;
#endif
    /*spi->mode = SPI_MODE_0;*/
    spi->bits_per_word = 8;
    ret = spi_setup(spi);
    if (ret) {
        FTS_ERROR("[DIS-TF-TOUCH] spi setup fail");
        return ret;
    }

    /* malloc memory for global struct variable */
    ts_data = kzalloc(sizeof(*ts_data), GFP_KERNEL);
    if (!ts_data) {
        FTS_ERROR("allocate memory for fts_data fail");
        return -ENOMEM;
    }

    fts_data = ts_data;
    ts_data->spi = spi;
    ts_data->dev = &spi->dev;
    ts_data->log_level = 1;

    ts_data->bus_type = BUS_TYPE_SPI_V2;
    ts_data->poweroff_on_sleep = false;
    spi_set_drvdata(spi, ts_data);

#ifdef TOUCH_THP_SUPPORT
	INIT_DELAYED_WORK(&ts_data->thp_signal_work, fts_thp_signal_work);
#endif

    ret = fts_ts_probe_entry(ts_data);
    if (ret) {
        FTS_ERROR("[DIS-TF-TOUCH] Touch Screen(SPI BUS) driver probe fail");
        kfree_safe(ts_data);
        return ret;
    }

    FTS_INFO("Touch Screen(SPI BUS) driver probe successfully");
    return 0;
}

static void fts_ts_remove(struct spi_device *spi)
{
	struct fts_ts_data *ts_data = spi_get_drvdata(spi);

	if (!ts_data)
		return;

	xiaomi_unregister_panel_notifier_common(ts_data->dev, TOUCH_ID);
	unregister_touch_panel_common(TOUCH_ID);
    fts_ts_remove_entry(spi_get_drvdata(spi));
}

static const struct spi_device_id fts_ts_id[] = {
    {FTS_DRIVER_NAME, 0},
    {},
};
static const struct of_device_id fts_dt_match[] = {
    {.compatible = "focaltech,fts", },
    {},
};
MODULE_DEVICE_TABLE(of, fts_dt_match);

static struct spi_driver fts_ts_driver = {
    .probe = fts_ts_probe,
    .remove = fts_ts_remove,
    .driver = {
        .name = FTS_DRIVER_NAME,
        .owner = THIS_MODULE,
#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
        .pm = &fts_dev_pm_ops,
#endif
        .of_match_table = of_match_ptr(fts_dt_match),
    },
    .id_table = fts_ts_id,
};

#define PANEL_ID_DET1 (344+139)
#define PANEL_ID_DET2 (344+140)

static int __init fts_ts_init(void)
{
	int ret = 0;
	FTS_FUNC_ENTER();
	ret = spi_register_driver(&fts_ts_driver);
	if (ret != 0)
		FTS_ERROR("Focaltech touch screen driver init failed!");
	FTS_FUNC_EXIT();
	return ret;
}

static void __exit fts_ts_exit(void)
{
    spi_unregister_driver(&fts_ts_driver);
}

module_init(fts_ts_init);
/*late_initcall(fts_ts_init);*/
module_exit(fts_ts_exit);

MODULE_AUTHOR("FocalTech Driver Team");
MODULE_DESCRIPTION("FocalTech Touchscreen Driver");
MODULE_LICENSE("GPL v2");
