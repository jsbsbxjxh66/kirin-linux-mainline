ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif
-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk 

# Mainline dropped EXTRA_CFLAGS support (scripts/Makefile.lib no longer
# does 'ccflags-y += $(EXTRA_CFLAGS)'); the whole include list would
# vanish silently.  ccflags-y is the modern equivalent.
ccflags-y += -Idrivers/tee/hisilicon
ifeq ($(strip $(CFG_CONFIG_SEC_RFILE)),YES)
obj-y += sec_rfile_balong.o
endif
obj-$(CONFIG_RFILE_SUPPORT)			+= rfile_balong.o
obj-$(CONFIG_ENABLE_TEST_CODE)         += rfile_api_test.o
