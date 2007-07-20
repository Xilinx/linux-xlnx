/*
 * include/asm-microblaze/current.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_CURRENT_H
#define _ASM_CURRENT_H

#ifndef __ASSEMBLY__

/*
 * Dedicate r31 to keeping the current task pointer
 */
register struct task_struct *current asm("r31");

#define get_current() current

#endif

#endif /* _ASM_CURRENT_H */
