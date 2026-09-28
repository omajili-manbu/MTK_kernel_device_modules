#
# Makefile for the kernel et7480 type-c switch device drivers.
#
obj-$(CONFIG_USB_SWITCH_ET7480) += et7480.o # rodin: 批4-5 无条件 obj-m 加门（=y 内建）

MTK_CUSTOM_PATH := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek
subdir-ccflags-y += \
	-I$(MTK_CUSTOM_PATH)/typec
subdir-ccflags-y += \
	-I$(MTK_CUSTOM_PATH)/power_throttling
subdir-ccflags-y += \
    -I$(MTK_CUSTOM_PATH)/pbm

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/power/supply/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/codecs/
