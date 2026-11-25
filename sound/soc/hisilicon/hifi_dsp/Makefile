subdir-ccflags-y += -Wall -Werror

ifeq ($(CLT_IV),true)
ccflags-y += -DCLT_VOICE
endif

# hifi dsp device driver
ifeq ($(CFG_CONFIG_HISI_FAMA),true)
LOCAL_CFLAGS += -DCONFIG_HISI_FAMA
endif

ifeq ($(CONFIG_HISI_FB_970),y)
ccflags-y += -DCONFIG_DP_MULTI_CHANNELS
endif

ifeq ($(CONFIG_HISI_FB_V501),y)
ccflags-y += -DCONFIG_DP_MULTI_CHANNELS
endif

ifeq ($(CONFIG_HIFI_IPC_6250),y)
ccflags-y += -DPLATFORM_HI6250
ccflags-y += -DEXPAND_HIFI_RUNNING_REGION
endif

ifeq ($(CONFIG_SND_SOC_HI6402),y)
ccflags-y += -DMULTI_MIC
endif

ccflags-y += -Isound/soc/hisilicon/hifi_mailbox/mailbox
ccflags-y += -Isound/soc/hisilicon/hifi_mailbox/ipcm
ccflags-y += -Isound/usb/hifi/
ccflags-y += -Isound/soc/hisilicon/hifi_dsp
ccflags-y += -Isound/soc/hisilicon/hifi_dsp/dsm_audio
ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
ccflags-y += -I$(srctree)/include/linux/hisi
ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
ifeq ($(kcov),true)
ccflags-y += -DENABLE_AUDIO_KCOV
else
ccflags-y += -DENABLE_HIFI_DEBUG
endif
endif

obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= hifi_lpp.o
obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= hifi_om.o
obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= dsm_audio.o
obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= usbaudio_stub.o

ifneq ($(HISI_EXTERNAL_MODEM), )
obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= audio_pcie_proc.o
obj-$(CONFIG_HIFI_DSP_ONE_TRACK)	+= b5000_strategy_acore.o
ccflags-y += -DHISI_EXTERNAL_MODEM
endif
ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
obj-$(CONFIG_SND_SOC_CODEC_STUB)	+= memcpy_opt.o
ifeq ($(CONFIG_SND_SOC_CODEC_STUB),y)
ccflags-y += -DDYNAMIC_LOAD_HIFIIMG
endif
endif

obj-y	+= voice_proxy/
