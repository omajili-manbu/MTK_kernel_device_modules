// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */
#include <linux/io.h>
#include <linux/power_supply.h>
#include <mbraink_modules_ops_def.h>

#include "mbraink_v6899_battery.h"

/* rodin 6.9：原全局名 `drv_data` 与 mtk-mmdvfs / scp 撞名（链接期 duplicate
 * definition），过通用 ⇒ 加模块前缀。 */
struct battery_drv_data mbraink_bat_drv_data;

/* rodin b515 #115: init 一次性获取发生在 =y probe 早期（1.12s），psy 必未注册
 * （6.6 .ko 3.57s 才命中 bat1）⇒ 记住 dev 供运行期懒重取（幂等，取到即缓存）。 */
static struct device *mbraink_v6899_battery_dev;

static void mbraink_v6899_battery_lazy_get(void)
{
	if (!mbraink_v6899_battery_dev)
		return;

	if (mbraink_bat_drv_data.bat1_psy == NULL ||
	    IS_ERR(mbraink_bat_drv_data.bat1_psy)) {
		mbraink_bat_drv_data.bat1_psy = devm_power_supply_get_by_reference(
			mbraink_v6899_battery_dev, "gauge");
		if (mbraink_bat_drv_data.bat1_psy != NULL &&
		    !IS_ERR(mbraink_bat_drv_data.bat1_psy))
			pr_info("[MBK_v6899] %s: bat1_psy ready\n", __func__);
	}
	if (mbraink_bat_drv_data.bat2_psy == NULL ||
	    IS_ERR(mbraink_bat_drv_data.bat2_psy)) {
		mbraink_bat_drv_data.bat2_psy = devm_power_supply_get_by_reference(
			mbraink_v6899_battery_dev, "gauge2");
	}
}

static void mbraink_v6899_get_battery_info(struct mbraink_battery_data *battery_buffer,
			      long long timestamp)
{
	union power_supply_propval prop;

	memset(&prop, 0x00, sizeof(prop));

	mbraink_v6899_battery_lazy_get(); /* rodin b515 #115: 运行期补齐 init 期拿不到的 psy */
	if (mbraink_bat_drv_data.bat1_psy != NULL && !IS_ERR(mbraink_bat_drv_data.bat1_psy)) {
		battery_buffer->timestamp = timestamp;

		power_supply_get_property(mbraink_bat_drv_data.bat1_psy,
			POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, &prop);
		battery_buffer->qmaxt = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat1_psy,
			POWER_SUPPLY_PROP_ENERGY_FULL, &prop);
		battery_buffer->quse = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat1_psy,
			POWER_SUPPLY_PROP_ENERGY_NOW, &prop);
		battery_buffer->precise_soc = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat1_psy,
			POWER_SUPPLY_PROP_CAPACITY_LEVEL, &prop);
		battery_buffer->precise_uisoc = prop.intval;

		/**************************************************************************
		 *	pr_info("%s: timestamp=%lld qmaxt=%d, qusec=%d, socc=%d, uisocc=%d\n",
		 *	__func__,
		 *	battery_buffer->timestamp,
		 *	battery_buffer->qmaxt,
		 *	battery_buffer->quse,
		 *	battery_buffer->precise_soc,
		 *	battery_buffer->precise_uisoc);
		 **************************************************************************/
	}

	if (mbraink_bat_drv_data.bat2_psy != NULL && !IS_ERR(mbraink_bat_drv_data.bat2_psy)) {
		battery_buffer->timestamp = timestamp;

		power_supply_get_property(mbraink_bat_drv_data.bat2_psy,
			POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, &prop);
		battery_buffer->qmaxt2 = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat2_psy,
			POWER_SUPPLY_PROP_ENERGY_FULL, &prop);
		battery_buffer->quse2 = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat2_psy,
			POWER_SUPPLY_PROP_ENERGY_NOW, &prop);
		battery_buffer->precise_soc2 = prop.intval;

		power_supply_get_property(mbraink_bat_drv_data.bat2_psy,
			POWER_SUPPLY_PROP_CAPACITY_LEVEL, &prop);
		battery_buffer->precise_uisoc2 = prop.intval;

		/**************************************************************************
		 *	pr_info("%s: timestamp=%lld qmaxt=%d, qusec=%d, socc=%d, uisocc=%d\n",
		 *	__func__,
		 *	battery_buffer->timestamp,
		 *	battery_buffer->qmaxt2,
		 *	battery_buffer->quse2,
		 *	battery_buffer->precise_soc2,
		 *	battery_buffer->precise_uisoc2);
		 ***************************************************************************/
	}

}

static struct mbraink_battery_ops mbraink_v6899_battery_ops = {
	.getBatteryInfo = mbraink_v6899_get_battery_info,
};

int mbraink_v6899_battery_init(struct device *dev)
{
	int ret = 0;

	mbraink_v6899_battery_dev = dev; /* rodin b515 #115 */

	if (mbraink_bat_drv_data.bat1_psy == NULL) {
		pr_info("%s get phandle from bat1_psy\n", __func__);
		mbraink_bat_drv_data.bat1_psy = devm_power_supply_get_by_reference(dev, "gauge");
		if (mbraink_bat_drv_data.bat1_psy == NULL || IS_ERR(mbraink_bat_drv_data.bat1_psy))
			pr_info("%s Couldn't get bat1_psy\n", __func__);
	}

	if (mbraink_bat_drv_data.bat2_psy == NULL) {
		pr_info("%s get phandle from bat2_psy\n", __func__);
		mbraink_bat_drv_data.bat2_psy = devm_power_supply_get_by_reference(dev, "gauge2");
		if (mbraink_bat_drv_data.bat2_psy == NULL || IS_ERR(mbraink_bat_drv_data.bat2_psy))
			pr_info("%s Couldn't get bat2_psy\n", __func__);
	}

	ret = register_mbraink_battery_ops(&mbraink_v6899_battery_ops);
	return ret;
}

int mbraink_v6899_battery_deinit(void)
{
	int ret = 0;

	ret = unregister_mbraink_battery_ops();
	return ret;
}
