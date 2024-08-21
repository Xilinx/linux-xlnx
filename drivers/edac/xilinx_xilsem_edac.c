// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 */

#include <linux/edac.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/bitfield.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/firmware/xlnx-versal-error-events.h>
#include <linux/firmware/xlnx-versal-net-error-events.h>
#include <linux/firmware/xlnx-event-manager.h>

#include "edac_module.h"

#define VERSAL_XILSEM_EDAC_MSG_SIZE	256
#define VERSAL_XILSEM_EDAC_STRNG	"versal_xilsem"
#define EDAC_DEVICE	"Xilsem"

/* XilSem CE Error log count */
#define XILSEM_MAX_CE_LOG_CNT	0x07

/* XilSem_CRAM scan error info registers */
#define CRAM_STS_INFO_OFFSET	0x34
#define CRAM_CE_ADDRL0_OFFSET	0x38
#define CRAM_CE_ADDRH0_OFFSET	0x3C
#define CRAM_CE_COUNT_OFFSET	0x70

/* XilSem_NPI_Scan uncorrectable error info registers */
#define NPI_SCAN_COUNT			0x24
#define NPI_SCAN_HB_COUNT		0x28
#define NPI_ERR0_INFO_OFFSET	0x2C
#define NPI_ERR1_INFO_OFFSET	0x30

/* XilSem bit masks for extracting error details */
#define CRAM_ERR_ROW_MASK	GENMASK(26, 23)
#define CRAM_ERR_BIT_MASK	GENMASK(22, 16)
#define CRAM_ERR_QWRD_MASK	GENMASK(27, 23)
#define CRAM_ERR_FRAME_MASK	GENMASK(22, 0)

enum xsem_cmd_id {
	CRAM_INIT_SCAN = 1, /* To initialize CRAM scan */
	CRAM_START_SCAN = 2, /* To start CRAM scan */
	CRAM_STOP_SCAN = 3, /* To stop CRAM scan */
	CRAM_ERR_INJECT = 4, /* To inject CRAM error */
	NPI_START_SCAN = 5, /* To start NPI scan */
	NPI_STOP_SCAN = 6, /* To stop NPI scan */
	NPI_ERR_INJECT = 7, /* To inject NPI error */
};

/* XilSem Module IDs */
#define CRAM_MOD_ID			0x1
#define NPI_MOD_ID			0x2

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
 * @scan_ctrl_status:	Buffer for scan ctrl commands
 * @cram_errinj_status:	Buffer for CRAM error injection
 * @cram_frame_ecc:	Buffer for CRAM frame ECC
 * @xilsem_status:	Buffer for CRAM & NPI status
 * @sw_event_node_id: Error event node Id
 * @xilsem_cfg:	Buffer for CRAM & NPI configuration
 * @cram_ce_mask: Event bit mask for CRAM correctable error
 * @cram_ue_mask: Event bit mask for CRAM uncorrectable error
 * @npi_ue_mask: Event bit mask for NPI uncorrectable error
 * @ce_cnt:	Correctable Error count
 * @ue_cnt:	Uncorrectable Error count
 */
struct xsem_edac_priv {
	void __iomem *baseaddr;
	u32 scan_ctrl_status[2];
	u32 cram_errinj_status[2];
	u32 cram_frame_ecc[4];
	u32 xilsem_status[4];
	u32 sw_event_node_id;
	u32 xilsem_cfg[4];
	u32 cram_ce_mask;
	u32 cram_ue_mask;
	u32 npi_ue_mask;
	u32 ce_cnt;
	u32 ue_cnt;
};

/**
 * xsem_scan_control_show - Shows scan control operation status
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 *
 * Shows the scan control operations status
 * Return: Number of bytes copied.
 */
static ssize_t xsem_scan_control_show(struct edac_device_ctl_info
										*dci, char *data)
{
	struct xsem_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "[0x%x][0x%x]\n\r",
			priv->scan_ctrl_status[0], priv->scan_ctrl_status[1]);
}

/**
 * xsem_scan_control_store - Set scan control operation
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * User-space interface for doing Xilsem scan operations
 *
 * To control scan operations
 * echo <command> > /sys/devices/system/edac/versal_xilsem/xsem_scan_control
 * Usage:
 * echo 1 > /sys/devices/system/edac/versal_xilsem/xsem_scan_control
 *
 * Set scan control (init, start, stop) operations
 * Return: count argument if request succeeds, else error code
 */
static ssize_t xsem_scan_control_store(struct edac_device_ctl_info *dci,
				       const char *data, size_t count)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	u32 cmd;
	int ret;

	if (!data)
		return -EFAULT;

	if (kstrtouint(data, 0, &cmd))
		return -EINVAL;

	if (cmd < CRAM_INIT_SCAN || cmd > NPI_ERR_INJECT ||
	    cmd == CRAM_ERR_INJECT)
		return -EINVAL;

	ret = zynqmp_pm_xilsem_cntrl_ops(cmd, priv->scan_ctrl_status);
	if (ret)
		return ret;

	return count;
}

/**
 * xsem_cram_injecterr_show - Shows CRAM error injection status
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 *
 * Shows CRAM error injection status
 * Return: Number of bytes copied.
 */
static ssize_t xsem_cram_injecterr_show(struct edac_device_ctl_info *dci, char *data)
{
	struct xsem_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "[0x%x][0x%x]\n\r", priv->cram_errinj_status[0],
				   priv->cram_errinj_status[1]);
}

/**
 * xsem_cram_injecterr_store - Start error injection
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * User-space interface for doing CRAM error injection
 *
 * To inject error
 * echo <frame> <qword> <bit> <row> > /sys/devices/system/edac/versal_xilsem/xsem_cram_injecterr
 * Usage:
 * echo 1 2 2 1 > /sys/devices/system/edac/versal_xilsem/xsem_cram_injecterr
 *
 * Start error injection
 * Return: count argument if request succeeds, else error code
 */
static ssize_t xsem_cram_injecterr_store(struct edac_device_ctl_info *dci,
					 const char *data, size_t count)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	char *kern_buff, *inbuf, *tok;
	u32 row, frame, qword, bitloc;
	int ret;

	kern_buff = kzalloc(count, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	strscpy(kern_buff, data, count);

	inbuf = kern_buff;

	/* Read Frame number */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &frame);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read Qword number */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &qword);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read Bit location */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &bitloc);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read Row number */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &row);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_xilsem_cram_errinj(frame, qword, bitloc, row,
					   priv->cram_errinj_status);
err:
	kfree(kern_buff);

	if (ret)
		return ret;

	return count;
}

/**
 * xsem_cram_framecc_read_show - Shows CRAM Frame ECC
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 *
 * Shows CRAM Frame ECC value
 * Return: Number of bytes copied.
 */
static ssize_t xsem_cram_framecc_read_show(struct edac_device_ctl_info *dci,
					   char *data)
{
	struct xsem_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "[0x%x][0x%x][0x%x][0x%x]\n\r",
		       priv->cram_frame_ecc[0], priv->cram_frame_ecc[1],
		       priv->cram_frame_ecc[2], priv->cram_frame_ecc[3]);
}

/**
 * xsem_cram_framecc_read_store - Read CRAM Frame ECC
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * User-space interface for reading CRAM frame ECC
 *
 * To read ecc
 * echo <frame> <row> > /sys/devices/system/edac/versal_xilsem/xsem_cram_framecc_read
 * Usage:
 * echo 1 2 > /sys/devices/system/edac/versal_xilsem/xsem_cram_framecc_read
 *
 * Read CRAM Frame ECC
 * Return: count argument if request succeeds, else error code
 */
static ssize_t xsem_cram_framecc_read_store(struct edac_device_ctl_info *dci,
					    const char *data, size_t count)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	char *kern_buff, *inbuf, *tok;
	u32 frameaddr, row;
	int ret;

	kern_buff = kzalloc(count, GFP_KERNEL);
	if (!kern_buff)
		return -ENOMEM;

	strscpy(kern_buff, data, count);

	inbuf = kern_buff;

	/* Read Frame address */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &frameaddr);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	/* Read Row number */
	tok = strsep(&inbuf, " ");
	if (!tok) {
		ret = -EFAULT;
		goto err;
	}

	ret = kstrtouint(tok, 0, &row);
	if (ret) {
		ret = -EFAULT;
		goto err;
	}

	ret = zynqmp_pm_xilsem_cram_readecc(frameaddr, row, priv->cram_frame_ecc);
err:
	kfree(kern_buff);

	if (ret)
		return ret;

	return count;
}

/**
 * xsem_read_status_show - Shows CRAM & NPI scan status
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 *
 * Shows CRAM & NPI scan status
 * Return: Number of bytes copied.
 */
static ssize_t xsem_read_status_show(struct edac_device_ctl_info *dci, char *data)
{
	struct xsem_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "[0x%x][0x%x][0x%x]\n\r",
			priv->xilsem_status[0], priv->xilsem_status[1],
			priv->xilsem_status[2]);
}

/**
 * xsem_read_status_store - Read CRAM & NPI scan status
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * User-space interface for reading Xilsem status
 *
 * To read status
 * echo <module> > /sys/devices/system/edac/versal_xilsem/xsem_read_status
 * Usage:
 * echo 1 > /sys/devices/system/edac/versal_xilsem/xsem_read_status
 *
 * Read CRAM & NPI scan status
 * Return: count argument if rea succeeds, else error code
 */
static ssize_t xsem_read_status_store(struct edac_device_ctl_info *dci,
				      const char *data, size_t count)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	u32 module;

	if (!data)
		return -EFAULT;

	if (kstrtouint(data, 0, &module))
		return -EINVAL;

	if (module == CRAM_MOD_ID) {
		priv->xilsem_status[0] = readl(priv->baseaddr + CRAM_STS_INFO_OFFSET);
		priv->xilsem_status[1] = readl(priv->baseaddr + CRAM_CE_COUNT_OFFSET);
		priv->xilsem_status[2] = 0;
	} else if (module == NPI_MOD_ID) {
		priv->xilsem_status[0] = readl(priv->baseaddr);
		priv->xilsem_status[1] = readl(priv->baseaddr + NPI_SCAN_COUNT);
		priv->xilsem_status[2] = readl(priv->baseaddr + NPI_SCAN_HB_COUNT);
	} else {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Invalid module %d\n", module);
		return -EINVAL;
	}

	return count;
}

/**
 * xsem_read_config_show - Shows CRAM & NPI configuration
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 *
 * Shows CRAM & NPI configuration
 * Return: Number of bytes copied.
 */
static ssize_t xsem_read_config_show(struct edac_device_ctl_info *dci, char *data)
{
	struct xsem_edac_priv *priv = dci->pvt_info;

	return sprintf(data, "[0x%x][0x%x][0x%x][0x%x]\n\r",
		       priv->xilsem_cfg[0], priv->xilsem_cfg[1],
		       priv->xilsem_cfg[2], priv->xilsem_cfg[3]);
}

/**
 * xsem_read_config_store - Read CRAM & NPI configuration
 * @dci:	Pointer to the edac device struct
 * @data:	Pointer to user data
 * @count:	read the size bytes from buffer
 *
 * User-space interface for reading Xilsem configuration
 *
 * To read configuration
 * echo 1 > /sys/devices/system/edac/versal_xilsem/xsem_read_config
 * Usage:
 * echo 1 > /sys/devices/system/edac/versal_xilsem/xsem_read_config
 *
 * Read CRAM & NPI configuration
 * Return: count argument if request succeeds, else error code
 */
static ssize_t xsem_read_config_store(struct edac_device_ctl_info *dci,
				      const char *data, size_t count)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	int ret;

	if (!data)
		return -EFAULT;

	ret = zynqmp_pm_xilsem_read_cfg(priv->xilsem_cfg);

	if (ret)
		return ret;

	return count;
}

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
 * @dci:	Pointer to the edac device controller instance
 * @p:		Pointer to the Xilsem error status structure
 * @mask:	mask indictaes the error type
 *
 * Determines there is any ecc error or not
 */
static void xsem_geterror_info(struct edac_device_ctl_info *dci, struct xsem_error_status *p,
			       int mask)
{
	struct xsem_edac_priv *priv = dci->pvt_info;
	u32 error_word_0, error_word_1, ce_count;
	u8 index;

	if (mask & priv->cram_ce_mask) {
		p->ce_cnt++;

		/* Read CRAM total correctable error count */
		ce_count = readl(priv->baseaddr + CRAM_CE_COUNT_OFFSET);
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
		error_word_0 = readl(priv->baseaddr + CRAM_CE_ADDRL0_OFFSET + (index * 8U));
		error_word_1 = readl(priv->baseaddr + CRAM_CE_ADDRH0_OFFSET + (index * 8U));

		/* Frame is at 22:0 bits of SEM_CRAMERR_ADDRH0 reg */
		p->ceinfo.frame_addr = FIELD_GET(CRAM_ERR_FRAME_MASK, error_word_1);

		/* row is at 26:23 bits of SEM_CRAMERR_ADDRH0 reg */
		p->ceinfo.row_id = FIELD_GET(CRAM_ERR_ROW_MASK, error_word_1);

		/* bit is at 22:16 bits of SEM_CRAMERR_ADDRL0 reg */
		p->ceinfo.bit_loc = FIELD_GET(CRAM_ERR_BIT_MASK, error_word_0);

		/* Qword is at 27:23 bits of SEM_CRAMERR_ADDRL0 reg */
		p->ceinfo.qword = FIELD_GET(CRAM_ERR_QWRD_MASK, error_word_0);

		/* Read CRAM status */
		p->ceinfo.status = readl(priv->baseaddr + CRAM_STS_INFO_OFFSET);
	} else if (mask & priv->cram_ue_mask) {
		p->ue_cnt++;
		p->ueinfo.data0 = 0;
		p->ueinfo.data1 = 0;
		p->ueinfo.status = readl(priv->baseaddr + CRAM_STS_INFO_OFFSET);
	} else if (mask & priv->npi_ue_mask) {
		p->ue_cnt++;
		p->ueinfo.data0 = readl(priv->baseaddr + NPI_ERR0_INFO_OFFSET);
		p->ueinfo.data1 = readl(priv->baseaddr + NPI_ERR1_INFO_OFFSET);
		p->ueinfo.status = readl(priv->baseaddr);
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
	xsem_geterror_info(dci, &stat, event);

	priv->ce_cnt += stat.ce_cnt;
	priv->ue_cnt += stat.ue_cnt;
	xsem_handle_error(dci, &stat);
}

static struct edac_dev_sysfs_attribute xsem_edac_sysfs_attributes[] = {
	{
		.attr = {
			.name = "xsem_scan_control_ops",
			.mode = (0644)
		},
		.show = xsem_scan_control_show,
		.store = xsem_scan_control_store},
	{
		.attr = {
			.name = "xsem_cram_injecterr",
			.mode = (0644)
		},
		.show = xsem_cram_injecterr_show,
		.store = xsem_cram_injecterr_store},
	{
		.attr = {
			.name = "xsem_cram_framecc_read",
			.mode = (0644)
		},
		.show = xsem_cram_framecc_read_show,
		.store = xsem_cram_framecc_read_store},
	{
		.attr = {
			.name = "xsem_read_status",
			.mode = (0644)
		},
		.show = xsem_read_status_show,
		.store = xsem_read_status_store},
	{
		.attr = {
			.name = "xsem_read_config",
			.mode = (0644)
		},
		.show = xsem_read_config_show,
		.store = xsem_read_config_store},
	{
		.attr = {.name = NULL}
	}
};

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
	u32 device_sub_family_code;
	u32 family_code;
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

	dci->sysfs_attributes = xsem_edac_sysfs_attributes;
	rc = edac_device_add_device(dci);
	if (rc)
		goto free_dev_ctl;

	rc = zynqmp_pm_get_family_info(&family_code, &device_sub_family_code);
	if (rc) {
		if (rc == -ENODEV)
			rc = -EPROBE_DEFER;
		goto free_edac_dev;
	}

	if (device_sub_family_code == VERSALNET_SUB_FAMILY_CODE) {
		priv->sw_event_node_id = VERSAL_NET_EVENT_ERROR_SW_ERR;
		priv->cram_ce_mask = XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_CE;
		priv->cram_ue_mask = XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_CRAM_UE;
		priv->npi_ue_mask = XPM_VERSAL_NET_EVENT_ERROR_MASK_XSEM_NPI_UE;
	} else if (device_sub_family_code == VERSAL_SUB_FAMILY_CODE) {
		priv->sw_event_node_id = VERSAL_EVENT_ERROR_SW_ERR;
		priv->cram_ce_mask = XPM_VERSAL_EVENT_ERROR_MASK_XSEM_CRAM_CE_5;
		priv->cram_ue_mask = XPM_VERSAL_EVENT_ERROR_MASK_XSEM_CRAM_UE_6;
		priv->npi_ue_mask = XPM_VERSAL_EVENT_ERROR_MASK_XSEM_NPI_UE_7;
	} else {
		edac_printk(KERN_ERR, EDAC_DEVICE, "Invalid Device Sub family code %d\n",
			    device_sub_family_code);
	}
	rc = xlnx_register_event(PM_NOTIFY_CB, priv->sw_event_node_id,
				 priv->cram_ce_mask | priv->cram_ue_mask | priv->npi_ue_mask,
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
	struct xsem_edac_priv *priv = dci->pvt_info;

	xlnx_unregister_event(PM_NOTIFY_CB, priv->sw_event_node_id,
			      priv->cram_ce_mask | priv->cram_ue_mask | priv->npi_ue_mask,
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
