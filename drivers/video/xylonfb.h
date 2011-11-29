/*
 * XYLON logiCVC frame buffer driver
 *
 * Author: Xylon d.o.o.
 *
 * 2002-2007 (c) MontaVista Software, Inc.
 * 2007 (c) Secret Lab Technologies, Ltd.
 * 2009 (c) Xilinx Inc.
 * 2011 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef	__XYLON_FB_H__
#define __XYLON_FB_H__

#define XYLONFB_IOC_MAGIC   'x'
#define XYLONFB_IOC_GETVRAM _IO(XYLONFB_IOC_MAGIC, 0)

/* Supported video modes. */
#define XYLONFB_VM_VESA_640_480_8  0x101
#define XYLONFB_VM_VESA_640_480_16 0x111
#define XYLONFB_VM_VESA_640_480_32 0x112

#define XYLONFB_VM_VESA_800_600_8  0x103
#define XYLONFB_VM_VESA_800_600_16 0x114
#define XYLONFB_VM_VESA_800_600_32 0x115

#define XYLONFB_VM_VESA_1024_768_8  0x105
#define XYLONFB_VM_VESA_1024_768_16 0x117
#define XYLONFB_VM_VESA_1024_768_32 0x118

#define XYLONFB_VM_VESA_1280_1024_8  0x107
#define XYLONFB_VM_VESA_1280_1024_16 0x11A
#define XYLONFB_VM_VESA_1280_1024_32 0x11B

#endif // __XYLON_FB_H__
