/*
 * Module and Firmware Pinning Security Module
 *
 * Copyright 2011-2016 Google Inc.
 *
 * Author: Kees Cook <keescook@chromium.org>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "LoadPin: " fmt

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/lsm_hooks.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/sched.h>	/* current */
#include <linux/string_helpers.h>

static void report_load(const char *origin, struct file *file, char *operation)
{
	char *cmdline, *pathname;

	pathname = kstrdup_quotable_file(file, GFP_KERNEL);
	cmdline = kstrdup_quotable_cmdline(current, GFP_KERNEL);

	pr_notice("%s %s obj=%s%s%s pid=%d cmdline=%s%s%s\n",
		  origin, operation,
		  (pathname && pathname[0] != '<') ? "\"" : "",
		  pathname,
		  (pathname && pathname[0] != '<') ? "\"" : "",
		  task_pid_nr(current),
		  cmdline ? "\"" : "", cmdline, cmdline ? "\"" : "");

	kfree(cmdline);
	kfree(pathname);
}

static int enabled = IS_ENABLED(CONFIG_SECURITY_LOADPIN_ENABLED);
static struct super_block *pinned_root;
static DEFINE_SPINLOCK(pinned_root_spinlock);

#ifdef CONFIG_SYSCTL
static int zero;
static int one = 1;

static struct ctl_path loadpin_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "loadpin", },
	{ }
};

static struct ctl_table loadpin_sysctl_table[] = {
	{
		.procname       = "enabled",
		.data           = &enabled,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &one,
	},
	{ }
};

/*
 * This must be called after early kernel init, since then the rootdev
 * is available.
 */
static void check_pinning_enforcement(struct super_block *mnt_sb)
{
	bool ro = false;

	/*
	 * If load pinning is not enforced via a read-only block
	 * device, allow sysctl to change modes for testing.
	 */
	if (mnt_sb->s_bdev) {
		ro = bdev_read_only(mnt_sb->s_bdev);
		pr_info("dev(%u,%u): %s\n",
			MAJOR(mnt_sb->s_bdev->bd_dev),
			MINOR(mnt_sb->s_bdev->bd_dev),
			ro ? "read-only" : "writable");
	} else
		pr_info("mnt_sb lacks block device, treating as: writable\n");

	if (!ro) {
		if (!register_sysctl_paths(loadpin_sysctl_path,
					   loadpin_sysctl_table))
			pr_notice("sysctl registration failed!\n");
		else
			pr_info("load pinning can be disabled.\n");
	} else
		pr_info("load pinning engaged.\n");
}
#else
static void check_pinning_enforcement(struct super_block *mnt_sb)
{
	pr_info("load pinning engaged.\n");
}
#endif

static void loadpin_sb_free_security(struct super_block *mnt_sb)
{
	/*
	 * When unmounting the filesystem we were using for load
	 * pinning, we acknowledge the superblock release, but make sure
	 * no other modules or firmware can be loaded.
	 */
	if (!IS_ERR_OR_NULL(pinned_root) && mnt_sb == pinned_root) {
		pinned_root = ERR_PTR(-EIO);
		pr_info("umount pinned fs: refusing further loads\n");
	}
}

static int loadpin_read_file(struct file *file, enum kernel_read_file_id id)
{
	struct super_block *load_root;
	const char *origin = kernel_read_file_id_str(id);

	/* This handles the older init_module API that has a NULL file. */
	if (!file) {
		if (!enabled) {
			report_load(origin, NULL, "old-api-pinning-ignored");
			return 0;
		}

		report_load(origin, NULL, "old-api-denied");
		return -EPERM;
	}

	load_root = file->f_path.mnt->mnt_sb;

	/* First loaded module/firmware defines the root for all others. */
	spin_lock(&pinned_root_spinlock);
	/*
	 * pinned_root is only NULL at startup. Otherwise, it is either
	 * a valid reference, or an ERR_PTR.
	 */
	if (!pinned_root) {
		pinned_root = load_root;
		/*
		 * Unlock now since it's only pinned_root we care about.
		 * In the worst case, we will (correctly) report pinning
		 * failures before we have announced that pinning is
		 * enabled. This would be purely cosmetic.
		 */
		spin_unlock(&pinned_root_spinlock);
		check_pinning_enforcement(pinned_root);
		report_load(origin, file, "pinned");
	} else {
		spin_unlock(&pinned_root_spinlock);
	}

	if (IS_ERR_OR_NULL(pinned_root) || load_root != pinned_root) {
		if (unlikely(!enabled)) {
			report_load(origin, file, "pinning-ignored");
			return 0;
		}

		report_load(origin, file, "denied");
		return -EPERM;
	}

	return 0;
}

static struct security_hook_list loadpin_hooks[] = {
	LSM_HOOK_INIT(sb_free_security, loadpin_sb_free_security),
	LSM_HOOK_INIT(kernel_read_file, loadpin_read_file),
};

void __init loadpin_add_hooks(void)
{
	pr_info("ready to pin (currently %sabled)", enabled ? "en" : "dis");
	security_add_hooks(loadpin_hooks, ARRAY_SIZE(loadpin_hooks));
}

/* Should not be mutable after boot, so not listed in sysfs (perm == 0). */
module_param(enabled, int, 0);
MODULE_PARM_DESC(enabled, "Pin module/firmware loading (default: true)");
