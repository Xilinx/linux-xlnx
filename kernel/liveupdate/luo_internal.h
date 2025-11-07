/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _LINUX_LUO_INTERNAL_H
#define _LINUX_LUO_INTERNAL_H

#include <linux/liveupdate.h>
#include <linux/uaccess.h>

struct luo_ucmd {
	void __user *ubuffer;
	u32 user_size;
	void *cmd;
};

static inline int luo_ucmd_respond(struct luo_ucmd *ucmd,
				   size_t kernel_cmd_size)
{
	/*
	 * Copy the minimum of what the user provided and what we actually
	 * have.
	 */
	if (copy_to_user(ucmd->ubuffer, ucmd->cmd,
			 min_t(size_t, ucmd->user_size, kernel_cmd_size))) {
		return -EFAULT;
	}
	return 0;
}

void *luo_alloc_preserve(size_t size);
void luo_free_unpreserve(void *mem, size_t size);
void luo_free_restore(void *mem, size_t size);

/**
 * struct luo_session - Represents an active or incoming Live Update session.
 * @name:       A unique name for this session, used for identification and
 *              retrieval.
 * @files_list: An ordered list of files associated with this session, it is
 *              ordered by preservation time.
 * @ser:        Pointer to the serialized data for this session.
 * @count:      A counter tracking the number of files currently stored in the
 *              @files_xa for this session.
 * @list:       A list_head member used to link this session into a global list
 *              of either outgoing (to be preserved) or incoming (restored from
 *              previous kernel) sessions.
 * @retrieved:  A boolean flag indicating whether this session has been
 *              retrieved by a consumer in the new kernel.
 * @mutex:      Session lock, protects files_list, and count.
 * @files:      The physically contiguous memory block that holds the serialized
 *              state of files.
 * @pgcnt:      The number of pages files occupy.
 */
struct luo_session {
	char name[LIVEUPDATE_SESSION_NAME_LENGTH];
	struct list_head files_list;
	struct luo_session_ser *ser;
	long count;
	struct list_head list;
	bool retrieved;
	struct mutex mutex;
	struct luo_file_ser *files;
	u64 pgcnt;
};

int luo_session_create(const char *name, struct file **filep);
int luo_session_retrieve(const char *name, struct file **filep);
int __init luo_session_setup_outgoing(void *fdt);
int __init luo_session_setup_incoming(void *fdt);
int luo_session_serialize(void);
int luo_session_deserialize(void);
bool luo_session_is_deserialized(void);

int luo_preserve_file(struct luo_session *session, u64 token, int fd);
void luo_file_unpreserve_files(struct luo_session *session);
int luo_file_freeze(struct luo_session *session);
void luo_file_unfreeze(struct luo_session *session);
int luo_retrieve_file(struct luo_session *session, u64 token,
		      struct file **filep);
int luo_file_finish(struct luo_session *session);
int luo_file_deserialize(struct luo_session *session);

int luo_flb_file_preserve(struct liveupdate_file_handler *h);
void luo_flb_file_unpreserve(struct liveupdate_file_handler *h);
void luo_flb_file_finish(struct liveupdate_file_handler *h);
int __init luo_flb_setup_outgoing(void *fdt);
int __init luo_flb_setup_incoming(void *fdt);
void luo_flb_serialize(void);

#endif /* _LINUX_LUO_INTERNAL_H */
