# SPDX-License-Identifier: GPL-2.0
#
# RDR (Running Data Recorder) core.  Ported from the vendor tree; only the AP
# side is built here (the lpm3 platform and the diaginfo records are not part
# of this port); the hifi platform is pulled in by CONFIG_HISI_HIFI_BB because
# the hi64xx audio black box reports through it.
#
ccflags-y += -Wall
ccflags-y += -Idrivers/tee/hisilicon
ccflags-y += -Wno-error=date-time

obj-$(CONFIG_HISI_BB) += rdr_core.o
obj-$(CONFIG_HISI_BB) += rdr_common.o
obj-$(CONFIG_HISI_BB) += rdr_exception_core.o
obj-$(CONFIG_HISI_BB) += rdr_module_core.o
obj-$(CONFIG_HISI_BB) += rdr_dump_core.o
obj-$(CONFIG_HISI_BB) += rdr_logmonitor_core.o
obj-$(CONFIG_HISI_BB) += rdr_field_core.o

obj-$(CONFIG_HISI_BB) += platform_ap/
obj-$(CONFIG_HISI_HIFI_BB) += platform_hifi/
obj-$(CONFIG_HISI_BB) += rdr_cleartext.o
obj-$(CONFIG_HISI_BB) += rdr_exception_trace.o
