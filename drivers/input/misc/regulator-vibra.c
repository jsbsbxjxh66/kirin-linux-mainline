// SPDX-License-Identifier: GPL-2.0+
/*
 * Regulator vibrator driver
 *
 * Copyright (C) 2024
 *
 * Based on the GPIO vibrator driver:
 *  Copyright (C) 2019 Luca Weiss <luca@z3ntu.xyz>
 */

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct regulator_vibrator {
	struct input_dev *input;
	struct regulator *vcc;

	struct work_struct play_work;
	bool running;
	bool vcc_on;
};

static int regulator_vibrator_start(struct regulator_vibrator *vibrator)
{
	struct device *dev = vibrator->input->dev.parent;
	int err;

	if (!vibrator->vcc_on) {
		err = regulator_enable(vibrator->vcc);
		if (err) {
			dev_err(dev, "failed to enable regulator: %d\n", err);
			return err;
		}
		vibrator->vcc_on = true;
	}

	return 0;
}

static void regulator_vibrator_stop(struct regulator_vibrator *vibrator)
{
	if (vibrator->vcc_on) {
		regulator_disable(vibrator->vcc);
		vibrator->vcc_on = false;
	}
}

static void regulator_vibrator_play_work(struct work_struct *work)
{
	struct regulator_vibrator *vibrator =
		container_of(work, struct regulator_vibrator, play_work);

	if (vibrator->running)
		regulator_vibrator_start(vibrator);
	else
		regulator_vibrator_stop(vibrator);
}

static int regulator_vibrator_play_effect(struct input_dev *dev, void *data,
					  struct ff_effect *effect)
{
	struct regulator_vibrator *vibrator = input_get_drvdata(dev);
	int level;

	level = effect->u.rumble.strong_magnitude;
	if (!level)
		level = effect->u.rumble.weak_magnitude;

	vibrator->running = level;
	schedule_work(&vibrator->play_work);

	return 0;
}

static void regulator_vibrator_close(struct input_dev *input)
{
	struct regulator_vibrator *vibrator = input_get_drvdata(input);

	cancel_work_sync(&vibrator->play_work);
	regulator_vibrator_stop(vibrator);
	vibrator->running = false;
}

static int regulator_vibrator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_vibrator *vibrator;
	u32 voltage;
	int err;

	vibrator = devm_kzalloc(dev, sizeof(*vibrator), GFP_KERNEL);
	if (!vibrator)
		return -ENOMEM;

	vibrator->input = devm_input_allocate_device(dev);
	if (!vibrator->input)
		return -ENOMEM;

	vibrator->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(vibrator->vcc))
		return dev_err_probe(dev, PTR_ERR(vibrator->vcc),
				     "Failed to request regulator\n");

	if (!device_property_read_u32(dev, "vibrator-voltage-microvolt",
				      &voltage)) {
		err = regulator_set_voltage(vibrator->vcc, voltage, voltage);
		if (err)
			return dev_err_probe(dev, err,
					     "Failed to set vibrator voltage\n");
	}

	INIT_WORK(&vibrator->play_work, regulator_vibrator_play_work);

	vibrator->input->name = "regulator-vibrator";
	vibrator->input->id.bustype = BUS_HOST;
	vibrator->input->close = regulator_vibrator_close;

	input_set_drvdata(vibrator->input, vibrator);
	input_set_capability(vibrator->input, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(vibrator->input, NULL,
				      regulator_vibrator_play_effect);
	if (err) {
		dev_err(dev, "Couldn't create FF dev: %d\n", err);
		return err;
	}

	err = input_register_device(vibrator->input);
	if (err) {
		dev_err(dev, "Couldn't register input dev: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, vibrator);

	return 0;
}

static int regulator_vibrator_suspend(struct device *dev)
{
	struct regulator_vibrator *vibrator = dev_get_drvdata(dev);

	cancel_work_sync(&vibrator->play_work);
	if (vibrator->running)
		regulator_vibrator_stop(vibrator);

	return 0;
}

static int regulator_vibrator_resume(struct device *dev)
{
	struct regulator_vibrator *vibrator = dev_get_drvdata(dev);

	if (vibrator->running)
		regulator_vibrator_start(vibrator);

	return 0;
}

static void regulator_vibrator_remove(struct platform_device *pdev)
{
	struct regulator_vibrator *vibrator = platform_get_drvdata(pdev);

	/* Drain any queued play_work before devm frees vibrator. */
	cancel_work_sync(&vibrator->play_work);
	if (vibrator->running)
		regulator_vibrator_stop(vibrator);
}

static DEFINE_SIMPLE_DEV_PM_OPS(regulator_vibrator_pm_ops,
				regulator_vibrator_suspend,
				regulator_vibrator_resume);

static const struct of_device_id regulator_vibrator_of_match[] = {
	{ .compatible = "regulator-vibrator" },
	{}
};
MODULE_DEVICE_TABLE(of, regulator_vibrator_of_match);

static struct platform_driver regulator_vibrator_driver = {
	.probe = regulator_vibrator_probe,
	.remove = regulator_vibrator_remove,
	.driver = {
		.name = "regulator-vibrator",
		.pm = pm_sleep_ptr(&regulator_vibrator_pm_ops),
		.of_match_table = regulator_vibrator_of_match,
	},
};
module_platform_driver(regulator_vibrator_driver);

MODULE_DESCRIPTION("Regulator vibrator driver");
MODULE_LICENSE("GPL");
