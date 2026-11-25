// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hisilicon Hi3670 ISP remote processor
 *
 * The vendor driver kept a large private extension of struct rproc and a
 * vendor-specific firmware loader.  This file is the mainline-style base:
 * the platform device, register windows and remoteproc registration use only
 * public remoteproc APIs.  The ISP SMMU, resource-table handlers, shared
 * memory and mailbox transport will be added on top of this skeleton.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/hisi/isp_iommu.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/remoteproc.h>
#include <linux/slab.h>
#include <linux/virtio_ring.h>

#include <asm/cacheflush.h>

#include "../remoteproc_internal.h"
#include "isp_rsc.h"

#define ISP_CORE_REG		0
#define ISP_CRG_REG		1
#define ISP_NUM_REGS		9

#define CRG_C90_PERIPHISP_ISPA7_CTRL	0xc90
#define ISPA7_DBGPWRDUP			BIT(2)
#define ISPA7_VINITHI_HIGH		BIT(1)
#define ISPA7_REMAP_ENABLE		BIT(11)

struct hisi_isp_mapping {
	struct list_head node;
	u64 da;
	size_t len;
	void *va;
	dma_addr_t dma;
	/* true when @va was allocated by this driver and must be freed */
	bool owned;
};

struct hisi_isp_rproc {
	struct device *dev;
	struct rproc *rproc;
	void __iomem *regs[ISP_NUM_REGS];
	struct resource *res[ISP_NUM_REGS];
	phys_addr_t remap_addr;
	phys_addr_t rdr_addr;
	phys_addr_t mdc_addr;
	u32 local_timer;
	struct list_head mappings;
	/* protects the IOVA mapping list */
	struct mutex map_lock;
	struct clk_bulk_data *clks;
	int num_clks;
	struct regulator *core_supply;
	struct regulator *subsys_supply;
	struct mbox_client mbox_client;
	struct mbox_chan *tx_mbox;
	struct mbox_chan *rx_mbox;
};

static void hisi_isp_rproc_power(struct hisi_isp_rproc *hisp, bool on)
{
	void __iomem *crg = hisp->regs[ISP_CRG_REG];
	u32 val;

	if (!crg)
		return;

	if (!on) {
		val = readl(crg + CRG_C90_PERIPHISP_ISPA7_CTRL);
		val &= ~(ISPA7_REMAP_ENABLE | ISPA7_DBGPWRDUP);
		writel(val, crg + CRG_C90_PERIPHISP_ISPA7_CTRL);
		return;
	}

	if (!hisp->remap_addr)
		return;

	val = (lower_32_bits(hisp->remap_addr) >> 4) |
	      ISPA7_VINITHI_HIGH | ISPA7_REMAP_ENABLE | ISPA7_DBGPWRDUP;
	writel(val, crg + CRG_C90_PERIPHISP_ISPA7_CTRL);
}

static int hisi_isp_rewrite_rsc_types(struct rproc *rproc, bool to_vendor);

static int hisi_isp_rproc_start(struct rproc *rproc)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	int ret;

	ret = hisi_isp_rewrite_rsc_types(rproc, false);
	if (ret)
		return ret;

	if (hisp->subsys_supply) {
		ret = regulator_enable(hisp->subsys_supply);
		if (ret)
			return ret;
	}

	if (hisp->core_supply) {
		ret = regulator_enable(hisp->core_supply);
		if (ret)
			goto err_subsys;
	}

	ret = clk_bulk_prepare_enable(hisp->num_clks, hisp->clks);
	if (ret)
		goto err_core;

	hisi_isp_rproc_power(hisp, true);
	isp_iommu_program(hisp->regs[ISP_CORE_REG]);
	return 0;

err_core:
	if (hisp->core_supply)
		regulator_disable(hisp->core_supply);
err_subsys:
	if (hisp->subsys_supply)
		regulator_disable(hisp->subsys_supply);
	return ret;
}

static int hisi_isp_rproc_stop(struct rproc *rproc)
{
	struct hisi_isp_rproc *hisp = rproc->priv;

	hisi_isp_rproc_power(hisp, false);
	clk_bulk_disable_unprepare(hisp->num_clks, hisp->clks);
	if (hisp->core_supply)
		regulator_disable(hisp->core_supply);
	if (hisp->subsys_supply)
		regulator_disable(hisp->subsys_supply);
	return 0;
}

static struct hisi_isp_mapping *
hisi_isp_find_mapping(struct hisi_isp_rproc *hisp, u64 da, size_t len)
{
	struct hisi_isp_mapping *map;

	list_for_each_entry(map, &hisp->mappings, node) {
		if (da < map->da)
			continue;
		if (da - map->da + len > map->len)
			continue;
		return map;
	}

	return NULL;
}

/*
 * Add or refresh an IOVA mapping.  The caller must hold map_lock.  Existing
 * mappings are updated in place because a reboot reallocates carveouts but
 * keeps the firmware's device addresses unchanged.
 */
static struct hisi_isp_mapping *
hisi_isp_map_range(struct hisi_isp_rproc *hisp, u64 da, size_t len,
		   dma_addr_t dma, void *va, bool owned)
{
	struct hisi_isp_mapping *map;
	bool fresh = false;
	int ret;

	if (!len || !IS_ALIGNED(da, PAGE_SIZE) || !IS_ALIGNED(dma, PAGE_SIZE))
		return ERR_PTR(-EINVAL);

	map = hisi_isp_find_mapping(hisp, da, len);
	if (map && map->dma == dma && map->len == PAGE_ALIGN(len)) {
		map->va = va;
		map->owned = owned;
		return map;
	}

	if (map) {
		isp_iommu_unmap(map->da, map->len);
	} else {
		map = kzalloc_obj(*map);
		if (!map)
			return ERR_PTR(-ENOMEM);
		fresh = true;
	}

	ret = isp_iommu_map(da, dma, PAGE_ALIGN(len),
			    IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		if (!fresh) {
			/* Old IOVA was unmapped: release its owned buffer too. */
			if (map->owned && map->va)
				dma_free_coherent(hisp->dev, map->len,
						  map->va, map->dma);
			list_del(&map->node);
		}
		kfree(map);
		return ERR_PTR(ret);
	}

	map->da = da;
	map->len = PAGE_ALIGN(len);
	map->va = va;
	map->dma = dma;
	map->owned = owned;
	if (fresh)
		list_add_tail(&map->node, &hisp->mappings);

	dev_dbg(hisp->dev, "mapped ISP iova %#llx -> %pad size %#zx\n",
		da, &dma, map->len);
	return map;
}

static int hisi_isp_add_mapping(struct hisi_isp_rproc *hisp, u64 da, size_t len)
{
	struct hisi_isp_mapping *map;
	u64 start = da & PAGE_MASK;
	size_t size = PAGE_ALIGN(da + len) - start;
	dma_addr_t dma;
	void *va;

	if (!size)
		return -EINVAL;

	va = dma_alloc_coherent(hisp->dev, size, &dma, GFP_KERNEL);
	if (!va)
		return -ENOMEM;

	map = hisi_isp_map_range(hisp, start, size, dma, va, true);
	if (IS_ERR(map)) {
		dma_free_coherent(hisp->dev, size, va, dma);
		return PTR_ERR(map);
	}

	return 0;
}

static void *hisi_isp_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len,
				     bool *is_iomem)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct rproc_mem_entry *mem;
	struct hisi_isp_mapping *map;
	void *va = NULL;

	if (is_iomem)
		*is_iomem = false;

	list_for_each_entry(mem, &rproc->carveouts, node) {
		if (mem->va && da >= mem->da &&
		    da + len <= mem->da + mem->len)
			return mem->va + (da - mem->da);
	}

	if (!isp_iommu_ready())
		return NULL;

	mutex_lock(&hisp->map_lock);
	map = hisi_isp_find_mapping(hisp, da, len);
	if (!map && len && !hisi_isp_add_mapping(hisp, da, len))
		map = hisi_isp_find_mapping(hisp, da, len);
	if (map)
		va = map->va + (da - map->da);
	mutex_unlock(&hisp->map_lock);

	return va;
}

static void hisi_isp_mappings_exit(void *data)
{
	struct hisi_isp_rproc *hisp = data;
	struct hisi_isp_mapping *map, *tmp;

	mutex_lock(&hisp->map_lock);
	list_for_each_entry_safe(map, tmp, &hisp->mappings, node) {
		isp_iommu_unmap(map->da, map->len);
		if (map->owned)
			dma_free_coherent(hisp->dev, map->len, map->va,
					  map->dma);
		list_del(&map->node);
		kfree(map);
	}
	mutex_unlock(&hisp->map_lock);
}

static struct rproc_mem_entry *
hisi_isp_find_carveout(struct rproc *rproc, const char *name)
{
	struct rproc_mem_entry *mem;

	list_for_each_entry(mem, &rproc->carveouts, node) {
		if (!strcmp(mem->name, name))
			return mem;
	}

	return NULL;
}

static int hisi_isp_carveout_alloc(struct rproc *rproc,
				   struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	size_t size = PAGE_ALIGN(mem->len);
	dma_addr_t dma;
	void *va;
	int ret;

	va = dma_alloc_coherent(dev, size, &dma, GFP_KERNEL | GFP_DMA32);
	if (!va)
		return -ENOMEM;

	if (mem->da == FW_RSC_ADDR_ANY)
		mem->da = lower_32_bits(dma);

	ret = isp_iommu_map(mem->da, dma, size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "failed to map carveout %s: %d\n", mem->name, ret);
		dma_free_coherent(dev, size, va, dma);
		return ret;
	}

	mem->va = va;
	mem->dma = dma;

	return 0;
}

static int hisi_isp_carveout_release(struct rproc *rproc,
				     struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	size_t size = PAGE_ALIGN(mem->len);

	isp_iommu_unmap(mem->da, size);
	dma_free_coherent(dev, size, mem->va, mem->dma);

	return 0;
}

static int hisi_isp_add_carveout(struct rproc *rproc, const char *name,
				 u32 da, u32 len, u32 rsc_offset, u32 flags)
{
	struct rproc_mem_entry *mem;

	mem = hisi_isp_find_carveout(rproc, name);
	if (mem)
		return 0;

	mem = rproc_mem_entry_init(&rproc->dev, NULL, 0, len, da,
				   hisi_isp_carveout_alloc,
				   hisi_isp_carveout_release, "%s", name);
	if (!mem)
		return -ENOMEM;

	mem->flags = flags;
	mem->rsc_offset = rsc_offset;
	rproc_add_carveout(rproc, mem);

	return 0;
}

static int hisi_isp_handle_carveout(struct rproc *rproc, void *ptr,
				    int offset, int avail)
{
	struct fw_rsc_carveout *rsc = ptr;

	if (sizeof(*rsc) > avail || rsc->reserved || !rsc->len)
		return -EINVAL;

	return hisi_isp_add_carveout(rproc, rsc->name, rsc->da, rsc->len,
				     offset, rsc->flags);
}

static int hisi_isp_devmem_release(struct rproc *rproc,
				   struct rproc_mem_entry *mem)
{
	iounmap(mem->priv);
	isp_iommu_unmap(mem->da, PAGE_ALIGN(mem->len));

	return 0;
}

/*
 * Map a firmware-provided physical range into the ISP SMMU and keep the
 * mapping alive through a rproc carveout entry.  The memory is owned by the
 * firmware/hardware, so the release callback only drops the mapping.
 */
static int hisi_isp_add_phys_mem(struct rproc *rproc, const char *name,
				 u32 da, u32 len, phys_addr_t pa,
				 bool write_combine, u32 flags, void **out_va)
{
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *mem;
	size_t size = PAGE_ALIGN(len);
	void *va;
	int ret;

	if (!len || !IS_ALIGNED(da, PAGE_SIZE) || !IS_ALIGNED(pa, PAGE_SIZE))
		return -EINVAL;

	va = write_combine ? ioremap_wc(pa, len) : ioremap(pa, len);
	if (!va) {
		dev_err(dev, "failed to ioremap %s at %pa\n", name, &pa);
		return -ENOMEM;
	}

	ret = isp_iommu_map(da, pa, size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "failed to map %s into ISP SMMU: %d\n", name, ret);
		goto err_iounmap;
	}

	mem = rproc_mem_entry_init(&rproc->dev, NULL, pa, len, da, NULL,
				   hisi_isp_devmem_release, "%s", name);
	if (!mem) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	mem->va = va;
	mem->priv = va;
	mem->flags = flags;
	mem->rsc_offset = FW_RSC_ADDR_ANY;
	rproc_add_carveout(rproc, mem);

	if (out_va)
		*out_va = va;

	return 0;

err_unmap:
	isp_iommu_unmap(da, size);
err_iounmap:
	iounmap(va);
	return ret;
}

static int hisi_isp_handle_devmem(struct rproc *rproc, void *ptr,
				  int offset, int avail)
{
	struct fw_rsc_devmem *rsc = ptr;

	if (sizeof(*rsc) > avail || rsc->reserved)
		return -EINVAL;

	return hisi_isp_add_phys_mem(rproc, rsc->name, rsc->da, rsc->len,
				     rsc->pa, false, rsc->flags, NULL);
}

static int hisi_isp_handle_version(struct rproc *rproc, void *ptr, int avail)
{
	struct fw_rsc_version *rsc = ptr;

	if (sizeof(*rsc) > avail)
		return -EINVAL;

	dev_info(rproc->dev.parent, "ISP firmware %.*s %.*s (%.*s)\n",
		 (int)sizeof(rsc->module), rsc->module,
		 (int)sizeof(rsc->version), rsc->version,
		 (int)sizeof(rsc->build_time), rsc->build_time);

	return 0;
}

static int hisi_isp_handle_reserved(struct rproc *rproc, void *ptr, int avail)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct fw_rsc_reserved_memory *rsc = ptr;
	phys_addr_t pa;

	if (sizeof(*rsc) > avail || rsc->reserved)
		return -EINVAL;

	if (!strcmp(rsc->name, "ISP_MEM_BOOTWARE")) {
		if (!hisp->remap_addr)
			return -EINVAL;
		pa = hisp->remap_addr;
	} else if (!strcmp(rsc->name, "ISP_MDH_MEM")) {
		if (!hisp->mdc_addr) {
			dev_warn(rproc->dev.parent,
				 "MDC memory requested but no mdc address\n");
			return 0;
		}
		pa = hisp->mdc_addr;
	} else {
		pa = rsc->pa;
	}

	return hisi_isp_add_phys_mem(rproc, rsc->name, rsc->da, rsc->len,
				     pa, false, rsc->flags, NULL);
}

static int hisi_isp_handle_dynamic(struct rproc *rproc, void *ptr, int avail)
{
	struct fw_rsc_dynamic_memory *rsc = ptr;
	struct device *dev = rproc->dev.parent;
	struct rproc_mem_entry *mem;
	dma_addr_t dma;
	size_t size;
	void *va;
	int ret;

	if (sizeof(*rsc) > avail || rsc->reserved || !rsc->len)
		return -EINVAL;

	size = PAGE_ALIGN(rsc->len);
	va = dma_alloc_coherent(dev, size, &dma, GFP_KERNEL | GFP_DMA32);
	if (!va)
		return -ENOMEM;

	if (rsc->da == FW_RSC_ADDR_ANY)
		rsc->da = lower_32_bits(dma);

	ret = isp_iommu_map(rsc->da, dma, size, IOMMU_READ | IOMMU_WRITE);
	if (ret) {
		dev_err(dev, "failed to map dynamic memory %s: %d\n",
			rsc->name, ret);
		goto err_free;
	}

	mem = rproc_mem_entry_init(&rproc->dev, NULL, dma, size, rsc->da,
				   NULL, hisi_isp_carveout_release,
				   "%s", rsc->name);
	if (!mem) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	mem->va = va;
	mem->flags = rsc->flags;
	mem->rsc_offset = FW_RSC_ADDR_ANY;
	rproc_add_carveout(rproc, mem);

	return 0;

err_unmap:
	isp_iommu_unmap(rsc->da, size);
err_free:
	dma_free_coherent(dev, size, va, dma);
	return ret;
}

static int hisi_isp_handle_rdr(struct rproc *rproc, void *ptr, int avail)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct fw_rsc_carveout *rsc = ptr;

	if (sizeof(*rsc) > avail || rsc->reserved)
		return -EINVAL;

	if (!hisp->rdr_addr) {
		dev_dbg(rproc->dev.parent, "RDR memory disabled\n");
		return 0;
	}

	return hisi_isp_add_phys_mem(rproc, rsc->name, rsc->da, rsc->len,
				     hisp->rdr_addr, false, rsc->flags, NULL);
}

static int hisi_isp_handle_cda(struct rproc *rproc, void *ptr, int avail)
{
	struct fw_rsc_cda *rsc = ptr;
	struct rproc_debug_trace *trace;
	char name[15];

	if (sizeof(*rsc) > avail || rsc->reserved)
		return -EINVAL;

	trace = kzalloc_obj(*trace);
	if (!trace)
		return -ENOMEM;

	trace->trace_mem.len = rsc->len;
	trace->trace_mem.da = rsc->da;
	trace->rproc = rproc;

	snprintf(name, sizeof(name), "cda%d", rproc->num_traces);
	trace->tfile = rproc_create_trace_file(name, rproc, trace);

	list_add_tail(&trace->node, &rproc->traces);
	rproc->num_traces++;

	return 0;
}

static int hisi_isp_handle_shared_para(struct rproc *rproc, void *ptr,
				       int avail)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct fw_rsc_carveout *rsc = ptr;
	struct isp_shared_para *para;
	phys_addr_t pa;
	void *va;
	int ret;

	if (sizeof(*rsc) > avail || rsc->reserved || !rsc->len)
		return -EINVAL;

	if (!hisp->remap_addr) {
		dev_err(rproc->dev.parent,
			"shared parameters require isp-remap-addr\n");
		return -EINVAL;
	}

	pa = hisp->remap_addr + ISP_SHARED_PARA_OFFSET;
	ret = hisi_isp_add_phys_mem(rproc, rsc->name, rsc->da, rsc->len,
				    pa, true, rsc->flags, &va);
	if (ret)
		return ret;

	para = va;
	memset(para, 0, min_t(size_t, rsc->len, sizeof(*para)));
	if (rsc->len >= offsetofend(struct isp_shared_para, bootware_paddr)) {
		para->plat_cfg.isp_local_timer = hisp->local_timer;
		para->logx_switch = ISP_LOG_USE_APCTRL;
		para->bootware_paddr = hisp->remap_addr;
	}

	return 0;
}

static int hisi_isp_rproc_handle_rsc(struct rproc *rproc, u32 rsc_type,
				     void *rsc, int offset, int avail)
{
	int ret;

	switch (rsc_type) {
	case ISP_RSC_VENDOR(RSC_CARVEOUT):
		ret = hisi_isp_handle_carveout(rproc, rsc, offset, avail);
		break;
	case ISP_RSC_VENDOR(RSC_DEVMEM):
		ret = hisi_isp_handle_devmem(rproc, rsc, offset, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_VERSION):
		ret = hisi_isp_handle_version(rproc, rsc, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_RDR_MEMORY):
		ret = hisi_isp_handle_rdr(rproc, rsc, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_DYNAMIC_MEMORY):
		ret = hisi_isp_handle_dynamic(rproc, rsc, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_RESERVED_MEMORY):
		ret = hisi_isp_handle_reserved(rproc, rsc, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_CDA):
		ret = hisi_isp_handle_cda(rproc, rsc, avail);
		break;
	case ISP_RSC_VENDOR(ISP_RSC_TYPE_SHARED_PARA):
		ret = hisi_isp_handle_shared_para(rproc, rsc, avail);
		break;
	default:
		return RSC_IGNORED;
	}

	return ret ? ret : RSC_HANDLED;
}

/*
 * vring buffers are allocated by the remoteproc core (they are plain
 * carveouts created while parsing RSC_VDEV) and therefore still need to be
 * mapped into the ISP SMMU.  Other carveouts are allocated by
 * hisi_isp_carveout_alloc() and are already mapped.
 */
static int hisi_isp_map_vrings(struct rproc *rproc)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct rproc_mem_entry *mem;
	struct hisi_isp_mapping *map;
	int ret = 0;

	mutex_lock(&hisp->map_lock);
	list_for_each_entry(mem, &rproc->carveouts, node) {
		if (!mem->va || mem->da == FW_RSC_ADDR_ANY)
			continue;
		if (strncmp(mem->name, "vdev", strlen("vdev")))
			continue;

		map = hisi_isp_map_range(hisp, mem->da, mem->len, mem->dma,
					 mem->va, false);
		if (IS_ERR(map)) {
			dev_err(hisp->dev, "failed to map %s: %ld\n",
				mem->name, PTR_ERR(map));
			ret = PTR_ERR(map);
			break;
		}
	}
	mutex_unlock(&hisp->map_lock);

	return ret;
}

static int hisi_isp_rproc_load(struct rproc *rproc, const struct firmware *fw)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	struct hisi_isp_mapping *map;
	int ret;

	ret = hisi_isp_map_vrings(rproc);
	if (ret)
		return ret;

	ret = rproc_elf_load_segments(rproc, fw);
	if (ret)
		return ret;

	mutex_lock(&hisp->map_lock);
	list_for_each_entry(map, &hisp->mappings, node) {
		if (!map->va)
			continue;
		dcache_clean_inval_poc((unsigned long)map->va,
				       (unsigned long)map->va + map->len);
	}
	mutex_unlock(&hisp->map_lock);

	return 0;
}

static void hisi_isp_rproc_mbox_rx(struct mbox_client *client, void *data)
{
	struct hisi_isp_rproc *hisp =
		container_of(client, struct hisi_isp_rproc, mbox_client);
	u32 *msg = data;
	int ret;

	ret = rproc_vq_interrupt(hisp->rproc, msg[0]);
	if (ret)
		dev_dbg(hisp->dev, "unexpected vring %u\n", msg[0]);
}

static void hisi_isp_rproc_kick(struct rproc *rproc, int vqid)
{
	struct hisi_isp_rproc *hisp = rproc->priv;
	u32 msg = vqid;

	if (!hisp->tx_mbox)
		return;

	if (mbox_send_message(hisp->tx_mbox, &msg))
		dev_err_ratelimited(hisp->dev, "failed to kick vring %d\n",
				    vqid);
}

/*
 * Vendor resource types 4..9 fall into the range that mainline treats as
 * unsupported.  Move them (and the standard entries that need private
 * handling) into the vendor range so ->handle_rsc() is invoked, then put the
 * original values back before the firmware is started.
 */
static bool hisi_isp_needs_vendor_type(u32 type)
{
	switch (type) {
	case RSC_CARVEOUT:
	case RSC_DEVMEM:
	case ISP_RSC_TYPE_VERSION:
	case ISP_RSC_TYPE_RDR_MEMORY:
	case ISP_RSC_TYPE_DYNAMIC_MEMORY:
	case ISP_RSC_TYPE_RESERVED_MEMORY:
	case ISP_RSC_TYPE_CDA:
	case ISP_RSC_TYPE_SHARED_PARA:
		return true;
	default:
		return false;
	}
}

static int hisi_isp_rewrite_rsc_types(struct rproc *rproc, bool to_vendor)
{
	struct resource_table *table = rproc->table_ptr;
	struct fw_rsc_hdr *hdr;
	u32 off;
	int i;

	if (!table)
		return 0;

	for (i = 0; i < table->num; i++) {
		off = table->offset[i];
		if (off + sizeof(*hdr) > rproc->table_sz)
			return -EINVAL;
		hdr = (void *)table + off;

		if (to_vendor) {
			if (hisi_isp_needs_vendor_type(hdr->type))
				hdr->type = ISP_RSC_VENDOR(hdr->type);
		} else if (hdr->type >= ISP_RSC_VENDOR_BASE &&
			   hdr->type <= ISP_RSC_VENDOR(ISP_RSC_TYPE_SHARED_PARA)) {
			hdr->type = ISP_RSC_ORIGINAL(hdr->type);
		}
	}

	return 0;
}

static int hisi_isp_rproc_parse_fw(struct rproc *rproc,
				   const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret)
		return ret;

	return hisi_isp_rewrite_rsc_types(rproc, true);
}

static const struct rproc_ops hisi_isp_rproc_ops = {
	.start			= hisi_isp_rproc_start,
	.stop			= hisi_isp_rproc_stop,
	.da_to_va		= hisi_isp_rproc_da_to_va,
	.load			= hisi_isp_rproc_load,
	.parse_fw		= hisi_isp_rproc_parse_fw,
	.handle_rsc		= hisi_isp_rproc_handle_rsc,
	.kick			= hisi_isp_rproc_kick,
	.sanity_check		= rproc_elf_sanity_check,
	.find_loaded_rsc_table	= rproc_elf_find_loaded_rsc_table,
};

static void hisi_isp_mbox_exit(void *data)
{
	struct hisi_isp_rproc *hisp = data;

	mbox_free_channel(hisp->rx_mbox);
	mbox_free_channel(hisp->tx_mbox);
}

static int hisi_isp_mbox_init(struct platform_device *pdev,
			      struct hisi_isp_rproc *hisp)
{
	struct device *dev = &pdev->dev;
	struct mbox_chan *chan;

	if (!of_find_property(dev->of_node, "mboxes", NULL))
		return 0;

	hisp->mbox_client.dev = dev;
	hisp->mbox_client.rx_callback = hisi_isp_rproc_mbox_rx;
	hisp->mbox_client.tx_block = false;

	chan = mbox_request_channel_byname(&hisp->mbox_client, "tx");
	if (IS_ERR(chan))
		return dev_err_probe(dev, PTR_ERR(chan),
				     "failed to get TX mailbox\n");
	hisp->tx_mbox = chan;

	chan = mbox_request_channel_byname(&hisp->mbox_client, "rx");
	if (IS_ERR(chan)) {
		mbox_free_channel(hisp->tx_mbox);
		hisp->tx_mbox = NULL;
		return dev_err_probe(dev, PTR_ERR(chan),
				     "failed to get RX mailbox\n");
	}
	hisp->rx_mbox = chan;

	return devm_add_action_or_reset(dev, hisi_isp_mbox_exit, hisp);
}

static void hisi_isp_iommu_exit(void *data)
{
	isp_iommu_exit();
}

static int hisi_isp_iommu_init(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *iommu_np;
	u32 iova_start = 0xc0000000;
	u32 iova_size = 0x3ffffc00;
	int ret;

	iommu_np = of_get_child_by_name(np, "iommu-info");
	if (!iommu_np)
		iommu_np = of_get_child_by_name(np, "iommu_info");
	if (iommu_np) {
		of_property_read_u32(iommu_np, "start-addr", &iova_start);
		of_property_read_u32(iommu_np, "size", &iova_size);
		of_node_put(iommu_np);
	}

	ret = isp_iommu_init(pdev, iova_start, iova_size);
	if (ret)
		return ret;

	return devm_add_action_or_reset(&pdev->dev, hisi_isp_iommu_exit, NULL);
}

static int hisi_isp_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hisi_isp_rproc *hisp;
	struct rproc *rproc;
	const char *firmware = "isp_fw.elf";
	const char *name = "isp";
	int i, ret;

	if (of_property_read_string(np, "firmware-name", &firmware))
		of_property_read_string(np, "firmware-names", &firmware);
	if (of_property_read_string(np, "hisilicon,isp-name", &name))
		of_property_read_string(np, "isp-names", &name);

	rproc = devm_rproc_alloc(dev, name, &hisi_isp_rproc_ops,
				 firmware, sizeof(*hisp));
	if (!rproc)
		return -ENOMEM;

	hisp = rproc->priv;
	hisp->dev = dev;
	hisp->rproc = rproc;
	INIT_LIST_HEAD(&hisp->mappings);
	mutex_init(&hisp->map_lock);

	for (i = 0; i < ISP_NUM_REGS; i++) {
		hisp->res[i] = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!hisp->res[i])
			continue;

		/*
		 * PERI_CRG, PMC, PCTRL, SCTRL, NOC and CSSYS are shared with
		 * other drivers.  Do not request those regions; only the ISP
		 * core window is owned exclusively by this device.
		 */
		if (i == ISP_CRG_REG)
			hisp->regs[i] = devm_ioremap(dev,
						     hisp->res[i]->start,
						     resource_size(hisp->res[i]));
		else if (i == ISP_CORE_REG)
			hisp->regs[i] = devm_ioremap_resource(dev, hisp->res[i]);
		else
			continue;

		if (IS_ERR(hisp->regs[i]))
			return PTR_ERR(hisp->regs[i]);
	}

	if (!hisp->regs[ISP_CORE_REG]) {
		dev_err(dev, "missing ISP core register window\n");
		return -ENODEV;
	}

	if (of_property_read_u64(np, "hisilicon,isp-remap-addr",
				 &hisp->remap_addr) &&
	    of_property_read_u64(np, "isp-remap-addr", &hisp->remap_addr))
		hisp->remap_addr = 0;

	if (of_property_read_u64(np, "hisilicon,isp-rdr-addr",
				 &hisp->rdr_addr) &&
	    of_property_read_u64(np, "isp-rdr-addr", &hisp->rdr_addr))
		hisp->rdr_addr = 0;

	if (of_property_read_u64(np, "hisilicon,isp-mdc-addr",
				 &hisp->mdc_addr) &&
	    of_property_read_u64(np, "isp-mdc-addr", &hisp->mdc_addr))
		hisp->mdc_addr = 0;

	if (of_property_read_u32(np, "isp_local_timer", &hisp->local_timer) &&
	    of_property_read_u32(np, "hisilicon,isp-local-timer",
				 &hisp->local_timer))
		hisp->local_timer = 0;

	hisp->num_clks = devm_clk_bulk_get_all(dev, &hisp->clks);
	if (hisp->num_clks < 0)
		return hisp->num_clks;

	hisp->core_supply = devm_regulator_get_optional(dev, "core");
	if (IS_ERR(hisp->core_supply)) {
		ret = PTR_ERR(hisp->core_supply);
		if (ret != -ENODEV)
			return ret;
		hisp->core_supply = NULL;
	}

	hisp->subsys_supply = devm_regulator_get_optional(dev, "clockdep");
	if (IS_ERR(hisp->subsys_supply)) {
		ret = PTR_ERR(hisp->subsys_supply);
		if (ret != -ENODEV)
			return ret;
		hisp->subsys_supply = NULL;
	}

	ret = hisi_isp_iommu_init(pdev);
	if (ret) {
		dev_err(dev, "failed to initialise ISP SMMU: %d\n", ret);
		return ret;
	}

	ret = hisi_isp_mbox_init(pdev, hisp);
	if (ret)
		return ret;

	rproc->auto_boot = false;
	platform_set_drvdata(pdev, rproc);

	ret = devm_rproc_add(dev, rproc);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, hisi_isp_mappings_exit, hisp);
	if (ret)
		return ret;

	dev_info(dev, "ISP remoteproc registered (boot disabled)\n");
	return 0;
}

static const struct of_device_id hisi_isp_rproc_of_match[] = {
	{ .compatible = "hisilicon,isp" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_isp_rproc_of_match);

static struct platform_driver hisi_isp_rproc_driver = {
	.probe = hisi_isp_rproc_probe,
	.driver = {
		.name = "hisi-isp-rproc",
		.of_match_table = hisi_isp_rproc_of_match,
	},
};
module_platform_driver(hisi_isp_rproc_driver);

MODULE_DESCRIPTION("Hisilicon ISP remote processor");
MODULE_LICENSE("GPL");
