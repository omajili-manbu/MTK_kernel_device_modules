# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat
# xhci trace/dbg family is served by the kernel copy (b246 handback)
ccflags-y += -I$(srctree)/drivers/usb/host
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/usb/mtu3

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/usb20/

ifeq ($(CONFIG_MTK_USB_BOOST), y)
ifeq ($(CONFIG_USB_MTK_HDRC), y)
obj-$(CONFIG_MTK_USB_BOOST) += musb_boost_common.o usb_boost_plat.o
else
obj-$(CONFIG_MTK_USB_BOOST) += usb_boost_common.o usb_boost_plat.o
endif
else
obj-$(CONFIG_MTK_USB_BOOST) += usb_boost.o

usb_boost-y := usb_boost_common.o usb_boost_plat.o

obj-$(CONFIG_MTK_USB_BOOST) += musb_boost.o

musb_boost-y := musb_boost_common.o usb_boost_plat.o
endif
