/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __HISI_ISP_IOMMU_H__
#define __HISI_ISP_IOMMU_H__

#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct platform_device;

int isp_iommu_init(struct platform_device *pdev, dma_addr_t iova_start,
		   size_t iova_size);
void isp_iommu_exit(void);
bool isp_iommu_ready(void);
u64 isp_iommu_pgd_base(void);
void isp_iommu_program(void __iomem *isp_base);
int isp_iommu_map(dma_addr_t iova, phys_addr_t paddr, size_t size, int prot);
size_t isp_iommu_map_sg(dma_addr_t iova, struct scatterlist *sgl, int nents,
			int prot);
size_t isp_iommu_unmap(dma_addr_t iova, size_t size);
phys_addr_t isp_iommu_iova_to_phys(dma_addr_t iova);

#endif /* __HISI_ISP_IOMMU_H__ */
