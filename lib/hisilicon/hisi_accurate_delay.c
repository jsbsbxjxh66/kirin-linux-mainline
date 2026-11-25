// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <asm/arch_timer.h>
#include <linux/accurate_delay.h>

static unsigned long syscount_100us;

void accurate_delay_100us(unsigned long us_100)
{
	unsigned long now;
	unsigned long target;

	local_irq_disable();

	now = arch_timer_read_counter();
	target = now + us_100*syscount_100us;
	while (now < target)
		now = arch_timer_read_counter();

	local_irq_enable();

}

static int hisi_accurate_delay_init(void)
{
	syscount_100us = (unsigned long)(arch_timer_get_cntfrq() / 10000);

	return 0;
}
arch_initcall(hisi_accurate_delay_init);

MODULE_AUTHOR("Hisilicon Co. Ltd");
MODULE_DESCRIPTION("Hisi Accurate Delay Lib");
MODULE_LICENSE("GPL");
