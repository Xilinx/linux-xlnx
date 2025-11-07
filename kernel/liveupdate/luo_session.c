// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: LUO Sessions
 *
 * LUO Sessions provide the core mechanism for grouping and managing `struct
 * file *` instances that need to be preserved across a kexec-based live
 * update. Each session acts as a named container for a set of file objects,
 * allowing a userspace agent to manage the lifecycle of resources critical to a
 * workload.
 *
 * Core Concepts:
 *
 * - Named Containers: Sessions are identified by a unique, user-provided name,
 *   which is used for both creation in the current kernel and retrieval in the
 *   next kernel.
 *
 * - Userspace Interface: Session management is driven from userspace via
 *   ioctls on /dev/liveupdate.
 *
 * - Serialization: Session metadata is preserved using the KHO framework. When
 *   a live update is triggered via kexec, an array of `struct luo_session_ser`
 *   is populated and placed in a preserved memory region. An FDT node is also
 *   created, containing the count of sessions and the physical address of this
 *   array.
 *
 * Session Lifecycle:
 *
 * 1.  Creation: A userspace agent calls `luo_session_create()` to create a
 *     new, empty session and receives a file descriptor for it.
 *
 * 2.  Serialization: When the `reboot(LINUX_REBOOT_CMD_KEXEC)` syscall is
 *     made, `luo_session_serialize()` is called. It iterates through all
 *     active sessions and writes their metadata into a memory area preserved
 *     by KHO.
 *
 * 3.  Deserialization (in new kernel): After kexec, `luo_session_deserialize()`
 *     runs, reading the serialized data and creating a list of `struct
 *     luo_session` objects representing the preserved sessions.
 *
 * 4.  Retrieval: A userspace agent in the new kernel can then call
 *     `luo_session_retrieve()` with a session name to get a new file
 *     descriptor and access the preserved state.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/anon_inodes.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/libfdt.h>
#include <linux/liveupdate.h>
#include <linux/liveupdate/abi/luo.h>
#include <uapi/linux/liveupdate.h>
#include "luo_internal.h"

/* 16 4K pages, give space for 819 sessions */
#define LUO_SESSION_PGCNT	16ul
#define LUO_SESSION_MAX		(((LUO_SESSION_PGCNT << PAGE_SHIFT) -	\
		sizeof(struct luo_session_head_ser)) /			\
		sizeof(struct luo_session_ser))

/**
 * struct luo_session_head - Head struct for managing LUO sessions.
 * @count:    The number of sessions currently tracked in the @list.
 * @list:     The head of the linked list of `struct luo_session` instances.
 * @rwsem:    A read-write semaphore providing synchronized access to the
 *            session list and other fields in this structure.
 * @head_ser: The head data of serialization array.
 * @ser:      The serialized session data (an array of
 *            `struct luo_session_ser`).
 * @active:   Set to true when first initialized. If previous kernel did not
 *            send session data, active stays false for incoming.
 */
struct luo_session_head {
	long count;
	struct list_head list;
	struct rw_semaphore rwsem;
	struct luo_session_head_ser *head_ser;
	struct luo_session_ser *ser;
	bool active;
};

/**
 * struct luo_session_global - Global container for managing LUO sessions.
 * @incoming:     The sessions passed from the previous kernel.
 * @outgoing:     The sessions that are going to be passed to the next kernel.
 * @deserialized: The sessions have been deserialized once /dev/liveupdate
 *                has been opened.
 */
struct luo_session_global {
	struct luo_session_head incoming;
	struct luo_session_head outgoing;
	bool deserialized;
} luo_session_global;

static struct luo_session *luo_session_alloc(const char *name)
{
	struct luo_session *session = kzalloc(sizeof(*session), GFP_KERNEL);

	if (!session)
		return NULL;

	strscpy(session->name, name, sizeof(session->name));
	INIT_LIST_HEAD(&session->files_list);
	session->count = 0;
	INIT_LIST_HEAD(&session->list);
	mutex_init(&session->mutex);

	return session;
}

static void luo_session_free(struct luo_session *session)
{
	WARN_ON(session->count);
	WARN_ON(!list_empty(&session->files_list));
	mutex_destroy(&session->mutex);
	kfree(session);
}

static int luo_session_insert(struct luo_session_head *sh,
			      struct luo_session *session)
{
	struct luo_session *it;

	guard(rwsem_write)(&sh->rwsem);

	/*
	 * For outgoing we should make sure there is room in serialization array
	 * for new session.
	 */
	if (sh == &luo_session_global.outgoing) {
		if (sh->count == LUO_SESSION_MAX)
			return -ENOMEM;
	}

	/*
	 * For small number of sessions this loop won't hurt performance
	 * but if we ever start using a lot of sessions, this might
	 * become a bottle neck during deserialization time, as it would
	 * cause O(n*n) complexity.
	 */
	list_for_each_entry(it, &sh->list, list) {
		if (!strncmp(it->name, session->name, sizeof(it->name)))
			return -EEXIST;
	}
	list_add_tail(&session->list, &sh->list);
	sh->count++;

	return 0;
}

static void luo_session_remove(struct luo_session_head *sh,
			       struct luo_session *session)
{
	guard(rwsem_write)(&sh->rwsem);
	list_del(&session->list);
	sh->count--;
}

static int luo_session_finish_one(struct luo_session *session)
{
	guard(mutex)(&session->mutex);
	return luo_file_finish(session);
}

static void luo_session_unfreeze_one(struct luo_session *session)
{
	guard(mutex)(&session->mutex);
	luo_file_unfreeze(session);
}

static int luo_session_freeze_one(struct luo_session *session)
{
	guard(mutex)(&session->mutex);
	return luo_file_freeze(session);
}

static int luo_session_release(struct inode *inodep, struct file *filep)
{
	struct luo_session *session = filep->private_data;
	struct luo_session_head *sh;
	int err = 0;

	/* If retrieved is set, it means this session is from incoming list */
	if (session->retrieved) {
		sh = &luo_session_global.incoming;

		err = luo_session_finish_one(session);
		if (err) {
			pr_warn("Unable to finish session [%s] on release\n",
				session->name);
		} else {
			luo_session_remove(sh, session);
			luo_session_free(session);
		}

	} else {
		sh = &luo_session_global.outgoing;

		scoped_guard(mutex, &session->mutex)
			luo_file_unpreserve_files(session);
		luo_session_remove(sh, session);
		luo_session_free(session);
	}

	return err;
}

static int luo_session_preserve_fd(struct luo_session *session,
				   struct luo_ucmd *ucmd)
{
	struct liveupdate_session_preserve_fd *argp = ucmd->cmd;
	int err;

	guard(mutex)(&session->mutex);
	err = luo_preserve_file(session, argp->token, argp->fd);
	if (err)
		return err;

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		pr_warn("The file was successfully preserved, but response to user failed\n");

	return err;
}

static int luo_session_retrieve_fd(struct luo_session *session,
				   struct luo_ucmd *ucmd)
{
	struct liveupdate_session_retrieve_fd *argp = ucmd->cmd;
	struct file *file;
	int err;

	argp->fd = get_unused_fd_flags(O_CLOEXEC);
	if (argp->fd < 0)
		return argp->fd;

	guard(mutex)(&session->mutex);
	err = luo_retrieve_file(session, argp->token, &file);
	if (err < 0) {
		put_unused_fd(argp->fd);

		return err;
	}

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		return err;

	fd_install(argp->fd, file);

	return 0;
}

static int luo_session_finish(struct luo_session *session,
			      struct luo_ucmd *ucmd)
{
	struct liveupdate_session_finish *argp = ucmd->cmd;
	int err = luo_session_finish_one(session);

	if (err)
		return err;

	return luo_ucmd_respond(ucmd, sizeof(*argp));
}

union ucmd_buffer {
	struct liveupdate_session_finish finish;
	struct liveupdate_session_preserve_fd preserve;
	struct liveupdate_session_retrieve_fd retrieve;
};

struct luo_ioctl_op {
	unsigned int size;
	unsigned int min_size;
	unsigned int ioctl_num;
	int (*execute)(struct luo_session *session, struct luo_ucmd *ucmd);
};

#define IOCTL_OP(_ioctl, _fn, _struct, _last)                                  \
	[_IOC_NR(_ioctl) - LIVEUPDATE_CMD_SESSION_BASE] = {                    \
		.size = sizeof(_struct) +                                      \
			BUILD_BUG_ON_ZERO(sizeof(union ucmd_buffer) <          \
					  sizeof(_struct)),                    \
		.min_size = offsetofend(_struct, _last),                       \
		.ioctl_num = _ioctl,                                           \
		.execute = _fn,                                                \
	}

static const struct luo_ioctl_op luo_session_ioctl_ops[] = {
	IOCTL_OP(LIVEUPDATE_SESSION_FINISH, luo_session_finish,
		 struct liveupdate_session_finish, reserved),
	IOCTL_OP(LIVEUPDATE_SESSION_PRESERVE_FD, luo_session_preserve_fd,
		 struct liveupdate_session_preserve_fd, token),
	IOCTL_OP(LIVEUPDATE_SESSION_RETRIEVE_FD, luo_session_retrieve_fd,
		 struct liveupdate_session_retrieve_fd, token),
};

static long luo_session_ioctl(struct file *filep, unsigned int cmd,
			      unsigned long arg)
{
	struct luo_session *session = filep->private_data;
	const struct luo_ioctl_op *op;
	struct luo_ucmd ucmd = {};
	union ucmd_buffer buf;
	unsigned int nr;
	int ret;

	nr = _IOC_NR(cmd);
	if (nr < LIVEUPDATE_CMD_SESSION_BASE || (nr - LIVEUPDATE_CMD_SESSION_BASE) >=
	    ARRAY_SIZE(luo_session_ioctl_ops)) {
		return -EINVAL;
	}

	ucmd.ubuffer = (void __user *)arg;
	ret = get_user(ucmd.user_size, (u32 __user *)ucmd.ubuffer);
	if (ret)
		return ret;

	op = &luo_session_ioctl_ops[nr - LIVEUPDATE_CMD_SESSION_BASE];
	if (op->ioctl_num != cmd)
		return -ENOIOCTLCMD;
	if (ucmd.user_size < op->min_size)
		return -EINVAL;

	ucmd.cmd = &buf;
	ret = copy_struct_from_user(ucmd.cmd, op->size, ucmd.ubuffer,
				    ucmd.user_size);
	if (ret)
		return ret;

	return op->execute(session, &ucmd);
}

static const struct file_operations luo_session_fops = {
	.owner = THIS_MODULE,
	.release = luo_session_release,
	.unlocked_ioctl = luo_session_ioctl,
};

/* Create a "struct file" for session */
static int luo_session_getfile(struct luo_session *session, struct file **filep)
{
	char name_buf[128];
	struct file *file;

	guard(mutex)(&session->mutex);
	snprintf(name_buf, sizeof(name_buf), "[luo_session] %s", session->name);
	file = anon_inode_getfile(name_buf, &luo_session_fops, session, O_RDWR);
	if (IS_ERR(file))
		return PTR_ERR(file);

	*filep = file;

	return 0;
}

int luo_session_create(const char *name, struct file **filep)
{
	struct luo_session *session;
	int err;

	session = luo_session_alloc(name);
	if (!session)
		return -ENOMEM;

	err = luo_session_insert(&luo_session_global.outgoing, session);
	if (err) {
		luo_session_free(session);
		return err;
	}

	err = luo_session_getfile(session, filep);
	if (err) {
		luo_session_remove(&luo_session_global.outgoing, session);
		luo_session_free(session);
	}

	return err;
}

int luo_session_retrieve(const char *name, struct file **filep)
{
	struct luo_session_head *sh = &luo_session_global.incoming;
	struct luo_session *session = NULL;
	struct luo_session *it;
	int err;

	scoped_guard(rwsem_read, &sh->rwsem) {
		list_for_each_entry(it, &sh->list, list) {
			if (!strncmp(it->name, name, sizeof(it->name))) {
				session = it;
				break;
			}
		}
	}

	if (!session)
		return -ENOENT;

	scoped_guard(mutex, &session->mutex) {
		if (session->retrieved)
			return -EINVAL;
	}

	err = luo_session_getfile(session, filep);
	if (!err) {
		scoped_guard(mutex, &session->mutex)
			session->retrieved = true;
	}

	return err;
}

int __init luo_session_setup_outgoing(void *fdt_out)
{
	struct luo_session_head_ser *head_ser;
	u64 head_ser_pa;
	int err;

	head_ser = luo_alloc_preserve(LUO_SESSION_PGCNT << PAGE_SHIFT);
	if (IS_ERR(head_ser))
		return PTR_ERR(head_ser);
	head_ser_pa = __pa(head_ser);

	err = fdt_begin_node(fdt_out, LUO_FDT_SESSION_NODE_NAME);
	err |= fdt_property_string(fdt_out, "compatible",
				   LUO_FDT_SESSION_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_SESSION_HEAD, &head_ser_pa,
			    sizeof(head_ser_pa));
	err |= fdt_end_node(fdt_out);

	if (err)
		goto err_unpreserve;

	head_ser->pgcnt = LUO_SESSION_PGCNT;
	INIT_LIST_HEAD(&luo_session_global.outgoing.list);
	init_rwsem(&luo_session_global.outgoing.rwsem);
	luo_session_global.outgoing.head_ser = head_ser;
	luo_session_global.outgoing.ser = (void *)(head_ser + 1);
	luo_session_global.outgoing.active = true;

	return 0;

err_unpreserve:
	luo_free_unpreserve(head_ser, LUO_SESSION_PGCNT << PAGE_SHIFT);
	return err;
}

int __init luo_session_setup_incoming(void *fdt_in)
{
	struct luo_session_head_ser *head_ser;
	int err, head_size, offset;
	const void *ptr;
	u64 head_ser_pa;

	offset = fdt_subnode_offset(fdt_in, 0, LUO_FDT_SESSION_NODE_NAME);
	if (offset < 0) {
		pr_err("Unable to get session node: [%s]\n",
		       LUO_FDT_SESSION_NODE_NAME);
		return -EINVAL;
	}

	err = fdt_node_check_compatible(fdt_in, offset,
					LUO_FDT_SESSION_COMPATIBLE);
	if (err) {
		pr_err("Session node incompatibale [%s]\n",
		       LUO_FDT_SESSION_COMPATIBLE);
		return -EINVAL;
	}

	head_size = 0;
	ptr = fdt_getprop(fdt_in, offset, LUO_FDT_SESSION_HEAD, &head_size);
	if (!ptr || head_size != sizeof(u64)) {
		pr_err("Unable to get session head '%s' [%d]\n",
		       LUO_FDT_SESSION_HEAD, head_size);
		return -EINVAL;
	}

	memcpy(&head_ser_pa, ptr, sizeof(u64));
	head_ser = __va(head_ser_pa);

	luo_session_global.incoming.head_ser = head_ser;
	luo_session_global.incoming.ser = (void *)(head_ser + 1);
	INIT_LIST_HEAD(&luo_session_global.incoming.list);
	init_rwsem(&luo_session_global.incoming.rwsem);
	luo_session_global.incoming.active = true;

	return 0;
}

bool luo_session_is_deserialized(void)
{
	return luo_session_global.deserialized;
}

int luo_session_deserialize(void)
{
	struct luo_session_head *sh = &luo_session_global.incoming;

	if (luo_session_is_deserialized())
		return 0;

	luo_session_global.deserialized = true;
	if (!sh->active) {
		INIT_LIST_HEAD(&sh->list);
		init_rwsem(&sh->rwsem);
		return 0;
	}

	for (int i = 0; i < sh->head_ser->count; i++) {
		struct luo_session *session;

		session = luo_session_alloc(sh->ser[i].name);
		if (!session) {
			pr_warn("Failed to allocate session [%s] during deserialization\n",
				sh->ser[i].name);
			return -ENOMEM;
		}

		if (luo_session_insert(sh, session)) {
			pr_warn("Failed to insert session due to name conflict [%s]\n",
				session->name);
			return -EEXIST;
		}

		session->count = sh->ser[i].count;
		session->files = __va(sh->ser[i].files);
		session->pgcnt = sh->ser[i].pgcnt;
		scoped_guard(mutex, &session->mutex)
			luo_file_deserialize(session);
	}

	luo_free_restore(sh->head_ser, sh->head_ser->pgcnt << PAGE_SHIFT);
	sh->head_ser = NULL;
	sh->ser = NULL;

	return 0;
}

int luo_session_serialize(void)
{
	struct luo_session_head *sh = &luo_session_global.outgoing;
	struct luo_session *session;
	int i = 0;
	int err;

	guard(rwsem_write)(&sh->rwsem);
	list_for_each_entry(session, &sh->list, list) {
		err = luo_session_freeze_one(session);
		if (err)
			goto err_undo;

		strscpy(sh->ser[i].name, session->name,
			sizeof(sh->ser[i].name));
		sh->ser[i].count = session->count;
		sh->ser[i].files = __pa(session->files);
		sh->ser[i].pgcnt = session->pgcnt;
		i++;
	}
	sh->head_ser->count = sh->count;

	return 0;

err_undo:
	list_for_each_entry_continue_reverse(session, &sh->list, list) {
		luo_session_unfreeze_one(session);
		memset(&sh->ser[i], 0, sizeof(sh->ser[i]));
		i--;
	}

	return err;
}
