obj-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_status_v2.o
vcp_status_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_status.o

obj-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_v2.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_helper.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_excep.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_irq.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_logger.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_vcpctl.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_feature_table.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_awake.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += vcp_hwvoter_dbg.o
vcp_v2-$(CONFIG_MTK_TINYSYS_VCP_V2_SUPPORT) += mmup.o

# rodin r25: -fno-pic -mcmodel=large dropped -- as a builtin these TUs
# link into the KASLR-relocated vmlinux, which rejects their
# R_AARCH64_MOVW_UABS_* absolute relocations. Kernel default model works.
ccflags-y += -D DEBUG_DO
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/vcp/rv_v2
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/include/mt-plat
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/clk/mediatek
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/iommu/arm/arm-smmu-v3
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/dma-buf/heaps
