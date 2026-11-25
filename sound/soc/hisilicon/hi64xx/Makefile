subdir-ccflags-y += -Wall -Werror

ccflags-y += -Isound/soc/hisilicon/hi64xx_dsp/
ccflags-y += -Isound/soc/hisilicon/soundtrigger/
ccflags-y += -Isound/soc/codecs/hisi/hs_auto_calib/
ccflags-y += -Idrivers/soc/hisilicon/rdr/blackbox/platform_hifi/

ifeq ($(CLT_IV), true)
ccflags-y += -DCLT_AUDIO
endif

ifeq ($(CONFIG_HI6402_MBHC_ANC),y)
ccflags-y += -DANC_HEADSET
ccflags-y += -DINVERT_HEADSET
endif

ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
ccflags-y += -DCONFIG_HUAWEI_HEADSET_DEBUG
ifeq ($(kcov),true)
ccflags-y += -DKCOV_AUDIO
else
ccflags-y += -DENABLE_HI64XX_CODEC_DEBUG
obj-y += hi64xx_mad.o
endif
#ifeq ($(CONFIG_SND_SOC_CODEC_STUB),y)
#EXTRA_CFLAGS += -DHI6XXX_FPGA_CODEC_STUB
#endif
endif

ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
ccflags-y += -I$(srctree)/sound/soc/hisilicon/hifi_dsp
ccflags-y += -I$(srctree)/sound/soc/hisilicon/slimbus

obj-y += hi_cdc_ctrl.o
obj-y += hi_cdc_ssi.o
obj-y += hi64xx_resmgr.o
obj-y += hi64xx_mbhc.o
obj-y += hi64xx_irq.o
obj-y += hi64xx_compat.o
obj-y += hi64xx_utils.o
obj-y += hi64xx_vad.o
obj-y += asp_dma.o
