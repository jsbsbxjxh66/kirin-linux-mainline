#
# Makefile for the memory technology device drivers.
#

# Core functionality.
-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk

obj-y				+= soft_decode/
obj-y				+= debug/
obj-y				+= ppm/
obj-y				+= cpm/
obj-y				+= scm/

KBUILD_CPPFLAGS += -Idrivers/net/modem/Balong/765/drv/diag/scm \
                   -Idrivers/net/modem/Balong/765/drv/diag/cpm \
                   -Idrivers/net/modem/Balong/765/drv/diag/ppm \
				   -Idrivers/net/modem/Balong/765/drv/diag/soft_decode \
				   -Idrivers/net/modem/Balong/765/drv/diag/debug \



