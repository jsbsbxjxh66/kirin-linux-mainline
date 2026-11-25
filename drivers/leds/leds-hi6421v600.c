// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED driver for the Hi6421v600 PMIC
 *
 * The PMIC exposes three current sinks (red, green, blue).  Each sink has
 * an ISET register, a start-delay register and a time-config register.
 * Enabling and disabling is done through the global control register, while
 * the output control register has to be cleared for the corresponding pair
 * of bits.
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define HI6421V600_LED_ISET_BASE	0x130
#define HI6421V600_LED_START_BASE	0x134
#define HI6421V600_LED_TIME_BASE	0x137

#define HI6421V600_LED_START_DELAY	0x03
#define HI6421V600_LED_DELAY_ON		0xf0

struct hi6421v600_led_ctrl;

struct hi6421v600_led {
	struct led_classdev cdev;
	struct hi6421v600_led_ctrl *ctrl;
	u32 id;
	u32 max_current;
};

struct hi6421v600_led_ctrl {
	struct device *dev;
	struct regmap *regmap;
	struct mutex lock;
	u32 ctrl_reg;
	u32 out_ctrl_reg;
	unsigned int num_leds;
	struct hi6421v600_led leds[];
};

static int hi6421v600_led_set_brightness(struct led_classdev *cdev,
					 enum led_brightness brightness)
{
	struct hi6421v600_led *led =
		container_of(cdev, struct hi6421v600_led, cdev);
	struct hi6421v600_led_ctrl *ctrl = led->ctrl;
	unsigned int val;
	int ret;

	mutex_lock(&ctrl->lock);

	ret = regmap_read(ctrl->regmap, ctrl->ctrl_reg, &val);
	if (ret)
		goto out;

	if (brightness) {
		ret = regmap_write(ctrl->regmap,
				   HI6421V600_LED_ISET_BASE + led->id,
				   led->max_current);
		if (ret)
			goto out;

		ret = regmap_write(ctrl->regmap,
				   HI6421V600_LED_START_BASE + led->id,
				   HI6421V600_LED_START_DELAY);
		if (ret)
			goto out;

		ret = regmap_write(ctrl->regmap,
				   HI6421V600_LED_TIME_BASE + led->id,
				   HI6421V600_LED_DELAY_ON);
		if (ret)
			goto out;

		ret = regmap_write(ctrl->regmap, ctrl->ctrl_reg,
				   val | BIT(led->id));
		if (ret)
			goto out;

		ret = regmap_read(ctrl->regmap, ctrl->out_ctrl_reg, &val);
		if (ret)
			goto out;

		val &= ~(BIT(led->id * 2) | BIT(led->id * 2 + 1));
		ret = regmap_write(ctrl->regmap, ctrl->out_ctrl_reg, val);
	} else {
		ret = regmap_write(ctrl->regmap, ctrl->ctrl_reg,
				   val & ~BIT(led->id));
	}

out:
	mutex_unlock(&ctrl->lock);
	return ret;
}

static int hi6421v600_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi6421v600_led_ctrl *ctrl;
	struct device_node *child;
	struct led_init_data init_data = {};
	unsigned int num_leds;
	int ret;

	num_leds = of_get_child_count(dev->of_node);
	if (!num_leds)
		return dev_err_probe(dev, -EINVAL, "no LED nodes found\n");

	ctrl = devm_kzalloc(dev, struct_size(ctrl, leds, num_leds), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->dev = dev;
	ctrl->num_leds = num_leds;
	mutex_init(&ctrl->lock);

	ctrl->regmap = dev_get_regmap(dev->parent, NULL);
	if (!ctrl->regmap)
		return dev_err_probe(dev, -ENODEV, "no parent regmap\n");

	ret = of_property_read_u32(dev->of_node, "hisilicon,ctrl-reg",
				   &ctrl->ctrl_reg);
	if (ret)
		return dev_err_probe(dev, ret, "missing ctrl-reg\n");

	ret = of_property_read_u32(dev->of_node, "hisilicon,out-ctrl-reg",
				   &ctrl->out_ctrl_reg);
	if (ret)
		return dev_err_probe(dev, ret, "missing out-ctrl-reg\n");

	init_data.devicename = "hi6421v600";

	for_each_child_of_node(dev->of_node, child) {
		struct hi6421v600_led *led;
		u32 id;

		ret = of_property_read_u32(child, "reg", &id);
		if (ret) {
			dev_err(dev, "missing reg in %pOF\n", child);
			of_node_put(child);
			return ret;
		}

		if (id >= num_leds) {
			dev_err(dev, "invalid LED id %u\n", id);
			of_node_put(child);
			return -EINVAL;
		}

		led = &ctrl->leds[id];
		led->ctrl = ctrl;
		led->id = id;

		if (of_property_read_u32(child, "hisilicon,max-current",
					 &led->max_current))
			led->max_current = 0x7;

		led->cdev.max_brightness = LED_FULL;
		led->cdev.brightness_set_blocking =
			hi6421v600_led_set_brightness;

		init_data.fwnode = of_fwnode_handle(child);

		ret = devm_led_classdev_register_ext(dev, &led->cdev,
						     &init_data);
		if (ret) {
			dev_err(dev, "failed to register LED %u: %d\n", id, ret);
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id hi6421v600_led_of_match[] = {
	{ .compatible = "hisilicon,hi6421v600-led" },
	{}
};
MODULE_DEVICE_TABLE(of, hi6421v600_led_of_match);

static struct platform_driver hi6421v600_led_driver = {
	.probe = hi6421v600_led_probe,
	.driver = {
		.name = "hi6421v600-led",
		.of_match_table = hi6421v600_led_of_match,
	},
};
module_platform_driver(hi6421v600_led_driver);

MODULE_DESCRIPTION("Hi6421v600 PMIC LED driver");
MODULE_LICENSE("GPL");
