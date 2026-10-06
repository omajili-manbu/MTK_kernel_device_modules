# SPDX-License-Identifier: GPL-2.0
# focaltech_touch_rodin =y 内建（b567）：骨架 = MiCode popsicle-w-oss
# q200/focaltech_3383（thp 同代异 IC），FT5672 表按 blob 对齐。
obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_RODIN) += focaltech_touch_rodin.o
focaltech_touch_rodin-y := focaltech_core.o focaltech_ex_fun.o focaltech_ex_mode.o \
	focaltech_gesture.o focaltech_esdcheck.o focaltech_point_report_check.o \
	focaltech_test/focaltech_test.o focaltech_test/focaltech_test_ini.o \
	focaltech_test/supported_ic/focaltech_test_ft5672.o \
	focaltech_flash/focaltech_upgrade_ft5008.o \
	focaltech_flash.o focaltech_spi.o focaltech_scp_tp.o

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/focaltech_rodin -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/xiaomi_touch_rodin -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/mi_disp -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include \
	     -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/focaltech_rodin/focaltech_test \
	     -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/focaltech_rodin/focaltech_test/supported_ic \
	     -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/focaltech_rodin/focaltech_flash
