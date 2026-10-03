# SPDX-License-Identifier: GPL-2.0
#
# Makefile for mi memory info (rodin #166: 6.6 blob mi_memory.ko 内建重建)
#
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/vendor/

obj-y += mi-memory.o

mi-memory-y += memory_debugfs.o
mi-memory-y += ufs_info.o
mi-memory-y += dram_info.o
