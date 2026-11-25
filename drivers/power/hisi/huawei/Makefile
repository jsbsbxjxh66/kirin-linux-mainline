# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(srctree)/include/linux/hisi/kirin970

obj-$(CONFIG_HUAWEI_POWER_MESG_INTERFACE) += power_mesg/
obj-$(CONFIG_HUAWEI_CHARGER) += charger/
ifneq ($(filter y,$(CONFIG_HUAWEI_BATTERY_INFORMATION) $(CONFIG_HUAWEI_BATTERY_VOLTAGE) $(CONFIG_HUAWEI_BATTERY_TEMP) $(CONFIG_HUAWEI_BATTERY_TYPE_IDENTIFY)),)
obj-y += battery/
endif
