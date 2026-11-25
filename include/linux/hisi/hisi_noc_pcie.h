/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_HISI_NOC_PCIE_H
#define __LINUX_HISI_NOC_PCIE_H

#include <linux/types.h>

bool is_pcie_target(int target_id);
void set_pcie_dump_flag(int target_id);
bool get_pcie_dump_flag(void);
void dump_pcie_apb_info(void);

#endif /* __LINUX_HISI_NOC_PCIE_H */
