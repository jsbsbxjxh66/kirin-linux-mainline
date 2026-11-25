// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon Hi3670 stub clock driver.
 *
 * The CPU cluster and GPU rates are negotiated with the always-on LPM3
 * firmware over mailbox channel 13.  LPM3 writes the selected OPP index back
 * to SCBAKDATA4 in the sysctrl block, so recalc_rate() can report the live
 * rate without an IPC round trip.
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/hi3670-clock.h>

#define MHZ			1000000
#define SCBAK_INDEX_MASK	0xf
#define HI3670_STUB_CLK_COUNT	HI3670_CLK_STUB_NUM

struct hi3670_stub_mbox {
	struct mbox_client cl;
	struct mbox_chan *mbox;
	struct mutex lock;
};

struct hi3670_stub_clk {
	struct clk_hw hw;
	struct clk_init_data init;
	struct hi3670_stub_mbox *mb;
	void __iomem *scbak;
	void __iomem *pmctrl;
	u32 shift;
	u32 cmd;
	u32 vote_offset;
	u32 vote_mask;
	unsigned long rate;
	unsigned long *freqs;
	unsigned int num_freqs;
	bool read_only;
	bool vote;
	u32 msg[8];
};

static unsigned int hi3670_stub_clk_best_index(struct hi3670_stub_clk *clk,
					       unsigned long rate)
{
	unsigned int i, best = 0;
	unsigned long best_diff = ~0UL;

	for (i = 0; i < clk->num_freqs; i++) {
		unsigned long diff = clk->freqs[i] > rate ?
				     clk->freqs[i] - rate :
				     rate - clk->freqs[i];

		if (diff < best_diff) {
			best_diff = diff;
			best = i;
		}
	}

	return best;
}

static unsigned long hi3670_stub_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct hi3670_stub_clk *clk = container_of(hw, struct hi3670_stub_clk, hw);
	unsigned int index;

	if (!clk->num_freqs)
		return 0;

	if (clk->vote)
		return clk->rate;

	index = (readl(clk->scbak) >> clk->shift) & SCBAK_INDEX_MASK;
	if (index >= clk->num_freqs)
		index = 0;

	return clk->freqs[index];
}

static int hi3670_stub_clk_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	struct hi3670_stub_clk *clk = container_of(hw, struct hi3670_stub_clk, hw);

	if (!clk->num_freqs)
		return -EINVAL;

	req->rate = clk->freqs[hi3670_stub_clk_best_index(clk, req->rate)];

	return 0;
}

static int hi3670_stub_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct hi3670_stub_clk *clk = container_of(hw, struct hi3670_stub_clk, hw);
	struct hi3670_stub_mbox *mb = clk->mb;
	unsigned int index;
	int ret = 0;

	if (!clk->num_freqs)
		return -EINVAL;

	if (clk->read_only)
		return -EINVAL;

	index = hi3670_stub_clk_best_index(clk, rate);

	if (clk->vote) {
		writel((clk->freqs[index] / MHZ) | clk->vote_mask,
		       clk->pmctrl + clk->vote_offset);
		clk->rate = clk->freqs[index];
		return 0;
	}

	mutex_lock(&mb->lock);

	if (!mb->mbox) {
		mb->mbox = mbox_request_channel(&mb->cl, 0);
		if (IS_ERR(mb->mbox)) {
			ret = PTR_ERR(mb->mbox);
			mb->mbox = NULL;
			goto out;
		}
	}

	clk->msg[0] = clk->cmd;
	clk->msg[1] = clk->freqs[index] / MHZ;

	ret = mbox_send_message(mb->mbox, clk->msg);
	if (ret < 0)
		goto out;

	ret = 0;

out:
	mutex_unlock(&mb->lock);
	return ret;
}

static const struct clk_ops hi3670_stub_clk_ops = {
	.recalc_rate	= hi3670_stub_clk_recalc_rate,
	.determine_rate	= hi3670_stub_clk_determine_rate,
	.set_rate	= hi3670_stub_clk_set_rate,
};

static struct clk_hw *hi3670_stub_clk_hw_get(struct of_phandle_args *clkspec,
					     void *data)
{
	struct hi3670_stub_clk *clks = data;
	unsigned int index = clkspec->args[0];

	if (index >= HI3670_STUB_CLK_COUNT)
		return ERR_PTR(-EINVAL);

	return &clks[index].hw;
}

static int hi3670_stub_parse_opp(struct device *dev, struct device_node *np,
				 const char *prop, struct hi3670_stub_clk *clk)
{
	struct device_node *opp_np, *child;
	unsigned int count, i = 0;
	int ret = 0;

	opp_np = of_parse_phandle(np, prop, 0);
	if (!opp_np)
		return dev_err_probe(dev, -EINVAL, "missing %s\n", prop);

	count = of_get_child_count(opp_np);
	if (!count) {
		ret = -EINVAL;
		goto out_put;
	}

	clk->freqs = devm_kcalloc(dev, count, sizeof(*clk->freqs), GFP_KERNEL);
	if (!clk->freqs) {
		ret = -ENOMEM;
		goto out_put;
	}

	for_each_available_child_of_node(opp_np, child) {
		u64 hz;

		if (of_property_read_u64(child, "opp-hz", &hz)) {
			dev_err(dev, "%pOF: missing opp-hz\n", child);
			of_node_put(child);
			ret = -EINVAL;
			break;
		}
		clk->freqs[i++] = hz;
	}

	clk->num_freqs = i;

out_put:
	of_node_put(opp_np);
	return ret;
}

static int hi3670_stub_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *syscon_np, *pmctrl_np;
	struct hi3670_stub_mbox *mb;
	struct hi3670_stub_clk *clks;
	struct resource res, pmctrl_res;
	void __iomem *base, *pmctrl_base;
	u32 offset;
	int ret, i;

	static const struct {
		const char *name;
		const char *opp;
		u32 cmd;
		u32 shift;
		bool read_only;
		bool vote;
		u32 vote_offset;
		u32 vote_mask;
	} desc[] = {
		{ "cpu-cluster.0", "hisilicon,cluster0-opp", 0x1030a, 0 },
		{ "cpu-cluster.1", "hisilicon,cluster1-opp", 0x2030a, 4 },
		{ "clk_g3d", "hisilicon,gpu-opp", 0x3030a, 12 },
		{ "clk_ddrc_freq", "hisilicon,ddr-opp", 0, 8, true },
		{ "clk_ddrc_min", "hisilicon,ddr-opp", 0, 0, false, true,
		  0x2b0, 0x00ff0000 },
		{ "clk_ddrc_max", "hisilicon,ddr-opp", 0, 0, false, true,
		  0x2a0, 0x00ff0000 },
	};

	clks = devm_kcalloc(dev, HI3670_STUB_CLK_COUNT, sizeof(*clks),
			    GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	mb = devm_kzalloc(dev, sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -ENOMEM;

	syscon_np = of_parse_phandle(np, "hisilicon,syscon", 0);
	if (!syscon_np)
		return dev_err_probe(dev, -EINVAL, "missing hisilicon,syscon\n");

	ret = of_address_to_resource(syscon_np, 0, &res);
	of_node_put(syscon_np);
	if (ret)
		return dev_err_probe(dev, ret, "invalid syscon resource\n");

	base = devm_ioremap(dev, res.start, resource_size(&res));
	if (!base)
		return dev_err_probe(dev, -ENOMEM, "failed to map syscon\n");

	pmctrl_np = of_parse_phandle(np, "hisilicon,pmctrl", 0);
	if (!pmctrl_np)
		return dev_err_probe(dev, -EINVAL, "missing hisilicon,pmctrl\n");

	ret = of_address_to_resource(pmctrl_np, 0, &pmctrl_res);
	of_node_put(pmctrl_np);
	if (ret)
		return dev_err_probe(dev, ret, "invalid pmctrl resource\n");

	pmctrl_base = devm_ioremap(dev, pmctrl_res.start,
				   resource_size(&pmctrl_res));
	if (!pmctrl_base)
		return dev_err_probe(dev, -ENOMEM, "failed to map pmctrl\n");

	ret = of_property_read_u32(np, "hisilicon,scbak-offset", &offset);
	if (ret)
		return dev_err_probe(dev, ret, "missing hisilicon,scbak-offset\n");

	mutex_init(&mb->lock);
	mb->cl.dev = dev;
	mb->cl.tx_block = false;
	mb->cl.knows_txdone = false;

	platform_set_drvdata(pdev, mb);

	for (i = 0; i < HI3670_STUB_CLK_COUNT; i++) {
		struct hi3670_stub_clk *clk = &clks[i];

		clk->scbak = base + offset;
		clk->pmctrl = pmctrl_base;
		clk->shift = desc[i].shift;
		clk->cmd = desc[i].cmd;
		clk->read_only = desc[i].read_only;
		clk->vote = desc[i].vote;
		clk->vote_offset = desc[i].vote_offset;
		clk->vote_mask = desc[i].vote_mask;
		clk->mb = mb;

		ret = hi3670_stub_parse_opp(dev, np, desc[i].opp, clk);
		if (ret)
			return ret;

		clk->init.name = desc[i].name;
		clk->init.ops = &hi3670_stub_clk_ops;
		clk->init.flags = CLK_GET_RATE_NOCACHE;
		clk->hw.init = &clk->init;

		ret = devm_clk_hw_register(dev, &clk->hw);
		if (ret)
			return dev_err_probe(dev, ret, "failed to register %s\n",
					     desc[i].name);
	}

	return devm_of_clk_add_hw_provider(dev, hi3670_stub_clk_hw_get, clks);
}

static void hi3670_stub_clk_remove(struct platform_device *pdev)
{
	struct hi3670_stub_mbox *mb = platform_get_drvdata(pdev);

	if (mb->mbox)
		mbox_free_channel(mb->mbox);
}

static const struct of_device_id hi3670_stub_clk_of_match[] = {
	{ .compatible = "hisilicon,hi3670-stub-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3670_stub_clk_of_match);

static struct platform_driver hi3670_stub_clk_driver = {
	.probe = hi3670_stub_clk_probe,
	.remove = hi3670_stub_clk_remove,
	.driver = {
		.name = "hi3670-stub-clk",
		.of_match_table = hi3670_stub_clk_of_match,
	},
};
module_platform_driver(hi3670_stub_clk_driver);

MODULE_DESCRIPTION("HiSilicon Hi3670 stub clock driver");
MODULE_LICENSE("GPL");
