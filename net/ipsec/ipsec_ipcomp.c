/*
 * processing code for IPCOMP
 * Copyright (C) 2003 Michael Richardson <mcr@sandelman.ottawa.on.ca>
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
 */

char ipsec_ipcomp_c_version[] = "RCSID $Id: ipsec_ipcomp.c,v 1.5.2.1 2006/07/07 16:39:58 paul Exp $";
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */

#include "openswan/ipsec_param.h"

#ifdef MALLOC_SLAB
# include <linux/slab.h> /* kmalloc() */
#else /* MALLOC_SLAB */
# include <linux/malloc.h> /* kmalloc() */
#endif /* MALLOC_SLAB */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/netdevice.h>	/* struct device, and other headers */
#include <linux/etherdevice.h>	/* eth_type_trans */
#include <linux/ip.h>		/* struct iphdr */
#include <linux/skbuff.h>
#include <openswan.h>
#ifdef SPINLOCK
# ifdef SPINLOCK_23
#  include <linux/spinlock.h> /* *lock* */
# else /* SPINLOCK_23 */
#  include <asm/spinlock.h> /* *lock* */
# endif /* SPINLOCK_23 */
#endif /* SPINLOCK */

#include <net/ip.h>

#include "openswan/radij.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_sa.h"

#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_rcv.h"
#include "openswan/ipsec_xmit.h"

#include "openswan/ipsec_auth.h"

#ifdef CONFIG_KLIPS_IPCOMP
#include "openswan/ipsec_ipcomp.h"
#endif /* CONFIG_KLIPS_IPCOMP */

#include "openswan/ipsec_proto.h"

#ifdef CONFIG_KLIPS_DEBUG
int debug_ipcomp = 0;
#endif /* CONFIG_KLIPS_DEBUG */


#ifdef CONFIG_KLIPS_IPCOMP
enum ipsec_rcv_value
ipsec_rcv_ipcomp_checks(struct ipsec_rcv_state *irs,
			struct sk_buff *skb)
{
	int ipcompminlen;

	ipcompminlen = sizeof(struct iphdr);

	if(skb->len < (ipcompminlen + sizeof(struct ipcomphdr))) {
		KLIPS_PRINT(debug_rcv & DB_RX_INAU,
			    "klips_debug:ipsec_rcv: "
			    "runt comp packet of skb->len=%d received from %s, dropped.\n",
			    skb->len,
			    irs->ipsaddr_txt);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BADLEN;
	}

	irs->protostuff.ipcompstuff.compp = (struct ipcomphdr *)skb->h.raw;
	irs->said.spi = htonl((__u32)ntohs(irs->protostuff.ipcompstuff.compp->ipcomp_cpi));
	return IPSEC_RCV_OK;
}

enum ipsec_rcv_value
ipsec_rcv_ipcomp_decomp(struct ipsec_rcv_state *irs)
{
	unsigned int flags = 0;
	struct ipsec_sa *ipsp = irs->ipsp;
	struct sk_buff *skb;

	skb=irs->skb;

	ipsec_xmit_dmp("ipcomp", skb->h.raw, skb->len);

	if(ipsp == NULL) {
		return IPSEC_RCV_SAIDNOTFOUND;
	}

	if(sysctl_ipsec_inbound_policy_check &&
	   ((((ntohl(ipsp->ips_said.spi) & 0x0000ffff) != ntohl(irs->said.spi)) &&
	     (ipsp->ips_encalg != ntohl(irs->said.spi))   /* this is a workaround for peer non-compliance with rfc2393 */
		    ))) {
		char sa2[SATOT_BUF];
		size_t sa_len2 = 0;

		sa_len2 = KLIPS_SATOT(debug_rcv, &ipsp->ips_said, 0, sa2, sizeof(sa2));

		KLIPS_PRINT(debug_rcv,
			    "klips_debug:ipsec_rcv: "
			    "Incoming packet with SA(IPCA):%s does not match policy SA(IPCA):%s cpi=%04x cpi->spi=%08x spi=%08x, spi->cpi=%04x for SA grouping, dropped.\n",
			    irs->sa_len ? irs->sa : " (error)",
			    ipsp != NULL ? (sa_len2 ? sa2 : " (error)") : "NULL",
			    ntohs(irs->protostuff.ipcompstuff.compp->ipcomp_cpi),
			    (__u32)ntohl(irs->said.spi),
			    ipsp != NULL ? (__u32)ntohl((ipsp->ips_said.spi)) : 0,
			    ipsp != NULL ? (__u16)(ntohl(ipsp->ips_said.spi) & 0x0000ffff) : 0);
		if(irs->stats) {
			irs->stats->rx_dropped++;
		}
		return IPSEC_RCV_SAIDNOTFOUND;
	}

	ipsp->ips_comp_ratio_cbytes += ntohs(irs->ipp->tot_len);
	irs->next_header = irs->protostuff.ipcompstuff.compp->ipcomp_nh;

	skb = skb_decompress(skb, ipsp, &flags);
	if (!skb || flags) {
		KLIPS_PRINT(debug_rcv,
			    "klips_debug:ipsec_rcv: "
			    "skb_decompress() returned error flags=%x, dropped.\n",
			    flags);
		if (irs->stats) {
			if (flags)
				irs->stats->rx_errors++;
			else
				irs->stats->rx_dropped++;
		}
		return IPSEC_RCV_IPCOMPFAILED;
	}

	/* make sure we update the pointer */
	irs->skb = skb;
	
#ifdef NET_21
	irs->ipp = skb->nh.iph;
#else /* NET_21 */
	irs->ipp = skb->ip_hdr;
#endif /* NET_21 */

	ipsp->ips_comp_ratio_dbytes += ntohs(irs->ipp->tot_len);

	KLIPS_PRINT(debug_rcv,
		    "klips_debug:ipsec_rcv: "
		    "packet decompressed SA(IPCA):%s cpi->spi=%08x spi=%08x, spi->cpi=%04x, nh=%d.\n",
		    irs->sa_len ? irs->sa : " (error)",
		    (__u32)ntohl(irs->said.spi),
		    ipsp != NULL ? (__u32)ntohl((ipsp->ips_said.spi)) : 0,
		    ipsp != NULL ? (__u16)(ntohl(ipsp->ips_said.spi) & 0x0000ffff) : 0,
		    irs->next_header);
	KLIPS_IP_PRINT(debug_rcv & DB_RX_PKTRX, irs->ipp);

	return IPSEC_RCV_OK;
}

enum ipsec_xmit_value
ipsec_xmit_ipcomp_setup(struct ipsec_xmit_state *ixs)
{
  unsigned int flags = 0;
#ifdef CONFIG_KLIPS_DEBUG
  unsigned int old_tot_len = ntohs(ixs->iph->tot_len);
#endif /* CONFIG_KLIPS_DEBUG */

  ixs->ipsp->ips_comp_ratio_dbytes += ntohs(ixs->iph->tot_len);

  ixs->skb = skb_compress(ixs->skb, ixs->ipsp, &flags);

#ifdef NET_21
  ixs->iph = ixs->skb->nh.iph;
#else /* NET_21 */
  ixs->iph = ixs->skb->ip_hdr;
#endif /* NET_21 */
  
  ixs->ipsp->ips_comp_ratio_cbytes += ntohs(ixs->iph->tot_len);
  
#ifdef CONFIG_KLIPS_DEBUG
  if (debug_tunnel & DB_TN_CROUT)
    {
      if (old_tot_len > ntohs(ixs->iph->tot_len))
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "packet shrunk from %d to %d bytes after compression, cpi=%04x (should be from spi=%08x, spi&0xffff=%04x.\n",
		    old_tot_len, ntohs(ixs->iph->tot_len),
		    ntohs(((struct ipcomphdr*)(((char*)ixs->iph) + ((ixs->iph->ihl) << 2)))->ipcomp_cpi),
		    ntohl(ixs->ipsp->ips_said.spi),
		    (__u16)(ntohl(ixs->ipsp->ips_said.spi) & 0x0000ffff));
      else
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "packet did not compress (flags = %d).\n",
		    flags);
    }
#endif /* CONFIG_KLIPS_DEBUG */

  return IPSEC_XMIT_OK;
}

struct xform_functions ipcomp_xform_funcs[]={
	{rcv_checks:  ipsec_rcv_ipcomp_checks,
	 rcv_decrypt: ipsec_rcv_ipcomp_decomp,
	 xmit_setup:  ipsec_xmit_ipcomp_setup,
	 xmit_headroom: 0,
	 xmit_needtailroom: 0,
	},
};

#if 0
/* We probably don't want to install a pure IPCOMP protocol handler, but
   only want to handle IPCOMP if it is encapsulated inside an ESP payload
   (which is already handled) */
#ifdef CONFIG_KLIPS_IPCOMP
struct inet_protocol comp_protocol =
{
	ipsec_rcv,			/* COMP handler		*/
	NULL,				/* COMP error control	*/
#ifdef NETDEV_25
	1,				/* no policy */
#else
	0,				/* next */
	IPPROTO_COMP,			/* protocol ID */
	0,				/* copy */
	NULL,				/* data */
	"COMP"				/* name */
#endif
};
#endif /* CONFIG_KLIPS_IPCOMP */
#endif

#endif /* CONFIG_KLIPS_IPCOMP */
