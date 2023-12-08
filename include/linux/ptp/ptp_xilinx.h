/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Xilinx PTP driver header file
 *
 * Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.
 */

#ifndef __PTP_PTP_XILINX_H__
#define __PTP_PTP_XILINX_H__

#include <linux/ptp_clock_kernel.h>

struct xlnx_ptp_timer {
	struct device		*dev;
	void __iomem		*baseaddr;
	struct ptp_clock	*ptp_clock;
	struct ptp_clock_info	ptp_clock_info;
	spinlock_t		reg_lock; /* For reg access */
	u64			incr;
	s64			timeoffset;
	s32			static_delay;
	int			phc_index;
	bool			use_sys_timer_only;
	int			irq;
	int			extts_enable;
	u32			period_0;
	u32			period_1;
};

#endif /* __PTP_PTP_XILINX_H__ */
