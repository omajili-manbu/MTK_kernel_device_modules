#####MBRAINK Common Include #####
MTK_MBRAINK_FOLDER=$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mbraink/
ccflags-y += -I$(MTK_MBRAINK_FOLDER)
subdir-ccflags-y += -I$(MTK_MBRAINK_FOLDER)

#####FOR SWPM Include #####
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/inc/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/inc/

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_MTK_SWPM_MODULE)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/modules/debug/v6899
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/swpm/modules/debug/v6899/subsys
endif

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 本树 =y ⇒ fstb/ 与 mt-plat/ 的 -I 必须真进。
ifneq ($(filter y m,$(CONFIG_MTK_FPSGO_V3)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/fstb/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/fstb/
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/mt-plat/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/mt-plat/
endif

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/spmi/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/spmi/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/regulator/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/regulator/

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 本树 =y ⇒ ged/include 的 -I 必须真进。
ifneq ($(filter y m,$(CONFIG_MTK_GPU_SUPPORT)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/ged/include/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/ged/include/
endif
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/v2/include/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/gpufreq/v2/include/

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 本树 =y ⇒ lpm debug/sys_res 的 -I 必须真进。
ifneq ($(filter y m,$(CONFIG_MTK_SYS_RES_DBG_SUPPORT)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/sys_res
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/sys_res

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/v2
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/debug/v2

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/include
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/lpm/modules/include

endif

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/rv
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/rv

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 本树 4-9 新翻 =y ⇒ dvfsrc/ 的 -I 必须真进（dvfsrc_get_data 面）。
ifneq ($(filter y m,$(CONFIG_MTK_DVFSRC_MB)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/dvfsrc/
endif

ccflags-y += -I$(srctree)/drivers/android/

ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/mmdvfs/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/power_throttling/

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pbm/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/pbm/

ccflags-y += -I$(srctree)/drivers/misc/mediatek/qos/

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
# 本树 =y ⇒ ufs 的 -I 必须真进。
ifneq ($(filter y m,$(CONFIG_DEVICE_MODULES_SCSI_UFS_MEDIATEK)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/ufs/
endif


subdir-ccflags-y += -Wformat

BUILD_MBRAINK_MODULE_NAME=mtk_mbraink_v6899
obj-${CONFIG_MTK_MBRAINK_MT6899} += ${BUILD_MBRAINK_MODULE_NAME}.o

${BUILD_MBRAINK_MODULE_NAME}-objs += mbraink_v6899.o mbraink_v6899_memory.o mbraink_v6899_audio.o mbraink_v6899_battery.o mbraink_v6899_power.o mbraink_v6899_gpu.o mbraink_v6899_gps.o mbraink_v6899_wifi.o mbraink_v6899_camera.o

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_MTK_SWPM_MODULE)),)
${BUILD_MBRAINK_MODULE_NAME}-objs += mbraink_sys_res_plat.o mbraink_sys_res_mbrain_plat.o mbraink_sys_res.o mbraink_sys_res_mbrain_dbg.o
endif
