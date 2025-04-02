// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine driver AIE-2PS device specific implementation
 *
 * Copyright (C) 2023 - 2025 Advanced Micro Devices, Inc.
 */

#include "ai-engine-internal.h"

#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define AIE_PM_OPS_PKT_SIZE	200 /* bytes */

int aie_part_pm_ops_create(struct aie_partition *apart)
{
	struct aie_pm_ops *pm_ops = &apart->pm_ops;

	if (apart->adev->dev_gen != AIE_DEVICE_GEN_AIE2PS)
		return 0;

	pm_ops->pkt_va = dmam_alloc_coherent(&apart->dev, AIE_PM_OPS_PKT_SIZE, &pm_ops->pkt_dma,
					     GFP_KERNEL);
	if (!pm_ops->pkt_va)
		return -ENOMEM;
	memset(pm_ops->pkt_va, 0, AIE_PM_OPS_PKT_SIZE);

	pm_ops->size = AIE_PM_OPS_PKT_SIZE;
	pm_ops->offset = 0;
	pm_ops->op_range = NULL;

	return 0;
}

void aie_part_pm_ops_free(struct aie_partition *apart)
{
	if (!apart->pm_ops.pkt_va)
		return;
	dmam_free_coherent(&apart->dev, apart->pm_ops.size, apart->pm_ops.pkt_va,
			   apart->pm_ops.pkt_dma);
}

int aie_part_pm_ops_flush(struct aie_partition *apart)
{
	struct aie_pm_ops *pm_ops = &apart->pm_ops;
	int ret;

	if (!pm_ops->offset)
		return 0;

	/* Last pkt is aie_op_start_num_col. remove it. */
	if ((pm_ops->pkt_va + pm_ops->offset) == (pm_ops->op_range + sizeof(*pm_ops->op_range)))
		pm_ops->offset -= sizeof(*pm_ops->op_range);

	ret = versal2_pm_aie2ps_operation(apart->adev->pm_node_id, pm_ops->offset,
					  pm_ops->pkt_dma & 0xFFFFFFFFULL,
					  pm_ops->pkt_dma >> 32);

	pm_ops->offset = 0;
	pm_ops->op_range = NULL;
	memset(pm_ops->pkt_va, 0, AIE_PM_OPS_PKT_SIZE);

	return ret;
}

int aie_part_pm_ops(struct aie_partition *apart, void *data, u32 type, struct aie_range range,
		    bool flush)
{
	struct aie_pm_ops *pm_ops = &apart->pm_ops;
	struct aie_op_start_num_col *op_range;
	ssize_t end;
	int ret;

again:
	op_range = pm_ops->op_range;
	if (!op_range ||
	    op_range->num_col != range.size.col ||
	    op_range->start_col != range.start.col) {
		if (check_add_overflow(pm_ops->offset, sizeof(*op_range), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
		}
		pm_ops->op_range = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*pm_ops->op_range);
		op_range = pm_ops->op_range;
		op_range->type = XILINX_AIE_OPS_START_NUM_COL;
		op_range->start_col = range.start.col;
		op_range->num_col = range.size.col;
		op_range->len = sizeof(*op_range);
	}

	if (type & AIE_PART_INIT_OPT_COLUMN_RST) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_COLUMN_RST;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_COL_RST;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_SHIM_RST) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_SHIM_RST;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_SHIM_RST;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_BLOCK_NOCAXIMMERR;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ENB_AXI_MM_ERR_EVENT;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_ENB_COLCLK_BUFF) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_ENB_COLCLK_BUFF;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ENB_COL_CLK_BUFF;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_DIS_COLCLK_BUFF) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_DIS_COLCLK_BUFF;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_DIS_COL_CLK_BUFF;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_ZEROIZEMEM) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_ZEROIZEMEM;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ZEROISATION;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_UC_ENB_MEM_PRIV) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_UC_ENB_MEM_PRIV;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ENB_MEM_PRIV;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_UC_DIS_MEM_PRIV) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_UC_DIS_MEM_PRIV;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_DIS_MEM_PRIV;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_ENB_NOC_DMA_PAUSE) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_ENB_NOC_DMA_PAUSE;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ENB_NOC_DMA_PAUSE;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_ENB_UC_DMA_PAUSE) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_ENB_UC_DMA_PAUSE;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_ENB_UC_DMA_PAUSE;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_DIS_MEM_INTERLEAVE) {
		struct aie_op_type_len *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_DIS_MEM_INTERLEAVE;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_DIS_MEM_INTERLEAVE;
		op->len = sizeof(*op);
	}
	if (type & AIE_PART_INIT_OPT_UC_ZEROIZATION) {
		struct aie_op_uc_zeroisation *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_UC_ZEROIZATION;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_UC_ZEROIZATION;
		op->len = sizeof(*op);
		op->flag = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_ISOLATE) {
		struct aie_op_aximm_isolation *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_ISOLATE;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_AXIMM_ISOLATION;
		op->len = sizeof(*op);
		op->traffic = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_SET_L2_IRQ) {
		struct aie_op_l2_ctrl_irq *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_SET_L2_IRQ;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_SET_L2_CTRL_NPI_INTR;
		op->len = sizeof(*op);
		op->irq = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_NMU_CONFIG) {
		struct aie_op_nmu_switch *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_NMU_CONFIG;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_NMU_CONFIG;
		op->len = sizeof(*op);
		op->c0_route = 0x1;
		op->c1_route = 0x2;
	}
	if (type & AIE_PART_INIT_OPT_HW_ERR_INT) {
		struct aie_op_hw_err *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_HW_ERR_INT;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_HW_ERR_INT;
		op->len = sizeof(*op);
		op->val = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_HW_ERR_MASK) {
		struct aie_op_hw_err *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_HW_ERR_MASK;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_HW_ERR_MASK;
		op->len = sizeof(*op);
		op->val = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_SET_ECC_SCRUB_PERIOD) {
		struct aie_op_ecc_scrub_period *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_SET_ECC_SCRUB_PERIOD;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_SET_ECC_SCRUB_PERIOD;
		op->len = sizeof(*op);
		op->scrub_period = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_HW_ERR_STS) {
		struct aie_op_hw_err *op;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}
		type &= ~AIE_PART_INIT_OPT_HW_ERR_STS;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_CLR_HW_ERR_STS;
		op->len = sizeof(*op);
		op->val = *((u16 *)data);
	}
	if (type & AIE_PART_INIT_OPT_HANDSHAKE) {
		struct aie_op_handshake *op;
		struct aie_op_handshake_data *hs_data = data;
		void *hs_va;
		dma_addr_t hs_dma;

		if (check_add_overflow(pm_ops->offset, sizeof(*op), &end) ||
		    end >= pm_ops->size) {
			ret = aie_part_pm_ops_flush(apart);
			if (ret)
				return ret;
			goto again;
		}

		if (virt_addr_valid(hs_data->addr)) {
			hs_va = dmam_alloc_coherent(&apart->dev, hs_data->size, &hs_dma,
						    GFP_KERNEL);
			if (!hs_va)
				return -ENOMEM;
			memcpy(hs_va, hs_data->addr, hs_data->size);

		} else {
			hs_va = hs_data->addr;
			hs_dma = dma_map_single(&apart->dev, hs_va, hs_data->size, DMA_TO_DEVICE);
			if (dma_mapping_error(&apart->dev, hs_dma))
				return -ENOMEM;
			dma_sync_single_for_device(&apart->dev, hs_dma, hs_data->size,
						   DMA_TO_DEVICE);
		}

		type &= ~AIE_PART_INIT_OPT_HANDSHAKE;
		op = pm_ops->pkt_va + pm_ops->offset;
		pm_ops->offset += sizeof(*op);
		op->type = XILINX_AIE_OPS_HANDSHAKE;
		op->len = sizeof(*op) + hs_data->size;
		op->low_addr = hs_dma & 0xFFFFFFFFULL;
		op->high_addr = hs_dma >> 32;
		ret = aie_part_pm_ops_flush(apart);
		if (virt_addr_valid(hs_data->addr))
			dmam_free_coherent(&apart->dev, hs_data->size, hs_va, hs_dma);
		if (ret)
			return ret;
		goto again;
	}

	if (type)
		dev_warn(&apart->dev, "Unknown ops type: 0x%x", type);

	if (flush)
		ret = aie_part_pm_ops_flush(apart);

	return ret;
}
