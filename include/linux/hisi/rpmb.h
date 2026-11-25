/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HISI_RPMB_H__
#define __HISI_RPMB_H__

#include <linux/mutex.h>
#include <linux/types.h>

#define STORAGE_IOC_MAX_RPMB_CMD	3

enum rpmb_op_type {
	RPMB_OP_RD = 0,
	RPMB_OP_WR_DATA = 1,
	RPMB_OP_WR_CNT = 2,
};

enum func_id {
	RPMB_FUNC_ID_RESERVED,
	RPMB_FUNC_ID_SE,
	RPMB_FUNC_ID_SECURE_OS,
	RPMB_FUNC_ID_MAX,
};

enum rpmb_version {
	RPMB_VER_INVALID = 0,
	RPMB_VER_MMC_51,
	RPMB_VER_UFS_20,
	RPMB_VER_UFS_21,
	RPMB_VER_UFS_30,
	RPMB_VER_MAX,
};

struct storage_blk_ioc_data {
	unsigned char *buf;
	u64 buf_bytes;
	u32 blocks;
};

struct storage_blk_ioc_rpmb_data {
	struct storage_blk_ioc_data data[STORAGE_IOC_MAX_RPMB_CMD];
};

int hisi_rpmb_ioctl_cmd(enum func_id id, enum rpmb_op_type operation,
			struct storage_blk_ioc_rpmb_data *storage_data);

extern struct mutex rpmb_counter_lock;
void hisi_rpmb_active(void);
int rpmb_get_dev_ver(enum rpmb_version *ver);
int get_rpmb_key_status(void);
u32 get_rpmb_support_key_num(void);
int wait_hisee_rpmb_request_is_finished(void);
int check_hisee_rpmb_request_is_cleaned(void);
int32_t hisee_exception_to_reset_rpmb(void);

#endif /* __HISI_RPMB_H__ */
