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
 * File Name: focaltech_core.h

 * Author: Focaltech Driver Team
 *
 * Created: 2016-08-08
 *
 * Abstract:
 *
 * Reference:
 *
*****************************************************************************/
#ifndef __LINUX_FOCALTECH_CORE_H__
#define __LINUX_FOCALTECH_CORE_H__
#pragma once
/*****************************************************************************
* Included header files
*****************************************************************************/
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/pm_qos.h>
#include <linux/dma-mapping.h>
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/time64.h>
//#include <linux/string.h>
#include "focaltech_common.h"
#include <xiaomi_touch.h>
#include "./focaltech_test/focaltech_test_ini.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define FTS_MAX_POINTS_SUPPORT              10 /* constant value, can't be changed */
#define FTS_MAX_KEYS                        4
#define FTS_KEY_DIM                         10
#define FTS_COORDS_ARR_SIZE                 4
#define FTS_ONE_TCH_LEN                     6
//#define FTS_TOUCH_DATA_LEN   (64+((ROW_NUM_MAX + 2) * COL_NUM_NAX+(ROW_NUM_MAX + 2 +COL_NUM_NAX) * 2) * 2 + 48 + 8)//(FTS_MAX_POINTS_SUPPORT * FTS_ONE_TCH_LEN + 2)

#define FTS_ONE_TCH_LEN_V2                  8
//#define FTS_TOUCH_DATA_LEN_V2  (64+((ROW_NUM_MAX + 2) * COL_NUM_NAX + (ROW_NUM_MAX + 2 + COL_NUM_NAX) * 2) * 2 + 48 + 8)//(FTS_MAX_POINTS_SUPPORT * FTS_ONE_TCH_LEN_V2 + 11)
#define FTS_HI_RES_X_MAX                    16

#define FTS_GESTURE_POINTS_MAX              6
#define FTS_GESTURE_DATA_LEN               (FTS_GESTURE_POINTS_MAX * 4 + 4)

#define FTS_SIZE_PEN                        15
#define FTS_SIZE_DEFAULT                    15

#define FTS_MAX_ID                          0x0A
#define FTS_TOUCH_OFF_E_XH                  0
#define FTS_TOUCH_OFF_XL                    1
#define FTS_TOUCH_OFF_ID_YH                 2
#define FTS_TOUCH_OFF_YL                    3
#define FTS_TOUCH_OFF_PRE                   4
#define FTS_TOUCH_OFF_AREA                  5
#define FTS_TOUCH_OFF_MINOR                 6
#define FTS_TOUCH_E_NUM                     1
#define FTS_X_MIN_DISPLAY_DEFAULT           0
#define FTS_Y_MIN_DISPLAY_DEFAULT           0
#define FTS_X_MAX_DISPLAY_DEFAULT           904
#define FTS_Y_MAX_DISPLAY_DEFAULT           572

#define FTS_TOUCH_DOWN                      0
#define FTS_TOUCH_UP                        1
#define FTS_TOUCH_CONTACT                   2
#define EVENT_DOWN(flag)                    ((flag == FTS_TOUCH_DOWN) || (flag == FTS_TOUCH_CONTACT))
#define EVENT_UP(flag)                      (flag == FTS_TOUCH_UP)

/*#define FTS_MAX_COMPATIBLE_TYPE             4*/
#define FTS_MAX_COMMMAND_LENGTH             16

#define FTS_MAX_TOUCH_BUF                   4096
#define FTS_XIAOMI_TOUCHFEATURE
#define FTS_TOUCHSCREEN_FOD
#define FTS_LOCKDOWN_INFO_SIZE               8
#define EXPERT_ARRAY_SIZE          3
#define PANEL_ORIENTATION_DEGREE_0          0   /* normal portrait orientation */
#define PANEL_ORIENTATION_DEGREE_90         1   /* anticlockwise 90 degrees */
#define PANEL_ORIENTATION_DEGREE_180        2   /* anticlockwise 180 degrees */
#define PANEL_ORIENTATION_DEGREE_270        3   /* anticlockwise 270 degrees */
#define FTS_FRAME_DATA_ADDR                 0x01
#define TOUCH_DUMP_TIC_SUPPORT
#define TOUCH_FOD_SUPPORT

#define FOCAL_DRIVER_VERSION "FT3383-2025.07.04-01"
#define TX_NUM                              9
#define RX_NUM                              14
#define ROW_NUM_MAX                         20
#define COL_NUM_MAX                         41

#define DEBUG_BUF_SIZE                      48
#define RESERVES_BIG_BUF_SIZE               8
#define MAX_DATA_LEN                        256

#define ROW_NUM_CURRENT_POS					9
#define COL_NUM_CURRENT_POS					14
#define RAW_MAX_SIZE						((ROW_NUM_MAX + 2) * COL_NUM_MAX + (ROW_NUM_MAX + 2 + COL_NUM_MAX) * 2)
#define FRAME_MAX_SIZE						(64 + RAW_MAX_SIZE * 2 + 200 + 800)
#define RESERVES_TOUCH_DATA_SIZE			(RAW_MAX_SIZE - ((ROW_NUM_CURRENT_POS + 2) * COL_NUM_CURRENT_POS + (ROW_NUM_CURRENT_POS + 2 + COL_NUM_CURRENT_POS) * 2))
#define FTS_TOUCH_DATA_LEN				FRAME_MAX_SIZE
#define FTS_TOUCH_DATA_LEN_V2				FRAME_MAX_SIZE

#ifdef TOUCH_DUMP_TIC_SUPPORT
#define FTS_DEBUG_DATA_ADDR					0x3D
#endif /* TOUCH_DUMP_TIC_SUPPORT */

/*
 * Big-Endian/Little-Endian
 */
#define SOC_LITTLE_ENDIAN                       1
//#define THP_FRAMEDATA_DEBUG
#define FTS_HTC_FRAMEDATA_SWAP
#define TOUCH_ENABLE_RAW_CRC
#define FOCALTECH_XM_HTC
#define FOCALTECH_THP_PRAME_SIZE 4096
#define CRC32_POLYNOMIAL         0xE89061DB
#define BIG_SMALL_CHANGE
#define TOUCH_ID         (0)   /* blob 恒 0（scp_tp/框架交互全传 0） */
/*for global*/
extern struct fts_test *fts_ftest;
extern hardware_operation_t hardware_operation;
extern int fts_read_and_report_foddata(struct fts_ts_data *data);
/* fts_scp_tp_param —— 112B 参数缓冲（blob .bss+0x5c8；字段由 ipi 读写反推，
 * 见 tools/_b567_touch/scp_tp_recon.md §共享模板分析） */
struct scp_tp_params {
	u32 param0;		/* 0x00 状态机 0/1/2/3/4 */
	u32 unknown_04;		/* 0x04 [TODO-VERIFY] 无访问点 */
	u32 field_08;		/* 0x08 仅 verbose 打印 */
	u32 unknown_0c;		/* 0x0c [TODO-VERIFY] 无访问点 */
	u32 field_10;		/* 0x10 仅 verbose 打印 */
	u32 unknown_14[5];	/* 0x14..0x28 [TODO-VERIFY] 无访问点 */
	u32 gesture_type;	/* 0x28 SCP->AP: type */
	u32 gesture_len;	/* 0x2c SCP->AP: len（≤0x40） */
	u8  gesture_data[64];	/* 0x30 SCP->AP: data */
};				/* sizeof == 112 (0x70) */

extern struct scp_tp_params fts_scp_tp_param;   /* focaltech_scp_tp.c */
int fts_scp_tp_init(void);
void fts_scp_tp_exit(void);
int fts_scp_tp_switch(u32 mode);
int fts_gesture_10diff_reg_write(u8 value);
extern int fts_scp_tp_ipi_send(u32 arg0, u32 arg1, u32 arg2, u32 arg3);	/* _b571 patch G */
extern bool fts_scp_tp_mistouch_close;		/* _b571 patch G */
int fts_read_and_report_foddata(struct fts_ts_data *data);
extern hardware_param_t hardware_param;
// extern int touch_mode[DATA_MODE_45][VALUE_TYPE_SIZE];
/*****************************************************************************
*  Alternative mode (When something goes wrong, the modules may be able to solve the problem.)
*****************************************************************************/
/*
 * For commnication error in PM(deep sleep) state
 */
#define FTS_PATCH_COMERR_PM                 1
#define FTS_TIMEOUT_COMERR_PM               700
/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/

struct ftxxxx_proc {
        struct proc_dir_entry *proc_entry;
	struct proc_dir_entry *tp_lockdown_info_proc;
	struct proc_dir_entry *tp_fw_version_proc;
	struct proc_dir_entry *tp_selftest_proc;
	struct proc_dir_entry *tp_data_dump_proc;
        u8 opmode;
        u8 cmd_len;
        u8 cmd[FTS_MAX_COMMMAND_LENGTH];
};

struct fts_ts_platform_data {
        /* _b581：A-74③ 收口——blob 无 iovdd_source/avdd_source 属性与成员
         * （fts_parse_dt 的 of_property_read_string 仅 avdd-name/iovdd-name 2 处，
         *  fts_power_source_init 的 regulator_get 仅字面量 "avdd"/"iovdd"），已删。 */
        const char *iovdd_reg_name;
        const char *avdd_reg_name;
        u32 irq_gpio;
        u32 irq_gpio_flags;
        u32 reset_gpio;
        u32 reset_gpio_flags;
        int avdd_gpio;
        bool have_key;
        u32 key_number;
        u32 keys[FTS_MAX_KEYS];
        u32 key_y_coords[FTS_MAX_KEYS];
        u32 key_x_coords[FTS_MAX_KEYS];
        u32 x_max;
        u32 y_max;
        u32 x_min;
        u32 y_min;
        u32 max_touch_number;
	u32 super_resolution_factors;
/*#ifdef CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE*/
	u32 touch_range_array[5];
	u32 touch_def_array[4];
	u32 touch_expert_array[4 * EXPERT_ARRAY_SIZE];
/*#endif*/
};
/*thp struct tp_raw*/
#if 0
#ifdef THP_FRAMEDATA_DEBUG
#define HAL_ROW_NUM             17
#define HAL_COL_NUM             38
#define HAL_NODE_NUM            ((HAL_ROW_NUM) * (HAL_COL_NUM))
#define HAL_SNODE_NUM           2 * ((HAL_ROW_NUM) + (HAL_COL_NUM))
#pragma pack(1)
struct tp_raw {
    uint16_t signature;
    uint16_t head_count;
    int32_t crc;
    int32_t crc_len;
    int32_t crc_r;
    int32_t crc_r_len;
    char data_state;
    char err_info;
    char event_info;
    char noise_level;
    char scan_mode;
    char scan_rate;
    uint16_t scan_freq;
    uint16_t frame_index;
    uint16_t drop_frame_no;
    uint16_t noise_r0;
    uint16_t noise_r1;
    uint16_t noise_r2;
    uint16_t noise_r3;
    int32_t reserved1;
    uint16_t reserved2;
    uint16_t next_frame_len;
    char col_num;
    char row_num;
    uint16_t ic_ms_time;
    uint16_t scan_rate_hz;
    char scan_freq_num;
    char reserved;
    char frame_data_type;
    uint16_t flg_buf;
    char debug_buf_size;
    uint16_t reserved_big_buf_size;
    uint16_t tail_cnt;
    int16_t mc_raw[HAL_NODE_NUM];
    int16_t sc_raw[HAL_SNODE_NUM];
    char debug_buf[48];
};
#pragma pack()
#endif
#endif

/*thp struct tp_frame*/
#pragma pack(1)

#ifdef TOUCH_DUMP_TIC_SUPPORT
struct ST_RepotDbgBufThp {
	uint16_t frame_no;
	uint16_t frame_data[RAW_MAX_SIZE];
};
#endif /* TOUCH_DUMP_TIC_SUPPORT */

struct tp_frame {
	s64 time_ns;
	u64 frame_cnt;
	int fod_pressed;
	int fod_trackingId;
	char thp_frame_buf[PAGE_SIZE];
#ifdef TOUCH_DUMP_TIC_SUPPORT
	int dump_type;
	char thp_dbg_buf[sizeof(struct ST_RepotDbgBufThp)];
#endif /* TOUCH_DUMP_TIC_SUPPORT */
};

struct frame_afe_data{
    uint8_t protocol_type;
    uint8_t protocol_version;
    uint16_t head_cnt;
    int32_t crc;
    int32_t crc_len;
    int32_t n_crc;
    int32_t n_crc_len;
    uint8_t scan_saturation_state;
    uint8_t data_type;
    uint8_t event_info;
    uint8_t noise_lvl;
    uint8_t scan_mode;
    uint8_t scan_rate;
    uint16_t scan_freq;
    uint16_t frame_no;
    uint16_t drop_frame_no;
    uint16_t noise_r0;
    uint16_t noise_r1;
    uint16_t noise_r2;
    uint16_t noise_r3;
    uint16_t noise_r4;
    uint16_t noise_r5;
    uint16_t cur_frame_len;
    uint16_t next_frame_len;
    uint8_t numCol;
    uint8_t numRow;
    uint16_t ic_ms_time;
    uint16_t reserved_scan_rate;
    uint8_t reserved_scan_freq;
    uint8_t  write_cmd_cnt;
    uint8_t frame_data_type;
    uint8_t flg_buf_high;
    uint8_t flg_buf_low;
    uint8_t debug_buf_size;
    uint16_t  reserved_big_buf_size;
    uint16_t  write_cmd;
    uint16_t mc_data[(ROW_NUM_MAX + 2) * COL_NUM_MAX];
    uint16_t scap_raw[ROW_NUM_MAX + COL_NUM_MAX + 2];
    uint16_t scap2_raw[ROW_NUM_MAX + COL_NUM_MAX +2];
    uint8_t debug_buf[DEBUG_BUF_SIZE];
    uint8_t reserved_big_buf[RESERVES_BIG_BUF_SIZE];
};

/* report this */
struct frame_thp_data{
    uint8_t protocol_type;
    uint8_t protocol_version;
    uint16_t head_cnt;
    int32_t crc;
    int32_t crc_len;
    int32_t n_crc;
    int32_t n_crc_len;
    uint8_t scan_saturation_state;
    uint8_t data_type;
    uint8_t event_info;
    uint8_t noise_lvl;
    uint8_t scan_mode;
    uint8_t scan_rate;
    uint16_t scan_freq;
    uint16_t frame_no;
    uint16_t drop_frame_no;
    uint16_t noise_r0;
    uint16_t noise_r1;
    uint16_t noise_r2;
    uint16_t noise_r3;
    uint16_t noise_r4;
    uint16_t noise_r5;
    uint16_t cur_frame_len;
    uint16_t next_frame_len;
    uint8_t numCol;
    uint8_t numRow;
    uint16_t ic_ms_time;
    uint16_t reserved_scan_rate;
    uint8_t reserved_scan_freq;
    uint8_t  write_cmd_cnt;
    uint8_t frame_data_type;
    uint8_t flg_buf_low;
    uint8_t flg_buf_high;
    uint8_t debug_buf_size;
    uint16_t  reserved_big_buf_size;
    uint16_t  write_cmd;
    uint16_t mc_data[TX_NUM * RX_NUM];
    uint16_t scap_raw[TX_NUM + RX_NUM];
    uint16_t scap2_raw[TX_NUM + RX_NUM];
    uint8_t debug_buf[DEBUG_BUF_SIZE];
    uint8_t reserved_big_buf[RESERVES_BIG_BUF_SIZE];
};
#pragma pack()

struct ts_event {
        int x;      /*x coordinate */
        int y;      /*y coordinate */
        int p;      /* pressure */
        int flag;   /* touch event flag: 0 -- down; 1-- up; 2 -- contact */
        int id;     /*touch ID */
        int area;
	int minor;
};

struct pen_event {
        int down;
        int inrange;
        int tip;
        int x;      /*x coordinate */
        int y;      /*y coordinate */
        int p;      /* pressure */
        int flag;   /* touch event flag: 0 -- down; 1-- up; 2 -- contact */
        int id;     /*touch ID */
        int tilt_x;
        int tilt_y;
        int azimuth;
        int tool_type;
};

struct fts_ts_data {
        struct i2c_client *client;
        struct spi_device *spi;
        struct device *dev;
        struct input_dev *input_dev;
	struct class *fts_tp_class;
        struct input_dev *pen_dev;
        struct fts_ts_platform_data *pdata;
        struct ts_ic_info ic_info;
        struct workqueue_struct *ts_workqueue;
        struct work_struct fwupg_work;
        struct delayed_work esdcheck_work;
        struct delayed_work prc_work;
	int charger_status;
        wait_queue_head_t ts_waitqueue;
        struct ftxxxx_proc proc;
        struct ftxxxx_proc proc_ta;
        spinlock_t irq_lock;
        struct mutex report_mutex;
        struct mutex bus_lock;
        struct dev_pm_qos_request dev_pm_qos_req_irq;
        unsigned long intr_jiffies;
        int irq;
        int log_level;
        int fw_is_running;      /* confirm fw is running when using spi:default 0 */
        int dummy_byte;
#if defined(CONFIG_PM) && FTS_PATCH_COMERR_PM
        struct completion pm_completion;
        bool pm_suspend;
#endif
        bool suspended;
        bool fw_loading;
        bool irq_disabled;
        bool irq_wake;
        bool power_disabled;
        bool glove_mode;
        bool cover_mode;
        bool charger_mode;
        bool touch_analysis_support;
        bool prc_support;
        bool prc_mode;
        bool esd_support;

        bool gesture_support;   /* gesture enable or disable, default: disable */
        u8 gesture_bmode;       /*gesture buffer mode*/

        u8 old_point_id;
        u8 pen_etype;
        struct pen_event pevent;
        struct ts_event events[FTS_MAX_POINTS_SUPPORT];    /* multi-touch */
        u8 touch_addr;
        u32 touch_size;
	u8 touch_fod_addr;
	u32 touch_fod_size;
        u8 *touch_buf;
        int touch_event_num;
        int touch_points;
        int key_state;
        int ta_flag;
        u32 ta_size;
        u8 *ta_buf;

        struct frame_thp_data frame_data;
        u8 *bus_tx_buf;
        u8 *bus_rx_buf;
        int bus_type;
	struct regulator *iovdd;
	struct regulator *avdd;
	/* _b581：blob 全 ko 只向 +0xae0/+0xae8 写/读这两个稳压器指针（regulator_*
	 * 各 2 次）；原 iovdd_source/avdd_source 成员为 donor 多余件，已删。 */
	u8 lockdown_info[FTS_LOCKDOWN_INFO_SIZE];
#if FTS_PINCTRL_EN
        struct pinctrl *pinctrl;
        struct pinctrl_state *pins_active;
        struct pinctrl_state *pins_suspend;
        struct pinctrl_state *pins_release;
	struct pinctrl_state *pinctrl_state_spimode;
	struct pinctrl_state *pinctrl_state_cs_spimode;	/* blob："Set pinctrl_cs_spi_mode sucesses." 对应态 */
	struct pinctrl_state *pinctrl_state_cs_gpiomode;	/* blob："Set pinctrl_cs_gpio_mode sucesses." 对应态 */
	struct pinctrl_state *pinctrl_touch_mode_ap;	/* blob 0xb40：touch_mode_ap */
	struct pinctrl_state *pinctrl_touch_mode_scp;	/* blob 0xb48：touch_mode_scp */
	/* rodin blob 成员（donor ft3383 代缺，blob 反汇编实证） */
	int dump_type;					/* blob："enable_touch_raw:%d, dump_type:%d" */
	struct delayed_work thp_signal_work;		/* blob fts_thp_signal_work */
	struct pinctrl_state *pinctrl_dvdd_enable;
	struct pinctrl_state *pinctrl_dvdd_disable;
#endif
#ifdef FTS_TOUCHSCREEN_FOD
	int fod_status;
	bool finger_in_fod;
	bool fod_finger_skip;
	int overlap_area;
	int fod_icon_status;
	struct mutex fod_mutex;
	bool point_id_changed;
#endif

	struct mutex cmd_update_mutex;
	bool poweroff_on_sleep;
	u8 gesture_status;
	int nonui_status;
	int doubletap_status;
	int aod_status;
        int sensor_tap_status;
	struct dentry *tpdbg_dentry;
	bool gamemode_enabled;
	bool power_status;
	bool is_expert_mode;
	u8 gesture_cmd;
	bool gesture_cmd_delay;
	int current_fps;
	int report_rate_status;
	void *notifier_cookie;
	struct delayed_work panel_notifier_register_work;
	bool enable_touch_raw;
	struct tp_frame thp_frame;
	bool fod_pressed;
	int palm_status;	/* blob 0xbd8：fts_palm_sensor_write 存（_b571） */

#ifdef TOUCH_THP_SUPPORT
	struct delayed_work thp_signal_work;
#ifdef TOUCH_DUMP_TIC_SUPPORT
	int dump_type;
#endif /* TOUCH_DUMP_TIC_SUPPORT */
#endif
};

enum GESTURE_MODE_TYPE {
	GESTURE_DOUBLETAP,
	GESTURE_AOD,
	GESTURE_FOD,
        GESTURE_WEAK_DOUBLETAP,
};

enum THP_IC_MODE_COMD_TYPE {
	SET_IDLE_THD_TYPE 						= 0x10,
	SET_IDLE_RATE_TYPE 						= 0x11,
	SET_REPORT_RATE_TYPE 					= 0x13,
	SET_SCAN_FREQ_TYPE 						= 0x14,
	SET_SCAN_FREQ_HOPPING_EN_TYPE 			= 0x15,
	SET_AFE_EN_TYPE 						= 0x16,
	SET_MC_SCAN_EN_TYPE 					= 0x17,
	SET_SC_SCAN_EN_TYPE 					= 0x18,
	SET_MC_CALIBRATION_EN_TYPE 				= 0x19,
	SET_SC_CALIBRATION_EN_TYPE 				= 0x1a,
	SET_INT_STATE_TYPE 						= 0x1b,
	SET_BASE_REFRESH_EN_TYPE 				= 0x1c,
	SET_FRAME_DATA_TYPE_TYPE 				= 0x1d,
	SET_CHLICK_GESTURE_EN_TYPE 				= 0x21,
	SET_DOUBLE_CHLICK_EN_TYPE 				= 0x22,
	SET_FLAG_BUF_TYPE 						= 0x23,
	SET_IC_LOG_LEVEL_TYPE 					= 0x24,
	SET_IC_CALIBRATEION_TYPE 				= 0x25,
	SET_IC_SELF_TEST_TYPE 					= 0x26,
	SET_IC_SOFT_RETEST_TYPE 				= 0x27,
	SET_SCAN_SLOPE_TYPE 					= 0x28,
	SET_SCAN_VOLTAGE_TYPE 					= 0x29,
	SET_SCAN_NUM_TYPE 						= 0x2a,
	SET_SCAN_FREQ_NUM_TYPE 					= 0x2b,
	SET_FILTER_LEVEL_TYPE 					= 0x2d,
	SET_RAW_TYPE_TYPE 						= 0x2e,
	SET_IC_RUN_STEP_TYPE 					= 0x2F,
	SET_CRC_EN_TYPE 						= 0x30,
	SET_OPEN_TRANSPORT_MODE_TYPE 			= 0x31, //set_spi_data
	SET_POS_GESTURE_EN_TYPE 				= 0x32,
	SET_IDLE_BASE_TYPE 						= 0x33,

	SET_IDLE_HIGH_BASE_EN_TYPE 				= 0x35,
	SET_IDLE_HIGH_BASE_T_TYPE 				= 0x36,
	SET_IDLE_HIGH_BASE_KEEP_TIME_TYPE 		= 0x37,
	SET_IDLE_PERCENTAGE_THD_TYPE 			= 0x38,
	SET_TOUCH_IC_INFO_TYPE 					= 0x3b,
	SET_CHARGING_STATUS_EN_TYPE 			= 0x8B,
        SET_CAMERA_STATUS_REPORT_RATE		= 0x92,
	SET_EMPTY_INT_EN_TYPE					= 0x94,
	SET_DOWN_UP_THD_TYPE					= 0x95,
	SET_TEMPERATURE_STATUS_EN_TYPE			= 0x97,
        SET_IC_GESTRUE_FEEDBACK                                 =0X98,
	SET_GAME_MODE_EN_TYPE 					= 0x99,
	SET_THP_MODE_EN_TYPE					= 0x9E,
	SET_IC_WORK_MODE_TYPE 					= 0xA5,
	SET_ID_G_HOST_RST_FLAG                                  = 0xB6,
	SET_GLOVE_EN_TYPE						= 0xC0,
	SET_FOD_EN_TYPE 						= 0xCF,
	SET_GESTURE_EN_TYPE 					= 0xD0,
	SET_DOUBLE_AND_CHLICK_GESTURE_EN_TYPE 	= 0xD1,

	SET_LINE_SHIRT_EN_TYPE 					= 0xFF,
	SET_ACTIVE_STYLUS_PROTOCOL_TYPE 		= 0xFF,
	ACTIVE_STYLUS_EN_TYPE 					= 0xFF,
	ACTIVE_STYLUS_ONLY_EN_TYPE 				= 0xFF,
	ACTIVE_STYLUS_TOUCH_SIMULTANEOUSLY_TYPE = 0xFF,
	ACTIVE_STYLUS_SYNC_SUCESS_TYPE 			= 0xFF,
	ACTIVE_STYLUS_GESTURE_MODE_TYPE 		= 0xFF,
	SET_NULL_MODE_TYPE 						= 0xFF,
	SET_ENTER_SLEEP_MODE_TYPE 				= 0xFF,
	SET_VSYNC_EN_TYPE 						= 0xFF,
	SET_HSYNC_EN_TYPE 						= 0xFF,
	SET_MC_RAW_MAX_TYPE 					= 0xFF,
	SET_SC_RAW_MAX_TYPE 					= 0xFF,
	SET_PEN_RAW_MAX_TYPE 					= 0xFF,
};

enum _FTS_BUS_TYPE {
    BUS_TYPE_NONE,
    BUS_TYPE_I2C,
    BUS_TYPE_SPI,
    BUS_TYPE_SPI_V2,
};

enum _FTS_RAW_EN_TYPE {
    TYPE_TIC = 0x00,
    TYPE_THP = 0x41,
};

enum _FTS_TOUCH_ETYPE {
    TOUCH_DEFAULT = 0x00,
    TOUCH_PROTOCOL_v2 = 0x02,
    TOUCH_EXTRA_MSG = 0x08,
    TOUCH_PEN = 0x0B,
    TOUCH_GESTURE = 0x80,
    TOUCH_FW_INIT = 0x81,
    TOUCH_IGNORE = 0xFE,
    TOUCH_ERROR = 0xFF,
};

enum _FTS_STYLUS_ETYPE {
    STYLUS_DEFAULT,
    STYLUS_HOVER,
};

enum _FTS_GESTURE_BMODE {
    GESTURE_BM_REG,
    GESTURE_BM_TOUCH,
};

/* MODULE_IMPORT_NS(VFS_internal_...) — 6.18 该 NS 归属 fs/ 子系统，=y 内建不需要 */

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
extern struct fts_ts_data *fts_data;

/* communication interface */
int fts_read(u8 *cmd, u32 cmdlen, u8 *data, u32 datalen);
int fts_read_reg(u8 addr, u8 *value);
int fts_write(u8 *writebuf, u32 writelen);
int fts_write_reg(u8 addr, u8 value);
void fts_hid2std(void);
int fts_write_cmd(u8 reg);
void fts_reset_for_upgrade(void);
int fts_bus_init(struct fts_ts_data *ts_data);
int fts_bus_exit(struct fts_ts_data *ts_data);
int fts_spi_transfer_direct(u8 *writebuf, u32 writelen, u8 *readbuf, u32 readlen);
int fts_enable_touch_raw(int en);
int fts_get_x_resolution(void);
int fts_get_y_resolution(void);
int fts_get_rx_num(void);
int fts_get_tx_num(void);
u8 fts_get_super_resolution_factor(void);
int fts_ic_self_test(char *type, int *result);
int fts_resume_suspend(bool resume, u8 gesture_type);
int fts_get_system_info(char *buf);
int fts_get_ito_raw(char *data_dump_buf);
int fts_get_mutual_raw(char *data_dump_buf);
int fts_get_mutual_raw_lp(char *data_dump_buf);
int fts_ic_fw_version(char *fw_version_buf);
int fts_ic_data_collect(char *data, int *length);
int fts_get_ic_lockdown_info(u8 lockdown_info[8]);
int fts_get_ss_raw(char *data_dump_buf);
int fts_get_ss_raw_lp(char *data_dump_buf);
int fts_get_mutual_cx_lp(char *data_dump_buf);
int fts_get_ss_ix(char *data_dump_buf);
void fts_init_hardware_param(void);
void fts_init_xiaomi_touchfeature_v3(struct fts_ts_data *ts_data);
int fts_htc_ic_getModeValue(common_data_t *common_data);
int fts_htc_ic_setModeValue(common_data_t *common_data);

/* Gesture functions */
int fts_gesture_init(struct fts_ts_data *ts_data);
int fts_gesture_exit(struct fts_ts_data *ts_data);
void fts_gesture_recovery(struct fts_ts_data *ts_data);
int fts_gesture_readdata(struct fts_ts_data *ts_data, u8 *data);
int fts_gesture_suspend(struct fts_ts_data *ts_data);
int fts_gesture_resume(struct fts_ts_data *ts_data);
int fts_gesture_reg_write(u8 mask, bool enable);
#ifdef FTS_TOUCHSCREEN_FOD
int fts_fod_reg_write(u8 mask, bool enable);
void fts_fod_recovery(void);
#endif

/* Apk and functions */
int fts_create_proc(struct fts_ts_data *ts_data);
void fts_remove_proc(struct fts_ts_data *ts_data);
int fts_create_apk_debug_channel(struct fts_ts_data *ts_data);
void fts_release_apk_debug_channel(struct fts_ts_data *ts_data);

/* ADB functions */
int fts_create_sysfs(struct fts_ts_data *ts_data);
int fts_remove_sysfs(struct fts_ts_data *ts_data);

/* ESD */
int fts_esdcheck_init(struct fts_ts_data *ts_data);
int fts_esdcheck_exit(struct fts_ts_data *ts_data);
void fts_esdcheck_switch(struct fts_ts_data *ts_data, bool enable);
void fts_esdcheck_proc_busy(struct fts_ts_data *ts_data, bool proc_debug);
void fts_esdcheck_suspend(struct fts_ts_data *ts_data);
void fts_esdcheck_resume(struct fts_ts_data *ts_data);

/* Host test */
#if FTS_TEST_EN
int fts_test_init(struct fts_ts_data *ts_data);
int fts_test_exit(struct fts_ts_data *ts_data);
#endif

/* Point Report Check*/
int fts_point_report_check_init(struct fts_ts_data *ts_data);
int fts_point_report_check_exit(struct fts_ts_data *ts_data);
void fts_prc_queue_work(struct fts_ts_data *ts_data);

/* FW upgrade */
int fts_fwupg_init(struct fts_ts_data *ts_data);
int fts_fwupg_exit(struct fts_ts_data *ts_data);
int fts_upgrade_bin(char *fw_name, bool force);
int fts_enter_test_environment(bool test_state);
int fts_flash_read(u32 addr, u8 *buf, u32 len);
int fts_read_lockdown_info(u8 *buf);
int fts_read_lockdown_info_proc(u8 *buf);
/*int fts_fw_recovery(void);*/

/* Other */
int fts_thp_ic_write_interfaces(u8 addr, s32* value, int value_len);
int fts_reset_proc(int hdelayms);
int fts_recover_after_reset(void);
int fts_check_cid(struct fts_ts_data *ts_data, u8 id_h);
int fts_wait_tp_to_valid(void);
void fts_release_all_finger(void);
void fts_tp_state_recovery(struct fts_ts_data *ts_data);
int fts_ex_mode_init(struct fts_ts_data *ts_data);
int fts_ex_mode_exit(struct fts_ts_data *ts_data);
int fts_ex_mode_recovery(struct fts_ts_data *ts_data);

void fts_irq_disable(void);
void fts_irq_enable(void);

int fts_get_lockdown_information(struct fts_ts_data *ts_data);

extern int mi_disp_set_fod_queue_work(u32 fod_btn, bool from_touch);
extern int update_fod_press_status_common(int value);
#endif /* __LINUX_FOCALTECH_CORE_H__ */
