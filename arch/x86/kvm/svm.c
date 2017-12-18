/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * AMD SVM support
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#define pr_fmt(fmt) "SVM: " fmt

#include <linux/kvm_host.h>

#include "irq.h"
#include "mmu.h"
#include "kvm_cache_regs.h"
#include "x86.h"
#include "cpuid.h"
#include "pmu.h"

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/trace_events.h>
#include <linux/slab.h>
#include <linux/amd-iommu.h>
#include <linux/hashtable.h>

#include <asm/apic.h>
#include <asm/perf_event.h>
#include <asm/tlbflush.h>
#include <asm/desc.h>
#include <asm/debugreg.h>
#include <asm/kvm_para.h>
#include <asm/irq_remapping.h>

#include <asm/virtext.h>
#include "trace.h"

#define __ex(x) __kvm_handle_fault_on_reboot(x)

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

static const struct x86_cpu_id svm_cpu_id[] = {
	X86_FEATURE_MATCH(X86_FEATURE_SVM),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, svm_cpu_id);

#define IOPM_ALLOC_ORDER 2
#define MSRPM_ALLOC_ORDER 1

#define SEG_TYPE_LDT 2
#define SEG_TYPE_BUSY_TSS16 3

#define SVM_FEATURE_NPT            (1 <<  0)
#define SVM_FEATURE_LBRV           (1 <<  1)
#define SVM_FEATURE_SVML           (1 <<  2)
#define SVM_FEATURE_NRIP           (1 <<  3)
#define SVM_FEATURE_TSC_RATE       (1 <<  4)
#define SVM_FEATURE_VMCB_CLEAN     (1 <<  5)
#define SVM_FEATURE_FLUSH_ASID     (1 <<  6)
#define SVM_FEATURE_DECODE_ASSIST  (1 <<  7)
#define SVM_FEATURE_PAUSE_FILTER   (1 << 10)

#define SVM_AVIC_DOORBELL	0xc001011b

#define NESTED_EXIT_HOST	0	/* Exit handled on host level */
#define NESTED_EXIT_DONE	1	/* Exit caused nested vmexit  */
#define NESTED_EXIT_CONTINUE	2	/* Further checks needed      */

#define DEBUGCTL_RESERVED_BITS (~(0x3fULL))

#define TSC_RATIO_RSVD          0xffffff0000000000ULL
#define TSC_RATIO_MIN		0x0000000000000001ULL
#define TSC_RATIO_MAX		0x000000ffffffffffULL

#define AVIC_HPA_MASK	~((0xFFFULL << 52) | 0xFFF)

/*
 * 0xff is broadcast, so the max index allowed for physical APIC ID
 * table is 0xfe.  APIC IDs above 0xff are reserved.
 */
#define AVIC_MAX_PHYSICAL_ID_COUNT	255

#define AVIC_UNACCEL_ACCESS_WRITE_MASK		1
#define AVIC_UNACCEL_ACCESS_OFFSET_MASK		0xFF0
#define AVIC_UNACCEL_ACCESS_VECTOR_MASK		0xFFFFFFFF

/* AVIC GATAG is encoded using VM and VCPU IDs */
#define AVIC_VCPU_ID_BITS		8
#define AVIC_VCPU_ID_MASK		((1 << AVIC_VCPU_ID_BITS) - 1)

#define AVIC_VM_ID_BITS			24
#define AVIC_VM_ID_NR			(1 << AVIC_VM_ID_BITS)
#define AVIC_VM_ID_MASK			((1 << AVIC_VM_ID_BITS) - 1)

#define AVIC_GATAG(x, y)		(((x & AVIC_VM_ID_MASK) << AVIC_VCPU_ID_BITS) | \
						(y & AVIC_VCPU_ID_MASK))
#define AVIC_GATAG_TO_VMID(x)		((x >> AVIC_VCPU_ID_BITS) & AVIC_VM_ID_MASK)
#define AVIC_GATAG_TO_VCPUID(x)		(x & AVIC_VCPU_ID_MASK)

static bool erratum_383_found __read_mostly;

static const u32 host_save_user_msrs[] = {
#ifdef CONFIG_X86_64
	MSR_STAR, MSR_LSTAR, MSR_CSTAR, MSR_SYSCALL_MASK, MSR_KERNEL_GS_BASE,
	MSR_FS_BASE,
#endif
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_TSC_AUX,
};

#define NR_HOST_SAVE_USER_MSRS ARRAY_SIZE(host_save_user_msrs)

struct kvm_vcpu;

struct nested_state {
	struct vmcb *hsave;
	u64 hsave_msr;
	u64 vm_cr_msr;
	u64 vmcb;

	/* These are the merged vectors */
	u32 *msrpm;

	/* gpa pointers to the real vectors */
	u64 vmcb_msrpm;
	u64 vmcb_iopm;

	/* A VMEXIT is required but not yet emulated */
	bool exit_required;

	/* cache for intercepts of the guest */
	u32 intercept_cr;
	u32 intercept_dr;
	u32 intercept_exceptions;
	u64 intercept;

	/* Nested Paging related state */
	u64 nested_cr3;
};

#define MSRPM_OFFSETS	16
static u32 msrpm_offsets[MSRPM_OFFSETS] __read_mostly;

/*
 * Set osvw_len to higher value when updated Revision Guides
 * are published and we know what the new status bits are
 */
static uint64_t osvw_len = 4, osvw_status;

struct vcpu_svm {
	struct kvm_vcpu vcpu;
	struct vmcb *vmcb;
	unsigned long vmcb_pa;
	struct svm_cpu_data *svm_data;
	uint64_t asid_generation;
	uint64_t sysenter_esp;
	uint64_t sysenter_eip;
	uint64_t tsc_aux;

	u64 next_rip;

	u64 host_user_msrs[NR_HOST_SAVE_USER_MSRS];
	struct {
		u16 fs;
		u16 gs;
		u16 ldt;
		u64 gs_base;
	} host;

	u32 *msrpm;

	ulong nmi_iret_rip;

	struct nested_state nested;

	bool nmi_singlestep;

	unsigned int3_injected;
	unsigned long int3_rip;
	u32 apf_reason;

	/* cached guest cpuid flags for faster access */
	bool nrips_enabled	: 1;

	u32 ldr_reg;
	struct page *avic_backing_page;
	u64 *avic_physical_id_cache;
	bool avic_is_running;

	/*
	 * Per-vcpu list of struct amd_svm_iommu_ir:
	 * This is used mainly to store interrupt remapping information used
	 * when update the vcpu affinity. This avoids the need to scan for
	 * IRTE and try to match ga_tag in the IOMMU driver.
	 */
	struct list_head ir_list;
	spinlock_t ir_list_lock;
};

/*
 * This is a wrapper of struct amd_iommu_ir_data.
 */
struct amd_svm_iommu_ir {
	struct list_head node;	/* Used by SVM for per-vcpu ir_list */
	void *data;		/* Storing pointer to struct amd_ir_data */
};

#define AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK	(0xFF)
#define AVIC_LOGICAL_ID_ENTRY_VALID_MASK		(1 << 31)

#define AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK	(0xFFULL)
#define AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK	(0xFFFFFFFFFFULL << 12)
#define AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK		(1ULL << 62)
#define AVIC_PHYSICAL_ID_ENTRY_VALID_MASK		(1ULL << 63)

static DEFINE_PER_CPU(u64, current_tsc_ratio);
#define TSC_RATIO_DEFAULT	0x0100000000ULL

#define MSR_INVALID			0xffffffffU

static const struct svm_direct_access_msrs {
	u32 index;   /* Index of the MSR */
	bool always; /* True if intercept is always on */
} direct_access_msrs[] = {
	{ .index = MSR_STAR,				.always = true  },
	{ .index = MSR_IA32_SYSENTER_CS,		.always = true  },
#ifdef CONFIG_X86_64
	{ .index = MSR_GS_BASE,				.always = true  },
	{ .index = MSR_FS_BASE,				.always = true  },
	{ .index = MSR_KERNEL_GS_BASE,			.always = true  },
	{ .index = MSR_LSTAR,				.always = true  },
	{ .index = MSR_CSTAR,				.always = true  },
	{ .index = MSR_SYSCALL_MASK,			.always = true  },
#endif
	{ .index = MSR_IA32_LASTBRANCHFROMIP,		.always = false },
	{ .index = MSR_IA32_LASTBRANCHTOIP,		.always = false },
	{ .index = MSR_IA32_LASTINTFROMIP,		.always = false },
	{ .index = MSR_IA32_LASTINTTOIP,		.always = false },
	{ .index = MSR_INVALID,				.always = false },
};

/* enable NPT for AMD64 and X86 with PAE */
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
static bool npt_enabled = true;
#else
static bool npt_enabled;
#endif

/* allow nested paging (virtualized MMU) for all guests */
static int npt = true;
module_param(npt, int, S_IRUGO);

/* allow nested virtualization in KVM/SVM */
static int nested = true;
module_param(nested, int, S_IRUGO);

/* enable / disable AVIC */
static int avic;
#ifdef CONFIG_X86_LOCAL_APIC
module_param(avic, int, S_IRUGO);
#endif

/* AVIC VM ID bit masks and lock */
static DECLARE_BITMAP(avic_vm_id_bitmap, AVIC_VM_ID_NR);
static DEFINE_SPINLOCK(avic_vm_id_lock);

static void svm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
static void svm_flush_tlb(struct kvm_vcpu *vcpu);
static void svm_complete_interrupts(struct vcpu_svm *svm);

static int nested_svm_exit_handled(struct vcpu_svm *svm);
static int nested_svm_intercept(struct vcpu_svm *svm);
static int nested_svm_vmexit(struct vcpu_svm *svm);
static int nested_svm_check_exception(struct vcpu_svm *svm, unsigned nr,
				      bool has_error_code, u32 error_code);

enum {
	VMCB_INTERCEPTS, /* Intercept vectors, TSC offset,
			    pause filter count */
	VMCB_PERM_MAP,   /* IOPM Base and MSRPM Base */
	VMCB_ASID,	 /* ASID */
	VMCB_INTR,	 /* int_ctl, int_vector */
	VMCB_NPT,        /* npt_en, nCR3, gPAT */
	VMCB_CR,	 /* CR0, CR3, CR4, EFER */
	VMCB_DR,         /* DR6, DR7 */
	VMCB_DT,         /* GDT, IDT */
	VMCB_SEG,        /* CS, DS, SS, ES, CPL */
	VMCB_CR2,        /* CR2 only */
	VMCB_LBR,        /* DBGCTL, BR_FROM, BR_TO, LAST_EX_FROM, LAST_EX_TO */
	VMCB_AVIC,       /* AVIC APIC_BAR, AVIC APIC_BACKING_PAGE,
			  * AVIC PHYSICAL_TABLE pointer,
			  * AVIC LOGICAL_TABLE pointer
			  */
	VMCB_DIRTY_MAX,
};

/* TPR and CR2 are always written before VMRUN */
#define VMCB_ALWAYS_DIRTY_MASK	((1U << VMCB_INTR) | (1U << VMCB_CR2))

#define VMCB_AVIC_APIC_BAR_MASK		0xFFFFFFFFFF000ULL

static inline void mark_all_dirty(struct vmcb *vmcb)
{
	vmcb->control.clean = 0;
}

static inline void mark_all_clean(struct vmcb *vmcb)
{
	vmcb->control.clean = ((1 << VMCB_DIRTY_MAX) - 1)
			       & ~VMCB_ALWAYS_DIRTY_MASK;
}

static inline void mark_dirty(struct vmcb *vmcb, int bit)
{
	vmcb->control.clean &= ~(1 << bit);
}

static inline struct vcpu_svm *to_svm(struct kvm_vcpu *vcpu)
{
	return container_of(vcpu, struct vcpu_svm, vcpu);
}

static inline void avic_update_vapic_bar(struct vcpu_svm *svm, u64 data)
{
	svm->vmcb->control.avic_vapic_bar = data & VMCB_AVIC_APIC_BAR_MASK;
	mark_dirty(svm->vmcb, VMCB_AVIC);
}

static inline bool avic_vcpu_is_running(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 *entry = svm->avic_physical_id_cache;

	if (!entry)
		return false;

	return (READ_ONCE(*entry) & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK);
}

static void recalc_intercepts(struct vcpu_svm *svm)
{
	struct vmcb_control_area *c, *h;
	struct nested_state *g;

	mark_dirty(svm->vmcb, VMCB_INTERCEPTS);

	if (!is_guest_mode(&svm->vcpu))
		return;

	c = &svm->vmcb->control;
	h = &svm->nested.hsave->control;
	g = &svm->nested;

	c->intercept_cr = h->intercept_cr | g->intercept_cr;
	c->intercept_dr = h->intercept_dr | g->intercept_dr;
	c->intercept_exceptions = h->intercept_exceptions | g->intercept_exceptions;
	c->intercept = h->intercept | g->intercept;
}

static inline struct vmcb *get_host_vmcb(struct vcpu_svm *svm)
{
	if (is_guest_mode(&svm->vcpu))
		return svm->nested.hsave;
	else
		return svm->vmcb;
}

static inline void set_cr_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_cr |= (1U << bit);

	recalc_intercepts(svm);
}

static inline void clr_cr_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_cr &= ~(1U << bit);

	recalc_intercepts(svm);
}

static inline bool is_cr_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	return vmcb->control.intercept_cr & (1U << bit);
}

static inline void set_dr_intercepts(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_dr = (1 << INTERCEPT_DR0_READ)
		| (1 << INTERCEPT_DR1_READ)
		| (1 << INTERCEPT_DR2_READ)
		| (1 << INTERCEPT_DR3_READ)
		| (1 << INTERCEPT_DR4_READ)
		| (1 << INTERCEPT_DR5_READ)
		| (1 << INTERCEPT_DR6_READ)
		| (1 << INTERCEPT_DR7_READ)
		| (1 << INTERCEPT_DR0_WRITE)
		| (1 << INTERCEPT_DR1_WRITE)
		| (1 << INTERCEPT_DR2_WRITE)
		| (1 << INTERCEPT_DR3_WRITE)
		| (1 << INTERCEPT_DR4_WRITE)
		| (1 << INTERCEPT_DR5_WRITE)
		| (1 << INTERCEPT_DR6_WRITE)
		| (1 << INTERCEPT_DR7_WRITE);

	recalc_intercepts(svm);
}

static inline void clr_dr_intercepts(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_dr = 0;

	recalc_intercepts(svm);
}

static inline void set_exception_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_exceptions |= (1U << bit);

	recalc_intercepts(svm);
}

static inline void clr_exception_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept_exceptions &= ~(1U << bit);

	recalc_intercepts(svm);
}

static inline void set_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept |= (1ULL << bit);

	recalc_intercepts(svm);
}

static inline void clr_intercept(struct vcpu_svm *svm, int bit)
{
	struct vmcb *vmcb = get_host_vmcb(svm);

	vmcb->control.intercept &= ~(1ULL << bit);

	recalc_intercepts(svm);
}

static inline void enable_gif(struct vcpu_svm *svm)
{
	svm->vcpu.arch.hflags |= HF_GIF_MASK;
}

static inline void disable_gif(struct vcpu_svm *svm)
{
	svm->vcpu.arch.hflags &= ~HF_GIF_MASK;
}

static inline bool gif_set(struct vcpu_svm *svm)
{
	return !!(svm->vcpu.arch.hflags & HF_GIF_MASK);
}

static unsigned long iopm_base;

struct kvm_ldttss_desc {
	u16 limit0;
	u16 base0;
	unsigned base1:8, type:5, dpl:2, p:1;
	unsigned limit1:4, zero0:3, g:1, base2:8;
	u32 base3;
	u32 zero1;
} __attribute__((packed));

struct svm_cpu_data {
	int cpu;

	u64 asid_generation;
	u32 max_asid;
	u32 next_asid;
	struct kvm_ldttss_desc *tss_desc;

	struct page *save_area;
};

static DEFINE_PER_CPU(struct svm_cpu_data *, svm_data);

struct svm_init_data {
	int cpu;
	int r;
};

static const u32 msrpm_ranges[] = {0, 0xc0000000, 0xc0010000};

#define NUM_MSR_MAPS ARRAY_SIZE(msrpm_ranges)
#define MSRS_RANGE_SIZE 2048
#define MSRS_IN_RANGE (MSRS_RANGE_SIZE * 8 / 2)

static u32 svm_msrpm_offset(u32 msr)
{
	u32 offset;
	int i;

	for (i = 0; i < NUM_MSR_MAPS; i++) {
		if (msr < msrpm_ranges[i] ||
		    msr >= msrpm_ranges[i] + MSRS_IN_RANGE)
			continue;

		offset  = (msr - msrpm_ranges[i]) / 4; /* 4 msrs per u8 */
		offset += (i * MSRS_RANGE_SIZE);       /* add range offset */

		/* Now we have the u8 offset - but need the u32 offset */
		return offset / 4;
	}

	/* MSR not in any range */
	return MSR_INVALID;
}

#define MAX_INST_SIZE 15

static inline void clgi(void)
{
	asm volatile (__ex(SVM_CLGI));
}

static inline void stgi(void)
{
	asm volatile (__ex(SVM_STGI));
}

static inline void invlpga(unsigned long addr, u32 asid)
{
	asm volatile (__ex(SVM_INVLPGA) : : "a"(addr), "c"(asid));
}

static int get_npt_level(void)
{
#ifdef CONFIG_X86_64
	return PT64_ROOT_LEVEL;
#else
	return PT32E_ROOT_LEVEL;
#endif
}

static void svm_set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	vcpu->arch.efer = efer;
	if (!npt_enabled && !(efer & EFER_LMA))
		efer &= ~EFER_LME;

	to_svm(vcpu)->vmcb->save.efer = efer | EFER_SVME;
	mark_dirty(to_svm(vcpu)->vmcb, VMCB_CR);
}

static int is_external_interrupt(u32 info)
{
	info &= SVM_EVTINJ_TYPE_MASK | SVM_EVTINJ_VALID;
	return info == (SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR);
}

static u32 svm_get_interrupt_shadow(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 ret = 0;

	if (svm->vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK)
		ret = KVM_X86_SHADOW_INT_STI | KVM_X86_SHADOW_INT_MOV_SS;
	return ret;
}

static void svm_set_interrupt_shadow(struct kvm_vcpu *vcpu, int mask)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (mask == 0)
		svm->vmcb->control.int_state &= ~SVM_INTERRUPT_SHADOW_MASK;
	else
		svm->vmcb->control.int_state |= SVM_INTERRUPT_SHADOW_MASK;

}

static void skip_emulated_instruction(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm->vmcb->control.next_rip != 0) {
		WARN_ON_ONCE(!static_cpu_has(X86_FEATURE_NRIPS));
		svm->next_rip = svm->vmcb->control.next_rip;
	}

	if (!svm->next_rip) {
		if (emulate_instruction(vcpu, EMULTYPE_SKIP) !=
				EMULATE_DONE)
			printk(KERN_DEBUG "%s: NOP\n", __func__);
		return;
	}
	if (svm->next_rip - kvm_rip_read(vcpu) > MAX_INST_SIZE)
		printk(KERN_ERR "%s: ip 0x%lx next 0x%llx\n",
		       __func__, kvm_rip_read(vcpu), svm->next_rip);

	kvm_rip_write(vcpu, svm->next_rip);
	svm_set_interrupt_shadow(vcpu, 0);
}

static void svm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr,
				bool has_error_code, u32 error_code,
				bool reinject)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	/*
	 * If we are within a nested VM we'd better #VMEXIT and let the guest
	 * handle the exception
	 */
	if (!reinject &&
	    nested_svm_check_exception(svm, nr, has_error_code, error_code))
		return;

	if (nr == BP_VECTOR && !static_cpu_has(X86_FEATURE_NRIPS)) {
		unsigned long rip, old_rip = kvm_rip_read(&svm->vcpu);

		/*
		 * For guest debugging where we have to reinject #BP if some
		 * INT3 is guest-owned:
		 * Emulate nRIP by moving RIP forward. Will fail if injection
		 * raises a fault that is not intercepted. Still better than
		 * failing in all cases.
		 */
		skip_emulated_instruction(&svm->vcpu);
		rip = kvm_rip_read(&svm->vcpu);
		svm->int3_rip = rip + svm->vmcb->save.cs.base;
		svm->int3_injected = rip - old_rip;
	}

	svm->vmcb->control.event_inj = nr
		| SVM_EVTINJ_VALID
		| (has_error_code ? SVM_EVTINJ_VALID_ERR : 0)
		| SVM_EVTINJ_TYPE_EXEPT;
	svm->vmcb->control.event_inj_err = error_code;
}

static void svm_init_erratum_383(void)
{
	u32 low, high;
	int err;
	u64 val;

	if (!static_cpu_has_bug(X86_BUG_AMD_TLB_MMATCH))
		return;

	/* Use _safe variants to not break nested virtualization */
	val = native_read_msr_safe(MSR_AMD64_DC_CFG, &err);
	if (err)
		return;

	val |= (1ULL << 47);

	low  = lower_32_bits(val);
	high = upper_32_bits(val);

	native_write_msr_safe(MSR_AMD64_DC_CFG, low, high);

	erratum_383_found = true;
}

static void svm_init_osvw(struct kvm_vcpu *vcpu)
{
	/*
	 * Guests should see errata 400 and 415 as fixed (assuming that
	 * HLT and IO instructions are intercepted).
	 */
	vcpu->arch.osvw.length = (osvw_len >= 3) ? (osvw_len) : 3;
	vcpu->arch.osvw.status = osvw_status & ~(6ULL);

	/*
	 * By increasing VCPU's osvw.length to 3 we are telling the guest that
	 * all osvw.status bits inside that length, including bit 0 (which is
	 * reserved for erratum 298), are valid. However, if host processor's
	 * osvw_len is 0 then osvw_status[0] carries no information. We need to
	 * be conservative here and therefore we tell the guest that erratum 298
	 * is present (because we really don't know).
	 */
	if (osvw_len == 0 && boot_cpu_data.x86 == 0x10)
		vcpu->arch.osvw.status |= 1;
}

static int has_svm(void)
{
	const char *msg;

	if (!cpu_has_svm(&msg)) {
		printk(KERN_INFO "has_svm: %s\n", msg);
		return 0;
	}

	return 1;
}

static void svm_hardware_disable(void)
{
	/* Make sure we clean up behind us */
	if (static_cpu_has(X86_FEATURE_TSCRATEMSR))
		wrmsrl(MSR_AMD64_TSC_RATIO, TSC_RATIO_DEFAULT);

	cpu_svm_disable();

	amd_pmu_disable_virt();
}

static int svm_hardware_enable(void)
{

	struct svm_cpu_data *sd;
	uint64_t efer;
	struct desc_ptr gdt_descr;
	struct desc_struct *gdt;
	int me = raw_smp_processor_id();

	rdmsrl(MSR_EFER, efer);
	if (efer & EFER_SVME)
		return -EBUSY;

	if (!has_svm()) {
		pr_err("%s: err EOPNOTSUPP on %d\n", __func__, me);
		return -EINVAL;
	}
	sd = per_cpu(svm_data, me);
	if (!sd) {
		pr_err("%s: svm_data is NULL on %d\n", __func__, me);
		return -EINVAL;
	}

	sd->asid_generation = 1;
	sd->max_asid = cpuid_ebx(SVM_CPUID_FUNC) - 1;
	sd->next_asid = sd->max_asid + 1;

	native_store_gdt(&gdt_descr);
	gdt = (struct desc_struct *)gdt_descr.address;
	sd->tss_desc = (struct kvm_ldttss_desc *)(gdt + GDT_ENTRY_TSS);

	wrmsrl(MSR_EFER, efer | EFER_SVME);

	wrmsrl(MSR_VM_HSAVE_PA, page_to_pfn(sd->save_area) << PAGE_SHIFT);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		wrmsrl(MSR_AMD64_TSC_RATIO, TSC_RATIO_DEFAULT);
		__this_cpu_write(current_tsc_ratio, TSC_RATIO_DEFAULT);
	}


	/*
	 * Get OSVW bits.
	 *
	 * Note that it is possible to have a system with mixed processor
	 * revisions and therefore different OSVW bits. If bits are not the same
	 * on different processors then choose the worst case (i.e. if erratum
	 * is present on one processor and not on another then assume that the
	 * erratum is present everywhere).
	 */
	if (cpu_has(&boot_cpu_data, X86_FEATURE_OSVW)) {
		uint64_t len, status = 0;
		int err;

		len = native_read_msr_safe(MSR_AMD64_OSVW_ID_LENGTH, &err);
		if (!err)
			status = native_read_msr_safe(MSR_AMD64_OSVW_STATUS,
						      &err);

		if (err)
			osvw_status = osvw_len = 0;
		else {
			if (len < osvw_len)
				osvw_len = len;
			osvw_status |= status;
			osvw_status &= (1ULL << osvw_len) - 1;
		}
	} else
		osvw_status = osvw_len = 0;

	svm_init_erratum_383();

	amd_pmu_enable_virt();

	return 0;
}

static void svm_cpu_uninit(int cpu)
{
	struct svm_cpu_data *sd = per_cpu(svm_data, raw_smp_processor_id());

	if (!sd)
		return;

	per_cpu(svm_data, raw_smp_processor_id()) = NULL;
	__free_page(sd->save_area);
	kfree(sd);
}

static int svm_cpu_init(int cpu)
{
	struct svm_cpu_data *sd;
	int r;

	sd = kzalloc(sizeof(struct svm_cpu_data), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;
	sd->cpu = cpu;
	sd->save_area = alloc_page(GFP_KERNEL);
	r = -ENOMEM;
	if (!sd->save_area)
		goto err_1;

	per_cpu(svm_data, cpu) = sd;

	return 0;

err_1:
	kfree(sd);
	return r;

}

static bool valid_msr_intercept(u32 index)
{
	int i;

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++)
		if (direct_access_msrs[i].index == index)
			return true;

	return false;
}

static void set_msr_interception(u32 *msrpm, unsigned msr,
				 int read, int write)
{
	u8 bit_read, bit_write;
	unsigned long tmp;
	u32 offset;

	/*
	 * If this warning triggers extend the direct_access_msrs list at the
	 * beginning of the file
	 */
	WARN_ON(!valid_msr_intercept(msr));

	offset    = svm_msrpm_offset(msr);
	bit_read  = 2 * (msr & 0x0f);
	bit_write = 2 * (msr & 0x0f) + 1;
	tmp       = msrpm[offset];

	BUG_ON(offset == MSR_INVALID);

	read  ? clear_bit(bit_read,  &tmp) : set_bit(bit_read,  &tmp);
	write ? clear_bit(bit_write, &tmp) : set_bit(bit_write, &tmp);

	msrpm[offset] = tmp;
}

static void svm_vcpu_init_msrpm(u32 *msrpm)
{
	int i;

	memset(msrpm, 0xff, PAGE_SIZE * (1 << MSRPM_ALLOC_ORDER));

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++) {
		if (!direct_access_msrs[i].always)
			continue;

		set_msr_interception(msrpm, direct_access_msrs[i].index, 1, 1);
	}
}

static void add_msr_offset(u32 offset)
{
	int i;

	for (i = 0; i < MSRPM_OFFSETS; ++i) {

		/* Offset already in list? */
		if (msrpm_offsets[i] == offset)
			return;

		/* Slot used by another offset? */
		if (msrpm_offsets[i] != MSR_INVALID)
			continue;

		/* Add offset to list */
		msrpm_offsets[i] = offset;

		return;
	}

	/*
	 * If this BUG triggers the msrpm_offsets table has an overflow. Just
	 * increase MSRPM_OFFSETS in this case.
	 */
	BUG();
}

static void init_msrpm_offsets(void)
{
	int i;

	memset(msrpm_offsets, 0xff, sizeof(msrpm_offsets));

	for (i = 0; direct_access_msrs[i].index != MSR_INVALID; i++) {
		u32 offset;

		offset = svm_msrpm_offset(direct_access_msrs[i].index);
		BUG_ON(offset == MSR_INVALID);

		add_msr_offset(offset);
	}
}

static void svm_enable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.lbr_ctl = 1;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 1, 1);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 1, 1);
}

static void svm_disable_lbrv(struct vcpu_svm *svm)
{
	u32 *msrpm = svm->msrpm;

	svm->vmcb->control.lbr_ctl = 0;
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTBRANCHTOIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTFROMIP, 0, 0);
	set_msr_interception(msrpm, MSR_IA32_LASTINTTOIP, 0, 0);
}

/* Note:
 * This hash table is used to map VM_ID to a struct kvm_arch,
 * when handling AMD IOMMU GALOG notification to schedule in
 * a particular vCPU.
 */
#define SVM_VM_DATA_HASH_BITS	8
DECLARE_HASHTABLE(svm_vm_data_hash, SVM_VM_DATA_HASH_BITS);
static spinlock_t svm_vm_data_hash_lock;

/* Note:
 * This function is called from IOMMU driver to notify
 * SVM to schedule in a particular vCPU of a particular VM.
 */
static int avic_ga_log_notifier(u32 ga_tag)
{
	unsigned long flags;
	struct kvm_arch *ka = NULL;
	struct kvm_vcpu *vcpu = NULL;
	u32 vm_id = AVIC_GATAG_TO_VMID(ga_tag);
	u32 vcpu_id = AVIC_GATAG_TO_VCPUID(ga_tag);

	pr_debug("SVM: %s: vm_id=%#x, vcpu_id=%#x\n", __func__, vm_id, vcpu_id);

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_for_each_possible(svm_vm_data_hash, ka, hnode, vm_id) {
		struct kvm *kvm = container_of(ka, struct kvm, arch);
		struct kvm_arch *vm_data = &kvm->arch;

		if (vm_data->avic_vm_id != vm_id)
			continue;
		vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
		break;
	}
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);

	if (!vcpu)
		return 0;

	/* Note:
	 * At this point, the IOMMU should have already set the pending
	 * bit in the vAPIC backing page. So, we just need to schedule
	 * in the vcpu.
	 */
	if (vcpu->mode == OUTSIDE_GUEST_MODE)
		kvm_vcpu_wake_up(vcpu);

	return 0;
}

static __init int svm_hardware_setup(void)
{
	int cpu;
	struct page *iopm_pages;
	void *iopm_va;
	int r;

	iopm_pages = alloc_pages(GFP_KERNEL, IOPM_ALLOC_ORDER);

	if (!iopm_pages)
		return -ENOMEM;

	iopm_va = page_address(iopm_pages);
	memset(iopm_va, 0xff, PAGE_SIZE * (1 << IOPM_ALLOC_ORDER));
	iopm_base = page_to_pfn(iopm_pages) << PAGE_SHIFT;

	init_msrpm_offsets();

	if (boot_cpu_has(X86_FEATURE_NX))
		kvm_enable_efer_bits(EFER_NX);

	if (boot_cpu_has(X86_FEATURE_FXSR_OPT))
		kvm_enable_efer_bits(EFER_FFXSR);

	if (boot_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		kvm_has_tsc_control = true;
		kvm_max_tsc_scaling_ratio = TSC_RATIO_MAX;
		kvm_tsc_scaling_ratio_frac_bits = 32;
	}

	if (nested) {
		printk(KERN_INFO "kvm: Nested Virtualization enabled\n");
		kvm_enable_efer_bits(EFER_SVME | EFER_LMSLE);
	}

	for_each_possible_cpu(cpu) {
		r = svm_cpu_init(cpu);
		if (r)
			goto err;
	}

	if (!boot_cpu_has(X86_FEATURE_NPT))
		npt_enabled = false;

	if (npt_enabled && !npt) {
		printk(KERN_INFO "kvm: Nested Paging disabled\n");
		npt_enabled = false;
	}

	if (npt_enabled) {
		printk(KERN_INFO "kvm: Nested Paging enabled\n");
		kvm_enable_tdp();
	} else
		kvm_disable_tdp();

	if (avic) {
		if (!npt_enabled ||
		    !boot_cpu_has(X86_FEATURE_AVIC) ||
		    !IS_ENABLED(CONFIG_X86_LOCAL_APIC)) {
			avic = false;
		} else {
			pr_info("AVIC enabled\n");

			hash_init(svm_vm_data_hash);
			spin_lock_init(&svm_vm_data_hash_lock);
			amd_iommu_register_ga_log_notifier(&avic_ga_log_notifier);
		}
	}

	return 0;

err:
	__free_pages(iopm_pages, IOPM_ALLOC_ORDER);
	iopm_base = 0;
	return r;
}

static __exit void svm_hardware_unsetup(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		svm_cpu_uninit(cpu);

	__free_pages(pfn_to_page(iopm_base >> PAGE_SHIFT), IOPM_ALLOC_ORDER);
	iopm_base = 0;
}

static void init_seg(struct vmcb_seg *seg)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | SVM_SELECTOR_S_MASK |
		      SVM_SELECTOR_WRITE_MASK; /* Read/Write Data Segment */
	seg->limit = 0xffff;
	seg->base = 0;
}

static void init_sys_seg(struct vmcb_seg *seg, uint32_t type)
{
	seg->selector = 0;
	seg->attrib = SVM_SELECTOR_P_MASK | type;
	seg->limit = 0xffff;
	seg->base = 0;
}

static void svm_write_tsc_offset(struct kvm_vcpu *vcpu, u64 offset)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 g_tsc_offset = 0;

	if (is_guest_mode(vcpu)) {
		g_tsc_offset = svm->vmcb->control.tsc_offset -
			       svm->nested.hsave->control.tsc_offset;
		svm->nested.hsave->control.tsc_offset = offset;
	} else
		trace_kvm_write_tsc_offset(vcpu->vcpu_id,
					   svm->vmcb->control.tsc_offset,
					   offset);

	svm->vmcb->control.tsc_offset = offset + g_tsc_offset;

	mark_dirty(svm->vmcb, VMCB_INTERCEPTS);
}

static void avic_init_vmcb(struct vcpu_svm *svm)
{
	struct vmcb *vmcb = svm->vmcb;
	struct kvm_arch *vm_data = &svm->vcpu.kvm->arch;
	phys_addr_t bpa = page_to_phys(svm->avic_backing_page);
	phys_addr_t lpa = page_to_phys(vm_data->avic_logical_id_table_page);
	phys_addr_t ppa = page_to_phys(vm_data->avic_physical_id_table_page);

	vmcb->control.avic_backing_page = bpa & AVIC_HPA_MASK;
	vmcb->control.avic_logical_id = lpa & AVIC_HPA_MASK;
	vmcb->control.avic_physical_id = ppa & AVIC_HPA_MASK;
	vmcb->control.avic_physical_id |= AVIC_MAX_PHYSICAL_ID_COUNT;
	vmcb->control.int_ctl |= AVIC_ENABLE_MASK;
	svm->vcpu.arch.apicv_active = true;
}

static void init_vmcb(struct vcpu_svm *svm)
{
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct vmcb_save_area *save = &svm->vmcb->save;

	svm->vcpu.fpu_active = 1;
	svm->vcpu.arch.hflags = 0;

	set_cr_intercept(svm, INTERCEPT_CR0_READ);
	set_cr_intercept(svm, INTERCEPT_CR3_READ);
	set_cr_intercept(svm, INTERCEPT_CR4_READ);
	set_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	set_cr_intercept(svm, INTERCEPT_CR3_WRITE);
	set_cr_intercept(svm, INTERCEPT_CR4_WRITE);
	if (!kvm_vcpu_apicv_active(&svm->vcpu))
		set_cr_intercept(svm, INTERCEPT_CR8_WRITE);

	set_dr_intercepts(svm);

	set_exception_intercept(svm, PF_VECTOR);
	set_exception_intercept(svm, UD_VECTOR);
	set_exception_intercept(svm, MC_VECTOR);
	set_exception_intercept(svm, AC_VECTOR);
	set_exception_intercept(svm, DB_VECTOR);

	set_intercept(svm, INTERCEPT_INTR);
	set_intercept(svm, INTERCEPT_NMI);
	set_intercept(svm, INTERCEPT_SMI);
	set_intercept(svm, INTERCEPT_SELECTIVE_CR0);
	set_intercept(svm, INTERCEPT_RDPMC);
	set_intercept(svm, INTERCEPT_CPUID);
	set_intercept(svm, INTERCEPT_INVD);
	set_intercept(svm, INTERCEPT_HLT);
	set_intercept(svm, INTERCEPT_INVLPG);
	set_intercept(svm, INTERCEPT_INVLPGA);
	set_intercept(svm, INTERCEPT_IOIO_PROT);
	set_intercept(svm, INTERCEPT_MSR_PROT);
	set_intercept(svm, INTERCEPT_TASK_SWITCH);
	set_intercept(svm, INTERCEPT_SHUTDOWN);
	set_intercept(svm, INTERCEPT_VMRUN);
	set_intercept(svm, INTERCEPT_VMMCALL);
	set_intercept(svm, INTERCEPT_VMLOAD);
	set_intercept(svm, INTERCEPT_VMSAVE);
	set_intercept(svm, INTERCEPT_STGI);
	set_intercept(svm, INTERCEPT_CLGI);
	set_intercept(svm, INTERCEPT_SKINIT);
	set_intercept(svm, INTERCEPT_WBINVD);
	set_intercept(svm, INTERCEPT_MONITOR);
	set_intercept(svm, INTERCEPT_MWAIT);
	set_intercept(svm, INTERCEPT_XSETBV);

	control->iopm_base_pa = iopm_base;
	control->msrpm_base_pa = __pa(svm->msrpm);
	control->int_ctl = V_INTR_MASKING_MASK;

	init_seg(&save->es);
	init_seg(&save->ss);
	init_seg(&save->ds);
	init_seg(&save->fs);
	init_seg(&save->gs);

	save->cs.selector = 0xf000;
	save->cs.base = 0xffff0000;
	/* Executable/Readable Code Segment */
	save->cs.attrib = SVM_SELECTOR_READ_MASK | SVM_SELECTOR_P_MASK |
		SVM_SELECTOR_S_MASK | SVM_SELECTOR_CODE_MASK;
	save->cs.limit = 0xffff;

	save->gdtr.limit = 0xffff;
	save->idtr.limit = 0xffff;

	init_sys_seg(&save->ldtr, SEG_TYPE_LDT);
	init_sys_seg(&save->tr, SEG_TYPE_BUSY_TSS16);

	svm_set_efer(&svm->vcpu, 0);
	save->dr6 = 0xffff0ff0;
	kvm_set_rflags(&svm->vcpu, 2);
	save->rip = 0x0000fff0;
	svm->vcpu.arch.regs[VCPU_REGS_RIP] = save->rip;

	/*
	 * svm_set_cr0() sets PG and WP and clears NW and CD on save->cr0.
	 * It also updates the guest-visible cr0 value.
	 */
	svm_set_cr0(&svm->vcpu, X86_CR0_NW | X86_CR0_CD | X86_CR0_ET);
	kvm_mmu_reset_context(&svm->vcpu);

	save->cr4 = X86_CR4_PAE;
	/* rdx = ?? */

	if (npt_enabled) {
		/* Setup VMCB for Nested Paging */
		control->nested_ctl = 1;
		clr_intercept(svm, INTERCEPT_INVLPG);
		clr_exception_intercept(svm, PF_VECTOR);
		clr_cr_intercept(svm, INTERCEPT_CR3_READ);
		clr_cr_intercept(svm, INTERCEPT_CR3_WRITE);
		save->g_pat = svm->vcpu.arch.pat;
		save->cr3 = 0;
		save->cr4 = 0;
	}
	svm->asid_generation = 0;

	svm->nested.vmcb = 0;
	svm->vcpu.arch.hflags = 0;

	if (boot_cpu_has(X86_FEATURE_PAUSEFILTER)) {
		control->pause_filter_count = 3000;
		set_intercept(svm, INTERCEPT_PAUSE);
	}

	if (avic)
		avic_init_vmcb(svm);

	mark_all_dirty(svm->vmcb);

	enable_gif(svm);

}

static u64 *avic_get_physical_id_entry(struct kvm_vcpu *vcpu, int index)
{
	u64 *avic_physical_id_table;
	struct kvm_arch *vm_data = &vcpu->kvm->arch;

	if (index >= AVIC_MAX_PHYSICAL_ID_COUNT)
		return NULL;

	avic_physical_id_table = page_address(vm_data->avic_physical_id_table_page);

	return &avic_physical_id_table[index];
}

/**
 * Note:
 * AVIC hardware walks the nested page table to check permissions,
 * but does not use the SPA address specified in the leaf page
 * table entry since it uses  address in the AVIC_BACKING_PAGE pointer
 * field of the VMCB. Therefore, we set up the
 * APIC_ACCESS_PAGE_PRIVATE_MEMSLOT (4KB) here.
 */
static int avic_init_access_page(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	int ret;

	if (kvm->arch.apic_access_page_done)
		return 0;

	ret = x86_set_memory_region(kvm,
				    APIC_ACCESS_PAGE_PRIVATE_MEMSLOT,
				    APIC_DEFAULT_PHYS_BASE,
				    PAGE_SIZE);
	if (ret)
		return ret;

	kvm->arch.apic_access_page_done = true;
	return 0;
}

static int avic_init_backing_page(struct kvm_vcpu *vcpu)
{
	int ret;
	u64 *entry, new_entry;
	int id = vcpu->vcpu_id;
	struct vcpu_svm *svm = to_svm(vcpu);

	ret = avic_init_access_page(vcpu);
	if (ret)
		return ret;

	if (id >= AVIC_MAX_PHYSICAL_ID_COUNT)
		return -EINVAL;

	if (!svm->vcpu.arch.apic->regs)
		return -EINVAL;

	svm->avic_backing_page = virt_to_page(svm->vcpu.arch.apic->regs);

	/* Setting AVIC backing page address in the phy APIC ID table */
	entry = avic_get_physical_id_entry(vcpu, id);
	if (!entry)
		return -EINVAL;

	new_entry = READ_ONCE(*entry);
	new_entry = (page_to_phys(svm->avic_backing_page) &
		     AVIC_PHYSICAL_ID_ENTRY_BACKING_PAGE_MASK) |
		     AVIC_PHYSICAL_ID_ENTRY_VALID_MASK;
	WRITE_ONCE(*entry, new_entry);

	svm->avic_physical_id_cache = entry;

	return 0;
}

static inline int avic_get_next_vm_id(void)
{
	int id;

	spin_lock(&avic_vm_id_lock);

	/* AVIC VM ID is one-based. */
	id = find_next_zero_bit(avic_vm_id_bitmap, AVIC_VM_ID_NR, 1);
	if (id <= AVIC_VM_ID_MASK)
		__set_bit(id, avic_vm_id_bitmap);
	else
		id = -EAGAIN;

	spin_unlock(&avic_vm_id_lock);
	return id;
}

static inline int avic_free_vm_id(int id)
{
	if (id <= 0 || id > AVIC_VM_ID_MASK)
		return -EINVAL;

	spin_lock(&avic_vm_id_lock);
	__clear_bit(id, avic_vm_id_bitmap);
	spin_unlock(&avic_vm_id_lock);
	return 0;
}

static void avic_vm_destroy(struct kvm *kvm)
{
	unsigned long flags;
	struct kvm_arch *vm_data = &kvm->arch;

	avic_free_vm_id(vm_data->avic_vm_id);

	if (vm_data->avic_logical_id_table_page)
		__free_page(vm_data->avic_logical_id_table_page);
	if (vm_data->avic_physical_id_table_page)
		__free_page(vm_data->avic_physical_id_table_page);

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_del(&vm_data->hnode);
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);
}

static int avic_vm_init(struct kvm *kvm)
{
	unsigned long flags;
	int vm_id, err = -ENOMEM;
	struct kvm_arch *vm_data = &kvm->arch;
	struct page *p_page;
	struct page *l_page;

	if (!avic)
		return 0;

	vm_id = avic_get_next_vm_id();
	if (vm_id < 0)
		return vm_id;
	vm_data->avic_vm_id = (u32)vm_id;

	/* Allocating physical APIC ID table (4KB) */
	p_page = alloc_page(GFP_KERNEL);
	if (!p_page)
		goto free_avic;

	vm_data->avic_physical_id_table_page = p_page;
	clear_page(page_address(p_page));

	/* Allocating logical APIC ID table (4KB) */
	l_page = alloc_page(GFP_KERNEL);
	if (!l_page)
		goto free_avic;

	vm_data->avic_logical_id_table_page = l_page;
	clear_page(page_address(l_page));

	spin_lock_irqsave(&svm_vm_data_hash_lock, flags);
	hash_add(svm_vm_data_hash, &vm_data->hnode, vm_data->avic_vm_id);
	spin_unlock_irqrestore(&svm_vm_data_hash_lock, flags);

	return 0;

free_avic:
	avic_vm_destroy(kvm);
	return err;
}

static inline int
avic_update_iommu_vcpu_affinity(struct kvm_vcpu *vcpu, int cpu, bool r)
{
	int ret = 0;
	unsigned long flags;
	struct amd_svm_iommu_ir *ir;
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!kvm_arch_has_assigned_device(vcpu->kvm))
		return 0;

	/*
	 * Here, we go through the per-vcpu ir_list to update all existing
	 * interrupt remapping table entry targeting this vcpu.
	 */
	spin_lock_irqsave(&svm->ir_list_lock, flags);

	if (list_empty(&svm->ir_list))
		goto out;

	list_for_each_entry(ir, &svm->ir_list, node) {
		ret = amd_iommu_update_ga(cpu, r, ir->data);
		if (ret)
			break;
	}
out:
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
	return ret;
}

static void avic_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	u64 entry;
	/* ID = 0xff (broadcast), ID > 0xff (reserved) */
	int h_physical_id = kvm_cpu_get_apicid(cpu);
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	if (WARN_ON(h_physical_id >= AVIC_MAX_PHYSICAL_ID_COUNT))
		return;

	entry = READ_ONCE(*(svm->avic_physical_id_cache));
	WARN_ON(entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK);

	entry &= ~AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK;
	entry |= (h_physical_id & AVIC_PHYSICAL_ID_ENTRY_HOST_PHYSICAL_ID_MASK);

	entry &= ~AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;
	if (svm->avic_is_running)
		entry |= AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;

	WRITE_ONCE(*(svm->avic_physical_id_cache), entry);
	avic_update_iommu_vcpu_affinity(vcpu, h_physical_id,
					svm->avic_is_running);
}

static void avic_vcpu_put(struct kvm_vcpu *vcpu)
{
	u64 entry;
	struct vcpu_svm *svm = to_svm(vcpu);

	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	entry = READ_ONCE(*(svm->avic_physical_id_cache));
	if (entry & AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK)
		avic_update_iommu_vcpu_affinity(vcpu, -1, 0);

	entry &= ~AVIC_PHYSICAL_ID_ENTRY_IS_RUNNING_MASK;
	WRITE_ONCE(*(svm->avic_physical_id_cache), entry);
}

/**
 * This function is called during VCPU halt/unhalt.
 */
static void avic_set_running(struct kvm_vcpu *vcpu, bool is_run)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->avic_is_running = is_run;
	if (is_run)
		avic_vcpu_load(vcpu, vcpu->cpu);
	else
		avic_vcpu_put(vcpu);
}

static void svm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 dummy;
	u32 eax = 1;

	if (!init_event) {
		svm->vcpu.arch.apic_base = APIC_DEFAULT_PHYS_BASE |
					   MSR_IA32_APICBASE_ENABLE;
		if (kvm_vcpu_is_reset_bsp(&svm->vcpu))
			svm->vcpu.arch.apic_base |= MSR_IA32_APICBASE_BSP;
	}
	init_vmcb(svm);

	kvm_cpuid(vcpu, &eax, &dummy, &dummy, &dummy);
	kvm_register_write(vcpu, VCPU_REGS_RDX, eax);

	if (kvm_vcpu_apicv_active(vcpu) && !init_event)
		avic_update_vapic_bar(svm, APIC_DEFAULT_PHYS_BASE);
}

static struct kvm_vcpu *svm_create_vcpu(struct kvm *kvm, unsigned int id)
{
	struct vcpu_svm *svm;
	struct page *page;
	struct page *msrpm_pages;
	struct page *hsave_page;
	struct page *nested_msrpm_pages;
	int err;

	svm = kmem_cache_zalloc(kvm_vcpu_cache, GFP_KERNEL);
	if (!svm) {
		err = -ENOMEM;
		goto out;
	}

	err = kvm_vcpu_init(&svm->vcpu, kvm, id);
	if (err)
		goto free_svm;

	err = -ENOMEM;
	page = alloc_page(GFP_KERNEL);
	if (!page)
		goto uninit;

	msrpm_pages = alloc_pages(GFP_KERNEL, MSRPM_ALLOC_ORDER);
	if (!msrpm_pages)
		goto free_page1;

	nested_msrpm_pages = alloc_pages(GFP_KERNEL, MSRPM_ALLOC_ORDER);
	if (!nested_msrpm_pages)
		goto free_page2;

	hsave_page = alloc_page(GFP_KERNEL);
	if (!hsave_page)
		goto free_page3;

	if (avic) {
		err = avic_init_backing_page(&svm->vcpu);
		if (err)
			goto free_page4;

		INIT_LIST_HEAD(&svm->ir_list);
		spin_lock_init(&svm->ir_list_lock);
	}

	/* We initialize this flag to true to make sure that the is_running
	 * bit would be set the first time the vcpu is loaded.
	 */
	svm->avic_is_running = true;

	svm->nested.hsave = page_address(hsave_page);

	svm->msrpm = page_address(msrpm_pages);
	svm_vcpu_init_msrpm(svm->msrpm);

	svm->nested.msrpm = page_address(nested_msrpm_pages);
	svm_vcpu_init_msrpm(svm->nested.msrpm);

	svm->vmcb = page_address(page);
	clear_page(svm->vmcb);
	svm->vmcb_pa = page_to_pfn(page) << PAGE_SHIFT;
	svm->asid_generation = 0;
	init_vmcb(svm);

	svm_init_osvw(&svm->vcpu);

	return &svm->vcpu;

free_page4:
	__free_page(hsave_page);
free_page3:
	__free_pages(nested_msrpm_pages, MSRPM_ALLOC_ORDER);
free_page2:
	__free_pages(msrpm_pages, MSRPM_ALLOC_ORDER);
free_page1:
	__free_page(page);
uninit:
	kvm_vcpu_uninit(&svm->vcpu);
free_svm:
	kmem_cache_free(kvm_vcpu_cache, svm);
out:
	return ERR_PTR(err);
}

static void svm_free_vcpu(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	__free_page(pfn_to_page(svm->vmcb_pa >> PAGE_SHIFT));
	__free_pages(virt_to_page(svm->msrpm), MSRPM_ALLOC_ORDER);
	__free_page(virt_to_page(svm->nested.hsave));
	__free_pages(virt_to_page(svm->nested.msrpm), MSRPM_ALLOC_ORDER);
	kvm_vcpu_uninit(vcpu);
	kmem_cache_free(kvm_vcpu_cache, svm);
}

static void svm_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int i;

	if (unlikely(cpu != vcpu->cpu)) {
		svm->asid_generation = 0;
		mark_all_dirty(svm->vmcb);
	}

#ifdef CONFIG_X86_64
	rdmsrl(MSR_GS_BASE, to_svm(vcpu)->host.gs_base);
#endif
	savesegment(fs, svm->host.fs);
	savesegment(gs, svm->host.gs);
	svm->host.ldt = kvm_read_ldt();

	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		rdmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);

	if (static_cpu_has(X86_FEATURE_TSCRATEMSR)) {
		u64 tsc_ratio = vcpu->arch.tsc_scaling_ratio;
		if (tsc_ratio != __this_cpu_read(current_tsc_ratio)) {
			__this_cpu_write(current_tsc_ratio, tsc_ratio);
			wrmsrl(MSR_AMD64_TSC_RATIO, tsc_ratio);
		}
	}
	/* This assumes that the kernel never uses MSR_TSC_AUX */
	if (static_cpu_has(X86_FEATURE_RDTSCP))
		wrmsrl(MSR_TSC_AUX, svm->tsc_aux);

	avic_vcpu_load(vcpu, cpu);
}

static void svm_vcpu_put(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int i;

	avic_vcpu_put(vcpu);

	++vcpu->stat.host_state_reload;
	kvm_load_ldt(svm->host.ldt);
#ifdef CONFIG_X86_64
	loadsegment(fs, svm->host.fs);
	wrmsrl(MSR_KERNEL_GS_BASE, current->thread.gsbase);
	load_gs_index(svm->host.gs);
#else
#ifdef CONFIG_X86_32_LAZY_GS
	loadsegment(gs, svm->host.gs);
#endif
#endif
	for (i = 0; i < NR_HOST_SAVE_USER_MSRS; i++)
		wrmsrl(host_save_user_msrs[i], svm->host_user_msrs[i]);
}

static void svm_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	avic_set_running(vcpu, false);
}

static void svm_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	avic_set_running(vcpu, true);
}

static unsigned long svm_get_rflags(struct kvm_vcpu *vcpu)
{
	return to_svm(vcpu)->vmcb->save.rflags;
}

static void svm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags)
{
       /*
        * Any change of EFLAGS.VM is accompanied by a reload of SS
        * (caused by either a task switch or an inter-privilege IRET),
        * so we do not need to update the CPL here.
        */
	to_svm(vcpu)->vmcb->save.rflags = rflags;
}

static u32 svm_get_pkru(struct kvm_vcpu *vcpu)
{
	return 0;
}

static void svm_cache_reg(struct kvm_vcpu *vcpu, enum kvm_reg reg)
{
	switch (reg) {
	case VCPU_EXREG_PDPTR:
		BUG_ON(!npt_enabled);
		load_pdptrs(vcpu, vcpu->arch.walk_mmu, kvm_read_cr3(vcpu));
		break;
	default:
		BUG();
	}
}

static void svm_set_vintr(struct vcpu_svm *svm)
{
	set_intercept(svm, INTERCEPT_VINTR);
}

static void svm_clear_vintr(struct vcpu_svm *svm)
{
	clr_intercept(svm, INTERCEPT_VINTR);
}

static struct vmcb_seg *svm_seg(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	switch (seg) {
	case VCPU_SREG_CS: return &save->cs;
	case VCPU_SREG_DS: return &save->ds;
	case VCPU_SREG_ES: return &save->es;
	case VCPU_SREG_FS: return &save->fs;
	case VCPU_SREG_GS: return &save->gs;
	case VCPU_SREG_SS: return &save->ss;
	case VCPU_SREG_TR: return &save->tr;
	case VCPU_SREG_LDTR: return &save->ldtr;
	}
	BUG();
	return NULL;
}

static u64 svm_get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	return s->base;
}

static void svm_get_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	var->base = s->base;
	var->limit = s->limit;
	var->selector = s->selector;
	var->type = s->attrib & SVM_SELECTOR_TYPE_MASK;
	var->s = (s->attrib >> SVM_SELECTOR_S_SHIFT) & 1;
	var->dpl = (s->attrib >> SVM_SELECTOR_DPL_SHIFT) & 3;
	var->present = (s->attrib >> SVM_SELECTOR_P_SHIFT) & 1;
	var->avl = (s->attrib >> SVM_SELECTOR_AVL_SHIFT) & 1;
	var->l = (s->attrib >> SVM_SELECTOR_L_SHIFT) & 1;
	var->db = (s->attrib >> SVM_SELECTOR_DB_SHIFT) & 1;

	/*
	 * AMD CPUs circa 2014 track the G bit for all segments except CS.
	 * However, the SVM spec states that the G bit is not observed by the
	 * CPU, and some VMware virtual CPUs drop the G bit for all segments.
	 * So let's synthesize a legal G bit for all segments, this helps
	 * running KVM nested. It also helps cross-vendor migration, because
	 * Intel's vmentry has a check on the 'G' bit.
	 */
	var->g = s->limit > 0xfffff;

	/*
	 * AMD's VMCB does not have an explicit unusable field, so emulate it
	 * for cross vendor migration purposes by "not present"
	 */
	var->unusable = !var->present || (var->type == 0);

	switch (seg) {
	case VCPU_SREG_TR:
		/*
		 * Work around a bug where the busy flag in the tr selector
		 * isn't exposed
		 */
		var->type |= 0x2;
		break;
	case VCPU_SREG_DS:
	case VCPU_SREG_ES:
	case VCPU_SREG_FS:
	case VCPU_SREG_GS:
		/*
		 * The accessed bit must always be set in the segment
		 * descriptor cache, although it can be cleared in the
		 * descriptor, the cached bit always remains at 1. Since
		 * Intel has a check on this, set it here to support
		 * cross-vendor migration.
		 */
		if (!var->unusable)
			var->type |= 0x1;
		break;
	case VCPU_SREG_SS:
		/*
		 * On AMD CPUs sometimes the DB bit in the segment
		 * descriptor is left as 1, although the whole segment has
		 * been made unusable. Clear it here to pass an Intel VMX
		 * entry check when cross vendor migrating.
		 */
		if (var->unusable)
			var->db = 0;
		var->dpl = to_svm(vcpu)->vmcb->save.cpl;
		break;
	}
}

static int svm_get_cpl(struct kvm_vcpu *vcpu)
{
	struct vmcb_save_area *save = &to_svm(vcpu)->vmcb->save;

	return save->cpl;
}

static void svm_get_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->size = svm->vmcb->save.idtr.limit;
	dt->address = svm->vmcb->save.idtr.base;
}

static void svm_set_idt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.idtr.limit = dt->size;
	svm->vmcb->save.idtr.base = dt->address ;
	mark_dirty(svm->vmcb, VMCB_DT);
}

static void svm_get_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	dt->size = svm->vmcb->save.gdtr.limit;
	dt->address = svm->vmcb->save.gdtr.base;
}

static void svm_set_gdt(struct kvm_vcpu *vcpu, struct desc_ptr *dt)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.gdtr.limit = dt->size;
	svm->vmcb->save.gdtr.base = dt->address ;
	mark_dirty(svm->vmcb, VMCB_DT);
}

static void svm_decache_cr0_guest_bits(struct kvm_vcpu *vcpu)
{
}

static void svm_decache_cr3(struct kvm_vcpu *vcpu)
{
}

static void svm_decache_cr4_guest_bits(struct kvm_vcpu *vcpu)
{
}

static void update_cr0_intercept(struct vcpu_svm *svm)
{
	ulong gcr0 = svm->vcpu.arch.cr0;
	u64 *hcr0 = &svm->vmcb->save.cr0;

	if (!svm->vcpu.fpu_active)
		*hcr0 |= SVM_CR0_SELECTIVE_MASK;
	else
		*hcr0 = (*hcr0 & ~SVM_CR0_SELECTIVE_MASK)
			| (gcr0 & SVM_CR0_SELECTIVE_MASK);

	mark_dirty(svm->vmcb, VMCB_CR);

	if (gcr0 == *hcr0 && svm->vcpu.fpu_active) {
		clr_cr_intercept(svm, INTERCEPT_CR0_READ);
		clr_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	} else {
		set_cr_intercept(svm, INTERCEPT_CR0_READ);
		set_cr_intercept(svm, INTERCEPT_CR0_WRITE);
	}
}

static void svm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	struct vcpu_svm *svm = to_svm(vcpu);

#ifdef CONFIG_X86_64
	if (vcpu->arch.efer & EFER_LME) {
		if (!is_paging(vcpu) && (cr0 & X86_CR0_PG)) {
			vcpu->arch.efer |= EFER_LMA;
			svm->vmcb->save.efer |= EFER_LMA | EFER_LME;
		}

		if (is_paging(vcpu) && !(cr0 & X86_CR0_PG)) {
			vcpu->arch.efer &= ~EFER_LMA;
			svm->vmcb->save.efer &= ~(EFER_LMA | EFER_LME);
		}
	}
#endif
	vcpu->arch.cr0 = cr0;

	if (!npt_enabled)
		cr0 |= X86_CR0_PG | X86_CR0_WP;

	if (!vcpu->fpu_active)
		cr0 |= X86_CR0_TS;
	/*
	 * re-enable caching here because the QEMU bios
	 * does not do it - this results in some delay at
	 * reboot
	 */
	if (kvm_check_has_quirk(vcpu->kvm, KVM_X86_QUIRK_CD_NW_CLEARED))
		cr0 &= ~(X86_CR0_CD | X86_CR0_NW);
	svm->vmcb->save.cr0 = cr0;
	mark_dirty(svm->vmcb, VMCB_CR);
	update_cr0_intercept(svm);
}

static int svm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	unsigned long host_cr4_mce = cr4_read_shadow() & X86_CR4_MCE;
	unsigned long old_cr4 = to_svm(vcpu)->vmcb->save.cr4;

	if (cr4 & X86_CR4_VMXE)
		return 1;

	if (npt_enabled && ((old_cr4 ^ cr4) & X86_CR4_PGE))
		svm_flush_tlb(vcpu);

	vcpu->arch.cr4 = cr4;
	if (!npt_enabled)
		cr4 |= X86_CR4_PAE;
	cr4 |= host_cr4_mce;
	to_svm(vcpu)->vmcb->save.cr4 = cr4;
	mark_dirty(to_svm(vcpu)->vmcb, VMCB_CR);
	return 0;
}

static void svm_set_segment(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_seg *s = svm_seg(vcpu, seg);

	s->base = var->base;
	s->limit = var->limit;
	s->selector = var->selector;
	if (var->unusable)
		s->attrib = 0;
	else {
		s->attrib = (var->type & SVM_SELECTOR_TYPE_MASK);
		s->attrib |= (var->s & 1) << SVM_SELECTOR_S_SHIFT;
		s->attrib |= (var->dpl & 3) << SVM_SELECTOR_DPL_SHIFT;
		s->attrib |= (var->present & 1) << SVM_SELECTOR_P_SHIFT;
		s->attrib |= (var->avl & 1) << SVM_SELECTOR_AVL_SHIFT;
		s->attrib |= (var->l & 1) << SVM_SELECTOR_L_SHIFT;
		s->attrib |= (var->db & 1) << SVM_SELECTOR_DB_SHIFT;
		s->attrib |= (var->g & 1) << SVM_SELECTOR_G_SHIFT;
	}

	/*
	 * This is always accurate, except if SYSRET returned to a segment
	 * with SS.DPL != 3.  Intel does not have this quirk, and always
	 * forces SS.DPL to 3 on sysret, so we ignore that case; fixing it
	 * would entail passing the CPL to userspace and back.
	 */
	if (seg == VCPU_SREG_SS)
		svm->vmcb->save.cpl = (s->attrib >> SVM_SELECTOR_DPL_SHIFT) & 3;

	mark_dirty(svm->vmcb, VMCB_SEG);
}

static void update_bp_intercept(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	clr_exception_intercept(svm, BP_VECTOR);

	if (vcpu->guest_debug & KVM_GUESTDBG_ENABLE) {
		if (vcpu->guest_debug & KVM_GUESTDBG_USE_SW_BP)
			set_exception_intercept(svm, BP_VECTOR);
	} else
		vcpu->guest_debug = 0;
}

static void new_asid(struct vcpu_svm *svm, struct svm_cpu_data *sd)
{
	if (sd->next_asid > sd->max_asid) {
		++sd->asid_generation;
		sd->next_asid = 1;
		svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ALL_ASID;
	}

	svm->asid_generation = sd->asid_generation;
	svm->vmcb->control.asid = sd->next_asid++;

	mark_dirty(svm->vmcb, VMCB_ASID);
}

static u64 svm_get_dr6(struct kvm_vcpu *vcpu)
{
	return to_svm(vcpu)->vmcb->save.dr6;
}

static void svm_set_dr6(struct kvm_vcpu *vcpu, unsigned long value)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.dr6 = value;
	mark_dirty(svm->vmcb, VMCB_DR);
}

static void svm_sync_dirty_debug_regs(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	get_debugreg(vcpu->arch.db[0], 0);
	get_debugreg(vcpu->arch.db[1], 1);
	get_debugreg(vcpu->arch.db[2], 2);
	get_debugreg(vcpu->arch.db[3], 3);
	vcpu->arch.dr6 = svm_get_dr6(vcpu);
	vcpu->arch.dr7 = svm->vmcb->save.dr7;

	vcpu->arch.switch_db_regs &= ~KVM_DEBUGREG_WONT_EXIT;
	set_dr_intercepts(svm);
}

static void svm_set_dr7(struct kvm_vcpu *vcpu, unsigned long value)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.dr7 = value;
	mark_dirty(svm->vmcb, VMCB_DR);
}

static int pf_interception(struct vcpu_svm *svm)
{
	u64 fault_address = svm->vmcb->control.exit_info_2;
	u32 error_code;
	int r = 1;

	switch (svm->apf_reason) {
	default:
		error_code = svm->vmcb->control.exit_info_1;

		trace_kvm_page_fault(fault_address, error_code);
		if (!npt_enabled && kvm_event_needs_reinjection(&svm->vcpu))
			kvm_mmu_unprotect_page_virt(&svm->vcpu, fault_address);
		r = kvm_mmu_page_fault(&svm->vcpu, fault_address, error_code,
			svm->vmcb->control.insn_bytes,
			svm->vmcb->control.insn_len);
		break;
	case KVM_PV_REASON_PAGE_NOT_PRESENT:
		svm->apf_reason = 0;
		local_irq_disable();
		kvm_async_pf_task_wait(fault_address);
		local_irq_enable();
		break;
	case KVM_PV_REASON_PAGE_READY:
		svm->apf_reason = 0;
		local_irq_disable();
		kvm_async_pf_task_wake(fault_address);
		local_irq_enable();
		break;
	}
	return r;
}

static int db_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;

	if (!(svm->vcpu.guest_debug &
	      (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP)) &&
		!svm->nmi_singlestep) {
		kvm_queue_exception(&svm->vcpu, DB_VECTOR);
		return 1;
	}

	if (svm->nmi_singlestep) {
		svm->nmi_singlestep = false;
		if (!(svm->vcpu.guest_debug & KVM_GUESTDBG_SINGLESTEP))
			svm->vmcb->save.rflags &=
				~(X86_EFLAGS_TF | X86_EFLAGS_RF);
	}

	if (svm->vcpu.guest_debug &
	    (KVM_GUESTDBG_SINGLESTEP | KVM_GUESTDBG_USE_HW_BP)) {
		kvm_run->exit_reason = KVM_EXIT_DEBUG;
		kvm_run->debug.arch.pc =
			svm->vmcb->save.cs.base + svm->vmcb->save.rip;
		kvm_run->debug.arch.exception = DB_VECTOR;
		return 0;
	}

	return 1;
}

static int bp_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;

	kvm_run->exit_reason = KVM_EXIT_DEBUG;
	kvm_run->debug.arch.pc = svm->vmcb->save.cs.base + svm->vmcb->save.rip;
	kvm_run->debug.arch.exception = BP_VECTOR;
	return 0;
}

static int ud_interception(struct vcpu_svm *svm)
{
	int er;

	er = emulate_instruction(&svm->vcpu, EMULTYPE_TRAP_UD);
	if (er != EMULATE_DONE)
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int ac_interception(struct vcpu_svm *svm)
{
	kvm_queue_exception_e(&svm->vcpu, AC_VECTOR, 0);
	return 1;
}

static void svm_fpu_activate(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	clr_exception_intercept(svm, NM_VECTOR);

	svm->vcpu.fpu_active = 1;
	update_cr0_intercept(svm);
}

static int nm_interception(struct vcpu_svm *svm)
{
	svm_fpu_activate(&svm->vcpu);
	return 1;
}

static bool is_erratum_383(void)
{
	int err, i;
	u64 value;

	if (!erratum_383_found)
		return false;

	value = native_read_msr_safe(MSR_IA32_MC0_STATUS, &err);
	if (err)
		return false;

	/* Bit 62 may or may not be set for this mce */
	value &= ~(1ULL << 62);

	if (value != 0xb600000000010015ULL)
		return false;

	/* Clear MCi_STATUS registers */
	for (i = 0; i < 6; ++i)
		native_write_msr_safe(MSR_IA32_MCx_STATUS(i), 0, 0);

	value = native_read_msr_safe(MSR_IA32_MCG_STATUS, &err);
	if (!err) {
		u32 low, high;

		value &= ~(1ULL << 2);
		low    = lower_32_bits(value);
		high   = upper_32_bits(value);

		native_write_msr_safe(MSR_IA32_MCG_STATUS, low, high);
	}

	/* Flush tlb to evict multi-match entries */
	__flush_tlb_all();

	return true;
}

static void svm_handle_mce(struct vcpu_svm *svm)
{
	if (is_erratum_383()) {
		/*
		 * Erratum 383 triggered. Guest state is corrupt so kill the
		 * guest.
		 */
		pr_err("KVM: Guest triggered AMD Erratum 383\n");

		kvm_make_request(KVM_REQ_TRIPLE_FAULT, &svm->vcpu);

		return;
	}

	/*
	 * On an #MC intercept the MCE handler is not called automatically in
	 * the host. So do it by hand here.
	 */
	asm volatile (
		"int $0x12\n");
	/* not sure if we ever come back to this point */

	return;
}

static int mc_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int shutdown_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;

	/*
	 * VMCB is undefined after a SHUTDOWN intercept
	 * so reinitialize it.
	 */
	clear_page(svm->vmcb);
	init_vmcb(svm);

	kvm_run->exit_reason = KVM_EXIT_SHUTDOWN;
	return 0;
}

static int io_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;
	u32 io_info = svm->vmcb->control.exit_info_1; /* address size bug? */
	int size, in, string;
	unsigned port;

	++svm->vcpu.stat.io_exits;
	string = (io_info & SVM_IOIO_STR_MASK) != 0;
	in = (io_info & SVM_IOIO_TYPE_MASK) != 0;
	if (string || in)
		return emulate_instruction(vcpu, 0) == EMULATE_DONE;

	port = io_info >> 16;
	size = (io_info & SVM_IOIO_SIZE_MASK) >> SVM_IOIO_SIZE_SHIFT;
	svm->next_rip = svm->vmcb->control.exit_info_2;
	skip_emulated_instruction(&svm->vcpu);

	return kvm_fast_pio_out(vcpu, size, port);
}

static int nmi_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int intr_interception(struct vcpu_svm *svm)
{
	++svm->vcpu.stat.irq_exits;
	return 1;
}

static int nop_on_interception(struct vcpu_svm *svm)
{
	return 1;
}

static int halt_interception(struct vcpu_svm *svm)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 1;
	return kvm_emulate_halt(&svm->vcpu);
}

static int vmmcall_interception(struct vcpu_svm *svm)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	return kvm_emulate_hypercall(&svm->vcpu);
}

static unsigned long nested_svm_get_tdp_cr3(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return svm->nested.nested_cr3;
}

static u64 nested_svm_get_tdp_pdptr(struct kvm_vcpu *vcpu, int index)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr3 = svm->nested.nested_cr3;
	u64 pdpte;
	int ret;

	ret = kvm_vcpu_read_guest_page(vcpu, gpa_to_gfn(cr3), &pdpte,
				       offset_in_page(cr3) + index * 8, 8);
	if (ret)
		return 0;
	return pdpte;
}

static void nested_svm_set_tdp_cr3(struct kvm_vcpu *vcpu,
				   unsigned long root)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->control.nested_cr3 = root;
	mark_dirty(svm->vmcb, VMCB_NPT);
	svm_flush_tlb(vcpu);
}

static void nested_svm_inject_npf_exit(struct kvm_vcpu *vcpu,
				       struct x86_exception *fault)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm->vmcb->control.exit_code != SVM_EXIT_NPF) {
		/*
		 * TODO: track the cause of the nested page fault, and
		 * correctly fill in the high bits of exit_info_1.
		 */
		svm->vmcb->control.exit_code = SVM_EXIT_NPF;
		svm->vmcb->control.exit_code_hi = 0;
		svm->vmcb->control.exit_info_1 = (1ULL << 32);
		svm->vmcb->control.exit_info_2 = fault->address;
	}

	svm->vmcb->control.exit_info_1 &= ~0xffffffffULL;
	svm->vmcb->control.exit_info_1 |= fault->error_code;

	/*
	 * The present bit is always zero for page structure faults on real
	 * hardware.
	 */
	if (svm->vmcb->control.exit_info_1 & (2ULL << 32))
		svm->vmcb->control.exit_info_1 &= ~1;

	nested_svm_vmexit(svm);
}

static void nested_svm_init_mmu_context(struct kvm_vcpu *vcpu)
{
	WARN_ON(mmu_is_nested(vcpu));
	kvm_init_shadow_mmu(vcpu);
	vcpu->arch.mmu.set_cr3           = nested_svm_set_tdp_cr3;
	vcpu->arch.mmu.get_cr3           = nested_svm_get_tdp_cr3;
	vcpu->arch.mmu.get_pdptr         = nested_svm_get_tdp_pdptr;
	vcpu->arch.mmu.inject_page_fault = nested_svm_inject_npf_exit;
	vcpu->arch.mmu.shadow_root_level = get_npt_level();
	reset_shadow_zero_bits_mask(vcpu, &vcpu->arch.mmu);
	vcpu->arch.walk_mmu              = &vcpu->arch.nested_mmu;
}

static void nested_svm_uninit_mmu_context(struct kvm_vcpu *vcpu)
{
	vcpu->arch.walk_mmu = &vcpu->arch.mmu;
}

static int nested_svm_check_permissions(struct vcpu_svm *svm)
{
	if (!(svm->vcpu.arch.efer & EFER_SVME)
	    || !is_paging(&svm->vcpu)) {
		kvm_queue_exception(&svm->vcpu, UD_VECTOR);
		return 1;
	}

	if (svm->vmcb->save.cpl) {
		kvm_inject_gp(&svm->vcpu, 0);
		return 1;
	}

       return 0;
}

static int nested_svm_check_exception(struct vcpu_svm *svm, unsigned nr,
				      bool has_error_code, u32 error_code)
{
	int vmexit;

	if (!is_guest_mode(&svm->vcpu))
		return 0;

	svm->vmcb->control.exit_code = SVM_EXIT_EXCP_BASE + nr;
	svm->vmcb->control.exit_code_hi = 0;
	svm->vmcb->control.exit_info_1 = error_code;
	svm->vmcb->control.exit_info_2 = svm->vcpu.arch.cr2;

	vmexit = nested_svm_intercept(svm);
	if (vmexit == NESTED_EXIT_DONE)
		svm->nested.exit_required = true;

	return vmexit;
}

/* This function returns true if it is save to enable the irq window */
static inline bool nested_svm_intr(struct vcpu_svm *svm)
{
	if (!is_guest_mode(&svm->vcpu))
		return true;

	if (!(svm->vcpu.arch.hflags & HF_VINTR_MASK))
		return true;

	if (!(svm->vcpu.arch.hflags & HF_HIF_MASK))
		return false;

	/*
	 * if vmexit was already requested (by intercepted exception
	 * for instance) do not overwrite it with "external interrupt"
	 * vmexit.
	 */
	if (svm->nested.exit_required)
		return false;

	svm->vmcb->control.exit_code   = SVM_EXIT_INTR;
	svm->vmcb->control.exit_info_1 = 0;
	svm->vmcb->control.exit_info_2 = 0;

	if (svm->nested.intercept & 1ULL) {
		/*
		 * The #vmexit can't be emulated here directly because this
		 * code path runs with irqs and preemption disabled. A
		 * #vmexit emulation might sleep. Only signal request for
		 * the #vmexit here.
		 */
		svm->nested.exit_required = true;
		trace_kvm_nested_intr_vmexit(svm->vmcb->save.rip);
		return false;
	}

	return true;
}

/* This function returns true if it is save to enable the nmi window */
static inline bool nested_svm_nmi(struct vcpu_svm *svm)
{
	if (!is_guest_mode(&svm->vcpu))
		return true;

	if (!(svm->nested.intercept & (1ULL << INTERCEPT_NMI)))
		return true;

	svm->vmcb->control.exit_code = SVM_EXIT_NMI;
	svm->nested.exit_required = true;

	return false;
}

static void *nested_svm_map(struct vcpu_svm *svm, u64 gpa, struct page **_page)
{
	struct page *page;

	might_sleep();

	page = kvm_vcpu_gfn_to_page(&svm->vcpu, gpa >> PAGE_SHIFT);
	if (is_error_page(page))
		goto error;

	*_page = page;

	return kmap(page);

error:
	kvm_inject_gp(&svm->vcpu, 0);

	return NULL;
}

static void nested_svm_unmap(struct page *page)
{
	kunmap(page);
	kvm_release_page_dirty(page);
}

static int nested_svm_intercept_ioio(struct vcpu_svm *svm)
{
	unsigned port, size, iopm_len;
	u16 val, mask;
	u8 start_bit;
	u64 gpa;

	if (!(svm->nested.intercept & (1ULL << INTERCEPT_IOIO_PROT)))
		return NESTED_EXIT_HOST;

	port = svm->vmcb->control.exit_info_1 >> 16;
	size = (svm->vmcb->control.exit_info_1 & SVM_IOIO_SIZE_MASK) >>
		SVM_IOIO_SIZE_SHIFT;
	gpa  = svm->nested.vmcb_iopm + (port / 8);
	start_bit = port % 8;
	iopm_len = (start_bit + size > 8) ? 2 : 1;
	mask = (0xf >> (4 - size)) << start_bit;
	val = 0;

	if (kvm_vcpu_read_guest(&svm->vcpu, gpa, &val, iopm_len))
		return NESTED_EXIT_DONE;

	return (val & mask) ? NESTED_EXIT_DONE : NESTED_EXIT_HOST;
}

static int nested_svm_exit_handled_msr(struct vcpu_svm *svm)
{
	u32 offset, msr, value;
	int write, mask;

	if (!(svm->nested.intercept & (1ULL << INTERCEPT_MSR_PROT)))
		return NESTED_EXIT_HOST;

	msr    = svm->vcpu.arch.regs[VCPU_REGS_RCX];
	offset = svm_msrpm_offset(msr);
	write  = svm->vmcb->control.exit_info_1 & 1;
	mask   = 1 << ((2 * (msr & 0xf)) + write);

	if (offset == MSR_INVALID)
		return NESTED_EXIT_DONE;

	/* Offset is in 32 bit units but need in 8 bit units */
	offset *= 4;

	if (kvm_vcpu_read_guest(&svm->vcpu, svm->nested.vmcb_msrpm + offset, &value, 4))
		return NESTED_EXIT_DONE;

	return (value & mask) ? NESTED_EXIT_DONE : NESTED_EXIT_HOST;
}

static int nested_svm_exit_special(struct vcpu_svm *svm)
{
	u32 exit_code = svm->vmcb->control.exit_code;

	switch (exit_code) {
	case SVM_EXIT_INTR:
	case SVM_EXIT_NMI:
	case SVM_EXIT_EXCP_BASE + MC_VECTOR:
		return NESTED_EXIT_HOST;
	case SVM_EXIT_NPF:
		/* For now we are always handling NPFs when using them */
		if (npt_enabled)
			return NESTED_EXIT_HOST;
		break;
	case SVM_EXIT_EXCP_BASE + PF_VECTOR:
		/* When we're shadowing, trap PFs, but not async PF */
		if (!npt_enabled && svm->apf_reason == 0)
			return NESTED_EXIT_HOST;
		break;
	case SVM_EXIT_EXCP_BASE + NM_VECTOR:
		nm_interception(svm);
		break;
	default:
		break;
	}

	return NESTED_EXIT_CONTINUE;
}

/*
 * If this function returns true, this #vmexit was already handled
 */
static int nested_svm_intercept(struct vcpu_svm *svm)
{
	u32 exit_code = svm->vmcb->control.exit_code;
	int vmexit = NESTED_EXIT_HOST;

	switch (exit_code) {
	case SVM_EXIT_MSR:
		vmexit = nested_svm_exit_handled_msr(svm);
		break;
	case SVM_EXIT_IOIO:
		vmexit = nested_svm_intercept_ioio(svm);
		break;
	case SVM_EXIT_READ_CR0 ... SVM_EXIT_WRITE_CR8: {
		u32 bit = 1U << (exit_code - SVM_EXIT_READ_CR0);
		if (svm->nested.intercept_cr & bit)
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_READ_DR0 ... SVM_EXIT_WRITE_DR7: {
		u32 bit = 1U << (exit_code - SVM_EXIT_READ_DR0);
		if (svm->nested.intercept_dr & bit)
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_EXCP_BASE ... SVM_EXIT_EXCP_BASE + 0x1f: {
		u32 excp_bits = 1 << (exit_code - SVM_EXIT_EXCP_BASE);
		if (svm->nested.intercept_exceptions & excp_bits)
			vmexit = NESTED_EXIT_DONE;
		/* async page fault always cause vmexit */
		else if ((exit_code == SVM_EXIT_EXCP_BASE + PF_VECTOR) &&
			 svm->apf_reason != 0)
			vmexit = NESTED_EXIT_DONE;
		break;
	}
	case SVM_EXIT_ERR: {
		vmexit = NESTED_EXIT_DONE;
		break;
	}
	default: {
		u64 exit_bits = 1ULL << (exit_code - SVM_EXIT_INTR);
		if (svm->nested.intercept & exit_bits)
			vmexit = NESTED_EXIT_DONE;
	}
	}

	return vmexit;
}

static int nested_svm_exit_handled(struct vcpu_svm *svm)
{
	int vmexit;

	vmexit = nested_svm_intercept(svm);

	if (vmexit == NESTED_EXIT_DONE)
		nested_svm_vmexit(svm);

	return vmexit;
}

static inline void copy_vmcb_control_area(struct vmcb *dst_vmcb, struct vmcb *from_vmcb)
{
	struct vmcb_control_area *dst  = &dst_vmcb->control;
	struct vmcb_control_area *from = &from_vmcb->control;

	dst->intercept_cr         = from->intercept_cr;
	dst->intercept_dr         = from->intercept_dr;
	dst->intercept_exceptions = from->intercept_exceptions;
	dst->intercept            = from->intercept;
	dst->iopm_base_pa         = from->iopm_base_pa;
	dst->msrpm_base_pa        = from->msrpm_base_pa;
	dst->tsc_offset           = from->tsc_offset;
	dst->asid                 = from->asid;
	dst->tlb_ctl              = from->tlb_ctl;
	dst->int_ctl              = from->int_ctl;
	dst->int_vector           = from->int_vector;
	dst->int_state            = from->int_state;
	dst->exit_code            = from->exit_code;
	dst->exit_code_hi         = from->exit_code_hi;
	dst->exit_info_1          = from->exit_info_1;
	dst->exit_info_2          = from->exit_info_2;
	dst->exit_int_info        = from->exit_int_info;
	dst->exit_int_info_err    = from->exit_int_info_err;
	dst->nested_ctl           = from->nested_ctl;
	dst->event_inj            = from->event_inj;
	dst->event_inj_err        = from->event_inj_err;
	dst->nested_cr3           = from->nested_cr3;
	dst->lbr_ctl              = from->lbr_ctl;
}

static int nested_svm_vmexit(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct vmcb *hsave = svm->nested.hsave;
	struct vmcb *vmcb = svm->vmcb;
	struct page *page;

	trace_kvm_nested_vmexit_inject(vmcb->control.exit_code,
				       vmcb->control.exit_info_1,
				       vmcb->control.exit_info_2,
				       vmcb->control.exit_int_info,
				       vmcb->control.exit_int_info_err,
				       KVM_ISA_SVM);

	nested_vmcb = nested_svm_map(svm, svm->nested.vmcb, &page);
	if (!nested_vmcb)
		return 1;

	/* Exit Guest-Mode */
	leave_guest_mode(&svm->vcpu);
	svm->nested.vmcb = 0;

	/* Give the current vmcb to the guest */
	disable_gif(svm);

	nested_vmcb->save.es     = vmcb->save.es;
	nested_vmcb->save.cs     = vmcb->save.cs;
	nested_vmcb->save.ss     = vmcb->save.ss;
	nested_vmcb->save.ds     = vmcb->save.ds;
	nested_vmcb->save.gdtr   = vmcb->save.gdtr;
	nested_vmcb->save.idtr   = vmcb->save.idtr;
	nested_vmcb->save.efer   = svm->vcpu.arch.efer;
	nested_vmcb->save.cr0    = kvm_read_cr0(&svm->vcpu);
	nested_vmcb->save.cr3    = kvm_read_cr3(&svm->vcpu);
	nested_vmcb->save.cr2    = vmcb->save.cr2;
	nested_vmcb->save.cr4    = svm->vcpu.arch.cr4;
	nested_vmcb->save.rflags = kvm_get_rflags(&svm->vcpu);
	nested_vmcb->save.rip    = vmcb->save.rip;
	nested_vmcb->save.rsp    = vmcb->save.rsp;
	nested_vmcb->save.rax    = vmcb->save.rax;
	nested_vmcb->save.dr7    = vmcb->save.dr7;
	nested_vmcb->save.dr6    = vmcb->save.dr6;
	nested_vmcb->save.cpl    = vmcb->save.cpl;

	nested_vmcb->control.int_ctl           = vmcb->control.int_ctl;
	nested_vmcb->control.int_vector        = vmcb->control.int_vector;
	nested_vmcb->control.int_state         = vmcb->control.int_state;
	nested_vmcb->control.exit_code         = vmcb->control.exit_code;
	nested_vmcb->control.exit_code_hi      = vmcb->control.exit_code_hi;
	nested_vmcb->control.exit_info_1       = vmcb->control.exit_info_1;
	nested_vmcb->control.exit_info_2       = vmcb->control.exit_info_2;
	nested_vmcb->control.exit_int_info     = vmcb->control.exit_int_info;
	nested_vmcb->control.exit_int_info_err = vmcb->control.exit_int_info_err;

	if (svm->nrips_enabled)
		nested_vmcb->control.next_rip  = vmcb->control.next_rip;

	/*
	 * If we emulate a VMRUN/#VMEXIT in the same host #vmexit cycle we have
	 * to make sure that we do not lose injected events. So check event_inj
	 * here and copy it to exit_int_info if it is valid.
	 * Exit_int_info and event_inj can't be both valid because the case
	 * below only happens on a VMRUN instruction intercept which has
	 * no valid exit_int_info set.
	 */
	if (vmcb->control.event_inj & SVM_EVTINJ_VALID) {
		struct vmcb_control_area *nc = &nested_vmcb->control;

		nc->exit_int_info     = vmcb->control.event_inj;
		nc->exit_int_info_err = vmcb->control.event_inj_err;
	}

	nested_vmcb->control.tlb_ctl           = 0;
	nested_vmcb->control.event_inj         = 0;
	nested_vmcb->control.event_inj_err     = 0;

	/* We always set V_INTR_MASKING and remember the old value in hflags */
	if (!(svm->vcpu.arch.hflags & HF_VINTR_MASK))
		nested_vmcb->control.int_ctl &= ~V_INTR_MASKING_MASK;

	/* Restore the original control entries */
	copy_vmcb_control_area(vmcb, hsave);

	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	svm->nested.nested_cr3 = 0;

	/* Restore selected save entries */
	svm->vmcb->save.es = hsave->save.es;
	svm->vmcb->save.cs = hsave->save.cs;
	svm->vmcb->save.ss = hsave->save.ss;
	svm->vmcb->save.ds = hsave->save.ds;
	svm->vmcb->save.gdtr = hsave->save.gdtr;
	svm->vmcb->save.idtr = hsave->save.idtr;
	kvm_set_rflags(&svm->vcpu, hsave->save.rflags);
	svm_set_efer(&svm->vcpu, hsave->save.efer);
	svm_set_cr0(&svm->vcpu, hsave->save.cr0 | X86_CR0_PE);
	svm_set_cr4(&svm->vcpu, hsave->save.cr4);
	if (npt_enabled) {
		svm->vmcb->save.cr3 = hsave->save.cr3;
		svm->vcpu.arch.cr3 = hsave->save.cr3;
	} else {
		(void)kvm_set_cr3(&svm->vcpu, hsave->save.cr3);
	}
	kvm_register_write(&svm->vcpu, VCPU_REGS_RAX, hsave->save.rax);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RSP, hsave->save.rsp);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RIP, hsave->save.rip);
	svm->vmcb->save.dr7 = 0;
	svm->vmcb->save.cpl = 0;
	svm->vmcb->control.exit_int_info = 0;

	mark_all_dirty(svm->vmcb);

	nested_svm_unmap(page);

	nested_svm_uninit_mmu_context(&svm->vcpu);
	kvm_mmu_reset_context(&svm->vcpu);
	kvm_mmu_load(&svm->vcpu);

	return 0;
}

static bool nested_svm_vmrun_msrpm(struct vcpu_svm *svm)
{
	/*
	 * This function merges the msr permission bitmaps of kvm and the
	 * nested vmcb. It is optimized in that it only merges the parts where
	 * the kvm msr permission bitmap may contain zero bits
	 */
	int i;

	if (!(svm->nested.intercept & (1ULL << INTERCEPT_MSR_PROT)))
		return true;

	for (i = 0; i < MSRPM_OFFSETS; i++) {
		u32 value, p;
		u64 offset;

		if (msrpm_offsets[i] == 0xffffffff)
			break;

		p      = msrpm_offsets[i];
		offset = svm->nested.vmcb_msrpm + (p * 4);

		if (kvm_vcpu_read_guest(&svm->vcpu, offset, &value, 4))
			return false;

		svm->nested.msrpm[p] = svm->msrpm[p] | value;
	}

	svm->vmcb->control.msrpm_base_pa = __pa(svm->nested.msrpm);

	return true;
}

static bool nested_vmcb_checks(struct vmcb *vmcb)
{
	if ((vmcb->control.intercept & (1ULL << INTERCEPT_VMRUN)) == 0)
		return false;

	if (vmcb->control.asid == 0)
		return false;

	if (vmcb->control.nested_ctl && !npt_enabled)
		return false;

	return true;
}

static bool nested_svm_vmrun(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct vmcb *hsave = svm->nested.hsave;
	struct vmcb *vmcb = svm->vmcb;
	struct page *page;
	u64 vmcb_gpa;

	vmcb_gpa = svm->vmcb->save.rax;

	nested_vmcb = nested_svm_map(svm, svm->vmcb->save.rax, &page);
	if (!nested_vmcb)
		return false;

	if (!nested_vmcb_checks(nested_vmcb)) {
		nested_vmcb->control.exit_code    = SVM_EXIT_ERR;
		nested_vmcb->control.exit_code_hi = 0;
		nested_vmcb->control.exit_info_1  = 0;
		nested_vmcb->control.exit_info_2  = 0;

		nested_svm_unmap(page);

		return false;
	}

	trace_kvm_nested_vmrun(svm->vmcb->save.rip, vmcb_gpa,
			       nested_vmcb->save.rip,
			       nested_vmcb->control.int_ctl,
			       nested_vmcb->control.event_inj,
			       nested_vmcb->control.nested_ctl);

	trace_kvm_nested_intercepts(nested_vmcb->control.intercept_cr & 0xffff,
				    nested_vmcb->control.intercept_cr >> 16,
				    nested_vmcb->control.intercept_exceptions,
				    nested_vmcb->control.intercept);

	/* Clear internal status */
	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	/*
	 * Save the old vmcb, so we don't need to pick what we save, but can
	 * restore everything when a VMEXIT occurs
	 */
	hsave->save.es     = vmcb->save.es;
	hsave->save.cs     = vmcb->save.cs;
	hsave->save.ss     = vmcb->save.ss;
	hsave->save.ds     = vmcb->save.ds;
	hsave->save.gdtr   = vmcb->save.gdtr;
	hsave->save.idtr   = vmcb->save.idtr;
	hsave->save.efer   = svm->vcpu.arch.efer;
	hsave->save.cr0    = kvm_read_cr0(&svm->vcpu);
	hsave->save.cr4    = svm->vcpu.arch.cr4;
	hsave->save.rflags = kvm_get_rflags(&svm->vcpu);
	hsave->save.rip    = kvm_rip_read(&svm->vcpu);
	hsave->save.rsp    = vmcb->save.rsp;
	hsave->save.rax    = vmcb->save.rax;
	if (npt_enabled)
		hsave->save.cr3    = vmcb->save.cr3;
	else
		hsave->save.cr3    = kvm_read_cr3(&svm->vcpu);

	copy_vmcb_control_area(hsave, vmcb);

	if (kvm_get_rflags(&svm->vcpu) & X86_EFLAGS_IF)
		svm->vcpu.arch.hflags |= HF_HIF_MASK;
	else
		svm->vcpu.arch.hflags &= ~HF_HIF_MASK;

	if (nested_vmcb->control.nested_ctl) {
		kvm_mmu_unload(&svm->vcpu);
		svm->nested.nested_cr3 = nested_vmcb->control.nested_cr3;
		nested_svm_init_mmu_context(&svm->vcpu);
	}

	/* Load the nested guest state */
	svm->vmcb->save.es = nested_vmcb->save.es;
	svm->vmcb->save.cs = nested_vmcb->save.cs;
	svm->vmcb->save.ss = nested_vmcb->save.ss;
	svm->vmcb->save.ds = nested_vmcb->save.ds;
	svm->vmcb->save.gdtr = nested_vmcb->save.gdtr;
	svm->vmcb->save.idtr = nested_vmcb->save.idtr;
	kvm_set_rflags(&svm->vcpu, nested_vmcb->save.rflags);
	svm_set_efer(&svm->vcpu, nested_vmcb->save.efer);
	svm_set_cr0(&svm->vcpu, nested_vmcb->save.cr0);
	svm_set_cr4(&svm->vcpu, nested_vmcb->save.cr4);
	if (npt_enabled) {
		svm->vmcb->save.cr3 = nested_vmcb->save.cr3;
		svm->vcpu.arch.cr3 = nested_vmcb->save.cr3;
	} else
		(void)kvm_set_cr3(&svm->vcpu, nested_vmcb->save.cr3);

	/* Guest paging mode is active - reset mmu */
	kvm_mmu_reset_context(&svm->vcpu);

	svm->vmcb->save.cr2 = svm->vcpu.arch.cr2 = nested_vmcb->save.cr2;
	kvm_register_write(&svm->vcpu, VCPU_REGS_RAX, nested_vmcb->save.rax);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RSP, nested_vmcb->save.rsp);
	kvm_register_write(&svm->vcpu, VCPU_REGS_RIP, nested_vmcb->save.rip);

	/* In case we don't even reach vcpu_run, the fields are not updated */
	svm->vmcb->save.rax = nested_vmcb->save.rax;
	svm->vmcb->save.rsp = nested_vmcb->save.rsp;
	svm->vmcb->save.rip = nested_vmcb->save.rip;
	svm->vmcb->save.dr7 = nested_vmcb->save.dr7;
	svm->vmcb->save.dr6 = nested_vmcb->save.dr6;
	svm->vmcb->save.cpl = nested_vmcb->save.cpl;

	svm->nested.vmcb_msrpm = nested_vmcb->control.msrpm_base_pa & ~0x0fffULL;
	svm->nested.vmcb_iopm  = nested_vmcb->control.iopm_base_pa  & ~0x0fffULL;

	/* cache intercepts */
	svm->nested.intercept_cr         = nested_vmcb->control.intercept_cr;
	svm->nested.intercept_dr         = nested_vmcb->control.intercept_dr;
	svm->nested.intercept_exceptions = nested_vmcb->control.intercept_exceptions;
	svm->nested.intercept            = nested_vmcb->control.intercept;

	svm_flush_tlb(&svm->vcpu);
	svm->vmcb->control.int_ctl = nested_vmcb->control.int_ctl | V_INTR_MASKING_MASK;
	if (nested_vmcb->control.int_ctl & V_INTR_MASKING_MASK)
		svm->vcpu.arch.hflags |= HF_VINTR_MASK;
	else
		svm->vcpu.arch.hflags &= ~HF_VINTR_MASK;

	if (svm->vcpu.arch.hflags & HF_VINTR_MASK) {
		/* We only want the cr8 intercept bits of the guest */
		clr_cr_intercept(svm, INTERCEPT_CR8_READ);
		clr_cr_intercept(svm, INTERCEPT_CR8_WRITE);
	}

	/* We don't want to see VMMCALLs from a nested guest */
	clr_intercept(svm, INTERCEPT_VMMCALL);

	svm->vmcb->control.lbr_ctl = nested_vmcb->control.lbr_ctl;
	svm->vmcb->control.int_vector = nested_vmcb->control.int_vector;
	svm->vmcb->control.int_state = nested_vmcb->control.int_state;
	svm->vmcb->control.tsc_offset += nested_vmcb->control.tsc_offset;
	svm->vmcb->control.event_inj = nested_vmcb->control.event_inj;
	svm->vmcb->control.event_inj_err = nested_vmcb->control.event_inj_err;

	nested_svm_unmap(page);

	/* Enter Guest-Mode */
	enter_guest_mode(&svm->vcpu);

	/*
	 * Merge guest and host intercepts - must be called  with vcpu in
	 * guest-mode to take affect here
	 */
	recalc_intercepts(svm);

	svm->nested.vmcb = vmcb_gpa;

	enable_gif(svm);

	mark_all_dirty(svm->vmcb);

	return true;
}

static void nested_svm_vmloadsave(struct vmcb *from_vmcb, struct vmcb *to_vmcb)
{
	to_vmcb->save.fs = from_vmcb->save.fs;
	to_vmcb->save.gs = from_vmcb->save.gs;
	to_vmcb->save.tr = from_vmcb->save.tr;
	to_vmcb->save.ldtr = from_vmcb->save.ldtr;
	to_vmcb->save.kernel_gs_base = from_vmcb->save.kernel_gs_base;
	to_vmcb->save.star = from_vmcb->save.star;
	to_vmcb->save.lstar = from_vmcb->save.lstar;
	to_vmcb->save.cstar = from_vmcb->save.cstar;
	to_vmcb->save.sfmask = from_vmcb->save.sfmask;
	to_vmcb->save.sysenter_cs = from_vmcb->save.sysenter_cs;
	to_vmcb->save.sysenter_esp = from_vmcb->save.sysenter_esp;
	to_vmcb->save.sysenter_eip = from_vmcb->save.sysenter_eip;
}

static int vmload_interception(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct page *page;

	if (nested_svm_check_permissions(svm))
		return 1;

	nested_vmcb = nested_svm_map(svm, svm->vmcb->save.rax, &page);
	if (!nested_vmcb)
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	nested_svm_vmloadsave(nested_vmcb, svm->vmcb);
	nested_svm_unmap(page);

	return 1;
}

static int vmsave_interception(struct vcpu_svm *svm)
{
	struct vmcb *nested_vmcb;
	struct page *page;

	if (nested_svm_check_permissions(svm))
		return 1;

	nested_vmcb = nested_svm_map(svm, svm->vmcb->save.rax, &page);
	if (!nested_vmcb)
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	nested_svm_vmloadsave(svm->vmcb, nested_vmcb);
	nested_svm_unmap(page);

	return 1;
}

static int vmrun_interception(struct vcpu_svm *svm)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	/* Save rip after vmrun instruction */
	kvm_rip_write(&svm->vcpu, kvm_rip_read(&svm->vcpu) + 3);

	if (!nested_svm_vmrun(svm))
		return 1;

	if (!nested_svm_vmrun_msrpm(svm))
		goto failed;

	return 1;

failed:

	svm->vmcb->control.exit_code    = SVM_EXIT_ERR;
	svm->vmcb->control.exit_code_hi = 0;
	svm->vmcb->control.exit_info_1  = 0;
	svm->vmcb->control.exit_info_2  = 0;

	nested_svm_vmexit(svm);

	return 1;
}

static int stgi_interception(struct vcpu_svm *svm)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);

	enable_gif(svm);

	return 1;
}

static int clgi_interception(struct vcpu_svm *svm)
{
	if (nested_svm_check_permissions(svm))
		return 1;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);

	disable_gif(svm);

	/* After a CLGI no interrupts should come */
	if (!kvm_vcpu_apicv_active(&svm->vcpu)) {
		svm_clear_vintr(svm);
		svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;
		mark_dirty(svm->vmcb, VMCB_INTR);
	}

	return 1;
}

static int invlpga_interception(struct vcpu_svm *svm)
{
	struct kvm_vcpu *vcpu = &svm->vcpu;

	trace_kvm_invlpga(svm->vmcb->save.rip, kvm_register_read(&svm->vcpu, VCPU_REGS_RCX),
			  kvm_register_read(&svm->vcpu, VCPU_REGS_RAX));

	/* Let's treat INVLPGA the same as INVLPG (can be optimized!) */
	kvm_mmu_invlpg(vcpu, kvm_register_read(&svm->vcpu, VCPU_REGS_RAX));

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
	skip_emulated_instruction(&svm->vcpu);
	return 1;
}

static int skinit_interception(struct vcpu_svm *svm)
{
	trace_kvm_skinit(svm->vmcb->save.rip, kvm_register_read(&svm->vcpu, VCPU_REGS_RAX));

	kvm_queue_exception(&svm->vcpu, UD_VECTOR);
	return 1;
}

static int wbinvd_interception(struct vcpu_svm *svm)
{
	kvm_emulate_wbinvd(&svm->vcpu);
	return 1;
}

static int xsetbv_interception(struct vcpu_svm *svm)
{
	u64 new_bv = kvm_read_edx_eax(&svm->vcpu);
	u32 index = kvm_register_read(&svm->vcpu, VCPU_REGS_RCX);

	if (kvm_set_xcr(&svm->vcpu, index, new_bv) == 0) {
		svm->next_rip = kvm_rip_read(&svm->vcpu) + 3;
		skip_emulated_instruction(&svm->vcpu);
	}

	return 1;
}

static int task_switch_interception(struct vcpu_svm *svm)
{
	u16 tss_selector;
	int reason;
	int int_type = svm->vmcb->control.exit_int_info &
		SVM_EXITINTINFO_TYPE_MASK;
	int int_vec = svm->vmcb->control.exit_int_info & SVM_EVTINJ_VEC_MASK;
	uint32_t type =
		svm->vmcb->control.exit_int_info & SVM_EXITINTINFO_TYPE_MASK;
	uint32_t idt_v =
		svm->vmcb->control.exit_int_info & SVM_EXITINTINFO_VALID;
	bool has_error_code = false;
	u32 error_code = 0;

	tss_selector = (u16)svm->vmcb->control.exit_info_1;

	if (svm->vmcb->control.exit_info_2 &
	    (1ULL << SVM_EXITINFOSHIFT_TS_REASON_IRET))
		reason = TASK_SWITCH_IRET;
	else if (svm->vmcb->control.exit_info_2 &
		 (1ULL << SVM_EXITINFOSHIFT_TS_REASON_JMP))
		reason = TASK_SWITCH_JMP;
	else if (idt_v)
		reason = TASK_SWITCH_GATE;
	else
		reason = TASK_SWITCH_CALL;

	if (reason == TASK_SWITCH_GATE) {
		switch (type) {
		case SVM_EXITINTINFO_TYPE_NMI:
			svm->vcpu.arch.nmi_injected = false;
			break;
		case SVM_EXITINTINFO_TYPE_EXEPT:
			if (svm->vmcb->control.exit_info_2 &
			    (1ULL << SVM_EXITINFOSHIFT_TS_HAS_ERROR_CODE)) {
				has_error_code = true;
				error_code =
					(u32)svm->vmcb->control.exit_info_2;
			}
			kvm_clear_exception_queue(&svm->vcpu);
			break;
		case SVM_EXITINTINFO_TYPE_INTR:
			kvm_clear_interrupt_queue(&svm->vcpu);
			break;
		default:
			break;
		}
	}

	if (reason != TASK_SWITCH_GATE ||
	    int_type == SVM_EXITINTINFO_TYPE_SOFT ||
	    (int_type == SVM_EXITINTINFO_TYPE_EXEPT &&
	     (int_vec == OF_VECTOR || int_vec == BP_VECTOR)))
		skip_emulated_instruction(&svm->vcpu);

	if (int_type != SVM_EXITINTINFO_TYPE_SOFT)
		int_vec = -1;

	if (kvm_task_switch(&svm->vcpu, tss_selector, int_vec, reason,
				has_error_code, error_code) == EMULATE_FAIL) {
		svm->vcpu.run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		svm->vcpu.run->internal.suberror = KVM_INTERNAL_ERROR_EMULATION;
		svm->vcpu.run->internal.ndata = 0;
		return 0;
	}
	return 1;
}

static int cpuid_interception(struct vcpu_svm *svm)
{
	svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
	kvm_emulate_cpuid(&svm->vcpu);
	return 1;
}

static int iret_interception(struct vcpu_svm *svm)
{
	++svm->vcpu.stat.nmi_window_exits;
	clr_intercept(svm, INTERCEPT_IRET);
	svm->vcpu.arch.hflags |= HF_IRET_MASK;
	svm->nmi_iret_rip = kvm_rip_read(&svm->vcpu);
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	return 1;
}

static int invlpg_interception(struct vcpu_svm *svm)
{
	if (!static_cpu_has(X86_FEATURE_DECODEASSISTS))
		return emulate_instruction(&svm->vcpu, 0) == EMULATE_DONE;

	kvm_mmu_invlpg(&svm->vcpu, svm->vmcb->control.exit_info_1);
	skip_emulated_instruction(&svm->vcpu);
	return 1;
}

static int emulate_on_interception(struct vcpu_svm *svm)
{
	return emulate_instruction(&svm->vcpu, 0) == EMULATE_DONE;
}

static int rdpmc_interception(struct vcpu_svm *svm)
{
	int err;

	if (!static_cpu_has(X86_FEATURE_NRIPS))
		return emulate_on_interception(svm);

	err = kvm_rdpmc(&svm->vcpu);
	kvm_complete_insn_gp(&svm->vcpu, err);

	return 1;
}

static bool check_selective_cr0_intercepted(struct vcpu_svm *svm,
					    unsigned long val)
{
	unsigned long cr0 = svm->vcpu.arch.cr0;
	bool ret = false;
	u64 intercept;

	intercept = svm->nested.intercept;

	if (!is_guest_mode(&svm->vcpu) ||
	    (!(intercept & (1ULL << INTERCEPT_SELECTIVE_CR0))))
		return false;

	cr0 &= ~SVM_CR0_SELECTIVE_MASK;
	val &= ~SVM_CR0_SELECTIVE_MASK;

	if (cr0 ^ val) {
		svm->vmcb->control.exit_code = SVM_EXIT_CR0_SEL_WRITE;
		ret = (nested_svm_exit_handled(svm) == NESTED_EXIT_DONE);
	}

	return ret;
}

#define CR_VALID (1ULL << 63)

static int cr_interception(struct vcpu_svm *svm)
{
	int reg, cr;
	unsigned long val;
	int err;

	if (!static_cpu_has(X86_FEATURE_DECODEASSISTS))
		return emulate_on_interception(svm);

	if (unlikely((svm->vmcb->control.exit_info_1 & CR_VALID) == 0))
		return emulate_on_interception(svm);

	reg = svm->vmcb->control.exit_info_1 & SVM_EXITINFO_REG_MASK;
	if (svm->vmcb->control.exit_code == SVM_EXIT_CR0_SEL_WRITE)
		cr = SVM_EXIT_WRITE_CR0 - SVM_EXIT_READ_CR0;
	else
		cr = svm->vmcb->control.exit_code - SVM_EXIT_READ_CR0;

	err = 0;
	if (cr >= 16) { /* mov to cr */
		cr -= 16;
		val = kvm_register_read(&svm->vcpu, reg);
		switch (cr) {
		case 0:
			if (!check_selective_cr0_intercepted(svm, val))
				err = kvm_set_cr0(&svm->vcpu, val);
			else
				return 1;

			break;
		case 3:
			err = kvm_set_cr3(&svm->vcpu, val);
			break;
		case 4:
			err = kvm_set_cr4(&svm->vcpu, val);
			break;
		case 8:
			err = kvm_set_cr8(&svm->vcpu, val);
			break;
		default:
			WARN(1, "unhandled write to CR%d", cr);
			kvm_queue_exception(&svm->vcpu, UD_VECTOR);
			return 1;
		}
	} else { /* mov from cr */
		switch (cr) {
		case 0:
			val = kvm_read_cr0(&svm->vcpu);
			break;
		case 2:
			val = svm->vcpu.arch.cr2;
			break;
		case 3:
			val = kvm_read_cr3(&svm->vcpu);
			break;
		case 4:
			val = kvm_read_cr4(&svm->vcpu);
			break;
		case 8:
			val = kvm_get_cr8(&svm->vcpu);
			break;
		default:
			WARN(1, "unhandled read from CR%d", cr);
			kvm_queue_exception(&svm->vcpu, UD_VECTOR);
			return 1;
		}
		kvm_register_write(&svm->vcpu, reg, val);
	}
	kvm_complete_insn_gp(&svm->vcpu, err);

	return 1;
}

static int dr_interception(struct vcpu_svm *svm)
{
	int reg, dr;
	unsigned long val;

	if (svm->vcpu.guest_debug == 0) {
		/*
		 * No more DR vmexits; force a reload of the debug registers
		 * and reenter on this instruction.  The next vmexit will
		 * retrieve the full state of the debug registers.
		 */
		clr_dr_intercepts(svm);
		svm->vcpu.arch.switch_db_regs |= KVM_DEBUGREG_WONT_EXIT;
		return 1;
	}

	if (!boot_cpu_has(X86_FEATURE_DECODEASSISTS))
		return emulate_on_interception(svm);

	reg = svm->vmcb->control.exit_info_1 & SVM_EXITINFO_REG_MASK;
	dr = svm->vmcb->control.exit_code - SVM_EXIT_READ_DR0;

	if (dr >= 16) { /* mov to DRn */
		if (!kvm_require_dr(&svm->vcpu, dr - 16))
			return 1;
		val = kvm_register_read(&svm->vcpu, reg);
		kvm_set_dr(&svm->vcpu, dr - 16, val);
	} else {
		if (!kvm_require_dr(&svm->vcpu, dr))
			return 1;
		kvm_get_dr(&svm->vcpu, dr, &val);
		kvm_register_write(&svm->vcpu, reg, val);
	}

	skip_emulated_instruction(&svm->vcpu);

	return 1;
}

static int cr8_write_interception(struct vcpu_svm *svm)
{
	struct kvm_run *kvm_run = svm->vcpu.run;
	int r;

	u8 cr8_prev = kvm_get_cr8(&svm->vcpu);
	/* instruction emulation calls kvm_set_cr8() */
	r = cr_interception(svm);
	if (lapic_in_kernel(&svm->vcpu))
		return r;
	if (cr8_prev <= kvm_get_cr8(&svm->vcpu))
		return r;
	kvm_run->exit_reason = KVM_EXIT_SET_TPR;
	return 0;
}

static int svm_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	switch (msr_info->index) {
	case MSR_IA32_TSC: {
		msr_info->data = svm->vmcb->control.tsc_offset +
			kvm_scale_tsc(vcpu, rdtsc());

		break;
	}
	case MSR_STAR:
		msr_info->data = svm->vmcb->save.star;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		msr_info->data = svm->vmcb->save.lstar;
		break;
	case MSR_CSTAR:
		msr_info->data = svm->vmcb->save.cstar;
		break;
	case MSR_KERNEL_GS_BASE:
		msr_info->data = svm->vmcb->save.kernel_gs_base;
		break;
	case MSR_SYSCALL_MASK:
		msr_info->data = svm->vmcb->save.sfmask;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		msr_info->data = svm->vmcb->save.sysenter_cs;
		break;
	case MSR_IA32_SYSENTER_EIP:
		msr_info->data = svm->sysenter_eip;
		break;
	case MSR_IA32_SYSENTER_ESP:
		msr_info->data = svm->sysenter_esp;
		break;
	case MSR_TSC_AUX:
		if (!boot_cpu_has(X86_FEATURE_RDTSCP))
			return 1;
		msr_info->data = svm->tsc_aux;
		break;
	/*
	 * Nobody will change the following 5 values in the VMCB so we can
	 * safely return them on rdmsr. They will always be 0 until LBRV is
	 * implemented.
	 */
	case MSR_IA32_DEBUGCTLMSR:
		msr_info->data = svm->vmcb->save.dbgctl;
		break;
	case MSR_IA32_LASTBRANCHFROMIP:
		msr_info->data = svm->vmcb->save.br_from;
		break;
	case MSR_IA32_LASTBRANCHTOIP:
		msr_info->data = svm->vmcb->save.br_to;
		break;
	case MSR_IA32_LASTINTFROMIP:
		msr_info->data = svm->vmcb->save.last_excp_from;
		break;
	case MSR_IA32_LASTINTTOIP:
		msr_info->data = svm->vmcb->save.last_excp_to;
		break;
	case MSR_VM_HSAVE_PA:
		msr_info->data = svm->nested.hsave_msr;
		break;
	case MSR_VM_CR:
		msr_info->data = svm->nested.vm_cr_msr;
		break;
	case MSR_IA32_UCODE_REV:
		msr_info->data = 0x01000065;
		break;
	case MSR_F15H_IC_CFG: {

		int family, model;

		family = guest_cpuid_family(vcpu);
		model  = guest_cpuid_model(vcpu);

		if (family < 0 || model < 0)
			return kvm_get_msr_common(vcpu, msr_info);

		msr_info->data = 0;

		if (family == 0x15 &&
		    (model >= 0x2 && model < 0x20))
			msr_info->data = 0x1E;
		}
		break;
	default:
		return kvm_get_msr_common(vcpu, msr_info);
	}
	return 0;
}

static int rdmsr_interception(struct vcpu_svm *svm)
{
	u32 ecx = kvm_register_read(&svm->vcpu, VCPU_REGS_RCX);
	struct msr_data msr_info;

	msr_info.index = ecx;
	msr_info.host_initiated = false;
	if (svm_get_msr(&svm->vcpu, &msr_info)) {
		trace_kvm_msr_read_ex(ecx);
		kvm_inject_gp(&svm->vcpu, 0);
	} else {
		trace_kvm_msr_read(ecx, msr_info.data);

		kvm_register_write(&svm->vcpu, VCPU_REGS_RAX,
				   msr_info.data & 0xffffffff);
		kvm_register_write(&svm->vcpu, VCPU_REGS_RDX,
				   msr_info.data >> 32);
		svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
		skip_emulated_instruction(&svm->vcpu);
	}
	return 1;
}

static int svm_set_vm_cr(struct kvm_vcpu *vcpu, u64 data)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int svm_dis, chg_mask;

	if (data & ~SVM_VM_CR_VALID_MASK)
		return 1;

	chg_mask = SVM_VM_CR_VALID_MASK;

	if (svm->nested.vm_cr_msr & SVM_VM_CR_SVM_DIS_MASK)
		chg_mask &= ~(SVM_VM_CR_SVM_LOCK_MASK | SVM_VM_CR_SVM_DIS_MASK);

	svm->nested.vm_cr_msr &= ~chg_mask;
	svm->nested.vm_cr_msr |= (data & chg_mask);

	svm_dis = svm->nested.vm_cr_msr & SVM_VM_CR_SVM_DIS_MASK;

	/* check for svm_disable while efer.svme is set */
	if (svm_dis && (vcpu->arch.efer & EFER_SVME))
		return 1;

	return 0;
}

static int svm_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	u32 ecx = msr->index;
	u64 data = msr->data;
	switch (ecx) {
	case MSR_IA32_TSC:
		kvm_write_tsc(vcpu, msr);
		break;
	case MSR_STAR:
		svm->vmcb->save.star = data;
		break;
#ifdef CONFIG_X86_64
	case MSR_LSTAR:
		svm->vmcb->save.lstar = data;
		break;
	case MSR_CSTAR:
		svm->vmcb->save.cstar = data;
		break;
	case MSR_KERNEL_GS_BASE:
		svm->vmcb->save.kernel_gs_base = data;
		break;
	case MSR_SYSCALL_MASK:
		svm->vmcb->save.sfmask = data;
		break;
#endif
	case MSR_IA32_SYSENTER_CS:
		svm->vmcb->save.sysenter_cs = data;
		break;
	case MSR_IA32_SYSENTER_EIP:
		svm->sysenter_eip = data;
		svm->vmcb->save.sysenter_eip = data;
		break;
	case MSR_IA32_SYSENTER_ESP:
		svm->sysenter_esp = data;
		svm->vmcb->save.sysenter_esp = data;
		break;
	case MSR_TSC_AUX:
		if (!boot_cpu_has(X86_FEATURE_RDTSCP))
			return 1;

		/*
		 * This is rare, so we update the MSR here instead of using
		 * direct_access_msrs.  Doing that would require a rdmsr in
		 * svm_vcpu_put.
		 */
		svm->tsc_aux = data;
		wrmsrl(MSR_TSC_AUX, svm->tsc_aux);
		break;
	case MSR_IA32_DEBUGCTLMSR:
		if (!boot_cpu_has(X86_FEATURE_LBRV)) {
			vcpu_unimpl(vcpu, "%s: MSR_IA32_DEBUGCTL 0x%llx, nop\n",
				    __func__, data);
			break;
		}
		if (data & DEBUGCTL_RESERVED_BITS)
			return 1;

		svm->vmcb->save.dbgctl = data;
		mark_dirty(svm->vmcb, VMCB_LBR);
		if (data & (1ULL<<0))
			svm_enable_lbrv(svm);
		else
			svm_disable_lbrv(svm);
		break;
	case MSR_VM_HSAVE_PA:
		svm->nested.hsave_msr = data;
		break;
	case MSR_VM_CR:
		return svm_set_vm_cr(vcpu, data);
	case MSR_VM_IGNNE:
		vcpu_unimpl(vcpu, "unimplemented wrmsr: 0x%x data 0x%llx\n", ecx, data);
		break;
	case MSR_IA32_APICBASE:
		if (kvm_vcpu_apicv_active(vcpu))
			avic_update_vapic_bar(to_svm(vcpu), data);
		/* Follow through */
	default:
		return kvm_set_msr_common(vcpu, msr);
	}
	return 0;
}

static int wrmsr_interception(struct vcpu_svm *svm)
{
	struct msr_data msr;
	u32 ecx = kvm_register_read(&svm->vcpu, VCPU_REGS_RCX);
	u64 data = kvm_read_edx_eax(&svm->vcpu);

	msr.data = data;
	msr.index = ecx;
	msr.host_initiated = false;

	svm->next_rip = kvm_rip_read(&svm->vcpu) + 2;
	if (kvm_set_msr(&svm->vcpu, &msr)) {
		trace_kvm_msr_write_ex(ecx, data);
		kvm_inject_gp(&svm->vcpu, 0);
	} else {
		trace_kvm_msr_write(ecx, data);
		skip_emulated_instruction(&svm->vcpu);
	}
	return 1;
}

static int msr_interception(struct vcpu_svm *svm)
{
	if (svm->vmcb->control.exit_info_1)
		return wrmsr_interception(svm);
	else
		return rdmsr_interception(svm);
}

static int interrupt_window_interception(struct vcpu_svm *svm)
{
	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	svm_clear_vintr(svm);
	svm->vmcb->control.int_ctl &= ~V_IRQ_MASK;
	mark_dirty(svm->vmcb, VMCB_INTR);
	++svm->vcpu.stat.irq_window_exits;
	return 1;
}

static int pause_interception(struct vcpu_svm *svm)
{
	kvm_vcpu_on_spin(&(svm->vcpu));
	return 1;
}

static int nop_interception(struct vcpu_svm *svm)
{
	skip_emulated_instruction(&(svm->vcpu));
	return 1;
}

static int monitor_interception(struct vcpu_svm *svm)
{
	printk_once(KERN_WARNING "kvm: MONITOR instruction emulated as NOP!\n");
	return nop_interception(svm);
}

static int mwait_interception(struct vcpu_svm *svm)
{
	printk_once(KERN_WARNING "kvm: MWAIT instruction emulated as NOP!\n");
	return nop_interception(svm);
}

enum avic_ipi_failure_cause {
	AVIC_IPI_FAILURE_INVALID_INT_TYPE,
	AVIC_IPI_FAILURE_TARGET_NOT_RUNNING,
	AVIC_IPI_FAILURE_INVALID_TARGET,
	AVIC_IPI_FAILURE_INVALID_BACKING_PAGE,
};

static int avic_incomplete_ipi_interception(struct vcpu_svm *svm)
{
	u32 icrh = svm->vmcb->control.exit_info_1 >> 32;
	u32 icrl = svm->vmcb->control.exit_info_1;
	u32 id = svm->vmcb->control.exit_info_2 >> 32;
	u32 index = svm->vmcb->control.exit_info_2 & 0xFF;
	struct kvm_lapic *apic = svm->vcpu.arch.apic;

	trace_kvm_avic_incomplete_ipi(svm->vcpu.vcpu_id, icrh, icrl, id, index);

	switch (id) {
	case AVIC_IPI_FAILURE_INVALID_INT_TYPE:
		/*
		 * AVIC hardware handles the generation of
		 * IPIs when the specified Message Type is Fixed
		 * (also known as fixed delivery mode) and
		 * the Trigger Mode is edge-triggered. The hardware
		 * also supports self and broadcast delivery modes
		 * specified via the Destination Shorthand(DSH)
		 * field of the ICRL. Logical and physical APIC ID
		 * formats are supported. All other IPI types cause
		 * a #VMEXIT, which needs to emulated.
		 */
		kvm_lapic_reg_write(apic, APIC_ICR2, icrh);
		kvm_lapic_reg_write(apic, APIC_ICR, icrl);
		break;
	case AVIC_IPI_FAILURE_TARGET_NOT_RUNNING: {
		int i;
		struct kvm_vcpu *vcpu;
		struct kvm *kvm = svm->vcpu.kvm;
		struct kvm_lapic *apic = svm->vcpu.arch.apic;

		/*
		 * At this point, we expect that the AVIC HW has already
		 * set the appropriate IRR bits on the valid target
		 * vcpus. So, we just need to kick the appropriate vcpu.
		 */
		kvm_for_each_vcpu(i, vcpu, kvm) {
			bool m = kvm_apic_match_dest(vcpu, apic,
						     icrl & KVM_APIC_SHORT_MASK,
						     GET_APIC_DEST_FIELD(icrh),
						     icrl & KVM_APIC_DEST_MASK);

			if (m && !avic_vcpu_is_running(vcpu))
				kvm_vcpu_wake_up(vcpu);
		}
		break;
	}
	case AVIC_IPI_FAILURE_INVALID_TARGET:
		break;
	case AVIC_IPI_FAILURE_INVALID_BACKING_PAGE:
		WARN_ONCE(1, "Invalid backing page\n");
		break;
	default:
		pr_err("Unknown IPI interception\n");
	}

	return 1;
}

static u32 *avic_get_logical_id_entry(struct kvm_vcpu *vcpu, u32 ldr, bool flat)
{
	struct kvm_arch *vm_data = &vcpu->kvm->arch;
	int index;
	u32 *logical_apic_id_table;
	int dlid = GET_APIC_LOGICAL_ID(ldr);

	if (!dlid)
		return NULL;

	if (flat) { /* flat */
		index = ffs(dlid) - 1;
		if (index > 7)
			return NULL;
	} else { /* cluster */
		int cluster = (dlid & 0xf0) >> 4;
		int apic = ffs(dlid & 0x0f) - 1;

		if ((apic < 0) || (apic > 7) ||
		    (cluster >= 0xf))
			return NULL;
		index = (cluster << 2) + apic;
	}

	logical_apic_id_table = (u32 *) page_address(vm_data->avic_logical_id_table_page);

	return &logical_apic_id_table[index];
}

static int avic_ldr_write(struct kvm_vcpu *vcpu, u8 g_physical_id, u32 ldr,
			  bool valid)
{
	bool flat;
	u32 *entry, new_entry;

	flat = kvm_lapic_get_reg(vcpu->arch.apic, APIC_DFR) == APIC_DFR_FLAT;
	entry = avic_get_logical_id_entry(vcpu, ldr, flat);
	if (!entry)
		return -EINVAL;

	new_entry = READ_ONCE(*entry);
	new_entry &= ~AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK;
	new_entry |= (g_physical_id & AVIC_LOGICAL_ID_ENTRY_GUEST_PHYSICAL_ID_MASK);
	if (valid)
		new_entry |= AVIC_LOGICAL_ID_ENTRY_VALID_MASK;
	else
		new_entry &= ~AVIC_LOGICAL_ID_ENTRY_VALID_MASK;
	WRITE_ONCE(*entry, new_entry);

	return 0;
}

static int avic_handle_ldr_update(struct kvm_vcpu *vcpu)
{
	int ret;
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 ldr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_LDR);

	if (!ldr)
		return 1;

	ret = avic_ldr_write(vcpu, vcpu->vcpu_id, ldr, true);
	if (ret && svm->ldr_reg) {
		avic_ldr_write(vcpu, 0, svm->ldr_reg, false);
		svm->ldr_reg = 0;
	} else {
		svm->ldr_reg = ldr;
	}
	return ret;
}

static int avic_handle_apic_id_update(struct kvm_vcpu *vcpu)
{
	u64 *old, *new;
	struct vcpu_svm *svm = to_svm(vcpu);
	u32 apic_id_reg = kvm_lapic_get_reg(vcpu->arch.apic, APIC_ID);
	u32 id = (apic_id_reg >> 24) & 0xff;

	if (vcpu->vcpu_id == id)
		return 0;

	old = avic_get_physical_id_entry(vcpu, vcpu->vcpu_id);
	new = avic_get_physical_id_entry(vcpu, id);
	if (!new || !old)
		return 1;

	/* We need to move physical_id_entry to new offset */
	*new = *old;
	*old = 0ULL;
	to_svm(vcpu)->avic_physical_id_cache = new;

	/*
	 * Also update the guest physical APIC ID in the logical
	 * APIC ID table entry if already setup the LDR.
	 */
	if (svm->ldr_reg)
		avic_handle_ldr_update(vcpu);

	return 0;
}

static int avic_handle_dfr_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct kvm_arch *vm_data = &vcpu->kvm->arch;
	u32 dfr = kvm_lapic_get_reg(vcpu->arch.apic, APIC_DFR);
	u32 mod = (dfr >> 28) & 0xf;

	/*
	 * We assume that all local APICs are using the same type.
	 * If this changes, we need to flush the AVIC logical
	 * APID id table.
	 */
	if (vm_data->ldr_mode == mod)
		return 0;

	clear_page(page_address(vm_data->avic_logical_id_table_page));
	vm_data->ldr_mode = mod;

	if (svm->ldr_reg)
		avic_handle_ldr_update(vcpu);
	return 0;
}

static int avic_unaccel_trap_write(struct vcpu_svm *svm)
{
	struct kvm_lapic *apic = svm->vcpu.arch.apic;
	u32 offset = svm->vmcb->control.exit_info_1 &
				AVIC_UNACCEL_ACCESS_OFFSET_MASK;

	switch (offset) {
	case APIC_ID:
		if (avic_handle_apic_id_update(&svm->vcpu))
			return 0;
		break;
	case APIC_LDR:
		if (avic_handle_ldr_update(&svm->vcpu))
			return 0;
		break;
	case APIC_DFR:
		avic_handle_dfr_update(&svm->vcpu);
		break;
	default:
		break;
	}

	kvm_lapic_reg_write(apic, offset, kvm_lapic_get_reg(apic, offset));

	return 1;
}

static bool is_avic_unaccelerated_access_trap(u32 offset)
{
	bool ret = false;

	switch (offset) {
	case APIC_ID:
	case APIC_EOI:
	case APIC_RRR:
	case APIC_LDR:
	case APIC_DFR:
	case APIC_SPIV:
	case APIC_ESR:
	case APIC_ICR:
	case APIC_LVTT:
	case APIC_LVTTHMR:
	case APIC_LVTPC:
	case APIC_LVT0:
	case APIC_LVT1:
	case APIC_LVTERR:
	case APIC_TMICT:
	case APIC_TDCR:
		ret = true;
		break;
	default:
		break;
	}
	return ret;
}

static int avic_unaccelerated_access_interception(struct vcpu_svm *svm)
{
	int ret = 0;
	u32 offset = svm->vmcb->control.exit_info_1 &
		     AVIC_UNACCEL_ACCESS_OFFSET_MASK;
	u32 vector = svm->vmcb->control.exit_info_2 &
		     AVIC_UNACCEL_ACCESS_VECTOR_MASK;
	bool write = (svm->vmcb->control.exit_info_1 >> 32) &
		     AVIC_UNACCEL_ACCESS_WRITE_MASK;
	bool trap = is_avic_unaccelerated_access_trap(offset);

	trace_kvm_avic_unaccelerated_access(svm->vcpu.vcpu_id, offset,
					    trap, write, vector);
	if (trap) {
		/* Handling Trap */
		WARN_ONCE(!write, "svm: Handling trap read.\n");
		ret = avic_unaccel_trap_write(svm);
	} else {
		/* Handling Fault */
		ret = (emulate_instruction(&svm->vcpu, 0) == EMULATE_DONE);
	}

	return ret;
}

static int (*const svm_exit_handlers[])(struct vcpu_svm *svm) = {
	[SVM_EXIT_READ_CR0]			= cr_interception,
	[SVM_EXIT_READ_CR3]			= cr_interception,
	[SVM_EXIT_READ_CR4]			= cr_interception,
	[SVM_EXIT_READ_CR8]			= cr_interception,
	[SVM_EXIT_CR0_SEL_WRITE]		= cr_interception,
	[SVM_EXIT_WRITE_CR0]			= cr_interception,
	[SVM_EXIT_WRITE_CR3]			= cr_interception,
	[SVM_EXIT_WRITE_CR4]			= cr_interception,
	[SVM_EXIT_WRITE_CR8]			= cr8_write_interception,
	[SVM_EXIT_READ_DR0]			= dr_interception,
	[SVM_EXIT_READ_DR1]			= dr_interception,
	[SVM_EXIT_READ_DR2]			= dr_interception,
	[SVM_EXIT_READ_DR3]			= dr_interception,
	[SVM_EXIT_READ_DR4]			= dr_interception,
	[SVM_EXIT_READ_DR5]			= dr_interception,
	[SVM_EXIT_READ_DR6]			= dr_interception,
	[SVM_EXIT_READ_DR7]			= dr_interception,
	[SVM_EXIT_WRITE_DR0]			= dr_interception,
	[SVM_EXIT_WRITE_DR1]			= dr_interception,
	[SVM_EXIT_WRITE_DR2]			= dr_interception,
	[SVM_EXIT_WRITE_DR3]			= dr_interception,
	[SVM_EXIT_WRITE_DR4]			= dr_interception,
	[SVM_EXIT_WRITE_DR5]			= dr_interception,
	[SVM_EXIT_WRITE_DR6]			= dr_interception,
	[SVM_EXIT_WRITE_DR7]			= dr_interception,
	[SVM_EXIT_EXCP_BASE + DB_VECTOR]	= db_interception,
	[SVM_EXIT_EXCP_BASE + BP_VECTOR]	= bp_interception,
	[SVM_EXIT_EXCP_BASE + UD_VECTOR]	= ud_interception,
	[SVM_EXIT_EXCP_BASE + PF_VECTOR]	= pf_interception,
	[SVM_EXIT_EXCP_BASE + NM_VECTOR]	= nm_interception,
	[SVM_EXIT_EXCP_BASE + MC_VECTOR]	= mc_interception,
	[SVM_EXIT_EXCP_BASE + AC_VECTOR]	= ac_interception,
	[SVM_EXIT_INTR]				= intr_interception,
	[SVM_EXIT_NMI]				= nmi_interception,
	[SVM_EXIT_SMI]				= nop_on_interception,
	[SVM_EXIT_INIT]				= nop_on_interception,
	[SVM_EXIT_VINTR]			= interrupt_window_interception,
	[SVM_EXIT_RDPMC]			= rdpmc_interception,
	[SVM_EXIT_CPUID]			= cpuid_interception,
	[SVM_EXIT_IRET]                         = iret_interception,
	[SVM_EXIT_INVD]                         = emulate_on_interception,
	[SVM_EXIT_PAUSE]			= pause_interception,
	[SVM_EXIT_HLT]				= halt_interception,
	[SVM_EXIT_INVLPG]			= invlpg_interception,
	[SVM_EXIT_INVLPGA]			= invlpga_interception,
	[SVM_EXIT_IOIO]				= io_interception,
	[SVM_EXIT_MSR]				= msr_interception,
	[SVM_EXIT_TASK_SWITCH]			= task_switch_interception,
	[SVM_EXIT_SHUTDOWN]			= shutdown_interception,
	[SVM_EXIT_VMRUN]			= vmrun_interception,
	[SVM_EXIT_VMMCALL]			= vmmcall_interception,
	[SVM_EXIT_VMLOAD]			= vmload_interception,
	[SVM_EXIT_VMSAVE]			= vmsave_interception,
	[SVM_EXIT_STGI]				= stgi_interception,
	[SVM_EXIT_CLGI]				= clgi_interception,
	[SVM_EXIT_SKINIT]			= skinit_interception,
	[SVM_EXIT_WBINVD]                       = wbinvd_interception,
	[SVM_EXIT_MONITOR]			= monitor_interception,
	[SVM_EXIT_MWAIT]			= mwait_interception,
	[SVM_EXIT_XSETBV]			= xsetbv_interception,
	[SVM_EXIT_NPF]				= pf_interception,
	[SVM_EXIT_RSM]                          = emulate_on_interception,
	[SVM_EXIT_AVIC_INCOMPLETE_IPI]		= avic_incomplete_ipi_interception,
	[SVM_EXIT_AVIC_UNACCELERATED_ACCESS]	= avic_unaccelerated_access_interception,
};

static void dump_vmcb(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;
	struct vmcb_save_area *save = &svm->vmcb->save;

	pr_err("VMCB Control Area:\n");
	pr_err("%-20s%04x\n", "cr_read:", control->intercept_cr & 0xffff);
	pr_err("%-20s%04x\n", "cr_write:", control->intercept_cr >> 16);
	pr_err("%-20s%04x\n", "dr_read:", control->intercept_dr & 0xffff);
	pr_err("%-20s%04x\n", "dr_write:", control->intercept_dr >> 16);
	pr_err("%-20s%08x\n", "exceptions:", control->intercept_exceptions);
	pr_err("%-20s%016llx\n", "intercepts:", control->intercept);
	pr_err("%-20s%d\n", "pause filter count:", control->pause_filter_count);
	pr_err("%-20s%016llx\n", "iopm_base_pa:", control->iopm_base_pa);
	pr_err("%-20s%016llx\n", "msrpm_base_pa:", control->msrpm_base_pa);
	pr_err("%-20s%016llx\n", "tsc_offset:", control->tsc_offset);
	pr_err("%-20s%d\n", "asid:", control->asid);
	pr_err("%-20s%d\n", "tlb_ctl:", control->tlb_ctl);
	pr_err("%-20s%08x\n", "int_ctl:", control->int_ctl);
	pr_err("%-20s%08x\n", "int_vector:", control->int_vector);
	pr_err("%-20s%08x\n", "int_state:", control->int_state);
	pr_err("%-20s%08x\n", "exit_code:", control->exit_code);
	pr_err("%-20s%016llx\n", "exit_info1:", control->exit_info_1);
	pr_err("%-20s%016llx\n", "exit_info2:", control->exit_info_2);
	pr_err("%-20s%08x\n", "exit_int_info:", control->exit_int_info);
	pr_err("%-20s%08x\n", "exit_int_info_err:", control->exit_int_info_err);
	pr_err("%-20s%lld\n", "nested_ctl:", control->nested_ctl);
	pr_err("%-20s%016llx\n", "nested_cr3:", control->nested_cr3);
	pr_err("%-20s%016llx\n", "avic_vapic_bar:", control->avic_vapic_bar);
	pr_err("%-20s%08x\n", "event_inj:", control->event_inj);
	pr_err("%-20s%08x\n", "event_inj_err:", control->event_inj_err);
	pr_err("%-20s%lld\n", "lbr_ctl:", control->lbr_ctl);
	pr_err("%-20s%016llx\n", "next_rip:", control->next_rip);
	pr_err("%-20s%016llx\n", "avic_backing_page:", control->avic_backing_page);
	pr_err("%-20s%016llx\n", "avic_logical_id:", control->avic_logical_id);
	pr_err("%-20s%016llx\n", "avic_physical_id:", control->avic_physical_id);
	pr_err("VMCB State Save Area:\n");
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "es:",
	       save->es.selector, save->es.attrib,
	       save->es.limit, save->es.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "cs:",
	       save->cs.selector, save->cs.attrib,
	       save->cs.limit, save->cs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ss:",
	       save->ss.selector, save->ss.attrib,
	       save->ss.limit, save->ss.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ds:",
	       save->ds.selector, save->ds.attrib,
	       save->ds.limit, save->ds.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "fs:",
	       save->fs.selector, save->fs.attrib,
	       save->fs.limit, save->fs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "gs:",
	       save->gs.selector, save->gs.attrib,
	       save->gs.limit, save->gs.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "gdtr:",
	       save->gdtr.selector, save->gdtr.attrib,
	       save->gdtr.limit, save->gdtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "ldtr:",
	       save->ldtr.selector, save->ldtr.attrib,
	       save->ldtr.limit, save->ldtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "idtr:",
	       save->idtr.selector, save->idtr.attrib,
	       save->idtr.limit, save->idtr.base);
	pr_err("%-5s s: %04x a: %04x l: %08x b: %016llx\n",
	       "tr:",
	       save->tr.selector, save->tr.attrib,
	       save->tr.limit, save->tr.base);
	pr_err("cpl:            %d                efer:         %016llx\n",
		save->cpl, save->efer);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cr0:", save->cr0, "cr2:", save->cr2);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cr3:", save->cr3, "cr4:", save->cr4);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "dr6:", save->dr6, "dr7:", save->dr7);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "rip:", save->rip, "rflags:", save->rflags);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "rsp:", save->rsp, "rax:", save->rax);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "star:", save->star, "lstar:", save->lstar);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "cstar:", save->cstar, "sfmask:", save->sfmask);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "kernel_gs_base:", save->kernel_gs_base,
	       "sysenter_cs:", save->sysenter_cs);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "sysenter_esp:", save->sysenter_esp,
	       "sysenter_eip:", save->sysenter_eip);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "gpat:", save->g_pat, "dbgctl:", save->dbgctl);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "br_from:", save->br_from, "br_to:", save->br_to);
	pr_err("%-15s %016llx %-13s %016llx\n",
	       "excp_from:", save->last_excp_from,
	       "excp_to:", save->last_excp_to);
}

static void svm_get_exit_info(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2)
{
	struct vmcb_control_area *control = &to_svm(vcpu)->vmcb->control;

	*info1 = control->exit_info_1;
	*info2 = control->exit_info_2;
}

static int handle_exit(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct kvm_run *kvm_run = vcpu->run;
	u32 exit_code = svm->vmcb->control.exit_code;

	trace_kvm_exit(exit_code, vcpu, KVM_ISA_SVM);

	if (!is_cr_intercept(svm, INTERCEPT_CR0_WRITE))
		vcpu->arch.cr0 = svm->vmcb->save.cr0;
	if (npt_enabled)
		vcpu->arch.cr3 = svm->vmcb->save.cr3;

	if (unlikely(svm->nested.exit_required)) {
		nested_svm_vmexit(svm);
		svm->nested.exit_required = false;

		return 1;
	}

	if (is_guest_mode(vcpu)) {
		int vmexit;

		trace_kvm_nested_vmexit(svm->vmcb->save.rip, exit_code,
					svm->vmcb->control.exit_info_1,
					svm->vmcb->control.exit_info_2,
					svm->vmcb->control.exit_int_info,
					svm->vmcb->control.exit_int_info_err,
					KVM_ISA_SVM);

		vmexit = nested_svm_exit_special(svm);

		if (vmexit == NESTED_EXIT_CONTINUE)
			vmexit = nested_svm_exit_handled(svm);

		if (vmexit == NESTED_EXIT_DONE)
			return 1;
	}

	svm_complete_interrupts(svm);

	if (svm->vmcb->control.exit_code == SVM_EXIT_ERR) {
		kvm_run->exit_reason = KVM_EXIT_FAIL_ENTRY;
		kvm_run->fail_entry.hardware_entry_failure_reason
			= svm->vmcb->control.exit_code;
		pr_err("KVM: FAILED VMRUN WITH VMCB:\n");
		dump_vmcb(vcpu);
		return 0;
	}

	if (is_external_interrupt(svm->vmcb->control.exit_int_info) &&
	    exit_code != SVM_EXIT_EXCP_BASE + PF_VECTOR &&
	    exit_code != SVM_EXIT_NPF && exit_code != SVM_EXIT_TASK_SWITCH &&
	    exit_code != SVM_EXIT_INTR && exit_code != SVM_EXIT_NMI)
		printk(KERN_ERR "%s: unexpected exit_int_info 0x%x "
		       "exit_code 0x%x\n",
		       __func__, svm->vmcb->control.exit_int_info,
		       exit_code);

	if (exit_code >= ARRAY_SIZE(svm_exit_handlers)
	    || !svm_exit_handlers[exit_code]) {
		WARN_ONCE(1, "svm: unexpected exit reason 0x%x\n", exit_code);
		kvm_queue_exception(vcpu, UD_VECTOR);
		return 1;
	}

	return svm_exit_handlers[exit_code](svm);
}

static void reload_tss(struct kvm_vcpu *vcpu)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *sd = per_cpu(svm_data, cpu);
	sd->tss_desc->type = 9; /* available 32/64-bit TSS */
	load_TR_desc();
}

static void pre_svm_run(struct vcpu_svm *svm)
{
	int cpu = raw_smp_processor_id();

	struct svm_cpu_data *sd = per_cpu(svm_data, cpu);

	/* FIXME: handle wraparound of asid_generation */
	if (svm->asid_generation != sd->asid_generation)
		new_asid(svm, sd);
}

static void svm_inject_nmi(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->control.event_inj = SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_NMI;
	vcpu->arch.hflags |= HF_NMI_MASK;
	set_intercept(svm, INTERCEPT_IRET);
	++vcpu->stat.nmi_injections;
}

static inline void svm_inject_irq(struct vcpu_svm *svm, int irq)
{
	struct vmcb_control_area *control;

	/* The following fields are ignored when AVIC is enabled */
	control = &svm->vmcb->control;
	control->int_vector = irq;
	control->int_ctl &= ~V_INTR_PRIO_MASK;
	control->int_ctl |= V_IRQ_MASK |
		((/*control->int_vector >> 4*/ 0xf) << V_INTR_PRIO_SHIFT);
	mark_dirty(svm->vmcb, VMCB_INTR);
}

static void svm_set_irq(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	BUG_ON(!(gif_set(svm)));

	trace_kvm_inj_virq(vcpu->arch.interrupt.nr);
	++vcpu->stat.irq_injections;

	svm->vmcb->control.event_inj = vcpu->arch.interrupt.nr |
		SVM_EVTINJ_VALID | SVM_EVTINJ_TYPE_INTR;
}

static inline bool svm_nested_virtualize_tpr(struct kvm_vcpu *vcpu)
{
	return is_guest_mode(vcpu) && (vcpu->arch.hflags & HF_VINTR_MASK);
}

static void update_cr8_intercept(struct kvm_vcpu *vcpu, int tpr, int irr)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm_nested_virtualize_tpr(vcpu) ||
	    kvm_vcpu_apicv_active(vcpu))
		return;

	clr_cr_intercept(svm, INTERCEPT_CR8_WRITE);

	if (irr == -1)
		return;

	if (tpr >= irr)
		set_cr_intercept(svm, INTERCEPT_CR8_WRITE);
}

static void svm_set_virtual_x2apic_mode(struct kvm_vcpu *vcpu, bool set)
{
	return;
}

static bool svm_get_enable_apicv(void)
{
	return avic;
}

static void svm_hwapic_irr_update(struct kvm_vcpu *vcpu, int max_irr)
{
}

static void svm_hwapic_isr_update(struct kvm_vcpu *vcpu, int max_isr)
{
}

/* Note: Currently only used by Hyper-V. */
static void svm_refresh_apicv_exec_ctrl(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;

	if (!avic)
		return;

	vmcb->control.int_ctl &= ~AVIC_ENABLE_MASK;
	mark_dirty(vmcb, VMCB_INTR);
}

static void svm_load_eoi_exitmap(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap)
{
	return;
}

static void svm_sync_pir_to_irr(struct kvm_vcpu *vcpu)
{
	return;
}

static void svm_deliver_avic_intr(struct kvm_vcpu *vcpu, int vec)
{
	kvm_lapic_set_irr(vec, vcpu->arch.apic);
	smp_mb__after_atomic();

	if (avic_vcpu_is_running(vcpu))
		wrmsrl(SVM_AVIC_DOORBELL,
		       kvm_cpu_get_apicid(vcpu->cpu));
	else
		kvm_vcpu_wake_up(vcpu);
}

static void svm_ir_list_del(struct vcpu_svm *svm, struct amd_iommu_pi_data *pi)
{
	unsigned long flags;
	struct amd_svm_iommu_ir *cur;

	spin_lock_irqsave(&svm->ir_list_lock, flags);
	list_for_each_entry(cur, &svm->ir_list, node) {
		if (cur->data != pi->ir_data)
			continue;
		list_del(&cur->node);
		kfree(cur);
		break;
	}
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
}

static int svm_ir_list_add(struct vcpu_svm *svm, struct amd_iommu_pi_data *pi)
{
	int ret = 0;
	unsigned long flags;
	struct amd_svm_iommu_ir *ir;

	/**
	 * In some cases, the existing irte is updaed and re-set,
	 * so we need to check here if it's already been * added
	 * to the ir_list.
	 */
	if (pi->ir_data && (pi->prev_ga_tag != 0)) {
		struct kvm *kvm = svm->vcpu.kvm;
		u32 vcpu_id = AVIC_GATAG_TO_VCPUID(pi->prev_ga_tag);
		struct kvm_vcpu *prev_vcpu = kvm_get_vcpu_by_id(kvm, vcpu_id);
		struct vcpu_svm *prev_svm;

		if (!prev_vcpu) {
			ret = -EINVAL;
			goto out;
		}

		prev_svm = to_svm(prev_vcpu);
		svm_ir_list_del(prev_svm, pi);
	}

	/**
	 * Allocating new amd_iommu_pi_data, which will get
	 * add to the per-vcpu ir_list.
	 */
	ir = kzalloc(sizeof(struct amd_svm_iommu_ir), GFP_KERNEL);
	if (!ir) {
		ret = -ENOMEM;
		goto out;
	}
	ir->data = pi->ir_data;

	spin_lock_irqsave(&svm->ir_list_lock, flags);
	list_add(&ir->node, &svm->ir_list);
	spin_unlock_irqrestore(&svm->ir_list_lock, flags);
out:
	return ret;
}

/**
 * Note:
 * The HW cannot support posting multicast/broadcast
 * interrupts to a vCPU. So, we still use legacy interrupt
 * remapping for these kind of interrupts.
 *
 * For lowest-priority interrupts, we only support
 * those with single CPU as the destination, e.g. user
 * configures the interrupts via /proc/irq or uses
 * irqbalance to make the interrupts single-CPU.
 */
static int
get_pi_vcpu_info(struct kvm *kvm, struct kvm_kernel_irq_routing_entry *e,
		 struct vcpu_data *vcpu_info, struct vcpu_svm **svm)
{
	struct kvm_lapic_irq irq;
	struct kvm_vcpu *vcpu = NULL;

	kvm_set_msi_irq(kvm, e, &irq);

	if (!kvm_intr_is_single_vcpu(kvm, &irq, &vcpu)) {
		pr_debug("SVM: %s: use legacy intr remap mode for irq %u\n",
			 __func__, irq.vector);
		return -1;
	}

	pr_debug("SVM: %s: use GA mode for irq %u\n", __func__,
		 irq.vector);
	*svm = to_svm(vcpu);
	vcpu_info->pi_desc_addr = page_to_phys((*svm)->avic_backing_page);
	vcpu_info->vector = irq.vector;

	return 0;
}

/*
 * svm_update_pi_irte - set IRTE for Posted-Interrupts
 *
 * @kvm: kvm
 * @host_irq: host irq of the interrupt
 * @guest_irq: gsi of the interrupt
 * @set: set or unset PI
 * returns 0 on success, < 0 on failure
 */
static int svm_update_pi_irte(struct kvm *kvm, unsigned int host_irq,
			      uint32_t guest_irq, bool set)
{
	struct kvm_kernel_irq_routing_entry *e;
	struct kvm_irq_routing_table *irq_rt;
	int idx, ret = -EINVAL;

	if (!kvm_arch_has_assigned_device(kvm) ||
	    !irq_remapping_cap(IRQ_POSTING_CAP))
		return 0;

	pr_debug("SVM: %s: host_irq=%#x, guest_irq=%#x, set=%#x\n",
		 __func__, host_irq, guest_irq, set);

	idx = srcu_read_lock(&kvm->irq_srcu);
	irq_rt = srcu_dereference(kvm->irq_routing, &kvm->irq_srcu);
	WARN_ON(guest_irq >= irq_rt->nr_rt_entries);

	hlist_for_each_entry(e, &irq_rt->map[guest_irq], link) {
		struct vcpu_data vcpu_info;
		struct vcpu_svm *svm = NULL;

		if (e->type != KVM_IRQ_ROUTING_MSI)
			continue;

		/**
		 * Here, we setup with legacy mode in the following cases:
		 * 1. When cannot target interrupt to a specific vcpu.
		 * 2. Unsetting posted interrupt.
		 * 3. APIC virtialization is disabled for the vcpu.
		 */
		if (!get_pi_vcpu_info(kvm, e, &vcpu_info, &svm) && set &&
		    kvm_vcpu_apicv_active(&svm->vcpu)) {
			struct amd_iommu_pi_data pi;

			/* Try to enable guest_mode in IRTE */
			pi.base = page_to_phys(svm->avic_backing_page) & AVIC_HPA_MASK;
			pi.ga_tag = AVIC_GATAG(kvm->arch.avic_vm_id,
						     svm->vcpu.vcpu_id);
			pi.is_guest_mode = true;
			pi.vcpu_data = &vcpu_info;
			ret = irq_set_vcpu_affinity(host_irq, &pi);

			/**
			 * Here, we successfully setting up vcpu affinity in
			 * IOMMU guest mode. Now, we need to store the posted
			 * interrupt information in a per-vcpu ir_list so that
			 * we can reference to them directly when we update vcpu
			 * scheduling information in IOMMU irte.
			 */
			if (!ret && pi.is_guest_mode)
				svm_ir_list_add(svm, &pi);
		} else {
			/* Use legacy mode in IRTE */
			struct amd_iommu_pi_data pi;

			/**
			 * Here, pi is used to:
			 * - Tell IOMMU to use legacy mode for this interrupt.
			 * - Retrieve ga_tag of prior interrupt remapping data.
			 */
			pi.is_guest_mode = false;
			ret = irq_set_vcpu_affinity(host_irq, &pi);

			/**
			 * Check if the posted interrupt was previously
			 * setup with the guest_mode by checking if the ga_tag
			 * was cached. If so, we need to clean up the per-vcpu
			 * ir_list.
			 */
			if (!ret && pi.prev_ga_tag) {
				int id = AVIC_GATAG_TO_VCPUID(pi.prev_ga_tag);
				struct kvm_vcpu *vcpu;

				vcpu = kvm_get_vcpu_by_id(kvm, id);
				if (vcpu)
					svm_ir_list_del(to_svm(vcpu), &pi);
			}
		}

		if (!ret && svm) {
			trace_kvm_pi_irte_update(svm->vcpu.vcpu_id,
						 host_irq, e->gsi,
						 vcpu_info.vector,
						 vcpu_info.pi_desc_addr, set);
		}

		if (ret < 0) {
			pr_err("%s: failed to update PI IRTE\n", __func__);
			goto out;
		}
	}

	ret = 0;
out:
	srcu_read_unlock(&kvm->irq_srcu, idx);
	return ret;
}

static int svm_nmi_allowed(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;
	int ret;
	ret = !(vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK) &&
	      !(svm->vcpu.arch.hflags & HF_NMI_MASK);
	ret = ret && gif_set(svm) && nested_svm_nmi(svm);

	return ret;
}

static bool svm_get_nmi_mask(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	return !!(svm->vcpu.arch.hflags & HF_NMI_MASK);
}

static void svm_set_nmi_mask(struct kvm_vcpu *vcpu, bool masked)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (masked) {
		svm->vcpu.arch.hflags |= HF_NMI_MASK;
		set_intercept(svm, INTERCEPT_IRET);
	} else {
		svm->vcpu.arch.hflags &= ~HF_NMI_MASK;
		clr_intercept(svm, INTERCEPT_IRET);
	}
}

static int svm_interrupt_allowed(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb *vmcb = svm->vmcb;
	int ret;

	if (!gif_set(svm) ||
	     (vmcb->control.int_state & SVM_INTERRUPT_SHADOW_MASK))
		return 0;

	ret = !!(kvm_get_rflags(vcpu) & X86_EFLAGS_IF);

	if (is_guest_mode(vcpu))
		return ret && !(svm->vcpu.arch.hflags & HF_VINTR_MASK);

	return ret;
}

static void enable_irq_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (kvm_vcpu_apicv_active(vcpu))
		return;

	/*
	 * In case GIF=0 we can't rely on the CPU to tell us when GIF becomes
	 * 1, because that's a separate STGI/VMRUN intercept.  The next time we
	 * get that intercept, this function will be called again though and
	 * we'll get the vintr intercept.
	 */
	if (gif_set(svm) && nested_svm_intr(svm)) {
		svm_set_vintr(svm);
		svm_inject_irq(svm, 0x0);
	}
}

static void enable_nmi_window(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if ((svm->vcpu.arch.hflags & (HF_NMI_MASK | HF_IRET_MASK))
	    == HF_NMI_MASK)
		return; /* IRET will cause a vm exit */

	/*
	 * Something prevents NMI from been injected. Single step over possible
	 * problem (IRET or exception injection or interrupt shadow)
	 */
	svm->nmi_singlestep = true;
	svm->vmcb->save.rflags |= (X86_EFLAGS_TF | X86_EFLAGS_RF);
}

static int svm_set_tss_addr(struct kvm *kvm, unsigned int addr)
{
	return 0;
}

static void svm_flush_tlb(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (static_cpu_has(X86_FEATURE_FLUSHBYASID))
		svm->vmcb->control.tlb_ctl = TLB_CONTROL_FLUSH_ASID;
	else
		svm->asid_generation--;
}

static void svm_prepare_guest_switch(struct kvm_vcpu *vcpu)
{
}

static inline void sync_cr8_to_lapic(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	if (svm_nested_virtualize_tpr(vcpu))
		return;

	if (!is_cr_intercept(svm, INTERCEPT_CR8_WRITE)) {
		int cr8 = svm->vmcb->control.int_ctl & V_TPR_MASK;
		kvm_set_cr8(vcpu, cr8);
	}
}

static inline void sync_lapic_to_cr8(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	u64 cr8;

	if (svm_nested_virtualize_tpr(vcpu) ||
	    kvm_vcpu_apicv_active(vcpu))
		return;

	cr8 = kvm_get_cr8(vcpu);
	svm->vmcb->control.int_ctl &= ~V_TPR_MASK;
	svm->vmcb->control.int_ctl |= cr8 & V_TPR_MASK;
}

static void svm_complete_interrupts(struct vcpu_svm *svm)
{
	u8 vector;
	int type;
	u32 exitintinfo = svm->vmcb->control.exit_int_info;
	unsigned int3_injected = svm->int3_injected;

	svm->int3_injected = 0;

	/*
	 * If we've made progress since setting HF_IRET_MASK, we've
	 * executed an IRET and can allow NMI injection.
	 */
	if ((svm->vcpu.arch.hflags & HF_IRET_MASK)
	    && kvm_rip_read(&svm->vcpu) != svm->nmi_iret_rip) {
		svm->vcpu.arch.hflags &= ~(HF_NMI_MASK | HF_IRET_MASK);
		kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);
	}

	svm->vcpu.arch.nmi_injected = false;
	kvm_clear_exception_queue(&svm->vcpu);
	kvm_clear_interrupt_queue(&svm->vcpu);

	if (!(exitintinfo & SVM_EXITINTINFO_VALID))
		return;

	kvm_make_request(KVM_REQ_EVENT, &svm->vcpu);

	vector = exitintinfo & SVM_EXITINTINFO_VEC_MASK;
	type = exitintinfo & SVM_EXITINTINFO_TYPE_MASK;

	switch (type) {
	case SVM_EXITINTINFO_TYPE_NMI:
		svm->vcpu.arch.nmi_injected = true;
		break;
	case SVM_EXITINTINFO_TYPE_EXEPT:
		/*
		 * In case of software exceptions, do not reinject the vector,
		 * but re-execute the instruction instead. Rewind RIP first
		 * if we emulated INT3 before.
		 */
		if (kvm_exception_is_soft(vector)) {
			if (vector == BP_VECTOR && int3_injected &&
			    kvm_is_linear_rip(&svm->vcpu, svm->int3_rip))
				kvm_rip_write(&svm->vcpu,
					      kvm_rip_read(&svm->vcpu) -
					      int3_injected);
			break;
		}
		if (exitintinfo & SVM_EXITINTINFO_VALID_ERR) {
			u32 err = svm->vmcb->control.exit_int_info_err;
			kvm_requeue_exception_e(&svm->vcpu, vector, err);

		} else
			kvm_requeue_exception(&svm->vcpu, vector);
		break;
	case SVM_EXITINTINFO_TYPE_INTR:
		kvm_queue_interrupt(&svm->vcpu, vector, false);
		break;
	default:
		break;
	}
}

static void svm_cancel_injection(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct vmcb_control_area *control = &svm->vmcb->control;

	control->exit_int_info = control->event_inj;
	control->exit_int_info_err = control->event_inj_err;
	control->event_inj = 0;
	svm_complete_interrupts(svm);
}

static void svm_vcpu_run(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.rax = vcpu->arch.regs[VCPU_REGS_RAX];
	svm->vmcb->save.rsp = vcpu->arch.regs[VCPU_REGS_RSP];
	svm->vmcb->save.rip = vcpu->arch.regs[VCPU_REGS_RIP];

	/*
	 * A vmexit emulation is required before the vcpu can be executed
	 * again.
	 */
	if (unlikely(svm->nested.exit_required))
		return;

	pre_svm_run(svm);

	sync_lapic_to_cr8(vcpu);

	svm->vmcb->save.cr2 = vcpu->arch.cr2;

	clgi();

	local_irq_enable();

	asm volatile (
		"push %%" _ASM_BP "; \n\t"
		"mov %c[rbx](%[svm]), %%" _ASM_BX " \n\t"
		"mov %c[rcx](%[svm]), %%" _ASM_CX " \n\t"
		"mov %c[rdx](%[svm]), %%" _ASM_DX " \n\t"
		"mov %c[rsi](%[svm]), %%" _ASM_SI " \n\t"
		"mov %c[rdi](%[svm]), %%" _ASM_DI " \n\t"
		"mov %c[rbp](%[svm]), %%" _ASM_BP " \n\t"
#ifdef CONFIG_X86_64
		"mov %c[r8](%[svm]),  %%r8  \n\t"
		"mov %c[r9](%[svm]),  %%r9  \n\t"
		"mov %c[r10](%[svm]), %%r10 \n\t"
		"mov %c[r11](%[svm]), %%r11 \n\t"
		"mov %c[r12](%[svm]), %%r12 \n\t"
		"mov %c[r13](%[svm]), %%r13 \n\t"
		"mov %c[r14](%[svm]), %%r14 \n\t"
		"mov %c[r15](%[svm]), %%r15 \n\t"
#endif

		/* Enter guest mode */
		"push %%" _ASM_AX " \n\t"
		"mov %c[vmcb](%[svm]), %%" _ASM_AX " \n\t"
		__ex(SVM_VMLOAD) "\n\t"
		__ex(SVM_VMRUN) "\n\t"
		__ex(SVM_VMSAVE) "\n\t"
		"pop %%" _ASM_AX " \n\t"

		/* Save guest registers, load host registers */
		"mov %%" _ASM_BX ", %c[rbx](%[svm]) \n\t"
		"mov %%" _ASM_CX ", %c[rcx](%[svm]) \n\t"
		"mov %%" _ASM_DX ", %c[rdx](%[svm]) \n\t"
		"mov %%" _ASM_SI ", %c[rsi](%[svm]) \n\t"
		"mov %%" _ASM_DI ", %c[rdi](%[svm]) \n\t"
		"mov %%" _ASM_BP ", %c[rbp](%[svm]) \n\t"
#ifdef CONFIG_X86_64
		"mov %%r8,  %c[r8](%[svm]) \n\t"
		"mov %%r9,  %c[r9](%[svm]) \n\t"
		"mov %%r10, %c[r10](%[svm]) \n\t"
		"mov %%r11, %c[r11](%[svm]) \n\t"
		"mov %%r12, %c[r12](%[svm]) \n\t"
		"mov %%r13, %c[r13](%[svm]) \n\t"
		"mov %%r14, %c[r14](%[svm]) \n\t"
		"mov %%r15, %c[r15](%[svm]) \n\t"
#endif
		"pop %%" _ASM_BP
		:
		: [svm]"a"(svm),
		  [vmcb]"i"(offsetof(struct vcpu_svm, vmcb_pa)),
		  [rbx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RBX])),
		  [rcx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RCX])),
		  [rdx]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RDX])),
		  [rsi]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RSI])),
		  [rdi]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RDI])),
		  [rbp]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_RBP]))
#ifdef CONFIG_X86_64
		  , [r8]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R8])),
		  [r9]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R9])),
		  [r10]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R10])),
		  [r11]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R11])),
		  [r12]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R12])),
		  [r13]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R13])),
		  [r14]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R14])),
		  [r15]"i"(offsetof(struct vcpu_svm, vcpu.arch.regs[VCPU_REGS_R15]))
#endif
		: "cc", "memory"
#ifdef CONFIG_X86_64
		, "rbx", "rcx", "rdx", "rsi", "rdi"
		, "r8", "r9", "r10", "r11" , "r12", "r13", "r14", "r15"
#else
		, "ebx", "ecx", "edx", "esi", "edi"
#endif
		);

#ifdef CONFIG_X86_64
	wrmsrl(MSR_GS_BASE, svm->host.gs_base);
#else
	loadsegment(fs, svm->host.fs);
#ifndef CONFIG_X86_32_LAZY_GS
	loadsegment(gs, svm->host.gs);
#endif
#endif

	reload_tss(vcpu);

	local_irq_disable();

	vcpu->arch.cr2 = svm->vmcb->save.cr2;
	vcpu->arch.regs[VCPU_REGS_RAX] = svm->vmcb->save.rax;
	vcpu->arch.regs[VCPU_REGS_RSP] = svm->vmcb->save.rsp;
	vcpu->arch.regs[VCPU_REGS_RIP] = svm->vmcb->save.rip;

	if (unlikely(svm->vmcb->control.exit_code == SVM_EXIT_NMI))
		kvm_before_handle_nmi(&svm->vcpu);

	stgi();

	/* Any pending NMI will happen here */

	if (unlikely(svm->vmcb->control.exit_code == SVM_EXIT_NMI))
		kvm_after_handle_nmi(&svm->vcpu);

	sync_cr8_to_lapic(vcpu);

	svm->next_rip = 0;

	svm->vmcb->control.tlb_ctl = TLB_CONTROL_DO_NOTHING;

	/* if exit due to PF check for async PF */
	if (svm->vmcb->control.exit_code == SVM_EXIT_EXCP_BASE + PF_VECTOR)
		svm->apf_reason = kvm_read_and_reset_pf_reason();

	if (npt_enabled) {
		vcpu->arch.regs_avail &= ~(1 << VCPU_EXREG_PDPTR);
		vcpu->arch.regs_dirty &= ~(1 << VCPU_EXREG_PDPTR);
	}

	/*
	 * We need to handle MC intercepts here before the vcpu has a chance to
	 * change the physical cpu
	 */
	if (unlikely(svm->vmcb->control.exit_code ==
		     SVM_EXIT_EXCP_BASE + MC_VECTOR))
		svm_handle_mce(svm);

	mark_all_clean(svm->vmcb);
}

static void svm_set_cr3(struct kvm_vcpu *vcpu, unsigned long root)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->save.cr3 = root;
	mark_dirty(svm->vmcb, VMCB_CR);
	svm_flush_tlb(vcpu);
}

static void set_tdp_cr3(struct kvm_vcpu *vcpu, unsigned long root)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	svm->vmcb->control.nested_cr3 = root;
	mark_dirty(svm->vmcb, VMCB_NPT);

	/* Also sync guest cr3 here in case we live migrate */
	svm->vmcb->save.cr3 = kvm_read_cr3(vcpu);
	mark_dirty(svm->vmcb, VMCB_CR);

	svm_flush_tlb(vcpu);
}

static int is_disabled(void)
{
	u64 vm_cr;

	rdmsrl(MSR_VM_CR, vm_cr);
	if (vm_cr & (1 << SVM_VM_CR_SVM_DISABLE))
		return 1;

	return 0;
}

static void
svm_patch_hypercall(struct kvm_vcpu *vcpu, unsigned char *hypercall)
{
	/*
	 * Patch in the VMMCALL instruction:
	 */
	hypercall[0] = 0x0f;
	hypercall[1] = 0x01;
	hypercall[2] = 0xd9;
}

static void svm_check_processor_compat(void *rtn)
{
	*(int *)rtn = 0;
}

static bool svm_cpu_has_accelerated_tpr(void)
{
	return false;
}

static bool svm_has_high_real_mode_segbase(void)
{
	return true;
}

static u64 svm_get_mt_mask(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio)
{
	return 0;
}

static void svm_cpuid_update(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	struct kvm_cpuid_entry2 *entry;

	/* Update nrips enabled cache */
	svm->nrips_enabled = !!guest_cpuid_has_nrips(&svm->vcpu);

	if (!kvm_vcpu_apicv_active(vcpu))
		return;

	entry = kvm_find_cpuid_entry(vcpu, 1, 0);
	if (entry)
		entry->ecx &= ~bit(X86_FEATURE_X2APIC);
}

static void svm_set_supported_cpuid(u32 func, struct kvm_cpuid_entry2 *entry)
{
	switch (func) {
	case 0x1:
		if (avic)
			entry->ecx &= ~bit(X86_FEATURE_X2APIC);
		break;
	case 0x80000001:
		if (nested)
			entry->ecx |= (1 << 2); /* Set SVM bit */
		break;
	case 0x8000000A:
		entry->eax = 1; /* SVM revision 1 */
		entry->ebx = 8; /* Lets support 8 ASIDs in case we add proper
				   ASID emulation to nested SVM */
		entry->ecx = 0; /* Reserved */
		entry->edx = 0; /* Per default do not support any
				   additional features */

		/* Support next_rip if host supports it */
		if (boot_cpu_has(X86_FEATURE_NRIPS))
			entry->edx |= SVM_FEATURE_NRIP;

		/* Support NPT for the guest if enabled */
		if (npt_enabled)
			entry->edx |= SVM_FEATURE_NPT;

		break;
	}
}

static int svm_get_lpage_level(void)
{
	return PT_PDPE_LEVEL;
}

static bool svm_rdtscp_supported(void)
{
	return boot_cpu_has(X86_FEATURE_RDTSCP);
}

static bool svm_invpcid_supported(void)
{
	return false;
}

static bool svm_mpx_supported(void)
{
	return false;
}

static bool svm_xsaves_supported(void)
{
	return false;
}

static bool svm_has_wbinvd_exit(void)
{
	return true;
}

static void svm_fpu_deactivate(struct kvm_vcpu *vcpu)
{
	struct vcpu_svm *svm = to_svm(vcpu);

	set_exception_intercept(svm, NM_VECTOR);
	update_cr0_intercept(svm);
}

#define PRE_EX(exit)  { .exit_code = (exit), \
			.stage = X86_ICPT_PRE_EXCEPT, }
#define POST_EX(exit) { .exit_code = (exit), \
			.stage = X86_ICPT_POST_EXCEPT, }
#define POST_MEM(exit) { .exit_code = (exit), \
			.stage = X86_ICPT_POST_MEMACCESS, }

static const struct __x86_intercept {
	u32 exit_code;
	enum x86_intercept_stage stage;
} x86_intercept_map[] = {
	[x86_intercept_cr_read]		= POST_EX(SVM_EXIT_READ_CR0),
	[x86_intercept_cr_write]	= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_clts]		= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_lmsw]		= POST_EX(SVM_EXIT_WRITE_CR0),
	[x86_intercept_smsw]		= POST_EX(SVM_EXIT_READ_CR0),
	[x86_intercept_dr_read]		= POST_EX(SVM_EXIT_READ_DR0),
	[x86_intercept_dr_write]	= POST_EX(SVM_EXIT_WRITE_DR0),
	[x86_intercept_sldt]		= POST_EX(SVM_EXIT_LDTR_READ),
	[x86_intercept_str]		= POST_EX(SVM_EXIT_TR_READ),
	[x86_intercept_lldt]		= POST_EX(SVM_EXIT_LDTR_WRITE),
	[x86_intercept_ltr]		= POST_EX(SVM_EXIT_TR_WRITE),
	[x86_intercept_sgdt]		= POST_EX(SVM_EXIT_GDTR_READ),
	[x86_intercept_sidt]		= POST_EX(SVM_EXIT_IDTR_READ),
	[x86_intercept_lgdt]		= POST_EX(SVM_EXIT_GDTR_WRITE),
	[x86_intercept_lidt]		= POST_EX(SVM_EXIT_IDTR_WRITE),
	[x86_intercept_vmrun]		= POST_EX(SVM_EXIT_VMRUN),
	[x86_intercept_vmmcall]		= POST_EX(SVM_EXIT_VMMCALL),
	[x86_intercept_vmload]		= POST_EX(SVM_EXIT_VMLOAD),
	[x86_intercept_vmsave]		= POST_EX(SVM_EXIT_VMSAVE),
	[x86_intercept_stgi]		= POST_EX(SVM_EXIT_STGI),
	[x86_intercept_clgi]		= POST_EX(SVM_EXIT_CLGI),
	[x86_intercept_skinit]		= POST_EX(SVM_EXIT_SKINIT),
	[x86_intercept_invlpga]		= POST_EX(SVM_EXIT_INVLPGA),
	[x86_intercept_rdtscp]		= POST_EX(SVM_EXIT_RDTSCP),
	[x86_intercept_monitor]		= POST_MEM(SVM_EXIT_MONITOR),
	[x86_intercept_mwait]		= POST_EX(SVM_EXIT_MWAIT),
	[x86_intercept_invlpg]		= POST_EX(SVM_EXIT_INVLPG),
	[x86_intercept_invd]		= POST_EX(SVM_EXIT_INVD),
	[x86_intercept_wbinvd]		= POST_EX(SVM_EXIT_WBINVD),
	[x86_intercept_wrmsr]		= POST_EX(SVM_EXIT_MSR),
	[x86_intercept_rdtsc]		= POST_EX(SVM_EXIT_RDTSC),
	[x86_intercept_rdmsr]		= POST_EX(SVM_EXIT_MSR),
	[x86_intercept_rdpmc]		= POST_EX(SVM_EXIT_RDPMC),
	[x86_intercept_cpuid]		= PRE_EX(SVM_EXIT_CPUID),
	[x86_intercept_rsm]		= PRE_EX(SVM_EXIT_RSM),
	[x86_intercept_pause]		= PRE_EX(SVM_EXIT_PAUSE),
	[x86_intercept_pushf]		= PRE_EX(SVM_EXIT_PUSHF),
	[x86_intercept_popf]		= PRE_EX(SVM_EXIT_POPF),
	[x86_intercept_intn]		= PRE_EX(SVM_EXIT_SWINT),
	[x86_intercept_iret]		= PRE_EX(SVM_EXIT_IRET),
	[x86_intercept_icebp]		= PRE_EX(SVM_EXIT_ICEBP),
	[x86_intercept_hlt]		= POST_EX(SVM_EXIT_HLT),
	[x86_intercept_in]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_ins]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_out]		= POST_EX(SVM_EXIT_IOIO),
	[x86_intercept_outs]		= POST_EX(SVM_EXIT_IOIO),
};

#undef PRE_EX
#undef POST_EX
#undef POST_MEM

static int svm_check_intercept(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage)
{
	struct vcpu_svm *svm = to_svm(vcpu);
	int vmexit, ret = X86EMUL_CONTINUE;
	struct __x86_intercept icpt_info;
	struct vmcb *vmcb = svm->vmcb;

	if (info->intercept >= ARRAY_SIZE(x86_intercept_map))
		goto out;

	icpt_info = x86_intercept_map[info->intercept];

	if (stage != icpt_info.stage)
		goto out;

	switch (icpt_info.exit_code) {
	case SVM_EXIT_READ_CR0:
		if (info->intercept == x86_intercept_cr_read)
			icpt_info.exit_code += info->modrm_reg;
		break;
	case SVM_EXIT_WRITE_CR0: {
		unsigned long cr0, val;
		u64 intercept;

		if (info->intercept == x86_intercept_cr_write)
			icpt_info.exit_code += info->modrm_reg;

		if (icpt_info.exit_code != SVM_EXIT_WRITE_CR0 ||
		    info->intercept == x86_intercept_clts)
			break;

		intercept = svm->nested.intercept;

		if (!(intercept & (1ULL << INTERCEPT_SELECTIVE_CR0)))
			break;

		cr0 = vcpu->arch.cr0 & ~SVM_CR0_SELECTIVE_MASK;
		val = info->src_val  & ~SVM_CR0_SELECTIVE_MASK;

		if (info->intercept == x86_intercept_lmsw) {
			cr0 &= 0xfUL;
			val &= 0xfUL;
			/* lmsw can't clear PE - catch this here */
			if (cr0 & X86_CR0_PE)
				val |= X86_CR0_PE;
		}

		if (cr0 ^ val)
			icpt_info.exit_code = SVM_EXIT_CR0_SEL_WRITE;

		break;
	}
	case SVM_EXIT_READ_DR0:
	case SVM_EXIT_WRITE_DR0:
		icpt_info.exit_code += info->modrm_reg;
		break;
	case SVM_EXIT_MSR:
		if (info->intercept == x86_intercept_wrmsr)
			vmcb->control.exit_info_1 = 1;
		else
			vmcb->control.exit_info_1 = 0;
		break;
	case SVM_EXIT_PAUSE:
		/*
		 * We get this for NOP only, but pause
		 * is rep not, check this here
		 */
		if (info->rep_prefix != REPE_PREFIX)
			goto out;
	case SVM_EXIT_IOIO: {
		u64 exit_info;
		u32 bytes;

		if (info->intercept == x86_intercept_in ||
		    info->intercept == x86_intercept_ins) {
			exit_info = ((info->src_val & 0xffff) << 16) |
				SVM_IOIO_TYPE_MASK;
			bytes = info->dst_bytes;
		} else {
			exit_info = (info->dst_val & 0xffff) << 16;
			bytes = info->src_bytes;
		}

		if (info->intercept == x86_intercept_outs ||
		    info->intercept == x86_intercept_ins)
			exit_info |= SVM_IOIO_STR_MASK;

		if (info->rep_prefix)
			exit_info |= SVM_IOIO_REP_MASK;

		bytes = min(bytes, 4u);

		exit_info |= bytes << SVM_IOIO_SIZE_SHIFT;

		exit_info |= (u32)info->ad_bytes << (SVM_IOIO_ASIZE_SHIFT - 1);

		vmcb->control.exit_info_1 = exit_info;
		vmcb->control.exit_info_2 = info->next_rip;

		break;
	}
	default:
		break;
	}

	/* TODO: Advertise NRIPS to guest hypervisor unconditionally */
	if (static_cpu_has(X86_FEATURE_NRIPS))
		vmcb->control.next_rip  = info->next_rip;
	vmcb->control.exit_code = icpt_info.exit_code;
	vmexit = nested_svm_exit_handled(svm);

	ret = (vmexit == NESTED_EXIT_DONE) ? X86EMUL_INTERCEPTED
					   : X86EMUL_CONTINUE;

out:
	return ret;
}

static void svm_handle_external_intr(struct kvm_vcpu *vcpu)
{
	local_irq_enable();
	/*
	 * We must have an instruction with interrupts enabled, so
	 * the timer interrupt isn't delayed by the interrupt shadow.
	 */
	asm("nop");
	local_irq_disable();
}

static void svm_sched_in(struct kvm_vcpu *vcpu, int cpu)
{
}

static inline void avic_post_state_restore(struct kvm_vcpu *vcpu)
{
	if (avic_handle_apic_id_update(vcpu) != 0)
		return;
	if (avic_handle_dfr_update(vcpu) != 0)
		return;
	avic_handle_ldr_update(vcpu);
}

static struct kvm_x86_ops svm_x86_ops __ro_after_init = {
	.cpu_has_kvm_support = has_svm,
	.disabled_by_bios = is_disabled,
	.hardware_setup = svm_hardware_setup,
	.hardware_unsetup = svm_hardware_unsetup,
	.check_processor_compatibility = svm_check_processor_compat,
	.hardware_enable = svm_hardware_enable,
	.hardware_disable = svm_hardware_disable,
	.cpu_has_accelerated_tpr = svm_cpu_has_accelerated_tpr,
	.cpu_has_high_real_mode_segbase = svm_has_high_real_mode_segbase,

	.vcpu_create = svm_create_vcpu,
	.vcpu_free = svm_free_vcpu,
	.vcpu_reset = svm_vcpu_reset,

	.vm_init = avic_vm_init,
	.vm_destroy = avic_vm_destroy,

	.prepare_guest_switch = svm_prepare_guest_switch,
	.vcpu_load = svm_vcpu_load,
	.vcpu_put = svm_vcpu_put,
	.vcpu_blocking = svm_vcpu_blocking,
	.vcpu_unblocking = svm_vcpu_unblocking,

	.update_bp_intercept = update_bp_intercept,
	.get_msr = svm_get_msr,
	.set_msr = svm_set_msr,
	.get_segment_base = svm_get_segment_base,
	.get_segment = svm_get_segment,
	.set_segment = svm_set_segment,
	.get_cpl = svm_get_cpl,
	.get_cs_db_l_bits = kvm_get_cs_db_l_bits,
	.decache_cr0_guest_bits = svm_decache_cr0_guest_bits,
	.decache_cr3 = svm_decache_cr3,
	.decache_cr4_guest_bits = svm_decache_cr4_guest_bits,
	.set_cr0 = svm_set_cr0,
	.set_cr3 = svm_set_cr3,
	.set_cr4 = svm_set_cr4,
	.set_efer = svm_set_efer,
	.get_idt = svm_get_idt,
	.set_idt = svm_set_idt,
	.get_gdt = svm_get_gdt,
	.set_gdt = svm_set_gdt,
	.get_dr6 = svm_get_dr6,
	.set_dr6 = svm_set_dr6,
	.set_dr7 = svm_set_dr7,
	.sync_dirty_debug_regs = svm_sync_dirty_debug_regs,
	.cache_reg = svm_cache_reg,
	.get_rflags = svm_get_rflags,
	.set_rflags = svm_set_rflags,

	.get_pkru = svm_get_pkru,

	.fpu_activate = svm_fpu_activate,
	.fpu_deactivate = svm_fpu_deactivate,

	.tlb_flush = svm_flush_tlb,

	.run = svm_vcpu_run,
	.handle_exit = handle_exit,
	.skip_emulated_instruction = skip_emulated_instruction,
	.set_interrupt_shadow = svm_set_interrupt_shadow,
	.get_interrupt_shadow = svm_get_interrupt_shadow,
	.patch_hypercall = svm_patch_hypercall,
	.set_irq = svm_set_irq,
	.set_nmi = svm_inject_nmi,
	.queue_exception = svm_queue_exception,
	.cancel_injection = svm_cancel_injection,
	.interrupt_allowed = svm_interrupt_allowed,
	.nmi_allowed = svm_nmi_allowed,
	.get_nmi_mask = svm_get_nmi_mask,
	.set_nmi_mask = svm_set_nmi_mask,
	.enable_nmi_window = enable_nmi_window,
	.enable_irq_window = enable_irq_window,
	.update_cr8_intercept = update_cr8_intercept,
	.set_virtual_x2apic_mode = svm_set_virtual_x2apic_mode,
	.get_enable_apicv = svm_get_enable_apicv,
	.refresh_apicv_exec_ctrl = svm_refresh_apicv_exec_ctrl,
	.load_eoi_exitmap = svm_load_eoi_exitmap,
	.sync_pir_to_irr = svm_sync_pir_to_irr,
	.hwapic_irr_update = svm_hwapic_irr_update,
	.hwapic_isr_update = svm_hwapic_isr_update,
	.apicv_post_state_restore = avic_post_state_restore,

	.set_tss_addr = svm_set_tss_addr,
	.get_tdp_level = get_npt_level,
	.get_mt_mask = svm_get_mt_mask,

	.get_exit_info = svm_get_exit_info,

	.get_lpage_level = svm_get_lpage_level,

	.cpuid_update = svm_cpuid_update,

	.rdtscp_supported = svm_rdtscp_supported,
	.invpcid_supported = svm_invpcid_supported,
	.mpx_supported = svm_mpx_supported,
	.xsaves_supported = svm_xsaves_supported,

	.set_supported_cpuid = svm_set_supported_cpuid,

	.has_wbinvd_exit = svm_has_wbinvd_exit,

	.write_tsc_offset = svm_write_tsc_offset,

	.set_tdp_cr3 = set_tdp_cr3,

	.check_intercept = svm_check_intercept,
	.handle_external_intr = svm_handle_external_intr,

	.sched_in = svm_sched_in,

	.pmu_ops = &amd_pmu_ops,
	.deliver_posted_interrupt = svm_deliver_avic_intr,
	.update_pi_irte = svm_update_pi_irte,
};

static int __init svm_init(void)
{
	return kvm_init(&svm_x86_ops, sizeof(struct vcpu_svm),
			__alignof__(struct vcpu_svm), THIS_MODULE);
}

static void __exit svm_exit(void)
{
	kvm_exit();
}

module_init(svm_init)
module_exit(svm_exit)
