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

ifeq ($(CONFIG_MTK_ECCCI_DRIVER), y)
CCCI_OBJ=y
else ifeq ($(CONFIG_MTK_ECCCI_DRIVER), m)
CCCI_OBJ=m
else
CCCI_OBJ=n
endif

ifneq ($(CCCI_OBJ), n)
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/inc/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/ccmni/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/conn_md/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/rv/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/memory-amms
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/hif/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mdpm/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pbm/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/fsm/

# rodin 批4-3b（=y 复用 =m 的模块切分）：
# 设备按 =m 出货 6 个独立 .ko（ccci_md_all/ccci_auxadc/ccci_ccif/ccci_dpmaif/
# ccci_fsm_scp/ccci_util_lib）。原 =y 聚合布局（ccci_all + 下潜 port//udc/）既与设备
# blob 名不匹配（跳过表空转），又整段丢掉 dpmaif 对象（调制解调器数据面缺失）。
# CCCI_KMODULE_ENABLE 在 =y 下也定义（设备真值）：设备 blob 带 D ccci_debug_enable，
# 且 ccci_fsm_scp.ko 有 U __platform_driver_register —— 该宏内的 fsm_scp platform
# driver 若不编，SCP 侧 modem 状态同步整段消失。
ccflags-y += -DCCCI_KMODULE_ENABLE
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/udc/udc_lib_inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/inc/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/ccmni/

ifneq ($(CONFIG_DEVICE_MODULES_MEDIATEK_MT6577_AUXADC), n)
obj-$(CCCI_OBJ) += ccci_auxadc.o
endif

obj-$(CCCI_OBJ) += ccci_md_all.o
ccci_md_all-$(CCCI_OBJ) := ccci_core.o ccci_bm.o ccci_modem.o \
		udc/udc.o \
		hif/ccci_hif.o

# port start
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/port/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/udc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/udc/udc_lib_inc
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/conn_md/include/

ccci_md_all-$(CCCI_OBJ) += port/port_cfg.o \
		port/port_char.o \
		port/port_ctlmsg.o \
		port/port_udc.o \
		port/port_ipc.o \
		port/port_poller.o \
		port/port_proxy.o \
		port/port_rpc.o \
		port/port_smem.o \
		port/port_sysmsg.o \
		port/ccci_swtp.o \
		port/port_kernel_interface.o
# port end
# fsm start
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/fsm/
ccci_md_all-$(CCCI_OBJ) += fsm/ccci_fsm.o \
			fsm/ccci_fsm_ioctl.o \
			fsm/ccci_fsm_monitor.o \
			fsm/ccci_fsm_poller.o \
			fsm/ccci_fsm_ee.o \
			fsm/ccci_fsm_sys.o \
			fsm/mdee_dumper_v1.o \
			fsm/mdee_dumper_v2.o \
			fsm/mdee_dumper_v3.o \
			fsm/mdee_dumper_v5.o \
			fsm/mdee_dumper_v6.o \
			fsm/modem_sys1.o \
			fsm/md_sys1_platform.o \
			fsm/ap_md_reg_dump.o \
			fsm/ap_md_reg_dump_gen93.o \
			fsm/ap_md_reg_dump_gen95.o \
			fsm/ap_md_reg_dump_gen97.o \
			fsm/ap_md_mem.o

# fsm end

# plat start
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pmic/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek
# security objects
MASP_CORE_DRIVER_DIR := $(DEVICE_MODULES_PATH)/drivers/misc/mediatek/drivers/masp

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/fsm
obj-$(CCCI_OBJ)	+= fsm/

# ccci_ccif.ko, ccci_dpmaif.ko, ccci_cldma.ko
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/eccci/hif
obj-$(CCCI_OBJ)	+= hif/




# rodin 批4-3b: 原 =y 聚合布局（ccci_all/ccci_fsm_all/ccci_hif_all/ccci_port/udc）
# 已删除；=y 与 =m 共用上面同一份对象清单（模块名与设备出货逐一对齐）。

endif
