#####MBRAINK Common Include #####
MTK_MBRAINK_FOLDER=$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/
ccflags-y += -I$(MTK_MBRAINK_FOLDER)
subdir-ccflags-y += -I$(MTK_MBRAINK_FOLDER)
subdir-ccflags-y += -Wno-frame-address

ccflags-y += -I$(srctree)/drivers/android/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/qos/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/inc/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/inc/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/bridge
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/bridge

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/mbraink_auto
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/mbraink_auto

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 设备真值 =m、本树 =y ⇒ 必须真进（mbraink.c 需要 lpm_dbg_logger.h，B3）。
ifneq ($(filter y m,$(CONFIG_MTK_LOW_POWER_MODULE)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/v2
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/v2

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/include
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/include
endif

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 设备真值 =m、本树 =y ⇒ 必须真进（mbraink_video.c 需要 mtk_vcodec_drv.h，B2）。
ifneq ($(filter y m,$(CONFIG_DEVICE_MODULES_VIDEO_MEDIATEK_VCODEC)),)
# rodin 6.9（B2）：原 subdir 行少了 drivers/ 一级（路径解析不到）。另外正牌
# mtk_vcodec_drv.h（drivers/media/platform/mtk-vcodec/）会继续 #include <slbc_ops.h>
# ⇒ 复用 vcodec/Makefile 自己的那两条 -I。
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/videocodec/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/videocodec/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/media/platform/mtk-vcodec/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/media/platform/mtk-vcodec/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/slbc
endif

ccflags-y += -I$(srctree)/sound/usb
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/usb/


subdir-ccflags-y += -Wformat

obj-${CONFIG_MTK_MBRAINK} += mtk_mbraink.o
obj-${CONFIG_MTK_MBRAINK} += modules/
obj-${CONFIG_MTK_MBRAINK_BRIDGE} += bridge/

mtk_mbraink-objs += mbraink_power.o mbraink_video.o mbraink_process.o mbraink.o mbraink_memory.o mbraink_gpu.o mbraink_audio.o mbraink_cpufreq.o mbraink_battery.o mbraink_pmu.o mbraink_gps.o mbraink_wifi.o mbraink_usb.o

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 设备不编（hypervisor 路径）；改成 filter 后仍不成立，只是防将来翻门时静默。
ifneq ($(filter y m,$(CONFIG_MTK_MBRAINK_MT8678)),)
mtk_mbraink-objs += mbraink_auto/mbraink_auto.o

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_GRT_HYPERVISOR)),)
mtk_mbraink-objs += mbraink_auto/mbraink_hypervisor_virtio.o mbraink_auto/mbraink_hypervisor.o

subdir-ccflags-y += -I$(srctree)/kernel/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/vhost/	\
			 -I$(KERNEL_SRC)/drivers/vhost/
endif
endif
