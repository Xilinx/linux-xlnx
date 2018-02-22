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

#include "xilinx_tsn_switch.h"
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/miscdevice.h>

static struct miscdevice switch_dev;
struct axienet_local lp;

#define ADD					1
#define DELETE					0

#define PMAP_EGRESS_QUEUE_MASK			0x7
#define PMAP_EGRESS_QUEUE0_SELECT		0x0
#define PMAP_EGRESS_QUEUE1_SELECT		0x1
#define PMAP_EGRESS_QUEUE2_SELECT		0x2
#define PMAP_PRIORITY0_SHIFT			0
#define PMAP_PRIORITY1_SHIFT			4
#define PMAP_PRIORITY2_SHIFT			8
#define PMAP_PRIORITY3_SHIFT			12
#define PMAP_PRIORITY4_SHIFT			16
#define PMAP_PRIORITY5_SHIFT			20
#define PMAP_PRIORITY6_SHIFT			24
#define PMAP_PRIORITY7_SHIFT			28
#define SDL_EN_CAM_IPV_SHIFT			28
#define SDL_CAM_IPV_SHIFT			29

#define SDL_CAM_WR_ENABLE			BIT(0)
#define SDL_CAM_ADD_ENTRY			0x1
#define SDL_CAM_DELETE_ENTRY			0x3
#define SDL_CAM_VLAN_SHIFT			16
#define SDL_CAM_VLAN_MASK			0xFFF
#define SDL_CAM_IPV_MASK			0x7
#define SDL_CAM_PORT_LIST_SHIFT			8
#define SDL_GATEID_SHIFT			16
#define SDL_CAM_FWD_TO_EP			BIT(0)
#define SDL_CAM_FWD_TO_PORT_1			BIT(1)
#define SDL_CAM_FWD_TO_PORT_2			BIT(2)
#define SDL_CAM_EP_ACTION_LIST_SHIFT		0
#define SDL_CAM_MAC_ACTION_LIST_SHIFT		4
#define SDL_CAM_DEST_MAC_XLATION		BIT(0)
#define SDL_CAM_VLAN_ID_XLATION			BIT(1)
#define SDL_CAM_UNTAG_FRAME			BIT(2)

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
	axienet_iow(&lp, XAS_MAC2_MNG_Q_OPTION_OFFSET, config);
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
	data->mac2_config = axienet_ior(&lp, XAS_MAC2_MNG_Q_OPTION_OFFSET);

	/* Port VLAN Membership control*/
	data->port_vlan_mem_ctrl = axienet_ior(&lp, XAS_VLAN_MEMB_CTRL_REG);
	/* Port VLAN Membership read data*/
	data->port_vlan_mem_data = axienet_ior(&lp, XAS_VLAN_MEMB_DATA_REG);
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
	data->mem_arr_cnt.err_res_mac2_mac1.msb = axienet_ior(&lp,
					XAS_MEM_STCNTR_ERR_RES_MAC2_MAC1 + 0x4);
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
	data->mem_arr_cnt.err_res_mac1_mac2.msb = axienet_ior(&lp,
					XAS_MEM_STCNTR_ERR_RES_MAC1_MAC2 + 0x4);
	data->mem_arr_cnt.err_be_mac1_mac2.lsb = axienet_ior(&lp,
					XAS_MEM_STCNTR_ERR_BE_MAC1_MAC2);
	data->mem_arr_cnt.err_be_mac1_mac2.msb = axienet_ior(&lp,
					XAS_MEM_STCNTR_ERR_BE_MAC1_MAC2 + 0x4);
}

static void add_delete_cam_entry(struct cam_struct data, u8 add)
{
	u32 port_action = 0;
	u32 tv2 = 0;
	u32 timeout = 20000;

	/* wait for cam init done */
	while (!(axienet_ior(&lp, XAS_SDL_CAM_STATUS_OFFSET) &
		SDL_CAM_WR_ENABLE) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("CAM init took longer time!!");
	/* mac and vlan */
	axienet_iow(&lp, XAS_SDL_CAM_KEY1_OFFSET,
		    (data.dest_addr[0] << 24) | (data.dest_addr[1] << 16) |
		    (data.dest_addr[2] << 8)  | (data.dest_addr[3]));
	axienet_iow(&lp, XAS_SDL_CAM_KEY2_OFFSET,
		    ((data.dest_addr[4] << 8) | data.dest_addr[5]) |
		    ((data.vlanid & SDL_CAM_VLAN_MASK) << SDL_CAM_VLAN_SHIFT));

	/* TV 1 and TV 2 */
	axienet_iow(&lp, XAS_SDL_CAM_TV1_OFFSET,
		    (data.src_addr[0] << 24) | (data.src_addr[1] << 16) |
		    (data.src_addr[2] << 8)  | (data.src_addr[3]));

	tv2 = ((data.src_addr[4] << 8) | data.src_addr[5]) |
	       ((data.tv_vlanid & SDL_CAM_VLAN_MASK) << SDL_CAM_VLAN_SHIFT);

#if IS_ENABLED(CONFIG_XILINX_TSN_QCI)
	tv2 = tv2 | ((data.ipv & SDL_CAM_IPV_MASK) << SDL_CAM_IPV_SHIFT)
				| (data.en_ipv << SDL_EN_CAM_IPV_SHIFT);
#endif
	axienet_iow(&lp, XAS_SDL_CAM_TV2_OFFSET, tv2);

	if (data.tv_en)
		port_action = ((SDL_CAM_DEST_MAC_XLATION |
		SDL_CAM_VLAN_ID_XLATION) << SDL_CAM_MAC_ACTION_LIST_SHIFT);

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

	timeout = 20000;
	/* wait for write to complete */
	while ((axienet_ior(&lp, XAS_SDL_CAM_CTRL_OFFSET) &
		SDL_CAM_WR_ENABLE) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("CAM write took longer time!!");
}

static void port_vlan_mem_ctrl(u32 port_vlan_mem)
{
		axienet_iow(&lp, XAS_VLAN_MEMB_CTRL_REG, port_vlan_mem);
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
		add_delete_cam_entry(data.cam_data, ADD);
		break;

	case DELETE_CAM_ENTRY:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		add_delete_cam_entry(data.cam_data, DELETE);
		break;

	case PORT_VLAN_MEM_CTRL:
		if (copy_from_user(&data, (char __user *)arg, sizeof(data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		port_vlan_mem_ctrl(data.port_vlan_mem_ctrl);
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
		program_member_reg(cb_data.frer_memb_config_data);
		break;

	case CONFIG_INGRESS_FLTR:
		if (copy_from_user(&cb_data, (char __user *)arg,
				   sizeof(cb_data))) {
			pr_err("Copy from user failed\n");
			retval = -EINVAL;
			goto end;
		}
		config_ingress_filter(cb_data.in_fltr_data);
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

	pr_debug("Xilinx TSN Switch driver initialized!\n");
	return 0;
}

static int tsn_switch_cam_init(u16 num_q)
{
	u32 pmap;
	u32 timeout = 20000;

	/* wait for switch init done */
	while (!(axienet_ior(&lp, XAS_STATUS_OFFSET) &
		SDL_CAM_WR_ENABLE) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("Switch init took longer time!!");

	if (num_q == 3) {
	/* map pcp = 2,3 to queue1
	 *     pcp = 4 to queue2
	 */
	pmap = ((PMAP_EGRESS_QUEUE1_SELECT << PMAP_PRIORITY2_SHIFT) |
		(PMAP_EGRESS_QUEUE1_SELECT << PMAP_PRIORITY3_SHIFT) |
		(PMAP_EGRESS_QUEUE2_SELECT << PMAP_PRIORITY4_SHIFT));
	} else if (num_q == 2) {
		/*     pcp = 4 to queue1 */
		pmap = (PMAP_EGRESS_QUEUE1_SELECT << PMAP_PRIORITY4_SHIFT);
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

static int tsnswitch_probe(struct platform_device *pdev)
{
	struct resource *swt;
	int ret;
	u16 num_q;

	pr_info("TSN Switch probe\n");
	/* Map device registers */
	swt = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lp.regs = devm_ioremap_resource(&pdev->dev, swt);
	if (IS_ERR(lp.regs))
		return PTR_ERR(lp.regs);

	ret = of_property_read_u16(pdev->dev.of_node, "xlnx,num-queues",
				   &num_q);
	if (ret || ((num_q != 2) && (num_q != 3)))
		num_q = XAE_MAX_QUEUES;

	pr_info("TSN Switch Initializing ....\n");
	ret = tsn_switch_init();
	if (ret)
		return ret;
	pr_info("TSN CAM Initializing ....\n");
	ret = tsn_switch_cam_init(num_q);

	return ret;
}

static int tsnswitch_remove(struct platform_device *pdev)
{
	misc_deregister(&switch_dev);
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
