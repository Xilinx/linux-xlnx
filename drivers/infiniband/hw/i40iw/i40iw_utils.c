/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*******************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <net/netevent.h>
#include <net/neighbour.h>
#include "i40iw.h"

/**
 * i40iw_arp_table - manage arp table
 * @iwdev: iwarp device
 * @ip_addr: ip address for device
 * @mac_addr: mac address ptr
 * @action: modify, delete or add
 */
int i40iw_arp_table(struct i40iw_device *iwdev,
		    u32 *ip_addr,
		    bool ipv4,
		    u8 *mac_addr,
		    u32 action)
{
	int arp_index;
	int err;
	u32 ip[4];

	if (ipv4) {
		memset(ip, 0, sizeof(ip));
		ip[0] = *ip_addr;
	} else {
		memcpy(ip, ip_addr, sizeof(ip));
	}

	for (arp_index = 0; (u32)arp_index < iwdev->arp_table_size; arp_index++)
		if (memcmp(iwdev->arp_table[arp_index].ip_addr, ip, sizeof(ip)) == 0)
			break;
	switch (action) {
	case I40IW_ARP_ADD:
		if (arp_index != iwdev->arp_table_size)
			return -1;

		arp_index = 0;
		err = i40iw_alloc_resource(iwdev, iwdev->allocated_arps,
					   iwdev->arp_table_size,
					   (u32 *)&arp_index,
					   &iwdev->next_arp_index);

		if (err)
			return err;

		memcpy(iwdev->arp_table[arp_index].ip_addr, ip, sizeof(ip));
		ether_addr_copy(iwdev->arp_table[arp_index].mac_addr, mac_addr);
		break;
	case I40IW_ARP_RESOLVE:
		if (arp_index == iwdev->arp_table_size)
			return -1;
		break;
	case I40IW_ARP_DELETE:
		if (arp_index == iwdev->arp_table_size)
			return -1;
		memset(iwdev->arp_table[arp_index].ip_addr, 0,
		       sizeof(iwdev->arp_table[arp_index].ip_addr));
		eth_zero_addr(iwdev->arp_table[arp_index].mac_addr);
		i40iw_free_resource(iwdev, iwdev->allocated_arps, arp_index);
		break;
	default:
		return -1;
	}
	return arp_index;
}

/**
 * i40iw_wr32 - write 32 bits to hw register
 * @hw: hardware information including registers
 * @reg: register offset
 * @value: vvalue to write to register
 */
inline void i40iw_wr32(struct i40iw_hw *hw, u32 reg, u32 value)
{
	writel(value, hw->hw_addr + reg);
}

/**
 * i40iw_rd32 - read a 32 bit hw register
 * @hw: hardware information including registers
 * @reg: register offset
 *
 * Return value of register content
 */
inline u32 i40iw_rd32(struct i40iw_hw *hw, u32 reg)
{
	return readl(hw->hw_addr + reg);
}

/**
 * i40iw_inetaddr_event - system notifier for netdev events
 * @notfier: not used
 * @event: event for notifier
 * @ptr: if address
 */
int i40iw_inetaddr_event(struct notifier_block *notifier,
			 unsigned long event,
			 void *ptr)
{
	struct in_ifaddr *ifa = ptr;
	struct net_device *event_netdev = ifa->ifa_dev->dev;
	struct net_device *netdev;
	struct net_device *upper_dev;
	struct i40iw_device *iwdev;
	struct i40iw_handler *hdl;
	u32 local_ipaddr;

	hdl = i40iw_find_netdev(event_netdev);
	if (!hdl)
		return NOTIFY_DONE;

	iwdev = &hdl->device;
	netdev = iwdev->ldev->netdev;
	upper_dev = netdev_master_upper_dev_get(netdev);
	if (netdev != event_netdev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_DOWN:
		if (upper_dev)
			local_ipaddr = ntohl(
				((struct in_device *)upper_dev->ip_ptr)->ifa_list->ifa_address);
		else
			local_ipaddr = ntohl(ifa->ifa_address);
		i40iw_manage_arp_cache(iwdev,
				       netdev->dev_addr,
				       &local_ipaddr,
				       true,
				       I40IW_ARP_DELETE);
		return NOTIFY_OK;
	case NETDEV_UP:
		if (upper_dev)
			local_ipaddr = ntohl(
				((struct in_device *)upper_dev->ip_ptr)->ifa_list->ifa_address);
		else
			local_ipaddr = ntohl(ifa->ifa_address);
		i40iw_manage_arp_cache(iwdev,
				       netdev->dev_addr,
				       &local_ipaddr,
				       true,
				       I40IW_ARP_ADD);
		break;
	case NETDEV_CHANGEADDR:
		/* Add the address to the IP table */
		if (upper_dev)
			local_ipaddr = ntohl(
				((struct in_device *)upper_dev->ip_ptr)->ifa_list->ifa_address);
		else
			local_ipaddr = ntohl(ifa->ifa_address);

		i40iw_manage_arp_cache(iwdev,
				       netdev->dev_addr,
				       &local_ipaddr,
				       true,
				       I40IW_ARP_ADD);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

/**
 * i40iw_inet6addr_event - system notifier for ipv6 netdev events
 * @notfier: not used
 * @event: event for notifier
 * @ptr: if address
 */
int i40iw_inet6addr_event(struct notifier_block *notifier,
			  unsigned long event,
			  void *ptr)
{
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)ptr;
	struct net_device *event_netdev = ifa->idev->dev;
	struct net_device *netdev;
	struct i40iw_device *iwdev;
	struct i40iw_handler *hdl;
	u32 local_ipaddr6[4];

	hdl = i40iw_find_netdev(event_netdev);
	if (!hdl)
		return NOTIFY_DONE;

	iwdev = &hdl->device;
	netdev = iwdev->ldev->netdev;
	if (netdev != event_netdev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_DOWN:
		i40iw_copy_ip_ntohl(local_ipaddr6, ifa->addr.in6_u.u6_addr32);
		i40iw_manage_arp_cache(iwdev,
				       netdev->dev_addr,
				       local_ipaddr6,
				       false,
				       I40IW_ARP_DELETE);
		return NOTIFY_OK;
	case NETDEV_UP:
		/* Fall through */
	case NETDEV_CHANGEADDR:
		i40iw_copy_ip_ntohl(local_ipaddr6, ifa->addr.in6_u.u6_addr32);
		i40iw_manage_arp_cache(iwdev,
				       netdev->dev_addr,
				       local_ipaddr6,
				       false,
				       I40IW_ARP_ADD);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

/**
 * i40iw_net_event - system notifier for net events
 * @notfier: not used
 * @event: event for notifier
 * @ptr: neighbor
 */
int i40iw_net_event(struct notifier_block *notifier, unsigned long event, void *ptr)
{
	struct neighbour *neigh = ptr;
	struct i40iw_device *iwdev;
	struct i40iw_handler *iwhdl;
	__be32 *p;
	u32 local_ipaddr[4];

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		iwhdl = i40iw_find_netdev((struct net_device *)neigh->dev);
		if (!iwhdl)
			return NOTIFY_DONE;
		iwdev = &iwhdl->device;
		p = (__be32 *)neigh->primary_key;
		i40iw_copy_ip_ntohl(local_ipaddr, p);
		if (neigh->nud_state & NUD_VALID) {
			i40iw_manage_arp_cache(iwdev,
					       neigh->ha,
					       local_ipaddr,
					       false,
					       I40IW_ARP_ADD);

		} else {
			i40iw_manage_arp_cache(iwdev,
					       neigh->ha,
					       local_ipaddr,
					       false,
					       I40IW_ARP_DELETE);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

/**
 * i40iw_get_cqp_request - get cqp struct
 * @cqp: device cqp ptr
 * @wait: cqp to be used in wait mode
 */
struct i40iw_cqp_request *i40iw_get_cqp_request(struct i40iw_cqp *cqp, bool wait)
{
	struct i40iw_cqp_request *cqp_request = NULL;
	unsigned long flags;

	spin_lock_irqsave(&cqp->req_lock, flags);
	if (!list_empty(&cqp->cqp_avail_reqs)) {
		cqp_request = list_entry(cqp->cqp_avail_reqs.next,
					 struct i40iw_cqp_request, list);
		list_del_init(&cqp_request->list);
	}
	spin_unlock_irqrestore(&cqp->req_lock, flags);
	if (!cqp_request) {
		cqp_request = kzalloc(sizeof(*cqp_request), GFP_ATOMIC);
		if (cqp_request) {
			cqp_request->dynamic = true;
			INIT_LIST_HEAD(&cqp_request->list);
			init_waitqueue_head(&cqp_request->waitq);
		}
	}
	if (!cqp_request) {
		i40iw_pr_err("CQP Request Fail: No Memory");
		return NULL;
	}

	if (wait) {
		atomic_set(&cqp_request->refcount, 2);
		cqp_request->waiting = true;
	} else {
		atomic_set(&cqp_request->refcount, 1);
	}
	return cqp_request;
}

/**
 * i40iw_free_cqp_request - free cqp request
 * @cqp: cqp ptr
 * @cqp_request: to be put back in cqp list
 */
void i40iw_free_cqp_request(struct i40iw_cqp *cqp, struct i40iw_cqp_request *cqp_request)
{
	unsigned long flags;

	if (cqp_request->dynamic) {
		kfree(cqp_request);
	} else {
		cqp_request->request_done = false;
		cqp_request->callback_fcn = NULL;
		cqp_request->waiting = false;

		spin_lock_irqsave(&cqp->req_lock, flags);
		list_add_tail(&cqp_request->list, &cqp->cqp_avail_reqs);
		spin_unlock_irqrestore(&cqp->req_lock, flags);
	}
}

/**
 * i40iw_put_cqp_request - dec ref count and free if 0
 * @cqp: cqp ptr
 * @cqp_request: to be put back in cqp list
 */
void i40iw_put_cqp_request(struct i40iw_cqp *cqp,
			   struct i40iw_cqp_request *cqp_request)
{
	if (atomic_dec_and_test(&cqp_request->refcount))
		i40iw_free_cqp_request(cqp, cqp_request);
}

/**
 * i40iw_free_qp - callback after destroy cqp completes
 * @cqp_request: cqp request for destroy qp
 * @num: not used
 */
static void i40iw_free_qp(struct i40iw_cqp_request *cqp_request, u32 num)
{
	struct i40iw_sc_qp *qp = (struct i40iw_sc_qp *)cqp_request->param;
	struct i40iw_qp *iwqp = (struct i40iw_qp *)qp->back_qp;
	struct i40iw_device *iwdev;
	u32 qp_num = iwqp->ibqp.qp_num;

	iwdev = iwqp->iwdev;

	i40iw_rem_pdusecount(iwqp->iwpd, iwdev);
	i40iw_free_qp_resources(iwdev, iwqp, qp_num);
}

/**
 * i40iw_wait_event - wait for completion
 * @iwdev: iwarp device
 * @cqp_request: cqp request to wait
 */
static int i40iw_wait_event(struct i40iw_device *iwdev,
			    struct i40iw_cqp_request *cqp_request)
{
	struct cqp_commands_info *info = &cqp_request->info;
	struct i40iw_cqp *iwcqp = &iwdev->cqp;
	bool cqp_error = false;
	int err_code = 0;
	int timeout_ret = 0;

	timeout_ret = wait_event_timeout(cqp_request->waitq,
					 cqp_request->request_done,
					 I40IW_EVENT_TIMEOUT);
	if (!timeout_ret) {
		i40iw_pr_err("error cqp command 0x%x timed out ret = %d\n",
			     info->cqp_cmd, timeout_ret);
		err_code = -ETIME;
		i40iw_request_reset(iwdev);
		goto done;
	}
	cqp_error = cqp_request->compl_info.error;
	if (cqp_error) {
		i40iw_pr_err("error cqp command 0x%x completion maj = 0x%x min=0x%x\n",
			     info->cqp_cmd, cqp_request->compl_info.maj_err_code,
			     cqp_request->compl_info.min_err_code);
		err_code = -EPROTO;
		goto done;
	}
done:
	i40iw_put_cqp_request(iwcqp, cqp_request);
	return err_code;
}

/**
 * i40iw_handle_cqp_op - process cqp command
 * @iwdev: iwarp device
 * @cqp_request: cqp request to process
 */
enum i40iw_status_code i40iw_handle_cqp_op(struct i40iw_device *iwdev,
					   struct i40iw_cqp_request
					   *cqp_request)
{
	struct i40iw_sc_dev *dev = &iwdev->sc_dev;
	enum i40iw_status_code status;
	struct cqp_commands_info *info = &cqp_request->info;
	int err_code = 0;

	status = i40iw_process_cqp_cmd(dev, info);
	if (status) {
		i40iw_pr_err("error cqp command 0x%x failed\n", info->cqp_cmd);
		i40iw_free_cqp_request(&iwdev->cqp, cqp_request);
		return status;
	}
	if (cqp_request->waiting)
		err_code = i40iw_wait_event(iwdev, cqp_request);
	if (err_code)
		status = I40IW_ERR_CQP_COMPL_ERROR;
	return status;
}

/**
 * i40iw_add_pdusecount - add pd refcount
 * @iwpd: pd for refcount
 */
void i40iw_add_pdusecount(struct i40iw_pd *iwpd)
{
	atomic_inc(&iwpd->usecount);
}

/**
 * i40iw_rem_pdusecount - decrement refcount for pd and free if 0
 * @iwpd: pd for refcount
 * @iwdev: iwarp device
 */
void i40iw_rem_pdusecount(struct i40iw_pd *iwpd, struct i40iw_device *iwdev)
{
	if (!atomic_dec_and_test(&iwpd->usecount))
		return;
	i40iw_free_resource(iwdev, iwdev->allocated_pds, iwpd->sc_pd.pd_id);
	kfree(iwpd);
}

/**
 * i40iw_add_ref - add refcount for qp
 * @ibqp: iqarp qp
 */
void i40iw_add_ref(struct ib_qp *ibqp)
{
	struct i40iw_qp *iwqp = (struct i40iw_qp *)ibqp;

	atomic_inc(&iwqp->refcount);
}

/**
 * i40iw_rem_ref - rem refcount for qp and free if 0
 * @ibqp: iqarp qp
 */
void i40iw_rem_ref(struct ib_qp *ibqp)
{
	struct i40iw_qp *iwqp;
	enum i40iw_status_code status;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_device *iwdev;
	u32 qp_num;
	unsigned long flags;

	iwqp = to_iwqp(ibqp);
	iwdev = iwqp->iwdev;
	spin_lock_irqsave(&iwdev->qptable_lock, flags);
	if (!atomic_dec_and_test(&iwqp->refcount)) {
		spin_unlock_irqrestore(&iwdev->qptable_lock, flags);
		return;
	}

	qp_num = iwqp->ibqp.qp_num;
	iwdev->qp_table[qp_num] = NULL;
	spin_unlock_irqrestore(&iwdev->qptable_lock, flags);
	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, false);
	if (!cqp_request)
		return;

	cqp_request->callback_fcn = i40iw_free_qp;
	cqp_request->param = (void *)&iwqp->sc_qp;
	cqp_info = &cqp_request->info;
	cqp_info->cqp_cmd = OP_QP_DESTROY;
	cqp_info->post_sq = 1;
	cqp_info->in.u.qp_destroy.qp = &iwqp->sc_qp;
	cqp_info->in.u.qp_destroy.scratch = (uintptr_t)cqp_request;
	cqp_info->in.u.qp_destroy.remove_hash_idx = true;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Destroy QP fail");
}

/**
 * i40iw_get_qp - get qp address
 * @device: iwarp device
 * @qpn: qp number
 */
struct ib_qp *i40iw_get_qp(struct ib_device *device, int qpn)
{
	struct i40iw_device *iwdev = to_iwdev(device);

	if ((qpn < IW_FIRST_QPN) || (qpn >= iwdev->max_qp))
		return NULL;

	return &iwdev->qp_table[qpn]->ibqp;
}

/**
 * i40iw_debug_buf - print debug msg and buffer is mask set
 * @dev: hardware control device structure
 * @mask: mask to compare if to print debug buffer
 * @buf: points buffer addr
 * @size: saize of buffer to print
 */
void i40iw_debug_buf(struct i40iw_sc_dev *dev,
		     enum i40iw_debug_flag mask,
		     char *desc,
		     u64 *buf,
		     u32 size)
{
	u32 i;

	if (!(dev->debug_mask & mask))
		return;
	i40iw_debug(dev, mask, "%s\n", desc);
	i40iw_debug(dev, mask, "starting address virt=%p phy=%llxh\n", buf,
		    (unsigned long long)virt_to_phys(buf));

	for (i = 0; i < size; i += 8)
		i40iw_debug(dev, mask, "index %03d val: %016llx\n", i, buf[i / 8]);
}

/**
 * i40iw_get_hw_addr - return hw addr
 * @par: points to shared dev
 */
u8 __iomem *i40iw_get_hw_addr(void *par)
{
	struct i40iw_sc_dev *dev = (struct i40iw_sc_dev *)par;

	return dev->hw->hw_addr;
}

/**
 * i40iw_remove_head - return head entry and remove from list
 * @list: list for entry
 */
void *i40iw_remove_head(struct list_head *list)
{
	struct list_head *entry;

	if (list_empty(list))
		return NULL;

	entry = (void *)list->next;
	list_del(entry);
	return (void *)entry;
}

/**
 * i40iw_allocate_dma_mem - Memory alloc helper fn
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 * @alignment: what to align the allocation to
 */
enum i40iw_status_code i40iw_allocate_dma_mem(struct i40iw_hw *hw,
					      struct i40iw_dma_mem *mem,
					      u64 size,
					      u32 alignment)
{
	struct pci_dev *pcidev = (struct pci_dev *)hw->dev_context;

	if (!mem)
		return I40IW_ERR_PARAM;
	mem->size = ALIGN(size, alignment);
	mem->va = dma_zalloc_coherent(&pcidev->dev, mem->size,
				      (dma_addr_t *)&mem->pa, GFP_KERNEL);
	if (!mem->va)
		return I40IW_ERR_NO_MEMORY;
	return 0;
}

/**
 * i40iw_free_dma_mem - Memory free helper fn
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 */
void i40iw_free_dma_mem(struct i40iw_hw *hw, struct i40iw_dma_mem *mem)
{
	struct pci_dev *pcidev = (struct pci_dev *)hw->dev_context;

	if (!mem || !mem->va)
		return;

	dma_free_coherent(&pcidev->dev, mem->size,
			  mem->va, (dma_addr_t)mem->pa);
	mem->va = NULL;
}

/**
 * i40iw_allocate_virt_mem - virtual memory alloc helper fn
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to fill out
 * @size: size of memory requested
 */
enum i40iw_status_code i40iw_allocate_virt_mem(struct i40iw_hw *hw,
					       struct i40iw_virt_mem *mem,
					       u32 size)
{
	if (!mem)
		return I40IW_ERR_PARAM;

	mem->size = size;
	mem->va = kzalloc(size, GFP_KERNEL);

	if (mem->va)
		return 0;
	else
		return I40IW_ERR_NO_MEMORY;
}

/**
 * i40iw_free_virt_mem - virtual memory free helper fn
 * @hw:   pointer to the HW structure
 * @mem:  ptr to mem struct to free
 */
enum i40iw_status_code i40iw_free_virt_mem(struct i40iw_hw *hw,
					   struct i40iw_virt_mem *mem)
{
	if (!mem)
		return I40IW_ERR_PARAM;
	/*
	 * mem->va points to the parent of mem, so both mem and mem->va
	 * can not be touched once mem->va is freed
	 */
	kfree(mem->va);
	return 0;
}

/**
 * i40iw_cqp_sds_cmd - create cqp command for sd
 * @dev: hardware control device structure
 * @sd_info: information  for sd cqp
 *
 */
enum i40iw_status_code i40iw_cqp_sds_cmd(struct i40iw_sc_dev *dev,
					 struct i40iw_update_sds_info *sdinfo)
{
	enum i40iw_status_code status;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, true);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;
	cqp_info = &cqp_request->info;
	memcpy(&cqp_info->in.u.update_pe_sds.info, sdinfo,
	       sizeof(cqp_info->in.u.update_pe_sds.info));
	cqp_info->cqp_cmd = OP_UPDATE_PE_SDS;
	cqp_info->post_sq = 1;
	cqp_info->in.u.update_pe_sds.dev = dev;
	cqp_info->in.u.update_pe_sds.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Update SD's fail");
	return status;
}

/**
 * i40iw_term_modify_qp - modify qp for term message
 * @qp: hardware control qp
 * @next_state: qp's next state
 * @term: terminate code
 * @term_len: length
 */
void i40iw_term_modify_qp(struct i40iw_sc_qp *qp, u8 next_state, u8 term, u8 term_len)
{
	struct i40iw_qp *iwqp;

	iwqp = (struct i40iw_qp *)qp->back_qp;
	i40iw_next_iw_state(iwqp, next_state, 0, term, term_len);
};

/**
 * i40iw_terminate_done - after terminate is completed
 * @qp: hardware control qp
 * @timeout_occurred: indicates if terminate timer expired
 */
void i40iw_terminate_done(struct i40iw_sc_qp *qp, int timeout_occurred)
{
	struct i40iw_qp *iwqp;
	u32 next_iwarp_state = I40IW_QP_STATE_ERROR;
	u8 hte = 0;
	bool first_time;
	unsigned long flags;

	iwqp = (struct i40iw_qp *)qp->back_qp;
	spin_lock_irqsave(&iwqp->lock, flags);
	if (iwqp->hte_added) {
		iwqp->hte_added = 0;
		hte = 1;
	}
	first_time = !(qp->term_flags & I40IW_TERM_DONE);
	qp->term_flags |= I40IW_TERM_DONE;
	spin_unlock_irqrestore(&iwqp->lock, flags);
	if (first_time) {
		if (!timeout_occurred)
			i40iw_terminate_del_timer(qp);
		else
			next_iwarp_state = I40IW_QP_STATE_CLOSING;

		i40iw_next_iw_state(iwqp, next_iwarp_state, hte, 0, 0);
		i40iw_cm_disconn(iwqp);
	}
}

/**
 * i40iw_terminate_imeout - timeout happened
 * @context: points to iwarp qp
 */
static void i40iw_terminate_timeout(unsigned long context)
{
	struct i40iw_qp *iwqp = (struct i40iw_qp *)context;
	struct i40iw_sc_qp *qp = (struct i40iw_sc_qp *)&iwqp->sc_qp;

	i40iw_terminate_done(qp, 1);
}

/**
 * i40iw_terminate_start_timer - start terminate timeout
 * @qp: hardware control qp
 */
void i40iw_terminate_start_timer(struct i40iw_sc_qp *qp)
{
	struct i40iw_qp *iwqp;

	iwqp = (struct i40iw_qp *)qp->back_qp;
	init_timer(&iwqp->terminate_timer);
	iwqp->terminate_timer.function = i40iw_terminate_timeout;
	iwqp->terminate_timer.expires = jiffies + HZ;
	iwqp->terminate_timer.data = (unsigned long)iwqp;
	add_timer(&iwqp->terminate_timer);
}

/**
 * i40iw_terminate_del_timer - delete terminate timeout
 * @qp: hardware control qp
 */
void i40iw_terminate_del_timer(struct i40iw_sc_qp *qp)
{
	struct i40iw_qp *iwqp;

	iwqp = (struct i40iw_qp *)qp->back_qp;
	del_timer(&iwqp->terminate_timer);
}

/**
 * i40iw_cqp_generic_worker - generic worker for cqp
 * @work: work pointer
 */
static void i40iw_cqp_generic_worker(struct work_struct *work)
{
	struct i40iw_virtchnl_work_info *work_info =
	    &((struct virtchnl_work *)work)->work_info;

	if (work_info->worker_vf_dev)
		work_info->callback_fcn(work_info->worker_vf_dev);
}

/**
 * i40iw_cqp_spawn_worker - spawn worket thread
 * @iwdev: device struct pointer
 * @work_info: work request info
 * @iw_vf_idx: virtual function index
 */
void i40iw_cqp_spawn_worker(struct i40iw_sc_dev *dev,
			    struct i40iw_virtchnl_work_info *work_info,
			    u32 iw_vf_idx)
{
	struct virtchnl_work *work;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	work = &iwdev->virtchnl_w[iw_vf_idx];
	memcpy(&work->work_info, work_info, sizeof(*work_info));
	INIT_WORK(&work->work, i40iw_cqp_generic_worker);
	queue_work(iwdev->virtchnl_wq, &work->work);
}

/**
 * i40iw_cqp_manage_hmc_fcn_worker -
 * @work: work pointer for hmc info
 */
static void i40iw_cqp_manage_hmc_fcn_worker(struct work_struct *work)
{
	struct i40iw_cqp_request *cqp_request =
	    ((struct virtchnl_work *)work)->cqp_request;
	struct i40iw_ccq_cqe_info ccq_cqe_info;
	struct i40iw_hmc_fcn_info *hmcfcninfo =
			&cqp_request->info.in.u.manage_hmc_pm.info;
	struct i40iw_device *iwdev =
	    (struct i40iw_device *)cqp_request->info.in.u.manage_hmc_pm.dev->back_dev;

	ccq_cqe_info.cqp = NULL;
	ccq_cqe_info.maj_err_code = cqp_request->compl_info.maj_err_code;
	ccq_cqe_info.min_err_code = cqp_request->compl_info.min_err_code;
	ccq_cqe_info.op_code = cqp_request->compl_info.op_code;
	ccq_cqe_info.op_ret_val = cqp_request->compl_info.op_ret_val;
	ccq_cqe_info.scratch = 0;
	ccq_cqe_info.error = cqp_request->compl_info.error;
	hmcfcninfo->callback_fcn(cqp_request->info.in.u.manage_hmc_pm.dev,
				 hmcfcninfo->cqp_callback_param, &ccq_cqe_info);
	i40iw_put_cqp_request(&iwdev->cqp, cqp_request);
}

/**
 * i40iw_cqp_manage_hmc_fcn_callback - called function after cqp completion
 * @cqp_request: cqp request info struct for hmc fun
 * @unused: unused param of callback
 */
static void i40iw_cqp_manage_hmc_fcn_callback(struct i40iw_cqp_request *cqp_request,
					      u32 unused)
{
	struct virtchnl_work *work;
	struct i40iw_hmc_fcn_info *hmcfcninfo =
	    &cqp_request->info.in.u.manage_hmc_pm.info;
	struct i40iw_device *iwdev =
	    (struct i40iw_device *)cqp_request->info.in.u.manage_hmc_pm.dev->
	    back_dev;

	if (hmcfcninfo && hmcfcninfo->callback_fcn) {
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_HMC, "%s1\n", __func__);
		atomic_inc(&cqp_request->refcount);
		work = &iwdev->virtchnl_w[hmcfcninfo->iw_vf_idx];
		work->cqp_request = cqp_request;
		INIT_WORK(&work->work, i40iw_cqp_manage_hmc_fcn_worker);
		queue_work(iwdev->virtchnl_wq, &work->work);
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_HMC, "%s2\n", __func__);
	} else {
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_HMC, "%s: Something wrong\n", __func__);
	}
}

/**
 * i40iw_cqp_manage_hmc_fcn_cmd - issue cqp command to manage hmc
 * @dev: hardware control device structure
 * @hmcfcninfo: info for hmc
 */
enum i40iw_status_code i40iw_cqp_manage_hmc_fcn_cmd(struct i40iw_sc_dev *dev,
						    struct i40iw_hmc_fcn_info *hmcfcninfo)
{
	enum i40iw_status_code status;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_HMC, "%s\n", __func__);
	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, false);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;
	cqp_info = &cqp_request->info;
	cqp_request->callback_fcn = i40iw_cqp_manage_hmc_fcn_callback;
	cqp_request->param = hmcfcninfo;
	memcpy(&cqp_info->in.u.manage_hmc_pm.info, hmcfcninfo,
	       sizeof(*hmcfcninfo));
	cqp_info->in.u.manage_hmc_pm.dev = dev;
	cqp_info->cqp_cmd = OP_MANAGE_HMC_PM_FUNC_TABLE;
	cqp_info->post_sq = 1;
	cqp_info->in.u.manage_hmc_pm.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Manage HMC fail");
	return status;
}

/**
 * i40iw_cqp_query_fpm_values_cmd - send cqp command for fpm
 * @iwdev: function device struct
 * @values_mem: buffer for fpm
 * @hmc_fn_id: function id for fpm
 */
enum i40iw_status_code i40iw_cqp_query_fpm_values_cmd(struct i40iw_sc_dev *dev,
						      struct i40iw_dma_mem *values_mem,
						      u8 hmc_fn_id)
{
	enum i40iw_status_code status;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, true);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;
	cqp_info = &cqp_request->info;
	cqp_request->param = NULL;
	cqp_info->in.u.query_fpm_values.cqp = dev->cqp;
	cqp_info->in.u.query_fpm_values.fpm_values_pa = values_mem->pa;
	cqp_info->in.u.query_fpm_values.fpm_values_va = values_mem->va;
	cqp_info->in.u.query_fpm_values.hmc_fn_id = hmc_fn_id;
	cqp_info->cqp_cmd = OP_QUERY_FPM_VALUES;
	cqp_info->post_sq = 1;
	cqp_info->in.u.query_fpm_values.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Query FPM fail");
	return status;
}

/**
 * i40iw_cqp_commit_fpm_values_cmd - commit fpm values in hw
 * @dev: hardware control device structure
 * @values_mem: buffer with fpm values
 * @hmc_fn_id: function id for fpm
 */
enum i40iw_status_code i40iw_cqp_commit_fpm_values_cmd(struct i40iw_sc_dev *dev,
						       struct i40iw_dma_mem *values_mem,
						       u8 hmc_fn_id)
{
	enum i40iw_status_code status;
	struct i40iw_cqp_request *cqp_request;
	struct cqp_commands_info *cqp_info;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	cqp_request = i40iw_get_cqp_request(&iwdev->cqp, true);
	if (!cqp_request)
		return I40IW_ERR_NO_MEMORY;
	cqp_info = &cqp_request->info;
	cqp_request->param = NULL;
	cqp_info->in.u.commit_fpm_values.cqp = dev->cqp;
	cqp_info->in.u.commit_fpm_values.fpm_values_pa = values_mem->pa;
	cqp_info->in.u.commit_fpm_values.fpm_values_va = values_mem->va;
	cqp_info->in.u.commit_fpm_values.hmc_fn_id = hmc_fn_id;
	cqp_info->cqp_cmd = OP_COMMIT_FPM_VALUES;
	cqp_info->post_sq = 1;
	cqp_info->in.u.commit_fpm_values.scratch = (uintptr_t)cqp_request;
	status = i40iw_handle_cqp_op(iwdev, cqp_request);
	if (status)
		i40iw_pr_err("CQP-OP Commit FPM fail");
	return status;
}

/**
 * i40iw_vf_wait_vchnl_resp - wait for channel msg
 * @iwdev: function's device struct
 */
enum i40iw_status_code i40iw_vf_wait_vchnl_resp(struct i40iw_sc_dev *dev)
{
	struct i40iw_device *iwdev = dev->back_dev;
	int timeout_ret;

	i40iw_debug(dev, I40IW_DEBUG_VIRT, "%s[%u] dev %p, iwdev %p\n",
		    __func__, __LINE__, dev, iwdev);

	atomic_set(&iwdev->vchnl_msgs, 2);
	timeout_ret = wait_event_timeout(iwdev->vchnl_waitq,
					 (atomic_read(&iwdev->vchnl_msgs) == 1),
					 I40IW_VCHNL_EVENT_TIMEOUT);
	atomic_dec(&iwdev->vchnl_msgs);
	if (!timeout_ret) {
		i40iw_pr_err("virt channel completion timeout = 0x%x\n", timeout_ret);
		atomic_set(&iwdev->vchnl_msgs, 0);
		dev->vchnl_up = false;
		return I40IW_ERR_TIMEOUT;
	}
	wake_up(&dev->vf_reqs);
	return 0;
}

/**
 * i40iw_ieq_mpa_crc_ae - generate AE for crc error
 * @dev: hardware control device structure
 * @qp: hardware control qp
 */
void i40iw_ieq_mpa_crc_ae(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp)
{
	struct i40iw_qp_flush_info info;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	i40iw_debug(dev, I40IW_DEBUG_AEQ, "%s entered\n", __func__);
	memset(&info, 0, sizeof(info));
	info.ae_code = I40IW_AE_LLP_RECEIVED_MPA_CRC_ERROR;
	info.generate_ae = true;
	info.ae_source = 0x3;
	(void)i40iw_hw_flush_wqes(iwdev, qp, &info, false);
}

/**
 * i40iw_init_hash_desc - initialize hash for crc calculation
 * @desc: cryption type
 */
enum i40iw_status_code i40iw_init_hash_desc(struct shash_desc **desc)
{
	struct crypto_shash *tfm;
	struct shash_desc *tdesc;

	tfm = crypto_alloc_shash("crc32c", 0, 0);
	if (IS_ERR(tfm))
		return I40IW_ERR_MPA_CRC;

	tdesc = kzalloc(sizeof(*tdesc) + crypto_shash_descsize(tfm),
			GFP_KERNEL);
	if (!tdesc) {
		crypto_free_shash(tfm);
		return I40IW_ERR_MPA_CRC;
	}
	tdesc->tfm = tfm;
	*desc = tdesc;

	return 0;
}

/**
 * i40iw_free_hash_desc - free hash desc
 * @desc: to be freed
 */
void i40iw_free_hash_desc(struct shash_desc *desc)
{
	if (desc) {
		crypto_free_shash(desc->tfm);
		kfree(desc);
	}
}

/**
 * i40iw_alloc_query_fpm_buf - allocate buffer for fpm
 * @dev: hardware control device structure
 * @mem: buffer ptr for fpm to be allocated
 * @return: memory allocation status
 */
enum i40iw_status_code i40iw_alloc_query_fpm_buf(struct i40iw_sc_dev *dev,
						 struct i40iw_dma_mem *mem)
{
	enum i40iw_status_code status;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;

	status = i40iw_obj_aligned_mem(iwdev, mem, I40IW_QUERY_FPM_BUF_SIZE,
				       I40IW_FPM_QUERY_BUF_ALIGNMENT_MASK);
	return status;
}

/**
 * i40iw_ieq_check_mpacrc - check if mpa crc is OK
 * @desc: desc for hash
 * @addr: address of buffer for crc
 * @length: length of buffer
 * @value: value to be compared
 */
enum i40iw_status_code i40iw_ieq_check_mpacrc(struct shash_desc *desc,
					      void *addr,
					      u32 length,
					      u32 value)
{
	u32 crc = 0;
	int ret;
	enum i40iw_status_code ret_code = 0;

	crypto_shash_init(desc);
	ret = crypto_shash_update(desc, addr, length);
	if (!ret)
		crypto_shash_final(desc, (u8 *)&crc);
	if (crc != value) {
		i40iw_pr_err("mpa crc check fail\n");
		ret_code = I40IW_ERR_MPA_CRC;
	}
	return ret_code;
}

/**
 * i40iw_ieq_get_qp - get qp based on quad in puda buffer
 * @dev: hardware control device structure
 * @buf: receive puda buffer on exception q
 */
struct i40iw_sc_qp *i40iw_ieq_get_qp(struct i40iw_sc_dev *dev,
				     struct i40iw_puda_buf *buf)
{
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;
	struct i40iw_qp *iwqp;
	struct i40iw_cm_node *cm_node;
	u32 loc_addr[4], rem_addr[4];
	u16 loc_port, rem_port;
	struct ipv6hdr *ip6h;
	struct iphdr *iph = (struct iphdr *)buf->iph;
	struct tcphdr *tcph = (struct tcphdr *)buf->tcph;

	if (iph->version == 4) {
		memset(loc_addr, 0, sizeof(loc_addr));
		loc_addr[0] = ntohl(iph->daddr);
		memset(rem_addr, 0, sizeof(rem_addr));
		rem_addr[0] = ntohl(iph->saddr);
	} else {
		ip6h = (struct ipv6hdr *)buf->iph;
		i40iw_copy_ip_ntohl(loc_addr, ip6h->daddr.in6_u.u6_addr32);
		i40iw_copy_ip_ntohl(rem_addr, ip6h->saddr.in6_u.u6_addr32);
	}
	loc_port = ntohs(tcph->dest);
	rem_port = ntohs(tcph->source);

	cm_node = i40iw_find_node(&iwdev->cm_core, rem_port, rem_addr, loc_port,
				  loc_addr, false);
	if (!cm_node)
		return NULL;
	iwqp = cm_node->iwqp;
	return &iwqp->sc_qp;
}

/**
 * i40iw_ieq_update_tcpip_info - update tcpip in the buffer
 * @buf: puda to update
 * @length: length of buffer
 * @seqnum: seq number for tcp
 */
void i40iw_ieq_update_tcpip_info(struct i40iw_puda_buf *buf, u16 length, u32 seqnum)
{
	struct tcphdr *tcph;
	struct iphdr *iph;
	u16 iphlen;
	u16 packetsize;
	u8 *addr = (u8 *)buf->mem.va;

	iphlen = (buf->ipv4) ? 20 : 40;
	iph = (struct iphdr *)(addr + buf->maclen);
	tcph = (struct tcphdr *)(addr + buf->maclen + iphlen);
	packetsize = length + buf->tcphlen + iphlen;

	iph->tot_len = htons(packetsize);
	tcph->seq = htonl(seqnum);
}

/**
 * i40iw_puda_get_tcpip_info - get tcpip info from puda buffer
 * @info: to get information
 * @buf: puda buffer
 */
enum i40iw_status_code i40iw_puda_get_tcpip_info(struct i40iw_puda_completion_info *info,
						 struct i40iw_puda_buf *buf)
{
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct tcphdr *tcph;
	u16 iphlen;
	u16 pkt_len;
	u8 *mem = (u8 *)buf->mem.va;
	struct ethhdr *ethh = (struct ethhdr *)buf->mem.va;

	if (ethh->h_proto == htons(0x8100)) {
		info->vlan_valid = true;
		buf->vlan_id = ntohs(((struct vlan_ethhdr *)ethh)->h_vlan_TCI) & VLAN_VID_MASK;
	}
	buf->maclen = (info->vlan_valid) ? 18 : 14;
	iphlen = (info->l3proto) ? 40 : 20;
	buf->ipv4 = (info->l3proto) ? false : true;
	buf->iph = mem + buf->maclen;
	iph = (struct iphdr *)buf->iph;

	buf->tcph = buf->iph + iphlen;
	tcph = (struct tcphdr *)buf->tcph;

	if (buf->ipv4) {
		pkt_len = ntohs(iph->tot_len);
	} else {
		ip6h = (struct ipv6hdr *)buf->iph;
		pkt_len = ntohs(ip6h->payload_len) + iphlen;
	}

	buf->totallen = pkt_len + buf->maclen;

	if (info->payload_len < buf->totallen - 4) {
		i40iw_pr_err("payload_len = 0x%x totallen expected0x%x\n",
			     info->payload_len, buf->totallen);
		return I40IW_ERR_INVALID_SIZE;
	}

	buf->tcphlen = (tcph->doff) << 2;
	buf->datalen = pkt_len - iphlen - buf->tcphlen;
	buf->data = (buf->datalen) ? buf->tcph + buf->tcphlen : NULL;
	buf->hdrlen = buf->maclen + iphlen + buf->tcphlen;
	buf->seqnum = ntohl(tcph->seq);
	return 0;
}

/**
 * i40iw_hw_stats_timeout - Stats timer-handler which updates all HW stats
 * @dev: hardware control device structure
 */
static void i40iw_hw_stats_timeout(unsigned long dev)
{
	struct i40iw_sc_dev *pf_dev = (struct i40iw_sc_dev *)dev;
	struct i40iw_dev_pestat *pf_devstat = &pf_dev->dev_pestat;
	struct i40iw_dev_pestat *vf_devstat = NULL;
	u16 iw_vf_idx;
	unsigned long flags;

	/*PF*/
	pf_devstat->ops.iw_hw_stat_read_all(pf_devstat, &pf_devstat->hw_stats);
	for (iw_vf_idx = 0; iw_vf_idx < I40IW_MAX_PE_ENABLED_VF_COUNT; iw_vf_idx++) {
		spin_lock_irqsave(&pf_devstat->stats_lock, flags);
		if (pf_dev->vf_dev[iw_vf_idx]) {
			if (pf_dev->vf_dev[iw_vf_idx]->stats_initialized) {
				vf_devstat = &pf_dev->vf_dev[iw_vf_idx]->dev_pestat;
				vf_devstat->ops.iw_hw_stat_read_all(vf_devstat, &vf_devstat->hw_stats);
			}
		}
		spin_unlock_irqrestore(&pf_devstat->stats_lock, flags);
	}

	mod_timer(&pf_devstat->stats_timer,
		  jiffies + msecs_to_jiffies(STATS_TIMER_DELAY));
}

/**
 * i40iw_hw_stats_start_timer - Start periodic stats timer
 * @dev: hardware control device structure
 */
void i40iw_hw_stats_start_timer(struct i40iw_sc_dev *dev)
{
	struct i40iw_dev_pestat *devstat = &dev->dev_pestat;

	init_timer(&devstat->stats_timer);
	devstat->stats_timer.function = i40iw_hw_stats_timeout;
	devstat->stats_timer.data = (unsigned long)dev;
	mod_timer(&devstat->stats_timer,
		  jiffies + msecs_to_jiffies(STATS_TIMER_DELAY));
}

/**
 * i40iw_hw_stats_del_timer - Delete periodic stats timer
 * @dev: hardware control device structure
 */
void i40iw_hw_stats_del_timer(struct i40iw_sc_dev *dev)
{
	struct i40iw_dev_pestat *devstat = &dev->dev_pestat;

	del_timer_sync(&devstat->stats_timer);
}
