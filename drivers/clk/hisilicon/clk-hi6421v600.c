// SPDX-License-Identifier: GPL-2.0
/*
 * Hi6421v600 PMIC clock gates
 *
 * The PMU audio clock is a gate bit in the PMIC register space, not in the
 * SoC CRG.  Keep it as a normal clock gate so the codec controller can use
 * the standard clk consumer API.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

struct hi6421v600_clk {
	struct clk_hw hw;
	struct regmap *regmap;
	unsigned int reg;
	unsigned int bit;
};

static int hi6421v600_clk_enable(struct clk_hw *hw)
{
	struct hi6421v600_clk *priv = container_of(hw, struct hi6421v600_clk, hw);

	return regmap_update_bits(priv->regmap, priv->reg,
				  BIT(priv->bit), BIT(priv->bit));
}

static void hi6421v600_clk_disable(struct clk_hw *hw)
{
	struct hi6421v600_clk *priv = container_of(hw, struct hi6421v600_clk, hw);
	int ret;

	ret = regmap_update_bits(priv->regmap, priv->reg, BIT(priv->bit), 0);
	if (ret)
		pr_warn("failed to gate PMU audio clock: %d\n", ret);
}

static int hi6421v600_clk_is_enabled(struct clk_hw *hw)
{
	struct hi6421v600_clk *priv = container_of(hw, struct hi6421v600_clk, hw);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, priv->reg, &val);
	if (ret)
		return ret;

	return !!(val & BIT(priv->bit));
}

static void hi6421v600_clkdev_drop(void *data)
{
	clkdev_drop(data);
}

static const struct clk_ops hi6421v600_clk_ops = {
	.enable = hi6421v600_clk_enable,
	.disable = hi6421v600_clk_disable,
	.is_enabled = hi6421v600_clk_is_enabled,
};

static int hi6421v600_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi6421v600_clk *priv;
	struct clk_init_data init;
	struct clk_lookup *cl;
	const char *parent_name;
	const char *clk_name;
	u32 gate[2];
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = dev_get_drvdata(dev->parent);
	if (!priv->regmap) {
		dev_err(dev, "parent PMIC regmap is not available\n");
		return -ENODEV;
	}

	ret = of_property_read_u32_array(np, "hisilicon,clkgate", gate, 2);
	if (ret) {
		dev_err(dev, "missing hisilicon,clkgate\n");
		return ret;
	}
	priv->reg = gate[0];
	priv->bit = gate[1];
	if (priv->bit > 7) {
		dev_err(dev, "invalid clock gate bit %u\n", priv->bit);
		return -EINVAL;
	}

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name) {
		dev_err(dev, "failed to get parent clock\n");
		return -EINVAL;
	}

	ret = of_property_read_string(np, "clock-output-names", &clk_name);
	if (ret)
		clk_name = np->name;

	memset(&init, 0, sizeof(init));
	init.name = clk_name;
	init.ops = &hi6421v600_clk_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	priv->hw.init = &init;

	ret = devm_clk_hw_register(dev, &priv->hw);
	if (ret) {
		dev_err(dev, "failed to register clock %s: %d\n", clk_name, ret);
		return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &priv->hw);
	if (ret) {
		dev_err(dev, "failed to add clock provider: %d\n", ret);
		return ret;
	}

	/*
	 * Some vendor-derived consumers (the connectivity board driver) look
	 * the PMIC gate up by its output name through a plain clkdev alias
	 * instead of a DT phandle.  Register the alias like the vendor clock
	 * core did, so devm_clk_get(dev, "<clock-output-names>") resolves.
	 */
	cl = clkdev_hw_create(&priv->hw, clk_name, NULL);
	if (!cl) {
		dev_err(dev, "failed to register clkdev for %s\n", clk_name);
		return -ENOMEM;
	}
	ret = devm_add_action_or_reset(dev, hi6421v600_clkdev_drop, cl);
	if (ret) {
		dev_err(dev, "failed to add clkdev cleanup for %s: %d\n",
			clk_name, ret);
		return ret;
	}

	dev_info(dev, "registered %s, gate 0x%x bit %u\n",
		 clk_name, priv->reg, priv->bit);

	return 0;
}

static const struct of_device_id hi6421v600_clk_of_match[] = {
	{ .compatible = "hisilicon,hi6421v600-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi6421v600_clk_of_match);

static struct platform_driver hi6421v600_clk_driver = {
	.driver = {
		.name = "hi6421v600-clk",
		.of_match_table = hi6421v600_clk_of_match,
	},
	.probe = hi6421v600_clk_probe,
};
module_platform_driver(hi6421v600_clk_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 PMIC clock gate");
MODULE_LICENSE("GPL");
