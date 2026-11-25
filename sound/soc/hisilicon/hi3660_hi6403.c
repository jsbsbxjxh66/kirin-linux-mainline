// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA SoC machine driver for Hi3660 with Hi6403 codec
 *
 * Copyright (c) 2015 Hisilicon Technologies CO., Ltd.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

static struct snd_soc_dai_link_component hi6403_audio_codec = {
	.name = "hi6403-codec",
	.dai_name = "hi6403-audio-dai",
};

static struct snd_soc_dai_link_component hi6403_voice_codec = {
	.name = "hi6403-codec",
	.dai_name = "hi6403-voice-dai",
};

static struct snd_soc_dai_link_component hi6403_fm_codec = {
	.name = "hi6403-codec",
	.dai_name = "hi6403-fm-dai",
};

static struct snd_soc_dai_link_component slimbus_cpu = {
	.name = "slimbus-dai",
	.dai_name = "slimbus-dai",
};

static struct snd_soc_dai_link_component hifi_platform = {
	.name = "hi6210-hifi",
};

static struct snd_soc_dai_link_component dummy_platform = {
	.name = "snd-soc-dummy",
};

static struct snd_soc_dai_link hi3660_hi6403_dai_link[] = {
	{
		.name = "hi3660_hi6403_pb_normal",
		.stream_name = "hi3660_hi6403_pb_normal",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_audio_codec,
		.num_codecs = 1,
		.platforms = &hifi_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_voice",
		.stream_name = "hi3660_voice",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_voice_codec,
		.num_codecs = 1,
		.platforms = &dummy_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_fm1",
		.stream_name = "hi3660_fm1",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_fm_codec,
		.num_codecs = 1,
		.platforms = &dummy_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_hi6403_pb_dsp",
		.stream_name = "hi3660_hi6403_pb_dsp",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_audio_codec,
		.num_codecs = 1,
		.platforms = &hifi_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_hi6403_pb_direct",
		.stream_name = "hi3660_hi6403_pb_direct",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_audio_codec,
		.num_codecs = 1,
		.platforms = &hifi_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_hi6403_lowlatency",
		.stream_name = "hi3660_hi6403_lowlatency",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_audio_codec,
		.num_codecs = 1,
		.platforms = &hifi_platform,
		.num_platforms = 1,
	},
	{
		.name = "hi3660_hi6403_mmap",
		.stream_name = "hi3660_hi6403_mmap",
		.cpus = &slimbus_cpu,
		.num_cpus = 1,
		.codecs = &hi6403_audio_codec,
		.num_codecs = 1,
		.platforms = &hifi_platform,
		.num_platforms = 1,
	},
};

static struct snd_soc_card hi3660_hi6403_card = {
	.name = "hi3660_HI6403_CARD",
	.owner = THIS_MODULE,
	.dai_link = hi3660_hi6403_dai_link,
	.num_links = ARRAY_SIZE(hi3660_hi6403_dai_link),
};

static int hi3660_hi6403_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &hi3660_hi6403_card;
	int ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "failed to register sound card: %d\n", ret);

	return ret;
}

static const struct of_device_id hi3660_hi6403_of_match[] = {
	{ .compatible = "hisilicon,hi3xxx-hi6403" },
	{ }
};
MODULE_DEVICE_TABLE(of, hi3660_hi6403_of_match);

static struct platform_driver hi3660_hi6403_driver = {
	.probe = hi3660_hi6403_probe,
	.driver = {
		.name = "hi3660_hi6403",
		.of_match_table = hi3660_hi6403_of_match,
	},
};
module_platform_driver(hi3660_hi6403_driver);

MODULE_AUTHOR("liuyang <liuyang66@hisilicon.com>");
MODULE_DESCRIPTION("ALSA SoC for Hisilicon hi3660 with hi6403 codec");
MODULE_LICENSE("GPL");
MODULE_ALIAS("machine driver:hi3660-hi6403");
