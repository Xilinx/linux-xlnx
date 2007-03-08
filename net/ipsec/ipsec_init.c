/*
 * @(#) Initialization code.
 * Copyright (C) 1996, 1997   John Ioannidis.
 * Copyright (C) 1998 - 2002  Richard Guy Briggs <rgb@freeswan.org>
 *               2001 - 2004  Michael Richardson <mcr@xelerance.com>
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
 * /proc system code was split out into ipsec_proc.c after rev. 1.70.
 *
 */

char ipsec_init_c_version[] = "RCSID $Id: ipsec_init.c,v 1.104.2.3 2006/07/31 15:25:20 paul Exp $";

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/version.h>
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

#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/in.h>          /* struct sockaddr_in */
#include <linux/skbuff.h>
#include <linux/random.h>       /* get_random_bytes() */
#include <net/protocol.h>

#include <openswan.h>

#ifdef SPINLOCK
# ifdef SPINLOCK_23
#  include <linux/spinlock.h> /* *lock* */
# else /* 23_SPINLOCK */
#  include <asm/spinlock.h> /* *lock* */
# endif /* 23_SPINLOCK */
#endif /* SPINLOCK */

#include <net/ip.h>

#ifdef CONFIG_PROC_FS
# include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */

#ifdef NETLINK_SOCK
# include <linux/netlink.h>
#else
# include <net/netlink.h>
#endif

#include "openswan/radij.h"

#include "openswan/ipsec_life.h"
#include "openswan/ipsec_stats.h"
#include "openswan/ipsec_sa.h"

#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_radij.h"
#include "openswan/ipsec_xform.h"
#include "openswan/ipsec_tunnel.h"

#include "openswan/ipsec_rcv.h"
#include "openswan/ipsec_xmit.h"
#include "openswan/ipsec_ah.h"
#include "openswan/ipsec_esp.h"

#ifdef CONFIG_KLIPS_IPCOMP
# include "openswan/ipcomp.h"
#endif /* CONFIG_KLIPS_IPCOMP */

#include "openswan/ipsec_proto.h"
#include "openswan/ipsec_alg.h"

#ifdef CONFIG_KLIPS_OCF
#include "ipsec_ocf.h"
#endif

#include <pfkeyv2.h>
#include <pfkey.h>

#if defined(NET_26) && defined(CONFIG_IPSEC_NAT_TRAVERSAL)
#include <net/xfrmudp.h>
#endif

#if defined(NET_26) && defined(CONFIG_IPSEC_NAT_TRAVERSAL) && !defined(HAVE_XFRM4_UDP_REGISTER)
#warning "You are trying to build KLIPS2.6 with NAT-T support, but you did not"
#error   "properly apply the NAT-T patch to your 2.6 kernel source tree."
#endif

#if !defined(CONFIG_KLIPS_ESP) && !defined(CONFIG_KLIPS_AH)
#error "kernel configuration must include ESP or AH"
#endif

/*
 * seems to be present in 2.4.10 (Linus), but also in some RH and other
 * distro kernels of a lower number.
 */
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

#ifdef CONFIG_KLIPS_DEBUG
int debug_eroute = 0;
int debug_spi = 0;
int debug_netlink = 0;
#endif /* CONFIG_KLIPS_DEBUG */

/*
 * We limit the number of outstanding RX/TX requests.
 * Because we are now async,  we cannot just keep allocating
 * these as fast as the come in,  crypto is usually much slower
 * than your network interface
 */
kmem_cache_t *ipsec_irs_cache;
kmem_cache_t *ipsec_ixs_cache;

#if !defined(MODULE_PARM) && defined(module_param)
/*
 * As of 2.6.17 MODULE_PARM no longer exists, use module_param instead.
 */
#define	MODULE_PARM(a,b)	module_param(a,int,0)
#endif

atomic_t ipsec_irs_cnt;
int ipsec_irs_max = 1000;
MODULE_PARM(ipsec_irs_max,"i");
MODULE_PARM_DESC(ipsec_irs_max, "Maximum outstanding receive packets");

atomic_t ipsec_ixs_cnt;
int ipsec_ixs_max = 1000;
MODULE_PARM(ipsec_ixs_max,"i");
MODULE_PARM_DESC(ipsec_ixs_max, "Maximum outstanding transmit packets");

struct prng ipsec_prng;


#if defined(NET_26) && defined(CONFIG_IPSEC_NAT_TRAVERSAL)
xfrm4_rcv_encap_t klips_old_encap = NULL;
#endif

extern int ipsec_device_event(struct notifier_block *dnot, unsigned long event, void *ptr);
/*
 * the following structure is required so that we receive
 * event notifications when network devices are enabled and
 * disabled (ifconfig up and down).
 */
static struct notifier_block ipsec_dev_notifier={
	ipsec_device_event,
	NULL,
	0
};

#ifdef CONFIG_SYSCTL
extern int ipsec_sysctl_register(void);
extern void ipsec_sysctl_unregister(void);
#endif

#if defined(NET_26) || defined(IPSKB_XFRM_TUNNEL_SIZE)
static inline int
openswan_inet_add_protocol(struct inet_protocol *prot, unsigned protocol)
{
	return inet_add_protocol(prot, protocol);
}

static inline int
openswan_inet_del_protocol(struct inet_protocol *prot, unsigned protocol)
{
	return inet_del_protocol(prot, protocol);
}

#else
static inline int
openswan_inet_add_protocol(struct inet_protocol *prot, unsigned protocol)
{
	inet_add_protocol(prot);
	return 0;
}

static inline int
openswan_inet_del_protocol(struct inet_protocol *prot, unsigned protocol)
{
	inet_del_protocol(prot);
	return 0;
}

#endif

/* void */
int
ipsec_klips_init(void)
{
	int error = 0;
	unsigned char seed[256];
#ifdef CONFIG_KLIPS_ENC_3DES
	extern int des_check_key;

	/* turn off checking of keys */
	des_check_key=0;
#endif /* CONFIG_KLIPS_ENC_3DES */

	KLIPS_PRINT(1, "klips_info:ipsec_init: "
		    "KLIPS startup, Openswan KLIPS IPsec stack version: %s\n",
		    ipsec_version_code());

	error |= ipsec_proc_init();

#ifdef SPINLOCK
	ipsec_sadb.sadb_lock = SPIN_LOCK_UNLOCKED;
#else /* SPINLOCK */
	ipsec_sadb.sadb_lock = 0;
#endif /* SPINLOCK */

#ifndef SPINLOCK
	tdb_lock.lock = 0;
	eroute_lock.lock = 0;
#endif /* !SPINLOCK */

	error |= ipsec_sadb_init();
	error |= ipsec_radijinit();

	error |= pfkey_init();

	error |= register_netdevice_notifier(&ipsec_dev_notifier);

#ifdef CONFIG_KLIPS_ESP
	openswan_inet_add_protocol(&esp_protocol, IPPROTO_ESP);
#endif /* CONFIG_KLIPS_ESP */

#ifdef CONFIG_KLIPS_AH
	openswan_inet_add_protocol(&ah_protocol, IPPROTO_AH);
#endif /* CONFIG_KLIPS_AH */

/* we never actually link IPCOMP to the stack */
#ifdef IPCOMP_USED_ALONE
#ifdef CONFIG_KLIPS_IPCOMP
 	openswan_inet_add_protocol(&comp_protocol, IPPROTO_COMP);
#endif /* CONFIG_KLIPS_IPCOMP */
#endif

	error |= ipsec_tunnel_init_devices();

#if defined(NET_26) && defined(CONFIG_IPSEC_NAT_TRAVERSAL)
	/* register our ESP-UDP handler */
	if(udp4_register_esp_rcvencap(klips26_rcv_encap
				      , &klips_old_encap)!=0) {
	   printk(KERN_ERR "KLIPS: can not register klips_rcv_encap function\n");
	}
#endif	


#ifdef CONFIG_SYSCTL
        error |= ipsec_sysctl_register();
#endif                                                                          

#ifdef CONFIG_KLIPS_ALG
	ipsec_alg_init();
#endif

#ifdef CONFIG_KLIPS_OCF
	ipsec_ocf_init();
#endif

	get_random_bytes((void *)seed, sizeof(seed));
	prng_init(&ipsec_prng, seed, sizeof(seed));

	atomic_set(&ipsec_irs_cnt, 0);
	atomic_set(&ipsec_ixs_cnt, 0);

	ipsec_irs_cache = kmem_cache_create("ipsec_irs",
			sizeof(struct ipsec_rcv_state), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ipsec_irs_cache) {
		printk("Failed to get IRS cache\n");
		error |= 1;
	}
	ipsec_ixs_cache = kmem_cache_create("ipsec_ixs",
			sizeof(struct ipsec_xmit_state), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ipsec_ixs_cache) {
		printk("Failed to get IXS cache\n");
		error |= 1;
	}
	return error;
}	


/* void */
int
ipsec_cleanup(void)
{
	int error = 0;

#ifdef CONFIG_SYSCTL
        ipsec_sysctl_unregister();
#endif                                                                          
#if defined(NET_26) && defined(CONFIG_IPSEC_NAT_TRAVERSAL)
	if(udp4_unregister_esp_rcvencap(klips_old_encap) < 0) {
		printk(KERN_ERR "KLIPS: can not unregister klips_rcv_encap function\n");
	}
#endif

	KLIPS_PRINT(debug_netlink, /* debug_tunnel & DB_TN_INIT, */
		    "klips_debug:ipsec_cleanup: "
		    "calling ipsec_tunnel_cleanup_devices.\n");
	error |= ipsec_tunnel_cleanup_devices();

	KLIPS_PRINT(debug_netlink, "called ipsec_tunnel_cleanup_devices");

/* we never actually link IPCOMP to the stack */
#ifdef IPCOMP_USED_ALONE
#ifdef CONFIG_KLIPS_IPCOMP
 	if (openswan_inet_del_protocol(&comp_protocol, IPPROTO_COMP) < 0)
		printk(KERN_INFO "klips_debug:ipsec_cleanup: "
		       "comp close: can't remove protocol\n");
#endif /* CONFIG_KLIPS_IPCOMP */
#endif /* IPCOMP_USED_ALONE */

#ifdef CONFIG_KLIPS_AH
 	if (openswan_inet_del_protocol(&ah_protocol, IPPROTO_AH) < 0)
		printk(KERN_INFO "klips_debug:ipsec_cleanup: "
		       "ah close: can't remove protocol\n");
#endif /* CONFIG_KLIPS_AH */

#ifdef CONFIG_KLIPS_ESP
 	if (openswan_inet_del_protocol(&esp_protocol, IPPROTO_ESP) < 0)
		printk(KERN_INFO "klips_debug:ipsec_cleanup: "
		       "esp close: can't remove protocol\n");
#endif /* CONFIG_KLIPS_ESP */

	error |= unregister_netdevice_notifier(&ipsec_dev_notifier);

	KLIPS_PRINT(debug_netlink, /* debug_tunnel & DB_TN_INIT, */
		    "klips_debug:ipsec_cleanup: "
		    "calling ipsec_sadb_cleanup.\n");
	error |= ipsec_sadb_cleanup(0);
	error |= ipsec_sadb_free();

	KLIPS_PRINT(debug_netlink, /* debug_tunnel & DB_TN_INIT, */
		    "klips_debug:ipsec_cleanup: "
		    "calling ipsec_radijcleanup.\n");
	error |= ipsec_radijcleanup();
	
	KLIPS_PRINT(debug_pfkey, /* debug_tunnel & DB_TN_INIT, */
		    "klips_debug:ipsec_cleanup: "
		    "calling pfkey_cleanup.\n");
	error |= pfkey_cleanup();

	ipsec_proc_cleanup();

	prng_final(&ipsec_prng);

	if (ipsec_irs_cache)
		kmem_cache_destroy(ipsec_irs_cache);
	ipsec_irs_cache = NULL;
	if (ipsec_ixs_cache)
		kmem_cache_destroy(ipsec_ixs_cache);
	ipsec_ixs_cache = NULL;

	return error;
}

#ifdef MODULE
int
init_module(void)
{
	int error = 0;

	error |= ipsec_klips_init();

	return error;
}

void
cleanup_module(void)
{
	KLIPS_PRINT(debug_netlink, /* debug_tunnel & DB_TN_INIT, */
		    "klips_debug:cleanup_module: "
		    "calling ipsec_cleanup.\n");

	ipsec_cleanup();

	KLIPS_PRINT(1, "klips_info:cleanup_module: "
		    "ipsec module unloaded.\n");
}
#endif /* MODULE */

/*
 * $Log: ipsec_init.c,v $
 * Revision 1.104.2.3  2006/07/31 15:25:20  paul
 * Check for NETKEY backport in Debian using IPSKB_XFRM_TUNNEL_SIZE to
 * determine wether inet_add_protocol needs the protocol argument.
 *
 * Revision 1.104.2.2  2006/04/20 16:33:06  mcr
 * remove all of CONFIG_KLIPS_ALG --- one can no longer build without it.
 * Fix in-kernel module compilation. Sub-makefiles do not work.
 *
 * Revision 1.104.2.1  2005/08/12 01:18:20  ken
 * Warn people who don't have NAT-T patch applied, but try and compile NAT-T code
 *
 * Revision 1.105  2005/08/12 00:56:33  mcr
 * 	add warning for people who didn't apply nat-t patch.
 *
 * Revision 1.104  2005/07/08 15:51:41  mcr
 * 	removed duplicate NAT-T code.
 * 	if CONFIG_IPSEC_NAT_TRAVERSAL isn't defined, then there is no issue.
 *
 * Revision 1.103  2005/07/08 03:02:05  paul
 * Fixed garbled define that accidentally got commited to the real tree.
 *
 * Revision 1.102  2005/07/08 02:56:37  paul
 * gcc4 fixes that were not commited because vault was down
 *
 * Revision 1.101  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.100  2005/04/10 22:56:09  mcr
 * 	change to udp.c registration API.
 *
 * Revision 1.99  2005/04/08 18:26:13  mcr
 * 	register with udp.c, the klips26 encap receive function
 *
 * Revision 1.98  2004/09/13 02:23:18  mcr
 * 	#define inet_protocol if necessary.
 *
 * Revision 1.97  2004/09/06 18:35:49  mcr
 * 	2.6.8.1 gets rid of inet_protocol->net_protocol compatibility,
 * 	so adjust for that.
 *
 * Revision 1.96  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
 *
 * Revision 1.95  2004/08/03 18:19:08  mcr
 * 	in 2.6, use "net_device" instead of #define device->net_device.
 * 	this probably breaks 2.0 compiles.
 *
 * Revision 1.94  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.93  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.92  2004/03/30 15:30:39  ken
 * Proper Capitalization
 *
 * Revision 1.91  2004/03/22 01:51:51  ken
 * We are open
 *
 * Revision 1.90.4.2  2004/04/05 04:30:46  mcr
 * 	patches for alg-branch to compile/work with 2.x openswan
 *
 * Revision 1.90.4.1  2003/12/22 15:25:52  jjo
 *      Merged algo-0.8.1-rc11-test1 into alg-branch
 *
 * Revision 1.90  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.89.4.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.89  2003/07/31 22:47:16  mcr
 * 	preliminary (untested by FS-team) 2.5 patches.
 *
 * Revision 1.88  2003/06/22 20:05:36  mcr
 * 	clarified why IPCOMP was not being registered, and put a new
 * 	#ifdef in rather than #if 0.
 *
 * Revision 1.87  2002/09/20 15:40:51  rgb
 * Added a lock to the global ipsec_sadb struct for future use.
 * Split ipsec_sadb_cleanup from new funciton ipsec_sadb_free to avoid problem
 * of freeing newly created structures when clearing the reftable upon startup
 * to start from a known state.
 *
 * Revision 1.86  2002/08/15 18:39:15  rgb
 * Move ipsec_prng outside debug code.
 *
 * Revision 1.85  2002/05/14 02:35:29  rgb
 * Change reference to tdb to ipsa.
 *
 * Revision 1.84  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.83  2002/04/24 07:36:28  mcr
 * Moved from ./klips/net/ipsec/ipsec_init.c,v
 *
 * Revision 1.82  2002/04/20 00:12:25  rgb
 * Added esp IV CBC attack fix, disabled.
 *
 * Revision 1.81  2002/04/09 16:13:32  mcr
 * 	switch license to straight GPL.
 *
 * Revision 1.80  2002/03/24 07:34:08  rgb
 * Sanity check for at least one of AH or ESP configured.
 *
 * Revision 1.79  2002/02/05 22:55:15  mcr
 * 	added MODULE_LICENSE declaration.
 * 	This macro does not appear in all kernel versions (see comment).
 *
 * Revision 1.78  2002/01/29 17:17:55  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.77  2002/01/29 04:00:51  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.76  2002/01/29 02:13:17  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.75  2001/11/26 09:23:48  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.74  2001/11/22 05:44:11  henry
 * new version stuff
 *
 * Revision 1.71.2.2  2001/10/22 20:51:00  mcr
 * 	explicitely set des_check_key.
 *
 * Revision 1.71.2.1  2001/09/25 02:19:39  mcr
 * 	/proc manipulation code moved to new ipsec_proc.c
 *
 * Revision 1.73  2001/11/06 19:47:17  rgb
 * Changed lifetime_packets to uint32 from uint64.
 *
 * Revision 1.72  2001/10/18 04:45:19  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.71  2001/09/20 15:32:45  rgb
 * Minor pfkey lifetime fixes.
 *
 * Revision 1.70  2001/07/06 19:51:21  rgb
 * Added inbound policy checking code for IPIP SAs.
 *
 * Revision 1.69  2001/06/14 19:33:26  rgb
 * Silence startup message for console, but allow it to be logged.
 * Update copyright date.
 *
 * Revision 1.68  2001/05/29 05:14:36  rgb
 * Added PMTU to /proc/net/ipsec_tncfg output.  See 'man 5 ipsec_tncfg'.
 *
 * Revision 1.67  2001/05/04 16:34:52  rgb
 * Rremove erroneous checking of return codes for proc_net_* in 2.4.
 *
 * Revision 1.66  2001/05/03 19:40:34  rgb
 * Check error return codes in startup and shutdown.
 *
 * Revision 1.65  2001/02/28 05:03:27  rgb
 * Clean up and rationalise startup messages.
 *
 * Revision 1.64  2001/02/27 22:24:53  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.63  2000/11/29 20:14:06  rgb
 * Add src= to the output of /proc/net/ipsec_spi and delete dst from IPIP.
 *
 * Revision 1.62  2000/11/06 04:31:24  rgb
 * Ditched spin_lock_irqsave in favour of spin_lock_bh.
 * Fixed longlong for pre-2.4 kernels (Svenning).
 * Add Svenning's adaptive content compression.
 * Disabled registration of ipcomp handler.
 *
 * Revision 1.61  2000/10/11 13:37:54  rgb
 * #ifdef out debug print that causes proc/net/ipsec_version to oops.
 *
 * Revision 1.60  2000/09/20 03:59:01  rgb
 * Change static info functions to DEBUG_NO_STATIC to reveal function names
 * in oopsen.
 *
 * Revision 1.59  2000/09/16 01:06:26  rgb
 * Added cast of var to silence compiler warning about long fed to int
 * format.
 *
 * Revision 1.58  2000/09/15 11:37:01  rgb
 * Merge in heavily modified Svenning Soerensen's <svenning@post5.tele.dk>
 * IPCOMP zlib deflate code.
 *
 * Revision 1.57  2000/09/12 03:21:50  rgb
 * Moved radij_c_version printing to ipsec_version_get_info().
 * Reformatted ipsec_version_get_info().
 * Added sysctl_{,un}register() calls.
 *
 * Revision 1.56  2000/09/08 19:16:50  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Removed all references to CONFIG_IPSEC_PFKEYv2.
 *
 * Revision 1.55  2000/08/30 05:19:03  rgb
 * Cleaned up no longer used spi_next, netlink register/unregister, other
 * minor cleanup.
 * Removed cruft replaced by TDB_XFORM_NAME.
 * Removed all the rest of the references to tdb_spi, tdb_proto, tdb_dst.
 * Moved debug version strings to printk when /proc/net/ipsec_version is
 * called.
 *
 * Revision 1.54  2000/08/20 18:31:05  rgb
 * Changed cosmetic alignment in spi_info.
 * Changed addtime and usetime to use actual value which is relative
 * anyways, as intended. (Momchil)
 *
 * Revision 1.53  2000/08/18 17:37:03  rgb
 * Added an (int) cast to shut up the compiler...
 *
 * Revision 1.52  2000/08/01 14:51:50  rgb
 * Removed _all_ remaining traces of DES.
 *
 * Revision 1.51  2000/07/25 20:41:22  rgb
 * Removed duplicate parameter in spi_getinfo.
 *
 * Revision 1.50  2000/07/17 03:21:45  rgb
 * Removed /proc/net/ipsec_spinew.
 *
 * Revision 1.49  2000/06/28 05:46:51  rgb
 * Renamed ivlen to iv_bits for consistency.
 * Changed output of add and use times to be relative to now.
 *
 * Revision 1.48  2000/05/11 18:26:10  rgb
 * Commented out calls to netlink_attach/detach to avoid activating netlink
 * in the kenrel config.
 *
 * Revision 1.47  2000/05/10 22:35:26  rgb
 * Comment out most of the startup version information.
 *
 * Revision 1.46  2000/03/22 16:15:36  rgb
 * Fixed renaming of dev_get (MB).
 *
 * Revision 1.45  2000/03/16 06:40:48  rgb
 * Hardcode PF_KEYv2 support.
 *
 * Revision 1.44  2000/01/22 23:19:20  rgb
 * Simplified code to use existing macro TDB_XFORM_NAME().
 *
 * Revision 1.43  2000/01/21 06:14:04  rgb
 * Print individual stats only if non-zero.
 * Removed 'bits' from each keylength for brevity.
 * Shortened lifetimes legend for brevity.
 * Changed wording from 'last_used' to the clearer 'idle'.
 *
 * Revision 1.42  1999/12/31 14:57:19  rgb
 * MB fix for new dummy-less proc_get_info in 2.3.35.
 *
 *
 * Local variables:
 * c-file-style: "linux"
 * End:
 *
 */
