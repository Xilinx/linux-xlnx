/*
 *  include/linux/userfaultfd_k.h
 *
 *  Copyright (C) 2015  Red Hat, Inc.
 *
 */

#ifndef _LINUX_USERFAULTFD_K_H
#define _LINUX_USERFAULTFD_K_H

#ifdef CONFIG_USERFAULTFD

#include <linux/userfaultfd.h> /* linux/include/uapi/linux/userfaultfd.h */

#include <linux/fcntl.h>

/*
 * CAREFUL: Check include/uapi/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from userfaultfd, in order to leave a free define-space for
 * shared O_* flags.
 */
#define UFFD_CLOEXEC O_CLOEXEC
#define UFFD_NONBLOCK O_NONBLOCK

#define UFFD_SHARED_FCNTL_FLAGS (O_CLOEXEC | O_NONBLOCK)
#define UFFD_FLAGS_SET (EFD_SHARED_FCNTL_FLAGS)

extern int handle_userfault(struct fault_env *fe, unsigned long reason);

extern ssize_t mcopy_atomic(struct mm_struct *dst_mm, unsigned long dst_start,
			    unsigned long src_start, unsigned long len);
extern ssize_t mfill_zeropage(struct mm_struct *dst_mm,
			      unsigned long dst_start,
			      unsigned long len);

/* mm helpers */
static inline bool is_mergeable_vm_userfaultfd_ctx(struct vm_area_struct *vma,
					struct vm_userfaultfd_ctx vm_ctx)
{
	return vma->vm_userfaultfd_ctx.ctx == vm_ctx.ctx;
}

static inline bool userfaultfd_missing(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_UFFD_MISSING;
}

static inline bool userfaultfd_armed(struct vm_area_struct *vma)
{
	return vma->vm_flags & (VM_UFFD_MISSING | VM_UFFD_WP);
}

#else /* CONFIG_USERFAULTFD */

/* mm helpers */
static inline int handle_userfault(struct fault_env *fe, unsigned long reason)
{
	return VM_FAULT_SIGBUS;
}

static inline bool is_mergeable_vm_userfaultfd_ctx(struct vm_area_struct *vma,
					struct vm_userfaultfd_ctx vm_ctx)
{
	return true;
}

static inline bool userfaultfd_missing(struct vm_area_struct *vma)
{
	return false;
}

static inline bool userfaultfd_armed(struct vm_area_struct *vma)
{
	return false;
}

#endif /* CONFIG_USERFAULTFD */

#endif /* _LINUX_USERFAULTFD_K_H */
