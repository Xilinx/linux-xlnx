// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx TSN Switch Device support driver
 *
 * Copyright (c) 2022 Xilinx, Inc. All rights reserved.
 */
#ifdef CONFIG_XILINX_TSN_SWITCH
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/if_bridge.h>
#include <net/switchdev.h>
#include "xilinx_axienet.h"
#include "xilinx_tsn_switch.h"

#define tsn_to_linux_sw_state(s) \
	(((s) == BR_STATE_DISABLED)   ? TSN_SW_STATE_DISABLED : \
	 ((s) == BR_STATE_BLOCKING)   ? TSN_SW_STATE_BLOCKING : \
	 ((s) == BR_STATE_LISTENING)  ? TSN_SW_STATE_LISTENING : \
	 ((s) == BR_STATE_LEARNING)   ? TSN_SW_STATE_LEARNING : \
	 ((s) == BR_STATE_FORWARDING) ? TSN_SW_STATE_FORWARDING : \
	 TSN_SW_STATE_DISABLED)

#define stp_state_string(s) \
	(((s) == BR_STATE_DISABLED)   ? "disabled" : \
	 ((s) == BR_STATE_BLOCKING)   ? "blocking" : \
	 ((s) == BR_STATE_LISTENING)  ? "listening" : \
	 ((s) == BR_STATE_LEARNING)   ? "learning" : \
	 ((s) == BR_STATE_FORWARDING) ? "forwarding" : \
	 "und_blocked")

#define TSN_SW_STATE_DISABLED		0
#define TSN_SW_STATE_BLOCKING		1
#define TSN_SW_STATE_LISTENING		2
#define TSN_SW_STATE_LEARNING		3
#define TSN_SW_STATE_FORWARDING		4
#define TSN_SW_STATE_FLUSH		5

static struct workqueue_struct *xlnx_sw_owq;
static int
xlnx_switchdev_port_attr_set_event(struct net_device *netdev,
				   struct switchdev_notifier_port_attr_info *port_attr_info);

static int xlnx_switch_fdb_set(struct axienet_local *lp,
			       struct switchdev_notifier_fdb_info *fdb_info,
			       bool adding)
{
	struct cam_struct data;

	memset(&data, 0, sizeof(struct cam_struct));
	data.fwd_port = lp->switch_prt;
	ether_addr_copy((u8 *)&data.dest_addr, fdb_info->addr);
	data.vlanid = fdb_info->vid;

	return tsn_switch_cam_set(data, adding);
}

struct xlnx_switchdev_event_work {
	struct work_struct work;
	struct switchdev_notifier_fdb_info fdb_info;
	struct axienet_local *lp;
	unsigned long event;
};

static void xlnx_sw_fdb_offload_notify(struct axienet_local *lp,
				       struct switchdev_notifier_fdb_info *recv_info)
{
	struct switchdev_notifier_fdb_info info;

	info.addr = recv_info->addr;
	info.vid = recv_info->vid;
	call_switchdev_notifiers(SWITCHDEV_FDB_OFFLOADED,
				 lp->ndev, &info.info, NULL);
}

static int xlnx_sw_port_obj_vlan_add(struct axienet_local *lp,
				     const struct switchdev_obj_port_vlan *vlan)
{
	struct port_vlan pvl;
	struct native_vlan nvl;
	bool flag_pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	u16 vid;
	int err;

	memset(&nvl, 0, sizeof(struct native_vlan));
	memset(&pvl, 0, sizeof(struct port_vlan));

	pvl.port_num = lp->switch_prt;
	nvl.port_num = lp->switch_prt;

	/* TODO deal with vlan->flags for PVID and untagged */
	vid = vlan->vid;
	if (flag_pvid) {
		nvl.vlan_id = vid;
		err = tsn_switch_pvid_add(&nvl);
	} else {
		pvl.vlan_id = vid;
		err = tsn_switch_vlan_add(&pvl, true);
	}
	if (err)
		return err;

	return 0;
}

static int xlnx_sw_port_obj_vlan_del(struct axienet_local *lp,
				     const struct switchdev_obj_port_vlan *vlan)
{
	struct port_vlan pvl;
	struct native_vlan nvl;
	u16 vid;
	int err;

	memset(&nvl, 0, sizeof(struct native_vlan));
	memset(&pvl, 0, sizeof(struct port_vlan));

	pvl.port_num = lp->switch_prt;
	nvl.port_num = lp->switch_prt;

	tsn_switch_pvid_get(&nvl);

	vid = vlan->vid;
	if (vid == nvl.vlan_id) {
		nvl.vlan_id = 1;
		err = tsn_switch_pvid_add(&nvl);
	} else {
		pvl.vlan_id = vid;
		err = tsn_switch_vlan_add(&pvl, false);
	}
	if (err)
		return err;

	return 0;
}

static int xlnx_sw_obj_add(struct net_device *ndev,
			   const struct switchdev_obj *obj)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = xlnx_sw_port_obj_vlan_add(lp,
						SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int xlnx_sw_obj_del(struct net_device *ndev,
			   const struct switchdev_obj *obj)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = xlnx_sw_port_obj_vlan_del(lp,
						SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void xlnx_switchdev_event_work(struct work_struct *work)
{
	struct xlnx_switchdev_event_work *switchdev_work =
		container_of(work, struct xlnx_switchdev_event_work, work);
	struct axienet_local *lp = switchdev_work->lp;
	struct switchdev_notifier_fdb_info *fdb_info;
	int err;

	rtnl_lock();
	switch (switchdev_work->event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		err = xlnx_switch_fdb_set(lp, fdb_info, true);
		if (err) {
			netdev_dbg(lp->ndev, "fdb add failed err=%d\n", err);
			break;
		}
		xlnx_sw_fdb_offload_notify(lp, fdb_info);
		break;
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		fdb_info = &switchdev_work->fdb_info;
		err = xlnx_switch_fdb_set(lp, fdb_info, false);
		if (err) {
			netdev_dbg(lp->ndev, "fdb add failed err=%d\n", err);
			break;
		}
		break;
	}
	rtnl_unlock();

	kfree(switchdev_work->fdb_info.addr);
	kfree(switchdev_work);
	dev_put(lp->ndev);
}

static int xlnx_switchdev_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);
	struct switchdev_notifier_fdb_info *fdb_info = ptr;
	struct xlnx_switchdev_event_work *switchdev_work;
	struct axienet_local *lp;

	lp = netdev_priv(dev);
	switchdev_work = kzalloc(sizeof(*switchdev_work), GFP_ATOMIC);
	if (!switchdev_work)
		return NOTIFY_BAD;

	if (event == SWITCHDEV_PORT_ATTR_SET)
		return xlnx_switchdev_port_attr_set_event(dev, ptr);

	INIT_WORK(&switchdev_work->work, xlnx_switchdev_event_work);
	switchdev_work->lp = lp;
	switchdev_work->event = event;

	switch (event) {
	case SWITCHDEV_FDB_ADD_TO_DEVICE:
	case SWITCHDEV_FDB_DEL_TO_DEVICE:
		memcpy(&switchdev_work->fdb_info, ptr,
		       sizeof(switchdev_work->fdb_info));
		switchdev_work->fdb_info.addr = kzalloc(ETH_ALEN, GFP_ATOMIC);
		ether_addr_copy((u8 *)switchdev_work->fdb_info.addr,
				fdb_info->addr);
		/* take a reference on the switch port dev */
		dev_hold(dev);
		break;
	default:
		kfree(switchdev_work);
		return NOTIFY_DONE;
	}

	queue_work(xlnx_sw_owq, &switchdev_work->work);

	return NOTIFY_DONE;
}

static struct notifier_block xlnx_switchdev_notifier = {
	.notifier_call = xlnx_switchdev_event,
};

static int
xlnx_switchdev_port_obj_event(unsigned long event, struct net_device *netdev,
			      struct switchdev_notifier_port_obj_info *port_obj_info)
{
	int err = -EOPNOTSUPP;

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
		err = xlnx_sw_obj_add(netdev, port_obj_info->obj);
		break;
	case SWITCHDEV_PORT_OBJ_DEL:
		err = xlnx_sw_obj_del(netdev, port_obj_info->obj);
		break;
	}

	port_obj_info->handled = true;

	return notifier_from_errno(err);
}

static int xlnx_switchdev_blocking_event(struct notifier_block *unused, unsigned long event,
					 void *ptr)
{
	struct net_device *dev = switchdev_notifier_info_to_dev(ptr);

	switch (event) {
	case SWITCHDEV_PORT_OBJ_ADD:
	case SWITCHDEV_PORT_OBJ_DEL:
		return xlnx_switchdev_port_obj_event(event, dev, ptr);
	case SWITCHDEV_PORT_ATTR_SET:
		return xlnx_switchdev_port_attr_set_event(dev, ptr);
	}

	return NOTIFY_DONE;
}

static struct notifier_block xlnx_switchdev_blocking_notifier = {
	.notifier_call = xlnx_switchdev_blocking_event,
};

static int xlnx_sw_port_attr_stp_state_set(struct axienet_local *lp, u8 state)
{
	struct port_status ps;

	ps.port_num = lp->switch_prt;
	ps.port_status = tsn_to_linux_sw_state(state);

	return tsn_switch_set_stp_state(&ps);
}

static int xlnx_sw_port_attr_pre_bridge_flags_set(struct axienet_local *lp,
						  struct switchdev_brport_flags brport_flags)
{
	if (brport_flags.mask & ~(BR_LEARNING | BR_FLOOD))
		return -EINVAL;

	return 0;
}

static int xlnx_sw_attr_set(struct net_device *ndev,
			    const struct switchdev_attr *attr)
{
	struct axienet_local *lp = netdev_priv(ndev);
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = xlnx_sw_port_attr_stp_state_set(lp,
						      attr->u.stp_state);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		pr_info("received request to SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS: %lu\n",
			attr->u.brport_flags.val);
		break;
	case SWITCHDEV_ATTR_ID_PORT_PRE_BRIDGE_FLAGS:
		err = xlnx_sw_port_attr_pre_bridge_flags_set(lp, attr->u.brport_flags);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING:
		break;
	default:
		pr_info("%s: unhandled id: %d\n", __func__, attr->id);
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int
xlnx_switchdev_port_attr_set_event(struct net_device *netdev,
				   struct switchdev_notifier_port_attr_info *port_attr_info)
{
	int err;

	err = xlnx_sw_attr_set(netdev, port_attr_info->attr);

	port_attr_info->handled = true;

	return notifier_from_errno(err);
}

int xlnx_switchdev_init(void)
{
	xlnx_sw_owq = alloc_ordered_workqueue("%s_ordered", WQ_MEM_RECLAIM,
					      "xlnx_sw");
	if (!xlnx_sw_owq)
		return -ENOMEM;
	register_switchdev_notifier(&xlnx_switchdev_notifier);
	register_switchdev_blocking_notifier(&xlnx_switchdev_blocking_notifier);

	return 0;
}

void xlnx_switchdev_remove(void)
{
	destroy_workqueue(xlnx_sw_owq);
	unregister_switchdev_notifier(&xlnx_switchdev_notifier);
	unregister_switchdev_blocking_notifier(&xlnx_switchdev_blocking_notifier);
}
#endif
