#ifndef _GOODIX_TS_CORE_H_
#define _GOODIX_TS_CORE_H_
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/of_irq.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/time.h>
#include <linux/rtc.h>
#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#endif
#ifdef CONFIG_FB
#include <linux/notifier.h>
#include <linux/fb.h>
#endif
#include <xiaomi_touch.h>

#ifdef TOUCH_TRUSTED_SUPPORT
#include <linux/spi/spi.h>
#include "../qts/qts_core.h"
#endif // TOUCH_TRUSTED_SUPPORT

// #define GOODIX_DEBUG_SPI

#define GOODIX_DRIVER_VERSION			"gt9916-2025.8.19-01"

#define GOODIX_CORE_DRIVER_NAME			"goodix_ts"
#define GOODIX_PEN_DRIVER_NAME			"goodix_ts,pen"
#define GOODIX_MAX_TOUCH			10
#define GOODIX_PEN_MAX_PRESSURE			4096
#define GOODIX_MAX_PEN_KEY 			2
#define GOODIX_PEN_MAX_TILT			90
#define GOODIX_CFG_MAX_SIZE			4096
#define GOODIX_MAX_STR_LABLE_LEN		40
#define GOODIX_MAX_FRAMEDATA_LEN		2000

#define GOODIX_NORMAL_RESET_DELAY_MS	100
#define GOODIX_HOLD_CPU_RESET_DELAY_MS  5

#define GOODIX_RETRY_3					3
#define GOODIX_RETRY_5					5
#define GOODIX_RETRY_10					10

#define TS_DEFAULT_FIRMWARE			"goodix_firmware_warsaw.bin"
#define TS_DEFAULT_FIRMWARE_1			"goodix_firmware_1.bin"
#define TS_DEFAULT_CFG_BIN 			"goodix_cfg_group_warsaw.bin"
#define TS_DEFAULT_CFG_BIN_1 			"goodix_cfg_group_1.bin"

#ifdef CONFIG_TOUCH_FACTORY_BUILD
#define TS_DEFAULT_LIMIT_CSV 			"goodix_test_limits"
#else
#define TS_DEFAULT_LIMIT_CSV 			"goodix_test_limits_aftersale"
#endif

#define GOODIX_LOCKDOWN_SIZE		8
#define TS_LOCKDOWN_REG				0x10030

#define GOODIX_XIAOMI_TOUCHFEATURE
#define GOODIX_DEBUGFS_ENABLE
#define TOUCH_DUMP_TIC_SUPPORT
/*/#define CONFIG_TOUCH_BOOST*/

#define GOODIX_THP_FRAME_SIZE			2500
//For P12X fw report diffdata
#define GOODIX_THP_FRAME_DUMP_SIZE 4096

#define GTP_RESULT_INVALID				0
#define GTP_RESULT_FAIL					1
#define GTP_RESULT_PASS					2
#define CONFIG_TOUCHSCREEN_GOODIX_BRL_SPI
#define TIC_RAW_DATA_ADDR				0x11A34
#define TIC_BASE_DATA_ADDR				0x13618
#define GOODIX_TIC_RAW_SIZE				20 * 40 * 2

#define PANEL_ORIENTATION_DEGREE_0	0	/* normal portrait orientation */
#define PANEL_ORIENTATION_DEGREE_90	1	/* anticlockwise 90 degrees */
#define PANEL_ORIENTATION_DEGREE_180	2	/* anticlockwise 180 degrees */
#define PANEL_ORIENTATION_DEGREE_270	3	/* anticlockwise 270 degrees */

#define GAME_ARRAY_LEN    4
#define GAME_ARRAY_SIZE   3

#define SINGLE_TAP_EN    0x01
#define DOUBLE_TAP_EN    0x02
#define FOD_EN           0x04

#define TOUCH_ID			0
#define FLASH_WRITE_MAX_LEN				4096
#define FLASH_READ_MAX_LEN				4096
#define FLASH_PACKAGE_HEAD_LEN			6
#define FLASH_PACKAGE_EXTRA_LEN			8
#define FLASH_FREQ_MAX_LEN				8192

#define DEFAULT_WATER_PROOF_CFG			1 // 0, SelfDis; 1, WithSelf;
#define UPDATE_CFG_MAX_NUM				5
#define CSOT_CFG_WITH_SELF_ID			0x658BD1E7 // goodix_cfg_group-0x5C_658BD1E7-WithSelf.bin, [Default]
#define CSOT_CFG_SELF_DIS_ID			0x658CE230 // goodix_cfg_group-0x66_658CE230-SelfDis.bin

//#define FOD_ATTN_HIGH					4
//#define FOD_ATTN_LOW					5

enum CFG_SELF_STATE {
	SELF_DISABLE = 0,
	SELF_ENABLE = 1,
	SELF_UPDATE = 2,
	SELF_UNKNOWN = 3,
};

enum CORD_PROB_STA {
	CORE_MODULE_UNPROBED = 0,
	CORE_MODULE_PROB_SUCCESS = 1,
	CORE_MODULE_PROB_FAILED = -1,
	CORE_MODULE_REMOVED = -2,
};

enum GOODIX_ERR_CODE {
	GOODIX_EBUS      = (1<<0),
	GOODIX_ECHECKSUM = (1<<1),
	GOODIX_EVERSION  = (1<<2),
	GOODIX_ETIMEOUT  = (1<<3),
	GOODIX_EMEMCMP   = (1<<4),

	GOODIX_EOTHER    = (1<<7)
};

enum IC_TYPE_ID {
	IC_TYPE_NONE,
	IC_TYPE_NORMANDY,
	IC_TYPE_NANJING,
	IC_TYPE_YELLOWSTONE,
	IC_TYPE_BERLIN_A,
	IC_TYPE_BERLIN_B,
	IC_TYPE_BERLIN_D
};

enum GOODIX_IC_CONFIG_TYPE {
	CONFIG_TYPE_TEST = 0,
	CONFIG_TYPE_NORMAL = 1,
	CONFIG_TYPE_HIGHSENSE = 2,
	CONFIG_TYPE_CHARGER = 3,
	CONFIG_TYPE_CHARGER_HS = 4,
	CONFIG_TYPE_HOLSTER = 5,
	CONFIG_TYPE_HOSTER_CH = 6,
	CONFIG_TYPE_OTHER = 7,
	/* keep this at the last */
	GOODIX_MAX_CONFIG_GROUP = 8,
};

enum CHECKSUM_MODE {
	CHECKSUM_MODE_U8_LE,
	CHECKSUM_MODE_U16_LE,
};

#define MAX_SCAN_FREQ_NUM            10
#define MAX_SCAN_RATE_NUM            5
#define MAX_FREQ_NUM_STYLUS          8
#define MAX_STYLUS_SCAN_FREQ_NUM     6
#pragma pack(1)
typedef struct __attribute__((packed)) {
	uint32_t checksum;
	uint32_t address;
	uint32_t length;
} flash_head_info_t;

struct frame_head {
	uint8_t sync;
	uint16_t frame_index;
	uint16_t cur_frame_len;
	uint16_t next_frame_len;
	uint32_t data_en; /* 0- 7 for pack_en; 8 - 31 for type en */
	uint8_t touch_pack_index;
	uint8_t stylus_pack_index;
	uint8_t res;
	uint16_t checksum;
};

struct fw_log_st
{
	uint8_t fw_log_st_res1[108];
	uint8_t ReportInterval; /* 上报间隔周期 */
	uint8_t CurState;
	uint16_t MainLoop; /* 软件主循环次数，采集模块计数 */
	uint8_t fw_log_st_res2[18];
	uint16_t fw_normalize_scan_times; /* 当前归一化写flash次数 */
};

union fw_log_union
{
	uint8_t fw_log_struct[132];
	struct fw_log_st fw_log_t;
};

struct tp_raw
{
	uint8_t sync;
	uint16_t frame_index; /* SPI 读取计数 */
	uint16_t cur_frame_len;
	uint16_t next_frame_len;
	uint32_t pack_en_type_en;
	uint8_t touch_pack_index;
	uint8_t stylus_pack_index;
	uint8_t res1;
	uint16_t checksum;

	/* fw attribute */
	uint16_t res2;
	uint16_t res3;
	uint16_t scan_rate;
	uint32_t fw_status;
	uint32_t res4;
	uint32_t res5;
	uint8_t res6;
	uint8_t frame_status;
	uint16_t res7;
	union fw_log_union fw_log_union_t;
	uint16_t mc_druation;
	uint16_t mc_tx_freq;
	uint16_t mc_tx_freq_index;
	uint8_t framedata_type;
	uint8_t res8; /* debug_flag: 0-frame_data, 1-debug_data */
	int16_t *mc_raw;
	uint16_t mc_adc_state[3];

	uint16_t sc_tx_druation;
	uint16_t sc_rx_druation;
	uint16_t sc_tx_freq;
	uint16_t sc_rx_freq;
	uint16_t res9;
	int16_t *sc_raw;
	uint16_t sc_adc_state[3];

	uint16_t com_noise_data[8];
	uint16_t lcd_noise_data[8];
	uint16_t *noise_data;
	uint16_t noise_adc_state[3];

	uint16_t tail_index;
	uint32_t frame_checksum;
};

struct goodix_fw_version
{
	u8 rom_pid[6]; /* rom PID */
	u8 rom_vid[3]; /* Mask VID */
	u8 rom_vid_reserved;
	u8 patch_pid[8];              /* Patch PID */
	u8 patch_vid[4];              /* Patch VID */
	u8 patch_vid_reserved;
	u8 sensor_id;
	u8 reserved[2];
	u16 checksum;
};

struct goodix_ic_info_version {
	u8 info_customer_id;
	u8 info_version_id;
	u8 ic_die_id;
	u8 ic_version_id;
	u32 config_id;
	u8 config_version;
	u8 frame_data_customer_id;
	u8 frame_data_version_id;
	u8 touch_data_customer_id;
	u8 touch_data_version_id;
	u8 reserved[3];
};

struct goodix_ic_info_feature { /* feature info*/
	u16 freqhop_feature;
	u16 calibration_feature;
	u16 gesture_feature;
	u16 side_touch_feature;
	u16 stylus_feature;
};

struct goodix_ic_info_param { /* param */
	u8 drv_num;
	u8 sen_num;
	u8 button_num;
	u8 force_num;
	u8 active_scan_rate_num;
	u16 active_scan_rate[MAX_SCAN_RATE_NUM];
	u8 mutual_freq_num;
	u16 mutual_freq[MAX_SCAN_FREQ_NUM];
	u8 self_tx_freq_num;
	u16 self_tx_freq[MAX_SCAN_FREQ_NUM];
	u8 self_rx_freq_num;
	u16 self_rx_freq[MAX_SCAN_FREQ_NUM];
	u8 stylus_freq_num;
	u16 stylus_freq[MAX_FREQ_NUM_STYLUS];
};

struct goodix_ic_info_misc { /* other data */
	u32 cmd_addr;
	u16 cmd_max_len;
	u32 cmd_reply_addr;
	u16 cmd_reply_len;
	u32 fw_state_addr;
	u16 fw_state_len;
	u32 fw_buffer_addr;
	u16 fw_buffer_max_len;
	u32 frame_data_addr;
	u16 frame_data_head_len;
	u16 fw_attr_len;
	u16 fw_log_len;
	u8 pack_max_num;
	u8 pack_compress_version;
	u16 stylus_struct_len;
	u16 mutual_struct_len;
	u16 self_struct_len;
	u16 noise_struct_len;
	u32 touch_data_addr;
	u16 touch_data_head_len;
	u16 point_struct_len;
	u16 panel_x;
	u16 panel_y;
	u32 mutual_rawdata_addr;
	u32 mutual_diffdata_addr;
	u32 mutual_refdata_addr;
	u32 self_rawdata_addr;
	u32 self_diffdata_addr;
	u32 self_refdata_addr;
	u32 iq_rawdata_addr;
	u32 iq_refdata_addr;
	u32 im_rawdata_addr;
	u16 im_readata_len;
	u32 noise_rawdata_addr;
	u16 noise_rawdata_len;
	u32 stylus_rawdata_addr;
	u16 stylus_rawdata_len;
	u32 noise_data_addr;
	u32 esd_addr;
	u32 auto_scan_cmd_addr;
	u32 auto_scan_info_addr;
	u16 normalize_k_version;
};

struct goodix_ic_info_other {
	u32 irrigation_data_addr;
	u32 algo_debug_data_addr;
	u16 algo_debug_data_len;
	u32 update_sync_data_addr;
	u16 screen_max_x;
	u16 screen_max_y;
};

struct goodix_ic_info {
	u16 length;
	struct goodix_ic_info_version version;
	struct goodix_ic_info_feature feature;
	struct goodix_ic_info_param parm;
	struct goodix_ic_info_misc misc;
	struct goodix_ic_info_other other;
};
#pragma pack()

/*
 * struct ts_rawdata_info
 *
 */
#define TS_RAWDATA_BUFF_MAX             7000
#define TS_RAWDATA_RESULT_MAX           100
struct ts_rawdata_info {
	int used_size; /*fill in rawdata size*/
	s16 buff[TS_RAWDATA_BUFF_MAX];
	char result[TS_RAWDATA_RESULT_MAX];
};

#define FRAME_DATA_MAX_LEN	2500 /*same as GOODIX_THP_FRAME_SIZE*/
struct ts_framedata {
	unsigned char buff[FRAME_DATA_MAX_LEN];
	int used_size;
};

/*
 * struct goodix_module - external modules container
 * @head: external modules list
 * @initilized: whether this struct is initilized
 * @mutex: mutex lock
 * @wq: workqueue to do register work
 * @core_data: core_data pointer
 */
struct goodix_module {
	struct list_head head;
	bool initilized;
	struct mutex mutex;
	struct workqueue_struct *wq;
	struct goodix_ts_core *core_data;
};

/*
 * struct goodix_ts_board_data -  board data
 * @avdd_name: name of analoy regulator
 * @iovdd_name: name of analoy regulator
 * @reset_gpio: reset gpio number
 * @irq_gpio: interrupt gpio number
 * @irq_flag: irq trigger type
 * @swap_axis: whether swaw x y axis
 * @panel_max_x/y/w/p: resolution and size
 * @pannel_key_map: key map
 * @fw_name: name of the firmware image
 */
struct goodix_ts_board_data {
	char avdd_name[GOODIX_MAX_STR_LABLE_LEN];
	char iovdd_name[GOODIX_MAX_STR_LABLE_LEN];
	int reset_gpio;
	int irq_gpio;
	int avdd_gpio;
	int iovdd_gpio;
	unsigned int irq_flags;

	char panel_name[GOODIX_MAX_STR_LABLE_LEN]; // for o16u panel diff

	unsigned int swap_axis;
	unsigned int panel_max_x;
	unsigned int panel_max_y;
	unsigned int super_resolution_factor;
	unsigned int panel_max_w; /*major and minor*/
	unsigned int panel_max_p; /*pressure*/

	bool pen_enable;
	char fw[GOODIX_MAX_STR_LABLE_LEN];
	char fw_name[GOODIX_MAX_STR_LABLE_LEN];
	char cfg_bin[GOODIX_MAX_STR_LABLE_LEN];
	char cfg_bin_name[GOODIX_MAX_STR_LABLE_LEN];
	char limit_csv_name[GOODIX_MAX_STR_LABLE_LEN];
	u32 touch_expert_array[GAME_ARRAY_LEN * GAME_ARRAY_SIZE];
};

enum goodix_fw_update_mode {
	UPDATE_MODE_DEFAULT = 0,
	UPDATE_MODE_FORCE = (1<<0), /* force update mode */
	UPDATE_MODE_BLOCK = (1<<1), /* update in block mode */
	UPDATE_MODE_FLASH_CFG = (1<<2), /* reflash config */
	UPDATE_MODE_SRC_SYSFS = (1<<4), /* firmware file from sysfs */
	UPDATE_MODE_SRC_HEAD = (1<<5), /* firmware file from head file */
	UPDATE_MODE_SRC_REQUEST = (1<<6), /* request firmware */
	UPDATE_MODE_SRC_ARGS = (1<<7), /* firmware data from function args */
};

#define MAX_CMD_DATA_LEN 10
#define MAX_CMD_BUF_LEN  16
#pragma pack(1)
struct goodix_ts_cmd {
	union {
		struct {
			u8 state;
			u8 ack;
			u8 len;
			u8 cmd;
			u8 data[MAX_CMD_DATA_LEN];
		};
		u8 buf[MAX_CMD_BUF_LEN];
	};
};
#pragma pack()

/* interrupt event type */
enum ts_event_type {
	EVENT_INVALID = 0,
	EVENT_TOUCH = (1 << 0), /* finger touch event */
	EVENT_PEN = (1 << 1),   /* pen event */
	EVENT_REQUEST = (1 << 2),
	EVENT_GESTURE = (1 << 3),
	EVENT_FRAME = (1 << 4),
};

enum ts_request_type {
	REQUEST_TYPE_CONFIG = 1,
	REQUEST_TYPE_RESET = 3,
};

/* notifier event */
enum ts_notify_event {
	NOTIFY_FWUPDATE_START,
	NOTIFY_FWUPDATE_FAILED,
	NOTIFY_FWUPDATE_SUCCESS,
	NOTIFY_SUSPEND,
	NOTIFY_RESUME,
	NOTIFY_ESD_OFF,
	NOTIFY_ESD_ON,
	NOTIFY_CFG_BIN_FAILED,
	NOTIFY_CFG_BIN_SUCCESS,
};

enum touch_point_status {
	TS_NONE,
	TS_RELEASE,
	TS_TOUCH,
};
/* coordinate package */
struct goodix_ts_coords {
	int status; /* NONE, RELEASE, TOUCH */
	unsigned int x, y, w, p;
};

struct goodix_pen_coords {
	int status; /* NONE, RELEASE, TOUCH */
	int tool_type;  /* BTN_TOOL_RUBBER BTN_TOOL_PEN */
	unsigned int x, y, p;
	signed char tilt_x;
	signed char tilt_y;
};

/* touch event data */
struct goodix_touch_data {
	int touch_num;
	struct goodix_ts_coords coords[GOODIX_MAX_TOUCH];
	unsigned int overlay;
	int fod_id;
	int t_id;
};

struct goodix_ts_key {
	int status;
	int code;
};

struct goodix_pen_data {
	struct goodix_pen_coords coords;
	struct goodix_ts_key keys[GOODIX_MAX_PEN_KEY];
};

#ifdef TOUCH_THP_SUPPORT
struct tp_frame {
	long time_ns;
	unsigned long frame_cnt;
	int fod_pressed;
	int fod_trackingId;
//For P12X fw report diffdata
#ifdef TOUCH_DUMP_TIC_SUPPORT
	u8 thp_frame[GOODIX_THP_FRAME_DUMP_SIZE];
	int dump_type;
#else
	u8 thp_frame[GOODIX_THP_FRAME_SIZE];
#endif
};
#endif

/*
 * struct goodix_ts_event - touch event struct
 * @event_type: touch event type, touch data or
 *	request event
 * @event_data: event data
 */
struct goodix_ts_event {
	int retry;
	enum ts_event_type event_type;
	u8 request_code; /* represent the request type */
	u8 gesture_type;
	struct goodix_touch_data touch_data;
	struct goodix_pen_data pen_data;
};

enum goodix_ic_bus_type {
	GOODIX_BUS_TYPE_I2C,
	GOODIX_BUS_TYPE_SPI,
	GOODIX_BUS_TYPE_I3C,
};

struct goodix_bus_interface {
	int bus_type;
	int ic_type;
	struct device *dev;
	int (*read)(struct device *dev, unsigned int addr,
			 unsigned char *data, unsigned int len);
	int (*write)(struct device *dev, unsigned int addr,
			unsigned char *data, unsigned int len);
};

struct goodix_ts_hw_ops {
	int (*power_on)(struct goodix_ts_core *cd, bool on);
	int (*dev_confirm)(struct goodix_ts_core *cd);
	int (*resume)(struct goodix_ts_core *cd);
	int (*suspend)(struct goodix_ts_core *cd);
	int (*gesture)(struct goodix_ts_core *cd, int gesture_type);
	int (*reset)(struct goodix_ts_core *cd, int delay_ms);
	int (*irq_enable)(struct goodix_ts_core *cd, bool enable);
	int (*read)(struct goodix_ts_core *cd, unsigned int addr,
			unsigned char *data, unsigned int len);
	int (*write)(struct goodix_ts_core *cd, unsigned int addr,
			unsigned char *data, unsigned int len);
	int (*read_flash)(struct goodix_ts_core *cd, unsigned int addr,
			unsigned char *data, unsigned int len);
	int (*write_flash)(struct goodix_ts_core *cd, unsigned int addr,
			unsigned char *data, unsigned int len);
	int (*send_cmd)(struct goodix_ts_core *cd, struct goodix_ts_cmd *cmd);
	int (*send_config)(struct goodix_ts_core *cd, u8 *config, int len);
	int (*read_config)(struct goodix_ts_core *cd, u8 *config_data, int size);
	int (*read_version)(struct goodix_ts_core *cd, struct goodix_fw_version *version);
	int (*get_ic_info)(struct goodix_ts_core *cd, struct goodix_ic_info *ic_info);
	int (*esd_check)(struct goodix_ts_core *cd);
	int (*event_handler)(struct goodix_ts_core *cd, struct goodix_ts_event *ts_event);
	int (*after_event_handler)(struct goodix_ts_core *cd); /* clean sync flag */
	int (*get_capacitance_data)(struct goodix_ts_core *cd, struct ts_rawdata_info *info);
	int (*charger_on)(struct goodix_ts_core *cd, bool on);
	int (*palm_on)(struct goodix_ts_core *cd, bool on);
	//int (*fod_attn)(struct goodix_ts_core *cd, int value);
	int (*game)(struct goodix_ts_core *cd, u8 data0, u8 data1, bool on);
	int (*get_frame_data)(struct goodix_ts_core *cd, struct ts_framedata *info);
	int (*switch_report_rate)(struct goodix_ts_core *cd, bool on);
	int (*get_frame_data_size)(struct goodix_ts_core *cd);
};

/*
 * struct goodix_ts_esd - esd protector structure
 * @esd_work: esd delayed work
 * @esd_on: 1 - turn on esd protection, 0 - turn
 *  off esd protection
 */
struct goodix_ts_esd {
	bool irq_status;
	atomic_t esd_on;
	struct delayed_work esd_work;
	struct notifier_block esd_notifier;
	struct goodix_ts_core *ts_core;
};

enum goodix_core_init_stage {
	CORE_UNINIT,
	CORE_INIT_FAIL,
	CORE_INIT_STAGE1,
	CORE_INIT_STAGE2
};

struct goodix_ic_config {
	int len;
	u8 data[GOODIX_CFG_MAX_SIZE];
};
enum ts_work_stat {
	TP_NORMAL,
	TP_GESTURE,
	TP_SLEEP,
};
enum SYNC_MODE {
	NO_SYNC,
	AUTO_SYNC,
	SYNC,
	DIFF_AUTO_SYNC,
};

struct goodix_ts_core {
	int init_stage;
	struct platform_device *pdev;
	struct goodix_fw_version fw_version;
	struct goodix_ic_info ic_info;
	struct goodix_bus_interface *bus;
	struct goodix_ts_board_data board_data;
	struct goodix_ts_hw_ops *hw_ops;
	struct input_dev *input_dev;
	struct input_dev *pen_dev;
	struct class *goodix_tp_class;
	struct device *goodix_touch_dev;
 	/* TODO counld we remove this from core data? */
	struct goodix_ts_event ts_event;
	unsigned long touch_id;
	u8 eventsdata;

	struct work_struct self_check_work;

	/* every pointer of this array represent a kind of config */
	struct goodix_ic_config *ic_configs[GOODIX_MAX_CONFIG_GROUP];
	struct regulator *avdd;
	struct regulator *iovdd;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_sta_active;
	struct pinctrl_state *pin_sta_suspend;
	struct pinctrl_state *pin_sta_touch_mode_ap;	/* blob 0x4b0：touch_mode_ap */
	struct pinctrl_state *pin_sta_touch_mode_scp;	/* blob 0x4b8：touch_mode_scp */
	struct pinctrl_state *pin_sta_boot;

	int power_on;
	int irq;
	size_t irq_trig_cnt;
	void *notifier_cookie;

	atomic_t irq_enabled;
	atomic_t suspended;
	/* when this flag is true, driver should not clean the sync flag */
	bool tools_ctrl_sync;
	bool fod_finger;
	int fingerprint_authstate;
	bool fod_down_before_suspend;
	bool fod_display_enabled;
	bool irq_priority_high;
	bool doze_test;

	struct notifier_block ts_notifier;
	struct goodix_ts_esd ts_esd;
	bool is_inspecting;

#ifdef CONFIG_FB
	struct notifier_block fb_notifier;
#endif
	struct work_struct charger_work;
	struct pm_qos_request pm_qos_req_irq;
	u8 lockdown_info[GOODIX_LOCKDOWN_SIZE];
#ifdef GOODIX_DEBUGFS_ENABLE
	struct dentry *debugfs;
#endif
	struct mutex report_mutex;
	struct mutex core_mutex;
	int work_status;
	int gesture_enabled;
	int charger_status;
	int palm_status;
	int super_wallpaper;
	int report_rate;
	int fps_value;
	bool tp_pm_suspend;
	struct completion pm_resume_completion;
	struct notifier_block notifier;
	int sync_mode;
#ifdef TOUCH_THP_SUPPORT
	int enable_touch_raw;
	struct delayed_work thp_signal_work;
	int frame_data_size;
#ifdef TOUCH_DUMP_TIC_SUPPORT
	int dump_type;
	int debug_data_size;
#endif /* TOUCH_DUMP_TIC_SUPPORT */
#endif
	struct delayed_work panel_notifier_register_work;
	int hang_debug;
	bool cfg_cloud_state; // false, SelfDis; true, WithSelf;
	int need_update_cfg;
	bool gamemode_enable;
#ifdef TOUCH_TRUSTED_SUPPORT
	/* tui */
	bool qts_en;
	struct completion tui_finish;
	bool tui_process;
#endif // TOUCH_TRUSTED_SUPPORT
	/* diff panel tag */
	bool sec_panel_tag;
	/* low attn tag */
	//int low_attn_status; //false, normal; true, low attn forever
	/* thp fod status tag */
	int thp_fod_status;
};

/* external module structures */
enum goodix_ext_priority {
	EXTMOD_PRIO_RESERVED = 0,
	EXTMOD_PRIO_FWUPDATE,
	EXTMOD_PRIO_GESTURE,
	EXTMOD_PRIO_HOTKNOT,
	EXTMOD_PRIO_DBGTOOL,
	EXTMOD_PRIO_DEFAULT,
};

#define EVT_HANDLED				0
#define EVT_CONTINUE			0
#define EVT_CANCEL				1
#define EVT_CANCEL_IRQEVT		1
#define EVT_CANCEL_SUSPEND		1
#define EVT_CANCEL_RESUME		1
#define EVT_CANCEL_RESET		1

struct goodix_ext_module;
/* external module's operations callback */
struct goodix_ext_module_funcs {
	int (*init)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*exit)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*before_reset)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*after_reset)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*before_suspend)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*after_suspend)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*before_resume)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*after_resume)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
	int (*irq_event)(struct goodix_ts_core *core_data,
			struct goodix_ext_module *module);
};

/*
 * struct goodix_ext_module - external module struct
 * @list: list used to link into modules manager
 * @name: name of external module
 * @priority: module priority vlaue, zero is invalid
 * @funcs: operations callback
 * @priv_data: private data region
 * @kobj: kobject
 * @work: used to queue one work to do registration
 */
struct goodix_ext_module {
	struct list_head list;
	char *name;
	enum goodix_ext_priority priority;
	const struct goodix_ext_module_funcs *funcs;
	void *priv_data;
	struct kobject kobj;
	struct work_struct work;
};

/*
 * struct goodix_ext_attribute - exteranl attribute struct
 * @attr: attribute
 * @show: show interface of external attribute
 * @store: store interface of external attribute
 */
struct goodix_ext_attribute {
	struct attribute attr;
	ssize_t (*show)(struct goodix_ext_module *, char *);
	ssize_t (*store)(struct goodix_ext_module *, const char *, size_t);
};

/* external attrs helper macro */
#define __EXTMOD_ATTR(_name, _mode, _show, _store)	{	\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,	\
	.store  = _store,	\
}

/* external attrs helper macro, used to define external attrs */
#define DEFINE_EXTMOD_ATTR(_name, _mode, _show, _store)	\
static struct goodix_ext_attribute ext_attr_##_name = \
	__EXTMOD_ATTR(_name, _mode, _show, _store);

/* log macro */
enum GTP_LOG_LEVEL
{
	GTP_LOG_ALWAYS = 0,
	GTP_LOG_ERROR,
	GTP_LOG_WARNING,
	GTP_LOG_INFO,
	GTP_LOG_DEBUG,
	GTP_LOG_VERBOSE,
};

extern enum GTP_LOG_LEVEL debug_log_level;
#define TAG "[GTP"

#define ts_verbose(fmt, args...)                                         \
	do                                                                   \
	{                                                                    \
		if (debug_log_level >= GTP_LOG_VERBOSE)                          \
		{                                                                \
			pr_info(TAG "_V][%s:%d]: " fmt, __func__, __LINE__, ##args); \
		}                                                                \
	} while (0)

#define ts_debug(fmt, args...)                                           \
	do                                                                   \
	{                                                                    \
		if (debug_log_level >= GTP_LOG_DEBUG)                            \
		{                                                                \
			pr_info(TAG "_D][%s:%d]: " fmt, __func__, __LINE__, ##args); \
		}                                                                \
	} while (0)

#define ts_info(fmt, args...)                                            \
	do                                                                   \
	{                                                                    \
		if (debug_log_level >= GTP_LOG_INFO)                             \
		{                                                                \
			pr_info(TAG "_I][%s:%d]: " fmt, __func__, __LINE__, ##args); \
		}                                                                \
	} while (0)

#define ts_warn(fmt, args...)                                            \
	do                                                                   \
	{                                                                    \
		if (debug_log_level >= GTP_LOG_WARNING)                          \
		{                                                                \
			pr_info(TAG "_W][%s:%d]: " fmt, __func__, __LINE__, ##args); \
		}                                                                \
	} while (0)

#define ts_err(fmt, args...)                                             \
	do                                                                   \
	{                                                                    \
		if (debug_log_level >= GTP_LOG_ERROR)                            \
		{                                                                \
			pr_info(TAG "_E][%s:%d]: " fmt, __func__, __LINE__, ##args); \
		}                                                                \
	} while (0)

enum GOODIX_CMD_MODE
{
	GOODIX_CMD_EMPTY_INT = 0x11,
	GOODIX_GAME_CMD = 0x17,
	GOODIX_NORMAL_CMD = 0x18,
	GOODIX_TEMPCMD = 0x60,
	GOODIX_PALM_CMD = 0x70,
	GOODIX_GLOVE_CMD = 0x72,
	GOODIX_SLEEP_CMD = 0x84,
	GOODIX_CMD_FRAMEDATA = 0x90,
	GOODIX_CMD_TOUCHDATA = 0x91,
	GOODIX_CMD_FREQ_SHIFT = 0x9B,
	GOODIX_CMD_SCAN_FREQ = 0x9C,
	GOODIX_CMD_ACTIVE_SCAN_RATE = 0x9D,
	GOODIX_CMD_IDLE = 0x9F,
	GOODIX_CMD_IDLE_THRESHOLD = 0xA1,
	GOODIX_CMD_UPDATE_IDLE_BASELINE = 0xA2,
	GOODIX_GESTURE_CMD = 0xA6,
	GOODIX_GESTURE_10DIFF_CMD = 0x13,
	GOODIX_CMD_START_FREQ_SCAN = 0xA8,
	GOODIX_ESD_TICK_WRITE_DATA = 0xAA,
	GOODIX_CHARGER_CMD = 0xAF,
	GOODIX_HIGH_RATE_CMD = 0xC1,
	GOODIX_REF_HOPPING_SET = 0xD3,
};

/*
 * get board data pointer
 */
static inline struct goodix_ts_board_data *board_data(
		struct goodix_ts_core *core)
{
	if (!core)
		return NULL;
	return &(core->board_data);
}

/**
 * goodix_register_ext_module - interface for external module
 * to register into touch core modules structure
 *
 * @module: pointer to external module to be register
 * return: 0 ok, <0 failed
 */
int goodix_register_ext_module(struct goodix_ext_module *module);
/* register module no wait */
int goodix_register_ext_module_no_wait(struct goodix_ext_module *module);
/**
 * goodix_unregister_ext_module - interface for external module
 * to unregister external modules
 *
 * @module: pointer to external module
 * return: 0 ok, <0 failed
 */
int goodix_unregister_ext_module(struct goodix_ext_module *module);
/* remove all registered ext module
 * return 0 on success, otherwise return < 0
 */
int goodix_ts_blocking_notify(enum ts_notify_event evt, void *v);
struct kobj_type *goodix_get_default_ktype(void);
struct kobject *goodix_get_default_kobj(void);

struct goodix_ts_hw_ops *goodix_get_hw_ops(void);
int goodix_get_config_proc(struct goodix_ts_core *cd);

int goodix_spi_bus_init(void);
void goodix_spi_bus_exit(void);
int goodix_i2c_bus_init(void);
void goodix_i2c_bus_exit(void);

#ifdef TOUCH_TRUSTED_SUPPORT
void goodix_set_spi_device(struct spi_device *spi);
#endif // TOUCH_TRUSTED_SUPPORT

u32 goodix_append_checksum(u8 *data, int len, int mode);
int checksum_cmp(const u8 *data, int size, int mode);
int is_risk_data(const u8 *data, int size);
u32 goodix_get_file_config_id(u8 *ic_config);
void goodix_rotate_abcd2cbad(int tx, int rx, s16 *data);

int goodix_fw_update_init(struct goodix_ts_core *core_data);
void goodix_fw_update_uninit(void);
int goodix_do_fw_update(struct goodix_ic_config *ic_config, int mode);
int goodix_get_self_config_state(int file_cfg_id, int ic_cfg_id);

int goodix_gesture_ist(struct goodix_ts_core *cd);
int gsx_gesture_before_suspend(struct goodix_ts_core *cd);
int gsx_gesture_before_resume(struct goodix_ts_core *cd);

int goodix_get_ic_type(struct device_node *node);
int gesture_module_init(void);
void gesture_module_exit(void);
int inspect_module_init(struct goodix_ts_core *core_data);
void inspect_module_exit(void);
int goodix_tools_init(void);
void goodix_tools_exit(void);
int goodix_get_rawdata(struct device *dev, struct ts_rawdata_info *info);
int brl_switch_report_rate(struct goodix_ts_core *cd, bool on);
int get_limit_csv_file_version(struct goodix_ts_core *ts_core, char *limit_version);

#ifdef TOUCH_THP_SUPPORT
int goodix_htc_enable(int en);
int goodix_htc_enter_idle(int *value);
int goodix_htc_ref_hopping_set(int *value);
int goodix_htc_enter_glove(int *value);
int goodix_htc_start_calibration(void);
int goodix_htc_enable_b_array(void);
int goodix_normalize_coeffi_update(struct goodix_ts_core *cd);
#endif
int goodix_htc_update_idle_baseline(void);
int goodix_htc_set_active_scan_rate(int scan_rate);
int goodix_get_tx_num(void);
int goodix_get_rx_num(void);
int goodix_get_freq_num(void);
int goodix_get_x_resolution(void);
int goodix_get_y_resolution(void);
int goodix_htc_set_scan_freq(u8 index);
// for goodix_alg
int goodix_htc_set_freq_hopping(int index);
int goodix_htc_set_soft_reset(int index);
int goodix_htc_set_double_scan(int index);
int goodix_htc_set_normalize_study(int *index);
int goodix_htc_set_gesture_feedback(int index);
int goodix_htc_set_idle_threshold(int threshold);
int goodix_htc_set_display_fps(int value);

int goodix_htc_enable_empty_int(bool en);
int goodix_htc_enable_ic_dump(int en);


#ifdef CONFIG_TOUCH_FACTORY_BUILD
void ts_test_cmd_enable(bool en);
#endif
void goodix_ts_report_finger(struct input_dev *dev,
							 struct goodix_touch_data *touch_data);

#ifdef CONFIG_FACTORY_BUILD
void ts_test_cmd_enable(bool en);
#endif

bool goodix_get_ic_self_test_mode(void);
void goodix_ts_esd_off(struct goodix_ts_core *cd);
void goodix_ts_esd_on(struct goodix_ts_core *cd);

int goodix_do_inspect_thread(void *arg);
int goodix_get_final_result(void);

#endif

/* ==================== _b571：SCP-TP 对外面（scp_debug 节点用）==================== */
struct scp_tp_params {
	u32 param0;		/* 0x00 状态机 */
	u32 unknown_04;
	u32 field_08;
	u32 unknown_0c;
	u32 field_10;
	u32 unknown_14[5];
	u32 gesture_type;	/* 0x28 */
	u32 gesture_len;	/* 0x2c */
	u8  gesture_data[64];	/* 0x30 */
};
extern struct scp_tp_params scp_tp_param;
int scp_tp_ipi_send(u32 arg0, u32 arg1, u32 arg2, u32 arg3);
int scp_tp_sendparam(void);
int scp_tp_switch(u32 mode);
