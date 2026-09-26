/* SPDX-License-Identifier: GPL-2.0 */
/* rodin stage2: 6.18 power-supply API migrations for the vendor charger family.
 * - devm_power_supply_get_by_phandle was renamed devm_power_supply_get_by_reference
 *   (same signature, fwnode-based lookup).
 * - power_supply_desc.usb_types became a BIT() bitmap; num_usb_types is gone.
 * Include after <linux/power_supply.h>. */
#ifndef RODIN_PSY_COMPAT_H
#define RODIN_PSY_COMPAT_H

#include <linux/power_supply.h>

#define devm_power_supply_get_by_phandle(dev, property) \
	devm_power_supply_get_by_reference((dev), (property))

#endif /* RODIN_PSY_COMPAT_H */
