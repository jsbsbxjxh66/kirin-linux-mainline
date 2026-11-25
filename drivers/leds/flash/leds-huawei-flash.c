// SPDX-License-Identifier: GPL-2.0
/*
 * Flash/torch LED driver for Huawei devices using LM3642, MP3331 or AW3642.
 *
 * The bootloader may populate one of several pin-compatible flash ICs on the
 * same I2C bus, so the chip is identified from its ID register at probe time.
 */

#include <linux/i2c.h>
#include <linux/led-class-flash.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define HUAWEI_FLASH_REG_ID		0x00
#define HUAWEI_FLASH_DEFAULT_TIMEOUT	600000

struct huawei_flash_chip {
	const char *name;
	u8 id_mask;
	u8 id_val;
	u8 enable_reg;
	u8 mode_flash;
	u8 mode_torch;
	u8 mode_off;
	u8 flash_cur_reg;
	u8 torch_cur_reg;
	bool shared_cur;
	u8 flash_cur_mask;
	u8 torch_cur_mask;
	u8 torch_cur_shift;
	u8 cur_set_bit;
	u8 init_reg;
	u8 init_val;
	u8 fault_reg;
	u8 fault2_reg;
	u32 flash_min_ua;
	u32 flash_step_ua;
	u8 flash_max_level;
	u32 torch_min_ua;
	u32 torch_step_ua;
	u8 torch_max_level;
	const u32 *flash_table;
	const u32 *torch_table;
	u8 flash_table_size;
	u8 torch_table_size;
};

struct huawei_flash {
	struct led_classdev_flash fled_cdev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex lock; /* protects register access and LED state */
	struct delayed_work powerdown_work;
	const struct huawei_flash_chip *chip;
	u32 timeout;
	u8 flash_level;
	u8 torch_level;
	bool strobe;
};

static const u32 lm3642_flash_table[] = {
	94000, 188000, 281000, 375000, 469000, 563000, 656000, 750000,
	844000, 938000, 1031000, 1125000, 1219000, 1313000, 1406000, 1500000,
};

static const u32 lm3642_torch_table[] = {
	48000, 94000, 141000, 188000, 234000, 281000, 328000, 375000,
};

static const u32 aw3642_flash_table[] = {
	24000, 47000, 71000, 95000, 118000, 142000, 165000, 189000,
	212000, 236000, 260000, 283000, 307000, 331000, 354000, 378000,
	401000, 424000, 448000, 471000, 495000, 519000, 542000, 566000,
	589000, 613000, 636000, 659000, 683000, 707000, 730000, 754000,
	775000, 799000, 822000, 846000, 870000, 894000, 917000, 940000,
	964000, 988000, 1010000, 1034000, 1058000, 1082000, 1105000, 1128000,
	1149000, 1173000, 1196000, 1219000, 1244000, 1267000, 1290000, 1313000,
	1337000, 1361000, 1383000, 1407000, 1431000, 1455000, 1477000, 1500000,
};

static const u32 aw3642_torch_table[] = {
	6000, 11000, 17000, 23000, 29000, 35000, 41000, 47000,
	53000, 59000, 65000, 70000, 76000, 82000, 88000, 94000,
	100000, 106000, 111000, 117000, 123000, 129000, 135000, 141000,
	147000, 153000, 159000, 165000, 170000, 176000, 182000, 188000,
	193000, 199000, 205000, 211000, 217000, 223000, 229000, 235000,
	241000, 247000, 252000, 258000, 264000, 270000, 276000, 282000,
	288000, 293000, 299000, 305000, 311000, 317000, 323000, 329000,
	335000, 341000, 346000, 353000, 358000, 364000, 370000, 376000,
};

static const struct huawei_flash_chip lm3642_chip = {
	.name = "lm3642",
	.id_mask = 0x07,
	.id_val = 0x00,
	.enable_reg = 0x0a,
	.mode_flash = 0x83,
	.mode_torch = 0x82,
	.mode_off = 0x00,
	.flash_cur_reg = 0x09,
	.torch_cur_reg = 0x09,
	.shared_cur = true,
	.flash_cur_mask = 0x0f,
	.torch_cur_mask = 0x07,
	.torch_cur_shift = 4,
	.init_reg = 0x08,
	.init_val = 0x0d,
	.fault_reg = 0x0b,
	.flash_min_ua = 94000,
	.flash_step_ua = 93750,
	.flash_max_level = 15,
	.torch_min_ua = 48000,
	.torch_step_ua = 46875,
	.torch_max_level = 7,
	.flash_table = lm3642_flash_table,
	.torch_table = lm3642_torch_table,
	.flash_table_size = ARRAY_SIZE(lm3642_flash_table),
	.torch_table_size = ARRAY_SIZE(lm3642_torch_table),
};

static const struct huawei_flash_chip mp3331_chip = {
	.name = "mp3331",
	.id_mask = 0xf8,
	.id_val = 0x18,
	.enable_reg = 0x01,
	.mode_flash = 0x96,
	.mode_torch = 0x94,
	.mode_off = 0xa0,
	.flash_cur_reg = 0x06,
	.torch_cur_reg = 0x0a,
	.flash_cur_mask = 0x3f,
	.torch_cur_mask = 0x1f,
	.init_reg = 0x03,
	.init_val = 0xba,
	.fault_reg = 0x0b,
	.fault2_reg = 0x0c,
	.flash_min_ua = 0,
	.flash_step_ua = 31700,
	.flash_max_level = 47,
	.torch_min_ua = 0,
	.torch_step_ua = 31700,
	.torch_max_level = 12,
};

static const struct huawei_flash_chip aw3642_chip = {
	.name = "aw3642",
	.id_mask = 0xff,
	.id_val = 0x36,
	.enable_reg = 0x01,
	.mode_flash = 0x0f,
	.mode_torch = 0x0b,
	.mode_off = 0x80,
	.flash_cur_reg = 0x03,
	.torch_cur_reg = 0x05,
	.flash_cur_mask = 0x3f,
	.torch_cur_mask = 0x3f,
	.cur_set_bit = 0x80,
	.init_reg = 0x02,
	.init_val = 0x59,
	.fault_reg = 0x0a,
	.fault2_reg = 0x0b,
	.flash_min_ua = 24000,
	.flash_step_ua = 23809,
	.flash_max_level = 63,
	.torch_min_ua = 6000,
	.torch_step_ua = 5873,
	.torch_max_level = 63,
	.flash_table = aw3642_flash_table,
	.torch_table = aw3642_torch_table,
	.flash_table_size = ARRAY_SIZE(aw3642_flash_table),
	.torch_table_size = ARRAY_SIZE(aw3642_torch_table),
};

static const struct huawei_flash_chip *huawei_flash_chips[] = {
	&aw3642_chip,
	&mp3331_chip,
	&lm3642_chip,
};

static struct huawei_flash *fled_cdev_to_flash(struct led_classdev_flash *fled_cdev)
{
	return container_of(fled_cdev, struct huawei_flash, fled_cdev);
}

static u8 huawei_flash_lookup_level(const u32 *table, u8 size, u32 ua)
{
	u8 level;

	if (!table || !size)
		return 0;

	for (level = 0; level < size - 1; level++) {
		if (ua <= table[level])
			break;
	}

	return level;
}

static u8 huawei_flash_level(const struct huawei_flash *priv, bool flash, u32 ua)
{
	const struct huawei_flash_chip *chip = priv->chip;
	const u32 *table;
	u8 size, max_level;
	u32 min_ua, step_ua;

	if (flash) {
		table = chip->flash_table;
		size = chip->flash_table_size;
		min_ua = chip->flash_min_ua;
		step_ua = chip->flash_step_ua;
		max_level = chip->flash_max_level;
	} else {
		table = chip->torch_table;
		size = chip->torch_table_size;
		min_ua = chip->torch_min_ua;
		step_ua = chip->torch_step_ua;
		max_level = chip->torch_max_level;
	}

	if (table && size)
		return huawei_flash_lookup_level(table, size, ua);

	if (!step_ua || ua <= min_ua)
		return 0;

	return min_t(u32, (ua - min_ua) / step_ua, max_level);
}

static u32 huawei_flash_table_max(const struct huawei_flash_chip *chip, bool flash)
{
	if (flash) {
		if (chip->flash_table)
			return chip->flash_table[chip->flash_table_size - 1];
		return chip->flash_min_ua + chip->flash_step_ua * chip->flash_max_level;
	}

	if (chip->torch_table)
		return chip->torch_table[chip->torch_table_size - 1];
	return chip->torch_min_ua + chip->torch_step_ua * chip->torch_max_level;
}

static int huawei_flash_write_level(struct huawei_flash *priv, bool flash, u8 level)
{
	const struct huawei_flash_chip *chip = priv->chip;
	unsigned int val;
	int ret;

	if (!chip->shared_cur) {
		val = level | chip->cur_set_bit;
		return regmap_write(priv->regmap,
				    flash ? chip->flash_cur_reg : chip->torch_cur_reg,
				    val);
	}

	ret = regmap_read(priv->regmap, chip->flash_cur_reg, &val);
	if (ret)
		return ret;

	if (flash)
		val = (val & ~chip->flash_cur_mask) |
		      (level & chip->flash_cur_mask);
	else
		val = (val & ~(chip->torch_cur_mask << chip->torch_cur_shift)) |
		      ((level & chip->torch_cur_mask) << chip->torch_cur_shift);

	return regmap_write(priv->regmap, chip->flash_cur_reg, val);
}

static int huawei_flash_set_mode(struct huawei_flash *priv, u8 mode)
{
	return regmap_write(priv->regmap, priv->chip->enable_reg, mode);
}

static int huawei_flash_off(struct huawei_flash *priv)
{
	priv->strobe = false;

	return huawei_flash_set_mode(priv, priv->chip->mode_off);
}

static int huawei_flash_read_faults(struct huawei_flash *priv, u32 *fault)
{
	const struct huawei_flash_chip *chip = priv->chip;
	unsigned int val = 0, val2 = 0;
	int ret;

	ret = regmap_read(priv->regmap, chip->fault_reg, &val);
	if (ret)
		return ret;
	if (chip->fault2_reg) {
		ret = regmap_read(priv->regmap, chip->fault2_reg, &val2);
		if (ret)
			return ret;
	}

	*fault = 0;

	if (chip == &lm3642_chip) {
		if (val & BIT(4))
			*fault |= LED_FAULT_UNDER_VOLTAGE;
		if (val & BIT(3))
			*fault |= LED_FAULT_OVER_VOLTAGE;
		if (val & BIT(2))
			*fault |= LED_FAULT_SHORT_CIRCUIT;
		if (val & BIT(1))
			*fault |= LED_FAULT_OVER_TEMPERATURE;
	} else if (chip == &aw3642_chip) {
		if (val & BIT(1))
			*fault |= LED_FAULT_UNDER_VOLTAGE;
		if (val & BIT(4))
			*fault |= LED_FAULT_SHORT_CIRCUIT;
		if (val & BIT(6))
			*fault |= LED_FAULT_SHORT_CIRCUIT;
		if (val & BIT(2))
			*fault |= LED_FAULT_OVER_TEMPERATURE;
		if (val2 & BIT(1))
			*fault |= LED_FAULT_OVER_VOLTAGE;
	} else {
		if (val & BIT(6))
			*fault |= LED_FAULT_OVER_VOLTAGE;
		if (val & (BIT(5) | BIT(4)))
			*fault |= LED_FAULT_SHORT_CIRCUIT;
		if (val & BIT(3))
			*fault |= LED_FAULT_OVER_TEMPERATURE;
		if (val2 & BIT(0))
			*fault |= LED_FAULT_SHORT_CIRCUIT;
	}

	return 0;
}

static int huawei_flash_brightness_set(struct led_classdev *led_cdev,
				       enum led_brightness brightness)
{
	struct huawei_flash *priv = fled_cdev_to_flash(lcdev_to_flcdev(led_cdev));
	int ret;

	cancel_delayed_work_sync(&priv->powerdown_work);

	mutex_lock(&priv->lock);
	if (brightness == LED_OFF) {
		ret = huawei_flash_off(priv);
	} else {
		priv->torch_level = min_t(u8, brightness - 1,
					  priv->chip->torch_max_level);
		ret = huawei_flash_write_level(priv, false, priv->torch_level);
		if (!ret)
			ret = huawei_flash_set_mode(priv, priv->chip->mode_torch);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int huawei_flash_flash_brightness_set(struct led_classdev_flash *fled_cdev,
					     u32 brightness)
{
	struct huawei_flash *priv = fled_cdev_to_flash(fled_cdev);
	int ret;

	mutex_lock(&priv->lock);
	priv->flash_level = huawei_flash_level(priv, true, brightness);
	ret = huawei_flash_write_level(priv, true, priv->flash_level);
	mutex_unlock(&priv->lock);

	return ret;
}

static int huawei_flash_timeout_set(struct led_classdev_flash *fled_cdev,
				    u32 timeout)
{
	struct huawei_flash *priv = fled_cdev_to_flash(fled_cdev);

	mutex_lock(&priv->lock);
	priv->timeout = timeout;
	mutex_unlock(&priv->lock);

	return 0;
}

static void huawei_flash_powerdown_work(struct work_struct *work)
{
	struct huawei_flash *priv = container_of(to_delayed_work(work),
						 struct huawei_flash,
						 powerdown_work);

	mutex_lock(&priv->lock);
	if (priv->strobe)
		huawei_flash_off(priv);
	mutex_unlock(&priv->lock);
}

static int huawei_flash_strobe_set(struct led_classdev_flash *fled_cdev,
				   bool state)
{
	struct huawei_flash *priv = fled_cdev_to_flash(fled_cdev);
	int ret;

	cancel_delayed_work_sync(&priv->powerdown_work);

	mutex_lock(&priv->lock);
	if (state) {
		ret = huawei_flash_write_level(priv, true, priv->flash_level);
		if (!ret)
			ret = huawei_flash_set_mode(priv, priv->chip->mode_flash);
		if (!ret) {
			priv->strobe = true;
			mod_delayed_work(system_wq, &priv->powerdown_work,
					 usecs_to_jiffies(priv->timeout));
		}
	} else {
		ret = huawei_flash_off(priv);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int huawei_flash_strobe_get(struct led_classdev_flash *fled_cdev,
				   bool *state)
{
	struct huawei_flash *priv = fled_cdev_to_flash(fled_cdev);

	mutex_lock(&priv->lock);
	*state = priv->strobe;
	mutex_unlock(&priv->lock);

	return 0;
}

static int huawei_flash_fault_get(struct led_classdev_flash *fled_cdev,
				  u32 *fault)
{
	struct huawei_flash *priv = fled_cdev_to_flash(fled_cdev);

	return huawei_flash_read_faults(priv, fault);
}

static const struct led_flash_ops huawei_flash_ops = {
	.flash_brightness_set = huawei_flash_flash_brightness_set,
	.strobe_set = huawei_flash_strobe_set,
	.strobe_get = huawei_flash_strobe_get,
	.timeout_set = huawei_flash_timeout_set,
	.fault_get = huawei_flash_fault_get,
};

static const struct of_device_id huawei_flash_of_match[] = {
	{ .compatible = "huawei,flash-led" },
	{ }
};
MODULE_DEVICE_TABLE(of, huawei_flash_of_match);

static const struct i2c_device_id huawei_flash_id[] = {
	{ "huawei-flash" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, huawei_flash_id);

static const struct regmap_config huawei_flash_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0c,
};

static int huawei_flash_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct huawei_flash *priv;
	struct fwnode_handle *child;
	struct led_init_data init_data = {};
	struct led_flash_setting *setting;
	const struct huawei_flash_chip *chip = NULL;
	unsigned int id;
	u32 flash_max_ua, torch_max_ua, flash_max_timeout;
	size_t i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);
	mutex_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->powerdown_work, huawei_flash_powerdown_work);

	priv->regmap = devm_regmap_init_i2c(client, &huawei_flash_regmap_config);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "failed to initialise regmap\n");

	ret = regmap_read(priv->regmap, HUAWEI_FLASH_REG_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read chip ID\n");

	for (i = 0; i < ARRAY_SIZE(huawei_flash_chips); i++) {
		const struct huawei_flash_chip *candidate = huawei_flash_chips[i];

		if ((id & candidate->id_mask) == candidate->id_val) {
			chip = candidate;
			break;
		}
	}

	if (!chip)
		return dev_err_probe(dev, -ENODEV, "unsupported flash LED ID 0x%02x\n",
				     id);

	priv->chip = chip;

	if (chip->init_reg) {
		ret = regmap_write(priv->regmap, chip->init_reg, chip->init_val);
		if (ret)
			return dev_err_probe(dev, ret, "failed to initialise %s\n",
					     chip->name);
	}

	child = device_get_next_child_node(dev, NULL);
	if (!child)
		return dev_err_probe(dev, -ENODEV, "missing LED child node\n");

	ret = fwnode_property_read_u32(child, "flash-max-microamp", &flash_max_ua);
	if (ret)
		flash_max_ua = huawei_flash_table_max(chip, true);

	ret = fwnode_property_read_u32(child, "led-max-microamp", &torch_max_ua);
	if (ret)
		torch_max_ua = huawei_flash_table_max(chip, false);

	ret = fwnode_property_read_u32(child, "flash-max-timeout-us",
				       &flash_max_timeout);
	if (ret)
		flash_max_timeout = HUAWEI_FLASH_DEFAULT_TIMEOUT;

	priv->flash_level = huawei_flash_level(priv, true, flash_max_ua);
	priv->torch_level = huawei_flash_level(priv, false, torch_max_ua);
	priv->timeout = min_t(u32, flash_max_timeout, HUAWEI_FLASH_DEFAULT_TIMEOUT);
	priv->fled_cdev.ops = &huawei_flash_ops;

	setting = &priv->fled_cdev.brightness;
	setting->min = 0;
	setting->max = flash_max_ua;
	setting->step = 1;
	setting->val = flash_max_ua;

	setting = &priv->fled_cdev.timeout;
	setting->min = 1;
	setting->max = flash_max_timeout;
	setting->step = 1;
	setting->val = priv->timeout;

	priv->fled_cdev.led_cdev.brightness_set_blocking =
		huawei_flash_brightness_set;
	priv->fled_cdev.led_cdev.max_brightness =
		min_t(u32, torch_max_ua ? huawei_flash_level(priv, false, torch_max_ua) + 1 : 1,
		      chip->torch_max_level + 1);
	priv->fled_cdev.led_cdev.flags |= LED_DEV_CAP_FLASH;

	init_data.fwnode = child;
	init_data.devicename = chip->name;
	init_data.default_label = "flash";

	ret = devm_led_classdev_flash_register_ext(dev, &priv->fled_cdev,
						   &init_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register LED\n");

	dev_info(dev, "registered %s flash LED (ID 0x%02x)\n", chip->name, id);

	return 0;
}

static void huawei_flash_remove(struct i2c_client *client)
{
	struct huawei_flash *priv = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&priv->powerdown_work);

	mutex_lock(&priv->lock);
	huawei_flash_off(priv);
	mutex_unlock(&priv->lock);
}

static struct i2c_driver huawei_flash_driver = {
	.driver = {
		.name = "huawei-flash",
		.of_match_table = huawei_flash_of_match,
	},
	.probe = huawei_flash_probe,
	.remove = huawei_flash_remove,
	.id_table = huawei_flash_id,
};
module_i2c_driver(huawei_flash_driver);

MODULE_DESCRIPTION("Huawei LM3642/MP3331/AW3642 flash LED driver");
MODULE_LICENSE("GPL");
