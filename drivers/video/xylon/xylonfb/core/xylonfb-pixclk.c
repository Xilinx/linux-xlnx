/*
 * Xylon logiCVC frame buffer driver pixel clock generation
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2013 Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


/*
 * This file implements HW dependent functionality for controlling pixel clock
 * generation on various HW platforms.
 */


#include <linux/kernel.h>


#define XYLONFB_PIXCLK_GEN_DEVS 8

static int (*xylonfb_hw_pixclk_set_fn[XYLONFB_PIXCLK_GEN_DEVS])(unsigned long);
static bool xylonfb_hw_pixclk_init;

#if defined(CONFIG_FB_XYLON_PIXCLK_ZYNQ_PS)

#define XYLONFB_PIXCLK_ZYNQ_PS 1

#include <linux/io.h>
#include <linux/errno.h>

int xylonfb_hw_pixclk_set_zynq_ps(unsigned long pixclk_khz)
{
	unsigned long pllclk, sysclk;
	unsigned long div, delta, delta_dec, delta_inc;
	void *slcr_regs, *clk_regs, *rst_reg;

	/* all clock values are in kHz */
	pllclk = 1000000;
	sysclk = 100000;

	slcr_regs = ioremap_nocache(0xF8000004, 8);
	if (!slcr_regs) {
		pr_err("Error mapping SLCR\n");
		return -EBUSY;
	}
	clk_regs = ioremap_nocache(0xF8000170, 32);
	if (!clk_regs) {
		pr_err("Error setting xylonfb pixelclock\n");
		iounmap(slcr_regs);
		return -EBUSY;
	}
	rst_reg = ioremap_nocache(0xF8000240, 4);
	if (!rst_reg) {
		pr_err("Error setting xylonfb pixelclock\n");
		iounmap(clk_regs);
		iounmap(slcr_regs);
		return -EBUSY;
	}

	/* unlock register access */
	writel(0xDF0D, (slcr_regs+4));
#if 0
	/* calculate system clock divisor */
	div = pllclk / sysclk;
	/* prepare for register writting */
	div = (div + 0x1000) << 8;
	/* set system clock */
	writel(div, clk_regs);
	/* calculate video clock divisor */
#endif
	div = pllclk / pixclk_khz;
	delta = (pllclk / div) - pixclk_khz;
	if (delta != 0) {
		delta_inc = pixclk_khz - (pllclk / (div+1));
		delta_dec = (pllclk / (div-1)) - pixclk_khz;
		if (delta < delta_inc) {
			if (delta > delta_dec)
				div--;
#if 0
			else
				div = div;
#endif
		} else {
			if (delta > delta_dec) {
				if (delta_inc > delta_dec)
					div--;
				else
					div++;
			} else {
				div++;
			}
		}
	}
	/* prepare for register writting */
	div = (div + 0x1000) << 8;
	/* set video clock */
	writel(div, (clk_regs+0x10));
	/* lock register access */
	writel(0x767B, slcr_regs);

	iounmap(rst_reg);
	iounmap(clk_regs);
	iounmap(slcr_regs);

	return 0;
}

#endif /* #if defined(CONFIG_FB_XYLON_PIXCLK_ZYNQ_PS) */

#if defined(CONFIG_FB_XYLON_PIXCLK_LOGICLK)

#define XYLONFB_PIXCLK_LOGICLK 2

#include <linux/io.h>
#include <linux/errno.h>
#include <linux/delay.h>
#ifdef CONFIG_OF
/* For open firmware. */
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#endif
#include "logiclk.h"

int xylonfb_hw_pixclk_set_logiclk(unsigned long pixclk_khz)
{
#ifdef CONFIG_OF
	struct device_node *dn;
	const unsigned int *val;
	int len;
#endif
	u32 *logiclk_regs;
	struct logiclk_freq_out freq_out;
	u32 logiclk[LOGICLK_REGS];
	u32 address, osc_freq_hz;
	int i, size;

	address = 0x40010000;
	size = LOGICLK_REGS * sizeof(u32);
	osc_freq_hz = 100000000;

#ifdef CONFIG_OF
	dn = of_find_node_by_name(NULL, "logiclk");
	if (dn) {
		val = of_get_property(dn, "reg", &len);
		address = be32_to_cpu(val[0]);
		size = be32_to_cpu(val[1]);
		val = of_get_property(dn, "osc-clk-freq-hz", &len);
		osc_freq_hz = be32_to_cpu(val[0]);
	}
#endif

	logiclk_regs = ioremap_nocache(address, size);
	if (!logiclk_regs) {
		pr_err("Error mapping logiCLK\n");
		return -EBUSY;
	}

	for (i = 0; i < LOGICLK_OUTPUTS; i++)
		freq_out.freq_out_hz[i] = pixclk_khz * 1000;

	if (logiclk_calc_regs(&freq_out, osc_freq_hz, logiclk)) {
		pr_err("Error calculating logiCLK parameters\n");
		return -EINVAL;
	}
	writel(1, logiclk_regs+LOGICLK_RST_REG_OFF);
	udelay(10);
	writel(0, logiclk_regs+LOGICLK_RST_REG_OFF);

	for (i = 0; i < LOGICLK_REGS; i++)
		writel(logiclk[i], logiclk_regs+LOGICLK_PLL_MANUAL_REG_OFF+i);

	while (1) {
		if (readl(logiclk_regs+LOGICLK_PLL_REG_OFF) & LOGICLK_PLL_RDY) {
			writel((LOGICLK_PLL_REG_EN | LOGICLK_PLL_EN),
				logiclk_regs+LOGICLK_PLL_REG_OFF);
			break;
		}
	}

	iounmap(logiclk_regs);

	return 0;
}

#endif /* #if defined(CONFIG_FB_XYLON_PIXCLK_LOGICLK) */

#if defined(CONFIG_FB_XYLON_PIXCLK_SI570)

#define XYLONFB_PIXCLK_SI570 3

#include <linux/i2c/si570.h>

int xylonfb_hw_pixclk_set_si570(unsigned long pixclk_khz)
{
	struct i2c_client *si570_client;

	si570_client = get_i2c_client_si570();
	if (si570_client)
		return set_frequency_si570(&si570_client->dev, (pixclk_khz * 1000));
	else
		return -EPERM;
}

#endif /* #if defined(CONFIG_FB_XYLON_PIXCLK_SI570) */


bool xylonfb_hw_pixclk_supported(int id)
{
	if (!xylonfb_hw_pixclk_init) {
#if defined(XYLONFB_PIXCLK_ZYNQ_PS)
		xylonfb_hw_pixclk_set_fn[XYLONFB_PIXCLK_ZYNQ_PS] =
			xylonfb_hw_pixclk_set_zynq_ps;
#endif
#if defined(XYLONFB_PIXCLK_LOGICLK)
		xylonfb_hw_pixclk_set_fn[XYLONFB_PIXCLK_LOGICLK] =
			xylonfb_hw_pixclk_set_logiclk;
#endif
#if defined(XYLONFB_PIXCLK_SI570)
		xylonfb_hw_pixclk_set_fn[XYLONFB_PIXCLK_SI570] =
			xylonfb_hw_pixclk_set_si570;
#endif
		xylonfb_hw_pixclk_init = true;
	}

	return xylonfb_hw_pixclk_set_fn[id] ? true : false;
}

#if !defined(CONFIG_FB_XYLON_PIXCLK)

int xylonfb_hw_pixclk_set(int id, unsigned long pixclk_khz)
{
	pr_info("Pixel clock change not supported\n");
	return 0;
}

#else

int xylonfb_hw_pixclk_set(int id, unsigned long pixclk_khz)
{
	return xylonfb_hw_pixclk_set_fn[id](pixclk_khz);
}

#endif /* #if defined(CONFIG_FB_XYLON_PIXCLK) */
