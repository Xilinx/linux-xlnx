// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: LUO File Lifecycle Bound Global Data
 *
 * File-Lifecycle-Bound (FLB) objects provide a mechanism for managing global
 * state that is shared across multiple live-updatable files. The lifecycle of
 * this shared state is tied to the preservation of the files that depend on it.
 *
 * An FLB represents a global resource, such as the IOMMU core state, that is
 * required by multiple file descriptors (e.g., all VFIO fds).
 *
 * The preservation of the FLB's state is triggered when the *first* file
 * depending on it is preserved. The cleanup of this state (unpreserve or
 * finish) is triggered when the *last* file depending on it is unpreserved or
 * finished.
 *
 * Handler Dependency: A file handler declares its dependency on one or more
 * FLBs by registering them via liveupdate_register_flb().
 *
 * Callback Model: Each FLB is defined by a set of operations
 * (&struct liveupdate_flb_ops) that LUO invokes at key points:
 *
 *     - .preserve(): Called for the first file. Saves global state.
 *     - .unpreserve(): Called for the last file (if aborted pre-reboot).
 *     - .retrieve(): Called on-demand in the new kernel to restore the state.
 *     - .finish(): Called for the last file in the new kernel for cleanup.
 *
 * This reference-counted approach ensures that shared state is saved exactly
 * once and restored exactly once, regardless of how many files depend on it,
 * and that its lifecycle is correctly managed across the kexec transition.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/libfdt.h>
#include <linux/liveupdate.h>
#include <linux/liveupdate/abi/luo.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include "luo_internal.h"

#define LUO_FLB_PGCNT		1ul
#define LUO_FLB_MAX		(((LUO_FLB_PGCNT << PAGE_SHIFT) -	\
		sizeof(struct luo_flb_head_ser)) / sizeof(struct luo_flb_ser))

struct luo_flb_head {
	struct luo_flb_head_ser *head_ser;
	struct luo_flb_ser *ser;
	bool active;
};

struct luo_flb_global {
	struct luo_flb_head incoming;
	struct luo_flb_head outgoing;
	struct list_head list;
	long count;
};

static struct luo_flb_global luo_flb_global = {
	.list = LIST_HEAD_INIT(luo_flb_global.list),
};

/*
 * struct luo_flb_link - Links an FLB definition to a file handler's internal
 * list of dependencies.
 * @flb:  A pointer to the registered &struct liveupdate_flb definition.
 * @list: The list_head for linking.
 */
struct luo_flb_link {
	struct liveupdate_flb *flb;
	struct list_head list;
};

/*
 * struct luo_flb_state - Holds the runtime state for one FLB lifecycle path.
 * @count: The number of preserved files currently depending on this FLB.
 *         This is used to trigger the preserve/unpreserve/finish ops on the
 *         first/last file.
 * @data:  The opaque u64 handle returned by .preserve() or passed to
 *         .retrieve().
 * @obj:   The live kernel object returned by .preserve() or .retrieve().
 * @lock:  A mutex that protects all fields within this structure, providing
 *         the synchronization service for the FLB's ops.
 */
struct luo_flb_state {
	long count;
	u64 data;
	void *obj;
	struct mutex lock;
};

/*
 * struct luo_flb_internal - Keep separate incoming and outgoing states.
 * @outgoing:    The runtime state for the pre-reboot (preserve/unpreserve)
 *               lifecycle.
 * @incoming:    The runtime state for the post-reboot (retrieve/finish)
 *               lifecycle.
 */
struct luo_flb_internal {
	struct luo_flb_state outgoing;
	struct luo_flb_state incoming;
};

static int luo_flb_file_preserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_internal *internal = flb->internal;

	scoped_guard(mutex, &internal->outgoing.lock) {
		if (!internal->outgoing.count) {
			struct liveupdate_flb_op_args args = {0};
			int err;

			args.flb = flb;
			err = flb->ops->preserve(&args);
			if (err)
				return err;
			internal->outgoing.data = args.data;
			internal->outgoing.obj = args.obj;
		}
		internal->outgoing.count++;
	}

	return 0;
}

static void luo_flb_file_unpreserve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_internal *internal = flb->internal;

	scoped_guard(mutex, &internal->outgoing.lock) {
		internal->outgoing.count--;
		if (!internal->outgoing.count) {
			struct liveupdate_flb_op_args args = {0};

			args.flb = flb;
			args.data = internal->outgoing.data;
			args.obj = internal->outgoing.obj;

			if (flb->ops->unpreserve)
				flb->ops->unpreserve(&args);

			internal->outgoing.data = 0;
			internal->outgoing.obj = NULL;
		}
	}
}

static int luo_flb_retrieve_one(struct liveupdate_flb *flb)
{
	struct luo_flb_head *fh = &luo_flb_global.incoming;
	struct luo_flb_internal *internal = flb->internal;
	struct liveupdate_flb_op_args args = {0};
	bool found = false;

	guard(mutex)(&internal->incoming.lock);

	if (internal->incoming.obj)
		return 0;

	if (!fh->active)
		return -ENODATA;

	for (int i = 0; i < fh->head_ser->count; i++) {
		if (!strcmp(fh->ser[i].name, flb->compatible)) {
			internal->incoming.data = fh->ser[i].data;
			internal->incoming.count = fh->ser[i].count;
			found = true;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	args.flb = flb;
	args.data = internal->incoming.data;

	flb->ops->retrieve(&args);
	internal->incoming.obj = args.obj;

	if (WARN_ON_ONCE(!internal->incoming.obj))
		return -EIO;

	return 0;
}

static void luo_flb_file_finish_one(struct liveupdate_flb *flb)
{
	struct luo_flb_internal *internal = flb->internal;
	u64 count;

	scoped_guard(mutex, &internal->incoming.lock)
		count = --internal->incoming.count;

	if (!count) {
		struct liveupdate_flb_op_args args = {0};

		if (!internal->incoming.obj) {
			int err = luo_flb_retrieve_one(flb);

			if (WARN_ON(err))
				return;
		}

		scoped_guard(mutex, &internal->incoming.lock) {
			args.flb = flb;
			args.obj = internal->incoming.obj;
			flb->ops->finish(&args);

			internal->incoming.data = 0;
			internal->incoming.obj = NULL;
		}
	}
}

/**
 * luo_flb_file_preserve - Notifies FLBs that a file is about to be preserved.
 * @h: The file handler for the preserved file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler. It increments the reference count for each FLB. If the count becomes
 * 1, it triggers the FLB's .preserve() callback to save the global state.
 *
 * This operation is atomic. If any FLB's .preserve() op fails, it will roll
 * back by calling .unpreserve() on any FLBs that were successfully preserved
 * during this call.
 *
 * Context: Called from luo_preserve_file()
 * Return: 0 on success, or a negative errno on failure.
 */
int luo_flb_file_preserve(struct liveupdate_file_handler *h)
{
	struct luo_flb_link *iter;
	int err = 0;

	list_for_each_entry(iter, &h->flb_list, list) {
		err = luo_flb_file_preserve_one(iter->flb);
		if (err)
			goto exit_err;
	}

	return 0;

exit_err:
	list_for_each_entry_continue_reverse(iter, &h->flb_list, list)
		luo_flb_file_unpreserve_one(iter->flb);

	return err;
}

/**
 * luo_flb_file_unpreserve - Notifies FLBs that a dependent file was unpreserved.
 * @h: The file handler for the unpreserved file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler, in reverse order of registration. It decrements the reference count
 * for each FLB. If the count becomes 0, it triggers the FLB's .unpreserve()
 * callback to clean up the global state.
 *
 * Context: Called when a preserved file is being cleaned up before reboot
 *          (e.g., from luo_file_unpreserve_files()).
 */
void luo_flb_file_unpreserve(struct liveupdate_file_handler *h)
{
	struct luo_flb_link *iter;

	list_for_each_entry_reverse(iter, &h->flb_list, list)
		luo_flb_file_unpreserve_one(iter->flb);
}

/**
 * luo_flb_file_finish - Notifies FLBs that a dependent file has been finished.
 * @h: The file handler for the finished file.
 *
 * This function iterates through all FLBs associated with the given file
 * handler, in reverse order of registration. It decrements the incoming
 * reference count for each FLB. If the count becomes 0, it triggers the FLB's
 * .finish() callback for final cleanup in the new kernel.
 *
 * Context: Called from luo_file_finish() for each file being finished.
 */
void luo_flb_file_finish(struct liveupdate_file_handler *h)
{
	struct luo_flb_link *iter;

	list_for_each_entry_reverse(iter, &h->flb_list, list)
		luo_flb_file_finish_one(iter->flb);
}

/**
 * liveupdate_init_flb - Initializes a liveupdate FLB structure.
 * @flb: The &struct liveupdate_flb to initialize.
 *
 * This function must be called to prepare an FLB structure before it can be
 * used with liveupdate_register_flb() or any other LUO functions.
 *
 * Context: Typically called once from a subsystem's module init function for
 *          each global FLB object that the module defines.
 *
 * Return: 0 on success, or -ENOMEM if memory allocation fails.
 */
int liveupdate_init_flb(struct liveupdate_flb *flb)
{
	struct luo_flb_internal *internal = kzalloc(sizeof(*internal),
						    GFP_KERNEL | __GFP_ZERO);

	if (!internal)
		return -ENOMEM;

	mutex_init(&internal->incoming.lock);
	mutex_init(&internal->outgoing.lock);

	flb->internal = internal;
	INIT_LIST_HEAD(&flb->list);

	return 0;
}

/**
 * liveupdate_register_flb - Associate an FLB with a file handler and register it globally.
 * @h:   The file handler that will now depend on the FLB.
 * @flb: The File-Lifecycle-Bound object to associate.
 *
 * Establishes a dependency, informing the LUO core that whenever a file of
 * type @h is preserved, the state of @flb must also be managed.
 *
 * On the first registration of a given @flb object, it is added to a global
 * registry. This function checks for duplicate registrations, both for a
 * specific handler and globally, and ensures the total number of unique
 * FLBs does not exceed the system limit.
 *
 * Context: Typically called from a subsystem's module init function after
 *          both the handler and the FLB have been defined and initialized.
 * Return: 0 on success. Returns a negative errno on failure:
 *         -EINVAL if arguments are NULL or not initialized.
 *         -ENOMEM on memory allocation failure.
 *         -EEXIST if this FLB is already registered with this handler.
 *         -ENOSPC if the maximum number of global FLBs has been reached.
 */
int liveupdate_register_flb(struct liveupdate_file_handler *h,
			    struct liveupdate_flb *flb)
{
	struct luo_flb_internal *internal = flb->internal;
	struct luo_flb_link *link __free(kfree) = NULL;
	static DEFINE_MUTEX(register_flb_lock);
	struct liveupdate_flb *gflb;
	struct luo_flb_link *iter;

	if (WARN_ON(!h || !flb || !internal))
		return -EINVAL;

	if (WARN_ON(!flb->ops->preserve || !flb->ops->unpreserve ||
		    !flb->ops->retrieve || !flb->ops->finish)) {
		return -EINVAL;
	}

	/*
	 * Once session/files have been deserialized, FLBs cannot be registered,
	 * it is too late. Deserialization uses file handlers, and FLB registers
	 * to file handlers.
	 */
	if (WARN_ON(luo_session_is_deserialized()))
		return -EBUSY;

	/*
	 * File handler must already be registered, as it is initializes the
	 * flb_list
	 */
	if (WARN_ON(list_empty(&h->list)))
		return -EINVAL;

	link = kzalloc(sizeof(*link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	guard(mutex)(&register_flb_lock);

	/* Check that this FLB is not already linked to this file handler */
	list_for_each_entry(iter, &h->flb_list, list) {
		if (iter->flb == flb)
			return -EEXIST;
	}

	/* Is this FLB linked to global list ? */
	if (list_empty(&flb->list)) {
		if (luo_flb_global.count == LUO_FLB_MAX)
			return -ENOSPC;

		/* Check that compatible string is unique in global list */
		list_for_each_entry(gflb, &luo_flb_global.list, list) {
			if (!strcmp(gflb->compatible, flb->compatible))
				return -EEXIST;
		}

		list_add_tail(&flb->list, &luo_flb_global.list);
		luo_flb_global.count++;
	}

	/* Finally, link the FLB to the file handler */
	link->flb = flb;
	list_add_tail(&no_free_ptr(link)->list, &h->flb_list);

	return 0;
}

/**
 * liveupdate_flb_incoming_locked - Lock and retrieve the incoming FLB object.
 * @flb:  The FLB definition.
 * @objp: Output parameter; will be populated with the live shared object.
 *
 * Acquires the FLB's internal lock and returns a pointer to its shared live
 * object for the incoming (post-reboot) path.
 *
 * If this is the first time the object is requested in the new kernel, this
 * function will trigger the FLB's .retrieve() callback to reconstruct the
 * object from its preserved state. Subsequent calls will return the same
 * cached object.
 *
 * The caller MUST call liveupdate_flb_incoming_unlock() to release the lock.
 *
 * Return: 0 on success, or a negative errno on failure. -ENODATA means no
 * incoming FLB data, and -ENOENT means specific flb not found in the incoming
 * data.
 */
int liveupdate_flb_incoming_locked(struct liveupdate_flb *flb, void **objp)
{
	struct luo_flb_internal *internal = flb->internal;

	if (WARN_ON(!internal))
		return -EINVAL;

	if (!internal->incoming.obj) {
		int err = luo_flb_retrieve_one(flb);

		if (err)
			return err;
	}

	mutex_lock(&internal->incoming.lock);
	*objp = internal->incoming.obj;

	return 0;
}

/**
 * liveupdate_flb_incoming_unlock - Unlock an incoming FLB object.
 * @flb: The FLB definition.
 * @obj: The object that was returned by the _locked call (used for validation).
 *
 * Releases the internal lock acquired by liveupdate_flb_incoming_locked().
 */
void liveupdate_flb_incoming_unlock(struct liveupdate_flb *flb, void *obj)
{
	struct luo_flb_internal *internal = flb->internal;

	lockdep_assert_held(&internal->incoming.lock);
	internal->incoming.obj = obj;
	mutex_unlock(&internal->incoming.lock);
}

/**
 * liveupdate_flb_outgoing_locked - Lock and retrieve the outgoing FLB object.
 * @flb:  The FLB definition.
 * @objp: Output parameter; will be populated with the live shared object.
 *
 * Acquires the FLB's internal lock and returns a pointer to its shared live
 * object for the outgoing (pre-reboot) path.
 *
 * This function assumes the object has already been created by the FLB's
 * .preserve() callback, which is triggered when the first dependent file
 * is preserved.
 *
 * The caller MUST call liveupdate_flb_outgoing_unlock() to release the lock.
 *
 * Return: 0 on success, or a negative errno on failure.
 */
int liveupdate_flb_outgoing_locked(struct liveupdate_flb *flb, void **objp)
{
	struct luo_flb_internal *internal = flb->internal;

	mutex_lock(&internal->outgoing.lock);

	/* The object must exist if any file is being preserved */
	if (WARN_ON_ONCE(!internal->outgoing.obj)) {
		mutex_unlock(&internal->outgoing.lock);
		return -ENOENT;
	}

	*objp = internal->outgoing.obj;

	return 0;
}

/**
 * liveupdate_flb_outgoing_unlock - Unlock an outgoing FLB object.
 * @flb: The FLB definition.
 * @obj: The object that was returned by the _locked call (used for validation).
 *
 * Releases the internal lock acquired by liveupdate_flb_outgoing_locked().
 */
void liveupdate_flb_outgoing_unlock(struct liveupdate_flb *flb, void *obj)
{
	struct luo_flb_internal *internal = flb->internal;

	lockdep_assert_held(&internal->outgoing.lock);
	internal->outgoing.obj = obj;
	mutex_unlock(&internal->outgoing.lock);
}

int __init luo_flb_setup_outgoing(void *fdt_out)
{
	struct luo_flb_head_ser *head_ser;
	u64 head_ser_pa;
	int err;

	head_ser = luo_alloc_preserve(LUO_FLB_PGCNT << PAGE_SHIFT);
	if (IS_ERR(head_ser))
		return PTR_ERR(head_ser);

	head_ser_pa = __pa(head_ser);

	err = fdt_begin_node(fdt_out, LUO_FDT_FLB_NODE_NAME);
	err |= fdt_property_string(fdt_out, "compatible",
				   LUO_FDT_FLB_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_FLB_HEAD, &head_ser_pa,
			    sizeof(head_ser_pa));
	err |= fdt_end_node(fdt_out);

	if (err)
		goto err_unpreserve;

	head_ser->pgcnt = LUO_FLB_PGCNT;
	luo_flb_global.outgoing.head_ser = head_ser;
	luo_flb_global.outgoing.ser = (void *)(head_ser + 1);
	luo_flb_global.outgoing.active = true;

	return 0;

err_unpreserve:
	luo_free_unpreserve(head_ser, LUO_FLB_PGCNT << PAGE_SHIFT);

	return err;
}

int __init luo_flb_setup_incoming(void *fdt_in)
{
	struct luo_flb_head_ser *head_ser;
	int err, head_size, offset;
	const void *ptr;
	u64 head_ser_pa;

	offset = fdt_subnode_offset(fdt_in, 0, LUO_FDT_FLB_NODE_NAME);
	if (offset < 0) {
		pr_err("Unable to get FLB node [%s]\n", LUO_FDT_FLB_NODE_NAME);

		return -ENOENT;
	}

	err = fdt_node_check_compatible(fdt_in, offset,
					LUO_FDT_FLB_COMPATIBLE);
	if (err) {
		pr_err("FLB node is incompatible with '%s' [%d]\n",
		       LUO_FDT_FLB_COMPATIBLE, err);

		return -EINVAL;
	}

	head_size = 0;
	ptr = fdt_getprop(fdt_in, offset, LUO_FDT_FLB_HEAD, &head_size);
	if (!ptr || head_size != sizeof(u64)) {
		pr_err("Unable to get FLB head property '%s' [%d]\n",
		       LUO_FDT_FLB_HEAD, head_size);

		return -EINVAL;
	}

	memcpy(&head_ser_pa, ptr, sizeof(u64));
	head_ser = __va(head_ser_pa);

	luo_flb_global.incoming.head_ser = head_ser;
	luo_flb_global.incoming.ser = (void *)(head_ser + 1);
	luo_flb_global.incoming.active = true;

	return 0;
}

/**
 * luo_flb_serialize - Serializes all active FLB objects for KHO.
 *
 * This function is called from the reboot path. It iterates through all
 * registered File-Lifecycle-Bound (FLB) objects. For each FLB that has been
 * preserved (i.e., its reference count is greater than zero), it writes its
 * metadata into the memory region designated for Kexec Handover.
 *
 * The serialized data includes the FLB's compatibility string, its opaque
 * data handle, and the final reference count. This allows the new kernel to
 * find the appropriate handler and reconstruct the FLB's state.
 *
 * Context: Called from liveupdate_reboot() just before kho_finalize().
 */
void luo_flb_serialize(void)
{
	struct luo_flb_head *fh = &luo_flb_global.outgoing;
	struct liveupdate_flb *flb;
	int i = 0;

	list_for_each_entry(flb, &luo_flb_global.list, list) {
		struct luo_flb_internal *internal = flb->internal;

		if (internal->outgoing.count > 0) {
			strscpy(fh->ser[i].name, flb->compatible,
				sizeof(fh->ser[i].name));
			fh->ser[i].data = internal->outgoing.data;
			fh->ser[i].count = internal->outgoing.count;
			i++;
		}
	}

	fh->head_ser->count = i;
}
