// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 MediaTek Inc.

#include <linux/module.h>
#include <linux/vseq.h>
#include <linux/of_device.h>
#include <linux/of.h> /* rodin stage2: 6.18 of_device.h slimmed */
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spmi.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <dt-bindings/spmi/spmi.h>

struct mtk_spmi_pmic_debug_data {
	struct mutex lock;
	struct regmap *regmap;
	struct device *dev;
	unsigned int reg_value;
	u8 usid;
};

static struct mtk_spmi_pmic_debug_data *mtk_spmi_pmic_debug[SPMI_MAX_SLAVE_ID];

static ssize_t pmic_access_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct mtk_spmi_pmic_debug_data *data = dev_get_drvdata(dev);

	dev_info(dev, "0x%x\n", data->reg_value);
	return sprintf(buf, "0x%x\n", data->reg_value);
}

static ssize_t pmic_access_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf,
				 size_t size)
{
	struct mtk_spmi_pmic_debug_data *data;
	struct regmap *regmap;
	unsigned int reg_val = 0, reg_adr = 0;
	int ret = 0;
	char *pvalue = NULL, *addr, *val;
	u8 usid = 0;

	if (dev) {
		data = dev_get_drvdata(dev);
		if (!data)
			return -ENODEV;
	} else
		return -ENODEV;

	if (buf != NULL && size != 0) {
		dev_info(dev, "size is %d, buf is %s\n", (int)size, buf);

		pvalue = (char *)buf;
		addr = strsep(&pvalue, " ");
		val = strsep(&pvalue, " ");
		if (addr) {
			ret = kstrtou32(addr, 16, (unsigned int *)&reg_adr);
			if (ret < 0)
				dev_notice(dev, "%s failed to use kstrtou32\n", __func__);
		}
		if (reg_adr & 0xF0000) {
			usid = (u8)((reg_adr & 0xF0000) >> 16);
			if (!mtk_spmi_pmic_debug[usid]) {
				data->reg_value = 0;
				dev_info(dev, "invalid slave-%d\n", usid);
				return -ENODEV;
			}
			regmap = mtk_spmi_pmic_debug[usid]->regmap;
			reg_adr &= (0xFFFF);
		} else {
			usid = data->usid;
			regmap = data->regmap;
		}
		mutex_lock(&data->lock);

		if (val) {
			ret = kstrtou32(val, 16, (unsigned int *)&reg_val);
			if (ret < 0)
				dev_notice(dev, "%s failed to use kstrtou32\n", __func__);
			regmap_write(regmap, reg_adr, reg_val);
		} else
			regmap_read(regmap, reg_adr, &data->reg_value);

		mutex_unlock(&data->lock);
		dev_info(dev, "%s slave-%d PMIC Reg[0x%x]=0x%x!\n",
			 (val ? "write" : "read"), usid, reg_adr,
			 (val ? reg_val : data->reg_value));
	}
	return size;
}
static DEVICE_ATTR_RW(pmic_access);

/*
 * rodin: model-aware power-off / reset reason snapshot.
 *
 * PG_SDN_STS* describe which rails entered shutdown; TOPSTATUS/WDTRSTB hold
 * the chip-side reset latches.  The bit meanings below are NOT decoded here:
 * only raw bytes are recorded, because the public MT6363 notes disagree with
 * what a clean reboot shows on this board.  Keeping the raw values lets the
 * offline decode be corrected without another build.
 *
 * MT6368 has no published power-off map, so its status windows are captured
 * raw as well.
 */
struct rodin_pmic_reg {
	const char *name;
	u16 addr;
	u8 bytes;
};

static const struct rodin_pmic_reg rodin_mt6363_regs[] = {
	{ "TOPSTATUS",        0x1e,  1 },
	{ "WDTRSTB",          0x139, 1 },
	{ "PG_SDN_STS0_1",    0x20b, 2 },
	{ "PG_SDN_STS2_3",    0x20d, 2 },
	{ "PG_SDN_STS4",      0x20f, 1 },
	{ "SHUTDOWN_SRC_SEL", 0x408, 1 },	/* address not confirmed */
};

static const struct rodin_pmic_reg rodin_mt6368_windows[] = {
	{ "raw", 0x000, 32 },
	{ "raw", 0x130, 16 },
	{ "raw", 0x200, 32 },
};

static const char *rodin_pmic_model(struct device_node *np)
{
	if (!np)
		return NULL;
	if (of_device_is_compatible(np, "mediatek,mt6363"))
		return "MT6363";
	if (of_device_is_compatible(np, "mediatek,mt6368"))
		return "MT6368";
	return NULL;
}

static int rodin_pmic_read(struct regmap *map, u16 addr, u8 bytes, u32 *out)
{
	unsigned int v;
	u32 acc = 0;
	int i, ret;

	for (i = 0; i < bytes; i++) {
		ret = regmap_read(map, addr + i, &v);
		if (ret)
			return ret;
		acc |= (u32)v << (8 * i);
	}
	*out = acc;
	return 0;
}

static void rodin_pmic_snapshot(struct device *dev,
				struct mtk_spmi_pmic_debug_data *data,
				const char *tag)
{
	const char *model;
	char line[256];
	u32 v;
	int i, k, len;

	if (!dev || !data || !data->regmap)
		return;
	model = rodin_pmic_model(dev->parent ? dev->parent->of_node : NULL);
	if (!model)
		return;

	if (!strcmp(model, "MT6363")) {
		len = scnprintf(line, sizeof(line), "rodin-pmic[%s] MT6363 usid=%u",
				tag, data->usid);
		for (i = 0; i < ARRAY_SIZE(rodin_mt6363_regs); i++) {
			if (len >= (int)sizeof(line) - 1)
				break;
			if (rodin_pmic_read(data->regmap, rodin_mt6363_regs[i].addr,
					    rodin_mt6363_regs[i].bytes, &v))
				len += scnprintf(line + len, sizeof(line) - len,
						 " %s=ERR", rodin_mt6363_regs[i].name);
			else
				len += scnprintf(line + len, sizeof(line) - len,
						 " %s=%0*x", rodin_mt6363_regs[i].name,
						 rodin_mt6363_regs[i].bytes * 2, v);
		}
		pr_info("%s\n", line);
		return;
	}

	/* MT6368: raw windows, 8 bytes per line */
	pr_info("rodin-pmic[%s] MT6368 usid=%u raw windows\n", tag, data->usid);
	for (i = 0; i < ARRAY_SIZE(rodin_mt6368_windows); i++) {
		for (k = 0; k < rodin_mt6368_windows[i].bytes; k += 8) {
			int n, off = rodin_mt6368_windows[i].addr + k;

			len = scnprintf(line, sizeof(line),
					"rodin-pmic[%s] MT6368 %03x:", tag, off);
			for (n = 0; n < 8 && k + n < rodin_mt6368_windows[i].bytes; n++) {
				if (len >= (int)sizeof(line) - 1)
					break;
				if (rodin_pmic_read(data->regmap, off + n, 1, &v))
					len += scnprintf(line + len, sizeof(line) - len, " --");
				else
					len += scnprintf(line + len, sizeof(line) - len,
							 " %02x", v);
			}
			pr_info("%s\n", line);
		}
	}
}

/*
 * Probe runs before the AEE ram console is registered, so the same snapshot is
 * taken again from late_initcall: that copy lands in the persistent SRAM ring
 * and survives the next reset even when pstore is overwritten.
 */
static int __init rodin_pmic_snapshot_late(void)
{
	int i;

	for (i = 0; i < SPMI_MAX_SLAVE_ID; i++)
		if (mtk_spmi_pmic_debug[i])
			rodin_pmic_snapshot(mtk_spmi_pmic_debug[i]->dev,
					    mtk_spmi_pmic_debug[i], "late");
	return 0;
}
late_initcall(rodin_pmic_snapshot_late);

static int mtk_spmi_debug_parse_dt(struct device *dev)
{
	struct device_node *node;
	int err;
	u32 reg[2] = {0};

	if (!dev || !dev->of_node)
		return -ENODEV;
	node = dev->of_node;
	err = of_property_read_u32_array(node, "reg", reg, 2);
	if (err) {
		pr_info("%s does not have 'reg' property\n", __func__);
		return -EINVAL;
	}

	if (reg[1] != SPMI_USID) {
		pr_info("%s node contains unsupported 'reg' entry\n", __func__);
		return -EINVAL;
	}

	if (reg[0] >= SPMI_MAX_SLAVE_ID) {
		pr_info("%s invalid usid=%d\n", __func__, reg[0]);
		return -EINVAL;
	}

	return reg[0];
}

static int mtk_spmi_pmic_debug_probe(struct platform_device *pdev)
{
	struct mtk_spmi_pmic_debug_data *data;
	int ret = 0, usid = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(struct mtk_spmi_pmic_debug_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);
	data->dev = &pdev->dev;
	data->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!data->regmap)
		return -ENODEV;

	platform_set_drvdata(pdev, data);

	/* Create sysfs entry */
	ret = device_create_file(&pdev->dev, &dev_attr_pmic_access);
	if (ret < 0)
		dev_info(&pdev->dev, "failed to create sysfs file\n");
	usid = mtk_spmi_debug_parse_dt(pdev->dev.parent);
	if (usid >= 0) {
		data->usid = (u8) usid;
		mtk_spmi_pmic_debug[usid] = data;
		rodin_pmic_snapshot(&pdev->dev, data, "probe");
	}
	dev_info(&pdev->dev, "success to create %s slave-%d sysfs file\n", pdev->name, usid);

	return ret;
}

static void mtk_spmi_pmic_debug_remove(struct platform_device *pdev) /* rodin stage2: 6.18 .remove is void */
{
	device_remove_file(&pdev->dev, &dev_attr_pmic_access);
}

static const struct of_device_id mtk_spmi_pmic_debug_of_match[] = {
	{ .compatible = "mediatek,spmi-pmic-debug", },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_spmi_pmic_debug_of_match);

static struct platform_driver mtk_spmi_pmic_debug_driver = {
	.driver = {
		.name = "mtk-spmi-pmic-debug",
		.of_match_table = mtk_spmi_pmic_debug_of_match,
	},
	.probe = mtk_spmi_pmic_debug_probe,
	.remove = mtk_spmi_pmic_debug_remove,
};
vseq_module_platform_driver(mtk_spmi_pmic_debug_driver);

MODULE_AUTHOR("Wen Su <wen.su@mediatek.com>");
MODULE_AUTHOR("Jeter Chen <jeter.chen@mediatek.com>");
MODULE_DESCRIPTION("Debug driver for MediaTek SPMI PMIC");
MODULE_LICENSE("GPL v2");
