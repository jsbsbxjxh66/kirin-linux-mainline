/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HiSilicon HHEE hypervisor interface.
 */

#ifndef __HHEE_H__
#define __HHEE_H__

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/hisi/hisi_hhee.h>

struct circular_buffer {
	unsigned long size;
	unsigned long start;
	unsigned long end;
	unsigned long overflow;
	unsigned long virt_log_addr;
	unsigned long virt_log_size;
	unsigned int inited;
	unsigned int logtype;
	char *buf;
};

enum ltype {
	NORMAL_LOG,
	CRASH_LOG,
	MONITOR_LOG,
};

irqreturn_t hhee_irq_handle(int irq, void *data);

int hhee_logger_init(void);
void hhee_logger_exit(void);
ssize_t hhee_copy_logs(char __user *buf, size_t count, loff_t *offp,
		       int logtype);

void hhee_msg_exit(void);

#ifdef CONFIG_DEBUG_FS
int hhee_debugfs_init(void);
void hhee_debugfs_exit(void);
#else
static inline int hhee_debugfs_init(void)
{
	return 0;
}

static inline void hhee_debugfs_exit(void)
{
}
#endif

#endif /* __HHEE_H__ */
