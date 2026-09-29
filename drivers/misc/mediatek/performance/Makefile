
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

MTK_PM_BUILD_FOLDER=$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/performance

subdir-ccflags-y += \
	-I$(MTK_PM_BUILD_FOLDER)/perf_ioctl/ \
	-I$(MTK_PM_BUILD_FOLDER)/include/\
	-I$(MTK_PM_BUILD_FOLDER)/powerhal_cpu_ctrl/

# mtk_perf_ioctl
include $(MTK_PM_BUILD_FOLDER)/perf_ioctl/build.mk
MTK_PERF_IOCTL = $(mtk_perf_ioctl_objs:%.o=perf_ioctl/%.o)
MTK_PERF_IOCTL_SRC = $(MTK_PERF_IOCTL)
obj-$(CONFIG_MTK_PERFORMANCE_MODULE) += mtk_perf_ioctl.o
mtk_perf_ioctl-objs += $(MTK_PERF_IOCTL_SRC)

# mtk_ioctl_touch_boost
MTK_IOCTL_TOUCH_BOOST = $(mtk_ioctl_touch_boost_objs:%.o=perf_ioctl/%.o)
MTK_IOCTL_TOUCH_BOOST_SRC = $(MTK_IOCTL_TOUCH_BOOST)
obj-$(CONFIG_MTK_PERFORMANCE_MODULE) += mtk_ioctl_touch_boost.o
mtk_ioctl_touch_boost-objs += $(MTK_IOCTL_TOUCH_BOOST_SRC)

# mtk_ioctl_powerhal
MTK_IOCTL_POWERHAL = $(mtk_ioctl_powerhal_objs:%.o=perf_ioctl/%.o)
MTK_IOCTL_POWERHAL_SRC = $(MTK_IOCTL_POWERHAL)
obj-$(CONFIG_MTK_PERFORMANCE_MODULE) += mtk_ioctl_powerhal.o
mtk_ioctl_powerhal-objs += $(MTK_IOCTL_POWERHAL_SRC)

# mtk_perf_ioctl_magt
MTK_PERF_IOCTL_MAGT = $(mtk_perf_ioctl_magt_objs:%.o=perf_ioctl/%.o)
MTK_PERF_IOCTL_MAGT_SRC = $(MTK_PERF_IOCTL_MAGT)
obj-$(CONFIG_MTK_PERF_IOCTL_MAGT) += mtk_perf_ioctl_magt.o
mtk_perf_ioctl_magt-objs += $(MTK_PERF_IOCTL_MAGT_SRC)

# mtk_game
mtk_game_objs += game.o
MTK_GAME = $(mtk_game_objs:%.o=game/%.o)
MTK_GAME_SRC = $(MTK_GAME)
obj-$(CONFIG_MTK_GAME) += mtk_game.o
mtk_game-objs += $(MTK_GAME_SRC)

#Generic subdir build
obj-$(CONFIG_MTK_FPSGO_V3) += fpsgo_v3/
obj-$(CONFIG_MTK_FRS) += frs/
# rodin 4-3: powerhal_cpu_ctrl 闭包含闭源 metis/mi_schedule（sched 簇）；当时拆门默认 n。
# rodin 4-9: 门已复活 =y（与 perf/sched 栈同批内建）。闭包源头是设备 blob 侧的 Xiaomi
# 私有补丁，源码内建后不再被引用 —— 4-9 闭包实测对该件零影响。
obj-$(CONFIG_MTK_POWERHAL_CPU_CTRL) += powerhal_cpu_ctrl/
obj-$(CONFIG_MTK_PERFORMANCE_MODULE) += touch_boost/

obj-$(CONFIG_MTK_LOAD_TRACKER) += load_track/
obj-$(CONFIG_MTK_LOAD_TRACKER) += uload_ind/
