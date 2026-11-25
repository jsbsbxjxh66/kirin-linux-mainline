// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon hardware spinlock driver.
 *
 * The lock registers live in the always-on pctrl block.  Each group register
 * holds several four-bit slots; a slot is taken by writing the AP master ID
 * and reading back the grant status.  The AP is master 1.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "hwspinlock_internal.h"

#define HISI_HWLOCK_MASTER_ID		0x01
#define HISI_HWLOCK_CMD			((HISI_HWLOCK_MASTER_ID << 1) | 0x01)
#define HISI_HWLOCK_ID_MASK		0x0f
#define HISI_HWLOCK_UNLOCK_OFFSET	0x04
#define HISI_HWLOCK_STATUS_OFFSET	0x08

struct hisi_hwspinlock {
	void __iomem *base;
	unsigned int offset;
	unsigned int shift;
};

static int hisi_hwspinlock_trylock(struct hwspinlock *lock)
{
	struct hisi_hwspinlock *priv = lock->priv;
	unsigned int cmd = HISI_HWLOCK_CMD << priv->shift;
	unsigned int status;

	writel(cmd, priv->base + priv->offset);
	status = readl(priv->base + priv->offset + HISI_HWLOCK_STATUS_OFFSET);

	return HISI_HWLOCK_CMD == ((status >> priv->shift) & HISI_HWLOCK_ID_MASK);
}

static void hisi_hwspinlock_unlock(struct hwspinlock *lock)
{
	struct hisi_hwspinlock *priv = lock->priv;
	unsigned int cmd = HISI_HWLOCK_CMD << priv->shift;

	writel(cmd, priv->base + priv->offset + HISI_HWLOCK_UNLOCK_OFFSET);
}

static void hisi_hwspinlock_relax(struct hwspinlock *lock)
{
	ndelay(50);
}

static const struct hwspinlock_ops hisi_hwspinlock_ops = {
	.trylock	= hisi_hwspinlock_trylock,
	.unlock		= hisi_hwspinlock_unlock,
	.relax		= hisi_hwspinlock_relax,
};

static int hisi_hwspinlock_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *syscon_np;
	struct hwspinlock_device *bank;
	struct hisi_hwspinlock *priv;
	struct resource res;
	void __iomem *base;
	u32 register_width, bits_per_lock, groups;
	u32 *offsets;
	unsigned int locks_per_group, num_locks;
	int ret, i;

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

	ret = of_property_read_u32(np, "hwlock,register-width", &register_width);
	if (ret)
		return dev_err_probe(dev, ret, "missing hwlock,register-width\n");

	ret = of_property_read_u32(np, "hwlock,bits-per-single", &bits_per_lock);
	if (ret)
		return dev_err_probe(dev, ret, "missing hwlock,bits-per-single\n");

	ret = of_property_read_u32(np, "hwlock,groups", &groups);
	if (ret)
		return dev_err_probe(dev, ret, "missing hwlock,groups\n");

	if (!bits_per_lock || !groups || register_width % bits_per_lock)
		return dev_err_probe(dev, -EINVAL, "invalid lock geometry\n");

	locks_per_group = register_width / bits_per_lock;
	num_locks = locks_per_group * groups;
	if (!num_locks)
		return dev_err_probe(dev, -EINVAL, "no locks\n");

	offsets = devm_kcalloc(dev, groups, sizeof(*offsets), GFP_KERNEL);
	if (!offsets)
		return -ENOMEM;

	ret = of_property_read_u32_array(np, "hwlock,offset", offsets, groups);
	if (ret)
		return dev_err_probe(dev, ret, "missing hwlock,offset\n");

	bank = devm_kzalloc(dev, struct_size(bank, lock, num_locks),
			    GFP_KERNEL);
	if (!bank)
		return -ENOMEM;

	priv = devm_kcalloc(dev, num_locks, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < num_locks; i++) {
		priv[i].base = base;
		priv[i].offset = offsets[i / locks_per_group];
		priv[i].shift = (i % locks_per_group) * bits_per_lock;
		bank->lock[i].priv = &priv[i];
	}

	ret = devm_hwspin_lock_register(dev, bank, &hisi_hwspinlock_ops, 0,
					num_locks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register locks\n");

	dev_info(dev, "registered %u locks\n", num_locks);

	return 0;
}

static const struct of_device_id hisi_hwspinlock_of_match[] = {
	{ .compatible = "hisilicon,hwspinlock" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_hwspinlock_of_match);

static struct platform_driver hisi_hwspinlock_driver = {
	.probe = hisi_hwspinlock_probe,
	.driver = {
		.name = "hisi-hwspinlock",
		.of_match_table = hisi_hwspinlock_of_match,
	},
};
module_platform_driver(hisi_hwspinlock_driver);

MODULE_DESCRIPTION("HiSilicon hardware spinlock driver");
MODULE_LICENSE("GPL");
