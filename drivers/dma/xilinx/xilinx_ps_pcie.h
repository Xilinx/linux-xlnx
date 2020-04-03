/*
 * Xilinx PS PCIe DMA Engine platform header file
 *
 * Copyright (C) 2010-2017 Xilinx, Inc. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#ifndef __XILINX_PS_PCIE_H
#define __XILINX_PS_PCIE_H

#include <linux/delay.h>
#include <linux/dma-direction.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/dma/xilinx_ps_pcie_dma.h>

/**
 * dma_platform_driver_register - This will be invoked by module init
 *
 * Return: returns status of platform_driver_register
 */
int dma_platform_driver_register(void);
/**
 * dma_platform_driver_unregister - This will be invoked by module exit
 *
 * Return: returns void after unregustering platform driver
 */
void dma_platform_driver_unregister(void);

#endif
