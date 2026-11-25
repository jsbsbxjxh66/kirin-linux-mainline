// SPDX-License-Identifier: GPL-2.0
/*
 * HiSilicon Hi3670 BC1.2 charger detection
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/mfd/syscon.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/usb/hisi_charger_type.h>
#include <linux/usb/role.h>
#include <linux/workqueue.h>

#define BC_CTRL0			0x30
#define BC_CTRL1			0x34
#define BC_CTRL2			0x38
#define BC_STS0				0x3c

#define BC_CTRL0_SUSPEND_N		BIT(9)
#define BC_CTRL0_DMPULLDOWN		BIT(8)
#define BC_CTRL1_BC_MODE		BIT(0)
#define BC_CTRL2_VDATDETENB		BIT(4)
#define BC_CTRL2_VDATARCENB		BIT(3)
#define BC_CTRL2_CHRGSEL		BIT(2)
#define BC_CTRL2_DCDENB			BIT(1)
#define BC_STS0_CHGDET			BIT(8)
#define BC_STS0_FSVPLUS			BIT(6)

#define BC12_POLL_INTERVAL_MS		100
#define BC12_DCD_TIMEOUT_MS		900

int hisi_charger_type_notifier_register(struct notifier_block *nb);
int hisi_charger_type_notifier_unregister(struct notifier_block *nb);
enum hisi_charger_type hisi_get_charger_type(void);

static BLOCKING_NOTIFIER_HEAD(hisi_charger_type_chain);
static enum hisi_charger_type hisi_charger_type = CHARGER_TYPE_NONE;

struct hisi_bc12 {
	struct device *dev;
	struct regmap *regmap;
	struct usb_role_switch *role_sw;
	struct power_supply *psy;
	struct delayed_work work;
	struct mutex lock;
	enum power_supply_usb_type usb_type;
	bool attached;
	bool online;
};

enum hisi_charger_type hisi_get_charger_type(void)
{
	return READ_ONCE(hisi_charger_type);
}
EXPORT_SYMBOL_GPL(hisi_get_charger_type);

int hisi_charger_type_notifier_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&hisi_charger_type_chain, nb);
}
EXPORT_SYMBOL_GPL(hisi_charger_type_notifier_register);

int hisi_charger_type_notifier_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&hisi_charger_type_chain, nb);
}
EXPORT_SYMBOL_GPL(hisi_charger_type_notifier_unregister);

static enum hisi_charger_type hisi_bc12_to_legacy(enum power_supply_usb_type type)
{
	switch (type) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		return CHARGER_TYPE_SDP;
	case POWER_SUPPLY_USB_TYPE_CDP:
		return CHARGER_TYPE_CDP;
	case POWER_SUPPLY_USB_TYPE_DCP:
		return CHARGER_TYPE_DCP;
	case POWER_SUPPLY_USB_TYPE_UNKNOWN:
		return CHARGER_TYPE_UNKNOWN;
	default:
		return CHARGER_TYPE_NONE;
	}
}

static void hisi_bc12_notify(struct hisi_bc12 *bc)
{
	enum hisi_charger_type type;

	type = bc->online ? hisi_bc12_to_legacy(bc->usb_type) : CHARGER_TYPE_NONE;
	WRITE_ONCE(hisi_charger_type, type);
	blocking_notifier_call_chain(&hisi_charger_type_chain, type, NULL);
}

static int hisi_bc12_dcd_wait(struct hisi_bc12 *bc)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(BC12_DCD_TIMEOUT_MS);
	unsigned int val;
	int stable = 0;
	int ret;

	msleep(50);

	for (;;) {
		ret = regmap_read(bc->regmap, BC_STS0, &val);
		if (ret)
			return ret;

		if (!(val & BC_STS0_FSVPLUS)) {
			if (++stable >= 10)
				return 0;
		} else {
			stable = 0;
		}

		msleep(10);

		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
	}
}

static void hisi_bc12_set_vdp_src(struct hisi_bc12 *bc, bool enable)
{
	if (enable) {
		regmap_update_bits(bc->regmap, BC_CTRL2, BC_CTRL2_CHRGSEL, 0);
		regmap_update_bits(bc->regmap, BC_CTRL2,
				   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB,
				   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB);
	} else {
		regmap_update_bits(bc->regmap, BC_CTRL2,
				   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB, 0);
		regmap_update_bits(bc->regmap, BC_CTRL0, BC_CTRL0_SUSPEND_N,
				   BC_CTRL0_SUSPEND_N);
		regmap_update_bits(bc->regmap, BC_CTRL1, BC_CTRL1_BC_MODE, 0);
	}
}

static void hisi_bc12_detect(struct hisi_bc12 *bc)
{
	unsigned int val;
	int type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	int ret;

	mutex_lock(&bc->lock);

	regmap_write(bc->regmap, BC_CTRL1, BC_CTRL1_BC_MODE);
	regmap_update_bits(bc->regmap, BC_CTRL0, BC_CTRL0_SUSPEND_N, 0);
	regmap_update_bits(bc->regmap, BC_CTRL2, BC_CTRL2_DCDENB,
			   BC_CTRL2_DCDENB);
	regmap_update_bits(bc->regmap, BC_CTRL0, BC_CTRL0_DMPULLDOWN,
			   BC_CTRL0_DMPULLDOWN);

	ret = hisi_bc12_dcd_wait(bc);

	regmap_update_bits(bc->regmap, BC_CTRL0, BC_CTRL0_DMPULLDOWN, 0);
	regmap_update_bits(bc->regmap, BC_CTRL2, BC_CTRL2_DCDENB, 0);

	if (ret)
		goto out;

	regmap_update_bits(bc->regmap, BC_CTRL2,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB);
	msleep(40);

	regmap_read(bc->regmap, BC_STS0, &val);
	if (!(val & BC_STS0_CHGDET)) {
		type = POWER_SUPPLY_USB_TYPE_SDP;
		regmap_update_bits(bc->regmap, BC_CTRL2,
				   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB,
				   0);
		goto out;
	}

	regmap_update_bits(bc->regmap, BC_CTRL2,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB, 0);
	regmap_update_bits(bc->regmap, BC_CTRL2,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB |
			   BC_CTRL2_CHRGSEL,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB |
			   BC_CTRL2_CHRGSEL);
	msleep(40);

	regmap_read(bc->regmap, BC_STS0, &val);
	if (!(val & BC_STS0_CHGDET))
		type = POWER_SUPPLY_USB_TYPE_CDP;
	else
		type = POWER_SUPPLY_USB_TYPE_DCP;

	regmap_update_bits(bc->regmap, BC_CTRL2,
			   BC_CTRL2_VDATARCENB | BC_CTRL2_VDATDETENB |
			   BC_CTRL2_CHRGSEL, 0);

out:
	if (type == POWER_SUPPLY_USB_TYPE_DCP ||
	    type == POWER_SUPPLY_USB_TYPE_CDP) {
		hisi_bc12_set_vdp_src(bc, true);
	} else {
		hisi_bc12_set_vdp_src(bc, false);
	}

	bc->usb_type = type;
	bc->online = true;
	mutex_unlock(&bc->lock);

	power_supply_changed(bc->psy);
	hisi_bc12_notify(bc);
}

static void hisi_bc12_detach(struct hisi_bc12 *bc)
{
	mutex_lock(&bc->lock);

	bc->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	bc->online = false;

	mutex_unlock(&bc->lock);

	power_supply_changed(bc->psy);
	hisi_bc12_notify(bc);
}

static void hisi_bc12_work(struct work_struct *work)
{
	struct hisi_bc12 *bc = container_of(to_delayed_work(work),
					    struct hisi_bc12, work);
	enum usb_role role = bc->role_sw ?
		usb_role_switch_get_role(bc->role_sw) : USB_ROLE_NONE;
	bool attached = role == USB_ROLE_DEVICE;

	if (attached && !bc->attached)
		hisi_bc12_detect(bc);
	else if (!attached && bc->attached)
		hisi_bc12_detach(bc);

	bc->attached = attached;

	schedule_delayed_work(&bc->work,
			      msecs_to_jiffies(BC12_POLL_INTERVAL_MS));
}

static int hisi_bc12_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
	struct hisi_bc12 *bc = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = bc->online;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = bc->usb_type;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property hisi_bc12_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static const struct power_supply_desc hisi_bc12_desc = {
	.name			= "hisi-bc12",
	.type			= POWER_SUPPLY_TYPE_USB,
	.usb_types		= BIT(POWER_SUPPLY_USB_TYPE_SDP) |
				  BIT(POWER_SUPPLY_USB_TYPE_CDP) |
				  BIT(POWER_SUPPLY_USB_TYPE_DCP) |
				  BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties		= hisi_bc12_props,
	.num_properties		= ARRAY_SIZE(hisi_bc12_props),
	.get_property		= hisi_bc12_get_property,
};

static int hisi_bc12_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &pdev->dev;
	struct hisi_bc12 *bc;
	int ret;

	bc = devm_kzalloc(dev, sizeof(*bc), GFP_KERNEL);
	if (!bc)
		return -ENOMEM;

	bc->dev = dev;
	mutex_init(&bc->lock);
	INIT_DELAYED_WORK(&bc->work, hisi_bc12_work);
	bc->usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	bc->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(bc->regmap))
		return dev_err_probe(dev, PTR_ERR(bc->regmap),
				     "failed to get BC regmap\n");

	bc->role_sw = usb_role_switch_get(dev);
	if (IS_ERR(bc->role_sw)) {
		/*
		 * -EPROBE_DEFER means the provider (dwc3 role switch) is not
		 * ready yet: retry later.  Only a genuine "no provider" is
		 * tolerated by degrading to poll-only.
		 */
		if (PTR_ERR(bc->role_sw) == -EPROBE_DEFER)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "USB role switch not ready\n");
		dev_info(dev, "no USB role switch (%ld), poll-only\n",
			 PTR_ERR(bc->role_sw));
		bc->role_sw = NULL;
	}

	psy_cfg.drv_data = bc;
	psy_cfg.fwnode = dev_fwnode(dev);
	bc->psy = devm_power_supply_register(dev, &hisi_bc12_desc, &psy_cfg);
	if (IS_ERR(bc->psy)) {
		ret = PTR_ERR(bc->psy);
		goto err_put_sw;
	}

	platform_set_drvdata(pdev, bc);

	schedule_delayed_work(&bc->work, 0);

	return 0;

err_put_sw:
	usb_role_switch_put(bc->role_sw);
	return ret;
}

static void hisi_bc12_remove(struct platform_device *pdev)
{
	struct hisi_bc12 *bc = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&bc->work);
	usb_role_switch_put(bc->role_sw);
}

static const struct of_device_id hisi_bc12_of_match[] = {
	{ .compatible = "hisilicon,hi3670-bc12" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_bc12_of_match);

static struct platform_driver hisi_bc12_driver = {
	.probe	= hisi_bc12_probe,
	.remove	= hisi_bc12_remove,
	.driver	= {
		.name		= "hisi-bc12",
		.of_match_table	= hisi_bc12_of_match,
	},
};
module_platform_driver(hisi_bc12_driver);

MODULE_AUTHOR("Honor Play mainline port");
MODULE_DESCRIPTION("HiSilicon Hi3670 BC1.2 charger detection");
MODULE_LICENSE("GPL v2");
