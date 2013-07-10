/*
 * Xilinx AXI Traffic Generator
 *
 * Copyright (C) 2013 Xilinx, Inc. All rights reserved.
 *
 * Description:
 * This driver is developed for AXI Traffic Generator IP, which is
 * designed to generate AXI4 traffic which can be used to stress
 * different modules/interconnect connected in the system. Different
 * configurable options which are provided through sysfs entries
 * allow the user to generate a wide variety of traffic based on
 * their requirements.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Hw specific definitions */

/* Internal RAM Offsets */
#define XTG_PARAM_RAM_OFFSET	0x1000	/* Parameter RAM offset */
#define XTG_COMMAND_RAM_OFFSET	0x8000	/* Command RAM offset */
#define XTG_MASTER_RAM_OFFSET	0x10000	/* Master RAM offset */

/* Register Offsets */
#define XTG_MCNTL_OFFSET	0x00	/* Master control */
#define XTG_SCNTL_OFFSET	0x04	/* Slave control */
#define XTG_ERR_STS_OFFSET	0x08	/* Error status  */
#define XTG_ERR_EN_OFFSET	0x0C	/* Error enable */
#define XTG_MSTERR_INTR_OFFSET	0x10	/* Master error interrupt enable */
#define XTG_CFG_STS_OFFSET	0x14	/* Config status */

/* Register Bitmasks */

/* Master logic enable */
#define XTG_MCNTL_MSTEN_MASK		0x00100000
/* Slave error interrupt enable */
#define XTG_SCNTL_ERREN_MASK		0x00008000
/* Master complete interrupt enable */
#define XTG_ERR_EN_MSTIRQEN_MASK	0x80000000
/* Master error interrupt enable */
#define XTG_MSTERR_INTR_MINTREN_MASK	0x00008000
/* Master complete done status */
#define XTG_ERR_STS_MSTDONE_MASK	0x80000000
/* Error mask for error status/enable registers */
#define XTG_ERR_ALL_ERRS_MASK		0x001F0003

/* Axi Traffic Generator Command RAM Entry field mask/shifts */

/* Command RAM entry masks */
#define XTG_LEN_MASK		0xFF		/* Driven to a*_len line  */
#define XTG_LOCK_MASK		0x1		/* Driven to a*_lock line */
#define XTG_BURST_MASK		0x3		/* Driven to a*_burst line */
#define XTG_SIZE_MASK		0x7		/* Driven to a*_size line */
#define XTG_ID_MASK		0x1F		/* Driven to a*_id line */
#define XTG_PROT_MASK		0x7		/* Driven to a*_prot line */
#define XTG_LAST_ADDR_MASK	0x7		/* Last address */
#define XTG_VALID_CMD_MASK	0x1		/* Valid Command */
#define XTG_MSTRAM_INDEX_MASK	0x1FFF		/* Master RAM Index */
#define XTG_OTHER_DEPEND_MASK	0x1FF		/* Other depend Command no */
#define XTG_MY_DEPEND_MASK	0x1FF		/* My depend command no */
#define XTG_QOS_MASK		0xF		/* Driven to a*_qos line */
#define XTG_USER_MASK		0xFF		/* Driven to a*_user line */
#define XTG_CACHE_MASK		0xF		/* Driven to a*_cache line */
#define XTG_EXPECTED_RESP_MASK	0x7		/* Expected response */

/* Command RAM entry shift values */
#define XTG_LEN_SHIFT		0		/* Driven to a*_len line  */
#define XTG_LOCK_SHIFT		8		/* Driven to a*_lock line */
#define XTG_BURST_SHIFT		10		/* Driven to a*_burst line */
#define XTG_SIZE_SHIFT		12		/* Driven to a*_size line */
#define XTG_ID_SHIFT		15		/* Driven to a*_id line */
#define XTG_PROT_SHIFT		21		/* Driven to a*_prot line */
#define XTG_LAST_ADDR_SHIFT	28		/* Last address */
#define XTG_VALID_CMD_SHIFT	31		/* Valid Command */
#define XTG_MSTRAM_INDEX_SHIFT	0		/* Master RAM Index */
#define XTG_OTHER_DEPEND_SHIFT	13		/* Other depend cmd num */
#define XTG_MY_DEPEND_SHIFT	22		/* My depend cmd num */
#define XTG_QOS_SHIFT		16		/* Driven to a*_qos line */
#define XTG_USER_SHIFT		5		/* Driven to a*_user line */
#define XTG_CACHE_SHIFT		4		/* Driven to a*_cache line */
#define XTG_EXPECTED_RESP_SHIFT	0		/* Expected response */

/* Axi Traffic Generator Parameter RAM Entry field mask/shifts */

/* Parameter RAM Entry field shift values */
#define XTG_PARAM_ADDRMODE_SHIFT	24	/* Address mode */
#define XTG_PARAM_INTERVALMODE_SHIFT	26	/* Interval mode */
#define XTG_PARAM_IDMODE_SHIFT		28	/* Id mode */
#define XTG_PARAM_OP_SHIFT		29	/* Opcode */

/* PARAM RAM Opcode shift values */
#define XTG_PARAM_COUNT_SHIFT		0	/* Repeat/Delay count */
#define XTG_PARAM_DELAYRANGE_SHIFT	0	/* Delay range */
#define XTG_PARAM_DELAY_SHIFT		8	/* FIXED RPT delay count */
#define XTG_PARAM_ADDRRANGE_SHIFT	20	/* Address range */

/* Parameter RAM Entry field mask values */
#define XTG_PARAM_ADDRMODE_MASK		0x3	/* Address mode */
#define XTG_PARAM_INTERVALMODE_MASK	0x3	/* Interval mode */
#define XTG_PARAM_IDMODE_MASK		0x1	/* Id mode */
#define XTG_PARAM_OP_MASK		0x7	/* Opcode */

/* PARAM RAM Opcode mask values */
#define XTG_PARAM_COUNT_MASK		0xFFFFFF/* Repeat/Delay count */
#define XTG_PARAM_DELAYRANGE_MASK	0xFF	/* Delay range */
#define XTG_PARAM_DELAY_MASK		0xFFF	/* FIXED RPT delay count */
#define XTG_PARAM_ADDRRANGE_MASK	0xF	/* Address range */

/* PARAM RAM Opcode values */
#define XTG_PARAM_OP_NOP		0x0	/* NOP mode */
#define XTG_PARAM_OP_RPT		0x1	/* Repeat mode */
#define XTG_PARAM_OP_DELAY		0x2	/* Delay mode */
#define XTG_PARAM_OP_FIXEDRPT		0x3	/* Fixed repeat delay */

/* Driver Specific Definitions */

#define MAX_NUM_ENTRIES	256	/* Number of command entries per region */

#define VALID_SIG	0xa5a5a5a5	/* Valid unique identifier */

/* Internal RAM Sizes */
#define XTG_PRM_RAM_BLOCK_SIZE	0x400	/* PRAM Block size (1KB) */
#define XTG_CMD_RAM_BLOCK_SIZE	0x1000	/* CRAM Block size (4KB) */
#define XTG_PARAM_RAM_SIZE	0x800	/* Parameter RAM (2KB) */
#define XTG_COMMAND_RAM_SIZE	0x2000	/* Command RAM (8KB) */
#define XTG_MASTER_RAM_SIZE	0x2000	/* Master RAM (8KB) */

/* RAM Access Flags */
#define XTG_READ_RAM		0x0	/* Read RAM flag */
#define XTG_WRITE_RAM		0x1	/* Write RAM flag */
#define XTG_WRITE_RAM_ZERO	0x2	/* Write Zero flag */

/* Bytes per entry */
#define XTG_CRAM_BYTES_PER_ENTRY	16 /* CRAM bytes per entry */
#define XTG_PRAM_BYTES_PER_ENTRY	4  /* PRAM bytes per entry */

/* Interrupt Definitions */
#define XTG_MASTER_CMP_INTR	0x1	/* Master complete intr flag */
#define XTG_MASTER_ERR_INTR	0x2	/* Master error intr flag */
#define XTG_SLAVE_ERR_INTR	0x4	/* Slave error intr flag */

/* Macro */
#define to_xtg_dev_info(n)	((struct xtg_dev_info *)dev_get_drvdata(n))

/**
 * struct xtg_cram - Command RAM structure
 * @addr: Address Driven to a*_addr line
 * @valid_cmd: Valid Command
 * @last_addr: Last address
 * @prot: Driven to a*_prot line
 * @id: Driven to a*_id line
 * @size: Driven to a*_size line
 * @burst: Driven to a*_burst line
 * @lock: Driven to a*_lock line
 * @length: Driven to a*_len line
 * @my_dpnd: My Depend command number
 * @other_dpnd: Other depend command number
 * @mram_idx: Master RAM index
 * @qos: Driven to a*_qos line
 * @user: Driven to a*_user line
 * @cache: Driven to a*_cache line
 * @expected_resp: Expected response
 * @index: Command Index
 * @is_write_block: Write/Read block
 * @is_valid_req: Unique signature
 *
 * FIXME: This structure is shared with the user application and
 * hence need to be synchronized. We know these kind of structures
 * should not be defined in the driver and this need to be fixed
 * if found a proper placeholder (in uapi/).
 */
struct xtg_cram {
	u32 addr;
	u32 valid_cmd;
	u32 last_addr;
	u32 prot;
	u32 id;
	u32 size;
	u32 burst;
	u32 lock;
	u32 length;
	u32 my_dpnd;
	u32 other_dpnd;
	u32 mram_idx;
	u32 qos;
	u32 user;
	u32 cache;
	u32 expected_resp;
	u16 index;
	bool is_write_block;
	u32 is_valid_req;
};

/**
 * struct xtg_pram - Parameter RAM structure
 * @op_cntl0: Control field 0
 * @op_cntl1: Control field 1
 * @op_cntl2: Control field 2
 * @addr_mode: Address mode
 * @interval_mode: Interval mode
 * @id_mode: Id mode
 * @opcode: Opcode
 * @index: Command Index
 * @is_write_block: Write/Read block
 * @is_valid_req: Unique signature
 *
 * FIXME: This structure is shared with the user application and
 * hence need to be synchronized. We know these kind of structures
 * should not be defined in the driver and this need to be fixed
 * if found a proper placeholder (in uapi/).
 */
struct xtg_pram {
	u32 op_cntl0;
	u32 op_cntl1;
	u32 op_cntl2;
	u32 addr_mode;
	u32 interval_mode;
	u32 id_mode;
	u32 opcode;
	u16 index;
	bool is_write_block;
	u32 is_valid_req;
};

/**
 * struct xtg_dev_info - Global Driver structure
 * @regs: Iomapped base address
 * @dev: Device structure
 * @phys_base_addr: Physical base address
 * @last_rd_valid_idx: Last Read Valid Command Index
 * @last_wr_valid_idx: Last Write Valid Command Index
 * @id: Device instance id
 */
struct xtg_dev_info {
	void __iomem *regs;
	struct device *dev;
	u32 phys_base_addr;
	s16 last_rd_valid_idx;
	s16 last_wr_valid_idx;
	u32 id;
};

/**
 * enum xtg_sysfs_ioctl - Ioctl opcodes
 * @XTG_GET_MASTER_CMP_STS: get master complete status
 * @XTG_GET_SLV_CTRL_REG: get slave control reg status
 * @XTG_GET_ERR_STS: get error status
 * @XTG_GET_CFG_STS: get config status
 * @XTG_GET_LAST_VALID_INDEX: get last valid index
 * @XTG_GET_DEVICE_ID: get device id
 * @XTG_GET_RESOURCE: get resource
 * @XTG_START_MASTER_LOGIC: start master logic
 * @XTG_SET_SLV_CTRL_REG: set slave control
 * @XTG_CLEAR_ERRORS: clear errors
 * @XTG_ENABLE_ERRORS: enable errors
 * @XTG_ENABLE_INTRS: enable interrupts
 * @XTG_CLEAR_MRAM: clear master ram
 * @XTG_CLEAR_CRAM: clear command ram
 * @XTG_CLEAR_PRAM: clear parameter ram
 */
enum xtg_sysfs_ioctl_opcode {
	XTG_GET_MASTER_CMP_STS,
	XTG_GET_SLV_CTRL_REG,
	XTG_GET_ERR_STS,
	XTG_GET_CFG_STS,
	XTG_GET_LAST_VALID_INDEX,
	XTG_GET_DEVICE_ID,
	XTG_GET_RESOURCE,
	XTG_START_MASTER_LOGIC,
	XTG_SET_SLV_CTRL_REG,
	XTG_CLEAR_ERRORS,
	XTG_ENABLE_ERRORS,
	XTG_ENABLE_INTRS,
	XTG_CLEAR_MRAM,
	XTG_CLEAR_CRAM,
	XTG_CLEAR_PRAM
};

/**
 * xtg_access_rams - Write/Read Master/Command/Parameter RAM
 * @tg: Pointer to xtg_dev_info structure
 * @where: Offset from base
 * @count: Number of bytes to write/read
 * @flags: Read/Write/Write Zero
 * @data: Data pointer
 */
static void xtg_access_rams(struct xtg_dev_info *tg, int where,
				int count, int flags, u32 *data)
{
	u32 index;

	for (index = 0; count > 0; index++, count -= 4) {
		if (flags) {
			if (flags & XTG_WRITE_RAM_ZERO)
				writel(0x0, tg->regs + where + index * 4);
			else
				writel(data[index],
					tg->regs + where + index * 4);
		} else {
			data[index] = readl(tg->regs + where + index * 4);
		}
	}
}

/**
 * xtg_prepare_cmd_words - Prepares all four Command RAM words
 * @tg: Pointer to xtg_dev_info structure
 * @cmdp: Pointer to xtg_cram structure
 * @cmd_words: Pointer to Command Words that needs to be prepared
 */
static void xtg_prepare_cmd_words(struct xtg_dev_info *tg,
				const struct xtg_cram *cmdp, u32 *cmd_words)
{
	/* Command Word 0 */
	cmd_words[0] = cmdp->addr;

	/* Command Word 1 */
	cmd_words[1] = 0;
	cmd_words[1] |= (cmdp->length & XTG_LEN_MASK) << XTG_LEN_SHIFT;
	cmd_words[1] |= (cmdp->lock & XTG_LOCK_MASK) << XTG_LOCK_SHIFT;
	cmd_words[1] |= (cmdp->burst & XTG_BURST_MASK) << XTG_BURST_SHIFT;
	cmd_words[1] |= (cmdp->size & XTG_SIZE_MASK) << XTG_SIZE_SHIFT;
	cmd_words[1] |= (cmdp->id & XTG_ID_MASK) << XTG_ID_SHIFT;
	cmd_words[1] |= (cmdp->prot & XTG_PROT_MASK) << XTG_PROT_SHIFT;
	cmd_words[1] |= (cmdp->last_addr & XTG_LAST_ADDR_MASK) <<
					XTG_LAST_ADDR_SHIFT;
	cmd_words[1] |= (cmdp->valid_cmd & XTG_VALID_CMD_MASK) <<
					XTG_VALID_CMD_SHIFT;

	/* Command Word 2 */
	cmd_words[2] = 0;
	cmd_words[2] |= (cmdp->mram_idx & XTG_MSTRAM_INDEX_MASK) <<
					XTG_MSTRAM_INDEX_SHIFT;
	cmd_words[2] |= (cmdp->other_dpnd & XTG_OTHER_DEPEND_MASK) <<
					XTG_OTHER_DEPEND_SHIFT;
	cmd_words[2] |= (cmdp->my_dpnd & XTG_MY_DEPEND_MASK) <<
					XTG_MY_DEPEND_SHIFT;

	/* Command Word 3 */
	cmd_words[3] = 0;
	cmd_words[3] |= (cmdp->qos & XTG_QOS_MASK) << XTG_QOS_SHIFT;
	cmd_words[3] |= (cmdp->user & XTG_USER_MASK) << XTG_USER_SHIFT;
	cmd_words[3] |= (cmdp->cache & XTG_CACHE_MASK) << XTG_CACHE_SHIFT;
	cmd_words[3] |= (cmdp->expected_resp & XTG_EXPECTED_RESP_MASK) <<
					XTG_EXPECTED_RESP_SHIFT;
}

/**
 * xtg_prepare_param_words - Prepares Parameter RAM word
 * @tg: Pointer to xtg_dev_info structure
 * @cmdp: Pointer to xtg_pram structure
 * @param_word: Pointer to Param Word that needs to be prepared
 */
static void xtg_prepare_param_word(struct xtg_dev_info *tg,
			const struct xtg_pram *cmdp, u32 *param_word)
{
	*param_word = 0;
	*param_word |= (cmdp->opcode & XTG_PARAM_OP_MASK) << XTG_PARAM_OP_SHIFT;
	*param_word |= (cmdp->addr_mode & XTG_PARAM_ADDRMODE_MASK) <<
					XTG_PARAM_ADDRMODE_SHIFT;
	*param_word |= (cmdp->id_mode & XTG_PARAM_IDMODE_MASK) <<
					XTG_PARAM_IDMODE_SHIFT;
	*param_word |= (cmdp->interval_mode & XTG_PARAM_INTERVALMODE_MASK) <<
					XTG_PARAM_INTERVALMODE_SHIFT;

	switch (cmdp->opcode) {
	case XTG_PARAM_OP_RPT:
	case XTG_PARAM_OP_DELAY:
		*param_word |= (cmdp->op_cntl0 & XTG_PARAM_COUNT_MASK) <<
					XTG_PARAM_COUNT_SHIFT;
		break;

	case XTG_PARAM_OP_FIXEDRPT:
		*param_word |= (cmdp->op_cntl0 & XTG_PARAM_ADDRRANGE_MASK) <<
					XTG_PARAM_ADDRRANGE_SHIFT;
		*param_word |= (cmdp->op_cntl1 & XTG_PARAM_DELAY_MASK) <<
					XTG_PARAM_DELAY_SHIFT;
		*param_word |= (cmdp->op_cntl2 & XTG_PARAM_DELAYRANGE_MASK) <<
					XTG_PARAM_DELAYRANGE_SHIFT;
		break;

	case XTG_PARAM_OP_NOP:
		*param_word = 0;
		break;
	}
}

/**
 * xtg_sysfs_ioctl - Implements sysfs operations
 * @dev: Device structure
 * @buf: Value to write
 * @opcode: Ioctl opcode
 */
static ssize_t xtg_sysfs_ioctl(struct device *dev, const char *buf,
				enum xtg_sysfs_ioctl_opcode opcode)
{
	struct xtg_dev_info *tg = to_xtg_dev_info(dev);
	unsigned long wrval;
	ssize_t status, rdval = 0;

	if (opcode > XTG_GET_RESOURCE) {
		status = kstrtoul(buf, 16, &wrval);
		if (status < 0)
			return status;
	}

	switch (opcode) {
	case XTG_GET_MASTER_CMP_STS:
		rdval = (readl(tg->regs + XTG_MCNTL_OFFSET) &
				XTG_MCNTL_MSTEN_MASK) ? 1 : 0;
		break;

	case XTG_GET_SLV_CTRL_REG:
		rdval = readl(tg->regs + XTG_SCNTL_OFFSET);
		break;

	case XTG_GET_ERR_STS:
		rdval = readl(tg->regs + XTG_ERR_STS_OFFSET) &
				XTG_ERR_ALL_ERRS_MASK;
		break;

	case XTG_GET_CFG_STS:
		rdval = readl(tg->regs + XTG_CFG_STS_OFFSET);
		break;

	case XTG_GET_LAST_VALID_INDEX:
		rdval = (tg->last_wr_valid_idx << 16) |
				tg->last_rd_valid_idx;
		break;

	case XTG_GET_DEVICE_ID:
		rdval = tg->id;
		break;

	case XTG_GET_RESOURCE:
		rdval = (unsigned long)tg->regs;
		break;

	case XTG_START_MASTER_LOGIC:
		if (wrval)
			writel(readl(tg->regs + XTG_MCNTL_OFFSET) |
					XTG_MCNTL_MSTEN_MASK,
				tg->regs + XTG_MCNTL_OFFSET);
		break;

	case XTG_SET_SLV_CTRL_REG:
		writel(wrval, tg->regs + XTG_SCNTL_OFFSET);
		break;

	case XTG_ENABLE_ERRORS:
		wrval &= XTG_ERR_ALL_ERRS_MASK;
		writel(readl(tg->regs + XTG_ERR_EN_OFFSET) | wrval,
			tg->regs + XTG_ERR_EN_OFFSET);
		break;

	case XTG_CLEAR_ERRORS:
		wrval &= XTG_ERR_ALL_ERRS_MASK;
		writel(readl(tg->regs + XTG_ERR_STS_OFFSET) | wrval,
			tg->regs + XTG_ERR_STS_OFFSET);
		break;

	case XTG_ENABLE_INTRS:
		if (wrval & XTG_MASTER_CMP_INTR) {
			pr_info("Enabling Master Complete Interrupt\n");
			writel(readl(tg->regs + XTG_ERR_EN_OFFSET) |
					XTG_ERR_EN_MSTIRQEN_MASK,
				tg->regs + XTG_ERR_EN_OFFSET);
		}
		if (wrval & XTG_MASTER_ERR_INTR) {
			pr_info("Enabling Interrupt on Master Errors\n");
			writel(readl(tg->regs + XTG_MSTERR_INTR_OFFSET) |
					XTG_MSTERR_INTR_MINTREN_MASK,
				tg->regs + XTG_MSTERR_INTR_OFFSET);
		}
		if (wrval & XTG_SLAVE_ERR_INTR) {
			pr_info("Enabling Interrupt on Slave Errors\n");
			writel(readl(tg->regs + XTG_SCNTL_OFFSET) |
					XTG_SCNTL_ERREN_MASK,
				tg->regs + XTG_SCNTL_OFFSET);
		}
		break;

	case XTG_CLEAR_MRAM:
		if (wrval)
			xtg_access_rams(tg, XTG_MASTER_RAM_OFFSET,
				XTG_MASTER_RAM_SIZE, XTG_WRITE_RAM |
				XTG_WRITE_RAM_ZERO, NULL);
		break;

	case XTG_CLEAR_CRAM:
		if (wrval)
			xtg_access_rams(tg, XTG_COMMAND_RAM_OFFSET,
				XTG_COMMAND_RAM_SIZE, XTG_WRITE_RAM |
				XTG_WRITE_RAM_ZERO, NULL);
		break;

	case XTG_CLEAR_PRAM:
		if (wrval)
			xtg_access_rams(tg, XTG_PARAM_RAM_OFFSET,
				XTG_PARAM_RAM_SIZE, XTG_WRITE_RAM |
				XTG_WRITE_RAM_ZERO, NULL);
		break;

	default:
		break;
	}

	return rdval;
}

/* Sysfs functions */

static ssize_t xtg_show_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_DEVICE_ID);

	return sprintf(buf, "%d\n", rdval);
}
static DEVICE_ATTR(id, S_IRUGO, xtg_show_id, NULL);

static ssize_t xtg_show_resource(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_RESOURCE);

	return sprintf(buf, "0x%08x\n", rdval);
}
static DEVICE_ATTR(resource, S_IRUGO, xtg_show_resource, NULL);

static ssize_t xtg_show_master_cmp_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_MASTER_CMP_STS);

	return sprintf(buf, "%d\n", rdval);
}

static ssize_t xtg_start_master_logic(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_START_MASTER_LOGIC);

	return size;
}
static DEVICE_ATTR(start_master, 0644, xtg_show_master_cmp_status,
				xtg_start_master_logic);

static ssize_t xtg_show_slv_ctrl_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_SLV_CTRL_REG);

	return sprintf(buf, "0x%08x\n", rdval);
}

static ssize_t xtg_config_slv_ctrl(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_SLV_CTRL_REG);

	return size;
}
static DEVICE_ATTR(config_slave, 0644, xtg_show_slv_ctrl_status,
				xtg_config_slv_ctrl);

static ssize_t xtg_show_errs(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_ERR_STS);

	return sprintf(buf, "0x%08x\n", rdval);
}

static ssize_t xtg_clear_errs(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_CLEAR_ERRORS);

	return size;
}
static DEVICE_ATTR(err_sts, 0644, xtg_show_errs, xtg_clear_errs);

static ssize_t xtg_enable_errs(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_ENABLE_ERRORS);

	return size;
}
static DEVICE_ATTR(err_en, 0644, NULL, xtg_enable_errs);

static ssize_t xtg_enable_interrupts(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_ENABLE_INTRS);

	return size;
}
static DEVICE_ATTR(intr_en, 0644, NULL, xtg_enable_interrupts);

static ssize_t xtg_show_last_valid_index(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_LAST_VALID_INDEX);

	return sprintf(buf, "0x%08x\n", rdval);
}
static DEVICE_ATTR(last_valid_index, S_IRUGO, xtg_show_last_valid_index, NULL);

static ssize_t xtg_show_config_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_CFG_STS);

	return sprintf(buf, "0x%08x\n", rdval);
}
static DEVICE_ATTR(config_sts, S_IRUGO, xtg_show_config_status, NULL);

static ssize_t xtg_clear_mram(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_CLEAR_MRAM);

	return size;
}
static DEVICE_ATTR(mram_clear, 0644, NULL, xtg_clear_mram);

static ssize_t xtg_clear_cram(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_CLEAR_CRAM);

	return size;
}
static DEVICE_ATTR(cram_clear, 0644, NULL, xtg_clear_cram);

static ssize_t xtg_clear_pram(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_CLEAR_CRAM);

	return size;
}
static DEVICE_ATTR(pram_clear, 0644, NULL, xtg_clear_pram);

static ssize_t xtg_pram_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	pr_info("No read access to Parameter RAM\n");

	return 0;
}

static ssize_t xtg_pram_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *)buf;

	if (off >= XTG_PARAM_RAM_SIZE) {
		pr_err("Requested Write len exceeds 2K PRAM size\n");
		return -ENOMEM;
	}

	if (count >= XTG_PARAM_RAM_SIZE)
		count = XTG_PARAM_RAM_SIZE;

	/* Program each command */
	if (count == sizeof(struct xtg_pram)) {
		struct xtg_pram *cmdp = (struct xtg_pram *)buf;
		u32 param_word;

		if (!cmdp)
			return -EINVAL;

		if (cmdp->is_valid_req == VALID_SIG) {
			/* Prepare parameter word */
			xtg_prepare_param_word(tg, cmdp, &param_word);

			count = XTG_PRAM_BYTES_PER_ENTRY;
			data = &param_word;

			/* Maximum command entries are 256 */
			if (cmdp->index > MAX_NUM_ENTRIES)
				return -EINVAL;

			/* Calculate the block index */
			if (cmdp->is_write_block)
				off = XTG_PRM_RAM_BLOCK_SIZE +
						cmdp->index * count;
			else
				off = cmdp->index * count;
		}
	}

	off += XTG_PARAM_RAM_OFFSET;
	xtg_access_rams(tg, off, count, XTG_WRITE_RAM, data);

	return count;
}

static ssize_t xtg_pram_mmap(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;

	ret = remap_pfn_range(vma, vma->vm_start, (tg->phys_base_addr +
			XTG_PARAM_RAM_OFFSET) >> PAGE_SHIFT,
			XTG_PARAM_RAM_SIZE, vma->vm_page_prot);
	return ret;
}

static struct bin_attribute xtg_pram_attr = {
	.attr =	{
		.name = "parameter_ram",
		.mode = 0644,
	},
	.size = XTG_PARAM_RAM_SIZE,
	.read = xtg_pram_read,
	.write = xtg_pram_write,
	.mmap = xtg_pram_mmap,
};

static ssize_t xtg_cram_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));

	off += XTG_COMMAND_RAM_OFFSET;
	xtg_access_rams(tg, off, count, XTG_READ_RAM, (u32 *)buf);

	return count;
}

static ssize_t xtg_cram_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));
	u32 *data = (u32 *)buf;

	if (off >= XTG_COMMAND_RAM_SIZE) {
		pr_err("Requested Write len exceeds 8K CRAM size\n");
		return -ENOMEM;
	}

	/* Program each command */
	if (count == sizeof(struct xtg_cram)) {
		struct xtg_cram *cmdp = (struct xtg_cram *)buf;
		u32 cmd_words[4];

		if (!cmdp)
			return -EINVAL;

		if (cmdp->is_valid_req == VALID_SIG) {
			/* Prepare command words */
			xtg_prepare_cmd_words(tg, cmdp, cmd_words);
			count = XTG_CRAM_BYTES_PER_ENTRY;
			data = cmd_words;

			/* Maximum command entries are 256 */
			if (cmdp->index > MAX_NUM_ENTRIES)
				return -EINVAL;

			/* Calculate the block index */
			if (cmdp->is_write_block)
				off = XTG_CMD_RAM_BLOCK_SIZE +
						cmdp->index * count;
			else
				off = cmdp->index * count;

			/* Store the valid command index */
			if (cmdp->valid_cmd) {
				if (cmdp->is_write_block)
					tg->last_wr_valid_idx =
							cmdp->index;
				else
					tg->last_rd_valid_idx =
							cmdp->index;
			}
		}
	}

	off += XTG_COMMAND_RAM_OFFSET;
	xtg_access_rams(tg, off, count, XTG_WRITE_RAM, data);

	return count;
}

static ssize_t xtg_cram_mmap(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;

	ret = remap_pfn_range(vma, vma->vm_start, (tg->phys_base_addr +
			XTG_COMMAND_RAM_OFFSET) >> PAGE_SHIFT,
			XTG_COMMAND_RAM_SIZE, vma->vm_page_prot);
	return ret;
}

static struct bin_attribute xtg_cram_attr = {
	.attr =	{
		.name = "command_ram",
		.mode = 0644,
	},
	.size = XTG_COMMAND_RAM_SIZE,
	.read = xtg_cram_read,
	.write = xtg_cram_write,
	.mmap = xtg_cram_mmap,
};

static ssize_t xtg_mram_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));

	off += XTG_MASTER_RAM_OFFSET;
	xtg_access_rams(tg, off, count, XTG_READ_RAM, (u32 *)buf);

	return count;
}

static ssize_t xtg_mram_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));

	if (off >= XTG_MASTER_RAM_SIZE) {
		pr_err("Requested Write len exceeds 8K MRAM size\n");
		return -ENOMEM;
	}

	off += XTG_MASTER_RAM_OFFSET;
	xtg_access_rams(tg, off, count, XTG_WRITE_RAM, (u32 *)buf);

	return count;
}

static ssize_t xtg_mram_mmap(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				struct vm_area_struct *vma)
{
	struct xtg_dev_info *tg =
		to_xtg_dev_info(container_of(kobj, struct device, kobj));
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_IO;

	ret = remap_pfn_range(vma, vma->vm_start, (tg->phys_base_addr +
			XTG_MASTER_RAM_OFFSET) >> PAGE_SHIFT,
			XTG_MASTER_RAM_SIZE,
			vma->vm_page_prot);
	return ret;
}

static struct bin_attribute xtg_mram_attr = {
	.attr =	{
		.name = "master_ram",
		.mode = 0644,
	},
	.size = XTG_MASTER_RAM_SIZE,
	.read = xtg_mram_read,
	.write = xtg_mram_write,
	.mmap = xtg_mram_mmap,
};

static const struct attribute *xtg_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_resource.attr,
	&dev_attr_start_master.attr,
	&dev_attr_config_slave.attr,
	&dev_attr_err_en.attr,
	&dev_attr_err_sts.attr,
	&dev_attr_intr_en.attr,
	&dev_attr_last_valid_index.attr,
	&dev_attr_config_sts.attr,
	&dev_attr_mram_clear.attr,
	&dev_attr_cram_clear.attr,
	&dev_attr_pram_clear.attr,
	NULL,
};

/**
 * xtg_remove_sysfs_dev_files - Remove sysfs entries for device
 * @tg: Pointer to xtg_dev_info structure
 */
static void xtg_remove_sysfs_dev_files(struct xtg_dev_info *tg)
{
	struct device *dev = tg->dev;

	sysfs_remove_files(&dev->kobj, xtg_attrs);
	sysfs_remove_bin_file(&dev->kobj, &xtg_mram_attr);
	sysfs_remove_bin_file(&dev->kobj, &xtg_cram_attr);
	sysfs_remove_bin_file(&dev->kobj, &xtg_pram_attr);
}

/**
 * xtg_create_sysfs_dev_files - Create sysfs entries for device
 * @tg: Pointer to xtg_dev_info structure
 *
 * Returns '0' on success and failure value on error
 */
static int xtg_create_sysfs_dev_files(struct xtg_dev_info *tg)
{
	struct device *dev = tg->dev;
	int err;

	err = sysfs_create_files(&dev->kobj, xtg_attrs);
	if (err < 0)
		goto out;

	err = sysfs_create_bin_file(&dev->kobj, &xtg_mram_attr);
	if (err < 0)
		goto out;

	err = sysfs_create_bin_file(&dev->kobj, &xtg_cram_attr);
	if (err < 0)
		goto out;

	err = sysfs_create_bin_file(&dev->kobj, &xtg_pram_attr);
	if (err < 0)
		goto out;

	return 0;

out:
	xtg_remove_sysfs_dev_files(tg);

	return err;
}

/**
 * xtg_cmp_intr_handler - Master Complete Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the xtg_dev_info structure
 *
 * Returns IRQ_HANDLED always
 */
static irqreturn_t xtg_cmp_intr_handler(int irq, void *data)
{
	struct xtg_dev_info *tg = (struct xtg_dev_info *)data;

	writel(readl(tg->regs + XTG_ERR_STS_OFFSET) |
			XTG_ERR_STS_MSTDONE_MASK,
		tg->regs + XTG_ERR_STS_OFFSET);

	return IRQ_HANDLED;
}

/**
 * xtg_err_intr_handler - Master/Slave Error Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the xtg_dev_info structure
 *
 * Returns IRQ_HANDLED always
 */
static irqreturn_t xtg_err_intr_handler(int irq, void *data)
{
	struct xtg_dev_info *tg = (struct xtg_dev_info *)data;
	u32 value;

	value = readl(tg->regs + XTG_ERR_STS_OFFSET) &
			XTG_ERR_ALL_ERRS_MASK;

	if (value) {
		dev_err(tg->dev, "Found errors 0x%08x\n", value);
		writel(readl(tg->regs + XTG_ERR_STS_OFFSET) | value,
			tg->regs + XTG_ERR_STS_OFFSET);
	}

	return IRQ_HANDLED;
}

/**
 * xtg_probe - Driver probe function
 * @pdev: Pointer to the platform_device structure
 *
 * Returns '0' on success and failure value on error
 */
static int xtg_probe(struct platform_device *pdev)
{
	struct xtg_dev_info *tg;
	struct device_node *node;
	struct resource *res;
	int err, irq;

	tg = devm_kzalloc(&pdev->dev, sizeof(struct xtg_dev_info), GFP_KERNEL);
	if (!tg) {
		dev_err(&pdev->dev, "Not enough memory for device\n");
		return -ENOMEM;
	}

	tg->dev = &(pdev->dev);

	node = pdev->dev.of_node;

	/* Map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tg->regs = devm_ioremap_resource(&pdev->dev, res);
	if (!tg->regs) {
		dev_err(&pdev->dev, "unable to iomap registers\n");
		return -ENOMEM;
	}

	/* Save physical base address */
	tg->phys_base_addr = res->start;

	/* Get the device instance id */
	err = of_property_read_u32(node, "xlnx,device-id", &tg->id);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to read property");
		return err;
	}

	/* Map the error interrupt, if it exists in the device tree. */
	irq = platform_get_irq_byname(pdev, "err-out");
	if (irq < 0) {
		dev_err(&pdev->dev, "unable to get err irq");
		return irq;
	}
	err = devm_request_irq(&pdev->dev, irq, xtg_err_intr_handler,
					0, dev_name(&pdev->dev), tg);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to request irq %d", irq);
		return err;
	}

	/* Map the completion interrupt, if it exists in the device tree. */
	irq = platform_get_irq_byname(pdev, "irq-out");
	if (irq < 0) {
		dev_err(&pdev->dev, "unable to get cmp irq");
		return irq;
	}
	err = devm_request_irq(&pdev->dev, irq, xtg_cmp_intr_handler,
					0, dev_name(&pdev->dev), tg);
	if (err < 0) {
		dev_err(&pdev->dev, "unable to request irq %d", irq);
		return err;
	}

	/*
	 * Create sysfs file entries for the device
	 *
	 * NOTE: We can create sysfs entries by adding attribute groups
	 * and then populate into device_driver structure. We see issue
	 * here, as this process doesn't allow to add sysfs entries with
	 * BIN attributes (SYSFS_KOBJ_BIN_ATTR). Also, this would create
	 * sysfs entries under driver/ which will be a bit confusing for
	 * users as bin files and normal files will be populated at diff
	 * erent places. So to avoid this, we created this function to
	 * add sysfs entries at a common place.
	 *
	 * this issue being addressed in mainline by
	 * 'sysfs: add support for binary attributes in groups'.
	 * It removes this overhead of creating/removing sysfs file entries.
	 */
	err = xtg_create_sysfs_dev_files(tg);
	if (err < 0) {
		dev_err(tg->dev, "unable to create sysfs entries\n");
		return err;
	}

	/*
	 * Initialize the write and read valid index values.
	 * Possible range of values for these variables is <0 255>.
	 */
	tg->last_wr_valid_idx = -1;
	tg->last_rd_valid_idx = -1;

	dev_set_drvdata(&pdev->dev, tg);

	dev_info(&pdev->dev, "Probing xilinx traffic generator\n");

	return 0;
}

/**
 * xtg_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * Always returns '0'
 */
static int xtg_remove(struct platform_device *pdev)
{
	struct xtg_dev_info *tg;

	tg = dev_get_drvdata(&pdev->dev);

	xtg_remove_sysfs_dev_files(tg);

	return 0;
}

static struct of_device_id xtg_of_match[] = {
	{ .compatible = "xlnx,axi-traffic-gen", },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, xtg_of_match);

static struct platform_driver xtg_driver = {
	.driver = {
		.name = "xilinx-trafgen",
		.owner = THIS_MODULE,
		.of_match_table = xtg_of_match,
	},
	.probe = xtg_probe,
	.remove = xtg_remove,
};

module_platform_driver(xtg_driver);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Xilinx Traffic Generator driver");
MODULE_LICENSE("GPL v2");
