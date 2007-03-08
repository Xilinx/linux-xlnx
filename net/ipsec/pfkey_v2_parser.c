/*
 * @(#) RFC2367 PF_KEYv2 Key management API message parser
 * Copyright (C) 1999, 2000, 2001  Richard Guy Briggs <rgb@freeswan.org>
 * 
 * OCF support written by David McCullough <dmccullough@cyberguard.com>
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
 *
 * RCSID $Id: pfkey_v2_parser.c,v 1.134.2.1 2006/05/01 14:37:25 mcr Exp $
 */

/*
 *		Template from klips/net/ipsec/ipsec/ipsec_netlink.c.
 */

char pfkey_v2_parser_c_version[] = "$Id: pfkey_v2_parser.c,v 1.134.2.1 2006/05/01 14:37:25 mcr Exp $";

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
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

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/skbuff.h>

#include <openswan.h>

#include <crypto/des.h>

#ifdef SPINLOCK
# ifdef SPINLOCK_23
#  include <linux/spinlock.h> /* *lock* */
# else /* SPINLOCK_23 */
#  include <asm/spinlock.h> /* *lock* */
# endif /* SPINLOCK_23 */
#endif /* SPINLOCK */

#include <linux/in6.h>
#include <net/route.h>

#include <net/ip.h>
#ifdef NETLINK_SOCK
# include <linux/netlink.h>
#else
# include <net/netlink.h>
#endif

#include <linux/random.h>	/* get_random_bytes() */

#include "openswan/radij.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_sa.h"

#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_rcv.h"
#include "openswan/ipcomp.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#include "openswan/ipsec_proto.h"
#include "openswan/ipsec_alg.h"

#include "openswan/ipsec_kern24.h"

#include "ipsec_ocf.h"

#define SENDERR(_x) do { error = -(_x); goto errlab; } while (0)

struct sklist_t {
	struct socket *sk;
	struct sklist_t* next;
} pfkey_sklist_head, *pfkey_sklist, *pfkey_sklist_prev;

__u32 pfkey_msg_seq = 0;


#if 0
#define DUMP_SAID dump_said(&extr->ips->ips_said, __LINE__)
#define DUMP_SAID2 dump_said(&extr.ips->ips_said, __LINE__)
static void dump_said(ip_said *s, int line)
{ 
	char msa[SATOT_BUF];
	size_t msa_len;
	
	msa_len = satot(s, 0, msa, sizeof(msa));
	
	printk("line: %d msa: %s\n", line, msa);
}
#endif


int
pfkey_alloc_eroute(struct eroute** eroute)
{
	int error = 0;
	if(*eroute) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_alloc_eroute: "
			    "eroute struct already allocated\n");
		SENDERR(EEXIST);
	}

	if((*eroute = kmalloc(sizeof(**eroute), GFP_ATOMIC) ) == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_alloc_eroute: "
			    "memory allocation error\n");
		SENDERR(ENOMEM);
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_alloc_eroute: "
		    "allocating %lu bytes for an eroute at 0p%p\n",
		    (unsigned long) sizeof(**eroute), *eroute);

	memset((caddr_t)*eroute, 0, sizeof(**eroute));
	(*eroute)->er_eaddr.sen_len =
		(*eroute)->er_emask.sen_len = sizeof(struct sockaddr_encap);
	(*eroute)->er_eaddr.sen_family =
		(*eroute)->er_emask.sen_family = AF_ENCAP;
	(*eroute)->er_eaddr.sen_type = SENT_IP4;
	(*eroute)->er_emask.sen_type = 255;
	(*eroute)->er_pid = 0;
	(*eroute)->er_count = 0;
	(*eroute)->er_lasttime = jiffies/HZ;

 errlab:
	return(error);
}

DEBUG_NO_STATIC int
pfkey_x_protocol_process(struct sadb_ext *pfkey_ext,
			 struct pfkey_extracted_data *extr)
{
	int error = 0;
	struct sadb_protocol * p = (struct sadb_protocol *)pfkey_ext;

	KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_protocol_process: %p\n", extr);

	if (extr == 0) {
		KLIPS_PRINT(debug_pfkey,
                         "klips_debug:pfkey_x_protocol_process:"
			    "extr is NULL, fatal\n");
		SENDERR(EINVAL);
	}
	if (extr->eroute == 0) {
		KLIPS_PRINT(debug_pfkey,
                        "klips_debug:pfkey_x_protocol_process:"
			    "extr->eroute is NULL, fatal\n");
		SENDERR(EINVAL);
	}

	extr->eroute->er_eaddr.sen_proto = p->sadb_protocol_proto;
	extr->eroute->er_emask.sen_proto = p->sadb_protocol_proto ? ~0:0;
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_protocol_process: protocol = %d.\n",
		    p->sadb_protocol_proto);
 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_ipsec_sa_init(struct ipsec_sa *ipsp)
{
	return ipsec_sa_init(ipsp);
}

int
pfkey_safe_build(int error, struct sadb_ext *extensions[SADB_MAX+1])
{
	KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_safe_build: "
		    "error=%d\n",
		    error);
	if (!error) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_safe_build:"
			    "success.\n");
		return 1;
	} else {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_safe_build:"
			    "caught error %d\n",
			    error);
		pfkey_extensions_free(extensions);
		return 0;
	}
}


DEBUG_NO_STATIC int
pfkey_getspi_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	ipsec_spi_t minspi = htonl(256), maxspi = htonl(-1L);
	int found_avail = 0;
	struct ipsec_sa *ipsq;
	char sa[SATOT_BUF];
	size_t sa_len;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_getspi_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(extr == NULL || extr->ips == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_getspi_parse: "
			    "error, extr or extr->ipsec_sa pointer NULL\n");
		SENDERR(EINVAL);
	}

	if(extensions[SADB_EXT_SPIRANGE]) {
		minspi = ((struct sadb_spirange *)extensions[SADB_EXT_SPIRANGE])->sadb_spirange_min;
		maxspi = ((struct sadb_spirange *)extensions[SADB_EXT_SPIRANGE])->sadb_spirange_max;
	}

	if(maxspi == minspi) {
		extr->ips->ips_said.spi = maxspi;
		ipsq = ipsec_sa_getbyid(&(extr->ips->ips_said));
		if(ipsq != NULL) {
			sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));
			ipsec_sa_put(ipsq);
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_getspi_parse: "
				    "EMT_GETSPI found an old ipsec_sa for SA: %s, delete it first.\n",
				    sa_len ? sa : " (error)");
			SENDERR(EEXIST);
		} else {
			found_avail = 1;
		}
	} else {
		int i = 0;
		__u32 rand_val;
		__u32 spi_diff;
		while( ( i < (spi_diff = (ntohl(maxspi) - ntohl(minspi)))) && !found_avail ) {
			prng_bytes(&ipsec_prng, (char *) &(rand_val),
					 ( (spi_diff < (2^8))  ? 1 :
					   ( (spi_diff < (2^16)) ? 2 :
					     ( (spi_diff < (2^24)) ? 3 :
					   4 ) ) ) );
			extr->ips->ips_said.spi = htonl(ntohl(minspi) +
					      (rand_val %
					      (spi_diff + 1)));
			i++;
			ipsq = ipsec_sa_getbyid(&(extr->ips->ips_said));
			if(ipsq == NULL) {
				found_avail = 1;
			} else {
				ipsec_sa_put(ipsq);
			}
		}
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

	if (!found_avail) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_getspi_parse: "
			    "found an old ipsec_sa for SA: %s, delete it first.\n",
			    sa_len ? sa : " (error)");
		SENDERR(EEXIST);
	}

	if(inet_addr_type((unsigned long)extr->ips->ips_said.dst.u.v4.sin_addr.s_addr) == RTN_LOCAL) {
		extr->ips->ips_flags |= EMT_INBOUND;
	}
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_getspi_parse: "
		    "existing ipsec_sa not found (this is good) for SA: %s, %s-bound, allocating.\n",
		    sa_len ? sa : " (error)",
		    extr->ips->ips_flags & EMT_INBOUND ? "in" : "out");
	
	/* XXX extr->ips->ips_rcvif = &(enc_softc[em->em_if].enc_if);*/
	extr->ips->ips_rcvif = NULL;
	extr->ips->ips_life.ipl_addtime.ipl_count = jiffies/HZ;

	extr->ips->ips_state = SADB_SASTATE_LARVAL;

	if(!extr->ips->ips_life.ipl_allocations.ipl_count) {
		extr->ips->ips_life.ipl_allocations.ipl_count += 1;
	}

	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_GETSPI,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							0,
							SADB_SASTATE_LARVAL,
							0,
							0,
							0,
							extr->ips->ips_ref),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
						     SADB_EXT_ADDRESS_SRC,
						     0, /*extr->ips->ips_said.proto,*/
						     0,
						     extr->ips->ips_addr_s),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
						     SADB_EXT_ADDRESS_DST,
						     0, /*extr->ips->ips_said.proto,*/
						     0,
						     extr->ips->ips_addr_d),
				 extensions_reply) )) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_getspi_parse: "
			    "failed to build the getspi reply message extensions\n");
		goto errlab;
	}
	
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_getspi_parse: "
			    "failed to build the getspi reply message\n");
		SENDERR(-error);
	}
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_getspi_parse: "
				    "sending up getspi reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_getspi_parse: "
			    "sending up getspi reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
	if((error = ipsec_sa_add(extr->ips))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_getspi_parse: "
			    "failed to add the larval SA=%s with error=%d.\n",
			    sa_len ? sa : " (error)",
			    error);
		SENDERR(-error);
	}
	extr->ips = NULL;
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_getspi_parse: "
		    "successful for SA: %s\n",
		    sa_len ? sa : " (error)");
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_update_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct ipsec_sa* ipsq;
	char sa[SATOT_BUF];
	size_t sa_len;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	struct ipsec_sa *nat_t_ips_saved = NULL;
#endif
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_update_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(((struct sadb_sa*)extensions[SADB_EXT_SA])->sadb_sa_state != SADB_SASTATE_MATURE) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_update_parse: "
			    "error, sa_state=%d must be MATURE=%d\n",
			    ((struct sadb_sa*)extensions[SADB_EXT_SA])->sadb_sa_state,
			    SADB_SASTATE_MATURE);
		SENDERR(EINVAL);
	}

	if(extr == NULL || extr->ips == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_update_parse: "
			    "error, extr or extr->ips pointer NULL\n");
		SENDERR(EINVAL);
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

	spin_lock_bh(&tdb_lock);

	ipsq = ipsec_sa_getbyid(&(extr->ips->ips_said));
	if (ipsq == NULL) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_update_parse: "
			    "reserved ipsec_sa for SA: %s not found.  Call SADB_GETSPI first or call SADB_ADD instead.\n",
			    sa_len ? sa : " (error)");
		SENDERR(ENOENT);
	}

	if(inet_addr_type((unsigned long)extr->ips->ips_said.dst.u.v4.sin_addr.s_addr) == RTN_LOCAL) {
		extr->ips->ips_flags |= EMT_INBOUND;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_update_parse: "
		    "existing ipsec_sa found (this is good) for SA: %s, %s-bound, updating.\n",
		    sa_len ? sa : " (error)",
		    extr->ips->ips_flags & EMT_INBOUND ? "in" : "out");

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	if (extr->ips->ips_natt_sport || extr->ips->ips_natt_dport) {
		KLIPS_PRINT(debug_pfkey,
			"klips_debug:pfkey_update_parse: only updating NAT-T ports "
			"(%u:%u -> %u:%u)\n", 
			ipsq->ips_natt_sport, ipsq->ips_natt_dport,
			extr->ips->ips_natt_sport, extr->ips->ips_natt_dport);

		if (extr->ips->ips_natt_sport) {
			ipsq->ips_natt_sport = extr->ips->ips_natt_sport;
			if (ipsq->ips_addr_s->sa_family == AF_INET) {
				((struct sockaddr_in *)(ipsq->ips_addr_s))->sin_port = htons(extr->ips->ips_natt_sport);
			}
		}

		if (extr->ips->ips_natt_dport) {
			ipsq->ips_natt_dport = extr->ips->ips_natt_dport;
			if (ipsq->ips_addr_d->sa_family == AF_INET) {
				((struct sockaddr_in *)(ipsq->ips_addr_d))->sin_port = htons(extr->ips->ips_natt_dport);
			}
		}

		nat_t_ips_saved = extr->ips;
		extr->ips = ipsq;
	}
	else {
#endif
	
	/* XXX extr->ips->ips_rcvif = &(enc_softc[em->em_if].enc_if);*/
	extr->ips->ips_rcvif = NULL;
	if ((error = pfkey_ipsec_sa_init(extr->ips))) {
		ipsec_sa_put(ipsq);
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_update_parse: "
			    "not successful for SA: %s, deleting.\n",
			    sa_len ? sa : " (error)");
		SENDERR(-error);
	}

	extr->ips->ips_life.ipl_addtime.ipl_count = ipsq->ips_life.ipl_addtime.ipl_count;
	ipsec_sa_put(ipsq);
	if((error = ipsec_sa_delchain(ipsq))) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_update_parse: "
			    "error=%d, trouble deleting intermediate ipsec_sa for SA=%s.\n",
			    error,
			    sa_len ? sa : " (error)");
		SENDERR(-error);
	}
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	}
#endif

	spin_unlock_bh(&tdb_lock);
	
	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_UPDATE,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     /* The 3 lifetime extentions should only be sent if non-zero. */
	     && (extensions[SADB_EXT_LIFETIME_HARD]
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_HARD],
								 SADB_EXT_LIFETIME_HARD,
								 extr->ips->ips_life.ipl_allocations.ipl_hard,
								 extr->ips->ips_life.ipl_bytes.ipl_hard,
								 extr->ips->ips_life.ipl_addtime.ipl_hard,
								 extr->ips->ips_life.ipl_usetime.ipl_hard,
								 extr->ips->ips_life.ipl_packets.ipl_hard),
				    extensions_reply) : 1)
	     && (extensions[SADB_EXT_LIFETIME_SOFT]
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_SOFT],
								 SADB_EXT_LIFETIME_SOFT,
								 extr->ips->ips_life.ipl_allocations.ipl_count,
								 extr->ips->ips_life.ipl_bytes.ipl_count,
								 extr->ips->ips_life.ipl_addtime.ipl_count,
								 extr->ips->ips_life.ipl_usetime.ipl_count,
								 extr->ips->ips_life.ipl_packets.ipl_count),
				    extensions_reply) : 1)
	     && (extr->ips->ips_life.ipl_allocations.ipl_count
		 || extr->ips->ips_life.ipl_bytes.ipl_count
		 || extr->ips->ips_life.ipl_addtime.ipl_count
		 || extr->ips->ips_life.ipl_usetime.ipl_count
		 || extr->ips->ips_life.ipl_packets.ipl_count

		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_CURRENT],
								 SADB_EXT_LIFETIME_CURRENT,
								 extr->ips->ips_life.ipl_allocations.ipl_count,
								 extr->ips->ips_life.ipl_bytes.ipl_count,
								 extr->ips->ips_life.ipl_addtime.ipl_count,
								 extr->ips->ips_life.ipl_usetime.ipl_count,
								 extr->ips->ips_life.ipl_packets.ipl_count),
				    extensions_reply) : 1)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
							     SADB_EXT_ADDRESS_SRC,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_s),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
							     SADB_EXT_ADDRESS_DST,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_d),
				 extensions_reply)
	     && (extr->ips->ips_ident_s.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_SRC],
                                                              SADB_EXT_IDENTITY_SRC,
							      extr->ips->ips_ident_s.type,
							      extr->ips->ips_ident_s.id,
                                                              extr->ips->ips_ident_s.len,
							      extr->ips->ips_ident_s.data),
                                    extensions_reply) : 1)
	     && (extr->ips->ips_ident_d.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_DST],
                                                              SADB_EXT_IDENTITY_DST,
							      extr->ips->ips_ident_d.type,
							      extr->ips->ips_ident_d.id,
                                                              extr->ips->ips_ident_d.len,
							      extr->ips->ips_ident_d.data),
                                    extensions_reply) : 1)
#if 0
	     /* FIXME: This won't work yet because I have not finished
		it. */
	     && (extr->ips->ips_sens_
		 ? pfkey_safe_build(error = pfkey_sens_build(&extensions_reply[SADB_EXT_SENSITIVITY],
							     extr->ips->ips_sens_dpd,
							     extr->ips->ips_sens_sens_level,
							     extr->ips->ips_sens_sens_len,
							     extr->ips->ips_sens_sens_bitmap,
							     extr->ips->ips_sens_integ_level,
							     extr->ips->ips_sens_integ_len,
							     extr->ips->ips_sens_integ_bitmap),
				    extensions_reply) : 1)
#endif
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_update_parse: "
			    "failed to build the update reply message extensions\n");
		SENDERR(-error);
	}
		
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_update_parse: "
			    "failed to build the update reply message\n");
		SENDERR(-error);
	}
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_update_parse: "
				    "sending up update reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_update_parse: "
			    "sending up update reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	if (nat_t_ips_saved) {
		/**
		 * As we _really_ update existing SA, we keep tdbq and need to delete
		 * parsed ips (nat_t_ips_saved, was extr->ips).
		 *
		 * goto errlab with extr->ips = nat_t_ips_saved will free it.
		 */

		extr->ips = nat_t_ips_saved;

		error = 0;
		KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_update_parse (NAT-T ports): "
		    "successful for SA: %s\n",
		    sa_len ? sa : " (error)");

		goto errlab;
	}
#endif

	if((error = ipsec_sa_add(extr->ips))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_update_parse: "
			    "failed to update the mature SA=%s with error=%d.\n",
			    sa_len ? sa : " (error)",
			    error);
		SENDERR(-error);
	}
	extr->ips = NULL;
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_update_parse: "
		    "successful for SA: %s\n",
		    sa_len ? sa : " (error)");
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_add_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct ipsec_sa* ipsq;
	char sa[SATOT_BUF];
	size_t sa_len;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_add_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(((struct sadb_sa*)extensions[SADB_EXT_SA])->sadb_sa_state != SADB_SASTATE_MATURE) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_add_parse: "
			    "error, sa_state=%d must be MATURE=%d\n",
			    ((struct sadb_sa*)extensions[SADB_EXT_SA])->sadb_sa_state,
			    SADB_SASTATE_MATURE);
		SENDERR(EINVAL);
	}

	if(!extr || !extr->ips) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_add_parse: "
			    "extr or extr->ips pointer NULL\n");
		SENDERR(EINVAL);
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

	ipsq = ipsec_sa_getbyid(&(extr->ips->ips_said));
	if(ipsq != NULL) {
		ipsec_sa_put(ipsq);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_add_parse: "
			    "found an old ipsec_sa for SA%s, delete it first.\n",
			    sa_len ? sa : " (error)");
		SENDERR(EEXIST);
	}

	if(inet_addr_type((unsigned long)extr->ips->ips_said.dst.u.v4.sin_addr.s_addr) == RTN_LOCAL) {
		extr->ips->ips_flags |= EMT_INBOUND;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_add_parse: "
		    "existing ipsec_sa not found (this is good) for SA%s, %s-bound, allocating.\n",
		    sa_len ? sa : " (error)",
		    extr->ips->ips_flags & EMT_INBOUND ? "in" : "out");
	
	/* XXX extr->ips->ips_rcvif = &(enc_softc[em->em_if].enc_if);*/
	extr->ips->ips_rcvif = NULL;
	
	if ((error = pfkey_ipsec_sa_init(extr->ips))) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_add_parse: "
			    "not successful for SA: %s, deleting.\n",
			    sa_len ? sa : " (error)");
		SENDERR(-error);
	}

	extr->ips->ips_life.ipl_addtime.ipl_count = jiffies / HZ;
	if(!extr->ips->ips_life.ipl_allocations.ipl_count) {
		extr->ips->ips_life.ipl_allocations.ipl_count += 1;
	}

	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_ADD,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     /* The 3 lifetime extentions should only be sent if non-zero. */
	     && (extensions[SADB_EXT_LIFETIME_HARD]
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_HARD],
								 SADB_EXT_LIFETIME_HARD,
								 extr->ips->ips_life.ipl_allocations.ipl_hard,
								 extr->ips->ips_life.ipl_bytes.ipl_hard,
								 extr->ips->ips_life.ipl_addtime.ipl_hard,
								 extr->ips->ips_life.ipl_usetime.ipl_hard,
								 extr->ips->ips_life.ipl_packets.ipl_hard),
				    extensions_reply) : 1)
	     && (extensions[SADB_EXT_LIFETIME_SOFT]
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_SOFT],
								 SADB_EXT_LIFETIME_SOFT,
								 extr->ips->ips_life.ipl_allocations.ipl_soft,
								 extr->ips->ips_life.ipl_bytes.ipl_soft,
								 extr->ips->ips_life.ipl_addtime.ipl_soft,
								 extr->ips->ips_life.ipl_usetime.ipl_soft,
								 extr->ips->ips_life.ipl_packets.ipl_soft),
				    extensions_reply) : 1)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
							     SADB_EXT_ADDRESS_SRC,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_s),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
							     SADB_EXT_ADDRESS_DST,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_d),
				 extensions_reply)
            && (extr->ips->ips_ident_s.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_SRC],
                                                              SADB_EXT_IDENTITY_SRC,
							      extr->ips->ips_ident_s.type,
							      extr->ips->ips_ident_s.id,
                                                              extr->ips->ips_ident_s.len,
							      extr->ips->ips_ident_s.data),
                                    extensions_reply) : 1)
            && (extr->ips->ips_ident_d.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_DST],
                                                              SADB_EXT_IDENTITY_DST,
							      extr->ips->ips_ident_d.type,
							      extr->ips->ips_ident_d.id,
                                                              extr->ips->ips_ident_d.len,
							      extr->ips->ips_ident_d.data),
                                    extensions_reply) : 1)
#if 0
	     /* FIXME: This won't work yet because I have not finished
		it. */
	     && (extr->ips->ips_sens_
		 ? pfkey_safe_build(error = pfkey_sens_build(&extensions_reply[SADB_EXT_SENSITIVITY],
							     extr->ips->ips_sens_dpd,
							     extr->ips->ips_sens_sens_level,
							     extr->ips->ips_sens_sens_len,
							     extr->ips->ips_sens_sens_bitmap,
							     extr->ips->ips_sens_integ_level,
							     extr->ips->ips_sens_integ_len,
							     extr->ips->ips_sens_integ_bitmap),
				    extensions_reply) : 1)
#endif
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_add_parse: "
			    "failed to build the add reply message extensions\n");
		SENDERR(-error);
	}
		
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_add_parse: "
			    "failed to build the add reply message\n");
		SENDERR(-error);
	}
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_add_parse: "
				    "sending up add reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_add_parse: "
			    "sending up add reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}

	if((error = ipsec_sa_add(extr->ips))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_add_parse: "
			    "failed to add the mature SA=%s with error=%d.\n",
			    sa_len ? sa : " (error)",
			    error);
		SENDERR(-error);
	}
	extr->ips = NULL;
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_add_parse: "
		    "successful for SA: %s\n",
		    sa_len ? sa : " (error)");
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_delete_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	struct ipsec_sa *ipsp;
	char sa[SATOT_BUF];
	size_t sa_len;
	int error = 0;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_delete_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(!extr || !extr->ips) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_delete_parse: "
			    "extr or extr->ips pointer NULL, fatal\n");
		SENDERR(EINVAL);
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

	spin_lock_bh(&tdb_lock);

	ipsp = ipsec_sa_getbyid(&(extr->ips->ips_said));
	if (ipsp == NULL) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_delete_parse: "
			    "ipsec_sa not found for SA:%s, could not delete.\n",
			    sa_len ? sa : " (error)");
		SENDERR(ESRCH);
	}

	ipsec_sa_put(ipsp);
	if((error = ipsec_sa_delchain(ipsp))) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_delete_parse: "
			    "error=%d returned trying to delete ipsec_sa for SA:%s.\n",
			    error,
			    sa_len ? sa : " (error)");
		SENDERR(-error);
	}
	spin_unlock_bh(&tdb_lock);

	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_DELETE,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							0,
							0,
							0,
							0,
							0,
							extr->ips->ips_ref),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
							     SADB_EXT_ADDRESS_SRC,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_s),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
							     SADB_EXT_ADDRESS_DST,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_d),
				 extensions_reply)
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_delete_parse: "
			    "failed to build the delete reply message extensions\n");
		SENDERR(-error);
	}
	
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_delete_parse: "
			    "failed to build the delete reply message\n");
		SENDERR(-error);
	}
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_delete_parse: "
				    "sending up delete reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_delete_parse: "
			    "sending up delete reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_get_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct ipsec_sa *ipsp;
	char sa[SATOT_BUF];
	size_t sa_len;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_get_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(!extr || !extr->ips) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_get_parse: "
			    "extr or extr->ips pointer NULL, fatal\n");
		SENDERR(EINVAL);
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

	spin_lock_bh(&tdb_lock);

	ipsp = ipsec_sa_getbyid(&(extr->ips->ips_said));
	if (ipsp == NULL) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_get_parse: "
			    "ipsec_sa not found for SA=%s, could not get.\n",
			    sa_len ? sa : " (error)");
		SENDERR(ESRCH);
	}
	
	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_GET,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     /* The 3 lifetime extentions should only be sent if non-zero. */
	     && (ipsp->ips_life.ipl_allocations.ipl_count
		 || ipsp->ips_life.ipl_bytes.ipl_count
		 || ipsp->ips_life.ipl_addtime.ipl_count
		 || ipsp->ips_life.ipl_usetime.ipl_count
		 || ipsp->ips_life.ipl_packets.ipl_count
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_CURRENT],
								 SADB_EXT_LIFETIME_CURRENT,
								 ipsp->ips_life.ipl_allocations.ipl_count,
								 ipsp->ips_life.ipl_bytes.ipl_count,
								 ipsp->ips_life.ipl_addtime.ipl_count,
								 ipsp->ips_life.ipl_usetime.ipl_count,
								 ipsp->ips_life.ipl_packets.ipl_count),
				    extensions_reply) : 1)
	     && (ipsp->ips_life.ipl_allocations.ipl_hard
		 || ipsp->ips_life.ipl_bytes.ipl_hard
		 || ipsp->ips_life.ipl_addtime.ipl_hard
		 || ipsp->ips_life.ipl_usetime.ipl_hard
		 || ipsp->ips_life.ipl_packets.ipl_hard
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_HARD],
								 SADB_EXT_LIFETIME_HARD,
								 ipsp->ips_life.ipl_allocations.ipl_hard,
								 ipsp->ips_life.ipl_bytes.ipl_hard,
								 ipsp->ips_life.ipl_addtime.ipl_hard,
								 ipsp->ips_life.ipl_usetime.ipl_hard,
								 ipsp->ips_life.ipl_packets.ipl_hard),
				    extensions_reply) : 1)
	     && (ipsp->ips_life.ipl_allocations.ipl_soft
		 || ipsp->ips_life.ipl_bytes.ipl_soft
		 || ipsp->ips_life.ipl_addtime.ipl_soft
		 || ipsp->ips_life.ipl_usetime.ipl_soft
		 || ipsp->ips_life.ipl_packets.ipl_soft
		 ? pfkey_safe_build(error = pfkey_lifetime_build(&extensions_reply[SADB_EXT_LIFETIME_SOFT],
								 SADB_EXT_LIFETIME_SOFT,
								 ipsp->ips_life.ipl_allocations.ipl_soft,
								 ipsp->ips_life.ipl_bytes.ipl_soft,
								 ipsp->ips_life.ipl_addtime.ipl_soft,
								 ipsp->ips_life.ipl_usetime.ipl_soft,
								 ipsp->ips_life.ipl_packets.ipl_soft),
				    extensions_reply) : 1)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
							     SADB_EXT_ADDRESS_SRC,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_s),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
							     SADB_EXT_ADDRESS_DST,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_d),
				 extensions_reply)
	     && (extr->ips->ips_addr_p
		 ? pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_PROXY],
								SADB_EXT_ADDRESS_PROXY,
								0, /*extr->ips->ips_said.proto,*/
								0,
								extr->ips->ips_addr_p),
				    extensions_reply) : 1)
#if 0
	     /* FIXME: This won't work yet because the keys are not
		stored directly in the ipsec_sa.  They are stored as
		contexts. */
	     && (extr->ips->ips_key_a_size
		 ? pfkey_safe_build(error = pfkey_key_build(&extensions_reply[SADB_EXT_KEY_AUTH],
							    SADB_EXT_KEY_AUTH,
							    extr->ips->ips_key_a_size * 8,
							    extr->ips->ips_key_a),
				    extensions_reply) : 1)
	     /* FIXME: This won't work yet because the keys are not
		stored directly in the ipsec_sa.  They are stored as
		key schedules. */
	     && (extr->ips->ips_key_e_size
		 ? pfkey_safe_build(error = pfkey_key_build(&extensions_reply[SADB_EXT_KEY_ENCRYPT],
							    SADB_EXT_KEY_ENCRYPT,
							    extr->ips->ips_key_e_size * 8,
							    extr->ips->ips_key_e),
				    extensions_reply) : 1)
#endif
	     && (extr->ips->ips_ident_s.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_SRC],
                                                              SADB_EXT_IDENTITY_SRC,
							      extr->ips->ips_ident_s.type,
							      extr->ips->ips_ident_s.id,
                                                              extr->ips->ips_ident_s.len,
							      extr->ips->ips_ident_s.data),
                                    extensions_reply) : 1)
	     && (extr->ips->ips_ident_d.data
                 ? pfkey_safe_build(error = pfkey_ident_build(&extensions_reply[SADB_EXT_IDENTITY_DST],
                                                              SADB_EXT_IDENTITY_DST,
							      extr->ips->ips_ident_d.type,
							      extr->ips->ips_ident_d.id,
                                                              extr->ips->ips_ident_d.len,
							      extr->ips->ips_ident_d.data),
                                    extensions_reply) : 1)
#if 0
	     /* FIXME: This won't work yet because I have not finished
		it. */
	     && (extr->ips->ips_sens_
		 ? pfkey_safe_build(error = pfkey_sens_build(&extensions_reply[SADB_EXT_SENSITIVITY],
							     extr->ips->ips_sens_dpd,
							     extr->ips->ips_sens_sens_level,
							     extr->ips->ips_sens_sens_len,
							     extr->ips->ips_sens_sens_bitmap,
							     extr->ips->ips_sens_integ_level,
							     extr->ips->ips_sens_integ_len,
							     extr->ips->ips_sens_integ_bitmap),
				    extensions_reply) : 1)
#endif
		     )) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_get_parse: "
			    "failed to build the get reply message extensions\n");
		ipsec_sa_put(ipsp);
		spin_unlock_bh(&tdb_lock);
		SENDERR(-error);
	}
		
	ipsec_sa_put(ipsp);
	spin_unlock_bh(&tdb_lock);
	
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_get_parse: "
			    "failed to build the get reply message\n");
		SENDERR(-error);
	}
	
	if((error = pfkey_upmsg(sk->sk_socket, pfkey_reply))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_get_parse: "
			    "failed to send the get reply message\n");
		SENDERR(-error);
	}
	
	KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_get_parse: "
		    "succeeded in sending get reply message.\n");
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_acquire_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_acquire_parse: .\n");

	/* XXX I don't know if we want an upper bound, since userspace may
	   want to register itself for an satype > SADB_SATYPE_MAX. */
	if((satype == 0) || (satype > SADB_SATYPE_MAX)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_acquire_parse: "
			    "SATYPE=%d invalid.\n",
			    satype);
		SENDERR(EINVAL);
	}

	if(!(pfkey_registered_sockets[satype])) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_acquire_parse: "
			    "no sockets registered for SAtype=%d(%s).\n",
			    satype,
			    satype2name(satype));
		SENDERR(EPROTONOSUPPORT);
	}

	for(pfkey_socketsp = pfkey_registered_sockets[satype];
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp,
					((struct sadb_msg*)extensions[SADB_EXT_RESERVED])))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_acquire_parse: "
				    "sending up acquire reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_acquire_parse: "
			    "sending up acquire reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_register_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_register_parse: .\n");

	/* XXX I don't know if we want an upper bound, since userspace may
	   want to register itself for an satype > SADB_SATYPE_MAX. */
	if((satype == 0) || (satype > SADB_SATYPE_MAX)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_register_parse: "
			    "SATYPE=%d invalid.\n",
			    satype);
		SENDERR(EINVAL);
	}

	if(!pfkey_list_insert_socket(sk->sk_socket,
				 &(pfkey_registered_sockets[satype]))) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_register_parse: "
			    "SATYPE=%02d(%s) successfully registered by KMd (pid=%d).\n",
			    satype,
			    satype2name(satype),
			    key_pid(sk));
	};
	
	/* send up register msg with supported SATYPE algos */

	error=pfkey_register_reply(satype, (struct sadb_msg*)extensions[SADB_EXT_RESERVED]);
 errlab:
	return error;
}

int
pfkey_register_reply(int satype, struct sadb_msg *sadb_msg)
{
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	struct supported_list *pfkey_supported_listp;
	unsigned int alg_num_a = 0, alg_num_e = 0;
	struct sadb_alg *alg_a = NULL, *alg_e = NULL, *alg_ap = NULL, *alg_ep = NULL;
	int error = 0;

	pfkey_extensions_init(extensions_reply);

	if((satype == 0) || (satype > SADB_SATYPE_MAX)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
			    "SAtype=%d unspecified or unknown.\n",
			    satype);
		SENDERR(EINVAL);
	}
	if(!(pfkey_registered_sockets[satype])) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
			    "no sockets registered for SAtype=%d(%s).\n",
			    satype,
			    satype2name(satype));
		SENDERR(EPROTONOSUPPORT);
	}
	/* send up register msg with supported SATYPE algos */
	pfkey_supported_listp = pfkey_supported_list[satype];
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_register_reply: "
		    "pfkey_supported_list[%d]=0p%p\n",
		    satype,
		    pfkey_supported_list[satype]);
	while(pfkey_supported_listp) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_register_reply: "
			    "checking supported=0p%p\n",
			    pfkey_supported_listp);
		if(pfkey_supported_listp->supportedp->ias_exttype == SADB_EXT_SUPPORTED_AUTH) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_register_reply: "
				    "adding auth alg.\n");
			alg_num_a++;
		}
		if(pfkey_supported_listp->supportedp->ias_exttype == SADB_EXT_SUPPORTED_ENCRYPT) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_register_reply: "
				    "adding encrypt alg.\n");
			alg_num_e++;
		}
		pfkey_supported_listp = pfkey_supported_listp->next;
	}
	
	if(alg_num_a) {
		KLIPS_PRINT(debug_pfkey,
		            "klips_debug:pfkey_register_reply: "
		            "allocating %lu bytes for auth algs.\n",
		            (unsigned long) (alg_num_a * sizeof(struct sadb_alg)));
		if((alg_a = kmalloc(alg_num_a * sizeof(struct sadb_alg), GFP_ATOMIC) ) == NULL) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_register_reply: "
				    "auth alg memory allocation error\n");
			SENDERR(ENOMEM);
		}
		alg_ap = alg_a;
	}
	
	if(alg_num_e) {
		KLIPS_PRINT(debug_pfkey,
		            "klips_debug:pfkey_register_reply: "
		            "allocating %lu bytes for enc algs.\n",
		            (unsigned long) (alg_num_e * sizeof(struct sadb_alg)));
		if((alg_e = kmalloc(alg_num_e * sizeof(struct sadb_alg), GFP_ATOMIC) ) == NULL) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_register_reply: "
				    "enc alg memory allocation error\n");
			SENDERR(ENOMEM);
		}
		alg_ep = alg_e;
	}
	
	pfkey_supported_listp = pfkey_supported_list[satype];
	while(pfkey_supported_listp) {
		if(alg_num_a) {
			if(pfkey_supported_listp->supportedp->ias_exttype == SADB_EXT_SUPPORTED_AUTH) {
				alg_ap->sadb_alg_id = pfkey_supported_listp->supportedp->ias_id;
				alg_ap->sadb_alg_ivlen = pfkey_supported_listp->supportedp->ias_ivlen;
				alg_ap->sadb_alg_minbits = pfkey_supported_listp->supportedp->ias_keyminbits;
				alg_ap->sadb_alg_maxbits = pfkey_supported_listp->supportedp->ias_keymaxbits;
				alg_ap->sadb_alg_reserved = 0;
				KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
					    "klips_debug:pfkey_register_reply: "
					    "adding auth=0p%p\n",
					    alg_ap);
				alg_ap++;
			}
		}
		if(alg_num_e) {
			if(pfkey_supported_listp->supportedp->ias_exttype == SADB_EXT_SUPPORTED_ENCRYPT) {
				alg_ep->sadb_alg_id = pfkey_supported_listp->supportedp->ias_id;
				alg_ep->sadb_alg_ivlen = pfkey_supported_listp->supportedp->ias_ivlen;
				alg_ep->sadb_alg_minbits = pfkey_supported_listp->supportedp->ias_keyminbits;
				alg_ep->sadb_alg_maxbits = pfkey_supported_listp->supportedp->ias_keymaxbits;
				alg_ep->sadb_alg_reserved = 0;
				KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
					    "klips_debug:pfkey_register_reply: "
					    "adding encrypt=0p%p\n",
					    alg_ep);
				alg_ep++;
			}
		}
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_register_reply: "
			    "found satype=%d(%s) exttype=%d id=%d ivlen=%d minbits=%d maxbits=%d.\n",
			    satype,
			    satype2name(satype),
			    pfkey_supported_listp->supportedp->ias_exttype,
			    pfkey_supported_listp->supportedp->ias_id,
			    pfkey_supported_listp->supportedp->ias_ivlen,
			    pfkey_supported_listp->supportedp->ias_keyminbits,
			    pfkey_supported_listp->supportedp->ias_keymaxbits);
		pfkey_supported_listp = pfkey_supported_listp->next;
	}
	
	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_REGISTER,
							  satype,
							  0,
							  sadb_msg? sadb_msg->sadb_msg_seq : ++pfkey_msg_seq,
							  sadb_msg? sadb_msg->sadb_msg_pid: current->pid),
			      extensions_reply) &&
	     (alg_num_a ? pfkey_safe_build(error = pfkey_supported_build(&extensions_reply[SADB_EXT_SUPPORTED_AUTH],
									SADB_EXT_SUPPORTED_AUTH,
									alg_num_a,
									alg_a),
					  extensions_reply) : 1) &&
	     (alg_num_e ? pfkey_safe_build(error = pfkey_supported_build(&extensions_reply[SADB_EXT_SUPPORTED_ENCRYPT],
									SADB_EXT_SUPPORTED_ENCRYPT,
									alg_num_e,
									alg_e),
					  extensions_reply) : 1))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
			    "failed to build the register message extensions_reply\n");
		SENDERR(-error);
	}
	
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
			    "failed to build the register message\n");
		SENDERR(-error);
	}
	/* this should go to all registered sockets for that satype only */
	for(pfkey_socketsp = pfkey_registered_sockets[satype];
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
				    "sending up acquire message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_register_reply: "
			    "sending up register message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	if(alg_a) {
		kfree(alg_a);
	}
	if(alg_e) {
		kfree(alg_e);
	}

	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_expire_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct socket_list *pfkey_socketsp;
#ifdef CONFIG_KLIPS_DEBUG
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
#endif /* CONFIG_KLIPS_DEBUG */

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_expire_parse: .\n");

	if(pfkey_open_sockets) {
		for(pfkey_socketsp = pfkey_open_sockets;
		    pfkey_socketsp;
		    pfkey_socketsp = pfkey_socketsp->next) {
			if((error = pfkey_upmsg(pfkey_socketsp->socketp,
						((struct sadb_msg*)extensions[SADB_EXT_RESERVED])))) {
				KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire_parse: "
					    "sending up expire reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
					    satype,
					    satype2name(satype),
					    pfkey_socketsp->socketp,
					    error);
				SENDERR(-error);
			}
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire_parse: "
				    "sending up expire reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp);
		}
	}

 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_flush_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
	uint8_t proto = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_flush_parse: "
		    "flushing type %d SAs\n",
		    satype);

	if(satype && !(proto = satype2proto(satype))) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_flush_parse: "
			    "satype %d lookup failed.\n", 
			    ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype);
		SENDERR(EINVAL);
	}

	if ((error = ipsec_sadb_cleanup(proto))) {
		SENDERR(-error);
	}

	if(pfkey_open_sockets) {
		for(pfkey_socketsp = pfkey_open_sockets;
		    pfkey_socketsp;
		    pfkey_socketsp = pfkey_socketsp->next) {
			if((error = pfkey_upmsg(pfkey_socketsp->socketp,
						((struct sadb_msg*)extensions[SADB_EXT_RESERVED])))) {
				KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_flush_parse: "
					    "sending up flush reply message for satype=%d(%s) (proto=%d) to socket=0p%p failed with error=%d.\n",
					    satype,
					    satype2name(satype),
					    proto,
					    pfkey_socketsp->socketp,
					    error);
				SENDERR(-error);
			}
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_flush_parse: "
				    "sending up flush reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp);
		}
	}

 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_dump_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_dump_parse: .\n");

	SENDERR(ENOSYS);
 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_promisc_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_promisc_parse: .\n");

	SENDERR(ENOSYS);
 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_pchange_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_pchange_parse: .\n");

	SENDERR(ENOSYS);
 errlab:
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_grpsa_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	struct ipsec_sa *ips1p, *ips2p, *ipsp;
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
	char sa1[SATOT_BUF], sa2[SATOT_BUF];
	size_t sa_len1, sa_len2 = 0;
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_grpsa_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	if(extr == NULL || extr->ips == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_grpsa_parse: "
			    "extr or extr->ips is NULL, fatal.\n");
		SENDERR(EINVAL);
	}

	sa_len1 = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa1, sizeof(sa1));
	if(extr->ips2 != NULL) {
		sa_len2 = KLIPS_SATOT(debug_pfkey, &extr->ips2->ips_said, 0, sa2, sizeof(sa2));
	}

	spin_lock_bh(&tdb_lock);

	ips1p = ipsec_sa_getbyid(&(extr->ips->ips_said));
	if(ips1p == NULL) {
		spin_unlock_bh(&tdb_lock);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_grpsa_parse: "
			    "reserved ipsec_sa for SA1: %s not found.  Call SADB_ADD/UPDATE first.\n",
			    sa_len1 ? sa1 : " (error)");
		SENDERR(ENOENT);
	}
	if(extr->ips2) { /* GRPSA */
		ips2p = ipsec_sa_getbyid(&(extr->ips2->ips_said));
		if(ips2p == NULL) {
			ipsec_sa_put(ips1p);
			spin_unlock_bh(&tdb_lock);
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_grpsa_parse: "
				    "reserved ipsec_sa for SA2: %s not found.  Call SADB_ADD/UPDATE first.\n",
				    sa_len2 ? sa2 : " (error)");
			SENDERR(ENOENT);
		}

		/* Is either one already linked? */
		if(ips1p->ips_onext) {
			ipsec_sa_put(ips1p);
			ipsec_sa_put(ips2p);
			spin_unlock_bh(&tdb_lock);
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_grpsa_parse: "
				    "ipsec_sa for SA: %s is already linked.\n",
				    sa_len1 ? sa1 : " (error)");
			SENDERR(EEXIST);
		}
		if(ips2p->ips_inext) {
			ipsec_sa_put(ips1p);
			ipsec_sa_put(ips2p);
			spin_unlock_bh(&tdb_lock);
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_grpsa_parse: "
				    "ipsec_sa for SA: %s is already linked.\n",
				    sa_len2 ? sa2 : " (error)");
			SENDERR(EEXIST);
		}
		
		/* Is extr->ips already linked to extr->ips2? */
		ipsp = ips2p;
		while(ipsp) {
			if(ipsp == ips1p) {
				ipsec_sa_put(ips1p);
				ipsec_sa_put(ips2p);
				spin_unlock_bh(&tdb_lock);
				KLIPS_PRINT(debug_pfkey,
					    "klips_debug:pfkey_x_grpsa_parse: "
					    "ipsec_sa for SA: %s is already linked to %s.\n",
					    sa_len1 ? sa1 : " (error)",
					    sa_len2 ? sa2 : " (error)");
				SENDERR(EEXIST);
			}
			ipsp = ipsp->ips_onext;
		}
		
		/* link 'em */
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_grpsa_parse: "
			    "linking ipsec_sa SA: %s with %s.\n",
			    sa_len1 ? sa1 : " (error)",
			    sa_len2 ? sa2 : " (error)");
		ips1p->ips_onext = ips2p;
		ips2p->ips_inext = ips1p;
	} else { /* UNGRPSA */
		ipsec_sa_put(ips1p);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_grpsa_parse: "
			    "unlinking ipsec_sa SA: %s.\n",
			    sa_len1 ? sa1 : " (error)");
		while(ips1p->ips_onext) {
			ips1p = ips1p->ips_onext;
		}
		while(ips1p->ips_inext) {
			ipsp = ips1p;
			ips1p = ips1p->ips_inext;
			ipsec_sa_put(ips1p);
			ipsp->ips_inext = NULL;
			ipsec_sa_put(ipsp);
			ips1p->ips_onext = NULL;
		}
	}

	spin_unlock_bh(&tdb_lock);

	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_X_GRPSA,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
							     SADB_EXT_ADDRESS_DST,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     extr->ips->ips_addr_d),
				 extensions_reply)
	     && (extr->ips2
		 ? (pfkey_safe_build(error = pfkey_x_satype_build(&extensions_reply[SADB_X_EXT_SATYPE2],
								  ((struct sadb_x_satype*)extensions[SADB_X_EXT_SATYPE2])->sadb_x_satype_satype
								  /* proto2satype(extr->ips2->ips_said.proto) */),
								  extensions_reply)
				     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_X_EXT_SA2],
										SADB_X_EXT_SA2,
										extr->ips2->ips_said.spi,
										extr->ips2->ips_replaywin,
										extr->ips2->ips_state,
										extr->ips2->ips_authalg,
										extr->ips2->ips_encalg,
										extr->ips2->ips_flags,
										extr->ips2->ips_ref),
							 extensions_reply)
				     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_DST2],
										     SADB_X_EXT_ADDRESS_DST2,
										     0, /*extr->ips->ips_said.proto,*/
										     0,
										     extr->ips2->ips_addr_d),
							 extensions_reply) ) : 1 )
		     )) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_grpsa_parse: "
			    "failed to build the x_grpsa reply message extensions\n");
		SENDERR(-error);
	}
	   
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_grpsa_parse: "
			    "failed to build the x_grpsa reply message\n");
		SENDERR(-error);
	}
	
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_grpsa_parse: "
				    "sending up x_grpsa reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_grpsa_parse: "
			    "sending up x_grpsa reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
	KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_grpsa_parse: "
		    "succeeded in sending x_grpsa reply message.\n");
	
 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_addflow_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
#ifdef CONFIG_KLIPS_DEBUG
	char buf1[64], buf2[64];
#endif /* CONFIG_KLIPS_DEBUG */
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
	ip_address srcflow, dstflow, srcmask, dstmask;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_addflow_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	memset((caddr_t)&srcflow, 0, sizeof(srcflow));
	memset((caddr_t)&dstflow, 0, sizeof(dstflow));
	memset((caddr_t)&srcmask, 0, sizeof(srcmask));
	memset((caddr_t)&dstmask, 0, sizeof(dstmask));

	if(!extr || !(extr->ips) || !(extr->eroute)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_addflow_parse: "
			    "missing extr, ipsec_sa or eroute data.\n");
		SENDERR(EINVAL);
	}

	srcflow.u.v4.sin_family = AF_INET;
	dstflow.u.v4.sin_family = AF_INET;
	srcmask.u.v4.sin_family = AF_INET;
	dstmask.u.v4.sin_family = AF_INET;
	srcflow.u.v4.sin_addr = extr->eroute->er_eaddr.sen_ip_src;
	dstflow.u.v4.sin_addr = extr->eroute->er_eaddr.sen_ip_dst;
	srcmask.u.v4.sin_addr = extr->eroute->er_emask.sen_ip_src;
	dstmask.u.v4.sin_addr = extr->eroute->er_emask.sen_ip_dst;

#ifdef CONFIG_KLIPS_DEBUG
	if (debug_pfkey) {
		subnettoa(extr->eroute->er_eaddr.sen_ip_src,
			  extr->eroute->er_emask.sen_ip_src, 0, buf1, sizeof(buf1));
		subnettoa(extr->eroute->er_eaddr.sen_ip_dst,
			  extr->eroute->er_emask.sen_ip_dst, 0, buf2, sizeof(buf2));
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_addflow_parse: "
			    "calling breakeroute and/or makeroute for %s->%s\n",
			    buf1, buf2);
	}
#endif /* CONFIG_KLIPS_DEBUG */

	if(extr->ips->ips_flags & SADB_X_SAFLAGS_INFLOW) {
		struct ipsec_sa *ipsp, *ipsq;
		char sa[SATOT_BUF];
		size_t sa_len;

		ipsq = ipsec_sa_getbyid(&(extr->ips->ips_said));
		if(ipsq == NULL) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "ipsec_sa not found, cannot set incoming policy.\n");
			SENDERR(ENOENT);
		}

		ipsp = ipsq;
		while(ipsp && ipsp->ips_said.proto != IPPROTO_IPIP) {
			ipsp = ipsp->ips_inext;
		}

		if(ipsp == NULL) {
			ipsec_sa_put(ipsq);
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "SA chain does not have an IPIP SA, cannot set incoming policy.\n");
			SENDERR(ENOENT);
		}
		
		sa_len = KLIPS_SATOT(debug_pfkey, &extr->ips->ips_said, 0, sa, sizeof(sa));

		ipsp->ips_flags |= SADB_X_SAFLAGS_INFLOW;
		ipsp->ips_flow_s = srcflow;
		ipsp->ips_flow_d = dstflow;
		ipsp->ips_mask_s = srcmask;
		ipsp->ips_mask_d = dstmask;

		ipsec_sa_put(ipsq);

		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_addflow_parse: "
			    "inbound eroute, setting incoming policy information in IPIP ipsec_sa for SA: %s.\n",
			    sa_len ? sa : " (error)");
	} else {
		struct sk_buff *first = NULL, *last = NULL;

		if(extr->ips->ips_flags & SADB_X_SAFLAGS_REPLACEFLOW) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "REPLACEFLOW flag set, calling breakeroute.\n");
			if ((error = ipsec_breakroute(&(extr->eroute->er_eaddr),
						      &(extr->eroute->er_emask),
						      &first, &last))) {
				KLIPS_PRINT(debug_pfkey,
					    "klips_debug:pfkey_x_addflow_parse: "
					    "breakeroute returned %d.  first=0p%p, last=0p%p\n",
					    error,
					    first,
					    last);
				if(first != NULL) {
					ipsec_kfree_skb(first);
				}
				if(last != NULL) {
					ipsec_kfree_skb(last);
				}
				SENDERR(-error);
			}
		}
		
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_addflow_parse: "
			    "calling makeroute.\n");
		
		if ((error = ipsec_makeroute(&(extr->eroute->er_eaddr),
					     &(extr->eroute->er_emask),
					     extr->ips->ips_said,
					     ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid,
					     NULL,
					     &(extr->ips->ips_ident_s),
					     &(extr->ips->ips_ident_d)))) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "makeroute returned %d.\n", error);
			SENDERR(-error);
		}
		if(first != NULL) {
			KLIPS_PRINT(debug_eroute,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "first=0p%p HOLD packet re-injected.\n",
				    first);
			DEV_QUEUE_XMIT(first, first->dev, SOPRI_NORMAL);
		}
		if(last != NULL) {
			KLIPS_PRINT(debug_eroute,
				    "klips_debug:pfkey_x_addflow_parse: "
				    "last=0p%p HOLD packet re-injected.\n",
				    last);
			DEV_QUEUE_XMIT(last, last->dev, SOPRI_NORMAL);
		}
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_addflow_parse: "
		    "makeroute call successful.\n");

	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_X_ADDFLOW,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     && (extensions[SADB_EXT_ADDRESS_SRC]
		 ? pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_SRC],
								SADB_EXT_ADDRESS_SRC,
								0, /*extr->ips->ips_said.proto,*/
								0,
								extr->ips->ips_addr_s),
				    extensions_reply) : 1)
	     && (extensions[SADB_EXT_ADDRESS_DST]
		 ? pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_EXT_ADDRESS_DST],
								SADB_EXT_ADDRESS_DST,
								0, /*extr->ips->ips_said.proto,*/
								0,
								extr->ips->ips_addr_d),
				    extensions_reply) : 1)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_SRC_FLOW],
							     SADB_X_EXT_ADDRESS_SRC_FLOW,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&srcflow),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_DST_FLOW],
							     SADB_X_EXT_ADDRESS_DST_FLOW,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&dstflow),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_SRC_MASK],
							     SADB_X_EXT_ADDRESS_SRC_MASK,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&srcmask),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_DST_MASK],
							     SADB_X_EXT_ADDRESS_DST_MASK,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&dstmask),
				 extensions_reply)
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_addflow_parse: "
			    "failed to build the x_addflow reply message extensions\n");
		SENDERR(-error);
	}
		
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_addflow_parse: "
			    "failed to build the x_addflow reply message\n");
		SENDERR(-error);
	}
	
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_addflow_parse: "
				    "sending up x_addflow reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_addflow_parse: "
			    "sending up x_addflow reply message for satype=%d(%s) (proto=%d) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    extr->ips->ips_said.proto,
			    pfkey_socketsp->socketp);
	}
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_addflow_parse: "
		    "extr->ips cleaned up and freed.\n");

 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_delflow_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;
#ifdef CONFIG_KLIPS_DEBUG
	char buf1[64], buf2[64];
#endif /* CONFIG_KLIPS_DEBUG */
	struct sadb_ext *extensions_reply[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_reply = NULL;
	struct socket_list *pfkey_socketsp;
	uint8_t satype = ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_satype;
	ip_address srcflow, dstflow, srcmask, dstmask;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_delflow_parse: .\n");

	pfkey_extensions_init(extensions_reply);

	memset((caddr_t)&srcflow, 0, sizeof(srcflow));
	memset((caddr_t)&dstflow, 0, sizeof(dstflow));
	memset((caddr_t)&srcmask, 0, sizeof(srcmask));
	memset((caddr_t)&dstmask, 0, sizeof(dstmask));

	if(!extr || !(extr->ips)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_delflow_parse: "
			    "extr, or extr->ips is NULL, fatal\n");
		SENDERR(EINVAL);
	}

	if(extr->ips->ips_flags & SADB_X_SAFLAGS_CLEARFLOW) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_x_delflow_parse: "
			    "CLEARFLOW flag set, calling cleareroutes.\n");
		if ((error = ipsec_cleareroutes()))
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_delflow_parse: "
				    "cleareroutes returned %d.\n", error);
			SENDERR(-error);
	} else {
		struct sk_buff *first = NULL, *last = NULL;

		if(!(extr->eroute)) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_delflow_parse: "
				    "extr->eroute is NULL, fatal.\n");
			SENDERR(EINVAL);
		}
		
		srcflow.u.v4.sin_family = AF_INET;
		dstflow.u.v4.sin_family = AF_INET;
		srcmask.u.v4.sin_family = AF_INET;
		dstmask.u.v4.sin_family = AF_INET;
		srcflow.u.v4.sin_addr = extr->eroute->er_eaddr.sen_ip_src;
		dstflow.u.v4.sin_addr = extr->eroute->er_eaddr.sen_ip_dst;
		srcmask.u.v4.sin_addr = extr->eroute->er_emask.sen_ip_src;
		dstmask.u.v4.sin_addr = extr->eroute->er_emask.sen_ip_dst;

#ifdef CONFIG_KLIPS_DEBUG
		if (debug_pfkey) {
			subnettoa(extr->eroute->er_eaddr.sen_ip_src,
				  extr->eroute->er_emask.sen_ip_src, 0, buf1, sizeof(buf1));
			subnettoa(extr->eroute->er_eaddr.sen_ip_dst,
				  extr->eroute->er_emask.sen_ip_dst, 0, buf2, sizeof(buf2));
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_delflow_parse: "
				    "calling breakeroute for %s->%s\n",
				    buf1, buf2);
		}
#endif /* CONFIG_KLIPS_DEBUG */
		error = ipsec_breakroute(&(extr->eroute->er_eaddr),
					     &(extr->eroute->er_emask),
					     &first, &last);
		if(error) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_x_delflow_parse: "
				    "breakeroute returned %d.  first=0p%p, last=0p%p\n",
				    error,
				    first,
				    last);
		}
		if(first != NULL) {
			ipsec_kfree_skb(first);
		}
		if(last != NULL) {
			ipsec_kfree_skb(last);
		}
		if(error) {
			SENDERR(-error);
		}
	}
	
	if(!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions_reply[0],
							  SADB_X_DELFLOW,
							  satype,
							  0,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_seq,
							  ((struct sadb_msg*)extensions[SADB_EXT_RESERVED])->sadb_msg_pid),
			      extensions_reply)
	     && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions_reply[SADB_EXT_SA],
							SADB_EXT_SA,
							extr->ips->ips_said.spi,
							extr->ips->ips_replaywin,
							extr->ips->ips_state,
							extr->ips->ips_authalg,
							extr->ips->ips_encalg,
							extr->ips->ips_flags,
							extr->ips->ips_ref),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_SRC_FLOW],
							     SADB_X_EXT_ADDRESS_SRC_FLOW,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&srcflow),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_DST_FLOW],
							     SADB_X_EXT_ADDRESS_DST_FLOW,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&dstflow),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_SRC_MASK],
							     SADB_X_EXT_ADDRESS_SRC_MASK,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&srcmask),
				 extensions_reply)
	     && pfkey_safe_build(error = pfkey_address_build(&extensions_reply[SADB_X_EXT_ADDRESS_DST_MASK],
							     SADB_X_EXT_ADDRESS_DST_MASK,
							     0, /*extr->ips->ips_said.proto,*/
							     0,
							     (struct sockaddr*)&dstmask),
				 extensions_reply)
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_delflow_parse: "
			    "failed to build the x_delflow reply message extensions\n");
		SENDERR(-error);
	}
		
	if((error = pfkey_msg_build(&pfkey_reply, extensions_reply, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_delflow_parse: "
			    "failed to build the x_delflow reply message\n");
		SENDERR(-error);
	}
	
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_delflow_parse: "
				    "sending up x_delflow reply message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_x_delflow_parse: "
			    "sending up x_delflow reply message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_delflow_parse: "
		    "extr->ips cleaned up and freed.\n");

 errlab:
	if (pfkey_reply) {
		pfkey_msg_free(&pfkey_reply);
	}
	pfkey_extensions_free(extensions_reply);
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_msg_debug_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_x_msg_debug_parse: .\n");

/* errlab:*/
	return error;
}

/* pfkey_expire expects the ipsec_sa table to be locked before being called. */
int
pfkey_expire(struct ipsec_sa *ipsp, int hard)
{
	struct sadb_ext *extensions[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_msg = NULL;
	struct socket_list *pfkey_socketsp;
	int error = 0;
	uint8_t satype;

	pfkey_extensions_init(extensions);

	if(!(satype = proto2satype(ipsp->ips_said.proto))) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_expire: "
			    "satype lookup for protocol %d lookup failed.\n", 
			    ipsp->ips_said.proto);
		SENDERR(EINVAL);
	}
	
	if(!pfkey_open_sockets) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire: "
			    "no sockets listening.\n");
		SENDERR(EPROTONOSUPPORT);
	}

	if (!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions[0],
							   SADB_EXPIRE,
							   satype,
							   0,
							   ++pfkey_msg_seq,
							   0),
			       extensions)
	      && pfkey_safe_build(error = pfkey_sa_ref_build(&extensions[SADB_EXT_SA],
							 SADB_EXT_SA,
							 ipsp->ips_said.spi,
							 ipsp->ips_replaywin,
							 ipsp->ips_state,
							 ipsp->ips_authalg,
							 ipsp->ips_encalg,
							 ipsp->ips_flags,
							 ipsp->ips_ref),
				  extensions)
	      && pfkey_safe_build(error = pfkey_lifetime_build(&extensions[SADB_EXT_LIFETIME_CURRENT],
							       SADB_EXT_LIFETIME_CURRENT,
							       ipsp->ips_life.ipl_allocations.ipl_count,
							       ipsp->ips_life.ipl_bytes.ipl_count,
							       ipsp->ips_life.ipl_addtime.ipl_count,
							       ipsp->ips_life.ipl_usetime.ipl_count,
							       ipsp->ips_life.ipl_packets.ipl_count),
				  extensions)
	      && (hard ? 
		  pfkey_safe_build(error = pfkey_lifetime_build(&extensions[SADB_EXT_LIFETIME_HARD],
								SADB_EXT_LIFETIME_HARD,
								ipsp->ips_life.ipl_allocations.ipl_hard,
								ipsp->ips_life.ipl_bytes.ipl_hard,
								ipsp->ips_life.ipl_addtime.ipl_hard,
								ipsp->ips_life.ipl_usetime.ipl_hard,
								ipsp->ips_life.ipl_packets.ipl_hard),
				   extensions)
		  : pfkey_safe_build(error = pfkey_lifetime_build(&extensions[SADB_EXT_LIFETIME_SOFT],
								  SADB_EXT_LIFETIME_SOFT,
								  ipsp->ips_life.ipl_allocations.ipl_soft,
								  ipsp->ips_life.ipl_bytes.ipl_soft,
								  ipsp->ips_life.ipl_addtime.ipl_soft,
								  ipsp->ips_life.ipl_usetime.ipl_soft,
								  ipsp->ips_life.ipl_packets.ipl_soft),
				     extensions))
	      && pfkey_safe_build(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_SRC],
							      SADB_EXT_ADDRESS_SRC,
							      0, /* ipsp->ips_said.proto, */
							      0,
							      ipsp->ips_addr_s),
				  extensions)
	      && pfkey_safe_build(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_DST],
							      SADB_EXT_ADDRESS_DST,
							      0, /* ipsp->ips_said.proto, */
							      0,
							      ipsp->ips_addr_d),
				  extensions))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire: "
			    "failed to build the expire message extensions\n");
		spin_unlock(&tdb_lock);
		goto errlab;
	}
	
	if ((error = pfkey_msg_build(&pfkey_msg, extensions, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire: "
			    "failed to build the expire message\n");
		SENDERR(-error);
	}
	
	for(pfkey_socketsp = pfkey_open_sockets;
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_msg))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire: "
				    "sending up expire message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_expire: "
			    "sending up expire message for satype=%d(%s) (proto=%d) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    ipsp->ips_said.proto,
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	if (pfkey_msg) {
		pfkey_msg_free(&pfkey_msg);
	}
	pfkey_extensions_free(extensions);
	return error;
}

int
pfkey_acquire(struct ipsec_sa *ipsp)
{
	struct sadb_ext *extensions[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_msg = NULL;
	struct socket_list *pfkey_socketsp;
	int error = 0;
	struct sadb_comb comb[] = {
		/* auth; encrypt; flags; */
		/* auth_minbits; auth_maxbits; encrypt_minbits; encrypt_maxbits; */
		/* reserved; soft_allocations; hard_allocations; soft_bytes; hard_bytes; */
		/* soft_addtime; hard_addtime; soft_usetime; hard_usetime; */
		/* soft_packets; hard_packets; */
		{ SADB_AALG_MD5HMAC,  SADB_EALG_3DESCBC, SADB_SAFLAGS_PFS,
		  128, 128, 168, 168,
		  0, 0, 0, 0, 0,
		  57600, 86400, 57600, 86400,
		  0, 0 },
		{ SADB_AALG_SHA1HMAC, SADB_EALG_3DESCBC, SADB_SAFLAGS_PFS,
		  160, 160, 168, 168,
		  0, 0, 0, 0, 0,
		  57600, 86400, 57600, 86400,
		  0, 0 }
	};
       
	/* XXX This should not be hard-coded.  It should be taken from the spdb */
	uint8_t satype = SADB_SATYPE_ESP;

	pfkey_extensions_init(extensions);

	if((satype == 0) || (satype > SADB_SATYPE_MAX)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_acquire: "
			    "SAtype=%d unspecified or unknown.\n",
			    satype);
		SENDERR(EINVAL);
	}

	if(!(pfkey_registered_sockets[satype])) {
		KLIPS_PRINT(1|debug_pfkey, "klips_debug:pfkey_acquire: "
			    "no sockets registered for SAtype=%d(%s).\n",
			    satype,
			    satype2name(satype));
		SENDERR(EPROTONOSUPPORT);
	}
	
	if (!(pfkey_safe_build(error = pfkey_msg_hdr_build(&extensions[0],
							  SADB_ACQUIRE,
							  satype,
							  0,
							  ++pfkey_msg_seq,
							  0),
			      extensions)
	      && pfkey_safe_build(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_SRC],
							      SADB_EXT_ADDRESS_SRC,
							      ipsp->ips_transport_protocol,
							      0,
							      ipsp->ips_addr_s),
				  extensions)
	      && pfkey_safe_build(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_DST],
							      SADB_EXT_ADDRESS_DST,
							      ipsp->ips_transport_protocol,
							      0,
							      ipsp->ips_addr_d),
				  extensions)
#if 0
	      && (ipsp->ips_addr_p
		  ? pfkey_safe_build(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_PROXY],
								 SADB_EXT_ADDRESS_PROXY,
								 ipsp->ips_transport_protocol,
								 0,
								 ipsp->ips_addr_p),
				     extensions) : 1)
#endif
	      && (ipsp->ips_ident_s.type != SADB_IDENTTYPE_RESERVED
		  ? pfkey_safe_build(error = pfkey_ident_build(&extensions[SADB_EXT_IDENTITY_SRC],
							       SADB_EXT_IDENTITY_SRC,
							       ipsp->ips_ident_s.type,
							       ipsp->ips_ident_s.id,
							       ipsp->ips_ident_s.len,
							       ipsp->ips_ident_s.data),
				     extensions) : 1)

	      && (ipsp->ips_ident_d.type != SADB_IDENTTYPE_RESERVED
		  ? pfkey_safe_build(error = pfkey_ident_build(&extensions[SADB_EXT_IDENTITY_DST],
							       SADB_EXT_IDENTITY_DST,
							       ipsp->ips_ident_d.type,
							       ipsp->ips_ident_d.id,
							       ipsp->ips_ident_d.len,
							       ipsp->ips_ident_d.data),
				     extensions) : 1)
#if 0
	      /* FIXME: This won't work yet because I have not finished
		 it. */
	      && (ipsp->ips_sens_
		  ? pfkey_safe_build(error = pfkey_sens_build(&extensions[SADB_EXT_SENSITIVITY],
							      ipsp->ips_sens_dpd,
							      ipsp->ips_sens_sens_level,
							      ipsp->ips_sens_sens_len,
							      ipsp->ips_sens_sens_bitmap,
							      ipsp->ips_sens_integ_level,
							      ipsp->ips_sens_integ_len,
							      ipsp->ips_sens_integ_bitmap),
				     extensions) : 1)
#endif
	      && pfkey_safe_build(error = pfkey_prop_build(&extensions[SADB_EXT_PROPOSAL],
							   64, /* replay */
							   sizeof(comb)/sizeof(struct sadb_comb),
							   &(comb[0])),
				  extensions)
		)) {
		KLIPS_PRINT(1|debug_pfkey, "klips_debug:pfkey_acquire: "
			    "failed to build the acquire message extensions\n");
		SENDERR(-error);
	}
	
	if ((error = pfkey_msg_build(&pfkey_msg, extensions, EXT_BITS_OUT))) {
		KLIPS_PRINT(1|debug_pfkey, "klips_debug:pfkey_acquire: "
			    "failed to build the acquire message\n");
		SENDERR(-error);
	}

#if KLIPS_PFKEY_ACQUIRE_LOSSAGE > 0
	if(sysctl_ipsec_regress_pfkey_lossage) {
		return(0);
	}
#endif	
	
	/* this should go to all registered sockets for that satype only */
	for(pfkey_socketsp = pfkey_registered_sockets[satype];
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_msg))) {
			KLIPS_PRINT(1|debug_pfkey, "klips_debug:pfkey_acquire: "
				    "sending up acquire message for satype=%d(%s) to socket=0p%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_acquire: "
			    "sending up acquire message for satype=%d(%s) to socket=0p%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	if (pfkey_msg) {
		pfkey_msg_free(&pfkey_msg);
	}
	pfkey_extensions_free(extensions);
	return error;
}

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
int
pfkey_nat_t_new_mapping(struct ipsec_sa *ipsp, struct sockaddr *ipaddr,
	__u16 sport)
{
	struct sadb_ext *extensions[SADB_EXT_MAX+1];
	struct sadb_msg *pfkey_msg = NULL;
	struct socket_list *pfkey_socketsp;
	int error = 0;
	uint8_t satype = (ipsp->ips_said.proto==IPPROTO_ESP) ? SADB_SATYPE_ESP : 0;

	/* Construct SADB_X_NAT_T_NEW_MAPPING message */

	pfkey_extensions_init(extensions);

	if((satype == 0) || (satype > SADB_SATYPE_MAX)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
			    "SAtype=%d unspecified or unknown.\n",
			    satype);
		SENDERR(EINVAL);
	}

	if(!(pfkey_registered_sockets[satype])) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
			    "no sockets registered for SAtype=%d(%s).\n",
			    satype,
			    satype2name(satype));
		SENDERR(EPROTONOSUPPORT);
	}

	if (!(pfkey_safe_build
		(error = pfkey_msg_hdr_build(&extensions[0], SADB_X_NAT_T_NEW_MAPPING,
			satype, 0, ++pfkey_msg_seq, 0), extensions)
		/* SA */
		&& pfkey_safe_build
		(error = pfkey_sa_build(&extensions[SADB_EXT_SA],
			SADB_EXT_SA, ipsp->ips_said.spi, 0, 0, 0, 0, 0), extensions)
		/* ADDRESS_SRC = old addr */
		&& pfkey_safe_build
		(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_SRC],
			SADB_EXT_ADDRESS_SRC, ipsp->ips_said.proto, 0, ipsp->ips_addr_s),
			extensions)
		/* NAT_T_SPORT = old port */
	    && pfkey_safe_build
		(error = pfkey_x_nat_t_port_build(&extensions[SADB_X_EXT_NAT_T_SPORT],
			SADB_X_EXT_NAT_T_SPORT, ipsp->ips_natt_sport), extensions)
		/* ADDRESS_DST = new addr */
		&& pfkey_safe_build
		(error = pfkey_address_build(&extensions[SADB_EXT_ADDRESS_DST],
			SADB_EXT_ADDRESS_DST, ipsp->ips_said.proto, 0, ipaddr), extensions)
		/* NAT_T_DPORT = new port */
	    && pfkey_safe_build
		(error = pfkey_x_nat_t_port_build(&extensions[SADB_X_EXT_NAT_T_DPORT],
			SADB_X_EXT_NAT_T_DPORT, sport), extensions)
		)) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
			    "failed to build the nat_t_new_mapping message extensions\n");
		SENDERR(-error);
	}
	
	if ((error = pfkey_msg_build(&pfkey_msg, extensions, EXT_BITS_OUT))) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
			    "failed to build the nat_t_new_mapping message\n");
		SENDERR(-error);
	}

	/* this should go to all registered sockets for that satype only */
	for(pfkey_socketsp = pfkey_registered_sockets[satype];
	    pfkey_socketsp;
	    pfkey_socketsp = pfkey_socketsp->next) {
		if((error = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_msg))) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
				    "sending up nat_t_new_mapping message for satype=%d(%s) to socket=%p failed with error=%d.\n",
				    satype,
				    satype2name(satype),
				    pfkey_socketsp->socketp,
				    error);
			SENDERR(-error);
		}
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_nat_t_new_mapping: "
			    "sending up nat_t_new_mapping message for satype=%d(%s) to socket=%p succeeded.\n",
			    satype,
			    satype2name(satype),
			    pfkey_socketsp->socketp);
	}
	
 errlab:
	if (pfkey_msg) {
		pfkey_msg_free(&pfkey_msg);
	}
	pfkey_extensions_free(extensions);
	return error;
}

DEBUG_NO_STATIC int
pfkey_x_nat_t_new_mapping_parse(struct sock *sk, struct sadb_ext **extensions, struct pfkey_extracted_data* extr)
{
	/* SADB_X_NAT_T_NEW_MAPPING not used in kernel */
	return -EINVAL;
}
#endif

DEBUG_NO_STATIC int (*ext_processors[SADB_EXT_MAX+1])(struct sadb_ext *pfkey_ext, struct pfkey_extracted_data* extr) =
{
  NULL, /* pfkey_msg_process, */
        pfkey_sa_process,
        pfkey_lifetime_process,
        pfkey_lifetime_process,
        pfkey_lifetime_process,
        pfkey_address_process,
        pfkey_address_process,
        pfkey_address_process,
        pfkey_key_process,
        pfkey_key_process,
        pfkey_ident_process,
        pfkey_ident_process,
        pfkey_sens_process,
        pfkey_prop_process,
        pfkey_supported_process,
        pfkey_supported_process,
        pfkey_spirange_process,
        pfkey_x_kmprivate_process,
        pfkey_x_satype_process,
        pfkey_sa_process,
        pfkey_address_process,
        pfkey_address_process,
        pfkey_address_process,
        pfkey_address_process,
        pfkey_address_process,
	pfkey_x_debug_process,
	pfkey_x_protocol_process
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	,
	pfkey_x_nat_t_type_process,
	pfkey_x_nat_t_port_process,
	pfkey_x_nat_t_port_process,
	pfkey_address_process
#endif	
};


DEBUG_NO_STATIC int (*msg_parsers[SADB_MAX +1])(struct sock *sk, struct sadb_ext *extensions[], struct pfkey_extracted_data* extr)
 =
{
	NULL, /* RESERVED */
	pfkey_getspi_parse,
	pfkey_update_parse,
	pfkey_add_parse,
	pfkey_delete_parse,
	pfkey_get_parse,
	pfkey_acquire_parse,
	pfkey_register_parse,
	pfkey_expire_parse,
	pfkey_flush_parse,
	pfkey_dump_parse,
	pfkey_x_promisc_parse,
	pfkey_x_pchange_parse,
	pfkey_x_grpsa_parse,
	pfkey_x_addflow_parse,
	pfkey_x_delflow_parse,
	pfkey_x_msg_debug_parse
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	, pfkey_x_nat_t_new_mapping_parse
#endif	
};

int
pfkey_build_reply(struct sadb_msg *pfkey_msg,
		  struct pfkey_extracted_data *extr,
		  struct sadb_msg **pfkey_reply)
{
	struct sadb_ext *extensions[SADB_EXT_MAX+1];
	int error = 0;
	int msg_type = pfkey_msg->sadb_msg_type;
	int seq = pfkey_msg->sadb_msg_seq;

	KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_build_reply: "
		    "building reply with type: %d\n",
		    msg_type);
	pfkey_extensions_init(extensions);
	if (!extr || !extr->ips) {
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_build_reply: "
				    "bad ipsec_sa passed\n");
			return EINVAL;
	}
	error = pfkey_safe_build(pfkey_msg_hdr_build(&extensions[0],
						     msg_type,
						     proto2satype(extr->ips->ips_said.proto),
						     0,
						     seq,
						     pfkey_msg->sadb_msg_pid),
				 extensions) &&
		(!(extensions_bitmaps[EXT_BITS_OUT][EXT_BITS_REQ][msg_type] &
		   1 << SADB_EXT_SA)
		 || pfkey_safe_build(pfkey_sa_ref_build(&extensions[SADB_EXT_SA],
						    SADB_EXT_SA,
						    extr->ips->ips_said.spi,
						    extr->ips->ips_replaywin,
						    extr->ips->ips_state,
						    extr->ips->ips_authalg,
						    extr->ips->ips_encalg,
						    extr->ips->ips_flags,
						    extr->ips->ips_ref),
				     extensions)) &&
		(!(extensions_bitmaps[EXT_BITS_OUT][EXT_BITS_REQ][msg_type] &
		   1 << SADB_EXT_LIFETIME_CURRENT)
		 || pfkey_safe_build(pfkey_lifetime_build(&extensions
							  [SADB_EXT_LIFETIME_CURRENT],
							  SADB_EXT_LIFETIME_CURRENT,
							  extr->ips->ips_life.ipl_allocations.ipl_count,
							  extr->ips->ips_life.ipl_bytes.ipl_count,
							  extr->ips->ips_life.ipl_addtime.ipl_count,
							  extr->ips->ips_life.ipl_usetime.ipl_count,
							  extr->ips->ips_life.ipl_packets.ipl_count),
				     extensions)) &&
		(!(extensions_bitmaps[EXT_BITS_OUT][EXT_BITS_REQ][msg_type] &
		   1 << SADB_EXT_ADDRESS_SRC)
		 || pfkey_safe_build(pfkey_address_build(&extensions[SADB_EXT_ADDRESS_SRC],
							 SADB_EXT_ADDRESS_SRC,
							 extr->ips->ips_said.proto,
							 0,
							 extr->ips->ips_addr_s),
				     extensions)) &&
		(!(extensions_bitmaps[EXT_BITS_OUT][EXT_BITS_REQ][msg_type] &
		   1 << SADB_EXT_ADDRESS_DST)
		 || pfkey_safe_build(pfkey_address_build(&extensions[SADB_EXT_ADDRESS_DST],
							 SADB_EXT_ADDRESS_DST,
							 extr->ips->ips_said.proto,
							 0,
							 extr->ips->ips_addr_d),
				     extensions));

	if (error == 0) {
		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_build_reply: "
			    "building extensions failed\n");
		return EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_build_reply: "
		    "built extensions, proceed to build the message\n");
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_build_reply: "
		    "extensions[1]=0p%p\n",
		    extensions[1]);
	error = pfkey_msg_build(pfkey_reply, extensions, EXT_BITS_OUT);
	pfkey_extensions_free(extensions);

	return error;
}

int
pfkey_msg_interp(struct sock *sk, struct sadb_msg *pfkey_msg,
				struct sadb_msg **pfkey_reply)
{
	int error = 0;
	int i;
	struct sadb_ext *extensions[SADB_EXT_MAX+1];
	struct pfkey_extracted_data extr = {NULL, NULL, NULL};
	
	pfkey_extensions_init(extensions);
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_msg_interp: "
		    "parsing message ver=%d, type=%d, errno=%d, satype=%d(%s), len=%d, res=%d, seq=%d, pid=%d.\n", 
		    pfkey_msg->sadb_msg_version,
		    pfkey_msg->sadb_msg_type,
		    pfkey_msg->sadb_msg_errno,
		    pfkey_msg->sadb_msg_satype,
		    satype2name(pfkey_msg->sadb_msg_satype),
		    pfkey_msg->sadb_msg_len,
		    pfkey_msg->sadb_msg_reserved,
		    pfkey_msg->sadb_msg_seq,
		    pfkey_msg->sadb_msg_pid);
	
	extr.ips = ipsec_sa_alloc(&error); /* pass in error var by pointer */
	if(extr.ips == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_msg_interp: "
			    "memory allocation error.\n");
		SENDERR(-error);
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_msg_interp: "
		    "allocated extr->ips=0p%p.\n",
		    extr.ips);
	
	if(pfkey_msg->sadb_msg_satype > SADB_SATYPE_MAX) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_msg_interp: "
			    "satype %d > max %d\n", 
			    pfkey_msg->sadb_msg_satype,
			    SADB_SATYPE_MAX);
		SENDERR(EINVAL);
	}
	
	switch(pfkey_msg->sadb_msg_type) {
	case SADB_GETSPI:
	case SADB_UPDATE:
	case SADB_ADD:
	case SADB_DELETE:
	case SADB_X_GRPSA:
	case SADB_X_ADDFLOW:
		if(!(extr.ips->ips_said.proto = satype2proto(pfkey_msg->sadb_msg_satype))) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_msg_interp: "
				    "satype %d lookup failed.\n", 
				    pfkey_msg->sadb_msg_satype);
			SENDERR(EINVAL);
		} else {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_msg_interp: "
				    "satype %d lookups to proto=%d.\n", 
				    pfkey_msg->sadb_msg_satype,
				    extr.ips->ips_said.proto);
		}
		break;
	default:
		break;
	}
	
	/* The NULL below causes the default extension parsers to be used */
	/* Parse the extensions */
	if((error = pfkey_msg_parse(pfkey_msg, NULL, extensions, EXT_BITS_IN)))
	{
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_msg_interp: "
			    "message parsing failed with error %d.\n",
			    error); 
		SENDERR(-error);
	}
	
	/* Process the extensions */
	for(i=1; i <= SADB_EXT_MAX;i++)	{
		if(extensions[i] != NULL) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_msg_interp: "
				    "processing ext %d 0p%p with processor 0p%p.\n", 
				    i, extensions[i], ext_processors[i]);
			if((error = ext_processors[i](extensions[i], &extr))) {
				KLIPS_PRINT(debug_pfkey,
					    "klips_debug:pfkey_msg_interp: "
					    "extension processing for type %d failed with error %d.\n",
					    i,
					    error); 
				SENDERR(-error);
			}
			
		}
		
	}
	
	/* Parse the message types */
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_msg_interp: "
		    "parsing message type %d(%s) with msg_parser 0p%p.\n",
		    pfkey_msg->sadb_msg_type,
		    pfkey_v2_sadb_type_string(pfkey_msg->sadb_msg_type),
		    msg_parsers[pfkey_msg->sadb_msg_type]); 
	if((error = msg_parsers[pfkey_msg->sadb_msg_type](sk, extensions, &extr))) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_msg_interp: "
			    "message parsing failed with error %d.\n",
			    error); 
		SENDERR(-error);
	}

#if 0
	error = pfkey_build_reply(pfkey_msg, &extr, pfkey_reply);
	if (error) {
		*pfkey_reply = NULL;
	}
#endif	
 errlab:
	if(extr.ips != NULL) {
		ipsec_sa_wipe(extr.ips);
	}
	if(extr.ips2 != NULL) {
		ipsec_sa_wipe(extr.ips2);
	}
	if (extr.eroute != NULL) {
		kfree(extr.eroute);
	}
	return(error);
}

/*
 * $Log: pfkey_v2_parser.c,v $
 * Revision 1.134.2.1  2006/05/01 14:37:25  mcr
 * ip_chk_addr -> inet_addr_type for more direct 2.4/2.6 support.
 *
 * Revision 1.134  2005/05/11 01:48:20  mcr
 * 	removed "poor-man"s OOP in favour of proper C structures.
 *
 * Revision 1.133  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.132  2005/04/14 20:56:24  mcr
 * 	moved (pfkey_)ipsec_sa_init to ipsec_sa.c.
 *
 * Revision 1.131  2005/01/26 00:50:35  mcr
 * 	adjustment of confusion of CONFIG_IPSEC_NAT vs CONFIG_KLIPS_NAT,
 * 	and make sure that NAT_TRAVERSAL is set as well to match
 * 	userspace compiles of code.
 *
 * Revision 1.130  2004/09/08 17:21:36  ken
 * Rename MD5* -> osMD5 functions to prevent clashes with other symbols exported by kernel modules (CIFS in 2.6 initiated this)
 *
 * Revision 1.129  2004/09/06 18:36:30  mcr
 * 	if a protocol can not be found, then log it. This is not
 * 	debugging.
 *
 * Revision 1.128  2004/08/21 00:45:19  mcr
 * 	CONFIG_KLIPS_NAT was wrong, also need to include udp.h.
 *
 * Revision 1.127  2004/08/20 21:45:45  mcr
 * 	CONFIG_KLIPS_NAT_TRAVERSAL is not used in an attempt to
 * 	be 26sec compatible. But, some defines where changed.
 *
 * Revision 1.126  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
 *
 * Revision 1.125  2004/08/04 15:57:07  mcr
 * 	moved des .h files to include/des/ *
 * 	included 2.6 protocol specific things
 * 	started at NAT-T support, but it will require a kernel patch.
 *
 * Revision 1.124  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.123  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.122.2.2  2004/04/05 04:30:46  mcr
 * 	patches for alg-branch to compile/work with 2.x openswan
 *
 * Revision 1.122.2.1  2003/12/22 15:25:52  jjo
 * . Merged algo-0.8.1-rc11-test1 into alg-branch
 *
 * Revision 1.122  2003/12/10 01:14:27  mcr
 * 	NAT-traversal patches to KLIPS.
 *
 * Revision 1.121  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.120.4.2  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.120.4.1  2003/09/21 13:59:56  mcr
 * 	pre-liminary X.509 patch - does not yet pass tests.
 *
 * Revision 1.120  2003/04/03 17:38:09  rgb
 * Centralised ipsec_kfree_skb and ipsec_dev_{get,put}.
 *
 * Revision 1.119  2003/02/06 01:52:37  rgb
 * Removed no longer relevant comment
 *
 * Revision 1.118  2003/01/30 02:32:44  rgb
 *
 * Transmit error code through to caller from callee for better diagnosis of problems.
 *
 * Revision 1.117  2003/01/16 18:48:13  rgb
 *
 * Fixed sign bug in error return from an sa allocation call in
 * pfkey_msg_interp.
 *
 * Revision 1.116  2002/10/17 16:38:01  rgb
 * Change pfkey_alloc_eroute() to never static since its consumers
 * have been moved outside the file.
 *
 * Revision 1.115  2002/10/12 23:11:53  dhr
 *
 * [KenB + DHR] more 64-bit cleanup
 *
 * Revision 1.114  2002/10/05 05:02:58  dhr
 *
 * C labels go on statements
 *
 * Revision 1.113  2002/09/30 19:11:22  rgb
 * 	Turn on debugging for upgoing acquire messages to test for reliability.
 *
 * Revision 1.112  2002/09/20 15:41:16  rgb
 * Switch from pfkey_alloc_ipsec_sa() to ipsec_sa_alloc().
 * Added sadb_x_sa_ref to struct sadb_sa.
 * Added ref parameter to pfkey_sa_build().
 *
 * Revision 1.111  2002/09/20 05:02:08  rgb
 * Added memory allocation debugging.
 * Convert to switch to divulge hmac keys for debugging.
 * Added text labels to elucidate numeric values presented.
 *
 * Revision 1.110  2002/08/03 18:03:05  mcr
 * 	loop that checks for SPI's to have been already linked
 * 	fails to actually step to next pointer, but continuously
 * 	resets to head of list. Wrong pointer used.
 * 	test east-icmp-02 revealed this.
 *
 * Revision 1.109  2002/07/26 08:48:31  rgb
 * Added SA ref table code.
 *
 * Revision 1.108  2002/05/27 18:55:03  rgb
 * Remove final vistiges of tdb references via IPSEC_KLIPS1_COMPAT.
 *
 * Revision 1.107  2002/05/23 07:16:08  rgb
 * Added ipsec_sa_put() for releasing an ipsec_sa refcount.
 * Pointer clean-up.
 * Added refcount code.
 *
 * Revision 1.106  2002/05/14 02:34:13  rgb
 * Converted reference from ipsec_sa_put to ipsec_sa_add to avoid confusion
 * with "put" usage in the kernel.
 * Change all references to tdb, TDB or Tunnel Descriptor Block to ips,
 * ipsec_sa or ipsec_sa.
 * Moved all the extension parsing functions to pfkey_v2_ext_process.c.
 *
 * Revision 1.105  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.104  2002/04/24 07:36:34  mcr
 * Moved from ./klips/net/ipsec/pfkey_v2_parser.c,v
 *
 * Revision 1.103  2002/04/20 00:12:25  rgb
 * Added esp IV CBC attack fix, disabled.
 *
 * Revision 1.102  2002/03/08 01:15:17  mcr
 * 	put some internal structure only debug messages behind
 * 	&& sysctl_ipsec_debug_verbose.
 *
 * Revision 1.101  2002/01/29 17:17:57  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.100  2002/01/29 04:00:54  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.99  2002/01/29 02:13:19  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.98  2002/01/12 02:57:57  mcr
 * 	first regression test causes acquire messages to be lost
 * 	100% of the time. This is to help testing of pluto.
 *
 * Revision 1.97  2001/11/26 09:23:52  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.93.2.4  2001/10/23 04:20:27  mcr
 * 	parity was forced on wrong structure! prototypes help here.
 *
 * Revision 1.93.2.3  2001/10/22 21:14:59  mcr
 * 	include des.h, removed phony prototypes and fixed calling
 * 	conventions to match real prototypes.
 *
 * Revision 1.93.2.2  2001/10/15 05:39:03  mcr
 * 	%08lx is not the right format for u32. Use %08x. 64-bit safe? ha.
 *
 * Revision 1.93.2.1  2001/09/25 02:30:14  mcr
 * 	struct tdb -> struct ipsec_sa.
 * 	use new lifetime structure. common format routines for debug.
 *
 * Revision 1.96  2001/11/06 20:47:54  rgb
 * Fixed user context call to ipsec_dev_start_xmit() bug.  Call
 * dev_queue_xmit() instead.
 *
 * Revision 1.95  2001/11/06 19:47:46  rgb
 * Added packet parameter to lifetime and comb structures.
 *
 * Revision 1.94  2001/10/18 04:45:23  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.93  2001/09/20 15:32:59  rgb
 * Min/max cleanup.
 *
 * Revision 1.92  2001/09/19 16:35:48  rgb
 * PF_KEY ident fix for getspi from NetCelo (puttdb duplication).
 *
 * Revision 1.91  2001/09/15 16:24:06  rgb
 * Re-inject first and last HOLD packet when an eroute REPLACE is done.
 *
 * Revision 1.90  2001/09/14 16:58:38  rgb
 * Added support for storing the first and last packets through a HOLD.
 *
 * Revision 1.89  2001/09/08 21:14:07  rgb
 * Added pfkey ident extension support for ISAKMPd. (NetCelo)
 * Better state coherency (error management) between pf_key and IKE daemon.
 * (NetCelo)
 *
 * Revision 1.88  2001/08/27 19:42:44  rgb
 * Fix memory leak of encrypt and auth structs in pfkey register.
 *
 * Revision 1.87  2001/07/06 19:50:46  rgb
 * Removed unused debugging code.
 * Added inbound policy checking code for IPIP SAs.
 *
 * Revision 1.86  2001/06/20 06:26:04  rgb
 * Changed missing SA errors from EEXIST to ENOENT and added debug output
 * for already linked SAs.
 *
 * Revision 1.85  2001/06/15 04:57:02  rgb
 * Remove single error return condition check and check for all errors in
 * the case of a replace eroute delete operation.  This means that
 * applications must expect to be deleting something before replacing it
 * and if nothing is found, complain.
 *
 * Revision 1.84  2001/06/14 19:35:12  rgb
 * Update copyright date.
 *
 * Revision 1.83  2001/06/12 00:03:19  rgb
 * Silence debug set/unset under normal conditions.
 *
 * Revision 1.82  2001/05/30 08:14:04  rgb
 * Removed vestiges of esp-null transforms.
 *
 * Revision 1.81  2001/05/27 06:12:12  rgb
 * Added structures for pid, packet count and last access time to eroute.
 * Added packet count to beginning of /proc/net/ipsec_eroute.
 *
 * Revision 1.80  2001/05/03 19:43:59  rgb
 * Check error return codes for all build function calls.
 * Standardise on SENDERR() macro.
 *
 * Revision 1.79  2001/04/20 21:09:16  rgb
 * Cleaned up fixed tdbwipes.
 * Free pfkey_reply and clean up extensions_reply for grpsa, addflow and
 * delflow (Per Cederqvist) plugging memleaks.
 *
 * Revision 1.78  2001/04/19 19:02:39  rgb
 * Fixed extr.tdb freeing, stealing it for getspi, update and add.
 * Refined a couple of spinlocks, fixed the one in update.
 *
 * Revision 1.77  2001/04/18 20:26:16  rgb
 * Wipe/free eroute and both tdbs from extr at end of pfkey_msg_interp()
 * instead of inside each message type parser.  This fixes two memleaks.
 *
 * Revision 1.76  2001/04/17 23:51:18  rgb
 * Quiet down pfkey_x_debug_process().
 *
 * Revision 1.75  2001/03/29 01:55:05  rgb
 * Fixed pfkey key init memleak.
 * Fixed pfkey encryption key debug output.
 *
 * Revision 1.74  2001/03/27 05:29:14  rgb
 * Debug output cleanup/silencing.
 *
 * Revision 1.73  2001/02/28 05:03:28  rgb
 * Clean up and rationalise startup messages.
 *
 * Revision 1.72  2001/02/27 22:24:56  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.71  2001/02/27 06:59:30  rgb
 * Added satype2name() conversions most places satype is debug printed.
 *
 * Revision 1.70  2001/02/26 22:37:08  rgb
 * Fixed 'unknown proto' INT bug in new code.
 * Added satype to protocol debugging instrumentation.
 *
 * Revision 1.69  2001/02/26 19:57:51  rgb
 * Re-formatted debug output (split lines, consistent spacing).
 * Fixed as yet undetected FLUSH bug which called ipsec_tdbcleanup()
 * with an satype instead of proto.
 * Checked for satype consistency and fixed minor bugs.
 * Fixed undetected ungrpspi bug that tried to upmsg a second tdb.
 * Check for satype sanity in pfkey_expire().
 * Added satype sanity check to addflow.
 *
 * Revision 1.68  2001/02/12 23:14:40  rgb
 * Remove double spin lock in pfkey_expire().
 *
 * Revision 1.67  2001/01/31 19:23:40  rgb
 * Fixed double-unlock bug introduced by grpsa upmsg (found by Lars Heete).
 *
 * Revision 1.66  2001/01/29 22:20:04  rgb
 * Fix minor add upmsg lifetime bug.
 *
 * Revision 1.65  2001/01/24 06:12:33  rgb
 * Fixed address extension compile bugs just introduced.
 *
 * Revision 1.64  2001/01/24 00:31:15  rgb
 * Added upmsg for addflow/delflow.
 *
 * Revision 1.63  2001/01/23 22:02:55  rgb
 * Added upmsg to x_grpsa.
 * Fixed lifetimes extentions to add/update/get upmsg.
 *
 * Revision 1.62  2000/11/30 21:47:51  rgb
 * Fix error return bug after returning from pfkey_tdb_init().
 *
 * Revision 1.61  2000/11/17 18:10:29  rgb
 * Fixed bugs mostly relating to spirange, to treat all spi variables as
 * network byte order since this is the way PF_KEYv2 stored spis.
 *
 * Revision 1.60  2000/11/06 04:34:53  rgb
 * Changed non-exported functions to DEBUG_NO_STATIC.
 * Add Svenning's adaptive content compression.
 * Ditched spin_lock_irqsave in favour of spin_lock/_bh.
 * Fixed double unlock bug (Svenning).
 * Fixed pfkey_msg uninitialized bug in pfkey_{expire,acquire}().
 * Fixed incorrect extension type (prop) in pfkey)acquire().
 *
 * Revision 1.59  2000/10/11 15:25:12  rgb
 * Fixed IPCOMP disabled compile bug.
 *
 * Revision 1.58  2000/10/11 14:54:03  rgb
 * Fixed pfkey_acquire() satype to SADB_SATYPE_ESP and removed pfkey
 * protocol violations of setting pfkey_address_build() protocol parameter
 * to non-zero except in the case of pfkey_acquire().
 *
 * Revision 1.57  2000/10/10 20:10:18  rgb
 * Added support for debug_ipcomp and debug_verbose to klipsdebug.
 *
 * Revision 1.56  2000/10/06 20:24:36  rgb
 * Fixes to pfkey_acquire to initialize extensions[] and use correct
 * ipproto.
 *
 * Revision 1.55  2000/10/03 03:20:57  rgb
 * Added brackets to get a?b:c scope right for pfkey_register reply.
 *
 * Revision 1.54  2000/09/29 19:49:30  rgb
 * As-yet-unused-bits cleanup.
 *
 * Revision 1.53  2000/09/28 00:35:45  rgb
 * Padded SATYPE printout in pfkey_register for vertical alignment.
 *
 * Revision 1.52  2000/09/20 16:21:58  rgb
 * Cleaned up ident string alloc/free.
 *
 * Revision 1.51  2000/09/20 04:04:20  rgb
 * Changed static functions to DEBUG_NO_STATIC to reveal function names in
 * oopsen.
 *
 * Revision 1.50  2000/09/16 01:10:53  rgb
 * Fixed unused var warning with debug off.
 *
 * Revision 1.49  2000/09/15 11:37:02  rgb
 * Merge in heavily modified Svenning Soerensen's <svenning@post5.tele.dk>
 * IPCOMP zlib deflate code.
 *
 * Revision 1.48  2000/09/15 04:57:57  rgb
 * Cleaned up existing IPCOMP code before svenning addition.
 * Initialize pfkey_reply and extensions_reply in case of early error in
 * message parsing functions (thanks Kai!).
 *
 * Revision 1.47  2000/09/13 08:02:56  rgb
 * Added KMd registration notification.
 *
 * Revision 1.46  2000/09/12 22:35:36  rgb
 * Restructured to remove unused extensions from CLEARFLOW messages.
 *
 * Revision 1.45  2000/09/12 03:24:23  rgb
 * Converted #if0 debugs to sysctl.
 *
 * Revision 1.44  2000/09/09 06:38:39  rgb
 * Correct SADB message type for update, add and delete.
 *
 * Revision 1.43  2000/09/08 19:19:56  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Removed all references to CONFIG_IPSEC_PFKEYv2.
 * Put in sanity checks in most msg type parsers to catch invalid satypes
 * and empty socket lists.
 * Moved spin-locks in pfkey_get_parse() to simplify.
 * Added pfkey_acquire().
 * Added upwards messages to update, add, delete, acquire_parse,
 * expire_parse and flush.
 * Fix pfkey_prop_build() parameter to be only single indirection.
 * Changed all replies to use pfkey_reply.
 * Check return code on puttdb() and deltdbchain() in getspi, update,
 * add, delete.
 * Fixed up all pfkey replies to open and registered sockets.
 *
 * Revision 1.42  2000/09/01 18:50:26  rgb
 * Added a supported algorithms array lists, one per satype and registered
 * existing algorithms.
 * Fixed pfkey_list_{insert,remove}_{socket,support}() to allow change to
 * list.
 * Only send pfkey_expire() messages to sockets registered for that satype.
 * Added reply to pfkey_getspi_parse().
 * Added reply to pfkey_get_parse().
 * Fixed debug output label bug in pfkey_lifetime_process().
 * Cleaned up pfkey_sa_process a little.
 * Moved pfkey_safe_build() above message type parsers to make it available
 * for creating replies.
 * Added comments for future work in pfkey_acquire_parse().
 * Fleshed out guts of pfkey_register_parse().
 *
 * Revision 1.41  2000/08/24 16:58:11  rgb
 * Fixed key debugging variables.
 * Fixed error return code for a failed search.
 * Changed order of pfkey_get operations.
 *
 * Revision 1.40  2000/08/21 16:32:27  rgb
 * Re-formatted for cosmetic consistency and readability.
 *
 * Revision 1.39  2000/08/20 21:38:57  rgb
 * Bugfixes to as-yet-unused pfkey_update_parse() and
 * pfkey_register_parse(). (Momchil)
 * Added functions pfkey_safe_build(), pfkey_expire() and
 * pfkey_build_reply(). (Momchil)
 * Added a pfkey_reply parameter to pfkey_msg_interp(). (Momchil)
 *
 * Revision 1.38  2000/08/18 21:30:41  rgb
 * Purged all tdb_spi, tdb_proto and tdb_dst macros.  They are unclear.
 *
 * Revision 1.37  2000/08/18 18:18:02  rgb
 * Cosmetic and descriptive changes made to debug test.
 * getspi and update fixes from Momchil.
 *
 * Revision 1.36  2000/08/15 15:41:55  rgb
 * Fixed the (as yet unused and untested) pfkey_getspi() routine.
 *
 * Revision 1.35  2000/08/01 14:51:52  rgb
 * Removed _all_ remaining traces of DES.
 *
 * Revision 1.34  2000/07/28 14:58:32  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.33  2000/06/28 05:50:11  rgb
 * Actually set iv_bits.
 *
 * Revision 1.32  2000/05/30 18:36:56  rgb
 * Fix AH auth hash setup bug.  This breaks interop with previous PF_KEY
 * FreeS/WAN, but fixes interop with other implementations.
 *
 * Revision 1.31  2000/03/16 14:05:48  rgb
 * Fixed brace scope preventing non-debug compile.
 * Added null parameter check for pfkey_x_debug().
 *
 * Revision 1.30  2000/01/22 23:21:13  rgb
 * Use new function satype2proto().
 *
 * Revision 1.29  2000/01/22 08:40:21  rgb
 * Invert condition to known value to avoid AF_INET6 in 2.0.36.
 *
 * Revision 1.28  2000/01/22 07:58:57  rgb
 * Fixed REPLACEFLOW bug, missing braces around KLIPS_PRINT *and* SENDERR.
 *
 * Revision 1.27  2000/01/22 03:48:01  rgb
 * Added extr pointer component debugging.
 *
 * Revision 1.26  2000/01/21 09:41:25  rgb
 * Changed a (void*) to (char*) cast to do proper pointer math.
 * Don't call tdbwipe if tdb2 is NULL.
 *
 * Revision 1.25  2000/01/21 06:21:01  rgb
 * Added address cases for eroute flows.
 * Tidied up compiler directive indentation for readability.
 * Added ictx,octx vars for simplification.
 * Added macros for HMAC padding magic numbers.
 * Converted from double tdb arguments to one structure (extr)
 * containing pointers to all temporary information structures
 * and checking for valid arguments to all ext processors and
 * msg type parsers.
 * Added spiungrp'ing.
 * Added klipsdebug switching capability.
 * Removed sa_process() check for zero protocol.
 * Added address case for DST2 for grouping.
 * Added/changed minor debugging instrumentation.
 * Fixed spigrp for single said, ungrouping case.
 * Added code to parse addflow and delflow messages.
 * Removed redundant statements duplicating tdbwipe() functionality
 * and causing double kfrees.
 * Permit addflow to have a protocol of 0.
 *
 * Revision 1.24  1999/12/09 23:23:00  rgb
 * Added check to pfkey_sa_process() to do eroutes.
 * Converted to DIVUP() macro.
 * Converted if() to switch() in pfkey_register_parse().
 * Use new pfkey_extensions_init() instead of memset().
 *
 * Revision 1.23  1999/12/01 22:18:13  rgb
 * Preset minspi and maxspi values in case and spirange extension is not
 * included and check for the presence of an spirange extension before
 * using it.  Initialise tdb_sastate to LARVAL.
 * Fixed debugging output typo.
 * Fixed authentication context initialisation bugs (4 places).
 *
 * Revision 1.22  1999/11/27 11:53:08  rgb
 * Moved pfkey_msg_parse prototype to pfkey.h
 * Moved exts_permitted/required prototype to pfkey.h.
 * Moved sadb_satype2proto protocol lookup table to lib/pfkey_v2_parse.c.
 * Deleted SADB_X_EXT_SA2 code from pfkey_sa_process() since it will never
 * be called.
 * Moved protocol/algorithm checks to lib/pfkey_v2_parse.c
 * Debugging error messages added.
 * Enable lifetime_current checking.
 * Remove illegal requirement for SA extension to be present in an
 * originating GETSPI call.
 * Re-instate requirement for UPDATE or ADD message to be MATURE.
 * Add argument to pfkey_msg_parse() for direction.
 * Fixed IPIP dst address bug and purged redundant, leaky code.
 *
 * Revision 1.21  1999/11/24 05:24:20  rgb
 * hanged 'void*extensions' to 'struct sadb_ext*extensions'.
 * Fixed indention.
 * Ditched redundant replay check.
 * Fixed debug message text from 'parse' to 'process'.
 * Added more debug output.
 * Forgot to zero extensions array causing bug, fixed.
 *
 * Revision 1.20  1999/11/23 23:08:13  rgb
 * Move all common parsing code to lib/pfkey_v2_parse.c and rename
 * remaining bits to *_process. (PJO)
 * Add macros for dealing with alignment and rounding up more opaquely.
 * Use provided macro ADDRTOA_BUF instead of hardcoded value.
 * Sort out pfkey and freeswan headers, putting them in a library path.
 * Corrected a couple of bugs in as-yet-inactive code.
 *
 * Revision 1.19  1999/11/20 22:01:10  rgb
 * Add more descriptive error messages for non-zero reserved fields.
 * Add more descriptive error message for spirange parsing.
 * Start on supported extension parsing.
 * Start on register and get message parsing.
 *
 * Revision 1.18  1999/11/18 04:09:20  rgb
 * Replaced all kernel version macros to shorter, readable form.
 *
 * Revision 1.17  1999/11/17 15:53:41  rgb
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 *
 * Revision 1.16  1999/10/26 16:57:43  rgb
 * Add shorter macros for compiler directives to visually clean-up.
 * Give ipv6 code meaningful compiler directive.
 * Add comments to other #if 0 debug code.
 * Remove unused *_bh_atomic() calls.
 * Fix mis-placed spinlock.
 *
 * Revision 1.15  1999/10/16 18:27:10  rgb
 * Clean-up unused cruft.
 * Fix-up lifetime_allocations_c and lifetime_addtime_c initialisations.
 *
 * Revision 1.14  1999/10/08 18:37:34  rgb
 * Fix end-of-line spacing to sate whining PHMs.
 *
 * Revision 1.13  1999/10/03 18:49:12  rgb
 * Spinlock fixes for 2.0.xx and 2.3.xx.
 *
 * Revision 1.12  1999/10/01 15:44:54  rgb
 * Move spinlock header include to 2.1> scope.
 *
 * Revision 1.11  1999/10/01 00:05:45  rgb
 * Added tdb structure locking.
 * Use 'jiffies' instead of do_get_timeofday().
 * Fix lifetime assignments.
 *
 * Revision 1.10  1999/09/21 15:24:45  rgb
 * Rework spirange code to save entropy and prevent endless loops.
 *
 * Revision 1.9  1999/09/16 12:10:21  rgb
 * Minor fixes to random spi selection for correctness and entropy conservation.
 *
 * Revision 1.8  1999/05/25 22:54:46  rgb
 * Fix comparison that should be an assignment in an if.
 *
 * Revision 1.7  1999/05/09 03:25:37  rgb
 * Fix bug introduced by 2.2 quick-and-dirty patch.
 *
 * Revision 1.6  1999/05/08 21:32:30  rgb
 * Fix error return reporting.
 *
 * Revision 1.5  1999/05/05 22:02:33  rgb
 * Add a quick and dirty port to 2.2 kernels by Marc Boucher <marc@mbsi.ca>.
 *
 * Revision 1.4  1999/04/29 15:22:40  rgb
 * Standardise an error return method.
 * Add debugging instrumentation.
 * Add check for existence of macros min/max.
 * Add extensions permitted/required in/out filters.
 * Add satype-to-protocol table.
 * Add a second tdb pointer to each parser to accomodate GRPSA.
 * Move AH & no_algo_set to GETSPI, UPDATE and ADD.
 * Add OOO window check.
 * Add support for IPPROTO_IPIP and hooks for IPPROTO_COMP.
 * Add timestamp to lifetime parse.
 * Fix address structure length checking bug.
 * Fix address structure allocation bug (forgot to kmalloc!).
 * Add checks for extension lengths.
 * Add checks for extension reserved illegal values.
 * Add check for spirange legal values.
 * Add an extension type for parsing a second satype, SA and
 * DST_ADDRESS.
 * Make changes to tdb_init() template to get pfkey_tdb_init(),
 * eliminating any mention of xformsw.
 * Implement getspi, update and grpsa (not tested).
 * Add stubs for as yet unimplemented message types.
 * Add table of message parsers to substitute for msg_parse switch.
 *
 * Revision 1.3  1999/04/15 17:58:07  rgb
 * Add RCSID labels.
 *
 * Revision 1.2  1999/04/15 15:37:26  rgb
 * Forward check changes from POST1_00 branch.
 *
 * Revision 1.1.2.1  1999/03/26 20:58:56  rgb
 * Add pfkeyv2 support to KLIPS.
 *
 * Local variables:
 * c-file-style: "linux"
 * End:
 *
 */
