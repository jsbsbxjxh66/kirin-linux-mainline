# SPDX-License-Identifier: GPL-2.0
ccflags-y += -DIRQ_EN

obj-$(CONFIG_VIDEO_HISI_VENC) += hi_venc.o

hi_venc-y := venc_iommu.o \
	     venc_regulator.o \
	     drv_venc_intf.o \
	     drv_venc_efl.o \
	     drv_venc_osal.o \
	     drv_venc.o \
	     hi_drv_mem.o \
	     hal_venc.o
