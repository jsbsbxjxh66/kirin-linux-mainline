// SPDX-License-Identifier: GPL-2.0
/*
 * Vendor entry points that have no implementation in this port.
 *
 * Three unrelated subsystems are referenced by code we do keep (hz driver
 * telemetry, DisplayPort audio hand-off and the secure-SPI fast path), but
 * none of their real implementations belong here:
 *
 *   imonitor_*        Huawei's hiview telemetry pipeline (drivers/log/ in the
 *                     vendor tree) - pure diagnostic reporting.
 *   hisi_dptx_*       DisplayPort EDID/audio hand-off from the DSS driver
 *                     (drivers/video/fbdev/hisi/dss/hisi_dp.c).
 *   spi_*_secos       The vendor PL022 secure-mode helper that is only needed
 *                     when a secure element hangs off the SoC SPI bus.
 *
 * Every consumer is written to tolerate the "nothing to report" answers
 * below, so the honest response is an error or a no-op.
 */
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/string.h>

#include <huawei_platform/log/imonitor.h>

/*
 * Prototypes for the declarations below.  The consumers reach these through
 * their own vendor headers (or, for the modem FID, through v_id.h), none of
 * which this file can include, so restate them here to satisfy
 * -Wmissing-prototypes.
 */
struct imonitor_eventobj *imonitor_create_eventobj(unsigned int eventid);
int imonitor_set_param(struct imonitor_eventobj *eventobj,
		       unsigned short paramid, long value);
int imonitor_set_param_integer_v2(struct imonitor_eventobj *eventobj,
				  const char *param, long value);
int imonitor_set_param_string_v2(struct imonitor_eventobj *eventobj,
				 const char *param, const char *value);
int imonitor_add_dynamic_path(struct imonitor_eventobj *eventobj,
			      const char *path);
int imonitor_send_event(struct imonitor_eventobj *eventobj);
void imonitor_destroy_eventobj(struct imonitor_eventobj *eventobj);
int hisi_dptx_get_spec(void *data, unsigned int size, unsigned int *ext_acount);
int hisi_dptx_set_aparam(unsigned int channel_num, unsigned int data_width,
			 unsigned int sample_rate);
int spi_init_secos(unsigned int spi_bus_id);
int spi_exit_secos(unsigned int spi_bus_id);

/*
 * imonitor: the event object is opaque, so returning NULL is safe - the
 * setting/sending entry points ignore NULL rather than dereferencing it.
 */
struct imonitor_eventobj *imonitor_create_eventobj(unsigned int eventid)
{
	return NULL;
}
EXPORT_SYMBOL_GPL(imonitor_create_eventobj);

int imonitor_set_param(struct imonitor_eventobj *eventobj,
		       unsigned short paramid, long value)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(imonitor_set_param);

int imonitor_set_param_integer_v2(struct imonitor_eventobj *eventobj,
				  const char *param, long value)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(imonitor_set_param_integer_v2);

int imonitor_set_param_string_v2(struct imonitor_eventobj *eventobj,
				 const char *param, const char *value)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(imonitor_set_param_string_v2);

int imonitor_add_dynamic_path(struct imonitor_eventobj *eventobj,
			      const char *path)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(imonitor_add_dynamic_path);

int imonitor_send_event(struct imonitor_eventobj *eventobj)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(imonitor_send_event);

void imonitor_destroy_eventobj(struct imonitor_eventobj *eventobj)
{
}
EXPORT_SYMBOL_GPL(imonitor_destroy_eventobj);

/* DisplayPort audio: no DP transmitter is driven by this port. */
int hisi_dptx_get_spec(void *data, unsigned int size, unsigned int *ext_acount)
{
	if (data)
		memset(data, 0, size);
	if (ext_acount)
		*ext_acount = 0;
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(hisi_dptx_get_spec);

int hisi_dptx_set_aparam(unsigned int channel_num, unsigned int data_width,
			 unsigned int sample_rate)
{
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(hisi_dptx_set_aparam);

/*
 * Secure SPI bus setup.  There is no secure element on this board's SPI
 * buses, so "nothing to bring up" and "nothing to tear down" both succeed.
 */
int spi_init_secos(unsigned int spi_bus_id)
{
	return 0;
}
EXPORT_SYMBOL_GPL(spi_init_secos);

int spi_exit_secos(unsigned int spi_bus_id)
{
	return 0;
}
EXPORT_SYMBOL_GPL(spi_exit_secos);
