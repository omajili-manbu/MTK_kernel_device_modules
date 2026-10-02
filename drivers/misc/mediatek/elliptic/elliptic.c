// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2014-2020, Elliptic Laboratories AS. All rights reserved.
 * Elliptic Labs Linux driver
 */

/* #define DEBUG */
#include <linux/module.h>
#include <linux/vseq.h>
#include <linux/kernel.h>
#include <linux/version.h>
/*  includes the file structure, that is, file open read close */
#include <linux/fs.h>

/* include the character device, makes cdev avilable */
#include <linux/cdev.h>
#include <linux/semaphore.h>

/* includes copy_user vice versa */
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/device.h>

#include <linux/pm_wakeup.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/kobject.h>
#include <linux/notifier.h>
#include <linux/soc/mediatek/mtk_tinysys_ipi.h>

#include "elliptic_sysfs.h"
#include "elliptic_device.h"
#include "elliptic_data_io.h"
#include "elliptic_mixer_controls.h"
#include "scp.h"

// #include <hwmsensor.h>
// #include <SCP_sensorHub.h>
// #include "scp_helper.h"

/* Alternative mechanism to load calibration data.
 * Read calibration data during driver initialization
 * and send message to the DSP
 *
 * #define ELLIPTIC_LOAD_CALIBRATION_DATA_FROM_FILESYSTEM 1
 */
#ifdef ELLIPTIC_LOAD_CALIBRATION_DATA_FROM_FILESYSTEM
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/uaccess.h>
#endif

static struct elliptic_device *elliptic_devices;

/* Global variable for the device class*/
struct class *elliptic_class;

typedef uint32_t el_fifo_size_t;

/* Major number provided by the kernel*/
static dev_t elliptic_major;

static struct wakeup_source *wake_source;


void elliptic_data_cancel(struct elliptic_data *elliptic_data)
{
	atomic_set(&elliptic_data->abort_io, 1);
	wake_up_interruptible(&elliptic_data->fifo_isr_not_empty);
}

void elliptic_data_reset_debug_counters(struct elliptic_data *elliptic_data)
{
	elliptic_data->isr_fifo_discard = 0;
}

void elliptic_data_print_debug_counters(struct elliptic_data *elliptic_data)
{
	if (elliptic_data->isr_fifo_discard > 0) {
		pr_debug("[ELUS] isr fifo discarded %u frames",
			elliptic_data->isr_fifo_discard);
	}

	if (elliptic_data->userspace_read_total !=
		elliptic_data->isr_write_total) {
		pr_info("[ELUS] user space reads / isr writes : %u / %u",
			elliptic_data->userspace_read_total,
			elliptic_data->isr_write_total);
	}

	pr_info("[ELUS] total isr fifo discarded frame count : %u",
		elliptic_data->isr_fifo_discard_total);
}

void elliptic_data_update_debug_counters(struct elliptic_data
	*elliptic_data)
{
	elliptic_data->isr_fifo_discard_total +=
		elliptic_data->isr_fifo_discard;
}


/* spin lock for isr must be held prior to calling */
static void elliptic_data_flush_isr_fifo(struct elliptic_data
	*elliptic_data)
{
	kfifo_reset(&elliptic_data->fifo_isr);
}

/* spin lock for isr must be held prior to calling */
static void elliptic_data_isr_fifo_pop(struct elliptic_data
	*elliptic_data, size_t size)
{
	unsigned int fifo_result;
	static uint8_t temp_buffer[ELLIPTIC_MSG_BUF_SIZE];

	if (size > ELLIPTIC_MSG_BUF_SIZE)
		pr_debug("[ELUS] pop size %zu too large", size);

	fifo_result = kfifo_out(&elliptic_data->fifo_isr,
		temp_buffer, size);

	if (size != fifo_result)
		pr_debug("[ELUS] failed to pop element");
}


int elliptic_notify_gain_change_msg(int component_id, int gaindb)
{
	int32_t msg[3] = {ESCPT_COMPONENT_GAIN_CHANGE, component_id, gaindb};

	return elliptic_data_write(
		ELLIPTIC_ULTRASOUND_SET_PARAMS,
		(const char *)msg, sizeof(msg));
}

/* inode refers to the actual file on disk */
static int device_open(struct inode *inode, struct file *filp)
{
	unsigned int major;
	unsigned int minor;
	struct elliptic_device *dev;
	struct elliptic_data *elliptic_data;

	major = imajor(inode);
	minor = iminor(inode);

	if (major != elliptic_major || minor < 0
		|| minor >= ELLIPTIC_NUM_DEVICES) {
		pr_warn("[ELUS] no device found with minor=%d and major=%d",
			major, minor);
		return -ENODEV; /* No such device */
	}

	dev = NULL;
	dev = &elliptic_devices[minor];
	filp->private_data = dev;

	if (inode->i_cdev != &dev->cdev) {
		pr_warn("[ELUS] dev pointer mismatch");
		return -ENODEV; /* No such device */
	}

	if (down_interruptible(&dev->sem) != 0) {
		pr_err("[ELUS] the device has been opened, unable to open lock");
		return -EINVAL;
	}

	elliptic_data = &dev->el_data;
	spin_lock(&elliptic_data->fifo_isr_spinlock);
	elliptic_data_flush_isr_fifo(elliptic_data);
	spin_unlock(&elliptic_data->fifo_isr_spinlock);

	atomic_set(&elliptic_data->abort_io, 0);
	elliptic_data_reset_debug_counters(elliptic_data);

	pr_info("[ELUS] Opened device elliptic%u", minor);
	if (minor == 1)
		elliptic_debug_io_open();
	dev->opened = 1;
	return 0;
}


int elliptic_data_initialize(struct elliptic_data
	*elliptic_data, size_t queue_size,
	unsigned int wakeup_timeout, int id)
{
	int is_power_of_two;

	is_power_of_two = (queue_size != 0) && !(queue_size & (queue_size - 1));

	if (is_power_of_two != 1) {
		pr_err("[ELUS] non power of 2 fifo size");
		return -EINVAL;
	}

	if (kfifo_alloc(&elliptic_data->fifo_isr,
		queue_size, GFP_KERNEL) != 0) {
		pr_err("[ELUS] failed to allocate fifo isr");
		return -EINVAL;
	}

	atomic_set(&elliptic_data->abort_io, 0);
	spin_lock_init(&elliptic_data->fifo_isr_spinlock);

	elliptic_data->wakeup_timeout = wakeup_timeout;

	mutex_init(&elliptic_data->user_buffer_lock);
	init_waitqueue_head(&elliptic_data->fifo_isr_not_empty);

	return 0;
}

int elliptic_data_cleanup(struct elliptic_data *elliptic_data)
{
	/* rodin b513 #113: 本函数从不加锁，原 spin_unlock 未配对 ——
	 * 内核以 "initcall ... returned with preemption imbalance"
	 * 自证（本轮 pstore 1.001360 实证）。kfifo_free 对未分配
	 * 的 fifo 安全。 */
	kfifo_free(&elliptic_data->fifo_isr);
	return 0;
}

size_t elliptic_data_pop(struct elliptic_data
	*elliptic_data, char __user *user_buffer, size_t buffer_size)
{
	int result;
	unsigned long num_copied;
	unsigned int fifo_result;
	unsigned long flags;

	if (buffer_size < ELLIPTIC_MSG_BUF_SIZE) {
		pr_debug("[ELUS] buffer_size : %lu smaller than %lu",
			buffer_size, (size_t)ELLIPTIC_MSG_BUF_SIZE);
		return 0;
	}

	result = wait_event_interruptible(elliptic_data->fifo_isr_not_empty,
		(kfifo_is_empty(&elliptic_data->fifo_isr) == 0)
		|| (atomic_read(&elliptic_data->abort_io) == 1));

	if (atomic_read(&elliptic_data->abort_io) == 1) {
		atomic_set(&elliptic_data->abort_io, 0);
		pr_debug("[ELUS] pop cancelled");
		return 0;
	}

	if (result == 0) {
		spin_lock_irqsave(&elliptic_data->fifo_isr_spinlock, flags);

		fifo_result = kfifo_out(&elliptic_data->fifo_isr,
			elliptic_data->isr_swap_buffer, ELLIPTIC_MSG_BUF_SIZE);

		spin_unlock_irqrestore(&elliptic_data->fifo_isr_spinlock,
			flags);

		if (fifo_result == 0) {
			pr_debug("[ELUS] failed to copy: fifo isr -> swap buffer %u",
				fifo_result);
			return 0;
		}
		mutex_lock(&elliptic_data->user_buffer_lock);

		num_copied = copy_to_user(user_buffer,
						elliptic_data->isr_swap_buffer,
						ELLIPTIC_MSG_BUF_SIZE);

		mutex_unlock(&elliptic_data->user_buffer_lock);

		if (num_copied != 0) {
			pr_debug("[ELUS] failed copy to user");
			return 0;
		}
		++elliptic_data->userspace_read_total;
	} else {
		if (-ERESTARTSYS == result)
			pr_info("[ELUS] wait interrupted");
		else
			pr_debug("[ELUS] wait error = %d", result);

		return 0;
	}

	return (size_t)ELLIPTIC_MSG_BUF_SIZE;
}



/* push data to specific device or all devices */
int elliptic_data_push(int deviceid,
	const char *buffer,
	size_t buffer_size,
	enum elliptic_data_push data_source)
{
	size_t available_space;
	size_t space_required;
	size_t zeros_to_pad;
//	unsigned int copied_from_user;
//	int copy_from_user_result;
	int err;
	int i;
	int i_max;

	unsigned long flags;
	struct elliptic_device *device;
	struct elliptic_data *elliptic_data;
	unsigned int fifo_result;
	static uint8_t zero_pad_buffer[ELLIPTIC_MSG_BUF_SIZE];

	err = 0;
	fifo_result = 0;
//	copied_from_user = 0;
//	copy_from_user_result = 0;
	if (buffer_size > ELLIPTIC_MSG_BUF_SIZE){
		pr_err("[ELUS]buffer size[%lu] is larger then %d\n", buffer_size, ELLIPTIC_MSG_BUF_SIZE);
		return -EINVAL;
	}

	zeros_to_pad = ELLIPTIC_MSG_BUF_SIZE - buffer_size;

	i = 0;
	i_max = ELLIPTIC_NUM_DEVICES;

	if (deviceid != ELLIPTIC_ALL_DEVICES) {
		/* Copy to specific device */
		i = deviceid;
		i_max = i + 1;
	}

	for (; i < i_max; ++i) {
		device = &elliptic_devices[i];
		elliptic_data = &device->el_data;

		if ((!device->opened))
			continue;

		available_space = kfifo_avail(&elliptic_data->fifo_isr);
		space_required = ELLIPTIC_MSG_BUF_SIZE;

		spin_lock_irqsave(&elliptic_data->fifo_isr_spinlock, flags);

		if (available_space < space_required) {

			++elliptic_data->isr_fifo_discard;
			pr_err("[ELUS]available_space < space_required discard:%d\n", elliptic_data->isr_fifo_discard);
			elliptic_data_isr_fifo_pop(elliptic_data,
				ELLIPTIC_MSG_BUF_SIZE);
		}

		if (data_source == ELLIPTIC_DATA_PUSH_FROM_KERNEL) {
			fifo_result = kfifo_in(&elliptic_data->fifo_isr,
				buffer, buffer_size);

			if (fifo_result == 0) {
				spin_unlock_irqrestore(
					&elliptic_data->fifo_isr_spinlock,
					flags);
				continue;
			}
		}
        /*
        else if (data_source == ELLIPTIC_DATA_PUSH_FROM_USERSPACE) {
			copy_from_user_result = kfifo_from_user(
				&elliptic_data->fifo_isr, buffer,
				buffer_size, &copied_from_user);

			if (-EFAULT == copy_from_user_result) {
				spin_unlock_irqrestore(
					&elliptic_data->fifo_isr_spinlock,
					flags);
				continue;
			}
		}
        */


		if (zeros_to_pad > 0) {
			fifo_result = kfifo_in(
				&elliptic_data->fifo_isr, zero_pad_buffer,
				zeros_to_pad);

			if (fifo_result == 0) {
				elliptic_data_isr_fifo_pop(elliptic_data,
					buffer_size);

				spin_unlock_irqrestore(
					&elliptic_data->fifo_isr_spinlock,
					flags);

				++elliptic_data->isr_fifo_discard;
				continue;
			}
		}


		++elliptic_data->isr_write_total;
		spin_unlock_irqrestore(
			&elliptic_data->fifo_isr_spinlock, flags);
		wake_up_interruptible(&elliptic_data->fifo_isr_not_empty);
		__pm_wakeup_event(wake_source, elliptic_data->wakeup_timeout);
	}

	return err;
}

int elliptic_open_port(int portid)
{
	return elliptic_io_open_port(portid);
}

int elliptic_close_port(int portid)
{
	return elliptic_io_close_port(portid);
}


int32_t elliptic_data_write(uint32_t message_id,
	const char *data, size_t data_size)
{
	int32_t err_dsp;
	/* int32_t err_us; */

	err_dsp = 0;
	err_dsp = elliptic_data_io_write(message_id, data, data_size);
	if (err_dsp)
		pr_debug("[ELUS] Failed write to DSP");
	return err_dsp;
}



/**
 *
 * @return Number of bytes read.
 */
static ssize_t device_read(struct file *fp, char __user *buff,
	size_t length, loff_t *ppos)
{
	ssize_t bytes_read = 0;
	struct elliptic_device *elliptic_device;
	struct elliptic_data *elliptic_data;

	elliptic_device = (struct elliptic_device *)fp->private_data;
	elliptic_data = (struct elliptic_data *)&elliptic_device->el_data;

	bytes_read = elliptic_data_pop(elliptic_data, buff, length);

	return bytes_read;
}

#ifndef ELLIPTIC_IPI_AP_TO_SCP_DATA_SIZE
#define ELLIPTIC_IPI_AP_TO_SCP_DATA_SIZE 40
#endif

/**
 *
 * @return number of bytes actually written
 */
static ssize_t device_write(struct file *fp, const char *buff,
	size_t length, loff_t *ppos)
{
	ssize_t ret_val;
	char buf[ELLIPTIC_IPI_AP_TO_SCP_DATA_SIZE] = {0};

	ret_val = 0;
	pr_err("Macle device_write called\n");
	if(length > ELLIPTIC_IPI_AP_TO_SCP_DATA_SIZE){
		pr_err("[ELUS] Data[size=%lu] is  too large \n", length);
		return  -ENOMEM;
	}
	if ((buff != NULL) && (length != 0)){
		if (copy_from_user(buf, buff, length)){
			pr_err("[ELUS] copy_from_user failed\n");
			return -EFAULT;
		}

		ret_val = elliptic_data_io_write(ELLIPTIC_ULTRASOUND_SET_PARAMS,
				buf, length);
	}
	return ret_val >= 0 ? (ssize_t)length : 0;
}


static long device_ioctl(struct file *fp, unsigned int number,
	unsigned long param)
{
	struct elliptic_device *device;
	struct elliptic_data *elliptic_data;

	device = (struct elliptic_device *)(fp->private_data);
	elliptic_data = &device->el_data;

	switch (number) {
	case IOCTL_ELLIPTIC_DATA_IO_CANCEL:
		pr_debug("[ELUS] IOCTL_ELLIPTIC_CANCEL_READ %ld",
			param);
		elliptic_data_cancel(elliptic_data);
		break;

	default:
		pr_warn("[ELUS] UNKNOWN IOCTL number=%d", number);
		break;
	}

	return 0;
}


static unsigned int device_poll(struct file *file,
	struct poll_table_struct *poll_table)
{
	unsigned int mask;

	struct elliptic_device *device;
	struct elliptic_data *elliptic_data;

	mask = 0;
	device = (struct elliptic_device *)file->private_data;
	elliptic_data = (struct elliptic_data *)&device->el_data;

	poll_wait(file, &elliptic_data->fifo_isr_not_empty, poll_table);

	if (!kfifo_is_empty(&elliptic_data->fifo_isr))
		mask = POLLIN | POLLRDNORM;

	return mask;
}


static int device_close(struct inode *inode, struct file *filp)
{
	struct elliptic_device *device;
	struct elliptic_data *elliptic_data;
	unsigned int minor;

	device = filp->private_data;
	elliptic_data = &device->el_data;
	minor = iminor(inode);
	if (device == NULL) {
		pr_err("[ELUS] device not found");
		return -ENODEV;
	}

	if (minor == 1)
		elliptic_debug_io_close();
	device->opened = 0;
	elliptic_data_update_debug_counters(elliptic_data);
	elliptic_data_print_debug_counters(elliptic_data);
	elliptic_data_cancel(elliptic_data);
	up(&device->sem);

	pr_info("[ELUS] Closed device elliptic%u", minor);
	return 0;
}

/* defines the file operations provided by the driver */
static const struct file_operations elliptic_fops = {
	.owner = THIS_MODULE, /* prevents unloading when operations are in use*/
	.open = device_open,  /*to open the device*/
	.write = device_write, /*to write to the device*/
	.read = device_read, /*to read the device*/
	.poll = device_poll,
	.unlocked_ioctl = device_ioctl, /* IOCTL calls */
	.release = device_close, /*to close the device*/
};


static int elliptic_device_initialize(struct elliptic_device
	*elliptic_device, int minor, struct class *class)
{
	int err;
	dev_t device_number;
	struct device *device;

	WARN_ON(elliptic_device == NULL || class == NULL);

	err = 0;
	device = NULL;
	device_number = MKDEV(elliptic_major, minor);
	/* Memory is to be allocated when the device is opened the first time */
	sema_init(&elliptic_device->sem, 1);
	cdev_init(&elliptic_device->cdev, &elliptic_fops);
	elliptic_device->cdev.owner = THIS_MODULE;

	err = cdev_add(&elliptic_device->cdev, device_number, 1);

	if (err) {
		pr_err("[ELUS] error %d while trying to add %s%d",
			err, ELLIPTIC_DEVICENAME, minor);
		return err;
	}

	device = device_create(class, NULL, device_number,
		NULL, ELLIPTIC_DEVICENAME "%d", minor);

	if (IS_ERR(device)) {
		err = PTR_ERR(device);
		pr_err("[ELUS] error %d while trying to create %s%d",
			err, ELLIPTIC_DEVICENAME, minor);
		cdev_del(&elliptic_device->cdev);
		return err;
	}

	if (err) {
		pr_err("[ELUS] failed device initialize");
		return err;
	}

	return 0;
}

static void elliptic_device_cleanup(struct elliptic_device *dev, int minor,
	struct class *class)

{
	WARN_ON(dev == NULL || class == NULL);
	device_destroy(class, MKDEV(elliptic_major, minor));
	cdev_del(&dev->cdev);
	up(&dev->sem);
}

static void elliptic_driver_cleanup(int devices_to_destroy)
{
	int i;

	if (elliptic_devices) {
		elliptic_data_io_cleanup();

		for (i = 0; i < devices_to_destroy; ++i) {
			elliptic_data_cleanup(&elliptic_devices[i].el_data);
			elliptic_device_cleanup(
				&elliptic_devices[i], i, elliptic_class);
		}

		kfree(elliptic_devices);
	}

	/* rodin b513 #113: class_create 失败时 elliptic_class 是 ERR_PTR
	 * 而不是 NULL，原判非空会把 ERR_PTR 交给 class_destroy。 */
	if (!IS_ERR_OR_NULL(elliptic_class))
		class_destroy(elliptic_class);

	unregister_chrdev_region(
		MKDEV(elliptic_major, 0), ELLIPTIC_NUM_DEVICES);
}



#ifdef ELLIPTIC_LOAD_CALIBRATION_DATA_FROM_FILESYSTEM

#define ELLIPTIC_CALIBRATION_MAX_DATA_SIZE                                     \
	(ELLIPTIC_CALIBRATION_V2_DATA_SIZE + ELLIPTIC_CALIBRATION_DATA_SIZE)
static unsigned char calibration_data[ELLIPTIC_CALIBRATION_MAX_DATA_SIZE];
static char *calibration_filename = "/persist/audio/elliptic_calibration";

/* function to load the calibration from a file (if possible) */
static size_t load_calibration_data(char *filename)
{
	size_t ret = 0;
	int fd;

	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);

	fd = sys_open(filename, O_RDONLY, 0);
	if (fd >= 0) {
		size_t bytes_read =
			sys_read(fd, calibration_data,
				 ELLIPTIC_CALIBRATION_MAX_DATA_SIZE);

		if (bytes_read == ELLIPTIC_CALIBRATION_DATA_SIZE ||
			bytes_read == ELLIPTIC_CALIBRATION_V2_DATA_SIZE) {
			ret = bytes_read;
		}
		sys_close(fd);
	}
	set_fs(old_fs);
	return ret;
}

static int32_t elliptic_send_calibration_to_engine(size_t calib_data_size)
{
	elliptic_set_calibration_data(calibration_data, calib_data_size);
	return elliptic_data_write(
		ELLIPTIC_ULTRASOUND_SET_PARAMS,
		(const char *)calibration_data, calib_data_size);
}

#endif


/* rodin b513 #113: 与 6.6 的模块装载语义等价 —— 6.6 上 scp.ko 先
 * 装载并注册 scp_ipidev（2.5796s），userspace 之后才装载
 * elliptic-ultrasound.ko（2.9386s）。6.18 内建化后 elliptic 的
 * vseq_device_initcall 早于 SCP 提供者的 vseq_device_initcall_sync（scp_init），
 * mtk_ipi_register() 因 !ipidev->ipi_inited 返回 IPI_DEV_ILLEGAL，
 * 于是错误路径释放主设备号、留下 /sys/dev/char 悬挂（#113 实证）。
 * 因此拆两段：SCP 未就绪时先不建任何资源（chrdev/class/device/
 * sysfs/io 全不建），等一次性就绪通知再一次性建立。
 * 注意：elliptic_setup() 会在 initmem 释放后执行，严禁 __init 归属。
 */
static bool elliptic_ready_done;

static int elliptic_setup(void)
{

	int err;
	int i;
	int devices_to_destroy;
	dev_t device_number;

	/* 幂等：就绪回调只应生效一次 */
	if (elliptic_ready_done)
		return 0;

	err = alloc_chrdev_region(&device_number, 0, ELLIPTIC_NUM_DEVICES,
		ELLIPTIC_DEVICENAME);

	devices_to_destroy = 0;

	if (err < 0) {
		pr_err("[ELUS] Failed to allocate cdev region");
		return err;
	}

	elliptic_major = MAJOR(device_number);
	elliptic_class = class_create("chardev");

	/* rodin b513 #113: class_create 失败返回 ERR_PTR，永不 NULL；
	 * 原判 NULL 会带着 ERR_PTR 继续建设备/class_destroy。 */
	if (IS_ERR(elliptic_class)) {
		pr_err("[ELUS] Class creation failed");
		goto fail;
	}

	err = elliptic_initialize_sysfs();

	if (err)
		goto fail;

	elliptic_devices = (struct elliptic_device *)
		kzalloc(sizeof(struct elliptic_device) * ELLIPTIC_NUM_DEVICES,
			GFP_KERNEL);

	if (elliptic_devices == NULL) {
		err = -ENOMEM;
		goto fail;
	}


	for (i = 0; i < ELLIPTIC_NUM_DEVICES; ++i) {
		err = elliptic_device_initialize(&elliptic_devices[i], i,
			elliptic_class);
		if (err) {
			/* rodin b513 #113: 原实现在此不写 err（保持 0），
			 * vseq_module_init 会假装成功。 */
			devices_to_destroy = i;
			goto fail;
		}

		err = elliptic_data_initialize(&elliptic_devices[i].el_data,
			ELLIPTIC_DATA_FIFO_SIZE, ELLIPTIC_WAKEUP_TIMEOUT, i);
		if (err) {
			/* rodin b513 #113: 该 device 的 device_create 已成功，
			 * 计数必须含它；否则 unregister_chrdev_region() 之前
			 * 漏 device_destroy -> /sys/dev/char/<maj>:<min> 悬挂，
			 * 被下一个 alloc_chrdev_region 的驱动抢到同一主号。 */
			devices_to_destroy = i + 1;
			goto fail;
		}
	}

	pr_debug("[ELUS] io initialize\n");
	err = elliptic_data_io_initialize();
	if (err) {
		/* rodin b513 #113: 此时全部 device 已建，必须全部销毁
		 * 之后才允许释放主设备号（原计数 0 ⇒ 一个都不销毁）。 */
		devices_to_destroy = ELLIPTIC_NUM_DEVICES;
		goto fail;
	}

	wake_source = wakeup_source_register(NULL, "elliptic_wake_source");

	if (!wake_source){
		pr_err("[ELUS] failed to register wake source\n");
		/* rodin b513 #113: 原为裸 return —— region/class/设备
		 * 全部泄漏；此时全部 device 已建，走统一清理。 */
		err = -ENOMEM;
		devices_to_destroy = ELLIPTIC_NUM_DEVICES;
		goto fail;
	}

#ifdef ELLIPTIC_LOAD_CALIBRATION_DATA_FROM_FILESYSTEM
	/* Code to send calibration to engine */
	{
		size_t calib_data_size =
			load_calibration_data(calibration_filename);
		if (calib_data_size > 0)
			elliptic_send_calibration_to_engine(calib_data_size);
	}
#endif
	return 0;

fail:
	/* rodin b513 #113: sysfs 也拆（幂等，见 elliptic_sysfs.c）。 */
	elliptic_cleanup_sysfs();
	elliptic_driver_cleanup(devices_to_destroy);
	return err;
}

/* rodin b513 #113: B1a 就绪回调。blocking notifier 回调内不注销
 * （会死锁），一次性语义由 elliptic_ready_done 保证；"是否已就绪"
 * 用公开字段 ipi_inited 自测（不改 struct 布局）。 */
static int elliptic_ipi_ready_cb(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	if (data != &scp_ipidev)
		return NOTIFY_DONE;
	if (elliptic_ready_done)
		return NOTIFY_DONE;
	if (elliptic_setup())
		return NOTIFY_DONE;

	elliptic_ready_done = true;
	return NOTIFY_OK;
}

static struct notifier_block elliptic_ipi_ready_nb = {
	.notifier_call = elliptic_ipi_ready_cb,
};

static int __init elliptic_driver_init(void)
{
	/* rodin b513 #113: SCP ipidev 未就绪就不建任何资源 */
	if (!scp_ipidev.ipi_inited) {
		pr_info("[ELUS] scp ipidev not ready, defer init\n");
		if (mtk_ipi_dev_register_ready_notifier(&elliptic_ipi_ready_nb))
			pr_err("[ELUS] register ready notifier failed\n");
		return 0;
	}

	if (elliptic_setup())
		return -ENODEV;

	elliptic_ready_done = true;
	return 0;
}


static void elliptic_driver_exit(void)
{
	wakeup_source_unregister(wake_source);

	elliptic_cleanup_sysfs();
	elliptic_driver_cleanup(ELLIPTIC_NUM_DEVICES);
}

MODULE_AUTHOR("Elliptic Labs");
MODULE_DESCRIPTION("Providing Interface to UPS data");
MODULE_LICENSE("GPL");

vseq_module_init(elliptic_driver_init);
module_exit(elliptic_driver_exit);
