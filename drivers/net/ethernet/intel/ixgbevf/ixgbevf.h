/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBEVF_H_
#define _IXGBEVF_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/u64_stats_sync.h>

#include "vf.h"

#ifdef CONFIG_NET_RX_BUSY_POLL
#include <net/busy_poll.h>
#define BP_EXTENDED_STATS
#endif

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbevf_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned long time_stamp;
	union ixgbe_adv_tx_desc *next_to_watch;
	u16 length;
	u16 mapped_as_page;
};

struct ixgbevf_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
};

struct ixgbevf_ring {
	struct ixgbevf_ring *next;
	struct net_device *netdev;
	struct device *dev;
	void *desc;			/* descriptor ring memory */
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */
	unsigned int count;		/* amount of descriptors */
	unsigned int next_to_use;
	unsigned int next_to_clean;

	int queue_index; /* needed for multiqueue queue management */
	union {
		struct ixgbevf_tx_buffer *tx_buffer_info;
		struct ixgbevf_rx_buffer *rx_buffer_info;
	};

	u64			total_bytes;
	u64			total_packets;
	struct u64_stats_sync	syncp;
	u64 hw_csum_rx_error;
	u64 hw_csum_rx_good;
#ifdef BP_EXTENDED_STATS
	u64 bp_yields;
	u64 bp_misses;
	u64 bp_cleaned;
#endif

	u16 head;
	u16 tail;

	u16 reg_idx; /* holds the special value that gets the hardware register
		      * offset associated with this ring, which is different
		      * for DCB and RSS modes */

	u16 rx_buf_len;
};

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBEVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define MAX_RX_QUEUES IXGBE_VF_MAX_RX_QUEUES
#define MAX_TX_QUEUES IXGBE_VF_MAX_TX_QUEUES

#define IXGBEVF_DEFAULT_TXD   1024
#define IXGBEVF_DEFAULT_RXD   512
#define IXGBEVF_MAX_TXD       4096
#define IXGBEVF_MIN_TXD       64
#define IXGBEVF_MAX_RXD       4096
#define IXGBEVF_MIN_RXD       64

/* Supported Rx Buffer Sizes */
#define IXGBEVF_RXBUFFER_256   256    /* Used for packet split */
#define IXGBEVF_RXBUFFER_2K    2048
#define IXGBEVF_RXBUFFER_4K    4096
#define IXGBEVF_RXBUFFER_8K    8192
#define IXGBEVF_RXBUFFER_10K   10240

#define IXGBEVF_RX_HDR_SIZE IXGBEVF_RXBUFFER_256

#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define IXGBE_TX_FLAGS_CSUM		(u32)(1)
#define IXGBE_TX_FLAGS_VLAN		(u32)(1 << 1)
#define IXGBE_TX_FLAGS_TSO		(u32)(1 << 2)
#define IXGBE_TX_FLAGS_IPV4		(u32)(1 << 3)
#define IXGBE_TX_FLAGS_FCOE		(u32)(1 << 4)
#define IXGBE_TX_FLAGS_FSO		(u32)(1 << 5)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK	0x0000e000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

struct ixgbevf_ring_container {
	struct ixgbevf_ring *ring;	/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u8 count;			/* total number of rings in vector */
	u8 itr;				/* current ITR setting for ring */
};

/* iterator for handling rings in ring container */
#define ixgbevf_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbevf_q_vector {
	struct ixgbevf_adapter *adapter;
	u16 v_idx;		/* index of q_vector within array, also used for
				 * finding the bit in EICR and friends that
				 * represents the vector for this ring */
	u16 itr;		/* Interrupt throttle rate written to EITR */
	struct napi_struct napi;
	struct ixgbevf_ring_container rx, tx;
	char name[IFNAMSIZ + 9];
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int state;
#define IXGBEVF_QV_STATE_IDLE		0
#define IXGBEVF_QV_STATE_NAPI		1    /* NAPI owns this QV */
#define IXGBEVF_QV_STATE_POLL		2    /* poll owns this QV */
#define IXGBEVF_QV_STATE_DISABLED	4    /* QV is disabled */
#define IXGBEVF_QV_OWNED (IXGBEVF_QV_STATE_NAPI | IXGBEVF_QV_STATE_POLL)
#define IXGBEVF_QV_LOCKED (IXGBEVF_QV_OWNED | IXGBEVF_QV_STATE_DISABLED)
#define IXGBEVF_QV_STATE_NAPI_YIELD	8    /* NAPI yielded this QV */
#define IXGBEVF_QV_STATE_POLL_YIELD	16   /* poll yielded this QV */
#define IXGBEVF_QV_YIELD (IXGBEVF_QV_STATE_NAPI_YIELD | IXGBEVF_QV_STATE_POLL_YIELD)
#define IXGBEVF_QV_USER_PEND (IXGBEVF_QV_STATE_POLL | IXGBEVF_QV_STATE_POLL_YIELD)
	spinlock_t lock;
#endif /* CONFIG_NET_RX_BUSY_POLL */
};
#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void ixgbevf_qv_init_lock(struct ixgbevf_q_vector *q_vector)
{

	spin_lock_init(&q_vector->lock);
	q_vector->state = IXGBEVF_QV_STATE_IDLE;
}

/* called from the device poll routine to get ownership of a q_vector */
static inline bool ixgbevf_qv_lock_napi(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);
	if (q_vector->state & IXGBEVF_QV_LOCKED) {
		WARN_ON(q_vector->state & IXGBEVF_QV_STATE_NAPI);
		q_vector->state |= IXGBEVF_QV_STATE_NAPI_YIELD;
		rc = false;
#ifdef BP_EXTENDED_STATS
		q_vector->tx.ring->bp_yields++;
#endif
	} else {
		/* we don't care if someone yielded */
		q_vector->state = IXGBEVF_QV_STATE_NAPI;
	}
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* returns true is someone tried to get the qv while napi had it */
static inline bool ixgbevf_qv_unlock_napi(struct ixgbevf_q_vector *q_vector)
{
	int rc = false;
	spin_lock_bh(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBEVF_QV_STATE_POLL |
				   IXGBEVF_QV_STATE_NAPI_YIELD));

	if (q_vector->state & IXGBEVF_QV_STATE_POLL_YIELD)
		rc = true;
	/* reset state to idle, unless QV is disabled */
	q_vector->state &= IXGBEVF_QV_STATE_DISABLED;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* called from ixgbevf_low_latency_poll() */
static inline bool ixgbevf_qv_lock_poll(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);
	if ((q_vector->state & IXGBEVF_QV_LOCKED)) {
		q_vector->state |= IXGBEVF_QV_STATE_POLL_YIELD;
		rc = false;
#ifdef BP_EXTENDED_STATS
		q_vector->rx.ring->bp_yields++;
#endif
	} else {
		/* preserve yield marks */
		q_vector->state |= IXGBEVF_QV_STATE_POLL;
	}
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* returns true if someone tried to get the qv while it was locked */
static inline bool ixgbevf_qv_unlock_poll(struct ixgbevf_q_vector *q_vector)
{
	int rc = false;
	spin_lock_bh(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBEVF_QV_STATE_NAPI));

	if (q_vector->state & IXGBEVF_QV_STATE_POLL_YIELD)
		rc = true;
	/* reset state to idle, unless QV is disabled */
	q_vector->state &= IXGBEVF_QV_STATE_DISABLED;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* true if a socket is polling, even if it did not get the lock */
static inline bool ixgbevf_qv_busy_polling(struct ixgbevf_q_vector *q_vector)
{
	WARN_ON(!(q_vector->state & IXGBEVF_QV_OWNED));
	return q_vector->state & IXGBEVF_QV_USER_PEND;
}

/* false if QV is currently owned */
static inline bool ixgbevf_qv_disable(struct ixgbevf_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);
	if (q_vector->state & IXGBEVF_QV_OWNED)
		rc = false;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

#endif /* CONFIG_NET_RX_BUSY_POLL */

/*
 * microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define IXGBE_MIN_RSC_ITR	24
#define IXGBE_100K_ITR		40
#define IXGBE_20K_ITR		200
#define IXGBE_10K_ITR		400
#define IXGBE_8K_ITR		500

/* Helper macros to switch between ints/sec and what the register uses.
 * And yes, it's the same math going both ways.  The lowest value
 * supported by all of the ixgbe hardware is 8.
 */
#define EITR_INTS_PER_SEC_TO_REG(_eitr) \
	((_eitr) ? (1000000000 / ((_eitr) * 256)) : 8)
#define EITR_REG_TO_INTS_PER_SEC EITR_INTS_PER_SEC_TO_REG

static inline u16 ixgbevf_desc_unused(struct ixgbevf_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

#define IXGBEVF_RX_DESC(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_DESC(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_CTXTDESC(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE	9728 /* Maximum Supported Size 9.5KB */

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS 2

#define MIN_MSIX_Q_VECTORS 1
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbevf_adapter {
	struct timer_list watchdog_timer;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 bd_number;
	struct work_struct reset_task;
	struct ixgbevf_q_vector *q_vector[MAX_MSIX_Q_VECTORS];

	/* Interrupt Throttle Rate */
	u16 rx_itr_setting;
	u16 tx_itr_setting;

	/* interrupt masks */
	u32 eims_enable_mask;
	u32 eims_other;

	/* TX */
	struct ixgbevf_ring *tx_ring;	/* One per active queue */
	int num_tx_queues;
	u64 restart_queue;
	u64 hw_csum_tx_good;
	u64 lsc_int;
	u64 hw_tso_ctxt;
	u64 hw_tso6_ctxt;
	u32 tx_timeout_count;

	/* RX */
	struct ixgbevf_ring *rx_ring;	/* One per active queue */
	int num_rx_queues;
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	u64 hw_csum_rx_good;
	u64 non_eop_descs;
	int num_msix_vectors;
	struct msix_entry *msix_entries;

	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

	/* Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	u32 flags;
#define IXGBE_FLAG_IN_WATCHDOG_TASK             (u32)(1)
#define IXGBE_FLAG_IN_NETPOLL                   (u32)(1 << 1)

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	/* structs defined in ixgbe_vf.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	struct ixgbevf_hw_stats stats;
	/* Interrupt Throttle Rate */
	u32 eitr_param;

	unsigned long state;
	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

#ifdef BP_EXTENDED_STATS
	u64 bp_rx_yields;
	u64 bp_rx_cleaned;
	u64 bp_rx_missed;

	u64 bp_tx_yields;
	u64 bp_tx_cleaned;
	u64 bp_tx_missed;
#endif

	u32 link_speed;
	bool link_up;

	struct work_struct watchdog_task;

	spinlock_t mbx_lock;
};

enum ixbgevf_state_t {
	__IXGBEVF_TESTING,
	__IXGBEVF_RESETTING,
	__IXGBEVF_DOWN
};

struct ixgbevf_cb {
	struct sk_buff *prev;
};
#define IXGBE_CB(skb) ((struct ixgbevf_cb *)(skb)->cb)

enum ixgbevf_boards {
	board_82599_vf,
	board_X540_vf,
};

extern const struct ixgbevf_info ixgbevf_82599_vf_info;
extern const struct ixgbevf_info ixgbevf_X540_vf_info;
extern const struct ixgbe_mbx_operations ixgbevf_mbx_ops;

/* needed by ethtool.c */
extern const char ixgbevf_driver_name[];
extern const char ixgbevf_driver_version[];

void ixgbevf_up(struct ixgbevf_adapter *adapter);
void ixgbevf_down(struct ixgbevf_adapter *adapter);
void ixgbevf_reinit_locked(struct ixgbevf_adapter *adapter);
void ixgbevf_reset(struct ixgbevf_adapter *adapter);
void ixgbevf_set_ethtool_ops(struct net_device *netdev);
int ixgbevf_setup_rx_resources(struct ixgbevf_adapter *, struct ixgbevf_ring *);
int ixgbevf_setup_tx_resources(struct ixgbevf_adapter *, struct ixgbevf_ring *);
void ixgbevf_free_rx_resources(struct ixgbevf_adapter *, struct ixgbevf_ring *);
void ixgbevf_free_tx_resources(struct ixgbevf_adapter *, struct ixgbevf_ring *);
void ixgbevf_update_stats(struct ixgbevf_adapter *adapter);
int ethtool_ioctl(struct ifreq *ifr);

extern void ixgbevf_write_eitr(struct ixgbevf_q_vector *q_vector);

void ixgbe_napi_add_all(struct ixgbevf_adapter *adapter);
void ixgbe_napi_del_all(struct ixgbevf_adapter *adapter);

#ifdef DEBUG
char *ixgbevf_get_hw_dev_name(struct ixgbe_hw *hw);
#define hw_dbg(hw, format, arg...) \
	printk(KERN_DEBUG "%s: " format, ixgbevf_get_hw_dev_name(hw), ##arg)
#else
#define hw_dbg(hw, format, arg...) do {} while (0)
#endif

#endif /* _IXGBEVF_H_ */
