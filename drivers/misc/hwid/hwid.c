// SPDX-License-Identifier: GPL-2.0
/*
 * HwId Module driver for mi driver acquire some hwid build info,
 * which is only used for xiaomi corporation internally.
 *
 * Copyright (C) 2022 XiaoMi, Inc.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/vseq.h>

#include "hwid.h"

#define HW_MAJOR_VERSION_SHIFT      16
#define HW_MINOR_VERSION_SHIFT      0
#define HW_COUNTRY_VERSION_SHIFT    20
#define HW_BUILD_VERSION_SHIFT      16
#define HW_MAJOR_VERSION_MASK       0xFFFF0000
#define HW_MINOR_VERSION_MASK       0x0000FFFF
#define HW_COUNTRY_VERSION_MASK     0xFFF00000
#define HW_BUILD_VERSION_MASK       0x000F0000

#define TAG "[xiaomi hwid]:"

static unsigned int hwid_value;
static unsigned int project;
static char *project_name;
static unsigned int build_adc;
static unsigned int project_adc;
static struct kobject *hwid_kobj;

module_param(hwid_value, uint, 0444);
MODULE_PARM_DESC(hwid_value, "xiaomi hwid value correspondingly different build");
module_param(project, uint, 0444);
MODULE_PARM_DESC(project, "xiaomi project serial num predefine");
module_param(project_name, charp, 0444);
MODULE_PARM_DESC(project_name, "xiaomi project name predefine");
module_param(build_adc, uint, 0444);
MODULE_PARM_DESC(build_adc, "xiaomi adc value of build resistance");
module_param(project_adc, uint, 0444);
MODULE_PARM_DESC(project_adc, "xiaomi adc value of project resistance");

uint32_t get_hw_version_platform(void)
{
	return project;
}
EXPORT_SYMBOL(get_hw_version_platform);

uint32_t get_hw_id_value(void)
{
	return hwid_value;
}
EXPORT_SYMBOL(get_hw_id_value);

uint32_t get_hw_country_version(void)
{
	return (hwid_value & HW_COUNTRY_VERSION_MASK) >> HW_COUNTRY_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_country_version);

uint32_t get_hw_version_major(void)
{
	return (hwid_value & HW_MAJOR_VERSION_MASK) >> HW_MAJOR_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_major);

uint32_t get_hw_version_minor(void)
{
	return (hwid_value & HW_MINOR_VERSION_MASK) >> HW_MINOR_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_minor);

uint32_t get_hw_version_build(void)
{
	return (hwid_value & HW_BUILD_VERSION_MASK) >> HW_BUILD_VERSION_SHIFT;
}
EXPORT_SYMBOL(get_hw_version_build);

char *product_name_get(void)
{
	return project_name;
}
EXPORT_SYMBOL(product_name_get);

static ssize_t hwid_project_show(struct kobject *kobj,
				 struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", project);
}

static ssize_t hwid_value_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n", hwid_value);
}

static ssize_t hwid_project_adc_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", project_adc);
}

static ssize_t hwid_build_adc_show(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", build_adc);
}

static struct kobj_attribute hwid_project_attr =
	__ATTR(hwid_project, 0444, hwid_project_show, NULL);
static struct kobj_attribute hwid_value_attr =
	__ATTR(hwid_value, 0444, hwid_value_show, NULL);
static struct kobj_attribute hwid_project_adc_attr =
	__ATTR(hwid_project_adc, 0444, hwid_project_adc_show, NULL);
static struct kobj_attribute hwid_build_adc_attr =
	__ATTR(hwid_build_adc, 0444, hwid_build_adc_show, NULL);

static struct attribute *hwid_attrs[] = {
	&hwid_project_attr.attr,
	&hwid_value_attr.attr,
	&hwid_project_adc_attr.attr,
	&hwid_build_adc_attr.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = hwid_attrs,
};

static int __init hwid_init(void)
{
	int ret;

	hwid_kobj = kobject_create_and_add("hwid", NULL);
	if (!hwid_kobj) {
		pr_err("hwid: hwid module init failed\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(hwid_kobj, &attr_group);
	if (ret) {
		pr_err("hwid: sysfs register failed\n");
		kobject_del(hwid_kobj);
		return ret;
	}

	pr_info("%s product_name=%s hwid_value=%d hw_country=%d hwv_major=%d hwv_minor=%d hwv_build=%d\n",
		TAG, project_name, hwid_value,
		(hwid_value & HW_COUNTRY_VERSION_MASK) >> HW_COUNTRY_VERSION_SHIFT,
		(hwid_value & HW_MAJOR_VERSION_MASK) >> HW_MAJOR_VERSION_SHIFT,
		(hwid_value & HW_MINOR_VERSION_MASK) >> HW_MINOR_VERSION_SHIFT,
		(hwid_value & HW_BUILD_VERSION_MASK) >> HW_BUILD_VERSION_SHIFT);

	kobject_del(hwid_kobj);

	return 0;
}

static void __exit hwid_exit(void)
{
	if (hwid_kobj) {
		sysfs_remove_group(hwid_kobj, &attr_group);
		kobject_del(hwid_kobj);
		hwid_kobj = NULL;
	}

	pr_info("hwid: hwid module exit success\n");
}

vseq_module_init(hwid_init);
module_exit(hwid_exit);
MODULE_AUTHOR("weixiaotian1@xiaomi.com");
MODULE_DESCRIPTION("Hwid Module Driver for Xiaomi Corporation");
MODULE_LICENSE("GPL v2");
