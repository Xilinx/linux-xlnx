/*
 * Xilinx VPHY driver
 *
 * The Video Phy is a high-level wrapper around the GT to configure it
 * for video applications. The driver also provides common functionality
 * for its tightly-bound video protocol drivers such as HDMI RX/TX.
 *
 * Copyright (C) 2016, 2017 Leon Woestenberg <leon@sidebranch.com>
 * Copyright (C) 2014, 2015, 2017 Xilinx, Inc.
 *
 * Authors: Leon Woestenberg <leon@sidebranch.com>
 *          Rohit Consul <rohitco@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/* if both both DEBUG and DEBUG_TRACE are defined, trace_printk() is used */
//#define DEBUG
//#define DEBUG_TRACE

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <dt-bindings/phy/phy.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#include "linux/phy/phy-vphy.h"

/* baseline driver includes */
#include "phy-xilinx-vphy/xvphy.h"
#include "phy-xilinx-vphy/xvphy_i.h"

/* common RX/TX */
#include "phy-xilinx-vphy/xdebug.h"
#include "phy-xilinx-vphy/xvidc.h"
#include "phy-xilinx-vphy/xvidc_edid.h"
#include "phy-xilinx-vphy/xtmrctr.h"
#include "phy-xilinx-vphy/xhdcp1x.h"
#include "phy-xilinx-vphy/xhdcp22_rx.h"
#include "phy-xilinx-vphy/xhdcp22_tx.h"
#include "phy-xilinx-vphy/bigdigits.h"
#include "phy-xilinx-vphy/xhdcp22_cipher.h"
#include "phy-xilinx-vphy/xhdcp22_mmult.h"
#include "phy-xilinx-vphy/xhdcp22_rng.h"
#include "phy-xilinx-vphy/xhdcp22_common.h"
#include "phy-xilinx-vphy/aes256.h"

#define XVPHY_DRU_REF_CLK_HZ	156250000

/* select either trace or printk logging */
#ifdef DEBUG_TRACE
#define do_hdmi_dbg(format, ...) do { \
  trace_printk("xlnx-hdmi-vphy: " format, ##__VA_ARGS__); \
} while(0)
#else
#define do_hdmi_dbg(format, ...) do { \
  printk(KERN_DEBUG "xlnx-hdmi-vphy: " format, ##__VA_ARGS__); \
} while(0)
#endif

/* either enable or disable debugging */
#ifdef DEBUG
#  define hdmi_dbg(x...) do_hdmi_dbg(x)
#else
#  define hdmi_dbg(x...)
#endif

#define hdmi_mutex_lock(x) mutex_lock(x)
#define hdmi_mutex_unlock(x) mutex_unlock(x)

/**
 * struct xvphy_lane - representation of a lane
 * @phy: pointer to the kernel PHY device
 *
 * @type: controller which uses this lane
 * @lane: lane number
 * @protocol: protocol in which the lane operates
 * @ref_clk: enum of allowed ref clock rates for this lane PLL
 * @pll_lock: PLL status
 * @data: pointer to hold private data
 * @direction: 0=rx, 1=tx
 * @share_laneclk: lane number of the clock to be shared
 */
struct xvphy_lane {
	struct phy *phy;
	u8 type;
	u8 lane;
	u8 protocol;
	bool pll_lock;
	/* data is pointer to parent xvphy_dev */
	void *data;
	bool direction_tx;
	u32 share_laneclk;
};

/**
 * struct xvphy_dev - representation of a Xilinx Video PHY
 * @dev: pointer to device
 * @iomem: serdes base address
 */
struct xvphy_dev {
	struct device *dev;
	/* virtual remapped I/O memory */
	void __iomem *iomem;
	int irq;
	/* protects the XVphy baseline against concurrent access */
	struct mutex xvphy_mutex;
	struct xvphy_lane *lanes[4];
	/* bookkeeping for the baseline subsystem driver instance */
	XVphy xvphy;
	/* AXI Lite clock drives the clock detector */
	struct clk *axi_lite_clk;
	/* NI-DRU clock input */
	struct clk *clkp;
};

/* given the (Linux) phy handle, return the xvphy */
XVphy *xvphy_get_xvphy(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	return &vphy_dev->xvphy;
}
EXPORT_SYMBOL_GPL(xvphy_get_xvphy);

/* given the (Linux) phy handle, enter critical section of xvphy baseline code
 * XVphy functions must be called with mutex acquired to prevent concurrent access
 * by XVphy and upper-layer video protocol drivers */
void xvphy_mutex_lock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_lock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_lock);

void xvphy_mutex_unlock(struct phy *phy)
{
	struct xvphy_lane *vphy_lane = phy_get_drvdata(phy);
	struct xvphy_dev *vphy_dev = vphy_lane->data;
	hdmi_mutex_unlock(&vphy_dev->xvphy_mutex);
}
EXPORT_SYMBOL_GPL(xvphy_mutex_unlock);

/* XVphy functions must be called with mutex acquired to prevent concurrent access
 * by XVphy and upper-layer video protocol drivers */
EXPORT_SYMBOL_GPL(XVphy_GetPllType);
EXPORT_SYMBOL_GPL(XVphy_IBufDsEnable);
EXPORT_SYMBOL_GPL(XVphy_SetHdmiCallback);
EXPORT_SYMBOL_GPL(XVphy_HdmiCfgCalcMmcmParam);
EXPORT_SYMBOL_GPL(XVphy_MmcmStart);

/* exclusively required by TX */
EXPORT_SYMBOL_GPL(XVphy_Clkout1OBufTdsEnable);
EXPORT_SYMBOL_GPL(XVphy_SetHdmiTxParam);
EXPORT_SYMBOL_GPL(XVphy_IsBonded);

static irqreturn_t xvphy_irq_handler(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;
	BUG_ON(!dev_id);
	vphydev = (struct xvphy_dev *)dev_id;
	BUG_ON(!vphydev);
	if (!vphydev)
		return IRQ_NONE;

	/* disable interrupts in the VPHY, they are re-enabled once serviced */
	XVphy_IntrDisable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
			XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
			XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
			XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
			XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
			XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
			XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t xvphy_irq_thread(int irq, void *dev_id)
{
	struct xvphy_dev *vphydev;
	u32 IntrStatus;
	BUG_ON(!dev_id);
	vphydev = (struct xvphy_dev *)dev_id;
	BUG_ON(!vphydev);
	if (!vphydev)
		return IRQ_NONE;

	/* call baremetal interrupt handler with mutex locked */
	hdmi_mutex_lock(&vphydev->xvphy_mutex);

	IntrStatus = XVphy_ReadReg(vphydev->xvphy.Config.BaseAddr, XVPHY_INTR_STS_REG);
	hdmi_dbg("XVphy IntrStatus = 0x%08x\n", IntrStatus);

	/* handle pending interrupts */
	XVphy_InterruptHandler(&vphydev->xvphy);
	hdmi_mutex_unlock(&vphydev->xvphy_mutex);

	/* enable interrupt requesting in the VPHY */
	XVphy_IntrEnable(&vphydev->xvphy, XVPHY_INTR_HANDLER_TYPE_TXRESET_DONE |
		XVPHY_INTR_HANDLER_TYPE_RXRESET_DONE |
		XVPHY_INTR_HANDLER_TYPE_CPLL_LOCK |
		XVPHY_INTR_HANDLER_TYPE_QPLL0_LOCK |
		XVPHY_INTR_HANDLER_TYPE_TXALIGN_DONE |
		XVPHY_INTR_HANDLER_TYPE_QPLL1_LOCK |
		XVPHY_INTR_HANDLER_TYPE_TX_CLKDET_FREQ_CHANGE |
		XVPHY_INTR_HANDLER_TYPE_RX_CLKDET_FREQ_CHANGE |
		XVPHY_INTR_HANDLER_TYPE_TX_TMR_TIMEOUT |
		XVPHY_INTR_HANDLER_TYPE_RX_TMR_TIMEOUT);

	return IRQ_HANDLED;
}

/**
 * xvphy_phy_init - initializes a lane
 * @phy: pointer to kernel PHY device
 *
 * Return: 0 on success or error on failure
 */
static int xvphy_phy_init(struct phy *phy)
{
	BUG_ON(!phy);
	printk(KERN_INFO "xvphy_phy_init(%p).\n", phy);

	return 0;
}

/**
 * xvphy_xlate - provides a PHY specific to a controller
 * @dev: pointer to device
 * @args: arguments from dts
 *
 * Return: pointer to kernel PHY device or error on failure
 *
 */
static struct phy *xvphy_xlate(struct device *dev,
				   struct of_phandle_args *args)
{
	struct xvphy_dev *vphydev = dev_get_drvdata(dev);
	struct xvphy_lane *vphy_lane = NULL;
	struct device_node *phynode = args->np;
	int index;
	u8 controller;
	u8 instance_num;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}
	if (!of_device_is_available(phynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}
	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (phynode == vphydev->lanes[index]->phy->dev.of_node) {
			vphy_lane = vphydev->lanes[index];
			break;
		}
	}
	if (!vphy_lane) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	/* get type of controller from lanes */
	controller = args->args[0];

	/* get controller instance number */
	instance_num = args->args[1];

	/* Check if lane sharing is required */
	vphy_lane->share_laneclk = args->args[2];

	/* get the direction for controller from lanes */
	vphy_lane->direction_tx = args->args[3];

	BUG_ON(!vphy_lane->phy);
	return vphy_lane->phy;
}

/* Local Global table for phy instance(s) configuration settings */
XVphy_Config XVphy_ConfigTable[XPAR_XVPHY_NUM_INSTANCES];

static struct phy_ops xvphy_phyops = {
	.init		= xvphy_phy_init,
	.owner		= THIS_MODULE,
};

static int instance = 0;
/* TX uses [1, 127], RX uses [128, 254] and VPHY uses [256, ...]. Note that 255 is used for not-present. */
#define VPHY_DEVICE_ID_BASE 256

static int vphy_parse_of(struct xvphy_dev *vphydev, XVphy_Config *c)
{
	struct device *dev = vphydev->dev;
	struct device_node *node = dev->of_node;
	int rc;
	u32 val;
	bool has_err_irq;

	rc = of_property_read_u32(node, "xlnx,transceiver-type", &val);
	if (rc < 0)
		goto error_dt;
	c->XcvrType = val;

	rc = of_property_read_u32(node, "xlnx,tx-buffer-bypass", &val);
	if (rc < 0)
		goto error_dt;
	c->TxBufferBypass = val;

	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0)
		goto error_dt;
	c->Ppc = val;

	rc = of_property_read_u32(node, "xlnx,nidru", &val);
	if (rc < 0)
		goto error_dt;
	c->DruIsPresent = val;

	rc = of_property_read_u32(node, "xlnx,nidru-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->DruRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	c->RxChannels = val;

	rc = of_property_read_u32(node, "xlnx,tx-no-of-channels", &val);
	if (rc < 0)
		goto error_dt;
	c->TxChannels = val;

	rc = of_property_read_u32(node, "xlnx,rx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	c->RxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,tx-protocol", &val);
	if (rc < 0)
		goto error_dt;
	c->TxProtocol = val;

	rc = of_property_read_u32(node, "xlnx,rx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->RxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-refclk-sel", &val);
	if (rc < 0)
		goto error_dt;
	c->TxRefClkSel = val;

	rc = of_property_read_u32(node, "xlnx,rx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	c->RxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,tx-pll-selection", &val);
	if (rc < 0)
		goto error_dt;
	c->TxSysPllClkSel = val;

	rc = of_property_read_u32(node, "xlnx,hdmi-fast-switch", &val);
	if (rc < 0)
		goto error_dt;
	c->HdmiFastSwitch = val;

	rc = of_property_read_u32(node, "xlnx,transceiver-width", &val);
	if (rc < 0)
		goto error_dt;
	c->TransceiverWidth = val;

	has_err_irq = false;
	has_err_irq = of_property_read_bool(node, "xlnx,err-irq-en");
	c->ErrIrq = has_err_irq;
	return 0;

error_dt:
	dev_err(vphydev->dev, "Error parsing device tree");
	return -EINVAL;
}

/**
 * xvphy_probe - The device probe function for driver initialization.
 * @pdev: pointer to the platform device structure.
 *
 * Return: 0 for success and error value on failure
 */
static int xvphy_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct xvphy_dev *vphydev;
	struct phy_provider *provider;
	struct phy *phy;
	unsigned long axi_lite_rate;
	unsigned long dru_clk_rate;

	struct resource *res;
	int port = 0, index = 0;
	int ret;
	u32 Status;
	u32 Data;
	u16 DrpVal;

	dev_info(&pdev->dev, "xlnx-hdmi-vphy: probed\n");
	vphydev = devm_kzalloc(&pdev->dev, sizeof(*vphydev), GFP_KERNEL);
	if (!vphydev)
		return -ENOMEM;

	/* mutex that protects against concurrent access */
	mutex_init(&vphydev->xvphy_mutex);

	vphydev->dev = &pdev->dev;
	/* set a pointer to our driver data */
	platform_set_drvdata(pdev, vphydev);

	BUG_ON(!np);

	XVphy_ConfigTable[instance].DeviceId = VPHY_DEVICE_ID_BASE + instance;

	hdmi_dbg("xvphy_probe DT parse start\n");
	ret = vphy_parse_of(vphydev, &XVphy_ConfigTable[instance]);
	if (ret) return ret;
	hdmi_dbg("xvphy_probe DT parse done\n");

	for_each_child_of_node(np, child) {
		struct xvphy_lane *vphy_lane;

		vphy_lane = devm_kzalloc(&pdev->dev, sizeof(*vphy_lane),
					 GFP_KERNEL);
		if (!vphy_lane)
			return -ENOMEM;

		/* Assign lane number to gtr_phy instance */
		vphy_lane->lane = index;

		/* Disable lane sharing as default */
		vphy_lane->share_laneclk = -1;

		BUG_ON(port >= 4);
		/* array of pointer to vphy_lane structs */
		vphydev->lanes[port] = vphy_lane;

		/* create phy device for each lane */
		phy = devm_phy_create(&pdev->dev, child, &xvphy_phyops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret == -EPROBE_DEFER)
				dev_info(&pdev->dev, "xvphy probe deferred\n");
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "failed to create PHY\n");
			return ret;
		}
		/* array of pointer to phy */
		vphydev->lanes[port]->phy = phy;
		/* where each phy device has vphy_lane as driver data */
		phy_set_drvdata(phy, vphydev->lanes[port]);
		/* and each vphy_lane points back to parent device */
		vphy_lane->data = vphydev;
		port++;
		index++;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vphydev->iomem = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vphydev->iomem))
		return PTR_ERR(vphydev->iomem);

	/* set address in configuration data */
	XVphy_ConfigTable[instance].BaseAddr = (uintptr_t)vphydev->iomem;

	vphydev->irq = platform_get_irq(pdev, 0);
	if (vphydev->irq <= 0) {
		dev_err(&pdev->dev, "platform_get_irq() failed\n");
		return vphydev->irq;
	}

	/* the AXI lite clock is used for the clock rate detector */
	vphydev->axi_lite_clk = devm_clk_get(&pdev->dev, "axi-lite");
	if (IS_ERR(vphydev->axi_lite_clk)) {
		ret = PTR_ERR(vphydev->axi_lite_clk);
		vphydev->axi_lite_clk = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "axi-lite-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get the axi lite clk.\n");
		return ret;
	}

	ret = clk_prepare_enable(vphydev->axi_lite_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable axi-lite clk\n");
		return ret;
	}
	axi_lite_rate = clk_get_rate(vphydev->axi_lite_clk);
	hdmi_dbg("AXI Lite clock rate = %lu Hz\n", axi_lite_rate);

	/* set axi-lite clk in configuration data */
	XVphy_ConfigTable[instance].AxiLiteClkFreq = axi_lite_rate;
	XVphy_ConfigTable[instance].DrpClkFreq = axi_lite_rate;

	/* dru-clk is used for the nidru block for low res support */
	vphydev->clkp = devm_clk_get(&pdev->dev, "dru-clk");
	if (IS_ERR(vphydev->clkp)) {
		ret = PTR_ERR(vphydev->clkp);
		vphydev->clkp = NULL;
		if (ret == -EPROBE_DEFER)
			dev_info(&pdev->dev, "dru-clk not ready -EPROBE_DEFER\n");
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "failed to get the nidru clk.\n");
		return ret;
	}

	ret = clk_prepare_enable(vphydev->clkp);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable nidru clk\n");
		return ret;
	}

	dru_clk_rate = clk_get_rate(vphydev->clkp);
	hdmi_dbg("default dru-clk rate = %lu\n", dru_clk_rate);
	if (dru_clk_rate != XVPHY_DRU_REF_CLK_HZ) {
		ret = clk_set_rate(vphydev->clkp, XVPHY_DRU_REF_CLK_HZ);
		if (ret != 0) {
			dev_err(&pdev->dev, "Cannot set rate : %d\n", ret);
		}
		dru_clk_rate = clk_get_rate(vphydev->clkp);
		hdmi_dbg("ref dru-clk rate = %lu\n", dru_clk_rate);
	}

	provider = devm_of_phy_provider_register(&pdev->dev, xvphy_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
			return PTR_ERR(provider);
	}


	/* Initialize HDMI VPHY */
	Status = XVphy_Hdmi_CfgInitialize(&vphydev->xvphy, 0/*QuadID*/,
		&XVphy_ConfigTable[instance]);
	if (Status != XST_SUCCESS) {
		dev_err(&pdev->dev, "HDMI VPHY initialization error\n");
		return ENODEV;
	}

	Data = XVphy_GetVersion(&vphydev->xvphy);
	printk(KERN_INFO "VPhy version : %02d.%02d (%04x)\n", ((Data >> 24) & 0xFF),
			((Data >> 16) & 0xFF), (Data & 0xFFFF));


	ret = devm_request_threaded_irq(&pdev->dev, vphydev->irq, xvphy_irq_handler, xvphy_irq_thread,
			IRQF_TRIGGER_HIGH /*IRQF_SHARED*/, "xilinx-vphy", vphydev/*dev_id*/);

	if (ret) {
		dev_err(&pdev->dev, "unable to request IRQ %d\n", vphydev->irq);
		return ret;
	}

	hdmi_dbg("config.DruIsPresent = %d\n", XVphy_ConfigTable[instance].DruIsPresent);
	if (vphydev->xvphy.Config.DruIsPresent == (TRUE)) {
		hdmi_dbg("DRU reference clock frequency %0d Hz\n\r",
						XVphy_DruGetRefClkFreqHz(&vphydev->xvphy));
	}
	dev_info(&pdev->dev, "hdmi-vphy probe successful\n");
	/* probe has succeeded for this instance, increment instance index */
	instance++;
	return 0;
}

/* Match table for of_platform binding */
static const struct of_device_id xvphy_of_match[] = {
	{ .compatible = "xlnx,vid-phy-controller-2.1" },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, xvphy_of_match);

static struct platform_driver xvphy_driver = {
	.probe = xvphy_probe,
	.driver = {
		.name = "xilinx-vphy",
		.of_match_table	= xvphy_of_match,
	},
};
module_platform_driver(xvphy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Leon Woestenberg <leon@sidebranch.com>");
MODULE_DESCRIPTION("Xilinx Vphy driver");

/* phy sub-directory is used as a place holder for all shared code for
   hdmi-rx and hdmi-tx driver. All shared API's need to be exported */

/* Configuration Tables for hdcp */
XHdcp1x_Config XHdcp1x_ConfigTable[XPAR_XHDCP_NUM_INSTANCES];
XTmrCtr_Config XTmrCtr_ConfigTable[XPAR_XTMRCTR_NUM_INSTANCES];
XHdcp22_Cipher_Config XHdcp22_Cipher_ConfigTable[XPAR_XHDCP22_CIPHER_NUM_INSTANCES];
XHdcp22_mmult_Config XHdcp22_mmult_ConfigTable[XPAR_XHDCP22_MMULT_NUM_INSTANCES];
XHdcp22_Rng_Config XHdcp22_Rng_ConfigTable[XPAR_XHDCP22_RNG_NUM_INSTANCES];
XHdcp22_Rx_Config XHdcp22_Rx_ConfigTable[XPAR_XHDCP22_RX_NUM_INSTANCES];
XHdcp22_Tx_Config XHdcp22_Tx_ConfigTable[XPAR_XHDCP22_TX_NUM_INSTANCES];


/* common functionality shared between RX and TX */
/* xvidc component */
EXPORT_SYMBOL_GPL(XVidC_ReportTiming);
EXPORT_SYMBOL_GPL(XVidC_SetVideoStream);
EXPORT_SYMBOL_GPL(XVidC_ReportStreamInfo);
EXPORT_SYMBOL_GPL(XVidC_EdidGetManName);
EXPORT_SYMBOL_GPL(XVidC_Set3DVideoStream);
EXPORT_SYMBOL_GPL(XVidC_GetPixelClockHzByVmId);
EXPORT_SYMBOL_GPL(XVidC_GetVideoModeIdWBlanking);
EXPORT_SYMBOL_GPL(XVidC_GetVideoModeId);
EXPORT_SYMBOL_GPL(XVidC_GetPixelClockHzByHVFr);
EXPORT_SYMBOL_GPL(XVphy_GetLineRateHz);

/* Configuration Tables for hdcp */
EXPORT_SYMBOL_GPL(XHdcp1x_ConfigTable);
EXPORT_SYMBOL_GPL(XTmrCtr_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Cipher_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_mmult_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Rng_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Rx_ConfigTable);
EXPORT_SYMBOL_GPL(XHdcp22_Tx_ConfigTable);

/* Global API's for hdcp key */
EXPORT_SYMBOL_GPL(XHdcp22Cmn_Sha256Hash);
EXPORT_SYMBOL_GPL(XHdcp22Cmn_HmacSha256Hash);
EXPORT_SYMBOL_GPL(XHdcp22Cmn_Aes128Encrypt);
EXPORT_SYMBOL_GPL(aes256_done);
EXPORT_SYMBOL_GPL(aes256_init);
EXPORT_SYMBOL_GPL(aes256_decrypt_ecb);

/* Global API's for xhdcp1x */
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyUpdate);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadPublicCert);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadLc128);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_GetVersion);
EXPORT_SYMBOL_GPL(XHdcp1x_CipherIntrHandler);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp1x_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp1x_Info);
EXPORT_SYMBOL_GPL(XHdcp1x_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_ProcessAKsv);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerStart);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerStop);
EXPORT_SYMBOL_GPL(XHdcp1x_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp1x_SelfTest);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInWaitforready);
EXPORT_SYMBOL_GPL(XHdcp1x_SetKeySelect);
EXPORT_SYMBOL_GPL(XHdcp1x_Reset);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTimerDelay);
EXPORT_SYMBOL_GPL(XHdcp1x_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopology);
EXPORT_SYMBOL_GPL(XHdcp1x_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp1x_Disable);
EXPORT_SYMBOL_GPL(XHdcp1x_SetDebugLogMsg);
EXPORT_SYMBOL_GPL(XHdcp1x_SetTopologyKSVList);
EXPORT_SYMBOL_GPL(XHdcp1x_IsEncrypted);
EXPORT_SYMBOL_GPL(XHdcp1x_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp1x_HandleTimeout);
EXPORT_SYMBOL_GPL(XHdcp1x_Poll);
EXPORT_SYMBOL_GPL(XHdcp1x_Enable);
EXPORT_SYMBOL_GPL(XHdcp1x_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp1x_SetHdmiMode);
EXPORT_SYMBOL_GPL(XHdcp1x_SetPhysicalState);
EXPORT_SYMBOL_GPL(XHdcp1x_IsInComputations);
EXPORT_SYMBOL_GPL(XHdcp1x_EnableBlank);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp1x_DisableEncryption);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopology);
EXPORT_SYMBOL_GPL(XHdcp1x_GetTopologyKSVList);
EXPORT_SYMBOL_GPL(XHdcp1x_DisableBlank);
EXPORT_SYMBOL_GPL(XHdcp1x_EnableEncryption);
EXPORT_SYMBOL_GPL(XHdcp1x_IsDwnstrmCapable);

/* Global API's for XTmr */
EXPORT_SYMBOL_GPL(XTmrCtr_CfgInitialize);
EXPORT_SYMBOL_GPL(XTmrCtr_IsExpired);
EXPORT_SYMBOL_GPL(XTmrCtr_Start);
EXPORT_SYMBOL_GPL(XTmrCtr_InitHw);
EXPORT_SYMBOL_GPL(XTmrCtr_SetOptions);
EXPORT_SYMBOL_GPL(XTmrCtr_SetResetValue);
EXPORT_SYMBOL_GPL(XTmrCtr_SetHandler);
EXPORT_SYMBOL_GPL(XTmrCtr_InterruptHandler);
EXPORT_SYMBOL_GPL(XTmrCtr_Offsets);
EXPORT_SYMBOL_GPL(XTmrCtr_LookupConfig);
EXPORT_SYMBOL_GPL(XTmrCtr_GetOptions);
EXPORT_SYMBOL_GPL(XTmrCtr_Stop);

/* Global API's for xhdcp22Rx */
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetDdcError);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Info);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LogReset);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Reset);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyUpdate);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Poll);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LogShow);
EXPORT_SYMBOL_GPL(XHdcp22Rx_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp22Rx_GetTimer);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Disable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LoadPrivateKey);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetReadMessageComplete);
EXPORT_SYMBOL_GPL(XHdcp22Rx_GetContentStreamType);
EXPORT_SYMBOL_GPL(XHdcp22Rx_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetWriteMessageAvailable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopology);
EXPORT_SYMBOL_GPL(XHdcp22Rx_Enable);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetTopologyReceiverIdList);
EXPORT_SYMBOL_GPL(XHdcp22Rx_SetLinkError);
EXPORT_SYMBOL_GPL(XHdcp22Rx_IsEncryptionEnabled);

/* Global API's for xhdcp22Tx */
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsInProgress);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopologyReceiverIdList);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LoadRevocationTable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopologyField);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Reset);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetMessagePollingValue);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Enable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTimer);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Disable);
EXPORT_SYMBOL_GPL(XHdcp1x_Authenticate);
EXPORT_SYMBOL_GPL(XHdcp22Tx_DisableEncryption);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LookupConfig);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LogReset);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsDwnstrmCapable);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsEncryptionEnabled);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Authenticate);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LoadLc128);
EXPORT_SYMBOL_GPL(XHdcp22Tx_CfgInitialize);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Info);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsRepeater);
EXPORT_SYMBOL_GPL(XHdcp22Tx_IsAuthenticated);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetTopology);
EXPORT_SYMBOL_GPL(XHdcp22Tx_DisableBlank);
EXPORT_SYMBOL_GPL(XHdcp22Tx_GetVersion);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetCallback);
EXPORT_SYMBOL_GPL(XHdcp22Tx_SetContentStreamType);
EXPORT_SYMBOL_GPL(XHdcp22Tx_EnableBlank);
EXPORT_SYMBOL_GPL(XHdcp22Tx_LogShow);
EXPORT_SYMBOL_GPL(XHdcp22Tx_EnableEncryption);
EXPORT_SYMBOL_GPL(XHdcp22Tx_Poll);

//#ifdef DEBUG
EXPORT_SYMBOL_GPL(XVphy_HdmiDebugInfo);
EXPORT_SYMBOL_GPL(XDebug_SetDebugBufPrintf);
EXPORT_SYMBOL_GPL(XDebug_SetDebugPrintf);
//#endif
EXPORT_SYMBOL_GPL(XVphy_LogShow);
