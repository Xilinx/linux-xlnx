/*--------------------------------------------------------------------
 * tdk78phy.h
 *
 * TDK 78Q2120 specific ethernet transceiver phy registers (09-18)
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

#ifndef _TDK78PHY_H_
    #define _TDK78PHY_H_


// Interrupt Control/Status Register
#define TDK78_INTCTLSTS_REG         0x11    // Phy Reg17
//
#define TDK78_INTIE_RXER_MASK       0x4000  // Rx error int enable
#define TDK78_INTIE_LSCHG_MASK      0x0400  // Link sts chg int enable
#define TDK78_INTIE_ANEGDONE_MASK   0x0100  // Auto neg done int enable
#define TDK78_INTSTS_RXER_MASK      0x0040  // Rx error (Read clears)
#define TDK78_INTSTS_LSCHG_MASK     0x0004  // Link sts chg (Read clears)
#define TDK78_INTSTS_ANEGDONE_MASK  0x0001  // Auto neg done (Read clears)

// Diagnostic Register
#define TDK78_DIAG_REG              0x12    // Phy Reg18
//
#define TDK78_DIAG_ANEGFAIL_MASK    0x1000  // Auto neg failed (Read clears)
#define TDK78_DIAG_FDUPLX_MASK      0x0800  // Full duplex
#define TDK78_DIAG_100TX_MASK       0x0400  // 100Base-TX

#endif  /* _TDK78PHY_H_ */

