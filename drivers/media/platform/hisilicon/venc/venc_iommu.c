/* Copyright (c) 2018-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * The VENC block contains its own SMMU (the vendor driver calls it
 * "VEDU SMMU").  It is not an ARM MMU-500, but it walks ARM LPAE stage-1
 * page tables, so the generic io-pgtable code can be reused.  Keep the
 * page table and IOVA allocation here instead of the old hisi-iommu/ION
 * private interface.
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include "venc_iommu.h"

#define VENC_IOVA_START	0x40000UL
#define VENC_IOVA_LIMIT	0xe0004000UL
#define VENC_IOVA_PROT	(IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE)

struct venc_iommu_map {
	struct list_head node;
	struct dma_buf *buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct iova *iova;
	dma_addr_t iova_start;
	size_t size;
	unsigned int refs;
};

struct venc_iommu {
	struct io_pgtable_ops *ops;
	struct device *dev;
	struct iova_domain iovad;
	struct list_head maps;
	struct mutex lock;
	u64 ttbr;
	bool ready;
	bool iova_cache;
};

static struct venc_iommu venc_iommu;

static void *venc_pgtable_alloc(void *cookie, size_t size, gfp_t gfp)
{
	size = max_t(size_t, size, 64);

	return (void *)__get_free_pages(gfp | __GFP_ZERO | __GFP_DMA32,
					get_order(size));
}

static void venc_pgtable_free(void *cookie, void *pages, size_t size)
{
	size = max_t(size_t, size, 64);
	free_pages((unsigned long)pages, get_order(size));
}

static int venc_iommu_map_sgt(struct sg_table *sgt, dma_addr_t iova,
			      size_t size)
{
	struct scatterlist *sg;
	size_t mapped = 0;
	int i, ret;

	for_each_sgtable_sg(sgt, sg, i) {
		size_t len = PAGE_ALIGN(sg->length);
		size_t done = 0;
		phys_addr_t phys = sg_phys(sg);

		if (!sg->length)
			continue;

		if (!IS_ALIGNED(phys, PAGE_SIZE) || mapped + len > size) {
			ret = -EINVAL;
			goto err;
		}

		ret = venc_iommu.ops->map_pages(venc_iommu.ops, iova + mapped,
						phys, PAGE_SIZE,
						len >> PAGE_SHIFT,
						VENC_IOVA_PROT, GFP_KERNEL,
						&done);
		if (ret || done != len) {
			if (!ret)
				ret = -ENOMEM;
			goto err;
		}

		mapped += len;
	}

	if (mapped < size) {
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	if (mapped) {
		struct iommu_iotlb_gather gather;

		iommu_iotlb_gather_init(&gather);
		venc_iommu.ops->unmap_pages(venc_iommu.ops, iova, PAGE_SIZE,
					    mapped >> PAGE_SHIFT, &gather);
	}
	return ret;
}

static void venc_iommu_release_map(struct venc_iommu_map *map)
{
	struct iommu_iotlb_gather gather;

	iommu_iotlb_gather_init(&gather);
	venc_iommu.ops->unmap_pages(venc_iommu.ops, map->iova_start,
				    PAGE_SIZE, map->size >> PAGE_SHIFT,
				    &gather);

	__free_iova(&venc_iommu.iovad, map->iova);

	if (map->sgt)
		dma_buf_unmap_attachment(map->attach, map->sgt,
					 DMA_BIDIRECTIONAL);
	if (map->attach)
		dma_buf_detach(map->buf, map->attach);
	if (map->buf)
		dma_buf_put(map->buf);

	list_del(&map->node);
	kfree(map);
}

int venc_iommu_init(struct platform_device *pdev)
{
	struct io_pgtable_cfg cfg = { 0 };
	struct device *dev;
	int ret;

	if (!pdev)
		return -EINVAL;

	dev = &pdev->dev;
	venc_iommu.dev = dev;

	mutex_init(&venc_iommu.lock);
	INIT_LIST_HEAD(&venc_iommu.maps);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (ret)
		return ret;

	ret = iova_cache_get();
	if (ret)
		return ret;
	venc_iommu.iova_cache = true;

	init_iova_domain(&venc_iommu.iovad, PAGE_SIZE,
			 VENC_IOVA_START >> PAGE_SHIFT);

	cfg.pgsize_bitmap = SZ_4K;
	cfg.ias = 32;
	cfg.oas = 40;
	cfg.coherent_walk = false;
	cfg.iommu_dev = dev;
	cfg.quirks = IO_PGTABLE_QUIRK_ARM_NS;
	cfg.alloc = venc_pgtable_alloc;
	cfg.free = venc_pgtable_free;

	venc_iommu.ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1, &cfg, NULL);
	if (!venc_iommu.ops) {
		ret = -ENOMEM;
		goto err_iova;
	}

	venc_iommu.ttbr = cfg.arm_lpae_s1_cfg.ttbr;
	venc_iommu.ready = true;

	dev_info(dev, "VENC SMMU ready, ttbr %pad\n", &venc_iommu.ttbr);
	return 0;

err_iova:
	put_iova_domain(&venc_iommu.iovad);
	iova_cache_put();
	venc_iommu.iova_cache = false;
	return ret;
}

void venc_iommu_exit(void)
{
	struct venc_iommu_map *map, *tmp;

	if (!venc_iommu.ready)
		return;

	mutex_lock(&venc_iommu.lock);
	list_for_each_entry_safe(map, tmp, &venc_iommu.maps, node)
		venc_iommu_release_map(map);
	mutex_unlock(&venc_iommu.lock);

	free_io_pgtable_ops(venc_iommu.ops);
	venc_iommu.ops = NULL;
	put_iova_domain(&venc_iommu.iovad);
	if (venc_iommu.iova_cache)
		iova_cache_put();
	venc_iommu.iova_cache = false;
	venc_iommu.ready = false;
	venc_iommu.ttbr = 0;
}

u64 venc_iommu_ttbr(void)
{
	return venc_iommu.ready ? venc_iommu.ttbr : 0;
}

int venc_iommu_map_fd(int fd, dma_addr_t *iova, size_t *size)
{
	struct venc_iommu_map *map, *iter;
	unsigned long limit_pfn;
	int ret;

	if (!venc_iommu.ready)
		return -EINVAL;
	if (fd < 0 || !iova || !size)
		return -EINVAL;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	map->buf = dma_buf_get(fd);
	if (IS_ERR(map->buf)) {
		ret = PTR_ERR(map->buf);
		map->buf = NULL;
		goto err_free;
	}

	mutex_lock(&venc_iommu.lock);
	list_for_each_entry(iter, &venc_iommu.maps, node) {
		if (iter->buf == map->buf) {
			iter->refs++;
			*iova = iter->iova_start;
			*size = iter->size;
			mutex_unlock(&venc_iommu.lock);
			dma_buf_put(map->buf);
			kfree(map);
			return 0;
		}
	}
	mutex_unlock(&venc_iommu.lock);

	map->attach = dma_buf_attach(map->buf, venc_iommu.dev);
	if (IS_ERR(map->attach)) {
		ret = PTR_ERR(map->attach);
		map->attach = NULL;
		goto err_put;
	}

	map->sgt = dma_buf_map_attachment(map->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(map->sgt)) {
		ret = PTR_ERR(map->sgt);
		map->sgt = NULL;
		goto err_detach;
	}

	map->size = PAGE_ALIGN(map->buf->size);
	if (!map->size || map->size > VENC_IOVA_LIMIT - VENC_IOVA_START) {
		ret = -EINVAL;
		goto err_unmap_attachment;
	}

	limit_pfn = (VENC_IOVA_LIMIT >> PAGE_SHIFT) - 1;
	map->iova = alloc_iova(&venc_iommu.iovad, map->size >> PAGE_SHIFT,
			       limit_pfn, true);
	if (!map->iova) {
		ret = -ENOMEM;
		goto err_unmap_attachment;
	}

	map->iova_start = (dma_addr_t)map->iova->pfn_lo << PAGE_SHIFT;

	ret = venc_iommu_map_sgt(map->sgt, map->iova_start, map->size);
	if (ret)
		goto err_free_iova;

	map->refs = 1;

	mutex_lock(&venc_iommu.lock);
	list_add_tail(&map->node, &venc_iommu.maps);
	mutex_unlock(&venc_iommu.lock);

	*iova = map->iova_start;
	*size = map->size;
	dev_dbg(venc_iommu.dev, "mapped fd %d at %pad size %zu\n",
		fd, iova, map->size);
	return 0;

err_free_iova:
	__free_iova(&venc_iommu.iovad, map->iova);
err_unmap_attachment:
	dma_buf_unmap_attachment(map->attach, map->sgt, DMA_BIDIRECTIONAL);
err_detach:
	dma_buf_detach(map->buf, map->attach);
err_put:
	dma_buf_put(map->buf);
err_free:
	kfree(map);
	return ret;
}

int venc_iommu_unmap_iova(dma_addr_t iova)
{
	struct venc_iommu_map *map, *tmp;

	if (!venc_iommu.ready)
		return -EINVAL;

	mutex_lock(&venc_iommu.lock);
	list_for_each_entry_safe(map, tmp, &venc_iommu.maps, node) {
		if (map->iova_start == iova) {
			if (--map->refs) {
				mutex_unlock(&venc_iommu.lock);
				return 0;
			}
			venc_iommu_release_map(map);
			mutex_unlock(&venc_iommu.lock);
			return 0;
		}
	}
	mutex_unlock(&venc_iommu.lock);

	return -ENOENT;
}
