// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the AK8789 Hall-effect sensor on the Honor Play (COR-AL00,
 * Hi3670 / Kirin 970).
 *
 * The ak8789 is a Hall switch wired straight to AP GPIOs (one per magnetic
 * pole).  Unlike the accel/gyro/mag/als sensors, it does NOT sit behind the
 * IOM3 MCU: the AP reads it directly, so this is an ordinary platform driver
 * with a GPIO interrupt.  A magnet near a pole pulls its line low; the driver
 * reports the resulting bit field as an EV_MSC/MSC_SCAN event, exactly as the
 * vendor's HALL_DATA_REPORT_INPUTDEV path does.
 *
 * Ported from the vendor tree's drivers/huawei_platform/sensor/hall/ak8789.c,
 * stripped of the private inputhub / DSM / wakelock framework.  Only the north
 * pole is wired on COR-AL00 (huawei,type = 1 in the vendor DT), but the south
 * pole is handled too so the driver is complete for any board.
 *
 * Expected DT:
 *   hall: hall {
 *       compatible = "hisilicon,ak8789-hall";
 *       interrupt-parent = <&gpioN>;
 *       interrupts = <line IRQ_TYPE_...>;
 *       north-gpios = <&gpioN line flags>;
 *       south-gpios = <&gpioM line flags>;   (optional)
 *       status = "okay";
 *   };
 * On COR-AL00 the part is not populated, so the board keeps the node disabled;
 * the driver is complete and binds as soon as a populated board enables it.
 */

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/property.h>

#define DRIVER_NAME "ak8789-hall"

/* hall_type bit fields: which poles this channel has wired. */
#define HALL_TYPE_NORTH		BIT(0)
#define HALL_TYPE_SOUTH		BIT(1)

/* Debounce: the vendor queues the report after huawei,int-delay-time (default
 * 50 ms) to let the reed switch settle before sampling it.
 */
#define HALL_REPORT_DELAY	msecs_to_jiffies(50)

struct ak8789 {
	struct device *dev;
	struct input_dev *input;
	struct gpio_desc *north;
	struct gpio_desc *south;
	unsigned int type;		/* HALL_TYPE_NORTH / _SOUTH mask */
	struct delayed_work work;
};

/*
 * Sample the wired poles and pack them into a 2-bit field: bit 0 = north
 * closed, bit 1 = south closed.  The GPIOs are declared GPIO_ACTIVE_LOW in
 * DT, so gpiod_get_value_cansleep() already returns 1 for "closed" (line
 * driven low); no extra inversion is needed.
 */
static int ak8789_read(struct ak8789 *hall)
{
	int value = 0;

	if (hall->type & HALL_TYPE_NORTH)
		value |= gpiod_get_value_cansleep(hall->north);
	if (hall->type & HALL_TYPE_SOUTH)
		value |= (gpiod_get_value_cansleep(hall->south) << 1);

	return value;
}

static void ak8789_report(struct work_struct *work)
{
	struct ak8789 *hall = container_of(to_delayed_work(work),
					   struct ak8789, work);
	int value;

	value = ak8789_read(hall);
	input_event(hall->input, EV_MSC, MSC_SCAN, value);
	input_sync(hall->input);
}

/*
 * The vendor toggles the IRQ edge on every interrupt so the opposite
 * transition re-arms it.  We instead declare both edges up front
 * (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING), so here we only need to
 * schedule the debounced report.
 */
static irqreturn_t ak8789_isr(int irq, void *dev_id)
{
	struct ak8789 *hall = dev_id;

	pm_wakeup_dev_event(hall->dev, 0, false);
	schedule_delayed_work(&hall->work, HALL_REPORT_DELAY);

	return IRQ_HANDLED;
}

static int ak8789_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ak8789 *hall;
	int ret, irq;

	hall = devm_kzalloc(dev, sizeof(*hall), GFP_KERNEL);
	if (!hall)
		return -ENOMEM;

	hall->dev = dev;
	INIT_DELAYED_WORK(&hall->work, ak8789_report);

	/*
	 * North is mandatory; south is optional.  The vendor reads huawei,type
	 * to decide which poles to touch; we instead probe from what the DT
	 * actually wires, which is equivalent and self-describing.
	 */
	hall->north = devm_gpiod_get(dev, "north", GPIOD_IN);
	if (IS_ERR(hall->north))
		return dev_err_probe(dev, PTR_ERR(hall->north),
				     "failed to get north-gpios\n");
	hall->type |= HALL_TYPE_NORTH;

	hall->south = devm_gpiod_get_optional(dev, "south", GPIOD_IN);
	if (IS_ERR(hall->south))
		return dev_err_probe(dev, PTR_ERR(hall->south),
				     "failed to get south-gpios\n");
	if (hall->south)
		hall->type |= HALL_TYPE_SOUTH;

	hall->input = devm_input_allocate_device(dev);
	if (!hall->input)
		return -ENOMEM;

	hall->input->name = "ak8789 hall";
	hall->input->id.bustype = BUS_HOST;
	input_set_capability(hall->input, EV_MSC, MSC_SCAN);

	ret = input_register_device(hall->input);
	if (ret) {
		dev_err(dev, "register input failed: %d\n", ret);
		return ret;
	}

	/*
	 * One IRQ per wired pole, both edges.  The north line carries the hall
	 * name in the vendor driver; here the north IRQ is the primary one and
	 * the south (if present) gets its own.
	 */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq < 0) {
		if (irq != -ENXIO)
			return dev_err_probe(dev, irq, "no north IRQ\n");
	} else {
		ret = devm_request_any_context_irq(dev, irq, ak8789_isr,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"ak8789-north", hall);
		if (ret < 0)
			return dev_err_probe(dev, ret, "request north IRQ failed\n");
	}

	if (hall->south) {
		irq = platform_get_irq_optional(pdev, 1);
		if (irq > 0) {
			ret = devm_request_any_context_irq(dev, irq,
					ak8789_isr,
					IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
					"ak8789-south", hall);
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "request south IRQ failed\n");
		}
	}

	device_init_wakeup(dev, true);
	platform_set_drvdata(pdev, hall);

	dev_info(dev, "AK8789 hall sensor registered (type 0x%x)\n", hall->type);
	return 0;
}

static void ak8789_remove(struct platform_device *pdev)
{
	struct ak8789 *hall = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&hall->work);
	device_init_wakeup(hall->dev, false);
}

static const struct of_device_id ak8789_of_match[] = {
	{ .compatible = "hisilicon,ak8789-hall" },
	{ }
};
MODULE_DEVICE_TABLE(of, ak8789_of_match);

static struct platform_driver ak8789_driver = {
	.probe = ak8789_probe,
	.remove = ak8789_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ak8789_of_match,
	},
};
module_platform_driver(ak8789_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Claude");
MODULE_DESCRIPTION("AK8789 Hall sensor driver (Honor Play / Hi3670)");
