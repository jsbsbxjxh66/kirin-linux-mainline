# hifi dsp device driver
ifeq ($(CONFIG_HIFI_IPC_3650),y)
ccflags-y += -DPLATFORM_HI3XXX
ccflags-y += -DPLATFORM_HI365X
endif

ifeq ($(CONFIG_HIFI_IPC_6250),y)
ccflags-y += -DPLATFORM_HI3XXX
ccflags-y += -DPLATFORM_HI6250
#todo fixme: hi6250(PLATFORM_FPGA should be deleted on asic)
ccflags-y += -DPLATFORM_FPGA
endif

ccflags-y += -Isound/soc/hisilicon/hifi_mailbox/mailbox
ccflags-y += -Isound/soc/hisilicon/hifi_mailbox/ipcm

ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
ccflags-y += -DENABLE_HIFI_DEBUG
endif

ccflags-y += -Isound/soc/hisilicon/hifi_dsp

obj-$(CONFIG_VOICE_PROXY) += voice_proxy.o
obj-$(CONFIG_VOICE_PROXY_VOLTE) += voice_proxy_volte.o
obj-$(CONFIG_VOICE_PROXY_VOWIFI) += voice_proxy_vowifi.o
obj-$(CONFIG_VOICE_PROXY_RTT) += voice_proxy_rtt.o
