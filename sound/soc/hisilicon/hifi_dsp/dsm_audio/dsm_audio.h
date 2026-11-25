/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal stand-in for Huawei's drivers/devkit/audiokit/dsm_audio/dsm_audio.h.
 * The vendor header hides everything behind CONFIG_HUAWEI_DSM, which this tree
 * does not carry (there is no drivers/dsm/ here), so the reporting entry points
 * are always declared and always resolve to the no-ops in dsm_audio.c.
 */
#ifndef __DSM_AUDIO_H__
#define __DSM_AUDIO_H__

#include <linux/types.h>
#include <dsm/dsm_pub.h>

enum audio_device_type {
	AUDIO_CODEC = 0,
	AUDIO_SMARTPA,
	AUDIO_ANC_HS,
	AUDIO_DEVICE_MAX
};

int audio_dsm_report_num(enum audio_device_type device_type, int error_no,
			 unsigned int mesg_no);
int audio_dsm_report_info(enum audio_device_type device_type, int error_no,
			  char *fmt, ...);

#define DSM_AUDIO_NAME			"dsm_audio_info"

#define ERROR_LEVEL	 1
#define INFO_LEVEL	 1
#define DEBUG_LEVEL	 0

#if INFO_LEVEL
#define dsm_logi(fmt, ...) pr_info(DSM_AUDIO_NAME"[I]:%s:%d: "fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define dsm_logi(fmt, ...)
#endif

#if DEBUG_LEVEL
#define dsm_logd(fmt, ...) pr_info(DSM_AUDIO_NAME"[D]:%s:%d: "fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define dsm_logd(fmt, ...)
#endif

#if ERROR_LEVEL
#define dsm_loge(fmt, ...) pr_err(DSM_AUDIO_NAME"[E]:%s:%d: "fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define dsm_loge(fmt, ...)
#endif

#endif /* __DSM_AUDIO_H__ */
