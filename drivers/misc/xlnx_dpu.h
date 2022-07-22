/* SPDX-License-Identifier: GPL-2.0 OR Apache-2.0 */
/*
 * Xilinx Vivado Flow Deep learning Processing Unit (DPU) Driver
 *
 * Copyright (C) 2022 Xilinx, Inc.
 * Copyright (C) 2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Authors:
 *    Ye Yang <ye.yang@amd.com>
 */

#ifndef _DPU_UAPI_H_
#define _DPU_UAPI_H_

/* up to 4 dpu cores and 1 softmax core */
#define MAX_CU_NUM		5
#define TIMEOUT			(timeout * CONFIG_HZ)
#define TIMEOUT_US		(timeout * 1000000)
#define POLL_PERIOD_US		(2000)

/* DPU fingerprint, target info */
#define DPU_PMU_IP_RST		(0x004)
#define DPU_IPVER_INFO		(0x1E0)
#define DPU_IPFREQENCY		(0x1E4)
#define DPU_TARGETID_L		(0x1F0)
#define DPU_TARGETID_H		(0x1F4)

/* DPU core0-3 registers */
#define DPU_HPBUS(x)		(0x200 + ((x) << 8))
#define DPU_INSADDR(x)		(0x20C + ((x) << 8))
#define DPU_IPSTART(x)		(0x220 + ((x) << 8))
#define DPU_ADDR0_L(x)		(0x224 + ((x) << 8))
#define DPU_ADDR0_H(x)		(0x228 + ((x) << 8))
#define DPU_ADDR1_L(x)		(0x22C + ((x) << 8))
#define DPU_ADDR1_H(x)		(0x230 + ((x) << 8))
#define DPU_ADDR2_L(x)		(0x234 + ((x) << 8))
#define DPU_ADDR2_H(x)		(0x238 + ((x) << 8))
#define DPU_ADDR3_L(x)		(0x23C + ((x) << 8))
#define DPU_ADDR3_H(x)		(0x240 + ((x) << 8))
#define DPU_ADDR4_L(x)		(0x244 + ((x) << 8))
#define DPU_ADDR4_H(x)		(0x248 + ((x) << 8))
#define DPU_ADDR5_L(x)		(0x24C + ((x) << 8))
#define DPU_ADDR5_H(x)		(0x250 + ((x) << 8))
#define DPU_ADDR6_L(x)		(0x254 + ((x) << 8))
#define DPU_ADDR6_H(x)		(0x258 + ((x) << 8))
#define DPU_ADDR7_L(x)		(0x25C + ((x) << 8))
#define DPU_ADDR7_H(x)		(0x260 + ((x) << 8))
#define DPU_P_END_C(x)		(0x264 + ((x) << 8))
#define DPU_C_END_C(x)		(0x268 + ((x) << 8))
#define DPU_S_END_C(x)		(0x26C + ((x) << 8))
#define DPU_L_END_C(x)		(0x270 + ((x) << 8))
#define DPU_P_STA_C(x)		(0x274 + ((x) << 8))
#define DPU_C_STA_C(x)		(0x278 + ((x) << 8))
#define DPU_S_STA_C(x)		(0x27C + ((x) << 8))
#define DPU_L_STA_C(x)		(0x280 + ((x) << 8))
#define DPU_AXI_STS(x)		(0x284 + ((x) << 8))
#define DPU_CYCLE_L(x)		(0x290 + ((x) << 8))
#define DPU_CYCLE_H(x)		(0x294 + ((x) << 8))

/* DPU INT Registers */
#define DPU_INT_STS		(0x600)
#define DPU_INT_MSK		(0x604)
#define DPU_INT_RAW		(0x608)
#define DPU_INT_ICR		(0x60C)

/* DPU Softmax Registers */
#define DPU_SFM_INT_DONE	(0x700)
#define DPU_SFM_CMD_XLEN	(0x704)
#define DPU_SFM_CMD_YLEN	(0x708)
#define DPU_SFM_SRC_ADDR	(0x70C)
#define DPU_SFM_DST_ADDR	(0x710)
#define DPU_SFM_CMD_SCAL	(0x714)
#define DPU_SFM_CMD_OFF		(0x718)
#define DPU_SFM_INT_CLR		(0x71C)
#define DPU_SFM_START		(0x720)
#define DPU_SFM_RESET		(0x730)
#define DPU_SFM_MODE		(0x738)
#define DPU_REG_END		(0x800)

#define DPU_NUM(x)		(GENMASK(3, 0) & (x))
#define DPU_FREQ(x)		(GENMASK(11, 0) & (x))
#define SFM_NUM(x)		((GENMASK(7, 4) & (x)) >> 4)
#define DPU_VER(x)		((GENMASK(31, 24) & (x)) >> 24)
#define DPU_SUB_VER(x)		((GENMASK(23, 16) & (x)) >> 16)
#define DPU_SAXI(x)		((GENMASK(23, 12) & (x)) >> 12)

#define DPU_HPBUS_VAL		(0x07070f0f)
#define DPU_RST_ALL_CORES	(0xF)
#define DPU_INSTR_OFFSET	(12)
#define DPU_IP_V3_4		(0x34)

enum DPU_DMA_DIR {
	CPU_TO_DPU = 0,
	DPU_TO_CPU = 1
};

struct dpcma_req_free {
	u64 phy_addr;
	size_t capacity;
};

struct dpcma_req_alloc {
	size_t size;
	u64 phy_addr;
	size_t capacity;
};

struct dpcma_req_sync {
	u64 phy_addr;
	size_t size;
	int direction;
};

/**
 * struct  ioc_kernel_run_t - describe structure for each dpu ioctl
 * @addr_code:	the address for DPU code
 * @addr0:	address reg0
 * @addr1:	address reg1
 * @addr2:	address reg2
 * @addr3:	address reg3
 * @addr4:	address reg4
 * @addr5:	address reg5
 * @addr6:	address reg6
 * @addr7:	address reg7
 * @time_start:	the start timestamp before running
 * @time_end:	the end timestamp after running
 * @counter:	nums of total cycles
 * @core_id:	the DPU core id
 * @pend_cnt:	finished counts of dpu misc
 * @cend_cnt:	finished counts of dpu conv
 * @send_cnt:	finished counts of dpu save
 * @lend_cnt:	finished counts of dpu load
 * @pstart_cnt:	initiated counts of dpu misc
 * @cstart_cnt:	initiated counts of dpu conv
 * @sstart_cnt:	initiated counts of dpu save
 * @lstart_cnt:	initiated counts of dpu load
 */
struct ioc_kernel_run_t {
	u64 addr_code;
	u64 addr0;
	u64 addr1;
	u64 addr2;
	u64 addr3;
	u64 addr4;
	u64 addr5;
	u64 addr6;
	u64 addr7;
	u64 time_start;
	u64 time_end;
	u64 counter;
	int core_id;
	u32 pend_cnt;
	u32 cend_cnt;
	u32 send_cnt;
	u32 lend_cnt;
	u32 pstart_cnt;
	u32 cstart_cnt;
	u32 sstart_cnt;
	u32 lstart_cnt;
};

/**
 * struct  ioc_softmax_t - describe structure for each softmax ioctl
 * @width:	width dimension of Tensor
 * @height:	height dimension of Tensor
 * @input:	physical address of input Tensor
 * @output:	physical address of output Tensor
 * @scale:	quantization info of input Tensor
 * @offset:	offset value for input Tensor
 */
struct ioc_softmax_t {
	u32 width;
	u32 height;
	u32 input;
	u32 output;
	u32 scale;
	u32 offset;
};

#define DPU_IOC_MAGIC 'D'

#define DPUIOC_CREATE_BO _IOWR(DPU_IOC_MAGIC, 1, struct dpcma_req_alloc*)
#define DPUIOC_FREE_BO _IOWR(DPU_IOC_MAGIC, 2, struct dpcma_req_free*)
#define DPUIOC_SYNC_BO _IOWR(DPU_IOC_MAGIC, 3, struct dpcma_req_sync*)
#define DPUIOC_G_INFO _IOR(DPU_IOC_MAGIC, 4, u32)
#define DPUIOC_G_TGTID _IOR(DPU_IOC_MAGIC, 5, u64)
#define DPUIOC_RUN _IOWR(DPU_IOC_MAGIC, 6, struct ioc_kernel_run_t*)
#define DPUIOC_RUN_SOFTMAX _IOWR(DPU_IOC_MAGIC, 7, struct ioc_softmax_t*)
#define DPUIOC_REG_READ _IOR(DPU_IOC_MAGIC, 8, u32)

#endif /* _DPU_UAPI_H_ */
