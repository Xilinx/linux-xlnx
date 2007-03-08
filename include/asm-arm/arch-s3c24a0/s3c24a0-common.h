/*
 * include/asm-arm/arch-s3c24a0/s3c24a0-common.h
 *
 * $Id: s3c24a0-common.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#ifndef _INCLUDE_LINUETTE_COMMON_H_
#define _INCLUDE_LINUETTE_COMMON_H_
#ifndef __ASSEMBLY__

/* 
 * New Audio Format MSM9842
 *
 * NOTE:
 *  refer to linux/soundcard.h
 */
#define AFMT_4_ADPCM2 0x80000000	// 4bit ADPCM2
#define AFMT_5_ADPCM2 0x40000000	// 5bit ADPCM2
#define AFMT_6_ADPCM2 0x20000000	// 6bit ADPCM2
#define AFMT_7_ADPCM2 0x10000000	// 7bit ADPCM2
#define AFMT_8_ADPCM2 0x08000000	// 8bit ADPCM2

/*
 * device name
 */
#define BIOS_NAME		"apm_bios"

/* definition of key/buttons */
#include "s3c24a0-key.h"

#endif	/* __ASSEMBLY__ */
#endif /* _INCLUDE_LINUETTE_COMMON_H_ */
