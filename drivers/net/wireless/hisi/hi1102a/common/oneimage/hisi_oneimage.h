

#ifndef __HISI__ONEIMAGE_H__
#define __HISI__ONEIMAGE_H__

#include <linux/version.h>
/* Always pull in the Huawei connectivity headers: this tree ships them under
 * <huawei_platform/...> but has no CONFIG_HWCONNECTIVITY, so gating on it left
 * CHIP_TYPE_HI110X / gps_*_enum / isMyConnectivityChip unresolved.  Both
 * headers self-stub when CONFIG_HWCONNECTIVITY is off. */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0))
#include <linux/huawei/hw_connectivity.h>
#include <linux/huawei/gps/huawei_gps.h>
#else
#include <huawei_platform/connectivity/hw_connectivity.h>
#include <huawei_platform/connectivity/huawei_gps.h>
#endif

#endif
