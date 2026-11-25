// SPDX-License-Identifier: GPL-2.0
/*
 * Compat shims for the Hisilicon mntn/RDR black box driver.
 *
 * See include/linux/hisi/hisi_compat.h for why each of these exists.  The
 * file is built-in only (every consumer is a bool Kconfig option), which is
 * what allows the unexported filename_mkdirat() helper to be called here.
 */
#include <linux/blkdev.h>
#include <linux/device/class.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/dirent.h>
#include <linux/utsname.h>
#include <linux/dma-direction.h>
#include <linux/rtc.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/mm.h>

#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/hisi/hisi_compat.h>

/* <uapi/unistd.h> is not exported to the kernel proper in this tree */
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif
#include "mntn_filesys.h"

/* fs/namei.c: non-static but not published in any header */
int filename_rmdir(int dfd, struct filename *name);

/*
 * Vendor arm64/mm/cache.S export: invalidate everything but a DMA_TO_DEVICE
 * range.  This tree's cache.S was rewritten in SYM_FUNC_START style and never
 * grew the dma_{map,unmap}_area pair, so rebuild it on dcache_inval_poc().
 */
void __dma_unmap_area(const void *start, size_t size, int dir)
{
	if (dir != DMA_TO_DEVICE)
		dcache_inval_poc((unsigned long)start,
				(unsigned long)start + size);
}
EXPORT_SYMBOL_GPL(__dma_unmap_area);

/*
 * Vendor mm/memory.c extension that rewrites the linear-map PTEs covering a
 * physical range to a given pgprot_t (PROT_DEVICE_nGnRE / PROT_NORMAL), used
 * to hand the HiFi CMA buffer over to the secure world.  Mainline publishes
 * no pgprot-taking page-table walker, and grafting the vendor's
 * change_{pte,pmd,pud}_range() ladder onto 7.2.5's folio-based mm/ is out of
 * scope for this port, so this is a no-op: the HiFi reload path still
 * allocates, loads and frees the buffer, it just does not flip the mapping
 * attributes.  Callers log the result either way.
 */
void change_secpage_range(phys_addr_t phys, unsigned long addr,
			  unsigned long size, pgprot_t prot)
{
	pr_warn_once("change_secpage_range: page attribute switch not implemented in this port, phys=%pa size=%#lx\n",
		     &phys, size);
}
EXPORT_SYMBOL_GPL(change_secpage_range);

long sys_open(const char *filename, int flags, umode_t mode)
{
	struct file *file;
	int fd;

	file = filp_open(filename, flags, mode);
	if (IS_ERR(file))
		return PTR_ERR(file);

	fd = get_unused_fd_flags(flags);
	if (fd < 0) {
		filp_close(file, NULL);
		return fd;
	}
	fd_install(fd, file);

	return fd;
}

int sys_close(unsigned int fd)
{
	return close_fd(fd);
}

long sys_read(unsigned int fd, char *buf, size_t count)
{
	struct fd f = fdget_pos(fd);
	struct file *file;
	long ret;

	if (fd_empty(f))
		return -EBADF;

	file = fd_file(f);
	ret = kernel_read(file, buf, count, &file->f_pos);
	fdput_pos(f);

	return ret;
}

long sys_write(unsigned int fd, const char *buf, size_t count)
{
	struct fd f = fdget_pos(fd);
	struct file *file;
	long ret;

	if (fd_empty(f))
		return -EBADF;

	file = fd_file(f);
	ret = kernel_write(file, buf, count, &file->f_pos);
	fdput_pos(f);

	return ret;
}

int sys_mkdir(const char *pathname, umode_t mode)
{
	struct filename *name;
	int ret;

	name = getname_kernel(pathname);
	if (IS_ERR(name))
		return PTR_ERR(name);

	ret = filename_mkdirat(AT_FDCWD, name, mode);
	putname(name);

	return ret;
}

void sys_sync(void)
{
	ksys_sync();
}

long sys_unlink(const char *pathname)
{
	struct filename *name;
	int ret;

	name = getname_kernel(pathname);
	if (IS_ERR(name))
		return PTR_ERR(name);

	ret = filename_unlinkat(AT_FDCWD, name);
	putname(name);

	return ret;
}

long sys_lseek(unsigned int fd, long offset, unsigned int whence)
{
	CLASS(fd_pos, f)(fd);

	if (fd_empty(f))
		return -EBADF;
	if (whence > SEEK_MAX)
		return -EINVAL;

	return vfs_llseek(fd_file(f), offset, whence);
}

void ramoops_console_write_buf(const char *buf, size_t len)
{
	printk(KERN_DEFAULT "%.*s", (int)len, buf);
}

/*
 * Vendor init/version.c helper: extract the build date/time from linux_banner
 * ("... (#11 SMP PREEMPT Mon Oct  5 03:54:03 UTC 2026)").  Ported verbatim so
 * that init/version.c, a mainline file, stays untouched.
 */
int get_kernel_build_time(char *blddt, int dtlen, char *bldtm, int tmlen);
int get_kernel_build_time(char *blddt, int dtlen, char *bldtm, int tmlen)
{
	static const char *const str_mon[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	enum { MONSTR = 1, DAYSTR = 2, TIMESTR = 3, STYLESTR = 4 };
	const int monthcount = 12, datetime_len = 23, year_len = 4;
	const int mon_len = 3, max_day_len = 2;
	char *timepos, *tmpstr, *tmpdt;
	int i, dstlen = 0, ret = -EINVAL;

	/* buffers must hold "XXX xx 2XXX" and "hh:mm:ss" plus the terminator */
	if (blddt == NULL || bldtm == NULL || dtlen < 11 || tmlen < 8)
		return ret;

	timepos = strrchr(linux_banner, ')');
	if (timepos == NULL)
		return ret;

	for (i = 0; i < monthcount; i++) {
		int subindex, getinval;

		tmpstr = strstr(timepos, str_mon[i]);
		if (tmpstr == NULL || strlen(tmpstr) < datetime_len)
			continue;

		tmpdt = strstr(tmpstr, " ");
		subindex = 0;
		getinval = 0;
		while (tmpdt != NULL) {
			int getstrlen = (int)(tmpdt - tmpstr);

			subindex++;
			switch (subindex) {
			case MONSTR:
				if (getstrlen > mon_len + 1) {
					getinval = -EINVAL;
					break;
				}
				memset(blddt, 0, dtlen);
				memcpy(blddt, tmpstr, getstrlen + 1);
				dstlen = getstrlen + 1;
				break;
			case DAYSTR:
				if (getstrlen > max_day_len + 1) {
					getinval = -EINVAL;
					break;
				}
				memcpy(blddt + dstlen, tmpstr, getstrlen + 1);
				dstlen += getstrlen + 1;
				break;
			case TIMESTR:
				if (getstrlen > tmlen) {
					getinval = -EINVAL;
					break;
				}
				memset(bldtm, 0, tmlen);
				memcpy(bldtm, tmpstr, getstrlen);
				break;
			case STYLESTR:
				break;
			default:
				break;
			}

			while (*tmpdt == ' ')
				tmpdt++;
			tmpstr = tmpdt;
			tmpdt = strstr(tmpstr, " ");
			if (subindex >= STYLESTR) {
				if (strlen(tmpstr) >= year_len) {
					memcpy(blddt + dstlen, tmpstr, year_len);
					ret = 0;
				}
				break;
			}
			if (getinval != 0)
				break;
		}
		break;
	}

	return ret;
}

long sys_access(const char *filename, int mode)
{
	struct path path;
	int mask = 0;
	int ret;

	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;

	if (mode & R_OK)
		mask |= MAY_READ;
	if (mode & W_OK)
		mask |= MAY_WRITE;
	if (mode & X_OK)
		mask |= MAY_EXEC;

	ret = inode_permission(mnt_idmap(path.mnt), path.dentry->d_inode, mask);
	path_put(&path);

	return ret;
}

long sys_chown(const char *filename, uid_t user, gid_t group)
{
	struct path path;
	int ret;

	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;

	ret = mnt_want_write(path.mnt);
	if (!ret) {
		ret = chown_common(&path, user, group);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);

	return ret;
}

int sys_chmod(const char *filename, umode_t mode)
{
	struct path path;
	int ret;

	ret = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (ret)
		return ret;

	ret = mnt_want_write(path.mnt);
	if (!ret) {
		ret = chmod_common(&path, mode);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);

	return ret;
}

long sys_rmdir(const char *pathname)
{
	struct filename *name;
	int ret;

	name = getname_kernel(pathname);
	if (IS_ERR(name))
		return PTR_ERR(name);

	ret = filename_rmdir(AT_FDCWD, name);
	putname(name);

	return ret;
}

struct hisi_getdents_ctx {
	struct dir_context ctx;
	char *buf;
	unsigned int used;
	int error;
};

static bool hisi_filldir(struct dir_context *ctx, const char *name, int namlen,
			 loff_t offset, u64 ino, unsigned d_type)
{
	struct hisi_getdents_ctx *c = container_of(ctx,
						   struct hisi_getdents_ctx,
						   ctx);
	struct linux_dirent *d = (struct linux_dirent *)(c->buf + c->used);
	int reclen = ALIGN(offsetof(struct linux_dirent, d_name) + namlen + 1,
			   sizeof(long));

	if (c->used + reclen > c->ctx.count) {
		c->error = -EINVAL;
		return false;
	}

	d->d_ino = ino;
	d->d_off = offset;
	d->d_reclen = reclen;
	memcpy(d->d_name, name, namlen);
	d->d_name[namlen] = '\0';
	c->used += reclen;

	return true;
}

/*
 * sys_getdents64() shares the machinery above; only the record layout differs.
 */
static bool hisi_filldir64(struct dir_context *ctx, const char *name, int namlen,
			   loff_t offset, u64 ino, unsigned d_type)
{
	struct hisi_getdents_ctx *c = container_of(ctx,
						   struct hisi_getdents_ctx,
						   ctx);
	struct linux_dirent64 *d = (struct linux_dirent64 *)(c->buf + c->used);
	int reclen = ALIGN(offsetof(struct linux_dirent64, d_name) + namlen + 1,
			   sizeof(u64));

	if (c->used + reclen > c->ctx.count) {
		c->error = -EINVAL;
		return false;
	}

	d->d_ino = ino;
	d->d_off = offset;
	d->d_reclen = reclen;
	d->d_type = d_type;
	memcpy(d->d_name, name, namlen);
	d->d_name[namlen] = '\0';
	c->used += reclen;

	return true;
}


long sys_getdents(unsigned int fd, void *dirent,
		  unsigned int count)
{
	struct hisi_getdents_ctx ctx = {
		.ctx = { .actor = hisi_filldir, .pos = 0, .count = count },
		.buf = (char *)dirent,
	};
	struct fd f = fdget(fd);
	long ret;

	if (fd_empty(f))
		return -EBADF;

	ret = iterate_dir(fd_file(f), &ctx.ctx);
	if (!ret)
		ret = ctx.used;
	if (!ret && ctx.error)
		ret = ctx.error;

	fdput(f);

	return ret;
}


/*
 * The remaining sys_* entry points the rfile/block adp layer needs.  None of
 * them can go through a __user copy loop any more: set_fs() is gone, and every
 * vendor call site hands us a kernel address.
 */
int filename_renameat2(int olddfd, struct filename *from,
		       int newdfd, struct filename *to, unsigned int flags);

long sys_getdents64(unsigned int fd, void *dirent, unsigned int count)
{
	struct hisi_getdents_ctx ctx = {
		.ctx = { .actor = hisi_filldir64, .pos = 0, .count = count },
		.buf = (char *)dirent,
	};
	struct fd f = fdget(fd);
	long ret;

	if (fd_empty(f))
		return -EBADF;

	ret = iterate_dir(fd_file(f), &ctx.ctx);
	if (!ret)
		ret = ctx.used;
	if (!ret && ctx.error)
		ret = ctx.error;

	fdput(f);

	return ret;
}

long sys_llseek(unsigned int fd, unsigned long offset, unsigned long whence,
		loff_t *result, unsigned long seek_whence)
{
	CLASS(fd_pos, f)(fd);
	loff_t ret;

	if (fd_empty(f))
		return -EBADF;
	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
		return -EINVAL;

	ret = vfs_llseek(fd_file(f), (loff_t)offset, (int)seek_whence);
	if (ret < 0)
		return ret;

	*result = ret;

	return 0;
}

long sys_rename(const char *oldname, const char *newname)
{
	struct filename *from, *to;
	int ret;

	from = getname_kernel(oldname);
	if (IS_ERR(from))
		return PTR_ERR(from);

	to = getname_kernel(newname);
	if (IS_ERR(to)) {
		ret = PTR_ERR(to);
		putname(from);
		return ret;
	}

	ret = filename_renameat2(AT_FDCWD, from, AT_FDCWD, to, 0);
	putname(from);
	putname(to);

	return ret;
}

/*
 * BLKGETSIZE64 is the only ioctl the vendor uses, and it is implemented as a
 * put_user() of the device size -- which now rejects the kernel address the
 * vendor passes, because set_fs() no longer widens the user range.
 */
long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct fd f = fdget(fd);
	long ret;

	if (fd_empty(f))
		return -EBADF;

	if (cmd == BLKGETSIZE64) {
		*(u64 *)arg = (u64)bdev_nr_bytes(file_bdev(fd_file(f)));
		fdput(f);
		return 0;
	}

	ret = security_file_ioctl(fd_file(f), cmd, arg);
	if (!ret) {
		if (fd_file(f)->f_op->unlocked_ioctl)
			ret = fd_file(f)->f_op->unlocked_ioctl(fd_file(f), cmd, arg);
		else
			ret = -ENOTTY;
		if (ret == -ENOIOCTLCMD)
			ret = -ENOTTY;
	}
	fdput(f);

	return ret;
}

/*
 * vfs_stat() is a __user API now, and the rfile call site passes a kernel
 * buffer that access_ok() will reject, so go through kern_path() instead.
 */
int hisi_vfs_stat(const char *name, struct kstat *stat)
{
	struct path path;
	int error;

	error = kern_path(name, LOOKUP_FOLLOW, &path);
	if (error)
		return error;

	error = vfs_getattr(&path, stat, STATX_BASIC_STATS, AT_STATX_SYNC_AS_STAT);
	path_put(&path);

	return error;
}

int sys_fsync(unsigned int fd)
{
	struct fd f = fdget(fd);
	int ret;

	if (fd_empty(f))
		return -EBADF;

	ret = vfs_fsync(fd_file(f), 0);
	fdput(f);

	return ret;
}

static int blk_flush_one(struct device *dev, void *data)
{
	struct gendisk *disk = dev_to_disk(dev);

	if (disk && disk->part0)
		blkdev_issue_flush(disk->part0);

	return 0;
}

void blk_power_off_flush(int emergency)
{
	class_for_each_device(&block_class, NULL, NULL, blk_flush_one);
}

int gpio_key_vol_updown_press_get(void)
{
	return 0;
}

void gpio_key_vol_updown_press_set_zero(void)
{
}

int is_gpio_key_vol_updown_pressed(void)
{
	return 0;
}

bool watchdog_softlockup_happen(void)
{
	return false;
}

bool watchdog_othercpu_hardlockup_happen(void)
{
	return false;
}

bool watchdog_sp805_hardlockup_happen(void)
{
	return false;
}

/*
 * staging/android/alarm-dev.c is gone from mainline; rtc-hisi-pmic-spmi.c
 * still expects this global (it restores the poweroff charge alarm window
 * while probing).  alarm-dev.c used to zero-initialise it, so with no
 * Android alarm client around it always stays disabled.
 */
struct rtc_wkalrm poweroff_rtc_alarm;

int usbswitch_common_dcd_timeout_status(void)
{
	return 0;
}

bool pd_dpm_check_cc_vbus_short(void)
{
	return false;
}

