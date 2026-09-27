#
# Copyright (C) 2018 MediaTek Inc.
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

ccflags-y += \
	-I$(srctree)/include/ \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/cpufreq_lite/

CFLAGS_powerhal_cpu_ctrl.o := -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/powerhal_cpu_ctrl

# rodin 4-3: 同上层拆门（该目录的唯一物件）
obj-$(CONFIG_MTK_POWERHAL_CPU_CTRL) += powerhal_cpu_ctrl.o
