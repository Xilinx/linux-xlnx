/*
 * @(#) lifetime structure utilities
 *
 * Copyright (C) 2001  Richard Guy Briggs  <rgb@freeswan.org>
 *                 and Michael Richardson  <mcr@freeswan.org>
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
 * RCSID $Id: ipsec_life.c,v 1.13 2004/07/10 19:11:18 mcr Exp $
 *
 */

/* 
 * This provides series of utility functions for dealing with lifetime
 * structures.
 *
 * ipsec_check_lifetime - returns -1    hard lifetime exceeded
 *                                 0    soft lifetime exceeded
 *                                 1    everything is okay
 *                        based upon whether or not the count exceeds hard/soft
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>	/* for CONFIG_IP_FORWARD */
#endif
#include <linux/version.h>
#include <linux/kernel.h> /* printk() */

#include "openswan/ipsec_param.h"

#include <linux/netdevice.h>   /* struct device, struct net_device_stats and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/skbuff.h>
#include <openswan.h>

#include "openswan/radij.h"
#include "openswan/ipsec_life.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_eroute.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"

#include "openswan/ipsec_sa.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_ipe4.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"

#ifdef CONFIG_KLIPS_IPCOMP
#include "openswan/ipcomp.h"
#endif /* CONFIG_KLIPS_IPCOMP */

#include <pfkeyv2.h>
#include <pfkey.h>

#include "openswan/ipsec_proto.h"


enum ipsec_life_alive
ipsec_lifetime_check(struct ipsec_lifetime64 *il64,
		     const char *lifename,
		     const char *saname,
		     enum ipsec_life_type ilt,
		     enum ipsec_direction idir,
		     struct ipsec_sa *ips)
{
	__u64 count;
	const char *dir;

	if(saname == NULL) {
		saname = "unknown-SA";
	}

	if(idir == ipsec_incoming) {
		dir = "incoming";
	} else {
		dir = "outgoing";
	}
		

	if(ilt == ipsec_life_timebased) {
		count = jiffies/HZ - il64->ipl_count;
	} else {
		count = il64->ipl_count;
	}

	if(il64->ipl_hard &&
	   (count > il64->ipl_hard)) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_lifetime_check: "
			    "hard %s lifetime of SA:<%s%s%s> %s has been reached, SA expired, "
			    "%s packet dropped.\n",
			    lifename,
			    IPS_XFORM_NAME(ips),
			    saname,
			    dir);

		pfkey_expire(ips, 1);
		return ipsec_life_harddied;
	}

	if(il64->ipl_soft &&
	   (count > il64->ipl_soft)) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_lifetime_check: "
			    "soft %s lifetime of SA:<%s%s%s> %s has been reached, SA expiring, "
			    "soft expire message sent up, %s packet still processed.\n",
			    lifename,
			    IPS_XFORM_NAME(ips),
			    saname,
			    dir);

		if(ips->ips_state != SADB_SASTATE_DYING) {
			pfkey_expire(ips, 0);
		}
		ips->ips_state = SADB_SASTATE_DYING;

		return ipsec_life_softdied;
	}
	return ipsec_life_okay;
}


/*
 * This function takes a buffer (with length), a lifetime name and type,
 * and formats a string to represent the current values of the lifetime.
 * 
 * It returns the number of bytes that the format took (or would take,
 * if the buffer were large enough: snprintf semantics).
 * This is used in /proc routines and in debug output.
 */
int
ipsec_lifetime_format(char *buffer,
		      int   buflen,
		      char *lifename,
		      enum ipsec_life_type timebaselife,
		      struct ipsec_lifetime64 *lifetime)
{
	int len = 0;
	__u64 count;

	if(timebaselife == ipsec_life_timebased) {
		count = jiffies/HZ - lifetime->ipl_count;
	} else {
		count = lifetime->ipl_count;
	}

	if(lifetime->ipl_count > 1 || 
	   lifetime->ipl_soft      ||
	   lifetime->ipl_hard) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)) 
		len = ipsec_snprintf(buffer, buflen,
			       "%s(%Lu,%Lu,%Lu)",
			       lifename,
			       count,
			       lifetime->ipl_soft,
			       lifetime->ipl_hard);
#else /* XXX high 32 bits are not displayed */
		len = ipsec_snprintf(buffer, buflen,
				"%s(%lu,%lu,%lu)",
				lifename,
				(unsigned long)count,
				(unsigned long)lifetime->ipl_soft,
				(unsigned long)lifetime->ipl_hard);
#endif
	}

	return len;
}

void
ipsec_lifetime_update_hard(struct ipsec_lifetime64 *lifetime,
			  __u64 newvalue)
{
	if(newvalue &&
	   (!lifetime->ipl_hard ||
	    (newvalue < lifetime->ipl_hard))) {
		lifetime->ipl_hard = newvalue;

		if(!lifetime->ipl_soft &&
		   (lifetime->ipl_hard < lifetime->ipl_soft)) {
			lifetime->ipl_soft = lifetime->ipl_hard;
		}
	}
}	

void
ipsec_lifetime_update_soft(struct ipsec_lifetime64 *lifetime,
			  __u64 newvalue)
{
	if(newvalue &&
	   (!lifetime->ipl_soft ||
	    (newvalue < lifetime->ipl_soft))) {
		lifetime->ipl_soft = newvalue;

		if(lifetime->ipl_hard &&
		   (lifetime->ipl_hard < lifetime->ipl_soft)) {
			lifetime->ipl_soft = lifetime->ipl_hard;
		}
	}
}

	
/*
 * $Log: ipsec_life.c,v $
 * Revision 1.13  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.12  2004/04/23 20:44:35  ken
 * Update comments
 *
 * Revision 1.11  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.10  2004/03/30 11:03:10  paul
 * two more occurances of snprintf, found by Sam from a users oops msg.
 *
 * Revision 1.9  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.8.4.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.8  2003/02/06 02:00:10  rgb
 * Fixed incorrect debugging text label
 *
 * Revision 1.7  2002/05/23 07:16:26  rgb
 * Fixed absolute/relative reference to lifetime count printout.
 *
 * Revision 1.6  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.5  2002/04/24 07:36:28  mcr
 * Moved from ./klips/net/ipsec/ipsec_life.c,v
 *
 * Revision 1.4  2002/01/29 17:17:55  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.3  2002/01/29 02:13:17  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.2  2001/11/26 09:16:14  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.1.2.1  2001/09/25 02:25:57  mcr
 * 	lifetime structure created and common functions created.
 *
 * Local variables:
 * c-file-style: "linux"
 * End:
 *
 */
