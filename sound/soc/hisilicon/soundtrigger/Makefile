# hifi dsp device driver
subdir-ccflags-y += -Wall -Werror

ccflags-y += -Isound/soc/hisilicon/slimbus
ccflags-y += -Isound/soc/hisilicon/hifi_dsp
ccflags-y +=	-Iinclude/linux/hisi/hi64xx/
ccflags-y += -I$(srctree)/sound/soc/hisilicon/hifi_mailbox/mailbox
ccflags-y += -I$(srctree)/sound/soc/hisilicon/hifi_dsp
ccflags-y += -I$(srctree)/drivers/soc/hisilicon
ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
ccflags-y += -I$(srctree)/include/linux/hisi

obj-$(CONFIG_SOUND_TRIGGER)	+= mlib_ringbuffer.o
obj-$(CONFIG_SOUND_TRIGGER)	+= mlib_static_ringbuffer.o
obj-$(CONFIG_SOUND_TRIGGER)	+= soundtrigger_dma_drv.o
obj-$(CONFIG_SOUND_TRIGGER)	+= soundtrigger_event.o
obj-$(CONFIG_SOUND_TRIGGER)	+= soundtrigger_socdsp_mailbox.o
obj-$(CONFIG_SOUND_TRIGGER)	+= soundtrigger_socdsp_pcm.o

