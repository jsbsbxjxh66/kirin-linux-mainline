// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon IP subsystem regulators controlled through ATF.
 */

#include <linux/arm-smccc.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

#define HISI_IP_REGULATOR_FN_ID		0xc500fff0UL

struct hisi_ip_regulator {
	struct regulator_desc rdesc;
	/* serializes SMC calls and protects @enabled */
	struct mutex lock;
	u32 id;
	bool enabled;
};

static int hisi_ip_regulator_smc(struct device *dev, u32 id, bool enable)
{
	struct arm_smccc_res res;

	arm_smccc_smc(HISI_IP_REGULATOR_FN_ID, id, enable ? 1 : 0, 0, 0, 0,
		      0, 0, &res);
	if (res.a0) {
		dev_err(dev, "regulator %u %s failed: 0x%lx\n", id,
			enable ? "enable" : "disable", res.a0);
		return -EIO;
	}

	return 0;
}

static int hisi_ip_regulator_enable(struct regulator_dev *rdev)
{
	struct hisi_ip_regulator *reg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&reg->lock);
	ret = hisi_ip_regulator_smc(rdev->dev.parent, reg->id, true);
	if (!ret)
		reg->enabled = true;
	mutex_unlock(&reg->lock);

	return ret;
}

static int hisi_ip_regulator_disable(struct regulator_dev *rdev)
{
	struct hisi_ip_regulator *reg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&reg->lock);
	ret = hisi_ip_regulator_smc(rdev->dev.parent, reg->id, false);
	if (!ret)
		reg->enabled = false;
	mutex_unlock(&reg->lock);

	return ret;
}

static int hisi_ip_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct hisi_ip_regulator *reg = rdev_get_drvdata(rdev);

	return reg->enabled;
}

static const struct regulator_ops hisi_ip_regulator_ops = {
	.enable		= hisi_ip_regulator_enable,
	.disable	= hisi_ip_regulator_disable,
	.is_enabled	= hisi_ip_regulator_is_enabled,
};

static int hisi_ip_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hisi_ip_regulator *reg;
	struct regulator_dev *rdev;
	struct regulator_config config = {};
	const char *name;
	int ret;

	reg = devm_kzalloc(dev, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	ret = of_property_read_string(np, "regulator-name", &name);
	if (ret)
		return dev_err_probe(dev, ret, "missing regulator-name\n");

	ret = of_property_read_u32(np, "hisilicon,hisi-regulator-id", &reg->id);
	if (ret)
		return dev_err_probe(dev, ret, "missing regulator-id\n");

	mutex_init(&reg->lock);

	reg->rdesc.name = name;
	if (of_property_read_bool(np, "vin-supply"))
		reg->rdesc.supply_name = "vin";
	reg->rdesc.type = REGULATOR_VOLTAGE;
	reg->rdesc.owner = THIS_MODULE;
	reg->rdesc.ops = &hisi_ip_regulator_ops;

	config.dev = dev;
	config.of_node = np;
	config.driver_data = reg;
	config.init_data = of_get_regulator_init_data(dev, np, &reg->rdesc);
	if (!config.init_data)
		return dev_err_probe(dev, -EINVAL, "failed to parse regulator data\n");

	rdev = devm_regulator_register(dev, &reg->rdesc, &config);
	if (IS_ERR(rdev))
		return dev_err_probe(dev, PTR_ERR(rdev),
				     "failed to register regulator\n");

	return 0;
}

static const struct of_device_id hisi_ip_regulator_of_match[] = {
	{ .compatible = "ip-regulator-atf" },
	{ .compatible = "hisilicon,ip-regulator-atf" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_ip_regulator_of_match);

static struct platform_driver hisi_ip_regulator_driver = {
	.probe	= hisi_ip_regulator_probe,
	.driver	= {
		.name = "hisi-ip-regulator",
		.of_match_table = hisi_ip_regulator_of_match,
	},
};
module_platform_driver(hisi_ip_regulator_driver);

MODULE_DESCRIPTION("HiSilicon IP subsystem ATF regulators");
MODULE_LICENSE("GPL");
