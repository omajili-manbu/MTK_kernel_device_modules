# SPDX-License-Identifier: GPL-2.0
#
# Makefile for USB Host Controller Drivers
#

# debugfs/dbg/trace/dbgcap/ext-caps are handed back to the kernel copy;
# vendor xhci.h keeps the __LINUX_XHCI_HCD_H guard so quoted includes of the
# deleted headers resolve to drivers/usb/host via the -I below.
ccflags-y += -I$(srctree)/drivers/usb/host
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/usb_offload
ccflags-y += -I$(srctree)/sound/usb

xhci-mtk-hcd-v2-y := xhci.o xhci-mem.o
xhci-mtk-hcd-v2-y += xhci-ring.o xhci-hub.o
xhci-mtk-hcd-v2-y += xhci-mtk.o xhci-mtk-sch.o
xhci-mtk-hcd-v2-y += quirks.o
xhci-mtk-hcd-v2-y += xhci-sideband.o

obj-$(CONFIG_DEVICE_MODULES_USB_XHCI_MTK) += xhci-mtk-hcd-v2.o
