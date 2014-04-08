/*
 * Xilinx AXI Traffic Generator
 *
 * Copyright (C) 2013 - 2014 Xilinx, Inc.
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
#define XTG_PARAM_RAM_OFFSET	   0x1000  /* Parameter RAM offset */
#define XTG_COMMAND_RAM_OFFSET	   0x8000  /* Command RAM offset */
#define XTG_MASTER_RAM_INIT_OFFSET 0x10000 /* Master RAM initial offset(v1.0) */
#define XTG_MASTER_RAM_OFFSET	   0xc000  /* Master RAM offset */

/* Register Offsets */
#define XTG_MCNTL_OFFSET	0x00	/* Master control */
#define XTG_SCNTL_OFFSET	0x04	/* Slave control */
#define XTG_ERR_STS_OFFSET	0x08	/* Error status  */
#define XTG_ERR_EN_OFFSET	0x0C	/* Error enable */
#define XTG_MSTERR_INTR_OFFSET	0x10	/* Master error interrupt enable */
#define XTG_CFG_STS_OFFSET	0x14	/* Config status */
#define XTG_STREAM_CNTL_OFFSET	0x30	/* Streaming Control */
#define XTG_STREAM_TL_OFFSET	0x38    /* Streaming Transfer Length */
#define XTG_STATIC_CNTL_OFFSET	0x60	/* Static Control */
#define XTG_STATIC_LEN_OFFSET	0x64	/* Static Length */

/* Register Bitmasks/shifts */

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
/* Core Revision shift */
#define XTG_MCNTL_REV_SHIFT		24

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

/* Axi Traffic Generator Static Mode masks */
#define XTG_STATIC_CNTL_TD_MASK		0x00000002	/* Transfer Done Mask */
#define XTG_STATIC_CNTL_STEN_MASK	0x00000001	/* Static Enable Mask */
#define XTG_STATIC_CNTL_RESET_MASK	0x00000000	/* Static Reset Mask */

/* Axi Traffic Generator Stream Mode mask/shifts */
#define XTG_STREAM_CNTL_STEN_MASK   0x00000001	/* Stream Enable Mask */
#define XTG_STREAM_TL_TCNT_MASK	    0xFFFF0000	/* Transfer Count Mask */
#define XTG_STREAM_TL_TLEN_MASK	    0x0000FFFF	/* Transfer Length Mask */
#define XTG_STREAM_TL_TCNT_SHIFT    16		/* Transfer Count Shift */

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

/*
 * Version value of the trafgen core.
 * For the initial IP release the version(v1.0) value is 0x47
 * From the v2.0 IP and onwards the value starts from  0x20.
 * For eg:
 * v2.1 -> 0x21
 * v2.2 -> 0x22 ... so on.
 *
 */
#define XTG_INIT_VERSION	0x47	/* Trafgen initial version(v1.0) */

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
 * @xtg_mram_offset: MasterRam offset
 */
struct xtg_dev_info {
	void __iomem *regs;
	struct device *dev;
	u32 phys_base_addr;
	s16 last_rd_valid_idx;
	s16 last_wr_valid_idx;
	u32 id;
	u32 xtg_mram_offset;
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
 * @XTG_GET_STATIC_ENABLE: get staic mode traffic genration state
 * @XTG_GET_STATIC_BURSTLEN: get static mode burst length
 * @XTG_GET_STATIC_TRANSFERDONE: get static transfer done
 * @XTG_GET_STREAM_ENABLE : get strean mode traffic genration state
 * @XTG_GET_STREAM_TRANSFERLEN: get streaming mode transfer length
 * @XTG_GET_STREAM_TRANSFERCNT: get streaming mode transfer count
 * @XTG_START_MASTER_LOGIC: start master logic
 * @XTG_SET_SLV_CTRL_REG: set slave control
 * @XTG_CLEAR_ERRORS: clear errors
 * @XTG_ENABLE_ERRORS: enable errors
 * @XTG_ENABLE_INTRS: enable interrupts
 * @XTG_CLEAR_MRAM: clear master ram
 * @XTG_CLEAR_CRAM: clear command ram
 * @XTG_CLEAR_PRAM: clear parameter ram
 * @XTG_SET_STATIC_ENABLE: enable static mode traffic genration
 * @XTG_SET_STATIC_DISABLE: disable static mode traffic genration
 * @XTG_SET_STATIC_BURSTLEN: set static mode burst length
 * @XTG_SET_STATIC_TRANSFERDONE: set static transfer done
 * @XTG_SET_STREAM_ENABLE: enable streaming mode traffic genration
 * @XTG_SET_STREAM_DISABLE: disable streaming mode traffic genration
 * @XTG_SET_STREAM_TRANSFERLEN: set streaming mode transfer length
 * @XTG_SET_STREAM_TRANSFERCNT: set streaming mode transfer count
 */
enum xtg_sysfs_ioctl_opcode {
	XTG_GET_MASTER_CMP_STS,
	XTG_GET_SLV_CTRL_REG,
	XTG_GET_ERR_STS,
	XTG_GET_CFG_STS,
	XTG_GET_LAST_VALID_INDEX,
	XTG_GET_DEVICE_ID,
	XTG_GET_RESOURCE,
	XTG_GET_STATIC_ENABLE,
	XTG_GET_STATIC_BURSTLEN,
	XTG_GET_STATIC_TRANSFERDONE,
	XTG_GET_STREAM_ENABLE,
	XTG_GET_STREAM_TRANSFERLEN,
	XTG_GET_STREAM_TRANSFERCNT,
	XTG_START_MASTER_LOGIC,
	XTG_SET_SLV_CTRL_REG,
	XTG_CLEAR_ERRORS,
	XTG_ENABLE_ERRORS,
	XTG_ENABLE_INTRS,
	XTG_CLEAR_MRAM,
	XTG_CLEAR_CRAM,
	XTG_CLEAR_PRAM,
	XTG_SET_STATIC_ENABLE,
	XTG_SET_STATIC_DISABLE,
	XTG_SET_STATIC_BURSTLEN,
	XTG_SET_STATIC_TRANSFERDONE,
	XTG_SET_STREAM_ENABLE,
	XTG_SET_STREAM_DISABLE,
	XTG_SET_STREAM_TRANSFERLEN,
	XTG_SET_STREAM_TRANSFERCNT
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
 *
 * Return: value read from the sysfs opcode.
 */
static ssize_t xtg_sysfs_ioctl(struct device *dev, const char *buf,
				enum xtg_sysfs_ioctl_opcode opcode)
{
	struct xtg_dev_info *tg = to_xtg_dev_info(dev);
	unsigned long wrval;
	ssize_t status, rdval = 0;

	if (opcode > XTG_GET_STREAM_TRANSFERCNT) {
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

	case XTG_GET_STATIC_ENABLE:
		rdval = readl(tg->regs + XTG_STATIC_CNTL_OFFSET);
		break;

	case XTG_GET_STATIC_BURSTLEN:
		rdval = readl(tg->regs + XTG_STATIC_LEN_OFFSET);
		break;

	case XTG_GET_STATIC_TRANSFERDONE:
		rdval = (readl(tg->regs + XTG_STATIC_CNTL_OFFSET) &
				XTG_STATIC_CNTL_TD_MASK);
		break;

	case XTG_GET_STREAM_ENABLE:
		rdval = readl(tg->regs + XTG_STREAM_CNTL_OFFSET);
		break;

	case XTG_GET_STREAM_TRANSFERLEN:
		rdval = (readl(tg->regs + XTG_STREAM_TL_OFFSET) &
				XTG_STREAM_TL_TLEN_MASK);
		break;

	case XTG_GET_STREAM_TRANSFERCNT:
		rdval = ((readl(tg->regs + XTG_STREAM_TL_OFFSET) &
				XTG_STREAM_TL_TCNT_MASK) >>
				XTG_STREAM_TL_TCNT_SHIFT);
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
			xtg_access_rams(tg, tg->xtg_mram_offset,
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

	case XTG_SET_STATIC_ENABLE:
		if (wrval) {
			wrval &= XTG_STATIC_CNTL_STEN_MASK;
			writel(readl(tg->regs + XTG_STATIC_CNTL_OFFSET) | wrval,
			tg->regs + XTG_STATIC_CNTL_OFFSET);
		} else {
			writel(readl(tg->regs + XTG_STATIC_CNTL_OFFSET) &
				~XTG_STATIC_CNTL_STEN_MASK,
				tg->regs + XTG_STATIC_CNTL_OFFSET);
		}
		break;

	case XTG_SET_STATIC_BURSTLEN:
		writel(wrval, tg->regs + XTG_STATIC_LEN_OFFSET);
		break;

	case XTG_SET_STATIC_TRANSFERDONE:
		wrval |= XTG_STATIC_CNTL_TD_MASK;
		writel(readl(tg->regs + XTG_STATIC_CNTL_OFFSET) | wrval,
			tg->regs + XTG_STATIC_CNTL_OFFSET);
		break;

	case XTG_SET_STREAM_ENABLE:
		if (wrval) {
			wrval &= XTG_STREAM_CNTL_STEN_MASK;
			writel(readl(tg->regs + XTG_STREAM_CNTL_OFFSET) | wrval,
			tg->regs + XTG_STREAM_CNTL_OFFSET);
		} else {
			writel(readl(tg->regs + XTG_STREAM_CNTL_OFFSET) &
			~XTG_STREAM_CNTL_STEN_MASK,
			tg->regs + XTG_STREAM_CNTL_OFFSET);
		}
		break;

	case XTG_SET_STREAM_TRANSFERLEN:
		wrval &= XTG_STREAM_TL_TLEN_MASK;
		writel(readl(tg->regs + XTG_STREAM_TL_OFFSET) | wrval,
			tg->regs + XTG_STREAM_TL_OFFSET);
		break;

	case XTG_SET_STREAM_TRANSFERCNT:
		wrval = ((wrval << XTG_STREAM_TL_TCNT_SHIFT) &
				XTG_STREAM_TL_TCNT_MASK);
		writel(readl(tg->regs + XTG_STREAM_TL_OFFSET) | wrval,
			tg->regs + XTG_STREAM_TL_OFFSET);
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

static ssize_t xtg_show_static_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STATIC_ENABLE);

	return sprintf(buf, "0x%08x\n", rdval);
}

static ssize_t xtg_static_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STATIC_ENABLE);

	return size;
}
static DEVICE_ATTR(static_en, 0644, xtg_show_static_enable, xtg_static_enable);

static ssize_t xtg_get_static_burstlen(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STATIC_BURSTLEN);

	return sprintf(buf, "%d\n", rdval);
}

static ssize_t xtg_static_burstlen(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STATIC_BURSTLEN);

	return size;
}
static DEVICE_ATTR(static_burstlen, 0644, xtg_get_static_burstlen,
			xtg_static_burstlen);

static ssize_t xtg_get_static_transferdone(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STATIC_TRANSFERDONE);

	return sprintf(buf, "%d\n", rdval);
}

static ssize_t xtg_static_transferdone(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STATIC_TRANSFERDONE);

	return size;
}
static DEVICE_ATTR(static_transferdone, 0644, xtg_get_static_transferdone,
				xtg_static_transferdone);

static ssize_t xtg_reset_static_transferdone(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STATIC_TRANSFERDONE);
	if (rdval == XTG_STATIC_CNTL_RESET_MASK)
		rdval = 1;
	else
		rdval = 0;
	return sprintf(buf, "%d\n", rdval);
}
static DEVICE_ATTR(reset_static_transferdone, 0644,
			xtg_reset_static_transferdone, NULL);

static ssize_t xtg_show_stream_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STREAM_ENABLE);

	return sprintf(buf, "0x%08x\n", rdval);
}

static ssize_t xtg_stream_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STREAM_ENABLE);

	return size;
}
static DEVICE_ATTR(stream_en, 0644, xtg_show_stream_enable, xtg_stream_enable);

static ssize_t xtg_get_stream_transferlen(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STREAM_TRANSFERLEN);

	return sprintf(buf, "%d\n", rdval);
}

static ssize_t xtg_set_stream_transferlen(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STREAM_TRANSFERLEN);

	return size;
}
static DEVICE_ATTR(stream_transferlen, 0644, xtg_get_stream_transferlen,
				xtg_set_stream_transferlen);

static ssize_t xtg_get_stream_transfercnt(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t rdval = xtg_sysfs_ioctl(dev, buf, XTG_GET_STREAM_TRANSFERCNT);

	return sprintf(buf, "%d\n", rdval);
}

static ssize_t xtg_set_stream_transfercnt(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	xtg_sysfs_ioctl(dev, buf, XTG_SET_STREAM_TRANSFERCNT);

	return size;
}
static DEVICE_ATTR(stream_transfercnt, 0644, xtg_get_stream_transfercnt,
				xtg_set_stream_transfercnt);

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
		.mode = S_IRUGO | S_IWUSR,
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
		.mode = S_IRUGO | S_IWUSR,
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

	off += tg->xtg_mram_offset;
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

	off += tg->xtg_mram_offset;
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
			tg->xtg_mram_offset) >> PAGE_SHIFT,
			XTG_MASTER_RAM_SIZE,
			vma->vm_page_prot);
	return ret;
}

static struct bin_attribute xtg_mram_attr = {
	.attr =	{
		.name = "master_ram",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = XTG_MASTER_RAM_SIZE,
	.read = xtg_mram_read,
	.write = xtg_mram_write,
	.mmap = xtg_mram_mmap,
};

static struct bin_attribute *xtg_bin_attrs[] = {
	&xtg_mram_attr,
	&xtg_pram_attr,
	&xtg_cram_attr,
	NULL,
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
	&dev_attr_static_en.attr,
	&dev_attr_static_burstlen.attr,
	&dev_attr_static_transferdone.attr,
	&dev_attr_stream_transfercnt.attr,
	&dev_attr_stream_transferlen.attr,
	&dev_attr_stream_en.attr,
	&dev_attr_reset_static_transferdone.attr,
	NULL,
};

static const struct attribute_group xtg_attributes = {
	.attrs = (struct attribute **)xtg_attrs,
	.bin_attrs = xtg_bin_attrs,
};
/**
 * xtg_cmp_intr_handler - Master Complete Interrupt handler
 * @irq: IRQ number
 * @data: Pointer to the xtg_dev_info structure
 *
 * Return: IRQ_HANDLED always
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
 * Return: IRQ_HANDLED always
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
 * This is the driver probe routine. It does all the memory
 * allocation and creates sysfs entires for the device.
 *
 * Return: 0 on success and failure value on error
 */
static int xtg_probe(struct platform_device *pdev)
{
	struct xtg_dev_info *tg;
	struct device_node *node;
	struct resource *res;
	struct device *dev;
	int err, irq, var;

	tg = devm_kzalloc(&pdev->dev, sizeof(*tg), GFP_KERNEL);
	if (!tg)
		return -ENOMEM;

	tg->dev = &(pdev->dev);
	dev = tg->dev;
	node = pdev->dev.of_node;

	/* Map the registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tg->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tg->regs))
		return PTR_ERR(tg->regs);


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
		dev_dbg(&pdev->dev, "unable to get err irq");
	} else {
		err = devm_request_irq(&pdev->dev, irq, xtg_err_intr_handler,
					0, dev_name(&pdev->dev), tg);
		if (err < 0) {
			dev_err(&pdev->dev, "unable to request irq %d", irq);
			return err;
		}
	}

	/* Map the completion interrupt, if it exists in the device tree. */
	irq = platform_get_irq_byname(pdev, "irq-out");
	if (irq < 0) {
		dev_dbg(&pdev->dev, "unable to get cmp irq");
	} else {
		err = devm_request_irq(&pdev->dev, irq, xtg_cmp_intr_handler,
					0, dev_name(&pdev->dev), tg);
		if (err < 0) {
			dev_err(&pdev->dev, "unable to request irq %d", irq);
			return err;
		}
	}

	/*
	 * Create sysfs file entries for the device
	 */
	err = sysfs_create_group(&dev->kobj, &xtg_attributes);
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

	/* Update the Proper MasterRam offset */
	tg->xtg_mram_offset = XTG_MASTER_RAM_OFFSET;
	var = readl(tg->regs + XTG_MCNTL_OFFSET) >> XTG_MCNTL_REV_SHIFT;
	if (var == XTG_INIT_VERSION)
		tg->xtg_mram_offset = XTG_MASTER_RAM_INIT_OFFSET;

	dev_info(&pdev->dev, "Probing xilinx traffic generator success\n");

	return 0;
}

/**
 * xtg_remove - Driver remove function
 * @pdev: Pointer to the platform_device structure
 *
 * This function frees all the resources allocated to the device.
 *
 * Return: 0 always
 */
static int xtg_remove(struct platform_device *pdev)
{
	struct xtg_dev_info *tg;
	struct device *dev;

	tg = dev_get_drvdata(&pdev->dev);
	dev = tg->dev;
	sysfs_remove_group(&dev->kobj, &xtg_attributes);


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
