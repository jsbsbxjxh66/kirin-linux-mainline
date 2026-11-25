// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon hardware frequency vote driver
 *
 * The PMCTRL block contains a small hardware state machine that aggregates
 * frequency votes from several masters and exposes the winning value.  Each
 * vote source owns one register field; consumers register a vote source and
 * update it when their own frequency changes.
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 HiSilicon.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/hisi/hisi_hw_vote.h>

#define HV_UNIT_KHZ		1000
#define HV_REG_U32_NUM		3

struct hv_reg_info {
	u32 offset;
	u32 rd_mask;
	u32 wr_mask;
};

static struct hv_channel *hv_channel_array;
static unsigned int hv_channel_size;
static DEFINE_MUTEX(hv_array_mutex);

static int hv_reg_write(struct hv_reg_cfg cfg, u32 val)
{
	unsigned int shift;
	unsigned int width;
	u32 mask;

	shift = ffs(cfg.rd_mask);
	if (!shift)
		return -EPERM;

	shift--;
	width = fls(cfg.rd_mask) - shift;
	if (width >= 32)
		mask = ~0U;
	else
		mask = (1U << width) - 1;

	if (val > mask)
		val = mask;

	writel(cfg.wr_mask | ((val & mask) << shift), cfg.reg);

	return 0;
}

static int hv_reg_read(struct hv_reg_cfg cfg, u32 *val)
{
	unsigned int shift;

	shift = ffs(cfg.rd_mask);
	if (!shift)
		return -EINVAL;

	shift--;
	*val = (readl(cfg.reg) & cfg.rd_mask) >> shift;

	return 0;
}

static int check_channel_para(struct hvdev *hvdev)
{
	if (IS_ERR_OR_NULL(hvdev->parent))
		return -EINVAL;

	if (!hvdev->parent->ratio)
		return -EINVAL;

	return 0;
}

int hisi_hv_set_freq(struct hvdev *hvdev, unsigned int freq_khz)
{
	int ret;

	if (IS_ERR_OR_NULL(hvdev)) {
		pr_err("%s: not registered for hardware vote\n", __func__);
		return -ENODEV;
	}

	ret = check_channel_para(hvdev);
	if (ret) {
		pr_err("%s: invalid parent parameters\n", __func__);
		return ret;
	}

	ret = hv_reg_write(hvdev->vote, ((freq_khz / HV_UNIT_KHZ) /
					 hvdev->parent->ratio));
	if (ret) {
		pr_err("%s: failed to write vote: %d\n", __func__, ret);
		return ret;
	}

	hvdev->last_set = freq_khz;

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_hv_set_freq);

static int check_get_para(struct hvdev *hvdev, unsigned int *freq_khz)
{
	if (IS_ERR_OR_NULL(hvdev) || IS_ERR_OR_NULL(freq_khz))
		return -ENODEV;

	return 0;
}

int hisi_hv_get_freq(struct hvdev *hvdev, unsigned int *freq_khz)
{
	u32 val = 0;
	int ret;

	ret = check_get_para(hvdev, freq_khz);
	if (ret) {
		pr_err("%s: invalid user parameters\n", __func__);
		return ret;
	}

	ret = check_channel_para(hvdev);
	if (ret) {
		pr_err("%s: invalid parent parameters\n", __func__);
		return ret;
	}

	ret = hv_reg_read(hvdev->vote, &val);
	if (ret) {
		pr_err("%s: failed to read vote: %d\n", __func__, ret);
		return ret;
	}

	*freq_khz = val * HV_UNIT_KHZ * hvdev->parent->ratio;

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_hv_get_freq);

int hisi_hv_get_result(struct hvdev *hvdev, unsigned int *freq_khz)
{
	u32 val = 0;
	int ret;

	ret = check_get_para(hvdev, freq_khz);
	if (ret) {
		pr_err("%s: invalid user parameters\n", __func__);
		return ret;
	}

	ret = check_channel_para(hvdev);
	if (ret) {
		pr_err("%s: invalid parent parameters\n", __func__);
		return ret;
	}

	ret = hv_reg_read(hvdev->parent->result, &val);
	if (ret) {
		pr_err("%s: failed to read result: %d\n", __func__, ret);
		return ret;
	}

	*freq_khz = val * HV_UNIT_KHZ * hvdev->parent->ratio;

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_hv_get_result);

int hisi_hv_get_last(struct hvdev *hvdev, unsigned int *freq_khz)
{
	int ret;

	ret = check_get_para(hvdev, freq_khz);
	if (ret)
		return ret;

	*freq_khz = hvdev->last_set;

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_hv_get_last);

static struct hvdev *find_hvdev_by_name(struct hv_channel *channel,
					const char *vsrc)
{
	struct hvdev *hvdev = channel->hvdev_head;
	unsigned int id;

	for (id = 0; id < channel->hvdev_num; id++) {
		if (!strcmp(hvdev[id].name, vsrc) && !hvdev[id].dev)
			return &hvdev[id];
	}

	return NULL;
}

struct hvdev *hisi_hvdev_register(struct device *dev, const char *ch_name,
				  const char *vsrc)
{
	struct hvdev *hvdev = NULL;
	unsigned int val = 0;
	unsigned int id;
	int ret;

	if (IS_ERR_OR_NULL(dev) || !ch_name || !vsrc) {
		pr_err("%s: invalid parameters\n", __func__);
		return NULL;
	}

	if (!hv_channel_size || !hv_channel_array) {
		pr_err("%s: hardware vote is not initialized\n", __func__);
		return NULL;
	}

	mutex_lock(&hv_array_mutex);
	for (id = 0; id < hv_channel_size; id++) {
		if (!strcmp(hv_channel_array[id].name, ch_name)) {
			hvdev = find_hvdev_by_name(&hv_channel_array[id], vsrc);
			break;
		}
	}

	if (!hvdev) {
		pr_err("%s: no free vote source for %s/%s\n", __func__,
		       ch_name, vsrc);
		goto out_unlock;
	}

	ret = hisi_hv_get_result(hvdev, &val);
	if (ret)
		pr_err("%s: failed to read initial result: %d\n", __func__, ret);

	hvdev->dev = dev;
	hvdev->last_set = val;

out_unlock:
	mutex_unlock(&hv_array_mutex);

	return hvdev;
}
EXPORT_SYMBOL_GPL(hisi_hvdev_register);

int hisi_hvdev_remove(struct hvdev *hvdev)
{
	if (IS_ERR_OR_NULL(hvdev)) {
		pr_err("%s: invalid vote source\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&hv_array_mutex);
	hvdev->last_set = 0;
	hvdev->dev = NULL;
	mutex_unlock(&hv_array_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(hisi_hvdev_remove);

static int get_member_data(struct device *dev, struct device_node *np,
			   struct hv_channel *channel)
{
	struct hvdev *hvdev_array;
	struct device_node *child;
	struct hv_reg_info reg_info;
	int count, id = 0;
	int ret;

	count = of_get_child_count(np);
	if (!count) {
		dev_err(dev, "channel %pOF has no vote sources\n", np);
		return -EINVAL;
	}

	hvdev_array = devm_kzalloc(dev, sizeof(*hvdev_array) * count,
				   GFP_KERNEL);
	if (!hvdev_array)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		ret = of_property_read_u32_array(child, "vote_reg",
						 (u32 *)&reg_info,
						 HV_REG_U32_NUM);
		if (ret) {
			dev_err(dev, "failed to parse %pOF vote_reg: %d\n",
				child, ret);
			of_node_put(child);
			return -EINVAL;
		}

		hvdev_array[id].name = child->name;
		hvdev_array[id].parent = channel;
		hvdev_array[id].vote.reg = channel->base + reg_info.offset;
		hvdev_array[id].vote.rd_mask = reg_info.rd_mask;
		hvdev_array[id].vote.wr_mask = reg_info.wr_mask;
		hvdev_array[id].dev = NULL;
		hvdev_array[id].last_set = 0;
		id++;
	}

	channel->hvdev_num = id;
	channel->hvdev_head = hvdev_array;

	return 0;
}

static int hisi_hv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *root = dev->of_node;
	struct device_node *child;
	struct hv_reg_info reg_info;
	struct hv_channel *channels;
	void __iomem *base;
	u32 ratio = 1;
	int count;
	unsigned int id = 0;
	int ret;

	if (!root)
		return -ENODEV;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	count = of_get_child_count(root);
	if (!count) {
		dev_err(dev, "no frequency channels found\n");
		return -EINVAL;
	}

	channels = devm_kzalloc(dev, sizeof(*channels) * count, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	for_each_child_of_node(root, child) {
		ret = of_property_read_u32_array(child, "result_reg",
						 (u32 *)&reg_info,
						 HV_REG_U32_NUM);
		if (ret) {
			dev_err(dev, "failed to parse %pOF result_reg: %d\n",
				child, ret);
			of_node_put(child);
			return -EINVAL;
		}

		ret = of_property_read_u32(child, "ratio", &ratio);
		if (ret)
			ratio = 1;

		channels[id].name = child->name;
		channels[id].ratio = ratio;
		channels[id].base = base;
		channels[id].result.reg = base + reg_info.offset;
		channels[id].result.rd_mask = reg_info.rd_mask;
		channels[id].result.wr_mask = reg_info.wr_mask;
		channels[id].hvdev_num = 0;
		channels[id].hvdev_head = NULL;

		ret = get_member_data(dev, child, &channels[id]);
		if (ret) {
			dev_err(dev, "failed to parse %pOF vote sources\n",
				child);
			of_node_put(child);
			return ret;
		}

		id++;
	}

	if (id != count) {
		dev_err(dev, "channel count mismatch (%u != %d)\n", id, count);
		return -EINVAL;
	}

	mutex_lock(&hv_array_mutex);
	hv_channel_array = channels;
	hv_channel_size = count;
	mutex_unlock(&hv_array_mutex);

	dev_info(dev, "registered %u frequency channels\n", id);

	return 0;
}

static void hisi_hv_remove(struct platform_device *pdev)
{
	mutex_lock(&hv_array_mutex);
	hv_channel_array = NULL;
	hv_channel_size = 0;
	mutex_unlock(&hv_array_mutex);
}

static const struct of_device_id hisi_hv_of_match[] = {
	{ .compatible = "hisi,freq-hw-vote" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_hv_of_match);

static struct platform_driver hisi_hv_driver = {
	.probe = hisi_hv_probe,
	.remove = hisi_hv_remove,
	.driver = {
		.name = "hisi-hw-vote",
		.of_match_table = hisi_hv_of_match,
	},
};
module_platform_driver(hisi_hv_driver);

MODULE_DESCRIPTION("HiSilicon hardware frequency vote driver");
MODULE_LICENSE("GPL");
