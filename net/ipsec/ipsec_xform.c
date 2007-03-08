/*
 * Common routines for IPSEC transformations.
 * Copyright (C) 1996, 1997  John Ioannidis.
 * Copyright (C) 1998, 1999, 2000, 2001  Richard Guy Briggs.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * RCSID $Id: ipsec_xform.c,v 1.65 2005/04/29 05:10:22 mcr Exp $
 */

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>
#include <linux/kernel.h> /* printk() */

#include "freeswan/ipsec_param.h"

#ifdef MALLOC_SLAB
# include <linux/slab.h> /* kmalloc() */
#else /* MALLOC_SLAB */
# include <linux/malloc.h> /* kmalloc() */
#endif /* MALLOC_SLAB */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/skbuff.h>
#include <linux/random.h>	/* get_random_bytes() */
#include <freeswan.h>
#ifdef SPINLOCK
# ifdef SPINLOCK_23
#  include <linux/spinlock.h> /* *lock* */
# else /* SPINLOCK_23 */
#  include <asm/spinlock.h> /* *lock* */
# endif /* SPINLOCK_23 */
#endif /* SPINLOCK */

#include <net/ip.h>

#include "freeswan/radij.h"
#include "freeswan/ipsec_encap.h"
#include "freeswan/ipsec_radij.h"
#include "freeswan/ipsec_xform.h"
#include "freeswan/ipsec_ipe4.h"
#include "freeswan/ipsec_ah.h"
#include "freeswan/ipsec_esp.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#ifdef CONFIG_KLIPS_DEBUG
int debug_xform = 0;
#endif /* CONFIG_KLIPS_DEBUG */

/*
 * $Log: ipsec_xform.c,v $
 * Revision 1.65  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.64  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.63  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.62.30.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.62  2002/05/14 02:34:21  rgb
 * Delete stale code.
 *
 * Revision 1.61  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.60  2002/04/24 07:36:33  mcr
 * Moved from ./klips/net/ipsec/ipsec_xform.c,v
 *
 * Revision 1.59  2002/03/29 15:01:36  rgb
 * Delete decommissioned code.
 *
 * Revision 1.58  2002/01/29 17:17:57  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.57  2002/01/29 04:00:53  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.56  2001/11/27 05:17:22  mcr
 * 	turn off the worst of the per-packet debugging.
 *
 * Revision 1.55  2001/11/26 09:23:50  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.54  2001/10/18 04:45:21  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.53  2001/09/08 21:13:34  rgb
 * Added pfkey ident extension support for ISAKMPd. (NetCelo)
 *
 * Revision 1.52  2001/06/14 19:35:11  rgb
 * Update copyright date.
 *
 * Revision 1.51  2001/05/30 08:14:03  rgb
 * Removed vestiges of esp-null transforms.
 *
 * Revision 1.50  2001/05/03 19:43:18  rgb
 * Initialise error return variable.
 * Update SENDERR macro.
 * Fix sign of error return code for ipsec_tdbcleanup().
 * Use more appropriate return code for ipsec_tdbwipe().
 *
 * Revision 1.49  2001/04/19 18:56:17  rgb
 * Fixed tdb table locking comments.
 *
 * Revision 1.48  2001/02/27 22:24:55  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.47  2000/11/06 04:32:08  rgb
 * Ditched spin_lock_irqsave in favour of spin_lock_bh.
 *
 * Revision 1.46  2000/09/20 16:21:57  rgb
 * Cleaned up ident string alloc/free.
 *
 * Revision 1.45  2000/09/08 19:16:51  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Removed all references to CONFIG_IPSEC_PFKEYv2.
 *
 * Revision 1.44  2000/08/30 05:29:04  rgb
 * Compiler-define out no longer used tdb_init() in ipsec_xform.c.
 *
 * Revision 1.43  2000/08/18 21:30:41  rgb
 * Purged all tdb_spi, tdb_proto and tdb_dst macros.  They are unclear.
 *
 * Revision 1.42  2000/08/01 14:51:51  rgb
 * Removed _all_ remaining traces of DES.
 *
 * Revision 1.41  2000/07/28 14:58:31  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.40  2000/06/28 05:50:11  rgb
 * Actually set iv_bits.
 *
 * Revision 1.39  2000/05/10 23:11:09  rgb
 * Added netlink debugging output.
 * Added a cast to quiet down the ntohl bug.
 *
 * Revision 1.38  2000/05/10 19:18:42  rgb
 * Cast output of ntohl so that the broken prototype doesn't make our
 * compile noisy.
 *
 * Revision 1.37  2000/03/16 14:04:59  rgb
 * Hardwired CONFIG_IPSEC_PFKEYv2 on.
 *
 * Revision 1.36  2000/01/26 10:11:28  rgb
 * Fixed spacing in error text causing run-in words.
 *
 * Revision 1.35  2000/01/21 06:17:16  rgb
 * Tidied up compiler directive indentation for readability.
 * Added ictx,octx vars for simplification.(kravietz)
 * Added macros for HMAC padding magic numbers.(kravietz)
 * Fixed missing key length reporting bug.
 * Fixed bug in tdbwipe to return immediately on NULL tdbp passed in.
 *
 * Revision 1.34  1999/12/08 00:04:19  rgb
 * Fixed SA direction overwriting bug for netlink users.
 *
 * Revision 1.33  1999/12/01 22:16:44  rgb
 * Minor formatting changes in ESP MD5 initialisation.
 *
 * Revision 1.32  1999/11/25 09:06:36  rgb
 * Fixed error return messages, should be returning negative numbers.
 * Implemented SENDERR macro for propagating error codes.
 * Added debug message and separate error code for algorithms not compiled
 * in.
 *
 * Revision 1.31  1999/11/23 23:06:26  rgb
 * Sort out pfkey and freeswan headers, putting them in a library path.
 *
 * Revision 1.30  1999/11/18 04:09:20  rgb
 * Replaced all kernel version macros to shorter, readable form.
 *
 * Revision 1.29  1999/11/17 15:53:40  rgb
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 *
 * Revision 1.28  1999/10/18 20:04:01  rgb
 * Clean-out unused cruft.
 *
 * Revision 1.27  1999/10/03 19:01:03  rgb
 * Spinlock support for 2.3.xx and 2.0.xx kernels.
 *
 * Revision 1.26  1999/10/01 16:22:24  rgb
 * Switch from assignment init. to functional init. of spinlocks.
 *
 * Revision 1.25  1999/10/01 15:44:54  rgb
 * Move spinlock header include to 2.1> scope.
 *
 * Revision 1.24  1999/10/01 00:03:46  rgb
 * Added tdb structure locking.
 * Minor formatting changes.
 * Add function to initialize tdb hash table.
 *
 * Revision 1.23  1999/05/25 22:42:12  rgb
 * Add deltdbchain() debugging.
 *
 * Revision 1.22  1999/05/25 21:24:31  rgb
 * Add debugging statements to deltdbchain().
 *
 * Revision 1.21  1999/05/25 03:51:48  rgb
 * Refix error return code.
 *
 * Revision 1.20  1999/05/25 03:34:07  rgb
 * Fix error return for flush.
 *
 * Revision 1.19  1999/05/09 03:25:37  rgb
 * Fix bug introduced by 2.2 quick-and-dirty patch.
 *
 * Revision 1.18  1999/05/05 22:02:32  rgb
 * Add a quick and dirty port to 2.2 kernels by Marc Boucher <marc@mbsi.ca>.
 *
 * Revision 1.17  1999/04/29 15:20:16  rgb
 * Change gettdb parameter to a pointer to reduce stack loading and
 * facilitate parameter sanity checking.
 * Add sanity checking for null pointer arguments.
 * Add debugging instrumentation.
 * Add function deltdbchain() which will take care of unlinking,
 * zeroing and deleting a chain of tdbs.
 * Add a parameter to tdbcleanup to be able to delete a class of SAs.
 * tdbwipe now actually zeroes the tdb as well as any of its pointed
 * structures.
 *
 * Revision 1.16  1999/04/16 15:36:29  rgb
 * Fix cut-and-paste error causing a memory leak in IPIP TDB freeing.
 *
 * Revision 1.15  1999/04/11 00:29:01  henry
 * GPL boilerplate
 *
 * Revision 1.14  1999/04/06 04:54:28  rgb
 * Fix/Add RCSID Id: and Log: bits to make PHMDs happy.  This includes
 * patch shell fixes.
 *
 * Revision 1.13  1999/02/19 18:23:01  rgb
 * Nix debug off compile warning.
 *
 * Revision 1.12  1999/02/17 16:52:16  rgb
 * Consolidate satoa()s for space and speed efficiency.
 * Convert DEBUG_IPSEC to KLIPS_PRINT
 * Clean out unused cruft.
 * Ditch NET_IPIP dependancy.
 * Loop for 3des key setting.
 *
 * Revision 1.11  1999/01/26 02:09:05  rgb
 * Remove ah/esp/IPIP switching on include files.
 * Removed CONFIG_IPSEC_ALGO_SWITCH macro.
 * Removed dead code.
 * Clean up debug code when switched off.
 * Remove references to INET_GET_PROTOCOL.
 * Added code exclusion macros to reduce code from unused algorithms.
 *
 * Revision 1.10  1999/01/22 06:28:55  rgb
 * Cruft clean-out.
 * Put random IV generation in kernel.
 * Added algorithm switch code.
 * Enhanced debugging.
 * 64-bit clean-up.
 *
 * Revision 1.9  1998/11/30 13:22:55  rgb
 * Rationalised all the klips kernel file headers.  They are much shorter
 * now and won't conflict under RH5.2.
 *
 * Revision 1.8  1998/11/25 04:59:06  rgb
 * Add conditionals for no IPIP tunnel code.
 * Delete commented out code.
 *
 * Revision 1.7  1998/10/31 06:50:41  rgb
 * Convert xform ASCII names to no spaces.
 * Fixed up comments in #endif directives.
 *
 * Revision 1.6  1998/10/19 14:44:28  rgb
 * Added inclusion of freeswan.h.
 * sa_id structure implemented and used: now includes protocol.
 *
 * Revision 1.5  1998/10/09 04:32:19  rgb
 * Added 'klips_debug' prefix to all klips printk debug statements.
 *
 * Revision 1.4  1998/08/12 00:11:31  rgb
 * Added new xform functions to the xform table.
 * Fixed minor debug output spelling error.
 *
 * Revision 1.3  1998/07/09 17:45:31  rgb
 * Clarify algorithm not available message.
 *
 * Revision 1.2  1998/06/23 03:00:51  rgb
 * Check for presence of IPIP protocol if it is setup one way (we don't
 * know what has been set up the other way and can only assume it will be
 * symmetrical with the exception of keys).
 *
 * Revision 1.1  1998/06/18 21:27:51  henry
 * move sources from klips/src to klips/net/ipsec, to keep stupid
 * kernel-build scripts happier in the presence of symlinks
 *
 * Revision 1.3  1998/06/11 05:54:59  rgb
 * Added transform version string pointer to xformsw initialisations.
 *
 * Revision 1.2  1998/04/21 21:28:57  rgb
 * Rearrange debug switches to change on the fly debug output from user
 * space.  Only kernel changes checked in at this time.  radij.c was also
 * changed to temporarily remove buggy debugging code in rj_delete causing
 * an OOPS and hence, netlink device open errors.
 *
 * Revision 1.1  1998/04/09 03:06:13  henry
 * sources moved up from linux/net/ipsec
 *
 * Revision 1.1.1.1  1998/04/08 05:35:02  henry
 * RGB's ipsec-0.8pre2.tar.gz ipsec-0.8
 *
 * Revision 0.5  1997/06/03 04:24:48  ji
 * Added ESP-3DES-MD5-96
 *
 * Revision 0.4  1997/01/15 01:28:15  ji
 * Added new transforms.
 *
 * Revision 0.3  1996/11/20 14:39:04  ji
 * Minor cleanups.
 * Rationalized debugging code.
 *
 * Revision 0.2  1996/11/02 00:18:33  ji
 * First limited release.
 *
 *
 */
