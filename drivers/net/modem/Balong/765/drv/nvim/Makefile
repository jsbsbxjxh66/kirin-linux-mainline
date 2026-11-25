ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif

-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk

subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/product/


ifeq ($(strip $(CFG_FEATURE_NV_FLASH_ON)),YES)
obj-$(CONFIG_NVIM)		    += nv_flash.o
endif
ifeq ($(strip $(CFG_FEATURE_NV_EMMC_ON)),YES)
obj-$(CONFIG_NVIM)		    += nv_emmc.o
endif
obj-$(CONFIG_NVIM)         += nv_ctrl.o nv_comm.o nv_base.o nv_cust.o nv_xml_dec.o nv_debug.o nv_crc.o nv_index.o nv_partition_upgrade.o nv_partition_img.o nv_partition_bakup.o nv_factory_check.o nv_msg.o nv_proc.o

subdir-ccflags-y := -Werror

