// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx FPGA Xilinx TSN Switch Controller driver.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Saurabh Sengar <saurabhs@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/of_net.h>
#include "xilinx_tsn_switch.h"

static struct miscdevice switch_dev;
static struct device_node *ep_node;
struct axienet_local lp;
static struct axienet_local *ep_lp;
static u8 en_hw_addr_learning;
static u8 sw_mac_addr[ETH_ALEN];

#define DELAY_OF_FIVE_MILLISEC			(5 * DELAY_OF_ONE_MILLISEC)

#define ADD					1
#define DELETE					0

#define PMAP_EGRESS_QUEUE_MASK			0x7
#define PMAP_EGRESS_QUEUE0_SELECT		0x0
#define PMAP_EGRESS_QUEUE1_SELECT		0x1
#define PMAP_EGRESS_QUEUE2_SELECT		0x2
#define SDL_EN_CAM_IPV_SHIFT			28
#define SDL_CAM_IPV_SHIFT			29

#define SDL_CAM_WR_ENABLE			BIT(0)
#define SDL_CAM_ADD_ENTRY			0x3
#define SDL_CAM_DELETE_ENTRY			0x5
#define SDL_CAM_READ_KEY_ENTRY			0x1
#define SDL_CAM_READ_ENTRY			GENMASK(2, 0)
#define SDL_CAM_VLAN_SHIFT			16
#define SDL_CAM_VLAN_MASK			0xFFF
#define SDL_CAM_IPV_MASK			0x7
#define SDL_CAM_PORT_LIST_SHIFT			8
#define SDL_GATEID_SHIFT			16
#define SDL_CAM_EP_MGMTQ_EN			BIT(15)
#define SDL_CAM_FWD_TO_EP			BIT(0)
#define SDL_CAM_FWD_TO_PORT_1			BIT(1)
#define SDL_CAM_FWD_TO_PORT_2			BIT(2)
#define SDL_CAM_EP_ACTION_LIST_SHIFT		0
#define SDL_CAM_MAC_ACTION_LIST_SHIFT		4
#define SDL_CAM_DEST_MAC_XLATION		BIT(0)
#define SDL_CAM_VLAN_ID_XLATION			BIT(1)
#define SDL_CAM_UNTAG_FRAME			BIT(2)
#define SDL_CAM_TAG_FRAME			BIT(3)

#define PORT_MAC_ADDR_LSB_MASK			(0xF)
#define MAC2_PORT_MAC_ADDR_LSB_SHIFT		(20)
#define PORT_STATUS_MASK                       (0x7)
#define MAC2_PORT_STATUS_SHIFT                 (17)
#define MAC2_PORT_STATUS_CHG_BIT               BIT(16)
#define MAC1_PORT_MAC_ADDR_LSB_SHIFT		(12)
#define MAC1_PORT_STATUS_SHIFT                 (9)
#define MAC1_PORT_STATUS_CHG_BIT               BIT(8)
#define EP_PORT_STATUS_SHIFT                   (1)
#define EP_PORT_STATUS_CHG_BIT                 BIT(0)
#define EX_EP_PORT_STATUS_SHIFT			(25)
#define EX_EP_PORT_STATUS_CHG_BIT		BIT(24)

#define SDL_CAM_LEARNT_ENT_MAC2_SHIFT		(20)
#define SDL_CAM_LEARNT_ENT_MAC1_SHIFT		(8)
#define SDL_CAM_LEARNT_ENT_MASK			GENMASK(11, 0)
#define SDL_CAM_FOUND_BIT			BIT(7)
#define SDL_CAM_READ_KEY_ADDR_SHIFT		(8)

#define HW_ADDR_AGING_TIME_SHIFT		(8)
#define HW_ADDR_AGING_TIME_MASK			GENMASK(19, 0)
#define HW_ADDR_AGING_BIT			BIT(2)
#define HW_ADDR_LEARN_UNTAG_BIT			BIT(1)
#define HW_ADDR_LEARN_BIT			BIT(0)

#define PORT_VLAN_ID_SHIFT			(16)
#define PORT_VLAN_ID_MASK			(0xFFF)
#define PORT_VLAN_IPV_SHIFT			(13)
#define PORT_VLAN_EN_IPV_SHIFT			(12)
#define PORT_VLAN_WRITE				(0x3)
#define PORT_VLAN_READ				(0x1)
#define PORT_VLAN_WRITE_READ_EN_BIT		BIT(0)
#define PORT_VLAN_PORT_LIST_VALID_BIT		BIT(3)
#define PORT_VLAN_HW_ADDR_LEARN_BIT		BIT(9)
#define PORT_VLAN_HW_ADDR_AGING_BIT		BIT(11)
#define PORT_VLAN_HW_ADDR_AGING_TIME_SHIFT	(12)

#define NATIVE_MAC1_PCP_SHIFT			(13)
#define NATIVE_MAC2_VLAN_SHIFT			(16)
#define NATIVE_MAC2_PCP_SHIFT			(29)

#define DEFAULT_PVID		1
#define DEFAULT_FWD_ALL		GENMASK(2, 0)

/* Match table for of_platform binding */
static const struct of_device_id tsnswitch_of_match[] = {
	{ .compatible = "xlnx,tsn-switch", },
	{},
};

MODULE_DEVICE_TABLE(of, tsnswitch_of_match);

static int switch_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int switch_release(struct inode *inode, struct file *file)
{
	return 0;
}

/* set_frame_filter_option Frame Filtering Type Field Options */
static void set_frame_filter_opt(u16 type1, u16 type2)
{
	int type = axienet_ior(&lp, XAS_FRM_FLTR_TYPE_FIELD_OPT_OFFSET);

	if (type1)
		type = (type & 0x0000FFFF) | (type1 << 16);
	if (type2)
		type = (type & 0xFFFF0000) | type2;
	axienet_iow(&lp, XAS_FRM_FLTR_TYPE_FIELD_OPT_OFFSET, type);
}

/* MAC Port-1 Management Queueing Options */
static void set_mac1_mngmntq(u32 config)
{
	axienet_iow(&lp, XAS_MAC1_MNG_Q_OPTION_OFFSET, config);
}

/* MAC Port-2 Management Queueing Options */
static void set_mac2_mngmntq(u32 config)
{
	axienet_iow(&lp, XAS_MNG_Q_CTRL_OFFSET, config);
}

/**
 * set_switch_regs -  read the various status of switch
 * @data:	Pointer which will be writen to switch
 */
static void set_switch_regs(struct switch_data *data)
{
	int tmp;
	u8 mac_addr[6];

	axienet_iow(&lp, XAS_CONTROL_OFFSET, data->switch_ctrl);
	axienet_iow(&lp, XAS_PMAP_OFFSET, data->switch_prt);
	mac_addr[0] = data->sw_mac_addr[0];
	mac_addr[1] = data->sw_mac_addr[1];
	mac_addr[2] = data->sw_mac_addr[2];
	mac_addr[3] = data->sw_mac_addr[3];
	mac_addr[4] = data->sw_mac_addr[4];
	mac_addr[5] = data->sw_mac_addr[5];
	axienet_iow(&lp, XAS_MAC_LSB_OFFSET,
		    (mac_addr[0] << 24) | (mac_addr[1] << 16) |
		    (mac_addr[2] << 8)  | (mac_addr[3]));
	axienet_iow(&lp, XAS_MAC_MSB_OFFSET, (mac_addr[4] << 8) | mac_addr[5]);

	/* Threshold */
	tmp = (data->thld_ep_mac[0].t1 << 16) | data->thld_ep_mac[0].t2;
	axienet_iow(&lp, XAS_EP2MAC_ST_FIFOT_OFFSET, tmp);

	tmp = (data->thld_ep_mac[1].t1 << 16) | data->thld_ep_mac[1].t2;
	axienet_iow(&lp, XAS_EP2MAC_RE_FIFOT_OFFSET, tmp);

	tmp = (data->thld_ep_mac[2].t1 << 16) | data->thld_ep_mac[2].t2;
	axienet_iow(&lp, XAS_EP2MAC_BE_FIFOT_OFFSET, tmp);

	tmp = (data->thld_mac_mac[0].t1 << 16) | data->thld_mac_mac[0].t2;
	axienet_iow(&lp, XAS_MAC2MAC_ST_FIFOT_OFFSET, tmp);

	tmp = (data->thld_mac_mac[1].t1 << 16) | data->thld_mac_mac[1].t2;
	axienet_iow(&lp, XAS_MAC2MAC_RE_FIFOT_OFFSET, tmp);

	tmp = (data->thld_mac_mac[2].t1 << 16) | data->thld_mac_mac[2].t2;
	axienet_iow(&lp, XAS_MAC2MAC_BE_FIFOT_OFFSET, tmp);

	/* Port VLAN ID */
	axienet_iow(&lp, XAS_EP_PORT_VLAN_OFFSET, data->ep_vlan);
	axienet_iow(&lp, XAS_MAC_PORT_VLAN_OFFSET, data->mac_vlan);

	/* max frame size */
	axienet_iow(&lp, XAS_ST_MAX_FRAME_SIZE_OFFSET, data->max_frame_sc_que);
	axienet_iow(&lp, XAS_RE_MAX_FRAME_SIZE_OFFSET, data->max_frame_res_que);
	axienet_iow(&lp, XAS_BE_MAX_FRAME_SIZE_OFFSET, data->max_frame_be_que);
}

/**
 * get_switch_regs -  read the various status of switch
 * @data:	Pointer which will return the switch status
 */
static void get_switch_regs(struct switch_data *data)
{
	int tmp;

	data->switch_status = axienet_ior(&lp, XAS_STATUS_OFFSET);
	data->switch_ctrl = axienet_ior(&lp, XAS_CONTROL_OFFSET);
	data->switch_prt = axienet_ior(&lp, XAS_PMAP_OFFSET);
	tmp = axienet_ior(&lp, XAS_MAC_LSB_OFFSET);
	data->sw_mac_addr[0] = (tmp & 0xFF000000) >> 24;
	data->sw_mac_addr[1] = (tmp & 0xFF0000) >> 16;
	data->sw_mac_addr[2] = (tmp & 0xFF00) >> 8;
	data->sw_mac_addr[3] = (tmp & 0xFF);
	tmp = axienet_ior(&lp, XAS_MAC_MSB_OFFSET);
	data->sw_mac_addr[4] = (tmp & 0xFF00) >> 8;
	data->sw_mac_addr[5] = (tmp & 0xFF);

	/* Threshold */
	tmp = axienet_ior(&lp, XAS_EP2MAC_ST_FIFOT_OFFSET);
	data->thld_ep_mac[0].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_ep_mac[0].t2 = tmp & (0xFFFF);

	tmp = axienet_ior(&lp, XAS_EP2MAC_RE_FIFOT_OFFSET);
	data->thld_ep_mac[1].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_ep_mac[1].t2 = tmp & (0xFFFF);

	tmp = axienet_ior(&lp, XAS_EP2MAC_BE_FIFOT_OFFSET);
	data->thld_ep_mac[2].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_ep_mac[2].t2 = tmp & (0xFFFF);

	tmp = axienet_ior(&lp, XAS_MAC2MAC_ST_FIFOT_OFFSET);
	data->thld_mac_mac[0].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_mac_mac[0].t2 = tmp & (0xFFFF);

	tmp = axienet_ior(&lp, XAS_MAC2MAC_RE_FIFOT_OFFSET);
	data->thld_mac_mac[1].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_mac_mac[1].t2 = tmp & (0xFFFF);

	tmp = axienet_ior(&lp, XAS_MAC2MAC_BE_FIFOT_OFFSET);
	data->thld_mac_mac[2].t1 = ((tmp >> 16) & 0xFFFF);
	data->thld_mac_mac[2].t2 = tmp & (0xFFFF);

	/* Port VLAN ID */
	data->ep_vlan = axienet_ior(&lp, XAS_EP_PORT_VLAN_OFFSET);
	data->mac_vlan = axienet_ior(&lp, XAS_MAC_PORT_VLAN_OFFSET);

	/* max frame size */
	data->max_frame_sc_que = (axienet_ior(&lp,
				XAS_ST_MAX_FRAME_SIZE_OFFSET) & 0xFFFF);
	data->max_frame_res_que = (axienet_ior(&lp,
				XAS_RE_MAX_FRAME_SIZE_OFFSET) & 0xFFFF);
	data->max_frame_be_que = (axienet_ior(&lp,
				XAS_BE_MAX_FRAME_SIZE_OFFSET) & 0xFFFF);

	/* frame filter type options*/
	tmp = axienet_ior(&lp, XAS_FRM_FLTR_TYPE_FIELD_OPT_OFFSET);
	data->typefield.type2 = (tmp & 0xFFFF0000) >> 16;
	data->typefield.type2 = tmp & 0x0000FFFF;

	/* MAC Port 1 Management Q option*/
	data->mac1_config = axienet_ior(&lp, XAS_MAC1_MNG_Q_OPTION_OFFSET);
	/* MAC Port 2 Management Q option*/
	data->mac2_config = axienet_ior(&lp, XAS_MNG_Q_CTRL_OFFSET);

	/* Port VLAN Membership control*/
	data->port_vlan_mem_ctrl = axienet_ior(&lp, XAS_VLAN_MEMB_CTRL_REG);
	/* Port VLAN Membership read data*/
	data->port_vlan_mem_data = axienet_ior(&lp, XAS_VLAN_MEMB_DATA_REG);
}

int tsn_switch_get_port_parent_id(struct net_device *dev, struct netdev_phys_item_id *ppid)
{
	u8 *switchid;

	switchid = tsn_switch_get_id();
	ppid->id_len = ETH_ALEN;
	memcpy(&ppid->id, switchid, ppid->id_len);

	return 0;
}

/**
 * get_memory_static_counter -  get memory static counters value
 * @data:	Value to be programmed
 */
static void get_memory_static_counter(struct switch_data *data)
{
	data->mem_arr_cnt.cam_lookup.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_CAM_LOOKUP);
	data->mem_arr_cnt.cam_lookup.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_CAM_LOOKUP + 0x4);

	data->mem_arr_cnt.multicast_fr.lsb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_MULTCAST);
	data->mem_arr_cnt.multicast_fr.msb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_MULTCAST + 0x4);

	data->mem_arr_cnt.err_mac1.lsb = axienet_ior(&lp,
						     XAS_MEM_STCNTR_ERR_MAC1);
	data->mem_arr_cnt.err_mac1.msb = axienet_ior(&lp,
						     XAS_MEM_STCNTR_ERR_MAC1 + 0x4);

	data->mem_arr_cnt.err_mac2.lsb = axienet_ior(&lp,
						     XAS_MEM_STCNTR_ERR_MAC2);
	data->mem_arr_cnt.err_mac2.msb = axienet_ior(&lp,
						     XAS_MEM_STCNTR_ERR_MAC2 + 0x4);

	data->mem_arr_cnt.sc_mac1_ep.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_MAC1_EP);
	data->mem_arr_cnt.sc_mac1_ep.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_MAC1_EP + 0x4);
	data->mem_arr_cnt.res_mac1_ep.lsb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_MAC1_EP);
	data->mem_arr_cnt.res_mac1_ep.msb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_MAC1_EP + 0x4);
	data->mem_arr_cnt.be_mac1_ep.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_MAC1_EP);
	data->mem_arr_cnt.be_mac1_ep.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_MAC1_EP + 0x4);
	data->mem_arr_cnt.err_sc_mac1_ep.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_MAC1_EP);
	data->mem_arr_cnt.err_sc_mac1_ep.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_MAC1_EP + 0x4);
	data->mem_arr_cnt.err_res_mac1_ep.lsb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_MAC1_EP);
	data->mem_arr_cnt.err_res_mac1_ep.msb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_MAC1_EP + 0x4);
	data->mem_arr_cnt.err_be_mac1_ep.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_MAC1_EP);
	data->mem_arr_cnt.err_be_mac1_ep.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_MAC1_EP + 0x4);

	data->mem_arr_cnt.sc_mac2_ep.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_MAC2_EP);
	data->mem_arr_cnt.sc_mac2_ep.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_MAC2_EP + 0x4);
	data->mem_arr_cnt.res_mac2_ep.lsb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_MAC2_EP);
	data->mem_arr_cnt.res_mac2_ep.msb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_MAC2_EP + 0x4);
	data->mem_arr_cnt.be_mac2_ep.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_MAC2_EP);
	data->mem_arr_cnt.be_mac2_ep.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_MAC2_EP + 0x4);
	data->mem_arr_cnt.err_sc_mac2_ep.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_MAC2_EP);
	data->mem_arr_cnt.err_sc_mac2_ep.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_MAC2_EP + 0x4);
	data->mem_arr_cnt.err_res_mac2_ep.lsb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_MAC2_EP);
	data->mem_arr_cnt.err_res_mac2_ep.msb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_MAC2_EP + 0x4);
	data->mem_arr_cnt.err_be_mac2_ep.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_MAC2_EP);
	data->mem_arr_cnt.err_be_mac2_ep.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_MAC2_EP + 0x4);

	data->mem_arr_cnt.sc_ep_mac1.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_EP_MAC1);
	data->mem_arr_cnt.sc_ep_mac1.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_EP_MAC1 + 0x4);
	data->mem_arr_cnt.res_ep_mac1.lsb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_EP_MAC1);
	data->mem_arr_cnt.res_ep_mac1.msb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_EP_MAC1 + 0x4);
	data->mem_arr_cnt.be_ep_mac1.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_EP_MAC1);
	data->mem_arr_cnt.be_ep_mac1.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_EP_MAC1 + 0x4);
	data->mem_arr_cnt.err_sc_ep_mac1.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_EP_MAC1);
	data->mem_arr_cnt.err_sc_ep_mac1.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_EP_MAC1 + 0x4);
	data->mem_arr_cnt.err_res_ep_mac1.lsb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_EP_MAC1);
	data->mem_arr_cnt.err_res_ep_mac1.msb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_EP_MAC1 + 0x4);
	data->mem_arr_cnt.err_be_ep_mac1.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_EP_MAC1);
	data->mem_arr_cnt.err_be_ep_mac1.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_EP_MAC1 + 0x4);

	data->mem_arr_cnt.sc_mac2_mac1.lsb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_SC_MAC2_MAC1);
	data->mem_arr_cnt.sc_mac2_mac1.msb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_SC_MAC2_MAC1 + 0x4);
	data->mem_arr_cnt.res_mac2_mac1.lsb = axienet_ior(&lp,
							  XAS_MEM_STCNTR_RES_MAC2_MAC1);
	data->mem_arr_cnt.res_mac2_mac1.msb = axienet_ior(&lp,
							  XAS_MEM_STCNTR_RES_MAC2_MAC1 + 0x4);
	data->mem_arr_cnt.be_mac2_mac1.lsb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_BE_MAC2_MAC1);
	data->mem_arr_cnt.be_mac2_mac1.msb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_BE_MAC2_MAC1 + 0x4);
	data->mem_arr_cnt.err_sc_mac2_mac1.lsb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_SC_MAC2_MAC1);
	data->mem_arr_cnt.err_sc_mac2_mac1.msb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_SC_MAC2_MAC1 + 0x4);
	data->mem_arr_cnt.err_res_mac2_mac1.lsb = axienet_ior(&lp,
							      XAS_MEM_STCNTR_ERR_RES_MAC2_MAC1);
	data->mem_arr_cnt.err_res_mac2_mac1.msb =
	axienet_ior(&lp, XAS_MEM_STCNTR_ERR_RES_MAC2_MAC1 + 0x4);
	data->mem_arr_cnt.err_be_mac2_mac1.lsb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_BE_MAC2_MAC1);
	data->mem_arr_cnt.err_be_mac2_mac1.msb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_BE_MAC2_MAC1 + 0x4);

	data->mem_arr_cnt.sc_ep_mac2.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_EP_MAC2);
	data->mem_arr_cnt.sc_ep_mac2.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_SC_EP_MAC2 + 0x4);
	data->mem_arr_cnt.res_ep_mac2.lsb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_EP_MAC2);
	data->mem_arr_cnt.res_ep_mac2.msb = axienet_ior(&lp,
							XAS_MEM_STCNTR_RES_EP_MAC2 + 0x4);
	data->mem_arr_cnt.be_ep_mac2.lsb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_EP_MAC2);
	data->mem_arr_cnt.be_ep_mac2.msb = axienet_ior(&lp,
						       XAS_MEM_STCNTR_BE_EP_MAC2 + 0x4);
	data->mem_arr_cnt.err_sc_ep_mac2.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_EP_MAC2);
	data->mem_arr_cnt.err_sc_ep_mac2.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_SC_EP_MAC2 + 0x4);
	data->mem_arr_cnt.err_res_ep_mac2.lsb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_EP_MAC2);
	data->mem_arr_cnt.err_res_ep_mac2.msb = axienet_ior(&lp,
							    XAS_MEM_STCNTR_ERR_RES_EP_MAC2 + 0x4);
	data->mem_arr_cnt.err_be_ep_mac2.lsb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_EP_MAC2);
	data->mem_arr_cnt.err_be_ep_mac2.msb = axienet_ior(&lp,
							   XAS_MEM_STCNTR_ERR_BE_EP_MAC2 + 0x4);

	data->mem_arr_cnt.sc_mac1_mac2.lsb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_SC_MAC1_MAC2);
	data->mem_arr_cnt.sc_mac1_mac2.msb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_SC_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.res_mac1_mac2.lsb = axienet_ior(&lp,
							  XAS_MEM_STCNTR_RES_MAC1_MAC2);
	data->mem_arr_cnt.res_mac1_mac2.msb = axienet_ior(&lp,
							  XAS_MEM_STCNTR_RES_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.be_mac1_mac2.lsb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_BE_MAC1_MAC2);
	data->mem_arr_cnt.be_mac1_mac2.msb = axienet_ior(&lp,
							 XAS_MEM_STCNTR_BE_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.err_sc_mac1_mac2.lsb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_SC_MAC1_MAC2);
	data->mem_arr_cnt.err_sc_mac1_mac2.msb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_SC_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.err_res_mac1_mac2.lsb = axienet_ior(&lp,
							      XAS_MEM_STCNTR_ERR_RES_MAC1_MAC2);
	data->mem_arr_cnt.err_res_mac1_mac2.msb =
	axienet_ior(&lp, XAS_MEM_STCNTR_ERR_RES_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.err_be_mac1_mac2.lsb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_BE_MAC1_MAC2);
	data->mem_arr_cnt.err_be_mac1_mac2.msb = axienet_ior(&lp,
							     XAS_MEM_STCNTR_ERR_BE_MAC1_MAC2 + 0x4);
}

int tsn_switch_cam_set(struct cam_struct data, u8 add)
{
	u32 port_action = 0;
	u32 tv2 = 0;
	u32 reg, err;
	u8 en_ipv = 0;

	err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_STATUS_OFFSET, reg,
				 (reg & SDL_CAM_WR_ENABLE), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM init timed out\n");
		return -ETIMEDOUT;
	}
	if (add && ((data.fwd_port & PORT_EX_ONLY) || (data.fwd_port & PORT_EX_EP))) {
		if (!(ep_lp->ex_ep)) {
			pr_err("Endpoint extension support is not present in this design\n");
			return -EINVAL;
		} else if ((data.fwd_port & PORT_EX_ONLY) &&
			    (data.fwd_port & PORT_EX_EP)) {
			if (!(ep_lp->packet_switch)) {
				pr_err("Support for forwarding packets from endpoint to extended endpoint or vice versa is not present in this design\n");
				return -EINVAL;
			}
		}
	}
	/* mac and vlan */
	axienet_iow(&lp, XAS_SDL_CAM_KEY1_OFFSET,
		    (data.dest_addr[0] << 24) | (data.dest_addr[1] << 16) |
		    (data.dest_addr[2] << 8)  | (data.dest_addr[3]));
	axienet_iow(&lp, XAS_SDL_CAM_KEY2_OFFSET,
		    ((data.dest_addr[4] << 8) | data.dest_addr[5]) |
		    ((data.vlanid & SDL_CAM_VLAN_MASK) << SDL_CAM_VLAN_SHIFT));

	/* Introduce wmb to preserve KEY2 and TV1 write order fix possible
	 * HW hang when KEY2 and TV1 registers are accessed sequentially.
	 * TODO: Check alternatives to barrier.
	 */
	wmb();
	/* TV 1 and TV 2 */
	axienet_iow(&lp, XAS_SDL_CAM_TV1_OFFSET,
		    (data.src_addr[0] << 24) | (data.src_addr[1] << 16) |
		    (data.src_addr[2] << 8)  | (data.src_addr[3]));

	tv2 = ((data.src_addr[4] << 8) | data.src_addr[5]) |
	       ((data.tv_vlanid & SDL_CAM_VLAN_MASK) << SDL_CAM_VLAN_SHIFT);

	if (data.flags & XAS_CAM_IPV_EN)
		en_ipv = 1;

	tv2 = tv2 | ((data.ipv & SDL_CAM_IPV_MASK) << SDL_CAM_IPV_SHIFT)
				| (en_ipv << SDL_EN_CAM_IPV_SHIFT);

	axienet_iow(&lp, XAS_SDL_CAM_TV2_OFFSET, tv2);

	if (data.fwd_port & PORT_EP)
		port_action = data.ep_port_act << SDL_CAM_EP_ACTION_LIST_SHIFT;
	if (data.fwd_port & PORT_MAC1 || data.fwd_port & PORT_MAC2)
		port_action |= data.mac_port_act <<
				SDL_CAM_MAC_ACTION_LIST_SHIFT;

	if (data.flags & XAS_CAM_EP_MGMTQ_EN)
		port_action |= SDL_CAM_EP_MGMTQ_EN;

	port_action = port_action | (data.fwd_port << SDL_CAM_PORT_LIST_SHIFT);

#if IS_ENABLED(CONFIG_XILINX_TSN_QCI) || IS_ENABLED(CONFIG_XILINX_TSN_CB)
	port_action = port_action | (data.gate_id << SDL_GATEID_SHIFT);
#endif

	/* port action */
	axienet_iow(&lp, XAS_SDL_CAM_PORT_ACT_OFFSET, port_action);

	if (add)
		axienet_iow(&lp, XAS_SDL_CAM_CTRL_OFFSET, SDL_CAM_ADD_ENTRY);
	else
		axienet_iow(&lp, XAS_SDL_CAM_CTRL_OFFSET, SDL_CAM_DELETE_ENTRY);

	/* wait for write to complete */
	err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_CTRL_OFFSET, reg,
				 (!(reg & SDL_CAM_WR_ENABLE)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM write timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void port_vlan_mem_ctrl(u32 port_vlan_mem)
{
		axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, port_vlan_mem);
}

static int read_cam_entry(struct cam_struct data, void __user *arg)
{
	u32 u_value, reg, err;

	/* wait for cam init done */
	err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_STATUS_OFFSET, reg,
				 (reg & SDL_CAM_WR_ENABLE), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM init timed out\n");
		return -ETIMEDOUT;
	}

	/* mac and vlan */
	axienet_iow(&lp, XAS_SDL_CAM_KEY1_OFFSET,
		    (data.dest_addr[0] << 24) | (data.dest_addr[1] << 16) |
		    (data.dest_addr[2] << 8)  | (data.dest_addr[3]));
	axienet_iow(&lp, XAS_SDL_CAM_KEY2_OFFSET,
		    ((data.dest_addr[4] << 8) | data.dest_addr[5]) |
		    ((data.vlanid & SDL_CAM_VLAN_MASK) << SDL_CAM_VLAN_SHIFT));
	axienet_iow(&lp, XAS_SDL_CAM_CTRL_OFFSET, SDL_CAM_READ_ENTRY);
	err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_CTRL_OFFSET, reg,
				 (!(reg & SDL_CAM_WR_ENABLE)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM write timed out\n");
		return -ETIMEDOUT;
	}
	u_value = axienet_ior(&lp, XAS_SDL_CAM_CTRL_OFFSET);

	if (u_value & SDL_CAM_FOUND_BIT) {
		u_value = axienet_ior(&lp, XAS_SDL_CAM_TV1_OFFSET);
		data.src_addr[0] = (u_value >> 24) & 0xFF;
		data.src_addr[1] = (u_value >> 16) & 0xFF;
		data.src_addr[2] = (u_value >> 8) & 0xFF;
		data.src_addr[3] = (u_value) & 0xFF;
		u_value = axienet_ior(&lp, XAS_SDL_CAM_TV2_OFFSET);
		data.src_addr[4] = (u_value >> 8) & 0xFF;
		data.src_addr[5] = (u_value) & 0xFF;
		data.tv_vlanid   = (u_value >> SDL_CAM_VLAN_SHIFT)
					& SDL_CAM_VLAN_MASK;
		data.ipv = (u_value >> SDL_CAM_IPV_SHIFT) & SDL_CAM_IPV_MASK;
		if ((u_value >> SDL_EN_CAM_IPV_SHIFT) & 0x1)
			data.flags |= XAS_CAM_IPV_EN;
		u_value = axienet_ior(&lp, XAS_SDL_CAM_PORT_ACT_OFFSET);
		if (ep_lp->ex_ep)
			data.fwd_port = (u_value >> SDL_CAM_PORT_LIST_SHIFT) & 0x1F;
		else
			data.fwd_port = (u_value >> SDL_CAM_PORT_LIST_SHIFT) & 0x7;
		data.ep_port_act = (u_value >> SDL_CAM_EP_ACTION_LIST_SHIFT)
					& 0xF;
		data.mac_port_act = (u_value >> SDL_CAM_MAC_ACTION_LIST_SHIFT)
					& 0xF;
		data.gate_id = (u_value >> SDL_GATEID_SHIFT) & 0xFF;
		data.flags |= XAS_CAM_VALID;
	} else {
		data.flags &= ~XAS_CAM_VALID;
	}
	if (copy_to_user(arg, &data, sizeof(struct cam_struct)))
		return -EFAULT;

	return 0;
}

static int set_mac_addr_learn(void __user *arg)
{
	struct mac_addr_learn mac_learn;
	u32 u_value;

	if (copy_from_user(&mac_learn, arg, sizeof(struct mac_addr_learn)))
		return -EFAULT;

	u_value = axienet_ior(&lp, XAS_HW_ADDR_LEARN_CTRL_OFFSET);
	if (mac_learn.aging_time) {
		u_value &= ~(HW_ADDR_AGING_TIME_MASK <<
				HW_ADDR_AGING_TIME_SHIFT);
		u_value |= (mac_learn.aging_time << HW_ADDR_AGING_TIME_SHIFT);
	}
	if (mac_learn.is_age) {
		if (!mac_learn.aging)
			u_value |= HW_ADDR_AGING_BIT;
		else
			u_value &= ~HW_ADDR_AGING_BIT;
	}
	if (mac_learn.is_learn) {
		if (!mac_learn.learning)
			u_value |= HW_ADDR_LEARN_BIT;
		else
			u_value &= ~HW_ADDR_LEARN_BIT;
	}
	if (mac_learn.is_untag) {
		if (mac_learn.learn_untag)
			u_value |= HW_ADDR_LEARN_UNTAG_BIT;
		else
			u_value &= ~HW_ADDR_LEARN_UNTAG_BIT;
	}
	axienet_iow(&lp, XAS_HW_ADDR_LEARN_CTRL_OFFSET, u_value);

	return 0;
}

static int get_mac_addr_learn(void __user *arg)
{
	struct mac_addr_learn mac_learn;
	u32 u_value;

	u_value = axienet_ior(&lp, XAS_HW_ADDR_LEARN_CTRL_OFFSET);
	mac_learn.aging_time = (u_value >> HW_ADDR_AGING_TIME_SHIFT) &
				HW_ADDR_AGING_TIME_MASK;
	mac_learn.aging = u_value & HW_ADDR_AGING_BIT;
	mac_learn.learning = u_value & HW_ADDR_LEARN_BIT;
	mac_learn.learn_untag = u_value & HW_ADDR_LEARN_UNTAG_BIT;

	u_value = axienet_ior(&lp, XAS_PORT_STATE_CTRL_OFFSET);

	if (copy_to_user(arg, &mac_learn, sizeof(struct mac_addr_learn)))
		return -EFAULT;

	return 0;
}

static int get_mac_addr_learnt_list(void __user *arg)
{
	struct mac_addr_list *mac_list;
	u32 i = 0;
	u32 u_value, reg, err;
	u16 read_key_addr = 0;
	int ret = 0;

	mac_list = kzalloc(sizeof(*mac_list), GFP_KERNEL);
	if (!mac_list) {
		ret = -ENOMEM;
		goto ret_status;
	}

	if (copy_from_user(mac_list, arg, sizeof(u8))) {
		ret = -EFAULT;
		goto free_mac_list;
	}
	/* wait for cam init done */
	err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_STATUS_OFFSET, reg,
				 (reg & SDL_CAM_WR_ENABLE), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM init timed out\n");
		ret = -ETIMEDOUT;
		goto free_mac_list;
	}
	u_value = axienet_ior(&lp, XAS_SDL_CAM_STATUS_OFFSET);

	if (mac_list->port_num == PORT_MAC1) {
		mac_list->num_list = (u_value >> SDL_CAM_LEARNT_ENT_MAC1_SHIFT)
					& SDL_CAM_LEARNT_ENT_MASK;
	} else {
		mac_list->num_list = (u_value >> SDL_CAM_LEARNT_ENT_MAC2_SHIFT)
					& SDL_CAM_LEARNT_ENT_MASK;
		read_key_addr = 0x800;
	}

	for (i = 0; i < MAX_NUM_MAC_ENTRIES ; i++) {
		err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_STATUS_OFFSET, reg,
					 (reg & SDL_CAM_WR_ENABLE), 10,
					 DELAY_OF_FIVE_MILLISEC);
		if (err) {
			pr_err("CAM init timed out\n");
			ret = -ETIMEDOUT;
			goto free_mac_list;
		}

		u_value = ((read_key_addr + i) << SDL_CAM_READ_KEY_ADDR_SHIFT)
			  | SDL_CAM_READ_KEY_ENTRY;
		axienet_iow(&lp, XAS_SDL_CAM_CTRL_OFFSET, u_value);

		err = readl_poll_timeout(lp.regs + XAS_SDL_CAM_CTRL_OFFSET, reg,
					 (!(reg & SDL_CAM_WR_ENABLE)), 10,
					 DELAY_OF_FIVE_MILLISEC);
		if (err) {
			pr_err("CAM write timed out\n");
			ret = -ETIMEDOUT;
			goto free_mac_list;
		}
		u_value = axienet_ior(&lp, XAS_SDL_CAM_CTRL_OFFSET);

		if (u_value & SDL_CAM_FOUND_BIT) {
			u_value = axienet_ior(&lp, XAS_SDL_CAM_KEY1_OFFSET);
			mac_list->list[i].mac_addr[0] = (u_value >> 24) & 0xFF;
			mac_list->list[i].mac_addr[1] = (u_value >> 16) & 0xFF;
			mac_list->list[i].mac_addr[2] = (u_value >> 8) & 0xFF;
			mac_list->list[i].mac_addr[3] = (u_value) & 0xFF;
			u_value = axienet_ior(&lp, XAS_SDL_CAM_KEY2_OFFSET);
			mac_list->list[i].mac_addr[4] = (u_value >> 8) & 0xFF;
			mac_list->list[i].mac_addr[5] = (u_value) & 0xFF;
			mac_list->list[i].vlan_id     = (u_value >> 16) & 0xFFF;
		}
	}
	if (copy_to_user(arg, mac_list, sizeof(struct mac_addr_list))) {
		ret = -EFAULT;
		goto free_mac_list;
	}

free_mac_list:
	kfree(mac_list);
ret_status:
	return ret;
}

int tsn_switch_set_stp_state(struct port_status *port)
{
	u32 u_value, reg, err;
	u32 en_port_sts_chg_bit = 1;

	u_value = axienet_ior(&lp, XAS_PORT_STATE_CTRL_OFFSET);
	switch (port->port_num) {
	case PORT_EP:
		if (!(u_value & EP_PORT_STATUS_CHG_BIT)) {
			u_value &= ~(PORT_STATUS_MASK <<
					EP_PORT_STATUS_SHIFT);
			u_value |= (port->port_status << EP_PORT_STATUS_SHIFT);
			en_port_sts_chg_bit = EP_PORT_STATUS_CHG_BIT;
		}
		break;
	case PORT_MAC1:
		if (!(u_value & MAC1_PORT_STATUS_CHG_BIT)) {
			u_value &= ~(PORT_STATUS_MASK <<
					MAC1_PORT_STATUS_SHIFT);
			u_value |= (port->port_status <<
					MAC1_PORT_STATUS_SHIFT);
			en_port_sts_chg_bit = MAC1_PORT_STATUS_CHG_BIT;
		}
		break;
	case PORT_MAC2:
		if (!(u_value & MAC2_PORT_STATUS_CHG_BIT)) {
			u_value &= ~(PORT_STATUS_MASK <<
					MAC2_PORT_STATUS_SHIFT);
			u_value |= (port->port_status <<
					MAC2_PORT_STATUS_SHIFT);
			en_port_sts_chg_bit = MAC2_PORT_STATUS_CHG_BIT;
		}
		break;
	case PORT_EX_ONLY:
		if (!(ep_lp->ex_ep))
			return -EINVAL;
		if (!(u_value & EX_EP_PORT_STATUS_CHG_BIT)) {
			u_value &= ~(PORT_STATUS_MASK << EX_EP_PORT_STATUS_SHIFT);
			u_value |= (port->port_status << EX_EP_PORT_STATUS_SHIFT);
			en_port_sts_chg_bit = EX_EP_PORT_STATUS_CHG_BIT;
		}
		break;
	}

	u_value |= en_port_sts_chg_bit;
	axienet_iow(&lp, XAS_PORT_STATE_CTRL_OFFSET, u_value);

	/* wait for write to complete */
	err = readl_poll_timeout(lp.regs + XAS_PORT_STATE_CTRL_OFFSET, reg,
				 (!(reg & en_port_sts_chg_bit)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("CAM write timed out\n");
		return -ETIMEDOUT;
	}
	u_value = axienet_ior(&lp, XAS_PORT_STATE_CTRL_OFFSET);

	return 0;
}

static int set_port_status(void __user *arg)
{
	struct port_status port;

	if (copy_from_user(&port, arg, sizeof(struct port_status)))
		return -EFAULT;

	return tsn_switch_set_stp_state(&port);
}

static int get_port_status(void __user *arg)
{
	struct port_status port;
	u32 u_value;

	if (copy_from_user(&port, arg, sizeof(struct port_status)))
		return -EINVAL;

	u_value = axienet_ior(&lp, XAS_PORT_STATE_CTRL_OFFSET);
	switch (port.port_num) {
	case PORT_EP:
		port.port_status = (u_value >> EP_PORT_STATUS_SHIFT) &
					PORT_STATUS_MASK;
		break;
	case PORT_MAC1:
		port.port_status = (u_value >> MAC1_PORT_STATUS_SHIFT) &
					PORT_STATUS_MASK;
		break;
	case PORT_MAC2:
		port.port_status = (u_value >> MAC2_PORT_STATUS_SHIFT) &
					PORT_STATUS_MASK;
		break;
	case PORT_EX_ONLY:
		if (!(ep_lp->ex_ep))
			return -EINVAL;
		port.port_status = (u_value >> EX_EP_PORT_STATUS_SHIFT) & PORT_STATUS_MASK;
		break;
	}

	if (copy_to_user(arg, &port, sizeof(struct port_status)))
		return -EINVAL;

	return 0;
}

u8 *tsn_switch_get_id(void)
{
	return sw_mac_addr;
}

int tsn_switch_vlan_add(struct port_vlan *port, int add)
{
	u32 u_value, u_value1, reg, err;
	u8 learning = 0;

	u_value1 = axienet_ior(&lp, XAS_VLAN_MEMB_DATA_REG);
	learning = (u_value1 & PORT_VLAN_HW_ADDR_LEARN_BIT) ? 0 : 1;
	if (learning) {
		if (port->en_ipv == 1) {
			pr_err("When hardware address learning is enabled for a VLAN, TSN streams cannot be mapped to a particular priority queue\n");
			return -EPERM;
		}
	} else {
		if (port->en_port_status == 1) {
			pr_err("When hardware address learning is disabled for a VLAN, port status cannot be set per VLAN\n");
			return -EPERM;
		}
	}
	u_value = ((port->vlan_id & PORT_VLAN_ID_MASK) << PORT_VLAN_ID_SHIFT)
			| PORT_VLAN_READ;
	axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, u_value);

	/* wait for port vlan write completion */
	err = readl_poll_timeout(lp.regs + XAS_VLAN_MEMB_CTRL_REG, reg,
				 (!(reg & PORT_VLAN_WRITE_READ_EN_BIT)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("Port vlan write timed out\n");
		return -ETIMEDOUT;
	}

	if (add)
		u_value1 |= PORT_VLAN_PORT_LIST_VALID_BIT | port->port_num;
	else
		u_value1 &= ~(port->port_num);
	axienet_iow(&lp, XAS_VLAN_MEMB_DATA_REG, u_value1);

	u_value = ((port->vlan_id & PORT_VLAN_ID_MASK) << PORT_VLAN_ID_SHIFT)
			| PORT_VLAN_WRITE;
	if (port->en_port_status == 1) {
		u_value |= ((port->port_status & SDL_CAM_IPV_MASK) << PORT_VLAN_IPV_SHIFT)
				| (port->en_port_status << PORT_VLAN_EN_IPV_SHIFT);
	}
	if (port->en_ipv == 1) {
		u_value |= ((port->ipv & SDL_CAM_IPV_MASK) << PORT_VLAN_IPV_SHIFT)
				| (port->en_ipv << PORT_VLAN_EN_IPV_SHIFT);
	}
	axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, u_value);

	/* wait for port vlan write completion */
	err = readl_poll_timeout(lp.regs + XAS_VLAN_MEMB_CTRL_REG, reg,
				 (!(reg & PORT_VLAN_WRITE_READ_EN_BIT)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("Port vlan write timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int add_del_port_vlan(void __user *arg, u8 add)
{
	struct port_vlan port;

	if (copy_from_user(&port, arg, sizeof(struct port_vlan)))
		return -EFAULT;

	return tsn_switch_vlan_add(&port, add);
}

static int set_vlan_mac_addr_learn(void __user *arg)
{
	struct port_vlan port;
	u32 u_value, u_value1, reg, err;

	if (copy_from_user(&port, arg, sizeof(struct port_vlan)))
		return -EFAULT;

	u_value = ((port.vlan_id & PORT_VLAN_ID_MASK) << PORT_VLAN_ID_SHIFT)
			| PORT_VLAN_READ;
	axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, u_value);

	/* wait for port vlan write completion */
	err = readl_poll_timeout(lp.regs + XAS_VLAN_MEMB_CTRL_REG, reg,
				 (!(reg & PORT_VLAN_WRITE_READ_EN_BIT)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("Port vlan write timed out\n");
		return -ETIMEDOUT;
	}
	u_value = axienet_ior(&lp, XAS_VLAN_MEMB_CTRL_REG);

	u_value1 = axienet_ior(&lp, XAS_VLAN_MEMB_DATA_REG);

	if (port.aging_time) {
		u_value1 &= ~(HW_ADDR_AGING_TIME_MASK <<
				PORT_VLAN_HW_ADDR_AGING_TIME_SHIFT);
		u_value1 |= (port.aging_time <<
				PORT_VLAN_HW_ADDR_AGING_TIME_SHIFT);
	}
	if (port.is_age) {
		if (port.aging)
			u_value1 |= PORT_VLAN_HW_ADDR_AGING_BIT;
		else
			u_value1 &= ~PORT_VLAN_HW_ADDR_AGING_BIT;
	}
	if (port.is_learn) {
		if (port.learning)
			u_value1 &= ~PORT_VLAN_HW_ADDR_LEARN_BIT;
		else
			u_value1 |= PORT_VLAN_HW_ADDR_LEARN_BIT;
	}
	axienet_iow(&lp, XAS_VLAN_MEMB_DATA_REG, u_value1);

	u_value |= PORT_VLAN_WRITE;
	axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, u_value);

	/* wait for port vlan write completion */
	err = readl_poll_timeout(lp.regs + XAS_VLAN_MEMB_CTRL_REG, reg,
				 (!(reg & PORT_VLAN_WRITE_READ_EN_BIT)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("Port vlan write timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int get_vlan_mac_addr_learn(void __user *arg)
{
	struct port_vlan port;
	u32 u_value, u_value1, reg, err;

	if (copy_from_user(&port, arg, sizeof(struct port_vlan)))
		return -EFAULT;

	u_value = ((port.vlan_id & PORT_VLAN_ID_MASK) << PORT_VLAN_ID_SHIFT)
			| PORT_VLAN_READ;
	axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, u_value);

	/* wait for port vlan write completion */
	err = readl_poll_timeout(lp.regs + XAS_VLAN_MEMB_CTRL_REG, reg,
				 (!(reg & PORT_VLAN_WRITE_READ_EN_BIT)), 10,
				 DELAY_OF_FIVE_MILLISEC);
	if (err) {
		pr_err("Port vlan write timed out\n");
		return -ETIMEDOUT;
	}
	u_value1 = axienet_ior(&lp, XAS_VLAN_MEMB_CTRL_REG);

	u_value = axienet_ior(&lp, XAS_VLAN_MEMB_DATA_REG);

	port.aging_time = (u_value >> PORT_VLAN_HW_ADDR_AGING_TIME_SHIFT) &
				HW_ADDR_AGING_TIME_MASK;
	port.aging = (u_value & PORT_VLAN_HW_ADDR_AGING_BIT) ? 1 : 0;
	port.learning = (u_value & PORT_VLAN_HW_ADDR_LEARN_BIT) ? 0 : 1;
	port.port_num = u_value & PORT_STATUS_MASK;

#if IS_ENABLED(CONFIG_XILINX_TSN_QCI)
	if (port.learning) {
		port.port_status = (u_value1 >> PORT_VLAN_IPV_SHIFT) & SDL_CAM_IPV_MASK;
		port.en_port_status = (u_value1 >> PORT_VLAN_EN_IPV_SHIFT) & 0x1;
	} else {
		port.ipv = (u_value1 >> PORT_VLAN_IPV_SHIFT) & SDL_CAM_IPV_MASK;
		port.en_ipv = (u_value1 >> PORT_VLAN_EN_IPV_SHIFT) & 0x1;
	}
#endif
	if (copy_to_user(arg, &port, sizeof(struct port_vlan)))
		return -EFAULT;

	return 0;
}

int tsn_switch_pvid_add(struct native_vlan *port)
{
	u32 u_value;

	switch (port->port_num) {
	case PORT_EP:
		u_value = axienet_ior(&lp, XAS_EP_PORT_VLAN_OFFSET);
		u_value &= ~PORT_VLAN_ID_MASK;
		u_value |= port->vlan_id;
		if (port->en_ipv) {
			u_value &= ~(SDL_CAM_IPV_MASK << NATIVE_MAC1_PCP_SHIFT);
			u_value |= (port->ipv << NATIVE_MAC1_PCP_SHIFT);
		}
		axienet_iow(&lp, XAS_EP_PORT_VLAN_OFFSET, u_value);
		break;
	case PORT_MAC1:
		u_value = axienet_ior(&lp, XAS_MAC_PORT_VLAN_OFFSET);
		u_value &= ~PORT_VLAN_ID_MASK;
		u_value |= port->vlan_id;
		if (port->en_ipv) {
			u_value &= ~(SDL_CAM_IPV_MASK << NATIVE_MAC1_PCP_SHIFT);
			u_value |= (port->ipv << NATIVE_MAC1_PCP_SHIFT);
		}
		axienet_iow(&lp, XAS_MAC_PORT_VLAN_OFFSET, u_value);
		break;
	case PORT_MAC2:
		u_value = axienet_ior(&lp, XAS_MAC_PORT_VLAN_OFFSET);
		u_value &= ~(PORT_VLAN_ID_MASK << NATIVE_MAC2_VLAN_SHIFT);
		u_value |= (port->vlan_id << NATIVE_MAC2_VLAN_SHIFT);
		if (port->en_ipv) {
			u_value &= ~(SDL_CAM_IPV_MASK << NATIVE_MAC2_PCP_SHIFT);
			u_value |= (port->ipv << NATIVE_MAC2_PCP_SHIFT);
		}
		axienet_iow(&lp, XAS_MAC_PORT_VLAN_OFFSET, u_value);
		break;
	}

	return 0;
}

static int set_native_vlan(void __user *arg)
{
	struct native_vlan port;

	if (copy_from_user(&port, arg, sizeof(struct native_vlan)))
		return -EFAULT;

	return tsn_switch_pvid_add(&port);
}

int tsn_switch_pvid_get(struct native_vlan *port)
{
	u32 u_value;

	switch (port->port_num) {
	case PORT_EP:
		u_value = axienet_ior(&lp, XAS_EP_PORT_VLAN_OFFSET);
		port->vlan_id = u_value & PORT_VLAN_ID_MASK;
		port->ipv = (u_value >> NATIVE_MAC1_PCP_SHIFT) &
				SDL_CAM_IPV_MASK;
		break;
	case PORT_MAC1:
		u_value = axienet_ior(&lp, XAS_MAC_PORT_VLAN_OFFSET);
		port->vlan_id = u_value & PORT_VLAN_ID_MASK;
		port->ipv = (u_value >> NATIVE_MAC1_PCP_SHIFT) &
				SDL_CAM_IPV_MASK;
		break;
	case PORT_MAC2:
		u_value = axienet_ior(&lp, XAS_MAC_PORT_VLAN_OFFSET);
		port->vlan_id = (u_value >> NATIVE_MAC2_VLAN_SHIFT) &
				PORT_VLAN_ID_MASK;
		port->ipv = (u_value >> NATIVE_MAC2_PCP_SHIFT) &
				SDL_CAM_IPV_MASK;
		break;
	}

	return 0;
}

static int get_native_vlan(void __user *arg)
{
	struct native_vlan port;

	if (copy_from_user(&port, arg, sizeof(struct native_vlan)))
		return -EFAULT;

	tsn_switch_pvid_get(&port);
	if (copy_to_user(arg, &port, sizeof(struct native_vlan)))
		return -EFAULT;

	return 0;
}

static long switch_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg)
{
	long retval = 0;
	struct switch_data data;
#if IS_ENABLED(CONFIG_XILINX_TSN_QCI)
	struct qci qci_data;
#endif
#if IS_ENABLED(CONFIG_XILINX_TSN_CB)
	struct cb cb_data;
#endif
	switch (cmd) {
	case GET_STATUS_SWITCH:
		/* Switch configurations */
		get_switch_regs(&data);

		/* Memory static counter*/
		get_memory_static_counter(&data);
		if (copy_to_user((char __user *)arg, &data, sizeof(data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
			}
		break;

	case SET_STATUS_SWITCH:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		set_switch_regs(&data);
		break;

	case ADD_CAM_ENTRY:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		if (tsn_switch_cam_set(data.cam_data, ADD)) {
			retval = -EINVAL;
			goto end;
		}
		break;

	case DELETE_CAM_ENTRY:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		if (tsn_switch_cam_set(data.cam_data, DELETE)) {
			retval = -EINVAL;
			goto end;
		}
		break;

	case PORT_VLAN_MEM_CTRL:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		port_vlan_mem_ctrl(data.port_vlan_mem_ctrl);
		break;

	case READ_CAM_ENTRY:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			retval = -EFAULT;
			goto end;
		}
		retval = read_cam_entry(data.cam_data, (void __user *)arg);
		break;

	case SET_MAC_ADDR_LEARN_CONFIG:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = set_mac_addr_learn((void __user *)arg);
		goto end;

	case GET_MAC_ADDR_LEARN_CONFIG:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = get_mac_addr_learn((void __user *)arg);
		goto end;

	case GET_MAC_ADDR_LEARNT_LIST:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = get_mac_addr_learnt_list((void __user *)arg);
		goto end;

	case SET_PORT_STATUS:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = set_port_status((void __user *)arg);
		goto end;

	case GET_PORT_STATUS:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = get_port_status((void __user *)arg);
		goto end;

	case ADD_PORT_VLAN:
		retval = add_del_port_vlan((void __user *)arg, ADD);
		break;

	case DEL_PORT_VLAN:
		retval = add_del_port_vlan((void __user *)arg, DELETE);
		break;

	case SET_VLAN_MAC_ADDR_LEARN_CONFIG:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = set_vlan_mac_addr_learn((void __user *)arg);
		break;

	case GET_VLAN_MAC_ADDR_LEARN_CONFIG:
		if (!en_hw_addr_learning)
			return -EPERM;
		retval = get_vlan_mac_addr_learn((void __user *)arg);
		break;

	case GET_VLAN_MAC_ADDR_LEARN_CONFIG_VLANM:
		retval = get_vlan_mac_addr_learn((void __user *)arg);
		break;

	case SET_PORT_NATIVE_VLAN:
		retval = set_native_vlan((void __user *)arg);
		break;

	case GET_PORT_NATIVE_VLAN:
		retval = get_native_vlan((void __user *)arg);
		break;

	case SET_FRAME_TYPE_FIELD:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		set_frame_filter_opt(data.typefield.type1,
				     data.typefield.type2);
		break;

	case SET_MAC1_MNGMNT_Q_CONFIG:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		set_mac1_mngmntq(data.mac1_config);
		break;

	case SET_MAC2_MNGMNT_Q_CONFIG:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		set_mac2_mngmntq(data.mac2_config);
		break;
#if IS_ENABLED(CONFIG_XILINX_TSN_QCI)
	case CONFIG_METER_MEM:
		if (copy_from_user(&qci_data, (char __user *)arg,
				   sizeof(qci_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		program_meter_reg(qci_data.meter_config_data);
		break;

	case CONFIG_GATE_MEM:
		if (copy_from_user(&qci_data, (char __user *)arg,
				   sizeof(qci_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		config_stream_filter(qci_data.stream_config_data);
		break;

	case PSFP_CONTROL:
		if (copy_from_user(&qci_data, (char __user *)arg,
				   sizeof(qci_data))) {
			retval = -EINVAL;
			pr_err("Copy from user failed\n");
			goto end;
		}
		psfp_control(qci_data.psfp_config_data);
		break;

	case GET_STATIC_PSFP_COUNTER:
		if (copy_from_user(&qci_data, (char __user *)arg,
				   sizeof(qci_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		get_psfp_static_counter(&qci_data.psfp_counter_data);
		if (copy_to_user((char __user *)arg, &qci_data,
				 sizeof(qci_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;
	case GET_METER_REG:
		get_meter_reg(&qci_data.meter_config_data);
		if (copy_to_user((char __user *)arg, &qci_data,
				 sizeof(qci_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;
	case GET_STREAM_FLTR_CONFIG:
		get_stream_filter_config(&qci_data.stream_config_data);
		if (copy_to_user((char __user *)arg, &qci_data,
				 sizeof(qci_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;
#endif
#if IS_ENABLED(CONFIG_XILINX_TSN_CB)
	case CONFIG_MEMBER_MEM:
		if (copy_from_user(&cb_data, (char __user *)arg,
				   sizeof(cb_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		program_member_reg(cb_data);
		break;

	case CONFIG_INGRESS_FLTR:
		if (copy_from_user(&cb_data, (char __user *)arg,
				   sizeof(cb_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		config_ingress_filter(cb_data);
		break;

	case FRER_CONTROL:
		if (copy_from_user(&cb_data, (char __user *)arg,
				   sizeof(cb_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		frer_control(cb_data.frer_ctrl_data);
		break;

	case GET_STATIC_FRER_COUNTER:
		if (copy_from_user(&cb_data, (char __user *)arg,
				   sizeof(cb_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		get_frer_static_counter(&cb_data.frer_counter_data);
		if (copy_to_user((char __user *)arg, &cb_data,
				 sizeof(cb_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;

	case GET_MEMBER_REG:
		get_member_reg(&cb_data.frer_memb_config_data);
		if (copy_to_user((char __user *)arg, &cb_data,
				 sizeof(cb_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;

	case GET_INGRESS_FLTR:
		get_ingress_filter_config(&cb_data.in_fltr_data);
		if (copy_to_user((char __user *)arg, &cb_data,
				 sizeof(cb_data))) {
			pr_err("Copy to user failed\n");
			retval = -EINVAL;
			goto end;
		}
		break;
#endif
	}
end:
	return retval;
}

static const struct file_operations switch_fops = {
	.owner		=	THIS_MODULE,
	.unlocked_ioctl	=	switch_ioctl,
	.open		=	switch_open,
	.release	=       switch_release,
};

static int tsn_switch_init(void)
{
	int ret;

	switch_dev.minor = MISC_DYNAMIC_MINOR;
	switch_dev.name = "switch";
	switch_dev.fops = &switch_fops;
	ret = misc_register(&switch_dev);
	if (ret < 0) {
		pr_err("Switch driver registration failed!\n");
		return ret;
	}

	eth_random_addr((u8 *)&sw_mac_addr);

	pr_debug("Xilinx TSN Switch driver initialized!\n");
	return 0;
}

static int tsn_switch_cam_init(u16 num_q)
{
	u32 pmap = 0;
	u32 timeout = 20000;
	u8 pmap_priority_shift = 0;
	u8 i = 0;

	/* wait for switch init done */
	while (!(axienet_ior(&lp, XAS_STATUS_OFFSET) &
		SDL_CAM_WR_ENABLE) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("Switch init took longer time!!");

	if (num_q == 3) {
	/* map pcp's to queues in accordance with device tree */
		i = 0;
		while (i != 8) {
			if (lp.st_pcp & (1 << i)) {
				pmap_priority_shift = 4 * i;
				pmap = pmap |
				PMAP_EGRESS_QUEUE2_SELECT << pmap_priority_shift;
			} else if (lp.res_pcp & (1 << i)) {
				pmap_priority_shift = 4 * i;
				pmap = pmap |
				PMAP_EGRESS_QUEUE1_SELECT << pmap_priority_shift;
			}
			i = i + 1;
		}
	} else {
		i = 0;
		while (i != 8) {
			if (lp.st_pcp & (1 << i)) {
				pmap_priority_shift = 4 * i;
				pmap = pmap |
				PMAP_EGRESS_QUEUE1_SELECT << pmap_priority_shift;
			}
			i = i + 1;
		}
	}

	axienet_iow(&lp, XAS_PMAP_OFFSET, pmap);

	timeout = 20000;
	/* wait for cam init done */
	while (!(axienet_ior(&lp, XAS_SDL_CAM_STATUS_OFFSET) &
		SDL_CAM_WR_ENABLE) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("CAM init took longer time!!");

	return 0;
}

static inline void tsn_switch_set_src_mac_filter(const u8 *mac, int port)
{
	u32 val;
	u32 shift = (port == 1) ? MAC1_PORT_MAC_ADDR_LSB_SHIFT :
		MAC2_PORT_MAC_ADDR_LSB_SHIFT;

	/* program the Source MAC address to filter */
	val = axienet_ior(&lp, XAS_PORT_STATE_CTRL_OFFSET);

	val &= ~(PORT_MAC_ADDR_LSB_MASK << shift);
	val |= ((mac[5] & PORT_MAC_ADDR_LSB_MASK) << shift);

	axienet_iow(&lp, XAS_PORT_STATE_CTRL_OFFSET, val);
}

/* initialize pre-configured fdbs in the system */
static int tsn_switch_fdb_init(struct platform_device *pdev)
{
	u32 val, port;
	u8 mac_addr[ETH_ALEN];
	struct device_node *np;
	u16 num_ports;
	int ret;
	struct cam_struct cam;

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-ports",
				   &num_ports);
	if (ret) {
		dev_err(&pdev->dev, "could not read xlnx,num-ports\n");
		return -EINVAL;
	}

	/* enable source mac based filtering */
	val = axienet_ior(&lp, XAS_MNG_Q_CTRL_OFFSET);

	/* compares Source MAC address to determine the Network
	 * Port on which a frame needs to be forwarded for frames received
	 * with Internal Endpoint interface.
	 * [i.e. CPU generated  management frames]
	 */
	val |= (1 << XAS_MNG_Q_SRC_MAC_FIL_EN_SHIFT);

	axienet_iow(&lp, XAS_MNG_Q_CTRL_OFFSET, val);

	/* port0 == ep, port1 == temac1 port2 == temac2
	 * temac1, temac2.. temacn are network ports
	 */
	/* network port mac addresses must differ in last lsb nibble
	 * this is a pre-requisite;
	 * we program the last nibble here per mac basis
	 */
	ep_node = of_parse_phandle(pdev->dev.of_node, "ports", 0);
	for (port = 1; port < num_ports; port++) {
		np = of_parse_phandle(pdev->dev.of_node, "ports", port);

		ret = of_get_mac_address(np, mac_addr);
		if (ret) {
			dev_err(&pdev->dev, "could not find MAC address\n");
			return -EINVAL;
		}
		tsn_switch_set_src_mac_filter(mac_addr, port);
	}

	/* rest of the mac addr for all ports would be same
	 * so use the last mac instance of the loop above
	 * set the 32bit lsb of mac address
	 */
	axienet_iow(&lp, XAS_MAC_LSB_OFFSET,
		    (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		    (mac_addr[4] << 8)  | (mac_addr[5]));

	/* set rest of 16bit msb and 4bit filter(0xF) */
	axienet_iow(&lp, XAS_MAC_MSB_OFFSET,
		    (0xF << XAS_MAC_MSB_FF_MASK_SHIFT) |
		    (mac_addr[0] << 8) | mac_addr[1]);

	/* now tell the switch which frames to consider as mgmt frames
	 */
	/*  DA list
	 *  01-80-C2-00-00-00 STP 802.1d && LLDP
	 */
	memset(&cam, 0, sizeof(struct cam_struct));

	cam.dest_addr[0] = 0x01;
	cam.dest_addr[1] = 0x80;
	cam.dest_addr[2] = 0xc2;
	cam.dest_addr[3] = 0x00;
	cam.dest_addr[4] = 0x00;
	cam.dest_addr[5] = 0x00;

	/* send it all, src mac filter will pick the port */
	cam.fwd_port = DEFAULT_FWD_ALL;
	cam.flags |= XAS_CAM_EP_MGMTQ_EN;
	cam.vlanid = DEFAULT_PVID;
	/* TODO if pvid changes on the port of switch,
	 * these cam entries have to be updated
	 */

	if (tsn_switch_cam_set(cam, ADD) < 0)
		dev_err(&pdev->dev, "could not add default fdb\n");

	/* 01-80-c2-00-00-0e  LLDP */
	cam.dest_addr[5] = 0x0e;
	if (tsn_switch_cam_set(cam, ADD) < 0)
		dev_err(&pdev->dev, "could not add default fdb\n");
	/* CDP */
	cam.dest_addr[0] = 0x01;
	cam.dest_addr[1] = 0x00;
	cam.dest_addr[2] = 0x0c;
	cam.dest_addr[3] = 0xcc;
	cam.dest_addr[4] = 0xcc;
	cam.dest_addr[5] = 0xcc;
	if (tsn_switch_cam_set(cam, ADD) < 0)
		dev_err(&pdev->dev, "could not add default fdb\n");

	/* PTPv2 UDP Announce Messages */
	cam.dest_addr[0] = 0x01;
	cam.dest_addr[1] = 0x00;
	cam.dest_addr[2] = 0x5e;
	cam.dest_addr[3] = 0x00;
	cam.dest_addr[4] = 0x01;
	cam.dest_addr[5] = 0x81;
	if (tsn_switch_cam_set(cam, ADD) < 0)
		dev_err(&pdev->dev, "could not add default fdb\n");
	/* PTPv2 UDP P2P Mechanism Messages */
	cam.dest_addr[4] = 0x00;
	cam.dest_addr[5] = 0x6b;
	if (tsn_switch_cam_set(cam, ADD) < 0)
		dev_err(&pdev->dev, "could not add default fdb\n");

	/* on RX path enable sideband management on frames forwarded to ep
	 * this only applicable if IP param EN_INBAND_MGMT_TAG is 0
	 */
	val = axienet_ior(&lp, XAS_MNG_Q_CTRL_OFFSET);
	val |= (1 << XAS_MNG_Q_SIDEBAND_EN_SHIFT);
	axienet_iow(&lp, XAS_MNG_Q_CTRL_OFFSET, val);

	return 0;
}

static int tsnswitch_probe(struct platform_device *pdev)
{
	struct resource *swt;
	int ret;
	u16 num_tc;
	int data;
	struct net_device *ndev;
	int value;
	u8 inband_mgmt_tag;

	pr_info("TSN Switch probe\n");
	/* Map device registers */
	swt = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp.regs = devm_ioremap_resource(&pdev->dev, swt);
	if (IS_ERR(lp.regs))
		return PTR_ERR(lp.regs);

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-tc",
				   &num_tc);
	if (ret || (num_tc != 2 && num_tc != 3))
		num_tc = XAE_MAX_TSN_TC;

	axienet_get_pcp_mask(pdev, &lp, num_tc);

	en_hw_addr_learning = of_property_read_bool(pdev->dev.of_node,
						    "xlnx,has-hwaddr-learning");

	pr_info("TSN Switch Initializing ....\n");
	pr_info("TSN Switch hw_addr_learning :%d\n", en_hw_addr_learning);

	ret = tsn_switch_init();
	if (ret)
		return ret;
	pr_info("TSN CAM Initializing ....\n");
	ret = tsn_switch_cam_init(num_tc);

	inband_mgmt_tag = of_property_read_bool(pdev->dev.of_node,
						"xlnx,has-inband-mgmt-tag");
	/* only support switchdev in sideband management */
	if (!inband_mgmt_tag) {
		ret = tsn_switch_fdb_init(pdev);
		xlnx_switchdev_init();
	} else {
		pr_info("TSN IP with inband mgmt: Linux SWITCHDEV turned off\n");
	}

	/* writing into endpoint extension control register for channel mapping as follows:
	 *
	 *	3 traffic classes & EP + switch + Extended EP
	 *	      +---------+
	 *	  1   |         |
	 *    BE-------         |
	 *	  2   |         |   1
	 *     ST------         |------BE
	 *	  3   |         |   2
	 *   mgmt------         |------RES
	 *	  4   |         |
	 *   RES-------         |
	 *	  5   |         |
	 * EX-BE-------         |
	 *	  6   |         |
	 * EX-ST-------         |
	 *	  7   |         |
	 * EX-RES------         |
	 *	      +---------+
	 *
	 *	2 traffic classes & EP + switch + Extended EP
	 *	      +---------+
	 *	  1   |         |
	 *    BE-------         |
	 *	  2   |         |   1
	 *    ST-------         |------BE
	 *	  3   |         |
	 *   mgmt------         |
	 *	  4   |         |
	 * EX-BE-------         |
	 *	  5   |         |
	 * EX-ST-------         |
	 *	      |         |
	 *	      +---------+
	 *	3 traffic classes & EP + switch
	 *	      +---------+
	 *	  1   |         |
	 *    BE-------         |
	 *	  2   |         |   1
	 *     ST------         |------BE
	 *	  3   |         |   2
	 *   mgmt------         |------RES
	 *	  4   |         |
	 *   RES-------         |
	 *	      |         |
	 *	      +---------+
	 *
	 *	2 traffic classes & EP + switch
	 *	      +---------+
	 *	  1   |         |
	 *    BE-------         |
	 *	  2   |         |   1
	 *     ST------         |------BE
	 *	  3   |         |
	 *   mgmt------         |
	 *	      |         |
	 *	      +---------+
	 */
	data = axienet_ior(&lp, XAE_EP_EXT_CTRL_OFFSET);
	pr_info("Data in Endpoint Extension Control Register is %x\n", data);
	ndev = of_find_net_device_by_node(ep_node);
	ep_lp = netdev_priv(ndev);
	if (ep_lp->ex_ep) {
		if (num_tc == 3) {
			data = (data & XAE_EX_EP_EXT_CTRL_MASK) |
					XAE_EX_EP_EXT_CTRL_DATA_TC_3;
			axienet_iow(&lp, XAE_EP_EXT_CTRL_OFFSET, data);
		}
		if (num_tc == 2) {
			data = (data & XAE_EX_EP_EXT_CTRL_MASK) |
					XAE_EX_EP_EXT_CTRL_DATA_TC_2;
			axienet_iow(&lp, XAE_EP_EXT_CTRL_OFFSET, data);
		}
		/* Enabling endpoint packet switching extension for broadcast and
		 * multicast packets received from endpoint
		 */
		value = axienet_ior(&lp, XAE_MGMT_QUEUING_OPTIONS_OFFSET);
		value |= XAE_EX_EP_BROADCAST_PKT_SWITCH;
		value |= XAE_EX_EP_MULTICAST_PKT_SWITCH;
		axienet_iow(&lp, XAE_MGMT_QUEUING_OPTIONS_OFFSET, value);
	} else {
		if (num_tc == 3) {
			data = (data & XAE_EP_EXT_CTRL_MASK) |
					XAE_EP_EXT_CTRL_DATA_TC_3;
			axienet_iow(&lp, XAE_EP_EXT_CTRL_OFFSET, data);
		}
		if (num_tc == 2) {
			data = (data & XAE_EP_EXT_CTRL_MASK) |
					XAE_EP_EXT_CTRL_DATA_TC_2;
			axienet_iow(&lp, XAE_EP_EXT_CTRL_OFFSET, data);
		}
	}

	return ret;
}

static int tsnswitch_remove(struct platform_device *pdev)
{
	misc_deregister(&switch_dev);
	xlnx_switchdev_remove();
	return 0;
}

static struct platform_driver tsnswitch_driver = {
	.probe = tsnswitch_probe,
	.remove = tsnswitch_remove,
	.driver = {
		 .name = "xilinx_tsnswitch",
		 .of_match_table = tsnswitch_of_match,
	},
};

module_platform_driver(tsnswitch_driver);

MODULE_DESCRIPTION("Xilinx TSN Switch driver");
MODULE_AUTHOR("Xilinx");
MODULE_LICENSE("GPL v2");
