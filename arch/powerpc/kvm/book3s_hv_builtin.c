/*
 * Copyright 2011 Paul Mackerras, IBM Corp. <paulus@au1.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#include <linux/cpu.h>
#include <linux/kvm_host.h>
#include <linux/preempt.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/cma.h>
#include <linux/bitops.h>

#include <asm/cputable.h>
#include <asm/kvm_ppc.h>
#include <asm/kvm_book3s.h>
#include <asm/archrandom.h>
#include <asm/xics.h>
#include <asm/dbell.h>
#include <asm/cputhreads.h>
#include <asm/io.h>

#define KVM_CMA_CHUNK_ORDER	18

/*
 * Hash page table alignment on newer cpus(CPU_FTR_ARCH_206)
 * should be power of 2.
 */
#define HPT_ALIGN_PAGES		((1 << 18) >> PAGE_SHIFT) /* 256k */
/*
 * By default we reserve 5% of memory for hash pagetable allocation.
 */
static unsigned long kvm_cma_resv_ratio = 5;

static struct cma *kvm_cma;

static int __init early_parse_kvm_cma_resv(char *p)
{
	pr_debug("%s(%s)\n", __func__, p);
	if (!p)
		return -EINVAL;
	return kstrtoul(p, 0, &kvm_cma_resv_ratio);
}
early_param("kvm_cma_resv_ratio", early_parse_kvm_cma_resv);

struct page *kvm_alloc_hpt(unsigned long nr_pages)
{
	VM_BUG_ON(order_base_2(nr_pages) < KVM_CMA_CHUNK_ORDER - PAGE_SHIFT);

	return cma_alloc(kvm_cma, nr_pages, order_base_2(HPT_ALIGN_PAGES));
}
EXPORT_SYMBOL_GPL(kvm_alloc_hpt);

void kvm_release_hpt(struct page *page, unsigned long nr_pages)
{
	cma_release(kvm_cma, page, nr_pages);
}
EXPORT_SYMBOL_GPL(kvm_release_hpt);

/**
 * kvm_cma_reserve() - reserve area for kvm hash pagetable
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the memblock allocator
 * has been activated and all other subsystems have already allocated/reserved
 * memory.
 */
void __init kvm_cma_reserve(void)
{
	unsigned long align_size;
	struct memblock_region *reg;
	phys_addr_t selected_size = 0;

	/*
	 * We need CMA reservation only when we are in HV mode
	 */
	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return;
	/*
	 * We cannot use memblock_phys_mem_size() here, because
	 * memblock_analyze() has not been called yet.
	 */
	for_each_memblock(memory, reg)
		selected_size += memblock_region_memory_end_pfn(reg) -
				 memblock_region_memory_base_pfn(reg);

	selected_size = (selected_size * kvm_cma_resv_ratio / 100) << PAGE_SHIFT;
	if (selected_size) {
		pr_debug("%s: reserving %ld MiB for global area\n", __func__,
			 (unsigned long)selected_size / SZ_1M);
		align_size = HPT_ALIGN_PAGES << PAGE_SHIFT;
		cma_declare_contiguous(0, selected_size, 0, align_size,
			KVM_CMA_CHUNK_ORDER - PAGE_SHIFT, false, &kvm_cma);
	}
}

/*
 * Real-mode H_CONFER implementation.
 * We check if we are the only vcpu out of this virtual core
 * still running in the guest and not ceded.  If so, we pop up
 * to the virtual-mode implementation; if not, just return to
 * the guest.
 */
long int kvmppc_rm_h_confer(struct kvm_vcpu *vcpu, int target,
			    unsigned int yield_count)
{
	struct kvmppc_vcore *vc = local_paca->kvm_hstate.kvm_vcore;
	int ptid = local_paca->kvm_hstate.ptid;
	int threads_running;
	int threads_ceded;
	int threads_conferring;
	u64 stop = get_tb() + 10 * tb_ticks_per_usec;
	int rv = H_SUCCESS; /* => don't yield */

	set_bit(ptid, &vc->conferring_threads);
	while ((get_tb() < stop) && !VCORE_IS_EXITING(vc)) {
		threads_running = VCORE_ENTRY_MAP(vc);
		threads_ceded = vc->napping_threads;
		threads_conferring = vc->conferring_threads;
		if ((threads_ceded | threads_conferring) == threads_running) {
			rv = H_TOO_HARD; /* => do yield */
			break;
		}
	}
	clear_bit(ptid, &vc->conferring_threads);
	return rv;
}

/*
 * When running HV mode KVM we need to block certain operations while KVM VMs
 * exist in the system. We use a counter of VMs to track this.
 *
 * One of the operations we need to block is onlining of secondaries, so we
 * protect hv_vm_count with get/put_online_cpus().
 */
static atomic_t hv_vm_count;

void kvm_hv_vm_activated(void)
{
	get_online_cpus();
	atomic_inc(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_activated);

void kvm_hv_vm_deactivated(void)
{
	get_online_cpus();
	atomic_dec(&hv_vm_count);
	put_online_cpus();
}
EXPORT_SYMBOL_GPL(kvm_hv_vm_deactivated);

bool kvm_hv_mode_active(void)
{
	return atomic_read(&hv_vm_count) != 0;
}

extern int hcall_real_table[], hcall_real_table_end[];

int kvmppc_hcall_impl_hv_realmode(unsigned long cmd)
{
	cmd /= 4;
	if (cmd < hcall_real_table_end - hcall_real_table &&
	    hcall_real_table[cmd])
		return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(kvmppc_hcall_impl_hv_realmode);

int kvmppc_hwrng_present(void)
{
	return powernv_hwrng_present();
}
EXPORT_SYMBOL_GPL(kvmppc_hwrng_present);

long kvmppc_h_random(struct kvm_vcpu *vcpu)
{
	if (powernv_get_random_real_mode(&vcpu->arch.gpr[4]))
		return H_SUCCESS;

	return H_HARDWARE;
}

static inline void rm_writeb(unsigned long paddr, u8 val)
{
	__asm__ __volatile__("stbcix %0,0,%1"
		: : "r" (val), "r" (paddr) : "memory");
}

/*
 * Send an interrupt or message to another CPU.
 * This can only be called in real mode.
 * The caller needs to include any barrier needed to order writes
 * to memory vs. the IPI/message.
 */
void kvmhv_rm_send_ipi(int cpu)
{
	unsigned long xics_phys;

	/* On POWER8 for IPIs to threads in the same core, use msgsnd */
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    cpu_first_thread_sibling(cpu) ==
	    cpu_first_thread_sibling(raw_smp_processor_id())) {
		unsigned long msg = PPC_DBELL_TYPE(PPC_DBELL_SERVER);
		msg |= cpu_thread_in_core(cpu);
		__asm__ __volatile__ (PPC_MSGSND(%0) : : "r" (msg));
		return;
	}

	/* Else poke the target with an IPI */
	xics_phys = paca[cpu].kvm_hstate.xics_phys;
	rm_writeb(xics_phys + XICS_MFRR, IPI_PRIORITY);
}

/*
 * The following functions are called from the assembly code
 * in book3s_hv_rmhandlers.S.
 */
static void kvmhv_interrupt_vcore(struct kvmppc_vcore *vc, int active)
{
	int cpu = vc->pcpu;

	/* Order setting of exit map vs. msgsnd/IPI */
	smp_mb();
	for (; active; active >>= 1, ++cpu)
		if (active & 1)
			kvmhv_rm_send_ipi(cpu);
}

void kvmhv_commence_exit(int trap)
{
	struct kvmppc_vcore *vc = local_paca->kvm_hstate.kvm_vcore;
	int ptid = local_paca->kvm_hstate.ptid;
	struct kvm_split_mode *sip = local_paca->kvm_hstate.kvm_split_mode;
	int me, ee, i;

	/* Set our bit in the threads-exiting-guest map in the 0xff00
	   bits of vcore->entry_exit_map */
	me = 0x100 << ptid;
	do {
		ee = vc->entry_exit_map;
	} while (cmpxchg(&vc->entry_exit_map, ee, ee | me) != ee);

	/* Are we the first here? */
	if ((ee >> 8) != 0)
		return;

	/*
	 * Trigger the other threads in this vcore to exit the guest.
	 * If this is a hypervisor decrementer interrupt then they
	 * will be already on their way out of the guest.
	 */
	if (trap != BOOK3S_INTERRUPT_HV_DECREMENTER)
		kvmhv_interrupt_vcore(vc, ee & ~(1 << ptid));

	/*
	 * If we are doing dynamic micro-threading, interrupt the other
	 * subcores to pull them out of their guests too.
	 */
	if (!sip)
		return;

	for (i = 0; i < MAX_SUBCORES; ++i) {
		vc = sip->master_vcs[i];
		if (!vc)
			break;
		do {
			ee = vc->entry_exit_map;
			/* Already asked to exit? */
			if ((ee >> 8) != 0)
				break;
		} while (cmpxchg(&vc->entry_exit_map, ee,
				 ee | VCORE_EXIT_REQ) != ee);
		if ((ee >> 8) == 0)
			kvmhv_interrupt_vcore(vc, ee);
	}
}

struct kvmppc_host_rm_ops *kvmppc_host_rm_ops_hv;
EXPORT_SYMBOL_GPL(kvmppc_host_rm_ops_hv);

#ifdef CONFIG_KVM_XICS
static struct kvmppc_irq_map *get_irqmap(struct kvmppc_passthru_irqmap *pimap,
					 u32 xisr)
{
	int i;

	/*
	 * We access the mapped array here without a lock.  That
	 * is safe because we never reduce the number of entries
	 * in the array and we never change the v_hwirq field of
	 * an entry once it is set.
	 *
	 * We have also carefully ordered the stores in the writer
	 * and the loads here in the reader, so that if we find a matching
	 * hwirq here, the associated GSI and irq_desc fields are valid.
	 */
	for (i = 0; i < pimap->n_mapped; i++)  {
		if (xisr == pimap->mapped[i].r_hwirq) {
			/*
			 * Order subsequent reads in the caller to serialize
			 * with the writer.
			 */
			smp_rmb();
			return &pimap->mapped[i];
		}
	}
	return NULL;
}

/*
 * If we have an interrupt that's not an IPI, check if we have a
 * passthrough adapter and if so, check if this external interrupt
 * is for the adapter.
 * We will attempt to deliver the IRQ directly to the target VCPU's
 * ICP, the virtual ICP (based on affinity - the xive value in ICS).
 *
 * If the delivery fails or if this is not for a passthrough adapter,
 * return to the host to handle this interrupt. We earlier
 * saved a copy of the XIRR in the PACA, it will be picked up by
 * the host ICP driver.
 */
static int kvmppc_check_passthru(u32 xisr, __be32 xirr)
{
	struct kvmppc_passthru_irqmap *pimap;
	struct kvmppc_irq_map *irq_map;
	struct kvm_vcpu *vcpu;

	vcpu = local_paca->kvm_hstate.kvm_vcpu;
	if (!vcpu)
		return 1;
	pimap = kvmppc_get_passthru_irqmap(vcpu->kvm);
	if (!pimap)
		return 1;
	irq_map = get_irqmap(pimap, xisr);
	if (!irq_map)
		return 1;

	/* We're handling this interrupt, generic code doesn't need to */
	local_paca->kvm_hstate.saved_xirr = 0;

	return kvmppc_deliver_irq_passthru(vcpu, xirr, irq_map, pimap);
}

#else
static inline int kvmppc_check_passthru(u32 xisr, __be32 xirr)
{
	return 1;
}
#endif

/*
 * Determine what sort of external interrupt is pending (if any).
 * Returns:
 *	0 if no interrupt is pending
 *	1 if an interrupt is pending that needs to be handled by the host
 *	2 Passthrough that needs completion in the host
 *	-1 if there was a guest wakeup IPI (which has now been cleared)
 *	-2 if there is PCI passthrough external interrupt that was handled
 */

long kvmppc_read_intr(void)
{
	unsigned long xics_phys;
	u32 h_xirr;
	__be32 xirr;
	u32 xisr;
	u8 host_ipi;

	/* see if a host IPI is pending */
	host_ipi = local_paca->kvm_hstate.host_ipi;
	if (host_ipi)
		return 1;

	/* Now read the interrupt from the ICP */
	xics_phys = local_paca->kvm_hstate.xics_phys;
	if (unlikely(!xics_phys))
		return 1;

	/*
	 * Save XIRR for later. Since we get control in reverse endian
	 * on LE systems, save it byte reversed and fetch it back in
	 * host endian. Note that xirr is the value read from the
	 * XIRR register, while h_xirr is the host endian version.
	 */
	xirr = _lwzcix(xics_phys + XICS_XIRR);
	h_xirr = be32_to_cpu(xirr);
	local_paca->kvm_hstate.saved_xirr = h_xirr;
	xisr = h_xirr & 0xffffff;
	/*
	 * Ensure that the store/load complete to guarantee all side
	 * effects of loading from XIRR has completed
	 */
	smp_mb();

	/* if nothing pending in the ICP */
	if (!xisr)
		return 0;

	/* We found something in the ICP...
	 *
	 * If it is an IPI, clear the MFRR and EOI it.
	 */
	if (xisr == XICS_IPI) {
		_stbcix(xics_phys + XICS_MFRR, 0xff);
		_stwcix(xics_phys + XICS_XIRR, xirr);
		/*
		 * Need to ensure side effects of above stores
		 * complete before proceeding.
		 */
		smp_mb();

		/*
		 * We need to re-check host IPI now in case it got set in the
		 * meantime. If it's clear, we bounce the interrupt to the
		 * guest
		 */
		host_ipi = local_paca->kvm_hstate.host_ipi;
		if (unlikely(host_ipi != 0)) {
			/* We raced with the host,
			 * we need to resend that IPI, bummer
			 */
			_stbcix(xics_phys + XICS_MFRR, IPI_PRIORITY);
			/* Let side effects complete */
			smp_mb();
			return 1;
		}

		/* OK, it's an IPI for us */
		local_paca->kvm_hstate.saved_xirr = 0;
		return -1;
	}

	return kvmppc_check_passthru(xisr, xirr);
}
