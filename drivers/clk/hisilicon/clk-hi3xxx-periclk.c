// SPDX-License-Identifier: GPL-2.0
/*
 * Hisilicon hi3xxx per-clock gate / mux nodes ("hisilicon,hi3xxx-clk-gate" /
 * "hisilicon,hi3xxx-clk-mux").
 *
 * Ported from the Kirin970 Android kernel (drivers/clk/hisi/hisi-clkgate.c
 * and clk-kirin-common.c::hi3xxx_clkmux_setup), rewritten onto the mainline
 * clk_hw API.  Unlike the common clk-gate/clk-mux helpers these nodes:
 *
 *   - live as children of a clock-controller node (the parent node carries
 *     the reg); the controller needs no driver of its own;
 *   - gate by *writing the enable mask* to the gate register (and the same
 *     mask to reg+4 to gate off) instead of set/clear single bits;
 *   - optionally assert a reset register before gating off.
 *
 * Recognised child properties (matching the retail Kirin970 DTS):
 *   hisilicon,hi3xxx-clkgate = <offset mask>   gate register + enable mask
 *   hisilicon,hi3xxx-clkreset = <offset mask>  optional reset + mask
 *   clock-friend-names                          optional friend clock name
 *   gate_sync_time = <us>                       settle delay (<= 10us)
 *   hisilicon,clkmux-reg = <offset mask>        mux register + field mask
 *   hiword                                      mux uses hi-word masks
 *
 * The Kirin970 GPS clock tree (clk_mdm2gps0..2 / clkmux_gps_ref /
 * clk_gps_ref) is the in-tree user.
 */
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define HI3XXX_GATE_DISABLE_OFFSET	0x4
#define HI3XXX_GATE_STATUS_OFFSET	0x8
#define HI3XXX_RST_DISABLE_REG_OFFSET	0x4
#define HI3XXX_GATE_SYNC_MAX		10	/* us */

struct hi3xxx_gate {
	struct clk_hw		hw;
	void __iomem		*reg;		/* gate register */
	u32			mask;		/* enable mask written as-is */
	void __iomem		*reset;		/* optional reset register */
	u32			rmask;
	u32			sync_us;
};

struct hi3xxx_mux {
	struct clk_hw		hw;
	void __iomem		*reg;
	u8			shift;
	u8			width;
	bool			hiword;
};

static DEFINE_SPINLOCK(hi3xxx_clk_lock);

static inline struct hi3xxx_gate *to_gate(struct clk_hw *hw)
{
	return container_of(hw, struct hi3xxx_gate, hw);
}

static inline struct hi3xxx_mux *to_mux(struct clk_hw *hw)
{
	return container_of(hw, struct hi3xxx_mux, hw);
}

/* ---------- gate ------------------------------------------------------- */

static int hi3xxx_gate_enable(struct clk_hw *hw)
{
	struct hi3xxx_gate *g = to_gate(hw);

	if (g->sync_us)
		udelay(g->sync_us);
	writel(g->mask, g->reg);
	if (g->reset)
		writel(g->rmask, g->reset + HI3XXX_RST_DISABLE_REG_OFFSET);
	if (g->sync_us)
		udelay(1);

	return 0;
}

static void hi3xxx_gate_disable(struct clk_hw *hw)
{
	struct hi3xxx_gate *g = to_gate(hw);

	/* reset first, then gate: matches the vendor disable order */
	if (g->reset)
		writel(g->rmask, g->reset);
	writel(g->mask, g->reg + HI3XXX_GATE_DISABLE_OFFSET);
	if (g->sync_us)
		udelay(g->sync_us);
}

static int hi3xxx_gate_is_enabled(struct clk_hw *hw)
{
	struct hi3xxx_gate *g = to_gate(hw);

	return !!(readl(g->reg + HI3XXX_GATE_STATUS_OFFSET) & g->mask);
}

static const struct clk_ops hi3xxx_gate_ops = {
	.enable		= hi3xxx_gate_enable,
	.disable	= hi3xxx_gate_disable,
	.is_enabled	= hi3xxx_gate_is_enabled,
};

/* ---------- mux -------------------------------------------------------- */

static u8 hi3xxx_mux_get_parent(struct clk_hw *hw)
{
	struct hi3xxx_mux *m = to_mux(hw);
	u32 mask = ((u32)1 << m->width) - 1;
	unsigned int num = (readl(m->reg) >> m->shift) & mask;

	if (num >= clk_hw_get_num_parents(hw))
		return 0;
	return num;
}

static int hi3xxx_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct hi3xxx_mux *m = to_mux(hw);
	unsigned long flags;
	u32 val, mask;

	mask = ((u32)1 << m->width) - 1;

	spin_lock_irqsave(&hi3xxx_clk_lock, flags);
	if (m->hiword) {
		/* HiSilicon hi-word mask: select the field, then write it */
		writel(mask << m->shift, m->reg + 4);
		writel((u32)index << m->shift, m->reg);
	} else {
		val = readl(m->reg);
		val &= ~(mask << m->shift);
		val |= (u32)index << m->shift;
		writel(val, m->reg);
	}
	spin_unlock_irqrestore(&hi3xxx_clk_lock, flags);

	return 0;
}

static const struct clk_ops hi3xxx_mux_ops = {
	.determine_rate = clk_hw_determine_rate_no_reparent,
	.get_parent	= hi3xxx_mux_get_parent,
	.set_parent	= hi3xxx_mux_set_parent,
};

/* ---------- DT probe ---------------------------------------------------- */

static void __iomem *hi3xxx_parent_base(struct device_node *np)
{
	struct device_node *parent = of_get_parent(np);
	void __iomem *base;

	if (!parent)
		return IOMEM_ERR_PTR(-ENODEV);

	base = of_iomap(parent, 0);
	of_node_put(parent);
	return base;
}

static struct clk_hw *hi3xxx_of_gate(struct device_node *np)
{
	struct hi3xxx_gate *g;
	struct clk_init_data init = {};
	void __iomem *base;
	const char *parent_name;
	u32 data[2];
	int ret;

	if (of_property_read_string(np, "clock-output-names", &init.name))
		return ERR_PTR(-EINVAL);

	if (of_property_read_u32_array(np, "hisilicon,hi3xxx-clkgate",
				       data, ARRAY_SIZE(data)))
		return ERR_PTR(-EINVAL);

	base = hi3xxx_parent_base(np);
	if (IS_ERR(base))
		return ERR_CAST(base);

	g = kzalloc(sizeof(*g), GFP_KERNEL);
	if (!g)
		return ERR_PTR(-ENOMEM);

	/* an enable mask of 0 means the enable register is fake */
	g->mask = data[1];
	if (g->mask)
		g->reg = base + data[0];

	g->sync_us = 0;
	of_property_read_u32(np, "gate_sync_time", &g->sync_us);
	g->sync_us = min_t(u32, g->sync_us, HI3XXX_GATE_SYNC_MAX);

	if (!of_property_read_u32_array(np, "hisilicon,hi3xxx-clkreset",
					data, ARRAY_SIZE(data))) {
		g->reset = base + data[0];
		g->rmask = data[1];
	}

	/*
	 * clock-friend-names support from the vendor driver is dropped: no
	 * in-tree node uses it and mainline models this as a normal parent.
	 */

	parent_name = of_clk_get_parent_name(np, 0);
	init.ops = &hi3xxx_gate_ops;
	init.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;
	g->hw.init = &init;

	ret = clk_hw_register(NULL, &g->hw);
	if (ret) {
		kfree(g);
		return ERR_PTR(ret);
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, &g->hw);
	if (ret)
		clk_hw_unregister(&g->hw);
	return ret ? ERR_PTR(ret) : &g->hw;
}

static struct clk_hw *hi3xxx_of_mux(struct device_node *np)
{
	struct hi3xxx_mux *m;
	struct clk_init_data init = {};
	const char **parent_names;
	void __iomem *base;
	u32 data[2], mask;
	unsigned int i;
	int n, ret;

	if (of_property_read_string(np, "clock-output-names", &init.name))
		return ERR_PTR(-EINVAL);

	if (of_property_read_u32_array(np, "hisilicon,clkmux-reg",
				       data, ARRAY_SIZE(data)))
		return ERR_PTR(-EINVAL);

	n = of_clk_get_parent_count(np);
	if (n < 1)
		return ERR_PTR(-EINVAL);

	base = hi3xxx_parent_base(np);
	if (IS_ERR(base))
		return ERR_CAST(base);

	parent_names = kcalloc(n, sizeof(*parent_names), GFP_KERNEL);
	if (!parent_names)
		return ERR_PTR(-ENOMEM);
	for (i = 0; i < n; i++)
		parent_names[i] = of_clk_get_parent_name(np, i);

	m = kzalloc(sizeof(*m), GFP_KERNEL);
	if (!m) {
		kfree(parent_names);
		return ERR_PTR(-ENOMEM);
	}

	m->reg = base + data[0];
	mask = data[1];
	m->shift = __ffs(mask);
	m->width = fls(mask) - __ffs(mask) + 1;
	m->hiword = of_property_read_bool(np, "hiword");

	init.ops = &hi3xxx_mux_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent_names;
	init.num_parents = n;
	m->hw.init = &init;

	ret = clk_hw_register(NULL, &m->hw);
	if (ret) {
		kfree(m);
		kfree(parent_names);
		return ERR_PTR(ret);
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_simple_get, &m->hw);
	if (ret)
		clk_hw_unregister(&m->hw);
	kfree(parent_names);
	return ret ? ERR_PTR(ret) : &m->hw;
}

static const struct of_device_id hi3xxx_clk_of_match[] = {
	{ .compatible = "hisilicon,hi3xxx-clk-gate", .data = hi3xxx_of_gate },
	{ .compatible = "hisilicon,hi3xxx-clk-mux",  .data = hi3xxx_of_mux },
	{}
};

typedef struct clk_hw *(*hi3xxx_clk_of_fn)(struct device_node *);

static void __init hi3xxx_clk_init(struct device_node *np)
{
	const struct of_device_id *match = of_match_node(hi3xxx_clk_of_match, np);
	struct clk_hw *hw;

	if (!match || !match->data)
		return;

	hw = ((hi3xxx_clk_of_fn)match->data)(np);
	if (IS_ERR(hw))
		pr_err("%pOF: failed to register clock: %ld\n", np, PTR_ERR(hw));
}
CLK_OF_DECLARE_DRIVER(hi3xxx_gate, "hisilicon,hi3xxx-clk-gate", hi3xxx_clk_init);
CLK_OF_DECLARE_DRIVER(hi3xxx_mux, "hisilicon,hi3xxx-clk-mux", hi3xxx_clk_init);
