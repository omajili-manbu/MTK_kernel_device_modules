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

# MTK Platform Support
# rodin: 批4-5 按目录直连挂载，父级 subdir-ccflags 不生效，自补 slbc_ops.h 的 -I
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc

# adsp include path
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/audio_ipi/include
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/adsp/include
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/base/power/include
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/audio_dsp

# platform include path
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/common

# object code
snd-soc-audiodsp-common-objs := audio_buf.o mtk-dsp-dai.o mtk-dsp-pcm.o mtk-dsp-common.o mtk-dsp-platform-driver.o mtk-dsp-mem-control.o

#ifeq ($(CONFIG_DEVICE_MODULES_VHOST_ADSP),y)
snd-soc-audiodsp-common-objs += mtk-dsp-platform-driver-auto.o
#endif

obj-$(CONFIG_SND_SOC_MTK_AUDIO_DSP) += snd-soc-audiodsp-common.o

# Offload support
obj-$(CONFIG_SND_SOC_MTK_OFFLOAD) += mtk-soc-offload-common.o
