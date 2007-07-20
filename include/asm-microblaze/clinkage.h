/*
 * include/asm-microblaze/clinkage.h -- Macros to reflect C symbol-naming conventions
 *
 *  Copyright (C) 2003       John Williams <jwilliams@itee.uq.edu.au>
 *  Copyright (C) 2001,2002  NEC Corporatione
 *  Copyright (C) 2001,2002  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 * Microblaze port by John Williams
 */

#ifndef __MICROBLAZE_CLINKAGE_H__
#define __MICROBLAZE_CLINKAGE_H__

#define __ASSEMBLY__

#include <linux/linkage.h>

#define C_SYMBOL_NAME(name) 	name 
#define C_ENTRY(name)		.globl name;   .align 4;  name
#define C_END(name)

#endif /* __MICROBLAZE_CLINKAGE_H__ */
