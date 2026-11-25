/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hisilicon Hi3670 ISP firmware ABI.
 *
 * The vendor firmware uses private resource-table entries and a shared
 * parameter block located at isp-remap-addr + 0xf000.  Keep the ABI
 * definitions here instead of extending the public remoteproc headers.
 */

#ifndef _HISI_ISP_RSC_H
#define _HISI_ISP_RSC_H

#include <linux/types.h>

/*
 * Vendor resource types.  These used to sit right after RSC_VDEV in the
 * vendor kernel; mainline treats 4..127 as unsupported, so the loader
 * remaps them into the vendor range before parsing the table.
 */
#define ISP_RSC_TYPE_VERSION		4
#define ISP_RSC_TYPE_RDR_MEMORY		5
#define ISP_RSC_TYPE_DYNAMIC_MEMORY	6
#define ISP_RSC_TYPE_RESERVED_MEMORY	7
#define ISP_RSC_TYPE_CDA		8
#define ISP_RSC_TYPE_SHARED_PARA	9

#define ISP_RSC_VENDOR_BASE		128
#define ISP_RSC_VENDOR(t)		(ISP_RSC_VENDOR_BASE + (t))
#define ISP_RSC_ORIGINAL(t)		((t) - ISP_RSC_VENDOR_BASE)

struct fw_rsc_version {
	u32 magic;
	char module[8];
	char version[8];
	char build_time[32];
	char reserved[4];
} __packed;

struct fw_rsc_dynamic_memory {
	u32 da;
	u32 pa;
	u32 len;
	u32 flags;
	u32 reserved;
	u8 name[32];
} __packed;

struct fw_rsc_reserved_memory {
	u32 da;
	u32 pa;
	u32 len;
	u32 flags;
	u32 reserved;
	u8 name[32];
} __packed;

struct fw_rsc_cda {
	u32 da;
	u32 len;
	u32 reserved;
	u8 name[32];
} __packed;

/* Offset of struct isp_shared_para inside the A7 remap window. */
#define ISP_SHARED_PARA_OFFSET		0xf000
#define ISP_SHARED_IRQ_NUM		64
#define ISP_SHARED_MONITOR_CHANS	4
#define ISP_SHARED_CLK_NUM		7

#define ISP_RDR_USE_APCTRL		BIT(31)
#define ISP_LOG_USE_APCTRL		BIT(31)

struct isp_plat_cfg {
	u32 platform_id;
	u32 isp_local_timer;
	u32 perf_power;
};

struct isp_bw_boot_status {
	u32 entry:1;
	u32 invalid_tlb:1;
	u32 enable_mmu:1;
	u32 reserved:29;
};

struct isp_fw_boot_status {
	u32 entry:1;
	u32 hard_boot_init:1;
	u32 hard_drv_init:1;
	u32 app_init:1;
	u32 reserved:28;
};

/*
 * Layout must match the vendor firmware exactly.  Only the fields touched
 * by the AP are documented; the rest is firmware-owned state.
 *
 * Note: the vendor definition (include/linux/rproc_share.h) is naturally
 * aligned, NOT __packed.  Packing it moves bootware_paddr from offset 112
 * to 108 and shrinks the block from 248 to 233 bytes, which corrupts every
 * field the AP and the ISP firmware share.
 */
struct isp_shared_para {
	struct isp_plat_cfg plat_cfg;
	int rdr_enable;
	u32 rdr_enable_type;
	u8 irq[ISP_SHARED_IRQ_NUM];
	int log_flush_flag;
	u32 log_head_size;
	u32 log_cache_write;
	struct isp_bw_boot_status bw_stat;
	struct isp_fw_boot_status fw_stat;
	u32 logx_switch;
	u64 bootware_paddr;
	u64 dynamic_pgtable_base;
	u64 coresight_addr_da;
	u64 coresight_addr_vir;
	u32 perf_switch;
	u64 perf_addr;
	u32 coredump_addr;
	u32 exc_flag;
	u64 mdc_pa_addr;
	u64 sec_smmuerr_addr;
	u32 monitor_addr[ISP_SHARED_MONITOR_CHANS];
	u32 monitor_ctrl;
	u32 monitor_pa_va;
	u32 monitor_hit_flag;
	u32 clk_value[ISP_SHARED_CLK_NUM];
	u8 isp_efuse;
};

#endif /* _HISI_ISP_RSC_H */
