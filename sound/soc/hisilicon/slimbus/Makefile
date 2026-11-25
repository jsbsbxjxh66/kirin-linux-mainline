# slimbus driver
subdir-ccflags-y += -Wall -Werror
subdir-ccflags-y += -Wno-enum-conversion -Wno-ignored-qualifiers

ccflags-y += -Isound/soc/hisilicon/slimbus/vendor/candance/include
ccflags-y +=	-Isound/soc/hisilicon/slimbus/vendor/candance
ccflags-y +=	-Isound/soc/hisilicon
ccflags-y += -Idrivers/soc/hisilicon/rdr/blackbox/platform_hifi/
ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
ccflags-y += -I$(srctree)/sound/soc/hisilicon/slimbus
ccflags-y += -I$(srctree)/sound/soc/hisilicon/hifi_dsp
ccflags-y += -I$(srctree)/sound/soc/hisilicon

hisilicon-slimbus-platform-objs		:= vendor/candance/cps_bm.o vendor/candance/src/csmi.o vendor/candance/slimbus_drv.o
hisilicon-slimbus-platform-objs		+= slimbus.o slimbus_utils.o slimbus_64xx.o slimbus_6403.o 

ifeq ($(CONFIG_SND_SOC_HI6402),y)
ccflags-y += -DHI6402_SUPPORT
hisilicon-slimbus-platform-objs += slimbus_6402.o
endif

ifeq ($(CONFIG_SND_SOC_HI6405),y)
hisilicon-slimbus-platform-objs += slimbus_6405.o
endif


ifeq ($(CONFIG_HISI_AUDIO_DEBUG),y)
ifneq ($(kcov),true)
hisilicon-slimbus-platform-objs += slimbus_debug.o
endif
endif
obj-$(CONFIG_HISILICON_PLATFORM_SLIMBUS)		+= hisilicon-slimbus-platform.o
