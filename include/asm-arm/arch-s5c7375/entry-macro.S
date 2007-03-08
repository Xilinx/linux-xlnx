/*
 *  arch/armnommu/mach-s5c7375/entry-macro.S
 *
 *  Copyright (C) 2003 Hyok S. Choi <hyok.choi@samsung.com>
 *  Samsung Electronics Co.,Ltd.
 *
 *  defines machine dependent entry macros.
 *  included in the arch/armnommu/kernel/entry.S
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

#if defined(CONFIG_ARCH_S5C7375)
                .macro  disable_fiq
                .endm
        #if 1   
                        /* r0     r6      r5   lr */
                .macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
                ldr    \irqstat, =(rINTBase + 0x3C) @ rIRQISPR
                ldr    \irqstat,   [\irqstat, #0]   @ load irqstat

                mov    \irqnr, #0 
1001:           
                tst       \irqstat, #1
                bne     1002f
                add     \irqnr, \irqnr, #1
                mov    \irqstat, \irqstat, lsr #1
                cmp    \irqnr, #NR_IRQS 
                bcc     1001b
1002:           /* EQ will be set if we reach 32 */
                .endm   
                
        #else     /* for better performance */ 
                
                        /* r0     r6      r5   lr */
                .macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
                ldr      \irqstat, =(rINTBase + 0x78) @ rIVEC_ADDR
                ldr    \irqnr,   [\irqstat]             @ load irqnr * 4
                mov     \irqnr, \irqnr, lsr #2          @ irqnr >> 2
                movs   \tmp,  \irqnr
                .endm
        #endif
                .macro  irq_prio_table
                .endm
#endif
