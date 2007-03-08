/*
 *  linux/include/asm-arm/arch-s5c7375/time.h
 *
 *  Copyright (C) SAMSUNG ELECTRONICS 
 *                      Hyok S. Choi <hyok.choi@samsung.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <asm/system.h>
#include <asm/leds.h>
#include <asm/arch/s5c7375.h>
/*
 *	Bits 	Name 	Type 	Function 	 
 *	15:12 	-	Read 	Reserved. Read only as zero 	 
 *	11:10 	M 	Read/write 	Operating mode :
 *				00 : Free running timer mode(default) 	01 : Periodic timer mode. 	 
 *				10 : Free running counter mode. 		11 : Periodic counter mode. 	 
 *	9:8 	ES 	Read/write 	External input active edge selection. 
 *				00 : Positive edge(default). 01 : Negative edge.
 *				10 : Both positive and negative edge. 11 : unused. 	 
 *	7 	-	Read 	Reserved. Read only as zero 	 
 *	6 	OM 	Read/write 	Time output mode. 0 : Toggle mode(default). 1 : Pulse mode. 	 
 *	5 	UDS 	Read/write 	Up/down counting control selection. 
 *				0 : Up/down is controlled by UD field of TxCTR register(default).
 *				1 : Up/down is controlled by EXTUD[4:0]input register. 	 
 *	4 	UD 	Read/write 	Up/down counting selection. 
 *				0 : Down counting(default). 1 : Up counting. 	 
 *				This bit affects the counting of timer only when UDS bit is LOW. 	 
 *	3 	-	Read 	Reserved. Read only as zero 	 
 *	2 	OE 	Read/write 	Output enable.
 *				0 : Disable timer outputs(default). 1 : Enable timer outputs. 	 
 *				This bit affects the generation of timer interrupt only when TE bit is HIGH. 	 
 *	1 	IE 	Read/write 	Interrupt enable. 0 : Toggle mode(default). 1 : Pulse mode. 	 
 *				This bit affects the generation of timer output only when TE bit is HIGH. 	 
 *	0 	TE 	Read/write 	Timer enable. 0 : Diable timer(default). 1 : Enable timer. 	 
 */

#define TMR_TE_DISABLE				0x0000
#define TMR_TE_ENABLE				0x0001

#define TMR_IE_TOGGLE				0x0000
#define TMR_IE_PULSE				0x0002

#define TMR_OE_DISABLE				0x0000
#define TMR_OE_ENABLE				0x0004

#define TMR_UD_DOWN				0x0000
#define TMR_UD_UP					0x0010

#define TMR_UDS_TxCTR				0x0000
#define TMR_UDS_EXTUD				0x0020

#define TMR_OM_TOGGLE				0x0000
#define TMR_OM_PULSE				0x0040

#define TMR_ES_POS					0x0000
#define TMR_ES_NEG					0x0100
#define TMR_ES_BOTH				0x0200

#define TMR_M_FREE_TIMER			0x0000
#define TMR_M_PERIODIC_TIMER		0x0400
#define TMR_M_FREE_COUNTER		0x0800
#define TMR_M_PERIODIC_COUNTER	0x0C00




/*
 * simpler new version of gettimeoffset
 * by Hyok S. Choi
 */
#define TICKS_PER_uSEC                  24
#define CLOCKS_PER_USEC	(2* ECLK/ (SYS_TIMER03_PRESCALER +1))
							//(ECLK/1000000) /* (ARM_CLK/1000000) */
							/* this is the newer version */

