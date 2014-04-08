/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 */

#ifndef _LINUX_BONDING_H
#define _LINUX_BONDING_H

#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/if_bonding.h>
#include <linux/cpumask.h>
#include <linux/in6.h>
#include <linux/netpoll.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include "bond_3ad.h"
#include "bond_alb.h"

#define DRV_VERSION	"3.7.1"
#define DRV_RELDATE	"April 27, 2011"
#define DRV_NAME	"bonding"
#define DRV_DESCRIPTION	"Ethernet Channel Bonding Driver"

#define bond_version DRV_DESCRIPTION ": v" DRV_VERSION " (" DRV_RELDATE ")\n"

#define BOND_MAX_ARP_TARGETS	16

#define BOND_DEFAULT_MIIMON	100

#define IS_UP(dev)					   \
	      ((((dev)->flags & IFF_UP) == IFF_UP)	&& \
	       netif_running(dev)			&& \
	       netif_carrier_ok(dev))

/*
 * Checks whether slave is ready for transmit.
 */
#define SLAVE_IS_OK(slave)			        \
		    (((slave)->dev->flags & IFF_UP)  && \
		     netif_running((slave)->dev)     && \
		     ((slave)->link == BOND_LINK_UP) && \
		     bond_is_active_slave(slave))


#define USES_PRIMARY(mode)				\
		(((mode) == BOND_MODE_ACTIVEBACKUP) ||	\
		 ((mode) == BOND_MODE_TLB)          ||	\
		 ((mode) == BOND_MODE_ALB))

#define BOND_NO_USES_ARP(mode)				\
		(((mode) == BOND_MODE_8023AD)	||	\
		 ((mode) == BOND_MODE_TLB)	||	\
		 ((mode) == BOND_MODE_ALB))

#define TX_QUEUE_OVERRIDE(mode)				\
			(((mode) == BOND_MODE_ACTIVEBACKUP) ||	\
			 ((mode) == BOND_MODE_ROUNDROBIN))

#define BOND_MODE_IS_LB(mode)			\
		(((mode) == BOND_MODE_TLB) ||	\
		 ((mode) == BOND_MODE_ALB))

#define IS_IP_TARGET_UNUSABLE_ADDRESS(a)	\
	((htonl(INADDR_BROADCAST) == a) ||	\
	 ipv4_is_zeronet(a))
/*
 * Less bad way to call ioctl from within the kernel; this needs to be
 * done some other way to get the call out of interrupt context.
 * Needs "ioctl" variable to be supplied by calling context.
 */
#define IOCTL(dev, arg, cmd) ({		\
	int res = 0;			\
	mm_segment_t fs = get_fs();	\
	set_fs(get_ds());		\
	res = ioctl(dev, arg, cmd);	\
	set_fs(fs);			\
	res; })

/* slave list primitives */
#define bond_slave_list(bond) (&(bond)->dev->adj_list.lower)

#define bond_has_slaves(bond) !list_empty(bond_slave_list(bond))

/* IMPORTANT: bond_first/last_slave can return NULL in case of an empty list */
#define bond_first_slave(bond) \
	(bond_has_slaves(bond) ? \
		netdev_adjacent_get_private(bond_slave_list(bond)->next) : \
		NULL)
#define bond_last_slave(bond) \
	(bond_has_slaves(bond) ? \
		netdev_adjacent_get_private(bond_slave_list(bond)->prev) : \
		NULL)

#define bond_is_first_slave(bond, pos) (pos == bond_first_slave(bond))
#define bond_is_last_slave(bond, pos) (pos == bond_last_slave(bond))

/**
 * bond_for_each_slave - iterate over all slaves
 * @bond:	the bond holding this list
 * @pos:	current slave
 * @iter:	list_head * iterator
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave(bond, pos, iter) \
	netdev_for_each_lower_private((bond)->dev, pos, iter)

/* Caller must have rcu_read_lock */
#define bond_for_each_slave_rcu(bond, pos, iter) \
	netdev_for_each_lower_private_rcu((bond)->dev, pos, iter)

#ifdef CONFIG_NET_POLL_CONTROLLER
extern atomic_t netpoll_block_tx;

static inline void block_netpoll_tx(void)
{
	atomic_inc(&netpoll_block_tx);
}

static inline void unblock_netpoll_tx(void)
{
	atomic_dec(&netpoll_block_tx);
}

static inline int is_netpoll_tx_blocked(struct net_device *dev)
{
	if (unlikely(netpoll_tx_running(dev)))
		return atomic_read(&netpoll_block_tx);
	return 0;
}
#else
#define block_netpoll_tx()
#define unblock_netpoll_tx()
#define is_netpoll_tx_blocked(dev) (0)
#endif

struct bond_params {
	int mode;
	int xmit_policy;
	int miimon;
	u8 num_peer_notif;
	int arp_interval;
	int arp_validate;
	int arp_all_targets;
	int use_carrier;
	int fail_over_mac;
	int updelay;
	int downdelay;
	int lacp_fast;
	unsigned int min_links;
	int ad_select;
	char primary[IFNAMSIZ];
	int primary_reselect;
	__be32 arp_targets[BOND_MAX_ARP_TARGETS];
	int tx_queues;
	int all_slaves_active;
	int resend_igmp;
	int lp_interval;
	int packets_per_slave;
};

struct bond_parm_tbl {
	char *modename;
	int mode;
};

#define BOND_MAX_MODENAME_LEN 20

struct slave {
	struct net_device *dev; /* first - useful for panic debug */
	struct bonding *bond; /* our master */
	int    delay;
	unsigned long jiffies;
	unsigned long last_arp_rx;
	unsigned long target_last_arp_rx[BOND_MAX_ARP_TARGETS];
	s8     link;    /* one of BOND_LINK_XXXX */
	s8     new_link;
	u8     backup:1,   /* indicates backup slave. Value corresponds with
			      BOND_STATE_ACTIVE and BOND_STATE_BACKUP */
	       inactive:1; /* indicates inactive slave */
	u8     duplex;
	u32    original_mtu;
	u32    link_failure_count;
	u32    speed;
	u16    queue_id;
	u8     perm_hwaddr[ETH_ALEN];
	struct ad_slave_info ad_info; /* HUGE - better to dynamically alloc */
	struct tlb_slave_info tlb_info;
#ifdef CONFIG_NET_POLL_CONTROLLER
	struct netpoll *np;
#endif
};

/*
 * Link pseudo-state only used internally by monitors
 */
#define BOND_LINK_NOCHANGE -1

/*
 * Here are the locking policies for the two bonding locks:
 *
 * 1) Get bond->lock when reading/writing slave list.
 * 2) Get bond->curr_slave_lock when reading/writing bond->curr_active_slave.
 *    (It is unnecessary when the write-lock is put with bond->lock.)
 * 3) When we lock with bond->curr_slave_lock, we must lock with bond->lock
 *    beforehand.
 */
struct bonding {
	struct   net_device *dev; /* first - useful for panic debug */
	struct   slave *curr_active_slave;
	struct   slave *current_arp_slave;
	struct   slave *primary_slave;
	bool     force_primary;
	s32      slave_cnt; /* never change this value outside the attach/detach wrappers */
	int     (*recv_probe)(const struct sk_buff *, struct bonding *,
			      struct slave *);
	rwlock_t lock;
	rwlock_t curr_slave_lock;
	u8	 send_peer_notif;
	u8       igmp_retrans;
#ifdef CONFIG_PROC_FS
	struct   proc_dir_entry *proc_entry;
	char     proc_file_name[IFNAMSIZ];
#endif /* CONFIG_PROC_FS */
	struct   list_head bond_list;
	u32      rr_tx_counter;
	struct   ad_bond_info ad_info;
	struct   alb_bond_info alb_info;
	struct   bond_params params;
	struct   workqueue_struct *wq;
	struct   delayed_work mii_work;
	struct   delayed_work arp_work;
	struct   delayed_work alb_work;
	struct   delayed_work ad_work;
	struct   delayed_work mcast_work;
#ifdef CONFIG_DEBUG_FS
	/* debugging support via debugfs */
	struct	 dentry *debug_dir;
#endif /* CONFIG_DEBUG_FS */
};

#define bond_slave_get_rcu(dev) \
	((struct slave *) rcu_dereference(dev->rx_handler_data))

#define bond_slave_get_rtnl(dev) \
	((struct slave *) rtnl_dereference(dev->rx_handler_data))

/**
 * Returns NULL if the net_device does not belong to any of the bond's slaves
 *
 * Caller must hold bond lock for read
 */
static inline struct slave *bond_get_slave_by_dev(struct bonding *bond,
						  struct net_device *slave_dev)
{
	return netdev_lower_dev_get_private(bond->dev, slave_dev);
}

static inline struct bonding *bond_get_bond_by_slave(struct slave *slave)
{
	if (!slave || !slave->bond)
		return NULL;
	return slave->bond;
}

static inline bool bond_is_lb(const struct bonding *bond)
{
	return BOND_MODE_IS_LB(bond->params.mode);
}

static inline void bond_set_active_slave(struct slave *slave)
{
	slave->backup = 0;
}

static inline void bond_set_backup_slave(struct slave *slave)
{
	slave->backup = 1;
}

static inline int bond_slave_state(struct slave *slave)
{
	return slave->backup;
}

static inline bool bond_is_active_slave(struct slave *slave)
{
	return !bond_slave_state(slave);
}

#define BOND_PRI_RESELECT_ALWAYS	0
#define BOND_PRI_RESELECT_BETTER	1
#define BOND_PRI_RESELECT_FAILURE	2

#define BOND_FOM_NONE			0
#define BOND_FOM_ACTIVE			1
#define BOND_FOM_FOLLOW			2

#define BOND_ARP_TARGETS_ANY		0
#define BOND_ARP_TARGETS_ALL		1

#define BOND_ARP_VALIDATE_NONE		0
#define BOND_ARP_VALIDATE_ACTIVE	(1 << BOND_STATE_ACTIVE)
#define BOND_ARP_VALIDATE_BACKUP	(1 << BOND_STATE_BACKUP)
#define BOND_ARP_VALIDATE_ALL		(BOND_ARP_VALIDATE_ACTIVE | \
					 BOND_ARP_VALIDATE_BACKUP)

static inline int slave_do_arp_validate(struct bonding *bond,
					struct slave *slave)
{
	return bond->params.arp_validate & (1 << bond_slave_state(slave));
}

/* Get the oldest arp which we've received on this slave for bond's
 * arp_targets.
 */
static inline unsigned long slave_oldest_target_arp_rx(struct bonding *bond,
						       struct slave *slave)
{
	int i = 1;
	unsigned long ret = slave->target_last_arp_rx[0];

	for (; (i < BOND_MAX_ARP_TARGETS) && bond->params.arp_targets[i]; i++)
		if (time_before(slave->target_last_arp_rx[i], ret))
			ret = slave->target_last_arp_rx[i];

	return ret;
}

static inline unsigned long slave_last_rx(struct bonding *bond,
					struct slave *slave)
{
	if (slave_do_arp_validate(bond, slave)) {
		if (bond->params.arp_all_targets == BOND_ARP_TARGETS_ALL)
			return slave_oldest_target_arp_rx(bond, slave);
		else
			return slave->last_arp_rx;
	}

	return slave->dev->last_rx;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static inline void bond_netpoll_send_skb(const struct slave *slave,
					 struct sk_buff *skb)
{
	struct netpoll *np = slave->np;

	if (np)
		netpoll_send_skb(np, skb);
}
#else
static inline void bond_netpoll_send_skb(const struct slave *slave,
					 struct sk_buff *skb)
{
}
#endif

static inline void bond_set_slave_inactive_flags(struct slave *slave)
{
	if (!bond_is_lb(slave->bond))
		bond_set_backup_slave(slave);
	if (!slave->bond->params.all_slaves_active)
		slave->inactive = 1;
}

static inline void bond_set_slave_active_flags(struct slave *slave)
{
	bond_set_active_slave(slave);
	slave->inactive = 0;
}

static inline bool bond_is_slave_inactive(struct slave *slave)
{
	return slave->inactive;
}

static inline __be32 bond_confirm_addr(struct net_device *dev, __be32 dst, __be32 local)
{
	struct in_device *in_dev;
	__be32 addr = 0;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);

	if (in_dev)
		addr = inet_confirm_addr(in_dev, dst, local, RT_SCOPE_HOST);

	rcu_read_unlock();
	return addr;
}

static inline bool slave_can_tx(struct slave *slave)
{
	if (IS_UP(slave->dev) && slave->link == BOND_LINK_UP &&
	    bond_is_active_slave(slave))
		return true;
	else
		return false;
}

struct bond_net;

int bond_arp_rcv(const struct sk_buff *skb, struct bonding *bond, struct slave *slave);
int bond_dev_queue_xmit(struct bonding *bond, struct sk_buff *skb, struct net_device *slave_dev);
void bond_xmit_slave_id(struct bonding *bond, struct sk_buff *skb, int slave_id);
int bond_create(struct net *net, const char *name);
int bond_create_sysfs(struct bond_net *net);
void bond_destroy_sysfs(struct bond_net *net);
void bond_prepare_sysfs_group(struct bonding *bond);
int bond_enslave(struct net_device *bond_dev, struct net_device *slave_dev);
int bond_release(struct net_device *bond_dev, struct net_device *slave_dev);
void bond_mii_monitor(struct work_struct *);
void bond_loadbalance_arp_mon(struct work_struct *);
void bond_activebackup_arp_mon(struct work_struct *);
int bond_xmit_hash(struct bonding *bond, struct sk_buff *skb, int count);
int bond_parse_parm(const char *mode_arg, const struct bond_parm_tbl *tbl);
void bond_select_active_slave(struct bonding *bond);
void bond_change_active_slave(struct bonding *bond, struct slave *new_active);
void bond_create_debugfs(void);
void bond_destroy_debugfs(void);
void bond_debug_register(struct bonding *bond);
void bond_debug_unregister(struct bonding *bond);
void bond_debug_reregister(struct bonding *bond);
const char *bond_mode_name(int mode);
void bond_setup(struct net_device *bond_dev);
unsigned int bond_get_num_tx_queues(void);
int bond_netlink_init(void);
void bond_netlink_fini(void);
int bond_option_mode_set(struct bonding *bond, int mode);
int bond_option_active_slave_set(struct bonding *bond, struct net_device *slave_dev);
struct net_device *bond_option_active_slave_get_rcu(struct bonding *bond);
struct net_device *bond_option_active_slave_get(struct bonding *bond);

struct bond_net {
	struct net *		net;	/* Associated network namespace */
	struct list_head	dev_list;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *	proc_dir;
#endif
	struct class_attribute	class_attr_bonding_masters;
};

#ifdef CONFIG_PROC_FS
void bond_create_proc_entry(struct bonding *bond);
void bond_remove_proc_entry(struct bonding *bond);
void bond_create_proc_dir(struct bond_net *bn);
void bond_destroy_proc_dir(struct bond_net *bn);
#else
static inline void bond_create_proc_entry(struct bonding *bond)
{
}

static inline void bond_remove_proc_entry(struct bonding *bond)
{
}

static inline void bond_create_proc_dir(struct bond_net *bn)
{
}

static inline void bond_destroy_proc_dir(struct bond_net *bn)
{
}
#endif

static inline struct slave *bond_slave_has_mac(struct bonding *bond,
					       const u8 *mac)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave(bond, tmp, iter)
		if (ether_addr_equal_64bits(mac, tmp->dev->dev_addr))
			return tmp;

	return NULL;
}

/* Caller must hold rcu_read_lock() for read */
static inline struct slave *bond_slave_has_mac_rcu(struct bonding *bond,
					       const u8 *mac)
{
	struct list_head *iter;
	struct slave *tmp;

	bond_for_each_slave_rcu(bond, tmp, iter)
		if (ether_addr_equal_64bits(mac, tmp->dev->dev_addr))
			return tmp;

	return NULL;
}

/* Check if the ip is present in arp ip list, or first free slot if ip == 0
 * Returns -1 if not found, index if found
 */
static inline int bond_get_targets_ip(__be32 *targets, __be32 ip)
{
	int i;

	for (i = 0; i < BOND_MAX_ARP_TARGETS; i++)
		if (targets[i] == ip)
			return i;
		else if (targets[i] == 0)
			break;

	return -1;
}

/* exported from bond_main.c */
extern int bond_net_id;
extern const struct bond_parm_tbl bond_lacp_tbl[];
extern const struct bond_parm_tbl bond_mode_tbl[];
extern const struct bond_parm_tbl xmit_hashtype_tbl[];
extern const struct bond_parm_tbl arp_validate_tbl[];
extern const struct bond_parm_tbl arp_all_targets_tbl[];
extern const struct bond_parm_tbl fail_over_mac_tbl[];
extern const struct bond_parm_tbl pri_reselect_tbl[];
extern struct bond_parm_tbl ad_select_tbl[];

/* exported from bond_netlink.c */
extern struct rtnl_link_ops bond_link_ops;

#endif /* _LINUX_BONDING_H */
