/*
 * bpf_jit32.h: BPF JIT compiler for PPC
 *
 * Copyright 2011 Matt Evans <matt@ozlabs.org>, IBM Corporation
 *
 * Split from bpf_jit.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#ifndef _BPF_JIT32_H
#define _BPF_JIT32_H

#include "bpf_jit.h"

#ifdef CONFIG_PPC64
#define BPF_PPC_STACK_R3_OFF	48
#define BPF_PPC_STACK_LOCALS	32
#define BPF_PPC_STACK_BASIC	(48+64)
#define BPF_PPC_STACK_SAVE	(18*8)
#define BPF_PPC_STACKFRAME	(BPF_PPC_STACK_BASIC+BPF_PPC_STACK_LOCALS+ \
				 BPF_PPC_STACK_SAVE)
#define BPF_PPC_SLOWPATH_FRAME	(48+64)
#else
#define BPF_PPC_STACK_R3_OFF	24
#define BPF_PPC_STACK_LOCALS	16
#define BPF_PPC_STACK_BASIC	(24+32)
#define BPF_PPC_STACK_SAVE	(18*4)
#define BPF_PPC_STACKFRAME	(BPF_PPC_STACK_BASIC+BPF_PPC_STACK_LOCALS+ \
				 BPF_PPC_STACK_SAVE)
#define BPF_PPC_SLOWPATH_FRAME	(24+32)
#endif

#define REG_SZ         (BITS_PER_LONG/8)

/*
 * Generated code register usage:
 *
 * As normal PPC C ABI (e.g. r1=sp, r2=TOC), with:
 *
 * skb		r3	(Entry parameter)
 * A register	r4
 * X register	r5
 * addr param	r6
 * r7-r10	scratch
 * skb->data	r14
 * skb headlen	r15	(skb->len - skb->data_len)
 * m[0]		r16
 * m[...]	...
 * m[15]	r31
 */
#define r_skb		3
#define r_ret		3
#define r_A		4
#define r_X		5
#define r_addr		6
#define r_scratch1	7
#define r_scratch2	8
#define r_D		14
#define r_HL		15
#define r_M		16

#ifndef __ASSEMBLY__

/*
 * Assembly helpers from arch/powerpc/net/bpf_jit.S:
 */
#define DECLARE_LOAD_FUNC(func)	\
	extern u8 func[], func##_negative_offset[], func##_positive_offset[]

DECLARE_LOAD_FUNC(sk_load_word);
DECLARE_LOAD_FUNC(sk_load_half);
DECLARE_LOAD_FUNC(sk_load_byte);
DECLARE_LOAD_FUNC(sk_load_byte_msh);

#define PPC_LBZ_OFFS(r, base, i) do { if ((i) < 32768) PPC_LBZ(r, base, i);   \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LBZ(r, r, IMM_L(i)); } } while(0)

#define PPC_LD_OFFS(r, base, i) do { if ((i) < 32768) PPC_LD(r, base, i);     \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LD(r, r, IMM_L(i)); } } while(0)

#define PPC_LWZ_OFFS(r, base, i) do { if ((i) < 32768) PPC_LWZ(r, base, i);   \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LWZ(r, r, IMM_L(i)); } } while(0)

#define PPC_LHZ_OFFS(r, base, i) do { if ((i) < 32768) PPC_LHZ(r, base, i);   \
		else {	PPC_ADDIS(r, base, IMM_HA(i));			      \
			PPC_LHZ(r, r, IMM_L(i)); } } while(0)

#ifdef CONFIG_PPC64
#define PPC_LL_OFFS(r, base, i) do { PPC_LD_OFFS(r, base, i); } while(0)
#else
#define PPC_LL_OFFS(r, base, i) do { PPC_LWZ_OFFS(r, base, i); } while(0)
#endif

#ifdef CONFIG_SMP
#ifdef CONFIG_PPC64
#define PPC_BPF_LOAD_CPU(r)		\
	do { BUILD_BUG_ON(FIELD_SIZEOF(struct paca_struct, paca_index) != 2);	\
		PPC_LHZ_OFFS(r, 13, offsetof(struct paca_struct, paca_index));	\
	} while (0)
#else
#define PPC_BPF_LOAD_CPU(r)     \
	do { BUILD_BUG_ON(FIELD_SIZEOF(struct thread_info, cpu) != 4);		\
		PPC_LHZ_OFFS(r, (1 & ~(THREAD_SIZE - 1)),			\
				offsetof(struct thread_info, cpu));		\
	} while(0)
#endif
#else
#define PPC_BPF_LOAD_CPU(r) do { PPC_LI(r, 0); } while(0)
#endif

#define PPC_LHBRX_OFFS(r, base, i) \
		do { PPC_LI32(r, i); PPC_LHBRX(r, r, base); } while(0)
#ifdef __LITTLE_ENDIAN__
#define PPC_NTOHS_OFFS(r, base, i)	PPC_LHBRX_OFFS(r, base, i)
#else
#define PPC_NTOHS_OFFS(r, base, i)	PPC_LHZ_OFFS(r, base, i)
#endif

#define SEEN_DATAREF 0x10000 /* might call external helpers */
#define SEEN_XREG    0x20000 /* X reg is used */
#define SEEN_MEM     0x40000 /* SEEN_MEM+(1<<n) = use mem[n] for temporary
			      * storage */
#define SEEN_MEM_MSK 0x0ffff

struct codegen_context {
	unsigned int seen;
	unsigned int idx;
	int pc_ret0; /* bpf index of first RET #0 instruction (if any) */
};

#endif

#endif
