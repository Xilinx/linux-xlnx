/*
 * Xilinx TSN core switch header
 *
 * Copyright (C) 2017 Xilinx, Inc.
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

#ifndef XILINX_TSN_SWITCH_H
#define XILINX_TSN_SWITCH_H

#include "xilinx_axienet.h"

/* ioctls */
#define GET_STATUS_SWITCH			0x16
#define SET_STATUS_SWITCH			0x17
#define ADD_CAM_ENTRY				0x18
#define DELETE_CAM_ENTRY			0x19
#define PORT_VLAN_MEM_CTRL			0x20
#define SET_FRAME_TYPE_FIELD			0x21
#define SET_MAC1_MNGMNT_Q_CONFIG		0x22
#define SET_MAC2_MNGMNT_Q_CONFIG		0x23
#define CONFIG_METER_MEM			0x24
#define CONFIG_GATE_MEM				0x25
#define PSFP_CONTROL				0x26
#define GET_STATIC_PSFP_COUNTER			0x27
#define GET_METER_REG				0x28
#define GET_STREAM_FLTR_CONFIG			0x29
#define CONFIG_MEMBER_MEM			0x2A
#define CONFIG_INGRESS_FLTR			0x2B
#define FRER_CONTROL				0x2C
#define GET_STATIC_FRER_COUNTER			0x2D
#define GET_MEMBER_REG				0x2E
#define GET_INGRESS_FLTR			0x2F

/* Xilinx Axi Switch Offsets*/
#define XAS_STATUS_OFFSET			0x00000
#define XAS_CONTROL_OFFSET			0x00004
#define XAS_PMAP_OFFSET				0x00008
#define XAS_MAC_LSB_OFFSET			0x0000C
#define XAS_MAC_MSB_OFFSET			0x00010
#define XAS_EP2MAC_ST_FIFOT_OFFSET		0x00020
#define XAS_EP2MAC_RE_FIFOT_OFFSET		0x00024
#define XAS_EP2MAC_BE_FIFOT_OFFSET		0x00028
#define XAS_MAC2MAC_ST_FIFOT_OFFSET		0x00030
#define XAS_MAC2MAC_RE_FIFOT_OFFSET		0x00034
#define XAS_MAC2MAC_BE_FIFOT_OFFSET		0x00038
#define XAS_EP_PORT_VLAN_OFFSET			0x00040
#define XAS_MAC_PORT_VLAN_OFFSET		0x00044
#define XAS_FRM_FLTR_TYPE_FIELD_OPT_OFFSET	0x00050
#define XAS_MAC2_MNG_Q_OPTION_OFFSET		0x00054
#define XAS_MAC1_MNG_Q_OPTION_OFFSET		0x00058
#define XAS_ST_MAX_FRAME_SIZE_OFFSET		0x00060
#define XAS_RE_MAX_FRAME_SIZE_OFFSET		0x00064
#define XAS_BE_MAX_FRAME_SIZE_OFFSET		0x00068

/* Memory static counters */
#define XAS_MEM_STCNTR_CAM_LOOKUP		0x00400
#define XAS_MEM_STCNTR_MULTCAST			0x00408
#define XAS_MEM_STCNTR_ERR_MAC1			0x00410
#define XAS_MEM_STCNTR_ERR_MAC2			0x00418
#define XAS_MEM_STCNTR_SC_MAC1_EP		0x00420
#define XAS_MEM_STCNTR_RES_MAC1_EP		0x00428
#define XAS_MEM_STCNTR_BE_MAC1_EP		0x00430
#define XAS_MEM_STCNTR_ERR_SC_MAC1_EP		0x00438
#define XAS_MEM_STCNTR_ERR_RES_MAC1_EP		0x00440
#define XAS_MEM_STCNTR_ERR_BE_MAC1_EP		0x00448
#define XAS_MEM_STCNTR_SC_MAC2_EP		0x00458
#define XAS_MEM_STCNTR_RES_MAC2_EP		0x00460
#define XAS_MEM_STCNTR_BE_MAC2_EP		0x00468
#define XAS_MEM_STCNTR_ERR_SC_MAC2_EP		0x00470
#define XAS_MEM_STCNTR_ERR_RES_MAC2_EP		0x00478
#define XAS_MEM_STCNTR_ERR_BE_MAC2_EP		0x00480
#define XAS_MEM_STCNTR_SC_EP_MAC1		0x00490
#define XAS_MEM_STCNTR_RES_EP_MAC1		0x00498
#define XAS_MEM_STCNTR_BE_EP_MAC1		0x004A0
#define XAS_MEM_STCNTR_ERR_SC_EP_MAC1		0x004A8
#define XAS_MEM_STCNTR_ERR_RES_EP_MAC1		0x004B0
#define XAS_MEM_STCNTR_ERR_BE_EP_MAC1		0x004B8
#define XAS_MEM_STCNTR_SC_MAC2_MAC1		0x004C0
#define XAS_MEM_STCNTR_RES_MAC2_MAC1		0x004C8
#define XAS_MEM_STCNTR_BE_MAC2_MAC1		0x004D0
#define XAS_MEM_STCNTR_ERR_SC_MAC2_MAC1		0x004D8
#define XAS_MEM_STCNTR_ERR_RES_MAC2_MAC1	0x004E0
#define XAS_MEM_STCNTR_ERR_BE_MAC2_MAC1		0x004E8
#define XAS_MEM_STCNTR_SC_EP_MAC2		0x004F0
#define XAS_MEM_STCNTR_RES_EP_MAC2		0x004F8
#define XAS_MEM_STCNTR_BE_EP_MAC2		0x00500
#define XAS_MEM_STCNTR_ERR_SC_EP_MAC2		0x00508
#define XAS_MEM_STCNTR_ERR_RES_EP_MAC2		0x00510
#define XAS_MEM_STCNTR_ERR_BE_EP_MAC2		0x00518
#define XAS_MEM_STCNTR_SC_MAC1_MAC2		0x00520
#define XAS_MEM_STCNTR_RES_MAC1_MAC2		0x00528
#define XAS_MEM_STCNTR_BE_MAC1_MAC2		0x00530
#define XAS_MEM_STCNTR_ERR_SC_MAC1_MAC2		0x00538
#define XAS_MEM_STCNTR_ERR_RES_MAC1_MAC2	0x00540
#define XAS_MEM_STCNTR_ERR_BE_MAC1_MAC2		0x00548

/* Stream Destination Lookup CAM */
#define XAS_SDL_CAM_CTRL_OFFSET			0x1000
#define XAS_SDL_CAM_STATUS_OFFSET		0x1004
#define XAS_SDL_CAM_KEY1_OFFSET			0x1008
#define XAS_SDL_CAM_KEY2_OFFSET			0x100C
#define XAS_SDL_CAM_TV1_OFFSET			0x1010
#define XAS_SDL_CAM_TV2_OFFSET			0x1014
#define XAS_SDL_CAM_PORT_ACT_OFFSET		0x1018

/* Port VLAN Membership Memory */
#define XAS_VLAN_MEMB_CTRL_REG			0x1100
#define XAS_VLAN_MEMB_DATA_REG			0x1104

/* QCI */
#define PSFP_CONTROL_OFFSET			0x1200
#define STREAM_FILTER_CONFIG_OFFSET		0x1204
#define	STREAM_METER_CIR_OFFSET			0x1208
#define	STREAM_METER_EIR_OFFSET			0x120C
#define	STREAM_METER_CBR_OFFSET			0x1210
#define	STREAM_METER_EBR_OFFSET			0x1214

/* PSFP Statistics Counters */
#define TOTAL_PSFP_FRAMES_OFFSET		0x2000
#define FLTR_INGS_PORT_ERR_OFFSET		0x2800
#define FLTR_STDU_ERR_OFFSET			0x3000
#define METER_ERR_OFFSET			0x3800

/* CB */
#define FRER_CONTROL_OFFSET			0x1300
#define INGRESS_FILTER_OFFSET			0x1304
#define FRER_CONFIG_REG1			0x1308
#define FRER_CONFIG_REG2			0x130C

/* FRER Statistics Counters */
#define TOTAL_FRER_FRAMES_OFFSET		0x4000
#define FRER_DISCARD_INGS_FLTR_OFFSET		0x4800
#define FRER_PASS_FRAMES_SEQ_OFFSET		0x5000
#define FRER_DISCARD_FRAMES_SEQ_OFFSET		0x5800
#define FRER_ROGUE_FRAMES_SEQ_OFFSET		0x6000
#define FRER_PASS_FRAMES_INDV_OFFSET		0x6800
#define FRER_DISCARD_FRAMES_INDV_OFFSET		0x7000
#define SEQ_RECV_RESETS_OFFSET			0x7800

/* 64 bit counter*/
struct static_cntr {
	u32 msb;
	u32 lsb;
};

/*********** QCI Structures **************/
struct psfp_config {
	u8 gate_id;
	u8 meter_id;
	bool en_meter;
	bool allow_stream;
	bool en_psfp;
	u8 wr_op_type;
	bool op_type;
};

struct meter_config {
	u32 cir;
	u32 eir;
	u32 cbr;
	u32 ebr;
	u8 mode;
};

struct stream_filter {
	u8 in_pid; /* ingress port id*/
	u16 max_fr_size; /* max frame size*/
};

/* PSFP Static counter*/
struct psfp_static_counter {
	struct static_cntr psfp_fr_count;
	struct static_cntr err_filter_ins_port;
	struct static_cntr err_filtr_sdu;
	struct static_cntr err_meter;
	unsigned char num;
};

/* QCI Core stuctures */
struct qci {
	struct meter_config meter_config_data;
	struct stream_filter stream_config_data;
	struct psfp_config psfp_config_data;
	struct psfp_static_counter psfp_counter_data;
};

/************* QCI Structures end *************/

/*********** CB Structures **************/
struct frer_ctrl {
	u8 gate_id;
	u8 memb_id;
	bool seq_reset;
	bool gate_state;
	bool rcvry_tmout;
	bool frer_valid;
	u8 wr_op_type;
	bool op_type;
};

struct in_fltr {
	u8 in_port_id;
	u16 max_seq_id;
};

struct frer_memb_config {
	u8 seq_rec_hist_len;
	bool split_strm_valid;
	u8 split_strm_inport_id;
	u16 split_strm_vlan_id;
	u32 rem_ticks;
};

/* FRER Static counter*/
struct frer_static_counter {
	struct static_cntr frer_fr_count;
	struct static_cntr disc_frames_in_portid;
	struct static_cntr pass_frames_seq_recv;
	struct static_cntr disc_frames_seq_recv;
	struct static_cntr rogue_frames_seq_recv;
	struct static_cntr pass_frames_ind_recv;
	struct static_cntr disc_frames_ind_recv;
	struct static_cntr seq_recv_rst;
	unsigned char num;
};

/* CB Core stuctures */
struct cb {
	struct frer_ctrl frer_ctrl_data;
	struct in_fltr in_fltr_data;
	struct frer_memb_config frer_memb_config_data;
	struct frer_static_counter frer_counter_data;
};

/************* CB Structures end *************/

/********* Switch Structures Starts ***********/
struct thershold {
	u16 t1;
	u16 t2;
};

/* memory static counters */
struct mem_static_arr_cntr {
	struct static_cntr cam_lookup;
	struct static_cntr multicast_fr;
	struct static_cntr err_mac1;
	struct static_cntr err_mac2;
	struct static_cntr sc_mac1_ep;
	struct static_cntr res_mac1_ep;
	struct static_cntr be_mac1_ep;
	struct static_cntr err_sc_mac1_ep;
	struct static_cntr err_res_mac1_ep;
	struct static_cntr err_be_mac1_ep;
	struct static_cntr sc_mac2_ep;
	struct static_cntr res_mac2_ep;
	struct static_cntr be_mac2_ep;
	struct static_cntr err_sc_mac2_ep;
	struct static_cntr err_res_mac2_ep;
	struct static_cntr err_be_mac2_ep;
	struct static_cntr sc_ep_mac1;
	struct static_cntr res_ep_mac1;
	struct static_cntr be_ep_mac1;
	struct static_cntr err_sc_ep_mac1;
	struct static_cntr err_res_ep_mac1;
	struct static_cntr err_be_ep_mac1;
	struct static_cntr sc_mac2_mac1;
	struct static_cntr res_mac2_mac1;
	struct static_cntr be_mac2_mac1;
	struct static_cntr err_sc_mac2_mac1;
	struct static_cntr err_res_mac2_mac1;
	struct static_cntr err_be_mac2_mac1;
	struct static_cntr sc_ep_mac2;
	struct static_cntr res_ep_mac2;
	struct static_cntr be_ep_mac2;
	struct static_cntr err_sc_ep_mac2;
	struct static_cntr err_res_ep_mac2;
	struct static_cntr err_be_ep_mac2;
	struct static_cntr sc_mac1_mac2;
	struct static_cntr res_mac1_mac2;
	struct static_cntr be_mac1_mac2;
	struct static_cntr err_sc_mac1_mac2;
	struct static_cntr err_res_mac1_mac2;
	struct static_cntr err_be_mac1_mac2;
};

/* CAM structure */
struct cam_struct {
	u8 src_addr[6];
	u8 dest_addr[6];
	u16 vlanid;
	u16 tv_vlanid;
	u8 fwd_port;
	bool tv_en;
	u8 gate_id;
	u8 ipv;
	bool en_ipv;
};

/*Frame Filtering Type Field Option */
struct ff_type {
	u16 type1;
	u16 type2;
};

/* Core switch structure*/
struct switch_data {
	u32 switch_status;
	u32 switch_ctrl;
	u32 switch_prt;
	u8 sw_mac_addr[6];
	/*0 - schedule, 1 - reserved, 2 - best effort queue*/
	struct thershold thld_ep_mac[3];
	struct thershold thld_mac_mac[3];
	u32 ep_vlan;
	u32 mac_vlan;
	u32 max_frame_sc_que;
	u32 max_frame_res_que;
	u32 max_frame_be_que;
	/* Memory counters */
	struct mem_static_arr_cntr mem_arr_cnt;
	/* CAM */
	struct cam_struct cam_data;
/* Frame Filtering Type Field Option */
	struct ff_type typefield;
/* MAC Port-1 Management Queueing Options */
	int mac1_config;
/* MAC Port-2 Management Queueing Options */
	int mac2_config;
/* Port VLAN Membership Registers */
	int port_vlan_mem_ctrl;
	char port_vlan_mem_data;
};

/********* Switch Structures ends ***********/

extern struct axienet_local lp;

/********* qci function declararions ********/
void psfp_control(struct psfp_config data);
void config_stream_filter(struct stream_filter data);
void program_meter_reg(struct meter_config data);
void get_psfp_static_counter(struct psfp_static_counter *data);
void get_meter_reg(struct meter_config *data);
void get_stream_filter_config(struct stream_filter *data);

/********* cb function declararions ********/
void frer_control(struct frer_ctrl data);
void get_ingress_filter_config(struct in_fltr *data);
void config_ingress_filter(struct in_fltr data);
void get_member_reg(struct frer_memb_config *data);
void program_member_reg(struct frer_memb_config data);
void get_frer_static_counter(struct frer_static_counter *data);
#endif /* XILINX_TSN_SWITCH_H */
