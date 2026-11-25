// SPDX-License-Identifier: GPL-2.0
/*
 * PWM Controller Driver for HiSilicon Hi3670 (Kirin 970) BL-PWM
 *
 * The Kirin 970 display backlight is driven by a small PWM block that lives
 * in the SoC's always-on (IOMCU) region.  Unlike the HiSilicon BVT PWM cores
 * (see pwm-hibvt.c), this block is not a set of identical channels: it is a
 * single output with a fixed-width "brightness" register whose two halves
 * carry the high- and low-time of the waveform directly.  That makes it
 * useless for anything but a backlight, so it is exposed as a one-channel
 * PWM chip and driven from DT by the generic pwm-backlight driver.
 *
 * Register layout (offsets from the block base):
 *
 *   0x100  BLPWM_OUT_CTRL    1 = output enabled, 0 = output disabled
 *   0x104  BLPWM_OUT_DIV     clock prescaler, selects the resolution
 *   0x108  BLPWM_OUT_CFG     bits 31..16 = high time, bits 15..0 = low time
 *
 * The prescaler picks one of the two resolutions the hardware supports:
 *
 *   DIV = 0   -> 10000 steps per period   (BLPWM_OUT_PRECISION_10000)
 *   DIV = 2   ->  2048 steps per period   (BLPWM_OUT_PRECISION_2048)
 *   DIV = 1   ->  4096 steps per period   (BLPWM_OUT_PRECISION_4096)
 *
 * The COR-AL00 ships with a BOE TD4320 panel whose DT sets
 * bl-pwm-preci-type = <1> (BLPWM_PRECISION_10000_TYPE), i.e. the 10000-step
 * resolution with DIV = 0.  The default in this driver matches that; the
 * "hisilicon,blpwm-precision" property lets a different board pick another
 * resolution without touching the code.
 *
 * Copyright (C) 2026, Mainline port
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define HI3670_BLPWM_CTRL		0x100
#define HI3670_BLPWM_DIV		0x104
#define HI3670_BLPWM_CFG		0x108

#define HI3670_BLPWM_CTRL_ENABLE	BIT(0)

/* Prescaler values selecting each supported resolution. */
#define HI3670_BLPWM_DIV_10000		0
#define HI3670_BLPWM_DIV_2048		2
#define HI3670_BLPWM_DIV_4096		1

/* Number of PWM steps per period for each prescaler setting. */
#define HI3670_BLPWM_PRECISION_10000	10000
#define HI3670_BLPWM_PRECISION_2048	2048
#define HI3670_BLPWM_PRECISION_4096	4096

#define HI3670_BLPWM_MAX_PRECISION	10000

enum hi3670_blpwm_precision {
	HI3670_BLPWM_PRECISION_10000_TYPE = 1,
	HI3670_BLPWM_PRECISION_2048_TYPE = 2,
	HI3670_BLPWM_PRECISION_4096_TYPE = 3,
};

struct hi3670_blpwm {
	void __iomem *base;
	struct clk *clk;
	u32 precision;
	u32 div;
};

static inline struct hi3670_blpwm *to_hi3670_blpwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static void hi3670_blpwm_write_cfg(struct hi3670_blpwm *blpwm, u32 high,
				   u32 low)
{
	/*
	 * The CFG register packs the two halves of the period:
	 *   bits 31..16  high time
	 *   bits 15..0   low time
	 * A duty of 0% is 0 high / precision low, 100% the reverse.  Writing
	 * both halves in one store keeps them consistent.
	 */
	writel_relaxed((high << 16) | (low & 0xffff),
		       blpwm->base + HI3670_BLPWM_CFG);
}

/*
 * Nominal duration of one PWM cycle.  The block always counts "precision"
 * steps per cycle, so this is also the time base used to convert between the
 * PWM framework's nanoseconds and the hardware's step counts.
 */
static u64 hi3670_blpwm_period_ns(struct hi3670_blpwm *blpwm)
{
	return DIV_ROUND_UP_ULL(NSEC_PER_SEC, blpwm->precision);
}

static int hi3670_blpwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct hi3670_blpwm *blpwm = to_hi3670_blpwm(chip);
	u64 period;
	u32 cfg, ctrl;

	cfg = readl_relaxed(blpwm->base + HI3670_BLPWM_CFG);
	ctrl = readl_relaxed(blpwm->base + HI3670_BLPWM_CTRL);

	/*
	 * One high step is period/precision nanoseconds.  Report the nominal
	 * period even when the channel is off, so a consumer that asks for a
	 * fraction of the period gets a usable number back.
	 */
	period = hi3670_blpwm_period_ns(blpwm);
	state->period = period;
	state->duty_cycle = DIV_ROUND_UP_ULL((u64)(cfg >> 16) * period,
					     blpwm->precision);
	state->polarity = PWM_POLARITY_NORMAL;
	state->enabled = ctrl & HI3670_BLPWM_CTRL_ENABLE;
	state->usage_power = false;

	return 0;
}

static int hi3670_blpwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct hi3670_blpwm *blpwm = to_hi3670_blpwm(chip);
	u64 period = state->period ? state->period :
				     hi3670_blpwm_period_ns(blpwm);
	u32 high, low;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	/*
	 * Convert the requested duty cycle into the block's own units.  The
	 * hardware has no notion of a configurable period - it always spans
	 * "precision" ticks - so any requested period is rounded to what the
	 * block actually produces and only the duty ratio is honoured.
	 */
	high = min_t(u64, DIV_ROUND_DOWN_ULL(state->duty_cycle *
					     blpwm->precision, period),
		     blpwm->precision);
	low = blpwm->precision - high;

	hi3670_blpwm_write_cfg(blpwm, high, low);

	if (state->enabled)
		writel_relaxed(HI3670_BLPWM_CTRL_ENABLE,
			       blpwm->base + HI3670_BLPWM_CTRL);
	else
		writel_relaxed(0, blpwm->base + HI3670_BLPWM_CTRL);

	return 0;
}

static const struct pwm_ops hi3670_blpwm_ops = {
	.get_state = hi3670_blpwm_get_state,
	.apply = hi3670_blpwm_apply,
};

static int hi3670_blpwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	enum hi3670_blpwm_precision type;
	struct pwm_chip *chip;
	struct hi3670_blpwm *blpwm;
	u32 precision;
	int ret;

	chip = devm_pwmchip_alloc(dev, 1, sizeof(*blpwm));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	blpwm = to_hi3670_blpwm(chip);
	platform_set_drvdata(pdev, chip);

	blpwm->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(blpwm->base))
		return PTR_ERR(blpwm->base);

	blpwm->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(blpwm->clk))
		return dev_err_probe(dev, PTR_ERR(blpwm->clk),
				     "failed to get clock\n");

	ret = clk_prepare_enable(blpwm->clk);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable clock\n");

	/*
	 * Pick the prescaler/resolution pair.  The compatible selects the
	 * default the SoC was designed around; the 10000-step mode is what
	 * the COR-AL00's panel uses (bl-pwm-preci-type = <1>).  A board that
	 * needs a different resolution overrides it with the
	 * "hisilicon,blpwm-precision" property.
	 */
	type = (enum hi3670_blpwm_precision)(uintptr_t)device_get_match_data(dev);

	switch (type) {
	case HI3670_BLPWM_PRECISION_4096_TYPE:
		blpwm->div = HI3670_BLPWM_DIV_4096;
		precision = HI3670_BLPWM_PRECISION_4096;
		break;
	case HI3670_BLPWM_PRECISION_2048_TYPE:
		blpwm->div = HI3670_BLPWM_DIV_2048;
		precision = HI3670_BLPWM_PRECISION_2048;
		break;
	case HI3670_BLPWM_PRECISION_10000_TYPE:
	default:
		blpwm->div = HI3670_BLPWM_DIV_10000;
		precision = HI3670_BLPWM_PRECISION_10000;
		break;
	}

	/*
	 * An explicit DT property wins over the compatible's default.  Only
	 * the three resolutions the hardware can produce are accepted;
	 * anything else keeps the compatible's choice.
	 */
	ret = of_property_read_u32(dev->of_node, "hisilicon,blpwm-precision",
				   &precision);
	if (!ret) {
		switch (precision) {
		case HI3670_BLPWM_PRECISION_10000:
			blpwm->div = HI3670_BLPWM_DIV_10000;
			break;
		case HI3670_BLPWM_PRECISION_4096:
			blpwm->div = HI3670_BLPWM_DIV_4096;
			break;
		case HI3670_BLPWM_PRECISION_2048:
			blpwm->div = HI3670_BLPWM_DIV_2048;
			break;
		default:
			dev_warn(dev, "unsupported precision %u, ignoring\n",
				 precision);
			precision = 0;
			break;
		}
	}
	blpwm->precision = precision ? : HI3670_BLPWM_PRECISION_10000;

	writel_relaxed(blpwm->div, blpwm->base + HI3670_BLPWM_DIV);

	chip->ops = &hi3670_blpwm_ops;

	ret = pwmchip_add(chip);
	if (ret) {
		clk_disable_unprepare(blpwm->clk);
		return dev_err_probe(dev, ret, "failed to add PWM chip\n");
	}

	return 0;
}

static void hi3670_blpwm_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct hi3670_blpwm *blpwm = to_hi3670_blpwm(chip);

	writel_relaxed(0, blpwm->base + HI3670_BLPWM_CTRL);

	pwmchip_remove(chip);

	clk_disable_unprepare(blpwm->clk);
}

static const struct of_device_id hi3670_blpwm_of_match[] = {
	{
		.compatible = "hisilicon,hi3670-blpwm",
		.data = (void *)HI3670_BLPWM_PRECISION_10000_TYPE,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, hi3670_blpwm_of_match);

static struct platform_driver hi3670_blpwm_driver = {
	.driver = {
		.name = "hi3670-blpwm",
		.of_match_table = hi3670_blpwm_of_match,
	},
	.probe = hi3670_blpwm_probe,
	.remove = hi3670_blpwm_remove,
};
module_platform_driver(hi3670_blpwm_driver);

MODULE_AUTHOR("Mainline port");
MODULE_DESCRIPTION("HiSilicon Hi3670 (Kirin 970) BL-PWM driver");
MODULE_LICENSE("GPL");
