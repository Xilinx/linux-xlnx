/*
 * include/asm-microblaze/scatterlist.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_SCATTERLIST_H
#define _ASM_SCATTERLIST_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <asm/dma.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long sg_magic;
#endif
	unsigned long page_link;
	unsigned int offset;
	unsigned int length;

	/* For TCE support */
	dma_addr_t dma_address;
	u32 dma_length;
};

#endif

#endif
