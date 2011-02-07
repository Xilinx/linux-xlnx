/* arch/arm/mach-xilinx/include/mach/dma.h
 *
 *  Copyright (C) 2009 Xilinx
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_DMA_H__
#define __ASM_ARCH_DMA_H__

#include <linux/ioport.h>

#ifndef MAX_DMA_DEVICES
#define MAX_DMA_DEVICES 2
#endif

#ifndef MAX_DMA_CHANNELS
#define MAX_DMA_CHANNELS 8
#endif

/**
 * pl330_platform_config - Platform configuration specific to PL330.
 * Each device uses one instance.
 * @channels: Number of channels for the device.
 * @starting_channel: Starting channel number in the driver API.
 */
struct pl330_platform_config {
	unsigned int channels;
	unsigned int starting_channel;
};

#endif
