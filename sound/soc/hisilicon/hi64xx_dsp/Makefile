# hi6402 dsp device driver
#subdir-ccflags-y += -Wall -Werror
subdir-ccflags-y += -Wall

ccflags-y += -Isound/soc/hisilicon/slimbus/
ccflags-y += -Isound/soc/hisilicon/hifi_dsp/
ccflags-y += -Iinclude/linux/hisi/
ccflags-y += -Idrivers/soc/hisilicon/rdr/blackbox/platform_hifi/
ccflags-y += -Isound/soc/hisilicon/soundtrigger/
ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
ccflags-y += -I$(srctree)/sound/soc/hisilicon/hi64xx_dsp

ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
ifeq ($(kcov),true)
ccflags-y += -DENABLE_AUDIO_KCOV
else
ccflags-y += -DENABLE_HI64XX_HIFI_DEBUG
obj-y += hi64xx_hifi_file_ops.o
endif
#ifeq ($(CONFIG_SND_SOC_CODEC_STUB),y)
#	EXTRA_CFLAGS += -DHI6XXX_FPGA_CODEC_STUB
#endif
endif

ifeq ($(CONFIG_HIFI_IPC_3660),y)
	ccflags-y += -DPLATFORM_HI3660
endif

ifeq ($(CFG_CONFIG_HISI_FAMA),true)
	ccflags-y += -DCONFIG_HISI_FAMA
endif

ifeq ($(TARGET_VERSION_MODE),factory)
ccflags-y += -DAUDIO_FACTORY_MODE
endif

ifeq ($(CONFIG_SND_SOC_HI6402),y)
obj-$(CONFIG_HI6402_HIFI_MISC) += hi6402_hifi_config.o
endif

obj-$(CONFIG_HI6402_HIFI_MISC) += hi64xx_hifi_misc.o
obj-$(CONFIG_HI6402_HIFI_MISC) += hi6403_hifi_config.o

ifeq ($(CONFIG_SND_SOC_HI6405),y)
obj-$(CONFIG_SND_SOC_HI6405) += hi6405_hifi_config.o
endif


obj-$(CONFIG_HI6402_HIFI_MISC) += hi64xx_hifi_img_dl.o
obj-$(CONFIG_HI6402_HIFI_MISC) += hi64xx_hifi_om.o
obj-$(CONFIG_HI6402_HIFI_MISC) += hi64xx_hifi_anc_beta.o
