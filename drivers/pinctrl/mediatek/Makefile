# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(srctree)/drivers/pinctrl/mediatek

# rodin 2026-10-12（6.18 对齐）：vendor × 内核同名实现 → 链接期 duplicate symbol。
# 内核树 drivers/pinctrl/mediatek/{mtk-eint,pinctrl-mtk-common-v2,pinctrl-paris}.c
# 在本树打开（\.config 的 CONFIG_PINCTRL_MT6858/MT6878=y 经 Kconfig default ARM64
# && ARCH_MEDIATEK 打开并 select PINCTRL_MTK_PARIS/V2/EINT_MTK），对 rodin 却是惰性
# 死代码（mt6858/mt6878 无对应 DT 节点，永不 probe）。内核树零改动 ⇒ vendor 侧改名
# 分流：本目录（该族全部 35 个符号的**全部引用面**逐符号实测都落在本目录内，1..14
# 个文件）整体加 -D 前缀改名。用 ccflags-y 一处覆盖全目录 TU：不依赖 include 顺序、
# 新增 TU 自动覆盖；内核侧那句 -I 仍保留（供 ../core.h 等上游头）。
# 收尾判据：vmlinux 里旧名只由内核侧对象提供、新名只由 vendor 对象提供。
ccflags-y += -Dmtk_eint_do_init=mtk_eint_do_init_rodin
ccflags-y += -Dmtk_eint_do_suspend=mtk_eint_do_suspend_rodin
ccflags-y += -Dmtk_eint_do_resume=mtk_eint_do_resume_rodin
ccflags-y += -Dmtk_eint_set_debounce=mtk_eint_set_debounce_rodin
ccflags-y += -Dmtk_eint_find_irq=mtk_eint_find_irq_rodin
ccflags-y += -Dmtk_hw_set_value=mtk_hw_set_value_rodin
ccflags-y += -Dmtk_hw_get_value=mtk_hw_get_value_rodin
ccflags-y += -Dmtk_is_virt_gpio=mtk_is_virt_gpio_rodin
ccflags-y += -Dmtk_build_eint=mtk_build_eint_rodin
ccflags-y += -Dmtk_rmw=mtk_rmw_rodin
ccflags-y += -Dmtk_pinconf_bias_disable_set=mtk_pinconf_bias_disable_set_rodin
ccflags-y += -Dmtk_pinconf_bias_disable_get=mtk_pinconf_bias_disable_get_rodin
ccflags-y += -Dmtk_pinconf_bias_set=mtk_pinconf_bias_set_rodin
ccflags-y += -Dmtk_pinconf_bias_get=mtk_pinconf_bias_get_rodin
ccflags-y += -Dmtk_pinconf_bias_disable_set_rev1=mtk_pinconf_bias_disable_set_rev1_rodin
ccflags-y += -Dmtk_pinconf_bias_disable_get_rev1=mtk_pinconf_bias_disable_get_rev1_rodin
ccflags-y += -Dmtk_pinconf_bias_set_rev1=mtk_pinconf_bias_set_rev1_rodin
ccflags-y += -Dmtk_pinconf_bias_get_rev1=mtk_pinconf_bias_get_rev1_rodin
ccflags-y += -Dmtk_pinconf_bias_set_combo=mtk_pinconf_bias_set_combo_rodin
ccflags-y += -Dmtk_pinconf_bias_get_combo=mtk_pinconf_bias_get_combo_rodin
ccflags-y += -Dmtk_pinconf_drive_set=mtk_pinconf_drive_set_rodin
ccflags-y += -Dmtk_pinconf_drive_get=mtk_pinconf_drive_get_rodin
ccflags-y += -Dmtk_pinconf_drive_set_rev1=mtk_pinconf_drive_set_rev1_rodin
ccflags-y += -Dmtk_pinconf_drive_get_rev1=mtk_pinconf_drive_get_rev1_rodin
ccflags-y += -Dmtk_pinconf_drive_set_raw=mtk_pinconf_drive_set_raw_rodin
ccflags-y += -Dmtk_pinconf_drive_get_raw=mtk_pinconf_drive_get_raw_rodin
ccflags-y += -Dmtk_pinconf_adv_pull_set=mtk_pinconf_adv_pull_set_rodin
ccflags-y += -Dmtk_pinconf_adv_pull_get=mtk_pinconf_adv_pull_get_rodin
ccflags-y += -Dmtk_pinconf_adv_drive_set=mtk_pinconf_adv_drive_set_rodin
ccflags-y += -Dmtk_pinconf_adv_drive_get=mtk_pinconf_adv_drive_get_rodin
ccflags-y += -Dmtk_pinconf_adv_drive_set_raw=mtk_pinconf_adv_drive_set_raw_rodin
ccflags-y += -Dmtk_pinconf_adv_drive_get_raw=mtk_pinconf_adv_drive_get_raw_rodin
ccflags-y += -Dmtk_paris_pinctrl_probe=mtk_paris_pinctrl_probe_rodin
ccflags-y += -Dmtk_paris_pinctrl_pm_ops=mtk_paris_pinctrl_pm_ops_rodin
ccflags-y += -Dmtk_pctrl_show_one_pin=mtk_pctrl_show_one_pin_rodin

# Core
ifeq ($(CONFIG_DEVICE_MODULES_EINT_MTK),m)
obj-$(CONFIG_DEVICE_MODULES_ALLOW_BUILTIN) += pinctrl-mtk-v2.o
else ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_V2),m)
obj-$(CONFIG_DEVICE_MODULES_ALLOW_BUILTIN) += pinctrl-mtk-v2.o
else ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_PARIS),m)
obj-$(CONFIG_DEVICE_MODULES_ALLOW_BUILTIN) += pinctrl-mtk-v2.o
endif

ifeq ($(CONFIG_DEVICE_MODULES_EINT_MTK),y)
obj-y += mtk-eint.o
else ifeq ($(CONFIG_DEVICE_MODULES_EINT_MTK),m)
pinctrl-mtk-v2-y += mtk-eint.o
endif

ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_V2),y)
obj-y += pinctrl-mtk-common-v2.o
else ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_V2),m)
pinctrl-mtk-v2-y += pinctrl-mtk-common-v2.o
endif

ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_PARIS),y)
obj-y += pinctrl-paris.o pinctrl-mtk-common-v2_debug.o
else ifeq ($(CONFIG_DEVICE_MODULES_PINCTRL_MTK_PARIS),m)
pinctrl-mtk-v2-y += pinctrl-paris.o
obj-m += pinctrl-mtk-common-v2_debug.o
endif

# SoC Drivers
obj-$(CONFIG_DEVICE_MODULES_PINCTRL_MT6779)	+= pinctrl-mt6779.o
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6363.c),)
obj-$(CONFIG_PINCTRL_MT6363)	+= pinctrl-mt6363.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6373.c),)
obj-$(CONFIG_PINCTRL_MT6373)	+= pinctrl-mt6373.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6853.c),)
obj-$(CONFIG_PINCTRL_MT6853)	+= pinctrl-mt6853.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6833.c),)
obj-$(CONFIG_PINCTRL_MT6833)	+= pinctrl-mt6833.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6873.c),)
obj-$(CONFIG_PINCTRL_MT6873)	+= pinctrl-mt6873.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6885.c),)
obj-$(CONFIG_PINCTRL_MT6885)	+= pinctrl-mt6885.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6886.c),)
obj-$(CONFIG_PINCTRL_MT6886)	+= pinctrl-mt6886.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6897.c),)
obj-$(CONFIG_PINCTRL_MT6897)	+= pinctrl-mt6897.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6899.c),)
obj-$(CONFIG_PINCTRL_MT6899)	+= pinctrl-mt6899.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6983.c),)
obj-$(CONFIG_PINCTRL_MT6983)	+= pinctrl-mt6983.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6985.c),)
obj-$(CONFIG_PINCTRL_MT6985)	+= pinctrl-mt6985.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6768.c),)
obj-$(CONFIG_PINCTRL_MT6768)	+= pinctrl-mt6768.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6761.c),)
obj-$(CONFIG_PINCTRL_MT6761)	+= pinctrl-mt6761.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6781.c),)
obj-$(CONFIG_PINCTRL_MT6781)	+= pinctrl-mt6781.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6765.c),)
obj-$(CONFIG_DEVICE_MODULES_PINCTRL_MT6765)	+= pinctrl-mt6765.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6853.c),)
obj-$(CONFIG_PINCTRL_MT6853)	+= pinctrl-mt6853.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6877.c),)
obj-$(CONFIG_PINCTRL_MT6877)	+= pinctrl-mt6877.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6989.c),)
obj-$(CONFIG_PINCTRL_MT6989)	+= pinctrl-mt6989.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt6991.c),)
obj-$(CONFIG_PINCTRL_MT6991)	+= pinctrl-mt6991.o
endif
ifneq ($(wildcard $(srctree)/$(src)/pinctrl-mt8188.c),)
obj-$(CONFIG_DEVICE_MODULES_PINCTRL_MT8188)	+= pinctrl-mt8188.o
endif
obj-$(CONFIG_DEVICE_MODULES_PINCTRL_MT8195)    += pinctrl-mt8195.o
