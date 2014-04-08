/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __POWERPC_KVM_PPC_H__
#define __POWERPC_KVM_PPC_H__

/* This file exists just so we can dereference kvm_vcpu, avoiding nested header
 * dependencies. */

#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/bug.h>
#ifdef CONFIG_PPC_BOOK3S
#include <asm/kvm_book3s.h>
#else
#include <asm/kvm_booke.h>
#endif
#ifdef CONFIG_KVM_BOOK3S_64_HANDLER
#include <asm/paca.h>
#endif

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with MMIO request */
	EMULATE_DO_DCR,       /* kvm_run filled with DCR request */
	EMULATE_FAIL,         /* can't emulate this instruction */
	EMULATE_AGAIN,        /* something went wrong. go again */
	EMULATE_EXIT_USER,    /* emulation requires exit to user-space */
};

extern int kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern int __kvmppc_vcpu_run(struct kvm_run *kvm_run, struct kvm_vcpu *vcpu);
extern void kvmppc_handler_highmem(void);

extern void kvmppc_dump_vcpu(struct kvm_vcpu *vcpu);
extern int kvmppc_handle_load(struct kvm_run *run, struct kvm_vcpu *vcpu,
                              unsigned int rt, unsigned int bytes,
                              int is_bigendian);
extern int kvmppc_handle_loads(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               unsigned int rt, unsigned int bytes,
                               int is_bigendian);
extern int kvmppc_handle_store(struct kvm_run *run, struct kvm_vcpu *vcpu,
                               u64 val, unsigned int bytes, int is_bigendian);

extern int kvmppc_emulate_instruction(struct kvm_run *run,
                                      struct kvm_vcpu *vcpu);
extern int kvmppc_emulate_mmio(struct kvm_run *run, struct kvm_vcpu *vcpu);
extern void kvmppc_emulate_dec(struct kvm_vcpu *vcpu);
extern u32 kvmppc_get_dec(struct kvm_vcpu *vcpu, u64 tb);
extern void kvmppc_decrementer_func(unsigned long data);
extern int kvmppc_sanity_check(struct kvm_vcpu *vcpu);
extern int kvmppc_subarch_vcpu_init(struct kvm_vcpu *vcpu);
extern void kvmppc_subarch_vcpu_uninit(struct kvm_vcpu *vcpu);

/* Core-specific hooks */

extern void kvmppc_mmu_map(struct kvm_vcpu *vcpu, u64 gvaddr, gpa_t gpaddr,
                           unsigned int gtlb_idx);
extern void kvmppc_mmu_priv_switch(struct kvm_vcpu *vcpu, int usermode);
extern void kvmppc_mmu_switch_pid(struct kvm_vcpu *vcpu, u32 pid);
extern void kvmppc_mmu_destroy(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_init(struct kvm_vcpu *vcpu);
extern int kvmppc_mmu_dtlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);
extern int kvmppc_mmu_itlb_index(struct kvm_vcpu *vcpu, gva_t eaddr);
extern gpa_t kvmppc_mmu_xlate(struct kvm_vcpu *vcpu, unsigned int gtlb_index,
                              gva_t eaddr);
extern void kvmppc_mmu_dtlb_miss(struct kvm_vcpu *vcpu);
extern void kvmppc_mmu_itlb_miss(struct kvm_vcpu *vcpu);

extern struct kvm_vcpu *kvmppc_core_vcpu_create(struct kvm *kvm,
                                                unsigned int id);
extern void kvmppc_core_vcpu_free(struct kvm_vcpu *vcpu);
extern int kvmppc_core_vcpu_setup(struct kvm_vcpu *vcpu);
extern int kvmppc_core_check_processor_compat(void);
extern int kvmppc_core_vcpu_translate(struct kvm_vcpu *vcpu,
                                      struct kvm_translation *tr);

extern void kvmppc_core_vcpu_load(struct kvm_vcpu *vcpu, int cpu);
extern void kvmppc_core_vcpu_put(struct kvm_vcpu *vcpu);

extern int kvmppc_core_prepare_to_enter(struct kvm_vcpu *vcpu);
extern int kvmppc_core_pending_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_program(struct kvm_vcpu *vcpu, ulong flags);
extern void kvmppc_core_queue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_dequeue_dec(struct kvm_vcpu *vcpu);
extern void kvmppc_core_queue_external(struct kvm_vcpu *vcpu,
                                       struct kvm_interrupt *irq);
extern void kvmppc_core_dequeue_external(struct kvm_vcpu *vcpu);
extern void kvmppc_core_flush_tlb(struct kvm_vcpu *vcpu);
extern int kvmppc_core_check_requests(struct kvm_vcpu *vcpu);

extern int kvmppc_booke_init(void);
extern void kvmppc_booke_exit(void);

extern void kvmppc_core_destroy_mmu(struct kvm_vcpu *vcpu);
extern int kvmppc_kvm_pv(struct kvm_vcpu *vcpu);
extern void kvmppc_map_magic(struct kvm_vcpu *vcpu);

extern long kvmppc_alloc_hpt(struct kvm *kvm, u32 *htab_orderp);
extern long kvmppc_alloc_reset_hpt(struct kvm *kvm, u32 *htab_orderp);
extern void kvmppc_free_hpt(struct kvm *kvm);
extern long kvmppc_prepare_vrma(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem);
extern void kvmppc_map_vrma(struct kvm_vcpu *vcpu,
			struct kvm_memory_slot *memslot, unsigned long porder);
extern int kvmppc_pseries_do_hcall(struct kvm_vcpu *vcpu);

extern long kvm_vm_ioctl_create_spapr_tce(struct kvm *kvm,
				struct kvm_create_spapr_tce *args);
extern long kvmppc_h_put_tce(struct kvm_vcpu *vcpu, unsigned long liobn,
			     unsigned long ioba, unsigned long tce);
extern struct kvm_rma_info *kvm_alloc_rma(void);
extern void kvm_release_rma(struct kvm_rma_info *ri);
extern struct page *kvm_alloc_hpt(unsigned long nr_pages);
extern void kvm_release_hpt(struct page *page, unsigned long nr_pages);
extern int kvmppc_core_init_vm(struct kvm *kvm);
extern void kvmppc_core_destroy_vm(struct kvm *kvm);
extern void kvmppc_core_free_memslot(struct kvm *kvm,
				     struct kvm_memory_slot *free,
				     struct kvm_memory_slot *dont);
extern int kvmppc_core_create_memslot(struct kvm *kvm,
				      struct kvm_memory_slot *slot,
				      unsigned long npages);
extern int kvmppc_core_prepare_memory_region(struct kvm *kvm,
				struct kvm_memory_slot *memslot,
				struct kvm_userspace_memory_region *mem);
extern void kvmppc_core_commit_memory_region(struct kvm *kvm,
				struct kvm_userspace_memory_region *mem,
				const struct kvm_memory_slot *old);
extern int kvm_vm_ioctl_get_smmu_info(struct kvm *kvm,
				      struct kvm_ppc_smmu_info *info);
extern void kvmppc_core_flush_memslot(struct kvm *kvm,
				      struct kvm_memory_slot *memslot);

extern int kvmppc_bookehv_init(void);
extern void kvmppc_bookehv_exit(void);

extern int kvmppc_prepare_to_enter(struct kvm_vcpu *vcpu);

extern int kvm_vm_ioctl_get_htab_fd(struct kvm *kvm, struct kvm_get_htab_fd *);

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq);

extern int kvm_vm_ioctl_rtas_define_token(struct kvm *kvm, void __user *argp);
extern int kvmppc_rtas_hcall(struct kvm_vcpu *vcpu);
extern void kvmppc_rtas_tokens_free(struct kvm *kvm);
extern int kvmppc_xics_set_xive(struct kvm *kvm, u32 irq, u32 server,
				u32 priority);
extern int kvmppc_xics_get_xive(struct kvm *kvm, u32 irq, u32 *server,
				u32 *priority);
extern int kvmppc_xics_int_on(struct kvm *kvm, u32 irq);
extern int kvmppc_xics_int_off(struct kvm *kvm, u32 irq);

union kvmppc_one_reg {
	u32	wval;
	u64	dval;
	vector128 vval;
	u64	vsxval[2];
	struct {
		u64	addr;
		u64	length;
	}	vpaval;
};

struct kvmppc_ops {
	struct module *owner;
	int (*get_sregs)(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
	int (*set_sregs)(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
	int (*get_one_reg)(struct kvm_vcpu *vcpu, u64 id,
			   union kvmppc_one_reg *val);
	int (*set_one_reg)(struct kvm_vcpu *vcpu, u64 id,
			   union kvmppc_one_reg *val);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);
	void (*set_msr)(struct kvm_vcpu *vcpu, u64 msr);
	int (*vcpu_run)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	struct kvm_vcpu *(*vcpu_create)(struct kvm *kvm, unsigned int id);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	int (*check_requests)(struct kvm_vcpu *vcpu);
	int (*get_dirty_log)(struct kvm *kvm, struct kvm_dirty_log *log);
	void (*flush_memslot)(struct kvm *kvm, struct kvm_memory_slot *memslot);
	int (*prepare_memory_region)(struct kvm *kvm,
				     struct kvm_memory_slot *memslot,
				     struct kvm_userspace_memory_region *mem);
	void (*commit_memory_region)(struct kvm *kvm,
				     struct kvm_userspace_memory_region *mem,
				     const struct kvm_memory_slot *old);
	int (*unmap_hva)(struct kvm *kvm, unsigned long hva);
	int (*unmap_hva_range)(struct kvm *kvm, unsigned long start,
			   unsigned long end);
	int (*age_hva)(struct kvm *kvm, unsigned long hva);
	int (*test_age_hva)(struct kvm *kvm, unsigned long hva);
	void (*set_spte_hva)(struct kvm *kvm, unsigned long hva, pte_t pte);
	void (*mmu_destroy)(struct kvm_vcpu *vcpu);
	void (*free_memslot)(struct kvm_memory_slot *free,
			     struct kvm_memory_slot *dont);
	int (*create_memslot)(struct kvm_memory_slot *slot,
			      unsigned long npages);
	int (*init_vm)(struct kvm *kvm);
	void (*destroy_vm)(struct kvm *kvm);
	int (*get_smmu_info)(struct kvm *kvm, struct kvm_ppc_smmu_info *info);
	int (*emulate_op)(struct kvm_run *run, struct kvm_vcpu *vcpu,
			  unsigned int inst, int *advance);
	int (*emulate_mtspr)(struct kvm_vcpu *vcpu, int sprn, ulong spr_val);
	int (*emulate_mfspr)(struct kvm_vcpu *vcpu, int sprn, ulong *spr_val);
	void (*fast_vcpu_kick)(struct kvm_vcpu *vcpu);
	long (*arch_vm_ioctl)(struct file *filp, unsigned int ioctl,
			      unsigned long arg);

};

extern struct kvmppc_ops *kvmppc_hv_ops;
extern struct kvmppc_ops *kvmppc_pr_ops;

static inline bool is_kvmppc_hv_enabled(struct kvm *kvm)
{
	return kvm->arch.kvm_ops == kvmppc_hv_ops;
}

/*
 * Cuts out inst bits with ordering according to spec.
 * That means the leftmost bit is zero. All given bits are included.
 */
static inline u32 kvmppc_get_field(u64 inst, int msb, int lsb)
{
	u32 r;
	u32 mask;

	BUG_ON(msb > lsb);

	mask = (1 << (lsb - msb + 1)) - 1;
	r = (inst >> (63 - lsb)) & mask;

	return r;
}

/*
 * Replaces inst bits with ordering according to spec.
 */
static inline u32 kvmppc_set_field(u64 inst, int msb, int lsb, int value)
{
	u32 r;
	u32 mask;

	BUG_ON(msb > lsb);

	mask = ((1 << (lsb - msb + 1)) - 1) << (63 - lsb);
	r = (inst & ~mask) | ((value << (63 - lsb)) & mask);

	return r;
}

#define one_reg_size(id)	\
	(1ul << (((id) & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT))

#define get_reg_val(id, reg)	({		\
	union kvmppc_one_reg __u;		\
	switch (one_reg_size(id)) {		\
	case 4: __u.wval = (reg); break;	\
	case 8: __u.dval = (reg); break;	\
	default: BUG();				\
	}					\
	__u;					\
})


#define set_reg_val(id, val)	({		\
	u64 __v;				\
	switch (one_reg_size(id)) {		\
	case 4: __v = (val).wval; break;	\
	case 8: __v = (val).dval; break;	\
	default: BUG();				\
	}					\
	__v;					\
})

int kvmppc_core_get_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_core_set_sregs(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvmppc_get_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);
int kvmppc_set_sregs_ivor(struct kvm_vcpu *vcpu, struct kvm_sregs *sregs);

int kvm_vcpu_ioctl_get_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);
int kvm_vcpu_ioctl_set_one_reg(struct kvm_vcpu *vcpu, struct kvm_one_reg *reg);
int kvmppc_get_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *);
int kvmppc_set_one_reg(struct kvm_vcpu *vcpu, u64 id, union kvmppc_one_reg *);

void kvmppc_set_pid(struct kvm_vcpu *vcpu, u32 pid);

struct openpic;

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
extern void kvm_cma_reserve(void) __init;
static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{
	paca[cpu].kvm_hstate.xics_phys = addr;
}

static inline u32 kvmppc_get_xics_latch(void)
{
	u32 xirr;

	xirr = get_paca()->kvm_hstate.saved_xirr;
	get_paca()->kvm_hstate.saved_xirr = 0;
	return xirr;
}

static inline void kvmppc_set_host_ipi(int cpu, u8 host_ipi)
{
	paca[cpu].kvm_hstate.host_ipi = host_ipi;
}

static inline void kvmppc_fast_vcpu_kick(struct kvm_vcpu *vcpu)
{
	vcpu->kvm->arch.kvm_ops->fast_vcpu_kick(vcpu);
}

#else
static inline void __init kvm_cma_reserve(void)
{}

static inline void kvmppc_set_xics_phys(int cpu, unsigned long addr)
{}

static inline u32 kvmppc_get_xics_latch(void)
{
	return 0;
}

static inline void kvmppc_set_host_ipi(int cpu, u8 host_ipi)
{}

static inline void kvmppc_fast_vcpu_kick(struct kvm_vcpu *vcpu)
{
	kvm_vcpu_kick(vcpu);
}
#endif

#ifdef CONFIG_KVM_XICS
static inline int kvmppc_xics_enabled(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.irq_type == KVMPPC_IRQ_XICS;
}
extern void kvmppc_xics_free_icp(struct kvm_vcpu *vcpu);
extern int kvmppc_xics_create_icp(struct kvm_vcpu *vcpu, unsigned long server);
extern int kvm_vm_ioctl_xics_irq(struct kvm *kvm, struct kvm_irq_level *args);
extern int kvmppc_xics_hcall(struct kvm_vcpu *vcpu, u32 cmd);
extern u64 kvmppc_xics_get_icp(struct kvm_vcpu *vcpu);
extern int kvmppc_xics_set_icp(struct kvm_vcpu *vcpu, u64 icpval);
extern int kvmppc_xics_connect_vcpu(struct kvm_device *dev,
			struct kvm_vcpu *vcpu, u32 cpu);
#else
static inline int kvmppc_xics_enabled(struct kvm_vcpu *vcpu)
	{ return 0; }
static inline void kvmppc_xics_free_icp(struct kvm_vcpu *vcpu) { }
static inline int kvmppc_xics_create_icp(struct kvm_vcpu *vcpu,
					 unsigned long server)
	{ return -EINVAL; }
static inline int kvm_vm_ioctl_xics_irq(struct kvm *kvm,
					struct kvm_irq_level *args)
	{ return -ENOTTY; }
static inline int kvmppc_xics_hcall(struct kvm_vcpu *vcpu, u32 cmd)
	{ return 0; }
#endif

static inline void kvmppc_set_epr(struct kvm_vcpu *vcpu, u32 epr)
{
#ifdef CONFIG_KVM_BOOKE_HV
	mtspr(SPRN_GEPR, epr);
#elif defined(CONFIG_BOOKE)
	vcpu->arch.epr = epr;
#endif
}

#ifdef CONFIG_KVM_MPIC

void kvmppc_mpic_set_epr(struct kvm_vcpu *vcpu);
int kvmppc_mpic_connect_vcpu(struct kvm_device *dev, struct kvm_vcpu *vcpu,
			     u32 cpu);
void kvmppc_mpic_disconnect_vcpu(struct openpic *opp, struct kvm_vcpu *vcpu);

#else

static inline void kvmppc_mpic_set_epr(struct kvm_vcpu *vcpu)
{
}

static inline int kvmppc_mpic_connect_vcpu(struct kvm_device *dev,
		struct kvm_vcpu *vcpu, u32 cpu)
{
	return -EINVAL;
}

static inline void kvmppc_mpic_disconnect_vcpu(struct openpic *opp,
		struct kvm_vcpu *vcpu)
{
}

#endif /* CONFIG_KVM_MPIC */

int kvm_vcpu_ioctl_config_tlb(struct kvm_vcpu *vcpu,
			      struct kvm_config_tlb *cfg);
int kvm_vcpu_ioctl_dirty_tlb(struct kvm_vcpu *vcpu,
			     struct kvm_dirty_tlb *cfg);

long kvmppc_alloc_lpid(void);
void kvmppc_claim_lpid(long lpid);
void kvmppc_free_lpid(long lpid);
void kvmppc_init_lpid(unsigned long nr_lpids);

static inline void kvmppc_mmu_flush_icache(pfn_t pfn)
{
	struct page *page;
	/*
	 * We can only access pages that the kernel maps
	 * as memory. Bail out for unmapped ones.
	 */
	if (!pfn_valid(pfn))
		return;

	/* Clear i-cache for new pages */
	page = pfn_to_page(pfn);
	if (!test_bit(PG_arch_1, &page->flags)) {
		flush_dcache_icache_page(page);
		set_bit(PG_arch_1, &page->flags);
	}
}

/*
 * Please call after prepare_to_enter. This function puts the lazy ee and irq
 * disabled tracking state back to normal mode, without actually enabling
 * interrupts.
 */
static inline void kvmppc_fix_ee_before_entry(void)
{
	trace_hardirqs_on();

#ifdef CONFIG_PPC64
	/* Only need to enable IRQs by hard enabling them after this */
	local_paca->irq_happened = 0;
	local_paca->soft_enabled = 1;
#endif
}

static inline ulong kvmppc_get_ea_indexed(struct kvm_vcpu *vcpu, int ra, int rb)
{
	ulong ea;
	ulong msr_64bit = 0;

	ea = kvmppc_get_gpr(vcpu, rb);
	if (ra)
		ea += kvmppc_get_gpr(vcpu, ra);

#if defined(CONFIG_PPC_BOOK3E_64)
	msr_64bit = MSR_CM;
#elif defined(CONFIG_PPC_BOOK3S_64)
	msr_64bit = MSR_SF;
#endif

	if (!(vcpu->arch.shared->msr & msr_64bit))
		ea = (uint32_t)ea;

	return ea;
}

extern void xics_wake_cpu(int cpu);

#endif /* __POWERPC_KVM_PPC_H__ */
