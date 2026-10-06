# SPDX-License-Identifier: GPL-2.0
# goodix_core_rodin =y 内建（b567）：骨架 = MiCode popsicle-w-oss p2/goodix/goodix_9615v
# + warsaw-w-oss goodix_9916k 修订（tools/_b567_touch donor 打分）。
obj-$(CONFIG_TOUCHSCREEN_GOODIX_BRL_RODIN) += goodix_core_rodin.o
goodix_core_rodin-y := goodix_brl_i2c.o goodix_brl_spi.o goodix_ts_core.o \
	goodix_brl_hw.o goodix_cfg_bin.o goodix_ts_utils.o goodix_brl_fwupdate.o \
	goodix_ts_gesture.o goodix_ts_inspect.o goodix_ts_tools.o \
	goodix_brl_normalize_coeffi_update.o goodix_scp_tp.o goodix_scp_tp.o goodix_scp_tp.o

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/goodix_brl_rodin -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/xiaomi_touch_rodin -DTOUCH_FOD_SUPPORT -DTOUCH_THP_SUPPORT \
	     -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/scp/include
