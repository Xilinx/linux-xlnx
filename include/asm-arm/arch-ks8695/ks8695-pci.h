/*
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
#ifndef __ASM_ARCH_PLATFORM_PCI_H
#define __ASM_ARCH_PLATFORM_PCI_H 1

/* PCI memory related defines */
#define KS8695P_PCIBG_MEM_BASE      0x60000000  /* memory base for bridge*/  
#define KS8695P_PCI_MEM_BASE	    0x60000000UL/* memory base in PCI space */
#define KS8695P_PCI_MEM_SIZE	    0x20000000UL/* 512M, can be extended */
#define KS8695P_PCI_MEM_MASK	    0xE0000000  /* 512M */

/* PCI IO related defines */
#define KS8695P_PCIBG_IO_BASE       0x10000000  /* io base for bridge */
#define KS8695P_PCI_IO_BASE         0x10000000
#define KS8695P_PCI_IO_SIZE         0x00010000  /* 64K */
#define KS8695P_PCI_IO_MASK         0xFF800000  /* 64K range */

/* new registers specific to KS8695P */
/* PCI related */
#define	KS8695_CRCFID		0x2000
#define	KS8695_CRCFCS		0x2004
#define	KS8695_CRCFRV		0x2008
#define	KS8695_CRCFLT		0x200c
#define	KS8695_CRCBMA		0x2010
#define	KS8695_CRCBA0		0x2014
#define	KS8695_CRCSID		0x202c
#define	KS8695_CRCFIT		0x203c

/* bridge configuration related registers */
#define	KS8695_PBCA		0x2100
#define	KS8695_PBCD		0x2104

/* bridge mode related registers */
#define	KS8695_PBM		0x2200
#define	KS8695_PBCS		0x2204
#define	KS8695_PMBA		0x2208
#define	KS8695_PMBAC		0x220c
#define	KS8695_PMBAM		0x2210
#define	KS8695_PMBAT		0x2214
#define	KS8695_PIOBA		0x2218
#define	KS8695_PIOBAC		0x221c
#define	KS8695_PIOBAM		0x2220
#define	KS8695_PIOBAT		0x2224

/* bits for registers */
/* 0x2200 */
#define	PBM_BRIDGE_MODE		0x80000000

/* 0x2204 */
#define	PBCS_SW_RESET		0x80000000

/* 0x220c */
#define	PMBAC_TRANS_ENABLE	0x80000000

#endif /* __ASM_ARCH_PLATFORM_PCI_H */
