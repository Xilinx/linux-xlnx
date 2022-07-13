// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/firmware/xlnx-error-events.h>
#include <linux/firmware/xlnx-event-manager.h>

#include "edac_module.h"

#define VERSAL_XILSEM_EDAC_MSG_SIZE	256
#define VERSAL_XILSEM_EDAC_STRNG	"versal_xilsem"
#define EDAC_DEVICE	"Xilsem"

/* XilSem Error type masks */
#define XILSEM_CRAM_CE_MASK	BIT(5)
#define XILSEM_CRAM_UE_MASK	BIT(6)
#define XILSEM_NPI_UE_MASK	BIT(7)
#define XILSEM_MAX_CE_LOG_CNT	0x07

/* XilSem_CRAM scan error info registers */
#define CRAM_STS_INFO_OFFSET	0x34
#define CRAM_CE_ADDRL0_OFFSET	0x38
#define CRAM_CE_ADDRH0_OFFSET	0x3C
#define CRAM_CE_COUNT_OFFSET	0x70

/* XilSem_NPI_Scan uncorrectable error info registers */
#define NPI_ERR0_INFO_OFFSET	0x2C
#define NPI_ERR1_INFO_OFFSET	0x30

/* XilSem bit masks for extracting error details */
#define CRAM_ERR_ROW_MASK	GENMASK(26, 23)
#define CRAM_ERR_BIT_MASK	GENMASK(22, 16)
#define CRAM_ERR_QWRD_MASK	GENMASK(27, 23)
#define CRAM_ERR_FRAME_MASK	GENMASK(22, 0)

/**
 * struct ecc_error_info - ECC error log information
 * @status:	CRAM/NPI scan error status
 * @data0:	Checksum of the error descriptor
 * @data1:	Index of the error descriptor
 * @frame_addr:	Frame location at which error occurred
 * @block_type:	Block type
 * @row_id:	Row number
 * @bit_loc:	Bit position in the Qword
 * @qword:	Qword location in the frame
 */
struct ecc_error_info {
	u32 status;
	u32 data0;
	u32 data1;
	u32 frame_addr;
	u8 block_type;
	u8 row_id;
	u8 bit_loc;
	u8 qword;
};

/**
 * struct xsem_error_status - ECC status information to report
 * @ce_cnt:	Correctable error count
 * @ue_cnt:	Uncorrectable error count
 * @ceinfo:	Correctable error log information
 * @ueinfo:	Uncorrectable error log information
 */
struct xsem_error_status {
	u32 ce_cnt;
	u32 ue_cnt;
	struct ecc_error_info ceinfo;
	struct ecc_error_info ueinfo;
};

/**
 * struct xsem_edac_priv - Xilsem private instance data
 * @baseaddr:	Base address of the XilSem PLM RTCA module
 * @message:	Buffer for framing the event specific info
 * @stat:	ECC status information
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 */
struct xsem_edac_priv {
	void __iomem *baseaddr;
	u32 ce_cnt;
	u32 ue_cnt;
};

/**
 * xsem_handle_error - Handle XilSem error types CE and UE
 * @dci:	Pointer to the edac device controller instance
 * @p:		Pointer to the xilsem error status structure
 *
 * Handles the correctable and uncorrectable error.
 */
static void xsem_handle_error(struct edac_device_ctl_info *dci, struct xsem_error_status *p)
{
	struct ecc_error_info *pinf;
	char message[VERSAL_XILSEM_EDAC_MSG_SIZE];

	if (p->ce_cnt) {
		pinf = &p->ceinfo;
		snprintf(message, VERSAL_XILSEM_EDAC_MSG_SIZE,
			 "\n\rXILSEM CRAM error type :%s\n\r"
			 "\nFrame_Addr: [0x%X]\t Row_num: [0x%X]\t Bit_loc: [0x%X]\t Qword: [0x%X]\n\r",
			 "CE", pinf->frame_addr, pinf->row_id,
			 pinf->bit_loc, pinf->qword);
		edac_device_handle_ce(dci, 0, 0, message);
	}

	if (p->ue_cnt) {
		pinf = &p->ueinfo;
		snprintf(message, VERSAL_XILSEM_EDAC_MSG_SIZE,
			 "\n\rXILSEM error type :%s\n\r"
			 "status: [0x%X]\n\rError_Info0: [0x%X]\n\r"
			 "Error_Info1: [0x%X]",
			 "UE", pinf->status, pinf->data0, pinf->data1);
		edac_device_handle_ue(dci, 0, 0, message);
	}
}

/**
 * xsem_geterror_info - Get the current ecc error info
 * @base:	Pointer to the base address of the PLM RTCA memory
 * @p:		Pointer to the Xilsem error status structure
 * @mask:	mask indictaes the error type
 *
 * Determines there is any ecc error or not
 */
static void xsem_geterror_info(void __iomem *base, struct xsem_error_status *p, int mask)
{
	u32 error_word_0, error_word_1, ce_count;
	u8 index;

	if (mask & XILSEM_CRAM_CE_MASK) {
		p->ce_cnt++;

		/* Read CRAM total correctable error count */
		ce_count = readl(base + CRAM_CE_COUNT_OFFSET);
		/* Calculate index for error log */
		index = (ce_count % XILSEM_MAX_CE_LOG_CNT);
		/*
		 * Check if addr index is not 0
		 * if yes, then decrement index, else set index as last entry
		 */
		if (index != 0U) {
			/* Decrement Index */
			--index;
		} else {
			/* Set log index to 6 (Max-1) */
			index = (XILSEM_MAX_CE_LOG_CNT - 1);
		}
		error_word_0 = readl(base + CRAM_CE_ADDRL0_OFFSET + (index * 8U));
		error_word_1 = readl(base + CRAM_CE_ADDRH0_OFFSET + (index * 8U));

		/* Frame is at 22:0 bits of SEM_CRAMERR_ADDRH0 reg */
		p->ceinfo.frame_addr = FIELD_GET(CRAM_ERR_FRAME_MASK, error_word_1);

		/* row is at 26:23 bits of SEM_CRAMERR_ADDRH0 reg */
		p->ceinfo.row_id = FIELD_GET(CRAM_ERR_ROW_MASK, error_word_1);

		/* bit is at 22:16 bits of SEM_CRAMERR_ADDRL0 reg */
		p->ceinfo.bit_loc = FIELD_GET(CRAM_ERR_BIT_MASK, error_word_0);

		/* Qword is at 27:23 bits of SEM_CRAMERR_ADDRL0 reg */
		p->ceinfo.qword = FIELD_GET(CRAM_ERR_QWRD_MASK, error_word_0);

		/* Read CRAM status */
		p->ceinfo.status = readl(base + CRAM_STS_INFO_OFFSET);
	} else if (mask & XILSEM_CRAM_UE_MASK) {
		p->ue_cnt++;
		p->ueinfo.data0 = 0;
		p->ueinfo.data1 = 0;
		p->ueinfo.status = readl(base + CRAM_STS_INFO_OFFSET);
	} else if (mask & XILSEM_NPI_UE_MASK) {
		p->ue_cnt++;
		p->ueinfo.data0 = readl(base + NPI_ERR0_INFO_OFFSET);
		p->ueinfo.data1 = readl(base + NPI_ERR1_INFO_OFFSET);
		p->ueinfo.status = readl(base);
	} else {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Invalid Event received %d\n", mask);
	}
}

/**
 * xsem_err_callback - Handle Correctable and Uncorrectable errors.
 * @payload:	payload data.
 * @data:	controller data.
 *
 * Handles ECC correctable and uncorrectable errors.
 */
static void xsem_err_callback(const u32 *payload, void *data)
{
	struct edac_device_ctl_info *dci = (struct edac_device_ctl_info *)data;
	struct xsem_error_status stat;
	struct xsem_edac_priv *priv;
	int event;

	priv = dci->pvt_info;
	memset(&stat, 0, sizeof(stat));
	/* Read payload to get the event type */
	event = payload[2];
	edac_printk(KERN_INFO, EDAC_DEVICE, "Event received %x\n", event);
	xsem_geterror_info(priv->baseaddr, &stat, event);

	priv->ce_cnt += stat.ce_cnt;
	priv->ue_cnt += stat.ue_cnt;
	xsem_handle_error(dci, &stat);
}

/**
 * xsem_edac_probe - Check controller and bind driver.
 * @pdev:	platform device.
 *
 * Probe a specific controller instance for binding with the driver.
 *
 * Return: 0 if the controller instance was successfully bound to the
 * driver; otherwise, < 0 on error.
 */
static int xsem_edac_probe(struct platform_device *pdev)
{
	struct xsem_edac_priv *priv;
	void __iomem *plmrtca_baseaddr;
	struct edac_device_ctl_info *dci;
	int rc;

	plmrtca_baseaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(plmrtca_baseaddr))
		return PTR_ERR(plmrtca_baseaddr);

	dci = edac_device_alloc_ctl_info(sizeof(*priv), VERSAL_XILSEM_EDAC_STRNG,
					 1, VERSAL_XILSEM_EDAC_STRNG, 1, 0, NULL, 0,
					 edac_device_alloc_index());
	if (!dci) {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Unable to allocate EDAC device\n");
		return -ENOMEM;
	}

	priv = dci->pvt_info;
	platform_set_drvdata(pdev, dci);
	dci->dev = &pdev->dev;
	priv->baseaddr = plmrtca_baseaddr;
	dci->mod_name = pdev->dev.driver->name;
	dci->ctl_name = VERSAL_XILSEM_EDAC_STRNG;
	dci->dev_name = dev_name(&pdev->dev);

	rc = edac_device_add_device(dci);
	if (rc)
		goto free_dev_ctl;

	rc = xlnx_register_event(PM_NOTIFY_CB,
				 XPM_NODETYPE_EVENT_ERROR_SW_ERR,
				 XPM_EVENT_ERROR_MASK_XSEM_CRAM_CE_5 |
				 XPM_EVENT_ERROR_MASK_XSEM_CRAM_UE_6 |
				 XPM_EVENT_ERROR_MASK_XSEM_NPI_UE_7,
				 false, xsem_err_callback, dci);
	if (rc) {
		if (rc == -EACCES)
			rc = -EPROBE_DEFER;
		goto free_edac_dev;
	}

	edac_printk(KERN_DEBUG, EDAC_DEVICE, "%s success\n", __func__);
	return rc;

free_edac_dev:
	edac_device_del_device(&pdev->dev);
free_dev_ctl:
	edac_device_free_ctl_info(dci);

	return rc;
}

/**
 * xsem_edac_remove - Unbind driver from controller.
 * @pdev:	Platform device.
 *
 * Return: Unconditionally 0
 */
static int xsem_edac_remove(struct platform_device *pdev)
{
	struct edac_device_ctl_info *dci = platform_get_drvdata(pdev);

	xlnx_unregister_event(PM_NOTIFY_CB, XPM_NODETYPE_EVENT_ERROR_SW_ERR,
			      XPM_EVENT_ERROR_MASK_XSEM_CRAM_CE_5 |
			      XPM_EVENT_ERROR_MASK_XSEM_CRAM_UE_6 |
			      XPM_EVENT_ERROR_MASK_XSEM_NPI_UE_7,
			      xsem_err_callback, dci);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(dci);

	return 0;
}

static const struct of_device_id xlnx_xsem_edac_match[] = {
	{ .compatible = "xlnx,versal-xilsem-edac", },
	{
		/* end of table */
	}
};

MODULE_DEVICE_TABLE(of, xlnx_xsem_edac_match);

static struct platform_driver xilinx_xsem_edac_driver = {
	.driver = {
		.name = "xilinx-xilsem-edac",
		.of_match_table = xlnx_xsem_edac_match,
	},
	.probe = xsem_edac_probe,
	.remove = xsem_edac_remove,
};

module_platform_driver(xilinx_xsem_edac_driver);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("Xilinx XilSEM driver");
MODULE_LICENSE("GPL");
