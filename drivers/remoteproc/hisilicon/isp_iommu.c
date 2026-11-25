// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kirin 970 ISP SMMU page-table management.
 *
 * The ISP block owns an ARM LPAE stage-1 SMMU that is not described as a
 * standard ARM SMMU in DT.  The vendor driver used the private hisi-iommu
 * API; keep the page tables local and expose a small map/unmap interface.
 */
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include <linux/hisi/isp_iommu.h>

#define ISP_IOMMU_PROT	(IOMMU_READ | IOMMU_WRITE)

#define ISP_SMMU_OFFSET		0x6000
#define ISP_SMMU_CLK_OFFSET	0x020010
#define ISP_SMMU_CLK_ENABLE	BIT(22)

#define ISP_SMMU_SCR		0x000
#define ISP_SMMU_INTMASK_NS	0x010
#define ISP_SMMU_SMR_NS		0x020
#define ISP_SMMU_RLD_EN0_NS	0x1f0
#define ISP_SMMU_RLD_EN1_NS	0x1f4
#define ISP_SMMU_RLD_EN2_NS	0x1f8
#define ISP_SMMU_CB_TTBR0	0x204
#define ISP_SMMU_CB_TTBCR	0x20c
#define ISP_SMMU_SCACHEI_ALL	0x214
#define ISP_SMMU_SMR_S		0x500
#define ISP_SMMU_RLD_EN0_S	0x6f0
#define ISP_SMMU_RLD_EN1_S	0x6f4
#define ISP_SMMU_RLD_EN2_S	0x6f8
#define ISP_SMMU_INTMASK_S	0x700
#define ISP_SMMU_SCR_S		0x710
#define ISP_SMMU_SCB_TTBR	0x718
#define ISP_SMMU_SCB_TTBCR	0x71c
#define ISP_SMMU_NUM_SMR	80

#define ISP_SMMU_SCR_DEFAULT	(BIT(16) | BIT(5) | BIT(4) | BIT(3))
#define ISP_SMMU_INTMASK_NS_DEFAULT	0x3f
#define ISP_SMMU_INTMASK_S_DEFAULT	0x30
#define ISP_SMMU_SMR_NS_DEFAULT	0
#define ISP_SMMU_SMR_S_DEFAULT	BIT(1)
#define ISP_SMMU_SCR_S_DEFAULT	BIT(1)

struct isp_iommu {
	struct io_pgtable_ops *ops;
	struct device *dev;
	/* protects page-table updates */
	struct mutex lock;
	dma_addr_t iova_start;
	size_t iova_size;
	u64 pgd_base;
	bool ready;
};

static struct isp_iommu isp_iommu;

static void *isp_pgtable_alloc(void *cookie, size_t size, gfp_t gfp)
{
	size = max_t(size_t, size, 64);
	return (void *)__get_free_pages(gfp | __GFP_ZERO | __GFP_DMA32,
					get_order(size));
}

static void isp_pgtable_free(void *cookie, void *pages, size_t size)
{
	size = max_t(size_t, size, 64);
	free_pages((unsigned long)pages, get_order(size));
}

int isp_iommu_init(struct platform_device *pdev, dma_addr_t iova_start,
		   size_t iova_size)
{
	struct io_pgtable_cfg cfg = { 0 };
	struct device *dev;
	int ret;

	if (!pdev || !iova_size)
		return -EINVAL;

	dev = &pdev->dev;

	if (isp_iommu.ready)
		return 0;

	mutex_init(&isp_iommu.lock);
	isp_iommu.dev = dev;
	isp_iommu.iova_start = iova_start;
	isp_iommu.iova_size = iova_size;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (ret)
		return ret;

	cfg.pgsize_bitmap = SZ_4K;
	cfg.ias = 32;
	cfg.oas = 40;
	cfg.coherent_walk = false;
	cfg.iommu_dev = dev;
	cfg.quirks = IO_PGTABLE_QUIRK_ARM_NS;
	cfg.alloc = isp_pgtable_alloc;
	cfg.free = isp_pgtable_free;

	isp_iommu.ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1, &cfg, NULL);
	if (!isp_iommu.ops)
		return -ENOMEM;

	isp_iommu.pgd_base = cfg.arm_lpae_s1_cfg.ttbr;
	isp_iommu.ready = true;

	dev_info(dev, "ISP SMMU ready, pgd %pad iova %pad+%#zx\n",
		 &isp_iommu.pgd_base, &isp_iommu.iova_start,
		 isp_iommu.iova_size);
	return 0;
}

void isp_iommu_exit(void)
{
	if (!isp_iommu.ready)
		return;

	free_io_pgtable_ops(isp_iommu.ops);
	isp_iommu.ops = NULL;
	isp_iommu.ready = false;
	isp_iommu.pgd_base = 0;
}

bool isp_iommu_ready(void)
{
	return isp_iommu.ready;
}

u64 isp_iommu_pgd_base(void)
{
	return isp_iommu.ready ? isp_iommu.pgd_base : 0;
}

void isp_iommu_program(void __iomem *isp_base)
{
	void __iomem *base;
	int i;

	if (!isp_iommu.ready || !isp_base)
		return;

	/*
	 * Enable the SMMU module clock first (vendor isp.c:
	 * ISP_020010_MODULE_CGR_TOP | SMMU_CLK_ENABLE).  The register sits at
	 * offset 0x20010 inside the 0x200000 ISP core window.
	 */
	writel(readl(isp_base + ISP_SMMU_CLK_OFFSET) | ISP_SMMU_CLK_ENABLE,
	       isp_base + ISP_SMMU_CLK_OFFSET);

	base = isp_base + ISP_SMMU_OFFSET;
	writel(ISP_SMMU_SCR_DEFAULT, base + ISP_SMMU_SCR);
	writel(ISP_SMMU_INTMASK_NS_DEFAULT, base + ISP_SMMU_INTMASK_NS);
	writel(isp_iommu.pgd_base, base + ISP_SMMU_CB_TTBR0);
	writel(0, base + ISP_SMMU_RLD_EN0_NS);
	writel(0, base + ISP_SMMU_RLD_EN1_NS);
	writel(0, base + ISP_SMMU_RLD_EN2_NS);

	for (i = 0; i < ISP_SMMU_NUM_SMR; i++)
		writel(ISP_SMMU_SMR_NS_DEFAULT, base + ISP_SMMU_SMR_NS + i * 4);

	writel(ISP_SMMU_SCR_S_DEFAULT, base + ISP_SMMU_SCR_S);
	writel(0, base + ISP_SMMU_RLD_EN0_S);
	writel(0, base + ISP_SMMU_RLD_EN1_S);
	writel(0, base + ISP_SMMU_RLD_EN2_S);
	writel(isp_iommu.pgd_base, base + ISP_SMMU_SCB_TTBR);

	for (i = 0; i < ISP_SMMU_NUM_SMR; i++)
		writel(ISP_SMMU_SMR_S_DEFAULT, base + ISP_SMMU_SMR_S + i * 4);

	writel(1, base + ISP_SMMU_SCB_TTBCR);
	writel(1, base + ISP_SMMU_CB_TTBCR);
	writel(isp_iommu.pgd_base, base + ISP_SMMU_CB_TTBR0);
	writel(isp_iommu.pgd_base, base + ISP_SMMU_SCB_TTBR);

	writel(1, base + ISP_SMMU_SCACHEI_ALL);
}

static bool isp_iommu_range_valid(dma_addr_t iova, size_t size)
{
	return isp_iommu.ready && size &&
	       iova >= isp_iommu.iova_start &&
	       iova + size <= isp_iommu.iova_start + isp_iommu.iova_size;
}

int isp_iommu_map(dma_addr_t iova, phys_addr_t paddr, size_t size, int prot)
{
	size_t done = 0;
	int ret;

	if (!isp_iommu_range_valid(iova, size))
		return -EINVAL;
	if (!IS_ALIGNED(iova, PAGE_SIZE) || !IS_ALIGNED(paddr, PAGE_SIZE) ||
	    !IS_ALIGNED(size, PAGE_SIZE))
		return -EINVAL;

	mutex_lock(&isp_iommu.lock);
	ret = isp_iommu.ops->map_pages(isp_iommu.ops, iova, paddr, PAGE_SIZE,
				       size >> PAGE_SHIFT,
				       prot & ISP_IOMMU_PROT, GFP_KERNEL,
				       &done);
	mutex_unlock(&isp_iommu.lock);

	if (ret)
		return ret;
	return done == size ? 0 : -ENOMEM;
}

size_t isp_iommu_map_sg(dma_addr_t iova, struct scatterlist *sgl, int nents,
			int prot)
{
	struct scatterlist *sg;
	size_t mapped = 0;
	int i, ret = 0;

	if (!isp_iommu.ready || !sgl || nents <= 0)
		return 0;

	mutex_lock(&isp_iommu.lock);
	for_each_sg(sgl, sg, nents, i) {
		size_t len = PAGE_ALIGN(sg->length);
		size_t done = 0;

		if (!sg->length)
			continue;
		if (!isp_iommu_range_valid(iova + mapped, len)) {
			ret = -EINVAL;
			break;
		}

		ret = isp_iommu.ops->map_pages(isp_iommu.ops, iova + mapped,
					       sg_phys(sg), PAGE_SIZE,
					       len >> PAGE_SHIFT,
					       prot & ISP_IOMMU_PROT,
					       GFP_KERNEL, &done);
		if (ret || done != len) {
			ret = ret ?: -ENOMEM;
			break;
		}
		mapped += len;
	}
	mutex_unlock(&isp_iommu.lock);

	if (ret) {
		isp_iommu_unmap(iova, mapped);
		return 0;
	}
	return mapped;
}

size_t isp_iommu_unmap(dma_addr_t iova, size_t size)
{
	struct iommu_iotlb_gather gather;
	size_t unmapped;

	if (!isp_iommu.ready || !size)
		return 0;

	iommu_iotlb_gather_init(&gather);
	mutex_lock(&isp_iommu.lock);
	unmapped = isp_iommu.ops->unmap_pages(isp_iommu.ops, iova, PAGE_SIZE,
					      size >> PAGE_SHIFT, &gather);
	mutex_unlock(&isp_iommu.lock);

	return unmapped;
}

phys_addr_t isp_iommu_iova_to_phys(dma_addr_t iova)
{
	phys_addr_t paddr;

	if (!isp_iommu.ready)
		return 0;

	mutex_lock(&isp_iommu.lock);
	paddr = isp_iommu.ops->iova_to_phys(isp_iommu.ops, iova);
	mutex_unlock(&isp_iommu.lock);

	return paddr;
}
