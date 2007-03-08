/* $Id: dma.h,v 1.1 2006/07/05 06:20:25 gerg Exp $
 *
 * Copyright 2004 (C) Microtronix Datacom Ltd.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _ASM_NIOS2_DMA_H
#define _ASM_NIOS2_DMA_H

#include <linux/kernel.h>
#include <asm/asm-offsets.h>

#define MAX_DMA_ADDRESS	(LINUX_SDRAM_END)

int request_dma(unsigned int, const char *);
void free_dma(unsigned int);
void enable_dma(unsigned int dmanr);
void disable_dma(unsigned int dmanr);
void set_dma_count(unsigned int dmanr, unsigned int count);
int get_dma_residue(unsigned int dmanr);
void nios2_set_dma_data_width(unsigned int dmanr, unsigned int width);

void nios2_set_dma_handler(unsigned int dmanr, int (*handler)(void*, int), void* user);
int nios2_request_dma(const char *);

void nios2_set_dma_mode(unsigned int dmanr, unsigned int mode);
void nios2_set_dma_rcon(unsigned int dmanr, unsigned int set);
void nios2_set_dma_wcon(unsigned int dmanr, unsigned int set);
void nios2_set_dma_raddr(unsigned int dmanr, unsigned int a);
void nios2_set_dma_waddr(unsigned int dmanr, unsigned int a);

static inline unsigned long claim_dma_lock(void)
{
}

static inline void release_dma_lock(unsigned long flags)
{
}

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy 	(0)
#endif

#endif /* !(_ASM_NIOS2_DMA_H) */
