#ifndef __XIAOMI_TOUCH_H__
#define __XIAOMI_TOUCH_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <asm/atomic.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/string.h>

#if defined(CONFIG_DRM)
#if defined(TOUCH_PLATFORM_XRING)
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <drm/drm_panel.h>
#include <soc/xring/xr_timestamp.h>
#include <soc/xring/sensorhub/ipc/shub_ipc_drv.h>
#include <soc/xring/sensorhub/shub_boot_prepare.h>
#include <soc/xring/sensorhub/shub_notifier.h>
#include <dt-bindings/xring/platform-specific/ipc_resource.h>
#include <dt-bindings/xring/platform-specific/ipc_tags_sh_ap.h>
#else
#include <drm/drm_panel.h>
#include "mi_disp_notifier.h"
#endif
#endif

#include "xiaomi_touch_type_common.h"

/* _b581-XT③：blob 串表内版本号 = "2025.07.24-01"（.rodata.str1.1，被
 * xiaomi_touch_probe / proc_tp_write 等引用），树侧原为 donor 串 "2024.08.30-01"。 */
#define XIAOMI_TOUCH_VERSION    "2025.07.24-01"

#define BTN_INFO 0x152

#define LOG_TAG						"[MI_TP"
#define COMMON_DATA_BUF_SIZE    10
#define MAX_TOUCH_PANEL_COUNT   2
#define MAX_TOUCH_ID 10
#define HAL_VERSION_LENGTH  128
#define LIMIT_CSV_VERSION_LENGTH 30

#define GESTURE_LONGPRESS_EVENT 0x01
#define GESTURE_SINGLETAP_EVENT 0x02
#define GESTURE_DOUBLETAP_EVENT 0x04

#define TEMPERATURE_CHAGNE_VALUE 2

#ifdef TOUCH_STYLUS_SUPPORT
#define GESTURE_STYLUS_SINGLETAP_EVENT 0x08
#define GESTURE_PAD_SINGLETAP_EVENT 0x10
#endif
/*
 * DFS TEST NODE
 */
//#define DFS_DEBUG_TEST 1

/*
 * If the temperature >= 100 degrees or <= -100 degrees,
 *   it is considered invalid temperature.
 */
#define INVAILD_TEMPERATURE			1000

#define IS_TOUCH_ID_INVALID(touch_id) (touch_id < 0 || touch_id >= MAX_TOUCH_PANEL_COUNT)

enum touch_doze_analysis {
	POWER_RESET = 0,
	RELOAD_FW,
	ENABLE_IRQ,
	DISABLE_IRQ,
	REGISTER_IRQ,
	IRQ_PIN_LEVEL,
	ENTER_SUSPEND,
	ENTER_RESUME,
	POWER_ON,
	POWER_OFF,
	SPI_GET_SYNC,
	SPI_PUT_SYNC,
};

enum MI_TP_LOG_LEVEL {
	MI_TP_LOG_ALWAYS = 0,
	MI_TP_LOG_ERROR,
	MI_TP_LOG_WARNING,
	MI_TP_LOG_INFO,
	MI_TP_LOG_DEBUG,
	MI_TP_LOG_VERBOSE,
};

enum charge_status {
	NOT_CHARGING = 0,
	CHARGING = 1,
	WIRED_CHARGING = 2,
	WIRELESS_CHARGING = 4,
};

typedef enum {
	ST_PRI = 0x0010,
	ST_SEC = 0x0011,
	FOCAL_PRI = 0x0020,
	FOCAL_SEC = 0x0021,
	GOODIX_PRI = 0x0030,
	GOODIX_SEC = 0x0031,
	SYNA_PRI = 0x0040,
	SYNA_SEC = 0x0041,
	NVT_PRI = 0x0050,
	HIMAX_PRI = 0x0060,
} ic_product_code;

#if defined(TOUCH_PLATFORM_XRING)
typedef enum {
	SYNA_IPC_RESP_OK = 0,
	SYNA_IPC_RESP_ERROR,
	SYNA_IPC_DO_RESUME,
	SYNA_IPC_DO_SUSPEND,
	SYNA_IPC_SWITCH_MODE_START,
	SYNA_IPC_SWITCH_MODE_FINISH,
	SYNA_IPC_REPORT_TAP_EVENT,
	SYNA_IPC_REPORT_HOLD_EVENT,
	SYNA_IPC_SOS = 44,
	SYNA_IPC_DEBUG = 66,
	SYNA_IPC_MAX_NUM,
} syna_shub_ipc_cmd_type_t;
#endif

extern enum MI_TP_LOG_LEVEL current_log_level;

#define LOG_ALWAYS(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_ALWAYS) {\
				pr_info(LOG_TAG "_A][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_ERROR(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_ERROR) {\
				pr_info(LOG_TAG "_E][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_WARNING(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_WARNING) {\
				pr_info(LOG_TAG "_W][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_INFO(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_INFO) {\
				pr_info(LOG_TAG "_I][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_DEBUG(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_DEBUG) {\
				pr_info(LOG_TAG "_D][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)

#define LOG_VERBOSE(fmt, args...)\
		do {\
			if (current_log_level >= MI_TP_LOG_VERBOSE) {\
				pr_info(LOG_TAG "_V][%s:%d]: " fmt, __func__, __LINE__, ##args);\
			}\
		} while (0)


#if defined(TOUCH_PLATFORM_XRING)
#define xiaomi_touch_get_ktime xr_timestamp_gettime
#else
#define xiaomi_touch_get_ktime ktime_get
#endif

#define XIAOMI_TOUCH_UTC_PRINT(tag) \
	do { \
		struct timespec64 ts; \
		struct tm tm; \
		ktime_t time_ns = xiaomi_touch_get_ktime(); \
		ktime_get_real_ts64(&ts); \
		time64_to_tm(ts.tv_sec, 0, &tm); \
		LOG_INFO("%s[ktime:utc] [%llu.%llu : %ld-%02d-%02d %02d:%02d:%02d.%06lu]\n", \
			tag, time_ns / 1000000000, (time_ns % 1000000000) / 1000, \
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, \
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000); \
	} while(0)

enum poll_notify_type {
	COMMON_DATA_NOTIFY = 0,
	FRAME_DATA_NOTIFY,
	RAW_DATA_NOTIFY,
};

typedef struct private_data {
	struct list_head node;
	u8 mmap_area;
	s8 touch_id;
	wait_queue_head_t poll_wait_queue_head;
	/* _b583-XT：blob private_data_t = 0x40B —— xiaomi_touch_dev_open 0x71c8 实参
	 * `kzalloc_retry(#0x40, 3)`（64 = node16+mmap_area1+touch_id1+pad6+waitqueue24
	 * +3×atomic12 → 0x40），且全区只有 1 个 wait queue：
	 *   · dev_open 0x7204/0x720c：`strb w10(-1),[priv+0x11]`（touch_id=-1）后单次
	 *     __init_waitqueue_head(priv+0x18, "&client_private_data->poll_wait_queue_head")；
	 *     blob 串表全量复核：**无** poll_wait_queue_head_for_{cmd,frame,raw}（.strings 仅 1 条
	 *     `&client_private_data->poll_wait_queue_head`）。
	 *   · notify_xiaomi_touch 0x59c：列表遍历内单次 `__wake_up(priv+0x18, 3, 0, NULL)`
	 *     （type 形参未用）。
	 *   · xiaomi_touch_dev_poll 0x69dc：单点 poll_wait(&priv+0x18)——0x69e4 起即标准
	 *     poll_wait 宏序列 `if (p && p->_qproc && addr) p->_qproc(...)`，无 _key 分支。
	 * ⇒ 树侧 _for_cmd/_for_frame/_for_raw 三个队列（3×0x18 = 0x48B）为 donor 附加，删除。 */
	atomic_t common_data_index;
	atomic_t frame_data_index;
	atomic_t raw_data_index;
} private_data_t;

/* _b583-XT2：xiaomi_touch_data_t 逐成员按 blob 归位（步长 0x2A78，见文末断言块）。
 * 归位证据链（双证 = .disr 机器码 + IDA asm）：见各成员行内锚点。 */
typedef struct xiaomi_touch_data {
	s8 touch_id;						/* 0x0000 */
	atomic_t frame_data_buf_index;				/* 0x0004 dev_poll[0x6a18 段前] */
	u32 frame_data_size;					/* 0x0008 register_tpc[x28+0x18]，x28=元素-0x10 */
	u8 frame_data_buf_size;					/* 0x000c 同上 [x28+0x1c] */
	void *frame_data_mmap_base;				/* 0x0010 unregister_tpc[x26+0x20]，x26=元素-0x10 */
	dma_addr_t frame_data_mmap_phy_base;			/* 0x0018 dev_mmap area1 [xd+0x18] */
	atomic_t raw_data_buf_index;				/* 0x0020 dev_poll [x19+0x20] */
	u32 raw_data_size;					/* 0x0024 register_tpc [x26+0x34] */
	u8 raw_data_buf_size;					/* 0x0028 register_tpc [x21+0x10] */
	void *raw_data_mmap_base;				/* 0x0030 unregister_tpc [x26+0x40] */
	dma_addr_t raw_data_mmap_phy_base;			/* 0x0038 dev_mmap area2 [xd+0x38] */
	/* blob 独有 8B：全模块无任何读写点（IDA + .disr 对 0x40/0x44 的检索仅命中
	 * 偏置基址 x26+0x40=元素+0x30）；语义未定 ⇒ 保留空洞，禁止访问。 */
	u8 reserved_0x40[8];					/* 0x0040 */
	struct list_head private_data_list;			/* 0x0048 notify[0x578 ldr x21,[x19,#0x48]!] */
	spinlock_t private_data_lock;				/* 0x0058 notify[0x568 add x20,x0,#0x58] */
	atomic_t common_data_buf_index;				/* 0x005c dev_poll[0x6a18 ldr w9,[x19,#0x5c]] */
	common_data_t common_data_buf[COMMON_DATA_BUF_SIZE];	/* 0x0060 */
	struct mutex common_data_buf_lock;			/* 0x28b0 */
	struct htc_ic_polldata* poll_data;			/* 0x28e0 元素-0x10 基址 umaddl 命中 20 处 */
	struct workqueue_struct *event_wq;			/* 0x28e8 */
#if defined(CONFIG_DRM)
	bool is_suspend;					/* 0x28f0 */
	struct work_struct suspend_work;			/* 0x28f8 */
	struct work_struct resume_work;				/* 0x2928 */
	struct delayed_work panel_notifier_register_work;	/* 0x2958 notifier_work[work-0x2958=元素基址] */
	struct device *dev;					/* 0x29e0 rpc[0x20e0 str x0,[x20,#0x29f0]]，x20=元素-0x10 */
	struct notifier_block disp_nb;	/* blob：nb 内嵌（work+0x90 形态，notifier_work[0x2378 str x8,[x19,#0x90]!]）*/
#endif
	/* blob 0x2A00 / 0x2A04（各 4B）：xiaomi_register_panel_notifier_common 的第 3/4 实参
	 * 落点（rpc[0x20ec/0x20f8 str w2/w3,[x20,#0x2a10/0x2a14]]，x20=元素-0x10）；
	 * blob 两 IC ko 调用点均传 0（focaltech 0x3b00-0x3b08、goodix 0xeeb0-0xeebc 的 w2=w3=0）
	 * ⇒ 语义未定，保留空洞。注：树侧 donor 成员 `panel_register_retry`（原占 0x29F8）已删，
	 * blob 0x29F8 实为 disp_nb.priority（见 register_touch_panel_common 锚点）。 */
	u32 reserved_2a00;					/* 0x2A00 */
	u32 reserved_2a04;					/* 0x2A04 */
	wait_queue_head_t temp_detect_wait_queue;		/* 0x2A08 rpc/notifier_common [x28/x20,#0x2a08] 4B 存 */
	atomic_t temp_detect_ready;				/* 0x2A20 单槽（三处访问全 0x2A20；无 [1]） */
	/* blob 0x2A24-0x2A78 = boost 子状态（与元素同体！证据：get_xiaomi_touch_data 0x3f8 同时
	 * 服务 core/boost 两侧——boost_recon 的"boost 槽访问器"实参/串全同；元素步长 0x2A78 ==
	 * boost 槽步长 0x2A78；ready 单槽 0x2A20 后紧接 enable@0x2A24）。视图结构见 xiaomi_touch_boost.c，
	 * 该处有 sizeof==0x54 与 offsetof==0x2A24 断言。 */
	u8 boost_state[0x54];					/* 0x2A24..0x2A78 */
} xiaomi_touch_data_t;						/* sizeof == 0x2A78 */

typedef struct hardware_operation {
	int (*ic_self_test)(char *type, int *result);
	int (*ic_data_collect)(char *buf, int *length);
	int (*ic_get_lockdown_info)(u8 lockdown_info[8]);
	int (*ic_get_fw_version)(char fw_version[64]);			/* 0x18 */

	void (*set_mode_value)(int mode, int *value);
	void (*get_mode_value)(common_data_t *common_data);
	void (*get_mode_all)(common_data_t *common_data);		/* 0x30 */
	void (*reset_mode)(common_data_t *common_data);			/* 0x38 */
	void (*ic_switch_mode)(u8 gesture_type);			/* 0x40 */
	void (*cmd_update_func)(long mode_update_flag, int mode_value[DATA_MODE_45]); /* 0x48 */
	void (*ic_enable_irq)(bool enable);				/* 0x50 */

	int (*palm_sensor_write)(int on);				/* 0x58 */
	int (*enable_touch_raw)(int en);				/* 0x60 */
	u8 (*panel_vendor_read)(void);					/* 0x68 */
	u8 (*panel_color_read)(void);					/* 0x70 */
	u8 (*panel_display_read)(void);					/* 0x78 */
	char (*touch_vendor_read)(void);				/* 0x80 */
	void (*get_touch_ic_buffer)(u64 address, u8 *buf);		/* 0x88 */
	int (*touch_doze_analysis)(int value);				/* 0x90 */
	int (*touch_log_level_control)(bool value);			/* 0x98 */
	int (*htc_ic_setModeValue)(common_data_t *common_data);		/* 0xa0 */
	int (*htc_ic_getModeValue)(common_data_t *common_data);		/* 0xa8 */
	int (*ic_resume_suspend)(bool is_resume, u8 gesture_type);	/* 0xb0 */
	/* ==== 以下 16 项 = 6.18 在场扩展（blob 6.6 框架 ops 到 0xB8 即止：0x178+0xB8 = 0x230
	 * == driver_param 步长实测；IC 表 0xB8-0xD0 的 4 槽与 6.18 独有 12 项统一集中尾部）==== */
	void (*ic_set_charge_state)(int status);			/* +0xb8（IC 表槽位）*/
	void (*touch_dfs_test)(int value);				/* +0xc0 */
	void (*xiaomi_touch_fod_test)(int value);			/* +0xc8 */
	int (*set_thermal_temp)(int temp, bool force);   		/* +0xd0（blob 2 参实证）*/
	int (*get_limit_csv_version)(char limit_version[30]);		/* 6.18 独有 */
	void (*set_mode_long_value)(s32 value[], int length);
	int (*touch_log_level_control_v2)(int value);
	bool (*get_tddi_status)(void);
	void (*set_nfc_to_touch_event)(u8 val);
	void (*display_suspend_ready)(void);
	void (*ic_set_fod_value)(s32 value[], int length);
	void (*xiaomi_touch_fod_attn_test)(int value);
	void (*xiaomi_touch_fod_low_attn)(int value);
	void (*set_panel_notifier_status)(enum suspend_state panel_status[]);
	void (*notify_sensorhub_status)(int notify_type);
	/* popsicle ops 与 blob ops 不同代，按命名成员追加（§B 记账） */
	void (*scp_mistouch_enable)(int *value);
} hardware_operation_t;

typedef struct xiaomi_touch_driver_param {
	s8 touch_id;						/* 0x000 */
	char hal_version[HAL_VERSION_LENGTH];			/* 0x001 */
	char limit_csv_version[LIMIT_CSV_VERSION_LENGTH];	/* 0x081 */
	hardware_param_t hardware_param;			/* 0x0a0（逐成员与 blob 一致，
								 * 见文末断言：lockdown@0xd、fw_version@0x95）*/
	/* blob 步长实测 = 0x230：get_xiaomi_touch_driver_param umaddl(#0x230)+#0x5500
	 * → &param[1] = .bss+0x5500+0x230，闭包 = probe 的 charging_status 存点 0x5960
	 * （=0x5500+2*0x230）⇒ hardware_operation 起址 0x178、blob ops 大小 0xB8（23 槽）。
	 * 树侧尾部 16 项使 sizeof=0x2B0（blob+0x80），blob 同形前缀 = 前 0x230B。 */
	hardware_operation_t hardware_operation;		/* 0x178（前 0xB8 与 blob 同槽）*/
} xiaomi_touch_driver_param_t;

typedef struct xiaomi_touch {
	/* _b583-XT：blob xiaomi_touch_t 首成员（结构基址 +0x0）——blob 证据三重：
	 *   ① xiaomi_touch_probe 0x4abc/0x4b00/0x4b04：`add x20,x0,#0x10`(=&pdev->dev)->
	 *      `cbz x20`->`str x20,[x19]`(x19=.bss+0x8=&xiaomi_touch)；
	 *   ② xiaomi_register_panel_notifier_work 0x21fc/0x2204：`ldr x8,[&xiaomi_touch]`
	 *      后 `ldr x0,[x8,#0x300]`(=dev->of_node) 传给 of_count_phandle_with_args；
	 *   ③ 成员偏移自洽：blob panel_register_mask 在 .bss+0x12(=结构 +0xA)、
	 *      xiaomi_touch_data[] 在结构 +0x10（get_xiaomi_touch_data umaddl 基址 .bss+0x8
	 *      +0x10），⇒ 首 8B 必为指针、use_count@+0x8。DTS 的 panel 属性只挂
	 *      xiaomi-touch 节点（xiaomi_rodin_mt6899_touch.dtsi:12），IC 节点无该属性
	 *      ⇒ 面板 phandle 必须从本字段解析（树侧原用 per-panel dev 会永远解析失败）。 */
	struct device *dev;
	u16 use_count;
	u8 panel_register_mask;
	xiaomi_touch_data_t xiaomi_touch_data[MAX_TOUCH_PANEL_COUNT];
	xiaomi_touch_driver_param_t xiaomi_touch_driver_param[MAX_TOUCH_PANEL_COUNT];
	int charging_status;
	struct notifier_block power_supply_notifier;
	struct work_struct power_supply_work;
	/* memory for input event time line */
	void *input_event_time_line_mmap_base;
	dma_addr_t input_event_time_line_phy_base;
} xiaomi_touch_t;

/* ===================== _b583-XT2 结构归位断言（blob 双证口径）=====================
 * 口径：① = .disr 机器码（objdump 重定位视图）② = IDA asm 符号视图；
 *       偏移 = blob 元素/参数结构相对基址的立即数（偏置基 x20/x26/x28 = 元素-0x10）。
 * 元素侧：#define XTD(f) offsetof(struct xiaomi_touch_data, f)
 * 前 0x38B 与树原形态完全一致（frame/raw 两段），0x40 起 blob 整体 +8B（8B 空洞=0x40），
 * 尾部 0x54B 为 boost 子状态；总步长 0x2A78（get_xiaomi_touch_data umaddl #0x2a78、
 * &param[0]=.bss+0x18、&param[1]=.bss+0x2a90；闭合：driver_param 起址 .bss+0x5500
 * = 0x10+2*0x2A78）。 */
#define __XT_OFF(T, f)	offsetof(T, f)
static_assert(sizeof(xiaomi_touch_data_t) == 0x2A78, "xiaomi_touch_data_t 必须 = blob 0x2A78");
static_assert(__XT_OFF(xiaomi_touch_data_t, touch_id) == 0x0000, "touch_id @0");
static_assert(__XT_OFF(xiaomi_touch_data_t, frame_data_buf_index) == 0x0004, "frame idx @4");
static_assert(__XT_OFF(xiaomi_touch_data_t, frame_data_size) == 0x0008, "frame size @8");
static_assert(__XT_OFF(xiaomi_touch_data_t, frame_data_buf_size) == 0x000C, "frame bufsz @c");
static_assert(__XT_OFF(xiaomi_touch_data_t, frame_data_mmap_base) == 0x0010, "frame base @10");
static_assert(__XT_OFF(xiaomi_touch_data_t, frame_data_mmap_phy_base) == 0x0018, "frame phy @18");
static_assert(__XT_OFF(xiaomi_touch_data_t, raw_data_buf_index) == 0x0020, "raw idx @20");
static_assert(__XT_OFF(xiaomi_touch_data_t, raw_data_size) == 0x0024, "raw size @24");
static_assert(__XT_OFF(xiaomi_touch_data_t, raw_data_buf_size) == 0x0028, "raw bufsz @28");
static_assert(__XT_OFF(xiaomi_touch_data_t, raw_data_mmap_base) == 0x0030, "raw base @30");
static_assert(__XT_OFF(xiaomi_touch_data_t, raw_data_mmap_phy_base) == 0x0038, "raw phy @38");
static_assert(__XT_OFF(xiaomi_touch_data_t, reserved_0x40) == 0x0040, "blob-only 8B 空洞 @40");
static_assert(__XT_OFF(xiaomi_touch_data_t, private_data_list) == 0x0048, "list @48");
static_assert(__XT_OFF(xiaomi_touch_data_t, private_data_lock) == 0x0058, "lock @58");
static_assert(__XT_OFF(xiaomi_touch_data_t, common_data_buf_index) == 0x005C, "common idx @5c");
static_assert(__XT_OFF(xiaomi_touch_data_t, common_data_buf) == 0x0060, "common buf @60");
static_assert(__XT_OFF(xiaomi_touch_data_t, common_data_buf_lock) == 0x28B0, "common lock @28b0");
static_assert(__XT_OFF(xiaomi_touch_data_t, poll_data) == 0x28E0, "poll_data @28e0");
static_assert(__XT_OFF(xiaomi_touch_data_t, event_wq) == 0x28E8, "event_wq @28e8");
static_assert(__XT_OFF(xiaomi_touch_data_t, is_suspend) == 0x28F0, "is_suspend @28f0");
static_assert(__XT_OFF(xiaomi_touch_data_t, suspend_work) == 0x28F8, "suspend_work @28f8");
static_assert(__XT_OFF(xiaomi_touch_data_t, resume_work) == 0x2928, "resume_work @2928");
static_assert(__XT_OFF(xiaomi_touch_data_t, panel_notifier_register_work) == 0x2958,
	      "panel work @2958（container_of 步长 0x2958 实证）");
static_assert(__XT_OFF(xiaomi_touch_data_t, dev) == 0x29E0, "dev @29e0（work+0x88）");
static_assert(__XT_OFF(xiaomi_touch_data_t, disp_nb) == 0x29E8, "disp_nb @29e8（work+0x90）");
static_assert(__XT_OFF(xiaomi_touch_data_t, reserved_2a00) == 0x2A00, "rpc 第3实参落点 @2a00");
static_assert(__XT_OFF(xiaomi_touch_data_t, reserved_2a04) == 0x2A04, "rpc 第4实参落点 @2a04");
static_assert(__XT_OFF(xiaomi_touch_data_t, temp_detect_wait_queue) == 0x2A08, "temp wq @2a08");
static_assert(__XT_OFF(xiaomi_touch_data_t, temp_detect_ready) == 0x2A20, "ready 单槽 @2a20");
static_assert(__XT_OFF(xiaomi_touch_data_t, boost_state) == 0x2A24, "boost 子状态 @2a24");
static_assert(sizeof(((xiaomi_touch_data_t *)0)->boost_state) == 0x54, "boost 子状态 0x54B");
/* 参数结构侧：blob 步长 0x230（同形前缀）；树侧尾部 16 项扩展至 0x2B0 */
static_assert(__XT_OFF(xiaomi_touch_driver_param_t, touch_id) == 0x000, "param touch_id @0");
static_assert(__XT_OFF(xiaomi_touch_driver_param_t, hal_version) == 0x001, "hal_version @1");
static_assert(__XT_OFF(xiaomi_touch_driver_param_t, limit_csv_version) == 0x081, "limit_csv @81");
static_assert(__XT_OFF(xiaomi_touch_driver_param_t, hardware_param) == 0x0A0, "hw param @a0");
static_assert(__XT_OFF(xiaomi_touch_driver_param_t, hardware_operation) == 0x178, "ops @178");
static_assert(__XT_OFF(hardware_operation_t, ic_self_test) == 0x00, "ops slot0");
static_assert(__XT_OFF(hardware_operation_t, set_mode_value) == 0x20, "ops slot4");
static_assert(__XT_OFF(hardware_operation_t, cmd_update_func) == 0x48, "ops slot9");
static_assert(__XT_OFF(hardware_operation_t, ic_enable_irq) == 0x50, "ops slot10");
static_assert(__XT_OFF(hardware_operation_t, htc_ic_setModeValue) == 0xA0, "ops slot20");
static_assert(__XT_OFF(hardware_operation_t, ic_resume_suspend) == 0xB0, "ops slot22（blob 末槽）");
static_assert(__XT_OFF(hardware_operation_t, ic_set_charge_state) == 0xB8,
	      "blob 框架 ops 到 0xB8 止（0x178+0xB8 = 0x230 = blob 步长）；其下为 6.18 尾部");
static_assert(sizeof(hardware_operation_t) == 0x138, "ops = 23 blob 槽 + 16 扩展项");
static_assert(sizeof(xiaomi_touch_driver_param_t) == 0x2B0,
	      "树侧 0x2B0 = blob 0x230 + 0x80（6.18 尾部 16 项；同形前缀 = 0x230）");
#undef __XT_OFF
/* ============================== 断言块结束 ============================== */

#pragma pack(1)
typedef struct htc_ic_polldata {
	uint16_t protocol_version;
	uint16_t frame_len;
	uint16_t ic_fw_v;
	uint16_t ic_name;
	uint16_t ic_project_name;
	uint16_t ic_supplier_name;
	uint8_t pitch_size_y;
	uint8_t pitch_size_x;
	uint8_t numCol;
	uint8_t numRow;
	uint16_t packaging_factory;
	uint16_t wafe_factory;
	uint16_t x_resolution;
	uint16_t y_resolution;
	uint64_t lockdown_info;
	uint8_t frame_data_type;
	uint16_t mutual_len;
	uint16_t slef1_len;
	uint16_t slef2_len;
} htc_ic_polldata_t;
#pragma pack()

/* export for other module and xiaomi_touch module */
#ifdef TOUCH_STYLUS_SUPPORT
int update_stylus_connect_status_value(int value);
#endif
#ifdef TOUCH_FOD_SUPPORT
int update_fod_press_status_common(int value);
#endif
struct class *get_xiaomi_touch_class_common(void);
int update_palm_sensor_value_common(int value);
/* _b583-XT2：update_weak_doubletap_value 声明删除（blob 确无；见 xiaomi_touch_sys.c 锚点）*/
int update_abnormal_event(u16 type, u16 code, u16 value);
void *get_raw_data_base_common(s8 touch_id);
void notify_raw_data_update_common(s8 touch_id);
void add_common_data_to_buf_common(s8 touch_id, enum common_data_cmd cmd, enum common_data_mode mode, int length, int *data);
int register_touch_panel_common(struct device *dev, s8 touch_id, hardware_param_t *hardware_param, hardware_operation_t *hardware_operation);
void unregister_touch_panel_common(s8 touch_id);
void xiaomi_register_panel_notifier_common(struct device *dev, s8 touch_id);
void xiaomi_unregister_panel_notifier_common(struct device *dev, s8 touch_id);
void schedule_resume_suspend_work_common(s8 touch_id, bool resume_work);
void driver_update_touch_mode_common(s8 touch_id, int touch_mode[DATA_MODE_45], long update_mode_mask);
u8 xiaomi_get_gesture_type_common(s8 touch_id);
int driver_get_touch_mode_common(s8 touch_id, int mode);

/* blob 导出面（b567 重建，xiaomi_touch_boost.c）：boost 六件族 + 数据访问器 */
struct device_node;
void init_touch_irq(int id, struct device_node *np);
void touch_irq_boost_switch(int irq, int enable);
void touch_irq_cpumask(int irq);
void remove_touch_irq_boost(int irq);
void touch_irq_boost(int irq);
void touch_irq_boost_release(int irq);
extern u8 scp_tp_mistouch_close;
int get_bms_temp_common(void);
/* warsaw xiaomi_touch.h:110 原样（goodix mievent error_code 实参用） */
enum param_parse_fail_type {
	ERROR_REGULATOR_INIT,
	ERROR_GPIO_REQUEST,
	ERROR_DTS_PARSE,
};
void enable_temperature_detection_func(s8 touch_id, bool is_resume);
void xiaomi_touch_mievent_report_int_common(unsigned int code, int panel_id,
	const char *fault_name, const char *vendor_name, long error_code);
void xiaomi_touch_mievent_report_str_common(unsigned int code, int panel_id,
	const char *fault_name, const char *vendor_name);

/* use in xiaomi_touch module */
#ifdef TOUCH_KNOCK_SUPPORT
int knock_node_init(void);
void knock_node_release(void);
void register_frame_count_change_listener(void *listener);
void update_knock_data(u8 *buf, int size, int frame_id);
void knock_data_notify(void);
#endif
void notify_xiaomi_touch(xiaomi_touch_data_t *xiaomi_touch_data, enum poll_notify_type type);
void update_get_ic_current_value(common_data_t *common_data);
void schedule_resume_suspend_work_common(s8 touch_id, bool resume_work);

void sendnlmsg(char message);

void *kzalloc_retry(size_t size, int retry);
void kzalloc_free(void *p);
void *kvzalloc_retry(size_t size, int retry);
void kvzalloc_free(void *p);

xiaomi_touch_data_t *get_xiaomi_touch_data(s8 touch_id);
/* b567：blob GLOBAL 164B 的 boost 槽访问器原名 get_xiaomi_touch_data，与 popsicle
 * static（blob LOCAL 语义）在 =y 单符号空间撞名，按 §84 让名 _boost 后缀。 */
struct xiaomi_touch_irq_boost *get_xiaomi_touch_boost_data(int id);
xiaomi_touch_driver_param_t *get_xiaomi_touch_driver_param(s8 touch_id);

int xiaomi_touch_create_proc(xiaomi_touch_driver_param_t *xiaomi_touch_driver_param);
int xiaomi_touch_remove_proc(s8 touch_id);

int xiaomi_touch_operation_init(xiaomi_touch_t *temp_xiaomi_touch);
int xiaomi_touch_operation_remove(void);

int xiaomi_touch_init_touch_mode(s8 touch_id, struct device *dev);
int xiaomi_touch_mode(private_data_t *client_private_data, u32 user_size, unsigned long arg);

int xiaomi_touch_sys_init(void);
int xiaomi_touch_sys_remove(void);
int xiaomi_touch_evdev_init(xiaomi_touch_t *xiaomi_touch);
void add_input_event_timeline_before_event_time_common(int type, u64 frame_count, s64 start_time, s64 end_time);
bool init_input_event_timeline(void);
void release_input_event_timeline(void);
void xiaomi_touch_evdev_remove(void);
/* _b582-INPUT：A-80① 输入设备层成对拆改——device.c 的 register/unregister_xiaomi_
 * input_dev 族、report_touch_event/get_report_point_info_phy_addr（report-point 面）
 * 在 blob（xiaomi_touch_rodin.ko）里**全无**（符号面 + 串面 + ioctl 面三重实证），
 * 且 IC 侧（focaltech/goodix）皆自建 input_dev ⇒ 整层删除，声明随之移除。
 * 注：blob 的 xiaomi_touch_dev_mmap 只处理 area 1/2/3（无 "mmap report point buf mmap"
 * 串），ioctl 只处理 cmd 0..5（UPDATE_REPORT_POINT=6 无跳表项），见 _b582_input/。 */
/* blob 2 参形态（blob 0x4fbc：w0=slot/w1=state；fts 3+1 处直调 + evdev 内联各一处） */
void last_touch_events_collect_common(int slot, int state);
/* _b582-INTB：nfc_to_touch_event 声明删除（详情见 xiaomi_touch_core.c 同名注释） */
#endif
