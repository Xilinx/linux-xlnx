/*
 * xilinx_syms.c
 *
 * This file EXPORT_SYMBOL_GPL's all of the Xilinx entry points.
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under the terms
 * of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/module.h>

#include "xbasic_types.h"
EXPORT_SYMBOL_GPL(XAssert);
EXPORT_SYMBOL_GPL(XAssertSetCallback);
EXPORT_SYMBOL_GPL(XAssertStatus);
extern u32 XWaitInAssert;
EXPORT_SYMBOL_GPL(XWaitInAssert);

#include "xdma_channel.h"
EXPORT_SYMBOL_GPL(XDmaChannel_CommitPuts);
EXPORT_SYMBOL_GPL(XDmaChannel_CreateSgList);
EXPORT_SYMBOL_GPL(XDmaChannel_DecrementPktCount);
EXPORT_SYMBOL_GPL(XDmaChannel_GetControl);
EXPORT_SYMBOL_GPL(XDmaChannel_GetDescriptor);
EXPORT_SYMBOL_GPL(XDmaChannel_GetIntrEnable);
EXPORT_SYMBOL_GPL(XDmaChannel_GetIntrStatus);
EXPORT_SYMBOL_GPL(XDmaChannel_GetPktCount);
EXPORT_SYMBOL_GPL(XDmaChannel_GetPktThreshold);
EXPORT_SYMBOL_GPL(XDmaChannel_GetPktWaitBound);
EXPORT_SYMBOL_GPL(XDmaChannel_GetStatus);
EXPORT_SYMBOL_GPL(XDmaChannel_GetVersion);
EXPORT_SYMBOL_GPL(XDmaChannel_Initialize);
EXPORT_SYMBOL_GPL(XDmaChannel_IsReady);
EXPORT_SYMBOL_GPL(XDmaChannel_IsSgListEmpty);
EXPORT_SYMBOL_GPL(XDmaChannel_PutDescriptor);
EXPORT_SYMBOL_GPL(XDmaChannel_Reset);
EXPORT_SYMBOL_GPL(XDmaChannel_SelfTest);
EXPORT_SYMBOL_GPL(XDmaChannel_SetControl);
EXPORT_SYMBOL_GPL(XDmaChannel_SetIntrEnable);
EXPORT_SYMBOL_GPL(XDmaChannel_SetIntrStatus);
EXPORT_SYMBOL_GPL(XDmaChannel_SetPktThreshold);
EXPORT_SYMBOL_GPL(XDmaChannel_SetPktWaitBound);
EXPORT_SYMBOL_GPL(XDmaChannel_SgStart);
EXPORT_SYMBOL_GPL(XDmaChannel_SgStop);
EXPORT_SYMBOL_GPL(XDmaChannel_Transfer);

#include "xipif_v1_23_b.h"
//EXPORT_SYMBOL_GPL(XIpIfV123b_SelfTest);

#include "xpacket_fifo_v2_00_a.h"
EXPORT_SYMBOL_GPL(XPacketFifoV200a_Initialize);
EXPORT_SYMBOL_GPL(XPacketFifoV200a_Read);
EXPORT_SYMBOL_GPL(XPacketFifoV200a_SelfTest);
EXPORT_SYMBOL_GPL(XPacketFifoV200a_Write);

#include "xio.h"
EXPORT_SYMBOL_GPL(XIo_Out8);
EXPORT_SYMBOL_GPL(XIo_In8);
EXPORT_SYMBOL_GPL(XIo_Out16);
EXPORT_SYMBOL_GPL(XIo_In16);
EXPORT_SYMBOL_GPL(XIo_Out32);
EXPORT_SYMBOL_GPL(XIo_In32);

#include "xversion.h"
EXPORT_SYMBOL_GPL(XVersion_Copy);
EXPORT_SYMBOL_GPL(XVersion_FromString);
EXPORT_SYMBOL_GPL(XVersion_IsEqual);
EXPORT_SYMBOL_GPL(XVersion_Pack);
EXPORT_SYMBOL_GPL(XVersion_ToString);
EXPORT_SYMBOL_GPL(XVersion_UnPack);
