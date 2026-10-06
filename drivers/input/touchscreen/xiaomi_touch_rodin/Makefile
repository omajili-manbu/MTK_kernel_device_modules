# SPDX-License-Identifier: GPL-2.0
# xiaomi_touch_rodin =y 内建（b567）：骨架 = MiCode/vendor_xiaomi_proprietary_touch-driver
# popsicle-w-oss xiaomi/（结构代），_common 改名代对齐 rodin blob（tools/_b567_touch）。
obj-$(CONFIG_TOUCHSCREEN_XIAOMI_TOUCH_RODIN) += xiaomi_touch_rodin.o
xiaomi_touch_rodin-y := xiaomi_touch_core.o xiaomi_touch_device.o xiaomi_touch_evdev.o \
	xiaomi_touch_knock_data.o xiaomi_touch_mode.o xiaomi_touch_operations.o \
	xiaomi_touch_proc.o xiaomi_touch_sys.o xiaomi_touch_boost.o

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/xiaomi_touch_rodin -DTOUCH_FOD_SUPPORT \
	     -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/mi_disp
