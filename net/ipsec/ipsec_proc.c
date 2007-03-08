/*
 * @(#) /proc file system interface code.
 *
 * Copyright (C) 1996, 1997  John Ioannidis.
 * Copyright (C) 1998, 1999, 2000, 2001  Richard Guy Briggs <rgb@freeswan.org>
 *                                 2001  Michael Richardson <mcr@freeswan.org>
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
 * Split out from ipsec_init.c version 1.70.
 */

char ipsec_proc_c_version[] = "RCSID $Id: ipsec_proc.c,v 1.39.2.2 2006/02/13 18:48:12 paul Exp $";


#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>
#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */

#include "openswan/ipsec_kversion.h"
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
#include <linux/in.h>          /* struct sockaddr_in */
#include <linux/skbuff.h>
#include <asm/uaccess.h>       /* copy_from_user */
#include <openswan.h>
#ifdef SPINLOCK
#ifdef SPINLOCK_23
#include <linux/spinlock.h> /* *lock* */
#else /* SPINLOCK_23 */
#include <asm/spinlock.h> /* *lock* */
#endif /* SPINLOCK_23 */
#endif /* SPINLOCK */

#include <net/ip.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#ifdef NETLINK_SOCK
#include <linux/netlink.h>
#else
#include <net/netlink.h>
#endif

#include "openswan/radij.h"

#include "openswan/ipsec_life.h"
#include "openswan/ipsec_stats.h"
#include "openswan/ipsec_sa.h"

#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_tunnel.h"
#include "openswan/ipsec_xmit.h"

#include "openswan/ipsec_rcv.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"
#include "openswan/ipsec_kern24.h"

#ifdef CONFIG_KLIPS_IPCOMP
#include "openswan/ipcomp.h"
#endif /* CONFIG_KLIPS_IPCOMP */

#include "openswan/ipsec_proto.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#ifdef CONFIG_PROC_FS

#ifdef IPSEC_PROC_SUBDIRS
static struct proc_dir_entry *proc_net_ipsec_dir = NULL;
static struct proc_dir_entry *proc_eroute_dir    = NULL;
static struct proc_dir_entry *proc_spi_dir       = NULL;
static struct proc_dir_entry *proc_spigrp_dir    = NULL;
static struct proc_dir_entry *proc_birth_dir     = NULL;
static struct proc_dir_entry *proc_stats_dir     = NULL;
#endif

struct ipsec_birth_reply ipsec_ipv4_birth_packet;
struct ipsec_birth_reply ipsec_ipv6_birth_packet;

#ifdef CONFIG_KLIPS_DEBUG
int debug_esp = 0;
int debug_ah = 0;
#endif /* CONFIG_KLIPS_DEBUG */

#define DECREMENT_UNSIGNED(X, amount) ((amount < (X)) ? (X)-amount : 0)

#ifdef CONFIG_KLIPS_ALG
extern int ipsec_xform_get_info(char *buffer, char **start,
				off_t offset, int length IPSEC_PROC_LAST_ARG);
#endif /* CONFIG_KLIPS_ALG */


IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_eroute_get_info(char *buffer, 
		      char **start, 
		      off_t offset, 
		      int length        IPSEC_PROC_LAST_ARG)
{
	struct wsbuf w = {buffer, length, offset, 0, 0};

#ifdef CONFIG_KLIPS_DEBUG
	if (debug_radij & DB_RJ_DUMPTREES)
	  rj_dumptrees();			/* XXXXXXXXX */
#endif /* CONFIG_KLIPS_DEBUG */

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_eroute_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);

	spin_lock_bh(&eroute_lock);

	rj_walktree(rnh, ipsec_rj_walker_procprint, &w);
/*	rj_walktree(mask_rjhead, ipsec_rj_walker_procprint, &w); */

	spin_unlock_bh(&eroute_lock);

	*start = buffer + (offset - w.begin);	/* Start of wanted data */
	return w.len - (offset - w.begin);
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_spi_get_info(char *buffer,
		   char **start,
		   off_t offset,
		   int length    IPSEC_PROC_LAST_ARG)
{
	const int max_content = length > 0? length-1 : 0;
	int len = 0;
	off_t begin = 0;
	int i;
	struct ipsec_sa *sa_p;
	char sa[SATOT_BUF];
	char buf_s[SUBNETTOA_BUF];
	char buf_d[SUBNETTOA_BUF];
	size_t sa_len;

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_spi_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);
	
	spin_lock_bh(&tdb_lock);

	for (i = 0; i < SADB_HASHMOD; i++) {
		for (sa_p = ipsec_sadb_hash[i];
		     sa_p;
		     sa_p = sa_p->ips_hnext) {
			atomic_inc(&sa_p->ips_refcount);
			sa_len = satot(&sa_p->ips_said, 'x', sa, sizeof(sa));
			len += ipsec_snprintf(buffer+len, length-len, "%s ",
				       sa_len ? sa : " (error)");

			len += ipsec_snprintf(buffer+len, length-len, "%s%s%s",
				       IPS_XFORM_NAME(sa_p));

			len += ipsec_snprintf(buffer+len, length-len, ": dir=%s",
				       (sa_p->ips_flags & EMT_INBOUND) ?
				       "in " : "out");

			if(sa_p->ips_addr_s) {
				addrtoa(((struct sockaddr_in*)(sa_p->ips_addr_s))->sin_addr,
					0, buf_s, sizeof(buf_s));
				len += ipsec_snprintf(buffer+len, length-len, " src=%s",
					       buf_s);
			}

			if((sa_p->ips_said.proto == IPPROTO_IPIP)
			   && (sa_p->ips_flags & SADB_X_SAFLAGS_INFLOW)) {
				subnettoa(sa_p->ips_flow_s.u.v4.sin_addr,
					  sa_p->ips_mask_s.u.v4.sin_addr,
					  0,
					  buf_s,
					  sizeof(buf_s));

				subnettoa(sa_p->ips_flow_d.u.v4.sin_addr,
					  sa_p->ips_mask_d.u.v4.sin_addr,
					  0,
					  buf_d,
					  sizeof(buf_d));

				len += ipsec_snprintf(buffer+len, length-len, " policy=%s->%s",
					       buf_s, buf_d);
			}
			
			if(sa_p->ips_iv_bits) {
				int j;
				len += ipsec_snprintf(buffer+len, length-len, " iv_bits=%dbits iv=0x",
					       sa_p->ips_iv_bits);

#ifdef CONFIG_KLIPS_OCF
				if (!sa_p->ips_iv) {
					/* ocf doesn't set the IV, fake it for the UML tests */
					len += ipsec_snprintf(buffer+len, length-len, "0cf0");
					for (j = 0; j < (sa_p->ips_iv_bits / 8) - 2; j++) {
						len += ipsec_snprintf(buffer+len, length-len, "%02x",
								   (((__u32)sa_p) >> j) & 0xff);
					}
				} else
#endif
				for(j = 0; j < sa_p->ips_iv_bits / 8; j++) {
					len += ipsec_snprintf(buffer+len, length-len, "%02x",
						       (__u32)((__u8*)(sa_p->ips_iv))[j]);
				}
			}

			if(sa_p->ips_encalg || sa_p->ips_authalg) {
				if(sa_p->ips_replaywin) {
					len += ipsec_snprintf(buffer+len, length-len, " ooowin=%d",
						       sa_p->ips_replaywin);
				}
				if(sa_p->ips_errs.ips_replaywin_errs) {
					len += ipsec_snprintf(buffer+len, length-len, " ooo_errs=%d",
						       sa_p->ips_errs.ips_replaywin_errs);
				}
				if(sa_p->ips_replaywin_lastseq) {
                                       len += ipsec_snprintf(buffer+len, length-len, " seq=%d",
						      sa_p->ips_replaywin_lastseq);
				}
				if(sa_p->ips_replaywin_bitmap) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
					len += ipsec_snprintf(buffer+len, length-len, " bit=0x%Lx",
						       sa_p->ips_replaywin_bitmap);
#else
					len += ipsec_snprintf(buffer+len, length-len, " bit=0x%x%08x",
						       (__u32)(sa_p->ips_replaywin_bitmap >> 32),
						       (__u32)sa_p->ips_replaywin_bitmap);
#endif
				}
				if(sa_p->ips_replaywin_maxdiff) {
					len += ipsec_snprintf(buffer+len, length-len, " max_seq_diff=%d",
						       sa_p->ips_replaywin_maxdiff);
				}
			}
			if(sa_p->ips_flags & ~EMT_INBOUND) {
				len += ipsec_snprintf(buffer+len, length-len, " flags=0x%x",
					       sa_p->ips_flags & ~EMT_INBOUND);
				len += ipsec_snprintf(buffer+len, length-len, "<");
				/* flag printing goes here */
				len += ipsec_snprintf(buffer+len, length-len, ">");
			}
			if(sa_p->ips_auth_bits) {
				len += ipsec_snprintf(buffer+len, length-len, " alen=%d",
					       sa_p->ips_auth_bits);
			}
			if(sa_p->ips_key_bits_a) {
				len += ipsec_snprintf(buffer+len, length-len, " aklen=%d",
					       sa_p->ips_key_bits_a);
			}
			if(sa_p->ips_errs.ips_auth_errs) {
				len += ipsec_snprintf(buffer+len, length-len, " auth_errs=%d",
					       sa_p->ips_errs.ips_auth_errs);
			}
			if(sa_p->ips_key_bits_e) {
				len += ipsec_snprintf(buffer+len, length-len, " eklen=%d",
					       sa_p->ips_key_bits_e);
			}
			if(sa_p->ips_errs.ips_encsize_errs) {
				len += ipsec_snprintf(buffer+len, length-len, " encr_size_errs=%d",
					       sa_p->ips_errs.ips_encsize_errs);
			}
			if(sa_p->ips_errs.ips_encpad_errs) {
				len += ipsec_snprintf(buffer+len, length-len, " encr_pad_errs=%d",
					       sa_p->ips_errs.ips_encpad_errs);
			}
			
			len += ipsec_snprintf(buffer+len, length-len, " life(c,s,h)=");

			len += ipsec_lifetime_format(buffer + len,
						     length - len,
						     "alloc", 
						     ipsec_life_countbased,
						     &sa_p->ips_life.ipl_allocations);

			len += ipsec_lifetime_format(buffer + len,
						     length - len,
						     "bytes",
						     ipsec_life_countbased,
						     &sa_p->ips_life.ipl_bytes);

			len += ipsec_lifetime_format(buffer + len,
						     length - len,
						     "addtime",
						     ipsec_life_timebased,
						     &sa_p->ips_life.ipl_addtime);

			len += ipsec_lifetime_format(buffer + len,
						     length - len,
						     "usetime",
						     ipsec_life_timebased,
						     &sa_p->ips_life.ipl_usetime);
			
			len += ipsec_lifetime_format(buffer + len,
						     length - len,
						     "packets",
						     ipsec_life_countbased,
						     &sa_p->ips_life.ipl_packets);
			
			if(sa_p->ips_life.ipl_usetime.ipl_last) { /* XXX-MCR should be last? */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
				len += ipsec_snprintf(buffer+len, length-len, " idle=%Ld",
					       jiffies / HZ - sa_p->ips_life.ipl_usetime.ipl_last);
#else
				len += ipsec_snprintf(buffer+len, length-len, " idle=%lu",
					       jiffies / HZ - (unsigned long)sa_p->ips_life.ipl_usetime.ipl_last);
#endif
			}

#ifdef CONFIG_KLIPS_IPCOMP
			if(sa_p->ips_said.proto == IPPROTO_COMP &&
			   (sa_p->ips_comp_ratio_dbytes ||
			    sa_p->ips_comp_ratio_cbytes)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
				len += ipsec_snprintf(buffer+len, length-len, " ratio=%Ld:%Ld",
					       sa_p->ips_comp_ratio_dbytes,
					       sa_p->ips_comp_ratio_cbytes);
#else
				len += ipsec_snprintf(buffer+len, length-len, " ratio=%lu:%lu",
					       (unsigned long)sa_p->ips_comp_ratio_dbytes,
					       (unsigned long)sa_p->ips_comp_ratio_cbytes);
#endif
			}
#endif /* CONFIG_KLIPS_IPCOMP */

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
			{
				char *natttype_name;

				switch(sa_p->ips_natt_type)
				{
				case 0:
					natttype_name="none";
					break;
				case ESPINUDP_WITH_NON_IKE:
					natttype_name="nonike";
					break;
				case ESPINUDP_WITH_NON_ESP:
					natttype_name="nonesp";
					break;
				default:
					natttype_name = "unknown";
					break;
				}

				len += ipsec_snprintf(buffer + len, length-len, " natencap=%s",
					       natttype_name);
				
				len += ipsec_snprintf(buffer + len, length-len, " natsport=%d",
					       sa_p->ips_natt_sport);
				
				len += ipsec_snprintf(buffer + len,length-len, " natdport=%d",
					       sa_p->ips_natt_dport);
			}
#else
			len += ipsec_snprintf(buffer + len, length-len, " natencap=na");
#endif /* CONFIG_IPSEC_NAT_TRAVERSAL */
				
			len += ipsec_snprintf(buffer + len,length-len, " refcount=%d",
				       atomic_read(&sa_p->ips_refcount));

			len += ipsec_snprintf(buffer+len, length-len, " ref=%d",
				       sa_p->ips_ref);
#ifdef CONFIG_KLIPS_DEBUG
			if(debug_xform) {
			len += ipsec_snprintf(buffer+len, length-len, " reftable=%lu refentry=%lu",
				       (unsigned long)IPsecSAref2table(sa_p->ips_ref),
				       (unsigned long)IPsecSAref2entry(sa_p->ips_ref));
			}
#endif /* CONFIG_KLIPS_DEBUG */

			len += ipsec_snprintf(buffer+len, length-len, "\n");

                        atomic_dec(&sa_p->ips_refcount);   
                       
                        if (len >= max_content) {
                               /* we've done all that can fit -- stop loops */
                               len = max_content;      /* truncate crap */
                                goto done_spi_i;
                        } else {
                               const off_t pos = begin + len;  /* file position of end of what we've generated */

                               if (pos <= offset) {
                                       /* all is before first interesting character:
                                        * discard, but note where we are.
                                        */
                                       len = 0;
                                       begin = pos;
                               }
                        }
                }
        }

done_spi_i:	
	spin_unlock_bh(&tdb_lock);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	return len - (offset - begin);
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_spigrp_get_info(char *buffer,
		      char **start,
		      off_t offset,
		      int length     IPSEC_PROC_LAST_ARG)
{
	/* Limit of useful snprintf output */
	const int max_content = length > 0? length-1 : 0; 

	int len = 0;
	off_t begin = 0;
	int i;
	struct ipsec_sa *sa_p, *sa_p2;
	char sa[SATOT_BUF];
	size_t sa_len;

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_spigrp_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);

	spin_lock_bh(&tdb_lock);
	
	for (i = 0; i < SADB_HASHMOD; i++) {
		for (sa_p = ipsec_sadb_hash[i];
		     sa_p != NULL;
		     sa_p = sa_p->ips_hnext)
		{
			atomic_inc(&sa_p->ips_refcount);
			if(sa_p->ips_inext == NULL) {
				sa_p2 = sa_p;
				while(sa_p2 != NULL) {
					atomic_inc(&sa_p2->ips_refcount);
					sa_len = satot(&sa_p2->ips_said,
						       'x', sa, sizeof(sa));
					
					len += ipsec_snprintf(buffer+len, length-len, "%s ",
						       sa_len ? sa : " (error)");
					atomic_dec(&sa_p2->ips_refcount);
					sa_p2 = sa_p2->ips_onext;
				}
				len += ipsec_snprintf(buffer+len, length-len, "\n");
                       }

                       atomic_dec(&sa_p->ips_refcount);
                                        
                       if (len >= max_content) {
                               /* we've done all that can fit -- stop loops */
                               len = max_content;      /* truncate crap */
                               goto done_spigrp_i;
                       } else {
                               const off_t pos = begin + len;

                               if (pos <= offset) {
                                       /* all is before first interesting character:
                                        * discard, but note where we are.
                                        */
                                        len = 0;
                                        begin = pos;
                               }
                       }
		}
	}

done_spigrp_i:	
	spin_unlock_bh(&tdb_lock);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	return len - (offset - begin);
}


IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_tncfg_get_info(char *buffer,
		     char **start,
		     off_t offset,
		     int length     IPSEC_PROC_LAST_ARG)
{
	/* limit of useful snprintf output */ 
	const int max_content = length > 0? length-1 : 0;
	int len = 0;
	off_t begin = 0;
	int i;
	char name[9];
	struct net_device *dev, *privdev;
	struct ipsecpriv *priv;

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_tncfg_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);

	for(i = 0; i < IPSEC_NUM_IF; i++) {
		ipsec_snprintf(name, (ssize_t) sizeof(name), IPSEC_DEV_FORMAT, i);
		dev = __ipsec_dev_get(name);
		if(dev) {
			priv = (struct ipsecpriv *)(dev->priv);
			len += ipsec_snprintf(buffer+len, length-len, "%s",
				       dev->name);
			if(priv) {
				privdev = (struct net_device *)(priv->dev);
				len += ipsec_snprintf(buffer+len, length-len, " -> %s",
					       privdev ? privdev->name : "NULL");
				len += ipsec_snprintf(buffer+len, length-len, " mtu=%d(%d) -> %d",
					       dev->mtu,
					       priv->mtu,
					       privdev ? privdev->mtu : 0);
			} else {
				KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
					    "klips_debug:ipsec_tncfg_get_info: device '%s' has no private data space!\n",
					    dev->name);
			}
			len += ipsec_snprintf(buffer+len, length-len, "\n");

                        if (len >= max_content) {
                                /* we've done all that can fit -- stop loop */
                                len = max_content;      /* truncate crap */
                                 break;
                        } else {
                                const off_t pos = begin + len;
                                if (pos <= offset) {
                                        len = 0;
                                        begin = pos;
                                }
			}
		}
	}
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);			/* Start slop */
	if (len > length)
		len = length;
	return len;
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_version_get_info(char *buffer,
		       char **start,
		       off_t offset,
		       int length  IPSEC_PROC_LAST_ARG)
{
	int len = 0;
	off_t begin = 0;

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_version_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);

	len += ipsec_snprintf(buffer + len,length-len, "Openswan version: %s\n",
		       ipsec_version_code());
#if 0
	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_version_get_info: "
		    "ipsec_init version: %s\n",
		    ipsec_init_c_version);
	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_version_get_info: "
		    "ipsec_tunnel version: %s\n",
		    ipsec_tunnel_c_version);
	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_version_get_info: "
		    "ipsec_netlink version: %s\n",
		    ipsec_netlink_c_version);
	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_version_get_info: "
		    "radij_c_version: %s\n",
		    radij_c_version);
#endif


	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);			/* Start slop */
	if (len > length)
		len = length;
	return len;
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_natt_get_info(char *buffer,
                    char **start,
                    off_t offset,
                    int length  IPSEC_PROC_LAST_ARG)
{
        int len = 0;
        off_t begin = 0;

        len += ipsec_snprintf(buffer + len,
                              length-len, "%d\n",
#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
                              1
#else
                              0
#endif
                );

        *start = buffer + (offset - begin);     /* Start of wanted data */
        len -= (offset - begin);                        /* Start slop */
        if (len > length)
                len = length;
        return len;
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_birth_info(char *page,
		 char **start,
		 off_t offset,
		 int count,
		 int *eof,
		 void *data)
{
	struct ipsec_birth_reply *ibr = (struct ipsec_birth_reply *)data;
	int len;

	if(offset >= ibr->packet_template_len) {
		if(eof) {
			*eof=1;
		}
		return 0;
	}

	len = ibr->packet_template_len;
	len -= offset;
	if (len > count)
		len = count;

	memcpy(page + offset, ibr->packet_template+offset, len);

	return len;
}

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_birth_set(struct file *file, const char *buffer,
		unsigned long count, void *data)
{
	struct ipsec_birth_reply *ibr = (struct ipsec_birth_reply *)data;
	int len;

	KLIPS_INC_USE;
        if(count > IPSEC_BIRTH_TEMPLATE_MAXLEN) {
                len = IPSEC_BIRTH_TEMPLATE_MAXLEN;
	} else {
                len = count;
	}

        if(copy_from_user(ibr->packet_template, buffer, len)) {
                KLIPS_DEC_USE;
                return -EFAULT;
        }
	ibr->packet_template_len = len;

        KLIPS_DEC_USE;

        return len;
}


#ifdef CONFIG_KLIPS_DEBUG
IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_klipsdebug_get_info(char *buffer,
			  char **start,
			  off_t offset,
			  int length      IPSEC_PROC_LAST_ARG)
{
	int len = 0;
	off_t begin = 0;

	KLIPS_PRINT(debug_tunnel & DB_TN_PROCFS,
		    "klips_debug:ipsec_klipsdebug_get_info: "
		    "buffer=0p%p, *start=0p%p, offset=%d, length=%d\n",
		    buffer,
		    *start,
		    (int)offset,
		    length);

	len += ipsec_snprintf(buffer+len, length-len, "debug_tunnel=%08x.\n", debug_tunnel);
	len += ipsec_snprintf(buffer+len, length-len, "debug_xform=%08x.\n", debug_xform);
	len += ipsec_snprintf(buffer+len, length-len, "debug_eroute=%08x.\n", debug_eroute);
	len += ipsec_snprintf(buffer+len, length-len, "debug_spi=%08x.\n", debug_spi);
	len += ipsec_snprintf(buffer+len, length-len, "debug_radij=%08x.\n", debug_radij);
	len += ipsec_snprintf(buffer+len, length-len, "debug_esp=%08x.\n", debug_esp);
	len += ipsec_snprintf(buffer+len, length-len, "debug_ah=%08x.\n", debug_ah);
	len += ipsec_snprintf(buffer+len, length-len, "debug_rcv=%08x.\n", debug_rcv);
	len += ipsec_snprintf(buffer+len, length-len, "debug_pfkey=%08x.\n", debug_pfkey);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);			/* Start slop */
	if (len > length)
		len = length;
	return len;
}
#endif /* CONFIG_KLIPS_DEBUG */

IPSEC_PROCFS_DEBUG_NO_STATIC
int
ipsec_stats_get_int_info(char *buffer,
			 char **start,
			 off_t offset,
			 int   length,
			 int   *eof,
			 void  *data)
{

	const int max_content = length > 0? length-1 : 0;
	int len = 0;
	int *thing;

	thing = (int *)data;
	
	len = ipsec_snprintf(buffer+len, length-len, "%08x\n", *thing);

	if (len >= max_content)
               len = max_content;      /* truncate crap */

        *start = buffer + offset;       /* Start of wanted data */
        return len > offset? len - offset : 0;

}

#ifndef PROC_FS_2325
struct proc_dir_entry ipsec_eroute =
{
	0,
	12, "ipsec_eroute",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_eroute_get_info,
	NULL, NULL, NULL, NULL, NULL
};

struct proc_dir_entry ipsec_spi =
{
	0,
	9, "ipsec_spi",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_spi_get_info,
	NULL, NULL, NULL, NULL, NULL
};

struct proc_dir_entry ipsec_spigrp =
{
	0,
	12, "ipsec_spigrp",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_spigrp_get_info,
	NULL, NULL, NULL, NULL, NULL
};

struct proc_dir_entry ipsec_tncfg =
{
	0,
	11, "ipsec_tncfg",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_tncfg_get_info,
	NULL, NULL, NULL, NULL, NULL
};

struct proc_dir_entry ipsec_version =
{
	0,
	13, "ipsec_version",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_version_get_info,
	NULL, NULL, NULL, NULL, NULL
};

#ifdef CONFIG_KLIPS_DEBUG
struct proc_dir_entry ipsec_klipsdebug =
{
	0,
	16, "ipsec_klipsdebug",
	S_IFREG | S_IRUGO, 1, 0, 0, 0,
	&proc_net_inode_operations,
	ipsec_klipsdebug_get_info,
	NULL, NULL, NULL, NULL, NULL
};
#endif /* CONFIG_KLIPS_DEBUG */
#endif /* !PROC_FS_2325 */
#endif /* CONFIG_PROC_FS */

#if defined(PROC_FS_2325) 
struct ipsec_proc_list {
	char                   *name;
	struct proc_dir_entry **parent;
	struct proc_dir_entry **dir;
	read_proc_t            *readthing;
	write_proc_t           *writething;
	void                   *data;
};
static struct ipsec_proc_list proc_items[]={
#ifdef CONFIG_KLIPS_DEBUG
	{"klipsdebug", &proc_net_ipsec_dir, NULL,             ipsec_klipsdebug_get_info, NULL, NULL},
#endif
	{"eroute",     &proc_net_ipsec_dir, &proc_eroute_dir, NULL, NULL, NULL},
	{"all",        &proc_eroute_dir,    NULL,             ipsec_eroute_get_info,     NULL, NULL},
	{"spi",        &proc_net_ipsec_dir, &proc_spi_dir,    NULL, NULL, NULL},
	{"all",        &proc_spi_dir,       NULL,             ipsec_spi_get_info,        NULL, NULL},
	{"spigrp",     &proc_net_ipsec_dir, &proc_spigrp_dir, NULL, NULL, NULL},
	{"all",        &proc_spigrp_dir,    NULL,             ipsec_spigrp_get_info,     NULL, NULL},
	{"birth",      &proc_net_ipsec_dir, &proc_birth_dir,  NULL,      NULL, NULL},
	{"ipv4",       &proc_birth_dir,     NULL,             ipsec_birth_info, ipsec_birth_set, (void *)&ipsec_ipv4_birth_packet},
	{"ipv6",       &proc_birth_dir,     NULL,             ipsec_birth_info, ipsec_birth_set, (void *)&ipsec_ipv6_birth_packet},
	{"tncfg",      &proc_net_ipsec_dir, NULL,             ipsec_tncfg_get_info,      NULL, NULL},
#ifdef CONFIG_KLIPS_ALG
	{"xforms",     &proc_net_ipsec_dir, NULL,             ipsec_xform_get_info,      NULL, NULL},
#endif /* CONFIG_KLIPS_ALG */
	{"stats",      &proc_net_ipsec_dir, &proc_stats_dir,  NULL,      NULL, NULL},
	{"trap_count", &proc_stats_dir,     NULL,             ipsec_stats_get_int_info, NULL, &ipsec_xmit_trap_count},
	{"trap_sendcount", &proc_stats_dir, NULL,             ipsec_stats_get_int_info, NULL, &ipsec_xmit_trap_sendcount},
	{"version",    &proc_net_ipsec_dir, NULL,             ipsec_version_get_info,    NULL, NULL},
	{NULL,         NULL,                NULL,             NULL,      NULL, NULL}
};
#endif
		
int
ipsec_proc_init()
{
	int error = 0;
#ifdef IPSEC_PROC_SUBDIRS
	struct proc_dir_entry *item;
#endif

	/*
	 * just complain because pluto won't run without /proc!
	 */
#ifndef CONFIG_PROC_FS 
#error You must have PROC_FS built in to use KLIPS
#endif

        /* for 2.0 kernels */
#if !defined(PROC_FS_2325) && !defined(PROC_FS_21)
	error |= proc_register_dynamic(&proc_net, &ipsec_eroute);
	error |= proc_register_dynamic(&proc_net, &ipsec_spi);
	error |= proc_register_dynamic(&proc_net, &ipsec_spigrp);
	error |= proc_register_dynamic(&proc_net, &ipsec_tncfg);
	error |= proc_register_dynamic(&proc_net, &ipsec_version);
#ifdef CONFIG_KLIPS_DEBUG
	error |= proc_register_dynamic(&proc_net, &ipsec_klipsdebug);
#endif /* CONFIG_KLIPS_DEBUG */
#endif

	/* for 2.2 kernels */
#if !defined(PROC_FS_2325) && defined(PROC_FS_21)
	error |= proc_register(proc_net, &ipsec_eroute);
	error |= proc_register(proc_net, &ipsec_spi);
	error |= proc_register(proc_net, &ipsec_spigrp);
	error |= proc_register(proc_net, &ipsec_tncfg);
	error |= proc_register(proc_net, &ipsec_version);
#ifdef CONFIG_KLIPS_DEBUG
	error |= proc_register(proc_net, &ipsec_klipsdebug);
#endif /* CONFIG_KLIPS_DEBUG */
#endif

	/* for 2.4 kernels */
#if defined(PROC_FS_2325)
	/* create /proc/net/ipsec */

	/* zero these out before we initialize /proc/net/ipsec/birth/stuff */
	memset(&ipsec_ipv4_birth_packet, 0, sizeof(struct ipsec_birth_reply));
	memset(&ipsec_ipv6_birth_packet, 0, sizeof(struct ipsec_birth_reply));

	proc_net_ipsec_dir = proc_mkdir("ipsec", proc_net);
	if(proc_net_ipsec_dir == NULL) {
		/* no point in continuing */
		return 1;
	} 	

	{
		struct ipsec_proc_list *it;

		it=proc_items;
		while(it->name!=NULL) {
			if(it->dir) {
				/* make a dir instead */
				item = proc_mkdir(it->name, *it->parent);
				*it->dir = item;
			} else {
				item = create_proc_entry(it->name, 0400, *it->parent);
			}
			if(item) {
				item->read_proc  = it->readthing;
				item->write_proc = it->writething;
				item->data       = it->data;
#ifdef MODULE
				item->owner = THIS_MODULE;
#endif
			} else {
				error |= 1;
			}
			it++;
		}
	}
	
	/* now create some symlinks to provide compatibility */
	proc_symlink("ipsec_eroute", proc_net, "ipsec/eroute/all");
	proc_symlink("ipsec_spi",    proc_net, "ipsec/spi/all");
	proc_symlink("ipsec_spigrp", proc_net, "ipsec/spigrp/all");
	proc_symlink("ipsec_tncfg",  proc_net, "ipsec/tncfg");
	proc_symlink("ipsec_version",proc_net, "ipsec/version");
	proc_symlink("ipsec_klipsdebug",proc_net,"ipsec/klipsdebug");

#endif /* !PROC_FS_2325 */

	return error;
}

void
ipsec_proc_cleanup()
{

	/* for 2.0 and 2.2 kernels */
#if !defined(PROC_FS_2325) 

#ifdef CONFIG_KLIPS_DEBUG
	if (proc_net_unregister(ipsec_klipsdebug.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_klipsdebug\n");
#endif /* CONFIG_KLIPS_DEBUG */

	if (proc_net_unregister(ipsec_version.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_version\n");
	if (proc_net_unregister(ipsec_eroute.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_eroute\n");
	if (proc_net_unregister(ipsec_spi.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_spi\n");
	if (proc_net_unregister(ipsec_spigrp.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_spigrp\n");
	if (proc_net_unregister(ipsec_tncfg.low_ino) != 0)
		printk("klips_debug:ipsec_cleanup: "
		       "cannot unregister /proc/net/ipsec_tncfg\n");
#endif

	/* for 2.4 kernels */
#if defined(PROC_FS_2325)
	{
		struct ipsec_proc_list *it;

		/* find end of list */
		it=proc_items;
		while(it->name!=NULL) {
			it++;
		}
		it--;

		do {
			remove_proc_entry(it->name, *it->parent);
			it--;
		} while(it >= proc_items);
	}


#ifdef CONFIG_KLIPS_DEBUG
	remove_proc_entry("ipsec_klipsdebug", proc_net);
#endif /* CONFIG_KLIPS_DEBUG */
	remove_proc_entry("ipsec_eroute",     proc_net);
	remove_proc_entry("ipsec_spi",        proc_net);
	remove_proc_entry("ipsec_spigrp",     proc_net);
	remove_proc_entry("ipsec_tncfg",      proc_net);
	remove_proc_entry("ipsec_version",    proc_net);
	remove_proc_entry("ipsec",            proc_net);
#endif /* 2.4 kernel */
}

/*
 * $Log: ipsec_proc.c,v $
 * Revision 1.39.2.2  2006/02/13 18:48:12  paul
 * Fix by  Ankit Desai <ankit@elitecore.com> for module unloading.
 *
 * Revision 1.39.2.1  2005/09/07 00:45:59  paul
 * pull up of mcr's nat-t klips detection patch from head
 *
 * Revision 1.39  2005/05/20 03:19:18  mcr
 * 	modifications for use on 2.4.30 kernel, with backported
 * 	printk_ratelimit(). all warnings removed.
 *
 * Revision 1.38  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.37  2005/04/13 22:49:49  mcr
 * 	moved KLIPS specific snprintf() wrapper to seperate file.
 *
 * Revision 1.36  2005/04/06 17:44:36  mcr
 * 	when NAT-T is compiled out, show encap as "NA"
 *
 * Revision 1.35  2005/01/26 00:50:35  mcr
 * 	adjustment of confusion of CONFIG_IPSEC_NAT vs CONFIG_KLIPS_NAT,
 * 	and make sure that NAT_TRAVERSAL is set as well to match
 * 	userspace compiles of code.
 *
 * Revision 1.34  2004/12/03 21:25:57  mcr
 * 	compile time fixes for running on 2.6.
 * 	still experimental.
 *
 * Revision 1.33  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
 *
 * Revision 1.32  2004/08/03 18:19:08  mcr
 * 	in 2.6, use "net_device" instead of #define device->net_device.
 * 	this probably breaks 2.0 compiles.
 *
 * Revision 1.31  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.30  2004/04/25 21:23:11  ken
 * Pull in dhr's changes from FreeS/WAN 2.06
 *
 * Revision 1.29  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.28  2004/03/28 20:29:58  paul
 *      <hugh_> ssize_t, not ssized_t
 *
 * Revision 1.27  2004/03/28 20:27:20  paul
 * Included tested and confirmed fixes mcr made and dhr verified for
 * snprint statements. Changed one other snprintf to use ipsec_snprintf
 * so it wouldnt break compatibility with 2.0/2.2 kernels. Verified with
 * dhr. (thanks dhr!)
 *
 * Revision 1.26  2004/02/09 22:07:06  mcr
 * 	added information about nat-traversal setting to spi-output.
 *
 * Revision 1.25.4.1  2004/04/05 04:30:46  mcr
 * 	patches for alg-branch to compile/work with 2.x openswan
 *
 * Revision 1.25  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.24.4.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.24  2003/06/20 01:42:21  mcr
 * 	added counters to measure how many ACQUIREs we send to pluto,
 * 	and how many are successfully sent.
 *
 * Revision 1.23  2003/04/03 17:38:09  rgb
 * Centralised ipsec_kfree_skb and ipsec_dev_{get,put}.
 *
 * Revision 1.22  2002/09/20 15:40:57  rgb
 * Renamed saref macros for consistency and brevity.
 *
 * Revision 1.21  2002/09/20 05:01:35  rgb
 * Print ref and  reftable, refentry seperately.
 *
 * Revision 1.20  2002/09/19 02:35:39  mcr
 * 	do not define structures needed by /proc/net/ipsec/ if we
 * 	aren't going create that directory.
 *
 * Revision 1.19  2002/09/10 01:43:25  mcr
 * 	fixed problem in /-* comment.
 *
 * Revision 1.18  2002/09/03 16:22:11  mcr
 * 	fixed initialization of birth/stuff values - some simple
 * 	screw ups in the code.
 * 	removed debugging that was left in by mistake.
 *
 * Revision 1.17  2002/09/02 17:54:53  mcr
 * 	changed how the table driven /proc entries are created so that
 * 	making subdirs is now explicit rather than implicit.
 *
 * Revision 1.16  2002/08/30 01:23:37  mcr
 * 	reorganized /proc creating code to clear up ifdefs,
 * 	make the 2.4 code table driven, and put things into
 * 	/proc/net/ipsec subdir. Symlinks are left for compatibility.
 *
 * Revision 1.15  2002/08/13 19:01:25  mcr
 * 	patches from kenb to permit compilation of FreeSWAN on ia64.
 * 	des library patched to use proper DES_LONG type for ia64.
 *
 * Revision 1.14  2002/07/26 08:48:31  rgb
 * Added SA ref table code.
 *
 * Revision 1.13  2002/07/24 18:44:54  rgb
 * Type fiddling to tame ia64 compiler.
 *
 * Revision 1.12  2002/05/27 18:56:07  rgb
 * Convert to dynamic ipsec device allocation.
 *
 * Revision 1.11  2002/05/23 07:14:50  rgb
 * Added refcount code.
 * Cleaned up %p variants to 0p%p for test suite cleanup.
 * Convert "usecount" to "refcount" to remove ambiguity.
 *
 * Revision 1.10  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.9  2002/04/24 07:36:28  mcr
 * Moved from ./klips/net/ipsec/ipsec_proc.c,v
 *
 * Revision 1.8  2002/01/29 17:17:55  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.7  2002/01/29 04:00:52  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.6  2002/01/29 02:13:17  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.5  2002/01/12 02:54:30  mcr
 * 	beginnings of /proc/net/ipsec dir.
 *
 * Revision 1.4  2001/12/11 02:21:05  rgb
 * Don't include module version here, fixing 2.2 compile bug.
 *
 * Revision 1.3  2001/12/05 07:19:44  rgb
 * Fixed extraneous #include "version.c" bug causing modular KLIPS failure.
 *
 * Revision 1.2  2001/11/26 09:16:14  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.74  2001/11/22 05:44:11  henry
 * new version stuff
 *
 * Revision 1.1.2.1  2001/09/25 02:19:40  mcr
 * 	/proc manipulation code moved to new ipsec_proc.c
 *
 *
 * Local variables:
 * c-file-style: "linux"
 * End:
 *
 */
