/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx FPGA SDI Tx timing controller driver
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Contacts: Saurabh Sengar <saurabhs@xilinx.com>
 */

#ifndef _XLNX_SDI_TIMING_H_
#define _XLNX_SDI_TIMING_H_

struct videomode;

void xlnx_stc_enable(void __iomem *base);
void xlnx_stc_disable(void __iomem *base);
void xlnx_stc_reset(void __iomem *base);
void xlnx_stc_sig(void __iomem *base, struct videomode *vm);

#endif /* _XLNX_SDI_TIMING_H_ */
