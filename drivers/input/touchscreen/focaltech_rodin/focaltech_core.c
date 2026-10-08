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
#include <linux/vseq.h>

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

/* _b571：镜像定义前移（recovery/game_mode_update 早期使用点需要在声明之前）*/
/* ==== FT5672 驱动本地模式镜像（blob *.bss touch_mode，840B）==== */
#define FTS_TOUCH_MODE_MAX		35
#define FTS_TOUCH_MODE_VALUE_NUM	6

static int fts_touch_mode[FTS_TOUCH_MODE_MAX][FTS_TOUCH_MODE_VALUE_NUM];

static void fts_update_touchmode_data(struct fts_ts_data *ts_data);

enum FTS_LOG_LEVEL fts_debug_log_level = FTS_LOG_INFO;

#define FOCALTECH_RX_NUM                    9
#define FOCALTECH_TX_NUM                    14
#define SUPER_RESOLUTION_FACOTR             100
/*struct device_node *gf_spi_dp;*/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
/* _b582-SLEEP：blob 侧 resume/suspend 是单函数（fts_resume_suspend，符号表无
 * fts_ts_* 条目）——两半被 LLVM 全内联进入口。树侧保留两半函数便于阅读，此处
 * 强制内联以复现 blob 的单函数调用面（否则 fts_resume_suspend 只剩 2 条调用，
 * 两半的 22 类被调全落 only-blob/only-tree）。 */
static __attribute__((always_inline)) inline int fts_ts_suspend(struct device *dev);
static __attribute__((always_inline)) inline int fts_ts_resume(struct device *dev);
static int fts_set_thermal_temp(int temp, bool force);
/* _b582-INTA：A-80④② —— fts_charger_on（focaltech_scp_tp.c:578）的临时原型已按
 * ② 的建议并入 focaltech_core.h 的 scp_tp 声明块（blob 0x2890 resume 半直调、
 * ex_mode 的 MODE_CHARGER 也走它），此处不再重复声明。 */

#ifdef FTS_XIAOMI_TOUCHFEATURE
int fts_ic_data_collect(char *buf, int *length);
int fts_read_and_report_foddata(struct fts_ts_data *data);
/* _b581：A-74 续行② —— blob 无 fts_game_mode_recovery 符号（donor 桩，仅打
 * "this is null !!!!!"），已随 fts_tp_state_recovery 接线收口删除。 */
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

/* _b582-INTD：fts_ic_self_test 宿主链（blob 0x14dec-0x150e4）所需的跨 TU 符号。
 * core.c 的 TU 不含 focaltech_test/focaltech_test.h（core.h:72 只 include
 * focaltech_test_ini.h），故按 A-80④② 的临时原型做法就地声明；签名与
 * focaltech_test.h:655（fts_test_main_init）、:667（enter_work_mode）、
 * :695（fts_test_main_exit）逐字一致。ic_self_test_flag 定义在
 * focaltech_scp_tp.c:518（= blob .bss+0x5b8，blob 唯一写点即本宿主链）。 */
extern int fts_test_main_init(void);
extern int fts_test_main_exit(void);
extern int enter_work_mode(void);
extern bool ic_self_test_flag;

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
            /* _b582-INTA：族对齐 D→I（blob 0x4b1a '\0016[FTS_TS_I][%s:%d]: TP Ready,Device ID:0x%02x'，
             * 引用点 fts_wait_tp_to_valid+0x1b4，门 cmp w8,#3; b.hs） */
            FTS_INFO("TP Ready,Device ID:0x%02x", idh);
            return 0;
        } else
            /* _b582-INTA：族对齐 E→D（blob 0x5f8c '\0016[FTS_TS_D][%s:%d]: TP Not Ready,ReadData:0x%02x,ret:%d'，
             * 引用点 fts_wait_tp_to_valid+0x2c，门 cmp w8,#4; b.hs） */
            FTS_DEBUG("TP Not Ready,ReadData:0x%02x,ret:%d", idh, ret);

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
	/* recover TP charger state 0x8B（blob 0x364：fts_charger_on(ts_data, charger_status!=0)） */
	fts_charger_status_recovery(ts_data);
	/* recover TP glove state 0xC0 / cover state 0xC1 */
	/* recover TP gesture state 0xD0 */
	fts_gesture_recovery(ts_data);
	/* _b581：A-74 续行② 接线 —— blob 0x37c..0x3ac：current_fps(+0xbf4)==135 时
	 * 重推 thp SET_REPORT_RATE_TYPE(0x13) 双字（135）。树侧 current_fps 同址同义。 */
	if (ts_data->current_fps == 135)
		fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE,
					    &ts_data->current_fps, 2);
	/* recover TP report_rate state 0x92 */
	fts_report_rate_recovery(ts_data);
#ifdef FTS_XIAOMI_TOUCHFEATURE
	/* _b571：blob 0x3b8-0x3fc 段——镜像 GET_CUR 复位为 GET_DEF + 刷新。 */
	{
		int __i;
		int __modes[] = { DATA_MODE_0, DATA_MODE_7, DATA_MODE_8 };
		for (__i = 0; __i < 3; __i++)
			fts_touch_mode[__modes[__i]][GET_CUR_VALUE] =
				fts_touch_mode[__modes[__i]][GET_DEF_VALUE];
	}
	fts_update_touchmode_data(ts_data);
	/* _b581：A-74 续行② 收口——blob 该函数无 fts_game_mode_recovery 调用（blob 亦无
	 * 此符号），原 donor 桩（仅打 "this is null !!!!!"）删除；game idle 0x8E 与 fod 0xCF
	 * 的重推分别由 fts_game_idle_high_refresh_recovery / fts_fod_status_recovery 承担
	 * （对应 blob 0x3e0-0x3fc 与 0x478-0x4c4）。 */
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

    /* _b582-INTA：打印形态按 blob —— fts_irq_disable(0xbe8) 只有**一条** I 级
     * "Enter"（str1.1+0x105e2 '\0016[FTS_TS_I][%s:%d]: Enter'，引用点 +0x70，
     * 门 cmp w8,#3 = FTS_LOG_INFO），**无** Exit 打印；树侧原 FTS_FUNC_ENTER +
     * FTS_FUNC_EXIT（V 族/门 ≥5，且多一条 _printk）⇒ 收成单条 FTS_INFO("Enter")。 */
    FTS_INFO("Enter");
    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (!fts_data->irq_disabled) {
        disable_irq_nosync(fts_data->irq);
        fts_data->irq_disabled = true;
    }

    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
}

void fts_irq_enable(void)
{
    unsigned long irqflags = 0;

    /* _b582-INTA：同 fts_irq_disable —— blob fts_irq_enable(0xc78) 只有一条 I 级
     * "Enter"（引用点 +0x6c，门 cmp w8,#3），无 Exit。 */
    FTS_INFO("Enter");
    spin_lock_irqsave(&fts_data->irq_lock, irqflags);

    if (fts_data->irq_disabled) {
        enable_irq(fts_data->irq);
        fts_data->irq_disabled = false;
    }

    spin_unlock_irqrestore(&fts_data->irq_lock, irqflags);
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
/* _b581：A-74② 收口——blob 无 fts_recover_sleep_from_gesture 符号（全 ko 零
 * fts_recover_sleep_from_gesture/零 fts_irq_disable+write_reg(SLEEP) 组合）；其
 * 「手势→睡眠」语义由 blob fts_resume_suspend 的 suspend 半（树侧 fts_ts_suspend
 * 内 FTS_REG_POWER_MODE_SLEEP 段）承担。原 donor recover 对已随 switch_mode
 * blob 化删除，避免不可达死码。 */
static void fts_recover_gesture_from_sleep(struct fts_ts_data *data)
{
	FTS_FUNC_ENTER();
	fts_reset_proc(50);
	fts_tp_state_recovery(data);
	fts_irq_enable();
	/* _b581：blob 0x3f60..0x3fb0 内联本函数处无 irq_wake 处理（blob 全 ko 的
	 * irq_set_irq_wake 仅 4 处：focal_select_touchmode×2 / fts_gesture_suspend /
	 * fts_gesture_resume），故删去 donor 的 enable_irq_wake 块。 */
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

	/* _b582-INTA：A-80④① lockdown 格式化按 blob 收口（⑥-b 报告 §5.3）——
	 * blob fts_get_lockdown_information(0xe5c) 全函数只两条打印：
	 *   失败 0xeb4：E "can't get lockdown"（0x5fd1，线 638；树侧 "lockdown_info init fail"
	 *               在 blob 全 ko 无此串 ⇒ 一并按 blob 换字面量）；
	 *   成功 0xed4：**单条** I "lockdown info: 0x%02x,×8"（0xb71，线 644），
	 *               直接读 ts_data->lockdown_info[0..7]（blob 0xaf0..0xaf7）。
	 * 树侧原 8 次 sprintf 拼 " %02x " + before/after 两条 I 打印（callface
	 * fts_get_lockdown_information only-tree={sprintf:8, _printk:1}）⇒ 按 blob 删净；
	 * 无调用面变化（ft_read_lockdown_info_proc 仍 1 次）。 */
	ret = fts_read_lockdown_info_proc(ts_data->lockdown_info);
	if (ret) {
		FTS_ERROR("can't get lockdown");
		return -EIO;
	}
	FTS_INFO("lockdown info: 0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x",
		 ts_data->lockdown_info[0], ts_data->lockdown_info[1],
		 ts_data->lockdown_info[2], ts_data->lockdown_info[3],
		 ts_data->lockdown_info[4], ts_data->lockdown_info[5],
		 ts_data->lockdown_info[6], ts_data->lockdown_info[7]);
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
	/* _b582-INPUT：按 blob 收口——FOD 收尾块（blob 0xf48 cbz [ts+0xb50]）整块条件化：
	 * 仅 finger_in_fod 置位时进锁，锁内清 finger_in_fod/overlap_area 后**补发**
	 * 0x152(BTN_INFO) UP + ABS_MT_WIDTH_MAJOR/MINOR 归零 + SYN，再
	 * update_fod_press_status_common(0)（blob 4 处 input_event 全在本块：
	 * 0xf80/0xf94/0xfa8/0xfbc）；finger_in_fod 打印在块后（blob 源行 695 > 692）。 */
	if (ts_data->finger_in_fod) {
		mutex_lock(&ts_data->report_mutex);
		ts_data->finger_in_fod = false;
		ts_data->overlap_area = 0;
		input_report_key(input_dev, BTN_INFO, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_sync(input_dev);
		update_fod_press_status_common(0);
		FTS_INFO("ts fod up for suspend");
		mutex_unlock(&ts_data->report_mutex);
	}
	FTS_INFO("%s : finger_in_fod = %d", __func__, fts_data->finger_in_fod);
#endif

    mutex_lock(&ts_data->report_mutex);
#if FTS_MT_PROTOCOL_B_EN
    for (finger_count = 0; finger_count < max_touches; finger_count++) {
        input_mt_slot(input_dev, finger_count);
        input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
        /* _b582-INPUT：blob 0x1028 处逐槽 last_touch_events_collect_common(finger_count, 0) */
        last_touch_events_collect_common(finger_count, 0);
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
                /* _b582-INPUT：blob 0x71e4 = 先补发 0x152(BTN_INFO) 按下，再 update_fod_press_status_common(1)，
                 * 后接 "Report_0x152 resume DOWN"（blob 0x7430）——原树缺这条 input_event。 */
                input_report_key(input_dev, BTN_INFO, 1);
                update_fod_press_status_common(1);
                FTS_INFO("Report_0x152 resume DOWN");
                /* mi_disp_set_fod_queue_work(1, true); */
	    }
	    input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, ts_data->overlap_area);
            input_report_abs(input_dev, ABS_MT_TOUCH_MINOR, events[i].minor);
	    /*input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, ts_data->overlap_area);*/
            /*input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, events[i].area);*/
            /* _b582-INPUT：blob 0x722c/0x7240 直接上送 events[i].x/.y（无超分换算）——
             * 树侧 device-coords 系数 100 == SUPER_RESOLUTION_FACOTR，原式在 rodin 上恒等，
             * 按 blob 形态收口。 */
            input_report_abs(input_dev, ABS_MT_POSITION_X, events[i].x);
            input_report_abs(input_dev, ABS_MT_POSITION_Y, events[i].y);

            touch_down_point_cur |= (1 << events[i].id);
            touch_point_pre |= (1 << events[i].id);

            if ((ts_data->log_level >= 3) ||
                ((ts_data->log_level >= 1) && (events[i].flag == FTS_TOUCH_DOWN))) {
                FTS_DEBUG("[B]P%d(%d, %d)[p:%d,tm:%d] DOWN!",
                          events[i].id, events[i].x, events[i].y,
                          events[i].p, events[i].area);
            }
            /* _b582-INPUT：blob 0x70b8 = 下行 collect(id, 1) */
            last_touch_events_collect_common(events[i].id, 1);
        } else {
            input_mt_slot(input_dev, events[i].id);
            input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
            touch_point_pre &= ~(1 << events[i].id);
            if (ts_data->log_level >= 1)
                    FTS_DEBUG("[B]P%d UP!", events[i].id);
            /* _b582-INPUT：blob 0x7330 = 上行 collect(id, 0) */
            last_touch_events_collect_common(events[i].id, 0);
        }
    }

    if (unlikely(touch_point_pre ^ touch_down_point_cur)) {
        for (i = 0; i < max_touch_num; i++)  {
            if ((1 << i) & (touch_point_pre ^ touch_down_point_cur)) {
                if (ts_data->log_level >= 1)
                        FTS_DEBUG("[B]P%d UP!", i);
                input_mt_slot(input_dev, i);
                input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
                /* _b582-INPUT：blob 0x7560 = 差集补发 collect(i, 0) */
                last_touch_events_collect_common(i, 0);
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
				/* _b582-INTA：族对齐 D→V（blob 0x56ea '\0016[FTS_TS_V][%s:%d]: TX%d ~ TX%d …'，
				 * 引用点 fts_irq_handler+0xb98/0xe04，门 cmp w8,#5; b.hs） */
				FTS_VERBOSE("TX%d ~ TX%d (cnt:%llu, frame_no:%hu):\n%s",
						(col - TX_PRINT_MAX_NUM), col - 1, cnt, frame_no, str);
				memset(str, 0, sizeof(str));
				sprintf(str + strlen(str), "[FTS]");
			} else if (i == size - 1) {
				FTS_VERBOSE("TX%d ~ TX%d (cnt:%llu, frame_no:%hu):\n%s",
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
			/* _b582-INTA：族对齐 D→V（blob 0xaea4 '\0016[FTS_TS_V][%s:%d]: frame size: %d, frame data index: %d'，
			 * 引用点 fts_irq_handler+0x908，门 cmp w8,#5; b.lo） */
			FTS_VERBOSE("frame size: %d, frame data index: %d", ts_data->touch_size, ic_head_cnt);
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
    /* _b582-INPUT：原 int ret 仅服务于 donor 的 dev_pm_qos_add_request 返回值检查，
     * 按 blob 换成 cpu_latency_qos_（void）后删除，避免 unused-variable 告警。 */

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

    /* _b582-INPUT：blob 0x5e2c/0x6a98 = cpu_latency_qos_add_request(&req, 0) /
     * cpu_latency_qos_remove_request(&req)——树侧 donor 的 dev_pm_qos_* 三参形态
     * （blob 无 dev_pm_qos_add_request 调用面）按 blob 收口；句柄类型随之由
     * struct dev_pm_qos_request 改为 struct pm_qos_request（blob 该槽 0x270 恒 48B，
     * 二者同宽，布局不变；goodix 同形先例 core.h:pm_qos_req_irq）。 */
    cpu_latency_qos_add_request(&ts_data->pm_qos_req_irq, 0);

    ts_data->intr_jiffies = jiffies;
    fts_prc_queue_work(ts_data);
#ifdef CONFIG_TOUCH_BOOST
    lpm_disable_for_dev(true, LPM_EVENT_INPUT);
#endif
    fts_irq_read_report(ts_data);

    /* _b582-INTA：按 blob 补 fts_irq_handler 内联的掌面读数块（①报告 §四.2，
     * blob 0x5ea8-0x5f20，紧跟在 fts_irq_handler 内联的 touch 数据 fts_read(0x5ea4) 之后）：
     *   0x5ea8  ldr w8,[x19,#0xbd8]  → 门控 ts_data->palm_status（blob +0xbd8；
     *                                6.18 树侧 palm_status 为 +0x20b0，符号访问不受漂移影响）
     *   0x5ec0  mov w0,#0x9b         → fts_read_reg(0x9B, &val)（val 初值 0）
     *   0x5ecc  tbnz w0,#0x1f        → val 读失败：E "read palm data error\n"
     *                                （blob 0x10f8f，引用点 +0xd44，门 cbnz lv ⇒ E 族）
     *   0x5ed4  cmp w0,#0x1 / cbnz   → val∈{0,1} 才处理
     *   0x5ee0  update_palm_sensor_value_common(val)（实参 = val，非恒 0）
     *   0x5f00  I "update palm data:0x%02X"（blob 0x82ed，引用点 +0x16c，门 cmp w8,#3）
     * 成对性：本块内 update_palm_sensor_value_common 的调用面 = 1（与 blob 一致），
     * fts_read_reg = 1（blob fts_irq_handler 亦各 1；① gate_b582.txt 的
     * "fts_read_reg:1 / update_palm_sensor_value_common:1" 残留即指本缺口）。 */
    if (ts_data->palm_status && fts_data) {
        u8 palm_value = 0;

        if (fts_read_reg(0x9B, &palm_value) < 0) {
            FTS_ERROR("read palm data error\n");
        } else if (palm_value <= 1) {
            update_palm_sensor_value_common(palm_value);
            FTS_INFO("update palm data:0x%02X", palm_value);
        }
    }

    if (ts_data->touch_analysis_support && ts_data->ta_flag) {
        ts_data->ta_flag = 0;
        if (ts_data->ta_buf && ts_data->ta_size)
            memcpy(ts_data->ta_buf, ts_data->touch_buf, ts_data->ta_size);
        wake_up_interruptible(&ts_data->ts_waitqueue);
    }
    /* _b573 boost wiring: blob 0x6a8c-0x6a90 = touch_irq_cpumask(0)（框架内 once 语义） */
    touch_irq_cpumask(TOUCH_ID);

    cpu_latency_qos_remove_request(&ts_data->pm_qos_req_irq);

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
    /* _b582-INPUT：A-80① 输入设备层成对拆改（fts 侧，行为基准 = blob）——
     * blob `fts_ts_probe`（fts_input_init 全内联，0x7dcc/0x8014 input_allocate_device
     * … 0x81f4 input_register_device）为 **IC 自建**输入设备，且 blob 全 ko 无
     * register_xiaomi_input_dev 类符号/串 ⇒ 此处按 blob 自建，删框架调用。
     * 逐项常量（blob 反汇编）：name = "focaltech_ts"（0x7dd4）；
     * bustype = ([ts_data+0xad8]==1) ? BUS_I2C(0x18) : BUS_SPI(0x1c)（0x7dec-0x7e00）；
     * propbit |= INPUT_PROP_DIRECT、evbit |= EV_SYN|EV_KEY|EV_ABS（0x7e1c-0x7e28）；
     * keybit |= BTN_TOOL_FINGER|BTN_TOUCH（0x7e04 orr #0x420）；
     * slots = max_touch_number + INPUT_MT_DIRECT(0x2)（0x811c-0x8128）；
     * abs ×5 = POSITION_X/Y(0x35/0x36，x_min→x_max **不减 1**，0x812c-0x8160)、
     * TOUCH_MAJOR(0x30，0→0xFF)、WIDTH_MAJOR(0x32)/WIDTH_MINOR(0x33，x_min→x_max-1)；
     * cap ×7 = 键区最多 4 枚（have_key/key_number 门控）+ KEY_WAKEUP(0x8f) +
     * KEY_GOTO(0x162) + BTN_INFO(0x152)（0x818c/0x819c/0x81ac）；
     * 失败路径 = input_set_drvdata(dev,NULL) + input_free_device（0x8208-0x8210）。
     * 笔设备为 blob 所无（blob 无 pen allocate/串），不引入。 */
    int ret = 0;
    int key_num = 0;
    struct fts_ts_platform_data *pdata = ts_data->pdata;
    struct input_dev *input_dev;

    FTS_FUNC_ENTER();
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
    input_set_abs_params(input_dev, ABS_MT_POSITION_X, pdata->x_min, pdata->x_max, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_POSITION_Y, pdata->y_min, pdata->y_max, 0, 0);
    input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 0xFF, 0, 0);
#if FTS_REPORT_PRESSURE_EN
    input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 0xFF, 0, 0);
#endif
    input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
    input_set_capability(input_dev, EV_KEY, KEY_GOTO);
    input_set_capability(input_dev, EV_KEY, BTN_INFO);
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

	ts->pinctrl_state_spimode = pinctrl_lookup_state(ts->pinctrl, "pmx_gt_spi_mode");
	if (IS_ERR_OR_NULL(ts->pinctrl_state_spimode)) {
		ret = PTR_ERR(ts->pinctrl_state_spimode);
		/*FTS_ERROR("Can not lookup pinctrl_spi_mode pinstate %d\n", ret);*/
		ts->pinctrl_state_spimode = NULL;
		FTS_INFO("Can not lookup pmx_gt_spi_mode pinstate %d, no need for pmx_gt_spi_mode", ret);
		/* goto err_pinctrl_lookup; */
	}
	ts->pinctrl_state_cs_spimode = pinctrl_lookup_state(ts->pinctrl, "pmx_gt_cs_spi_mode");
	if (IS_ERR_OR_NULL(ts->pinctrl_state_cs_spimode)) {
		ret = PTR_ERR(ts->pinctrl_state_cs_spimode);
		ts->pinctrl_state_cs_spimode = NULL;
		FTS_INFO("Can not lookup pmx_gt_cs_spi_mode pinstate %d, no need for pmx_gt_cs_spi_mode", ret);
	}
	ts->pinctrl_state_cs_gpiomode = pinctrl_lookup_state(ts->pinctrl, "pmx_gt_cs_gpio_mode");
	if (IS_ERR_OR_NULL(ts->pinctrl_state_cs_gpiomode)) {
		ret = PTR_ERR(ts->pinctrl_state_cs_gpiomode);
		ts->pinctrl_state_cs_gpiomode = NULL;
		FTS_INFO("Can not lookup pmx_gt_cs_gpio_mode pinstate %d, no need for pmx_gt_cs_gpio_mode", ret);
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
	ts->pinctrl_state_cs_spimode = NULL;
	ts->pinctrl_state_cs_gpiomode = NULL;
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

/* _b581：A-74③／续① 全形复核落码 —— blob fts_power_source_ctrl_simplify
 * (0x6bf8, 796B) 逐点还原：
 *   enable : avdd reg_enable(+0xae8) -> avdd_gpio 置 1(gpio_to_desc+
 *            gpiod_direction_output_raw) -> usleep_range(3000,3100) ->
 *            iovdd reg_enable(+0xae0) -> power_disabled=false
 *   disable: iovdd reg_disable -> usleep_range(3000,3100) -> avdd reg_disable ->
 *            avdd_gpio 置 0 + FTS_INFO("disable avdd gpio") -> power_disabled=true
 * blob 该函数体内 print 串与行号见 .rodata.str1.1 反解（L1969/1971/1981/1983/
 * 1995/1997/2007/2009/2011 与 6.6 dmesg "successs to enable avdd/iovdd:1971/1983" 互证）。
 * 无 *_source 稳压器、无 dvdd pinctrl（+0xb20/+0xb28 在 blob 全 ko 只被 init 写、
 * 零读 —— 死字段），故树侧一律不接。 */
static int fts_power_source_ctrl_simplify(struct fts_ts_data *ts_data, int enable)
{
	int ret = 0;

	FTS_FUNC_ENTER();					/* blob L1963 */
	if (enable) {
		if (ts_data->power_disabled) {
			if (!IS_ERR_OR_NULL(ts_data->avdd)) {
				ret = regulator_enable(ts_data->avdd);
				if (ret)
					FTS_ERROR("enable avdd regulator failed,ret=%d", ret);	/* L1969 */
				else
					/* _b582-INTA：HIT_NL 补尾 '\n'（blob 0xbf27 '\0016[FTS_TS_I][%s:%d]:
					 * successs to enable avdd\n'，引用点 fts_power_source_ctrl_simplify+0x2a8） */
					FTS_INFO("successs to enable avdd\n");			/* L1971 */
			}
			if (gpio_is_valid(ts_data->pdata->avdd_gpio)) {
				struct gpio_desc *avdd_desc =
					gpio_to_desc(ts_data->pdata->avdd_gpio);

				if (avdd_desc)
					gpiod_direction_output_raw(avdd_desc, 1);
			}
			usleep_range(3000, 3100);		/* blob usleep_range_state(3000,3100,2) */
			if (!IS_ERR_OR_NULL(ts_data->iovdd)) {
				ret = regulator_enable(ts_data->iovdd);
				if (ret)
					FTS_ERROR("enable iovdd regulator failed,ret=%d", ret);	/* L1981 */
				else
					/* _b582-INTA：HIT_NL 补尾 '\n'（blob 0x1eef '\0016[FTS_TS_I][%s:%d]:
					 * successs to enable iovdd\n'，引用点 fts_power_source_ctrl_simplify+0x2c4） */
					FTS_INFO("successs to enable iovdd\n");			/* L1983 */
			} else {
				/* blob 0x6cf8：iovdd 指针为空/err 时的 else 打印（L1986）
				 * _b582-INTA：HIT_NL 补尾 '\n'（blob 0x263f '\0016[FTS_TS_E][%s:%d]:
				 * failed to get iovdd regulator\n'，引用点 +0x108） */
				FTS_ERROR("failed to get iovdd regulator\n");
			}
			ts_data->power_disabled = false;
		}
	} else {
		if (!ts_data->power_disabled) {
			if (!IS_ERR_OR_NULL(ts_data->iovdd)) {
				ret = regulator_disable(ts_data->iovdd);
				if (ret)
					FTS_ERROR("disable iovdd regulator failed,ret=%d", ret);	/* L1995 */
				else
					/* _b582-INTA：HIT_NL 补尾 '\n'（blob 0x2f25 '\0016[FTS_TS_I][%s:%d]:
					 * %s: successs to disable iovdd\n'，`%s` 前缀与 __func__ 实参保持） */
					FTS_INFO("%s: successs to disable iovdd\n", __func__);	/* L1997 */
			}
			usleep_range(3000, 3100);
			if (!IS_ERR_OR_NULL(ts_data->avdd)) {
				ret = regulator_disable(ts_data->avdd);
				if (ret)
					FTS_ERROR("disable avdd regulator failed,ret=%d", ret);	/* L2007 */
				else
					/* _b582-INTA：HIT_NL 补尾 '\n'（blob 0x117d9 '\0016[FTS_TS_I][%s:%d]:
					 * successs to disable avdd\n'，引用点 fts_power_source_ctrl_simplify+0x300） */
					FTS_INFO("successs to disable avdd\n");			/* L2009 */
			}
			if (gpio_is_valid(ts_data->pdata->avdd_gpio)) {
				struct gpio_desc *avdd_desc =
					gpio_to_desc(ts_data->pdata->avdd_gpio);

				if (avdd_desc) {
					gpiod_direction_output_raw(avdd_desc, 0);
					FTS_INFO("disable avdd gpio");			/* L2011 */
				}
			}
			ts_data->power_disabled = true;
		}
	}
	FTS_FUNC_EXIT();
	return ret;
}

/* _b581：A-74③／续① —— blob fts_power_source_ctrl (0x9850, 240B) 逐点还原：
 *   enable : FTS_INFO("regulator enable !")(L2026) -> reset_gpio=0 ->
 *            usleep(2000,2100) -> simplify(ENABLE)(0x98c0) -> usleep(500,510) ->
 *            pinctrl cs_spi_mode(+0xaf8/+0xb38, 失败复位 ret 为 0)(L2036/2038)
 *   disable: write_reg(SET_ID_G_HOST_RST_FLAG=0xB6, 1) + msleep(20)【无条件，先于
 *            power_disabled 判定】-> if(!power_disabled): FTS_INFO("regulator
 *            disable !")(L2047) -> reset_gpio=0 -> usleep(2000,2100) ->
 *            pinctrl cs_gpio_mode(+0xaf8/+0xb30)(L2055/2057) -> simplify(DISABLE)
 *            （返回值不复用，函数返回 cs 段的 ret）
 * 树侧原实现把 enable/disable 链全部内联并多接 dvdd pinctrl 与 *_source 稳压器；
 * 按 blob 收口后本函数 print 数=8、pinctrl_select_state=2、usleep_range_state=3。 */
static int fts_power_source_ctrl(struct fts_ts_data *ts_data, int enable)
{
	int ret = 0;

	FTS_FUNC_ENTER();					/* blob L2023 */
	if (enable) {
		if (ts_data->power_disabled) {
			FTS_INFO("regulator enable !");		/* L2026 */
			gpio_direction_output(ts_data->pdata->reset_gpio, 0);
			usleep_range(2000, 2100);
			ret = fts_power_source_ctrl_simplify(ts_data, ENABLE);
			usleep_range(500, 510);
			if (ts_data->pinctrl && ts_data->pinctrl_state_cs_spimode) {
				ret = pinctrl_select_state(ts_data->pinctrl,
							   ts_data->pinctrl_state_cs_spimode);
				if (ret < 0)
					FTS_ERROR("Set pinctrl_cs_spi_mode error:%d", ret);	/* L2036 */
				else
					FTS_INFO("Set pinctrl_cs_spi_mode sucesses.");		/* L2038 */
			} else {
				ret = 0;
			}
		} else {
			ret = 0;
		}
	} else {
		/* blob 0x9934：disable 先无条件写 RST_FLAG 并 msleep(20)（返回值不判） */
		fts_write_reg(SET_ID_G_HOST_RST_FLAG, 0x01);
		msleep(20);
		if (!ts_data->power_disabled) {
			FTS_INFO("regulator disable !");	/* L2047 */
			gpio_direction_output(ts_data->pdata->reset_gpio, 0);
			usleep_range(2000, 2100);
			if (ts_data->pinctrl && ts_data->pinctrl_state_cs_gpiomode) {
				ret = pinctrl_select_state(ts_data->pinctrl,
							   ts_data->pinctrl_state_cs_gpiomode);
				if (ret < 0)
					FTS_ERROR("Set pinctrl_cs_gpio_mode error:%d", ret);	/* L2055 */
				else
					FTS_INFO("Set pinctrl_cs_gpio_mode sucesses.");		/* L2057 */
			} else {
				ret = 0;
			}
			fts_power_source_ctrl_simplify(ts_data, DISABLE);
		} else {
			ret = 0;
		}
	}
	FTS_FUNC_EXIT();					/* blob L2064 */
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

    FTS_FUNC_ENTER();
    /* _b581：A-74③ 全形复核 —— blob fts_power_source_init (0x8dd4) 只用两个字面量
     * 稳压器名：regulator_get(dev, "avdd")（0x8e00 直接取 .rodata 串，非 pdata 成员）
     * 与 regulator_get(dev, "iovdd")（0x8e90）。6.6 出厂 dmesg 互证：
     *   [FTS_TS_I][fts_parse_dt:2328]: iovdd name from dt: iovdd_focal
     *   focaltech_ts spi1.0: supply avdd not found, using dummy regulator
     * （即运行期用的就是字面量 "avdd"；"iovdd" 命中 DTB 同节点的 iovdd-supply=
     *  &mt6368_vtp，故无 dummy 告警）。blob 无 iovdd_source/avdd_source 参与
     * （regulator_* 各 2 次），树侧同步删除。 */
    ts_data->avdd = regulator_get(ts_data->dev, "avdd");
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

    ts_data->iovdd = regulator_get(ts_data->dev, "iovdd");
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

/* _b581：A-74④ 续行 —— blob fts_power_source_exit (0x9744) 是 2 形参
 * (ts_data, flag)：pinctrl release 段后按 flag 分派 —— flag!=0 → fts_power_source_ctrl
 * (DISABLE)（全链：RST_FLAG/msleep/reset gpio/cs pinctrl），flag==0 →
 * fts_power_source_ctrl_simplify(DISABLE)（只动稳压器与 avdd-gpio）。
 * blob 调用点：fts_ts_probe +0x87a0 w1=1（probe 失败路径）、fts_ts_remove +0x8be8
 * w1=0（remove 路径），树侧同步补 flag。 */
static int fts_power_source_exit(struct fts_ts_data *ts_data, int flag)
{
#if FTS_PINCTRL_EN
    fts_pinctrl_select_release(ts_data);
#endif

    if (flag)
        fts_power_source_ctrl(ts_data, DISABLE);
    else
        fts_power_source_ctrl_simplify(ts_data, DISABLE);

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

    /* _b581：A-74④ 电源链组织对齐 —— blob fts_ts_probe 内联 fts_parse_dt 的调用
     * 次序为 reset-gpio(0x79b8) → irq-gpio(0x79d0) → avdd-gpio(0x79e8) →
     * avdd-name(0x7a3c) → iovdd-name(0x7aa8)；blob 全 ko 无 iovdd_source/avdd_source
     * 属性读取（of_property_read_string 仅 2 处），故删去树侧 2 个 *_source-name 读。 */
    pdata->reset_gpio = of_get_named_gpio(np, "focaltech,reset-gpio", 0);
    if (pdata->reset_gpio < 0)
        FTS_ERROR("Unable to get reset_gpio");

    pdata->irq_gpio = of_get_named_gpio(np, "focaltech,irq-gpio", 0);
    if (pdata->irq_gpio < 0)
        FTS_ERROR("Unable to get irq_gpio");
    /* _b581：blob parse_dt 不写 pdata->irq_gpio_flags（blob 只在 fts_irq_registration
     * 0x96e0 写 0x2002），故此处不再赋值以免与 blob 面不一致。 */

    /* blob 同形：avdd 由 GPIO 承重，出厂 parse_dt 先读 focaltech,avdd-gpio 再读 name */
    pdata->avdd_gpio = of_get_named_gpio(np, "focaltech,avdd-gpio", 0);
    if (gpio_is_valid(pdata->avdd_gpio))
        FTS_INFO("get avdd-gpio[%d] from dt", pdata->avdd_gpio);
    else
        FTS_ERROR("can't find avdd-gpio, use other power supply");

    /* _b583b-B10：名字域按 blob 换 char[40] 内嵌数组（0x18/0x40 槽）。blob 形态
     * （probe 0x10A50..0x10AFC）：先 memset 清零 40B → of_property_read_string →
     * 成功且 strlen<=0x27 才 strncpy(dst,src,0x28)；读失败或超长保持全零
     * （无 "avdd"/"iovdd" 字面量兜底 —— 电源面 regulator_get 用字面量，与该
     * 成员无耦合，见 fts_power_source_init）。 */
    memset(pdata->avdd_reg_name, 0, sizeof(pdata->avdd_reg_name));
    ret = of_property_read_string(np, "focaltech,avdd-name", &name);
    if (ret == 0) {
        FTS_INFO("avdd name from dt: %s", name);	/* blob L2322 串（0xf694） */
        if (strlen(name) <= 0x27)
            strncpy(pdata->avdd_reg_name, name, sizeof(pdata->avdd_reg_name));
    }

    memset(pdata->iovdd_reg_name, 0, sizeof(pdata->iovdd_reg_name));
    ret = of_property_read_string(np, "focaltech,iovdd-name", &name);
    if (ret == 0) {
        FTS_INFO("iovdd name from dt: %s", name);	/* blob L2328 串（0x8bb3） */
        if (strlen(name) <= 0x27)
            strncpy(pdata->iovdd_reg_name, name, sizeof(pdata->iovdd_reg_name));
    }

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

    /* _b582-INTA：族对齐 I→D（blob 0xb638 '\0016[FTS_TS_D][%s:%d]: max touch number:%d, irq gpio:%d, reset gpio:%d'，
     * 引用点 fts_ts_probe+0x948（fts_parse_dt 全内联），门 cmp w8,#4; b.hs） */
    FTS_DEBUG("max touch number:%d, irq gpio:%d, reset gpio:%d",
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
	/* _b583b-T4b：blob fts_set_charge_state (0xCD28, data 面被
	 * fts_init_xiaomi_touchfeature_v3+0x1F0 取址) 逐点收口 ——
	 *   0xCD38-0xCD40  !fts_data → 直接返回；
	 *   0xCD44-0xCD48  门 = [ts_data,#0x2D8](pm_suspend)：非挂起才动充电链；
	 *   0xCD58-0xCD6C  挂起分支 = FTS_ERROR("TP is in suspend mode, don't set usb
	 *                  status!")（串 0x4D76C = '\0016[FTS_TS_E][%s:%d]: TP is in
	 *                  suspend mode, don't set usb status!'，门 cbz lv ⇒ E 族）；
	 *   0xCD74-0xCD7C  pm_stay_awake([ts,#0x10]=dev)；
	 *   0xCD80-0xCD90  [ts,#0x1A8](charger_status) = status 后 fts_charger_on(fts_data,
	 *                  status != 0)（CSET NE —— 0/非0 归一，不再透传原值）；
	 *   0xCD94-0xCD9C  pm_relax(dev)。
	 * 原树 donor 的 fts_write_reg(0x8B) 双写与 I/E/D 三族打印全部删除（其宿主在 blob
	 * 为 fts_charger_on，本函数 callface = {fts_charger_on, pm_stay_awake, pm_relax}
	 * 与 blob 全等，无 _printk/无 fts_write_reg）。 */
	struct fts_ts_data *ts_data = fts_data;

	if (!ts_data)
		return;

	if (ts_data->pm_suspend) {
		FTS_ERROR("TP is in suspend mode, don't set usb status!");
		return;
	}

	pm_stay_awake(ts_data->dev);
	ts_data->charger_status = status;
	fts_charger_on(fts_data, status != 0);
	pm_relax(fts_data->dev);
}
#ifdef FTS_TOUCHSCREEN_FOD
static void fts_xiaomi_touch_fod_test(int value)
{
	struct input_dev *input_dev = fts_data->input_dev;

	if (value) {
		/* blob 54d4: input_event(input, EV_KEY, 0x152, 1) */
		input_report_key(input_dev, BTN_INFO, 1);
		update_fod_press_status_common(1);
		/* blob 54f0: input_event(input, EV_SYN, 0, 0) */
		input_sync(input_dev);
		input_mt_slot(input_dev, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
		input_report_key(input_dev, BTN_TOUCH, 1);
		input_report_key(input_dev, BTN_TOOL_FINGER, 1);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 1);
		input_report_abs(input_dev, ABS_MT_POSITION_X, 9744);	/* 609*16 */
		input_report_abs(input_dev, ABS_MT_POSITION_Y, 38992);	/* 2437*16 */
		input_sync(input_dev);
	} else {
		input_mt_slot(input_dev, 0);
		input_report_abs(input_dev, ABS_MT_WIDTH_MINOR, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
		input_report_abs(input_dev, ABS_MT_TRACKING_ID, -1);
		/* blob 55f0: input_event(input, EV_KEY, 0x152, 0) */
		input_report_key(input_dev, BTN_INFO, 0);
		update_fod_press_status_common(0);
		input_sync(input_dev);
	}
}
#endif
#ifdef FTS_XIAOMI_TOUCHFEATURE

hardware_operation_t hardware_operation;
hardware_param_t hardware_param;
/*static struct xiaomi_touch_interface xiaomi_touch_interfaces;*/

/* _b581：A-74② 计数对齐——blob 该函数为独立符号、且 fts_ic_switch_mode 内正好
 * 2 处 bl 直调（AOD / DoubleTap，0x3e84 / 0x3ee0），编译器未内联；树侧 2 处调用点
 * 会被 LLVM 内联（改前 5 处调用点全部内联成 .text+0x5c34 副本），故显式禁止内联，
 * 使调用面与 blob 一致（blob 该函数体仅 2 条 _printk，本就「小函数不内联」的形态）。 */
static noinline void fts_update_gesture_state(struct fts_ts_data *ts_data, int bit, bool enable)
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

	/* _b582-SLEEP：blob 0x5bdc 起 suspended（+0x2d9）分支——睡眠期只登记
	 * 「延迟」（gesture_cmd_delay/+0xbec），不动 gesture_support；唤醒时由
	 * fts_ts_suspend 的 gesture_cmd_delay 块消费（成对）。blob 串：
	 * 0x8235 = "TP is suspended, do not update gesture state"（E 级，门 debug!=0，
	 * L2645）；0x11688 = "delay gesture state:0x%02X, delay write cmd:0x%02X"
	 * （I 级，门 debug>=3，L2647，实参 gesture_status/gesture_cmd）。 */
	if (ts_data->suspended) {
		if (fts_debug_log_level)
			FTS_ERROR("TP is suspended, do not update gesture state");
		ts_data->gesture_cmd_delay = true;
		if (fts_debug_log_level >= 3)
			FTS_INFO("delay gesture state:0x%02X, delay write cmd:0x%02X", ts_data->gesture_status, ts_data->gesture_cmd);
	} else {
		/* blob 0x5bf8 起 else 半：L2651（I 级串 "AOD: %d DoubleClick: %d "，门
		 * debug>=3）+ L2652（"gesture state:0x%02X, write cmd:0x%02X"，门 debug>=3）
		 * + gesture_support = (gesture_status != 0)（blob 0x5c00）。 */
		/* _b582-SLEEP：blob 该条是 I 级串（\0016[FTS_TS_I]...: AOD: %d DoubleClick: %d ，
		 * 0xae47，门 debug>=3），树侧原 FTS_DEBUG 为 D 级/门>=4，改回 FTS_INFO 对齐。 */
		FTS_INFO("AOD: %d DoubleClick: %d ", ts_data->gesture_status>>1 & 0x01, ts_data->gesture_status & 0x01);
		FTS_INFO("gesture state:0x%02X, write cmd:0x%02X", ts_data->gesture_status, ts_data->gesture_cmd);
		ts_data->gesture_support = ts_data->gesture_status != 0 ? ENABLE : DISABLE;
	}
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

/* _b583-FTS（A7-d）：原 fts_switch_report_rate() 已删 —— blob 无独立符号（全 ko
 * .symtab 0 命中；其代码内联于 fts_game_mode_update，串 "fts_switch_report_rate" 的 8 个
 * 引用点全在该函数内），树侧 fts_game_mode_update 已按 blob 手工展开该段 ⇒ 本函数在树侧
 * 仅剩 A7-d 删除的 DATA_MODE_54 调用点，删除后无引用者，按 blob 一并删除。 */

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


/* _b583-FTS（A7-c）：原 fts_send_camera_report_rate() 已删（donor 件）——
 * blob 符号面（.symtab + UND 表）0 命中、串面 0 命中、跳表无对应 case；树侧原调用点
 * （DATA_MODE_25）已同批删除。 */

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
	/* _b583-FTS（A7-f 续）：原 donor 护栏
	 * `if (fts_data->suspended && mode_input != DATA_MODE_138) { INFO("tp is suspend,
	 * skip set_cur_value: ..."); return; }` 已删 —— blob 0x45F0 全函数对 [ts_data,#0x2D0..
	 * 0x2E0] **零访问**（tools/_b583_ftsface/blbscan.py 0x2d0 0x2e0 fts_set_cur_value 空结果；
	 * 函数序言只做 `tbnz w0,#0x1f`（mode<0）与 `ldr x9,[fts_data]; cbz x9` 两项校验），
	 * 且串 "tp is suspend, skip set_cur_value" 在 blob 全 ko 0 命中（classify_intD ③树）。 */

	/* _b582-INTA：族对齐 D→I + 站点数收口 —— blob fts_set_cur_value 只有**一个**
	 * touch mode 站点（0x48cc，\0016[FTS_TS_I][%s:%d]: touch mode:%d, value:%d，
	 * 门 cmp w8,#3; b.hs），且**无** DATA_MODE_153 门（0x4644 直接 cmp lv,#3 后进
	 * 模式分派）；树侧原 if (mode_input != DATA_MODE_153) I / else D 两条等价分支
	 * ⇒ 按 blob 收成单条 FTS_INFO（打印行为逐字一致）。 */
	FTS_INFO("touch mode:%d, value:%d", mode, value);
	/* _b583-FTS（A7-f）：原 DATA_MODE_9 (=9) 块已删 —— blob 跳表上 mode 9 落 0xCE
	 * = default（"not support mode!"），且串 "Mode:DATA_MODE_9  Report_Rate_status"
	 * 在 blob 全 ko 0 命中（classify_intD ③树）⇒ 连同 0x92 写一起删除。
	 * （blob 的 report-rate 语义在 DATA_MODE_73 = 1011，见下块。） */
	/* _b583-FTS（A7-c）：blob 无 mode 1025 分支（跳表 idx 25 = 0xCE default）；
	 * fts_send_camera_report_rate 在 blob 符号面（含 UND）与串面均 0 命中
	 * （rostr_ref NO-HIT）⇒ 删除调用点与函数本体（下一步）。 */
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
	if (mode == DATA_MODE_73) {
		/* _b583-FTS（A7-f）：blob 0x46FC = 跳表 idx 11（mode 1011 = DATA_MODE_73）：
		 *   `ldr x8,[fts_data]; str w19,[x8,#0xBF4]`（current_fps = value）;
		 *   `mov w0,#0x13; add x1,sp,#0xC; mov w2,#2; bl fts_thp_ic_write_interfaces`
		 *   （SET_REPORT_RATE_TYPE，&value，2）；失败 → _printk(0x4720 =
		 *   .rodata.str1.1+0x116D0 b'…: Failed to switch Report_Rate to %d Hz'，L3127)。
		 * 树侧原块（report_rate_status + "ic report rate skip write"，classify ③树）
		 * ⇒ 按 blob 重写。 */
		fts_data->current_fps = value;
		ret = fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE, &value, 2);
		if (ret < 0)
			FTS_ERROR("Failed to switch Report_Rate to %d Hz", value);
		return;
	}
	if (mode == DATA_MODE_63) {
		if (fts_data->enable_touch_raw)
			fts_set_fod_downup(fts_data, value);
		return;
	}
	if (mode == DATA_MODE_66) {
		/* _b583-FTS（A7-f）：blob 0x46D0 = 跳表 idx 4（mode 1004）——
		 * `ldr w8,[debug_log_level]; cmp w8,#3; b.hs` → _printk(0x49F0 =
		 * b'…: hal init ready.'，L3115)；随后 queue_delayed_work_on(0x20, system_wq,
		 * &ts_data->thp_signal_work, 250)（= schedule_delayed_work(...,HZ)，HZ=250）。
		 * blob 该 case **无** ts_data->enable_touch_raw(0xC88) 读取（0xC88 的 blob 访问点
		 * 只有 enable_touch_raw/game_mode_update/irq_handler/set_cur_value(B)/ic_feature_v3）
		 * ⇒ 删树侧护栏（thp_signal_work 体内本就有 enable_touch_raw 复检）。 */
		FTS_INFO("hal init ready.");
		schedule_delayed_work(&fts_data->thp_signal_work, 1 * HZ);
		return;
	}
	/* _b583-FTS（A7-d）：blob 无 mode 1054 分支（跳表 idx 54 = 0xCE default）；
	 * fts_switch_report_rate 在 blob 无独立符号（其代码以 __func__ =
	 * "fts_switch_report_rate" 内联进 fts_game_mode_update，rostr 0xD137 refs 8 点全在
	 * 该函数），树侧 fts_game_mode_update 已按 blob 手工展开同段（core.c:3347 注）
	 * ⇒ 删除本调用点与函数本体（下一步）。 */
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
	/* _b583-FTS（A7-b）：blob fts_set_cur_value 跳表（.rodata+0x63，104 项）中
	 * mode=1114(DATA_MODE_177) 落 0xCE = default（"not support mode!"）；
	 * blob 符号面（含 UND 表）与串面均无 update_weak_doubletap_value
	 * （该符号定义在框架 xiaomi_touch_sys.c，blob 的 fts ko 从不引用）⇒ 删本调用点。 */
	/* _b582-INTA：补树侧**完全缺失**的 W 族站点 —— blob fts_set_cur_value+0x3cc
	 * （0x49b0）为模式分派的 else 尾块：ldr w8,[x22]; cmp w8,#0x2; b.lo <ret>;
	 * _printk(.rodata.str1.1+0x1170b '\0016[FTS_TS_W][%s:%d]: not support mode!',
	 * __func__, 3160) ⇒ 门控 = 级别 ≥ 2（FTS_LOG_WARNING），族 W。 */
	/* _b583-FTS（A7-f）：补树侧**完全缺失**的三个 blob case（跳表 .rodata+0x63 实测
	 * idx→target：idx 0x00→0x4678(1000) 0x10→0x46B8(1001) 0x16→0x46D0(1004)
	 * 0x21→0x46FC(1011) 0x32→0x4740(1071) 0x37→0x4754(1076) 0x3F→0x4774(1084)
	 * 0x49→0x479C(1091) 0x69→0x481C(1098) 0x6C→0x4828(1099) 0x6F→0x4834(1101)
	 * 0x82→0x4880(1103)，其余 0xCE=default）： */
	if (mode == DATA_MODE_49) {		/* 103：SCP 触控抑制开关 */
		/* blob 0x4968/0x4974：_printk(0x4A50 = .rodata.str1.1+0xEC46
		 * b'…: %s SCP_TP_MISTOUCH'，L3097，open/close 由 value 选)；
		 * 随后 `cmp w19,#0; cset w8,eq; strb w8,[scp_tp_mistouch_close]`（0x4980）。 */
		FTS_INFO("%s SCP_TP_MISTOUCH", value ? "open" : "close");
		fts_scp_tp_mistouch_close = (value == 0);
		return;
	}
	if (mode == DATA_MODE_153) {		/* 1091：glove（blob 内联 fts_htc_enter_glove） */
		/* blob 0x479C-0x4814（__func__ = 0x10F74 "fts_htc_enter_glove"）：
		 *   0x4A0C/0x4A28 _printk(.rodata.str1.1+0x92B7 b'…: glove enable: %d,
		 *     down_thd: %d, up_thd: %d'，L3055，实参 value/value_input[1]/value_input[2])；
		 *   0x47AC fts_write_reg(0xC0, value)，失败 → 0x4A48 _printk(+0x8A65
		 *     b'…: notify ic switch glove mode: %d failed!'，L3058)；
		 *   0x47C4-0x47F4 cmd = {0x95, BE16(value_input[1]), BE16(value_input[2])} →
		 *     fts_write(cmd, 5)，失败 → 0x4804 _printk(+0xAE75
		 *     b'…: set down up level failed!'，L3061)。 */
		u8 writebuf[5] = { 0 };

		FTS_INFO("glove enable: %d, down_thd: %d, up_thd: %d",
			 value, value_input[1], value_input[2]);
		ret = fts_write_reg(SET_GLOVE_EN_TYPE, value);
		if (ret < 0)
			FTS_ERROR("notify ic switch glove mode: %d failed!", value);
		writebuf[0] = SET_DOWN_UP_THD_TYPE;
		writebuf[1] = (u8)(value_input[1] >> 8);
		writebuf[2] = (u8)value_input[1];
		writebuf[3] = (u8)(value_input[2] >> 8);
		writebuf[4] = (u8)value_input[2];
		ret = fts_write(writebuf, sizeof(writebuf));
		if (ret < 0)
			FTS_ERROR("set down up level failed!");
		return;
	}
	if (mode == DATA_MODE_163) {		/* 1101：double scan（blob 内联 fts_htc_set_double_scan） */
		/* blob 0x4834-0x48C8：cmd = {0x9D, value} → fts_write(cmd,2)，失败 →
		 * _printk(+0xE3E3 b'…: data write(addr:%x) fail,value:%x,ret:%d'，L3007，
		 * __func__ = 0xC7B8 "fts_htc_set_double_scan")；树侧同名函数在
		 * focaltech_scp_tp.c:556（非 static，逐字节同形）⇒ 直调。 */
		fts_htc_set_double_scan((u8)value);
		return;
	}
	FTS_WARNING("not support mode!");
	return;
}

static void fts_ic_switch_mode(u8 _gesture_type)
{
	struct fts_ts_data *ts_data = fts_data;
	int value = 0;
	static u8 last_gesture_type = 0;

#if IS_ENABLED(CONFIG_MITEE_TUI_SUPPORT)
	if (atomic_read(&ts_data->tui_process)) {
		if (wait_for_completion_interruptible(&ts_data->tui_finish) ) {
			FTS_ERROR("cautious, ERESTARTSYS may cause cmd loss recomand try again");
			return;
		}
		FTS_INFO("wait finished, its time to go ahead");
	}
#endif
	/* _b573 scp 联动：blob 0x3ddc-0x3e24 = SCP 托管手势（param0==3）时同步手势类型位图
	 * 给 SCP（ipi cmd 5；位序重排 bit0→bit2、bit1-2→bit0-1；Nonui 模式值作第 3 参） */
	if (fts_scp_tp_param.param0 == 3) {
		int scp_gesture_type = ((_gesture_type & 0x1) << 2) | ((_gesture_type >> 1) & 0x3);

		FTS_INFO("[scp-tp]: fts_ic_switch_mode, cur_gesture=0x%x", scp_gesture_type);
		fts_scp_tp_ipi_send(5, scp_gesture_type,
				driver_get_touch_mode_common(TOUCH_ID, Touch_Nonui_Mode), 0);
	}
	/* _b581：A-74② 结构代差收口 —— blob 0x3db0..0x4150 为「逐模式取值驱动」形态：
	 * 每个手势位各自取门控值(AOD=DATA_MODE_11 / DoubleClick=DATA_MODE_14 /
	 * FOD=DATA_MODE_10)，仅在状态位翻转时更新手势位；无 sensor_tap(WEAK_DOUBLETAP)
	 * 分支、无 suspended 态分支（睡眠面由 resume/suspend 承担，blob 该函数零
	 * fts_recover_* 调用）。取门控值发生的次数与 blob 一致（4 次：17/11/14/10）。 */
	if ((_gesture_type & GESTURE_SINGLETAP_EVENT) || (last_gesture_type & GESTURE_SINGLETAP_EVENT)) { /* DATA_MODE_11 */
		value = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_11);
		FTS_INFO("Mode:AOD  aod_status = %d", value);	/* blob L2410 */
		if ((_gesture_type & GESTURE_SINGLETAP_EVENT) ^ (last_gesture_type & GESTURE_SINGLETAP_EVENT)) {/* when aod gesture state change */
			FTS_DEBUG("need to update aod status");	/* blob L2412 */
			fts_update_gesture_state(ts_data, GESTURE_AOD, value != 0 ? true : false);
		}
	}

	if ((_gesture_type & GESTURE_DOUBLETAP_EVENT) || (last_gesture_type & GESTURE_DOUBLETAP_EVENT)) { /* DATA_MODE_14 */
		value = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_14);
		FTS_INFO("Mode:DoubleClick  double_status = %d", value);	/* blob L2419 */
		if ((_gesture_type & GESTURE_DOUBLETAP_EVENT) ^ (last_gesture_type & GESTURE_DOUBLETAP_EVENT)) {/* when double tap gesture state change */
			FTS_DEBUG("need to update double tap status");		/* blob L2422 */
			fts_update_gesture_state(fts_data, GESTURE_DOUBLETAP, value != 0 ? true : false);
		}
	}

	if ((_gesture_type & GESTURE_LONGPRESS_EVENT) || (last_gesture_type & GESTURE_LONGPRESS_EVENT)) { /* Touch_Fod_Mode */
		value = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
		FTS_INFO("Mode:FOD  fod_status = %d", value);	/* blob L2429 */
		/* blob 0x3f0c：仅本拍按下（cur bit0）才动 FOD 电源/恢复面 */
		if (_gesture_type & GESTURE_LONGPRESS_EVENT) {
			ts_data->gesture_support = ENABLE;	/* blob 0x3f1c strb #1 -> +0x2e4 */
			if (!ts_data->finger_in_fod && !value) {
				/* blob 0x3f28：finger 不在 FOD 且门控值 0 -> 关 FOD 寄存器 */
				fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, false);
			} else if (value == 1) {
				/* blob 0x3f4c：值==1 且处于 poweroff_on_sleep -> 整机重挂
				 * （0x3f60 起内联 fts_recover_gesture_from_sleep）+ scp 通道切换 */
				if (ts_data->poweroff_on_sleep) {
					fts_recover_gesture_from_sleep(ts_data);
					ts_data->poweroff_on_sleep = false;
					if (!fts_scp_tp_mistouch_close &&
					    (fts_scp_tp_param.param0 == 2 || fts_scp_tp_param.param0 == 4)) {
						FTS_INFO("sleep_to_gesture, switch to scp");	/* blob L2447 */
						fts_scp_tp_switch(1);
					}
				} else {
					fts_fod_recovery();	/* blob 0x3ffc */
				}
			} else if (value == 2) {
				fts_fod_recovery();		/* blob 0x4000 */
			}
		}
	}

	last_gesture_type = _gesture_type;
}

/*
 * _b581：A-74 续行② 接线 —— blob fts_game_mode_update (0x4154..0x44a0) 为「薄封装」：
 *   1) !mode_update_flag            -> FTS_INFO("no need update mode value") 直接返回（L2465）
 *   2) enable_touch_raw(+0xc88) 置位 -> thp SET_GAME_MODE_EN_TYPE(0x99) 单字 + 打印
 *      （L2473/2474）-> thp SET_REPORT_RATE_TYPE(0x13) 双字推 current_fps(+0xbf4) -> 返回
 *   3) is_expert_mode(+0xbea) = !!(flag & (1<<DATA_MODE_6))；置位时打 "Enter Mode:Expert_Mode"
 *      （L2485）
 *   4) mode_value[0]==1 -> 打 "Mode:Game_Mode  Game_Mode_status = 1"（L2488）+ 内联
 *      fts_switch_report_rate(true)：current_fps=240 + thp 0x13 双字（L2803/2818 串）
 *      否则 current_fps=135 + thp 0x13 双字（L2803/2809 串）
 *   5) 镜像 clamp：mode_value[0..DATA_MODE_8] -> fts_touch_mode[k][SET_CUR]（0x42c4..0x4498）
 *   6) fts_update_touchmode_data()（0x449c，本函数唯一调用点）
 * 树侧原 donor 实现把 C1 命令组装/0x8d/0x8c 下发内联在本函数 —— 与
 * fts_update_touchmode_data 完全重复（该项即「未接 fts_update_touchmode_data」），
 * 按 blob 整体收口为薄封装，避免同一次 mode 更新重复下发。
 */
static void fts_game_mode_update(long mode_update_flag, int mode_value[FTS_TOUCH_MODE_MAX])  /* _b583-FTS：契约按 blob = 35 长数组（步长 0x348 = 35*24）；本函数只读 0..DATA_MODE_8 */
{
	int temp_value = 0;
	int ret = 0;
	int __i;

	if (!mode_update_flag) {
		FTS_INFO("no need update mode value");		/* blob L2465 */
		return;
	}

	if (fts_data->enable_touch_raw) {			/* blob ldrb [ts+0xc88] */
		temp_value = mode_value[DATA_MODE_0];
		ret = fts_thp_ic_write_interfaces(SET_GAME_MODE_EN_TYPE, &temp_value, 1);
		if (ret < 0)
			FTS_ERROR("failed to send game mode: %d", temp_value);	/* L2474 */
		else
			FTS_INFO("game mode status: %s   %d",
				 temp_value ? "ON" : "OFF", temp_value);	/* L2473 */
		fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE,
					    &fts_data->current_fps, 2);
		return;
	}

	fts_data->is_expert_mode =
		(mode_update_flag & (1L << DATA_MODE_6)) ? true : false;	/* blob +0xbea */
	if (fts_data->is_expert_mode)
		FTS_INFO("Enter Mode:Expert_Mode");		/* blob L2485 */

	if (mode_value[DATA_MODE_0] == 1) {
		FTS_INFO("Mode:Game_Mode  Game_Mode_status = 1");	/* blob L2488 */
		/* blob 0x4244..0x4288 = 内联 fts_switch_report_rate(ts_data, true) */
		fts_data->current_fps = 240;			/* blob 0xbf4 */
		FTS_INFO("on: %d, set Report_Rate_status:%s", 1, "240HZ");	/* L2803 */
		ret = fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE,
						  &fts_data->current_fps, 2);
		if (ret < 0)
			FTS_ERROR("failed send report rate cmd to switch Report_Rate to 240HZ, ret=%d",
				  ret);				/* L2818 */
	} else {
		fts_data->current_fps = 135;
		FTS_INFO("on: %d, set Report_Rate_status:%s", 0, "135HZ");	/* L2803 */
		ret = fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE,
						  &fts_data->current_fps, 2);
		if (ret < 0)
			FTS_ERROR("failed send report rate cmd to switch Report_Rate to 135HZ, ret=%d",
				  ret);				/* L2809 */
	}

	/* blob 0x42c4..0x4498：mode_value[0..DATA_MODE_8] 写 SET_CUR 槽 + 按镜像
	 * [GET_MIN,GET_MAX] clamp（9 组，stride 0x18） */
	for (__i = 0; __i <= DATA_MODE_8 && __i < FTS_TOUCH_MODE_MAX; __i++) {
		int __v = mode_value[__i];

		fts_touch_mode[__i][SET_CUR_VALUE] = __v;
		if (__v > fts_touch_mode[__i][GET_MAX_VALUE])
			fts_touch_mode[__i][SET_CUR_VALUE] = fts_touch_mode[__i][GET_MAX_VALUE];
		else if (__v < fts_touch_mode[__i][GET_MIN_VALUE])
			fts_touch_mode[__i][SET_CUR_VALUE] = fts_touch_mode[__i][GET_MIN_VALUE];
	}

	fts_update_touchmode_data(fts_data);			/* blob 0x449c（唯一调用点） */
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

/* _b582-INTA：删除第二个 log-level 写点 —— 原 fts_touch_log_level_control()
 * { fts_debug_log_level = value; return 0; }（全树 0 引用，无原型）在本轮删除。
 * 证据：blob 全模块对模块级 debug_log_level（.data+0x0、4B、初值 3）只有**一个**
 * 写点 = fts_log_level_control（blob 0x4ee8, 212B，`str w19,[x20]`，
 * tools/_b582_log/evid_b582.txt §[4]：ldr(读)=825 / str(写)=1）。
 * ops 槽保持：hardware_operation.touch_log_level_control_v2 = fts_log_level_control
 * （blob 有的槽）；touch_log_level_control(v1) 槽仍为 NULL。 */

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
	/* _b581：A-74 续行② 面 —— blob 0x3e0 读 [ts_data+0xbe8]（gamemode 标志，
	 * 由 fts_update_touchmode_data 0x58fc 写入，6.18 同址成员 gamemode_enabled），
	 * 非 framework 判存；0x8E 的重开条件按 blob 用本地镜像标志。 */
	if (ts_data->gamemode_enabled) {
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

/* _b581：donor 桩 fts_game_mode_recovery 已删（blob 无此符号、无调用点）。 */

static void fts_init_touchmode_data(struct fts_ts_data *ts_data)
{
	struct fts_ts_platform_data *pdata = ts_data->pdata;	/* blob: ts_data->[0x38] */

	/* mode0/mode1：仅 GET_MAX=1 */
	fts_touch_mode[DATA_MODE_0][GET_MAX_VALUE] = 1;
	fts_touch_mode[DATA_MODE_1][GET_MAX_VALUE] = 1;

	/* mode2..mode5：默认值取自 pdata->touch_def_array[]，范围 1..5 */
	fts_touch_mode[DATA_MODE_2][SET_CUR_VALUE] = pdata->touch_def_array[0];
	fts_touch_mode[DATA_MODE_2][GET_CUR_VALUE] = pdata->touch_def_array[0];
	fts_touch_mode[DATA_MODE_2][GET_DEF_VALUE] = pdata->touch_def_array[0];
	fts_touch_mode[DATA_MODE_2][GET_MIN_VALUE] = 1;
	fts_touch_mode[DATA_MODE_2][GET_MAX_VALUE] = 5;

	fts_touch_mode[DATA_MODE_3][SET_CUR_VALUE] = pdata->touch_def_array[1];
	fts_touch_mode[DATA_MODE_3][GET_CUR_VALUE] = pdata->touch_def_array[1];
	fts_touch_mode[DATA_MODE_3][GET_DEF_VALUE] = pdata->touch_def_array[1];
	fts_touch_mode[DATA_MODE_3][GET_MIN_VALUE] = 1;
	fts_touch_mode[DATA_MODE_3][GET_MAX_VALUE] = 5;

	fts_touch_mode[DATA_MODE_4][SET_CUR_VALUE] = pdata->touch_def_array[2];
	fts_touch_mode[DATA_MODE_4][GET_CUR_VALUE] = pdata->touch_def_array[2];
	fts_touch_mode[DATA_MODE_4][GET_DEF_VALUE] = pdata->touch_def_array[2];
	fts_touch_mode[DATA_MODE_4][GET_MIN_VALUE] = 1;
	fts_touch_mode[DATA_MODE_4][GET_MAX_VALUE] = 5;

	fts_touch_mode[DATA_MODE_5][SET_CUR_VALUE] = pdata->touch_def_array[3];
	fts_touch_mode[DATA_MODE_5][GET_CUR_VALUE] = pdata->touch_def_array[3];
	fts_touch_mode[DATA_MODE_5][GET_DEF_VALUE] = pdata->touch_def_array[3];
	fts_touch_mode[DATA_MODE_5][GET_MIN_VALUE] = 1;
	fts_touch_mode[DATA_MODE_5][GET_MAX_VALUE] = 5;

	/* mode6（Expert_Mode） */
	fts_touch_mode[DATA_MODE_6][SET_CUR_VALUE] = 1;
	fts_touch_mode[DATA_MODE_6][GET_CUR_VALUE] = 1;
	fts_touch_mode[DATA_MODE_6][GET_DEF_VALUE] = 1;
	fts_touch_mode[DATA_MODE_6][GET_MIN_VALUE] = 1;
	fts_touch_mode[DATA_MODE_6][GET_MAX_VALUE] = 3;

	/* mode7（edge filter area） */
	fts_touch_mode[DATA_MODE_7][SET_CUR_VALUE] = 2;
	fts_touch_mode[DATA_MODE_7][GET_CUR_VALUE] = 2;
	fts_touch_mode[DATA_MODE_7][GET_DEF_VALUE] = 2;
	fts_touch_mode[DATA_MODE_7][GET_MIN_VALUE] = 2;
	fts_touch_mode[DATA_MODE_7][GET_MAX_VALUE] = 3;

	/* mode8（panel orientation）：SET/GET_CUR=0，GET_MAX=3 */
	fts_touch_mode[DATA_MODE_8][GET_MAX_VALUE] = 3;

	FTS_INFO("touchfeature value init done");
}

/* _b583-FTS（B11）：blob/IDA 逐字节收口 —— 三个 read 面均为**纯取缓存 + 无打印**：
 *   fts_panel_vendor_read  0xDFC4: CBZ fts_data → LDRB W0,[fts_data,#0xAF0]; RET
 *   fts_panel_display_read 0xE004: … [fts_data,#0xAF1]
 *   fts_panel_color_read   0xDFE4: … [fts_data,#0xAF2]
 * 0xAF0/0xAF1/0xAF2 = lockdown_info[0]/[1]/[2]（lockdown_info@0xAF0 由
 * fts_lockdown_info_read / fts_get_lockdown_information / fts_init_xiaomi_touchfeature_v3
 * 逐字节写入，见 _b583 blbscan 0xAF0..0xAF7 表）。
 * 树侧原有 ①"read info is %c"/"return info is %c" 打印（classify ③树）与 ②vendor 的
 * 0x71/0x46→0x46 现场推导（blob 无该分支，blob 直接回缓存值）⇒ 一并删除。 */
static u8 fts_panel_vendor_read(void)
{
	if (!fts_data)
		return 0;

	return fts_data->lockdown_info[0];
}

static u8 fts_panel_color_read(void)
{
	if (!fts_data)
		return 0;

	return fts_data->lockdown_info[2];
}

static u8 fts_panel_display_read(void)
{
	if (!fts_data)
		return 0;

	return fts_data->lockdown_info[1];
}

static char fts_touch_vendor_read(void)
{
	/* _b583-FTS（B11）：blob 0xE024 = `MOV W0,#0x33; RET`（无打印）⇒ 去树侧独有串。 */
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

/* _b571 fw_version 对齐（blob 0x3c44/220B） */
int fts_ic_fw_version(char *fw_version_buf)
{
	int ret = 0;
	u8 fwver = 0;

	if (!fts_data)
		return -1;
	mutex_lock(&fts_data->input_dev->mutex);
	ret = fts_read_reg(FTS_REG_FW_VER, &fwver);
	mutex_unlock(&fts_data->input_dev->mutex);
	if ((ret < 0) || (fwver == 0xFF) || (fwver == 0x00)) {
		memcpy(fw_version_buf, "get tp fw version fail!\n",
		       sizeof("get tp fw version fail!\n"));
	} else {
		snprintf(fw_version_buf, 64, "%02x\n", fwver);
	}
	return 0;
}

int fts_ic_self_test(char *type, int *result)
{
	int retval = 0;//0 invalid; 1 fail; 2 pass
	int ret = 0;
	int i = 0;
	struct fts_ts_data *ts_data = fts_data;

	/* _b582-INTD（B）：宿主链按 blob 收口 —— blob fts_ic_self_test 0x14dec-0x150e4
	 * （focaltech_core.c 侧；符号表 size 0x2f8）逐段：
	 *   0x14e0c-0x14e28 三点守卫（!fts_data / !fts_ftest / !fts_ftest->func(+0x3c8)）
	 *       → FTS_ERROR(.rodata.str1.1+0x460e = b'\0016[FTS_TS_E][%s:%d]: invalid params'，
	 *         行 2743) + 返回 -EINVAL（0x14e68 mov w0,#-0x16；__func__ 0x7b24
	 *         "fts_ic_self_test"）
	 *   0x14e2c ldrb w9,[fts_data+0x2d9]（= ts_data->suspended）非 0
	 *       → FTS_INFO(0x111c3 = b'\0016[FTS_TS_I][%s:%d]: In suspend, no test, return now'，
	 *         行 2748) + -EINVAL
	 *   0x14ea8-0x14ebc mutex_lock(&ts_data->input_dev->mutex)
	 *         （ldr x8,[fts_data+0x18] = input_dev；mutex @ +0x208）
	 *   0x14ec0 fts_irq_disable()
	 *   0x14ed4 ic_self_test_flag = 1（strb w9=1,[.bss+0x5b8]）
	 *   0x14ed8-0x14ef8 FTS_DEBUG("enter ic_self_test_flag %d", 1)（cmp w8,#4; b.lo，行 2756）
	 *   0x14efc fts_test_main_init()；失败 → FTS_TEST_ERROR(0x111f8 =
	 *         b'\0013[FTS_TS/E][TEST]%s:fts_test_main_init error.\n'，形如
	 *         focaltech_test.h:723 FTS_TEST_ERROR) ⇒ retval = 0（0x14f34 mov w22,wzr）
	 *         → 直落尾块（0x1509c），不跑 teardown（blob 同形，保留）
	 *   0x14f10-0x14f30 fts_test_get_testparam_from_ini("Conf_MultipleTest.ini")
	 *         （0xde6b = 实参串）；失败 → FTS_TEST_ERROR(0x633a =
	 *         b'\0013[FTS_TS/E][TEST]%s:get testparam fail\n') ⇒ retval = 0 → 尾块
	 *   0x14f3c-0x14fc8 分派（逐支先 strncmp 再判 func 槽非空）：
	 *         0x712b "short"/w2=5 → +0x40 short_test；0x261d "open"/w2=4 → +0x38 open_test；
	 *         0xcb36 "i2c"/w2=3 → +0x48 spi_test；无匹配 → retval = 0；retval = w0(被调)
	 *   0x14fcc fts_test_main_exit()
	 *   0x14fe8-0x1506c 厂测侧 fts_free_test_memory 全内联体（INT-A 已落，见下）
	 *   0x15070-0x15094 fts_ftest->func->free_item_data(fts_ftest)（func+0x78）
	 *   0x15098 enter_work_mode()
	 *   尾块 0x1509c-0x150e0：fts_irq_enable() → mutex_unlock(input_dev+0x208)
	 *         → ic_self_test_flag = 0（strb wzr,[.bss+0x5b8]）
	 *         → FTS_DEBUG("enter ic_self_test_flag %d", 0)（行 2789）
	 *         → *result = retval（0x150bc str w22,[x19]）→ return 0（mov w0,wzr）
	 * 成对性：mutex_lock↔mutex_unlock、fts_irq_disable↔fts_irq_enable 均在单一尾块闭合
	 * （含两条错误路径）；分配↔释放 = fts_test_main_init ↔ fts_test_main_exit+
	 * fts_test_malloc_free_thr(false)+vfree×3 —— 注意 blob 的两条前置错误路径
	 * 直落尾块、**不**跑释放（厂测固有：main_init 失败即未完成分配；按 blob 保留）。
	 * _b582-INTE 收口：原 INT-D 记的「树侧偏离（唯一）：额外写 ic_in_selftest」已按 blob
	 * 删除 —— blob .bss+0x5b8（ic_self_test_flag）全模块唯一写点即本宿主链；blob
	 * fts_set_cur_value (0x45f0,1244B) 对 .bss+0x5b8 与 focal_get_ic_self_test_mode 均无
	 * 引用（后者全模块 CALL26 仅 fts_htc_ic_setModeValue+0xcc = 0x3344 一处，且该函数
	 * 以「tp is in open/short test」串自门）。 */

	if (!ts_data || !fts_ftest || !fts_ftest->func) {
		FTS_ERROR("invalid params");
		return -EINVAL;
	}
	if (ts_data->suspended) {
		FTS_INFO("In suspend, no test, return now");
		return -EINVAL;
	}

	mutex_lock(&ts_data->input_dev->mutex);
	fts_irq_disable();
	ic_self_test_flag = 1;
	FTS_DEBUG("enter ic_self_test_flag %d", ic_self_test_flag);

	ret = fts_test_main_init();
	if (ret < 0) {
		pr_err("[FTS_TS/E][TEST]%s:" "fts_test_main_init error." "\n", __func__);
		goto selftest_out;
	}
	ret = fts_test_get_testparam_from_ini("Conf_MultipleTest.ini");
	if (ret < 0) {
		pr_err("[FTS_TS/E][TEST]%s:" "get testparam fail" "\n", __func__);
		goto selftest_out;
	}

	if (!strncmp("short", type, 5) && fts_ftest->func->short_test) {
		retval = fts_ftest->func->short_test();
	} else if (!strncmp("open", type, 4) && fts_ftest->func->open_test) {
		retval = fts_ftest->func->open_test();
	} else if (!strncmp("i2c", type, 3)) {
		retval = fts_ftest->func->spi_test();
	} else {
		retval = 0;
	}

	fts_test_main_exit();

	/* _b582-INTA：teardown 按 blob（③报告 §6.1，blob fts_ic_self_test 0x14fe8-0x1506c
	 * = 厂测侧 fts_free_test_memory 全内联体）——分派之后依次：
	 *   0x14fe8/0x14ff0  fts_test_malloc_free_thr(fts_ftest, false)（w1 = false）
	 *   0x14ff4-0x15038  for (i = 0; i < testdata.item_count; i++)
	 *                      if (info[i].data) { vfree(info[i].data); info[i].data = NULL; }
	 *                    （count @fts_ftest+0x3d8 = testdata.item_count；数组 @+0x408、
	 *                      步长 0x40 = sizeof(struct item_info)；编译器另加 i!=0x20 越界守卫）
	 *   0x1503c-0x15048  if (buffer@0x90) { vfree(buffer); buffer = NULL; }
	 *   0x1504c-0x15068  vfree(*(fts_ftest+0xbf0)); *(fts_ftest+0xbf0) = NULL;
	 *                    ⚠️ 该槽 blob 全 ko 无写入点（恒 NULL，只 vfree(NULL)+置 NULL）
	 *                    ⇒ 树侧保持为洞：core.h 不为其造成员，此处借用 ③ 的保位洞
	 *                    `reserved_bf0`（u64，blob 无对应语义成员）承载同形态调用。
	 * 0x14fd0/0x15054 的两条 TEST 族 FUNC 打印（__func__ = "fts_free_test_memory"）
	 * 因树侧以「内联体」形态落地而无同名函数可打印，保持 INT-A 决定：另账（④ 记录）。 */
	fts_test_malloc_free_thr(fts_ftest, false);

	for (i = 0; i < fts_ftest->testdata.item_count; i++) {
		if (fts_ftest->testdata.info[i].data) {
			vfree(fts_ftest->testdata.info[i].data);
			fts_ftest->testdata.info[i].data = NULL;
		}
	}

	if (fts_ftest->buffer) {
		vfree(fts_ftest->buffer);
		fts_ftest->buffer = NULL;
	}

	vfree((void *)fts_ftest->reserved_bf0[0]);
	fts_ftest->reserved_bf0[0] = 0;

	/* _b582-INTD（B）：0x15070-0x15094 = fts_ftest->func->free_item_data(fts_ftest)
	 * （x8 = fts_ftest->func(+0x3c8)；x8 = [x8+0x78]；x0 = fts_ftest；无判空，
	 * 仅 KCFI 检查 ldur w16,[x8,#-4]）。树侧 test_funcs 槽位同布局
	 * （focaltech_test_ini.h:628/633 `_Static_assert(... free_item_data) == 0x78`）。 */
	fts_ftest->func->free_item_data(fts_ftest);

	/* _b582-INTD（B）：0x15098 = enter_work_mode() */
	enter_work_mode();

selftest_out:
	/* _b582-INTD（B）：尾块（0x1509c-0x150e0，成功与两条错误路径共用）——
	 * fts_irq_enable() → mutex_unlock() → ic_self_test_flag = 0 → 调试打印 → 结果回填。 */
	fts_irq_enable();
	mutex_unlock(&ts_data->input_dev->mutex);
	ic_self_test_flag = 0;
	FTS_DEBUG("enter ic_self_test_flag %d", ic_self_test_flag);

	*result = retval;

	return 0;
}


/* _b582-SLEEP：blob 入口形态（0x25a0 单函数）——符号 GLOBAL，取址存
 * hardware_operation+0xb0（blob 0x390c/0x3914 stp xzr,x9,[x8,#0xa8]，即
 * ic_resume_suspend 槽），由 xiaomi_touch 侧 schedule_resume_suspend_work_common
 * 回调（树侧 tpdbg_shutdown/tpdbg_suspend 发起）。blob 只测 w0 低位
 * （0x25cc: tbz w0,#0x0）选 resume/suspend 半，w1（gesture_type）全程未读，
 * 返回恒 0（0x290c: mov w0,wzr; ret）。 */
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
    /* _b583-FTS（C）：blob 0x2DFC 逐点（callface only-blob={_printk:2} 的闭口）——
     *   0x2E1C cmp w8,#4; b.hs → _printk(0x2E4C = .rodata.str1.1+0x257D
     *     b"6[FTS_TS_D][%s:%d]: [converhex] data before hex is: %d
"，L3741，实参=data)；
     *   0x2E28 str w20,[x19]（小端四字节 = 树侧逐字节分解，同义）；
     *   0x2E2C 再次 cmp #4 → _printk(0x2E6C = +0x10F3D
     *     b"6[FTS_TS_D][%s:%d]: [converhex] hex after hex is: %s
"，L3746，实参=hex)。 */
    FTS_DEBUG("[converhex] data before hex is: %d\n", data);
    hex[0] = (value & 0xFF);
    hex[1] = ((value >> 8) & 0xFF);
    hex[2] = ((value >> 16) & 0xFF);
    hex[3] = ((value >> 24) & 0xFF);
    FTS_DEBUG("[converhex] hex after hex is: %s\n", hex);
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

/* _b582-INTE：blob 忠实形态重建 —— blob fts_htc_ic_setModeValue (0x3278, 668B)。
 * 逐段：ldrh w21,[x0,#0x2]=mode / ldrh w19,[x0,#0x4]=data_len / add x20,x0,#0x8=data_buf
 *   行 3968 FTS_INFO("mode: %d", mode)                串 .rodata.str1.1+0xc07（门 lv>=3）
 *   行 3971 FTS_INFO("value[i:%d]:%x", i, value[i])   串 +0x563b；blob 取 [x20+i*4]（32 位步长）
 *   if (mode <= 0xbb7 = THP_IC_CMD_BASE-1) → 行 3974 FTS_ERROR("mode is error!!\n")
 *       （串 +0x37b0；该 printk 只有 format/__func__/line，**无可变参**）→ return -1
 *   if (focal_get_ic_self_test_mode()) → 行 3980 FTS_INFO("tp is in open/short test")
 *       （串 +0x685f，同样无可变参）→ return 0
 *   htc_ic_mode = mode（blob str w21,[.bss+0x8]）
 *   switch(mode)：blob .rodata 跳表 [49]（0x0..0x30，索引 = mode - IC_MODE_0(=0xbb9)）；
 *     命中即**尾调用** fts_thp_ic_write_interfaces(cmd, data_buf, data_len)（blob 0x34ec，
 *     函数返回被调返回值）；IC_MODE_20 = 空 case（直跳尾声 → return 0）；表外（> IC_MODE_48）
 *     → b.hi 直返 0。常量 = 跳表项逐项解码。 */
int fts_htc_ic_setModeValue(common_data_t *common_data)
{
	int mode = common_data->mode;		/* blob: ldrh w21,[x0,#0x2] */
	int value_len = common_data->data_len;	/* blob: ldrh w19,[x0,#0x4] */
	s32 *value = common_data->data_buf;	/* blob: add x20,x0,#0x8 */
	int i = 0;

	FTS_INFO("mode: %d", mode);
	for (i = 0; i < value_len; i++)
		FTS_INFO("value[i:%d]:%x", i, value[i]);

	if (mode < THP_IC_CMD_BASE) {
		FTS_ERROR("mode is error!!\n");
		return -1;
	}

	if (focal_get_ic_self_test_mode()) {
		FTS_INFO("tp is in open/short test");
		return 0;
	}

	htc_ic_mode = mode;

	switch (mode) {
	case IC_MODE_0:
		return fts_thp_ic_write_interfaces(SET_IDLE_THD_TYPE, value, value_len);
	case IC_MODE_1:
		return fts_thp_ic_write_interfaces(SET_IDLE_RATE_TYPE, value, value_len);
	case IC_MODE_2:
	case IC_MODE_3:
	case IC_MODE_4:
		return fts_thp_ic_write_interfaces(SET_NULL_MODE_TYPE, value, value_len);
	case IC_MODE_5:
		return fts_thp_ic_write_interfaces(SET_FOD_EN_TYPE, value, value_len);
	case IC_MODE_6:
		return fts_thp_ic_write_interfaces(SET_REPORT_RATE_TYPE, value, value_len);
	case IC_MODE_7:
		return fts_thp_ic_write_interfaces(SET_SCAN_FREQ_TYPE, value, value_len);
	case IC_MODE_8:
		return fts_thp_ic_write_interfaces(SET_SCAN_FREQ_HOPPING_EN_TYPE, value, value_len);
	case IC_MODE_9:
		return fts_thp_ic_write_interfaces(SET_AFE_EN_TYPE, value, value_len);
	case IC_MODE_10:
		return fts_thp_ic_write_interfaces(SET_MC_SCAN_EN_TYPE, value, value_len);
	case IC_MODE_11:
		return fts_thp_ic_write_interfaces(SET_SC_SCAN_EN_TYPE, value, value_len);
	case IC_MODE_12:
		return fts_thp_ic_write_interfaces(SET_MC_CALIBRATION_EN_TYPE, value, value_len);
	case IC_MODE_13:
		return fts_thp_ic_write_interfaces(SET_SC_CALIBRATION_EN_TYPE, value, value_len);
	case IC_MODE_14:
		return fts_thp_ic_write_interfaces(SET_NULL_MODE_TYPE, value, value_len);
	case IC_MODE_15:
		return fts_thp_ic_write_interfaces(SET_INT_STATE_TYPE, value, value_len);
	case IC_MODE_16:
		return fts_thp_ic_write_interfaces(SET_BASE_REFRESH_EN_TYPE, value, value_len);
	case IC_MODE_17:
		return fts_thp_ic_write_interfaces(SET_FRAME_DATA_TYPE_TYPE, value, value_len);
	case IC_MODE_18:
		return fts_thp_ic_write_interfaces(SET_GAME_MODE_EN_TYPE, value, value_len);
	case IC_MODE_19:
		return fts_thp_ic_write_interfaces(SET_CHARGING_STATUS_EN_TYPE, value, value_len);
	case IC_MODE_20:
		break;
	case IC_MODE_21:
		return fts_thp_ic_write_interfaces(SET_GESTURE_EN_TYPE, value, value_len);
	case IC_MODE_22:
		return fts_thp_ic_write_interfaces(SET_CHLICK_GESTURE_EN_TYPE, value, value_len);
	case IC_MODE_23:
		return fts_thp_ic_write_interfaces(SET_DOUBLE_CHLICK_EN_TYPE, value, value_len);
	case IC_MODE_24:
		return fts_thp_ic_write_interfaces(SET_FLAG_BUF_TYPE, value, value_len);
	case IC_MODE_25:
	case IC_MODE_26:
	case IC_MODE_27:
	case IC_MODE_28:
	case IC_MODE_29:
	case IC_MODE_30:
		return fts_thp_ic_write_interfaces(SET_NULL_MODE_TYPE, value, value_len);
	case IC_MODE_31:
		return fts_thp_ic_write_interfaces(SET_IC_RUN_STEP_TYPE, value, value_len);
	case IC_MODE_32:
		return fts_thp_ic_write_interfaces(SET_NULL_MODE_TYPE, value, value_len);
	case IC_MODE_33:
		return fts_thp_ic_write_interfaces(SET_IC_LOG_LEVEL_TYPE, value, value_len);
	case IC_MODE_34:
		return fts_thp_ic_write_interfaces(SET_IC_CALIBRATEION_TYPE, value, value_len);
	case IC_MODE_35:
		return fts_thp_ic_write_interfaces(SET_IC_SELF_TEST_TYPE, value, value_len);
	case IC_MODE_36:
		return fts_thp_ic_write_interfaces(SET_IC_SOFT_RETEST_TYPE, value, value_len);
	case IC_MODE_37:
		return fts_thp_ic_write_interfaces(SET_SCAN_SLOPE_TYPE, value, value_len);
	case IC_MODE_38:
		return fts_thp_ic_write_interfaces(SET_SCAN_VOLTAGE_TYPE, value, value_len);
	case IC_MODE_39:
		return fts_thp_ic_write_interfaces(SET_SCAN_NUM_TYPE, value, value_len);
	case IC_MODE_40:
		return fts_thp_ic_write_interfaces(SET_SCAN_FREQ_NUM_TYPE, value, value_len);
	case IC_MODE_41:
		return fts_thp_ic_write_interfaces(SET_IC_WORK_MODE_TYPE, value, value_len);
	case IC_MODE_42:
		return fts_thp_ic_write_interfaces(SET_FILTER_LEVEL_TYPE, value, value_len);
	case IC_MODE_43:
		return fts_thp_ic_write_interfaces(SET_RAW_TYPE_TYPE, value, value_len);
	case IC_MODE_44:
		return fts_thp_ic_write_interfaces(SET_OPEN_TRANSPORT_MODE_TYPE, value, value_len);
	case IC_MODE_45:
		return fts_thp_ic_write_interfaces(SET_CRC_EN_TYPE, value, value_len);
	case IC_MODE_46:
	case IC_MODE_47:
	case IC_MODE_48:
		return fts_thp_ic_write_interfaces(0x2, value, value_len);
	}

	return 0;
}

/* _b582-INTE：blob 忠实形态重建 —— blob fts_htc_ic_getModeValue (0x3518, 524B)。
 * 逐段：ldrh w21,[x0,#0x2]=mode / ldrh w19,[x0,#0x4]=data_len / add x20,x0,#0x8=data_buf
 *   行 4148 FTS_INFO("mode:%d, value:%s, value_len:%d", mode, value, value_len)
 *       串 +0x25b6；实参 x3=mode、x4=[x0+0x8]（指针，按 %s 传入）、w5=data_len（门 lv>=3）
 *   if (mode < THP_IC_CMD_BASE) → 行 4150 FTS_ERROR("mode is error!!\n") → return -1
 *   **无** focal_get_ic_self_test_mode 门 —— blob 0x3588 b.ls 直落 0x3550 错误支，
 *   全模块对 focal_get_ic_self_test_mode 的 CALL26 只有 setModeValue+0xcc 一处。
 *   htc_ic_mode = mode
 *   switch(mode)：跳表 [50]（.rodata+0x31..0x62，IC_MODE_0..IC_MODE_49）
 *     → fts_thp_ic_read_interfaces(cmd, data_buf, data_len)（blob 0x3700）；IC_MODE_20 = 空 case；
 *     每支调用后 mov w0,wzr ⇒ **恒 return 0**。 */
int fts_htc_ic_getModeValue(common_data_t *common_data)
{
	int mode = common_data->mode;		/* blob: ldrh w21,[x0,#0x2] */
	int value_len = common_data->data_len;	/* blob: ldrh w19,[x0,#0x4] */
	s32 *value = common_data->data_buf;	/* blob: add x20,x0,#0x8 */

	FTS_INFO("mode:%d, value:%s, value_len:%d", mode, (char *)value, value_len);

	if (mode < THP_IC_CMD_BASE) {
		FTS_ERROR("mode is error!!\n");
		return -1;
	}

	htc_ic_mode = mode;

	switch (mode) {
	case IC_MODE_0:
		fts_thp_ic_read_interfaces(SET_IDLE_THD_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_1:
		fts_thp_ic_read_interfaces(SET_IDLE_RATE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_2:
	case IC_MODE_3:
	case IC_MODE_4:
		fts_thp_ic_read_interfaces(SET_NULL_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_5:
		fts_thp_ic_read_interfaces(SET_FOD_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_6:
		fts_thp_ic_read_interfaces(SET_REPORT_RATE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_7:
		fts_thp_ic_read_interfaces(SET_SCAN_FREQ_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_8:
		fts_thp_ic_read_interfaces(SET_SCAN_FREQ_HOPPING_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_9:
		fts_thp_ic_read_interfaces(SET_AFE_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_10:
		fts_thp_ic_read_interfaces(SET_MC_SCAN_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_11:
		fts_thp_ic_read_interfaces(SET_SC_SCAN_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_12:
		fts_thp_ic_read_interfaces(SET_MC_CALIBRATION_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_13:
		fts_thp_ic_read_interfaces(SET_SC_CALIBRATION_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_14:
		fts_thp_ic_read_interfaces(SET_NULL_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_15:
		fts_thp_ic_read_interfaces(SET_INT_STATE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_16:
		fts_thp_ic_read_interfaces(SET_BASE_REFRESH_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_17:
		fts_thp_ic_read_interfaces(SET_FRAME_DATA_TYPE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_18:
		fts_thp_ic_read_interfaces(SET_GAME_MODE_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_19:
		fts_thp_ic_read_interfaces(SET_CHARGING_STATUS_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_20:
		break;
	case IC_MODE_21:
		fts_thp_ic_read_interfaces(SET_GESTURE_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_22:
		fts_thp_ic_read_interfaces(SET_CHLICK_GESTURE_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_23:
		fts_thp_ic_read_interfaces(SET_DOUBLE_CHLICK_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_24:
		fts_thp_ic_read_interfaces(SET_FLAG_BUF_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_25:
	case IC_MODE_26:
	case IC_MODE_27:
	case IC_MODE_28:
	case IC_MODE_29:
	case IC_MODE_30:
		fts_thp_ic_read_interfaces(SET_NULL_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_31:
		fts_thp_ic_read_interfaces(SET_IC_RUN_STEP_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_32:
		fts_thp_ic_read_interfaces(SET_NULL_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_33:
		fts_thp_ic_read_interfaces(SET_IC_LOG_LEVEL_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_34:
		fts_thp_ic_read_interfaces(SET_IC_CALIBRATEION_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_35:
		fts_thp_ic_read_interfaces(SET_IC_SELF_TEST_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_36:
		fts_thp_ic_read_interfaces(SET_IC_SOFT_RETEST_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_37:
		fts_thp_ic_read_interfaces(SET_SCAN_SLOPE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_38:
		fts_thp_ic_read_interfaces(SET_SCAN_VOLTAGE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_39:
		fts_thp_ic_read_interfaces(SET_SCAN_NUM_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_40:
		fts_thp_ic_read_interfaces(SET_SCAN_FREQ_NUM_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_41:
		fts_thp_ic_read_interfaces(SET_IC_WORK_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_42:
		fts_thp_ic_read_interfaces(SET_FILTER_LEVEL_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_43:
		fts_thp_ic_read_interfaces(SET_RAW_TYPE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_44:
		fts_thp_ic_read_interfaces(SET_OPEN_TRANSPORT_MODE_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_45:
		fts_thp_ic_read_interfaces(SET_CRC_EN_TYPE, (u8 *)value, value_len);
		break;
	case IC_MODE_46:
	case IC_MODE_47:
	case IC_MODE_48:
		fts_thp_ic_read_interfaces(0x2, (u8 *)value, value_len);
		break;
	case IC_MODE_49:
		fts_thp_ic_read_interfaces(SET_TOUCH_IC_INFO_TYPE, (u8 *)value, value_len);
		break;
	}

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
	/* _b582-INTD（A5-②）：blob fts_init_xiaomi_touchfeature_v3 0x37a4-0x37c8 的
	 * config_file_name 实参 = .rodata.str1.1+0xe473 b'rodin_fts_thp_config.ini'
	 * （19B+NUL）；树原串 "pandora_focal_thp_config.ini" 全 ko 0 命中 ⇒ 按 blob 换名。 */
	memcpy(hardware_param.config_file_name, "rodin_fts_thp_config.ini", strlen("rodin_fts_thp_config.ini"));
	memset(hardware_param.driver_version, 0, 64);
	/* _b582-INTD（A5-②）：blob 同位实参 = .rodata.str1.1+0x9b27 b'FT3683:2025.04.23-001'
	 * （0x37bc-0x37e0 拷贝 24B；树 FOCAL_DRIVER_VERSION = "FT3383-2025.07.04-01" 全 ko
	 * 0 命中）⇒ 按 blob 落串（core.h:130 的宏保持不动，改为未使用）。 */
	memcpy(hardware_param.driver_version, "FT3683:2025.04.23-001", strlen("FT3683:2025.04.23-001"));
	/* save lockdown type: u8 */
	/* init lockdown_info/fw_version in fw_upgrade_work for the right data*/
	fts_lockdown_info_read(hardware_param.lockdown_info);
	fts_ic_fw_version(hardware_param.fw_version);
}


/* ==================== _b571 缺件重建（blob 机器码）插入段 ==================== */

/* _b583-FTS（A7-a）：原 fts_seed_touch_mode_mirror() 已删 —— 两层确认：
 *   ① blob 符号面：.symtab **无** fts_seed_touch_mode_mirror（亦无 UND 项）；
 *   ② blob 串面：.rodata.str1.1 / .strings 全 ko 无该串（rostr_ref NO-HIT）；
 *   ③ 全树引用面：core.c 4 个调用点 + 定义 = 本函数删除面（本批同改）。
 * 框架 mode 面自洽性：blob 的 fts_get_mode_value/get_mode_all/reset_mode/
 * update_touchmode_data 都**直接**读本地 fts_touch_mode[][]（0x4AD0/0x4B94/0x4C88/0x5754
 * 逐点实证，无 driver_get_touch_mode_common 调用）；写侧由框架 → ops.set_cur_value →
 * fts_set_cur_value + fts_update_touchmode_data 的 GET_CUR=SET_CUR 回路维护 ⇒ 删 seed 后
 * 与 blob 同形，A-74/A-80 已收口的框架 store_touch_log_level/jump table 面不受影响。 */

static void fts_get_mode_value(common_data_t *common_data)
{
	int mode = common_data->mode;			/* blob: ldrh [x0,#0x2] */
	int value_type = common_data->data_buf[0];	/* blob: ldr  [x0,#0x8] */

	if (mode >= FTS_TOUCH_MODE_MAX) {
		/* blob 串面为 "mode:% d don't support"（% 后带空格，vendor 原文如此） */
		if (fts_debug_log_level)
			FTS_ERROR("mode:% d don't support", mode);
		return;
	}

	if (value_type < 0 || value_type >= FTS_TOUCH_MODE_VALUE_NUM) {
		/* _b583-FTS（A4）：blob 0x4AEC `cmp w4,#5; b.hi 0x4B8C` = brk（UB 界，无打印）
		 * ⇒ 树侧保留等价静默护栏、删除树侧独有串 "value_type(%d) out of range"
		 * （classify ③树）与 fts_seed_touch_mode_mirror 调用（blob 全 ko 无该符号/串，
		 * 见 A7-a）。 */
		return;
	}

	common_data->data_buf[0] = fts_touch_mode[mode][value_type];

	if (fts_debug_log_level >= 3)
		FTS_INFO("mode:%d, value_type:%d, value:%d",
			 mode, value_type, common_data->data_buf[0]);
}

static void fts_get_mode_all(common_data_t *common_data)
{
	int mode = common_data->mode;			/* blob: ldrh [x0,#0x2] */
	int *val = (int *)common_data->data_buf;

	if (mode < FTS_TOUCH_MODE_MAX) {
		/* _b583-FTS（A4）：blob 0x4B94 无镜像 seed（callface only-tree=
		 * {'fts_seed_touch_mode_mirror':1}；blob 全 ko 无该符号/串）⇒ 删。 */
		val[0] = fts_touch_mode[mode][GET_CUR_VALUE];	/* blob [x9,#0x4] → [x0,#0x8]  */
		val[1] = fts_touch_mode[mode][GET_DEF_VALUE];	/* blob [x8,#0x8] → [x0,#0xc]  */
		val[2] = fts_touch_mode[mode][GET_MIN_VALUE];	/* blob [x8,#0xc] → [x0,#0x10] */
		val[3] = fts_touch_mode[mode][GET_MAX_VALUE];	/* blob [x8,#0x10] → [x0,#0x14] */
	} else {
		if (fts_debug_log_level)
			FTS_ERROR("mode:%d don't support", mode);
	}

	if (fts_debug_log_level >= 3)
		FTS_INFO("mode:%d, value:%d:%d:%d:%d",
			 mode, val[0], val[1], val[2], val[3]);
}

static void fts_reset_mode(common_data_t *common_data)
{
	int mode = common_data->mode;		/* blob: ldrh [x0,#0x2] */
	int i;

	/* _b583-FTS（A4）：blob 0x4C88 无镜像 seed（callface only-tree=
	 * {'fts_seed_touch_mode_mirror':1}；blob 无该符号/串）⇒ 删。 */

	if (mode == DATA_MODE_0) {
		/* 全量复位：mode0..7 的 SET_CUR 归位到 GET_DEF（blob 展平 8 组） */
		for (i = DATA_MODE_0; i <= DATA_MODE_7; i++)
			fts_touch_mode[i][SET_CUR_VALUE] = fts_touch_mode[i][GET_DEF_VALUE];

		fts_data->gamemode_enabled = false;	/* blob [ts_data+0xbe8] = 0 */
		fts_data->is_expert_mode = false;	/* blob [ts_data+0xbea] = 0 */
		fts_write_reg(0x8E, 0);			/* 0x8E：game idle high refresh 关 */
	} else if (mode <= DATA_MODE_8) {
		fts_touch_mode[mode][SET_CUR_VALUE] = fts_touch_mode[mode][GET_DEF_VALUE];
	} else {
		if (fts_debug_log_level)
			FTS_ERROR("mode:%d don't support", mode);
	}

	if (fts_debug_log_level >= 3)
		FTS_INFO("mode:%d reset", mode);

	fts_update_touchmode_data(fts_data);	/* blob 0x4d64: ldr x0,[fts_data]; bl */
}

static int fts_palm_sensor_write(int on)
{
	int ret = 0;

	if (!fts_data)
		return -EINVAL;

	fts_data->palm_status = on;		/* blob: str w0, [x8,#0xbd8] */

	if (fts_data->suspended)		/* blob: ldrb [x8,#0x2d9] */
		return 0;

	if (IS_ERR(fts_data)) {
		ret = -EINVAL;
		if (fts_debug_log_level)
			FTS_ERROR("set palm sensor cmd failed: %d\n", on);
	} else {
		ret = fts_write_reg(0x9A, on ? 5 : 0);	/* 0x9A：骨架无宏 [TODO-VERIFY] */
		if (ret < 0) {
			if (fts_debug_log_level)
				FTS_ERROR("Set palm sensor switch failed!\n");
		} else if (fts_debug_log_level >= 3) {
			FTS_INFO("Set palm sensor switch: %d\n", on != 0);
		}
	}

	if (!fts_data->palm_status) {
		if (fts_debug_log_level >= 3)
			FTS_INFO("disable palm reg and update palm_status_value for palm_sensor node :%d", 0);
		update_palm_sensor_value_common(0);
	}

	return ret;
}

static int fts_log_level_control(int value)
{
	if (!fts_data) {
		if (fts_debug_log_level)
			FTS_ERROR("fts data is null");
		return -1;
	}

	if (fts_debug_log_level >= 3)
		FTS_INFO("debug level: %d", value);

	fts_debug_log_level = value;

	if (!fts_scp_tp_mistouch_close)
		fts_scp_tp_ipi_send(6, value, 0, 0);

	if (fts_debug_log_level >= 3)		/* blob: 重读（=value）后 cmp #3 */
		FTS_INFO("scp set log level = %d", value);

	return value;
}

static int fts_set_thermal_temp(int temp, bool force)
{
	int temp0 = 0;
	int ret;

	if (force) {
		temp0 = get_bms_temp_common();
		if (abs(temp0) >= INVAILD_TEMPERATURE)	/* blob: abs > 999（0x3e7）→ -1 */
			return -1;

		temp = (temp0 + 5) / 10;	/* Rounding, in degrees Celsius */
		add_common_data_to_buf_common(0, SET_CUR_VALUE, DATA_MODE_156, 1, &temp);
	}

	if (fts_debug_log_level >= 4)
		FTS_DEBUG("temp: %d", temp);

	ret = fts_write_reg(SET_TEMPERATURE_STATUS_EN_TYPE, temp);	/* 0x97 */
	if (ret < 0) {
		if (fts_debug_log_level)
			FTS_ERROR("failed send temp:%d", temp);
	}

	return ret;
}

static void fts_touch_dfs_test(int value)
{
	switch (value) {
	case TOUCH_EVENT_TRANSFER_ERR:
		xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_TRANSFER_ERR, 0,
						       "TpTransferErr", "focal");
		break;
	case TOUCH_EVENT_FWLOAD_ERR:
		xiaomi_touch_mievent_report_str_common(TOUCH_EVENT_FWLOAD_ERR, 0,
						       "TpFirmwareLoadFail", "focal");
		break;
	case TOUCH_EVENT_PARAM_ERR:
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_PARAM_ERR, 0,
						       "TpParamParseFail", "focal",
						       ERROR_GPIO_REQUEST);
		break;
	case TOUCH_EVENT_OPENTEST_FAIL:
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_OPENTEST_FAIL, 0,
						       "TpOpenTestFail", "focal", 0);
		break;
	case TOUCH_EVENT_SHORTTEST_FAIL:
		xiaomi_touch_mievent_report_int_common(TOUCH_EVENT_SHORTTEST_FAIL, 0,
						       "TpShortTestFail", "focal", 0);
		break;
	default:
		if (fts_debug_log_level)
			FTS_ERROR("don't support touch dfs test\n");
		break;
	}
}

static void fts_update_touchmode_data(struct fts_ts_data *ts_data)
{
	u8 cmd[7] = {0xC1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int mode0_changed = 0;
	int mode8;
	int val;
	int ret;
	int i;

#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
	if (ts_data && ts_data->pm_suspend) {		/* blob: ldrb [x19,#0x2d8] */
		if (fts_debug_log_level)
			FTS_ERROR("SYSTEM is in suspend mode, don't set touch mode data");
		return;
	}
#endif

	/* _b583-FTS（A4）：blob 0x5754 无镜像 seed（callface only-tree=
	 * {'fts_seed_touch_mode_mirror':1}；blob 无该符号/串）⇒ 删。 */

	pm_stay_awake(ts_data->dev);			/* blob: ldr x0,[x19,#0x10] */
	mutex_lock(&ts_data->cmd_update_mutex);		/* blob: x20 = x19+0xba8 */

	/* ---- 7 字节 0xC1 命令组装（blob 0x57dc..0x58c4） ---- */
	cmd[1] = (u8)fts_touch_mode[DATA_MODE_0][SET_CUR_VALUE];
	cmd[2] = (u8)(fts_touch_mode[DATA_MODE_1][SET_CUR_VALUE] ? 30 : 3);

	if (ts_data->is_expert_mode) {			/* blob: ldrb [x19,#0xbea] */
		val = fts_touch_mode[DATA_MODE_6][SET_CUR_VALUE];
		if (val < 1 || val > 4) {		/* blob: (val-1)*4 ≤ 0xc 界限 */
			/* _b583-FTS（A4）：blob 0x5988 起只有 clamp（val=1），**无**打印
			 * （树侧独有串 "expert mode value(%d) out of range"，classify ③树；
			 *  callface only-tree={_printk:1}）⇒ 删打印、保留 clamp。 */
			val = 1;
		}
		cmd[3] = (u8)ts_data->pdata->touch_expert_array[(val - 1) * 4 + 0];
		cmd[4] = (u8)ts_data->pdata->touch_expert_array[(val - 1) * 4 + 1];
		cmd[5] = (u8)ts_data->pdata->touch_expert_array[(val - 1) * 4 + 2];
		cmd[6] = (u8)ts_data->pdata->touch_expert_array[(val - 1) * 4 + 3];
	} else {
		val = fts_touch_mode[DATA_MODE_3][SET_CUR_VALUE];
		if (val >= 1 && val <= 5)
			cmd[3] = (u8)ts_data->pdata->touch_range_array[val - 1];
		val = fts_touch_mode[DATA_MODE_2][SET_CUR_VALUE];
		if (val >= 1 && val <= 5)
			cmd[4] = (u8)ts_data->pdata->touch_range_array[val - 1];
		val = fts_touch_mode[DATA_MODE_4][SET_CUR_VALUE];
		if (val >= 1 && val <= 5)
			cmd[5] = (u8)ts_data->pdata->touch_range_array[val - 1];
		val = fts_touch_mode[DATA_MODE_5][SET_CUR_VALUE];
		if (val >= 1 && val <= 5)
			cmd[6] = (u8)ts_data->pdata->touch_range_array[val - 1];
	}

	ret = fts_write(cmd, sizeof(cmd));
	if (ret < 0) {
		if (fts_debug_log_level)
			FTS_ERROR("write game mode parameter failed\n");
	} else {
		if (fts_debug_log_level >= 3)
			FTS_INFO("update game mode cmd: %02X,%02X,%02X,%02X,%02X,%02X,%02X",
				 cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);

		/* mode0 变化 → 刷 gamemode 标志（blob 0x58e4-0x58fc，先比较后同步） */
		mode0_changed = (fts_touch_mode[DATA_MODE_0][GET_CUR_VALUE] !=
				 fts_touch_mode[DATA_MODE_0][SET_CUR_VALUE]);
		if (mode0_changed)
			ts_data->gamemode_enabled =
				(fts_touch_mode[DATA_MODE_0][SET_CUR_VALUE] != 0);

		/* mode0..6: GET_CUR = SET_CUR（blob 0x5908-0x5938，7 组） */
		for (i = DATA_MODE_0; i <= DATA_MODE_6; i++)
			fts_touch_mode[i][GET_CUR_VALUE] =
				fts_touch_mode[i][SET_CUR_VALUE];
	}

	/* ---- 方向寄存器 0x8C（mode8；blob 0x5944-0x59dc） ---- */
	mode8 = fts_touch_mode[DATA_MODE_8][SET_CUR_VALUE];
	if ((mode8 != fts_touch_mode[DATA_MODE_8][GET_CUR_VALUE]) || mode0_changed) {
		val = mode8;
		if (mode8 == PANEL_ORIENTATION_DEGREE_0 ||
		    mode8 == PANEL_ORIENTATION_DEGREE_180) {
			val = ORIENTATION_0_OR_180;
		} else if (mode8 == PANEL_ORIENTATION_DEGREE_270) {
			val = ts_data->gamemode_enabled ? GAME_ORIENTATION_270 :
							  NORMAL_ORIENTATION_270;
		} else if (mode8 == PANEL_ORIENTATION_DEGREE_90) {
			val = ts_data->gamemode_enabled ? GAME_ORIENTATION_90 :
							  NORMAL_ORIENTATION_90;
		}	/* else：blob 原值透传（0x5970 b.ne 落 0x59b8） */

		ret = fts_write_reg(FTS_REG_ORIENTATION, (u8)val);
		if (ret < 0) {
			if (fts_debug_log_level)
				FTS_ERROR("write touch mode:%d reg failed", DATA_MODE_8);
		} else {
			if (fts_debug_log_level >= 3)
				FTS_INFO("write touch mode:%d, value: %d, addr:0x%02X",
					 DATA_MODE_8, val, FTS_REG_ORIENTATION);
			fts_touch_mode[DATA_MODE_8][GET_CUR_VALUE] =
				fts_touch_mode[DATA_MODE_8][SET_CUR_VALUE];
		}
	}

	/* ---- 边缘滤波寄存器 0x8D（mode7；blob 0x59e4-0x5a1c） ---- */
	if (fts_touch_mode[DATA_MODE_7][GET_CUR_VALUE] !=
	    fts_touch_mode[DATA_MODE_7][SET_CUR_VALUE]) {
		val = fts_touch_mode[DATA_MODE_7][SET_CUR_VALUE];
		ret = fts_write_reg(FTS_REG_EDGE_FILTER_LEVEL, (u8)val);
		if (ret < 0) {
			if (fts_debug_log_level)
				FTS_ERROR("write touch mode:%d reg failed", DATA_MODE_7);
		} else {
			if (fts_debug_log_level >= 3)
				FTS_INFO("write touch mode:%d, value: %d, addr:0x%02X",
					 DATA_MODE_7, val, FTS_REG_EDGE_FILTER_LEVEL);
			fts_touch_mode[DATA_MODE_7][GET_CUR_VALUE] =
				fts_touch_mode[DATA_MODE_7][SET_CUR_VALUE];
		}
	}

	mutex_unlock(&ts_data->cmd_update_mutex);
	pm_relax(ts_data->dev);
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
	hardware_operation.ic_get_fw_version = fts_ic_fw_version;	/* _b571 接线（blob 0x18 槽） */

	hardware_operation.set_mode_value = fts_set_cur_value;
	hardware_operation.get_mode_value = fts_get_mode_value;
	hardware_operation.get_mode_all = fts_get_mode_all;
	hardware_operation.reset_mode = fts_reset_mode;
	hardware_operation.ic_switch_mode = fts_ic_switch_mode;
	hardware_operation.cmd_update_func = fts_game_mode_update;
	hardware_operation.set_mode_long_value = NULL;

	hardware_operation.palm_sensor_write = fts_palm_sensor_write;
	hardware_operation.enable_touch_raw = fts_enable_touch_raw;
	hardware_operation.panel_vendor_read = fts_panel_vendor_read;
	hardware_operation.panel_color_read = fts_panel_color_read;
	hardware_operation.panel_display_read = fts_panel_display_read;
	hardware_operation.touch_vendor_read = fts_touch_vendor_read;
	hardware_operation.get_touch_ic_buffer = NULL;
	hardware_operation.touch_doze_analysis = fts_touch_doze_analysis;
	hardware_operation.touch_log_level_control = NULL;
	hardware_operation.touch_log_level_control_v2 = fts_log_level_control;
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
	hardware_operation.set_thermal_temp = fts_set_thermal_temp;
	hardware_operation.touch_dfs_test = fts_touch_dfs_test;

#ifdef TOUCH_DUMP_TIC_SUPPORT
	fts_data->dump_type = DUMP_OFF;
#endif /* TOUCH_DUMP_TIC_SUPPORT */
	INIT_DELAYED_WORK(&fts_data->thp_signal_work, fts_thp_signal_work);

	/* init struct touchmode for reg setting */
	fts_init_touchmode_data(ts_data);
	register_touch_panel_common(ts_data->dev, TOUCH_ID, &hardware_param, &hardware_operation);
	xiaomi_register_panel_notifier_common(fts_data->dev, TOUCH_ID);
	/* _b573 boost wiring: blob 0x3b0c-0x3b1c = if (ts_data->dev->of_node) init_touch_irq(0, of_node) */
	if (ts_data->dev->of_node)
		init_touch_irq(TOUCH_ID, ts_data->dev->of_node);
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
    /* _b582-INTA：族对齐 I→A（blob 0x578f '\0016[FTS_TS_A][%s:%d]: %s'，引用点
     * fts_ts_probe+0x19c/0x1a0，**无门控**；%s 实参 = FTS_DRIVER_VERSION 串，
     * blob 0x7754 的 x3 = .rodata.str1.1+0xff3d "Focaltech V3.4 20250724"）。 */
    FTS_ALWAYS("%s", FTS_DRIVER_VERSION);
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

    /* _b583b-B8：blob 无 fod_mutex/sensor_tap_status 成员与初始化
     * （FOD 区锁 = report_mutex；0xB50..0xB88 访问点全录无此二者）⇒ 删
     * mutex_init(&ts_data->fod_mutex) 与 ts_data->sensor_tap_status = 0。 */
    ts_data->doubletap_status = 0;
    ts_data->aod_status = 0;
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
        /* _b582-INTD（A2-1）：blob 同位消息 = .rodata.str1.1+0x4d25
		 * b'\0016[FTS_TS_E][%s:%d]: failed init gpio'（引用点 0x861c，行 0x11d8=4568，
		 * __func__ = 0xe50e "fts_ts_probe_entry"，FTS_ERROR 级 cbz 门）。
		 * blob 全 ko 无 "[DIS-TF-TOUCH]" 串（0 命中），且无 "configure the gpios fail"
		 * 字样 ⇒ 整串按 blob 收口（非仅删前缀）。 */
        FTS_ERROR("failed init gpio");
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
    /* _b582-PROC：blob .rodata.str1.1 0x3024 = "tp_debug"（+0xdb8b "create tp_debug dir fail"），
     * 树侧 "tp_debug_1" 为串面 EXTRA（debugfs 目录名非节点增删，按 blob 改名）。
     * 6.18 fs/debugfs/inode.c:379 `if (IS_ERR(parent)) return parent;` ⇒ 即便同名失败也不崩。 */
    ts_data->tpdbg_dentry = debugfs_create_dir("tp_debug", NULL);
    if (IS_ERR_OR_NULL(ts_data->tpdbg_dentry))
		FTS_ERROR("create tp_debug dir fail");

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

	/* _b582-INTC：blob fts_ts_probe+0x12f0（0x88a8）**唯一**调用
	 * fts_init_xiaomi_touchfeature_v3(ts_data)（= hardware_operation 表填充器）。
	 * blob 次序：0x887c-0x8894 init_completion(&pm_completion)（done=0 + swait 名 "&x->wait"）、
	 * 0x88a0 pm_suspend=false、0x88a4 charger_status=-1 之后紧接本调用，再 0x88b8-0x88f0
	 * scp 参数填充、0x88f4 scp_tp_init。返值 blob 丢弃（0x88ac 起 w0 即被覆盖）⇒ 语句化调用。
	 * blob 内 fts_fwupg_work（0x2f0ac 段）**无**该调用（INT-B 已按 blob 删除）⇒ 此处为唯一填充点。 */
	fts_init_xiaomi_touchfeature_v3(ts_data);

	/* _b573 scp 接线：blob 0x88b8-0x88f4 = 探针尾填 fts_scp_tp_param 缺省后 fts_scp_tp_init()。
	 * blob 从 ts_data 0x434 读的一字全模块零写入（kzalloc 后恒 0），按 0 种子化。 */
	fts_scp_tp_param.param0 = 2;
	fts_scp_tp_param.unknown_04 = 211;
	fts_scp_tp_param.unknown_0c = 26;
	fts_scp_tp_param.unknown_14[0] = 225;
	fts_scp_tp_param.unknown_14[1] = 9;
	fts_scp_tp_param.unknown_14[2] = 1;
	fts_scp_tp_param.unknown_14[3] = 0;

	/* _b582-INTD（A1）：blob ts_info 级打印补位 —— blob fts_ts_probe 0x88f0
	 * `cmp w8,#3; b.hs 0x8aa4`（W 门 = FTS_LOG_INFO），0x8aa4/0x8aa8 = adrp/add
	 * .rodata.str1.1 + 0x10ff5 = b'\0016[FTS_TS_I][%s:%d]: mtk_scp_touch_init in probe'，
	 * 行号 w2 = 0x13df = 5087；printk 后 0x8abc `b 0x88f4` 回到 scp_tp_init 调用点。
	 * 位置：scp 参数填充（0x88b8-0x88ec）之后、scp_tp_init（0x88f4）之前。
	 * 端口固有差（记录，不搬函数）：blob 该 printk 的 __func__ 实参 = 0x1f55
	 * "fts_ts_probe"（blob 侧 fts_ts_probe_entry 被内联进 fts_ts_probe），
	 * 树侧本处在 fts_ts_probe_entry 体内 ⇒ 树侧 __func__ = "fts_ts_probe_entry"。 */
	FTS_INFO("mtk_scp_touch_init in probe");

	fts_scp_tp_init();

	/* _b582-INTC：fts_enable_touch_raw(1) 的 blob 唯一调用点在 fts_fwupg_work（0x2f0ac 段，
	 * INT-B 已按 blob 收口），树侧保持注释、不挪到 probe。 */
	/*fts_enable_touch_raw(1);*/

	FTS_FUNC_EXIT();
	return 0;

err_irq_req:
#if FTS_POWER_SOURCE_CUST_EN
err_power_init:
	fts_power_source_exit(ts_data, 1);	/* blob fts_ts_probe+0x87a0 w1=1 */
#endif
	if (gpio_is_valid(ts_data->pdata->reset_gpio))
		gpio_free(ts_data->pdata->reset_gpio);
	if (gpio_is_valid(ts_data->pdata->irq_gpio))
		gpio_free(ts_data->pdata->irq_gpio);
err_gpio_config:
	kfree_safe(ts_data->touch_buf);
err_buffer_init:
	/* _b582-INPUT：blob fts_ts_probe+0x8440-0x8444 = ldr x0,[ts+0x18]==input_dev 后
	 * 直接 input_unregister_device（blob 无 unregister_xiaomi_input_dev 类符号/串），
	 * 与 fts_ts_remove_entry 同形。FTS_PEN_EN=0 时笔设备路径不生成（blob 无 pen）。 */
	input_unregister_device(ts_data->input_dev);
#if FTS_PEN_EN
	input_unregister_device(ts_data->pen_dev);
#endif
err_input_init:
	if (ts_data->ts_workqueue)
		destroy_workqueue(ts_data->ts_workqueue);

	/* _b573 boost wiring: blob 0x8454-0x8458 = destroy_workqueue 之后 remove_touch_irq_boost(0) */
	remove_touch_irq_boost(TOUCH_ID);
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

    /* _b573 boost wiring: blob 0x8b10 = 起手 fts_scp_tp_exit() */
    fts_scp_tp_exit();
    /* _b573 boost wiring: blob 0x8b1c = remove_touch_irq_boost(0) */
    remove_touch_irq_boost(TOUCH_ID);

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
    fts_power_source_exit(ts_data, 0);	/* blob fts_ts_remove+0x8be8 w1=0 */
#endif

    kfree_safe(ts_data->touch_buf);
    kfree_safe(ts_data->pdata);
    kfree_safe(ts_data);

    FTS_FUNC_EXIT();

    return 0;
}

static __attribute__((always_inline)) inline int fts_ts_suspend(struct device *dev)
{
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;
	/* _b582-SLEEP：blob 忠实形态——blob 的 suspend 半首条打印是带实参的
	 * "Enter, scptp_cur_state=%d"（0x26f0，L4809，实参 scp_tp_param.param0，门
	 * debug>=3），不是 FTS_FUNC_ENTER 宏串（blob 该函数内无 "Enter" 宏串）。 */
	if (fts_debug_log_level >= 3)
		FTS_INFO("Enter, scptp_cur_state=%d", fts_scp_tp_param.param0);
	if (ts_data->suspended) {
		FTS_INFO("Already in suspend state");	/* blob 0x2658，L4811 */
		return 0;
	}
	if (ts_data->fw_loading) {
		FTS_INFO("fw upgrade in process, can't suspend");	/* blob 0x272c，L4815 */
		return 0;
	}
	/* _b582-SLEEP：blob 0x2744 无条件 enable_temperature_detection_func(0)
	 * （blob 只置 w0=0；导出者原型 (s8 touch_id, bool is_resume)，按 goodix_brl
	 * 同形补 TOUCH_ID/false）。 */
	enable_temperature_detection_func(TOUCH_ID, false);
	/* _b582-SLEEP：blob 0x2750 起 palm_status（+0xbd8）块——掌面 ON->OFF：
	 * debug>=3 打印（0x2cd8，L4823）+ update_palm_sensor_value_common(0)（0x2764）
	 * + 写 0x9A=0（0x2774）；失败/成功打印同 fts_palm_sensor_write（0x2d74 L2556 /
	 * 0x2790 L2558）。blob 该处另有 cbz x19（NULL）校验，因上方已解引用 +0xbd8
	 * 属死码，树侧按 IS_ERR 单判（等效）。 */
	if (ts_data->palm_status) {
		if (fts_debug_log_level >= 3)
			FTS_INFO("palm sensor ON, switch to OFF");
		update_palm_sensor_value_common(0);
		if (!IS_ERR(ts_data)) {
			ret = fts_write_reg(0x9A, 0);
			if (ret < 0) {
				if (fts_debug_log_level)
					FTS_ERROR("Set palm sensor switch failed!\n");
			} else if (fts_debug_log_level >= 3) {
				FTS_INFO("Set palm sensor switch: %d\n", 0);
			}
		}
	}
	/* _b582-SLEEP：blob 0x2964 gesture_cmd_delay（+0xbec）块——睡眠期手势变更被
	 * fts_update_gesture_state 记成「延迟」，此处消费：打印（0x2cf4，L4829，
	 * debug>=3）、按 gesture_status 重算 gesture_support（0x2980）、清延迟标志
	 * （0x2988）。与 fts_update_gesture_state 的 suspended 分支成对。 */
	if (ts_data->gesture_cmd_delay) {
		if (fts_debug_log_level >= 3)
			FTS_INFO("suspended gesture state:0x%02X, write cmd:0x%02X", ts_data->gesture_status, ts_data->gesture_cmd);
		ts_data->gesture_support = ts_data->gesture_status != 0 ? ENABLE : DISABLE;
		ts_data->gesture_cmd_delay = false;
	}
#ifdef FTS_XIAOMI_TOUCHFEATURE
	ts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	ts_data->aod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_11);
	ts_data->doubletap_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_14);
	/* _b582-SLEEP：blob 0x29b8-0x29e0 首条件为 (fod 不在 {0,-1}) || dt || aod
	 * （LLVM 减法技巧：w8=fod-1; cmn w8,#2; b.lo 即 fod∉{0,-1}），树侧原多一个
	 * fod_status != 100；第二条件与 blob 同（三者全 0 -> gesture_support=0）。
	 * 树侧仍镜像写入三个 status 字段（blob 用局部量，但输入面/唤醒面消费这些
	 * 字段，保留写入）。 */
	if ((ts_data->fod_status != 0 && ts_data->fod_status != -1) || ts_data->doubletap_status || ts_data->aod_status) {
		ts_data->gesture_support = 1;
	}
	/*Fod_status is 0 when the fingerprint is closed after locking the screen*/
	if (!ts_data->doubletap_status && !ts_data->aod_status && !ts_data->fod_status) {
		ts_data->gesture_support = 0;
	}
#endif
#ifdef FTS_TOUCHSCREEN_FOD
	/* _b582-SLEEP：blob 0x29e4 在 FOD 块前重读 get_mode(0,10)（blob 该函数 4 次
	 * driver_get_touch_mode_common 的第 4 次），CF 判定用新值；树侧原复用上面
	 * 同一次读取值（旧值）。 */
	ts_data->fod_status = driver_get_touch_mode_common(TOUCH_ID, DATA_MODE_10);
	if ((ts_data->fod_status == -1 || ts_data->fod_status == 100)) {
		FTS_INFO("clear CF reg");	/* blob 0x2c78，L4848 */
		 ret = fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, false);	/* blob 0x2a40 前 0x2a10 */
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);	/* blob 0x2c94，L4851 */
	}
	/* _b582-SLEEP：blob write 支路条件只有两道判定（0x2a00：fod!=100 且 fod!=-1，
	 * 含 fod==0 直落写支路），树侧原多一个 != 0；且该支路无任何打印（blob 串表
	 * 无 "write CF reg"，写支路 0x2a38 直接进 fts_fod_reg_write）。 */
	if ((ts_data->fod_status != -1 && ts_data->fod_status != 100)) {
		ret = fts_fod_reg_write(FTS_REG_GESTURE_FOD_ON, true);	/* blob 0x2a40 */
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);	/* blob 0x2d14，L4856 */
		fts_gesture_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, true);	/* blob 0x2a5c */
		if (ret < 0)
			FTS_ERROR("%s fts_fod_reg_write failed\n", __func__);	/* blob 0x2d34，L4859 */
	}
#endif
	fts_esdcheck_suspend(ts_data);	/* blob 0x2a70 */
#ifdef CONFIG_FACTORY_BUILD
	ts_data->poweroff_on_sleep = true;
#endif

	/* _b582-SLEEP：blob 0x2c04 串带 %s（__func__）与行号 L4867（树侧旧串无 %s） */
	FTS_INFO("%s : gesture_support:%d  poweroff_on_sleep:%d ", __func__, ts_data->gesture_support, ts_data->poweroff_on_sleep);
	if (ts_data->gesture_support && !ts_data->poweroff_on_sleep)
		fts_gesture_suspend(ts_data);	/* blob 0x2b28 */
	else {
		fts_irq_disable();			/* blob 0x2a9c..0x2ad4（blob 侧内联） */
		FTS_INFO("make TP enter into sleep mode");	/* blob 0x2c70，L4872 */
		ret = fts_write_reg(FTS_REG_POWER_MODE, FTS_REG_POWER_MODE_SLEEP);	/* blob 0x2aec = (0xA5,3) */
		ts_data->poweroff_on_sleep = true;	/* blob 0x2af4：写寄存器之后置位 */
		if (ret < 0)
			FTS_ERROR("failed send sleep cmd, ret=%d", ret);	/* blob 0x2b08，L4876（树侧旧串不同） */
	}
#ifdef CONFIG_FACTORY_BUILD
	FTS_INFO("[%s] factory disable power", __func__);
	fts_power_source_ctrl(ts_data, DISABLE);
#endif

	/* _b582-SLEEP：树侧原在此处有 ts_data->finger_in_fod = false;，blob 睡眠面
	 * 无该写入（blob 该字段写入只在 fts_set_fod_downup 0x5d28 与 FOD 上报路径
	 * 0xf68/0x147c）；而唤醒半的复位判据正是读 finger_in_fod/fod_finger_skip
	 * （0x2610/0x2618），睡眠期清零会破坏该判据 ⇒ 按 blob 删除。
	 * _b582-SLEEP：blob 0x2b2c-0x2b88 = suspend 尾把手势移交 SCP。按 blob 条件：
	 * 防误触开(0x2b34) -> 仅 gesture_support(+0x2e4) 时 10diff 复位(0x2b48)；
	 * 防误触关 -> param0>=2 且 gesture_support 时切 scp 通道(0x2b7c)。树侧原以
	 * gesture_status(+0x829) 为门且结构为嵌套 if，与 blob 不符。 */
	if (fts_scp_tp_mistouch_close) {
		if (ts_data->gesture_support)
			fts_gesture_10diff_reg_write(0);
	} else if (fts_scp_tp_param.param0 >= 2 && ts_data->gesture_support) {
		ret = fts_scp_tp_switch(1);
		if (ret)
			FTS_ERROR("scp_tp_switch fail, ret=%d", ret);	/* blob 0x2d54，L4887 */
	}
	/* _b582-SLEEP：blob 0x2b90-0x2ba0 param0<=1 时的 scptp_cur_state 报错
	 * （树侧缺失；blob 门为 debug_log_level != 0，串尾带 \n）。 */
	if (fts_scp_tp_param.param0 <= 1) {
		if (fts_debug_log_level)
			FTS_ERROR("scp_tp_switch fail, scptp_cur_state=%d\n", fts_scp_tp_param.param0);	/* blob 0x2c24，L4891 */
	}

	fts_release_all_finger();	/* blob 0x2ba0 */
	ts_data->suspended = true;	/* blob 0x2bac */
	/*notify thp for suspend state*/
	/* _b582-SLEEP：blob 末条是 I 级串 "\0016[FTS_TS_I][%s:%d]: Exit"（0x2bb8，L4902，
	 * 门 debug>=3）；树侧 FTS_FUNC_EXIT 为 V 级(>=5)串，此处按 blob 用 FTS_INFO。 */
	FTS_INFO("Exit");
	return 0;
}

static __attribute__((always_inline)) inline int fts_ts_resume(struct device *dev)
{
	/*u8 id = 0;*/
	/*int i = 0;*/
	int ret = 0;
	struct fts_ts_data *ts_data = fts_data;
	/* _b582-SLEEP：与 suspend 半同形——首条打印为带实参的
	 * "Enter, scptp_cur_state=%d"（blob 0x26a4，L4914，门 debug>=3），
	 * 不是 FTS_FUNC_ENTER 宏串。 */
	if (fts_debug_log_level >= 3)
		FTS_INFO("Enter, scptp_cur_state=%d", fts_scp_tp_param.param0);
	if (!ts_data->suspended) {
		FTS_DEBUG("Already in awake state");	/* blob 0x26d8，L4916（blob 为 D 级，门 debug>=4） */
		return 0;
	}
	/* _b573 scp 联动：blob 0x25d4-0x2608 = resume 先从 SCP 收回手势
	 * （param0∈{2,3} 且（param0==3 或未关防误触）） */
	if (fts_scp_tp_param.param0 >= 2 &&
	    (fts_scp_tp_param.param0 == 3 || !fts_scp_tp_mistouch_close)) {
		ret = fts_scp_tp_switch(0);

		if (ret)
			FTS_ERROR("scp_tp_switch fail, ret=%d", ret);	/* blob 0x2688，L4925 */
	}
	ts_data->suspended = false;	/* blob 0x2608（在 scp 块之后） */
#ifndef CONFIG_FACTORY_BUILD
#ifdef FTS_TOUCHSCREEN_FOD
	/* _b582-SLEEP：blob 0x2610-0x263c 两分支互斥——finger_in_fod/fod_finger_skip
	 * 命中则只打明细（0x27d8，L4933），否则 "resume reset"（0x2cd0，L4935）+
	 * fts_reset_proc(40)/fts_recover_after_reset/fts_release_all_finger 三步复位。
	 * 树侧原为「先无条件打明细再判 if」，与 blob 的分支位置不符。 */
	if (ts_data->finger_in_fod || ts_data->fod_finger_skip) {
		FTS_INFO("%s finger_in_fod:%d fod_finger_skip:%d\n", __func__, ts_data->finger_in_fod, ts_data->fod_finger_skip);
	} else {
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
	/* _b582-SLEEP：blob 0x27e4/0x27f4 唤醒必做且无条件——温度检测开（w0=1）
	 * 与热区温度 force 重设 fts_set_thermal_temp(0,1)（树侧两处均缺失）。 */
	enable_temperature_detection_func(TOUCH_ID, true);
	fts_set_thermal_temp(0, true);
	fts_ex_mode_recovery(ts_data);	/* blob 0x27fc */
	fts_esdcheck_resume(ts_data);	/* blob 0x2804 */
	/* _b582-SLEEP：blob 0x2808 palm_status（+0xbd8）块——掌面 OFF->ON：写 0x9A=5
	 * （失败/成功打印 0x285c L2556 / 0x2838 L2558），块尾 debug>=3 再补
	 * "palm sensor OFF, switch to ON"（0x2bd0，L4961）。blob 该处 cbz x19 校验同
	 * suspend 半属死码，树侧按 IS_ERR 单判。 */
	if (ts_data->palm_status) {
		if (!IS_ERR(ts_data)) {
			ret = fts_write_reg(0x9A, 5);
			if (ret < 0) {
				if (fts_debug_log_level)
					FTS_ERROR("Set palm sensor switch failed!\n");
			} else if (fts_debug_log_level >= 3) {
				FTS_INFO("Set palm sensor switch: %d\n", 1);
			}
		}
		if (fts_debug_log_level >= 3)
			FTS_INFO("palm sensor OFF, switch to ON");
	}
	/* enable charger mode */
	if (ts_data->charger_status)
		fts_charger_on(ts_data, true);	/* blob 0x2890（树侧原直写 0x8B 寄存器，blob 调 fts_charger_on） */
	if (ts_data->gesture_support && !ts_data->poweroff_on_sleep)
		fts_gesture_resume(ts_data);	/* blob 0x28a8 */
	fts_irq_enable();			/* blob 0x28b8..0x28ec（blob 侧内联） */
	ts_data->poweroff_on_sleep = false;	/* blob 0x28f8（早于 fts_gesture_reg_write 调用点） */
#ifdef FTS_TOUCHSCREEN_FOD
	fts_gesture_reg_write(FTS_REG_GESTURE_DOUBLETAP_ON, false);	/* blob 0x28fc = (1,0) */
#endif
	/* _b582-SLEEP：同上——blob 0x2944 "\0016[FTS_TS_I][%s:%d]: Exit"（L4974，门>=3） */
	FTS_INFO("Exit");
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
/* blob 同形：判型门读全局 gpio 639/640（pio 基号 500 + 偏移 139/140；
 * 6.18 pio 基号同为 500，#221 boot goodix 读 avdd-gpio 亦得全局 585
 * 双侧互证）。DET1=1: CSOT+goodix9916R（goodix 读 639），
 * DET2=1: TIANMA+focal FT3683（fts 读 640）。A-78：改在 probe 顶端读 */
#define PANEL_ID_DET1 639
#define PANEL_ID_DET2 640

static int fts_ts_probe(struct spi_device *spi)
{
    int ret = 0;
    struct fts_ts_data *ts_data = NULL;

    /* _b582-INTA：族对齐 I→A（blob 0x10fba '\0016[FTS_TS_A][%s:%d]: Touch Screen(SPI BUS)
     * driver probe...'，引用点 fts_ts_probe+0x48（0x7600），**无门控**） */
    FTS_ALWAYS("Touch Screen(SPI BUS) driver probe...");

    /* A-78 判型门（blob 同形；#222 原在 module init，initcall 期 pio gpiochip
     * 未注册 → gpio_request 恒 -517 双 fail-open 门空转）：移到 probe 顶端，
     * gpio 已就绪、读真实值。DET2=全局 640（pio 基号 500 + 偏移 140）；
     * 非 1 → "TP is not focal!" 并 return -ENODEV（让总线把 spi1.0 让给
     * goodix）；读脚失败（-517 等）fail-open 继续，如实打印错误码 */
    ret = gpio_request(PANEL_ID_DET2, "fts-det1");
    if (!ret) {
    	ret = gpio_direction_input(PANEL_ID_DET2);
    	if (!ret) {
    		int gpio_det1 = gpio_get_value(PANEL_ID_DET2);

    		FTS_INFO("gpio_det1 = %d", gpio_det1);
    		if (gpio_det1 != 1) {
    			/* _b582-INTA：HIT_NL 补尾 '\n'（blob 0x8367 '\0016[FTS_TS_I][%s:%d]: TP is not focal!\n'，
    			 * intA_strref 逐字节命中；树侧原无 '\n'） */
    			FTS_INFO("TP is not focal!\n");
    			return -ENODEV;
    		}
    	} else {
    		FTS_ERROR("gpio%d direction_input failed:%d, fail-open",
    			  PANEL_ID_DET2, ret);
    	}
    } else {
    	FTS_ERROR("gpio%d request failed:%d, fail-open", PANEL_ID_DET2, ret);
    }
    ret = 0;

#if (FTS_CHIP_TYPE == _FT8719) || (FTS_CHIP_TYPE == _FT8615) || (FTS_CHIP_TYPE == _FT8006P) || (FTS_CHIP_TYPE == _FT7120)
    spi->mode = SPI_MODE_1;
#else
    spi->mode = SPI_MODE_0;
#endif
    /*spi->mode = SPI_MODE_0;*/
    spi->bits_per_word = 8;
    ret = spi_setup(spi);
    if (ret) {
        /* _b582-INTD（A2-2）：blob = .rodata.str1.1+0xe4ea
		 * b'\0016[FTS_TS_E][%s:%d]: spi setup fail'（0x7638/0x764c，行 0x13a5=5029，
		 * __func__ 0x1f55 "fts_ts_probe"）⇒ 删前缀。 */
        FTS_ERROR("spi setup fail");
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
        /* _b582-INTD（A2-3）：blob 同位消息 = .rodata.str1.1+0x4cde
		 * b'\0016[FTS_TS_E][%s:%d]: Touch Screen(SPI BUS) driver spi probe out failed'
		 * （0x84bc/0x84d0，行 0x13bf=5055，__func__ 0x1f55 "fts_ts_probe"）——
		 * 与树原串不同文（非仅前缀差），按 blob 整串收口（blob 无 "probe fail" 字样）。 */
        FTS_ERROR("Touch Screen(SPI BUS) driver spi probe out failed");
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

/* blob fts_ts_shutdown @0x8c68/240B 重建（_b571） */
static void fts_ts_shutdown(struct spi_device *spi)
{
	struct fts_ts_data *ts_data = spi_get_drvdata(spi);

	if (!ts_data)		/* 【树侧加固】blob 无此检查；树侧 fts_ts_remove 先例（4665 行） */
		return;

	if (fts_debug_log_level >= 3)
		FTS_INFO("fts_ts_shutdown enter");

	fts_irq_disable();	/* blob 内联 fts_irq_disable()（0x8c90..0x8cc8） */

	usleep_range(500, 510);	/* blob: usleep_range_state(500, 510, 2) */

	if (fts_data)
		fts_power_source_ctrl(fts_data, DISABLE);

	xiaomi_unregister_panel_notifier_common(ts_data->dev, TOUCH_ID);
	unregister_touch_panel_common(TOUCH_ID);
}

static const struct spi_device_id fts_ts_id[] = {
    {FTS_DRIVER_NAME, 0},
    {},
};
/* blob 同形：出厂 fts ko of 别名仅 xiaomi,touch-spi（全 ko 无 focaltech,fts 串），
 * DTB SPI 面板唯一节点 compatible=xiaomi,touch-spi；无对应 spi_device_id 条目
 * 的 __spi_register_driver 静态检查 warning 与出厂一致（基线噪音，保留） */
static const struct of_device_id fts_dt_match[] = {
    {.compatible = "xiaomi,touch-spi", },
    {},
};
MODULE_DEVICE_TABLE(of, fts_dt_match);

static struct spi_driver fts_ts_driver = {
    .probe = fts_ts_probe,
    .remove = fts_ts_remove,
    .shutdown = fts_ts_shutdown,
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

/* _b580：触控框架 probe 时序倒挂修复（vseq 化）。原厂本单元 = focaltech_touch.ko，
 * 6.6 装载位 vendor_dlkm modules.load 行 213（框架 xiaomi_touch 行 211 之后），
 * 框架 probe 先跑、IC probe 后补 register_touch_panel_common。=y 内建后
 * module_init 落 device_initcall 波（真机 0.3876s，probe 0.97-1.31s），反超框架
 * 的 vseq 重放（5.4805s）：IC 先注册的活态被框架 probe 的 memset 整块清零，
 * 内嵌 timer 函数指针归零 -> 6.4308s timer.c WARN_ON_ONCE(!fn) panic。
 * 改入 .vseq.entries，重放波按 seq 1255 在框架 1253 之后收尾（.ko 语境
 * module_init = vseq_module_init，单入口单元，层级无内序影响）。 */
vseq_module_init(fts_ts_init);
/*late_initcall(fts_ts_init);*/
module_exit(fts_ts_exit);

MODULE_AUTHOR("FocalTech Driver Team");
MODULE_DESCRIPTION("FocalTech Touchscreen Driver");
MODULE_LICENSE("GPL v2");
