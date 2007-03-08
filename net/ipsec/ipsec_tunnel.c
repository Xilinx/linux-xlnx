/*
 * IPSEC Tunneling code. Heavily based on drivers/net/new_tunnel.c
 * Copyright (C) 1996, 1997  John Ioannidis.
 * Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003  Richard Guy Briggs.
 * 
 * OCF/receive state machine written by
 * David McCullough <dmccullough@cyberguard.com>
 * Copyright (C) 2004-2005 Intel Corporation.  All Rights Reserved.
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

char ipsec_tunnel_c_version[] = "RCSID $Id: ipsec_tunnel.c,v 1.232.2.4 2006/03/28 20:58:19 ken Exp $";

#define __NO_VERSION__
#include <linux/module.h>
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>	/* for CONFIG_IP_FORWARD */
#endif
#include <linux/version.h>
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

#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>

#include <linux/netdevice.h>   /* struct device, struct net_device_stats, dev_queue_xmit() and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/skbuff.h>

#include <openswan.h>

#ifdef NET_21
# include <linux/in6.h>
# define ip_chk_addr inet_addr_type
# define IS_MYADDR RTN_LOCAL
# include <net/dst.h>
# undef dev_kfree_skb
# define dev_kfree_skb(a,b) kfree_skb(a)
# define PHYSDEV_TYPE
#endif /* NET_21 */

#include <net/icmp.h>		/* icmp_send() */
#include <net/ip.h>
#include <net/arp.h>
#ifdef NETDEV_23
# include <linux/netfilter_ipv4.h>
#endif /* NETDEV_23 */

#include <linux/if_arp.h>
#include <net/arp.h>

#include "openswan/ipsec_kversion.h"
#include "openswan/radij.h"
#include "openswan/ipsec_life.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_eroute.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_sa.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_xmit.h"
#include "openswan/ipsec_ipe4.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"
#include "openswan/ipsec_kern24.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#include "openswan/ipsec_proto.h"
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
#include <linux/udp.h>
#endif

static __u32 zeroes[64];

#ifdef CONFIG_KLIPS_DEBUG
int debug_tunnel = 0;
#endif /* CONFIG_KLIPS_DEBUG */

DEBUG_NO_STATIC int
ipsec_tunnel_open(struct net_device *dev)
{
	struct ipsecpriv *prv = dev->priv;
	
	/*
	 * Can't open until attached.
	 */

	KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
		    "klips_debug:ipsec_tunnel_open: "
		    "dev = %s, prv->dev = %s\n",
		    dev->name, prv->dev?prv->dev->name:"NONE");

	if (prv->dev == NULL)
		return -ENODEV;
	
	KLIPS_INC_USE;
	return 0;
}

DEBUG_NO_STATIC int
ipsec_tunnel_close(struct net_device *dev)
{
	KLIPS_DEC_USE;
	return 0;
}

#ifdef NETDEV_23
static inline int ipsec_tunnel_xmit2(struct sk_buff *skb)
{
#ifdef NETDEV_25	/* 2.6 kernels */
	return dst_output(skb);
#else
	return ip_send(skb);
#endif
}
#endif /* NETDEV_23 */

enum ipsec_xmit_value
ipsec_tunnel_strip_hard_header(struct ipsec_xmit_state *ixs)
{
	/* ixs->physdev->hard_header_len is unreliable and should not be used */
        ixs->hard_header_len = (unsigned char *)(ixs->iph) - ixs->skb->data;

	if(ixs->hard_header_len < 0) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_error:ipsec_xmit_strip_hard_header: "
			    "Negative hard_header_len (%d)?!\n", ixs->hard_header_len);
		ixs->stats->tx_dropped++;
		return IPSEC_XMIT_BADHHLEN;
	}

	/* while ixs->physdev->hard_header_len is unreliable and
	 * should not be trusted, it accurate and required for ATM, GRE and
	 * some other interfaces to work. Thanks to Willy Tarreau 
	 * <willy@w.ods.org>.
	 */
	if(ixs->hard_header_len == 0) { /* no hard header present */
		ixs->hard_header_stripped = 1;
		ixs->hard_header_len = ixs->physdev->hard_header_len;
	}

#ifdef CONFIG_KLIPS_DEBUG
	if (debug_tunnel & DB_TN_XMIT) {
		int i;
		char c;
		
		printk(KERN_INFO "klips_debug:ipsec_xmit_strip_hard_header: "
		       ">>> skb->len=%ld hard_header_len:%d",
		       (unsigned long int)ixs->skb->len, ixs->hard_header_len);
		c = ' ';
		for (i=0; i < ixs->hard_header_len; i++) {
			printk("%c%02x", c, ixs->skb->data[i]);
			c = ':';
		}
		printk(" \n");
	}
#endif /* CONFIG_KLIPS_DEBUG */

	KLIPS_IP_PRINT(debug_tunnel & DB_TN_XMIT, ixs->iph);

	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_strip_hard_header: "
		    "Original head,tailroom: %d,%d\n",
		    skb_headroom(ixs->skb), skb_tailroom(ixs->skb));

	return IPSEC_XMIT_OK;
}

enum ipsec_xmit_value
ipsec_tunnel_SAlookup(struct ipsec_xmit_state *ixs)
{
	unsigned int bypass;

	bypass = FALSE;

	/*
	 * First things first -- look us up in the erouting tables.
	 */
	ixs->matcher.sen_len = sizeof (struct sockaddr_encap);
	ixs->matcher.sen_family = AF_ENCAP;
	ixs->matcher.sen_type = SENT_IP4;
	ixs->matcher.sen_ip_src.s_addr = ixs->iph->saddr;
	ixs->matcher.sen_ip_dst.s_addr = ixs->iph->daddr;
	ixs->matcher.sen_proto = ixs->iph->protocol;
	ipsec_extract_ports(ixs->iph, &ixs->matcher);

	/*
	 * The spinlock is to prevent any other process from accessing or deleting
	 * the eroute while we are using and updating it.
	 */
	spin_lock(&eroute_lock);
	
	ixs->eroute = ipsec_findroute(&ixs->matcher);

	if(ixs->iph->protocol == IPPROTO_UDP) {
		struct udphdr *t = NULL;

		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:udp port check: "
			    "fragoff: %d len: %d>%ld \n",
			    ntohs(ixs->iph->frag_off) & IP_OFFSET,
			    (ixs->skb->len - ixs->hard_header_len),
                            (unsigned long int) ((ixs->iph->ihl << 2) + sizeof(struct udphdr)));
		
		if((ntohs(ixs->iph->frag_off) & IP_OFFSET) == 0 &&
		   ((ixs->skb->len - ixs->hard_header_len) >=
		    ((ixs->iph->ihl << 2) + sizeof(struct udphdr))))
		{
			t =((struct udphdr*)((caddr_t)ixs->iph+(ixs->iph->ihl<<2)));
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:udp port in packet: "
				    "port %d -> %d\n",
				    ntohs(t->source), ntohs(t->dest));
		}

		ixs->sport=0; ixs->dport=0;

		if(ixs->skb->sk) {
#ifdef NET_26
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
			ixs->sport = ntohs(inet_sk(ixs->skb->sk)->sport);
			ixs->dport = ntohs(inet_sk(ixs->skb->sk)->dport);
#else
			struct udp_sock *us;
			
			us = (struct udp_sock *)ixs->skb->sk;

			ixs->sport = ntohs(us->inet.sport);
			ixs->dport = ntohs(us->inet.dport);
#endif
#else
			ixs->sport = ntohs(ixs->skb->sk->sport);
			ixs->dport = ntohs(ixs->skb->sk->dport);
#endif

		} 

		if(t != NULL) {
			if(ixs->sport == 0) {
				ixs->sport = ntohs(t->source);
			}
			if(ixs->dport == 0) {
				ixs->dport = ntohs(t->dest);
			}
		}
	}

	/*
	 * practically identical to above, but let's be careful about
	 * tcp vs udp headers
	 */
	if(ixs->iph->protocol == IPPROTO_TCP) {
		struct tcphdr *t = NULL;

		if((ntohs(ixs->iph->frag_off) & IP_OFFSET) == 0 &&
		   ((ixs->skb->len - ixs->hard_header_len) >=
		    ((ixs->iph->ihl << 2) + sizeof(struct tcphdr)))) {
			t =((struct tcphdr*)((caddr_t)ixs->iph+(ixs->iph->ihl<<2)));
		}

		ixs->sport=0; ixs->dport=0;

		if(ixs->skb->sk) {
#ifdef NET_26
#ifdef HAVE_INET_SK_SPORT
			ixs->sport = ntohs(inet_sk(ixs->skb->sk)->sport);
			ixs->dport = ntohs(inet_sk(ixs->skb->sk)->dport);
#else
			struct tcp_tw_bucket *tw;
			
			tw = (struct tcp_tw_bucket *)ixs->skb->sk;

			ixs->sport = ntohs(tw->tw_sport);
			ixs->dport = ntohs(tw->tw_dport);
#endif
#else
			ixs->sport = ntohs(ixs->skb->sk->sport);
			ixs->dport = ntohs(ixs->skb->sk->dport);
#endif
		} 

		if(t != NULL) {
			if(ixs->sport == 0) {
				ixs->sport = ntohs(t->source);
			}
			if(ixs->dport == 0) {
				ixs->dport = ntohs(t->dest);
			}
		}
	}
	
	/* default to a %drop eroute */
	ixs->outgoing_said.proto = IPPROTO_INT;
	ixs->outgoing_said.spi = htonl(SPI_DROP);
	ixs->outgoing_said.dst.u.v4.sin_addr.s_addr = INADDR_ANY;
	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
		    "klips_debug:ipsec_xmit_SAlookup: "
		    "checking for local udp/500 IKE packet "
		    "saddr=%x, er=0p%p, daddr=%x, er_dst=%x, proto=%d sport=%d dport=%d\n",
		    ntohl((unsigned int)ixs->iph->saddr),
		    ixs->eroute,
		    ntohl((unsigned int)ixs->iph->daddr),
		    ixs->eroute ? ntohl((unsigned int)ixs->eroute->er_said.dst.u.v4.sin_addr.s_addr) : 0,
		    ixs->iph->protocol,
		    ixs->sport,
		    ixs->dport); 

	/*
	 * cheat for now...are we udp/500? If so, let it through
	 * without interference since it is most likely an IKE packet.
	 */

	if (ip_chk_addr((unsigned long)ixs->iph->saddr) == IS_MYADDR
	    && (ixs->eroute==NULL
		|| ixs->iph->daddr == ixs->eroute->er_said.dst.u.v4.sin_addr.s_addr
		|| INADDR_ANY == ixs->eroute->er_said.dst.u.v4.sin_addr.s_addr)
	    && (ixs->iph->protocol == IPPROTO_UDP &&
		(ixs->sport == 500 || ixs->sport == 4500))) {
		/* Whatever the eroute, this is an IKE message 
		 * from us (i.e. not being forwarded).
		 * Furthermore, if there is a tunnel eroute,
		 * the destination is the peer for this eroute.
		 * So %pass the packet: modify the default %drop.
		 */

		ixs->outgoing_said.spi = htonl(SPI_PASS);
		if(!(ixs->skb->sk) && ((ntohs(ixs->iph->frag_off) & IP_MF) != 0)) {
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_SAlookup: "
				    "local UDP/500 (probably IKE) passthrough: base fragment, rest of fragments will probably get filtered.\n");
		}
		bypass = TRUE;
	}

#ifdef KLIPS_EXCEPT_DNS53
	/*
	 *
	 * if we are udp/53 or tcp/53, also let it through a %trap or %hold,
	 * since it is DNS, but *also* follow the %trap.
	 * 
	 * we do not do this for tunnels, only %trap's and %hold's.
	 *
	 */

	if (ip_chk_addr((unsigned long)ixs->iph->saddr) == IS_MYADDR
	    && (ixs->eroute==NULL
		|| ixs->iph->daddr == ixs->eroute->er_said.dst.u.v4.sin_addr.s_addr
		|| INADDR_ANY == ixs->eroute->er_said.dst.u.v4.sin_addr.s_addr)
	    && ((ixs->iph->protocol == IPPROTO_UDP
		 || ixs->iph->protocol == IPPROTO_TCP)
		&& ixs->dport == 53)) {
		
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_SAlookup: "
			    "possible DNS packet\n");

		if(ixs->eroute)
		{
			if(ixs->eroute->er_said.spi == htonl(SPI_TRAP)
			   || ixs->eroute->er_said.spi == htonl(SPI_HOLD))
			{
				ixs->outgoing_said.spi = htonl(SPI_PASSTRAP);
				bypass = TRUE;
			}
		}
		else
		{
			ixs->outgoing_said.spi = htonl(SPI_PASSTRAP);
			bypass = TRUE;
		}
				
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_SAlookup: "
			    "bypass = %d\n", bypass);

		if(bypass
		   && !(ixs->skb->sk)
		   && ((ntohs(ixs->iph->frag_off) & IP_MF) != 0))
		{
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_SAlookup: "
				    "local port 53 (probably DNS) passthrough:"
				    "base fragment, rest of fragments will "
				    "probably get filtered.\n");
		}
	}
#endif

	if (bypass==FALSE && ixs->eroute) {
		ixs->eroute->er_count++;
		ixs->eroute->er_lasttime = jiffies/HZ;
		if(ixs->eroute->er_said.proto==IPPROTO_INT
		   && ixs->eroute->er_said.spi==htonl(SPI_HOLD))
		{
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_SAlookup: "
				    "shunt SA of HOLD: skb stored in HOLD.\n");
			if(ixs->eroute->er_last != NULL) {
				kfree_skb(ixs->eroute->er_last);
			}
			ixs->eroute->er_last = ixs->skb;
			ixs->skb = NULL;
			ixs->stats->tx_dropped++;
			spin_unlock(&eroute_lock);
			return IPSEC_XMIT_STOLEN;
		}
		ixs->outgoing_said = ixs->eroute->er_said;
		ixs->eroute_pid = ixs->eroute->er_pid;

		/* Copy of the ident for the TRAP/TRAPSUBNET eroutes */
		if(ixs->outgoing_said.proto==IPPROTO_INT
		   && (ixs->outgoing_said.spi==htonl(SPI_TRAP)
		       || (ixs->outgoing_said.spi==htonl(SPI_TRAPSUBNET)))) {
			int len;
			
			ixs->ips.ips_ident_s.type = ixs->eroute->er_ident_s.type;
			ixs->ips.ips_ident_s.id = ixs->eroute->er_ident_s.id;
			ixs->ips.ips_ident_s.len = ixs->eroute->er_ident_s.len;
			if (ixs->ips.ips_ident_s.len)
			{
				len = ixs->ips.ips_ident_s.len * IPSEC_PFKEYv2_ALIGN - sizeof(struct sadb_ident);
				KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
					    "klips_debug:ipsec_xmit_SAlookup: "
					    "allocating %d bytes for ident_s shunt SA of HOLD: skb stored in HOLD.\n",
					    len);
				if ((ixs->ips.ips_ident_s.data = kmalloc(len, GFP_ATOMIC)) == NULL) {
					printk(KERN_WARNING "klips_debug:ipsec_xmit_SAlookup: "
					       "Failed, tried to allocate %d bytes for source ident.\n", 
					       len);
					ixs->stats->tx_dropped++;
					spin_unlock(&eroute_lock);
					return IPSEC_XMIT_ERRMEMALLOC;
				}
				memcpy(ixs->ips.ips_ident_s.data, ixs->eroute->er_ident_s.data, len);
			}
			ixs->ips.ips_ident_d.type = ixs->eroute->er_ident_d.type;
			ixs->ips.ips_ident_d.id = ixs->eroute->er_ident_d.id;
			ixs->ips.ips_ident_d.len = ixs->eroute->er_ident_d.len;
			if (ixs->ips.ips_ident_d.len)
			{
				len = ixs->ips.ips_ident_d.len * IPSEC_PFKEYv2_ALIGN - sizeof(struct sadb_ident);
				KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
					    "klips_debug:ipsec_xmit_SAlookup: "
					    "allocating %d bytes for ident_d shunt SA of HOLD: skb stored in HOLD.\n",
					    len);
				if ((ixs->ips.ips_ident_d.data = kmalloc(len, GFP_ATOMIC)) == NULL) {
					printk(KERN_WARNING "klips_debug:ipsec_xmit_SAlookup: "
					       "Failed, tried to allocate %d bytes for dest ident.\n", 
					       len);
					ixs->stats->tx_dropped++;
					spin_unlock(&eroute_lock);
					return IPSEC_XMIT_ERRMEMALLOC;
				}
				memcpy(ixs->ips.ips_ident_d.data, ixs->eroute->er_ident_d.data, len);
			}
		}
	}

	spin_unlock(&eroute_lock);
	return IPSEC_XMIT_OK;
}


enum ipsec_xmit_value
ipsec_tunnel_restore_hard_header(struct ipsec_xmit_state*ixs)
{
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_restore_hard_header: "
		    "After recursive xforms -- head,tailroom: %d,%d\n",
		    skb_headroom(ixs->skb),
		    skb_tailroom(ixs->skb));

	if(ixs->saved_header) {
		if(skb_headroom(ixs->skb) < ixs->hard_header_len) {
			printk(KERN_WARNING
			       "klips_error:ipsec_xmit_restore_hard_header: "
			       "tried to skb_push hhlen=%d, %d available.  This should never happen, please report.\n",
			       ixs->hard_header_len,
			       skb_headroom(ixs->skb));
			ixs->stats->tx_errors++;
			return IPSEC_XMIT_PUSHPULLERR;

		}
		skb_push(ixs->skb, ixs->hard_header_len);
		{
			int i;
			for (i = 0; i < ixs->hard_header_len; i++) {
				ixs->skb->data[i] = ixs->saved_header[i];
			}
		}
	}
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	if (ixs->natt_type && ixs->natt_head) {
		struct iphdr *ipp = ixs->skb->nh.iph;
		struct udphdr *udp;
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "encapsuling packet into UDP (NAT-Traversal) (%d %d)\n",
			    ixs->natt_type, ixs->natt_head);

		ixs->iphlen = ipp->ihl << 2;
		ipp->tot_len =
			htons(ntohs(ipp->tot_len) + ixs->natt_head);
		if(skb_tailroom(ixs->skb) < ixs->natt_head) {
			printk(KERN_WARNING "klips_error:ipsec_tunnel_start_xmit: "
				"tried to skb_put %d, %d available. "
				"This should never happen, please report.\n",
				ixs->natt_head,
				skb_tailroom(ixs->skb));
			ixs->stats->tx_errors++;
			return IPSEC_XMIT_ESPUDP;
		}
		skb_put(ixs->skb, ixs->natt_head);

		udp = (struct udphdr *)((char *)ipp + ixs->iphlen);

		/* move ESP hdr after UDP hdr */
		memmove((void *)((char *)udp + ixs->natt_head),
			(void *)(udp),
			ntohs(ipp->tot_len) - ixs->iphlen - ixs->natt_head);

		/* clear UDP & Non-IKE Markers (if any) */
		memset(udp, 0, ixs->natt_head);

		/* fill UDP with usefull informations ;-) */
		udp->source = htons(ixs->natt_sport);
		udp->dest = htons(ixs->natt_dport);
		udp->len = htons(ntohs(ipp->tot_len) - ixs->iphlen);

		/* set protocol */
		ipp->protocol = IPPROTO_UDP;

		/* fix IP checksum */
		ipp->check = 0;
		ipp->check = ip_fast_csum((unsigned char *)ipp, ipp->ihl);
	}
#endif	
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_restore_hard_header: "
		    "With hard_header, final head,tailroom: %d,%d\n",
		    skb_headroom(ixs->skb),
		    skb_tailroom(ixs->skb));

	return IPSEC_XMIT_OK;
}

enum ipsec_xmit_value
ipsec_tunnel_send(struct ipsec_xmit_state*ixs)
{
#ifdef NETDEV_25
	struct flowi fl;
#endif
  
#ifdef NET_21	/* 2.2 and 2.4 kernels */
	/* new route/dst cache code from James Morris */
	ixs->skb->dev = ixs->physdev;
#ifdef NETDEV_25
	memset (&fl, 0x0, sizeof (struct flowi));
 	fl.oif = ixs->physdev->iflink;
 	fl.nl_u.ip4_u.daddr = ixs->skb->nh.iph->daddr;
 	fl.nl_u.ip4_u.saddr = ixs->pass ? 0 : ixs->skb->nh.iph->saddr;
 	fl.nl_u.ip4_u.tos = RT_TOS(ixs->skb->nh.iph->tos);
 	fl.proto = ixs->skb->nh.iph->protocol;
 	if ((ixs->error = ip_route_output_key(&ixs->route, &fl))) {
#else
	/*skb_orphan(ixs->skb);*/
	if((ixs->error = ip_route_output(&ixs->route,
				    ixs->skb->nh.iph->daddr,
				    ixs->pass ? 0 : ixs->skb->nh.iph->saddr,
				    RT_TOS(ixs->skb->nh.iph->tos),
                                    /* mcr->rgb: should this be 0 instead? */
				    ixs->physdev->iflink))) {
#endif
		ixs->stats->tx_errors++;
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_send: "
			    "ip_route_output failed with error code %d, rt->u.dst.dev=%s, dropped\n",
			    ixs->error,
			    ixs->route->u.dst.dev->name);
		return IPSEC_XMIT_ROUTEERR;
	}
	if(ixs->dev == ixs->route->u.dst.dev) {
		ip_rt_put(ixs->route);
		/* This is recursion, drop it. */
		ixs->stats->tx_errors++;
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_send: "
			    "suspect recursion, dev=rt->u.dst.dev=%s, dropped\n",
			    ixs->dev->name);
		return IPSEC_XMIT_RECURSDETECT;
	}
	dst_release(ixs->skb->dst);
	ixs->skb->dst = &ixs->route->u.dst;
	ixs->stats->tx_bytes += ixs->skb->len;
	if(ixs->skb->len < ixs->skb->nh.raw - ixs->skb->data) {
		ixs->stats->tx_errors++;
		printk(KERN_WARNING
		       "klips_error:ipsec_xmit_send: "
		       "tried to __skb_pull nh-data=%ld, %d available.  This should never happen, please report.\n",
		       (unsigned long)(ixs->skb->nh.raw - ixs->skb->data),
		       ixs->skb->len);
		return IPSEC_XMIT_PUSHPULLERR;
	}
	__skb_pull(ixs->skb, ixs->skb->nh.raw - ixs->skb->data);
#ifdef SKB_RESET_NFCT
	if(!ixs->pass) {
	  nf_conntrack_put(ixs->skb->nfct);
	  ixs->skb->nfct = NULL;
	}
#if defined(CONFIG_NETFILTER_DEBUG) && defined(HAVE_SKB_NF_DEBUG)
	ixs->skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
#endif /* SKB_RESET_NFCT */
	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
		    "klips_debug:ipsec_xmit_send: "
		    "...done, calling ip_send() on device:%s\n",
		    ixs->skb->dev ? ixs->skb->dev->name : "NULL");
	KLIPS_IP_PRINT(debug_tunnel & DB_TN_XMIT, ixs->skb->nh.iph);
#ifdef NETDEV_23	/* 2.4 kernels */
	{
		int err;

		err = NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, ixs->skb, NULL, ixs->route->u.dst.dev,
			      ipsec_tunnel_xmit2);
		if(err != NET_XMIT_SUCCESS && err != NET_XMIT_CN) {
			if(net_ratelimit())
				printk(KERN_ERR
				       "klips_error:ipsec_xmit_send: "
				       "ip_send() failed, err=%d\n", 
				       -err);
			ixs->stats->tx_errors++;
			ixs->stats->tx_aborted_errors++;
			ixs->skb = NULL;
			return IPSEC_XMIT_IPSENDFAILURE;
		}
	}
#else /* NETDEV_23 */	/* 2.2 kernels */
	ip_send(ixs->skb);
#endif /* NETDEV_23 */
#else /* NET_21 */	/* 2.0 kernels */
	ixs->skb->arp = 1;
	/* ISDN/ASYNC PPP from Matjaz Godec. */
	/*	skb->protocol = htons(ETH_P_IP); */
	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
		    "klips_debug:ipsec_xmit_send: "
		    "...done, calling dev_queue_xmit() or ip_fragment().\n");
	IP_SEND(ixs->skb, ixs->physdev);
#endif /* NET_21 */
	ixs->stats->tx_packets++;

	ixs->skb = NULL;
	
	return IPSEC_XMIT_OK;
}

void
ipsec_tunnel_cleanup(struct ipsec_xmit_state*ixs)
{
#if defined(HAS_NETIF_QUEUE) || defined (HAVE_NETIF_QUEUE)
	netif_wake_queue(ixs->dev);
#else /* defined(HAS_NETIF_QUEUE) || defined (HAVE_NETIF_QUEUE) */
	ixs->dev->tbusy = 0;
#endif /* defined(HAS_NETIF_QUEUE) || defined (HAVE_NETIF_QUEUE) */
	if(ixs->saved_header) {
		kfree(ixs->saved_header);
	}
	if(ixs->skb) {
		dev_kfree_skb(ixs->skb, FREE_WRITE);
	}
	if(ixs->oskb) {
		dev_kfree_skb(ixs->oskb, FREE_WRITE);
	}
	if (ixs->ips.ips_ident_s.data) {
		kfree(ixs->ips.ips_ident_s.data);
	}
	if (ixs->ips.ips_ident_d.data) {
		kfree(ixs->ips.ips_ident_d.data);
	}
	kmem_cache_free(ipsec_ixs_cache, ixs);
	atomic_dec(&ipsec_ixs_cnt);
}


/*
 * when encap processing is complete it call this for us to continue
 */

void
ipsec_tunnel_xsm_complete(
	struct ipsec_xmit_state *ixs,
	enum ipsec_xmit_value stat)
{
	if(stat != IPSEC_XMIT_OK) {
		if(stat == IPSEC_XMIT_PASS) {
			goto bypass;
		}
		
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				"klips_debug:ipsec_tunnel_start_xmit: encap_bundle failed: %d\n",
				stat);
		goto cleanup;
	}

	ixs->matcher.sen_ip_src.s_addr = ixs->iph->saddr;
	ixs->matcher.sen_ip_dst.s_addr = ixs->iph->daddr;
	ixs->matcher.sen_proto = ixs->iph->protocol;
	ipsec_extract_ports(ixs->iph, &ixs->matcher);

	spin_lock(&eroute_lock);
	ixs->eroute = ipsec_findroute(&ixs->matcher);
	if(ixs->eroute) {
		ixs->outgoing_said = ixs->eroute->er_said;
		ixs->eroute_pid = ixs->eroute->er_pid;
		ixs->eroute->er_count++;
		ixs->eroute->er_lasttime = jiffies/HZ;
	}
	spin_unlock(&eroute_lock);

	KLIPS_PRINT((debug_tunnel & DB_TN_XMIT) &&
			/* ((ixs->orgdst != ixs->newdst) || (ixs->orgsrc != ixs->newsrc)) */
			(ixs->orgedst != ixs->outgoing_said.dst.u.v4.sin_addr.s_addr) &&
			ixs->outgoing_said.dst.u.v4.sin_addr.s_addr &&
			ixs->eroute,
			"klips_debug:ipsec_tunnel_start_xmit: "
			"We are recursing here.\n");

	if (/*((ixs->orgdst != ixs->newdst) || (ixs->orgsrc != ixs->newsrc))*/
			(ixs->orgedst != ixs->outgoing_said.dst.u.v4.sin_addr.s_addr) &&
			ixs->outgoing_said.dst.u.v4.sin_addr.s_addr &&
			ixs->eroute) {
		ipsec_xsm(ixs);
		return;
	}

	stat = ipsec_tunnel_restore_hard_header(ixs);
	if(stat != IPSEC_XMIT_OK) {
		goto cleanup;
	}

bypass:
	stat = ipsec_tunnel_send(ixs);

cleanup:
	ipsec_tunnel_cleanup(ixs);
}


/*
 *	This function assumes it is being called from dev_queue_xmit()
 *	and that skb is filled properly by that function.
 */
int
ipsec_tunnel_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ipsec_xmit_state *ixs = NULL;
	enum ipsec_xmit_value stat;

	if (atomic_read(&ipsec_ixs_cnt) >= ipsec_ixs_max)
		return -ENOMEM;
	ixs = kmem_cache_alloc(ipsec_ixs_cache, GFP_ATOMIC);
	if (ixs == NULL)
		return -ENOMEM;
	atomic_inc(&ipsec_ixs_cnt);
#if 0 /* optimised to only clear the required bits */
	memset((caddr_t)ixs, 0, sizeof(*ixs));
#else
	ixs->pass = 0;
	ixs->state = 0;
	ixs->next_state = 0;
	ixs->ipsp = NULL;
	ixs->ipsq = NULL;
	ixs->sa_len = 0;
	ixs->stats = NULL;
	ixs->ips.ips_ident_s.data = NULL;
	ixs->ips.ips_ident_d.data = NULL;
	ixs->outgoing_said.proto = 0;
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	ixs->natt_type = 0, ixs->natt_head = 0;
	ixs->natt_sport = 0, ixs->natt_dport = 0;
#endif
	ixs->tot_headroom = 0;
	ixs->tot_tailroom = 0;
	ixs->eroute = NULL;
	ixs->hard_header_stripped = 0;
	ixs->hard_header_len = 0;
	ixs->cur_mtu = 0; /* FIXME: can we do something better ? */

	ixs->oskb = NULL;
	ixs->saved_header = NULL;	/* saved copy of the hard header */
	ixs->route = NULL;
#endif /* memset */
	ixs->dev = dev;
	ixs->skb = skb;

	stat = ipsec_xmit_sanity_check_dev(ixs);
	if(stat != IPSEC_XMIT_OK) {
		goto cleanup;
	}

	stat = ipsec_xmit_sanity_check_skb(ixs);
	if(stat != IPSEC_XMIT_OK) {
		goto cleanup;
	}

	stat = ipsec_tunnel_strip_hard_header(ixs);
	if(stat != IPSEC_XMIT_OK) {
		goto cleanup;
	}

	stat = ipsec_tunnel_SAlookup(ixs);
	if(stat != IPSEC_XMIT_OK) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_tunnel_start_xmit: SAlookup failed: %d\n",
			    stat);
		goto cleanup;
	}
	
	ixs->innersrc = ixs->iph->saddr;

	ixs->xsm_complete = ipsec_tunnel_xsm_complete;

	ipsec_xsm(ixs);
	return 0;

 cleanup:
	ipsec_tunnel_cleanup(ixs);

	return 0;
}

DEBUG_NO_STATIC struct net_device_stats *
ipsec_tunnel_get_stats(struct net_device *dev)
{
	return &(((struct ipsecpriv *)(dev->priv))->mystats);
}

/*
 * Revectored calls.
 * For each of these calls, a field exists in our private structure.
 */

DEBUG_NO_STATIC int
ipsec_tunnel_hard_header(struct sk_buff *skb, struct net_device *dev,
	unsigned short type, void *daddr, void *saddr, unsigned len)
{
	struct ipsecpriv *prv = dev->priv;
	struct net_device *tmp;
	int ret;
	struct net_device_stats *stats;	/* This device's statistics */
	
	if(skb == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "no skb...\n");
		return -ENODATA;
	}

	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "no device...\n");
		return -ENODEV;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel_hard_header: "
		    "skb->dev=%s dev=%s.\n",
		    skb->dev ? skb->dev->name : "NULL",
		    dev->name);
	
	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "no private space associated with dev=%s\n",
			    dev->name ? dev->name : "NULL");
		return -ENODEV;
	}

	stats = (struct net_device_stats *) &(prv->mystats);

	if(prv->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "no physical device associated with dev=%s\n",
			    dev->name ? dev->name : "NULL");
		stats->tx_dropped++;
		return -ENODEV;
	}

	/* check if we have to send a IPv6 packet. It might be a Router
	   Solicitation, where the building of the packet happens in
	   reverse order:
	   1. ll hdr,
	   2. IPv6 hdr,
	   3. ICMPv6 hdr
	   -> skb->nh.raw is still uninitialized when this function is
	   called!!  If this is no IPv6 packet, we can print debugging
	   messages, otherwise we skip all debugging messages and just
	   build the ll header */
	if(type != ETH_P_IPV6) {
		/* execute this only, if we don't have to build the
		   header for a IPv6 packet */
		if(!prv->hard_header) {
			KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
				    "klips_debug:ipsec_tunnel_hard_header: "
				    "physical device has been detached, packet dropped 0p%p->0p%p len=%d type=%d dev=%s->NULL ",
				    saddr,
				    daddr,
				    len,
				    type,
				    dev->name);
#ifdef NET_21
			KLIPS_PRINTMORE(debug_tunnel & DB_TN_REVEC,
					"ip=%08x->%08x\n",
					(__u32)ntohl(skb->nh.iph->saddr),
					(__u32)ntohl(skb->nh.iph->daddr) );
#else /* NET_21 */
			KLIPS_PRINTMORE(debug_tunnel & DB_TN_REVEC,
					"ip=%08x->%08x\n",
					(__u32)ntohl(skb->ip_hdr->saddr),
					(__u32)ntohl(skb->ip_hdr->daddr) );
#endif /* NET_21 */
			stats->tx_dropped++;
			return -ENODEV;
		}
		
#define da ((struct net_device *)(prv->dev))->dev_addr
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "Revectored 0p%p->0p%p len=%d type=%d dev=%s->%s dev_addr=%02x:%02x:%02x:%02x:%02x:%02x ",
			    saddr,
			    daddr,
			    len,
			    type,
			    dev->name,
			    prv->dev->name,
			    da[0], da[1], da[2], da[3], da[4], da[5]);
#ifdef NET_21
		KLIPS_PRINTMORE(debug_tunnel & DB_TN_REVEC,
			    "ip=%08x->%08x\n",
			    (__u32)ntohl(skb->nh.iph->saddr),
			    (__u32)ntohl(skb->nh.iph->daddr) );
#else /* NET_21 */
		KLIPS_PRINTMORE(debug_tunnel & DB_TN_REVEC,
			    "ip=%08x->%08x\n",
			    (__u32)ntohl(skb->ip_hdr->saddr),
			    (__u32)ntohl(skb->ip_hdr->daddr) );
#endif /* NET_21 */
	} else {
		KLIPS_PRINT(debug_tunnel,
			    "klips_debug:ipsec_tunnel_hard_header: "
			    "is IPv6 packet, skip debugging messages, only revector and build linklocal header.\n");
	}                                                                       
	tmp = skb->dev;
	skb->dev = prv->dev;
	ret = prv->hard_header(skb, prv->dev, type, (void *)daddr, (void *)saddr, len);
	skb->dev = tmp;
	return ret;
}

DEBUG_NO_STATIC int
#ifdef NET_21
ipsec_tunnel_rebuild_header(struct sk_buff *skb)
#else /* NET_21 */
ipsec_tunnel_rebuild_header(void *buff, struct net_device *dev,
			unsigned long raddr, struct sk_buff *skb)
#endif /* NET_21 */
{
	struct ipsecpriv *prv = skb->dev->priv;
	struct net_device *tmp;
	int ret;
	struct net_device_stats *stats;	/* This device's statistics */
	
	if(skb->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_rebuild_header: "
			    "no device...");
		return -ENODEV;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_rebuild_header: "
			    "no private space associated with dev=%s",
			    skb->dev->name ? skb->dev->name : "NULL");
		return -ENODEV;
	}

	stats = (struct net_device_stats *) &(prv->mystats);

	if(prv->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_rebuild_header: "
			    "no physical device associated with dev=%s",
			    skb->dev->name ? skb->dev->name : "NULL");
		stats->tx_dropped++;
		return -ENODEV;
	}

	if(!prv->rebuild_header) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_rebuild_header: "
			    "physical device has been detached, packet dropped skb->dev=%s->NULL ",
			    skb->dev->name);
#ifdef NET_21
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "ip=%08x->%08x\n",
			    (__u32)ntohl(skb->nh.iph->saddr),
			    (__u32)ntohl(skb->nh.iph->daddr) );
#else /* NET_21 */
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "ip=%08x->%08x\n",
			    (__u32)ntohl(skb->ip_hdr->saddr),
			    (__u32)ntohl(skb->ip_hdr->daddr) );
#endif /* NET_21 */
		stats->tx_dropped++;
		return -ENODEV;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel: "
		    "Revectored rebuild_header dev=%s->%s ",
		    skb->dev->name, prv->dev->name);
#ifdef NET_21
	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "ip=%08x->%08x\n",
		    (__u32)ntohl(skb->nh.iph->saddr),
		    (__u32)ntohl(skb->nh.iph->daddr) );
#else /* NET_21 */
	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "ip=%08x->%08x\n",
		    (__u32)ntohl(skb->ip_hdr->saddr),
		    (__u32)ntohl(skb->ip_hdr->daddr) );
#endif /* NET_21 */
	tmp = skb->dev;
	skb->dev = prv->dev;
	
#ifdef NET_21
	ret = prv->rebuild_header(skb);
#else /* NET_21 */
	ret = prv->rebuild_header(buff, prv->dev, raddr, skb);
#endif /* NET_21 */
	skb->dev = tmp;
	return ret;
}

DEBUG_NO_STATIC int
ipsec_tunnel_set_mac_address(struct net_device *dev, void *addr)
{
	struct ipsecpriv *prv = dev->priv;
	
	struct net_device_stats *stats;	/* This device's statistics */
	
	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_set_mac_address: "
			    "no device...");
		return -ENODEV;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_set_mac_address: "
			    "no private space associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		return -ENODEV;
	}

	stats = (struct net_device_stats *) &(prv->mystats);

	if(prv->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_set_mac_address: "
			    "no physical device associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		stats->tx_dropped++;
		return -ENODEV;
	}

	if(!prv->set_mac_address) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_set_mac_address: "
			    "physical device has been detached, cannot set - skb->dev=%s->NULL\n",
			    dev->name);
		return -ENODEV;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel_set_mac_address: "
		    "Revectored dev=%s->%s addr=0p%p\n",
		    dev->name, prv->dev->name, addr);
	return prv->set_mac_address(prv->dev, addr);

}

#ifndef NET_21
DEBUG_NO_STATIC void
ipsec_tunnel_cache_bind(struct hh_cache **hhp, struct net_device *dev,
				 unsigned short htype, __u32 daddr)
{
	struct ipsecpriv *prv = dev->priv;
	
	struct net_device_stats *stats;	/* This device's statistics */
	
	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_bind: "
			    "no device...");
		return;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_bind: "
			    "no private space associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		return;
	}

	stats = (struct net_device_stats *) &(prv->mystats);

	if(prv->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_bind: "
			    "no physical device associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		stats->tx_dropped++;
		return;
	}

	if(!prv->header_cache_bind) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_bind: "
			    "physical device has been detached, cannot set - skb->dev=%s->NULL\n",
			    dev->name);
		stats->tx_dropped++;
		return;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel_cache_bind: "
		    "Revectored \n");
	prv->header_cache_bind(hhp, prv->dev, htype, daddr);
	return;
}
#endif /* !NET_21 */


DEBUG_NO_STATIC void
ipsec_tunnel_cache_update(struct hh_cache *hh, struct net_device *dev, unsigned char *  haddr)
{
	struct ipsecpriv *prv = dev->priv;
	
	struct net_device_stats *stats;	/* This device's statistics */
	
	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_update: "
			    "no device...");
		return;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_update: "
			    "no private space associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		return;
	}

	stats = (struct net_device_stats *) &(prv->mystats);

	if(prv->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_update: "
			    "no physical device associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		stats->tx_dropped++;
		return;
	}

	if(!prv->header_cache_update) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_cache_update: "
			    "physical device has been detached, cannot set - skb->dev=%s->NULL\n",
			    dev->name);
		return;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel: "
		    "Revectored cache_update\n");
	prv->header_cache_update(hh, prv->dev, haddr);
	return;
}

#ifdef NET_21
DEBUG_NO_STATIC int
ipsec_tunnel_neigh_setup(struct neighbour *n)
{
	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel_neigh_setup:\n");

        if (n->nud_state == NUD_NONE) {
                n->ops = &arp_broken_ops;
                n->output = n->ops->output;
        }
        return 0;
}

DEBUG_NO_STATIC int
ipsec_tunnel_neigh_setup_dev(struct net_device *dev, struct neigh_parms *p)
{
	KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
		    "klips_debug:ipsec_tunnel_neigh_setup_dev: "
		    "setting up %s\n",
		    dev ? dev->name : "NULL");

        if (p->tbl->family == AF_INET) {
                p->neigh_setup = ipsec_tunnel_neigh_setup;
                p->ucast_probes = 0;
                p->mcast_probes = 0;
        }
        return 0;
}
#endif /* NET_21 */

/*
 * We call the attach routine to attach another device.
 */

DEBUG_NO_STATIC int
ipsec_tunnel_attach(struct net_device *dev, struct net_device *physdev)
{
        int i;
	struct ipsecpriv *prv = dev->priv;

	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_attach: "
			    "no device...");
		return -ENODEV;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_attach: "
			    "no private space associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		return -ENODATA;
	}

	prv->dev = physdev;
	prv->hard_start_xmit = physdev->hard_start_xmit;
	prv->get_stats = physdev->get_stats;

	if (physdev->hard_header) {
		prv->hard_header = physdev->hard_header;
		dev->hard_header = ipsec_tunnel_hard_header;
	} else
		dev->hard_header = NULL;
	
	if (physdev->rebuild_header) {
		prv->rebuild_header = physdev->rebuild_header;
		dev->rebuild_header = ipsec_tunnel_rebuild_header;
	} else
		dev->rebuild_header = NULL;
	
	if (physdev->set_mac_address) {
		prv->set_mac_address = physdev->set_mac_address;
		dev->set_mac_address = ipsec_tunnel_set_mac_address;
	} else
		dev->set_mac_address = NULL;
	
#ifndef NET_21
	if (physdev->header_cache_bind) {
		prv->header_cache_bind = physdev->header_cache_bind;
		dev->header_cache_bind = ipsec_tunnel_cache_bind;
	} else
		dev->header_cache_bind = NULL;
#endif /* !NET_21 */

	if (physdev->header_cache_update) {
		prv->header_cache_update = physdev->header_cache_update;
		dev->header_cache_update = ipsec_tunnel_cache_update;
	} else
		dev->header_cache_update = NULL;

	dev->hard_header_len = physdev->hard_header_len;

#ifdef NET_21
/*	prv->neigh_setup        = physdev->neigh_setup; */
	dev->neigh_setup        = ipsec_tunnel_neigh_setup_dev;
#endif /* NET_21 */
	dev->mtu = 16260; /* 0xfff0; */ /* dev->mtu; */
	prv->mtu = physdev->mtu;

#ifdef PHYSDEV_TYPE
	dev->type = physdev->type; /* ARPHRD_TUNNEL; */
#endif /*  PHYSDEV_TYPE */

	dev->addr_len = physdev->addr_len;
	for (i=0; i<dev->addr_len; i++) {
		dev->dev_addr[i] = physdev->dev_addr[i];
	}
#ifdef CONFIG_KLIPS_DEBUG
	if(debug_tunnel & DB_TN_INIT) {
		printk(KERN_INFO "klips_debug:ipsec_tunnel_attach: "
		       "physical device %s being attached has HW address: %2x",
		       physdev->name, physdev->dev_addr[0]);
		for (i=1; i < physdev->addr_len; i++) {
			printk(":%02x", physdev->dev_addr[i]);
		}
		printk("\n");
	}
#endif /* CONFIG_KLIPS_DEBUG */

	return 0;
}

/*
 * We call the detach routine to detach the ipsec tunnel from another device.
 */

DEBUG_NO_STATIC int
ipsec_tunnel_detach(struct net_device *dev)
{
        int i;
	struct ipsecpriv *prv = dev->priv;

	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_detach: "
			    "no device...");
		return -ENODEV;
	}

	if(prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_REVEC,
			    "klips_debug:ipsec_tunnel_detach: "
			    "no private space associated with dev=%s",
			    dev->name ? dev->name : "NULL");
		return -ENODATA;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
		    "klips_debug:ipsec_tunnel_detach: "
		    "physical device %s being detached from virtual device %s\n",
		    prv->dev ? prv->dev->name : "NULL",
		    dev->name);

	ipsec_dev_put(prv->dev);
	prv->dev = NULL;
	prv->hard_start_xmit = NULL;
	prv->get_stats = NULL;

	prv->hard_header = NULL;
#ifdef DETACH_AND_DOWN
	dev->hard_header = NULL;
#endif /* DETACH_AND_DOWN */
	
	prv->rebuild_header = NULL;
#ifdef DETACH_AND_DOWN
	dev->rebuild_header = NULL;
#endif /* DETACH_AND_DOWN */
	
	prv->set_mac_address = NULL;
#ifdef DETACH_AND_DOWN
	dev->set_mac_address = NULL;
#endif /* DETACH_AND_DOWN */
	
#ifndef NET_21
	prv->header_cache_bind = NULL;
#ifdef DETACH_AND_DOWN
	dev->header_cache_bind = NULL;
#endif /* DETACH_AND_DOWN */
#endif /* !NET_21 */

	prv->header_cache_update = NULL;
#ifdef DETACH_AND_DOWN
	dev->header_cache_update = NULL;
#endif /* DETACH_AND_DOWN */

#ifdef NET_21
/*	prv->neigh_setup        = NULL; */
#ifdef DETACH_AND_DOWN
	dev->neigh_setup        = NULL;
#endif /* DETACH_AND_DOWN */
#endif /* NET_21 */
	dev->hard_header_len = 0;
#ifdef DETACH_AND_DOWN
	dev->mtu = 0;
#endif /* DETACH_AND_DOWN */
	prv->mtu = 0;
	for (i=0; i<MAX_ADDR_LEN; i++) {
		dev->dev_addr[i] = 0;
	}
	dev->addr_len = 0;
#ifdef PHYSDEV_TYPE
	dev->type = ARPHRD_VOID; /* ARPHRD_TUNNEL; */
#endif /*  PHYSDEV_TYPE */
	
	return 0;
}

/*
 * We call the clear routine to detach all ipsec tunnels from other devices.
 */
DEBUG_NO_STATIC int
ipsec_tunnel_clear(void)
{
	int i;
	struct net_device *ipsecdev = NULL, *prvdev;
	struct ipsecpriv *prv;
	int ret;

	KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
		    "klips_debug:ipsec_tunnel_clear: .\n");

	for(i = 0; i < IPSEC_NUM_IF; i++) {
   	        ipsecdev = ipsecdevices[i];
		if(ipsecdev != NULL) {
			if((prv = (struct ipsecpriv *)(ipsecdev->priv))) {
				prvdev = (struct net_device *)(prv->dev);
				if(prvdev) {
					KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
						    "klips_debug:ipsec_tunnel_clear: "
						    "physical device for device %s is %s\n",
						    ipsecdev->name, prvdev->name);
					if((ret = ipsec_tunnel_detach(ipsecdev))) {
						KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
							    "klips_debug:ipsec_tunnel_clear: "
							    "error %d detatching device %s from device %s.\n",
							    ret, ipsecdev->name, prvdev->name);
						return ret;
					}
				}
			}
		}
	}
	return 0;
}

DEBUG_NO_STATIC int
ipsec_tunnel_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ipsectunnelconf *cf = (struct ipsectunnelconf *)&ifr->ifr_data;
	struct ipsecpriv *prv = dev->priv;
	struct net_device *them; /* physical device */
#ifdef CONFIG_IP_ALIAS
	char *colon;
	char realphysname[IFNAMSIZ];
#endif /* CONFIG_IP_ALIAS */
	
	if(dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_ioctl: "
			    "device not supplied.\n");
		return -ENODEV;
	}

	KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
		    "klips_debug:ipsec_tunnel_ioctl: "
		    "tncfg service call #%d for dev=%s\n",
		    cmd,
		    dev->name ? dev->name : "NULL");
	switch (cmd) {
	/* attach a virtual ipsec? device to a physical device */
	case IPSEC_SET_DEV:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_ioctl: "
			    "calling ipsec_tunnel_attatch...\n");
#ifdef CONFIG_IP_ALIAS
		/* If this is an IP alias interface, get its real physical name */
		strncpy(realphysname, cf->cf_name, IFNAMSIZ);
		realphysname[IFNAMSIZ-1] = 0;
		colon = strchr(realphysname, ':');
		if (colon) *colon = 0;
		them = ipsec_dev_get(realphysname);
#else /* CONFIG_IP_ALIAS */
		them = ipsec_dev_get(cf->cf_name);
#endif /* CONFIG_IP_ALIAS */

		if (them == NULL) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_ioctl: "
				    "physical device %s requested is null\n",
				    cf->cf_name);
			return -ENXIO;
		}
		
#if 0
		if (them->flags & IFF_UP) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_ioctl: "
				    "physical device %s requested is not up.\n",
				    cf->cf_name);
			ipsec_dev_put(them);
			return -ENXIO;
		}
#endif
		
		if (prv && prv->dev) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_ioctl: "
				    "virtual device is already connected to %s.\n",
				    prv->dev->name ? prv->dev->name : "NULL");
			ipsec_dev_put(them);
			return -EBUSY;
		}
		return ipsec_tunnel_attach(dev, them);

	case IPSEC_DEL_DEV:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_ioctl: "
			    "calling ipsec_tunnel_detatch.\n");
		if (! prv->dev) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_ioctl: "
				    "physical device not connected.\n");
			return -ENODEV;
		}
		return ipsec_tunnel_detach(dev);
	       
	case IPSEC_CLR_DEV:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_ioctl: "
			    "calling ipsec_tunnel_clear.\n");
		return ipsec_tunnel_clear();

	default:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_ioctl: "
			    "unknown command %d.\n",
			    cmd);
		return -EOPNOTSUPP;
	}
}

struct net_device *ipsec_get_device(int inst)
{
  struct net_device *ipsec_dev;

  ipsec_dev = NULL;

  if(inst < IPSEC_NUM_IF) {
    ipsec_dev = ipsecdevices[inst];
  }

  return ipsec_dev;
}

int
ipsec_device_event(struct notifier_block *unused, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;
	struct net_device *ipsec_dev;
	struct ipsecpriv *priv;
	int i;

	if (dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "dev=NULL for event type %ld.\n",
			    event);
		return(NOTIFY_DONE);
	}

	/* check for loopback devices */
	if (dev && (dev->flags & IFF_LOOPBACK)) {
		return(NOTIFY_DONE);
	}

	switch (event) {
	case NETDEV_DOWN:
		/* look very carefully at the scope of these compiler
		   directives before changing anything... -- RGB */
#ifdef NET_21
	case NETDEV_UNREGISTER:
		switch (event) {
		case NETDEV_DOWN:
#endif /* NET_21 */
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_device_event: "
				    "NETDEV_DOWN dev=%s flags=%x\n",
				    dev->name,
				    dev->flags);
			if(strncmp(dev->name, "ipsec", strlen("ipsec")) == 0) {
				printk(KERN_CRIT "IPSEC EVENT: KLIPS device %s shut down.\n",
				       dev->name);
			}
#ifdef NET_21
			break;
		case NETDEV_UNREGISTER:
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_device_event: "
				    "NETDEV_UNREGISTER dev=%s flags=%x\n",
				    dev->name,
				    dev->flags);
			break;
		}
#endif /* NET_21 */
		
		/* find the attached physical device and detach it. */
		for(i = 0; i < IPSEC_NUM_IF; i++) {
			ipsec_dev = ipsecdevices[i];

			if(ipsec_dev) {
				priv = (struct ipsecpriv *)(ipsec_dev->priv);
				if(priv) {
					;
					if(((struct net_device *)(priv->dev)) == dev) {
						/* dev_close(ipsec_dev); */
						/* return */ ipsec_tunnel_detach(ipsec_dev);
						KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
							    "klips_debug:ipsec_device_event: "
							    "device '%s' has been detached.\n",
							    ipsec_dev->name);
						break;
					}
				} else {
					KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
						    "klips_debug:ipsec_device_event: "
						    "device '%s' has no private data space!\n",
						    ipsec_dev->name);
				}
			}
		}
		break;
	case NETDEV_UP:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_UP dev=%s\n",
			    dev->name);
		break;
#ifdef NET_21
	case NETDEV_REBOOT:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_REBOOT dev=%s\n",
			    dev->name);
		break;
	case NETDEV_CHANGE:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_CHANGE dev=%s flags=%x\n",
			    dev->name,
			    dev->flags);
		break;
	case NETDEV_REGISTER:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_REGISTER dev=%s\n",
			    dev->name);
		break;
	case NETDEV_CHANGEMTU:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_CHANGEMTU dev=%s to mtu=%d\n",
			    dev->name,
			    dev->mtu);
		break;
	case NETDEV_CHANGEADDR:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_CHANGEADDR dev=%s\n",
			    dev->name);
		break;
	case NETDEV_GOING_DOWN:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_GOING_DOWN dev=%s\n",
			    dev->name);
		break;
	case NETDEV_CHANGENAME:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "NETDEV_CHANGENAME dev=%s\n",
			    dev->name);
		break;
#endif /* NET_21 */
	default:
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_device_event: "
			    "event type %ld unrecognised for dev=%s\n",
			    event,
			    dev->name);
		break;
	}
	return NOTIFY_DONE;
}

/*
 *	Called when an ipsec tunnel device is initialized.
 *	The ipsec tunnel device structure is passed to us.
 */
 
int
ipsec_tunnel_init(struct net_device *dev)
{
	int i;

	KLIPS_PRINT(debug_tunnel,
		    "klips_debug:ipsec_tunnel_init: "
		    "allocating %lu bytes initialising device: %s\n",
		    (unsigned long) sizeof(struct ipsecpriv),
		    dev->name ? dev->name : "NULL");

	/* Add our tunnel functions to the device */
	dev->open		= ipsec_tunnel_open;
	dev->stop		= ipsec_tunnel_close;
	dev->hard_start_xmit	= ipsec_tunnel_start_xmit;
	dev->get_stats		= ipsec_tunnel_get_stats;

	dev->priv = kmalloc(sizeof(struct ipsecpriv), GFP_KERNEL);
	if (dev->priv == NULL)
		return -ENOMEM;
	memset((caddr_t)(dev->priv), 0, sizeof(struct ipsecpriv));

	for(i = 0; i < sizeof(zeroes); i++) {
		((__u8*)(zeroes))[i] = 0;
	}
	
#ifndef NET_21
	/* Initialize the tunnel device structure */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);
#endif /* !NET_21 */

	dev->set_multicast_list = NULL;
	dev->do_ioctl		= ipsec_tunnel_ioctl;
	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= NULL;
#ifndef NET_21
	dev->header_cache_bind 	= NULL;
#endif /* !NET_21 */
	dev->header_cache_update= NULL;

#ifdef NET_21
/*	prv->neigh_setup        = NULL; */
	dev->neigh_setup        = ipsec_tunnel_neigh_setup_dev;
#endif /* NET_21 */
	dev->hard_header_len 	= 0;
	dev->mtu		= 0;
	dev->addr_len		= 0;
	dev->type		= ARPHRD_VOID; /* ARPHRD_TUNNEL; */ /* ARPHRD_ETHER; */
	dev->tx_queue_len	= 10;		/* Small queue */
	memset((caddr_t)(dev->broadcast),0xFF, ETH_ALEN);	/* what if this is not attached to ethernet? */

	/* New-style flags. */
	dev->flags		= IFF_NOARP /* 0 */ /* Petr Novak */;

#if 0
#ifdef NET_21
	dev_init_buffers(dev);
#else /* NET_21 */
	dev->family		= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr 	= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= 4;
#endif /* NET_21 */
#endif

	/* We're done.  Have I forgotten anything? */
	return 0;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*  Module specific interface (but it links with the rest of IPSEC)  */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int
ipsec_tunnel_probe(struct net_device *dev)
{
	ipsec_tunnel_init(dev); 
	return 0;
}

struct net_device *ipsecdevices[IPSEC_NUM_IF];

int 
ipsec_tunnel_init_devices(void)
{
	int i;
	char name[IFNAMSIZ];
	struct net_device *dev_ipsec;
	
	KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
		    "klips_debug:ipsec_tunnel_init_devices: "
		    "creating and registering IPSEC_NUM_IF=%u devices, allocating %lu per device, IFNAMSIZ=%u.\n",
		    IPSEC_NUM_IF,
		    (unsigned long) (sizeof(struct net_device) + IFNAMSIZ),
		    IFNAMSIZ);

	for(i = 0; i < IPSEC_NUM_IF; i++) {
		sprintf(name, IPSEC_DEV_FORMAT, i);
		dev_ipsec = (struct net_device*)kmalloc(sizeof(struct net_device), GFP_KERNEL);
		if (dev_ipsec == NULL) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_init_devices: "
				    "failed to allocate memory for device %s, quitting device init.\n",
				    name);
			return -ENOMEM;
		}
		memset((caddr_t)dev_ipsec, 0, sizeof(struct net_device));
#ifdef NETDEV_23
		strncpy(dev_ipsec->name, name, sizeof(dev_ipsec->name));
#else /* NETDEV_23 */
		dev_ipsec->name = (char*)kmalloc(IFNAMSIZ, GFP_KERNEL);
		if (dev_ipsec->name == NULL) {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_init_devices: "
				    "failed to allocate memory for device %s name, quitting device init.\n",
				    name);
			return -ENOMEM;
		}
		memset((caddr_t)dev_ipsec->name, 0, IFNAMSIZ);
		strncpy(dev_ipsec->name, name, IFNAMSIZ);
#endif /* NETDEV_23 */
		dev_ipsec->next = NULL;
		dev_ipsec->init = &ipsec_tunnel_probe;
		KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
			    "klips_debug:ipsec_tunnel_init_devices: "
			    "registering device %s\n",
			    dev_ipsec->name);

		/* reference and hold the device reference */
		dev_hold(dev_ipsec);
		ipsecdevices[i]=dev_ipsec;

		if (register_netdev(dev_ipsec) != 0) {
			KLIPS_PRINT(1 || debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_init_devices: "
				    "registering device %s failed, quitting device init.\n",
				    dev_ipsec->name);
			return -EIO;
		} else {
			KLIPS_PRINT(debug_tunnel & DB_TN_INIT,
				    "klips_debug:ipsec_tunnel_init_devices: "
				    "registering device %s succeeded, continuing...\n",
				    dev_ipsec->name);
		}
	}
	return 0;
}

/* void */
int
ipsec_tunnel_cleanup_devices(void)
{
	int error = 0;
	int i;
	struct net_device *dev_ipsec;
	
	for(i = 0; i < IPSEC_NUM_IF; i++) {
   	        dev_ipsec = ipsecdevices[i];
		if(dev_ipsec == NULL) {
		  continue;
		}

		/* release reference */
		ipsecdevices[i]=NULL;
		ipsec_dev_put(dev_ipsec);

		KLIPS_PRINT(debug_tunnel, "Unregistering %s (refcnt=%d)\n",
			    dev_ipsec->name,
			    atomic_read(&dev_ipsec->refcnt));
		unregister_netdev(dev_ipsec);
		KLIPS_PRINT(debug_tunnel, "Unregisted %s\n", dev_ipsec->name);
#ifndef NETDEV_23
		kfree(dev_ipsec->name);
		dev_ipsec->name=NULL;
#endif /* !NETDEV_23 */
		kfree(dev_ipsec->priv);
		dev_ipsec->priv=NULL;
	}
	return error;
}

/*
 * $Log: ipsec_tunnel.c,v $
 * Revision 1.232.2.4  2006/03/28 20:58:19  ken
 * Fix for KLIPS on 2.6.16 - need to include <net/arp.h> now
 *
 * Revision 1.232.2.3  2006/02/15 05:14:12  paul
 * 568: uninitialized struct in ipsec_tunnel.c coud break routing under 2.6 kernels
 * ipsec_tunnel_send() calls the entry point function of routing subsystem
 * (ip_route_output_key()) using a not fully initialized struct of type
 * struct flowi.
 * This will cause a failure in routing packets through an ipsec interface
 * when patches for multipath routing from http://www.ssi.bg/~ja/
 * are applied.
 *
 * Revision 1.232.2.2  2005/11/22 04:11:52  ken
 * Backport fixes for 2.6.14 kernels from HEAD
 *
 * Revision 1.232.2.1  2005/09/21 22:57:43  paul
 * pulled up compile fix for 2.6.13
 *
 * Revision 1.232  2005/06/04 16:06:06  mcr
 * 	better patch for nat-t rcv-device code.
 *
 * Revision 1.231  2005/05/21 03:28:51  mcr
 * 	make sure that port-500 hole is used for port-4500 as well.
 *
 * Revision 1.230  2005/05/11 01:42:04  mcr
 * 	removal of debugging showed useless/wrong variables used.
 *
 * Revision 1.229  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.228  2005/01/26 00:50:35  mcr
 * 	adjustment of confusion of CONFIG_IPSEC_NAT vs CONFIG_KLIPS_NAT,
 * 	and make sure that NAT_TRAVERSAL is set as well to match
 * 	userspace compiles of code.
 *
 * Revision 1.227  2004/12/10 21:16:08  ken
 * 64bit fixes from Opteron port of KLIPS 2.6
 *
 * Revision 1.226  2004/12/04 07:11:23  mcr
 * 	fix for snmp SIOCPRIVATE use of snmpd.
 * 	http://bugs.xelerance.com/view.php?id=144
 *
 * Revision 1.225  2004/12/03 21:25:57  mcr
 * 	compile time fixes for running on 2.6.
 * 	still experimental.
 *
 * Revision 1.224  2004/08/14 03:28:24  mcr
 * 	fixed log comment to remove warning about embedded comment.
 *
 * Revision 1.223  2004/08/04 15:57:07  mcr
 * 	moved des .h files to include/des/ *
 * 	included 2.6 protocol specific things
 * 	started at NAT-T support, but it will require a kernel patch.
 *
 * Revision 1.222  2004/08/03 18:19:08  mcr
 * 	in 2.6, use "net_device" instead of #define device->net_device.
 * 	this probably breaks 2.0 compiles.
 *
 * Revision 1.221  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.220  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.219  2004/02/03 03:13:17  mcr
 * 	minor edits for readability, and error reporting.
 *
 * Revision 1.218  2004/01/27 20:29:20  mcr
 * 	fix for unregister_netdev() problem for underlying eth0.
 *
 * Revision 1.217  2003/12/10 01:14:27  mcr
 * 	NAT-traversal patches to KLIPS.
 *
 * Revision 1.216  2003/12/04 23:01:17  mcr
 * 	removed ipsec_netlink.h
 *
 * Revision 1.215  2003/12/04 16:35:16  ken
 * Fix for ATM devices where physdev->hard_header_len *is* correct
 *
 * Revision 1.214  2003/11/25 23:52:37  mcr
 * 	fix typo in patch - ixs-> needed.
 *
 * Revision 1.213  2003/11/24 18:25:49  mcr
 * 	patch from willy@w.ods.org to fix problems with ATM interfaces.
 *
 * Revision 1.212  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.211.2.2  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.211.2.1  2003/09/21 13:59:56  mcr
 * 	pre-liminary X.509 patch - does not yet pass tests.
 *
 * Revision 1.211  2003/09/10 16:46:30  mcr
 * 	patches for 2.4 backport/2.6 existence.
 *
 * Revision 1.210  2003/07/31 22:47:16  mcr
 * 	preliminary (untested by FS-team) 2.5 patches.
 *
 * Revision 1.209  2003/06/22 21:28:43  mcr
 * 	inability to unload module was caused by calls to dev_get
 * 	(ipsec_dev_get), to gather a device from a name. There is
 * 	simply no reason to look the devices up - they should be kept
 * 	in a nice array, ready for use.
 *
 * Revision 1.208  2003/06/22 21:25:07  mcr
 * 	all staticly counted ipsecXXX device support removed.
 *
 * Revision 1.207  2003/04/02 20:15:37  mcr
 * 	fix for PR#204 - do not clear connection tracking info if we
 * 	the packet is being sent in the clear.
 *
 * Revision 1.206  2003/02/12 19:32:51  rgb
 * Refactored file to:
 * ipsec_xmit.c
 * ipsec_xmit.h
 * ipsec_mast.c
 *
 * Revision 1.205  2003/02/06 17:47:00  rgb
 *
 * Remove unused ipsec_tunnel_lock() and ipsec_tunnel_unlock() code.
 * Refactor ipsec_tunnel_start_xmit() further into:
 *         ipsec_xmit_sanity_check_dev()
 *         ipsec_xmit_sanity_check_skb()
 *         ipsec_xmit_strip_hard_header()
 *         ipsec_xmit_restore_hard_header()
 *         ipsec_xmit_send()
 *         ipsec_xmit_cleanup()
 * and start a skeletal ipsec_mast_start_xmit() .
 *
 * Revision 1.204  2003/02/06 06:43:46  rgb
 *
 * Refactor ipsec_tunnel_start_xmit, bringing out:
 *     ipsec_xmit_SAlookup
 *     ipsec_xmit_encap_once
 *     ipsec_xmit_encap_bundle
 *
 * Revision 1.203  2003/02/06 02:21:34  rgb
 *
 * Moved "struct auth_alg" from ipsec_rcv.c to ipsec_ah.h .
 * Changed "struct ah" to "struct ahhdr" and "struct esp" to "struct esphdr".
 * Removed "#ifdef INBOUND_POLICY_CHECK_eroute" dead code.
 *
 * Revision 1.202  2003/01/03 07:38:01  rgb
 *
 * Start to refactor ipsec_tunnel_start_xmit() by putting local variables
 * into struct ipsec_xmit_state and renaming a few variables to give more
 * unique or searchable names.
 *
 * Revision 1.201  2003/01/03 00:31:28  rgb
 *
 * Clean up memset usage, including fixing 2 places where keys were not
 * properly wiped.
 *
 * Revision 1.200  2002/12/06 02:24:02  mcr
 * 	patches for compiling against SUSE 8.1 kernels. Requires
 * 	an additional -DSUSE_LINUX_2_4_19_IS_STUPID.
 *
 * Revision 1.199  2002/10/12 23:11:53  dhr
 *
 * [KenB + DHR] more 64-bit cleanup
 *
 * Revision 1.198  2002/10/05 05:02:58  dhr
 *
 * C labels go on statements
 *
 * Revision 1.197  2002/09/20 05:01:50  rgb
 * Added compiler directive to switch on IP options and fix IP options bug.
 * Make ip->ihl treatment consistent using shifts rather than multiplications.
 * Check for large enough packet before accessing udp header for IKE bypass.
 * Added memory allocation debugging.
 * Fixed potential memory allocation failure-induced oops.
 *
 * Revision 1.196  2002/07/24 18:44:54  rgb
 * Type fiddling to tame ia64 compiler.
 *
 * Revision 1.195  2002/07/23 03:36:07  rgb
 * Fixed 2.2 device initialisation hang.
 *
 * Revision 1.194  2002/05/27 21:40:34  rgb
 * Set unused ipsec devices to ARPHRD_VOID to avoid confusing iproute2.
 * Cleaned up intermediate step to dynamic device allocation.
 *
 * Revision 1.193  2002/05/27 19:31:36  rgb
 * Convert to dynamic ipsec device allocation.
 * Remove final vistiges of tdb references via IPSEC_KLIPS1_COMPAT.
 *
 * Revision 1.192  2002/05/23 07:14:28  rgb
 * Added refcount code.
 * Cleaned up %p variants to 0p%p for test suite cleanup.
 *
 * Revision 1.191  2002/05/14 02:34:37  rgb
 * Change all references to tdb, TDB or Tunnel Descriptor Block to ips,
 * ipsec_sa or ipsec_sa.
 *
 * Revision 1.190  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.189  2002/04/24 07:36:32  mcr
 * Moved from ./klips/net/ipsec/ipsec_tunnel.c,v
 *
 * Revision 1.188  2002/04/20 00:12:25  rgb
 * Added esp IV CBC attack fix, disabled.
 *
 * Revision 1.187  2002/03/23 19:55:17  rgb
 * Fix for 2.2 local IKE fragmentation blackhole.  Still won't work if
 * iptraf or another pcap app is running.
 *
 * Revision 1.186  2002/03/19 03:26:22  rgb
 * Applied DHR's tunnel patch to streamline IKE/specialSA processing.
 *
 * Revision 1.185  2002/02/20 04:13:05  rgb
 * Send back ICMP_PKT_FILTERED upon %reject.
 *
 * Revision 1.184  2002/01/29 17:17:56  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.183  2002/01/29 04:00:53  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.182  2002/01/29 02:13:18  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.181  2002/01/07 20:00:33  rgb
 * Added IKE destination port debugging.
 *
 * Revision 1.180  2001/12/21 21:49:54  rgb
 * Fixed bug as a result of moving IKE bypass above %trap/%hold code.
 *
 * Revision 1.179  2001/12/19 21:08:14  rgb
 * Added transport protocol ports to ipsec_print_ip().
 * Update eroute info for non-SA targets.
 * Added obey DF code disabled.
 * Fixed formatting bugs in ipsec_tunnel_hard_header().
 *
 * Revision 1.178  2001/12/05 09:36:10  rgb
 * Moved the UDP/500 IKE check just above the %hold/%trap checks to avoid
 * IKE packets being stolen by the %hold (and returned to the sending KMd
 * in an ACQUIRE, ironically  ;-).
 *
 * Revision 1.177  2001/11/26 09:23:50  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.170.2.1  2001/09/25 02:28:27  mcr
 * 	struct tdb -> struct ipsec_sa.
 * 	lifetime checks moved to common routines.
 * 	cleaned up includes.
 *
 * Revision 1.170.2.2  2001/10/22 21:08:01  mcr
 * 	include des.h, removed phony prototypes and fixed calling
 * 	conventions to match real prototypes.
 *
 * Revision 1.176  2001/11/09 18:32:31  rgb
 * Added Hans Schultz' fragmented UDP/500 IKE socket port selector.
 *
 * Revision 1.175  2001/11/06 20:47:00  rgb
 * Added Eric Espie's TRAPSUBNET fix, minus spin-lock-bh dabbling.
 *
 * Revision 1.174  2001/11/06 19:50:43  rgb
 * Moved IP_SEND, ICMP_SEND, DEV_QUEUE_XMIT macros to ipsec_tunnel.h for
 * use also by pfkey_v2_parser.c
 *
 * Revision 1.173  2001/10/29 21:53:44  henry
 * tone down the device-down message slightly, until we can make it smarter
 *
 * Revision 1.172  2001/10/26 04:59:37  rgb
 * Added a critical level syslog message if an ipsec device goes down.
 *
 * Revision 1.171  2001/10/18 04:45:21  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.170  2001/09/25 00:09:50  rgb
 * Added NetCelo's TRAPSUBNET code to convert a new type TRAPSUBNET into a
 * HOLD.
 *
 * Revision 1.169  2001/09/15 16:24:05  rgb
 * Re-inject first and last HOLD packet when an eroute REPLACE is done.
 *
 * Revision 1.168  2001/09/14 16:58:37  rgb
 * Added support for storing the first and last packets through a HOLD.
 *
 * Revision 1.167  2001/09/08 21:13:33  rgb
 * Added pfkey ident extension support for ISAKMPd. (NetCelo)
 *
 * Revision 1.166  2001/08/27 19:47:59  rgb
 * Clear tdb  before usage.
 * Added comment: clear IF before calling routing?
 *
 * Revision 1.165  2001/07/03 01:23:53  rgb
 * Send back ICMP iff DF set, !ICMP, offset==0, sysctl_icmp, iph->tot_len >
 * emtu, and don't drop.
 *
 * Revision 1.164  2001/06/14 19:35:10  rgb
 * Update copyright date.
 *
 * Revision 1.163  2001/06/06 20:28:51  rgb
 * Added sanity checks for NULL skbs and devices.
 * Added more debugging output to various functions.
 * Removed redundant dev->priv argument to ipsec_tunnel_{at,de}tach().
 * Renamed ipsec_tunnel_attach() virtual and physical device arguments.
 * Corrected neigh_setup() device function assignment.
 * Keep valid pointers to ipsec_tunnel_*() on detach.
 * Set dev->type to the originally-initiallised value.
 *
 * Revision 1.162  2001/06/01 07:28:04  rgb
 * Added sanity checks for detached devices.  Don't down virtual devices
 * to prevent packets going out in the clear if the detached device comes
 * back up.
 *
 * Revision 1.161  2001/05/30 08:14:52  rgb
 * Removed vestiges of esp-null transforms.
 * NetDev Notifier instrumentation to track down disappearing devices.
 *
 * Revision 1.160  2001/05/29 05:15:12  rgb
 * Added SS' PMTU patch which notifies sender if packet doesn't fit
 * physical MTU (if it wasn't ICMP) and then drops it.
 *
 * Revision 1.159  2001/05/27 06:12:12  rgb
 * Added structures for pid, packet count and last access time to eroute.
 * Added packet count to beginning of /proc/net/ipsec_eroute.
 *
 * Revision 1.158  2001/05/24 05:39:33  rgb
 * Applied source zeroing to 2.2 ip_route_output() call as well to enable
 * PASS eroutes for opportunism.
 *
 * Revision 1.157  2001/05/23 22:35:28  rgb
 * 2.4 source override simplification.
 *
 * Revision 1.156  2001/05/23 21:41:31  rgb
 * Added error return code printing on ip_route_output().
 *
 * Revision 1.155  2001/05/23 05:09:13  rgb
 * Fixed incorrect ip_route_output() failure message.
 *
 * Revision 1.154  2001/05/21 14:53:31  rgb
 * Added debug statement for case when ip_route_output() fails, causing
 * packet to be dropped, but log looked ok.
 *
 * Revision 1.153  2001/05/19 02:37:54  rgb
 * Fixed missing comment termination.
 *
 * Revision 1.152  2001/05/19 02:35:50  rgb
 * Debug code optimisation for non-debug speed.
 * Kernel version compiler define comments.
 * 2.2 and 2.4 kernel ip_send device and ip debug output added.
 *
 * Revision 1.151  2001/05/18 16:17:35  rgb
 * Changed reference from "magic" to "shunt" SAs.
 *
 * Revision 1.150  2001/05/18 16:12:19  rgb
 * Changed UDP/500 bypass test from 3 nested ifs to one anded if.
 *
 * Revision 1.149  2001/05/16 04:39:33  rgb
 * Add default == eroute.dest to IKE bypass conditions for magic eroutes.
 *
 * Revision 1.148  2001/05/05 03:31:41  rgb
 * IP frag debugging updates and enhancements.
 *
 * Revision 1.147  2001/05/03 19:41:40  rgb
 * Added SS' skb_cow fix for 2.4.4.
 *
 * Revision 1.146  2001/04/30 19:28:16  rgb
 * Update for 2.4.4.  ip_select_ident() now has 3 args.
 *
 * Revision 1.145  2001/04/23 14:56:10  rgb
 * Added spin_lock() check to prevent double-locking for multiple
 * transforms and hence kernel lock-ups with SMP kernels.
 *
 * Revision 1.144  2001/04/21 23:04:45  rgb
 * Define out skb->used for 2.4 kernels.
 * Check if soft expire has already been sent before sending another to
 * prevent ACQUIRE flooding.
 *
 * Revision 1.143  2001/03/16 07:37:21  rgb
 * Added comments to all #endifs.
 *
 * Revision 1.142  2001/02/28 05:03:27  rgb
 * Clean up and rationalise startup messages.
 *
 * Revision 1.141  2001/02/27 22:24:54  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.140  2001/02/27 06:40:12  rgb
 * Fixed TRAP->HOLD eroute byte order.
 *
 * Revision 1.139  2001/02/26 20:38:59  rgb
 * Added compiler defines for 2.4.x-specific code.
 *
 * Revision 1.138  2001/02/26 19:57:27  rgb
 * Implement magic SAs %drop, %reject, %trap, %hold, %pass as part
 * of the new SPD and to support opportunistic.
 * Drop sysctl_ipsec_{no_eroute_pass,opportunistic}, replaced by magic SAs.
 *
 * Revision 1.137  2001/02/19 22:29:49  rgb
 * Fixes for presence of active ipv6 segments which share ipsec physical
 * device (gg).
 *
 * Revision 1.136  2001/01/29 22:30:38  rgb
 * Fixed minor acquire debug printing bug.
 *
 * Revision 1.135  2001/01/29 22:19:45  rgb
 * Zero source address for 2.4 bypass route lookup.
 *
 * Revision 1.134  2001/01/23 20:19:49  rgb
 * 2.4 fix to remove removed is_clone member.
 *
 * Revision 1.133  2000/12/09 22:08:35  rgb
 * Fix NET_23 bug, should be NETDEV_23.
 *
 * Revision 1.132  2000/12/01 06:54:50  rgb
 * Fix for new 2.4 IP TTL default variable name.
 *
 * Revision 1.131  2000/11/09 20:52:15  rgb
 * More spinlock shuffling, locking earlier and unlocking later in rcv to
 * include ipcomp and prevent races, renaming some tdb variables that got
 * forgotten, moving some unlocks to include tdbs and adding a missing
 * unlock.  Thanks to Svenning for some of these.
 *
 * Revision 1.130  2000/11/09 20:11:22  rgb
 * Minor shuffles to fix non-standard kernel config option selection.
 *
 * Revision 1.129  2000/11/06 04:32:49  rgb
 * Clean up debug printing.
 * Copy skb->protocol for all kernel versions.
 * Ditched spin_lock_irqsave in favour of spin_lock.
 * Disabled TTL decrement, done in ip_forward.
 * Added debug printing before pfkey_acquire().
 * Fixed printk-deltdbchain-spin_lock races (Svenning).
 * Use defaultTTL for 2.1+ kernels.
 * Add Svenning's adaptive content compression.
 * Fix up debug display arguments.
 *
 * Revision 1.128  2000/09/28 00:58:57  rgb
 * Moved the IKE passthrough check after the eroute lookup so we can pass
 * IKE through intermediate tunnels.
 *
 * Revision 1.127  2000/09/22 17:52:11  rgb
 * Fixed misleading ipcomp debug output.
 *
 * Revision 1.126  2000/09/22 04:22:56  rgb
 * Fixed dumb spi->cpi conversion error.
 *
 * Revision 1.125  2000/09/21 04:34:48  rgb
 * A few debug-specific things should be hidden under
 * CONFIG_IPSEC_DEBUG.(MB)
 * Improved ip_send() error handling.(MB)
 *
 * Revision 1.124  2000/09/21 03:40:58  rgb
 * Added more debugging to try and track down the cpi outward copy problem.
 *
 * Revision 1.123  2000/09/19 07:08:49  rgb
 * Added debugging to outgoing compression report.
 *
 * Revision 1.122  2000/09/18 19:21:26  henry
 * RGB-supplied fix for RH5.2 problem
 *
 * Revision 1.121  2000/09/17 21:05:09  rgb
 * Added tdb to skb_compress call to write in cpi.
 *
 * Revision 1.120  2000/09/17 16:57:16  rgb
 * Added Svenning's patch to remove restriction of ipcomp to innermost
 * transform.
 *
 * Revision 1.119  2000/09/15 11:37:01  rgb
 * Merge in heavily modified Svenning Soerensen's <svenning@post5.tele.dk>
 * IPCOMP zlib deflate code.
 *
 * Revision 1.118  2000/09/15 04:57:16  rgb
 * Moved debug output after sanity check.
 * Added tos copy sysctl.
 *
 * Revision 1.117  2000/09/12 03:22:51  rgb
 * Converted ipsec_icmp, no_eroute_pass, opportunistic and #if0 debugs to
 * sysctl.
 *
 * Revision 1.116  2000/09/08 19:18:19  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Added outgoing opportunistic hook, ifdef'ed out.
 *
 * Revision 1.115  2000/08/30 05:27:29  rgb
 * Removed all the rest of the references to tdb_spi, tdb_proto, tdb_dst.
 * Kill remainder of tdb_xform, tdb_xdata, xformsw.
 *
 * Revision 1.114  2000/08/28 18:15:46  rgb
 * Added MB's nf-debug reset patch.
 *
 * Revision 1.113  2000/08/27 02:26:40  rgb
 * Send all no-eroute-bypass, pluto-bypass and passthrough packets through
 * fragmentation machinery for 2.0, 2.2 and 2.4 kernels.
 *
 * Revision 1.112  2000/08/20 21:37:33  rgb
 * Activated pfkey_expire() calls.
 * Added a hard/soft expiry parameter to pfkey_expire(). (Momchil)
 * Re-arranged the order of soft and hard expiry to conform to RFC2367.
 * Clean up references to CONFIG_IPSEC_PFKEYv2.
 *
 * Revision 1.111  2000/08/01 14:51:51  rgb
 * Removed _all_ remaining traces of DES.
 *
 * Revision 1.110  2000/07/28 14:58:31  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.109  2000/07/28 13:50:54  rgb
 * Changed enet_statistics to net_device_stats and added back compatibility
 * for pre-2.1.19.
 *
 * Revision 1.108  2000/05/16 03:03:11  rgb
 * Updates for 2.3.99pre8 from MB.
 *
 * Revision 1.107  2000/05/10 23:08:21  rgb
 * Print a debug warning about bogus packets received by the outgoing
 * processing machinery only when klipsdebug is not set to none.
 * Comment out the device initialisation informational messages.
 *
 * Revision 1.106  2000/05/10 19:17:14  rgb
 * Define an IP_SEND macro, intending to have all packet passthroughs
 * use fragmentation.  This didn't quite work, but is a step in the
 * right direction.
 * Added buffer allocation debugging statements.
 * Added configure option to shut off no eroute passthrough.
 * Only check usetime against soft and hard limits if the tdb has been
 * used.
 * Cast output of ntohl so that the broken prototype doesn't make our
 * compile noisy.
 *
 * Revision 1.105  2000/03/22 16:15:37  rgb
 * Fixed renaming of dev_get (MB).
 *
 * Revision 1.104  2000/03/16 14:04:15  rgb
 * Indented headers for readability.
 * Fixed debug scope to enable compilation with debug off.
 * Added macros for ip_chk_addr and IS_MYADDR for identifying self.
 *
 * Revision 1.103  2000/03/16 07:11:07  rgb
 * Hardcode PF_KEYv2 support.
 * Fixed bug which allowed UDP/500 packet from another machine
 * through in the clear.
 * Added disabled skb->protocol fix for ISDN/ASYNC PPP from Matjaz Godec.
 *
 * Revision 1.102  2000/03/14 12:26:59  rgb
 * Added skb->nfct support for clearing netfilter conntrack bits (MB).
 *
 * Revision 1.101  2000/02/14 21:05:22  rgb
 * Added MB's netif_queue fix for kernels 2.3.43+.
 *
 * Revision 1.100  2000/01/26 10:04:57  rgb
 * Fixed noisy 2.0 printk arguments.
 *
 * Revision 1.99  2000/01/21 06:16:25  rgb
 * Added sanity checks on skb_push(), skb_pull() to prevent panics.
 * Switched to AF_ENCAP macro.
 * Shortened debug output per packet and re-arranging debug_tunnel
 * bitmap flags, while retaining necessary information to avoid
 * trampling the kernel print ring buffer.
 * Reformatted recursion switch code.
 * Changed all references to tdb_proto to tdb_said.proto for clarity.
 *
 * Revision 1.98  2000/01/13 08:09:31  rgb
 * Shuffled debug_tunnel switches to focus output.
 * Fixed outgoing recursion bug, limiting to recursing only if the remote
 * SG changes and if it is valid, ie. not passthrough.
 * Clarified a number of debug messages.
 *
 * Revision 1.97  2000/01/10 16:37:16  rgb
 * MB support for new ip_select_ident() upon disappearance of
 * ip_id_count in 2.3.36+.
 *
 * Revision 1.96  1999/12/31 14:59:08  rgb
 * MB fix to use new skb_copy_expand in kernel 2.3.35.
 *
 * Revision 1.95  1999/12/29 21:15:44  rgb
 * Fix tncfg to aliased device bug.
 *
 * Revision 1.94  1999/12/22 04:26:06  rgb
 * Converted all 'static' functions to 'DEBUG_NO_STATIC' to enable
 * debugging by providing external labels to all functions with debugging
 * turned on.
 *
 * Revision 1.93  1999/12/13 13:30:14  rgb
 * Changed MTU reports and HW address reporting back to debug only.
 *
 * Revision 1.92  1999/12/07 18:57:56  rgb
 * Fix PFKEY symbol compile error (SADB_*) without pfkey enabled.
 *
 * Revision 1.91  1999/12/01 22:15:36  rgb
 * Add checks for LARVAL and DEAD SAs.
 * Change state of SA from MATURE to DYING when a soft lifetime is
 * reached and print debug warning.
 *
 * Revision 1.90  1999/11/23 23:04:04  rgb
 * Use provided macro ADDRTOA_BUF instead of hardcoded value.
 * Sort out pfkey and freeswan headers, putting them in a library path.
 *
 * Revision 1.89  1999/11/18 18:50:59  rgb
 * Changed all device registrations for static linking to
 * dynamic to reduce the number and size of patches.
 *
 * Revision 1.88  1999/11/18 04:09:19  rgb
 * Replaced all kernel version macros to shorter, readable form.
 *
 * Revision 1.87  1999/11/17 15:53:40  rgb
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 *
 * Revision 1.86  1999/10/16 18:25:37  rgb
 * Moved SA lifetime expiry checks before packet processing.
 * Expire SA on replay counter rollover.
 *
 * Revision 1.85  1999/10/16 04:24:31  rgb
 * Add stats for time since last packet.
 *
 * Revision 1.84  1999/10/16 00:30:47  rgb
 * Added SA lifetime counting.
 *
 * Revision 1.83  1999/10/15 22:15:57  rgb
 * Clean out cruft.
 * Add debugging.
 *
 * Revision 1.82  1999/10/08 18:26:19  rgb
 * Fix 2.0.3x outgoing fragmented packet memory leak.
 *
 * Revision 1.81  1999/10/05 02:38:54  rgb
 * Lower the default mtu of virtual devices to 16260.
 *
 * Revision 1.80  1999/10/03 18:56:41  rgb
 * Spinlock support for 2.3.xx.
 * Don't forget to undo spinlocks on error!
 * Check for valid eroute before copying the structure.
 *
 * Revision 1.79  1999/10/01 15:44:53  rgb
 * Move spinlock header include to 2.1> scope.
 *
 * Revision 1.78  1999/10/01 00:02:43  rgb
 * Added tdb structure locking.
 * Added eroute structure locking.
 *
 * Revision 1.77  1999/09/30 02:52:29  rgb
 * Add Marc Boucher's Copy-On-Write code (same as ipsec_rcv.c).
 *
 * Revision 1.76  1999/09/25 19:31:27  rgb
 * Refine MSS hack to affect SYN, but not SYN+ACK packets.
 *
 * Revision 1.75  1999/09/24 22:52:38  rgb
 * Fix two things broken in 2.0.38 by trying to fix network notifiers.
 *
 * Revision 1.74  1999/09/24 00:30:37  rgb
 * Add test for changed source as well as destination to check for
 * recursion.
 *
 * Revision 1.73  1999/09/23 20:52:24  rgb
 * Add James Morris' MSS hack patch, disabled.
 *
 * Revision 1.72  1999/09/23 20:22:40  rgb
 * Enable, tidy and fix network notifier code.
 *
 * Revision 1.71  1999/09/23 18:09:05  rgb
 * Clean up 2.2.x fragmenting traces.
 * Disable dev->type switching, forcing ARPHRD_TUNNEL.
 *
 * Revision 1.70  1999/09/22 14:14:24  rgb
 * Add sanity checks for revectored calls to prevent calling a downed I/F.
 *
 * Revision 1.69  1999/09/21 15:00:57  rgb
 * Add Marc Boucher's packet size check.
 * Flesh out network device notifier code.
 *
 * Revision 1.68  1999/09/18 11:39:57  rgb
 * Start to add (disabled) netdevice notifier code.
 *
 * Revision 1.67  1999/09/17 23:44:40  rgb
 * Add a comment warning potential code hackers to stay away from mac.raw.
 *
 * Revision 1.66  1999/09/17 18:04:02  rgb
 * Add fix for unpredictable hard_header_len for ISDN folks (thanks MB).
 * Ditch TTL decrement in 2.2 (MB).
 *
 * Revision 1.65  1999/09/15 23:15:35  henry
 * Marc Boucher's PPP fixes
 *
 * Revision 1.64  1999/09/07 13:40:53  rgb
 * Ditch unreliable references to skb->mac.raw.
 *
 * Revision 1.63  1999/08/28 11:33:09  rgb
 * Check for null skb->mac pointer.
 *
 * Revision 1.62  1999/08/28 02:02:30  rgb
 * Add Marc Boucher's fix for properly dealing with skb->sk.
 *
 * Revision 1.61  1999/08/27 05:23:05  rgb
 * Clean up skb->data/raw/nh/h manipulation.
 * Add Marc Boucher's mods to aid tcpdump.
 * Add sanity checks to skb->raw/nh/h pointer copies in skb_copy_expand.
 * Re-order hard_header stripping -- might be able to remove it...
 *
 * Revision 1.60  1999/08/26 20:01:02  rgb
 * Tidy up compiler directives and macros.
 * Re-enable ICMP for tunnels where inner_dst !=  outer_dst.
 * Remove unnecessary skb->dev = physdev assignment affecting 2.2.x.
 *
 * Revision 1.59  1999/08/25 15:44:41  rgb
 * Clean up from 2.2.x instrumenting for compilation under 2.0.36.
 *
 * Revision 1.58  1999/08/25 15:00:54  rgb
 * Add dst cache code for 2.2.xx.
 * Add sanity check for skb packet header pointers.
 * Add/modify debugging instrumentation to *_start_xmit, *_hard_header and
 * *_rebuild_header.
 * Add neigh_* cache code.
 * Change dev->type back to ARPHRD_TUNNEL.
 *
 * Revision 1.57  1999/08/17 21:50:23  rgb
 * Fixed minor debug output bugs.
 * Regrouped error recovery exit code.
 * Added compiler directives to remove unwanted code and symbols.
 * Shut off ICMP messages: to be refined to only send ICMP to remote systems.
 * Add debugging code for output function addresses.
 * Fix minor bug in (possibly unused) header_cache_bind function.
 * Add device neighbour caching code.
 * Change dev->type from ARPHRD_TUNNEL to physdev->type.
 *
 * Revision 1.56  1999/08/03 17:22:56  rgb
 * Debug output clarification using KERN_* macros.  Other inactive changes
 * added.
 *
 * Revision 1.55  1999/08/03 16:58:46  rgb
 * Fix skb_copy_expand size bug.  Was getting incorrect size.
 *
 * Revision 1.54  1999/07/14 19:32:38  rgb
 * Fix oversize packet crash and ssh stalling in 2.2.x kernels.
 *
 * Revision 1.53  1999/06/10 15:44:02  rgb
 * Minor reformatting and clean-up.
 *
 * Revision 1.52  1999/05/09 03:25:36  rgb
 * Fix bug introduced by 2.2 quick-and-dirty patch.
 *
 * Revision 1.51  1999/05/08 21:24:59  rgb
 * Add casting to silence the 2.2.x compile.
 *
 * Revision 1.50  1999/05/05 22:02:32  rgb
 * Add a quick and dirty port to 2.2 kernels by Marc Boucher <marc@mbsi.ca>.
 *
 * Revision 1.49  1999/04/29 15:18:52  rgb
 * Change gettdb parameter to a pointer to reduce stack loading and
 * facilitate parameter sanity checking.
 * Fix undetected bug that might have tried to access a null pointer.
 * Eliminate unnessessary usage of tdb_xform member to further switch
 * away from the transform switch to the algorithm switch.
 * Add return values to init and cleanup functions.
 *
 * Revision 1.48  1999/04/16 15:38:00  rgb
 * Minor rearrangement of freeing code to avoid memory leaks with impossible or
 * rare situations.
 *
 * Revision 1.47  1999/04/15 15:37:25  rgb
 * Forward check changes from POST1_00 branch.
 *
 * Revision 1.32.2.4  1999/04/13 21:00:18  rgb
 * Ditch 'things I wish I had known before...'.
 *
 * Revision 1.32.2.3  1999/04/13 20:34:38  rgb
 * Free skb after fragmentation.
 * Use stats more effectively.
 * Add I/F to mtu notch-down reporting.
 *
 * Revision 1.32.2.2  1999/04/02 04:26:14  rgb
 * Backcheck from HEAD, pre1.0.
 *
 * Revision 1.46  1999/04/11 00:29:00  henry
 * GPL boilerplate
 *
 * Revision 1.45  1999/04/07 15:42:01  rgb
 * Fix mtu/ping bug AGAIN!
 *
 * Revision 1.44  1999/04/06 04:54:27  rgb
 * Fix/Add RCSID Id: and Log: bits to make PHMDs happy.  This includes
 * patch shell fixes.
 *
 * Revision 1.43  1999/04/04 03:57:07  rgb
 * ip_fragment() doesn't free the supplied skb.  Freed.
 *
 * Revision 1.42  1999/04/01 23:27:15  rgb
 * Preload size of virtual mtu.
 *
 * Revision 1.41  1999/04/01 09:31:23  rgb
 * Invert meaning of ICMP PMTUD config option and clarify.
 * Code clean-up.
 *
 * Revision 1.40  1999/04/01 04:37:17  rgb
 * SSH stalling bug fix.
 *
 * Revision 1.39  1999/03/31 23:44:28  rgb
 * Don't send ICMP on DF and frag_off.
 *
 * Revision 1.38  1999/03/31 15:20:10  rgb
 * Quiet down debugging.
 *
 * Revision 1.37  1999/03/31 08:30:31  rgb
 * Add switch to shut off ICMP PMTUD packets.
 *
 * Revision 1.36  1999/03/31 05:44:47  rgb
 * Keep PMTU reduction private.
 *
 * Revision 1.35  1999/03/27 15:13:02  rgb
 * PMTU/fragmentation bug fix.
 *
 * Revision 1.34  1999/03/17 21:19:26  rgb
 * Fix kmalloc nonatomic bug.
 *
 * Revision 1.33  1999/03/17 15:38:42  rgb
 * Code clean-up.
 * ESP_NULL IV bug fix.
 *
 * Revision 1.32  1999/03/01 20:44:25  rgb
 * Code clean-up.
 * Memory leak bug fix.
 *
 * Revision 1.31  1999/02/27 00:02:09  rgb
 * Tune to report the MTU reduction once, rather than after every recursion
 * through the encapsulating code, preventing tcp stream stalling.
 *
 * Revision 1.30  1999/02/24 20:21:01  rgb
 * Reformat debug printk's.
 * Fix recursive encapsulation, dynamic MTU bugs and add debugging code.
 * Clean-up.
 *
 * Revision 1.29  1999/02/22 17:08:14  rgb
 * Fix recursive encapsulation code.
 *
 * Revision 1.28  1999/02/19 18:27:02  rgb
 * Improve DF, fragmentation and PMTU behaviour and add dynamic MTU discovery.
 *
 * Revision 1.27  1999/02/17 16:51:37  rgb
 * Clean out unused cruft.
 * Temporarily tone down volume of debug output.
 * Temporarily shut off fragment rejection.
 * Disabled temporary failed recursive encapsulation loop.
 *
 * Revision 1.26  1999/02/12 21:21:26  rgb
 * Move KLIPS_PRINT to ipsec_netlink.h for accessibility.
 *
 * Revision 1.25  1999/02/11 19:38:27  rgb
 * More clean-up.
 * Add sanity checking for skb_copy_expand() to prevent kernel panics on
 * skb_put() values out of range.
 * Fix head/tailroom calculation causing skb_put() out-of-range values.
 * Fix return values to prevent 'nonatomic alloc_skb' warnings.
 * Allocate new skb iff needed.
 * Added more debug statements.
 * Make headroom depend on structure, not hard-coded values.
 *
 * Revision 1.24  1999/02/10 23:20:33  rgb
 * Shut up annoying 'statement has no effect' compiler warnings with
 * debugging compiled out.
 *
 * Revision 1.23  1999/02/10 22:36:30  rgb
 * Clean-up obsolete, unused and messy code.
 * Converted most IPSEC_DEBUG statements to KLIPS_PRINT macros.
 * Rename ipsec_tunnel_do_xmit to ipsec_tunnel_start_xmit and eliminated
 * original ipsec_tunnel_start_xmit.
 * Send all packet with different inner and outer destinations directly to
 * the attached physical device, rather than back through ip_forward,
 * preventing disappearing routes problems.
 * Do sanity checking before investing too much CPU in allocating new
 * structures.
 * Fail on IP header options: We cannot process them yet.
 * Add some helpful comments.
 * Use virtual device for parameters instead of physical device.
 *
 * Revision 1.22  1999/02/10 03:03:02  rgb
 * Duh.  Fixed the TTL bug: forgot to update the checksum.
 *
 * Revision 1.21  1999/02/09 23:17:53  rgb
 * Add structure members to ipsec_print_ip debug function.
 * Temporarily fix TTL bug preventing tunnel mode from functioning.
 *
 * Revision 1.20  1999/02/09 00:14:25  rgb
 * Add KLIPSPRINT macro.  (Not used yet, though.)
 * Delete old ip_tunnel code (BADCODE).
 * Decrement TTL in outgoing packet.
 * Set TTL on new IPIP_TUNNEL to default, not existing packet TTL.
 * Delete ethernet only feature and fix hard-coded hard_header_len.
 *
 * Revision 1.19  1999/01/29 17:56:22  rgb
 * 64-bit re-fix submitted by Peter Onion.
 *
 * Revision 1.18  1999/01/28 22:43:24  rgb
 * Fixed bug in ipsec_print_ip that caused an OOPS, found by P.Onion.
 *
 * Revision 1.17  1999/01/26 02:08:16  rgb
 * Removed CONFIG_IPSEC_ALGO_SWITCH macro.
 * Removed dead code.
 *
 * Revision 1.16  1999/01/22 06:25:26  rgb
 * Cruft clean-out.
 * Added algorithm switch code.
 * 64-bit clean-up.
 * Passthrough on IPIP protocol, spi 0x0 fix.
 * Enhanced debugging.
 *
 * Revision 1.15  1998/12/01 13:22:04  rgb
 * Added support for debug printing of version info.
 *
 * Revision 1.14  1998/11/30 13:22:55  rgb
 * Rationalised all the klips kernel file headers.  They are much shorter
 * now and won't conflict under RH5.2.
 *
 * Revision 1.13  1998/11/17 21:13:52  rgb
 * Put IKE port bypass debug output in user-switched debug statements.
 *
 * Revision 1.12  1998/11/13 13:20:25  rgb
 * Fixed ntohs bug in udp/500 hole for IKE.
 *
 * Revision 1.11  1998/11/10 08:01:19  rgb
 * Kill tcp/500 hole,  keep udp/500 hole.
 *
 * Revision 1.10  1998/11/09 21:29:26  rgb
 * If no eroute is found, discard packet and incr. tx_error.
 *
 * Revision 1.9  1998/10/31 06:50:00  rgb
 * Add tcp/udp/500 bypass.
 * Fixed up comments in #endif directives.
 *
 * Revision 1.8  1998/10/27 00:34:31  rgb
 * Reformat debug output of IP headers.
 * Newlines added before calls to ipsec_print_ip.
 *
 * Revision 1.7  1998/10/19 14:44:28  rgb
 * Added inclusion of freeswan.h.
 * sa_id structure implemented and used: now includes protocol.
 *
 * Revision 1.6  1998/10/09 04:31:35  rgb
 * Added 'klips_debug' prefix to all klips printk debug statements.
 *
 * Revision 1.5  1998/08/28 03:09:51  rgb
 * Prevent kernel log spam with default route through ipsec.
 *
 * Revision 1.4  1998/08/05 22:23:09  rgb
 * Change setdev return code to ENXIO for a non-existant physical device.
 *
 * Revision 1.3  1998/07/29 20:41:11  rgb
 * Add ipsec_tunnel_clear to clear all tunnel attachments.
 *
 * Revision 1.2  1998/06/25 20:00:33  rgb
 * Clean up #endif comments.
 * Rename dev_ipsec to dev_ipsec0 for consistency.
 * Document ipsec device fields.
 * Make ipsec_tunnel_probe visible from rest of kernel for static linking.
 * Get debugging report for *every* ipsec device initialisation.
 * Comment out redundant code.
 *
 * Revision 1.1  1998/06/18 21:27:50  henry
 * move sources from klips/src to klips/net/ipsec, to keep stupid
 * kernel-build scripts happier in the presence of symlinks
 *
 * Revision 1.8  1998/06/14 23:49:40  rgb
 * Clarify version reporting on module loading.
 *
 * Revision 1.7  1998/05/27 23:19:20  rgb
 * Added version reporting.
 *
 * Revision 1.6  1998/05/18 21:56:23  rgb
 * Clean up for numerical consistency of output and cleaning up debug code.
 *
 * Revision 1.5  1998/05/12 02:44:23  rgb
 * Clarifying 'no e-route to host' message.
 *
 * Revision 1.4  1998/04/30 15:34:35  rgb
 * Enclosed most remaining debugging statements in #ifdef's to make it quieter.
 *
 * Revision 1.3  1998/04/21 21:28:54  rgb
 * Rearrange debug switches to change on the fly debug output from user
 * space.  Only kernel changes checked in at this time.  radij.c was also
 * changed to temporarily remove buggy debugging code in rj_delete causing
 * an OOPS and hence, netlink device open errors.
 *
 * Revision 1.2  1998/04/12 22:03:24  rgb
 * Updated ESP-3DES-HMAC-MD5-96,
 * 	ESP-DES-HMAC-MD5-96,
 * 	AH-HMAC-MD5-96,
 * 	AH-HMAC-SHA1-96 since Henry started freeswan cvs repository
 * from old standards (RFC182[5-9] to new (as of March 1998) drafts.
 *
 * Fixed eroute references in /proc/net/ipsec*.
 *
 * Started to patch module unloading memory leaks in ipsec_netlink and
 * radij tree unloading.
 *
 * Revision 1.1  1998/04/09 03:06:12  henry
 * sources moved up from linux/net/ipsec
 *
 * Revision 1.1.1.1  1998/04/08 05:35:04  henry
 * RGB's ipsec-0.8pre2.tar.gz ipsec-0.8
 *
 * Revision 0.5  1997/06/03 04:24:48  ji
 * Added transport mode.
 * Changed the way routing is done.
 * Lots of bug fixes.
 *
 * Revision 0.4  1997/01/15 01:28:15  ji
 * No changes.
 *
 * Revision 0.3  1996/11/20 14:39:04  ji
 * Minor cleanups.
 * Rationalized debugging code.
 *
 * Revision 0.2  1996/11/02 00:18:33  ji
 * First limited release.
 *
 * Local Variables:
 * c-style: linux
 * End:
 */
