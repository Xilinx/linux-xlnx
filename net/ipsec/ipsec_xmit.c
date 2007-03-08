/*
 * IPSEC Transmit code.
 * Copyright (C) 1996, 1997  John Ioannidis.
 * Copyright (C) 1998-2003   Richard Guy Briggs.
 * Copyright (C) 2004-2005   Michael Richardson <mcr@xelerance.com>
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

char ipsec_xmit_c_version[] = "RCSID $Id: ipsec_xmit.c,v 1.20.2.6 2006/07/07 22:09:49 paul Exp $";

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

#include <linux/netdevice.h>   /* struct device, struct net_device_stats, dev_queue_xmit() and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/udp.h>         /* struct udphdr */
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <openswan.h>
#ifdef NET_21
# define MSS_HACK_		/* experimental */
# include <linux/in6.h>
# include <net/dst.h>
# define proto_priv cb
#endif /* NET_21 */

#include <net/icmp.h>		/* icmp_send() */
#include <net/ip.h>
#ifdef NETDEV_23
# include <linux/netfilter_ipv4.h>
#endif /* NETDEV_23 */

#include <linux/if_arp.h>
#ifdef MSS_HACK
# include <net/tcp.h>		/* TCP options */
#endif	/* MSS_HACK */

#include "openswan/radij.h"
#include "openswan/ipsec_life.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_eroute.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xmit.h"
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
#include "openswan/ipsec_alg.h"
#include "ipsec_ocf.h"


/* 
 * Stupid kernel API differences in APIs. Not only do some
 * kernels not have ip_select_ident, but some have differing APIs,
 * and SuSE has one with one parameter, but no way of checking to
 * see what is really what.
 */

#ifdef SUSE_LINUX_2_4_19_IS_STUPID
#define KLIPS_IP_SELECT_IDENT(iph, skb) ip_select_ident(iph)
#else

/* simplest case, nothing */
#if !defined(IP_SELECT_IDENT)
#define KLIPS_IP_SELECT_IDENT(iph, skb)  do { iph->id = htons(ip_id_count++); } while(0)
#endif

/* kernels > 2.3.37-ish */
#if defined(IP_SELECT_IDENT) && !defined(IP_SELECT_IDENT_NEW)
#define KLIPS_IP_SELECT_IDENT(iph, skb) ip_select_ident(iph, skb->dst)
#endif

/* kernels > 2.4.2 */
#if defined(IP_SELECT_IDENT) && defined(IP_SELECT_IDENT_NEW)
#define KLIPS_IP_SELECT_IDENT(iph, skb) ip_select_ident(iph, skb->dst, NULL)
#endif

#endif /* SUSE_LINUX_2_4_19_IS_STUPID */



#if defined(CONFIG_KLIPS_AH)
static __u32 zeroes[64];
#endif

#ifdef CONFIG_KLIPS_DEBUG
int sysctl_ipsec_debug_verbose = 0;
#endif /* CONFIG_KLIPS_DEBUG */

int ipsec_xmit_trap_count = 0;
int ipsec_xmit_trap_sendcount = 0;

int sysctl_ipsec_icmp = 0;
int sysctl_ipsec_tos = 0;

#ifdef CONFIG_KLIPS_DEBUG
#define dmp(_x,_y,_z) if(debug_tunnel) ipsec_dmp_block(_x,_y,_z)
#else /* CONFIG_KLIPS_DEBUG */
#define dmp(_x, _y, _z) 
#endif /* CONFIG_KLIPS_DEBUG */


#if !defined(SKB_COPY_EXPAND) || defined(KLIPS_UNIT_TESTS)
/*
 *	This is mostly skbuff.c:skb_copy().
 */
struct sk_buff *
skb_copy_expand(const struct sk_buff *skb, int headroom,
		int tailroom, int priority)
{
	struct sk_buff *n;
	unsigned long offset;

	/*
	 *	Do sanity checking
	 */
	if((headroom < 0) || (tailroom < 0) || ((headroom+tailroom) < 0)) {
		printk(KERN_WARNING
		       "klips_error:skb_copy_expand: "
		       "Illegal negative head,tailroom %d,%d\n",
		       headroom,
		       tailroom);
		return NULL;
	}
	/*
	 *	Allocate the copy buffer
	 */
	 
#ifndef NET_21
	IS_SKB(skb);
#endif /* !NET_21 */


	n=alloc_skb(skb->end - skb->head + headroom + tailroom, priority);

	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:skb_copy_expand: "
		    "allocating %d bytes, head=0p%p data=0p%p tail=0p%p end=0p%p end-head=%d tail-data=%d\n",
		    skb->end - skb->head + headroom + tailroom,
		    skb->head,
		    skb->data,
		    skb->tail,
		    skb->end,
		    skb->end - skb->head,
		    skb->tail - skb->data);

	if(n==NULL)
		return NULL;

	/*
	 *	Shift between the two data areas in bytes
	 */
	 
	/* Set the data pointer */
	skb_reserve(n,skb->data-skb->head+headroom);
	/* Set the tail pointer and length */
	if(skb_tailroom(n) < skb->len) {
		printk(KERN_WARNING "klips_error:skb_copy_expand: "
		       "tried to skb_put %ld, %d available.  This should never happen, please report.\n",
		       (unsigned long int)skb->len,
		       skb_tailroom(n));
		ipsec_kfree_skb(n);
		return NULL;
	}
	skb_put(n,skb->len);

	offset=n->head + headroom - skb->head;

	/* Copy the bytes */
	memcpy(n->head + headroom, skb->head,skb->end-skb->head);
#ifdef NET_21
	n->csum=skb->csum;
	n->priority=skb->priority;
	n->dst=dst_clone(skb->dst);
	if(skb->nh.raw)
		n->nh.raw=skb->nh.raw+offset;
#ifndef NETDEV_23
	n->is_clone=0;
#endif /* NETDEV_23 */
	atomic_set(&n->users, 1);
	n->destructor = NULL;
#ifdef HAVE_SOCK_SECURITY
	n->security=skb->security;
#endif
#else /* NET_21 */
	n->link3=NULL;
	n->when=skb->when;
	if(skb->ip_hdr)
	        n->ip_hdr=(struct iphdr *)(((char *)skb->ip_hdr)+offset);
	n->saddr=skb->saddr;
	n->daddr=skb->daddr;
	n->raddr=skb->raddr;
	n->seq=skb->seq;
	n->end_seq=skb->end_seq;
	n->ack_seq=skb->ack_seq;
	n->acked=skb->acked;
	n->free=1;
	n->arp=skb->arp;
	n->tries=0;
	n->lock=0;
	n->users=0;
#endif /* NET_21 */
	n->protocol=skb->protocol;
	n->list=NULL;
	n->sk=NULL;
	n->dev=skb->dev;
	if(skb->h.raw)
		n->h.raw=skb->h.raw+offset;
	if(skb->mac.raw) 
		n->mac.raw=skb->mac.raw+offset;
	memcpy(n->proto_priv, skb->proto_priv, sizeof(skb->proto_priv));
#ifndef NETDEV_23
	n->used=skb->used;
#endif /* !NETDEV_23 */
	n->pkt_type=skb->pkt_type;
	n->stamp=skb->stamp;
	
#ifndef NET_21
	IS_SKB(n);
#endif /* !NET_21 */
	return n;
}
#endif /* !SKB_COPY_EXPAND */

#ifdef CONFIG_KLIPS_DEBUG
void
ipsec_print_ip(struct iphdr *ip)
{
	char buf[ADDRTOA_BUF];

	printk(KERN_INFO "klips_debug:   IP:");
	printk(" ihl:%d", ip->ihl << 2);
	printk(" ver:%d", ip->version);
	printk(" tos:%d", ip->tos);
	printk(" tlen:%d", ntohs(ip->tot_len));
	printk(" id:%d", ntohs(ip->id));
	printk(" %s%s%sfrag_off:%d",
               ip->frag_off & __constant_htons(IP_CE) ? "CE " : "",
               ip->frag_off & __constant_htons(IP_DF) ? "DF " : "",
               ip->frag_off & __constant_htons(IP_MF) ? "MF " : "",
               (ntohs(ip->frag_off) & IP_OFFSET) << 3);
	printk(" ttl:%d", ip->ttl);
	printk(" proto:%d", ip->protocol);
	if(ip->protocol == IPPROTO_UDP)
		printk(" (UDP)");
	if(ip->protocol == IPPROTO_TCP)
		printk(" (TCP)");
	if(ip->protocol == IPPROTO_ICMP)
		printk(" (ICMP)");
	if(ip->protocol == IPPROTO_ESP)
		printk(" (ESP)");
	if(ip->protocol == IPPROTO_AH)
		printk(" (AH)");
	if(ip->protocol == IPPROTO_COMP)
		printk(" (COMP)");
	printk(" chk:%d", ntohs(ip->check));
	addrtoa(*((struct in_addr*)(&ip->saddr)), 0, buf, sizeof(buf));
	printk(" saddr:%s", buf);
	if(ip->protocol == IPPROTO_UDP)
		printk(":%d",
		       ntohs(((struct udphdr*)((caddr_t)ip + (ip->ihl << 2)))->source));
	if(ip->protocol == IPPROTO_TCP)
		printk(":%d",
		       ntohs(((struct tcphdr*)((caddr_t)ip + (ip->ihl << 2)))->source));
	addrtoa(*((struct in_addr*)(&ip->daddr)), 0, buf, sizeof(buf));
	printk(" daddr:%s", buf);
	if(ip->protocol == IPPROTO_UDP)
		printk(":%d",
		       ntohs(((struct udphdr*)((caddr_t)ip + (ip->ihl << 2)))->dest));
	if(ip->protocol == IPPROTO_TCP)
		printk(":%d",
		       ntohs(((struct tcphdr*)((caddr_t)ip + (ip->ihl << 2)))->dest));
	if(ip->protocol == IPPROTO_ICMP)
		printk(" type:code=%d:%d",
		       ((struct icmphdr*)((caddr_t)ip + (ip->ihl << 2)))->type,
		       ((struct icmphdr*)((caddr_t)ip + (ip->ihl << 2)))->code);
	printk("\n");

	if(sysctl_ipsec_debug_verbose) {
		__u8 *c;
		int len = ntohs(ip->tot_len) - ip->ihl*4;
		
		c = ((__u8*)ip) + ip->ihl*4;
		ipsec_dmp_block("ip_print", c, len);
	}
}
#endif /* CONFIG_KLIPS_DEBUG */

#ifdef MSS_HACK
/*
 * Issues:
 *  1) Fragments arriving in the tunnel should probably be rejected.
 *  2) How does this affect syncookies, mss_cache, dst cache ?
 *  3) Path MTU discovery handling needs to be reviewed.  For example,
 *     if we receive an ICMP 'packet too big' message from an intermediate 
 *     router specifying it's next hop MTU, our stack may process this and
 *     adjust the MSS without taking our AH/ESP overheads into account.
 */

 
/*
 * Recaclulate checksum using differences between changed datum, 
 * borrowed from netfilter.
 */
DEBUG_NO_STATIC u_int16_t 
ipsec_fast_csum(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck)
{
	u_int32_t diffs[] = { oldvalinv, newval };
	return csum_fold(csum_partial((char *)diffs, sizeof(diffs),
	oldcheck^0xFFFF));
}

/*
 * Determine effective MSS.
 *
 * Note that we assume that there is always an MSS option for our own
 * SYN segments, which is mentioned in tcp_syn_build_options(), kernel 2.2.x.
 * This could change, and we should probably parse TCP options instead.
 *
 */
DEBUG_NO_STATIC u_int8_t
ipsec_adjust_mss(struct sk_buff *skb, struct tcphdr *tcph, u_int16_t mtu)
{
	u_int16_t oldmss, newmss;
	u_int32_t *mssp;
	struct sock *sk = skb->sk;
	
	newmss = tcp_sync_mss(sk, mtu);
	printk(KERN_INFO "klips: setting mss to %u\n", newmss);
	mssp = (u_int32_t *)tcph + sizeof(struct tcphdr) / sizeof(u_int32_t);
	oldmss = ntohl(*mssp) & 0x0000FFFF;
	*mssp = htonl((TCPOPT_MSS << 24) | (TCPOLEN_MSS << 16) | newmss);
	tcph->check = ipsec_fast_csum(htons(~oldmss), 
	                              htons(newmss), tcph->check);
	return 1;
}
#endif	/* MSS_HACK */

#ifdef CONFIG_KLIPS_DEBUG
DEBUG_NO_STATIC char *
ipsec_xmit_err(int err)
{
	static char tmp[32];
	switch ((int) err) {
	case IPSEC_XMIT_STOLEN:			return("IPSEC_XMIT_STOLEN");
	case IPSEC_XMIT_PASS:			return("IPSEC_XMIT_PASS");
	case IPSEC_XMIT_OK:				return("IPSEC_XMIT_OK");
	case IPSEC_XMIT_ERRMEMALLOC:	return("IPSEC_XMIT_ERRMEMALLOC");
	case IPSEC_XMIT_ESP_BADALG:		return("IPSEC_XMIT_ESP_BADALG");
	case IPSEC_XMIT_BADPROTO:		return("IPSEC_XMIT_BADPROTO");
	case IPSEC_XMIT_ESP_PUSHPULLERR:return("IPSEC_XMIT_ESP_PUSHPULLERR");
	case IPSEC_XMIT_BADLEN:			return("IPSEC_XMIT_BADLEN");
	case IPSEC_XMIT_AH_BADALG:		return("IPSEC_XMIT_AH_BADALG");
	case IPSEC_XMIT_SAIDNOTFOUND:	return("IPSEC_XMIT_SAIDNOTFOUND");
	case IPSEC_XMIT_SAIDNOTLIVE:	return("IPSEC_XMIT_SAIDNOTLIVE");
	case IPSEC_XMIT_REPLAYROLLED:	return("IPSEC_XMIT_REPLAYROLLED");
	case IPSEC_XMIT_LIFETIMEFAILED:	return("IPSEC_XMIT_LIFETIMEFAILED");
	case IPSEC_XMIT_CANNOTFRAG:		return("IPSEC_XMIT_CANNOTFRAG");
	case IPSEC_XMIT_MSSERR:			return("IPSEC_XMIT_MSSERR");
	case IPSEC_XMIT_ERRSKBALLOC:	return("IPSEC_XMIT_ERRSKBALLOC");
	case IPSEC_XMIT_ENCAPFAIL:		return("IPSEC_XMIT_ENCAPFAIL");
	case IPSEC_XMIT_NODEV:			return("IPSEC_XMIT_NODEV");
	case IPSEC_XMIT_NOPRIVDEV:		return("IPSEC_XMIT_NOPRIVDEV");
	case IPSEC_XMIT_NOPHYSDEV:		return("IPSEC_XMIT_NOPHYSDEV");
	case IPSEC_XMIT_NOSKB:			return("IPSEC_XMIT_NOSKB");
	case IPSEC_XMIT_NOIPV6:			return("IPSEC_XMIT_NOIPV6");
	case IPSEC_XMIT_NOIPOPTIONS:	return("IPSEC_XMIT_NOIPOPTIONS");
	case IPSEC_XMIT_TTLEXPIRED:		return("IPSEC_XMIT_TTLEXPIRED");
	case IPSEC_XMIT_BADHHLEN:		return("IPSEC_XMIT_BADHHLEN");
	case IPSEC_XMIT_PUSHPULLERR:	return("IPSEC_XMIT_PUSHPULLERR");
	case IPSEC_XMIT_ROUTEERR:		return("IPSEC_XMIT_ROUTEERR");
	case IPSEC_XMIT_RECURSDETECT:	return("IPSEC_XMIT_RECURSDETECT");
	case IPSEC_XMIT_IPSENDFAILURE:	return("IPSEC_XMIT_IPSENDFAILURE");
	case IPSEC_XMIT_ESPUDP:			return("IPSEC_XMIT_ESPUDP");
	case IPSEC_XMIT_ESPUDP_BADTYPE:	return("IPSEC_XMIT_ESPUDP_BADTYPE");
	case IPSEC_XMIT_PENDING:		return("IPSEC_XMIT_PENDING");
	}
	snprintf(tmp, sizeof(tmp), "%d", err);
	return tmp;
}
#endif
                                                        
/*
 * Sanity checks
 */
enum ipsec_xmit_value
ipsec_xmit_sanity_check_dev(struct ipsec_xmit_state *ixs)
{

	if (ixs->dev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_error:ipsec_xmit_sanity_check_dev: "
			    "No device associated with skb!\n" );
		return IPSEC_XMIT_NODEV;
	}

	ixs->prv = ixs->dev->priv;
	if (ixs->prv == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_error:ipsec_xmit_sanity_check_dev: "
			    "Device has no private structure!\n" );
		return 	IPSEC_XMIT_NOPRIVDEV;
	}

	ixs->physdev = ixs->prv->dev;
	if (ixs->physdev == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_error:ipsec_xmit_sanity_check_dev: "
			    "Device is not attached to physical device!\n" );
		return IPSEC_XMIT_NOPHYSDEV;
	}

	ixs->physmtu = ixs->physdev->mtu;
        ixs->cur_mtu = ixs->physdev->mtu;
	ixs->stats = (struct net_device_stats *) &(ixs->prv->mystats);

	return IPSEC_XMIT_OK;
}

enum ipsec_xmit_value
ipsec_xmit_sanity_check_skb(struct ipsec_xmit_state *ixs)
{
	/*
	 *	Return if there is nothing to do.  (Does this ever happen?) XXX
	 */
	if (ixs->skb == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_error:ipsec_xmit_sanity_check_skb: "
			    "Nothing to do!\n" );
		return IPSEC_XMIT_NOSKB;
	}

	/* if skb was cloned (most likely due to a packet sniffer such as
	   tcpdump being momentarily attached to the interface), make
	   a copy of our own to modify */
	if(skb_cloned(ixs->skb)) {
		if
#ifdef SKB_COW_NEW
	       (skb_cow(ixs->skb, skb_headroom(ixs->skb)) != 0)
#else /* SKB_COW_NEW */
	       ((ixs->skb = skb_cow(ixs->skb, skb_headroom(ixs->skb))) == NULL)
#endif /* SKB_COW_NEW */
		{
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_error:ipsec_xmit_sanity_check_skb: "
				    "skb_cow failed to allocate buffer, dropping.\n" );
			ixs->stats->tx_dropped++;
			return IPSEC_XMIT_ERRSKBALLOC;
		}
	}

	ixs->iph = ixs->skb->nh.iph;

	/* sanity check for IP version as we can't handle IPv6 right now */
	if (ixs->iph->version != 4) {
		KLIPS_PRINT(debug_tunnel,
			    "klips_debug:ipsec_xmit_sanity_check_skb: "
			    "found IP Version %d but cannot process other IP versions than v4.\n",
			    ixs->iph->version); /* XXX */
		ixs->stats->tx_dropped++;
		return IPSEC_XMIT_NOIPV6;
	}
	
#if IPSEC_DISALLOW_IPOPTIONS
	if ((ixs->iph->ihl << 2) != sizeof (struct iphdr)) {
		KLIPS_PRINT(debug_tunnel,
			    "klips_debug:ipsec_xmit_sanity_check_skb: "
			    "cannot process IP header options yet.  May be mal-formed packet.\n"); /* XXX */
		ixs->stats->tx_dropped++;
		return IPSEC_XMIT_NOIPOPTIONS;
	}
#endif /* IPSEC_DISALLOW_IPOPTIONS */
	
#ifndef NET_21
	if (ixs->iph->ttl <= 0) {
		/* Tell the sender its packet died... */
		ICMP_SEND(ixs->skb, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL, 0, ixs->physdev);

		KLIPS_PRINT(debug_tunnel, "klips_debug:ipsec_xmit_sanity_check_skb: "
			    "TTL=0, too many hops!\n");
		ixs->stats->tx_dropped++;
		return IPSEC_XMIT_TTLEXPIRED;
	}
#endif /* !NET_21 */
	
	return IPSEC_XMIT_OK;
}


enum ipsec_xmit_value
ipsec_xmit_encap_init(struct ipsec_xmit_state *ixs)
{
	ixs->blocksize = 8;
	ixs->headroom = 0;
	ixs->tailroom = 0;
	ixs->authlen = 0;

#ifdef CONFIG_KLIPS_ALG
	ixs->ixt_e = NULL;
	ixs->ixt_a = NULL;
#endif /* CONFIG_KLIPS_ALG */

	ixs->iphlen = ixs->iph->ihl << 2;
	ixs->pyldsz = ntohs(ixs->iph->tot_len) - ixs->iphlen;
	ixs->sa_len = KLIPS_SATOT(debug_tunnel, &ixs->ipsp->ips_said, 0, ixs->sa_txt, SATOT_BUF);
	KLIPS_PRINT(debug_tunnel & DB_TN_OXFS,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "calling output for <%s%s%s>, SA:%s\n", 
		    IPS_XFORM_NAME(ixs->ipsp),
		    ixs->sa_len ? ixs->sa_txt : " (error)");
	switch(ixs->ipsp->ips_said.proto) {
#ifdef CONFIG_KLIPS_AH
	case IPPROTO_AH:
		ixs->headroom += sizeof(struct ahhdr);
		break;
#endif /* CONFIG_KLIPS_AH */
#ifdef CONFIG_KLIPS_ESP
	case IPPROTO_ESP:
#ifdef CONFIG_KLIPS_OCF
		/*
		 * this needs cleaning up for sure - DM
		 */
		if (ixs->ipsp->ocf_in_use) {
			switch (ixs->ipsp->ips_encalg) {
			case ESP_DES:
			case ESP_3DES:
				ixs->blocksize = 8;
				ixs->headroom += ESP_HEADER_LEN + 8 /* ivsize */;
				break;
			case ESP_AES:
				ixs->blocksize = 16;
				ixs->headroom += ESP_HEADER_LEN + 16 /* ivsize */;
				break;
			default:
				ixs->stats->tx_errors++;
				return IPSEC_XMIT_ESP_BADALG;
			}
		} else
#endif
#ifdef CONFIG_KLIPS_ALG
		if ((ixs->ixt_e=ixs->ipsp->ips_alg_enc)) {
			ixs->blocksize = ixs->ixt_e->ixt_common.ixt_blocksize;
			ixs->headroom += ESP_HEADER_LEN + ixs->ixt_e->ixt_common.ixt_support.ias_ivlen/8;
		} else
#endif /* CONFIG_KLIPS_ALG */
		{
			ixs->stats->tx_errors++;
			return IPSEC_XMIT_ESP_BADALG;
		}
#ifdef CONFIG_KLIPS_OCF
		if (ixs->ipsp->ocf_in_use) {
			switch (ixs->ipsp->ips_authalg) {
			case AH_MD5:
			case AH_SHA:
				ixs->authlen = AHHMAC_HASHLEN;
				break;
			case AH_NONE:
				break;
			}
		} else
#endif /* CONFIG_KLIPS_OCF */
#ifdef CONFIG_KLIPS_ALG
		if ((ixs->ixt_a=ixs->ipsp->ips_alg_auth)) {
			ixs->tailroom += AHHMAC_HASHLEN;
		} else
#endif /* CONFIG_KLIPS_ALG */
		switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		case AH_MD5:
			ixs->authlen = AHHMAC_HASHLEN;
			break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		case AH_SHA:
			ixs->authlen = AHHMAC_HASHLEN;
			break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
		case AH_NONE:
			break;
		default:
			ixs->stats->tx_errors++;
			return IPSEC_XMIT_ESP_BADALG;
		}		
		ixs->tailroom += ixs->blocksize != 1 ?
			((ixs->blocksize - ((ixs->pyldsz + 2) % ixs->blocksize)) % ixs->blocksize) + 2 :
			((4 - ((ixs->pyldsz + 2) % 4)) % 4) + 2;
		ixs->tailroom += ixs->authlen;
		break;
#endif /* !CONFIG_KLIPS_ESP */
#ifdef CONFIG_KLIPS_IPIP
	case IPPROTO_IPIP:
		ixs->headroom += sizeof(struct iphdr);
		ixs->iphlen = sizeof(struct iphdr);
		break;
#endif /* !CONFIG_KLIPS_IPIP */
#ifdef CONFIG_KLIPS_IPCOMP
	case IPPROTO_COMP:
		break;
#endif /* CONFIG_KLIPS_IPCOMP */
	default:
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_BADPROTO;
	}
	
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "pushing %d bytes, putting %d, proto %d.\n", 
		    ixs->headroom, ixs->tailroom, ixs->ipsp->ips_said.proto);
	if(skb_headroom(ixs->skb) < ixs->headroom) {
		printk(KERN_WARNING
		       "klips_error:ipsec_xmit_encap_once: "
		       "tried to skb_push headroom=%d, %d available.  This should never happen, please report.\n",
		       ixs->headroom, skb_headroom(ixs->skb));
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_ESP_PUSHPULLERR;
	}

	ixs->dat = skb_push(ixs->skb, ixs->headroom);
	ixs->ilen = ixs->skb->len - ixs->tailroom;
	if(skb_tailroom(ixs->skb) < ixs->tailroom) {
		printk(KERN_WARNING
		       "klips_error:ipsec_xmit_encap_once: "
		       "tried to skb_put %d, %d available.  This should never happen, please report.\n",
		       ixs->tailroom, skb_tailroom(ixs->skb));
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_ESP_PUSHPULLERR;
	}
	skb_put(ixs->skb, ixs->tailroom);
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "head,tailroom: %d,%d before xform.\n",
		    skb_headroom(ixs->skb), skb_tailroom(ixs->skb));
	ixs->len = ixs->skb->len;
	if(ixs->len > 0xfff0) {
		printk(KERN_WARNING "klips_error:ipsec_xmit_encap_once: "
		       "tot_len (%d) > 65520.  This should never happen, please report.\n",
		       ixs->len);
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_BADLEN;
	}
	memmove((void *)ixs->dat, (void *)(ixs->dat + ixs->headroom), ixs->iphlen);
	ixs->iph = (struct iphdr *)ixs->dat;
	ixs->iph->tot_len = htons(ixs->skb->len);

	return IPSEC_XMIT_OK;
}


/*
 * work out which state to proceed to next
 */

enum ipsec_xmit_value
ipsec_xmit_encap_select(struct ipsec_xmit_state *ixs)
{
	switch (ixs->ipsp->ips_said.proto) {
#ifdef CONFIG_KLIPS_ESP
	case IPPROTO_ESP:
		ixs->next_state = IPSEC_XSM_ESP;
		break;
#endif
#ifdef CONFIG_KLIPS_AH
	case IPPROTO_AH:
		ixs->next_state = IPSEC_XSM_AH;
		break;
#endif
#ifdef CONFIG_KLIPS_IPIP
	case IPPROTO_IPIP:
		ixs->next_state = IPSEC_XSM_IPIP;
		break;
#endif
#ifdef CONFIG_KLIPS_IPCOMP
	case IPPROTO_COMP:
		ixs->next_state = IPSEC_XSM_IPCOMP;
		break;
#endif
	default:
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_BADPROTO;
	}
	return IPSEC_XMIT_OK;
}


#ifdef CONFIG_KLIPS_ESP

enum ipsec_xmit_value
ipsec_xmit_esp(struct ipsec_xmit_state *ixs)
{
	int i;
	unsigned char *pad;
	int padlen = 0;

	ixs->espp = (struct esphdr *)(ixs->dat + ixs->iphlen);
#ifdef NET_21
	ixs->skb->h.raw = (unsigned char*)ixs->espp;
#endif /* NET_21 */
	ixs->espp->esp_spi = ixs->ipsp->ips_said.spi;
	ixs->espp->esp_rpl = htonl(++(ixs->ipsp->ips_replaywin_lastseq));
	
	ixs->idat = ixs->dat + ixs->iphlen + ixs->headroom;
	ixs->ilen = ixs->len - (ixs->iphlen + ixs->headroom + ixs->authlen);

	/* Self-describing padding */
	pad = &ixs->dat[ixs->len - ixs->tailroom];
	padlen = ixs->tailroom - 2 - ixs->authlen;
	for (i = 0; i < padlen; i++) {
		pad[i] = i + 1; 
	}
	ixs->dat[ixs->len - ixs->authlen - 2] = padlen;
	
	ixs->dat[ixs->len - ixs->authlen - 1] = ixs->iph->protocol;
	ixs->iph->protocol = IPPROTO_ESP;
	
#ifdef CONFIG_KLIPS_OCF
	if (ixs->ipsp->ocf_in_use)
		return(ipsec_ocf_xmit(ixs));
#endif

#ifdef CONFIG_KLIPS_ALG
	if (!ixs->ixt_e) {
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_ESP_BADALG;
	}
	
	if(debug_tunnel & DB_TN_ENCAP) {
		dmp("pre-encrypt", ixs->dat, ixs->len);
	}

	/*
	 * Do all operations here:
	 * copy IV->ESP, encrypt, update ips IV
	 *
	 */
	{
		int ret;
		memcpy(ixs->espp->esp_iv, 
			   ixs->ipsp->ips_iv, 
			   ixs->ipsp->ips_iv_size);
		ret=ipsec_alg_esp_encrypt(ixs->ipsp, 
					  ixs->idat, ixs->ilen, ixs->espp->esp_iv,
					  IPSEC_ALG_ENCRYPT);

		prng_bytes(&ipsec_prng,
			   (char *)ixs->ipsp->ips_iv,
			   ixs->ipsp->ips_iv_size);
	} 
	return IPSEC_XMIT_OK;
#else
	return IPSEC_XMIT_ESP_BADALG;
#endif /*  CONFIG_KLIPS_ALG */
}


enum ipsec_xmit_value
ipsec_xmit_esp_ah(struct ipsec_xmit_state *ixs)
{
#if defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1)
	__u8 hash[AH_AMAX];
#endif
#if defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1)
	union {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		MD5_CTX md5;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		SHA1_CTX sha1;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
	} tctx;
#endif /* defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1) */

#ifdef CONFIG_KLIPS_OCF
	if (ixs->ipsp->ocf_in_use) {
		/* we should never be here using OCF */
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_AH_BADALG;
	} else
#endif
#ifdef CONFIG_KLIPS_ALG
	if (ixs->ixt_a) {
		ipsec_alg_sa_esp_hash(ixs->ipsp,
				(caddr_t)ixs->espp, ixs->len - ixs->iphlen - ixs->authlen,
				&(ixs->dat[ixs->len - ixs->authlen]), ixs->authlen);

	} else
#endif /* CONFIG_KLIPS_ALG */
	switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
	case AH_MD5:
		dmp("espp", (char*)ixs->espp, ixs->len - ixs->iphlen - ixs->authlen);
		tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->ictx;
		dmp("ictx", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, (caddr_t)ixs->espp, ixs->len - ixs->iphlen - ixs->authlen);
		dmp("ictx+dat", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Final(hash, &tctx.md5);
		dmp("ictx hash", (char*)&hash, sizeof(hash));
		tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->octx;
		dmp("octx", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, hash, AHMD596_ALEN);
		dmp("octx+hash", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Final(hash, &tctx.md5);
		dmp("octx hash", (char*)&hash, sizeof(hash));
		memcpy(&(ixs->dat[ixs->len - ixs->authlen]), hash, ixs->authlen);
		
		/* paranoid */
		memset((caddr_t)&tctx.md5, 0, sizeof(tctx.md5));
		memset((caddr_t)hash, 0, sizeof(*hash));
		break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
	case AH_SHA:
		tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->ictx;
		SHA1Update(&tctx.sha1, (caddr_t)ixs->espp, ixs->len - ixs->iphlen - ixs->authlen);
		SHA1Final(hash, &tctx.sha1);
		tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->octx;
		SHA1Update(&tctx.sha1, hash, AHSHA196_ALEN);
		SHA1Final(hash, &tctx.sha1);
		memcpy(&(ixs->dat[ixs->len - ixs->authlen]), hash, ixs->authlen);
		
		/* paranoid */
		memset((caddr_t)&tctx.sha1, 0, sizeof(tctx.sha1));
		memset((caddr_t)hash, 0, sizeof(*hash));
		break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
	case AH_NONE:
		break;
	default:
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_AH_BADALG;
	}
	return IPSEC_XMIT_OK;
}

#endif /* CONFIG_KLIPS_ESP */



#ifdef CONFIG_KLIPS_AH

enum ipsec_xmit_value
ipsec_xmit_ah(struct ipsec_xmit_state *ixs)
{
	struct iphdr ipo;
	struct ahhdr *ahp;
#if defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1)
	__u8 hash[AH_AMAX];
#endif
#if defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1)
	union {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		MD5_CTX md5;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		SHA1_CTX sha1;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
	} tctx;
#endif /* defined(CONFIG_KLIPS_AUTH_HMAC_MD5) || defined(CONFIG_KLIPS_AUTH_HMAC_SHA1) */

	ahp = (struct ahhdr *)(ixs->dat + ixs->iphlen);
#ifdef NET_21
	ixs->skb->h.raw = (unsigned char*)ahp;
#endif /* NET_21 */
	ahp->ah_spi = ixs->ipsp->ips_said.spi;
	ahp->ah_rpl = htonl(++(ixs->ipsp->ips_replaywin_lastseq));
	ahp->ah_rv = 0;
	ahp->ah_nh = ixs->iph->protocol;
	ahp->ah_hl = (ixs->headroom >> 2) - sizeof(__u64)/sizeof(__u32);
	ixs->iph->protocol = IPPROTO_AH;
	dmp("ahp", (char*)ahp, sizeof(*ahp));
	
#ifdef CONFIG_KLIPS_OCF
	if (ixs->ipsp->ocf_in_use)
		return(ipsec_ocf_xmit(ixs));
#endif

	ipo = *ixs->iph;
	ipo.tos = 0;
	ipo.frag_off = 0;
	ipo.ttl = 0;
	ipo.check = 0;
	dmp("ipo", (char*)&ipo, sizeof(ipo));
	
	switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
	case AH_MD5:
		tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->ictx;
		dmp("ictx", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, (unsigned char *)&ipo, sizeof (struct iphdr));
		dmp("ictx+ipo", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, (unsigned char *)ahp, ixs->headroom - sizeof(ahp->ah_data));
		dmp("ictx+ahp", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, (unsigned char *)zeroes, AHHMAC_HASHLEN);
		dmp("ictx+zeroes", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5,  ixs->dat + ixs->iphlen + ixs->headroom, ixs->len - ixs->iphlen - ixs->headroom);
		dmp("ictx+dat", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Final(hash, &tctx.md5);
		dmp("ictx hash", (char*)&hash, sizeof(hash));
		tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->octx;
		dmp("octx", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Update(&tctx.md5, hash, AHMD596_ALEN);
		dmp("octx+hash", (char*)&tctx.md5, sizeof(tctx.md5));
		osMD5Final(hash, &tctx.md5);
		dmp("octx hash", (char*)&hash, sizeof(hash));
				
		memcpy(ahp->ah_data, hash, AHHMAC_HASHLEN);
				
		/* paranoid */
		memset((caddr_t)&tctx.md5, 0, sizeof(tctx.md5));
		memset((caddr_t)hash, 0, sizeof(*hash));
		break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
	case AH_SHA:
		tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->ictx;
		SHA1Update(&tctx.sha1, (unsigned char *)&ipo, sizeof (struct iphdr));
		SHA1Update(&tctx.sha1, (unsigned char *)ahp, ixs->headroom - sizeof(ahp->ah_data));
		SHA1Update(&tctx.sha1, (unsigned char *)zeroes, AHHMAC_HASHLEN);
		SHA1Update(&tctx.sha1,  ixs->dat + ixs->iphlen + ixs->headroom, ixs->len - ixs->iphlen - ixs->headroom);
		SHA1Final(hash, &tctx.sha1);
		tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->octx;
		SHA1Update(&tctx.sha1, hash, AHSHA196_ALEN);
		SHA1Final(hash, &tctx.sha1);
				
		memcpy(ahp->ah_data, hash, AHHMAC_HASHLEN);
				
		/* paranoid */
		memset((caddr_t)&tctx.sha1, 0, sizeof(tctx.sha1));
		memset((caddr_t)hash, 0, sizeof(*hash));
		break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
	default:
		ixs->stats->tx_errors++;
		return IPSEC_XMIT_AH_BADALG;
	}
	return IPSEC_XMIT_OK;
}

#endif /* CONFIG_KLIPS_AH */


#ifdef CONFIG_KLIPS_IPIP

enum ipsec_xmit_value
ipsec_xmit_ipip(struct ipsec_xmit_state *ixs)
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

#endif /* CONFIG_KLIPS_IPIP */


#ifdef CONFIG_KLIPS_IPCOMP

enum ipsec_xmit_value
ipsec_xmit_ipcomp(struct ipsec_xmit_state *ixs)
{
#ifdef CONFIG_KLIPS_DEBUG
	unsigned int old_tot_len;
#endif
	int flags = 0;

#ifdef CONFIG_KLIPS_DEBUG
	old_tot_len = ntohs(ixs->iph->tot_len);
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

#endif /* CONFIG_KLIPS_IPCOMP */



/*
 * upon entry to this function, ixs->skb should be setup
 * as follows:
 *
 *   data   = beginning of IP packet   <- differs from ipsec_rcv().
 *   nh.raw = beginning of IP packet.
 *   h.raw  = data after the IP packet.
 *
 */
enum ipsec_xmit_value
ipsec_xmit_cont(struct ipsec_xmit_state *ixs)
{
#ifdef NET_21
	ixs->skb->nh.raw = ixs->skb->data;
#else /* NET_21 */
	ixs->skb->ip_hdr = ixs->skb->h.iph = (struct iphdr *) ixs->skb->data;
#endif /* NET_21 */
	ixs->iph->check = 0;
	ixs->iph->check = ip_fast_csum((unsigned char *)ixs->iph, ixs->iph->ihl);
			
	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
		    "klips_debug:ipsec_xmit_encap_once: "
		    "after <%s%s%s>, SA:%s:\n",
		    IPS_XFORM_NAME(ixs->ipsp),
		    ixs->sa_len ? ixs->sa_txt : " (error)");
	KLIPS_IP_PRINT(debug_tunnel & DB_TN_XMIT, ixs->iph);
 			
	ixs->ipsp->ips_life.ipl_bytes.ipl_count += ixs->len;
	ixs->ipsp->ips_life.ipl_bytes.ipl_last = ixs->len;

	if(!ixs->ipsp->ips_life.ipl_usetime.ipl_count) {
		ixs->ipsp->ips_life.ipl_usetime.ipl_count = jiffies / HZ;
	}
	ixs->ipsp->ips_life.ipl_usetime.ipl_last = jiffies / HZ;
	ixs->ipsp->ips_life.ipl_packets.ipl_count++; 

	ixs->ipsp = ixs->ipsp->ips_onext;

	/*
	 * start again if we have more work to do
	 */
	if (ixs->ipsp)
		ixs->next_state = IPSEC_XSM_ENCAP_INIT;

	return IPSEC_XMIT_OK;
}


/*
 * If the IP packet (iph) is a carrying TCP/UDP, then set the encaps
 * source and destination ports to those from the TCP/UDP header.
 */
void ipsec_extract_ports(struct iphdr * iph, struct sockaddr_encap * er)
{
	struct udphdr *udp;

	switch (iph->protocol) {
	case IPPROTO_UDP:
	case IPPROTO_TCP:
		/*
		 * The ports are at the same offsets in a TCP and UDP
		 * header so hack it ...
		 */
		udp = (struct udphdr*)(((char*)iph)+(iph->ihl<<2));
		er->sen_sport = udp->source;
		er->sen_dport = udp->dest;
		break;
	default:
		er->sen_sport = 0;
		er->sen_dport = 0;
		break;
	}
}

/*
 * A TRAP eroute is installed and we want to replace it with a HOLD
 * eroute.
 */
static int create_hold_eroute(struct eroute *origtrap,
			      struct sk_buff * skb, struct iphdr * iph,
			      uint32_t eroute_pid)
{
	struct eroute hold_eroute;
	ip_said hold_said;
	struct sk_buff *first, *last;
	int error;

	first = last = NULL;
	memset((caddr_t)&hold_eroute, 0, sizeof(hold_eroute));
	memset((caddr_t)&hold_said, 0, sizeof(hold_said));
	
	hold_said.proto = IPPROTO_INT;
	hold_said.spi = htonl(SPI_HOLD);
	hold_said.dst.u.v4.sin_addr.s_addr = INADDR_ANY;
	
	hold_eroute.er_eaddr.sen_len = sizeof(struct sockaddr_encap);
	hold_eroute.er_emask.sen_len = sizeof(struct sockaddr_encap);
	hold_eroute.er_eaddr.sen_family = AF_ENCAP;
	hold_eroute.er_emask.sen_family = AF_ENCAP;
	hold_eroute.er_eaddr.sen_type = SENT_IP4;
	hold_eroute.er_emask.sen_type = 255;
	
	hold_eroute.er_eaddr.sen_ip_src.s_addr = iph->saddr;
	hold_eroute.er_eaddr.sen_ip_dst.s_addr = iph->daddr;
	hold_eroute.er_emask.sen_ip_src.s_addr = INADDR_BROADCAST;
	hold_eroute.er_emask.sen_ip_dst.s_addr = INADDR_BROADCAST;
	hold_eroute.er_emask.sen_sport = 0;
	hold_eroute.er_emask.sen_dport = 0;
	hold_eroute.er_pid = eroute_pid;
	hold_eroute.er_count = 0;
	hold_eroute.er_lasttime = jiffies/HZ;

	/*
	 * if it wasn't captured by a wildcard, then don't record it as
	 * a wildcard.
	 */
	if(origtrap->er_eaddr.sen_proto != 0) {
	  hold_eroute.er_eaddr.sen_proto = iph->protocol;

	  if((iph->protocol == IPPROTO_TCP ||
	      iph->protocol == IPPROTO_UDP) &&
	     (origtrap->er_eaddr.sen_sport != 0 ||
	      origtrap->er_eaddr.sen_dport != 0)) {

	    if(origtrap->er_eaddr.sen_sport != 0)
	      hold_eroute.er_emask.sen_sport = ~0;

	    if(origtrap->er_eaddr.sen_dport != 0) 
	      hold_eroute.er_emask.sen_dport = ~0;

	    ipsec_extract_ports(iph, &hold_eroute.er_eaddr);
	  }
	}

#ifdef CONFIG_KLIPS_DEBUG
	if (debug_pfkey) {
		char buf1[64], buf2[64];
		subnettoa(hold_eroute.er_eaddr.sen_ip_src,
			  hold_eroute.er_emask.sen_ip_src, 0, buf1, sizeof(buf1));
		subnettoa(hold_eroute.er_eaddr.sen_ip_dst,
			  hold_eroute.er_emask.sen_ip_dst, 0, buf2, sizeof(buf2));
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "calling breakeroute and makeroute for %s:%d->%s:%d %d HOLD eroute.\n",
			    buf1, ntohs(hold_eroute.er_eaddr.sen_sport),
			    buf2, ntohs(hold_eroute.er_eaddr.sen_dport),
			    hold_eroute.er_eaddr.sen_proto);
	}
#endif /* CONFIG_KLIPS_DEBUG */

	if (ipsec_breakroute(&(hold_eroute.er_eaddr), &(hold_eroute.er_emask),
			     &first, &last)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "HOLD breakeroute found nothing.\n");
	} else {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "HOLD breakroute deleted %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u %u\n",
			    NIPQUAD(hold_eroute.er_eaddr.sen_ip_src),
			    ntohs(hold_eroute.er_eaddr.sen_sport),
			    NIPQUAD(hold_eroute.er_eaddr.sen_ip_dst),
			    ntohs(hold_eroute.er_eaddr.sen_dport),
			    hold_eroute.er_eaddr.sen_proto);
	}
	if (first != NULL)
		kfree_skb(first);
	if (last != NULL)
		kfree_skb(last);

	error = ipsec_makeroute(&(hold_eroute.er_eaddr),
				&(hold_eroute.er_emask),
				hold_said, eroute_pid, skb, NULL, NULL);
	if (error) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "HOLD makeroute returned %d, failed.\n", error);
	} else {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:ipsec_tunnel_start_xmit: "
			    "HOLD makeroute call successful.\n");
	}
	return (error == 0);
}

enum ipsec_xmit_value
ipsec_xmit_init(struct ipsec_xmit_state *ixs)
{
	enum ipsec_xmit_value bundle_stat = IPSEC_XMIT_OK;
#ifdef CONFIG_KLIPS_ALG
	ixs->blocksize = 8;
	ixs->ixt_e = NULL;
	ixs->ixt_a = NULL;
#endif /* CONFIG_KLIPS_ALG */
 
	ixs->newdst = ixs->orgdst = ixs->iph->daddr;
	ixs->newsrc = ixs->orgsrc = ixs->iph->saddr;
	ixs->orgedst = ixs->outgoing_said.dst.u.v4.sin_addr.s_addr;
	ixs->iphlen = ixs->iph->ihl << 2;
	ixs->pyldsz = ntohs(ixs->iph->tot_len) - ixs->iphlen;
	ixs->max_headroom = ixs->max_tailroom = 0;
		
	if (ixs->outgoing_said.proto == IPPROTO_INT) {
		switch (ntohl(ixs->outgoing_said.spi)) {
		case SPI_DROP:
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "shunt SA of DROP or no eroute: dropping.\n");
			ixs->stats->tx_dropped++;
			break;
				
		case SPI_REJECT:
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "shunt SA of REJECT: notifying and dropping.\n");
			ICMP_SEND(ixs->skb,
				  ICMP_DEST_UNREACH,
				  ICMP_PKT_FILTERED,
				  0,
				  ixs->physdev);
			ixs->stats->tx_dropped++;
			break;
				
		case SPI_PASS:
#ifdef NET_21
			ixs->pass = 1;
#endif /* NET_21 */
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "PASS: calling dev_queue_xmit\n");
			return IPSEC_XMIT_PASS;
				
		case SPI_HOLD:
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "shunt SA of HOLD: this does not make sense here, dropping.\n");
			ixs->stats->tx_dropped++;
			break;

		case SPI_TRAP:
		case SPI_TRAPSUBNET:
		{
			struct sockaddr_in src, dst;
#ifdef CONFIG_KLIPS_DEBUG
			char bufsrc[ADDRTOA_BUF], bufdst[ADDRTOA_BUF];
#endif /* CONFIG_KLIPS_DEBUG */

			/* Signal all listening KMds with a PF_KEY ACQUIRE */

			memset(&src, 0, sizeof(src));
			memset(&dst, 0, sizeof(dst));
			src.sin_family = AF_INET;
			dst.sin_family = AF_INET;
			src.sin_addr.s_addr = ixs->iph->saddr;
			dst.sin_addr.s_addr = ixs->iph->daddr;

			ixs->ips.ips_transport_protocol = 0;
			src.sin_port = 0;
			dst.sin_port = 0;
			
			if(ixs->eroute->er_eaddr.sen_proto != 0) {
			  ixs->ips.ips_transport_protocol = ixs->iph->protocol;
			  
			  if(ixs->eroute->er_eaddr.sen_sport != 0) {
			    src.sin_port = 
			      (ixs->iph->protocol == IPPROTO_UDP
			       ? ((struct udphdr*) (((caddr_t)ixs->iph) + (ixs->iph->ihl << 2)))->source
			       : (ixs->iph->protocol == IPPROTO_TCP
				  ? ((struct tcphdr*)((caddr_t)ixs->iph + (ixs->iph->ihl << 2)))->source
				  : 0));
			  }
			  if(ixs->eroute->er_eaddr.sen_dport != 0) {
			    dst.sin_port = 
			      (ixs->iph->protocol == IPPROTO_UDP
			       ? ((struct udphdr*) (((caddr_t)ixs->iph) + (ixs->iph->ihl << 2)))->dest
			       : (ixs->iph->protocol == IPPROTO_TCP
				  ? ((struct tcphdr*)((caddr_t)ixs->iph + (ixs->iph->ihl << 2)))->dest
				  : 0));
			  }
			}
				
			ixs->ips.ips_addr_s = (struct sockaddr*)(&src);
			ixs->ips.ips_addr_d = (struct sockaddr*)(&dst);
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "SADB_ACQUIRE sent with src=%s:%d, dst=%s:%d, proto=%d.\n",
				    addrtoa(((struct sockaddr_in*)(ixs->ips.ips_addr_s))->sin_addr, 0, bufsrc, sizeof(bufsrc)) <= ADDRTOA_BUF ? bufsrc : "BAD_ADDR",
				    ntohs(((struct sockaddr_in*)(ixs->ips.ips_addr_s))->sin_port),
				    addrtoa(((struct sockaddr_in*)(ixs->ips.ips_addr_d))->sin_addr, 0, bufdst, sizeof(bufdst)) <= ADDRTOA_BUF ? bufdst : "BAD_ADDR",
				    ntohs(((struct sockaddr_in*)(ixs->ips.ips_addr_d))->sin_port),
				    ixs->ips.ips_said.proto);
				
			/* increment count of total traps needed */
			ipsec_xmit_trap_count++;

			if (pfkey_acquire(&ixs->ips) == 0) {

				/* note that we succeeded */
			        ipsec_xmit_trap_sendcount++;
					
				if (ixs->outgoing_said.spi==htonl(SPI_TRAPSUBNET)) {
					/*
					 * The spinlock is to prevent any other
					 * process from accessing or deleting
					 * the eroute while we are using and
					 * updating it.
					 */
					spin_lock(&eroute_lock);
					ixs->eroute = ipsec_findroute(&ixs->matcher);
					if(ixs->eroute) {
						ixs->eroute->er_said.spi = htonl(SPI_HOLD);
						ixs->eroute->er_first = ixs->skb;
						ixs->skb = NULL;
					}
					spin_unlock(&eroute_lock);
				} else if (create_hold_eroute(ixs->eroute,
							      ixs->skb,
							      ixs->iph,
							      ixs->eroute_pid)) {
					ixs->skb = NULL;
				} 
				/* whether or not the above succeeded, we continue */
				
			}
			ixs->stats->tx_dropped++;
		}
		default:
			/* XXX what do we do with an unknown shunt spi? */
			break;
		} /* switch (ntohl(ixs->outgoing_said.spi)) */
		return IPSEC_XMIT_STOLEN;
	} /* if (ixs->outgoing_said.proto == IPPROTO_INT) */
	
	ixs->ipsp = ipsec_sa_getbyid(&ixs->outgoing_said);
	ixs->sa_len = KLIPS_SATOT(debug_tunnel, &ixs->outgoing_said, 0, ixs->sa_txt, sizeof(ixs->sa_txt));

	if (ixs->ipsp == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "no ipsec_sa for SA%s: outgoing packet with no SA, dropped.\n",
			    ixs->sa_len ? ixs->sa_txt : " (error)");
		if(ixs->stats) {
			ixs->stats->tx_dropped++;
		}
		bundle_stat = IPSEC_XMIT_SAIDNOTFOUND;
		goto cleanup;
	}
		
	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
		    "klips_debug:ipsec_xmit_encap_bundle: "
		    "found ipsec_sa -- SA:<%s%s%s> %s\n",
		    IPS_XFORM_NAME(ixs->ipsp),
		    ixs->sa_len ? ixs->sa_txt : " (error)");
		
	/*
	 * How much headroom do we need to be able to apply
	 * all the grouped transforms?
	 */
	ixs->ipsq = ixs->ipsp;	/* save the head of the ipsec_sa chain */
	while (ixs->ipsp) {
		if (debug_tunnel & DB_TN_XMIT) {
		ixs->sa_len = satot(&ixs->ipsp->ips_said, 0, ixs->sa_txt, sizeof(ixs->sa_txt));
		if(ixs->sa_len == 0) {
			strcpy(ixs->sa_txt, "(error)");
		}
		} else {
			*ixs->sa_txt = 0;
			ixs->sa_len = 0;
		}

		/* If it is in larval state, drop the packet, we cannot process yet. */
		if(ixs->ipsp->ips_state == SADB_SASTATE_LARVAL) {
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "ipsec_sa in larval state for SA:<%s%s%s> %s, cannot be used yet, dropping packet.\n",
				    IPS_XFORM_NAME(ixs->ipsp),
				    ixs->sa_len ? ixs->sa_txt : " (error)");
			if(ixs->stats) {
				ixs->stats->tx_errors++;
			}
			bundle_stat = IPSEC_XMIT_SAIDNOTLIVE;
			goto cleanup;
		}

		if(ixs->ipsp->ips_state == SADB_SASTATE_DEAD) {
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "ipsec_sa in dead state for SA:<%s%s%s> %s, can no longer be used, dropping packet.\n",
				    IPS_XFORM_NAME(ixs->ipsp),
				    ixs->sa_len ? ixs->sa_txt : " (error)");
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_SAIDNOTLIVE;
			goto cleanup;
		}

		/* If the replay window counter == -1, expire SA, it will roll */
		if(ixs->ipsp->ips_replaywin && ixs->ipsp->ips_replaywin_lastseq == -1) {
			pfkey_expire(ixs->ipsp, 1);
			KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				    "klips_debug:ipsec_xmit_encap_bundle: "
				    "replay window counter rolled for SA:<%s%s%s> %s, packet dropped, expiring SA.\n",
				    IPS_XFORM_NAME(ixs->ipsp),
				    ixs->sa_len ? ixs->sa_txt : " (error)");
			ipsec_sa_delchain(ixs->ipsp);
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_REPLAYROLLED;
			goto cleanup;
		}

		/*
		 * if this is the first time we are using this SA, mark start time,
		 * and offset hard/soft counters by "now" for later checking.
		 */
#if 0
		if(ixs->ipsp->ips_life.ipl_usetime.count == 0) {
			ixs->ipsp->ips_life.ipl_usetime.count = jiffies;
			ixs->ipsp->ips_life.ipl_usetime.hard += jiffies;
			ixs->ipsp->ips_life.ipl_usetime.soft += jiffies;
		}
#endif
			  

		if(ipsec_lifetime_check(&ixs->ipsp->ips_life.ipl_bytes, "bytes", ixs->sa_txt, 
					ipsec_life_countbased, ipsec_outgoing, ixs->ipsp) == ipsec_life_harddied ||
		   ipsec_lifetime_check(&ixs->ipsp->ips_life.ipl_addtime, "addtime",ixs->sa_txt,
					ipsec_life_timebased,  ipsec_outgoing, ixs->ipsp) == ipsec_life_harddied ||
		   ipsec_lifetime_check(&ixs->ipsp->ips_life.ipl_usetime, "usetime",ixs->sa_txt,
					ipsec_life_timebased,  ipsec_outgoing, ixs->ipsp) == ipsec_life_harddied ||
		   ipsec_lifetime_check(&ixs->ipsp->ips_life.ipl_packets, "packets",ixs->sa_txt,
					ipsec_life_countbased, ipsec_outgoing, ixs->ipsp) == ipsec_life_harddied) {
				
			ipsec_sa_delchain(ixs->ipsp);
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_LIFETIMEFAILED;
			goto cleanup;
		}
			

		ixs->headroom = ixs->tailroom = 0;
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "calling room for <%s%s%s>, SA:%s\n", 
			    IPS_XFORM_NAME(ixs->ipsp),
			    ixs->sa_len ? ixs->sa_txt : " (error)");
		switch(ixs->ipsp->ips_said.proto) {
#ifdef CONFIG_KLIPS_AH
		case IPPROTO_AH:
			ixs->headroom += sizeof(struct ahhdr);
			break;
#endif /* CONFIG_KLIPS_AH */

#ifdef CONFIG_KLIPS_ESP
		case IPPROTO_ESP:
#ifdef CONFIG_KLIPS_OCF
			/*
			 * this needs cleaning up for sure - DM
			 */
			if (ixs->ipsp->ocf_in_use) {
				switch (ixs->ipsp->ips_encalg) {
				case ESP_DES:
				case ESP_3DES:
					ixs->blocksize = 8;
					ixs->headroom += ESP_HEADER_LEN + 8 /* ivsize */;
					break;
				case ESP_AES:
					ixs->blocksize = 16;
					ixs->headroom += ESP_HEADER_LEN + 16 /* ivsize */;
					break;
				default:
					ixs->stats->tx_errors++;
					bundle_stat = IPSEC_XMIT_ESP_BADALG;
					goto cleanup;
				}
			} else
#endif /* CONFIG_KLIPS_OCF */
#ifdef CONFIG_KLIPS_ALG
			if ((ixs->ixt_e=ixs->ipsp->ips_alg_enc)) {
				ixs->blocksize = ixs->ixt_e->ixt_common.ixt_blocksize;
				ixs->headroom += ESP_HEADER_LEN + ixs->ixt_e->ixt_common.ixt_support.ias_ivlen/8;
			} else
#endif /* CONFIG_KLIPS_ALG */
			{
				ixs->stats->tx_errors++;
				bundle_stat = IPSEC_XMIT_ESP_BADALG;
				goto cleanup;
			}
#ifdef CONFIG_KLIPS_OCF
			if (ixs->ipsp->ocf_in_use) {
				switch (ixs->ipsp->ips_authalg) {
				case AH_MD5:
				case AH_SHA:
					ixs->tailroom += AHHMAC_HASHLEN;
					break;
				case AH_NONE:
					break;
				}
			} else
#endif /* CONFIG_KLIPS_OCF */
#ifdef CONFIG_KLIPS_ALG
			if ((ixs->ixt_a=ixs->ipsp->ips_alg_auth)) {
				ixs->tailroom += AHHMAC_HASHLEN;
			} else
#endif /* CONFIG_KLIPS_ALG */
			switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
			case AH_MD5:
				ixs->tailroom += AHHMAC_HASHLEN;
				break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
			case AH_SHA:
				ixs->tailroom += AHHMAC_HASHLEN;
				break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
			case AH_NONE:
				break;
			default:
				ixs->stats->tx_errors++;
				bundle_stat = IPSEC_XMIT_AH_BADALG;
				goto cleanup;
			}			
			ixs->tailroom += ixs->blocksize != 1 ?
				((ixs->blocksize - ((ixs->pyldsz + 2) % ixs->blocksize)) % ixs->blocksize) + 2 :
				((4 - ((ixs->pyldsz + 2) % 4)) % 4) + 2;
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
		if ((ixs->ipsp->ips_natt_type) && (!ixs->natt_type)) {
			ixs->natt_type = ixs->ipsp->ips_natt_type;
			ixs->natt_sport = ixs->ipsp->ips_natt_sport;
			ixs->natt_dport = ixs->ipsp->ips_natt_dport;
			switch (ixs->natt_type) {
				case ESPINUDP_WITH_NON_IKE:
					ixs->natt_head = sizeof(struct udphdr)+(2*sizeof(__u32));
					break;
					
				case ESPINUDP_WITH_NON_ESP:
					ixs->natt_head = sizeof(struct udphdr);
					break;
					
				default:
				  KLIPS_PRINT(debug_tunnel & DB_TN_CROUT
					      , "klips_xmit: invalid nat-t type %d"
					      , ixs->natt_type);
				  bundle_stat = IPSEC_XMIT_ESPUDP_BADTYPE;
				  goto cleanup;
					      
					break;
			}
			ixs->tailroom += ixs->natt_head;
		}
#endif
			break;
#endif /* CONFIG_KLIPS_ESP */
#ifdef CONFIG_KLIPS_IPIP
		case IPPROTO_IPIP:
			ixs->headroom += sizeof(struct iphdr);
			break;
#endif /* !CONFIG_KLIPS_IPIP */

		case IPPROTO_COMP:
#ifdef CONFIG_KLIPS_IPCOMP
			/*
			  We can't predict how much the packet will
			  shrink without doing the actual compression.
			  We could do it here, if we were the first
			  encapsulation in the chain.  That might save
			  us a skb_copy_expand, since we might fit
			  into the existing skb then.  However, this
			  would be a bit unclean (and this hack has
			  bit us once), so we better not do it. After
			  all, the skb_copy_expand is cheap in
			  comparison to the actual compression.
			  At least we know the packet will not grow.
			*/
			break;
#endif /* CONFIG_KLIPS_IPCOMP */

		default:
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_BADPROTO;
			goto cleanup;
		}
		ixs->ipsp = ixs->ipsp->ips_onext;
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "Required head,tailroom: %d,%d\n", 
			    ixs->headroom, ixs->tailroom);
		ixs->max_headroom += ixs->headroom;
		ixs->max_tailroom += ixs->tailroom;
		ixs->pyldsz += (ixs->headroom + ixs->tailroom);

		if(debug_tunnel & DB_TN_ENCAP) {
			ipsec_print_ip(ixs->iph);
		}
	}
	ixs->ipsp = ixs->ipsq;	/* restore the head of the ipsec_sa chain */
		
	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_bundle: "
		    "existing head,tailroom: %d,%d before applying xforms with head,tailroom: %d,%d .\n",
		    skb_headroom(ixs->skb), skb_tailroom(ixs->skb),
		    ixs->max_headroom, ixs->max_tailroom);
		
	ixs->tot_headroom += ixs->max_headroom;
	ixs->tot_tailroom += ixs->max_tailroom;

	ixs->mtudiff = ixs->cur_mtu + ixs->tot_headroom + ixs->tot_tailroom - ixs->physmtu;

	KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
		    "klips_debug:ipsec_xmit_encap_bundle: "
		    "mtu:%d physmtu:%d tothr:%d tottr:%d mtudiff:%d ippkttotlen:%d\n",
		    ixs->cur_mtu, ixs->physmtu,
		    ixs->tot_headroom, ixs->tot_tailroom, ixs->mtudiff, ntohs(ixs->iph->tot_len));
	if(ixs->cur_mtu == 0 || ixs->mtudiff > 0) {
		int newmtu = ixs->physmtu - (ixs->tot_headroom + ((ixs->tot_tailroom + 2) & ~7) + 5);

		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_info:ipsec_xmit_encap_bundle: "
			    "dev %s mtu of %d decreased by %d to %d\n",
			    ixs->dev ? ixs->dev->name : "ifX",
			    ixs->cur_mtu,
			    ixs->cur_mtu - newmtu,
			    newmtu);
		ixs->cur_mtu = newmtu;

		/* this would seem to adjust the MTU of the route as well */
#if 0
		ixs->skb->dst->pmtu = ixs->prv->mtu; /* RGB */
#endif /* 0 */
	}

	/* 
	   If the sender is doing PMTU discovery, and the
	   packet doesn't fit within ixs->prv->mtu, notify him
	   (unless it was an ICMP packet, or it was not the
	   zero-offset packet) and send it anyways.

	   Note: buggy firewall configuration may prevent the
	   ICMP packet from getting back.
	*/
	if(sysctl_ipsec_icmp
	   && ixs->cur_mtu < ntohs(ixs->iph->tot_len)
	   && (ixs->iph->frag_off & __constant_htons(IP_DF)) ) {
		int notify = ixs->iph->protocol != IPPROTO_ICMP
			&& (ixs->iph->frag_off & __constant_htons(IP_OFFSET)) == 0;
			
#ifdef IPSEC_obey_DF
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "fragmentation needed and DF set; %sdropping packet\n",
			    notify ? "sending ICMP and " : "");
		if (notify)
			ICMP_SEND(ixs->skb,
				  ICMP_DEST_UNREACH,
				  ICMP_FRAG_NEEDED,
				  ixs->cur_mtu,
				  ixs->physdev);
		ixs->stats->tx_errors++;
		bundle_stat = IPSEC_XMIT_CANNOTFRAG;
		goto cleanup;
#else /* IPSEC_obey_DF */
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "fragmentation needed and DF set; %spassing packet\n",
			    notify ? "sending ICMP and " : "");
		if (notify)
			ICMP_SEND(ixs->skb,
				  ICMP_DEST_UNREACH,
				  ICMP_FRAG_NEEDED,
				  ixs->cur_mtu,
				  ixs->physdev);
#endif /* IPSEC_obey_DF */
	}
		
#ifdef MSS_HACK
	/*
	 * If this is a transport mode TCP packet with
	 * SYN set, determine an effective MSS based on 
	 * AH/ESP overheads determined above.
	 */
	if (ixs->iph->protocol == IPPROTO_TCP 
	    && ixs->outgoing_said.proto != IPPROTO_IPIP) {
		struct tcphdr *tcph = ixs->skb->h.th;
		if (tcph->syn && !tcph->ack) {
			if(!ipsec_adjust_mss(ixs->skb, tcph, ixs->cur_mtu)) {
				printk(KERN_WARNING
				       "klips_warning:ipsec_xmit_encap_bundle: "
				       "ipsec_adjust_mss() failed\n");
				ixs->stats->tx_errors++;
				bundle_stat = IPSEC_XMIT_MSSERR;
				goto cleanup;
			}
		}
	}
#endif /* MSS_HACK */

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
      if ((ixs->natt_type) && (ixs->outgoing_said.proto != IPPROTO_IPIP)) {
	      /**
	       * NAT-Traversal and Transport Mode:
	       *   we need to correct TCP/UDP checksum
	       *
	       * If we've got NAT-OA, we can fix checksum without recalculation.
	       * If we don't we can zero udp checksum.
	       */
	      __u32 natt_oa = ixs->ipsp->ips_natt_oa ?
		      ((struct sockaddr_in*)(ixs->ipsp->ips_natt_oa))->sin_addr.s_addr : 0;
	      __u16 pkt_len = ixs->skb->tail - (unsigned char *)ixs->iph;
	      __u16 data_len = pkt_len - (ixs->iph->ihl << 2);
	      switch (ixs->iph->protocol) {
		      case IPPROTO_TCP:
			      if (data_len >= sizeof(struct tcphdr)) {
				      struct tcphdr *tcp = (struct tcphdr *)((__u32 *)ixs->iph+ixs->iph->ihl);
				      if (natt_oa) {
					      __u32 buff[2] = { ~ixs->iph->daddr, natt_oa };
					      KLIPS_PRINT(debug_tunnel,
						      "klips_debug:ipsec_tunnel_start_xmit: "
						      "NAT-T & TRANSPORT: "
						      "fix TCP checksum using NAT-OA\n");
					      tcp->check = csum_fold(
						      csum_partial((unsigned char *)buff, sizeof(buff),
						      tcp->check^0xffff));
				      }
				      else {
					      KLIPS_PRINT(debug_tunnel,
						      "klips_debug:ipsec_tunnel_start_xmit: "
						      "NAT-T & TRANSPORT: do not recalc TCP checksum\n");
				      }
			      }
			      else {
				      KLIPS_PRINT(debug_tunnel,
					      "klips_debug:ipsec_tunnel_start_xmit: "
					      "NAT-T & TRANSPORT: can't fix TCP checksum\n");
			      }
			      break;
		      case IPPROTO_UDP:
			      if (data_len >= sizeof(struct udphdr)) {
				      struct udphdr *udp = (struct udphdr *)((__u32 *)ixs->iph+ixs->iph->ihl);
				      if (udp->check == 0) {
					      KLIPS_PRINT(debug_tunnel,
						      "klips_debug:ipsec_tunnel_start_xmit: "
						      "NAT-T & TRANSPORT: UDP checksum already 0\n");
				      }
				      else if (natt_oa) {
					      __u32 buff[2] = { ~ixs->iph->daddr, natt_oa };
					      KLIPS_PRINT(debug_tunnel,
						      "klips_debug:ipsec_tunnel_start_xmit: "
						      "NAT-T & TRANSPORT: "
						      "fix UDP checksum using NAT-OA\n");
					      udp->check = csum_fold(
						      csum_partial((unsigned char *)buff, sizeof(buff),
						      udp->check^0xffff));
				      }
				      else {
					      KLIPS_PRINT(debug_tunnel,
						      "klips_debug:ipsec_tunnel_start_xmit: "
						      "NAT-T & TRANSPORT: zero UDP checksum\n");
					      udp->check = 0;
				      }
			      }
			      else {
				      KLIPS_PRINT(debug_tunnel,
					      "klips_debug:ipsec_tunnel_start_xmit: "
					      "NAT-T & TRANSPORT: can't fix UDP checksum\n");
			      }
			      break;
		      default:
			      KLIPS_PRINT(debug_tunnel,
				      "klips_debug:ipsec_tunnel_start_xmit: "
				      "NAT-T & TRANSPORT: non TCP/UDP packet -- do nothing\n");
			      break;
	      }
      }
#endif /* CONFIG_IPSEC_NAT_TRAVERSAL */

	if(!ixs->hard_header_stripped && ixs->hard_header_len>0) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "allocating %d bytes for hardheader.\n",
			    ixs->hard_header_len);
		if((ixs->saved_header = kmalloc(ixs->hard_header_len, GFP_ATOMIC)) == NULL) {
			printk(KERN_WARNING "klips_debug:ipsec_xmit_encap_bundle: "
			       "Failed, tried to allocate %d bytes for temp hard_header.\n", 
			       ixs->hard_header_len);
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_ERRMEMALLOC;
			goto cleanup;
		}
		{
			int i;
			for (i = 0; i < ixs->hard_header_len; i++) {
				ixs->saved_header[i] = ixs->skb->data[i];
			}
		}
		if(ixs->skb->len < ixs->hard_header_len) {
			printk(KERN_WARNING "klips_error:ipsec_xmit_encap_bundle: "
			       "tried to skb_pull hhlen=%d, %d available.  This should never happen, please report.\n",
			       ixs->hard_header_len, (int)(ixs->skb->len));
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_ESP_PUSHPULLERR;
			goto cleanup;
		}
		skb_pull(ixs->skb, ixs->hard_header_len);
		ixs->hard_header_stripped = 1;
			
/*			ixs->iph = (struct iphdr *) (ixs->skb->data); */
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "head,tailroom: %d,%d after hard_header stripped.\n",
			    skb_headroom(ixs->skb), skb_tailroom(ixs->skb));
		KLIPS_IP_PRINT(debug_tunnel & DB_TN_CROUT, ixs->iph);
	} else {
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "hard header already stripped.\n");
	}
		
	ixs->ll_headroom = (ixs->hard_header_len + 15) & ~15;

	if ((skb_headroom(ixs->skb) >= ixs->max_headroom + 2 * ixs->ll_headroom) && 
	    (skb_tailroom(ixs->skb) >= ixs->max_tailroom)
#ifndef NET_21
	    && ixs->skb->free
#endif /* !NET_21 */
		) {
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "data fits in existing skb\n");
	} else {
		struct sk_buff* tskb;

		if(!ixs->oskb) {
			ixs->oskb = ixs->skb;
		}

		tskb = skb_copy_expand(ixs->skb,
				       /* The need for 2 * link layer length here remains unexplained...RGB */
				       ixs->max_headroom + 2 * ixs->ll_headroom,
				       ixs->max_tailroom,
				       GFP_ATOMIC);

		if(tskb && ixs->skb->sk) {
			skb_set_owner_w(tskb, ixs->skb->sk);
		}

		if(ixs->skb != ixs->oskb) {
			ipsec_kfree_skb(ixs->skb);
		}
		ixs->skb = tskb;
		if (!ixs->skb) {
			printk(KERN_WARNING
			       "klips_debug:ipsec_xmit_encap_bundle: "
			       "Failed, tried to allocate %d head and %d tailroom\n", 
			       ixs->max_headroom, ixs->max_tailroom);
			ixs->stats->tx_errors++;
			bundle_stat = IPSEC_XMIT_ERRSKBALLOC;
			goto cleanup;
		}
		KLIPS_PRINT(debug_tunnel & DB_TN_CROUT,
			    "klips_debug:ipsec_xmit_encap_bundle: "
			    "head,tailroom: %d,%d after allocation\n",
			    skb_headroom(ixs->skb), skb_tailroom(ixs->skb));
	}

	if(debug_tunnel & DB_TN_ENCAP) {
		ipsec_print_ip(ixs->iph);
	}

cleanup:
	return bundle_stat;
}


/*
 * here is a state machine to handle encapsulation
 * basically we keep getting re-entered until processing is
 * complete.  For the simple case we step down the states and finish.
 * each state is ideally some logical part of the process.  If a state
 * can pend (ie., require async processing to complete),  then this
 * should be the part of last action before it returns IPSEC_RCV_PENDING
 *
 * Any particular action may alter the next_state in ixs to move us to
 * a state other than the preferred "next_state",  but this is the
 * exception and is highlighted when it is done.
 *
 * prototypes for state action
 */

struct {
	enum ipsec_xmit_value (*action)(struct ipsec_xmit_state *ixs);
	int next_state;
} xmit_state_table[] = {
	[IPSEC_XSM_INIT]        = {ipsec_xmit_init,        IPSEC_XSM_ENCAP_INIT },
	[IPSEC_XSM_ENCAP_INIT]  = {ipsec_xmit_encap_init,  IPSEC_XSM_ENCAP_SELECT },
	[IPSEC_XSM_ENCAP_SELECT]= {ipsec_xmit_encap_select,IPSEC_XSM_DONE },

#ifdef CONFIG_KLIPS_ESP
	[IPSEC_XSM_ESP]         = {ipsec_xmit_esp,         IPSEC_XSM_ESP_AH },
	[IPSEC_XSM_ESP_AH]      = {ipsec_xmit_esp_ah,      IPSEC_XSM_CONT },
#endif

#ifdef CONFIG_KLIPS_AH
	[IPSEC_XSM_AH]          = {ipsec_xmit_ah,          IPSEC_XSM_CONT },
#endif

#ifdef CONFIG_KLIPS_IPIP
	[IPSEC_XSM_IPIP]        = {ipsec_xmit_ipip,        IPSEC_XSM_CONT },
#endif

#ifdef CONFIG_KLIPS_IPCOMP
	[IPSEC_XSM_IPCOMP]      = {ipsec_xmit_ipcomp,      IPSEC_XSM_CONT },
#endif

	[IPSEC_XSM_CONT]        = {ipsec_xmit_cont,        IPSEC_XSM_DONE },
	[IPSEC_XSM_DONE]        = {NULL,                   IPSEC_XSM_DONE},
};



void
ipsec_xsm(struct ipsec_xmit_state *ixs)
{
	enum ipsec_xmit_value stat = IPSEC_XMIT_ENCAPFAIL;

	if (ixs == NULL) {
		KLIPS_PRINT(debug_tunnel, "klips_debug:ipsec_xsm: ixs == NULL.\n");
		return;
	}

	/*
	 * make sure nothing is removed from underneath us
	 */
	spin_lock(&tdb_lock);

	/*
	 * if we have a valid said,  then we must check it here to ensure it
	 * hasn't gone away while we were waiting for a task to complete
	 */

	if (ixs->ipsp && ipsec_sa_getbyid(&ixs->outgoing_said) == NULL) {
		KLIPS_PRINT(debug_tunnel,
			    "klips_debug:ipsec_xsm: "
			    "no ipsec_sa for SA:%s: outgoing packet with no SA dropped\n",
			    ixs->sa_len ? ixs->sa_txt : " (error)");
		if (ixs->stats)
			ixs->stats->tx_dropped++;

		/* drop through and cleanup */
		stat = IPSEC_XMIT_SAIDNOTFOUND;
		ixs->state = IPSEC_XSM_DONE;
	}

	while (ixs->state != IPSEC_XSM_DONE) {

		ixs->next_state = xmit_state_table[ixs->state].next_state;

		stat = xmit_state_table[ixs->state].action(ixs);

		if (stat == IPSEC_XMIT_OK) {
			/* some functions change the next state, see the state table */
			ixs->state = ixs->next_state;
		} else if (stat == IPSEC_XMIT_PENDING) {
			/*
			 * things are on hold until we return here in the next/new state
			 * we check our SA is valid when we return
			 */
			spin_unlock(&tdb_lock);
			return;
		} else {
			/* bad result, force state change to done */
			KLIPS_PRINT(debug_tunnel,
					"klips_debug:ipsec_xsm: "
					"processing completed due to %s.\n",
					ipsec_xmit_err(stat));
			ixs->state = IPSEC_XSM_DONE;
		}
	}

	/*
	 * all done with anything needing locks
	 */
	spin_unlock(&tdb_lock);

	/* we are done with this SA */
	if (ixs->ipsp) {
		ipsec_sa_put(ixs->ipsp); 
		ixs->ipsp = NULL;
	}

	/*
	 * let the caller continue with their processing
	 */
	ixs->xsm_complete(ixs, stat);
}


/*
 * $Log: ipsec_xmit.c,v $
 * Revision 1.20.2.6  2006/07/07 22:09:49  paul
 * From: Bart Trojanowski <bart@xelerance.com>
 * Removing a left over '#else' that split another '#if/#endif' block in two.
 *
 * Revision 1.20.2.5  2006/07/07 15:43:17  paul
 * From: Bart Trojanowski <bart@xelerance.com>
 * improved protocol detection in ipsec_print_ip() -- a debug aid.
 *
 * Revision 1.20.2.4  2006/04/20 16:33:07  mcr
 * remove all of CONFIG_KLIPS_ALG --- one can no longer build without it.
 * Fix in-kernel module compilation. Sub-makefiles do not work.
 *
 * Revision 1.20.2.3  2005/11/29 21:52:57  ken
 * Fix for #518 MTU issues
 *
 * Revision 1.20.2.2  2005/11/27 21:41:03  paul
 * Pull down TTL fixes from head. this fixes "Unknown symbol sysctl_ip_default_ttl"in for klips as module.
 *
 * Revision 1.20.2.1  2005/08/27 23:40:00  paul
 * recommited HAVE_SOCK_SECURITY fixes for linux 2.6.13
 *
 * Revision 1.20  2005/07/12 15:39:27  paul
 * include asm/uaccess.h for VERIFY_WRITE
 *
 * Revision 1.19  2005/05/24 01:02:35  mcr
 * 	some refactoring/simplification of situation where alg
 * 	is not found.
 *
 * Revision 1.18  2005/05/23 23:52:33  mcr
 * 	adjust comments, add additional debugging.
 *
 * Revision 1.17  2005/05/23 22:57:23  mcr
 * 	removed explicit 3DES support.
 *
 * Revision 1.16  2005/05/21 03:29:15  mcr
 * 	fixed warning about unused zeroes if AH is off.
 *
 * Revision 1.15  2005/05/20 16:47:59  mcr
 * 	include asm/checksum.h to get ip_fast_csum macro.
 *
 * Revision 1.14  2005/05/11 01:43:03  mcr
 * 	removed "poor-man"s OOP in favour of proper C structures.
 *
 * Revision 1.13  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.12  2005/04/15 01:28:34  mcr
 * 	use ipsec_dmp_block.
 *
 * Revision 1.11  2005/01/26 00:50:35  mcr
 * 	adjustment of confusion of CONFIG_IPSEC_NAT vs CONFIG_KLIPS_NAT,
 * 	and make sure that NAT_TRAVERSAL is set as well to match
 * 	userspace compiles of code.
 *
 * Revision 1.10  2004/09/13 17:55:21  ken
 * MD5* -> osMD5*
 *
 * Revision 1.9  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.8  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.7  2004/02/03 03:13:41  mcr
 * 	mark invalid encapsulation states.
 *
 * Revision 1.6.2.1  2003/12/22 15:25:52  jjo
 *      Merged algo-0.8.1-rc11-test1 into alg-branch
 *
 * Revision 1.6  2003/12/10 01:14:27  mcr
 * 	NAT-traversal patches to KLIPS.
 *
 * Revision 1.5  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.4.4.2  2003/10/29 01:37:39  mcr
 * 	when creating %hold from %trap, only make the %hold as
 * 	specific as the %trap was - so if the protocol and ports
 * 	were wildcards, then the %hold will be too.
 *
 * Revision 1.4.4.1  2003/09/21 13:59:56  mcr
 * 	pre-liminary X.509 patch - does not yet pass tests.
 *
 * Revision 1.4  2003/06/20 02:28:10  mcr
 * 	misstype of variable name, not detected by module build.
 *
 * Revision 1.3  2003/06/20 01:42:21  mcr
 * 	added counters to measure how many ACQUIREs we send to pluto,
 * 	and how many are successfully sent.
 *
 * Revision 1.2  2003/04/03 17:38:35  rgb
 * Centralised ipsec_kfree_skb and ipsec_dev_{get,put}.
 * Normalised coding style.
 * Simplified logic and reduced duplication of code.
 *
 * Revision 1.1  2003/02/12 19:31:23  rgb
 * Refactored from ipsec_tunnel.c
 *
 * Local Variables:
 * c-file-style: "linux"
 * End:
 *
 */
