ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif

obj-$(CONFIG_BALONG_MODEM_RESET)        += reset_balong.o

# Mainline dropped EXTRA_CFLAGS support (scripts/Makefile.lib no longer
# does 'ccflags-y += $(EXTRA_CFLAGS)'); the whole include list would
# vanish silently.  ccflags-y is the modern equivalent.
ccflags-y += -Idrivers/tee/hisilicon

subdir-ccflags-y := -Werror
