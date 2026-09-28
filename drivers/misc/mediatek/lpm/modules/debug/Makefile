# common KOs
# rodin batch4-7: 设备真值只有 dbg-v2（569 名单无 mtk-lpm-dbg-common-v1）⇒ v1 不下潜，
# 避免多造 569 之外的模块（v1 内含未适配的 hrtimer_init/del_timer_sync）
# obj-$(CONFIG_MTK_LOW_POWER_MODULE) += v1/
obj-$(CONFIG_MTK_LOW_POWER_MODULE) += v2/

ifeq ($(CONFIG_MTK_LPM_MT6983),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6983),)
obj-$(CONFIG_MTK_LPM_MT6983) += mt6983/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6985),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6985),)
obj-$(CONFIG_MTK_LPM_MT6985) += mt6985/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6886),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6886),)
obj-$(CONFIG_MTK_LPM_MT6886) += mt6886/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6897),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6897),)
obj-$(CONFIG_MTK_LPM_MT6897) += mt6897/
endif
endif

# rodin batch4-7: =y 内建时本行原为 =m 判定（恒假）⇒ 与 C 侧 IS_ENABLED 语义对齐
ifneq ($(CONFIG_MTK_LPM_MT6899),)
ifneq ($(wildcard $(srctree)/$(src)/mt6899),)
obj-$(CONFIG_MTK_LPM_MT6899) += mt6899/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6989),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6989),)
obj-$(CONFIG_MTK_LPM_MT6989) += mt6989/
endif
endif

ifeq ($(CONFIG_MTK_LPM_MT6991),m)
ifneq ($(wildcard $(srctree)/$(src)/mt6991),)
obj-$(CONFIG_MTK_LPM_MT6991) += mt6991/
endif
endif
