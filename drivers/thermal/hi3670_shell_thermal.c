// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shell and ambient thermal zones for the HiSilicon Kirin970 (Hi3670).
 *
 * The vendor kernel exposes three weighted board temperature zones plus one
 * ambient zone.  The weights are supplied by the board device tree and the
 * raw inputs come from the HKADC IIO driver and the battery power supply.
 */

#include <linux/err.h>
#include <linux/iio/consumer.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/thermal.h>

#define HI3670_THERMAL_DEFAULT_INTERVAL_MS	5000
#define HI3670_THERMAL_DEFAULT_SAMPLES		1
#define HI3670_THERMAL_INVALID_TEMP		125000
#define HI3670_THERMAL_MIN_TEMP			(-40000)
#define HI3670_THERMAL_MAX_TEMP			125000
#define HI3670_THERMAL_COEF_SCALE		100000

struct hi3670_ntc_point {
	int millivolt;
	int temp_mc;
};

struct hi3670_ntc_table {
	const struct hi3670_ntc_point *points;
	int count;
};

static const struct hi3670_ntc_point hi3670_ntc_soc_points[] = {
	{ 44, 125000 }, { 46, 124000 }, { 47, 123000 }, { 48, 122000 }, { 50, 121000 },
	{ 51, 120000 }, { 53, 119000 }, { 54, 118000 }, { 56, 117000 }, { 57, 116000 },
	{ 59, 115000 }, { 61, 114000 }, { 62, 113000 }, { 64, 112000 }, { 66, 111000 },
	{ 68, 110000 }, { 70, 109000 }, { 72, 108000 }, { 74, 107000 }, { 77, 106000 },
	{ 79, 105000 }, { 81, 104000 }, { 84, 103000 }, { 86, 102000 }, { 89, 101000 },
	{ 92, 100000 }, { 95, 99000 }, { 98, 98000 }, { 101, 97000 }, { 104, 96000 },
	{ 107, 95000 }, { 110, 94000 }, { 114, 93000 }, { 118, 92000 }, { 121, 91000 },
	{ 125, 90000 }, { 129, 89000 }, { 133, 88000 }, { 137, 87000 }, { 142, 86000 },
	{ 146, 85000 }, { 151, 84000 }, { 156, 83000 }, { 161, 82000 }, { 166, 81000 },
	{ 172, 80000 }, { 177, 79000 }, { 183, 78000 }, { 189, 77000 }, { 195, 76000 },
	{ 201, 75000 }, { 208, 74000 }, { 215, 73000 }, { 222, 72000 }, { 229, 71000 },
	{ 237, 70000 }, { 245, 69000 }, { 253, 68000 }, { 261, 67000 }, { 269, 66000 },
	{ 278, 65000 }, { 287, 64000 }, { 297, 63000 }, { 306, 62000 }, { 316, 61000 },
	{ 327, 60000 }, { 337, 59000 }, { 348, 58000 }, { 360, 57000 }, { 371, 56000 },
	{ 383, 55000 }, { 395, 54000 }, { 408, 53000 }, { 421, 52000 }, { 434, 51000 },
	{ 448, 50000 }, { 462, 49000 }, { 477, 48000 }, { 491, 47000 }, { 506, 46000 },
	{ 522, 45000 }, { 538, 44000 }, { 554, 43000 }, { 571, 42000 }, { 588, 41000 },
	{ 605, 40000 }, { 623, 39000 }, { 641, 38000 }, { 659, 37000 }, { 677, 36000 },
	{ 696, 35000 }, { 716, 34000 }, { 735, 33000 }, { 755, 32000 }, { 775, 31000 },
	{ 795, 30000 }, { 816, 29000 }, { 836, 28000 }, { 857, 27000 }, { 878, 26000 },
	{ 900, 25000 }, { 921, 24000 }, { 942, 23000 }, { 964, 22000 }, { 985, 21000 },
	{ 1007, 20000 }, { 1028, 19000 }, { 1050, 18000 }, { 1071, 17000 }, { 1093, 16000 },
	{ 1114, 15000 }, { 1135, 14000 }, { 1156, 13000 }, { 1177, 12000 }, { 1197, 11000 },
	{ 1218, 10000 }, { 1238, 9000 }, { 1258, 8000 }, { 1277, 7000 }, { 1296, 6000 },
	{ 1315, 5000 }, { 1334, 4000 }, { 1352, 3000 }, { 1370, 2000 }, { 1387, 1000 },
	{ 1404, 0 }, { 1421, -1000 }, { 1437, -2000 }, { 1453, -3000 }, { 1468, -4000 },
	{ 1483, -5000 }, { 1498, -6000 }, { 1512, -7000 }, { 1525, -8000 }, { 1538, -9000 },
	{ 1551, -10000 }, { 1563, -11000 }, { 1575, -12000 }, { 1586, -13000 }, { 1597, -14000 },
	{ 1608, -15000 }, { 1618, -16000 }, { 1627, -17000 }, { 1636, -18000 }, { 1645, -19000 },
	{ 1654, -20000 }, { 1662, -21000 }, { 1670, -22000 }, { 1677, -23000 }, { 1684, -24000 },
	{ 1691, -25000 }, { 1697, -26000 }, { 1703, -27000 }, { 1709, -28000 }, { 1714, -29000 },
	{ 1719, -30000 }, { 1724, -31000 }, { 1729, -32000 }, { 1733, -33000 }, { 1738, -34000 },
	{ 1742, -35000 }, { 1745, -36000 }, { 1749, -37000 }, { 1752, -38000 }, { 1755, -39000 },
	{ 1758, -40000 },
};

static const struct hi3670_ntc_point hi3670_ntc_dcxo_points[] = {
	{ 51, 120000 }, { 59, 115000 }, { 68, 110000 }, { 79, 105000 }, { 92, 100000 },
	{ 107, 95000 }, { 125, 90000 }, { 146, 85000 }, { 172, 80000 }, { 201, 75000 },
	{ 237, 70000 }, { 278, 65000 }, { 327, 60000 }, { 383, 55000 }, { 448, 50000 },
	{ 522, 45000 }, { 605, 40000 }, { 696, 35000 }, { 795, 30000 }, { 900, 25000 },
	{ 1007, 20000 }, { 1114, 15000 }, { 1218, 10000 }, { 1316, 5000 }, { 1405, 0 },
	{ 1484, -5000 }, { 1552, -10000 }, { 1608, -15000 }, { 1655, -20000 }, { 1692, -25000 },
	{ 1720, -30000 }, { 1742, -35000 },
};

static const struct hi3670_ntc_table hi3670_ntc_soc = {
	.points = hi3670_ntc_soc_points,
	.count = ARRAY_SIZE(hi3670_ntc_soc_points),
};

static const struct hi3670_ntc_table hi3670_ntc_dcxo = {
	.points = hi3670_ntc_dcxo_points,
	.count = ARRAY_SIZE(hi3670_ntc_dcxo_points),
};

enum hi3670_sensor_source {
	HI3670_SRC_IIO,
	HI3670_SRC_BATTERY,
	HI3670_SRC_THERMAL_ZONE,
};

struct hi3670_thermal_sensor {
	const char *name;
	enum hi3670_sensor_source source;
	struct iio_channel *channel;
	const struct hi3670_ntc_table *table;
	s32 *coef;
	int *history;
	bool has_coef;
};

struct hi3670_thermal {
	struct device *dev;
	struct thermal_zone_device *tz;
	unsigned int interval_ms;
	int bias_mc;
	int step_mc;
	int step_range_mc;
	int sample_count;
	int sensor_count;
	int index;
	int samples;
	int valid_samples;
	int last_temp;
	bool has_temp;
	bool ambient;
	bool weighted;
	struct hi3670_thermal_sensor *sensors;
};

static int hi3670_volt_to_temp_mc(int millivolt,
				  const struct hi3670_ntc_table *table)
{
	const struct hi3670_ntc_point *points = table->points;
	int i;

	if (millivolt <= points[0].millivolt)
		return points[0].temp_mc;

	if (millivolt >= points[table->count - 1].millivolt)
		return points[table->count - 1].temp_mc;

	for (i = 0; i < table->count - 1; i++) {
		const struct hi3670_ntc_point *lo = &points[i];
		const struct hi3670_ntc_point *hi = &points[i + 1];
		s64 delta;

		if (millivolt > hi->millivolt)
			continue;

		delta = (s64)(millivolt - lo->millivolt) *
			(hi->temp_mc - lo->temp_mc);
		return lo->temp_mc +
			div_s64(delta, hi->millivolt - lo->millivolt);
	}

	return points[table->count - 1].temp_mc;
}

static int hi3670_read_battery_temp(void)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int ret;

	psy = power_supply_get_by_name("Battery");
	if (!psy)
		psy = power_supply_get_by_name("battery");
	if (!psy)
		return -ENODEV;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &val);
	power_supply_put(psy);
	if (ret)
		return ret;

	return val.intval * 100;
}

static int hi3670_read_sensor(struct hi3670_thermal_sensor *sensor)
{
	struct thermal_zone_device *tz;
	int millivolt;
	int temp;
	int ret;

	switch (sensor->source) {
	case HI3670_SRC_IIO:
		ret = iio_read_channel_processed(sensor->channel, &millivolt);
		if (ret)
			return ret;
		return hi3670_volt_to_temp_mc(millivolt, sensor->table);

	case HI3670_SRC_BATTERY:
		return hi3670_read_battery_temp();

	case HI3670_SRC_THERMAL_ZONE:
		tz = thermal_zone_get_zone_by_name(sensor->name);
		if (IS_ERR(tz))
			return PTR_ERR(tz);
		ret = thermal_zone_get_temp(tz, &temp);
		if (ret)
			return ret;
		return temp;
	}

	return -EINVAL;
}

static int hi3670_weighted_temp(struct hi3670_thermal *priv, int index)
{
	s64 sum = 0;
	int i, j;

	for (i = 0; i < priv->sensor_count; i++) {
		struct hi3670_thermal_sensor *sensor = &priv->sensors[i];

		for (j = 0; j < priv->sample_count; j++) {
			int pos = index - j;

			if (pos < 0)
				pos += priv->sample_count;
			sum += (s64)sensor->coef[j] * sensor->history[pos];
		}
	}

	return div_s64(sum, HI3670_THERMAL_COEF_SCALE) + priv->bias_mc;
}

static int hi3670_shell_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct hi3670_thermal *priv = thermal_zone_device_priv(tz);
	int valid_count = 0;
	int invalid_count = 0;
	int sum = 0;
	int i;

	for (i = 0; i < priv->sensor_count; i++) {
		struct hi3670_thermal_sensor *sensor = &priv->sensors[i];
		int value;

		value = hi3670_read_sensor(sensor);

		if (value < HI3670_THERMAL_MIN_TEMP ||
		    value > HI3670_THERMAL_MAX_TEMP) {
			sensor->history[priv->index] = HI3670_THERMAL_INVALID_TEMP;
			invalid_count++;
			continue;
		}

		sensor->history[priv->index] = value;
		sum += value;
		valid_count++;
	}

	if (!valid_count) {
		priv->valid_samples = 0;
		if (!priv->has_temp)
			return -EAGAIN;
		*temp = priv->last_temp;
		return 0;
	}

	if (invalid_count) {
		priv->valid_samples = 0;
		*temp = sum / valid_count + priv->bias_mc;
	} else {
		priv->valid_samples++;
		if (priv->weighted &&
		    priv->valid_samples > priv->sample_count)
			*temp = hi3670_weighted_temp(priv, priv->index);
		else
			*temp = sum / valid_count + priv->bias_mc;
	}

	if (priv->has_temp) {
		int delta = *temp - priv->last_temp;

		if (delta > priv->step_range_mc)
			*temp = priv->last_temp + priv->step_mc;
		else if (delta < -priv->step_range_mc)
			*temp = priv->last_temp - priv->step_mc;
	}

	priv->last_temp = *temp;
	priv->has_temp = true;
	priv->samples++;
	priv->index = (priv->index + 1) % priv->sample_count;

	return 0;
}

static int hi3670_ambient_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct hi3670_thermal *priv = thermal_zone_device_priv(tz);
	int min_temp = HI3670_THERMAL_MAX_TEMP;
	int valid = 0;
	int i;

	for (i = 0; i < priv->sensor_count; i++) {
		int value;

		value = hi3670_read_sensor(&priv->sensors[i]);
		if (value < HI3670_THERMAL_MIN_TEMP ||
		    value > HI3670_THERMAL_MAX_TEMP)
			continue;

		if (!valid || value < min_temp)
			min_temp = value;
		valid = 1;
	}

	if (!valid)
		return -EAGAIN;

	*temp = min_temp - priv->bias_mc;
	return 0;
}

static const struct thermal_zone_device_ops hi3670_shell_ops = {
	.get_temp = hi3670_shell_get_temp,
};

static const struct thermal_zone_device_ops hi3670_ambient_ops = {
	.get_temp = hi3670_ambient_get_temp,
};

static int hi3670_thermal_parse_sensor(struct hi3670_thermal *priv,
				       struct device_node *np,
				       struct hi3670_thermal_sensor *sensor)
{
	int ret;
	int i;

	ret = of_property_read_string(np, "type", &sensor->name);
	if (ret)
		return ret;

	if (!strcmp(sensor->name, "Battery")) {
		sensor->source = HI3670_SRC_BATTERY;
	} else if (!strcmp(sensor->name, "system_h") ||
		   !strcmp(sensor->name, "charger") ||
		   !strcmp(sensor->name, "pa_0")) {
		sensor->source = HI3670_SRC_IIO;
		sensor->table = &hi3670_ntc_soc;
	} else if (!strcmp(sensor->name, "dcxo0")) {
		sensor->source = HI3670_SRC_IIO;
		sensor->table = &hi3670_ntc_dcxo;
	} else {
		sensor->source = HI3670_SRC_THERMAL_ZONE;
	}

	if (sensor->source == HI3670_SRC_IIO) {
		sensor->channel = devm_iio_channel_get(priv->dev, sensor->name);
		if (IS_ERR(sensor->channel))
			return dev_err_probe(priv->dev, PTR_ERR(sensor->channel),
					     "missing IIO channel %s\n",
					     sensor->name);
	}

	sensor->coef = devm_kcalloc(priv->dev, priv->sample_count,
				    sizeof(*sensor->coef), GFP_KERNEL);
	sensor->history = devm_kcalloc(priv->dev, priv->sample_count,
				       sizeof(*sensor->history), GFP_KERNEL);
	if (!sensor->coef || !sensor->history)
		return -ENOMEM;

	if (!priv->ambient) {
		ret = of_property_read_u32_array(np, "coef",
						 (u32 *)sensor->coef,
						 priv->sample_count);
		if (ret)
			return dev_err_probe(priv->dev, ret,
					     "invalid coef for %s\n",
					     sensor->name);
	}

	for (i = 0; i < priv->sample_count; i++) {
		if (sensor->coef[i])
			sensor->has_coef = true;
	}

	return 0;
}

static int hi3670_thermal_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *sensors_np;
	struct hi3670_thermal *priv;
	struct hi3670_thermal_sensor *sensor;
	const struct thermal_zone_device_ops *ops;
	unsigned int sample_count;
	u32 value;
	int sensor_count;
	int ret;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->ambient = (bool)of_device_get_match_data(dev);

	ret = device_property_read_u32(dev, "interval", &priv->interval_ms);
	if (ret)
		priv->interval_ms = of_device_get_match_data(dev) ? 900 :
			HI3670_THERMAL_DEFAULT_INTERVAL_MS;

	ret = device_property_read_u32(dev, "bias", &value);
	if (ret)
		priv->bias_mc = 0;
	else
		priv->bias_mc = value;

	if (priv->ambient) {
		priv->bias_mc *= 1000;
		priv->interval_ms *= 1000;
		priv->sample_count = HI3670_THERMAL_DEFAULT_SAMPLES;
	} else {
		ret = device_property_read_u32(dev, "count", &sample_count);
		if (ret)
			sample_count = HI3670_THERMAL_DEFAULT_SAMPLES;
		if (!sample_count)
			return -EINVAL;
		priv->sample_count = sample_count;

		ret = device_property_read_u32(dev, "shell-temp-step", &value);
		if (ret)
			priv->step_mc = 0;
		else
			priv->step_mc = value;

		ret = device_property_read_u32(dev, "shell-temp-step-range",
					       &value);
		if (ret)
			priv->step_range_mc = 0;
		else
			priv->step_range_mc = value;

		priv->step_mc = priv->step_mc * priv->interval_ms / 1000;
		priv->step_range_mc = priv->step_range_mc *
			priv->interval_ms / 1000;
	}

	sensors_np = of_get_child_by_name(dev->of_node, "sensors");
	if (!sensors_np)
		return dev_err_probe(dev, -ENODEV, "missing sensors node\n");

	sensor_count = of_get_child_count(sensors_np);
	if (!sensor_count) {
		of_node_put(sensors_np);
		return dev_err_probe(dev, -EINVAL, "no sensors\n");
	}

	priv->sensors = devm_kcalloc(dev, sensor_count, sizeof(*priv->sensors),
				     GFP_KERNEL);
	if (!priv->sensors) {
		of_node_put(sensors_np);
		return -ENOMEM;
	}
	priv->sensor_count = sensor_count;

	sensor = priv->sensors;
	for_each_child_of_node_scoped(sensors_np, child) {
		ret = hi3670_thermal_parse_sensor(priv, child, sensor);
		if (ret) {
			of_node_put(sensors_np);
			return ret;
		}
		sensor++;
	}
	of_node_put(sensors_np);

	for (i = 0; i < priv->sensor_count; i++) {
		if (priv->sensors[i].has_coef)
			priv->weighted = true;
	}

	ops = priv->ambient ? &hi3670_ambient_ops : &hi3670_shell_ops;
	priv->tz = thermal_zone_device_register_with_trips(dev_name(dev), NULL, 0,
							   priv, ops, NULL, 0,
							   priv->interval_ms);
	if (IS_ERR(priv->tz))
		return dev_err_probe(dev, PTR_ERR(priv->tz),
				     "failed to register thermal zone\n");

	/* a freshly registered zone is disabled until explicitly enabled */
	ret = thermal_zone_device_enable(priv->tz);
	if (ret) {
		thermal_zone_device_unregister(priv->tz);
		return dev_err_probe(dev, ret, "failed to enable thermal zone\n");
	}

	platform_set_drvdata(pdev, priv);
	return 0;
}

static void hi3670_thermal_remove(struct platform_device *pdev)
{
	struct hi3670_thermal *priv = platform_get_drvdata(pdev);

	thermal_zone_device_unregister(priv->tz);
}

static const struct of_device_id hi3670_thermal_of_match[] = {
	{ .compatible = "hisilicon,hi3670-shell-temp", .data = (void *)0 },
	{ .compatible = "hisilicon,hi3670-ambient-temp", .data = (void *)1 },
	{}
};
MODULE_DEVICE_TABLE(of, hi3670_thermal_of_match);

static struct platform_driver hi3670_thermal_driver = {
	.probe = hi3670_thermal_probe,
	.remove = hi3670_thermal_remove,
	.driver = {
		.name = "hi3670-shell-thermal",
		.of_match_table = hi3670_thermal_of_match,
	},
};
module_platform_driver(hi3670_thermal_driver);

MODULE_DESCRIPTION("HiSilicon Kirin970 shell and ambient thermal zones");
MODULE_LICENSE("GPL");
