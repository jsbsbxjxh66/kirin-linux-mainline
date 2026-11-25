/* Copyright (c) 2018-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#ifndef HISI_JPU_IOMMU_H
#define HISI_JPU_IOMMU_H

#include <linux/dma-buf.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

struct jpu_iommu_map {
	struct dma_buf *buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct iova *iova;
	dma_addr_t iova_start;
	size_t size;
	bool mapped;
};

struct jpu_iommu {
	struct io_pgtable_ops *ops;
	struct iova_domain iovad;
	u64 ttbr;
	struct iova *lb_iova;
	void *lb_cpu;
	phys_addr_t lb_phys;
	size_t lb_size;
	bool ready;
	bool iova_cache;
};

#endif /* HISI_JPU_IOMMU_H */
