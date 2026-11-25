ccflags-$(CONFIG_SND_SOC_HISILICON_SLIMBUS_DAI) += -Isound/soc/hisilicon/slimbus
ccflags-$(CONFIG_SND_SOC_PCM_HI6210) += -Isound/soc/hisilicon/hifi_mailbox/mailbox
ccflags-$(CONFIG_SND_SOC_PCM_HI6210) += -Isound/soc/hisilicon/hifi_dsp
# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_SND_I2S_HI6210_I2S) += hi6210-i2s.o
obj-$(CONFIG_SND_SOC_HISILICON_SLIMBUS_DAI) += slimbus_dai.o
obj-$(CONFIG_SND_SOC_HI3660_HI6403) += hi3660_hi6403.o
obj-$(CONFIG_SND_SOC_PCM_HI6210) += hisi_pcm_hifi.o

ifneq ($(filter y,$(CONFIG_HI6402_CODEC) $(CONFIG_HI6403_CODEC)),)
obj-y += hi64xx/
endif
obj-$(CONFIG_HI6402_HIFI_MISC) += hi64xx_dsp/
obj-$(CONFIG_HIFI_DSP_ONE_TRACK) += hifi_dsp/
obj-$(CONFIG_HIFI_MAILBOX) += hifi_mailbox/
obj-$(CONFIG_HISILICON_PLATFORM_SLIMBUS) += slimbus/
obj-$(CONFIG_SOUND_TRIGGER) += soundtrigger/
