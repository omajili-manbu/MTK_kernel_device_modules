/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023-2024 XiaoMi, Inc.
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

#include "goodix_ts_core.h"
#include <linux/rtc.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>

/* test config */
#define TOTAL_FRAME_NUM 16		/* rawdata test frames */
#define NOISEDATA_TEST_TIMES 20 /* noise test frames */
#define DISCARD_FRAMES 3
#define SAVE_IN_CSV

#define GOODIX_RESULT_SAVE_PATH "/data/misc/tp_selftest_data/Test_Data.csv"
#define GOODIX_TEST_FILE_NAME "goodix"
#define DEFAULT_SEQ_FILE_SIZE 1 * 1024 * 1024
#define MAX_DATA_BUFFER 28000
#define MAX_SHORT_NUM 15
#define MAX_LINE_LEN (1024 * 3 * 7)
#define MAX_DRV_NUM 52
#define MAX_SEN_NUM 75

#define STATISTICS_DATA_LEN 32
#define MAX_STR_LEN 32

#define GTP_TEST_PASS 1
#define GTP_PANEL_REASON 2
#define SYS_SOFTWARE_REASON 3

#define CHN_VDD 0xFF
#define CHN_GND 0x7F
#define DRV_CHANNEL_FLAG 0x80

#define CSV_TP_SPECIAL_RAW_MIN "special_raw_min"
#define CSV_TP_SPECIAL_RAW_MAX "special_raw_max"
#define CSV_TP_SPECIAL_RAW_DELTA "special_raw_delta"
#define CSV_IP_SPECIAL_FREQ "special_freq"
#define CSV_IP_SPECIAL_FREQ_DELTA "special_freq_delta"
#define CSV_TP_SHORT_THRESHOLD "shortciurt_threshold"
#define CSV_TP_SPECIAL_SELFRAW_MAX "special_selfraw_max"
#define CSV_TP_SPECIAL_SELFRAW_MIN "special_selfraw_min"
#define CSV_TP_NOISE_LIMIT "noise_data_limit"
#define CSV_TP_SELFNOISE_LIMIT "noise_selfdata_limit"
#define CSV_TP_TEST_CONFIG "test_config"
#define CSV_TP_DIFF_SHIFT_MAX "diff_shift_max"
#define CSV_TP_ADC_DUMP_MAX_MIN "adcdump_max_min_threshold"
#define CSV_TP_FILE_VERSION "version"

#define MAX_TEST_TIME_MS 15000
#define DEFAULT_TEST_TIME_MS 7000

/* berlin A */
#define MAX_DRV_NUM_BRA 21
#define MAX_SEN_NUM_BRA 42
#define SHORT_TEST_TIME_REG_BRA 0x11FF2
#define DFT_ADC_DUMP_NUM_BRA 1396
#define DFT_SHORT_THRESHOLD_BRA 16
#define DFT_DIFFCODE_SHORT_THRESHOLD_BRA 16
#define SHORT_TEST_STATUS_REG_BRA 0x10400
#define SHORT_TEST_RESULT_REG_BRA 0x10410
#define DRV_DRV_SELFCODE_REG_BRA 0x1045E
#define SEN_SEN_SELFCODE_REG_BRA 0x1084E
#define DRV_SEN_SELFCODE_REG_BRA 0x11712
#define DIFF_CODE_DATA_REG_BRA 0x11F72

/* berlin B */
#define MAX_DRV_NUM_BRB 52
#define MAX_SEN_NUM_BRB 75
#define SHORT_TEST_TIME_REG_BRB 0x26AE0
#define DFT_ADC_DUMP_NUM_BRB 762
#define DFT_SHORT_THRESHOLD_BRB 100
#define DFT_DIFFCODE_SHORT_THRESHOLD_BRB 32
#define SHORT_TEST_STATUS_REG_BRB 0x20400
#define SHORT_TEST_RESULT_REG_BRB 0x20410
#define DRV_DRV_SELFCODE_REG_BRB 0x2049A
#define SEN_SEN_SELFCODE_REG_BRB 0x21AF2
#define DRV_SEN_SELFCODE_REG_BRB 0x248A6
#define DIFF_CODE_DATA_REG_BRB 0x269E0

/* berlinD */
#define MAX_DRV_NUM_BRD 20
#define MAX_SEN_NUM_BRD 40
#define SHORT_TEST_TIME_REG_BRD 0x14D7A
#define DFT_ADC_DUMP_NUM_BRD 762
#define DFT_SHORT_THRESHOLD_BRD 100
#define DFT_DIFFCODE_SHORT_THRESHOLD_BRD 32
#define SHORT_TEST_STATUS_REG_BRD 0x13400
#define SHORT_TEST_RESULT_REG_BRD 0x13408
#define DRV_DRV_SELFCODE_REG_BRD 0x1344E
#define SEN_SEN_SELFCODE_REG_BRD 0x137E6
#define DRV_SEN_SELFCODE_REG_BRD 0x14556
#define DIFF_CODE_DATA_REG_BRD 0x14D00

#define ABS(val)			(((val) < 0)? -(val) : val)
#define MAX_ABS(a, b)			((ABS(a) > ABS(b))? a : b)
#define MIN_ABS(a, b)			((ABS(a) > ABS(b))? b : a)
#define MAX(a, b) ((a > b) ? a : b)

//for diffshift
static char g_limit_file_name[100];


enum GTP_TEST_ITEMS {
	GTP_CAP_TEST = 0,
	GTP_DELTA_TEST,
	GTP_NOISE_TEST,
	GTP_SHORT_TEST,
	GTP_SELFCAP_TEST,
	GTP_SELFNOISE_TEST,
	GTP_CUSTOM_INFO_TEST,
	GTP_CHIP_KEY_INFO_TEST,
	GTP_VERSION_TEST,
	GTP_RESET_TEST,
	GTP_SPECIAL_DELTA_TEST,
	GTP_DIFF_SHIFT_TEST,
	MAX_TEST_ITEMS = 20
};

enum GTP_MUTUAL_DATA_TYPE {
	GTP_MUTUAL_RAW,
	GTP_MUTUAL_DIFF,
	GTP_SELF_RAW,
	GTP_SELF_DIFF
};

static bool module_initialized;
// static struct seq_file *g_seq;

/* berlin A drv-sen map */
static u8 brl_a_drv_map[] = {42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62};

static u8 brl_a_sen_map[] = {0,	 1,	 2,	 3,	 4,	 5,	 6,	 7,	 8,	 9,	 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
							 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41};

/* berlin B drv-sen map */
static u8 brl_b_drv_map[] = {75,  76,  77,	78,	 79,  80,  81,	82,	 83,  84,  85,	86,	 87,  88,  89,	90,	 91,  92,
							 93,  94,  95,	96,	 97,  98,  99,	100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
							 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126};
static u8 brl_b_sen_map[] = {0,	 1,	 2,	 3,	 4,	 5,	 6,	 7,	 8,	 9,	 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
							 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
							 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74};

/* berlin D drv-sen map */
static u8 brl_d_drv_map[] = {
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59,
};

static u8 brl_d_sen_map[] = {
	0,	1,	2,	3,	4,	5,	6,	7,	8,	9,	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
};

typedef struct __attribute__((packed)) {
	u8 result;
	u8 drv_drv_num;
	u8 sen_sen_num;
	u8 drv_sen_num;
	u8 drv_gnd_avdd_num;
	u8 sen_gnd_avdd_num;
	u16 checksum;
} test_result_t;

#define CHIP_KEY_INFO_MAX_LEN 0x40
#define CHIP_UID_MAX_LEN 16
typedef struct {
	u8 hw_ver;
	u8 package_site;
	u8 chip_uid[CHIP_UID_MAX_LEN];
	u8 ft_test_result;
	u8 substrate_tech;
	u8 mcu_fab;
	u8 res[40];
	u8 vaild_len;
	u16 check_sum;
} chip_attr_info_t;

struct params_info_t {
	u32 max_drv_num;
	u32 max_sen_num;
	u8 *drv_map;
	u8 *sen_map;
	u32 short_test_time_reg;
	u32 short_test_status_reg;
	u32 short_test_result_reg;
	u32 drv_drv_selfcode_reg;
	u32 sen_sen_selfcode_reg;
	u32 drv_sen_selfcode_reg;
	u32 diffcode_data_reg;
	u16 short_test_dump_num;
	u16 dft_short_threshold;
	u16 short_diffcode_threshold;
};

struct params_info_t params_bra = {
	MAX_DRV_NUM_BRA,
	MAX_SEN_NUM_BRA,
	brl_a_drv_map,
	brl_a_sen_map,
	SHORT_TEST_TIME_REG_BRA,
	SHORT_TEST_STATUS_REG_BRA,
	SHORT_TEST_RESULT_REG_BRA,
	DRV_DRV_SELFCODE_REG_BRA,
	SEN_SEN_SELFCODE_REG_BRA,
	DRV_SEN_SELFCODE_REG_BRA,
	DIFF_CODE_DATA_REG_BRA,
	DFT_ADC_DUMP_NUM_BRA,
	DFT_SHORT_THRESHOLD_BRA,
	DFT_DIFFCODE_SHORT_THRESHOLD_BRA,
};

struct params_info_t params_brb = {
	MAX_DRV_NUM_BRB,
	MAX_SEN_NUM_BRB,
	brl_b_drv_map,
	brl_b_sen_map,
	SHORT_TEST_TIME_REG_BRB,
	SHORT_TEST_STATUS_REG_BRB,
	SHORT_TEST_RESULT_REG_BRB,
	DRV_DRV_SELFCODE_REG_BRB,
	SEN_SEN_SELFCODE_REG_BRB,
	DRV_SEN_SELFCODE_REG_BRB,
	DIFF_CODE_DATA_REG_BRB,
	DFT_ADC_DUMP_NUM_BRB,
	DFT_SHORT_THRESHOLD_BRB,
	DFT_DIFFCODE_SHORT_THRESHOLD_BRB,
};

struct params_info_t params_brd = {
	MAX_DRV_NUM_BRD,
	MAX_SEN_NUM_BRD,
	brl_d_drv_map,
	brl_d_sen_map,
	SHORT_TEST_TIME_REG_BRD,
	SHORT_TEST_STATUS_REG_BRD,
	SHORT_TEST_RESULT_REG_BRD,
	DRV_DRV_SELFCODE_REG_BRD,
	SEN_SEN_SELFCODE_REG_BRD,
	DRV_SEN_SELFCODE_REG_BRD,
	DIFF_CODE_DATA_REG_BRD,
	DFT_ADC_DUMP_NUM_BRD,
	DFT_SHORT_THRESHOLD_BRD,
	DFT_DIFFCODE_SHORT_THRESHOLD_BRD,
};

struct ts_test_params {
	bool test_items[MAX_TEST_ITEMS];

	u32 rawdata_addr;
	u32 noisedata_addr;
	u32 self_rawdata_addr;
	u32 self_noisedata_addr;

	u32 drv_num;
	u32 sen_num;

	struct params_info_t *params_info;

	s32 cfg_buf[GOODIX_CFG_MAX_SIZE];
	s32 max_limits[MAX_DRV_NUM * MAX_SEN_NUM];
	s32 min_limits[MAX_DRV_NUM * MAX_SEN_NUM];
	s32 deviation_limits[MAX_DRV_NUM * MAX_SEN_NUM];
	s32 self_max_limits[MAX_DRV_NUM + MAX_SEN_NUM];
	s32 self_min_limits[MAX_DRV_NUM + MAX_SEN_NUM];
	s32 noise_threshold[MAX_SCAN_FREQ_NUM][MAX_DRV_NUM * MAX_SEN_NUM];
	s32 diff_shift_test_freq_cnt;
	s32 diff_shift_freq[MAX_SCAN_FREQ_NUM]; /* record diff shift test freq index */
	s32 diff_shift_max[MAX_SCAN_FREQ_NUM][MAX_DRV_NUM * MAX_SEN_NUM];
	s32 self_noise_threshold;
	s32 special_freq_deviation_limts[MAX_DRV_NUM * MAX_SEN_NUM];
	s32 adcdump_max_min_threshold;

	u32 special_freq;
	u32 short_threshold;
	u32 r_drv_drv_threshold;
	u32 r_drv_sen_threshold;
	u32 r_sen_sen_threshold;
	u32 r_drv_gnd_threshold;
	u32 r_sen_gnd_threshold;
	u32 avdd_value;
};

struct ts_test_rawdata {
	s16 data[MAX_DRV_NUM * MAX_SEN_NUM];
	u32 size;
};

struct ts_test_self_rawdata {
	s16 data[MAX_DRV_NUM + MAX_SEN_NUM];
	u32 size;
};

struct ts_short_res {
	u8 short_num;
	s16 short_msg[4 * MAX_SHORT_NUM];
};

struct ts_open_res {
	u8 beyond_max_limit_cnt[MAX_DRV_NUM * MAX_SEN_NUM];
	u8 beyond_min_limit_cnt[MAX_DRV_NUM * MAX_SEN_NUM];
	u8 beyond_accord_limit_cnt[MAX_DRV_NUM * MAX_SEN_NUM];
};

struct ts_mutual_freq_noise {
	struct ts_test_rawdata noisedata[NOISEDATA_TEST_TIMES];
};

struct goodix_ts_test {
	struct goodix_ts_core *ts;
	struct ts_test_params test_params;
	struct ts_test_rawdata rawdata[TOTAL_FRAME_NUM];
	struct ts_test_rawdata special_freq_accord_arr;
	struct ts_test_rawdata accord_arr[TOTAL_FRAME_NUM];
	struct ts_mutual_freq_noise mutual_freq_diff[MAX_SCAN_FREQ_NUM];
	s16 diff_shift_noise[MAX_SCAN_FREQ_NUM][MAX_DRV_NUM * MAX_SEN_NUM];
	struct goodix_ic_config test_config;
	struct ts_test_self_rawdata self_rawdata;
	struct ts_test_self_rawdata self_noisedata;
	struct ts_short_res short_res;
	u8 b_adc_mm_ng;
	u8 b_adc_negative_sta_ng;
	u8 b_adc_forward_sta_ng;
	s16 adc_negative_sta[80];//Negative Saturation Data
	s16 adc_forward_sta[80];//Forward Saturation Data
	s16 adc_dump_mm[80];//ADC dump data, max-min Data
	struct ts_open_res open_res;
	char cur_version[64];
	char chip_pid[8];
	chip_attr_info_t chip_info;
	char custom_info_flash[16];
	char custom_info_sram[16];

	/*[0][0][0][0][0]..  0 without test; 1 pass, 2 panel failed; 3 software failed */
	char test_result[MAX_TEST_ITEMS];
	char test_info[TS_RAWDATA_RESULT_MAX];
	u8 frame_buf[8*1024];
} *g_ts_test;

static DEFINE_MUTEX(inspect_mutex);
static DECLARE_COMPLETION(inspect_done);

static u8 g_test_config[4096];
static int g_test_config_len;

static int cal_cha_to_cha_res(struct goodix_ts_test *ts_test, int v1, int v2)
{
	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_A)
		return (v1 - v2) * 63 / v2;
	else if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_B)
		return (v1 - v2) * 74 / v2 + 20;
	else
		return (v1 / v2 - 1) * 70 + 59;
}

static int cal_cha_to_avdd_res(struct goodix_ts_test *ts_test, int v1, int v2)
{
	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_A)
		return 64 * (2 * v2 - 25) * 40 / v1 - 40;
	else if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_B)
		return 64 * (2 * v2 - 25) * 99 / v1 - 60;
	else if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_D)
		return 64 * (2 * v2 - 25) * 93 / v1 - 20;
	else
		return 64 * (2 * v2 - 25) * 76 / v1 - 15;
}

static int cal_cha_to_gnd_res(struct goodix_ts_test *ts_test, int v)
{
	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_A)
		return 64148 / v - 40;
	else if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_B)
		return 150500 / v - 60;
	else
		return 145000 / v - 15;
}

static int ts_test_reset(struct goodix_ts_test *ts_test, u32 delay_ms)
{
	return ts_test->ts->hw_ops->reset(ts_test->ts, delay_ms);
}

static int ts_test_read(struct goodix_ts_test *ts_test, u32 addr, u8 *data, u32 len)
{
	return ts_test->ts->hw_ops->read(ts_test->ts, addr, data, len);
}

static int ts_test_write(struct goodix_ts_test *ts_test, u32 addr, u8 *data, u32 len)
{
	return ts_test->ts->hw_ops->write(ts_test->ts, addr, data, len);
}

static int ts_test_send_cmd(struct goodix_ts_test *ts_test,
		struct goodix_ts_cmd *cmd)
{
	u32 cmd_addr = ts_test->ts->ic_info.misc.cmd_addr;
	int retry = 5;
	u8 recv_buf[8] = {0};
	int i;
	u16 checksum = 0;

	cmd->state = 0;
	cmd->ack = 0;
	for (i = 0; i < cmd->len; i++)
		checksum += cmd->buf[i];
	cmd->buf[cmd->len] = checksum & 0xFF;
	cmd->buf[cmd->len + 1] = (checksum >> 8) & 0xFF;

	ts_info("write cmd [0x%04X]: %*ph",
		cmd_addr,
		(cmd->len + 2) > 24 ? 24 : cmd->len + 2,
		cmd->buf);

	ts_test->ts->hw_ops->write(ts_test->ts, cmd_addr, cmd->buf, cmd->len + 2);
	while (retry--) {
		usleep_range(2000, 2100);
		ts_test->ts->hw_ops->read(ts_test->ts, cmd_addr, recv_buf, sizeof(recv_buf));

		ts_info("read ack [0x%04X]: %*ph",
			cmd_addr,
			(int)sizeof(recv_buf) > 24 ? 24 : (int)sizeof(recv_buf),
			recv_buf);

		if (recv_buf[1] == 0x80)
			break;
	}
	if (retry < 0) {
		ts_err("can't receive cmd ack:%x", recv_buf[1]);
		return -1;
	}
	msleep(20);
	return 0;
}

static int ts_test_irq_enable(struct goodix_ts_test *ts_test, bool flag)
{
	return ts_test->ts->hw_ops->irq_enable(ts_test->ts, flag);
}

static int ts_test_send_config(struct goodix_ts_test *ts_test, u8 *cfg, u32 len)
{
	struct goodix_ts_hw_ops *hw_ops = ts_test->ts->hw_ops;
	u32 fw_buffer_addr = ts_test->ts->ic_info.misc.fw_buffer_addr;
	u32 cmd_addr = ts_test->ts->ic_info.misc.cmd_addr;
	u8 start_cmd[] = {0x00, 0x00, 0x04, 0x04, 0x08, 0x00};
	u8 write_cmd[] = {0x00, 0x00, 0x04, 0x05, 0x09, 0x00};
	u8 end_cmd[] = {0x00, 0x00, 0x04, 0x06, 0x0A, 0x00};
	int retry;
	u8 recv_buf[2];
	int ret = 0;

	hw_ops->write(ts_test->ts, cmd_addr, start_cmd, sizeof(start_cmd));
	retry = 20;
	while (retry--) {
		usleep_range(5000, 5100);
		hw_ops->read(ts_test->ts, cmd_addr, recv_buf, sizeof(recv_buf));
		if (recv_buf[0] == 0x80 && recv_buf[1] == 0x80)
			break;
	}
	if (retry < 0) {
		ts_err("failed write cfg prepare cmd, %x %x", recv_buf[0], recv_buf[1]);
		return -1;
	}

	hw_ops->write(ts_test->ts, fw_buffer_addr, cfg, len);
	hw_ops->write(ts_test->ts, cmd_addr, write_cmd, sizeof(write_cmd));
	retry = 40;
	while (retry--) {
		usleep_range(5000, 5100);
		hw_ops->read(ts_test->ts, cmd_addr, recv_buf, sizeof(recv_buf));
		if (recv_buf[0] == 0x80 && recv_buf[1] == 0x80)
			break;
	}
	if (retry < 0) {
		ts_err("failed send config data ready cmd, %x %x", recv_buf[0], recv_buf[1]);
		ret = -1;
	}

	hw_ops->write(ts_test->ts, cmd_addr, end_cmd, sizeof(end_cmd));
	if (ret == 0) {
		msleep(100);
		ts_info("success send config");
	}

	return ret;
}

static int ts_test_read_version(struct goodix_ts_test *ts_test, struct goodix_fw_version *version)
{
	return ts_test->ts->hw_ops->read_version(ts_test->ts, version);
}

static void goto_next_line(char **ptr)
{
	do {
		*ptr = *ptr + 1;
	}
	while (**ptr != '\n' && **ptr != '\0');
	if (**ptr == '\0') {
		return;
	}
	*ptr = *ptr + 1;
}

static void copy_this_line(char *dest, char *src)
{
	char *copy_from;
	char *copy_to;

	copy_from = src;
	copy_to = dest;
	do {
		*copy_to = *copy_from;
		copy_from++;
		copy_to++;
	}
	while ((*copy_from != '\n') && (*copy_from != '\r') && (*copy_from != '\0'));
	*copy_to = '\0';
}

static int getrid_space(s8 *data, s32 len)
{
	u8 *buf = NULL;
	s32 i;
	u32 count = 0;

	buf = (char *)kzalloc(len + 5, GFP_KERNEL);
	if (buf == NULL) {
		ts_err("get space kzalloc error");
		return -ESRCH;
	}

	for (i = 0; i < len; i++) {
		if (data[i] == ' ' || data[i] == '\r' || data[i] == '\n') {
			continue;
		}
		buf[count++] = data[i];
	}

	buf[count++] = '\0';

	memcpy(data, buf, count);
	kfree(buf);

	return count;
}

static int parse_valid_data(char *buf_start, loff_t buf_size, char *ptr, s32 *data, s32 rows)
{
	int i = 0;
	int j = 0;
	char *token = NULL;
	char *tok_ptr = NULL;
	char *row_data = NULL;
	long temp_val;

	if (!ptr) {
		ts_err("ptr is NULL");
		return -EINVAL;
	}
	if (!data) {
		ts_err("data is NULL");
		return -EINVAL;
	}

	row_data = (char *)kzalloc(MAX_LINE_LEN, GFP_KERNEL);
	if (!row_data) {
		ts_err("alloc bytes %d failed.", MAX_LINE_LEN);
		return -ENOMEM;
	}

	for (i = 0; i < rows; i++) {
		memset(row_data, 0, MAX_LINE_LEN);
		copy_this_line(row_data, ptr);
		getrid_space(row_data, strlen(row_data));
		tok_ptr = row_data;
		while ((token = strsep(&tok_ptr, ","))) {
			if (strlen(token) == 0)
				continue;
			if (kstrtol(token, 0, &temp_val)) {
				kfree(row_data);
				return -EINVAL;
			}
			data[j++] = (s32)temp_val;
		}
		if (i == rows - 1)
			break;
		goto_next_line(&ptr);
		if (!ptr || (0 == strlen(ptr)) || (ptr >= (buf_start + buf_size))) {
			ts_info("invalid ptr, return");
			kfree(row_data);
			row_data = NULL;
			return -EPERM;
		}
	}
	kfree(row_data);
	return j;
}

static int parse_string_from_csvfile(char *buf, size_t size,
		char *target_name, char *out_str, int out_len)
{

	char *ptr = NULL;
	int read_ret = size;
	char * line = NULL;

	if (!buf || !target_name || !out_str) {
		ts_err("buf or out_str is NULL");
		return -EINVAL;
	}

	if (read_ret > 0) {
		ptr = buf;
		ptr = strstr(ptr, target_name);
		if (!ptr) {
			ts_info("load %s failed 1, maybe not this item", target_name);
			return -EINTR;
		}

		goto_next_line(&ptr);
		if (!ptr || (0 == strlen(ptr))) {
			ts_err("load %s failed 2!", target_name);
			return -EIO;
		}
		line = (char *)kzalloc(MAX_LINE_LEN, GFP_KERNEL);
		if (!line) {
			ts_err("alloc bytes %d failed.", MAX_LINE_LEN);
			return -ENOMEM;
		}
		copy_this_line(line, ptr);
		snprintf(out_str, out_len, "%s", line);
		kfree(line);
		return 0;
	} else {
		ts_err("csv buffer size error");
		return -ENXIO;
	}
}

int get_limit_csv_file_version(struct goodix_ts_core *ts_core, char *limit_version)
{
	int ret;
	const struct firmware *firmware = NULL;
	struct device *dev = &ts_core->pdev->dev;
	char limit_file[100] = {0};
	char *temp_buf = NULL;

	sprintf(limit_file, "%s_%d.csv", ts_core->board_data.limit_csv_name,
			ts_core->fw_version.sensor_id);
	ts_info("limit_file_name:%s", limit_file);

	ret = request_firmware(&firmware, limit_file, dev);
	if (ret < 0) {
		ts_err("limits file [%s] not available", limit_file);
		return -EINVAL;
	}

	if (firmware->size <= 0) {
		ts_err("request_firmware, limits param length error,len:%zu",
				firmware->size);
		ret = -EINVAL;
		goto exit_free;
	}

	temp_buf = vzalloc(firmware->size + 1);
	if (!temp_buf) {
		ts_err("vzalloc bytes failed.");
		ret = -ENOMEM;
		goto exit_free;
	}

	memcpy(temp_buf, firmware->data, firmware->size);

	ret = parse_string_from_csvfile(temp_buf, firmware->size,
			CSV_TP_FILE_VERSION, limit_version, 30);

exit_free:
	if (temp_buf)
		vfree(temp_buf);
	if (firmware)
		release_firmware(firmware);
	return ret;
}

static int parse_csvfile(char *buf, size_t size, char *target_name, s32 *data, s32 rows, s32 col)
{
	int ret = 0;
	char *ptr = NULL;
	int read_ret;

	read_ret = size;
	if (read_ret > 0) {
		ptr = buf;
		ptr = strstr(ptr, target_name);
		if (!ptr) {
			ts_info("load %s failed 1, maybe not this item", target_name);
			return -EINTR;
		}

		goto_next_line(&ptr);
		if (!ptr || (0 == strlen(ptr))) {
			ts_err("load %s failed 2!", target_name);
			return -EIO;
		}

		if (data) {
			ret = parse_valid_data(buf, size, ptr, data, rows);
		} else {
			ts_err("load %s failed 3!", target_name);
			return -EINTR;
		}
	} else {
		ts_err("ret=%d, read_ret=%d", ret, read_ret);
		ret = -ENXIO;
	}

	return ret;
}

static void goodix_init_params(struct goodix_ts_test *ts_test)
{
	struct goodix_ts_core *ts = ts_test->ts;
	struct ts_test_params *test_params = &ts_test->test_params;

	test_params->rawdata_addr = ts->ic_info.misc.mutual_rawdata_addr;
	test_params->noisedata_addr = ts->ic_info.misc.mutual_diffdata_addr;
	test_params->self_rawdata_addr = ts->ic_info.misc.self_rawdata_addr;
	test_params->self_noisedata_addr = ts->ic_info.misc.self_diffdata_addr;

	test_params->drv_num = ts->ic_info.parm.drv_num;
	test_params->sen_num = ts->ic_info.parm.sen_num;

	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_A)
		test_params->params_info = &params_bra;
	else if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_B)
		test_params->params_info = &params_brb;
	else
		test_params->params_info = &params_brd;
}

static int goodix_init_testlimits(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	u32 data_buf[10] = {0};
	char *temp_buf = NULL;
	struct ts_test_params *test_params = &ts_test->test_params;
	struct goodix_ts_core *ts_core = ts_test->ts;
	const struct firmware *firmware = NULL;
	struct device *dev = &ts_core->pdev->dev;
	char limit_file[100] = {0};
	u32 tx = test_params->drv_num;
	u32 rx = test_params->sen_num;
	int freq_num = ts_core->ic_info.parm.mutual_freq_num;
	char tmp_str[64] = {0};
	int freq_cnt;
	bool flag = false;

	flag = goodix_get_ic_self_test_mode();

	//工厂自检默认都会传文件名
	if (flag) {
		ts_info("use default limit file");
		sprintf(limit_file, "%s_%d.csv", ts_core->board_data.limit_csv_name, ts_core->fw_version.sensor_id);
	} else {
		ts_info("use g_limit_file_name");
		sprintf(limit_file, "%s_%d.csv", g_limit_file_name, ts_core->fw_version.sensor_id);
	}
	ts_info("limit_file_name:%s", limit_file);

	ret = request_firmware(&firmware, limit_file, dev);
	if (ret < 0) {
		ts_err("limits file [%s] not available", limit_file);
		return -EINVAL;
	}
	if (firmware->size <= 0) {
		ts_err("request_firmware, limits param length error,len:%zu", firmware->size);
		ret = -EINVAL;
		goto exit_free;
	}
	temp_buf = vzalloc(firmware->size + 1);
	if (!temp_buf) {
		ts_err("vzalloc bytes failed.");
		ret = -ENOMEM;
		goto exit_free;
	}
	memcpy(temp_buf, firmware->data, firmware->size);

	test_params->test_items[GTP_VERSION_TEST] = true;
	test_params->test_items[GTP_CHIP_KEY_INFO_TEST] = true;
	test_params->test_items[GTP_CUSTOM_INFO_TEST] = true;
	test_params->test_items[GTP_RESET_TEST] = true;

	/* obtain config data */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_TEST_CONFIG, test_params->cfg_buf, 1, GOODIX_CFG_MAX_SIZE);
	if (ret < 0) {
		ts_info("Can't find %s", CSV_TP_TEST_CONFIG);
	} else {
		ts_info("parse_csvfile %s OK, cfg_len:%d", CSV_TP_TEST_CONFIG, ret);
		for (i = 0; i < ret; i++)
			ts_test->test_config.data[i] = (u8)test_params->cfg_buf[i];
		ts_test->test_config.len = ret;
		memcpy(g_test_config, ts_test->test_config.data, ts_test->test_config.len);
		g_test_config_len = ts_test->test_config.len;
	}

	/* obtain mutual_raw min */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SPECIAL_RAW_MIN, test_params->min_limits, rx, tx);
	if (ret < 0) {
		ts_err("Failed get min_limits");
		goto exit_free;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SPECIAL_RAW_MIN);
	}
	/* obtain mutual_raw max */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SPECIAL_RAW_MAX, test_params->max_limits, rx, tx);
	if (ret < 0) {
		ts_err("Failed get max_limits");
		goto exit_free;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SPECIAL_RAW_MAX);
	}
	/* obtain delta limit */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SPECIAL_RAW_DELTA, test_params->deviation_limits, rx, tx);
	if (ret < 0) {
		ts_err("Failed get delta limit");
		goto exit_free;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SPECIAL_RAW_DELTA);
	}

	/* obtain self_raw min */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SPECIAL_SELFRAW_MIN, test_params->self_min_limits, 1, tx + rx);
	/* obtain self_raw max */
	ret |= parse_csvfile(temp_buf, firmware->size, CSV_TP_SPECIAL_SELFRAW_MAX, test_params->self_max_limits, 1, tx + rx);
	if (ret < 0) {
		ts_info("Can't find self_min_max_limits, ship this item");
		ret = 0;
		test_params->test_items[GTP_SELFCAP_TEST] = false;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SPECIAL_SELFRAW_MIN);
		ts_info("parse_csvfile %s OK", CSV_TP_SPECIAL_SELFRAW_MAX);
		test_params->test_items[GTP_SELFCAP_TEST] = true;
	}

	/* obtain noise_threshold */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_NOISE_LIMIT,
			test_params->noise_threshold[0], rx, tx);
	if (ret < 0) {
		ts_info("Can't find noise_threshold, skip this item");
		ret = 0;
		test_params->test_items[GTP_NOISE_TEST] = false;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_NOISE_LIMIT);
		test_params->test_items[GTP_NOISE_TEST] = true;
		for (freq_cnt = 1; freq_cnt < freq_num; freq_cnt++) {
			memset(tmp_str, 0, sizeof(tmp_str));
			sprintf(tmp_str, "%s%d", CSV_TP_NOISE_LIMIT, freq_cnt);
			parse_csvfile(temp_buf, firmware->size, tmp_str,
				test_params->noise_threshold[freq_cnt], rx, tx);
		}
	}

	/* obtain self_noise_threshold */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SELFNOISE_LIMIT, &test_params->self_noise_threshold, 1, 1);
	if (ret < 0) {
		ts_info("Can't find self_noise_threshold, skip this item");
		ret = 0;
		test_params->test_items[GTP_SELFNOISE_TEST] = false;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SELFNOISE_LIMIT);
		test_params->test_items[GTP_SELFNOISE_TEST] = true;
	}

	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_ADC_DUMP_MAX_MIN,
			&test_params->adcdump_max_min_threshold, 1, 1);
	if (ret < 0) {
		ts_info("Can't find adcdump_max_min_threshold, use default val[%d]", 18);
		test_params->adcdump_max_min_threshold = 18;
	} else {
		ts_info("parse_csvfile %s OK, val[%d]", CSV_TP_ADC_DUMP_MAX_MIN,
				test_params->adcdump_max_min_threshold);
	}

	/* obtain short_params */
	ret = parse_csvfile(temp_buf, firmware->size, CSV_TP_SHORT_THRESHOLD, (s32 *)data_buf, 1, 7);
	if (ret < 0) {
		ts_info("Can't find short shortciurt_threshold, skip this item");
		ret = 0;
		test_params->test_items[GTP_SHORT_TEST] = false;
	} else {
		ts_info("parse_csvfile %s OK", CSV_TP_SHORT_THRESHOLD);
		test_params->test_items[GTP_SHORT_TEST] = true;
		test_params->short_threshold = data_buf[0];
		test_params->r_drv_drv_threshold = data_buf[1];
		test_params->r_drv_sen_threshold = data_buf[2];
		test_params->r_sen_sen_threshold = data_buf[3];
		test_params->r_drv_gnd_threshold = data_buf[4];
		test_params->r_sen_gnd_threshold = data_buf[5];
		test_params->avdd_value = data_buf[6];
	}

	ret = parse_csvfile(temp_buf, firmware->size, CSV_IP_SPECIAL_FREQ, &test_params->special_freq, 1, 1);
	if (ret < 0) {
		ts_info("Can't find special_freq, skip this item");
		ret = 0;
		test_params->test_items[GTP_SPECIAL_DELTA_TEST] = false;
	} else {
		ret = parse_csvfile(temp_buf, firmware->size, CSV_IP_SPECIAL_FREQ_DELTA, test_params->special_freq_deviation_limts, rx, tx);
		if (ret < 0) {
			ts_info("Can't find special_freq_delta, skip this item");
			ret = 0;
			test_params->test_items[GTP_SPECIAL_DELTA_TEST] = false;
		} else {
			ts_info("parse_csvfile %s OK", CSV_IP_SPECIAL_FREQ_DELTA);
			test_params->test_items[GTP_SPECIAL_DELTA_TEST] = true;
		}
	}

	test_params->test_items[GTP_DIFF_SHIFT_TEST] = false;
	memset(test_params->diff_shift_freq, 0, sizeof(test_params->diff_shift_freq));
	for (freq_cnt = 0, i = 0; i < freq_num; i++) {
		memset(tmp_str, 0, sizeof(tmp_str));
		sprintf(tmp_str, "%s%d", CSV_TP_DIFF_SHIFT_MAX, i);
		ret = parse_csvfile(temp_buf, firmware->size, tmp_str,
				test_params->diff_shift_max[i], rx, tx);
		if (ret == -EINTR)
			continue;

		if (ret < 0){
			ts_err("failed get freq diff shift max %d", i);
			goto exit_free;
		}

		ts_info("parse_csvfile %s, freq %d OK", CSV_TP_DIFF_SHIFT_MAX, i);
		test_params->diff_shift_freq[i] = 1;
		freq_cnt++;
	}
	ret = 0;
	if (freq_cnt) {
		test_params->diff_shift_test_freq_cnt = freq_cnt;
		test_params->test_items[GTP_DIFF_SHIFT_TEST] = true;
		ts_info("%s: test freq num %d", CSV_TP_DIFF_SHIFT_MAX, freq_cnt);
	}

exit_free:
	vfree(temp_buf);
	if (firmware)
		release_firmware(firmware);
	return ret;
}

static int goodix_tptest_prepare(struct goodix_ts_test *ts_test)
{
	int ret;

	ts_info("TP test prepare IN");

	goodix_init_params(ts_test);
	/* parse test limits from csv */
	ret = goodix_init_testlimits(ts_test);
	if (ret < 0) {
		ts_err("Failed to init testlimits from csv.");
		return ret;
	}

	/* disable irq */
	ts_test_irq_enable(ts_test, false);
#ifdef CONFIG_FACTORY_BUILD
	/* disable cmd*/
	ts_test_cmd_enable(false);
#endif
	/* close esd */
	goodix_ts_blocking_notify(NOTIFY_ESD_OFF, NULL);

	return 0;
}

static void goodix_tptest_finish(struct goodix_ts_test *ts_test)
{
	ts_info("TP test finish IN");
	/* reset chip */
	ts_test_reset(ts_test, 100);

	/* open esd */
	goodix_ts_blocking_notify(NOTIFY_ESD_ON, NULL);
	/* enable irq */
	ts_test_irq_enable(ts_test, true);
}

#define SHORT_TEST_RUN_REG 0x10400
#define SHORT_TEST_RUN_FLAG 0xAA
#define INSPECT_FW_SWITCH_CMD 0x85
#define TEST_FW_PID "OST"
static int goodix_short_test_prepare(struct goodix_ts_test *ts_test)
{
	struct goodix_ts_cmd tmp_cmd;
	struct goodix_fw_version fw_ver;
	int ret;
	int retry = 3;
	int resend = 3;
	u8 status;

	ts_info("short test prepare IN");
	ts_test->test_result[GTP_SHORT_TEST] = SYS_SOFTWARE_REASON;
	tmp_cmd.len = 4;
	tmp_cmd.cmd = INSPECT_FW_SWITCH_CMD;

resend_cmd:
	ret = ts_test_send_cmd(ts_test, &tmp_cmd);
	if (ret < 0) {
		ts_err("send test mode failed");
		return ret;
	}
	retry = 3;
	while (retry--) {
		msleep(40);
		if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_A) {
			ret = ts_test_read_version(ts_test, &fw_ver);
			if (ret < 0) {
				ts_err("read test version failed");
				return ret;
			}
			ret = memcmp(&(fw_ver.patch_pid[3]), TEST_FW_PID, strlen(TEST_FW_PID));
			if (ret == 0)
				return 0;
			else
				ts_info("patch ID dismatch %s != %s", fw_ver.patch_pid, TEST_FW_PID);
		} else {
			ret = ts_test_read(ts_test, SHORT_TEST_RUN_REG, &status, 1);
			if (!ret && status == SHORT_TEST_RUN_FLAG)
				return 0;
			ts_info("short_mode_status=0x%02x ret=%d", status, ret);
		}
	}

	if (resend--) {
		ts_test_reset(ts_test, 100);
		goto resend_cmd;
	}

	return -EINVAL;
}

static u32 map_die2pin(struct ts_test_params *test_params, u32 chn_num)
{
	int i = 0;
	u32 res = 255;

	if (chn_num & DRV_CHANNEL_FLAG)
		chn_num = (chn_num & ~DRV_CHANNEL_FLAG) + test_params->params_info->max_sen_num;

	for (i = 0; i < test_params->params_info->max_sen_num; i++) {
		if (test_params->params_info->sen_map[i] == chn_num) {
			res = i;
			break;
		}
	}
	/* res != 255 mean found the corresponding channel num */
	if (res != 255)
		return res;
	/* if cannot find in SenMap try find in DrvMap */
	for (i = 0; i < test_params->params_info->max_drv_num; i++) {
		if (test_params->params_info->drv_map[i] == chn_num) {
			res = i;
			break;
		}
	}
	if (i >= test_params->params_info->max_drv_num)
		ts_err("Faild found corrresponding channel num:%d", chn_num);
	else
		res |= DRV_CHANNEL_FLAG;

	return res;
}

static void goodix_save_short_res(struct ts_test_params *params, u16 chn1, u16 chn2, int r)
{
	int i;
	u8 repeat_cnt = 0;
	u8 repeat = 0;
	struct goodix_ts_test *ts_test = container_of(params, struct goodix_ts_test, test_params);
	struct ts_short_res *short_res = &ts_test->short_res;

	if (chn1 == chn2 || short_res->short_num >= MAX_SHORT_NUM)
		return;

	for (i = 0; i < short_res->short_num; i++) {
		repeat_cnt = 0;
		if (short_res->short_msg[4 * i] == chn1)
			repeat_cnt++;
		if (short_res->short_msg[4 * i] == chn2)
			repeat_cnt++;
		if (short_res->short_msg[4 * i + 1] == chn1)
			repeat_cnt++;
		if (short_res->short_msg[4 * i + 1] == chn2)
			repeat_cnt++;
		if (repeat_cnt >= 2) {
			repeat = 1;
			break;
		}
	}
	if (repeat == 0) {
		short_res->short_msg[4 * short_res->short_num + 0] = chn1;
		short_res->short_msg[4 * short_res->short_num + 1] = chn2;
		short_res->short_msg[4 * short_res->short_num + 2] = (r >> 8) & 0xFF;
		short_res->short_msg[4 * short_res->short_num + 3] = r & 0xFF;
		if (short_res->short_num < MAX_SHORT_NUM)
			short_res->short_num++;
	}
}

static int gdix_check_tx_tx_shortcircut(struct goodix_ts_test *ts_test, u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf;
	u32 data_reg;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_drv_num = test_params->params_info->max_drv_num;
	int max_sen_num = test_params->params_info->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_drv_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		ts_err("memory failed");
		return -ENOMEM;
	}
	/* drv&drv shortcircut check */
	data_reg = test_params->params_info->drv_drv_selfcode_reg;
	for (i = 0; i < short_ch_num; i++) {
		ret = ts_test_read(ts_test, data_reg, data_buf, size);
		if (ret < 0) {
			ts_err("Failed read Drv-to-Drv short rawdata");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			ts_err("Drv-to-Drv adc data checksum error");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_drv_drv_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		short_die_num -= max_sen_num;
		if (short_die_num >= max_drv_num) {
			ts_info("invalid short pad num:%d", short_die_num + max_sen_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			ts_info("invalid self_capdata:0x%x", self_capdata);
			continue;
		}

		for (j = short_die_num + 1; j < max_drv_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(ts_test, self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num = map_die2pin(test_params, short_die_num + max_sen_num);
				slave_pin_num = map_die2pin(test_params, j + max_sen_num);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					ts_info("WARNNING invalid pin");
					continue;
				}
				goodix_save_short_res(test_params, master_pin_num, slave_pin_num, short_r);
				ts_err("short circut:R=%dK,R_Threshold=%dK", short_r, r_threshold);
				ts_err("%s%d--%s%d shortcircut", (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (master_pin_num & ~DRV_CHANNEL_FLAG),
					   (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

static int gdix_check_rx_rx_shortcircut(struct goodix_ts_test *ts_test, u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf;
	u32 data_reg;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_sen_num = test_params->params_info->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_sen_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		ts_err("memory failed");
		return -ENOMEM;
	}
	/* drv&drv shortcircut check */
	data_reg = test_params->params_info->sen_sen_selfcode_reg;
	for (i = 0; i < short_ch_num; i++) {
		ret = ts_test_read(ts_test, data_reg, data_buf, size);
		if (ret) {
			ts_err("Failed read Sen-to-Sen short rawdata");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			ts_err("Sen-to-Sen adc data checksum error");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_sen_sen_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		if (short_die_num >= max_sen_num) {
			ts_info("invalid short pad num:%d", short_die_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			ts_info("invalid self_capdata:0x%x", self_capdata);
			continue;
		}

		for (j = short_die_num + 1; j < max_sen_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(ts_test, self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num = map_die2pin(test_params, short_die_num);
				slave_pin_num = map_die2pin(test_params, j);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					ts_info("WARNNING invalid pin");
					continue;
				}
				goodix_save_short_res(test_params, master_pin_num, slave_pin_num, short_r);
				ts_err("short circut:R=%dK,R_Threshold=%dK", short_r, r_threshold);
				ts_err("%s%d--%s%d shortcircut", (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (master_pin_num & ~DRV_CHANNEL_FLAG),
					   (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

static int gdix_check_tx_rx_shortcircut(struct goodix_ts_test *ts_test, u8 short_ch_num)
{
	int ret = 0, err = 0;
	u32 r_threshold = 0, short_r = 0;
	int size = 0, i = 0, j = 0;
	u16 adc_signal = 0;
	u8 master_pin_num, slave_pin_num;
	u8 *data_buf = NULL;
	u32 data_reg;
	struct ts_test_params *test_params = &ts_test->test_params;
	int max_drv_num = test_params->params_info->max_drv_num;
	int max_sen_num = test_params->params_info->max_sen_num;
	u16 self_capdata, short_die_num = 0;

	size = 4 + max_drv_num * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		ts_err("memory failed");
		return -ENOMEM;
	}
	/* drv&sen shortcircut check */
	data_reg = test_params->params_info->drv_sen_selfcode_reg;
	for (i = 0; i < short_ch_num; i++) {
		ret = ts_test_read(ts_test, data_reg, data_buf, size);
		if (ret) {
			ts_err("Failed read Drv-to-Sen short rawdata");
			err = -EINVAL;
			break;
		}

		if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
			ts_err("Drv-to-Sen adc data checksum error");
			err = -EINVAL;
			break;
		}

		r_threshold = test_params->r_drv_sen_threshold;
		short_die_num = le16_to_cpup((__le16 *)&data_buf[0]);
		if (short_die_num >= max_sen_num) {
			ts_info("invalid short pad num:%d", short_die_num);
			continue;
		}

		/* TODO: j start position need recheck */
		self_capdata = le16_to_cpup((__le16 *)&data_buf[2]);
		if (self_capdata == 0xffff || self_capdata == 0) {
			ts_info("invalid self_capdata:0x%x", self_capdata);
			continue;
		}

		for (j = 0; j < max_drv_num; j++) {
			adc_signal = le16_to_cpup((__le16 *)&data_buf[4 + j * 2]);

			if (adc_signal < test_params->short_threshold)
				continue;

			short_r = (u32)cal_cha_to_cha_res(ts_test, self_capdata, adc_signal);
			if (short_r < r_threshold) {
				master_pin_num = map_die2pin(test_params, short_die_num);
				slave_pin_num = map_die2pin(test_params, j + max_sen_num);
				if (master_pin_num == 0xFF || slave_pin_num == 0xFF) {
					ts_info("WARNNING invalid pin");
					continue;
				}
				goodix_save_short_res(test_params, master_pin_num, slave_pin_num, short_r);
				ts_err("short circut:R=%dK,R_Threshold=%dK", short_r, r_threshold);
				ts_err("%s%d--%s%d shortcircut", (master_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (master_pin_num & ~DRV_CHANNEL_FLAG),
					   (slave_pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (slave_pin_num & ~DRV_CHANNEL_FLAG));
				err = -EINVAL;
			}
		}
		data_reg += size;
	}

	kfree(data_buf);
	return err;
}

#define SHORT_TYPE_FLAG ((uint16_t)1 << 15)
static int gdix_check_resistance_to_gnd(struct ts_test_params *test_params, u16 adc_signal, u32 pos)
{
	long r = 0;
	u16 r_th = 0, avdd_value = 0;
	u16 chn_id_tmp = 0;
	u8 pin_num = 0;
	unsigned short short_type;
	struct goodix_ts_test *ts_test = container_of(test_params, struct goodix_ts_test, test_params);
	int max_drv_num = test_params->params_info->max_drv_num;
	int max_sen_num = test_params->params_info->max_sen_num;

	avdd_value = test_params->avdd_value;
	short_type = adc_signal & 0x8000;
	adc_signal &= ~0x8000;
	if (adc_signal == 0)
		adc_signal = 1;

	if (short_type == 0) {
		/* short to GND */
		r = cal_cha_to_gnd_res(ts_test, adc_signal);
	} else {
		/* short to VDD */
		r = cal_cha_to_avdd_res(ts_test, adc_signal, avdd_value);
	}

	if (pos < max_drv_num)
		r_th = test_params->r_drv_gnd_threshold;
	else
		r_th = test_params->r_sen_gnd_threshold;

	chn_id_tmp = pos;
	if (chn_id_tmp < max_drv_num)
		chn_id_tmp += max_sen_num;
	else
		chn_id_tmp -= max_drv_num;

	if (r < r_th) {
		pin_num = map_die2pin(test_params, chn_id_tmp);
		goodix_save_short_res(test_params, pin_num, short_type ? CHN_VDD : CHN_GND, r);
		ts_err("%s%d shortcircut to %s,R=%ldK,R_Threshold=%dK", (pin_num & DRV_CHANNEL_FLAG) ? "DRV" : "SEN", (pin_num & ~DRV_CHANNEL_FLAG),
			   short_type ? "VDD" : "GND", r, r_th);

		return -EINVAL;
	}

	return 0;
}

static int gdix_check_gndvdd_shortcircut(struct goodix_ts_test *ts_test)
{
	int ret = 0, err = 0;
	int size = 0, i = 0;
	u16 adc_signal = 0;
	u32 data_reg;
	u8 *data_buf = NULL;
	int max_drv_num = ts_test->test_params.params_info->max_drv_num;
	int max_sen_num = ts_test->test_params.params_info->max_sen_num;

	size = (max_drv_num + max_sen_num) * 2 + 2;
	data_buf = kzalloc(size, GFP_KERNEL);
	if (!data_buf) {
		ts_err("memory failed");
		return -ENOMEM;
	}
	/* read diff code, diff code will be used to calculate
	 * resistance between channel and GND */
	data_reg = ts_test->test_params.params_info->diffcode_data_reg;
	ret = ts_test_read(ts_test, data_reg, data_buf, size);
	if (ret < 0) {
		ts_err("Failed read to-gnd rawdata");
		err = -EINVAL;
		goto err_out;
	}

	if (checksum_cmp(data_buf, size, CHECKSUM_MODE_U8_LE)) {
		ts_err("diff code checksum error");
		err = -EINVAL;
		goto err_out;
	}

	for (i = 0; i < max_drv_num + max_sen_num; i++) {
		adc_signal = le16_to_cpup((__le16 *)&data_buf[i * 2]);
		ret = gdix_check_resistance_to_gnd(&ts_test->test_params, adc_signal, i);
		if (ret != 0) {
			ts_err("Resistance to-gnd/vdd short");
			err = ret;
		}
	}

err_out:
	kfree(data_buf);
	return err;
}

static int goodix_shortcircut_analysis(struct goodix_ts_test *ts_test)
{
	int ret;
	int err = 0;
	test_result_t test_result;

	ret = ts_test_read(ts_test, ts_test->test_params.params_info->short_test_result_reg, (u8 *)&test_result, sizeof(test_result));
	if (ret < 0) {
		ts_err("Read TEST_RESULT_REG failed");
		return ret;
	}

	if (checksum_cmp((u8 *)&test_result, sizeof(test_result), CHECKSUM_MODE_U8_LE)) {
		ts_err("shrot result checksum err");
		return -EINVAL;
	}

	if (!(test_result.result & 0x0F)) {
		ts_info(">>>>> No shortcircut");
		return 0;
	}
	ts_info("short flag 0x%02x, drv&drv:%d, sen&sen:%d, drv&sen:%d, drv/GNDVDD:%d, sen/GNDVDD:%d", test_result.result,
			test_result.drv_drv_num, test_result.sen_sen_num, test_result.drv_sen_num, test_result.drv_gnd_avdd_num,
			test_result.sen_gnd_avdd_num);

	if (test_result.drv_drv_num)
		err |= gdix_check_tx_tx_shortcircut(ts_test, test_result.drv_drv_num);
	if (test_result.sen_sen_num)
		err |= gdix_check_rx_rx_shortcircut(ts_test, test_result.sen_sen_num);
	if (test_result.drv_sen_num)
		err |= gdix_check_tx_rx_shortcircut(ts_test, test_result.drv_sen_num);
	if (test_result.drv_gnd_avdd_num || test_result.sen_gnd_avdd_num)
		err |= gdix_check_gndvdd_shortcircut(ts_test);

	ts_info(">>>>> short check return 0x%x", err);

	return err;
}

#define SHORT_FW_CMD_REG 0x10400
static int send_test_cmd(struct goodix_ts_test *ts_test, struct goodix_ts_cmd *cmd)
{
	int ret;
	u32 reg = SHORT_FW_CMD_REG;
	cmd->state = 0;
	cmd->ack = 0;
	goodix_append_checksum(&(cmd->buf[2]), cmd->len - 2, CHECKSUM_MODE_U8_LE);
	ret = ts_test_write(ts_test, reg, cmd->buf, cmd->len + 2);
	if (ret < 0)
		return ret;
	usleep_range(10000, 11000);
	return ret;
}

enum SHORT_ADC_CHK_FLG
{
    ADC_NEG_SAT_PASS = -2048,//means channel adc is ok
    ADC_FOR_SAT_PASS = 2047,//means channel adc is ok
    ADC_NO_USE1_FLG = 32767,//means channel adc not use
    ADC_NO_USE2_FLG = -32768,//means channel adc not use
};
#define BLND_ADC_HARD_CHECK_ADDR          0x15100
static int goodix_check_chn_adc(struct goodix_ts_test *ts_test)
{
	u8 p_buf[320];
	int length = MAX_SEN_NUM_BRD * 2 * 3 + 2;
	u16 byte_len = MAX_SEN_NUM_BRD * 2;
	u8* p_negative_b;
	u8* p_forward_b;
	u8* p_adcdump_b;
	int i;
	const u8 data_len = MAX_SEN_NUM_BRD;
	const u8 adc_grp_chn = 5;//fix
	//five channels use one ADC unit 
	u8 adc_mm_ng[30] = { 0 };
	u8 adc_id = 0;
	u8 adc_grp_t;
	s16 tmp_val = 0;
	s16 *adc_negative_sta = ts_test->adc_negative_sta;
	s16 *adc_forward_sta = ts_test->adc_forward_sta;
	s16 *adc_dump_mm = ts_test->adc_dump_mm;
	int adcdump_max_min_threshold = ts_test->test_params.adcdump_max_min_threshold;

	if (ts_test->ts->bus->ic_type != IC_TYPE_BERLIN_D)
		return 0;

	ts_test_read(ts_test, BLND_ADC_HARD_CHECK_ADDR, p_buf, length);

	if (checksum_cmp(p_buf, length, CHECKSUM_MODE_U8_LE)) {
		ts_err("adc dump data checksum error");
		return -1;
	}

	ts_test->b_adc_mm_ng = 0;
	ts_test->b_adc_negative_sta_ng = 0;
	ts_test->b_adc_forward_sta_ng = 0;

    //Negative Saturation byte:
    p_negative_b = p_buf;
    for (i = 0; i < byte_len; i += 2) {
        tmp_val = p_negative_b[i] + (p_negative_b[i + 1] << 8);
        adc_negative_sta[i / 2] = tmp_val;
    }

    //Forward Saturation byte :
    p_forward_b = p_buf + byte_len;
    for (i = 0; i < byte_len; i += 2) {
        tmp_val = p_forward_b[i] + (p_forward_b[i + 1] << 8);
        adc_forward_sta[i / 2] = tmp_val;
    }

    //ADC dump byte, max - min :
    p_adcdump_b = p_buf + (byte_len * 2);
    for (i = 0; i < byte_len; i += 2) {
        tmp_val = p_adcdump_b[i] + (p_adcdump_b[i + 1] << 8);
        adc_dump_mm[i / 2] = tmp_val;
    }

	for (i = 0; i < data_len; i++) {
		tmp_val = adc_negative_sta[i];
		if (tmp_val != ADC_NEG_SAT_PASS && tmp_val != ADC_NO_USE1_FLG) {
			ts_test->b_adc_negative_sta_ng = 1;
		}

		tmp_val = adc_forward_sta[i];
		if (tmp_val != ADC_FOR_SAT_PASS && tmp_val != ADC_NO_USE2_FLG) {
			ts_test->b_adc_forward_sta_ng = 1;
		}

		tmp_val = adc_dump_mm[i];
		if (tmp_val != ADC_NO_USE2_FLG) {
			if (tmp_val == 0) {
				ts_test->b_adc_mm_ng = 1;
			}
			if (tmp_val > adcdump_max_min_threshold) {
				adc_mm_ng[adc_id]++;
			}
		}

		if ((i != 0) && ((i + 1) % adc_grp_chn == 0))
			adc_id++;
	}

	adc_grp_t = (data_len / adc_grp_chn);
	for (adc_id = 0; adc_id < adc_grp_t && adc_id < 30; adc_id++) {
		if (adc_mm_ng[adc_id] >= 2)
			ts_test->b_adc_mm_ng = 1;
	}

	if (ts_test->b_adc_negative_sta_ng) {
		ts_err("b_adc_negative_sta_ng error");
		return -1;
	}

	if (ts_test->b_adc_forward_sta_ng) {
		ts_err("b_adc_forward_sta_ng error");
		return -1;
	}

	if (ts_test->b_adc_mm_ng) {
		ts_err("b_adc_mm_ng error");
		return -1;
	}

	return 0;
}

#define INSPECT_PARAM_CMD 0xAA
#define SHORT_TEST_FINISH_FLAG 0x88
#define SHORT_TEST_THRESHOLD_REG 0x20402
static void goodix_shortcircut_test(struct goodix_ts_test *ts_test)
{
	int ret = 0;
	int retry;
	u16 test_time;
	u8 status;
	int ic_type = ts_test->ts->bus->ic_type;
	struct goodix_ts_cmd test_parm_cmd;
	/*u8 test_param[6];*/

	ts_info("---------------------- short_test begin ----------------------");
	ret = goodix_short_test_prepare(ts_test);
	if (ret < 0) {
		ts_err("Failed enter short test mode");
		return;
	}

	/* get short test time */

	ret = ts_test_read(ts_test, ts_test->test_params.params_info->short_test_time_reg, (u8 *)&test_time, 2);
	if (ret < 0) {
		ts_err("Failed to get test_time, default %dms", DEFAULT_TEST_TIME_MS);
		test_time = DEFAULT_TEST_TIME_MS;
	} else {
		if (ic_type == IC_TYPE_BERLIN_A)
			test_time /= 10;
		if (test_time > MAX_TEST_TIME_MS) {
			ts_info("test time too long %d > %d", test_time, MAX_TEST_TIME_MS);
			test_time = MAX_TEST_TIME_MS;
		}
	}

	ts_info("get test time %dms", test_time);
	/* start short test */
	if (ic_type == IC_TYPE_BERLIN_A) {
		test_parm_cmd.len = 0x0A;
		test_parm_cmd.cmd = INSPECT_PARAM_CMD;
		test_parm_cmd.data[0] = ts_test->test_params.params_info->dft_short_threshold & 0xFF;
		test_parm_cmd.data[1] = (ts_test->test_params.params_info->dft_short_threshold >> 8) & 0xFF;
		test_parm_cmd.data[2] = ts_test->test_params.params_info->short_diffcode_threshold & 0xFF;
		test_parm_cmd.data[3] = (ts_test->test_params.params_info->short_diffcode_threshold >> 8) & 0xFF;
		test_parm_cmd.data[4] = ts_test->test_params.params_info->short_test_dump_num & 0xFF;
		test_parm_cmd.data[5] = (ts_test->test_params.params_info->short_test_dump_num >> 8) & 0xFF;
		ret = send_test_cmd(ts_test, &test_parm_cmd);
		if (ret < 0) {
			ts_err("send INSPECT_PARAM_CMD failed");
			return;
		}
	} else {
		/*
		test_param[0] = ts_test->test_params.params_info->dft_short_threshold & 0xFF;
		test_param[1] = (ts_test->test_params.params_info->dft_short_threshold >> 8) & 0xFF;
		test_param[2] = ts_test->test_params.params_info->short_diffcode_threshold & 0xFF;
		test_param[3] = (ts_test->test_params.params_info->short_diffcode_threshold >> 8) & 0xFF;
		test_param[4] = ts_test->test_params.params_info->short_test_dump_num & 0xFF;
		test_param[5] = (ts_test->test_params.params_info->short_test_dump_num >> 8) & 0xFF;
		ts_test_write(ts_test, SHORT_TEST_THRESHOLD_REG, test_param, sizeof(test_param));
		*/
		status = 0;
		ts_test_write(ts_test, SHORT_TEST_RUN_REG, &status, 1);
	}

	/* wait short test finish */
	msleep(test_time);
	retry = 50;
	while (retry--) {
		ret = ts_test_read(ts_test, ts_test->test_params.params_info->short_test_status_reg, &status, 1);
		if (!ret && status == SHORT_TEST_FINISH_FLAG)
			break;
		msleep(50);
	}
	if (retry < 0) {
		ts_err("short test failed, status:0x%02x", status);
		return;
	}

	/* start analysis short result */
	ts_info("short_test finished, start analysis");
	ret = goodix_shortcircut_analysis(ts_test);
	if (ret < 0) {
		ts_test->test_result[GTP_SHORT_TEST] = GTP_PANEL_REASON;
		ts_err("shortcircut analysis failed");
		return;
	}

	ret = goodix_check_chn_adc(ts_test);
	if (ret < 0) {
		ts_test->test_result[GTP_SHORT_TEST] = GTP_PANEL_REASON;
		ts_err("check chn adc failed");
		return;
	}
	ts_test->test_result[GTP_SHORT_TEST] = GTP_TEST_PASS;
}

#define GOODIX_CMD_RAWDATA 2
#define GOODIX_TOUCH_EVENT 0x80
static int goodix_cap_test_prepare(struct goodix_ts_test *ts_test)
{
	int ret;
	struct goodix_ts_cmd temp_cmd;
	struct goodix_ic_config *cfg = &ts_test->test_config;

	ts_info("cap test prepare IN");
	ts_test->test_result[GTP_CAP_TEST] = SYS_SOFTWARE_REASON;
	ts_test->test_result[GTP_DELTA_TEST] = SYS_SOFTWARE_REASON;
	if (ts_test->test_params.test_items[GTP_SELFCAP_TEST])
		ts_test->test_result[GTP_SELFCAP_TEST] = SYS_SOFTWARE_REASON;
	if (ts_test->test_params.test_items[GTP_NOISE_TEST])
		ts_test->test_result[GTP_NOISE_TEST] = SYS_SOFTWARE_REASON;
	if (ts_test->test_params.test_items[GTP_SELFNOISE_TEST])
		ts_test->test_result[GTP_SELFNOISE_TEST] = SYS_SOFTWARE_REASON;

	/* send test config if exist */
	if (cfg->len > 0) {
		ts_info("Test config exists and send it");
			ret = ts_test_send_config(ts_test, (u8 *)cfg->data, (u32)cfg->len);
			if (ret < 0) {
				ts_err("Send test config failed, exit");
			return ret;
		}
	}

	/* switch rawdata mode */
	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_D) {
		temp_cmd.cmd = 0x90;
		temp_cmd.data[0] = 0x81;
		temp_cmd.len = 5;
	} else {
		temp_cmd.cmd = GOODIX_CMD_RAWDATA;
		temp_cmd.len = 4;
	}
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0)
		ts_err("Enter rawdata mode failed");

	return ret;
}

static int goodix_cap_test_finish(struct goodix_ts_test *ts_test)
{
	ts_info("cap_test finished");
	/* switch coor mode */
	ts_test_reset(ts_test, 200);
	return 0;
}

// To check
#define FRAME_DATA_MAX_SIZE 2500
static int goodix_poll_rawdata(struct goodix_ts_test *ts_test, u8 data_type, u8 *data, u32 data_num)
{
	struct goodix_ts_core *cd = ts_test->ts;
	u32 flag_addr = ts_test->ts->ic_info.misc.touch_data_addr;
	u32 data_addr;
	u8 *frame_buf;
	int offset = 0;
	int frame_len;
	u8 val;
	int ret;
	int retry;

	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_D)
		flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;

	if (data_type == GTP_MUTUAL_RAW) {
		data_addr = ts_test->test_params.rawdata_addr;
	} else if (data_type == GTP_MUTUAL_DIFF) {
		data_addr = ts_test->test_params.noisedata_addr;
	} else if (data_type == GTP_SELF_RAW) {
		data_addr = ts_test->test_params.self_rawdata_addr;
	} else {
		data_addr = ts_test->test_params.self_noisedata_addr;
	}

	val = 0;
	ret = ts_test_write(ts_test, flag_addr, &val, 1);
	if (ret < 0)
		return ret;
	retry = 20;
	while (retry--) {
		usleep_range(5000, 5100);
		ret = ts_test_read(ts_test, flag_addr, &val, 1);
		if (!ret && (val & 0x80))
			break;
	}
	if (retry < 0) {
		ts_err("rawdata is not ready val:0x%02x", val);
		return -EAGAIN;
	}

	frame_buf = kzalloc(FRAME_DATA_MAX_SIZE, GFP_KERNEL);
	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		ret = ts_test_read(ts_test, flag_addr, frame_buf, FRAME_DATA_MAX_SIZE);
		if (ret < 0)
			goto free_exit;

		if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
			ts_err("frame head checksum error");
			ret = -EAGAIN;
			goto free_exit;
		}

		frame_len = le16_to_cpup((__le16 *)&frame_buf[3]);
		if (checksum_cmp(frame_buf, frame_len, CHECKSUM_MODE_U16_LE)) {
			ts_err("frame body checksum error");
			ret = -EAGAIN;
			goto free_exit;
		}

		offset += cd->ic_info.misc.frame_data_head_len;
		offset += cd->ic_info.misc.fw_attr_len;
		offset += cd->ic_info.misc.fw_log_len;
		if (data_type == GTP_MUTUAL_RAW || data_type == GTP_MUTUAL_DIFF) {
			offset += 8;
		} else {
			offset += cd->ic_info.misc.mutual_struct_len;
			offset += 10;
		}
		memcpy(data, frame_buf + offset, data_num * 2);
	} else {
		ret = ts_test_read(ts_test, data_addr, data, data_num * 2);
	}

free_exit:
	kfree(frame_buf);
	return ret;
}

static int goodix_cache_rawdata(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	int retry;
	u8 val;
	static unsigned char frame_buf[FRAME_DATA_MAX_LEN];
	unsigned char *cur_ptr;
	struct frame_head *frame_head;
	struct goodix_ts_core *cd = ts_test->ts;
	u32 sen_num = ts_test->test_params.sen_num;
	u32 drv_num = ts_test->test_params.drv_num;
	u32 data_size = sen_num * drv_num;
	u32 data_addr = ts_test->test_params.rawdata_addr;
	u32 flag_addr = ts_test->ts->ic_info.misc.touch_data_addr;

	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_D)
		flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;

	for (i = 0; i < DISCARD_FRAMES; i++) {
		val = 0;
		ts_test_write(ts_test, flag_addr, &val, 1);
		msleep(20);
	}

	for (i = 0; i < TOTAL_FRAME_NUM; i++) {
		val = 0;
		ret = ts_test_write(ts_test, flag_addr, &val, 1);
		if (ret < 0) {
			ts_err("clean touch event failed, exit");
			return -EAGAIN;
		}
		retry = 20;
		while (retry--) {
			usleep_range(5000, 5100);
			ret = ts_test_read(ts_test, flag_addr, &val, 1);
			if (!ret && (val & 0x80))
				break;
		}
		if (retry < 0) {
			ts_err("rawdata is not ready val:0x%02x i:%d, exit", val, i);
			return -EAGAIN;
		}

		if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
			ret = ts_test_read(ts_test, flag_addr, frame_buf, sizeof(frame_buf));
			if (ret < 0)
				return ret;
			if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
				ts_err("frame head checksum error");
				return -EINVAL;
			}
			frame_head = (struct frame_head *)frame_buf;
			if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
				ts_err("frame body checksum error");
				return -EINVAL;
			}
			cur_ptr = frame_buf;
			cur_ptr += cd->ic_info.misc.frame_data_head_len;
			cur_ptr += cd->ic_info.misc.fw_attr_len;
			cur_ptr += cd->ic_info.misc.fw_log_len;
			memcpy((u8 *)ts_test->rawdata[i].data, cur_ptr + 8, data_size * 2);
		} else {
			ret = ts_test_read(ts_test, data_addr, (u8 *)ts_test->rawdata[i].data, data_size * sizeof(s16));
			if (ret < 0)
				return ret;
		}
		ts_test->rawdata[i].size = data_size;
		goodix_rotate_abcd2cbad(drv_num, sen_num, ts_test->rawdata[i].data);
	}

	return ret;
}

static void goodix_cache_deltadata(struct goodix_ts_test *ts_test)
{
	u32 data_size;
	int tx = ts_test->test_params.drv_num;
	int i;
	int j;
	int max_val;
	int raw;
	int temp;

	for (i = 0; i < TOTAL_FRAME_NUM; i++) {
		data_size = ts_test->rawdata[i].size;
		if (data_size == 0)
			continue;
		for (j = 0; j < data_size; j++) {
			raw = ts_test->rawdata[i].data[j];
			max_val = 0;
			/* calcu delta with above node */
			if (j - tx >= 0) {
				temp = ts_test->rawdata[i].data[j - tx];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with bellow node */
			if (j + tx < data_size) {
				temp = ts_test->rawdata[i].data[j + tx];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with left node */
			if (j % tx) {
				temp = ts_test->rawdata[i].data[j - 1];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			/* calcu delta with right node */
			if ((j + 1) % tx) {
				temp = ts_test->rawdata[i].data[j + 1];
				temp = ABS(temp - raw);
				max_val = MAX(max_val, temp);
			}
			ts_test->accord_arr[i].data[j] = max_val * 1000 / raw;
		}
		ts_test->accord_arr[i].size = data_size;
	}
}

static int goodix_cache_self_rawdata(struct goodix_ts_test *ts_test)
{
	int ret;
	u32 sen_num = ts_test->test_params.sen_num;
	u32 drv_num = ts_test->test_params.drv_num;
	u32 data_size = sen_num + drv_num;
	u32 data_addr = ts_test->test_params.self_rawdata_addr;
	u32 flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;
	struct frame_head *frame_head;
	struct goodix_ts_core *cd = ts_test->ts;
	static unsigned char frame_buf[FRAME_DATA_MAX_LEN];
	unsigned char *cur_ptr;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		ret = ts_test_read(ts_test, flag_addr, frame_buf, sizeof(frame_buf));
		if (ret < 0)
			return ret;
		if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
			ts_err("frame head checksum error");
			return -EINVAL;
		}
		frame_head = (struct frame_head *)frame_buf;
		if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
			ts_err("frame body checksum error");
			return -EINVAL;
		}
		cur_ptr = frame_buf;
		cur_ptr += cd->ic_info.misc.frame_data_head_len;
		cur_ptr += cd->ic_info.misc.fw_attr_len;
		cur_ptr += cd->ic_info.misc.fw_log_len;
		cur_ptr += cd->ic_info.misc.mutual_struct_len;
		memcpy((u8 *)ts_test->self_rawdata.data, cur_ptr + 10, data_size * 2);
	} else {
		ret = ts_test_read(ts_test, data_addr, (u8 *)ts_test->self_rawdata.data, data_size * sizeof(s16));
		if (ret < 0)
			return ret;
	}
	ts_test->self_rawdata.size = data_size;

	return ret;
}

enum TEST_STAGE {
	TEST_STAGE_NONE = 0,
	TEST_STAGE_PREPARE,
	TEST_STAGE_OPEN_SHORT_S,
	TEST_STAGE_OPEN_SHORT_E,
	TEST_STAGE_MALLOC_FAIL,
	TEST_STAGE_FINISH = 20,
};

int g_test_stage = 0;

#define DATA_MODE_RAW  1
#define DATA_MODE_DIFF 2
static int goodix_switch_data_mode(struct goodix_ts_test *ts_test, int mode)
{
	struct goodix_ts_cmd temp_cmd;

	if (ts_test->ts->bus->ic_type != IC_TYPE_BERLIN_D) {
		temp_cmd.cmd = GOODIX_CMD_RAWDATA;
		temp_cmd.len = 4;
		return ts_test_send_cmd(ts_test, &temp_cmd);
	}

	if (mode == DATA_MODE_RAW)
		temp_cmd.data[0] = 0x81;
	else if (mode == DATA_MODE_DIFF)
		temp_cmd.data[0] = 0x82;
	else
		return -EINVAL;

	temp_cmd.cmd = 0x90;
	temp_cmd.len = 5;
	return ts_test_send_cmd(ts_test, &temp_cmd);
}

int goodix_diff_shift_test_start(struct goodix_ts_core *cd, int freq_index)
{
	int ret;
	struct goodix_ts_cmd temp_cmd;
	struct goodix_ts_test *ts_test = g_ts_test;

	if (!ts_test) {
		ts_err("test work does not init");
		return -EINVAL;
	}

	if (ts_test->test_params.diff_shift_test_freq_cnt == 0 ||
			!ts_test->test_params.diff_shift_freq[freq_index]) {
		ts_err("freq %d diff shift test not enabled", freq_index);
		return -EINVAL;
	}

	/* diff shift test prepare */
	if (freq_index == 0) {
		goodix_ts_esd_off(ts_test->ts);
		ts_test_irq_enable(ts_test, false);
		cd->is_inspecting = true;
		if (g_test_config_len > 0) {
			ts_info("send test config");
			ts_test_send_config(ts_test, g_test_config, g_test_config_len);
		}
	}

	// send diff data cmd
	ret = goodix_switch_data_mode(ts_test, DATA_MODE_DIFF);
	if (ret < 0) {
		ts_err("switch diffdata mode failed, exit!");
		return ret;
	}
	ts_info("start diff shift test on freq %d", freq_index);
	// send switch to freq_index cmd
	temp_cmd.cmd = 0x9C;
	temp_cmd.data[0] = freq_index;
	temp_cmd.len = 5;
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0) {
		ts_err("switch freq index:%d failed", freq_index);
		return ret;
	}
	//  delay 10ms
	msleep(10);
	// send diff test start cmd
	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x88;
	temp_cmd.data[1] = 0x1;
	temp_cmd.len = 6;
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0) {
		ts_err("send start diff test cmd failed");
		return ret;
	}
	return 0;
}

static int goodix_diff_shift_result_check(struct goodix_ts_test *ts_test)
{
	int i;
	int freq_index;
	int error_node_count;
	s16 val;
	s32 sen_num = ts_test->test_params.sen_num;
	s32 drv_num = ts_test->test_params.drv_num;
	u32 data_size;

	if (!ts_test->test_params.test_items[GTP_DIFF_SHIFT_TEST]) {
		/* diff shift test not enable */
		return 0;
	}

	ts_test->test_result[GTP_DIFF_SHIFT_TEST] = SYS_SOFTWARE_REASON;

	data_size = sen_num * drv_num;
	error_node_count = 0;
	for (freq_index = 0; freq_index < MAX_SCAN_FREQ_NUM; freq_index++) {
		if (!ts_test->test_params.diff_shift_freq[freq_index])
			continue;

		for (i = 0; i < data_size; i++) {
			val = ts_test->diff_shift_noise[freq_index][i];
			if (val > ts_test->test_params.diff_shift_max[freq_index][i]) {
				ts_err("diff shift test freq %d, node %d = %d > %d",
					freq_index, i, val, ts_test->test_params.diff_shift_max[freq_index][i]);
				error_node_count++;
			}
		}
	}

	if (!error_node_count) {
		ts_test->test_result[GTP_DIFF_SHIFT_TEST] = GTP_TEST_PASS;
		ts_info("diff shift test pass");
	} else {
		ts_test->test_result[GTP_DIFF_SHIFT_TEST] = GTP_PANEL_REASON;
		ts_info("diff shift test failed, fail node num %d", error_node_count);
	}
	return 0;
}

static bool final_result = false;

int goodix_get_final_result(void)
{
	if (final_result) {
		return GTP_RESULT_PASS;
	} else {
		return GTP_RESULT_FAIL;
	}
}


int goodix_diff_shift_test_end(struct goodix_ts_core *cd, int freq_index)
{
	int ret, retry, i;
	u8 val;
	struct goodix_ts_cmd temp_cmd;
	struct goodix_ts_test *ts_test = g_ts_test;
	struct frame_head *frame_head;
	u8 *frame_buf, *cur_ptr;
	u32 sen_num, drv_num, data_size, data_addr, flag_addr;
	int frame_len;
	int freq_cnt;

	if (!ts_test) {
		ts_err("test work does not init");
		return -EINVAL;
	}

	freq_cnt = ts_test->test_params.diff_shift_test_freq_cnt;
	frame_buf = ts_test->frame_buf;
	sen_num = ts_test->test_params.sen_num;
	drv_num = ts_test->test_params.drv_num;
	data_size = sen_num * drv_num;
	data_addr = ts_test->test_params.noisedata_addr;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D)
		flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;
	else
		flag_addr = ts_test->ts->ic_info.misc.touch_data_addr;
	// clean data ready flag
	val = 0;
	ret = ts_test_write(ts_test, flag_addr, &val, 1);
	if (ret < 0) {
		ts_err("clean touch event failed, exit");
		return -EAGAIN;
	}

	ts_info("try stop diff shift test on freq %d", freq_index);
	// send cmd stop diff test
	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x88;
	temp_cmd.data[1] = 0x2;
	temp_cmd.len = 6;
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0) {
		ts_err("send stop diff test cmd failed");
		return ret;
	}

	// wait data ready
	retry = 20;
	while (retry--) {
		usleep_range(5000, 5100);
		ret = ts_test_read(ts_test, flag_addr, &val, 1);
		if (!ret && (val & 0x80))
			break;
	}
	if (retry < 0) {
		ts_err("diff shift data is not ready val:0x%02x", val);
		return -EAGAIN;
	}

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		frame_len = cd->ic_info.misc.frame_data_head_len +
				cd->ic_info.misc.fw_attr_len +
				cd->ic_info.misc.fw_log_len +
				data_size * sizeof(s16) +
				1024;
		if (frame_len > sizeof(ts_test->frame_buf)) {
			ts_info("frame_len exceed limit %d > %lu", frame_len, sizeof(ts_test->frame_buf));
			return -EINVAL;
		}
		ret = ts_test_read(ts_test, flag_addr, frame_buf, frame_len);
		if (ret < 0)
			return ret;
		if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
			ts_err("frame head checksum error");
			return -EINVAL;
		}
		frame_head = (struct frame_head *)frame_buf;
		if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
			ts_err("frame body checksum error");
			return -EINVAL;
		}
		cur_ptr = frame_buf;
		cur_ptr += cd->ic_info.misc.frame_data_head_len;
		cur_ptr += cd->ic_info.misc.fw_attr_len;
		cur_ptr += cd->ic_info.misc.fw_log_len;
		memcpy((u8 *)ts_test->diff_shift_noise[freq_index], cur_ptr + 8,
				data_size * 2);
	} else {
		ret = ts_test_read(ts_test, data_addr,
			(u8 *)ts_test->diff_shift_noise[freq_index], data_size * sizeof(s16));
		if (ret < 0)
			return ret;
	}

	goodix_rotate_abcd2cbad(drv_num, sen_num, ts_test->diff_shift_noise[freq_index]);
	for (i = 0; i < data_size; i++)
		ts_test->diff_shift_noise[freq_index][i] = ABS(ts_test->diff_shift_noise[freq_index][i]);

	/* diff shift test finished */
	if (freq_index == freq_cnt - 1) {
		ts_test_reset(ts_test, 100);
		ts_test_irq_enable(ts_test, true);
		goodix_ts_esd_on(ts_test->ts);
		cd->is_inspecting = false;
		goodix_diff_shift_result_check(ts_test);
		if (ts_test->test_result[GTP_DIFF_SHIFT_TEST] != GTP_TEST_PASS)
			final_result = false;

	}

	return 0;
}

static int goodix_cache_noisedata(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	int cnt;
	int retry;
	u8 val;
	// static unsigned char frame_buf[FRAME_DATA_MAX_LEN];
	u8 *frame_buf = ts_test->frame_buf;
	unsigned char *cur_ptr;
	struct frame_head *frame_head;
	struct goodix_ts_cmd temp_cmd;
	struct goodix_ts_core *cd = ts_test->ts;
	u32 sen_num = ts_test->test_params.sen_num;
	u32 drv_num = ts_test->test_params.drv_num;
	u32 data_size = sen_num * drv_num;
	u32 data_addr = ts_test->test_params.noisedata_addr;
	u32 flag_addr = ts_test->ts->ic_info.misc.touch_data_addr;
	int freq_num = ts_test->ts->ic_info.parm.mutual_freq_num;
	int freq_cnt, frame_len;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;
		temp_cmd.cmd = 0x90;
		temp_cmd.data[0] = 0x82;
		temp_cmd.len = 5;
		ret = ts_test_send_cmd(ts_test, &temp_cmd);
		if (ret < 0) {
			ts_err("switch diffdata mode failed, exit!");
			return ret;
		}
	}

	for (freq_cnt = 0; freq_cnt < freq_num; freq_cnt++) {
		temp_cmd.cmd = 0x9C;
		temp_cmd.data[0] = freq_cnt;
		temp_cmd.len = 5;
		ret = ts_test_send_cmd(ts_test, &temp_cmd);
		if (ret < 0) {
			ts_err("switch freq index:%d failed", freq_cnt);
			return ret;
		}

		ts_info("switch freq index:%d", freq_cnt);

		for (i = 0; i < DISCARD_FRAMES; i++) {
			val = 0;
			ts_test_write(ts_test, flag_addr, &val, 1);
			msleep(20);
		}

		temp_cmd.cmd = 0x83;
		temp_cmd.len = 4;
		ts_test_send_cmd(ts_test, &temp_cmd); // force refresh base

		for (i = 0; i < DISCARD_FRAMES; i++) {
			val = 0;
			ts_test_write(ts_test, flag_addr, &val, 1);
			msleep(20);
		}

		for (cnt = 0; cnt < NOISEDATA_TEST_TIMES; cnt++) {
			val = 0;
			ret = ts_test_write(ts_test, flag_addr, &val, 1);
			if (ret < 0) {
				ts_err("clean touch event failed, exit");
				return -EAGAIN;
			}
			retry = 20;
			while (retry--) {
				usleep_range(5000, 5100);
				ret = ts_test_read(ts_test, flag_addr, &val, 1);
				if (!ret && (val & 0x80))
					break;
			}
			if (retry < 0) {
				ts_err("noisedata is not ready val:0x%02x i:%d, exit", val, cnt);
				return -EAGAIN;
			}
			if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
				frame_len = cd->ic_info.misc.frame_data_head_len +
						cd->ic_info.misc.fw_attr_len +
						cd->ic_info.misc.fw_log_len +
						data_size * sizeof(s16) +
						1024;
				if (frame_len > sizeof(ts_test->frame_buf)) {
					ts_info("frame_len exceed limit %d > %lu", frame_len, sizeof(ts_test->frame_buf));
					return -EINVAL;
				}
				ret = ts_test_read(ts_test, flag_addr, frame_buf, frame_len);
				if (ret < 0)
					return ret;
				if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
					ts_err("frame head checksum error");
					return -EINVAL;
				}
				frame_head = (struct frame_head *)frame_buf;
				if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
					ts_err("frame body checksum error");
					return -EINVAL;
				}
				cur_ptr = frame_buf;
				cur_ptr += cd->ic_info.misc.frame_data_head_len;
				cur_ptr += cd->ic_info.misc.fw_attr_len;
				cur_ptr += cd->ic_info.misc.fw_log_len;
				memcpy((u8 *)ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data, cur_ptr + 8, data_size * 2);
			} else {
				ret = ts_test_read(ts_test, data_addr, (u8 *)ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data,
								   data_size * sizeof(s16));
				if (ret < 0)
					return ret;
			}
			ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].size = data_size;
			goodix_rotate_abcd2cbad(drv_num, sen_num, ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data);
			for (i = 0; i < data_size; i++)
				ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data[i] =
					ABS(ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data[i]);
		}
	}

	return ret;
}

static int goodix_cache_self_noisedata(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	u32 sen_num = ts_test->test_params.sen_num;
	u32 drv_num = ts_test->test_params.drv_num;
	u32 data_size = sen_num + drv_num;
	u32 data_addr = ts_test->test_params.self_noisedata_addr;
	u32 flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;
	struct frame_head *frame_head;
	struct goodix_ts_core *cd = ts_test->ts;
	static unsigned char frame_buf[FRAME_DATA_MAX_LEN];
	unsigned char *cur_ptr;

	if (cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		ret = ts_test_read(ts_test, flag_addr, frame_buf, sizeof(frame_buf));
		if (ret < 0)
			return ret;
		if (checksum_cmp(frame_buf, cd->ic_info.misc.frame_data_head_len, CHECKSUM_MODE_U8_LE)) {
			ts_err("frame head checksum error");
			return -EINVAL;
		}
		frame_head = (struct frame_head *)frame_buf;
		if (checksum_cmp(frame_buf, frame_head->cur_frame_len, CHECKSUM_MODE_U16_LE)) {
			ts_err("frame body checksum error");
			return -EINVAL;
		}
		cur_ptr = frame_buf;
		cur_ptr += cd->ic_info.misc.frame_data_head_len;
		cur_ptr += cd->ic_info.misc.fw_attr_len;
		cur_ptr += cd->ic_info.misc.fw_log_len;
		cur_ptr += cd->ic_info.misc.mutual_struct_len;
		memcpy((u8 *)ts_test->self_noisedata.data, cur_ptr + 10, data_size * 2);
	} else {
		ret = ts_test_read(ts_test, data_addr, (u8 *)ts_test->self_noisedata.data, data_size * sizeof(s16));
		if (ret < 0)
			return ret;
	}
	ts_test->self_noisedata.size = data_size;
	for (i = 0; i < data_size; i++) {
		ts_test->self_noisedata.data[i] = ABS(ts_test->self_noisedata.data[i]);
	}

	return ret;
}

static int goodix_analysis_rawdata(struct goodix_ts_test *ts_test)
{
	int i;
	int j;
	bool fail_flag = false;
	int err_cnt = 0;
	int times = TOTAL_FRAME_NUM;
	s16 val;
	u32 data_size = ts_test->rawdata[0].size;

	for (i = 0; i < times; i++) {
		for (j = 0; j < data_size; j++) {
			val = ts_test->rawdata[i].data[j];
			if (val < ts_test->test_params.min_limits[j]) {
				fail_flag = true;
				ts_test->open_res.beyond_min_limit_cnt[j]++;
			}
			if (val > ts_test->test_params.max_limits[j]) {
				fail_flag = true;
				ts_test->open_res.beyond_max_limit_cnt[j]++;
			}
		}
		if (fail_flag)
			err_cnt++;
		fail_flag = false;
	}

	if (err_cnt > 0)
		ts_err("rawdata have %d frames out of range", err_cnt);

	err_cnt *= 100;
	if (err_cnt > times * 100 * 9 / 10)
		return -EINVAL;

	return 0;
}

static int goodix_analysis_deltadata(struct goodix_ts_test *ts_test)
{
	int i;
	int j;
	int ret = 0;
	s16 val;
	u32 data_size = ts_test->accord_arr[0].size;

	for (i = 0; i < TOTAL_FRAME_NUM; i++) {
		for (j = 0; j < data_size; j++) {
			val = ts_test->accord_arr[i].data[j];
			if (val > ts_test->test_params.deviation_limits[j]) {
				ts_test->open_res.beyond_accord_limit_cnt[j]++;
				ret = -EINVAL;
			}
		}
	}

	return ret;
}

static int goodix_analysis_self_rawdata(struct goodix_ts_test *ts_test)
{
	int i;
	s16 val;
	u32 data_size = ts_test->self_rawdata.size;

	for (i = 0; i < data_size; i++) {
		val = ts_test->self_rawdata.data[i];
		if (val < ts_test->test_params.self_min_limits[i] || val > ts_test->test_params.self_max_limits[i]) {
			ts_err("self_rawdata isn't in range, val:%d threshold:[%d,%d]", val, ts_test->test_params.self_min_limits[i],
				   ts_test->test_params.self_max_limits[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static int goodix_analysis_noisedata(struct goodix_ts_test *ts_test)
{
	int cnt;
	int i;
	bool fail_flag = false;
	int err_cnt = 0;
	int times = NOISEDATA_TEST_TIMES;
	s16 val;
	u32 data_size = ts_test->mutual_freq_diff[0].noisedata[0].size;
	int freq_num = ts_test->ts->ic_info.parm.mutual_freq_num;
	int freq_cnt;

	for (freq_cnt = 0; freq_cnt < freq_num; freq_cnt++) {
		for (cnt = 0; cnt < times; cnt++) {
			for (i = 0; i < data_size; i++) {
				val = ts_test->mutual_freq_diff[freq_cnt].noisedata[cnt].data[i];
				if (val > ts_test->test_params.noise_threshold[freq_cnt][i])
					fail_flag = true;
			}
			if (fail_flag)
				err_cnt++;
			fail_flag = false;
		}
	}

	if (err_cnt > 0) {
		ts_err("noisedata have %d frames out of range", err_cnt);
		return -EINVAL;
	}

	return 0;
}

static int goodix_analysis_self_noisedata(struct goodix_ts_test *ts_test)
{
	int i;
	s16 val;
	u32 data_size = ts_test->self_noisedata.size;

	for (i = 0; i < data_size; i++) {
		val = ts_test->self_noisedata.data[i];
		if (val > ts_test->test_params.self_noise_threshold) {
			ts_err("self noisedata isn't in range, val:%d threshold:[0,%d]", val, ts_test->test_params.self_noise_threshold);
			return -EINVAL;
		}
	}

	return 0;
}

static void goodix_cal_special_delta(struct goodix_ts_test *ts_test, int16_t *rawdata, int16_t *deltadata)
{
	u32 data_size;
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int j;
	int max_val;
	int raw;
	int temp;

	data_size = tx * rx;
	for (j = 0; j < data_size; j++) {
		raw = rawdata[j];
		max_val = 0;
		/* calcu delta with above node */
		if (j - tx >= 0) {
			temp = rawdata[j - tx];
			temp = ABS(temp - raw);
			max_val = MAX(max_val, temp);
		}
		/* calcu delta with bellow node */
		if (j + tx < data_size) {
			temp = rawdata[j + tx];
			temp = ABS(temp - raw);
			max_val = MAX(max_val, temp);
		}
		/* calcu delta with left node */
		if (j % tx) {
			temp = rawdata[j - 1];
			temp = ABS(temp - raw);
			max_val = MAX(max_val, temp);
		}
		/* calcu delta with right node */
		if ((j + 1) % tx) {
			temp = rawdata[j + 1];
			temp = ABS(temp - raw);
			max_val = MAX(max_val, temp);
		}
		temp = max_val * 1000 / raw;
		temp = (temp > 32767) ? 32767 : temp;
		deltadata[j] = temp;
	}
}

static int goodix_cache_special_deltadata(struct goodix_ts_test *ts_test)
{
	int ret;
	int i, j;
	u8 val;
	u32 sen_num = ts_test->test_params.sen_num;
	u32 drv_num = ts_test->test_params.drv_num;
	u32 data_size = sen_num * drv_num;
	u32 flag_addr = ts_test->ts->ic_info.misc.touch_data_addr;
	s16 *tmp_raw;
	s16 *tmp_delta;
	int cnt;

	tmp_raw = kzalloc(data_size * 2, GFP_KERNEL);
	tmp_delta = kzalloc(data_size * 2, GFP_KERNEL);

	if (ts_test->ts->bus->ic_type == IC_TYPE_BERLIN_D)
		flag_addr = ts_test->ts->ic_info.misc.frame_data_addr;

	for (i = 0; i < DISCARD_FRAMES; i++) {
		val = 0;
		ts_test_write(ts_test, flag_addr, &val, 1);
		msleep(20);
	}

	ts_test->special_freq_accord_arr.size = data_size;
	for (i = 0; i < TOTAL_FRAME_NUM; i++) {
		for (j = 0; j < GOODIX_RETRY_3; j++) {
			ret = goodix_poll_rawdata(ts_test, GTP_MUTUAL_RAW, (u8 *)tmp_raw, data_size);
			if (ret == 0) {
				goodix_rotate_abcd2cbad(drv_num, sen_num, tmp_raw);
				goodix_cal_special_delta(ts_test, tmp_raw, tmp_delta);
				break;
			}
		}
		if (i == 0) {
			for (cnt = 0; cnt < data_size; cnt++)
				ts_test->special_freq_accord_arr.data[cnt] = tmp_delta[cnt];
		} else {
			for (cnt = 0; cnt < data_size; cnt++)
				ts_test->special_freq_accord_arr.data[cnt] = (ts_test->special_freq_accord_arr.data[cnt] + tmp_delta[cnt]) / 2;
		}
	}

	kfree(tmp_raw);
	kfree(tmp_delta);
	return ret;
}

static int goodix_analysis_special_delta(struct goodix_ts_test *ts_test)
{
	int i;
	s32 val;
	u32 data_size = ts_test->special_freq_accord_arr.size;

	for (i = 0; i < data_size; i++) {
		val = ts_test->special_freq_accord_arr.data[i];
		if (val > ts_test->test_params.special_freq_deviation_limts[i])
			return -1;
	}

	return 0;
}

static int goodix_special_delta_test(struct goodix_ts_test *ts_test)
{
	struct goodix_ts_cmd temp_cmd;
	uint16_t tmp_freq;
	int ret;

	ts_info("Enter special delta test");

	temp_cmd.cmd = 0x90;
	temp_cmd.data[0] = 0x81;
	temp_cmd.len = 5;
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0) {
		ts_err("failed switch rawdata mode");
		return ret;
	}

	ts_info("switch frequency to %d", ts_test->test_params.special_freq);
	tmp_freq = ts_test->test_params.special_freq / 61;
	temp_cmd.len = 6;
	temp_cmd.cmd = 0xB1;
	temp_cmd.data[0] = tmp_freq & 0xFF;
	temp_cmd.data[1] = (tmp_freq >> 8) & 0xFF;
	ret = ts_test_send_cmd(ts_test, &temp_cmd);
	if (ret < 0) {
		ts_err("failed set frequency");
		return ret;
	}

	ret = goodix_cache_special_deltadata(ts_test);
	if (ret < 0) {
		ts_err("Failed to read special deltadata");
	} else {
		ret = goodix_analysis_special_delta(ts_test);
		if (ret < 0) {
			ts_info("special freq delta test NG");
			ts_test->test_result[GTP_SPECIAL_DELTA_TEST] = GTP_PANEL_REASON;
		} else {
			ts_info("special freq delta test OK");
			ts_test->test_result[GTP_SPECIAL_DELTA_TEST] = GTP_TEST_PASS;
		}
	}

	return 0;
}

static void goodix_capacitance_test(struct goodix_ts_test *ts_test)
{
	int ret;

	if (!ts_test || !ts_test->ts) {
		ts_err("ts_test or ts_test->ts is NULL, abort cap test");
		return;
	}

	ts_info("---------------------- cap_test begin ----------------------");
	ret = goodix_cap_test_prepare(ts_test);
	if (ret < 0) {
		ts_err("cap_test prepare failed, exit");
		goto exit;
	}
	ts_info("cap rawdata prepare OK");

	/* obtain rawdata */
	ret = goodix_cache_rawdata(ts_test);
	if (ret < 0) {
		if (ret == -EAGAIN) {
			ts_err("Capacitance exit");
			goto exit;
		} else {
			ts_err("Failed to read capdata");
		}
	} else {
		ts_info("get rawdata finish, start analysis");
		ret = goodix_analysis_rawdata(ts_test);
		if (ret < 0)
			ts_test->test_result[GTP_CAP_TEST] = GTP_PANEL_REASON;
		else
			ts_test->test_result[GTP_CAP_TEST] = GTP_TEST_PASS;
	}

	/* obtain delta_data */
	goodix_cache_deltadata(ts_test);
	ts_info("get deltadata finish, start analysis");
	ret = goodix_analysis_deltadata(ts_test);
	if (ret < 0)
		ts_test->test_result[GTP_DELTA_TEST] = GTP_PANEL_REASON;
	else
		ts_test->test_result[GTP_DELTA_TEST] = GTP_TEST_PASS;

	/* obtain self_rawdata */
	if (ts_test->test_params.test_items[GTP_SELFCAP_TEST]) {
		ret = goodix_cache_self_rawdata(ts_test);
		if (ret < 0) {
			ts_err("Failed to read self_capdata");
		} else {
			ts_info("get self_rawdata finish, start analysis");
			ret = goodix_analysis_self_rawdata(ts_test);
			if (ret < 0)
				ts_test->test_result[GTP_SELFCAP_TEST] = GTP_PANEL_REASON;
			else
				ts_test->test_result[GTP_SELFCAP_TEST] = GTP_TEST_PASS;
		}
	}

	/* obtain noisedata */
	if (ts_test->test_params.test_items[GTP_NOISE_TEST]) {
		ret = goodix_cache_noisedata(ts_test);
		if (ret < 0) {
			ts_err("Failed to read noisedata");
		} else {
			ts_info("get noisedata finish, start analysis");
			ret = goodix_analysis_noisedata(ts_test);
			if (ret < 0)
				ts_test->test_result[GTP_NOISE_TEST] = GTP_PANEL_REASON;
			else
				ts_test->test_result[GTP_NOISE_TEST] = GTP_TEST_PASS;
		}
	}

	/* obtain self_noisedata */
	if (ts_test->test_params.test_items[GTP_SELFNOISE_TEST]) {
		ret = goodix_cache_self_noisedata(ts_test);
		if (ret < 0) {
			ts_err("Failed to read self_noisedata");
		} else {
			ts_info("get self_noisedata finish, start analysis");
			ret = goodix_analysis_self_noisedata(ts_test);
			if (ret < 0)
				ts_test->test_result[GTP_SELFNOISE_TEST] = GTP_PANEL_REASON;
			else
				ts_test->test_result[GTP_SELFNOISE_TEST] = GTP_TEST_PASS;
		}
	}

	if (ts_test->test_params.test_items[GTP_SPECIAL_DELTA_TEST]) {
		goodix_special_delta_test(ts_test);
	}

exit:
	goodix_cap_test_finish(ts_test);
}

static void goodix_version_test(struct goodix_ts_test *ts_test)
{
	struct goodix_ts_core *cd = ts_test->ts;
	u8 rom_pid[8] = {0};
	if (ts_test->test_params.test_items[GTP_VERSION_TEST])
		ts_test->test_result[GTP_VERSION_TEST] = SYS_SOFTWARE_REASON;
	memcpy(rom_pid, cd->fw_version.rom_pid, sizeof(cd->fw_version.rom_pid));
	sprintf(ts_test->cur_version, "%s-%02x.%02x.%02x_%s_%02x.%02x.%02x.%02x", rom_pid, cd->fw_version.rom_vid[0], cd->fw_version.rom_vid[1],
			cd->fw_version.rom_vid[2], cd->fw_version.patch_pid, cd->fw_version.patch_vid[0], cd->fw_version.patch_vid[1],
			cd->fw_version.patch_vid[2], cd->fw_version.patch_vid[3]);
	ts_test->test_result[GTP_VERSION_TEST] = GTP_TEST_PASS;
}
#define FT_TEST_PASS_FLAG 0xAA
static void goodix_check_key_info_test(struct goodix_ts_test *ts_test)
{
	int ret;
	u32 chip_pid_addr;
	u32 chip_info_addr;
	u8 chip_info_buf[CHIP_KEY_INFO_MAX_LEN];
	u8 test_result = 0;
	u16 chksum;
	u16 sum = 0;
	int i;
	struct goodix_ts_core *cd = ts_test->ts;
	if (ts_test->test_params.test_items[GTP_CHIP_KEY_INFO_TEST])
		ts_test->test_result[GTP_CHIP_KEY_INFO_TEST] = SYS_SOFTWARE_REASON;
	if (cd->bus->ic_type == IC_TYPE_BERLIN_B)
		chip_pid_addr = 0x3F051;
	else
		chip_pid_addr = 0x1F031;
	for (i = 0; i < 3; i++) {
		ret = cd->hw_ops->read_flash(cd, chip_pid_addr, ts_test->chip_pid, sizeof(ts_test->chip_pid));
		if (ret == 0)
			break;
		msleep(100);
	}
	if (ret < 0) {
		ts_err("read chip pid from flash failed");
		return;
	}
	if (memcmp(ts_test->chip_pid, cd->fw_version.patch_pid, 7)) {
		test_result |= 0x01; // chip pid check failed
		ts_err("chip_pid:%s != patch_pid:%s", ts_test->chip_pid, cd->fw_version.patch_pid);
	}
	if (cd->bus->ic_type == IC_TYPE_BERLIN_B || cd->bus->ic_type == IC_TYPE_BERLIN_D) {
		if (cd->bus->ic_type == IC_TYPE_BERLIN_B)
			chip_info_addr = 0x3F300;
		else
			chip_info_addr = 0x1F300;
		for (i = 0; i < 3; i++) {
			ret = cd->hw_ops->read_flash(cd, chip_info_addr, chip_info_buf, sizeof(chip_info_buf));
			if (ret == 0)
				break;
			msleep(100);
		}
		if (ret < 0) {
			ts_err("read chip info from flash failed");
			return;
		}
		chksum = chip_info_buf[CHIP_KEY_INFO_MAX_LEN - 2] + ((u16)chip_info_buf[CHIP_KEY_INFO_MAX_LEN - 1] << 8);
		for (i = 0; i < CHIP_KEY_INFO_MAX_LEN - 2; i++)
			sum += chip_info_buf[i];
		if ((sum & 0x00FF) != (chksum & 0x00FF)) {
			ts_err("chip info checksum error");
			return;
		}
		ts_test->chip_info.hw_ver = chip_info_buf[0];
		ts_test->chip_info.package_site = chip_info_buf[1];
		if (chip_info_buf[2 + CHIP_UID_MAX_LEN] == FT_TEST_PASS_FLAG) {
			memcpy(ts_test->chip_info.chip_uid, &chip_info_buf[2], CHIP_UID_MAX_LEN); // UID
			ts_test->chip_info.ft_test_result = chip_info_buf[2 + CHIP_UID_MAX_LEN];
		} else if (chip_info_buf[2] == FT_TEST_PASS_FLAG) {
			ts_test->chip_info.ft_test_result = chip_info_buf[2];
			memcpy(ts_test->chip_info.chip_uid, &chip_info_buf[3], CHIP_UID_MAX_LEN); // UID
		} else {
			test_result |= 0x02;
			ts_err("chip info test failed");
		}
		ts_test->chip_info.substrate_tech = chip_info_buf[3 + CHIP_UID_MAX_LEN];
		ts_test->chip_info.mcu_fab = chip_info_buf[4 + CHIP_UID_MAX_LEN];
		ts_test->chip_info.vaild_len = chip_info_buf[CHIP_KEY_INFO_MAX_LEN - 3];
		ts_test->chip_info.check_sum = chksum;
	}
	if (test_result > 0)
		ts_test->test_result[GTP_CHIP_KEY_INFO_TEST] = GTP_PANEL_REASON;
	else
		ts_test->test_result[GTP_CHIP_KEY_INFO_TEST] = GTP_TEST_PASS;
}
static void goodix_custom_info_test(struct goodix_ts_test *ts_test)
{
	int ret;
	struct goodix_ts_core *cd = ts_test->ts;
	u32 custom_info_addr;
	u32 custom_info_ram_addr;
	u32 custom_info_len = 16;
	if (ts_test->test_params.test_items[GTP_CUSTOM_INFO_TEST])
		ts_test->test_result[GTP_CUSTOM_INFO_TEST] = SYS_SOFTWARE_REASON;
	if (cd->bus->ic_type == IC_TYPE_BERLIN_A) {
		custom_info_addr = 0x3F000;
		custom_info_ram_addr = 0x10028;
	} else if (cd->bus->ic_type == IC_TYPE_BERLIN_B) {
		custom_info_addr = 0x7F000;
		custom_info_ram_addr = 0x10030;
	} else {
		custom_info_addr = 0x1E000;
		custom_info_ram_addr = 0x10030;
	}
	ret = cd->hw_ops->read_flash(cd, custom_info_addr, ts_test->custom_info_flash, custom_info_len);
	if (ret < 0) {
		ts_err("read custom info from flash failed");
		return;
	}
	ret = cd->hw_ops->read(cd, custom_info_ram_addr, ts_test->custom_info_sram, custom_info_len);
	if (ret < 0) {
		ts_err("read custom info from sram failed");
		return;
	}
	ts_test->test_result[GTP_CUSTOM_INFO_TEST] = GTP_TEST_PASS;
}

static void goodix_reset_test(struct goodix_ts_test *ts_test)
{
	struct goodix_ts_core *cd = ts_test->ts;
	struct goodix_fw_version ver;
	struct goodix_ts_cmd temp_cmd;
	int ret = -1;
	int retry = 2;

	/* enter gesture */
	temp_cmd.len = 6;
	temp_cmd.cmd = 0xA6;
	temp_cmd.data[0] = 0x00;
	temp_cmd.data[1] = 0x00;

	while (retry--) {
		ts_test_send_cmd(ts_test, &temp_cmd);
		memset(&ver, 0, sizeof(ver));
		ret = ts_test_read_version(ts_test, &ver);
		ts_test_reset(ts_test, GOODIX_NORMAL_RESET_DELAY_MS);
		if (!strncmp(ver.patch_pid, "GEST", 4))
			break;
	}

	if (retry < 0) {
		ts_err("PID[%s] != GEST", ver.patch_pid);
		ts_test->test_result[GTP_RESET_TEST] = GTP_PANEL_REASON;
		return;
	}

	memset(&ver, 0, sizeof(ver));
	ret = ts_test_read_version(ts_test, &ver);
	if (strncmp(ver.patch_pid, cd->fw_version.patch_pid, sizeof(ver.patch_pid))) {
		ts_err("PID[%s] != %s", ver.patch_pid, cd->fw_version.patch_pid);
		ts_test->test_result[GTP_RESET_TEST] = GTP_PANEL_REASON;
		return;
	}

	if (0 == ret) {
		ts_test->test_result[GTP_RESET_TEST] = GTP_TEST_PASS;
	} else {
		ts_test->test_result[GTP_RESET_TEST] = GTP_PANEL_REASON;
	}
}

char *goodix_strncat(char *dest, char *src, size_t dest_size)
{
	size_t dest_len = 0;

	dest_len = strnlen(dest, dest_size);
	return strncat(&dest[dest_len], src, dest_size - dest_len - 1);
}

char *goodix_strncatint(char *dest, int src, char *format, size_t dest_size)
{
	char src_str[MAX_STR_LEN] = {0};

	snprintf(src_str, MAX_STR_LEN, format, src);
	return goodix_strncat(dest, src_str, dest_size);
}

static void goodix_data_cal(s16 *data, size_t data_size, s16 *stat_result)
{
	int i = 0;
	s16 avg = 0;
	s16 min = 0;
	s16 max = 0;
	long long sum = 0;

	min = data[0];
	max = data[0];
	for (i = 0; i < data_size; i++) {
		sum += data[i];
		if (max < data[i])
			max = data[i];
		if (min > data[i])
			min = data[i];
	}
	avg = div_s64(sum, data_size);
	stat_result[0] = avg;
	stat_result[1] = max;
	stat_result[2] = min;
}

static void goodix_data_statistics(s16 *data, size_t data_size, char *result, size_t res_size)
{
	s16 stat_value[3];

	if (!data || !result) {
		ts_err("parameters error please check *data and *result value");
		return;
	}

	if (data_size <= 0 || res_size <= 0) {
		ts_err("input parameter is illegva:data_size=%ld, res_size=%ld", data_size, res_size);
		return;
	}
	goodix_data_cal(data, data_size, stat_value);

	memset(result, 0, res_size);
	snprintf(result, res_size, "[%d,%d,%d]", stat_value[0], stat_value[1], stat_value[2]);
	return;
}

#ifdef SAVE_IN_CSV
static u8 *g_test_result_buf;
static u32 g_test_result_size;
static int fs_write(const void* buf, size_t size)
{
	if (!g_test_result_buf) {
		g_test_result_buf = vmalloc(DEFAULT_SEQ_FILE_SIZE);
		if (!g_test_result_buf)
			return -ENOMEM;
		g_test_result_size = 0;
	}

	if (size > DEFAULT_SEQ_FILE_SIZE - g_test_result_size) {
		ts_err("test result too much");
		return -EINVAL;
	}

	memcpy(g_test_result_buf + g_test_result_size, buf, size);
	g_test_result_size += size;

	return 0;
}

static int deinit_test_result_buf(void)
{
	g_test_result_size = 0;
	if (!g_test_result_buf)
		return 0;

	vfree(g_test_result_buf);
	g_test_result_buf = NULL;
	return 0;
}

static int goodix_save_test_config(struct goodix_ts_test *ts_test)
{
	int ret = 0;
	int i;
	int bytes = 0;
	char *data;
	struct goodix_ic_config *cfg = &ts_test->test_config;

	if (cfg->len <= 0) {
		ts_info("Can't find vaild test config");
		return 0;
	}

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<OrderConfig>\n");
	for (i = 0; i < cfg->len; i++) {
		bytes += sprintf(&data[bytes], "0x%02x,", cfg->data[i]);
	}
	bytes += sprintf(&data[bytes], "\n");
	bytes += sprintf(&data[bytes], "</OrderConfig>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("test config write failed");
		goto save_end;
	}

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_header(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	int bytes = 0;
	bool result = false;
	char *data = NULL;
	struct goodix_ts_core *ts = ts_test->ts;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	bytes += sprintf(&data[bytes], "<TESTLOG>\n");
	bytes += sprintf(&data[bytes], "<Header>\n");
	/* sava test result */
	for (i = 0; i < MAX_TEST_ITEMS; i++) {
		if ((ts_test->test_result[i] > 0) && (ts_test->test_result[i] != GTP_TEST_PASS)) {
			result = true;
			break;
		}
	}
	if (result)
		bytes += sprintf(&data[bytes], "<Result>NG</Result>\n");
	else
		bytes += sprintf(&data[bytes], "<Result>OK</Result>\n");
	bytes += sprintf(&data[bytes], "<DeviceType>GT%s</DeviceType>\n", ts->fw_version.patch_pid);
	bytes += sprintf(&data[bytes], "<SensorId>%d</SensorId>\n", ts_test->ts->fw_version.sensor_id);
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("header write failed");
		goto save_end;
	}
	bytes = 0;
	/* save test config */
	ret = goodix_save_test_config(ts_test);
	if (ret < 0) {
		ts_err("save test config failed");
		goto save_end;
	}

	bytes += sprintf(&data[bytes], "</Header>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("header write failed");
		goto save_end;
	}
	bytes = 0;

	/* item list */
	bytes += sprintf(&data[bytes], "<ItemList>\n");

	if (ts_test->test_result[GTP_VERSION_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_VERSION_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Version Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Version Test\" result=\"NG\"/>\n");
	}
	if (ts_test->test_result[GTP_CHIP_KEY_INFO_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_CHIP_KEY_INFO_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Check Chip Key Info\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Check Chip Key Info\" result=\"NG\"/>\n");
	}
	if (ts_test->test_result[GTP_CUSTOM_INFO_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_CUSTOM_INFO_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Custom Info Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Custom Info Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_params.test_items[GTP_RESET_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_RESET_TEST]) {
			bytes += sprintf(&data[bytes], "<Item name=\"Chip Reset Test\" result=\"OK\"/>\n");
		} else {
			bytes += sprintf(&data[bytes], "<Item name=\"Chip Reset Test\" result=\"NG\"/>\n");
		}
	}

	if (ts_test->test_result[GTP_CAP_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_CAP_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Rawdata MAX/MIN Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Rawdata MAX/MIN Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_DELTA_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_DELTA_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Rawdata Adjcent Deviation Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Rawdata Adjcent Deviation Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_NOISE_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_NOISE_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Diffdata Jitter Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Diffdata Jitter Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_SELFNOISE_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_SELFNOISE_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Self Diffdata Jitter Limit Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Self Diffdata Jitter Limit Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_SELFCAP_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_SELFCAP_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Self Rawdata Upper Limit Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Self Rawdata Upper Limit Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_DIFF_SHIFT_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_DIFF_SHIFT_TEST])
			bytes += sprintf(&data[bytes],
					"<Item name=\"Diff shift test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes],
					"<Item name=\"Diff shift test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_result[GTP_SHORT_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_SHORT_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Short Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Short Test\" result=\"NG\"/>\n");
	}

	if (ts_test->test_params.test_items[GTP_SPECIAL_DELTA_TEST]) {
		if (GTP_TEST_PASS == ts_test->test_result[GTP_SPECIAL_DELTA_TEST])
			bytes += sprintf(&data[bytes], "<Item name=\"Special Delta Test\" result=\"OK\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<Item name=\"Special Delta Test\" result=\"NG\"/>\n");
	}

	bytes += sprintf(&data[bytes], "</ItemList>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("item list write failed");
		goto save_end;
	}

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_limits(struct goodix_ts_test *ts_test)
{
	int ret;
	int i;
	int bytes = 0;
	char *data = NULL;
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int chn1;
	int chn2;
	int r;
	int freq_num = ts_test->ts->ic_info.parm.mutual_freq_num;
	int freq_index;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<TestItems>\n");

	if (ts_test->test_result[GTP_VERSION_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Version Test\">\n");
		bytes += sprintf(&data[bytes], "<CurVerDataString>%s</CurVerDataString>\n", ts_test->cur_version);
		bytes += sprintf(&data[bytes], "</Item>\n");
	}
	if (ts_test->test_result[GTP_CHIP_KEY_INFO_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Check Chip Key Info\">\n");
		if (GTP_TEST_PASS == ts_test->test_result[GTP_CHIP_KEY_INFO_TEST])
			bytes += sprintf(&data[bytes], "<TestInfo checkPID=\"true\"/>\n");
		else
			bytes += sprintf(&data[bytes], "<TestInfo checkPID=\"false\"/>\n");
		bytes += sprintf(&data[bytes],
						 "<ChipInfo Pid=\"%s\" HwVer=\"0x%02x\" PackageSite=\"0x%02x\" FtTestResult=\"0x%02x\" "
						 "SubstrateTechnology=\"0x%02x\" McuFab=\"0x%02x\">\n",
						 ts_test->chip_pid, ts_test->chip_info.hw_ver, ts_test->chip_info.package_site, ts_test->chip_info.ft_test_result,
						 ts_test->chip_info.substrate_tech, ts_test->chip_info.mcu_fab);
		bytes += sprintf(&data[bytes], "<ChipUID>");
		for (i = 0; i < sizeof(ts_test->chip_info.chip_uid); i++)
			bytes += sprintf(&data[bytes], "%02x", ts_test->chip_info.chip_uid[i]);
		bytes += sprintf(&data[bytes], "</ChipUID>\n");
		bytes += sprintf(&data[bytes], "</ChipInfo>\n");
		bytes += sprintf(&data[bytes], "</Item>\n");
	}
	if (ts_test->test_result[GTP_CUSTOM_INFO_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Custom Info Test\">\n");
		bytes += sprintf(&data[bytes], "<CustomInfoInFlash>");
		for (i = 0; i < sizeof(ts_test->custom_info_flash); i++)
			bytes += sprintf(&data[bytes], "0x%02x,", ts_test->custom_info_flash[i]);
		bytes += sprintf(&data[bytes], "</CustomInfoInFlash>\n");
		bytes += sprintf(&data[bytes], "<CustomInfoInRam>");
		for (i = 0; i < sizeof(ts_test->custom_info_sram); i++)
			bytes += sprintf(&data[bytes], "0x%02x,", ts_test->custom_info_sram[i]);
		bytes += sprintf(&data[bytes], "</CustomInfoInRam>\n");
		bytes += sprintf(&data[bytes], "</Item>\n");
	}

	/* save short result */
	if (ts_test->test_result[GTP_SHORT_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Short Test\">\n");
		bytes += sprintf(&data[bytes], "<ShortNum>%d</ShortNum>\n", ts_test->short_res.short_num);
		for (i = 0; i < ts_test->short_res.short_num; i++) {
			chn1 = ts_test->short_res.short_msg[4 * i];
			chn2 = ts_test->short_res.short_msg[4 * i + 1];
			r = (ts_test->short_res.short_msg[4 * i + 2] << 8) + ts_test->short_res.short_msg[4 * i + 3];
			if (chn1 == CHN_VDD)
				bytes += sprintf(&data[bytes], "<ShortMess Chn1=\"VDD\" ");
			else if (chn1 == CHN_GND)
				bytes += sprintf(&data[bytes], "<ShortMess Chn1=\"GND\" ");
			else if (chn1 & DRV_CHANNEL_FLAG)
				bytes += sprintf(&data[bytes], "<ShortMess Chn1=\"Tx%d\" ", chn1 & 0x7f);
			else
				bytes += sprintf(&data[bytes], "<ShortMess Chn1=\"Rx%d\" ", chn1 & 0x7f);
			if (chn2 == CHN_VDD)
				bytes += sprintf(&data[bytes], "Chn2=\"VDD\" ShortResistor= \"%dKom\"/>\n", r);
			else if (chn2 == CHN_GND)
				bytes += sprintf(&data[bytes], "Chn2=\"GND\" ShortResistor= \"%dKom\"/>\n", r);
			else if (chn2 & DRV_CHANNEL_FLAG)
				bytes += sprintf(&data[bytes], "Chn2=\"Tx%d\" ShortResistor= \"%dKom\"/>\n", chn2 & 0x7f, r);
			else
				bytes += sprintf(&data[bytes], "Chn2=\"Rx%d\" ShortResistor= \"%dKom\"/>\n", chn2 & 0x7f, r);
		}

		/* save adc dump result */
		bytes += sprintf(&data[bytes], "<AdcDumpNegative>%s</AdcDumpNegative>\n", ts_test->b_adc_negative_sta_ng ? "NG" : "OK");
		if (ts_test->b_adc_negative_sta_ng != 0) {
			bytes += sprintf(&data[bytes], "<AdcDumpNegativeValue>");
			for (i = 0; i < MAX_SEN_NUM_BRD; i++)
				bytes += sprintf(&data[bytes], "%d,", ts_test->adc_negative_sta[i]);
			bytes += sprintf(&data[bytes], "</AdcDumpNegativeValue>\n");
		}

		bytes += sprintf(&data[bytes], "<AdcDumpForward>%s</AdcDumpForward>\n", ts_test->b_adc_forward_sta_ng ? "NG" : "OK");
		if (ts_test->b_adc_forward_sta_ng != 0) {
			bytes += sprintf(&data[bytes], "<AdcDumpForwardValue>");
			for (i = 0; i < MAX_SEN_NUM_BRD; i++)
				bytes += sprintf(&data[bytes], "%d,", ts_test->adc_forward_sta[i]);
			bytes += sprintf(&data[bytes], "</AdcDumpForwardValue>\n");
		}

		bytes += sprintf(&data[bytes], "<AdcDumpMM>%s</AdcDumpMM>\n", ts_test->b_adc_mm_ng ? "NG" : "OK");
		if (ts_test->b_adc_mm_ng != 0) {
			bytes += sprintf(&data[bytes], "<AdcDumpMMValue>");
			for (i = 0; i < MAX_SEN_NUM_BRD; i++)
				bytes += sprintf(&data[bytes], "%d,", ts_test->adc_dump_mm[i]);
			bytes += sprintf(&data[bytes], "</AdcDumpMMValue>\n");
		}

		bytes += sprintf(&data[bytes], "</Item>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("short res write fail.");
			goto save_end;
		}
		bytes = 0;
	}

	/* rawdata max limit */
	bytes += sprintf(&data[bytes], "<Item name=\"Rawdata Test Sets\">\n");
	bytes += sprintf(&data[bytes], "<TotalFrameCnt>%d</TotalFrameCnt>\n", TOTAL_FRAME_NUM);
	bytes += sprintf(&data[bytes], "<MaxRawLimit>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.max_limits[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</MaxRawLimit>\n");
	/* BeyondRawdataUpperLimit */
	bytes += sprintf(&data[bytes], "<BeyondRawdataUpperLimitCnt>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->open_res.beyond_max_limit_cnt[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</BeyondRawdataUpperLimitCnt>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("rawdata limit write failed");
		goto save_end;
	}
	bytes = 0;

	/* rawdata min limit */
	bytes += sprintf(&data[bytes], "<MinRawLimit>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.min_limits[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</MinRawLimit>\n");
	/* BeyondRawdataLower limit */
	bytes += sprintf(&data[bytes], "<BeyondRawdataLowerLimitCnt>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->open_res.beyond_min_limit_cnt[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</BeyondRawdataLowerLimitCnt>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("rawdata limit write failed");
		goto save_end;
	}
	bytes = 0;

	/* Max Accord limit */
	bytes += sprintf(&data[bytes], "<MaxAccordLimit>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.deviation_limits[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</MaxAccordLimit>\n");
	/* BeyondAccordLimitCnt */
	bytes += sprintf(&data[bytes], "<BeyondAccordLimitCnt>\n");
	for (i = 0; i < tx * rx; i++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->open_res.beyond_accord_limit_cnt[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</BeyondAccordLimitCnt>\n");
	bytes += sprintf(&data[bytes], "</Item>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("rawdata limit write failed");
		goto save_end;
	}
	bytes = 0;

	/* save noise limit */
	if (ts_test->test_result[GTP_NOISE_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Diffdata Test Sets\">\n");
		bytes += sprintf(&data[bytes], "<TotalFrameCnt>%d</TotalFrameCnt>\n",
				NOISEDATA_TEST_TIMES);

		for (freq_index = 0; freq_index < freq_num; freq_index++) {
			bytes += sprintf(&data[bytes], "<MaxJitterLimit%d>\n", freq_index);
			for (i = 0; i < tx * rx; i++) {
				bytes += sprintf(&data[bytes], "%d,",
						ts_test->test_params.noise_threshold[freq_index][i]);
				if ((i + 1) % tx == 0)
					bytes += sprintf(&data[bytes], "\n");
			}
			bytes += sprintf(&data[bytes], "</MaxJitterLimit%d>\n", freq_index);

			fs_write(data, bytes);
			bytes = 0;
		}

		bytes += sprintf(&data[bytes], "</Item>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("noise limit write failed");
			goto save_end;
		}
		bytes = 0;
	}

	/* save self rawdata limit */
	if (ts_test->test_result[GTP_SELFCAP_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Self Rawdata Test Sets\">\n");
		bytes += sprintf(&data[bytes], "<TotalFrameCnt>1</TotalFrameCnt>\n");
		bytes += sprintf(&data[bytes], "<MaxRawLimit>\n");
		for (i = 0; i < tx + rx; i++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.self_max_limits[i]);
			if ((i + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		if ((tx + rx) % tx != 0)
			bytes += sprintf(&data[bytes], "\n");
		bytes += sprintf(&data[bytes], "</MaxRawLimit>\n");
		bytes += sprintf(&data[bytes], "<MinRawLimit>\n");
		for (i = 0; i < tx + rx; i++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.self_min_limits[i]);
			if ((i + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		if ((tx + rx) % tx != 0)
			bytes += sprintf(&data[bytes], "\n");
		bytes += sprintf(&data[bytes], "</MinRawLimit>\n");
		bytes += sprintf(&data[bytes], "</Item>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("self rawdata limit write failed");
			goto save_end;
		}
		bytes = 0;
	}

	/* save selfnoise limit */
	if (ts_test->test_result[GTP_SELFNOISE_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Self Diffdata Test Sets\">\n");
		bytes += sprintf(&data[bytes], "<TotalFrameCnt>1</TotalFrameCnt>\n");
		bytes += sprintf(&data[bytes], "<MaxJitterLimit>%d</MaxJitterLimit>\n", ts_test->test_params.self_noise_threshold);
		bytes += sprintf(&data[bytes], "</Item>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("raw limit write failed");
			goto save_end;
		}
		bytes = 0;
	}

	if (ts_test->test_params.test_items[GTP_SPECIAL_DELTA_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"SpecialDelta Test Sets\">\n");
		bytes += sprintf(&data[bytes], "<SpecialAccordLimit>\n");
		for (i = 0; i < tx * rx; i++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->test_params.special_freq_deviation_limts[i]);
			if ((i + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		bytes += sprintf(&data[bytes], "</SpecialAccordLimit>\n");
		bytes += sprintf(&data[bytes], "</Item>\n");
	}

	/* save diff shift test limit */
	if (ts_test->test_result[GTP_DIFF_SHIFT_TEST]) {
		bytes += sprintf(&data[bytes], "<Item name=\"Diff Shift Test Sets\">\n");

		for (freq_index = 0; freq_index < MAX_SCAN_FREQ_NUM; freq_index++) {
			if (!ts_test->test_params.diff_shift_freq[freq_index])
				continue;

			bytes += sprintf(&data[bytes], "<DiffShiftLimit%d>\n", freq_index);

			for (i = 0; i < tx * rx; i++) {
				bytes += sprintf(&data[bytes], "%d,",
						ts_test->test_params.diff_shift_max[freq_index][i]);
				if ((i + 1) % tx == 0)
					bytes += sprintf(&data[bytes], "\n");
			}

			bytes += sprintf(&data[bytes], "</DiffShiftLimit%d>\n", freq_index);
		}

		bytes += sprintf(&data[bytes], "</Item>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("diff shift limit write failed");
			goto save_end;
		}
		bytes = 0;
	}

	bytes += sprintf(&data[bytes], "</TestItems>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("limit write fail.");

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_rawdata(struct goodix_ts_test *ts_test)
{
	int i;
	int j;
	int ret;
	int bytes = 0;
	s16 stat_result[3];
	char *data = NULL;
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int len = tx * rx;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<RawDataRecord>\n");
	for (i = 0; i < TOTAL_FRAME_NUM; i++) {
		goodix_data_cal(ts_test->rawdata[i].data, len, stat_result);
		bytes += sprintf(&data[bytes], "<DataContent No.=\"%d\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n", i, len,
						 stat_result[1], stat_result[2], stat_result[0]);
		for (j = 0; j < len; j++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->rawdata[i].data[j]);
			if ((j + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		bytes += sprintf(&data[bytes], "</DataContent>\n");
		goodix_data_cal(ts_test->accord_arr[i].data, len, stat_result);
		bytes += sprintf(&data[bytes], "<RawAccord No.=\"%d\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n", i, len,
						 stat_result[1], stat_result[2], stat_result[0]);
		for (j = 0; j < len; j++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->accord_arr[i].data[j]);
			if ((j + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		bytes += sprintf(&data[bytes], "</RawAccord>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("rawdata write fail.");
			goto save_end;
		}
		bytes = 0;
	}

	bytes += sprintf(&data[bytes], "</RawDataRecord>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("rawdata write fail.");

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_noise_data(struct goodix_ts_test *ts_test)
{
	int i;
	int j;
	int ret = 0;
	int bytes = 0;
	s16 stat_result[3];
	char *data = NULL;
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int len = tx * rx;
	int freq_num = ts_test->ts->ic_info.parm.mutual_freq_num;
	int freq_cnt;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<DiffDataRecord>\n");
	for (freq_cnt = 0; freq_cnt < freq_num; freq_cnt++) {
		bytes += sprintf(&data[bytes], "<MutualFreq>%d</MutualFreq>\n", ts_test->ts->ic_info.parm.mutual_freq[freq_cnt]);
		for (i = 0; i < NOISEDATA_TEST_TIMES; i++) {
			goodix_data_cal(ts_test->mutual_freq_diff[freq_cnt].noisedata[i].data, len, stat_result);
			bytes += sprintf(&data[bytes], "<DataContent No.=\"%d\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n", i,
							 len, stat_result[1], stat_result[2], stat_result[0]);
			for (j = 0; j < len; j++) {
				bytes += sprintf(&data[bytes], "%d,", ts_test->mutual_freq_diff[freq_cnt].noisedata[i].data[j]);
				if ((j + 1) % tx == 0)
					bytes += sprintf(&data[bytes], "\n");
			}
			bytes += sprintf(&data[bytes], "</DataContent>\n");
			ret = fs_write(data, bytes);
			if (ret < 0) {
				ts_err("noisedata write fail.");
				goto save_end;
			}
			bytes = 0;
		}
	}

	bytes += sprintf(&data[bytes], "</DiffDataRecord>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("noisedata write fail.");

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_self_data(struct goodix_ts_test *ts_test, s16 *src_data, u8 *title, int len)
{
	int i;
	int ret = 0;
	int bytes = 0;
	char *data;
	s16 stat_result[3];
	int tx = ts_test->test_params.drv_num;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<%s>\n", title);
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("rawdata write fail.");
		goto save_end;
	}
	bytes = 0;

	goodix_data_cal(src_data, len, stat_result);
	bytes += sprintf(&data[bytes], "<DataContent No.=\"0\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n", len,
					 stat_result[1], stat_result[2], stat_result[0]);
	for (i = 0; i < len; i++) {
		bytes += sprintf(&data[bytes], "%d,", src_data[i]);
		if ((i + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	if (len % tx != 0)
		bytes += sprintf(&data[bytes], "\n");
	bytes += sprintf(&data[bytes], "</DataContent>\n");
	bytes += sprintf(&data[bytes], "</%s>\n", title);
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("rawdata write fail.");

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_diff_shift_data(struct goodix_ts_test *ts_test)
{
	int i;
	int j;
	int ret = 0;
	int bytes = 0;
	s16 stat_result[3];
	char *data = NULL;
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int len = tx * rx;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("alloc memory failed for ");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<DiffShiftDataRecord>\n");
	for (i = 0; i < MAX_SCAN_FREQ_NUM; i++) {
		if (!ts_test->test_params.diff_shift_freq[i])
			continue;
		goodix_data_cal(ts_test->diff_shift_noise[i], len, stat_result);
		bytes += sprintf(&data[bytes],
			"<DataContent No.=\"%d\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n",
			i, len, stat_result[1], stat_result[2], stat_result[0]);
		for (j = 0; j < len; j++) {
			bytes += sprintf(&data[bytes], "%d,", ts_test->diff_shift_noise[i][j]);
			if ((j + 1) % tx == 0)
				bytes += sprintf(&data[bytes], "\n");
		}
		bytes += sprintf(&data[bytes], "</DataContent>\n");
		ret = fs_write(data, bytes);
		if (ret < 0) {
			ts_err("diff shift data write fail.");
			goto save_end;
		}
		bytes = 0;
	}

	bytes += sprintf(&data[bytes], "</DiffShiftDataRecord>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("raw shift data write fail.");

save_end:
	kfree(data);
	return ret;
}

static int goodix_save_special_deltadata(struct goodix_ts_test *ts_test)
{
	int j;
	int bytes = 0;
	int ret = 0;
	char *data;
	s16 stat_result[3];
	int tx = ts_test->test_params.drv_num;
	int rx = ts_test->test_params.sen_num;
	int len = tx * rx;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);

	bytes += sprintf(&data[bytes], "<SpecialDeltaDataRecord>\n");
	goodix_data_cal(ts_test->special_freq_accord_arr.data, len, stat_result);
	bytes += sprintf(&data[bytes], "<DataContent No=\"%d\" DataCount=\"%d\" Maximum=\"%d\" Minimum=\"%d\" Average=\"%d\">\n", 0, len,
					 stat_result[1], stat_result[2], stat_result[0]);
	for (j = 0; j < len; j++) {
		bytes += sprintf(&data[bytes], "%d,", ts_test->special_freq_accord_arr.data[j]);
		if ((j + 1) % tx == 0)
			bytes += sprintf(&data[bytes], "\n");
	}
	bytes += sprintf(&data[bytes], "</DataContent>\n");
	bytes += sprintf(&data[bytes], "</SpecialDeltaDataRecord>\n");

	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("rawdata write fail.");
	return 0;
}

static int goodix_save_data(struct goodix_ts_test *ts_test)
{
	int ret = 0;
	int bytes = 0;
	char *data = NULL;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "<DataRecord>\n");
	ret = fs_write(data, bytes);
	if (ret < 0) {
		ts_err("rawdata record lable failed");
		goto save_end;
	}
	bytes = 0;

	ret = goodix_save_rawdata(ts_test);
	if (ret < 0)
		goto save_end;

	if (ts_test->test_result[GTP_NOISE_TEST]) {
		ret = goodix_save_noise_data(ts_test);
		if (ret < 0)
			goto save_end;
	}

	if (ts_test->test_result[GTP_SELFCAP_TEST]) {
		ret = goodix_save_self_data(ts_test, ts_test->self_rawdata.data, "selfDataRecord", ts_test->self_rawdata.size);
		if (ret < 0)
			goto save_end;
	}

	if (ts_test->test_result[GTP_SELFNOISE_TEST]) {
		ret = goodix_save_self_data(ts_test, ts_test->self_noisedata.data, "selfDiffDataRecord", ts_test->self_noisedata.size);
		if (ret < 0)
			goto save_end;
	}

	if (ts_test->test_params.test_items[GTP_SPECIAL_DELTA_TEST]) {
		ret = goodix_save_special_deltadata(ts_test);
	}

	if (ts_test->test_result[GTP_DIFF_SHIFT_TEST]) {
		ret = goodix_save_diff_shift_data(ts_test);
		if (ret < 0)
			goto save_end;
	}

	bytes += sprintf(&data[bytes], "</DataRecord>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("rawdata data record lable fail.");

save_end:
	kfree(data);
	return ret;
}

/* save end tag in csv file */
static int goodix_save_tail(struct goodix_ts_test *ts_test)
{
	int ret = 0;
	int bytes = 0;
	char *data = NULL;

	data = kzalloc(MAX_DATA_BUFFER, GFP_KERNEL);
	if (!data) {
		ts_err("memory failed");
		return -ENOMEM;
	}

	bytes += sprintf(&data[bytes], "</TESTLOG>\n");
	ret = fs_write(data, bytes);
	if (ret < 0)
		ts_err("tail write failed");

	kfree(data);
	return ret;
}

static void goodix_save_result_data(struct goodix_ts_test *ts_test)
{
	int ret;

	/* save header */
	ret = goodix_save_header(ts_test);
	if (ret < 0)
		return;

	/* save limits */
	ret = goodix_save_limits(ts_test);
	if (ret < 0)
		return;

	/* save data */
	ret = goodix_save_data(ts_test);
	if (ret < 0)
		return;

	/* save tail */
	ret = goodix_save_tail(ts_test);
	if (ret < 0)
		return;

	fs_write(ts_test->test_info, strlen(ts_test->test_info));
}
#endif /* SAVE_IN_CSV*/

static void goodix_put_test_result(struct goodix_ts_test *ts_test)
{
	int i;
	bool have_bus_error = false;
	bool have_panel_error = false;
	char statistics_data[STATISTICS_DATA_LEN] = {0};
	struct goodix_ts_core *ts = ts_test->ts;

	ts_info("put test result IN");

	/* check if there have bus error */
	for (i = 0; i < MAX_TEST_ITEMS; i++) {
		if (ts_test->test_result[i] == SYS_SOFTWARE_REASON)
			have_bus_error = true;
		else if (ts_test->test_result[i] == GTP_PANEL_REASON)
			have_panel_error = true;
	}
	ts_info("Have bus error:%d, have panel error: %d", have_bus_error, have_panel_error);
	if (have_bus_error || have_panel_error)
		goodix_strncat(ts_test->test_info, "[FAIL]-", TS_RAWDATA_RESULT_MAX);
	else
		goodix_strncat(ts_test->test_info, "[PASS]-", TS_RAWDATA_RESULT_MAX);

	if (have_bus_error)
		goodix_strncat(ts_test->test_info, "0F-", TS_RAWDATA_RESULT_MAX);
	else
		goodix_strncat(ts_test->test_info, "0P-", TS_RAWDATA_RESULT_MAX);

	for (i = 0; i < MAX_TEST_ITEMS; i++) {
		/* if have tested, show result */
		if (ts_test->test_result[i]) {
			if (GTP_TEST_PASS == ts_test->test_result[i])
				goodix_strncatint(ts_test->test_info, i, "%dP-",
					TS_RAWDATA_RESULT_MAX);
			else
				goodix_strncatint(ts_test->test_info, i, "%dF-",
					TS_RAWDATA_RESULT_MAX);
		}
	}

	/* calculate rawdata min avg max value*/
	if (ts_test->rawdata[0].size) {
		goodix_data_statistics(
				ts_test->rawdata[0].data,
				ts_test->rawdata[0].size,
				statistics_data,
				STATISTICS_DATA_LEN);
		goodix_strncat(ts_test->test_info, statistics_data,
			TS_RAWDATA_RESULT_MAX);
	} else {
		ts_err("NO valiable rawdata");
		goodix_strncat(ts_test->test_info, "[0,0,0]",
			TS_RAWDATA_RESULT_MAX);
	}

	/* calculate noisedata min avg max value*/
	if (ts_test->test_params.test_items[GTP_NOISE_TEST]) {
		if (ts_test->mutual_freq_diff[0].noisedata[0].size) {
			goodix_data_statistics(
					ts_test->mutual_freq_diff[0].noisedata[0].data,
					ts_test->mutual_freq_diff[0].noisedata[0].size,
					statistics_data,
					STATISTICS_DATA_LEN);
			goodix_strncat(ts_test->test_info, statistics_data,
				TS_RAWDATA_RESULT_MAX);
		} else {
			ts_err("NO valiable noisedata");
			goodix_strncat(ts_test->test_info, "[0,0,0]",
				TS_RAWDATA_RESULT_MAX);
		}
	}

	/* calculate self_rawdata min avg max value*/
	if (ts_test->test_params.test_items[GTP_SELFCAP_TEST]) {
		if (ts_test->self_rawdata.size) {
			goodix_data_statistics(
					ts_test->self_rawdata.data,
					ts_test->self_rawdata.size,
					statistics_data,
					STATISTICS_DATA_LEN);
			goodix_strncat(ts_test->test_info, statistics_data,
				TS_RAWDATA_RESULT_MAX);
		} else {
			ts_err("NO valiable self_rawdata");
			goodix_strncat(ts_test->test_info, "[0,0,0]",
				TS_RAWDATA_RESULT_MAX);
		}
	}

	/* calculate self_noisedata min avg max value*/
	if (ts_test->test_params.test_items[GTP_SELFNOISE_TEST]) {
		if (ts_test->self_noisedata.size) {
			goodix_data_statistics(
					ts_test->self_noisedata.data,
					ts_test->self_noisedata.size,
					statistics_data,
					STATISTICS_DATA_LEN);
			goodix_strncat(ts_test->test_info, statistics_data,
				TS_RAWDATA_RESULT_MAX);
		} else {
			ts_err("NO valiable self_noisedata");
			goodix_strncat(ts_test->test_info, "[0,0,0]",
				TS_RAWDATA_RESULT_MAX);
		}
	}

	goodix_strncat(ts_test->test_info, "-GT",
		TS_RAWDATA_RESULT_MAX);
	goodix_strncat(ts_test->test_info, ts->fw_version.patch_pid,
		TS_RAWDATA_RESULT_MAX);
	goodix_strncat(ts_test->test_info, "\n",
		TS_RAWDATA_RESULT_MAX);

	ts_info("%s", ts_test->test_info);
#ifdef SAVE_IN_CSV
	/* save result to file */
	goodix_save_result_data(ts_test);
#endif
}

int goodix_do_inspect_thread(void *arg)
{
	int ret;
	int i;
	struct goodix_ts_test *ts_test;
	struct goodix_ts_core *cd = arg;

	if (!cd) {
		ts_err("core_data is NULL");
		complete(&inspect_done);
		return -ENODEV;
	}

	mutex_lock(&inspect_mutex);

	if (!g_ts_test) {
		g_ts_test = vzalloc(sizeof(*g_ts_test));
		if (!g_ts_test) {
			ts_err("Failed to alloc mem");
			g_test_stage = TEST_STAGE_MALLOC_FAIL;
			mutex_unlock(&inspect_mutex);
			complete(&inspect_done);
			return -ENOMEM;
		}
	} else {
		memset(g_ts_test, 0, sizeof(*g_ts_test));
	}
	ts_test = g_ts_test;

	cd->is_inspecting = true;
	g_test_stage = TEST_STAGE_PREPARE;
	ts_test->ts = cd;
	ret = goodix_tptest_prepare(ts_test);
	if (ret < 0) {
		ts_err("[FAIL]-0F-software reason");
		fs_write("[FAIL]-0F-software reason\n", strlen("[FAIL]-0F-software reason\n"));
		goto exit_finish;
	}
	ts_info("TP test prepare OK");
	g_test_stage = TEST_STAGE_OPEN_SHORT_S;
	goodix_version_test(ts_test);
	goodix_check_key_info_test(ts_test);
	goodix_custom_info_test(ts_test);
	goodix_reset_test(ts_test);
	goodix_capacitance_test(ts_test); /* 1F 3F 6F 7F test */
	if (ts_test->test_params.test_items[GTP_SHORT_TEST])
		goodix_shortcircut_test(ts_test); /* 5F test */
	goodix_tptest_finish(ts_test);
	g_test_stage = TEST_STAGE_OPEN_SHORT_E;

	final_result = true;
	for (i = 0; i < MAX_TEST_ITEMS; i++) {
		if ((ts_test->test_result[i] > 0) &&
				(ts_test->test_result[i] != GTP_TEST_PASS)) {
			final_result = false;
			break;
		}
	}

exit_finish:
	cd->is_inspecting = false;
	mutex_unlock(&inspect_mutex);
	complete(&inspect_done);
#ifdef CONFIG_FACTORY_BUILD
	ts_test_cmd_enable(true);
#ifdef TOUCH_THP_SUPPORT
	if (cd->enable_touch_raw)
		goodix_htc_start_calibration();//only for THP is open
#endif
	msleep(300);
#endif
	return ret;
}

/* inspect timeout: normal ~3s, worst ~3.6s with flash_cmd retries */
#define INSPECT_TIMEOUT msecs_to_jiffies(5000)

int goodix_inspect_run(struct goodix_ts_core *cd, const char *name)
{
	int i;
	unsigned long ret;
	struct task_struct *inspect_thrd;

	if (cd->is_inspecting) {
		ts_err("inspect already running, skip");
		return -EBUSY;
	}

	reinit_completion(&inspect_done);

	/* create and run update thread */
	memset(g_limit_file_name, 0, sizeof(g_limit_file_name));
	for (i = 0; i < sizeof(g_limit_file_name); i++) {
		if (name[i] == '\0' || name[i] == '\n' || name[i] == '\r')
			break;
		g_limit_file_name[i] = name[i];
	}

	inspect_thrd = kthread_run(goodix_do_inspect_thread,
				cd, "goodix_do_inspect_thread");
	if (IS_ERR_OR_NULL(inspect_thrd)) {
		ts_err("Failed to create inspect thread:%ld",
		       PTR_ERR(inspect_thrd));
		return -EFAULT;
	}
	ret = wait_for_completion_timeout(&inspect_done, INSPECT_TIMEOUT);
	if (!ret) {
		ts_err("inspect thread timeout");
		cd->is_inspecting = false;
		return -ETIMEDOUT;
	}
	return 0;
}

static int show_result_all_show(struct seq_file *m, void *v)
{
	int ret;

	mutex_lock(&inspect_mutex);

	if (!g_ts_test) {
		mutex_unlock(&inspect_mutex);
		ts_info("no test result, please retry");
		return -EAGAIN;
	}

	/* check diff shift test result */
	goodix_diff_shift_result_check(g_ts_test);
	goodix_put_test_result(g_ts_test);
	ret = seq_write(m, g_test_result_buf, g_test_result_size);
	deinit_test_result_buf();

	vfree(g_ts_test);
	g_ts_test = NULL;

	mutex_unlock(&inspect_mutex);
	return ret;
}

static int show_result_all_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, show_result_all_show,
			pde_data(inode), DEFAULT_SEQ_FILE_SIZE);
}

static int show_result_show(struct seq_file *m, void *v)
{
	struct goodix_ts_test *ts_test;

	if (!g_ts_test) {
		ts_info("no test result, please retry");
		return -EAGAIN;
	}
	ts_test = g_ts_test;
	//send config to touch ic
	goodix_tptest_finish(ts_test);
	seq_printf(m, "%s\n", final_result ? "PASS" : "FAIL");
	return 0;
}

static int show_result_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, show_result_show,
			pde_data(inode), PAGE_SIZE);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops show_result_all = {
	.proc_open = show_result_all_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static const struct proc_ops show_result = {
	.proc_open = show_result_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations show_result_all = {
	.open = show_result_all_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations show_result = {
	.open = show_result_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

int goodix_get_rawdata(struct device *dev, struct ts_rawdata_info *info)
{
	int ret;
	struct goodix_ts_test *ts_test = NULL;
	bool flag = false;

	flag = goodix_get_ic_self_test_mode();

	ts_info("tp self test start");

	if (!dev || !info) {
		ts_err("core_data or info is NULL");
		return -ENODEV;
	}

	ts_test = vzalloc(sizeof(*ts_test));
	if (!ts_test) {
		ts_err("Failed to alloc mem");
		return -ENOMEM;
	}

	ts_test->ts = dev_get_drvdata(dev);
	ret = goodix_tptest_prepare(ts_test);
	if (ret < 0) {
		ts_err("Failed to prepare TP test, exit");
		strncpy(info->result, "[FAIL]-0F-software reason\n", TS_RAWDATA_RESULT_MAX - 1);
		goto exit_finish;
	}
	strncpy(info->result, ts_test->test_info, TS_RAWDATA_RESULT_MAX - 1);
	ts_info("TP test prepare OK");

	goodix_capacitance_test(ts_test); /* 1F 3F 6F 7F test */
	if (ts_test->test_params.test_items[GTP_SHORT_TEST])
		goodix_shortcircut_test(ts_test); /* 5F test */
	goodix_put_test_result(ts_test);
	if (flag) {
		strncpy(info->result, ts_test->test_info, TS_RAWDATA_RESULT_MAX - 1);
		goodix_tptest_finish(ts_test);
	}

exit_finish:
	vfree(ts_test);
#ifdef CONFIG_FACTORY_BUILD
	ts_test_cmd_enable(true);
#endif
	return ret;
}

int inspect_module_init(struct goodix_ts_core *core_data)
{
	if (module_initialized) {
		ts_info("inspect module has already init");
		return 0;
	}

	proc_create_data("show_result_all",
			0660, NULL, &show_result_all, core_data);
	proc_create_data("show_result",
			0660, NULL, &show_result, core_data);
	module_initialized = true;
	ts_info("inspect module init success");
	return 0;
}

void inspect_module_exit(void)
{
	ts_info("inspect module exit");
	if (!module_initialized)
		return;

	remove_proc_entry("goodix_ts.0/show_result_all", NULL);
	remove_proc_entry("goodix_ts.0/show_result", NULL);
	module_initialized = false;
}

