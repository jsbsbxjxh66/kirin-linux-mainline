obj-y += rdr_hisi_audio_adapter.o
obj-y += rdr_hisi_audio_codec.o
obj-y += rdr_hisi_audio_soc.o

ifeq ($(TARGET_VERSION_MODE),factory)
ccflags-y += -DAUDIO_FACTORY_MODE
endif

ccflags-y += -Isound/soc/hisilicon/hifi_dsp/
ccflags-y += -Isound/soc/hisilicon/hi64xx_dsp/
ccflags-y += -Idrivers/soc/hisilicon/rdr/blackbox/
ccflags-y += -Isound/usb/hifi/

ccflags-y += -I$(srctree)/drivers/tee/hisilicon

ccflags-y += -DSECOS_RELOAD_HIFI
ccflags-y += -DRDR_SOCHIFI
ccflags-y += -DRDR_CODECDSP

