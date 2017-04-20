/*
 * Xilinx FPGA Xilinx TSN QCI Controller module.
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

#define IN_PORTID_MASK				0x3
#define IN_PORTID_SHIFT				24
#define MAX_SEQID_MASK				0x0000FFFF

#define SEQ_REC_HIST_LEN_MASK			0x000000FF
#define SEQ_REC_HIST_LEN_SHIFT			16
#define SPLIT_STREAM_INPORTID_SHIFT		12
#define SPLIT_STREAM_INPORTID_MASK		0x3
#define SPLIT_STREAM_VLANID_MASK		0x00000FFF

#define GATE_ID_SHIFT				24
#define MEMBER_ID_SHIFT				8
#define SEQ_RESET_SHIFT				7
#define REC_TIMEOUT_SHIFT			6
#define GATE_STATE_SHIFT			5
#define FRER_VALID_SHIFT			4
#define WR_OP_TYPE_SHIFT			2
#define OP_TYPE_SHIFT				1
#define WR_OP_TYPE_MASK				0x3
#define FRER_EN_CONTROL_MASK			0x1

/**
 * frer_control - Configure thr control for frer
 * @data:	Value to be programmed
 */
void frer_control(struct frer_ctrl data)
{
	u32 mask = 0;

	mask = data.gate_id << GATE_ID_SHIFT;
	mask |= data.memb_id << MEMBER_ID_SHIFT;
	mask |= data.seq_reset << SEQ_RESET_SHIFT;
	mask |= data.gate_state << GATE_STATE_SHIFT;
	mask |= data.rcvry_tmout << REC_TIMEOUT_SHIFT;
	mask |= data.frer_valid << FRER_VALID_SHIFT;
	mask |= (data.wr_op_type & WR_OP_TYPE_MASK) << WR_OP_TYPE_SHIFT;
	mask |= data.op_type << OP_TYPE_SHIFT;
	mask |= FRER_EN_CONTROL_MASK;

	axienet_iow(&lp, FRER_CONTROL_OFFSET, mask);

	/* wait for write to complete */
	while ((axienet_ior(&lp, FRER_CONTROL_OFFSET) & FRER_EN_CONTROL_MASK))
		;
}

/**
 * get_ingress_filter_config -  Get Ingress Filter Configuration
 * @data:	Value returned
 */
void get_ingress_filter_config(struct in_fltr *data)
{
	u32 reg_val = 0;

	reg_val = axienet_ior(&lp, INGRESS_FILTER_OFFSET);

	data->max_seq_id = reg_val & MAX_SEQID_MASK;
	data->in_port_id = (reg_val >> IN_PORTID_SHIFT) & IN_PORTID_MASK;
}

/**
 * config_stream_filter -  Configure Ingress Filter Configuration
 * @data:	Value to be programmed
 */
void config_ingress_filter(struct in_fltr data)
{
	u32 mask = 0;

	mask = ((data.in_port_id & IN_PORTID_MASK) << IN_PORTID_SHIFT) |
					(data.max_seq_id & MAX_SEQID_MASK);
	axienet_iow(&lp, INGRESS_FILTER_OFFSET, mask);
}

/**
 * get_member_reg -  Read frer member Configuration registers value
 * @data:	Value returned
 */
void get_member_reg(struct frer_memb_config *data)
{
	u32 conf_r1 = 0;

	conf_r1 = axienet_ior(&lp, FRER_CONFIG_REG1);
	data->rem_ticks = axienet_ior(&lp, FRER_CONFIG_REG2);

	data->seq_rec_hist_len = (conf_r1 >> SEQ_REC_HIST_LEN_SHIFT)
						& SEQ_REC_HIST_LEN_MASK;
	data->split_strm_egport_id = (conf_r1 >> SPLIT_STREAM_INPORTID_SHIFT)
						& SPLIT_STREAM_INPORTID_MASK;
	data->split_strm_vlan_id = conf_r1 & SPLIT_STREAM_VLANID_MASK;
}

/**
 * program_member_reg -  configure frer member Configuration registers
 * @data:	Value to be programmed
 */
void program_member_reg(struct frer_memb_config data)
{
	u32 conf_r1 = 0;

	conf_r1 = (data.seq_rec_hist_len & SEQ_REC_HIST_LEN_MASK)
						<< SEQ_REC_HIST_LEN_SHIFT;
	conf_r1 = conf_r1 | ((data.split_strm_egport_id
					& SPLIT_STREAM_INPORTID_MASK)
					<< SPLIT_STREAM_INPORTID_SHIFT);
	conf_r1 = conf_r1 | (data.split_strm_vlan_id
					& SPLIT_STREAM_VLANID_MASK);

	axienet_iow(&lp, FRER_CONFIG_REG1, conf_r1);
	axienet_iow(&lp, FRER_CONFIG_REG2, data.rem_ticks);
}

/**
 * get_frer_static_counter -  get frer static counters value
 * @data:	return value, containing counter value
 */
void get_frer_static_counter(struct frer_static_counter *data)
{
	int offset = (data->num) * 8;

	data->frer_fr_count.lsb = axienet_ior(&lp, TOTAL_FRER_FRAMES_OFFSET +
									offset);
	data->frer_fr_count.msb = axienet_ior(&lp, TOTAL_FRER_FRAMES_OFFSET +
								offset + 0x4);

	data->disc_frames_in_portid.lsb = axienet_ior(&lp,
				FRER_DISCARD_INGS_FLTR_OFFSET + offset);
	data->disc_frames_in_portid.msb = axienet_ior(&lp,
				FRER_DISCARD_INGS_FLTR_OFFSET + offset + 0x4);

	data->pass_frames_ind_recv.lsb = axienet_ior(&lp,
				FRER_PASS_FRAMES_INDV_OFFSET + offset);
	data->pass_frames_ind_recv.msb = axienet_ior(&lp,
				FRER_PASS_FRAMES_INDV_OFFSET + offset + 0x4);

	data->disc_frames_ind_recv.lsb = axienet_ior(&lp,
				FRER_DISCARD_FRAMES_INDV_OFFSET + offset);
	data->disc_frames_ind_recv.msb = axienet_ior(&lp,
				FRER_DISCARD_FRAMES_INDV_OFFSET + offset + 0x4);

	data->pass_frames_seq_recv.lsb = axienet_ior(&lp,
				FRER_PASS_FRAMES_SEQ_OFFSET + offset);
	data->pass_frames_seq_recv.msb = axienet_ior(&lp,
				FRER_PASS_FRAMES_SEQ_OFFSET + offset + 0x4);

	data->disc_frames_seq_recv.lsb = axienet_ior(&lp,
				FRER_DISCARD_FRAMES_SEQ_OFFSET + offset);
	data->disc_frames_seq_recv.msb = axienet_ior(&lp,
				FRER_DISCARD_FRAMES_SEQ_OFFSET + offset + 0x4);

	data->rogue_frames_seq_recv.lsb = axienet_ior(&lp,
				FRER_ROGUE_FRAMES_SEQ_OFFSET + offset);
	data->rogue_frames_seq_recv.msb = axienet_ior(&lp,
				FRER_ROGUE_FRAMES_SEQ_OFFSET + offset + 0x4);

	data->seq_recv_rst.lsb = axienet_ior(&lp,
				SEQ_RECV_RESETS_OFFSET + offset);
	data->seq_recv_rst.msb = axienet_ior(&lp,
				SEQ_RECV_RESETS_OFFSET + offset + 0x4);
}
