ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif

-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk
# Mainline dropped EXTRA_CFLAGS support (scripts/Makefile.lib no longer
# does 'ccflags-y += $(EXTRA_CFLAGS)'); the whole include list would
# vanish silently.  ccflags-y is the modern equivalent.
ccflags-y += -Idrivers/tee/hisilicon \
		-Iscripts/dtc/libfdt

obj-y		    += load_image.o

ifeq ($(strip $(CFG_FEATURE_DELAY_MODEM_INIT)),FEATURE_ON)
obj-y		    += loadm_phone.o
else
obj-y		    += loadm_mbb.o
endif

#subdir-ccflags-y := -Werror
