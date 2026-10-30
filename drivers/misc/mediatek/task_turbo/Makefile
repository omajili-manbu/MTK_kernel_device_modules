# Copyright (C) 2019 MediaTek Inc.
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
#
ccflags-y += -I$(srctree)
ccflags-y += -I$(srctree)/$(src)              # needed for trace events
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sched/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/fstb
obj-$(CONFIG_MTK_TASK_TURBO) += task_turbo.o
# rodin 6.18（2026-10-31）：tt_vip 算法层重建（原 blob task_turbo_api_no_cfi.o_shipped
# 从未链接；hook 指针由此接线复活）。回滚 = 删两行并还原 task_turbo.c。
obj-$(CONFIG_MTK_TASK_TURBO) += task_turbo_hook_rb.o
obj-$(CONFIG_MTK_TASK_TURBO) += task_turbo_api_rb.o
