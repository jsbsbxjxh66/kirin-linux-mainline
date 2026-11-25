/*
 * Copyright (c) 2018-2019, Hisilicon Tech. Co., Ltd. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "drv_venc_osal.h"
#include "hi_drv_mem.h"
#include "venc_iommu.h"

#define MAX_BUFFER_SIZE	(10 * 1024)

struct venc_kalloc_node {
	void *virt;
	dma_addr_t phys;
	size_t size;
	bool used;
};

char *g_sbuf;
static struct venc_kalloc_node g_venc_mem_node[MAX_KMALLOC_MEM_NODE];
static DEFINE_SEMAPHORE(g_VencMemSem, 1);

extern struct platform_device *gPlatDev;
VENC_SMMU_ERR_ADDR g_smmu_err_mem;

static struct venc_kalloc_node *venc_find_kalloc(dma_addr_t phys, void *virt)
{
	int i;

	for (i = 0; i < MAX_KMALLOC_MEM_NODE; i++) {
		if (g_venc_mem_node[i].used &&
		    g_venc_mem_node[i].phys == phys &&
		    g_venc_mem_node[i].virt == virt)
			return &g_venc_mem_node[i];
	}

	return NULL;
}

HI_S32 DRV_MEM_INIT(HI_VOID)
{
	MEM_BUFFER_S rd = { 0 };
	MEM_BUFFER_S wr = { 0 };
	int ret;

	g_sbuf = vmalloc(MAX_BUFFER_SIZE);
	if (!g_sbuf)
		return HI_FAILURE;
	memset(g_sbuf, 0, MAX_BUFFER_SIZE);

	memset(g_venc_mem_node, 0, sizeof(g_venc_mem_node));
	memset(&g_smmu_err_mem, 0, sizeof(g_smmu_err_mem));

	rd.u32Size = SMMU_RWERRADDR_SIZE;
	ret = DRV_MEM_KAlloc("SMMU_RDERR", "OMXVENC", &rd);
	if (ret) {
		HI_ERR_VENC("SMMU_RDERR alloc failed\n");
		goto err_sbuf;
	}

	wr.u32Size = SMMU_RWERRADDR_SIZE;
	ret = DRV_MEM_KAlloc("SMMU_WRERR", "OMXVENC", &wr);
	if (ret) {
		HI_ERR_VENC("SMMU_WRERR alloc failed\n");
		goto err_rd;
	}

	g_smmu_err_mem.RdAddr = rd.u64StartPhyAddr;
	g_smmu_err_mem.WrAddr = wr.u64StartPhyAddr;

	return HI_SUCCESS;

err_rd:
	DRV_MEM_KFree(&rd);
err_sbuf:
	vfree(g_sbuf);
	g_sbuf = NULL;
	return HI_FAILURE;
}

HI_S32 DRV_MEM_EXIT(HI_VOID)
{
	int i;

	for (i = 0; i < MAX_KMALLOC_MEM_NODE; i++) {
		struct venc_kalloc_node *node = &g_venc_mem_node[i];

		if (!node->used)
			continue;
		dma_free_coherent(&gPlatDev->dev, node->size, node->virt,
				  node->phys);
		memset(node, 0, sizeof(*node));
	}

	if (g_sbuf) {
		vfree(g_sbuf);
		g_sbuf = NULL;
	}

	return HI_SUCCESS;
}

HI_S32 DRV_MEM_KAlloc(const HI_CHAR *bufName, const HI_CHAR *zone_name,
		      MEM_BUFFER_S *psMBuf)
{
	struct venc_kalloc_node *node = NULL;
	dma_addr_t phys;
	void *virt;
	int i;

	if (!psMBuf || !psMBuf->u32Size)
		return HI_FAILURE;

	if (down_interruptible(&g_VencMemSem))
		return HI_FAILURE;

	for (i = 0; i < MAX_KMALLOC_MEM_NODE; i++) {
		if (!g_venc_mem_node[i].used) {
			node = &g_venc_mem_node[i];
			break;
		}
	}
	if (!node) {
		HI_ERR_VENC("no free kalloc node\n");
		goto err_unlock;
	}

	virt = dma_alloc_coherent(&gPlatDev->dev, psMBuf->u32Size, &phys,
				  GFP_KERNEL);
	if (!virt) {
		HI_ERR_VENC("dma_alloc_coherent(%u) failed\n", psMBuf->u32Size);
		goto err_unlock;
	}
	memset(virt, 0, psMBuf->u32Size);

	node->virt = virt;
	node->phys = phys;
	node->size = psMBuf->u32Size;
	node->used = true;

	psMBuf->pStartVirAddr = virt;
	psMBuf->u64StartPhyAddr = phys;

	up(&g_VencMemSem);
	return HI_SUCCESS;

err_unlock:
	up(&g_VencMemSem);
	return HI_FAILURE;
}

HI_S32 DRV_MEM_KFree(const MEM_BUFFER_S *psMBuf)
{
	struct venc_kalloc_node *node;

	if (!psMBuf || !psMBuf->pStartVirAddr || !psMBuf->u64StartPhyAddr)
		return HI_FAILURE;

	if (down_interruptible(&g_VencMemSem))
		return HI_FAILURE;

	node = venc_find_kalloc(psMBuf->u64StartPhyAddr,
				psMBuf->pStartVirAddr);
	if (!node) {
		HI_ERR_VENC("kalloc node not found\n");
		up(&g_VencMemSem);
		return HI_FAILURE;
	}

	dma_free_coherent(&gPlatDev->dev, node->size, node->virt, node->phys);
	memset(node, 0, sizeof(*node));

	up(&g_VencMemSem);
	return HI_SUCCESS;
}

HI_S32 DRV_MEM_GetMapInfo(HI_S32 share_fd, MEM_BUFFER_S *psMBuf)
{
	dma_addr_t iova;
	size_t size;
	int ret;

	if (share_fd <= 0 || !psMBuf)
		return HI_FAILURE;

	ret = venc_iommu_map_fd(share_fd, &iova, &size);
	if (ret) {
		HI_ERR_VENC("map fd %d failed: %d\n", share_fd, ret);
		return HI_FAILURE;
	}

	psMBuf->u32Size = size;
	psMBuf->u64StartPhyAddr = iova;
	psMBuf->u32ShareFd = share_fd;

	return HI_SUCCESS;
}

HI_S32 DRV_MEM_PutMapInfo(MEM_BUFFER_S *psMBuf)
{
	if (!psMBuf || !psMBuf->u64StartPhyAddr)
		return HI_FAILURE;

	return venc_iommu_unmap_iova(psMBuf->u64StartPhyAddr) ?
		HI_FAILURE : HI_SUCCESS;
}

HI_S32 DRV_MEM_IommuMap(VencBufferRecord *node,
			struct platform_device *pltdev)
{
	dma_addr_t iova;
	size_t size;
	int ret;

	if (!node || node->share_fd <= 0)
		return HI_FAILURE;

	ret = venc_iommu_map_fd(node->share_fd, &iova, &size);
	if (ret) {
		HI_ERR_VENC("map fd %d failed: %d\n", node->share_fd, ret);
		return HI_FAILURE;
	}

	node->iova = iova;
	node->iova_size = size;

	return HI_SUCCESS;
}

HI_S32 DRV_MEM_IommuUnmap(HI_S32 share_fd, HI_U64 phys_addr,
			  struct platform_device *pltdev)
{
	if (!phys_addr)
		return HI_FAILURE;

	return venc_iommu_unmap_iova(phys_addr) ? HI_FAILURE : HI_SUCCESS;
}

HI_S32 HI_DRV_UserCopy(struct file *file, HI_U32 cmd, unsigned long arg,
		       long (*func)(struct file *file, HI_U32 cmd,
				    unsigned long uarg))
{
	void *parg;
	long err;

	if (!(void __user *)arg)
		return -EINVAL;
	if (_IOC_SIZE(cmd) > MAX_BUFFER_SIZE || !g_sbuf)
		return -EINVAL;

	parg = g_sbuf;
	err = -EFAULT;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(parg, (void __user *)arg, _IOC_SIZE(cmd)))
			return err;
	}

	err = func(file, cmd, (unsigned long)parg);
	if (err == -ENOIOCTLCMD)
		return -EINVAL;
	if (err < 0)
		return err;

	switch (_IOC_DIR(cmd)) {
	case _IOC_READ:
	case _IOC_WRITE | _IOC_READ:
		if (copy_to_user((void __user *)arg, parg, _IOC_SIZE(cmd)))
			err = -EFAULT;
		break;
	default:
		break;
	}

	return err;
}
