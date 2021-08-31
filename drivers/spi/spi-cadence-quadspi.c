// SPDX-License-Identifier: GPL-2.0-only
//
// Driver for Cadence QSPI Controller
//
// Copyright Altera Corporation (C) 2012-2014. All rights reserved.
// Copyright Intel Corporation (C) 2019-2020. All rights reserved.
// Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define CQSPI_NAME			"cadence-qspi"
#define CQSPI_MAX_CHIPSELECT		16

/* Quirks */
#define CQSPI_NEEDS_WR_DELAY		BIT(0)
#define CQSPI_DISABLE_DAC_MODE		BIT(1)
#define CQSPI_HAS_DMA			BIT(2)
#define CQSPI_SUPPORT_RESET		BIT(3)

/* Capabilities */
#define CQSPI_SUPPORTS_OCTAL		BIT(0)

struct cqspi_st;

struct cqspi_flash_pdata {
	struct cqspi_st	*cqspi;
	u32		clk_rate;
	u32		read_delay;
	u32		tshsl_ns;
	u32		tsd2d_ns;
	u32		tchsh_ns;
	u32		tslch_ns;
	u8		inst_width;
	u8		addr_width;
	u8		data_width;
	u8		cs;
};

struct cqspi_st {
	struct platform_device	*pdev;

	struct clk		*clk;
	unsigned int		sclk;

	void __iomem		*iobase;
	void __iomem		*ahb_base;
	resource_size_t		ahb_size;
	struct completion	transfer_complete;

	struct dma_chan		*rx_chan;
	struct completion	rx_dma_complete;
	dma_addr_t		mmap_phys_base;

	int			current_cs;
	unsigned long		master_ref_clk_hz;
	bool			is_decoded_cs;
	u32			fifo_depth;
	u32			fifo_width;
	bool			rclk_en;
	u32			trigger_address;
	u32			wr_delay;
	bool			use_direct_mode;
	struct cqspi_flash_pdata f_pdata[CQSPI_MAX_CHIPSELECT];
	bool			read_dma;
	void			*rxbuf;
	int			bytes_to_rx;
	int			bytes_to_dma;
	loff_t			addr;
	dma_addr_t		dma_addr;
	u8			edge_mode;
	bool			extra_dummy;
	u8			access_mode;
	bool			unalined_byte_cnt;
	u8			dll_mode;
	u32			pm_dev_id;
	struct completion	tuning_complete;
	struct completion	request_complete;
	int (*indirect_read_dma)(struct cqspi_flash_pdata *f_pdata,
				 u_char *rxbuf, loff_t from_addr, size_t n_rx);
	int (*flash_reset)(struct cqspi_st *cqspi, u8 reset_type);
	int (*access_mode_switch)(struct cqspi_flash_pdata *f_pdata);
};

struct cqspi_driver_platdata {
	u32 hwcaps_mask;
	u8 quirks;
};

/* Operation timeout value */
#define CQSPI_TIMEOUT_MS			500
#define CQSPI_READ_TIMEOUT_MS			10
#define CQSPI_TUNING_TIMEOUT_MS			5000
#define CQSPI_TUNING_PERIODICITY_MS		300000

/* Instruction type */
#define CQSPI_INST_TYPE_SINGLE			0
#define CQSPI_INST_TYPE_DUAL			1
#define CQSPI_INST_TYPE_QUAD			2
#define CQSPI_INST_TYPE_OCTAL			3

#define CQSPI_DUMMY_CLKS_PER_BYTE		8
#define CQSPI_DUMMY_BYTES_MAX			4
#define CQSPI_DUMMY_CLKS_MAX			31

#define CQSPI_STIG_DATA_LEN_MAX			8

/* Register map */
#define CQSPI_REG_CONFIG			0x00
#define CQSPI_REG_CONFIG_ENABLE_MASK		BIT(0)
#define CQSPI_REG_CONFIG_PHY_ENABLE_MASK	BIT(3)
#define CQSPI_REG_CONFIG_ENB_DIR_ACC_CTRL	BIT(7)
#define CQSPI_REG_CONFIG_DECODE_MASK		BIT(9)
#define CQSPI_REG_CONFIG_CHIPSELECT_LSB		10
#define CQSPI_REG_CONFIG_DMA_MASK		BIT(15)
#define CQSPI_REG_CONFIG_AHB_ADDR_REMAP_MASK	BIT(16)
#define CQSPI_REG_CONFIG_DTR_PROT_EN_MASK	BIT(24)
#define CQSPI_REG_CONFIG_DUAL_BYTE_OP		BIT(30)
#define CQSPI_REG_CONFIG_BAUD_LSB		19
#define CQSPI_REG_CONFIG_DUAL_OP_LSB		30
#define CQSPI_REG_CONFIG_IDLE_LSB		31
#define CQSPI_REG_CONFIG_CHIPSELECT_MASK	0xF
#define CQSPI_REG_CONFIG_BAUD_MASK		0xF

#define CQSPI_REG_RD_INSTR			0x04
#define CQSPI_REG_RD_INSTR_OPCODE_LSB		0
#define CQSPI_REG_RD_INSTR_TYPE_INSTR_LSB	8
#define CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB	12
#define CQSPI_REG_RD_INSTR_TYPE_DATA_LSB	16
#define CQSPI_REG_RD_INSTR_MODE_EN_LSB		20
#define CQSPI_REG_RD_INSTR_DUMMY_LSB		24
#define CQSPI_REG_RD_INSTR_TYPE_INSTR_MASK	0x3
#define CQSPI_REG_RD_INSTR_TYPE_ADDR_MASK	0x3
#define CQSPI_REG_RD_INSTR_TYPE_DATA_MASK	0x3
#define CQSPI_REG_RD_INSTR_DUMMY_MASK		0x1F

#define CQSPI_REG_WR_INSTR			0x08
#define CQSPI_REG_WR_INSTR_OPCODE_LSB		0
#define CQSPI_REG_WR_INSTR_OPCODE_MASK		0xFF
#define CQSPI_REG_WR_INSTR_TYPE_ADDR_LSB	12
#define CQSPI_REG_WR_INSTR_TYPE_DATA_LSB	16

#define CQSPI_REG_DELAY				0x0C
#define CQSPI_REG_DELAY_TSLCH_LSB		0
#define CQSPI_REG_DELAY_TCHSH_LSB		8
#define CQSPI_REG_DELAY_TSD2D_LSB		16
#define CQSPI_REG_DELAY_TSHSL_LSB		24
#define CQSPI_REG_DELAY_TSLCH_MASK		0xFF
#define CQSPI_REG_DELAY_TCHSH_MASK		0xFF
#define CQSPI_REG_DELAY_TSD2D_MASK		0xFF
#define CQSPI_REG_DELAY_TSHSL_MASK		0xFF

#define CQSPI_REG_READCAPTURE			0x10
#define CQSPI_REG_READCAPTURE_BYPASS_LSB	0
#define CQSPI_REG_READCAPTURE_DELAY_LSB		1
#define CQSPI_REG_READCAPTURE_DELAY_MASK	0xF
#define CQSPI_REG_READCAPTURE_DQS_ENABLE	BIT(8)

#define CQSPI_REG_SIZE				0x14
#define CQSPI_REG_SIZE_ADDRESS_LSB		0
#define CQSPI_REG_SIZE_PAGE_LSB			4
#define CQSPI_REG_SIZE_BLOCK_LSB		16
#define CQSPI_REG_SIZE_ADDRESS_MASK		0xF
#define CQSPI_REG_SIZE_PAGE_MASK		0xFFF
#define CQSPI_REG_SIZE_BLOCK_MASK		0x3F

#define CQSPI_REG_SRAMPARTITION			0x18
#define CQSPI_REG_INDIRECTTRIGGER		0x1C

#define CQSPI_REG_DMA				0x20
#define CQSPI_REG_DMA_SINGLE_LSB		0
#define CQSPI_REG_DMA_BURST_LSB			8
#define CQSPI_REG_DMA_SINGLE_MASK		0xFF
#define CQSPI_REG_DMA_BURST_MASK		0xFF
#define CQSPI_REG_DMA_VAL			0x602

#define CQSPI_REG_REMAP				0x24
#define CQSPI_REG_MODE_BIT			0x28

#define CQSPI_REG_SDRAMLEVEL			0x2C
#define CQSPI_REG_SDRAMLEVEL_RD_LSB		0
#define CQSPI_REG_SDRAMLEVEL_WR_LSB		16
#define CQSPI_REG_SDRAMLEVEL_RD_MASK		0xFFFF
#define CQSPI_REG_SDRAMLEVEL_WR_MASK		0xFFFF

#define CQSPI_REG_WRCOMPLETION			0x38
#define CQSPI_REG_WRCOMPLETION_POLLCNT_MASK	0xFF0000
#define CQSPI_REG_WRCOMPLETION_POLLCNY_LSB	16

#define CQSPI_REG_IRQSTATUS			0x40
#define CQSPI_REG_IRQMASK			0x44
#define CQSPI_REG_ECO				0x48

#define CQSPI_REG_INDIRECTRD			0x60
#define CQSPI_REG_INDIRECTRD_START_MASK		BIT(0)
#define CQSPI_REG_INDIRECTRD_CANCEL_MASK	BIT(1)
#define CQSPI_REG_INDIRECTRD_DONE_MASK		BIT(5)

#define CQSPI_REG_INDIRECTRDWATERMARK		0x64
#define CQSPI_REG_INDIRECTRDSTARTADDR		0x68
#define CQSPI_REG_INDIRECTRDBYTES		0x6C

#define CQSPI_REG_CMDCTRL			0x90
#define CQSPI_REG_CMDCTRL_EXECUTE_MASK		BIT(0)
#define CQSPI_REG_CMDCTRL_INPROGRESS_MASK	BIT(1)
#define CQSPI_REG_CMDCTRL_WR_BYTES_LSB		12
#define CQSPI_REG_CMDCTRL_WR_EN_LSB		15
#define CQSPI_REG_CMDCTRL_ADD_BYTES_LSB		16
#define CQSPI_REG_CMDCTRL_ADDR_EN_LSB		19
#define CQSPI_REG_CMDCTRL_RD_BYTES_LSB		20
#define CQSPI_REG_CMDCTRL_RD_EN_LSB		23
#define CQSPI_REG_CMDCTRL_OPCODE_LSB		24
#define CQSPI_REG_CMDCTRL_WR_BYTES_MASK		0x7
#define CQSPI_REG_CMDCTRL_ADD_BYTES_MASK	0x3
#define CQSPI_REG_CMDCTRL_RD_BYTES_MASK		0x7
#define CQSPI_REG_CMDCTRL_DUMMY_BYTES_LSB	7
#define CQSPI_REG_CMDCTRL_DUMMY_BYTES_MASK      0x1F

#define CQSPI_REG_INDIRECTWR			0x70
#define CQSPI_REG_INDIRECTWR_START_MASK		BIT(0)
#define CQSPI_REG_INDIRECTWR_CANCEL_MASK	BIT(1)
#define CQSPI_REG_INDIRECTWR_DONE_MASK		BIT(5)

#define CQSPI_REG_INDIRECTWRWATERMARK		0x74
#define CQSPI_REG_INDIRECTWRSTARTADDR		0x78
#define CQSPI_REG_INDIRECTWRBYTES		0x7C

#define CQSPI_REG_INDTRIG_ADDRRANGE		0x80
#define CQSPI_REG_INDTRIG_ADDRRANGE_WIDTH	0x6

#define CQSPI_REG_CMDADDRESS			0x94
#define CQSPI_REG_CMDREADDATALOWER		0xA0
#define CQSPI_REG_CMDREADDATAUPPER		0xA4
#define CQSPI_REG_CMDWRITEDATALOWER		0xA8
#define CQSPI_REG_CMDWRITEDATAUPPER		0xAC

#define CQSPI_REG_PHY_CONFIG			0xB4
#define CQSPI_REG_PHY_CONFIG_RESYNC_FLD_MASK	0x80000000
#define CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK	0x40000000
#define CQSPI_REG_PHY_CONFIG_TX_DLL_DLY_LSB	16

#define CQSPI_REG_PHY_MASTER_CTRL		0xB8
#define CQSPI_REG_DLL_LOWER			0xBC
#define CQSPI_REG_DLL_LOWER_LPBK_LOCK_MASK	0x8000
#define CQSPI_REG_DLL_LOWER_DLL_LOCK_MASK	0x1

#define CQSPI_REG_DLL_OBSVBLE_UPPER		0xC0
#define CQSPI_REG_DLL_UPPER_RX_FLD_MASK		0x7F

#define CQSPI_REG_EXT_OP_LOWER			0xE0
#define CQSPI_REG_EXT_STIG_OP_MASK		0xFF
#define CQSPI_REG_EXT_READ_OP_MASK		0xFF000000
#define CQSPI_REG_EXT_READ_OP_SHIFT		24
#define CQSPI_REG_EXT_WRITE_OP_MASK		0xFF0000
#define CQSPI_REG_EXT_WRITE_OP_SHIFT		16
#define CQSPI_REG_DMA_SRC_ADDR			0x1000
#define CQSPI_REG_DMA_DST_ADDR			0x1800
#define CQSPI_REG_DMA_DST_SIZE			0x1804
#define CQSPI_REG_DMA_DST_STS			0x1808
#define CQSPI_REG_DMA_DST_CTRL			0x180C
#define CQSPI_REG_DMA_DST_CTRL_VAL		0xF43FFA00

#define CQSPI_REG_DMA_DTS_I_STS			0x1814
#define CQSPI_REG_DMA_DST_I_EN			0x1818
#define CQSPI_REG_DMA_DST_I_EN_DONE		BIT(1)

#define CQSPI_REG_DMA_DST_I_DIS			0x181C
#define CQSPI_REG_DMA_DST_I_DIS_DONE		BIT(1)
#define CQSPI_REG_DMA_DST_ALL_I_DIS_MASK	0xFE

#define CQSPI_REG_DMA_DST_I_MASK		0x1820
#define CQSPI_REG_DMA_DST_ADDR_MSB		0x1828

/* Interrupt status bits */
#define CQSPI_REG_IRQ_MODE_ERR			BIT(0)
#define CQSPI_REG_IRQ_UNDERFLOW			BIT(1)
#define CQSPI_REG_IRQ_IND_COMP			BIT(2)
#define CQSPI_REG_IRQ_IND_RD_REJECT		BIT(3)
#define CQSPI_REG_IRQ_WR_PROTECTED_ERR		BIT(4)
#define CQSPI_REG_IRQ_ILLEGAL_AHB_ERR		BIT(5)
#define CQSPI_REG_IRQ_WATERMARK			BIT(6)
#define CQSPI_REG_IRQ_IND_SRAM_FULL		BIT(12)

#define CQSPI_IRQ_MASK_RD		(CQSPI_REG_IRQ_WATERMARK	| \
					 CQSPI_REG_IRQ_IND_SRAM_FULL	| \
					 CQSPI_REG_IRQ_IND_COMP)

#define CQSPI_IRQ_MASK_WR		(CQSPI_REG_IRQ_IND_COMP		| \
					 CQSPI_REG_IRQ_WATERMARK	| \
					 CQSPI_REG_IRQ_UNDERFLOW)

#define CQSPI_IRQ_STATUS_MASK		0x1FFFF

#define CQSPI_EDGE_MODE_SDR		0
#define CQSPI_EDGE_MODE_DDR		1

#define CQSPI_DMA_MODE			0
#define CQSPI_LINEAR_MODE		1

#define READ_4B_OP			0x13
#define CQSPI_MIO_NODE_ID_12		0x14108027
#define RESET_OSPI			0xc10402e
#define CQSPI_RESET_TYPE_HWPIN		0
#define CQSPI_READ_ID			0x9F
#define CQSPI_READ_ID_LEN		6
#define TERA_MACRO			1000000000000ULL
#define SILICON_VER_MASK		0xFF
#define SILICON_VER_1			0x10
#define CQSPI_DLL_MODE_MASTER		0
#define CQSPI_DLL_MODE_BYPASS		1
#define TAP_GRAN_SEL_MIN_FREQ		120000000
#define CQSPI_TX_TAP_MASTER		0x1E
#define CQSPI_MAX_DLL_TAPS		127

#define CQSPI_CS_LOWER			0
#define CQSPI_CS_UPPER			1

static int cqspi_wait_for_bit(void __iomem *reg, const u32 mask, bool clr)
{
	u32 val;

	return readl_relaxed_poll_timeout(reg, val,
					  (((clr ? ~val : val) & mask) == mask),
					  10, CQSPI_TIMEOUT_MS * 1000);
}

static bool cqspi_is_idle(struct cqspi_st *cqspi)
{
	u32 reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);

	return reg & (1 << CQSPI_REG_CONFIG_IDLE_LSB);
}

static u32 cqspi_get_rd_sram_level(struct cqspi_st *cqspi)
{
	u32 reg = readl(cqspi->iobase + CQSPI_REG_SDRAMLEVEL);

	reg >>= CQSPI_REG_SDRAMLEVEL_RD_LSB;
	return reg & CQSPI_REG_SDRAMLEVEL_RD_MASK;
}

static unsigned int cqspi_calc_rdreg(struct cqspi_flash_pdata *f_pdata)
{
	u32 rdreg = 0;

	rdreg |= f_pdata->inst_width << CQSPI_REG_RD_INSTR_TYPE_INSTR_LSB;
	rdreg |= f_pdata->addr_width << CQSPI_REG_RD_INSTR_TYPE_ADDR_LSB;
	rdreg |= f_pdata->data_width << CQSPI_REG_RD_INSTR_TYPE_DATA_LSB;

	return rdreg;
}

static int cqspi_wait_idle(struct cqspi_st *cqspi)
{
	const unsigned int poll_idle_retry = 3;
	unsigned int count = 0;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(CQSPI_TIMEOUT_MS);
	while (1) {
		/*
		 * Read few times in succession to ensure the controller
		 * is indeed idle, that is, the bit does not transition
		 * low again.
		 */
		if (cqspi_is_idle(cqspi))
			count++;
		else
			count = 0;

		if (count >= poll_idle_retry)
			return 0;

		if (time_after(jiffies, timeout)) {
			/* Timeout, in busy mode. */
			dev_err(&cqspi->pdev->dev,
				"QSPI is still busy after %dms timeout.\n",
				CQSPI_TIMEOUT_MS);
			return -ETIMEDOUT;
		}

		cpu_relax();
	}
}

static int cqspi_exec_flash_cmd(struct cqspi_st *cqspi, unsigned int reg)
{
	void __iomem *reg_base = cqspi->iobase;
	int ret;

	/* Write the CMDCTRL without start execution. */
	writel(reg, reg_base + CQSPI_REG_CMDCTRL);
	/* Start execute */
	reg |= CQSPI_REG_CMDCTRL_EXECUTE_MASK;
	writel(reg, reg_base + CQSPI_REG_CMDCTRL);

	/* Polling for completion. */
	ret = cqspi_wait_for_bit(reg_base + CQSPI_REG_CMDCTRL,
				 CQSPI_REG_CMDCTRL_INPROGRESS_MASK, 1);
	if (ret) {
		dev_err(&cqspi->pdev->dev,
			"Flash command execution timed out.\n");
		return ret;
	}

	/* Polling QSPI idle status. */
	return cqspi_wait_idle(cqspi);
}

static void process_dma_irq(struct cqspi_st *cqspi)
{
	struct platform_device *pdev = cqspi->pdev;
	struct device *dev = &pdev->dev;
	unsigned int rem;
	unsigned int reg;
	unsigned int data;
	u8 addr_bytes;
	u8 opcode;
	u8 dummy_cycles;

	/* Disable DMA interrupt */
	writel(CQSPI_REG_DMA_DST_I_DIS_DONE,
	       cqspi->iobase + CQSPI_REG_DMA_DST_I_DIS);

	/* Clear indirect completion status */
	writel(CQSPI_REG_INDIRECTRD_DONE_MASK,
	       cqspi->iobase + CQSPI_REG_INDIRECTRD);
	dma_unmap_single(dev, cqspi->dma_addr, cqspi->bytes_to_dma,
			 DMA_FROM_DEVICE);
	rem = cqspi->bytes_to_rx - cqspi->bytes_to_dma;

	/* Read unaligned data in STIG */
	if (rem) {
		cqspi->rxbuf += cqspi->bytes_to_dma;
		writel(cqspi->addr + cqspi->bytes_to_dma,
		       cqspi->iobase + CQSPI_REG_CMDADDRESS);
		opcode = (u8)readl(cqspi->iobase + CQSPI_REG_RD_INSTR);
		addr_bytes = readl(cqspi->iobase + CQSPI_REG_SIZE) &
					CQSPI_REG_SIZE_ADDRESS_MASK;
		reg = opcode << CQSPI_REG_CMDCTRL_OPCODE_LSB;
		reg |= (0x1 << CQSPI_REG_CMDCTRL_RD_EN_LSB);
		reg |= (0x1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB);
		reg |= (addr_bytes & CQSPI_REG_CMDCTRL_ADD_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;
		dummy_cycles = (readl(cqspi->iobase + CQSPI_REG_RD_INSTR) >>
				CQSPI_REG_RD_INSTR_DUMMY_LSB) &
				CQSPI_REG_RD_INSTR_DUMMY_MASK;
		reg |= (dummy_cycles & CQSPI_REG_CMDCTRL_DUMMY_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_DUMMY_BYTES_LSB;
		cqspi->unalined_byte_cnt = false;
		if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
		    ((rem % 2) != 0)) {
			cqspi->unalined_byte_cnt = true;
		}

		/* 0 means 1 byte. */
		reg |= (((rem - 1 + cqspi->unalined_byte_cnt) &
			CQSPI_REG_CMDCTRL_RD_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_RD_BYTES_LSB);
		cqspi_exec_flash_cmd(cqspi, reg);
		data = readl(cqspi->iobase + CQSPI_REG_CMDREADDATALOWER);

		/* Put the read value into rx_buf */
		memcpy(cqspi->rxbuf, &data, rem);
	}
}

static irqreturn_t cqspi_irq_handler(int this_irq, void *dev)
{
	struct cqspi_st *cqspi = dev;
	unsigned int irq_status;
	unsigned int dma_status;

	/* Read interrupt status */
	irq_status = readl(cqspi->iobase + CQSPI_REG_IRQSTATUS);
	irq_status &= CQSPI_IRQ_MASK_RD | CQSPI_IRQ_MASK_WR;

	/* Clear interrupt */
	if (irq_status)
		writel(irq_status, cqspi->iobase + CQSPI_REG_IRQSTATUS);

	dma_status = readl(cqspi->iobase + CQSPI_REG_DMA_DTS_I_STS);
	dma_status &= CQSPI_REG_DMA_DST_I_EN_DONE;
	if (dma_status)
		writel(dma_status, cqspi->iobase + CQSPI_REG_DMA_DTS_I_STS);

	if (irq_status || dma_status)
		complete(&cqspi->transfer_complete);

	return IRQ_HANDLED;
}

static int cqspi_command_read(struct cqspi_flash_pdata *f_pdata,
			      const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *reg_base = cqspi->iobase;
	u8 *rxbuf = op->data.buf.in;
	u8 opcode = op->cmd.opcode;
	size_t n_rx = op->data.nbytes;
	unsigned int rdreg;
	unsigned int reg;
	size_t read_len;
	int status;
	u8 dummy_clk;
	u8 is_dual_op;

	if (!n_rx || n_rx > CQSPI_STIG_DATA_LEN_MAX || !rxbuf) {
		dev_err(&cqspi->pdev->dev,
			"Invalid input argument, len %zu rxbuf 0x%p\n",
			n_rx, rxbuf);
		return -EINVAL;
	}

	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		f_pdata->inst_width = CQSPI_INST_TYPE_OCTAL;
		if (op->addr.nbytes)
			f_pdata->addr_width = CQSPI_INST_TYPE_OCTAL;
		if (op->data.nbytes)
			f_pdata->data_width = CQSPI_INST_TYPE_OCTAL;
	}

	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR)
		n_rx = ((n_rx % 2) != 0) ? (n_rx + 1) : n_rx;

	reg = opcode << CQSPI_REG_CMDCTRL_OPCODE_LSB;
	if (op->addr.nbytes) {
		reg |= (0x1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB);
		reg |= ((op->addr.nbytes - 1) &
			CQSPI_REG_CMDCTRL_ADD_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;

		writel(op->addr.val, reg_base + CQSPI_REG_CMDADDRESS);
	}

	rdreg = cqspi_calc_rdreg(f_pdata);
	writel(rdreg, reg_base + CQSPI_REG_RD_INSTR);

	reg |= (0x1 << CQSPI_REG_CMDCTRL_RD_EN_LSB);

	/* 0 means 1 byte. */
	reg |= (((n_rx - 1) & CQSPI_REG_CMDCTRL_RD_BYTES_MASK)
		<< CQSPI_REG_CMDCTRL_RD_BYTES_LSB);

	dummy_clk = (op->dummy.nbytes * 8) / op->dummy.buswidth;
	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR && !dummy_clk)
		dummy_clk = 8;

	if (dummy_clk > CQSPI_DUMMY_CLKS_MAX)
		dummy_clk = CQSPI_DUMMY_CLKS_MAX;

	if (cqspi->extra_dummy)
		dummy_clk++;

	if (dummy_clk)
		reg |= ((dummy_clk & CQSPI_REG_CMDCTRL_DUMMY_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_DUMMY_BYTES_LSB);

	status = cqspi_exec_flash_cmd(cqspi, reg);
	if (status)
		return status;

	reg = readl(reg_base + CQSPI_REG_CMDREADDATALOWER);

	/* Put the read value into rx_buf */
	read_len = (n_rx > 4) ? 4 : n_rx;
	memcpy(rxbuf, &reg, read_len);
	rxbuf += read_len;

	if (n_rx > 4) {
		reg = readl(reg_base + CQSPI_REG_CMDREADDATAUPPER);

		read_len = n_rx - read_len;
		memcpy(rxbuf, &reg, read_len);
	}

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	is_dual_op = (reg & CQSPI_REG_CONFIG_DUAL_BYTE_OP) >>
			CQSPI_REG_CONFIG_DUAL_OP_LSB;

	if ((opcode == 0x70 && rxbuf[0] == 0x81) ||
	    (opcode == 0x5 && rxbuf[0] == 0 && is_dual_op) ||
	    (opcode != 0x5 && opcode != 0x70 && opcode != CQSPI_READ_ID))
		complete(&cqspi->request_complete);

	return 0;
}

static int cqspi_command_write(struct cqspi_flash_pdata *f_pdata,
			       const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *reg_base = cqspi->iobase;
	const u8 opcode = op->cmd.opcode;
	const u8 *txbuf = op->data.buf.out;
	size_t n_tx = op->data.nbytes;
	unsigned int reg;
	unsigned int data;
	size_t write_len;
	int ret;

	if (n_tx > CQSPI_STIG_DATA_LEN_MAX || (n_tx && !txbuf)) {
		dev_err(&cqspi->pdev->dev,
			"Invalid input argument, cmdlen %zu txbuf 0x%p\n",
			n_tx, txbuf);
		return -EINVAL;
	}

	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		f_pdata->inst_width = CQSPI_INST_TYPE_OCTAL;
		if (op->addr.nbytes)
			f_pdata->addr_width = CQSPI_INST_TYPE_OCTAL;
		if (op->data.nbytes)
			f_pdata->data_width = CQSPI_INST_TYPE_OCTAL;
	}

	reg = f_pdata->data_width << CQSPI_REG_WR_INSTR_TYPE_DATA_LSB;
	reg |= f_pdata->addr_width << CQSPI_REG_WR_INSTR_TYPE_ADDR_LSB;
	writel(reg, reg_base + CQSPI_REG_WR_INSTR);
	reg = cqspi_calc_rdreg(f_pdata);
	writel(reg, reg_base + CQSPI_REG_RD_INSTR);

	reg = opcode << CQSPI_REG_CMDCTRL_OPCODE_LSB;

	if (op->addr.nbytes) {
		reg |= (0x1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB);
		reg |= ((op->addr.nbytes - 1) &
			CQSPI_REG_CMDCTRL_ADD_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;

		writel(op->addr.val, reg_base + CQSPI_REG_CMDADDRESS);
	}

	if (n_tx) {
		reg |= (0x1 << CQSPI_REG_CMDCTRL_WR_EN_LSB);
		reg |= ((n_tx - 1) & CQSPI_REG_CMDCTRL_WR_BYTES_MASK)
			<< CQSPI_REG_CMDCTRL_WR_BYTES_LSB;
		data = 0;
		write_len = (n_tx > 4) ? 4 : n_tx;
		memcpy(&data, txbuf, write_len);
		txbuf += write_len;
		writel(data, reg_base + CQSPI_REG_CMDWRITEDATALOWER);

		if (n_tx > 4) {
			data = 0;
			write_len = n_tx - 4;
			memcpy(&data, txbuf, write_len);
			writel(data, reg_base + CQSPI_REG_CMDWRITEDATAUPPER);
		}
	}

	ret = cqspi_exec_flash_cmd(cqspi, reg);
	if (!ret && opcode != 0x6 && !(op->addr.nbytes && !op->data.nbytes))
		complete(&cqspi->request_complete);

	return ret;
}

static int cqspi_read_setup(struct cqspi_flash_pdata *f_pdata,
			    const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *reg_base = cqspi->iobase;
	unsigned int dummy_clk = 0;
	unsigned int reg;

	reg = op->cmd.opcode << CQSPI_REG_RD_INSTR_OPCODE_LSB;
	reg |= cqspi_calc_rdreg(f_pdata);

	/* Setup dummy clock cycles */
	dummy_clk = (op->dummy.nbytes * 8) / op->dummy.buswidth;
	if (dummy_clk > CQSPI_DUMMY_CLKS_MAX)
		dummy_clk = CQSPI_DUMMY_CLKS_MAX;

	if (cqspi->extra_dummy)
		dummy_clk++;

	if (dummy_clk)
		reg |= (dummy_clk & CQSPI_REG_RD_INSTR_DUMMY_MASK)
		       << CQSPI_REG_RD_INSTR_DUMMY_LSB;

	writel(reg, reg_base + CQSPI_REG_RD_INSTR);

	/* Set address width */
	reg = readl(reg_base + CQSPI_REG_SIZE);
	reg &= ~CQSPI_REG_SIZE_ADDRESS_MASK;
	reg |= (op->addr.nbytes - 1);
	writel(reg, reg_base + CQSPI_REG_SIZE);
	return 0;
}

static int cqspi_indirect_read_execute(struct cqspi_flash_pdata *f_pdata,
				       u8 *rxbuf, loff_t from_addr,
				       const size_t n_rx)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	struct device *dev = &cqspi->pdev->dev;
	void __iomem *reg_base = cqspi->iobase;
	void __iomem *ahb_base = cqspi->ahb_base;
	unsigned int remaining = n_rx;
	unsigned int mod_bytes = n_rx % 4;
	unsigned int bytes_to_read = 0;
	u8 *rxbuf_end = rxbuf + n_rx;
	u8 *rxbuf_start = rxbuf;
	int ret = 0;
	u8 extra_bytes = 0;
	u32 reg;
	u32 req_bytes;
	u32 threshold_val;

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg &= ~CQSPI_REG_CONFIG_DMA_MASK;
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	if (cqspi->access_mode_switch && cqspi->access_mode == CQSPI_DMA_MODE)
		cqspi->access_mode_switch(f_pdata);

	writel(from_addr, reg_base + CQSPI_REG_INDIRECTRDSTARTADDR);
	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
	    (from_addr % 2) != 0) {
		mod_bytes += 1;
		if (!cqspi->unalined_byte_cnt)
			extra_bytes = 2;
	}

	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR && (from_addr % 2) != 0)
		writel(from_addr - 1, reg_base + CQSPI_REG_INDIRECTRDSTARTADDR);

	req_bytes = remaining + cqspi->unalined_byte_cnt + extra_bytes;
	writel(req_bytes, reg_base + CQSPI_REG_INDIRECTRDBYTES);

	/* Clear all interrupts. */
	writel(CQSPI_IRQ_STATUS_MASK, reg_base + CQSPI_REG_IRQSTATUS);

	writel(CQSPI_IRQ_MASK_RD, reg_base + CQSPI_REG_IRQMASK);
	threshold_val = readl(reg_base + CQSPI_REG_INDIRECTRDWATERMARK);

	reinit_completion(&cqspi->transfer_complete);
	writel(CQSPI_REG_INDIRECTRD_START_MASK,
	       reg_base + CQSPI_REG_INDIRECTRD);

	while (remaining > 0) {
		if (!wait_for_completion_timeout(&cqspi->transfer_complete,
						 msecs_to_jiffies(CQSPI_READ_TIMEOUT_MS)))
			ret = -ETIMEDOUT;

		if (req_bytes > (threshold_val + cqspi->fifo_width))
			bytes_to_read = threshold_val + cqspi->fifo_width;
		else
			bytes_to_read = req_bytes;

		if (ret && bytes_to_read == 0) {
			dev_err(dev, "Indirect read timeout, no bytes\n");
			goto failrd;
		}

		while (bytes_to_read != 0) {
			unsigned int word_remain = round_down(remaining, 4);
			unsigned int bytes_read = 0;

			bytes_to_read = bytes_to_read > remaining ?
					remaining : bytes_to_read;
			bytes_to_read = round_down(bytes_to_read, 4);
			/* Read 4 byte word chunks then single bytes */
			if (bytes_to_read) {
				u8 offset = 0;

				if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
				    ((from_addr % 2) != 0) && rxbuf ==
				    rxbuf_start) {
					unsigned int temp = ioread32(ahb_base);

					temp >>= 8;
					memcpy(rxbuf, &temp, 3);
					bytes_to_read -= 3;
					offset = 3;
					bytes_read += 3;
				}
				if (bytes_to_read >= 4) {
					ioread32_rep(ahb_base, rxbuf + offset,
						     (bytes_to_read / 4));
					bytes_read += (bytes_to_read / 4) * 4;
				}
			} else if (!word_remain && mod_bytes) {
				unsigned int temp = ioread32(ahb_base);
				if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
				    ((from_addr % 2) != 0) && rxbuf ==
				    rxbuf_start)
					temp >>= 8;

				bytes_to_read = min(remaining, mod_bytes);
				bytes_read = min((unsigned int)
						    (rxbuf_end - rxbuf),
						     bytes_to_read);
				memcpy(rxbuf, &temp, bytes_read);
			}
			rxbuf += bytes_read;
			remaining -= bytes_read;
			req_bytes -= bytes_read;
			bytes_to_read = cqspi_get_rd_sram_level(cqspi);
			bytes_to_read *= cqspi->fifo_width;
		}

		if (remaining > 0)
			reinit_completion(&cqspi->transfer_complete);
	}

	/* Check indirect done status */
	ret = cqspi_wait_for_bit(reg_base + CQSPI_REG_INDIRECTRD,
				 CQSPI_REG_INDIRECTRD_DONE_MASK, 0);
	if (ret) {
		dev_err(dev, "Indirect read completion error (%i)\n", ret);
		goto failrd;
	}

	/* Disable interrupt */
	writel(0, reg_base + CQSPI_REG_IRQMASK);

	/* Clear indirect completion status */
	writel(CQSPI_REG_INDIRECTRD_DONE_MASK, reg_base + CQSPI_REG_INDIRECTRD);
	complete(&cqspi->request_complete);

	return 0;

failrd:
	/* Disable interrupt */
	writel(0, reg_base + CQSPI_REG_IRQMASK);

	/* Cancel the indirect read */
	writel(CQSPI_REG_INDIRECTWR_CANCEL_MASK,
	       reg_base + CQSPI_REG_INDIRECTRD);
	return ret;
}

static int cqspi_write_setup(struct cqspi_flash_pdata *f_pdata,
			     const struct spi_mem_op *op)
{
	unsigned int reg;
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *reg_base = cqspi->iobase;

	/* Set opcode. */
	reg = op->cmd.opcode << CQSPI_REG_WR_INSTR_OPCODE_LSB;
	reg |= f_pdata->data_width << CQSPI_REG_WR_INSTR_TYPE_DATA_LSB;
	reg |= f_pdata->addr_width << CQSPI_REG_WR_INSTR_TYPE_ADDR_LSB;
	writel(reg, reg_base + CQSPI_REG_WR_INSTR);
	reg = cqspi_calc_rdreg(f_pdata);
	writel(reg, reg_base + CQSPI_REG_RD_INSTR);

	reg = readl(reg_base + CQSPI_REG_SIZE);
	reg &= ~CQSPI_REG_SIZE_ADDRESS_MASK;
	reg |= (op->addr.nbytes - 1);
	writel(reg, reg_base + CQSPI_REG_SIZE);
	return 0;
}

static int cqspi_indirect_write_execute(struct cqspi_flash_pdata *f_pdata,
					loff_t to_addr, const u8 *txbuf,
					const size_t n_tx)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	struct device *dev = &cqspi->pdev->dev;
	void __iomem *reg_base = cqspi->iobase;
	unsigned int remaining = n_tx;
	unsigned int write_bytes;
	int ret;
	u32 reg;

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg &= ~CQSPI_REG_CONFIG_DMA_MASK;
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	if (cqspi->access_mode_switch && cqspi->access_mode == CQSPI_DMA_MODE)
		cqspi->access_mode_switch(f_pdata);

	writel(to_addr, reg_base + CQSPI_REG_INDIRECTWRSTARTADDR);
	writel(remaining + cqspi->unalined_byte_cnt,
	       reg_base + CQSPI_REG_INDIRECTWRBYTES);

	/* Clear all interrupts. */
	writel(CQSPI_IRQ_STATUS_MASK, reg_base + CQSPI_REG_IRQSTATUS);

	writel(CQSPI_IRQ_MASK_WR, reg_base + CQSPI_REG_IRQMASK);

	reinit_completion(&cqspi->transfer_complete);
	writel(CQSPI_REG_INDIRECTWR_START_MASK,
	       reg_base + CQSPI_REG_INDIRECTWR);
	/*
	 * As per 66AK2G02 TRM SPRUHY8F section 11.15.5.3 Indirect Access
	 * Controller programming sequence, couple of cycles of
	 * QSPI_REF_CLK delay is required for the above bit to
	 * be internally synchronized by the QSPI module. Provide 5
	 * cycles of delay.
	 */
	if (cqspi->wr_delay)
		ndelay(cqspi->wr_delay);

	while (remaining > 0) {
		size_t write_words, mod_bytes;

		write_bytes = remaining;
		write_words = write_bytes / 4;
		mod_bytes = write_bytes % 4;
		/* Write 4 bytes at a time then single bytes. */
		if (write_words) {
			iowrite32_rep(cqspi->ahb_base, txbuf, write_words);
			txbuf += (write_words * 4);
		}
		if (mod_bytes) {
			unsigned int temp = 0xFFFFFFFF;

			memcpy(&temp, txbuf, mod_bytes);
			iowrite32(temp, cqspi->ahb_base);
			txbuf += mod_bytes;
		}

		if (!wait_for_completion_timeout(&cqspi->transfer_complete,
						 msecs_to_jiffies(CQSPI_TIMEOUT_MS))) {
			dev_err(dev, "Indirect write timeout\n");
			ret = -ETIMEDOUT;
			goto failwr;
		}

		remaining -= write_bytes;

		if (remaining > 0)
			reinit_completion(&cqspi->transfer_complete);
	}

	/* Check indirect done status */
	ret = cqspi_wait_for_bit(reg_base + CQSPI_REG_INDIRECTWR,
				 CQSPI_REG_INDIRECTWR_DONE_MASK, 0);
	if (ret) {
		dev_err(dev, "Indirect write completion error (%i)\n", ret);
		goto failwr;
	}

	/* Disable interrupt. */
	writel(0, reg_base + CQSPI_REG_IRQMASK);

	/* Clear indirect completion status */
	writel(CQSPI_REG_INDIRECTWR_DONE_MASK, reg_base + CQSPI_REG_INDIRECTWR);

	cqspi_wait_idle(cqspi);

	return 0;

failwr:
	/* Disable interrupt. */
	writel(0, reg_base + CQSPI_REG_IRQMASK);

	/* Cancel the indirect write */
	writel(CQSPI_REG_INDIRECTWR_CANCEL_MASK,
	       reg_base + CQSPI_REG_INDIRECTWR);
	return ret;
}

static void cqspi_chipselect(struct cqspi_flash_pdata *f_pdata)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *reg_base = cqspi->iobase;
	unsigned int chip_select = f_pdata->cs;
	unsigned int reg;

	reg = readl(reg_base + CQSPI_REG_CONFIG);
	if (cqspi->is_decoded_cs) {
		reg |= CQSPI_REG_CONFIG_DECODE_MASK;
	} else {
		reg &= ~CQSPI_REG_CONFIG_DECODE_MASK;

		/* Convert CS if without decoder.
		 * CS0 to 4b'1110
		 * CS1 to 4b'1101
		 * CS2 to 4b'1011
		 * CS3 to 4b'0111
		 */
		chip_select = 0xF & ~(1 << chip_select);
	}

	reg &= ~(CQSPI_REG_CONFIG_CHIPSELECT_MASK
		 << CQSPI_REG_CONFIG_CHIPSELECT_LSB);
	reg |= (chip_select & CQSPI_REG_CONFIG_CHIPSELECT_MASK)
	    << CQSPI_REG_CONFIG_CHIPSELECT_LSB;
	writel(reg, reg_base + CQSPI_REG_CONFIG);
}

static unsigned int calculate_ticks_for_ns(const unsigned int ref_clk_hz,
					   const unsigned int ns_val)
{
	unsigned int ticks;

	ticks = ref_clk_hz / 1000;	/* kHz */
	ticks = DIV_ROUND_UP(ticks * ns_val, 1000000);

	return ticks;
}

static void cqspi_delay(struct cqspi_flash_pdata *f_pdata)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	void __iomem *iobase = cqspi->iobase;
	const unsigned int ref_clk_hz = cqspi->master_ref_clk_hz;
	unsigned int tshsl, tchsh, tslch, tsd2d;
	unsigned int reg;
	unsigned int tsclk;

	/* calculate the number of ref ticks for one sclk tick */
	tsclk = DIV_ROUND_UP(ref_clk_hz, cqspi->sclk);

	tshsl = calculate_ticks_for_ns(ref_clk_hz, f_pdata->tshsl_ns);
	/* this particular value must be at least one sclk */
	if (tshsl < tsclk)
		tshsl = tsclk;

	tchsh = calculate_ticks_for_ns(ref_clk_hz, f_pdata->tchsh_ns);
	tslch = calculate_ticks_for_ns(ref_clk_hz, f_pdata->tslch_ns);
	tsd2d = calculate_ticks_for_ns(ref_clk_hz, f_pdata->tsd2d_ns);

	reg = (tshsl & CQSPI_REG_DELAY_TSHSL_MASK)
	       << CQSPI_REG_DELAY_TSHSL_LSB;
	reg |= (tchsh & CQSPI_REG_DELAY_TCHSH_MASK)
		<< CQSPI_REG_DELAY_TCHSH_LSB;
	reg |= (tslch & CQSPI_REG_DELAY_TSLCH_MASK)
		<< CQSPI_REG_DELAY_TSLCH_LSB;
	reg |= (tsd2d & CQSPI_REG_DELAY_TSD2D_MASK)
		<< CQSPI_REG_DELAY_TSD2D_LSB;
	writel(reg, iobase + CQSPI_REG_DELAY);
}

static void cqspi_config_baudrate_div(struct cqspi_st *cqspi)
{
	const unsigned int ref_clk_hz = cqspi->master_ref_clk_hz;
	void __iomem *reg_base = cqspi->iobase;
	u32 reg, div;

	/* Recalculate the baudrate divisor based on QSPI specification. */
	div = DIV_ROUND_UP(ref_clk_hz, 2 * cqspi->sclk) - 1;

	reg = readl(reg_base + CQSPI_REG_CONFIG);
	reg &= ~(CQSPI_REG_CONFIG_BAUD_MASK << CQSPI_REG_CONFIG_BAUD_LSB);
	reg |= (div & CQSPI_REG_CONFIG_BAUD_MASK) << CQSPI_REG_CONFIG_BAUD_LSB;
	writel(reg, reg_base + CQSPI_REG_CONFIG);
}

static void cqspi_readdata_capture(struct cqspi_st *cqspi,
				   const bool bypass,
				   const unsigned int delay)
{
	void __iomem *reg_base = cqspi->iobase;
	unsigned int reg;

	reg = readl(reg_base + CQSPI_REG_READCAPTURE);

	if (bypass)
		reg |= (1 << CQSPI_REG_READCAPTURE_BYPASS_LSB);
	else
		reg &= ~(1 << CQSPI_REG_READCAPTURE_BYPASS_LSB);

	reg &= ~(CQSPI_REG_READCAPTURE_DELAY_MASK
		 << CQSPI_REG_READCAPTURE_DELAY_LSB);

	reg |= (delay & CQSPI_REG_READCAPTURE_DELAY_MASK)
		<< CQSPI_REG_READCAPTURE_DELAY_LSB;

	writel(reg, reg_base + CQSPI_REG_READCAPTURE);
}

static void cqspi_controller_enable(struct cqspi_st *cqspi, bool enable)
{
	void __iomem *reg_base = cqspi->iobase;
	unsigned int reg;

	reg = readl(reg_base + CQSPI_REG_CONFIG);

	if (enable)
		reg |= CQSPI_REG_CONFIG_ENABLE_MASK;
	else
		reg &= ~CQSPI_REG_CONFIG_ENABLE_MASK;

	writel(reg, reg_base + CQSPI_REG_CONFIG);
}

static void cqspi_configure(struct cqspi_flash_pdata *f_pdata,
			    unsigned long sclk)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	int switch_cs = (cqspi->current_cs != f_pdata->cs);
	int switch_ck = (cqspi->sclk != sclk);

	if (switch_cs || switch_ck)
		cqspi_controller_enable(cqspi, 0);

	/* Switch chip select. */
	if (switch_cs) {
		cqspi->current_cs = f_pdata->cs;
		cqspi_chipselect(f_pdata);
	}

	/* Setup baudrate divisor and delays */
	if (switch_ck) {
		cqspi->sclk = sclk;
		cqspi_config_baudrate_div(cqspi);
		cqspi_delay(f_pdata);
		cqspi_readdata_capture(cqspi, !cqspi->rclk_en,
				       f_pdata->read_delay);
	}

	if (switch_cs || switch_ck)
		cqspi_controller_enable(cqspi, 1);
}

static int cqspi_set_protocol(struct cqspi_flash_pdata *f_pdata,
			      const struct spi_mem_op *op)
{
	f_pdata->inst_width = CQSPI_INST_TYPE_SINGLE;
	f_pdata->addr_width = CQSPI_INST_TYPE_SINGLE;
	f_pdata->data_width = CQSPI_INST_TYPE_SINGLE;

	if (f_pdata->cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		f_pdata->inst_width = CQSPI_INST_TYPE_OCTAL;
		if (op->addr.nbytes)
			f_pdata->addr_width = CQSPI_INST_TYPE_OCTAL;
		if (op->data.nbytes)
			f_pdata->data_width = CQSPI_INST_TYPE_OCTAL;

		return 0;
	}

	switch (op->cmd.buswidth) {
	case 1:
		f_pdata->inst_width = CQSPI_INST_TYPE_SINGLE;
		break;
	case 2:
		f_pdata->inst_width = CQSPI_INST_TYPE_DUAL;
		break;
	case 4:
		f_pdata->inst_width = CQSPI_INST_TYPE_QUAD;
		break;
	case 8:
		f_pdata->inst_width = CQSPI_INST_TYPE_OCTAL;
		break;
	default:
		return -EINVAL;
	}

	if (op->addr.nbytes) {
		switch (op->addr.buswidth) {
		case 1:
			f_pdata->addr_width = CQSPI_INST_TYPE_SINGLE;
			break;
		case 2:
			f_pdata->addr_width = CQSPI_INST_TYPE_DUAL;
			break;
		case 4:
			f_pdata->addr_width = CQSPI_INST_TYPE_QUAD;
			break;
		case 8:
			f_pdata->addr_width = CQSPI_INST_TYPE_OCTAL;
			break;
		default:
			return -EINVAL;
		}
	}

	if (op->data.nbytes) {
		switch (op->data.buswidth) {
		case 1:
			f_pdata->data_width = CQSPI_INST_TYPE_SINGLE;
			break;
		case 2:
			f_pdata->data_width = CQSPI_INST_TYPE_DUAL;
			break;
		case 4:
			f_pdata->data_width = CQSPI_INST_TYPE_QUAD;
			break;
		case 8:
			f_pdata->data_width = CQSPI_INST_TYPE_OCTAL;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static ssize_t cqspi_write(struct cqspi_flash_pdata *f_pdata,
			   const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	loff_t to = op->addr.val;
	size_t len = op->data.nbytes;
	const u_char *buf = op->data.buf.out;
	int ret;

	ret = cqspi_set_protocol(f_pdata, op);
	if (ret)
		return ret;

	ret = cqspi_write_setup(f_pdata, op);
	if (ret)
		return ret;

	cqspi->unalined_byte_cnt = false;
	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
	    ((len % 2) != 0)) {
		cqspi->unalined_byte_cnt = true;
	}

	if (cqspi->use_direct_mode && ((to + len) <= cqspi->ahb_size)) {
		memcpy_toio(cqspi->ahb_base + to, buf, len);
		return cqspi_wait_idle(cqspi);
	}

	return cqspi_indirect_write_execute(f_pdata, to, buf, len);
}

static void cqspi_rx_dma_callback(void *param)
{
	struct cqspi_st *cqspi = param;

	complete(&cqspi->rx_dma_complete);
}

static int cqspi_direct_read_execute(struct cqspi_flash_pdata *f_pdata,
				     u_char *buf, loff_t from, size_t len)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	struct device *dev = &cqspi->pdev->dev;
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	dma_addr_t dma_src = (dma_addr_t)cqspi->mmap_phys_base + from;
	int ret = 0;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;
	dma_addr_t dma_dst;
	struct device *ddev;

	if (!cqspi->rx_chan || !virt_addr_valid(buf)) {
		memcpy_fromio(buf, cqspi->ahb_base + from, len);
		complete(&cqspi->request_complete);
		return 0;
	}

	ddev = cqspi->rx_chan->device->dev;
	dma_dst = dma_map_single(ddev, buf, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(ddev, dma_dst)) {
		dev_err(dev, "dma mapping failed\n");
		return -ENOMEM;
	}
	tx = dmaengine_prep_dma_memcpy(cqspi->rx_chan, dma_dst, dma_src,
				       len, flags);
	if (!tx) {
		dev_err(dev, "device_prep_dma_memcpy error\n");
		ret = -EIO;
		goto err_unmap;
	}

	tx->callback = cqspi_rx_dma_callback;
	tx->callback_param = cqspi;
	cookie = tx->tx_submit(tx);
	reinit_completion(&cqspi->rx_dma_complete);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(dev, "dma_submit_error %d\n", cookie);
		ret = -EIO;
		goto err_unmap;
	}

	dma_async_issue_pending(cqspi->rx_chan);
	if (!wait_for_completion_timeout(&cqspi->rx_dma_complete,
					 msecs_to_jiffies(len))) {
		dmaengine_terminate_sync(cqspi->rx_chan);
		dev_err(dev, "DMA wait_for_completion_timeout\n");
		ret = -ETIMEDOUT;
		goto err_unmap;
	}
	complete(&cqspi->request_complete);

err_unmap:
	dma_unmap_single(ddev, dma_dst, len, DMA_FROM_DEVICE);

	return ret;
}

static ssize_t cqspi_read(struct cqspi_flash_pdata *f_pdata,
			  const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	loff_t from = op->addr.val;
	size_t len = op->data.nbytes;
	u_char *buf = op->data.buf.in;
	u64 dma_align = (u64)(uintptr_t)buf;
	int ret;

	ret = cqspi_set_protocol(f_pdata, op);
	if (ret)
		return ret;

	ret = cqspi_read_setup(f_pdata, op);
	if (ret)
		return ret;

	cqspi->unalined_byte_cnt = false;
	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		if ((len % 2) != 0)
			cqspi->unalined_byte_cnt = true;
	}

	if (cqspi->use_direct_mode && ((from + len) <= cqspi->ahb_size))
		return cqspi_direct_read_execute(f_pdata, buf, from, len);

	if (cqspi->read_dma && virt_addr_valid(buf) &&
	    cqspi->indirect_read_dma && ((dma_align & 0x3) == 0) &&
	    !is_vmalloc_addr(buf))
		return cqspi->indirect_read_dma(f_pdata, buf, from, len);

	return cqspi_indirect_read_execute(f_pdata, buf, from, len);
}

static int cqspi_setdlldelay(struct spi_mem *mem)
{
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	struct platform_device *pdev = cqspi->pdev;
	struct cqspi_flash_pdata *f_pdata;
	int i;
	u8 j;
	int ret;
	u8 id[CQSPI_READ_ID_LEN];
	bool rxtapfound = false;
	u8 min_rxtap = 0;
	u8 max_rxtap = 0;
	u8 avg_rxtap;
	bool id_matched;
	u32 txtap = 0;
	u8 max_tap;
	s8 max_windowsize = -1;
	u8 windowsize;
	u8 dummy_incr;
	u8 dummy_flag = 0;
	u32 reg;
	u8 count;
	u64 tera_macro = TERA_MACRO;
	u8 max_index = 0, min_index = 0;
	struct spi_mem_op op =
			SPI_MEM_OP(SPI_MEM_OP_CMD(CQSPI_READ_ID, 8),
				   SPI_MEM_OP_NO_ADDR,
				   SPI_MEM_OP_DUMMY(8, 8),
				   SPI_MEM_OP_DATA_IN(CQSPI_READ_ID_LEN, id, 8));

	ret = cqspi_wait_idle(cqspi);
	if (ret)
		return ret;

	f_pdata = &cqspi->f_pdata[mem->spi->chip_select];
	max_tap = (do_div(tera_macro, cqspi->master_ref_clk_hz) / 160);
	if (cqspi->dll_mode == CQSPI_DLL_MODE_MASTER) {
		/* Drive DLL reset bit to low */
		writel(0, cqspi->iobase + CQSPI_REG_PHY_CONFIG);

		/* Set initial delay value */
		writel(0x4, cqspi->iobase + CQSPI_REG_PHY_MASTER_CTRL);

		/* Set DLL reset bit */
		writel(CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK,
		       cqspi->iobase + CQSPI_REG_PHY_CONFIG);

		/* Check for loopback lock */
		ret = cqspi_wait_for_bit(cqspi->iobase + CQSPI_REG_DLL_LOWER,
					 CQSPI_REG_DLL_LOWER_LPBK_LOCK_MASK, 0);
		if (ret) {
			dev_err(&pdev->dev,
				"Loopback lock bit error (%i)\n", ret);
			return ret;
		}

		/* Re-synchronize slave DLLs */
		writel(CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK,
		       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
		writel(CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK |
		       CQSPI_REG_PHY_CONFIG_RESYNC_FLD_MASK,
		       cqspi->iobase + CQSPI_REG_PHY_CONFIG);

		txtap = CQSPI_TX_TAP_MASTER <<
			CQSPI_REG_PHY_CONFIG_TX_DLL_DLY_LSB;
		max_tap = CQSPI_MAX_DLL_TAPS;
	}

	cqspi->extra_dummy = false;
	for (dummy_incr = 0; dummy_incr <= 1; dummy_incr++) {
		if (dummy_incr)
			cqspi->extra_dummy = true;
		for (i = 0; i <= max_tap; i++) {
			writel((txtap | i |
			       CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK),
			       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
			writel((CQSPI_REG_PHY_CONFIG_RESYNC_FLD_MASK | txtap |
			       i | CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK),
			       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
			if (cqspi->dll_mode == CQSPI_DLL_MODE_MASTER) {
				ret = cqspi_wait_for_bit(cqspi->iobase +
							 CQSPI_REG_DLL_LOWER,
					CQSPI_REG_DLL_LOWER_DLL_LOCK_MASK, 0);
				if (ret)
					return ret;
			}

			if ((mem->spi->master->flags & SPI_DUAL_BYTE_OP) &&
			    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
				reg = readl(cqspi->iobase + CQSPI_REG_EXT_OP_LOWER);
				reg &= ~CQSPI_REG_EXT_STIG_OP_MASK;
				reg |= (u8)~CQSPI_READ_ID;
				writel(reg, cqspi->iobase + CQSPI_REG_EXT_OP_LOWER);
				op.addr.nbytes = 4;
				op.addr.buswidth = 8;
				op.addr.val = 0;
				op.dummy.nbytes = 4;
			}

			count = 0;
			do {
				count += 1;
				ret = cqspi_set_protocol(f_pdata, &op);
				if (!ret)
					ret = cqspi_command_read(f_pdata, &op);

				if (ret < 0) {
					dev_err(&pdev->dev,
						"error %d reading JEDEC ID\n", ret);
					return ret;
				}

				id_matched = true;
				for (j = 0; j < CQSPI_READ_ID_LEN; j++) {
					if (mem->device_id[j] != id[j]) {
						id_matched = false;
						break;
					}
				}
			} while (id_matched && (count <= 10));

			if (id_matched && !rxtapfound) {
				if (cqspi->dll_mode == CQSPI_DLL_MODE_MASTER) {
					min_rxtap = readl(cqspi->iobase +
							  CQSPI_REG_DLL_OBSVBLE_UPPER) &
							  CQSPI_REG_DLL_UPPER_RX_FLD_MASK;
					max_rxtap = min_rxtap;
					max_index = i;
					min_index = i;
				} else {
					min_rxtap = i;
					max_rxtap = i;
				}
				rxtapfound = true;
			}

			if (id_matched && rxtapfound) {
				if (cqspi->dll_mode == CQSPI_DLL_MODE_MASTER) {
					max_rxtap = readl(cqspi->iobase +
							  CQSPI_REG_DLL_OBSVBLE_UPPER) &
							  CQSPI_REG_DLL_UPPER_RX_FLD_MASK;
					max_index = i;
				} else {
					max_rxtap = i;
				}
			}
			if ((!id_matched || i == max_tap) && rxtapfound) {
				windowsize = max_rxtap - min_rxtap + 1;
				if (windowsize > max_windowsize) {
					dummy_flag = dummy_incr;
					max_windowsize = windowsize;
					if (cqspi->dll_mode ==
					    CQSPI_DLL_MODE_MASTER)
						avg_rxtap = (max_index +
							     min_index);
					else
						avg_rxtap = (max_rxtap +
							     min_rxtap);
					avg_rxtap /= 2;
				}

				if (windowsize >= 3)
					i = max_tap;

				rxtapfound = false;
			}
		}
		if (!dummy_incr) {
			rxtapfound = false;
			min_rxtap = 0;
			max_rxtap = 0;
		}
	}
	if (!dummy_flag)
		cqspi->extra_dummy = false;

	if (max_windowsize < 3)
		return -EINVAL;

	writel((txtap | avg_rxtap | CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK),
	       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
	writel((CQSPI_REG_PHY_CONFIG_RESYNC_FLD_MASK | txtap | avg_rxtap |
	       CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK),
	       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
	if (cqspi->dll_mode == CQSPI_DLL_MODE_MASTER) {
		ret = cqspi_wait_for_bit(cqspi->iobase + CQSPI_REG_DLL_LOWER,
					 CQSPI_REG_DLL_LOWER_DLL_LOCK_MASK, 0);
		if (ret)
			return ret;
	}

	return 0;
}

static void cqspi_setup_ddrmode(struct cqspi_st *cqspi)
{
	u32 reg;

	cqspi_controller_enable(cqspi, 0);

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg |= (CQSPI_REG_CONFIG_PHY_ENABLE_MASK);
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	/* Program POLL_CNT */
	reg = readl(cqspi->iobase + CQSPI_REG_WRCOMPLETION);
	reg &= ~CQSPI_REG_WRCOMPLETION_POLLCNT_MASK;
	writel(reg, cqspi->iobase + CQSPI_REG_WRCOMPLETION);

	reg |= (0x3 << CQSPI_REG_WRCOMPLETION_POLLCNY_LSB);
	writel(reg, cqspi->iobase + CQSPI_REG_WRCOMPLETION);

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg |= CQSPI_REG_CONFIG_DTR_PROT_EN_MASK;
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	reg = readl(cqspi->iobase + CQSPI_REG_READCAPTURE);
	reg |= CQSPI_REG_READCAPTURE_DQS_ENABLE;
	writel(reg, cqspi->iobase + CQSPI_REG_READCAPTURE);

	cqspi->edge_mode = CQSPI_EDGE_MODE_DDR;

	cqspi_controller_enable(cqspi, 1);
}

static void cqspi_periodictuning(struct work_struct *work)
{
	struct delayed_work *d = to_delayed_work(work);
	struct spi_mem *mem = container_of(d, struct spi_mem, complete_work);
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	int ret;

	if (!cqspi->request_complete.done)
		wait_for_completion(&cqspi->request_complete);

	reinit_completion(&cqspi->tuning_complete);
	ret = cqspi_setdlldelay(mem);
	complete_all(&cqspi->tuning_complete);
	if (ret) {
		dev_err(&cqspi->pdev->dev,
			"Setting dll delay error (%i)\n", ret);
	} else {
		schedule_delayed_work(&mem->complete_work,
			msecs_to_jiffies(CQSPI_TUNING_PERIODICITY_MS));
	}
}

static int cqspi_setup_edgemode(struct spi_mem *mem)
{
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	int ret;
	u32 reg;

	cqspi_setup_ddrmode(cqspi);
	if (mem->spi->master->flags & SPI_DUAL_BYTE_OP) {
		reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
		reg |= CQSPI_REG_CONFIG_DUAL_BYTE_OP;
		writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);
	}

	ret = cqspi_setdlldelay(mem);
	if (ret)
		return ret;

	complete_all(&cqspi->tuning_complete);
	complete_all(&cqspi->request_complete);
	INIT_DELAYED_WORK(&mem->complete_work, cqspi_periodictuning);
	schedule_delayed_work(&mem->complete_work,
			      msecs_to_jiffies(CQSPI_TUNING_PERIODICITY_MS));

	return ret;
}

static int cqspi_mem_process(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	struct cqspi_flash_pdata *f_pdata;
	void __iomem *reg_base = cqspi->iobase;
	u32 reg;

	f_pdata = &cqspi->f_pdata[mem->spi->chip_select];
	if (mem->spi->master->flags & SPI_MASTER_U_PAGE)
		f_pdata->cs = CQSPI_CS_UPPER;
	else
		f_pdata->cs = CQSPI_CS_LOWER;

	cqspi_configure(f_pdata, mem->spi->max_speed_hz);

	reinit_completion(&cqspi->request_complete);

	if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR &&
	    !cqspi->tuning_complete.done) {
		if (!wait_for_completion_timeout(&cqspi->tuning_complete,
			msecs_to_jiffies(CQSPI_TUNING_TIMEOUT_MS))) {
			return -ETIMEDOUT;
		}
	}

	reg = readl(reg_base + CQSPI_REG_EXT_OP_LOWER);
	reg &= ~(CQSPI_REG_EXT_STIG_OP_MASK | CQSPI_REG_EXT_READ_OP_MASK |
			CQSPI_REG_EXT_WRITE_OP_MASK);
	if (op->data.dir == SPI_MEM_DATA_IN && op->data.buf.in) {
		if (!op->addr.nbytes) {
			if ((mem->spi->master->flags & SPI_DUAL_BYTE_OP) &&
			    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
				reg |= (u8)~op->cmd.opcode;
				writel(reg, reg_base + CQSPI_REG_EXT_OP_LOWER);
			}
			return cqspi_command_read(f_pdata, op);
		}

		if ((mem->spi->master->flags & SPI_DUAL_BYTE_OP) &&
		    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
			reg |= (u8)(~op->cmd.opcode) << CQSPI_REG_EXT_READ_OP_SHIFT;
			writel(reg, reg_base + CQSPI_REG_EXT_OP_LOWER);
		}

		return cqspi_read(f_pdata, op);
	}

	if (!op->addr.nbytes || !op->data.buf.out) {
		if ((mem->spi->master->flags & SPI_DUAL_BYTE_OP) &&
		    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
			reg |= (u8)~op->cmd.opcode;
			writel(reg, reg_base + CQSPI_REG_EXT_OP_LOWER);
		}

		return cqspi_command_write(f_pdata, op);
	}

	if ((mem->spi->master->flags & SPI_DUAL_BYTE_OP) &&
	    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		reg |= (u8)(~op->cmd.opcode) << CQSPI_REG_EXT_WRITE_OP_SHIFT;
		writel(reg, cqspi->iobase + CQSPI_REG_EXT_OP_LOWER);
	}

	return cqspi_write(f_pdata, op);
}

static int cqspi_exec_mem_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	int ret;

	ret = cqspi_mem_process(mem, op);
	if (ret) {
		complete(&cqspi->request_complete);
		dev_err(&mem->spi->dev, "operation failed with %d\n", ret);
	}

	if (!ret && op->cmd.tune_clk)
		ret = cqspi_setup_edgemode(mem);

	return ret;
}

static int cqspi_of_get_flash_pdata(struct platform_device *pdev,
				    struct cqspi_flash_pdata *f_pdata,
				    struct device_node *np)
{
	if (of_property_read_u32(np, "cdns,read-delay", &f_pdata->read_delay)) {
		dev_err(&pdev->dev, "couldn't determine read-delay\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,tshsl-ns", &f_pdata->tshsl_ns)) {
		dev_err(&pdev->dev, "couldn't determine tshsl-ns\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,tsd2d-ns", &f_pdata->tsd2d_ns)) {
		dev_err(&pdev->dev, "couldn't determine tsd2d-ns\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,tchsh-ns", &f_pdata->tchsh_ns)) {
		dev_err(&pdev->dev, "couldn't determine tchsh-ns\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,tslch-ns", &f_pdata->tslch_ns)) {
		dev_err(&pdev->dev, "couldn't determine tslch-ns\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "spi-max-frequency", &f_pdata->clk_rate)) {
		dev_err(&pdev->dev, "couldn't determine spi-max-frequency\n");
		return -ENXIO;
	}

	return 0;
}

static int cqspi_of_get_pdata(struct cqspi_st *cqspi)
{
	struct device *dev = &cqspi->pdev->dev;
	struct device_node *np = dev->of_node;

	cqspi->is_decoded_cs = of_property_read_bool(np, "cdns,is-decoded-cs");

	if (of_property_read_u32(np, "cdns,fifo-depth", &cqspi->fifo_depth)) {
		dev_err(dev, "couldn't determine fifo-depth\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,fifo-width", &cqspi->fifo_width)) {
		dev_err(dev, "couldn't determine fifo-width\n");
		return -ENXIO;
	}

	if (of_property_read_u32(np, "cdns,trigger-address",
				 &cqspi->trigger_address)) {
		dev_err(dev, "couldn't determine trigger-address\n");
		return -ENXIO;
	}

	cqspi->rclk_en = of_property_read_bool(np, "cdns,rclk-en");

	return 0;
}

static void cqspi_controller_init(struct cqspi_st *cqspi)
{
	u32 reg;

	cqspi_controller_enable(cqspi, 0);

	/* Configure the remap address register, no remap */
	writel(0, cqspi->iobase + CQSPI_REG_REMAP);

	/* Reset the Delay lines */
	writel(CQSPI_REG_PHY_CONFIG_RESET_FLD_MASK,
	       cqspi->iobase + CQSPI_REG_PHY_CONFIG);

	/* Disable all interrupts. */
	writel(0, cqspi->iobase + CQSPI_REG_IRQMASK);
	writel(CQSPI_REG_DMA_DST_ALL_I_DIS_MASK,
	       cqspi->iobase + CQSPI_REG_DMA_DST_I_DIS);

	/* Configure the SRAM split to 1:1 . */
	writel(cqspi->fifo_depth / 2, cqspi->iobase + CQSPI_REG_SRAMPARTITION);

	/* Load indirect trigger address. */
	writel(cqspi->trigger_address,
	       cqspi->iobase + CQSPI_REG_INDIRECTTRIGGER);

	/* Program read watermark -- 1/2 of the FIFO. */
	writel(cqspi->fifo_depth * cqspi->fifo_width / 2,
	       cqspi->iobase + CQSPI_REG_INDIRECTRDWATERMARK);
	/* Program write watermark -- 1/8 of the FIFO. */
	writel(cqspi->fifo_depth * cqspi->fifo_width / 8,
	       cqspi->iobase + CQSPI_REG_INDIRECTWRWATERMARK);

	/* Enable Direct Access Controller */
	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg &= ~CQSPI_REG_CONFIG_DTR_PROT_EN_MASK;
	reg &= ~CQSPI_REG_CONFIG_DUAL_BYTE_OP;
	reg &= ~CQSPI_REG_CONFIG_PHY_ENABLE_MASK;
	if (cqspi->read_dma) {
		reg &= ~CQSPI_REG_CONFIG_ENB_DIR_ACC_CTRL;
		reg |= CQSPI_REG_CONFIG_DMA_MASK;
	} else {
		/* Enable Direct Access Controller */
		reg |= CQSPI_REG_CONFIG_ENB_DIR_ACC_CTRL;
	}
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	cqspi_controller_enable(cqspi, 1);
}

static int cqspi_versal_mode_switch(struct cqspi_flash_pdata *f_pdata)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	u32 phy_reg, rd_instr, addr_width;

	if (cqspi->access_mode == CQSPI_DMA_MODE) {
		cqspi_wait_idle(cqspi);
		zynqmp_pm_ospi_mux_select(cqspi->pm_dev_id,
					  PM_OSPI_MUX_SEL_LINEAR);
		cqspi->access_mode = CQSPI_LINEAR_MODE;
	} else if (cqspi->access_mode == CQSPI_LINEAR_MODE) {
		cqspi_wait_idle(cqspi);

		/* Issue controller reset */
		if (cqspi->dll_mode != CQSPI_DLL_MODE_MASTER) {
			phy_reg = readl(cqspi->iobase + CQSPI_REG_PHY_CONFIG);
			rd_instr = readl(cqspi->iobase + CQSPI_REG_RD_INSTR);
			addr_width = readl(cqspi->iobase + CQSPI_REG_SIZE);
			zynqmp_pm_reset_assert(RESET_OSPI, PM_RESET_ACTION_ASSERT);
		}

		zynqmp_pm_ospi_mux_select(cqspi->pm_dev_id,
					  PM_OSPI_MUX_SEL_DMA);
		cqspi->access_mode = CQSPI_DMA_MODE;
		if (cqspi->dll_mode != CQSPI_DLL_MODE_MASTER) {
			zynqmp_pm_reset_assert(RESET_OSPI, PM_RESET_ACTION_RELEASE);
			cqspi_controller_init(cqspi);
			cqspi->current_cs = -1;
			cqspi->sclk = 0;
			if (cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
				cqspi_setup_ddrmode(cqspi);
				writel(CQSPI_REG_PHY_CONFIG_RESYNC_FLD_MASK | phy_reg,
				       cqspi->iobase + CQSPI_REG_PHY_CONFIG);
			}

			writel(rd_instr, cqspi->iobase + CQSPI_REG_RD_INSTR);
			writel(addr_width, cqspi->iobase + CQSPI_REG_SIZE);
		}
	} else {
		return -EINVAL;
	}

	writel(CQSPI_REG_INDTRIG_ADDRRANGE_WIDTH,
	       cqspi->iobase + CQSPI_REG_INDTRIG_ADDRRANGE);

	return 0;
}

static int cqspi_versal_flash_reset(struct cqspi_st *cqspi, u8 reset_type)
{
	struct platform_device *pdev = cqspi->pdev;
	int ret;
	int gpio;
	enum of_gpio_flags flags;

	if (reset_type == CQSPI_RESET_TYPE_HWPIN) {
		gpio = of_get_named_gpio_flags(pdev->dev.of_node,
					       "reset-gpios", 0, &flags);
		if (!gpio_is_valid(gpio))
			return -EIO;
		ret = devm_gpio_request_one(&pdev->dev, gpio, flags,
					    "flash-reset");
		if (ret) {
			dev_err(&pdev->dev,
				"failed to get reset-gpios: %d\n", ret);
			return -EIO;
		}

		/* Request for PIN */
		zynqmp_pm_pinctrl_request(CQSPI_MIO_NODE_ID_12);

		/* Enable hysteresis in cmos receiver */
		zynqmp_pm_pinctrl_set_config(CQSPI_MIO_NODE_ID_12,
					     PM_PINCTRL_CONFIG_SCHMITT_CMOS,
					     PM_PINCTRL_INPUT_TYPE_SCHMITT);

		/* Set the direction as output and enable the output */
		gpio_direction_output(gpio, 1);

		/* Disable Tri-state */
		zynqmp_pm_pinctrl_set_config(CQSPI_MIO_NODE_ID_12,
					     PM_PINCTRL_CONFIG_TRI_STATE,
					     PM_PINCTRL_TRI_STATE_DISABLE);
		udelay(1);

		/* Set value 0 to pin */
		gpio_set_value(gpio, 0);
		udelay(10);

		/* Set value 1 to pin */
		gpio_set_value(gpio, 1);
		udelay(35);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int cqspi_versal_indirect_read_dma(struct cqspi_flash_pdata *f_pdata,
					  u_char *rxbuf, loff_t from_addr,
					  size_t n_rx)
{
	struct cqspi_st *cqspi = f_pdata->cqspi;
	struct device *dev = &cqspi->pdev->dev;
	void __iomem *reg_base = cqspi->iobase;
	unsigned int rx_rem;
	int ret = 0;
	u32 reg;

	rx_rem = n_rx % 4;
	cqspi->bytes_to_rx = n_rx;
	cqspi->bytes_to_dma = (n_rx - rx_rem);
	cqspi->addr = from_addr;
	cqspi->rxbuf = rxbuf;

	if (((from_addr % 2) != 0) &&
	    cqspi->edge_mode == CQSPI_EDGE_MODE_DDR) {
		u8 addr_bytes, opcode, dummy_cycles;
		unsigned int data;

		writel(cqspi->addr - 1, cqspi->iobase + CQSPI_REG_CMDADDRESS);
		opcode = (u8)readl(cqspi->iobase + CQSPI_REG_RD_INSTR);
		addr_bytes = readl(cqspi->iobase + CQSPI_REG_SIZE) &
					CQSPI_REG_SIZE_ADDRESS_MASK;
		reg = opcode << CQSPI_REG_CMDCTRL_OPCODE_LSB;
		reg |= (0x1 << CQSPI_REG_CMDCTRL_RD_EN_LSB);
		reg |= (0x1 << CQSPI_REG_CMDCTRL_ADDR_EN_LSB);
		reg |= (addr_bytes & CQSPI_REG_CMDCTRL_ADD_BYTES_MASK) <<
				CQSPI_REG_CMDCTRL_ADD_BYTES_LSB;
		dummy_cycles = (readl(cqspi->iobase + CQSPI_REG_RD_INSTR) >>
				CQSPI_REG_RD_INSTR_DUMMY_LSB) &
				CQSPI_REG_RD_INSTR_DUMMY_MASK;
		reg |= (dummy_cycles & CQSPI_REG_CMDCTRL_DUMMY_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_DUMMY_BYTES_LSB;
		reg |= ((0x1 & CQSPI_REG_CMDCTRL_RD_BYTES_MASK) <<
			CQSPI_REG_CMDCTRL_RD_BYTES_LSB);
		cqspi_exec_flash_cmd(cqspi, reg);
		data = readl(cqspi->iobase + CQSPI_REG_CMDREADDATALOWER);
		data = data >> 8;
		memcpy(cqspi->rxbuf, &data, 1);
		cqspi->bytes_to_rx -= 1;
		cqspi->addr += 1;
		cqspi->rxbuf += 1;
		rx_rem = cqspi->bytes_to_rx % 4;
		cqspi->bytes_to_dma = (cqspi->bytes_to_rx - rx_rem);
	}

	if (cqspi->bytes_to_rx < 4) {
		process_dma_irq(cqspi);
		complete(&cqspi->request_complete);
		return 0;
	}

	reg = readl(cqspi->iobase + CQSPI_REG_CONFIG);
	reg |= CQSPI_REG_CONFIG_DMA_MASK;
	writel(reg, cqspi->iobase + CQSPI_REG_CONFIG);

	if (cqspi->access_mode_switch &&
	    cqspi->access_mode == CQSPI_LINEAR_MODE)
		cqspi->access_mode_switch(f_pdata);

	writel(cqspi->addr, reg_base + CQSPI_REG_INDIRECTRDSTARTADDR);
	writel(cqspi->bytes_to_dma, reg_base + CQSPI_REG_INDIRECTRDBYTES);
	writel(CQSPI_REG_INDTRIG_ADDRRANGE_WIDTH,
	       reg_base + CQSPI_REG_INDTRIG_ADDRRANGE);

	/* Clear all interrupts. */
	writel(CQSPI_IRQ_STATUS_MASK, reg_base + CQSPI_REG_IRQSTATUS);

	/* Enable DMA done interrupt */
	writel(CQSPI_REG_DMA_DST_I_EN_DONE,
	       reg_base + CQSPI_REG_DMA_DST_I_EN);

	/* Default DMA periph configuration */
	writel(CQSPI_REG_DMA_VAL, reg_base + CQSPI_REG_DMA);

	cqspi->dma_addr = dma_map_single(dev, cqspi->rxbuf, cqspi->bytes_to_dma,
					 DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, cqspi->dma_addr)) {
		dev_err(dev, "ERR:rxdma:memory not mapped\n");
		goto failrd;
	}
	/* Configure DMA Dst address */
	writel(lower_32_bits(cqspi->dma_addr),
	       reg_base + CQSPI_REG_DMA_DST_ADDR);
	writel(upper_32_bits(cqspi->dma_addr),
	       reg_base + CQSPI_REG_DMA_DST_ADDR_MSB);

	/* Configure DMA Src read address */
	writel(cqspi->trigger_address, reg_base + CQSPI_REG_DMA_SRC_ADDR);

	/* Set DMA destination size */
	writel(cqspi->bytes_to_dma, reg_base + CQSPI_REG_DMA_DST_SIZE);

	/* Set DMA destination control */
	writel(CQSPI_REG_DMA_DST_CTRL_VAL, reg_base + CQSPI_REG_DMA_DST_CTRL);

	writel(CQSPI_REG_INDIRECTRD_START_MASK,
	       reg_base + CQSPI_REG_INDIRECTRD);

	reinit_completion(&cqspi->transfer_complete);

	if (!wait_for_completion_timeout(&cqspi->transfer_complete,
			msecs_to_jiffies(CQSPI_READ_TIMEOUT_MS))) {
		ret = -ETIMEDOUT;
		goto failrd;
	}

	/* Check indirect done status */
	ret = cqspi_wait_for_bit(reg_base + CQSPI_REG_INDIRECTRD,
				 CQSPI_REG_INDIRECTRD_DONE_MASK, 0);
	if (ret) {
		dev_err(dev,
			"Indirect read completion error (%i)\n", ret);
		goto failrd;
	}

	process_dma_irq(cqspi);
	complete(&cqspi->request_complete);

	return 0;

failrd:
	/* Disable DMA interrupt */
	writel(CQSPI_REG_DMA_DST_I_DIS_DONE,
	       reg_base + CQSPI_REG_DMA_DST_I_DIS);

	dma_unmap_single(dev, cqspi->dma_addr, cqspi->bytes_to_dma,
			 DMA_DEV_TO_MEM);

	/* Cancel the indirect read */
	writel(CQSPI_REG_INDIRECTWR_CANCEL_MASK,
	       reg_base + CQSPI_REG_INDIRECTRD);

	return ret;
}

static int cqspi_request_mmap_dma(struct cqspi_st *cqspi)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	cqspi->rx_chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(cqspi->rx_chan)) {
		int ret = PTR_ERR(cqspi->rx_chan);
		cqspi->rx_chan = NULL;
		return dev_err_probe(&cqspi->pdev->dev, ret, "No Rx DMA available\n");
	}
	init_completion(&cqspi->rx_dma_complete);

	return 0;
}

static const char *cqspi_get_name(struct spi_mem *mem)
{
	struct cqspi_st *cqspi = spi_master_get_devdata(mem->spi->master);
	struct device *dev = &cqspi->pdev->dev;

	return devm_kasprintf(dev, GFP_KERNEL, "%s.%d", dev_name(dev), mem->spi->chip_select);
}

static const struct spi_controller_mem_ops cqspi_mem_ops = {
	.exec_op = cqspi_exec_mem_op,
	.get_name = cqspi_get_name,
};

static int cqspi_setup_flash(struct cqspi_st *cqspi)
{
	struct platform_device *pdev = cqspi->pdev;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct cqspi_flash_pdata *f_pdata;
	unsigned int cs;
	int ret;

	/* Get flash device data */
	for_each_available_child_of_node(dev->of_node, np) {
		ret = of_property_read_u32(np, "reg", &cs);
		if (ret) {
			dev_err(dev, "Couldn't determine chip select.\n");
			return ret;
		}

		if (cs >= CQSPI_MAX_CHIPSELECT) {
			dev_err(dev, "Chip select %d out of range.\n", cs);
			return -EINVAL;
		}

		f_pdata = &cqspi->f_pdata[cs];
		f_pdata->cqspi = cqspi;
		f_pdata->cs = cs;

		ret = cqspi_of_get_flash_pdata(pdev, f_pdata, np);
		if (ret)
			return ret;
	}

	return 0;
}

static int cqspi_probe(struct platform_device *pdev)
{
	const struct cqspi_driver_platdata *ddata;
	struct reset_control *rstc, *rstc_ocp;
	struct device *dev = &pdev->dev;
	struct spi_master *master;
	struct resource *res_ahb;
	struct cqspi_st *cqspi;
	struct resource *res;
	int ret;
	int irq;
	u32 idcode;
	u32 version;
	u32 id[2];

	master = spi_alloc_master(&pdev->dev, sizeof(*cqspi));
	if (!master) {
		dev_err(&pdev->dev, "spi_alloc_master failed\n");
		return -ENOMEM;
	}
	master->mode_bits = SPI_RX_QUAD | SPI_RX_DUAL;
	master->mem_ops = &cqspi_mem_ops;
	master->dev.of_node = pdev->dev.of_node;

	cqspi = spi_master_get_devdata(master);

	cqspi->pdev = pdev;

	/* Obtain configuration from OF. */
	ret = cqspi_of_get_pdata(cqspi);
	if (ret) {
		dev_err(dev, "Cannot get mandatory OF data.\n");
		ret = -ENODEV;
		goto probe_master_put;
	}

	/* Obtain QSPI clock. */
	cqspi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(cqspi->clk)) {
		dev_err(dev, "Cannot claim QSPI clock.\n");
		ret = PTR_ERR(cqspi->clk);
		goto probe_master_put;
	}

	/* Obtain and remap controller address. */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cqspi->iobase = devm_ioremap_resource(dev, res);
	if (IS_ERR(cqspi->iobase)) {
		dev_err(dev, "Cannot remap controller address.\n");
		ret = PTR_ERR(cqspi->iobase);
		goto probe_master_put;
	}

	/* Obtain and remap AHB address. */
	res_ahb = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	cqspi->ahb_base = devm_ioremap_resource(dev, res_ahb);
	if (IS_ERR(cqspi->ahb_base)) {
		dev_err(dev, "Cannot remap AHB address.\n");
		ret = PTR_ERR(cqspi->ahb_base);
		goto probe_master_put;
	}
	cqspi->mmap_phys_base = (dma_addr_t)res_ahb->start;
	cqspi->ahb_size = resource_size(res_ahb);

	init_completion(&cqspi->transfer_complete);
	init_completion(&cqspi->tuning_complete);
	init_completion(&cqspi->request_complete);

	/* Obtain IRQ line. */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto probe_master_put;
	}

	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		goto probe_master_put;
	}

	ret = clk_prepare_enable(cqspi->clk);
	if (ret) {
		dev_err(dev, "Cannot enable QSPI clock.\n");
		goto probe_clk_failed;
	}

	/* Obtain QSPI reset control */
	rstc = devm_reset_control_get_optional_exclusive(dev, "qspi");
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		dev_err(dev, "Cannot get QSPI reset.\n");
		goto probe_reset_failed;
	}

	rstc_ocp = devm_reset_control_get_optional_exclusive(dev, "qspi-ocp");
	if (IS_ERR(rstc_ocp)) {
		ret = PTR_ERR(rstc_ocp);
		dev_err(dev, "Cannot get QSPI OCP reset.\n");
		goto probe_reset_failed;
	}

	reset_control_assert(rstc);
	reset_control_deassert(rstc);

	reset_control_assert(rstc_ocp);
	reset_control_deassert(rstc_ocp);

	cqspi->master_ref_clk_hz = clk_get_rate(cqspi->clk);
	cqspi->pm_dev_id = 0;
	ddata  = of_device_get_match_data(dev);
	if (ddata) {
		if (ddata->quirks & CQSPI_NEEDS_WR_DELAY)
			cqspi->wr_delay = 5 * DIV_ROUND_UP(NSEC_PER_SEC,
						cqspi->master_ref_clk_hz);
		if (ddata->hwcaps_mask & CQSPI_SUPPORTS_OCTAL)
			master->mode_bits |= SPI_RX_OCTAL;
		if (!(ddata->quirks & CQSPI_DISABLE_DAC_MODE))
			cqspi->use_direct_mode = true;

		if (ddata && (ddata->quirks & CQSPI_HAS_DMA)) {
			dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
			cqspi->read_dma = true;
		}

		if (of_device_is_compatible(pdev->dev.of_node,
					    "xlnx,versal-ospi-1.0") &&
					    cqspi->read_dma) {
			master->mode_bits |= SPI_TX_OCTAL;
			cqspi->indirect_read_dma =
					cqspi_versal_indirect_read_dma;
			cqspi->flash_reset = cqspi_versal_flash_reset;
			cqspi->access_mode_switch = cqspi_versal_mode_switch;
			cqspi->dll_mode = CQSPI_DLL_MODE_BYPASS;

			ret = zynqmp_pm_get_chipid(&idcode, &version);
			if (ret < 0) {
				dev_err(dev, "Cannot get chipid is %d\n", ret);
				goto probe_clk_failed;
			}
			if ((version & SILICON_VER_MASK) != SILICON_VER_1) {
				cqspi->dll_mode = CQSPI_DLL_MODE_MASTER;
				if (cqspi->master_ref_clk_hz >= TAP_GRAN_SEL_MIN_FREQ)
					writel(0x1, cqspi->iobase + CQSPI_REG_ECO);
			}

			ret = of_property_read_u32_array(pdev->dev.of_node,
							 "power-domains", id,
							 ARRAY_SIZE(id));
			if (ret < 0) {
				dev_err(&pdev->dev,
					"Failed to read pm device id information\n");
				goto probe_clk_failed;
			}
			cqspi->pm_dev_id = id[1];
		}
	}

	ret = devm_request_irq(dev, irq, cqspi_irq_handler, 0,
			       pdev->name, cqspi);
	if (ret) {
		dev_err(dev, "Cannot request IRQ.\n");
		goto probe_reset_failed;
	}

	cqspi_wait_idle(cqspi);
	cqspi_controller_init(cqspi);
	cqspi->current_cs = -1;
	cqspi->sclk = 0;
	cqspi->extra_dummy = false;
	cqspi->edge_mode = CQSPI_EDGE_MODE_SDR;
	cqspi->access_mode = CQSPI_DMA_MODE;
	cqspi->unalined_byte_cnt = false;

	ret = cqspi_setup_flash(cqspi);
	if (ret) {
		dev_err(dev, "failed to setup flash parameters %d\n", ret);
		goto probe_setup_failed;
	}

	if (ddata->quirks & CQSPI_SUPPORT_RESET) {
		ret = cqspi->flash_reset(cqspi, CQSPI_RESET_TYPE_HWPIN);
		if (ret)
			goto probe_setup_failed;
	}

	if (cqspi->use_direct_mode) {
		ret = cqspi_request_mmap_dma(cqspi);
		if (ret == -EPROBE_DEFER)
			goto probe_setup_failed;
	}

	ret = devm_spi_register_master(dev, master);
	if (ret) {
		dev_err(&pdev->dev, "failed to register SPI ctlr %d\n", ret);
		goto probe_setup_failed;
	}

	return 0;
probe_setup_failed:
	cqspi_controller_enable(cqspi, 0);
probe_reset_failed:
	clk_disable_unprepare(cqspi->clk);
probe_clk_failed:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
probe_master_put:
	spi_master_put(master);
	return ret;
}

static int cqspi_remove(struct platform_device *pdev)
{
	struct cqspi_st *cqspi = platform_get_drvdata(pdev);

	cqspi_controller_enable(cqspi, 0);

	if (cqspi->rx_chan)
		dma_release_channel(cqspi->rx_chan);

	clk_disable_unprepare(cqspi->clk);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cqspi_suspend(struct device *dev)
{
	struct cqspi_st *cqspi = dev_get_drvdata(dev);

	cqspi_controller_enable(cqspi, 0);
	return 0;
}

static int cqspi_resume(struct device *dev)
{
	struct cqspi_st *cqspi = dev_get_drvdata(dev);

	cqspi_controller_enable(cqspi, 1);
	return 0;
}

static const struct dev_pm_ops cqspi__dev_pm_ops = {
	.suspend = cqspi_suspend,
	.resume = cqspi_resume,
};

#define CQSPI_DEV_PM_OPS	(&cqspi__dev_pm_ops)
#else
#define CQSPI_DEV_PM_OPS	NULL
#endif

static const struct cqspi_driver_platdata cdns_qspi = {
	.quirks = CQSPI_DISABLE_DAC_MODE,
};

static const struct cqspi_driver_platdata k2g_qspi = {
	.quirks = CQSPI_NEEDS_WR_DELAY,
};

static const struct cqspi_driver_platdata am654_ospi = {
	.hwcaps_mask = CQSPI_SUPPORTS_OCTAL,
	.quirks = CQSPI_NEEDS_WR_DELAY,
};

static const struct cqspi_driver_platdata versal_ospi = {
	.hwcaps_mask = CQSPI_SUPPORTS_OCTAL,
	.quirks = CQSPI_HAS_DMA | CQSPI_DISABLE_DAC_MODE | CQSPI_SUPPORT_RESET,
};

static const struct of_device_id cqspi_dt_ids[] = {
	{
		.compatible = "cdns,qspi-nor",
		.data = &cdns_qspi,
	},
	{
		.compatible = "ti,k2g-qspi",
		.data = &k2g_qspi,
	},
	{
		.compatible = "ti,am654-ospi",
		.data = &am654_ospi,
	},
	{
		.compatible = "xlnx,versal-ospi-1.0",
		.data = (void *)&versal_ospi,
	},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, cqspi_dt_ids);

static struct platform_driver cqspi_platform_driver = {
	.probe = cqspi_probe,
	.remove = cqspi_remove,
	.driver = {
		.name = CQSPI_NAME,
		.pm = CQSPI_DEV_PM_OPS,
		.of_match_table = cqspi_dt_ids,
	},
};

module_platform_driver(cqspi_platform_driver);

MODULE_DESCRIPTION("Cadence QSPI Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" CQSPI_NAME);
MODULE_AUTHOR("Ley Foon Tan <lftan@altera.com>");
MODULE_AUTHOR("Graham Moore <grmoore@opensource.altera.com>");
MODULE_AUTHOR("Vadivel Murugan R <vadivel.muruganx.ramuthevar@intel.com>");
MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
