/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_HISI_IOMMU_H
#define __LINUX_HISI_IOMMU_H

#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/types.h>

struct hisi_iommu_domain_data {
	unsigned long iova_start;
	unsigned long iova_size;
	unsigned long iova_align;
	phys_addr_t phy_pgd_base;
};

int of_get_iova_info(struct device_node *np, unsigned long *iova_start,
		     unsigned long *iova_size, unsigned long *iova_align);
phys_addr_t hisi_domain_get_ttbr(struct device *dev);

#endif /* __LINUX_HISI_IOMMU_H */
