/* include/linux/wakelock.h
 *
 * Copyright (C) 2007-2012 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be distributed, used, and modified under those terms.
 *
 * Android's wake_lock wrapper.  Mainline deleted wakeup_source_init() and
 * wakeup_source_trash(), and there is no public "register this embedded
 * struct wakeup_source" helper any more (wakeup_source_add() is static in
 * drivers/base/power/wakeup.c), so the lock now holds a pointer to a
 * wakeup_source obtained from wakeup_source_register().  The call shape is
 * unchanged, which is all the vendor users care about.
 *
 */

#ifndef _LINUX_WAKELOCK_H
#define _LINUX_WAKELOCK_H

#include <linux/ktime.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>

/* A wake_lock prevents the system from entering suspend or other low power
 * states when active. If the type is set to WAKE_LOCK_SUSPEND, the wake_lock
 * prevents a full system suspend.
 */

enum {
	WAKE_LOCK_SUSPEND, /* Prevent suspend */
	WAKE_LOCK_TYPE_COUNT
};

struct wake_lock {
	struct wakeup_source *ws;
};

static inline void wake_lock_init(struct wake_lock *lock, int type,
				  const char *name)
{
	lock->ws = wakeup_source_register(NULL, name ? name : "wake_lock");
}

static inline void wake_lock_destroy(struct wake_lock *lock)
{
	wakeup_source_unregister(lock->ws);
	lock->ws = NULL;
}

static inline void wake_lock(struct wake_lock *lock)
{
	__pm_stay_awake(lock->ws);
}

static inline void wake_lock_timeout(struct wake_lock *lock, long timeout)
{
	pm_wakeup_ws_event(lock->ws, jiffies_to_msecs(timeout), false);
}

static inline void wake_unlock(struct wake_lock *lock)
{
	__pm_relax(lock->ws);
}

static inline int wake_lock_active(struct wake_lock *lock)
{
	return lock->ws && lock->ws->active;
}

#endif
