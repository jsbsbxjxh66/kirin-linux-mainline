/* SPDX-License-Identifier: GPL-2.0 */
/*
 * No-op replacements for the vendor's drivers/devkit/audiokit/dsm_audio/
 * dsm_audio.c.  Huawei's DSM (device state monitor) is not present in this
 * tree, so the hifi diagnostics code just drops its reports on the floor.
 */
#include <linux/export.h>
#include <linux/printk.h>

#include <dsm_audio/dsm_audio.h>

int audio_dsm_report_num(enum audio_device_type dev_type, int error_no,
			 unsigned int mesg_no)
{
	return 0;
}
EXPORT_SYMBOL(audio_dsm_report_num);

int audio_dsm_report_info(enum audio_device_type dev_type, int error_no,
			  char *fmt, ...)
{
	return 0;
}
EXPORT_SYMBOL(audio_dsm_report_info);
