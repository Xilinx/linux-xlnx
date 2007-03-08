/*--------------------------------------------------------------------
 *
 * Assembly language portions of Nios GDB Stub
 *
 * arch\nios2nommu\kernel\switch.S
 *
 * Derived from Nios1
 *
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Modified for uClinux - Vic - Apr 2002
 * Jan/20/2004		dgt	    NiosII
 *
 ---------------------------------------------------------------------*/


//;dgt2;tmp;

	.equ	ethernet_exists, 1


	.equ	gdbRegistersGeneral,0
	.equ	gdbRegistersPC,32
	.equ	gdbRegistersCtl0Ctl1,33
	.equ	gdbRegistersCtl2Ctl3,34
	.equ	gdbTrapNumber,35		; ISR can report trap number here


	.text

	.global	StubBreakpointHandler
	.global	StubHarmlessHandler
	.global	StubButtonHandler
	.global StubHWBreakpointHandler
	.global	GDBMain

	.comm	_gdb_stub_stack,1024,4	; Local stack, statically allocated.
	.equ	gdbStubStacktop,_gdb_stub_stack+992


StubHarmlessHandler:
//;dgt2;tmp

	.equ	gdbBreakChar,0x3
	.global	StubUartHandler

StubUartHandler:
//;dgt2;tmp

StubUartRx:
//;dgt2;tmp

StubHWBreakpointHandler:
//;dgt2;tmp

StubBreakpointHandler:
//;dgt2;tmp

#ifdef __KERNEL__
;----------------------------------------
;         Name: nr_uart_rxchar
;  Description: Read character if available
;        Input: %o0: UART base to use
;       Output: %o0 = character 0-0xff, or -1 if none present
; Side Effects: %g0 & %g1 altered
;    CWP Depth: 0
;

	.global	nr_uart_rxchar
nr_uart_rxchar:
//;dgt2;tmp


;----------------------------------------
;         Name: nr_uart_txchar
;  Description: Send a single byte out the UART
;        Input: %o0 = A character
;               %o1 = the UART to use, 0 for default
;       Output: none
; Side Effects: %g0 & %g1 altered, CPU waits for UART
;    CWP Depth: 0
;

; nr_uart_txchar
		.global	nr_uart_txchar
nr_uart_txchar:
//;dgt2;tmp

#endif
