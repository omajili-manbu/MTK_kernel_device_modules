# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(srctree) -I$(srctree)/kernel -I$(srctree)/$(src)
obj-$(CONFIG_XIAOMI_METIS) += metis.o
metis-y := metis-state.o metis-classify.o metis-blocked.o metis-binder-request.o \
	metis-link.o metis-common.o metis-coldstart.o metis-vip-queue.o \
	metis-extra-controls.o metis-wakeup.o metis-monitor.o \
	metis-cluster-boost.o metis-lock-wait.o metis-controls.o \
	metis-final-control-affinity.o metis-final-control-persist.o \
	metis-final-control-api.o metis-final-control-comm.o \
	metis-final-link.o metis-final-link-spin.o \
	metis-final-scheduler-affinity.o metis-final-boost.o \
	metis-final-important.o metis-final-owners.o metis-final-tables.o \
	metis-final-trace.o metis-final-module.o metis-final-exports.o
