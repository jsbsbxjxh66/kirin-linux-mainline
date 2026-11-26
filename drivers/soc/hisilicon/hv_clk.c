// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Kirin 970 hardware frequency-vote clocks
 *
 * The CPU cluster frequency is not programmable by the AP directly: PMCTRL
 * takes a per-source frequency vote and applies it (frequency + rail) by
 * itself.  drivers/soc/hisilicon/hw_vote.c exposes that vote mechanism; this
 * driver wraps the "little-freq"/"big-freq" channels as clocks so the generic
 * cpufreq-dt/OPP stack can drive the CPUs through clk_set_rate().
 *
 * The HiKey970-style hisilicon,hi3670-stub-clk path (LPM3 mailbox + SRAM
 * request) is not implemented by this board's firmware and is left disabled.
 */
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/hisi/hisi_hw_vote.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define HV_CLK_NUM		2
#define HV_CLK_VOTE_SRC		"vote-src-1"

static const char *const hv_clk_names[HV_CLK_NUM] = {
	"little-freq",
	"big-freq",
};

struct hv_clk {
	struct clk_hw hw;
	struct hvdev *hvdev;
};

static int hv_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct hv_clk *hc = container_of(hw, struct hv_clk, hw);

	/* hw_vote takes kHz and encodes it in PMCTRL's vote register. */
	return hisi_hv_set_freq(hc->hvdev, (unsigned int)(rate / 1000));
}

static unsigned long hv_clk_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct hv_clk *hc = container_of(hw, struct hv_clk, hw);
	unsigned int khz = 0;

	if (hisi_hv_get_result(hc->hvdev, &khz))
		return 0;

	return (unsigned long)khz * 1000;
}

/*
 * PMCTRL encodes the vote in 16 MHz steps (the "ratio" DT property), and
 * hisi_hv_set_freq() truncates.  Round down here so the rate reported after
 * clk_set_rate() never exceeds what the hardware can apply.
 */
#define HV_VOTE_STEP_HZ	(16UL * 1000 * 1000)

static int hv_clk_determine_rate(struct clk_hw *hw,
				 struct clk_rate_request *req)
{
	req->rate = req->rate / HV_VOTE_STEP_HZ * HV_VOTE_STEP_HZ;
	return 0;
}

static const struct clk_ops hv_clk_ops = {
	.set_rate	= hv_clk_set_rate,
	.recalc_rate	= hv_clk_recalc_rate,
	.determine_rate	= hv_clk_determine_rate,
};

static int hv_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *onecell;
	struct hv_clk *clks;
	struct device_node *hv_np;
	bool hv_ready;
	int i, ret;

	/*
	 * hw_vote is a separate platform device; wait for it instead of
	 * hammering hisi_hvdev_register() (which logs an error) on every
	 * deferred probe.
	 */
	hv_np = of_find_compatible_node(NULL, NULL, "hisi,freq-hw-vote");
	hv_ready = hv_np && of_device_is_available(hv_np);
	of_node_put(hv_np);
	if (!hv_ready)
		return -EPROBE_DEFER;

	onecell = devm_kzalloc(dev, struct_size(onecell, hws, HV_CLK_NUM),
			       GFP_KERNEL);
	if (!onecell)
		return -ENOMEM;
	onecell->num = HV_CLK_NUM;

	clks = devm_kcalloc(dev, HV_CLK_NUM, sizeof(*clks), GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	for (i = 0; i < HV_CLK_NUM; i++) {
		struct clk_init_data init = { };

		clks[i].hvdev = hisi_hvdev_register(dev, hv_clk_names[i],
						    HV_CLK_VOTE_SRC);
		if (!clks[i].hvdev)
			return -EPROBE_DEFER;

		init.name = hv_clk_names[i];
		init.ops = &hv_clk_ops;
		clks[i].hw.init = &init;

		ret = devm_clk_hw_register(dev, &clks[i].hw);
		if (ret)
			return ret;

		onecell->hws[i] = &clks[i].hw;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, onecell);
	if (ret)
		return ret;

	dev_info(dev, "registered %d CPU frequency-vote clocks\n", HV_CLK_NUM);

	return 0;
}

static const struct of_device_id hv_clk_of_match[] = {
	{ .compatible = "hisilicon,hi3670-hv-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, hv_clk_of_match);

static struct platform_driver hv_clk_driver = {
	.probe	= hv_clk_probe,
	.driver	= {
		.name		= "hi3670-hv-clk",
		.of_match_table	= hv_clk_of_match,
	},
};
module_platform_driver(hv_clk_driver);

MODULE_DESCRIPTION("HiSilicon Kirin 970 frequency-vote CPU clocks");
MODULE_LICENSE("GPL");
