/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Vendor entry points the Hisilicon mntn/RDR black box driver calls that the
 * mainline kernel no longer provides.
 *
 * The consumers of everything declared here are bool Kconfig options
 * (CONFIG_HISI_BB and friends), i.e. built-in code only, so a couple of
 * otherwise-unexported VFS helpers may be used by the implementations in
 * drivers/soc/hisilicon/rdr/hisi_compat.c.
 */
#ifndef _LINUX_HISI_COMPAT_H
#define _LINUX_HISI_COMPAT_H

#include <linux/fs.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>

/*
 * fs/namei.c helper used by sys_mkdir(); it is non-static but is not
 * published in any header, so name it here.
 */


int filename_mkdirat(int dfd, struct filename *name, umode_t mode);
int filename_unlinkat(int dfd, struct filename *name);

/*
 * strncpy() was dropped from <linux/string.h> (and lib/string.c).  strscpy()
 * is not a drop-in replacement because it does not zero-pad, and several of
 * the vendor call sites rely on the padding, so keep the old behaviour.
 */
static inline char *hisi_strncpy(char *dst, const char *src, size_t count)
{
	size_t len = strnlen(src, count);

	memcpy(dst, src, len);
	memset(dst + len, '\0', count - len);

	return dst;
}

/* defined in drivers/hisi/printk/hisi_printk.c */
u64 hisi_getcurtime(void);

/*
 * The sys_* syscall entry points (and the set_fs() they depended on) were
 * removed from the kernel, and a raw fd plus a kernel buffer no longer works
 * either because access_ok() now rejects kernel addresses.  These are
 * reimplemented on top of filp_open()/kernel_read()/kernel_write(), so the
 * vendor call sites keep their original fd-based shape.
 */
long sys_open(const char *filename, int flags, umode_t mode);
int sys_close(unsigned int fd);
long sys_read(unsigned int fd, char *buf, size_t count);
long sys_write(unsigned int fd, const char *buf, size_t count);
int sys_mkdir(const char *pathname, umode_t mode);
void sys_sync(void);
long sys_unlink(const char *pathname);
long sys_lseek(unsigned int fd, long offset, unsigned int whence);
int sys_fsync(unsigned int fd);
long sys_access(const char *filename, int mode);
long sys_chown(const char *filename, uid_t user, gid_t group);
int sys_chmod(const char *filename, umode_t mode);
long sys_rmdir(const char *pathname);

/*
 * mntn_filesys.h already provides the getdents() result layout, so the
 * buffer is kept opaque here to avoid a second definition of the struct.
 */
long sys_getdents(unsigned int fd, void *dirent, unsigned int count);

long sys_getdents64(unsigned int fd, void *dirent, unsigned int count);
long sys_llseek(unsigned int fd, unsigned long offset,
		unsigned long whence, loff_t *result, unsigned long seek_whence);
long sys_rename(const char *oldname, const char *newname);
long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

/* vfs_stat() takes a __user pointer now; kernel buffers need this */
int hisi_vfs_stat(const char *name, struct kstat *stat);

int chown_common(const struct path *path, uid_t user, gid_t group);
int chmod_common(const struct path *path, umode_t mode);

/*
 * The "power + vol up + vol down = fastboot" combination keys.  The vendor
 * keeps these in include/uapi/linux/input.h, which is a mainline file this
 * port must not touch; the state itself lives in the vendor's
 * drivers/input/keyboard/hisi_gpio_key.c, which is not part of this port, so
 * the query helpers below always report "not pressed".
 */
#define VOL_DOWN_BIT			(0)
#define VOL_UP_BIT			(1)
#define VOL_UPDOWN_PRESS		((1 << VOL_DOWN_BIT) | (1 << VOL_UP_BIT))

int gpio_key_vol_updown_press_get(void);
void gpio_key_vol_updown_press_set_zero(void);
int is_gpio_key_vol_updown_pressed(void);

/* vendor block/hisi-blk-flush.S, rebuilt on the in-tree disk walk */
void blk_power_off_flush(int emergency);

/*
 * Lockup-detector state exported by the vendor's patched kernel/watchdog.c.
 * Mainline keeps that state private and has no SP805 external watchdog, so
 * these report "not this kind of lockup" and the panic notifier falls through
 * to the generic exception id.
 */
/*
 * security_initcall() never made it into mainline; the vendor definition is
 * an ordinary device_initcall() at the same level.
 */
#ifndef security_initcall
#define security_initcall(fn) device_initcall(fn)
#endif

/*
 * Vendor pstore extension used by mntn_print_to_ramconsole(); mainline has no
 * ramoops console ring, so the shim in drivers/soc/hisilicon/rdr/hisi_compat.c falls
 * back to the kernel log.
 */
void ramoops_console_write_buf(const char *buf, size_t len);

/* USB switch / PD stubs used by the charger stack. */
int usbswitch_common_dcd_timeout_status(void);
bool pd_dpm_check_cc_vbus_short(void);

bool watchdog_softlockup_happen(void);
bool watchdog_othercpu_hardlockup_happen(void);
bool watchdog_sp805_hardlockup_happen(void);

/*
 * Vendor arch/mm extensions used by the HiFi black box's firmware reload path.
 */
void __dma_unmap_area(const void *start, size_t size, int dir);
void change_secpage_range(phys_addr_t phys, unsigned long addr,
			  unsigned long size, pgprot_t prot);

#endif /* _LINUX_HISI_COMPAT_H */
