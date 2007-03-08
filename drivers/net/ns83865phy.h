/*--------------------------------------------------------------------

 * ns83865.h

 *

 * National NS83865 specific ethernet transceiver phy registers (09-31)

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



#ifndef _NS883865PHY_H_

    #define _NS883865PHY_H_



// Link Status Register

#define NS883865_LNKSTS_REG         0x11    // Phy Reg17

//

#define NS883865_LNKSTS_100_MASK       0x0008  // 100 Mbps

#define NS883865_LNKSTS_LNKUP_MASK     0x0004  // Link Up

#define NS883865_LNKSTS_FDUPLX_MASK    0x0002  // Full duplex



// Interrupt Status Register

#define NS883865_INTSTS_REG         0x14    // Phy Reg20

//

#define NS883865_INTSTS_LSCHG_MASK     0x4000 // Link status change

#define NS883865_INTSTS_ANEGDONE_MASK  0x0010 // Auto neg done



// Interrupt Enable Register

#define NS883865_INTIE_REG          0x15    // Phy Reg21

//

#define NS883865_INTIE_LSCHG_MASK      NS883865_INTSTS_LSCHG_MASK

#define NS883865_INTIE_ANEGDONE_MASK   NS883865_INTSTS_ANEGDONE_MASK



// Interrupt Ack Register

#define NS883865_INTACK_REG         0x17    // Phy Reg23

//

#define NS883865_INTACK_LSCHG_MASK     NS883865_INTSTS_LSCHG_MASK

#define NS883865_INTACK_ANEGDONE_MASK  NS883865_INTSTS_ANEGDONE_MASK 

#endif  /* _NS883865PHY_H_ */

