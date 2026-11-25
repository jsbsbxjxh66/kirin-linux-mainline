# SPDX-License-Identifier: GPL-2.0

_PRE_PRODUCT_VERSION ?= 1102a
HI1102_BOARD_VERSION ?= default
PLAT_DEFCONFIG_FILE ?= wifi_$(_PRE_PRODUCT_VERSION)_$(HI1102_BOARD_VERSION)_defconfig
HI1102_COMM_DEFCONFIG := hi$(_PRE_PRODUCT_VERSION)_comm_defconfig

HI1102_DRIVER_SRC_PATH := $(srctree)/$(src)/..
HI110X_DRIVER_SRC_PATH := $(HI1102_DRIVER_SRC_PATH)

WIFI_DRIVER_PATH := $(HI1102_DRIVER_SRC_PATH)/wifi
PLAT_DRIVER_PATH := $(HI1102_DRIVER_SRC_PATH)/platform

ifneq ($(wildcard $(PLAT_DRIVER_PATH)/$(HI1102_COMM_DEFCONFIG)),)
include $(PLAT_DRIVER_PATH)/$(HI1102_COMM_DEFCONFIG)
include $(WIFI_DRIVER_PATH)/$(PLAT_DEFCONFIG_FILE)
endif

hmac-objs := hmac_main.o hmac_rx_data.o hmac_tx_amsdu.o hmac_tx_data.o hmac_user.o hmac_vap.o hmac_mgmt_bss_comm.o hmac_mgmt_classifier.o hmac_fsm.o hmac_mgmt_ap.o hmac_mgmt_sta.o hmac_encap_frame_ap.o hmac_encap_frame_sta.o hmac_encap_frame.o hmac_sme_sta.o hmac_11i.o hmac_uapsd.o hmac_config.o hmac_protection.o hmac_frag.o hmac_crypto_tkip.o hmac_chan_mgmt.o hmac_m2u.o hmac_smps.o hmac_data_acq.o hmac_rx_filter.o hmac_proxy_arp.o hmac_hcc_adapt.o hmac_reset.o hmac_scan.o hmac_p2p.o hmac_device.o hmac_resource.o  hmac_wapi_wpi.o hmac_wapi_sms4.o hmac_wapi.o hmac_cali_mgmt.o hmac_cali_dpd.o
hmac-objs += hmac_custom_security.o hmac_scan.o hmac_blockack.o hmac_dfs.o hmac_psm_ap.o hmac_dfx.o hmac_edca_opt.o hmac_arp_offload.o hmac_verify/hmac_test_main.o  hmac_tcp_opt.o hmac_roam_alg.o hmac_roam_connect.o hmac_roam_main.o
hmac-objs += hmac_traffic_classify.o hmac_proxysta.o hmac_wmmac.o hmac_statistic_data_flow.o
hmac-objs += hmac_btcoex.o hmac_11v.o
hmac-objs := $(addprefix hmac/,$(hmac-objs))

mac-objs := mac_device.o mac_frame.o mac_user.o mac_vap.o mac_regdomain.o mac_ie.o mac_resource.o mac_board.o mac_pm.o mac_data.o
mac-objs := $(addprefix mac_ec/,$(mac-objs))
mac-ram-objs := mac_board_ram.o mac_data_ram.o mac_device_ram.o mac_frame_ram.o mac_ie_ram.o mac_pm_ram.o mac_regdomain_ram.o mac_resource_ram.o mac_user_ram.o mac_vap_ram.o
mac-ram-objs := $(addprefix mac_ram/,$(mac-ram-objs))

hmac-objs += $(mac-objs)
hmac-objs += $(mac-ram-objs)

wal-objs := wal_config.o wal_config_debug.o wal_linux_bridge.o  wal_main.o wal_linux_scan.o wal_linux_rx_rsp.o wal_linux_event.o wal_regdb.o wal_config_acs.o wal_linux_flowctl.o  wal_linux_ioctl.o wal_linux_ioctl_debug.o wal_linux_atcmdsrv.o
wal-objs += wal_linux_cfg80211.o wal_dfx.o wal_linux_cfgvendor.o
wal-objs := $(addprefix wal/,$(wal-objs))

main-objs := main.o
main-objs := $(addprefix main/,$(main-objs))

#wifi.ko
wifi-objs := $(wal-objs)

wifi-objs += $(main-objs)

wifi-objs += $(hmac-objs)

wifi_$(_PRE_PRODUCT_VERSION)-objs = $(wifi-objs)

#main.ko
obj-y += wifi_$(_PRE_PRODUCT_VERSION).o

HI1102_INCLUDES := -I$(WIFI_DRIVER_PATH)/inc

ifeq ($(HI110X_DRIVER_VERSION),1102)
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/board
else
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/board
endif

HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/inc/hmac
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/inc/wal
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/hmac
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/wal

ifeq ($(HI110X_DRIVER_VERSION),1102)
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc/frw
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc/oal
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc/oal/linux
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc/pm
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/inc/oam
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/oam
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/pm
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/oal
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/frw
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/platform/sdt
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/common/customize
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/common/inc
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/common/chr_log
HI1102_INCLUDES += -I$(HI1102_DRIVER_SRC_PATH)/../1102/common/oneimage
else
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc/frw
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc/oal
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc/oal/linux
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc/pm
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/inc/oam
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/oam
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/pm
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/oal
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/frw
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/sdt
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/../common/customize
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/../common/inc
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/../common/chr_log
HI1102_INCLUDES += -I$(PLAT_DRIVER_PATH)/../common/oneimage
endif

HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/inc/dmac
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/inc/hal
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/hal

HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/hal/hal_verify/
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/hmac/hmac_verify/
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/dmac/dmac_verify/
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/alg/alg_verify
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/mac_ec
HI1102_INCLUDES += -I$(WIFI_DRIVER_PATH)/alg

ifneq ($(TARGET_BUILD_VARIANT),user)
CCFLAGS_EXTRA += -DPLATFORM_DEBUG_ENABLE
endif

ccflags-y := $(HI1102_INCLUDES)
ccflags-y += $(COPTS)
ccflags-y += $(COMM_COPTS)
ccflags-y += $(CCFLAGS_EXTRA)
ccflags-y += -I$(srctree)/drivers/net/wireless/hisi
