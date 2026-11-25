ifeq ($(strip $(llt_gcov)),y)
GCOV_PROFILE := y
endif

obj-$(CONFIG_HW_IP_BASE_ADDR)	+= hwadp_balong.o hwadp_core.o hwadp_debug.o

subdir-ccflags-y := -Werror
