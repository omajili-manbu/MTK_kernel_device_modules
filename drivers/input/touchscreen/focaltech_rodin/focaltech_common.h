/*
 *
 * FocalTech fts TouchScreen driver.
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
* File Name: focaltech_common.h
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-16
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

#ifndef __LINUX_FOCALTECH_COMMON_H__
#define __LINUX_FOCALTECH_COMMON_H__

#include "focaltech_config.h"

/*****************************************************************************
* Macro definitions using #define
*****************************************************************************/
/* _b582-INTA：blob `.rodata.str1.1+0xff3d` = "Focaltech V3.4 20250724"（逐字节；
 * 引用点 tools/_b582_intA/intA_strref.py：fts_ts_probe+0x19c/0x1a0 A 族 %s 站点、
 * fts_debug_read+0x19c、fts_driver_info_show+0x3c），树侧原 "20211214" 为 donor 值。 */
#define FTS_DRIVER_VERSION                  "Focaltech V3.4 20250724"

#define BYTE_OFF_0(x)           (u8)((x) & 0xFF)
#define BYTE_OFF_8(x)           (u8)(((x) >> 8) & 0xFF)
#define BYTE_OFF_16(x)          (u8)(((x) >> 16) & 0xFF)
#define BYTE_OFF_24(x)          (u8)(((x) >> 24) & 0xFF)
#define FLAGBIT(x)              (0x00000001 << (x))
#define FLAGBITS(x, y)          ((0xFFFFFFFF >> (32 - (y) - 1)) & (0xFFFFFFFF << (x)))

#define FLAG_ICSERIALS_LEN      8
#define FLAG_HID_BIT            10
#define FLAG_IDC_BIT            11

#define IC_SERIALS              (FTS_CHIP_TYPE & FLAGBITS(0, FLAG_ICSERIALS_LEN-1))
#define IC_TO_SERIALS(x)        ((x) & FLAGBITS(0, FLAG_ICSERIALS_LEN-1))
#define FTS_CHIP_IDC            ((FTS_CHIP_TYPE & FLAGBIT(FLAG_IDC_BIT)) == FLAGBIT(FLAG_IDC_BIT))
#define FTS_HID_SUPPORTTED      ((FTS_CHIP_TYPE & FLAGBIT(FLAG_HID_BIT)) == FLAGBIT(FLAG_HID_BIT))

#define FTS_MAX_CHIP_IDS        8

#define FTS_CHIP_TYPE_ID        0x36
#define FTS_CHIP_TYPE_MAPPING {\
{0x92, 0x36, 0x83, 0x00, 0x00, 0x00, 0x00, 0x36, 0xA3},\
}

#define FTS_CHIP_ID_MAPPING   { \
{0x10, {0x821A, 0x8006} }, \
{0x1C, {0x8726, 0x872A, 0x872B} }, \
{0x21, {0x820A, 0x820B} }, \
{0x92, {0x5672} }, \
}

#define FILE_NAME_LENGTH                    128
#define ENABLE                              1
#define DISABLE                             0
#define VALID                               1
#define INVALID                             0
#define FTS_CMD_START1                      0x55
#define FTS_CMD_START2                      0xAA
#define FTS_CMD_START_DELAY                 12
#define FTS_CMD_READ_ID                     0x90
#define FTS_CMD_READ_ID_LEN                 4
#define FTS_CMD_READ_ID_LEN_INCELL          1
#define FTS_CMD_READ_INFO                   0x5E
/*add new macro*/
#define FTS_REG_MONITOR                     0x49
#define FTS_REG_LOCKDOWN                    0x3B
#define FTS_LOCKDOWN_LEN                    39

/*register address*/
#define FTS_REG_INT_CNT                     0x8F
#define FTS_REG_FLOW_WORK_CNT               0x91
#define FTS_REG_WORKMODE                    0x00
#define FTS_REG_WORKMODE_FACTORY_VALUE      0x40
#define FTS_REG_WORKMODE_WORK_VALUE         0x00
#define FTS_REG_ESDCHECK_DISABLE            0x8D
#define FTS_REG_CHIP_ID                     0xA3
#define FTS_REG_CHIP_ID2                    0x9F
#define FTS_REG_POWER_MODE                  0xA5    // 0-active,1-idle,3-sleep
#define FTS_REG_POWER_MODE_SLEEP            0x03
#define FTS_REG_FW_VER                      0xA6
#define FTS_REG_VENDOR_ID                   0xA8
#define FTS_REG_LCD_BUSY_NUM                0xAB
#define FTS_REG_FACE_DEC_MODE_EN            0xB0
#define FTS_REG_FACTORY_MODE_DETACH_FLAG    0xB4
#define FTS_REG_FACE_DEC_MODE_STATUS        0x01
#define FTS_REG_IDE_PARA_VER_ID             0xB5
#define FTS_REG_IDE_PARA_STATUS             0xB6
#define FTS_REG_GLOVE_MODE_EN               0xC0
#define FTS_REG_COVER_MODE_EN               0xC1
#define FTS_REG_CHARGER_MODE_EN             0x8B
#define FTS_REG_GESTURE_EN                  0xD0
#define FTS_REG_GESTURE_OUTPUT_ADDRESS      0xD3
#define FTS_REG_MODULE_ID                   0xE3
#define FTS_REG_LIC_VER                     0xE4
#define FTS_REG_ESD_SATURATE                0xED

#define FTS_REG_FOD_OUTPUT_ADDRESS          0xE1
#define FTS_REG_GESTURE_DOUBLETAP_ON        0x01
#define FTS_REG_GESTURE_FOD_ON              0x02
#define FTS_REG_GESTURE_SUPPORT             0xCF
#define FTS_REG_SENSIVITY                   0x80
#define FTS_REG_THDIFF                      0x85
#define FTS_REG_EDGE_FILTER_EN              0x8c
#define FTS_REG_EDGE_FILTER_LEVEL           0x8d
#define FTS_REG_GAMEMODE                    0xC1
#define FTS_REG_ORIENTATION                 0x8C
#define FTS_PANEL_CHANGE_FPS                0x8A
#define FTS_GESTURE_CTRL                    0xD1
#define FTS_GESTURE_DOUBLETAP               0x04
#define FTS_GESTURE_WEAK_DOUBLETAP          0x05
#define FTS_GESTURE_AOD                     0x07

#define FTS_SYSFS_ECHO_ON(buf)      (buf[0] == '1')
#define FTS_SYSFS_ECHO_OFF(buf)     (buf[0] == '0')

#define kfree_safe(pbuf) do  {\
	kfree(pbuf);\
	pbuf = NULL;\
} while (0)

/*****************************************************************************
*  Alternative mode (When something goes wrong, the modules may be able to solve the problem.)
*****************************************************************************/
/*
 * point report check
 * default: disable
 */
#define FTS_POINT_REPORT_CHECK_EN               0

enum FTS_LOG_LEVEL {
      FTS_LOG_ALWAYS = 0,
      FTS_LOG_ERROR,
      FTS_LOG_WARNING,
      FTS_LOG_INFO,
      FTS_LOG_DEBUG,
      FTS_LOG_VERBOSE,
};

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
struct ft_chip_t {
    u16 type;
    u8 chip_idh;
    u8 chip_idl;
    u8 rom_idh;
    u8 rom_idl;
    u8 pb_idh;
    u8 pb_idl;
    u8 bl_idh;
    u8 bl_idl;
};

struct ft_chip_id_t {
    u16 type;
    u16 chip_ids[FTS_MAX_CHIP_IDS];
};

struct ts_ic_info {
    bool is_incell;
    bool hid_supported;
    struct ft_chip_t ids;
    struct ft_chip_id_t cid;
};

/*****************************************************************************
* DEBUG function define here
*****************************************************************************/
/* _b582-LOG：切到 blob 忠实形态（focaltech_touch_rodin.ko 逐站点反汇编坐实，见
 * tools/_b582_log/gates_raw.txt + b582_io.py）
 * 门控变量 = 模块全局 debug_log_level（树侧同物异名 fts_debug_log_level：blob .data+0x0、
 *   4B、初值 3 = FTS_LOG_INFO；blob 全模块唯一写点 = fts_log_level_control 0x4f1c）。
 *   核心日志一律 pr_info ⇒ 串面前缀首字节 '\0016' = KERN_INFO（树侧原为 pr_err/'\0013'）。
 * 展开形态（与 blob 串面逐字节一致，含无尾随 '\n'）：
 *   FTS_ALWAYS  : "\0016[FTS_TS_A][%s:%d]: " fmt                （无门控）
 *   FTS_ERROR   : "\0016[FTS_TS_E][%s:%d]: " fmt  if (lv)      （cbz/cbnz ⇒ lv != 0）
 *   FTS_WARNING : "\0016[FTS_TS_W][%s:%d]: " fmt  if (lv >= 2) （cmp #2; b.hs）
 *   FTS_INFO    : "\0016[FTS_TS_I][%s:%d]: " fmt  if (lv >= 3) （cmp #3; b.hs）
 *   FTS_DEBUG   : "\0016[FTS_TS_D][%s:%d]: " fmt  if (lv >= 4) （cmp #4; b.hs）
 *   FTS_FUNC_ENTER/EXIT : "\0016[FTS_TS_V][%s:%d]: Enter|Exit"  if (lv >= 5)（cmp #5; b.hs）
 * 已去：`[TP-Driver][时:分:秒.毫秒]` 前缀与 ktime_get_real_ts64/rtc_time64_to_tm 调用；
 *   blob 无尾随 '\n'（kernel 对无 '\n' 的 printk 记录自行补行），故一律不追加 '\n'。
 * 判据串内容一字不动，只改前缀与门控（宏名/调用签名不变）。 */
#if FTS_DEBUG_EN
#define FTS_FUNC_ENTER() \
	do { \
		if (fts_debug_log_level >= FTS_LOG_VERBOSE) \
			pr_info("[FTS_TS_V][%s:%d]: Enter", __func__, __LINE__); \
	} while (0)
#define FTS_FUNC_EXIT() \
	do { \
		if (fts_debug_log_level >= FTS_LOG_VERBOSE) \
			pr_info("[FTS_TS_V][%s:%d]: Exit", __func__, __LINE__); \
	} while (0)
#define FTS_DEBUG(fmt, args...) \
	do { \
		if (fts_debug_log_level >= FTS_LOG_DEBUG) \
			pr_info("[FTS_TS_D][%s:%d]: " fmt, __func__, __LINE__, ##args); \
	} while (0)
#else /* #if FTS_DEBUG_EN*/
#define FTS_DEBUG(fmt, args...)
#define FTS_FUNC_ENTER()
#define FTS_FUNC_EXIT()
#endif

extern enum FTS_LOG_LEVEL fts_debug_log_level;
#define FTS_INFO(fmt, args...) \
do { \
	if (fts_debug_log_level >= FTS_LOG_INFO) \
		pr_info("[FTS_TS_I][%s:%d]: " fmt, __func__, __LINE__, ##args); \
} while(0)

#define FTS_ERROR(fmt, args...) \
do { \
	if (fts_debug_log_level) \
		pr_info("[FTS_TS_E][%s:%d]: " fmt, __func__, __LINE__, ##args); \
} while(0)

/* _b582-INTA：补 blob 的 A/W 两族（只加，不动上面已对齐的 I/E/D/V 块；宏名=调用点族名）。
 * 证据（tools/_b582_log/evid_b582.txt §[1] 串面 + gates_raw.txt §按族汇总：
 *   FTS_TS_A n=2 {'NONE': 1, 'b.lo #5': 1}、FTS_TS_W n=1 {'b.hs #2': 1}）：
 *   FTS_TS_A   : "\0016[FTS_TS_A][%s:%d]: " fmt               （无门控）
 *                blob `.rodata.str1.1` 0x578f（"%s"）/0x10fba（"Touch Screen(SPI BUS)
 *                driver probe..."）。
 *   FTS_TS_W   : "\0016[FTS_TS_W][%s:%d]: " fmt  if (lv >= 2)  （cmp #2; b.hs）
 *                blob 0x55d8 "spi_sync retry:%d"（fts_spi_transfer+0x17c）、
 *                0x1170b "not support mode!"（fts_set_cur_value+0x3cc）。
 * 与 FTS_INFO/E/D/V 同形：前缀首字节 '\0016' = KERN_INFO；一律不追加 '\n'。 */
#define FTS_ALWAYS(fmt, args...) \
do { \
	pr_info("[FTS_TS_A][%s:%d]: " fmt, __func__, __LINE__, ##args); \
} while(0)

#define FTS_WARNING(fmt, args...) \
do { \
	if (fts_debug_log_level >= FTS_LOG_WARNING) \
		pr_info("[FTS_TS_W][%s:%d]: " fmt, __func__, __LINE__, ##args); \
} while(0)

/* _b582-INTA：V 族的「任意 fmt」形态（与既有 FTS_FUNC_ENTER/EXIT 同族同门控，
 * 只补格式串版）。证据：tree 侧 D→V 三站点（core.c show_raw 的 TX%d ~ TX%d ×2、
 * fts_read_framedata 的 frame size: %d, frame data index: %d）在 blob 里全部是
 * FTS_TS_V 且门控 = cmp w8,#0x5; b.hs/b.lo（0x68c4/0x6b80/0x668c，>= VERBOSE）；
 * 串：0x56ea '\0016[FTS_TS_V][%s:%d]: TX%d ~ TX%d (cnt:%llu, frame_no:%hu):\n%s'、
 * 0xaea4 '\0016[FTS_TS_V][%s:%d]: frame size: %d, frame data index: %d'。 */
#define FTS_VERBOSE(fmt, args...) \
do { \
	if (fts_debug_log_level >= FTS_LOG_VERBOSE) \
		pr_info("[FTS_TS_V][%s:%d]: " fmt, __func__, __LINE__, ##args); \
} while(0)
#endif /* __LINUX_FOCALTECH_COMMON_H__ */
