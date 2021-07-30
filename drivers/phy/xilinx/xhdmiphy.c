// SPDX-License-Identifier: GPL-2.0-only
/*
 * Xilinx HDMI PHY
 *
 * Copyright (C) 2021 Xilinx, Inc.
 *
 * Author: Rajesh Gugulothu <gugulothu.rajesh@xilinx.com>
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-hdmi.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include "xhdmiphy.h"

struct xhdmiphy_lane {
	struct phy *phy;
	u32 share_laneclk;
	u8 direction;
	u8 lane;
	void *data;
};

static int xhdmiphy_init(struct phy *phy)
{
	struct xhdmiphy_lane *phy_lane = phy_get_drvdata(phy);
	struct xhdmiphy_dev *phy_dev = phy_lane->data;
	unsigned int ret;
	static int count;

	count++;

	if (count < XHDMIPHY_MAX_LANES)
		return 0;

	/* initialize HDMI phy */
	ret = xhdmiphy_init_phy(phy_dev);
	if (ret != 0) {
		dev_err(phy_dev->dev, "HDMI PHY initialization error\n");
		return -ENODEV;
	}
	count = 0;

	return 0;
}

static int xhdmiphy_reset(struct phy *phy)
{
	struct xhdmiphy_lane *phy_lane = phy_get_drvdata(phy);
	struct xhdmiphy_dev *phy_dev = phy_lane->data;

	if (!phy_lane->direction)
		xhdmiphy_ibufds_en(phy_dev, XHDMIPHY_DIR_TX, false);

	return 0;
}

static int xhdmiphy_configure(struct phy *phy, union phy_configure_opts *opts)
{
	struct xhdmiphy_lane *phy_lane = phy_get_drvdata(phy);
	struct xhdmiphy_dev *phy_dev = phy_lane->data;
	struct phy_configure_opts_hdmi *cfg = &opts->hdmi;
	struct hdmiphy_callback *cb_ptr = &cfg->hdmiphycb;
	unsigned int ret = 0;
	static int count_tx, count_rx;

	if (!phy_lane->direction) {
		count_rx++;
		if (count_rx < XHDMIPHY_MAX_LANES) {
			return 0;
		} else if (cfg->ibufds) {
			xhdmiphy_ibufds_en(phy_dev, XHDMIPHY_DIR_RX,
					   cfg->ibufds_en);
		} else if (cfg->tmdsclock_ratio_flag) {
			/* update TMDS clock ratio */
			phy_dev->rx_tmdsclock_ratio = cfg->tmdsclock_ratio;
		} else if (cfg->phycb) {
			switch (cb_ptr->type) {
			case RX_INIT_CB:
				phy_dev->phycb[RX_INIT_CB].cb =
							cfg->hdmiphycb.cb;
				phy_dev->phycb[RX_INIT_CB].data =
							cfg->hdmiphycb.data;
				break;
			case RX_READY_CB:
				phy_dev->phycb[RX_READY_CB].cb =
							cfg->hdmiphycb.cb;
				phy_dev->phycb[RX_READY_CB].data =
							cfg->hdmiphycb.data;
				break;
			default:
				dev_info(phy_dev->dev,
					 "type - %d phy callback does't match\n\r",
					 cb_ptr->type);
				break;
			}
		} else if (cfg->cal_mmcm_param) {
			ret = xhdmiphy_cal_mmcm_param(phy_dev,
						      XHDMIPHY_CHID_CH1,
						      XHDMIPHY_DIR_RX, cfg->ppc,
						      cfg->bpc);
			if (ret)
				dev_err(phy_dev->dev,
					"failed to update mmcm params\n\r");

			xhdmiphy_mmcm_start(phy_dev, XHDMIPHY_DIR_RX);
		} else if (cfg->clkout1_obuftds) {
			xhdmiphy_clkout1_obuftds_en(phy_dev, XHDMIPHY_DIR_RX,
						    cfg->clkout1_obuftds_en);
			cfg->clkout1_obuftds_en = 0;
		} else if (cfg->config_hdmi20 && !cfg->config_hdmi21) {
			xhdmiphy_hdmi20_conf(phy_dev, XHDMIPHY_DIR_RX);
		} else if (cfg->rx_get_refclk) {
			cfg->rx_refclk_hz = phy_dev->rx_refclk_hz;
		}
		count_rx = 0;
	}

	if (phy_lane->direction) {
		count_tx++;

		if (count_tx < XHDMIPHY_MAX_LANES) {
			return 0;
		} else if (cfg->ibufds) {
			xhdmiphy_ibufds_en(phy_dev, XHDMIPHY_DIR_TX,
					   cfg->ibufds_en);
			cfg->ibufds = 0;
		} else if (cfg->clkout1_obuftds) {
			xhdmiphy_clkout1_obuftds_en(phy_dev, XHDMIPHY_DIR_TX,
						    cfg->clkout1_obuftds_en);
			cfg->clkout1_obuftds_en = 0;
		} else if (cfg->tx_params) {
			phy_dev->tx_refclk_hz = cfg->tx_tmdsclk;

			clk_set_rate(phy_dev->tmds_clk, phy_dev->tx_refclk_hz);
			ret = xhdmiphy_set_tx_param(phy_dev,
						    XHDMIPHY_CHID_CHA,
						    cfg->ppc, cfg->bpc,
						    cfg->fmt);
			if (ret)
				dev_err(phy_dev->dev,
					"unable to set requested tx resolutions\n\r");
			cfg->tx_params = 0;
			dev_info(phy_dev->dev,
				 "tx_tmdsclk %lld\n", cfg->tx_tmdsclk);
		}
		count_tx = 0;
	}

	return 0;
}

static const struct phy_ops xhdmiphy_phyops = {
	.configure	= xhdmiphy_configure,
	.reset		= xhdmiphy_reset,
	.init		= xhdmiphy_init,
	.owner		= THIS_MODULE,
};

static struct phy *xhdmiphy_xlate(struct device *dev,
				  struct of_phandle_args *args)
{
	struct xhdmiphy_dev *priv = dev_get_drvdata(dev);
	struct xhdmiphy_lane *hdmiphy_lane = NULL;
	struct device_node *hdmiphynode = args->np;
	int index;

	if (args->args_count != 4) {
		dev_err(dev, "Invalid number of cells in 'phy' property\n");
		return ERR_PTR(-EINVAL);
	}

	if (!of_device_is_available(hdmiphynode)) {
		dev_warn(dev, "requested PHY is disabled\n");
		return ERR_PTR(-ENODEV);
	}

	for (index = 0; index < of_get_child_count(dev->of_node); index++) {
		if (hdmiphynode == priv->lanes[index]->phy->dev.of_node) {
			hdmiphy_lane = priv->lanes[index];
			break;
		}
	}

	if (!hdmiphy_lane) {
		dev_err(dev, "failed to find appropriate phy\n");
		return ERR_PTR(-EINVAL);
	}

	hdmiphy_lane->share_laneclk = args->args[2];
	hdmiphy_lane->direction = args->args[3];

	return hdmiphy_lane->phy;
}

static irqreturn_t xhdmiphy_irq_handler(int irq, void *dev_id)
{
	struct xhdmiphy_dev *priv;

	priv = (struct xhdmiphy_dev *)dev_id;
	if (!priv)
		return IRQ_NONE;

	/*
	 * disable interrupts in the HDMI PHY, they are re-enabled once
	 * serviced
	 */
	xhdmiphy_intr_dis(priv, XHDMIPHY_INTR_ALL_MASK);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t xhdmiphy_irq_thread(int irq, void *dev_id)
{
	struct xhdmiphy_dev *priv;
	u32 status;
	u32 event_mask;
	u32 event_ack;

	priv = (struct xhdmiphy_dev *)dev_id;
	if (!priv)
		return IRQ_NONE;

	/* call baremetal interrupt handler with mutex locked */
	mutex_lock(&priv->hdmiphy_mutex);

	status = xhdmiphy_read(priv, XHDMIPHY_INTR_STS_REG);
	dev_dbg(priv->dev, "xhdmiphy status = %x\n", status);

	if (priv->conf.gt_type != XHDMIPHY_GTYE5) {
		event_mask = XHDMIPHY_INTR_QPLL0_LOCK_MASK |
			     XHDMIPHY_INTR_CPLL_LOCK_MASK |
			     XHDMIPHY_INTR_QPLL1_LOCK_MASK |
			     XHDMIPHY_INTR_TXALIGNDONE_MASK |
			     XHDMIPHY_INTR_TXRESETDONE_MASK |
			     XHDMIPHY_INTR_RXRESETDONE_MASK |
			     XHDMIPHY_INTR_TXMMCMUSRCLK_LOCK_MASK |
			     XHDMIPHY_INTR_RXMMCMUSRCLK_LOCK_MASK;
	} else {
		event_mask = XHDMIPHY_INTR_LCPLL_LOCK_MASK |
			     XHDMIPHY_INTR_RPLL_LOCK_MASK |
			     XHDMIPHY_INTR_TXGPO_RE_MASK |
			     XHDMIPHY_INTR_RXGPO_RE_MASK |
			     XHDMIPHY_INTR_TXRESETDONE_MASK |
			     XHDMIPHY_INTR_RXRESETDONE_MASK |
			     XHDMIPHY_INTR_TXMMCMUSRCLK_LOCK_MASK |
			     XHDMIPHY_INTR_RXMMCMUSRCLK_LOCK_MASK;
	}

	event_ack = event_mask & status;
	if (event_ack)
		xhdmiphy_gt_handler(priv, event_ack, status);

	event_mask = XHDMIPHY_INTR_TXFREQCHANGE_MASK |
		     XHDMIPHY_INTR_RXFREQCHANGE_MASK |
		     XHDMIPHY_INTR_TXTMRTIMEOUT_MASK |
		     XHDMIPHY_INTR_RXTMRTIMEOUT_MASK;

	event_ack = event_mask & status;
	if (event_ack)
		xhdmiphy_clkdet_handler(priv, event_ack, status);

	mutex_unlock(&priv->hdmiphy_mutex);

	/* enable interrupt requesting in the PHY */
	xhdmiphy_intr_en(priv, XHDMIPHY_INTR_ALL_MASK);

	return IRQ_HANDLED;
}

static int xhdmiphy_parse_of(struct xhdmiphy_dev *priv)
{
	struct xhdmiphy_conf *xgtphycfg = &priv->conf;
	struct device *dev = priv->dev;
	struct device_node *node = dev->of_node;
	int rc, val;

	rc = of_property_read_u32(node, "xlnx,transceiver-type", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,transceiver-type");
		return rc;
	}

	if (val != XHDMIPHY_GTHE4 && val != XHDMIPHY_GTYE4 &&
	    val != XHDMIPHY_GTYE5) {
		dev_err(priv->dev, "dt transceiver-type %d is invalid\n", val);
		return -EINVAL;
	}
	xgtphycfg->gt_type = val;

	rc = of_property_read_u32(node, "xlnx,input-pixels-per-clock", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,input-pixels-per-clock");
		return rc;
	}

	if (val != 4 && val != 8) {
		dev_err(priv->dev, "dt input-pixels-per-clock %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->ppc = val;

	rc = of_property_read_u32(node, "xlnx,nidru", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,nidru");
		return rc;
	}

	if (val != 0 && val != 1) {
		dev_err(priv->dev, "dt nidru %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->dru_present = val;

	rc = of_property_read_u32(node, "xlnx,nidru-refclk-sel", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,nidru-refclk-sel");
		return rc;
	}

	if (val < XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 - 1 &&
	    val > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK - 1) {
		dev_err(priv->dev, "dt nidru-refclk-sel %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->dru_refclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,rx-no-of-channels", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,rx-no-of-channels");
		return rc;
	}

	if (val != 1 && val != 2 && val != 4) {
		dev_err(priv->dev, "dt rx-no-of-channels %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->rx_channels = val;

	rc = of_property_read_u32(node, "xlnx,tx-no-of-channels", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,tx-no-of-channels");
		return rc;
	}

	if (val != 1 && val != 2 && val != 4) {
		dev_err(priv->dev, "dt tx-no-of-channels %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->tx_channels = val;

	rc = of_property_read_u32(node, "xlnx,rx-protocol", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,rx-protocol");
		return rc;
	}

	if (val != XHDMIPHY_PROT_HDMI && val != XHDMIPHY_PROT_HDMI21 &&
	    val != XHDMIPHY_PROT_NONE) {
		dev_err(priv->dev, "dt rx-protocol %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->rx_protocol = val;

	rc = of_property_read_u32(node, "xlnx,tx-protocol", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,tx-protocol");
		return rc;
	}

	if (val != XHDMIPHY_PROT_HDMI && val != XHDMIPHY_PROT_HDMI21 &&
	    val != XHDMIPHY_PROT_NONE) {
		dev_err(priv->dev, "dt tx-protocol %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->tx_protocol = val;

	rc = of_property_read_u32(node, "xlnx,rx-refclk-sel", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,rx-refclk-sel");
		return rc;
	}

	if (val < XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 - 1 &&
	    val > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK - 1) {
		dev_err(priv->dev, "dt rx-refclk-sel %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->rx_refclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,tx-refclk-sel", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,tx-refclk-sel");
		return rc;
	}

	if (val < XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 - 1 &&
	    val > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK - 1) {
		dev_err(priv->dev, "dt tx-refclk-sel %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->tx_refclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,rx-pll-selection", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,rx-pll-selection");
		return rc;
	}

	if (val < 0 && val > 6) {
		dev_err(priv->dev, "dt rx-pll-selection %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->rx_pllclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,tx-pll-selection", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,tx-pll-selection");
		return rc;
	}

	if (val < 0 && val > 6) {
		dev_err(priv->dev, "dt tx-pll-selection %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->tx_pllclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,transceiver-width", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,transceiver-width");
		return rc;
	}
	if (val != 2 && val != 4) {
		dev_err(priv->dev, "dt transceiver-width %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->transceiver_width = val;

	rc = of_property_read_u32(node, "xlnx,use-gt-ch4-hdmi", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,use-gt-ch4-hdmi");
		return rc;
	}

	if (val != 0 && val != 1) {
		dev_err(priv->dev, "dt use-gt-ch4-hdmi %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->gt_as_tx_tmdsclk = val;

	rc = of_property_read_u32(node, "xlnx,rx-frl-refclk-sel", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,rx-frl-refclk-sel");
		return rc;
	}

	if (val < XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 - 1 &&
	    val > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK - 1) {
		dev_err(priv->dev, "dt rx-frl-refclk-sel %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->rx_frl_refclk_sel = val;

	rc = of_property_read_u32(node, "xlnx,tx-frl-refclk-sel", &val);
	if (rc < 0) {
		dev_err(priv->dev, "unable to parse %s property\n",
			"xlnx,tx-frl-refclk-sel");
		return rc;
	}

	if (val < XHDMIPHY_PLL_REFCLKSEL_GTREFCLK0 - 1 &&
	    val > XHDMIPHY_PLL_REFCLKSEL_GTGREFCLK - 1) {
		dev_err(priv->dev, "dt tx-frl-refclk-sel %d is invalid\n",
			val);
		return -EINVAL;
	}
	xgtphycfg->tx_frl_refclk_sel = val;

	return rc;
}

static int xhdmiphy_clk_init(struct xhdmiphy_dev *priv)
{
	unsigned long dru_clk_rate = 0;
	int err;

	priv->axi_lite_clk = devm_clk_get(priv->dev, "vid_phy_axi4lite_aclk");
	if (IS_ERR(priv->axi_lite_clk))
		return dev_err_probe(priv->dev, PTR_ERR(priv->axi_lite_clk),
				     "failed to get vid_phy_axi4lite_aclk\n");

	priv->tmds_clk = devm_clk_get(priv->dev, "tmds_clock");
	if (IS_ERR(priv->tmds_clk))
		return dev_err_probe(priv->dev, PTR_ERR(priv->tmds_clk),
				     "failed to get tmds_clock\n");

	if (priv->conf.dru_present) {
		priv->dru_clk = devm_clk_get(priv->dev, "drpclk");
		if (IS_ERR(priv->dru_clk))
			return dev_err_probe(priv->dev, PTR_ERR(priv->dru_clk),
					     "failed to get drpclk\n");
	} else {
		dev_dbg(priv->dev, "DRU is not enabled from device tree\n");
	}

	err = clk_prepare_enable(priv->axi_lite_clk);
	if (err) {
		dev_err(priv->dev,
			"failed to enable axi-lite clk (%d)\n", err);
		return err;
	}

	err = clk_prepare_enable(priv->tmds_clk);
	if (err) {
		dev_err(priv->dev, "failed to enable tmds_clk (%d)\n", err);
		goto err_disable_axiclk;
	}

	if (priv->conf.dru_present) {
		err = clk_prepare_enable(priv->dru_clk);
		if (err) {
			dev_err(priv->dev,
				"failed to enable nidru clk (%d)\n", err);
			goto err_disable_tmds_clk;
		}

		dru_clk_rate = clk_get_rate(priv->dru_clk);
		dev_dbg(priv->dev, "default dru-clk rate = %lu\n",
			dru_clk_rate);
		if (dru_clk_rate != XHDMIPHY_DRU_REF_CLK_HZ) {
			err = clk_set_rate(priv->dru_clk,
					   XHDMIPHY_DRU_REF_CLK_HZ);
			if (err) {
				dev_err(priv->dev,
					"Cannot set rate : %d\n", err);
				return err;
			}
			dru_clk_rate = clk_get_rate(priv->dru_clk);
			dev_dbg(priv->dev,
				"ref dru-clk rate = %lu\n", dru_clk_rate);
		}
	}

	priv->conf.drpclk_freq = dru_clk_rate;
	priv->conf.axilite_freq = clk_get_rate(priv->axi_lite_clk);

	return 0;

err_disable_tmds_clk:
	clk_disable_unprepare(priv->tmds_clk);
err_disable_axiclk:
	clk_disable_unprepare(priv->axi_lite_clk);

	return err;
}

static const struct of_device_id xhdmiphy_of_match[] = {
	{ .compatible = "xlnx,v-hdmi-phy1-1.0" },
	{},
};

MODULE_DEVICE_TABLE(of, xhdmiphy_of_match);

static int xhdmiphy_probe(struct platform_device *pdev)
{
	struct device_node *child, *np = pdev->dev.of_node;
	struct xhdmiphy_dev *priv;
	struct phy_provider *provider;
	struct xhdmiphy_lane *hdmiphy_lane;
	struct resource *res;
	struct phy *phy;
	int index = 0, ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;

	ret = xhdmiphy_parse_of(priv);
	if (ret) {
		dev_err(priv->dev, "Error parsing device tree\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->phy_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->phy_base))
		return PTR_ERR(priv->phy_base);

	mutex_init(&priv->hdmiphy_mutex);

	for_each_child_of_node(np, child) {
		if (index >= XHDMIPHY_MAX_LANES) {
			dev_err(&pdev->dev,
				"MAX 4 PHY Lanes are supported\n");
			return -E2BIG;
		}

		phy = devm_phy_create(&pdev->dev, child, &xhdmiphy_phyops);
		if (IS_ERR(phy)) {
			dev_err(&pdev->dev, "failed to create HDMI PHY\n");
			return PTR_ERR(phy);
		}

		hdmiphy_lane = devm_kzalloc(&pdev->dev, sizeof(*hdmiphy_lane),
					    GFP_KERNEL);
		if (!hdmiphy_lane)
			return -ENOMEM;

		hdmiphy_lane->lane = index;
		hdmiphy_lane->share_laneclk = -1;
		priv->lanes[index] = hdmiphy_lane;

		priv->lanes[index]->phy = phy;
		phy_set_drvdata(phy, priv->lanes[index]);
		hdmiphy_lane->data = priv;
		index++;
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq > 0) {
		ret = devm_request_threaded_irq(&pdev->dev,
						priv->irq, xhdmiphy_irq_handler,
						xhdmiphy_irq_thread,
						IRQF_TRIGGER_HIGH,
						dev_name(priv->dev),
						priv);
		if (ret)
			return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = xhdmiphy_clk_init(priv);
	if (ret)
		return ret;

	provider = devm_of_phy_provider_register(&pdev->dev, xhdmiphy_xlate);
	if (IS_ERR(provider)) {
		dev_err(&pdev->dev, "registering provider failed\n");
		ret = PTR_ERR(provider);
		goto err_clk;
	}
	return 0;

err_clk:
	clk_disable_unprepare(priv->dru_clk);
	clk_disable_unprepare(priv->tmds_clk);
	clk_disable_unprepare(priv->axi_lite_clk);

	return ret;
}

static int xhdmiphy_remove(struct platform_device *pdev)
{
	struct xhdmiphy_dev *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->dru_clk);
	clk_disable_unprepare(priv->tmds_clk);
	clk_disable_unprepare(priv->axi_lite_clk);

	return 0;
}

static struct platform_driver xhdmiphy_driver = {
	.probe = xhdmiphy_probe,
	.remove = xhdmiphy_remove,
	.driver = {
		.name = "xilinx-hdmiphy",
		.of_match_table	= xhdmiphy_of_match,
	},
};
module_platform_driver(xhdmiphy_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rajesh Gugulothu <gugulothu.rajesh@xilinx.com");
MODULE_DESCRIPTION("Xilinx HDMI PHY driver");
