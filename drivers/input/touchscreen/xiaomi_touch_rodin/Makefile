# SPDX-License-Identifier: GPL-2.0
# xiaomi_touch_rodin =y 内建（b567）：骨架 = MiCode/vendor_xiaomi_proprietary_touch-driver
# popsicle-w-oss xiaomi/（结构代），_common 改名代对齐 rodin blob（tools/_b567_touch）。
obj-$(CONFIG_TOUCHSCREEN_XIAOMI_TOUCH_RODIN) += xiaomi_touch_rodin.o
# _b582-INPUT：xiaomi_touch_device.o 整件退出接线——该文件全部组成（register/unregister_
# xiaomi_input_dev 族、register/unregister_xiaomi_stylus_input_dev、report-point mmap 面
# report_finger_event/report_stylus_event/report_touch_event/get_report_point_info_phy_addr）
# 在 blob xiaomi_touch_rodin.ko 符号面/串面/ioctl 面均零命中，属树侧 donor EXTRA；
# fts 侧已按 blob 自建 input_dev（focaltech_core.c:fts_input_init），无残留调用者。
xiaomi_touch_rodin-y := xiaomi_touch_core.o xiaomi_touch_evdev.o \
	xiaomi_touch_knock_data.o xiaomi_touch_mode.o xiaomi_touch_operations.o \
	xiaomi_touch_proc.o xiaomi_touch_sys.o xiaomi_touch_boost.o

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/xiaomi_touch_rodin -DTOUCH_FOD_SUPPORT -DTOUCH_THP_SUPPORT \
	     -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/mi_disp
