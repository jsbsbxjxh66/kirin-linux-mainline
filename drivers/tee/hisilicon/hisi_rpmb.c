// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HiSilicon secure RPMB proxy.
 *
 * The secure OS speaks a three-command RPMB protocol through the TEE agent.
 * Translate that protocol to the generic in-kernel RPMB interface so the
 * mainline MMC/UFS RPMB implementations own the actual frame transport.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rpmb.h>
#include <linux/types.h>

#include <linux/hisi/rpmb.h>

/* Serializes RPMB requests coming from the secure OS. */
DEFINE_MUTEX(rpmb_counter_lock);

static int hisi_rpmb_match(struct device *dev, const void *data)
{
	struct rpmb_dev *rdev = to_rpmb_dev(dev);

	return rdev->descr.type == *(const enum rpmb_type *)data;
}

static struct rpmb_dev *hisi_rpmb_find(void)
{
	enum rpmb_type type = RPMB_TYPE_UFS;
	struct rpmb_dev *rdev;

	rdev = rpmb_dev_find_device(&type, NULL, hisi_rpmb_match);
	if (rdev)
		return rdev;

	type = RPMB_TYPE_EMMC;
	return rpmb_dev_find_device(&type, NULL, hisi_rpmb_match);
}

static int hisi_rpmb_get_frames(enum rpmb_op_type operation,
				struct storage_blk_ioc_rpmb_data *storage_data,
				u8 **req, unsigned int *req_len,
				u8 **resp, unsigned int *resp_len)
{
	struct storage_blk_ioc_data *request = &storage_data->data[0];
	struct storage_blk_ioc_data *response;

	if (!request->buf || !request->buf_bytes)
		return -EINVAL;

	if (request->buf_bytes > UINT_MAX)
		return -EINVAL;

	switch (operation) {
	case RPMB_OP_RD:
	case RPMB_OP_WR_CNT:
		response = &storage_data->data[1];
		break;
	case RPMB_OP_WR_DATA:
		response = &storage_data->data[2];
		break;
	default:
		return -EINVAL;
	}

	if (!response->buf || !response->buf_bytes ||
	    response->buf_bytes > UINT_MAX)
		return -EINVAL;

	*req = request->buf;
	*req_len = request->buf_bytes;
	*resp = response->buf;
	*resp_len = response->buf_bytes;

	return 0;
}

int hisi_rpmb_ioctl_cmd(enum func_id id, enum rpmb_op_type operation,
			struct storage_blk_ioc_rpmb_data *storage_data)
{
	struct rpmb_dev *rdev;
	u8 *req, *resp;
	unsigned int req_len, resp_len;
	int ret;

	if (!storage_data)
		return -EINVAL;

	ret = hisi_rpmb_get_frames(operation, storage_data, &req, &req_len,
				   &resp, &resp_len);
	if (ret)
		return ret;

	rdev = hisi_rpmb_find();
	if (!rdev)
		return -ENODEV;

	ret = rpmb_route_frames(rdev, req, req_len, resp, resp_len);
	rpmb_dev_put(rdev);

	return ret;
}
EXPORT_SYMBOL_GPL(hisi_rpmb_ioctl_cmd);

void hisi_rpmb_active(void)
{
}
EXPORT_SYMBOL_GPL(hisi_rpmb_active);

int rpmb_get_dev_ver(enum rpmb_version *ver)
{
	struct rpmb_dev *rdev;
	int ret = 0;

	if (!ver)
		return -EINVAL;

	rdev = hisi_rpmb_find();
	if (!rdev)
		return -ENODEV;

	switch (rdev->descr.type) {
	case RPMB_TYPE_EMMC:
		*ver = RPMB_VER_MMC_51;
		break;
	case RPMB_TYPE_UFS:
		*ver = RPMB_VER_UFS_21;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	rpmb_dev_put(rdev);
	return ret;
}
EXPORT_SYMBOL_GPL(rpmb_get_dev_ver);

int get_rpmb_key_status(void)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(get_rpmb_key_status);

u32 get_rpmb_support_key_num(void)
{
	struct rpmb_dev *rdev = hisi_rpmb_find();

	if (!rdev)
		return 0;

	rpmb_dev_put(rdev);
	return 1;
}
EXPORT_SYMBOL_GPL(get_rpmb_support_key_num);

int wait_hisee_rpmb_request_is_finished(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(wait_hisee_rpmb_request_is_finished);

int check_hisee_rpmb_request_is_cleaned(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(check_hisee_rpmb_request_is_cleaned);

int32_t hisee_exception_to_reset_rpmb(void)
{
	return 0;
}
EXPORT_SYMBOL_GPL(hisee_exception_to_reset_rpmb);
