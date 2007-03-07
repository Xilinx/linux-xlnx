/*
 * include/asm-microblaze/param.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_PARAM_H
#define _ASM_PARAM_H

#ifdef __KERNEL__
# define HZ		100		/* internal timer frequency */
# define USER_HZ	100		/* for user interfaces in "ticks" */
# define CLOCKS_PER_SEC (USER_HZ)	/* frequnzy at which times() counts */
#endif

#ifndef NGROUPS
#define NGROUPS		32
#endif

#ifndef NOGROUP
#define NOGROUP		(-1)
#endif

#define EXEC_PAGESIZE   4096

#ifndef HZ
#define HZ 100
#endif

#define MAXHOSTNAMELEN	64	/* max length of hostname */

#endif /* _ASM_PARAM_H */
