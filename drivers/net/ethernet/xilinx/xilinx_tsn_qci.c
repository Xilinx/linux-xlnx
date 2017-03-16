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

#define SMC_MODE_SHIFT				28
#define	SMC_CBR_MASK				0x00FFFFFF
#define	SMC_EBR_MASK				0x00FFFFFF
#define IN_PORTID_MASK				0x3
#define IN_PORT_SHIFT				14
#define MAX_FR_SIZE_MASK			0x00000FFF

#define GATE_ID_SHIFT				24
#define METER_ID_SHIFT				8
#define EN_METER_SHIFT				6
#define ALLOW_STREM_SHIFT			5
#define EN_PSFP_SHIFT				4
#define WR_OP_TYPE_MASK				0x3
#define WR_OP_TYPE_SHIFT			2
#define OP_TYPE_SHIFT				1
#define PSFP_EN_CONTROL_MASK			0x1

/**
 * psfp_control - Configure thr control for PSFP
 * @data:	Value to be programmed
 */
void psfp_control(struct psfp_config data)
{
	u32 mask;
	u32 timeout = 20000;

	mask = data.gate_id << GATE_ID_SHIFT;
	mask |= data.meter_id << METER_ID_SHIFT;
	mask |= data.en_meter << EN_METER_SHIFT;
	mask |= data.allow_stream << ALLOW_STREM_SHIFT;
	mask |= data.en_psfp << EN_PSFP_SHIFT;
	mask |= (data.wr_op_type & WR_OP_TYPE_MASK) << WR_OP_TYPE_SHIFT;
	mask |= data.op_type << OP_TYPE_SHIFT;
	mask |= PSFP_EN_CONTROL_MASK;

	axienet_iow(&lp, PSFP_CONTROL_OFFSET, mask);

	/* wait for write to complete */
	while ((axienet_ior(&lp, PSFP_CONTROL_OFFSET) &
		PSFP_EN_CONTROL_MASK) && timeout)
		timeout--;

	if (!timeout)
		pr_warn("PSFP control write took longer time!!");
}

/**
 * get_stream_filter_config -  Get Stream Filter Configuration
 * @data:	Value returned
 */
void get_stream_filter_config(struct stream_filter *data)
{
	u32 reg_val;

	reg_val = axienet_ior(&lp, STREAM_FILTER_CONFIG_OFFSET);

	data->max_fr_size = reg_val & MAX_FR_SIZE_MASK;
	data->in_pid = (reg_val >> IN_PORT_SHIFT) & IN_PORTID_MASK;
}

/**
 * config_stream_filter -  Configure Stream Filter Configuration
 * @data:	Value to be programmed
 */
void config_stream_filter(struct stream_filter data)
{
	u32 mask;

	mask = ((data.in_pid & IN_PORTID_MASK) << IN_PORT_SHIFT) |
					(data.max_fr_size & MAX_FR_SIZE_MASK);
	axienet_iow(&lp, STREAM_FILTER_CONFIG_OFFSET, mask);
}

/**
 * get_meter_reg -  Read Stream Meter Configuration registers value
 * @data:	Value returned
 */
void get_meter_reg(struct meter_config *data)
{
	u32 conf_r4;

	data->cir = axienet_ior(&lp, STREAM_METER_CIR_OFFSET);
	data->eir = axienet_ior(&lp, STREAM_METER_EIR_OFFSET);
	data->cbr = axienet_ior(&lp, STREAM_METER_CBR_OFFSET) & SMC_CBR_MASK;
	conf_r4 = axienet_ior(&lp, STREAM_METER_EBR_OFFSET);

	data->ebr = conf_r4 & SMC_EBR_MASK;
	data->mode = (conf_r4 & 0xF0000000) >> SMC_MODE_SHIFT;
}

/**
 * program_meter_reg -  configure Stream Meter Configuration registers
 * @data:	Value to be programmed
 */
void program_meter_reg(struct meter_config data)
{
	u32 conf_r4;

	axienet_iow(&lp, STREAM_METER_CIR_OFFSET, data.cir);
	axienet_iow(&lp, STREAM_METER_EIR_OFFSET, data.eir);
	axienet_iow(&lp, STREAM_METER_CBR_OFFSET, data.cbr & SMC_CBR_MASK);

	conf_r4 = (data.ebr & SMC_EBR_MASK) | (data.mode << SMC_MODE_SHIFT);
	axienet_iow(&lp, STREAM_METER_EBR_OFFSET, conf_r4);
}

/**
 * get_psfp_static_counter -  get memory static counters value
 * @data  :	return value, containing counter value
 */
void get_psfp_static_counter(struct psfp_static_counter *data)
{
	int offset = (data->num) * 8;

	data->psfp_fr_count.lsb = axienet_ior(&lp, TOTAL_PSFP_FRAMES_OFFSET +
									offset);
	data->psfp_fr_count.msb = axienet_ior(&lp, TOTAL_PSFP_FRAMES_OFFSET  +
								offset + 0x4);

	data->err_filter_ins_port.lsb = axienet_ior(&lp,
					FLTR_INGS_PORT_ERR_OFFSET + offset);
	data->err_filter_ins_port.msb = axienet_ior(&lp,
				FLTR_INGS_PORT_ERR_OFFSET + offset + 0x4);

	data->err_filtr_sdu.lsb = axienet_ior(&lp, FLTR_STDU_ERR_OFFSET +
									offset);
	data->err_filtr_sdu.msb = axienet_ior(&lp, FLTR_STDU_ERR_OFFSET +
								offset + 0x4);

	data->err_meter.lsb = axienet_ior(&lp, METER_ERR_OFFSET + offset);
	data->err_meter.msb = axienet_ior(&lp, METER_ERR_OFFSET + offset + 0x4);
}
