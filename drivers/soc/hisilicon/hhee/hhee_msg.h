/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2017, Huawei Limited and Contributors.
 */

#ifndef __HHEE_MSG_H__
#define __HHEE_MSG_H__

#include <linux/types.h>
#include <linux/hisi/hisi_hhee.h>

#define HHEE_MSG_SIZE		0x400
#define HHEE_MSG_MAX_SLOTS	15
#define HHEE_MSG_MAX_PAYLOAD	(HHEE_MSG_SIZE - sizeof(struct hhee_msg))

struct hhee_msg_state {
	u32 read_pos;
	u32 write_pos;
	u64 count;
	u64 lost;
	u8 enabled[HHEE_MSG_ID_MAX];
} __aligned(8);

struct hhee_msg {
	u64 seq;
	u32 id;
	u32 len;
	u8 payload[];
} __aligned(8);

#endif /* __HHEE_MSG_H__ */
