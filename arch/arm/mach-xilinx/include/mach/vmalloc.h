/* arch/arm/mach-xilinx/include/mach/vmalloc.h 
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_VMALLOC_H__
#define __ASM_ARCH_VMALLOC_H__

/* vmalloc end address, this is setup so that devices can be mapped flat 
   with respect to virtual to physical addresses
*/

#define VMALLOC_END       IO_BASE

#endif /* __ASM_ARCH_VMALLOC_H__ */
