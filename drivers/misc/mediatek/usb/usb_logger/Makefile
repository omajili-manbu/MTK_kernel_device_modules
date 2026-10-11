# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/usb/mtu3
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/usb_xhci
# b246: xhci trace family handed back to the kernel tree
ccflags-y += -I$(srctree)/drivers/usb/host
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/usb_offload

# define_trace.h needs to know how to find our header
CFLAGS_u_logger_trace.o	:= -I$(srctree)/$(src)

obj-$(CONFIG_MTK_USB_LOGGER)	+= usb_logger.o

usb_logger-y	:= u_logger.o
usb_logger-y	+= u_logger_test_helper.o

ifneq ($(CONFIG_TRACING),)
usb_logger-y	+= u_logger_trace.o
endif
