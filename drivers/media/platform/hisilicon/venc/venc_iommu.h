/* Copyright (c) 2018-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#ifndef __HISI_VENC_IOMMU_H__
#define __HISI_VENC_IOMMU_H__

#include <linux/dma-mapping.h>
#include <linux/types.h>

struct platform_device;

int venc_iommu_init(struct platform_device *pdev);
void venc_iommu_exit(void);
u64 venc_iommu_ttbr(void);
int venc_iommu_map_fd(int fd, dma_addr_t *iova, size_t *size);
int venc_iommu_unmap_iova(dma_addr_t iova);

#endif /* __HISI_VENC_IOMMU_H__ */
