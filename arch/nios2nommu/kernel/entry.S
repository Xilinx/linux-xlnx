/*
 *  linux/arch/nios2nommu/kernel/entry.S
 *
 *  Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 2000  Lineo Inc. (www.lineo.com)
 *  Copyright (C) 2004  Microtronix Datacom Ltd.
 *
 * Based on:
 *
 *  linux/arch/m68knommu/kernel/entry.S
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 *
 * Linux/m68k support by Hamish Macdonald
 *
 * 68060 fixes by Jesper Skov
 * ColdFire support by Greg Ungerer (gerg@snapgear.com)
 * 5307 fixes by David W. Miller
 * linux 2.4 support David McCullough <davidm@snapgear.com>
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/asm-macros.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/entry.h>
#include <asm/unistd.h>
#include <asm/traps.h>
#include <asm/processor.h>

.text
.set noat
.set nobreak

ENTRY(system_call)
/*	SAVE_ALL */
	rdctl	r10,status		/* enable intrs again */
	ori	r10,r10,0x0001
	wrctl	status,r10

	movi	r2,-LENOSYS
	stw	r2,PT_R2(sp)		/* default return value in r2 */
					/* original r2 is in orig_r2 */

	movui	r1,NR_syscalls
	bgtu	r3,r1,ret_from_exception
	slli	r1,r3,2
	movhi	r11,%hiadj(sys_call_table)
	add	r1,r1,r11
	ldw	r1,%lo(sys_call_table)(r1)
	beq	r1,r0,ret_from_exception

	movi	r11,%lo(0xffffe000)	/* Get thread info pointer */
	and	r11,sp,r11
	ldw	r11,TI_FLAGS(r11)
	BTBNZ	r11,r11,TIF_SYSCALL_TRACE_ASM,1f

	callr	r1
	stw	r2,PT_R2(sp)		/* save the return value */
	br	ret_from_exception
1:
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK
	/* wentao: restore r4-9, since they are trashed by syscall_trace */
	ldw	r4,PT_R4(sp)
	ldw	r5,PT_R5(sp)
	ldw	r6,PT_R6(sp)
	ldw	r7,PT_R7(sp)
	ldw	r8,PT_R8(sp)
	ldw	r9,PT_R9(sp)
	callr	r1
	stw	r2,PT_R2(sp)		/* save the return value */
	SAVE_SWITCH_STACK
	call	syscall_trace
	RESTORE_SWITCH_STACK

ret_from_exception:
	ldw	r1,PT_STATUS_EXTENSION(sp)	/* check if returning to kernel */
	TSTBZ	r1,r1,PS_S_ASM,Luser_return	/* if so, skip resched, signals */

restore_all:
	rdctl	r10,status			/* disable intrs */
	andi	r10,r10,0xfffe
	wrctl	status, r10
	RESTORE_ALL
	eret

Luser_return:
	GET_THREAD_INFO	r24			/* get thread_info pointer */
	ldw	r10,TI_FLAGS(r24)		/* get thread_info->flags */
	ANDI32	r11,r10,_TIF_WORK_MASK_ASM
	beq	r11,r0,restore_all		/* Nothing to do */
	BTBZ	r1,r10,TIF_NEED_RESCHED_ASM,Lsignal_return

Lwork_resched:
	call	schedule
	br	ret_from_exception

Lsignal_return:
	BTBZ	r1,r10,TIF_SIGPENDING_ASM,restore_all
	mov	r5,sp			/* pt_regs */
	SAVE_SWITCH_STACK
	CLR	r4			/* oldset = 0 */
	call	do_signal
	RESTORE_SWITCH_STACK
	br	restore_all

/*
 * Handle software exceptions. Put here so external interrupts
 * can fall throught to ret_from_interrupt.
 */

software_exception:
	ldw	r24,-4(ea)		// instruction that caused the exception
	xorhi	r24,r24,0x003b		// upper half of trap opcode
	xori	r24,r24,0x683a		// lower half of trap opcode
	bne	r24,r0,instruction_trap	/* N - check for instruction trap */
	cmpeqi	r11,r2,TRAP_ID_SYSCALL	/* ? Is this a syscall */
	bne	r11,r0,system_call	/* Y - handle syscall */
	cmpeqi	r11,r2,TRAP_ID_APPDEBUG	/* ? Is this an application debug */
	bne	r11,r0,app_debug	/* Y - handle app_debug */
	cmpeqi	r11,r2,63		/* ? Is this the old syscall number */
	bne	r11,r0,system_call	/* Y - handle syscall to catch older apps*/
	br	restore_all		/* N - everything else is ignored for now */

app_debug:
	GET_THREAD_INFO	r24			/* get thread_info */
	ldw	r1,TI_TASK(r24)		/* get thread_info->task */
	ldw r24,(TASK_THREAD + THREAD_FLAGS)(r1)	/* get thread_info->task->thread.flags */
	ORI32 r24, r24, NIOS2_FLAG_DEBUG  /* set the debug flag */
	stw	r24,(TASK_THREAD + THREAD_FLAGS)(r1)	/* save thread_info->task->thread.flags */
	br	restore_all

/*
 * This is the generic interrupt handler (for all hardware interrupt
 * sources). It figures out the vector number and calls the appropriate
 * interrupt service routine directly.
 */
ENTRY(inthandler)
	SAVE_ALL
	/*
	 * Test to see if the exception was a software exception or caused by an
	 * external interrupt, and vector accordingly.
	 */

	rdctl	r24,estatus
	andi	r24,r24,1
	beq	r24,r0,software_exception
	rdctl	r12,ipending
	beq	r12,r0,software_exception

	movi	r24,-1
	stw	r24,PT_ORIG_R2(sp)
	
	/* 
	 * Process an external hardware interrupt. 
	 */

	addi	ea,ea,-4		/* re-issue the interrupted instruction */
	stw	ea,PT_EA(sp)
	rdctl	r9,ienable		/* Isolate possible interrupts */
	and	r12,r12,r9
	beq	r12,r0,ret_from_interrupt /* No one to service done */
	movi	r4,%lo(-1)		/* Start from bit position 0, highest priority */
					/* This is the IRQ # for handler call */
1:	addi	r4,r4,1
	srl	r10,r12,r4
	andi	r10,r10,1		/* Isolate bit we are interested in */
	cmpeqi	r11,r4,32		  /* ? End of the register */
	bne	r11,r0,ret_from_interrupt /* Y - out of here */
	beq	r10,r0,1b
	mov	r5,sp			/* Setup pt_regs pointer for handler call */
	PUSH	r4			/* Save state for return */
	PUSH	r12
	call	process_int
	POP	r12
	POP	r4
	br	1b			/* Check for other interrupts while here */

ENTRY(ret_from_interrupt)
	ldw	r4,PT_STATUS_EXTENSION(sp)
	TSTBZ	r4,r4,PS_S_ASM,Luser_return	// Returning to user

#ifdef CONFIG_PREEMPT
	GET_THREAD_INFO	r1
	ldw	r4,TI_PREEMPT_COUNT(r1)
	bne	r4,r0,restore_all

need_resched:
	ldw	r4,TI_FLAGS(r1)		// ? Need resched set
	BTBZ	r10,r4,TIF_NEED_RESCHED_ASM,restore_all
	ldw	r4,PT_ESTATUS(sp)	// ? Interrupts off
	BTBZ	r10,r4,NIOS2_STATUS_PIE_OFST_ASM,restore_all
	movia	r4,PREEMPT_ACTIVE_ASM
	stw	r4,TI_PREEMPT_COUNT(r1)
	rdctl	r10,status		/* enable intrs again */
	ori	r10,r10,0x0001
	wrctl	status,r10
	PUSH	r1
	call	schedule
	POP	r1
	mov	r4,r0
	stw	r4,TI_PREEMPT_COUNT(r1)
	rdctl	r10,status		/* disable intrs */
	andi	r10,r10,0xfffe
	wrctl	status, r10
	br	need_resched
#else
	br	restore_all
#endif


/*
 * Beware - when entering resume, prev (the current task) is
 * in r4, next (the new task) is in r5, don't change these
 * registers.
 */
ENTRY(resume)

	rdctl	r7,status			/* save thread status reg */
	stw	r7,TASK_THREAD+THREAD_KPSR(r4)	

	andi	r7,r7,0x0fffe			/* disable interrupts */
	wrctl	status,r7

	movia	r8,status_extension		/* save status extension */
	ldw	r7,0(r8)
	stw	r7,TASK_THREAD+THREAD_KESR(r4)

	SAVE_SWITCH_STACK
	stw	sp,TASK_THREAD+THREAD_KSP(r4)	/* save kernel stack pointer */
	ldw	sp,TASK_THREAD+THREAD_KSP(r5)	/* restore new thread stack */
	movia	r24,_current_thread		/* save thread */
	GET_THREAD_INFO r1
	stw	r1,0(r24)
	RESTORE_SWITCH_STACK
	
	ldw	r7,TASK_THREAD+THREAD_KESR(r5)	/* restore extended status reg */
	stw	r7,0(r8)

	ldw	r7,TASK_THREAD+THREAD_KPSR(r5)	/* restore thread status reg */
	wrctl	status,r7
	ret

ENTRY(ret_from_fork)
	call	schedule_tail
	br	ret_from_exception

ENTRY(sys_fork)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	nios2_vfork
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_vfork)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	nios2_vfork
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_execve)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	nios2_execve
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_clone)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	nios2_clone
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigsuspend)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	do_sigsuspend
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_rt_sigsuspend)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	do_rt_sigsuspend
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigreturn)
	mov	r4,sp
	SAVE_SWITCH_STACK
	call	do_sigreturn
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_sigaltstack)
	ldw	r4,PT_R4(sp)
	ldw	r5,PT_R5(sp)
	ldw	r6,PT_SP(sp)
	SAVE_SWITCH_STACK
	call	do_sigaltstack
	RESTORE_SWITCH_STACK
	ret

ENTRY(sys_rt_sigreturn)
	SAVE_SWITCH_STACK
	call	do_rt_sigreturn
	RESTORE_SWITCH_STACK
	ret

/******************************************************************************
*                                                                             *
* License Agreement                                                           *
*                                                                             *
* Copyright (c) 2003 Altera Corporation, San Jose, California, USA.           *
* All rights reserved.                                                        *
*                                                                             *
* Permission is hereby granted, free of charge, to any person obtaining a     *
* copy of this software and associated documentation files (the "Software"),  *
* to deal in the Software without restriction, including without limitation   *
* the rights to use, copy, modify, merge, publish, distribute, sublicense,    *
* and/or sell copies of the Software, and to permit persons to whom the       *
* Software is furnished to do so, subject to the following conditions:        *
*                                                                             *
* The above copyright notice and this permission notice shall be included in  *
* all copies or substantial portions of the Software.                         *
*                                                                             *
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR  *
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,    *
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE *
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      *
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING     *
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER         *
* DEALINGS IN THE SOFTWARE.                                                   *
*                                                                             *
* This agreement shall be governed in all respects by the laws of the State   *
* of California and by the laws of the United States of America.              *
*                                                                             *
******************************************************************************/

    /*
     * This is the software exception handler for Nios2.
     */	

     /*
      * Explicitly allow the use of r1 (the assembler temporary register)
      * within this code. This register is normally reserved for the use of
      * the compiler.
      */

ENTRY(instruction_trap)
	RESTORE_ALL	// Clean off our save & setup for emulation
	
    /* INSTRUCTION EMULATION
    *  ---------------------
    *
    * Nios II processors generate exceptions for unimplemented instructions.
    * The routines below emulate these instructions.  Depending on the
    * processor core, the only instructions that might need to be emulated
    * are div, divu, mul, muli, mulxss, mulxsu, and mulxuu.
    *
    * The emulations match the instructions, except for the following
    * limitations:
    *
    * 1) The emulation routines do not emulate the use of the exception
    *    temporary register (et) as a source operand because the exception
    *    handler already has modified it.
    *
    * 2) The routines do not emulate the use of the stack pointer (sp) or the
    *    exception return address register (ea) as a destination because
    *    modifying these registers crashes the exception handler or the
    *    interrupted routine.
    *
    * Detailed Design
    * ---------------
    *
    * The emulation routines expect the contents of integer registers r0-r31
    * to be on the stack at addresses sp, 4(sp), 8(sp), ... 124(sp).  The
    * routines retrieve source operands from the stack and modify the
    * destination register's value on the stack prior to the end of the
    * exception handler.  Then all registers except the destination register
    * are restored to their previous values.
    *
    * The instruction that causes the exception is found at address -4(ea).
    * The instruction's OP and OPX fields identify the operation to be
    * performed.
    *
    * One instruction, muli, is an I-type instruction that is identified by
    * an OP field of 0x24.
    *
    * muli   AAAAA,BBBBB,IIIIIIIIIIIIIIII,-0x24-
    *           27    22                6      0    <-- LSB of field
    *
    * The remaining emulated instructions are R-type and have an OP field
    * of 0x3a.  Their OPX fields identify them.
    *
    * R-type AAAAA,BBBBB,CCCCC,XXXXXX,NNNNN,-0x3a-
    *           27    22    17     11     6      0  <-- LSB of field
    * 
    * 
    * Opcode Encoding.  muli is identified by its OP value.  Then OPX & 0x02
    * is used to differentiate between the division opcodes and the remaining
    * multiplication opcodes.
    *
    * Instruction   OP      OPX    OPX & 0x02
    * -----------   ----    ----   ----------
    * muli          0x24
    * divu          0x3a    0x24         0
    * div           0x3a    0x25         0
    * mul           0x3a    0x27      != 0
    * mulxuu        0x3a    0x07      != 0
    * mulxsu        0x3a    0x17      != 0
    * mulxss        0x3a    0x1f      != 0
    */


    /*
    * Save everything on the stack to make it easy for the emulation routines
    * to retrieve the source register operands.
    */

    addi sp, sp, -128
    stw zero,  0(sp)    // Save zero on stack to avoid special case for r0.
    stw r1,    4(sp)
    stw r2,    8(sp)
    stw r3,   12(sp)
    stw r4,   16(sp)
    stw r5,   20(sp)
    stw r6,   24(sp)
    stw r7,   28(sp)
    stw r8,   32(sp)
    stw r9,   36(sp)
    stw r10,  40(sp)
    stw r11,  44(sp)
    stw r12,  48(sp)
    stw r13,  52(sp)
    stw r14,  56(sp)
    stw r15,  60(sp)
    stw r16,  64(sp)
    stw r17,  68(sp)
    stw r18,  72(sp)
    stw r19,  76(sp)
    stw r20,  80(sp)
    stw r21,  84(sp)
    stw r22,  88(sp)
    stw r23,  92(sp)
                        // Don't bother to save et.  It's already been changed.
    stw bt,  100(sp)
    stw gp,  104(sp)
    stw sp,  108(sp)
    stw fp,  112(sp)
                        // Don't bother to save ea.  It's already been changed.
    stw ba,  120(sp)
    stw ra,  124(sp)


    /*
    * Split the instruction into its fields.  We need 4*A, 4*B, and 4*C as
    * offsets to the stack pointer for access to the stored register values.
    */
    ldw r2,-4(ea)       // r2 = AAAAA,BBBBB,IIIIIIIIIIIIIIII,PPPPPP
    roli r3,r2,7        // r3 = BBB,IIIIIIIIIIIIIIII,PPPPPP,AAAAA,BB
    roli r4,r3,3        // r4 = IIIIIIIIIIIIIIII,PPPPPP,AAAAA,BBBBB
    roli r5,r4,2        // r5 = IIIIIIIIIIIIII,PPPPPP,AAAAA,BBBBB,II
    srai r4,r4,16       // r4 = (sign-extended) IMM16
    roli r6,r5,5        // r6 = XXXX,NNNNN,PPPPPP,AAAAA,BBBBB,CCCCC,XX
    andi r2,r2,0x3f     // r2 = 00000000000000000000000000,PPPPPP
    andi r3,r3,0x7c     // r3 = 0000000000000000000000000,AAAAA,00
    andi r5,r5,0x7c     // r5 = 0000000000000000000000000,BBBBB,00
    andi r6,r6,0x7c     // r6 = 0000000000000000000000000,CCCCC,00

    /* Now
    * r2 = OP
    * r3 = 4*A
    * r4 = IMM16 (sign extended)
    * r5 = 4*B
    * r6 = 4*C
    */


    /*
    * Get the operands.
    *
    * It is necessary to check for muli because it uses an I-type instruction
    * format, while the other instructions are have an R-type format.
    *
    *  Prepare for either multiplication or division loop.
    *  They both loop 32 times.
    */
    movi r14,32

    add  r3,r3,sp       // r3 = address of A-operand.
    ldw  r3,0(r3)       // r3 = A-operand.
    movi r7,0x24        // muli opcode (I-type instruction format)
    beq r2,r7,mul_immed // muli doesn't use the B register as a source

    add  r5,r5,sp       // r5 = address of B-operand.
    ldw  r5,0(r5)       // r5 = B-operand.
                        // r4 = SSSSSSSSSSSSSSSS,-----IMM16------
                        // IMM16 not needed, align OPX portion
                        // r4 = SSSSSSSSSSSSSSSS,CCCCC,-OPX--,00000
    srli r4,r4,5        // r4 = 00000,SSSSSSSSSSSSSSSS,CCCCC,-OPX--
    andi r4,r4,0x3f     // r4 = 00000000000000000000000000,-OPX--

    /* Now
    * r2 = OP
    * r3 = src1
    * r5 = src2
    * r4 = OPX (no longer can be muli)
    * r6 = 4*C
    */



    /*
    *  Multiply or Divide?
    */
    andi r7,r4,0x02    // For R-type multiply instructions, OPX & 0x02 != 0
    bne r7,zero,multiply


    /* DIVISION
    *
    * Divide an unsigned dividend by an unsigned divisor using
    * a shift-and-subtract algorithm.  The example below shows
    * 43 div 7 = 6 for 8-bit integers.  This classic algorithm uses a
    * single register to store both the dividend and the quotient,
    * allowing both values to be shifted with a single instruction.
    *
    *                               remainder dividend:quotient
    *                               --------- -----------------
    *   initialize                   00000000     00101011:
    *   shift                        00000000     0101011:_
    *   remainder >= divisor? no     00000000     0101011:0
    *   shift                        00000000     101011:0_
    *   remainder >= divisor? no     00000000     101011:00
    *   shift                        00000001     01011:00_
    *   remainder >= divisor? no     00000001     01011:000
    *   shift                        00000010     1011:000_
    *   remainder >= divisor? no     00000010     1011:0000
    *   shift                        00000101     011:0000_
    *   remainder >= divisor? no     00000101     011:00000
    *   shift                        00001010     11:00000_
    *   remainder >= divisor? yes    00001010     11:000001
    *       remainder -= divisor   - 00000111
    *                              ----------
    *                                00000011     11:000001
    *   shift                        00000111     1:000001_
    *   remainder >= divisor? yes    00000111     1:0000011
    *       remainder -= divisor   - 00000111
    *                              ----------
    *                                00000000     1:0000011
    *   shift                        00000001     :0000011_
    *   remainder >= divisor? no     00000001     :00000110
    *
    * The quotient is 00000110.
    */

divide:
    /*
    *  Prepare for division by assuming the result
    *  is unsigned, and storing its "sign" as 0.
    */
    movi r17,0


    // Which division opcode?
    xori r7,r4,0x25         // OPX of div
    bne r7,zero,unsigned_division


    /*
    *  OPX is div.  Determine and store the sign of the quotient.
    *  Then take the absolute value of both operands.
    */
    xor r17,r3,r5       // MSB contains sign of quotient
    bge r3,zero,dividend_is_nonnegative
    sub r3,zero,r3      // -r3
dividend_is_nonnegative:
    bge r5,zero,divisor_is_nonnegative
    sub r5,zero,r5      // -r5
divisor_is_nonnegative:


unsigned_division:
    // Initialize the unsigned-division loop.
    movi r13,0          // remainder = 0

    /* Now
    * r3 = dividend : quotient
    * r4 = 0x25 for div, 0x24 for divu
    * r5 = divisor
    * r13 = remainder
    * r14 = loop counter (already initialized to 32)
    * r17 = MSB contains sign of quotient
    */


    /*
    *   for (count = 32; count > 0; --count)
    *   {
    */
divide_loop:

    /*
    *       Division:
    *
    *       (remainder:dividend:quotient) <<= 1;
    */
    slli r13,r13,1
    cmplt r7,r3,zero        // r7 = MSB of r3
    or r13,r13,r7
    slli r3,r3,1


    /*
    *       if (remainder >= divisor)
    *       {
    *           set LSB of quotient
    *           remainder -= divisor;
    *       }
    */
    bltu r13,r5,div_skip
    ori r3,r3,1
    sub r13,r13,r5
div_skip:

    /*
    *   }
    */
    subi r14,r14,1
    bne r14,zero,divide_loop


    /* Now
    * r3 = quotient
    * r4 = 0x25 for div, 0x24 for divu
    * r6 = 4*C
    * r17 = MSB contains sign of quotient
    */

    
    /*
    *  Conditionally negate signed quotient.  If quotient is unsigned,
    *  the sign already is initialized to 0.
    */
    bge r17,zero,quotient_is_nonnegative
    sub r3,zero,r3      // -r3
quotient_is_nonnegative:


    /*
    *  Final quotient is in r3.
    */
    add r6,r6,sp
    stw r3,0(r6)           // write quotient to stack
    br restore_registers




    /* MULTIPLICATION
    *
    * A "product" is the number that one gets by summing a "multiplicand"
    * several times.  The "multiplier" specifies the number of copies of the
    * multiplicand that are summed.
    *
    * Actual multiplication algorithms don't use repeated addition, however.
    * Shift-and-add algorithms get the same answer as repeated addition, and
    * they are faster.  To compute the lower half of a product (pppp below)
    * one shifts the product left before adding in each of the partial products
    * (a * mmmm) through (d * mmmm).
    *
    * To compute the upper half of a product (PPPP below), one adds in the
    * partial products (d * mmmm) through (a * mmmm), each time following the
    * add by a right shift of the product.
    *
    *     mmmm
    *   * abcd
    *   ------
    *     ####  = d * mmmm
    *    ####   = c * mmmm
    *   ####    = b * mmmm
    *  ####     = a * mmmm
    * --------
    * PPPPpppp
    *
    * The example above shows 4 partial products.  Computing actual Nios II
    * products requires 32 partials.
    *
    * It is possible to compute the result of mulxsu from the result of mulxuu
    * because the only difference between the results of these two opcodes is
    * the value of the partial product associated with the sign bit of rA.
    *
    *   mulxsu = mulxuu - (rA < 0) ? rB : 0;
    *
    * It is possible to compute the result of mulxss from the result of mulxsu
    * because the only difference between the results of these two opcodes is
    * the value of the partial product associated with the sign bit of rB.
    *
    *   mulxss = mulxsu - (rB < 0) ? rA : 0;
    *
    */

mul_immed:
    // Opcode is muli.  Change it into mul for remainder of algorithm.
    mov r6,r5              // Field B is dest register, not field C.
    mov r5,r4              // Field IMM16 is src2, not field B.
    movi r4,0x27           // OPX of mul is 0x27

multiply:
    // Initialize the multiplication loop.
    movi r9,0           // mul_product    = 0
    movi r10,0          // mulxuu_product = 0
    mov r11,r5          // save original multiplier for mulxsu and mulxss
    mov r12,r5          // mulxuu_multiplier (will be shifted)
    movi r16,1          // used to create "rori B,A,1" from "ror B,A,r16"

    /* Now
    * r3 = multiplicand
    * r5 = mul_multiplier
    * r6 = 4 * dest_register (used later as offset to sp)
    * r7 = temp
    * r9 = mul_product
    * r10 = mulxuu_product
    * r11 = original multiplier
    * r12 = mulxuu_multiplier
    * r14 = loop counter (already initialized)
    * r16 = 1
    */


    /*
    *   for (count = 32; count > 0; --count)
    *   {
    */
multiply_loop:

    /*
    *       mul_product <<= 1;
    *       lsb = multiplier & 1;
    */
    slli r9,r9,1
    andi r7,r12,1

    /*
    *       if (lsb == 1)
    *       {
    *           mulxuu_product += multiplicand;
    *       }
    */
    beq r7,zero,mulx_skip
    add r10,r10,r3
    cmpltu r7,r10,r3    // Save the carry from the MSB of mulxuu_product.
    ror r7,r7,r16       // r7 = 0x80000000 on carry, or else 0x00000000
mulx_skip:

    /*
    *       if (MSB of mul_multiplier == 1)
    *       {
    *           mul_product += multiplicand;
    *       }
    */
    bge r5,zero,mul_skip
    add r9,r9,r3
mul_skip:

    /*
    *       mulxuu_product >>= 1;           logical shift
    *       mul_multiplier <<= 1;           done with MSB
    *       mulx_multiplier >>= 1;          done with LSB
    */
    srli r10,r10,1
    or r10,r10,r7           // OR in the saved carry bit.
    slli r5,r5,1
    srli r12,r12,1


    /*
    *   }
    */
    subi r14,r14,1
    bne r14,zero,multiply_loop


    /*
    *  Multiply emulation loop done.
    */

    /* Now
    * r3 = multiplicand
    * r4 = OPX
    * r6 = 4 * dest_register (used later as offset to sp)
    * r7 = temp
    * r9 = mul_product
    * r10 = mulxuu_product
    * r11 = original multiplier
    */


    // Calculate address for result from 4 * dest_register
    add r6,r6,sp


    /*
    *  Select/compute the result based on OPX.
    */


    // OPX == mul?  Then store.
    xori r7,r4,0x27
    beq r7,zero,store_product

    // It's one of the mulx.. opcodes.  Move over the result.
    mov r9,r10

    // OPX == mulxuu?  Then store.
    xori r7,r4,0x07
    beq r7,zero,store_product

    // Compute mulxsu
    //
    // mulxsu = mulxuu - (rA < 0) ? rB : 0;
    //
    bge r3,zero,mulxsu_skip
    sub r9,r9,r11
mulxsu_skip:

    // OPX == mulxsu?  Then store.
    xori r7,r4,0x17
    beq r7,zero,store_product

    // Compute mulxss
    //
    // mulxss = mulxsu - (rB < 0) ? rA : 0;
    //
    bge r11,zero,mulxss_skip
    sub r9,r9,r3
mulxss_skip:
    // At this point, assume that OPX is mulxss, so store


store_product:
    stw  r9,0(r6)


restore_registers:
                        // No need to restore r0.
    ldw r1,    4(sp)
    ldw r2,    8(sp)
    ldw r3,   12(sp)
    ldw r4,   16(sp)
    ldw r5,   20(sp)
    ldw r6,   24(sp)
    ldw r7,   28(sp)
    ldw r8,   32(sp)
    ldw r9,   36(sp)
    ldw r10,  40(sp)
    ldw r11,  44(sp)
    ldw r12,  48(sp)
    ldw r13,  52(sp)
    ldw r14,  56(sp)
    ldw r15,  60(sp)
    ldw r16,  64(sp)
    ldw r17,  68(sp)
    ldw r18,  72(sp)
    ldw r19,  76(sp)
    ldw r20,  80(sp)
    ldw r21,  84(sp)
    ldw r22,  88(sp)
    ldw r23,  92(sp)
    ldw et,   96(sp)
    ldw bt,  100(sp)
    ldw gp,  104(sp)
                        // Don't corrupt sp.
    ldw fp,  112(sp)
                        // Don't corrupt ea.
    ldw ba,  120(sp)
    ldw ra,  124(sp)
    addi sp, sp, 128
    eret

.set at
.set break

