// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: Live Update Orchestrator (LUO)
 *
 * Live Update is a specialized, kexec-based reboot process that allows a
 * running kernel to be updated from one version to another while preserving
 * the state of selected resources and keeping designated hardware devices
 * operational. For these devices, DMA activity may continue throughout the
 * kernel transition.
 *
 * While the primary use case driving this work is supporting live updates of
 * the Linux kernel when it is used as a hypervisor in cloud environments, the
 * LUO framework itself is designed to be workload-agnostic. Much like Kernel
 * Live Patching, which applies security fixes regardless of the workload,
 * Live Update facilitates a full kernel version upgrade for any type of system.
 *
 * For example, a non-hypervisor system running an in-memory cache like
 * memcached with many gigabytes of data can use LUO. The userspace service
 * can place its cache into a memfd, have its state preserved by LUO, and
 * restore it immediately after the kernel kexec.
 *
 * Whether the system is running virtual machines, containers, a
 * high-performance database, or networking services, LUO's primary goal is to
 * enable a full kernel update by preserving critical userspace state and
 * keeping essential devices operational.
 *
 * The core of LUO is a mechanism that tracks the progress of a live update,
 * along with a callback API that allows other kernel subsystems to participate
 * in the process. Example subsystems that can hook into LUO include: kvm,
 * iommu, interrupts, vfio, participating filesystems, and memory management.
 *
 * LUO uses Kexec Handover to transfer memory state from the current kernel to
 * the next kernel. For more details see
 * Documentation/core-api/kho/concepts.rst.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kexec_handover.h>
#include <linux/kobject.h>
#include <linux/libfdt.h>
#include <linux/liveupdate.h>
#include <linux/liveupdate/abi/luo.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include "luo_internal.h"
#include "kexec_handover_internal.h"

static struct {
	bool enabled;
	void *fdt_out;
	void *fdt_in;
	u64 liveupdate_num;
} luo_global;

static int __init early_liveupdate_param(char *buf)
{
	return kstrtobool(buf, &luo_global.enabled);
}
early_param("liveupdate", early_liveupdate_param);

static int __init luo_early_startup(void)
{
	phys_addr_t fdt_phys;
	int err, ln_size;
	const void *ptr;

	if (!kho_is_enabled()) {
		if (liveupdate_enabled())
			pr_warn("Disabling liveupdate because KHO is disabled\n");
		luo_global.enabled = false;
		return 0;
	}

	/* Retrieve LUO subtree, and verify its format. */
	err = kho_retrieve_subtree(LUO_FDT_KHO_ENTRY_NAME, &fdt_phys);
	if (err) {
		if (err != -ENOENT) {
			pr_err("failed to retrieve FDT '%s' from KHO: %pe\n",
			       LUO_FDT_KHO_ENTRY_NAME, ERR_PTR(err));
			return err;
		}

		return 0;
	}

	luo_global.fdt_in = __va(fdt_phys);
	err = fdt_node_check_compatible(luo_global.fdt_in, 0,
					LUO_FDT_COMPATIBLE);
	if (err) {
		pr_err("FDT '%s' is incompatible with '%s' [%d]\n",
		       LUO_FDT_KHO_ENTRY_NAME, LUO_FDT_COMPATIBLE, err);

		return -EINVAL;
	}

	ln_size = 0;
	ptr = fdt_getprop(luo_global.fdt_in, 0, LUO_FDT_LIVEUPDATE_NUM,
			  &ln_size);
	if (!ptr || ln_size != sizeof(luo_global.liveupdate_num)) {
		pr_err("Unable to get live update number '%s' [%d]\n",
		       LUO_FDT_LIVEUPDATE_NUM, ln_size);

		return -EINVAL;
	}
	memcpy(&luo_global.liveupdate_num, ptr,
	       sizeof(luo_global.liveupdate_num));
	pr_info("Retrieved live update data, liveupdate number: %lld\n",
		luo_global.liveupdate_num);

	return 0;
}

void __init liveupdate_init(void)
{
	int err;

	err = luo_early_startup();
	if (err) {
		pr_err("The incoming tree failed to initialize properly [%pe], disabling live update\n",
		       ERR_PTR(err));
		luo_global.enabled = false;
	}
}

/* Called during boot to create LUO fdt tree */
static int __init luo_fdt_setup(void)
{
	const u64 ln = luo_global.liveupdate_num + 1;
	void *fdt_out;
	int err;

	fdt_out = luo_alloc_preserve(LUO_FDT_SIZE);
	if (IS_ERR(fdt_out)) {
		pr_err("failed to allocate/preserve FDT memory\n");
		return PTR_ERR(fdt_out);
	}

	err = fdt_create(fdt_out, LUO_FDT_SIZE);
	err |= fdt_finish_reservemap(fdt_out);
	err |= fdt_begin_node(fdt_out, "");
	err |= fdt_property_string(fdt_out, "compatible", LUO_FDT_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_LIVEUPDATE_NUM, &ln, sizeof(ln));
	err |= fdt_end_node(fdt_out);
	err |= fdt_finish(fdt_out);
	if (err)
		goto exit_free;

	err = kho_add_subtree(LUO_FDT_KHO_ENTRY_NAME, fdt_out);
	if (err)
		goto exit_free;
	luo_global.fdt_out = fdt_out;

	return 0;

exit_free:
	luo_free_unpreserve(fdt_out, LUO_FDT_SIZE);
	pr_err("failed to prepare LUO FDT: %d\n", err);

	return err;
}

static int __init luo_late_startup(void)
{
	int err;

	if (!liveupdate_enabled())
		return 0;

	err = luo_fdt_setup();
	if (err)
		luo_global.enabled = false;

	return err;
}
late_initcall(luo_late_startup);

/* Public Functions */

/**
 * liveupdate_reboot() - Kernel reboot notifier for live update final
 * serialization.
 *
 * This function is invoked directly from the reboot() syscall pathway
 * if kexec is in progress.
 *
 * If any callback fails, this function aborts KHO, undoes the freeze()
 * callbacks, and returns an error.
 */
int liveupdate_reboot(void)
{
	int err;

	if (!liveupdate_enabled())
		return 0;

	err = kho_finalize();
	if (err) {
		pr_err("kho_finalize failed %d\n", err);
		/*
		 * kho_finalize() may return libfdt errors, to aboid passing to
		 * userspace unknown errors, change this to EAGAIN.
		 */
		err = -EAGAIN;
	}

	return err;
}

/**
 * liveupdate_enabled - Check if the live update feature is enabled.
 *
 * This function returns the state of the live update feature flag, which
 * can be controlled via the ``liveupdate`` kernel command-line parameter.
 *
 * @return true if live update is enabled, false otherwise.
 */
bool liveupdate_enabled(void)
{
	return luo_global.enabled;
}

/**
 * luo_alloc_preserve - Allocate, zero, and preserve memory.
 * @size: The number of bytes to allocate.
 *
 * Allocates a physically contiguous block of zeroed pages that is large
 * enough to hold @size bytes. The allocated memory is then registered with
 * KHO for preservation across a kexec.
 *
 * Note: The actual allocated size will be rounded up to the nearest
 * power-of-two page boundary.
 *
 * @return A virtual pointer to the allocated and preserved memory on success,
 * or an ERR_PTR() encoded error on failure.
 */
void *luo_alloc_preserve(size_t size)
{
	struct folio *folio;
	int order, ret;

	if (!size)
		return ERR_PTR(-EINVAL);

	order = get_order(size);
	if (order > MAX_PAGE_ORDER)
		return ERR_PTR(-E2BIG);

	folio = folio_alloc(GFP_KERNEL | __GFP_ZERO, order);
	if (!folio)
		return ERR_PTR(-ENOMEM);

	ret = kho_preserve_folio(folio);
	if (ret) {
		folio_put(folio);
		return ERR_PTR(ret);
	}

	return folio_address(folio);
}

/**
 * luo_free_unpreserve - Unpreserve and free memory.
 * @mem:  Pointer to the memory allocated by luo_alloc_preserve().
 * @size: The original size requested during allocation. This is used to
 *        recalculate the correct order for freeing the pages.
 *
 * Unregisters the memory from KHO preservation and frees the underlying
 * pages back to the system. This function should be called to clean up
 * memory allocated with luo_alloc_preserve().
 */
void luo_free_unpreserve(void *mem, size_t size)
{
	struct folio *folio;

	unsigned int order;

	if (!mem || !size)
		return;

	order = get_order(size);
	if (WARN_ON_ONCE(order > MAX_PAGE_ORDER))
		return;

	folio = virt_to_folio(mem);
	WARN_ON_ONCE(kho_unpreserve_folio(folio));
	folio_put(folio);
}

/**
 * luo_free_restore - Restore and free memory after kexec.
 * @mem:  Pointer to the memory (in the new kernel's address space)
 * that was allocated by the old kernel.
 * @size: The original size requested during allocation. This is used to
 * recalculate the correct order for freeing the pages.
 *
 * This function is intended to be called in the new kernel (post-kexec)
 * to take ownership of and free a memory region that was preserved by the
 * old kernel using luo_alloc_preserve().
 *
 * It first restores the pages from KHO (using their physical address)
 * and then frees the pages back to the new kernel's page allocator.
 */
void luo_free_restore(void *mem, size_t size)
{
	struct folio *folio;
	unsigned int order;

	if (!mem || !size)
		return;

	order = get_order(size);
	if (WARN_ON_ONCE(order > MAX_PAGE_ORDER))
		return;

	folio = kho_restore_folio(__pa(mem));
	if (!WARN_ON(!folio))
		free_pages((unsigned long)mem, order);
}
