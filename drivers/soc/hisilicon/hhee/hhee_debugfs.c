// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon HHEE debugfs interface.
 */

#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "hhee.h"

static ssize_t hhee_log_read(struct file *file, char __user *buf,
			     size_t count, loff_t *ppos)
{
	return hhee_copy_logs(buf, count, ppos,
			      (int)(uintptr_t)file->private_data);
}

static const struct file_operations hhee_log_fops = {
	.owner = THIS_MODULE,
	.read = hhee_log_read,
	.llseek = default_llseek,
};

static struct dentry *hhee_dir;

int hhee_debugfs_init(void)
{
	hhee_dir = debugfs_create_dir("hhee", NULL);
	if (IS_ERR(hhee_dir)) {
		int ret = PTR_ERR(hhee_dir);

		hhee_dir = NULL;
		return ret;
	}

	debugfs_create_file("normal", 0400, hhee_dir,
			    (void *)(uintptr_t)NORMAL_LOG, &hhee_log_fops);
	debugfs_create_file("crash", 0400, hhee_dir,
			    (void *)(uintptr_t)CRASH_LOG, &hhee_log_fops);
	debugfs_create_file("monitor", 0400, hhee_dir,
			    (void *)(uintptr_t)MONITOR_LOG, &hhee_log_fops);

	return 0;
}

void hhee_debugfs_exit(void)
{
	debugfs_remove_recursive(hhee_dir);
	hhee_dir = NULL;
}
