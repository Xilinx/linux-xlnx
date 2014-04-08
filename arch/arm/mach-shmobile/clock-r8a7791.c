/*
 * r8a7791 clock framework support
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/clock.h>
#include <mach/common.h>

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)		*1	*1
 *---------------------------------------------------
 * 0  0  0	15 x 1		x172/2	x208/2	x106
 * 0  0  1	15 x 1		x172/2	x208/2	x88
 * 0  1  0	20 x 1		x130/2	x156/2	x80
 * 0  1  1	20 x 1		x130/2	x156/2	x66
 * 1  0  0	26 / 2		x200/2	x240/2	x122
 * 1  0  1	26 / 2		x200/2	x240/2	x102
 * 1  1  0	30 / 2		x172/2	x208/2	x106
 * 1  1  1	30 / 2		x172/2	x208/2	x88
 *
 * *1 :	Table 7.6 indicates VCO ouput (PLLx = VCO/2)
 *	see "p1 / 2" on R8A7791_CLOCK_ROOT() below
 */

#define MD(nr)	(1 << nr)

#define CPG_BASE 0xe6150000
#define CPG_LEN 0x1000

#define SMSTPCR0	0xE6150130
#define SMSTPCR1	0xE6150134
#define SMSTPCR2	0xe6150138
#define SMSTPCR3	0xE615013C
#define SMSTPCR5	0xE6150144
#define SMSTPCR7	0xe615014c
#define SMSTPCR8	0xE6150990
#define SMSTPCR9	0xE6150994
#define SMSTPCR10	0xE6150998
#define SMSTPCR11	0xE615099C

#define MODEMR		0xE6160060
#define SDCKCR		0xE6150074
#define SD2CKCR		0xE6150078
#define SD3CKCR		0xE615007C
#define MMC0CKCR	0xE6150240
#define MMC1CKCR	0xE6150244
#define SSPCKCR		0xE6150248
#define SSPRSCKCR	0xE615024C

static struct clk_mapping cpg_mapping = {
	.phys   = CPG_BASE,
	.len    = CPG_LEN,
};

static struct clk extal_clk = {
	/* .rate will be updated on r8a7791_clock_init() */
	.mapping	= &cpg_mapping,
};

static struct sh_clk_ops followparent_clk_ops = {
	.recalc	= followparent_recalc,
};

static struct clk main_clk = {
	/* .parent will be set r8a73a4_clock_init */
	.ops	= &followparent_clk_ops,
};

/*
 * clock ratio of these clock will be updated
 * on r8a7791_clock_init()
 */
SH_FIXED_RATIO_CLK_SET(pll1_clk,		main_clk,	1, 1);
SH_FIXED_RATIO_CLK_SET(pll3_clk,		main_clk,	1, 1);

/* fixed ratio clock */
SH_FIXED_RATIO_CLK_SET(extal_div2_clk,		extal_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(cp_clk,			extal_clk,	1, 2);

SH_FIXED_RATIO_CLK_SET(pll1_div2_clk,		pll1_clk,	1, 2);
SH_FIXED_RATIO_CLK_SET(hp_clk,			pll1_clk,	1, 12);
SH_FIXED_RATIO_CLK_SET(p_clk,			pll1_clk,	1, 24);
SH_FIXED_RATIO_CLK_SET(rclk_clk,		pll1_clk,	1, (48 * 1024));
SH_FIXED_RATIO_CLK_SET(mp_clk,			pll1_div2_clk,	1, 15);

static struct clk *main_clks[] = {
	&extal_clk,
	&extal_div2_clk,
	&main_clk,
	&pll1_clk,
	&pll1_div2_clk,
	&pll3_clk,
	&hp_clk,
	&p_clk,
	&rclk_clk,
	&mp_clk,
	&cp_clk,
};

/* MSTP */
enum {
	MSTP721, MSTP720,
	MSTP719, MSTP718, MSTP715, MSTP714,
	MSTP216, MSTP207, MSTP206,
	MSTP204, MSTP203, MSTP202, MSTP1105, MSTP1106, MSTP1107,
	MSTP124,
	MSTP_NR
};

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP721] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 21, 0), /* SCIF0 */
	[MSTP720] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 20, 0), /* SCIF1 */
	[MSTP719] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 19, 0), /* SCIF2 */
	[MSTP718] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 18, 0), /* SCIF3 */
	[MSTP715] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 15, 0), /* SCIF4 */
	[MSTP714] = SH_CLK_MSTP32(&p_clk, SMSTPCR7, 14, 0), /* SCIF5 */
	[MSTP216] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 16, 0), /* SCIFB2 */
	[MSTP207] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 7, 0), /* SCIFB1 */
	[MSTP206] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 6, 0), /* SCIFB0 */
	[MSTP204] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 4, 0), /* SCIFA0 */
	[MSTP203] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 3, 0), /* SCIFA1 */
	[MSTP202] = SH_CLK_MSTP32(&mp_clk, SMSTPCR2, 2, 0), /* SCIFA2 */
	[MSTP1105] = SH_CLK_MSTP32(&mp_clk, SMSTPCR11, 5, 0), /* SCIFA3 */
	[MSTP1106] = SH_CLK_MSTP32(&mp_clk, SMSTPCR11, 6, 0), /* SCIFA4 */
	[MSTP1107] = SH_CLK_MSTP32(&mp_clk, SMSTPCR11, 7, 0), /* SCIFA5 */
	[MSTP124] = SH_CLK_MSTP32(&rclk_clk, SMSTPCR1, 24, 0), /* CMT0 */
};

static struct clk_lookup lookups[] = {

	/* main clocks */
	CLKDEV_CON_ID("extal",		&extal_clk),
	CLKDEV_CON_ID("extal_div2",	&extal_div2_clk),
	CLKDEV_CON_ID("main",		&main_clk),
	CLKDEV_CON_ID("pll1",		&pll1_clk),
	CLKDEV_CON_ID("pll1_div2",	&pll1_div2_clk),
	CLKDEV_CON_ID("pll3",		&pll3_clk),
	CLKDEV_CON_ID("hp",		&hp_clk),
	CLKDEV_CON_ID("p",		&p_clk),
	CLKDEV_CON_ID("rclk",		&rclk_clk),
	CLKDEV_CON_ID("mp",		&mp_clk),
	CLKDEV_CON_ID("cp",		&cp_clk),
	CLKDEV_CON_ID("peripheral_clk", &hp_clk),

	/* MSTP */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP204]), /* SCIFA0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP203]), /* SCIFA1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP206]), /* SCIFB0 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP207]), /* SCIFB1 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP216]), /* SCIFB2 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP202]), /* SCIFA2 */
	CLKDEV_DEV_ID("sh-sci.6", &mstp_clks[MSTP721]), /* SCIF0 */
	CLKDEV_DEV_ID("sh-sci.7", &mstp_clks[MSTP720]), /* SCIF1 */
	CLKDEV_DEV_ID("sh-sci.8", &mstp_clks[MSTP719]), /* SCIF2 */
	CLKDEV_DEV_ID("sh-sci.9", &mstp_clks[MSTP718]), /* SCIF3 */
	CLKDEV_DEV_ID("sh-sci.10", &mstp_clks[MSTP715]), /* SCIF4 */
	CLKDEV_DEV_ID("sh-sci.11", &mstp_clks[MSTP714]), /* SCIF5 */
	CLKDEV_DEV_ID("sh-sci.12", &mstp_clks[MSTP1105]), /* SCIFA3 */
	CLKDEV_DEV_ID("sh-sci.13", &mstp_clks[MSTP1106]), /* SCIFA4 */
	CLKDEV_DEV_ID("sh-sci.14", &mstp_clks[MSTP1107]), /* SCIFA5 */
	CLKDEV_DEV_ID("sh_cmt.0", &mstp_clks[MSTP124]),
};

#define R8A7791_CLOCK_ROOT(e, m, p0, p1, p30, p31)		\
	extal_clk.rate	= e * 1000 * 1000;			\
	main_clk.parent	= m;					\
	SH_CLK_SET_RATIO(&pll1_clk_ratio, p1 / 2, 1);		\
	if (mode & MD(19))					\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p31, 1);	\
	else							\
		SH_CLK_SET_RATIO(&pll3_clk_ratio, p30, 1)


void __init r8a7791_clock_init(void)
{
	void __iomem *modemr = ioremap_nocache(MODEMR, PAGE_SIZE);
	u32 mode;
	int k, ret = 0;

	BUG_ON(!modemr);
	mode = ioread32(modemr);
	iounmap(modemr);

	switch (mode & (MD(14) | MD(13))) {
	case 0:
		R8A7791_CLOCK_ROOT(15, &extal_clk, 172, 208, 106, 88);
		break;
	case MD(13):
		R8A7791_CLOCK_ROOT(20, &extal_clk, 130, 156, 80, 66);
		break;
	case MD(14):
		R8A7791_CLOCK_ROOT(26, &extal_div2_clk, 200, 240, 122, 102);
		break;
	case MD(13) | MD(14):
		R8A7791_CLOCK_ROOT(30, &extal_div2_clk, 172, 208, 106, 88);
		break;
	}

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		goto epanic;

	return;

epanic:
	panic("failed to setup r8a7791 clocks\n");
}
