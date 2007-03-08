/*
 *  linux/include/asm-arm/arch-s5c7375/irqs.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONIS  
 *                        Hyok S. Choi <hyok.choi@samsung.com>
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

#ifndef __S5C7375_irqs_h
#define __S5C7375_irqs_h                        1

	#define iSRAMBase    0x06000000    // internal SRAM base address
	#define iSRAMTop     0x06001FFC    // top address of interanl SRAM
	#define SDRAMBase    0x00000000
	#define SDRAMTop     0x003FFFFC
	
#define SHORTNUMOFINT	
	/* the number of interrupt source */
  #ifdef SHORTNUMOFINT
	#define NumOfInt	16
  #else
	#define NumOfInt	32
  #endif
	/* for compatibility for linux */
	#define NR_IRQS (NumOfInt)
	
	/* Interrupt Vector table address */
#ifdef SHORTNUMOFINT
	#define IntVectorTable          iSRAMTop-(NumOfInt << 3)  //internal SRAM area
#else
	#define IntVectorTable          iSRAMTop-(NumOfInt << 2)  //internal SRAM area
#endif
	#define IntVectorTableEnd       iSRAMTop
	//#define IntVectorTable          SDRAMTop-(NumOfInt << 2)  //SDRAM area
	//#define IntVectorTableEnd       SDRAMTop
	
	/*
	 * Interrupt Vector Table
	 */
	
	#define pIVT_TIMER0     (*(volatile unsigned *)(IntVectorTable))
	#define pIVT_TIMER2     (*(volatile unsigned *)(IntVectorTable+0x04))
	#define pIVT_TIMER3     (*(volatile unsigned *)(IntVectorTable+0x08))
	#define pIVT_USB        (*(volatile unsigned *)(IntVectorTable+0x0C))
	#define pIVT_TIMER4     (*(volatile unsigned *)(IntVectorTable+0x10))	 // ADD BY HIS
	#define pIVT_DMA        (*(volatile unsigned *)(IntVectorTable+0x14))
	#define pIVT_TIMER1     (*(volatile unsigned *)(IntVectorTable+0x18))
	#define pIVT_I2C        (*(volatile unsigned *)(IntVectorTable+0x1C))
	#define pIVT_COMMRX     (*(volatile unsigned *)(IntVectorTable+0x20))
	#define pIVT_COMMTX     (*(volatile unsigned *)(IntVectorTable+0x24))
	#define pIVT_GPIO       (*(volatile unsigned *)(IntVectorTable+0x28))
	#define pIVT_EXT0       (*(volatile unsigned *)(IntVectorTable+0x2C))
	#define pIVT_EXT1       (*(volatile unsigned *)(IntVectorTable+0x30))
	#define pIVT_EXT2       (*(volatile unsigned *)(IntVectorTable+0x34))
	#define pIVT_EXT3       (*(volatile unsigned *)(IntVectorTable+0x38))
	
	
	/*
	 *  define the interrupt source corresponing to each interrupt register bits 
	 */
	
	#define INT_TIMER0      0x00000001
	#define INT_TIMER2      0x00000002
	#define INT_TIMER3      0x00000004
	#define INT_USB	        0x00000008
	#define INT_TIMER4      0x00000010	
	#define INT_DMA	        0x00000020
	#define INT_TIMER1      0x00000040
	#define INT_I2C	        0x00000080
	#define INT_COMMRX      0x00000100
	#define INT_COMMTX      0x00000200
	#define INT_GPIO        0x00000400
	#define INT_EXT0        0x00000800
	#define INT_EXT1        0x00001000
	#define INT_EXT2        0x00002000
	#define INT_EXT3        0x00004000

	#define INT_N_TIMER0    	0
	#define INT_N_TIMER2      1
	#define INT_N_TIMER3      2
	#define INT_N_USB	     	3
	#define INT_N_TIMER4      4
	#define INT_N_DMA	      	5
	#define INT_N_TIMER1      6
	#define INT_N_I2C	      	7
	#define INT_N_COMMRX    	8
	#define INT_N_COMMTX   	9
	#define INT_N_GPIO        	10
	#define INT_N_EXT0        	11
	#define INT_N_EXT1        	12
	#define INT_N_EXT2        	13
	#define INT_N_EXT3        	14

	
	
	#define EnableFIQ()     (rINTCON = ((rINTCON) & (0x0E)))
	#define DisableFIQ()    (rINTCON = ((rINTCON) | (0x01)))
	
	#define EnableIRQ()     (rINTCON = ((rINTCON )& (0x0D)))
	#define DisableIRQ()    (rINTCON = ((rINTCON) | (0x02)))
	
	#define EnableGMask()   (rINTCON = ((rINTCON) | (0x08)))
	#define DisableGMask()  (rINTCON = ((rINTCON) & (0x07)))
	
	#define EnableInt(x)    (rINTMSK = ((rINTMSK) & (~(x))))
	#define DisableInt(x)   (rINTMSK = ((rINTMSK) | (x)))


//	#define OS_TIMER        INT_TIMER4 // used in irq.c?

#endif /* End of __irqs_h */
