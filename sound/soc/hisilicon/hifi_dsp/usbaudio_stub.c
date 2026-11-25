/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stand-in for Huawei's sound/usb/hifi/usbaudio_ctrl.c.  The real one pulls in
 * hisi_usb.h and the huawei_platform usb_audio_power helpers, none of which are
 * part of this port; the hifi diagnostics code only asks these four questions
 * about the USB audio gadget, so answer them with "nothing to report".
 */
#include <linux/string.h>

#include "usbaudio_ioctl.h"

void usbaudio_ctrl_query_info(struct usbaudio_info *usbinfo)
{
	if (usbinfo)
		memset(usbinfo, 0, sizeof(*usbinfo));
}

int usbaudio_ctrl_usb_resume(void)
{
	return 0;
}

void usbaudio_ctrl_hifi_reset_inform(void)
{
}

void usbaudio_set_nv_ready(void)
{
}
