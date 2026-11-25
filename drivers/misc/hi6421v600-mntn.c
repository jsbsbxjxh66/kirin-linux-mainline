// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi6421v600 PMIC maintenance monitor
 *
 * The PMIC can protect the SoC rails independently of the AP: it raises an
 * OCP/SCP interrupt when a regulator trips, and an OTMP interrupt when the
 * die exceeds the configured temperature.  It also latches the reason of
 * the previous shutdown in a set of record registers.
 *
 * This driver consumes the OCP and OTMP interrupts from the hi6421v600-irq
 * domain, clears the latched events, disables the affected rail for
 * non-fatal OCP events and logs the boot-time record registers.  Fatal OCP
 * events and OTMP can optionally request an emergency restart, matching the
 * vendor PMIC monitor policy.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mfd/hi6421v600-mntn.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#define HI6421V600_OCP_REGS	8
#define HI6421V600_NO_RAIL	0xdead
#define HI6421V600_REG_WIDTH	8

struct hi6421v600_mntn_field {
	u32 addr;
	u32 shift;
	u32 width;
};

struct hi6421v600_ocp_desc {
	u16 status;
	u8 fatal;
	u16 disable[HI6421V600_REG_WIDTH];
	const char *name[HI6421V600_REG_WIDTH];
};

static const struct hi6421v600_ocp_desc hi6421v600_ocp[] = {
	{
		.status = 0x214,
		.fatal = 0xff,
		.disable = { 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08 },
		.name = { "buck7", "buck6", "buck5", "buck4",
			  "buck3", "buck2", "buck1", "buck0" },
	}, {
		.status = 0x215,
		.fatal = 0x1f,
		.disable = { 0x16, 0x14, 0x13, 0x12, 0x10,
			     HI6421V600_NO_RAIL, HI6421V600_NO_RAIL,
			     HI6421V600_NO_RAIL },
		.name = { "ldo3", "ldo2", "ldo1", "ldo0_2", "buck8",
			  "reserved", "reserved", "reserved" },
	}, {
		.status = 0x216,
		.fatal = 0x78,
		.disable = { 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19, 0x18, 0x17 },
		.name = { "ldo12", "ldo11", "ldo9", "ldo8",
			  "ldo7", "ldo6", "ldo5", "ldo4" },
	}, {
		.status = 0x217,
		.fatal = 0x61,
		.disable = { 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20, 0x1f },
		.name = { "ldo20", "ldo19", "ldo18", "ldo17",
			  "ldo16", "ldo15", "ldo14", "ldo13" },
	}, {
		.status = 0x218,
		.fatal = 0xef,
		.disable = { 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28, 0x27 },
		.name = { "ldo28", "ldo27", "ldo26", "ldo25",
			  "ldo24", "ldo23", "ldo22", "ldo21" },
	}, {
		.status = 0x219,
		.fatal = 0x7b,
		.disable = { 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x2f,
			     HI6421V600_NO_RAIL },
		.name = { "ldo36", "ldo35", "ldo34", "ldo33",
			  "ldo32", "ldo30", "ldo29", "reserved" },
	}, {
		.status = 0x21a,
		.fatal = 0xff,
		.disable = { 0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08 },
		.name = { "buck7", "buck6", "buck5", "buck4",
			  "buck3", "buck2", "buck1", "buck0" },
	}, {
		.status = 0x21b,
		.fatal = 0x03,
		.disable = { HI6421V600_NO_RAIL, 0x10, HI6421V600_NO_RAIL,
			     HI6421V600_NO_RAIL, HI6421V600_NO_RAIL,
			     HI6421V600_NO_RAIL, HI6421V600_NO_RAIL,
			     HI6421V600_NO_RAIL },
		.name = { "ldobuff", "buck8", "reserved", "reserved",
			  "reserved", "reserved", "reserved", "reserved" },
	},
};

struct hi6421v600_mntn {
	struct device *dev;
	struct regmap *regmap;
	u32 ocp_regs[HI6421V600_OCP_REGS];
	u32 record_regs[15];
	int record_num;
	struct hi6421v600_mntn_field otmp_threshold;
	struct hi6421v600_mntn_field otmp_powerdown;
	struct hi6421v600_mntn_field smpl_enable;
	struct hi6421v600_mntn_field smpl_time;
	u32 otmp_threshold_val;
	u32 otmp_powerdown_val;
	u32 smpl_enable_val;
	u32 smpl_time_val;
	bool otmp_powerdown_en;
	bool ocp_restart;
	bool otmp_restart;
};

static int hi6421v600_mntn_read_field(struct device *dev, const char *name,
				      struct hi6421v600_mntn_field *field)
{
	u32 value[3];
	int ret;

	ret = device_property_read_u32_array(dev, name, value, 3);
	if (ret)
		return dev_err_probe(dev, ret, "missing %s\n", name);

	field->addr = value[0];
	field->shift = value[1];
	field->width = value[2];
	if (!field->width || field->width > 31 || field->shift > 31)
		return dev_err_probe(dev, -EINVAL, "invalid %s\n", name);

	return 0;
}

static int hi6421v600_mntn_write_field(struct regmap *regmap,
				       const struct hi6421v600_mntn_field *field,
				       u32 value)
{
	u32 mask = ((1U << field->width) - 1) << field->shift;

	return regmap_update_bits(regmap, field->addr, mask,
				  (value << field->shift) & mask);
}

static int hi6421v600_mntn_read_record(struct hi6421v600_mntn *mntn)
{
	unsigned int i;
	int ret;

	mntn->record_num = device_property_count_u32(mntn->dev,
						     "hisilicon,record-regs");
	if (mntn->record_num < 0)
		return mntn->record_num;
	if (mntn->record_num > ARRAY_SIZE(mntn->record_regs))
		return dev_err_probe(mntn->dev, -EINVAL,
				     "too many record registers\n");

	ret = device_property_read_u32_array(mntn->dev, "hisilicon,record-regs",
					     mntn->record_regs,
					     mntn->record_num);
	if (ret)
		return ret;

	for (i = 0; i < mntn->record_num; i++) {
		unsigned int value;

		ret = regmap_read(mntn->regmap, mntn->record_regs[i], &value);
		if (ret)
			return ret;

		if (value)
			dev_warn(mntn->dev,
				 "PMIC record 0x%x: 0x%02x (previous shutdown reason)\n",
				 mntn->record_regs[i], value);

		/* Write-one-to-clear the latched record bits. */
		regmap_write(mntn->regmap, mntn->record_regs[i], value);
	}

	return 0;
}

static int hi6421v600_mntn_clear_ocp(struct hi6421v600_mntn *mntn)
{
	unsigned int i;
	int ret;

	for (i = 0; i < HI6421V600_OCP_REGS; i++) {
		unsigned int value;

		ret = regmap_read(mntn->regmap, mntn->ocp_regs[i], &value);
		if (ret)
			return ret;

		if (value)
			dev_warn(mntn->dev, "PMIC OCP 0x%x: 0x%02x\n",
				 mntn->ocp_regs[i], value);

		regmap_write(mntn->regmap, mntn->ocp_regs[i], value);
	}

	return 0;
}

struct hi6421v600_ocp_handler {
	struct list_head node;
	const char *power_name;
	hi6421v600_ocp_handler_t handler;
};

static LIST_HEAD(hi6421v600_ocp_handlers);
static DEFINE_MUTEX(hi6421v600_ocp_handlers_lock);

int hi6421v600_mntn_register_ocp_handler(const char *power_name,
					 hi6421v600_ocp_handler_t handler)
{
	struct hi6421v600_ocp_handler *entry;
	const char *name;

	if (!power_name || !handler)
		return -EINVAL;

	name = kstrdup(power_name, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	entry = kzalloc_obj(*entry);
	if (!entry) {
		kfree(name);
		return -ENOMEM;
	}

	entry->power_name = name;
	entry->handler = handler;

	mutex_lock(&hi6421v600_ocp_handlers_lock);
	list_add_tail(&entry->node, &hi6421v600_ocp_handlers);
	mutex_unlock(&hi6421v600_ocp_handlers_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(hi6421v600_mntn_register_ocp_handler);

static bool hi6421v600_mntn_handle_ocp(const char *power_name)
{
	hi6421v600_ocp_handler_t handler = NULL;
	struct hi6421v600_ocp_handler *entry;

	mutex_lock(&hi6421v600_ocp_handlers_lock);
	list_for_each_entry(entry, &hi6421v600_ocp_handlers, node) {
		if (!strcmp(entry->power_name, power_name)) {
			handler = entry->handler;
			break;
		}
	}
	mutex_unlock(&hi6421v600_ocp_handlers_lock);

	/* Handlers are never unregistered, so @handler stays valid. */
	return handler && handler(power_name) == 0;
}

static irqreturn_t hi6421v600_mntn_ocp_irq(int irq, void *data)
{
	struct hi6421v600_mntn *mntn = data;
	bool fatal = false;
	unsigned int i;

	for (i = 0; i < HI6421V600_OCP_REGS; i++) {
		const struct hi6421v600_ocp_desc *desc = &hi6421v600_ocp[i];
		unsigned int value;
		unsigned long pending;
		unsigned int bit;

		if (regmap_read(mntn->regmap, mntn->ocp_regs[i], &value))
			continue;

		if (!value)
			continue;

		pending = value;
		for_each_set_bit(bit, &pending, HI6421V600_REG_WIDTH) {
			dev_err(mntn->dev, "PMIC OCP on %s (0x%x bit %u)\n",
				desc->name[bit], mntn->ocp_regs[i], bit);

			/*
			 * A registered subsystem handler takes precedence over
			 * the generic policy.  This is used by the modem to
			 * report the affected rail over ICC before the rail is
			 * cut or the AP is restarted.
			 */
			if (hi6421v600_mntn_handle_ocp(desc->name[bit]))
				continue;

			if (desc->fatal & BIT(bit)) {
				fatal = true;
				continue;
			}

			/*
			 * For a non-fatal rail, cut it off so that the fault
			 * cannot keep drawing current.  Fatal rails are left
			 * alone: they usually power the AP itself, and the
			 * restart below is the only useful action.
			 */
			if (desc->disable[bit] != HI6421V600_NO_RAIL)
				regmap_write(mntn->regmap, desc->disable[bit], 0);
		}

		/* Write-one-to-clear the latched OCP bits. */
		regmap_write(mntn->regmap, mntn->ocp_regs[i], value);
	}

	if (fatal && mntn->ocp_restart) {
		dev_emerg(mntn->dev, "fatal PMIC OCP, restarting\n");
		emergency_restart();
	}

	return IRQ_HANDLED;
}

static irqreturn_t hi6421v600_mntn_otmp_irq(int irq, void *data)
{
	struct hi6421v600_mntn *mntn = data;

	dev_crit(mntn->dev, "PMIC over-temperature event\n");

	if (mntn->otmp_powerdown_en)
		hi6421v600_mntn_write_field(mntn->regmap,
					    &mntn->otmp_powerdown,
					    mntn->otmp_powerdown_val);

	if (mntn->otmp_restart) {
		dev_emerg(mntn->dev, "PMIC over-temperature, restarting\n");
		emergency_restart();
	}

	return IRQ_HANDLED;
}

static int hi6421v600_mntn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi6421v600_mntn *mntn;
	int ret;

	mntn = devm_kzalloc(dev, sizeof(*mntn), GFP_KERNEL);
	if (!mntn)
		return -ENOMEM;

	mntn->dev = dev;
	mntn->regmap = dev_get_regmap(dev->parent, NULL);
	if (!mntn->regmap)
		return dev_err_probe(dev, -ENODEV, "no parent regmap\n");

	ret = device_property_read_u32_array(dev, "hisilicon,ocp-regs",
					     mntn->ocp_regs,
					     HI6421V600_OCP_REGS);
	if (ret) {
		unsigned int i;

		dev_warn(dev, "no hisilicon,ocp-regs, using PMIC defaults\n");
		for (i = 0; i < HI6421V600_OCP_REGS; i++)
			mntn->ocp_regs[i] = hi6421v600_ocp[i].status;
	}

	ret = hi6421v600_mntn_read_field(dev, "hisilicon,otmp-threshold-reg",
					 &mntn->otmp_threshold);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "hisilicon,otmp-threshold-val",
				       &mntn->otmp_threshold_val);
	if (ret)
		return dev_err_probe(dev, ret, "missing OTMP threshold value\n");

	ret = hi6421v600_mntn_read_field(dev, "hisilicon,otmp-hreset-pwrdown-reg",
					 &mntn->otmp_powerdown);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "hisilicon,otmp-hreset-pwrdown-val",
				       &mntn->otmp_powerdown_val);
	if (ret)
		return dev_err_probe(dev, ret, "missing OTMP powerdown value\n");
	mntn->otmp_powerdown_en = device_property_read_bool(dev,
					"hisilicon,otmp-hreset-pwrdown-flag");

	ret = hi6421v600_mntn_read_field(dev, "hisilicon,smpl-ctrl-en-reg",
					 &mntn->smpl_enable);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "hisilicon,smpl-ctrl-en",
				       &mntn->smpl_enable_val);
	if (ret)
		return dev_err_probe(dev, ret, "missing SMPL enable value\n");

	ret = hi6421v600_mntn_read_field(dev, "hisilicon,smpl-ctrl-time-reg",
					 &mntn->smpl_time);
	if (ret)
		return ret;
	ret = device_property_read_u32(dev, "hisilicon,smpl-ctrl-time",
				       &mntn->smpl_time_val);
	if (ret)
		return dev_err_probe(dev, ret, "missing SMPL time value\n");

	mntn->ocp_restart = device_property_read_bool(dev,
						      "hisilicon,ocp-restart");
	mntn->otmp_restart = device_property_read_bool(dev,
						       "hisilicon,otmp-restart");

	ret = hi6421v600_mntn_write_field(mntn->regmap, &mntn->otmp_threshold,
					  mntn->otmp_threshold_val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set OTMP threshold\n");

	ret = hi6421v600_mntn_write_field(mntn->regmap, &mntn->smpl_enable,
					  mntn->smpl_enable_val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set SMPL enable\n");

	ret = hi6421v600_mntn_write_field(mntn->regmap, &mntn->smpl_time,
					  mntn->smpl_time_val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to set SMPL time\n");

	ret = hi6421v600_mntn_clear_ocp(mntn);
	if (ret)
		return dev_err_probe(dev, ret, "failed to clear OCP state\n");

	ret = hi6421v600_mntn_read_record(mntn);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read record state\n");

	ret = platform_get_irq_byname(pdev, "ocp");
	if (ret < 0)
		return ret;
	ret = devm_request_threaded_irq(dev, ret, NULL,
					hi6421v600_mntn_ocp_irq,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"pmic-ocp", mntn);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request OCP IRQ\n");

	ret = platform_get_irq_byname(pdev, "otmp");
	if (ret < 0)
		return ret;
	ret = devm_request_threaded_irq(dev, ret, NULL,
					hi6421v600_mntn_otmp_irq,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"pmic-otmp", mntn);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request OTMP IRQ\n");

	platform_set_drvdata(pdev, mntn);

	dev_info(dev, "PMIC OCP/OTMP monitor enabled\n");

	return 0;
}

static const struct of_device_id hi6421v600_mntn_of_match[] = {
	{ .compatible = "hisilicon,hi6421v600-mntn" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi6421v600_mntn_of_match);

static struct platform_driver hi6421v600_mntn_driver = {
	.probe = hi6421v600_mntn_probe,
	.driver = {
		.name = "hi6421v600-mntn",
		.of_match_table = hi6421v600_mntn_of_match,
	},
};
module_platform_driver(hi6421v600_mntn_driver);

MODULE_DESCRIPTION("HiSilicon Hi6421v600 PMIC OCP/OTMP monitor");
MODULE_LICENSE("GPL");
