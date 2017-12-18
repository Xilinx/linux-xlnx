/*
 * Copyright (C) 2014 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundationr
 */

#ifndef EXYNOS_REGS_DECON_H
#define EXYNOS_REGS_DECON_H

/* Exynos543X DECON */
#define DECON_VIDCON0			0x0000
#define DECON_VIDOUTCON0		0x0010
#define DECON_WINCONx(n)		(0x0020 + ((n) * 4))
#define DECON_VIDOSDxH(n)		(0x0080 + ((n) * 4))
#define DECON_SHADOWCON			0x00A0
#define DECON_VIDOSDxA(n)		(0x00B0 + ((n) * 0x20))
#define DECON_VIDOSDxB(n)		(0x00B4 + ((n) * 0x20))
#define DECON_VIDOSDxC(n)		(0x00B8 + ((n) * 0x20))
#define DECON_VIDOSDxD(n)		(0x00BC + ((n) * 0x20))
#define DECON_VIDOSDxE(n)		(0x00C0 + ((n) * 0x20))
#define DECON_VIDW0xADD0B0(n)		(0x0150 + ((n) * 0x10))
#define DECON_VIDW0xADD0B1(n)		(0x0154 + ((n) * 0x10))
#define DECON_VIDW0xADD0B2(n)		(0x0158 + ((n) * 0x10))
#define DECON_VIDW0xADD1B0(n)		(0x01A0 + ((n) * 0x10))
#define DECON_VIDW0xADD1B1(n)		(0x01A4 + ((n) * 0x10))
#define DECON_VIDW0xADD1B2(n)		(0x01A8 + ((n) * 0x10))
#define DECON_VIDW0xADD2(n)		(0x0200 + ((n) * 4))
#define DECON_LOCALxSIZE(n)		(0x0214 + ((n) * 4))
#define DECON_VIDINTCON0		0x0220
#define DECON_VIDINTCON1		0x0224
#define DECON_WxKEYCON0(n)		(0x0230 + ((n - 1) * 8))
#define DECON_WxKEYCON1(n)		(0x0234 + ((n - 1) * 8))
#define DECON_WxKEYALPHA(n)		(0x0250 + ((n - 1) * 4))
#define DECON_WINxMAP(n)		(0x0270 + ((n) * 4))
#define DECON_QOSLUT07_00		0x02C0
#define DECON_QOSLUT15_08		0x02C4
#define DECON_QOSCTRL			0x02C8
#define DECON_BLENDERQx(n)		(0x0300 + ((n - 1) * 4))
#define DECON_BLENDCON			0x0310
#define DECON_OPE_VIDW0xADD0(n)		(0x0400 + ((n) * 4))
#define DECON_OPE_VIDW0xADD1(n)		(0x0414 + ((n) * 4))
#define DECON_FRAMEFIFO_REG7		0x051C
#define DECON_FRAMEFIFO_REG8		0x0520
#define DECON_FRAMEFIFO_STATUS		0x0524
#define DECON_CMU			0x1404
#define DECON_UPDATE			0x1410
#define DECON_UPDATE_SCHEME		0x1438
#define DECON_VIDCON1			0x2000
#define DECON_VIDCON2			0x2004
#define DECON_VIDCON3			0x2008
#define DECON_VIDCON4			0x200C
#define DECON_VIDTCON2			0x2028
#define DECON_FRAME_SIZE		0x2038
#define DECON_LINECNT_OP_THRESHOLD	0x203C
#define DECON_TRIGCON			0x2040
#define DECON_TRIGSKIP			0x2050
#define DECON_CRCRDATA			0x20B0
#define DECON_CRCCTRL			0x20B4

/* Exynos5430 DECON */
#define DECON_VIDTCON0			0x2020
#define DECON_VIDTCON1			0x2024

/* Exynos5433 DECON */
#define DECON_VIDTCON00			0x2010
#define DECON_VIDTCON01			0x2014
#define DECON_VIDTCON10			0x2018
#define DECON_VIDTCON11			0x201C

/* Exynos543X DECON Internal */
#define DECON_W013DSTREOCON		0x0320
#define DECON_W233DSTREOCON		0x0324
#define DECON_FRAMEFIFO_REG0		0x0500
#define DECON_ENHANCER_CTRL		0x2100

/* Exynos543X DECON TV */
#define DECON_VCLKCON0			0x0014
#define DECON_VIDINTCON2		0x0228
#define DECON_VIDINTCON3		0x022C

/* VIDCON0 */
#define VIDCON0_SWRESET			(1 << 28)
#define VIDCON0_CLKVALUP		(1 << 14)
#define VIDCON0_VLCKFREE		(1 << 5)
#define VIDCON0_STOP_STATUS		(1 << 2)
#define VIDCON0_ENVID			(1 << 1)
#define VIDCON0_ENVID_F			(1 << 0)

/* VIDOUTCON0 */
#define VIDOUT_LCD_ON			(1 << 24)
#define VIDOUT_IF_F_MASK		(0x3 << 20)
#define VIDOUT_RGB_IF			(0x0 << 20)
#define VIDOUT_COMMAND_IF		(0x2 << 20)

/* WINCONx */
#define WINCONx_HAWSWP_F		(1 << 16)
#define WINCONx_WSWP_F			(1 << 15)
#define WINCONx_BURSTLEN_MASK		(0x3 << 10)
#define WINCONx_BURSTLEN_16WORD		(0x0 << 10)
#define WINCONx_BURSTLEN_8WORD		(0x1 << 10)
#define WINCONx_BURSTLEN_4WORD		(0x2 << 10)
#define WINCONx_BLD_PIX_F		(1 << 6)
#define WINCONx_BPPMODE_MASK		(0xf << 2)
#define WINCONx_BPPMODE_16BPP_565	(0x5 << 2)
#define WINCONx_BPPMODE_16BPP_A1555	(0x6 << 2)
#define WINCONx_BPPMODE_16BPP_I1555	(0x7 << 2)
#define WINCONx_BPPMODE_24BPP_888	(0xb << 2)
#define WINCONx_BPPMODE_24BPP_A1887	(0xc << 2)
#define WINCONx_BPPMODE_25BPP_A1888	(0xd << 2)
#define WINCONx_BPPMODE_32BPP_A8888	(0xd << 2)
#define WINCONx_BPPMODE_16BPP_A4444	(0xe << 2)
#define WINCONx_ALPHA_SEL_F		(1 << 1)
#define WINCONx_ENWIN_F			(1 << 0)

/* SHADOWCON */
#define SHADOWCON_Wx_PROTECT(n)		(1 << (10 + (n)))

/* VIDOSDxD */
#define VIDOSD_Wx_ALPHA_R_F(n)		(((n) & 0xff) << 16)
#define VIDOSD_Wx_ALPHA_G_F(n)		(((n) & 0xff) << 8)
#define VIDOSD_Wx_ALPHA_B_F(n)		(((n) & 0xff) << 0)

/* VIDINTCON0 */
#define VIDINTCON0_FRAMEDONE		(1 << 17)
#define VIDINTCON0_INTFRMEN		(1 << 12)
#define VIDINTCON0_INTEN		(1 << 0)

/* VIDINTCON1 */
#define VIDINTCON1_INTFRMDONEPEND	(1 << 2)
#define VIDINTCON1_INTFRMPEND		(1 << 1)
#define VIDINTCON1_INTFIFOPEND		(1 << 0)

/* DECON_CMU */
#define CMU_CLKGAGE_MODE_SFR_F		(1 << 1)
#define CMU_CLKGAGE_MODE_MEM_F		(1 << 0)

/* DECON_UPDATE */
#define STANDALONE_UPDATE_F		(1 << 0)

/* DECON_VIDCON1 */
#define VIDCON1_VCLK_MASK		(0x3 << 9)
#define VIDCON1_VCLK_RUN_VDEN_DISABLE	(0x3 << 9)
#define VIDCON1_VCLK_HOLD		(0x0 << 9)
#define VIDCON1_VCLK_RUN		(0x1 << 9)


/* DECON_VIDTCON00 */
#define VIDTCON00_VBPD_F(x)		(((x) & 0xfff) << 16)
#define VIDTCON00_VFPD_F(x)		((x) & 0xfff)

/* DECON_VIDTCON01 */
#define VIDTCON01_VSPW_F(x)		(((x) & 0xfff) << 16)

/* DECON_VIDTCON10 */
#define VIDTCON10_HBPD_F(x)		(((x) & 0xfff) << 16)
#define VIDTCON10_HFPD_F(x)		((x) & 0xfff)

/* DECON_VIDTCON11 */
#define VIDTCON11_HSPW_F(x)		(((x) & 0xfff) << 16)

/* DECON_VIDTCON2 */
#define VIDTCON2_LINEVAL(x)		(((x) & 0xfff) << 16)
#define VIDTCON2_HOZVAL(x)		((x) & 0xfff)

/* TRIGCON */
#define TRIGCON_TRIGEN_PER_F		(1 << 31)
#define TRIGCON_TRIGEN_F		(1 << 30)
#define TRIGCON_TE_AUTO_MASK		(1 << 29)
#define TRIGCON_WB_SWTRIGCMD		(1 << 28)
#define TRIGCON_SWTRIGCMD_W4BUF		(1 << 26)
#define TRIGCON_TRIGMODE_W4BUF		(1 << 25)
#define TRIGCON_SWTRIGCMD_W3BUF		(1 << 21)
#define TRIGCON_TRIGMODE_W3BUF		(1 << 20)
#define TRIGCON_SWTRIGCMD_W2BUF		(1 << 16)
#define TRIGCON_TRIGMODE_W2BUF		(1 << 15)
#define TRIGCON_SWTRIGCMD_W1BUF		(1 << 11)
#define TRIGCON_TRIGMODE_W1BUF		(1 << 10)
#define TRIGCON_SWTRIGCMD_W0BUF		(1 << 6)
#define TRIGCON_TRIGMODE_W0BUF		(1 << 5)
#define TRIGCON_HWTRIGMASK		(1 << 4)
#define TRIGCON_HWTRIGEN		(1 << 3)
#define TRIGCON_HWTRIG_INV		(1 << 2)
#define TRIGCON_SWTRIGCMD		(1 << 1)
#define TRIGCON_SWTRIGEN		(1 << 0)

/* DECON_CRCCTRL */
#define CRCCTRL_CRCCLKEN		(0x1 << 2)
#define CRCCTRL_CRCSTART_F		(0x1 << 1)
#define CRCCTRL_CRCEN			(0x1 << 0)
#define CRCCTRL_MASK			(0x7)

#endif /* EXYNOS_REGS_DECON_H */
