// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon HHEE log buffer access.
 *
 * Copyright (c) 2013-2014 NVIDIA Corporation. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/memremap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <linux/hisi/hisi_hhee.h>

#include "hhee.h"

static bool hhee_logging_enabled;
static struct circular_buffer *cb;
static struct circular_buffer *crash_cb;
static struct circular_buffer *monitor_cb;

static int circ_buf_init(struct circular_buffer *cir_buf)
{
	struct circular_buffer *tmp_cb;

	if (!cir_buf || !cir_buf->virt_log_addr)
		return -EINVAL;

	tmp_cb = (struct circular_buffer *)(uintptr_t)cir_buf->virt_log_addr;

	if (!cir_buf->inited) {
		cir_buf->virt_log_addr = (unsigned long)tmp_cb;
		cir_buf->start = tmp_cb->start;
		cir_buf->inited = 1;
	} else {
		cir_buf->start = cir_buf->end;
	}

	if (!tmp_cb->size || tmp_cb->size > cir_buf->virt_log_size)
		return -EINVAL;
	if (tmp_cb->start >= tmp_cb->size || tmp_cb->end >= tmp_cb->size)
		return -EINVAL;

	cir_buf->size = tmp_cb->size;
	cir_buf->end = tmp_cb->end;
	cir_buf->overflow = tmp_cb->overflow;
	cir_buf->buf = (char *)tmp_cb;

	return 0;
}

static ssize_t circ_buf_copy(struct circular_buffer *incb, char *text)
{
	size_t len = 0;

	if (incb->end == incb->start)
		return 0;

	if (incb->start >= incb->size || incb->end >= incb->size) {
		pr_err("%s: cbuf pointers corrupted\n", __func__);
		return -EINVAL;
	}

	if (incb->end > incb->start) {
		len = incb->end - incb->start;
		memcpy(text, incb->buf + incb->start, len);
	} else {
		size_t first = incb->size - incb->start;

		memcpy(text, incb->buf + incb->start, first);
		memcpy(text + first, incb->buf, incb->end);
		len = first + incb->end;
	}

	incb->start = incb->end;

	return len;
}

ssize_t hhee_copy_logs(char __user *buf, size_t count, loff_t *offp,
		       int logtype)
{
	struct circular_buffer *incb;
	char *buffer;
	ssize_t len;

	if (!hhee_logging_enabled)
		return 0;

	switch (logtype) {
	case NORMAL_LOG:
		incb = cb;
		break;
	case CRASH_LOG:
		incb = crash_cb;
		break;
	case MONITOR_LOG:
		incb = monitor_cb;
		break;
	default:
		return -EINVAL;
	}

	if (!incb)
		return -ENODEV;

	if (circ_buf_init(incb))
		return -EINVAL;

	buffer = kzalloc(incb->size + 1, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	if (incb->overflow == 1) {
		pr_info("[HHEE] log buffer overwritten\n");
		incb->overflow = 0;
	}

	len = circ_buf_copy(incb, buffer);
	if (len < 0) {
		kfree(buffer);
		return len;
	}

	buffer[len] = '\0';
	len = simple_read_from_buffer(buf, count, offp, buffer, len);
	kfree(buffer);

	return len;
}

static int cb_init(u64 inlog_addr, u64 inlog_size,
		   struct circular_buffer **incb, unsigned int logtype)
{
	struct circular_buffer *tmp_cb;
	void *addr;

	if (!inlog_addr || !inlog_size)
		return -EINVAL;

	addr = memremap(inlog_addr, inlog_size, MEMREMAP_WB);
	if (!addr) {
		pr_err("failed to map HHEE log buffer\n");
		return -ENOMEM;
	}

	tmp_cb = kzalloc(sizeof(*tmp_cb), GFP_KERNEL);
	if (!tmp_cb) {
		memunmap(addr);
		return -ENOMEM;
	}

	tmp_cb->virt_log_addr = (unsigned long)addr;
	tmp_cb->virt_log_size = inlog_size;
	tmp_cb->logtype = logtype;
	*incb = tmp_cb;

	return 0;
}

static void cb_deinit(struct circular_buffer *incb)
{
	if (!incb)
		return;

	memunmap((void *)incb->virt_log_addr);
	kfree(incb);
}

int hhee_logger_init(void)
{
	struct arm_smccc_res res;
	int ret;

	res = hhee_fn_hvc(HHEE_LOGBUF_INFO, 0, 0, 0);
	ret = cb_init(res.a0, res.a1, &cb, NORMAL_LOG);
	if (ret)
		goto err;

	res = hhee_fn_hvc(HHEE_CRASHLOG_INFO, 0, 0, 0);
	ret = cb_init(res.a0, res.a1, &crash_cb, CRASH_LOG);
	if (ret)
		goto err;

	res = hhee_fn_hvc(HHEE_PMFBUFLOG_INFO, 0, 0, 0);
	ret = cb_init(res.a0, res.a1, &monitor_cb, MONITOR_LOG);
	if (ret)
		goto err;

	hhee_logging_enabled = true;
	return 0;

err:
	hhee_logger_exit();
	return ret;
}

void hhee_logger_exit(void)
{
	cb_deinit(crash_cb);
	cb_deinit(cb);
	cb_deinit(monitor_cb);
	cb = NULL;
	crash_cb = NULL;
	monitor_cb = NULL;
	hhee_logging_enabled = false;
}
