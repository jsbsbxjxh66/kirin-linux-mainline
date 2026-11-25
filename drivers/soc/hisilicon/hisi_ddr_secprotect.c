// SPDX-License-Identifier: GPL-2.0-only
/*
 * HiSilicon DDR secure protection and DMSS FIQ reporting
 *
 * The secure monitor owns the DDR firewall.  Linux programs dynamic secure
 * regions by filling a descriptor in the BL31 shared memory window and
 * issuing an SMC.  The DMSS reports violations through an FIQ; the flag is
 * published in the same shared window.
 *
 * Copyright (c) 2012-2013 Linaro Ltd.
 */

#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/wait.h>

#include <linux/hisi/global_ddr_map.h>
#include <linux/hisi/hisi_ddr.h>
#include <linux/hisi/hisi_drmdriver.h>
#include <linux/hisi/rdr_hisi_platform.h>
#include <linux/hisi/rdr_pub.h>

#define DRM_SHARE_MEM_OFFSET		0x100
#define DMSS_FIQ_FLAG			0x5a5a5a5a5a5a5a00ULL
#define IRQ_WDT_INTR_FIQ		76
#define IRQ_DMSS_INTR_FIQ		163

static void __iomem *dmss_intr_fiq;
static DECLARE_WAIT_QUEUE_HEAD(modemddrc_wait);
static atomic_t modemddrc_pending = ATOMIC_INIT(0);
static struct task_struct *modemddrc_task;

void dmss_ipi_handler(void)
{
	pr_crit("dmss_ipi_inform start\n");
	atomic_set(&modemddrc_pending, 1);
	wake_up_interruptible(&modemddrc_wait);
}

static int modemddrc_happen(void *arg)
{
	while (!kthread_should_stop()) {
		wait_event_interruptible(modemddrc_wait,
					 atomic_read(&modemddrc_pending) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			break;

		atomic_set(&modemddrc_pending, 0);
		pr_err("%s: goto rdr_system_error\n", __func__);
		rdr_system_error(RDR_MODEM_DMSS_MOD_ID, 0, 0);
	}

	return 0;
}

static void fiq_print_src(u64 intrsrc)
{
	unsigned int intr = intrsrc & 0xff;

	if (intr == IRQ_WDT_INTR_FIQ) {
		pr_err("fiq triggered by: Watchdog\n");
	} else if (intr == IRQ_DMSS_INTR_FIQ) {
		smp_send_stop();
		pr_err("fiq triggered by: DMSS\n");
	} else {
		pr_err("fiq triggered by: Unknown, intr=0x%x\n", intr);
	}
}

void dmss_fiq_handler(void)
{
	u64 intrsrc;

	if (!dmss_intr_fiq) {
		pr_err("dmss fiq handler is not initialized\n");
		return;
	}

	intrsrc = readq(dmss_intr_fiq);
	fiq_print_src(intrsrc);

	if ((DMSS_FIQ_FLAG | IRQ_DMSS_INTR_FIQ) == intrsrc) {
		pr_err("dmss intr happened, see BL31 log\n");
		rdr_syserr_process_for_ap(MODID_AP_S_DDRC_SEC, 0, 0);
	}
}

int hisi_sec_ddr_set(DRM_SEC_CFG *sec_cfg, DYNAMIC_DDR_SEC_TYPE type)
{
	phys_addr_t base = HISI_SUB_RESERVED_BL31_SHARE_MEM_PHYMEM_BASE +
			   DRM_SHARE_MEM_OFFSET;
	void __iomem *share;

	if (!sec_cfg)
		return -EINVAL;

	share = ioremap(base, sizeof(*sec_cfg));
	if (!share)
		return -ENOMEM;

	memcpy_toio(share, sec_cfg, sizeof(*sec_cfg));
	iounmap(share);

	return atfd_hisi_service_access_register_smc(ACCESS_REGISTER_FN_MAIN_ID,
						     base, type,
						     ACCESS_REGISTER_FN_SUB_ID_DDR_DRM_SET);
}
EXPORT_SYMBOL_GPL(hisi_sec_ddr_set);

int hisi_sec_ddr_clr(DYNAMIC_DDR_SEC_TYPE type)
{
	phys_addr_t base = HISI_SUB_RESERVED_BL31_SHARE_MEM_PHYMEM_BASE +
			   DRM_SHARE_MEM_OFFSET;

	return atfd_hisi_service_access_register_smc(ACCESS_REGISTER_FN_MAIN_ID,
						     base, type,
						     ACCESS_REGISTER_FN_SUB_ID_DDR_DRM_CLR);
}
EXPORT_SYMBOL_GPL(hisi_sec_ddr_clr);

static int hisi_ddr_secprotect_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	dmss_intr_fiq = devm_ioremap(dev,
				     HISI_SUB_RESERVED_BL31_SHARE_MEM_PHYMEM_BASE,
				     sizeof(u64));
	if (!dmss_intr_fiq)
		return -ENOMEM;

	modemddrc_task = kthread_run(modemddrc_happen, NULL, "modemddrc_emit");
	if (IS_ERR(modemddrc_task)) {
		int ret = PTR_ERR(modemddrc_task);

		modemddrc_task = NULL;
		dev_err(dev, "failed to create modemddrc thread: %d\n", ret);
		return ret;
	}

	return 0;
}

static void hisi_ddr_secprotect_remove(struct platform_device *pdev)
{
	if (modemddrc_task) {
		kthread_stop(modemddrc_task);
		modemddrc_task = NULL;
	}

	dmss_intr_fiq = NULL;
}

static const struct of_device_id hisi_ddr_secprotect_of_match[] = {
	{ .compatible = "hisilicon,ddr_secprotect" },
	{ }
};
MODULE_DEVICE_TABLE(of, hisi_ddr_secprotect_of_match);

static struct platform_driver hisi_ddr_secprotect_driver = {
	.probe = hisi_ddr_secprotect_probe,
	.remove = hisi_ddr_secprotect_remove,
	.driver = {
		.name = "ddr-secprotect",
		.of_match_table = hisi_ddr_secprotect_of_match,
	},
};
module_platform_driver(hisi_ddr_secprotect_driver);

MODULE_DESCRIPTION("HiSilicon DDR secure protection driver");
MODULE_LICENSE("GPL");
