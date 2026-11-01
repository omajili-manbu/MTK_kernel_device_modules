# rodin 6.18（2026-11-01）：tinysys（v2）选路优先于 legacy（v1）。
# 原序为 MTK_PLAT_POWER_6893 抢先命中 v1 —— 但该开关是"平台系列总开关"
# （core_ctl/upower/dcm/leakage/power_gs 等 10+ 处引用，不能关），于是 6.18
# 编了 legacy v1，而 6.6 stock 走 tinysys v2（stock mcupm.ko 导出
# mcupm_plt_module_init = v2 独有；boot 打印 "mcupm module init." 与
# "MCUPM: Alive RES_MEM(0) SKIP_LOG(1) ret(0)" 亦为 v2 独有）。
# v1 缺 MCUPM 固件握手路径 ⇒ 固件任务未进 TASK_STA=1 ⇒ BL31 DEPD readiness
# 闸永不就绪（40s 兜底不可达）⇒ PSCI CPU_SUSPEND 全被 -2 拒 ⇒ 深档 0 进入。
# v1/v2 有 5 个同名导出（get_mcupm_ipidev/mcupm_mbox_write/
# mcupm_reserve_mem_get_*），不可同时编，故只能择一 —— 此处按 stock 选 v2。
ifneq (, $(filter $(strip $(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT)),m y))
    obj-$(CONFIG_MTK_TINYSYS_MCUPM_SUPPORT) += v2/
else ifneq (, $(filter $(strip $(CONFIG_MTK_PLAT_POWER_6893)),m y))
    obj-y += v1/
endif