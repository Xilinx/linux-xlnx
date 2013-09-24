/*
xvdma.h
Wrapper client driver for xilinx VDMA Engine.

*/
#ifndef __XVDMA_H
#define __XVDMA_H

#include <linux/amba/xilinx_dma.h>

#define DRIVER_NAME     "xvdma"
#define XVDMA_SUSPEND   NULL
#define XVDMA_RESUME    NULL

#define XVDMA_MAJOR     10
#define XVDMA_MINOR     224

#define MAX_DEVICES     4
#define MAX_FRAMES      5
#define DMA_CHAN_RESET 10

#define XVDMA_IOCTL_BASE        'W'
#define XVDMA_GET_NUM_DEVICES   _IO(XVDMA_IOCTL_BASE, 0)
#define XVDMA_GET_DEV_INFO      _IO(XVDMA_IOCTL_BASE, 1)
#define XVDMA_DEVICE_CONTROL    _IO(XVDMA_IOCTL_BASE, 2)
#define XVDMA_PREP_BUF          _IO(XVDMA_IOCTL_BASE, 3)
#define XVDMA_START_TRANSFER    _IO(XVDMA_IOCTL_BASE, 4)
#define XVDMA_STOP_TRANSFER     _IO(XVDMA_IOCTL_BASE, 5)

#define XVDMA_DEVICE_ID_SHIFT   28

struct xvdma_drvdata {
	struct device *dev;
	struct cdev cdev;       /* Char device structure */
	dev_t devt;
};

struct xvdma_dev {

	u32 tx_chan;
	u32 rx_chan;
	u32 device_id;
};

struct xvdma_chan_cfg {
	struct xilinx_vdma_config config;
	u32 chan;
};

struct xvdma_buf_info {
	u32 chan;
	u32 device_id;
	u32 direction;
	u32 shared_buffer;
	u32 mem_type;
	u32 fixed_buffer;
	u32 buf_size;
	u32 addr_base;
	u32 frm_cnt;
	u32 callback;
};

struct xvdma_transfer {
	u32 chan;
	u32 wait;
};

struct chan_buf {
	u32 device_id;
	dma_addr_t dma_addr[MAX_FRAMES];
};

void xvdma_device_control(struct xvdma_chan_cfg *);
void xvdma_prep_slave_sg(struct xvdma_buf_info *);
void xvdma_start_transfer(struct xvdma_transfer *);
void xvdma_stop_transfer(struct dma_chan *);

#endif
