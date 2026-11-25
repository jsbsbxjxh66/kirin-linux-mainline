subdir-ccflags-y += -I$(srctree)/include/linux/hisi/kirin970
subdir-ccflags-y += -I$(srctree)/drivers/power/hisi \
		    -I$(srctree)/drivers/power/hisi/coul \
		    -I$(srctree)/drivers/power/hisi/coul/hi6xxx \
		    -I$(srctree)/drivers/power/hisi/charger \
		    -I$(srctree)/drivers/power/hisi/charger/schargerV300 \
		    -I$(srctree)/lib/hisilicon/securec


obj-$(CONFIG_HISI_BATTERY_DATA)		+= hisi_battery_data.o
obj-$(CONFIG_HISI_BCI_BATTERY)		+= hisi_bci_battery.o
obj-$(CONFIG_HISI_COUL)		        += coul/
obj-$(CONFIG_HISI_CHARGER)		+= charger/
obj-$(CONFIG_HISI_SOH)	                += soh/
obj-$(CONFIG_HISI_ASW)	                += asw/
obj-y					+= huawei/

ifeq ($(HLTHERM_RUNTEST),true)
CFLAGS_KERNEL += -DCONFIG_HLTHERM_RUNTEST
endif

EXTRA_CFLAGS += -Ilib/hisilicon/securec/
