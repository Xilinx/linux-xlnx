char radij_c_version[] = "RCSID $Id: radij.c,v 1.48 2005/04/29 05:10:22 mcr Exp $";

/*
 * This file is defived from ${SRC}/sys/net/radix.c of BSD 4.4lite
 *
 * Variable and procedure names have been modified so that they don't
 * conflict with the original BSD code, as a small number of modifications
 * have been introduced and we may want to reuse this code in BSD.
 * 
 * The `j' in `radij' is pronounced as a voiceless guttural (like a Greek
 * chi or a German ch sound (as `doch', not as in `milch'), or even a 
 * spanish j as in Juan.  It is not as far back in the throat like
 * the corresponding Hebrew sound, nor is it a soft breath like the English h.
 * It has nothing to do with the Dutch ij sound.
 * 
 * Here is the appropriate copyright notice:
 */

/*
 * Copyright (c) 1988, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)radix.c	8.2 (Berkeley) 1/4/94
 */

/*
 * Routines to build and maintain radix trees for routing lookups.
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
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/skbuff.h>
#ifdef NET_21
# include <linux/in6.h>
#endif /* NET_21 */

#include <net/ip.h>

#include <openswan.h>

#include "openswan/radij.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"

int	maj_keylen;
struct radij_mask *rj_mkfreelist;
struct radij_node_head *mask_rjhead;
static int gotOddMasks;
static char *maskedKey;
static char *rj_zeroes, *rj_ones;

#define rj_masktop (mask_rjhead->rnh_treetop)
#ifdef Bcmp
# undef Bcmp
#endif /* Bcmp */
#define Bcmp(a, b, l) (l == 0 ? 0 : memcmp((caddr_t)(b), (caddr_t)(a), (size_t)l))
/*
 * The data structure for the keys is a radix tree with one way
 * branching removed.  The index rj_b at an internal node n represents a bit
 * position to be tested.  The tree is arranged so that all descendants
 * of a node n have keys whose bits all agree up to position rj_b - 1.
 * (We say the index of n is rj_b.)
 *
 * There is at least one descendant which has a one bit at position rj_b,
 * and at least one with a zero there.
 *
 * A route is determined by a pair of key and mask.  We require that the
 * bit-wise logical and of the key and mask to be the key.
 * We define the index of a route to associated with the mask to be
 * the first bit number in the mask where 0 occurs (with bit number 0
 * representing the highest order bit).
 * 
 * We say a mask is normal if every bit is 0, past the index of the mask.
 * If a node n has a descendant (k, m) with index(m) == index(n) == rj_b,
 * and m is a normal mask, then the route applies to every descendant of n.
 * If the index(m) < rj_b, this implies the trailing last few bits of k
 * before bit b are all 0, (and hence consequently true of every descendant
 * of n), so the route applies to all descendants of the node as well.
 *
 * The present version of the code makes no use of normal routes,
 * but similar logic shows that a non-normal mask m such that
 * index(m) <= index(n) could potentially apply to many children of n.
 * Thus, for each non-host route, we attach its mask to a list at an internal
 * node as high in the tree as we can go. 
 */

struct radij_node *
rj_search(v_arg, head)
	void *v_arg;
	struct radij_node *head;
{
	register struct radij_node *x;
	register caddr_t v;

	for (x = head, v = v_arg; x->rj_b >= 0;) {
		if (x->rj_bmask & v[x->rj_off])
			x = x->rj_r;
		else
			x = x->rj_l;
	}
	return (x);
};

struct radij_node *
rj_search_m(v_arg, head, m_arg)
	struct radij_node *head;
	void *v_arg, *m_arg;
{
	register struct radij_node *x;
	register caddr_t v = v_arg, m = m_arg;

	for (x = head; x->rj_b >= 0;) {
		if ((x->rj_bmask & m[x->rj_off]) &&
		    (x->rj_bmask & v[x->rj_off]))
			x = x->rj_r;
		else
			x = x->rj_l;
	}
	return x;
};

int
rj_refines(m_arg, n_arg)
	void *m_arg, *n_arg;
{
	register caddr_t m = m_arg, n = n_arg;
	register caddr_t lim, lim2 = lim = n + *(u_char *)n;
	int longer = (*(u_char *)n++) - (int)(*(u_char *)m++);
	int masks_are_equal = 1;

	if (longer > 0)
		lim -= longer;
	while (n < lim) {
		if (*n & ~(*m))
			return 0;
		if (*n++ != *m++)
			masks_are_equal = 0;
			
	}
	while (n < lim2)
		if (*n++)
			return 0;
	if (masks_are_equal && (longer < 0))
		for (lim2 = m - longer; m < lim2; )
			if (*m++)
				return 1;
	return (!masks_are_equal);
}


struct radij_node *
rj_match(v_arg, head)
	void *v_arg;
	struct radij_node_head *head;
{
	caddr_t v = v_arg;
	register struct radij_node *t = head->rnh_treetop, *x;
	register caddr_t cp = v, cp2, cp3;
	caddr_t cplim, mstart;
	struct radij_node *saved_t, *top = t;
	int off = t->rj_off, vlen = *(u_char *)cp, matched_off;

	/*
	 * Open code rj_search(v, top) to avoid overhead of extra
	 * subroutine call.
	 */
	for (; t->rj_b >= 0; ) {
		if (t->rj_bmask & cp[t->rj_off])
			t = t->rj_r;
		else
			t = t->rj_l;
	}
	/*
	 * See if we match exactly as a host destination
	 */
	KLIPS_PRINT(debug_radij,
		    "klips_debug:rj_match: "
		    "* See if we match exactly as a host destination\n");
	
	cp += off; cp2 = t->rj_key + off; cplim = v + vlen;
	for (; cp < cplim; cp++, cp2++)
		if (*cp != *cp2)
			goto on1;
	/*
	 * This extra grot is in case we are explicitly asked
	 * to look up the default.  Ugh!
	 */
	if ((t->rj_flags & RJF_ROOT) && t->rj_dupedkey)
		t = t->rj_dupedkey;
	return t;
on1:
	matched_off = cp - v;
	saved_t = t;
	KLIPS_PRINT(debug_radij,
		    "klips_debug:rj_match: "
		    "** try to match a leaf, t=0p%p\n", t);
	do {
	    if (t->rj_mask) {
		/*
		 * Even if we don't match exactly as a hosts;
		 * we may match if the leaf we wound up at is
		 * a route to a net.
		 */
		cp3 = matched_off + t->rj_mask;
		cp2 = matched_off + t->rj_key;
		for (; cp < cplim; cp++)
			if ((*cp2++ ^ *cp) & *cp3++)
				break;
		if (cp == cplim)
			return t;
		cp = matched_off + v;
	    }
	} while ((t = t->rj_dupedkey));
	t = saved_t;
	/* start searching up the tree */
	KLIPS_PRINT(debug_radij,
		    "klips_debug:rj_match: "
		    "*** start searching up the tree, t=0p%p\n",
		    t);
	do {
		register struct radij_mask *m;
		
		t = t->rj_p;
		KLIPS_PRINT(debug_radij,
			    "klips_debug:rj_match: "
			    "**** t=0p%p\n",
			    t);
		if ((m = t->rj_mklist)) {
			/*
			 * After doing measurements here, it may
			 * turn out to be faster to open code
			 * rj_search_m here instead of always
			 * copying and masking.
			 */
			/* off = min(t->rj_off, matched_off); */
			off = t->rj_off;
			if (matched_off < off)
				off = matched_off;
			mstart = maskedKey + off;
			do {
				cp2 = mstart;
				cp3 = m->rm_mask + off;
				KLIPS_PRINT(debug_radij,
					    "klips_debug:rj_match: "
					    "***** cp2=0p%p cp3=0p%p\n",
					    cp2, cp3);
				for (cp = v + off; cp < cplim;)
					*cp2++ =  *cp++ & *cp3++;
				x = rj_search(maskedKey, t);
				while (x && x->rj_mask != m->rm_mask)
					x = x->rj_dupedkey;
				if (x &&
				    (Bcmp(mstart, x->rj_key + off,
					vlen - off) == 0))
					    return x;
			} while ((m = m->rm_mklist));
		}
	} while (t != top);
	KLIPS_PRINT(debug_radij,
		    "klips_debug:rj_match: "
		    "***** not found.\n");
	return 0;
};
		
#ifdef RJ_DEBUG
int	rj_nodenum;
struct	radij_node *rj_clist;
int	rj_saveinfo;
DEBUG_NO_STATIC void traverse(struct radij_node *);
#ifdef RJ_DEBUG2
int	rj_debug =  1;
#else
int	rj_debug =  0;
#endif /* RJ_DEBUG2 */
#endif /* RJ_DEBUG */

struct radij_node *
rj_newpair(v, b, nodes)
	void *v;
	int b;
	struct radij_node nodes[2];
{
	register struct radij_node *tt = nodes, *t = tt + 1;
	t->rj_b = b; t->rj_bmask = 0x80 >> (b & 7);
	t->rj_l = tt; t->rj_off = b >> 3;
	tt->rj_b = -1; tt->rj_key = (caddr_t)v; tt->rj_p = t;
	tt->rj_flags = t->rj_flags = RJF_ACTIVE;
#ifdef RJ_DEBUG
	tt->rj_info = rj_nodenum++; t->rj_info = rj_nodenum++;
	tt->rj_twin = t; tt->rj_ybro = rj_clist; rj_clist = tt;
#endif /* RJ_DEBUG */
	return t;
}

struct radij_node *
rj_insert(v_arg, head, dupentry, nodes)
	void *v_arg;
	struct radij_node_head *head;
	int *dupentry;
	struct radij_node nodes[2];
{
	caddr_t v = v_arg;
	struct radij_node *top = head->rnh_treetop;
	int head_off = top->rj_off, vlen = (int)*((u_char *)v);
	register struct radij_node *t = rj_search(v_arg, top);
	register caddr_t cp = v + head_off;
	register int b;
	struct radij_node *tt;
    	/*
	 *find first bit at which v and t->rj_key differ
	 */
    {
	register caddr_t cp2 = t->rj_key + head_off;
	register int cmp_res;
	caddr_t cplim = v + vlen;

	while (cp < cplim)
		if (*cp2++ != *cp++)
			goto on1;
	*dupentry = 1;
	return t;
on1:
	*dupentry = 0;
	cmp_res = (cp[-1] ^ cp2[-1]) & 0xff;
	for (b = (cp - v) << 3; cmp_res; b--)
		cmp_res >>= 1;
    }
    {
	register struct radij_node *p, *x = top;
	cp = v;
	do {
		p = x;
		if (cp[x->rj_off] & x->rj_bmask) 
			x = x->rj_r;
		else x = x->rj_l;
	} while (b > (unsigned) x->rj_b); /* x->rj_b < b && x->rj_b >= 0 */
#ifdef RJ_DEBUG
	if (rj_debug)
		printk("klips_debug:rj_insert: Going In:\n"), traverse(p);
#endif /* RJ_DEBUG */
	t = rj_newpair(v_arg, b, nodes); tt = t->rj_l;
	if ((cp[p->rj_off] & p->rj_bmask) == 0)
		p->rj_l = t;
	else
		p->rj_r = t;
	x->rj_p = t; t->rj_p = p; /* frees x, p as temp vars below */
	if ((cp[t->rj_off] & t->rj_bmask) == 0) {
		t->rj_r = x;
	} else {
		t->rj_r = tt; t->rj_l = x;
	}
#ifdef RJ_DEBUG
	if (rj_debug)
		printk("klips_debug:rj_insert: Coming out:\n"), traverse(p);
#endif /* RJ_DEBUG */
    }
	return (tt);
}

struct radij_node *
rj_addmask(n_arg, search, skip)
	int search, skip;
	void *n_arg;
{
	caddr_t netmask = (caddr_t)n_arg;
	register struct radij_node *x;
	register caddr_t cp, cplim;
	register int b, mlen, j;
	int maskduplicated;

	mlen = *(u_char *)netmask;
	if (search) {
		x = rj_search(netmask, rj_masktop);
		mlen = *(u_char *)netmask;
		if (Bcmp(netmask, x->rj_key, mlen) == 0)
			return (x);
	}
	R_Malloc(x, struct radij_node *, maj_keylen + 2 * sizeof (*x));
	if (x == 0)
		return (0);
	Bzero(x, maj_keylen + 2 * sizeof (*x));
	cp = (caddr_t)(x + 2);
	Bcopy(netmask, cp, mlen);
	netmask = cp;
	x = rj_insert(netmask, mask_rjhead, &maskduplicated, x);
	/*
	 * Calculate index of mask.
	 */
	cplim = netmask + mlen;
	for (cp = netmask + skip; cp < cplim; cp++)
		if (*(u_char *)cp != 0xff)
			break;
	b = (cp - netmask) << 3;
	if (cp != cplim) {
		if (*cp != 0) {
			gotOddMasks = 1;
			for (j = 0x80; j; b++, j >>= 1)  
				if ((j & *cp) == 0)
					break;
		}
	}
	x->rj_b = -1 - b;
	return (x);
}

#if 0
struct radij_node *
#endif
int
rj_addroute(v_arg, n_arg, head, treenodes)
	void *v_arg, *n_arg;
	struct radij_node_head *head;
	struct radij_node treenodes[2];
{
	caddr_t v = (caddr_t)v_arg, netmask = (caddr_t)n_arg;
	register struct radij_node *t, *x=NULL, *tt;
	struct radij_node *saved_tt, *top = head->rnh_treetop;
	short b = 0, b_leaf;
	int mlen, keyduplicated;
	caddr_t cplim;
	struct radij_mask *m, **mp;

	/*
	 * In dealing with non-contiguous masks, there may be
	 * many different routes which have the same mask.
	 * We will find it useful to have a unique pointer to
	 * the mask to speed avoiding duplicate references at
	 * nodes and possibly save time in calculating indices.
	 */
	if (netmask)  {
		x = rj_search(netmask, rj_masktop);
		mlen = *(u_char *)netmask;
		if (Bcmp(netmask, x->rj_key, mlen) != 0) {
			x = rj_addmask(netmask, 0, top->rj_off);
			if (x == 0)
				return -ENOMEM; /* (0) rgb */
		}
		netmask = x->rj_key;
		b = -1 - x->rj_b;
	}
	/*
	 * Deal with duplicated keys: attach node to previous instance
	 */
	saved_tt = tt = rj_insert(v, head, &keyduplicated, treenodes);
#ifdef RJ_DEBUG
	printk("addkey: duplicated: %d\n", keyduplicated);
#endif
	if (keyduplicated) {
		do {
			if (tt->rj_mask == netmask)
				return -EEXIST; /* -ENXIO; (0) rgb */
			t = tt;
			if (netmask == 0 ||
			    (tt->rj_mask && rj_refines(netmask, tt->rj_mask)))
				break;
		} while ((tt = tt->rj_dupedkey));
		/*
		 * If the mask is not duplicated, we wouldn't
		 * find it among possible duplicate key entries
		 * anyway, so the above test doesn't hurt.
		 *
		 * We sort the masks for a duplicated key the same way as
		 * in a masklist -- most specific to least specific.
		 * This may require the unfortunate nuisance of relocating
		 * the head of the list.
		 */
		if (tt && t == saved_tt) {
			struct	radij_node *xx = x;
			/* link in at head of list */
			(tt = treenodes)->rj_dupedkey = t;
			tt->rj_flags = t->rj_flags;
			tt->rj_p = x = t->rj_p;
			if (x->rj_l == t) x->rj_l = tt; else x->rj_r = tt;
			saved_tt = tt; x = xx;
		} else {
			(tt = treenodes)->rj_dupedkey = t->rj_dupedkey;
			t->rj_dupedkey = tt;
		}
#ifdef RJ_DEBUG
		t=tt+1; tt->rj_info = rj_nodenum++; t->rj_info = rj_nodenum++;
		tt->rj_twin = t; tt->rj_ybro = rj_clist; rj_clist = tt;
#endif /* RJ_DEBUG */
		t = saved_tt;
		tt->rj_key = (caddr_t) v;
		tt->rj_b = -1;
		tt->rj_flags = t->rj_flags & ~RJF_ROOT;
	}
	/*
	 * Put mask in tree.
	 */
	if (netmask) {
		tt->rj_mask = netmask;
		tt->rj_b = x->rj_b;
	}
	t = saved_tt->rj_p;
	b_leaf = -1 - t->rj_b;
	if (t->rj_r == saved_tt) x = t->rj_l; else x = t->rj_r;
	/* Promote general routes from below */
	if (x->rj_b < 0) { 
		if (x->rj_mask && (x->rj_b >= b_leaf) && x->rj_mklist == 0) {
			MKGet(m);
			if (m) {
				Bzero(m, sizeof *m);
				m->rm_b = x->rj_b;
				m->rm_mask = x->rj_mask;
				x->rj_mklist = t->rj_mklist = m;
			}
		}
	} else if (x->rj_mklist) {
		/*
		 * Skip over masks whose index is > that of new node
		 */
		for (mp = &x->rj_mklist; (m = *mp); mp = &m->rm_mklist)
			if (m->rm_b >= b_leaf)
				break;
		t->rj_mklist = m; *mp = 0;
	}
	/* Add new route to highest possible ancestor's list */
	if ((netmask == 0) || (b > t->rj_b )) {
#ifdef RJ_DEBUG
	        printk("klips:radij.c: netmask = %p or b(%d)>t->rjb(%d)\n", netmask, b, t->rj_b);
#endif
		return 0; /* tt rgb */ /* can't lift at all */
	}
	b_leaf = tt->rj_b;
	do {
		x = t;
		t = t->rj_p;
	} while (b <= t->rj_b && x != top);
	/*
	 * Search through routes associated with node to
	 * insert new route according to index.
	 * For nodes of equal index, place more specific
	 * masks first.
	 */
	cplim = netmask + mlen;
	for (mp = &x->rj_mklist; (m = *mp); mp = &m->rm_mklist) {
		if (m->rm_b < b_leaf)
			continue;
		if (m->rm_b > b_leaf)
			break;
		if (m->rm_mask == netmask) {
			m->rm_refs++;
			tt->rj_mklist = m;
#ifdef RJ_DEBUG
			printk("klips:radij.c: m->rm_mask %p == netmask\n", netmask);
#endif
			return 0; /* tt rgb */
		}
		if (rj_refines(netmask, m->rm_mask))
			break;
	}
	MKGet(m);
	if (m == 0) {
		printk("klips_debug:rj_addroute: "
		       "Mask for route not entered\n");
		return 0; /* (tt) rgb */
	}
	Bzero(m, sizeof *m);
	m->rm_b = b_leaf;
	m->rm_mask = netmask;
	m->rm_mklist = *mp;
	*mp = m;
	tt->rj_mklist = m;
#ifdef RJ_DEBUG
	printk("klips:radij.c: addroute done\n");
#endif
	return 0; /* tt rgb */
}

int
rj_delete(v_arg, netmask_arg, head, node)
	void *v_arg, *netmask_arg;
	struct radij_node_head *head;
	struct radij_node **node;
{
	register struct radij_node *t, *p, *x, *tt;
	struct radij_mask *m, *saved_m, **mp;
	struct radij_node *dupedkey, *saved_tt, *top;
	caddr_t v, netmask;
	int b, head_off, vlen;

	v = v_arg;
	netmask = netmask_arg;
	x = head->rnh_treetop;
	tt = rj_search(v, x);
	head_off = x->rj_off;
	vlen =  *(u_char *)v;
	saved_tt = tt;
	top = x;
	if (tt == 0 ||
	    Bcmp(v + head_off, tt->rj_key + head_off, vlen - head_off))
		return -EFAULT; /* (0) rgb */
	/*
	 * Delete our route from mask lists.
	 */
	if ((dupedkey = tt->rj_dupedkey)) {
		if (netmask) 
			netmask = rj_search(netmask, rj_masktop)->rj_key;
		while (tt->rj_mask != netmask)
			if ((tt = tt->rj_dupedkey) == 0)
				return -ENOENT; /* -ENXIO; (0) rgb */
	}
	if (tt->rj_mask == 0 || (saved_m = m = tt->rj_mklist) == 0)
		goto on1;
	if (m->rm_mask != tt->rj_mask) {
		printk("klips_debug:rj_delete: "
		       "inconsistent annotation\n");
		goto on1;
	}
	if (--m->rm_refs >= 0)
		goto on1;
	b = -1 - tt->rj_b;
	t = saved_tt->rj_p;
	if (b > t->rj_b)
		goto on1; /* Wasn't lifted at all */
	do {
		x = t;
		t = t->rj_p;
	} while (b <= t->rj_b && x != top);
	for (mp = &x->rj_mklist; (m = *mp); mp = &m->rm_mklist)
		if (m == saved_m) {
			*mp = m->rm_mklist;
			MKFree(m);
			break;
		}
	if (m == 0)
		printk("klips_debug:rj_delete: "
		       "couldn't find our annotation\n");
on1:
	/*
	 * Eliminate us from tree
	 */
	if (tt->rj_flags & RJF_ROOT)
		return -EFAULT; /* (0) rgb */
#ifdef RJ_DEBUG
	/* Get us out of the creation list */
	for (t = rj_clist; t && t->rj_ybro != tt; t = t->rj_ybro) {}
	if (t) t->rj_ybro = tt->rj_ybro;
#endif /* RJ_DEBUG */
	t = tt->rj_p;
	if (dupedkey) {
		if (tt == saved_tt) {
			x = dupedkey; x->rj_p = t;
			if (t->rj_l == tt) t->rj_l = x; else t->rj_r = x;
		} else {
			for (x = p = saved_tt; p && p->rj_dupedkey != tt;)
				p = p->rj_dupedkey;
			if (p) p->rj_dupedkey = tt->rj_dupedkey;
			else printk("klips_debug:rj_delete: "
				       "couldn't find node that we started with\n");
		}
		t = tt + 1;
		if  (t->rj_flags & RJF_ACTIVE) {
#ifndef RJ_DEBUG
			*++x = *t; p = t->rj_p;
#else
			b = t->rj_info; *++x = *t; t->rj_info = b; p = t->rj_p;
#endif /* RJ_DEBUG */
			if (p->rj_l == t) p->rj_l = x; else p->rj_r = x;
			x->rj_l->rj_p = x; x->rj_r->rj_p = x;
		}
		goto out;
	}
	if (t->rj_l == tt) x = t->rj_r; else x = t->rj_l;
	p = t->rj_p;
	if (p->rj_r == t) p->rj_r = x; else p->rj_l = x;
	x->rj_p = p;
	/*
	 * Demote routes attached to us.
	 */
	if (t->rj_mklist) {
		if (x->rj_b >= 0) {
			for (mp = &x->rj_mklist; (m = *mp);)
				mp = &m->rm_mklist;
			*mp = t->rj_mklist;
		} else {
			for (m = t->rj_mklist; m;) {
				struct radij_mask *mm = m->rm_mklist;
				if (m == x->rj_mklist && (--(m->rm_refs) < 0)) {
					x->rj_mklist = 0;
					MKFree(m);
				} else 
					printk("klips_debug:rj_delete: "
					    "Orphaned Mask 0p%p at 0p%p\n", m, x);
				m = mm;
			}
		}
	}
	/*
	 * We may be holding an active internal node in the tree.
	 */
	x = tt + 1;
	if (t != x) {
#ifndef RJ_DEBUG
		*t = *x;
#else
		b = t->rj_info; *t = *x; t->rj_info = b;
#endif /* RJ_DEBUG */
		t->rj_l->rj_p = t; t->rj_r->rj_p = t;
		p = x->rj_p;
		if (p->rj_l == x) p->rj_l = t; else p->rj_r = t;
	}
out:
	tt->rj_flags &= ~RJF_ACTIVE;
	tt[1].rj_flags &= ~RJF_ACTIVE;
	*node = tt;
	return 0; /* (tt) rgb */
}

int
rj_walktree(h, f, w)
	struct radij_node_head *h;
	register int (*f)(struct radij_node *,void *);
	void *w;
{
	int error;
	struct radij_node *base, *next;
	register struct radij_node *rn;

	if(!h || !f /* || !w */) {
		return -ENODATA;
	}

	rn = h->rnh_treetop;
	/*
	 * This gets complicated because we may delete the node
	 * while applying the function f to it, so we need to calculate
	 * the successor node in advance.
	 */
	/* First time through node, go left */
	while (rn->rj_b >= 0)
		rn = rn->rj_l;
	for (;;) {
#ifdef CONFIG_KLIPS_DEBUG
		if(debug_radij) {
			printk("klips_debug:rj_walktree: "
			       "for: rn=0p%p rj_b=%d rj_flags=%x",
			       rn,
			       rn->rj_b,
			       rn->rj_flags);
			rn->rj_b >= 0 ?
				printk(" node off=%x\n",
				       rn->rj_off) :
				printk(" leaf key = %08x->%08x\n",
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_src.s_addr),
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_dst.s_addr))
				;
		}
#endif /* CONFIG_KLIPS_DEBUG */
		base = rn;
		/* If at right child go back up, otherwise, go right */
		while (rn->rj_p->rj_r == rn && (rn->rj_flags & RJF_ROOT) == 0)
			rn = rn->rj_p;
		/* Find the next *leaf* since next node might vanish, too */
		for (rn = rn->rj_p->rj_r; rn->rj_b >= 0;)
			rn = rn->rj_l;
		next = rn;
#ifdef CONFIG_KLIPS_DEBUG
		if(debug_radij) {
			printk("klips_debug:rj_walktree: "
			       "processing leaves, rn=0p%p rj_b=%d rj_flags=%x",
			       rn,
			       rn->rj_b,
			       rn->rj_flags);
			rn->rj_b >= 0 ?
				printk(" node off=%x\n",
				       rn->rj_off) :
				printk(" leaf key = %08x->%08x\n",
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_src.s_addr),
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_dst.s_addr))
				;
		}
#endif /* CONFIG_KLIPS_DEBUG */
		/* Process leaves */
		while ((rn = base)) {
			base = rn->rj_dupedkey;
#ifdef CONFIG_KLIPS_DEBUG
			if(debug_radij) {
				printk("klips_debug:rj_walktree: "
				       "while: base=0p%p rn=0p%p rj_b=%d rj_flags=%x",
				       base,
				       rn,
				       rn->rj_b,
				       rn->rj_flags);
				rn->rj_b >= 0 ?
					printk(" node off=%x\n",
					       rn->rj_off) :
					printk(" leaf key = %08x->%08x\n",
					       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_src.s_addr),
					       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_dst.s_addr))
					;
			}
#endif /* CONFIG_KLIPS_DEBUG */
			if (!(rn->rj_flags & RJF_ROOT) && (error = (*f)(rn, w)))
				return (-error);
		}
		rn = next;
		if (rn->rj_flags & RJF_ROOT)
			return (0);
	}
	/* NOTREACHED */
}

int
rj_inithead(head, off)
	void **head;
	int off;
{
	register struct radij_node_head *rnh;
	register struct radij_node *t, *tt, *ttt;
	if (*head)
		return (1);
	R_Malloc(rnh, struct radij_node_head *, sizeof (*rnh));
	if (rnh == NULL)
		return (0);
	Bzero(rnh, sizeof (*rnh));
	*head = rnh;
	t = rj_newpair(rj_zeroes, off, rnh->rnh_nodes);
	ttt = rnh->rnh_nodes + 2;
	t->rj_r = ttt;
	t->rj_p = t;
	tt = t->rj_l;
	tt->rj_flags = t->rj_flags = RJF_ROOT | RJF_ACTIVE;
	tt->rj_b = -1 - off;
	*ttt = *tt;
	ttt->rj_key = rj_ones;
	rnh->rnh_addaddr = rj_addroute;
	rnh->rnh_deladdr = rj_delete;
	rnh->rnh_matchaddr = rj_match;
	rnh->rnh_walktree = rj_walktree;
	rnh->rnh_treetop = t;
	return (1);
}

void
rj_init()
{
	char *cp, *cplim;

	if (maj_keylen == 0) {
		printk("klips_debug:rj_init: "
		       "radij functions require maj_keylen be set\n");
		return;
	}
	R_Malloc(rj_zeroes, char *, 3 * maj_keylen);
	if (rj_zeroes == NULL)
		panic("rj_init");
	Bzero(rj_zeroes, 3 * maj_keylen);
	rj_ones = cp = rj_zeroes + maj_keylen;
	maskedKey = cplim = rj_ones + maj_keylen;
	while (cp < cplim)
		*cp++ = -1;
	if (rj_inithead((void **)&mask_rjhead, 0) == 0)
		panic("rj_init 2");
}

void
rj_preorder(struct radij_node *rn, int l)
{
	int i;
	
	if (rn == NULL){
		printk("klips_debug:rj_preorder: "
		       "NULL pointer\n");
		return;
	}
	
	if (rn->rj_b >= 0){
		rj_preorder(rn->rj_l, l+1);
		rj_preorder(rn->rj_r, l+1);
		printk("klips_debug:");
		for (i=0; i<l; i++)
			printk("*");
		printk(" off = %d\n",
		       rn->rj_off);
	} else {
		printk("klips_debug:");
		for (i=0; i<l; i++)
			printk("@");
		printk(" flags = %x",
		       (u_int)rn->rj_flags);
		if (rn->rj_flags & RJF_ACTIVE) {
			printk(" @key=0p%p",
			       rn->rj_key);
			printk(" key = %08x->%08x",
			       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_src.s_addr),
			       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_key)->sen_ip_dst.s_addr));
			printk(" @mask=0p%p",
			       rn->rj_mask);
			if (rn->rj_mask)
				printk(" mask = %08x->%08x",
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_mask)->sen_ip_src.s_addr),
				       (u_int)ntohl(((struct sockaddr_encap *)rn->rj_mask)->sen_ip_dst.s_addr));
			if (rn->rj_dupedkey)
				printk(" dupedkey = 0p%p",
				       rn->rj_dupedkey);
		}
		printk("\n");
	}
}

#ifdef RJ_DEBUG
DEBUG_NO_STATIC void traverse(struct radij_node *p)
{
  rj_preorder(p, 0);
}
#endif /* RJ_DEBUG */

void
rj_dumptrees(void)
{
	rj_preorder(rnh->rnh_treetop, 0);
}

void
rj_free_mkfreelist(void)
{
	struct radij_mask *mknp, *mknp2;

	mknp = rj_mkfreelist;
	while(mknp)
	{
		mknp2 = mknp;
		mknp = mknp->rm_mklist;
		kfree(mknp2);
	}
}

int
radijcleartree(void)
{
	return rj_walktree(rnh, ipsec_rj_walker_delete, NULL);
}

int
radijcleanup(void)
{
	int error = 0;

	error = radijcleartree();

	rj_free_mkfreelist();

/*	rj_walktree(mask_rjhead, ipsec_rj_walker_delete, NULL); */
  	if(mask_rjhead) {
		kfree(mask_rjhead);
	}

	if(rj_zeroes) {
		kfree(rj_zeroes);
	}

	if(rnh) {
		kfree(rnh);
	}

	return error;
}

/*
 * $Log: radij.c,v $
 * Revision 1.48  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.47  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.46  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.45  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.44.30.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.44  2002/07/24 18:44:54  rgb
 * Type fiddling to tame ia64 compiler.
 *
 * Revision 1.43  2002/05/23 07:14:11  rgb
 * Cleaned up %p variants to 0p%p for test suite cleanup.
 *
 * Revision 1.42  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.41  2002/04/24 07:36:35  mcr
 * Moved from ./klips/net/ipsec/radij.c,v
 *
 * Revision 1.40  2002/01/29 17:17:58  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.39  2002/01/29 04:00:55  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.38  2002/01/29 02:13:19  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.37  2001/10/18 04:45:23  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.36  2001/08/22 13:43:51  henry
 * eliminate the single use of min() to avoid problems with Linus changing it
 *
 * Revision 1.35  2001/06/15 04:57:29  rgb
 * Clarified error return codes.
 * Changed mask add already exists to EEXIST.
 * Changed mask delete did not exist to ENOENT.
 *
 * Revision 1.34  2001/05/03 19:44:26  rgb
 * Fix sign of error return codes for rj_addroute().
 *
 * Revision 1.33  2001/02/27 22:24:56  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.32  2001/02/27 06:23:15  rgb
 * Debug line splitting.
 *
 * Revision 1.31  2000/11/06 04:35:21  rgb
 * Clear table *before* releasing other items in radijcleanup.
 *
 * Revision 1.30  2000/09/20 04:07:40  rgb
 * Changed static functions to DEBUG_NO_STATIC to reveal function names in
 * oopsen.
 *
 * Revision 1.29  2000/09/12 03:25:02  rgb
 * Moved radij_c_version printing to ipsec_version_get_info().
 *
 * Revision 1.28  2000/09/08 19:12:56  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 *
 * Revision 1.27  2000/07/28 14:58:32  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.26  2000/05/10 23:11:37  rgb
 * Comment out most of the startup version information.
 *
 * Revision 1.25  2000/01/21 06:21:47  rgb
 * Change return codes to negative on error.
 *
 * Revision 1.24  1999/11/18 04:09:20  rgb
 * Replaced all kernel version macros to shorter, readable form.
 *
 * Revision 1.23  1999/11/17 15:53:41  rgb
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 *
 * Revision 1.22  1999/10/15 22:17:28  rgb
 * Modify radijcleanup() to call radijcleartree().
 *
 * Revision 1.21  1999/10/08 18:37:34  rgb
 * Fix end-of-line spacing to sate whining PHMs.
 *
 * Revision 1.20  1999/10/01 15:44:54  rgb
 * Move spinlock header include to 2.1> scope.
 *
 * Revision 1.19  1999/10/01 08:35:52  rgb
 * Add spinlock include to shut up compiler for 2.0.38.
 *
 * Revision 1.18  1999/09/23 18:02:52  rgb
 * De-alarm the search failure message so it doesn't sound so grave.
 *
 * Revision 1.17  1999/05/25 21:26:01  rgb
 * Fix rj_walktree() sanity checking bug.
 *
 * Revision 1.16  1999/05/09 03:25:38  rgb
 * Fix bug introduced by 2.2 quick-and-dirty patch.
 *
 * Revision 1.15  1999/05/05 22:02:33  rgb
 * Add a quick and dirty port to 2.2 kernels by Marc Boucher <marc@mbsi.ca>.
 *
 * Revision 1.14  1999/04/29 15:24:15  rgb
 * Add sanity checking for null pointer arguments.
 * Standardise an error return method.
 *
 * Revision 1.13  1999/04/11 00:29:02  henry
 * GPL boilerplate
 *
 * Revision 1.12  1999/04/06 04:54:28  rgb
 * Fix/Add RCSID Id: and Log: bits to make PHMDs happy.  This includes
 * patch shell fixes.
 *
 * Revision 1.11  1999/02/17 16:52:53  rgb
 * Convert DEBUG_IPSEC to KLIPS_PRINT
 * Clean out unused cruft.
 *
 * Revision 1.10  1999/01/22 06:30:05  rgb
 * Cruft clean-out.
 * 64-bit clean-up.
 *
 * Revision 1.9  1998/12/01 13:22:04  rgb
 * Added support for debug printing of version info.
 *
 * Revision 1.8  1998/11/30 13:22:55  rgb
 * Rationalised all the klips kernel file headers.  They are much shorter
 * now and won't conflict under RH5.2.
 *
 * Revision 1.7  1998/10/25 02:43:26  rgb
 * Change return type on rj_addroute and rj_delete and add and argument
 * to the latter to be able to transmit more infomation about errors.
 *
 * Revision 1.6  1998/10/19 14:30:06  rgb
 * Added inclusion of freeswan.h.
 *
 * Revision 1.5  1998/10/09 04:33:27  rgb
 * Added 'klips_debug' prefix to all klips printk debug statements.
 * Fixed output formatting slightly.
 *
 * Revision 1.4  1998/07/28 00:06:59  rgb
 * Add debug detail to tree traversing.
 *
 * Revision 1.3  1998/07/14 18:07:58  rgb
 * Add a routine to clear the eroute tree.
 *
 * Revision 1.2  1998/06/25 20:03:22  rgb
 * Cleanup #endif comments.  Debug output for rj_init.
 *
 * Revision 1.1  1998/06/18 21:30:22  henry
 * move sources from klips/src to klips/net/ipsec to keep stupid kernel
 * build scripts happier about symlinks
 *
 * Revision 1.8  1998/05/25 20:34:15  rgb
 * Remove temporary ipsec_walk, rj_deltree and rj_delnodes functions.
 *
 * Rename ipsec_rj_walker (ipsec_walk) to ipsec_rj_walker_procprint and
 * add ipsec_rj_walker_delete.
 *
 * Recover memory for eroute table on unload of module.
 *
 * Revision 1.7  1998/05/21 12:58:58  rgb
 * Moved 'extern' definitions to ipsec_radij.h to support /proc 3k limit fix.
 *
 * Revision 1.6  1998/04/23 20:57:29  rgb
 * Cleaned up compiler warnings for unused debugging functions.
 *
 * Revision 1.5  1998/04/22 16:51:38  rgb
 * Tidy up radij debug code from recent rash of modifications to debug code.
 *
 * Revision 1.4  1998/04/21 21:28:56  rgb
 * Rearrange debug switches to change on the fly debug output from user
 * space.  Only kernel changes checked in at this time.  radij.c was also
 * changed to temporarily remove buggy debugging code in rj_delete causing
 * an OOPS and hence, netlink device open errors.
 *
 * Revision 1.3  1998/04/14 17:30:37  rgb
 * Fix up compiling errors for radij tree memory reclamation.
 *
 * Revision 1.2  1998/04/12 22:03:25  rgb
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
 * Revision 1.1  1998/04/09 03:06:15  henry
 * sources moved up from linux/net/ipsec
 *
 * Revision 1.1.1.1  1998/04/08 05:35:03  henry
 * RGB's ipsec-0.8pre2.tar.gz ipsec-0.8
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
 *
 */
