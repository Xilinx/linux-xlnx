#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/of_mtd.h>
#include <linux/platform_device.h>

#define DRIVER_NAME			"arasan_nfc"
#define EVNT_TIMEOUT			1000
#define STATUS_TIMEOUT			2000

#define PKT_OFST			0x00
#define MEM_ADDR1_OFST			0x04
#define MEM_ADDR2_OFST			0x08
#define CMD_OFST			0x0C
#define PROG_OFST			0x10
#define INTR_STS_EN_OFST		0x14
#define INTR_SIG_EN_OFST		0x18
#define INTR_STS_OFST			0x1C
#define READY_STS_OFST			0x20
#define DMA_ADDR1_OFST			0x24
#define FLASH_STS_OFST			0x28
#define DATA_PORT_OFST			0x30
#define ECC_OFST			0x34
#define ECC_ERR_CNT_OFST		0x38
#define ECC_SPR_CMD_OFST		0x3C
#define ECC_ERR_CNT_1BIT_OFST		0x40
#define ECC_ERR_CNT_2BIT_OFST		0x44
#define DMA_ADDR0_OFST			0x50

#define PKT_CNT_SHIFT			12

#define ECC_ENABLE			BIT(31)
#define DMA_EN_MASK			GENMASK(27, 26)
#define DMA_ENABLE			0x2
#define DMA_EN_SHIFT			26
#define PAGE_SIZE_MASK			GENMASK(25, 23)
#define PAGE_SIZE_SHIFT			23
#define PAGE_SIZE_512			0
#define PAGE_SIZE_1K			5
#define PAGE_SIZE_2K			1
#define PAGE_SIZE_4K			2
#define PAGE_SIZE_8K			3
#define PAGE_SIZE_16K			4
#define CMD2_SHIFT			8
#define ADDR_CYCLES_SHIFT		28

#define XFER_COMPLETE			BIT(2)
#define READ_READY			BIT(1)
#define WRITE_READY			BIT(0)
#define MBIT_ERROR			BIT(3)
#define ERR_INTRPT			BIT(4)

#define PROG_PGRD			BIT(0)
#define PROG_ERASE			BIT(2)
#define PROG_STATUS			BIT(3)
#define PROG_PGPROG			BIT(4)
#define PROG_RDID			BIT(6)
#define PROG_RDPARAM			BIT(7)
#define PROG_RST			BIT(8)

#define ONFI_STATUS_FAIL		BIT(0)
#define ONFI_STATUS_READY		BIT(6)

#define PG_ADDR_SHIFT			16
#define BCH_MODE_SHIFT			25
#define BCH_EN_SHIFT			27
#define ECC_SIZE_SHIFT			16

#define MEM_ADDR_MASK			GENMASK(7, 0)
#define BCH_MODE_MASK			GENMASK(27, 25)

#define CS_MASK				GENMASK(31, 30)
#define CS_SHIFT			30

#define PAGE_ERR_CNT_MASK		GENMASK(16, 8)
#define PKT_ERR_CNT_MASK		GENMASK(7, 0)

#define ONFI_ID_LEN			8
#define TEMP_BUF_SIZE			512

/**
 * struct anfc_ecc_matrix - Defines ecc information storage format
 * @pagesize:		Page size in bytes.
 * @codeword_size:	Code word size information.
 * @eccbits:		Number of ecc bits.
 * @bch:		Bch / Hamming mode enable/disable.
 * @eccaddr:		Ecc start address information.
 * @eccsize:		Ecc size information.
 */
struct anfc_ecc_matrix {
	u32 pagesize;
	u32 codeword_size;
	u8 eccbits;
	u8 bch;
	u16 eccaddr;
	u16 eccsize;
};

static const struct anfc_ecc_matrix ecc_matrix[] = {
	{512,	512,	1,	0,	0x20D,	0x3},
	{512,	512,	4,	1,	0x209,	0x7},
	{512,	512,	8,	1,	0x203,	0xD},
	/* 2K byte page */
	{2048,	512,	1,	0,	0x834,	0xC},
	{2048,	512,	4,	1,	0x826,	0x1A},
	{2048,	512,	8,	1,	0x80c,	0x34},
	{2048,	512,	12,	1,	0x822,	0x4E},
	{2048,	1024,	24,	1,	0x81c,	0x54},
	/* 4K byte page */
	{4096,	512,	1,	0,	0x1068,	0x18},
	{4096,	512,	4,	1,	0x104c,	0x34},
	{4096,	512,	8,	1,	0x1018,	0x68},
	{4096,	512,	12,	1,	0x1044,	0x9C},
	{4096,	1024,	4,	1,	0x1038,	0xA8},
	/* 8K byte page */
	{8192,	512,	1,	0,	0x20d0,	0x30},
	{8192,	512,	4,	1,	0x2098,	0x68},
	{8192,	512,	8,	1,	0x2030,	0xD0},
	{8192,	512,	12,	1,	0x2088,	0x138},
	{8192,	1024,	24,	1,	0x2070,	0x150},
	/* 16K byte page */
	{16384,	512,	1,	0,	0x4460,	0x60},
	{16384,	512,	4,	1,	0x43f0,	0xD0},
	{16384,	512,	8,	1,	0x4320,	0x1A0},
	{16384,	512,	12,	1,	0x4250,	0x270},
	{16384,	1024,	24,	1,	0x4220,	0x2A0}
};

/**
 * struct anfc - Defines the Arasan NAND flash driver instance
 * @chip:		NAND chip information structure.
 * @mtd:		MTD information structure.
 * @parts:		Pointer to the mtd_partition structure.
 * @dev:		Pointer to the device structure.
 * @base:		Virtual address of the NAND flash device.
 * @curr_cmd:		Current command issued.
 * @dma:		Dma enable/disable.
 * @bch:		Bch / Hamming mode enable/disable.
 * @err:		Error identifier.
 * @iswriteoob:		Identifies if oob write operation is required.
 * @buf:		Buffer used for read/write byte operations.
 * @raddr_cycles:	Row address cycle information.
 * @caddr_cycles:	Column address cycle information.
 * @irq:		irq number
 * @page:		Page address to be use for write oob operations.
 * @pktsize:		Packet size for read / write operation.
 * @bufshift:		Variable used for indexing buffer operation
 * @rdintrmask:		Interrupt mask value for read operation.
 * @bufrdy:		Completion event for buffer ready.
 * @xfercomp:		Completion event for transfer complete.
 * @ecclayout:		Ecc layout object
 */
struct anfc {
	struct nand_chip chip;
	struct mtd_info mtd;
	struct mtd_partition *parts;
	struct device *dev;

	void __iomem *base;
	int curr_cmd;

	bool dma;
	bool bch;
	bool err;
	bool iswriteoob;

	u8 buf[TEMP_BUF_SIZE];

	u16 raddr_cycles;
	u16 caddr_cycles;

	u32 irq;
	u32 page;
	u32 pktsize;
	u32 bufshift;
	u32 rdintrmask;

	struct completion bufrdy;
	struct completion xfercomp;
	struct nand_ecclayout ecclayout;
};

static u8 anfc_page(u32 pagesize)
{
	switch (pagesize) {
	case 512:
		return PAGE_SIZE_512;
	case 2048:
		return PAGE_SIZE_2K;
	case 4096:
		return PAGE_SIZE_4K;
	case 8192:
		return PAGE_SIZE_8K;
	case 16384:
		return PAGE_SIZE_16K;
	case 1024:
		return PAGE_SIZE_1K;
	default:
		break;
	}

	return 0;
}

static inline void anfc_enable_intrs(struct anfc *nfc, u32 val)
{
	writel(val, nfc->base + INTR_STS_EN_OFST);
	writel(val, nfc->base + INTR_SIG_EN_OFST);
}

static int anfc_wait_for_event(struct anfc *nfc, u32 event)
{
	struct completion *comp;
	int ret;

	if (event == XFER_COMPLETE)
		comp = &nfc->xfercomp;
	else
		comp = &nfc->bufrdy;

	ret = wait_for_completion_timeout(comp, msecs_to_jiffies(EVNT_TIMEOUT));

	return ret;
}

static inline void anfc_setpktszcnt(struct anfc *nfc, u32 pktsize,
				    u32 pktcount)
{
	writel(pktsize | (pktcount << PKT_CNT_SHIFT), nfc->base + PKT_OFST);
}

static inline void anfc_set_eccsparecmd(struct anfc *nfc, u8 cmd1, u8 cmd2)
{
	writel(cmd1 | (cmd2 << CMD2_SHIFT) |
	       (nfc->caddr_cycles << ADDR_CYCLES_SHIFT),
	       nfc->base + ECC_SPR_CMD_OFST);
}

static void anfc_setpagecoladdr(struct anfc *nfc, u32 page, u16 col)
{
	u32 val;

	writel(col | (page << PG_ADDR_SHIFT), nfc->base + MEM_ADDR1_OFST);

	val = readl(nfc->base + MEM_ADDR2_OFST);
	val = (val & ~MEM_ADDR_MASK) |
	      ((page >> PG_ADDR_SHIFT) & MEM_ADDR_MASK);
	writel(val, nfc->base + MEM_ADDR2_OFST);
}

static void anfc_prepare_cmd(struct anfc *nfc, u8 cmd1, u8 cmd2,
			     u8 dmamode, u32 pagesize, u8 addrcycles)
{
	u32 regval;

	regval = cmd1 | (cmd2 << CMD2_SHIFT);
	if (dmamode && nfc->dma)
		regval |= DMA_ENABLE << DMA_EN_SHIFT;
	if (addrcycles)
		regval |= addrcycles << ADDR_CYCLES_SHIFT;
	if (pagesize)
		regval |= anfc_page(pagesize) << PAGE_SIZE_SHIFT;
	writel(regval, nfc->base + CMD_OFST);
}

static int anfc_device_ready(struct mtd_info *mtd,
			     struct nand_chip *chip)
{
	u8 status;
	u32 timeout = STATUS_TIMEOUT;

	while (timeout--) {
		chip->cmdfunc(mtd, NAND_CMD_STATUS, 0, 0);
		status = chip->read_byte(mtd);
		if (status & ONFI_STATUS_READY) {
			if (status & ONFI_STATUS_FAIL)
				return NAND_STATUS_FAIL;
			return 0;
		}
	}

	pr_err("%s timed out\n", __func__);
	return -ETIMEDOUT;
}

static int anfc_read_oob(struct mtd_info *mtd, struct nand_chip *chip,
			 int page)
{
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);

	chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, page);
	if (nfc->dma)
		nfc->rdintrmask = XFER_COMPLETE;
	else
		nfc->rdintrmask = READ_READY;
	chip->read_buf(mtd, chip->oob_poi, mtd->oobsize);

	return 0;
}

static int anfc_write_oob(struct mtd_info *mtd, struct nand_chip *chip,
			  int page)
{
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);

	nfc->iswriteoob = true;
	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	nfc->iswriteoob = false;

	return 0;
}

static void anfc_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	u32 i, pktcount, buf_rd_cnt = 0, pktsize;
	u32 *bufptr = (u32 *)buf;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);
	dma_addr_t paddr = 0;

	if (nfc->curr_cmd == NAND_CMD_READ0) {
		pktsize = nfc->pktsize;
		if (mtd->writesize % pktsize)
			pktcount = mtd->writesize / pktsize + 1;
		else
			pktcount = mtd->writesize / pktsize;
	} else {
		pktsize = len;
		pktcount = 1;
	}

	anfc_setpktszcnt(nfc, pktsize, pktcount);

	if (nfc->dma) {
		paddr = dma_map_single(nfc->dev, buf, len, DMA_FROM_DEVICE);
		if (dma_mapping_error(nfc->dev, paddr)) {
			dev_err(nfc->dev, "Rdbuf mapping error");
			return;
		}
		writel(paddr, nfc->base + DMA_ADDR0_OFST);
		writel(paddr >> 32, nfc->base + DMA_ADDR1_OFST);
		anfc_enable_intrs(nfc, nfc->rdintrmask);
		writel(PROG_PGRD, nfc->base + PROG_OFST);
		anfc_wait_for_event(nfc, XFER_COMPLETE);
		dma_unmap_single(nfc->dev, paddr, len, DMA_FROM_DEVICE);
		return;
	}

	anfc_enable_intrs(nfc, nfc->rdintrmask);
	writel(PROG_PGRD, nfc->base + PROG_OFST);

	while (buf_rd_cnt < pktcount) {

		anfc_wait_for_event(nfc, READ_READY);
		buf_rd_cnt++;

		if (buf_rd_cnt == pktcount)
			anfc_enable_intrs(nfc, XFER_COMPLETE);

		for (i = 0; i < pktsize / 4; i++)
			bufptr[i] = readl(nfc->base + DATA_PORT_OFST);

		bufptr += (pktsize / 4);

		if (buf_rd_cnt < pktcount)
			anfc_enable_intrs(nfc, nfc->rdintrmask);
	}

	anfc_wait_for_event(nfc, XFER_COMPLETE);
}

static void anfc_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	u32 buf_wr_cnt = 0, pktcount = 1, i, pktsize;
	u32 *bufptr = (u32 *)buf;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);
	dma_addr_t paddr = 0;

	if (nfc->iswriteoob) {
		pktsize = len;
		pktcount = 1;
	} else {
		pktsize = nfc->pktsize;
		pktcount = mtd->writesize / pktsize;
	}

	anfc_setpktszcnt(nfc, pktsize, pktcount);

	if (nfc->dma) {
		paddr = dma_map_single(nfc->dev, (void *)buf, len,
				       DMA_TO_DEVICE);
		if (dma_mapping_error(nfc->dev, paddr)) {
			dev_err(nfc->dev, "Writebuf mapping error");
			return;
		}
		writel(paddr, nfc->base + DMA_ADDR0_OFST);
		writel(paddr >> 32, nfc->base + DMA_ADDR1_OFST);
		anfc_enable_intrs(nfc, XFER_COMPLETE);
		writel(PROG_PGPROG, nfc->base + PROG_OFST);
		anfc_wait_for_event(nfc, XFER_COMPLETE);
		dma_unmap_single(nfc->dev, paddr, len, DMA_TO_DEVICE);
		return;
	}

	anfc_enable_intrs(nfc, WRITE_READY);
	writel(PROG_PGPROG, nfc->base + PROG_OFST);

	while (buf_wr_cnt < pktcount) {
		anfc_wait_for_event(nfc, WRITE_READY);

		buf_wr_cnt++;
		if (buf_wr_cnt == pktcount)
			anfc_enable_intrs(nfc, XFER_COMPLETE);

		for (i = 0; i < (pktsize / 4); i++)
			writel(bufptr[i], nfc->base + DATA_PORT_OFST);

		bufptr += (pktsize / 4);

		if (buf_wr_cnt < pktcount)
			anfc_enable_intrs(nfc, WRITE_READY);
	}

	anfc_wait_for_event(nfc, XFER_COMPLETE);
}

static int anfc_read_page_hwecc(struct mtd_info *mtd,
				struct nand_chip *chip, uint8_t *buf,
				int oob_required, int page)
{
	u32 val;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);

	anfc_set_eccsparecmd(nfc, NAND_CMD_RNDOUT, NAND_CMD_RNDOUTSTART);

	val = readl(nfc->base + CMD_OFST);
	val = val | ECC_ENABLE;
	writel(val, nfc->base + CMD_OFST);

	if (nfc->dma)
		nfc->rdintrmask = XFER_COMPLETE;
	else
		nfc->rdintrmask = READ_READY;

	if (!nfc->bch)
		nfc->rdintrmask = MBIT_ERROR;

	chip->read_buf(mtd, buf, mtd->writesize);

	val = readl(nfc->base + ECC_ERR_CNT_OFST);
	if (nfc->bch) {
		mtd->ecc_stats.corrected += val & PAGE_ERR_CNT_MASK;
	} else {
		val = readl(nfc->base + ECC_ERR_CNT_1BIT_OFST);
		mtd->ecc_stats.corrected += val;
		val = readl(nfc->base + ECC_ERR_CNT_2BIT_OFST);
		mtd->ecc_stats.failed += val;
		/* clear ecc error count register 1Bit, 2Bit */
		writel(0x0, nfc->base + ECC_ERR_CNT_1BIT_OFST);
		writel(0x0, nfc->base + ECC_ERR_CNT_2BIT_OFST);
	}
	nfc->err = false;

	if (oob_required)
		chip->ecc.read_oob(mtd, chip, page);

	return 0;
}

static int anfc_write_page_hwecc(struct mtd_info *mtd,
				 struct nand_chip *chip, const uint8_t *buf,
				 int oob_required)
{
	u32 val, i;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);
	uint8_t *ecc_calc = chip->buffers->ecccalc;
	uint32_t *eccpos = chip->ecc.layout->eccpos;

	anfc_set_eccsparecmd(nfc, NAND_CMD_RNDIN, 0);

	val = readl(nfc->base + CMD_OFST);
	val = val | ECC_ENABLE;
	writel(val, nfc->base + CMD_OFST);

	chip->write_buf(mtd, buf, mtd->writesize);

	if (oob_required) {
		anfc_device_ready(mtd, chip);
		chip->cmdfunc(mtd, NAND_CMD_READOOB, 0, nfc->page);
		if (nfc->dma)
			nfc->rdintrmask = XFER_COMPLETE;
		else
			nfc->rdintrmask = READ_READY;
		chip->read_buf(mtd, ecc_calc, mtd->oobsize);
		for (i = 0; i < chip->ecc.total; i++)
			chip->oob_poi[eccpos[i]] = ecc_calc[eccpos[i]];
		chip->ecc.write_oob(mtd, chip, nfc->page);
	}

	return 0;
}

static u8 anfc_read_byte(struct mtd_info *mtd)
{
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);

	return nfc->buf[nfc->bufshift++];
}

static void anfc_readfifo(struct anfc *nfc, u32 prog, u32 size)
{
	u32 i, *bufptr = (u32 *)&nfc->buf[0];

	anfc_enable_intrs(nfc, READ_READY);

	writel(prog, nfc->base + PROG_OFST);
	anfc_wait_for_event(nfc, READ_READY);

	anfc_enable_intrs(nfc, XFER_COMPLETE);

	for (i = 0; i < size / 4; i++)
		bufptr[i] = readl(nfc->base + DATA_PORT_OFST);

	anfc_wait_for_event(nfc, XFER_COMPLETE);
}

static int anfc_ecc_init(struct mtd_info *mtd,
			 struct nand_ecc_ctrl *ecc)
{
	u32 oob_index, i, ecc_addr, regval, bchmode = 0;
	struct nand_chip *nand_chip = mtd->priv;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);
	int found = -1;

	nand_chip->ecc.mode = NAND_ECC_HW;
	nand_chip->ecc.read_page = anfc_read_page_hwecc;
	nand_chip->ecc.write_page = anfc_write_page_hwecc;
	nand_chip->ecc.write_oob = anfc_write_oob;
	nand_chip->ecc.read_oob = anfc_read_oob;

	for (i = 0; i < sizeof(ecc_matrix) / sizeof(struct anfc_ecc_matrix);
	     i++) {
		if ((ecc_matrix[i].pagesize == mtd->writesize) &&
		    (ecc_matrix[i].codeword_size >= nand_chip->ecc_step_ds)) {
			if (ecc_matrix[i].eccbits >=
			    nand_chip->ecc_strength_ds) {
				found = i;
				break;
			}
			found = i;
		}
	}

	if (found < 0) {
		dev_err(nfc->dev, "ECC scheme not supported");
		return 1;
	}
	if (ecc_matrix[found].bch) {
		switch (ecc_matrix[found].eccbits) {
		case 12:
			bchmode = 0x1;
			break;
		case 8:
			bchmode = 0x2;
			break;
		case 4:
			bchmode = 0x3;
			break;
		case 24:
			bchmode = 0x4;
			break;
		default:
			bchmode = 0x0;
		}
	}

	nand_chip->ecc.strength = ecc_matrix[found].eccbits;
	nand_chip->ecc.size = ecc_matrix[found].codeword_size;
	nand_chip->ecc.steps = ecc_matrix[found].pagesize /
			       ecc_matrix[found].codeword_size;
	nand_chip->ecc.bytes = ecc_matrix[found].eccsize /
			       nand_chip->ecc.steps;
	nfc->ecclayout.eccbytes = ecc_matrix[found].eccsize;
	nfc->bch = ecc_matrix[found].bch;
	oob_index = nand_chip->onfi_params.spare_bytes_per_page -
		    nfc->ecclayout.eccbytes;
	ecc_addr = mtd->writesize + oob_index;

	for (i = 0; i < nand_chip->ecc.size; i++)
		nfc->ecclayout.eccpos[i] = oob_index + i;

	nfc->ecclayout.oobfree->offset = 2;
	nfc->ecclayout.oobfree->length = oob_index -
					 nfc->ecclayout.oobfree->offset;

	nand_chip->ecc.layout = &(nfc->ecclayout);
	regval = ecc_addr | (ecc_matrix[found].eccsize << ECC_SIZE_SHIFT) |
		 (ecc_matrix[found].bch << BCH_EN_SHIFT);
	writel(regval, nfc->base + ECC_OFST);

	regval = readl(nfc->base + MEM_ADDR2_OFST);
	regval = (regval & ~(BCH_MODE_MASK)) | (bchmode << BCH_MODE_SHIFT);
	writel(regval, nfc->base + MEM_ADDR2_OFST);

	if (nand_chip->ecc_step_ds >= 1024)
		nfc->pktsize = 1024;
	else
		nfc->pktsize = 512;

	return 0;
}

static void anfc_cmd_function(struct mtd_info *mtd,
			      unsigned int cmd, int column, int page_addr)
{
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);
	bool wait = false, read = false;
	u32 addrcycles, prog;
	u32 *bufptr = (u32 *)&nfc->buf[0];

	nfc->bufshift = 0;
	nfc->curr_cmd = cmd;

	if (page_addr == -1)
		page_addr = 0;
	if (column == -1)
		column = 0;

	switch (cmd) {
	case NAND_CMD_RESET:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 0);
		prog = PROG_RST;
		wait = true;
		break;
	case NAND_CMD_SEQIN:
		addrcycles = nfc->raddr_cycles + nfc->caddr_cycles;
		nfc->page = page_addr;
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_PAGEPROG, 1,
				 mtd->writesize, addrcycles);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	case NAND_CMD_READOOB:
		column += mtd->writesize;
	case NAND_CMD_READ0:
	case NAND_CMD_READ1:
		addrcycles = nfc->raddr_cycles + nfc->caddr_cycles;
		anfc_prepare_cmd(nfc, NAND_CMD_READ0, NAND_CMD_READSTART, 1,
				 mtd->writesize, addrcycles);
		anfc_setpagecoladdr(nfc, page_addr, column);
		break;
	case NAND_CMD_RNDOUT:
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_RNDOUTSTART, 1,
				 mtd->writesize, 2);
		anfc_setpagecoladdr(nfc, page_addr, column);
		if (nfc->dma)
			nfc->rdintrmask = XFER_COMPLETE;
		else
			nfc->rdintrmask = READ_READY;
		break;
	case NAND_CMD_PARAM:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		anfc_setpktszcnt(nfc, sizeof(struct nand_onfi_params), 1);
		anfc_readfifo(nfc, PROG_RDPARAM,
				sizeof(struct nand_onfi_params));
		break;
	case NAND_CMD_READID:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		anfc_setpktszcnt(nfc, ONFI_ID_LEN, 1);
		anfc_readfifo(nfc, PROG_RDID, 8);
		break;
	case NAND_CMD_ERASE1:
		addrcycles = nfc->raddr_cycles;
		prog = PROG_ERASE;
		anfc_prepare_cmd(nfc, cmd, NAND_CMD_ERASE2, 0, 0, addrcycles);
		column = page_addr & 0xffff;
		page_addr = (page_addr >> PG_ADDR_SHIFT) & 0xffff;
		anfc_setpagecoladdr(nfc, page_addr, column);
		wait = true;
		break;
	case NAND_CMD_STATUS:
		anfc_prepare_cmd(nfc, cmd, 0, 0, 0, 0);
		anfc_setpktszcnt(nfc, 1, 1);
		anfc_setpagecoladdr(nfc, page_addr, column);
		prog = PROG_STATUS;
		wait = read = true;
		break;
	default:
		return;
	}

	if (wait) {
		anfc_enable_intrs(nfc, XFER_COMPLETE);
		writel(prog, nfc->base + PROG_OFST);
		anfc_wait_for_event(nfc, XFER_COMPLETE);
	}

	if (read)
		bufptr[0] = readl(nfc->base + FLASH_STS_OFST);
}

static void anfc_select_chip(struct mtd_info *mtd, int num)
{
	u32 val;
	struct anfc *nfc = container_of(mtd, struct anfc, mtd);

	if (num == -1)
		return;

	val = readl(nfc->base + MEM_ADDR2_OFST);
	val = (val & ~(CS_MASK)) | (num << CS_SHIFT);
	writel(val, nfc->base + MEM_ADDR2_OFST);
}

static irqreturn_t anfc_irq_handler(int irq, void *ptr)
{
	struct anfc *nfc = ptr;
	u32 regval = 0, status;

	status = readl(nfc->base + INTR_STS_OFST);
	if (status & XFER_COMPLETE) {
		complete(&nfc->xfercomp);
		regval |= XFER_COMPLETE;
	}

	if (status & READ_READY) {
		complete(&nfc->bufrdy);
		regval |= READ_READY;
	}

	if (status & WRITE_READY) {
		complete(&nfc->bufrdy);
		regval |= WRITE_READY;
	}

	if (status & MBIT_ERROR) {
		nfc->err = true;
		complete(&nfc->bufrdy);
		regval |= MBIT_ERROR;
	}

	if (regval) {
		writel(regval, nfc->base + INTR_STS_OFST);
		writel(0, nfc->base + INTR_STS_EN_OFST);
		writel(0, nfc->base + INTR_SIG_EN_OFST);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int anfc_probe(struct platform_device *pdev)
{
	struct anfc *nfc;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct resource *res;
	struct mtd_part_parser_data ppdata;
	int err;

	nfc = devm_kzalloc(&pdev->dev, sizeof(*nfc), GFP_KERNEL);
	if (!nfc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	nfc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nfc->base))
		return PTR_ERR(nfc->base);

	mtd = &nfc->mtd;
	nand_chip = &nfc->chip;
	nand_chip->priv = nfc;
	mtd->priv = nand_chip;
	mtd->owner = THIS_MODULE;
	mtd->name = DRIVER_NAME;
	nfc->dev = &pdev->dev;

	nand_chip->cmdfunc = anfc_cmd_function;
	nand_chip->waitfunc = anfc_device_ready;
	nand_chip->chip_delay = 30;
	nand_chip->read_buf = anfc_read_buf;
	nand_chip->write_buf = anfc_write_buf;
	nand_chip->read_byte = anfc_read_byte;
	nand_chip->options = NAND_BUSWIDTH_AUTO;
	nand_chip->bbt_options = NAND_BBT_USE_FLASH;
	nand_chip->select_chip = anfc_select_chip;
	mtd->size = nand_chip->chipsize;
	nfc->dma = of_property_read_bool(pdev->dev.of_node,
					 "arasan,has-mdma");
	platform_set_drvdata(pdev, nfc);
	init_completion(&nfc->bufrdy);
	init_completion(&nfc->xfercomp);
	nfc->irq = platform_get_irq(pdev, 0);
	err = devm_request_irq(&pdev->dev, nfc->irq, anfc_irq_handler,
			       0, "arasannfc", nfc);
	if (err)
		return err;

	if (nand_scan_ident(mtd, 1, NULL)) {
		dev_err(&pdev->dev, "nand_scan_ident for NAND failed\n");
		return -ENXIO;
	}
	nfc->raddr_cycles = nand_chip->onfi_params.addr_cycles & 0xF;
	nfc->caddr_cycles = (nand_chip->onfi_params.addr_cycles >> 4) & 0xF;

	if (anfc_ecc_init(mtd, &nand_chip->ecc))
		return -ENXIO;

	if (nand_scan_tail(mtd)) {
		dev_err(&pdev->dev, "nand_scan_tail for NAND failed\n");
		return -ENXIO;
	}

	ppdata.of_node = pdev->dev.of_node;

	mtd_device_parse_register(&nfc->mtd, NULL, &ppdata, NULL, 0);
	return 0;
}

static int anfc_remove(struct platform_device *pdev)
{
	struct anfc *nfc = platform_get_drvdata(pdev);

	nand_release(&nfc->mtd);

	return 0;
}

static const struct of_device_id anfc_ids[] = {
	{ .compatible = "arasan,nfc-v3p10" },
	{  }
};
MODULE_DEVICE_TABLE(of, anfc_ids);

static struct platform_driver anfc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = anfc_ids,
	},
	.probe = anfc_probe,
	.remove = anfc_remove,
};
module_platform_driver(anfc_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx, Inc");
MODULE_DESCRIPTION("Arasan NAND Flash Controller Driver");
