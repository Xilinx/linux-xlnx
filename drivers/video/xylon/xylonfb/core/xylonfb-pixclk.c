/*
 * Xylon logiCVC frame buffer driver pixel clock generation
 *
 * Author: Xylon d.o.o.
 * e-mail: davor.joja@logicbricks.com
 *
 * 2012 (c) Xylon d.o.o.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */


/*
 * This file implements HW dependent functionality for controlling pixel clock
 * generation on various HW platforms.
 */


#define HW_PIXEL_CLOCK_CHANGE_SUPPORTED 1


#if defined(CONFIG_FB_XYLON_EXT_PIXCLK)

#if defined (HW_PIXEL_CLOCK_CHANGE_SUPPORTED)
#undef HW_PIXEL_CLOCK_CHANGE_SUPPORTED
#endif
#define HW_PIXEL_CLOCK_CHANGE_SUPPORTED 0

#include <linux/kernel.h>

int xylonfb_hw_pixclk_set(unsigned long pixclk_khz)
{
	pr_info("Pixel clock change not supported\n");
	return 0;
}

#elif defined(CONFIG_FB_XYLON_ZYNQ_PS_PIXCLK)

#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/errno.h>

int xylonfb_hw_pixclk_set(unsigned long pixclk_khz)
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
	/* calculate system clock divisor */
//	div = pllclk / sysclk;
	/* prepare for register writting */
//	div = (div + 0x1000) << 8;
	/* set system clock */
//	writel(div, clk_regs);
	/* calculate video clock divisor */
	div = pllclk / pixclk_khz;
	delta = (pllclk / div) - pixclk_khz;
	if (delta != 0) {
		delta_inc = pixclk_khz - (pllclk / (div+1));
		delta_dec = (pllclk / (div-1)) - pixclk_khz;
		if (delta < delta_inc) {
			if (delta > delta_dec)
				div--;
			//else
			//	div = div;
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
	/* reset FPGA */
//	writel(0, rst_reg);
//	writel(0x1, rst_reg);
//	writel(0, rst_reg);
	/* lock register access */
	writel(0x767B, slcr_regs);

	iounmap(rst_reg);
	iounmap(clk_regs);
	iounmap(slcr_regs);

	return 0;
}

#elif defined(CONFIG_FB_XYLON_ZC702_PIXCLK)

#include <linux/i2c/si570.h>

int xylonfb_hw_pixclk_set(unsigned long pixclk_khz)
{
	struct i2c_client *si570_client;

	si570_client = get_i2c_client_si570();
	if (si570_client)
		return set_frequency_si570(&si570_client->dev, (pixclk_khz * 1000));
	else
		return -EPERM;
}

#endif


bool xylonfb_hw_pixclk_change(void)
{
	return HW_PIXEL_CLOCK_CHANGE_SUPPORTED;
}
