// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 (Kirin 970) LPM3-controlled regulator.
 *
 * The g3d rail is not controlled by a local register: the AP sends an
 * enable/disable command to the always-on LPM3 firmware over a synchronous
 * HiIPCV230 channel (14 on this SoC) and waits for the echoed command with
 * a zero status byte.
 */

#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/mailbox_client.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>
#include <linux/string.h>

#define HI3670_LPM_MSG_LEN		8
#define HI3670_LPM_ACK_TIMEOUT_MS	300

struct hi3670_lpm_reg {
	struct device *dev;
	struct regulator_desc rdesc;
	struct regulator_dev *rdev;
	struct mbox_client cl;
	struct mbox_chan *mbox;
	struct completion ack;
	struct mutex lock;
	u32 ack_msg[HI3670_LPM_MSG_LEN];
	u32 msg[HI3670_LPM_MSG_LEN];
	u32 enable_cmd[2];
	u32 disable_cmd[2];
	u32 voltage;
	bool ack_valid;
	bool enabled;
};

static void hi3670_lpm_reg_rx(struct mbox_client *cl, void *msg)
{
	struct hi3670_lpm_reg *reg = container_of(cl, struct hi3670_lpm_reg, cl);

	memcpy(reg->ack_msg, msg, sizeof(reg->ack_msg));
	reg->ack_valid = true;
	complete(&reg->ack);
}

static int hi3670_lpm_reg_send(struct hi3670_lpm_reg *reg, const u32 *cmd)
{
	int ret;

	memset(reg->msg, 0, sizeof(reg->msg));
	reg->msg[0] = cmd[0];
	reg->msg[1] = cmd[1];

	reg->ack_valid = false;
	reinit_completion(&reg->ack);

	ret = mbox_send_message(reg->mbox, reg->msg);
	if (ret < 0)
		return ret;

	if (!wait_for_completion_timeout(&reg->ack,
					 msecs_to_jiffies(HI3670_LPM_ACK_TIMEOUT_MS))) {
		dev_err(reg->dev, "LPM3 command 0x%x timed out\n", cmd[0]);
		return -ETIMEDOUT;
	}

	if (!reg->ack_valid || reg->ack_msg[0] != cmd[0] ||
	    (reg->ack_msg[1] >> 24) != 0) {
		dev_err(reg->dev, "LPM3 command 0x%x failed: 0x%x 0x%x\n",
			cmd[0], reg->ack_msg[0], reg->ack_msg[1]);
		return -EIO;
	}

	return 0;
}

static int hi3670_lpm_reg_enable(struct regulator_dev *rdev)
{
	struct hi3670_lpm_reg *reg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&reg->lock);
	ret = hi3670_lpm_reg_send(reg, reg->enable_cmd);
	if (!ret)
		reg->enabled = true;
	mutex_unlock(&reg->lock);

	return ret;
}

static int hi3670_lpm_reg_disable(struct regulator_dev *rdev)
{
	struct hi3670_lpm_reg *reg = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&reg->lock);
	ret = hi3670_lpm_reg_send(reg, reg->disable_cmd);
	if (!ret)
		reg->enabled = false;
	mutex_unlock(&reg->lock);

	return ret;
}

static int hi3670_lpm_reg_is_enabled(struct regulator_dev *rdev)
{
	struct hi3670_lpm_reg *reg = rdev_get_drvdata(rdev);

	return reg->enabled;
}

static int hi3670_lpm_reg_get_voltage(struct regulator_dev *rdev)
{
	struct hi3670_lpm_reg *reg = rdev_get_drvdata(rdev);

	return reg->voltage;
}

static const struct regulator_ops hi3670_lpm_reg_ops = {
	.enable		= hi3670_lpm_reg_enable,
	.disable	= hi3670_lpm_reg_disable,
	.is_enabled	= hi3670_lpm_reg_is_enabled,
	.get_voltage	= hi3670_lpm_reg_get_voltage,
};

static int hi3670_lpm_reg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hi3670_lpm_reg *reg;
	struct regulator_config config = {};
	const char *name;
	int ret;

	reg = devm_kzalloc(dev, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	reg->dev = dev;

	ret = of_property_read_u32_array(np, "hisilicon,enable-step",
					 reg->enable_cmd, 2);
	if (ret)
		return dev_err_probe(dev, ret, "missing enable step\n");

	ret = of_property_read_u32_array(np, "hisilicon,disable-step",
					 reg->disable_cmd, 2);
	if (ret)
		return dev_err_probe(dev, ret, "missing disable step\n");

	ret = of_property_read_string(np, "regulator-name", &name);
	if (ret)
		return dev_err_probe(dev, ret, "missing regulator-name\n");

	ret = of_property_read_u32(np, "regulator-min-microvolt",
				   &reg->voltage);
	if (ret)
		return dev_err_probe(dev, ret, "missing fixed voltage\n");

	reg->cl.dev = dev;
	reg->cl.rx_callback = hi3670_lpm_reg_rx;
	reg->cl.tx_block = false;

	reg->mbox = mbox_request_channel(&reg->cl, 0);
	if (IS_ERR(reg->mbox))
		return dev_err_probe(dev, PTR_ERR(reg->mbox),
				     "failed to request LPM3 mailbox\n");

	init_completion(&reg->ack);
	mutex_init(&reg->lock);

	reg->rdesc.name = name;
	reg->rdesc.type = REGULATOR_VOLTAGE;
	reg->rdesc.owner = THIS_MODULE;
	reg->rdesc.ops = &hi3670_lpm_reg_ops;

	config.dev = dev;
	config.of_node = np;
	config.driver_data = reg;

	reg->rdev = devm_regulator_register(dev, &reg->rdesc, &config);
	if (IS_ERR(reg->rdev)) {
		ret = PTR_ERR(reg->rdev);
		mbox_free_channel(reg->mbox);
		return dev_err_probe(dev, ret, "failed to register regulator\n");
	}

	platform_set_drvdata(pdev, reg);

	return 0;
}

static void hi3670_lpm_reg_remove(struct platform_device *pdev)
{
	struct hi3670_lpm_reg *reg = platform_get_drvdata(pdev);

	mbox_free_channel(reg->mbox);
}

static const struct of_device_id hi3670_lpm_reg_of_match[] = {
	{ .compatible = "hisilicon,hi3670-lpm-regulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3670_lpm_reg_of_match);

static struct platform_driver hi3670_lpm_reg_driver = {
	.probe	= hi3670_lpm_reg_probe,
	.remove	= hi3670_lpm_reg_remove,
	.driver	= {
		.name = "hi3670-lpm-regulator",
		.of_match_table = hi3670_lpm_reg_of_match,
	},
};
module_platform_driver(hi3670_lpm_reg_driver);

MODULE_DESCRIPTION("HiSilicon Hi3670 LPM3 regulator");
MODULE_LICENSE("GPL");
