// SPDX-License-Identifier: GPL-2.0
/*
 * BASTET TAF entry point.
 *
 * The bastet TAF objects stay out of the build (CFG_FEATURE_BASTET is
 * FEATURE_OFF, see config/product/kirin970/config/
 * balong_product_config_gucas.mk), but the modem FID table in v_iddef.inc is
 * still compiled with FEATURE_BASTET=FEATURE_ON, so the entry point has to
 * resolve to something.
 */
#include <linux/export.h>

unsigned int BASTET_CommFidInit(unsigned int ip);

unsigned int BASTET_CommFidInit(unsigned int ip)
{
	return 0;
}
EXPORT_SYMBOL(BASTET_CommFidInit);
