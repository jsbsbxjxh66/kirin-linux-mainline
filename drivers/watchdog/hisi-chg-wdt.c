// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon charger watchdog driver.
 *
 * The block is an ARM SP805-compatible watchdog clocked by the always-on
 * 32.768 kHz oscillator.  It resets the SoC when the charger workqueue
 * stops kicking it.  The device is exposed through the generic watchdog
 * framework; the charge_{enable,feed,stop}_sys_wdt() entry points are kept
 * for the out-of-tree charger stack until that is replaced.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spinlock.h>
#include <linux/watchdog.h>

#define HISI_CHG_WDT_DEFAULT_TIMEOUT	120

#define WDTLOAD		0x000
#define WDTVALUE	0x004
#define WDTCONTROL	0x008
#define WDTINTCLR	0x00c
#define WDTRIS		0x010
#define WDTLOCK		0xc00

#define WDT_INT_ENABLE		BIT(0)
#define WDT_RESET_ENABLE	BIT(1)
#define WDT_ENABLE_MASK		(WDT_INT_ENABLE | WDT_RESET_ENABLE)
#define WDT_INT_MASK		BIT(0)

#define WDT_UNLOCK	0x1acce551
#define WDT_LOCK	0x00000001

struct hisi_chg_wdt {
	struct watchdog_device	wdd;
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	u64			rate;
	unsigned int		load;
	spinlock_t		lock;
};

static struct hisi_chg_wdt *hisi_chg_wdt_dev;

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

void charge_enable_sys_wdt(void);
void charge_feed_sys_wdt(unsigned int timeout);
void charge_stop_sys_wdt(void);

static const struct watchdog_info hisi_chg_wdt_info = {
	.options = WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "hisi-chg-wdt",
};

/*
 * The SP805 counts the loaded value down twice before asserting reset: the
 * first pass raises the interrupt, the second one resets the SoC.  Clearing
 * the interrupt would disable the reset, so the IRQ is intentionally left
 * unrequested and thus masked at the GIC.
 */
static void hisi_chg_wdt_write(struct hisi_chg_wdt *w, unsigned int load,
			       bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&w->lock, flags);
	writel_relaxed(WDT_UNLOCK, w->base + WDTLOCK);
	writel_relaxed(load, w->base + WDTLOAD);
	writel_relaxed(WDT_INT_MASK, w->base + WDTINTCLR);
	writel_relaxed(enable ? WDT_ENABLE_MASK : 0, w->base + WDTCONTROL);
	writel_relaxed(WDT_LOCK, w->base + WDTLOCK);
	readl_relaxed(w->base + WDTLOCK);
	w->load = load;
	spin_unlock_irqrestore(&w->lock, flags);
}

static unsigned int hisi_chg_wdt_load(struct hisi_chg_wdt *w,
				      unsigned int timeout)
{
	u64 load = div_u64(w->rate, 2) * timeout;

	if (!load)
		load = 1;
	if (load > U32_MAX)
		load = U32_MAX;

	return load;
}

static void hisi_chg_wdt_program(struct hisi_chg_wdt *w,
				 unsigned int timeout, bool enable)
{
	hisi_chg_wdt_write(w, hisi_chg_wdt_load(w, timeout), enable);
}

static int hisi_chg_wdt_set_timeout(struct watchdog_device *wdd,
				    unsigned int timeout)
{
	struct hisi_chg_wdt *w = watchdog_get_drvdata(wdd);
	unsigned long flags;
	u64 load;

	load = hisi_chg_wdt_load(w, timeout);

	spin_lock_irqsave(&w->lock, flags);
	w->load = load;
	wdd->timeout = div_u64((load + 1) * 2 + div_u64(w->rate, 2), w->rate);
	spin_unlock_irqrestore(&w->lock, flags);

	return 0;
}

static int hisi_chg_wdt_start(struct watchdog_device *wdd)
{
	struct hisi_chg_wdt *w = watchdog_get_drvdata(wdd);

	hisi_chg_wdt_program(w, wdd->timeout, true);

	return 0;
}

static int hisi_chg_wdt_ping(struct watchdog_device *wdd)
{
	struct hisi_chg_wdt *w = watchdog_get_drvdata(wdd);

	hisi_chg_wdt_program(w, wdd->timeout, true);

	return 0;
}

static int hisi_chg_wdt_stop(struct watchdog_device *wdd)
{
	struct hisi_chg_wdt *w = watchdog_get_drvdata(wdd);

	hisi_chg_wdt_write(w, w->load, false);

	return 0;
}

static unsigned int hisi_chg_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct hisi_chg_wdt *w = watchdog_get_drvdata(wdd);
	unsigned long flags;
	u64 value;

	spin_lock_irqsave(&w->lock, flags);
	value = readl_relaxed(w->base + WDTVALUE);
	if (!(readl_relaxed(w->base + WDTRIS) & WDT_INT_MASK))
		value += (u64)w->load + 1;
	spin_unlock_irqrestore(&w->lock, flags);

	return div_u64(value, w->rate);
}

static const struct watchdog_ops hisi_chg_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= hisi_chg_wdt_start,
	.stop		= hisi_chg_wdt_stop,
	.ping		= hisi_chg_wdt_ping,
	.set_timeout	= hisi_chg_wdt_set_timeout,
	.get_timeleft	= hisi_chg_wdt_get_timeleft,
};

void charge_enable_sys_wdt(void)
{
	struct hisi_chg_wdt *w = READ_ONCE(hisi_chg_wdt_dev);

	if (w)
		hisi_chg_wdt_program(w, w->wdd.timeout, true);
}
EXPORT_SYMBOL_GPL(charge_enable_sys_wdt);

void charge_feed_sys_wdt(unsigned int timeout)
{
	struct hisi_chg_wdt *w = READ_ONCE(hisi_chg_wdt_dev);
	u64 load;

	if (!w)
		return;

	/* Preserve the vendor semantics: reset after 2 * timeout seconds. */
	load = (u64)w->rate * timeout;
	if (!load)
		load = 1;
	if (load > U32_MAX)
		load = U32_MAX;

	hisi_chg_wdt_write(w, load, true);
}
EXPORT_SYMBOL_GPL(charge_feed_sys_wdt);

void charge_stop_sys_wdt(void)
{
	struct hisi_chg_wdt *w = READ_ONCE(hisi_chg_wdt_dev);

	if (w)
		hisi_chg_wdt_write(w, w->load, false);
}
EXPORT_SYMBOL_GPL(charge_stop_sys_wdt);

static int hisi_chg_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_chg_wdt *w;
	u64 max_timeout;
	u64 rate;
	int ret;

	w = devm_kzalloc(dev, sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;

	w->dev = dev;
	w->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(w->base))
		return PTR_ERR(w->base);

	w->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(w->clk))
		return dev_err_probe(dev, PTR_ERR(w->clk), "failed to get clock\n");

	if (w->clk) {
		ret = clk_prepare_enable(w->clk);
		if (ret)
			return dev_err_probe(dev, ret, "failed to enable clock\n");
		w->rate = clk_get_rate(w->clk);
	}

	if (!w->rate) {
		if (device_property_read_u64(dev, "clock-frequency", &rate))
			rate = 32768;
		w->rate = rate;
	}

	spin_lock_init(&w->lock);
	watchdog_set_drvdata(&w->wdd, w);
	w->wdd.info = &hisi_chg_wdt_info;
	w->wdd.ops = &hisi_chg_wdt_ops;
	w->wdd.parent = dev;
	w->wdd.min_timeout = 1;
	max_timeout = div_u64(U32_MAX, div_u64(w->rate, 2));
	w->wdd.max_timeout = min_t(u64, max_timeout, UINT_MAX);
	w->wdd.timeout = HISI_CHG_WDT_DEFAULT_TIMEOUT;
	watchdog_init_timeout(&w->wdd, 0, dev);
	hisi_chg_wdt_set_timeout(&w->wdd, w->wdd.timeout);

	watchdog_set_nowayout(&w->wdd, nowayout);
	watchdog_stop_on_reboot(&w->wdd);
	watchdog_stop_on_unregister(&w->wdd);

	hisi_chg_wdt_write(w, w->load, false);

	ret = watchdog_register_device(&w->wdd);
	if (ret)
		goto err_clk;

	platform_set_drvdata(pdev, w);
	WRITE_ONCE(hisi_chg_wdt_dev, w);

	dev_info(dev, "charger watchdog registered, %llu Hz\n", w->rate);

	return 0;

err_clk:
	clk_disable_unprepare(w->clk);
	return ret;
}

static void hisi_chg_wdt_remove(struct platform_device *pdev)
{
	struct hisi_chg_wdt *w = platform_get_drvdata(pdev);

	WRITE_ONCE(hisi_chg_wdt_dev, NULL);
	watchdog_unregister_device(&w->wdd);
	clk_disable_unprepare(w->clk);
}

static const struct of_device_id hisi_chg_wdt_of_match[] = {
	{ .compatible = "hisilicon,chgwdg" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_chg_wdt_of_match);

static struct platform_driver hisi_chg_wdt_driver = {
	.probe = hisi_chg_wdt_probe,
	.remove = hisi_chg_wdt_remove,
	.driver = {
		.name = "hisi-chg-wdt",
		.of_match_table = hisi_chg_wdt_of_match,
	},
};
module_platform_driver(hisi_chg_wdt_driver);

MODULE_DESCRIPTION("HiSilicon charger watchdog driver");
MODULE_LICENSE("GPL");
