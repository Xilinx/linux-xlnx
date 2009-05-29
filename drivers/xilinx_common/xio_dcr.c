/* $Id: xio_dcr.c,v 1.9 2007/01/24 17:00:16 meinelte Exp $ */
/******************************************************************************
*
*       XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS"
*       AS A COURTESY TO YOU, SOLELY FOR USE IN DEVELOPING PROGRAMS AND
*       SOLUTIONS FOR XILINX DEVICES.  BY PROVIDING THIS DESIGN, CODE,
*       OR INFORMATION AS ONE POSSIBLE IMPLEMENTATION OF THIS FEATURE,
*       APPLICATION OR STANDARD, XILINX IS MAKING NO REPRESENTATION
*       THAT THIS IMPLEMENTATION IS FREE FROM ANY CLAIMS OF INFRINGEMENT,
*       AND YOU ARE RESPONSIBLE FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE
*       FOR YOUR IMPLEMENTATION.  XILINX EXPRESSLY DISCLAIMS ANY
*       WARRANTY WHATSOEVER WITH RESPECT TO THE ADEQUACY OF THE
*       IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OR
*       REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE FROM CLAIMS OF
*       INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*       FOR A PARTICULAR PURPOSE.
*
*       (c) Copyright 2007-2008 Xilinx Inc.
*       All rights reserved.
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2 of the License, or (at your
* option) any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*
******************************************************************************/
/*****************************************************************************/
/**
*
* @file xio_dcr.c
*
* The implementation of the XDcrIo interface. See xio_dcr.h for more
* information about the component.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00a ecm  11/09/06 Modified from the PPC405 version to use the indirect
*                     addressing that is available in the PPC440 block in V5.
*                     Removed the jump table structure in xio_dcr.c also.
*                     Added functionality from the SEG driver to allow for
*                     one file pair.
* 1.00a ecm  01/02/07 Incorporated changes from testing with multiple DCR
*                     masters, discovered and fixed several concurrency
*                     issues.
* 1.00a ecm  01/24/07 update for new coding standard.
* </pre>
*
* @internal
*
* The C functions which subsequently call into either the assembly code or into
* the provided table of functions are required since the registers assigned to
* the calling and return from functions are strictly defined in the ABI and that
* definition is used in the low-level functions directly. The use of macros is
* not recommended since the temporary registers in the ABI are defined but there
* is no way to force the compiler to use a specific register in a block of code.
*
*****************************************************************************/

/***************************** Include Files ********************************/

#include <asm/dcr.h>
#include <asm/reg.h>

#include "xstatus.h"
#include "xbasic_types.h"
#include "xio.h"
#include "xio_dcr.h"

/************************** Constant Definitions ****************************/

/*
 * base address defines for each of the four possible DCR base
 * addresses a processor can have
 */
#define XDCR_0_BASEADDR 0x000
#define XDCR_1_BASEADDR 0x100
#define XDCR_2_BASEADDR 0x200
#define XDCR_3_BASEADDR 0x300


#define MAX_DCR_REGISTERS           4096
#define MAX_DCR_REGISTER            MAX_DCR_REGISTERS - 1
#define MIN_DCR_REGISTER            0

/**************************** Type Definitions ******************************/


/***************** Macros (Inline Functions) Definitions ********************/

/************************** Variable Definitions ****************************/



/************************** Function Prototypes *****************************/

/*****************************************************************************/
/**
*
* Outputs value provided to specified register defined in the header file.
*
* @param    DcrRegister is the intended destination DCR register
* @param    Data is the value to be placed into the specified DCR register
*
* @return
*
* None.
*
* @note
*
* None.
*
****************************************************************************/
void XIo_DcrOut(u32 DcrRegister, u32 Data)
{
    /*
     * Assert validates the register number
     */
    XASSERT_VOID(DcrRegister < MAX_DCR_REGISTERS);

    /*
     * pass the call on to the proper function
     */
    XIo_mDcrIndirectAddrWriteReg(XDCR_0_BASEADDR, DcrRegister, Data);
}

/*****************************************************************************/
/**
*
* Reads value from specified register.
*
* @param    DcrRegister is the intended source DCR register
*
* @return
*
* Contents of the specified DCR register.
*
* @note
*
* None.
*
****************************************************************************/
u32 XIo_DcrIn(u32 DcrRegister)
{
    /*
     * Assert validates the register number
     */
    XASSERT_NONVOID(DcrRegister < MAX_DCR_REGISTERS);

    /*
     * pass the call on to the proper function
     */
    return (XIo_mDcrIndirectAddrReadReg(XDCR_0_BASEADDR, DcrRegister));
}

/*****************************************************************************/
/**
*
* Reads the value of the specified register using the indirect access method.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended destination DCR register
*
* @return
*
* Contents of the specified DCR register.
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
u32 XIo_DcrReadReg(u32 DcrBase, u32 DcrRegister)
{
    switch (DcrBase) {
    case 0x000:
        return XIo_mDcrIndirectAddrReadReg(XDCR_0_BASEADDR,
                           DcrRegister);
    case 0x100:
        return XIo_mDcrIndirectAddrReadReg(XDCR_1_BASEADDR,
                           DcrRegister);
    case 0x200:
        return XIo_mDcrIndirectAddrReadReg(XDCR_2_BASEADDR,
                           DcrRegister);
    case 0x300:
        return XIo_mDcrIndirectAddrReadReg(XDCR_3_BASEADDR,
                           DcrRegister);
    default:
        return XIo_mDcrIndirectAddrReadReg(XDCR_0_BASEADDR,
                           DcrRegister);
    }
}

/*****************************************************************************/
/**
*
* Writes the value to the specified register using the indirect access method.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended destination DCR register
* @param    Data is the value to be placed into the specified DCR register
*
* @return
*
* None
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
void XIo_DcrWriteReg(u32 DcrBase, u32 DcrRegister, u32 Data)
{
    switch (DcrBase) {
    case 0x000:
        XIo_mDcrIndirectAddrWriteReg(XDCR_0_BASEADDR, DcrRegister,
                         Data);
        return;
    case 0x100:
        XIo_mDcrIndirectAddrWriteReg(XDCR_1_BASEADDR, DcrRegister,
                         Data);
        return;
    case 0x200:
        XIo_mDcrIndirectAddrWriteReg(XDCR_2_BASEADDR, DcrRegister,
                         Data);
        return;
    case 0x300:
        XIo_mDcrIndirectAddrWriteReg(XDCR_3_BASEADDR, DcrRegister,
                         Data);
        return;
    default:
        XIo_mDcrIndirectAddrWriteReg(XDCR_0_BASEADDR, DcrRegister,
                         Data);
        return;
    }
}

/*****************************************************************************/
/**
*
* Explicitly acquires and release DCR lock--Auto-Lock is disabled.
* Reads the value of the specified register using the indirect access method.
* This function is provided because the most common usecase is to enable
* Auto-Lock. Checking for Auto-Lock in every indirect access would defeat the
* purpose of having Auto-Lock.
* Auto-Lock can only be enable/disabled in hardware.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended destination DCR register
*
* @return
*
* Contents of the specified DCR register.
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
u32 XIo_DcrLockAndReadReg(u32 DcrBase, u32 DcrRegister)
{
    unsigned int rVal;

    switch (DcrBase) {
    case 0x000:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        rVal = XIo_mDcrIndirectAddrReadReg(XDCR_0_BASEADDR,
                           DcrRegister);
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
    case 0x100:
        XIo_mDcrLock(XDCR_1_BASEADDR);
        rVal = XIo_mDcrIndirectAddrReadReg(XDCR_1_BASEADDR,
                           DcrRegister);
        XIo_mDcrUnlock(XDCR_1_BASEADDR);
    case 0x200:
        XIo_mDcrLock(XDCR_2_BASEADDR);
        rVal = XIo_mDcrIndirectAddrReadReg(XDCR_2_BASEADDR,
                           DcrRegister);
        XIo_mDcrUnlock(XDCR_2_BASEADDR);
    case 0x300:
        XIo_mDcrLock(XDCR_3_BASEADDR);
        rVal = XIo_mDcrIndirectAddrReadReg(XDCR_3_BASEADDR,
                           DcrRegister);
        XIo_mDcrUnlock(XDCR_3_BASEADDR);
    default:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        rVal = XIo_mDcrIndirectAddrReadReg(XDCR_0_BASEADDR,
                           DcrRegister);
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
    }
    return rVal;
}

/*****************************************************************************/
/**
*
* Explicitly acquires and release DCR lock--Auto-Lock is disabled.
* Writes the value to the specified register using the indirect access method.
* This function is provided because the most common usecase is to enable
* Auto-Lock. Checking for Auto-Lock in every indirect access would defeat the
* purpose of having Auto-Lock.
* Auto-Lock can only be enable/disabled in hardware.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    DcrRegister is the intended destination DCR register
* @param    Data is the value to be placed into the specified DCR register
*
* @return
*
* None
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
void XIo_DcrLockAndWriteReg(u32 DcrBase, u32 DcrRegister, u32 Data)
{
    switch (DcrBase) {
    case 0x000:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        XIo_mDcrIndirectAddrWriteReg(XDCR_0_BASEADDR, DcrRegister,
                         Data);
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
        return;
    case 0x100:
        XIo_mDcrLock(XDCR_1_BASEADDR);
        XIo_mDcrIndirectAddrWriteReg(XDCR_1_BASEADDR, DcrRegister,
                         Data);
        XIo_mDcrUnlock(XDCR_1_BASEADDR);
        return;
    case 0x200:
        XIo_mDcrLock(XDCR_2_BASEADDR);
        XIo_mDcrIndirectAddrWriteReg(XDCR_2_BASEADDR, DcrRegister,
                         Data);
        XIo_mDcrUnlock(XDCR_2_BASEADDR);
        return;
    case 0x300:
        XIo_mDcrLock(XDCR_3_BASEADDR);
        XIo_mDcrIndirectAddrWriteReg(XDCR_3_BASEADDR, DcrRegister,
                         Data);
        XIo_mDcrUnlock(XDCR_3_BASEADDR);
        return;
    default:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        XIo_mDcrIndirectAddrWriteReg(XDCR_0_BASEADDR, DcrRegister,
                         Data);
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
        return;
    }
}

/*****************************************************************************/
/**
*
* Read APU UDI DCR via indirect addressing.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the desired APU UDI register
*
* @return
*
* Contents of the specified APU register.
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
u32 XIo_DcrReadAPUUDIReg(u32 DcrBase, short UDInum)
{
    switch (DcrBase) {
    case 0x000:
        return XIo_mDcrIndirectAddrReadAPUUDIReg(XDCR_0_BASEADDR,
                             UDInum);
    case 0x100:
        return XIo_mDcrIndirectAddrReadAPUUDIReg(XDCR_1_BASEADDR,
                             UDInum);
    case 0x200:
        return XIo_mDcrIndirectAddrReadAPUUDIReg(XDCR_2_BASEADDR,
                             UDInum);
    case 0x300:
        return XIo_mDcrIndirectAddrReadAPUUDIReg(XDCR_3_BASEADDR,
                             UDInum);
    default:
        return XIo_mDcrIndirectAddrReadAPUUDIReg(XDCR_0_BASEADDR,
                             UDInum);
    }
}

/*****************************************************************************/
/**
*
* Writes the value to the APU UDI DCR using the indirect access method.
*
* @param    DcrBase is the base of the block of DCR registers
* @param    UDInum is the intended destination APU register
* @param    Data is the value to be placed into the specified APU register
*
* @return
*
* None
*
* @note
*
* Uses the indirect addressing method available in V5 with PPC440.
*
****************************************************************************/
void XIo_DcrWriteAPUUDIReg(u32 DcrBase, short UDInum, u32 Data)
{
    switch (DcrBase) {
    case 0x000:
        XIo_mDcrIndirectAddrWriteAPUUDIReg(XDCR_0_BASEADDR, UDInum,
                           Data);
        return;
    case 0x100:
        XIo_mDcrIndirectAddrWriteAPUUDIReg(XDCR_1_BASEADDR, UDInum,
                           Data);
        return;
    case 0x200:
        XIo_mDcrIndirectAddrWriteAPUUDIReg(XDCR_2_BASEADDR, UDInum,
                           Data);
        return;
    case 0x300:
        XIo_mDcrIndirectAddrWriteAPUUDIReg(XDCR_3_BASEADDR, UDInum,
                           Data);
        return;
    default:
        XIo_mDcrIndirectAddrWriteAPUUDIReg(XDCR_0_BASEADDR, UDInum,
                           Data);
        return;
    }
}

/*****************************************************************************/
/**
*
* Locks DCR bus via the Global Status/Control register.
*
* @param    DcrBase is the base of the block of DCR registers
*
* @return
*
* None
*
* @note
*
* Care must be taken to not write a '1' to either timeout bit because
* it will be cleared. The internal PPC440 can clear both timeout bits but an
* external DCR master can only clear the external DCR master's timeout bit.
*
* Only available in V5 with PPC440.
*
****************************************************************************/
void XIo_DcrLock(u32 DcrBase)
{
    switch (DcrBase) {
    case 0x000:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        return;
    case 0x100:
        XIo_mDcrLock(XDCR_1_BASEADDR);
        return;
    case 0x200:
        XIo_mDcrLock(XDCR_2_BASEADDR);
        return;
    case 0x300:
        XIo_mDcrLock(XDCR_3_BASEADDR);
        return;
    default:
        XIo_mDcrLock(XDCR_0_BASEADDR);
        return;
    }
}

/*****************************************************************************/
/**
*
* Unlocks DCR bus via the Global Status/Control register.
*
* @param    DcrBase is the base of the block of DCR registers
*
* @return
*
* None
*
* @note
*
* Care must be taken to not write a '1' to either timeout bit because
* it will be cleared. The internal PPC440 can clear both timeout bits but an
* external DCR master can only clear the external DCR master's timeout bit.
*
* Only available in V5 with PPC440.
*
****************************************************************************/
void XIo_DcrUnlock(u32 DcrBase)
{
    switch (DcrBase) {
    case 0x000:
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
        return;
    case 0x100:
        XIo_mDcrUnlock(XDCR_1_BASEADDR);
        return;
    case 0x200:
        XIo_mDcrUnlock(XDCR_2_BASEADDR);
        return;
    case 0x300:
        XIo_mDcrUnlock(XDCR_3_BASEADDR);
        return;
    default:
        XIo_mDcrUnlock(XDCR_0_BASEADDR);
        return;
    }
}
