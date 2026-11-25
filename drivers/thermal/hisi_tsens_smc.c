// SPDX-License-Identifier: GPL-2.0-only
/*
 * Temperature sensor driver for the HiSilicon Kirin970 (Hi3670)
 *
 * The TSENSOR block is only reachable from the secure firmware, so the
 * vendor kernel reads it through an SMC call.  Use the same interface and
 * expose the sensors through the generic thermal framework.
 */

#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/thermal.h>

#define HI3670_TSENS_READ_SMC	0xc5009900UL

#define HI3670_TSENS_NUM_SENSORS	4
#define HI3670_TSENS_TEMP_MIN		(-40)
#define HI3670_TSENS_TEMP_MAX		125

struct hi3670_tsens;

struct hi3670_tsens_sensor {
	struct hi3670_tsens *tsens;
	u32 id;
};

struct hi3670_tsens {
	struct device *dev;
	u32 adc_start;
	u32 adc_end;
	struct hi3670_tsens_sensor sensors[];
};

static int hi3670_tsens_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct hi3670_tsens_sensor *sensor = thermal_zone_device_priv(tz);
	struct hi3670_tsens *tsens = sensor->tsens;
	struct arm_smccc_res res;
	int code;

	arm_smccc_smc(HI3670_TSENS_READ_SMC, sensor->id, 0, 0, 0, 0, 0, 0, &res);
	code = (int)res.a0;

	if (code < tsens->adc_start || code > tsens->adc_end)
		return -EINVAL;

	*temp = ((code - tsens->adc_start) *
		 (HI3670_TSENS_TEMP_MAX - HI3670_TSENS_TEMP_MIN) /
		 (tsens->adc_end - tsens->adc_start) +
		 HI3670_TSENS_TEMP_MIN) * 1000;

	return 0;
}

static const struct thermal_zone_device_ops hi3670_tsens_ops = {
	.get_temp = hi3670_tsens_get_temp,
};

static int hi3670_tsens_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hi3670_tsens *tsens;
	int i, ret, registered = 0;

	tsens = devm_kzalloc(dev, struct_size(tsens, sensors,
					      HI3670_TSENS_NUM_SENSORS),
			     GFP_KERNEL);
	if (!tsens)
		return -ENOMEM;

	tsens->dev = dev;

	ret = of_property_read_u32(dev->of_node, "hisilicon,adc-start-value",
				   &tsens->adc_start);
	if (ret)
		return dev_err_probe(dev, ret, "missing adc-start-value\n");

	ret = of_property_read_u32(dev->of_node, "hisilicon,adc-end-value",
				   &tsens->adc_end);
	if (ret)
		return dev_err_probe(dev, ret, "missing adc-end-value\n");

	if (tsens->adc_end <= tsens->adc_start)
		return dev_err_probe(dev, -EINVAL, "invalid ADC range\n");

	for (i = 0; i < HI3670_TSENS_NUM_SENSORS; i++) {
		struct hi3670_tsens_sensor *sensor = &tsens->sensors[i];
		struct thermal_zone_device *tz;

		sensor->tsens = tsens;
		sensor->id = i;

		tz = devm_thermal_of_zone_register(dev, i, sensor,
						   &hi3670_tsens_ops);
		if (IS_ERR(tz)) {
			ret = PTR_ERR(tz);
			if (ret == -ENODEV) {
				dev_dbg(dev, "sensor %d has no thermal zone\n",
					i);
				continue;
			}
			return dev_err_probe(dev, ret,
					     "failed to register sensor %d\n", i);
		}

		registered++;
	}

	if (!registered)
		return dev_err_probe(dev, -ENODEV, "no thermal zones defined\n");

	return 0;
}

static const struct of_device_id hi3670_tsens_of_match[] = {
	{ .compatible = "hisilicon,hi3670-tsensor" },
	{}
};
MODULE_DEVICE_TABLE(of, hi3670_tsens_of_match);

static struct platform_driver hi3670_tsens_driver = {
	.probe = hi3670_tsens_probe,
	.driver = {
		.name = "hi3670-tsens",
		.of_match_table = hi3670_tsens_of_match,
	},
};
module_platform_driver(hi3670_tsens_driver);

MODULE_DESCRIPTION("HiSilicon Kirin970 TSENSOR driver");
MODULE_LICENSE("GPL");
