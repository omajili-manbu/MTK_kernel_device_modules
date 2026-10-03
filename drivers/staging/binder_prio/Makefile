# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_BINDER_PRIO) += binder_prio.o
# binder driver private header（6.18: drivers/android/binder_internal.h）—— mi_log 同法
ccflags-y += -I$(srctree)/drivers/android
# kernel/sched/sched.h 私有头（fair_policy/rt_policy 真身；dash 源同名 include 的 6.18 化）
ccflags-y += -I$(srctree)/kernel/sched
# binder_prio_trace.h 的 TRACE_INCLUDE_PATH=.
ccflags-y += -I$(src)
