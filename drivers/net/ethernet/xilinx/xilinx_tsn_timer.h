/*
 * Xilinx FPGA Xilinx TSN timer module header.
 *
 * Copyright (c) 2017 Xilinx Pvt., Ltd
 *
 * Author: Syed S <syeds@xilinx.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _XILINX_TSN_H_
#define _XILINX_TSN_H_

#include <linux/platform_device.h>

#define XAE_RTC_OFFSET			0x12800
/* RTC Nanoseconds Field Offset Register */
#define XTIMER1588_RTC_OFFSET_NS	0x00000
/* RTC Seconds Field Offset Register - Low */
#define XTIMER1588_RTC_OFFSET_SEC_L	0x00008
/* RTC Seconds Field Offset Register - High */
#define XTIMER1588_RTC_OFFSET_SEC_H	0x0000C
/* RTC Increment */
#define XTIMER1588_RTC_INCREMENT	0x00010
/* Current TOD Nanoseconds - RO */
#define XTIMER1588_CURRENT_RTC_NS	0x00014
/* Current TOD Seconds -Low RO  */
#define XTIMER1588_CURRENT_RTC_SEC_L	0x00018
/* Current TOD Seconds -High RO */
#define XTIMER1588_CURRENT_RTC_SEC_H	0x0001C
#define XTIMER1588_SYNTONIZED_NS	0x0002C
#define XTIMER1588_SYNTONIZED_SEC_L	0x00030
#define XTIMER1588_SYNTONIZED_SEC_H	0x00034
/* Write to Bit 0 to clear the interrupt */
#define XTIMER1588_INTERRUPT		0x00020
/* 8kHz Pulse Offset Register */
#define XTIMER1588_8KPULSE		0x00024
/* Correction Field - Low */
#define XTIMER1588_CF_L			0x0002C
/* Correction Field - Low */
#define XTIMER1588_CF_H			0x00030

#define XTIMER1588_RTC_MASK  ((1 << 26) - 1)
#define XTIMER1588_INT_SHIFT 0
#define NANOSECOND_BITS 20
#define NANOSECOND_MASK ((1 << NANOSECOND_BITS) - 1)
#define SECOND_MASK ((1 << (32 - NANOSECOND_BITS)) - 1)
#define XTIMER1588_RTC_INCREMENT_SHIFT 20
#define PULSESIN1PPS 128

/* Read/Write access to the registers */
#ifndef out_be32
#if defined(CONFIG_ARCH_ZYNQ) || defined(CONFIG_ARCH_ZYNQMP)
#define in_be32(offset)		__raw_readl(offset)
#define out_be32(offset, val)	__raw_writel(val, offset)
#endif
#endif

/* The tsn ptp module will set this variable */
extern int axienet_phc_index;

void *axienet_ptp_timer_probe(void __iomem *base,
			      struct platform_device *pdev);
int axienet_ptp_timer_remove(void *priv);
int axienet_get_phc_index(void *priv);
#endif
