#
# Copyright (C) 2016 MediaTek Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#


ifeq ($(CONFIG_MTK_ECCCI_DRIVER), m)
ccflags-y += -DCCCI_KMODULE_ENABLE

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/port/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/ccmni/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/platform
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/mt6873
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/include
#ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/

# ccci_ccif.ko

ccci_ccif-y := ccci_ringbuf.o ccci_hif_ccif.o
obj-$(CONFIG_MTK_ECCCI_DRIVER) += ccci_ccif.o

# ccci_dpmaif.ko

ccci_dpmaif-y := net_pool.o md_spd_dvfs_fn.o md_spd_dvfs_method.o net_speed.o \
	ccci_dpmaif_com.o ccci_dpmaif_bat.o ccci_dpmaif_debug.o ccci_dpmaif_resv_mem.o \
	ccci_dpmaif_drv_com.o ccci_dpmaif_drv_v1.o ccci_dpmaif_drv_v2.o ccci_dpmaif_drv_v3.o

ifeq ($(CONFIG_PAGE_POOL), y)
ccflags-y += -DRX_PAGE_POOL
ccci_dpmaif-y += ccci_dpmaif_page_pool.o
endif

obj-$(CONFIG_MTK_ECCCI_DRIVER) += ccci_dpmaif.o

# ccci_cldma.ko
ccci_cldma-y := ccci_cldma_plat.o ccci_hif_cldma.o
obj-$(CONFIG_MTK_ECCCI_DRIVER) += ccci_cldma.o

endif


ifeq ($(CONFIG_MTK_ECCCI_DRIVER), y)

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/ccmni/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/mt6873
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/mt6873
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/conn_md/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/mt6873
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/


# rodin 批4-3b: =y 复用 =m 的模块切分（ccci_ccif.o / ccci_dpmaif.o）。
# cldma 不编：设备三分区无任何 cldma blob，且 ccci_cldma_plat.c/ccci_hif_cldma.c 的
# 函数面在 6 个设备 blob 中零命中；ccci_hif.o 归 ccci_md_all（见 eccci/Makefile）。
# 增补 -I eccci/port/：=m 独有而 dpmaif 需要（ccci_dpmaif_debug.c → ccci_port.h）。
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/port/
ccflags-y += -DCCCI_KMODULE_ENABLE

ccci_ccif-y := ccci_ringbuf.o ccci_hif_ccif.o
obj-$(CONFIG_MTK_ECCCI_DRIVER) += ccci_ccif.o

ccci_dpmaif-y := net_pool.o md_spd_dvfs_fn.o md_spd_dvfs_method.o net_speed.o \
	ccci_dpmaif_com.o ccci_dpmaif_bat.o ccci_dpmaif_debug.o ccci_dpmaif_resv_mem.o \
	ccci_dpmaif_drv_com.o ccci_dpmaif_drv_v1.o ccci_dpmaif_drv_v2.o ccci_dpmaif_drv_v3.o

ifeq ($(CONFIG_PAGE_POOL), y)
ccflags-y += -DRX_PAGE_POOL
ccci_dpmaif-y += ccci_dpmaif_page_pool.o
endif

obj-$(CONFIG_MTK_ECCCI_DRIVER) += ccci_dpmaif.o

endif

