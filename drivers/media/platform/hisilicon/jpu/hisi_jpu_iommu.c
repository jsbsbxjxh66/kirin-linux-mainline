/* Copyright (c) 2018-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "hisi_jpu.h"
#include "hisi_jpu_iommu.h"

#define MAX_INPUT_DATA_LEN	(8192 * 8192 * 4)
#define JPU_IOVA_START	0x40000UL
#define JPU_IOVA_LIMIT	0x80000000UL
#define JPU_IOVA_PROT	(IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE)

static void *jpu_pgtable_alloc(void *cookie, size_t size, gfp_t gfp)
{
	size = max_t(size_t, size, 64);

	return (void *)__get_free_pages(gfp | __GFP_ZERO | __GFP_DMA32,
				       get_order(size));
}

static void jpu_pgtable_free(void *cookie, void *pages, size_t size)
{
	size = max_t(size_t, size, 64);
	free_pages((unsigned long)pages, get_order(size));
}

static int jpu_iommu_map_sgt(struct hisi_jpu_data_type *hisijd,
			     struct sg_table *sgt, dma_addr_t iova,
			     size_t size)
{
	struct io_pgtable_ops *ops = hisijd->iommu.ops;
	struct scatterlist *sg;
	size_t mapped = 0;
	int i, ret;

	for_each_sgtable_sg(sgt, sg, i) {
		size_t len = PAGE_ALIGN(sg->length);
		size_t done = 0;
		phys_addr_t phys = sg_phys(sg);

		if (!sg->length)
			continue;

		if (!IS_ALIGNED(phys, PAGE_SIZE) ||
		    mapped + len > size) {
			HISI_JPU_ERR("invalid sg: phys=%pa len=%zu mapped=%zu size=%zu\n",
				     &phys, (size_t)sg->length, mapped, size);
			ret = -EINVAL;
			goto err;
		}

		ret = ops->map_pages(ops, iova + mapped, phys, PAGE_SIZE,
				     len >> PAGE_SHIFT, JPU_IOVA_PROT,
				     GFP_KERNEL, &done);
		if (ret || done != len) {
			HISI_JPU_ERR("map_pages failed: ret=%d done=%zu len=%zu\n",
				     ret, done, len);
			if (!ret)
				ret = -ENOMEM;
			goto err;
		}

		mapped += len;
	}

	if (mapped < size) {
		HISI_JPU_ERR("sg too small: mapped=%zu size=%zu\n", mapped, size);
		ret = -EINVAL;
		goto err;
	}

	return 0;

err:
	if (mapped) {
		struct iommu_iotlb_gather gather;

		iommu_iotlb_gather_init(&gather);
		ops->unmap_pages(ops, iova, PAGE_SIZE, mapped >> PAGE_SHIFT,
				 &gather);
	}
	return ret;
}

static void jpu_iommu_unmap_sgt(struct hisi_jpu_data_type *hisijd,
				struct jpu_iommu_map *map)
{
	struct iommu_iotlb_gather gather;

	if (!map->mapped)
		return;

	iommu_iotlb_gather_init(&gather);
	hisijd->iommu.ops->unmap_pages(hisijd->iommu.ops, map->iova_start,
				       PAGE_SIZE, map->size >> PAGE_SHIFT,
				       &gather);
	__free_iova(&hisijd->iommu.iovad, map->iova);
	map->iova = NULL;
	map->mapped = false;
}

static int jpu_iommu_map_dmabuf(struct hisi_jpu_data_type *hisijd, int fd,
				struct jpu_iommu_map *map)
{
	struct device *dev = &hisijd->pdev->dev;
	unsigned long limit_pfn;
	int ret;

	if (!hisijd->iommu.ready) {
		HISI_JPU_ERR("iommu is not ready\n");
		return -EINVAL;
	}

	if (fd < 0) {
		HISI_JPU_ERR("invalid dma-buf fd %d\n", fd);
		return -EINVAL;
	}

	map->buf = dma_buf_get(fd);
	if (IS_ERR(map->buf)) {
		ret = PTR_ERR(map->buf);
		map->buf = NULL;
		return ret;
	}

	map->attach = dma_buf_attach(map->buf, dev);
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
	if (!map->size || map->size > JPU_IOVA_LIMIT - JPU_IOVA_START) {
		HISI_JPU_ERR("invalid dma-buf size %zu\n", map->size);
		ret = -EINVAL;
		goto err_unmap_attachment;
	}

	limit_pfn = (JPU_IOVA_LIMIT >> PAGE_SHIFT) - 1;
	map->iova = alloc_iova(&hisijd->iommu.iovad,
			       map->size >> PAGE_SHIFT, limit_pfn, true);
	if (!map->iova) {
		HISI_JPU_ERR("failed to allocate iova for %zu bytes\n",
			     map->size);
		ret = -ENOMEM;
		goto err_unmap_attachment;
	}

	map->iova_start = (dma_addr_t)map->iova->pfn_lo << PAGE_SHIFT;
	map->mapped = true;

	ret = jpu_iommu_map_sgt(hisijd, map->sgt, map->iova_start, map->size);
	if (ret)
		goto err_free_iova;

	HISI_JPU_INFO("mapped fd %d at %pad size %zu\n", fd, &map->iova_start,
		      map->size);
	return 0;

err_free_iova:
	__free_iova(&hisijd->iommu.iovad, map->iova);
	map->iova = NULL;
	map->mapped = false;
err_unmap_attachment:
	dma_buf_unmap_attachment(map->attach, map->sgt, DMA_BIDIRECTIONAL);
	map->sgt = NULL;
err_detach:
	dma_buf_detach(map->buf, map->attach);
	map->attach = NULL;
err_put:
	dma_buf_put(map->buf);
	map->buf = NULL;
	map->size = 0;
	return ret;
}

static void jpu_iommu_unmap_dmabuf(struct hisi_jpu_data_type *hisijd,
				   struct jpu_iommu_map *map)
{
	if (!map->buf)
		return;

	jpu_iommu_unmap_sgt(hisijd, map);

	if (map->sgt)
		dma_buf_unmap_attachment(map->attach, map->sgt,
					 DMA_BIDIRECTIONAL);
	if (map->attach)
		dma_buf_detach(map->buf, map->attach);

	dma_buf_put(map->buf);
	memset(map, 0, sizeof(*map));
}

int hisijpu_create_buffer_client(struct hisi_jpu_data_type *hisijd)
{
	return 0;
}

void hisijpu_destroy_buffer_client(struct hisi_jpu_data_type *hisijd)
{
	struct jpu_iommu *iommu = &hisijd->iommu;

	if (!iommu->ready)
		return;

	free_io_pgtable_ops(iommu->ops);
	iommu->ops = NULL;
	put_iova_domain(&iommu->iovad);
	if (iommu->iova_cache)
		iova_cache_put();
	iommu->iova_cache = false;
	iommu->ready = false;
}

int hisijpu_enable_iommu(struct hisi_jpu_data_type *hisijd)
{
	struct io_pgtable_cfg cfg = { 0 };
	struct device *dev;
	int ret;

	if (!hisijd || !hisijd->pdev) {
		HISI_JPU_ERR("hisijd is NULL\n");
		return -EINVAL;
	}

	dev = &hisijd->pdev->dev;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40));
	if (ret) {
		HISI_JPU_ERR("failed to set DMA mask: %d\n", ret);
		return ret;
	}

	ret = iova_cache_get();
	if (ret) {
		HISI_JPU_ERR("failed to get iova cache: %d\n", ret);
		return ret;
	}
	hisijd->iommu.iova_cache = true;

	init_iova_domain(&hisijd->iommu.iovad, PAGE_SIZE,
			 JPU_IOVA_START >> PAGE_SHIFT);

	cfg.pgsize_bitmap = SZ_4K;
	cfg.ias = 32;
	cfg.oas = 40;
	cfg.coherent_walk = false;
	cfg.iommu_dev = dev;
	cfg.quirks = IO_PGTABLE_QUIRK_ARM_NS;
	cfg.alloc = jpu_pgtable_alloc;
	cfg.free = jpu_pgtable_free;

	hisijd->iommu.ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1, &cfg, hisijd);
	if (!hisijd->iommu.ops) {
		HISI_JPU_ERR("failed to allocate io-pgtable\n");
		ret = -ENOMEM;
		goto err_put_domain;
	}

	hisijd->iommu.ttbr = cfg.arm_lpae_s1_cfg.ttbr;
	hisijd->iommu.ready = true;

	HISI_JPU_INFO("iommu ready, ttbr %pad\n", &hisijd->iommu.ttbr);
	return 0;

err_put_domain:
	put_iova_domain(&hisijd->iommu.iovad);
	iova_cache_put();
	hisijd->iommu.iova_cache = false;
	return ret;
}

phys_addr_t hisi_jpu_domain_get_ttbr(struct hisi_jpu_data_type *hisijd)
{
	if (!hisijd || !hisijd->iommu.ready)
		return 0;

	return hisijd->iommu.ttbr;
}

int hisi_jpu_lb_alloc(struct hisi_jpu_data_type *hisijd)
{
	struct jpu_iommu *iommu;
	dma_addr_t dma_handle;
	unsigned long limit_pfn;
	size_t done = 0;
	int ret;

	if (!hisijd || !hisijd->pdev) {
		HISI_JPU_ERR("hisijd is NULL\n");
		return -EINVAL;
	}

	if (!hisijd->iommu.ready) {
		HISI_JPU_ERR("iommu is not ready\n");
		return -EINVAL;
	}

	iommu = &hisijd->iommu;
	iommu->lb_size = JPU_LB_SIZE;
	iommu->lb_cpu = dma_alloc_coherent(&hisijd->pdev->dev, iommu->lb_size,
					   &dma_handle,
					   GFP_KERNEL | __GFP_DMA32);
	if (!iommu->lb_cpu) {
		HISI_JPU_ERR("failed to allocate line buffer\n");
		return -ENOMEM;
	}
	iommu->lb_phys = dma_handle;

	limit_pfn = (JPU_IOVA_LIMIT >> PAGE_SHIFT) - 1;
	iommu->lb_iova = alloc_iova(&iommu->iovad,
				    iommu->lb_size >> PAGE_SHIFT,
				    limit_pfn, true);
	if (!iommu->lb_iova) {
		HISI_JPU_ERR("failed to allocate line buffer iova\n");
		ret = -ENOMEM;
		goto err_free;
	}

	hisijd->lb_addr = (uint32_t)(iommu->lb_iova->pfn_lo << PAGE_SHIFT) >> 4;

	ret = iommu->ops->map_pages(iommu->ops,
				    (unsigned long)iommu->lb_iova->pfn_lo << PAGE_SHIFT,
				    iommu->lb_phys, PAGE_SIZE,
				    iommu->lb_size >> PAGE_SHIFT,
				    JPU_IOVA_PROT, GFP_KERNEL, &done);
	if (ret || done != iommu->lb_size) {
		HISI_JPU_ERR("failed to map line buffer: ret=%d done=%zu\n",
			     ret, done);
		if (!ret)
			ret = -ENOMEM;
		goto err_free_iova;
	}

	if (hisijd->lb_addr & (JPU_LB_ADDR_ALIGN - 1)) {
		HISI_JPU_ERR("lb_addr 0x%x is misaligned\n", hisijd->lb_addr);
		ret = -EINVAL;
		goto err_unmap;
	}

	HISI_JPU_INFO("lb_addr 0x%x size %zu\n", hisijd->lb_addr,
		      iommu->lb_size);
	return 0;

err_unmap:
	{
		struct iommu_iotlb_gather gather;

		iommu_iotlb_gather_init(&gather);
		iommu->ops->unmap_pages(iommu->ops,
					(unsigned long)iommu->lb_iova->pfn_lo << PAGE_SHIFT,
					PAGE_SIZE, iommu->lb_size >> PAGE_SHIFT,
					&gather);
	}
err_free_iova:
	__free_iova(&iommu->iovad, iommu->lb_iova);
	iommu->lb_iova = NULL;
err_free:
	dma_free_coherent(&hisijd->pdev->dev, iommu->lb_size, iommu->lb_cpu,
			  iommu->lb_phys);
	iommu->lb_cpu = NULL;
	iommu->lb_size = 0;
	return ret;
}

void hisi_jpu_lb_free(struct hisi_jpu_data_type *hisijd)
{
	struct jpu_iommu *iommu;

	if (!hisijd)
		return;

	iommu = &hisijd->iommu;
	if (!iommu->lb_iova)
		return;

	{
		struct iommu_iotlb_gather gather;

		iommu_iotlb_gather_init(&gather);
		iommu->ops->unmap_pages(iommu->ops,
					(unsigned long)iommu->lb_iova->pfn_lo << PAGE_SHIFT,
					PAGE_SIZE, iommu->lb_size >> PAGE_SHIFT,
					&gather);
	}

	__free_iova(&iommu->iovad, iommu->lb_iova);
	iommu->lb_iova = NULL;
	dma_free_coherent(&hisijd->pdev->dev, iommu->lb_size, iommu->lb_cpu,
			  iommu->lb_phys);
	iommu->lb_cpu = NULL;
	iommu->lb_size = 0;
	hisijd->lb_addr = 0;
}

int hisi_jpu_map_input(struct hisi_jpu_data_type *hisijd, int fd)
{
	hisi_jpu_unmap_input(hisijd);
	return jpu_iommu_map_dmabuf(hisijd, fd, &hisijd->in_map);
}

void hisi_jpu_unmap_input(struct hisi_jpu_data_type *hisijd)
{
	if (hisijd)
		jpu_iommu_unmap_dmabuf(hisijd, &hisijd->in_map);
}

int hisi_jpu_map_output(struct hisi_jpu_data_type *hisijd, int fd)
{
	hisi_jpu_unmap_output(hisijd);
	return jpu_iommu_map_dmabuf(hisijd, fd, &hisijd->out_map);
}

void hisi_jpu_unmap_output(struct hisi_jpu_data_type *hisijd)
{
	if (hisijd)
		jpu_iommu_unmap_dmabuf(hisijd, &hisijd->out_map);
}

int hisi_jpu_check_inbuff_addr(struct hisi_jpu_data_type *hisijd,
			       jpu_data_t *jpu_req)
{
	uint64_t inbuffer_addr;
	int ret;

	if (!hisijd || !hisijd->pdev || !jpu_req)
		return -EINVAL;

	if (jpu_req->end_addr <= jpu_req->start_addr) {
		HISI_JPU_ERR("invalid input range [%llu, %llu]\n",
			     jpu_req->start_addr, jpu_req->end_addr);
		return -EINVAL;
	}

	if (jpu_req->addr_offset > jpu_req->start_addr ||
	    jpu_req->addr_offset > MAX_INPUT_DATA_LEN) {
		HISI_JPU_ERR("invalid input offset %u\n", jpu_req->addr_offset);
		return -EINVAL;
	}

	ret = hisi_jpu_map_input(hisijd, jpu_req->in_sharefd);
	if (ret) {
		HISI_JPU_ERR("failed to map input buffer: %d\n", ret);
		return ret;
	}

	if (jpu_req->end_addr > hisijd->in_map.size) {
		HISI_JPU_ERR("input end %llu beyond buffer %zu\n",
			     jpu_req->end_addr, hisijd->in_map.size);
		hisi_jpu_unmap_input(hisijd);
		return -EINVAL;
	}

	inbuffer_addr = hisijd->in_map.iova_start;
	jpu_req->start_addr += inbuffer_addr;
	jpu_req->end_addr += inbuffer_addr;

	HISI_JPU_INFO("input iova %llu size %zu\n", inbuffer_addr,
		      hisijd->in_map.size);
	return 0;
}

int hisi_jpu_check_outbuff_addr(struct hisi_jpu_data_type *hisijd,
				jpu_data_t *jpu_req)
{
	uint64_t outbuffer_addr;
	int ret;

	if (!hisijd || !hisijd->pdev || !jpu_req)
		return -EINVAL;

	ret = hisi_jpu_map_output(hisijd, jpu_req->out_sharefd);
	if (ret) {
		HISI_JPU_ERR("failed to map output buffer: %d\n", ret);
		return ret;
	}

	outbuffer_addr = hisijd->out_map.iova_start;
	jpu_req->start_addr_y = outbuffer_addr / 16;

	if ((jpu_req->out_color_format >= HISI_JPEG_DECODE_OUT_RGBA4444) &&
	    (HISI_JPEG_DECODE_OUT_BGRA8888 >= jpu_req->out_color_format)) {
		jpu_req->last_page_y += jpu_req->start_addr_y / 2048;
	} else {
		if (HISI_JPEG_DECODE_OUT_YUV400 == jpu_req->out_color_format)
			jpu_req->last_page_y += jpu_req->start_addr_y / 2048;

		if ((HISI_JPEG_DECODE_OUT_YUV420 == jpu_req->out_color_format) ||
		    (HISI_JPEG_DECODE_OUT_YUV422_H2V1 == jpu_req->out_color_format) ||
		    (HISI_JPEG_DECODE_OUT_YUV444 == jpu_req->out_color_format) ||
		    (HISI_JPEG_DECODE_OUT_YUV422_H1V2 == jpu_req->out_color_format)) {
			jpu_req->last_page_y += jpu_req->start_addr_y / 2048;
			jpu_req->start_addr_c += jpu_req->start_addr_y;
			jpu_req->last_page_c += jpu_req->start_addr_y / 2048;
		}
	}

	if ((jpu_req->last_page_y < (jpu_req->start_addr_y / 2048)) ||
	    (jpu_req->last_page_c < (jpu_req->start_addr_c / 2048))) {
		HISI_JPU_ERR("invalid output page range\n");
		hisi_jpu_unmap_output(hisijd);
		return -EINVAL;
	}

	HISI_JPU_INFO("output iova %llu size %zu\n", outbuffer_addr,
		      hisijd->out_map.size);
	return 0;
}
