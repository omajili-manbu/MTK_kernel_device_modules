subdir-ccflags-y += -I$(srctree)/include/
subdir-ccflags-y += -I$(srctree)/kernel/

# rodin 6.9：原写法 `ifeq ($(CONFIG_X),$(filter $(CONFIG_X),m))` 只在 =m 时成立，
# 内建 =y 后 $filter 为空 ⇒ 条件不成立 ⇒ 丢 -I ⇒ cpuqos_v3_proto.c 的
# `#include <mtk_slbc_sram.h>` fatal error（其余目录无此写法，属孤例）。
ifneq ($(filter y m,$(CONFIG_MTK_SLBC)),)
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc/
endif

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sched/

ccflags-y += -I$(srctree)/$(src)

obj-$(CONFIG_MTK_CPUQOS_V3) += cpuqos_v3.o
cpuqos_v3-y += cpuqos_v3_proto.o cpuqos_sys_common.o
