/*
 * Copyright (c) 2023 Xiaomi, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Xiaomi, Inc.
 */
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/stdarg.h>
#include <linux/power_supply.h>
#include <net/sock.h>
#include <net/netlink.h>

#include "xiaomi_touch.h"
#include <linux/vseq.h>
#include "miev/mievent.h"
#include <linux/vseq.h>
#include "miev/mievent.h"
xiaomi_touch_t xiaomi_touch;
static struct task_struct *xiaomi_touch_temp_thread = NULL;
static int touch_id_for_temperature = 0;

#define NETLINK_TEST 24
#define MAX_MSGSIZE 16
int stringlength(char *s);
static int pid = -1;
struct sock *nl_sk;

void sendnlmsg(char message)//char *message
{
	struct sk_buff *skb_1;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);
	int slen = 0;
	int ret = 0;

	if (!nl_sk || !pid) {
		return;
	}

	skb_1 = alloc_skb(len, GFP_KERNEL);

	if (!skb_1) {
		LOG_ERROR("alloc_skb error\n");
		return;
	}

	slen = sizeof(message);
	nlh = nlmsg_put(skb_1, 0, 0, 0, MAX_MSGSIZE, 0);
	NETLINK_CB(skb_1).portid = 0;
	NETLINK_CB(skb_1).dst_group = 0;
//	message[slen] = '\0';
	memcpy(NLMSG_DATA(nlh), &message, slen);
	ret = netlink_unicast(nl_sk, skb_1, pid, MSG_DONTWAIT);

	if (!ret) {
		/*kfree_skb(skb_1); */
		LOG_ERROR("send msg from kernel to usespace failed ret 0x%x\n",
		       ret);
	}
}

void nl_data_ready(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char str[100] = {'\0'};
	skb = skb_get(__skb);

	if (skb->len >= NLMSG_SPACE(0)) {
		nlh = nlmsg_hdr(skb);
		memcpy(str, NLMSG_DATA(nlh), sizeof(str));
		pid = nlh->nlmsg_pid;
		kfree_skb(skb);
	}
	LOG_INFO("netlink socket, pid =%d, msg = %d\n", pid, str[0]);
}

int netlink_init(void)
{
	struct netlink_kernel_cfg netlink_cfg;
	memset(&netlink_cfg, 0, sizeof(struct netlink_kernel_cfg));
	netlink_cfg.groups = 0;
	netlink_cfg.flags = 0;
	netlink_cfg.input = nl_data_ready;
	nl_sk = netlink_kernel_create(&init_net, NETLINK_TEST, &netlink_cfg);

	if (!nl_sk) {
		LOG_ERROR("[Probe Failed] create netlink socket error\n");
		return 1;
	}

	return 0;
}

void netlink_exit(void)
{
	if (nl_sk != NULL) {
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}

	LOG_INFO("self module exited\n");
}

void *kzalloc_retry(size_t size, int retry)
{
	void *p = NULL;
	while (retry > 0) {
		retry--;
		p = kzalloc(size, GFP_KERNEL);
		if (p)
			return p;
	}
	return NULL;
}

void kzalloc_free(void *p)
{
	kfree(p);
}

void *kvzalloc_retry(size_t size, int retry)
{
	void *p = NULL;

	while (retry > 0) {
		retry--;
		p = kvzalloc(size, GFP_KERNEL);
		if (p)
			return p;
	}
	return NULL;
}

void kvzalloc_free(void *p)
{
	kvfree(p);
}

xiaomi_touch_data_t *get_xiaomi_touch_data(s8 touch_id)
{
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id %d hasn't select, return!", touch_id);
		return NULL;
	} else if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		LOG_ERROR("panel in touch id %d hasn't register, return!", touch_id);
		return NULL;
	}
	return &xiaomi_touch.xiaomi_touch_data[touch_id];
}

xiaomi_touch_driver_param_t *get_xiaomi_touch_driver_param(s8 touch_id)
{
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		return NULL;
	} else if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		LOG_ERROR("panel in touch id %d hasn't register, return!", touch_id);
		return NULL;
	}
	return &xiaomi_touch.xiaomi_touch_driver_param[touch_id];
}

void notify_xiaomi_touch(xiaomi_touch_data_t *xiaomi_touch_data, enum poll_notify_type type)
{
	private_data_t *client_private_data = NULL;
	if (!xiaomi_touch_data)
		return;
	/* _b583-XT：blob notify_xiaomi_touch 0x54c-0x5f0 列表遍历内**只有一处**唤醒：
	 * 0x59c `add x0,x21,#0x18; mov w1,#3; mov w2,wzr; mov x3,xzr; bl __wake_up`
	 * （= wake_up_all(&client_private_data->poll_wait_queue_head)，mode=3(nr_exclusive=0)
	 * ⇔ wake_up_all 形态）；type 形参在 blob 全程未用（blob private_data 只有 1 个队列，
	 * 见 xiaomi_touch.h 锚点）⇒ 删除按 type 的三分支唤醒。 */
	spin_lock(&xiaomi_touch_data->private_data_lock);
	list_for_each_entry_rcu(client_private_data, &xiaomi_touch_data->private_data_list, node) {
		LOG_VERBOSE("notify xiaomi-touch data update, client private data is %p", client_private_data);
		wake_up_all(&client_private_data->poll_wait_queue_head);
	}
	spin_unlock(&xiaomi_touch_data->private_data_lock);
	(void)type;
}

void add_common_data_to_buf_common(s8 touch_id, enum common_data_cmd cmd, enum common_data_mode mode, int length, int *data)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	common_data_t *common_data = NULL;

	if (!xiaomi_touch_data)
		return;

	/* _b580-A74①（站点1-容量）：blob 原件对 length 无源码级上界——6.6 blob 仅靠 FORTIFY 的
	 * __memcpy_chk 运行时兜底（判据 = blob add_common_data_to_buf_common 0x7a0/0x7c4：
	 * memcpy 前 cmp x22(=length*4), #0x401；越界走 0x8e4 __warn_printk/fortify_panic 路径）。
	 * 6.18 pahole 实测 common_data_t.data_buf = CMD_DATA_BUF_SIZE(256) 个 s32（sizeof=1032），
	 * 而无界 data_len 的来源是 xiaomi_touch_mode ioctl(SET_CMD_FOR_THP / 尾推) 与
	 * store_touch_thp_ic_cmd 文本解析（皆用户值）⇒ 越界可横穿 common_data_buf[10] 之后的
	 * common_data_buf_lock(10408)/poll_data(10456)/event_wq(10464)/suspend_work(10480)…
	 * 【安全偏离】此处按目标容量夹取（blob 原为 FORTIFY 告警/panic）：合法路径
	 * 0 <= length <= 256 语义完全不变。 */
	if (length < 0)
		length = 0;
	else if (length > CMD_DATA_BUF_SIZE) {
		LOG_ERROR("common data length %d overflow, clamp to %d", length, CMD_DATA_BUF_SIZE);
		length = CMD_DATA_BUF_SIZE;
	}

	LOG_DEBUG("add touch id %d common mode: %d to buffer:%d", touch_id, mode, atomic_read(&xiaomi_touch_data->common_data_buf_index));
	mutex_lock(&xiaomi_touch_data->common_data_buf_lock);
	common_data = &xiaomi_touch_data->common_data_buf[atomic_read(&xiaomi_touch_data->common_data_buf_index)];
	common_data->touch_id = touch_id;
	common_data->cmd = cmd;
	common_data->mode = mode;
	common_data->data_len = length;
	memcpy(common_data->data_buf, data, length * sizeof(s32));

	atomic_inc(&xiaomi_touch_data->common_data_buf_index);
	if (atomic_read(&xiaomi_touch_data->common_data_buf_index) >= COMMON_DATA_BUF_SIZE)
		atomic_set(&xiaomi_touch_data->common_data_buf_index, 0);

	mutex_unlock(&xiaomi_touch_data->common_data_buf_lock);
	notify_xiaomi_touch(xiaomi_touch_data, COMMON_DATA_NOTIFY);
}
EXPORT_SYMBOL(add_common_data_to_buf_common);


void *get_raw_data_base_common(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	void *base = NULL;

	if (!xiaomi_touch_data)
		return NULL;

	if (!xiaomi_touch_data->frame_data_mmap_base) {
		LOG_ERROR("touch id %d copy data failed, xiaomi_touch_data %p, base %p",
			touch_id, xiaomi_touch_data, xiaomi_touch_data->frame_data_mmap_base);
		return NULL;
	}

	base = xiaomi_touch_data->frame_data_mmap_base +
		atomic_read(&xiaomi_touch_data->frame_data_buf_index) * xiaomi_touch_data->frame_data_size;
	return base;
}
EXPORT_SYMBOL(get_raw_data_base_common);

void notify_raw_data_update_common(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);

	if (!xiaomi_touch_data)
		return;

	atomic_inc(&xiaomi_touch_data->frame_data_buf_index);
	if (atomic_read(&xiaomi_touch_data->frame_data_buf_index) >= xiaomi_touch_data->frame_data_buf_size)
		atomic_set(&xiaomi_touch_data->frame_data_buf_index, 0);


	notify_xiaomi_touch(xiaomi_touch_data, FRAME_DATA_NOTIFY);
}
EXPORT_SYMBOL(notify_raw_data_update_common);

int get_bms_temp_common(void);
EXPORT_SYMBOL(get_bms_temp_common);

int get_bms_temp_common(void)
{
	struct power_supply *battery;
	union power_supply_propval prop;
	int ret;

	/* _b580-A74①（站点3-a）：blob 0xc08 只查 "bms" 一个名字（串面 0x1cdc="bms"），
	 * 失败串 "can't find bms battery"（0x2c9e）；树侧原为 donor popsicle 的
	 * "battery"→"bms" 二级回退 + 串 "can't find bms and battery"（blob 无此串）。 */
	battery = power_supply_get_by_name("bms");
	if (!battery) {
		LOG_INFO("can't find bms battery\n");
		return -INVAILD_TEMPERATURE;
	}

	ret = power_supply_get_property(battery, POWER_SUPPLY_PROP_TEMP, &prop);
	if (ret) {
		LOG_INFO("can't read battery temp\n");
		return -INVAILD_TEMPERATURE;
	}

	return prop.intval;
}

/*
 * Temperature distribution strategy:
 *   a. Only launch when screen is on
 *   b. Update temperature for firmware at the moment the screen lights up,
 *     and then, update temperature for firmware every 2 degrees of subsequent temperature change
 *
 * Notes:
 *   VIRTUAL-SENSOR, Case temperature fitting, is a parameter that can well reflect the overall
 * temperature change of the mobile phone in the range of 25℃ ~ 53℃. It can be obtained through
 * monitoring node /sys/class/thermal/thermal_message/board_sensor_temp or through program
 * usb_get_property(USB_PROP_BOARD_TEMP, &board_temp).
 *   However, temperature changes in low-temperature scenarios require more attention for goodix.
 * And VIRTUAL-SENSOR cannot guarantee the detection accuracy below 25℃.
 *   Therefore, the monitoring object is changed to battery temperature.
 */
static int xiaomi_touch_temp_thread_func(void *data)
{
	int temp_n = 0;
	static int last_temp = 1000;
	int cur_temp0, cur_temp = 0;
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id_for_temperature);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(0);

	LOG_INFO("enter");
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param ||
			!xiaomi_touch_driver_param->hardware_operation.set_thermal_temp)
		return -1;

	while (!kthread_should_stop()) {
		/* _b581-XT③：blob 0x1874-0x18c4 的 wait 条件只有一处 32 位原子读
		 * （ldr w8,[x21,#0x2a20] ×2 处 = temp_detect_ready），无 ready[1] 项；
		 * 树侧原为 donor 的 ready[0]||ready[1]。rodin 为单面板（TOUCH_ID=0，
		 * enable_temperature_detection_func 也只被 touch_id=0 分支触达），
		 * 按 blob 收口为只测 ready[0]。 */
		wait_event_interruptible(xiaomi_touch_data->temp_detect_wait_queue,
				atomic_read(&xiaomi_touch_data->temp_detect_ready));
			cur_temp0 = get_bms_temp_common();
			cur_temp = (cur_temp0 + 5) / 10; // Rounding, in degrees Celsius
			/* _b580-A74①（站点3-b/c）：blob 0x1960-0x19d4 = 单面板形态——外层只判
			 * driver_param[0].set_thermal_temp 非空（0x1960 ldr x8,[x25,#0x228] +
			 * 0x1964 cbz x8 → 整块跳过；6.18 pahole = driver_param+0x288，树侧实测同）；
			 * 内层判有效温度 |temp0| < 1000 与 2℃ 温差；set_thermal_temp → common-data
			 * 推送 → last_temp 更新三件同块（blob 0x1990 / 0x19b0 / 0x19c8）。
			 * blob 体内无 ready[] 门、无 panel1 分支（树侧原为 donor 的 ready[0] 门 +
			 * driver_param_1 双面板块），此处按 blob 收口；wait_event 条件仍保留
			 * ready[0]||ready[1]（blob 只测 ready[0]，见报告“保留偏差点”）。 */
			if (xiaomi_touch_driver_param->hardware_operation.set_thermal_temp) {
				if (abs(cur_temp0) < INVAILD_TEMPERATURE &&
					abs(cur_temp - last_temp) >= TEMPERATURE_CHAGNE_VALUE) {
					xiaomi_touch_driver_param->hardware_operation.set_thermal_temp(cur_temp, false);
					/* blob 0x19b0-0x19c4：add_common_data_to_buf_common(0, SET_CUR_VALUE,
					 * 1094 = DATA_MODE_1000+94 = DATA_MODE_156, 1, &cur_temp)
					 * （blob 形参 w0=0/w1=0/w2=#0x446/w3=1/x4=&cur_temp@[x29,#-0xc]）。 */
					add_common_data_to_buf_common(0, SET_CUR_VALUE, DATA_MODE_1000 + 94, 1, &cur_temp);
					last_temp = cur_temp;
				}
			}

			/*
			 * The update rules for battery temperature are as follows:
			 * a. updates every 5s when charging; updates every 30s when not charging
			 * b. if the system enters sleep mode when screen-off, the temperature will not be updated until the system is awakened.
			 */
			if (xiaomi_touch.charging_status)
				temp_n = 5;
			else
				temp_n = 10; // 30;

			LOG_DEBUG("cur_temp0:%d, last_temp:%d, sleep %dms", cur_temp0, last_temp, temp_n * 1000);
			msleep(temp_n * 1000);
	}
	LOG_INFO("exit");

	return 0;
}

void enable_temperature_detection_func(s8 touch_id, bool is_resume);
EXPORT_SYMBOL(enable_temperature_detection_func);

void enable_temperature_detection_func(s8 touch_id, bool is_resume)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id_for_temperature);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);

	LOG_VERBOSE("enter");
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param ||
			!xiaomi_touch_driver_param->hardware_operation.set_thermal_temp)
		return;

	if (is_resume) {
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 1);
		/* _b581-XT③：blob 该两处串为 MI_TP_D（LOG_DEBUG），树侧原为 LOG_INFO。 */
		LOG_DEBUG("start detect temperature");
	} else {
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 0);
		LOG_DEBUG("stop detect temperature");
	}

	wake_up_interruptible(&xiaomi_touch_data->temp_detect_wait_queue);
}

	/* _b581-XT④a：原 donor 的 set_thermal_temp_force() 已删除——blob symtab/调用面
	 * 均无该符号（blob 全模块 0 调用者；resume_work 体内亦无 power_supply_get_by_name
	 * 内联），树侧仅被 resume_work 调用（本次按 blob 一并删除）。 */

int register_touch_panel_common(struct device *dev, s8 touch_id, hardware_param_t *hardware_param, hardware_operation_t *hardware_operation)
{
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
	xiaomi_touch_data_t *xiaomi_touch_data = NULL;
	u32 alloc_size = 0;

	LOG_INFO("enter");
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("touch id error, please check it!");
		return -1;
	}

	if (xiaomi_touch.panel_register_mask & (1 << touch_id)) {
		LOG_ERROR("touch id %d has register, stop register again!", touch_id);
		return -1;
	}

	/* init panel driver param */
	if (hardware_param) {
		xiaomi_touch_driver_param = &xiaomi_touch.xiaomi_touch_driver_param[touch_id];
		xiaomi_touch_driver_param->touch_id = touch_id;
		memcpy(&xiaomi_touch_driver_param->hardware_param, hardware_param, sizeof(hardware_param_t));
	} else {
		LOG_ERROR("param is null");
		return -1;
	}
	if (hardware_operation)
		memcpy(&xiaomi_touch_driver_param->hardware_operation, hardware_operation, sizeof(hardware_operation_t));

	/* create proc node */
	xiaomi_touch_create_proc(xiaomi_touch_driver_param);

	/* init xiaomi touch driver param */
	xiaomi_touch_data = &xiaomi_touch.xiaomi_touch_data[touch_id];
	memset(xiaomi_touch_data, 0, sizeof(xiaomi_touch_data_t));
	xiaomi_touch_data->touch_id = touch_id;
	spin_lock_init(&xiaomi_touch_data->private_data_lock);
	mutex_init(&xiaomi_touch_data->common_data_buf_lock);
	xiaomi_touch_data->frame_data_size = hardware_param->frame_data_page_size * PAGE_SIZE;
	xiaomi_touch_data->frame_data_buf_size = hardware_param->frame_data_buf_size;
	xiaomi_touch_data->raw_data_size = hardware_param->raw_data_page_size * PAGE_SIZE;
	xiaomi_touch_data->raw_data_buf_size = hardware_param->raw_data_buf_size;

	/* alloc mmap memory */
	xiaomi_touch_data->frame_data_mmap_phy_base = 0;
	/* _b583-XT2③：blob 0x1064 `str w9(=1), [x28, #0x2a08]` —— x28 = 元素-0x10（同函数
	 * 0x1034 `str w4,[x28,#0x18]`=frame_data_size、0x1054 `str xzr,[x28,#0x28]!`=
	 * frame_data_mmap_phy_base 两个定标点）⇒ 落点 = 元素+0x29F8 = disp_nb.priority
	 * （disp_nb @元素+0x29E8 由 work 相对存点 xiaomi_register_panel_notifier_work
	 * 0x2378 `str x8,[x19,#0x90]!`（x19 = work = 元素+0x2958）锁定，notifier_block
	 * .priority 位于 +0x10）。值 1、4B 宽；mi_disp_notifier.c 不读 priority
	 * ⇒ 无行为影响，纯布局保真（coordinator/A3 的"+0x29F0 dev"读法即差该 0x10 偏置）。
	 * 注：树侧原占该 4B 的 donor 成员 panel_register_retry 已删（见 xiaomi_touch.h）。 */
	xiaomi_touch_data->disp_nb.priority = 1;
	alloc_size = xiaomi_touch_data->frame_data_size * xiaomi_touch_data->frame_data_buf_size;
	LOG_DEBUG("alloc size = %d, frame data size %d, frame data page size %d, frame data buf size %d",
		alloc_size, xiaomi_touch_data->frame_data_size,
		hardware_param->frame_data_page_size, hardware_param->frame_data_buf_size);
	if (alloc_size) {
		xiaomi_touch_data->frame_data_mmap_base = kzalloc_retry(alloc_size, 3);
		if (!xiaomi_touch_data->frame_data_mmap_base) {
			LOG_ERROR("touch id %d alloc frame data memory failed!", touch_id);
			return -1;
		}
		LOG_DEBUG("frame data base %p, size %u", xiaomi_touch_data->frame_data_mmap_base, alloc_size);
		xiaomi_touch_data->frame_data_mmap_phy_base = virt_to_phys(xiaomi_touch_data->frame_data_mmap_base);
	}

	xiaomi_touch_data->raw_data_mmap_phy_base = 0;
	alloc_size = xiaomi_touch_data->raw_data_size * xiaomi_touch_data->raw_data_buf_size;

	LOG_DEBUG("alloc size = %d, raw data size %d, raw data page size %d, raw data buf size %d",
		alloc_size, xiaomi_touch_data->raw_data_size,
		hardware_param->raw_data_page_size, hardware_param->raw_data_buf_size);
	if (alloc_size) {
		xiaomi_touch_data->raw_data_mmap_base = kzalloc_retry(alloc_size, 3);
		if (!xiaomi_touch_data->raw_data_mmap_base) {
			LOG_ERROR("touch id %d alloc raw data memory failed!", touch_id);
			return -1;
		}
		LOG_DEBUG("raw data base %p, size %u", xiaomi_touch_data->raw_data_mmap_base, alloc_size);
		xiaomi_touch_data->raw_data_mmap_phy_base = virt_to_phys(xiaomi_touch_data->raw_data_mmap_base);
	}

	xiaomi_touch_data->event_wq = alloc_workqueue("xiaomi-touch-event-queue",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!xiaomi_touch_data->event_wq) {
		LOG_ERROR("ERROR: Cannot create work thread");
		return -1;
	}

	/* alloc poll data memory */
	xiaomi_touch_data->poll_data = kzalloc_retry(sizeof(htc_ic_polldata_t), 3);
	if (!xiaomi_touch_data->poll_data) {
		/* _b581-XT③：blob 串 = "alloc poll data memory failed!"（0x3733）。 */
		LOG_ERROR("alloc poll data memory failed!");
		return -1;
	}

	/* init list */
	INIT_LIST_HEAD(&xiaomi_touch_data->private_data_list);

	/* print hardware param*/
	if (hardware_operation && hardware_param) {

		if (hardware_operation->ic_get_lockdown_info) {
			hardware_operation->ic_get_lockdown_info(hardware_param->lockdown_info);
		}
		LOG_INFO("x_resolution=%d, y_resolution=%d, rx_num=%d, tx_num=%d, "
			"super_resolution_factor=%d, "
			"frame_data_page_size=%d, frame_data_buf_size=%d, "
			"raw_data_page_size=%d, raw_data_buf_size=%d, "
			"config_file_name=%s, driver_version=%s, fw_version=%s, "
			"lockdown={0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}",
			hardware_param->x_resolution, hardware_param->y_resolution, hardware_param->rx_num, hardware_param->tx_num,
			hardware_param->super_resolution_factor,
			hardware_param->frame_data_page_size, hardware_param->frame_data_buf_size,
			hardware_param->raw_data_page_size, hardware_param->raw_data_buf_size,
			hardware_param->config_file_name, hardware_param->driver_version, hardware_param->fw_version,
			hardware_param->lockdown_info[0], hardware_param->lockdown_info[1], hardware_param->lockdown_info[2], hardware_param->lockdown_info[3],
			hardware_param->lockdown_info[4], hardware_param->lockdown_info[5], hardware_param->lockdown_info[6], hardware_param->lockdown_info[7]);
	}
	/* init touch mode */
	xiaomi_touch_init_touch_mode(touch_id, dev);

	/* complete register */
	xiaomi_touch.panel_register_mask |= (1 << touch_id);
	LOG_INFO("current panel_register_mask is %d", xiaomi_touch.panel_register_mask);
	/* create a thread for temp detect */
	if (hardware_operation && hardware_operation->set_thermal_temp) {
		/* The temperature detection function is enabled by default when machine startup */
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 1);
		if (xiaomi_touch_temp_thread == NULL) {
			LOG_INFO("startup temperature detect thread");
			xiaomi_touch_temp_thread = kthread_create(xiaomi_touch_temp_thread_func, NULL, "xiaomi_touch_temp_thread");
			if (IS_ERR(xiaomi_touch_temp_thread)) {
				LOG_ERROR("Failed to create xiaomi_touch_temp_thread");
				goto err_out;
			}
			touch_id_for_temperature = touch_id;
			init_waitqueue_head(&xiaomi_touch_data->temp_detect_wait_queue);
			wake_up_process(xiaomi_touch_temp_thread);
		}
	}
	return 0;
err_out:
	if(!IS_ERR(xiaomi_touch_temp_thread)) {
		kthread_stop(xiaomi_touch_temp_thread);
	}
	return 0;
}
EXPORT_SYMBOL(register_touch_panel_common);

void unregister_touch_panel_common(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = NULL;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = NULL;
	if (!(xiaomi_touch.panel_register_mask & (1 << touch_id))) {
		/* _b581-XT③（纠错）：blob 0x1817 该直检串确为 "touch id %d didn't register,
		 * break unregister!"（blob 串表内存在；"panel in touch id %d hasn't register"
		 * 是 get_xiaomi_touch_data/_driver_param 内联副本的串，非本处）。 */
		LOG_ERROR("touch id %d didn't register, break unregister!", touch_id);
		return;
	}

	/* free mmap memory */
	xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data) {
		kzalloc_free(xiaomi_touch_data->frame_data_mmap_base);
		xiaomi_touch_data->frame_data_mmap_base = NULL;
		xiaomi_touch_data->frame_data_mmap_phy_base = 0;

		kzalloc_free(xiaomi_touch_data->raw_data_mmap_base);
		xiaomi_touch_data->raw_data_mmap_base = NULL;
		xiaomi_touch_data->raw_data_mmap_phy_base = 0;
	}

	/* remove proc node */
	xiaomi_touch_remove_proc(touch_id);

	/* clear driver param */
	xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	memset(&xiaomi_touch_driver_param->hardware_param, 0, sizeof(hardware_param_t));
	memset(&xiaomi_touch_driver_param->hardware_operation, 0, sizeof(hardware_operation_t));

	/*free poll data memory*/
	if (xiaomi_touch_data) {
		kzalloc_free(xiaomi_touch_data->poll_data);
		xiaomi_touch_data->poll_data = NULL;
	}

	/* complete unregister */
	xiaomi_touch.panel_register_mask &= ~(1 << touch_id);
	LOG_INFO("current panel_register_mask is %d", xiaomi_touch.panel_register_mask);
	/* free temp detect thread */
	if(xiaomi_touch_temp_thread != NULL) {
		/* _b581-XT③：blob 0x1dbc 只有一处 4 字节清零 str wzr,[x23,#0x2a20]
		 * = temp_detect_ready（无 ready[1] 槽；ready[1] 为 donor 残留）。 */
		atomic_set(&xiaomi_touch_data->temp_detect_ready, 0);
		// kthread_stop(xiaomi_touch_temp_thread); /* Optimize restart time */
		LOG_INFO("stop detect temperature");
	}
}
EXPORT_SYMBOL(unregister_touch_panel_common);

#if defined(CONFIG_DRM)
#ifdef TOUCH_MULTI_PANEL_NOTIFIER_SUPPORT
static enum suspend_state panel_status[MAX_TOUCH_PANEL_COUNT];
static void save_panel_notifier_status(long touch_id, enum suspend_state panel_status[], enum suspend_state status)
{
	if (IS_TOUCH_ID_INVALID(touch_id)) {
		LOG_ERROR("Invalid data. touch_id %ld", touch_id);
		return;
	}
	panel_status[touch_id] = status;
}
#endif

static void xiaomi_touch_resume_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, resume_work);
	s8 touch_id = xiaomi_touch_data->touch_id;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
#ifdef TOUCH_THP_SUPPORT
	int value = 0;
#endif

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param)
		return;
	if (!xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is resume, stop resume", touch_id);
		return;
	}
	/* _b581-XT④a：blob 0x25f4-0x282c 体内无 XIAOMI_TOUCH_UTC_PRINT
	 * （blob 全模块 ktime_get_real_ts64 只出现在 last_touch_events_collect_common/
	 * xiaomitouch_input_event；无 time64_to_tm 调用点）、无 enable_temperature_detection_func
	 * 调用（blob 该符号仅导出给 IC 侧）、无 set_thermal_temp_force（blob symtab 无此符号）。
	 * 树侧三处为 donor 附加，按 blob 删除。 */
	if (xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend) {
		xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend(true, xiaomi_get_gesture_type_common(touch_id));
	}
#ifdef TOUCH_MULTI_PANEL_NOTIFIER_SUPPORT
	/* save all panel status */
	save_panel_notifier_status(touch_id, panel_status, XIAOMI_TOUCH_RESUME);
	/* send all panel status to ic if needed */
	for (int i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		if (touch_id == i)
			continue;
		xiaomi_touch_driver_param_t *handler = get_xiaomi_touch_driver_param(i);
		if (handler && handler->hardware_operation.set_panel_notifier_status) {
			handler->hardware_operation.set_panel_notifier_status(panel_status);
		}
	}
#endif
#ifdef TOUCH_THP_SUPPORT
	add_common_data_to_buf_common(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
	add_common_data_to_buf_common(touch_id, SET_CUR_VALUE, DATA_MODE_72, 1, &xiaomi_touch.charging_status);
#endif
		/*reset charge state*/
		if (xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state) {
			xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state(xiaomi_touch.charging_status);
		}
	xiaomi_touch_data->is_suspend = false;
	/* other resume to do */

}

static void xiaomi_touch_suspend_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, suspend_work);
	s8 touch_id = xiaomi_touch_data->touch_id;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
#ifdef TOUCH_THP_SUPPORT
	int value = 1;
#endif

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param)
		return;
	if (xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is suspend, stop suspend", touch_id);
		return;
	}
	/* _b581-XT④a：blob suspend_work 无 XIAOMI_TOUCH_UTC_PRINT / 无
	 * enable_temperature_detection_func（同 resume_work，见上注）。 */
	if (xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend) {
		xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend(false, xiaomi_get_gesture_type_common(touch_id));
	}

	//enable fod under lock screen interact scene
	if (xiaomi_touch_driver_param->hardware_operation.get_tddi_status &&
			xiaomi_touch_driver_param->hardware_operation.get_tddi_status() &&
			xiaomi_touch_driver_param->hardware_operation.display_suspend_ready) {
		xiaomi_touch_driver_param->hardware_operation.display_suspend_ready();
	}
#ifdef TOUCH_MULTI_PANEL_NOTIFIER_SUPPORT
	/* save all panel status */
	save_panel_notifier_status(touch_id, panel_status, XIAOMI_TOUCH_SUSPEND);
	/* send all panel status to ic if needed */
	for (int i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		if (touch_id == i)
			continue;
		xiaomi_touch_driver_param_t *handler = get_xiaomi_touch_driver_param(i);
		if (handler && handler->hardware_operation.set_panel_notifier_status) {
			handler->hardware_operation.set_panel_notifier_status(panel_status);
		}
	}
#endif
#ifdef TOUCH_THP_SUPPORT
	add_common_data_to_buf_common(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
#endif
	xiaomi_touch_data->is_suspend = true;
	/* other suspend to do */

}

/* blob 同为全模块零调用；保留结构，__maybe_unused 压制孤儿告警 */
static __maybe_unused void xiaomi_touch_suspend_tddi(s8 touch_id)
{
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(touch_id);
	int value = 1;

	LOG_INFO("touch id %d enter", touch_id);
	if (!xiaomi_touch_data || !xiaomi_touch_driver_param)
		return;
	if (xiaomi_touch_data->is_suspend) {
		LOG_ERROR("touch id %d is suspend, stop suspend", touch_id);
		return;
	}
	if (!xiaomi_touch_driver_param->hardware_operation.get_tddi_status) {
		return;
	}
	XIAOMI_TOUCH_UTC_PRINT("");
	if (xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend) {
		xiaomi_touch_driver_param->hardware_operation.ic_resume_suspend(false, xiaomi_get_gesture_type_common(touch_id));
	}
	enable_temperature_detection_func(touch_id,false);
#ifdef TOUCH_THP_SUPPORT
		LOG_INFO("suspend to thp");
		add_common_data_to_buf_common(touch_id, SET_CUR_VALUE, DATA_MODE_27, 1, &value);
#endif
	xiaomi_touch_data->is_suspend = true;
	/* other suspend to do */
}

/* _b581-XT④b：blob @0x4834 xiaomi_drm_panel_notifier_callback 还原
 *   d = container_of(nb, xiaomi_touch_data_t, disp_nb); touch_id = d->touch_id(s8)
 *   if (!v || !((struct mi_disp_notifier *)v)->data || IS_TOUCH_ID_INVALID(touch_id))
 *           return NOTIFY_DONE;                       (blob 0x4858-0x4884)
 *   code = *(int *)evt->data;                          (blob 0x4888 ldr w21,[x8])
 *   LOG_INFO("notifier tp event:%lu, code:%d.", action, code)   (blob 0x48d8, 行号 639)
 *   action==2(EARLY)：code>5 或 code∉{1,2,5} → return；
 *           LOG_INFO("touchpanel suspend by %s", code==5 ? "blank" : "doze")
 *                                                     (blob 0x4900-0x4980, 行号 641)
 *   action==1(DPMS)：code!=0 → return；LOG_INFO("touchpanel resume")   (行号 644)
 *   其它 action → return；schedule_resume_suspend_work_common(touch_id, action != 2)
 *                                                     (blob 0x4928-0x4934)
 * 树侧原为 donor 形态：action/state 解引用 + 串 "action:%lu, state:%d"，
 * 且 EARLY 分支无 code 掩码、无 doze/blank 文案。 */
static int xiaomi_drm_panel_notifier_callback(struct notifier_block *nb,
		unsigned long action, void *v)
{
	xiaomi_touch_data_t *xiaomi_touch_data =
		container_of(nb, xiaomi_touch_data_t, disp_nb);
	s8 touch_id = xiaomi_touch_data->touch_id;
	struct mi_disp_notifier *evt = (struct mi_disp_notifier *)v;
	int code = -1;

	if (!v || !evt->data || IS_TOUCH_ID_INVALID(touch_id))
		return NOTIFY_DONE;

	code = *(int *)evt->data;
	LOG_INFO("notifier tp event:%lu, code:%d.", action, code);

	if (action == MI_DISP_DPMS_EARLY_EVENT) {
		if (code > MI_DISP_DPMS_POWERDOWN ||
				!((1 << code) & ((1 << MI_DISP_DPMS_LP1) |
						(1 << MI_DISP_DPMS_LP2) |
						(1 << MI_DISP_DPMS_POWERDOWN))))
			return NOTIFY_DONE;
		LOG_INFO("touchpanel suspend by %s",
				code == MI_DISP_DPMS_POWERDOWN ? "blank" : "doze");
	} else if (action == MI_DISP_DPMS_EVENT) {
		if (code != MI_DISP_DPMS_ON)
			return NOTIFY_DONE;
		LOG_INFO("touchpanel resume");
	} else {
		return NOTIFY_DONE;
	}

	schedule_resume_suspend_work_common(touch_id, action != MI_DISP_DPMS_EARLY_EVENT);
	return NOTIFY_DONE;
}

static void xiaomi_register_panel_notifier_work(struct work_struct *work)
{
	xiaomi_touch_data_t *xiaomi_touch_data = container_of(work, xiaomi_touch_data_t, panel_notifier_register_work.work);
	static int check_count = 0;
	struct drm_panel *panel = NULL;
	struct device_node *node;
	int count = 0;
	int i = 0;
#if defined(TOUCH_PLATFORM_XRING)
	char *property_name = "dsi-panel";
#else
	char *property_name = "panel";
#endif

	LOG_INFO("Start register panel notifier");
	/* _b583-XT③：blob 0x21f8-0x22b0 —— Start 串（行663）之后、of_count_phandle_with_args
	 * 之前有一处 `ldr x8,[&xiaomi_touch]; cbz x8,<行679 "Invalid params">`（E 级）并
	 * return；此后 of_count/of_parse 两处 of_node 均取自**该全局 dev**（0x2204/0x2238：
	 * `ldr x8,[.bss+0x8]; ldr x0,[x8,#0x300]`），而非 per-panel dev。
	 * 判据：DTS 的 panel 属性只挂 xiaomi-touch 节点（xiaomi_rodin_mt6899_touch.dtsi:12
	 * `panel = <&rodin_42_02_0a_dsc_vdo &rodin_36_02_0b_dsc_vdo>`），IC(spi)节点无该属性
	 * ⇒ 原树侧用 xiaomi_touch_data->dev 会 count<1 恒走 "try again"→"not try"，
	 * 面板通知器永不注册（挂起/恢复事件全丢）。 */
	if (!xiaomi_touch.dev) {
		LOG_ERROR("Invalid params");
		return;
	}
	count = of_count_phandle_with_args(xiaomi_touch.dev->of_node, property_name, NULL);
	for (i = 0; i < count; i++) {
		node = of_parse_phandle(xiaomi_touch.dev->of_node, property_name, i);
		panel = of_drm_find_panel(node);
		if (!IS_ERR(panel)) {
			break;
		} else {
			panel = NULL;
			of_node_put(node);
		}
	}

	if (!panel) {
		LOG_ERROR("Failed to register panel notifier, try again");
		if (check_count++ < 5)
			schedule_delayed_work(&xiaomi_touch_data->panel_notifier_register_work, msecs_to_jiffies(1250));
		else {
			LOG_ERROR("Failed to register panel notifier, not try");
		}
		return;
	}

	/* _b583-XT③：blob 0x22b8/0x22c8-0x22f4 —— 找到 panel 后、注册前，把 container_of
	 * 出来的指针与 &xiaomi_touch.xiaomi_touch_data[0]（.bss+0x18）/ [1]（.bss+0x2a90，
	 * 步长 0x2A78）两两比对（clang 展开的 2 元素 for 循环）；两者皆不等则打
	 * "can't not find this touch id!"（E 级，行713）并 return。树侧缺此校验（串面
	 * blob-only 命中即此）。 */
	if (xiaomi_touch_data != &xiaomi_touch.xiaomi_touch_data[0] &&
	    xiaomi_touch_data != &xiaomi_touch.xiaomi_touch_data[1]) {
		LOG_ERROR("can't not find this touch id!");
		return;
	}

	xiaomi_touch_data->disp_nb.notifier_call = xiaomi_drm_panel_notifier_callback;
	if (mi_disp_register_client(&xiaomi_touch_data->disp_nb)) {
		LOG_ERROR("Failed to register for panel events");
		return;
	}
	/* _b581-XT③：blob @0x2390 该函数末只有上面这条 E 级串，无
	 * "panel notifier registered (mi_disp), touch_id %ld"（树侧 donor 附加，删除；
	 * 连带删除仅供该串使用的 long touch_id 变量）。 */
}
#endif

void schedule_resume_suspend_work_common(s8 touch_id, bool resume_work)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	bool queue_work_result = false;
	if (!xiaomi_touch_data) {
		LOG_ERROR("xiaomi_touch_data is NULL!");
		return;
	}
	flush_workqueue(xiaomi_touch_data->event_wq);
	if (resume_work) {
		queue_work_result = queue_work(xiaomi_touch_data->event_wq, &xiaomi_touch_data->resume_work);
	} else {
		queue_work_result = queue_work(xiaomi_touch_data->event_wq, &xiaomi_touch_data->suspend_work);
	}

	if (!queue_work_result) {
		LOG_ERROR("queue %s work failed, retry", resume_work ? "resume" : "suspend");
	}
#endif
}
EXPORT_SYMBOL(schedule_resume_suspend_work_common);

/**
 * this function must be called after register_touch_panel_common()
*/
void xiaomi_register_panel_notifier_common(struct device *dev, s8 touch_id)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data == NULL)
		return;
	xiaomi_touch_data->dev = dev;
	INIT_DELAYED_WORK(&xiaomi_touch_data->panel_notifier_register_work, xiaomi_register_panel_notifier_work);
	INIT_WORK(&xiaomi_touch_data->suspend_work, xiaomi_touch_suspend_work);
	INIT_WORK(&xiaomi_touch_data->resume_work, xiaomi_touch_resume_work);
	schedule_delayed_work(&xiaomi_touch_data->panel_notifier_register_work, msecs_to_jiffies(0));
#endif
}
EXPORT_SYMBOL(xiaomi_register_panel_notifier_common);

void xiaomi_unregister_panel_notifier_common(struct device *dev, s8 touch_id)
{
#if defined(CONFIG_DRM)
	xiaomi_touch_data_t *xiaomi_touch_data = get_xiaomi_touch_data(touch_id);
	if (xiaomi_touch_data == NULL)
		return;
	cancel_delayed_work_sync(&xiaomi_touch_data->panel_notifier_register_work);
	cancel_work_sync(&xiaomi_touch_data->suspend_work);
	cancel_work_sync(&xiaomi_touch_data->resume_work);

	mi_disp_unregister_client(&xiaomi_touch_data->disp_nb);
#endif
}
EXPORT_SYMBOL(xiaomi_unregister_panel_notifier_common);

static int xiaomi_get_charging_status(void)
{
	struct power_supply *usb_psy;
	struct power_supply *dc_psy;
	union power_supply_propval val;
	int rc = 0;

	dc_psy = power_supply_get_by_name("wireless");
	if (dc_psy) {
		rc = power_supply_get_property(dc_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (rc < 0)
			LOG_ERROR("Couldn't get DC online status, rc=%d\n", rc);
		else if (val.intval == 1)
			return CHARGING | WIRELESS_CHARGING;
	}

	usb_psy = power_supply_get_by_name("usb");
	if (usb_psy) {
		rc = power_supply_get_property(usb_psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (rc < 0)
			LOG_ERROR("Couldn't get usb online status, rc=%d\n", rc);
		else if (val.intval == 1)
			return CHARGING | WIRED_CHARGING;
	}
	return NOT_CHARGING;
}

static void xiaomi_power_supply_work(struct work_struct *work)
{
	xiaomi_touch_t *xiaomi_touch = container_of(work, xiaomi_touch_t, power_supply_work);
	int charging_status;
	int i = 0;
	xiaomi_touch_driver_param_t *xiaomi_touch_driver_param;

	charging_status = xiaomi_get_charging_status();
	if (charging_status != xiaomi_touch->charging_status || xiaomi_touch->charging_status < 0) {
		xiaomi_touch->charging_status = charging_status;
	for (i = 0; i < MAX_TOUCH_PANEL_COUNT; i++) {
		xiaomi_touch_driver_param = get_xiaomi_touch_driver_param(i);
		if (!xiaomi_touch_driver_param)
			break;
#ifdef TOUCH_THP_SUPPORT
		add_common_data_to_buf_common(i, SET_CUR_VALUE, DATA_MODE_72, 1, &charging_status);
#endif
		if (xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state) {
			xiaomi_touch_driver_param->hardware_operation.ic_set_charge_state(charging_status);
		}
	}
	}
}

static int xiaomi_power_supply_notifier_callback(struct notifier_block *nb, unsigned long event, void *ptr)
{
	schedule_work(&xiaomi_touch.power_supply_work);
	return 0;
}

static void xiaomi_register_power_supply_event(xiaomi_touch_t *xiaomi_touch)
{
	int retval = 0;

	if (xiaomi_touch == NULL)
		return;
	xiaomi_touch->charging_status = -1;
	xiaomi_touch->power_supply_notifier.notifier_call = xiaomi_power_supply_notifier_callback;
	retval = power_supply_reg_notifier(&xiaomi_touch->power_supply_notifier);
	if (retval < 0) {
		LOG_ERROR("[Probe Failed] error:%d\n", retval);
		return;
	}
	INIT_WORK(&xiaomi_touch->power_supply_work, xiaomi_power_supply_work);
}

static void xiaomi_unregister_power_supply_event(void)
{
	cancel_work_sync(&xiaomi_touch.power_supply_work);
	power_supply_unreg_notifier(&xiaomi_touch.power_supply_notifier);
}

/* _b582-INTB：A-80① 死面——nfc_to_touch_event 在 blob 三 ko 中 nfc 串/符号 0 命中
 * （strings -a/nm 复核）；全树 0 调用者；6.6 ops 表（216B=27 槽，_b581_goodix 逐槽解码）
 * 无该槽位 ⇒ 整函数删除；结构体成员 set_nfc_to_touch_event 保留（值为 NULL，勿改结构体）。 */

static int xiaomi_touch_probe(struct platform_device *pdev)
{
	/* _b583-XT③：blob xiaomi_touch_probe 0x4abc `add x20,x0,#0x10`（=&pdev->dev）→
	 * 0x4b00 `cbz x20,<行1245 "Invalid touch device">`→`mov w0,#-0x13`(-ENODEV)；
	 * 0x4b04 `str x20,[x19]`（x19=&xiaomi_touch，即结构 +0x0 的 dev 成员）。
	 * 判据：该检查在 6.6 blob 为死检（&pdev->dev 恒非 0），但成员与写入是
	 * xiaomi_register_panel_notifier_work 的 of_node 来源（见 xiaomi_touch.h 锚点）
	 * ⇒ 必须落码，否则面板通知器解析失败。 */
	struct device *dev = &pdev->dev;

	LOG_ALWAYS("xiaomi_touch ver: %s", XIAOMI_TOUCH_VERSION);
#ifdef TOUCH_KNOCK_SUPPORT
	knock_node_init();
#endif
	memset(&xiaomi_touch, 0, sizeof(xiaomi_touch_t));
	if (!dev) {
		LOG_INFO("Invalid touch device");
		return -ENODEV;
	}
	xiaomi_touch.dev = dev;
	xiaomi_touch_sys_init();
	xiaomi_touch_operation_init(&xiaomi_touch);
	xiaomi_register_power_supply_event(&xiaomi_touch);
	netlink_init();
	xiaomi_touch_evdev_init(&xiaomi_touch);
	LOG_INFO("over");

	return 0;
}

static void xiaomi_touch_remove(struct platform_device *pdev)
{
	LOG_INFO("enter");
	xiaomi_touch_evdev_remove();
    netlink_exit();
	xiaomi_unregister_power_supply_event();
	xiaomi_touch_operation_remove();
	xiaomi_touch_sys_remove();
	memset(&xiaomi_touch, 0, sizeof(xiaomi_touch_t));
#ifdef TOUCH_KNOCK_SUPPORT
	knock_node_release();
#endif
	LOG_INFO("over");
}

#if defined(TOUCH_PLATFORM_XRING)
#include "touch_of_match_table.h"
#else
static const struct of_device_id xiaomi_touch_of_match[] = {
	{ .compatible = "xiaomi-touch", },
	{ },
};
#endif
static struct platform_driver xiaomi_touch_device_driver = {
	.probe		= xiaomi_touch_probe,
	.remove		= xiaomi_touch_remove,
	.driver		= {
		.name	= "xiaomi-touch",
		.of_match_table = of_match_ptr(xiaomi_touch_of_match),
	}
};

static int __init xiaomi_touch_init(void)
{
	return platform_driver_register(&xiaomi_touch_device_driver);
}

static void __exit xiaomi_touch_exit(void)
{
	platform_driver_unregister(&xiaomi_touch_device_driver);
}

MODULE_LICENSE("GPL");


/* blob def：xiaomi_touch_mievent_report_int_common(280B)/_str_common(252B)，平段导出。
 * 语义 = yoro dash 同代实现（cdev_tevent 五字段/四字段上报），blob 串面
 * "code:%d,fault_name:%s,panel_id:%d,vendor_name:%s,error_code:%ld" 逐字对上。 */
void xiaomi_touch_mievent_report_int_common(unsigned int code, int panel_id,
	const char *fault_name, const char *vendor_name, long error_code)
{
	struct misight_mievent *event;

	/* _b581-XT③：blob 该串带尾 \n（+0x1ec2 区，形参同） */
	printk(KERN_INFO "[MI_TP_I][%s:%d]: code:%d,fault_name:%s,panel_id:%d,vendor_name:%s,error_code:%ld\n",
	       __func__, __LINE__, code, fault_name, panel_id, vendor_name, error_code);
	event = cdev_tevent_alloc(code);
	if (!event) {
		/* _b581-XT③：blob 该串为 KERN_INFO(\x016) + 尾 \n（树侧原 KERN_ERR 无 \n）。 */
		printk(KERN_INFO "[MI_TP_E][%s:%d]: misight event is error\n", __func__, __LINE__);
		return;
	}
	cdev_tevent_add_int(event, "panel_id", panel_id);
	cdev_tevent_add_str(event, "fault_name", fault_name);
	cdev_tevent_add_str(event, "vendor_name", vendor_name);
	cdev_tevent_add_int(event, "error_code", error_code);
	cdev_tevent_write(event);
	cdev_tevent_destroy(event);
}
EXPORT_SYMBOL(xiaomi_touch_mievent_report_int_common);

void xiaomi_touch_mievent_report_str_common(unsigned int code, int panel_id,
	const char *fault_name, const char *vendor_name)
{
	struct misight_mievent *event;

	/* _b581-XT③：blob 该串带尾 \n */
	printk(KERN_INFO "[MI_TP_I][%s:%d]: code:%d,fault_name:%s,panel_id:%d,vendor_name:%s\n",
	       __func__, __LINE__, code, fault_name, panel_id, vendor_name);
	event = cdev_tevent_alloc(code);
	if (!event) {
		/* _b581-XT③：blob 该串为 KERN_INFO(\x016) + 尾 \n（树侧原 KERN_ERR 无 \n）。 */
		printk(KERN_INFO "[MI_TP_E][%s:%d]: misight event is error\n", __func__, __LINE__);
		return;
	}
	cdev_tevent_add_int(event, "panel_id", panel_id);
	cdev_tevent_add_str(event, "fault_name", fault_name);
	cdev_tevent_add_str(event, "vendor_name", vendor_name);
	cdev_tevent_write(event);
	cdev_tevent_destroy(event);
}
EXPORT_SYMBOL(xiaomi_touch_mievent_report_str_common);



vseq_module_init(xiaomi_touch_init);
module_exit(xiaomi_touch_exit);
