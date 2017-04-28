/*
 * Xilinx PS PCIe DMA Engine support header file
 *
 * Copyright (C) 2010-2014 Xilinx, Inc. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#ifndef __DMA_XILINX_PS_PCIE_H
#define __DMA_XILINX_PS_PCIE_H

#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#define XLNX_PLATFORM_DRIVER_NAME "xlnx-platform-dma-driver"

#define ZYNQMP_DMA_DEVID	(0xD024)
#define ZYNQMP_RC_DMA_DEVID	(0xD021)

#define MAX_ALLOWED_CHANNELS_IN_HW	4

#define MAX_NUMBER_OF_CHANNELS	MAX_ALLOWED_CHANNELS_IN_HW

#define DEFAULT_DMA_QUEUES	4
#define TWO_DMA_QUEUES		2

#define NUMBER_OF_BUFFER_DESCRIPTORS	1999
#define MAX_DESCRIPTORS			65536

#define CHANNEL_COAELSE_COUNT		0

#define CHANNEL_POLL_TIMER_FREQUENCY	1000 /* in milli seconds */

#define PCIE_AXI_DIRECTION	DMA_TO_DEVICE
#define AXI_PCIE_DIRECTION	DMA_FROM_DEVICE

/**
 * struct BAR_PARAMS - PCIe Bar Parameters
 * @BAR_PHYS_ADDR: PCIe BAR Physical address
 * @BAR_LENGTH: Length of PCIe BAR
 * @BAR_VIRT_ADDR: Virtual Address to access PCIe BAR
 */
struct BAR_PARAMS {
	dma_addr_t BAR_PHYS_ADDR; /**< Base physical address of BAR memory */
	unsigned long BAR_LENGTH; /**< Length of BAR memory window */
	void *BAR_VIRT_ADDR;      /**< Virtual Address of mapped BAR memory */
};

/**
 * struct ps_pcie_dma_channel_match - Match structure for dma clients
 * @pci_vendorid: PCIe Vendor id of PS PCIe DMA device
 * @pci_deviceid: PCIe Device id of PS PCIe DMA device
 * @board_number: Unique id to identify individual device in a system
 * @channel_number: Unique channel number of the device
 * @direction: DMA channel direction
 * @bar_params: Pointer to BAR_PARAMS for accessing application specific data
 */
struct ps_pcie_dma_channel_match {
	u16 pci_vendorid;
	u16 pci_deviceid;
	u16 board_number;
	u16 channel_number;
	enum dma_data_direction direction;
	struct BAR_PARAMS *bar_params;
};

#endif
