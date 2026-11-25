# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the memory technology device drivers.
#

# Core functionality.
obj-$(CONFIG_MTD)		+= mtd.o
mtd-y				:= mtdcore.o mtdsuper.o mtdconcat.o mtdpart.o mtdchar.o
mtd-$(CONFIG_MTD_VIRT_CONCAT)	+= mtd_virt_concat.o

obj-y				+= parsers/

# 'Users' - code which presents functionality to userspace.
obj-$(CONFIG_MTD_BLKDEVS)	+= mtd_blkdevs.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o
obj-$(CONFIG_MTD_BLOCK_RO)	+= mtdblock_ro.o
obj-$(CONFIG_FTL)		+= ftl.o
obj-$(CONFIG_NFTL)		+= nftl.o
obj-$(CONFIG_INFTL)		+= inftl.o
obj-$(CONFIG_RFD_FTL)		+= rfd_ftl.o
obj-$(CONFIG_SSFDC)		+= ssfdc.o
obj-$(CONFIG_SM_FTL)		+= sm_ftl.o
obj-$(CONFIG_MTD_OOPS)		+= mtdoops.o
obj-$(CONFIG_MTD_PSTORE)	+= mtdpstore.o
obj-$(CONFIG_MTD_SWAP)		+= mtdswap.o

# Hisilicon NVE (non-volatile extension) character device.
obj-$(CONFIG_HISI_NVE)		+= hisi_nve.o

# hisi_nve_crc.h encodes CRC32C with the ARMv8 CRC32 instructions inline.  The
# compiler emits a bare ".arch armv8-a" line at the top of the generated
# assembly, which overrides any -Wa,-march=... we could add, so the arch has to
# be raised for the compiler itself -- for this translation unit only.
CFLAGS_hisi_nve.o		+= -march=armv8-a+crc

nftl-objs		:= nftlcore.o nftlmount.o
inftl-objs		:= inftlcore.o inftlmount.o

obj-y		+= chips/ lpddr/ maps/ devices/ nand/ tests/

obj-$(CONFIG_MTD_SPI_NOR)	+= spi-nor/
obj-$(CONFIG_MTD_UBI)		+= ubi/
obj-$(CONFIG_MTD_HYPERBUS)	+= hyperbus/
