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
    u32 auxiliary_freq_noise_test   : 1;
    u32 rst_test                    : 1;
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
    int *item1_data;
    int *item2_data;
    int *item3_data;
    int *item4_data;
    int *item5_data;
    int *item6_data;
    int *item7_data;
    int *buffer;
    int buffer_length;
    int *node_valid;
    int *node_valid_sc;
    int basic_thr_count;
    int csv_item_cnt;
    int csv_item_scb;
    int csv_item_sraw;
    int csv_item_af_noise;
    int code1;
    int code2;
    int offset;
    int null_noise_max;
    union {
        struct incell_test incell;
        struct mc_sc_test mc_sc;
        struct sc_test sc;
    } ic;

    struct test_funcs *func;
    struct fts_test_data testdata;
    char *testresult;
    int testresult_len;
    char *csv_data_buffer;
    int result;
#if defined(TEST_SAVE_FAIL_RESULT) && TEST_SAVE_FAIL_RESULT
    struct timeval tv;
#endif
    struct ini_data ini;
};

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
    bool (*rst_test)(struct fts_test *tdata, bool *test_result);
    int (*data_dump)(int *var, int *param);

    void (*save_data_private)(char *buf, int *len);
};
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
