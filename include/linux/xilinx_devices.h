/*
 * include/linux/xilinx_devices.h
 *
 * Definitions for any platform device related flags or structures for
 * Xilinx EDK IPs
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2005 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#ifdef __KERNEL__
#ifndef _XILINX_DEVICE_H_
#define _XILINX_DEVICE_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/platform_device.h>

/* ML300/403 reference design framebuffer driver platform data struct */
struct xilinxfb_platform_data {
       u32 rotate_screen;
       u32 screen_height_mm;
       u32 screen_width_mm;
};

/*- 10/100 Mb Ethernet Controller IP (XEMAC) -*/

struct xemac_platform_data {
	u32 device_flags;
	u32 dma_mode;
	u32 has_mii;
	u32 has_err_cnt;
	u32 has_cam;
	u32 has_jumbo;
	u32 tx_dre;
	u32 rx_dre;
	u32 tx_hw_csum;
	u32 rx_hw_csum;
	u8 mac_addr[6];
};

/* Flags related to XEMAC device features */
#define XEMAC_HAS_ERR_COUNT	0x00000001
#define XEMAC_HAS_MII		0x00000002
#define XEMAC_HAS_CAM		0x00000004
#define XEMAC_HAS_JUMBO		0x00000008

/* Possible DMA modes supported by XEMAC */
#define XEMAC_DMA_NONE		1
#define XEMAC_DMA_SIMPLE	2	/* simple 2 channel DMA */
#define XEMAC_DMA_SGDMA		3	/* scatter gather DMA */

/*- 10/100 Mb Ethernet Controller IP (XEMACLITE) -*/
struct xemaclite_platform_data {
	u32 tx_ping_pong;
	u32 rx_ping_pong;
	u8 mac_addr[6];
};

/*- 10/100/1000 Mb Ethernet Controller IP (XTEMAC) -*/

struct xtemac_platform_data {
#ifdef XPAR_TEMAC_0_INCLUDE_RX_CSUM
	u8 tx_dre;
	u8 rx_dre;
	u8 tx_csum;
	u8 rx_csum;
	u8 phy_type;
#endif
	u8 dma_mode;
	u32 rx_pkt_fifo_depth;
	u32 tx_pkt_fifo_depth;
	u16 mac_fifo_depth;
	u8 dcr_host;
	u8 dre;

	u8 mac_addr[6];
};

/* Possible DMA modes supported by XTEMAC */
#define XTEMAC_DMA_NONE		1
#define XTEMAC_DMA_SIMPLE	2	/* simple 2 channel DMA */
#define XTEMAC_DMA_SGDMA	3	/* scatter gather DMA */


/* LLTEMAC platform data */
struct xlltemac_platform_data {
	u8 tx_csum;
	u8 rx_csum;
	u8 phy_type;
	u8 dcr_host;
	u8 ll_dev_type;
	u32 ll_dev_baseaddress;
	u32 ll_dev_dma_rx_irq;
	u32 ll_dev_dma_tx_irq;
	u32 ll_dev_fifo_irq;

	u8 mac_addr[6];
};

/*- GPIO -*/

/* Flags related to XGPIO device features */
#define XGPIO_IS_DUAL		0x00000001

#endif /* _XILINX_DEVICE_H_ */
#endif /* __KERNEL__ */
