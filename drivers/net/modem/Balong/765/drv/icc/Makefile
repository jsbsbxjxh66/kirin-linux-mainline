#gcov
ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
GCOV_PROFILE_icc_debug.o := n
endif

# Mainline dropped EXTRA_CFLAGS support (scripts/Makefile.lib no longer
# does 'ccflags-y += $(EXTRA_CFLAGS)'); the whole include list would
# vanish silently.  ccflags-y is the modern equivalent.
ccflags-y += -Idrivers/tee/hisilicon
obj-$(CONFIG_ICC_BALONG)       += icc_core.o  icc_linux.o  icc_debug.o
obj-$(CONFIG_CA_ICC)           += ca_icc.o
obj-$(CONFIG_ENABLE_TEST_CODE) += icc_test.o

subdir-ccflags-y := -Werror
