/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum.c
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2015 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2015 Ido Schimmel <idosch@mellanox.com>
 * Copyright (c) 2015 Elad Raz <eladr@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/dcbnl.h>
#include <linux/inetdevice.h>
#include <net/switchdev.h>
#include <generated/utsrelease.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_mirred.h>
#include <net/netevent.h>

#include "spectrum.h"
#include "core.h"
#include "reg.h"
#include "port.h"
#include "trap.h"
#include "txheader.h"

static const char mlxsw_sp_driver_name[] = "mlxsw_spectrum";
static const char mlxsw_sp_driver_version[] = "1.0";

/* tx_hdr_version
 * Tx header version.
 * Must be set to 1.
 */
MLXSW_ITEM32(tx, hdr, version, 0x00, 28, 4);

/* tx_hdr_ctl
 * Packet control type.
 * 0 - Ethernet control (e.g. EMADs, LACP)
 * 1 - Ethernet data
 */
MLXSW_ITEM32(tx, hdr, ctl, 0x00, 26, 2);

/* tx_hdr_proto
 * Packet protocol type. Must be set to 1 (Ethernet).
 */
MLXSW_ITEM32(tx, hdr, proto, 0x00, 21, 3);

/* tx_hdr_rx_is_router
 * Packet is sent from the router. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, rx_is_router, 0x00, 19, 1);

/* tx_hdr_fid_valid
 * Indicates if the 'fid' field is valid and should be used for
 * forwarding lookup. Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid_valid, 0x00, 16, 1);

/* tx_hdr_swid
 * Switch partition ID. Must be set to 0.
 */
MLXSW_ITEM32(tx, hdr, swid, 0x00, 12, 3);

/* tx_hdr_control_tclass
 * Indicates if the packet should use the control TClass and not one
 * of the data TClasses.
 */
MLXSW_ITEM32(tx, hdr, control_tclass, 0x00, 6, 1);

/* tx_hdr_etclass
 * Egress TClass to be used on the egress device on the egress port.
 */
MLXSW_ITEM32(tx, hdr, etclass, 0x00, 0, 4);

/* tx_hdr_port_mid
 * Destination local port for unicast packets.
 * Destination multicast ID for multicast packets.
 *
 * Control packets are directed to a specific egress port, while data
 * packets are transmitted through the CPU port (0) into the switch partition,
 * where forwarding rules are applied.
 */
MLXSW_ITEM32(tx, hdr, port_mid, 0x04, 16, 16);

/* tx_hdr_fid
 * Forwarding ID used for L2 forwarding lookup. Valid only if 'fid_valid' is
 * set, otherwise calculated based on the packet's VID using VID to FID mapping.
 * Valid for data packets only.
 */
MLXSW_ITEM32(tx, hdr, fid, 0x08, 0, 16);

/* tx_hdr_type
 * 0 - Data packets
 * 6 - Control packets
 */
MLXSW_ITEM32(tx, hdr, type, 0x0C, 0, 4);

static bool mlxsw_sp_port_dev_check(const struct net_device *dev);

static void mlxsw_sp_txhdr_construct(struct sk_buff *skb,
				     const struct mlxsw_tx_info *tx_info)
{
	char *txhdr = skb_push(skb, MLXSW_TXHDR_LEN);

	memset(txhdr, 0, MLXSW_TXHDR_LEN);

	mlxsw_tx_hdr_version_set(txhdr, MLXSW_TXHDR_VERSION_1);
	mlxsw_tx_hdr_ctl_set(txhdr, MLXSW_TXHDR_ETH_CTL);
	mlxsw_tx_hdr_proto_set(txhdr, MLXSW_TXHDR_PROTO_ETH);
	mlxsw_tx_hdr_swid_set(txhdr, 0);
	mlxsw_tx_hdr_control_tclass_set(txhdr, 1);
	mlxsw_tx_hdr_port_mid_set(txhdr, tx_info->local_port);
	mlxsw_tx_hdr_type_set(txhdr, MLXSW_TXHDR_TYPE_CONTROL);
}

static int mlxsw_sp_base_mac_get(struct mlxsw_sp *mlxsw_sp)
{
	char spad_pl[MLXSW_REG_SPAD_LEN];
	int err;

	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(spad), spad_pl);
	if (err)
		return err;
	mlxsw_reg_spad_base_mac_memcpy_from(spad_pl, mlxsw_sp->base_mac);
	return 0;
}

static int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_resources *resources;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	if (!resources->max_span_valid)
		return -EIO;

	mlxsw_sp->span.entries_count = resources->max_span;
	mlxsw_sp->span.entries = kcalloc(mlxsw_sp->span.entries_count,
					 sizeof(struct mlxsw_sp_span_entry),
					 GFP_KERNEL);
	if (!mlxsw_sp->span.entries)
		return -ENOMEM;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++)
		INIT_LIST_HEAD(&mlxsw_sp->span.entries[i].bound_ports_list);

	return 0;
}

static void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		WARN_ON_ONCE(!list_empty(&curr->bound_ports_list));
	}
	kfree(mlxsw_sp->span.entries);
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_create(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	struct mlxsw_sp_span_entry *span_entry;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	u8 local_port = port->local_port;
	int index;
	int i;
	int err;

	/* find a free entry to use */
	index = -1;
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		if (!mlxsw_sp->span.entries[i].used) {
			index = i;
			span_entry = &mlxsw_sp->span.entries[i];
			break;
		}
	}
	if (index < 0)
		return NULL;

	/* create a new port analayzer entry for local_port */
	mlxsw_reg_mpat_pack(mpat_pl, index, local_port, true);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
	if (err)
		return NULL;

	span_entry->used = true;
	span_entry->id = index;
	span_entry->ref_count = 1;
	span_entry->local_port = local_port;
	return span_entry;
}

static void mlxsw_sp_span_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_span_entry *span_entry)
{
	u8 local_port = span_entry->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, false);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
	span_entry->used = false;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->used && curr->local_port == port->local_port)
			return curr;
	}
	return NULL;
}

static struct mlxsw_sp_span_entry
*mlxsw_sp_span_entry_get(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find(port);
	if (span_entry) {
		/* Already exists, just take a reference */
		span_entry->ref_count++;
		return span_entry;
	}

	return mlxsw_sp_span_entry_create(port);
}

static int mlxsw_sp_span_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_span_entry *span_entry)
{
	WARN_ON(!span_entry->ref_count);
	if (--span_entry->ref_count == 0)
		mlxsw_sp_span_entry_destroy(mlxsw_sp, span_entry);
	return 0;
}

static bool mlxsw_sp_span_is_egress_mirror(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	struct mlxsw_sp_span_inspected_port *p;
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		list_for_each_entry(p, &curr->bound_ports_list, list)
			if (p->local_port == port->local_port &&
			    p->type == MLXSW_SP_SPAN_EGRESS)
				return true;
	}

	return false;
}

static int mlxsw_sp_span_mtu_to_buffsize(int mtu)
{
	return MLXSW_SP_BYTES_TO_CELLS(mtu * 5 / 2) + 1;
}

static int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int err;

	/* If port is egress mirrored, the shared buffer size should be
	 * updated according to the mtu value
	 */
	if (mlxsw_sp_span_is_egress_mirror(port)) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port,
				    mlxsw_sp_span_mtu_to_buffsize(mtu));
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not update shared buffer for mirroring\n");
			return err;
		}
	}

	return 0;
}

static struct mlxsw_sp_span_inspected_port *
mlxsw_sp_span_entry_bound_port_find(struct mlxsw_sp_port *port,
				    struct mlxsw_sp_span_entry *span_entry)
{
	struct mlxsw_sp_span_inspected_port *p;

	list_for_each_entry(p, &span_entry->bound_ports_list, list)
		if (port->local_port == p->local_port)
			return p;
	return NULL;
}

static int
mlxsw_sp_span_inspected_port_bind(struct mlxsw_sp_port *port,
				  struct mlxsw_sp_span_entry *span_entry,
				  enum mlxsw_sp_span_type type)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char mpar_pl[MLXSW_REG_MPAR_LEN];
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int pa_id = span_entry->id;
	int err;

	/* if it is an egress SPAN, bind a shared buffer to it */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port,
				    mlxsw_sp_span_mtu_to_buffsize(port->dev->mtu));
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not create shared buffer for mirroring\n");
			return err;
		}
	}

	/* bind the port to the SPAN entry */
	mlxsw_reg_mpar_pack(mpar_pl, port->local_port,
			    (enum mlxsw_reg_mpar_i_e) type, true, pa_id);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpar), mpar_pl);
	if (err)
		goto err_mpar_reg_write;

	inspected_port = kzalloc(sizeof(*inspected_port), GFP_KERNEL);
	if (!inspected_port) {
		err = -ENOMEM;
		goto err_inspected_port_alloc;
	}
	inspected_port->local_port = port->local_port;
	inspected_port->type = type;
	list_add_tail(&inspected_port->list, &span_entry->bound_ports_list);

	return 0;

err_mpar_reg_write:
err_inspected_port_alloc:
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}
	return err;
}

static void
mlxsw_sp_span_inspected_port_unbind(struct mlxsw_sp_port *port,
				    struct mlxsw_sp_span_entry *span_entry,
				    enum mlxsw_sp_span_type type)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char mpar_pl[MLXSW_REG_MPAR_LEN];
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int pa_id = span_entry->id;

	inspected_port = mlxsw_sp_span_entry_bound_port_find(port, span_entry);
	if (!inspected_port)
		return;

	/* remove the inspected port */
	mlxsw_reg_mpar_pack(mpar_pl, port->local_port,
			    (enum mlxsw_reg_mpar_i_e) type, false, pa_id);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpar), mpar_pl);

	/* remove the SBIB buffer if it was egress SPAN */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}

	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);

	list_del(&inspected_port->list);
	kfree(inspected_port);
}

static int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
				    struct mlxsw_sp_port *to,
				    enum mlxsw_sp_span_type type)
{
	struct mlxsw_sp *mlxsw_sp = from->mlxsw_sp;
	struct mlxsw_sp_span_entry *span_entry;
	int err;

	span_entry = mlxsw_sp_span_entry_get(to);
	if (!span_entry)
		return -ENOENT;

	netdev_dbg(from->dev, "Adding inspected port to SPAN entry %d\n",
		   span_entry->id);

	err = mlxsw_sp_span_inspected_port_bind(from, span_entry, type);
	if (err)
		goto err_port_bind;

	return 0;

err_port_bind:
	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);
	return err;
}

static void mlxsw_sp_span_mirror_remove(struct mlxsw_sp_port *from,
					struct mlxsw_sp_port *to,
					enum mlxsw_sp_span_type type)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find(to);
	if (!span_entry) {
		netdev_err(from->dev, "no span entry found\n");
		return;
	}

	netdev_dbg(from->dev, "removing inspected port from SPAN entry %d\n",
		   span_entry->id);
	mlxsw_sp_span_inspected_port_unbind(from, span_entry, type);
}

static int mlxsw_sp_port_admin_status_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  bool is_up)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char paos_pl[MLXSW_REG_PAOS_LEN];

	mlxsw_reg_paos_pack(paos_pl, mlxsw_sp_port->local_port,
			    is_up ? MLXSW_PORT_ADMIN_STATUS_UP :
			    MLXSW_PORT_ADMIN_STATUS_DOWN);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(paos), paos_pl);
}

static int mlxsw_sp_port_dev_addr_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      unsigned char *addr)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ppad_pl[MLXSW_REG_PPAD_LEN];

	mlxsw_reg_ppad_pack(ppad_pl, true, mlxsw_sp_port->local_port);
	mlxsw_reg_ppad_mac_memcpy_to(ppad_pl, addr);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppad), ppad_pl);
}

static int mlxsw_sp_port_dev_addr_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	unsigned char *addr = mlxsw_sp_port->dev->dev_addr;

	ether_addr_copy(addr, mlxsw_sp->base_mac);
	addr[ETH_ALEN - 1] += mlxsw_sp_port->local_port;
	return mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr);
}

static int mlxsw_sp_port_mtu_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char pmtu_pl[MLXSW_REG_PMTU_LEN];
	int max_mtu;
	int err;

	mtu += MLXSW_TXHDR_LEN + ETH_HLEN;
	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
	if (err)
		return err;
	max_mtu = mlxsw_reg_pmtu_max_mtu_get(pmtu_pl);

	if (mtu > max_mtu)
		return -EINVAL;

	mlxsw_reg_pmtu_pack(pmtu_pl, mlxsw_sp_port->local_port, mtu);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmtu), pmtu_pl);
}

static int __mlxsw_sp_port_swid_set(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u8 swid)
{
	char pspa_pl[MLXSW_REG_PSPA_LEN];

	mlxsw_reg_pspa_pack(pspa_pl, swid, local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pspa), pspa_pl);
}

static int mlxsw_sp_port_swid_set(struct mlxsw_sp_port *mlxsw_sp_port, u8 swid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	return __mlxsw_sp_port_swid_set(mlxsw_sp, mlxsw_sp_port->local_port,
					swid);
}

static int mlxsw_sp_port_vp_mode_set(struct mlxsw_sp_port *mlxsw_sp_port,
				     bool enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char svpe_pl[MLXSW_REG_SVPE_LEN];

	mlxsw_reg_svpe_pack(svpe_pl, mlxsw_sp_port->local_port, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svpe), svpe_pl);
}

int mlxsw_sp_port_vid_to_fid_set(struct mlxsw_sp_port *mlxsw_sp_port,
				 enum mlxsw_reg_svfa_mt mt, bool valid, u16 fid,
				 u16 vid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char svfa_pl[MLXSW_REG_SVFA_LEN];

	mlxsw_reg_svfa_pack(svfa_pl, mlxsw_sp_port->local_port, mt, valid,
			    fid, vid);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(svfa), svfa_pl);
}

int __mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
				     u16 vid_begin, u16 vid_end,
				     bool learn_enable)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvmlr_pl;
	int err;

	spvmlr_pl = kmalloc(MLXSW_REG_SPVMLR_LEN, GFP_KERNEL);
	if (!spvmlr_pl)
		return -ENOMEM;
	mlxsw_reg_spvmlr_pack(spvmlr_pl, mlxsw_sp_port->local_port, vid_begin,
			      vid_end, learn_enable);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvmlr), spvmlr_pl);
	kfree(spvmlr_pl);
	return err;
}

static int mlxsw_sp_port_vid_learning_set(struct mlxsw_sp_port *mlxsw_sp_port,
					  u16 vid, bool learn_enable)
{
	return __mlxsw_sp_port_vid_learning_set(mlxsw_sp_port, vid, vid,
						learn_enable);
}

static int
mlxsw_sp_port_system_port_mapping_set(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sspr_pl[MLXSW_REG_SSPR_LEN];

	mlxsw_reg_sspr_pack(sspr_pl, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sspr), sspr_pl);
}

static int mlxsw_sp_port_module_info_get(struct mlxsw_sp *mlxsw_sp,
					 u8 local_port, u8 *p_module,
					 u8 *p_width, u8 *p_lane)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int err;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
	if (err)
		return err;
	*p_module = mlxsw_reg_pmlp_module_get(pmlp_pl, 0);
	*p_width = mlxsw_reg_pmlp_width_get(pmlp_pl);
	*p_lane = mlxsw_reg_pmlp_tx_lane_get(pmlp_pl, 0);
	return 0;
}

static int mlxsw_sp_port_module_map(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				    u8 module, u8 width, u8 lane)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];
	int i;

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, width);
	for (i = 0; i < width; i++) {
		mlxsw_reg_pmlp_module_set(pmlp_pl, i, module);
		mlxsw_reg_pmlp_tx_lane_set(pmlp_pl, i, lane + i);  /* Rx & Tx */
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_module_unmap(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	char pmlp_pl[MLXSW_REG_PMLP_LEN];

	mlxsw_reg_pmlp_pack(pmlp_pl, local_port);
	mlxsw_reg_pmlp_width_set(pmlp_pl, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pmlp), pmlp_pl);
}

static int mlxsw_sp_port_open(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	int err;

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);
	if (err)
		return err;
	netif_start_queue(dev);
	return 0;
}

static int mlxsw_sp_port_stop(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	netif_stop_queue(dev);
	return mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
}

static netdev_tx_t mlxsw_sp_port_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;
	const struct mlxsw_tx_info tx_info = {
		.local_port = mlxsw_sp_port->local_port,
		.is_emad = false,
	};
	u64 len;
	int err;

	if (mlxsw_core_skb_transmit_busy(mlxsw_sp->core, &tx_info))
		return NETDEV_TX_BUSY;

	if (unlikely(skb_headroom(skb) < MLXSW_TXHDR_LEN)) {
		struct sk_buff *skb_orig = skb;

		skb = skb_realloc_headroom(skb, MLXSW_TXHDR_LEN);
		if (!skb) {
			this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
			dev_kfree_skb_any(skb_orig);
			return NETDEV_TX_OK;
		}
	}

	if (eth_skb_pad(skb)) {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		return NETDEV_TX_OK;
	}

	mlxsw_sp_txhdr_construct(skb, &tx_info);
	/* TX header is consumed by HW on the way so we shouldn't count its
	 * bytes as being sent.
	 */
	len = skb->len - MLXSW_TXHDR_LEN;

	/* Due to a race we might fail here because of a full queue. In that
	 * unlikely case we simply drop the packet.
	 */
	err = mlxsw_core_skb_transmit(mlxsw_sp->core, skb, &tx_info);

	if (!err) {
		pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
		u64_stats_update_begin(&pcpu_stats->syncp);
		pcpu_stats->tx_packets++;
		pcpu_stats->tx_bytes += len;
		u64_stats_update_end(&pcpu_stats->syncp);
	} else {
		this_cpu_inc(mlxsw_sp_port->pcpu_stats->tx_dropped);
		dev_kfree_skb_any(skb);
	}
	return NETDEV_TX_OK;
}

static void mlxsw_sp_set_rx_mode(struct net_device *dev)
{
}

static int mlxsw_sp_port_set_mac_address(struct net_device *dev, void *p)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct sockaddr *addr = p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = mlxsw_sp_port_dev_addr_set(mlxsw_sp_port, addr->sa_data);
	if (err)
		return err;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static void mlxsw_sp_pg_buf_pack(char *pbmc_pl, int pg_index, int mtu,
				 bool pause_en, bool pfc_en, u16 delay)
{
	u16 pg_size = 2 * MLXSW_SP_BYTES_TO_CELLS(mtu);

	delay = pfc_en ? mlxsw_sp_pfc_delay_get(mtu, delay) :
			 MLXSW_SP_PAUSE_DELAY;

	if (pause_en || pfc_en)
		mlxsw_reg_pbmc_lossless_buffer_pack(pbmc_pl, pg_index,
						    pg_size + delay, pg_size);
	else
		mlxsw_reg_pbmc_lossy_buffer_pack(pbmc_pl, pg_index, pg_size);
}

int __mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port, int mtu,
				 u8 *prio_tc, bool pause_en,
				 struct ieee_pfc *my_pfc)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 pfc_en = !!my_pfc ? my_pfc->pfc_en : 0;
	u16 delay = !!my_pfc ? my_pfc->delay : 0;
	char pbmc_pl[MLXSW_REG_PBMC_LEN];
	int i, j, err;

	mlxsw_reg_pbmc_pack(pbmc_pl, mlxsw_sp_port->local_port, 0, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
	if (err)
		return err;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		bool configure = false;
		bool pfc = false;

		for (j = 0; j < IEEE_8021QAZ_MAX_TCS; j++) {
			if (prio_tc[j] == i) {
				pfc = pfc_en & BIT(j);
				configure = true;
				break;
			}
		}

		if (!configure)
			continue;
		mlxsw_sp_pg_buf_pack(pbmc_pl, i, mtu, pause_en, pfc, delay);
	}

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pbmc), pbmc_pl);
}

static int mlxsw_sp_port_headroom_set(struct mlxsw_sp_port *mlxsw_sp_port,
				      int mtu, bool pause_en)
{
	u8 def_prio_tc[IEEE_8021QAZ_MAX_TCS] = {0};
	bool dcb_en = !!mlxsw_sp_port->dcb.ets;
	struct ieee_pfc *my_pfc;
	u8 *prio_tc;

	prio_tc = dcb_en ? mlxsw_sp_port->dcb.ets->prio_tc : def_prio_tc;
	my_pfc = dcb_en ? mlxsw_sp_port->dcb.pfc : NULL;

	return __mlxsw_sp_port_headroom_set(mlxsw_sp_port, mtu, prio_tc,
					    pause_en, my_pfc);
}

static int mlxsw_sp_port_change_mtu(struct net_device *dev, int mtu)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	int err;

	err = mlxsw_sp_port_headroom_set(mlxsw_sp_port, mtu, pause_en);
	if (err)
		return err;
	err = mlxsw_sp_span_port_mtu_update(mlxsw_sp_port, mtu);
	if (err)
		goto err_span_port_mtu_update;
	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, mtu);
	if (err)
		goto err_port_mtu_set;
	dev->mtu = mtu;
	return 0;

err_port_mtu_set:
	mlxsw_sp_span_port_mtu_update(mlxsw_sp_port, dev->mtu);
err_span_port_mtu_update:
	mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	return err;
}

static int
mlxsw_sp_port_get_sw_stats64(const struct net_device *dev,
			     struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port_pcpu_stats *p;
	u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
	u32 tx_dropped = 0;
	unsigned int start;
	int i;

	for_each_possible_cpu(i) {
		p = per_cpu_ptr(mlxsw_sp_port->pcpu_stats, i);
		do {
			start = u64_stats_fetch_begin_irq(&p->syncp);
			rx_packets	= p->rx_packets;
			rx_bytes	= p->rx_bytes;
			tx_packets	= p->tx_packets;
			tx_bytes	= p->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&p->syncp, start));

		stats->rx_packets	+= rx_packets;
		stats->rx_bytes		+= rx_bytes;
		stats->tx_packets	+= tx_packets;
		stats->tx_bytes		+= tx_bytes;
		/* tx_dropped is u32, updated without syncp protection. */
		tx_dropped	+= p->tx_dropped;
	}
	stats->tx_dropped	= tx_dropped;
	return 0;
}

static bool mlxsw_sp_port_has_offload_stats(int attr_id)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return true;
	}

	return false;
}

static int mlxsw_sp_port_get_offload_stats(int attr_id, const struct net_device *dev,
					   void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return mlxsw_sp_port_get_sw_stats64(dev, sp);
	}

	return -EINVAL;
}

static int mlxsw_sp_port_get_stats_raw(struct net_device *dev, int grp,
				       int prio, char *ppcnt_pl)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	mlxsw_reg_ppcnt_pack(ppcnt_pl, mlxsw_sp_port->local_port, grp, prio);
	return mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ppcnt), ppcnt_pl);
}

static int mlxsw_sp_port_get_hw_stats(struct net_device *dev,
				      struct rtnl_link_stats64 *stats)
{
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int err;

	err = mlxsw_sp_port_get_stats_raw(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT,
					  0, ppcnt_pl);
	if (err)
		goto out;

	stats->tx_packets =
		mlxsw_reg_ppcnt_a_frames_transmitted_ok_get(ppcnt_pl);
	stats->rx_packets =
		mlxsw_reg_ppcnt_a_frames_received_ok_get(ppcnt_pl);
	stats->tx_bytes =
		mlxsw_reg_ppcnt_a_octets_transmitted_ok_get(ppcnt_pl);
	stats->rx_bytes =
		mlxsw_reg_ppcnt_a_octets_received_ok_get(ppcnt_pl);
	stats->multicast =
		mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get(ppcnt_pl);

	stats->rx_crc_errors =
		mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get(ppcnt_pl);
	stats->rx_frame_errors =
		mlxsw_reg_ppcnt_a_alignment_errors_get(ppcnt_pl);

	stats->rx_length_errors = (
		mlxsw_reg_ppcnt_a_in_range_length_errors_get(ppcnt_pl) +
		mlxsw_reg_ppcnt_a_out_of_range_length_field_get(ppcnt_pl) +
		mlxsw_reg_ppcnt_a_frame_too_long_errors_get(ppcnt_pl));

	stats->rx_errors = (stats->rx_crc_errors +
		stats->rx_frame_errors + stats->rx_length_errors);

out:
	return err;
}

static void update_stats_cache(struct work_struct *work)
{
	struct mlxsw_sp_port *mlxsw_sp_port =
		container_of(work, struct mlxsw_sp_port,
			     hw_stats.update_dw.work);

	if (!netif_carrier_ok(mlxsw_sp_port->dev))
		goto out;

	mlxsw_sp_port_get_hw_stats(mlxsw_sp_port->dev,
				   mlxsw_sp_port->hw_stats.cache);

out:
	mlxsw_core_schedule_dw(&mlxsw_sp_port->hw_stats.update_dw,
			       MLXSW_HW_STATS_UPDATE_TIME);
}

/* Return the stats from a cache that is updated periodically,
 * as this function might get called in an atomic context.
 */
static struct rtnl_link_stats64 *
mlxsw_sp_port_get_stats64(struct net_device *dev,
			  struct rtnl_link_stats64 *stats)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	memcpy(stats, mlxsw_sp_port->hw_stats.cache, sizeof(*stats));

	return stats;
}

int mlxsw_sp_port_vlan_set(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid_begin,
			   u16 vid_end, bool is_member, bool untagged)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char *spvm_pl;
	int err;

	spvm_pl = kmalloc(MLXSW_REG_SPVM_LEN, GFP_KERNEL);
	if (!spvm_pl)
		return -ENOMEM;

	mlxsw_reg_spvm_pack(spvm_pl, mlxsw_sp_port->local_port,	vid_begin,
			    vid_end, is_member, untagged);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(spvm), spvm_pl);
	kfree(spvm_pl);
	return err;
}

static int mlxsw_sp_port_vp_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	u16 vid, last_visited_vid;
	int err;

	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, VLAN_N_VID) {
		err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, true, vid,
						   vid);
		if (err) {
			last_visited_vid = vid;
			goto err_port_vid_to_fid_set;
		}
	}

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, true);
	if (err) {
		last_visited_vid = VLAN_N_VID;
		goto err_port_vid_to_fid_set;
	}

	return 0;

err_port_vid_to_fid_set:
	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, last_visited_vid)
		mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, false, vid,
					     vid);
	return err;
}

static int mlxsw_sp_port_vlan_mode_trans(struct mlxsw_sp_port *mlxsw_sp_port)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	u16 vid;
	int err;

	err = mlxsw_sp_port_vp_mode_set(mlxsw_sp_port, false);
	if (err)
		return err;

	for_each_set_bit(vid, mlxsw_sp_port->active_vlans, VLAN_N_VID) {
		err = mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_port, mt, false,
						   vid, vid);
		if (err)
			return err;
	}

	return 0;
}

static struct mlxsw_sp_port *
mlxsw_sp_port_vport_create(struct mlxsw_sp_port *mlxsw_sp_port, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	mlxsw_sp_vport = kzalloc(sizeof(*mlxsw_sp_vport), GFP_KERNEL);
	if (!mlxsw_sp_vport)
		return NULL;

	/* dev will be set correctly after the VLAN device is linked
	 * with the real device. In case of bridge SELF invocation, dev
	 * will remain as is.
	 */
	mlxsw_sp_vport->dev = mlxsw_sp_port->dev;
	mlxsw_sp_vport->mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	mlxsw_sp_vport->local_port = mlxsw_sp_port->local_port;
	mlxsw_sp_vport->stp_state = BR_STATE_FORWARDING;
	mlxsw_sp_vport->lagged = mlxsw_sp_port->lagged;
	mlxsw_sp_vport->lag_id = mlxsw_sp_port->lag_id;
	mlxsw_sp_vport->vport.vid = vid;

	list_add(&mlxsw_sp_vport->vport.list, &mlxsw_sp_port->vports_list);

	return mlxsw_sp_vport;
}

static void mlxsw_sp_port_vport_destroy(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	list_del(&mlxsw_sp_vport->vport.list);
	kfree(mlxsw_sp_vport);
}

static int mlxsw_sp_port_add_vid(struct net_device *dev,
				 __be16 __always_unused proto, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port *mlxsw_sp_vport;
	bool untagged = vid == 1;
	int err;

	/* VLAN 0 is added to HW filter when device goes up, but it is
	 * reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	if (mlxsw_sp_port_vport_find(mlxsw_sp_port, vid))
		return 0;

	mlxsw_sp_vport = mlxsw_sp_port_vport_create(mlxsw_sp_port, vid);
	if (!mlxsw_sp_vport)
		return -ENOMEM;

	/* When adding the first VLAN interface on a bridged port we need to
	 * transition all the active 802.1Q bridge VLANs to use explicit
	 * {Port, VID} to FID mappings and set the port's mode to Virtual mode.
	 */
	if (list_is_singular(&mlxsw_sp_port->vports_list)) {
		err = mlxsw_sp_port_vp_mode_trans(mlxsw_sp_port);
		if (err)
			goto err_port_vp_mode_trans;
	}

	err = mlxsw_sp_port_vlan_set(mlxsw_sp_vport, vid, vid, true, untagged);
	if (err)
		goto err_port_add_vid;

	return 0;

err_port_add_vid:
	if (list_is_singular(&mlxsw_sp_port->vports_list))
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);
err_port_vp_mode_trans:
	mlxsw_sp_port_vport_destroy(mlxsw_sp_vport);
	return err;
}

static int mlxsw_sp_port_kill_vid(struct net_device *dev,
				  __be16 __always_unused proto, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_fid *f;

	/* VLAN 0 is removed from HW filter when device goes down, but
	 * it is reserved in our case, so simply return.
	 */
	if (!vid)
		return 0;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_vport))
		return 0;

	mlxsw_sp_port_vlan_set(mlxsw_sp_vport, vid, vid, false, false);

	/* Drop FID reference. If this was the last reference the
	 * resources will be freed.
	 */
	f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);
	if (f && !WARN_ON(!f->leave))
		f->leave(mlxsw_sp_vport);

	/* When removing the last VLAN interface on a bridged port we need to
	 * transition all active 802.1Q bridge VLANs to use VID to FID
	 * mappings and set port's mode to VLAN mode.
	 */
	if (list_is_singular(&mlxsw_sp_port->vports_list))
		mlxsw_sp_port_vlan_mode_trans(mlxsw_sp_port);

	mlxsw_sp_port_vport_destroy(mlxsw_sp_vport);

	return 0;
}

static int mlxsw_sp_port_get_phys_port_name(struct net_device *dev, char *name,
					    size_t len)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	u8 module = mlxsw_sp_port->mapping.module;
	u8 width = mlxsw_sp_port->mapping.width;
	u8 lane = mlxsw_sp_port->mapping.lane;
	int err;

	if (!mlxsw_sp_port->split)
		err = snprintf(name, len, "p%d", module + 1);
	else
		err = snprintf(name, len, "p%ds%d", module + 1,
			       lane / width);

	if (err >= len)
		return -EINVAL;

	return 0;
}

static struct mlxsw_sp_port_mall_tc_entry *
mlxsw_sp_port_mirror_entry_find(struct mlxsw_sp_port *port,
				unsigned long cookie) {
	struct mlxsw_sp_port_mall_tc_entry *mall_tc_entry;

	list_for_each_entry(mall_tc_entry, &port->mall_tc_list, list)
		if (mall_tc_entry->cookie == cookie)
			return mall_tc_entry;

	return NULL;
}

static int
mlxsw_sp_port_add_cls_matchall_mirror(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct tc_cls_matchall_offload *cls,
				      const struct tc_action *a,
				      bool ingress)
{
	struct mlxsw_sp_port_mall_tc_entry *mall_tc_entry;
	struct net *net = dev_net(mlxsw_sp_port->dev);
	enum mlxsw_sp_span_type span_type;
	struct mlxsw_sp_port *to_port;
	struct net_device *to_dev;
	int ifindex;
	int err;

	ifindex = tcf_mirred_ifindex(a);
	to_dev = __dev_get_by_index(net, ifindex);
	if (!to_dev) {
		netdev_err(mlxsw_sp_port->dev, "Could not find requested device\n");
		return -EINVAL;
	}

	if (!mlxsw_sp_port_dev_check(to_dev)) {
		netdev_err(mlxsw_sp_port->dev, "Cannot mirror to a non-spectrum port");
		return -ENOTSUPP;
	}
	to_port = netdev_priv(to_dev);

	mall_tc_entry = kzalloc(sizeof(*mall_tc_entry), GFP_KERNEL);
	if (!mall_tc_entry)
		return -ENOMEM;

	mall_tc_entry->cookie = cls->cookie;
	mall_tc_entry->type = MLXSW_SP_PORT_MALL_MIRROR;
	mall_tc_entry->mirror.to_local_port = to_port->local_port;
	mall_tc_entry->mirror.ingress = ingress;
	list_add_tail(&mall_tc_entry->list, &mlxsw_sp_port->mall_tc_list);

	span_type = ingress ? MLXSW_SP_SPAN_INGRESS : MLXSW_SP_SPAN_EGRESS;
	err = mlxsw_sp_span_mirror_add(mlxsw_sp_port, to_port, span_type);
	if (err)
		goto err_mirror_add;
	return 0;

err_mirror_add:
	list_del(&mall_tc_entry->list);
	kfree(mall_tc_entry);
	return err;
}

static int mlxsw_sp_port_add_cls_matchall(struct mlxsw_sp_port *mlxsw_sp_port,
					  __be16 protocol,
					  struct tc_cls_matchall_offload *cls,
					  bool ingress)
{
	const struct tc_action *a;
	LIST_HEAD(actions);
	int err;

	if (!tc_single_action(cls->exts)) {
		netdev_err(mlxsw_sp_port->dev, "only singular actions are supported\n");
		return -ENOTSUPP;
	}

	tcf_exts_to_list(cls->exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (!is_tcf_mirred_mirror(a) || protocol != htons(ETH_P_ALL))
			return -ENOTSUPP;

		err = mlxsw_sp_port_add_cls_matchall_mirror(mlxsw_sp_port, cls,
							    a, ingress);
		if (err)
			return err;
	}

	return 0;
}

static void mlxsw_sp_port_del_cls_matchall(struct mlxsw_sp_port *mlxsw_sp_port,
					   struct tc_cls_matchall_offload *cls)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_port_mall_tc_entry *mall_tc_entry;
	enum mlxsw_sp_span_type span_type;
	struct mlxsw_sp_port *to_port;

	mall_tc_entry = mlxsw_sp_port_mirror_entry_find(mlxsw_sp_port,
							cls->cookie);
	if (!mall_tc_entry) {
		netdev_dbg(mlxsw_sp_port->dev, "tc entry not found on port\n");
		return;
	}

	switch (mall_tc_entry->type) {
	case MLXSW_SP_PORT_MALL_MIRROR:
		to_port = mlxsw_sp->ports[mall_tc_entry->mirror.to_local_port];
		span_type = mall_tc_entry->mirror.ingress ?
				MLXSW_SP_SPAN_INGRESS : MLXSW_SP_SPAN_EGRESS;

		mlxsw_sp_span_mirror_remove(mlxsw_sp_port, to_port, span_type);
		break;
	default:
		WARN_ON(1);
	}

	list_del(&mall_tc_entry->list);
	kfree(mall_tc_entry);
}

static int mlxsw_sp_setup_tc(struct net_device *dev, u32 handle,
			     __be16 proto, struct tc_to_netdev *tc)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool ingress = TC_H_MAJ(handle) == TC_H_MAJ(TC_H_INGRESS);

	if (tc->type == TC_SETUP_MATCHALL) {
		switch (tc->cls_mall->command) {
		case TC_CLSMATCHALL_REPLACE:
			return mlxsw_sp_port_add_cls_matchall(mlxsw_sp_port,
							      proto,
							      tc->cls_mall,
							      ingress);
		case TC_CLSMATCHALL_DESTROY:
			mlxsw_sp_port_del_cls_matchall(mlxsw_sp_port,
						       tc->cls_mall);
			return 0;
		default:
			return -EINVAL;
		}
	}

	return -ENOTSUPP;
}

static const struct net_device_ops mlxsw_sp_port_netdev_ops = {
	.ndo_open		= mlxsw_sp_port_open,
	.ndo_stop		= mlxsw_sp_port_stop,
	.ndo_start_xmit		= mlxsw_sp_port_xmit,
	.ndo_setup_tc           = mlxsw_sp_setup_tc,
	.ndo_set_rx_mode	= mlxsw_sp_set_rx_mode,
	.ndo_set_mac_address	= mlxsw_sp_port_set_mac_address,
	.ndo_change_mtu		= mlxsw_sp_port_change_mtu,
	.ndo_get_stats64	= mlxsw_sp_port_get_stats64,
	.ndo_has_offload_stats	= mlxsw_sp_port_has_offload_stats,
	.ndo_get_offload_stats	= mlxsw_sp_port_get_offload_stats,
	.ndo_vlan_rx_add_vid	= mlxsw_sp_port_add_vid,
	.ndo_vlan_rx_kill_vid	= mlxsw_sp_port_kill_vid,
	.ndo_neigh_construct	= mlxsw_sp_router_neigh_construct,
	.ndo_neigh_destroy	= mlxsw_sp_router_neigh_destroy,
	.ndo_fdb_add		= switchdev_port_fdb_add,
	.ndo_fdb_del		= switchdev_port_fdb_del,
	.ndo_fdb_dump		= switchdev_port_fdb_dump,
	.ndo_bridge_setlink	= switchdev_port_bridge_setlink,
	.ndo_bridge_getlink	= switchdev_port_bridge_getlink,
	.ndo_bridge_dellink	= switchdev_port_bridge_dellink,
	.ndo_get_phys_port_name	= mlxsw_sp_port_get_phys_port_name,
};

static void mlxsw_sp_port_get_drvinfo(struct net_device *dev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;

	strlcpy(drvinfo->driver, mlxsw_sp_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, mlxsw_sp_driver_version,
		sizeof(drvinfo->version));
	snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
		 "%d.%d.%d",
		 mlxsw_sp->bus_info->fw_rev.major,
		 mlxsw_sp->bus_info->fw_rev.minor,
		 mlxsw_sp->bus_info->fw_rev.subminor);
	strlcpy(drvinfo->bus_info, mlxsw_sp->bus_info->device_name,
		sizeof(drvinfo->bus_info));
}

static void mlxsw_sp_port_get_pauseparam(struct net_device *dev,
					 struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);

	pause->rx_pause = mlxsw_sp_port->link.rx_pause;
	pause->tx_pause = mlxsw_sp_port->link.tx_pause;
}

static int mlxsw_sp_port_pause_set(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct ethtool_pauseparam *pause)
{
	char pfcc_pl[MLXSW_REG_PFCC_LEN];

	mlxsw_reg_pfcc_pack(pfcc_pl, mlxsw_sp_port->local_port);
	mlxsw_reg_pfcc_pprx_set(pfcc_pl, pause->rx_pause);
	mlxsw_reg_pfcc_pptx_set(pfcc_pl, pause->tx_pause);

	return mlxsw_reg_write(mlxsw_sp_port->mlxsw_sp->core, MLXSW_REG(pfcc),
			       pfcc_pl);
}

static int mlxsw_sp_port_set_pauseparam(struct net_device *dev,
					struct ethtool_pauseparam *pause)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	bool pause_en = pause->tx_pause || pause->rx_pause;
	int err;

	if (mlxsw_sp_port->dcb.pfc && mlxsw_sp_port->dcb.pfc->pfc_en) {
		netdev_err(dev, "PFC already enabled on port\n");
		return -EINVAL;
	}

	if (pause->autoneg) {
		netdev_err(dev, "PAUSE frames autonegotiation isn't supported\n");
		return -EINVAL;
	}

	err = mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	if (err) {
		netdev_err(dev, "Failed to configure port's headroom\n");
		return err;
	}

	err = mlxsw_sp_port_pause_set(mlxsw_sp_port, pause);
	if (err) {
		netdev_err(dev, "Failed to set PAUSE parameters\n");
		goto err_port_pause_configure;
	}

	mlxsw_sp_port->link.rx_pause = pause->rx_pause;
	mlxsw_sp_port->link.tx_pause = pause->tx_pause;

	return 0;

err_port_pause_configure:
	pause_en = mlxsw_sp_port_is_pause_en(mlxsw_sp_port);
	mlxsw_sp_port_headroom_set(mlxsw_sp_port, dev->mtu, pause_en);
	return err;
}

struct mlxsw_sp_port_hw_stats {
	char str[ETH_GSTRING_LEN];
	u64 (*getter)(char *payload);
};

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_stats[] = {
	{
		.str = "a_frames_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_transmitted_ok_get,
	},
	{
		.str = "a_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_frames_received_ok_get,
	},
	{
		.str = "a_frame_check_sequence_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_check_sequence_errors_get,
	},
	{
		.str = "a_alignment_errors",
		.getter = mlxsw_reg_ppcnt_a_alignment_errors_get,
	},
	{
		.str = "a_octets_transmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_transmitted_ok_get,
	},
	{
		.str = "a_octets_received_ok",
		.getter = mlxsw_reg_ppcnt_a_octets_received_ok_get,
	},
	{
		.str = "a_multicast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_xmitted_ok_get,
	},
	{
		.str = "a_broadcast_frames_xmitted_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_xmitted_ok_get,
	},
	{
		.str = "a_multicast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_multicast_frames_received_ok_get,
	},
	{
		.str = "a_broadcast_frames_received_ok",
		.getter = mlxsw_reg_ppcnt_a_broadcast_frames_received_ok_get,
	},
	{
		.str = "a_in_range_length_errors",
		.getter = mlxsw_reg_ppcnt_a_in_range_length_errors_get,
	},
	{
		.str = "a_out_of_range_length_field",
		.getter = mlxsw_reg_ppcnt_a_out_of_range_length_field_get,
	},
	{
		.str = "a_frame_too_long_errors",
		.getter = mlxsw_reg_ppcnt_a_frame_too_long_errors_get,
	},
	{
		.str = "a_symbol_error_during_carrier",
		.getter = mlxsw_reg_ppcnt_a_symbol_error_during_carrier_get,
	},
	{
		.str = "a_mac_control_frames_transmitted",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_transmitted_get,
	},
	{
		.str = "a_mac_control_frames_received",
		.getter = mlxsw_reg_ppcnt_a_mac_control_frames_received_get,
	},
	{
		.str = "a_unsupported_opcodes_received",
		.getter = mlxsw_reg_ppcnt_a_unsupported_opcodes_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_received",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_received_get,
	},
	{
		.str = "a_pause_mac_ctrl_frames_xmitted",
		.getter = mlxsw_reg_ppcnt_a_pause_mac_ctrl_frames_transmitted_get,
	},
};

#define MLXSW_SP_PORT_HW_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_stats)

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_prio_stats[] = {
	{
		.str = "rx_octets_prio",
		.getter = mlxsw_reg_ppcnt_rx_octets_get,
	},
	{
		.str = "rx_frames_prio",
		.getter = mlxsw_reg_ppcnt_rx_frames_get,
	},
	{
		.str = "tx_octets_prio",
		.getter = mlxsw_reg_ppcnt_tx_octets_get,
	},
	{
		.str = "tx_frames_prio",
		.getter = mlxsw_reg_ppcnt_tx_frames_get,
	},
	{
		.str = "rx_pause_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_get,
	},
	{
		.str = "rx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_rx_pause_duration_get,
	},
	{
		.str = "tx_pause_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_get,
	},
	{
		.str = "tx_pause_duration_prio",
		.getter = mlxsw_reg_ppcnt_tx_pause_duration_get,
	},
};

#define MLXSW_SP_PORT_HW_PRIO_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_prio_stats)

static u64 mlxsw_reg_ppcnt_tc_transmit_queue_bytes_get(char *ppcnt_pl)
{
	u64 transmit_queue = mlxsw_reg_ppcnt_tc_transmit_queue_get(ppcnt_pl);

	return MLXSW_SP_CELLS_TO_BYTES(transmit_queue);
}

static struct mlxsw_sp_port_hw_stats mlxsw_sp_port_hw_tc_stats[] = {
	{
		.str = "tc_transmit_queue_tc",
		.getter = mlxsw_reg_ppcnt_tc_transmit_queue_bytes_get,
	},
	{
		.str = "tc_no_buffer_discard_uc_tc",
		.getter = mlxsw_reg_ppcnt_tc_no_buffer_discard_uc_get,
	},
};

#define MLXSW_SP_PORT_HW_TC_STATS_LEN ARRAY_SIZE(mlxsw_sp_port_hw_tc_stats)

#define MLXSW_SP_PORT_ETHTOOL_STATS_LEN (MLXSW_SP_PORT_HW_STATS_LEN + \
					 (MLXSW_SP_PORT_HW_PRIO_STATS_LEN + \
					  MLXSW_SP_PORT_HW_TC_STATS_LEN) * \
					 IEEE_8021QAZ_MAX_TCS)

static void mlxsw_sp_port_get_prio_strings(u8 **p, int prio)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_PRIO_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%s_%d",
			 mlxsw_sp_port_hw_prio_stats[i].str, prio);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_tc_strings(u8 **p, int tc)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_HW_TC_STATS_LEN; i++) {
		snprintf(*p, ETH_GSTRING_LEN, "%s_%d",
			 mlxsw_sp_port_hw_tc_stats[i].str, tc);
		*p += ETH_GSTRING_LEN;
	}
}

static void mlxsw_sp_port_get_strings(struct net_device *dev,
				      u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MLXSW_SP_PORT_HW_STATS_LEN; i++) {
			memcpy(p, mlxsw_sp_port_hw_stats[i].str,
			       ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			mlxsw_sp_port_get_prio_strings(&p, i);

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
			mlxsw_sp_port_get_tc_strings(&p, i);

		break;
	}
}

static int mlxsw_sp_port_set_phys_id(struct net_device *dev,
				     enum ethtool_phys_id_state state)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char mlcr_pl[MLXSW_REG_MLCR_LEN];
	bool active;

	switch (state) {
	case ETHTOOL_ID_ACTIVE:
		active = true;
		break;
	case ETHTOOL_ID_INACTIVE:
		active = false;
		break;
	default:
		return -EOPNOTSUPP;
	}

	mlxsw_reg_mlcr_pack(mlcr_pl, mlxsw_sp_port->local_port, active);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mlcr), mlcr_pl);
}

static int
mlxsw_sp_get_hw_stats_by_group(struct mlxsw_sp_port_hw_stats **p_hw_stats,
			       int *p_len, enum mlxsw_reg_ppcnt_grp grp)
{
	switch (grp) {
	case  MLXSW_REG_PPCNT_IEEE_8023_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_stats;
		*p_len = MLXSW_SP_PORT_HW_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_PRIO_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_prio_stats;
		*p_len = MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
		break;
	case MLXSW_REG_PPCNT_TC_CNT:
		*p_hw_stats = mlxsw_sp_port_hw_tc_stats;
		*p_len = MLXSW_SP_PORT_HW_TC_STATS_LEN;
		break;
	default:
		WARN_ON(1);
		return -ENOTSUPP;
	}
	return 0;
}

static void __mlxsw_sp_port_get_stats(struct net_device *dev,
				      enum mlxsw_reg_ppcnt_grp grp, int prio,
				      u64 *data, int data_index)
{
	struct mlxsw_sp_port_hw_stats *hw_stats;
	char ppcnt_pl[MLXSW_REG_PPCNT_LEN];
	int i, len;
	int err;

	err = mlxsw_sp_get_hw_stats_by_group(&hw_stats, &len, grp);
	if (err)
		return;
	mlxsw_sp_port_get_stats_raw(dev, grp, prio, ppcnt_pl);
	for (i = 0; i < len; i++)
		data[data_index + i] = hw_stats[i].getter(ppcnt_pl);
}

static void mlxsw_sp_port_get_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	int i, data_index = 0;

	/* IEEE 802.3 Counters */
	__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_IEEE_8023_CNT, 0,
				  data, data_index);
	data_index = MLXSW_SP_PORT_HW_STATS_LEN;

	/* Per-Priority Counters */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_PRIO_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_PRIO_STATS_LEN;
	}

	/* Per-TC Counters */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		__mlxsw_sp_port_get_stats(dev, MLXSW_REG_PPCNT_TC_CNT, i,
					  data, data_index);
		data_index += MLXSW_SP_PORT_HW_TC_STATS_LEN;
	}
}

static int mlxsw_sp_port_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MLXSW_SP_PORT_ETHTOOL_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

struct mlxsw_sp_port_link_mode {
	enum ethtool_link_mode_bit_indices mask_ethtool;
	u32 mask;
	u32 speed;
};

static const struct mlxsw_sp_port_link_mode mlxsw_sp_port_link_mode[] = {
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100BASE_T,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100baseT_Full_BIT,
		.speed		= SPEED_100,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_SGMII |
				  MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX,
		.mask_ethtool	= ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
		.speed		= SPEED_1000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_T,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CX4 |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
				  MLXSW_REG_PTYS_ETH_SPEED_10GBASE_ER_LR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
		.speed		= SPEED_10000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_20GBASE_KR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_20000baseKR2_Full_BIT,
		.speed		= SPEED_20000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_40GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT,
		.speed		= SPEED_40000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_CR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseCR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_KR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseKR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_25GBASE_SR,
		.mask_ethtool	= ETHTOOL_LINK_MODE_25000baseSR_Full_BIT,
		.speed		= SPEED_25000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_CR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_KR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_50GBASE_SR2,
		.mask_ethtool	= ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT,
		.speed		= SPEED_50000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT,
		.speed		= SPEED_56000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_56000baseCR4_Full_BIT,
		.speed		= SPEED_56000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_56000baseSR4_Full_BIT,
		.speed		= SPEED_56000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_56GBASE_R4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_56000baseLR4_Full_BIT,
		.speed		= SPEED_56000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT,
		.speed		= SPEED_100000,
	},
	{
		.mask		= MLXSW_REG_PTYS_ETH_SPEED_100GBASE_LR4_ER4,
		.mask_ethtool	= ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
		.speed		= SPEED_100000,
	},
};

#define MLXSW_SP_PORT_LINK_MODE_LEN ARRAY_SIZE(mlxsw_sp_port_link_mode)

static void
mlxsw_sp_from_ptys_supported_port(u32 ptys_eth_proto,
				  struct ethtool_link_ksettings *cmd)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_1000BASE_KX))
		ethtool_link_ksettings_add_link_mode(cmd, supported, Backplane);
}

static void mlxsw_sp_from_ptys_link(u32 ptys_eth_proto, unsigned long *mode)
{
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp_port_link_mode[i].mask)
			__set_bit(mlxsw_sp_port_link_mode[i].mask_ethtool,
				  mode);
	}
}

static void mlxsw_sp_from_ptys_speed_duplex(bool carrier_ok, u32 ptys_eth_proto,
					    struct ethtool_link_ksettings *cmd)
{
	u32 speed = SPEED_UNKNOWN;
	u8 duplex = DUPLEX_UNKNOWN;
	int i;

	if (!carrier_ok)
		goto out;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (ptys_eth_proto & mlxsw_sp_port_link_mode[i].mask) {
			speed = mlxsw_sp_port_link_mode[i].speed;
			duplex = DUPLEX_FULL;
			break;
		}
	}
out:
	cmd->base.speed = speed;
	cmd->base.duplex = duplex;
}

static u8 mlxsw_sp_port_connector_port(u32 ptys_eth_proto)
{
	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_SR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_SR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_SGMII))
		return PORT_FIBRE;

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_CR |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_CR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_CR4))
		return PORT_DA;

	if (ptys_eth_proto & (MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KR |
			      MLXSW_REG_PTYS_ETH_SPEED_10GBASE_KX4 |
			      MLXSW_REG_PTYS_ETH_SPEED_40GBASE_KR4 |
			      MLXSW_REG_PTYS_ETH_SPEED_100GBASE_KR4))
		return PORT_NONE;

	return PORT_OTHER;
}

static u32
mlxsw_sp_to_ptys_advert_link(const struct ethtool_link_ksettings *cmd)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (test_bit(mlxsw_sp_port_link_mode[i].mask_ethtool,
			     cmd->link_modes.advertising))
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp_to_ptys_speed(u32 speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (speed == mlxsw_sp_port_link_mode[i].speed)
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static u32 mlxsw_sp_to_ptys_upper_speed(u32 upper_speed)
{
	u32 ptys_proto = 0;
	int i;

	for (i = 0; i < MLXSW_SP_PORT_LINK_MODE_LEN; i++) {
		if (mlxsw_sp_port_link_mode[i].speed <= upper_speed)
			ptys_proto |= mlxsw_sp_port_link_mode[i].mask;
	}
	return ptys_proto;
}

static void mlxsw_sp_port_get_link_supported(u32 eth_proto_cap,
					     struct ethtool_link_ksettings *cmd)
{
	ethtool_link_ksettings_add_link_mode(cmd, supported, Asym_Pause);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
	ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);

	mlxsw_sp_from_ptys_supported_port(eth_proto_cap, cmd);
	mlxsw_sp_from_ptys_link(eth_proto_cap, cmd->link_modes.supported);
}

static void mlxsw_sp_port_get_link_advertise(u32 eth_proto_admin, bool autoneg,
					     struct ethtool_link_ksettings *cmd)
{
	if (!autoneg)
		return;

	ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
	mlxsw_sp_from_ptys_link(eth_proto_admin, cmd->link_modes.advertising);
}

static void
mlxsw_sp_port_get_link_lp_advertise(u32 eth_proto_lp, u8 autoneg_status,
				    struct ethtool_link_ksettings *cmd)
{
	if (autoneg_status != MLXSW_REG_PTYS_AN_STATUS_OK || !eth_proto_lp)
		return;

	ethtool_link_ksettings_add_link_mode(cmd, lp_advertising, Autoneg);
	mlxsw_sp_from_ptys_link(eth_proto_lp, cmd->link_modes.lp_advertising);
}

static int mlxsw_sp_port_get_link_ksettings(struct net_device *dev,
					    struct ethtool_link_ksettings *cmd)
{
	u32 eth_proto_cap, eth_proto_admin, eth_proto_oper, eth_proto_lp;
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u8 autoneg_status;
	bool autoneg;
	int err;

	autoneg = mlxsw_sp_port->link.autoneg;
	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	mlxsw_reg_ptys_unpack(ptys_pl, &eth_proto_cap, &eth_proto_admin,
			      &eth_proto_oper);

	mlxsw_sp_port_get_link_supported(eth_proto_cap, cmd);

	mlxsw_sp_port_get_link_advertise(eth_proto_admin, autoneg, cmd);

	eth_proto_lp = mlxsw_reg_ptys_eth_proto_lp_advertise_get(ptys_pl);
	autoneg_status = mlxsw_reg_ptys_an_status_get(ptys_pl);
	mlxsw_sp_port_get_link_lp_advertise(eth_proto_lp, autoneg_status, cmd);

	cmd->base.autoneg = autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	cmd->base.port = mlxsw_sp_port_connector_port(eth_proto_oper);
	mlxsw_sp_from_ptys_speed_duplex(netif_carrier_ok(dev), eth_proto_oper,
					cmd);

	return 0;
}

static int
mlxsw_sp_port_set_link_ksettings(struct net_device *dev,
				 const struct ethtool_link_ksettings *cmd)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_cap, eth_proto_new;
	bool autoneg;
	int err;

	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, 0);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;
	mlxsw_reg_ptys_unpack(ptys_pl, &eth_proto_cap, NULL, NULL);

	autoneg = cmd->base.autoneg == AUTONEG_ENABLE;
	eth_proto_new = autoneg ?
		mlxsw_sp_to_ptys_advert_link(cmd) :
		mlxsw_sp_to_ptys_speed(cmd->base.speed);

	eth_proto_new = eth_proto_new & eth_proto_cap;
	if (!eth_proto_new) {
		netdev_err(dev, "No supported speed requested\n");
		return -EINVAL;
	}

	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port, eth_proto_new);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
	if (err)
		return err;

	if (!netif_running(dev))
		return 0;

	mlxsw_sp_port->link.autoneg = autoneg;

	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	mlxsw_sp_port_admin_status_set(mlxsw_sp_port, true);

	return 0;
}

static const struct ethtool_ops mlxsw_sp_port_ethtool_ops = {
	.get_drvinfo		= mlxsw_sp_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_pauseparam		= mlxsw_sp_port_get_pauseparam,
	.set_pauseparam		= mlxsw_sp_port_set_pauseparam,
	.get_strings		= mlxsw_sp_port_get_strings,
	.set_phys_id		= mlxsw_sp_port_set_phys_id,
	.get_ethtool_stats	= mlxsw_sp_port_get_stats,
	.get_sset_count		= mlxsw_sp_port_get_sset_count,
	.get_link_ksettings	= mlxsw_sp_port_get_link_ksettings,
	.set_link_ksettings	= mlxsw_sp_port_set_link_ksettings,
};

static int
mlxsw_sp_port_speed_by_width_set(struct mlxsw_sp_port *mlxsw_sp_port, u8 width)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u32 upper_speed = MLXSW_SP_PORT_BASE_SPEED * width;
	char ptys_pl[MLXSW_REG_PTYS_LEN];
	u32 eth_proto_admin;

	eth_proto_admin = mlxsw_sp_to_ptys_upper_speed(upper_speed);
	mlxsw_reg_ptys_pack(ptys_pl, mlxsw_sp_port->local_port,
			    eth_proto_admin);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ptys), ptys_pl);
}

int mlxsw_sp_port_ets_set(struct mlxsw_sp_port *mlxsw_sp_port,
			  enum mlxsw_reg_qeec_hr hr, u8 index, u8 next_index,
			  bool dwrr, u8 dwrr_weight)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_pack(qeec_pl, mlxsw_sp_port->local_port, hr, index,
			    next_index);
	mlxsw_reg_qeec_de_set(qeec_pl, true);
	mlxsw_reg_qeec_dwrr_set(qeec_pl, dwrr);
	mlxsw_reg_qeec_dwrr_weight_set(qeec_pl, dwrr_weight);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

int mlxsw_sp_port_ets_maxrate_set(struct mlxsw_sp_port *mlxsw_sp_port,
				  enum mlxsw_reg_qeec_hr hr, u8 index,
				  u8 next_index, u32 maxrate)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qeec_pl[MLXSW_REG_QEEC_LEN];

	mlxsw_reg_qeec_pack(qeec_pl, mlxsw_sp_port->local_port, hr, index,
			    next_index);
	mlxsw_reg_qeec_mase_set(qeec_pl, true);
	mlxsw_reg_qeec_max_shaper_rate_set(qeec_pl, maxrate);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qeec), qeec_pl);
}

int mlxsw_sp_port_prio_tc_set(struct mlxsw_sp_port *mlxsw_sp_port,
			      u8 switch_prio, u8 tclass)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char qtct_pl[MLXSW_REG_QTCT_LEN];

	mlxsw_reg_qtct_pack(qtct_pl, mlxsw_sp_port->local_port, switch_prio,
			    tclass);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(qtct), qtct_pl);
}

static int mlxsw_sp_port_ets_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	int err, i;

	/* Setup the elements hierarcy, so that each TC is linked to
	 * one subgroup, which are all member in the same group.
	 */
	err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
				    MLXSW_REG_QEEC_HIERARCY_GROUP, 0, 0, false,
				    0);
	if (err)
		return err;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HIERARCY_SUBGROUP, i,
					    0, false, 0);
		if (err)
			return err;
	}
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HIERARCY_TC, i, i,
					    false, 0);
		if (err)
			return err;
	}

	/* Make sure the max shaper is disabled in all hierarcies that
	 * support it.
	 */
	err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HIERARCY_PORT, 0, 0,
					    MLXSW_REG_QEEC_MAS_DIS);
	if (err)
		return err;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HIERARCY_SUBGROUP,
						    i, 0,
						    MLXSW_REG_QEEC_MAS_DIS);
		if (err)
			return err;
	}
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
						    MLXSW_REG_QEEC_HIERARCY_TC,
						    i, i,
						    MLXSW_REG_QEEC_MAS_DIS);
		if (err)
			return err;
	}

	/* Map all priorities to traffic class 0. */
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		err = mlxsw_sp_port_prio_tc_set(mlxsw_sp_port, i, 0);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_port_pvid_vport_create(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_port->pvid = 1;

	return mlxsw_sp_port_add_vid(mlxsw_sp_port->dev, 0, 1);
}

static int mlxsw_sp_port_pvid_vport_destroy(struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_port_kill_vid(mlxsw_sp_port->dev, 0, 1);
}

static int mlxsw_sp_port_create(struct mlxsw_sp *mlxsw_sp, u8 local_port,
				bool split, u8 module, u8 width, u8 lane)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct net_device *dev;
	size_t bytes;
	int err;

	dev = alloc_etherdev(sizeof(struct mlxsw_sp_port));
	if (!dev)
		return -ENOMEM;
	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp_port->dev = dev;
	mlxsw_sp_port->mlxsw_sp = mlxsw_sp;
	mlxsw_sp_port->local_port = local_port;
	mlxsw_sp_port->split = split;
	mlxsw_sp_port->mapping.module = module;
	mlxsw_sp_port->mapping.width = width;
	mlxsw_sp_port->mapping.lane = lane;
	mlxsw_sp_port->link.autoneg = 1;
	bytes = DIV_ROUND_UP(VLAN_N_VID, BITS_PER_BYTE);
	mlxsw_sp_port->active_vlans = kzalloc(bytes, GFP_KERNEL);
	if (!mlxsw_sp_port->active_vlans) {
		err = -ENOMEM;
		goto err_port_active_vlans_alloc;
	}
	mlxsw_sp_port->untagged_vlans = kzalloc(bytes, GFP_KERNEL);
	if (!mlxsw_sp_port->untagged_vlans) {
		err = -ENOMEM;
		goto err_port_untagged_vlans_alloc;
	}
	INIT_LIST_HEAD(&mlxsw_sp_port->vports_list);
	INIT_LIST_HEAD(&mlxsw_sp_port->mall_tc_list);

	mlxsw_sp_port->pcpu_stats =
		netdev_alloc_pcpu_stats(struct mlxsw_sp_port_pcpu_stats);
	if (!mlxsw_sp_port->pcpu_stats) {
		err = -ENOMEM;
		goto err_alloc_stats;
	}

	mlxsw_sp_port->hw_stats.cache =
		kzalloc(sizeof(*mlxsw_sp_port->hw_stats.cache), GFP_KERNEL);

	if (!mlxsw_sp_port->hw_stats.cache) {
		err = -ENOMEM;
		goto err_alloc_hw_stats;
	}
	INIT_DELAYED_WORK(&mlxsw_sp_port->hw_stats.update_dw,
			  &update_stats_cache);

	dev->netdev_ops = &mlxsw_sp_port_netdev_ops;
	dev->ethtool_ops = &mlxsw_sp_port_ethtool_ops;

	err = mlxsw_sp_port_swid_set(mlxsw_sp_port, 0);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set SWID\n",
			mlxsw_sp_port->local_port);
		goto err_port_swid_set;
	}

	err = mlxsw_sp_port_dev_addr_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Unable to init port mac address\n",
			mlxsw_sp_port->local_port);
		goto err_dev_addr_init;
	}

	netif_carrier_off(dev);

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_LLTX | NETIF_F_SG |
			 NETIF_F_HW_VLAN_CTAG_FILTER | NETIF_F_HW_TC;
	dev->hw_features |= NETIF_F_HW_TC;

	/* Each packet needs to have a Tx header (metadata) on top all other
	 * headers.
	 */
	dev->needed_headroom = MLXSW_TXHDR_LEN;

	err = mlxsw_sp_port_system_port_mapping_set(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set system port mapping\n",
			mlxsw_sp_port->local_port);
		goto err_port_system_port_mapping_set;
	}

	err = mlxsw_sp_port_speed_by_width_set(mlxsw_sp_port, width);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to enable speeds\n",
			mlxsw_sp_port->local_port);
		goto err_port_speed_by_width_set;
	}

	err = mlxsw_sp_port_mtu_set(mlxsw_sp_port, ETH_DATA_LEN);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to set MTU\n",
			mlxsw_sp_port->local_port);
		goto err_port_mtu_set;
	}

	err = mlxsw_sp_port_admin_status_set(mlxsw_sp_port, false);
	if (err)
		goto err_port_admin_status_set;

	err = mlxsw_sp_port_buffers_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize buffers\n",
			mlxsw_sp_port->local_port);
		goto err_port_buffers_init;
	}

	err = mlxsw_sp_port_ets_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize ETS\n",
			mlxsw_sp_port->local_port);
		goto err_port_ets_init;
	}

	/* ETS and buffers must be initialized before DCB. */
	err = mlxsw_sp_port_dcb_init(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to initialize DCB\n",
			mlxsw_sp_port->local_port);
		goto err_port_dcb_init;
	}

	err = mlxsw_sp_port_pvid_vport_create(mlxsw_sp_port);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to create PVID vPort\n",
			mlxsw_sp_port->local_port);
		goto err_port_pvid_vport_create;
	}

	mlxsw_sp_port_switchdev_init(mlxsw_sp_port);
	mlxsw_sp->ports[local_port] = mlxsw_sp_port;
	err = register_netdev(dev);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to register netdev\n",
			mlxsw_sp_port->local_port);
		goto err_register_netdev;
	}

	err = mlxsw_core_port_init(mlxsw_sp->core, &mlxsw_sp_port->core_port,
				   mlxsw_sp_port->local_port, dev,
				   mlxsw_sp_port->split, module);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Port %d: Failed to init core port\n",
			mlxsw_sp_port->local_port);
		goto err_core_port_init;
	}

	mlxsw_core_schedule_dw(&mlxsw_sp_port->hw_stats.update_dw, 0);
	return 0;

err_core_port_init:
	unregister_netdev(dev);
err_register_netdev:
	mlxsw_sp->ports[local_port] = NULL;
	mlxsw_sp_port_switchdev_fini(mlxsw_sp_port);
	mlxsw_sp_port_pvid_vport_destroy(mlxsw_sp_port);
err_port_pvid_vport_create:
	mlxsw_sp_port_dcb_fini(mlxsw_sp_port);
err_port_dcb_init:
err_port_ets_init:
err_port_buffers_init:
err_port_admin_status_set:
err_port_mtu_set:
err_port_speed_by_width_set:
err_port_system_port_mapping_set:
err_dev_addr_init:
	mlxsw_sp_port_swid_set(mlxsw_sp_port, MLXSW_PORT_SWID_DISABLED_PORT);
err_port_swid_set:
	kfree(mlxsw_sp_port->hw_stats.cache);
err_alloc_hw_stats:
	free_percpu(mlxsw_sp_port->pcpu_stats);
err_alloc_stats:
	kfree(mlxsw_sp_port->untagged_vlans);
err_port_untagged_vlans_alloc:
	kfree(mlxsw_sp_port->active_vlans);
err_port_active_vlans_alloc:
	free_netdev(dev);
	return err;
}

static void mlxsw_sp_port_remove(struct mlxsw_sp *mlxsw_sp, u8 local_port)
{
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];

	if (!mlxsw_sp_port)
		return;
	cancel_delayed_work_sync(&mlxsw_sp_port->hw_stats.update_dw);
	mlxsw_core_port_fini(&mlxsw_sp_port->core_port);
	unregister_netdev(mlxsw_sp_port->dev); /* This calls ndo_stop */
	mlxsw_sp->ports[local_port] = NULL;
	mlxsw_sp_port_switchdev_fini(mlxsw_sp_port);
	mlxsw_sp_port_pvid_vport_destroy(mlxsw_sp_port);
	mlxsw_sp_port_dcb_fini(mlxsw_sp_port);
	mlxsw_sp_port_swid_set(mlxsw_sp_port, MLXSW_PORT_SWID_DISABLED_PORT);
	mlxsw_sp_port_module_unmap(mlxsw_sp, mlxsw_sp_port->local_port);
	free_percpu(mlxsw_sp_port->pcpu_stats);
	kfree(mlxsw_sp_port->hw_stats.cache);
	kfree(mlxsw_sp_port->untagged_vlans);
	kfree(mlxsw_sp_port->active_vlans);
	WARN_ON_ONCE(!list_empty(&mlxsw_sp_port->vports_list));
	free_netdev(mlxsw_sp_port->dev);
}

static void mlxsw_sp_ports_remove(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 1; i < MLXSW_PORT_MAX_PORTS; i++)
		mlxsw_sp_port_remove(mlxsw_sp, i);
	kfree(mlxsw_sp->ports);
}

static int mlxsw_sp_ports_create(struct mlxsw_sp *mlxsw_sp)
{
	u8 module, width, lane;
	size_t alloc_size;
	int i;
	int err;

	alloc_size = sizeof(struct mlxsw_sp_port *) * MLXSW_PORT_MAX_PORTS;
	mlxsw_sp->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!mlxsw_sp->ports)
		return -ENOMEM;

	for (i = 1; i < MLXSW_PORT_MAX_PORTS; i++) {
		err = mlxsw_sp_port_module_info_get(mlxsw_sp, i, &module,
						    &width, &lane);
		if (err)
			goto err_port_module_info_get;
		if (!width)
			continue;
		mlxsw_sp->port_to_module[i] = module;
		err = mlxsw_sp_port_create(mlxsw_sp, i, false, module, width,
					   lane);
		if (err)
			goto err_port_create;
	}
	return 0;

err_port_create:
err_port_module_info_get:
	for (i--; i >= 1; i--)
		mlxsw_sp_port_remove(mlxsw_sp, i);
	kfree(mlxsw_sp->ports);
	return err;
}

static u8 mlxsw_sp_cluster_base_port_get(u8 local_port)
{
	u8 offset = (local_port - 1) % MLXSW_SP_PORTS_PER_CLUSTER_MAX;

	return local_port - offset;
}

static int mlxsw_sp_port_split_create(struct mlxsw_sp *mlxsw_sp, u8 base_port,
				      u8 module, unsigned int count)
{
	u8 width = MLXSW_PORT_MODULE_MAX_WIDTH / count;
	int err, i;

	for (i = 0; i < count; i++) {
		err = mlxsw_sp_port_module_map(mlxsw_sp, base_port + i, module,
					       width, i * width);
		if (err)
			goto err_port_module_map;
	}

	for (i = 0; i < count; i++) {
		err = __mlxsw_sp_port_swid_set(mlxsw_sp, base_port + i, 0);
		if (err)
			goto err_port_swid_set;
	}

	for (i = 0; i < count; i++) {
		err = mlxsw_sp_port_create(mlxsw_sp, base_port + i, true,
					   module, width, i * width);
		if (err)
			goto err_port_create;
	}

	return 0;

err_port_create:
	for (i--; i >= 0; i--)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);
	i = count;
err_port_swid_set:
	for (i--; i >= 0; i--)
		__mlxsw_sp_port_swid_set(mlxsw_sp, base_port + i,
					 MLXSW_PORT_SWID_DISABLED_PORT);
	i = count;
err_port_module_map:
	for (i--; i >= 0; i--)
		mlxsw_sp_port_module_unmap(mlxsw_sp, base_port + i);
	return err;
}

static void mlxsw_sp_port_unsplit_create(struct mlxsw_sp *mlxsw_sp,
					 u8 base_port, unsigned int count)
{
	u8 local_port, module, width = MLXSW_PORT_MODULE_MAX_WIDTH;
	int i;

	/* Split by four means we need to re-create two ports, otherwise
	 * only one.
	 */
	count = count / 2;

	for (i = 0; i < count; i++) {
		local_port = base_port + i * 2;
		module = mlxsw_sp->port_to_module[local_port];

		mlxsw_sp_port_module_map(mlxsw_sp, local_port, module, width,
					 0);
	}

	for (i = 0; i < count; i++)
		__mlxsw_sp_port_swid_set(mlxsw_sp, base_port + i * 2, 0);

	for (i = 0; i < count; i++) {
		local_port = base_port + i * 2;
		module = mlxsw_sp->port_to_module[local_port];

		mlxsw_sp_port_create(mlxsw_sp, local_port, false, module,
				     width, 0);
	}
}

static int mlxsw_sp_port_split(struct mlxsw_core *mlxsw_core, u8 local_port,
			       unsigned int count)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 module, cur_width, base_port;
	int i;
	int err;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		return -EINVAL;
	}

	module = mlxsw_sp_port->mapping.module;
	cur_width = mlxsw_sp_port->mapping.width;

	if (count != 2 && count != 4) {
		netdev_err(mlxsw_sp_port->dev, "Port can only be split into 2 or 4 ports\n");
		return -EINVAL;
	}

	if (cur_width != MLXSW_PORT_MODULE_MAX_WIDTH) {
		netdev_err(mlxsw_sp_port->dev, "Port cannot be split further\n");
		return -EINVAL;
	}

	/* Make sure we have enough slave (even) ports for the split. */
	if (count == 2) {
		base_port = local_port;
		if (mlxsw_sp->ports[base_port + 1]) {
			netdev_err(mlxsw_sp_port->dev, "Invalid split configuration\n");
			return -EINVAL;
		}
	} else {
		base_port = mlxsw_sp_cluster_base_port_get(local_port);
		if (mlxsw_sp->ports[base_port + 1] ||
		    mlxsw_sp->ports[base_port + 3]) {
			netdev_err(mlxsw_sp_port->dev, "Invalid split configuration\n");
			return -EINVAL;
		}
	}

	for (i = 0; i < count; i++)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);

	err = mlxsw_sp_port_split_create(mlxsw_sp, base_port, module, count);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to create split ports\n");
		goto err_port_split_create;
	}

	return 0;

err_port_split_create:
	mlxsw_sp_port_unsplit_create(mlxsw_sp, base_port, count);
	return err;
}

static int mlxsw_sp_port_unsplit(struct mlxsw_core *mlxsw_core, u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	struct mlxsw_sp_port *mlxsw_sp_port;
	u8 cur_width, base_port;
	unsigned int count;
	int i;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port) {
		dev_err(mlxsw_sp->bus_info->dev, "Port number \"%d\" does not exist\n",
			local_port);
		return -EINVAL;
	}

	if (!mlxsw_sp_port->split) {
		netdev_err(mlxsw_sp_port->dev, "Port wasn't split\n");
		return -EINVAL;
	}

	cur_width = mlxsw_sp_port->mapping.width;
	count = cur_width == 1 ? 4 : 2;

	base_port = mlxsw_sp_cluster_base_port_get(local_port);

	/* Determine which ports to remove. */
	if (count == 2 && local_port >= base_port + 2)
		base_port = base_port + 2;

	for (i = 0; i < count; i++)
		mlxsw_sp_port_remove(mlxsw_sp, base_port + i);

	mlxsw_sp_port_unsplit_create(mlxsw_sp, base_port, count);

	return 0;
}

static void mlxsw_sp_pude_event_func(const struct mlxsw_reg_info *reg,
				     char *pude_pl, void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port;
	enum mlxsw_reg_pude_oper_status status;
	u8 local_port;

	local_port = mlxsw_reg_pude_local_port_get(pude_pl);
	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	if (!mlxsw_sp_port)
		return;

	status = mlxsw_reg_pude_oper_status_get(pude_pl);
	if (status == MLXSW_PORT_OPER_STATUS_UP) {
		netdev_info(mlxsw_sp_port->dev, "link up\n");
		netif_carrier_on(mlxsw_sp_port->dev);
	} else {
		netdev_info(mlxsw_sp_port->dev, "link down\n");
		netif_carrier_off(mlxsw_sp_port->dev);
	}
}

static struct mlxsw_event_listener mlxsw_sp_pude_event = {
	.func = mlxsw_sp_pude_event_func,
	.trap_id = MLXSW_TRAP_ID_PUDE,
};

static int mlxsw_sp_event_register(struct mlxsw_sp *mlxsw_sp,
				   enum mlxsw_event_trap_id trap_id)
{
	struct mlxsw_event_listener *el;
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int err;

	switch (trap_id) {
	case MLXSW_TRAP_ID_PUDE:
		el = &mlxsw_sp_pude_event;
		break;
	}
	err = mlxsw_core_event_listener_register(mlxsw_sp->core, el, mlxsw_sp);
	if (err)
		return err;

	mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_FORWARD, trap_id);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);
	if (err)
		goto err_event_trap_set;

	return 0;

err_event_trap_set:
	mlxsw_core_event_listener_unregister(mlxsw_sp->core, el, mlxsw_sp);
	return err;
}

static void mlxsw_sp_event_unregister(struct mlxsw_sp *mlxsw_sp,
				      enum mlxsw_event_trap_id trap_id)
{
	struct mlxsw_event_listener *el;

	switch (trap_id) {
	case MLXSW_TRAP_ID_PUDE:
		el = &mlxsw_sp_pude_event;
		break;
	}
	mlxsw_core_event_listener_unregister(mlxsw_sp->core, el, mlxsw_sp);
}

static void mlxsw_sp_rx_listener_func(struct sk_buff *skb, u8 local_port,
				      void *priv)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *mlxsw_sp_port = mlxsw_sp->ports[local_port];
	struct mlxsw_sp_port_pcpu_stats *pcpu_stats;

	if (unlikely(!mlxsw_sp_port)) {
		dev_warn_ratelimited(mlxsw_sp->bus_info->dev, "Port %d: skb received for non-existent port\n",
				     local_port);
		return;
	}

	skb->dev = mlxsw_sp_port->dev;

	pcpu_stats = this_cpu_ptr(mlxsw_sp_port->pcpu_stats);
	u64_stats_update_begin(&pcpu_stats->syncp);
	pcpu_stats->rx_packets++;
	pcpu_stats->rx_bytes += skb->len;
	u64_stats_update_end(&pcpu_stats->syncp);

	skb->protocol = eth_type_trans(skb, skb->dev);
	netif_receive_skb(skb);
}

static void mlxsw_sp_rx_listener_mark_func(struct sk_buff *skb, u8 local_port,
					   void *priv)
{
	skb->offload_fwd_mark = 1;
	return mlxsw_sp_rx_listener_func(skb, local_port, priv);
}

#define MLXSW_SP_RXL(_func, _trap_id, _action)			\
	{							\
		.func = _func,					\
		.local_port = MLXSW_PORT_DONT_CARE,		\
		.trap_id = MLXSW_TRAP_ID_##_trap_id,		\
		.action = MLXSW_REG_HPKT_ACTION_##_action,	\
	}

static const struct mlxsw_rx_listener mlxsw_sp_rx_listener[] = {
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, FDB_MC, TRAP_TO_CPU),
	/* Traps for specific L2 packet types, not trapped as FDB MC */
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, STP, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, LACP, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, EAPOL, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, LLDP, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, MMRP, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, MVRP, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, RPVST, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_mark_func, DHCP, MIRROR_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_mark_func, IGMP_QUERY, MIRROR_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, IGMP_V1_REPORT, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, IGMP_V2_REPORT, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, IGMP_V2_LEAVE, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, IGMP_V3_REPORT, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_mark_func, ARPBC, MIRROR_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_mark_func, ARPUC, MIRROR_TO_CPU),
	/* L3 traps */
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, MTUERROR, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, TTLERROR, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, LBERROR, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_mark_func, OSPF, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, IP2ME, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, RTR_INGRESS0, TRAP_TO_CPU),
	MLXSW_SP_RXL(mlxsw_sp_rx_listener_func, HOST_MISS_IPV4, TRAP_TO_CPU),
};

static int mlxsw_sp_traps_init(struct mlxsw_sp *mlxsw_sp)
{
	char htgt_pl[MLXSW_REG_HTGT_LEN];
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int i;
	int err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_RX);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	mlxsw_reg_htgt_pack(htgt_pl, MLXSW_REG_HTGT_TRAP_GROUP_CTRL);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(htgt), htgt_pl);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_rx_listener); i++) {
		err = mlxsw_core_rx_listener_register(mlxsw_sp->core,
						      &mlxsw_sp_rx_listener[i],
						      mlxsw_sp);
		if (err)
			goto err_rx_listener_register;

		mlxsw_reg_hpkt_pack(hpkt_pl, mlxsw_sp_rx_listener[i].action,
				    mlxsw_sp_rx_listener[i].trap_id);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);
		if (err)
			goto err_rx_trap_set;
	}
	return 0;

err_rx_trap_set:
	mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
					  &mlxsw_sp_rx_listener[i],
					  mlxsw_sp);
err_rx_listener_register:
	for (i--; i >= 0; i--) {
		mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_DISCARD,
				    mlxsw_sp_rx_listener[i].trap_id);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);

		mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
						  &mlxsw_sp_rx_listener[i],
						  mlxsw_sp);
	}
	return err;
}

static void mlxsw_sp_traps_fini(struct mlxsw_sp *mlxsw_sp)
{
	char hpkt_pl[MLXSW_REG_HPKT_LEN];
	int i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_rx_listener); i++) {
		mlxsw_reg_hpkt_pack(hpkt_pl, MLXSW_REG_HPKT_ACTION_DISCARD,
				    mlxsw_sp_rx_listener[i].trap_id);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(hpkt), hpkt_pl);

		mlxsw_core_rx_listener_unregister(mlxsw_sp->core,
						  &mlxsw_sp_rx_listener[i],
						  mlxsw_sp);
	}
}

static int __mlxsw_sp_flood_init(struct mlxsw_core *mlxsw_core,
				 enum mlxsw_reg_sfgc_type type,
				 enum mlxsw_reg_sfgc_bridge_type bridge_type)
{
	enum mlxsw_flood_table_type table_type;
	enum mlxsw_sp_flood_table flood_table;
	char sfgc_pl[MLXSW_REG_SFGC_LEN];

	if (bridge_type == MLXSW_REG_SFGC_BRIDGE_TYPE_VFID)
		table_type = MLXSW_REG_SFGC_TABLE_TYPE_FID;
	else
		table_type = MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFEST;

	if (type == MLXSW_REG_SFGC_TYPE_UNKNOWN_UNICAST)
		flood_table = MLXSW_SP_FLOOD_TABLE_UC;
	else
		flood_table = MLXSW_SP_FLOOD_TABLE_BM;

	mlxsw_reg_sfgc_pack(sfgc_pl, type, bridge_type, table_type,
			    flood_table);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(sfgc), sfgc_pl);
}

static int mlxsw_sp_flood_init(struct mlxsw_sp *mlxsw_sp)
{
	int type, err;

	for (type = 0; type < MLXSW_REG_SFGC_TYPE_MAX; type++) {
		if (type == MLXSW_REG_SFGC_TYPE_RESERVED)
			continue;

		err = __mlxsw_sp_flood_init(mlxsw_sp->core, type,
					    MLXSW_REG_SFGC_BRIDGE_TYPE_VFID);
		if (err)
			return err;

		err = __mlxsw_sp_flood_init(mlxsw_sp->core, type,
					    MLXSW_REG_SFGC_BRIDGE_TYPE_1Q_FID);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_lag_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_resources *resources;
	char slcr_pl[MLXSW_REG_SLCR_LEN];
	int err;

	mlxsw_reg_slcr_pack(slcr_pl, MLXSW_REG_SLCR_LAG_HASH_SMAC |
				     MLXSW_REG_SLCR_LAG_HASH_DMAC |
				     MLXSW_REG_SLCR_LAG_HASH_ETHERTYPE |
				     MLXSW_REG_SLCR_LAG_HASH_VLANID |
				     MLXSW_REG_SLCR_LAG_HASH_SIP |
				     MLXSW_REG_SLCR_LAG_HASH_DIP |
				     MLXSW_REG_SLCR_LAG_HASH_SPORT |
				     MLXSW_REG_SLCR_LAG_HASH_DPORT |
				     MLXSW_REG_SLCR_LAG_HASH_IPPROTO);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcr), slcr_pl);
	if (err)
		return err;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	if (!(resources->max_lag_valid && resources->max_ports_in_lag_valid))
		return -EIO;

	mlxsw_sp->lags = kcalloc(resources->max_lag,
				 sizeof(struct mlxsw_sp_upper),
				 GFP_KERNEL);
	if (!mlxsw_sp->lags)
		return -ENOMEM;

	return 0;
}

static void mlxsw_sp_lag_fini(struct mlxsw_sp *mlxsw_sp)
{
	kfree(mlxsw_sp->lags);
}

static int mlxsw_sp_init(struct mlxsw_core *mlxsw_core,
			 const struct mlxsw_bus_info *mlxsw_bus_info)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);
	int err;

	mlxsw_sp->core = mlxsw_core;
	mlxsw_sp->bus_info = mlxsw_bus_info;
	INIT_LIST_HEAD(&mlxsw_sp->fids);
	INIT_LIST_HEAD(&mlxsw_sp->vfids.list);
	INIT_LIST_HEAD(&mlxsw_sp->br_mids.list);

	err = mlxsw_sp_base_mac_get(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to get base mac\n");
		return err;
	}

	err = mlxsw_sp_event_register(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to register for PUDE events\n");
		return err;
	}

	err = mlxsw_sp_traps_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to set traps for RX\n");
		goto err_rx_listener_register;
	}

	err = mlxsw_sp_flood_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize flood tables\n");
		goto err_flood_init;
	}

	err = mlxsw_sp_buffers_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize buffers\n");
		goto err_buffers_init;
	}

	err = mlxsw_sp_lag_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize LAG\n");
		goto err_lag_init;
	}

	err = mlxsw_sp_switchdev_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize switchdev\n");
		goto err_switchdev_init;
	}

	err = mlxsw_sp_router_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to initialize router\n");
		goto err_router_init;
	}

	err = mlxsw_sp_span_init(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to init span system\n");
		goto err_span_init;
	}

	err = mlxsw_sp_ports_create(mlxsw_sp);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to create ports\n");
		goto err_ports_create;
	}

	return 0;

err_ports_create:
	mlxsw_sp_span_fini(mlxsw_sp);
err_span_init:
	mlxsw_sp_router_fini(mlxsw_sp);
err_router_init:
	mlxsw_sp_switchdev_fini(mlxsw_sp);
err_switchdev_init:
	mlxsw_sp_lag_fini(mlxsw_sp);
err_lag_init:
	mlxsw_sp_buffers_fini(mlxsw_sp);
err_buffers_init:
err_flood_init:
	mlxsw_sp_traps_fini(mlxsw_sp);
err_rx_listener_register:
	mlxsw_sp_event_unregister(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
	return err;
}

static void mlxsw_sp_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_core_driver_priv(mlxsw_core);

	mlxsw_sp_ports_remove(mlxsw_sp);
	mlxsw_sp_span_fini(mlxsw_sp);
	mlxsw_sp_router_fini(mlxsw_sp);
	mlxsw_sp_switchdev_fini(mlxsw_sp);
	mlxsw_sp_lag_fini(mlxsw_sp);
	mlxsw_sp_buffers_fini(mlxsw_sp);
	mlxsw_sp_traps_fini(mlxsw_sp);
	mlxsw_sp_event_unregister(mlxsw_sp, MLXSW_TRAP_ID_PUDE);
	WARN_ON(!list_empty(&mlxsw_sp->vfids.list));
	WARN_ON(!list_empty(&mlxsw_sp->fids));
}

static struct mlxsw_config_profile mlxsw_sp_config_profile = {
	.used_max_vepa_channels		= 1,
	.max_vepa_channels		= 0,
	.used_max_mid			= 1,
	.max_mid			= MLXSW_SP_MID_MAX,
	.used_max_pgt			= 1,
	.max_pgt			= 0,
	.used_flood_tables		= 1,
	.used_flood_mode		= 1,
	.flood_mode			= 3,
	.max_fid_offset_flood_tables	= 2,
	.fid_offset_flood_table_size	= VLAN_N_VID - 1,
	.max_fid_flood_tables		= 2,
	.fid_flood_table_size		= MLXSW_SP_VFID_MAX,
	.used_max_ib_mc			= 1,
	.max_ib_mc			= 0,
	.used_max_pkey			= 1,
	.max_pkey			= 0,
	.used_kvd_split_data		= 1,
	.kvd_hash_granularity		= MLXSW_SP_KVD_GRANULARITY,
	.kvd_hash_single_parts		= 2,
	.kvd_hash_double_parts		= 1,
	.kvd_linear_size		= MLXSW_SP_KVD_LINEAR_SIZE,
	.swid_config			= {
		{
			.used_type	= 1,
			.type		= MLXSW_PORT_SWID_TYPE_ETH,
		}
	},
	.resource_query_enable		= 1,
};

static struct mlxsw_driver mlxsw_sp_driver = {
	.kind				= MLXSW_DEVICE_KIND_SPECTRUM,
	.owner				= THIS_MODULE,
	.priv_size			= sizeof(struct mlxsw_sp),
	.init				= mlxsw_sp_init,
	.fini				= mlxsw_sp_fini,
	.port_split			= mlxsw_sp_port_split,
	.port_unsplit			= mlxsw_sp_port_unsplit,
	.sb_pool_get			= mlxsw_sp_sb_pool_get,
	.sb_pool_set			= mlxsw_sp_sb_pool_set,
	.sb_port_pool_get		= mlxsw_sp_sb_port_pool_get,
	.sb_port_pool_set		= mlxsw_sp_sb_port_pool_set,
	.sb_tc_pool_bind_get		= mlxsw_sp_sb_tc_pool_bind_get,
	.sb_tc_pool_bind_set		= mlxsw_sp_sb_tc_pool_bind_set,
	.sb_occ_snapshot		= mlxsw_sp_sb_occ_snapshot,
	.sb_occ_max_clear		= mlxsw_sp_sb_occ_max_clear,
	.sb_occ_port_pool_get		= mlxsw_sp_sb_occ_port_pool_get,
	.sb_occ_tc_port_bind_get	= mlxsw_sp_sb_occ_tc_port_bind_get,
	.txhdr_construct		= mlxsw_sp_txhdr_construct,
	.txhdr_len			= MLXSW_TXHDR_LEN,
	.profile			= &mlxsw_sp_config_profile,
};

static bool mlxsw_sp_port_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &mlxsw_sp_port_netdev_ops;
}

static struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find(struct net_device *dev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	if (mlxsw_sp_port_dev_check(dev))
		return netdev_priv(dev);

	netdev_for_each_all_lower_dev(dev, lower_dev, iter) {
		if (mlxsw_sp_port_dev_check(lower_dev))
			return netdev_priv(lower_dev);
	}
	return NULL;
}

static struct mlxsw_sp *mlxsw_sp_lower_get(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find(dev);
	return mlxsw_sp_port ? mlxsw_sp_port->mlxsw_sp : NULL;
}

static struct mlxsw_sp_port *mlxsw_sp_port_dev_lower_find_rcu(struct net_device *dev)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	if (mlxsw_sp_port_dev_check(dev))
		return netdev_priv(dev);

	netdev_for_each_all_lower_dev_rcu(dev, lower_dev, iter) {
		if (mlxsw_sp_port_dev_check(lower_dev))
			return netdev_priv(lower_dev);
	}
	return NULL;
}

struct mlxsw_sp_port *mlxsw_sp_port_lower_dev_hold(struct net_device *dev)
{
	struct mlxsw_sp_port *mlxsw_sp_port;

	rcu_read_lock();
	mlxsw_sp_port = mlxsw_sp_port_dev_lower_find_rcu(dev);
	if (mlxsw_sp_port)
		dev_hold(mlxsw_sp_port->dev);
	rcu_read_unlock();
	return mlxsw_sp_port;
}

void mlxsw_sp_port_dev_put(struct mlxsw_sp_port *mlxsw_sp_port)
{
	dev_put(mlxsw_sp_port->dev);
}

static bool mlxsw_sp_rif_should_config(struct mlxsw_sp_rif *r,
				       unsigned long event)
{
	switch (event) {
	case NETDEV_UP:
		if (!r)
			return true;
		r->ref_count++;
		return false;
	case NETDEV_DOWN:
		if (r && --r->ref_count == 0)
			return true;
		/* It is possible we already removed the RIF ourselves
		 * if it was assigned to a netdev that is now a bridge
		 * or LAG slave.
		 */
		return false;
	}

	return false;
}

static int mlxsw_sp_avail_rif_get(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_resources *resources;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_rif; i++)
		if (!mlxsw_sp->rifs[i])
			return i;

	return MLXSW_SP_INVALID_RIF;
}

static void mlxsw_sp_vport_rif_sp_attr_get(struct mlxsw_sp_port *mlxsw_sp_vport,
					   bool *p_lagged, u16 *p_system_port)
{
	u8 local_port = mlxsw_sp_vport->local_port;

	*p_lagged = mlxsw_sp_vport->lagged;
	*p_system_port = *p_lagged ? mlxsw_sp_vport->lag_id : local_port;
}

static int mlxsw_sp_vport_rif_sp_op(struct mlxsw_sp_port *mlxsw_sp_vport,
				    struct net_device *l3_dev, u16 rif,
				    bool create)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	bool lagged = mlxsw_sp_vport->lagged;
	char ritr_pl[MLXSW_REG_RITR_LEN];
	u16 system_port;

	mlxsw_reg_ritr_pack(ritr_pl, create, MLXSW_REG_RITR_SP_IF, rif,
			    l3_dev->mtu, l3_dev->dev_addr);

	mlxsw_sp_vport_rif_sp_attr_get(mlxsw_sp_vport, &lagged, &system_port);
	mlxsw_reg_ritr_sp_if_pack(ritr_pl, lagged, system_port,
				  mlxsw_sp_vport_vid_get(mlxsw_sp_vport));

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static void mlxsw_sp_vport_rif_sp_leave(struct mlxsw_sp_port *mlxsw_sp_vport);

static struct mlxsw_sp_fid *
mlxsw_sp_rfid_alloc(u16 fid, struct net_device *l3_dev)
{
	struct mlxsw_sp_fid *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return NULL;

	f->leave = mlxsw_sp_vport_rif_sp_leave;
	f->ref_count = 0;
	f->dev = l3_dev;
	f->fid = fid;

	return f;
}

static struct mlxsw_sp_rif *
mlxsw_sp_rif_alloc(u16 rif, struct net_device *l3_dev, struct mlxsw_sp_fid *f)
{
	struct mlxsw_sp_rif *r;

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;

	ether_addr_copy(r->addr, l3_dev->dev_addr);
	r->mtu = l3_dev->mtu;
	r->ref_count = 1;
	r->dev = l3_dev;
	r->rif = rif;
	r->f = f;

	return r;
}

static struct mlxsw_sp_rif *
mlxsw_sp_vport_rif_sp_create(struct mlxsw_sp_port *mlxsw_sp_vport,
			     struct net_device *l3_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	struct mlxsw_sp_fid *f;
	struct mlxsw_sp_rif *r;
	u16 fid, rif;
	int err;

	rif = mlxsw_sp_avail_rif_get(mlxsw_sp);
	if (rif == MLXSW_SP_INVALID_RIF)
		return ERR_PTR(-ERANGE);

	err = mlxsw_sp_vport_rif_sp_op(mlxsw_sp_vport, l3_dev, rif, true);
	if (err)
		return ERR_PTR(err);

	fid = mlxsw_sp_rif_sp_to_fid(rif);
	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, fid, true);
	if (err)
		goto err_rif_fdb_op;

	f = mlxsw_sp_rfid_alloc(fid, l3_dev);
	if (!f) {
		err = -ENOMEM;
		goto err_rfid_alloc;
	}

	r = mlxsw_sp_rif_alloc(rif, l3_dev, f);
	if (!r) {
		err = -ENOMEM;
		goto err_rif_alloc;
	}

	f->r = r;
	mlxsw_sp->rifs[rif] = r;

	return r;

err_rif_alloc:
	kfree(f);
err_rfid_alloc:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, fid, false);
err_rif_fdb_op:
	mlxsw_sp_vport_rif_sp_op(mlxsw_sp_vport, l3_dev, rif, false);
	return ERR_PTR(err);
}

static void mlxsw_sp_vport_rif_sp_destroy(struct mlxsw_sp_port *mlxsw_sp_vport,
					  struct mlxsw_sp_rif *r)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	struct net_device *l3_dev = r->dev;
	struct mlxsw_sp_fid *f = r->f;
	u16 fid = f->fid;
	u16 rif = r->rif;

	mlxsw_sp->rifs[rif] = NULL;
	f->r = NULL;

	kfree(r);

	kfree(f);

	mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, fid, false);

	mlxsw_sp_vport_rif_sp_op(mlxsw_sp_vport, l3_dev, rif, false);
}

static int mlxsw_sp_vport_rif_sp_join(struct mlxsw_sp_port *mlxsw_sp_vport,
				      struct net_device *l3_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_vport->mlxsw_sp;
	struct mlxsw_sp_rif *r;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, l3_dev);
	if (!r) {
		r = mlxsw_sp_vport_rif_sp_create(mlxsw_sp_vport, l3_dev);
		if (IS_ERR(r))
			return PTR_ERR(r);
	}

	mlxsw_sp_vport_fid_set(mlxsw_sp_vport, r->f);
	r->f->ref_count++;

	netdev_dbg(mlxsw_sp_vport->dev, "Joined FID=%d\n", r->f->fid);

	return 0;
}

static void mlxsw_sp_vport_rif_sp_leave(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	struct mlxsw_sp_fid *f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);

	netdev_dbg(mlxsw_sp_vport->dev, "Left FID=%d\n", f->fid);

	mlxsw_sp_vport_fid_set(mlxsw_sp_vport, NULL);
	if (--f->ref_count == 0)
		mlxsw_sp_vport_rif_sp_destroy(mlxsw_sp_vport, f->r);
}

static int mlxsw_sp_inetaddr_vport_event(struct net_device *l3_dev,
					 struct net_device *port_dev,
					 unsigned long event, u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(port_dev);
	struct mlxsw_sp_port *mlxsw_sp_vport;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_vport))
		return -EINVAL;

	switch (event) {
	case NETDEV_UP:
		return mlxsw_sp_vport_rif_sp_join(mlxsw_sp_vport, l3_dev);
	case NETDEV_DOWN:
		mlxsw_sp_vport_rif_sp_leave(mlxsw_sp_vport);
		break;
	}

	return 0;
}

static int mlxsw_sp_inetaddr_port_event(struct net_device *port_dev,
					unsigned long event)
{
	if (netif_is_bridge_port(port_dev) || netif_is_lag_port(port_dev))
		return 0;

	return mlxsw_sp_inetaddr_vport_event(port_dev, port_dev, event, 1);
}

static int __mlxsw_sp_inetaddr_lag_event(struct net_device *l3_dev,
					 struct net_device *lag_dev,
					 unsigned long event, u16 vid)
{
	struct net_device *port_dev;
	struct list_head *iter;
	int err;

	netdev_for_each_lower_dev(lag_dev, port_dev, iter) {
		if (mlxsw_sp_port_dev_check(port_dev)) {
			err = mlxsw_sp_inetaddr_vport_event(l3_dev, port_dev,
							    event, vid);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mlxsw_sp_inetaddr_lag_event(struct net_device *lag_dev,
				       unsigned long event)
{
	if (netif_is_bridge_port(lag_dev))
		return 0;

	return __mlxsw_sp_inetaddr_lag_event(lag_dev, lag_dev, event, 1);
}

static struct mlxsw_sp_fid *mlxsw_sp_bridge_fid_get(struct mlxsw_sp *mlxsw_sp,
						    struct net_device *l3_dev)
{
	u16 fid;

	if (is_vlan_dev(l3_dev))
		fid = vlan_dev_vlan_id(l3_dev);
	else if (mlxsw_sp->master_bridge.dev == l3_dev)
		fid = 1;
	else
		return mlxsw_sp_vfid_find(mlxsw_sp, l3_dev);

	return mlxsw_sp_fid_find(mlxsw_sp, fid);
}

static enum mlxsw_flood_table_type mlxsw_sp_flood_table_type_get(u16 fid)
{
	return mlxsw_sp_fid_is_vfid(fid) ? MLXSW_REG_SFGC_TABLE_TYPE_FID :
	       MLXSW_REG_SFGC_TABLE_TYPE_FID_OFFEST;
}

static u16 mlxsw_sp_flood_table_index_get(u16 fid)
{
	return mlxsw_sp_fid_is_vfid(fid) ? mlxsw_sp_fid_to_vfid(fid) : fid;
}

static int mlxsw_sp_router_port_flood_set(struct mlxsw_sp *mlxsw_sp, u16 fid,
					  bool set)
{
	enum mlxsw_flood_table_type table_type;
	char *sftr_pl;
	u16 index;
	int err;

	sftr_pl = kmalloc(MLXSW_REG_SFTR_LEN, GFP_KERNEL);
	if (!sftr_pl)
		return -ENOMEM;

	table_type = mlxsw_sp_flood_table_type_get(fid);
	index = mlxsw_sp_flood_table_index_get(fid);
	mlxsw_reg_sftr_pack(sftr_pl, MLXSW_SP_FLOOD_TABLE_BM, index, table_type,
			    1, MLXSW_PORT_ROUTER_PORT, set);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sftr), sftr_pl);

	kfree(sftr_pl);
	return err;
}

static enum mlxsw_reg_ritr_if_type mlxsw_sp_rif_type_get(u16 fid)
{
	if (mlxsw_sp_fid_is_vfid(fid))
		return MLXSW_REG_RITR_FID_IF;
	else
		return MLXSW_REG_RITR_VLAN_IF;
}

static int mlxsw_sp_rif_bridge_op(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *l3_dev,
				  u16 fid, u16 rif,
				  bool create)
{
	enum mlxsw_reg_ritr_if_type rif_type;
	char ritr_pl[MLXSW_REG_RITR_LEN];

	rif_type = mlxsw_sp_rif_type_get(fid);
	mlxsw_reg_ritr_pack(ritr_pl, create, rif_type, rif, l3_dev->mtu,
			    l3_dev->dev_addr);
	mlxsw_reg_ritr_fid_set(ritr_pl, rif_type, fid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_rif_bridge_create(struct mlxsw_sp *mlxsw_sp,
				      struct net_device *l3_dev,
				      struct mlxsw_sp_fid *f)
{
	struct mlxsw_sp_rif *r;
	u16 rif;
	int err;

	rif = mlxsw_sp_avail_rif_get(mlxsw_sp);
	if (rif == MLXSW_SP_INVALID_RIF)
		return -ERANGE;

	err = mlxsw_sp_router_port_flood_set(mlxsw_sp, f->fid, true);
	if (err)
		return err;

	err = mlxsw_sp_rif_bridge_op(mlxsw_sp, l3_dev, f->fid, rif, true);
	if (err)
		goto err_rif_bridge_op;

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, f->fid, true);
	if (err)
		goto err_rif_fdb_op;

	r = mlxsw_sp_rif_alloc(rif, l3_dev, f);
	if (!r) {
		err = -ENOMEM;
		goto err_rif_alloc;
	}

	f->r = r;
	mlxsw_sp->rifs[rif] = r;

	netdev_dbg(l3_dev, "RIF=%d created\n", rif);

	return 0;

err_rif_alloc:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, f->fid, false);
err_rif_fdb_op:
	mlxsw_sp_rif_bridge_op(mlxsw_sp, l3_dev, f->fid, rif, false);
err_rif_bridge_op:
	mlxsw_sp_router_port_flood_set(mlxsw_sp, f->fid, false);
	return err;
}

void mlxsw_sp_rif_bridge_destroy(struct mlxsw_sp *mlxsw_sp,
				 struct mlxsw_sp_rif *r)
{
	struct net_device *l3_dev = r->dev;
	struct mlxsw_sp_fid *f = r->f;
	u16 rif = r->rif;

	mlxsw_sp->rifs[rif] = NULL;
	f->r = NULL;

	kfree(r);

	mlxsw_sp_rif_fdb_op(mlxsw_sp, l3_dev->dev_addr, f->fid, false);

	mlxsw_sp_rif_bridge_op(mlxsw_sp, l3_dev, f->fid, rif, false);

	mlxsw_sp_router_port_flood_set(mlxsw_sp, f->fid, false);

	netdev_dbg(l3_dev, "RIF=%d destroyed\n", rif);
}

static int mlxsw_sp_inetaddr_bridge_event(struct net_device *l3_dev,
					  struct net_device *br_dev,
					  unsigned long event)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(l3_dev);
	struct mlxsw_sp_fid *f;

	/* FID can either be an actual FID if the L3 device is the
	 * VLAN-aware bridge or a VLAN device on top. Otherwise, the
	 * L3 device is a VLAN-unaware bridge and we get a vFID.
	 */
	f = mlxsw_sp_bridge_fid_get(mlxsw_sp, l3_dev);
	if (WARN_ON(!f))
		return -EINVAL;

	switch (event) {
	case NETDEV_UP:
		return mlxsw_sp_rif_bridge_create(mlxsw_sp, l3_dev, f);
	case NETDEV_DOWN:
		mlxsw_sp_rif_bridge_destroy(mlxsw_sp, f->r);
		break;
	}

	return 0;
}

static int mlxsw_sp_inetaddr_vlan_event(struct net_device *vlan_dev,
					unsigned long event)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_lower_get(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_inetaddr_vport_event(vlan_dev, real_dev, event,
						     vid);
	else if (netif_is_lag_master(real_dev))
		return __mlxsw_sp_inetaddr_lag_event(vlan_dev, real_dev, event,
						     vid);
	else if (netif_is_bridge_master(real_dev) &&
		 mlxsw_sp->master_bridge.dev == real_dev)
		return mlxsw_sp_inetaddr_bridge_event(vlan_dev, real_dev,
						      event);

	return 0;
}

static int mlxsw_sp_inetaddr_event(struct notifier_block *unused,
				   unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *) ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *r;
	int err = 0;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		goto out;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!mlxsw_sp_rif_should_config(r, event))
		goto out;

	if (mlxsw_sp_port_dev_check(dev))
		err = mlxsw_sp_inetaddr_port_event(dev, event);
	else if (netif_is_lag_master(dev))
		err = mlxsw_sp_inetaddr_lag_event(dev, event);
	else if (netif_is_bridge_master(dev))
		err = mlxsw_sp_inetaddr_bridge_event(dev, dev, event);
	else if (is_vlan_dev(dev))
		err = mlxsw_sp_inetaddr_vlan_event(dev, event);

out:
	return notifier_from_errno(err);
}

static int mlxsw_sp_rif_edit(struct mlxsw_sp *mlxsw_sp, u16 rif,
			     const char *mac, int mtu)
{
	char ritr_pl[MLXSW_REG_RITR_LEN];
	int err;

	mlxsw_reg_ritr_rif_pack(ritr_pl, rif);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
	if (err)
		return err;

	mlxsw_reg_ritr_mtu_set(ritr_pl, mtu);
	mlxsw_reg_ritr_if_mac_memcpy_to(ritr_pl, mac);
	mlxsw_reg_ritr_op_set(ritr_pl, MLXSW_REG_RITR_RIF_CREATE);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ritr), ritr_pl);
}

static int mlxsw_sp_netdevice_router_port_event(struct net_device *dev)
{
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_rif *r;
	int err;

	mlxsw_sp = mlxsw_sp_lower_get(dev);
	if (!mlxsw_sp)
		return 0;

	r = mlxsw_sp_rif_find_by_dev(mlxsw_sp, dev);
	if (!r)
		return 0;

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, r->addr, r->f->fid, false);
	if (err)
		return err;

	err = mlxsw_sp_rif_edit(mlxsw_sp, r->rif, dev->dev_addr, dev->mtu);
	if (err)
		goto err_rif_edit;

	err = mlxsw_sp_rif_fdb_op(mlxsw_sp, dev->dev_addr, r->f->fid, true);
	if (err)
		goto err_rif_fdb_op;

	ether_addr_copy(r->addr, dev->dev_addr);
	r->mtu = dev->mtu;

	netdev_dbg(dev, "Updated RIF=%d\n", r->rif);

	return 0;

err_rif_fdb_op:
	mlxsw_sp_rif_edit(mlxsw_sp, r->rif, r->addr, r->mtu);
err_rif_edit:
	mlxsw_sp_rif_fdb_op(mlxsw_sp, r->addr, r->f->fid, true);
	return err;
}

static bool mlxsw_sp_lag_port_fid_member(struct mlxsw_sp_port *lag_port,
					 u16 fid)
{
	if (mlxsw_sp_fid_is_vfid(fid))
		return mlxsw_sp_port_vport_find_by_fid(lag_port, fid);
	else
		return test_bit(fid, lag_port->active_vlans);
}

static bool mlxsw_sp_port_fdb_should_flush(struct mlxsw_sp_port *mlxsw_sp_port,
					   u16 fid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u8 local_port = mlxsw_sp_port->local_port;
	u16 lag_id = mlxsw_sp_port->lag_id;
	struct mlxsw_resources *resources;
	int i, count = 0;

	if (!mlxsw_sp_port->lagged)
		return true;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_ports_in_lag; i++) {
		struct mlxsw_sp_port *lag_port;

		lag_port = mlxsw_sp_port_lagged_get(mlxsw_sp, lag_id, i);
		if (!lag_port || lag_port->local_port == local_port)
			continue;
		if (mlxsw_sp_lag_port_fid_member(lag_port, fid))
			count++;
	}

	return !count;
}

static int
mlxsw_sp_port_fdb_flush_by_port_fid(const struct mlxsw_sp_port *mlxsw_sp_port,
				    u16 fid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_PORT_AND_FID);
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid);
	mlxsw_reg_sfdf_port_fid_system_port_set(sfdf_pl,
						mlxsw_sp_port->local_port);

	netdev_dbg(mlxsw_sp_port->dev, "FDB flushed using Port=%d, FID=%d\n",
		   mlxsw_sp_port->local_port, fid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static int
mlxsw_sp_port_fdb_flush_by_lag_id_fid(const struct mlxsw_sp_port *mlxsw_sp_port,
				      u16 fid)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_LAG_AND_FID);
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid);
	mlxsw_reg_sfdf_lag_fid_lag_id_set(sfdf_pl, mlxsw_sp_port->lag_id);

	netdev_dbg(mlxsw_sp_port->dev, "FDB flushed using LAG ID=%d, FID=%d\n",
		   mlxsw_sp_port->lag_id, fid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

int mlxsw_sp_port_fdb_flush(struct mlxsw_sp_port *mlxsw_sp_port, u16 fid)
{
	if (!mlxsw_sp_port_fdb_should_flush(mlxsw_sp_port, fid))
		return 0;

	if (mlxsw_sp_port->lagged)
		return mlxsw_sp_port_fdb_flush_by_lag_id_fid(mlxsw_sp_port,
							     fid);
	else
		return mlxsw_sp_port_fdb_flush_by_port_fid(mlxsw_sp_port, fid);
}

static void mlxsw_sp_master_bridge_gone_sync(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_fid *f, *tmp;

	list_for_each_entry_safe(f, tmp, &mlxsw_sp->fids, list)
		if (--f->ref_count == 0)
			mlxsw_sp_fid_destroy(mlxsw_sp, f);
		else
			WARN_ON_ONCE(1);
}

static bool mlxsw_sp_master_bridge_check(struct mlxsw_sp *mlxsw_sp,
					 struct net_device *br_dev)
{
	return !mlxsw_sp->master_bridge.dev ||
	       mlxsw_sp->master_bridge.dev == br_dev;
}

static void mlxsw_sp_master_bridge_inc(struct mlxsw_sp *mlxsw_sp,
				       struct net_device *br_dev)
{
	mlxsw_sp->master_bridge.dev = br_dev;
	mlxsw_sp->master_bridge.ref_count++;
}

static void mlxsw_sp_master_bridge_dec(struct mlxsw_sp *mlxsw_sp)
{
	if (--mlxsw_sp->master_bridge.ref_count == 0) {
		mlxsw_sp->master_bridge.dev = NULL;
		/* It's possible upper VLAN devices are still holding
		 * references to underlying FIDs. Drop the reference
		 * and release the resources if it was the last one.
		 * If it wasn't, then something bad happened.
		 */
		mlxsw_sp_master_bridge_gone_sync(mlxsw_sp);
	}
}

static int mlxsw_sp_port_bridge_join(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct net_device *br_dev)
{
	struct net_device *dev = mlxsw_sp_port->dev;
	int err;

	/* When port is not bridged untagged packets are tagged with
	 * PVID=VID=1, thereby creating an implicit VLAN interface in
	 * the device. Remove it and let bridge code take care of its
	 * own VLANs.
	 */
	err = mlxsw_sp_port_kill_vid(dev, 0, 1);
	if (err)
		return err;

	mlxsw_sp_master_bridge_inc(mlxsw_sp_port->mlxsw_sp, br_dev);

	mlxsw_sp_port->learning = 1;
	mlxsw_sp_port->learning_sync = 1;
	mlxsw_sp_port->uc_flood = 1;
	mlxsw_sp_port->bridged = 1;

	return 0;
}

static void mlxsw_sp_port_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct net_device *dev = mlxsw_sp_port->dev;

	mlxsw_sp_port_pvid_set(mlxsw_sp_port, 1);

	mlxsw_sp_master_bridge_dec(mlxsw_sp_port->mlxsw_sp);

	mlxsw_sp_port->learning = 0;
	mlxsw_sp_port->learning_sync = 0;
	mlxsw_sp_port->uc_flood = 0;
	mlxsw_sp_port->bridged = 0;

	/* Add implicit VLAN interface in the device, so that untagged
	 * packets will be classified to the default vFID.
	 */
	mlxsw_sp_port_add_vid(dev, 0, 1);
}

static int mlxsw_sp_lag_create(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_create_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_destroy(struct mlxsw_sp *mlxsw_sp, u16 lag_id)
{
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_destroy_pack(sldr_pl, lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_col_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				     u16 lag_id, u8 port_index)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_add_pack(slcor_pl, mlxsw_sp_port->local_port,
				      lag_id, port_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_port_remove_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_enable(struct mlxsw_sp_port *mlxsw_sp_port,
					u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_enable_pack(slcor_pl, mlxsw_sp_port->local_port,
					lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_col_port_disable(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char slcor_pl[MLXSW_REG_SLCOR_LEN];

	mlxsw_reg_slcor_col_disable_pack(slcor_pl, mlxsw_sp_port->local_port,
					 lag_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(slcor), slcor_pl);
}

static int mlxsw_sp_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				  struct net_device *lag_dev,
				  u16 *p_lag_id)
{
	struct mlxsw_resources *resources;
	struct mlxsw_sp_upper *lag;
	int free_lag_id = -1;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_lag; i++) {
		lag = mlxsw_sp_lag_get(mlxsw_sp, i);
		if (lag->ref_count) {
			if (lag->dev == lag_dev) {
				*p_lag_id = i;
				return 0;
			}
		} else if (free_lag_id < 0) {
			free_lag_id = i;
		}
	}
	if (free_lag_id < 0)
		return -EBUSY;
	*p_lag_id = free_lag_id;
	return 0;
}

static bool
mlxsw_sp_master_lag_check(struct mlxsw_sp *mlxsw_sp,
			  struct net_device *lag_dev,
			  struct netdev_lag_upper_info *lag_upper_info)
{
	u16 lag_id;

	if (mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id) != 0)
		return false;
	if (lag_upper_info->tx_type != NETDEV_LAG_TX_TYPE_HASH)
		return false;
	return true;
}

static int mlxsw_sp_port_lag_index_get(struct mlxsw_sp *mlxsw_sp,
				       u16 lag_id, u8 *p_port_index)
{
	struct mlxsw_resources *resources;
	int i;

	resources = mlxsw_core_resources_get(mlxsw_sp->core);
	for (i = 0; i < resources->max_ports_in_lag; i++) {
		if (!mlxsw_sp_port_lagged_get(mlxsw_sp, lag_id, i)) {
			*p_port_index = i;
			return 0;
		}
	}
	return -EBUSY;
}

static void
mlxsw_sp_port_pvid_vport_lag_join(struct mlxsw_sp_port *mlxsw_sp_port,
				  u16 lag_id)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_fid *f;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, 1);
	if (WARN_ON(!mlxsw_sp_vport))
		return;

	/* If vPort is assigned a RIF, then leave it since it's no
	 * longer valid.
	 */
	f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);
	if (f)
		f->leave(mlxsw_sp_vport);

	mlxsw_sp_vport->lag_id = lag_id;
	mlxsw_sp_vport->lagged = 1;
}

static void
mlxsw_sp_port_pvid_vport_lag_leave(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct mlxsw_sp_fid *f;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, 1);
	if (WARN_ON(!mlxsw_sp_vport))
		return;

	f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);
	if (f)
		f->leave(mlxsw_sp_vport);

	mlxsw_sp_vport->lagged = 0;
}

static int mlxsw_sp_port_lag_join(struct mlxsw_sp_port *mlxsw_sp_port,
				  struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_upper *lag;
	u16 lag_id;
	u8 port_index;
	int err;

	err = mlxsw_sp_lag_index_get(mlxsw_sp, lag_dev, &lag_id);
	if (err)
		return err;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	if (!lag->ref_count) {
		err = mlxsw_sp_lag_create(mlxsw_sp, lag_id);
		if (err)
			return err;
		lag->dev = lag_dev;
	}

	err = mlxsw_sp_port_lag_index_get(mlxsw_sp, lag_id, &port_index);
	if (err)
		return err;
	err = mlxsw_sp_lag_col_port_add(mlxsw_sp_port, lag_id, port_index);
	if (err)
		goto err_col_port_add;
	err = mlxsw_sp_lag_col_port_enable(mlxsw_sp_port, lag_id);
	if (err)
		goto err_col_port_enable;

	mlxsw_core_lag_mapping_set(mlxsw_sp->core, lag_id, port_index,
				   mlxsw_sp_port->local_port);
	mlxsw_sp_port->lag_id = lag_id;
	mlxsw_sp_port->lagged = 1;
	lag->ref_count++;

	mlxsw_sp_port_pvid_vport_lag_join(mlxsw_sp_port, lag_id);

	return 0;

err_col_port_enable:
	mlxsw_sp_lag_col_port_remove(mlxsw_sp_port, lag_id);
err_col_port_add:
	if (!lag->ref_count)
		mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);
	return err;
}

static void mlxsw_sp_port_lag_leave(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct net_device *lag_dev)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u16 lag_id = mlxsw_sp_port->lag_id;
	struct mlxsw_sp_upper *lag;

	if (!mlxsw_sp_port->lagged)
		return;
	lag = mlxsw_sp_lag_get(mlxsw_sp, lag_id);
	WARN_ON(lag->ref_count == 0);

	mlxsw_sp_lag_col_port_disable(mlxsw_sp_port, lag_id);
	mlxsw_sp_lag_col_port_remove(mlxsw_sp_port, lag_id);

	if (mlxsw_sp_port->bridged) {
		mlxsw_sp_port_active_vlans_del(mlxsw_sp_port);
		mlxsw_sp_port_bridge_leave(mlxsw_sp_port);
	}

	if (lag->ref_count == 1)
		mlxsw_sp_lag_destroy(mlxsw_sp, lag_id);

	mlxsw_core_lag_mapping_clear(mlxsw_sp->core, lag_id,
				     mlxsw_sp_port->local_port);
	mlxsw_sp_port->lagged = 0;
	lag->ref_count--;

	mlxsw_sp_port_pvid_vport_lag_leave(mlxsw_sp_port);
}

static int mlxsw_sp_lag_dist_port_add(struct mlxsw_sp_port *mlxsw_sp_port,
				      u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_add_port_pack(sldr_pl, lag_id,
					 mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_lag_dist_port_remove(struct mlxsw_sp_port *mlxsw_sp_port,
					 u16 lag_id)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char sldr_pl[MLXSW_REG_SLDR_LEN];

	mlxsw_reg_sldr_lag_remove_port_pack(sldr_pl, lag_id,
					    mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sldr), sldr_pl);
}

static int mlxsw_sp_port_lag_tx_en_set(struct mlxsw_sp_port *mlxsw_sp_port,
				       bool lag_tx_enabled)
{
	if (lag_tx_enabled)
		return mlxsw_sp_lag_dist_port_add(mlxsw_sp_port,
						  mlxsw_sp_port->lag_id);
	else
		return mlxsw_sp_lag_dist_port_remove(mlxsw_sp_port,
						     mlxsw_sp_port->lag_id);
}

static int mlxsw_sp_port_lag_changed(struct mlxsw_sp_port *mlxsw_sp_port,
				     struct netdev_lag_lower_state_info *info)
{
	return mlxsw_sp_port_lag_tx_en_set(mlxsw_sp_port, info->tx_enabled);
}

static int mlxsw_sp_port_vlan_link(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct net_device *vlan_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_vport))
		return -EINVAL;

	mlxsw_sp_vport->dev = vlan_dev;

	return 0;
}

static void mlxsw_sp_port_vlan_unlink(struct mlxsw_sp_port *mlxsw_sp_port,
				      struct net_device *vlan_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);
	if (WARN_ON(!mlxsw_sp_vport))
		return;

	mlxsw_sp_vport->dev = mlxsw_sp_port->dev;
}

static int mlxsw_sp_netdevice_port_upper_event(struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct net_device *upper_dev;
	struct mlxsw_sp *mlxsw_sp;
	int err = 0;

	mlxsw_sp_port = netdev_priv(dev);
	mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	info = ptr;

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!is_vlan_dev(upper_dev) &&
		    !netif_is_lag_master(upper_dev) &&
		    !netif_is_bridge_master(upper_dev))
			return -EINVAL;
		if (!info->linking)
			break;
		/* HW limitation forbids to put ports to multiple bridges. */
		if (netif_is_bridge_master(upper_dev) &&
		    !mlxsw_sp_master_bridge_check(mlxsw_sp, upper_dev))
			return -EINVAL;
		if (netif_is_lag_master(upper_dev) &&
		    !mlxsw_sp_master_lag_check(mlxsw_sp, upper_dev,
					       info->upper_info))
			return -EINVAL;
		if (netif_is_lag_master(upper_dev) && vlan_uses_dev(dev))
			return -EINVAL;
		if (netif_is_lag_port(dev) && is_vlan_dev(upper_dev) &&
		    !netif_is_lag_master(vlan_dev_real_dev(upper_dev)))
			return -EINVAL;
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (is_vlan_dev(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_vlan_link(mlxsw_sp_port,
							      upper_dev);
			else
				 mlxsw_sp_port_vlan_unlink(mlxsw_sp_port,
							   upper_dev);
		} else if (netif_is_bridge_master(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_bridge_join(mlxsw_sp_port,
								upper_dev);
			else
				mlxsw_sp_port_bridge_leave(mlxsw_sp_port);
		} else if (netif_is_lag_master(upper_dev)) {
			if (info->linking)
				err = mlxsw_sp_port_lag_join(mlxsw_sp_port,
							     upper_dev);
			else
				mlxsw_sp_port_lag_leave(mlxsw_sp_port,
							upper_dev);
		} else {
			err = -EINVAL;
			WARN_ON(1);
		}
		break;
	}

	return err;
}

static int mlxsw_sp_netdevice_port_lower_event(struct net_device *dev,
					       unsigned long event, void *ptr)
{
	struct netdev_notifier_changelowerstate_info *info;
	struct mlxsw_sp_port *mlxsw_sp_port;
	int err;

	mlxsw_sp_port = netdev_priv(dev);
	info = ptr;

	switch (event) {
	case NETDEV_CHANGELOWERSTATE:
		if (netif_is_lag_port(dev) && mlxsw_sp_port->lagged) {
			err = mlxsw_sp_port_lag_changed(mlxsw_sp_port,
							info->lower_state_info);
			if (err)
				netdev_err(dev, "Failed to reflect link aggregation lower state change\n");
		}
		break;
	}

	return 0;
}

static int mlxsw_sp_netdevice_port_event(struct net_device *dev,
					 unsigned long event, void *ptr)
{
	switch (event) {
	case NETDEV_PRECHANGEUPPER:
	case NETDEV_CHANGEUPPER:
		return mlxsw_sp_netdevice_port_upper_event(dev, event, ptr);
	case NETDEV_CHANGELOWERSTATE:
		return mlxsw_sp_netdevice_port_lower_event(dev, event, ptr);
	}

	return 0;
}

static int mlxsw_sp_netdevice_lag_event(struct net_device *lag_dev,
					unsigned long event, void *ptr)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_port_event(dev, event, ptr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int mlxsw_sp_master_bridge_vlan_link(struct mlxsw_sp *mlxsw_sp,
					    struct net_device *vlan_dev)
{
	u16 fid = vlan_dev_vlan_id(vlan_dev);
	struct mlxsw_sp_fid *f;

	f = mlxsw_sp_fid_find(mlxsw_sp, fid);
	if (!f) {
		f = mlxsw_sp_fid_create(mlxsw_sp, fid);
		if (IS_ERR(f))
			return PTR_ERR(f);
	}

	f->ref_count++;

	return 0;
}

static void mlxsw_sp_master_bridge_vlan_unlink(struct mlxsw_sp *mlxsw_sp,
					       struct net_device *vlan_dev)
{
	u16 fid = vlan_dev_vlan_id(vlan_dev);
	struct mlxsw_sp_fid *f;

	f = mlxsw_sp_fid_find(mlxsw_sp, fid);
	if (f && f->r)
		mlxsw_sp_rif_bridge_destroy(mlxsw_sp, f->r);
	if (f && --f->ref_count == 0)
		mlxsw_sp_fid_destroy(mlxsw_sp, f);
}

static int mlxsw_sp_netdevice_bridge_event(struct net_device *br_dev,
					   unsigned long event, void *ptr)
{
	struct netdev_notifier_changeupper_info *info;
	struct net_device *upper_dev;
	struct mlxsw_sp *mlxsw_sp;
	int err;

	mlxsw_sp = mlxsw_sp_lower_get(br_dev);
	if (!mlxsw_sp)
		return 0;
	if (br_dev != mlxsw_sp->master_bridge.dev)
		return 0;

	info = ptr;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!is_vlan_dev(upper_dev))
			break;
		if (info->linking) {
			err = mlxsw_sp_master_bridge_vlan_link(mlxsw_sp,
							       upper_dev);
			if (err)
				return err;
		} else {
			mlxsw_sp_master_bridge_vlan_unlink(mlxsw_sp, upper_dev);
		}
		break;
	}

	return 0;
}

static u16 mlxsw_sp_avail_vfid_get(const struct mlxsw_sp *mlxsw_sp)
{
	return find_first_zero_bit(mlxsw_sp->vfids.mapped,
				   MLXSW_SP_VFID_MAX);
}

static int mlxsw_sp_vfid_op(struct mlxsw_sp *mlxsw_sp, u16 fid, bool create)
{
	char sfmr_pl[MLXSW_REG_SFMR_LEN];

	mlxsw_reg_sfmr_pack(sfmr_pl, !create, fid, 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfmr), sfmr_pl);
}

static void mlxsw_sp_vport_vfid_leave(struct mlxsw_sp_port *mlxsw_sp_vport);

static struct mlxsw_sp_fid *mlxsw_sp_vfid_create(struct mlxsw_sp *mlxsw_sp,
						 struct net_device *br_dev)
{
	struct device *dev = mlxsw_sp->bus_info->dev;
	struct mlxsw_sp_fid *f;
	u16 vfid, fid;
	int err;

	vfid = mlxsw_sp_avail_vfid_get(mlxsw_sp);
	if (vfid == MLXSW_SP_VFID_MAX) {
		dev_err(dev, "No available vFIDs\n");
		return ERR_PTR(-ERANGE);
	}

	fid = mlxsw_sp_vfid_to_fid(vfid);
	err = mlxsw_sp_vfid_op(mlxsw_sp, fid, true);
	if (err) {
		dev_err(dev, "Failed to create FID=%d\n", fid);
		return ERR_PTR(err);
	}

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		goto err_allocate_vfid;

	f->leave = mlxsw_sp_vport_vfid_leave;
	f->fid = fid;
	f->dev = br_dev;

	list_add(&f->list, &mlxsw_sp->vfids.list);
	set_bit(vfid, mlxsw_sp->vfids.mapped);

	return f;

err_allocate_vfid:
	mlxsw_sp_vfid_op(mlxsw_sp, fid, false);
	return ERR_PTR(-ENOMEM);
}

static void mlxsw_sp_vfid_destroy(struct mlxsw_sp *mlxsw_sp,
				  struct mlxsw_sp_fid *f)
{
	u16 vfid = mlxsw_sp_fid_to_vfid(f->fid);
	u16 fid = f->fid;

	clear_bit(vfid, mlxsw_sp->vfids.mapped);
	list_del(&f->list);

	if (f->r)
		mlxsw_sp_rif_bridge_destroy(mlxsw_sp, f->r);

	kfree(f);

	mlxsw_sp_vfid_op(mlxsw_sp, fid, false);
}

static int mlxsw_sp_vport_fid_map(struct mlxsw_sp_port *mlxsw_sp_vport, u16 fid,
				  bool valid)
{
	enum mlxsw_reg_svfa_mt mt = MLXSW_REG_SVFA_MT_PORT_VID_TO_FID;
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);

	return mlxsw_sp_port_vid_to_fid_set(mlxsw_sp_vport, mt, valid, fid,
					    vid);
}

static int mlxsw_sp_vport_vfid_join(struct mlxsw_sp_port *mlxsw_sp_vport,
				    struct net_device *br_dev)
{
	struct mlxsw_sp_fid *f;
	int err;

	f = mlxsw_sp_vfid_find(mlxsw_sp_vport->mlxsw_sp, br_dev);
	if (!f) {
		f = mlxsw_sp_vfid_create(mlxsw_sp_vport->mlxsw_sp, br_dev);
		if (IS_ERR(f))
			return PTR_ERR(f);
	}

	err = mlxsw_sp_vport_flood_set(mlxsw_sp_vport, f->fid, true);
	if (err)
		goto err_vport_flood_set;

	err = mlxsw_sp_vport_fid_map(mlxsw_sp_vport, f->fid, true);
	if (err)
		goto err_vport_fid_map;

	mlxsw_sp_vport_fid_set(mlxsw_sp_vport, f);
	f->ref_count++;

	netdev_dbg(mlxsw_sp_vport->dev, "Joined FID=%d\n", f->fid);

	return 0;

err_vport_fid_map:
	mlxsw_sp_vport_flood_set(mlxsw_sp_vport, f->fid, false);
err_vport_flood_set:
	if (!f->ref_count)
		mlxsw_sp_vfid_destroy(mlxsw_sp_vport->mlxsw_sp, f);
	return err;
}

static void mlxsw_sp_vport_vfid_leave(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	struct mlxsw_sp_fid *f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);

	netdev_dbg(mlxsw_sp_vport->dev, "Left FID=%d\n", f->fid);

	mlxsw_sp_vport_fid_map(mlxsw_sp_vport, f->fid, false);

	mlxsw_sp_vport_flood_set(mlxsw_sp_vport, f->fid, false);

	mlxsw_sp_port_fdb_flush(mlxsw_sp_vport, f->fid);

	mlxsw_sp_vport_fid_set(mlxsw_sp_vport, NULL);
	if (--f->ref_count == 0)
		mlxsw_sp_vfid_destroy(mlxsw_sp_vport->mlxsw_sp, f);
}

static int mlxsw_sp_vport_bridge_join(struct mlxsw_sp_port *mlxsw_sp_vport,
				      struct net_device *br_dev)
{
	struct mlxsw_sp_fid *f = mlxsw_sp_vport_fid_get(mlxsw_sp_vport);
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);
	struct net_device *dev = mlxsw_sp_vport->dev;
	int err;

	if (f && !WARN_ON(!f->leave))
		f->leave(mlxsw_sp_vport);

	err = mlxsw_sp_vport_vfid_join(mlxsw_sp_vport, br_dev);
	if (err) {
		netdev_err(dev, "Failed to join vFID\n");
		return err;
	}

	err = mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, true);
	if (err) {
		netdev_err(dev, "Failed to enable learning\n");
		goto err_port_vid_learning_set;
	}

	mlxsw_sp_vport->learning = 1;
	mlxsw_sp_vport->learning_sync = 1;
	mlxsw_sp_vport->uc_flood = 1;
	mlxsw_sp_vport->bridged = 1;

	return 0;

err_port_vid_learning_set:
	mlxsw_sp_vport_vfid_leave(mlxsw_sp_vport);
	return err;
}

static void mlxsw_sp_vport_bridge_leave(struct mlxsw_sp_port *mlxsw_sp_vport)
{
	u16 vid = mlxsw_sp_vport_vid_get(mlxsw_sp_vport);

	mlxsw_sp_port_vid_learning_set(mlxsw_sp_vport, vid, false);

	mlxsw_sp_vport_vfid_leave(mlxsw_sp_vport);

	mlxsw_sp_vport->learning = 0;
	mlxsw_sp_vport->learning_sync = 0;
	mlxsw_sp_vport->uc_flood = 0;
	mlxsw_sp_vport->bridged = 0;
}

static bool
mlxsw_sp_port_master_bridge_check(const struct mlxsw_sp_port *mlxsw_sp_port,
				  const struct net_device *br_dev)
{
	struct mlxsw_sp_port *mlxsw_sp_vport;

	list_for_each_entry(mlxsw_sp_vport, &mlxsw_sp_port->vports_list,
			    vport.list) {
		struct net_device *dev = mlxsw_sp_vport_dev_get(mlxsw_sp_vport);

		if (dev && dev == br_dev)
			return false;
	}

	return true;
}

static int mlxsw_sp_netdevice_vport_event(struct net_device *dev,
					  unsigned long event, void *ptr,
					  u16 vid)
{
	struct mlxsw_sp_port *mlxsw_sp_port = netdev_priv(dev);
	struct netdev_notifier_changeupper_info *info = ptr;
	struct mlxsw_sp_port *mlxsw_sp_vport;
	struct net_device *upper_dev;
	int err = 0;

	mlxsw_sp_vport = mlxsw_sp_port_vport_find(mlxsw_sp_port, vid);

	switch (event) {
	case NETDEV_PRECHANGEUPPER:
		upper_dev = info->upper_dev;
		if (!netif_is_bridge_master(upper_dev))
			return -EINVAL;
		if (!info->linking)
			break;
		/* We can't have multiple VLAN interfaces configured on
		 * the same port and being members in the same bridge.
		 */
		if (!mlxsw_sp_port_master_bridge_check(mlxsw_sp_port,
						       upper_dev))
			return -EINVAL;
		break;
	case NETDEV_CHANGEUPPER:
		upper_dev = info->upper_dev;
		if (info->linking) {
			if (WARN_ON(!mlxsw_sp_vport))
				return -EINVAL;
			err = mlxsw_sp_vport_bridge_join(mlxsw_sp_vport,
							 upper_dev);
		} else {
			if (!mlxsw_sp_vport)
				return 0;
			mlxsw_sp_vport_bridge_leave(mlxsw_sp_vport);
		}
	}

	return err;
}

static int mlxsw_sp_netdevice_lag_vport_event(struct net_device *lag_dev,
					      unsigned long event, void *ptr,
					      u16 vid)
{
	struct net_device *dev;
	struct list_head *iter;
	int ret;

	netdev_for_each_lower_dev(lag_dev, dev, iter) {
		if (mlxsw_sp_port_dev_check(dev)) {
			ret = mlxsw_sp_netdevice_vport_event(dev, event, ptr,
							     vid);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int mlxsw_sp_netdevice_vlan_event(struct net_device *vlan_dev,
					 unsigned long event, void *ptr)
{
	struct net_device *real_dev = vlan_dev_real_dev(vlan_dev);
	u16 vid = vlan_dev_vlan_id(vlan_dev);

	if (mlxsw_sp_port_dev_check(real_dev))
		return mlxsw_sp_netdevice_vport_event(real_dev, event, ptr,
						      vid);
	else if (netif_is_lag_master(real_dev))
		return mlxsw_sp_netdevice_lag_vport_event(real_dev, event, ptr,
							  vid);

	return 0;
}

static int mlxsw_sp_netdevice_event(struct notifier_block *unused,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int err = 0;

	if (event == NETDEV_CHANGEADDR || event == NETDEV_CHANGEMTU)
		err = mlxsw_sp_netdevice_router_port_event(dev);
	else if (mlxsw_sp_port_dev_check(dev))
		err = mlxsw_sp_netdevice_port_event(dev, event, ptr);
	else if (netif_is_lag_master(dev))
		err = mlxsw_sp_netdevice_lag_event(dev, event, ptr);
	else if (netif_is_bridge_master(dev))
		err = mlxsw_sp_netdevice_bridge_event(dev, event, ptr);
	else if (is_vlan_dev(dev))
		err = mlxsw_sp_netdevice_vlan_event(dev, event, ptr);

	return notifier_from_errno(err);
}

static struct notifier_block mlxsw_sp_netdevice_nb __read_mostly = {
	.notifier_call = mlxsw_sp_netdevice_event,
};

static struct notifier_block mlxsw_sp_inetaddr_nb __read_mostly = {
	.notifier_call = mlxsw_sp_inetaddr_event,
	.priority = 10,	/* Must be called before FIB notifier block */
};

static struct notifier_block mlxsw_sp_router_netevent_nb __read_mostly = {
	.notifier_call = mlxsw_sp_router_netevent_event,
};

static int __init mlxsw_sp_module_init(void)
{
	int err;

	register_netdevice_notifier(&mlxsw_sp_netdevice_nb);
	register_inetaddr_notifier(&mlxsw_sp_inetaddr_nb);
	register_netevent_notifier(&mlxsw_sp_router_netevent_nb);

	err = mlxsw_core_driver_register(&mlxsw_sp_driver);
	if (err)
		goto err_core_driver_register;
	return 0;

err_core_driver_register:
	unregister_netevent_notifier(&mlxsw_sp_router_netevent_nb);
	unregister_inetaddr_notifier(&mlxsw_sp_inetaddr_nb);
	unregister_netdevice_notifier(&mlxsw_sp_netdevice_nb);
	return err;
}

static void __exit mlxsw_sp_module_exit(void)
{
	mlxsw_core_driver_unregister(&mlxsw_sp_driver);
	unregister_netevent_notifier(&mlxsw_sp_router_netevent_nb);
	unregister_inetaddr_notifier(&mlxsw_sp_inetaddr_nb);
	unregister_netdevice_notifier(&mlxsw_sp_netdevice_nb);
}

module_init(mlxsw_sp_module_init);
module_exit(mlxsw_sp_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Mellanox Spectrum driver");
MODULE_MLXSW_DRIVER_ALIAS(MLXSW_DEVICE_KIND_SPECTRUM);
