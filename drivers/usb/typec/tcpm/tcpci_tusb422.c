// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI TUSB422 Type-C port controller driver
 *
 * The TUSB422 is a TCPCI-compliant Type-C port controller.  Apart from the
 * standard TCPCI register set it needs a small vendor-specific initialisation
 * sequence: the CC comparator trim values must be programmed on untrimmed
 * parts, over-temperature shutdown has to be enabled and the CC sampling rate
 * is lowered to 1 ms for accurate forced-discharge timing.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/usb/tcpci.h>
#include <linux/usb/tcpm.h>

#define TUSB422_REG_INT_STATUS		0x90
#define TUSB422_REG_INT_MASK		0x92
#define TUSB422_REG_CC_GEN_CTRL		0x94
#define TUSB422_REG_OTSD_CTRL		0x99
#define TUSB422_REG_PAGE_SEL		0xff

#define TUSB422_INT_STATUS_ALL		0x1d

#define TUSB422_CC_SAMPLE_RATE_MASK	GENMASK(3, 2)
#define TUSB422_CC_SAMPLE_RATE_1MS	0

#define TUSB422_GLOBAL_SW_RESET		BIT(5)

#define TUSB422_EFUSE_TRIM_BIT		BIT(7)

#define TUSB422_TI_VID			0x0451
#define TUSB422_TI_PID			0x0422

struct tusb422_chip {
	struct tcpci_data data;
	struct tcpci *tcpci;
};

static const struct regmap_config tusb422_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xff,
};

static int tusb422_sw_reset(struct regmap *regmap)
{
	unsigned int val;
	int ret;

	ret = regmap_write(regmap, TUSB422_REG_PAGE_SEL, 0);
	if (ret)
		return ret;

	ret = regmap_read(regmap, TUSB422_REG_CC_GEN_CTRL, &val);
	if (ret)
		return ret;

	val &= ~TUSB422_GLOBAL_SW_RESET;
	ret = regmap_write(regmap, TUSB422_REG_CC_GEN_CTRL, val);
	if (ret)
		return ret;

	val |= TUSB422_GLOBAL_SW_RESET;
	ret = regmap_write(regmap, TUSB422_REG_CC_GEN_CTRL, val);
	if (ret)
		return ret;

	/* The datasheet requires at least 1 ms for the reset to complete. */
	usleep_range(1000, 2000);

	return 0;
}

static int tusb422_program_trim(struct regmap *regmap)
{
	static const u8 trim[] = { 0xc0, 0x08, 0x20, 0x80, 0x96, 0x1a, 0x80, 0x70 };
	unsigned int val;
	int i, ret;

	ret = regmap_write(regmap, TUSB422_REG_PAGE_SEL, 1);
	if (ret)
		return ret;

	ret = regmap_read(regmap, 0xe7, &val);
	if (ret)
		goto out_page0;

	if (!(val & TUSB422_EFUSE_TRIM_BIT)) {
		for (i = 0; i < ARRAY_SIZE(trim); i++) {
			ret = regmap_write(regmap, 0xe0 + i, trim[i]);
			if (ret)
				goto out_page0;
		}
	}

out_page0:
	return regmap_write(regmap, TUSB422_REG_PAGE_SEL, 0);
}

static int tusb422_init(struct tcpci *tcpci, struct tcpci_data *data)
{
	struct regmap *regmap = data->regmap;
	int ret;

	ret = tusb422_sw_reset(regmap);
	if (ret)
		return ret;

	ret = tusb422_program_trim(regmap);
	if (ret)
		return ret;

	/* Enable over-temperature shutdown. */
	ret = regmap_write(regmap, TUSB422_REG_OTSD_CTRL, BIT(0));
	if (ret)
		return ret;

	/* Clear stale vendor interrupts; TCPM implements its own timers. */
	ret = regmap_write(regmap, TUSB422_REG_INT_STATUS, TUSB422_INT_STATUS_ALL);
	if (ret)
		return ret;

	ret = regmap_write(regmap, TUSB422_REG_INT_MASK, 0);
	if (ret)
		return ret;

	/* 1 ms CC sampling for accurate forced-discharge timing. */
	return regmap_update_bits(regmap, TUSB422_REG_CC_GEN_CTRL,
				  TUSB422_CC_SAMPLE_RATE_MASK,
				  TUSB422_CC_SAMPLE_RATE_1MS);
}

static irqreturn_t tusb422_irq(int irq, void *dev_id)
{
	struct tusb422_chip *chip = dev_id;
	unsigned int status;
	int ret;

	ret = regmap_read(chip->data.regmap, TUSB422_REG_INT_STATUS, &status);
	if (!ret && status)
		regmap_write(chip->data.regmap, TUSB422_REG_INT_STATUS, status);

	return tcpci_irq(chip->tcpci);
}

static void tusb422_unregister_port(void *tcpci)
{
	tcpci_unregister_port(tcpci);
}

static int tusb422_probe(struct i2c_client *client)
{
	struct tusb422_chip *chip;
	u16 vid, pid;
	int ret;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->data.regmap = devm_regmap_init_i2c(client, &tusb422_regmap_config);
	if (IS_ERR(chip->data.regmap))
		return PTR_ERR(chip->data.regmap);

	/*
	 * TCPC_VENDOR_ID / TCPC_PRODUCT_ID are 16-bit TCPCI registers: the
	 * regmap is 8 bits wide, so a plain regmap_read() would only return
	 * the low byte.  Read both bytes, little-endian like tcpci_read16().
	 */
	ret = regmap_raw_read(chip->data.regmap, TCPC_VENDOR_ID, &vid,
			      sizeof(vid));
	if (ret)
		return ret;
	ret = regmap_raw_read(chip->data.regmap, TCPC_PRODUCT_ID, &pid,
			      sizeof(pid));
	if (ret)
		return ret;

	vid = le16_to_cpu(vid);
	pid = le16_to_cpu(pid);

	if (vid != TUSB422_TI_VID || pid != TUSB422_TI_PID) {
		dev_err(&client->dev, "unexpected ID %04x:%04x\n", vid, pid);
		return -ENODEV;
	}

	chip->data.init = tusb422_init;

	chip->tcpci = tcpci_register_port(&client->dev, &chip->data);
	if (IS_ERR_OR_NULL(chip->tcpci))
		return PTR_ERR(chip->tcpci);

	ret = devm_add_action_or_reset(&client->dev, tusb422_unregister_port,
				       chip->tcpci);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					tusb422_irq, IRQF_ONESHOT,
					dev_name(&client->dev), chip);
	if (ret)
		return ret;

	device_set_wakeup_capable(&client->dev, true);

	return 0;
}

static const struct i2c_device_id tusb422_id[] = {
	{ .name = "tusb422" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tusb422_id);

static const struct of_device_id tusb422_of_match[] = {
	{ .compatible = "ti,tusb422" },
	{ }
};
MODULE_DEVICE_TABLE(of, tusb422_of_match);

static struct i2c_driver tusb422_driver = {
	.driver = {
		.name = "tusb422",
		.of_match_table = tusb422_of_match,
	},
	.probe = tusb422_probe,
	.id_table = tusb422_id,
};
module_i2c_driver(tusb422_driver);

MODULE_AUTHOR("The Kirin970 mainline port");
MODULE_DESCRIPTION("TI TUSB422 Type-C port controller driver");
MODULE_LICENSE("GPL");
