#
# Makefile for the memory technology device drivers.
#

# Core functionality.
-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk

obj-y				+= apr/
ifeq ($(strip $(CFG_ENABLE_BUILD_OM)),YES)
obj-y				+= comm/
obj-y				+= mdmcp/
obj-y				+= mdmap/
obj-y				+= mem/
endif

KBUILD_CPPFLAGS += -Idrivers/net/modem/Balong/765/drv/dump/apr \
                   -Idrivers/net/modem/Balong/765/drv/dump/comm \
                   -Idrivers/net/modem/Balong/765/drv/dump/mdmcp \
                   -Idrivers/net/modem/Balong/765/drv/dump/mdmap \
				   -Idrivers/net/modem/Balong/765/drv/dump/mem


