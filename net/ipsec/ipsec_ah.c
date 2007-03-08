/*
 * processing code for AH
 * Copyright (C) 2003-2004   Michael Richardson <mcr@xelerance.com>
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

char ipsec_ah_c_version[] = "RCSID $Id: ipsec_ah.c,v 1.12.2.1 2006/02/15 05:35:14 paul Exp $";
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
#include <net/protocol.h>

#include "openswan/radij.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_sa.h"

#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_tunnel.h" 
#include "openswan/ipsec_rcv.h"
#include "openswan/ipsec_xmit.h"

#include "openswan/ipsec_auth.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_proto.h"

#include "ipsec_ocf.h"

__u32 zeroes[AH_AMAX];

enum ipsec_rcv_value
ipsec_rcv_ah_checks(struct ipsec_rcv_state *irs,
		    struct sk_buff *skb)
{
	int ahminlen;

	ahminlen = irs->hard_header_len + sizeof(struct iphdr);

	/* take care not to deref this pointer until we check the minlen though */
	irs->protostuff.ahstuff.ahp = (struct ahhdr *)skb->h.raw;

	if((skb->len < ahminlen+sizeof(struct ahhdr)) ||
	   (skb->len < ahminlen+(irs->protostuff.ahstuff.ahp->ah_hl << 2))) {
		KLIPS_PRINT(debug_rcv & DB_RX_INAU,
			    "klips_debug:ipsec_rcv: "
			    "runt ah packet of skb->len=%d received from %s, dropped.\n",
			    skb->len,
			    irs->ipsaddr_txt);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BADLEN;
	}

	irs->said.spi = irs->protostuff.ahstuff.ahp->ah_spi;

	/* XXX we only support the one 12-byte authenticator for now */
	if(irs->protostuff.ahstuff.ahp->ah_hl != ((AHHMAC_HASHLEN+AHHMAC_RPLLEN) >> 2)) {
		KLIPS_PRINT(debug_rcv & DB_RX_INAU,
			    "klips_debug:ipsec_rcv: "
			    "bad authenticator length %ld, expected %lu from %s.\n",
			    (long)(irs->protostuff.ahstuff.ahp->ah_hl << 2),
			    (unsigned long) sizeof(struct ahhdr),
			    irs->ipsaddr_txt);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BADLEN;
	}

	return IPSEC_RCV_OK;
}


enum ipsec_rcv_value
ipsec_rcv_ah_setup_auth(struct ipsec_rcv_state *irs,
			struct sk_buff *skb,
			__u32          *replay,
			unsigned char **authenticator)
{
	struct ahhdr *ahp = irs->protostuff.ahstuff.ahp;

	*replay = ntohl(ahp->ah_rpl);
	*authenticator = ahp->ah_data;

	return IPSEC_RCV_OK;
}

enum ipsec_rcv_value
ipsec_rcv_ah_authcalc(struct ipsec_rcv_state *irs,
		      struct sk_buff *skb)
{
	struct auth_alg *aa;
	struct ahhdr *ahp = irs->protostuff.ahstuff.ahp;
	union {
		MD5_CTX		md5;
		SHA1_CTX	sha1;
	} tctx;
	struct iphdr ipo;
	int ahhlen;

#ifdef CONFIG_KLIPS_OCF
	if (irs->ipsp->ocf_in_use)
		return(ipsec_ocf_rcv(irs));
#endif

	aa = irs->authfuncs;

	/* copy the initialized keying material */
	memcpy(&tctx, irs->ictx, irs->ictx_len);

	ipo = *irs->ipp;
	ipo.tos = 0;	/* mutable RFC 2402 3.3.3.1.1.1 */
	ipo.frag_off = 0;
	ipo.ttl = 0;
	ipo.check = 0;


	/* do the sanitized header */
	(*aa->update)((void*)&tctx, (caddr_t)&ipo, sizeof(struct iphdr));

	/* XXX we didn't do the options here! */

	/* now do the AH header itself */
	ahhlen = AH_BASIC_LEN + (ahp->ah_hl << 2);
	(*aa->update)((void*)&tctx, (caddr_t)ahp,  ahhlen - AHHMAC_HASHLEN);

	/* now, do some zeroes */
	(*aa->update)((void*)&tctx, (caddr_t)zeroes,  AHHMAC_HASHLEN);

	/* finally, do the packet contents themselves */
	(*aa->update)((void*)&tctx,
		      (caddr_t)skb->h.raw + ahhlen,
		      skb->len - ahhlen);

	(*aa->final)(irs->hash, (void *)&tctx);

	memcpy(&tctx, irs->octx, irs->octx_len);

	(*aa->update)((void *)&tctx, irs->hash, aa->hashlen);
	(*aa->final)(irs->hash, (void *)&tctx);

	return IPSEC_RCV_OK;
}

enum ipsec_rcv_value
ipsec_rcv_ah_decap(struct ipsec_rcv_state *irs)
{
	struct ahhdr *ahp = irs->protostuff.ahstuff.ahp;
	struct sk_buff *skb;
	int ahhlen;

	skb=irs->skb;

	ahhlen = AH_BASIC_LEN + (ahp->ah_hl << 2);

	irs->ipp->tot_len = htons(ntohs(irs->ipp->tot_len) - ahhlen);
	irs->next_header  = ahp->ah_nh;

	/*
	 * move the IP header forward by the size of the AH header, which
	 * will remove the the AH header from the packet.
	 */
	memmove((void *)(skb->nh.raw + ahhlen),
		(void *)(skb->nh.raw), irs->iphlen);

	ipsec_rcv_dmp("ah postmove", skb->data, skb->len);

	/* skb_pull below, will move up by ahhlen */

	/* XXX not clear how this can happen, as the message indicates */
	if(skb->len < ahhlen) {
		printk(KERN_WARNING
		       "klips_error:ipsec_rcv: "
		       "tried to skb_pull ahhlen=%d, %d available.  This should never happen, please report.\n",
		       ahhlen,
		       (int)(skb->len));
		return IPSEC_RCV_DECAPFAIL;
	}
	skb_pull(skb, ahhlen);

	skb->nh.raw = skb->nh.raw + ahhlen;
	irs->ipp = skb->nh.iph;

	ipsec_rcv_dmp("ah postpull", (void *)skb->nh.iph, skb->len);

	return IPSEC_RCV_OK;
}

enum ipsec_xmit_value
ipsec_xmit_ah_setup(struct ipsec_xmit_state *ixs)
{
  struct iphdr ipo;
  struct ahhdr *ahp;
  __u8 hash[AH_AMAX];
  union {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
    MD5_CTX md5;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
    SHA1_CTX sha1;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
  } tctx;
  unsigned char *dat = (unsigned char *)ixs->iph;

  ahp = (struct ahhdr *)(dat + ixs->iphlen);
  ahp->ah_spi = ixs->ipsp->ips_said.spi;
  ahp->ah_rpl = htonl(++(ixs->ipsp->ips_replaywin_lastseq));
  ahp->ah_rv = 0;
  ahp->ah_nh = ixs->iph->protocol;
  ahp->ah_hl = (sizeof(struct ahhdr) >> 2) - sizeof(__u64)/sizeof(__u32);
  ixs->iph->protocol = IPPROTO_AH;
  ipsec_xmit_dmp("ahp", (char*)ahp, sizeof(*ahp));
  
  ipo = *ixs->iph;
  ipo.tos = 0;
  ipo.frag_off = 0;
  ipo.ttl = 0;
  ipo.check = 0;
  ipsec_xmit_dmp("ipo", (char*)&ipo, sizeof(ipo));
  
  switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
  case AH_MD5:
    tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->ictx;
    ipsec_xmit_dmp("ictx", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, (unsigned char *)&ipo, sizeof (struct iphdr));
    ipsec_xmit_dmp("ictx+ipo", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, (unsigned char *)ahp,
	      sizeof(struct ahhdr) - sizeof(ahp->ah_data));
    ipsec_xmit_dmp("ictx+ahp", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, (unsigned char *)zeroes, AHHMAC_HASHLEN);
    ipsec_xmit_dmp("ictx+zeroes", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5,  dat + ixs->iphlen + sizeof(struct ahhdr),
	      ixs->skb->len - ixs->iphlen - sizeof(struct ahhdr));
    ipsec_xmit_dmp("ictx+dat", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Final(hash, &tctx.md5);
    ipsec_xmit_dmp("ictx hash", (char*)&hash, sizeof(hash));
    tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->octx;
    ipsec_xmit_dmp("octx", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, hash, AHMD596_ALEN);
    ipsec_xmit_dmp("octx+hash", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Final(hash, &tctx.md5);
    ipsec_xmit_dmp("octx hash", (char*)&hash, sizeof(hash));
    
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
    SHA1Update(&tctx.sha1, (unsigned char *)ahp, sizeof(struct ahhdr) - sizeof(ahp->ah_data));
    SHA1Update(&tctx.sha1, (unsigned char *)zeroes, AHHMAC_HASHLEN);
    SHA1Update(&tctx.sha1, dat + ixs->iphlen + sizeof(struct ahhdr),
	       ixs->skb->len - ixs->iphlen - sizeof(struct ahhdr));
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
#ifdef NET_21
  ixs->skb->h.raw = (unsigned char*)ahp;
#endif /* NET_21 */

  return IPSEC_XMIT_OK;
}

struct xform_functions ah_xform_funcs[]={
	{	rcv_checks:         ipsec_rcv_ah_checks,
		rcv_setup_auth:     ipsec_rcv_ah_setup_auth,
		rcv_calc_auth:      ipsec_rcv_ah_authcalc,
		rcv_decrypt:        ipsec_rcv_ah_decap,

		xmit_setup:         ipsec_xmit_ah_setup,
		xmit_headroom:      sizeof(struct ahhdr),
		xmit_needtailroom:  0,
	},
};


#ifdef NET_26
struct inet_protocol ah_protocol = {
  .handler = ipsec_rcv,
  .no_policy = 1,
};
#else
struct inet_protocol ah_protocol =
{
	ipsec_rcv,				/* AH handler */
	NULL,				/* TUNNEL error control */
#ifdef NETDEV_25
	1,				/* no policy */
#else
	0,				/* next */
	IPPROTO_AH,			/* protocol ID */
	0,				/* copy */
	NULL,				/* data */
	"AH"				/* name */
#endif
};
#endif /* NET_26 */

/*
 * $Log: ipsec_ah.c,v $
 * Revision 1.12.2.1  2006/02/15 05:35:14  paul
 * Patch by  David McCullough <davidm@snapgear.com>
 * If you setup a tunnel without ESP it doesn't work.  It used to work in
 * an older openswan version but stopped when klips was modified to deal
 * with the pulled IP header on the received SKB's.
 *
 * The code in ipsec_ah.c still thinks the IP header is there and runs the
 * hash on the incorrect data.
 *
 * Revision 1.12  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.11  2005/04/15 19:50:55  mcr
 * 	adjustments to use proper skb fields for data.
 *
 * Revision 1.10  2004/09/14 00:22:57  mcr
 * 	adjustment of MD5* functions.
 *
 * Revision 1.9  2004/09/13 02:22:47  mcr
 * 	#define inet_protocol if necessary.
 *
 * Revision 1.8  2004/09/06 18:35:48  mcr
 * 	2.6.8.1 gets rid of inet_protocol->net_protocol compatibility,
 * 	so adjust for that.
 *
 * Revision 1.7  2004/08/22 05:00:48  mcr
 * 	if we choose to compile the file, we want the contents,
 * 	so don't pull any punches.
 *
 * Revision 1.6  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
 *
 * Revision 1.5  2004/08/14 03:28:24  mcr
 * 	fixed log comment to remove warning about embedded comment.
 *
 * Revision 1.4  2004/08/04 15:57:07  mcr
 * 	moved des .h files to include/des/ *
 * 	included 2.6 protocol specific things
 * 	started at NAT-T support, but it will require a kernel patch.
 *
 * Revision 1.3  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.2  2004/04/06 02:49:25  mcr
 * 	pullup of algo code from alg-branch.
 *
 *
 *
 */
