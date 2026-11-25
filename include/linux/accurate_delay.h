/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ACCURATE_DELAY_H
#define _LINUX_ACCURATE_DELAY_H

#include <linux/delay.h>

#ifdef CONFIG_HISI_ACCURATE_DELAY
void accurate_delay_100us(unsigned long us_100);
#else
static inline void accurate_delay_100us(unsigned long us_100)
{
	udelay(100 * us_100);
}
#endif

#endif /* _LINUX_ACCURATE_DELAY_H */
