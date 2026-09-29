# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_MTK_MBRAINK_MT6989)),)
obj-$(CONFIG_MTK_MBRAINK_MT6989) += v6989/
endif

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_MTK_MBRAINK_MT6991)),)
obj-$(CONFIG_MTK_MBRAINK_MT6991) += v6991/
endif

# rodin 6.9（V3）：原 `ifeq ($(CONFIG_X),m)` 在 =y 内建时**静默不成立** ⇒
# 丢 -I 并连带丢功能（编译期无感知）。统一改 `ifneq ($(filter y m,$(CONFIG_X)),)`。
ifneq ($(filter y m,$(CONFIG_MTK_MBRAINK_MT6899)),)
obj-$(CONFIG_MTK_MBRAINK_MT6899) += v6899/
endif
