/*
 * Xilinx Zynq DDR ECC Driver
 * This driver is based on ppc4xx_edac.c drivers
 *
 * Copyright (C) 2012 - 2014 Xilinx, Inc.
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

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "edac_core.h"

/* Number of cs_rows needed per memory controller */
#define ZYNQ_EDAC_NR_CSROWS	1

/* Number of channels per memory controller */
#define ZYNQ_EDAC_NR_CHANS	1

/* Granularity of reported error in bytes */
#define ZYNQ_EDAC_ERROR_GRAIN	1

#define ZYNQ_EDAC_MESSAGE_SIZE	256

/* Zynq DDR memory controller registers that are relevant to ECC */
#define ZYNQ_DDRC_CONTROL_REG_OFFSET	0x0 /* Control regsieter */
#define ZYNQ_DDRC_T_ZQ_REG_OFFSET	0xA4 /* ZQ register */

/* ECC control register */
#define ZYNQ_DDRC_ECC_CONTROL_REG_OFFSET	0xC4
/* ECC log register */
#define ZYNQ_DDRC_ECC_CE_LOG_REG_OFFSET		0xC8
/* ECC address register */
#define ZYNQ_DDRC_ECC_CE_ADDR_REG_OFFSET	0xCC
/* ECC data[31:0] register */
#define ZYNQ_DDRC_ECC_CE_DATA_31_0_REG_OFFSET	0xD0

/* Uncorrectable error info regsisters */
#define ZYNQ_DDRC_ECC_UE_LOG_REG_OFFSET		0xDC /* ECC control register */
#define ZYNQ_DDRC_ECC_UE_ADDR_REG_OFFSET	0xE0 /* ECC log register */
#define ZYNQ_DDRC_ECC_UE_DATA_31_0_REG_OFFSET	0xE4 /* ECC address register */

#define ZYNQ_DDRC_ECC_STAT_REG_OFFSET	0xF0 /* ECC statistics register */
#define ZYNQ_DDRC_ECC_SCRUB_REG_OFFSET	0xF4 /* ECC scrub register */

/* Control regsiter bitfield definitions */
#define ZYNQ_DDRC_CTRLREG_BUSWIDTH_MASK		0xC
#define ZYNQ_DDRC_CTRLREG_BUSWIDTH_SHIFT	2

#define ZYNQ_DDRCTL_WDTH_16	1
#define ZYNQ_DDRCTL_WDTH_32	0

/* ZQ register bitfield definitions */
#define ZYNQ_DDRC_T_ZQ_REG_DDRMODE_MASK		0x2

/* ECC control register bitfield definitions */
#define ZYNQ_DDRC_ECCCTRL_CLR_CE_ERR	0x2
#define ZYNQ_DDRC_ECCCTRL_CLR_UE_ERR	0x1

/* ECC correctable/uncorrectable error log register definitions */
#define ZYNQ_DDRC_ECC_CE_LOGREG_VALID		0x1
#define ZYNQ_DDRC_ECC_CE_LOGREG_BITPOS_MASK	0xFE
#define ZYNQ_DDRC_ECC_CE_LOGREG_BITPOS_SHIFT	1

/* ECC correctable/uncorrectable error address register definitions */
#define ZYNQ_DDRC_ECC_ADDRREG_COL_MASK		0xFFF
#define ZYNQ_DDRC_ECC_ADDRREG_ROW_MASK		0xFFFF000
#define ZYNQ_DDRC_ECC_ADDRREG_ROW_SHIFT		12
#define ZYNQ_DDRC_ECC_ADDRREG_BANK_MASK		0x70000000
#define ZYNQ_DDRC_ECC_ADDRREG_BANK_SHIFT	28

/* ECC statistic regsiter definitions */
#define ZYNQ_DDRC_ECC_STATREG_UECOUNT_MASK	0xFF
#define ZYNQ_DDRC_ECC_STATREG_CECOUNT_MASK	0xFF00
#define ZYNQ_DDRC_ECC_STATREG_CECOUNT_SHIFT	8

/* ECC scrub regsiter definitions */
#define ZYNQ_DDRC_ECC_SCRUBREG_ECC_MODE_MASK	0x7
#define ZYNQ_DDRC_ECC_SCRUBREG_ECCMODE_SECDED	0x4

/**
 * struct ecc_error_info - ECC error log information
 * @row:	Row number
 * @col:	Column number
 * @bank:	Bank number
 * @bitpos:	Bit position
 * @data:	Data causing the error
 */
struct ecc_error_info {
	u32 row;
	u32 col;
	u32 bank;
	u32 bitpos;
	u32 data;
};

/**
 * struct zynq_ecc_status - ECC status information to report
 * @ce_count:	Correctable error count
 * @ue_count:	Uncorrectable error count
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct zynq_ecc_status {
	u32 ce_count;
	u32 ue_count;
	struct ecc_error_info ceinfo;
	struct ecc_error_info ueinfo;
};

/**
 * struct zynq_edac_priv - Zynq DDR memory controller private instance data
 * @baseaddr:		Base address of the DDR controller
 * @ce_count:		Correctable Error count
 * @ue_count:		Uncorrectable Error count
 */
struct zynq_edac_priv {
	void __iomem *baseaddr;
	u32 ce_count;
	u32 ue_count;
};

/**
 * zynq_edac_geterror_info - Get the current ecc error info
 * @base:	Pointer to the base address of the ddr memory controller
 * @perrstatus:	Pointer to the zynq ecc status structure
 *
 * This routine determines there is any ecc error or not
 *
 * Return: zero if there is no error otherwise returns 1
 */
static int zynq_edac_geterror_info(void __iomem *base,
		struct zynq_ecc_status *perrstatus)
{
	u32 regval;
	u32 clearval = 0;

	regval = readl(base + ZYNQ_DDRC_ECC_STAT_REG_OFFSET) &
			(ZYNQ_DDRC_ECC_STATREG_UECOUNT_MASK |
			ZYNQ_DDRC_ECC_STATREG_CECOUNT_MASK);

	if (regval == 0)
		return 0;

	memset(perrstatus, 0, sizeof(struct zynq_ecc_status));

	perrstatus->ce_count = (regval & ZYNQ_DDRC_ECC_STATREG_CECOUNT_MASK) >>
				ZYNQ_DDRC_ECC_STATREG_CECOUNT_SHIFT;
	perrstatus->ue_count = (regval & ZYNQ_DDRC_ECC_STATREG_UECOUNT_MASK);

	if (perrstatus->ce_count) {
		regval = readl(base + ZYNQ_DDRC_ECC_CE_LOG_REG_OFFSET);
		if (regval & ZYNQ_DDRC_ECC_CE_LOGREG_VALID) {
			perrstatus->ceinfo.bitpos = (regval &
				ZYNQ_DDRC_ECC_CE_LOGREG_BITPOS_MASK) >>
				ZYNQ_DDRC_ECC_CE_LOGREG_BITPOS_SHIFT;
			regval = readl(base +
					ZYNQ_DDRC_ECC_CE_ADDR_REG_OFFSET);
			perrstatus->ceinfo.row = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_ROW_MASK) >>
					ZYNQ_DDRC_ECC_ADDRREG_ROW_SHIFT;
			perrstatus->ceinfo.col = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_COL_MASK);
			perrstatus->ceinfo.bank = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_BANK_MASK) >>
					ZYNQ_DDRC_ECC_ADDRREG_BANK_SHIFT;
			perrstatus->ceinfo.data = readl(base +
					ZYNQ_DDRC_ECC_CE_DATA_31_0_REG_OFFSET);
			edac_dbg(3, "ce bitposition: %d data: %d\n",
					perrstatus->ceinfo.bitpos,
					perrstatus->ceinfo.data);
		}
		clearval = ZYNQ_DDRC_ECCCTRL_CLR_CE_ERR;
	}

	if (perrstatus->ue_count) {
		regval = readl(base + ZYNQ_DDRC_ECC_UE_LOG_REG_OFFSET);
		if (regval & ZYNQ_DDRC_ECC_CE_LOGREG_VALID) {
			regval = readl(base +
					ZYNQ_DDRC_ECC_UE_ADDR_REG_OFFSET);
			perrstatus->ueinfo.row = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_ROW_MASK) >>
					ZYNQ_DDRC_ECC_ADDRREG_ROW_SHIFT;
			perrstatus->ueinfo.col = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_COL_MASK);
			perrstatus->ueinfo.bank = (regval &
					ZYNQ_DDRC_ECC_ADDRREG_BANK_MASK) >>
					ZYNQ_DDRC_ECC_ADDRREG_BANK_SHIFT;
			perrstatus->ueinfo.data = readl(base +
					ZYNQ_DDRC_ECC_UE_DATA_31_0_REG_OFFSET);
		}
		clearval |= ZYNQ_DDRC_ECCCTRL_CLR_UE_ERR;
	}

	writel(clearval, base + ZYNQ_DDRC_ECC_CONTROL_REG_OFFSET);
	writel(0x0, base + ZYNQ_DDRC_ECC_CONTROL_REG_OFFSET);

	return 1;
}

/**
 * zynq_edac_generate_message - Generate interpreted ECC status message
 * @mci:	Pointer to the edac memory controller instance
 * @perrstatus:	Pointer to the zynq ecc status structure
 * @buffer:	Pointer to the buffer in which to generate the
 *		message
 * @size:	The size, in bytes, of space available in buffer
 *
 * This routine generates to the provided buffer the portion of the
 * driver-unique report message associated with the ECC register of
 * the specified ECC status.
 */
static void zynq_edac_generate_message(const struct mem_ctl_info *mci,
			struct zynq_ecc_status *perrstatus, char *buffer,
			size_t size)
{
	struct ecc_error_info *pinfo = NULL;

	if (perrstatus->ce_count > 0)
		pinfo = &perrstatus->ceinfo;
	else
		pinfo = &perrstatus->ueinfo;

	snprintf(buffer, ZYNQ_EDAC_MESSAGE_SIZE,
		 "DDR ECC error type :%s Row %d Bank %d Col %d ",
		 (perrstatus->ce_count > 0) ? "CE" : "UE", pinfo->row,
		 pinfo->bank, pinfo->col);
}

/**
 * zynq_edac_handle_error - Handle controller error types CE and UE
 * @mci:	Pointer to the edac memory controller instance
 * @perrstatus:	Pointer to the zynq ecc status structure
 *
 * This routine handles the controller ECC correctable error.
 */
static void zynq_edac_handle_error(struct mem_ctl_info *mci,
			struct zynq_ecc_status *perrstatus)
{
	char message[ZYNQ_EDAC_MESSAGE_SIZE];

	zynq_edac_generate_message(mci, perrstatus, &message[0],
				   ZYNQ_EDAC_MESSAGE_SIZE);

	if (perrstatus->ce_count)
		edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci,
				     perrstatus->ce_count, 0, 0, 0, 0, 0, -1,
				     &message[0], "");
	else
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci,
				     perrstatus->ue_count, 0, 0, 0, 0, 0, -1,
				     &message[0], "");
}

/**
 * zynq_edac_check - Check controller for ECC errors
 * @mci:	Pointer to the edac memory controller instance
 *
 * This routine is used to check and post ECC errors and is called by
 * the EDAC polling thread
 */
static void zynq_edac_check(struct mem_ctl_info *mci)
{
	struct zynq_edac_priv *priv = mci->pvt_info;
	struct zynq_ecc_status errstatus;
	int status;

	status = zynq_edac_geterror_info(priv->baseaddr, &errstatus);
	if (status) {
		priv->ce_count += errstatus.ce_count;
		priv->ue_count += errstatus.ue_count;

		if (errstatus.ce_count) {
			zynq_edac_handle_error(mci, &errstatus);
			errstatus.ce_count = 0;
		}
		if (errstatus.ue_count) {
			zynq_edac_handle_error(mci, &errstatus);
			errstatus.ue_count = 0;
		}
		edac_dbg(3, "total error count ce %d ue %d\n",
			 priv->ce_count, priv->ue_count);
	}
}

/**
 * zynq_edac_get_dtype - Return the controller memory width
 * @base:	Pointer to the ddr memory contoller base address
 *
 * This routine returns the EDAC device type width appropriate for the
 * current controller configuration.
 *
 * Return: a device type width enumeration.
 */
static enum dev_type zynq_edac_get_dtype(void __iomem *base)
{
	enum dev_type dt;
	u32 width;

	width = readl(base + ZYNQ_DDRC_CONTROL_REG_OFFSET);
	width = (width & ZYNQ_DDRC_CTRLREG_BUSWIDTH_MASK) >>
			ZYNQ_DDRC_CTRLREG_BUSWIDTH_SHIFT;

	switch (width) {
	case ZYNQ_DDRCTL_WDTH_16:
		dt = DEV_X2;
		break;
	case ZYNQ_DDRCTL_WDTH_32:
		dt = DEV_X4;
		break;
	default:
		dt = DEV_UNKNOWN;
	}

	return dt;
}

/**
 * zynq_edac_get_eccstate - Return the controller ecc enable/disable status
 * @base:	Pointer to the ddr memory contoller base address
 *
 * This routine returns the ECC enable/diable status for the controller
 *
 * Return: a ecc status boolean i.e true/false - enabled/disabled.
 */
static bool zynq_edac_get_eccstate(void __iomem *base)
{
	enum dev_type dt;
	u32 ecctype;
	bool state = false;

	dt = zynq_edac_get_dtype(base);

	ecctype = (readl(base + ZYNQ_DDRC_ECC_SCRUB_REG_OFFSET) &
			ZYNQ_DDRC_ECC_SCRUBREG_ECC_MODE_MASK);

	if ((ecctype == ZYNQ_DDRC_ECC_SCRUBREG_ECCMODE_SECDED)
			&& (dt == DEV_X2)) {
		state = true;
		writel(0x0, base + ZYNQ_DDRC_ECC_CONTROL_REG_OFFSET);
	} else {
		state = false;
	}

	return state;
}

/**
 * zynq_edac_get_memsize - reads the size of the attached memory device
 *
 * Return: the memory size in bytes
 *
 * This routine returns the size of the system memory by reading the sysinfo
 * information
 */
static u32 zynq_edac_get_memsize(void)
{
	struct sysinfo inf;

	/* Reading the system memory size from the global meminfo structure */
	si_meminfo(&inf);

	return inf.totalram * inf.mem_unit;
}

/**
 * zynq_edac_get_mtype - Returns controller memory type
 * @base:	pointer to the zynq ecc status structure
 *
 * This routine returns the EDAC memory type appropriate for the
 * current controller configuration.
 *
 * Return: a memory type enumeration.
 */
static enum mem_type zynq_edac_get_mtype(void __iomem *base)
{
	enum mem_type mt;
	u32 memtype;

	memtype = readl(base + ZYNQ_DDRC_T_ZQ_REG_OFFSET);

	if (memtype & ZYNQ_DDRC_T_ZQ_REG_DDRMODE_MASK)
		mt = MEM_DDR3;
	else
		mt = MEM_DDR2;

	return mt;
}

/**
 * zynq_edac_init_csrows - Initialize the cs row data
 * @mci:	Pointer to the edac memory controller instance
 *
 * This routine initializes the chip select rows associated
 * with the EDAC memory controller instance
 *
 * Return: 0 if OK; otherwise, -EINVAL if the memory bank size
 * configuration cannot be determined.
 */
static int zynq_edac_init_csrows(struct mem_ctl_info *mci)
{
	struct csrow_info *csi;
	struct dimm_info *dimm;
	struct zynq_edac_priv *priv = mci->pvt_info;
	u32 size;
	int row, j;

	for (row = 0; row < mci->nr_csrows; row++) {
		csi = mci->csrows[row];
		size = zynq_edac_get_memsize();

		for (j = 0; j < csi->nr_channels; j++) {
			dimm = csi->channels[j]->dimm;
			dimm->edac_mode = EDAC_FLAG_SECDED;
			dimm->mtype = zynq_edac_get_mtype(priv->baseaddr);
			dimm->nr_pages =
			    (size >> PAGE_SHIFT) / csi->nr_channels;
			dimm->grain = ZYNQ_EDAC_ERROR_GRAIN;
			dimm->dtype = zynq_edac_get_dtype(priv->baseaddr);
		}

	}

	return 0;
}

/**
 * zynq_edac_mc_init - Initialize driver instance
 * @mci:	Pointer to the edac memory controller instance
 * @pdev:	Pointer to the platform_device struct
 *
 * This routine performs initialization of the EDAC memory controller
 * instance and related driver-private data associated with the
 * memory controller the instance is bound to.
 *
 * Return: 0 if OK; otherwise, < 0 on error.
 */
static int zynq_edac_mc_init(struct mem_ctl_info *mci,
			struct platform_device *pdev)
{
	int status;
	struct zynq_edac_priv *priv;

	/* Initial driver pointers and private data */
	mci->pdev = &pdev->dev;
	priv = mci->pvt_info;
	platform_set_drvdata(pdev, mci);

	/* Initialize controller capabilities and configuration */
	mci->mtype_cap = MEM_FLAG_DDR3 | MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->scrub_cap = SCRUB_HW_SRC;
	/* Check the scrub setting from the controller */
	mci->scrub_mode = SCRUB_NONE;

	mci->edac_cap = EDAC_FLAG_SECDED;
	/* Initialize strings */
	mci->ctl_name = "zynq_ddr_controller";
	mci->dev_name = dev_name(&pdev->dev);
	mci->mod_name = "zynq_edac";
	mci->mod_ver = "1";

	/* Initialize callbacks */
	edac_op_state = EDAC_OPSTATE_POLL;
	mci->edac_check = zynq_edac_check;
	mci->ctl_page_to_phys = NULL;

	/*
	 * Initialize the MC control structure 'csrows' table
	 * with the mapping and control information.
	 */
	status = zynq_edac_init_csrows(mci);
	if (status)
		pr_err("Failed to initialize rows!\n");

	return status;
}

/**
 * zynq_edac_mc_probe - Check controller and bind driver
 * @pdev:	Pointer to the platform_device struct
 *
 * This routine probes a specific controller
 * instance for binding with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int zynq_edac_mc_probe(struct platform_device *pdev)
{
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct zynq_edac_priv *priv;
	int rc;
	struct resource *res;
	void __iomem *baseaddr;

	/* Get the data from the platform device */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	baseaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(baseaddr))
		return PTR_ERR(baseaddr);

	/* Check for the ecc enable status */
	if (zynq_edac_get_eccstate(baseaddr) == false) {
		dev_err(&pdev->dev, "ecc not enabled\n");
		return -ENXIO;
	}

	/*
	 * At this point, we know ECC is enabled, allocate an EDAC
	 * controller instance and perform the appropriate
	 * initialization.
	 */
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = ZYNQ_EDAC_NR_CSROWS;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = ZYNQ_EDAC_NR_CHANS;
	layers[1].is_virt_csrow = false;

	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers,
			    sizeof(struct zynq_edac_priv));
	if (mci == NULL) {
		pr_err("Failed memory allocation for mci instance!\n");
		return -ENOMEM;
	}

	priv = mci->pvt_info;
	priv->baseaddr = baseaddr;
	rc = zynq_edac_mc_init(mci, pdev);
	if (rc) {
		pr_err("Failed to initialize instance!\n");
		goto free_edac_mc;
	}

	/*
	 * We have a valid, initialized EDAC instance bound to the
	 * controller. Attempt to register it with the EDAC subsystem
	 */
	rc = edac_mc_add_mc(mci);
	if (rc) {
		dev_err(&pdev->dev, "failed to register with EDAC core\n");
		goto del_edac_mc;
	}

	return rc;

del_edac_mc:
	edac_mc_del_mc(&pdev->dev);
free_edac_mc:
	edac_mc_free(mci);

	return rc;
}

/**
 * zynq_edac_mc_remove - Unbind driver from controller
 * @pdev:	Pointer to the platform_device struct
 *
 * This routine unbinds the EDAC memory controller instance associated
 * with the specified controller described by the
 * OpenFirmware device tree node passed as a parameter.
 *
 * Return: Unconditionally 0
 */
static int zynq_edac_mc_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

/* Device tree node type and compatible tuples this driver can match on */
static struct of_device_id zynq_edac_match[] = {
	{ .compatible = "xlnx,zynq-ddrc-1.0", },
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, zynq_edac_match);

static struct platform_driver zynq_edac_mc_driver = {
	.driver = {
		   .name = "zynq-edac",
		   .owner = THIS_MODULE,
		   .of_match_table = zynq_edac_match,
		   },
	.probe = zynq_edac_mc_probe,
	.remove = zynq_edac_mc_remove,
};

module_platform_driver(zynq_edac_mc_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Zynq DDR ECC driver");
MODULE_LICENSE("GPL v2");
