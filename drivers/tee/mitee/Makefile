# SPDX-License-Identifier: GPL-2.0

$(info "Welcome to MiTee...")
ccflags-y += -I$(src)/tee
# rodin: FFA v1.1 提供者 = devmod trusted_mem/ffa_v11（vendor 6.6 ABI fork，
# struct ffa_device 比内核 6.18 头少 properties 字段）⇒ 全树统一用它自己的
# arm_ffa.h（gud/tmem_ffa 同款 ccflags -I 先例）。
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/trusted_mem/
obj-$(CONFIG_MITEE_SUPPORT) += mitee.o
mitee-y :=	\
	core.o \
	call.o \
	rpc.o \
	supp.o \
	ffa_abi.o \
	device.o \
	bench.o \
	mitee_task.o \
	mitee_memlog.o \
	mitee_smc_notify.o \
	dynamic_mem.o \
	tee/tee_core.o \
	tee/tee_shm.o \
	tee/tee_shm_pool.o
