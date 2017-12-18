/*
 * Intel CPU Microcode Update Driver for Linux
 *
 * Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		 2006 Shaohua Li <shaohua.li@intel.com>
 *
 * Intel CPU microcode early update for Linux
 *
 * Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *		      H Peter Anvin" <hpa@zytor.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * This needs to be before all headers so that pr_debug in printk.h doesn't turn
 * printk calls into no_printk().
 *
 *#define DEBUG
 */
#define pr_fmt(fmt) "microcode: " fmt

#include <linux/earlycpio.h>
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/initrd.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/mm.h>

#include <asm/microcode_intel.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/setup.h>
#include <asm/msr.h>

/*
 * Temporary microcode blobs pointers storage. We note here during early load
 * the pointers to microcode blobs we've got from whatever storage (detached
 * initrd, builtin). Later on, we put those into final storage
 * mc_saved_data.mc_saved.
 *
 * Important: those are offsets from the beginning of initrd or absolute
 * addresses within the kernel image when built-in.
 */
static unsigned long mc_tmp_ptrs[MAX_UCODE_COUNT];

static struct mc_saved_data {
	unsigned int num_saved;
	struct microcode_intel **mc_saved;
} mc_saved_data;

/* Microcode blobs within the initrd. 0 if builtin. */
static struct ucode_blobs {
	unsigned long start;
	bool valid;
} blobs;

/* Go through saved patches and find the one suitable for the current CPU. */
static enum ucode_state
find_microcode_patch(struct microcode_intel **saved,
		     unsigned int num_saved, struct ucode_cpu_info *uci)
{
	struct microcode_intel *ucode_ptr, *new_mc = NULL;
	struct microcode_header_intel *mc_hdr;
	int new_rev, ret, i;

	new_rev = uci->cpu_sig.rev;

	for (i = 0; i < num_saved; i++) {
		ucode_ptr = saved[i];
		mc_hdr	  = (struct microcode_header_intel *)ucode_ptr;

		ret = has_newer_microcode(ucode_ptr,
					  uci->cpu_sig.sig,
					  uci->cpu_sig.pf,
					  new_rev);
		if (!ret)
			continue;

		new_rev = mc_hdr->rev;
		new_mc  = ucode_ptr;
	}

	if (!new_mc)
		return UCODE_NFOUND;

	uci->mc = (struct microcode_intel *)new_mc;
	return UCODE_OK;
}

static inline void
copy_ptrs(struct microcode_intel **mc_saved, unsigned long *mc_ptrs,
	  unsigned long off, int num_saved)
{
	int i;

	for (i = 0; i < num_saved; i++)
		mc_saved[i] = (struct microcode_intel *)(mc_ptrs[i] + off);
}

#ifdef CONFIG_X86_32
static void
microcode_phys(struct microcode_intel **mc_saved_tmp, struct mc_saved_data *mcs)
{
	int i;
	struct microcode_intel ***mc_saved;

	mc_saved = (struct microcode_intel ***)__pa_nodebug(&mcs->mc_saved);

	for (i = 0; i < mcs->num_saved; i++) {
		struct microcode_intel *p;

		p = *(struct microcode_intel **)__pa_nodebug(mcs->mc_saved + i);
		mc_saved_tmp[i] = (struct microcode_intel *)__pa_nodebug(p);
	}
}
#endif

static enum ucode_state
load_microcode(struct mc_saved_data *mcs, unsigned long *mc_ptrs,
	       unsigned long offset, struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	unsigned int count = mcs->num_saved;

	if (!mcs->mc_saved) {
		copy_ptrs(mc_saved_tmp, mc_ptrs, offset, count);

		return find_microcode_patch(mc_saved_tmp, count, uci);
	} else {
#ifdef CONFIG_X86_32
		microcode_phys(mc_saved_tmp, mcs);
		return find_microcode_patch(mc_saved_tmp, count, uci);
#else
		return find_microcode_patch(mcs->mc_saved, count, uci);
#endif
	}
}

/*
 * Given CPU signature and a microcode patch, this function finds if the
 * microcode patch has matching family and model with the CPU.
 */
static enum ucode_state
matching_model_microcode(struct microcode_header_intel *mc_header,
			unsigned long sig)
{
	unsigned int fam, model;
	unsigned int fam_ucode, model_ucode;
	struct extended_sigtable *ext_header;
	unsigned long total_size = get_totalsize(mc_header);
	unsigned long data_size = get_datasize(mc_header);
	int ext_sigcount, i;
	struct extended_signature *ext_sig;

	fam   = x86_family(sig);
	model = x86_model(sig);

	fam_ucode   = x86_family(mc_header->sig);
	model_ucode = x86_model(mc_header->sig);

	if (fam == fam_ucode && model == model_ucode)
		return UCODE_OK;

	/* Look for ext. headers: */
	if (total_size <= data_size + MC_HEADER_SIZE)
		return UCODE_NFOUND;

	ext_header   = (void *) mc_header + data_size + MC_HEADER_SIZE;
	ext_sig      = (void *)ext_header + EXT_HEADER_SIZE;
	ext_sigcount = ext_header->count;

	for (i = 0; i < ext_sigcount; i++) {
		fam_ucode   = x86_family(ext_sig->sig);
		model_ucode = x86_model(ext_sig->sig);

		if (fam == fam_ucode && model == model_ucode)
			return UCODE_OK;

		ext_sig++;
	}
	return UCODE_NFOUND;
}

static int
save_microcode(struct mc_saved_data *mcs,
	       struct microcode_intel **mc_saved_src,
	       unsigned int num_saved)
{
	int i, j;
	struct microcode_intel **saved_ptr;
	int ret;

	if (!num_saved)
		return -EINVAL;

	/*
	 * Copy new microcode data.
	 */
	saved_ptr = kcalloc(num_saved, sizeof(struct microcode_intel *), GFP_KERNEL);
	if (!saved_ptr)
		return -ENOMEM;

	for (i = 0; i < num_saved; i++) {
		struct microcode_header_intel *mc_hdr;
		struct microcode_intel *mc;
		unsigned long size;

		if (!mc_saved_src[i]) {
			ret = -EINVAL;
			goto err;
		}

		mc     = mc_saved_src[i];
		mc_hdr = &mc->hdr;
		size   = get_totalsize(mc_hdr);

		saved_ptr[i] = kmemdup(mc, size, GFP_KERNEL);
		if (!saved_ptr[i]) {
			ret = -ENOMEM;
			goto err;
		}
	}

	/*
	 * Point to newly saved microcode.
	 */
	mcs->mc_saved  = saved_ptr;
	mcs->num_saved = num_saved;

	return 0;

err:
	for (j = 0; j <= i; j++)
		kfree(saved_ptr[j]);
	kfree(saved_ptr);

	return ret;
}

/*
 * A microcode patch in ucode_ptr is saved into mc_saved
 * - if it has matching signature and newer revision compared to an existing
 *   patch mc_saved.
 * - or if it is a newly discovered microcode patch.
 *
 * The microcode patch should have matching model with CPU.
 *
 * Returns: The updated number @num_saved of saved microcode patches.
 */
static unsigned int _save_mc(struct microcode_intel **mc_saved,
			     u8 *ucode_ptr, unsigned int num_saved)
{
	struct microcode_header_intel *mc_hdr, *mc_saved_hdr;
	unsigned int sig, pf;
	int found = 0, i;

	mc_hdr = (struct microcode_header_intel *)ucode_ptr;

	for (i = 0; i < num_saved; i++) {
		mc_saved_hdr = (struct microcode_header_intel *)mc_saved[i];
		sig	     = mc_saved_hdr->sig;
		pf	     = mc_saved_hdr->pf;

		if (!find_matching_signature(ucode_ptr, sig, pf))
			continue;

		found = 1;

		if (mc_hdr->rev <= mc_saved_hdr->rev)
			continue;

		/*
		 * Found an older ucode saved earlier. Replace it with
		 * this newer one.
		 */
		mc_saved[i] = (struct microcode_intel *)ucode_ptr;
		break;
	}

	/* Newly detected microcode, save it to memory. */
	if (i >= num_saved && !found)
		mc_saved[num_saved++] = (struct microcode_intel *)ucode_ptr;

	return num_saved;
}

/*
 * Get microcode matching with BSP's model. Only CPUs with the same model as
 * BSP can stay in the platform.
 */
static enum ucode_state __init
get_matching_model_microcode(unsigned long start, void *data, size_t size,
			     struct mc_saved_data *mcs, unsigned long *mc_ptrs,
			     struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	struct microcode_header_intel *mc_header;
	unsigned int num_saved = mcs->num_saved;
	enum ucode_state state = UCODE_OK;
	unsigned int leftover = size;
	u8 *ucode_ptr = data;
	unsigned int mc_size;
	int i;

	while (leftover && num_saved < ARRAY_SIZE(mc_saved_tmp)) {

		if (leftover < sizeof(mc_header))
			break;

		mc_header = (struct microcode_header_intel *)ucode_ptr;

		mc_size = get_totalsize(mc_header);
		if (!mc_size || mc_size > leftover ||
			microcode_sanity_check(ucode_ptr, 0) < 0)
			break;

		leftover -= mc_size;

		/*
		 * Since APs with same family and model as the BSP may boot in
		 * the platform, we need to find and save microcode patches
		 * with the same family and model as the BSP.
		 */
		if (matching_model_microcode(mc_header, uci->cpu_sig.sig) != UCODE_OK) {
			ucode_ptr += mc_size;
			continue;
		}

		num_saved = _save_mc(mc_saved_tmp, ucode_ptr, num_saved);

		ucode_ptr += mc_size;
	}

	if (leftover) {
		state = UCODE_ERROR;
		return state;
	}

	if (!num_saved) {
		state = UCODE_NFOUND;
		return state;
	}

	for (i = 0; i < num_saved; i++)
		mc_ptrs[i] = (unsigned long)mc_saved_tmp[i] - start;

	mcs->num_saved = num_saved;

	return state;
}

static int collect_cpu_info_early(struct ucode_cpu_info *uci)
{
	unsigned int val[2];
	unsigned int family, model;
	struct cpu_signature csig;
	unsigned int eax, ebx, ecx, edx;

	csig.sig = 0;
	csig.pf = 0;
	csig.rev = 0;

	memset(uci, 0, sizeof(*uci));

	eax = 0x00000001;
	ecx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);
	csig.sig = eax;

	family = x86_family(csig.sig);
	model  = x86_model(csig.sig);

	if ((model >= 5) || (family > 6)) {
		/* get processor flags from MSR 0x17 */
		native_rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig.pf = 1 << ((val[1] >> 18) & 7);
	}
	native_wrmsrl(MSR_IA32_UCODE_REV, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	native_rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	csig.rev = val[1];

	uci->cpu_sig = csig;
	uci->valid = 1;

	return 0;
}

static void show_saved_mc(void)
{
#ifdef DEBUG
	int i, j;
	unsigned int sig, pf, rev, total_size, data_size, date;
	struct ucode_cpu_info uci;

	if (!mc_saved_data.num_saved) {
		pr_debug("no microcode data saved.\n");
		return;
	}
	pr_debug("Total microcode saved: %d\n", mc_saved_data.num_saved);

	collect_cpu_info_early(&uci);

	sig = uci.cpu_sig.sig;
	pf = uci.cpu_sig.pf;
	rev = uci.cpu_sig.rev;
	pr_debug("CPU: sig=0x%x, pf=0x%x, rev=0x%x\n", sig, pf, rev);

	for (i = 0; i < mc_saved_data.num_saved; i++) {
		struct microcode_header_intel *mc_saved_header;
		struct extended_sigtable *ext_header;
		int ext_sigcount;
		struct extended_signature *ext_sig;

		mc_saved_header = (struct microcode_header_intel *)
				  mc_saved_data.mc_saved[i];
		sig = mc_saved_header->sig;
		pf = mc_saved_header->pf;
		rev = mc_saved_header->rev;
		total_size = get_totalsize(mc_saved_header);
		data_size = get_datasize(mc_saved_header);
		date = mc_saved_header->date;

		pr_debug("mc_saved[%d]: sig=0x%x, pf=0x%x, rev=0x%x, total size=0x%x, date = %04x-%02x-%02x\n",
			 i, sig, pf, rev, total_size,
			 date & 0xffff,
			 date >> 24,
			 (date >> 16) & 0xff);

		/* Look for ext. headers: */
		if (total_size <= data_size + MC_HEADER_SIZE)
			continue;

		ext_header = (void *) mc_saved_header + data_size + MC_HEADER_SIZE;
		ext_sigcount = ext_header->count;
		ext_sig = (void *)ext_header + EXT_HEADER_SIZE;

		for (j = 0; j < ext_sigcount; j++) {
			sig = ext_sig->sig;
			pf = ext_sig->pf;

			pr_debug("\tExtended[%d]: sig=0x%x, pf=0x%x\n",
				 j, sig, pf);

			ext_sig++;
		}

	}
#endif
}

/*
 * Save this mc into mc_saved_data. So it will be loaded early when a CPU is
 * hot added or resumes.
 *
 * Please make sure this mc should be a valid microcode patch before calling
 * this function.
 */
static void save_mc_for_early(u8 *mc)
{
#ifdef CONFIG_HOTPLUG_CPU
	/* Synchronization during CPU hotplug. */
	static DEFINE_MUTEX(x86_cpu_microcode_mutex);

	struct microcode_intel *mc_saved_tmp[MAX_UCODE_COUNT];
	unsigned int mc_saved_count_init;
	unsigned int num_saved;
	struct microcode_intel **mc_saved;
	int ret, i;

	mutex_lock(&x86_cpu_microcode_mutex);

	mc_saved_count_init = mc_saved_data.num_saved;
	num_saved = mc_saved_data.num_saved;
	mc_saved = mc_saved_data.mc_saved;

	if (mc_saved && num_saved)
		memcpy(mc_saved_tmp, mc_saved,
		       num_saved * sizeof(struct microcode_intel *));
	/*
	 * Save the microcode patch mc in mc_save_tmp structure if it's a newer
	 * version.
	 */
	num_saved = _save_mc(mc_saved_tmp, mc, num_saved);

	/*
	 * Save the mc_save_tmp in global mc_saved_data.
	 */
	ret = save_microcode(&mc_saved_data, mc_saved_tmp, num_saved);
	if (ret) {
		pr_err("Cannot save microcode patch.\n");
		goto out;
	}

	show_saved_mc();

	/*
	 * Free old saved microcode data.
	 */
	if (mc_saved) {
		for (i = 0; i < mc_saved_count_init; i++)
			kfree(mc_saved[i]);
		kfree(mc_saved);
	}

out:
	mutex_unlock(&x86_cpu_microcode_mutex);
#endif
}

static bool __init load_builtin_intel_microcode(struct cpio_data *cp)
{
#ifdef CONFIG_X86_64
	unsigned int eax = 0x00000001, ebx, ecx = 0, edx;
	char name[30];

	native_cpuid(&eax, &ebx, &ecx, &edx);

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		      x86_family(eax), x86_model(eax), x86_stepping(eax));

	return get_builtin_firmware(cp, name);
#else
	return false;
#endif
}

/*
 * Print ucode update info.
 */
static void
print_ucode_info(struct ucode_cpu_info *uci, unsigned int date)
{
	pr_info_once("microcode updated early to revision 0x%x, date = %04x-%02x-%02x\n",
		     uci->cpu_sig.rev,
		     date & 0xffff,
		     date >> 24,
		     (date >> 16) & 0xff);
}

#ifdef CONFIG_X86_32

static int delay_ucode_info;
static int current_mc_date;

/*
 * Print early updated ucode info after printk works. This is delayed info dump.
 */
void show_ucode_info_early(void)
{
	struct ucode_cpu_info uci;

	if (delay_ucode_info) {
		collect_cpu_info_early(&uci);
		print_ucode_info(&uci, current_mc_date);
		delay_ucode_info = 0;
	}
}

/*
 * At this point, we can not call printk() yet. Keep microcode patch number in
 * mc_saved_data.mc_saved and delay printing microcode info in
 * show_ucode_info_early() until printk() works.
 */
static void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc;
	int *delay_ucode_info_p;
	int *current_mc_date_p;

	mc = uci->mc;
	if (!mc)
		return;

	delay_ucode_info_p = (int *)__pa_nodebug(&delay_ucode_info);
	current_mc_date_p = (int *)__pa_nodebug(&current_mc_date);

	*delay_ucode_info_p = 1;
	*current_mc_date_p = mc->hdr.date;
}
#else

/*
 * Flush global tlb. We only do this in x86_64 where paging has been enabled
 * already and PGE should be enabled as well.
 */
static inline void flush_tlb_early(void)
{
	__native_flush_tlb_global_irq_disabled();
}

static inline void print_ucode(struct ucode_cpu_info *uci)
{
	struct microcode_intel *mc;

	mc = uci->mc;
	if (!mc)
		return;

	print_ucode_info(uci, mc->hdr.date);
}
#endif

static int apply_microcode_early(struct ucode_cpu_info *uci, bool early)
{
	struct microcode_intel *mc;
	unsigned int val[2];

	mc = uci->mc;
	if (!mc)
		return 0;

	/* write microcode via MSR 0x79 */
	native_wrmsrl(MSR_IA32_UCODE_WRITE, (unsigned long)mc->bits);
	native_wrmsrl(MSR_IA32_UCODE_REV, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	native_rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);
	if (val[1] != mc->hdr.rev)
		return -1;

#ifdef CONFIG_X86_64
	/* Flush global tlb. This is precaution. */
	flush_tlb_early();
#endif
	uci->cpu_sig.rev = val[1];

	if (early)
		print_ucode(uci);
	else
		print_ucode_info(uci, mc->hdr.date);

	return 0;
}

/*
 * This function converts microcode patch offsets previously stored in
 * mc_tmp_ptrs to pointers and stores the pointers in mc_saved_data.
 */
int __init save_microcode_in_initrd_intel(void)
{
	struct microcode_intel *mc_saved[MAX_UCODE_COUNT];
	unsigned int count = mc_saved_data.num_saved;
	unsigned long offset = 0;
	int ret;

	if (!count)
		return 0;

	/*
	 * We have found a valid initrd but it might've been relocated in the
	 * meantime so get its updated address.
	 */
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && blobs.valid)
		offset = initrd_start;

	copy_ptrs(mc_saved, mc_tmp_ptrs, offset, count);

	ret = save_microcode(&mc_saved_data, mc_saved, count);
	if (ret)
		pr_err("Cannot save microcode patches from initrd.\n");
	else
		show_saved_mc();

	return ret;
}

static __init enum ucode_state
__scan_microcode_initrd(struct cpio_data *cd, struct ucode_blobs *blbp)
{
#ifdef CONFIG_BLK_DEV_INITRD
	static __initdata char ucode_name[] = "kernel/x86/microcode/GenuineIntel.bin";
	char *p = IS_ENABLED(CONFIG_X86_32) ? (char *)__pa_nodebug(ucode_name)
						    : ucode_name;
# ifdef CONFIG_X86_32
	unsigned long start = 0, size;
	struct boot_params *params;

	params = (struct boot_params *)__pa_nodebug(&boot_params);
	size   = params->hdr.ramdisk_size;

	/*
	 * Set start only if we have an initrd image. We cannot use initrd_start
	 * because it is not set that early yet.
	 */
	start = (size ? params->hdr.ramdisk_image : 0);

# else /* CONFIG_X86_64 */
	unsigned long start = 0, size;

	size  = (u64)boot_params.ext_ramdisk_size << 32;
	size |= boot_params.hdr.ramdisk_size;

	if (size) {
		start  = (u64)boot_params.ext_ramdisk_image << 32;
		start |= boot_params.hdr.ramdisk_image;

		start += PAGE_OFFSET;
	}
# endif

	*cd = find_cpio_data(p, (void *)start, size, NULL);
	if (cd->data) {
		blbp->start = start;
		blbp->valid = true;

		return UCODE_OK;
	} else
#endif /* CONFIG_BLK_DEV_INITRD */
		return UCODE_ERROR;
}

static __init enum ucode_state
scan_microcode(struct mc_saved_data *mcs, unsigned long *mc_ptrs,
	       struct ucode_cpu_info *uci, struct ucode_blobs *blbp)
{
	struct cpio_data cd = { NULL, 0, "" };
	enum ucode_state ret;

	/* try built-in microcode first */
	if (load_builtin_intel_microcode(&cd))
		/*
		 * Invalidate blobs as we might've gotten an initrd too,
		 * supplied by the boot loader, by mistake or simply forgotten
		 * there. That's fine, we ignore it since we've found builtin
		 * microcode already.
		 */
		blbp->valid = false;
	else {
		ret = __scan_microcode_initrd(&cd, blbp);
		if (ret != UCODE_OK)
			return ret;
	}

	return get_matching_model_microcode(blbp->start, cd.data, cd.size,
					    mcs, mc_ptrs, uci);
}

static void __init
_load_ucode_intel_bsp(struct mc_saved_data *mcs, unsigned long *mc_ptrs,
		      struct ucode_blobs *blbp)
{
	struct ucode_cpu_info uci;
	enum ucode_state ret;

	collect_cpu_info_early(&uci);

	ret = scan_microcode(mcs, mc_ptrs, &uci, blbp);
	if (ret != UCODE_OK)
		return;

	ret = load_microcode(mcs, mc_ptrs, blbp->start, &uci);
	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, true);
}

void __init load_ucode_intel_bsp(void)
{
	struct ucode_blobs *blobs_p;
	struct mc_saved_data *mcs;
	unsigned long *ptrs;

#ifdef CONFIG_X86_32
	mcs	= (struct mc_saved_data *)__pa_nodebug(&mc_saved_data);
	ptrs	= (unsigned long *)__pa_nodebug(&mc_tmp_ptrs);
	blobs_p	= (struct ucode_blobs *)__pa_nodebug(&blobs);
#else
	mcs	= &mc_saved_data;
	ptrs	= mc_tmp_ptrs;
	blobs_p = &blobs;
#endif

	_load_ucode_intel_bsp(mcs, ptrs, blobs_p);
}

void load_ucode_intel_ap(void)
{
	struct ucode_blobs *blobs_p;
	unsigned long *ptrs, start = 0;
	struct mc_saved_data *mcs;
	struct ucode_cpu_info uci;
	enum ucode_state ret;

#ifdef CONFIG_X86_32
	mcs	= (struct mc_saved_data *)__pa_nodebug(&mc_saved_data);
	ptrs	= (unsigned long *)__pa_nodebug(mc_tmp_ptrs);
	blobs_p	= (struct ucode_blobs *)__pa_nodebug(&blobs);
#else
	mcs	= &mc_saved_data;
	ptrs	= mc_tmp_ptrs;
	blobs_p = &blobs;
#endif

	/*
	 * If there is no valid ucode previously saved in memory, no need to
	 * update ucode on this AP.
	 */
	if (!mcs->num_saved)
		return;

	if (blobs_p->valid) {
		start = blobs_p->start;

		/*
		 * Pay attention to CONFIG_RANDOMIZE_MEMORY=y as it shuffles
		 * physmem mapping too and there we have the initrd.
		 */
		start += PAGE_OFFSET - __PAGE_OFFSET_BASE;
	}

	collect_cpu_info_early(&uci);
	ret = load_microcode(mcs, ptrs, start, &uci);
	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, true);
}

void reload_ucode_intel(void)
{
	struct ucode_cpu_info uci;
	enum ucode_state ret;

	if (!mc_saved_data.num_saved)
		return;

	collect_cpu_info_early(&uci);

	ret = find_microcode_patch(mc_saved_data.mc_saved,
				   mc_saved_data.num_saved, &uci);
	if (ret != UCODE_OK)
		return;

	apply_microcode_early(&uci, false);
}

static int collect_cpu_info(int cpu_num, struct cpu_signature *csig)
{
	static struct cpu_signature prev;
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);
	unsigned int val[2];

	memset(csig, 0, sizeof(*csig));

	csig->sig = cpuid_eax(0x00000001);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig->pf = 1 << ((val[1] >> 18) & 7);
	}

	csig->rev = c->microcode;

	/* No extra locking on prev, races are harmless. */
	if (csig->sig != prev.sig || csig->pf != prev.pf || csig->rev != prev.rev) {
		pr_info("sig=0x%x, pf=0x%x, revision=0x%x\n",
			csig->sig, csig->pf, csig->rev);
		prev = *csig;
	}

	return 0;
}

/*
 * return 0 - no update found
 * return 1 - found update
 */
static int get_matching_mc(struct microcode_intel *mc, int cpu)
{
	struct cpu_signature cpu_sig;
	unsigned int csig, cpf, crev;

	collect_cpu_info(cpu, &cpu_sig);

	csig = cpu_sig.sig;
	cpf = cpu_sig.pf;
	crev = cpu_sig.rev;

	return has_newer_microcode(mc, csig, cpf, crev);
}

static int apply_microcode_intel(int cpu)
{
	struct microcode_intel *mc;
	struct ucode_cpu_info *uci;
	struct cpuinfo_x86 *c;
	unsigned int val[2];
	static int prev_rev;

	/* We should bind the task to the CPU */
	if (WARN_ON(raw_smp_processor_id() != cpu))
		return -1;

	uci = ucode_cpu_info + cpu;
	mc = uci->mc;
	if (!mc)
		return 0;

	/*
	 * Microcode on this CPU could be updated earlier. Only apply the
	 * microcode patch in mc when it is newer than the one on this
	 * CPU.
	 */
	if (!get_matching_mc(mc, cpu))
		return 0;

	/* write microcode via MSR 0x79 */
	wrmsrl(MSR_IA32_UCODE_WRITE, (unsigned long)mc->bits);
	wrmsrl(MSR_IA32_UCODE_REV, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	if (val[1] != mc->hdr.rev) {
		pr_err("CPU%d update to revision 0x%x failed\n",
		       cpu, mc->hdr.rev);
		return -1;
	}

	if (val[1] != prev_rev) {
		pr_info("updated to revision 0x%x, date = %04x-%02x-%02x\n",
			val[1],
			mc->hdr.date & 0xffff,
			mc->hdr.date >> 24,
			(mc->hdr.date >> 16) & 0xff);
		prev_rev = val[1];
	}

	c = &cpu_data(cpu);

	uci->cpu_sig.rev = val[1];
	c->microcode = val[1];

	return 0;
}

static enum ucode_state generic_load_microcode(int cpu, void *data, size_t size,
				int (*get_ucode_data)(void *, const void *, size_t))
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	u8 *ucode_ptr = data, *new_mc = NULL, *mc = NULL;
	int new_rev = uci->cpu_sig.rev;
	unsigned int leftover = size;
	enum ucode_state state = UCODE_OK;
	unsigned int curr_mc_size = 0;
	unsigned int csig, cpf;

	while (leftover) {
		struct microcode_header_intel mc_header;
		unsigned int mc_size;

		if (leftover < sizeof(mc_header)) {
			pr_err("error! Truncated header in microcode data file\n");
			break;
		}

		if (get_ucode_data(&mc_header, ucode_ptr, sizeof(mc_header)))
			break;

		mc_size = get_totalsize(&mc_header);
		if (!mc_size || mc_size > leftover) {
			pr_err("error! Bad data in microcode data file\n");
			break;
		}

		/* For performance reasons, reuse mc area when possible */
		if (!mc || mc_size > curr_mc_size) {
			vfree(mc);
			mc = vmalloc(mc_size);
			if (!mc)
				break;
			curr_mc_size = mc_size;
		}

		if (get_ucode_data(mc, ucode_ptr, mc_size) ||
		    microcode_sanity_check(mc, 1) < 0) {
			break;
		}

		csig = uci->cpu_sig.sig;
		cpf = uci->cpu_sig.pf;
		if (has_newer_microcode(mc, csig, cpf, new_rev)) {
			vfree(new_mc);
			new_rev = mc_header.rev;
			new_mc  = mc;
			mc = NULL;	/* trigger new vmalloc */
		}

		ucode_ptr += mc_size;
		leftover  -= mc_size;
	}

	vfree(mc);

	if (leftover) {
		vfree(new_mc);
		state = UCODE_ERROR;
		goto out;
	}

	if (!new_mc) {
		state = UCODE_NFOUND;
		goto out;
	}

	vfree(uci->mc);
	uci->mc = (struct microcode_intel *)new_mc;

	/*
	 * If early loading microcode is supported, save this mc into
	 * permanent memory. So it will be loaded early when a CPU is hot added
	 * or resumes.
	 */
	save_mc_for_early(new_mc);

	pr_debug("CPU%d found a matching microcode update with version 0x%x (current=0x%x)\n",
		 cpu, new_rev, uci->cpu_sig.rev);
out:
	return state;
}

static int get_ucode_fw(void *to, const void *from, size_t n)
{
	memcpy(to, from, n);
	return 0;
}

static enum ucode_state request_microcode_fw(int cpu, struct device *device,
					     bool refresh_fw)
{
	char name[30];
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	const struct firmware *firmware;
	enum ucode_state ret;

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_mask);

	if (request_firmware_direct(&firmware, name, device)) {
		pr_debug("data file %s load failed\n", name);
		return UCODE_NFOUND;
	}

	ret = generic_load_microcode(cpu, (void *)firmware->data,
				     firmware->size, &get_ucode_fw);

	release_firmware(firmware);

	return ret;
}

static int get_ucode_user(void *to, const void *from, size_t n)
{
	return copy_from_user(to, from, n);
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	return generic_load_microcode(cpu, (void *)buf, size, &get_ucode_user);
}

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	vfree(uci->mc);
	uci->mc = NULL;
}

static struct microcode_ops microcode_intel_ops = {
	.request_microcode_user		  = request_microcode_user,
	.request_microcode_fw             = request_microcode_fw,
	.collect_cpu_info                 = collect_cpu_info,
	.apply_microcode                  = apply_microcode_intel,
	.microcode_fini_cpu               = microcode_fini_cpu,
};

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	return &microcode_intel_ops;
}

