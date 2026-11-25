# Board platform register/address maps (soc_*_interface.h, mntn_*_map.h) live
# beside the mntn sources rather than under include/, as in the vendor tree.
subdir-ccflags-y		+= -I$(srctree)/include/linux/hisi/kirin970 \
				   -I$(srctree)/drivers/tee/hisilicon \
				   -I$(srctree)/lib/hisilicon/securec \
				   -I$(srctree)/drivers/soc/hisilicon/rdr

obj-$(CONFIG_HISI_BB)		+= blackbox/
obj-$(CONFIG_HISI_BB)		+= bl31/
obj-$(CONFIG_HISI_BB)		+= hisi_compat.o
obj-$(CONFIG_HISI_BB)		+= util.o
obj-$(CONFIG_HISI_BB)		+= hisi_bootup_keypoint.o
obj-$(CONFIG_HISI_BB)		+= mntn_filesys.o
obj-$(CONFIG_HISI_MNTNDUMP)	+= mntn_dump.o
obj-$(CONFIG_HISI_KERNELDUMP)	+= kernel_dump.o
