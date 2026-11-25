// SPDX-License-Identifier: GPL-2.0
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "slimbus.h"
/*lint -e750 -e429*/
/* pcm devices */
#define SLIMBUS_DEV_AUDIO           0
#define SLIMBUS_DEV_DIRECT_AUDIO    1
#define SLIMBUS_DEV_FAST_AUDIO      2
#define SLIMBUS_DEV_VOICE           3
#define SLIMBUS_DEV_BT              4
#define SLIMBUS_DEV_MODEM           5
#define SLIMBUS_DEV_DSP_PB          6

struct slimbus_dai_data {
	slimbus_track_param_t           hw_params;
	slimbus_track_type_t            track;
	struct mutex                    lock;
	int                             playback_count;
	int                             capture_count;
};

static int slimbus_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	return 0;
}

static void slimbus_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	/* do nothing */
}

static int slimbus_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	return ret;
}

static  int slimbus_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	return ret;
}

static int slimbus_dai_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	return ret;
}

static const struct snd_soc_dai_ops slimbus_dai_ops = {
	.startup    = slimbus_dai_startup,
	.shutdown   = slimbus_dai_shutdown,
	.hw_params  = slimbus_dai_hw_params,
	.trigger    = slimbus_dai_trigger,
	.hw_free    = slimbus_dai_hw_free,
};


static struct snd_soc_dai_driver slimbus_dai = {
	.name = "slimbus-dai",
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_384000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S16_BE |
			   SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S24_BE,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 6,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &slimbus_dai_ops,
};

static const struct snd_soc_component_driver slimbus_dai_component = {
	.name       = "slimbus-dai",
};

static int slimbus_dai_module_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct slimbus_dai_data *pdata = NULL;
	int ret = 0;

	pr_info("[%s:%d] slimbus_dai probe!\n", __func__, __LINE__);
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "cannot allocate slimbus DAI platform data\n");
		return -ENOMEM;
	}

	mutex_init(&pdata->lock);

	platform_set_drvdata(pdev, pdata);

	ret = snd_soc_register_component(&pdev->dev, &slimbus_dai_component,
					  &slimbus_dai, 1);

	return ret;
}

static void slimbus_dai_module_remove(struct platform_device *pdev)
{
	struct slimbus_dai_data *pdata = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);
	mutex_destroy(&pdata->lock);
}

static const struct of_device_id slimbus_dai_of_match[] = {
	{
		.compatible = "hisilicon,slimbus-dai",
	},
	{ },
};

static struct platform_driver slimbus_dai_driver = {
	.driver = {
		.name   = "slimbus-dai",
		.of_match_table = slimbus_dai_of_match,
	},
	.probe  = slimbus_dai_module_probe,
	.remove = slimbus_dai_module_remove,
};
module_platform_driver(slimbus_dai_driver);

MODULE_LICENSE("GPL");
