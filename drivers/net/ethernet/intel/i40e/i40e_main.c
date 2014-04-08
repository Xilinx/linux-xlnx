/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

/* Local includes */
#include "i40e.h"

const char i40e_driver_name[] = "i40e";
static const char i40e_driver_string[] =
			"Intel(R) Ethernet Connection XL710 Network Driver";

#define DRV_KERN "-k"

#define DRV_VERSION_MAJOR 0
#define DRV_VERSION_MINOR 3
#define DRV_VERSION_BUILD 11
#define DRV_VERSION __stringify(DRV_VERSION_MAJOR) "." \
	     __stringify(DRV_VERSION_MINOR) "." \
	     __stringify(DRV_VERSION_BUILD)    DRV_KERN
const char i40e_driver_version_str[] = DRV_VERSION;
static const char i40e_copyright[] = "Copyright (c) 2013 Intel Corporation.";

/* a bit of forward declarations */
static void i40e_vsi_reinit_locked(struct i40e_vsi *vsi);
static void i40e_handle_reset_warning(struct i40e_pf *pf);
static int i40e_add_vsi(struct i40e_vsi *vsi);
static int i40e_add_veb(struct i40e_veb *veb, struct i40e_vsi *vsi);
static int i40e_setup_pf_switch(struct i40e_pf *pf);
static int i40e_setup_misc_vector(struct i40e_pf *pf);
static void i40e_determine_queue_usage(struct i40e_pf *pf);
static int i40e_setup_pf_filter_control(struct i40e_pf *pf);

/* i40e_pci_tbl - PCI Device ID Table
 *
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static DEFINE_PCI_DEVICE_TABLE(i40e_pci_tbl) = {
	{PCI_VDEVICE(INTEL, I40E_SFP_XL710_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_SFP_X710_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_QEMU_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_KX_A_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_KX_B_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_KX_C_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_KX_D_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_QSFP_A_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_QSFP_B_DEVICE_ID), 0},
	{PCI_VDEVICE(INTEL, I40E_QSFP_C_DEVICE_ID), 0},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, i40e_pci_tbl);

#define I40E_MAX_VF_COUNT 128
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

MODULE_AUTHOR("Intel Corporation, <e1000-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Intel(R) Ethernet Connection XL710 Network Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/**
 * i40e_allocate_dma_mem_d - OS specific memory alloc for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 * @alignment: what to align the allocation to
 **/
int i40e_allocate_dma_mem_d(struct i40e_hw *hw, struct i40e_dma_mem *mem,
			    u64 size, u32 alignment)
{
	struct i40e_pf *pf = (struct i40e_pf *)hw->back;

	mem->size = ALIGN(size, alignment);
	mem->va = dma_zalloc_coherent(&pf->pdev->dev, mem->size,
				      &mem->pa, GFP_KERNEL);
	if (!mem->va)
		return -ENOMEM;

	return 0;
}

/**
 * i40e_free_dma_mem_d - OS specific memory free for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 **/
int i40e_free_dma_mem_d(struct i40e_hw *hw, struct i40e_dma_mem *mem)
{
	struct i40e_pf *pf = (struct i40e_pf *)hw->back;

	dma_free_coherent(&pf->pdev->dev, mem->size, mem->va, mem->pa);
	mem->va = NULL;
	mem->pa = 0;
	mem->size = 0;

	return 0;
}

/**
 * i40e_allocate_virt_mem_d - OS specific memory alloc for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 **/
int i40e_allocate_virt_mem_d(struct i40e_hw *hw, struct i40e_virt_mem *mem,
			     u32 size)
{
	mem->size = size;
	mem->va = kzalloc(size, GFP_KERNEL);

	if (!mem->va)
		return -ENOMEM;

	return 0;
}

/**
 * i40e_free_virt_mem_d - OS specific memory free for shared code
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 **/
int i40e_free_virt_mem_d(struct i40e_hw *hw, struct i40e_virt_mem *mem)
{
	/* it's ok to kfree a NULL pointer */
	kfree(mem->va);
	mem->va = NULL;
	mem->size = 0;

	return 0;
}

/**
 * i40e_get_lump - find a lump of free generic resource
 * @pf: board private structure
 * @pile: the pile of resource to search
 * @needed: the number of items needed
 * @id: an owner id to stick on the items assigned
 *
 * Returns the base item index of the lump, or negative for error
 *
 * The search_hint trick and lack of advanced fit-finding only work
 * because we're highly likely to have all the same size lump requests.
 * Linear search time and any fragmentation should be minimal.
 **/
static int i40e_get_lump(struct i40e_pf *pf, struct i40e_lump_tracking *pile,
			 u16 needed, u16 id)
{
	int ret = -ENOMEM;
	int i, j;

	if (!pile || needed == 0 || id >= I40E_PILE_VALID_BIT) {
		dev_info(&pf->pdev->dev,
			 "param err: pile=%p needed=%d id=0x%04x\n",
			 pile, needed, id);
		return -EINVAL;
	}

	/* start the linear search with an imperfect hint */
	i = pile->search_hint;
	while (i < pile->num_entries) {
		/* skip already allocated entries */
		if (pile->list[i] & I40E_PILE_VALID_BIT) {
			i++;
			continue;
		}

		/* do we have enough in this lump? */
		for (j = 0; (j < needed) && ((i+j) < pile->num_entries); j++) {
			if (pile->list[i+j] & I40E_PILE_VALID_BIT)
				break;
		}

		if (j == needed) {
			/* there was enough, so assign it to the requestor */
			for (j = 0; j < needed; j++)
				pile->list[i+j] = id | I40E_PILE_VALID_BIT;
			ret = i;
			pile->search_hint = i + j;
			break;
		} else {
			/* not enough, so skip over it and continue looking */
			i += j;
		}
	}

	return ret;
}

/**
 * i40e_put_lump - return a lump of generic resource
 * @pile: the pile of resource to search
 * @index: the base item index
 * @id: the owner id of the items assigned
 *
 * Returns the count of items in the lump
 **/
static int i40e_put_lump(struct i40e_lump_tracking *pile, u16 index, u16 id)
{
	int valid_id = (id | I40E_PILE_VALID_BIT);
	int count = 0;
	int i;

	if (!pile || index >= pile->num_entries)
		return -EINVAL;

	for (i = index;
	     i < pile->num_entries && pile->list[i] == valid_id;
	     i++) {
		pile->list[i] = 0;
		count++;
	}

	if (count && index < pile->search_hint)
		pile->search_hint = index;

	return count;
}

/**
 * i40e_service_event_schedule - Schedule the service task to wake up
 * @pf: board private structure
 *
 * If not already scheduled, this puts the task into the work queue
 **/
static void i40e_service_event_schedule(struct i40e_pf *pf)
{
	if (!test_bit(__I40E_DOWN, &pf->state) &&
	    !test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state) &&
	    !test_and_set_bit(__I40E_SERVICE_SCHED, &pf->state))
		schedule_work(&pf->service_task);
}

/**
 * i40e_tx_timeout - Respond to a Tx Hang
 * @netdev: network interface device structure
 *
 * If any port has noticed a Tx timeout, it is likely that the whole
 * device is munged, not just the one netdev port, so go for the full
 * reset.
 **/
static void i40e_tx_timeout(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;

	pf->tx_timeout_count++;

	if (time_after(jiffies, (pf->tx_timeout_last_recovery + HZ*20)))
		pf->tx_timeout_recovery_level = 0;
	pf->tx_timeout_last_recovery = jiffies;
	netdev_info(netdev, "tx_timeout recovery level %d\n",
		    pf->tx_timeout_recovery_level);

	switch (pf->tx_timeout_recovery_level) {
	case 0:
		/* disable and re-enable queues for the VSI */
		if (in_interrupt()) {
			set_bit(__I40E_REINIT_REQUESTED, &pf->state);
			set_bit(__I40E_REINIT_REQUESTED, &vsi->state);
		} else {
			i40e_vsi_reinit_locked(vsi);
		}
		break;
	case 1:
		set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
		break;
	case 2:
		set_bit(__I40E_CORE_RESET_REQUESTED, &pf->state);
		break;
	case 3:
		set_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state);
		break;
	default:
		netdev_err(netdev, "tx_timeout recovery unsuccessful\n");
		i40e_down(vsi);
		break;
	}
	i40e_service_event_schedule(pf);
	pf->tx_timeout_recovery_level++;
}

/**
 * i40e_release_rx_desc - Store the new tail and head values
 * @rx_ring: ring to bump
 * @val: new head index
 **/
static inline void i40e_release_rx_desc(struct i40e_ring *rx_ring, u32 val)
{
	rx_ring->next_to_use = val;

	/* Force memory writes to complete before letting h/w
	 * know there are new descriptors to fetch.  (Only
	 * applicable for weak-ordered memory model archs,
	 * such as IA-64).
	 */
	wmb();
	writel(val, rx_ring->tail);
}

/**
 * i40e_get_vsi_stats_struct - Get System Network Statistics
 * @vsi: the VSI we care about
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the service task.
 **/
struct rtnl_link_stats64 *i40e_get_vsi_stats_struct(struct i40e_vsi *vsi)
{
	return &vsi->net_stats;
}

/**
 * i40e_get_netdev_stats_struct - Get statistics for netdev interface
 * @netdev: network interface device structure
 *
 * Returns the address of the device statistics structure.
 * The statistics are actually updated from the service task.
 **/
static struct rtnl_link_stats64 *i40e_get_netdev_stats_struct(
					     struct net_device *netdev,
					     struct rtnl_link_stats64 *stats)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct rtnl_link_stats64 *vsi_stats = i40e_get_vsi_stats_struct(vsi);
	int i;

	if (!vsi->tx_rings)
		return stats;

	rcu_read_lock();
	for (i = 0; i < vsi->num_queue_pairs; i++) {
		struct i40e_ring *tx_ring, *rx_ring;
		u64 bytes, packets;
		unsigned int start;

		tx_ring = ACCESS_ONCE(vsi->tx_rings[i]);
		if (!tx_ring)
			continue;

		do {
			start = u64_stats_fetch_begin_bh(&tx_ring->syncp);
			packets = tx_ring->stats.packets;
			bytes   = tx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_bh(&tx_ring->syncp, start));

		stats->tx_packets += packets;
		stats->tx_bytes   += bytes;
		rx_ring = &tx_ring[1];

		do {
			start = u64_stats_fetch_begin_bh(&rx_ring->syncp);
			packets = rx_ring->stats.packets;
			bytes   = rx_ring->stats.bytes;
		} while (u64_stats_fetch_retry_bh(&rx_ring->syncp, start));

		stats->rx_packets += packets;
		stats->rx_bytes   += bytes;
	}
	rcu_read_unlock();

	/* following stats updated by ixgbe_watchdog_task() */
	stats->multicast	= vsi_stats->multicast;
	stats->tx_errors	= vsi_stats->tx_errors;
	stats->tx_dropped	= vsi_stats->tx_dropped;
	stats->rx_errors	= vsi_stats->rx_errors;
	stats->rx_crc_errors	= vsi_stats->rx_crc_errors;
	stats->rx_length_errors	= vsi_stats->rx_length_errors;

	return stats;
}

/**
 * i40e_vsi_reset_stats - Resets all stats of the given vsi
 * @vsi: the VSI to have its stats reset
 **/
void i40e_vsi_reset_stats(struct i40e_vsi *vsi)
{
	struct rtnl_link_stats64 *ns;
	int i;

	if (!vsi)
		return;

	ns = i40e_get_vsi_stats_struct(vsi);
	memset(ns, 0, sizeof(*ns));
	memset(&vsi->net_stats_offsets, 0, sizeof(vsi->net_stats_offsets));
	memset(&vsi->eth_stats, 0, sizeof(vsi->eth_stats));
	memset(&vsi->eth_stats_offsets, 0, sizeof(vsi->eth_stats_offsets));
	if (vsi->rx_rings)
		for (i = 0; i < vsi->num_queue_pairs; i++) {
			memset(&vsi->rx_rings[i]->stats, 0 ,
			       sizeof(vsi->rx_rings[i]->stats));
			memset(&vsi->rx_rings[i]->rx_stats, 0 ,
			       sizeof(vsi->rx_rings[i]->rx_stats));
			memset(&vsi->tx_rings[i]->stats, 0 ,
			       sizeof(vsi->tx_rings[i]->stats));
			memset(&vsi->tx_rings[i]->tx_stats, 0,
			       sizeof(vsi->tx_rings[i]->tx_stats));
		}
	vsi->stat_offsets_loaded = false;
}

/**
 * i40e_pf_reset_stats - Reset all of the stats for the given pf
 * @pf: the PF to be reset
 **/
void i40e_pf_reset_stats(struct i40e_pf *pf)
{
	memset(&pf->stats, 0, sizeof(pf->stats));
	memset(&pf->stats_offsets, 0, sizeof(pf->stats_offsets));
	pf->stat_offsets_loaded = false;
}

/**
 * i40e_stat_update48 - read and update a 48 bit stat from the chip
 * @hw: ptr to the hardware info
 * @hireg: the high 32 bit reg to read
 * @loreg: the low 32 bit reg to read
 * @offset_loaded: has the initial offset been loaded yet
 * @offset: ptr to current offset value
 * @stat: ptr to the stat
 *
 * Since the device stats are not reset at PFReset, they likely will not
 * be zeroed when the driver starts.  We'll save the first values read
 * and use them as offsets to be subtracted from the raw values in order
 * to report stats that count from zero.  In the process, we also manage
 * the potential roll-over.
 **/
static void i40e_stat_update48(struct i40e_hw *hw, u32 hireg, u32 loreg,
			       bool offset_loaded, u64 *offset, u64 *stat)
{
	u64 new_data;

	if (hw->device_id == I40E_QEMU_DEVICE_ID) {
		new_data = rd32(hw, loreg);
		new_data |= ((u64)(rd32(hw, hireg) & 0xFFFF)) << 32;
	} else {
		new_data = rd64(hw, loreg);
	}
	if (!offset_loaded)
		*offset = new_data;
	if (likely(new_data >= *offset))
		*stat = new_data - *offset;
	else
		*stat = (new_data + ((u64)1 << 48)) - *offset;
	*stat &= 0xFFFFFFFFFFFFULL;
}

/**
 * i40e_stat_update32 - read and update a 32 bit stat from the chip
 * @hw: ptr to the hardware info
 * @reg: the hw reg to read
 * @offset_loaded: has the initial offset been loaded yet
 * @offset: ptr to current offset value
 * @stat: ptr to the stat
 **/
static void i40e_stat_update32(struct i40e_hw *hw, u32 reg,
			       bool offset_loaded, u64 *offset, u64 *stat)
{
	u32 new_data;

	new_data = rd32(hw, reg);
	if (!offset_loaded)
		*offset = new_data;
	if (likely(new_data >= *offset))
		*stat = (u32)(new_data - *offset);
	else
		*stat = (u32)((new_data + ((u64)1 << 32)) - *offset);
}

/**
 * i40e_update_eth_stats - Update VSI-specific ethernet statistics counters.
 * @vsi: the VSI to be updated
 **/
void i40e_update_eth_stats(struct i40e_vsi *vsi)
{
	int stat_idx = le16_to_cpu(vsi->info.stat_counter_idx);
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */

	es = &vsi->eth_stats;
	oes = &vsi->eth_stats_offsets;

	/* Gather up the stats that the hw collects */
	i40e_stat_update32(hw, I40E_GLV_TEPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_errors, &es->tx_errors);
	i40e_stat_update32(hw, I40E_GLV_RDPC(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_discards, &es->rx_discards);

	i40e_stat_update48(hw, I40E_GLV_GORCH(stat_idx),
			   I40E_GLV_GORCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_bytes, &es->rx_bytes);
	i40e_stat_update48(hw, I40E_GLV_UPRCH(stat_idx),
			   I40E_GLV_UPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_unicast, &es->rx_unicast);
	i40e_stat_update48(hw, I40E_GLV_MPRCH(stat_idx),
			   I40E_GLV_MPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_multicast, &es->rx_multicast);
	i40e_stat_update48(hw, I40E_GLV_BPRCH(stat_idx),
			   I40E_GLV_BPRCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->rx_broadcast, &es->rx_broadcast);

	i40e_stat_update48(hw, I40E_GLV_GOTCH(stat_idx),
			   I40E_GLV_GOTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_bytes, &es->tx_bytes);
	i40e_stat_update48(hw, I40E_GLV_UPTCH(stat_idx),
			   I40E_GLV_UPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_unicast, &es->tx_unicast);
	i40e_stat_update48(hw, I40E_GLV_MPTCH(stat_idx),
			   I40E_GLV_MPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_multicast, &es->tx_multicast);
	i40e_stat_update48(hw, I40E_GLV_BPTCH(stat_idx),
			   I40E_GLV_BPTCL(stat_idx),
			   vsi->stat_offsets_loaded,
			   &oes->tx_broadcast, &es->tx_broadcast);
	vsi->stat_offsets_loaded = true;
}

/**
 * i40e_update_veb_stats - Update Switch component statistics
 * @veb: the VEB being updated
 **/
static void i40e_update_veb_stats(struct i40e_veb *veb)
{
	struct i40e_pf *pf = veb->pf;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */
	int idx = 0;

	idx = veb->stats_idx;
	es = &veb->stats;
	oes = &veb->stats_offsets;

	/* Gather up the stats that the hw collects */
	i40e_stat_update32(hw, I40E_GLSW_TDPC(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_discards, &es->tx_discards);
	i40e_stat_update32(hw, I40E_GLSW_RUPP(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_unknown_protocol, &es->rx_unknown_protocol);

	i40e_stat_update48(hw, I40E_GLSW_GORCH(idx), I40E_GLSW_GORCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_bytes, &es->rx_bytes);
	i40e_stat_update48(hw, I40E_GLSW_UPRCH(idx), I40E_GLSW_UPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_unicast, &es->rx_unicast);
	i40e_stat_update48(hw, I40E_GLSW_MPRCH(idx), I40E_GLSW_MPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_multicast, &es->rx_multicast);
	i40e_stat_update48(hw, I40E_GLSW_BPRCH(idx), I40E_GLSW_BPRCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->rx_broadcast, &es->rx_broadcast);

	i40e_stat_update48(hw, I40E_GLSW_GOTCH(idx), I40E_GLSW_GOTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_bytes, &es->tx_bytes);
	i40e_stat_update48(hw, I40E_GLSW_UPTCH(idx), I40E_GLSW_UPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_unicast, &es->tx_unicast);
	i40e_stat_update48(hw, I40E_GLSW_MPTCH(idx), I40E_GLSW_MPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_multicast, &es->tx_multicast);
	i40e_stat_update48(hw, I40E_GLSW_BPTCH(idx), I40E_GLSW_BPTCL(idx),
			   veb->stat_offsets_loaded,
			   &oes->tx_broadcast, &es->tx_broadcast);
	veb->stat_offsets_loaded = true;
}

/**
 * i40e_update_link_xoff_rx - Update XOFF received in link flow control mode
 * @pf: the corresponding PF
 *
 * Update the Rx XOFF counter (PAUSE frames) in link flow control mode
 **/
static void i40e_update_link_xoff_rx(struct i40e_pf *pf)
{
	struct i40e_hw_port_stats *osd = &pf->stats_offsets;
	struct i40e_hw_port_stats *nsd = &pf->stats;
	struct i40e_hw *hw = &pf->hw;
	u64 xoff = 0;
	u16 i, v;

	if ((hw->fc.current_mode != I40E_FC_FULL) &&
	    (hw->fc.current_mode != I40E_FC_RX_PAUSE))
		return;

	xoff = nsd->link_xoff_rx;
	i40e_stat_update32(hw, I40E_GLPRT_LXOFFRXC(hw->port),
			   pf->stat_offsets_loaded,
			   &osd->link_xoff_rx, &nsd->link_xoff_rx);

	/* No new LFC xoff rx */
	if (!(nsd->link_xoff_rx - xoff))
		return;

	/* Clear the __I40E_HANG_CHECK_ARMED bit for all Tx rings */
	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		struct i40e_vsi *vsi = pf->vsi[v];

		if (!vsi)
			continue;

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			struct i40e_ring *ring = vsi->tx_rings[i];
			clear_bit(__I40E_HANG_CHECK_ARMED, &ring->state);
		}
	}
}

/**
 * i40e_update_prio_xoff_rx - Update XOFF received in PFC mode
 * @pf: the corresponding PF
 *
 * Update the Rx XOFF counter (PAUSE frames) in PFC mode
 **/
static void i40e_update_prio_xoff_rx(struct i40e_pf *pf)
{
	struct i40e_hw_port_stats *osd = &pf->stats_offsets;
	struct i40e_hw_port_stats *nsd = &pf->stats;
	bool xoff[I40E_MAX_TRAFFIC_CLASS] = {false};
	struct i40e_dcbx_config *dcb_cfg;
	struct i40e_hw *hw = &pf->hw;
	u16 i, v;
	u8 tc;

	dcb_cfg = &hw->local_dcbx_config;

	/* See if DCB enabled with PFC TC */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED) ||
	    !(dcb_cfg->pfc.pfcenable)) {
		i40e_update_link_xoff_rx(pf);
		return;
	}

	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		u64 prio_xoff = nsd->priority_xoff_rx[i];
		i40e_stat_update32(hw, I40E_GLPRT_PXOFFRXC(hw->port, i),
				   pf->stat_offsets_loaded,
				   &osd->priority_xoff_rx[i],
				   &nsd->priority_xoff_rx[i]);

		/* No new PFC xoff rx */
		if (!(nsd->priority_xoff_rx[i] - prio_xoff))
			continue;
		/* Get the TC for given priority */
		tc = dcb_cfg->etscfg.prioritytable[i];
		xoff[tc] = true;
	}

	/* Clear the __I40E_HANG_CHECK_ARMED bit for Tx rings */
	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		struct i40e_vsi *vsi = pf->vsi[v];

		if (!vsi)
			continue;

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			struct i40e_ring *ring = vsi->tx_rings[i];

			tc = ring->dcb_tc;
			if (xoff[tc])
				clear_bit(__I40E_HANG_CHECK_ARMED,
					  &ring->state);
		}
	}
}

/**
 * i40e_update_stats - Update the board statistics counters.
 * @vsi: the VSI to be updated
 *
 * There are a few instances where we store the same stat in a
 * couple of different structs.  This is partly because we have
 * the netdev stats that need to be filled out, which is slightly
 * different from the "eth_stats" defined by the chip and used in
 * VF communications.  We sort it all out here in a central place.
 **/
void i40e_update_stats(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct rtnl_link_stats64 *ons;
	struct rtnl_link_stats64 *ns;   /* netdev stats */
	struct i40e_eth_stats *oes;
	struct i40e_eth_stats *es;     /* device's eth stats */
	u32 tx_restart, tx_busy;
	u32 rx_page, rx_buf;
	u64 rx_p, rx_b;
	u64 tx_p, tx_b;
	int i;
	u16 q;

	if (test_bit(__I40E_DOWN, &vsi->state) ||
	    test_bit(__I40E_CONFIG_BUSY, &pf->state))
		return;

	ns = i40e_get_vsi_stats_struct(vsi);
	ons = &vsi->net_stats_offsets;
	es = &vsi->eth_stats;
	oes = &vsi->eth_stats_offsets;

	/* Gather up the netdev and vsi stats that the driver collects
	 * on the fly during packet processing
	 */
	rx_b = rx_p = 0;
	tx_b = tx_p = 0;
	tx_restart = tx_busy = 0;
	rx_page = 0;
	rx_buf = 0;
	rcu_read_lock();
	for (q = 0; q < vsi->num_queue_pairs; q++) {
		struct i40e_ring *p;
		u64 bytes, packets;
		unsigned int start;

		/* locate Tx ring */
		p = ACCESS_ONCE(vsi->tx_rings[q]);

		do {
			start = u64_stats_fetch_begin_bh(&p->syncp);
			packets = p->stats.packets;
			bytes = p->stats.bytes;
		} while (u64_stats_fetch_retry_bh(&p->syncp, start));
		tx_b += bytes;
		tx_p += packets;
		tx_restart += p->tx_stats.restart_queue;
		tx_busy += p->tx_stats.tx_busy;

		/* Rx queue is part of the same block as Tx queue */
		p = &p[1];
		do {
			start = u64_stats_fetch_begin_bh(&p->syncp);
			packets = p->stats.packets;
			bytes = p->stats.bytes;
		} while (u64_stats_fetch_retry_bh(&p->syncp, start));
		rx_b += bytes;
		rx_p += packets;
		rx_buf += p->rx_stats.alloc_rx_buff_failed;
		rx_page += p->rx_stats.alloc_rx_page_failed;
	}
	rcu_read_unlock();
	vsi->tx_restart = tx_restart;
	vsi->tx_busy = tx_busy;
	vsi->rx_page_failed = rx_page;
	vsi->rx_buf_failed = rx_buf;

	ns->rx_packets = rx_p;
	ns->rx_bytes = rx_b;
	ns->tx_packets = tx_p;
	ns->tx_bytes = tx_b;

	i40e_update_eth_stats(vsi);
	/* update netdev stats from eth stats */
	ons->rx_errors = oes->rx_errors;
	ns->rx_errors = es->rx_errors;
	ons->tx_errors = oes->tx_errors;
	ns->tx_errors = es->tx_errors;
	ons->multicast = oes->rx_multicast;
	ns->multicast = es->rx_multicast;
	ons->tx_dropped = oes->tx_discards;
	ns->tx_dropped = es->tx_discards;

	/* Get the port data only if this is the main PF VSI */
	if (vsi == pf->vsi[pf->lan_vsi]) {
		struct i40e_hw_port_stats *nsd = &pf->stats;
		struct i40e_hw_port_stats *osd = &pf->stats_offsets;

		i40e_stat_update48(hw, I40E_GLPRT_GORCH(hw->port),
				   I40E_GLPRT_GORCL(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->eth.rx_bytes, &nsd->eth.rx_bytes);
		i40e_stat_update48(hw, I40E_GLPRT_GOTCH(hw->port),
				   I40E_GLPRT_GOTCL(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->eth.tx_bytes, &nsd->eth.tx_bytes);
		i40e_stat_update32(hw, I40E_GLPRT_RDPC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->eth.rx_discards,
				   &nsd->eth.rx_discards);
		i40e_stat_update32(hw, I40E_GLPRT_TDPC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->eth.tx_discards,
				   &nsd->eth.tx_discards);
		i40e_stat_update48(hw, I40E_GLPRT_MPRCH(hw->port),
				   I40E_GLPRT_MPRCL(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->eth.rx_multicast,
				   &nsd->eth.rx_multicast);

		i40e_stat_update32(hw, I40E_GLPRT_TDOLD(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_dropped_link_down,
				   &nsd->tx_dropped_link_down);

		i40e_stat_update32(hw, I40E_GLPRT_CRCERRS(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->crc_errors, &nsd->crc_errors);
		ns->rx_crc_errors = nsd->crc_errors;

		i40e_stat_update32(hw, I40E_GLPRT_ILLERRC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->illegal_bytes, &nsd->illegal_bytes);
		ns->rx_errors = nsd->crc_errors
				+ nsd->illegal_bytes;

		i40e_stat_update32(hw, I40E_GLPRT_MLFC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->mac_local_faults,
				   &nsd->mac_local_faults);
		i40e_stat_update32(hw, I40E_GLPRT_MRFC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->mac_remote_faults,
				   &nsd->mac_remote_faults);

		i40e_stat_update32(hw, I40E_GLPRT_RLEC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_length_errors,
				   &nsd->rx_length_errors);
		ns->rx_length_errors = nsd->rx_length_errors;

		i40e_stat_update32(hw, I40E_GLPRT_LXONRXC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->link_xon_rx, &nsd->link_xon_rx);
		i40e_stat_update32(hw, I40E_GLPRT_LXONTXC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->link_xon_tx, &nsd->link_xon_tx);
		i40e_update_prio_xoff_rx(pf);  /* handles I40E_GLPRT_LXOFFRXC */
		i40e_stat_update32(hw, I40E_GLPRT_LXOFFTXC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->link_xoff_tx, &nsd->link_xoff_tx);

		for (i = 0; i < 8; i++) {
			i40e_stat_update32(hw, I40E_GLPRT_PXONRXC(hw->port, i),
					   pf->stat_offsets_loaded,
					   &osd->priority_xon_rx[i],
					   &nsd->priority_xon_rx[i]);
			i40e_stat_update32(hw, I40E_GLPRT_PXONTXC(hw->port, i),
					   pf->stat_offsets_loaded,
					   &osd->priority_xon_tx[i],
					   &nsd->priority_xon_tx[i]);
			i40e_stat_update32(hw, I40E_GLPRT_PXOFFTXC(hw->port, i),
					   pf->stat_offsets_loaded,
					   &osd->priority_xoff_tx[i],
					   &nsd->priority_xoff_tx[i]);
			i40e_stat_update32(hw,
					   I40E_GLPRT_RXON2OFFCNT(hw->port, i),
					   pf->stat_offsets_loaded,
					   &osd->priority_xon_2_xoff[i],
					   &nsd->priority_xon_2_xoff[i]);
		}

		i40e_stat_update48(hw, I40E_GLPRT_PRC64H(hw->port),
				   I40E_GLPRT_PRC64L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_64, &nsd->rx_size_64);
		i40e_stat_update48(hw, I40E_GLPRT_PRC127H(hw->port),
				   I40E_GLPRT_PRC127L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_127, &nsd->rx_size_127);
		i40e_stat_update48(hw, I40E_GLPRT_PRC255H(hw->port),
				   I40E_GLPRT_PRC255L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_255, &nsd->rx_size_255);
		i40e_stat_update48(hw, I40E_GLPRT_PRC511H(hw->port),
				   I40E_GLPRT_PRC511L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_511, &nsd->rx_size_511);
		i40e_stat_update48(hw, I40E_GLPRT_PRC1023H(hw->port),
				   I40E_GLPRT_PRC1023L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_1023, &nsd->rx_size_1023);
		i40e_stat_update48(hw, I40E_GLPRT_PRC1522H(hw->port),
				   I40E_GLPRT_PRC1522L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_1522, &nsd->rx_size_1522);
		i40e_stat_update48(hw, I40E_GLPRT_PRC9522H(hw->port),
				   I40E_GLPRT_PRC9522L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_size_big, &nsd->rx_size_big);

		i40e_stat_update48(hw, I40E_GLPRT_PTC64H(hw->port),
				   I40E_GLPRT_PTC64L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_64, &nsd->tx_size_64);
		i40e_stat_update48(hw, I40E_GLPRT_PTC127H(hw->port),
				   I40E_GLPRT_PTC127L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_127, &nsd->tx_size_127);
		i40e_stat_update48(hw, I40E_GLPRT_PTC255H(hw->port),
				   I40E_GLPRT_PTC255L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_255, &nsd->tx_size_255);
		i40e_stat_update48(hw, I40E_GLPRT_PTC511H(hw->port),
				   I40E_GLPRT_PTC511L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_511, &nsd->tx_size_511);
		i40e_stat_update48(hw, I40E_GLPRT_PTC1023H(hw->port),
				   I40E_GLPRT_PTC1023L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_1023, &nsd->tx_size_1023);
		i40e_stat_update48(hw, I40E_GLPRT_PTC1522H(hw->port),
				   I40E_GLPRT_PTC1522L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_1522, &nsd->tx_size_1522);
		i40e_stat_update48(hw, I40E_GLPRT_PTC9522H(hw->port),
				   I40E_GLPRT_PTC9522L(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->tx_size_big, &nsd->tx_size_big);

		i40e_stat_update32(hw, I40E_GLPRT_RUC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_undersize, &nsd->rx_undersize);
		i40e_stat_update32(hw, I40E_GLPRT_RFC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_fragments, &nsd->rx_fragments);
		i40e_stat_update32(hw, I40E_GLPRT_ROC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_oversize, &nsd->rx_oversize);
		i40e_stat_update32(hw, I40E_GLPRT_RJC(hw->port),
				   pf->stat_offsets_loaded,
				   &osd->rx_jabber, &nsd->rx_jabber);
	}

	pf->stat_offsets_loaded = true;
}

/**
 * i40e_find_filter - Search VSI filter list for specific mac/vlan filter
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure its a vf filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns ptr to the filter object or NULL
 **/
static struct i40e_mac_filter *i40e_find_filter(struct i40e_vsi *vsi,
						u8 *macaddr, s16 vlan,
						bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return NULL;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (vlan == f->vlan)    &&
		    (!is_vf || f->is_vf) &&
		    (!is_netdev || f->is_netdev))
			return f;
	}
	return NULL;
}

/**
 * i40e_find_mac - Find a mac addr in the macvlan filters list
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address we are searching for
 * @is_vf: make sure its a vf filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns the first filter with the provided MAC address or NULL if
 * MAC address was not found
 **/
struct i40e_mac_filter *i40e_find_mac(struct i40e_vsi *vsi, u8 *macaddr,
				      bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return NULL;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if ((ether_addr_equal(macaddr, f->macaddr)) &&
		    (!is_vf || f->is_vf) &&
		    (!is_netdev || f->is_netdev))
			return f;
	}
	return NULL;
}

/**
 * i40e_is_vsi_in_vlan - Check if VSI is in vlan mode
 * @vsi: the VSI to be searched
 *
 * Returns true if VSI is in vlan mode or false otherwise
 **/
bool i40e_is_vsi_in_vlan(struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f;

	/* Only -1 for all the filters denotes not in vlan mode
	 * so we have to go through all the list in order to make sure
	 */
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (f->vlan >= 0)
			return true;
	}

	return false;
}

/**
 * i40e_put_mac_in_vlan - Make macvlan filters from macaddrs and vlans
 * @vsi: the VSI to be searched
 * @macaddr: the mac address to be filtered
 * @is_vf: true if it is a vf
 * @is_netdev: true if it is a netdev
 *
 * Goes through all the macvlan filters and adds a
 * macvlan filter for each unique vlan that already exists
 *
 * Returns first filter found on success, else NULL
 **/
struct i40e_mac_filter *i40e_put_mac_in_vlan(struct i40e_vsi *vsi, u8 *macaddr,
					     bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (!i40e_find_filter(vsi, macaddr, f->vlan,
				      is_vf, is_netdev)) {
			if (!i40e_add_filter(vsi, macaddr, f->vlan,
						is_vf, is_netdev))
				return NULL;
		}
	}

	return list_first_entry_or_null(&vsi->mac_filter_list,
					struct i40e_mac_filter, list);
}

/**
 * i40e_add_filter - Add a mac/vlan filter to the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure its a vf filter, else doesn't matter
 * @is_netdev: make sure its a netdev filter, else doesn't matter
 *
 * Returns ptr to the filter object or NULL when no memory available.
 **/
struct i40e_mac_filter *i40e_add_filter(struct i40e_vsi *vsi,
					u8 *macaddr, s16 vlan,
					bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return NULL;

	f = i40e_find_filter(vsi, macaddr, vlan, is_vf, is_netdev);
	if (!f) {
		f = kzalloc(sizeof(*f), GFP_ATOMIC);
		if (!f)
			goto add_filter_out;

		memcpy(f->macaddr, macaddr, ETH_ALEN);
		f->vlan = vlan;
		f->changed = true;

		INIT_LIST_HEAD(&f->list);
		list_add(&f->list, &vsi->mac_filter_list);
	}

	/* increment counter and add a new flag if needed */
	if (is_vf) {
		if (!f->is_vf) {
			f->is_vf = true;
			f->counter++;
		}
	} else if (is_netdev) {
		if (!f->is_netdev) {
			f->is_netdev = true;
			f->counter++;
		}
	} else {
		f->counter++;
	}

	/* changed tells sync_filters_subtask to
	 * push the filter down to the firmware
	 */
	if (f->changed) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
	}

add_filter_out:
	return f;
}

/**
 * i40e_del_filter - Remove a mac/vlan filter from the VSI
 * @vsi: the VSI to be searched
 * @macaddr: the MAC address
 * @vlan: the vlan
 * @is_vf: make sure it's a vf filter, else doesn't matter
 * @is_netdev: make sure it's a netdev filter, else doesn't matter
 **/
void i40e_del_filter(struct i40e_vsi *vsi,
		     u8 *macaddr, s16 vlan,
		     bool is_vf, bool is_netdev)
{
	struct i40e_mac_filter *f;

	if (!vsi || !macaddr)
		return;

	f = i40e_find_filter(vsi, macaddr, vlan, is_vf, is_netdev);
	if (!f || f->counter == 0)
		return;

	if (is_vf) {
		if (f->is_vf) {
			f->is_vf = false;
			f->counter--;
		}
	} else if (is_netdev) {
		if (f->is_netdev) {
			f->is_netdev = false;
			f->counter--;
		}
	} else {
		/* make sure we don't remove a filter in use by vf or netdev */
		int min_f = 0;
		min_f += (f->is_vf ? 1 : 0);
		min_f += (f->is_netdev ? 1 : 0);

		if (f->counter > min_f)
			f->counter--;
	}

	/* counter == 0 tells sync_filters_subtask to
	 * remove the filter from the firmware's list
	 */
	if (f->counter == 0) {
		f->changed = true;
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
	}
}

/**
 * i40e_set_mac - NDO callback to set mac address
 * @netdev: network interface device structure
 * @p: pointer to an address structure
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_set_mac(struct net_device *netdev, void *p)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct sockaddr *addr = p;
	struct i40e_mac_filter *f;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	netdev_info(netdev, "set mac address=%pM\n", addr->sa_data);

	if (ether_addr_equal(netdev->dev_addr, addr->sa_data))
		return 0;

	if (vsi->type == I40E_VSI_MAIN) {
		i40e_status ret;
		ret = i40e_aq_mac_address_write(&vsi->back->hw,
						I40E_AQC_WRITE_TYPE_LAA_ONLY,
						addr->sa_data, NULL);
		if (ret) {
			netdev_info(netdev,
				    "Addr change for Main VSI failed: %d\n",
				    ret);
			return -EADDRNOTAVAIL;
		}

		memcpy(vsi->back->hw.mac.addr, addr->sa_data, netdev->addr_len);
	}

	/* In order to be sure to not drop any packets, add the new address
	 * then delete the old one.
	 */
	f = i40e_add_filter(vsi, addr->sa_data, I40E_VLAN_ANY, false, false);
	if (!f)
		return -ENOMEM;

	i40e_sync_vsi_filters(vsi);
	i40e_del_filter(vsi, netdev->dev_addr, I40E_VLAN_ANY, false, false);
	i40e_sync_vsi_filters(vsi);

	memcpy(netdev->dev_addr, addr->sa_data, netdev->addr_len);

	return 0;
}

/**
 * i40e_vsi_setup_queue_map - Setup a VSI queue map based on enabled_tc
 * @vsi: the VSI being setup
 * @ctxt: VSI context structure
 * @enabled_tc: Enabled TCs bitmap
 * @is_add: True if called before Add VSI
 *
 * Setup VSI queue mapping for enabled traffic classes.
 **/
static void i40e_vsi_setup_queue_map(struct i40e_vsi *vsi,
				     struct i40e_vsi_context *ctxt,
				     u8 enabled_tc,
				     bool is_add)
{
	struct i40e_pf *pf = vsi->back;
	u16 sections = 0;
	u8 netdev_tc = 0;
	u16 numtc = 0;
	u16 qcount;
	u8 offset;
	u16 qmap;
	int i;

	sections = I40E_AQ_VSI_PROP_QUEUE_MAP_VALID;
	offset = 0;

	if (enabled_tc && (vsi->back->flags & I40E_FLAG_DCB_ENABLED)) {
		/* Find numtc from enabled TC bitmap */
		for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
			if (enabled_tc & (1 << i)) /* TC is enabled */
				numtc++;
		}
		if (!numtc) {
			dev_warn(&pf->pdev->dev, "DCB is enabled but no TC enabled, forcing TC0\n");
			numtc = 1;
		}
	} else {
		/* At least TC0 is enabled in case of non-DCB case */
		numtc = 1;
	}

	vsi->tc_config.numtc = numtc;
	vsi->tc_config.enabled_tc = enabled_tc ? enabled_tc : 1;

	/* Setup queue offset/count for all TCs for given VSI */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* See if the given TC is enabled for the given VSI */
		if (vsi->tc_config.enabled_tc & (1 << i)) { /* TC is enabled */
			int pow, num_qps;

			vsi->tc_config.tc_info[i].qoffset = offset;
			switch (vsi->type) {
			case I40E_VSI_MAIN:
				if (i == 0)
					qcount = pf->rss_size;
				else
					qcount = pf->num_tc_qps;
				vsi->tc_config.tc_info[i].qcount = qcount;
				break;
			case I40E_VSI_FDIR:
			case I40E_VSI_SRIOV:
			case I40E_VSI_VMDQ2:
			default:
				qcount = vsi->alloc_queue_pairs;
				vsi->tc_config.tc_info[i].qcount = qcount;
				WARN_ON(i != 0);
				break;
			}

			/* find the power-of-2 of the number of queue pairs */
			num_qps = vsi->tc_config.tc_info[i].qcount;
			pow = 0;
			while (num_qps &&
			      ((1 << pow) < vsi->tc_config.tc_info[i].qcount)) {
				pow++;
				num_qps >>= 1;
			}

			vsi->tc_config.tc_info[i].netdev_tc = netdev_tc++;
			qmap =
			    (offset << I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT) |
			    (pow << I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT);

			offset += vsi->tc_config.tc_info[i].qcount;
		} else {
			/* TC is not enabled so set the offset to
			 * default queue and allocate one queue
			 * for the given TC.
			 */
			vsi->tc_config.tc_info[i].qoffset = 0;
			vsi->tc_config.tc_info[i].qcount = 1;
			vsi->tc_config.tc_info[i].netdev_tc = 0;

			qmap = 0;
		}
		ctxt->info.tc_mapping[i] = cpu_to_le16(qmap);
	}

	/* Set actual Tx/Rx queue pairs */
	vsi->num_queue_pairs = offset;

	/* Scheduler section valid can only be set for ADD VSI */
	if (is_add) {
		sections |= I40E_AQ_VSI_PROP_SCHED_VALID;

		ctxt->info.up_enable_bits = enabled_tc;
	}
	if (vsi->type == I40E_VSI_SRIOV) {
		ctxt->info.mapping_flags |=
				     cpu_to_le16(I40E_AQ_VSI_QUE_MAP_NONCONTIG);
		for (i = 0; i < vsi->num_queue_pairs; i++)
			ctxt->info.queue_mapping[i] =
					       cpu_to_le16(vsi->base_queue + i);
	} else {
		ctxt->info.mapping_flags |=
					cpu_to_le16(I40E_AQ_VSI_QUE_MAP_CONTIG);
		ctxt->info.queue_mapping[0] = cpu_to_le16(vsi->base_queue);
	}
	ctxt->info.valid_sections |= cpu_to_le16(sections);
}

/**
 * i40e_set_rx_mode - NDO callback to set the netdev filters
 * @netdev: network interface device structure
 **/
static void i40e_set_rx_mode(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_vsi *vsi = np->vsi;
	struct netdev_hw_addr *uca;
	struct netdev_hw_addr *mca;
	struct netdev_hw_addr *ha;

	/* add addr if not already in the filter list */
	netdev_for_each_uc_addr(uca, netdev) {
		if (!i40e_find_mac(vsi, uca->addr, false, true)) {
			if (i40e_is_vsi_in_vlan(vsi))
				i40e_put_mac_in_vlan(vsi, uca->addr,
						     false, true);
			else
				i40e_add_filter(vsi, uca->addr, I40E_VLAN_ANY,
						false, true);
		}
	}

	netdev_for_each_mc_addr(mca, netdev) {
		if (!i40e_find_mac(vsi, mca->addr, false, true)) {
			if (i40e_is_vsi_in_vlan(vsi))
				i40e_put_mac_in_vlan(vsi, mca->addr,
						     false, true);
			else
				i40e_add_filter(vsi, mca->addr, I40E_VLAN_ANY,
						false, true);
		}
	}

	/* remove filter if not in netdev list */
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
		bool found = false;

		if (!f->is_netdev)
			continue;

		if (is_multicast_ether_addr(f->macaddr)) {
			netdev_for_each_mc_addr(mca, netdev) {
				if (ether_addr_equal(mca->addr, f->macaddr)) {
					found = true;
					break;
				}
			}
		} else {
			netdev_for_each_uc_addr(uca, netdev) {
				if (ether_addr_equal(uca->addr, f->macaddr)) {
					found = true;
					break;
				}
			}

			for_each_dev_addr(netdev, ha) {
				if (ether_addr_equal(ha->addr, f->macaddr)) {
					found = true;
					break;
				}
			}
		}
		if (!found)
			i40e_del_filter(
			   vsi, f->macaddr, I40E_VLAN_ANY, false, true);
	}

	/* check for other flag changes */
	if (vsi->current_netdev_flags != vsi->netdev->flags) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		vsi->back->flags |= I40E_FLAG_FILTER_SYNC;
	}
}

/**
 * i40e_sync_vsi_filters - Update the VSI filter list to the HW
 * @vsi: ptr to the VSI
 *
 * Push any outstanding VSI filter changes through the AdminQ.
 *
 * Returns 0 or error value
 **/
int i40e_sync_vsi_filters(struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f, *ftmp;
	bool promisc_forced_on = false;
	bool add_happened = false;
	int filter_list_len = 0;
	u32 changed_flags = 0;
	i40e_status aq_ret = 0;
	struct i40e_pf *pf;
	int num_add = 0;
	int num_del = 0;
	u16 cmd_flags;

	/* empty array typed pointers, kcalloc later */
	struct i40e_aqc_add_macvlan_element_data *add_list;
	struct i40e_aqc_remove_macvlan_element_data *del_list;

	while (test_and_set_bit(__I40E_CONFIG_BUSY, &vsi->state))
		usleep_range(1000, 2000);
	pf = vsi->back;

	if (vsi->netdev) {
		changed_flags = vsi->current_netdev_flags ^ vsi->netdev->flags;
		vsi->current_netdev_flags = vsi->netdev->flags;
	}

	if (vsi->flags & I40E_VSI_FLAG_FILTER_CHANGED) {
		vsi->flags &= ~I40E_VSI_FLAG_FILTER_CHANGED;

		filter_list_len = pf->hw.aq.asq_buf_size /
			    sizeof(struct i40e_aqc_remove_macvlan_element_data);
		del_list = kcalloc(filter_list_len,
			    sizeof(struct i40e_aqc_remove_macvlan_element_data),
			    GFP_KERNEL);
		if (!del_list)
			return -ENOMEM;

		list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
			if (!f->changed)
				continue;

			if (f->counter != 0)
				continue;
			f->changed = false;
			cmd_flags = 0;

			/* add to delete list */
			memcpy(del_list[num_del].mac_addr,
			       f->macaddr, ETH_ALEN);
			del_list[num_del].vlan_tag =
				cpu_to_le16((u16)(f->vlan ==
					    I40E_VLAN_ANY ? 0 : f->vlan));

			/* vlan0 as wild card to allow packets from all vlans */
			if (f->vlan == I40E_VLAN_ANY ||
			    (vsi->netdev && !(vsi->netdev->features &
					      NETIF_F_HW_VLAN_CTAG_FILTER)))
				cmd_flags |= I40E_AQC_MACVLAN_DEL_IGNORE_VLAN;
			cmd_flags |= I40E_AQC_MACVLAN_DEL_PERFECT_MATCH;
			del_list[num_del].flags = cmd_flags;
			num_del++;

			/* unlink from filter list */
			list_del(&f->list);
			kfree(f);

			/* flush a full buffer */
			if (num_del == filter_list_len) {
				aq_ret = i40e_aq_remove_macvlan(&pf->hw,
					    vsi->seid, del_list, num_del,
					    NULL);
				num_del = 0;
				memset(del_list, 0, sizeof(*del_list));

				if (aq_ret)
					dev_info(&pf->pdev->dev,
						 "ignoring delete macvlan error, err %d, aq_err %d while flushing a full buffer\n",
						 aq_ret,
						 pf->hw.aq.asq_last_status);
			}
		}
		if (num_del) {
			aq_ret = i40e_aq_remove_macvlan(&pf->hw, vsi->seid,
						     del_list, num_del, NULL);
			num_del = 0;

			if (aq_ret)
				dev_info(&pf->pdev->dev,
					 "ignoring delete macvlan error, err %d, aq_err %d\n",
					 aq_ret, pf->hw.aq.asq_last_status);
		}

		kfree(del_list);
		del_list = NULL;

		/* do all the adds now */
		filter_list_len = pf->hw.aq.asq_buf_size /
			       sizeof(struct i40e_aqc_add_macvlan_element_data),
		add_list = kcalloc(filter_list_len,
			       sizeof(struct i40e_aqc_add_macvlan_element_data),
			       GFP_KERNEL);
		if (!add_list)
			return -ENOMEM;

		list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
			if (!f->changed)
				continue;

			if (f->counter == 0)
				continue;
			f->changed = false;
			add_happened = true;
			cmd_flags = 0;

			/* add to add array */
			memcpy(add_list[num_add].mac_addr,
			       f->macaddr, ETH_ALEN);
			add_list[num_add].vlan_tag =
				cpu_to_le16(
				 (u16)(f->vlan == I40E_VLAN_ANY ? 0 : f->vlan));
			add_list[num_add].queue_number = 0;

			cmd_flags |= I40E_AQC_MACVLAN_ADD_PERFECT_MATCH;

			/* vlan0 as wild card to allow packets from all vlans */
			if (f->vlan == I40E_VLAN_ANY || (vsi->netdev &&
			    !(vsi->netdev->features &
						 NETIF_F_HW_VLAN_CTAG_FILTER)))
				cmd_flags |= I40E_AQC_MACVLAN_ADD_IGNORE_VLAN;
			add_list[num_add].flags = cpu_to_le16(cmd_flags);
			num_add++;

			/* flush a full buffer */
			if (num_add == filter_list_len) {
				aq_ret = i40e_aq_add_macvlan(&pf->hw, vsi->seid,
							     add_list, num_add,
							     NULL);
				num_add = 0;

				if (aq_ret)
					break;
				memset(add_list, 0, sizeof(*add_list));
			}
		}
		if (num_add) {
			aq_ret = i40e_aq_add_macvlan(&pf->hw, vsi->seid,
						     add_list, num_add, NULL);
			num_add = 0;
		}
		kfree(add_list);
		add_list = NULL;

		if (add_happened && (!aq_ret)) {
			/* do nothing */;
		} else if (add_happened && (aq_ret)) {
			dev_info(&pf->pdev->dev,
				 "add filter failed, err %d, aq_err %d\n",
				 aq_ret, pf->hw.aq.asq_last_status);
			if ((pf->hw.aq.asq_last_status == I40E_AQ_RC_ENOSPC) &&
			    !test_bit(__I40E_FILTER_OVERFLOW_PROMISC,
				      &vsi->state)) {
				promisc_forced_on = true;
				set_bit(__I40E_FILTER_OVERFLOW_PROMISC,
					&vsi->state);
				dev_info(&pf->pdev->dev, "promiscuous mode forced on\n");
			}
		}
	}

	/* check for changes in promiscuous modes */
	if (changed_flags & IFF_ALLMULTI) {
		bool cur_multipromisc;
		cur_multipromisc = !!(vsi->current_netdev_flags & IFF_ALLMULTI);
		aq_ret = i40e_aq_set_vsi_multicast_promiscuous(&vsi->back->hw,
							       vsi->seid,
							       cur_multipromisc,
							       NULL);
		if (aq_ret)
			dev_info(&pf->pdev->dev,
				 "set multi promisc failed, err %d, aq_err %d\n",
				 aq_ret, pf->hw.aq.asq_last_status);
	}
	if ((changed_flags & IFF_PROMISC) || promisc_forced_on) {
		bool cur_promisc;
		cur_promisc = (!!(vsi->current_netdev_flags & IFF_PROMISC) ||
			       test_bit(__I40E_FILTER_OVERFLOW_PROMISC,
					&vsi->state));
		aq_ret = i40e_aq_set_vsi_unicast_promiscuous(&vsi->back->hw,
							     vsi->seid,
							     cur_promisc, NULL);
		if (aq_ret)
			dev_info(&pf->pdev->dev,
				 "set uni promisc failed, err %d, aq_err %d\n",
				 aq_ret, pf->hw.aq.asq_last_status);
	}

	clear_bit(__I40E_CONFIG_BUSY, &vsi->state);
	return 0;
}

/**
 * i40e_sync_filters_subtask - Sync the VSI filter list with HW
 * @pf: board private structure
 **/
static void i40e_sync_filters_subtask(struct i40e_pf *pf)
{
	int v;

	if (!pf || !(pf->flags & I40E_FLAG_FILTER_SYNC))
		return;
	pf->flags &= ~I40E_FLAG_FILTER_SYNC;

	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (pf->vsi[v] &&
		    (pf->vsi[v]->flags & I40E_VSI_FLAG_FILTER_CHANGED))
			i40e_sync_vsi_filters(pf->vsi[v]);
	}
}

/**
 * i40e_change_mtu - NDO callback to change the Maximum Transfer Unit
 * @netdev: network interface device structure
 * @new_mtu: new value for maximum frame size
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	int max_frame = new_mtu + ETH_HLEN + ETH_FCS_LEN;
	struct i40e_vsi *vsi = np->vsi;

	/* MTU < 68 is an error and causes problems on some kernels */
	if ((new_mtu < 68) || (max_frame > I40E_MAX_RXBUFFER))
		return -EINVAL;

	netdev_info(netdev, "changing MTU from %d to %d\n",
		    netdev->mtu, new_mtu);
	netdev->mtu = new_mtu;
	if (netif_running(netdev))
		i40e_vsi_reinit_locked(vsi);

	return 0;
}

/**
 * i40e_vlan_stripping_enable - Turn on vlan stripping for the VSI
 * @vsi: the vsi being adjusted
 **/
void i40e_vlan_stripping_enable(struct i40e_vsi *vsi)
{
	struct i40e_vsi_context ctxt;
	i40e_status ret;

	if ((vsi->info.valid_sections &
	     cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID)) &&
	    ((vsi->info.port_vlan_flags & I40E_AQ_VSI_PVLAN_MODE_MASK) == 0))
		return;  /* already enabled */

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
				    I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH;

	ctxt.seid = vsi->seid;
	memcpy(&ctxt.info, &vsi->info, sizeof(vsi->info));
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "%s: update vsi failed, aq_err=%d\n",
			 __func__, vsi->back->hw.aq.asq_last_status);
	}
}

/**
 * i40e_vlan_stripping_disable - Turn off vlan stripping for the VSI
 * @vsi: the vsi being adjusted
 **/
void i40e_vlan_stripping_disable(struct i40e_vsi *vsi)
{
	struct i40e_vsi_context ctxt;
	i40e_status ret;

	if ((vsi->info.valid_sections &
	     cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID)) &&
	    ((vsi->info.port_vlan_flags & I40E_AQ_VSI_PVLAN_EMOD_MASK) ==
	     I40E_AQ_VSI_PVLAN_EMOD_MASK))
		return;  /* already disabled */

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.port_vlan_flags = I40E_AQ_VSI_PVLAN_MODE_ALL |
				    I40E_AQ_VSI_PVLAN_EMOD_NOTHING;

	ctxt.seid = vsi->seid;
	memcpy(&ctxt.info, &vsi->info, sizeof(vsi->info));
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "%s: update vsi failed, aq_err=%d\n",
			 __func__, vsi->back->hw.aq.asq_last_status);
	}
}

/**
 * i40e_vlan_rx_register - Setup or shutdown vlan offload
 * @netdev: network interface to be adjusted
 * @features: netdev features to test if VLAN offload is enabled or not
 **/
static void i40e_vlan_rx_register(struct net_device *netdev, u32 features)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);
}

/**
 * i40e_vsi_add_vlan - Add vsi membership for given vlan
 * @vsi: the vsi being configured
 * @vid: vlan id to be added (0 = untagged only , -1 = any)
 **/
int i40e_vsi_add_vlan(struct i40e_vsi *vsi, s16 vid)
{
	struct i40e_mac_filter *f, *add_f;
	bool is_netdev, is_vf;
	int ret;

	is_vf = (vsi->type == I40E_VSI_SRIOV);
	is_netdev = !!(vsi->netdev);

	if (is_netdev) {
		add_f = i40e_add_filter(vsi, vsi->netdev->dev_addr, vid,
					is_vf, is_netdev);
		if (!add_f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add vlan filter %d for %pM\n",
				 vid, vsi->netdev->dev_addr);
			return -ENOMEM;
		}
	}

	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		add_f = i40e_add_filter(vsi, f->macaddr, vid, is_vf, is_netdev);
		if (!add_f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add vlan filter %d for %pM\n",
				 vid, f->macaddr);
			return -ENOMEM;
		}
	}

	ret = i40e_sync_vsi_filters(vsi);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Could not sync filters for vid %d\n", vid);
		return ret;
	}

	/* Now if we add a vlan tag, make sure to check if it is the first
	 * tag (i.e. a "tag" -1 does exist) and if so replace the -1 "tag"
	 * with 0, so we now accept untagged and specified tagged traffic
	 * (and not any taged and untagged)
	 */
	if (vid > 0) {
		if (is_netdev && i40e_find_filter(vsi, vsi->netdev->dev_addr,
						  I40E_VLAN_ANY,
						  is_vf, is_netdev)) {
			i40e_del_filter(vsi, vsi->netdev->dev_addr,
					I40E_VLAN_ANY, is_vf, is_netdev);
			add_f = i40e_add_filter(vsi, vsi->netdev->dev_addr, 0,
						is_vf, is_netdev);
			if (!add_f) {
				dev_info(&vsi->back->pdev->dev,
					 "Could not add filter 0 for %pM\n",
					 vsi->netdev->dev_addr);
				return -ENOMEM;
			}
		}

		list_for_each_entry(f, &vsi->mac_filter_list, list) {
			if (i40e_find_filter(vsi, f->macaddr, I40E_VLAN_ANY,
					     is_vf, is_netdev)) {
				i40e_del_filter(vsi, f->macaddr, I40E_VLAN_ANY,
						is_vf, is_netdev);
				add_f = i40e_add_filter(vsi, f->macaddr,
							0, is_vf, is_netdev);
				if (!add_f) {
					dev_info(&vsi->back->pdev->dev,
						 "Could not add filter 0 for %pM\n",
						 f->macaddr);
					return -ENOMEM;
				}
			}
		}
		ret = i40e_sync_vsi_filters(vsi);
	}

	return ret;
}

/**
 * i40e_vsi_kill_vlan - Remove vsi membership for given vlan
 * @vsi: the vsi being configured
 * @vid: vlan id to be removed (0 = untagged only , -1 = any)
 *
 * Return: 0 on success or negative otherwise
 **/
int i40e_vsi_kill_vlan(struct i40e_vsi *vsi, s16 vid)
{
	struct net_device *netdev = vsi->netdev;
	struct i40e_mac_filter *f, *add_f;
	bool is_vf, is_netdev;
	int filter_count = 0;
	int ret;

	is_vf = (vsi->type == I40E_VSI_SRIOV);
	is_netdev = !!(netdev);

	if (is_netdev)
		i40e_del_filter(vsi, netdev->dev_addr, vid, is_vf, is_netdev);

	list_for_each_entry(f, &vsi->mac_filter_list, list)
		i40e_del_filter(vsi, f->macaddr, vid, is_vf, is_netdev);

	ret = i40e_sync_vsi_filters(vsi);
	if (ret) {
		dev_info(&vsi->back->pdev->dev, "Could not sync filters\n");
		return ret;
	}

	/* go through all the filters for this VSI and if there is only
	 * vid == 0 it means there are no other filters, so vid 0 must
	 * be replaced with -1. This signifies that we should from now
	 * on accept any traffic (with any tag present, or untagged)
	 */
	list_for_each_entry(f, &vsi->mac_filter_list, list) {
		if (is_netdev) {
			if (f->vlan &&
			    ether_addr_equal(netdev->dev_addr, f->macaddr))
				filter_count++;
		}

		if (f->vlan)
			filter_count++;
	}

	if (!filter_count && is_netdev) {
		i40e_del_filter(vsi, netdev->dev_addr, 0, is_vf, is_netdev);
		f = i40e_add_filter(vsi, netdev->dev_addr, I40E_VLAN_ANY,
				    is_vf, is_netdev);
		if (!f) {
			dev_info(&vsi->back->pdev->dev,
				 "Could not add filter %d for %pM\n",
				 I40E_VLAN_ANY, netdev->dev_addr);
			return -ENOMEM;
		}
	}

	if (!filter_count) {
		list_for_each_entry(f, &vsi->mac_filter_list, list) {
			i40e_del_filter(vsi, f->macaddr, 0, is_vf, is_netdev);
			add_f = i40e_add_filter(vsi, f->macaddr, I40E_VLAN_ANY,
					    is_vf, is_netdev);
			if (!add_f) {
				dev_info(&vsi->back->pdev->dev,
					 "Could not add filter %d for %pM\n",
					 I40E_VLAN_ANY, f->macaddr);
				return -ENOMEM;
			}
		}
	}

	return i40e_sync_vsi_filters(vsi);
}

/**
 * i40e_vlan_rx_add_vid - Add a vlan id filter to HW offload
 * @netdev: network interface to be adjusted
 * @vid: vlan id to be added
 *
 * net_device_ops implementation for adding vlan ids
 **/
static int i40e_vlan_rx_add_vid(struct net_device *netdev,
				__always_unused __be16 proto, u16 vid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	int ret = 0;

	if (vid > 4095)
		return -EINVAL;

	netdev_info(netdev, "adding %pM vid=%d\n", netdev->dev_addr, vid);

	/* If the network stack called us with vid = 0, we should
	 * indicate to i40e_vsi_add_vlan() that we want to receive
	 * any traffic (i.e. with any vlan tag, or untagged)
	 */
	ret = i40e_vsi_add_vlan(vsi, vid ? vid : I40E_VLAN_ANY);

	if (!ret && (vid < VLAN_N_VID))
		set_bit(vid, vsi->active_vlans);

	return ret;
}

/**
 * i40e_vlan_rx_kill_vid - Remove a vlan id filter from HW offload
 * @netdev: network interface to be adjusted
 * @vid: vlan id to be removed
 *
 * net_device_ops implementation for adding vlan ids
 **/
static int i40e_vlan_rx_kill_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	netdev_info(netdev, "removing %pM vid=%d\n", netdev->dev_addr, vid);

	/* return code is ignored as there is nothing a user
	 * can do about failure to remove and a log message was
	 * already printed from the other function
	 */
	i40e_vsi_kill_vlan(vsi, vid);

	clear_bit(vid, vsi->active_vlans);

	return 0;
}

/**
 * i40e_restore_vlan - Reinstate vlans when vsi/netdev comes back up
 * @vsi: the vsi being brought back up
 **/
static void i40e_restore_vlan(struct i40e_vsi *vsi)
{
	u16 vid;

	if (!vsi->netdev)
		return;

	i40e_vlan_rx_register(vsi->netdev, vsi->netdev->features);

	for_each_set_bit(vid, vsi->active_vlans, VLAN_N_VID)
		i40e_vlan_rx_add_vid(vsi->netdev, htons(ETH_P_8021Q),
				     vid);
}

/**
 * i40e_vsi_add_pvid - Add pvid for the VSI
 * @vsi: the vsi being adjusted
 * @vid: the vlan id to set as a PVID
 **/
int i40e_vsi_add_pvid(struct i40e_vsi *vsi, u16 vid)
{
	struct i40e_vsi_context ctxt;
	i40e_status aq_ret;

	vsi->info.valid_sections = cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
	vsi->info.pvid = cpu_to_le16(vid);
	vsi->info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_INSERT_PVID;
	vsi->info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_MODE_UNTAGGED;

	ctxt.seid = vsi->seid;
	memcpy(&ctxt.info, &vsi->info, sizeof(vsi->info));
	aq_ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (aq_ret) {
		dev_info(&vsi->back->pdev->dev,
			 "%s: update vsi failed, aq_err=%d\n",
			 __func__, vsi->back->hw.aq.asq_last_status);
		return -ENOENT;
	}

	return 0;
}

/**
 * i40e_vsi_remove_pvid - Remove the pvid from the VSI
 * @vsi: the vsi being adjusted
 *
 * Just use the vlan_rx_register() service to put it back to normal
 **/
void i40e_vsi_remove_pvid(struct i40e_vsi *vsi)
{
	vsi->info.pvid = 0;
	i40e_vlan_rx_register(vsi->netdev, vsi->netdev->features);
}

/**
 * i40e_vsi_setup_tx_resources - Allocate VSI Tx queue resources
 * @vsi: ptr to the VSI
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40e_vsi_setup_tx_resources(struct i40e_vsi *vsi)
{
	int i, err = 0;

	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_setup_tx_descriptors(vsi->tx_rings[i]);

	return err;
}

/**
 * i40e_vsi_free_tx_resources - Free Tx resources for VSI queues
 * @vsi: ptr to the VSI
 *
 * Free VSI's transmit software resources
 **/
static void i40e_vsi_free_tx_resources(struct i40e_vsi *vsi)
{
	int i;

	for (i = 0; i < vsi->num_queue_pairs; i++)
		if (vsi->tx_rings[i]->desc)
			i40e_free_tx_resources(vsi->tx_rings[i]);
}

/**
 * i40e_vsi_setup_rx_resources - Allocate VSI queues Rx resources
 * @vsi: ptr to the VSI
 *
 * If this function returns with an error, then it's possible one or
 * more of the rings is populated (while the rest are not).  It is the
 * callers duty to clean those orphaned rings.
 *
 * Return 0 on success, negative on failure
 **/
static int i40e_vsi_setup_rx_resources(struct i40e_vsi *vsi)
{
	int i, err = 0;

	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_setup_rx_descriptors(vsi->rx_rings[i]);
	return err;
}

/**
 * i40e_vsi_free_rx_resources - Free Rx Resources for VSI queues
 * @vsi: ptr to the VSI
 *
 * Free all receive software resources
 **/
static void i40e_vsi_free_rx_resources(struct i40e_vsi *vsi)
{
	int i;

	for (i = 0; i < vsi->num_queue_pairs; i++)
		if (vsi->rx_rings[i]->desc)
			i40e_free_rx_resources(vsi->rx_rings[i]);
}

/**
 * i40e_configure_tx_ring - Configure a transmit ring context and rest
 * @ring: The Tx ring to configure
 *
 * Configure the Tx descriptor ring in the HMC context.
 **/
static int i40e_configure_tx_ring(struct i40e_ring *ring)
{
	struct i40e_vsi *vsi = ring->vsi;
	u16 pf_q = vsi->base_queue + ring->queue_index;
	struct i40e_hw *hw = &vsi->back->hw;
	struct i40e_hmc_obj_txq tx_ctx;
	i40e_status err = 0;
	u32 qtx_ctl = 0;

	/* some ATR related tx ring init */
	if (vsi->back->flags & I40E_FLAG_FDIR_ATR_ENABLED) {
		ring->atr_sample_rate = vsi->back->atr_sample_rate;
		ring->atr_count = 0;
	} else {
		ring->atr_sample_rate = 0;
	}

	/* initialize XPS */
	if (ring->q_vector && ring->netdev &&
	    !test_and_set_bit(__I40E_TX_XPS_INIT_DONE, &ring->state))
		netif_set_xps_queue(ring->netdev,
				    &ring->q_vector->affinity_mask,
				    ring->queue_index);

	/* clear the context structure first */
	memset(&tx_ctx, 0, sizeof(tx_ctx));

	tx_ctx.new_context = 1;
	tx_ctx.base = (ring->dma / 128);
	tx_ctx.qlen = ring->count;
	tx_ctx.fd_ena = !!(vsi->back->flags & (I40E_FLAG_FDIR_ENABLED |
			I40E_FLAG_FDIR_ATR_ENABLED));

	/* As part of VSI creation/update, FW allocates certain
	 * Tx arbitration queue sets for each TC enabled for
	 * the VSI. The FW returns the handles to these queue
	 * sets as part of the response buffer to Add VSI,
	 * Update VSI, etc. AQ commands. It is expected that
	 * these queue set handles be associated with the Tx
	 * queues by the driver as part of the TX queue context
	 * initialization. This has to be done regardless of
	 * DCB as by default everything is mapped to TC0.
	 */
	tx_ctx.rdylist = le16_to_cpu(vsi->info.qs_handle[ring->dcb_tc]);
	tx_ctx.rdylist_act = 0;

	/* clear the context in the HMC */
	err = i40e_clear_lan_tx_queue_context(hw, pf_q);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to clear LAN Tx queue context on Tx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* set the context in the HMC */
	err = i40e_set_lan_tx_queue_context(hw, pf_q, &tx_ctx);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to set LAN Tx queue context on Tx ring %d (pf_q %d, error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* Now associate this queue with this PCI function */
	qtx_ctl = I40E_QTX_CTL_PF_QUEUE;
	qtx_ctl |= ((hw->pf_id << I40E_QTX_CTL_PF_INDX_SHIFT) &
		    I40E_QTX_CTL_PF_INDX_MASK);
	wr32(hw, I40E_QTX_CTL(pf_q), qtx_ctl);
	i40e_flush(hw);

	clear_bit(__I40E_HANG_CHECK_ARMED, &ring->state);

	/* cache tail off for easier writes later */
	ring->tail = hw->hw_addr + I40E_QTX_TAIL(pf_q);

	return 0;
}

/**
 * i40e_configure_rx_ring - Configure a receive ring context
 * @ring: The Rx ring to configure
 *
 * Configure the Rx descriptor ring in the HMC context.
 **/
static int i40e_configure_rx_ring(struct i40e_ring *ring)
{
	struct i40e_vsi *vsi = ring->vsi;
	u32 chain_len = vsi->back->hw.func_caps.rx_buf_chain_len;
	u16 pf_q = vsi->base_queue + ring->queue_index;
	struct i40e_hw *hw = &vsi->back->hw;
	struct i40e_hmc_obj_rxq rx_ctx;
	i40e_status err = 0;

	ring->state = 0;

	/* clear the context structure first */
	memset(&rx_ctx, 0, sizeof(rx_ctx));

	ring->rx_buf_len = vsi->rx_buf_len;
	ring->rx_hdr_len = vsi->rx_hdr_len;

	rx_ctx.dbuff = ring->rx_buf_len >> I40E_RXQ_CTX_DBUFF_SHIFT;
	rx_ctx.hbuff = ring->rx_hdr_len >> I40E_RXQ_CTX_HBUFF_SHIFT;

	rx_ctx.base = (ring->dma / 128);
	rx_ctx.qlen = ring->count;

	if (vsi->back->flags & I40E_FLAG_16BYTE_RX_DESC_ENABLED) {
		set_ring_16byte_desc_enabled(ring);
		rx_ctx.dsize = 0;
	} else {
		rx_ctx.dsize = 1;
	}

	rx_ctx.dtype = vsi->dtype;
	if (vsi->dtype) {
		set_ring_ps_enabled(ring);
		rx_ctx.hsplit_0 = I40E_RX_SPLIT_L2      |
				  I40E_RX_SPLIT_IP      |
				  I40E_RX_SPLIT_TCP_UDP |
				  I40E_RX_SPLIT_SCTP;
	} else {
		rx_ctx.hsplit_0 = 0;
	}

	rx_ctx.rxmax = min_t(u16, vsi->max_frame,
				  (chain_len * ring->rx_buf_len));
	rx_ctx.tphrdesc_ena = 1;
	rx_ctx.tphwdesc_ena = 1;
	rx_ctx.tphdata_ena = 1;
	rx_ctx.tphhead_ena = 1;
	rx_ctx.lrxqthresh = 2;
	rx_ctx.crcstrip = 1;
	rx_ctx.l2tsel = 1;
	rx_ctx.showiv = 1;

	/* clear the context in the HMC */
	err = i40e_clear_lan_rx_queue_context(hw, pf_q);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to clear LAN Rx queue context on Rx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* set the context in the HMC */
	err = i40e_set_lan_rx_queue_context(hw, pf_q, &rx_ctx);
	if (err) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed to set LAN Rx queue context on Rx ring %d (pf_q %d), error: %d\n",
			 ring->queue_index, pf_q, err);
		return -ENOMEM;
	}

	/* cache tail for quicker writes, and clear the reg before use */
	ring->tail = hw->hw_addr + I40E_QRX_TAIL(pf_q);
	writel(0, ring->tail);

	i40e_alloc_rx_buffers(ring, I40E_DESC_UNUSED(ring));

	return 0;
}

/**
 * i40e_vsi_configure_tx - Configure the VSI for Tx
 * @vsi: VSI structure describing this set of rings and resources
 *
 * Configure the Tx VSI for operation.
 **/
static int i40e_vsi_configure_tx(struct i40e_vsi *vsi)
{
	int err = 0;
	u16 i;

	for (i = 0; (i < vsi->num_queue_pairs) && !err; i++)
		err = i40e_configure_tx_ring(vsi->tx_rings[i]);

	return err;
}

/**
 * i40e_vsi_configure_rx - Configure the VSI for Rx
 * @vsi: the VSI being configured
 *
 * Configure the Rx VSI for operation.
 **/
static int i40e_vsi_configure_rx(struct i40e_vsi *vsi)
{
	int err = 0;
	u16 i;

	if (vsi->netdev && (vsi->netdev->mtu > ETH_DATA_LEN))
		vsi->max_frame = vsi->netdev->mtu + ETH_HLEN
			       + ETH_FCS_LEN + VLAN_HLEN;
	else
		vsi->max_frame = I40E_RXBUFFER_2048;

	/* figure out correct receive buffer length */
	switch (vsi->back->flags & (I40E_FLAG_RX_1BUF_ENABLED |
				    I40E_FLAG_RX_PS_ENABLED)) {
	case I40E_FLAG_RX_1BUF_ENABLED:
		vsi->rx_hdr_len = 0;
		vsi->rx_buf_len = vsi->max_frame;
		vsi->dtype = I40E_RX_DTYPE_NO_SPLIT;
		break;
	case I40E_FLAG_RX_PS_ENABLED:
		vsi->rx_hdr_len = I40E_RX_HDR_SIZE;
		vsi->rx_buf_len = I40E_RXBUFFER_2048;
		vsi->dtype = I40E_RX_DTYPE_HEADER_SPLIT;
		break;
	default:
		vsi->rx_hdr_len = I40E_RX_HDR_SIZE;
		vsi->rx_buf_len = I40E_RXBUFFER_2048;
		vsi->dtype = I40E_RX_DTYPE_SPLIT_ALWAYS;
		break;
	}

	/* round up for the chip's needs */
	vsi->rx_hdr_len = ALIGN(vsi->rx_hdr_len,
				(1 << I40E_RXQ_CTX_HBUFF_SHIFT));
	vsi->rx_buf_len = ALIGN(vsi->rx_buf_len,
				(1 << I40E_RXQ_CTX_DBUFF_SHIFT));

	/* set up individual rings */
	for (i = 0; i < vsi->num_queue_pairs && !err; i++)
		err = i40e_configure_rx_ring(vsi->rx_rings[i]);

	return err;
}

/**
 * i40e_vsi_config_dcb_rings - Update rings to reflect DCB TC
 * @vsi: ptr to the VSI
 **/
static void i40e_vsi_config_dcb_rings(struct i40e_vsi *vsi)
{
	u16 qoffset, qcount;
	int i, n;

	if (!(vsi->back->flags & I40E_FLAG_DCB_ENABLED))
		return;

	for (n = 0; n < I40E_MAX_TRAFFIC_CLASS; n++) {
		if (!(vsi->tc_config.enabled_tc & (1 << n)))
			continue;

		qoffset = vsi->tc_config.tc_info[n].qoffset;
		qcount = vsi->tc_config.tc_info[n].qcount;
		for (i = qoffset; i < (qoffset + qcount); i++) {
			struct i40e_ring *rx_ring = vsi->rx_rings[i];
			struct i40e_ring *tx_ring = vsi->tx_rings[i];
			rx_ring->dcb_tc = n;
			tx_ring->dcb_tc = n;
		}
	}
}

/**
 * i40e_set_vsi_rx_mode - Call set_rx_mode on a VSI
 * @vsi: ptr to the VSI
 **/
static void i40e_set_vsi_rx_mode(struct i40e_vsi *vsi)
{
	if (vsi->netdev)
		i40e_set_rx_mode(vsi->netdev);
}

/**
 * i40e_vsi_configure - Set up the VSI for action
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_configure(struct i40e_vsi *vsi)
{
	int err;

	i40e_set_vsi_rx_mode(vsi);
	i40e_restore_vlan(vsi);
	i40e_vsi_config_dcb_rings(vsi);
	err = i40e_vsi_configure_tx(vsi);
	if (!err)
		err = i40e_vsi_configure_rx(vsi);

	return err;
}

/**
 * i40e_vsi_configure_msix - MSIX mode Interrupt Config in the HW
 * @vsi: the VSI being configured
 **/
static void i40e_vsi_configure_msix(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_q_vector *q_vector;
	struct i40e_hw *hw = &pf->hw;
	u16 vector;
	int i, q;
	u32 val;
	u32 qp;

	/* The interrupt indexing is offset by 1 in the PFINT_ITRn
	 * and PFINT_LNKLSTn registers, e.g.:
	 *   PFINT_ITRn[0..n-1] gets msix-1..msix-n  (qpair interrupts)
	 */
	qp = vsi->base_queue;
	vector = vsi->base_vector;
	for (i = 0; i < vsi->num_q_vectors; i++, vector++) {
		q_vector = vsi->q_vectors[i];
		q_vector->rx.itr = ITR_TO_REG(vsi->rx_itr_setting);
		q_vector->rx.latency_range = I40E_LOW_LATENCY;
		wr32(hw, I40E_PFINT_ITRN(I40E_RX_ITR, vector - 1),
		     q_vector->rx.itr);
		q_vector->tx.itr = ITR_TO_REG(vsi->tx_itr_setting);
		q_vector->tx.latency_range = I40E_LOW_LATENCY;
		wr32(hw, I40E_PFINT_ITRN(I40E_TX_ITR, vector - 1),
		     q_vector->tx.itr);

		/* Linked list for the queuepairs assigned to this vector */
		wr32(hw, I40E_PFINT_LNKLSTN(vector - 1), qp);
		for (q = 0; q < q_vector->num_ringpairs; q++) {
			val = I40E_QINT_RQCTL_CAUSE_ENA_MASK |
			      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT)  |
			      (vector      << I40E_QINT_RQCTL_MSIX_INDX_SHIFT) |
			      (qp          << I40E_QINT_RQCTL_NEXTQ_INDX_SHIFT)|
			      (I40E_QUEUE_TYPE_TX
				      << I40E_QINT_RQCTL_NEXTQ_TYPE_SHIFT);

			wr32(hw, I40E_QINT_RQCTL(qp), val);

			val = I40E_QINT_TQCTL_CAUSE_ENA_MASK |
			      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT)  |
			      (vector      << I40E_QINT_TQCTL_MSIX_INDX_SHIFT) |
			      ((qp+1)      << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT)|
			      (I40E_QUEUE_TYPE_RX
				      << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

			/* Terminate the linked list */
			if (q == (q_vector->num_ringpairs - 1))
				val |= (I40E_QUEUE_END_OF_LIST
					   << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);

			wr32(hw, I40E_QINT_TQCTL(qp), val);
			qp++;
		}
	}

	i40e_flush(hw);
}

/**
 * i40e_enable_misc_int_causes - enable the non-queue interrupts
 * @hw: ptr to the hardware info
 **/
static void i40e_enable_misc_int_causes(struct i40e_hw *hw)
{
	u32 val;

	/* clear things first */
	wr32(hw, I40E_PFINT_ICR0_ENA, 0);  /* disable all */
	rd32(hw, I40E_PFINT_ICR0);         /* read to clear */

	val = I40E_PFINT_ICR0_ENA_ECC_ERR_MASK       |
	      I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK    |
	      I40E_PFINT_ICR0_ENA_GRST_MASK          |
	      I40E_PFINT_ICR0_ENA_PCI_EXCEPTION_MASK |
	      I40E_PFINT_ICR0_ENA_GPIO_MASK          |
	      I40E_PFINT_ICR0_ENA_STORM_DETECT_MASK  |
	      I40E_PFINT_ICR0_ENA_HMC_ERR_MASK       |
	      I40E_PFINT_ICR0_ENA_VFLR_MASK          |
	      I40E_PFINT_ICR0_ENA_ADMINQ_MASK;

	wr32(hw, I40E_PFINT_ICR0_ENA, val);

	/* SW_ITR_IDX = 0, but don't change INTENA */
	wr32(hw, I40E_PFINT_DYN_CTL0, I40E_PFINT_DYN_CTLN_SW_ITR_INDX_MASK |
					I40E_PFINT_DYN_CTLN_INTENA_MSK_MASK);

	/* OTHER_ITR_IDX = 0 */
	wr32(hw, I40E_PFINT_STAT_CTL0, 0);
}

/**
 * i40e_configure_msi_and_legacy - Legacy mode interrupt config in the HW
 * @vsi: the VSI being configured
 **/
static void i40e_configure_msi_and_legacy(struct i40e_vsi *vsi)
{
	struct i40e_q_vector *q_vector = vsi->q_vectors[0];
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	/* set the ITR configuration */
	q_vector->rx.itr = ITR_TO_REG(vsi->rx_itr_setting);
	q_vector->rx.latency_range = I40E_LOW_LATENCY;
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), q_vector->rx.itr);
	q_vector->tx.itr = ITR_TO_REG(vsi->tx_itr_setting);
	q_vector->tx.latency_range = I40E_LOW_LATENCY;
	wr32(hw, I40E_PFINT_ITR0(I40E_TX_ITR), q_vector->tx.itr);

	i40e_enable_misc_int_causes(hw);

	/* FIRSTQ_INDX = 0, FIRSTQ_TYPE = 0 (rx) */
	wr32(hw, I40E_PFINT_LNKLST0, 0);

	/* Associate the queue pair to the vector and enable the q int */
	val = I40E_QINT_RQCTL_CAUSE_ENA_MASK		      |
	      (I40E_RX_ITR << I40E_QINT_RQCTL_ITR_INDX_SHIFT) |
	      (I40E_QUEUE_TYPE_TX << I40E_QINT_TQCTL_NEXTQ_TYPE_SHIFT);

	wr32(hw, I40E_QINT_RQCTL(0), val);

	val = I40E_QINT_TQCTL_CAUSE_ENA_MASK		      |
	      (I40E_TX_ITR << I40E_QINT_TQCTL_ITR_INDX_SHIFT) |
	      (I40E_QUEUE_END_OF_LIST << I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT);

	wr32(hw, I40E_QINT_TQCTL(0), val);
	i40e_flush(hw);
}

/**
 * i40e_irq_dynamic_enable_icr0 - Enable default interrupt generation for icr0
 * @pf: board private structure
 **/
void i40e_irq_dynamic_enable_icr0(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	val = I40E_PFINT_DYN_CTL0_INTENA_MASK   |
	      I40E_PFINT_DYN_CTL0_CLEARPBA_MASK |
	      (I40E_ITR_NONE << I40E_PFINT_DYN_CTL0_ITR_INDX_SHIFT);

	wr32(hw, I40E_PFINT_DYN_CTL0, val);
	i40e_flush(hw);
}

/**
 * i40e_irq_dynamic_enable - Enable default interrupt generation settings
 * @vsi: pointer to a vsi
 * @vector: enable a particular Hw Interrupt vector
 **/
void i40e_irq_dynamic_enable(struct i40e_vsi *vsi, int vector)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u32 val;

	val = I40E_PFINT_DYN_CTLN_INTENA_MASK |
	      I40E_PFINT_DYN_CTLN_CLEARPBA_MASK |
	      (I40E_ITR_NONE << I40E_PFINT_DYN_CTLN_ITR_INDX_SHIFT);
	wr32(hw, I40E_PFINT_DYN_CTLN(vector - 1), val);
	/* skip the flush */
}

/**
 * i40e_msix_clean_rings - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 **/
static irqreturn_t i40e_msix_clean_rings(int irq, void *data)
{
	struct i40e_q_vector *q_vector = data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

/**
 * i40e_fdir_clean_rings - Interrupt Handler for FDIR rings
 * @irq: interrupt number
 * @data: pointer to a q_vector
 **/
static irqreturn_t i40e_fdir_clean_rings(int irq, void *data)
{
	struct i40e_q_vector *q_vector = data;

	if (!q_vector->tx.ring && !q_vector->rx.ring)
		return IRQ_HANDLED;

	pr_info("fdir ring cleaning needed\n");

	return IRQ_HANDLED;
}

/**
 * i40e_vsi_request_irq_msix - Initialize MSI-X interrupts
 * @vsi: the VSI being configured
 * @basename: name for the vector
 *
 * Allocates MSI-X vectors and requests interrupts from the kernel.
 **/
static int i40e_vsi_request_irq_msix(struct i40e_vsi *vsi, char *basename)
{
	int q_vectors = vsi->num_q_vectors;
	struct i40e_pf *pf = vsi->back;
	int base = vsi->base_vector;
	int rx_int_idx = 0;
	int tx_int_idx = 0;
	int vector, err;

	for (vector = 0; vector < q_vectors; vector++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[vector];

		if (q_vector->tx.ring && q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "TxRx", rx_int_idx++);
			tx_int_idx++;
		} else if (q_vector->rx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "rx", rx_int_idx++);
		} else if (q_vector->tx.ring) {
			snprintf(q_vector->name, sizeof(q_vector->name) - 1,
				 "%s-%s-%d", basename, "tx", tx_int_idx++);
		} else {
			/* skip this unused q_vector */
			continue;
		}
		err = request_irq(pf->msix_entries[base + vector].vector,
				  vsi->irq_handler,
				  0,
				  q_vector->name,
				  q_vector);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "%s: request_irq failed, error: %d\n",
				 __func__, err);
			goto free_queue_irqs;
		}
		/* assign the mask for this irq */
		irq_set_affinity_hint(pf->msix_entries[base + vector].vector,
				      &q_vector->affinity_mask);
	}

	return 0;

free_queue_irqs:
	while (vector) {
		vector--;
		irq_set_affinity_hint(pf->msix_entries[base + vector].vector,
				      NULL);
		free_irq(pf->msix_entries[base + vector].vector,
			 &(vsi->q_vectors[vector]));
	}
	return err;
}

/**
 * i40e_vsi_disable_irq - Mask off queue interrupt generation on the VSI
 * @vsi: the VSI being un-configured
 **/
static void i40e_vsi_disable_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int base = vsi->base_vector;
	int i;

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		wr32(hw, I40E_QINT_TQCTL(vsi->tx_rings[i]->reg_idx), 0);
		wr32(hw, I40E_QINT_RQCTL(vsi->rx_rings[i]->reg_idx), 0);
	}

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = vsi->base_vector;
		     i < (vsi->num_q_vectors + vsi->base_vector); i++)
			wr32(hw, I40E_PFINT_DYN_CTLN(i - 1), 0);

		i40e_flush(hw);
		for (i = 0; i < vsi->num_q_vectors; i++)
			synchronize_irq(pf->msix_entries[i + base].vector);
	} else {
		/* Legacy and MSI mode - this stops all interrupt handling */
		wr32(hw, I40E_PFINT_ICR0_ENA, 0);
		wr32(hw, I40E_PFINT_DYN_CTL0, 0);
		i40e_flush(hw);
		synchronize_irq(pf->pdev->irq);
	}
}

/**
 * i40e_vsi_enable_irq - Enable IRQ for the given VSI
 * @vsi: the VSI being configured
 **/
static int i40e_vsi_enable_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = vsi->base_vector;
		     i < (vsi->num_q_vectors + vsi->base_vector); i++)
			i40e_irq_dynamic_enable(vsi, i);
	} else {
		i40e_irq_dynamic_enable_icr0(pf);
	}

	i40e_flush(&pf->hw);
	return 0;
}

/**
 * i40e_stop_misc_vector - Stop the vector that handles non-queue events
 * @pf: board private structure
 **/
static void i40e_stop_misc_vector(struct i40e_pf *pf)
{
	/* Disable ICR 0 */
	wr32(&pf->hw, I40E_PFINT_ICR0_ENA, 0);
	i40e_flush(&pf->hw);
}

/**
 * i40e_intr - MSI/Legacy and non-queue interrupt handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 *
 * This is the handler used for all MSI/Legacy interrupts, and deals
 * with both queue and non-queue interrupts.  This is also used in
 * MSIX mode to handle the non-queue interrupts.
 **/
static irqreturn_t i40e_intr(int irq, void *data)
{
	struct i40e_pf *pf = (struct i40e_pf *)data;
	struct i40e_hw *hw = &pf->hw;
	u32 icr0, icr0_remaining;
	u32 val, ena_mask;

	icr0 = rd32(hw, I40E_PFINT_ICR0);

	val = rd32(hw, I40E_PFINT_DYN_CTL0);
	val = val | I40E_PFINT_DYN_CTL0_CLEARPBA_MASK;
	wr32(hw, I40E_PFINT_DYN_CTL0, val);

	/* if sharing a legacy IRQ, we might get called w/o an intr pending */
	if ((icr0 & I40E_PFINT_ICR0_INTEVENT_MASK) == 0)
		return IRQ_NONE;

	ena_mask = rd32(hw, I40E_PFINT_ICR0_ENA);

	/* only q0 is used in MSI/Legacy mode, and none are used in MSIX */
	if (icr0 & I40E_PFINT_ICR0_QUEUE_0_MASK) {

		/* temporarily disable queue cause for NAPI processing */
		u32 qval = rd32(hw, I40E_QINT_RQCTL(0));
		qval &= ~I40E_QINT_RQCTL_CAUSE_ENA_MASK;
		wr32(hw, I40E_QINT_RQCTL(0), qval);

		qval = rd32(hw, I40E_QINT_TQCTL(0));
		qval &= ~I40E_QINT_TQCTL_CAUSE_ENA_MASK;
		wr32(hw, I40E_QINT_TQCTL(0), qval);

		if (!test_bit(__I40E_DOWN, &pf->state))
			napi_schedule(&pf->vsi[pf->lan_vsi]->q_vectors[0]->napi);
	}

	if (icr0 & I40E_PFINT_ICR0_ADMINQ_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
		set_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_MAL_DETECT_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
		set_bit(__I40E_MDD_EVENT_PENDING, &pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_VFLR_MASK) {
		ena_mask &= ~I40E_PFINT_ICR0_ENA_VFLR_MASK;
		set_bit(__I40E_VFLR_EVENT_PENDING, &pf->state);
	}

	if (icr0 & I40E_PFINT_ICR0_GRST_MASK) {
		if (!test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state))
			set_bit(__I40E_RESET_INTR_RECEIVED, &pf->state);
		ena_mask &= ~I40E_PFINT_ICR0_ENA_GRST_MASK;
		val = rd32(hw, I40E_GLGEN_RSTAT);
		val = (val & I40E_GLGEN_RSTAT_RESET_TYPE_MASK)
		       >> I40E_GLGEN_RSTAT_RESET_TYPE_SHIFT;
		if (val & I40E_RESET_CORER)
			pf->corer_count++;
		else if (val & I40E_RESET_GLOBR)
			pf->globr_count++;
		else if (val & I40E_RESET_EMPR)
			pf->empr_count++;
	}

	/* If a critical error is pending we have no choice but to reset the
	 * device.
	 * Report and mask out any remaining unexpected interrupts.
	 */
	icr0_remaining = icr0 & ena_mask;
	if (icr0_remaining) {
		dev_info(&pf->pdev->dev, "unhandled interrupt icr0=0x%08x\n",
			 icr0_remaining);
		if ((icr0_remaining & I40E_PFINT_ICR0_HMC_ERR_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_PE_CRITERR_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_PCI_EXCEPTION_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_ECC_ERR_MASK) ||
		    (icr0_remaining & I40E_PFINT_ICR0_MAL_DETECT_MASK)) {
			if (icr0 & I40E_PFINT_ICR0_HMC_ERR_MASK) {
				dev_info(&pf->pdev->dev, "HMC error interrupt\n");
			} else {
				dev_info(&pf->pdev->dev, "device will be reset\n");
				set_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
				i40e_service_event_schedule(pf);
			}
		}
		ena_mask &= ~icr0_remaining;
	}

	/* re-enable interrupt causes */
	wr32(hw, I40E_PFINT_ICR0_ENA, ena_mask);
	if (!test_bit(__I40E_DOWN, &pf->state)) {
		i40e_service_event_schedule(pf);
		i40e_irq_dynamic_enable_icr0(pf);
	}

	return IRQ_HANDLED;
}

/**
 * i40e_map_vector_to_qp - Assigns the queue pair to the vector
 * @vsi: the VSI being configured
 * @v_idx: vector index
 * @qp_idx: queue pair index
 **/
static void map_vector_to_qp(struct i40e_vsi *vsi, int v_idx, int qp_idx)
{
	struct i40e_q_vector *q_vector = vsi->q_vectors[v_idx];
	struct i40e_ring *tx_ring = vsi->tx_rings[qp_idx];
	struct i40e_ring *rx_ring = vsi->rx_rings[qp_idx];

	tx_ring->q_vector = q_vector;
	tx_ring->next = q_vector->tx.ring;
	q_vector->tx.ring = tx_ring;
	q_vector->tx.count++;

	rx_ring->q_vector = q_vector;
	rx_ring->next = q_vector->rx.ring;
	q_vector->rx.ring = rx_ring;
	q_vector->rx.count++;
}

/**
 * i40e_vsi_map_rings_to_vectors - Maps descriptor rings to vectors
 * @vsi: the VSI being configured
 *
 * This function maps descriptor rings to the queue-specific vectors
 * we were allotted through the MSI-X enabling code.  Ideally, we'd have
 * one vector per queue pair, but on a constrained vector budget, we
 * group the queue pairs as "efficiently" as possible.
 **/
static void i40e_vsi_map_rings_to_vectors(struct i40e_vsi *vsi)
{
	int qp_remaining = vsi->num_queue_pairs;
	int q_vectors = vsi->num_q_vectors;
	int num_ringpairs;
	int v_start = 0;
	int qp_idx = 0;

	/* If we don't have enough vectors for a 1-to-1 mapping, we'll have to
	 * group them so there are multiple queues per vector.
	 */
	for (; v_start < q_vectors && qp_remaining; v_start++) {
		struct i40e_q_vector *q_vector = vsi->q_vectors[v_start];

		num_ringpairs = DIV_ROUND_UP(qp_remaining, q_vectors - v_start);

		q_vector->num_ringpairs = num_ringpairs;

		q_vector->rx.count = 0;
		q_vector->tx.count = 0;
		q_vector->rx.ring = NULL;
		q_vector->tx.ring = NULL;

		while (num_ringpairs--) {
			map_vector_to_qp(vsi, v_start, qp_idx);
			qp_idx++;
			qp_remaining--;
		}
	}
}

/**
 * i40e_vsi_request_irq - Request IRQ from the OS
 * @vsi: the VSI being configured
 * @basename: name for the vector
 **/
static int i40e_vsi_request_irq(struct i40e_vsi *vsi, char *basename)
{
	struct i40e_pf *pf = vsi->back;
	int err;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		err = i40e_vsi_request_irq_msix(vsi, basename);
	else if (pf->flags & I40E_FLAG_MSI_ENABLED)
		err = request_irq(pf->pdev->irq, i40e_intr, 0,
				  pf->misc_int_name, pf);
	else
		err = request_irq(pf->pdev->irq, i40e_intr, IRQF_SHARED,
				  pf->misc_int_name, pf);

	if (err)
		dev_info(&pf->pdev->dev, "request_irq failed, Error %d\n", err);

	return err;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/**
 * i40e_netpoll - A Polling 'interrupt'handler
 * @netdev: network interface device structure
 *
 * This is used by netconsole to send skbs without having to re-enable
 * interrupts.  It's not called while the normal interrupt routine is executing.
 **/
static void i40e_netpoll(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	int i;

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, &vsi->state))
		return;

	pf->flags |= I40E_FLAG_IN_NETPOLL;
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		for (i = 0; i < vsi->num_q_vectors; i++)
			i40e_msix_clean_rings(0, vsi->q_vectors[i]);
	} else {
		i40e_intr(pf->pdev->irq, netdev);
	}
	pf->flags &= ~I40E_FLAG_IN_NETPOLL;
}
#endif

/**
 * i40e_vsi_control_tx - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
static int i40e_vsi_control_tx(struct i40e_vsi *vsi, bool enable)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int i, j, pf_q;
	u32 tx_reg;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		j = 1000;
		do {
			usleep_range(1000, 2000);
			tx_reg = rd32(hw, I40E_QTX_ENA(pf_q));
		} while (j-- && ((tx_reg >> I40E_QTX_ENA_QENA_REQ_SHIFT)
			       ^ (tx_reg >> I40E_QTX_ENA_QENA_STAT_SHIFT)) & 1);

		if (enable) {
			/* is STAT set ? */
			if ((tx_reg & I40E_QTX_ENA_QENA_STAT_MASK)) {
				dev_info(&pf->pdev->dev,
					 "Tx %d already enabled\n", i);
				continue;
			}
		} else {
			/* is !STAT set ? */
			if (!(tx_reg & I40E_QTX_ENA_QENA_STAT_MASK)) {
				dev_info(&pf->pdev->dev,
					 "Tx %d already disabled\n", i);
				continue;
			}
		}

		/* turn on/off the queue */
		if (enable)
			tx_reg |= I40E_QTX_ENA_QENA_REQ_MASK |
				  I40E_QTX_ENA_QENA_STAT_MASK;
		else
			tx_reg &= ~I40E_QTX_ENA_QENA_REQ_MASK;

		wr32(hw, I40E_QTX_ENA(pf_q), tx_reg);

		/* wait for the change to finish */
		for (j = 0; j < 10; j++) {
			tx_reg = rd32(hw, I40E_QTX_ENA(pf_q));
			if (enable) {
				if ((tx_reg & I40E_QTX_ENA_QENA_STAT_MASK))
					break;
			} else {
				if (!(tx_reg & I40E_QTX_ENA_QENA_STAT_MASK))
					break;
			}

			udelay(10);
		}
		if (j >= 10) {
			dev_info(&pf->pdev->dev, "Tx ring %d %sable timeout\n",
				 pf_q, (enable ? "en" : "dis"));
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/**
 * i40e_vsi_control_rx - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
static int i40e_vsi_control_rx(struct i40e_vsi *vsi, bool enable)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int i, j, pf_q;
	u32 rx_reg;

	pf_q = vsi->base_queue;
	for (i = 0; i < vsi->num_queue_pairs; i++, pf_q++) {
		j = 1000;
		do {
			usleep_range(1000, 2000);
			rx_reg = rd32(hw, I40E_QRX_ENA(pf_q));
		} while (j-- && ((rx_reg >> I40E_QRX_ENA_QENA_REQ_SHIFT)
			       ^ (rx_reg >> I40E_QRX_ENA_QENA_STAT_SHIFT)) & 1);

		if (enable) {
			/* is STAT set ? */
			if ((rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
				continue;
		} else {
			/* is !STAT set ? */
			if (!(rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
				continue;
		}

		/* turn on/off the queue */
		if (enable)
			rx_reg |= I40E_QRX_ENA_QENA_REQ_MASK |
				  I40E_QRX_ENA_QENA_STAT_MASK;
		else
			rx_reg &= ~(I40E_QRX_ENA_QENA_REQ_MASK |
				  I40E_QRX_ENA_QENA_STAT_MASK);
		wr32(hw, I40E_QRX_ENA(pf_q), rx_reg);

		/* wait for the change to finish */
		for (j = 0; j < 10; j++) {
			rx_reg = rd32(hw, I40E_QRX_ENA(pf_q));

			if (enable) {
				if ((rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
					break;
			} else {
				if (!(rx_reg & I40E_QRX_ENA_QENA_STAT_MASK))
					break;
			}

			udelay(10);
		}
		if (j >= 10) {
			dev_info(&pf->pdev->dev, "Rx ring %d %sable timeout\n",
				 pf_q, (enable ? "en" : "dis"));
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/**
 * i40e_vsi_control_rings - Start or stop a VSI's rings
 * @vsi: the VSI being configured
 * @enable: start or stop the rings
 **/
static int i40e_vsi_control_rings(struct i40e_vsi *vsi, bool request)
{
	int ret;

	/* do rx first for enable and last for disable */
	if (request) {
		ret = i40e_vsi_control_rx(vsi, request);
		if (ret)
			return ret;
		ret = i40e_vsi_control_tx(vsi, request);
	} else {
		ret = i40e_vsi_control_tx(vsi, request);
		if (ret)
			return ret;
		ret = i40e_vsi_control_rx(vsi, request);
	}

	return ret;
}

/**
 * i40e_vsi_free_irq - Free the irq association with the OS
 * @vsi: the VSI being configured
 **/
static void i40e_vsi_free_irq(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	int base = vsi->base_vector;
	u32 val, qp;
	int i;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		if (!vsi->q_vectors)
			return;

		for (i = 0; i < vsi->num_q_vectors; i++) {
			u16 vector = i + base;

			/* free only the irqs that were actually requested */
			if (vsi->q_vectors[i]->num_ringpairs == 0)
				continue;

			/* clear the affinity_mask in the IRQ descriptor */
			irq_set_affinity_hint(pf->msix_entries[vector].vector,
					      NULL);
			free_irq(pf->msix_entries[vector].vector,
				 vsi->q_vectors[i]);

			/* Tear down the interrupt queue link list
			 *
			 * We know that they come in pairs and always
			 * the Rx first, then the Tx.  To clear the
			 * link list, stick the EOL value into the
			 * next_q field of the registers.
			 */
			val = rd32(hw, I40E_PFINT_LNKLSTN(vector - 1));
			qp = (val & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK)
				>> I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
			val |= I40E_QUEUE_END_OF_LIST
				<< I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
			wr32(hw, I40E_PFINT_LNKLSTN(vector - 1), val);

			while (qp != I40E_QUEUE_END_OF_LIST) {
				u32 next;

				val = rd32(hw, I40E_QINT_RQCTL(qp));

				val &= ~(I40E_QINT_RQCTL_MSIX_INDX_MASK  |
					 I40E_QINT_RQCTL_MSIX0_INDX_MASK |
					 I40E_QINT_RQCTL_CAUSE_ENA_MASK  |
					 I40E_QINT_RQCTL_INTEVENT_MASK);

				val |= (I40E_QINT_RQCTL_ITR_INDX_MASK |
					 I40E_QINT_RQCTL_NEXTQ_INDX_MASK);

				wr32(hw, I40E_QINT_RQCTL(qp), val);

				val = rd32(hw, I40E_QINT_TQCTL(qp));

				next = (val & I40E_QINT_TQCTL_NEXTQ_INDX_MASK)
					>> I40E_QINT_TQCTL_NEXTQ_INDX_SHIFT;

				val &= ~(I40E_QINT_TQCTL_MSIX_INDX_MASK  |
					 I40E_QINT_TQCTL_MSIX0_INDX_MASK |
					 I40E_QINT_TQCTL_CAUSE_ENA_MASK  |
					 I40E_QINT_TQCTL_INTEVENT_MASK);

				val |= (I40E_QINT_TQCTL_ITR_INDX_MASK |
					 I40E_QINT_TQCTL_NEXTQ_INDX_MASK);

				wr32(hw, I40E_QINT_TQCTL(qp), val);
				qp = next;
			}
		}
	} else {
		free_irq(pf->pdev->irq, pf);

		val = rd32(hw, I40E_PFINT_LNKLST0);
		qp = (val & I40E_PFINT_LNKLSTN_FIRSTQ_INDX_MASK)
			>> I40E_PFINT_LNKLSTN_FIRSTQ_INDX_SHIFT;
		val |= I40E_QUEUE_END_OF_LIST
			<< I40E_PFINT_LNKLST0_FIRSTQ_INDX_SHIFT;
		wr32(hw, I40E_PFINT_LNKLST0, val);

		val = rd32(hw, I40E_QINT_RQCTL(qp));
		val &= ~(I40E_QINT_RQCTL_MSIX_INDX_MASK  |
			 I40E_QINT_RQCTL_MSIX0_INDX_MASK |
			 I40E_QINT_RQCTL_CAUSE_ENA_MASK  |
			 I40E_QINT_RQCTL_INTEVENT_MASK);

		val |= (I40E_QINT_RQCTL_ITR_INDX_MASK |
			I40E_QINT_RQCTL_NEXTQ_INDX_MASK);

		wr32(hw, I40E_QINT_RQCTL(qp), val);

		val = rd32(hw, I40E_QINT_TQCTL(qp));

		val &= ~(I40E_QINT_TQCTL_MSIX_INDX_MASK  |
			 I40E_QINT_TQCTL_MSIX0_INDX_MASK |
			 I40E_QINT_TQCTL_CAUSE_ENA_MASK  |
			 I40E_QINT_TQCTL_INTEVENT_MASK);

		val |= (I40E_QINT_TQCTL_ITR_INDX_MASK |
			I40E_QINT_TQCTL_NEXTQ_INDX_MASK);

		wr32(hw, I40E_QINT_TQCTL(qp), val);
	}
}

/**
 * i40e_free_q_vector - Free memory allocated for specific interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: Index of vector to be freed
 *
 * This function frees the memory allocated to the q_vector.  In addition if
 * NAPI is enabled it will delete any references to the NAPI struct prior
 * to freeing the q_vector.
 **/
static void i40e_free_q_vector(struct i40e_vsi *vsi, int v_idx)
{
	struct i40e_q_vector *q_vector = vsi->q_vectors[v_idx];
	struct i40e_ring *ring;

	if (!q_vector)
		return;

	/* disassociate q_vector from rings */
	i40e_for_each_ring(ring, q_vector->tx)
		ring->q_vector = NULL;

	i40e_for_each_ring(ring, q_vector->rx)
		ring->q_vector = NULL;

	/* only VSI w/ an associated netdev is set up w/ NAPI */
	if (vsi->netdev)
		netif_napi_del(&q_vector->napi);

	vsi->q_vectors[v_idx] = NULL;

	kfree_rcu(q_vector, rcu);
}

/**
 * i40e_vsi_free_q_vectors - Free memory allocated for interrupt vectors
 * @vsi: the VSI being un-configured
 *
 * This frees the memory allocated to the q_vectors and
 * deletes references to the NAPI struct.
 **/
static void i40e_vsi_free_q_vectors(struct i40e_vsi *vsi)
{
	int v_idx;

	for (v_idx = 0; v_idx < vsi->num_q_vectors; v_idx++)
		i40e_free_q_vector(vsi, v_idx);
}

/**
 * i40e_reset_interrupt_capability - Disable interrupt setup in OS
 * @pf: board private structure
 **/
static void i40e_reset_interrupt_capability(struct i40e_pf *pf)
{
	/* If we're in Legacy mode, the interrupt was cleaned in vsi_close */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		pci_disable_msix(pf->pdev);
		kfree(pf->msix_entries);
		pf->msix_entries = NULL;
	} else if (pf->flags & I40E_FLAG_MSI_ENABLED) {
		pci_disable_msi(pf->pdev);
	}
	pf->flags &= ~(I40E_FLAG_MSIX_ENABLED | I40E_FLAG_MSI_ENABLED);
}

/**
 * i40e_clear_interrupt_scheme - Clear the current interrupt scheme settings
 * @pf: board private structure
 *
 * We go through and clear interrupt specific resources and reset the structure
 * to pre-load conditions
 **/
static void i40e_clear_interrupt_scheme(struct i40e_pf *pf)
{
	int i;

	i40e_put_lump(pf->irq_pile, 0, I40E_PILE_VALID_BIT-1);
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++)
		if (pf->vsi[i])
			i40e_vsi_free_q_vectors(pf->vsi[i]);
	i40e_reset_interrupt_capability(pf);
}

/**
 * i40e_napi_enable_all - Enable NAPI for all q_vectors in the VSI
 * @vsi: the VSI being configured
 **/
static void i40e_napi_enable_all(struct i40e_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_enable(&vsi->q_vectors[q_idx]->napi);
}

/**
 * i40e_napi_disable_all - Disable NAPI for all q_vectors in the VSI
 * @vsi: the VSI being configured
 **/
static void i40e_napi_disable_all(struct i40e_vsi *vsi)
{
	int q_idx;

	if (!vsi->netdev)
		return;

	for (q_idx = 0; q_idx < vsi->num_q_vectors; q_idx++)
		napi_disable(&vsi->q_vectors[q_idx]->napi);
}

/**
 * i40e_quiesce_vsi - Pause a given VSI
 * @vsi: the VSI being paused
 **/
static void i40e_quiesce_vsi(struct i40e_vsi *vsi)
{
	if (test_bit(__I40E_DOWN, &vsi->state))
		return;

	set_bit(__I40E_NEEDS_RESTART, &vsi->state);
	if (vsi->netdev && netif_running(vsi->netdev)) {
		vsi->netdev->netdev_ops->ndo_stop(vsi->netdev);
	} else {
		set_bit(__I40E_DOWN, &vsi->state);
		i40e_down(vsi);
	}
}

/**
 * i40e_unquiesce_vsi - Resume a given VSI
 * @vsi: the VSI being resumed
 **/
static void i40e_unquiesce_vsi(struct i40e_vsi *vsi)
{
	if (!test_bit(__I40E_NEEDS_RESTART, &vsi->state))
		return;

	clear_bit(__I40E_NEEDS_RESTART, &vsi->state);
	if (vsi->netdev && netif_running(vsi->netdev))
		vsi->netdev->netdev_ops->ndo_open(vsi->netdev);
	else
		i40e_up(vsi);   /* this clears the DOWN bit */
}

/**
 * i40e_pf_quiesce_all_vsi - Pause all VSIs on a PF
 * @pf: the PF
 **/
static void i40e_pf_quiesce_all_vsi(struct i40e_pf *pf)
{
	int v;

	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (pf->vsi[v])
			i40e_quiesce_vsi(pf->vsi[v]);
	}
}

/**
 * i40e_pf_unquiesce_all_vsi - Resume all VSIs on a PF
 * @pf: the PF
 **/
static void i40e_pf_unquiesce_all_vsi(struct i40e_pf *pf)
{
	int v;

	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (pf->vsi[v])
			i40e_unquiesce_vsi(pf->vsi[v]);
	}
}

/**
 * i40e_dcb_get_num_tc -  Get the number of TCs from DCBx config
 * @dcbcfg: the corresponding DCBx configuration structure
 *
 * Return the number of TCs from given DCBx configuration
 **/
static u8 i40e_dcb_get_num_tc(struct i40e_dcbx_config *dcbcfg)
{
	u8 num_tc = 0;
	int i;

	/* Scan the ETS Config Priority Table to find
	 * traffic class enabled for a given priority
	 * and use the traffic class index to get the
	 * number of traffic classes enabled
	 */
	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		if (dcbcfg->etscfg.prioritytable[i] > num_tc)
			num_tc = dcbcfg->etscfg.prioritytable[i];
	}

	/* Traffic class index starts from zero so
	 * increment to return the actual count
	 */
	return num_tc + 1;
}

/**
 * i40e_dcb_get_enabled_tc - Get enabled traffic classes
 * @dcbcfg: the corresponding DCBx configuration structure
 *
 * Query the current DCB configuration and return the number of
 * traffic classes enabled from the given DCBX config
 **/
static u8 i40e_dcb_get_enabled_tc(struct i40e_dcbx_config *dcbcfg)
{
	u8 num_tc = i40e_dcb_get_num_tc(dcbcfg);
	u8 enabled_tc = 1;
	u8 i;

	for (i = 0; i < num_tc; i++)
		enabled_tc |= 1 << i;

	return enabled_tc;
}

/**
 * i40e_pf_get_num_tc - Get enabled traffic classes for PF
 * @pf: PF being queried
 *
 * Return number of traffic classes enabled for the given PF
 **/
static u8 i40e_pf_get_num_tc(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u8 i, enabled_tc;
	u8 num_tc = 0;
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	/* If DCB is not enabled then always in single TC */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return 1;

	/* MFP mode return count of enabled TCs for this PF */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		enabled_tc = pf->hw.func_caps.enabled_tcmap;
		for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
			if (enabled_tc & (1 << i))
				num_tc++;
		}
		return num_tc;
	}

	/* SFP mode will be enabled for all TCs on port */
	return i40e_dcb_get_num_tc(dcbcfg);
}

/**
 * i40e_pf_get_default_tc - Get bitmap for first enabled TC
 * @pf: PF being queried
 *
 * Return a bitmap for first enabled traffic class for this PF.
 **/
static u8 i40e_pf_get_default_tc(struct i40e_pf *pf)
{
	u8 enabled_tc = pf->hw.func_caps.enabled_tcmap;
	u8 i = 0;

	if (!enabled_tc)
		return 0x1; /* TC0 */

	/* Find the first enabled TC */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & (1 << i))
			break;
	}

	return 1 << i;
}

/**
 * i40e_pf_get_pf_tc_map - Get bitmap for enabled traffic classes
 * @pf: PF being queried
 *
 * Return a bitmap for enabled traffic classes for this PF.
 **/
static u8 i40e_pf_get_tc_map(struct i40e_pf *pf)
{
	/* If DCB is not enabled for this PF then just return default TC */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED))
		return i40e_pf_get_default_tc(pf);

	/* MFP mode will have enabled TCs set by FW */
	if (pf->flags & I40E_FLAG_MFP_ENABLED)
		return pf->hw.func_caps.enabled_tcmap;

	/* SFP mode we want PF to be enabled for all TCs */
	return i40e_dcb_get_enabled_tc(&pf->hw.local_dcbx_config);
}

/**
 * i40e_vsi_get_bw_info - Query VSI BW Information
 * @vsi: the VSI being queried
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_vsi_get_bw_info(struct i40e_vsi *vsi)
{
	struct i40e_aqc_query_vsi_ets_sla_config_resp bw_ets_config = {0};
	struct i40e_aqc_query_vsi_bw_config_resp bw_config = {0};
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	i40e_status aq_ret;
	u32 tc_bw_max;
	int i;

	/* Get the VSI level BW configuration */
	aq_ret = i40e_aq_query_vsi_bw_config(hw, vsi->seid, &bw_config, NULL);
	if (aq_ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get pf vsi bw config, err %d, aq_err %d\n",
			 aq_ret, pf->hw.aq.asq_last_status);
		return -EINVAL;
	}

	/* Get the VSI level BW configuration per TC */
	aq_ret = i40e_aq_query_vsi_ets_sla_config(hw, vsi->seid, &bw_ets_config,
					          NULL);
	if (aq_ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get pf vsi ets bw config, err %d, aq_err %d\n",
			 aq_ret, pf->hw.aq.asq_last_status);
		return -EINVAL;
	}

	if (bw_config.tc_valid_bits != bw_ets_config.tc_valid_bits) {
		dev_info(&pf->pdev->dev,
			 "Enabled TCs mismatch from querying VSI BW info 0x%08x 0x%08x\n",
			 bw_config.tc_valid_bits,
			 bw_ets_config.tc_valid_bits);
		/* Still continuing */
	}

	vsi->bw_limit = le16_to_cpu(bw_config.port_bw_limit);
	vsi->bw_max_quanta = bw_config.max_bw;
	tc_bw_max = le16_to_cpu(bw_ets_config.tc_bw_max[0]) |
		    (le16_to_cpu(bw_ets_config.tc_bw_max[1]) << 16);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		vsi->bw_ets_share_credits[i] = bw_ets_config.share_credits[i];
		vsi->bw_ets_limit_credits[i] =
					le16_to_cpu(bw_ets_config.credits[i]);
		/* 3 bits out of 4 for each TC */
		vsi->bw_ets_max_quanta[i] = (u8)((tc_bw_max >> (i*4)) & 0x7);
	}

	return 0;
}

/**
 * i40e_vsi_configure_bw_alloc - Configure VSI BW allocation per TC
 * @vsi: the VSI being configured
 * @enabled_tc: TC bitmap
 * @bw_credits: BW shared credits per TC
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_vsi_configure_bw_alloc(struct i40e_vsi *vsi, u8 enabled_tc,
				       u8 *bw_share)
{
	struct i40e_aqc_configure_vsi_tc_bw_data bw_data;
	i40e_status aq_ret;
	int i;

	bw_data.tc_valid_bits = enabled_tc;
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		bw_data.tc_bw_credits[i] = bw_share[i];

	aq_ret = i40e_aq_config_vsi_tc_bw(&vsi->back->hw, vsi->seid, &bw_data,
					  NULL);
	if (aq_ret) {
		dev_info(&vsi->back->pdev->dev,
			 "%s: AQ command Config VSI BW allocation per TC failed = %d\n",
			 __func__, vsi->back->hw.aq.asq_last_status);
		return -EINVAL;
	}

	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++)
		vsi->info.qs_handle[i] = bw_data.qs_handles[i];

	return 0;
}

/**
 * i40e_vsi_config_netdev_tc - Setup the netdev TC configuration
 * @vsi: the VSI being configured
 * @enabled_tc: TC map to be enabled
 *
 **/
static void i40e_vsi_config_netdev_tc(struct i40e_vsi *vsi, u8 enabled_tc)
{
	struct net_device *netdev = vsi->netdev;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	u8 netdev_tc = 0;
	int i;
	struct i40e_dcbx_config *dcbcfg = &hw->local_dcbx_config;

	if (!netdev)
		return;

	if (!enabled_tc) {
		netdev_reset_tc(netdev);
		return;
	}

	/* Set up actual enabled TCs on the VSI */
	if (netdev_set_num_tc(netdev, vsi->tc_config.numtc))
		return;

	/* set per TC queues for the VSI */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		/* Only set TC queues for enabled tcs
		 *
		 * e.g. For a VSI that has TC0 and TC3 enabled the
		 * enabled_tc bitmap would be 0x00001001; the driver
		 * will set the numtc for netdev as 2 that will be
		 * referenced by the netdev layer as TC 0 and 1.
		 */
		if (vsi->tc_config.enabled_tc & (1 << i))
			netdev_set_tc_queue(netdev,
					vsi->tc_config.tc_info[i].netdev_tc,
					vsi->tc_config.tc_info[i].qcount,
					vsi->tc_config.tc_info[i].qoffset);
	}

	/* Assign UP2TC map for the VSI */
	for (i = 0; i < I40E_MAX_USER_PRIORITY; i++) {
		/* Get the actual TC# for the UP */
		u8 ets_tc = dcbcfg->etscfg.prioritytable[i];
		/* Get the mapped netdev TC# for the UP */
		netdev_tc =  vsi->tc_config.tc_info[ets_tc].netdev_tc;
		netdev_set_prio_tc_map(netdev, i, netdev_tc);
	}
}

/**
 * i40e_vsi_update_queue_map - Update our copy of VSi info with new queue map
 * @vsi: the VSI being configured
 * @ctxt: the ctxt buffer returned from AQ VSI update param command
 **/
static void i40e_vsi_update_queue_map(struct i40e_vsi *vsi,
				      struct i40e_vsi_context *ctxt)
{
	/* copy just the sections touched not the entire info
	 * since not all sections are valid as returned by
	 * update vsi params
	 */
	vsi->info.mapping_flags = ctxt->info.mapping_flags;
	memcpy(&vsi->info.queue_mapping,
	       &ctxt->info.queue_mapping, sizeof(vsi->info.queue_mapping));
	memcpy(&vsi->info.tc_mapping, ctxt->info.tc_mapping,
	       sizeof(vsi->info.tc_mapping));
}

/**
 * i40e_vsi_config_tc - Configure VSI Tx Scheduler for given TC map
 * @vsi: VSI to be configured
 * @enabled_tc: TC bitmap
 *
 * This configures a particular VSI for TCs that are mapped to the
 * given TC bitmap. It uses default bandwidth share for TCs across
 * VSIs to configure TC for a particular VSI.
 *
 * NOTE:
 * It is expected that the VSI queues have been quisced before calling
 * this function.
 **/
static int i40e_vsi_config_tc(struct i40e_vsi *vsi, u8 enabled_tc)
{
	u8 bw_share[I40E_MAX_TRAFFIC_CLASS] = {0};
	struct i40e_vsi_context ctxt;
	int ret = 0;
	int i;

	/* Check if enabled_tc is same as existing or new TCs */
	if (vsi->tc_config.enabled_tc == enabled_tc)
		return ret;

	/* Enable ETS TCs with equal BW Share for now across all VSIs */
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		if (enabled_tc & (1 << i))
			bw_share[i] = 1;
	}

	ret = i40e_vsi_configure_bw_alloc(vsi, enabled_tc, bw_share);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed configuring TC map %d for VSI %d\n",
			 enabled_tc, vsi->seid);
		goto out;
	}

	/* Update Queue Pairs Mapping for currently enabled UPs */
	ctxt.seid = vsi->seid;
	ctxt.pf_num = vsi->back->hw.pf_id;
	ctxt.vf_num = 0;
	ctxt.uplink_seid = vsi->uplink_seid;
	memcpy(&ctxt.info, &vsi->info, sizeof(vsi->info));
	i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, false);

	/* Update the VSI after updating the VSI queue-mapping information */
	ret = i40e_aq_update_vsi_params(&vsi->back->hw, &ctxt, NULL);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "update vsi failed, aq_err=%d\n",
			 vsi->back->hw.aq.asq_last_status);
		goto out;
	}
	/* update the local VSI info with updated queue map */
	i40e_vsi_update_queue_map(vsi, &ctxt);
	vsi->info.valid_sections = 0;

	/* Update current VSI BW information */
	ret = i40e_vsi_get_bw_info(vsi);
	if (ret) {
		dev_info(&vsi->back->pdev->dev,
			 "Failed updating vsi bw info, aq_err=%d\n",
			 vsi->back->hw.aq.asq_last_status);
		goto out;
	}

	/* Update the netdev TC setup */
	i40e_vsi_config_netdev_tc(vsi, enabled_tc);
out:
	return ret;
}

/**
 * i40e_up_complete - Finish the last steps of bringing up a connection
 * @vsi: the VSI being configured
 **/
static int i40e_up_complete(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int err;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		i40e_vsi_configure_msix(vsi);
	else
		i40e_configure_msi_and_legacy(vsi);

	/* start rings */
	err = i40e_vsi_control_rings(vsi, true);
	if (err)
		return err;

	clear_bit(__I40E_DOWN, &vsi->state);
	i40e_napi_enable_all(vsi);
	i40e_vsi_enable_irq(vsi);

	if ((pf->hw.phy.link_info.link_info & I40E_AQ_LINK_UP) &&
	    (vsi->netdev)) {
		netdev_info(vsi->netdev, "NIC Link is Up\n");
		netif_tx_start_all_queues(vsi->netdev);
		netif_carrier_on(vsi->netdev);
	} else if (vsi->netdev) {
		netdev_info(vsi->netdev, "NIC Link is Down\n");
	}
	i40e_service_event_schedule(pf);

	return 0;
}

/**
 * i40e_vsi_reinit_locked - Reset the VSI
 * @vsi: the VSI being configured
 *
 * Rebuild the ring structs after some configuration
 * has changed, e.g. MTU size.
 **/
static void i40e_vsi_reinit_locked(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	WARN_ON(in_interrupt());
	while (test_and_set_bit(__I40E_CONFIG_BUSY, &pf->state))
		usleep_range(1000, 2000);
	i40e_down(vsi);

	/* Give a VF some time to respond to the reset.  The
	 * two second wait is based upon the watchdog cycle in
	 * the VF driver.
	 */
	if (vsi->type == I40E_VSI_SRIOV)
		msleep(2000);
	i40e_up(vsi);
	clear_bit(__I40E_CONFIG_BUSY, &pf->state);
}

/**
 * i40e_up - Bring the connection back up after being down
 * @vsi: the VSI being configured
 **/
int i40e_up(struct i40e_vsi *vsi)
{
	int err;

	err = i40e_vsi_configure(vsi);
	if (!err)
		err = i40e_up_complete(vsi);

	return err;
}

/**
 * i40e_down - Shutdown the connection processing
 * @vsi: the VSI being stopped
 **/
void i40e_down(struct i40e_vsi *vsi)
{
	int i;

	/* It is assumed that the caller of this function
	 * sets the vsi->state __I40E_DOWN bit.
	 */
	if (vsi->netdev) {
		netif_carrier_off(vsi->netdev);
		netif_tx_disable(vsi->netdev);
	}
	i40e_vsi_disable_irq(vsi);
	i40e_vsi_control_rings(vsi, false);
	i40e_napi_disable_all(vsi);

	for (i = 0; i < vsi->num_queue_pairs; i++) {
		i40e_clean_tx_ring(vsi->tx_rings[i]);
		i40e_clean_rx_ring(vsi->rx_rings[i]);
	}
}

/**
 * i40e_setup_tc - configure multiple traffic classes
 * @netdev: net device to configure
 * @tc: number of traffic classes to enable
 **/
static int i40e_setup_tc(struct net_device *netdev, u8 tc)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	u8 enabled_tc = 0;
	int ret = -EINVAL;
	int i;

	/* Check if DCB enabled to continue */
	if (!(pf->flags & I40E_FLAG_DCB_ENABLED)) {
		netdev_info(netdev, "DCB is not enabled for adapter\n");
		goto exit;
	}

	/* Check if MFP enabled */
	if (pf->flags & I40E_FLAG_MFP_ENABLED) {
		netdev_info(netdev, "Configuring TC not supported in MFP mode\n");
		goto exit;
	}

	/* Check whether tc count is within enabled limit */
	if (tc > i40e_pf_get_num_tc(pf)) {
		netdev_info(netdev, "TC count greater than enabled on link for adapter\n");
		goto exit;
	}

	/* Generate TC map for number of tc requested */
	for (i = 0; i < tc; i++)
		enabled_tc |= (1 << i);

	/* Requesting same TC configuration as already enabled */
	if (enabled_tc == vsi->tc_config.enabled_tc)
		return 0;

	/* Quiesce VSI queues */
	i40e_quiesce_vsi(vsi);

	/* Configure VSI for enabled TCs */
	ret = i40e_vsi_config_tc(vsi, enabled_tc);
	if (ret) {
		netdev_info(netdev, "Failed configuring TC for VSI seid=%d\n",
			    vsi->seid);
		goto exit;
	}

	/* Unquiesce VSI */
	i40e_unquiesce_vsi(vsi);

exit:
	return ret;
}

/**
 * i40e_open - Called when a network interface is made active
 * @netdev: network interface device structure
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS, the netdev watchdog subtask is
 * enabled, and the stack is notified that the interface is ready.
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_open(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;
	struct i40e_pf *pf = vsi->back;
	char int_name[IFNAMSIZ];
	int err;

	/* disallow open during test */
	if (test_bit(__I40E_TESTING, &pf->state))
		return -EBUSY;

	netif_carrier_off(netdev);

	/* allocate descriptors */
	err = i40e_vsi_setup_tx_resources(vsi);
	if (err)
		goto err_setup_tx;
	err = i40e_vsi_setup_rx_resources(vsi);
	if (err)
		goto err_setup_rx;

	err = i40e_vsi_configure(vsi);
	if (err)
		goto err_setup_rx;

	snprintf(int_name, sizeof(int_name) - 1, "%s-%s",
		 dev_driver_string(&pf->pdev->dev), netdev->name);
	err = i40e_vsi_request_irq(vsi, int_name);
	if (err)
		goto err_setup_rx;

	err = i40e_up_complete(vsi);
	if (err)
		goto err_up_complete;

	if ((vsi->type == I40E_VSI_MAIN) || (vsi->type == I40E_VSI_VMDQ2)) {
		err = i40e_aq_set_vsi_broadcast(&pf->hw, vsi->seid, true, NULL);
		if (err)
			netdev_info(netdev,
				    "couldn't set broadcast err %d aq_err %d\n",
				    err, pf->hw.aq.asq_last_status);
	}

	return 0;

err_up_complete:
	i40e_down(vsi);
	i40e_vsi_free_irq(vsi);
err_setup_rx:
	i40e_vsi_free_rx_resources(vsi);
err_setup_tx:
	i40e_vsi_free_tx_resources(vsi);
	if (vsi == pf->vsi[pf->lan_vsi])
		i40e_do_reset(pf, (1 << __I40E_PF_RESET_REQUESTED));

	return err;
}

/**
 * i40e_close - Disables a network interface
 * @netdev: network interface device structure
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the driver's control, but
 * this netdev interface is disabled.
 *
 * Returns 0, this is not allowed to fail
 **/
static int i40e_close(struct net_device *netdev)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (test_and_set_bit(__I40E_DOWN, &vsi->state))
		return 0;

	i40e_down(vsi);
	i40e_vsi_free_irq(vsi);

	i40e_vsi_free_tx_resources(vsi);
	i40e_vsi_free_rx_resources(vsi);

	return 0;
}

/**
 * i40e_do_reset - Start a PF or Core Reset sequence
 * @pf: board private structure
 * @reset_flags: which reset is requested
 *
 * The essential difference in resets is that the PF Reset
 * doesn't clear the packet buffers, doesn't reset the PE
 * firmware, and doesn't bother the other PFs on the chip.
 **/
void i40e_do_reset(struct i40e_pf *pf, u32 reset_flags)
{
	u32 val;

	WARN_ON(in_interrupt());

	/* do the biggest reset indicated */
	if (reset_flags & (1 << __I40E_GLOBAL_RESET_REQUESTED)) {

		/* Request a Global Reset
		 *
		 * This will start the chip's countdown to the actual full
		 * chip reset event, and a warning interrupt to be sent
		 * to all PFs, including the requestor.  Our handler
		 * for the warning interrupt will deal with the shutdown
		 * and recovery of the switch setup.
		 */
		dev_info(&pf->pdev->dev, "GlobalR requested\n");
		val = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		val |= I40E_GLGEN_RTRIG_GLOBR_MASK;
		wr32(&pf->hw, I40E_GLGEN_RTRIG, val);

	} else if (reset_flags & (1 << __I40E_CORE_RESET_REQUESTED)) {

		/* Request a Core Reset
		 *
		 * Same as Global Reset, except does *not* include the MAC/PHY
		 */
		dev_info(&pf->pdev->dev, "CoreR requested\n");
		val = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		val |= I40E_GLGEN_RTRIG_CORER_MASK;
		wr32(&pf->hw, I40E_GLGEN_RTRIG, val);
		i40e_flush(&pf->hw);

	} else if (reset_flags & (1 << __I40E_PF_RESET_REQUESTED)) {

		/* Request a PF Reset
		 *
		 * Resets only the PF-specific registers
		 *
		 * This goes directly to the tear-down and rebuild of
		 * the switch, since we need to do all the recovery as
		 * for the Core Reset.
		 */
		dev_info(&pf->pdev->dev, "PFR requested\n");
		i40e_handle_reset_warning(pf);

	} else if (reset_flags & (1 << __I40E_REINIT_REQUESTED)) {
		int v;

		/* Find the VSI(s) that requested a re-init */
		dev_info(&pf->pdev->dev,
			 "VSI reinit requested\n");
		for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
			struct i40e_vsi *vsi = pf->vsi[v];
			if (vsi != NULL &&
			    test_bit(__I40E_REINIT_REQUESTED, &vsi->state)) {
				i40e_vsi_reinit_locked(pf->vsi[v]);
				clear_bit(__I40E_REINIT_REQUESTED, &vsi->state);
			}
		}

		/* no further action needed, so return now */
		return;
	} else {
		dev_info(&pf->pdev->dev,
			 "bad reset request 0x%08x\n", reset_flags);
		return;
	}
}

/**
 * i40e_handle_lan_overflow_event - Handler for LAN queue overflow event
 * @pf: board private structure
 * @e: event info posted on ARQ
 *
 * Handler for LAN Queue Overflow Event generated by the firmware for PF
 * and VF queues
 **/
static void i40e_handle_lan_overflow_event(struct i40e_pf *pf,
					   struct i40e_arq_event_info *e)
{
	struct i40e_aqc_lan_overflow *data =
		(struct i40e_aqc_lan_overflow *)&e->desc.params.raw;
	u32 queue = le32_to_cpu(data->prtdcb_rupto);
	u32 qtx_ctl = le32_to_cpu(data->otx_ctl);
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vf *vf;
	u16 vf_id;

	dev_info(&pf->pdev->dev, "%s: Rx Queue Number = %d QTX_CTL=0x%08x\n",
		 __func__, queue, qtx_ctl);

	/* Queue belongs to VF, find the VF and issue VF reset */
	if (((qtx_ctl & I40E_QTX_CTL_PFVF_Q_MASK)
	    >> I40E_QTX_CTL_PFVF_Q_SHIFT) == I40E_QTX_CTL_VF_QUEUE) {
		vf_id = (u16)((qtx_ctl & I40E_QTX_CTL_VFVM_INDX_MASK)
			 >> I40E_QTX_CTL_VFVM_INDX_SHIFT);
		vf_id -= hw->func_caps.vf_base_id;
		vf = &pf->vf[vf_id];
		i40e_vc_notify_vf_reset(vf);
		/* Allow VF to process pending reset notification */
		msleep(20);
		i40e_reset_vf(vf, false);
	}
}

/**
 * i40e_service_event_complete - Finish up the service event
 * @pf: board private structure
 **/
static void i40e_service_event_complete(struct i40e_pf *pf)
{
	BUG_ON(!test_bit(__I40E_SERVICE_SCHED, &pf->state));

	/* flush memory to make sure state is correct before next watchog */
	smp_mb__before_clear_bit();
	clear_bit(__I40E_SERVICE_SCHED, &pf->state);
}

/**
 * i40e_fdir_reinit_subtask - Worker thread to reinit FDIR filter table
 * @pf: board private structure
 **/
static void i40e_fdir_reinit_subtask(struct i40e_pf *pf)
{
	if (!(pf->flags & I40E_FLAG_FDIR_REQUIRES_REINIT))
		return;

	pf->flags &= ~I40E_FLAG_FDIR_REQUIRES_REINIT;

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, &pf->state))
		return;
}

/**
 * i40e_vsi_link_event - notify VSI of a link event
 * @vsi: vsi to be notified
 * @link_up: link up or down
 **/
static void i40e_vsi_link_event(struct i40e_vsi *vsi, bool link_up)
{
	if (!vsi)
		return;

	switch (vsi->type) {
	case I40E_VSI_MAIN:
		if (!vsi->netdev || !vsi->netdev_registered)
			break;

		if (link_up) {
			netif_carrier_on(vsi->netdev);
			netif_tx_wake_all_queues(vsi->netdev);
		} else {
			netif_carrier_off(vsi->netdev);
			netif_tx_stop_all_queues(vsi->netdev);
		}
		break;

	case I40E_VSI_SRIOV:
		break;

	case I40E_VSI_VMDQ2:
	case I40E_VSI_CTRL:
	case I40E_VSI_MIRROR:
	default:
		/* there is no notification for other VSIs */
		break;
	}
}

/**
 * i40e_veb_link_event - notify elements on the veb of a link event
 * @veb: veb to be notified
 * @link_up: link up or down
 **/
static void i40e_veb_link_event(struct i40e_veb *veb, bool link_up)
{
	struct i40e_pf *pf;
	int i;

	if (!veb || !veb->pf)
		return;
	pf = veb->pf;

	/* depth first... */
	for (i = 0; i < I40E_MAX_VEB; i++)
		if (pf->veb[i] && (pf->veb[i]->uplink_seid == veb->seid))
			i40e_veb_link_event(pf->veb[i], link_up);

	/* ... now the local VSIs */
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++)
		if (pf->vsi[i] && (pf->vsi[i]->uplink_seid == veb->seid))
			i40e_vsi_link_event(pf->vsi[i], link_up);
}

/**
 * i40e_link_event - Update netif_carrier status
 * @pf: board private structure
 **/
static void i40e_link_event(struct i40e_pf *pf)
{
	bool new_link, old_link;

	new_link = (pf->hw.phy.link_info.link_info & I40E_AQ_LINK_UP);
	old_link = (pf->hw.phy.link_info_old.link_info & I40E_AQ_LINK_UP);

	if (new_link == old_link)
		return;

	if (!test_bit(__I40E_DOWN, &pf->vsi[pf->lan_vsi]->state))
		netdev_info(pf->vsi[pf->lan_vsi]->netdev,
			    "NIC Link is %s\n", (new_link ? "Up" : "Down"));

	/* Notify the base of the switch tree connected to
	 * the link.  Floating VEBs are not notified.
	 */
	if (pf->lan_veb != I40E_NO_VEB && pf->veb[pf->lan_veb])
		i40e_veb_link_event(pf->veb[pf->lan_veb], new_link);
	else
		i40e_vsi_link_event(pf->vsi[pf->lan_vsi], new_link);

	if (pf->vf)
		i40e_vc_notify_link_state(pf);
}

/**
 * i40e_check_hang_subtask - Check for hung queues and dropped interrupts
 * @pf: board private structure
 *
 * Set the per-queue flags to request a check for stuck queues in the irq
 * clean functions, then force interrupts to be sure the irq clean is called.
 **/
static void i40e_check_hang_subtask(struct i40e_pf *pf)
{
	int i, v;

	/* If we're down or resetting, just bail */
	if (test_bit(__I40E_CONFIG_BUSY, &pf->state))
		return;

	/* for each VSI/netdev
	 *     for each Tx queue
	 *         set the check flag
	 *     for each q_vector
	 *         force an interrupt
	 */
	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		struct i40e_vsi *vsi = pf->vsi[v];
		int armed = 0;

		if (!pf->vsi[v] ||
		    test_bit(__I40E_DOWN, &vsi->state) ||
		    (vsi->netdev && !netif_carrier_ok(vsi->netdev)))
			continue;

		for (i = 0; i < vsi->num_queue_pairs; i++) {
			set_check_for_tx_hang(vsi->tx_rings[i]);
			if (test_bit(__I40E_HANG_CHECK_ARMED,
				     &vsi->tx_rings[i]->state))
				armed++;
		}

		if (armed) {
			if (!(pf->flags & I40E_FLAG_MSIX_ENABLED)) {
				wr32(&vsi->back->hw, I40E_PFINT_DYN_CTL0,
				     (I40E_PFINT_DYN_CTL0_INTENA_MASK |
				      I40E_PFINT_DYN_CTL0_SWINT_TRIG_MASK));
			} else {
				u16 vec = vsi->base_vector - 1;
				u32 val = (I40E_PFINT_DYN_CTLN_INTENA_MASK |
					   I40E_PFINT_DYN_CTLN_SWINT_TRIG_MASK);
				for (i = 0; i < vsi->num_q_vectors; i++, vec++)
					wr32(&vsi->back->hw,
					     I40E_PFINT_DYN_CTLN(vec), val);
			}
			i40e_flush(&vsi->back->hw);
		}
	}
}

/**
 * i40e_watchdog_subtask - Check and bring link up
 * @pf: board private structure
 **/
static void i40e_watchdog_subtask(struct i40e_pf *pf)
{
	int i;

	/* if interface is down do nothing */
	if (test_bit(__I40E_DOWN, &pf->state) ||
	    test_bit(__I40E_CONFIG_BUSY, &pf->state))
		return;

	/* Update the stats for active netdevs so the network stack
	 * can look at updated numbers whenever it cares to
	 */
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++)
		if (pf->vsi[i] && pf->vsi[i]->netdev)
			i40e_update_stats(pf->vsi[i]);

	/* Update the stats for the active switching components */
	for (i = 0; i < I40E_MAX_VEB; i++)
		if (pf->veb[i])
			i40e_update_veb_stats(pf->veb[i]);
}

/**
 * i40e_reset_subtask - Set up for resetting the device and driver
 * @pf: board private structure
 **/
static void i40e_reset_subtask(struct i40e_pf *pf)
{
	u32 reset_flags = 0;

	if (test_bit(__I40E_REINIT_REQUESTED, &pf->state)) {
		reset_flags |= (1 << __I40E_REINIT_REQUESTED);
		clear_bit(__I40E_REINIT_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_PF_RESET_REQUESTED, &pf->state)) {
		reset_flags |= (1 << __I40E_PF_RESET_REQUESTED);
		clear_bit(__I40E_PF_RESET_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_CORE_RESET_REQUESTED, &pf->state)) {
		reset_flags |= (1 << __I40E_CORE_RESET_REQUESTED);
		clear_bit(__I40E_CORE_RESET_REQUESTED, &pf->state);
	}
	if (test_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state)) {
		reset_flags |= (1 << __I40E_GLOBAL_RESET_REQUESTED);
		clear_bit(__I40E_GLOBAL_RESET_REQUESTED, &pf->state);
	}

	/* If there's a recovery already waiting, it takes
	 * precedence before starting a new reset sequence.
	 */
	if (test_bit(__I40E_RESET_INTR_RECEIVED, &pf->state)) {
		i40e_handle_reset_warning(pf);
		return;
	}

	/* If we're already down or resetting, just bail */
	if (reset_flags &&
	    !test_bit(__I40E_DOWN, &pf->state) &&
	    !test_bit(__I40E_CONFIG_BUSY, &pf->state))
		i40e_do_reset(pf, reset_flags);
}

/**
 * i40e_handle_link_event - Handle link event
 * @pf: board private structure
 * @e: event info posted on ARQ
 **/
static void i40e_handle_link_event(struct i40e_pf *pf,
				   struct i40e_arq_event_info *e)
{
	struct i40e_hw *hw = &pf->hw;
	struct i40e_aqc_get_link_status *status =
		(struct i40e_aqc_get_link_status *)&e->desc.params.raw;
	struct i40e_link_status *hw_link_info = &hw->phy.link_info;

	/* save off old link status information */
	memcpy(&pf->hw.phy.link_info_old, hw_link_info,
	       sizeof(pf->hw.phy.link_info_old));

	/* update link status */
	hw_link_info->phy_type = (enum i40e_aq_phy_type)status->phy_type;
	hw_link_info->link_speed = (enum i40e_aq_link_speed)status->link_speed;
	hw_link_info->link_info = status->link_info;
	hw_link_info->an_info = status->an_info;
	hw_link_info->ext_info = status->ext_info;
	hw_link_info->lse_enable =
		le16_to_cpu(status->command_flags) &
			    I40E_AQ_LSE_ENABLE;

	/* process the event */
	i40e_link_event(pf);

	/* Do a new status request to re-enable LSE reporting
	 * and load new status information into the hw struct,
	 * then see if the status changed while processing the
	 * initial event.
	 */
	i40e_aq_get_link_info(&pf->hw, true, NULL, NULL);
	i40e_link_event(pf);
}

/**
 * i40e_clean_adminq_subtask - Clean the AdminQ rings
 * @pf: board private structure
 **/
static void i40e_clean_adminq_subtask(struct i40e_pf *pf)
{
	struct i40e_arq_event_info event;
	struct i40e_hw *hw = &pf->hw;
	u16 pending, i = 0;
	i40e_status ret;
	u16 opcode;
	u32 val;

	if (!test_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state))
		return;

	event.msg_size = I40E_MAX_AQ_BUF_SIZE;
	event.msg_buf = kzalloc(event.msg_size, GFP_KERNEL);
	if (!event.msg_buf)
		return;

	do {
		ret = i40e_clean_arq_element(hw, &event, &pending);
		if (ret == I40E_ERR_ADMIN_QUEUE_NO_WORK) {
			dev_info(&pf->pdev->dev, "No ARQ event found\n");
			break;
		} else if (ret) {
			dev_info(&pf->pdev->dev, "ARQ event error %d\n", ret);
			break;
		}

		opcode = le16_to_cpu(event.desc.opcode);
		switch (opcode) {

		case i40e_aqc_opc_get_link_status:
			i40e_handle_link_event(pf, &event);
			break;
		case i40e_aqc_opc_send_msg_to_pf:
			ret = i40e_vc_process_vf_msg(pf,
					le16_to_cpu(event.desc.retval),
					le32_to_cpu(event.desc.cookie_high),
					le32_to_cpu(event.desc.cookie_low),
					event.msg_buf,
					event.msg_size);
			break;
		case i40e_aqc_opc_lldp_update_mib:
			dev_info(&pf->pdev->dev, "ARQ: Update LLDP MIB event received\n");
			break;
		case i40e_aqc_opc_event_lan_overflow:
			dev_info(&pf->pdev->dev, "ARQ LAN queue overflow event received\n");
			i40e_handle_lan_overflow_event(pf, &event);
			break;
		default:
			dev_info(&pf->pdev->dev,
				 "ARQ Error: Unknown event %d received\n",
				 event.desc.opcode);
			break;
		}
	} while (pending && (i++ < pf->adminq_work_limit));

	clear_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state);
	/* re-enable Admin queue interrupt cause */
	val = rd32(hw, I40E_PFINT_ICR0_ENA);
	val |=  I40E_PFINT_ICR0_ENA_ADMINQ_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, val);
	i40e_flush(hw);

	kfree(event.msg_buf);
}

/**
 * i40e_reconstitute_veb - rebuild the VEB and anything connected to it
 * @veb: pointer to the VEB instance
 *
 * This is a recursive function that first builds the attached VSIs then
 * recurses in to build the next layer of VEB.  We track the connections
 * through our own index numbers because the seid's from the HW could
 * change across the reset.
 **/
static int i40e_reconstitute_veb(struct i40e_veb *veb)
{
	struct i40e_vsi *ctl_vsi = NULL;
	struct i40e_pf *pf = veb->pf;
	int v, veb_idx;
	int ret;

	/* build VSI that owns this VEB, temporarily attached to base VEB */
	for (v = 0; v < pf->hw.func_caps.num_vsis && !ctl_vsi; v++) {
		if (pf->vsi[v] &&
		    pf->vsi[v]->veb_idx == veb->idx &&
		    pf->vsi[v]->flags & I40E_VSI_FLAG_VEB_OWNER) {
			ctl_vsi = pf->vsi[v];
			break;
		}
	}
	if (!ctl_vsi) {
		dev_info(&pf->pdev->dev,
			 "missing owner VSI for veb_idx %d\n", veb->idx);
		ret = -ENOENT;
		goto end_reconstitute;
	}
	if (ctl_vsi != pf->vsi[pf->lan_vsi])
		ctl_vsi->uplink_seid = pf->vsi[pf->lan_vsi]->uplink_seid;
	ret = i40e_add_vsi(ctl_vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "rebuild of owner VSI failed: %d\n", ret);
		goto end_reconstitute;
	}
	i40e_vsi_reset_stats(ctl_vsi);

	/* create the VEB in the switch and move the VSI onto the VEB */
	ret = i40e_add_veb(veb, ctl_vsi);
	if (ret)
		goto end_reconstitute;

	/* create the remaining VSIs attached to this VEB */
	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (!pf->vsi[v] || pf->vsi[v] == ctl_vsi)
			continue;

		if (pf->vsi[v]->veb_idx == veb->idx) {
			struct i40e_vsi *vsi = pf->vsi[v];
			vsi->uplink_seid = veb->seid;
			ret = i40e_add_vsi(vsi);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "rebuild of vsi_idx %d failed: %d\n",
					 v, ret);
				goto end_reconstitute;
			}
			i40e_vsi_reset_stats(vsi);
		}
	}

	/* create any VEBs attached to this VEB - RECURSION */
	for (veb_idx = 0; veb_idx < I40E_MAX_VEB; veb_idx++) {
		if (pf->veb[veb_idx] && pf->veb[veb_idx]->veb_idx == veb->idx) {
			pf->veb[veb_idx]->uplink_seid = veb->seid;
			ret = i40e_reconstitute_veb(pf->veb[veb_idx]);
			if (ret)
				break;
		}
	}

end_reconstitute:
	return ret;
}

/**
 * i40e_get_capabilities - get info about the HW
 * @pf: the PF struct
 **/
static int i40e_get_capabilities(struct i40e_pf *pf)
{
	struct i40e_aqc_list_capabilities_element_resp *cap_buf;
	u16 data_size;
	int buf_len;
	int err;

	buf_len = 40 * sizeof(struct i40e_aqc_list_capabilities_element_resp);
	do {
		cap_buf = kzalloc(buf_len, GFP_KERNEL);
		if (!cap_buf)
			return -ENOMEM;

		/* this loads the data into the hw struct for us */
		err = i40e_aq_discover_capabilities(&pf->hw, cap_buf, buf_len,
					    &data_size,
					    i40e_aqc_opc_list_func_capabilities,
					    NULL);
		/* data loaded, buffer no longer needed */
		kfree(cap_buf);

		if (pf->hw.aq.asq_last_status == I40E_AQ_RC_ENOMEM) {
			/* retry with a larger buffer */
			buf_len = data_size;
		} else if (pf->hw.aq.asq_last_status != I40E_AQ_RC_OK) {
			dev_info(&pf->pdev->dev,
				 "capability discovery failed: aq=%d\n",
				 pf->hw.aq.asq_last_status);
			return -ENODEV;
		}
	} while (err);

	if (pf->hw.debug_mask & I40E_DEBUG_USER)
		dev_info(&pf->pdev->dev,
			 "pf=%d, num_vfs=%d, msix_pf=%d, msix_vf=%d, fd_g=%d, fd_b=%d, pf_max_q=%d num_vsi=%d\n",
			 pf->hw.pf_id, pf->hw.func_caps.num_vfs,
			 pf->hw.func_caps.num_msix_vectors,
			 pf->hw.func_caps.num_msix_vectors_vf,
			 pf->hw.func_caps.fd_filters_guaranteed,
			 pf->hw.func_caps.fd_filters_best_effort,
			 pf->hw.func_caps.num_tx_qp,
			 pf->hw.func_caps.num_vsis);

	return 0;
}

/**
 * i40e_fdir_setup - initialize the Flow Director resources
 * @pf: board private structure
 **/
static void i40e_fdir_setup(struct i40e_pf *pf)
{
	struct i40e_vsi *vsi;
	bool new_vsi = false;
	int err, i;

	if (!(pf->flags & (I40E_FLAG_FDIR_ENABLED |
			   I40E_FLAG_FDIR_ATR_ENABLED)))
		return;

	pf->atr_sample_rate = I40E_DEFAULT_ATR_SAMPLE_RATE;

	/* find existing or make new FDIR VSI */
	vsi = NULL;
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++)
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR)
			vsi = pf->vsi[i];
	if (!vsi) {
		vsi = i40e_vsi_setup(pf, I40E_VSI_FDIR, pf->mac_seid, 0);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "Couldn't create FDir VSI\n");
			pf->flags &= ~I40E_FLAG_FDIR_ENABLED;
			return;
		}
		new_vsi = true;
	}
	WARN_ON(vsi->base_queue != I40E_FDIR_RING);
	i40e_vsi_setup_irqhandler(vsi, i40e_fdir_clean_rings);

	err = i40e_vsi_setup_tx_resources(vsi);
	if (!err)
		err = i40e_vsi_setup_rx_resources(vsi);
	if (!err)
		err = i40e_vsi_configure(vsi);
	if (!err && new_vsi) {
		char int_name[IFNAMSIZ + 9];
		snprintf(int_name, sizeof(int_name) - 1, "%s-fdir",
			 dev_driver_string(&pf->pdev->dev));
		err = i40e_vsi_request_irq(vsi, int_name);
	}
	if (!err)
		err = i40e_up_complete(vsi);

	clear_bit(__I40E_NEEDS_RESTART, &vsi->state);
}

/**
 * i40e_fdir_teardown - release the Flow Director resources
 * @pf: board private structure
 **/
static void i40e_fdir_teardown(struct i40e_pf *pf)
{
	int i;

	for (i = 0; i < pf->hw.func_caps.num_vsis; i++) {
		if (pf->vsi[i] && pf->vsi[i]->type == I40E_VSI_FDIR) {
			i40e_vsi_release(pf->vsi[i]);
			break;
		}
	}
}

/**
 * i40e_handle_reset_warning - prep for the core to reset
 * @pf: board private structure
 *
 * Close up the VFs and other things in prep for a Core Reset,
 * then get ready to rebuild the world.
 **/
static void i40e_handle_reset_warning(struct i40e_pf *pf)
{
	struct i40e_driver_version dv;
	struct i40e_hw *hw = &pf->hw;
	i40e_status ret;
	u32 v;

	clear_bit(__I40E_RESET_INTR_RECEIVED, &pf->state);
	if (test_and_set_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state))
		return;

	dev_info(&pf->pdev->dev, "Tearing down internal switch for reset\n");

	i40e_vc_notify_reset(pf);

	/* quiesce the VSIs and their queues that are not already DOWN */
	i40e_pf_quiesce_all_vsi(pf);

	for (v = 0; v < pf->hw.func_caps.num_vsis; v++) {
		if (pf->vsi[v])
			pf->vsi[v]->seid = 0;
	}

	i40e_shutdown_adminq(&pf->hw);

	/* Now we wait for GRST to settle out.
	 * We don't have to delete the VEBs or VSIs from the hw switch
	 * because the reset will make them disappear.
	 */
	ret = i40e_pf_reset(hw);
	if (ret)
		dev_info(&pf->pdev->dev, "PF reset failed, %d\n", ret);
	pf->pfr_count++;

	if (test_bit(__I40E_DOWN, &pf->state))
		goto end_core_reset;
	dev_info(&pf->pdev->dev, "Rebuilding internal switch\n");

	/* rebuild the basics for the AdminQ, HMC, and initial HW switch */
	ret = i40e_init_adminq(&pf->hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "Rebuild AdminQ failed, %d\n", ret);
		goto end_core_reset;
	}

	ret = i40e_get_capabilities(pf);
	if (ret) {
		dev_info(&pf->pdev->dev, "i40e_get_capabilities failed, %d\n",
			 ret);
		goto end_core_reset;
	}

	/* call shutdown HMC */
	ret = i40e_shutdown_lan_hmc(hw);
	if (ret) {
		dev_info(&pf->pdev->dev, "shutdown_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}

	ret = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp,
				pf->fcoe_hmc_cntx_num, pf->fcoe_hmc_filt_num);
	if (ret) {
		dev_info(&pf->pdev->dev, "init_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}
	ret = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (ret) {
		dev_info(&pf->pdev->dev, "configure_lan_hmc failed: %d\n", ret);
		goto end_core_reset;
	}

	/* do basic switch setup */
	ret = i40e_setup_pf_switch(pf);
	if (ret)
		goto end_core_reset;

	/* Rebuild the VSIs and VEBs that existed before reset.
	 * They are still in our local switch element arrays, so only
	 * need to rebuild the switch model in the HW.
	 *
	 * If there were VEBs but the reconstitution failed, we'll try
	 * try to recover minimal use by getting the basic PF VSI working.
	 */
	if (pf->vsi[pf->lan_vsi]->uplink_seid != pf->mac_seid) {
		dev_info(&pf->pdev->dev, "attempting to rebuild switch\n");
		/* find the one VEB connected to the MAC, and find orphans */
		for (v = 0; v < I40E_MAX_VEB; v++) {
			if (!pf->veb[v])
				continue;

			if (pf->veb[v]->uplink_seid == pf->mac_seid ||
			    pf->veb[v]->uplink_seid == 0) {
				ret = i40e_reconstitute_veb(pf->veb[v]);

				if (!ret)
					continue;

				/* If Main VEB failed, we're in deep doodoo,
				 * so give up rebuilding the switch and set up
				 * for minimal rebuild of PF VSI.
				 * If orphan failed, we'll report the error
				 * but try to keep going.
				 */
				if (pf->veb[v]->uplink_seid == pf->mac_seid) {
					dev_info(&pf->pdev->dev,
						 "rebuild of switch failed: %d, will try to set up simple PF connection\n",
						 ret);
					pf->vsi[pf->lan_vsi]->uplink_seid
								= pf->mac_seid;
					break;
				} else if (pf->veb[v]->uplink_seid == 0) {
					dev_info(&pf->pdev->dev,
						 "rebuild of orphan VEB failed: %d\n",
						 ret);
				}
			}
		}
	}

	if (pf->vsi[pf->lan_vsi]->uplink_seid == pf->mac_seid) {
		dev_info(&pf->pdev->dev, "attempting to rebuild PF VSI\n");
		/* no VEB, so rebuild only the Main VSI */
		ret = i40e_add_vsi(pf->vsi[pf->lan_vsi]);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "rebuild of Main VSI failed: %d\n", ret);
			goto end_core_reset;
		}
	}

	/* reinit the misc interrupt */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		ret = i40e_setup_misc_vector(pf);

	/* restart the VSIs that were rebuilt and running before the reset */
	i40e_pf_unquiesce_all_vsi(pf);

	/* tell the firmware that we're starting */
	dv.major_version = DRV_VERSION_MAJOR;
	dv.minor_version = DRV_VERSION_MINOR;
	dv.build_version = DRV_VERSION_BUILD;
	dv.subbuild_version = 0;
	i40e_aq_send_driver_version(&pf->hw, &dv, NULL);

	dev_info(&pf->pdev->dev, "PF reset done\n");

end_core_reset:
	clear_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state);
}

/**
 * i40e_handle_mdd_event
 * @pf: pointer to the pf structure
 *
 * Called from the MDD irq handler to identify possibly malicious vfs
 **/
static void i40e_handle_mdd_event(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	bool mdd_detected = false;
	struct i40e_vf *vf;
	u32 reg;
	int i;

	if (!test_bit(__I40E_MDD_EVENT_PENDING, &pf->state))
		return;

	/* find what triggered the MDD event */
	reg = rd32(hw, I40E_GL_MDET_TX);
	if (reg & I40E_GL_MDET_TX_VALID_MASK) {
		u8 func = (reg & I40E_GL_MDET_TX_FUNCTION_MASK)
				>> I40E_GL_MDET_TX_FUNCTION_SHIFT;
		u8 event = (reg & I40E_GL_MDET_TX_EVENT_SHIFT)
				>> I40E_GL_MDET_TX_EVENT_SHIFT;
		u8 queue = (reg & I40E_GL_MDET_TX_QUEUE_MASK)
				>> I40E_GL_MDET_TX_QUEUE_SHIFT;
		dev_info(&pf->pdev->dev,
			 "Malicious Driver Detection TX event 0x%02x on q %d of function 0x%02x\n",
			 event, queue, func);
		wr32(hw, I40E_GL_MDET_TX, 0xffffffff);
		mdd_detected = true;
	}
	reg = rd32(hw, I40E_GL_MDET_RX);
	if (reg & I40E_GL_MDET_RX_VALID_MASK) {
		u8 func = (reg & I40E_GL_MDET_RX_FUNCTION_MASK)
				>> I40E_GL_MDET_RX_FUNCTION_SHIFT;
		u8 event = (reg & I40E_GL_MDET_RX_EVENT_SHIFT)
				>> I40E_GL_MDET_RX_EVENT_SHIFT;
		u8 queue = (reg & I40E_GL_MDET_RX_QUEUE_MASK)
				>> I40E_GL_MDET_RX_QUEUE_SHIFT;
		dev_info(&pf->pdev->dev,
			 "Malicious Driver Detection RX event 0x%02x on q %d of function 0x%02x\n",
			 event, queue, func);
		wr32(hw, I40E_GL_MDET_RX, 0xffffffff);
		mdd_detected = true;
	}

	/* see if one of the VFs needs its hand slapped */
	for (i = 0; i < pf->num_alloc_vfs && mdd_detected; i++) {
		vf = &(pf->vf[i]);
		reg = rd32(hw, I40E_VP_MDET_TX(i));
		if (reg & I40E_VP_MDET_TX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_TX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "MDD TX event on VF %d\n", i);
		}

		reg = rd32(hw, I40E_VP_MDET_RX(i));
		if (reg & I40E_VP_MDET_RX_VALID_MASK) {
			wr32(hw, I40E_VP_MDET_RX(i), 0xFFFF);
			vf->num_mdd_events++;
			dev_info(&pf->pdev->dev, "MDD RX event on VF %d\n", i);
		}

		if (vf->num_mdd_events > I40E_DEFAULT_NUM_MDD_EVENTS_ALLOWED) {
			dev_info(&pf->pdev->dev,
				 "Too many MDD events on VF %d, disabled\n", i);
			dev_info(&pf->pdev->dev,
				 "Use PF Control I/F to re-enable the VF\n");
			set_bit(I40E_VF_STAT_DISABLED, &vf->vf_states);
		}
	}

	/* re-enable mdd interrupt cause */
	clear_bit(__I40E_MDD_EVENT_PENDING, &pf->state);
	reg = rd32(hw, I40E_PFINT_ICR0_ENA);
	reg |=  I40E_PFINT_ICR0_ENA_MAL_DETECT_MASK;
	wr32(hw, I40E_PFINT_ICR0_ENA, reg);
	i40e_flush(hw);
}

/**
 * i40e_service_task - Run the driver's async subtasks
 * @work: pointer to work_struct containing our data
 **/
static void i40e_service_task(struct work_struct *work)
{
	struct i40e_pf *pf = container_of(work,
					  struct i40e_pf,
					  service_task);
	unsigned long start_time = jiffies;

	i40e_reset_subtask(pf);
	i40e_handle_mdd_event(pf);
	i40e_vc_process_vflr_event(pf);
	i40e_watchdog_subtask(pf);
	i40e_fdir_reinit_subtask(pf);
	i40e_check_hang_subtask(pf);
	i40e_sync_filters_subtask(pf);
	i40e_clean_adminq_subtask(pf);

	i40e_service_event_complete(pf);

	/* If the tasks have taken longer than one timer cycle or there
	 * is more work to be done, reschedule the service task now
	 * rather than wait for the timer to tick again.
	 */
	if (time_after(jiffies, (start_time + pf->service_timer_period)) ||
	    test_bit(__I40E_ADMINQ_EVENT_PENDING, &pf->state)		 ||
	    test_bit(__I40E_MDD_EVENT_PENDING, &pf->state)		 ||
	    test_bit(__I40E_VFLR_EVENT_PENDING, &pf->state))
		i40e_service_event_schedule(pf);
}

/**
 * i40e_service_timer - timer callback
 * @data: pointer to PF struct
 **/
static void i40e_service_timer(unsigned long data)
{
	struct i40e_pf *pf = (struct i40e_pf *)data;

	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));
	i40e_service_event_schedule(pf);
}

/**
 * i40e_set_num_rings_in_vsi - Determine number of rings in the VSI
 * @vsi: the VSI being configured
 **/
static int i40e_set_num_rings_in_vsi(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;

	switch (vsi->type) {
	case I40E_VSI_MAIN:
		vsi->alloc_queue_pairs = pf->num_lan_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		if (pf->flags & I40E_FLAG_MSIX_ENABLED)
			vsi->num_q_vectors = pf->num_lan_msix;
		else
			vsi->num_q_vectors = 1;

		break;

	case I40E_VSI_FDIR:
		vsi->alloc_queue_pairs = 1;
		vsi->num_desc = ALIGN(I40E_FDIR_RING_COUNT,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = 1;
		break;

	case I40E_VSI_VMDQ2:
		vsi->alloc_queue_pairs = pf->num_vmdq_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		vsi->num_q_vectors = pf->num_vmdq_msix;
		break;

	case I40E_VSI_SRIOV:
		vsi->alloc_queue_pairs = pf->num_vf_qps;
		vsi->num_desc = ALIGN(I40E_DEFAULT_NUM_DESCRIPTORS,
				      I40E_REQ_DESCRIPTOR_MULTIPLE);
		break;

	default:
		WARN_ON(1);
		return -ENODATA;
	}

	return 0;
}

/**
 * i40e_vsi_mem_alloc - Allocates the next available struct vsi in the PF
 * @pf: board private structure
 * @type: type of VSI
 *
 * On error: returns error code (negative)
 * On success: returns vsi index in PF (positive)
 **/
static int i40e_vsi_mem_alloc(struct i40e_pf *pf, enum i40e_vsi_type type)
{
	int ret = -ENODEV;
	struct i40e_vsi *vsi;
	int sz_vectors;
	int sz_rings;
	int vsi_idx;
	int i;

	/* Need to protect the allocation of the VSIs at the PF level */
	mutex_lock(&pf->switch_mutex);

	/* VSI list may be fragmented if VSI creation/destruction has
	 * been happening.  We can afford to do a quick scan to look
	 * for any free VSIs in the list.
	 *
	 * find next empty vsi slot, looping back around if necessary
	 */
	i = pf->next_vsi;
	while (i < pf->hw.func_caps.num_vsis && pf->vsi[i])
		i++;
	if (i >= pf->hw.func_caps.num_vsis) {
		i = 0;
		while (i < pf->next_vsi && pf->vsi[i])
			i++;
	}

	if (i < pf->hw.func_caps.num_vsis && !pf->vsi[i]) {
		vsi_idx = i;             /* Found one! */
	} else {
		ret = -ENODEV;
		goto unlock_pf;  /* out of VSI slots! */
	}
	pf->next_vsi = ++i;

	vsi = kzalloc(sizeof(*vsi), GFP_KERNEL);
	if (!vsi) {
		ret = -ENOMEM;
		goto unlock_pf;
	}
	vsi->type = type;
	vsi->back = pf;
	set_bit(__I40E_DOWN, &vsi->state);
	vsi->flags = 0;
	vsi->idx = vsi_idx;
	vsi->rx_itr_setting = pf->rx_itr_default;
	vsi->tx_itr_setting = pf->tx_itr_default;
	vsi->netdev_registered = false;
	vsi->work_limit = I40E_DEFAULT_IRQ_WORK;
	INIT_LIST_HEAD(&vsi->mac_filter_list);

	ret = i40e_set_num_rings_in_vsi(vsi);
	if (ret)
		goto err_rings;

	/* allocate memory for ring pointers */
	sz_rings = sizeof(struct i40e_ring *) * vsi->alloc_queue_pairs * 2;
	vsi->tx_rings = kzalloc(sz_rings, GFP_KERNEL);
	if (!vsi->tx_rings) {
		ret = -ENOMEM;
		goto err_rings;
	}
	vsi->rx_rings = &vsi->tx_rings[vsi->alloc_queue_pairs];

	/* allocate memory for q_vector pointers */
	sz_vectors = sizeof(struct i40e_q_vectors *) * vsi->num_q_vectors;
	vsi->q_vectors = kzalloc(sz_vectors, GFP_KERNEL);
	if (!vsi->q_vectors) {
		ret = -ENOMEM;
		goto err_vectors;
	}

	/* Setup default MSIX irq handler for VSI */
	i40e_vsi_setup_irqhandler(vsi, i40e_msix_clean_rings);

	pf->vsi[vsi_idx] = vsi;
	ret = vsi_idx;
	goto unlock_pf;

err_vectors:
 	kfree(vsi->tx_rings);
err_rings:
	pf->next_vsi = i - 1;
	kfree(vsi);
unlock_pf:
	mutex_unlock(&pf->switch_mutex);
	return ret;
}

/**
 * i40e_vsi_clear - Deallocate the VSI provided
 * @vsi: the VSI being un-configured
 **/
static int i40e_vsi_clear(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf;

	if (!vsi)
		return 0;

	if (!vsi->back)
		goto free_vsi;
	pf = vsi->back;

	mutex_lock(&pf->switch_mutex);
	if (!pf->vsi[vsi->idx]) {
		dev_err(&pf->pdev->dev, "pf->vsi[%d] is NULL, just free vsi[%d](%p,type %d)\n",
			vsi->idx, vsi->idx, vsi, vsi->type);
		goto unlock_vsi;
	}

	if (pf->vsi[vsi->idx] != vsi) {
		dev_err(&pf->pdev->dev,
			"pf->vsi[%d](%p, type %d) != vsi[%d](%p,type %d): no free!\n",
			pf->vsi[vsi->idx]->idx,
			pf->vsi[vsi->idx],
			pf->vsi[vsi->idx]->type,
			vsi->idx, vsi, vsi->type);
		goto unlock_vsi;
	}

	/* updates the pf for this cleared vsi */
	i40e_put_lump(pf->qp_pile, vsi->base_queue, vsi->idx);
	i40e_put_lump(pf->irq_pile, vsi->base_vector, vsi->idx);

	/* free the ring and vector containers */
	kfree(vsi->q_vectors);
	kfree(vsi->tx_rings);

	pf->vsi[vsi->idx] = NULL;
	if (vsi->idx < pf->next_vsi)
		pf->next_vsi = vsi->idx;

unlock_vsi:
	mutex_unlock(&pf->switch_mutex);
free_vsi:
	kfree(vsi);

	return 0;
}

/**
 * i40e_vsi_clear_rings - Deallocates the Rx and Tx rings for the provided VSI
 * @vsi: the VSI being cleaned
 **/
static s32 i40e_vsi_clear_rings(struct i40e_vsi *vsi)
{
	int i;

	if (vsi->tx_rings[0])
		for (i = 0; i < vsi->alloc_queue_pairs; i++) {
			kfree_rcu(vsi->tx_rings[i], rcu);
			vsi->tx_rings[i] = NULL;
			vsi->rx_rings[i] = NULL;
		}

	return 0;
}

/**
 * i40e_alloc_rings - Allocates the Rx and Tx rings for the provided VSI
 * @vsi: the VSI being configured
 **/
static int i40e_alloc_rings(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int i;

	/* Set basic values in the rings to be used later during open() */
	for (i = 0; i < vsi->alloc_queue_pairs; i++) {
		struct i40e_ring *tx_ring;
		struct i40e_ring *rx_ring;

		tx_ring = kzalloc(sizeof(struct i40e_ring) * 2, GFP_KERNEL);
		if (!tx_ring)
			goto err_out;

		tx_ring->queue_index = i;
		tx_ring->reg_idx = vsi->base_queue + i;
		tx_ring->ring_active = false;
		tx_ring->vsi = vsi;
		tx_ring->netdev = vsi->netdev;
		tx_ring->dev = &pf->pdev->dev;
		tx_ring->count = vsi->num_desc;
		tx_ring->size = 0;
		tx_ring->dcb_tc = 0;
		vsi->tx_rings[i] = tx_ring;

		rx_ring = &tx_ring[1];
		rx_ring->queue_index = i;
		rx_ring->reg_idx = vsi->base_queue + i;
		rx_ring->ring_active = false;
		rx_ring->vsi = vsi;
		rx_ring->netdev = vsi->netdev;
		rx_ring->dev = &pf->pdev->dev;
		rx_ring->count = vsi->num_desc;
		rx_ring->size = 0;
		rx_ring->dcb_tc = 0;
		if (pf->flags & I40E_FLAG_16BYTE_RX_DESC_ENABLED)
			set_ring_16byte_desc_enabled(rx_ring);
		else
			clear_ring_16byte_desc_enabled(rx_ring);
		vsi->rx_rings[i] = rx_ring;
	}

	return 0;

err_out:
	i40e_vsi_clear_rings(vsi);
	return -ENOMEM;
}

/**
 * i40e_reserve_msix_vectors - Reserve MSI-X vectors in the kernel
 * @pf: board private structure
 * @vectors: the number of MSI-X vectors to request
 *
 * Returns the number of vectors reserved, or error
 **/
static int i40e_reserve_msix_vectors(struct i40e_pf *pf, int vectors)
{
	int err = 0;

	pf->num_msix_entries = 0;
	while (vectors >= I40E_MIN_MSIX) {
		err = pci_enable_msix(pf->pdev, pf->msix_entries, vectors);
		if (err == 0) {
			/* good to go */
			pf->num_msix_entries = vectors;
			break;
		} else if (err < 0) {
			/* total failure */
			dev_info(&pf->pdev->dev,
				 "MSI-X vector reservation failed: %d\n", err);
			vectors = 0;
			break;
		} else {
			/* err > 0 is the hint for retry */
			dev_info(&pf->pdev->dev,
				 "MSI-X vectors wanted %d, retrying with %d\n",
				 vectors, err);
			vectors = err;
		}
	}

	if (vectors > 0 && vectors < I40E_MIN_MSIX) {
		dev_info(&pf->pdev->dev,
			 "Couldn't get enough vectors, only %d available\n",
			 vectors);
		vectors = 0;
	}

	return vectors;
}

/**
 * i40e_init_msix - Setup the MSIX capability
 * @pf: board private structure
 *
 * Work with the OS to set up the MSIX vectors needed.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_init_msix(struct i40e_pf *pf)
{
	i40e_status err = 0;
	struct i40e_hw *hw = &pf->hw;
	int v_budget, i;
	int vec;

	if (!(pf->flags & I40E_FLAG_MSIX_ENABLED))
		return -ENODEV;

	/* The number of vectors we'll request will be comprised of:
	 *   - Add 1 for "other" cause for Admin Queue events, etc.
	 *   - The number of LAN queue pairs
	 *        already adjusted for the NUMA node
	 *        assumes symmetric Tx/Rx pairing
	 *   - The number of VMDq pairs
	 * Once we count this up, try the request.
	 *
	 * If we can't get what we want, we'll simplify to nearly nothing
	 * and try again.  If that still fails, we punt.
	 */
	pf->num_lan_msix = pf->num_lan_qps;
	pf->num_vmdq_msix = pf->num_vmdq_qps;
	v_budget = 1 + pf->num_lan_msix;
	v_budget += (pf->num_vmdq_vsis * pf->num_vmdq_msix);
	if (pf->flags & I40E_FLAG_FDIR_ENABLED)
		v_budget++;

	/* Scale down if necessary, and the rings will share vectors */
	v_budget = min_t(int, v_budget, hw->func_caps.num_msix_vectors);

	pf->msix_entries = kcalloc(v_budget, sizeof(struct msix_entry),
				   GFP_KERNEL);
	if (!pf->msix_entries)
		return -ENOMEM;

	for (i = 0; i < v_budget; i++)
		pf->msix_entries[i].entry = i;
	vec = i40e_reserve_msix_vectors(pf, v_budget);
	if (vec < I40E_MIN_MSIX) {
		pf->flags &= ~I40E_FLAG_MSIX_ENABLED;
		kfree(pf->msix_entries);
		pf->msix_entries = NULL;
		return -ENODEV;

	} else if (vec == I40E_MIN_MSIX) {
		/* Adjust for minimal MSIX use */
		dev_info(&pf->pdev->dev, "Features disabled, not enough MSIX vectors\n");
		pf->flags &= ~I40E_FLAG_VMDQ_ENABLED;
		pf->num_vmdq_vsis = 0;
		pf->num_vmdq_qps = 0;
		pf->num_vmdq_msix = 0;
		pf->num_lan_qps = 1;
		pf->num_lan_msix = 1;

	} else if (vec != v_budget) {
		/* Scale vector usage down */
		pf->num_vmdq_msix = 1;    /* force VMDqs to only one vector */
		vec--;                    /* reserve the misc vector */

		/* partition out the remaining vectors */
		switch (vec) {
		case 2:
			pf->num_vmdq_vsis = 1;
			pf->num_lan_msix = 1;
			break;
		case 3:
			pf->num_vmdq_vsis = 1;
			pf->num_lan_msix = 2;
			break;
		default:
			pf->num_lan_msix = min_t(int, (vec / 2),
						 pf->num_lan_qps);
			pf->num_vmdq_vsis = min_t(int, (vec - pf->num_lan_msix),
						  I40E_DEFAULT_NUM_VMDQ_VSI);
			break;
		}
	}

	return err;
}

/**
 * i40e_alloc_q_vector - Allocate memory for a single interrupt vector
 * @vsi: the VSI being configured
 * @v_idx: index of the vector in the vsi struct
 *
 * We allocate one q_vector.  If allocation fails we return -ENOMEM.
 **/
static int i40e_alloc_q_vector(struct i40e_vsi *vsi, int v_idx)
{
	struct i40e_q_vector *q_vector;

	/* allocate q_vector */
	q_vector = kzalloc(sizeof(struct i40e_q_vector), GFP_KERNEL);
	if (!q_vector)
		return -ENOMEM;

	q_vector->vsi = vsi;
	q_vector->v_idx = v_idx;
	cpumask_set_cpu(v_idx, &q_vector->affinity_mask);
	if (vsi->netdev)
		netif_napi_add(vsi->netdev, &q_vector->napi,
			       i40e_napi_poll, vsi->work_limit);

	q_vector->rx.latency_range = I40E_LOW_LATENCY;
	q_vector->tx.latency_range = I40E_LOW_LATENCY;

	/* tie q_vector and vsi together */
	vsi->q_vectors[v_idx] = q_vector;

	return 0;
}

/**
 * i40e_alloc_q_vectors - Allocate memory for interrupt vectors
 * @vsi: the VSI being configured
 *
 * We allocate one q_vector per queue interrupt.  If allocation fails we
 * return -ENOMEM.
 **/
static int i40e_alloc_q_vectors(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	int v_idx, num_q_vectors;
	int err;

	/* if not MSIX, give the one vector only to the LAN VSI */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED)
		num_q_vectors = vsi->num_q_vectors;
	else if (vsi == pf->vsi[pf->lan_vsi])
		num_q_vectors = 1;
	else
		return -EINVAL;

	for (v_idx = 0; v_idx < num_q_vectors; v_idx++) {
		err = i40e_alloc_q_vector(vsi, v_idx);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (v_idx--)
		i40e_free_q_vector(vsi, v_idx);

	return err;
}

/**
 * i40e_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 **/
static void i40e_init_interrupt_scheme(struct i40e_pf *pf)
{
	int err = 0;

	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		err = i40e_init_msix(pf);
		if (err) {
			pf->flags &= ~(I40E_FLAG_MSIX_ENABLED	   |
					I40E_FLAG_RSS_ENABLED	   |
					I40E_FLAG_MQ_ENABLED	   |
					I40E_FLAG_DCB_ENABLED	   |
					I40E_FLAG_SRIOV_ENABLED	   |
					I40E_FLAG_FDIR_ENABLED	   |
					I40E_FLAG_FDIR_ATR_ENABLED |
					I40E_FLAG_VMDQ_ENABLED);

			/* rework the queue expectations without MSIX */
			i40e_determine_queue_usage(pf);
		}
	}

	if (!(pf->flags & I40E_FLAG_MSIX_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSI_ENABLED)) {
		dev_info(&pf->pdev->dev, "MSIX not available, trying MSI\n");
		err = pci_enable_msi(pf->pdev);
		if (err) {
			dev_info(&pf->pdev->dev, "MSI init failed - %d\n", err);
			pf->flags &= ~I40E_FLAG_MSI_ENABLED;
		}
	}

	if (!(pf->flags & (I40E_FLAG_MSIX_ENABLED | I40E_FLAG_MSI_ENABLED)))
		dev_info(&pf->pdev->dev, "MSIX and MSI not available, falling back to Legacy IRQ\n");

	/* track first vector for misc interrupts */
	err = i40e_get_lump(pf, pf->irq_pile, 1, I40E_PILE_VALID_BIT-1);
}

/**
 * i40e_setup_misc_vector - Setup the misc vector to handle non queue events
 * @pf: board private structure
 *
 * This sets up the handler for MSIX 0, which is used to manage the
 * non-queue interrupts, e.g. AdminQ and errors.  This is not used
 * when in MSI or Legacy interrupt mode.
 **/
static int i40e_setup_misc_vector(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	int err = 0;

	/* Only request the irq if this is the first time through, and
	 * not when we're rebuilding after a Reset
	 */
	if (!test_bit(__I40E_RESET_RECOVERY_PENDING, &pf->state)) {
		err = request_irq(pf->msix_entries[0].vector,
				  i40e_intr, 0, pf->misc_int_name, pf);
		if (err) {
			dev_info(&pf->pdev->dev,
				 "request_irq for msix_misc failed: %d\n", err);
			return -EFAULT;
		}
	}

	i40e_enable_misc_int_causes(hw);

	/* associate no queues to the misc vector */
	wr32(hw, I40E_PFINT_LNKLST0, I40E_QUEUE_END_OF_LIST);
	wr32(hw, I40E_PFINT_ITR0(I40E_RX_ITR), I40E_ITR_8K);

	i40e_flush(hw);

	i40e_irq_dynamic_enable_icr0(pf);

	return err;
}

/**
 * i40e_config_rss - Prepare for RSS if used
 * @pf: board private structure
 **/
static int i40e_config_rss(struct i40e_pf *pf)
{
	struct i40e_hw *hw = &pf->hw;
	u32 lut = 0;
	int i, j;
	u64 hena;
	/* Set of random keys generated using kernel random number generator */
	static const u32 seed[I40E_PFQF_HKEY_MAX_INDEX + 1] = {0x41b01687,
				0x183cfd8c, 0xce880440, 0x580cbc3c, 0x35897377,
				0x328b25e1, 0x4fa98922, 0xb7d90c14, 0xd5bad70d,
				0xcd15a2c1, 0xe8580225, 0x4a1e9d11, 0xfe5731be};

	/* Fill out hash function seed */
	for (i = 0; i <= I40E_PFQF_HKEY_MAX_INDEX; i++)
		wr32(hw, I40E_PFQF_HKEY(i), seed[i]);

	/* By default we enable TCP/UDP with IPv4/IPv6 ptypes */
	hena = (u64)rd32(hw, I40E_PFQF_HENA(0)) |
		((u64)rd32(hw, I40E_PFQF_HENA(1)) << 32);
	hena |= ((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_TCP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_TCP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP) |
		((u64)1 << I40E_FILTER_PCTYPE_FRAG_IPV4)|
		((u64)1 << I40E_FILTER_PCTYPE_FRAG_IPV6);
	wr32(hw, I40E_PFQF_HENA(0), (u32)hena);
	wr32(hw, I40E_PFQF_HENA(1), (u32)(hena >> 32));

	/* Populate the LUT with max no. of queues in round robin fashion */
	for (i = 0, j = 0; i < pf->hw.func_caps.rss_table_size; i++, j++) {

		/* The assumption is that lan qp count will be the highest
		 * qp count for any PF VSI that needs RSS.
		 * If multiple VSIs need RSS support, all the qp counts
		 * for those VSIs should be a power of 2 for RSS to work.
		 * If LAN VSI is the only consumer for RSS then this requirement
		 * is not necessary.
		 */
		if (j == pf->rss_size)
			j = 0;
		/* lut = 4-byte sliding window of 4 lut entries */
		lut = (lut << 8) | (j &
			 ((0x1 << pf->hw.func_caps.rss_table_entry_width) - 1));
		/* On i = 3, we have 4 entries in lut; write to the register */
		if ((i & 3) == 3)
			wr32(hw, I40E_PFQF_HLUT(i >> 2), lut);
	}
	i40e_flush(hw);

	return 0;
}

/**
 * i40e_sw_init - Initialize general software structures (struct i40e_pf)
 * @pf: board private structure to initialize
 *
 * i40e_sw_init initializes the Adapter private data structure.
 * Fields are initialized based on PCI device information and
 * OS network device settings (MTU size).
 **/
static int i40e_sw_init(struct i40e_pf *pf)
{
	int err = 0;
	int size;

	pf->msg_enable = netif_msg_init(I40E_DEFAULT_MSG_ENABLE,
				(NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK));
	if (debug != -1 && debug != I40E_DEFAULT_MSG_ENABLE) {
		if (I40E_DEBUG_USER & debug)
			pf->hw.debug_mask = debug;
		pf->msg_enable = netif_msg_init((debug & ~I40E_DEBUG_USER),
						I40E_DEFAULT_MSG_ENABLE);
	}

	/* Set default capability flags */
	pf->flags = I40E_FLAG_RX_CSUM_ENABLED |
		    I40E_FLAG_MSI_ENABLED     |
		    I40E_FLAG_MSIX_ENABLED    |
		    I40E_FLAG_RX_PS_ENABLED   |
		    I40E_FLAG_MQ_ENABLED      |
		    I40E_FLAG_RX_1BUF_ENABLED;

	pf->rss_size_max = 0x1 << pf->hw.func_caps.rss_table_entry_width;
	if (pf->hw.func_caps.rss) {
		pf->flags |= I40E_FLAG_RSS_ENABLED;
		pf->rss_size = min_t(int, pf->rss_size_max,
				     nr_cpus_node(numa_node_id()));
	} else {
		pf->rss_size = 1;
	}

	if (pf->hw.func_caps.dcb)
		pf->num_tc_qps = I40E_DEFAULT_QUEUES_PER_TC;
	else
		pf->num_tc_qps = 0;

	if (pf->hw.func_caps.fd) {
		/* FW/NVM is not yet fixed in this regard */
		if ((pf->hw.func_caps.fd_filters_guaranteed > 0) ||
		    (pf->hw.func_caps.fd_filters_best_effort > 0)) {
			pf->flags |= I40E_FLAG_FDIR_ATR_ENABLED;
			dev_info(&pf->pdev->dev,
				 "Flow Director ATR mode Enabled\n");
			pf->flags |= I40E_FLAG_FDIR_ENABLED;
			dev_info(&pf->pdev->dev,
				 "Flow Director Side Band mode Enabled\n");
			pf->fdir_pf_filter_count =
					 pf->hw.func_caps.fd_filters_guaranteed;
		}
	} else {
		pf->fdir_pf_filter_count = 0;
	}

	if (pf->hw.func_caps.vmdq) {
		pf->flags |= I40E_FLAG_VMDQ_ENABLED;
		pf->num_vmdq_vsis = I40E_DEFAULT_NUM_VMDQ_VSI;
		pf->num_vmdq_qps = I40E_DEFAULT_QUEUES_PER_VMDQ;
	}

	/* MFP mode enabled */
	if (pf->hw.func_caps.npar_enable || pf->hw.func_caps.mfp_mode_1) {
		pf->flags |= I40E_FLAG_MFP_ENABLED;
		dev_info(&pf->pdev->dev, "MFP mode Enabled\n");
	}

#ifdef CONFIG_PCI_IOV
	if (pf->hw.func_caps.num_vfs) {
		pf->num_vf_qps = I40E_DEFAULT_QUEUES_PER_VF;
		pf->flags |= I40E_FLAG_SRIOV_ENABLED;
		pf->num_req_vfs = min_t(int,
					pf->hw.func_caps.num_vfs,
					I40E_MAX_VF_COUNT);
	}
#endif /* CONFIG_PCI_IOV */
	pf->eeprom_version = 0xDEAD;
	pf->lan_veb = I40E_NO_VEB;
	pf->lan_vsi = I40E_NO_VSI;

	/* set up queue assignment tracking */
	size = sizeof(struct i40e_lump_tracking)
		+ (sizeof(u16) * pf->hw.func_caps.num_tx_qp);
	pf->qp_pile = kzalloc(size, GFP_KERNEL);
	if (!pf->qp_pile) {
		err = -ENOMEM;
		goto sw_init_done;
	}
	pf->qp_pile->num_entries = pf->hw.func_caps.num_tx_qp;
	pf->qp_pile->search_hint = 0;

	/* set up vector assignment tracking */
	size = sizeof(struct i40e_lump_tracking)
		+ (sizeof(u16) * pf->hw.func_caps.num_msix_vectors);
	pf->irq_pile = kzalloc(size, GFP_KERNEL);
	if (!pf->irq_pile) {
		kfree(pf->qp_pile);
		err = -ENOMEM;
		goto sw_init_done;
	}
	pf->irq_pile->num_entries = pf->hw.func_caps.num_msix_vectors;
	pf->irq_pile->search_hint = 0;

	mutex_init(&pf->switch_mutex);

sw_init_done:
	return err;
}

/**
 * i40e_set_features - set the netdev feature flags
 * @netdev: ptr to the netdev being adjusted
 * @features: the feature set that the stack is suggesting
 **/
static int i40e_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct i40e_netdev_priv *np = netdev_priv(netdev);
	struct i40e_vsi *vsi = np->vsi;

	if (features & NETIF_F_HW_VLAN_CTAG_RX)
		i40e_vlan_stripping_enable(vsi);
	else
		i40e_vlan_stripping_disable(vsi);

	return 0;
}

static const struct net_device_ops i40e_netdev_ops = {
	.ndo_open		= i40e_open,
	.ndo_stop		= i40e_close,
	.ndo_start_xmit		= i40e_lan_xmit_frame,
	.ndo_get_stats64	= i40e_get_netdev_stats_struct,
	.ndo_set_rx_mode	= i40e_set_rx_mode,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= i40e_set_mac,
	.ndo_change_mtu		= i40e_change_mtu,
	.ndo_tx_timeout		= i40e_tx_timeout,
	.ndo_vlan_rx_add_vid	= i40e_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= i40e_vlan_rx_kill_vid,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= i40e_netpoll,
#endif
	.ndo_setup_tc		= i40e_setup_tc,
	.ndo_set_features	= i40e_set_features,
	.ndo_set_vf_mac		= i40e_ndo_set_vf_mac,
	.ndo_set_vf_vlan	= i40e_ndo_set_vf_port_vlan,
	.ndo_set_vf_tx_rate	= i40e_ndo_set_vf_bw,
	.ndo_get_vf_config	= i40e_ndo_get_vf_config,
};

/**
 * i40e_config_netdev - Setup the netdev flags
 * @vsi: the VSI being configured
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_config_netdev(struct i40e_vsi *vsi)
{
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_netdev_priv *np;
	struct net_device *netdev;
	u8 mac_addr[ETH_ALEN];
	int etherdev_size;

	etherdev_size = sizeof(struct i40e_netdev_priv);
	netdev = alloc_etherdev_mq(etherdev_size, vsi->alloc_queue_pairs);
	if (!netdev)
		return -ENOMEM;

	vsi->netdev = netdev;
	np = netdev_priv(netdev);
	np->vsi = vsi;

	netdev->hw_enc_features = NETIF_F_IP_CSUM	 |
				  NETIF_F_GSO_UDP_TUNNEL |
				  NETIF_F_TSO		 |
				  NETIF_F_SG;

	netdev->features = NETIF_F_SG		       |
			   NETIF_F_IP_CSUM	       |
			   NETIF_F_SCTP_CSUM	       |
			   NETIF_F_HIGHDMA	       |
			   NETIF_F_GSO_UDP_TUNNEL      |
			   NETIF_F_HW_VLAN_CTAG_TX     |
			   NETIF_F_HW_VLAN_CTAG_RX     |
			   NETIF_F_HW_VLAN_CTAG_FILTER |
			   NETIF_F_IPV6_CSUM	       |
			   NETIF_F_TSO		       |
			   NETIF_F_TSO6		       |
			   NETIF_F_RXCSUM	       |
			   NETIF_F_RXHASH	       |
			   0;

	/* copy netdev features into list of user selectable features */
	netdev->hw_features |= netdev->features;

	if (vsi->type == I40E_VSI_MAIN) {
		SET_NETDEV_DEV(netdev, &pf->pdev->dev);
		memcpy(mac_addr, hw->mac.perm_addr, ETH_ALEN);
	} else {
		/* relate the VSI_VMDQ name to the VSI_MAIN name */
		snprintf(netdev->name, IFNAMSIZ, "%sv%%d",
			 pf->vsi[pf->lan_vsi]->netdev->name);
		random_ether_addr(mac_addr);
		i40e_add_filter(vsi, mac_addr, I40E_VLAN_ANY, false, false);
	}

	memcpy(netdev->dev_addr, mac_addr, ETH_ALEN);
	memcpy(netdev->perm_addr, mac_addr, ETH_ALEN);
	/* vlan gets same features (except vlan offload)
	 * after any tweaks for specific VSI types
	 */
	netdev->vlan_features = netdev->features & ~(NETIF_F_HW_VLAN_CTAG_TX |
						     NETIF_F_HW_VLAN_CTAG_RX |
						   NETIF_F_HW_VLAN_CTAG_FILTER);
	netdev->priv_flags |= IFF_UNICAST_FLT;
	netdev->priv_flags |= IFF_SUPP_NOFCS;
	/* Setup netdev TC information */
	i40e_vsi_config_netdev_tc(vsi, vsi->tc_config.enabled_tc);

	netdev->netdev_ops = &i40e_netdev_ops;
	netdev->watchdog_timeo = 5 * HZ;
	i40e_set_ethtool_ops(netdev);

	return 0;
}

/**
 * i40e_vsi_delete - Delete a VSI from the switch
 * @vsi: the VSI being removed
 *
 * Returns 0 on success, negative value on failure
 **/
static void i40e_vsi_delete(struct i40e_vsi *vsi)
{
	/* remove default VSI is not allowed */
	if (vsi == vsi->back->vsi[vsi->back->lan_vsi])
		return;

	/* there is no HW VSI for FDIR */
	if (vsi->type == I40E_VSI_FDIR)
		return;

	i40e_aq_delete_element(&vsi->back->hw, vsi->seid, NULL);
	return;
}

/**
 * i40e_add_vsi - Add a VSI to the switch
 * @vsi: the VSI being configured
 *
 * This initializes a VSI context depending on the VSI type to be added and
 * passes it down to the add_vsi aq command.
 **/
static int i40e_add_vsi(struct i40e_vsi *vsi)
{
	int ret = -ENODEV;
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_pf *pf = vsi->back;
	struct i40e_hw *hw = &pf->hw;
	struct i40e_vsi_context ctxt;
	u8 enabled_tc = 0x1; /* TC0 enabled */
	int f_count = 0;

	memset(&ctxt, 0, sizeof(ctxt));
	switch (vsi->type) {
	case I40E_VSI_MAIN:
		/* The PF's main VSI is already setup as part of the
		 * device initialization, so we'll not bother with
		 * the add_vsi call, but we will retrieve the current
		 * VSI context.
		 */
		ctxt.seid = pf->main_vsi_seid;
		ctxt.pf_num = pf->hw.pf_id;
		ctxt.vf_num = 0;
		ret = i40e_aq_get_vsi_params(&pf->hw, &ctxt, NULL);
		ctxt.flags = I40E_AQ_VSI_TYPE_PF;
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "couldn't get pf vsi config, err %d, aq_err %d\n",
				 ret, pf->hw.aq.asq_last_status);
			return -ENOENT;
		}
		memcpy(&vsi->info, &ctxt.info, sizeof(ctxt.info));
		vsi->info.valid_sections = 0;

		vsi->seid = ctxt.seid;
		vsi->id = ctxt.vsi_number;

		enabled_tc = i40e_pf_get_tc_map(pf);

		/* MFP mode setup queue map and update VSI */
		if (pf->flags & I40E_FLAG_MFP_ENABLED) {
			memset(&ctxt, 0, sizeof(ctxt));
			ctxt.seid = pf->main_vsi_seid;
			ctxt.pf_num = pf->hw.pf_id;
			ctxt.vf_num = 0;
			i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, false);
			ret = i40e_aq_update_vsi_params(hw, &ctxt, NULL);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "update vsi failed, aq_err=%d\n",
					 pf->hw.aq.asq_last_status);
				ret = -ENOENT;
				goto err;
			}
			/* update the local VSI info queue map */
			i40e_vsi_update_queue_map(vsi, &ctxt);
			vsi->info.valid_sections = 0;
		} else {
			/* Default/Main VSI is only enabled for TC0
			 * reconfigure it to enable all TCs that are
			 * available on the port in SFP mode.
			 */
			ret = i40e_vsi_config_tc(vsi, enabled_tc);
			if (ret) {
				dev_info(&pf->pdev->dev,
					 "failed to configure TCs for main VSI tc_map 0x%08x, err %d, aq_err %d\n",
					 enabled_tc, ret,
					 pf->hw.aq.asq_last_status);
				ret = -ENOENT;
			}
		}
		break;

	case I40E_VSI_FDIR:
		/* no queue mapping or actual HW VSI needed */
		vsi->info.valid_sections = 0;
		vsi->seid = 0;
		vsi->id = 0;
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		return 0;
		break;

	case I40E_VSI_VMDQ2:
		ctxt.pf_num = hw->pf_id;
		ctxt.vf_num = 0;
		ctxt.uplink_seid = vsi->uplink_seid;
		ctxt.connection_type = 0x1;     /* regular data port */
		ctxt.flags = I40E_AQ_VSI_TYPE_VMDQ2;

		ctxt.info.valid_sections |= cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);

		/* This VSI is connected to VEB so the switch_id
		 * should be set to zero by default.
		 */
		ctxt.info.switch_id = 0;
		ctxt.info.switch_id |= cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_LOCAL_LB);
		ctxt.info.switch_id |= cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);

		/* Setup the VSI tx/rx queue map for TC0 only for now */
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		break;

	case I40E_VSI_SRIOV:
		ctxt.pf_num = hw->pf_id;
		ctxt.vf_num = vsi->vf_id + hw->func_caps.vf_base_id;
		ctxt.uplink_seid = vsi->uplink_seid;
		ctxt.connection_type = 0x1;     /* regular data port */
		ctxt.flags = I40E_AQ_VSI_TYPE_VF;

		ctxt.info.valid_sections |= cpu_to_le16(I40E_AQ_VSI_PROP_SWITCH_VALID);

		/* This VSI is connected to VEB so the switch_id
		 * should be set to zero by default.
		 */
		ctxt.info.switch_id = cpu_to_le16(I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB);

		ctxt.info.valid_sections |= cpu_to_le16(I40E_AQ_VSI_PROP_VLAN_VALID);
		ctxt.info.port_vlan_flags |= I40E_AQ_VSI_PVLAN_MODE_ALL;
		/* Setup the VSI tx/rx queue map for TC0 only for now */
		i40e_vsi_setup_queue_map(vsi, &ctxt, enabled_tc, true);
		break;

	default:
		return -ENODEV;
	}

	if (vsi->type != I40E_VSI_MAIN) {
		ret = i40e_aq_add_vsi(hw, &ctxt, NULL);
		if (ret) {
			dev_info(&vsi->back->pdev->dev,
				 "add vsi failed, aq_err=%d\n",
				 vsi->back->hw.aq.asq_last_status);
			ret = -ENOENT;
			goto err;
		}
		memcpy(&vsi->info, &ctxt.info, sizeof(ctxt.info));
		vsi->info.valid_sections = 0;
		vsi->seid = ctxt.seid;
		vsi->id = ctxt.vsi_number;
	}

	/* If macvlan filters already exist, force them to get loaded */
	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list) {
		f->changed = true;
		f_count++;
	}
	if (f_count) {
		vsi->flags |= I40E_VSI_FLAG_FILTER_CHANGED;
		pf->flags |= I40E_FLAG_FILTER_SYNC;
	}

	/* Update VSI BW information */
	ret = i40e_vsi_get_bw_info(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't get vsi bw info, err %d, aq_err %d\n",
			 ret, pf->hw.aq.asq_last_status);
		/* VSI is already added so not tearing that up */
		ret = 0;
	}

err:
	return ret;
}

/**
 * i40e_vsi_release - Delete a VSI and free its resources
 * @vsi: the VSI being removed
 *
 * Returns 0 on success or < 0 on error
 **/
int i40e_vsi_release(struct i40e_vsi *vsi)
{
	struct i40e_mac_filter *f, *ftmp;
	struct i40e_veb *veb = NULL;
	struct i40e_pf *pf;
	u16 uplink_seid;
	int i, n;

	pf = vsi->back;

	/* release of a VEB-owner or last VSI is not allowed */
	if (vsi->flags & I40E_VSI_FLAG_VEB_OWNER) {
		dev_info(&pf->pdev->dev, "VSI %d has existing VEB %d\n",
			 vsi->seid, vsi->uplink_seid);
		return -ENODEV;
	}
	if (vsi == pf->vsi[pf->lan_vsi] &&
	    !test_bit(__I40E_DOWN, &pf->state)) {
		dev_info(&pf->pdev->dev, "Can't remove PF VSI\n");
		return -ENODEV;
	}

	uplink_seid = vsi->uplink_seid;
	if (vsi->type != I40E_VSI_SRIOV) {
		if (vsi->netdev_registered) {
			vsi->netdev_registered = false;
			if (vsi->netdev) {
				/* results in a call to i40e_close() */
				unregister_netdev(vsi->netdev);
				free_netdev(vsi->netdev);
				vsi->netdev = NULL;
			}
		} else {
			if (!test_and_set_bit(__I40E_DOWN, &vsi->state))
				i40e_down(vsi);
			i40e_vsi_free_irq(vsi);
			i40e_vsi_free_tx_resources(vsi);
			i40e_vsi_free_rx_resources(vsi);
		}
		i40e_vsi_disable_irq(vsi);
	}

	list_for_each_entry_safe(f, ftmp, &vsi->mac_filter_list, list)
		i40e_del_filter(vsi, f->macaddr, f->vlan,
				f->is_vf, f->is_netdev);
	i40e_sync_vsi_filters(vsi);

	i40e_vsi_delete(vsi);
	i40e_vsi_free_q_vectors(vsi);
	i40e_vsi_clear_rings(vsi);
	i40e_vsi_clear(vsi);

	/* If this was the last thing on the VEB, except for the
	 * controlling VSI, remove the VEB, which puts the controlling
	 * VSI onto the next level down in the switch.
	 *
	 * Well, okay, there's one more exception here: don't remove
	 * the orphan VEBs yet.  We'll wait for an explicit remove request
	 * from up the network stack.
	 */
	for (n = 0, i = 0; i < pf->hw.func_caps.num_vsis; i++) {
		if (pf->vsi[i] &&
		    pf->vsi[i]->uplink_seid == uplink_seid &&
		    (pf->vsi[i]->flags & I40E_VSI_FLAG_VEB_OWNER) == 0) {
			n++;      /* count the VSIs */
		}
	}
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;
		if (pf->veb[i]->uplink_seid == uplink_seid)
			n++;     /* count the VEBs */
		if (pf->veb[i]->seid == uplink_seid)
			veb = pf->veb[i];
	}
	if (n == 0 && veb && veb->uplink_seid != 0)
		i40e_veb_release(veb);

	return 0;
}

/**
 * i40e_vsi_setup_vectors - Set up the q_vectors for the given VSI
 * @vsi: ptr to the VSI
 *
 * This should only be called after i40e_vsi_mem_alloc() which allocates the
 * corresponding SW VSI structure and initializes num_queue_pairs for the
 * newly allocated VSI.
 *
 * Returns 0 on success or negative on failure
 **/
static int i40e_vsi_setup_vectors(struct i40e_vsi *vsi)
{
	int ret = -ENOENT;
	struct i40e_pf *pf = vsi->back;

	if (vsi->q_vectors[0]) {
		dev_info(&pf->pdev->dev, "VSI %d has existing q_vectors\n",
			 vsi->seid);
		return -EEXIST;
	}

	if (vsi->base_vector) {
		dev_info(&pf->pdev->dev,
			 "VSI %d has non-zero base vector %d\n",
			 vsi->seid, vsi->base_vector);
		return -EEXIST;
	}

	ret = i40e_alloc_q_vectors(vsi);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "failed to allocate %d q_vector for VSI %d, ret=%d\n",
			 vsi->num_q_vectors, vsi->seid, ret);
		vsi->num_q_vectors = 0;
		goto vector_setup_out;
	}

	if (vsi->num_q_vectors)
		vsi->base_vector = i40e_get_lump(pf, pf->irq_pile,
						 vsi->num_q_vectors, vsi->idx);
	if (vsi->base_vector < 0) {
		dev_info(&pf->pdev->dev,
			 "failed to get q tracking for VSI %d, err=%d\n",
			 vsi->seid, vsi->base_vector);
		i40e_vsi_free_q_vectors(vsi);
		ret = -ENOENT;
		goto vector_setup_out;
	}

vector_setup_out:
	return ret;
}

/**
 * i40e_vsi_setup - Set up a VSI by a given type
 * @pf: board private structure
 * @type: VSI type
 * @uplink_seid: the switch element to link to
 * @param1: usage depends upon VSI type. For VF types, indicates VF id
 *
 * This allocates the sw VSI structure and its queue resources, then add a VSI
 * to the identified VEB.
 *
 * Returns pointer to the successfully allocated and configure VSI sw struct on
 * success, otherwise returns NULL on failure.
 **/
struct i40e_vsi *i40e_vsi_setup(struct i40e_pf *pf, u8 type,
				u16 uplink_seid, u32 param1)
{
	struct i40e_vsi *vsi = NULL;
	struct i40e_veb *veb = NULL;
	int ret, i;
	int v_idx;

	/* The requested uplink_seid must be either
	 *     - the PF's port seid
	 *              no VEB is needed because this is the PF
	 *              or this is a Flow Director special case VSI
	 *     - seid of an existing VEB
	 *     - seid of a VSI that owns an existing VEB
	 *     - seid of a VSI that doesn't own a VEB
	 *              a new VEB is created and the VSI becomes the owner
	 *     - seid of the PF VSI, which is what creates the first VEB
	 *              this is a special case of the previous
	 *
	 * Find which uplink_seid we were given and create a new VEB if needed
	 */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (pf->veb[i] && pf->veb[i]->seid == uplink_seid) {
			veb = pf->veb[i];
			break;
		}
	}

	if (!veb && uplink_seid != pf->mac_seid) {

		for (i = 0; i < pf->hw.func_caps.num_vsis; i++) {
			if (pf->vsi[i] && pf->vsi[i]->seid == uplink_seid) {
				vsi = pf->vsi[i];
				break;
			}
		}
		if (!vsi) {
			dev_info(&pf->pdev->dev, "no such uplink_seid %d\n",
				 uplink_seid);
			return NULL;
		}

		if (vsi->uplink_seid == pf->mac_seid)
			veb = i40e_veb_setup(pf, 0, pf->mac_seid, vsi->seid,
					     vsi->tc_config.enabled_tc);
		else if ((vsi->flags & I40E_VSI_FLAG_VEB_OWNER) == 0)
			veb = i40e_veb_setup(pf, 0, vsi->uplink_seid, vsi->seid,
					     vsi->tc_config.enabled_tc);

		for (i = 0; i < I40E_MAX_VEB && !veb; i++) {
			if (pf->veb[i] && pf->veb[i]->seid == vsi->uplink_seid)
				veb = pf->veb[i];
		}
		if (!veb) {
			dev_info(&pf->pdev->dev, "couldn't add VEB\n");
			return NULL;
		}

		vsi->flags |= I40E_VSI_FLAG_VEB_OWNER;
		uplink_seid = veb->seid;
	}

	/* get vsi sw struct */
	v_idx = i40e_vsi_mem_alloc(pf, type);
	if (v_idx < 0)
		goto err_alloc;
	vsi = pf->vsi[v_idx];
	vsi->type = type;
	vsi->veb_idx = (veb ? veb->idx : I40E_NO_VEB);

	if (type == I40E_VSI_MAIN)
		pf->lan_vsi = v_idx;
	else if (type == I40E_VSI_SRIOV)
		vsi->vf_id = param1;
	/* assign it some queues */
	ret = i40e_get_lump(pf, pf->qp_pile, vsi->alloc_queue_pairs, vsi->idx);
	if (ret < 0) {
		dev_info(&pf->pdev->dev, "VSI %d get_lump failed %d\n",
			 vsi->seid, ret);
		goto err_vsi;
	}
	vsi->base_queue = ret;

	/* get a VSI from the hardware */
	vsi->uplink_seid = uplink_seid;
	ret = i40e_add_vsi(vsi);
	if (ret)
		goto err_vsi;

	switch (vsi->type) {
	/* setup the netdev if needed */
	case I40E_VSI_MAIN:
	case I40E_VSI_VMDQ2:
		ret = i40e_config_netdev(vsi);
		if (ret)
			goto err_netdev;
		ret = register_netdev(vsi->netdev);
		if (ret)
			goto err_netdev;
		vsi->netdev_registered = true;
		netif_carrier_off(vsi->netdev);
		/* fall through */

	case I40E_VSI_FDIR:
		/* set up vectors and rings if needed */
		ret = i40e_vsi_setup_vectors(vsi);
		if (ret)
			goto err_msix;

		ret = i40e_alloc_rings(vsi);
		if (ret)
			goto err_rings;

		/* map all of the rings to the q_vectors */
		i40e_vsi_map_rings_to_vectors(vsi);

		i40e_vsi_reset_stats(vsi);
		break;

	default:
		/* no netdev or rings for the other VSI types */
		break;
	}

	return vsi;

err_rings:
	i40e_vsi_free_q_vectors(vsi);
err_msix:
	if (vsi->netdev_registered) {
		vsi->netdev_registered = false;
		unregister_netdev(vsi->netdev);
		free_netdev(vsi->netdev);
		vsi->netdev = NULL;
	}
err_netdev:
	i40e_aq_delete_element(&pf->hw, vsi->seid, NULL);
err_vsi:
	i40e_vsi_clear(vsi);
err_alloc:
	return NULL;
}

/**
 * i40e_veb_get_bw_info - Query VEB BW information
 * @veb: the veb to query
 *
 * Query the Tx scheduler BW configuration data for given VEB
 **/
static int i40e_veb_get_bw_info(struct i40e_veb *veb)
{
	struct i40e_aqc_query_switching_comp_ets_config_resp ets_data;
	struct i40e_aqc_query_switching_comp_bw_config_resp bw_data;
	struct i40e_pf *pf = veb->pf;
	struct i40e_hw *hw = &pf->hw;
	u32 tc_bw_max;
	int ret = 0;
	int i;

	ret = i40e_aq_query_switch_comp_bw_config(hw, veb->seid,
						  &bw_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "query veb bw config failed, aq_err=%d\n",
			 hw->aq.asq_last_status);
		goto out;
	}

	ret = i40e_aq_query_switch_comp_ets_config(hw, veb->seid,
						   &ets_data, NULL);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "query veb bw ets config failed, aq_err=%d\n",
			 hw->aq.asq_last_status);
		goto out;
	}

	veb->bw_limit = le16_to_cpu(ets_data.port_bw_limit);
	veb->bw_max_quanta = ets_data.tc_bw_max;
	veb->is_abs_credits = bw_data.absolute_credits_enable;
	tc_bw_max = le16_to_cpu(bw_data.tc_bw_max[0]) |
		    (le16_to_cpu(bw_data.tc_bw_max[1]) << 16);
	for (i = 0; i < I40E_MAX_TRAFFIC_CLASS; i++) {
		veb->bw_tc_share_credits[i] = bw_data.tc_bw_share_credits[i];
		veb->bw_tc_limit_credits[i] =
					le16_to_cpu(bw_data.tc_bw_limits[i]);
		veb->bw_tc_max_quanta[i] = ((tc_bw_max >> (i*4)) & 0x7);
	}

out:
	return ret;
}

/**
 * i40e_veb_mem_alloc - Allocates the next available struct veb in the PF
 * @pf: board private structure
 *
 * On error: returns error code (negative)
 * On success: returns vsi index in PF (positive)
 **/
static int i40e_veb_mem_alloc(struct i40e_pf *pf)
{
	int ret = -ENOENT;
	struct i40e_veb *veb;
	int i;

	/* Need to protect the allocation of switch elements at the PF level */
	mutex_lock(&pf->switch_mutex);

	/* VEB list may be fragmented if VEB creation/destruction has
	 * been happening.  We can afford to do a quick scan to look
	 * for any free slots in the list.
	 *
	 * find next empty veb slot, looping back around if necessary
	 */
	i = 0;
	while ((i < I40E_MAX_VEB) && (pf->veb[i] != NULL))
		i++;
	if (i >= I40E_MAX_VEB) {
		ret = -ENOMEM;
		goto err_alloc_veb;  /* out of VEB slots! */
	}

	veb = kzalloc(sizeof(*veb), GFP_KERNEL);
	if (!veb) {
		ret = -ENOMEM;
		goto err_alloc_veb;
	}
	veb->pf = pf;
	veb->idx = i;
	veb->enabled_tc = 1;

	pf->veb[i] = veb;
	ret = i;
err_alloc_veb:
	mutex_unlock(&pf->switch_mutex);
	return ret;
}

/**
 * i40e_switch_branch_release - Delete a branch of the switch tree
 * @branch: where to start deleting
 *
 * This uses recursion to find the tips of the branch to be
 * removed, deleting until we get back to and can delete this VEB.
 **/
static void i40e_switch_branch_release(struct i40e_veb *branch)
{
	struct i40e_pf *pf = branch->pf;
	u16 branch_seid = branch->seid;
	u16 veb_idx = branch->idx;
	int i;

	/* release any VEBs on this VEB - RECURSION */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;
		if (pf->veb[i]->uplink_seid == branch->seid)
			i40e_switch_branch_release(pf->veb[i]);
	}

	/* Release the VSIs on this VEB, but not the owner VSI.
	 *
	 * NOTE: Removing the last VSI on a VEB has the SIDE EFFECT of removing
	 *       the VEB itself, so don't use (*branch) after this loop.
	 */
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++) {
		if (!pf->vsi[i])
			continue;
		if (pf->vsi[i]->uplink_seid == branch_seid &&
		   (pf->vsi[i]->flags & I40E_VSI_FLAG_VEB_OWNER) == 0) {
			i40e_vsi_release(pf->vsi[i]);
		}
	}

	/* There's one corner case where the VEB might not have been
	 * removed, so double check it here and remove it if needed.
	 * This case happens if the veb was created from the debugfs
	 * commands and no VSIs were added to it.
	 */
	if (pf->veb[veb_idx])
		i40e_veb_release(pf->veb[veb_idx]);
}

/**
 * i40e_veb_clear - remove veb struct
 * @veb: the veb to remove
 **/
static void i40e_veb_clear(struct i40e_veb *veb)
{
	if (!veb)
		return;

	if (veb->pf) {
		struct i40e_pf *pf = veb->pf;

		mutex_lock(&pf->switch_mutex);
		if (pf->veb[veb->idx] == veb)
			pf->veb[veb->idx] = NULL;
		mutex_unlock(&pf->switch_mutex);
	}

	kfree(veb);
}

/**
 * i40e_veb_release - Delete a VEB and free its resources
 * @veb: the VEB being removed
 **/
void i40e_veb_release(struct i40e_veb *veb)
{
	struct i40e_vsi *vsi = NULL;
	struct i40e_pf *pf;
	int i, n = 0;

	pf = veb->pf;

	/* find the remaining VSI and check for extras */
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++) {
		if (pf->vsi[i] && pf->vsi[i]->uplink_seid == veb->seid) {
			n++;
			vsi = pf->vsi[i];
		}
	}
	if (n != 1) {
		dev_info(&pf->pdev->dev,
			 "can't remove VEB %d with %d VSIs left\n",
			 veb->seid, n);
		return;
	}

	/* move the remaining VSI to uplink veb */
	vsi->flags &= ~I40E_VSI_FLAG_VEB_OWNER;
	if (veb->uplink_seid) {
		vsi->uplink_seid = veb->uplink_seid;
		if (veb->uplink_seid == pf->mac_seid)
			vsi->veb_idx = I40E_NO_VEB;
		else
			vsi->veb_idx = veb->veb_idx;
	} else {
		/* floating VEB */
		vsi->uplink_seid = pf->vsi[pf->lan_vsi]->uplink_seid;
		vsi->veb_idx = pf->vsi[pf->lan_vsi]->veb_idx;
	}

	i40e_aq_delete_element(&pf->hw, veb->seid, NULL);
	i40e_veb_clear(veb);

	return;
}

/**
 * i40e_add_veb - create the VEB in the switch
 * @veb: the VEB to be instantiated
 * @vsi: the controlling VSI
 **/
static int i40e_add_veb(struct i40e_veb *veb, struct i40e_vsi *vsi)
{
	bool is_default = (vsi->idx == vsi->back->lan_vsi);
	int ret;

	/* get a VEB from the hardware */
	ret = i40e_aq_add_veb(&veb->pf->hw, veb->uplink_seid, vsi->seid,
			      veb->enabled_tc, is_default, &veb->seid, NULL);
	if (ret) {
		dev_info(&veb->pf->pdev->dev,
			 "couldn't add VEB, err %d, aq_err %d\n",
			 ret, veb->pf->hw.aq.asq_last_status);
		return -EPERM;
	}

	/* get statistics counter */
	ret = i40e_aq_get_veb_parameters(&veb->pf->hw, veb->seid, NULL, NULL,
					 &veb->stats_idx, NULL, NULL, NULL);
	if (ret) {
		dev_info(&veb->pf->pdev->dev,
			 "couldn't get VEB statistics idx, err %d, aq_err %d\n",
			 ret, veb->pf->hw.aq.asq_last_status);
		return -EPERM;
	}
	ret = i40e_veb_get_bw_info(veb);
	if (ret) {
		dev_info(&veb->pf->pdev->dev,
			 "couldn't get VEB bw info, err %d, aq_err %d\n",
			 ret, veb->pf->hw.aq.asq_last_status);
		i40e_aq_delete_element(&veb->pf->hw, veb->seid, NULL);
		return -ENOENT;
	}

	vsi->uplink_seid = veb->seid;
	vsi->veb_idx = veb->idx;
	vsi->flags |= I40E_VSI_FLAG_VEB_OWNER;

	return 0;
}

/**
 * i40e_veb_setup - Set up a VEB
 * @pf: board private structure
 * @flags: VEB setup flags
 * @uplink_seid: the switch element to link to
 * @vsi_seid: the initial VSI seid
 * @enabled_tc: Enabled TC bit-map
 *
 * This allocates the sw VEB structure and links it into the switch
 * It is possible and legal for this to be a duplicate of an already
 * existing VEB.  It is also possible for both uplink and vsi seids
 * to be zero, in order to create a floating VEB.
 *
 * Returns pointer to the successfully allocated VEB sw struct on
 * success, otherwise returns NULL on failure.
 **/
struct i40e_veb *i40e_veb_setup(struct i40e_pf *pf, u16 flags,
				u16 uplink_seid, u16 vsi_seid,
				u8 enabled_tc)
{
	struct i40e_veb *veb, *uplink_veb = NULL;
	int vsi_idx, veb_idx;
	int ret;

	/* if one seid is 0, the other must be 0 to create a floating relay */
	if ((uplink_seid == 0 || vsi_seid == 0) &&
	    (uplink_seid + vsi_seid != 0)) {
		dev_info(&pf->pdev->dev,
			 "one, not both seid's are 0: uplink=%d vsi=%d\n",
			 uplink_seid, vsi_seid);
		return NULL;
	}

	/* make sure there is such a vsi and uplink */
	for (vsi_idx = 0; vsi_idx < pf->hw.func_caps.num_vsis; vsi_idx++)
		if (pf->vsi[vsi_idx] && pf->vsi[vsi_idx]->seid == vsi_seid)
			break;
	if (vsi_idx >= pf->hw.func_caps.num_vsis && vsi_seid != 0) {
		dev_info(&pf->pdev->dev, "vsi seid %d not found\n",
			 vsi_seid);
		return NULL;
	}

	if (uplink_seid && uplink_seid != pf->mac_seid) {
		for (veb_idx = 0; veb_idx < I40E_MAX_VEB; veb_idx++) {
			if (pf->veb[veb_idx] &&
			    pf->veb[veb_idx]->seid == uplink_seid) {
				uplink_veb = pf->veb[veb_idx];
				break;
			}
		}
		if (!uplink_veb) {
			dev_info(&pf->pdev->dev,
				 "uplink seid %d not found\n", uplink_seid);
			return NULL;
		}
	}

	/* get veb sw struct */
	veb_idx = i40e_veb_mem_alloc(pf);
	if (veb_idx < 0)
		goto err_alloc;
	veb = pf->veb[veb_idx];
	veb->flags = flags;
	veb->uplink_seid = uplink_seid;
	veb->veb_idx = (uplink_veb ? uplink_veb->idx : I40E_NO_VEB);
	veb->enabled_tc = (enabled_tc ? enabled_tc : 0x1);

	/* create the VEB in the switch */
	ret = i40e_add_veb(veb, pf->vsi[vsi_idx]);
	if (ret)
		goto err_veb;

	return veb;

err_veb:
	i40e_veb_clear(veb);
err_alloc:
	return NULL;
}

/**
 * i40e_setup_pf_switch_element - set pf vars based on switch type
 * @pf: board private structure
 * @ele: element we are building info from
 * @num_reported: total number of elements
 * @printconfig: should we print the contents
 *
 * helper function to assist in extracting a few useful SEID values.
 **/
static void i40e_setup_pf_switch_element(struct i40e_pf *pf,
				struct i40e_aqc_switch_config_element_resp *ele,
				u16 num_reported, bool printconfig)
{
	u16 downlink_seid = le16_to_cpu(ele->downlink_seid);
	u16 uplink_seid = le16_to_cpu(ele->uplink_seid);
	u8 element_type = ele->element_type;
	u16 seid = le16_to_cpu(ele->seid);

	if (printconfig)
		dev_info(&pf->pdev->dev,
			 "type=%d seid=%d uplink=%d downlink=%d\n",
			 element_type, seid, uplink_seid, downlink_seid);

	switch (element_type) {
	case I40E_SWITCH_ELEMENT_TYPE_MAC:
		pf->mac_seid = seid;
		break;
	case I40E_SWITCH_ELEMENT_TYPE_VEB:
		/* Main VEB? */
		if (uplink_seid != pf->mac_seid)
			break;
		if (pf->lan_veb == I40E_NO_VEB) {
			int v;

			/* find existing or else empty VEB */
			for (v = 0; v < I40E_MAX_VEB; v++) {
				if (pf->veb[v] && (pf->veb[v]->seid == seid)) {
					pf->lan_veb = v;
					break;
				}
			}
			if (pf->lan_veb == I40E_NO_VEB) {
				v = i40e_veb_mem_alloc(pf);
				if (v < 0)
					break;
				pf->lan_veb = v;
			}
		}

		pf->veb[pf->lan_veb]->seid = seid;
		pf->veb[pf->lan_veb]->uplink_seid = pf->mac_seid;
		pf->veb[pf->lan_veb]->pf = pf;
		pf->veb[pf->lan_veb]->veb_idx = I40E_NO_VEB;
		break;
	case I40E_SWITCH_ELEMENT_TYPE_VSI:
		if (num_reported != 1)
			break;
		/* This is immediately after a reset so we can assume this is
		 * the PF's VSI
		 */
		pf->mac_seid = uplink_seid;
		pf->pf_seid = downlink_seid;
		pf->main_vsi_seid = seid;
		if (printconfig)
			dev_info(&pf->pdev->dev,
				 "pf_seid=%d main_vsi_seid=%d\n",
				 pf->pf_seid, pf->main_vsi_seid);
		break;
	case I40E_SWITCH_ELEMENT_TYPE_PF:
	case I40E_SWITCH_ELEMENT_TYPE_VF:
	case I40E_SWITCH_ELEMENT_TYPE_EMP:
	case I40E_SWITCH_ELEMENT_TYPE_BMC:
	case I40E_SWITCH_ELEMENT_TYPE_PE:
	case I40E_SWITCH_ELEMENT_TYPE_PA:
		/* ignore these for now */
		break;
	default:
		dev_info(&pf->pdev->dev, "unknown element type=%d seid=%d\n",
			 element_type, seid);
		break;
	}
}

/**
 * i40e_fetch_switch_configuration - Get switch config from firmware
 * @pf: board private structure
 * @printconfig: should we print the contents
 *
 * Get the current switch configuration from the device and
 * extract a few useful SEID values.
 **/
int i40e_fetch_switch_configuration(struct i40e_pf *pf, bool printconfig)
{
	struct i40e_aqc_get_switch_config_resp *sw_config;
	u16 next_seid = 0;
	int ret = 0;
	u8 *aq_buf;
	int i;

	aq_buf = kzalloc(I40E_AQ_LARGE_BUF, GFP_KERNEL);
	if (!aq_buf)
		return -ENOMEM;

	sw_config = (struct i40e_aqc_get_switch_config_resp *)aq_buf;
	do {
		u16 num_reported, num_total;

		ret = i40e_aq_get_switch_config(&pf->hw, sw_config,
						I40E_AQ_LARGE_BUF,
						&next_seid, NULL);
		if (ret) {
			dev_info(&pf->pdev->dev,
				 "get switch config failed %d aq_err=%x\n",
				 ret, pf->hw.aq.asq_last_status);
			kfree(aq_buf);
			return -ENOENT;
		}

		num_reported = le16_to_cpu(sw_config->header.num_reported);
		num_total = le16_to_cpu(sw_config->header.num_total);

		if (printconfig)
			dev_info(&pf->pdev->dev,
				 "header: %d reported %d total\n",
				 num_reported, num_total);

		if (num_reported) {
			int sz = sizeof(*sw_config) * num_reported;

			kfree(pf->sw_config);
			pf->sw_config = kzalloc(sz, GFP_KERNEL);
			if (pf->sw_config)
				memcpy(pf->sw_config, sw_config, sz);
		}

		for (i = 0; i < num_reported; i++) {
			struct i40e_aqc_switch_config_element_resp *ele =
				&sw_config->element[i];

			i40e_setup_pf_switch_element(pf, ele, num_reported,
						     printconfig);
		}
	} while (next_seid != 0);

	kfree(aq_buf);
	return ret;
}

/**
 * i40e_setup_pf_switch - Setup the HW switch on startup or after reset
 * @pf: board private structure
 *
 * Returns 0 on success, negative value on failure
 **/
static int i40e_setup_pf_switch(struct i40e_pf *pf)
{
	int ret;

	/* find out what's out there already */
	ret = i40e_fetch_switch_configuration(pf, false);
	if (ret) {
		dev_info(&pf->pdev->dev,
			 "couldn't fetch switch config, err %d, aq_err %d\n",
			 ret, pf->hw.aq.asq_last_status);
		return ret;
	}
	i40e_pf_reset_stats(pf);

	/* fdir VSI must happen first to be sure it gets queue 0, but only
	 * if there is enough room for the fdir VSI
	 */
	if (pf->num_lan_qps > 1)
		i40e_fdir_setup(pf);

	/* first time setup */
	if (pf->lan_vsi == I40E_NO_VSI) {
		struct i40e_vsi *vsi = NULL;
		u16 uplink_seid;

		/* Set up the PF VSI associated with the PF's main VSI
		 * that is already in the HW switch
		 */
		if (pf->lan_veb != I40E_NO_VEB && pf->veb[pf->lan_veb])
			uplink_seid = pf->veb[pf->lan_veb]->seid;
		else
			uplink_seid = pf->mac_seid;

		vsi = i40e_vsi_setup(pf, I40E_VSI_MAIN, uplink_seid, 0);
		if (!vsi) {
			dev_info(&pf->pdev->dev, "setup of MAIN VSI failed\n");
			i40e_fdir_teardown(pf);
			return -EAGAIN;
		}
		/* accommodate kcompat by copying the main VSI queue count
		 * into the pf, since this newer code pushes the pf queue
		 * info down a level into a VSI
		 */
		pf->num_rx_queues = vsi->alloc_queue_pairs;
		pf->num_tx_queues = vsi->alloc_queue_pairs;
	} else {
		/* force a reset of TC and queue layout configurations */
		u8 enabled_tc = pf->vsi[pf->lan_vsi]->tc_config.enabled_tc;
		pf->vsi[pf->lan_vsi]->tc_config.enabled_tc = 0;
		pf->vsi[pf->lan_vsi]->seid = pf->main_vsi_seid;
		i40e_vsi_config_tc(pf->vsi[pf->lan_vsi], enabled_tc);
	}
	i40e_vlan_stripping_disable(pf->vsi[pf->lan_vsi]);

	/* Setup static PF queue filter control settings */
	ret = i40e_setup_pf_filter_control(pf);
	if (ret) {
		dev_info(&pf->pdev->dev, "setup_pf_filter_control failed: %d\n",
			 ret);
		/* Failure here should not stop continuing other steps */
	}

	/* enable RSS in the HW, even for only one queue, as the stack can use
	 * the hash
	 */
	if ((pf->flags & I40E_FLAG_RSS_ENABLED))
		i40e_config_rss(pf);

	/* fill in link information and enable LSE reporting */
	i40e_aq_get_link_info(&pf->hw, true, NULL, NULL);
	i40e_link_event(pf);

	/* Initialize user-specifics link properties */
	pf->fc_autoneg_status = ((pf->hw.phy.link_info.an_info &
				  I40E_AQ_AN_COMPLETED) ? true : false);
	pf->hw.fc.requested_mode = I40E_FC_DEFAULT;
	if (pf->hw.phy.link_info.an_info &
	   (I40E_AQ_LINK_PAUSE_TX | I40E_AQ_LINK_PAUSE_RX))
		pf->hw.fc.current_mode = I40E_FC_FULL;
	else if (pf->hw.phy.link_info.an_info & I40E_AQ_LINK_PAUSE_TX)
		pf->hw.fc.current_mode = I40E_FC_TX_PAUSE;
	else if (pf->hw.phy.link_info.an_info & I40E_AQ_LINK_PAUSE_RX)
		pf->hw.fc.current_mode = I40E_FC_RX_PAUSE;
	else
		pf->hw.fc.current_mode = I40E_FC_DEFAULT;

	return ret;
}

/**
 * i40e_set_rss_size - helper to set rss_size
 * @pf: board private structure
 * @queues_left: how many queues
 */
static u16 i40e_set_rss_size(struct i40e_pf *pf, int queues_left)
{
	int num_tc0;

	num_tc0 = min_t(int, queues_left, pf->rss_size_max);
	num_tc0 = min_t(int, num_tc0, nr_cpus_node(numa_node_id()));
	num_tc0 = rounddown_pow_of_two(num_tc0);

	return num_tc0;
}

/**
 * i40e_determine_queue_usage - Work out queue distribution
 * @pf: board private structure
 **/
static void i40e_determine_queue_usage(struct i40e_pf *pf)
{
	int accum_tc_size;
	int queues_left;

	pf->num_lan_qps = 0;
	pf->num_tc_qps = rounddown_pow_of_two(pf->num_tc_qps);
	accum_tc_size = (I40E_MAX_TRAFFIC_CLASS - 1) * pf->num_tc_qps;

	/* Find the max queues to be put into basic use.  We'll always be
	 * using TC0, whether or not DCB is running, and TC0 will get the
	 * big RSS set.
	 */
	queues_left = pf->hw.func_caps.num_tx_qp;

	if   (!((pf->flags & I40E_FLAG_MSIX_ENABLED)		 &&
		(pf->flags & I40E_FLAG_MQ_ENABLED))		 ||
		!(pf->flags & (I40E_FLAG_RSS_ENABLED |
		I40E_FLAG_FDIR_ENABLED | I40E_FLAG_DCB_ENABLED)) ||
		(queues_left == 1)) {

		/* one qp for PF, no queues for anything else */
		queues_left = 0;
		pf->rss_size = pf->num_lan_qps = 1;

		/* make sure all the fancies are disabled */
		pf->flags &= ~(I40E_FLAG_RSS_ENABLED       |
				I40E_FLAG_MQ_ENABLED	   |
				I40E_FLAG_FDIR_ENABLED	   |
				I40E_FLAG_FDIR_ATR_ENABLED |
				I40E_FLAG_DCB_ENABLED	   |
				I40E_FLAG_SRIOV_ENABLED	   |
				I40E_FLAG_VMDQ_ENABLED);

	} else if (pf->flags & I40E_FLAG_RSS_ENABLED	  &&
		   !(pf->flags & I40E_FLAG_FDIR_ENABLED)  &&
		   !(pf->flags & I40E_FLAG_DCB_ENABLED)) {

		pf->rss_size = i40e_set_rss_size(pf, queues_left);

		queues_left -= pf->rss_size;
		pf->num_lan_qps = pf->rss_size;

	} else if (pf->flags & I40E_FLAG_RSS_ENABLED	  &&
		   !(pf->flags & I40E_FLAG_FDIR_ENABLED)  &&
		   (pf->flags & I40E_FLAG_DCB_ENABLED)) {

		/* save num_tc_qps queues for TCs 1 thru 7 and the rest
		 * are set up for RSS in TC0
		 */
		queues_left -= accum_tc_size;

		pf->rss_size = i40e_set_rss_size(pf, queues_left);

		queues_left -= pf->rss_size;
		if (queues_left < 0) {
			dev_info(&pf->pdev->dev, "not enough queues for DCB\n");
			return;
		}

		pf->num_lan_qps = pf->rss_size + accum_tc_size;

	} else if (pf->flags & I40E_FLAG_RSS_ENABLED   &&
		  (pf->flags & I40E_FLAG_FDIR_ENABLED) &&
		  !(pf->flags & I40E_FLAG_DCB_ENABLED)) {

		queues_left -= 1; /* save 1 queue for FD */

		pf->rss_size = i40e_set_rss_size(pf, queues_left);

		queues_left -= pf->rss_size;
		if (queues_left < 0) {
			dev_info(&pf->pdev->dev, "not enough queues for Flow Director\n");
			return;
		}

		pf->num_lan_qps = pf->rss_size;

	} else if (pf->flags & I40E_FLAG_RSS_ENABLED   &&
		  (pf->flags & I40E_FLAG_FDIR_ENABLED) &&
		  (pf->flags & I40E_FLAG_DCB_ENABLED)) {

		/* save 1 queue for TCs 1 thru 7,
		 * 1 queue for flow director,
		 * and the rest are set up for RSS in TC0
		 */
		queues_left -= 1;
		queues_left -= accum_tc_size;

		pf->rss_size = i40e_set_rss_size(pf, queues_left);
		queues_left -= pf->rss_size;
		if (queues_left < 0) {
			dev_info(&pf->pdev->dev, "not enough queues for DCB and Flow Director\n");
			return;
		}

		pf->num_lan_qps = pf->rss_size + accum_tc_size;

	} else {
		dev_info(&pf->pdev->dev,
			 "Invalid configuration, flags=0x%08llx\n", pf->flags);
		return;
	}

	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    pf->num_vf_qps && pf->num_req_vfs && queues_left) {
		pf->num_req_vfs = min_t(int, pf->num_req_vfs, (queues_left /
							       pf->num_vf_qps));
		queues_left -= (pf->num_req_vfs * pf->num_vf_qps);
	}

	if ((pf->flags & I40E_FLAG_VMDQ_ENABLED) &&
	    pf->num_vmdq_vsis && pf->num_vmdq_qps && queues_left) {
		pf->num_vmdq_vsis = min_t(int, pf->num_vmdq_vsis,
					  (queues_left / pf->num_vmdq_qps));
		queues_left -= (pf->num_vmdq_vsis * pf->num_vmdq_qps);
	}

	return;
}

/**
 * i40e_setup_pf_filter_control - Setup PF static filter control
 * @pf: PF to be setup
 *
 * i40e_setup_pf_filter_control sets up a pf's initial filter control
 * settings. If PE/FCoE are enabled then it will also set the per PF
 * based filter sizes required for them. It also enables Flow director,
 * ethertype and macvlan type filter settings for the pf.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_setup_pf_filter_control(struct i40e_pf *pf)
{
	struct i40e_filter_control_settings *settings = &pf->filter_settings;

	settings->hash_lut_size = I40E_HASH_LUT_SIZE_128;

	/* Flow Director is enabled */
	if (pf->flags & (I40E_FLAG_FDIR_ENABLED | I40E_FLAG_FDIR_ATR_ENABLED))
		settings->enable_fdir = true;

	/* Ethtype and MACVLAN filters enabled for PF */
	settings->enable_ethtype = true;
	settings->enable_macvlan = true;

	if (i40e_set_filter_control(&pf->hw, settings))
		return -ENOENT;

	return 0;
}

/**
 * i40e_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in i40e_pci_tbl
 *
 * i40e_probe initializes a pf identified by a pci_dev structure.
 * The OS initialization, configuring of the pf private structure,
 * and a hardware reset occur.
 *
 * Returns 0 on success, negative on failure
 **/
static int i40e_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct i40e_driver_version dv;
	struct i40e_pf *pf;
	struct i40e_hw *hw;
	int err = 0;
	u32 len;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* set up for high or low dma */
	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		/* coherent mask for the same size will always succeed if
		 * dma_set_mask does
		 */
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(64));
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	} else {
		dev_err(&pdev->dev, "DMA configuration failed: %d\n", err);
		err = -EIO;
		goto err_dma;
	}

	/* set up pci connections */
	err = pci_request_selected_regions(pdev, pci_select_bars(pdev,
					   IORESOURCE_MEM), i40e_driver_name);
	if (err) {
		dev_info(&pdev->dev,
			 "pci_request_selected_regions failed %d\n", err);
		goto err_pci_reg;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	/* Now that we have a PCI connection, we need to do the
	 * low level device setup.  This is primarily setting up
	 * the Admin Queue structures and then querying for the
	 * device's current profile information.
	 */
	pf = kzalloc(sizeof(*pf), GFP_KERNEL);
	if (!pf) {
		err = -ENOMEM;
		goto err_pf_alloc;
	}
	pf->next_vsi = 0;
	pf->pdev = pdev;
	set_bit(__I40E_DOWN, &pf->state);

	hw = &pf->hw;
	hw->back = pf;
	hw->hw_addr = ioremap(pci_resource_start(pdev, 0),
			      pci_resource_len(pdev, 0));
	if (!hw->hw_addr) {
		err = -EIO;
		dev_info(&pdev->dev, "ioremap(0x%04x, 0x%04x) failed: 0x%x\n",
			 (unsigned int)pci_resource_start(pdev, 0),
			 (unsigned int)pci_resource_len(pdev, 0), err);
		goto err_ioremap;
	}
	hw->vendor_id = pdev->vendor;
	hw->device_id = pdev->device;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &hw->revision_id);
	hw->subsystem_vendor_id = pdev->subsystem_vendor;
	hw->subsystem_device_id = pdev->subsystem_device;
	hw->bus.device = PCI_SLOT(pdev->devfn);
	hw->bus.func = PCI_FUNC(pdev->devfn);

	/* Reset here to make sure all is clean and to define PF 'n' */
	err = i40e_pf_reset(hw);
	if (err) {
		dev_info(&pdev->dev, "Initial pf_reset failed: %d\n", err);
		goto err_pf_reset;
	}
	pf->pfr_count++;

	hw->aq.num_arq_entries = I40E_AQ_LEN;
	hw->aq.num_asq_entries = I40E_AQ_LEN;
	hw->aq.arq_buf_size = I40E_MAX_AQ_BUF_SIZE;
	hw->aq.asq_buf_size = I40E_MAX_AQ_BUF_SIZE;
	pf->adminq_work_limit = I40E_AQ_WORK_LIMIT;
	snprintf(pf->misc_int_name, sizeof(pf->misc_int_name) - 1,
		 "%s-pf%d:misc",
		 dev_driver_string(&pf->pdev->dev), pf->hw.pf_id);

	err = i40e_init_shared_code(hw);
	if (err) {
		dev_info(&pdev->dev, "init_shared_code failed: %d\n", err);
		goto err_pf_reset;
	}

	err = i40e_init_adminq(hw);
	dev_info(&pdev->dev, "%s\n", i40e_fw_version_str(hw));
	if (err) {
		dev_info(&pdev->dev,
			 "init_adminq failed: %d expecting API %02x.%02x\n",
			 err,
			 I40E_FW_API_VERSION_MAJOR, I40E_FW_API_VERSION_MINOR);
		goto err_pf_reset;
	}

	err = i40e_get_capabilities(pf);
	if (err)
		goto err_adminq_setup;

	err = i40e_sw_init(pf);
	if (err) {
		dev_info(&pdev->dev, "sw_init failed: %d\n", err);
		goto err_sw_init;
	}

	err = i40e_init_lan_hmc(hw, hw->func_caps.num_tx_qp,
				hw->func_caps.num_rx_qp,
				pf->fcoe_hmc_cntx_num, pf->fcoe_hmc_filt_num);
	if (err) {
		dev_info(&pdev->dev, "init_lan_hmc failed: %d\n", err);
		goto err_init_lan_hmc;
	}

	err = i40e_configure_lan_hmc(hw, I40E_HMC_MODEL_DIRECT_ONLY);
	if (err) {
		dev_info(&pdev->dev, "configure_lan_hmc failed: %d\n", err);
		err = -ENOENT;
		goto err_configure_lan_hmc;
	}

	i40e_get_mac_addr(hw, hw->mac.addr);
	if (i40e_validate_mac_addr(hw->mac.addr)) {
		dev_info(&pdev->dev, "invalid MAC address %pM\n", hw->mac.addr);
		err = -EIO;
		goto err_mac_addr;
	}
	dev_info(&pdev->dev, "MAC address: %pM\n", hw->mac.addr);
	memcpy(hw->mac.perm_addr, hw->mac.addr, ETH_ALEN);

	pci_set_drvdata(pdev, pf);
	pci_save_state(pdev);

	/* set up periodic task facility */
	setup_timer(&pf->service_timer, i40e_service_timer, (unsigned long)pf);
	pf->service_timer_period = HZ;

	INIT_WORK(&pf->service_task, i40e_service_task);
	clear_bit(__I40E_SERVICE_SCHED, &pf->state);
	pf->flags |= I40E_FLAG_NEED_LINK_UPDATE;
	pf->link_check_timeout = jiffies;

	/* set up the main switch operations */
	i40e_determine_queue_usage(pf);
	i40e_init_interrupt_scheme(pf);

	/* Set up the *vsi struct based on the number of VSIs in the HW,
	 * and set up our local tracking of the MAIN PF vsi.
	 */
	len = sizeof(struct i40e_vsi *) * pf->hw.func_caps.num_vsis;
	pf->vsi = kzalloc(len, GFP_KERNEL);
	if (!pf->vsi) {
		err = -ENOMEM;
		goto err_switch_setup;
	}

	err = i40e_setup_pf_switch(pf);
	if (err) {
		dev_info(&pdev->dev, "setup_pf_switch failed: %d\n", err);
		goto err_vsis;
	}

	/* The main driver is (mostly) up and happy. We need to set this state
	 * before setting up the misc vector or we get a race and the vector
	 * ends up disabled forever.
	 */
	clear_bit(__I40E_DOWN, &pf->state);

	/* In case of MSIX we are going to setup the misc vector right here
	 * to handle admin queue events etc. In case of legacy and MSI
	 * the misc functionality and queue processing is combined in
	 * the same vector and that gets setup at open.
	 */
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		err = i40e_setup_misc_vector(pf);
		if (err) {
			dev_info(&pdev->dev,
				 "setup of misc vector failed: %d\n", err);
			goto err_vsis;
		}
	}

	/* prep for VF support */
	if ((pf->flags & I40E_FLAG_SRIOV_ENABLED) &&
	    (pf->flags & I40E_FLAG_MSIX_ENABLED)) {
		u32 val;

		/* disable link interrupts for VFs */
		val = rd32(hw, I40E_PFGEN_PORTMDIO_NUM);
		val &= ~I40E_PFGEN_PORTMDIO_NUM_VFLINK_STAT_ENA_MASK;
		wr32(hw, I40E_PFGEN_PORTMDIO_NUM, val);
		i40e_flush(hw);
	}

	i40e_dbg_pf_init(pf);

	/* tell the firmware that we're starting */
	dv.major_version = DRV_VERSION_MAJOR;
	dv.minor_version = DRV_VERSION_MINOR;
	dv.build_version = DRV_VERSION_BUILD;
	dv.subbuild_version = 0;
	i40e_aq_send_driver_version(&pf->hw, &dv, NULL);

	/* since everything's happy, start the service_task timer */
	mod_timer(&pf->service_timer,
		  round_jiffies(jiffies + pf->service_timer_period));

	return 0;

	/* Unwind what we've done if something failed in the setup */
err_vsis:
	set_bit(__I40E_DOWN, &pf->state);
err_switch_setup:
	i40e_clear_interrupt_scheme(pf);
	kfree(pf->vsi);
	del_timer_sync(&pf->service_timer);
err_mac_addr:
err_configure_lan_hmc:
	(void)i40e_shutdown_lan_hmc(hw);
err_init_lan_hmc:
	kfree(pf->qp_pile);
	kfree(pf->irq_pile);
err_sw_init:
err_adminq_setup:
	(void)i40e_shutdown_adminq(hw);
err_pf_reset:
	iounmap(hw->hw_addr);
err_ioremap:
	kfree(pf);
err_pf_alloc:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_reg:
err_dma:
	pci_disable_device(pdev);
	return err;
}

/**
 * i40e_remove - Device removal routine
 * @pdev: PCI device information struct
 *
 * i40e_remove is called by the PCI subsystem to alert the driver
 * that is should release a PCI device.  This could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void i40e_remove(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	i40e_status ret_code;
	u32 reg;
	int i;

	i40e_dbg_pf_exit(pf);

	if (pf->flags & I40E_FLAG_SRIOV_ENABLED) {
		i40e_free_vfs(pf);
		pf->flags &= ~I40E_FLAG_SRIOV_ENABLED;
	}

	/* no more scheduling of any task */
	set_bit(__I40E_DOWN, &pf->state);
	del_timer_sync(&pf->service_timer);
	cancel_work_sync(&pf->service_task);

	i40e_fdir_teardown(pf);

	/* If there is a switch structure or any orphans, remove them.
	 * This will leave only the PF's VSI remaining.
	 */
	for (i = 0; i < I40E_MAX_VEB; i++) {
		if (!pf->veb[i])
			continue;

		if (pf->veb[i]->uplink_seid == pf->mac_seid ||
		    pf->veb[i]->uplink_seid == 0)
			i40e_switch_branch_release(pf->veb[i]);
	}

	/* Now we can shutdown the PF's VSI, just before we kill
	 * adminq and hmc.
	 */
	if (pf->vsi[pf->lan_vsi])
		i40e_vsi_release(pf->vsi[pf->lan_vsi]);

	i40e_stop_misc_vector(pf);
	if (pf->flags & I40E_FLAG_MSIX_ENABLED) {
		synchronize_irq(pf->msix_entries[0].vector);
		free_irq(pf->msix_entries[0].vector, pf);
	}

	/* shutdown and destroy the HMC */
	ret_code = i40e_shutdown_lan_hmc(&pf->hw);
	if (ret_code)
		dev_warn(&pdev->dev,
			 "Failed to destroy the HMC resources: %d\n", ret_code);

	/* shutdown the adminq */
	i40e_aq_queue_shutdown(&pf->hw, true);
	ret_code = i40e_shutdown_adminq(&pf->hw);
	if (ret_code)
		dev_warn(&pdev->dev,
			 "Failed to destroy the Admin Queue resources: %d\n",
			 ret_code);

	/* Clear all dynamic memory lists of rings, q_vectors, and VSIs */
	i40e_clear_interrupt_scheme(pf);
	for (i = 0; i < pf->hw.func_caps.num_vsis; i++) {
		if (pf->vsi[i]) {
			i40e_vsi_clear_rings(pf->vsi[i]);
			i40e_vsi_clear(pf->vsi[i]);
			pf->vsi[i] = NULL;
		}
	}

	for (i = 0; i < I40E_MAX_VEB; i++) {
		kfree(pf->veb[i]);
		pf->veb[i] = NULL;
	}

	kfree(pf->qp_pile);
	kfree(pf->irq_pile);
	kfree(pf->sw_config);
	kfree(pf->vsi);

	/* force a PF reset to clean anything leftover */
	reg = rd32(&pf->hw, I40E_PFGEN_CTRL);
	wr32(&pf->hw, I40E_PFGEN_CTRL, (reg | I40E_PFGEN_CTRL_PFSWR_MASK));
	i40e_flush(&pf->hw);

	iounmap(pf->hw.hw_addr);
	kfree(pf);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

/**
 * i40e_pci_error_detected - warning that something funky happened in PCI land
 * @pdev: PCI device information struct
 *
 * Called to warn that something happened and the error handling steps
 * are in progress.  Allows the driver to quiesce things, be ready for
 * remediation.
 **/
static pci_ers_result_t i40e_pci_error_detected(struct pci_dev *pdev,
						enum pci_channel_state error)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s: error %d\n", __func__, error);

	/* shutdown all operations */
	i40e_pf_quiesce_all_vsi(pf);

	/* Request a slot reset */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * i40e_pci_error_slot_reset - a PCI slot reset just happened
 * @pdev: PCI device information struct
 *
 * Called to find if the driver can work with the device now that
 * the pci slot has been reset.  If a basic connection seems good
 * (registers are readable and have sane content) then return a
 * happy little PCI_ERS_RESULT_xxx.
 **/
static pci_ers_result_t i40e_pci_error_slot_reset(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);
	pci_ers_result_t result;
	int err;
	u32 reg;

	dev_info(&pdev->dev, "%s\n", __func__);
	if (pci_enable_device_mem(pdev)) {
		dev_info(&pdev->dev,
			 "Cannot re-enable PCI device after reset.\n");
		result = PCI_ERS_RESULT_DISCONNECT;
	} else {
		pci_set_master(pdev);
		pci_restore_state(pdev);
		pci_save_state(pdev);
		pci_wake_from_d3(pdev, false);

		reg = rd32(&pf->hw, I40E_GLGEN_RTRIG);
		if (reg == 0)
			result = PCI_ERS_RESULT_RECOVERED;
		else
			result = PCI_ERS_RESULT_DISCONNECT;
	}

	err = pci_cleanup_aer_uncorrect_error_status(pdev);
	if (err) {
		dev_info(&pdev->dev,
			 "pci_cleanup_aer_uncorrect_error_status failed 0x%0x\n",
			 err);
		/* non-fatal, continue */
	}

	return result;
}

/**
 * i40e_pci_error_resume - restart operations after PCI error recovery
 * @pdev: PCI device information struct
 *
 * Called to allow the driver to bring things back up after PCI error
 * and/or reset recovery has finished.
 **/
static void i40e_pci_error_resume(struct pci_dev *pdev)
{
	struct i40e_pf *pf = pci_get_drvdata(pdev);

	dev_info(&pdev->dev, "%s\n", __func__);
	i40e_handle_reset_warning(pf);
}

static const struct pci_error_handlers i40e_err_handler = {
	.error_detected = i40e_pci_error_detected,
	.slot_reset = i40e_pci_error_slot_reset,
	.resume = i40e_pci_error_resume,
};

static struct pci_driver i40e_driver = {
	.name     = i40e_driver_name,
	.id_table = i40e_pci_tbl,
	.probe    = i40e_probe,
	.remove   = i40e_remove,
	.err_handler = &i40e_err_handler,
	.sriov_configure = i40e_pci_sriov_configure,
};

/**
 * i40e_init_module - Driver registration routine
 *
 * i40e_init_module is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 **/
static int __init i40e_init_module(void)
{
	pr_info("%s: %s - version %s\n", i40e_driver_name,
		i40e_driver_string, i40e_driver_version_str);
	pr_info("%s: %s\n", i40e_driver_name, i40e_copyright);
	i40e_dbg_init();
	return pci_register_driver(&i40e_driver);
}
module_init(i40e_init_module);

/**
 * i40e_exit_module - Driver exit cleanup routine
 *
 * i40e_exit_module is called just before the driver is removed
 * from memory.
 **/
static void __exit i40e_exit_module(void)
{
	pci_unregister_driver(&i40e_driver);
	i40e_dbg_exit();
}
module_exit(i40e_exit_module);
