#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdebug/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/smi/
obj-$(CONFIG_MTK_MMDVFS) += mtk-mmdvfs-ftrace.o mtk-mmdvfs-debug.o
# rodin batch4-4: mtk-mmdvfs-v3-start.o restored -- the device does ship
# vendor_dlkm/mtk-mmdvfs-v3-start.ko (14496 B); the b4 note was wrong.
# mtk-mmdvfs-ccu.o stays dropped: no device blob and it imports
# mtk_ccu_rproc_ipc_send from ccu remoteproc (batch 4-8).
obj-$(CONFIG_MTK_MMDVFS) += mtk-mmdvfs-v3-start.o

