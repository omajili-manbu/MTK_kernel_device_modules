/************************************************************************
* Copyright (c) 2012-2020, Focaltech Systems (R)£¬All Rights Reserved.
*
* File Name: focaltech_test_ini.h
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-01
*
* Abstract: parsing function of INI file
*
************************************************************************/
#ifndef _INI_H
#define _INI_H
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define MAX_KEYWORD_NUM                         (1000)
#define MAX_KEYWORD_NAME_LEN                    (50)
#define MAX_KEYWORD_VALUE_LEN                   (512)
#define MAX_KEYWORD_VALUE_ONE_LEN               (16)
#define MAX_INI_LINE_LEN        (MAX_KEYWORD_NAME_LEN + MAX_KEYWORD_VALUE_LEN)
#define MAX_INI_SECTION_NUM                     (20)
#define MAX_IC_NAME_LEN                         (32)
#define MAX_TEST_ITEM                           (20)
#define IC_CODE_OFFSET                          (16)
#define FTS_MAX_COMPATIBLE_TYPE                  4

/* _b571：CSV/TXT_SUPPORT 迁活区（原定义在 focaltech_test.h 的死区 #if 0 内，
 * 导致 csv/txt 链整段被剔除——blob 二进制证明两宏 =1：csv vmalloc(0x64000)+
 * TXT testresult 分配均在）。*/
#define CSV_SUPPORT                              1
#define TXT_SUPPORT                              1

/*****************************************************************************
* enumerations, structures and unions
*****************************************************************************/
struct ini_ic_type {
    char ic_name[MAX_IC_NAME_LEN];
    u32 ic_type;
};

enum line_type {
    LINE_SECTION = 1,
    LINE_KEYWORD = 2,
    LINE_OTHER = 3,
};

struct ini_keyword {
    char name[MAX_KEYWORD_NAME_LEN];
    char value[MAX_KEYWORD_VALUE_LEN];
};

struct ini_section {
    char name[MAX_KEYWORD_NAME_LEN];
    int keyword_num;
    /* point to ini.tmp, don't need free */
    struct ini_keyword *keyword;
};

struct ini_data {
    char *data;
    int length;
    int keyword_num_total;
    int section_num;
    struct ini_section section[MAX_INI_SECTION_NUM];
    struct ini_keyword *tmp;
    char ic_name[MAX_IC_NAME_LEN];
    u32 ic_code;
    char ini_ver[MAX_KEYWORD_NAME_LEN];
};

/*-------add fts_test struct for multi file using----------*/
#define TEST_SAVE_FAIL_RESULT                   0
#define TEST_ITEM_NAME_MAX                      32
#define TEST_ITEM_COUNT_MAX                     32

struct item_info {
    char name[TEST_ITEM_NAME_MAX];
    int code;
    int *data;
    int datalen;
    int result;
    int mc_sc;
    int key_support;
};

struct fts_test_data {
    int item_count;
    struct item_info info[TEST_ITEM_COUNT_MAX];
};

/* incell */
struct incell_testitem {
    u32 short_test                  : 1;
    u32 open_test                   : 1;
    u32 cb_test                     : 1;
    u32 rawdata_test                : 1;
    u32 lcdnoise_test               : 1;
    u32 keyshort_test               : 1;
    u32 mux_open_test               : 1;
};

struct incell_threshold_b {
    int short_res_min;
    int short_res_vk_min;
    int open_cb_min;
    int open_k1_check;
    int open_k1_value;
    int open_k2_check;
    int open_k2_value;
    int cb_min;
    int cb_max;
    int cb_vkey_check;
    int cb_min_vk;
    int cb_max_vk;
    int rawdata_min;
    int rawdata_max;
    int rawdata_vkey_check;
    int rawdata_min_vk;
    int rawdata_max_vk;
    int lcdnoise_frame;
    int lcdnoise_coefficient;
    int lcdnoise_coefficient_vkey;
    int open_diff_min;
    int open_diff_max;
    int open_nmos;
    int keyshort_k1;
    int keyshort_cb_max;
    int rawdata2_min;
    int rawdata2_max;
    int mux_open_cb_min;
    int open_delta_V;
};

struct incell_threshold {
    struct incell_threshold_b basic;
    int *rawdata_min;
    int *rawdata_max;
    int *rawdata2_min;
    int *rawdata2_max;
    int *cb_min;
    int *cb_max;
};

struct incell_test {
    struct incell_threshold thr;
    union {
        int tmp;
        struct incell_testitem item;
    } u;
};

/* mc_sc */
enum mapping_type {
    MAPPING = 0,
    NO_MAPPING = 1,
};

struct mc_sc_testitem {
    u32 rawdata_test                : 1;
    u32 rawdata_uniformity_test     : 1;
    u32 scap_cb_test                : 1;
    u32 scap_rawdata_test           : 1;
    u32 short_test                  : 1;
    u32 panel_differ_test           : 1;
    u32 noise_test                  : 1;
    u32 spi_test                    : 1;
    u32 rst_test                    : 1;    /* blob bit8（ex_rst/rst） */
    u32 rawshift_test               : 1;    /* blob bit9（rawshift 族结果位） */
    u32 auxiliary_freq_noise_test   : 1;    /* blob bit10 */
    u32 jump_freq_noise_test        : 1;    /* blob bit11 */
};

struct mc_sc_threshold_b {
    int rawdata_h_min;
    int rawdata_h_max;
    int rawdata_set_hfreq;
    int rawdata_l_min;
    int rawdata_l_max;
    int rawdata_set_lfreq;
    int uniformity_check_tx;
    int uniformity_check_rx;
    int uniformity_check_min_max;
    int uniformity_tx_hole;
    int uniformity_rx_hole;
    int uniformity_min_max_hole;
    int scap_cb_off_min;
    int scap_cb_off_max;
    int scap_cb_wp_off_check;
    int scap_cb_on_min;
    int scap_cb_on_max;
    int scap_cb_wp_on_check;
    int scap_rawdata_off_min;
    int scap_rawdata_off_max;
    int scap_rawdata_wp_off_check;
    int scap_rawdata_on_min;
    int scap_rawdata_on_max;
    int scap_rawdata_wp_on_check;
    int short_cg;
    int short_cc;
    int panel_differ_min;
    int panel_differ_max;
    int scap_cb_hi_min;
    int scap_cb_hi_max;
    int scap_cb_hi_check;
    int scap_rawdata_hi_min;
    int scap_rawdata_hi_max;
    int scap_rawdata_hi_check;
    int scap_cb_hov_min;
    int scap_cb_hov_max;
    int scap_cb_hov_check;
    int scap_rawdata_hov_min;
    int scap_rawdata_hov_max;
    int scap_rawdata_hov_check;
    int noise_max;
    int noise_framenum;
    int noise_mode;
    int noise_polling;

    int scap_cb_on_gcb_min;
    int scap_cb_on_gcb_max;
    int scap_cb_off_gcb_min;
    int scap_cb_off_gcb_max;
    int scap_cb_hi_gcb_min;
    int scap_cb_hi_gcb_max;
    int scap_cb_on_cf_min;
    int scap_cb_on_cf_max;
    int scap_cb_off_cf_min;
    int scap_cb_off_cf_max;
    int scap_cb_hi_cf_min;
    int scap_cb_hi_cf_max;

    int mcap_cmb_min;
    int mcap_cmb_max;
    int auxiliary_fre_noise_scan_mode;
    int auxiliary_fre_noise_framenum;

    int auxiliary_fre_noise_test_fre0;
    int auxiliary_fre_noise_test_fre0_threshold;

    int auxiliary_fre_noise_test_fre1;
    int auxiliary_fre_noise_test_fre1_threshold;

    int auxiliary_fre_noise_test_fre2;
    int auxiliary_fre_noise_test_fre2_threshold;

    int auxiliary_fre_noise_test_fre3;
    int auxiliary_fre_noise_test_fre3_threshold;

    int auxiliary_fre_noise_test_fre4;
    int auxiliary_fre_noise_test_fre4_threshold;

    int auxiliary_fre_noise_test_fre5;
    int auxiliary_fre_noise_test_fre5_threshold;

    /* --- FT5672 追加（blob .text 实测；basic 内偏移 0x120..0x1ac）--- */
    int jump_fre_noise_scan_mode;
    int jump_fre_noise_framenum;
    int jump_fre_noise_test_fre0;
    int jump_fre_noise_test_fre1;
    int jump_fre_noise_test_fre2;
    int jump_fre_noise_test_fre3;
    int jump_fre_noise_test_fre4;
    int jump_fre_noise_test_fre5;
    int jump_fre_noise_test_fre01_threshold;
    int jump_fre_noise_test_fre02_threshold;
    int jump_fre_noise_test_fre03_threshold;
    int jump_fre_noise_test_fre04_threshold;
    int jump_fre_noise_test_fre05_threshold;
    int jump_fre_noise_test_fre12_threshold;
    int jump_fre_noise_test_fre13_threshold;
    int jump_fre_noise_test_fre14_threshold;
    int jump_fre_noise_test_fre15_threshold;
    int jump_fre_noise_test_fre23_threshold;
    int jump_fre_noise_test_fre24_threshold;
    int jump_fre_noise_test_fre25_threshold;
    int jump_fre_noise_test_fre34_threshold;
    int jump_fre_noise_test_fre35_threshold;
    int jump_fre_noise_test_fre45_threshold;
    int rawshift_fre;                 /* +0x17c ini: RawShift_Fre_Test（blob 无写入点） */
    int rawshift_fre_frames;          /* +0x180 */
    int rawshift_fre_threshold;       /* +0x184 */
    int rawshift_fre_freq[6];         /* +0x188..0x19c */
    int rawshift_pic;                 /* +0x1a0 ini: RawShift_PIC */
    int rawshift_pic_frames;          /* +0x1a4 */
    int rawshift_pic_threshold;       /* +0x1a8 */
    int reserved_thr_1ac;             /* +0x1ac */
};

struct mc_sc_threshold {
    struct mc_sc_threshold_b basic;
    int *rawdata_h_min;
    int *rawdata_h_max;
    int *rawdata_l_min;
    int *rawdata_l_max;
    int *tx_linearity_max;
    int *tx_linearity_min;
    int *rx_linearity_max;
    int *rx_linearity_min;
    int *scap_cb_off_min;
    int *scap_cb_off_max;
    int *scap_cb_on_min;
    int *scap_cb_on_max;
    int *scap_cb_hi_min;
    int *scap_cb_hi_max;
    int *scap_cb_hov_min;
    int *scap_cb_hov_max;
    int *scap_rawdata_off_min;
    int *scap_rawdata_off_max;
    int *scap_rawdata_on_min;
    int *scap_rawdata_on_max;
    int *scap_rawdata_hi_min;
    int *scap_rawdata_hi_max;
    int *scap_rawdata_hov_min;
    int *scap_rawdata_hov_max;
    int *panel_differ_min;
    int *panel_differ_max;
    int *noise_min;
    int *noise_max;

    int *scap_cb_on_cf_min;
    int *scap_cb_on_cf_max;
    int *scap_cb_off_cf_min;
    int *scap_cb_off_cf_max;
    int *scap_cb_hi_cf_min;
    int *scap_cb_hi_cf_max;

    int *mcap_cmb_min;
    int *mcap_cmb_max;
};

struct mc_sc_test {
    struct mc_sc_threshold thr;
    union {
        u32 tmp;
        struct mc_sc_testitem item;
    } u;
};

/* sc */
struct sc_testitem {
    u32 rawdata_test                : 1;
    u32 cb_test                     : 1;
    u32 delta_cb_test               : 1;
    u32 short_test                  : 1;
};

struct sc_threshold_b {
    int rawdata_min;
    int rawdata_max;
    int cb_min;
    int cb_max;
    int dcb_base;
    int dcb_differ_max;
    int dcb_key_check;
    int dcb_key_differ_max;
    int dcb_ds1;
    int dcb_ds2;
    int dcb_ds3;
    int dcb_ds4;
    int dcb_ds5;
    int dcb_ds6;
    int dcb_critical_check;
    int dcb_cs1;
    int dcb_cs2;
    int dcb_cs3;
    int dcb_cs4;
    int dcb_cs5;
    int dcb_cs6;
    int short_min;
};

struct sc_threshold {
    struct sc_threshold_b basic;
    int *rawdata_min;
    int *rawdata_max;
    int *cb_min;
    int *cb_max;
    int *dcb_sort;
    int *dcb_base;
};

struct sc_test {
    struct sc_threshold thr;
    union {
        u32 tmp;
        struct sc_testitem item;
    } u;
};

enum test_hw_type {
    IC_HW_INCELL = 1,
    IC_HW_MC_SC,
    IC_HW_SC,
};

enum test_scan_mode {
    SCAN_NORMAL = 0,
    SCAN_SC,
};

struct fts_test_node {
    int channel_num;
    int tx_num;
    int rx_num;
    int node_num;
    int key_num;
};

#define FTS_TEST_FAIL_NODE_MAX		1500

struct fts_test_fail_node {
	int	tx;	/* +0x00 */
	int	rx;	/* +0x04 */
	int	val;	/* +0x08 */
	int	min;	/* +0x0c */
	int	max;	/* +0x10 */
};

struct fts_test_fail_buf {
	int	fail_num;				/* +0x00 */
	struct fts_test_fail_node node[FTS_TEST_FAIL_NODE_MAX];	/* +0x04, sizeof=30004 */
};

/* blob 实测偏移锚点（机器码对表用，改结构必过这些断言） */
#define FTS_TEST_FAILBUF_N		40
#define FTS_TEST_OFF_IC		0xf0
#define FTS_TEST_OFF_FUNC		0x3c8
#define FTS_TEST_OFF_TESTDATA	0x3d8
#define FTS_TEST_OFF_TESTRESULT	0xbe0
#define FTS_TEST_OFF_INI		0xc0c
#define FTS_TEST_OFF_ICNAME		0x1130
#define FTS_TEST_OFF_LOCKDOWN	0x1158
#define FTS_TEST_OFF_FAILFLAG	0x12bc
#define FTS_TEST_OFF_FAILBUF	0x12c0
#define FTS_TEST_OFF_WHITE		0x127a34
#define FTS_TEST_OFF_BLACK		0x12d7f4
#define FTS_TEST_OFF_DIFFER	0x1335b4
#define FTS_TEST_SIZE		0x139378
/* _b581：A-74 续行④ —— CSV 文本缓冲尺寸（blob fts_test_init 0x1658c 立即数
 * 0x64000 = 409600；vmalloc 与 memset 同长） */
#define FTS_TEST_CSV_BUF_SIZE	0x64000

struct fts_test {
    struct fts_ts_data *ts_data;
    struct fts_test_node node;
    struct fts_test_node sc_node;
    u8 fw_ver;
    u8 va_touch_thr;
    u8 vk_touch_thr;
    bool key_support;
    bool v3_pattern;
    u8 mapping;
    u8 normalize;
    u8 fre_num;
    int test_num;
    int *item1_data;                  /* 0x40 */
    int *item2_data;                  /* 0x48 */
    int *item3_data;                  /* 0x50 */
    int *item4_data;                  /* 0x58 */
    int *item5_data;                  /* 0x60 */
    int *item6_data;                  /* 0x68 */
    int *item7_data;                  /* 0x70 */
    int *rawshift_fre_data;           /* 0x78 blob: 9*node_num ints（donor buffer 位） */
    u8 reserved_80[8];                /* 0x80..0x87 */
    int *item8_data;                  /* 0x88 第 8 个 malloc 数组（malloc/free_item_data 配对） */
    int *node_valid_sc;               /* 0x90 */
    int basic_thr_count;              /* 0x98 */
    u8 reserved_9c[4];                /* 0x9c..0x9f */
    int *node_valid;                  /* 0xa0 blob 实证（compare_* 检查面/fts_test_malloc_free_thr 分配面/ini_init_test） */
    u8 reserved_a8[0x10];             /* 0xa8..0xb7 */
    /* _b581：A-74 续行④ 归位 —— blob 的 CSV 文本缓冲在 struct fts_test+0xb8：
     *   fts_test_init 0x1658c-0x165c0: vmalloc(0x64000) -> str x0,[fts_ftest,#0xb8]
     *   -> memset(buf,0,0x64000)（失败打 "malloc csv_file_buf fail"）
     *   fts_csv_show   0x18844: ldr x0,[x1,#0xb8]（"tdata/csv_file_buf is null" 自名）
     * 原树侧放在 +0xbf0（donor 位），与该处 vmalloc/show 字段不是同一处；此处按 blob
     * 前移至 0xa8 保留洞的尾 8 字节（其余偏移不变，struct 尺寸不变）。 */
    char *csv_data_buffer;            /* 0xb8 blob 实证（0x1659c str / 0x18844 ldr） */
    int csv_item_cnt;                 /* 0xc0 */
    int csv_item_sraw;                /* 0xc4 */
    int csv_item_scb;                 /* 0xc8 */
    u32 data_valid_mask;              /* 0xcc seg3: bit0..5=6频点 bit6..8=min/max/differ */
    u32 rawshift_result_mask;         /* 0xd0 bit0=black bit1=white bit2=compared（pic 状态机） */
    int csv_item_af_noise;            /* 0xd4 */
    u8 reserved_d8[0x10];             /* 0xd8..0xe7 */
    int null_noise_value;             /* 0xe8 get_null_noise buf[0] 锚点（blob 0x2a6a8） */
    u8 reserved_ec[4];                /* 0xec..0xef */
    union {
        struct incell_test incell;
        struct mc_sc_test mc_sc;
        struct sc_test sc;
    } ic;                             /* 0xf0 */

    struct test_funcs *func;          /* 0x3c8 */
    u8 reserved_3d0[8];               /* 0x3d0 */
    struct fts_test_data testdata;    /* 0x3d8 (0x808) */
    char *testresult;                 /* 0xbe0 */
    int testresult_len;               /* 0xbe8 */
    /* _b581：0xbf0..0xbf7 = donor 的 csv 指针位；blob 该 8 字节无对应成员
     * （blob 的 csv 缓冲在 +0xb8），保留为洞以免破坏其后全部锚点偏移。 */
    u64 reserved_bf0[1];              /* 0xbf0..0xbf7 blob 无此成员（u64 保 8B 对齐） */
    int result;                       /* 0xbf8 */
    int rawshift_pic_code;            /* 0xbfc 2=PASS 3=NG */
    int rawshift_pic_black_result;    /* 0xc00 */
    int rawshift_pic_white_result;    /* 0xc04 */
    int rawshift_fre_result;          /* 0xc08 */
    u8 reserved_c0c[0x524];           /* 0xc0c..0x112f 未锚定区（blob 此段成员未取证） */
    char ic_name[MAX_IC_NAME_LEN];    /* 0x1130 save_data "IC Name, %s" 锚点 */
    u8 reserved_1150[8];              /* 0x1150 */
    u8 lockdown_info[8];              /* 0x1158 start_test lockdown 8B 锚点 */
    int rawdata_max;                  /* 0x1160 seg2 save_data RawDataRecord */
    int rawdata_min;                  /* 0x1164 */
    u8 reserved_1168[0x30];           /* 0x1168..0x1197 */
    int panel_differ_max;             /* 0x1198 */
    int panel_differ_min;             /* 0x119c */
    int *buffer;                      /* 0x11a0 donor 字段落未锚定区 */
    int buffer_length;                /* 0x11a8 */
    int code1;                        /* 0x11ac */
    int code2;                        /* 0x11b0 */
    int offset;                       /* 0x11b4 */
    int null_noise_max;               /* 0x11b8 */
    u8 reserved_11bc[0xa0];           /* 0x11bc..0x125b */
    int rawshift_fre_max[6];          /* 0x125c */
    int rawshift_fre_min[6];          /* 0x1274 */
    int rawshift_raw_max;             /* 0x128c */
    int rawshift_raw_min;             /* 0x1290 */
    int rawshift_raw_max2;            /* 0x1294 */
    int rawshift_raw_min2;            /* 0x1298 */
    int rawshift_differ_max;          /* 0x129c */
    int rawshift_differ_min;          /* 0x12a0 */
    int rawshift_black_max;           /* 0x12a4 */
    int rawshift_black_min;           /* 0x12a8 */
    int rawshift_white_max;           /* 0x12ac */
    int rawshift_white_min;           /* 0x12b0 */
    int rawshift_pic_differ_max;      /* 0x12b4 */
    int rawshift_pic_differ_min;      /* 0x12b8 */
    int item_fail_flag;               /* 0x12bc 测试项失败位掩码 */
    struct fts_test_fail_buf fail_buf[FTS_TEST_FAILBUF_N];   /* 0x12c0 每槽 30004B */
    struct ini_data ini;              /* fail_buf 尾后未锚定区（框架 ini 解析用，偏移不锚定） */
    u8 reserved_failbuf[FTS_TEST_OFF_WHITE - FTS_TEST_OFF_FAILBUF
                        - FTS_TEST_FAILBUF_N * (int)sizeof(struct fts_test_fail_buf)
                        - (int)sizeof(struct ini_data)];
    int rawshift_pic_white_data[6000];   /* 0x127a34 */
    int rawshift_pic_black_data[6000];   /* 0x12d7f4 */
    int rawshift_pic_differ_data[6000];  /* 0x1335b4 —— 末成员，+4 pad = 0x139378 */
};

_Static_assert(sizeof(struct mc_sc_threshold_b) == 0x1b0, "mc_sc_threshold_b blob size");
_Static_assert(sizeof(struct mc_sc_test) == 0x2d8, "mc_sc_test blob size");
_Static_assert(__builtin_offsetof(struct fts_test, ic) == FTS_TEST_OFF_IC, "ic@0xf0");
_Static_assert(__builtin_offsetof(struct fts_test, func) == FTS_TEST_OFF_FUNC, "func@0x3c8");
_Static_assert(__builtin_offsetof(struct fts_test, testdata) == FTS_TEST_OFF_TESTDATA, "testdata@0x3d8");
_Static_assert(__builtin_offsetof(struct fts_test, testresult) == FTS_TEST_OFF_TESTRESULT, "testresult@0xbe0");
_Static_assert(sizeof(struct fts_test_data) == 0x808, "testdata size 0x808");
_Static_assert(__builtin_offsetof(struct fts_test, ini) == FTS_TEST_OFF_FAILBUF + FTS_TEST_FAILBUF_N * (int)sizeof(struct fts_test_fail_buf), "ini@failbuf-tail");
_Static_assert(__builtin_offsetof(struct fts_test, ic_name) == FTS_TEST_OFF_ICNAME, "ic_name@0x1130");
_Static_assert(__builtin_offsetof(struct fts_test, lockdown_info) == FTS_TEST_OFF_LOCKDOWN, "lockdown@0x1158");
_Static_assert(__builtin_offsetof(struct fts_test, item_fail_flag) == FTS_TEST_OFF_FAILFLAG, "failflag@0x12bc");
_Static_assert(__builtin_offsetof(struct fts_test, fail_buf) == FTS_TEST_OFF_FAILBUF, "failbuf@0x12c0");
_Static_assert(__builtin_offsetof(struct fts_test, rawshift_pic_white_data) == FTS_TEST_OFF_WHITE, "white@0x127a34");
_Static_assert(__builtin_offsetof(struct fts_test, rawshift_pic_black_data) == FTS_TEST_OFF_BLACK, "black@0x12d7f4");
_Static_assert(__builtin_offsetof(struct fts_test, rawshift_pic_differ_data) == FTS_TEST_OFF_DIFFER, "differ@0x1335b4");
_Static_assert(sizeof(struct fts_test) == FTS_TEST_SIZE, "fts_test total 0x139378");
_Static_assert(__builtin_offsetof(struct fts_test, node_valid_sc) == 0x90, "node_valid_sc@0x90");
_Static_assert(__builtin_offsetof(struct fts_test, basic_thr_count) == 0x98, "basic_thr_count@0x98");
_Static_assert(__builtin_offsetof(struct fts_test, csv_item_cnt) == 0xc0, "csv_item_cnt@0xc0");
/* _b581：A-74 续行④ 判据——blob 的 csv 文本缓冲在 +0xb8（fts_test_init 0x1659c
 * str / fts_csv_show 0x18844 ldr），与树侧同名字段必须同址；0xbf0 只许是洞。 */
_Static_assert(__builtin_offsetof(struct fts_test, csv_data_buffer) == 0xb8, "csv_data_buffer@0xb8 (blob 0x1659c/0x18844)");
_Static_assert(__builtin_offsetof(struct fts_test, csv_item_sraw) == 0xc4, "csv_item_sraw@0xc4");
_Static_assert(__builtin_offsetof(struct fts_test, csv_item_scb) == 0xc8, "csv_item_scb@0xc8");
_Static_assert(__builtin_offsetof(struct fts_test, csv_item_af_noise) == 0xd4, "csv_item_af_noise@0xd4");
_Static_assert(__builtin_offsetof(struct fts_test, item1_data) == 0x40, "item1@0x40");
_Static_assert(__builtin_offsetof(struct fts_test, item7_data) == 0x70, "item7@0x70");
_Static_assert(__builtin_offsetof(struct fts_test, rawshift_fre_data) == 0x78, "rawshift_fre_data@0x78");
_Static_assert(__builtin_offsetof(struct fts_test, item8_data) == 0x88, "item8@0x88");
_Static_assert(__builtin_offsetof(struct fts_test, node_valid) == 0xa0, "node_valid@0xa0");
_Static_assert(__builtin_offsetof(struct fts_test, test_num) == 0x38, "test_num@0x38");
_Static_assert(__builtin_offsetof(struct fts_test, fre_num) == 0x37, "fre_num@0x37");

struct test_funcs {
    u16 ctype[FTS_MAX_COMPATIBLE_TYPE];
    enum test_hw_type hwtype;
    int startscan_mode;
    int key_num_total;
    bool rawdata2_support;
    bool force_touch;
    bool mc_sc_short_v2;
    bool raw_u16;
    bool cb_high_support;
    bool param_update_support;
    int (*param_init)(void);
    int (*init)(void);
    int (*start_test)(void);

    int (*open_test)(void);
    int (*short_test)(void);
    int (*spi_test)(void);
    int (*data_dump)(int *var, int *param);   /* blob 无 rst_test 槽（12 槽 0x80 精确闭环） */

    void (*save_data_private)(char *buf, int *len);

    /* FT5672 扩展（blob test_func_ft5672 +0x60..0x78 实测） */
    int (*rawshift_fre_test)(void);
    int (*rawshift_pic_black_test)(void);
    int (*rawshift_pic_white_test)(void);
    void (*free_item_data)(struct fts_test *tdata);
};

_Static_assert(sizeof(struct test_funcs) == 0x80, "test_funcs 128B");
_Static_assert(__builtin_offsetof(struct test_funcs, rawshift_fre_test) == 0x60, "slot 0x60");
_Static_assert(__builtin_offsetof(struct test_funcs, free_item_data) == 0x78, "slot 0x78");
/*-----------------fts_test struct end-------------------*/
#define TEST_ITEM_INCELL            { \
    "SHORT_CIRCUIT_TEST", \
    "OPEN_TEST", \
    "CB_TEST", \
    "RAWDATA_TEST", \
    "LCD_NOISE_TEST", \
    "KEY_SHORT_TEST", \
    "MUX_OPEN_TEST", \
}

#define BASIC_THRESHOLD_INCELL      { \
    "ShortCircuit_ResMin", "ShortCircuit_VkResMin", \
    "OpenTest_CBMin", "OpenTest_Check_K1", "OpenTest_K1Threshold", "OpenTest_Check_K2", "OpenTest_K2Threshold", \
    "CBTest_Min", "CBTest_Max", \
    "CBTest_VKey_Check", "CBTest_Min_Vkey", "CBTest_Max_Vkey", \
    "RawDataTest_Min", "RawDataTest_Max", \
    "RawDataTest_VKey_Check", "RawDataTest_Min_VKey", "RawDataTest_Max_VKey", \
    "LCD_NoiseTest_Frame", "LCD_NoiseTest_Coefficient", "LCD_NoiseTest_Coefficient_key", \
    "OpenTest_DifferMin", "OpenTest_DifferMax", \
}


#define TEST_ITEM_MC_SC             { \
    "RAWDATA_TEST", \
    "UNIFORMITY_TEST", \
    "SCAP_CB_TEST", \
    "SCAP_RAWDATA_TEST", \
    "WEAK_SHORT_CIRCUIT_TEST", \
    "PANEL_DIFFER_TEST", \
    "NOISE_TEST", \
    "SPI_TEST", \
    "AUXILIARY_FRE_NOISE_TEST", \
    "RESET_PIN_TEST", \
}

#define BASIC_THRESHOLD_MC_SC       { \
    "RawDataTest_High_Min", "RawDataTest_High_Max", "RawDataTest_HighFreq", \
    "RawDataTest_Low_Min", "RawDataTest_Low_Max", "RawDataTest_LowFreq", \
    "UniformityTest_Check_Tx", "UniformityTest_Check_Rx", "UniformityTest_Check_MinMax", \
    "UniformityTest_Tx_Hole", "UniformityTest_Rx_Hole", "UniformityTest_MinMax_Hole", \
    "SCapCbTest_OFF_Min", "SCapCbTest_OFF_Max", "ScapCBTest_SetWaterproof_OFF", \
    "SCapCbTest_ON_Min", "SCapCbTest_ON_Max", "ScapCBTest_SetWaterproof_ON", \
    "SCapRawDataTest_OFF_Min", "SCapRawDataTest_OFF_Max", "SCapRawDataTest_SetWaterproof_OFF", \
    "SCapRawDataTest_ON_Min", "SCapRawDataTest_ON_Max", "SCapRawDataTest_SetWaterproof_ON", \
    "WeakShortTest_CG", "WeakShortTest_CC", \
    "PanelDifferTest_Min", "PanelDifferTest_Max", \
    "SCapCbTest_High_Min", "SCapCbTest_High_Max", "ScapCBTest_SetHighSensitivity", \
    "SCapRawDataTest_High_Min", "SCapRawDataTest_High_Max", "SCapRawDataTest_SetHighSensitivity", \
    "SCapCbTest_Hov_Min", "SCapCbTest_Hov_Max", "ScapCBTest_SetHov", \
    "SCapRawDataTest_Hov_Min", "SCapRawDataTest_Hov_Max", "SCapRawDataTest_SetHov", \
    "NoiseTest_Max", "NoiseTest_Frames", "NoiseTest_FwNoiseMode", "Polling_Frequency", \
}

#define TEST_ITEM_SC                { \
    "RAWDATA_TEST", \
    "CB_TEST", \
    "DELTA_CB_TEST", \
    "WEAK_SHORT_TEST", \
}

#define BASIC_THRESHOLD_SC          { \
    "RawDataTest_Min", "RawDataTest_Max", \
    "CbTest_Min", "CbTest_Max", \
    "DeltaCbTest_Base", "DeltaCbTest_Differ_Max", \
    "DeltaCbTest_Include_Key_Test", "DeltaCbTest_Key_Differ_Max", \
    "DeltaCbTest_Deviation_S1", "DeltaCbTest_Deviation_S2", "DeltaCbTest_Deviation_S3", \
    "DeltaCbTest_Deviation_S4", "DeltaCbTest_Deviation_S5", "DeltaCbTest_Deviation_S6", \
    "DeltaCbTest_Set_Critical", "DeltaCbTest_Critical_S1", "DeltaCbTest_Critical_S2", \
    "DeltaCbTest_Critical_S3", "DeltaCbTest_Critical_S4", \
    "DeltaCbTest_Critical_S5", "DeltaCbTest_Critical_S6", \
}

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
int fts_test_get_testparam_from_ini(char *config_name);
int get_keyword_value(char *section, char *name, int *value);

#define get_value_interface(name, value) \
    get_keyword_value("Interface", name, value)
#define get_value_basic(name, value) \
    get_keyword_value("Basic_Threshold", name, value)
#define get_value_detail(name, value) \
    get_keyword_value("SpecialSet", name, value)
#define get_value_testitem(name, value) \
    get_keyword_value("TestItem", name, value)
#endif /* _INI_H */
