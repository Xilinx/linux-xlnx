/*
 * arch/microblaze/kernel/registers.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_REGISTERS_H
#define _ASM_REGISTERS_H

#define MSR_BE  (1<<0)
#define MSR_IE  (1<<1)
#define MSR_C   (1<<2)
#define MSR_BIP (1<<3)
#define MSR_FSL (1<<4)
#define MSR_ICE (1<<5)
#define MSR_DZ  (1<<6)
#define MSR_DCE (1<<7)
#define MSR_EE  (1<<8)
#define MSR_EIP (1<<9)
#define MSR_CC  (1<<31)

#endif /* _ASM_REGISTERS_H */
