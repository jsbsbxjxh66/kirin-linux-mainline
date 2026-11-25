/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_MFD_HI6421V600_MNTN_H
#define __LINUX_MFD_HI6421V600_MNTN_H

/**
 * typedef hi6421v600_ocp_handler_t - PMIC OCP notifier callback
 * @power_name: name of the rail that triggered the OCP event
 *
 * Return 0 if the callback handled the event and the default PMIC OCP
 * policy should be skipped, non-zero to continue with the default policy.
 */
typedef int (*hi6421v600_ocp_handler_t)(const char *power_name);

int hi6421v600_mntn_register_ocp_handler(const char *power_name,
					 hi6421v600_ocp_handler_t handler);

#endif /* __LINUX_MFD_HI6421V600_MNTN_H */
