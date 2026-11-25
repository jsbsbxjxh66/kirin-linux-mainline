// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon HHEE hypervisor exception driver.
 *
 * Copyright (c) 2012-2013 Linaro Ltd.
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <linux/hisi/hisi_hhee.h>
#include <linux/hisi/rdr_hisi_platform.h>
#include <linux/hisi/rdr_pub.h>

#include "hhee.h"
#include "hhee_msg.h"

static bool hhee_enabled = true;

static struct rdr_exception_info_s hhee_exception_info = {
	.e_modid	    = MODID_AP_S_HHEE_PANIC,
	.e_modid_end	    = MODID_AP_S_HHEE_PANIC,
	.e_process_priority = RDR_ERR,
	.e_reboot_priority  = RDR_REBOOT_NOW,
	.e_notify_core_mask = RDR_AP,
	.e_reset_core_mask  = RDR_AP,
	.e_from_core	    = RDR_AP,
	.e_reentrant	    = RDR_REENTRANT_DISALLOW,
	.e_exce_type	    = AP_S_HHEE_PANIC,
	.e_upload_flag	    = RDR_UPLOAD_YES,
	.e_from_module	    = "RDR HHEE PANIC",
	.e_desc		    = "RDR HHEE PANIC",
};

static int hhee_panic_handle(unsigned int len, void *buf)
{
	pr_err("HHEE reported an EL2 panic\n");
	if (buf)
		pr_err("HHEE panic carried unexpected payload\n");

	rdr_syserr_process_for_ap(MODID_AP_S_HHEE_PANIC, 0, 0);
	return 0;
}

int hhee_check_enable(void)
{
	return hhee_enabled ? HHEE_ENABLE : HHEE_DISABLE;
}
EXPORT_SYMBOL_GPL(hhee_check_enable);

static int hisi_hhee_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int irq, ret;

	if (!hhee_check_enable())
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "failed to get IRQ\n");

	ret = devm_request_irq(dev, irq, hhee_irq_handle, 0, "hisi-hhee",
			       pdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	ret = (s32)rdr_register_exception(&hhee_exception_info);
	if (!ret)
		dev_err(dev, "failed to register RDR exception\n");

	ret = hhee_logger_init();
	if (ret)
		return dev_err_probe(dev, ret, "failed to init HHEE log\n");

	ret = hhee_msg_init();
	if (ret)
		goto err_log;

	ret = hhee_msg_register_handler(HHEE_MSG_ID_CRASH, hhee_panic_handle);
	if (ret)
		goto err_msg;

	ret = hhee_debugfs_init();
	if (ret)
		dev_warn(dev, "failed to create debugfs entries: %d\n", ret);

	dev_info(dev, "HHEE interface ready\n");
	return 0;

err_msg:
	hhee_msg_exit();
err_log:
	hhee_logger_exit();
	return dev_err_probe(dev, ret, "failed to init HHEE message queue\n");
}

static void hisi_hhee_remove(struct platform_device *pdev)
{
	rdr_unregister_exception(MODID_AP_S_HHEE_PANIC);
	hhee_debugfs_exit();
	hhee_msg_exit();
	hhee_logger_exit();
}

static const struct of_device_id hisi_hhee_of_match[] = {
	{ .compatible = "hisilicon,hisi-hhee" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_hhee_of_match);

static struct platform_driver hisi_hhee_driver = {
	.probe = hisi_hhee_probe,
	.remove = hisi_hhee_remove,
	.driver = {
		.name = "hisi-hhee",
		.of_match_table = hisi_hhee_of_match,
	},
};
builtin_platform_driver(hisi_hhee_driver);

static int __init hisi_hhee_cmd_get(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "0") || !strcmp(arg, "false") ||
	    !strcmp(arg, "no"))
		hhee_enabled = false;

	pr_info("HHEE enable = %d\n", hhee_enabled);
	return 0;
}
early_param("hhee_enable", hisi_hhee_cmd_get);

MODULE_DESCRIPTION("HiSilicon HHEE hypervisor exception driver");
MODULE_LICENSE("GPL");
