/*
 * Xilinx AXI-JESD204B v5.1 Interface Module
 *
 * Copyright 2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 * http://wiki.analog.com/resources/fpga/xilinx/
 */

#ifndef XILINX_JESD204B_H_
#define XILINX_JESD204B_H_

struct jesd204b_state {
	struct device	*dev;
	void __iomem	*regs;
	void __iomem	*phy;
	struct clk	*clk;
	u32		lanes;
	u32		vers_id;
	u32		addr;
	u32		band;
	u32		transmit;
	u32		pll;
	unsigned long	rate;
};

#define XLNX_JESD204_REG_VERSION		0x000
#define XLNX_JESD204_VERSION_MAJOR(x)		(((x) >> 24) & 0xFF)
#define XLNX_JESD204_VERSION_MINOR(x)		(((x) >> 16) & 0xFF)
#define XLNX_JESD204_VERSION_REV(x)		(((x) >> 8) & 0xFF)

#define XLNX_JESD204_REG_RESET			0x004
#define XLNX_JESD204_RESET			(1 << 0)

#define XLNX_JESD204_REG_ILA_CTRL		0x008
#define XLNX_JESD204_ILA_EN			(1 << 0)

#define XLNX_JESD204_REG_SCR_CTRL		0x00C
#define XLNX_JESD204_SCR_EN			(1 << 0)

#define XLNX_JESD204_REG_SYSREF_CTRL		0x010
#define XLNX_JESD204_ALWAYS_SYSREF_EN		(1 << 0)

#define XLNX_JESD204_REG_ILA_MFC		0x014
#define XLNX_JESD204_ILA_MFC(x)			(((x) - 1) & 0xFF)
						/* TX only 4..256 */

#define XLNX_JESD204_REG_TEST_MODE_SEL		0x018
#define XLNX_JESD204_TEST_MODE_OFF		0 /* Normal operation */
#define XLNX_JESD204_TEST_MODE_K28_5		1 /* Send/Receive /K28.5/
						   * indefinitely
						   */
#define XLNX_JESD204_TEST_MODE_ILA		2 /* Synchronize as normal then
						   * send/receive repeated ILA
						   * sequences
						   */
#define XLNX_JESD204_TEST_MODE_D21_5		3 /* Send/Receive /D21.5/
						   * indefinitely
						   */
#define XLNX_JESD204_TEST_MODE_RPAT		5 /* Send/Receive modified
						   * random pattern (RPAT)
						   */
#define XLNX_JESD204_TEST_MODE_JSPAT		7 /* Send/Receive a scrambled
						   * jitter pattern (JSPAT)
						   */

#define XLNX_JESD204_REG_SYNC_STATUS		0x038 /* Link SYNC status */
#define XLNX_JESD204_REG_SYNC_ERR_STAT		0x01C /* RX only */
#define XLNX_JESD204_SYNC_ERR_NOT_IN_TAB(lane)		(1 << (0 + (lane) * 3))
#define XLNX_JESD204_SYNC_ERR_DISPARITY(lane)		(1 << (1 + (lane) * 3))
#define XLNX_JESD204_SYNC_ERR_UNEXPECTED_K(lane)	(1 << (2 + (lane) * 3))

#define XLNX_JESD204_REG_OCTETS_PER_FRAME	0x020
#define XLNX_JESD204_OCTETS_PER_FRAME(x)	(((x) - 1) & 0xFF) /* 1..256 */

#define XLNX_JESD204_REG_FRAMES_PER_MFRAME	0x024
#define XLNX_JESD204_FRAMES_PER_MFRAME(x)	(((x) - 1) & 0x1F) /* 1..32 */

#define XLNX_JESD204_REG_LANES			0x028
#define XLNX_JESD204_LANES(x)			(((x) - 1) & 0x1F) /* 1..32 */

#define XLNX_JESD204_REG_SUBCLASS		0x02C

#define XLNX_JESD204_REG_RX_BUF_DELAY		0x030 /* RX only */
#define XLNX_JESD204_RX_BUF_DELAY(x)		((x) & 0x1FFF)

#define XLNX_JESD204_REG_RX_LINK_CTRL		0x034 /* RX only */
#define XLNX_JESD204_LINK_TEST_EN		(1 << 0)
#define XLNX_JESD204_SYNC_ERR_REP_DIS		(1 << 8)

/* Per LANE Registers */
#define XLNX_JESD204_REG_LANE_VERSION(l)	(0x800 + ((l) * 0x40))
#define XLNX_JESD204_LANE_SUBCLASS(x)		(((x) >> 0) & 0x7)
#define XLNX_JESD204_LANE_JESDV(x)		(((x) >> 8) & 0x7)

#define XLNX_JESD204_REG_LANE_F(l)		(0x804 + ((l) * 0x40))
#define XLNX_JESD204_LANE_F(x)			((((x) >> 0) & 0xFF) + 1)

#define XLNX_JESD204_REG_LANE_K(l)		(0x808 + ((l) * 0x40))
#define XLNX_JESD204_LANE_K(x)			((((x) >> 0) & 0x1F) + 1)

#define XLNX_JESD204_REG_ID_L(l)		(0x80C + ((l) * 0x40))
#define XLNX_JESD204_LANE_DID(x)		(((x) >> 0) & 0xFF)
#define XLNX_JESD204_LANE_BID(x)		(((x) >> 8) & 0x1F)
#define XLNX_JESD204_LANE_LID(x)		(((x) >> 16) & 0x1F)
#define XLNX_JESD204_LANE_L(x)			((((x) >> 24) & 0x1F) + 1)

#define XLNX_JESD204_REG_M_N_ND_CS(l)		(0x810 + ((l) * 0x40))
#define XLNX_JESD204_LANE_M(x)			((((x) >> 0) & 0xFF) + 1)
#define XLNX_JESD204_LANE_N(x)			((((x) >> 8) & 0x1F) + 1)
#define XLNX_JESD204_LANE_ND(x)			((((x) >> 16) & 0x1F) + 1)
#define XLNX_JESD204_LANE_CS(x)			(((x) >> 24) & 0x3)

#define XLNX_JESD204_REG_SCR_S_HD_CF(l)		(0x814 + ((l) * 0x40))
#define XLNX_JESD204_LANE_SCR(x)		(((x) >> 0) & 0x1)
#define XLNX_JESD204_LANE_S(x)			((((x) >> 8) & 0x1F) + 1)
#define XLNX_JESD204_LANE_HD(x)			(((x) >> 16) & 0x1)
#define XLNX_JESD204_LANE_CF(x)			(((x) >> 24) & 0x1F)

#define XLNX_JESD204_REG_FCHK(l)		(0x818 + ((l) * 0x40))
#define XLNX_JESD204_LANE_FCHK(x)		(((x) >> 16) & 0xFF)

#define XLNX_JESD204_REG_SC2_ADJ_CTRL(l)	(0x81C + ((l) * 0x40))
#define XLNX_JESD204_LANE_ADJ_CNT(x)		(((x) >> 0) & 0xF)
#define XLNX_JESD204_LANE_PHASE_ADJ_REQ(x)	(((x) >> 8) & 0x1)
#define XLNX_JESD204_LANE_ADJ_CNT_DIR(x)		(((x) >> 16) & 0x1)

#define XLNX_JESD204_REG_TM_ERR_CNT(l)		(0x820 + ((l) * 0x40))
#define XLNX_JESD204_REG_TM_LINK_ERR_CNT(l)	(0x824 + ((l) * 0x40))
#define XLNX_JESD204_REG_TM_ILA_CNT(l)		(0x828 + ((l) * 0x40))
#define XLNX_JESD204_REG_TM_MFC_CNT(l)		(0x82C + ((l) * 0x40))
#define XLNX_JESD204_REG_TM_BUF_ADJ(l)		(0x830 + ((l) * 0x40))

#endif /* ADI_JESD204B_V51_H_ */
