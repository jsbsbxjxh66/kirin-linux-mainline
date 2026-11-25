// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2012-2015. All rights reserved.
 *
 * Balong PMIC OCP forwarding.
 *
 * The hi6421v600 maintenance monitor knows which regulator tripped, while the
 * modem knows what that rail supplies.  Register a handler for each modem rail
 * so that the rail ID can be forwarded over ICC before the generic AP OCP
 * policy cuts the rail or restarts the SoC.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/mfd/hi6421v600-mntn.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include <bsp_icc.h>
#include <bsp_trace.h>

#define pmic_print_error(fmt, ...)					\
	bsp_trace(BSP_LOG_LEVEL_ERROR, BSP_MODU_PMU,			\
		  "[pmu]: <%s> " fmt, __func__, ##__VA_ARGS__)

struct pmic_volt_ocp {
	u32 volt_id;
	const char *volt_name;
	struct list_head list;
};

static LIST_HEAD(g_modem_ocp_list);

static int pmic_modem_ocp_handler(const char *name)
{
	struct pmic_volt_ocp *ocp_node, *found = NULL;
	u32 chan_id = ICC_CHN_IFC << 16 | IFC_RECV_FUNC_PMU_OCP;
	int len;

	if (!name) {
		pmic_print_error("name is null\n");
		return -EINVAL;
	}

	list_for_each_entry(ocp_node, &g_modem_ocp_list, list) {
		if (!strcmp(ocp_node->volt_name, name)) {
			found = ocp_node;
			break;
		}
	}

	if (!found) {
		pmic_print_error("no modem rail for %s\n", name);
		return -ENOENT;
	}

	len = bsp_icc_send(ICC_CPU_MODEM, chan_id,
			   (unsigned char *)&found->volt_id,
			   sizeof(found->volt_id));
	if (len != sizeof(found->volt_id)) {
		pmic_print_error("send len(%d) != expected len(%zu)\n",
				 len, sizeof(found->volt_id));
		return len < 0 ? len : -EIO;
	}

	return 0;
}

static int __init pmic_modem_ocp_init(void)
{
	struct device_node *root, *child_node;
	struct pmic_volt_ocp *ocp;
	int ret;

	root = of_find_compatible_node(NULL, NULL, "hisilicon,pmic_ocp_modem");
	if (!root) {
		pmic_print_error("get pmic dts node failed\n");
		return -ENODEV;
	}

	for_each_available_child_of_node(root, child_node) {
		ocp = kzalloc_obj(*ocp);
		if (!ocp) {
			of_node_put(child_node);
			of_node_put(root);
			return -ENOMEM;
		}

		INIT_LIST_HEAD(&ocp->list);
		if (of_property_read_u32(child_node, "id", &ocp->volt_id)) {
			pmic_print_error("bad OCP rail node %pOF\n", child_node);
			kfree(ocp);
			continue;
		}

		ocp->volt_name = kstrdup(child_node->name, GFP_KERNEL);
		if (!ocp->volt_name) {
			kfree(ocp);
			of_node_put(child_node);
			of_node_put(root);
			return -ENOMEM;
		}

		ret = hi6421v600_mntn_register_ocp_handler(ocp->volt_name,
							   pmic_modem_ocp_handler);
		if (ret) {
			pmic_print_error("modem(%s) ocp register fail: %d\n",
					 ocp->volt_name, ret);
			kfree(ocp->volt_name);
			kfree(ocp);
			continue;
		}

		list_add_tail(&ocp->list, &g_modem_ocp_list);
	}

	of_node_put(root);

	return 0;
}
module_init(pmic_modem_ocp_init);

MODULE_DESCRIPTION("Balong PMIC OCP forwarding");
MODULE_LICENSE("Dual BSD/GPL");
