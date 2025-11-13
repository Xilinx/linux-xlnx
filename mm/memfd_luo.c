// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 *
 * Copyright (C) 2025 Amazon.com Inc. or its affiliates.
 * Pratyush Yadav <ptyadav@amazon.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/file.h>
#include <linux/io.h>
#include <linux/libfdt.h>
#include <linux/liveupdate.h>
#include <linux/liveupdate/abi/memfd.h>
#include <linux/kexec_handover.h>
#include <linux/shmem_fs.h>
#include <linux/bits.h>
#include <linux/vmalloc.h>
#include "internal.h"

#define PRESERVED_PFN_MASK		GENMASK(63, 12)
#define PRESERVED_PFN_SHIFT		12
#define PRESERVED_FLAG_DIRTY		BIT(0)
#define PRESERVED_FLAG_UPTODATE		BIT(1)

#define PRESERVED_FOLIO_PFN(desc)	(((desc) & PRESERVED_PFN_MASK) >> PRESERVED_PFN_SHIFT)
#define PRESERVED_FOLIO_FLAGS(desc)	((desc) & ~PRESERVED_PFN_MASK)
#define PRESERVED_FOLIO_MKDESC(pfn, flags) (((pfn) << PRESERVED_PFN_SHIFT) | (flags))

struct memfd_luo_private {
	struct memfd_luo_folio_ser *pfolios;
	u64 nr_folios;
};

static struct memfd_luo_folio_ser *memfd_luo_preserve_folios(struct file *file, void *fdt,
							     u64 *nr_foliosp)
{
	struct inode *inode = file_inode(file);
	struct memfd_luo_folio_ser *pfolios;
	struct kho_vmalloc *kho_vmalloc;
	unsigned int max_folios;
	long i, size, nr_pinned;
	struct folio **folios;
	int err = -EINVAL;
	pgoff_t offset;
	u64 nr_folios;

	size = i_size_read(inode);
	/*
	 * If the file has zero size, then the folios and nr_folios properties
	 * are not set.
	 */
	if (!size) {
		*nr_foliosp = 0;
		return NULL;
	}

	/*
	 * Guess the number of folios based on inode size. Real number might end
	 * up being smaller if there are higher order folios.
	 */
	max_folios = PAGE_ALIGN(size) / PAGE_SIZE;
	folios = kvmalloc_array(max_folios, sizeof(*folios), GFP_KERNEL);
	if (!folios)
		return ERR_PTR(-ENOMEM);

	/*
	 * Pin the folios so they don't move around behind our back. This also
	 * ensures none of the folios are in CMA -- which ensures they don't
	 * fall in KHO scratch memory. It also moves swapped out folios back to
	 * memory.
	 *
	 * A side effect of doing this is that it allocates a folio for all
	 * indices in the file. This might waste memory on sparse memfds. If
	 * that is really a problem in the future, we can have a
	 * memfd_pin_folios() variant that does not allocate a page on empty
	 * slots.
	 */
	nr_pinned = memfd_pin_folios(file, 0, size - 1, folios, max_folios,
				     &offset);
	if (nr_pinned < 0) {
		err = nr_pinned;
		pr_err("failed to pin folios: %d\n", err);
		goto err_free_folios;
	}
	nr_folios = nr_pinned;

	err = fdt_property(fdt, MEMFD_FDT_NR_FOLIOS, &nr_folios, sizeof(nr_folios));
	if (err)
		goto err_unpin;

	err = fdt_property_placeholder(fdt, MEMFD_FDT_FOLIOS, sizeof(*kho_vmalloc),
				       (void **)&kho_vmalloc);
	if (err) {
		pr_err("Failed to reserve '%s' property in FDT: %s\n",
		       MEMFD_FDT_FOLIOS, fdt_strerror(err));
		err = -ENOMEM;
		goto err_unpin;
	}

	pfolios = vcalloc(nr_folios, sizeof(*pfolios));
	if (!pfolios) {
		err = -ENOMEM;
		goto err_unpin;
	}

	for (i = 0; i < nr_folios; i++) {
		struct memfd_luo_folio_ser *pfolio = &pfolios[i];
		struct folio *folio = folios[i];
		unsigned int flags = 0;
		unsigned long pfn;

		err = kho_preserve_folio(folio);
		if (err)
			goto err_unpreserve;

		pfn = folio_pfn(folio);
		if (folio_test_dirty(folio))
			flags |= PRESERVED_FLAG_DIRTY;
		if (folio_test_uptodate(folio))
			flags |= PRESERVED_FLAG_UPTODATE;

		pfolio->foliodesc = PRESERVED_FOLIO_MKDESC(pfn, flags);
		pfolio->index = folio->index;
	}

	err = kho_preserve_vmalloc(pfolios, kho_vmalloc);
	if (err)
		goto err_unpreserve;

	kvfree(folios);
	*nr_foliosp = nr_folios;
	return pfolios;

err_unpreserve:
	i--;
	for (; i >= 0; i--)
		WARN_ON_ONCE(kho_unpreserve_folio(folios[i]));
	vfree(pfolios);
err_unpin:
	unpin_folios(folios, nr_folios);
err_free_folios:
	kvfree(folios);
	return ERR_PTR(err);
}

static void memfd_luo_unpreserve_folios(void *fdt, struct memfd_luo_folio_ser *pfolios,
					u64 nr_folios)
{
	struct kho_vmalloc *kho_vmalloc;
	long i;

	if (!nr_folios)
		return;

	kho_vmalloc = (struct kho_vmalloc *)fdt_getprop(fdt, 0, MEMFD_FDT_FOLIOS, NULL);
	/* The FDT was created by this kernel so expect it to be sane. */
	WARN_ON_ONCE(!kho_vmalloc);
	kho_unpreserve_vmalloc(kho_vmalloc);

	for (i = 0; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &pfolios[i];
		struct folio *folio;

		if (!pfolio->foliodesc)
			continue;

		folio = pfn_folio(PRESERVED_FOLIO_PFN(pfolio->foliodesc));

		WARN_ON_ONCE(kho_unpreserve_folio(folio));
		unpin_folio(folio);
	}

	vfree(pfolios);
}

static struct memfd_luo_folio_ser *memfd_luo_fdt_folios(const void *fdt, u64 *nr_folios)
{
	const struct kho_vmalloc *kho_vmalloc;
	struct memfd_luo_folio_ser *pfolios;
	const u64 *nr;
	int len;

	nr = fdt_getprop(fdt, 0, MEMFD_FDT_NR_FOLIOS, &len);
	if (!nr || len != sizeof(*nr)) {
		pr_err("invalid '%s' property\n", MEMFD_FDT_NR_FOLIOS);
		return NULL;
	}

	kho_vmalloc = fdt_getprop(fdt, 0, MEMFD_FDT_FOLIOS, &len);
	if (!kho_vmalloc || len != sizeof(*kho_vmalloc)) {
		pr_err("invalid '%s' property\n", MEMFD_FDT_FOLIOS);
		return NULL;
	}

	pfolios = kho_restore_vmalloc(kho_vmalloc);
	if (!pfolios)
		return NULL;

	*nr_folios = *nr;
	return pfolios;
}

static void *memfd_luo_create_fdt(void)
{
	struct folio *fdt_folio;
	int err = 0;
	void *fdt;

	/*
	 * The FDT only contains a couple of properties and a kho_vmalloc
	 * object. One page should be enough for that.
	 */
	fdt_folio = folio_alloc(GFP_KERNEL | __GFP_ZERO, 0);
	if (!fdt_folio)
		return NULL;

	fdt = folio_address(fdt_folio);

	err |= fdt_create(fdt, folio_size(fdt_folio));
	err |= fdt_finish_reservemap(fdt);
	err |= fdt_begin_node(fdt, "");
	if (err)
		goto free;

	return fdt;

free:
	folio_put(fdt_folio);
	return NULL;
}

static int memfd_luo_finish_fdt(void *fdt)
{
	int err;

	err = fdt_end_node(fdt);
	if (err)
		return err;

	return fdt_finish(fdt);
}

static int memfd_luo_preserve(struct liveupdate_file_op_args *args)
{
	struct inode *inode = file_inode(args->file);
	struct memfd_luo_folio_ser *pfolios;
	struct memfd_luo_private *private;
	u64 pos, nr_folios;
	int err = 0;
	void *fdt;
	long size;

	private = kmalloc(sizeof(*private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	inode_lock(inode);
	shmem_i_mapping_freeze(inode, true);

	size = i_size_read(inode);

	fdt = memfd_luo_create_fdt();
	if (!fdt) {
		err = -ENOMEM;
		goto err_unlock;
	}

	pos = args->file->f_pos;
	err = fdt_property(fdt, MEMFD_FDT_POS, &pos, sizeof(pos));
	if (err)
		goto err_free_fdt;

	err = fdt_property(fdt, MEMFD_FDT_SIZE, &size, sizeof(size));
	if (err)
		goto err_free_fdt;

	pfolios = memfd_luo_preserve_folios(args->file, fdt, &nr_folios);
	if (IS_ERR(pfolios)) {
		err = PTR_ERR(pfolios);
		goto err_free_fdt;
	}

	err = memfd_luo_finish_fdt(fdt);
	if (err)
		goto err_unpreserve_folios;

	err = kho_preserve_folio(virt_to_folio(fdt));
	if (err)
		goto err_unpreserve_folios;

	inode_unlock(inode);

	private->pfolios = pfolios;
	private->nr_folios = nr_folios;
	args->private_data = private;
	args->serialized_data = virt_to_phys(fdt);
	return 0;

err_unpreserve_folios:
	memfd_luo_unpreserve_folios(fdt, pfolios, nr_folios);
err_free_fdt:
	folio_put(virt_to_folio(fdt));
err_unlock:
	shmem_i_mapping_freeze(inode, false);
	inode_unlock(inode);
	kfree(private);
	return err;
}

static int memfd_luo_freeze(struct liveupdate_file_op_args *args)
{
	u64 pos = args->file->f_pos;
	void *fdt;
	int err;

	if (WARN_ON_ONCE(!args->serialized_data))
		return -EINVAL;

	fdt = phys_to_virt(args->serialized_data);

	/*
	 * The pos might have changed since prepare. Everything else stays the
	 * same.
	 */
	err = fdt_setprop(fdt, 0, "pos", &pos, sizeof(pos));
	if (err)
		return err;

	return 0;
}

static void memfd_luo_unpreserve(struct liveupdate_file_op_args *args)
{
	struct memfd_luo_private *private = args->private_data;
	struct inode *inode = file_inode(args->file);
	struct folio *fdt_folio;
	void *fdt;

	if (WARN_ON_ONCE(!args->serialized_data || !args->private_data))
		return;

	inode_lock(inode);
	shmem_i_mapping_freeze(inode, false);

	fdt = phys_to_virt(args->serialized_data);
	fdt_folio = virt_to_folio(fdt);

	memfd_luo_unpreserve_folios(fdt, private->pfolios, private->nr_folios);

	kho_unpreserve_folio(fdt_folio);
	folio_put(fdt_folio);
	inode_unlock(inode);
	kfree(private);
}

static struct folio *memfd_luo_get_fdt(u64 data)
{
	return kho_restore_folio((phys_addr_t)data);
}

static void memfd_luo_discard_folios(const struct memfd_luo_folio_ser *pfolios,
				     long nr_folios)
{
	unsigned int i;

	for (i = 0; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &pfolios[i];
		struct folio *folio;
		phys_addr_t phys;

		if (!pfolio->foliodesc)
			continue;

		phys = PFN_PHYS(PRESERVED_FOLIO_PFN(pfolio->foliodesc));
		folio = kho_restore_folio(phys);
		if (!folio) {
			pr_warn_ratelimited("Unable to restore folio at physical address: %llx\n",
					    phys);
			continue;
		}

		folio_put(folio);
	}
}

static void memfd_luo_finish(struct liveupdate_file_op_args *args)
{
	const struct memfd_luo_folio_ser *pfolios;
	struct folio *fdt_folio;
	const void *fdt;
	u64 nr_folios;

	if (args->retrieved)
		return;

	fdt_folio = memfd_luo_get_fdt(args->serialized_data);
	if (!fdt_folio) {
		pr_err("failed to restore memfd FDT\n");
		return;
	}

	fdt = folio_address(fdt_folio);

	pfolios = memfd_luo_fdt_folios(fdt, &nr_folios);
	if (!pfolios)
		goto out;

	memfd_luo_discard_folios(pfolios, nr_folios);
	vfree(pfolios);

out:
	folio_put(fdt_folio);
}

static int memfd_luo_retrieve_folios(struct file *file, const void *fdt)
{
	const struct memfd_luo_folio_ser *pfolios;
	struct inode *inode = file_inode(file);
	struct address_space *mapping;
	struct folio *folio;
	u64 nr_folios;
	long i = 0;
	int err;

	/* Careful: folios don't exist in FDT on zero-size files. */
	if (!inode->i_size)
		return 0;

	pfolios = memfd_luo_fdt_folios(fdt, &nr_folios);
	if (!pfolios) {
		pr_err("failed to fetch preserved folio list\n");
		return -EINVAL;
	}

	inode = file->f_inode;
	mapping = inode->i_mapping;

	for (; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &pfolios[i];
		phys_addr_t phys;
		u64 index;
		int flags;

		if (!pfolio->foliodesc)
			continue;

		phys = PFN_PHYS(PRESERVED_FOLIO_PFN(pfolio->foliodesc));
		folio = kho_restore_folio(phys);
		if (!folio) {
			pr_err("Unable to restore folio at physical address: %llx\n",
			       phys);
			goto put_folios;
		}
		index = pfolio->index;
		flags = PRESERVED_FOLIO_FLAGS(pfolio->foliodesc);

		/* Set up the folio for insertion. */
		__folio_set_locked(folio);
		__folio_set_swapbacked(folio);

		err = mem_cgroup_charge(folio, NULL, mapping_gfp_mask(mapping));
		if (err) {
			pr_err("shmem: failed to charge folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		err = shmem_add_to_page_cache(folio, mapping, index, NULL,
					      mapping_gfp_mask(mapping));
		if (err) {
			pr_err("shmem: failed to add to page cache folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		if (flags & PRESERVED_FLAG_UPTODATE)
			folio_mark_uptodate(folio);
		if (flags & PRESERVED_FLAG_DIRTY)
			folio_mark_dirty(folio);

		err = shmem_inode_acct_blocks(inode, 1);
		if (err) {
			pr_err("shmem: failed to account folio index %ld: %d\n",
			       i, err);
			goto unlock_folio;
		}

		shmem_recalc_inode(inode, 1, 0);
		folio_add_lru(folio);
		folio_unlock(folio);
		folio_put(folio);
	}

	vfree(pfolios);
	return 0;

unlock_folio:
	folio_unlock(folio);
	folio_put(folio);
	i++;
put_folios:
	/*
	 * Note: don't free the folios already added to the file. They will be
	 * freed when the file is freed. Free the ones not added yet here.
	 */
	for (; i < nr_folios; i++) {
		const struct memfd_luo_folio_ser *pfolio = &pfolios[i];

		folio = kho_restore_folio(PRESERVED_FOLIO_PFN(pfolio->foliodesc));
		if (folio)
			folio_put(folio);
	}

	vfree(pfolios);
	return err;
}

static int memfd_luo_retrieve(struct liveupdate_file_op_args *args)
{
	struct folio *fdt_folio;
	const u64 *pos, *size;
	struct file *file;
	int len, ret = 0;
	const void *fdt;

	fdt_folio = memfd_luo_get_fdt(args->serialized_data);
	if (!fdt_folio)
		return -ENOENT;

	fdt = page_to_virt(folio_page(fdt_folio, 0));

	size = fdt_getprop(fdt, 0, "size", &len);
	if (!size || len != sizeof(u64)) {
		pr_err("invalid 'size' property\n");
		ret = -EINVAL;
		goto put_fdt;
	}

	pos = fdt_getprop(fdt, 0, "pos", &len);
	if (!pos || len != sizeof(u64)) {
		pr_err("invalid 'pos' property\n");
		ret = -EINVAL;
		goto put_fdt;
	}

	file = shmem_file_setup("", 0, VM_NORESERVE);

	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		pr_err("failed to setup file: %d\n", ret);
		goto put_fdt;
	}

	vfs_setpos(file, *pos, MAX_LFS_FILESIZE);
	file->f_inode->i_size = *size;

	ret = memfd_luo_retrieve_folios(file, fdt);
	if (ret)
		goto put_file;

	args->file = file;
	folio_put(fdt_folio);
	return 0;

put_file:
	fput(file);
put_fdt:
	folio_put(fdt_folio);
	return ret;
}

static bool memfd_luo_can_preserve(struct liveupdate_file_handler *handler,
				   struct file *file)
{
	struct inode *inode = file_inode(file);

	return shmem_file(file) && !inode->i_nlink;
}

static const struct liveupdate_file_ops memfd_luo_file_ops = {
	.freeze = memfd_luo_freeze,
	.finish = memfd_luo_finish,
	.retrieve = memfd_luo_retrieve,
	.preserve = memfd_luo_preserve,
	.unpreserve = memfd_luo_unpreserve,
	.can_preserve = memfd_luo_can_preserve,
	.owner = THIS_MODULE,
};

static struct liveupdate_file_handler memfd_luo_handler = {
	.ops = &memfd_luo_file_ops,
	.compatible = MEMFD_LUO_FH_COMPATIBLE,
};

static int __init memfd_luo_init(void)
{
	int err;

	err = liveupdate_register_file_handler(&memfd_luo_handler);
	if (err)
		pr_err("Could not register luo filesystem handler: %d\n", err);

	return err;
}
late_initcall(memfd_luo_init);
