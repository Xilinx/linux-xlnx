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

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/phy/phy.h>
#include <linux/mmc/mmc.h>
#include <linux/soc/xilinx/zynqmp/tap_delays.h>
#include <linux/soc/xilinx/zynqmp/fw.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regmap.h>
#include "sdhci-pltfm.h"
#include <linux/of.h>
#include <linux/slab.h>

#define SDHCI_ARASAN_CLK_CTRL_OFFSET	0x2c
#define SDHCI_ARASAN_VENDOR_REGISTER	0x78

#define VENDOR_ENHANCED_STROBE		BIT(0)
#define CLK_CTRL_TIMEOUT_SHIFT		16
#define CLK_CTRL_TIMEOUT_MASK		(0xf << CLK_CTRL_TIMEOUT_SHIFT)
#define CLK_CTRL_TIMEOUT_MIN_EXP	13
#define SD_CLK_25_MHZ				25000000
#define SD_CLK_19_MHZ				19000000
#define MAX_TUNING_LOOP 40

#define PHY_CLK_TOO_SLOW_HZ		400000

/*
 * On some SoCs the syscon area has a feature where the upper 16-bits of
 * each 32-bit register act as a write mask for the lower 16-bits.  This allows
 * atomic updates of the register without locking.  This macro is used on SoCs
 * that have that feature.
 */
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

/**
 * struct sdhci_arasan_soc_ctl_field - Field used in sdhci_arasan_soc_ctl_map
 *
 * @reg:	Offset within the syscon of the register containing this field
 * @width:	Number of bits for this field
 * @shift:	Bit offset within @reg of this field (or -1 if not avail)
 */
struct sdhci_arasan_soc_ctl_field {
	u32 reg;
	u16 width;
	s16 shift;
};

/**
 * struct sdhci_arasan_soc_ctl_map - Map in syscon to corecfg registers
 *
 * It's up to the licensee of the Arsan IP block to make these available
 * somewhere if needed.  Presumably these will be scattered somewhere that's
 * accessible via the syscon API.
 *
 * @baseclkfreq:	Where to find corecfg_baseclkfreq
 * @clockmultiplier:	Where to find corecfg_clockmultiplier
 * @hiword_update:	If true, use HIWORD_UPDATE to access the syscon
 */
struct sdhci_arasan_soc_ctl_map {
	struct sdhci_arasan_soc_ctl_field	baseclkfreq;
	struct sdhci_arasan_soc_ctl_field	clockmultiplier;
	bool					hiword_update;
};

/**
 * struct sdhci_arasan_data
 * @host:		Pointer to the main SDHCI host structure.
 * @clk_ahb:		Pointer to the AHB clock
 * @phy:		Pointer to the generic phy
 * @is_phy_on:		True if the PHY is on; false if not.
 * @sdcardclk_hw:	Struct for the clock we might provide to a PHY.
 * @sdcardclk:		Pointer to normal 'struct clock' for sdcardclk_hw.
 * @soc_ctl_base:	Pointer to regmap for syscon for soc_ctl registers.
 * @soc_ctl_map:	Map to get offsets into soc_ctl registers.
 */
struct sdhci_arasan_data {
	struct sdhci_host *host;
	struct clk	*clk_ahb;
	struct phy	*phy;
	u32 mio_bank;
	u32 device_id;
	bool		is_phy_on;

	struct clk_hw	sdcardclk_hw;
	struct clk      *sdcardclk;

	struct regmap	*soc_ctl_base;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default;
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map;
	unsigned int	quirks; /* Arasan deviations from spec */

/* Controller does not have CD wired and will not function normally without */
#define SDHCI_ARASAN_QUIRK_FORCE_CDTEST	BIT(0)
};

static const struct sdhci_arasan_soc_ctl_map rk3399_soc_ctl_map = {
	.baseclkfreq = { .reg = 0xf000, .width = 8, .shift = 8 },
	.clockmultiplier = { .reg = 0xf02c, .width = 8, .shift = 0},
	.hiword_update = true,
};

/**
 * sdhci_arasan_syscon_write - Write to a field in soc_ctl registers
 *
 * This function allows writing to fields in sdhci_arasan_soc_ctl_map.
 * Note that if a field is specified as not available (shift < 0) then
 * this function will silently return an error code.  It will be noisy
 * and print errors for any other (unexpected) errors.
 *
 * @host:	The sdhci_host
 * @fld:	The field to write to
 * @val:	The value to write
 */
static int sdhci_arasan_syscon_write(struct sdhci_host *host,
				   const struct sdhci_arasan_soc_ctl_field *fld,
				   u32 val)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	struct regmap *soc_ctl_base = sdhci_arasan->soc_ctl_base;
	u32 reg = fld->reg;
	u16 width = fld->width;
	s16 shift = fld->shift;
	int ret;

	/*
	 * Silently return errors for shift < 0 so caller doesn't have
	 * to check for fields which are optional.  For fields that
	 * are required then caller needs to do something special
	 * anyway.
	 */
	if (shift < 0)
		return -EINVAL;

	if (sdhci_arasan->soc_ctl_map->hiword_update)
		ret = regmap_write(soc_ctl_base, reg,
				   HIWORD_UPDATE(val, GENMASK(width, 0),
						 shift));
	else
		ret = regmap_update_bits(soc_ctl_base, reg,
					 GENMASK(shift + width, shift),
					 val << shift);

	/* Yell about (unexpected) regmap errors */
	if (ret)
		pr_warn("%s: Regmap write fail: %d\n",
			 mmc_hostname(host->mmc), ret);

	return ret;
}

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
		cmd.mrq = &mrq;
		cmd.error = 0;

		if (tuning_loop_counter-- == 0)
			break;

		mrq.cmd = &cmd;

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

	if (!IS_ERR(sdhci_arasan->phy)) {
		if (!sdhci_arasan->is_phy_on && clock <= PHY_CLK_TOO_SLOW_HZ) {
			/*
			 * If PHY off, set clock to max speed and power PHY on.
			 *
			 * Although PHY docs apparently suggest power cycling
			 * when changing the clock the PHY doesn't like to be
			 * powered on while at low speeds like those used in ID
			 * mode.  Even worse is powering the PHY on while the
			 * clock is off.
			 *
			 * To workaround the PHY limitations, the best we can
			 * do is to power it on at a faster speed and then slam
			 * through low speeds without power cycling.
			 */
			sdhci_set_clock(host, host->max_clk);
			spin_unlock_irq(&host->lock);
			phy_power_on(sdhci_arasan->phy);
			spin_lock_irq(&host->lock);
			sdhci_arasan->is_phy_on = true;

			/*
			 * We'll now fall through to the below case with
			 * ctrl_phy = false (so we won't turn off/on).  The
			 * sdhci_set_clock() will set the real clock.
			 */
		} else if (clock > PHY_CLK_TOO_SLOW_HZ) {
			/*
			 * At higher clock speeds the PHY is fine being power
			 * cycled and docs say you _should_ power cycle when
			 * changing clock speeds.
			 */
			ctrl_phy = true;
		}
	}

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

	if (ctrl_phy && sdhci_arasan->is_phy_on) {
		spin_unlock_irq(&host->lock);
		phy_power_off(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
		sdhci_arasan->is_phy_on = false;
	}

	sdhci_set_clock(host, clock);

	if (ctrl_phy) {
		spin_unlock_irq(&host->lock);
		phy_power_on(sdhci_arasan->phy);
		spin_lock_irq(&host->lock);
		sdhci_arasan->is_phy_on = true;
	}
}

static void sdhci_arasan_hs400_enhanced_strobe(struct mmc_host *mmc,
					struct mmc_ios *ios)
{
	u32 vendor;
	struct sdhci_host *host = mmc_priv(mmc);

	vendor = readl(host->ioaddr + SDHCI_ARASAN_VENDOR_REGISTER);
	if (ios->enhanced_strobe)
		vendor |= VENDOR_ENHANCED_STROBE;
	else
		vendor &= ~VENDOR_ENHANCED_STROBE;

	writel(vendor, host->ioaddr + SDHCI_ARASAN_VENDOR_REGISTER);
}

static void sdhci_arasan_reset(struct sdhci_host *host, u8 mask)
{
	u8 ctrl;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);

	sdhci_reset(host, mask);

	if (sdhci_arasan->quirks & SDHCI_ARASAN_QUIRK_FORCE_CDTEST) {
		ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);
		ctrl |= SDHCI_CTRL_CDTEST_INS | SDHCI_CTRL_CDTEST_EN;
		sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);
	}
}

static int sdhci_arasan_voltage_switch(struct mmc_host *mmc,
				       struct mmc_ios *ios)
{
	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_180:
		/*
		 * Plese don't switch to 1V8 as arasan,5.1 doesn't
		 * actually refer to this setting to indicate the
		 * signal voltage and the state machine will be broken
		 * actually if we force to enable 1V8. That's something
		 * like broken quirk but we could work around here.
		 */
		return 0;
	case MMC_SIGNAL_VOLTAGE_330:
	case MMC_SIGNAL_VOLTAGE_120:
		/* We don't support 3V3 and 1V2 */
		break;
	}

	return -EINVAL;
}

static struct sdhci_ops sdhci_arasan_ops = {
	.set_clock = sdhci_arasan_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_arasan_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_arasan_reset,
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

	if (!IS_ERR(sdhci_arasan->phy) && sdhci_arasan->is_phy_on) {
		ret = phy_power_off(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power off phy.\n");
			sdhci_resume_host(host);
			return ret;
		}
		sdhci_arasan->is_phy_on = false;
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

	if (!IS_ERR(sdhci_arasan->phy) && host->mmc->actual_clock) {
		ret = phy_power_on(sdhci_arasan->phy);
		if (ret) {
			dev_err(dev, "Cannot power on phy.\n");
			return ret;
		}
		sdhci_arasan->is_phy_on = true;
	}

	return sdhci_resume_host(host);
}
#endif /* ! CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_arasan_dev_pm_ops, sdhci_arasan_suspend,
			 sdhci_arasan_resume);

static const struct of_device_id sdhci_arasan_of_match[] = {
	/* SoC-specific compatible strings w/ soc_ctl_map */
	{
		.compatible = "rockchip,rk3399-sdhci-5.1",
		.data = &rk3399_soc_ctl_map,
	},

	/* Generic compatible below here */
	{ .compatible = "arasan,sdhci-8.9a" },
	{ .compatible = "arasan,sdhci-5.1" },
	{ .compatible = "arasan,sdhci-4.9a" },
	{ .compatible = "xlnx,zynqmp-8.9a" },

	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_arasan_of_match);

/**
 * sdhci_arasan_sdcardclk_recalc_rate - Return the card clock rate
 *
 * Return the current actual rate of the SD card clock.  This can be used
 * to communicate with out PHY.
 *
 * @hw:			Pointer to the hardware clock structure.
 * @parent_rate		The parent rate (should be rate of clk_xin).
 * Returns the card clock rate.
 */
static unsigned long sdhci_arasan_sdcardclk_recalc_rate(struct clk_hw *hw,
						      unsigned long parent_rate)

{
	struct sdhci_arasan_data *sdhci_arasan =
		container_of(hw, struct sdhci_arasan_data, sdcardclk_hw);
	struct sdhci_host *host = sdhci_arasan->host;

	return host->mmc->actual_clock;
}

static const struct clk_ops arasan_sdcardclk_ops = {
	.recalc_rate = sdhci_arasan_sdcardclk_recalc_rate,
};

/**
 * sdhci_arasan_update_clockmultiplier - Set corecfg_clockmultiplier
 *
 * The corecfg_clockmultiplier is supposed to contain clock multiplier
 * value of programmable clock generator.
 *
 * NOTES:
 * - Many existing devices don't seem to do this and work fine.  To keep
 *   compatibility for old hardware where the device tree doesn't provide a
 *   register map, this function is a noop if a soc_ctl_map hasn't been provided
 *   for this platform.
 * - The value of corecfg_clockmultiplier should sync with that of corresponding
 *   value reading from sdhci_capability_register. So this function is called
 *   once at probe time and never called again.
 *
 * @host:		The sdhci_host
 */
static void sdhci_arasan_update_clockmultiplier(struct sdhci_host *host,
						u32 value)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map =
		sdhci_arasan->soc_ctl_map;

	/* Having a map is optional */
	if (!soc_ctl_map)
		return;

	/* If we have a map, we expect to have a syscon */
	if (!sdhci_arasan->soc_ctl_base) {
		pr_warn("%s: Have regmap, but no soc-ctl-syscon\n",
			mmc_hostname(host->mmc));
		return;
	}

	sdhci_arasan_syscon_write(host, &soc_ctl_map->clockmultiplier, value);
}

/**
 * sdhci_arasan_update_baseclkfreq - Set corecfg_baseclkfreq
 *
 * The corecfg_baseclkfreq is supposed to contain the MHz of clk_xin.  This
 * function can be used to make that happen.
 *
 * NOTES:
 * - Many existing devices don't seem to do this and work fine.  To keep
 *   compatibility for old hardware where the device tree doesn't provide a
 *   register map, this function is a noop if a soc_ctl_map hasn't been provided
 *   for this platform.
 * - It's assumed that clk_xin is not dynamic and that we use the SDHCI divider
 *   to achieve lower clock rates.  That means that this function is called once
 *   at probe time and never called again.
 *
 * @host:		The sdhci_host
 */
static void sdhci_arasan_update_baseclkfreq(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_arasan_data *sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	const struct sdhci_arasan_soc_ctl_map *soc_ctl_map =
		sdhci_arasan->soc_ctl_map;
	u32 mhz = DIV_ROUND_CLOSEST(clk_get_rate(pltfm_host->clk), 1000000);

	/* Having a map is optional */
	if (!soc_ctl_map)
		return;

	/* If we have a map, we expect to have a syscon */
	if (!sdhci_arasan->soc_ctl_base) {
		pr_warn("%s: Have regmap, but no soc-ctl-syscon\n",
			mmc_hostname(host->mmc));
		return;
	}

	sdhci_arasan_syscon_write(host, &soc_ctl_map->baseclkfreq, mhz);
}

/**
 * sdhci_arasan_register_sdclk - Register the sdclk for a PHY to use
 *
 * Some PHY devices need to know what the actual card clock is.  In order for
 * them to find out, we'll provide a clock through the common clock framework
 * for them to query.
 *
 * Note: without seriously re-architecting SDHCI's clock code and testing on
 * all platforms, there's no way to create a totally beautiful clock here
 * with all clock ops implemented.  Instead, we'll just create a clock that can
 * be queried and set the CLK_GET_RATE_NOCACHE attribute to tell common clock
 * framework that we're doing things behind its back.  This should be sufficient
 * to create nice clean device tree bindings and later (if needed) we can try
 * re-architecting SDHCI if we see some benefit to it.
 *
 * @sdhci_arasan:	Our private data structure.
 * @clk_xin:		Pointer to the functional clock
 * @dev:		Pointer to our struct device.
 * Returns 0 on success and error value on error
 */
static int sdhci_arasan_register_sdclk(struct sdhci_arasan_data *sdhci_arasan,
				       struct clk *clk_xin,
				       struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct clk_init_data sdcardclk_init;
	const char *parent_clk_name;
	int ret;

	/* Providing a clock to the PHY is optional; no error if missing */
	if (!of_find_property(np, "#clock-cells", NULL))
		return 0;

	ret = of_property_read_string_index(np, "clock-output-names", 0,
					    &sdcardclk_init.name);
	if (ret) {
		dev_err(dev, "DT has #clock-cells but no clock-output-names\n");
		return ret;
	}

	parent_clk_name = __clk_get_name(clk_xin);
	sdcardclk_init.parent_names = &parent_clk_name;
	sdcardclk_init.num_parents = 1;
	sdcardclk_init.flags = CLK_GET_RATE_NOCACHE;
	sdcardclk_init.ops = &arasan_sdcardclk_ops;

	sdhci_arasan->sdcardclk_hw.init = &sdcardclk_init;
	sdhci_arasan->sdcardclk =
		devm_clk_register(dev, &sdhci_arasan->sdcardclk_hw);
	sdhci_arasan->sdcardclk_hw.init = NULL;

	ret = of_clk_add_provider(np, of_clk_src_simple_get,
				  sdhci_arasan->sdcardclk);
	if (ret)
		dev_err(dev, "Failed to add clock provider\n");

	return ret;
}

/**
 * sdhci_arasan_unregister_sdclk - Undoes sdhci_arasan_register_sdclk()
 *
 * Should be called any time we're exiting and sdhci_arasan_register_sdclk()
 * returned success.
 *
 * @dev:		Pointer to our struct device.
 */
static void sdhci_arasan_unregister_sdclk(struct device *dev)
{
	struct device_node *np = dev->of_node;

	if (!of_find_property(np, "#clock-cells", NULL))
		return;

	of_clk_del_provider(dev->of_node);
}

static int sdhci_arasan_probe(struct platform_device *pdev)
{
	int ret;
	const struct of_device_id *match;
	struct device_node *node;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_arasan_data *sdhci_arasan;
	struct device_node *np = pdev->dev.of_node;
	unsigned int host_quirks2 = 0;

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-8.9a")) {
		char *soc_rev;

		/* read Silicon version using nvmem driver */
		soc_rev = zynqmp_nvmem_get_silicon_version(&pdev->dev,
							   "soc_revision");
		if (PTR_ERR(soc_rev) == -EPROBE_DEFER)
			/* Do a deferred probe */
			return -EPROBE_DEFER;
		else if (IS_ERR(soc_rev))
			dev_dbg(&pdev->dev, "Error getting silicon version\n");

		/* Set host quirk if the silicon version is v1.0 */
		if (!IS_ERR(soc_rev) && (*soc_rev == ZYNQMP_SILICON_V1))
			host_quirks2 |= SDHCI_QUIRK2_NO_1_8_V;

		/* Clean soc_rev if got a valid pointer from nvmem driver
		 * else we may end up in kernel panic
		 */
		if (!IS_ERR(soc_rev))
			kfree(soc_rev);
	}

	host = sdhci_pltfm_init(pdev, &sdhci_arasan_pdata,
				sizeof(*sdhci_arasan));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	sdhci_arasan = sdhci_pltfm_priv(pltfm_host);
	sdhci_arasan->host = host;

	match = of_match_node(sdhci_arasan_of_match, pdev->dev.of_node);
	sdhci_arasan->soc_ctl_map = match->data;

	host->quirks2 |= host_quirks2;

	node = of_parse_phandle(pdev->dev.of_node, "arasan,soc-ctl-syscon", 0);
	if (node) {
		sdhci_arasan->soc_ctl_base = syscon_node_to_regmap(node);
		of_node_put(node);

		if (IS_ERR(sdhci_arasan->soc_ctl_base)) {
			ret = PTR_ERR(sdhci_arasan->soc_ctl_base);
			if (ret != -EPROBE_DEFER)
				dev_err(&pdev->dev, "Can't get syscon: %d\n",
					ret);
			goto err_pltfm_free;
		}
	}

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

	if (of_property_read_bool(np, "xlnx,fails-without-test-cd"))
		sdhci_arasan->quirks |= SDHCI_ARASAN_QUIRK_FORCE_CDTEST;

	pltfm_host->clk = clk_xin;

	if (of_device_is_compatible(pdev->dev.of_node,
				    "rockchip,rk3399-sdhci-5.1"))
		sdhci_arasan_update_clockmultiplier(host, 0x0);

	sdhci_arasan_update_baseclkfreq(host);

	ret = sdhci_arasan_register_sdclk(sdhci_arasan, clk_xin, &pdev->dev);
	if (ret)
		goto clk_disable_all;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "parsing dt failed (%u)\n", ret);
		goto unreg_clk;
	}

	if (of_device_is_compatible(pdev->dev.of_node, "xlnx,zynqmp-8.9a") ||
		of_device_is_compatible(pdev->dev.of_node,
					"arasan,sdhci-8.9a")) {
		host->quirks |= SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12;
		host->quirks2 |= SDHCI_QUIRK2_CLOCK_STANDARD_25_BROKEN;
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

	sdhci_arasan->pinctrl = devm_pinctrl_get(&pdev->dev);
	if (!IS_ERR(sdhci_arasan->pinctrl)) {
		sdhci_arasan->pins_default = pinctrl_lookup_state(
							sdhci_arasan->pinctrl,
							PINCTRL_STATE_DEFAULT);
		if (IS_ERR(sdhci_arasan->pins_default)) {
			dev_err(&pdev->dev, "Missing default pinctrl config\n");
			return IS_ERR(sdhci_arasan->pins_default);
		}

		pinctrl_select_state(sdhci_arasan->pinctrl,
				     sdhci_arasan->pins_default);
	}

	sdhci_arasan->phy = ERR_PTR(-ENODEV);
	if (of_device_is_compatible(pdev->dev.of_node,
				    "arasan,sdhci-5.1")) {
		sdhci_arasan->phy = devm_phy_get(&pdev->dev,
						 "phy_arasan");
		if (IS_ERR(sdhci_arasan->phy)) {
			ret = PTR_ERR(sdhci_arasan->phy);
			dev_err(&pdev->dev, "No phy for arasan,sdhci-5.1.\n");
			goto unreg_clk;
		}

		ret = phy_init(sdhci_arasan->phy);
		if (ret < 0) {
			dev_err(&pdev->dev, "phy_init err.\n");
			goto unreg_clk;
		}

		host->mmc_host_ops.hs400_enhanced_strobe =
					sdhci_arasan_hs400_enhanced_strobe;
		host->mmc_host_ops.start_signal_voltage_switch =
					sdhci_arasan_voltage_switch;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_add_host;

	return 0;

err_add_host:
	if (!IS_ERR(sdhci_arasan->phy))
		phy_exit(sdhci_arasan->phy);
unreg_clk:
	sdhci_arasan_unregister_sdclk(&pdev->dev);
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
		if (sdhci_arasan->is_phy_on)
			phy_power_off(sdhci_arasan->phy);
		phy_exit(sdhci_arasan->phy);
	}

	sdhci_arasan_unregister_sdclk(&pdev->dev);

	ret = sdhci_pltfm_unregister(pdev);

	clk_disable_unprepare(clk_ahb);

	return ret;
}

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
