# rodin 6.9（零错误陷阱，配方 §82）：原写法
#   ifeq ($(CONFIG_MTK_FPSGO_V3),$(filter $(CONFIG_MTK_FPSGO_V3),m))
# 在符号**未定义**时两侧都为空 ⇒ `""==""` 为真，-D 靠"侥幸"被加上；一旦门翻 =y
# 两侧不再相等 ⇒ -D 消失 ⇒ 5 处门静默编掉且 RC=0（最危险的失效形态）。
# 改为 filter y m 形式（内建与模块都加 -D；二者经 linux/kconfig.h 实测等价，
# 且 .config 里的 CONFIG_MTK_FPSGO_V3=y 已由 autoconf.h 提供同一语义）。
ifneq ($(filter y m,$(CONFIG_MTK_FPSGO_V3)),)
ccflags-y += -DCONFIG_MTK_FPSGO_V3
subdir-ccflags-y += -DCONFIG_MTK_FPSGO_V3
endif

FPSGO_TOP = $(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/fpsgo_v3/
BASE_TOP = $(FPSGO_TOP)/base
FBT_TOP = $(FPSGO_TOP)/fbt
FSTB_TOP = $(FPSGO_TOP)/fstb
COM_TOP = $(FPSGO_TOP)/composer

ccflags-y += \
        -I$(srctree)/include/ \
        -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/ \
        -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/ged/include/ \
        -I$(FPSGO_TOP)/mt-plat/ \
        -I$(FPSGO_TOP)/ \
        -I$(BASE_TOP)/include/ \
        -I$(FBT_TOP)/include/ \
        -I$(FSTB_TOP)/ \
        -I$(COM_TOP)/include/ \
        -I$(srctree)/kernel/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sched/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/ \
        -I$(DEVICE_MODULES_PATH)/drivers/thermal/mediatek/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/ \

subdir-ccflags-y += \
        -I$(srctree)/include/ \
        -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/ \
        -I$(DEVICE_MODULES_PATH)/drivers/gpu/mediatek/ged/include/ \
        -I$(FPSGO_TOP)/mt-plat/ \
        -I$(FPSGO_TOP)/ \
        -I$(BASE_TOP)/include/ \
        -I$(FBT_TOP)/include/ \
        -I$(FSTB_TOP)/ \
        -I$(COM_TOP)/include/ \
        -I$(srctree)/kernel/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sched/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat/ \
        -I$(DEVICE_MODULES_PATH)/drivers/thermal/mediatek/ \
        -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/ \

#fpsgo_main
MTK_FPSGO_MAIN += fpsgo_main.o

#base
include $(FPSGO_TOP)/base/build.mk
MTK_FPSGO_BASE = $(mtk_fpsgo_base_objs:%.o=base/%.o)

#composer
include $(FPSGO_TOP)/composer/build.mk
MTK_FPSGO_COMPOSER = $(mtk_fpsgo_composer_objs:%.o=composer/%.o)

#fbt
include $(FPSGO_TOP)/fbt/build.mk
MTK_FPSGO_FBT = $(mtk_fpsgo_fbt_objs:%.o=fbt/%.o)

#fstb
include $(FPSGO_TOP)/fstb/build.mk
MTK_FPSGO_FSTB = $(mtk_fpsgo_fstb_objs:%.o=fstb/%.o)

MTK_FPSGO_SRC = $(MTK_FPSGO_MAIN) $(MTK_FPSGO_BASE) $(MTK_FPSGO_COMPOSER) $(MTK_FPSGO_FBT) $(MTK_FPSGO_FSTB)

obj-$(CONFIG_MTK_FPSGO_V3) += mtk_fpsgo.o
mtk_fpsgo-objs += $(MTK_FPSGO_SRC)

