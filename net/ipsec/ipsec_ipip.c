/*
 * processing code for IPIP
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

char ipsec_ipip_c_version[] = "RCSID $Id: ipsec_ipip.c,v 1.3.2.2 2005/11/27 21:41:03 paul Exp $";
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
#include "openswan/ipsec_ipip.h"
#include "openswan/ipsec_param.h"

#include "openswan/ipsec_proto.h"

enum ipsec_xmit_value
ipsec_xmit_ipip_setup(struct ipsec_xmit_state *ixs)
{
  ixs->iph->version  = 4;

  switch(sysctl_ipsec_tos) {
  case 0:
#ifdef NET_21
    ixs->iph->tos = ixs->skb->nh.iph->tos;
#else /* NET_21 */
    ixs->iph->tos = ixs->skb->ip_hdr->tos;
#endif /* NET_21 */
    break;
  case 1:
    ixs->iph->tos = 0;
    break;
  default:
    break;
  }
  ixs->iph->ttl      = SYSCTL_IPSEC_DEFAULT_TTL;
  ixs->iph->frag_off = 0;
  ixs->iph->saddr    = ((struct sockaddr_in*)(ixs->ipsp->ips_addr_s))->sin_addr.s_addr;
  ixs->iph->daddr    = ((struct sockaddr_in*)(ixs->ipsp->ips_addr_d))->sin_addr.s_addr;
  ixs->iph->protocol = IPPROTO_IPIP;
  ixs->iph->ihl      = sizeof(struct iphdr) >> 2;
  
  KLIPS_IP_SELECT_IDENT(ixs->iph, ixs->skb);
  
  ixs->newdst = (__u32)ixs->iph->daddr;
  ixs->newsrc = (__u32)ixs->iph->saddr;
  
#ifdef NET_21
  ixs->skb->h.ipiph = ixs->skb->nh.iph;
#endif /* NET_21 */
  return IPSEC_XMIT_OK;
}

struct xform_functions ipip_xform_funcs[]={
  {	rcv_checks:         NULL,
	rcv_setup_auth:     NULL,
	rcv_calc_auth:      NULL,
	rcv_decrypt:        NULL,

	xmit_setup:         ipsec_xmit_ipip_setup,
	xmit_headroom:      sizeof(struct iphdr),
	xmit_needtailroom:  0,
  },
};







