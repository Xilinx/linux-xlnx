// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 * Author : Sandeep Dhanvada <sandeep.dhanvada@xilinx.com>
 *        : Anjaneyulu Reddy Mule <anjaneyulu.reddy.mule@xilinx.com>
 *        : Srija Malyala <srija.malyala@xilinx.com>
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/inet.h>
#include <linux/time.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <net/addrconf.h>
#include <linux/types.h>
#include "xcommon.h"

/* TODO: Need to remove this macro as all the experimental code is verified.
 * All the non-experimental code should be deleted.
 */
#define	EXPERIMENTAL_CODE
int debug;
struct class *xrnic_class;
/* Need to enable this using sysfs.*/
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none, 1=all)");

#define XRNIC_REG_MAP_NODE	0
#define cpu_to_be24(x)	((x) << 16)

struct xrnic_conn_param {
	const void *private_data;
	u8 private_data_len;
	u8 responder_resources;
	u8 initiator_depth;
	u8 flow_control;
	u8 retry_count;
	u8 rnr_retry_count;
	u8 srq;
	u8 qp_num;
};

/* EXTRA Bytes for Invariant CRC */
#define	ERNIC_INV_CRC	4
/* ERNIC Doesn't have Variant CRC for P2P */
#define	ERNIC_VAR_CRC	0
#define	EXTRA_PKT_LEN	(ERNIC_INV_CRC + ERNIC_VAR_CRC)

struct xrnic_dev_info *xrnic_dev;
static dev_t xrnic_dev_number;

/*
 * To store the IP address of the controller, which is passed as a
 * module param
 */
static char server_ip[16];
/* To store the port number. This is passed as a module param */
static unsigned short port_num;
/* To store the mac_address. This is passed as a module param */
static ushort mac_address[6] = {0x1, 0x0, 0x0, 0x35, 0x0a, 0x00};
/* To store the ethernet interface name, which is passed as a module param */
static char *ifname = "eth0";

module_param(port_num, ushort, 0444);
MODULE_PARM_DESC(port_num, "network port number");

module_param_array(mac_address, ushort, NULL, 0444);
MODULE_PARM_DESC(mac_address, "mac address");

module_param_string(server_ip, server_ip, 32, 0444);
MODULE_PARM_DESC(server_ip, "Target server ip address");

module_param(ifname, charp, 0444);
MODULE_PARM_DESC(ifname, "Target server interface name eth0..");

/**
 * xrnic_rdma_create_id() - Creates and RDMA ID
 * @xrnic_cm_handler: communication event handler
 * @cm_context: CM context
 * @ps: Port space
 * @qp_type: Queue transport type
 * @num_child: Max QP count
 *
 * @return: 0 on success, other value incase of failure
 */
struct xrnic_rdma_cm_id *xrnic_rdma_create_id
	(int (*xrnic_cm_handler)(struct xrnic_rdma_cm_id *cm_id,
	struct xrnic_rdma_cm_event_info *conn_event_info), void *cm_context,
	enum xrnic_port_space ps, enum xrnic_qp_type qp_type, int num_child)
{
	struct xrnic_qp_attr *qp1_attr = NULL;
	struct xrnic_rdma_cm_id *cm_id = NULL;
	struct xrnic_qp_info *qp_info = NULL;
	struct xrnic_rdma_cm_id_info *cm_id_info = NULL;

	if (!xrnic_dev) {
		pr_err("Received NULL pointer\n");
		return (struct xrnic_rdma_cm_id *)NULL;
	}

	qp1_attr = &xrnic_dev->qp1_attr;
	if (xrnic_dev->io_qp_count < num_child ||
	    num_child < 0 || qp_type != qp1_attr->qp_type) {
		pr_err("Invalid info received\n");
		return NULL;
	}

	cm_id_info = kzalloc(sizeof(*cm_id_info), GFP_KERNEL);
	if (!cm_id_info)
		return ERR_PTR(-ENOMEM);

	xrnic_dev->curr_cm_id_info = cm_id_info;
	cm_id = (struct xrnic_rdma_cm_id *)&cm_id_info->parent_cm_id;
	cm_id->xrnic_cm_handler = xrnic_cm_handler;
	cm_id->cm_context = cm_context;
	cm_id->ps = ps;
	cm_id->qp_type = qp_type;
	cm_id->cm_id_info = cm_id_info;
	cm_id->child_qp_num = 0;
	cm_id->qp_status = XRNIC_PORT_QP_FREE;

	qp_info = &cm_id->qp_info;
	memset(qp_info, 0, sizeof(*qp_info));

	qp_info->qp_num = qp1_attr->qp_num;
	list_add_tail(&cm_id->list, &cm_id_list);

	return cm_id;
}
EXPORT_SYMBOL(xrnic_rdma_create_id);

/**
 * ipv6_addr_compare() - Compares IPV6 addresses
 * @addr1: Address 1 to compare
 * @addr2: Address 2 to compare
 * @size: size of the address
 *
 * @return: 0 on success, -1 incase of a mismatch
 */
static int ipv6_addr_compare(u8 *addr1, u8 *addr2, size_t size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (addr1[(size - 1) - i] != addr2[i])
			return -1;
	}

	return 0;
}

/**
 * xrnic_rdma_bind_addr() - Binds IP-V4/V6 addresses
 * @cm_id: CM ID to with address CM info
 * @addr: Address to bind to
 * @port_num: Tranport port number
 * @ip_addr_type: IP-V4/V6
 *
 * @return: 0 on success, error indicative value incase of failure
 */
int xrnic_rdma_bind_addr(struct xrnic_rdma_cm_id *cm_id,
			 u8 *addr, u16 port_num, u16 ip_addr_type)
{
	if (!cm_id || !xrnic_dev) {
		pr_err("Invalid CM ID or XRNIC device info\n");
		return -EINVAL;
	}

	if (xrnic_dev->curr_cm_id_info != cm_id->cm_id_info)
		return -XRNIC_INVALID_CM_ID;

	if (port_num < 1UL || port_num > XRNIC_MAX_PORT_SUPPORT)
		return -XRNIC_INVALID_PORT;

	if (!cm_id)
		return -XRNIC_INVALID_CM_ID;

	if (cm_id->child_qp_num)
		return -XRNIC_INVALID_CHILD_NUM;

	if (xrnic_dev->cm_id_info[port_num - 1])
		return -XRNIC_INVALID_PORT;

	if (xrnic_dev->port_status[port_num - 1] == XRNIC_PORT_QP_IN_USE)
		return XRNIC_INVALID_CM_ID;

	if (cm_id->qp_status == XRNIC_PORT_QP_IN_USE)
		return XRNIC_INVALID_CM_ID;

	if (ip_addr_type == AF_INET6) {
		if (ipv6_addr_compare((u8 *)&xrnic_dev->ipv6_addr, addr,
				      sizeof(struct in6_addr)))
			return -XRNIC_INVALID_ADDR;
		memcpy((void *)&cm_id->route.src_addr, (void *)addr,
		       sizeof(struct in6_addr));
	} else if (ip_addr_type == AF_INET) {
		if (memcmp(&xrnic_dev->ipv4_addr, addr,
			   sizeof(struct in_addr)))
			return -XRNIC_INVALID_ADDR;
		memcpy((void *)&cm_id->route.src_addr, (void *)addr,
		       sizeof(struct in_addr));
	} else {
		return -XRNIC_INVALID_ADDR_TYPE;
	}
	xrnic_dev->cm_id_info[port_num - 1] = cm_id->cm_id_info;
	cm_id->port_num = port_num;
	cm_id->route.ip_addr_type = ip_addr_type;
	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_bind_addr);

/**
 * xrnic_rdma_listen() - Initiates listen on the socket
 * @cm_id: CM ID
 * @backlog: back log
 *
 * @return: 0 on success, error indicative value incase of failure
 */
int xrnic_rdma_listen(struct xrnic_rdma_cm_id *cm_id, int backlog)
{
	if (!cm_id || !xrnic_dev) {
		pr_err("Rx invalid pointers\n");
		return -EINVAL;
	}

	if (xrnic_dev->curr_cm_id_info != cm_id->cm_id_info)
		return XRNIC_INVALID_CM_ID;

	if (xrnic_dev->port_status[cm_id->port_num - 1] ==
	    XRNIC_PORT_QP_IN_USE)
		return XRNIC_INVALID_PORT;

	if (cm_id->qp_status == XRNIC_PORT_QP_IN_USE)
		return XRNIC_INVALID_QP_ID;

	xrnic_dev->port_status[cm_id->port_num - 1] = XRNIC_PORT_QP_IN_USE;
	xrnic_dev->curr_cm_id_info = NULL;

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_listen);

/**
 * xrnic_hw_hs_reset_sq_cq() - Enables HW Handshake for a given QP
 * @qp_info: QP which should be enabled for HW Handshake
 * @hw_hs_info: HW Handshake info with which QP config needs to be updated
 *
 * @return: XRNIC_SUCCESS on success, error indicative value incase of failure
 */
int xrnic_hw_hs_reset_sq_cq(struct xrnic_qp_info *qp_info,
			    struct xrnic_hw_handshake_info *hw_hs_info)
{
	struct xrnic_qp_attr *qp_attr;

	if (!qp_info) {
		pr_err("Rx invalid qp info\n");
		return -EINVAL;
	}

	if (!xrnic_dev) {
		pr_err("Invalid ERNIC info\n");
		return -EINVAL;
	}

	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	qp_attr = &xrnic_dev->qp_attr[qp_info->qp_num - 2];
	if (qp_attr->remote_cm_id)
		xrnic_reset_io_qp_sq_cq_ptr(qp_attr, hw_hs_info);

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_hw_hs_reset_sq_cq);

/**
 * xrnic_hw_hs_reset_rq() - Updates HW handshake for RQ
 * @qp_info: QP which should be enabled for HW Handshake
 *
 * @return: XRNIC_SUCCESS on success, error indicative value incase of failure
 */
int xrnic_hw_hs_reset_rq(struct xrnic_qp_info *qp_info)
{
	struct xrnic_qp_attr *qp_attr;

	if (!qp_info) {
		pr_err("Rx invalid qp info\n");
		return -EINVAL;
	}

	if (!xrnic_dev) {
		pr_err("Invalid ERNIC info\n");
		return -EINVAL;
	}
	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	qp_attr = &xrnic_dev->qp_attr[qp_info->qp_num - 2];
	if (qp_attr->remote_cm_id)
		xrnic_reset_io_qp_rq_ptr(qp_attr);

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_hw_hs_reset_rq);

/**
 * set_ipv4_ipaddress() - Configures XRNIC IP address
 * @return: 0 on success, error indicative value incase of failure
 */
static int set_ipv4_ipaddress(void)
{
	struct xrnic_ctrl_config *xrnic_ctrl_config =
		&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	u32 config_value = 0;
	u32 ipv4_addr = 0;
	struct net_device *dev = __dev_get_by_name(&init_net, ifname);
	struct in_device *inet_dev;

	inet_dev = (struct in_device *)dev->ip_ptr;

	if (!dev) {
		pr_err("CMAC interface not configured\n");
		return XRNIC_FAILED;
	}

	if (((struct in_device *)dev->ip_ptr)->ifa_list) {
		ipv4_addr = inet_dev->ifa_list->ifa_address;
		if (!ipv4_addr) {
			pr_err("cmac ip addr: ifa_address not available\n");
			return XRNIC_FAILED;
		}
		snprintf(server_ip, 16, "%pI4", &ipv4_addr);
		in4_pton(server_ip, strlen(server_ip), xrnic_dev->ipv4_addr,
			 '\0', NULL);
		DEBUG_LOG("xcmac ip_address:%s\n", server_ip);
	} else {
		pr_info("xcmac ip address: not available at present\n");
		return 0;
	}

	switch (dev->mtu) {
	case 340:
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_256;
			break;
	case 592:
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_512;
			break;
	case 1500:
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_1024;
			break;
	case 2200:
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_2048;
			break;
	case 4200:
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_4096;
			break;
	default:
			xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_4096;
			DEBUG_LOG("MTU set to %d\n", dev->mtu);
	}
	config_value = (xrnic_dev->ipv4_addr[3] << 0)	|
		(xrnic_dev->ipv4_addr[2] << 8)		|
		(xrnic_dev->ipv4_addr[1] << 16)		|
		(xrnic_dev->ipv4_addr[0] << 24);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_config->ipv4_address)));
	DEBUG_LOG("XRNIC IPV4 address [%x]\n", config_value);
	return 0;
}

/**
 * set_ipv6_ipaddress() - Configures XRNIC IPV6 address
 * @return: 0 on success, error indicative value incase of failure
 */
static int set_ipv6_ipaddress(void)
{
	struct xrnic_ctrl_config *xrnic_ctrl_conf;
	u32 config_value = 0;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifp, *tmp;
	u8 i, ip6_set = 0;
	struct net_device *dev = __dev_get_by_name(&init_net, ifname);

	xrnic_ctrl_conf = &xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	if (!dev) {
		pr_err("CMAC interface not configured\n");
		return XRNIC_FAILED;
	}

	idev = __in6_dev_get(dev);
	if (!idev) {
		pr_err("ipv6 inet device not found\n");
		return 0;
	}

	list_for_each_entry_safe(ifp, tmp, &idev->addr_list, if_list) {
		DEBUG_LOG("IP=%pI6, MAC=%pM\n", &ifp->addr, dev->dev_addr);
		for (i = 0; i < 16; i++) {
			DEBUG_LOG("IP=%x\n", ifp->addr.s6_addr[i]);
			xrnic_dev->ipv6_addr[15 - i] = ifp->addr.s6_addr[i];
		}
		ip6_set = 1;
	}
	if (ip6_set == 0) {
		pr_info("xcmac ipv6 address: not available at present\n");
		return 0;
	}

	switch (dev->mtu) {
	case 340:
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_256;
		break;
	case 592:
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_512;
		break;
	case 1500:
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_1024;
		break;
	case 2200:
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_2048;
		break;
	case 4200:
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_4096;
		break;
	default:
		xrnic_dev->pmtu = XRNIC_QP_CONFIG_PMTU_4096;
		DEBUG_LOG("MTU set to %d\n", dev->mtu);
	}
	config_value =	(xrnic_dev->ipv6_addr[0] << 0)	|
			(xrnic_dev->ipv6_addr[1] << 8)	|
			(xrnic_dev->ipv6_addr[2] << 16)	|
			(xrnic_dev->ipv6_addr[3] << 24);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->ip_xrnic_addr1)));
	DEBUG_LOG("XRNIC IPV6 address [%x]\n", config_value);

	config_value =	(xrnic_dev->ipv6_addr[4] << 0)	|
			(xrnic_dev->ipv6_addr[5] << 8)	|
			(xrnic_dev->ipv6_addr[6] << 16)	|
			(xrnic_dev->ipv6_addr[7] << 24);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->ip_xrnic_addr2)));
	DEBUG_LOG("XRNIC IPV6 address [%x]\n", config_value);

	config_value =	(xrnic_dev->ipv6_addr[8] << 0)	 |
			(xrnic_dev->ipv6_addr[9] << 8)	 |
			(xrnic_dev->ipv6_addr[10] << 16) |
			(xrnic_dev->ipv6_addr[11] << 24);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->ip_xrnic_addr3)));
	DEBUG_LOG("XRNIC IPV6 address [%x]\n", config_value);

	config_value =	(xrnic_dev->ipv6_addr[12] << 0)	 |
			(xrnic_dev->ipv6_addr[13] << 8)	 |
			(xrnic_dev->ipv6_addr[14] << 16) |
			(xrnic_dev->ipv6_addr[15] << 24);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->ip_xrnic_addr4)));
	DEBUG_LOG("XRNIC IPV6 address [%x]\n", config_value);
	return 0;
}

/**
 * cmac_inet6addr_event() - Handles IPV6 events
 * @notifier: notifier info
 * @event: Rx event
 * @data: Event specific data
 *
 * @return: 0 on success, error indicative value incase of failure
 */
static int cmac_inet6addr_event(struct notifier_block *notifier,
				unsigned long event, void *data)
{
	switch (event) {
	case NETDEV_DOWN:
		pr_info("Driver link down\r\n");
		break;
	case NETDEV_UP:
		pr_info("Driver link up ipv6\r\n");
		if (set_ipv6_ipaddress() == XRNIC_FAILED)
			return XRNIC_FAILED;
		break;
	case NETDEV_CHANGEADDR:
		pr_info("Driver link change address ipv6\r\n");
		if (set_ipv6_ipaddress() == XRNIC_FAILED)
			return XRNIC_FAILED;
		break;
	}
	return 0;
}

/**
 * cmac_inetaddr_event() - Handles IPV4 events
 * @notifier: notifier info
 * @event: Rx event
 * @data: Event specific data
 * @return: 0 on success, error indicative value incase of failure
 */
static int cmac_inetaddr_event(struct notifier_block *notifier,
			       unsigned long event, void *data)
{
	struct in_ifaddr *ifa = data;
	struct net_device *event_netdev = ifa->ifa_dev->dev;
	struct net_device *dev = __dev_get_by_name(&init_net, ifname);

	if (!dev) {
		pr_err("CMAC interface not configured\n");
		return XRNIC_FAILED;
	}

	if (event_netdev != dev)
		return 0;
	pr_info("Xrnic: event = %ld\n", event);
	switch (event) {
	case NETDEV_DOWN:
		pr_info("Xrnic: link down\n");
		break;
	case NETDEV_UP:
		pr_info("Xrnic: link up\n");
		if (set_ipv4_ipaddress() == XRNIC_FAILED)
			return XRNIC_FAILED;
		break;
	case NETDEV_CHANGEADDR:
		pr_info("Xrnic: ip address change detected\n");
		if (set_ipv4_ipaddress() == XRNIC_FAILED)
			return XRNIC_FAILED;
		break;
	}
	return 0;
}

struct notifier_block cmac_inetaddr_notifier = {
	.notifier_call = cmac_inetaddr_event
};

struct notifier_block cmac_inet6addr_notifier = {
	.notifier_call = cmac_inet6addr_event
};

static const struct file_operations xrnic_fops = {
	/*TODO: Implement read/write/ioctl operations. */
	.owner		= THIS_MODULE,	/* Owner */
};

/**
 * xrnic_irq_handler() - XRNIC interrupt handler
 * @irq: Irq number
 * @data: Pointer to XRNIC device info structure
 *
 * @return: IRQ_HANDLED incase of success or other value in case of failure
 */
static irqreturn_t xrnic_irq_handler(int irq, void *data)
{
	struct xrnic_dev_info *xrnic_dev = (struct xrnic_dev_info *)data;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct xrnic_ctrl_config *xrnic_ctrl_config =
			&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	u32 config_value = 0;
	unsigned long flag;

	spin_lock_irqsave(&qp1_attr->qp_lock, flag);
	config_value = ioread32((void *)&xrnic_ctrl_config->intr_sts);

	/* We are checking masked interrupt.*/
	config_value = config_value & xrnic_dev->xrnic_mmap.intr_en;
	if (!config_value)
		pr_err("Rx disabled or masked interrupt\n");

	if (config_value & PKT_VALID_ERR_INTR_EN) {
		pr_info("Packet validation fail interrupt rx\n");
		iowrite32(PKT_VALID_ERR_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
	}

	if (config_value & MAD_PKT_RCVD_INTR_EN) {
		DEBUG_LOG("MAD Packet rx interrupt\n");
		/* Clear the interrupt */
		iowrite32(MAD_PKT_RCVD_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
		/* process the MAD pkt */
		tasklet_schedule(&xrnic_dev->mad_pkt_recv_task);
	}

	if (config_value & BYPASS_PKT_RCVD_INTR_EN) {
		DEBUG_LOG("Bypass packet Rx interrupt\n");
		iowrite32(BYPASS_PKT_RCVD_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
	}

	if (config_value & RNR_NACK_GEN_INTR_EN) {
		DEBUG_LOG("Rx RNR Nack interrupt\n");
		iowrite32(RNR_NACK_GEN_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
	}

	if (config_value & WQE_COMPLETED_INTR_EN) {
		DEBUG_LOG("Rx WQE completion interrupt\n");
		xrnic_dev->xrnic_mmap.intr_en = xrnic_dev->xrnic_mmap.intr_en &
			(~WQE_COMPLETED_INTR_EN);
		iowrite32(xrnic_dev->xrnic_mmap.intr_en,
			  ((void *)(&xrnic_ctrl_config->intr_en)));
		tasklet_schedule(&xrnic_dev->wqe_completed_task);
	}

	if (config_value & ILL_OPC_SENDQ_INTR_EN) {
		DEBUG_LOG("Rx illegal opcode interrupt\n");
		iowrite32(ILL_OPC_SENDQ_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
	}

	if (config_value & QP_PKT_RCVD_INTR_EN) {
		DEBUG_LOG("Rx data packet interrupt\n");
		xrnic_dev->xrnic_mmap.intr_en = xrnic_dev->xrnic_mmap.intr_en &
			(~QP_PKT_RCVD_INTR_EN);
		iowrite32(xrnic_dev->xrnic_mmap.intr_en,
			  ((void *)(&xrnic_ctrl_config->intr_en)));
		tasklet_schedule(&xrnic_dev->qp_pkt_recv_task);
	}

	if (config_value & FATAL_ERR_INTR_EN) {
		pr_info("Rx Fatal error interrupt\n");

		iowrite32(FATAL_ERR_INTR_EN,
			  (void __iomem *)&xrnic_ctrl_config->intr_sts);
		/* 0 is some random value*/
		xrnic_qp_fatal_handler(0);
	}

	spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
	return IRQ_HANDLED;
}

/**
 * xrnic_ctrl_hw_configuration() - Xrnic control configuration initizations
 * @return: 0 on success, other value incase of failure
 */
static int xrnic_ctrl_hw_configuration(void)
{
	struct xrnic_memory_map *xrnic_mmap = &xrnic_dev->xrnic_mmap;
	struct xrnic_ctrl_config *xrnic_ctrl_conf;
	u32 config_value = 0;
	struct net_device *dev = NULL;

	xrnic_ctrl_conf = &xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;

	if (!xrnic_dev || !xrnic_dev->xrnic_mmap.xrnic_regs ||
	    !xrnic_ctrl_conf) {
		pr_err("Invalid device pointers\n");
		return -EINVAL;
	}

	xrnic_mmap = &xrnic_dev->xrnic_mmap;

	dev = __dev_get_by_name(&init_net, ifname);
	if (!dev) {
		pr_err("Ethernet mac address not configured\n");
		return XRNIC_FAILED;
	}
	/* Set the MAC address */
	config_value = dev->dev_addr[5] | (dev->dev_addr[4] << 8) |
		(dev->dev_addr[3] << 16) | (dev->dev_addr[2] << 24);
	DEBUG_LOG("Source MAC address LSB [%x]\n", config_value);
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->mac_xrnic_src_addr_lsb)));

	DEBUG_LOG("Source MAC address LSB [%x]\n", config_value);
	config_value = dev->dev_addr[1] | (dev->dev_addr[0] << 8);
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->mac_xrnic_src_addr_msb)));
	DEBUG_LOG("Source MAC address MSB [%x]\n", config_value);

	if (set_ipv4_ipaddress() == XRNIC_FAILED) {
		pr_err("ETH0 AF_INET address: ifa_list not available.\n");
		return XRNIC_FAILED;
	}

	if (set_ipv6_ipaddress() == XRNIC_FAILED) {
		pr_err("ETH0 AF_INET6 address: ifa_list not available.\n");
		return XRNIC_FAILED;
	}

	/* At present 128 TX headers and each size 128 bytes */
	config_value = xrnic_mmap->tx_hdr_buf_ba_phys;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->tx_hdr_buf_ba)));
	DEBUG_LOG("Tx header buf base address [0x%x]\n", config_value);

	config_value = XRNIC_NUM_OF_TX_HDR | (XRNIC_SIZE_OF_TX_HDR << 16);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->tx_hdr_buf_sz)));
	DEBUG_LOG("Tx header buf size [0x%x]\n", config_value);

	/* At present 256 TX SGL and each size 16 bytes */
	config_value = xrnic_mmap->tx_sgl_buf_ba_phys & 0xffffffff;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->tx_sgl_buf_ba)));
	DEBUG_LOG("Tx SGL buf base address [0x%x]\n", config_value);

	config_value = XRNIC_NUM_OF_TX_SGL | (XRNIC_SIZE_OF_TX_SGL << 16);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->tx_sgl_buf_sz)));
	DEBUG_LOG("Tx SGL buf size [0x%x]\n", config_value);

	/* At present 32 Bypass buffers and each size 512 bytes */
	config_value = xrnic_mmap->bypass_buf_ba_phys;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->bypass_buf_ba)));
	DEBUG_LOG("Bypass buf base address [0x%x]\n", config_value);

	config_value = XRNIC_NUM_OF_BYPASS_BUF |
		(XRNIC_SIZE_OF_BYPASS_BUF << 16);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->bypass_buf_sz)));
	DEBUG_LOG("Bypass buf size [0x%x]\n", config_value);

	config_value = XRNIC_BYPASS_BUF_WRPTR;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->bypass_buf_wrptr)));
	DEBUG_LOG("Bypass buffer write pointer [0x%x]\n", config_value);

	config_value = xrnic_mmap->err_pkt_buf_ba_phys;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->err_pkt_buf_ba)));
	DEBUG_LOG("Error packet buf base address [0x%x]\n", config_value);

	config_value = XRNIC_NUM_OF_ERROR_BUF |
			(XRNIC_SIZE_OF_ERROR_BUF << 16);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->err_pkt_buf_sz)));
	DEBUG_LOG("Error packet buf size [0x%x]\n", config_value);

	config_value = XRNIC_ERROR_BUF_WRPTR;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->err_buf_wrptr)));
	DEBUG_LOG("Error pakcet buf write pointer [0x%x]\n", config_value);

	config_value = xrnic_mmap->out_errsts_q_ba_phys;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->out_errsts_q_ba)));
	DEBUG_LOG("Outgoing error status queue base address [0x%x]\n",
		  config_value);

	config_value = XRNIC_OUT_ERRST_Q_NUM_ENTRIES;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->out_errsts_q_sz)));
	DEBUG_LOG("Outgoing error status queue size [0x%x]\n", config_value);

	config_value = xrnic_mmap->in_errsts_q_ba_phys;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->in_errsts_q_ba)));
	DEBUG_LOG("Incoming error status queue base address [0x%x]\n",
		  config_value);

	config_value = XRNIC_IN_ERRST_Q_NUM_ENTRIES;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->in_errsts_q_sz)));
	DEBUG_LOG("Incoming error status queue size [0x%x]\n", config_value);

	config_value = xrnic_mmap->data_buf_ba_phys;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->data_buf_ba)));
	DEBUG_LOG("RDMA Outgoing data buf base addr [0x%x]\n", config_value);

	config_value = XRNIC_NUM_OF_DATA_BUF | (XRNIC_SIZE_OF_DATA_BUF << 16);
	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->data_buf_sz)));
	DEBUG_LOG("RDMA Outgoing data buf size [0x%x]\n", config_value);

	config_value = xrnic_mmap->resp_err_pkt_buf_ba_phys;
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->resp_err_pkt_buf_ba)));
	DEBUG_LOG("Response error packet buf base address [0x%x]\n",
		  config_value);

	config_value = XRNIC_NUM_OF_RESP_ERR_BUF |
		(XRNIC_SIZE_OF_RESP_ERR_BUF << 16);
	iowrite32(config_value,
		  ((void *)(&xrnic_ctrl_conf->resp_err_buf_sz)));
	DEBUG_LOG("Response error packet buf size [0x%x]\n", config_value);

	/* Enable the RNIC configuration*/
	config_value = (XRNIC_CONFIG_XRNIC_EN |
			XRNIC_CONFIG_ERR_BUF_EN |
			XRNIC_CONFIG_NUM_QPS_ENABLED |
			XRNIC_CONFIG_FLOW_CONTROL_EN |
			XRNIC_CONFIG_UDP_SRC_PORT);

	iowrite32(config_value, ((void *)(&xrnic_ctrl_conf->xrnic_conf)));
	return	XRNIC_SUCCESS;
}

/**
 * xrnic_ctrl_hw_init() - Xrnic control configuration initizations
 * @return: 0 on success, other value incase of failure
 */
static int xrnic_ctrl_hw_init(void)
{
	struct xrnic_ctrl_config *xrnic_ctrl_config =
		&xrnic_dev->xrnic_mmap.xrnic_regs->xrnic_ctrl_config;
	u32 config_value = 0;
	int ret = 0, i;

	/* Invoking rnic global initialization configuration */
	ret = xrnic_ctrl_hw_configuration();
	if (ret) {
		pr_err("xrnic hw config failed with ret code [%d]\n", ret);
		return ret;
	}

	/* Invoking RDMA QP1 configuration */
	ret = xrnic_qp1_hw_configuration();
	if (ret) {
		pr_err("xrnic qp1 config failed with ret code [%d]\n", ret);
		return ret;
	}

	/* Invoking RDMA other data path QP configuration, as we are not
	 * resgistring any data path interrupt handler so no ret.
	 */
	for (i = 0; i < XRNIC_MAX_QP_SUPPORT; i++)
		xrnic_qp_hw_configuration(i);

	/* Enabling xrnic interrupts. */
	config_value = MAD_PKT_RCVD_INTR_EN |
		 RNR_NACK_GEN_INTR_EN |
		WQE_COMPLETED_INTR_EN | ILL_OPC_SENDQ_INTR_EN |
		QP_PKT_RCVD_INTR_EN | FATAL_ERR_INTR_EN;

	if (config_value & ~XRNIC_INTR_ENABLE_DEFAULT) {
		DEBUG_LOG("Setting the default interrupt enable config\n");
		config_value = XRNIC_INTR_ENABLE_DEFAULT;
	}

	/*Writing to interrupt enable register.*/
	xrnic_dev->xrnic_mmap.intr_en = config_value;
	iowrite32(config_value, ((void *)(&xrnic_ctrl_config->intr_en)));

	DEBUG_LOG("Interrupt enable reg value [%#x]\n",
		  ioread32((void __iomem *)&xrnic_ctrl_config->intr_en));
	return ret;
}

/**
 * xrnic_fill_wr() - This function fills the Send queue work request info
 * @qp_attr: qp config info to fill the WR
 * @qp_depth: Depth of the Queue
 */
void xrnic_fill_wr(struct xrnic_qp_attr *qp_attr, u32 qp_depth)
{
	int i;
	struct wr *sq_wr; /*sq_ba*/

	for (i = 0; i < qp_depth; i++) {
		sq_wr = (struct wr *)qp_attr->sq_ba + i;
		sq_wr->ctx.wr_id = i;
		sq_wr->local_offset[0] = (qp_attr->send_sgl_phys & 0xffffffff)
					 + (i * XRNIC_SEND_SGL_SIZE);
		sq_wr->local_offset[1] = 0;
		sq_wr->length = XRNIC_SEND_SGL_SIZE;
		sq_wr->opcode = XRNIC_SEND_ONLY;
		sq_wr->remote_offset[0] = 0;
		sq_wr->remote_offset[1] = 0;
		sq_wr->remote_tag = 0;
	}
}

static int xernic_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *np = NULL;
	struct resource resource;
	void __iomem *virt_addr;
	u64 start_addr;
	int status;
	int len;
/* TODO: Not using pdev. Rather using a global data structure xrnic_dev,
 * which is shared among all the objects in ernic driver.
 * Need to set platform private data as xrnic_dev and all the objects of
 * ernic driver has to retrieve from platform_device pointer.
 */
#ifdef EXPERIMENTAL_CODE
	int val = 0;
#endif
	phys_addr_t phy_addr;

	pr_info("XRNIC driver Version = %s\n", XRNIC_VERSION);

	register_inetaddr_notifier(&cmac_inetaddr_notifier);
	register_inet6addr_notifier(&cmac_inet6addr_notifier);
	init_mr(MEMORY_REGION_BASE, MEMORY_REGION_LEN);

	np = of_find_node_by_name(NULL, "ernic");
	if (!np) {
		pr_err("xrnic can't find compatible node in device tree.\n");
		return -ENODEV;
	}

	xrnic_dev = kzalloc(sizeof(*xrnic_dev), GFP_KERNEL);
	if (!xrnic_dev)
		return -ENOMEM;
	ret = alloc_chrdev_region(&xrnic_dev_number, 0,
				  NUM_XRNIC_DEVS, DEVICE_NAME);
	if (ret) {
		DEBUG_LOG("XRNIC:: Failed to register char device\n");
		goto alloc_failed;
	} else {
		DEBUG_LOG(KERN_INFO "XRNIC Registered with :\n");
		DEBUG_LOG(KERN_INFO "Major : %u || ", MAJOR(xrnic_dev_number));
		DEBUG_LOG(KERN_INFO "Minor : %u\n", MINOR(xrnic_dev_number));
	}
/* TODO: xrnic_class is created but not used. Need to enable debug and
 * statistic counters though this interface.
 */
	xrnic_class = class_create(THIS_MODULE, DEVICE_NAME);
	if (IS_ERR(xrnic_class)) {
		ret = PTR_ERR(xrnic_class);
		goto class_failed;
	}

	/* Connect the file operations with the cdev */
	/* TODO: cdev created but not used. Need to implement when
	 * userspace applications are implemented. Currently all the
	 * callbacks in xrnic_fops are dummy.
	 */
	cdev_init(&xrnic_dev->cdev, &xrnic_fops);
	xrnic_dev->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number to the cdev */
	ret = cdev_add(&xrnic_dev->cdev, xrnic_dev_number, 1);
	if (IS_ERR(ERR_PTR(ret))) {
		DEBUG_LOG("ERROR: XRNIC cdev allocation	failed\n");
		goto cdev_failed;
	}

	device_create(xrnic_class, NULL, xrnic_dev_number, NULL,
		      "%s", "xrnic0");

	/* The node offset argument 0 xrnic 0x0 0x84000000 len 128K*/
	ret = of_address_to_resource(np, XRNIC_REG_MAP_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 0.\n");
		goto dev_failed;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_REG_MAP_NODE);
	DEBUG_LOG("xrnic memory 0x%llx of size=%x bytes mapped at 0x%p\n",
		  start_addr, (u32)resource.end - (u32)resource.start,
		  virt_addr);

	xrnic_dev->xrnic_mmap.xrnic_regs_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.xrnic_regs = (struct xrnic_reg_map *)virt_addr;
#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (IS_ERR_VALUE(phy_addr)) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.tx_hdr_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.tx_hdr_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.tx_hdr_buf_ba, 0, len);
	DEBUG_LOG("xrnic memory Tx HDR BUF Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.tx_hdr_buf_ba_phys);
#else
	/*Mapping for Xrnic TX HEADERS 0x20100000 to 16 kb.*/
	ret = of_address_to_resource(np, XRNIC_TX_HDR_BUF_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 5.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_TX_HDR_BUF_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory TX header 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.tx_hdr_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.tx_hdr_buf_ba = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (IS_ERR_VALUE(phy_addr)) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.tx_sgl_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.tx_sgl_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.tx_sgl_buf_ba, 0, len);
	DEBUG_LOG("xrnic memory Tx SGL Buf Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.tx_sgl_buf_ba_phys);
#else
	/*Mapping for Xrnic TX DMA SGL 0xB4000000 to 16 kb.*/
	ret = of_address_to_resource(np, XRNIC_TX_SGL_BUF_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 6.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_TX_SGL_BUF_NODE);
	DEBUG_LOG("xrnic memory TX SGL 0x%llx of size=%x\n",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.tx_sgl_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.tx_sgl_buf_ba = (void *)(uintptr_t)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (IS_ERR_VALUE(phy_addr)) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.bypass_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.bypass_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.bypass_buf_ba, 0, len);
	DEBUG_LOG("xrnic memory Bypass Buf Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.bypass_buf_ba_phys);
#else
	/*Mapping for Xrnic BYPASS PL 0x20120000 to 16 kb.*/
	/*Mapping for Xrnic BYPASS PS 0x20120000 to 16 kb.*/
	ret = of_address_to_resource(np, XRNIC_BYPASS_BUF_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 7.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_BYPASS_BUF_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory BYPASS:0x%llx of siz:%xb mapped at 0x%p\n",
		  start_addr, (u32)resource.end - (u32)resource.start,
		  virt_addr);

	xrnic_dev->xrnic_mmap.bypass_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.bypass_buf_ba = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_NUM_OF_ERROR_BUF * XRNIC_SIZE_OF_ERROR_BUF;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.err_pkt_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.err_pkt_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.err_pkt_buf_ba, 0, len);
	DEBUG_LOG("xrnic memory ERR PKT Buf Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.err_pkt_buf_ba_phys);
#else
	/*Mapping for Xrnic ERROR-DROPP PL 0x20110000 to 16 kb.*/
	/*Mapping for Xrnic ERROR-DROPP PS 0x20110000 to 16 kb.*/
	ret = of_address_to_resource(np, XRNIC_ERRPKT_BUF_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 8.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_ERRPKT_BUF_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory ERROR PKT 0x%llx of size=%x\n",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.err_pkt_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.err_pkt_buf_ba = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_OUT_ERRST_Q_NUM_ENTRIES;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.out_errsts_q_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.out_errsts_q_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.out_errsts_q_ba, 0, len);
	DEBUG_LOG("xrnic memory OUT ERR STS Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.out_errsts_q_ba_phys);
#else
	/*Mapping for Xrnic OUT ERR_STS 0x29000000 to 4 kb.*/
	ret = of_address_to_resource(np, XRNIC_OUTERR_STS_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 9.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_OUTERR_STS_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory 0x%llx of size=%x bytes mapped at 0x%p\n",
		  start_addr, (u32)resource.end - (u32)resource.start,
		  virt_addr);

	xrnic_dev->xrnic_mmap.out_errsts_q_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.out_errsts_q_ba = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_IN_ERRST_Q_NUM_ENTRIES;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.in_errsts_q_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.in_errsts_q_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.in_errsts_q_ba, 0, len);
	DEBUG_LOG("xrnic memory IN ERR STS Base Address = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.in_errsts_q_ba_phys);
#else
	/*Mapping for Xrnic IN ERR_STS PL 0x29001000 to 16 kb.*/
	/*Mapping for Xrnic IN ERR_STS PS 0x29001000 to 16 kb.*/
	ret = of_address_to_resource(np, XRNIC_INERR_STS_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 10.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_INERR_STS_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory 0x%llx of size=%x bytes mapped at 0x%p\n",
		  start_addr, (u32)resource.end - (u32)resource.start,
		  virt_addr);

	xrnic_dev->xrnic_mmap.in_errsts_q_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.in_errsts_q_ba = (void *)virt_addr;
#endif

	/*Mapping for Xrnic RQ WR DBRL PL 0x29002000 to 4 kb.*/
	/*Mapping for Xrnic RQ WR DBRL PS 0x29002000 to 4 kb.*/
#ifdef EXPERIMENTAL_CODE
	len = XRNIC_NUM_OF_DATA_BUF * XRNIC_SIZE_OF_DATA_BUF;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.data_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.data_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.data_buf_ba, 0, len);
#else
	/*Mapping for Xrnic RQ STATUS PER QP 0x29040000 to 4 kb.*/
	ret = of_address_to_resource(np, XRNIC_DATA_BUF_BA_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 14.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_DATA_BUF_BA_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory DATA BUFF BA 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.data_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.data_buf_ba = (void *)virt_addr;
#endif
#ifdef EXPERIMENTAL_CODE
	len = XRNIC_NUM_OF_RESP_ERR_BUF * XRNIC_SIZE_OF_RESP_ERR_BUF;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba, 0, len);
#else
	/*Mapping for Xrnic RQ STATUS PER QP 0x20130000 to 16kb.*/
	ret = of_address_to_resource(np, XRNIC_RESP_ERR_PKT_BUF_BA, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 14.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_RESP_ERR_PKT_BUF_BA);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic response error packet buffer base address [0x%llx]",
		  start_addr);
	DEBUG_LOG(" of size=%x bytes mapped at 0x%p\n",
		  (u32)resource.end - (u32)resource.start, virt_addr);

	xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba = (void *)virt_addr;
#endif
#ifdef EXPERIMENTAL_CODE
	len = XRNIC_SEND_SGL_SIZE * XRNIC_SQ_DEPTH;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.send_sgl_phys = phy_addr;
	xrnic_dev->xrnic_mmap.send_sgl =
		(void *)(uintptr_t)get_virt_addr(phy_addr);

	memset(xrnic_dev->xrnic_mmap.send_sgl, 0, len);
	DEBUG_LOG("xrnic memory Send SGL Base Addr = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.send_sgl_phys);

#else /* EXPERIMENTAL_CODE */
	ret = of_address_to_resource(np, XRNIC_SEND_SGL_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 1.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_SEND_SGL_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));

	DEBUG_LOG("xrnic memory send sgl 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.send_sgl_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.send_sgl = (void *)virt_addr;
#endif /* EXPERIMENTAL_CODE */

	DEBUG_LOG("send SGL physical address :%llx\n",
		  xrnic_dev->xrnic_mmap.send_sgl_phys);
	DEBUG_LOG("xrnic mmap:%p\n", &xrnic_dev->xrnic_mmap);

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_SQ_DEPTH * sizeof(struct xrnic_cqe);
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.cq_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.cq_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.cq_ba, 0, len);
	DEBUG_LOG("xrnic memory CQ BA Base Addr = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.cq_ba_phys);

#else
	ret = of_address_to_resource(np, XRNIC_CQ_BA_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 2.\n");
		goto mem_config_err;
	}
	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_CQ_BA_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory send CQ 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.cq_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.cq_ba = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_RECV_PKT_SIZE * XRNIC_RQ_DEPTH;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.rq_buf_ba_ca_phys = phy_addr;
	xrnic_dev->xrnic_mmap.rq_buf_ba_ca =
		(void *)(uintptr_t)get_virt_addr(phy_addr);

	memset(xrnic_dev->xrnic_mmap.rq_buf_ba_ca, 0, len);
	DEBUG_LOG("xrnic memory Receive Q Buffer = %#x, %llx\n",
		  val, xrnic_dev->xrnic_mmap.rq_buf_ba_ca_phys);

#else /* EXPERIMENTAL_CODE */
	ret = of_address_to_resource(np, XRNIC_RQ_BUF_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 3.\n");
		goto mem_config_err;
	}
	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_RQ_BUF_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory receive Q Buf 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.rq_buf_ba_ca_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.rq_buf_ba_ca = (void *)virt_addr;
#endif /* EXPERIMENTAL_CODE */

#ifdef EXPERIMENTAL_CODE
	len = XRNIC_SEND_PKT_SIZE * XRNIC_SQ_DEPTH;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.sq_ba_phys = phy_addr;
	xrnic_dev->xrnic_mmap.sq_ba =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.sq_ba, 0, len);
	DEBUG_LOG("xrnic memory Send Q Base Addr = %#x, %llx.\n",
		  val, xrnic_dev->xrnic_mmap.sq_ba_phys);
#else
	ret = of_address_to_resource(np, XRNIC_SQ_BA_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 4.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_SQ_BA_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory SEND Q 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.sq_ba_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.sq_ba = (struct wr *)virt_addr;
#endif
#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.rq_wrptr_db_add_phys = phy_addr;
	xrnic_dev->xrnic_mmap.rq_wrptr_db_add =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.rq_wrptr_db_add, 0, len);
#else
	ret = of_address_to_resource(np, XRNIC_RQWR_PTR_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 11.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_RQWR_PTR_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory RQ WPTR 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.rq_wrptr_db_add_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.rq_wrptr_db_add = (void *)virt_addr;
#endif

#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.sq_cmpl_db_add_phys = phy_addr;
	xrnic_dev->xrnic_mmap.sq_cmpl_db_add =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.sq_cmpl_db_add, 0, len);
#else
	ret = of_address_to_resource(np, XRNIC_SQ_CMPL_NODE, &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 12.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_SQ_CMPL_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory SQ CMPL 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG("bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.sq_cmpl_db_add_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.sq_cmpl_db_add = (void *)virt_addr;
#endif
#ifdef EXPERIMENTAL_CODE
	len = 0x100;
	phy_addr = alloc_mem(NULL, len);
	if (phy_addr == -ENOMEM) {
		ret = -ENOMEM;
		goto mem_config_err;
	}
	xrnic_dev->xrnic_mmap.stat_rq_buf_ca_phys = phy_addr;
	xrnic_dev->xrnic_mmap.stat_rq_buf_ca =
		(void *)(uintptr_t)get_virt_addr(phy_addr);
	memset(xrnic_dev->xrnic_mmap.stat_rq_buf_ca, 0, len);
#else
	ret = of_address_to_resource(np, XRNIC_STAT_XRNIC_RQ_BUF_NODE,
				     &resource);
	if (ret < 0) {
		pr_err("xrnic can't find resource 13.\n");
		goto mem_config_err;
	}

	start_addr = (unsigned int)resource.start;
	virt_addr = of_iomap(np, XRNIC_STAT_XRNIC_RQ_BUF_NODE);
	memset((void *)virt_addr, 0, ((u32)resource.end -
	       (u32)resource.start + 1));
	DEBUG_LOG("xrnic memory STAT RQ BUF 0x%llx of size=%x",
		  start_addr, (u32)resource.end - (u32)resource.start);
	DEBUG_LOG(" bytes mapped at 0x%p\n", virt_addr);

	xrnic_dev->xrnic_mmap.stat_rq_buf_ca_phys = (u64)start_addr;
	xrnic_dev->xrnic_mmap.stat_rq_buf_ca = (void *)virt_addr;
#endif
	xrnic_dev->io_qp_count = XRNIC_MAX_QP_SUPPORT;
	/* XRNIC controller H/W configuration which includes XRNIC
	 * global configuration, QP1 initialization and interrupt enable.
	 */
	ret = xrnic_ctrl_hw_init();
	if (ret < 0) {
		pr_err("xrnic hw init failed.\n");
		goto mem_config_err;
	}
	/* TODO: Currently, ERNIC IP is exporting 8 interrupt lines in DTS.
	 * But, IP will assert only first interrupt line for all 8 lines.
	 * Internally, all 8 lines are logically ORed and given as
	 * Single interrupt with interrupt status register showing which
	 * line is asserted. So, we are parsing just the 0th index of irq_map
	 * from DTS and in interrupt handler routine, we are reading the
	 * interrupt status register to identify which interrupt is asserted.
	 *
	 * Need to fix the design to export only 1 interrupt line in DTS.
	 */
	xrnic_dev->xrnic_irq = irq_of_parse_and_map(np, 0);
	if (!xrnic_dev->xrnic_irq) {
		pr_err("xrnic can't determine irq.\n");
		ret = XRNIC_FAILED;
	}
	status = request_irq(xrnic_dev->xrnic_irq, xrnic_irq_handler, 0,
			     "xrnic_irq", xrnic_dev);
	if (status) {
		pr_err("XRNIC irq request handler failed\n");
		goto err_irq;
	}

	tasklet_init(&xrnic_dev->mad_pkt_recv_task,
		     xrnic_mad_pkt_recv_intr_handler,
		     (unsigned long)xrnic_dev);
	tasklet_init(&xrnic_dev->qp_pkt_recv_task,
		     xrnic_qp_pkt_recv_intr_handler, (unsigned long)xrnic_dev);
	tasklet_init(&xrnic_dev->qp_fatal_task,
		     xrnic_qp_fatal_handler, (unsigned long)xrnic_dev);
	tasklet_init(&xrnic_dev->wqe_completed_task,
		     xrnic_wqe_completed_intr_handler,
		     (unsigned long)xrnic_dev);
	INIT_LIST_HEAD(&cm_id_list);

	return XRNIC_SUCCESS;
err_irq:
mem_config_err:
/* free_mem() works on only valid physical address returned from alloc_mem(),
 * and ignores if NULL or invalid address is passed.
 * So, even if any of the above allocations fail in the middle,
 * we can safely call free_mem() on all addresses.
 *
 * we are using carve-out memory for the requirements of ERNIC.
 * so, we cannot use devm_kzalloc() as kernel cannot see these
 * memories until ioremapped.
 */
	iounmap(xrnic_dev->xrnic_mmap.xrnic_regs);
	free_mem(xrnic_dev->xrnic_mmap.send_sgl_phys);
	free_mem(xrnic_dev->xrnic_mmap.cq_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.rq_buf_ba_ca_phys);
	free_mem(xrnic_dev->xrnic_mmap.sq_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.tx_hdr_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.tx_sgl_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.bypass_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.err_pkt_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.out_errsts_q_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.in_errsts_q_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.rq_wrptr_db_add_phys);
	free_mem(xrnic_dev->xrnic_mmap.sq_cmpl_db_add_phys);
	free_mem(xrnic_dev->xrnic_mmap.stat_rq_buf_ca_phys);
	free_mem(xrnic_dev->xrnic_mmap.data_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba_phys);

dev_failed:
	/* Remove the cdev */
	cdev_del(&xrnic_dev->cdev);

	/* Remove the device node entry */
	device_destroy(xrnic_class, xrnic_dev_number);

cdev_failed:
	/* Destroy xrnic_class */
	class_destroy(xrnic_class);

class_failed:
	/* Release the major number */
	unregister_chrdev_region(MAJOR(xrnic_dev_number), 1);

alloc_failed:
	kfree(xrnic_dev);
	return ret;
}

static int xernic_remove(struct platform_device *pdev)
{
/* TODO: Not using pdev. Rather using a global data structure xrnic_dev,
 * which is shared among all the objects in ernic driver.
 * Need to get xrnic_dev from platform_device pointer.
 */
	iounmap(xrnic_dev->xrnic_mmap.xrnic_regs);
	free_mem(xrnic_dev->xrnic_mmap.send_sgl_phys);
	free_mem(xrnic_dev->xrnic_mmap.cq_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.rq_buf_ba_ca_phys);
	free_mem(xrnic_dev->xrnic_mmap.sq_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.tx_hdr_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.tx_sgl_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.bypass_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.err_pkt_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.out_errsts_q_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.in_errsts_q_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.rq_wrptr_db_add_phys);
	free_mem(xrnic_dev->xrnic_mmap.sq_cmpl_db_add_phys);
	free_mem(xrnic_dev->xrnic_mmap.stat_rq_buf_ca_phys);
	free_mem(xrnic_dev->xrnic_mmap.data_buf_ba_phys);
	free_mem(xrnic_dev->xrnic_mmap.resp_err_pkt_buf_ba_phys);

	cdev_del(&xrnic_dev->cdev);
	device_destroy(xrnic_class, xrnic_dev_number);
	cdev_del(&xrnic_dev->cdev);
	unregister_chrdev_region(MAJOR(xrnic_dev_number), 1);
	free_irq(xrnic_dev->xrnic_irq, xrnic_dev);
	kfree(xrnic_dev);
	class_destroy(xrnic_class);
	unregister_inetaddr_notifier(&cmac_inetaddr_notifier);
	unregister_inet6addr_notifier(&cmac_inet6addr_notifier);

	return 0;
}

static const struct of_device_id xernic_of_match[] = {
	{ .compatible = "xlnx,ernic-1.0", },
	{ /* end of table*/ }
};
MODULE_DEVICE_TABLE(of, xernic_of_match);

static struct platform_driver xernic_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = xernic_of_match,
	},
	.probe = xernic_probe,
	.remove = xernic_remove,
};

module_platform_driver(xernic_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xilinx RNIC driver");
MODULE_AUTHOR("Sandeep Dhanvada");
