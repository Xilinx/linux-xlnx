/*
 * Arasan Secure Digital Host Controller Interface.
 * Copyright (C) 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright (c) 2012 Wind River Systems, Inc.
 * Copyright (C) 2013 Pengutronix e.K.
 * Copyright (C) 2013 Xilinx Inc.
 *
 * Based on sdhci-of-esdhc.c
 *
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 * Copyright (c) 2009 MontaVista Software, Inc.
 *
 * Authors: Xiaobo Xie <X.Xie@freescale.com>
 *	    Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/mmc/mmc.h>
#include <linux/soc/xilinx/zynqmp/tap_delays.h>
#include "sdhci-pltfm.h"

#define SDHCI_ARASAN_CLK_CTRL_OFFSET	0x2c

#define CLK_CTRL_TIMEOUT_SHIFT		16
#define CLK_CTRL_TIMEOUT_MASK		(0xf << CLK_CTRL_TIMEOUT_SHIFT)
#define CLK_CTRL_TIMEOUT_MIN_EXP	13
#define SD_CLK_25_MHZ				25000000
#define SD_CLK_19_MHZ				19000000
#define MAX_TUNING_LOOP 40

/**
 * struct sdhci_arasan_data
 * @clk_ahb:	Pointer to the AHB clock
 * @phy: Pointer to the generic phy
 */
struct sdhci_arasan_data {
	struct clk	*clk_ahb;
	struct phy	*phy;
	u32 mio_bank;
	u32 device_id;
};

static unsigned int sdhci_arasan_get_timeout_clock(struct sdhci_host *host)
{
	u32 div;
	unsigned long freq;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	div = readl(host->ioaddr + SDHCI_ARASAN_CLK_CTRL_OFFSET);
	div = (div & CLK_CTRL_TIMEOUT_MASK) >> CLK_CTRL_TIMEOUT_SHIFT;

	freq = clk_get_rate(pltfm_host->clk);
	freq /= 1 << (CLK_CTRL_TIMEOUT_MIN_EXP + div);

	return freq;
}

static void arasan_zynqmp_dll_reset(struct sdhci_host *host, u8 deviceid)
{
	u16 clk;
	unsigned long timeout;

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk &= ~(SDHCI_CLOCK_CARD_EN | SDHCI_CLOCK_INT_EN);
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Issue DLL Reset */
	zynqmp_dll_reset(deviceid);

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk |= SDHCI_CLOCK_INT_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);

	/* Wait max 20 ms */
	timeout = 20;
	while (!((clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL))
				& SDHCI_CLOCK_INT_STABLE)) {
		if (timeout == 0) {
			dev_err(mmc_dev(host->mmc),
				": Internal clock never stabilised.\n");
			return;
		}
		timeout--;
		mdelay(1);
	}

	clk |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk, SDHCI_CLOCK_CONTROL);
}

static int arasan_zynqmp_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct mmc_host *mmc = host->mmc;
	u16 ctrl;
	int tuning_loop_counter = MAX_TUNING_LOOP;
	int err = 0;
	unsigned long flags;
	unsigned int tuning_count = 0;

	spin_lock_irqsave(&host->lock, flags);

	if (host->tuning_mode == SDHCI_TUNING_MODE_1)
		tuning_count = host->tuning_count;

	ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl |= SDHCI_CTRL_EXEC_TUNING;
	if (host->quirks2 & SDHCI_QUIRK2_TUNING_WORK_AROUND)
		ctrl |= SDHCI_CTRL_TUNED_CLK;
	sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

	mdelay(1);

	arasan_zynqmp_dll_reset(host, sdhci_arasan->device_id);

	/*
	 * As per the Host Controller spec v3.00, tuning command
	 * generates Buffer Read Ready interrupt, so enable that.
	 *
	 * Note: The spec clearly says that when tuning sequence
	 * is being performed, the controller does not generate
	 * interrupts other than Buffer Read Ready interrupt. But
	 * to make sure we don't hit a controller bug, we _only_
	 * enable Buffer Read Ready interrupt here.
	 */
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_INT_ENABLE);
	sdhci_writel(host, SDHCI_INT_DATA_AVAIL, SDHCI_SIGNAL_ENABLE);

	/*
	 * Issue CMD19 repeatedly till Execute Tuning is set to 0 or the number
	 * of loops reaches 40 times or a timeout of 150ms occurs.
	 */
	do {
		struct mmc_command cmd = {0};
		struct mmc_request mrq = {NULL};

		cmd.opcode = opcode;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		cmd.retries = 0;
		cmd.data = NULL;
		cmd.error = 0;

		if (tuning_loop_counter-- == 0)
			break;

		mrq.cmd = &cmd;
		host->mrq = &mrq;

		/*
		 * In response to CMD19, the card sends 64 bytes of tuning
		 * block to the Host Controller. So we set the block size
		 * to 64 here.
		 */
		if (cmd.opcode == MMC_SEND_TUNING_BLOCK_HS200) {
			if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
				sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 128),
					     SDHCI_BLOCK_SIZE);
			} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
				sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 64),
					     SDHCI_BLOCK_SIZE);
			}
		} else {
			sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 64),
				     SDHCI_BLOCK_SIZE);
		}

		/*
		 * The tuning block is sent by the card to the host controller.
		 * So we set the TRNS_READ bit in the Transfer Mode register.
		 * This also takes care of setting DMA Enable and Multi Block
		 * Select in the same register to 0.
		 */
		sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

		sdhci_send_command(host, &cmd);

		host->cmd = NULL;
		host->mrq = NULL;

		spin_unlock_irqrestore(&host->lock, flags);
		/* Wait for Buffer Read Ready interrupt */
		wait_event_interruptible_timeout(host->buf_ready_int,
					(host->tuning_done == 1),
					msecs_to_jiffies(50));
		spin_lock_irqsave(&host->lock, flags);

		if (!host->tuning_done) {
			dev_warn(mmc_dev(host->mmc),
				 ": Timeout for Buffer Read Ready interrupt, back to fixed sampling clock\n");
			ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);
			ctrl &= ~SDHCI_CTRL_TUNED_CLK;
			ctrl &= ~SDHCI_CTRL_EXEC_TUNING;
			sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);

			err = -EIO;
			goto out;
		}

		host->tuning_done = 0;

		ctrl = sdhci_readw(host, SDHCI_HOST_CONTROL2);

		/* eMMC spec does not require a delay between tuning cycles */
		if (opcode == MMC_SEND_TUNING_BLOCK)
			mdelay(1);
	} while (ctrl & SDHCI_CTRL_EXEC_TUNING);

	/*
	 * The Host Driver has exhausted the maximum number of loops allowed,
	 * so use fixed sampling frequency.
	 */
	if (tuning_loop_counter < 0) {
		ctrl &= ~SDHCI_CTRL_TUNED_CLK;
		sdhci_writew(host, ctrl, SDHCI_HOST_CONTROL2);
	}
	if (!(ctrl & SDHCI_CTRL_TUNED_CLK)) {
		dev_warn(mmc_dev(host->mmc),
			 ": Tuning failed, back to fixed sampling clock\n");
		err = -EIO;
	} else {
		arasan_zynqmp_dll_reset(host, sdhci_arasan->device_id);
	}

out:
	/*
	 * In case tuning fails, host controllers which support
	 * re-tuning can try tuning again at a later time, when the
	 * re-tuning timer expires.  So for these controllers, we
	 * return 0. Since there might be other controllers who do not
	 * have this capability, we return error for them.
	 */
	if (tuning_count)
		err = 0;

	host->mmc->retune_period = err ? 0 : tuning_count;

	sdhci_writel(host, host->ier, SDHCI_INT_ENABLE);
	sdhci_writel(host, host->ier, SDHCI_SIGNAL_ENABLE);
	spin_unlock_irqrestore(&host->lock, flags);

	return err;
}

static void sdhci_arasan_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	bool ctrl_phy = false;

	if (clock > MMC_HIGH_52_MAX_DTR && (!IS_ERR(sdhci_arasan->phy)))
		ctrl_phy = true;

	if ((host->quirks2 & SDHCI_QUIRK2_CLOCK_STANDARD_25_BROKEN) &&
		(host->version >= SDHCI_SPEC_300)) {
		if (clock == SD_CLK_25_MHZ)
			clock = SD_CLK_19_MHZ;
		if ((host->timing != MMC_TIMING_LEGACY) &&
			(host->timing != MMC_TIMING_UHS_SDR12))
			arasan_zynqmp_set_tap_delay(sdhci_arasan->device_id,
						    host->timing,
						    sdhci_arasan->mio_bank);
	}

	if (ctrl_phy) {
		spin_unlock_irq(&host->lock);
		phy_power_off(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
	}

	sdhci_set_clock(host, clock);

	if (ctrl_phy) {
		spin_unlock_irq(&host->lock);
		phy_power_on(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
	}
}

static struct sdhci_ops sdhci_arasan_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_arasan_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_arasan_pdata = {
	.ops = &sdhci_arasan_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
			SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN,
};

#ifdef CONFIG_PM_SLEEP
/**
 * sdhci_arasan_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Put the device in a low power state.
 */
static int sdhci_arasan_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	if (!IS_ERR(sdhci_arasan->phy)) {
		ret = phy_power_off(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power off phy.\n");
			sdhci_resume_host(host);
			return ret;
		}
	}

	clk_disable(pltfm_host->clk);
	clk_disable(sdhci_arasan->clk_ahb);

	return 0;
}

/**
 * sdhci_arasan_resume - Resume method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Resume operation after suspend
 */
static int sdhci_arasan_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_enable(sdhci_arasan->clk_ahb);
	if (ret) {
		dev_err(dev, "Cannot enable AHB clock.\n");
		return ret;
	}

	ret = clk_enable(pltfm_host->clk);
	if (ret) {
		dev_err(dev, "Cannot enable SD clock.\n");
		return ret;
	}

	if (!IS_ERR(sdhci_arasan->phy)) {
		ret = phy_power_on(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power on phy.\n");
			return ret;
		}
	}

	return sdhci_resume_host(host);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_arasan_dev_pm_ops, sdhci_arasan_suspend,
			 sdhci_arasan_resume);

static int sdhci_arasan_probe(struct platform_device *pdev)
{
	int ret;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_arasan_data *sdhci_arasan;

	host = sdhci_pltfm_init(pdev, &sdhci_arasan_pdata,
				sizeof(*sdhci_arasan));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_arasan = sdhci_pltfm_priv(pltfm_host);

	sdhci_arasan->clk_ahb = devm_clk_get(&pdev->dev, "clk_ahb");
	if (IS_ERR(sdhci_arasan->clk_ahb)) {
		dev_err(&pdev->dev, "clk_ahb clock not found.\n");
		ret = PTR_ERR(sdhci_arasan->clk_ahb);
		goto err_pltfm_free;
	}

	clk_xin = devm_clk_get(&pdev->dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		dev_err(&pdev->dev, "clk_xin clock not found.\n");
		ret = PTR_ERR(clk_xin);
		goto err_pltfm_free;
	}

	ret = clk_prepare_enable(sdhci_arasan->clk_ahb);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable AHB clock.\n");
		goto err_pltfm_free;
	}

	ret = clk_prepare_enable(clk_xin);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SD clock.\n");
		goto clk_dis_ahb;
	}

	sdhci_get_of_property(pdev);
	pltfm_host->clk = clk_xin;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "parsing dt failed (%u)\n", ret);
		goto clk_disable_all;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-8.9a") ||
		of_device_is_compatible(pdev->dev.of_node,
					"arasan,sdhci-8.9a")) {
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;
		host->quirks2 |= SDHCI_QUIRK2_CLOCK_STANDARD_25_BROKEN;
		if (of_get_property(pdev->dev.of_node, "broken-adma2", NULL))
			host->quirks |= SDHCI_QUIRK_BROKEN_ADMA;
		if (of_device_is_compatible(pdev->dev.of_node,
					    "xlnx,zynqmp-8.9a")) {
			ret = of_property_read_u32(pdev->dev.of_node,
						   "xlnx,mio_bank",
						   &sdhci_arasan->mio_bank);
			if (ret < 0) {
				dev_err(&pdev->dev,
					"\"xlnx,mio_bank \" property is missing.\n");
				goto clk_disable_all;
			}
			ret = of_property_read_u32(pdev->dev.of_node,
						   "xlnx,device_id",
						   &sdhci_arasan->device_id);
			if (ret < 0) {
				dev_err(&pdev->dev,
					"\"xlnx,device_id \" property is missing.\n");
				goto clk_disable_all;
			}
			sdhci_arasan_ops.platform_execute_tuning =
				arasan_zynqmp_execute_tuning;
		}
	}

	sdhci_arasan->phy = ERR_PTR(-ENODEV);
	if (of_device_is_compatible(pdev->dev.of_node,
				    "arasan,sdhci-5.1")) {
		sdhci_arasan->phy = devm_phy_get(&pdev->dev,
						 "phy_arasan");
		if (IS_ERR(sdhci_arasan->phy)) {
			ret = PTR_ERR(sdhci_arasan->phy);
			dev_err(&pdev->dev, "No phy for arasan,sdhci-5.1.\n");
			goto clk_disable_all;
		}

		ret = phy_init(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(&pdev->dev, "phy_init err.\n");
			goto clk_disable_all;
		}

		ret = phy_power_on(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(&pdev->dev, "phy_power_on err.\n");
			goto err_phy_power;
		}
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_power_off(sdhci_arasan->phy);
err_phy_power:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_exit(sdhci_arasan->phy);
clk_disable_all:
	clk_disable_unprepare(clk_xin);
clk_dis_ahb:
	clk_disable_unprepare(sdhci_arasan->clk_ahb);
err_pltfm_free:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_arasan_remove(struct platform_device *pdev)
{
	int ret;
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct clk *clk_ahb = sdhci_arasan->clk_ahb;

	if (!IS_ERR(sdhci_arasan->phy)) {
		phy_power_off(sdhci_arasan->phy);
		phy_exit(sdhci_arasan->phy);
	}

	ret = sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(clk_ahb);

	return ret;
}

static const struct of_device_id sdhci_arasan_of_match[] = {
	{ .compatible = "arasan,sdhci-8.9a" },
	{ .compatible = "arasan,sdhci-5.1" },
	{ .compatible = "arasan,sdhci-4.9a" },
	{ .compatible = "xlnx,zynqmp-8.9a" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_arasan_of_match);

static struct platform_driver sdhci_arasan_driver = {
	.driver = {
		.name = "sdhci-arasan",
		.of_match_table = sdhci_arasan_of_match,
		.pm = &sdhci_arasan_dev_pm_ops,
	},
	.probe = sdhci_arasan_probe,
	.remove = sdhci_arasan_remove,
};

module_platform_driver(sdhci_arasan_driver);

MODULE_DESCRIPTION("Driver for the Arasan SDHCI Controller");
MODULE_AUTHOR("Soeren Brinkmann <soren.brinkmann@xilinx.com>");
MODULE_LICENSE("GPL");
