/*
 * Micron zed zynq board paralle nand controller driver.
 *
 * Copyright (C) 2015 Micron Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/of_mtd.h>
#include <linux/debugfs.h>
#include <linux/mtd/nand_bch.h>

#include <linux/poll.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_OF
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif


#define NS_OUTPUT_PREFIX "[Micron_zed_nand]"
static int use_dma = 0;
static u32 DDR_SR = 0;
#define ENABLE_DMA 1
#define ENABLE_INTERRUPTER 0

#define NS_ERR(args...) \
	do { printk(KERN_ERR NS_OUTPUT_PREFIX " error: " args); } while(0)

#define NS_INFO(args...) \
	do { printk(KERN_INFO NS_OUTPUT_PREFIX " " args); } while(0)

#define LLD_DRIVER_NAMD "MICRON_LLD_NAND"

#define	B0					(1 << 0)
#define	B1					(1 << 1)
#define	B2					(1 << 2)
#define	B3					(1 << 3)
#define	B4					(1 << 4)
#define	B5					(1 << 5)
#define	B6					(1 << 6)
#define	B7					(1 << 7)
#define	B8					(1 << 8)
#define	B9					(1 << 9)
#define	B10					(1 << 10)
#define	B11					(1 << 11)
#define	B12					(1 << 12)
#define	B13					(1 << 13)
#define B15					(1 << 15)
/* NAND_POWER_LOSS register */
#define	B16					(1 << 16)
#define	B17					(1 << 17)
#define	B18					(1 << 18)
#define	B19					(1 << 19)
#define B31					(1 << 31)

/**********************************************************************
 * Register address mapping
 **********************************************************************/
#define NAND_SDR_DATA			0x00
#define NAND_SDR_ADDR_CMD_LEN	0x04
#define NAND_SDR_WR_LEN			0x08
#define NAND_SDR_RD_LEN			0x0C
#define NAND_CE					0x10
#define NAND_SDR_WE_TIME		0x14
#define NAND_SDR_CLE_TIME		0x18
#define NAND_SDR_ALE_TIME		0x1C
#define NAND_SDR_DQ_TIME		0x20
#define NAND_SDR_RE_TIME		0x24
#define NAND_SDR_CYCLE_TIME		0x28
#define NAND_SDR_STROBE_TIME	0x2C
#define NAND_SDR_SR				0x30
#define NAND_SDR_WR_FIFO		0x34
#define NAND_SDR_RD_FIFO		0x38

#define NAND_NVDDR_CMD			0x00
#define NAND_NVDDR_ADDR			0x04
#define NAND_NVDDR_DATA			0x08
#define NAND_NVDDR_SR			0x0C
#define NAND_NVDDR_WR_RISE_FIFO	0x10
#define NAND_NVDDR_WR_FALL_FIFO	0x14
#define NAND_NVDDR_RD_RISE_FIFO	0x18
#define NAND_NVDDR_RD_FALL_FIFO	0x1C
#define NAND_NVDDR_WR_LEN 		0x20
#define NAND_NVDDR_RD_LEN		0x24
#define NAND_NVDDR_IO_DELAY		0x28
#define NAND_NVDDR_DMA_RD_LEN	0x3C

/*  Power loss */
#define	VOLTAGE_CTRL	0x0C
#define POWER_CTRL		0x10
#define RESISTOR		0x14
/* misc */
#define NAND_CLK				0x00
#define NAND_CFG				0x04
#define NAND_RESISTOR			0x14
#define NAND_MISC_SR			0x18
#define	FPGA_SUB_VERSION		0x78
#define	FPGA_VERSION			0x7C

/* DMA */

#define MM2S_DMACR 		0x0
#define MM2S_DMASR 		0x4

#define MM2S_SA 		0x18
#define MM2S_LENGTH 	0x28

#define S2MM_DMACR 		0x30
#define S2MM_DMASR 		0x34

#define S2MM_DA 		0x48
#define S2MM_LENGTH 	0x58

#define SDR_NAND_MODE 0
#define DDR_NAND_MODE 1
#define DDR2_NAND_MODE 2

#define READ_OP 1
#define WRITE_OP 0

#define READY_TIMEOUT_2000MS 2000


struct zed_nand_chip {
	struct nand_chip	chip;
	struct mtd_info		mtd;
	struct mtd_partition    *parts;
	struct platform_device 	*pdev;

	int (*dev_ready)(struct mtd_info *mtd);
	int irq;
	u8 mode; /* 0:sdr,1:ddr;2:ddr2*/
	bool has_dma;
	struct dma_chan		*dma_chan;
	dma_addr_t		dma_io_phys;
	struct completion	comp;

	void __iomem	*sdr_reg;
	void __iomem	*nvddr_reg;
	void __iomem	*misc_regs;
	void __iomem	*dma_regs;
};


#define ENABLE_POWERLOSS_TEST 1

#if ENABLE_POWERLOSS_TEST

static char *devName = "powerloss";
static unsigned int device_num = 0;
static struct class *powerloss_class;

#define NOR_PROGRAM 1
#define NOR_ERASE 2
#define NOR_READ 3
#define NOR_FREE 4

#define START_P_POWERLOSS 5
#define START_E_POWERLOSS 6
#define START_R_POWERLOSS 7
#define START_NORMAL 8

static char currPowerlossSt = START_NORMAL;
static char initendflag = 0;
static char WERflag = 0; /* write/read/erase flag 0,1,2 */
struct powerloss_cdev {
	struct cdev _cdev;
	struct fasync_struct *powerloss_fasync;
	int flag;
};
struct powerloss_cdev *pcdev;

static void SetVcc(struct mtd_info *mtd, u32 voltage)
{
	u32 value;
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;
	value = (voltage * 255 ) / 3300;
	writel((value & 0xFF ), host->misc_regs + VOLTAGE_CTRL);
}

static void SetVccq(struct mtd_info *mtd, u32 voltage)
{
	u32 value;
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;
	value = (voltage * 255 ) / 3300;
	writel((value & 0xFF ) | B8, host->misc_regs + VOLTAGE_CTRL);
}

static void TurnOnVcc(struct mtd_info *mtd)
{
	u32 data;
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;

	data = readl(host->misc_regs + POWER_CTRL);
	writel((data | B0) & (~B1)  & (~B2)  & (~B3)  & (~B4)  & (~B5),
	       host->misc_regs + POWER_CTRL);
}

static void TurnOnVccq(struct mtd_info *mtd)
{
	u32 data;
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;
	data = readl(host->misc_regs + POWER_CTRL);
	writel((data | B8) & (~B9) & (~B10)  & (~B11)  & (~B12)  & (~B13),
	       host->misc_regs + POWER_CTRL);
}

static void PowerOn(struct mtd_info *mtd)
{
	SetVcc(mtd, 3300);
	udelay(1000);
	SetVccq(mtd, 3300);
	udelay(1000);
	TurnOnVcc(mtd);
	TurnOnVccq(mtd);
}

static int powerloss_fasync(int fd, struct file *filp, int on)
{
	return fasync_helper(fd, filp, on, &pcdev->powerloss_fasync);
	return 0;
}
static int powerloss_release(struct inode *inode, struct file *filp)
{
	powerloss_fasync(-1, filp, 0);
	return 0;
}
ssize_t powerloss_read(struct file *file, char __user *buf,
                       size_t size, loff_t *ppos)
{
	if (size != 1 )
		return -EINVAL;
	copy_to_user(buf, &WERflag, 1);
	return 1;

}
long powerloss_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err = 0;

	if (( cmd != START_P_POWERLOSS ) && ( cmd != START_E_POWERLOSS) &&
	    ( cmd != START_R_POWERLOSS) && ( cmd != START_NORMAL))
		return -EINVAL;

	switch (cmd) {

	case START_P_POWERLOSS:
		currPowerlossSt = START_P_POWERLOSS;
		printk("set powerloss mode: powerloss while programmimg.\n");
		break;

	case START_R_POWERLOSS:
		currPowerlossSt = START_R_POWERLOSS;
		printk("set powerloss mode: powerloss while reading.\n");
		break;

	case START_E_POWERLOSS:
		currPowerlossSt = START_E_POWERLOSS;
		printk("set powerloss mode: powerloss while erasing.\n");
		break;


	case START_NORMAL:
		currPowerlossSt = START_NORMAL;
		printk("set powerloss mode: powerloss while noraml status.\n");
		break;
	}
	return err;
}

static const struct file_operations powerloss_cdev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = powerloss_ioctl,
	.read = powerloss_read,
	.release = powerloss_release,
	.fasync = powerloss_fasync,
};

static int powerloss_cdev_init(void)
{
	int result;

	dev_t devno = MKDEV(device_num, 0);

	if (device_num)
		result = register_chrdev_region(devno, 1, devName);
	else {
		result = alloc_chrdev_region(&devno, 0, 1, devName);
		device_num = MAJOR(devno);
	}
	if (result < 0)
		return result;


	pcdev = kmalloc(sizeof(struct powerloss_cdev), GFP_KERNEL);
	if (!pcdev) {
		printk("%s:Couldn't powerloss cdev struct\n", devName);
		result = -ENOMEM;
		goto fail_malloc;
	}
	memset(pcdev, 0, sizeof(struct powerloss_cdev));

	cdev_init(&pcdev->_cdev, &powerloss_cdev_fops);
	pcdev->_cdev.owner = THIS_MODULE;
	pcdev->_cdev.ops = &powerloss_cdev_fops;

	cdev_add(&pcdev->_cdev, MKDEV(device_num, 0), 1);

	powerloss_class = class_create(THIS_MODULE, "powerloss");
	device_create(powerloss_class, NULL, MKDEV(device_num, 0), NULL, "powerloss");
	initendflag = 1;
	return result;

fail_malloc:
	unregister_chrdev_region(devno, 1);
	return result;

}
static void powerloss_cdev_exit(void)
{

	device_destroy(powerloss_class, MKDEV(device_num, 0));
	class_destroy(powerloss_class);

	cdev_del(&pcdev->_cdev);
	kfree(pcdev);
	printk("====>device_nume is %d \n", device_num);
	unregister_chrdev_region(MKDEV(device_num, 0), 1);

}

#endif
static int host_ready(struct mtd_info *mtd, u8 R_W);
static uint8_t zed_nand_read_byte(struct mtd_info *mtd);

static void DMAInit(struct mtd_info *mtd)
{
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;

	/* MM2S */
	writel(B2, host->dma_regs + MM2S_DMACR); /* software reset MM2S */
	while (readl(host->dma_regs + MM2S_DMACR) & B2); /*wait for reset finish */

	writel(B0, host->dma_regs + MM2S_DMACR); /* start DMA operations for MM2S */
	while (readl(host->dma_regs + MM2S_DMASR) & B0); /* wait for DMA channel running */


	/*  S2MM */
	writel(B2, host->dma_regs + S2MM_DMACR); /* software reset S2MM */
	while (readl(host->dma_regs + S2MM_DMACR) & B2); /*  wait for reset finish */

	writel(B0, host->dma_regs + S2MM_DMACR); /* start DMA operations for S2MM */
	while (readl(host->dma_regs + S2MM_DMASR) & B0); /*  wait for DMA channel running */
}

static void DDR_DmaEnable(struct mtd_info *mtd)
{
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;

	u32 temp = readl(host->misc_regs + NAND_CFG);
	temp |= B2;
	writel(temp, host->misc_regs + NAND_CFG);
}

static void DDR_DmaDisable(struct mtd_info *mtd)
{
	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;

	u32 temp = readl(host->misc_regs + NAND_CFG);
	temp &= (~B2);
	writel(temp, host->misc_regs + NAND_CFG);
}

static void send_cmd(struct mtd_info *mtd, u8 cmd, u8 *addr, u8 addrlen,
                     u8 R_W, u8 datalen)
{
	register struct nand_chip *chip = mtd->priv;
	struct zed_nand_chip *host = chip->priv;
	int i;
	/* SDR mode */
	if (host->mode == SDR_NAND_MODE) {
		writel(cmd, host->sdr_reg + NAND_SDR_DATA);
		if (addr != NULL)
			for (i = 0; i < addrlen; i++)
				writel(addr[i], host->sdr_reg + NAND_SDR_DATA);

		writel((0x01 | addrlen << 4), host->sdr_reg + NAND_SDR_ADDR_CMD_LEN);

		if (R_W == READ_OP)
			writel(datalen, host->sdr_reg + NAND_SDR_RD_LEN);
		if (R_W == WRITE_OP)
			writel(datalen, host->sdr_reg + NAND_SDR_WR_LEN);

		if (host_ready(mtd, WRITE_OP))
			NS_ERR("%d Error:Host Waiting For Ready Timeout %d ms.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
	}
	/* DDR mode */
	if (host->mode == DDR_NAND_MODE) {
		writel(cmd, host->nvddr_reg + NAND_NVDDR_CMD);
		if (host_ready(mtd, WRITE_OP))
			NS_ERR("%d Error:Host Waiting For Ready Timeout %d ms.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
		if (addr != NULL) {

			for (i = 0; i < addrlen; i++) {
				writel(addr[i], host->nvddr_reg + NAND_NVDDR_ADDR);
				if (host_ready(mtd, WRITE_OP))
					NS_ERR("%d Error:Host Waiting For Ready Timeout %d ms.\n",
					       __LINE__, READY_TIMEOUT_2000MS);
			}
		}
	}

}

static int SDR_PollingForHostReady(struct mtd_info *mtd)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	unsigned long timeo = jiffies + msecs_to_jiffies(READY_TIMEOUT_2000MS);
	int ret = 0;
	void __iomem	*_reg;

	_reg = host->sdr_reg + NAND_SDR_SR;

	while ((readl(_reg) & B0) == 0) {
		if (time_after(jiffies, timeo)) {
			ret = -1;
			break;
		}
	}
	writel(0x00, _reg);
	return ret;
}

static int DDR_PollingForHostReady(struct mtd_info *mtd, u8 R_W)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	unsigned long timeo = jiffies + msecs_to_jiffies(READY_TIMEOUT_2000MS);
	int ret = 0;
	void __iomem *_reg;
	u32 chk_bit ;

	_reg = host->nvddr_reg + NAND_NVDDR_SR;
	chk_bit = ((R_W == READ_OP) ? B1 : B0);

	if (ENABLE_INTERRUPTER == 1) {
		wait_for_completion(&host->comp);
		if ((DDR_SR & chk_bit) == 0)
			ret = -1;
		DDR_SR = 0;
	} else if (ENABLE_INTERRUPTER == 0) {
		while ((readl(_reg) & chk_bit) == 0) {
			if (time_after(jiffies, timeo)) {
				ret = -1;
				break;
			}
		}
		writel(0x00, _reg);
	}
	return ret;
}

static int  getDeviceReadyPin(struct mtd_info *mtd)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;

	return (readl(host->misc_regs + NAND_MISC_SR) & 0x01);
}

static int host_ready(struct mtd_info *mtd, u8 R_W)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;

	if (host->mode == SDR_NAND_MODE)
		return SDR_PollingForHostReady(mtd);
	if (host->mode == DDR_NAND_MODE)
		return DDR_PollingForHostReady(mtd, R_W);

}
static int nand_device_pin_ready(struct mtd_info *mtd)
{
	int sr = 0;
	int ret = 0;

	unsigned long timeo = READY_TIMEOUT_2000MS;
	timeo = jiffies + msecs_to_jiffies(timeo);
	do {
		sr = getDeviceReadyPin(mtd);
		if (sr & 0x01) {
			ret = 1;
			break;
		}
	} while (time_before(jiffies, timeo));
	return ret;
}

static int nand_device_sr_ready(struct mtd_info *mtd)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	int sr = 0;
	int ret = 0;

	unsigned long timeo = READY_TIMEOUT_2000MS;
	timeo = jiffies + msecs_to_jiffies(timeo);

	if (host->mode == SDR_NAND_MODE) {
		send_cmd(mtd, NAND_CMD_STATUS, NULL, 0, READ_OP, 1);

		do {
			sr = readl(host->sdr_reg + NAND_SDR_DATA);
			if (sr & NAND_STATUS_READY) {
				ret = 1;
				break;
			}
		} while (time_before(jiffies, timeo));
	}

	if (host->mode == DDR_NAND_MODE) {
		send_cmd(mtd, NAND_CMD_STATUS, NULL, 0, WRITE_OP, 0);

		do {
			sr = zed_nand_read_byte(mtd);
			if (sr & NAND_STATUS_READY) {
				ret = 1;
				break;
			}
		} while (time_before(jiffies, timeo));
	}
	send_cmd(mtd, NAND_CMD_READ0, NULL, 0, WRITE_OP, 0);

	printk("Read SR is 0x%x.\n", sr);
	return ret;
}

static void set_clk(struct zed_nand_chip *zed_chip, int clk)
{

	int clkm, clkd;
	int clk_wr_phase;
	int	clk_rd_phase;
	int	clk_cmd_phase;

	switch (clk) {
	case 25:
		clkm = 10;
		clkd = 40;
		clk_wr_phase = 10;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 50:
		clkm = 10;
		clkd = 20;
		clk_wr_phase = 5;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 100:
		clkm = 10;
		clkd = 10;
		clk_wr_phase = 4;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 133:
		clkm = 8;
		clkd = 8;
		clk_wr_phase = 4;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 166:
		clkm = 10;
		clkd = 6;
		clk_wr_phase = 2;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 200:
		clkm = 12;
		clkd = 6;
		clk_wr_phase = 1;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	case 250:
		clkm = 10;
		clkd = 4;
		clk_wr_phase = 1;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	default:
		clkm = 10;
		clkd = 40;
		clk_wr_phase = 10;
		clk_rd_phase = 0;
		clk_cmd_phase = 0;
		break;
	}

	writel((clk_cmd_phase << 24) | (clk_rd_phase << 18) |
	       (clk_wr_phase << 12) | (clkd << 6) | clkm, zed_chip->misc_regs + NAND_CLK);
	while ( ( readl(zed_chip->misc_regs + NAND_CLK) & B31 ) == 0);
	printk("Configure clock to %d MHz.\n\n", clk);

}

static void set_sdr_clk(struct zed_nand_chip *zed_chip, int clk)
{
	writel( 0x00020000, zed_chip->sdr_reg + NAND_SDR_WE_TIME);
	writel( 0x00020000, zed_chip->sdr_reg + NAND_SDR_RE_TIME);
	writel( 0x00030003, zed_chip->sdr_reg + NAND_SDR_CYCLE_TIME);
	writel( 0x00000002, zed_chip->sdr_reg + NAND_SDR_STROBE_TIME);
	udelay(100);
	printk("Configure clock to %d MHz.\n\n", clk);

}

static void fpga_init(struct mtd_info *mtd)
{

	struct nand_chip *nc = mtd->priv;
	struct zed_nand_chip *host = nc->priv;
	PowerOn(mtd);

	udelay(100);
	printk("The device version is %04x:%x.\n", readl(host->misc_regs + FPGA_VERSION),
	       readl(host->misc_regs + FPGA_SUB_VERSION));
}


static void set_ddr_DelayDqs(struct zed_nand_chip *host, u8 value)
{
	u16 delay_value = value << 8;
	delay_value += 8;
	writel(delay_value, host->nvddr_reg + NAND_NVDDR_IO_DELAY);
	udelay(1000);
}

static void change_host_mode(struct zed_nand_chip *host, int mode)
{
	u32 temp;
	if (mode  == DDR_NAND_MODE) {
		temp = readl(host->misc_regs + NAND_CFG);
		temp |= B0;
		writel(temp, host->misc_regs + NAND_CFG);
		set_clk(host, 50);
		set_ddr_DelayDqs(host, 6);
	}
	if (mode == SDR_NAND_MODE) {
		temp = readl(host->misc_regs + NAND_CFG);
		temp &= (~B0);
		writel(temp, host->misc_regs + NAND_CFG);
		set_sdr_clk(host, 50);
	}
}

static void __cmd_ctrl(struct mtd_info *mtd, int chipnr)
{
	return;
}

static void __nand_command(struct mtd_info *mtd, unsigned int command,
                           int column, int page_addr)
{
	register struct nand_chip *chip = mtd->priv;
	u8 zed_address[5];
	u8 addresslen = 0;
	u8 cmd_send = 0;
	u8 double_check = 0;

	/* Emulate NAND_CMD_READOOB */
	if (command == NAND_CMD_READOOB) {
		column += mtd->writesize;
		command = NAND_CMD_READ0;
	}
	/* Address latch cycle */
	if (column != -1 || page_addr != -1) {
		int ctrl = NAND_CTRL_CHANGE | NAND_NCE | NAND_ALE;
		/* Serially input address */
		if (column != -1) {
			/* Adjust columns for 16 bit buswidth */
			if (chip->options & NAND_BUSWIDTH_16 &&
			    !nand_opcode_8bits(command))
				column >>= 1;
			zed_address[addresslen] = column & 0x00FF;
			addresslen++;
			ctrl &= ~NAND_CTRL_CHANGE;
			zed_address[addresslen] = (column >> 8) & 0x00FF;
			addresslen++;
		}
		if (page_addr != -1) {
			zed_address[addresslen] = page_addr & 0x00FF;
			addresslen++;
			zed_address[addresslen] = (page_addr >> 8) & 0x00FF;
			addresslen++;
			/* One more address cycle for devices > 128MiB */
			if (chip->chipsize > (128 << 20))
				zed_address[addresslen] = (page_addr >> 16) & 0x00FF;
			addresslen++;
		}
	}

	send_cmd(mtd, command, zed_address, addresslen, WRITE_OP, 0);
	udelay(chip->chip_delay);
#ifdef DEBUG_COMNAND
	printk("[1]------Send command 0x%x. address len %d.\n", command, addresslen);
	u8 i;
	for (i = 0; i < addresslen; i++)
		printk("Send address [%d]= 0x%x.\n", i, zed_address[i]);
#endif

	/*
	 * Program and erase have their own busy handlers status, sequential
	 * in and status need no delay.
	 */
	switch (command) {

	case NAND_CMD_CACHEDPROG:
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_RNDIN:
	case NAND_CMD_STATUS:
		return;

	case NAND_CMD_RESET:
		if (chip->dev_ready)
			break;
		udelay(chip->chip_delay);
		udelay(700);/*  According to datasheet reset chapter */
		if (1 != nand_device_sr_ready(mtd))
			NS_ERR("%d Error:Device Waiting For Ready Timeout %d.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
		return;

	case NAND_CMD_RNDOUT:
		/* No ready / busy check necessary */
		command = NAND_CMD_RNDOUTSTART;
		cmd_send = 1;
		double_check = 1;
		break;
	case NAND_CMD_READ0:
		/* This applies to read commands */
		command = NAND_CMD_READSTART;
		cmd_send = 1;
		double_check = 1;
		break;
	default:
		/*
		 * If we don't have access to the busy pin, we apply the given
		 * command delay.
		 */
		if (!chip->dev_ready) {
			udelay(chip->chip_delay);
			return;
		}
	}
	if (cmd_send == 1) {
		send_cmd(mtd, command, NULL, 0, WRITE_OP, 0);
#ifdef DEBUG_COMNAND
		printk("[2]------Send command 0x%x.\n", command);
#endif

	}

	if (1 != nand_device_pin_ready(mtd)) {
		NS_ERR("%d Error:Device Waiting For Ready Timeout %d.\n",
		       __LINE__, READY_TIMEOUT_2000MS);
	}
	return ;
}

static void zed_dma_complete_func(void *completion)
{
	complete(completion);
}

static int zed_nand_dma_op(struct mtd_info *mtd, const uint8_t *buf, int len,
                           int is_read)
{
	struct dma_device *dma_dev;
	dma_addr_t phys_addr;
	struct nand_chip *chip = mtd->priv;
	struct zed_nand_chip *host = chip->priv;
	void *p = buf;
	int err = -EIO;
	enum dma_data_direction dir = is_read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	if (buf == NULL) {
		dev_err(&host->pdev->dev, "buf is NULL point.\n");
		goto err_buf;
	}
	if (buf >= high_memory) {
		dev_err(&host->pdev->dev, "buf address over high_memory 0x%llx.\n", high_memory);
		goto err_buf;
	}

	dma_dev = host->dma_chan->device;


	phys_addr = dma_map_single(&host->pdev->dev, p, len, dir);
	if (dma_mapping_error(&host->pdev->dev, phys_addr)) {
		dev_err(&host->pdev->dev, "Failed to dma_map_single\n");
		goto err_buf;
	}

	DDR_DmaEnable(mtd);
	if (is_read) {
		writel(B0, host->dma_regs + S2MM_DMACR);
		while (readl(host->dma_regs + S2MM_DMASR) & B0);

		writel(phys_addr, host->dma_regs + S2MM_DA);
		writel(len, host->dma_regs + S2MM_LENGTH);

		writel(len, host->nvddr_reg + NAND_NVDDR_RD_LEN);
		writel(len, host->nvddr_reg + NAND_NVDDR_DMA_RD_LEN);
	} else {
		writel(B0, host->dma_regs + MM2S_DMACR);
		while (readl(host->dma_regs + MM2S_DMASR) & B0);

		writel(phys_addr, host->dma_regs + MM2S_SA);
		writel(len, host->dma_regs + MM2S_LENGTH);

		writel(len, host->nvddr_reg + NAND_NVDDR_WR_LEN);
	}
	if (host_ready(mtd, is_read)) {
		NS_ERR("%d Error:Host Waiting For Ready Timeout %d.\n",
		       __LINE__, READY_TIMEOUT_2000MS);
		goto err_dma;
	}

	err = 0;

err_dma:

	dma_unmap_single(&host->pdev->dev, phys_addr, len, dir);
err_buf:
	if (err != 0)
		dev_dbg(&host->pdev->dev, "Fall back to CPU I/O\n");
	return err;
}


static void zed_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	int i;
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	u16 u16_data;
	if ((host->mode == DDR_NAND_MODE) && use_dma) {

		if (zed_nand_dma_op(mtd, &buf[0], len, READ_OP))
			NS_ERR("%d DMA read error!\n", __LINE__);

	} else  {
		if (host->mode == SDR_NAND_MODE) {
			writel(0x00, host->sdr_reg + NAND_SDR_ADDR_CMD_LEN);
			writel(len, host->sdr_reg + NAND_SDR_RD_LEN);
		} else if (host->mode == DDR_NAND_MODE) {
			DDR_DmaDisable(mtd);
			writel(len, host->nvddr_reg + NAND_NVDDR_RD_LEN);
		}

		if (host_ready(mtd, READ_OP)) {
			NS_ERR("%d Error:Host Waiting For Ready Timeout %d.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
		}
		if ((1 != nand_device_pin_ready(mtd)) &&
		    host->mode == SDR_NAND_MODE) {
			NS_ERR("%d Error:Device Waiting For Ready Timeout %d.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
		}
		for (i = 0; i < len;) {
			if (host->mode == SDR_NAND_MODE) {
				buf[i] = (uint8_t)readl(host->sdr_reg + NAND_SDR_DATA);
				i++;
			} else if (host->mode == DDR_NAND_MODE) {
				u16_data = (u16)readl(host->nvddr_reg + NAND_NVDDR_DATA);
				buf[i] = (u8)u16_data;
				buf[i + 1] = (u8)(u16_data >> 8);

				i += 2;
			}
		}
	}
#ifdef ENABLE_DEBUG
	printk(" \n====>read %d data by %d mode:\n", len, host->mode);
	for (i = 0; i < len; i++) {
		printk("0x%x ", buf[i]);

		if ((!((i + 1) % 10)) && (i))printk("\n");

	}
#endif
}

static void zed_nand_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	int i;
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	u16 u16_data = 0;

#ifdef ENABLE_DEBUG
	printk(" \n====>Write %d data by %d mode:\n", len, host->mode);
	for (i = 0; i < len; i++) {
		printk("0x%x ", buf[i]);
		if ((!((i + 1) % 10)) && (i))printk("\n");
	}
#endif
	if (host->mode == SDR_NAND_MODE) {
		for (i = 0; i < len; i++) {
			if (host->mode == SDR_NAND_MODE)
				writel(buf[i], host->sdr_reg + NAND_SDR_DATA);
		}

		writel(0x00, host->sdr_reg + NAND_SDR_ADDR_CMD_LEN);
		writel(len, host->sdr_reg + NAND_SDR_WR_LEN);

		if (host_ready(mtd, WRITE_OP)) {
			NS_ERR("%d Error:Host Waiting For Ready Timeout %d.\n",
			       __LINE__, READY_TIMEOUT_2000MS);
			return ;
		}
	} else if (host->mode == DDR_NAND_MODE) {
		if (use_dma && len > 2) {
			if (zed_nand_dma_op(mtd, &buf[0], len, WRITE_OP))
				NS_ERR("%d DMA write error!\n", __LINE__);
		} else {

			DDR_DmaDisable(mtd);
			for (i = 0; i < len; ) {
				if (len % 2 == 0)
					u16_data = (buf[i + 1] << 8) | buf[i];
				else {

				}
				writel(u16_data, host->nvddr_reg + NAND_NVDDR_DATA);
				i += 2;
			}

			writel(len, host->nvddr_reg + NAND_NVDDR_WR_LEN);

			if (host_ready(mtd, WRITE_OP)) {
				NS_ERR("%d Error:Host Waiting For Ready Timeout %d.\n",
				       __LINE__, READY_TIMEOUT_2000MS);
				return ;
			}
		}

	}

	if (1 != nand_device_pin_ready(mtd)) {
		NS_ERR("%d Error:Device Waiting For Ready Timeout %d.\n",
		       __LINE__, READY_TIMEOUT_2000MS);
		return;
	}
}


static uint8_t zed_nand_read_byte(struct mtd_info *mtd)
{
	register struct nand_chip *chip = mtd->priv;
	register struct zed_nand_chip *host = chip->priv;
	uint8_t ret = 0xFF;

	if (host->mode == SDR_NAND_MODE) {
		writel(00, host->sdr_reg + NAND_SDR_ADDR_CMD_LEN);
		writel(1, host->sdr_reg + NAND_SDR_RD_LEN);
	} else if (host->mode == DDR_NAND_MODE) {
		DDR_DmaDisable(mtd);
		writel(2, host->nvddr_reg + NAND_NVDDR_RD_LEN);
	}
	if ((host_ready(mtd, READ_OP)) &&
	    host->mode == SDR_NAND_MODE) {
		NS_ERR("%d Error:Host Waiting For Ready Timeout %d.\n",
		       __LINE__, READY_TIMEOUT_2000MS);
	}

	if (1 != nand_device_pin_ready(mtd)) {
		NS_ERR("%d Error:Device Waiting For Ready Timeout %d.\n",
		       __LINE__, READY_TIMEOUT_2000MS);
	}

	if (host->mode == SDR_NAND_MODE)
		ret = (uint8_t)readl(host->sdr_reg + NAND_SDR_DATA);
	else if (host->mode == DDR_NAND_MODE)
		ret = (uint8_t)readl(host->nvddr_reg + NAND_NVDDR_DATA);

#ifdef ENABLE_DEBUG
	printk("====>Read byte [0x%x],by %d mode \n", ret, host->mode);
#endif
	return ret;
}

static irqreturn_t zynq_irq_handler(int irq, void *dev_id)
{
	struct zed_nand_chip *host = dev_id;

	if (host->mode == SDR_NAND_MODE) {
		DDR_SR = readl(host->sdr_reg + NAND_SDR_SR);
		writel(0x00 , host->sdr_reg + NAND_SDR_SR);
	}
	if (host->mode == DDR_NAND_MODE) {
		DDR_SR = readl(host->nvddr_reg + NAND_NVDDR_SR);
		writel(0x00 , host->nvddr_reg + NAND_NVDDR_SR);
	}
	zed_dma_complete_func(&host->comp);
	printk("interrupter is running.mode %d.\n", host->mode);
	return IRQ_HANDLED;
}

static int zed_nand_chip_check_interface(struct zed_nand_chip *host,
        int mode)
{

	int ret = 0;

	int check_mode = ((mode == SDR_NAND_MODE) ? 0x00 : 0x10);
	uint8_t feature[ONFI_SUBFEATURE_PARAM_LEN] = {0, 0, 0, 0};

	ret = host->chip.onfi_get_features(&host->mtd, &host->chip,
	                                   ONFI_FEATURE_ADDR_TIMING_MODE, feature);

	if (ret || (feature[0] & 0xF0) != check_mode) {
		NS_ERR("Set nand mode error! Read mode is 0x%x.\n", feature[0]);
		return -1;
	} else
		NS_INFO("Set Nand mode %s successfully!\n",
		        (mode == SDR_NAND_MODE ? "SDR" : "DDR"));
	return ret;
}
static int zed_nand_chip_init_timings(struct zed_nand_chip *host,
                                      int mode)
{
	int ret = 0;
	int set_mode;
	uint8_t feature[ONFI_SUBFEATURE_PARAM_LEN] = {0, 0, 0, 0};

	switch (mode) {

	case SDR_NAND_MODE:
		set_mode = onfi_get_async_timing_mode(&host->chip);
		if (set_mode == ONFI_TIMING_MODE_UNKNOWN) {
			set_mode = host->chip.onfi_timing_mode_default;
		}
		set_mode &= 0x0F;
		feature[0] = set_mode;
		break;
	case DDR_NAND_MODE :
		set_mode = onfi_get_sync_timing_mode(&host->chip);
		if (set_mode == ONFI_TIMING_MODE_UNKNOWN) {
			set_mode = host->chip.onfi_timing_mode_default;
		}
		set_mode = 0x10;
		feature[0] = set_mode;
		break;
	default :
		NS_ERR("Interface Mode [%d] error.\n", mode);
		return -1;
		break;
	}

	/* [1] send SET FEATURE commond to NAND */
	feature[0] = set_mode;
	printk("set mode value is 0x%x.\n", feature[0]);
	ret = host->chip.onfi_set_features(&host->mtd, &host->chip,
	                                   ONFI_FEATURE_ADDR_TIMING_MODE, feature);
	udelay(10);
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id zed_nand_mach_id[];
#endif
static struct platform_driver zed_nand_driver;
struct zed_nand_chip *fnand;
struct resource *nand_res;

static int zed_nand_probe(struct platform_device *pdev)
{
	int err = 0;
	int retval = -ENOMEM;
	struct mtd_info *mtd;
	struct nand_chip *nand_chip;
	struct mtd_part_parser_data ppdata;
	struct resource *misc_reg;
	struct resource *sdr_reg;
	struct resource *nvddr_reg;
	struct resource *dma_reg;
	int m = 0;
	int mode_transfer = 0;
#ifdef CONFIG_OF
	const unsigned int *prop;
#endif

	fnand = devm_kzalloc(&pdev->dev, sizeof(*fnand), GFP_KERNEL);
	if (!fnand) {
		dev_err(&pdev->dev, "failed to allocate device structure.\n");
		return -ENOMEM;
	}
	fnand->pdev = pdev;
	mtd = &fnand->mtd;
	mtd->name = "zed_nand";
	nand_chip = &fnand->chip;

	misc_reg = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sdr_reg = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	nvddr_reg = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	dma_reg = platform_get_resource(pdev, IORESOURCE_MEM, 3);

#ifdef ENABLE_DEBUG
	printk("misc_reg is start 0x%x.end 0x%x\n", misc_reg->start, misc_reg->end);
	printk("sdr_reg is start 0x%x.end 0x%x\n", sdr_reg->start, sdr_reg->end);
	printk("nvddr_reg is start 0x%x.end 0x%x\n", nvddr_reg->start, nvddr_reg->end);
	printk("dma_reg is start 0x%x.end 0x%x\n", dma_reg->start, dma_reg->end);
#endif

	if (!misc_reg || !sdr_reg || !nvddr_reg) {
		err = -ENODEV;
		dev_err(&pdev->dev, "Platform_get_resource failed!\n");
		goto out_free_data;
	}
	fnand->dma_io_phys = (dma_addr_t)dma_reg->start;

	fnand->misc_regs = devm_ioremap_resource(&pdev->dev, misc_reg);

	fnand->sdr_reg = devm_ioremap_resource(&pdev->dev, sdr_reg);

	fnand->nvddr_reg = devm_ioremap_resource(&pdev->dev, nvddr_reg);

	fnand->dma_regs = devm_ioremap_resource(&pdev->dev, dma_reg);


#ifdef ENABLE_DEBUG
	printk("remap misc_reg is start 0x%x.\n", (int)fnand->misc_regs);
	printk("remap sdr_reg is start 0x%x.\n", (int)fnand->sdr_reg);
	printk("remap nvddr_reg is start 0x%x.\n", (int)fnand->nvddr_reg);
	printk("remap dma_regs is start 0x%x.\n", (int)fnand->dma_regs);
#endif
	if (IS_ERR(fnand->misc_regs) || IS_ERR(fnand->sdr_reg) || IS_ERR(fnand->nvddr_reg)) {
		err = -EIO;
		dev_err(&pdev->dev, "devm_ioremap_resource for cont failed\n");
		goto out_free_data;
	}

#ifdef CONFIG_OF
	prop = of_get_property(pdev->dev.of_node, "xlnx,nand-width", NULL);
	if (prop) {
		if (be32_to_cpup(prop) == 16) {
			nand_chip->options |= NAND_BUSWIDTH_16;
		} else if (be32_to_cpup(prop) == 8) {
			nand_chip->options &= ~NAND_BUSWIDTH_16;
		} else {
			dev_info(&pdev->dev, "xlnx,nand-width not valid, using 8");
			nand_chip->options &= ~NAND_BUSWIDTH_16;
		}
	} else {
		dev_info(&pdev->dev, "xlnx,nand-width not in device tree, using 8");
		nand_chip->options &= ~NAND_BUSWIDTH_16;
	}

	prop = of_get_property(pdev->dev.of_node, "zed,nand-mode", NULL);
	if (prop) {
		if (be32_to_cpup(prop) == 0) {
			fnand->mode = SDR_NAND_MODE;
			dev_info(&pdev->dev, "zed,nand-mode SDR mode.\n");
		} else if (be32_to_cpup(prop) == 1) {
			fnand->mode = SDR_NAND_MODE;
			mode_transfer = DDR_NAND_MODE;
			dev_info(&pdev->dev, "zed,nand-mode later transfer to DDR mode.\n");
		} else if (be32_to_cpup(prop) == 2) {
			fnand->mode = SDR_NAND_MODE;
			mode_transfer = DDR2_NAND_MODE;
			dev_info(&pdev->dev, "zed,nand-mode later transfer to DDR2 mode.\n");
		} else {
			dev_info(&pdev->dev, "zed,nand-mode not valid, using SDR mode");
			fnand->mode = SDR_NAND_MODE;
		}
	} else {
		dev_info(&pdev->dev, "zed,nand-mode not in device tree, using SDR mode.\n");
		fnand->mode = SDR_NAND_MODE;
	}

	fnand->has_dma = of_property_read_bool(pdev->dev.of_node, "zed,nand-has-dma");
	fnand->irq = platform_get_irq(pdev, 0);
	dev_info(&pdev->dev, "Get NAND pl interrupter num is [%d] \n", fnand->irq);

	if (fnand->irq < 0) {
		retval = -ENXIO;
		dev_err(&pdev->dev, "IRQ resource not found\n");
		goto out_free_data;
	}


#endif

	init_completion(&fnand->comp);

	nand_chip->priv = fnand;
	mtd->priv = nand_chip;
	mtd->owner = THIS_MODULE;
	nand_chip->IO_ADDR_R = fnand->misc_regs;
	nand_chip->IO_ADDR_W = fnand->misc_regs;
	nand_chip->cmd_ctrl = __cmd_ctrl;
	nand_chip->cmdfunc = __nand_command;
	/* nand_chip->dev_ready = getDeviceReadyPin; */
	nand_chip->read_buf = zed_nand_read_buf;
	nand_chip->write_buf = zed_nand_write_buf;
	nand_chip->read_byte = zed_nand_read_byte;
	nand_chip->bbt_options |= NAND_BBT_USE_FLASH;
	nand_chip->options |= NAND_NO_SUBPAGE_WRITE;

	platform_set_drvdata(pdev, fnand);

	fpga_init(mtd);
	retval = nand_scan_ident(mtd, 1, NULL);
	if (retval) {
		NS_ERR("Scan NAND Device Failed!\n");
		if (retval > 0)
			retval = -ENXIO;
		goto out_free_data;
	}

	NS_INFO("Using %u-bit/%u bytes BCH ECC\n", nand_chip->ecc_strength_ds,
	        nand_chip->ecc_step_ds);
	nand_chip->ecc.mode = NAND_ECC_SOFT_BCH;
	nand_chip->ecc.size = nand_chip->ecc_step_ds;
	/*
	* Calculate ecc byte accroding ecc strength and ecc size.
	*/
	NS_INFO("nand_chip->ecc.size %d \n", nand_chip->ecc.size);

	while (1) {
		m++;
		if (((nand_chip->ecc.size * 8) >> m) == 0) {
			nand_chip->ecc.bytes = (nand_chip->ecc_strength_ds * m + 7) / 8;
			pr_warn("strength_ds = %d, bytes = %d\n",
			        nand_chip->ecc_strength_ds, nand_chip->ecc.bytes);
			break;
		}
	}
	nand_chip->ecc.strength = nand_chip->ecc.bytes * 8 / fls(8 * nand_chip->ecc.size);//bean add for 4.0


	if ((ENABLE_DMA == 1) && (fnand->has_dma)) {
		DMAInit(mtd);
		use_dma = 1;
	}
	if (use_dma)
		dev_info(&pdev->dev, "Using %s for DMA transfers.\n",
		         "dma");
	else
		dev_info(&pdev->dev, "No DMA support for NAND access.\n");



	if (mode_transfer != fnand->mode) {

		retval = zed_nand_chip_init_timings(fnand, mode_transfer);
		if (!retval) {

			fnand->mode = mode_transfer;
			NS_INFO("Send transfer-mode to %s command OK.\n",
			        (fnand->mode == DDR_NAND_MODE) ? "DDR" : "DDR2");

			change_host_mode(fnand, DDR_NAND_MODE);
			/*nand_chip->options |= NAND_OWN_BUFFERS */
		}
		retval = zed_nand_chip_check_interface(fnand, mode_transfer);

		if (retval) {
			NS_INFO("Transfer-mode to %s mode failed.\n",
			        (mode_transfer == DDR_NAND_MODE) ? "DDR" : "DDR2");
			NS_INFO("Recover host mode to %s .\n",
			        (mode_transfer == DDR_NAND_MODE) ? "SDR" : "DDR");
			change_host_mode(fnand, SDR_NAND_MODE);
			fnand->mode = SDR_NAND_MODE;
		} else
			NS_INFO("Wonderful.Host and Device all transfer mode to %s mode.\n",
			        (mode_transfer == DDR_NAND_MODE) ? "DDR" : "DDR2");

	}

	if (ENABLE_INTERRUPTER == 1) {
		retval = devm_request_irq(&pdev->dev, fnand->irq, zynq_irq_handler,
		                          0, pdev->name, fnand);
		if (retval != 0) {
			retval = -ENXIO;
			dev_err(&pdev->dev, "Request_irq failed\n");
			goto out_free_data;
		}
	}


	/* second phase scan */
	retval = nand_scan_tail(mtd);
	if (retval) {
		dev_err(&pdev->dev, "Can't register Zed NAND controller\n");
		if (retval > 0)
			retval = -ENXIO;
		goto out_free_data;
	}

#ifdef CONFIG_OF
	ppdata.of_node = pdev->dev.of_node;
#endif

	err = mtd_device_parse_register(&fnand->mtd, NULL, &ppdata, NULL, 0);
	if (!err) {
		powerloss_cdev_init();
		dev_info(&pdev->dev, "Register Zed NAND Controller Successfully.\n");
		return 0;
	}

out_free_data:
	if (fnand->dma_chan)
		dma_release_channel(fnand->dma_chan);
	kfree(fnand);

	return retval;
}

static int zed_nand_remove(struct platform_device *pdev)
{

	struct zed_nand_chip *fnand = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &fnand->mtd;
	struct resource *nand_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	nand_release(mtd);
	release_mem_region(nand_res->start, resource_size(nand_res));

	if (fnand->dma_chan)
		dma_release_channel(fnand->dma_chan);

	powerloss_cdev_exit();
	kfree(fnand);

	return 0;
}

static const struct of_device_id zed_nand_mach_id[] = {
	{.compatible = "xlnx,Zed-Hspeed-Nand-1.0" },
	{},
};
MODULE_DEVICE_TABLE(of, zed_nand_mach_id);


static struct platform_driver zed_nand_driver = {
	.probe		= zed_nand_probe,
	.remove		= zed_nand_remove,
	.suspend	= NULL,
	.resume		= NULL,
	.driver		= {
		.name	= LLD_DRIVER_NAMD,
		.owner  = THIS_MODULE,
		.of_match_table = zed_nand_mach_id,
	},
};

module_platform_driver(zed_nand_driver);
MODULE_AUTHOR("BeanHuo@micron.com, Micorn.Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MTD nand controller driver with poserloss module for zynq zed");
