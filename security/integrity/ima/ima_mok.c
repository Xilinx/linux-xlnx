/*
 * Copyright (C) 2015 Juniper Networks, Inc.
 *
 * Author:
 * Petko Manolov <petko.manolov@konsulko.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/init.h>
#include <keys/system_keyring.h>


struct key *ima_blacklist_keyring;

/*
 * Allocate the IMA blacklist keyring
 */
__init int ima_mok_init(void)
{
	pr_notice("Allocating IMA blacklist keyring.\n");

	ima_blacklist_keyring = keyring_alloc(".ima_blacklist",
				KUIDT_INIT(0), KGIDT_INIT(0), current_cred(),
				(KEY_POS_ALL & ~KEY_POS_SETATTR) |
				KEY_USR_VIEW | KEY_USR_READ |
				KEY_USR_WRITE | KEY_USR_SEARCH,
				KEY_ALLOC_NOT_IN_QUOTA,
				restrict_link_by_builtin_trusted, NULL);

	if (IS_ERR(ima_blacklist_keyring))
		panic("Can't allocate IMA blacklist keyring.");

	set_bit(KEY_FLAG_KEEP, &ima_blacklist_keyring->flags);
	return 0;
}
device_initcall(ima_mok_init);
