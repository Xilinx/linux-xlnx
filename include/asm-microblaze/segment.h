/*
 * include/asm-microblaze/segment.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 */

#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long seg;
} mm_segment_t;

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 *
 * For non-MMU arch like Microblaze, KERNEL_DS and USER_DS is equal.
 */
#define KERNEL_DS		((mm_segment_t){0})
#define USER_DS			KERNEL_DS

#define get_ds()		(KERNEL_DS)
#define get_fs()		(current_thread_info()->addr_limit)
#define set_fs(x)		do { current_thread_info()->addr_limit = (x); } while(0)

#define segment_eq(a,b)		((a).seg == (b).seg)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_SEGMENT_H */
