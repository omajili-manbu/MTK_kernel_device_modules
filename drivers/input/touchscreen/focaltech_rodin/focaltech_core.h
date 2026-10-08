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
/* _b583-FTS（A7-f）：fts_htc_set_double_scan 定义在 focaltech_scp_tp.c:556
 * （blob GLOBAL 符号 0x2114，与 fts_htc_enter_idle 等同一族），fts_set_cur_value 的
 * DATA_MODE_163 分支直调它（blob 侧该 case 把它内联）。 */
int fts_htc_set_double_scan(u8 value);
int fts_gesture_10diff_reg_write(u8 value);
extern int fts_scp_tp_ipi_send(u32 arg0, u32 arg1, u32 arg2, u32 arg3);	/* _b571 patch G */
extern bool fts_scp_tp_mistouch_close;		/* _b571 patch G */
int fts_read_and_report_foddata(struct fts_ts_data *data);
/* _b582-INTA：A-80④② —— fts_charger_on 定义在 focaltech_scp_tp.c:578，原无原型
 * （② 的临时原型暂放 focaltech_core.c:92-95）；按 blob 收口后它同时被
 * fts_{charger,cover,glove}_mode_store → fts_ex_mode_switch（MODE_CHARGER）与
 * fts_ts_resume/fts_tp_state_recovery 直调 ⇒ 声明并入本块，删除 .c 内临时声明。 */
int fts_charger_on(struct fts_ts_data *ts_data, bool on);
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

/* _b582-TEST：A) 触控测试面 proc 收口——blob 的 struct ftxxxx_proc 只有 proc_entry +
 * opmode/cmd_len/cmd[]（opmode@+0x08、sizeof=0x20，⑥-b 实证）；原 donor 的
 * tp_lockdown_info_proc/tp_fw_version_proc/tp_selftest_proc/tp_data_dump_proc 四个 v0 记录
 * 字段舍去：前两者 ⑥-b 已删其使用侧，后两者随 focaltech_test.c 的 tp_selftest_v0 /
 * tp_data_dump_v0 两个 blob 全 ko 无串的 proc 节点一并删除（成对改，避免半对中间态）。 */
struct ftxxxx_proc {
        struct proc_dir_entry *proc_entry;
        u8 opmode;
        u8 cmd_len;
        u8 cmd[FTS_MAX_COMMMAND_LENGTH];
};

/* _b583b-B10：fts_ts_platform_data 按 blob 机器码全量归位（sizeof 0xC8 → 0x10C）。
 * 证据（全部为 fts_ts_probe(0x105BC) 内联 fts_parse_dt 的立即数落点，IDA 双证）：
 *   irq_gpio@0x00（0x109D0 STR W0,[X20] ← of_get_named_gpio("focaltech,irq-gpio")）
 *   reset_gpio@0x08（0x109E4 irq-gpio 之后 0x109E8 STR W0,[X20,#8]）
 *   avdd_gpio@0x10（0x10A54 STUR W23,[X22,#-8]，W23 = avdd-gpio 读数）
 *   avdd_reg_name[40]@0x18（0x10A50 STR XZR+0x10A58/0x10A5C STP 清零 40B +
 *                          0x10A94 strncpy(…,0x28)，strlen>0x27 跳过）
 *   iovdd_reg_name[40]@0x40（0x10ABC STR XZR,[X22,#0x40]! 起清零 40B +
 *                          0x10AFC strncpy(…,0x28)；X22=X20 重定基）
 *   have_key@0x68（0x108F0 STRB W8,[X20,#0x68] = of_property_read_bool 落点；
 *                          0x10E30 LDRB 同址回读做 if 判）
 *   key_number@0x6C（0x108F8 ADD X22,X20,#0x6C 作 of_property_read_u32 实参）
 *   keys[4]@0x70（0x10928 ADD X2,X20,#0x70 作 u32_array 实参；VK 打印读 0x70/74/78）
 *   key_x_coords[4]@0x80（VK 打印读 0x80/84/88 = x[0..2]）
 *   key_y_coords[4]@0x90（VK 打印读 0x90/94/98 = y[0..2]）
 *   x_max@0xA0 / y_max@0xA4 / x_min@0xA8 / y_min@0xAC
 *     （0x1080C STP W3,W5,[X20,#0xA8] + 0x10814 STP W4,W6,[X20,#0xA0] 双 STP；
 *      input_set_abs_params 实参 0x11130 W2=[#0xA8]=min、0x11134 W3=[#0xA0]=max
 *      （ABS_MT_POSITION_X），0x1114C/0x11150 同构（Y）⇒ min/max 方向与树一致，
 *      display-coords 解析语义不受影响；driver_info_show 0x15318/0x1531C 同址复读）
 *   max_touch_number@0xB0（0x10BB0 STR clamp 落点；0x11120 LDR 作 DEBUG 实参）
 *   super_resolution_factors@0xB4（0x10B5C STR，parse 序在 max-touch 之前）
 *   touch_range_array[5]@0xB8 / touch_def_array[4]@0xCC / touch_expert_array[12]@0xDC
 *     （0x10BDC/0x10BBC/0x10BFC 三条 ADD X2,X20,#imm 作 u32_array 实参；
 *      0xDC+0x30 = 0x10C = sizeof，与 kmalloc_trace size=0x10C 互证）
 * 树侧保留 irq_gpio_flags/reset_gpio_flags 两个 blob 零访问点成员（0x04/0x0C 死槽），
 * key_y_coords/key_x_coords 按 blob 互换声明序；名字域由 const char* 换 char[40]
 * （blob 内嵌数组，parse 侧同步改 memset/strncpy 形态，见 focaltech_core.c）。 */
struct fts_ts_platform_data {
	u32 irq_gpio;					/* blob 0x00 */
	u32 irq_gpio_flags;				/* blob 0x04（全 ko 零访问点，占位） */
	u32 reset_gpio;					/* blob 0x08 */
	u32 reset_gpio_flags;				/* blob 0x0C（全 ko 零访问点，占位） */
	int avdd_gpio;					/* blob 0x10 */
	u32 avdd_gpio_flags;				/* blob 0x14 槽（blob 零访问点；0x18 名字域锚点反推的 4B 位，
							 * 命名按 donor irq/reset_flags 对称风格） */
	char avdd_reg_name[40];				/* blob 0x18..0x3F（内嵌 40B 数组） */
	char iovdd_reg_name[40];			/* blob 0x40..0x67 */
	bool have_key;					/* blob 0x68 */
	u32 key_number;					/* blob 0x6C */
	u32 keys[FTS_MAX_KEYS];				/* blob 0x70 */
	u32 key_x_coords[FTS_MAX_KEYS];			/* blob 0x80 */
	u32 key_y_coords[FTS_MAX_KEYS];			/* blob 0x90 */
	u32 x_max;					/* blob 0xA0 */
	u32 y_max;					/* blob 0xA4 */
	u32 x_min;					/* blob 0xA8 */
	u32 y_min;					/* blob 0xAC */
	u32 max_touch_number;				/* blob 0xB0 */
	u32 super_resolution_factors;			/* blob 0xB4 */
	u32 touch_range_array[5];			/* blob 0xB8 */
	u32 touch_def_array[4];				/* blob 0xCC */
	u32 touch_expert_array[4 * EXPERT_ARRAY_SIZE];	/* blob 0xDC */
};
/* _b583b-B10 编译期断言（与 blob 机器码立即数偏移等式，见上注证据） */
_Static_assert(sizeof(struct fts_ts_platform_data) == 0x10C, "_b583b-B10 sizeof(pdata)==0x10C (blob probe 0x10660 kmalloc_trace)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, irq_gpio) == 0x00, "_b583b-B10 irq_gpio@0x00");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, reset_gpio) == 0x08, "_b583b-B10 reset_gpio@0x08");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, avdd_gpio) == 0x10, "_b583b-B10 avdd_gpio@0x10");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, avdd_reg_name) == 0x18, "_b583b-B10 avdd_reg_name@0x18 (probe 0x10A94 strncpy)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, iovdd_reg_name) == 0x40, "_b583b-B10 iovdd_reg_name@0x40 (probe 0x10AFC strncpy)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, have_key) == 0x68, "_b583b-B10 have_key@0x68 (probe 0x108F0 STRB)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, key_number) == 0x6C, "_b583b-B10 key_number@0x6C (probe 0x108F8)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, keys) == 0x70, "_b583b-B10 keys@0x70 (probe 0x10928)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, key_x_coords) == 0x80, "_b583b-B10 key_x_coords@0x80 (VK print 0x10990)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, key_y_coords) == 0x90, "_b583b-B10 key_y_coords@0x90 (VK print 0x10968)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, x_max) == 0xA0, "_b583b-B10 x_max@0xA0 (abs_params max=0xA0)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, y_max) == 0xA4, "_b583b-B10 y_max@0xA4");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, x_min) == 0xA8, "_b583b-B10 x_min@0xA8 (abs_params min=0xA8)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, y_min) == 0xAC, "_b583b-B10 y_min@0xAC");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, max_touch_number) == 0xB0, "_b583b-B10 max_touch_number@0xB0 (probe 0x10BB0)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, super_resolution_factors) == 0xB4, "_b583b-B10 super_resolution_factors@0xB4 (probe 0x10B5C)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, touch_range_array) == 0xB8, "_b583b-B10 touch_range_array@0xB8 (probe 0x10BDC)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, touch_def_array) == 0xCC, "_b583b-B10 touch_def_array@0xCC (probe 0x10BBC)");
_Static_assert(__builtin_offsetof(struct fts_ts_platform_data, touch_expert_array) == 0xDC, "_b583b-B10 touch_expert_array@0xDC (probe 0x10BFC)");
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
	/* _b582-INTA：A-80/③§6.2「前段 8B 缺口」收口 —— blob 反汇编逐点实证（工具
	 * tools/_b582_intA/intA_strref.py + intA_fnstr.py，全部为**符号访问之外的**
	 * 立即数偏移，故不受本轮改名影响）：
	 *   input_dev  @0x18 与 blob **同址**（fts_ts_probe 0x823c `str x21,[x19,#0x18]`，
	 *                    0x8440 `ldr x0,[x19,#0x18]` → input_unregister_device）；
	 *   pdata      @0x38 vs 树 0x30（0x7774 kmalloc_trace(size=0x10c=sizeof(pdata)) →
	 *                    0x7778 `str x0,[x19,#0x38]`；0x7dc0/0x8280 `ldr x,[x19,#0x38]`
	 *                    后按 pdata 语义读 +0xa0/+0xa8）；
	 *   ic_info    @0x40 vs 树 0x38（fts_get_ic_information：is_incell `ldrb [x19,#0x40]`、
	 *                    hid_supported #0x41、ids.chip_idh/idl #0x44/#0x45、
	 *                    cid.type `ldrh [x19,#0x4c]`；ic_info 内部布局与树完全同构）；
	 *   ts_workqueue @0x60 vs 树 0x58（0x7cf4 alloc_workqueue 结果 `str x0,[x19,#0x60]`，
	 *                    紧接 "create fts workqueue fail"）；
	 *   proc/proc_ta @0x1c8/0x1e8 vs 树 0x1c0/0x1e0（fts_create_apk_debug_channel
	 *                    0xa40c/0xa438）；pm_qos@0x270、log_level@0x2ac（fts_log_level_store
	 *                    0xc9ec `ldr w3,[x23,#0x2ac]`）vs 树 0x268/0x2a4；irq@0x2a8
	 *                    （fts_ts_remove 0x8b8c → free_irq）、pm_completion@0x2b8
	 *                    （0x8890 `str wzr,[x19,#0x2b8]` + 尾 swait_queue_head@0x2c0）、
	 *                    pm_suspend@0x2d8（0x88a0 / fts_spi_transfer 0x2ff74）vs 树
	 *                    0x2a0/0x2b0/0x2d0。
	 *   ⇒ 全链恒定 Δ=+8 ⇒ 缺口严格在 input_dev(0x18) 与 pdata 之间：blob 该窗有
	 *     **3 个 8B 槽**（0x20/0x28/0x30），树侧只有 2 个（fts_tp_class/pen_dev）。
	 *   blob 全 ko 对 0x20/0x28/0x30 三个槽**零访问点**（probe/remove/各函数全扫；
	 *   也无 class_create / get_xiaomi_touch_class_common / pen 设备调用面）⇒ 该槽
	 *   在 blob 中是无语义死槽（无可命名成员）。此处以显式 8B 占位补齐布局，只占位、
	 *   不引入任何代码或行为；如需语义命名须有 6.6 厂商源佐证。 */
	u64 front_slot_reserved;	/* 只占位（blob 0x30 槽；全 ko 无读写点） */
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
        /* _b582-INPUT：blob fts_irq_handler 用 cpu_latency_qos_add/remove_request
         * （blob 0x5e2c/0x6a98 直调），句柄槽 blob [ts+0x270] 恒 48B =
         * sizeof(struct pm_qos_request)（struct dev_pm_qos_request 为 56B）⇒ 按 blob
         * 换型换名，与 goodix（goodix_ts_core.h:pm_qos_req_irq）同形。 */
        struct pm_qos_request pm_qos_req_irq;
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
/*****************************************************************************
 * _b583-FTS（B9）：blob `fts_ts_data` 布尔区实证（逐偏移、逐成员）——
 *   suspended@0x2D9（fts_fod_recovery/fts_read_and_report_foddata/tp_state_recovery 等 31 点）、
 *   fw_loading@0x2DA（fts_resume_suspend/fwupg_work/upgrade_bin/test_*_store）、
 *   irq_disabled@0x2DB（fts_irq_disable/enable/focal_select_touchmode）、
 *   power_disabled@0x2DC（fts_power_source_{init,ctrl,ctrl_simplify}）、
 *   glove_mode@0x2DD、cover_mode@0x2DE、charger_mode@0x2DF（ex_mode 三组 sysfs）、
 *   touch_analysis_support@0x2E0（fts_irq_handler）、prc_support@0x2E1、prc_mode@0x2E2、
 *   esd_support@0x2E3、gesture_support@0x2E4、gesture_bmode@0x2E5。
 * ⇒ blob 的 {suspended,fw_loading,irq_disabled,power_disabled} 只有 **3** 个后继布尔，
 *   树侧多出的 `irq_wake` 是 donor 件（blob 全 ko 对 0x2DA..0x2DD 的四槽只出现三种语义；
 *   IDA 0xF2D4/0x184F4 两处 irq_set_irq_wake 均为**无条件**、无成员读写）⇒ 删除本成员，
 *   其后 glove/cover/charger 与 gesture_support/gesture_bmode 全部 -1 字节归位（见断言块）。 */
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

	/* _b583b-B8：blob 0x468..0xAC7（0x660B）零访问区 —— 全 ko 对该区无任何
	 * [ts,#imm] 立即数访问（blbscan_2e8_af0.txt + irq_handler 全量 ADD X,X19 穷举
	 * = {0x210,0x270,0x2B8,0x318,0x32C,0x330,0x430,0x1B0}），树侧 frame_data
	 * (struct frame_thp_data, 0x1D0) 唯一写者 fts_frame_parse_data 位于
	 * #ifdef CRC_CHECK 内而 CRC_CHECK 全工程未定义（死代码），blob 亦无 CRC 解析
	 * 面证据 ⇒ 按 blob 匿名占位。若后续拿到 6.6 厂商源佐证该区语义，再行命名。 */
	u8 reserved_frame_zone[0x660];			/* blob 0x468..0xAC7 零访问洞 */
        u8 *bus_tx_buf;					/* blob 0xAC8（fts_bus_init/read/write） */
        u8 *bus_rx_buf;					/* blob 0xAD0 */
        int bus_type;					/* blob 0xAD8（hid2std/ft5008_upgrade/probe） */
	struct regulator *iovdd;			/* blob 0xAE0（power_source_*） */
	struct regulator *avdd;				/* blob 0xAE8 */
	/* _b581：blob 全 ko 只向 +0xae0/+0xae8 写/读这两个稳压器指针（regulator_*
	 * 各 2 次）；原 iovdd_source/avdd_source 成员为 donor 多余件，已删。 */
	u8 lockdown_info[FTS_LOCKDOWN_INFO_SIZE];	/* blob 0xAF0..0xAF7 */
#if FTS_PINCTRL_EN
	/* _b583b-B8：11 指针段 0xAF8..0xB4F，序 = blob fts_power_source_init(0x11DD8)
	 * 内联 pinctrl 初始化的存储顺序（0x11EF4..0x120CC）：
	 *   pinctrl@0xAF8(devm_pinctrl_get) → active@0xB00 → suspend@0xB08 →
	 *   release@0xB10 → spimode@0xB18 → dvdd_enable@0xB20 → dvdd_disable@0xB28
	 *   （_b581 已判 0xB20/0xB28 全 ko 只写零读死槽）→ cs_gpio@0xB30 →
	 *   cs_spi@0xB38（power_source_ctrl enable 路径 0x129AC 读 0xB30=cs_gpio/
	 *   disable 路径 0x128D8 读 0xB38=cs_spi，串面 "Set pinctrl_cs_{gpio,spi}_mode"
	 *   双证，_b581 注释同口径）→ touch_ap@0xB40 → touch_scp@0xB48。
	 * 与旧树序差异 = dvdd 对移到 spimode 之后、cs_gpio/cs_spi 对调。 */
        struct pinctrl *pinctrl;			/* blob 0xAF8 */
        struct pinctrl_state *pins_active;		/* blob 0xB00 */
        struct pinctrl_state *pins_suspend;		/* blob 0xB08 */
        struct pinctrl_state *pins_release;		/* blob 0xB10 */
	struct pinctrl_state *pinctrl_state_spimode;	/* blob 0xB18 */
	struct pinctrl_state *pinctrl_dvdd_enable;	/* blob 0xB20（死槽：只写零读） */
	struct pinctrl_state *pinctrl_dvdd_disable;	/* blob 0xB28（死槽：只写零读） */
	struct pinctrl_state *pinctrl_state_cs_gpiomode;	/* blob 0xB30："Set pinctrl_cs_gpio_mode sucesses." */
	struct pinctrl_state *pinctrl_state_cs_spimode;		/* blob 0xB38："Set pinctrl_cs_spi_mode sucesses." */
	struct pinctrl_state *pinctrl_touch_mode_ap;	/* blob 0xB40 */
	struct pinctrl_state *pinctrl_touch_mode_scp;	/* blob 0xB48 */
#endif
	/* _b583b-B8 FOD/尾部块（blob 访问点全录）：blob 把 FOD 域紧随 pinctrl 段
	 * （0xB50 起），与旧树的独立 #ifdef FOD 块序不同。 */
#ifdef FTS_TOUCHSCREEN_FOD
	bool finger_in_fod;				/* blob 0xB50（irq_handler 0xF5DC 存入 tp_frame->fod_pressed；release_all_finger/fod 报点读写） */
	bool fod_finger_skip;				/* blob 0xB51（fod 报点门控；release_all_finger STRH 连 0xB50 清零） */
	int overlap_area;				/* blob 0xB54（fod 报点 0xA7FC/0xA810/0xA92C 三读，WIDTH_MAJOR 实参） */
	int fod_status;					/* 【blob 无访问点】树侧 recovery/fod 报点/gesture_suspend 仍依赖；
							* 状态面疑为框架 fts_touch_mode 镜像，blob 侧不落 ts_data；
							* 占位于 0xB58 零访问洞，语义不变。 */
	u8 reserved_fod[0x2C];				/* blob 0xB5C..0xB87 零访问洞 */
	bool point_id_changed;				/* blob 0xB88（fod 报点 0xA6FC 写 / release_all_finger 0xA48C 清零） */
#endif
	/* 0xB89..0xBD7：blob 零访问区（0x4F），宿主为下列树侧仍依赖成员；各成员
	 * blob 全 ko 无立即数访问点（状态面疑为框架 fts_touch_mode 镜像）。布局按
	 * 6.18 自然对齐（mutex=0x30）恰好填满该窗：pad7→mutex@0xB90..0xBBF →
	 * nonui@0xBC0/doubletap@0xBC4/aod@0xBC8 → pad4 → reserved_bd0@0xBD0..0xBD7。 */
	struct mutex cmd_update_mutex;			/* 0xB90（6.18 sizeof=0x30） */
	int nonui_status;				/* 0xBC0（fod 报点用；blob 对应函数取框架值入局部） */
	int doubletap_status;				/* 0xBC4（suspend 路径用） */
	int aod_status;					/* 0xBC8（suspend 路径用） */
	u8 reserved_bd0[12];				/* 0xBCC..0xBD7（aod 尾垫 4B + blob 零访问 8B；原 tpdbg_dentry@0xBD0 的垫位由显式占位保留） */
	int palm_status;				/* blob 0xBD8：fts_palm_sensor_write(0xDDA8) 存、tp_state_recovery/irq_handler 读 */
	bool poweroff_on_sleep;				/* blob 0xBDC：ic_switch_mode(0xCF50/0xCFC8)、suspend(0x131E8) 读写 */
	u8 gesture_status;				/* blob 0xBDD：update_gesture_state(0xEBA4..)/resume_suspend(0xB970) 读写 */
	/* 0xBE0..0xBE7 = tp_debug debugfs 目录 dentry（blob 双证：fts_ftest 为全局符号
	 * .bss+0x5b0、read_mass_data 0x1A934 adrp+PAGEOFF 走全局；probe 0x11684
	 * debugfs_create_dir("tp_debug",NULL) 返回值 0x11690 STR [X19,#0xBE0]、
	 * 0x119D4 以该槽作 create_file 父目录、fts_ts_remove 0x11B58 读出
	 * debugfs_remove。树侧 probe/remove 已用本成员（_b582-PROC 面），槽位由
	 * 0xBD0 归位至 blob 0xBE0 —— A1 原判（fts_ftest 成员化）系 tdata 内
	 * [X8,#0xBE0] 基址混淆，经 A2+A3 甄别 + 符号表/IDA 复核推翻。） */
	struct dentry *tpdbg_dentry;			/* blob 0xBE0..0xBE7 */
	bool gamemode_enabled;				/* blob 0xBE8（update_touchmode_data 0x58fc 写、tp_state_recovery 0x93E4 读） */
	bool power_status;				/* 0xBE9 槽位（blob 零访问；树亦零引用，作 gamemode/expert 间隔占位 —— C 布尔不产生自然间隙） */
	bool is_expert_mode;				/* blob 0xBEA（game_mode_update 0xD218 写、update_touchmode_data 0xE7E0 读） */
	u8 gesture_cmd;					/* blob 0xBEB（update_gesture_state/ic_switch_mode 写） */
	bool gesture_cmd_delay;				/* blob 0xBEC（update_gesture_state 0xEBF4/0xEC54 写） */
	int report_rate_status;				/* 0xBF0 槽位（blob 零访问；树侧 report_rate_recovery 依赖） */
	int current_fps;				/* blob 0xBF4（probe 0x10D8C 初始化、game_mode_update 240/135 写、tp_state_recovery 0x9380 读） */
	u8 reserved_mid[0x90];				/* blob 0xBF8..0xC87 零访问洞（0x90；候选宿主 = blob 独有
							* schedule_resume_suspend_work_common 相关域 —— ②b 串
							* 'enter schedule_resume_suspend_work_common enable %d'
							* 宿主 blob fts_suspend/doze 面，树侧无对应工作，留账） */
	bool enable_touch_raw;				/* blob 0xC88（set_cur_value 0xD6C0/game_mode_update 0xD194/irq_handler 0xF614） */
	/* blob 0xC89..0xC8F pad7（自然对齐）。blob 侧 thp_signal_work 0xC90..0xCD7
	 * （其内核 delayed_work=0x48）；本树 6.18 delayed_work=0x88 ⇒ 槽体
	 * 0xC90..0xD17 恰好衔接 blob 的 dump_type@0xD18 锚点，无占位。 */
	struct delayed_work thp_signal_work;		/* blob 0xC90（probe 0x106E0 INIT_DELAYED_WORK：data@0xC90/entry@0xC98/func@0xCA8=fts_thp_signal_work） */
	int dump_type;					/* blob 0xD18（irq_handler 0xF5EC 存入 tp_frame->dump_type、set_cur_value 0xDAA4 读） */
};

/* _b582-TEST：ftxxxx_proc / proc_ta 布局判据（⑥-b 实证：opmode @+0x08、
 * sizeof(struct ftxxxx_proc) = 0x20、proc_ta.proc_entry @ ts_data+0x1e8）。 */
_Static_assert(__builtin_offsetof(struct ftxxxx_proc, opmode) == 0x08, "_b582-TEST opmode@0x08");
_Static_assert(sizeof(struct ftxxxx_proc) == 0x20, "_b582-TEST sizeof(ftxxxx_proc)==0x20");
/* 相对不变量（与 blob 同构；前段 8B 缺口修复后仍成立）：两条 proc 记录相距 0x20。 */
_Static_assert(__builtin_offsetof(struct fts_ts_data, proc_ta)
               - __builtin_offsetof(struct fts_ts_data, proc) == 0x20,
               "_b582-TEST proc_ta-proc==0x20 (blob 0x1e8-0x1c8)");
/* _b582-INTA（A-80/③§6.2 收口）：前段 8B 缺口按 blob 补齐后，③ 预留的硬断言启用
 * （原为注释态）。blob 绝对目标（intA_strref/intA_fnstr 逐点）：
 *   proc@0x1c8 / proc_ta@0x1e8（fts_create_apk_debug_channel 0xa40c/0xa438）、
 *   pm_qos 槽@0x270（fts_irq_handler 0x5e2c cpu_latency_qos_add_request(&req)）、
 *   log_level@0x2ac（fts_log_level_store 0xc9ec ldr w3,[x23,#0x2ac]）。
 * （⑥-b 的 proc_ta@0x1e8 断言 = 本条；③ 原文里写 ==0x1e0 的版本为缺口未补时的
 *  中间态，本次按 blob 换成 ==0x1e8 并启用。） */
_Static_assert(__builtin_offsetof(struct fts_ts_data, proc) == 0x1c8,
               "_b582-INTA proc@0x1c8 (blob fts_create_apk_debug_channel 0xa40c)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, proc_ta) == 0x1e8,
               "_b582-INTA proc_ta@0x1e8 (blob fts_create_apk_debug_channel 0xa438)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pm_qos_req_irq) == 0x270,
               "_b582-INTA pm_qos_req_irq@0x270 (blob fts_irq_handler 0x5e2c)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, log_level) == 0x2ac,
               "_b582-INTA log_level@0x2ac (blob fts_log_level_store 0xc9ec)");
/* _b582-INTA：同链抽检（缺口补齐的旁证，非 ③ 三条判据） */
_Static_assert(__builtin_offsetof(struct fts_ts_data, pdata) == 0x38,
               "_b582-INTA pdata@0x38 (blob fts_ts_probe 0x7778)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, ic_info) == 0x40,
               "_b582-INTA ic_info@0x40 (blob fts_get_ic_information 0x40/0x44/0x4c)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, irq) == 0x2a8,
               "_b582-INTA irq@0x2a8 (blob fts_ts_remove 0x8b8c free_irq)");
/* _b583-FTS（B9）：布尔区锚点 —— 证据 = blob 全模块 [reg,#imm] 偏移扫描
 * （tools/_b583_ftsface/blbscan.py，逐偏移带访问函数名）：
 *   0x2D9 suspended（fts_fod_recovery/fts_read_and_report_foddata/fts_resume_suspend/
 *         fts_tp_state_recovery/fts_test_* 等 31 点）、
 *   0x2DA fw_loading、0x2DB irq_disabled、0x2DC power_disabled、
 *   0x2DD glove_mode、0x2DE cover_mode、0x2DF charger_mode、
 *   0x2E0 touch_analysis_support、0x2E1 prc_support、0x2E2 prc_mode、0x2E3 esd_support、
 *   0x2E4 gesture_support、0x2E5 gesture_bmode。
 * 等式两端：本断言的成员偏移（编译器 DWARF 口径）== blob 机器码立即数偏移。 */
_Static_assert(__builtin_offsetof(struct fts_ts_data, pm_completion) == 0x2b8,
               "_b583-FTS pm_completion@0x2b8 (blob fts_ts_probe 0x8890 str wzr)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pm_suspend) == 0x2d8,
               "_b583-FTS pm_suspend@0x2d8 (blob fts_update_touchmode_data ldrb [x19,#0x2d8])");
_Static_assert(__builtin_offsetof(struct fts_ts_data, suspended) == 0x2d9,
               "_b583-FTS suspended@0x2d9 (blob fts_fod_recovery ldrb [x8,#0x2d9])");
_Static_assert(__builtin_offsetof(struct fts_ts_data, fw_loading) == 0x2da,
               "_b583-FTS fw_loading@0x2da (blob fts_fwupg_work strb)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, irq_disabled) == 0x2db,
               "_b583-FTS irq_disabled@0x2db (blob fts_irq_disable/enable ldrb+strb)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, power_disabled) == 0x2dc,
               "_b583-FTS power_disabled@0x2dc (blob fts_power_source_ctrl+simplify)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, glove_mode) == 0x2dd,
               "_b583-FTS glove_mode@0x2dd (blob fts_glove_mode_show/store)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, cover_mode) == 0x2de,
               "_b583-FTS cover_mode@0x2de (blob fts_cover_mode_store/ex_mode_recovery)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, charger_mode) == 0x2df,
               "_b583-FTS charger_mode@0x2df (blob fts_charger_mode_store/ex_mode_init)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_analysis_support) == 0x2e0,
               "_b583-FTS touch_analysis_support@0x2e0 (blob fts_irq_handler 27 点)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, prc_support) == 0x2e1,
               "_b583-FTS prc_support@0x2e1 (blob fts_prc_show/store/point_report_check_init)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, prc_mode) == 0x2e2,
               "_b583-FTS prc_mode@0x2e2 (blob fts_prc_queue_work ldrb+strb)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, esd_support) == 0x2e3,
               "_b583-FTS esd_support@0x2e3 (blob fts_esdcheck_*)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gesture_support) == 0x2e4,
               "_b583-FTS gesture_support@0x2e4 (blob fts_gesture_readdata ldrb [x0,#0x2e4])");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gesture_bmode) == 0x2e5,
               "_b583-FTS gesture_bmode@0x2e5 (blob fts_gesture_bm_show/store)");

/* ===== _b583b-B8/B10：中尾段布局断言（证据 = blob 全 ko [reg,#imm] 基址追踪，
 * tools/_b583_ftsface/{ida_dump.py,blbscan_2e8_af0.txt} + evidence_b583b.txt；
 * 等式右端 = blob 机器码立即数偏移，左端 = 本结构编译期 offsetof） ===== */
_Static_assert(sizeof(struct pen_event) == 48, "_b583b-B8 sizeof(pen_event)==48 (blob events@0x318 边界反推)");
_Static_assert(sizeof(struct mutex) == 0x30, "_b583b-B8 sizeof(mutex)==0x30 (6.18 mutex_types.h)");
_Static_assert(sizeof(struct delayed_work) == 0x88, "_b583b-B8 sizeof(delayed_work)==0x88 (0xC90+0x88==0xD18 锚点衔接)");
/* 0x2E6..0x668 段（改动前后与 blob 同构，DWARF/IDA 双证） */
_Static_assert(__builtin_offsetof(struct fts_ts_data, old_point_id) == 0x2e6, "_b583b-B8 old_point_id@0x2e6");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pen_etype) == 0x2e7, "_b583b-B8 pen_etype@0x2e7 (blob fts_pen_show/store ldrb/strb)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pevent) == 0x2e8, "_b583b-B8 pevent@0x2e8");
_Static_assert(__builtin_offsetof(struct fts_ts_data, events) == 0x318, "_b583b-B8 events@0x318 (blob irq_handler ADD X8,X19,#0x318 → input_report_b)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_addr) == 0x430, "_b583b-B8 touch_addr@0x430 (blob irq_handler STRB 0xEEA0)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_size) == 0x434, "_b583b-B8 touch_size@0x434 (blob touch_size_show 0x15C1C)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_fod_addr) == 0x438, "_b583b-B8 touch_fod_addr@0x438 (blob fod STRB 0x438)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_fod_size) == 0x43c, "_b583b-B8 touch_fod_size@0x43c (blob fod STR 0x43c)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_buf) == 0x440, "_b583b-B8 touch_buf@0x440 (blob probe 0x1125C/irq 0xEE50)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_event_num) == 0x448, "_b583b-B8 touch_event_num@0x448 (blob irq 0xF3E4/report_b LDRSW 0x100C0)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, touch_points) == 0x44c, "_b583b-B8 touch_points@0x44c (blob report_b ldr/str)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, key_state) == 0x450, "_b583b-B8 key_state@0x450 (blob report_b 0x10164..)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, ta_flag) == 0x454, "_b583b-B8 ta_flag@0x454 (blob ta_open/ta_read/irq 0xFA54..0xFA60)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, ta_size) == 0x458, "_b583b-B8 ta_size@0x458 (blob irq 0xEEA4 = touch_size 同步、0xFA68 memcpy n)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, ta_buf) == 0x460, "_b583b-B8 ta_buf@0x460 (blob ta_open 0x14124 kmalloc 落槽)");
/* bus/power/lockdown 段 */
_Static_assert(__builtin_offsetof(struct fts_ts_data, bus_tx_buf) == 0xac8, "_b583b-B8 bus_tx_buf@0xac8 (blob fts_bus_init/write/probe 0x11460)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, bus_rx_buf) == 0xad0, "_b583b-B8 bus_rx_buf@0xad0 (blob fts_read/fts_bus_exit)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, bus_type) == 0xad8, "_b583b-B8 bus_type@0xad8 (blob hid2std/ft5008_upgrade LDR W)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, iovdd) == 0xae0, "_b583b-B8 iovdd@0xae0 (blob power_source_* 0xae0)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, avdd) == 0xae8, "_b583b-B8 avdd@0xae8 (blob power_source_* 0xae8)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, lockdown_info) == 0xaf0, "_b583b-B8 lockdown_info@0xaf0 (8B 锚点)");
#if FTS_PINCTRL_EN
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl) == 0xaf8, "_b583b-B8 pinctrl@0xaf8 (blob power_source_exit 0x1275C)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pins_active) == 0xb00, "_b583b-B8 pins_active@0xb00 (blob init 0x11F10/power 0x12184)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pins_suspend) == 0xb08, "_b583b-B8 pins_suspend@0xb08");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pins_release) == 0xb10, "_b583b-B8 pins_release@0xb10 (blob power_source_exit 0x12768)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_state_spimode) == 0xb18, "_b583b-B8 spimode@0xb18 (blob power 0x121A4)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_dvdd_enable) == 0xb20, "_b583b-B8 dvdd_enable@0xb20 (死槽：init 只写零读)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_dvdd_disable) == 0xb28, "_b583b-B8 dvdd_disable@0xb28 (死槽：init 只写零读)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_state_cs_gpiomode) == 0xb30, "_b583b-B8 cs_gpio@0xb30 (blob ctrl 0x129AC + cs_gpio_mode 串)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_state_cs_spimode) == 0xb38, "_b583b-B8 cs_spi@0xb38 (blob ctrl 0x128D8 + cs_spi_mode 串)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_touch_mode_ap) == 0xb40, "_b583b-B8 touch_ap@0xb40");
_Static_assert(__builtin_offsetof(struct fts_ts_data, pinctrl_touch_mode_scp) == 0xb48, "_b583b-B8 touch_scp@0xb48");
#endif
#ifdef FTS_TOUCHSCREEN_FOD
_Static_assert(__builtin_offsetof(struct fts_ts_data, finger_in_fod) == 0xb50, "_b583b-B8 finger_in_fod@0xb50 (blob irq 0xF5DC → tp_frame.fod_pressed)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, fod_finger_skip) == 0xb51, "_b583b-B8 fod_finger_skip@0xb51 (blob fod 报点 0xB7C0/release STRH 连清)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, overlap_area) == 0xb54, "_b583b-B8 overlap_area@0xb54 (blob fod 报点 0xA7FC/0xA810/0xA92C)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, point_id_changed) == 0xb88, "_b583b-B8 point_id_changed@0xb88 (blob fod 0xA6FC 写/release 0xA48C 清)");
#endif
_Static_assert(__builtin_offsetof(struct fts_ts_data, palm_status) == 0xbd8, "_b583b-B8 palm_status@0xbd8 (blob palm_sensor_write 0xDDA8 / irq 0x9410)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, poweroff_on_sleep) == 0xbdc, "_b583b-B8 poweroff_on_sleep@0xbdc (blob ic_switch_mode 0xCF50/0xCFC8、suspend 0x131E8)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gesture_status) == 0xbdd, "_b583b-B8 gesture_status@0xbdd (blob update_gesture_state 0xEBA4..0xECB4)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, tpdbg_dentry) == 0xbe0, "_b583b-B8 裁决归位 tpdbg_dentry@0xbe0 (blob probe 0x11690 STR、0x119D4 父目录、remove 0x11B58；fts_ftest 系全局 .bss+0x5b0)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gamemode_enabled) == 0xbe8, "_b583b-B8 gamemode_enabled@0xbe8 (blob 0x93E4/0xE900/0xDD28)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, is_expert_mode) == 0xbea, "_b583b-B8 is_expert_mode@0xbea (blob game_mode_update 0xD218)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gesture_cmd) == 0xbeb, "_b583b-B8 gesture_cmd@0xbeb (blob update_gesture_state 0xEBB4)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, gesture_cmd_delay) == 0xbec, "_b583b-B8 gesture_cmd_delay@0xbec (blob update_gesture_state 0xEBF4)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, current_fps) == 0xbf4, "_b583b-B8 current_fps@0xbf4 (blob probe 0x10D8C/game_mode_update 0xD260)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, enable_touch_raw) == 0xc88, "_b583b-B8 enable_touch_raw@0xc88 (blob 0xB410/0xD194/0xF614)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, thp_signal_work) == 0xc90, "_b583b-B8 thp_signal_work@0xc90 (blob probe INIT_DELAYED_WORK 0x106E0..0x10718)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, dump_type) == 0xd18, "_b583b-B8 dump_type@0xd18 (blob irq 0xF5EC → tp_frame+0x1018)");
_Static_assert(sizeof(struct fts_ts_data) == 0xd20, "_b583b-B8 sizeof(fts_ts_data)==0xd20 (blob probe 0x10660 kmalloc_trace w2=0xD20)");
/* 头段补钉（blob 侧证据链见 82/83 轮 + 本轮 set_charge_state 0x1A8/irq_handler 0x1B0/0x210） */
_Static_assert(__builtin_offsetof(struct fts_ts_data, ts_workqueue) == 0x60, "_b583b-B8 ts_workqueue@0x60 (blob probe 0x10CF8)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, charger_status) == 0x1a8, "_b583b-B8 charger_status@0x1a8 (blob set_charge_state 0xCD8C/charger_on 0xAA30)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, ts_waitqueue) == 0x1b0, "_b583b-B8 ts_waitqueue@0x1b0 (blob irq_handler wake_up ADD X0,X19,#0x1B0)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, irq_lock) == 0x208, "_b583b-B8 irq_lock@0x208 (blob probe spin_lock_init 0x10D1C STR WZR)");
_Static_assert(__builtin_offsetof(struct fts_ts_data, report_mutex) == 0x210, "_b583b-B8 report_mutex@0x210 (blob irq_handler mutex_lock ADD X21,X19,#0x210)");

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
/* _b582-INTE：blob focal_get_ic_self_test_mode (0x14d90, 88B, [GLOBAL]) —— 读
 * ic_self_test_flag（blob .bss+0x5b8）；定义在 focaltech_scp_tp.c:520。
 * 全模块唯一调用点 = fts_htc_ic_setModeValue（blob CALL26 xref 仅 0x3344）。 */
u8 focal_get_ic_self_test_mode(void);

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
/* _b582-INTA：fts_ic_self_test 的 blob teardown 直调 fts_test_malloc_free_thr
 * （blob fts_ic_self_test 0x14ff0；定义在 focaltech_test/focaltech_test.c:1796，
 * GLOBAL 符号）。原型原只在 focaltech_test/focaltech_test.h:696，而 core.c 的 TU
 * 不含该头（core.h 只 include focaltech_test_ini.h）⇒ 按 ② 处理 fts_charger_on 的
 * 同款做法，声明并入本头，避免在 .c 里放临时原型。 */
int fts_test_malloc_free_thr(struct fts_test *tdata, bool allocate);

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
