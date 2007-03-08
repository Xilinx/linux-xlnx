/*
 * IPSEC OCF support
 *
 * This code written by David McCullough <dmccullough@cyberguard.com>
 * Copyright (C) 2005 Intel Corporation.  All Rights Reserved.
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

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */

#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */

#include <linux/interrupt.h>

#include <net/ip.h>

#include <openswan.h>
#include "openswan/ipsec_sa.h"
#include "openswan/ipsec_rcv.h"
#include "openswan/ipsec_xmit.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_auth.h"
#include "openswan/ipsec_esp.h"
#include "openswan/ipsec_ah.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#include "ipsec_ocf.h"

extern int debug_pfkey;
extern int debug_rcv;

/*
 * Tuning parameters,  the settings below appear best for
 * the IXP
 */
#define USE_BATCH 1	/* enable batch mode */
#define USE_CBIMM 1	/* enable immediate callbacks */
#define FORCE_QS  0	/* force use of queues for continuation of state machine */

/*
 * Because some OCF operations are synchronous (ie., software encryption)
 * we need to protect ourselves from distructive re-entry.  All we do
 * is track where we are at and either callback  immediately or Q the
 * callback to avoid conflicts.  This allows us to deal with the fact that
 * OCF doesn't tell us if our crypto operations will be async or sync.
 */

#define PROCESS_LATER(wq, sm, arg) \
	({ \
		INIT_WORK(&(wq), (void (*)(void *))(sm), (void *)(arg)); \
		schedule_work(&(wq)); \
	})

#define PROCESS_NOW(wq, sm, arg) \
	({ \
		(*sm)(arg); \
	})

#if FORCE_QS == 0
	#define PROCESS_NEXT(wq, sm, arg) \
		if (in_interrupt()) { \
			PROCESS_LATER(wq, sm, arg); \
		} else { \
			PROCESS_NOW(wq, sm, arg); \
		}
#else
	#define PROCESS_NEXT(wq, sm, arg) PROCESS_LATER(wq, sm, arg)
#endif

/*
 * convert openswan values to OCF values
 */

static int
ipsec_ocf_authalg(int authalg)
{
	switch (authalg) {
	case AH_SHA:  return CRYPTO_SHA1_HMAC;
	case AH_MD5:  return CRYPTO_MD5_HMAC;
	}
	return 0;
}


static int
ipsec_ocf_encalg(int encalg)
{
	switch (encalg) {
	case ESP_NULL:      return CRYPTO_NULL_CBC;
	case ESP_DES:       return CRYPTO_DES_CBC;
	case ESP_3DES:      return CRYPTO_3DES_CBC;
	case ESP_AES:       return CRYPTO_AES_CBC;
	case ESP_CAST:      return CRYPTO_CAST_CBC;
	case ESP_BLOWFISH:  return CRYPTO_BLF_CBC;
	}
	return 0;
}

/*
 * if we can do the request ops, setup the sessions and return true
 * otherwise return false with ipsp unchanged
 */

int
ipsec_ocf_sa_init(struct ipsec_sa *ipsp, int authalg, int encalg)
{
	struct cryptoini crie, cria;
	int error;

	KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf_sa_init(a=0x%x,e=0x%x)\n",
			authalg, encalg);

	if (authalg && ipsp->ips_key_bits_a == 0) {
		KLIPS_PRINT(debug_pfkey,
				"klips_debug:ipsec_ocf_sa_init(a=0x%x,e=0x%x) a-key-bits=0\n",
				authalg, encalg);
		/* pretend we are happy with this */
		return 1;
	}

	if (encalg && ipsp->ips_key_bits_e == 0) {
		KLIPS_PRINT(debug_pfkey,
				"klips_debug:ipsec_ocf_sa_init(a=0x%x,e=0x%x) e-key-bits=0\n",
				authalg, encalg);
		/* pretend we are happy with this */
		return 1;
	}

	memset(&crie, 0, sizeof(crie));
	memset(&cria, 0, sizeof(cria));

	cria.cri_alg = ipsec_ocf_authalg(authalg);
	cria.cri_klen = ipsp->ips_key_bits_a;
	cria.cri_key  = ipsp->ips_key_a;

	crie.cri_alg = ipsec_ocf_encalg(encalg);
	crie.cri_klen = ipsp->ips_key_bits_e;
	crie.cri_key  = ipsp->ips_key_e;
	switch (crie.cri_alg) {
	case CRYPTO_AES_CBC:
		ipsp->ips_iv_size = 16;
		break;
	case CRYPTO_DES_CBC:
	case CRYPTO_3DES_CBC:
		ipsp->ips_iv_size = 8;
		break;
	default:
		ipsp->ips_iv_size = 0;
		break;
	}
	ipsp->ips_iv_bits = ipsp->ips_iv_size * 8;
	ipsp->ips_auth_bits = ipsp->ips_key_bits_a;

	if (authalg && encalg) {
		crie.cri_next = &cria;
		error = crypto_newsession(&ipsp->ocf_cryptoid, &crie, 0);
	} else if (encalg) {
		error = crypto_newsession(&ipsp->ocf_cryptoid, &crie, 0);
	} else if (authalg) {
		error = crypto_newsession(&ipsp->ocf_cryptoid, &cria, 0);
	} else {
		KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf_sa_init: "
				"no authalg or encalg\n");
		return 0;
	}

	if (error) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf_sa_init: "
				"crypto_newsession failed 0x%x\n", error);
		return 0;
	}

	/* make sure no ALG stuff bites us */
	if (ipsp->ips_alg_enc)
		printk("We received an ALG initted SA\n");
	ipsp->ips_alg_enc = NULL;

	ipsp->ocf_in_use = 1;
	return 1;
}


int
ipsec_ocf_sa_free(struct ipsec_sa *ipsp)
{
	KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf_sa_free()\n");
	crypto_freesession(ipsp->ocf_cryptoid);
	ipsp->ocf_cryptoid = -1;
	ipsp->ocf_in_use = 0;
	return 1;
}


static int
ipsec_ocf_rcv_cb(struct cryptop *crp)
{
	struct ipsec_rcv_state *irs = (struct ipsec_rcv_state *)crp->crp_opaque;

	KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv_cb\n");

	if (irs == NULL) {
		KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv_cb: "
				"NULL irs in callback\n");
		return 0;
	}

	/*
	 * we must update the state before returning to the state machine.
	 * if we have an error,  terminate the processing by moving to the DONE
	 * state
	 */

	irs->state = IPSEC_RSM_DONE; /* assume it went badly */
	if (crp->crp_etype) {
		KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv_cb: "
				"error in processing 0x%x\n", crp->crp_etype);
	} else {
		if (!irs->ipsp->ips_encalg) {
			/* AH post processing, put back fields we had to zero */
			irs->ipp->ttl      = irs->ttl;
			irs->ipp->check    = irs->check;
			irs->ipp->frag_off = irs->frag_off;
			irs->ipp->tos      = irs->tos;
			irs->state         = IPSEC_RSM_AUTH_CHK;
			/* pull up the IP header again after processing */
			skb_pull(irs->skb, ((unsigned char *)irs->protostuff.ahstuff.ahp) -
								((unsigned char *)irs->ipp));
		} else if (ipsec_rcv_esp_post_decrypt(irs) == IPSEC_RCV_OK) {
			/* this one came up good, set next state */
			irs->state         = IPSEC_RSM_DECAP_CONT;
		}
	}

	crypto_freereq(crp);
	crp = NULL;

	/* setup the rest of the processing now */
	PROCESS_NEXT(irs->workq, ipsec_rsm, irs);
	return 0;
}

enum ipsec_rcv_value
ipsec_ocf_rcv(struct ipsec_rcv_state *irs)
{
	struct cryptop *crp;
	struct cryptodesc *crde, *crda = NULL;
	struct ipsec_sa *ipsp;

	KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv\n");

	ipsp = irs->ipsp;
	if (!ipsp) {
		KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv: "
				"no SA for rcv processing\n");
		return IPSEC_RCV_SAIDNOTFOUND;
	}

	if (!irs->skb) {
		KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv: no skb\n");
		return IPSEC_RCV_SAIDNOTFOUND;
	}

	crp = crypto_getreq((ipsp->ips_authalg && ipsp->ips_encalg) ? 2 : 1);
	if (!crp) {
		KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv: "
				"crypto_getreq returned NULL\n");
		return IPSEC_RCV_REALLYBAD;
	}

	if (ipsp->ips_authalg) {
		crda = crp->crp_desc;
		crde = crda->crd_next;
	} else {
		crde = crp->crp_desc;
		crda = crde->crd_next;
	}

	if (crda) {
		/* Authentication descriptor */
		crda->crd_alg = ipsec_ocf_authalg(ipsp->ips_authalg);
		if (!crda->crd_alg) {
			KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv: "
					"bad auth alg 0x%x\n", ipsp->ips_authalg);
			return IPSEC_RCV_BADPROTO;
		}

		if (!crde) { /* assuming AH processing */
			/* push the IP header so we can authenticate it */
			skb_push(irs->skb, ((unsigned char *)irs->protostuff.ahstuff.ahp) -
								((unsigned char *)irs->ipp));
		}

		crda->crd_key          = ipsp->ips_key_a;
		crda->crd_klen         = ipsp->ips_key_bits_a;
		crda->crd_inject       = irs->authenticator - irs->skb->data;
		/* Copy the authenticator to check aganinst later */
		memcpy(irs->hash, irs->authenticator, 12);

		if (!crde) { /* assume AH processing */
			/* AH processing, save fields we have to zero */
			irs->ttl           = irs->ipp->ttl;
			irs->check         = irs->ipp->check;
			irs->frag_off      = irs->ipp->frag_off;
			irs->tos           = irs->ipp->tos;
			irs->ipp->ttl      = 0;
			irs->ipp->check    = 0;
			irs->ipp->frag_off = 0;
			irs->ipp->tos      = 0;
			crda->crd_len      = irs->skb->len;
			crda->crd_skip     = ((unsigned char *)irs->ipp) - irs->skb->data;
			memset(irs->authenticator, 0, 12);
		} else {
			crda->crd_len      = irs->ilen;
			crda->crd_skip     =
				((unsigned char *) irs->protostuff.espstuff.espp) -
							irs->skb->data;
			/* clear the authenticator to be sure */
			/* FIXME: don't do this as some drivers actually check this data */
			/* need to work out a cleaner way to ensure we do not see */
			/* the old value from the packet later */
			//memset(irs->authenticator, 0, 12);
		}
	}

	if (crde) {
		crde->crd_alg = ipsec_ocf_encalg(ipsp->ips_encalg);
		if (!crde->crd_alg) {
			KLIPS_PRINT(debug_rcv, "klips_debug:ipsec_ocf_rcv: "
					"bad enc alg 0x%x\n", ipsp->ips_encalg);
			return IPSEC_RCV_BADPROTO;
		}

		irs->esphlen     = ESP_HEADER_LEN + ipsp->ips_iv_size;
		irs->ilen       -= irs->esphlen;
		crde->crd_skip   = (irs->skb->h.raw - irs->skb->data) + irs->esphlen;
		crde->crd_len    = irs->ilen;
		crde->crd_inject = crde->crd_skip - ipsp->ips_iv_size;
		crde->crd_klen   = ipsp->ips_key_bits_e;
		crde->crd_key    = ipsp->ips_key_e;
	}

	crp->crp_ilen = irs->skb->len; /* Total input length */
	crp->crp_flags =
			CRYPTO_F_SKBUF |
#if USE_CBIMM == 1
			CRYPTO_F_CBIMM |
#endif
#if USE_BATCH == 1
			CRYPTO_F_BATCH |
#endif
			0;
	crp->crp_buf = (caddr_t) irs->skb;
	crp->crp_callback = ipsec_ocf_rcv_cb;
	crp->crp_sid = ipsp->ocf_cryptoid;
	crp->crp_opaque = (caddr_t) irs;
	crypto_dispatch(crp);
	return(IPSEC_RCV_PENDING);
}


static int
ipsec_ocf_xmit_cb(struct cryptop *crp)
{
	struct ipsec_xmit_state *ixs = (struct ipsec_xmit_state *)crp->crp_opaque;

	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit_cb\n");

	if (ixs == NULL) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit_cb: "
				"NULL ixs in callback\n");
		return 0;
	}

	/*
	 * we must update the state before returning to the state machine.
	 * if we have an error,  terminate the processing by moving to the DONE
	 * state
	 */

	ixs->state = IPSEC_XSM_DONE; /* assume bad xmit */
	if (crp->crp_etype) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit_cb: "
				"error in processing 0x%x\n", crp->crp_etype);
	} else {
		if (!ixs->ipsp->ips_encalg) {
			/* AH post processing, put back fields we had to zero */
			ixs->iph->ttl      = ixs->ttl;
			ixs->iph->check    = ixs->check;
			ixs->iph->frag_off = ixs->frag_off;
			ixs->iph->tos      = ixs->tos;
		}
		ixs->state = IPSEC_XSM_CONT; /* ESP was all good */
	}

	crypto_freereq(crp);
	crp = NULL;

	/* setup the rest of the processing now */
	PROCESS_NEXT(ixs->workq, ipsec_xsm, ixs);
	return 0;
}


enum ipsec_xmit_value
ipsec_ocf_xmit(struct ipsec_xmit_state *ixs)
{
	struct cryptop *crp;
	struct cryptodesc *crde, *crda;
	struct ipsec_sa *ipsp;

	KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit\n");

	ipsp = ixs->ipsp;
	if (!ipsp) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit: "
				"no SA for rcv processing\n");
		return IPSEC_XMIT_SAIDNOTFOUND;
	}

	if (!ixs->skb) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT,
				"klips_debug:ipsec_ocf_xmit: no skb\n");
		return IPSEC_XMIT_SAIDNOTFOUND;
	}

	crp = crypto_getreq((ipsp->ips_authalg && ipsp->ips_encalg) ? 2 : 1);
	if (!crp) {
		KLIPS_PRINT(debug_tunnel & DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit: "
				"crypto_getreq returned NULL\n");
		return IPSEC_XMIT_ERRMEMALLOC;
	}

	if (ipsp->ips_encalg) {
		crde = crp->crp_desc;
		crda = crde->crd_next;
	} else {
		crda = crp->crp_desc;
		crde = crda->crd_next;
	}

	if (crda) {
		/* Authentication descriptor */
		crda->crd_alg = ipsec_ocf_authalg(ipsp->ips_authalg);
		if (!crda->crd_alg) {
			KLIPS_PRINT(debug_tunnel&DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit: "
					"bad auth alg 0x%x\n", ipsp->ips_authalg);
			return IPSEC_RCV_BADPROTO;
		}
		if (!crde) { /* assume AH processing */
			/* AH processing, save fields we have to zero */
			crda->crd_skip     = ((unsigned char *) ixs->iph) - ixs->skb->data;
			ixs->ttl           = ixs->iph->ttl;
			ixs->check         = ixs->iph->check;
			ixs->frag_off      = ixs->iph->frag_off;
			ixs->tos           = ixs->iph->tos;
			ixs->iph->ttl      = 0;
			ixs->iph->check    = 0;
			ixs->iph->frag_off = 0;
			ixs->iph->tos      = 0;
			crda->crd_inject   =
				(((struct ahhdr *)(ixs->dat + ixs->iphlen))->ah_data) -
					ixs->skb->data;
			crda->crd_len      = ixs->len - ixs->authlen;
			memset(ixs->skb->data + crda->crd_inject, 0, 12); // DM
		} else {
			crda->crd_skip     = ((unsigned char *) ixs->espp) - ixs->skb->data;
			crda->crd_inject   = ixs->len - ixs->authlen;
			crda->crd_len      = ixs->len - ixs->iphlen - ixs->authlen;
		}
		crda->crd_key    = ipsp->ips_key_a;
		crda->crd_klen   = ipsp->ips_key_bits_a;
	}

	if (crde) {
		/* Encryption descriptor */
		crde->crd_alg = ipsec_ocf_encalg(ipsp->ips_encalg);
		if (!crde->crd_alg) {
			KLIPS_PRINT(debug_tunnel&DB_TN_XMIT, "klips_debug:ipsec_ocf_xmit: "
					"bad enc alg 0x%x\n", ipsp->ips_encalg);
			return IPSEC_RCV_BADPROTO;
		}
		crde->crd_flags  = CRD_F_ENCRYPT;
		crde->crd_skip   = ixs->idat - ixs->dat;
		crde->crd_len    = ixs->ilen;
		crde->crd_inject = ((unsigned char *) ixs->espp->esp_iv) - ixs->dat;
		crde->crd_klen   = ipsp->ips_key_bits_e;
		crde->crd_key    = ipsp->ips_key_e;
	}

	crp->crp_ilen = ixs->skb->len; /* Total input length */
	crp->crp_flags =
			CRYPTO_F_SKBUF |
#if USE_CBIMM == 1
			CRYPTO_F_CBIMM |
#endif
#if USE_BATCH == 1
			CRYPTO_F_BATCH |
#endif
			0;
	crp->crp_buf = (caddr_t) ixs->skb;
	crp->crp_callback = ipsec_ocf_xmit_cb;
	crp->crp_sid = ipsp->ocf_cryptoid;
	crp->crp_opaque = (caddr_t) ixs;
	crypto_dispatch(crp);
	return(IPSEC_XMIT_PENDING);
}




#ifdef CONFIG_KLIPS_AH
static struct ipsec_alg_supported ocf_ah_algs[] = {
  {
	  .ias_name       = "ocf-md5hmac",
	  .ias_id         = AH_MD5,
	  .ias_exttype    = SADB_EXT_SUPPORTED_AUTH,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 128,
	  .ias_keymaxbits = 128,
  },
  {
	  .ias_name       = "ocf-sha1hmac",
	  .ias_id         = AH_SHA,
	  .ias_exttype    = SADB_EXT_SUPPORTED_AUTH,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 160,
	  .ias_keymaxbits = 160,
  },
  {
	  .ias_name       = NULL,
	  .ias_id         = 0,
	  .ias_exttype    = 0,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 0,
	  .ias_keymaxbits = 0,
  }
};
#endif /* CONFIG_KLIPS_AH */

static struct ipsec_alg_supported ocf_esp_algs[] = {
  {
	  .ias_name       = "ocf-md5hmac",
	  .ias_id         = AH_MD5,
	  .ias_exttype    = SADB_EXT_SUPPORTED_AUTH,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 128,
	  .ias_keymaxbits = 128,
  },
  {
	  .ias_name       = "ocf-sha1hmac",
	  .ias_id         = AH_SHA,
	  .ias_exttype    = SADB_EXT_SUPPORTED_AUTH,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 160,
	  .ias_keymaxbits = 160,
  },
  {
	  .ias_name       = "ocf-aes",
	  .ias_id         = ESP_AES,
	  .ias_exttype    = SADB_EXT_SUPPORTED_ENCRYPT,
	  .ias_ivlen      = 16,
	  .ias_keyminbits = 128,
	  .ias_keymaxbits = 256,
  },
  {
	  .ias_name       = "ocf-3des",
	  .ias_id         = ESP_3DES,
	  .ias_exttype    = SADB_EXT_SUPPORTED_ENCRYPT,
	  .ias_ivlen      = 8,
	  .ias_keyminbits = 192,
	  .ias_keymaxbits = 192,
  },
  {
	  .ias_name       = "ocf-des",
	  .ias_id         = ESP_DES,
	  .ias_exttype    = SADB_EXT_SUPPORTED_ENCRYPT,
	  .ias_ivlen      = 8,
	  .ias_keyminbits = 64,
	  .ias_keymaxbits = 64,
  },
  {
	  .ias_name       = NULL,
	  .ias_id         = 0,
	  .ias_exttype    = 0,
	  .ias_ivlen      = 0,
	  .ias_keyminbits = 0,
	  .ias_keymaxbits = 0,
  }
};

static int
ipsec_ocf_check_alg(struct ipsec_alg_supported *s)
{
	struct cryptoini cri;
	int64_t cryptoid;

	memset(&cri, 0, sizeof(cri));
	if (s->ias_exttype == SADB_EXT_SUPPORTED_ENCRYPT)
		cri.cri_alg  = ipsec_ocf_encalg(s->ias_id);
	else
		cri.cri_alg  = ipsec_ocf_authalg(s->ias_id);
	cri.cri_klen     = s->ias_keyminbits;
	cri.cri_key      = "0123456789abcdefghijklmnopqrstuvwxyz";

	if (crypto_newsession(&cryptoid, &cri, 0)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf:%s not supported\n",
				s->ias_name);
		return 0;
	}
	crypto_freesession(cryptoid);
	KLIPS_PRINT(debug_pfkey, "klips_debug:ipsec_ocf:%s supported\n",
			s->ias_name);
	return 1;
}

void
ipsec_ocf_init(void)
{
	struct ipsec_alg_supported *s;

	for (s = ocf_esp_algs; s->ias_name; s++) {
		if (ipsec_ocf_check_alg(s))
			(void)pfkey_list_insert_supported(s,
					&(pfkey_supported_list[SADB_SATYPE_ESP]));
	}

#ifdef CONFIG_KLIPS_AH
	for (s = ocf_ah_algs; s->ias_name; s++) {
		if (ipsec_ocf_check_alg(s))
			(void)pfkey_list_insert_supported(s,
					&(pfkey_supported_list[SADB_SATYPE_AH]));
	}
#endif

	/* send register event to userspace	*/
	pfkey_register_reply(SADB_SATYPE_ESP, NULL);
	pfkey_register_reply(SADB_SATYPE_AH, NULL);
}

