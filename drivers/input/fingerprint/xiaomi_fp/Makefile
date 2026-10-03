# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the fingerprint device (rodin #166: 6.6 blob xiaomi_spi_tee.ko 内建重建)
#
obj-y += xiaomi_spi_tee.o

xiaomi_spi_tee-y := fp_driver.o fp_netlink.o fp_platform.o

ccflags-y += -DCONFIG_FP_MTK_PLATFORM
