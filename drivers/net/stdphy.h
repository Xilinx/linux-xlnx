/*--------------------------------------------------------------------
 * stdphy.h
 *
 * 802.3 standard ethernet transceiver phy registers (0-8)
 *
 * Derived from various works, Alpha, ix86, M68K, Sparc, ...et al
 *
 * Copyright (C) 2004   Microtronix Datacom Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Apr2004      DGT Microtronix Datacom
 *
 ---------------------------------------------------------------------*/

#ifndef _STDPHY_H_
    #define _STDPHY_H_

// PHY Control Register
#define PHY_CTL_REG             0x00    // Std Phy Reg0

#define PHY_CTL_RST_MASK        0x8000  // PHY Reset
#define PHY_CTL_LPBK_MASK       0x4000  // PHY Loopback
#define PHY_CTL_SPEED_MASK      0x2000  // 100Mbps, 0=10Mpbs
#define PHY_CTL_ANEG_EN_MASK    0x1000  // Enable Auto negotiation
#define PHY_CTL_PDN_MASK        0x0800  // PHY Power Down mode
#define PHY_CTL_MII_DIS_MASK    0x0400  // MII 4 bit interface disabled
#define PHY_CTL_ANEG_RST_MASK   0x0200  // Restart Auto negotiation
#define PHY_CTL_DPLX_MASK       0x0100  // Full Duplex (Else Half)
#define PHY_CTL_COLTST_MASK     0x0080  // MII Colision Test

// PHY Status Register
#define PHY_STS_REG             0x01    // Std Phy Reg1

#define PHY_STS_CAP_T4_MASK     0x8000  // 100Base-T4            capable
#define PHY_STS_CAP_TXF_MASK    0x4000  // 100Base-X full duplex capable
#define PHY_STS_CAP_TXH_MASK    0x2000  // 100Base-X half duplex capable
#define PHY_STS_CAP_TF_MASK     0x1000  // 10Mbps full duplex    capable
#define PHY_STS_CAP_TH_MASK     0x0800  // 10Mbps half duplex    capable
#define PHY_STS_ANEGDONE_MASK   0x0020  // ANEG has completed
#define PHY_STS_REM_FLT_MASK    0x0010  // Remote Fault detected
#define PHY_STS_CAP_ANEG_MASK   0x0008  // Auto negotiate        capable
#define PHY_STS_LNKSTS_MASK     0x0004  // Valid link
#define PHY_STS_JAB_MASK        0x0002  // 10Mbps jabber condition
#define PHY_STS_EXREG_MASK      0x0001  // Extended regs implemented

// PHY Identifier Registers
#define PHY_ID1_REG             0x02    // Std Phy Reg2
#define PHY_ID2_REG             0x03    // Std Phy Reg3

// PHY Auto-Negotiation Advertisement Register
#define PHY_ADV_REG             0x04    // Std Phy Reg4

#define PHY_ADV_T4              0x0200  // 100Base-T4       capable
#define PHY_ADV_TX_FDX          0x0100  // 100Base-TX FDPLX capable
#define PHY_ADV_TX_HDX          0x0080  // 100Base-TX HDPLX capable
#define PHY_ADV_10_FDX          0x0040  // 10Base-T FDPLX   capable
#define PHY_ADV_10_HDX          0x0020  // 10Base-T HDPLX   capable
#define PHY_ADV_CSMA            0x0001  // 802.3 CMSA       capable

// PHY Auto-negotiation Remote End Capability Register
#define PHY_REMCAP_REG          0x05    // Std Phy Reg5

#define PHY_REMCAP_T4           PHY_ADV_T4
#define PHY_REMCAP_TX_FDX       PHY_ADV_TX_FDX
#define PHY_REMCAP_TX_HDX       PHY_ADV_TX_HDX
#define PHY_REMCAP_10_FDX       PHY_ADV_10_FDX
#define PHY_REMCAP_10_HDX       PHY_ADV_10_HDX
#define PHY_REMCAP_CSMA         PHY_ADV_CSMA

#endif  /* _STDPHY_H_ */
