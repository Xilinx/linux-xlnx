/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _NLM_HAL_XLP_H
#define _NLM_HAL_XLP_H

#define PIC_UART_0_IRQ			17
#define PIC_UART_1_IRQ			18
#define PIC_PCIE_LINK_0_IRQ		19
#define PIC_PCIE_LINK_1_IRQ		20
#define PIC_PCIE_LINK_2_IRQ		21
#define PIC_PCIE_LINK_3_IRQ		22

#define PIC_EHCI_0_IRQ			23
#define PIC_EHCI_1_IRQ			24
#define PIC_OHCI_0_IRQ			25
#define PIC_OHCI_1_IRQ			26
#define PIC_OHCI_2_IRQ			27
#define PIC_OHCI_3_IRQ			28
#define PIC_2XX_XHCI_0_IRQ		23
#define PIC_2XX_XHCI_1_IRQ		24
#define PIC_2XX_XHCI_2_IRQ		25

#define PIC_MMC_IRQ			29
#define PIC_I2C_0_IRQ			30
#define PIC_I2C_1_IRQ			31
#define PIC_I2C_2_IRQ			32
#define PIC_I2C_3_IRQ			33

#ifndef __ASSEMBLY__

/* SMP support functions */
void xlp_boot_core0_siblings(void);
void xlp_wakeup_secondary_cpus(void);

void xlp_mmu_init(void);
void nlm_hal_init(void);
int xlp_get_dram_map(int n, uint64_t *dram_map);

/* Device tree related */
void xlp_early_init_devtree(void);
void *xlp_dt_init(void *fdtp);

static inline int cpu_is_xlpii(void)
{
	int chip = read_c0_prid() & 0xff00;

	return chip == PRID_IMP_NETLOGIC_XLP2XX;
}

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_NLM_XLP_H */
