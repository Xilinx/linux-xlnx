/*
 * include/asm-microblaze/setup.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_SETUP_H
#define _ASM_SETUP_H

#include <linux/init.h>

#define COMMAND_LINE_SIZE	256

int setup_early_printk(char *opt);

void __init setup_memory(void);

#endif /* _ASM_SETUP_H */
