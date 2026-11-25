// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon system counter driver
 *
 * The SoC system counter runs from the always-on clock and keeps counting
 * across CPU and system suspend.  Firmware and remote processors use it to
 * timestamp events; expose helpers to convert those timestamps to the
 * kernel's timespec64.
 *
 * Copyright (c) 2015 HiSilicon.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/syscore_ops.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>

#include <linux/hisi/hisi_syscounter.h>

static struct syscnt_device *syscnt_dev;

u64 hisi_get_syscount(void)
{
	struct syscnt_device *dev = syscnt_dev;
	u32 lo, hi, hi2;

	if (!dev || !dev->base)
		return 0;

	/*
	 * Read the high word twice so that a carry of the low word between
	 * the two reads cannot produce a torn value.
	 */
	do {
		hi = readl_relaxed(dev->base + SYSCOUNTER_H32);
		lo = readl_relaxed(dev->base + SYSCOUNTER_L32);
		hi2 = readl_relaxed(dev->base + SYSCOUNTER_H32);
	} while (hi != hi2);

	return ((u64)hi << 32) | lo;
}
EXPORT_SYMBOL_GPL(hisi_get_syscount);

int syscounter_to_timespec64(u64 syscnt, struct timespec64 *ts)
{
	struct syscnt_device *dev = syscnt_dev;
	struct syscnt_to_timespec_record record;
	unsigned long flags;
	s64 delta, sec, nsec;
	s32 rem;

	if (!ts)
		return -EINVAL;

	if (!dev || !dev->clock_rate)
		return -ENXIO;

	spin_lock_irqsave(&dev->sync_lock, flags);
	record = dev->record;
	spin_unlock_irqrestore(&dev->sync_lock, flags);

	delta = (s64)(syscnt - record.syscnt);
	sec = div_s64_rem(delta, (s32)dev->clock_rate, &rem);
	nsec = div_s64((s64)rem * NSEC_PER_SEC, (s32)dev->clock_rate);

	ts->tv_sec = record.ts.tv_sec + sec;
	ts->tv_nsec = record.ts.tv_nsec + nsec;

	if (ts->tv_nsec >= NSEC_PER_SEC) {
		ts->tv_sec++;
		ts->tv_nsec -= NSEC_PER_SEC;
	} else if (ts->tv_nsec < 0) {
		ts->tv_sec--;
		ts->tv_nsec += NSEC_PER_SEC;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(syscounter_to_timespec64);

static void hisi_syscounter_sync_record(void)
{
	struct syscnt_device *dev = syscnt_dev;
	unsigned long flags;

	if (!dev)
		return;

	spin_lock_irqsave(&dev->sync_lock, flags);
	ktime_get_boottime_ts64(&dev->record.ts);
	ktime_get_real_ts64(&dev->record.utc);
	dev->record.syscnt = hisi_get_syscount();
	spin_unlock_irqrestore(&dev->sync_lock, flags);
}

static int hisi_syscounter_suspend(void *data)
{
	hisi_syscounter_sync_record();

	return 0;
}

static void hisi_syscounter_resume(void *data)
{
	hisi_syscounter_sync_record();
}

static const struct syscore_ops hisi_syscounter_syscore_ops = {
	.suspend = hisi_syscounter_suspend,
	.resume = hisi_syscounter_resume,
};

static struct syscore hisi_syscounter_syscore = {
	.ops = &hisi_syscounter_syscore_ops,
};

static int hisi_syscounter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct syscnt_device *d;
	int ret;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	ret = of_property_read_u64(dev->of_node, "clock-rate", &d->clock_rate);
	if (ret || !d->clock_rate || d->clock_rate > S32_MAX) {
		dev_err(dev, "invalid or missing clock-rate\n");
		return -EINVAL;
	}

	d->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(d->base))
		return PTR_ERR(d->base);

	spin_lock_init(&d->r_lock);
	spin_lock_init(&d->sync_lock);

	syscnt_dev = d;
	hisi_syscounter_sync_record();
	register_syscore(&hisi_syscounter_syscore);

	dev_info(dev, "registered, clock rate %llu Hz\n", d->clock_rate);

	return 0;
}

static void hisi_syscounter_remove(struct platform_device *pdev)
{
	unregister_syscore(&hisi_syscounter_syscore);
	syscnt_dev = NULL;
}

static const struct of_device_id hisi_syscounter_of_match[] = {
	{ .compatible = "hisilicon,syscounter-driver" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_syscounter_of_match);

static struct platform_driver hisi_syscounter_driver = {
	.probe = hisi_syscounter_probe,
	.remove = hisi_syscounter_remove,
	.driver = {
		.name = "hisi-syscounter",
		.of_match_table = hisi_syscounter_of_match,
	},
};
module_platform_driver(hisi_syscounter_driver);

MODULE_AUTHOR("HiSilicon Co. Ltd");
MODULE_DESCRIPTION("HiSilicon system counter driver");
MODULE_LICENSE("GPL");
