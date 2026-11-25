// SPDX-License-Identifier: GPL-2.0
/*
 * Hisilicon LPAE SMMU page-table manager.
 *
 * The SMMU hardware on Kirin 970 is configured by the secure firmware and by
 * the consumer drivers (ISP, IVP, JPEG, ...), which program TTBR0 themselves.
 * The AP only owns the three-level, 4 KiB page tables.  This driver therefore
 * implements the IOMMU-domain/page-table side only; there is no MMIO register
 * programming and no fault interrupt.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/cacheflush.h>

#include <linux/hisi-iommu.h>

#define HISI_SMMU_PGD_SHIFT		30
#define HISI_SMMU_PMD_SHIFT		21
#define HISI_SMMU_PTE_SHIFT		12

#define HISI_SMMU_PTRS_PER_PGD		4
#define HISI_SMMU_PTRS_PER_PMD		512
#define HISI_SMMU_PTRS_PER_PTE		512

#define HISI_SMMU_PAGE_SIZE		SZ_4K
#define HISI_SMMU_PAGE_MASK		(~(HISI_SMMU_PAGE_SIZE - 1))
#define HISI_SMMU_TABLE_ADDR_MASK	GENMASK_ULL(51, HISI_SMMU_PTE_SHIFT)

#define HISI_SMMU_DESC_TYPE		GENMASK_ULL(1, 0)
#define HISI_SMMU_DESC_NS		BIT_ULL(63)

#define HISI_SMMU_PTE_NS		BIT_ULL(5)
#define HISI_SMMU_PTE_AP_USER		BIT_ULL(6)	/* AP[1]: unprivileged */
#define HISI_SMMU_PTE_AP_RDONLY		BIT_ULL(7)	/* AP[2] */
#define HISI_SMMU_PTE_SHARED		GENMASK_ULL(9, 8)
#define HISI_SMMU_PTE_AF		BIT_ULL(10)
#define HISI_SMMU_PTE_NG		BIT_ULL(11)
#define HISI_SMMU_PTE_PXN		BIT_ULL(53)
#define HISI_SMMU_PTE_UXN		BIT_ULL(54)
#define HISI_SMMU_PTE_ATTRINDX(t)	((u64)(t) << 2)

#define HISI_MT_NORMAL			0
#define HISI_MT_NORMAL_CACHE		4
#define HISI_MT_NORMAL_NC		5
#define HISI_MT_DEVICE_NGNRE		6

#define HISI_SMMU_BASE			(HISI_SMMU_DESC_TYPE | HISI_SMMU_PTE_AF | \
					 HISI_SMMU_PTE_SHARED)
#define HISI_SMMU_PROT_DEVICE		(HISI_SMMU_BASE | HISI_SMMU_PTE_PXN | \
					 HISI_SMMU_PTE_UXN | \
					 HISI_SMMU_PTE_ATTRINDX(HISI_MT_DEVICE_NGNRE))
#define HISI_SMMU_PROT_NORMAL		(HISI_SMMU_BASE | HISI_SMMU_PTE_PXN | \
					 HISI_SMMU_PTE_UXN | \
					 HISI_SMMU_PTE_ATTRINDX(HISI_MT_NORMAL))
#define HISI_SMMU_PROT_NORMAL_CACHE	(HISI_SMMU_BASE | HISI_SMMU_PTE_PXN | \
					 HISI_SMMU_PTE_UXN | \
					 HISI_SMMU_PTE_ATTRINDX(HISI_MT_NORMAL_CACHE))
#define HISI_SMMU_PROT_NORMAL_NC	(HISI_SMMU_BASE | HISI_SMMU_PTE_PXN | \
					 HISI_SMMU_PTE_UXN | \
					 HISI_SMMU_PTE_ATTRINDX(HISI_MT_NORMAL_NC))
/*
 * Stage-1 translations are performed as unprivileged accesses, so AP[1]
 * must be set (matches the vendor's SMMU_PAGE_READWRITE/READONLY).
 */
#define HISI_SMMU_PROT_READWRITE	(HISI_SMMU_BASE | HISI_SMMU_PTE_NG | \
					 HISI_SMMU_PTE_AP_USER)
#define HISI_SMMU_PROT_READONLY		(HISI_SMMU_BASE | HISI_SMMU_PTE_NG | \
					 HISI_SMMU_PTE_AP_USER | \
					 HISI_SMMU_PTE_AP_RDONLY)

struct hisi_smmu_domain {
	struct iommu_domain domain;
	spinlock_t lock;		/* protects page-table updates */
	struct page *pgd_page;
	u64 *pgd;
	phys_addr_t pgd_phys;
	struct hisi_iommu_domain_data data;
};

static struct hisi_smmu_device {
	struct iommu_device iommu;
} *hisi_smmu;

static const struct iommu_ops hisi_smmu_ops;

static inline struct hisi_smmu_domain *to_hisi_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct hisi_smmu_domain, domain);
}

static inline bool hisi_smmu_desc_valid(u64 desc)
{
	return desc & HISI_SMMU_DESC_TYPE;
}

static inline phys_addr_t hisi_smmu_desc_addr(u64 desc)
{
	return desc & HISI_SMMU_TABLE_ADDR_MASK;
}

static inline u64 hisi_smmu_table_desc(struct page *page)
{
	return page_to_phys(page) | HISI_SMMU_DESC_TYPE | HISI_SMMU_DESC_NS;
}

static void hisi_smmu_flush(void *addr, size_t size)
{
	dcache_clean_poc((unsigned long)addr, (unsigned long)addr + size);
}

int of_get_iova_info(struct device_node *np, unsigned long *iova_start,
		     unsigned long *iova_size, unsigned long *iova_align)
{
	struct device_node *node;
	u32 start, size;
	u64 align = SZ_256K;
	int ret;

	if (!np)
		return -ENODEV;

	node = of_get_child_by_name(np, "iommu_info");
	if (!node)
		return -ENODEV;

	ret = of_property_read_u32(node, "start-addr", &start);
	if (ret)
		goto out_put;

	ret = of_property_read_u32(node, "size", &size);
	if (ret)
		goto out_put;

	of_property_read_u64(node, "iova-align", &align);

	*iova_start = start;
	*iova_size = size;
	*iova_align = align;

out_put:
	of_node_put(node);
	return ret;
}
EXPORT_SYMBOL_GPL(of_get_iova_info);

phys_addr_t hisi_domain_get_ttbr(struct device *dev)
{
	struct iommu_domain *domain;
	struct hisi_smmu_domain *hisi_domain;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain || domain->owner != &hisi_smmu_ops)
		return 0;

	hisi_domain = to_hisi_domain(domain);
	return hisi_domain->data.phy_pgd_base;
}
EXPORT_SYMBOL_GPL(hisi_domain_get_ttbr);

static u64 hisi_smmu_make_pte(int prot)
{
	u64 pte = HISI_SMMU_DESC_TYPE;

	if (prot & IOMMU_MMIO) {
		pte |= HISI_SMMU_PROT_DEVICE;
	} else {
		if (prot & IOMMU_CACHE)
			pte |= HISI_SMMU_PROT_NORMAL_CACHE;
		else
			pte |= HISI_SMMU_PROT_NORMAL_NC;

		if ((prot & IOMMU_READ) && (prot & IOMMU_WRITE))
			pte |= HISI_SMMU_PROT_READWRITE;
		else if (prot & IOMMU_READ)
			pte |= HISI_SMMU_PROT_READONLY;

		if (!(prot & IOMMU_NOEXEC))
			pte &= ~(HISI_SMMU_PTE_PXN | HISI_SMMU_PTE_UXN);
	}

	return pte;
}

static struct page *hisi_smmu_alloc_table(void)
{
	struct page *page;

	page = alloc_page(GFP_KERNEL | __GFP_ZERO | __GFP_DMA);
	if (!page)
		return NULL;

	hisi_smmu_flush(page_address(page), HISI_SMMU_PAGE_SIZE);
	return page;
}

static u64 *hisi_smmu_get_pmd(struct hisi_smmu_domain *hisi_domain,
			      unsigned int pgd_idx)
{
	struct page *page = NULL;
	u64 *pmd = NULL;
	u64 *pgd = &hisi_domain->pgd[pgd_idx];
	unsigned long flags;

	spin_lock_irqsave(&hisi_domain->lock, flags);
	if (!hisi_smmu_desc_valid(*pgd)) {
		spin_unlock_irqrestore(&hisi_domain->lock, flags);
		page = hisi_smmu_alloc_table();
		if (!page)
			return NULL;
		spin_lock_irqsave(&hisi_domain->lock, flags);
		if (!hisi_smmu_desc_valid(*pgd)) {
			*pgd = hisi_smmu_table_desc(page);
			hisi_smmu_flush(pgd, sizeof(*pgd));
			page = NULL;
		}
	}
	pmd = phys_to_virt(hisi_smmu_desc_addr(*pgd));
	spin_unlock_irqrestore(&hisi_domain->lock, flags);

	if (page)
		__free_page(page);
	return pmd;
}

static u64 *hisi_smmu_get_pte(struct hisi_smmu_domain *hisi_domain, u64 *pmd,
			      unsigned int pte_idx)
{
	struct page *page = NULL;
	u64 *pte = NULL;
	unsigned long flags;

	spin_lock_irqsave(&hisi_domain->lock, flags);
	if (!hisi_smmu_desc_valid(*pmd)) {
		spin_unlock_irqrestore(&hisi_domain->lock, flags);
		page = hisi_smmu_alloc_table();
		if (!page)
			return NULL;
		spin_lock_irqsave(&hisi_domain->lock, flags);
		if (!hisi_smmu_desc_valid(*pmd)) {
			*pmd = hisi_smmu_table_desc(page);
			hisi_smmu_flush(pmd, sizeof(*pmd));
			page = NULL;
		}
	}
	pte = phys_to_virt(hisi_smmu_desc_addr(*pmd)) + pte_idx;
	spin_unlock_irqrestore(&hisi_domain->lock, flags);

	if (page)
		__free_page(page);
	return pte;
}

static int hisi_smmu_map_pages(struct iommu_domain *domain, unsigned long iova,
			       phys_addr_t paddr, size_t pgsize, size_t pgcount,
			       int prot, gfp_t gfp, size_t *mapped)
{
	struct hisi_smmu_domain *hisi_domain = to_hisi_domain(domain);
	unsigned long flags;
	size_t i;
	u64 pteval;
	int ret;

	(void)gfp;

	if (pgsize != HISI_SMMU_PAGE_SIZE)
		return -EINVAL;

	if (iova & ~HISI_SMMU_PAGE_MASK || paddr & ~HISI_SMMU_PAGE_MASK)
		return -EINVAL;

	pteval = hisi_smmu_make_pte(prot);
	for (i = 0; i < pgcount; i++) {
		unsigned long addr = iova + i * HISI_SMMU_PAGE_SIZE;
		unsigned int pgd_idx = (addr >> HISI_SMMU_PGD_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PGD - 1);
		unsigned int pmd_idx = (addr >> HISI_SMMU_PMD_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PMD - 1);
		unsigned int pte_idx = (addr >> HISI_SMMU_PTE_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PTE - 1);
		phys_addr_t page_pa = paddr + i * HISI_SMMU_PAGE_SIZE;
		u64 *pmd, *pte;

		pmd = hisi_smmu_get_pmd(hisi_domain, pgd_idx);
		if (!pmd) {
			ret = -ENOMEM;
			goto out_partial;
		}

		pte = hisi_smmu_get_pte(hisi_domain, &pmd[pmd_idx], pte_idx);
		if (!pte) {
			ret = -ENOMEM;
			goto out_partial;
		}

		spin_lock_irqsave(&hisi_domain->lock, flags);
		if (hisi_smmu_desc_valid(*pte)) {
			spin_unlock_irqrestore(&hisi_domain->lock, flags);
			ret = -EEXIST;
			goto out_partial;
		}
		*pte = page_pa | pteval;
		hisi_smmu_flush(pte, sizeof(*pte));
		spin_unlock_irqrestore(&hisi_domain->lock, flags);
	}

	*mapped = pgsize * pgcount;
	return 0;

out_partial:
	/*
	 * The iommu core unmaps *mapped bytes on error; report what was
	 * actually installed so the partially-mapped range gets rolled back.
	 */
	*mapped = i * HISI_SMMU_PAGE_SIZE;
	return ret;
}

static size_t hisi_smmu_unmap_pages(struct iommu_domain *domain,
				    unsigned long iova, size_t pgsize,
				    size_t pgcount,
				    struct iommu_iotlb_gather *gather)
{
	struct hisi_smmu_domain *hisi_domain = to_hisi_domain(domain);
	unsigned long flags;
	size_t i;

	if (pgsize != HISI_SMMU_PAGE_SIZE)
		return 0;

	for (i = 0; i < pgcount; i++) {
		unsigned long addr = iova + i * HISI_SMMU_PAGE_SIZE;
		unsigned int pgd_idx = (addr >> HISI_SMMU_PGD_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PGD - 1);
		unsigned int pmd_idx = (addr >> HISI_SMMU_PMD_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PMD - 1);
		unsigned int pte_idx = (addr >> HISI_SMMU_PTE_SHIFT) &
				       (HISI_SMMU_PTRS_PER_PTE - 1);
		u64 *pmd, *pte;

		if (!hisi_smmu_desc_valid(hisi_domain->pgd[pgd_idx]))
			continue;

		pmd = phys_to_virt(hisi_smmu_desc_addr(hisi_domain->pgd[pgd_idx]));
		if (!hisi_smmu_desc_valid(pmd[pmd_idx]))
			continue;

		pte = phys_to_virt(hisi_smmu_desc_addr(pmd[pmd_idx])) +
		      pte_idx;
		spin_lock_irqsave(&hisi_domain->lock, flags);
		if (hisi_smmu_desc_valid(*pte)) {
			*pte = 0;
			hisi_smmu_flush(pte, sizeof(*pte));
		}
		spin_unlock_irqrestore(&hisi_domain->lock, flags);
	}

	return pgsize * pgcount;
}

static phys_addr_t hisi_smmu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct hisi_smmu_domain *hisi_domain = to_hisi_domain(domain);
	unsigned int pgd_idx = (iova >> HISI_SMMU_PGD_SHIFT) &
			       (HISI_SMMU_PTRS_PER_PGD - 1);
	unsigned int pmd_idx = (iova >> HISI_SMMU_PMD_SHIFT) &
			       (HISI_SMMU_PTRS_PER_PMD - 1);
	unsigned int pte_idx = (iova >> HISI_SMMU_PTE_SHIFT) &
			       (HISI_SMMU_PTRS_PER_PTE - 1);
	u64 *pmd, *pte;

	if (!hisi_smmu_desc_valid(hisi_domain->pgd[pgd_idx]))
		return 0;

	pmd = phys_to_virt(hisi_smmu_desc_addr(hisi_domain->pgd[pgd_idx]));
	if (!hisi_smmu_desc_valid(pmd[pmd_idx]))
		return 0;

	pte = phys_to_virt(hisi_smmu_desc_addr(pmd[pmd_idx]));
	if (!hisi_smmu_desc_valid(pte[pte_idx]))
		return 0;

	return (pte[pte_idx] & HISI_SMMU_TABLE_ADDR_MASK) |
	       (iova & ~HISI_SMMU_PAGE_MASK);
}

static void hisi_smmu_free_pgtables(struct hisi_smmu_domain *hisi_domain)
{
	unsigned int i, j;

	for (i = 0; i < HISI_SMMU_PTRS_PER_PGD; i++) {
		phys_addr_t pgd_phys;
		u64 *pmd;

		if (!hisi_smmu_desc_valid(hisi_domain->pgd[i]))
			continue;

		pmd = phys_to_virt(hisi_smmu_desc_addr(hisi_domain->pgd[i]));
		for (j = 0; j < HISI_SMMU_PTRS_PER_PMD; j++) {
			if (!hisi_smmu_desc_valid(pmd[j]))
				continue;
			__free_page(phys_to_page(hisi_smmu_desc_addr(pmd[j])));
			pmd[j] = 0;
		}
		pgd_phys = hisi_smmu_desc_addr(hisi_domain->pgd[i]);
		__free_page(phys_to_page(pgd_phys));
		hisi_domain->pgd[i] = 0;
	}
}

static void hisi_smmu_domain_free(struct iommu_domain *domain)
{
	struct hisi_smmu_domain *hisi_domain = to_hisi_domain(domain);

	hisi_smmu_free_pgtables(hisi_domain);
	__free_page(hisi_domain->pgd_page);
	kfree(hisi_domain);
}

static int hisi_smmu_attach_dev(struct iommu_domain *domain,
				struct device *dev,
				struct iommu_domain *old)
{
	struct hisi_smmu_domain *hisi_domain = to_hisi_domain(domain);
	int ret;

	(void)old;
	ret = of_get_iova_info(dev->of_node, &hisi_domain->data.iova_start,
			       &hisi_domain->data.iova_size,
			       &hisi_domain->data.iova_align);
	if (ret)
		return ret;

	hisi_domain->data.phy_pgd_base = hisi_domain->pgd_phys;
	return 0;
}

static struct iommu_domain *
hisi_smmu_domain_alloc_paging(struct device *dev)
{
	struct hisi_smmu_domain *hisi_domain;

	(void)dev;
	hisi_domain = kzalloc_obj(*hisi_domain);
	if (!hisi_domain)
		return NULL;

	hisi_domain->pgd_page = hisi_smmu_alloc_table();
	if (!hisi_domain->pgd_page) {
		kfree(hisi_domain);
		return NULL;
	}

	hisi_domain->pgd = page_address(hisi_domain->pgd_page);
	hisi_domain->pgd_phys = page_to_phys(hisi_domain->pgd_page);
	spin_lock_init(&hisi_domain->lock);

	hisi_domain->domain.pgsize_bitmap = HISI_SMMU_PAGE_SIZE;
	hisi_domain->domain.geometry.aperture_start = 0;
	hisi_domain->domain.geometry.aperture_end = (1ULL << 32) - 1;
	hisi_domain->domain.geometry.force_aperture = true;

	return &hisi_domain->domain;
}

static struct iommu_device *hisi_smmu_probe_device(struct device *dev)
{
	if (!hisi_smmu)
		return ERR_PTR(-ENODEV);

	return &hisi_smmu->iommu;
}

static void hisi_smmu_release_device(struct device *dev)
{
	(void)dev;
}

static int hisi_smmu_of_xlate(struct device *dev,
			      const struct of_phandle_args *args)
{
	if (args->args_count)
		return -EINVAL;

	return 0;
}

static const struct iommu_ops hisi_smmu_ops = {
	.domain_alloc_paging = hisi_smmu_domain_alloc_paging,
	.probe_device = hisi_smmu_probe_device,
	.release_device = hisi_smmu_release_device,
	.device_group = generic_device_group,
	.of_xlate = hisi_smmu_of_xlate,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev = hisi_smmu_attach_dev,
		.map_pages = hisi_smmu_map_pages,
		.unmap_pages = hisi_smmu_unmap_pages,
		.iova_to_phys = hisi_smmu_iova_to_phys,
		.free = hisi_smmu_domain_free,
	},
};

static int hisi_smmu_probe(struct platform_device *pdev)
{
	int ret;

	if (hisi_smmu)
		return -EBUSY;

	hisi_smmu = devm_kzalloc(&pdev->dev, sizeof(*hisi_smmu), GFP_KERNEL);
	if (!hisi_smmu)
		return -ENOMEM;

	ret = iommu_device_sysfs_add(&hisi_smmu->iommu, &pdev->dev, NULL,
				     "hisi-smmu-lpae");
	if (ret)
		goto err_free;

	ret = iommu_device_register(&hisi_smmu->iommu, &hisi_smmu_ops,
				    &pdev->dev);
	if (ret)
		goto err_sysfs;

	return 0;

err_sysfs:
	iommu_device_sysfs_remove(&hisi_smmu->iommu);
err_free:
	hisi_smmu = NULL;
	return ret;
}

static void hisi_smmu_remove(struct platform_device *pdev)
{
	iommu_device_unregister(&hisi_smmu->iommu);
	iommu_device_sysfs_remove(&hisi_smmu->iommu);
	hisi_smmu = NULL;
}

static const struct of_device_id hisi_smmu_of_match[] = {
	{ .compatible = "hisi,hisi-smmu-lpae" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_smmu_of_match);

static struct platform_driver hisi_smmu_driver = {
	.probe = hisi_smmu_probe,
	.remove = hisi_smmu_remove,
	.driver = {
		.name = "hisi-smmu-lpae",
		.of_match_table = hisi_smmu_of_match,
	},
};
module_platform_driver(hisi_smmu_driver);

MODULE_DESCRIPTION("Hisilicon LPAE SMMU page-table manager");
MODULE_LICENSE("GPL");
