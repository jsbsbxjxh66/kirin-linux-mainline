ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif

KBUILD_CPPFLAGS += -Idrivers/net/modem/Balong/765/drv/nvim
-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk

obj-y			+= sc_balong.o

subdir-ccflags-y := -Werror

