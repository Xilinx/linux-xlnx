/*
 * processing code for ESP
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

char ipsec_esp_c_version[] = "RCSID $Id: ipsec_esp.c,v 1.13.2.4 2006/05/06 03:07:38 ken Exp $";
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

#ifdef CONFIG_KLIPS_ESP
#include "openswan/ipsec_esp.h"
#endif /* CONFIG_KLIPS_ESP */

#include "openswan/ipsec_proto.h"
#include "openswan/ipsec_alg.h"

#ifdef CONFIG_KLIPS_DEBUG
#define ESP_DMP(_x,_y,_z) if(debug_rcv && sysctl_ipsec_debug_verbose) ipsec_dmp_block(_x,_y,_z)
#else
#define ESP_DMP(_x,_y,_z)
#endif

#ifdef CONFIG_KLIPS_ESP
enum ipsec_rcv_value
ipsec_rcv_esp_checks(struct ipsec_rcv_state *irs,
		     struct sk_buff *skb)
{
	__u8 proto;
	int len;	/* packet length */

	len = skb->len;
	proto = irs->ipp->protocol;

	/* XXX this will need to be 8 for IPv6 */
	if ((proto == IPPROTO_ESP) && ((len - irs->iphlen) % 4)) {
		printk("klips_error:ipsec_rcv: "
		       "got packet with content length = %d from %s -- should be on 4 octet boundary, packet dropped\n",
		       len - irs->iphlen,
		       irs->ipsaddr_txt);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BADLEN;
	}

	if(skb->len < (irs->hard_header_len + sizeof(struct iphdr) + sizeof(struct esphdr))) {
		KLIPS_PRINT(debug_rcv & DB_RX_INAU,
			    "klips_debug:ipsec_rcv: "
			    "runt esp packet of skb->len=%d received from %s, dropped.\n",
			    skb->len,
			    irs->ipsaddr_txt);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BADLEN;
	}

	irs->protostuff.espstuff.espp = (struct esphdr *)skb->h.raw;
	irs->said.spi = irs->protostuff.espstuff.espp->esp_spi;

	return IPSEC_RCV_OK;
}

enum ipsec_rcv_value
ipsec_rcv_esp_decrypt_setup(struct ipsec_rcv_state *irs,
			    struct sk_buff *skb,
			    __u32          *replay,
			    unsigned char **authenticator)
{
	struct esphdr *espp = irs->protostuff.espstuff.espp;
	//unsigned char *idat = (unsigned char *)espp;

	KLIPS_PRINT(debug_rcv,
		    "klips_debug:ipsec_rcv: "
		    "packet from %s received with seq=%d (iv)=0x%08x%08x iplen=%d esplen=%d sa=%s\n",
		    irs->ipsaddr_txt,
		    (__u32)ntohl(espp->esp_rpl),
		    (__u32)ntohl(*((__u32 *)(espp->esp_iv)    )),
		    (__u32)ntohl(*((__u32 *)(espp->esp_iv) + 1)),
		    irs->len,
		    irs->ilen,
		    irs->sa_len ? irs->sa : " (error)");

	*replay = ntohl(espp->esp_rpl);
	*authenticator = &(skb->h.raw[irs->ilen]);

	return IPSEC_RCV_OK;
}

enum ipsec_rcv_value
ipsec_rcv_esp_authcalc(struct ipsec_rcv_state *irs,
		       struct sk_buff *skb)
{
	struct auth_alg *aa;
	struct esphdr *espp = irs->protostuff.espstuff.espp;
	union {
		MD5_CTX		md5;
		SHA1_CTX	sha1;
	} tctx;

#ifdef CONFIG_KLIPS_OCF
	if (irs->ipsp->ocf_in_use)
		return(ipsec_ocf_rcv(irs));
#endif

#ifdef CONFIG_KLIPS_ALG
	if (irs->ipsp->ips_alg_auth) {
		KLIPS_PRINT(debug_rcv,
				"klips_debug:ipsec_rcv: "
				"ipsec_alg hashing proto=%d... ",
				irs->said.proto);
		if(irs->said.proto == IPPROTO_ESP) {
			ipsec_alg_sa_esp_hash(irs->ipsp,
					(caddr_t)espp, irs->ilen,
					irs->hash, AHHMAC_HASHLEN);
			return IPSEC_RCV_OK;
		}
		return IPSEC_RCV_BADPROTO;
	}
#endif
	aa = irs->authfuncs;

	/* copy the initialized keying material */
	memcpy(&tctx, irs->ictx, irs->ictx_len);

#ifdef HASH_DEBUG
	ESP_DMP("ictx", irs->ictx, irs->ictx_len);

	ESP_DMP("mac_esp", (caddr_t)espp, irs->ilen);
#endif
	(*aa->update)((void *)&tctx, (caddr_t)espp, irs->ilen);

	(*aa->final)(irs->hash, (void *)&tctx);

#ifdef HASH_DEBUG
	ESP_DMP("hash1", irs->hash, aa->hashlen);
#endif

	memcpy(&tctx, irs->octx, irs->octx_len);

#ifdef HASH_DEBUG
	ESP_DMP("octx", irs->octx, irs->octx_len);
#endif

	(*aa->update)((void *)&tctx, irs->hash, aa->hashlen);
	(*aa->final)(irs->hash, (void *)&tctx);

	return IPSEC_RCV_OK;
}


enum ipsec_rcv_value
ipsec_rcv_esp_decrypt(struct ipsec_rcv_state *irs)
{
	struct ipsec_sa *ipsp = irs->ipsp;
	struct esphdr *espp = irs->protostuff.espstuff.espp;
	__u8 *idat;	/* pointer to content to be decrypted/authenticated */
	int encaplen = 0;
	struct sk_buff *skb;
	struct ipsec_alg_enc *ixt_e=NULL;

#ifdef CONFIG_KLIPS_OCF
	if (ipsp->ocf_in_use)
		return(ipsec_ocf_rcv(irs));
#endif

#ifdef CONFIG_KLIPS_ALG
	skb=irs->skb;

	idat = skb->h.raw;

	/* encaplen is the distance between the end of the IP
	 * header and the beginning of the ESP header.
	 * on ESP headers it is zero, but on UDP-encap ESP
	 * it includes the space for the UDP header.
	 *
	 * Note: UDP-encap code has already moved the
	 *       skb->data forward to accomodate this.
	 */
	encaplen = skb->h.raw - (skb->nh.raw + irs->iphlen);

	ixt_e=ipsp->ips_alg_enc;
	irs->esphlen = ESP_HEADER_LEN + ixt_e->ixt_common.ixt_support.ias_ivlen/8;
	KLIPS_PRINT(debug_rcv,
		    "klips_debug:ipsec_rcv: "
		    "encalg=%d esphlen=%d\n",
		    ipsp->ips_encalg, irs->esphlen);

	idat += irs->esphlen;
	irs->ilen -= irs->esphlen;

	if (ipsec_alg_esp_encrypt(ipsp, 
				  idat, irs->ilen, espp->esp_iv, 
				  IPSEC_ALG_DECRYPT) <= 0) {
		KLIPS_ERROR(debug_rcv, "klips_error:ipsec_rcv: "
			    "got packet with esplen = %d "
			    "from %s -- should be on "
			    "ENC(%d) octet boundary, "
			    "packet dropped\n",
			    irs->ilen,
			    irs->ipsaddr_txt,
			    ipsp->ips_encalg);
		if(irs->stats) {
			irs->stats->rx_errors++;
		}
		return IPSEC_RCV_BAD_DECRYPT;
	} 

	return ipsec_rcv_esp_post_decrypt(irs);
#else
	return IPSEC_RCV_BAD_DECRYPT;
#endif /* CONFIG_KLIPS_ALG */
}


enum ipsec_rcv_value
ipsec_rcv_esp_post_decrypt(struct ipsec_rcv_state *irs)
{
	struct sk_buff *skb;
	__u8 *idat;	/* pointer to content to be decrypted/authenticated */
	struct ipsec_sa *ipsp = irs->ipsp;
	int pad = 0, padlen;
	int badpad = 0;
	int i;

	skb = irs->skb;

	idat = skb->h.raw + irs->esphlen;

	ESP_DMP("postdecrypt", idat, irs->ilen);

	irs->next_header = idat[irs->ilen - 1];
	padlen = idat[irs->ilen - 2];
	pad = padlen + 2 + irs->authlen;

	KLIPS_PRINT(debug_rcv & DB_RX_IPAD,
		    "klips_debug:ipsec_rcv_esp_post_decrypt: "
		    "padlen=%d, contents: 0x<offset>: 0x<value> 0x<value> ...\n",
		    padlen);

	for (i = 1; i <= padlen; i++) {
		if((i % 16) == 1) {
			KLIPS_PRINT(debug_rcv & DB_RX_IPAD,
				    "klips_debug:           %02x:",
				    i - 1);
		}
		KLIPS_PRINTMORE(debug_rcv & DB_RX_IPAD,
				" %02x",
				idat[irs->ilen - 2 - padlen + i - 1]);
		if(i != idat[irs->ilen - 2 - padlen + i - 1]) {
			badpad = 1;
		}
		if((i % 16) == 0) {
			KLIPS_PRINTMORE(debug_rcv & DB_RX_IPAD,
					"\n");
		}
	}
	if((i % 16) != 1) {
		KLIPS_PRINTMORE(debug_rcv & DB_RX_IPAD,
						"\n");
	}
	if(badpad) {
		KLIPS_PRINT(debug_rcv & DB_RX_IPAD,
			    "klips_debug:ipsec_rcv_esp_post_decrypt: "
			    "warning, decrypted packet from %s has bad padding\n",
			    irs->ipsaddr_txt);
		KLIPS_PRINT(debug_rcv & DB_RX_IPAD,
			    "klips_debug:ipsec_rcv_esp_post_decrypt: "
			    "...may be bad decryption -- not dropped\n");
		ipsp->ips_errs.ips_encpad_errs += 1;
	}

	KLIPS_PRINT(debug_rcv & DB_RX_IPAD,
		    "klips_debug:ipsec_rcv_esp_post_decrypt: "
		    "packet decrypted from %s: next_header = %d, padding = %d\n",
		    irs->ipsaddr_txt,
		    irs->next_header,
		    pad - 2 - irs->authlen);

	irs->ipp->tot_len = htons(ntohs(irs->ipp->tot_len) - (irs->esphlen + pad));

	/*
	 * move the IP header forward by the size of the ESP header, which
	 * will remove the the ESP header from the packet.
	 *
	 * XXX this is really unnecessary, since odds we are in tunnel
	 *     mode, and we will be *removing* this IP header.
	 *
	 */
	memmove((void *)(idat - irs->iphlen),
		(void *)(skb->nh.raw), irs->iphlen);

	ESP_DMP("esp postmove", (idat - irs->iphlen),
		irs->iphlen + irs->ilen);

	/* skb_pull below, will move up by esphlen */

	/* XXX not clear how this can happen, as the message indicates */
	if(skb->len < irs->esphlen) {
		printk(KERN_WARNING
		       "klips_error:ipsec_rcv_esp_post_decrypt: "
		       "tried to skb_pull esphlen=%d, %d available.  This should never happen, please report.\n",
		       irs->esphlen, (int)(skb->len));
		return IPSEC_RCV_ESP_DECAPFAIL;
	}
	skb_pull(skb, irs->esphlen);
	skb->nh.raw = idat - irs->iphlen;
	irs->ipp = skb->nh.iph;

	ESP_DMP("esp postpull", skb->data, skb->len);

	/* now, trip off the padding from the end */
	KLIPS_PRINT(debug_rcv & DB_RX_PKTRX,
		    "klips_debug:ipsec_rcv: "
		    "trimming to %d.\n",
		    irs->len - irs->esphlen - pad);
	if(pad + irs->esphlen <= irs->len) {
		skb_trim(skb, irs->len - irs->esphlen - pad);
	} else {
		KLIPS_PRINT(debug_rcv & DB_RX_PKTRX,
			    "klips_debug:ipsec_rcv: "
			    "bogus packet, size is zero or negative, dropping.\n");
		return IPSEC_RCV_DECAPFAIL;
	}

	return IPSEC_RCV_OK;
}

/*
 *
 */
enum ipsec_xmit_value
ipsec_xmit_esp_setup(struct ipsec_xmit_state *ixs)
{
#ifdef CONFIG_KLIPS_ENC_3DES
  __u32 iv[2];
#endif
  struct esphdr *espp;
  int ilen = 0;
  int padlen = 0, i;
  unsigned char *dat;
  unsigned char *idat, *pad;
  __u8 hash[AH_AMAX];
  union {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
    MD5_CTX md5;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
    SHA1_CTX sha1;
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
  } tctx;

  dat = (unsigned char *)ixs->iph;

  espp = (struct esphdr *)(dat + ixs->iphlen);
  espp->esp_spi = ixs->ipsp->ips_said.spi;
  espp->esp_rpl = htonl(++(ixs->ipsp->ips_replaywin_lastseq));
  
  switch(ixs->ipsp->ips_encalg) {
#if defined(CONFIG_KLIPS_ENC_3DES)
#ifdef CONFIG_KLIPS_ENC_3DES
  case ESP_3DES:
#endif /* CONFIG_KLIPS_ENC_3DES */
    iv[0] = *((__u32*)&(espp->esp_iv)    ) =
      ((__u32*)(ixs->ipsp->ips_iv))[0];
    iv[1] = *((__u32*)&(espp->esp_iv) + 1) =
      ((__u32*)(ixs->ipsp->ips_iv))[1];
    break;
#endif /* defined(CONFIG_KLIPS_ENC_3DES) */
  default:
    ixs->stats->tx_errors++;
    return IPSEC_XMIT_ESP_BADALG;
  }
		
  idat = dat + ixs->iphlen + sizeof(struct esphdr);
  ilen = ixs->skb->len - (ixs->iphlen + sizeof(struct esphdr) + ixs->authlen);
  
  /* Self-describing padding */
  pad = &dat[ixs->skb->len - ixs->tailroom];
  padlen = ixs->tailroom - 2 - ixs->authlen;
  for (i = 0; i < padlen; i++) {
    pad[i] = i + 1; 
  }
  dat[ixs->skb->len - ixs->authlen - 2] = padlen;
  
  dat[ixs->skb->len - ixs->authlen - 1] = ixs->iph->protocol;
  ixs->iph->protocol = IPPROTO_ESP;
  
  switch(ixs->ipsp->ips_encalg) {
#ifdef CONFIG_KLIPS_ENC_3DES
  case ESP_3DES:
    des_ede3_cbc_encrypt((des_cblock *)idat,
			 (des_cblock *)idat,
			 ilen,
			 ((struct des_eks *)(ixs->ipsp->ips_key_e))[0].ks,
			 ((struct des_eks *)(ixs->ipsp->ips_key_e))[1].ks,
			 ((struct des_eks *)(ixs->ipsp->ips_key_e))[2].ks,
			 (des_cblock *)iv, 1);
    break;
#endif /* CONFIG_KLIPS_ENC_3DES */
  default:
    ixs->stats->tx_errors++;
    return IPSEC_XMIT_ESP_BADALG;
  }
  
  switch(ixs->ipsp->ips_encalg) {
#if defined(CONFIG_KLIPS_ENC_3DES)
#ifdef CONFIG_KLIPS_ENC_3DES
  case ESP_3DES:
#endif /* CONFIG_KLIPS_ENC_3DES */
    /* XXX update IV with the last 8 octets of the encryption */
#if KLIPS_IMPAIRMENT_ESPIV_CBC_ATTACK
    ((__u32*)(ixs->ipsp->ips_iv))[0] =
      ((__u32 *)(idat))[(ilen >> 2) - 2];
    ((__u32*)(ixs->ipsp->ips_iv))[1] =
      ((__u32 *)(idat))[(ilen >> 2) - 1];
#else /* KLIPS_IMPAIRMENT_ESPIV_CBC_ATTACK */
    prng_bytes(&ipsec_prng, (char *)ixs->ipsp->ips_iv, EMT_ESPDES_IV_SZ); 
#endif /* KLIPS_IMPAIRMENT_ESPIV_CBC_ATTACK */
    break;
#endif /* defined(CONFIG_KLIPS_ENC_3DES) */
  default:
    ixs->stats->tx_errors++;
    return IPSEC_XMIT_ESP_BADALG;
  }
  
  switch(ixs->ipsp->ips_authalg) {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
  case AH_MD5:
    ipsec_xmit_dmp("espp", (char*)espp, ixs->skb->len - ixs->iphlen - ixs->authlen);
    tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->ictx;
    ipsec_xmit_dmp("ictx", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, (caddr_t)espp, ixs->skb->len - ixs->iphlen - ixs->authlen);
    ipsec_xmit_dmp("ictx+dat", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Final(hash, &tctx.md5);
    ipsec_xmit_dmp("ictx hash", (char*)&hash, sizeof(hash));
    tctx.md5 = ((struct md5_ctx*)(ixs->ipsp->ips_key_a))->octx;
    ipsec_xmit_dmp("octx", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Update(&tctx.md5, hash, AHMD596_ALEN);
    ipsec_xmit_dmp("octx+hash", (char*)&tctx.md5, sizeof(tctx.md5));
    osMD5Final(hash, &tctx.md5);
    ipsec_xmit_dmp("octx hash", (char*)&hash, sizeof(hash));
    memcpy(&(dat[ixs->skb->len - ixs->authlen]), hash, ixs->authlen);
    
    /* paranoid */
    memset((caddr_t)&tctx.md5, 0, sizeof(tctx.md5));
    memset((caddr_t)hash, 0, sizeof(*hash));
    break;
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
  case AH_SHA:
    tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->ictx;
    SHA1Update(&tctx.sha1, (caddr_t)espp, ixs->skb->len - ixs->iphlen - ixs->authlen);
    SHA1Final(hash, &tctx.sha1);
    tctx.sha1 = ((struct sha1_ctx*)(ixs->ipsp->ips_key_a))->octx;
    SHA1Update(&tctx.sha1, hash, AHSHA196_ALEN);
    SHA1Final(hash, &tctx.sha1);
    memcpy(&(dat[ixs->skb->len - ixs->authlen]), hash, ixs->authlen);
    
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

  ixs->skb->h.raw = (unsigned char*)espp;

  return IPSEC_XMIT_OK;
}


struct xform_functions esp_xform_funcs[]={
	{	rcv_checks:         ipsec_rcv_esp_checks,
		rcv_setup_auth:     ipsec_rcv_esp_decrypt_setup,
		rcv_calc_auth:      ipsec_rcv_esp_authcalc,
		rcv_decrypt:        ipsec_rcv_esp_decrypt,

		xmit_setup:         ipsec_xmit_esp_setup,
		xmit_headroom:      sizeof(struct esphdr),
		xmit_needtailroom:  1,
	},
};

#ifdef NET_26
struct inet_protocol esp_protocol = {
  .handler = ipsec_rcv,
  .no_policy = 1,
};
#else
struct inet_protocol esp_protocol =
{
	ipsec_rcv,			/* ESP handler		*/
	NULL,				/* TUNNEL error control */
#ifdef NETDEV_25
	1,				/* no policy */
#else
	0,				/* next */
	IPPROTO_ESP,			/* protocol ID */
	0,				/* copy */
	NULL,				/* data */
	"ESP"				/* name */
#endif
};
#endif /* NET_26 */

#endif /* !CONFIG_KLIPS_ESP */


/*
 * $Log: ipsec_esp.c,v $
 * Revision 1.13.2.4  2006/05/06 03:07:38  ken
 * Pull in proper padsize->tailroom fix from #public
 * Need to do correct math on padlen since padsize is not equal to tailroom
 *
 * Revision 1.13.2.3  2006/05/05 03:58:04  ken
 * ixs->padsize becomes ixs->tailroom
 *
 * Revision 1.13.2.2  2006/05/01 14:36:03  mcr
 * use KLIPS_ERROR for fatal things.
 *
 * Revision 1.13.2.1  2006/04/20 16:33:06  mcr
 * remove all of CONFIG_KLIPS_ALG --- one can no longer build without it.
 * Fix in-kernel module compilation. Sub-makefiles do not work.
 *
 * Revision 1.13  2005/05/21 03:19:57  mcr
 * 	hash ctx is not really that interesting most of the time.
 *
 * Revision 1.12  2005/05/11 01:28:49  mcr
 * 	removed "poor-man"s OOP in favour of proper C structures.
 *
 * Revision 1.11  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.10  2005/04/17 04:36:14  mcr
 * 	code now deals with ESP and UDP-ESP code.
 *
 * Revision 1.9  2005/04/15 19:52:30  mcr
 * 	adjustments to use proper skb fields for data.
 *
 * Revision 1.8  2004/09/14 00:22:57  mcr
 * 	adjustment of MD5* functions.
 *
 * Revision 1.7  2004/09/13 02:23:01  mcr
 * 	#define inet_protocol if necessary.
 *
 * Revision 1.6  2004/09/06 18:35:49  mcr
 * 	2.6.8.1 gets rid of inet_protocol->net_protocol compatibility,
 * 	so adjust for that.
 *
 * Revision 1.5  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
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
