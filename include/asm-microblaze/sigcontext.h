/*
 * include/asm-microblaze/sigcontext.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_SIGCONTEXT_H
#define _ASM_SIGCONTEXT_H

#include <asm/ptrace.h>

struct sigcontext {
	struct pt_regs regs;
	unsigned long oldmask;
};

#endif /* _ASM_SIGCONTEXT_H */
