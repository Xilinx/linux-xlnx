#ifndef _ASM_POWERPC_ASM_PROTOTYPES_H
#define _ASM_POWERPC_ASM_PROTOTYPES_H
/*
 * This file is for prototypes of C functions that are only called
 * from asm, and any associated variables.
 *
 * Copyright 2016, Daniel Axtens, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <linux/kprobes.h>
#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <asm/uaccess.h>
#include <asm/epapr_hcalls.h>

#include <uapi/asm/ucontext.h>

/* SMP */
extern struct thread_info *current_set[NR_CPUS];
extern struct thread_info *secondary_ti;
void start_secondary(void *unused);

/* kexec */
struct paca_struct;
struct kimage;
extern struct paca_struct kexec_paca;
void kexec_copy_flush(struct kimage *image);

/* pseries hcall tracing */
extern struct static_key hcall_tracepoint_key;
void __trace_hcall_entry(unsigned long opcode, unsigned long *args);
void __trace_hcall_exit(long opcode, unsigned long retval,
			unsigned long *retbuf);
/* OPAL tracing */
#ifdef HAVE_JUMP_LABEL
extern struct static_key opal_tracepoint_key;
#endif

void __trace_opal_entry(unsigned long opcode, unsigned long *args);
void __trace_opal_exit(long opcode, unsigned long retval);

/* VMX copying */
int enter_vmx_usercopy(void);
int exit_vmx_usercopy(void);
int enter_vmx_copy(void);
void * exit_vmx_copy(void *dest);

/* Traps */
long machine_check_early(struct pt_regs *regs);
long hmi_exception_realmode(struct pt_regs *regs);
void SMIException(struct pt_regs *regs);
void handle_hmi_exception(struct pt_regs *regs);
void instruction_breakpoint_exception(struct pt_regs *regs);
void RunModeException(struct pt_regs *regs);
void single_step_exception(struct pt_regs *regs);
void program_check_exception(struct pt_regs *regs);
void alignment_exception(struct pt_regs *regs);
void StackOverflow(struct pt_regs *regs);
void nonrecoverable_exception(struct pt_regs *regs);
void kernel_fp_unavailable_exception(struct pt_regs *regs);
void altivec_unavailable_exception(struct pt_regs *regs);
void vsx_unavailable_exception(struct pt_regs *regs);
void fp_unavailable_tm(struct pt_regs *regs);
void altivec_unavailable_tm(struct pt_regs *regs);
void vsx_unavailable_tm(struct pt_regs *regs);
void facility_unavailable_exception(struct pt_regs *regs);
void TAUException(struct pt_regs *regs);
void altivec_assist_exception(struct pt_regs *regs);
void unrecoverable_exception(struct pt_regs *regs);
void kernel_bad_stack(struct pt_regs *regs);
void system_reset_exception(struct pt_regs *regs);
void machine_check_exception(struct pt_regs *regs);
void emulation_assist_interrupt(struct pt_regs *regs);

/* signals, syscalls and interrupts */
#ifdef CONFIG_PPC64
int sys_swapcontext(struct ucontext __user *old_ctx,
		    struct ucontext __user *new_ctx,
		    long ctx_size, long r6, long r7, long r8, struct pt_regs *regs);
#else
long sys_swapcontext(struct ucontext __user *old_ctx,
		    struct ucontext __user *new_ctx,
		    int ctx_size, int r6, int r7, int r8, struct pt_regs *regs);
#endif
long sys_switch_endian(void);
notrace unsigned int __check_irq_replay(void);
void notrace restore_interrupts(void);

/* ptrace */
long do_syscall_trace_enter(struct pt_regs *regs);
void do_syscall_trace_leave(struct pt_regs *regs);

/* process */
void restore_math(struct pt_regs *regs);
void restore_tm_state(struct pt_regs *regs);

/* prom_init (OpenFirmware) */
unsigned long __init prom_init(unsigned long r3, unsigned long r4,
			       unsigned long pp,
			       unsigned long r6, unsigned long r7,
			       unsigned long kbase);

/* setup */
void __init early_setup(unsigned long dt_ptr);
void early_setup_secondary(void);

/* time */
void accumulate_stolen_time(void);

/* misc runtime */
extern u64 __bswapdi2(u64);
extern s64 __lshrdi3(s64, int);
extern s64 __ashldi3(s64, int);
extern s64 __ashrdi3(s64, int);
extern int __cmpdi2(s64, s64);
extern int __ucmpdi2(u64, u64);

#endif /* _ASM_POWERPC_ASM_PROTOTYPES_H */
