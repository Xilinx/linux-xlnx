/*
 * Common routines for IPsec SA maintenance routines.
 *
 * Copyright (C) 1996, 1997  John Ioannidis.
 * Copyright (C) 1998, 1999, 2000, 2001, 2002  Richard Guy Briggs.
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
 * RCSID $Id: ipsec_sa.c,v 1.30.2.1 2006/04/20 16:33:07 mcr Exp $
 *
 * This is the file formerly known as "ipsec_xform.h"
 *
 */

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
#include <linux/vmalloc.h> /* vmalloc() */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/skbuff.h>
#include <openswan.h>
#ifdef SPINLOCK
#ifdef SPINLOCK_23
#include <linux/spinlock.h> /* *lock* */
#else /* SPINLOCK_23 */
#include <asm/spinlock.h> /* *lock* */
#endif /* SPINLOCK_23 */
#endif /* SPINLOCK */

#include <net/ip.h>

#include "openswan/radij.h"

#include "openswan/ipsec_stats.h"
#include "openswan/ipsec_life.h"
#include "openswan/ipsec_sa.h"
#include "openswan/ipsec_xform.h"

#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_ipe4.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#include "openswan/ipsec_proto.h"
#include "openswan/ipsec_alg.h"

#include "ipsec_ocf.h"


#ifdef CONFIG_KLIPS_DEBUG
int debug_xform = 0;
#endif /* CONFIG_KLIPS_DEBUG */

#define SENDERR(_x) do { error = -(_x); goto errlab; } while (0)

struct ipsec_sa *ipsec_sadb_hash[SADB_HASHMOD];
#ifdef SPINLOCK
spinlock_t tdb_lock = SPIN_LOCK_UNLOCKED;
#else /* SPINLOCK */
spinlock_t tdb_lock;
#endif /* SPINLOCK */

struct ipsec_sadb ipsec_sadb;

#if IPSEC_SA_REF_CODE

/* the sub table must be narrower (or equal) in bits than the variable type
   in the main table to count the number of unused entries in it. */
typedef struct {
	int testSizeOf_refSubTable :
		((sizeof(IPsecRefTableUnusedCount) * 8) < IPSEC_SA_REF_SUBTABLE_IDX_WIDTH ? -1 : 1);
} dummy;


/* The field where the saref will be hosted in the skb must be wide enough to
   accomodate the information it needs to store. */
typedef struct {
	int testSizeOf_refField : 
		(IPSEC_SA_REF_HOST_FIELD_WIDTH < IPSEC_SA_REF_TABLE_IDX_WIDTH ? -1 : 1 );
} dummy2;


#define IPS_HASH(said) (((said)->spi + (said)->dst.u.v4.sin_addr.s_addr + (said)->proto) % SADB_HASHMOD)


void
ipsec_SAtest(void)
{
	IPsecSAref_t SAref = 258;
	struct ipsec_sa ips;
	ips.ips_ref = 772;

	printk("klips_debug:ipsec_SAtest: "
	       "IPSEC_SA_REF_SUBTABLE_IDX_WIDTH=%u\n"
	       "IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES=%u\n"
	       "IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES=%u\n"
	       "IPSEC_SA_REF_HOST_FIELD_WIDTH=%lu\n"
	       "IPSEC_SA_REF_TABLE_MASK=%x\n"
	       "IPSEC_SA_REF_ENTRY_MASK=%x\n"
	       "IPsecSAref2table(%d)=%u\n"
	       "IPsecSAref2entry(%d)=%u\n"
	       "IPsecSAref2NFmark(%d)=%u\n"
	       "IPsecSAref2SA(%d)=%p\n"
	       "IPsecSA2SAref(%p)=%d\n"
	       ,
	       IPSEC_SA_REF_SUBTABLE_IDX_WIDTH,
	       IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES,
	       IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES,
	       (unsigned long) IPSEC_SA_REF_HOST_FIELD_WIDTH,
	       IPSEC_SA_REF_TABLE_MASK,
	       IPSEC_SA_REF_ENTRY_MASK,
	       SAref, IPsecSAref2table(SAref),
	       SAref, IPsecSAref2entry(SAref),
	       SAref, IPsecSAref2NFmark(SAref),
	       SAref, IPsecSAref2SA(SAref),
	       (&ips), IPsecSA2SAref((&ips))
		);
	return;
}

int
ipsec_SAref_recycle(void)
{
	int table;
	int entry;
	int error = 0;

	ipsec_sadb.refFreeListHead = -1;
	ipsec_sadb.refFreeListTail = -1;

	if(ipsec_sadb.refFreeListCont == IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES * IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SAref_recycle: "
			    "end of table reached, continuing at start..\n");
		ipsec_sadb.refFreeListCont = 0;
	}

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SAref_recycle: "
		    "recycling, continuing from SAref=%d (0p%p), table=%d, entry=%d.\n",
		    ipsec_sadb.refFreeListCont,
		    (ipsec_sadb.refTable[IPsecSAref2table(ipsec_sadb.refFreeListCont)] != NULL) ? IPsecSAref2SA(ipsec_sadb.refFreeListCont) : NULL,
		    IPsecSAref2table(ipsec_sadb.refFreeListCont),
		    IPsecSAref2entry(ipsec_sadb.refFreeListCont));

	for(table = IPsecSAref2table(ipsec_sadb.refFreeListCont);
	    table < IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES;
	    table++) {
		if(ipsec_sadb.refTable[table] == NULL) {
			error = ipsec_SArefSubTable_alloc(table);
			if(error) {
				return error;
			}
		}
		for(entry = IPsecSAref2entry(ipsec_sadb.refFreeListCont);
		    entry < IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES;
		    entry++) {
			if(ipsec_sadb.refTable[table]->entry[entry] == NULL) {
				ipsec_sadb.refFreeList[++ipsec_sadb.refFreeListTail] = IPsecSArefBuild(table, entry);
				if(ipsec_sadb.refFreeListTail == (IPSEC_SA_REF_FREELIST_NUM_ENTRIES - 1)) {
					ipsec_sadb.refFreeListHead = 0;
					ipsec_sadb.refFreeListCont = ipsec_sadb.refFreeList[ipsec_sadb.refFreeListTail] + 1;
					KLIPS_PRINT(debug_xform,
						    "klips_debug:ipsec_SAref_recycle: "
						    "SArefFreeList refilled.\n");
					return 0;
				}
			}
		}
	}

	if(ipsec_sadb.refFreeListTail == -1) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SAref_recycle: "
			    "out of room in the SArefTable.\n");

		return(-ENOSPC);
	}

	ipsec_sadb.refFreeListHead = 0;
	ipsec_sadb.refFreeListCont = ipsec_sadb.refFreeList[ipsec_sadb.refFreeListTail] + 1;
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SAref_recycle: "
		    "SArefFreeList partly refilled to %d of %d.\n",
		    ipsec_sadb.refFreeListTail,
		    IPSEC_SA_REF_FREELIST_NUM_ENTRIES);
	return 0;
}

int
ipsec_SArefSubTable_alloc(unsigned table)
{
	unsigned entry;
	struct IPsecSArefSubTable* SArefsub;

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SArefSubTable_alloc: "
		    "allocating %lu bytes for table %u of %u.\n",
		    (unsigned long) (IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES * sizeof(struct ipsec_sa *)),
		    table,
		    IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES);

	/* allocate another sub-table */
	SArefsub = vmalloc(IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES * sizeof(struct ipsec_sa *));
	if(SArefsub == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SArefSubTable_alloc: "
			    "error allocating memory for table %u of %u!\n",
			    table,
			    IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES);
		return -ENOMEM;
	}

	/* add this sub-table to the main table */
	ipsec_sadb.refTable[table] = SArefsub;

	/* initialise each element to NULL */
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SArefSubTable_alloc: "
		    "initialising %u elements (2 ^ %u) of table %u.\n",
		    IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES,
		    IPSEC_SA_REF_SUBTABLE_IDX_WIDTH,
		    table);
	for(entry = 0; entry < IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES; entry++) {
		SArefsub->entry[entry] = NULL;
	}

	return 0;
}
#endif /* IPSEC_SA_REF_CODE */

int
ipsec_saref_freelist_init(void)
{
	int i;

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_saref_freelist_init: "
		    "initialising %u elements of FreeList.\n",
		    IPSEC_SA_REF_FREELIST_NUM_ENTRIES);

	for(i = 0; i < IPSEC_SA_REF_FREELIST_NUM_ENTRIES; i++) {
		ipsec_sadb.refFreeList[i] = IPSEC_SAREF_NULL;
	}
	ipsec_sadb.refFreeListHead = -1;
	ipsec_sadb.refFreeListCont = 0;
	ipsec_sadb.refFreeListTail = -1;
       
	return 0;
}

int
ipsec_sadb_init(void)
{
	int error = 0;
	unsigned i;

	for(i = 0; i < SADB_HASHMOD; i++) {
		ipsec_sadb_hash[i] = NULL;
	}
	/* parts above are for the old style SADB hash table */
	

#if IPSEC_SA_REF_CODE
	/* initialise SA reference table */

	/* initialise the main table */
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sadb_init: "
		    "initialising main table of size %u (2 ^ %u).\n",
		    IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES,
		    IPSEC_SA_REF_MAINTABLE_IDX_WIDTH);
	{
		unsigned table;
		for(table = 0; table < IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES; table++) {
			ipsec_sadb.refTable[table] = NULL;
		}
	}

	/* allocate the first sub-table */
	error = ipsec_SArefSubTable_alloc(0);
	if(error) {
		return error;
	}

	error = ipsec_saref_freelist_init();
#endif /* IPSEC_SA_REF_CODE */
	return error;
}

#if IPSEC_SA_REF_CODE
IPsecSAref_t
ipsec_SAref_alloc(int*error) /* pass in error var by pointer */
{
	IPsecSAref_t SAref;

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SAref_alloc: "
		    "SAref requested... head=%d, cont=%d, tail=%d, listsize=%d.\n",
		    ipsec_sadb.refFreeListHead,
		    ipsec_sadb.refFreeListCont,
		    ipsec_sadb.refFreeListTail,
		    IPSEC_SA_REF_FREELIST_NUM_ENTRIES);

	if(ipsec_sadb.refFreeListHead == -1) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SAref_alloc: "
			    "FreeList empty, recycling...\n");
		*error = ipsec_SAref_recycle();
		if(*error) {
			return IPSEC_SAREF_NULL;
		}
	}

	SAref = ipsec_sadb.refFreeList[ipsec_sadb.refFreeListHead];
	if(SAref == IPSEC_SAREF_NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SAref_alloc: "
			    "unexpected error, refFreeListHead = %d points to invalid entry.\n",
			    ipsec_sadb.refFreeListHead);
			*error = -ESPIPE;
			return IPSEC_SAREF_NULL;
	}

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_SAref_alloc: "
		    "allocating SAref=%d, table=%u, entry=%u of %u.\n",
		    SAref,
		    IPsecSAref2table(SAref),
		    IPsecSAref2entry(SAref),
		    IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES * IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES);
	
	ipsec_sadb.refFreeList[ipsec_sadb.refFreeListHead] = IPSEC_SAREF_NULL;
	ipsec_sadb.refFreeListHead++;
	if(ipsec_sadb.refFreeListHead > ipsec_sadb.refFreeListTail) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_SAref_alloc: "
			    "last FreeList entry allocated, resetting list head to empty.\n");
		ipsec_sadb.refFreeListHead = -1;
	}

	return SAref;
}
#endif /* IPSEC_SA_REF_CODE */

int
ipsec_sa_print(struct ipsec_sa *ips)
{
        char sa[SATOT_BUF];
	size_t sa_len;

	printk(KERN_INFO "klips_debug:   SA:");
	if(ips == NULL) {
		printk("NULL\n");
		return -ENOENT;
	}
	printk(" ref=%d", ips->ips_ref);
	printk(" refcount=%d", atomic_read(&ips->ips_refcount));
	if(ips->ips_hnext != NULL) {
		printk(" hnext=0p%p", ips->ips_hnext);
	}
	if(ips->ips_inext != NULL) {
		printk(" inext=0p%p", ips->ips_inext);
	}
	if(ips->ips_onext != NULL) {
		printk(" onext=0p%p", ips->ips_onext);
	}
	sa_len = satot(&ips->ips_said, 0, sa, sizeof(sa));
	printk(" said=%s", sa_len ? sa : " (error)");
	if(ips->ips_seq) {
		printk(" seq=%u", ips->ips_seq);
	}
	if(ips->ips_pid) {
		printk(" pid=%u", ips->ips_pid);
	}
	if(ips->ips_authalg) {
		printk(" authalg=%u", ips->ips_authalg);
	}
	if(ips->ips_encalg) {
		printk(" encalg=%u", ips->ips_encalg);
	}
	printk(" XFORM=%s%s%s", IPS_XFORM_NAME(ips));
	if(ips->ips_replaywin) {
		printk(" ooowin=%u", ips->ips_replaywin);
	}
	if(ips->ips_flags) {
		printk(" flags=%u", ips->ips_flags);
	}
	if(ips->ips_addr_s) {
		char buf[SUBNETTOA_BUF];
		addrtoa(((struct sockaddr_in*)(ips->ips_addr_s))->sin_addr,
			0, buf, sizeof(buf));
		printk(" src=%s", buf);
	}
	if(ips->ips_addr_d) {
		char buf[SUBNETTOA_BUF];
		addrtoa(((struct sockaddr_in*)(ips->ips_addr_s))->sin_addr,
			0, buf, sizeof(buf));
		printk(" dst=%s", buf);
	}
	if(ips->ips_addr_p) {
		char buf[SUBNETTOA_BUF];
		addrtoa(((struct sockaddr_in*)(ips->ips_addr_p))->sin_addr,
			0, buf, sizeof(buf));
		printk(" proxy=%s", buf);
	}
	if(ips->ips_key_bits_a) {
		printk(" key_bits_a=%u", ips->ips_key_bits_a);
	}
	if(ips->ips_key_bits_e) {
		printk(" key_bits_e=%u", ips->ips_key_bits_e);
	}

	printk("\n");
	return 0;
}

struct ipsec_sa*
ipsec_sa_alloc(int*error) /* pass in error var by pointer */
{
	struct ipsec_sa* ips;

	if((ips = kmalloc(sizeof(*ips), GFP_ATOMIC) ) == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_alloc: "
			    "memory allocation error\n");
		*error = -ENOMEM;
		return NULL;
	}
	memset((caddr_t)ips, 0, sizeof(*ips));
#if IPSEC_SA_REF_CODE
	ips->ips_ref = ipsec_SAref_alloc(error); /* pass in error return by pointer */
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_alloc: "
		    "allocated %lu bytes for ipsec_sa struct=0p%p ref=%d.\n",
		    (unsigned long) sizeof(*ips),
		    ips,
		    ips->ips_ref);
	if(ips->ips_ref == IPSEC_SAREF_NULL) {
		kfree(ips);
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_alloc: "
			    "SAref allocation error\n");
		return NULL;
	}

	atomic_inc(&ips->ips_refcount);
	IPsecSAref2SA(ips->ips_ref) = ips;
#endif /* IPSEC_SA_REF_CODE */

	*error = 0;
	return(ips);
}

int
ipsec_sa_free(struct ipsec_sa* ips)
{
	return ipsec_sa_wipe(ips);
}

struct ipsec_sa *
ipsec_sa_getbyid(ip_said *said)
{
	int hashval;
	struct ipsec_sa *ips;
        char sa[SATOT_BUF];
	size_t sa_len;

	if(said == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_getbyid: "
			    "null pointer passed in!\n");
		return NULL;
	}

	sa_len = KLIPS_SATOT(debug_xform, said, 0, sa, sizeof(sa));

	hashval = IPS_HASH(said);
	
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_getbyid: "
		    "linked entry in ipsec_sa table for hash=%d of SA:%s requested.\n",
		    hashval,
		    sa_len ? sa : " (error)");

	if((ips = ipsec_sadb_hash[hashval]) == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_getbyid: "
			    "no entries in ipsec_sa table for hash=%d of SA:%s.\n",
			    hashval,
			    sa_len ? sa : " (error)");
		return NULL;
	}

	for (; ips; ips = ips->ips_hnext) {
		if ((ips->ips_said.spi == said->spi) &&
		    (ips->ips_said.dst.u.v4.sin_addr.s_addr == said->dst.u.v4.sin_addr.s_addr) &&
		    (ips->ips_said.proto == said->proto)) {
			atomic_inc(&ips->ips_refcount);
			return ips;
		}
	}
	
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_getbyid: "
		    "no entry in linked list for hash=%d of SA:%s.\n",
		    hashval,
		    sa_len ? sa : " (error)");
	return NULL;
}

int
ipsec_sa_put(struct ipsec_sa *ips)
{
        char sa[SATOT_BUF];
	size_t sa_len;

	if(ips == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_put: "
			    "null pointer passed in!\n");
		return -1;
	}

	sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_put: "
		    "ipsec_sa SA:%s, ref:%d reference count decremented.\n",
		    sa_len ? sa : " (error)",
		    ips->ips_ref);

	atomic_dec(&ips->ips_refcount);

	return 0;
}

/*
  The ipsec_sa table better *NOT* be locked before it is handed in, or SMP locks will happen
*/
int
ipsec_sa_add(struct ipsec_sa *ips)
{
	int error = 0;
	unsigned int hashval;

	if(ips == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_add: "
			    "null pointer passed in!\n");
		return -ENODATA;
	}
	hashval = IPS_HASH(&ips->ips_said);

	atomic_inc(&ips->ips_refcount);
	spin_lock_bh(&tdb_lock);
	
	ips->ips_hnext = ipsec_sadb_hash[hashval];
	ipsec_sadb_hash[hashval] = ips;
	
	spin_unlock_bh(&tdb_lock);

	return error;
}

/*
  The ipsec_sa table better be locked before it is handed in, or races might happen
*/
int
ipsec_sa_del(struct ipsec_sa *ips)
{
	unsigned int hashval;
	struct ipsec_sa *ipstp;
        char sa[SATOT_BUF];
	size_t sa_len;

	if(ips == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_del: "
			    "null pointer passed in!\n");
		return -ENODATA;
	}
	
	sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
	if(ips->ips_inext || ips->ips_onext) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_del: "
			    "SA:%s still linked!\n",
			    sa_len ? sa : " (error)");
		return -EMLINK;
	}
	
	hashval = IPS_HASH(&ips->ips_said);
	
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_del: "
		    "deleting SA:%s, hashval=%d.\n",
		    sa_len ? sa : " (error)",
		    hashval);
	if(ipsec_sadb_hash[hashval] == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_del: "
			    "no entries in ipsec_sa table for hash=%d of SA:%s.\n",
			    hashval,
			    sa_len ? sa : " (error)");
		return -ENOENT;
	}
	
	if (ips == ipsec_sadb_hash[hashval]) {
		ipsec_sadb_hash[hashval] = ipsec_sadb_hash[hashval]->ips_hnext;
		ips->ips_hnext = NULL;
		atomic_dec(&ips->ips_refcount);
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_del: "
			    "successfully deleted first ipsec_sa in chain.\n");
		return 0;
	} else {
		for (ipstp = ipsec_sadb_hash[hashval];
		     ipstp;
		     ipstp = ipstp->ips_hnext) {
			if (ipstp->ips_hnext == ips) {
				ipstp->ips_hnext = ips->ips_hnext;
				ips->ips_hnext = NULL;
				atomic_dec(&ips->ips_refcount);
				KLIPS_PRINT(debug_xform,
					    "klips_debug:ipsec_sa_del: "
					    "successfully deleted link in ipsec_sa chain.\n");
				return 0;
			}
		}
	}
	
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_del: "
		    "no entries in linked list for hash=%d of SA:%s.\n",
		    hashval,
		    sa_len ? sa : " (error)");
	return -ENOENT;
}

/*
  The ipsec_sa table better be locked before it is handed in, or races
  might happen
*/
int
ipsec_sa_delchain(struct ipsec_sa *ips)
{
	struct ipsec_sa *ipsdel;
	int error = 0;
        char sa[SATOT_BUF];
	size_t sa_len;

	if(ips == NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_error:ipsec_sa_delchain: "
			    "null pointer passed in!\n");
		return -ENODATA;
	}

	sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sa_delchain: "
		    "passed SA:%s\n",
		    sa_len ? sa : " (error)");
	while(ips->ips_onext != NULL) {
		ips = ips->ips_onext;
	}

	while(ips) {
		/* XXX send a pfkey message up to advise of deleted ipsec_sa */
		sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_delchain: "
			    "unlinking and delting SA:%s",
			    sa_len ? sa : " (error)");
		ipsdel = ips;
		ips = ips->ips_inext;
		if(ips != NULL) {
			sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
			KLIPS_PRINT(debug_xform,
				    ", inext=%s",
				    sa_len ? sa : " (error)");
			atomic_dec(&ipsdel->ips_refcount);
			ipsdel->ips_inext = NULL;
			atomic_dec(&ips->ips_refcount);
			ips->ips_onext = NULL;
		}
		KLIPS_PRINT(debug_xform,
			    ".\n");
		if((error = ipsec_sa_del(ipsdel))) {
			KLIPS_PRINT(debug_xform,
				    "klips_debug:ipsec_sa_delchain: "
				    "ipsec_sa_del returned error %d.\n", -error);
			return error;
		}
		if((error = ipsec_sa_wipe(ipsdel))) {
			KLIPS_PRINT(debug_xform,
				    "klips_debug:ipsec_sa_delchain: "
				    "ipsec_sa_wipe returned error %d.\n", -error);
			return error;
		}
	}
	return error;
}

int 
ipsec_sadb_cleanup(__u8 proto)
{
	unsigned i;
	int error = 0;
	struct ipsec_sa *ips, **ipsprev, *ipsdel;
        char sa[SATOT_BUF];
	size_t sa_len;

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sadb_cleanup: "
		    "cleaning up proto=%d.\n",
		    proto);

	spin_lock_bh(&tdb_lock);

	for (i = 0; i < SADB_HASHMOD; i++) {
		ipsprev = &(ipsec_sadb_hash[i]);
		ips = ipsec_sadb_hash[i];
		if(ips != NULL) {
			atomic_inc(&ips->ips_refcount);
		}
		for(; ips != NULL;) {
			sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
			KLIPS_PRINT(debug_xform,
				    "klips_debug:ipsec_sadb_cleanup: "
				    "checking SA:%s, hash=%d, ref=%d",
				    sa_len ? sa : " (error)",
				    i,
				    ips->ips_ref);
			ipsdel = ips;
			ips = ipsdel->ips_hnext;
			if(ips != NULL) {
				atomic_inc(&ips->ips_refcount);
				sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
				KLIPS_PRINT(debug_xform,
					    ", hnext=%s",
					    sa_len ? sa : " (error)");
			}
			if(*ipsprev != NULL) {
				sa_len = KLIPS_SATOT(debug_xform, &(*ipsprev)->ips_said, 0, sa, sizeof(sa));
				KLIPS_PRINT(debug_xform,
					    ", *ipsprev=%s",
					    sa_len ? sa : " (error)");
				if((*ipsprev)->ips_hnext) {
					sa_len = KLIPS_SATOT(debug_xform, &(*ipsprev)->ips_hnext->ips_said, 0, sa, sizeof(sa));
					KLIPS_PRINT(debug_xform,
						    ", *ipsprev->ips_hnext=%s",
						    sa_len ? sa : " (error)");
				}
			}
			KLIPS_PRINT(debug_xform,
				    ".\n");
			if(proto == 0 || (proto == ipsdel->ips_said.proto)) {
				sa_len = KLIPS_SATOT(debug_xform, &ipsdel->ips_said, 0, sa, sizeof(sa));
				KLIPS_PRINT(debug_xform,
					    "klips_debug:ipsec_sadb_cleanup: "
					    "deleting SA chain:%s.\n",
					    sa_len ? sa : " (error)");
				if((error = ipsec_sa_delchain(ipsdel))) {
					SENDERR(-error);
				}
				ipsprev = &(ipsec_sadb_hash[i]);
				ips = ipsec_sadb_hash[i];

				KLIPS_PRINT(debug_xform,
					    "klips_debug:ipsec_sadb_cleanup: "
					    "deleted SA chain:%s",
					    sa_len ? sa : " (error)");
				if(ips != NULL) {
					sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
					KLIPS_PRINT(debug_xform,
						    ", ipsec_sadb_hash[%d]=%s",
						    i,
						    sa_len ? sa : " (error)");
				}
				if(*ipsprev != NULL) {
					sa_len = KLIPS_SATOT(debug_xform, &(*ipsprev)->ips_said, 0, sa, sizeof(sa));
					KLIPS_PRINT(debug_xform,
						    ", *ipsprev=%s",
						    sa_len ? sa : " (error)");
					if((*ipsprev)->ips_hnext != NULL) {
					        sa_len = KLIPS_SATOT(debug_xform, &(*ipsprev)->ips_hnext->ips_said, 0, sa, sizeof(sa));
						KLIPS_PRINT(debug_xform,
							    ", *ipsprev->ips_hnext=%s",
							    sa_len ? sa : " (error)");
					}
				}
				KLIPS_PRINT(debug_xform,
					    ".\n");
			} else {
				ipsprev = &ipsdel;
			}
			if(ipsdel != NULL) {
				ipsec_sa_put(ipsdel);
			}
		}
	}
 errlab:

	spin_unlock_bh(&tdb_lock);


#if IPSEC_SA_REF_CODE
	/* clean up SA reference table */

	/* go through the ref table and clean out all the SAs */
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sadb_cleanup: "
		    "removing SAref entries and tables.");
	{
		unsigned table, entry;
		for(table = 0; table < IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES; table++) {
			KLIPS_PRINT(debug_xform,
				    "klips_debug:ipsec_sadb_cleanup: "
				    "cleaning SAref table=%u.\n",
				    table);
			if(ipsec_sadb.refTable[table] == NULL) {
				printk("\n");
				KLIPS_PRINT(debug_xform,
					    "klips_debug:ipsec_sadb_cleanup: "
					    "cleaned %u used refTables.\n",
					    table);
				break;
			}
			for(entry = 0; entry < IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES; entry++) {
				if(ipsec_sadb.refTable[table]->entry[entry] != NULL) {
					ipsec_sa_delchain(ipsec_sadb.refTable[table]->entry[entry]);
					ipsec_sadb.refTable[table]->entry[entry] = NULL;
				}
			}
		}
	}
#endif /* IPSEC_SA_REF_CODE */

	return(error);
}

int 
ipsec_sadb_free(void)
{
	int error = 0;

	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sadb_free: "
		    "freeing SArefTable memory.\n");

	/* clean up SA reference table */

	/* go through the ref table and clean out all the SAs if any are
	   left and free table memory */
	KLIPS_PRINT(debug_xform,
		    "klips_debug:ipsec_sadb_free: "
		    "removing SAref entries and tables.\n");
	{
		unsigned table, entry;
		for(table = 0; table < IPSEC_SA_REF_MAINTABLE_NUM_ENTRIES; table++) {
			KLIPS_PRINT(debug_xform,
				    "klips_debug:ipsec_sadb_free: "
				    "removing SAref table=%u.\n",
				    table);
			if(ipsec_sadb.refTable[table] == NULL) {
				KLIPS_PRINT(debug_xform,
					    "klips_debug:ipsec_sadb_free: "
					    "removed %u used refTables.\n",
					    table);
				break;
			}
			for(entry = 0; entry < IPSEC_SA_REF_SUBTABLE_NUM_ENTRIES; entry++) {
				if(ipsec_sadb.refTable[table]->entry[entry] != NULL) {
					ipsec_sa_delchain(ipsec_sadb.refTable[table]->entry[entry]);
					ipsec_sadb.refTable[table]->entry[entry] = NULL;
				}
			}
			vfree(ipsec_sadb.refTable[table]);
			ipsec_sadb.refTable[table] = NULL;
		}
	}

	return(error);
}

int
ipsec_sa_wipe(struct ipsec_sa *ips)
{
	if(ips == NULL) {
		return -ENODATA;
	}

	/* if(atomic_dec_and_test(ips)) {
	}; */

#if IPSEC_SA_REF_CODE
	/* remove me from the SArefTable */
	{
		char sa[SATOT_BUF];
		size_t sa_len;
		sa_len = KLIPS_SATOT(debug_xform, &ips->ips_said, 0, sa, sizeof(sa));
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_wipe: "
			    "removing SA=%s(0p%p), SAref=%d, table=%d(0p%p), entry=%d from the refTable.\n",
			    sa_len ? sa : " (error)",
			    ips,
			    ips->ips_ref,
			    IPsecSAref2table(IPsecSA2SAref(ips)),
			    ipsec_sadb.refTable[IPsecSAref2table(IPsecSA2SAref(ips))],
			    IPsecSAref2entry(IPsecSA2SAref(ips)));
	}
	if(ips->ips_ref == IPSEC_SAREF_NULL) {
		KLIPS_PRINT(debug_xform,
			    "klips_debug:ipsec_sa_wipe: "
			    "why does this SA not have a valid SAref?.\n");
	}
	ipsec_sadb.refTable[IPsecSAref2table(IPsecSA2SAref(ips))]->entry[IPsecSAref2entry(IPsecSA2SAref(ips))] = NULL;
	ips->ips_ref = IPSEC_SAREF_NULL;
	ipsec_sa_put(ips);
#endif /* IPSEC_SA_REF_CODE */

	/* paranoid clean up */
	if(ips->ips_addr_s != NULL) {
		memset((caddr_t)(ips->ips_addr_s), 0, ips->ips_addr_s_size);
		kfree(ips->ips_addr_s);
	}
	ips->ips_addr_s = NULL;

	if(ips->ips_addr_d != NULL) {
		memset((caddr_t)(ips->ips_addr_d), 0, ips->ips_addr_d_size);
		kfree(ips->ips_addr_d);
	}
	ips->ips_addr_d = NULL;

	if(ips->ips_addr_p != NULL) {
		memset((caddr_t)(ips->ips_addr_p), 0, ips->ips_addr_p_size);
		kfree(ips->ips_addr_p);
	}
	ips->ips_addr_p = NULL;

#ifdef CONFIG_IPSEC_NAT_TRAVERSAL
	if(ips->ips_natt_oa) {
		memset((caddr_t)(ips->ips_natt_oa), 0, ips->ips_natt_oa_size);
		kfree(ips->ips_natt_oa);
	}
	ips->ips_natt_oa = NULL;
#endif

	if(ips->ips_key_a != NULL) {
		memset((caddr_t)(ips->ips_key_a), 0, ips->ips_key_a_size);
		kfree(ips->ips_key_a);
	}
	ips->ips_key_a = NULL;

	if(ips->ips_key_e != NULL) {
		if (ips->ips_alg_enc &&
		    ips->ips_alg_enc->ixt_e_destroy_key)
		{
			ips->ips_alg_enc->ixt_e_destroy_key(ips->ips_alg_enc, 
							    ips->ips_key_e);
		} else
		{
			memset((caddr_t)(ips->ips_key_e), 0, ips->ips_key_e_size);
			kfree(ips->ips_key_e);
		}
	}
	ips->ips_key_e = NULL;

	if(ips->ips_iv != NULL) {
		memset((caddr_t)(ips->ips_iv), 0, ips->ips_iv_size);
		kfree(ips->ips_iv);
	}
	ips->ips_iv = NULL;

#ifdef CONFIG_KLIPS_OCF
	if (ips->ocf_in_use)
		ipsec_ocf_sa_free(ips);
#endif

	if(ips->ips_ident_s.data != NULL) {
		memset((caddr_t)(ips->ips_ident_s.data),
                       0,
		       ips->ips_ident_s.len * IPSEC_PFKEYv2_ALIGN - sizeof(struct sadb_ident));
		kfree(ips->ips_ident_s.data);
        }
	ips->ips_ident_s.data = NULL;
	
	if(ips->ips_ident_d.data != NULL) {
		memset((caddr_t)(ips->ips_ident_d.data),
                       0,
		       ips->ips_ident_d.len * IPSEC_PFKEYv2_ALIGN - sizeof(struct sadb_ident));
		kfree(ips->ips_ident_d.data);
        }
	ips->ips_ident_d.data = NULL;

#ifdef CONFIG_KLIPS_ALG
	if (ips->ips_alg_enc||ips->ips_alg_auth) {
		ipsec_alg_sa_wipe(ips);
	}
#endif /* CONFIG_KLIPS_ALG */
	
	memset((caddr_t)ips, 0, sizeof(*ips));
	kfree(ips);
	ips = NULL;

	return 0;
}

extern int sysctl_ipsec_debug_verbose;

int ipsec_sa_init(struct ipsec_sa *ipsp)
{
        int i;
        int error = 0;
        char sa[SATOT_BUF];
	size_t sa_len;
	char ipaddr_txt[ADDRTOA_BUF];
	char ipaddr2_txt[ADDRTOA_BUF];
#if defined (CONFIG_KLIPS_AUTH_HMAC_MD5) || defined (CONFIG_KLIPS_AUTH_HMAC_SHA1)
	unsigned char kb[AHMD596_BLKLEN];
#endif
#if defined CONFIG_KLIPS_ALG
	struct ipsec_alg_enc *ixt_e = NULL;
	struct ipsec_alg_auth *ixt_a = NULL;
#endif

	if(ipsp == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "ipsec_sa_init: "
			    "ipsp is NULL, fatal\n");
		SENDERR(EINVAL);
	}

	sa_len = KLIPS_SATOT(debug_pfkey, &ipsp->ips_said, 0, sa, sizeof(sa));

        KLIPS_PRINT(debug_pfkey,
		    "ipsec_sa_init: "
		    "(pfkey defined) called for SA:%s\n",
		    sa_len ? sa : " (error)");

	KLIPS_PRINT(debug_pfkey,
		    "ipsec_sa_init: "
		    "calling init routine of %s%s%s\n",
		    IPS_XFORM_NAME(ipsp));
	
	switch(ipsp->ips_said.proto) {
		
#ifdef CONFIG_KLIPS_IPIP
	case IPPROTO_IPIP: {
		addrtoa(((struct sockaddr_in*)(ipsp->ips_addr_s))->sin_addr,
			0,
			ipaddr_txt, sizeof(ipaddr_txt));
		addrtoa(((struct sockaddr_in*)(ipsp->ips_addr_d))->sin_addr,
			0,
			ipaddr2_txt, sizeof(ipaddr_txt));
		KLIPS_PRINT(debug_pfkey,
			    "ipsec_sa_init: "
			    "(pfkey defined) IPIP ipsec_sa set for %s->%s.\n",
			    ipaddr_txt,
			    ipaddr2_txt);
	}
	break;
#endif /* !CONFIG_KLIPS_IPIP */

#ifdef CONFIG_KLIPS_AH
	case IPPROTO_AH:

#ifdef CONFIG_KLIPS_OCF
		if (ipsec_ocf_sa_init(ipsp, ipsp->ips_authalg, 0))
		    break;
#endif

		switch(ipsp->ips_authalg) {
# ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		case AH_MD5: {
			unsigned char *akp;
			unsigned int aks;
			MD5_CTX *ictx;
			MD5_CTX *octx;
			
			if(ipsp->ips_key_bits_a != (AHMD596_KLEN * 8)) {
				KLIPS_PRINT(debug_pfkey,
					    "ipsec_sa_init: "
					    "incorrect key size: %d bits -- must be %d bits\n"/*octets (bytes)\n"*/,
					    ipsp->ips_key_bits_a, AHMD596_KLEN * 8);
				SENDERR(EINVAL);
			}
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "hmac md5-96 key is 0x%08x %08x %08x %08x\n",
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+0)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+1)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+2)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+3)));
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			
			ipsp->ips_auth_bits = AHMD596_ALEN * 8;
			
			/* save the pointer to the key material */
			akp = ipsp->ips_key_a;
			aks = ipsp->ips_key_a_size;
			
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
			           "ipsec_sa_init: "
			           "allocating %lu bytes for md5_ctx.\n",
			           (unsigned long) sizeof(struct md5_ctx));
			if((ipsp->ips_key_a = (caddr_t)
			    kmalloc(sizeof(struct md5_ctx), GFP_ATOMIC)) == NULL) {
				ipsp->ips_key_a = akp;
				SENDERR(ENOMEM);
			}
			ipsp->ips_key_a_size = sizeof(struct md5_ctx);

			for (i = 0; i < DIVUP(ipsp->ips_key_bits_a, 8); i++) {
				kb[i] = akp[i] ^ HMAC_IPAD;
			}
			for (; i < AHMD596_BLKLEN; i++) {
				kb[i] = HMAC_IPAD;
			}

			ictx = &(((struct md5_ctx*)(ipsp->ips_key_a))->ictx);
			osMD5Init(ictx);
			osMD5Update(ictx, kb, AHMD596_BLKLEN);

			for (i = 0; i < AHMD596_BLKLEN; i++) {
				kb[i] ^= (HMAC_IPAD ^ HMAC_OPAD);
			}

			octx = &(((struct md5_ctx*)(ipsp->ips_key_a))->octx);
			osMD5Init(octx);
			osMD5Update(octx, kb, AHMD596_BLKLEN);
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "MD5 ictx=0x%08x %08x %08x %08x octx=0x%08x %08x %08x %08x\n",
				    ((__u32*)ictx)[0],
				    ((__u32*)ictx)[1],
				    ((__u32*)ictx)[2],
				    ((__u32*)ictx)[3],
				    ((__u32*)octx)[0],
				    ((__u32*)octx)[1],
				    ((__u32*)octx)[2],
				    ((__u32*)octx)[3] );
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			
			/* zero key buffer -- paranoid */
			memset(akp, 0, aks);
			kfree(akp);
		}
		break;
# endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
# ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		case AH_SHA: {
			unsigned char *akp;
			unsigned int aks;
			SHA1_CTX *ictx;
			SHA1_CTX *octx;
			
			if(ipsp->ips_key_bits_a != (AHSHA196_KLEN * 8)) {
				KLIPS_PRINT(debug_pfkey,
					    "ipsec_sa_init: "
					    "incorrect key size: %d bits -- must be %d bits\n"/*octets (bytes)\n"*/,
					    ipsp->ips_key_bits_a, AHSHA196_KLEN * 8);
				SENDERR(EINVAL);
			}
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "hmac sha1-96 key is 0x%08x %08x %08x %08x\n",
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+0)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+1)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+2)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+3)));
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			
			ipsp->ips_auth_bits = AHSHA196_ALEN * 8;
			
			/* save the pointer to the key material */
			akp = ipsp->ips_key_a;
			aks = ipsp->ips_key_a_size;
			
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
			            "ipsec_sa_init: "
			            "allocating %lu bytes for sha1_ctx.\n",
			            (unsigned long) sizeof(struct sha1_ctx));
			if((ipsp->ips_key_a = (caddr_t)
			    kmalloc(sizeof(struct sha1_ctx), GFP_ATOMIC)) == NULL) {
				ipsp->ips_key_a = akp;
				SENDERR(ENOMEM);
			}
			ipsp->ips_key_a_size = sizeof(struct sha1_ctx);

			for (i = 0; i < DIVUP(ipsp->ips_key_bits_a, 8); i++) {
				kb[i] = akp[i] ^ HMAC_IPAD;
			}
			for (; i < AHMD596_BLKLEN; i++) {
				kb[i] = HMAC_IPAD;
			}

			ictx = &(((struct sha1_ctx*)(ipsp->ips_key_a))->ictx);
			SHA1Init(ictx);
			SHA1Update(ictx, kb, AHSHA196_BLKLEN);

			for (i = 0; i < AHSHA196_BLKLEN; i++) {
				kb[i] ^= (HMAC_IPAD ^ HMAC_OPAD);
			}

			octx = &(((struct sha1_ctx*)(ipsp->ips_key_a))->octx);
			SHA1Init(octx);
			SHA1Update(octx, kb, AHSHA196_BLKLEN);
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "SHA1 ictx=0x%08x %08x %08x %08x octx=0x%08x %08x %08x %08x\n", 
				    ((__u32*)ictx)[0],
				    ((__u32*)ictx)[1],
				    ((__u32*)ictx)[2],
				    ((__u32*)ictx)[3],
				    ((__u32*)octx)[0],
				    ((__u32*)octx)[1],
				    ((__u32*)octx)[2],
				    ((__u32*)octx)[3] );
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			/* zero key buffer -- paranoid */
			memset(akp, 0, aks);
			kfree(akp);
		}
		break;
# endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
		default:
			KLIPS_PRINT(debug_pfkey,
				    "ipsec_sa_init: "
				    "authalg=%d support not available in the kernel",
				    ipsp->ips_authalg);
			SENDERR(EINVAL);
		}
	break;
#endif /* CONFIG_KLIPS_AH */

#ifdef CONFIG_KLIPS_ESP
	case IPPROTO_ESP:
	{
#if defined (CONFIG_KLIPS_AUTH_HMAC_MD5) || defined (CONFIG_KLIPS_AUTH_HMAC_SHA1)
		unsigned char *akp;
		unsigned int aks;
#endif
		ipsp->ips_iv_size = 0;

#ifdef CONFIG_KLIPS_OCF
		if (ipsec_ocf_sa_init(ipsp, ipsp->ips_authalg, ipsp->ips_encalg))
		    break;
#endif

#ifdef CONFIG_KLIPS_ALG
		ipsec_alg_sa_init(ipsp);
		ixt_e=ipsp->ips_alg_enc;

		if (ixt_e == NULL) {
			if(printk_ratelimit()) {
				printk(KERN_INFO 
				       "ipsec_sa_init: "
				       "encalg=%d support not available in the kernel",
				       ipsp->ips_encalg);
			}
			SENDERR(ENOENT);
		}

		ipsp->ips_iv_size = ixt_e->ixt_common.ixt_support.ias_ivlen/8;

		/* Create IV */
		if (ipsp->ips_iv_size) {
			if((ipsp->ips_iv = (caddr_t)
			    kmalloc(ipsp->ips_iv_size, GFP_ATOMIC)) == NULL) {
				SENDERR(ENOMEM);
			}
			prng_bytes(&ipsec_prng,
				   (char *)ipsp->ips_iv,
				   ipsp->ips_iv_size);
			ipsp->ips_iv_bits = ipsp->ips_iv_size * 8;
		}
		
		if ((error=ipsec_alg_enc_key_create(ipsp)) < 0)
			SENDERR(-error);
#endif /* CONFIG_KLIPS_ALG */

#ifdef CONFIG_KLIPS_ALG
		if ((ixt_a=ipsp->ips_alg_auth)) {
			if ((error=ipsec_alg_auth_key_create(ipsp)) < 0)
				SENDERR(-error);
		} else	
#endif /* CONFIG_KLIPS_ALG */
		
		switch(ipsp->ips_authalg) {
# ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		case AH_MD5: {
			MD5_CTX *ictx;
			MD5_CTX *octx;

			if(ipsp->ips_key_bits_a != (AHMD596_KLEN * 8)) {
				KLIPS_PRINT(debug_pfkey,
					    "ipsec_sa_init: "
					    "incorrect authorisation key size: %d bits -- must be %d bits\n"/*octets (bytes)\n"*/,
					    ipsp->ips_key_bits_a,
					    AHMD596_KLEN * 8);
				SENDERR(EINVAL);
			}
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "hmac md5-96 key is 0x%08x %08x %08x %08x\n",
				    ntohl(*(((__u32 *)(ipsp->ips_key_a))+0)),
				    ntohl(*(((__u32 *)(ipsp->ips_key_a))+1)),
				    ntohl(*(((__u32 *)(ipsp->ips_key_a))+2)),
				    ntohl(*(((__u32 *)(ipsp->ips_key_a))+3)));
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			ipsp->ips_auth_bits = AHMD596_ALEN * 8;
			
			/* save the pointer to the key material */
			akp = ipsp->ips_key_a;
			aks = ipsp->ips_key_a_size;
			
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
			            "ipsec_sa_init: "
			            "allocating %lu bytes for md5_ctx.\n",
			            (unsigned long) sizeof(struct md5_ctx));
			if((ipsp->ips_key_a = (caddr_t)
			    kmalloc(sizeof(struct md5_ctx), GFP_ATOMIC)) == NULL) {
				ipsp->ips_key_a = akp;
				SENDERR(ENOMEM);
			}
			ipsp->ips_key_a_size = sizeof(struct md5_ctx);

			for (i = 0; i < DIVUP(ipsp->ips_key_bits_a, 8); i++) {
				kb[i] = akp[i] ^ HMAC_IPAD;
			}
			for (; i < AHMD596_BLKLEN; i++) {
				kb[i] = HMAC_IPAD;
			}

			ictx = &(((struct md5_ctx*)(ipsp->ips_key_a))->ictx);
			osMD5Init(ictx);
			osMD5Update(ictx, kb, AHMD596_BLKLEN);

			for (i = 0; i < AHMD596_BLKLEN; i++) {
				kb[i] ^= (HMAC_IPAD ^ HMAC_OPAD);
			}

			octx = &(((struct md5_ctx*)(ipsp->ips_key_a))->octx);
			osMD5Init(octx);
			osMD5Update(octx, kb, AHMD596_BLKLEN);
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "MD5 ictx=0x%08x %08x %08x %08x octx=0x%08x %08x %08x %08x\n",
				    ((__u32*)ictx)[0],
				    ((__u32*)ictx)[1],
				    ((__u32*)ictx)[2],
				    ((__u32*)ictx)[3],
				    ((__u32*)octx)[0],
				    ((__u32*)octx)[1],
				    ((__u32*)octx)[2],
				    ((__u32*)octx)[3] );
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			/* paranoid */
			memset(akp, 0, aks);
			kfree(akp);
			break;
		}
# endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
# ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		case AH_SHA: {
			SHA1_CTX *ictx;
			SHA1_CTX *octx;

			if(ipsp->ips_key_bits_a != (AHSHA196_KLEN * 8)) {
				KLIPS_PRINT(debug_pfkey,
					    "ipsec_sa_init: "
					    "incorrect authorisation key size: %d bits -- must be %d bits\n"/*octets (bytes)\n"*/,
					    ipsp->ips_key_bits_a,
					    AHSHA196_KLEN * 8);
				SENDERR(EINVAL);
			}
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "hmac sha1-96 key is 0x%08x %08x %08x %08x\n",
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+0)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+1)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+2)),
				    ntohl(*(((__u32 *)ipsp->ips_key_a)+3)));
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			ipsp->ips_auth_bits = AHSHA196_ALEN * 8;
			
			/* save the pointer to the key material */
			akp = ipsp->ips_key_a;
			aks = ipsp->ips_key_a_size;

			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
			            "ipsec_sa_init: "
			            "allocating %lu bytes for sha1_ctx.\n",
			            (unsigned long) sizeof(struct sha1_ctx));
			if((ipsp->ips_key_a = (caddr_t)
			    kmalloc(sizeof(struct sha1_ctx), GFP_ATOMIC)) == NULL) {
				ipsp->ips_key_a = akp;
				SENDERR(ENOMEM);
			}
			ipsp->ips_key_a_size = sizeof(struct sha1_ctx);

			for (i = 0; i < DIVUP(ipsp->ips_key_bits_a, 8); i++) {
				kb[i] = akp[i] ^ HMAC_IPAD;
			}
			for (; i < AHMD596_BLKLEN; i++) {
				kb[i] = HMAC_IPAD;
			}

			ictx = &(((struct sha1_ctx*)(ipsp->ips_key_a))->ictx);
			SHA1Init(ictx);
			SHA1Update(ictx, kb, AHSHA196_BLKLEN);

			for (i = 0; i < AHSHA196_BLKLEN; i++) {
				kb[i] ^= (HMAC_IPAD ^ HMAC_OPAD);
			}

			octx = &((struct sha1_ctx*)(ipsp->ips_key_a))->octx;
			SHA1Init(octx);
			SHA1Update(octx, kb, AHSHA196_BLKLEN);
			
#  if KLIPS_DIVULGE_HMAC_KEY
			KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
				    "ipsec_sa_init: "
				    "SHA1 ictx=0x%08x %08x %08x %08x octx=0x%08x %08x %08x %08x\n",
				    ((__u32*)ictx)[0],
				    ((__u32*)ictx)[1],
				    ((__u32*)ictx)[2],
				    ((__u32*)ictx)[3],
				    ((__u32*)octx)[0],
				    ((__u32*)octx)[1],
				    ((__u32*)octx)[2],
				    ((__u32*)octx)[3] );
#  endif /* KLIPS_DIVULGE_HMAC_KEY */
			memset(akp, 0, aks);
			kfree(akp);
			break;
		}
# endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
		case AH_NONE:
			break;
		default:
			KLIPS_PRINT(debug_pfkey,
				    "ipsec_sa_init: "
				    "authalg=%d support not available in the kernel.\n",
				    ipsp->ips_authalg);
			SENDERR(EINVAL);
		}
	}
			break;
#endif /* !CONFIG_KLIPS_ESP */
#ifdef CONFIG_KLIPS_IPCOMP
	case IPPROTO_COMP:
		ipsp->ips_comp_adapt_tries = 0;
		ipsp->ips_comp_adapt_skip = 0;
		ipsp->ips_comp_ratio_cbytes = 0;
		ipsp->ips_comp_ratio_dbytes = 0;
		break;
#endif /* CONFIG_KLIPS_IPCOMP */
	default:
		printk(KERN_ERR "KLIPS sa initialization: "
		       "proto=%d unknown.\n",
		       ipsp->ips_said.proto);
		SENDERR(EINVAL);
	}
	
 errlab:
	return(error);
}



/*
 * $Log: ipsec_sa.c,v $
 * Revision 1.30.2.1  2006/04/20 16:33:07  mcr
 * remove all of CONFIG_KLIPS_ALG --- one can no longer build without it.
 * Fix in-kernel module compilation. Sub-makefiles do not work.
 *
 * Revision 1.30  2005/05/24 01:02:35  mcr
 * 	some refactoring/simplification of situation where alg
 * 	is not found.
 *
 * Revision 1.29  2005/05/18 19:13:28  mcr
 * 	rename debug messages. make sure that algo not found is not
 * 	a debug message.
 *
 * Revision 1.28  2005/05/11 01:30:20  mcr
 * 	removed "poor-man"s OOP in favour of proper C structures.
 *
 * Revision 1.27  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.26  2005/04/14 20:56:24  mcr
 * 	moved (pfkey_)ipsec_sa_init to ipsec_sa.c.
 *
 * Revision 1.25  2004/08/22 20:12:16  mcr
 * 	one more KLIPS_NAT->IPSEC_NAT.
 *
 * Revision 1.24  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.23  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.22.2.1  2003/12/22 15:25:52  jjo
 * . Merged algo-0.8.1-rc11-test1 into alg-branch
 *
 * Revision 1.22  2003/12/10 01:14:27  mcr
 * 	NAT-traversal patches to KLIPS.
 *
 * Revision 1.21  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.20.4.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.20  2003/02/06 01:50:34  rgb
 * Fixed initialisation bug for first sadb hash bucket that would only manifest itself on platforms where NULL != 0.
 *
 * Revision 1.19  2003/01/30 02:32:22  rgb
 *
 * Rename SAref table macro names for clarity.
 * Transmit error code through to caller from callee for better diagnosis of problems.
 * Convert IPsecSAref_t from signed to unsigned to fix apparent SAref exhaustion bug.
 *
 * Revision 1.18  2002/10/12 23:11:53  dhr
 *
 * [KenB + DHR] more 64-bit cleanup
 *
 * Revision 1.17  2002/10/07 18:31:43  rgb
 * Move field width sanity checks to ipsec_sa.c
 *
 * Revision 1.16  2002/09/20 15:41:02  rgb
 * Re-wrote most of the SAref code to eliminate Entry pointers.
 * Added SAref code compiler directive switch.
 * Added a saref test function for testing macros.
 * Switch from pfkey_alloc_ipsec_sa() to ipsec_sa_alloc().
 * Split ipsec_sadb_cleanup from new funciton ipsec_sadb_free to avoid problem
 * of freeing newly created structures when clearing the reftable upon startup
 * to start from a known state.
 * Place all ipsec sadb globals into one struct.
 * Rework saref freelist.
 * Added memory allocation debugging.
 *
 * Revision 1.15  2002/09/20 05:01:44  rgb
 * Update copyright date.
 *
 * Revision 1.14  2002/08/13 19:01:25  mcr
 * 	patches from kenb to permit compilation of FreeSWAN on ia64.
 * 	des library patched to use proper DES_LONG type for ia64.
 *
 * Revision 1.13  2002/07/29 03:06:20  mcr
 * 	get rid of variable not used warnings.
 *
 * Revision 1.12  2002/07/26 08:48:31  rgb
 * Added SA ref table code.
 *
 * Revision 1.11  2002/06/04 16:48:49  rgb
 * Tidied up pointer code for processor independance.
 *
 * Revision 1.10  2002/05/23 07:16:17  rgb
 * Added ipsec_sa_put() for releasing an ipsec_sa refcount.
 * Pointer clean-up.
 * Added refcount code.
 * Convert "usecount" to "refcount" to remove ambiguity.
 *
 * Revision 1.9  2002/05/14 02:34:49  rgb
 * Converted reference from ipsec_sa_put to ipsec_sa_add to avoid confusion
 * with "put" usage in the kernel.
 * Change all references to tdb, TDB or Tunnel Descriptor Block to ips,
 * ipsec_sa or ipsec_sa.
 * Added some preliminary refcount code.
 *
 * Revision 1.8  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.7  2002/04/24 07:36:30  mcr
 * Moved from ./klips/net/ipsec/ipsec_sa.c,v
 *
 * Revision 1.6  2002/04/20 00:12:25  rgb
 * Added esp IV CBC attack fix, disabled.
 *
 * Revision 1.5  2002/01/29 17:17:56  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.4  2002/01/29 04:00:52  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.3  2002/01/29 02:13:18  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.2  2001/11/26 09:16:15  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.1.2.2  2001/10/22 21:05:41  mcr
 * 	removed phony prototype for des_set_key.
 *
 * Revision 1.1.2.1  2001/09/25 02:24:57  mcr
 * 	struct tdb -> struct ipsec_sa.
 * 	sa(tdb) manipulation functions renamed and moved to ipsec_sa.c
 * 	ipsec_xform.c removed. header file still contains useful things.
 *
 *
 *
 * CLONED from ipsec_xform.c:
 * Revision 1.53  2001/09/08 21:13:34  rgb
 * Added pfkey ident extension support for ISAKMPd. (NetCelo)
 *
 * Revision 1.52  2001/06/14 19:35:11  rgb
 * Update copyright date.
 *
 * Revision 1.51  2001/05/30 08:14:03  rgb
 * Removed vestiges of esp-null transforms.
 *
 * Revision 1.50  2001/05/03 19:43:18  rgb
 * Initialise error return variable.
 * Update SENDERR macro.
 * Fix sign of error return code for ipsec_tdbcleanup().
 * Use more appropriate return code for ipsec_tdbwipe().
 *
 * Revision 1.49  2001/04/19 18:56:17  rgb
 * Fixed tdb table locking comments.
 *
 * Revision 1.48  2001/02/27 22:24:55  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.47  2000/11/06 04:32:08  rgb
 * Ditched spin_lock_irqsave in favour of spin_lock_bh.
 *
 * Revision 1.46  2000/09/20 16:21:57  rgb
 * Cleaned up ident string alloc/free.
 *
 * Revision 1.45  2000/09/08 19:16:51  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Removed all references to CONFIG_IPSEC_PFKEYv2.
 *
 * Revision 1.44  2000/08/30 05:29:04  rgb
 * Compiler-define out no longer used tdb_init() in ipsec_xform.c.
 *
 * Revision 1.43  2000/08/18 21:30:41  rgb
 * Purged all tdb_spi, tdb_proto and tdb_dst macros.  They are unclear.
 *
 * Revision 1.42  2000/08/01 14:51:51  rgb
 * Removed _all_ remaining traces of DES.
 *
 * Revision 1.41  2000/07/28 14:58:31  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.40  2000/06/28 05:50:11  rgb
 * Actually set iv_bits.
 *
 * Revision 1.39  2000/05/10 23:11:09  rgb
 * Added netlink debugging output.
 * Added a cast to quiet down the ntohl bug.
 *
 * Revision 1.38  2000/05/10 19:18:42  rgb
 * Cast output of ntohl so that the broken prototype doesn't make our
 * compile noisy.
 *
 * Revision 1.37  2000/03/16 14:04:59  rgb
 * Hardwired CONFIG_IPSEC_PFKEYv2 on.
 *
 * Revision 1.36  2000/01/26 10:11:28  rgb
 * Fixed spacing in error text causing run-in words.
 *
 * Revision 1.35  2000/01/21 06:17:16  rgb
 * Tidied up compiler directive indentation for readability.
 * Added ictx,octx vars for simplification.(kravietz)
 * Added macros for HMAC padding magic numbers.(kravietz)
 * Fixed missing key length reporting bug.
 * Fixed bug in tdbwipe to return immediately on NULL tdbp passed in.
 *
 * Revision 1.34  1999/12/08 00:04:19  rgb
 * Fixed SA direction overwriting bug for netlink users.
 *
 * Revision 1.33  1999/12/01 22:16:44  rgb
 * Minor formatting changes in ESP MD5 initialisation.
 *
 * Revision 1.32  1999/11/25 09:06:36  rgb
 * Fixed error return messages, should be returning negative numbers.
 * Implemented SENDERR macro for propagating error codes.
 * Added debug message and separate error code for algorithms not compiled
 * in.
 *
 * Revision 1.31  1999/11/23 23:06:26  rgb
 * Sort out pfkey and freeswan headers, putting them in a library path.
 *
 * Revision 1.30  1999/11/18 04:09:20  rgb
 * Replaced all kernel version macros to shorter, readable form.
 *
 * Revision 1.29  1999/11/17 15:53:40  rgb
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 *
 * Revision 1.28  1999/10/18 20:04:01  rgb
 * Clean-out unused cruft.
 *
 * Revision 1.27  1999/10/03 19:01:03  rgb
 * Spinlock support for 2.3.xx and 2.0.xx kernels.
 *
 * Revision 1.26  1999/10/01 16:22:24  rgb
 * Switch from assignment init. to functional init. of spinlocks.
 *
 * Revision 1.25  1999/10/01 15:44:54  rgb
 * Move spinlock header include to 2.1> scope.
 *
 * Revision 1.24  1999/10/01 00:03:46  rgb
 * Added tdb structure locking.
 * Minor formatting changes.
 * Add function to initialize tdb hash table.
 *
 * Revision 1.23  1999/05/25 22:42:12  rgb
 * Add deltdbchain() debugging.
 *
 * Revision 1.22  1999/05/25 21:24:31  rgb
 * Add debugging statements to deltdbchain().
 *
 * Revision 1.21  1999/05/25 03:51:48  rgb
 * Refix error return code.
 *
 * Revision 1.20  1999/05/25 03:34:07  rgb
 * Fix error return for flush.
 *
 * Revision 1.19  1999/05/09 03:25:37  rgb
 * Fix bug introduced by 2.2 quick-and-dirty patch.
 *
 * Revision 1.18  1999/05/05 22:02:32  rgb
 * Add a quick and dirty port to 2.2 kernels by Marc Boucher <marc@mbsi.ca>.
 *
 * Revision 1.17  1999/04/29 15:20:16  rgb
 * Change gettdb parameter to a pointer to reduce stack loading and
 * facilitate parameter sanity checking.
 * Add sanity checking for null pointer arguments.
 * Add debugging instrumentation.
 * Add function deltdbchain() which will take care of unlinking,
 * zeroing and deleting a chain of tdbs.
 * Add a parameter to tdbcleanup to be able to delete a class of SAs.
 * tdbwipe now actually zeroes the tdb as well as any of its pointed
 * structures.
 *
 * Revision 1.16  1999/04/16 15:36:29  rgb
 * Fix cut-and-paste error causing a memory leak in IPIP TDB freeing.
 *
 * Revision 1.15  1999/04/11 00:29:01  henry
 * GPL boilerplate
 *
 * Revision 1.14  1999/04/06 04:54:28  rgb
 * Fix/Add RCSID Id: and Log: bits to make PHMDs happy.  This includes
 * patch shell fixes.
 *
 * Revision 1.13  1999/02/19 18:23:01  rgb
 * Nix debug off compile warning.
 *
 * Revision 1.12  1999/02/17 16:52:16  rgb
 * Consolidate satoa()s for space and speed efficiency.
 * Convert DEBUG_IPSEC to KLIPS_PRINT
 * Clean out unused cruft.
 * Ditch NET_IPIP dependancy.
 * Loop for 3des key setting.
 *
 * Revision 1.11  1999/01/26 02:09:05  rgb
 * Remove ah/esp/IPIP switching on include files.
 * Removed CONFIG_IPSEC_ALGO_SWITCH macro.
 * Removed dead code.
 * Clean up debug code when switched off.
 * Remove references to INET_GET_PROTOCOL.
 * Added code exclusion macros to reduce code from unused algorithms.
 *
 * Revision 1.10  1999/01/22 06:28:55  rgb
 * Cruft clean-out.
 * Put random IV generation in kernel.
 * Added algorithm switch code.
 * Enhanced debugging.
 * 64-bit clean-up.
 *
 * Revision 1.9  1998/11/30 13:22:55  rgb
 * Rationalised all the klips kernel file headers.  They are much shorter
 * now and won't conflict under RH5.2.
 *
 * Revision 1.8  1998/11/25 04:59:06  rgb
 * Add conditionals for no IPIP tunnel code.
 * Delete commented out code.
 *
 * Revision 1.7  1998/10/31 06:50:41  rgb
 * Convert xform ASCII names to no spaces.
 * Fixed up comments in #endif directives.
 *
 * Revision 1.6  1998/10/19 14:44:28  rgb
 * Added inclusion of freeswan.h.
 * sa_id structure implemented and used: now includes protocol.
 *
 * Revision 1.5  1998/10/09 04:32:19  rgb
 * Added 'klips_debug' prefix to all klips printk debug statements.
 *
 * Revision 1.4  1998/08/12 00:11:31  rgb
 * Added new xform functions to the xform table.
 * Fixed minor debug output spelling error.
 *
 * Revision 1.3  1998/07/09 17:45:31  rgb
 * Clarify algorithm not available message.
 *
 * Revision 1.2  1998/06/23 03:00:51  rgb
 * Check for presence of IPIP protocol if it is setup one way (we don't
 * know what has been set up the other way and can only assume it will be
 * symmetrical with the exception of keys).
 *
 * Revision 1.1  1998/06/18 21:27:51  henry
 * move sources from klips/src to klips/net/ipsec, to keep stupid
 * kernel-build scripts happier in the presence of symlinks
 *
 * Revision 1.3  1998/06/11 05:54:59  rgb
 * Added transform version string pointer to xformsw initialisations.
 *
 * Revision 1.2  1998/04/21 21:28:57  rgb
 * Rearrange debug switches to change on the fly debug output from user
 * space.  Only kernel changes checked in at this time.  radij.c was also
 * changed to temporarily remove buggy debugging code in rj_delete causing
 * an OOPS and hence, netlink device open errors.
 *
 * Revision 1.1  1998/04/09 03:06:13  henry
 * sources moved up from linux/net/ipsec
 *
 * Revision 1.1.1.1  1998/04/08 05:35:02  henry
 * RGB's ipsec-0.8pre2.tar.gz ipsec-0.8
 *
 * Revision 0.5  1997/06/03 04:24:48  ji
 * Added ESP-3DES-MD5-96
 *
 * Revision 0.4  1997/01/15 01:28:15  ji
 * Added new transforms.
 *
 * Revision 0.3  1996/11/20 14:39:04  ji
 * Minor cleanups.
 * Rationalized debugging code.
 *
 * Revision 0.2  1996/11/02 00:18:33  ji
 * First limited release.
 *
 *
 */
