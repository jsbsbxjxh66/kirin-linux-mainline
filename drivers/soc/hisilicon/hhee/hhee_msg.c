// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon HHEE message queue.
 *
 * Copyright (c) 2013-2014 NVIDIA Corporation. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/memremap.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/hisi/hisi_hhee.h>

#include "hhee.h"
#include "hhee_msg.h"

static hhee_callback_t msg_callbacks[HHEE_MSG_ID_MAX];
static struct hhee_msg_state *msg_state;

static void handle_message(struct hhee_msg *msg)
{
	unsigned int id = READ_ONCE(msg->id);
	unsigned int len = READ_ONCE(msg->len);

	if (id >= HHEE_MSG_ID_MAX || len > HHEE_MSG_MAX_PAYLOAD) {
		pr_err("unexpected msg, seq=%llu, id=%u, len=%u\n",
		       READ_ONCE(msg->seq), id, len);
		return;
	}

	if (!msg_callbacks[id]) {
		pr_err("unhandled msg, seq=%llu, id=%u, len=%u\n",
		       READ_ONCE(msg->seq), id, len);
		return;
	}

	pr_debug("process msg, seq=%llu, id=%u, len=%u\n",
		 READ_ONCE(msg->seq), id, len);

	msg_callbacks[id](len, msg->payload);
}

static void hhee_msg_handle(unsigned int start, unsigned int end)
{
	while (start != end) {
		struct hhee_msg *msg;

		msg = (struct hhee_msg *)((u8 *)msg_state +
			HHEE_MSG_SIZE * (start + 1));
		handle_message(msg);
		start++;
		if (start >= HHEE_MSG_MAX_SLOTS)
			start = 0;
	}
}

irqreturn_t hhee_irq_handle(int irq, void *data)
{
	unsigned int start, end;

	if (!msg_state)
		return IRQ_NONE;

	start = READ_ONCE(msg_state->read_pos);
	end = READ_ONCE(msg_state->write_pos);

	if (start >= HHEE_MSG_MAX_SLOTS || end >= HHEE_MSG_MAX_SLOTS) {
		pr_err("unexpected msg position, start=%u, end=%u\n",
		       start, end);
		return IRQ_HANDLED;
	}

	hhee_msg_handle(start, end);
	WRITE_ONCE(msg_state->read_pos, end);

	return IRQ_HANDLED;
}

int hhee_msg_init(void)
{
	struct arm_smccc_res res;
	void *addr;

	res = hhee_fn_hvc(HHEE_IRQ_INFOBUF, 0, 0, 0);
	if (!res.a0 || res.a1 != (HHEE_MSG_MAX_SLOTS + 1) * HHEE_MSG_SIZE) {
		pr_err("invalid HHEE message buffer\n");
		return -EFAULT;
	}

	addr = memremap(res.a0, res.a1, MEMREMAP_WB);
	if (!addr) {
		pr_err("failed to map HHEE message buffer\n");
		return -ENOMEM;
	}

	msg_state = addr;
	return 0;
}

void hhee_msg_exit(void)
{
	if (msg_state)
		memunmap(msg_state);
	msg_state = NULL;
	memset(msg_callbacks, 0, sizeof(msg_callbacks));
}

int hhee_msg_register_handler(unsigned int id, hhee_callback_t fn)
{
	if (!fn || id >= HHEE_MSG_ID_MAX)
		return -EINVAL;

	if (!msg_state)
		return -EFAULT;

	msg_callbacks[id] = fn;
	WRITE_ONCE(msg_state->enabled[id], 1);

	return 0;
}
