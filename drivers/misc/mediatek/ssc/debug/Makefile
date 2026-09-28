# debug KOs for ssc
ifeq ($(CONFIG_MTK_SSC_DBG_V1), m)
ifneq ($(wildcard $(srctree)/$(src)/v1),)
obj-$(CONFIG_MTK_SSC_DBG_V1) += v1/
endif
endif

# rodin batch4-7: =y 内建（原 =m 判定恒假）；V1 块保持原样（=n 恒假）
ifneq ($(CONFIG_MTK_SSC_DBG_V2),)
ifneq ($(wildcard $(srctree)/$(src)/v2),)
# rodin batch4-7: vendor 笔误 V1→V2（原行 obj-$(CONFIG_MTK_SSC_DBG_V1) 使 v2 永不进构建）
obj-$(CONFIG_MTK_SSC_DBG_V2) += v2/
endif
endif
