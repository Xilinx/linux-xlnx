#ifndef _ASM_X86_MMU_H
#define _ASM_X86_MMU_H

#include <linux/spinlock.h>
#include <linux/mutex.h>

/*
 * The x86 doesn't have a mmu context, but
 * we put the segment information here.
 */
typedef struct {
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	struct ldt_struct *ldt;
#endif

#ifdef CONFIG_X86_64
	/* True if mm supports a task running in 32 bit compatibility mode. */
	unsigned short ia32_compat;
#endif

	struct mutex lock;
	void __user *vdso;			/* vdso base address */
	const struct vdso_image *vdso_image;	/* vdso image in use */

	atomic_t perf_rdpmc_allowed;	/* nonzero if rdpmc is allowed */
#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
	/*
	 * One bit per protection key says whether userspace can
	 * use it or not.  protected by mmap_sem.
	 */
	u16 pkey_allocation_map;
	s16 execute_only_pkey;
#endif
} mm_context_t;

#ifdef CONFIG_SMP
void leave_mm(int cpu);
#else
static inline void leave_mm(int cpu)
{
}
#endif

#endif /* _ASM_X86_MMU_H */
