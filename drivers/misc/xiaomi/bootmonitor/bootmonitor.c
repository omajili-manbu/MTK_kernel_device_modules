// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi boot monitor driver (rodin)
 *
 * Functional rebuild of the stock `bootmonitor.ko` (vendor_boot partition,
 * vermagic 6.6.77-android15-8-g358a69b2ec0f-4k) as an in-tree built-in unit.
 * The stock module ships without source; the behaviour below is reconstructed
 * from its machine code (symbol table + full disassembly) and cross-checked
 * against the stock 6.6 boot log (boot_monitor_init at .411s, the stage
 * ledger lines `counts = bm_boot_events[N].warntime`, `boot_completed` at
 * 20.89s, `exit_boot_monitor-47:bdev no init` on a clean boot) and the live
 * 6.18 boot log.
 *
 * The monitor follows the ten boot stages below in the bootprof ring.  When
 * every stage is reached the thread leaves a `boot-ok` state and tears the
 * monitor down without ever touching the partition.  When a stage runs past
 * its waiting budget the boot counts as failed: the state becomes
 * `error:<name>` and the bootprof log, the kmsg tail and the ramoops pmsg
 * window are dumped to the blackbox partition:
 *
 *   log table header   @0x400            magic 0x56775af41bcde0f2
 *   error type header  @0x9a00000
 *   error slot (n)     @0x9a00000+0x1000+n*0x1000+0x3c000
 *   boot monitor hdr   @0x9a00000+0x200000
 *   write buffer       @0x9c01000        0xff000 bytes
 *   oops window        @0x9d00000        0x200000 bytes
 *
 * Consumes bootprof's exported facility (bootprof_lock / get_log_count /
 * get_bootprof_pointer).  The stock blob carries its own copy of the
 * bootprof logger with those names promoted to globals; in this tree
 * drivers/misc/mediatek/mtprof/bootprof.c already exports all three, so the
 * monitor binds to the real in-tree logger instead of duplicating it.
 *
 * Quirks reproduced on purpose (all observable in the stock boot log): the
 * two init gates only warn and never stop the monitor, a clean boot never
 * opens the partition, the devname parameter is overwritten by the by-name
 * path, the stage wait budget is a two-step warntime -> errortimes ladder,
 * `monitor_get_pmsg` keeps its mapping for the rest of the boot, and the
 * printk level numbers of the stock unit are preserved at their sites
 * (fail/info lines are KERN_ERR, enter/exit lines KERN_INFO).
 */
#define pr_fmt(fmt) "bootmonitor: " fmt

#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kmsg_dump.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pagemap.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/vseq.h>
#include <linux/writeback.h>

/* ------------------------------------------------------------------ */
/* bootprof facility (drivers/misc/mediatek/mtprof/bootprof.c)        */
/* ------------------------------------------------------------------ */

struct log_t {
	/* task cmdline for the first TASK_COMM_LEN bytes, boot event after */
	char *comm_event;
	pid_t pid;
	u64 timestamp;
};

#define BOOTPROF_LOGS_PER_BUF	80

extern spinlock_t bootprof_lock;
extern unsigned long get_log_count(void);
extern struct log_t **get_bootprof_pointer(void);

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define BM_DRV_NAME		"bootmonitor_pmsg"
#define BM_PROC_NAME		"boot_monitor"
#define BM_COMPATIBLE		"xiaomi,bootmonitor_pmsg"
#define BM_DEV_PATH		"/dev/block/by-name/blackbox"

#define BM_EVENT_NR		10
#define BM_EVENT_NAME_LEN	40
#define BM_ROUND_JIFFIES	250		/* schedule_timeout(0xfa) */
#define BM_DEFAULT_TIMEOUT	5		/* boottime seed = timeout - 1 */
#define BM_TIMEOUT_SECS		45

#define BM_WRITE_BUF_SIZE	0xff000
#define BM_OOPS_BUF_SIZE	0x200200
#define BM_OOPS_DATA_OFF	0x200

#define BM_BLACKBOX_VERSION	0x1000

/* on-partition layout */
#define BM_LOG_TABLE_OFF	0x400
#define BM_LOG_TABLE_LEN	0x30
#define BM_LOG_TABLE_MAGIC	0x56775af41bcde0f2ULL
#define BM_ERR_TYPE_MAGIC	0x627759541bcde0f0ULL
#define BM_ERR_MAGIC		0xe99bb480dd25e1f0ULL
#define BM_BM_HDR_MAGIC		0xa99bd480dd25e1f0ULL
#define BM_BOOT_CONTENT_OFF	0x9a00000
#define BM_ERR_RECORD_REGION	0x100000
#define BM_ERR_HDR_SIZE		0x1000
#define BM_ERR_SLOT_BASE	0x3c000
#define BM_ERR_SLOT_STRIDE	0x1000
#define BM_BM_HDR_BODY_OFF	0x200000
#define BM_LOG_BUF_OFF		0x9c01000
#define BM_OOPS_OFF		0x9d00000
#define BM_OOPS_LEN		0x200000
#define BM_ERR_IDX_MAX		30

#define BM_VER			0x1000

/* ------------------------------------------------------------------ */
/* Driver data                                                        */
/* ------------------------------------------------------------------ */

struct bm_boot_event {
	int warntime;			/* rounds before the errortimes line */
	int errortimes;			/* further rounds before giving up */
	char name[BM_EVENT_NAME_LEN];
};

static struct bm_boot_event bm_boot_events[BM_EVENT_NR] = {
	{ 15, 25, "early-init" },
	{ 3,  5,  "late-init" },
	{ 5,  10, "late-fs" },
	{ 8,  15, "zygote-start" },
	{ 5,  10, "Zygote:Preload End" },
	{ 3,  5,  "SysServerInit_START" },
	{ 3,  5,  "PMS_scan_START" },
	{ 15, 25, "PMS_READY" },
	{ 15, 25, "AMS_READY" },
	{ 5,  10, "boot_completed" },
};

/* boot context, filled by the platform device probe from DT */
static struct {
	unsigned long mem_size;
	phys_addr_t mem_address;
	unsigned long console_size;
	unsigned long pmsg_size;
	void *oops_buf;
} boot_cxt;

static void *bm_write_buffer;
static char bm_boot_mode[10];
static char build_fingerprint[128];
static char bm_devices_name[128];
static char prof_node[0x40];
static char before_fingerprint[128];
static struct proc_dir_entry *bootmonitor_entry;
static struct task_struct *monitor_main;
static DEFINE_MUTEX(write_bm_mutex);
static int bm_event_counts;		/* stage the thread is waiting on */
static int ringnums;			/* bootprof ring position already seen */
static unsigned int count;		/* ring entries scanned, cumulative */
static unsigned long log_count_2;
static struct log_t **bootprof_2;
static u8 version_change;
static int bootindex;

static int blackbox;
module_param(blackbox, int, 0600);
MODULE_PARM_DESC(blackbox, "blackbox (default 0)");
module_param_string(bootmode, bm_boot_mode, sizeof(bm_boot_mode), 0644);
module_param_string(fingerprint, build_fingerprint, sizeof(build_fingerprint),
		    0644);
module_param_string(devname, bm_devices_name, sizeof(bm_devices_name), 0644);

/* blackbox partition handle (opened by the writer) */
static struct file *bdev_file;

/* ------------------------------------------------------------------ */
/* On-partition structures (sizes follow the stock layout)            */
/* ------------------------------------------------------------------ */

struct black_box_log_table_header {
	u64 magic;			/* 0x56775af41bcde0f2 */
	u32 version;			/* 0x1000 */
	u32 header_size;		/* 0x400 */
	u32 boot_index;
	u32 log_content_offset;		/* 0x400 */
	u32 minidump_offset;		/* 0x1800 */
	u32 minidump_is_valid;		/* 0x190 */
	u32 field_20;			/* 0x00a01000 */
	u32 control_content_offset;	/* 0x09800000 */
	u32 field_28;			/* 0x05c00400 */
	u32 boot_content_offset;	/* 0x09a00000 */
};					/* 0x30, naturally aligned */

/* packed: the u64 fields sit on odd offsets on the stock layout (0xbc) */
struct error_type_header {
	u64 magic;			/* 0x627759541bcde0f0 */
	u32 version;			/* 0x1000 */
	u32 header_size;		/* 0x1000 */
	u32 boot_index;			/* @0x10 */
	u64 uefi_log_offset;		/* @0x14, 0x100000 */
	u32 error_num;			/* @0x1c, 0x59 */
	u32 error_header_size;		/* @0x20, 0x1000 */
	u64 current_error;		/* @0x24 */
	char error_record[128];		/* @0x2c */
	char stage[16];			/* @0xac */
} __packed;

/* packed: the u64 fields sit on odd offsets on the stock layout (0xa1) */
struct error_header {
	u64 magic;			/* 0xe99bb480dd25e1f0 */
	u32 version;			/* 0x1000 */
	u32 header_size;		/* 0x1000 */
	u32 boot_index;
	u64 error_type;			/* error_init_type_table[idx] */
	u32 current_error;
	u8 error_current;
	char error_record[128];		/* event name */
} __packed;

struct boot_monitor_header {
	u64 magic;			/* 0xa99bd480dd25e1f0 */
	u32 version;			/* 0x1000 */
	u32 boot_index;
	u32 version_change;
	char os_version[128];		/* build_fingerprint */
	char event[128];		/* boot stage name */
} __packed;				/* 0x114 */

/*
 * Error type ids written to error_header.error_type, indexed by the boot
 * stage (the stock table is 90 u32 entries; only 1..8 are populated and the
 * meaning of the values is not carried in the blob).
 */
static const u32 error_init_type_table[90] = {
	0,
	0x35b43ff6, 0x35b43ff7, 0x35b43ff8, 0x35b43ff9,
	0x35b43ffa, 0x35b43ffb, 0x35b43ffc, 0x35b43ffd,
};

static struct black_box_log_table_header black_box_log_table_header;
static struct error_type_header error_type_header;
static struct error_header error_header;
static struct boot_monitor_header boot_monitor_header;

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static int monitor_main_thread_body(void *unused);
static int get_bm_devices(void);
static int write_blackbox_header(struct black_box_log_table_header *hdr);
static void exit_boot_monitor(void);
static int monitor_get_kmsg(char *buf, size_t len);
static int monitor_get_pmsg(void);
static int _partition_bm_read(loff_t pos, void *buf, size_t len);
static int partition_bm_write(loff_t pos, size_t len, const void *buf);
static void printerrorheader(void);
static void printbootheader(void);
static void printerrortypeheader(void);
static void add_timeout_header(char *dst);

/* ------------------------------------------------------------------ */
/* Partition I/O                                                      */
/* ------------------------------------------------------------------ */

/*
 * Sector-granular access through the block device page cache, mirroring the
 * stock unit (read_cache_page + kmap_local_page + memcpy; the read path
 * carries no locking).
 */
static int _partition_bm_read(loff_t pos, void *buf, size_t len)
{
	struct address_space *mapping = file_bdev(bdev_file)->bd_mapping;
	size_t done = 0;

	if (!len)
		return 0;

	while (len) {
		struct page *page;
		size_t off = pos & (PAGE_SIZE - 1);
		size_t n = min_t(size_t, len, PAGE_SIZE - off);
		void *va;

		page = read_cache_page(mapping, pos >> PAGE_SHIFT, NULL, NULL);
		if (IS_ERR(page))
			return PTR_ERR(page);
		va = kmap_local_page(page);
		memcpy(buf + done, va + off, n);
		kunmap_local(va);
		put_page(page);
		done += n;
		pos += n;
		len -= n;
	}
	return 0;
}

/*
 * Read-modify-write through the page cache with the stock unit's unchanged
 * page skip; the page is locked for the modify and the dirty pass plus the
 * writeback kick stay inside the changed branch.
 */
static int partition_bm_write(loff_t pos, size_t len, const void *buf)
{
	struct block_device *bdev = bdev_file ? file_bdev(bdev_file) : NULL;
	struct address_space *mapping;
	int ret = 0;
	size_t done = 0;

	if (IS_ERR_OR_NULL(bdev)) {
		pr_err("<monitor fail>%s-%d:failed:IS_ERR(bdev)\n", __func__,
		       101);
		return -1;
	}
	if (!buf) {
		pr_err("<monitor fail>%s-%d:buf is NULL\n", __func__, 105);
		return -1;
	}
	mapping = bdev->bd_mapping;

	mutex_lock(&write_bm_mutex);

	while (len) {
		struct page *page;
		size_t off = pos & (PAGE_SIZE - 1);
		size_t n = min_t(size_t, len, PAGE_SIZE - off);
		void *va;
		int changed = 0;

		page = read_cache_page(mapping, pos >> PAGE_SHIFT, NULL, NULL);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			break;
		}
		va = kmap_local_page(page);
		if (memcmp(va + off, buf + done, n)) {
			/* stock unit: the page is only touched when it differs */
			folio_lock(page_folio(page));
			memcpy(va + off, buf + done, n);
			changed = 1;
		}
		kunmap_local(va);
		if (changed) {
			set_page_dirty(page);
			unlock_page(page);
			balance_dirty_pages_ratelimited(mapping);
		}
		put_page(page);
		done += n;
		pos += n;
		len -= n;
	}

	mutex_unlock(&write_bm_mutex);
	sync_blockdev(bdev);
	return ret;
}

/*
 * Open the monitor device.  The stock unit rewrites the name buffer with the
 * by-name path every time (the devname parameter is overwritten, kept here
 * as-is), then opens with up to six attempts while the partition node
 * appears: wait_for_device_probe before each retry, 250 jiffies of sleep
 * between.
 */
static int get_bm_devices(void)
{
	int i;

	strscpy(bm_devices_name, BM_DEV_PATH, sizeof(bm_devices_name));

	bdev_file = bdev_file_open_by_path(bm_devices_name,
					   BLK_OPEN_READ | BLK_OPEN_WRITE,
					   NULL, NULL);
	if (IS_ERR(bdev_file)) {
		bdev_file = NULL;
		pr_err("<monitor fail>%s-%d:blkdev_get_by_dev end !\n", __func__,
		       167);
	}

	for (i = 1; i < 6 && !bdev_file; i++) {
		wait_for_device_probe();
		bdev_file = bdev_file_open_by_path(bm_devices_name,
						   BLK_OPEN_READ |
						   BLK_OPEN_WRITE, NULL, NULL);
		if (IS_ERR(bdev_file)) {
			bdev_file = NULL;
			schedule_timeout(BM_ROUND_JIFFIES);
		}
	}

	if (!bdev_file) {
		pr_err("<monitor fail>%s-%d: error: cannot open device %s\n",
		       __func__, 171, bm_devices_name);
		return -1;
	}

	pr_err("<monitor info>%s-%d:block2mtd: str %s:%zu\n", __func__, 180,
	       bm_devices_name, strlen(bm_devices_name));
	return 0;
}

/* ------------------------------------------------------------------ */
/* Blackbox writer                                                    */
/* ------------------------------------------------------------------ */

static void printerrorheader(void)
{
	pr_err("<monitor info>error_header     :magic=0x%lx\nversion=%u\nheader_size=%u\nboot_index=%u\nerror_type=%lu\ncurrent_error=%u\nerror_current=%d\nerror_record=%s\n",
	       (unsigned long)error_header.magic, error_header.version,
	       error_header.header_size, error_header.boot_index,
	       (unsigned long)error_header.error_type, error_header.current_error,
	       error_header.error_current, error_header.error_record);
}

static void printbootheader(void)
{
	pr_err("<monitor info>boot_monitor     :magic=0x%lx\nversion=%u\nboot_index=%u\nversion_change=%u\nos_version=%s\nevent=%s\n",
	       (unsigned long)boot_monitor_header.magic,
	       boot_monitor_header.version, boot_monitor_header.boot_index,
	       boot_monitor_header.version_change, boot_monitor_header.os_version,
	       boot_monitor_header.event);
}

static void printerrortypeheader(void)
{
	pr_err("<monitor info>error_type_header:magic=0x%lx\nversion=%u\nheader_size=%u\nuefi_log_offset=0x%lx\nboot_index=%u\nerror_num=%u\nerror_header_size=%u\ncurrent_error=%lu\nerror_record=%s\nstage=%s\n",
	       (unsigned long)error_type_header.magic, error_type_header.version,
	       error_type_header.header_size,
	       (unsigned long)error_type_header.uefi_log_offset,
	       error_type_header.boot_index, error_type_header.error_num,
	       error_type_header.error_header_size,
	       (unsigned long)error_type_header.current_error,
	       error_type_header.error_record, error_type_header.stage);
}

/*
 * One boot block written into dst: boot index, the stage, the reboot mode,
 * both builds and the timestamp.  The stock unit takes the timestamp in UTC+8
 * (the +28800 s offset in front of time64_to_tm).
 */
static void add_timeout_header(char *dst)
{
	struct timespec64 ts;
	struct tm tm;
	int n;

	pr_info(">>>enter  %s: line: 441.\n", __func__);
	if (!dst)
		return;

	ktime_get_coarse_real_ts64(&ts);
	time64_to_tm(ts.tv_sec + 28800, 0, &tm);

	memset(dst, 0, 0x200);
	n = snprintf(dst, 0x200,
		     "\n```\n## BOOT INDEX:%d\n## BOOT EVENTS:%s\n## BOOT MODE:%s\n##CURRENT VEISION:%s\n##BEFORE VEISION:%s\n## VEISION_CHANGE:%d\n##### %04ld-%02d-%02d %02d:%02d:%02d\n```c\n",
		     bootindex, bm_boot_events[bm_event_counts].name,
		     bm_boot_mode, build_fingerprint, before_fingerprint,
		     version_change, (long)tm.tm_year + 1900, tm.tm_mon + 1,
		     tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	pr_err("<monitor info>ret_len is %d, str_buf is %s\n", n, dst);
	pr_info("<<<exit  %s: line 457.\n", __func__);
}

/*
 * Refresh the blackbox headers on the partition.  Any header whose magic is
 * absent is seeded with the stock values first; the error slot for this boot
 * (index max(1, event_count - 1), stride 0x1000) gets the stage's error type
 * and name and bumps its occurrence counter.
 */
static int write_blackbox_header(struct black_box_log_table_header *hdr)
{
	struct error_header *eh = &error_header;
	struct boot_monitor_header *bh = &boot_monitor_header;
	int idx = bm_event_counts > 1 ? bm_event_counts - 1 : 1;
	int ret = 0;

	pr_info(">>>enter  %s: line: 293.\n", __func__);
	if (!bdev_file) {
		pr_err("<monitor fail>%s-%d:failed:IS_ERR(bdev)\n", __func__,
		       296);
		return -1;
	}
	if (idx >= BM_ERR_IDX_MAX) {
		pr_err("<monitor fail>%s-%d:event_count too large\n", __func__,
		       306);
		return -1;
	}
	pr_err("<monitor info>%s-%d:event_count is %d\n", __func__, 306, idx);

	ret = _partition_bm_read(BM_LOG_TABLE_OFF, hdr, BM_LOG_TABLE_LEN);
	if (ret) {
		pr_err("<monitor fail>%s-%d:_partition_bm_read fail\n", __func__,
		       309);
		return ret;
	}
	if (hdr->magic != BM_LOG_TABLE_MAGIC) {
		pr_err("<monitor fail>%s-%d:black_box_log_table_header.magic error\n",
		       __func__, 314);
		memset(hdr, 0, sizeof(*hdr));
		hdr->magic = BM_LOG_TABLE_MAGIC;
		hdr->version = BM_VER;
		hdr->header_size = BM_LOG_TABLE_OFF;
		hdr->log_content_offset = BM_LOG_TABLE_OFF;
		hdr->minidump_offset = 0x1800;
		hdr->minidump_is_valid = 0x190;
		hdr->field_20 = 0x00a01000;
		hdr->control_content_offset = 0x09800000;
		hdr->field_28 = 0x05c00400;
		hdr->boot_content_offset = BM_BOOT_CONTENT_OFF;
	} else if (hdr->boot_content_offset != BM_BOOT_CONTENT_OFF) {
		pr_err("<monitor fail>%s-%d:black_box_log_table_header.boot_content_offset=0x%x\n",
		       __func__, 323, BM_BOOT_CONTENT_OFF);
		hdr->boot_content_offset = BM_BOOT_CONTENT_OFF;
	}
	pr_err("<monitor info>log_table_header :magic=0x%lx\nversion=%u\nheader_size=%u\nboot_index=%u\nlog_content_offset=%u\nminidump_offset=%u\nminidump_is_valid=%u\ncontrol_content_offset=0x%x\nboot_content_offset=0x%x\n",
	       (unsigned long)hdr->magic, hdr->version, hdr->header_size,
	       hdr->boot_index, hdr->log_content_offset, hdr->minidump_offset,
	       hdr->minidump_is_valid, hdr->control_content_offset,
	       hdr->boot_content_offset);
	ret = partition_bm_write(BM_LOG_TABLE_OFF, BM_LOG_TABLE_LEN, hdr);
	if (ret) {
		pr_err("<monitor fail>%s-%d:partition_bm_write fail\n", __func__,
		       328);
		return ret;
	}

	bootindex = hdr->boot_index;

	ret = _partition_bm_read(hdr->boot_content_offset, &error_type_header,
				 sizeof(error_type_header));
	if (ret) {
		pr_err("<monitor fail>%s-%d:_partition_bm_read fail\n", __func__,
		       314);
		return ret;
	}
	if (error_type_header.magic != BM_ERR_TYPE_MAGIC) {
		pr_err("<monitor fail>%s-%d:error_type_header.magic error\n",
		       __func__, 318);
		memset(&error_type_header, 0, sizeof(error_type_header));
		error_type_header.magic = BM_ERR_TYPE_MAGIC;
		error_type_header.version = BM_VER;
		error_type_header.header_size = BM_ERR_HDR_SIZE;
		error_type_header.uefi_log_offset = BM_ERR_RECORD_REGION;
		error_type_header.error_header_size = BM_ERR_HDR_SIZE;
	}
	error_type_header.boot_index = hdr->boot_index;
	error_type_header.error_num = 0x59;
	error_type_header.current_error = error_init_type_table[idx];
	strscpy(error_type_header.error_record, "boot error for software",
		sizeof(error_type_header.error_record));
	strscpy(error_type_header.stage, "init ",
		sizeof(error_type_header.stage));
	printerrortypeheader();
	ret = partition_bm_write(hdr->boot_content_offset,
				 sizeof(error_type_header), &error_type_header);
	if (ret) {
		pr_err("<monitor fail>%s-%d:partition_bm_write fail\n", __func__,
		       354);
		return ret;
	}

	ret = _partition_bm_read(hdr->boot_content_offset +
				 error_type_header.header_size + BM_ERR_SLOT_BASE,
				 eh, sizeof(*eh));
	if (ret) {
		pr_err("<monitor fail>%s-%d:_partition_bm_read faild\n", __func__,
		       362);
		return ret;
	}
	if (eh->magic != BM_ERR_MAGIC) {
		pr_err("<monitor fail>%s-%d:error_header.magic error\n", __func__,
		       366);
		memset(eh, 0, sizeof(*eh));
		eh->magic = BM_ERR_MAGIC;
		eh->version = BM_VER;
		eh->header_size = BM_ERR_HDR_SIZE;
		eh->current_error = 1;
	} else {
		eh->current_error++;
	}
	eh->error_type = error_init_type_table[idx];
	eh->boot_index = hdr->boot_index;
	eh->error_current = 1;
	strncpy(eh->error_record, bm_boot_events[bm_event_counts].name,
		sizeof(eh->error_record) - 1);
	printerrorheader();
	ret = partition_bm_write(hdr->boot_content_offset +
				 error_type_header.header_size +
				 idx * BM_ERR_SLOT_STRIDE + BM_ERR_SLOT_BASE,
				 sizeof(*eh), eh);
	if (ret) {
		pr_err("<monitor fail>%s-%d:partition_bm_write fail\n", __func__,
		       379);
		return ret;
	}

	ret = _partition_bm_read(hdr->boot_content_offset + BM_BM_HDR_BODY_OFF,
				 bh, sizeof(*bh));
	if (ret) {
		pr_err("<monitor fail>%s-%d:_partition_bm_read fail\n", __func__,
		       387);
		return ret;
	}
	if (bh->magic != BM_BM_HDR_MAGIC) {
		pr_err("<monitor fail>%s-%d:boot_monitor_header.magic\n", __func__,
		       391);
		memset(bh, 0, sizeof(*bh));
		bh->magic = BM_BM_HDR_MAGIC;
		bh->version = BM_VER;
		strscpy(before_fingerprint, "no record for fisrt bootfail init",
			sizeof(before_fingerprint));
		version_change = 1;
	} else {
		pr_err("<monitor fail>build_fingerprint=%s, boot_monitor_header.os_version=%s\n",
		       build_fingerprint, bh->os_version);
		version_change = strcmp(build_fingerprint, bh->os_version) != 0;
		strncpy(before_fingerprint, bh->os_version,
			sizeof(before_fingerprint) - 1);
	}
	bh->boot_index = hdr->boot_index;
	bh->version_change = version_change;
	strncpy(bh->os_version, build_fingerprint, sizeof(bh->os_version) - 1);
	strncpy(bh->event, bm_boot_events[bm_event_counts].name,
		sizeof(bh->event) - 1);
	printbootheader();
	ret = partition_bm_write(hdr->boot_content_offset + BM_BM_HDR_BODY_OFF,
				 sizeof(*bh), bh);
	if (ret) {
		pr_err("<monitor fail>%s-%d:partition_bm_write fail\n", __func__,
		       422);
		return ret;
	}

	add_timeout_header(bm_write_buffer);
	add_timeout_header(boot_cxt.oops_buf);

	pr_info("<<<exit  %s: line 428.\n", __func__);
	return 0;
}

/* ------------------------------------------------------------------ */
/* kmsg / pmsg capture                                                */
/* ------------------------------------------------------------------ */

static int monitor_get_kmsg(char *buf, size_t len)
{
	struct kmsg_dump_iter iter;
	size_t out = 0;

	pr_info(">>>enter  %s: line: 465.\n", __func__);
	if (!buf || len == 0)
		return -1;
	memset(&iter, 0, sizeof(iter));
	kmsg_dump_rewind(&iter);
	kmsg_dump_get_buffer(&iter, true, buf, len, &out);
	pr_err("<monitor fail>ret_len is %zu\n", out);
	pr_info("<<<exit  %s: line 470.\n", __func__);
	return 0;
}

/* ramoops pmsg window signature at the head of the window */
#define BM_PMSG_SIG	0x43474244	/* "DBGC" */

/*
 * The pmsg window mapping is the ram_vmap() body inlined from
 * fs/pstore/ram_core.c: pages of the window (reserved memory right after the
 * console window) are mapped with VM_MAP | VM_IOREMAP and the mapping is
 * kept for the rest of the boot; a window whose signature is not the ramoops
 * one is skipped.  The stock unit checks neither the allocation nor the vmap
 * result, and this unit keeps that shape.
 */
static int monitor_get_pmsg(void)
{
	phys_addr_t base = boot_cxt.mem_address + boot_cxt.console_size;
	unsigned long size = boot_cxt.pmsg_size;
	unsigned long npages, off, i;
	struct page **arr;
	char *va, *dst;
	u32 sig, head, ring, n;

	pr_info(">>>enter  %s: line: 489.\n", __func__);

	npages = (size + (base & 0xfff) + 0xfff) >> 12;
	arr = kmalloc_array(npages, sizeof(*arr), GFP_KERNEL);
	if (!arr) {
		pr_err("%s: Failed to allocate array for %u pages\n", "ram_vmap",
		       (unsigned)npages);
		return -1;
	}
	for (i = 0; i < npages; i++)
		arr[i] = pfn_to_page((base >> PAGE_SHIFT) + i);

	va = vmap(arr, npages, VM_MAP | VM_IOREMAP, PAGE_KERNEL);
	kfree(arr);

	off = base & 0xfff;
	sig = *(u32 *)(va + off);
	if (sig != BM_PMSG_SIG) {
		pr_err("<monitor info>mtdoops: read pmsg failed sig = 0x%x \n",
		       sig);
		return -1;
	}

	head = *(u32 *)(va + off + 4);
	ring = *(u32 *)(va + off + 8);
	n = min_t(u32, ring, 0x1ffe00);
	dst = (char *)boot_cxt.oops_buf + BM_OOPS_DATA_OFF;
	if ((int)n > (int)head) {
		memcpy(dst, va + off + 0xc + ring - (n - head), n - head);
		memcpy(dst + (n - head), va + off + 0xc, head);
	} else {
		memcpy(dst, va + off + 0xc, n);
	}
	pr_info("<<<exit  %s: line 537.\n", __func__);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Polling thread                                                     */
/* ------------------------------------------------------------------ */

/*
 * Boot stage ledger.  Stage N is latched as soon as its marker string shows
 * up in the bootprof ring; the marker search continues where the previous
 * stage left off.  Each stage has a two-step waiting budget: `warntime`
 * rounds, then (after the errortimes line) `errortimes` more; a stage that
 * outlives both counts as a boot failure and the dump path runs.  `boottime`
 * counts the waiting rounds across the whole boot and drives the single
 * timeout line at 45 s.
 */
static int monitor_main_thread_body(void *unused)
{
	unsigned int boottime = BM_DEFAULT_TIMEOUT - 1;
	struct bm_boot_event *ev = &bm_boot_events[0];
	int failed = 0;
	int stage;

	pr_info(">>>enter  %s: line: 153.\n", __func__);

	if (kthread_should_stop()) {
		pr_info("<<<exit  %s: line 204.\n", __func__);
		return 0;
	}

	bm_event_counts = 0;
	ringnums = 0;
	count = 0;

	for (stage = 0; stage < BM_EVENT_NR; stage++) {
		unsigned int budget;
		unsigned int k = 0;
		int found = 0;

		ev = &bm_boot_events[stage];
		budget = (unsigned int)ev->warntime + 1;
		failed = 0;

		pr_err("<monitor info>counts = bm_boot_events[%d].warntime = %d\n",
		       stage, ev->warntime);

		for (;;) {
			spin_lock(&bootprof_lock);
			log_count_2 = get_log_count();
			bootprof_2 = get_bootprof_pointer();
			while ((unsigned long)ringnums < log_count_2) {
				unsigned int slot =
					ringnums / BOOTPROF_LOGS_PER_BUF;
				unsigned int idx =
					ringnums % BOOTPROF_LOGS_PER_BUF;
				struct log_t *entries = bootprof_2 ?
					bootprof_2[slot] : NULL;

				ringnums++;
				count++;
				if (!entries || !entries[idx].comm_event)
					continue;
				if (strstr(entries[idx].comm_event +
					   TASK_COMM_LEN, ev->name)) {
					found = 1;
					break;
				}
			}
			spin_unlock(&bootprof_lock);

			if (found)
				break;

			if (k == budget - 1) {
				if (k != (unsigned int)ev->warntime) {
					if (boottime == BM_TIMEOUT_SECS) {
						pr_err("<monitor info>%s timeout:boottime is %d line=%d\n",
						       __func__, BM_TIMEOUT_SECS,
						       179);
						boottime = 0;
					}
					failed = 1;
					break;
				}
				pr_err("<monitor info>counts = bm_boot_events[%d].errortimes = %d\n",
				       stage, ev->errortimes);
				budget = (unsigned int)ev->errortimes + 1;
			}
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(BM_ROUND_JIFFIES);
			k++;
			boottime++;
		}

		if (found)
			pr_err("<monitor fail>%s done\n", ev->name);
		pr_err("<monitor fail>%s-%d:set task to running\n", __func__,
		       185);
		set_current_state(TASK_RUNNING);

		if (failed)
			break;

		bm_event_counts = stage + 1;
	}

	if (!failed) {
		strscpy(prof_node, "boot-ok", sizeof(prof_node));
		exit_boot_monitor();
		pr_info("<<<exit  %s: line 201.\n", __func__);
		return 0;
	}

	{
		char errbuf[64] = "error:";

		strncpy(errbuf + strlen(errbuf), ev->name,
			sizeof(errbuf) - strlen(errbuf) - 1);
		strscpy(prof_node, errbuf, sizeof(prof_node));

		if (get_bm_devices()) {
			pr_err("<monitor info>error write end\n");
			exit_boot_monitor();
			return 0;
		}
		if (!bm_write_buffer || !boot_cxt.oops_buf) {
			pr_err("<monitor fail>%s-%d:cxt->oops_buf or buffer is NULL\n",
			       __func__, 369);
			pr_err("<monitor info>error write end\n");
			exit_boot_monitor();
			return 0;
		}

		monitor_get_kmsg(bm_write_buffer + BM_OOPS_DATA_OFF,
				 BM_WRITE_BUF_SIZE - BM_OOPS_DATA_OFF);
		monitor_get_pmsg();
		write_blackbox_header(&black_box_log_table_header);
		partition_bm_write(BM_LOG_BUF_OFF, BM_WRITE_BUF_SIZE,
				   bm_write_buffer);
		partition_bm_write(BM_OOPS_OFF, BM_OOPS_LEN,
				   boot_cxt.oops_buf);
		pr_err("<monitor info>error write end\n");
		exit_boot_monitor();
		pr_info("<<<exit  %s: line 201.\n", __func__);
		return 0;
	}
}

/* ------------------------------------------------------------------ */
/* /proc interface                                                    */
/* ------------------------------------------------------------------ */

static int boot_monitor_show(struct seq_file *m, void *v)
{
	if (!m) {
		pr_err("<monitor fail>seq_file is Null.\n");
		return 0;
	}
	seq_printf(m, "%s\n", prof_node);
	return 0;
}

static int boot_monitor_open(struct inode *inode, struct file *file)
{
	return single_open(file, boot_monitor_show, inode->i_private);
}

static ssize_t boot_monitor_write(struct file *file, const char __user *buf,
				  size_t len, loff_t *ppos)
{
	char kbuf[32];
	size_t n = min(len, sizeof(kbuf) - 1);

	if (copy_from_user(kbuf, buf, n))
		return -EFAULT;
	kbuf[n] = '\0';
	strscpy(prof_node, kbuf, sizeof(prof_node));
	return len;
}

static const struct proc_ops boot_monitor_fops = {
	.proc_open = boot_monitor_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
	.proc_write = boot_monitor_write,
};

/* ------------------------------------------------------------------ */
/* Platform driver                                                    */
/* ------------------------------------------------------------------ */

static int boot_monitor_probe(struct platform_device *pdev)
{
	struct resource *res;
	u32 val = 0;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("<monitor fail>failed to locate DT /reserved-memory resource\n");
		return -EINVAL;
	}
	boot_cxt.mem_address = res->start;
	boot_cxt.mem_size = resource_size(res);

	ret = of_property_read_u32_array(pdev->dev.of_node, "console-size",
					 &val, 1);
	if (ret && ret != -EINVAL) {
		pr_err("<monitor fail>failed to parse property %s: %d\n",
		       "console-size", ret);
		return ret;
	}
	if (ret == -EINVAL)
		val = 0;
	if ((int)val < 0) {
		pr_err("<monitor fail>%s:%u > INT_MAX\n", "console-size", val);
		return -EOVERFLOW;
	}
	boot_cxt.console_size = val;

	val = 0;
	ret = of_property_read_u32_array(pdev->dev.of_node, "pmsg-size", &val, 1);
	if (ret && ret != -EINVAL) {
		pr_err("<monitor fail>failed to parse property %s: %d\n",
		       "pmsg-size", ret);
		return ret;
	}
	if (ret == -EINVAL)
		val = 0;
	if ((int)val < 0) {
		pr_err("<monitor fail>%s:%u > INT_MAX\n", "pmsg-size", val);
		return -EOVERFLOW;
	}
	boot_cxt.pmsg_size = val;

	pr_err("<monitor info>pares, mem_address =0x%llx, mem_size =0x%lx, console-size =0x%lx, pmsg-size =0x%lx\n",
	       (u64)boot_cxt.mem_address, boot_cxt.mem_size,
	       boot_cxt.console_size, boot_cxt.pmsg_size);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = BM_COMPATIBLE },
	{},
};

static struct platform_driver boot_monitor_driver = {
	.probe = boot_monitor_probe,
	.driver = {
		.name = BM_DRV_NAME,
		.of_match_table = dt_match,
	},
};

/* ------------------------------------------------------------------ */
/* init / exit                                                        */
/* ------------------------------------------------------------------ */

static int __init boot_monitor_init(void)
{
	if (blackbox != BM_BLACKBOX_VERSION)
		pr_err("<monitor fail>%s-%d:blackbox  is %d, BLACKBOX_VERSION is %d, no bootfail partition\n",
		       __func__, blackbox, BM_BLACKBOX_VERSION, 264);

	if (strcmp(bm_boot_mode, "normal"))
		pr_err("<monitor fail>%s-%d:boot BOOT_MODE_NORMAL is %s,no need boot monitor\n",
		       __func__, 270, bm_boot_mode);

	bm_write_buffer = vzalloc(BM_WRITE_BUF_SIZE);
	if (!bm_write_buffer) {
		pr_err("<monitor fail>%s-%d:bm_write_buffer vzalloc fail\n",
		       __func__, 276);
		return 0;
	}

	boot_cxt.oops_buf = vzalloc(BM_OOPS_BUF_SIZE);
	if (!boot_cxt.oops_buf) {
		pr_err("<monitor fail>%s-%d:cxt->oops_buf vzalloc fail\n",
		       __func__, 281);
		vfree(bm_write_buffer);
		return 0;
	}

	pr_err("<monitor info>cxt->oops_buf is 0x%p\n", boot_cxt.oops_buf);
	pr_err("<monitor info>bootprof_lock is 0x%p\n", &bootprof_lock);

	platform_driver_register(&boot_monitor_driver);

	bootmonitor_entry = proc_create(BM_PROC_NAME, 0664, NULL,
					&boot_monitor_fops);
	if (!bootmonitor_entry) {
		vfree(bm_write_buffer);
		vfree(boot_cxt.oops_buf);
		pr_err("<monitor fail>%s-%d:fail to create file node\n", __func__,
		       302);
		return 0;
	}

	monitor_main = kthread_create(monitor_main_thread_body, NULL,
				      "monitor_main");
	if (IS_ERR(monitor_main)) {
		monitor_main = NULL;
		vfree(bm_write_buffer);
		vfree(boot_cxt.oops_buf);
		proc_remove(bootmonitor_entry);
		pr_err("<monitor fail>%s-%d:create thread monitor_main\n",
		       __func__, 314);
		return 0;
	}
	wake_up_process(monitor_main);

	pr_info("<<<exit  %s: line 317.\n", __func__);
	return 0;
}

/* called by the monitor thread once the boot stage ledger is complete */
static void exit_boot_monitor(void)
{
	pr_info(">>>enter  %s: line: 44.\n", __func__);

	if (bdev_file) {
		sync_blockdev(file_bdev(bdev_file));
		invalidate_mapping_pages(file_bdev(bdev_file)->bd_mapping, 0, -1);
		pr_err("<monitor fail>%s-%d:invalidate_mapping_pages end\n",
		       __func__, 53);
		bdev_fput(bdev_file);
		bdev_file = NULL;
	} else {
		pr_err("<monitor fail>%s-%d:bdev no init\n", __func__, 47);
	}

	if (bm_write_buffer) {
		pr_err("<monitor fail>%s-%d:bm_write_buffer is %p\n", __func__,
		       56, bm_write_buffer);
		vfree(bm_write_buffer);
	}
	if (boot_cxt.oops_buf) {
		pr_err("<monitor fail>%s-%d:cxt->oops_buf is %p\n", __func__, 60,
		       boot_cxt.oops_buf);
		vfree(boot_cxt.oops_buf);
	}

	platform_driver_unregister(&boot_monitor_driver);
	monitor_main = NULL;
	pr_info("<<<exit  %s: line 67.\n", __func__);
}

static void __exit boot_monitor_exit(void)
{
	if (bdev_file) {
		sync_blockdev(file_bdev(bdev_file));
		invalidate_mapping_pages(file_bdev(bdev_file)->bd_mapping, 0, -1);
		pr_err("<monitor fail>%s-%d:invalidate_mapping_pages end\n",
		       __func__, 328);
		bdev_fput(bdev_file);
		bdev_file = NULL;
	} else {
		pr_err("<monitor fail>%s-%d:bdev no\n", __func__, 110);
	}
	if (bm_write_buffer) {
		pr_err("<monitor fail>%s-%d:bm_write_buffer is %p\n", __func__,
		       337, bm_write_buffer);
		vfree(bm_write_buffer);
	}
	if (boot_cxt.oops_buf) {
		pr_err("<monitor fail>%s-%d:cxt->oops_buf is %p\n", __func__, 341,
		       boot_cxt.oops_buf);
		vfree(boot_cxt.oops_buf);
	}
	pr_err("<monitor fail>%s-%d:bootmonitor_entry release\n", __func__,
	       345);
	proc_remove(bootmonitor_entry);
	if (monitor_main) {
		pr_err("<monitor fail>kthread_stop monitor_main\n");
		platform_driver_unregister(&boot_monitor_driver);
		kthread_stop(monitor_main);
	}
	pr_err("<monitor fail>exit monitor_main\n");
}

vseq_module_init(boot_monitor_init);
module_exit(boot_monitor_exit);

MODULE_DESCRIPTION("Xiaomi boot monitor");
MODULE_LICENSE("GPL v2");
