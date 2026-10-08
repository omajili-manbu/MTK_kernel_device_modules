/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/version.h>
#include <linux/uidgid.h>

#include "xiaomi_touch.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0))
/*
* Commit 359745d78351 ("proc: remove PDE_DATA() completely")
* Replaced PDE_DATA() with pde_data()
*/
#define pde_data(inode) PDE_DATA(inode)
#endif

#define PROC_COUNT_FOR_PANEL	(4)
#define DUMP_DATA_BUF_SIZE		(PAGE_SIZE * 3)
/* _b583-XT③：blob proc_open 0x5658 段 `mov w0,#0x140`(=320) + `mov w1,#3`
 * -> kvzalloc_retry(320,3)（19227 行 IDA 视图：MOV W0,#0x140/MOV W1,#3/BL）。
 * 树侧 donor 为 350，按 blob 改 320。 */
#define NORMAL_DATA_BUF_SIZE	(320)


/* _b583-XT③：blob 无 xiaomi_touch_proc_data_t —— proc_open 0xB628/0xB638
 * `str x22,[file+0xd8]`（file->private_data = pde_data=driver_param），结果缓冲与
 * 长度改为按 touch_id 索引的两条全局：symtab `tp_proc_result_buf`(.bss+0x8ac0,16B)、
 * `tp_proc_result_length`(.bss+0x8ad0,16B)；proc_release 0xC220/0xC24C/0xC270
 * 读/零这两个槽。树侧 donor 的 per-fd 结构体（含多一次 kvzalloc_retry 与多一条
 * "alloc tp proc data memory failed"）按 blob 删除。 */

static struct proc_dir_entry *tp_pde[MAX_TOUCH_PANEL_COUNT][PROC_COUNT_FOR_PANEL];
static int self_test_result[MAX_TOUCH_PANEL_COUNT];
/* _b581-XT③：blob 有 tp_proc_mutex[]（串 "&tp_proc_mutex[touch_id]"），
 * xiaomi_touch_create_proc 末尾 __mutex_init、proc_open 首处 mutex_lock、
 * proc_release 末处 mutex_unlock（见 blob 各函数调用集）。
 * 数组不能走 DEFINE_MUTEX（宏内含结构体指定初始化），与 blob 一致在 create_proc 内 init。 */
static struct mutex tp_proc_mutex[MAX_TOUCH_PANEL_COUNT];
static u8 *tp_proc_result_buf[MAX_TOUCH_PANEL_COUNT];
static long tp_proc_result_length[MAX_TOUCH_PANEL_COUNT];

static int proc_open(struct inode *inode, struct file *file)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = (xiaomi_touch_driver_param_t *)pde_data(inode);
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	/* _b583-XT③：blob proc_open 逐句对齐 ——
	 *   0xB608/0xB614 smaddl(&tp_proc_mutex[touch_id]，步长 0x30) + 0xB624 mutex_lock；
	 *   0xB638 `str x22,[x20,#0xd8]` = file->private_data = driver_param（非 proc_data 结构）；
	 *   0xB628/0xB63C strncmp("tp_data_dump", name, 12) 后分两支：dump 支 0xB6A8 =
	 *   kvzalloc_retry(0x3000=12288,3)、普通支 0xB658 = kvzalloc_retry(0x140=320,3)，
	 *   结果均落 tp_proc_result_buf[touch_id]（0xB67C/0xB6CC）；
	 *   0xB6EC `str xzr,[tp_proc_result_length+touch_id*8]` 清零、末 LOG_DEBUG("open %s")。 */
	mutex_lock(&tp_proc_mutex[touch_id]);
	file->private_data = xiaomi_touch_driver_param;

	if (!strncmp("tp_data_dump", name, 12)) {
		LOG_DEBUG("alloc dump data memory");
		tp_proc_result_buf[touch_id] = kvzalloc_retry(DUMP_DATA_BUF_SIZE, 3);
		if (!tp_proc_result_buf[touch_id]) {
			LOG_ERROR("alloc tp proc dump memory failed");
			mutex_unlock(&tp_proc_mutex[touch_id]);
			return -1;
		}
	} else {
		LOG_DEBUG("alloc proc data memory");
		tp_proc_result_buf[touch_id] = kvzalloc_retry(NORMAL_DATA_BUF_SIZE, 3);
		if (!tp_proc_result_buf[touch_id]) {
			LOG_ERROR("alloc tp proc data memory failed");
			mutex_unlock(&tp_proc_mutex[touch_id]);
			return -1;
		}
	}

	tp_proc_result_length[touch_id] = 0;
	LOG_DEBUG("open %s", name);
	return 0;
}

static int proc_release(struct inode *inode, struct file *file)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = (xiaomi_touch_driver_param_t *)file->private_data;
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	/* _b581-XT③：blob proc_release 只有 1 处 kvzalloc_free + 1 处 mutex_unlock，
	 * 树侧原为 donor 的重复 free 块（对同一 buf 二次 kvzalloc_free）。 */
	/* _b583-XT③：blob proc_release 0xC1F8-0xC280 —— private_data 即 driver_param；
	 * 0xC220 `ldr x0,[tp_proc_result_buf+touch_id*8]`，非空则 LOG_DEBUG
	 * "free proc data buf memory" + kvzalloc_free + 0xC24C 写 NULL；
	 * 0xC260-0xC270 `str xzr,[tp_proc_result_length+touch_id*8]` + LOG_DEBUG("release %s")。 */
	if (tp_proc_result_buf[touch_id]) {
		LOG_DEBUG("free proc data buf memory");
		kvzalloc_free(tp_proc_result_buf[touch_id]);
		tp_proc_result_buf[touch_id] = NULL;
	}
	tp_proc_result_length[touch_id] = 0;

	LOG_DEBUG("release %s", name);
	/* blob proc_release 末处 mutex_unlock(&tp_proc_mutex[touch_id]) */
	mutex_unlock(&tp_proc_mutex[touch_id]);
	return 0;
}

static ssize_t proc_tp_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = (xiaomi_touch_driver_param_t *)file->private_data;
	char *name = file->f_path.dentry->d_iname;
	int valid_length;
	s8 touch_id = -1;
	int ret = 0;
	int n = 0;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;

	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;

	LOG_DEBUG("*pos is %lld", *pos);
	if (*pos && *pos >= tp_proc_result_length[touch_id]) {
		return 0;
	}

	if (*pos) {
		goto copy_data_to_user;
	}

	/* _b581-XT③：blob proc_tp_read 该串为 MI_TP_I（LOG_INFO）；proc_tp_write 同名串为 D。 */
	LOG_INFO("open proc tp node name is %s", name);
	if (!strncmp("tp_fw_version", name, 10)) {
		LOG_DEBUG("read tp_fw_version");
		if (xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version)
			xiaomi_touch_driver_param->hardware_operation.ic_get_fw_version(xiaomi_touch_driver_param->hardware_param.fw_version);
		n += snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "fw version: %s\n", xiaomi_touch_driver_param->hardware_param.fw_version);
		n += snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "driver version: %s\n", xiaomi_touch_driver_param->hardware_param.driver_version);
		n += snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "hal version: %s\n", xiaomi_touch_driver_param->hal_version);
		/* _b581-XT③：blob proc_tp_read 的 tp_fw_version 分支只有 fw/driver/hal/
		 * xiaomi-touch 四条（无 "limit version: %s" 串，tree-only donor），删除该打印。 */
		n += snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "xiaomi-touch version: %s\n", XIAOMI_TOUCH_VERSION);
		tp_proc_result_length[touch_id] = n;
	} else if (!strncmp("tp_lockdown_info", name, 16)) {
		u8 *lockdown_info = xiaomi_touch_driver_param->hardware_param.lockdown_info;
		LOG_DEBUG("read tp_lockdown_info");
		if (xiaomi_touch_driver_param->hardware_operation.ic_get_lockdown_info) {
			/* _b581-XT③：blob 无 "read tp_lockdown_info error" 串（tree-only donor），
			 * ret<0 分支删除，仅保留 ic_get_lockdown_info 调用。 */
			ret = xiaomi_touch_driver_param->hardware_operation.ic_get_lockdown_info(lockdown_info);
			n = snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n,
					"0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X\n",
					lockdown_info[0], lockdown_info[1], lockdown_info[2], lockdown_info[3],
					lockdown_info[4], lockdown_info[5], lockdown_info[6], lockdown_info[7]);
		} else {
			n = snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "null");
		}
		tp_proc_result_length[touch_id] = n;
	} else if (!strncmp("tp_selftest", name, 11)) {
		/* _b581-XT③：blob proc_tp_read 该串为 MI_TP_I（"read tp_selftest result %d"）。 */
		LOG_INFO("read tp_selftest result %d", self_test_result[touch_id]);
		n = snprintf(tp_proc_result_buf[touch_id] + n, NORMAL_DATA_BUF_SIZE - n, "%d\n", self_test_result[touch_id]);
		tp_proc_result_length[touch_id] = n;
	} else if (!strncmp("tp_data_dump", name, 12)) {
		LOG_DEBUG("read tp_data_dump");
		if (xiaomi_touch_driver_param->hardware_operation.ic_data_collect)
			xiaomi_touch_driver_param->hardware_operation.ic_data_collect(tp_proc_result_buf[touch_id], &n);
		tp_proc_result_length[touch_id] = n;
	}

copy_data_to_user:

	valid_length = tp_proc_result_length[touch_id] - *pos;
	valid_length = valid_length < count ? valid_length : count;
	LOG_DEBUG("result length %ld, valid length %d, count %zu", tp_proc_result_length[touch_id], valid_length, count);
	if (valid_length && (ret = copy_to_user(buf, tp_proc_result_buf[touch_id] + *pos, valid_length))) {
		LOG_ERROR("copy result to user failed, result is %d", ret);
		return -EFAULT;
	}
	*pos += valid_length;
	return valid_length;
}

static ssize_t proc_tp_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = (xiaomi_touch_driver_param_t *)file->private_data;
	char *name = file->f_path.dentry->d_iname;
	s8 touch_id = -1;

	if (!xiaomi_touch_driver_param)
		return -EFAULT;
	touch_id = xiaomi_touch_driver_param->touch_id;
	if (IS_TOUCH_ID_INVALID(touch_id))
		return -EFAULT;
	LOG_DEBUG("open proc tp node name is %s", name);

	if (!strncmp("tp_fw_version", name, 10)) {
		size_t temp_count = count;
		char temp_buf[HAL_VERSION_LENGTH];
		memset(temp_buf, 0x00, HAL_VERSION_LENGTH);
		if (temp_count >= HAL_VERSION_LENGTH) {
			LOG_ERROR("write hal version length too long, current length is %zu", temp_count);
			temp_count = HAL_VERSION_LENGTH - 1;
		}
		if (copy_from_user(temp_buf, buf, temp_count)) {
			LOG_ERROR("copy hal version to buf has error");
			return -EFAULT;
		}
		if (temp_buf[0] == 's') {
			memset(xiaomi_touch_driver_param->hal_version, 0x00, HAL_VERSION_LENGTH);
			memcpy(xiaomi_touch_driver_param->hal_version, temp_buf, temp_count);
			LOG_INFO("hal version is %s", xiaomi_touch_driver_param->hal_version);
		}
		LOG_INFO("fw version is %s", xiaomi_touch_driver_param->hardware_param.fw_version);
		LOG_INFO("driver version is %s", xiaomi_touch_driver_param->hardware_param.driver_version);
		LOG_INFO("xiaomi-touch version is %s", XIAOMI_TOUCH_VERSION);
	} else if (!strncmp("tp_selftest", name, 11)) {
		static char self_test_cmd[64];
		size_t temp_count = count;
		int maxCount = 3;
		int retry = 0;
		memset(self_test_cmd, 0, 64);
		if (temp_count >= 64) {
			LOG_ERROR("write self test cmd length too long, current length is %zu", temp_count);
			temp_count = 64 - 1;
		}
		if (copy_from_user(self_test_cmd, buf, temp_count)) {
			LOG_ERROR("copy self test has error");
			return -EFAULT;
		}
		if (xiaomi_touch_driver_param->hardware_operation.ic_self_test) {
		ic_self_test:
			xiaomi_touch_driver_param->hardware_operation.ic_self_test(self_test_cmd, &self_test_result[touch_id]);
			LOG_ERROR("start self test cmd: %s", self_test_cmd);
			if (retry < maxCount && self_test_result[xiaomi_touch_driver_param->touch_id] != 2) {
				LOG_ERROR("self test failed, result is %d, retry %d times", self_test_result[xiaomi_touch_driver_param->touch_id], retry);
				retry++;
				goto ic_self_test;
			}
		}
		LOG_ERROR("self test result is %d", self_test_result[xiaomi_touch_driver_param->touch_id]);
	}

	return count;
}

static const struct proc_ops proc_tp_ops = {
	.proc_open = proc_open,
	.proc_release = proc_release,
	.proc_read = proc_tp_read,
	.proc_write = proc_tp_write,
	.proc_lseek = default_llseek,
};

static struct proc_dir_entry *create_proc_node(char *name, const struct proc_ops *proc_ops, xiaomi_touch_driver_param_t *xiaomi_touch_driver_param)
{
	struct proc_dir_entry *pde = proc_create_data(name, 0644, NULL, proc_ops, xiaomi_touch_driver_param);

	/* _b581-XT③：blob create_proc_node 只有 proc_create_data + NULL 检查
	 * （0x5a10 处 ldr x8,[x1,#0xb0] → d_iname 用法同），无 proc_set_user /
	 * make_kuid/make_kgid（树侧 donor 附加，会改 /proc 节点属主，按 blob 删除）。 */
	if (!pde) {
		LOG_ERROR("proc_create_data has error, exit!");
		return NULL;
	}

	return pde;
}

int xiaomi_touch_create_proc(xiaomi_touch_driver_param_t *xiaomi_touch_driver_param)
{
	s8 touch_id = -1;
	char proc_name[64];
	char name_suffix[10];

	if (!xiaomi_touch_driver_param )
		return -1;

	touch_id = xiaomi_touch_driver_param->touch_id;
	LOG_DEBUG("touch id is %d", touch_id);
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_DEBUG("invalid touch id %d", touch_id);
		return -1;
	}

	memset(name_suffix, 0, 10);
	if (touch_id > 0)
		snprintf(name_suffix, 10, "_%d", touch_id);

	/* create tp proc */
	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_fw_version%s", name_suffix);
	tp_pde[touch_id][0] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_lockdown_info%s", name_suffix);
	tp_pde[touch_id][1] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_selftest%s", name_suffix);
	tp_pde[touch_id][2] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	memset(proc_name, 0, 64);
	snprintf(proc_name, 64, "tp_data_dump%s", name_suffix);
	tp_pde[touch_id][3] = create_proc_node(proc_name, &proc_tp_ops, xiaomi_touch_driver_param);

	/* _b581-XT③：blob xiaomi_touch_create_proc 末尾有一处 __mutex_init
	 * （唯一实参 = &tp_proc_mutex[touch_id]，lockdep 名 "&tp_proc_mutex[touch_id]"）。 */
	mutex_init(&tp_proc_mutex[touch_id]);

	return 0;
}

int xiaomi_touch_remove_proc(s8 touch_id)
{
	int i = 0;

	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id is invalid, return!");
		return -1;
	}

	for (i = 0; i < PROC_COUNT_FOR_PANEL; i++) {
		if (!tp_pde[touch_id][i])
			continue;
		LOG_DEBUG("remove proc node %p", tp_pde[touch_id][i]);
		proc_remove(tp_pde[touch_id][i]);
		tp_pde[touch_id][i] = NULL;
	}
	return 0;
}
