# Vendor build flavour and product identification.  The Android kernel exports
# these from its top-level Makefile; they are defined here instead so that no
# other part of this tree is affected.
#
# OBB_PRODUCT_NAME/CFG_PLATFORM select config/product/<name>/config/*.mk and
# the platform/<arch>/<name> include trees below.  TARGET_BUILD_VARIANT=user is
# what the vendor build uses (Android Makefile: TARGET_BUILD_VARIANT := user)
# and is required here: config/product/<name>/config/product_config.h includes
# the *_eng.h headers unless CHOOSE_MODEM_USER is defined, and those headers
# are produced by the vendor eng-build step rather than shipped in the tree.
TARGET_BUILD_VARIANT := user
OBB_PRODUCT_NAME ?= kirin970
CFG_PLATFORM ?= kirin970
TARGET_ARM_TYPE ?= arm64
export TARGET_BUILD_VARIANT OBB_PRODUCT_NAME CFG_PLATFORM TARGET_ARM_TYPE

choose_variant_modem ?= $(TARGET_BUILD_VARIANT)
ifneq ($(choose_variant_modem),)
export choose_variant_modem
endif
-include $(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config/balong_product_config.mk
subdir-ccflags-y += -I$(srctree)/lib/hisilicon/securec/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/platform/acore/$(CFG_PLATFORM)/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/platform/acore/$(CFG_PLATFORM)/drv/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/platform/common/$(CFG_PLATFORM)/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/platform/common/$(CFG_PLATFORM)/soc/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/adrv/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/drv/acore/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/drv/common/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/drv/common/include/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/product/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/acore/drv/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/acore/msp/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/acore/guc_as/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/acore/guc_nas/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/sys/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/drv/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/msp/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/pam/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/guc_as/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/guc_nas/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/nv/common/tl_as
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/taf/common/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/include/taf/acore/
subdir-ccflags-y += -I$(srctree)/drivers/net/modem/Balong/765/config/product/$(OBB_PRODUCT_NAME)/config

ifeq ($(strip $(MODEM_FULL_DUMP)),true)
subdir-ccflags-y += -DMODEM_FULL_DUMP_INUSER
endif

ifeq ($(strip $(MODEM_DDR_MINI_DUMP)),true)
subdir-ccflags-y += -DMODEM_DDR_MINI_DUMP_INUSER
endif

ifeq ($(strip $(MODEM_LOGCAT_INUSER)),true)
subdir-ccflags-y += -DMODEM_LOGCAT_INUSER
endif

ifeq ($(strip $(MODEM_LOG_FILTER_NVCONTROL)),true)
subdir-ccflags-y += -DMODEM_LOG_FILTER_INUSER
endif

ifeq ($(strip $(MODEM_CHR_INUSER)),true)
subdir-ccflags-y += -DMODEM_CHR_INUSER
endif

ifeq ($(strip $(MODEM_PM_OM_INUSER)),true)
subdir-ccflags-y += -DMODEM_PM_OM_INUSER
endif

ifeq ($(strip $(OBB_LLT_MDRV)),y)
subdir-ccflags-y += -DDRV_BUILD_LLT
endif


ifeq ($(LLT_BUILD_GCOV), y)
subdir-ccflags-y += -DDRV_BUILD_GCOV
endif

obj-$(CONFIG_HISI_BALONG_MODEM) += bastet_stub.o
obj-$(CONFIG_HISI_BALONG_MODEM) +=drv/

ifneq ($(chip_type),es)
KBUILD_CFLAGS += -DCHIP_TYPE_CS=FEATURE_ON
endif

ifeq ($(secdog),rsa2048)
KBUILD_CFLAGS += -DSECDOG_SUPPORT_RSA_2048=FEATURE_ON
endif


ifeq ($(ota_netlock),true)
KBUILD_CFLAGS += -DFEATURE_OTA_NETLOCK=FEATURE_ON
endif

ifeq ($(simlock_cust),true)
KBUILD_CFLAGS += -DFEATURE_SIMLOCK_CUST=FEATURE_ON
endif

ifneq ($(OBB_SEPARATE),true)
obj-$(CONFIG_HISI_BALONG_MODEM) +=oam/ ps/ taf/ 

#ifeq ($(CFG_FEATURE_IMS),FEATURE_ON)
obj-$(CONFIG_HISI_BALONG_MODEM) +=ims/
#endif

endif

#add modem user
ifeq ($(TARGET_BUILD_VARIANT),user)
KBUILD_CFLAGS += -DCHOOSE_MODEM_USER
endif

ifeq ($(cdma),false)
KBUILD_CFLAGS += -DFEATURE_UE_MODE_CDMA=FEATURE_OFF
KBUILD_CFLAGS += -DFEATURE_CSDR=FEATURE_OFF
KBUILD_CFLAGS += -DFEATURE_BALONG_CL=FEATURE_OFF
endif
