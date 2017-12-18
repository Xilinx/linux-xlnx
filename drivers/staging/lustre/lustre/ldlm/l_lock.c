/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LDLM
#include "../../include/linux/libcfs/libcfs.h"

#include "../include/lustre_dlm.h"
#include "../include/lustre_lib.h"

/**
 * Lock a lock and its resource.
 *
 * LDLM locking uses resource to serialize access to locks
 * but there is a case when we change resource of lock upon
 * enqueue reply. We rely on lock->l_resource = new_res
 * being an atomic operation.
 */
struct ldlm_resource *lock_res_and_lock(struct ldlm_lock *lock)
				__acquires(&lock->l_lock)
				__acquires(&lock->l_resource->lr_lock)
{
	spin_lock(&lock->l_lock);

	lock_res(lock->l_resource);

	ldlm_set_res_locked(lock);
	return lock->l_resource;
}
EXPORT_SYMBOL(lock_res_and_lock);

/**
 * Unlock a lock and its resource previously locked with lock_res_and_lock
 */
void unlock_res_and_lock(struct ldlm_lock *lock)
		__releases(&lock->l_resource->lr_lock)
		__releases(&lock->l_lock)
{
	/* on server-side resource of lock doesn't change */
	ldlm_clear_res_locked(lock);

	unlock_res(lock->l_resource);
	spin_unlock(&lock->l_lock);
}
EXPORT_SYMBOL(unlock_res_and_lock);
