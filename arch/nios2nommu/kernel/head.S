/*
 * head.S for Altera's Excalibur development board with nios processor
 *
 * (c) Vic Phillips, Microtronix Datacom Ltd., 2001
 * (C) Copyright 2004  Microtronix Datacom Ltd
 *
 * Based on the following from the Excalibur sdk distribution:
 *	NA_MemoryMap.s, NR_JumpToStart.s, NR_Setup.s, NR_CWPManager.s
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <asm/asm-offsets.h>
#include <asm/asm-macros.h>


#ifdef CONFIG_CRC_CHECK
/**********************************************/
/* Define where the CRC table lives in flash. */
/* The __CRC_Sector_Size is the flash sector  */
/* size for the address range.                */
/**********************************************/

	GEQU	__CRC_Table_Begin,(na_flash)+0x4000   /* Second sector of main board flash */
	GEQU	__CRC_Sector_Size,0x2000
#endif

/*
 * This global variable is used as an extension to the nios'
 * STATUS register to emulate a user/supervisor mode.
 */
	.data
	.align	2
	.set noat
	.global	status_extension
status_extension:
	.long	0

	.global _current_thread
_current_thread:
	.long	0
/*
 * Input(s): passed from u-boot
 *   r4 - Optional pointer to a board information structure.
 *   r5 - Optional pointer to the physical starting address of the init RAM
 *        disk.
 *   r6 - Optional pointer to the physical ending address of the init RAM
 *        disk.
 *   r7 - Optional pointer to the physical starting address of any kernel
 *        command-line parameters.
 */

/*
 * First executable code - detected and jumped to by the ROM bootstrap
 * if the code resides in flash (looks for "Nios" at offset 0x0c from
 * the potential executable image).
 */
	.text
	.global _start
_start:
	wrctl	status,r0		/* Disable interrupts */
	
	/* Flush all cache lines within the instruction cache */

	movia	r1,NIOS2_ICACHE_SIZE
	movui	r2,NIOS2_ICACHE_LINE_SIZE

text_flush:
	flushi	r1
	sub	r1,r1,r2
	bgt	r1,r0,text_flush
	br	1f

	/* This is the default location for the exception 
	 * handler. Code in jump to our handler
	 */
	
	movia	r24,inthandler
	jmp	r24
1:		
	/*
	 * After flushing the instruction cache, we must flush the data
	 * cache.
	 */

	movia	r1,NIOS2_DCACHE_SIZE
	movi	r2,NIOS2_DCACHE_LINE_SIZE

data_flush:
	flushd	0(r1)
	sub	r1,r1,r2
	bgt	r1,r0,data_flush

NR_MoveStart:
#ifdef CONFIG_BREAK_ON_START
	break
#endif //CONFIG_BREAK_ON_START
	nextpc	r1			/* Find out where we are */
chkadr:	
	movia	r2,chkadr
	beq	r1,r2,finish_move	/* We are running in RAM done */
	addi	r1,r1,(_start - chkadr)	/* Source */
	movia	r2,_start		/* Destination */
	movia	r3,__data_end		/* End of copy */
	
loop_move:				// r1: src, r2: dest, r3: last dest
	ldw	r8,0(r1)		// load a word from [r1]
	stw	r8,0(r2)		// stort a word to dest [r2]
	flushd	0(r2)			// Flush cache for safty
	addi 	r1,r1,4			// inc the src addr
	addi	r2,r2,4			// inc the dest addr
	blt	r2,r3,loop_move
		
	movia	r1,finish_move		// VMA(_start)->l1
	jmp	r1			// jmp to _start	

finish_move:

	//------------------------------------
	// Disable interrupts on known devices
	//
#ifdef NA_ENET_ASM
#ifdef NA_ENET_RESET_ASM
	movia	r1,NA_ENET_RESET_ASM	// ethernet reset address
	stwio	r0,0(r1)		// reset
#endif
#ifdef NA_ENET_RESET_N_ASM
	movia	r1,NA_ENET_RESET_N_ASM	// ethernet reset address
	stwio	r0,0(r1)		// reset
#endif
	nop				// give it some time
	nop				//
	nop				//
	nop				//
#endif
#ifdef NA_TIMER0_ASM
	movia	r1,NA_TIMER0_ASM	    // get timer address
	stwio	r0,NP_TIMERCONTROL_ASM(r1)  // clear interrupt enable
	stwio	r0,NP_TIMERSTATUS_ASM(r1)   // clear interrupt condition
#endif
#ifdef NA_UART0_ASM
	movia	r1,NA_UART0_ASM
	stwio	r0,NP_UARTCONTROL_ASM(r1)	// clear interrupt enable
	stwio	r0,NP_UARTSTATUS_ASM(r1)	// clear interrupt status
#endif
#ifdef NA_UART1_ASM
	movia	r1,NA_UART1_ASM
	stwio	r0,NP_UARTCONTROL_ASM(r1)	// clear interrupt enable
	stwio	r0,NP_UARTSTATUS_ASM(r1)	// clear interrupt status
#endif
#ifdef NA_UART2_ASM
	movia	r1,NA_UART2_ASM
	stwio	r0,NP_UARTCONTROL_ASM(r1)	// clear interrupt enable
	stwio	r0,NP_UARTSTATUS_ASM(r1)	// clear interrupt status
#endif
#ifdef NA_UART3_ASM
	movia	r1,NA_UART3_ASM
	stwio	r0,NP_UARTCONTROL_ASM(r1)	// clear interrupt enable
	stwio	r0,NP_UARTSTATUS_ASM(r1)	// clear interrupt status
#endif
#ifdef NA_IDE_INTERFACE_ASM
	movia	r1,NA_IDE_INTERFACE_ASM	// ATA reset
	stwio	r0,0(r1)		// write to control register
#endif
#ifdef NA_ENET_ASM
#ifdef NA_ENET_RESET_ASM
	movia	r1,NA_ENET_RESET_ASM	// ethernet reset address
	movui	r2,1			// reset
	stwio	r2,0(r1)		//
#endif
#ifdef NA_ENET_RESET_N_ASM
	movia	r1,NA_ENET_RESET_N_ASM	// ethernet reset address
	movui	r2,1			// reset
	stwio	r2,0(r1)		//
#endif
#endif
	wrctl	ienable,r0		// Mask off all possible interrupts

	//------------------------------------------------------
	// Zero out the .bss segment (uninitialized common data)
	//
	movia	r2,__bss_start		// presume nothing is between
	movia	r1,_end			// the .bss and _end.
1:
	stb	r0,0(r2)
	addi	r2,r2,1
	bne	r1,r2,1b

	//------------------------------------------------------
	// Call main() with interrupts disabled
	//
	movia	r1,status_extension	// get the STATUS extension address
	movi	r2,PS_S_ASM		// set initial mode = supervisor
	stw	r2,0(r1)
	
	movia	r1,init_thread_union	// set stack at top of the task union
	addi	sp,r1,THREAD_SIZE_ASM
	movia	r2,_current_thread	// Remember current thread
	stw	r1,0(r2)

	movia	r1,nios2_boot_init	// save args r4-r7 passed from u-boot
	callr	r1
	
	movia	r1,main			// call main as a subroutine
	callr	r1

	//------------------------------------------------------------------
	// If we return from main,  break to the oci debugger and buggered we are
	//
	break	

	/* End of startup code */
.set at


