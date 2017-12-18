/**
 * core.h - DesignWare USB3 DRD Core Header
 *
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - http://www.ti.com
 *
 * Authors: Felipe Balbi <balbi@ti.com>,
 *	    Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_USB_DWC3_CORE_H
#define __DRIVERS_USB_DWC3_CORE_H

#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/debugfs.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/ulpi/interface.h>

#include <linux/phy/phy.h>

#define DWC3_MSG_MAX	500

/* Global constants */
#define DWC3_ZLP_BUF_SIZE	1024	/* size of a superspeed bulk */
#define DWC3_EP0_BOUNCE_SIZE	512
#define DWC3_ENDPOINTS_NUM	32
#define DWC3_XHCI_RESOURCES_NUM	2

#define DWC3_SCRATCHBUF_SIZE	4096	/* each buffer is assumed to be 4KiB */
#define DWC3_EVENT_SIZE		4	/* bytes */
#define DWC3_EVENT_MAX_NUM	64	/* 2 events/endpoint */
#define DWC3_EVENT_BUFFERS_SIZE	(DWC3_EVENT_SIZE * DWC3_EVENT_MAX_NUM)
#define DWC3_EVENT_TYPE_MASK	0xfe

#define DWC3_EVENT_TYPE_DEV	0
#define DWC3_EVENT_TYPE_CARKIT	3
#define DWC3_EVENT_TYPE_I2C	4

#define DWC3_DEVICE_EVENT_DISCONNECT		0
#define DWC3_DEVICE_EVENT_RESET			1
#define DWC3_DEVICE_EVENT_CONNECT_DONE		2
#define DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE	3
#define DWC3_DEVICE_EVENT_WAKEUP		4
#define DWC3_DEVICE_EVENT_HIBER_REQ		5
#define DWC3_DEVICE_EVENT_EOPF			6
#define DWC3_DEVICE_EVENT_SOF			7
#define DWC3_DEVICE_EVENT_ERRATIC_ERROR		9
#define DWC3_DEVICE_EVENT_CMD_CMPL		10
#define DWC3_DEVICE_EVENT_OVERFLOW		11

#define DWC3_GEVNTCOUNT_MASK	0xfffc
#define DWC3_GSNPSID_MASK	0xffff0000
#define DWC3_GSNPSREV_MASK	0xffff

/* DWC3 registers memory space boundries */
#define DWC3_XHCI_REGS_START		0x0
#define DWC3_XHCI_REGS_END		0x7fff
#define DWC3_GLOBALS_REGS_START		0xc100
#define DWC3_GLOBALS_REGS_END		0xc6ff
#define DWC3_DEVICE_REGS_START		0xc700
#define DWC3_DEVICE_REGS_END		0xcbff
#define DWC3_OTG_REGS_START		0xcc00
#define DWC3_OTG_REGS_END		0xccff

/* Global Registers */
#define DWC3_GSBUSCFG0		0xc100
#define DWC3_GSBUSCFG1		0xc104
#define DWC3_GTXTHRCFG		0xc108
#define DWC3_GRXTHRCFG		0xc10c
#define DWC3_GCTL		0xc110
#define DWC3_GEVTEN		0xc114
#define DWC3_GSTS		0xc118
#define DWC3_GUCTL1		0xc11c
#define DWC3_GSNPSID		0xc120
#define DWC3_GGPIO		0xc124
#define DWC3_GUID		0xc128
#define DWC3_GUCTL		0xc12c
#define DWC3_GBUSERRADDR0	0xc130
#define DWC3_GBUSERRADDR1	0xc134
#define DWC3_GPRTBIMAP0		0xc138
#define DWC3_GPRTBIMAP1		0xc13c
#define DWC3_GHWPARAMS0		0xc140
#define DWC3_GHWPARAMS1		0xc144
#define DWC3_GHWPARAMS2		0xc148
#define DWC3_GHWPARAMS3		0xc14c
#define DWC3_GHWPARAMS4		0xc150
#define DWC3_GHWPARAMS5		0xc154
#define DWC3_GHWPARAMS6		0xc158
#define DWC3_GHWPARAMS7		0xc15c
#define DWC3_GDBGFIFOSPACE	0xc160
#define DWC3_GDBGLTSSM		0xc164
#define DWC3_GPRTBIMAP_HS0	0xc180
#define DWC3_GPRTBIMAP_HS1	0xc184
#define DWC3_GPRTBIMAP_FS0	0xc188
#define DWC3_GPRTBIMAP_FS1	0xc18c
#define DWC3_GUCTL2		0xc19c

#define DWC3_VER_NUMBER		0xc1a0
#define DWC3_VER_TYPE		0xc1a4

#define DWC3_GUSB2PHYCFG(n)	(0xc200 + (n * 0x04))
#define DWC3_GUSB2I2CCTL(n)	(0xc240 + (n * 0x04))

#define DWC3_GUSB2PHYACC(n)	(0xc280 + (n * 0x04))

#define DWC3_GUSB3PIPECTL(n)	(0xc2c0 + (n * 0x04))

#define DWC3_GTXFIFOSIZ(n)	(0xc300 + (n * 0x04))
#define DWC3_GRXFIFOSIZ(n)	(0xc380 + (n * 0x04))

#define DWC3_GEVNTADRLO(n)	(0xc400 + (n * 0x10))
#define DWC3_GEVNTADRHI(n)	(0xc404 + (n * 0x10))
#define DWC3_GEVNTSIZ(n)	(0xc408 + (n * 0x10))
#define DWC3_GEVNTCOUNT(n)	(0xc40c + (n * 0x10))

#define DWC3_GHWPARAMS8		0xc600
#define DWC3_GFLADJ		0xc630

/* Device Registers */
#define DWC3_DCFG		0xc700
#define DWC3_DCTL		0xc704
#define DWC3_DEVTEN		0xc708
#define DWC3_DSTS		0xc70c
#define DWC3_DGCMDPAR		0xc710
#define DWC3_DGCMD		0xc714
#define DWC3_DALEPENA		0xc720

#define DWC3_DEP_BASE(n)	(0xc800 + (n * 0x10))
#define DWC3_DEPCMDPAR2		0x00
#define DWC3_DEPCMDPAR1		0x04
#define DWC3_DEPCMDPAR0		0x08
#define DWC3_DEPCMD		0x0c

/* OTG Registers */
#define DWC3_OCFG		0xcc00
#define DWC3_OCTL		0xcc04
#define DWC3_OEVT		0xcc08
#define DWC3_OEVTEN		0xcc0C
#define DWC3_OSTS		0xcc10

/* Bit fields */

/* Global Status Register */
#define DWC3_GSTS_CUR_MODE			(1 << 0)

/* Global Debug Queue/FIFO Space Available Register */
#define DWC3_GDBGFIFOSPACE_NUM(n)	((n) & 0x1f)
#define DWC3_GDBGFIFOSPACE_TYPE(n)	(((n) << 5) & 0x1e0)
#define DWC3_GDBGFIFOSPACE_SPACE_AVAILABLE(n) (((n) >> 16) & 0xffff)

#define DWC3_TXFIFOQ		1
#define DWC3_RXFIFOQ		3
#define DWC3_TXREQQ		5
#define DWC3_RXREQQ		7
#define DWC3_RXINFOQ		9
#define DWC3_DESCFETCHQ		13
#define DWC3_EVENTQ		15

/* Global SoC Bus Configuration Register */
#define DWC3_GSBUSCFG0_DATRDREQINFO	(0xf << 28)
#define DWC3_GSBUSCFG0_DESRDREQINFO	(0xf << 24)
#define DWC3_GSBUSCFG0_DATWRREQINFO	(0xf << 20)
#define DWC3_GSBUSCFG0_DESWRREQINFO	(0xf << 16)

/* Global RX Threshold Configuration Register */
#define DWC3_GRXTHRCFG_MAXRXBURSTSIZE(n) (((n) & 0x1f) << 19)
#define DWC3_GRXTHRCFG_RXPKTCNT(n) (((n) & 0xf) << 24)
#define DWC3_GRXTHRCFG_PKTCNTSEL (1 << 29)

/* Global Configuration Register */
#define DWC3_GCTL_PWRDNSCALE(n)	((n) << 19)
#define DWC3_GCTL_U2RSTECN	(1 << 16)
#define DWC3_GCTL_RAMCLKSEL(x)	(((x) & DWC3_GCTL_CLK_MASK) << 6)
#define DWC3_GCTL_CLK_BUS	(0)
#define DWC3_GCTL_CLK_PIPE	(1)
#define DWC3_GCTL_CLK_PIPEHALF	(2)
#define DWC3_GCTL_CLK_MASK	(3)

#define DWC3_GCTL_PRTCAP(n)	(((n) & (3 << 12)) >> 12)
#define DWC3_GCTL_PRTCAPDIR(n)	((n) << 12)
#define DWC3_GCTL_PRTCAP_HOST	1
#define DWC3_GCTL_PRTCAP_DEVICE	2
#define DWC3_GCTL_PRTCAP_OTG	3

#define DWC3_GCTL_CORESOFTRESET		(1 << 11)
#define DWC3_GCTL_SOFITPSYNC		(1 << 10)
#define DWC3_GCTL_SCALEDOWN(n)		((n) << 4)
#define DWC3_GCTL_SCALEDOWN_MASK	DWC3_GCTL_SCALEDOWN(3)
#define DWC3_GCTL_DISSCRAMBLE		(1 << 3)
#define DWC3_GCTL_U2EXIT_LFPS		(1 << 2)
#define DWC3_GCTL_GBLHIBERNATIONEN	(1 << 1)
#define DWC3_GCTL_DSBLCLKGTNG		(1 << 0)

/* Global USB2 PHY Configuration Register */
#define DWC3_GUSB2PHYCFG_PHYSOFTRST	(1 << 31)
#define DWC3_GUSB2PHYCFG_U2_FREECLK_EXISTS	(1 << 30)
#define DWC3_GUSB2PHYCFG_SUSPHY		(1 << 6)
#define DWC3_GUSB2PHYCFG_ULPI_UTMI	(1 << 4)
#define DWC3_GUSB2PHYCFG_ENBLSLPM	(1 << 8)
#define DWC3_GUSB2PHYCFG_PHYIF(n)	(n << 3)
#define DWC3_GUSB2PHYCFG_PHYIF_MASK	DWC3_GUSB2PHYCFG_PHYIF(1)
#define DWC3_GUSB2PHYCFG_USBTRDTIM(n)	(n << 10)
#define DWC3_GUSB2PHYCFG_USBTRDTIM_MASK	DWC3_GUSB2PHYCFG_USBTRDTIM(0xf)
#define USBTRDTIM_UTMI_8_BIT		9
#define USBTRDTIM_UTMI_16_BIT		5
#define UTMI_PHYIF_16_BIT		1
#define UTMI_PHYIF_8_BIT		0

/* Global USB2 PHY Vendor Control Register */
#define DWC3_GUSB2PHYACC_NEWREGREQ	(1 << 25)
#define DWC3_GUSB2PHYACC_BUSY		(1 << 23)
#define DWC3_GUSB2PHYACC_WRITE		(1 << 22)
#define DWC3_GUSB2PHYACC_ADDR(n)	(n << 16)
#define DWC3_GUSB2PHYACC_EXTEND_ADDR(n)	(n << 8)
#define DWC3_GUSB2PHYACC_DATA(n)	(n & 0xff)

/* Global USB3 PIPE Control Register */
#define DWC3_GUSB3PIPECTL_PHYSOFTRST	(1 << 31)
#define DWC3_GUSB3PIPECTL_U2SSINP3OK	(1 << 29)
#define DWC3_GUSB3PIPECTL_DISRXDETINP3	(1 << 28)
#define DWC3_GUSB3PIPECTL_REQP1P2P3	(1 << 24)
#define DWC3_GUSB3PIPECTL_DEP1P2P3(n)	((n) << 19)
#define DWC3_GUSB3PIPECTL_DEP1P2P3_MASK	DWC3_GUSB3PIPECTL_DEP1P2P3(7)
#define DWC3_GUSB3PIPECTL_DEP1P2P3_EN	DWC3_GUSB3PIPECTL_DEP1P2P3(1)
#define DWC3_GUSB3PIPECTL_DEPOCHANGE	(1 << 18)
#define DWC3_GUSB3PIPECTL_SUSPHY	(1 << 17)
#define DWC3_GUSB3PIPECTL_LFPSFILT	(1 << 9)
#define DWC3_GUSB3PIPECTL_RX_DETOPOLL	(1 << 8)
#define DWC3_GUSB3PIPECTL_TX_DEEPH_MASK	DWC3_GUSB3PIPECTL_TX_DEEPH(3)
#define DWC3_GUSB3PIPECTL_TX_DEEPH(n)	((n) << 1)

/* Global TX Fifo Size Register */
#define DWC3_GTXFIFOSIZ_TXFDEF(n)	((n) & 0xffff)
#define DWC3_GTXFIFOSIZ_TXFSTADDR(n)	((n) & 0xffff0000)

/* Global Event Size Registers */
#define DWC3_GEVNTSIZ_INTMASK		(1 << 31)
#define DWC3_GEVNTSIZ_SIZE(n)		((n) & 0xffff)

/* Global HWPARAMS0 Register */
#define DWC3_GHWPARAMS0_MODE(n)		((n) & 0x3)
#define DWC3_GHWPARAMS0_MODE_GADGET	0
#define DWC3_GHWPARAMS0_MODE_HOST	1
#define DWC3_GHWPARAMS0_MODE_DRD	2
#define DWC3_GHWPARAMS0_MBUS_TYPE(n)	(((n) >> 3) & 0x7)
#define DWC3_GHWPARAMS0_SBUS_TYPE(n)	(((n) >> 6) & 0x3)
#define DWC3_GHWPARAMS0_MDWIDTH(n)	(((n) >> 8) & 0xff)
#define DWC3_GHWPARAMS0_SDWIDTH(n)	(((n) >> 16) & 0xff)
#define DWC3_GHWPARAMS0_AWIDTH(n)	(((n) >> 24) & 0xff)

/* Global HWPARAMS1 Register */
#define DWC3_GHWPARAMS1_EN_PWROPT(n)	(((n) & (3 << 24)) >> 24)
#define DWC3_GHWPARAMS1_EN_PWROPT_NO	0
#define DWC3_GHWPARAMS1_EN_PWROPT_CLK	1
#define DWC3_GHWPARAMS1_EN_PWROPT_HIB	2
#define DWC3_GHWPARAMS1_PWROPT(n)	((n) << 24)
#define DWC3_GHWPARAMS1_PWROPT_MASK	DWC3_GHWPARAMS1_PWROPT(3)

/* Global HWPARAMS3 Register */
#define DWC3_GHWPARAMS3_SSPHY_IFC(n)		((n) & 3)
#define DWC3_GHWPARAMS3_SSPHY_IFC_DIS		0
#define DWC3_GHWPARAMS3_SSPHY_IFC_GEN1		1
#define DWC3_GHWPARAMS3_SSPHY_IFC_GEN2		2 /* DWC_usb31 only */
#define DWC3_GHWPARAMS3_HSPHY_IFC(n)		(((n) & (3 << 2)) >> 2)
#define DWC3_GHWPARAMS3_HSPHY_IFC_DIS		0
#define DWC3_GHWPARAMS3_HSPHY_IFC_UTMI		1
#define DWC3_GHWPARAMS3_HSPHY_IFC_ULPI		2
#define DWC3_GHWPARAMS3_HSPHY_IFC_UTMI_ULPI	3
#define DWC3_GHWPARAMS3_FSPHY_IFC(n)		(((n) & (3 << 4)) >> 4)
#define DWC3_GHWPARAMS3_FSPHY_IFC_DIS		0
#define DWC3_GHWPARAMS3_FSPHY_IFC_ENA		1

/* Global HWPARAMS4 Register */
#define DWC3_GHWPARAMS4_HIBER_SCRATCHBUFS(n)	(((n) & (0x0f << 13)) >> 13)
#define DWC3_MAX_HIBER_SCRATCHBUFS		15

/* Global HWPARAMS6 Register */
#define DWC3_GHWPARAMS6_EN_FPGA			(1 << 7)

/* Global HWPARAMS7 Register */
#define DWC3_GHWPARAMS7_RAM1_DEPTH(n)	((n) & 0xffff)
#define DWC3_GHWPARAMS7_RAM2_DEPTH(n)	(((n) >> 16) & 0xffff)

/* Global Frame Length Adjustment Register */
#define DWC3_GFLADJ_30MHZ_SDBND_SEL		(1 << 7)
#define DWC3_GFLADJ_30MHZ_MASK			0x3f
#define DWC3_GFLADJ_REFCLK_FLADJ		(0x3fff << 8)

/* Global User Control Register 1 */
#define DWC3_GUCTL1_RESUME_QUIRK		(1 << 10)
#define DWC3_GUCTL1_IPD_QUIRK			(1 << 9)

/* Global User Control Register 2 */
#define DWC3_GUCTL2_RST_ACTBITLATER		(1 << 14)

/* Device Configuration Register */
#define DWC3_DCFG_DEVADDR(addr)	((addr) << 3)
#define DWC3_DCFG_DEVADDR_MASK	DWC3_DCFG_DEVADDR(0x7f)

#define DWC3_DCFG_SPEED_MASK	(7 << 0)
#define DWC3_DCFG_SUPERSPEED_PLUS (5 << 0)  /* DWC_usb31 only */
#define DWC3_DCFG_SUPERSPEED	(4 << 0)
#define DWC3_DCFG_HIGHSPEED	(0 << 0)
#define DWC3_DCFG_FULLSPEED2	(1 << 0)
#define DWC3_DCFG_LOWSPEED	(2 << 0)
#define DWC3_DCFG_FULLSPEED1	(3 << 0)

#define DWC3_DCFG_NUMP_SHIFT	17
#define DWC3_DCFG_NUMP(n)	(((n) >> DWC3_DCFG_NUMP_SHIFT) & 0x1f)
#define DWC3_DCFG_NUMP_MASK	(0x1f << DWC3_DCFG_NUMP_SHIFT)
#define DWC3_DCFG_LPM_CAP	(1 << 22)

/* Device Control Register */
#define DWC3_DCTL_RUN_STOP	(1 << 31)
#define DWC3_DCTL_CSFTRST	(1 << 30)
#define DWC3_DCTL_LSFTRST	(1 << 29)

#define DWC3_DCTL_HIRD_THRES_MASK	(0x1f << 24)
#define DWC3_DCTL_HIRD_THRES(n)	((n) << 24)

#define DWC3_DCTL_APPL1RES	(1 << 23)

/* These apply for core versions 1.87a and earlier */
#define DWC3_DCTL_TRGTULST_MASK		(0x0f << 17)
#define DWC3_DCTL_TRGTULST(n)		((n) << 17)
#define DWC3_DCTL_TRGTULST_U2		(DWC3_DCTL_TRGTULST(2))
#define DWC3_DCTL_TRGTULST_U3		(DWC3_DCTL_TRGTULST(3))
#define DWC3_DCTL_TRGTULST_SS_DIS	(DWC3_DCTL_TRGTULST(4))
#define DWC3_DCTL_TRGTULST_RX_DET	(DWC3_DCTL_TRGTULST(5))
#define DWC3_DCTL_TRGTULST_SS_INACT	(DWC3_DCTL_TRGTULST(6))

/* These apply for core versions 1.94a and later */
#define DWC3_DCTL_LPM_ERRATA_MASK	DWC3_DCTL_LPM_ERRATA(0xf)
#define DWC3_DCTL_LPM_ERRATA(n)		((n) << 20)

#define DWC3_DCTL_KEEP_CONNECT		(1 << 19)
#define DWC3_DCTL_L1_HIBER_EN		(1 << 18)
#define DWC3_DCTL_CRS			(1 << 17)
#define DWC3_DCTL_CSS			(1 << 16)

#define DWC3_DCTL_INITU2ENA		(1 << 12)
#define DWC3_DCTL_ACCEPTU2ENA		(1 << 11)
#define DWC3_DCTL_INITU1ENA		(1 << 10)
#define DWC3_DCTL_ACCEPTU1ENA		(1 << 9)
#define DWC3_DCTL_TSTCTRL_MASK		(0xf << 1)

#define DWC3_DCTL_ULSTCHNGREQ_MASK	(0x0f << 5)
#define DWC3_DCTL_ULSTCHNGREQ(n) (((n) << 5) & DWC3_DCTL_ULSTCHNGREQ_MASK)

#define DWC3_DCTL_ULSTCHNG_NO_ACTION	(DWC3_DCTL_ULSTCHNGREQ(0))
#define DWC3_DCTL_ULSTCHNG_SS_DISABLED	(DWC3_DCTL_ULSTCHNGREQ(4))
#define DWC3_DCTL_ULSTCHNG_RX_DETECT	(DWC3_DCTL_ULSTCHNGREQ(5))
#define DWC3_DCTL_ULSTCHNG_SS_INACTIVE	(DWC3_DCTL_ULSTCHNGREQ(6))
#define DWC3_DCTL_ULSTCHNG_RECOVERY	(DWC3_DCTL_ULSTCHNGREQ(8))
#define DWC3_DCTL_ULSTCHNG_COMPLIANCE	(DWC3_DCTL_ULSTCHNGREQ(10))
#define DWC3_DCTL_ULSTCHNG_LOOPBACK	(DWC3_DCTL_ULSTCHNGREQ(11))

/* Device Event Enable Register */
#define DWC3_DEVTEN_VNDRDEVTSTRCVEDEN	(1 << 12)
#define DWC3_DEVTEN_EVNTOVERFLOWEN	(1 << 11)
#define DWC3_DEVTEN_CMDCMPLTEN		(1 << 10)
#define DWC3_DEVTEN_ERRTICERREN		(1 << 9)
#define DWC3_DEVTEN_SOFEN		(1 << 7)
#define DWC3_DEVTEN_EOPFEN		(1 << 6)
#define DWC3_DEVTEN_HIBERNATIONREQEVTEN	(1 << 5)
#define DWC3_DEVTEN_WKUPEVTEN		(1 << 4)
#define DWC3_DEVTEN_ULSTCNGEN		(1 << 3)
#define DWC3_DEVTEN_CONNECTDONEEN	(1 << 2)
#define DWC3_DEVTEN_USBRSTEN		(1 << 1)
#define DWC3_DEVTEN_DISCONNEVTEN	(1 << 0)

/* Device Status Register */
#define DWC3_DSTS_DCNRD			(1 << 29)

/* This applies for core versions 1.87a and earlier */
#define DWC3_DSTS_PWRUPREQ		(1 << 24)

/* These apply for core versions 1.94a and later */
#define DWC3_DSTS_RSS			(1 << 25)
#define DWC3_DSTS_SSS			(1 << 24)

#define DWC3_DSTS_COREIDLE		(1 << 23)
#define DWC3_DSTS_DEVCTRLHLT		(1 << 22)

#define DWC3_DSTS_USBLNKST_MASK		(0x0f << 18)
#define DWC3_DSTS_USBLNKST(n)		(((n) & DWC3_DSTS_USBLNKST_MASK) >> 18)

#define DWC3_DSTS_RXFIFOEMPTY		(1 << 17)

#define DWC3_DSTS_SOFFN_MASK		(0x3fff << 3)
#define DWC3_DSTS_SOFFN(n)		(((n) & DWC3_DSTS_SOFFN_MASK) >> 3)

#define DWC3_DSTS_CONNECTSPD		(7 << 0)

#define DWC3_DSTS_SUPERSPEED_PLUS	(5 << 0) /* DWC_usb31 only */
#define DWC3_DSTS_SUPERSPEED		(4 << 0)
#define DWC3_DSTS_HIGHSPEED		(0 << 0)
#define DWC3_DSTS_FULLSPEED2		(1 << 0)
#define DWC3_DSTS_LOWSPEED		(2 << 0)
#define DWC3_DSTS_FULLSPEED1		(3 << 0)

/* Device Generic Command Register */
#define DWC3_DGCMD_SET_LMP		0x01
#define DWC3_DGCMD_SET_PERIODIC_PAR	0x02
#define DWC3_DGCMD_XMIT_FUNCTION	0x03

/* These apply for core versions 1.94a and later */
#define DWC3_DGCMD_SET_SCRATCHPAD_ADDR_LO	0x04
#define DWC3_DGCMD_SET_SCRATCHPAD_ADDR_HI	0x05

#define DWC3_DGCMD_SELECTED_FIFO_FLUSH	0x09
#define DWC3_DGCMD_ALL_FIFO_FLUSH	0x0a
#define DWC3_DGCMD_SET_ENDPOINT_NRDY	0x0c
#define DWC3_DGCMD_RUN_SOC_BUS_LOOPBACK	0x10

#define DWC3_DGCMD_STATUS(n)		(((n) >> 12) & 0x0F)
#define DWC3_DGCMD_CMDACT		(1 << 10)
#define DWC3_DGCMD_CMDIOC		(1 << 8)

/* Device Generic Command Parameter Register */
#define DWC3_DGCMDPAR_FORCE_LINKPM_ACCEPT	(1 << 0)
#define DWC3_DGCMDPAR_FIFO_NUM(n)		((n) << 0)
#define DWC3_DGCMDPAR_RX_FIFO			(0 << 5)
#define DWC3_DGCMDPAR_TX_FIFO			(1 << 5)
#define DWC3_DGCMDPAR_LOOPBACK_DIS		(0 << 0)
#define DWC3_DGCMDPAR_LOOPBACK_ENA		(1 << 0)

/* Device Endpoint Command Register */
#define DWC3_DEPCMD_PARAM_SHIFT		16
#define DWC3_DEPCMD_PARAM(x)		((x) << DWC3_DEPCMD_PARAM_SHIFT)
#define DWC3_DEPCMD_GET_RSC_IDX(x)	(((x) >> DWC3_DEPCMD_PARAM_SHIFT) & 0x7f)
#define DWC3_DEPCMD_STATUS(x)		(((x) >> 12) & 0x0F)
#define DWC3_DEPCMD_HIPRI_FORCERM	(1 << 11)
#define DWC3_DEPCMD_CLEARPENDIN		(1 << 11)
#define DWC3_DEPCMD_CMDACT		(1 << 10)
#define DWC3_DEPCMD_CMDIOC		(1 << 8)

#define DWC3_DEPCMD_DEPSTARTCFG		(0x09 << 0)
#define DWC3_DEPCMD_ENDTRANSFER		(0x08 << 0)
#define DWC3_DEPCMD_UPDATETRANSFER	(0x07 << 0)
#define DWC3_DEPCMD_STARTTRANSFER	(0x06 << 0)
#define DWC3_DEPCMD_CLEARSTALL		(0x05 << 0)
#define DWC3_DEPCMD_SETSTALL		(0x04 << 0)
/* This applies for core versions 1.90a and earlier */
#define DWC3_DEPCMD_GETSEQNUMBER	(0x03 << 0)
/* This applies for core versions 1.94a and later */
#define DWC3_DEPCMD_GETEPSTATE		(0x03 << 0)
#define DWC3_DEPCMD_SETTRANSFRESOURCE	(0x02 << 0)
#define DWC3_DEPCMD_SETEPCONFIG		(0x01 << 0)

/* The EP number goes 0..31 so ep0 is always out and ep1 is always in */
#define DWC3_DALEPENA_EP(n)		(1 << n)

#define DWC3_DEPCMD_TYPE_CONTROL	0
#define DWC3_DEPCMD_TYPE_ISOC		1
#define DWC3_DEPCMD_TYPE_BULK		2
#define DWC3_DEPCMD_TYPE_INTR		3

/* Structures */

struct dwc3_trb;

/**
 * struct dwc3_event_buffer - Software event buffer representation
 * @buf: _THE_ buffer
 * @length: size of this buffer
 * @lpos: event offset
 * @count: cache of last read event count register
 * @flags: flags related to this event buffer
 * @dma: dma_addr_t
 * @dwc: pointer to DWC controller
 */
struct dwc3_event_buffer {
	void			*buf;
	unsigned		length;
	unsigned int		lpos;
	unsigned int		count;
	unsigned int		flags;

#define DWC3_EVENT_PENDING	BIT(0)

	dma_addr_t		dma;

	struct dwc3		*dwc;
};

#define DWC3_EP_FLAG_STALLED	(1 << 0)
#define DWC3_EP_FLAG_WEDGED	(1 << 1)

#define DWC3_EP_DIRECTION_TX	true
#define DWC3_EP_DIRECTION_RX	false

#define DWC3_TRB_NUM		256

/**
 * struct dwc3_ep - device side endpoint representation
 * @endpoint: usb endpoint
 * @pending_list: list of pending requests for this endpoint
 * @started_list: list of started requests on this endpoint
 * @lock: spinlock for endpoint request queue traversal
 * @regs: pointer to first endpoint register
 * @trb_pool: array of transaction buffers
 * @trb_pool_dma: dma address of @trb_pool
 * @trb_enqueue: enqueue 'pointer' into TRB array
 * @trb_dequeue: dequeue 'pointer' into TRB array
 * @desc: usb_endpoint_descriptor pointer
 * @dwc: pointer to DWC controller
 * @saved_state: ep state saved during hibernation
 * @flags: endpoint flags (wedged, stalled, ...)
 * @number: endpoint number (1 - 15)
 * @type: set to bmAttributes & USB_ENDPOINT_XFERTYPE_MASK
 * @resource_index: Resource transfer index
 * @interval: the interval on which the ISOC transfer is started
 * @allocated_requests: number of requests allocated
 * @queued_requests: number of requests queued for transfer
 * @name: a human readable name e.g. ep1out-bulk
 * @direction: true for TX, false for RX
 * @stream_capable: true when streams are enabled
 */
struct dwc3_ep {
	struct usb_ep		endpoint;
	struct list_head	pending_list;
	struct list_head	started_list;

	spinlock_t		lock;
	void __iomem		*regs;

	struct dwc3_trb		*trb_pool;
	dma_addr_t		trb_pool_dma;
	const struct usb_ss_ep_comp_descriptor *comp_desc;
	struct dwc3		*dwc;

	u32			saved_state;
	unsigned		flags;
#define DWC3_EP_ENABLED		(1 << 0)
#define DWC3_EP_STALL		(1 << 1)
#define DWC3_EP_WEDGE		(1 << 2)
#define DWC3_EP_BUSY		(1 << 4)
#define DWC3_EP_PENDING_REQUEST	(1 << 5)
#define DWC3_EP_MISSED_ISOC	(1 << 6)

	/* This last one is specific to EP0 */
#define DWC3_EP0_DIR_IN		(1 << 31)

	/*
	 * IMPORTANT: we *know* we have 256 TRBs in our @trb_pool, so we will
	 * use a u8 type here. If anybody decides to increase number of TRBs to
	 * anything larger than 256 - I can't see why people would want to do
	 * this though - then this type needs to be changed.
	 *
	 * By using u8 types we ensure that our % operator when incrementing
	 * enqueue and dequeue get optimized away by the compiler.
	 */
	u8			trb_enqueue;
	u8			trb_dequeue;

	u8			number;
	u8			type;
	u8			resource_index;
	u32			allocated_requests;
	u32			queued_requests;
	u32			interval;

	char			name[20];

	unsigned		direction:1;
	unsigned		stream_capable:1;
#define STREAM_TIMEOUT		50
	struct timer_list	stream_timeout_timer;
};

enum dwc3_phy {
	DWC3_PHY_UNKNOWN = 0,
	DWC3_PHY_USB3,
	DWC3_PHY_USB2,
};

enum dwc3_ep0_next {
	DWC3_EP0_UNKNOWN = 0,
	DWC3_EP0_COMPLETE,
	DWC3_EP0_NRDY_DATA,
	DWC3_EP0_NRDY_STATUS,
};

enum dwc3_ep0_state {
	EP0_UNCONNECTED		= 0,
	EP0_SETUP_PHASE,
	EP0_DATA_PHASE,
	EP0_STATUS_PHASE,
};

enum dwc3_link_state {
	/* In SuperSpeed */
	DWC3_LINK_STATE_U0		= 0x00, /* in HS, means ON */
	DWC3_LINK_STATE_U1		= 0x01,
	DWC3_LINK_STATE_U2		= 0x02, /* in HS, means SLEEP */
	DWC3_LINK_STATE_U3		= 0x03, /* in HS, means SUSPEND */
	DWC3_LINK_STATE_SS_DIS		= 0x04,
	DWC3_LINK_STATE_RX_DET		= 0x05, /* in HS, means Early Suspend */
	DWC3_LINK_STATE_SS_INACT	= 0x06,
	DWC3_LINK_STATE_POLL		= 0x07,
	DWC3_LINK_STATE_RECOV		= 0x08,
	DWC3_LINK_STATE_HRESET		= 0x09,
	DWC3_LINK_STATE_CMPLY		= 0x0a,
	DWC3_LINK_STATE_LPBK		= 0x0b,
	DWC3_LINK_STATE_RESET		= 0x0e,
	DWC3_LINK_STATE_RESUME		= 0x0f,
	DWC3_LINK_STATE_MASK		= 0x0f,
};

/* TRB Length, PCM and Status */
#define DWC3_TRB_SIZE_MASK	(0x00ffffff)
#define DWC3_TRB_SIZE_LENGTH(n)	((n) & DWC3_TRB_SIZE_MASK)
#define DWC3_TRB_SIZE_PCM1(n)	(((n) & 0x03) << 24)
#define DWC3_TRB_SIZE_TRBSTS(n)	(((n) & (0x0f << 28)) >> 28)

#define DWC3_TRBSTS_OK			0
#define DWC3_TRBSTS_MISSED_ISOC		1
#define DWC3_TRBSTS_SETUP_PENDING	2
#define DWC3_TRB_STS_XFER_IN_PROG	4

/* TRB Control */
#define DWC3_TRB_CTRL_HWO		(1 << 0)
#define DWC3_TRB_CTRL_LST		(1 << 1)
#define DWC3_TRB_CTRL_CHN		(1 << 2)
#define DWC3_TRB_CTRL_CSP		(1 << 3)
#define DWC3_TRB_CTRL_TRBCTL(n)		(((n) & 0x3f) << 4)
#define DWC3_TRB_CTRL_ISP_IMI		(1 << 10)
#define DWC3_TRB_CTRL_IOC		(1 << 11)
#define DWC3_TRB_CTRL_SID_SOFN(n)	(((n) & 0xffff) << 14)

#define DWC3_TRBCTL_TYPE(n)		((n) & (0x3f << 4))
#define DWC3_TRBCTL_NORMAL		DWC3_TRB_CTRL_TRBCTL(1)
#define DWC3_TRBCTL_CONTROL_SETUP	DWC3_TRB_CTRL_TRBCTL(2)
#define DWC3_TRBCTL_CONTROL_STATUS2	DWC3_TRB_CTRL_TRBCTL(3)
#define DWC3_TRBCTL_CONTROL_STATUS3	DWC3_TRB_CTRL_TRBCTL(4)
#define DWC3_TRBCTL_CONTROL_DATA	DWC3_TRB_CTRL_TRBCTL(5)
#define DWC3_TRBCTL_ISOCHRONOUS_FIRST	DWC3_TRB_CTRL_TRBCTL(6)
#define DWC3_TRBCTL_ISOCHRONOUS		DWC3_TRB_CTRL_TRBCTL(7)
#define DWC3_TRBCTL_LINK_TRB		DWC3_TRB_CTRL_TRBCTL(8)

/**
 * struct dwc3_trb - transfer request block (hw format)
 * @bpl: DW0-3
 * @bph: DW4-7
 * @size: DW8-B
 * @trl: DWC-F
 */
struct dwc3_trb {
	u32		bpl;
	u32		bph;
	u32		size;
	u32		ctrl;
} __packed;

/**
 * dwc3_hwparams - copy of HWPARAMS registers
 * @hwparams0 - GHWPARAMS0
 * @hwparams1 - GHWPARAMS1
 * @hwparams2 - GHWPARAMS2
 * @hwparams3 - GHWPARAMS3
 * @hwparams4 - GHWPARAMS4
 * @hwparams5 - GHWPARAMS5
 * @hwparams6 - GHWPARAMS6
 * @hwparams7 - GHWPARAMS7
 * @hwparams8 - GHWPARAMS8
 */
struct dwc3_hwparams {
	u32	hwparams0;
	u32	hwparams1;
	u32	hwparams2;
	u32	hwparams3;
	u32	hwparams4;
	u32	hwparams5;
	u32	hwparams6;
	u32	hwparams7;
	u32	hwparams8;
};

/* HWPARAMS0 */
#define DWC3_MODE(n)		((n) & 0x7)

#define DWC3_MDWIDTH(n)		(((n) & 0xff00) >> 8)

/* HWPARAMS1 */
#define DWC3_NUM_INT(n)		(((n) & (0x3f << 15)) >> 15)

/* HWPARAMS3 */
#define DWC3_NUM_IN_EPS_MASK	(0x1f << 18)
#define DWC3_NUM_EPS_MASK	(0x3f << 12)
#define DWC3_NUM_EPS(p)		(((p)->hwparams3 &		\
			(DWC3_NUM_EPS_MASK)) >> 12)
#define DWC3_NUM_IN_EPS(p)	(((p)->hwparams3 &		\
			(DWC3_NUM_IN_EPS_MASK)) >> 18)

/* HWPARAMS7 */
#define DWC3_RAM1_DEPTH(n)	((n) & 0xffff)

/**
 * struct dwc3_request - representation of a transfer request
 * @request: struct usb_request to be transferred
 * @list: a list_head used for request queueing
 * @dep: struct dwc3_ep owning this request
 * @sg: pointer to first incomplete sg
 * @num_pending_sgs: counter to pending sgs
 * @first_trb_index: index to first trb used by this request
 * @epnum: endpoint number to which this request refers
 * @trb: pointer to struct dwc3_trb
 * @trb_dma: DMA address of @trb
 * @direction: IN or OUT direction flag
 * @mapped: true when request has been dma-mapped
 * @queued: true when request has been queued to HW
 */
struct dwc3_request {
	struct usb_request	request;
	struct list_head	list;
	struct dwc3_ep		*dep;
	struct scatterlist	*sg;
	struct scatterlist	*sg_to_start;

	unsigned		num_pending_sgs;
	unsigned int		num_queued_sgs;
	u8			first_trb_index;
	u8			epnum;
	struct dwc3_trb		*trb;
	dma_addr_t		trb_dma;

	unsigned		direction:1;
	unsigned		mapped:1;
	unsigned		started:1;
};

/*
 * struct dwc3_scratchpad_array - hibernation scratchpad array
 * (format defined by hw)
 */
struct dwc3_scratchpad_array {
	__le64	dma_adr[DWC3_MAX_HIBER_SCRATCHBUFS];
};

/**
 * struct dwc3 - representation of our controller
 * @ctrl_req: usb control request which is used for ep0
 * @ep0_trb: trb which is used for the ctrl_req
 * @ep0_bounce: bounce buffer for ep0
 * @zlp_buf: used when request->zero is set
 * @setup_buf: used while precessing STD USB requests
 * @ctrl_req_addr: dma address of ctrl_req
 * @ep0_trb: dma address of ep0_trb
 * @ep0_usb_req: dummy req used while handling STD USB requests
 * @ep0_bounce_addr: dma address of ep0_bounce
 * @scratch_addr: dma address of scratchbuf
 * @lock: for synchronizing
 * @dev: pointer to our struct device
 * @xhci: pointer to our xHCI child
 * @event_buffer_list: a list of event buffers
 * @gadget: device side representation of the peripheral controller
 * @gadget_driver: pointer to the gadget driver
 * @otg: pointer to the dwc3_otg structure
 * @regs: base address for our registers
 * @regs_size: address space size
 * @fladj: frame length adjustment
 * @refclk_fladj: boolean to update GFLADJ_REFCLK_FLADJ field also
 * @irq_gadget: peripheral controller's IRQ number
 * @nr_scratch: number of scratch buffers
 * @u1u2: only used on revisions <1.83a for workaround
 * @maximum_speed: maximum speed requested (mainly for testing purposes)
 * @revision: revision register contents
 * @dr_mode: requested mode of operation
 * @hsphy_mode: UTMI phy mode, one of following:
 *		- USBPHY_INTERFACE_MODE_UTMI
 *		- USBPHY_INTERFACE_MODE_UTMIW
 * @usb2_phy: pointer to USB2 PHY
 * @usb3_phy: pointer to USB3 PHY
 * @usb2_generic_phy: pointer to USB2 PHY
 * @usb3_generic_phy: pointer to USB3 PHY
 * @ulpi: pointer to ulpi interface
 * @dcfg: saved contents of DCFG register
 * @gctl: saved contents of GCTL register
 * @isoch_delay: wValue from Set Isochronous Delay request;
 * @u2sel: parameter from Set SEL request.
 * @u2pel: parameter from Set SEL request.
 * @u1sel: parameter from Set SEL request.
 * @u1pel: parameter from Set SEL request.
 * @num_out_eps: number of out endpoints
 * @num_in_eps: number of in endpoints
 * @ep0_next_event: hold the next expected event
 * @ep0state: state of endpoint zero
 * @link_state: link state
 * @speed: device speed (super, high, full, low)
 * @mem: points to start of memory which is used for this struct.
 * @hwparams: copy of hwparams registers
 * @root: debugfs root folder pointer
 * @regset: debugfs pointer to regdump file
 * @test_mode: true when we're entering a USB test mode
 * @test_mode_nr: test feature selector
 * @lpm_nyet_threshold: LPM NYET response threshold
 * @hird_threshold: HIRD threshold
 * @hsphy_interface: "utmi" or "ulpi"
 * @connected: true when we're connected to a host, false otherwise
 * @delayed_status: true when gadget driver asks for delayed status
 * @ep0_bounced: true when we used bounce buffer
 * @ep0_expect_in: true when we expect a DATA IN transfer
 * @has_hibernation: true when dwc3 was configured with Hibernation
 * @has_lpm_erratum: true when core was configured with LPM Erratum. Note that
 *			there's now way for software to detect this in runtime.
 * @is_utmi_l1_suspend: the core asserts output signal
 * 	0	- utmi_sleep_n
 * 	1	- utmi_l1_suspend_n
 * @is_fpga: true when we are using the FPGA board
 * @pending_events: true when we have pending IRQs to be handled
 * @pullups_connected: true when Run/Stop bit is set
 * @setup_packet_pending: true when there's a Setup Packet in FIFO. Workaround
 * @start_config_issued: true when StartConfig command has been issued
 * @three_stage_setup: set if we perform a three phase setup
 * @usb3_lpm_capable: set if hadrware supports Link Power Management
 * @remote_wakeup: set if host supports Remote Wakeup from Peripheral
 * @disable_scramble_quirk: set if we enable the disable scramble quirk
 * @u2exit_lfps_quirk: set if we enable u2exit lfps quirk
 * @u2ss_inp3_quirk: set if we enable P3 OK for U2/SS Inactive quirk
 * @req_p1p2p3_quirk: set if we enable request p1p2p3 quirk
 * @del_p1p2p3_quirk: set if we enable delay p1p2p3 quirk
 * @del_phy_power_chg_quirk: set if we enable delay phy power change quirk
 * @lfps_filter_quirk: set if we enable LFPS filter quirk
 * @rx_detect_poll_quirk: set if we enable rx_detect to polling lfps quirk
 * @dis_u3_susphy_quirk: set if we disable usb3 suspend phy
 * @dis_u2_susphy_quirk: set if we disable usb2 suspend phy
 * @dis_enblslpm_quirk: set if we clear enblslpm in GUSB2PHYCFG,
 *                      disabling the suspend signal to the PHY.
 * @dis_u2_freeclk_exists_quirk : set if we clear u2_freeclk_exists
 *			in GUSB2PHYCFG, specify that USB2 PHY doesn't
 *			provide a free-running PHY clock.
 * @dis_del_phy_power_chg_quirk: set if we disable delay phy power
 *			change quirk.
 * @enable_guctl1_resume_quirk: Set if we enable quirk for fixing improper crc
 *			generation after resume from suspend.
 * @enable_guctl1_ipd_quirk: set if we enable quirk for reducing timing of inter
 *			packet delay(ipd).
 * @tx_de_emphasis_quirk: set if we enable Tx de-emphasis quirk
 * @tx_de_emphasis: Tx de-emphasis value
 * 	0	- -6dB de-emphasis
 * 	1	- -3.5dB de-emphasis
 * 	2	- No de-emphasis
 * 	3	- Reserved
 */
struct dwc3 {
	struct usb_ctrlrequest	*ctrl_req;
	struct dwc3_trb		*ep0_trb;
	void			*ep0_bounce;
	void			*zlp_buf;
	void			*scratchbuf;
	u8			*setup_buf;
	dma_addr_t		ctrl_req_addr;
	dma_addr_t		ep0_trb_addr;
	dma_addr_t		ep0_bounce_addr;
	dma_addr_t		scratch_addr;
	struct dwc3_request	ep0_usb_req;

	/* device lock */
	spinlock_t		lock;

	struct device		*dev;

	struct platform_device	*xhci;
	struct resource		xhci_resources[DWC3_XHCI_RESOURCES_NUM];

	struct dwc3_event_buffer *ev_buf;
	struct dwc3_ep		*eps[DWC3_ENDPOINTS_NUM];

	struct usb_gadget	gadget;
	struct usb_gadget_driver *gadget_driver;

	struct dwc3_otg		*otg;

	struct usb_phy		*usb2_phy;
	struct usb_phy		*usb3_phy;

	struct phy		*usb2_generic_phy;
	struct phy		*usb3_generic_phy;

	struct ulpi		*ulpi;

	void __iomem		*regs;
	size_t			regs_size;

	enum usb_dr_mode	dr_mode;
	enum usb_phy_interface	hsphy_mode;

	u32			fladj;
	bool			refclk_fladj;
	u32			irq_gadget;
	u32			nr_scratch;
	u32			u1u2;
	u32			maximum_speed;

	/*
	 * All 3.1 IP version constants are greater than the 3.0 IP
	 * version constants. This works for most version checks in
	 * dwc3. However, in the future, this may not apply as
	 * features may be developed on newer versions of the 3.0 IP
	 * that are not in the 3.1 IP.
	 */
	u32			revision;

#define DWC3_REVISION_173A	0x5533173a
#define DWC3_REVISION_175A	0x5533175a
#define DWC3_REVISION_180A	0x5533180a
#define DWC3_REVISION_183A	0x5533183a
#define DWC3_REVISION_185A	0x5533185a
#define DWC3_REVISION_187A	0x5533187a
#define DWC3_REVISION_188A	0x5533188a
#define DWC3_REVISION_190A	0x5533190a
#define DWC3_REVISION_194A	0x5533194a
#define DWC3_REVISION_200A	0x5533200a
#define DWC3_REVISION_202A	0x5533202a
#define DWC3_REVISION_210A	0x5533210a
#define DWC3_REVISION_220A	0x5533220a
#define DWC3_REVISION_230A	0x5533230a
#define DWC3_REVISION_240A	0x5533240a
#define DWC3_REVISION_250A	0x5533250a
#define DWC3_REVISION_260A	0x5533260a
#define DWC3_REVISION_270A	0x5533270a
#define DWC3_REVISION_280A	0x5533280a
#define DWC3_REVISION_300A	0x5533300a
#define DWC3_REVISION_310A	0x5533310a

/*
 * NOTICE: we're using bit 31 as a "is usb 3.1" flag. This is really
 * just so dwc31 revisions are always larger than dwc3.
 */
#define DWC3_REVISION_IS_DWC31		0x80000000
#define DWC3_USB31_REVISION_110A	(0x3131302a | DWC3_REVISION_IS_DWC31)

	enum dwc3_ep0_next	ep0_next_event;
	enum dwc3_ep0_state	ep0state;
	enum dwc3_link_state	link_state;

	u16			isoch_delay;
	u16			u2sel;
	u16			u2pel;
	u8			u1sel;
	u8			u1pel;

	u8			speed;

	u8			num_out_eps;
	u8			num_in_eps;

	void			*mem;

	struct dwc3_hwparams	hwparams;
	struct dentry		*root;
	struct debugfs_regset32	*regset;

	u8			test_mode;
	u8			test_mode_nr;
	u8			lpm_nyet_threshold;
	u8			hird_threshold;

	const char		*hsphy_interface;

	unsigned		connected:1;
	unsigned		delayed_status:1;
	unsigned		ep0_bounced:1;
	unsigned		ep0_expect_in:1;
	unsigned		has_hibernation:1;
	unsigned		has_lpm_erratum:1;
	unsigned		is_utmi_l1_suspend:1;
	unsigned		is_fpga:1;
	unsigned		pending_events:1;
	unsigned		pullups_connected:1;
	unsigned		setup_packet_pending:1;
	unsigned		three_stage_setup:1;
	unsigned		usb3_lpm_capable:1;
	unsigned                remote_wakeup:1;

	unsigned		disable_scramble_quirk:1;
	unsigned		u2exit_lfps_quirk:1;
	unsigned		u2ss_inp3_quirk:1;
	unsigned		req_p1p2p3_quirk:1;
	unsigned                del_p1p2p3_quirk:1;
	unsigned		del_phy_power_chg_quirk:1;
	unsigned		lfps_filter_quirk:1;
	unsigned		rx_detect_poll_quirk:1;
	unsigned		dis_u3_susphy_quirk:1;
	unsigned		dis_u2_susphy_quirk:1;
	unsigned		dis_enblslpm_quirk:1;
	unsigned		dis_rxdet_inp3_quirk:1;
	unsigned		dis_u2_freeclk_exists_quirk:1;
	unsigned		dis_del_phy_power_chg_quirk:1;
	unsigned		enable_guctl1_resume_quirk:1;
	unsigned		enable_guctl1_ipd_quirk:1;

	unsigned		tx_de_emphasis_quirk:1;
	unsigned		tx_de_emphasis:2;
};

/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */

struct dwc3_event_type {
	u32	is_devspec:1;
	u32	type:7;
	u32	reserved8_31:24;
} __packed;

#define DWC3_DEPEVT_XFERCOMPLETE	0x01
#define DWC3_DEPEVT_XFERINPROGRESS	0x02
#define DWC3_DEPEVT_XFERNOTREADY	0x03
#define DWC3_DEPEVT_RXTXFIFOEVT		0x04
#define DWC3_DEPEVT_STREAMEVT		0x06
#define DWC3_DEPEVT_EPCMDCMPLT		0x07

/**
 * struct dwc3_event_depvt - Device Endpoint Events
 * @one_bit: indicates this is an endpoint event (not used)
 * @endpoint_number: number of the endpoint
 * @endpoint_event: The event we have:
 *	0x00	- Reserved
 *	0x01	- XferComplete
 *	0x02	- XferInProgress
 *	0x03	- XferNotReady
 *	0x04	- RxTxFifoEvt (IN->Underrun, OUT->Overrun)
 *	0x05	- Reserved
 *	0x06	- StreamEvt
 *	0x07	- EPCmdCmplt
 * @reserved11_10: Reserved, don't use.
 * @status: Indicates the status of the event. Refer to databook for
 *	more information.
 * @parameters: Parameters of the current event. Refer to databook for
 *	more information.
 */
struct dwc3_event_depevt {
	u32	one_bit:1;
	u32	endpoint_number:5;
	u32	endpoint_event:4;
	u32	reserved11_10:2;
	u32	status:4;

/* Within XferNotReady */
#define DEPEVT_STATUS_TRANSFER_ACTIVE	(1 << 3)

/* Within XferComplete */
#define DEPEVT_STATUS_BUSERR	(1 << 0)
#define DEPEVT_STATUS_SHORT	(1 << 1)
#define DEPEVT_STATUS_IOC	(1 << 2)
#define DEPEVT_STATUS_LST	(1 << 3)

/* Stream event only */
#define DEPEVT_STREAMEVT_FOUND		1
#define DEPEVT_STREAMEVT_NOTFOUND	2

/* Control-only Status */
#define DEPEVT_STATUS_CONTROL_DATA	1
#define DEPEVT_STATUS_CONTROL_STATUS	2

/* In response to Start Transfer */
#define DEPEVT_TRANSFER_NO_RESOURCE	1
#define DEPEVT_TRANSFER_BUS_EXPIRY	2

	u32	parameters:16;
} __packed;

/**
 * struct dwc3_event_devt - Device Events
 * @one_bit: indicates this is a non-endpoint event (not used)
 * @device_event: indicates it's a device event. Should read as 0x00
 * @type: indicates the type of device event.
 *	0	- DisconnEvt
 *	1	- USBRst
 *	2	- ConnectDone
 *	3	- ULStChng
 *	4	- WkUpEvt
 *	5	- Reserved
 *	6	- EOPF
 *	7	- SOF
 *	8	- Reserved
 *	9	- ErrticErr
 *	10	- CmdCmplt
 *	11	- EvntOverflow
 *	12	- VndrDevTstRcved
 * @reserved15_12: Reserved, not used
 * @event_info: Information about this event
 * @reserved31_25: Reserved, not used
 */
struct dwc3_event_devt {
	u32	one_bit:1;
	u32	device_event:7;
	u32	type:4;
	u32	reserved15_12:4;
	u32	event_info:9;
	u32	reserved31_25:7;
} __packed;

/**
 * struct dwc3_event_gevt - Other Core Events
 * @one_bit: indicates this is a non-endpoint event (not used)
 * @device_event: indicates it's (0x03) Carkit or (0x04) I2C event.
 * @phy_port_number: self-explanatory
 * @reserved31_12: Reserved, not used.
 */
struct dwc3_event_gevt {
	u32	one_bit:1;
	u32	device_event:7;
	u32	phy_port_number:4;
	u32	reserved31_12:20;
} __packed;

/**
 * union dwc3_event - representation of Event Buffer contents
 * @raw: raw 32-bit event
 * @type: the type of the event
 * @depevt: Device Endpoint Event
 * @devt: Device Event
 * @gevt: Global Event
 */
union dwc3_event {
	u32				raw;
	struct dwc3_event_type		type;
	struct dwc3_event_depevt	depevt;
	struct dwc3_event_devt		devt;
	struct dwc3_event_gevt		gevt;
};

/**
 * struct dwc3_gadget_ep_cmd_params - representation of endpoint command
 * parameters
 * @param2: third parameter
 * @param1: second parameter
 * @param0: first parameter
 */
struct dwc3_gadget_ep_cmd_params {
	u32	param2;
	u32	param1;
	u32	param0;
};

/*
 * DWC3 Features to be used as Driver Data
 */

#define DWC3_HAS_PERIPHERAL		BIT(0)
#define DWC3_HAS_XHCI			BIT(1)
#define DWC3_HAS_OTG			BIT(3)

/* prototypes */
void dwc3_set_mode(struct dwc3 *dwc, u32 mode);
u32 dwc3_core_fifo_space(struct dwc3_ep *dep, u8 type);

/* check whether we are on the DWC_usb31 core */
static inline bool dwc3_is_usb31(struct dwc3 *dwc)
{
	return !!(dwc->revision & DWC3_REVISION_IS_DWC31);
}

#if IS_ENABLED(CONFIG_USB_DWC3_OF_SIMPLE)
int dwc3_enable_hw_coherency(struct device *dev);
void dwc3_set_phydata(struct device *dev, struct phy *phy);
void dwc3_simple_wakeup_capable(struct device *dev, bool wakeup);
void dwc3_set_simple_data(struct dwc3 *dwc);
void dwc3_simple_check_quirks(struct dwc3 *dwc);
int dwc3_set_usb_core_power(struct dwc3 *dwc, bool on);
#else
static inline int dwc3_enable_hw_coherency(struct device *dev)
{ return 1; }
static inline void dwc3_set_phydata(struct device *dev, struct phy *phy)
{ ; }
void dwc3_simple_wakeup_capable(struct device *dev, bool wakeup)
{ ; }
void dwc3_set_simple_data(struct dwc3 *dwc)
{ ; }
void dwc3_simple_check_quirks(struct dwc3 *dwc)
{ ; }
int dwc3_set_usb_core_power(struct dwc3 *dwc, bool on)
{ ; }
#endif

#if IS_ENABLED(CONFIG_USB_DWC3_HOST) || IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)\
	 || IS_ENABLED(CONFIG_USB_DWC3_OTG)
int dwc3_host_init(struct dwc3 *dwc);
void dwc3_host_exit(struct dwc3 *dwc);
#else
static inline int dwc3_host_init(struct dwc3 *dwc)
{ return 0; }
static inline void dwc3_host_exit(struct dwc3 *dwc)
{ }
#endif

#if IS_ENABLED(CONFIG_USB_DWC3_GADGET) || IS_ENABLED(CONFIG_USB_DWC3_DUAL_ROLE)\
	 || IS_ENABLED(CONFIG_USB_DWC3_OTG)
int dwc3_gadget_init(struct dwc3 *dwc);
void dwc3_gadget_exit(struct dwc3 *dwc);
int dwc3_gadget_set_test_mode(struct dwc3 *dwc, int mode);
int dwc3_gadget_get_link_state(struct dwc3 *dwc);
int dwc3_gadget_set_link_state(struct dwc3 *dwc, enum dwc3_link_state state);
int dwc3_send_gadget_ep_cmd(struct dwc3_ep *dep, unsigned cmd,
		struct dwc3_gadget_ep_cmd_params *params);
int dwc3_send_gadget_generic_command(struct dwc3 *dwc, unsigned cmd, u32 param);
#else
static inline int dwc3_gadget_init(struct dwc3 *dwc)
{ return 0; }
static inline void dwc3_gadget_exit(struct dwc3 *dwc)
{ }
static inline int dwc3_gadget_set_test_mode(struct dwc3 *dwc, int mode)
{ return 0; }
static inline int dwc3_gadget_get_link_state(struct dwc3 *dwc)
{ return 0; }
static inline int dwc3_gadget_set_link_state(struct dwc3 *dwc,
		enum dwc3_link_state state)
{ return 0; }

static inline int dwc3_send_gadget_ep_cmd(struct dwc3_ep *dep, unsigned cmd,
		struct dwc3_gadget_ep_cmd_params *params)
{ return 0; }
static inline int dwc3_send_gadget_generic_command(struct dwc3 *dwc,
		int cmd, u32 param)
{ return 0; }
#endif

#if IS_ENABLED(CONFIG_USB_DWC3_OTG)
int dwc3_otg_init(struct dwc3 *dwc);
#else
static inline int dwc3_otg_init(struct dwc3 *dwc)
{ return 0; }
#endif

/* power management interface */
#if !IS_ENABLED(CONFIG_USB_DWC3_HOST)
int dwc3_gadget_suspend(struct dwc3 *dwc);
int dwc3_gadget_resume(struct dwc3 *dwc);
void dwc3_gadget_process_pending_events(struct dwc3 *dwc);
#else
static inline int dwc3_gadget_suspend(struct dwc3 *dwc)
{
	return 0;
}

static inline int dwc3_gadget_resume(struct dwc3 *dwc)
{
	return 0;
}

static inline void dwc3_gadget_process_pending_events(struct dwc3 *dwc)
{
}
#endif /* !IS_ENABLED(CONFIG_USB_DWC3_HOST) */

#if IS_ENABLED(CONFIG_USB_DWC3_ULPI)
int dwc3_ulpi_init(struct dwc3 *dwc);
void dwc3_ulpi_exit(struct dwc3 *dwc);
#else
static inline int dwc3_ulpi_init(struct dwc3 *dwc)
{ return 0; }
static inline void dwc3_ulpi_exit(struct dwc3 *dwc)
{ }
#endif

int dwc3_alloc_event_buffers(struct dwc3 *dwc, unsigned length);
void dwc3_free_event_buffers(struct dwc3 *dwc);
int dwc3_event_buffers_setup(struct dwc3 *dwc);

#endif /* __DRIVERS_USB_DWC3_CORE_H */
