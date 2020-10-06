/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef __XLNXSYNC_H__
#define __XLNXSYNC_H__

#define XLNXSYNC_IOCTL_HDR_VER		0x10004

/*
 * This is set in the fb_id of struct xlnxsync_chan_config when
 * configuring the channel. This makes the driver auto search for
 * a free framebuffer slot.
 */
#define XLNXSYNC_AUTO_SEARCH		0xFF

#define XLNXSYNC_MAX_ENC_CHAN		4
#define XLNXSYNC_MAX_DEC_CHAN		2
#define XLNXSYNC_BUF_PER_CHAN		3

#define XLNXSYNC_PROD			0
#define XLNXSYNC_CONS			1
#define XLNXSYNC_IO			2

#define XLNXSYNC_MAX_CORES		4

/**
 * struct xlnxsync_err_intr - Channel error interrupt types
 * @prod_sync: Producer synchronization error interrupt
 * @prod_wdg: Producer watchdog interrupt
 * @cons_sync: Consumer synchronization error interrupt
 * @cons_wdg: Consumer watchdog interrupt
 * @ldiff: Luma buffer difference interrupt
 * @cdiff: Chroma buffer difference interrupt
 */
struct xlnxsync_err_intr {
	u8 prod_sync : 1;
	u8 prod_wdg : 1;
	u8 cons_sync : 1;
	u8 cons_wdg : 1;
	u8 ldiff : 1;
	u8 cdiff : 1;
};

/**
 * struct xlnxsync_intr - Channel Interrupt types
 * @hdr_ver: IOCTL header version
 * @err: Structure for error interrupts
 * @prod_lfbdone: Producer luma frame buffer done interrupt
 * @prod_cfbdone: Producer chroma frame buffer done interrupt
 * @cons_lfbdone: Consumer luma frame buffer done interrupt
 * @cons_cfbdone: Consumer chroma frame buffer done interrupt
 */
struct xlnxsync_intr {
	u64 hdr_ver;
	struct xlnxsync_err_intr err;
	u8 prod_lfbdone : 1;
	u8 prod_cfbdone : 1;
	u8 cons_lfbdone : 1;
	u8 cons_cfbdone : 1;
};

/**
 * struct xlnxsync_chan_config - Synchronizer channel configuration struct
 * @hdr_ver: IOCTL header version
 * @luma_start_offset: Start offset of Luma buffer
 * @chroma_start_offset: Start offset of Chroma buffer
 * @luma_end_offset: End offset of Luma buffer
 * @chroma_end_offset: End offset of Chroma buffer
 * @luma_margin: Margin for Luma buffer
 * @chroma_margin: Margin for Chroma buffer
 * @luma_core_offset: Array of 4 offsets for luma
 * @chroma_core_offset: Array of 4 offsets for chroma
 * @dma_fd: File descriptor of dma
 * @fb_id: Framebuffer index. Valid values 0/1/2/XLNXSYNC_AUTO_SEARCH
 * @ismono: Flag to indicate if buffer is Luma only.
 * Valid 0..3 & XLNXSYNC_AUTO_SEARCH
 *
 * This structure contains the configuration for monitoring a particular
 * framebuffer on a particular channel.
 */
struct xlnxsync_chan_config {
	u64 hdr_ver;
	u64 luma_start_offset[XLNXSYNC_IO];
	u64 chroma_start_offset[XLNXSYNC_IO];
	u64 luma_end_offset[XLNXSYNC_IO];
	u64 chroma_end_offset[XLNXSYNC_IO];
	u32 luma_margin;
	u32 chroma_margin;
	u32 luma_core_offset[XLNXSYNC_MAX_CORES];
	u32 chroma_core_offset[XLNXSYNC_MAX_CORES];
	u32 dma_fd;
	u8 fb_id[XLNXSYNC_IO];
	u8 ismono[XLNXSYNC_IO];
};

/**
 * struct xlnxsync_clr_err - Clear channel error
 * @hdr_ver: IOCTL header version
 * @err: Structure for error interrupts
 */
struct xlnxsync_clr_err {
	u64 hdr_ver;
	struct xlnxsync_err_intr err;
};

/**
 * struct xlnxsync_fbdone - Framebuffer Done
 * @hdr_ver: IOCTL header version
 * @status: Framebuffer Done status
 */
struct xlnxsync_fbdone {
	u64 hdr_ver;
	u8 status[XLNXSYNC_BUF_PER_CHAN][XLNXSYNC_IO];
};

/**
 * struct xlnxsync_config - Synchronizer IP configuration
 * @hdr_ver: IOCTL header version
 * @encode: true if encoder type, false for decoder type
 * @max_channels: Maximum channels this IP supports
 * @active_channels: Number of active IP channels
 * @reserved_id: Reserved channel ID for instance
 */
struct xlnxsync_config {
	u64	hdr_ver;
	u8	encode;
	u8	max_channels;
	u8	active_channels;
	u8	reserved_id;
	u32	reserved[10];
};

/**
 * struct xlnxsync_stat - Sync IP channel status
 * @hdr_ver: IOCTL header version
 * @fbdone: for every pair of luma/chroma buffer for every producer/consumer
 * @enable: channel enable
 * @err: Structure for error interrupts
 */
struct xlnxsync_stat {
	u64 hdr_ver;
	u8 fbdone[XLNXSYNC_BUF_PER_CHAN][XLNXSYNC_IO];
	u8 enable;
	struct xlnxsync_err_intr err;
};

#define XLNXSYNC_MAGIC			'X'

/*
 * This ioctl is used to get the IP config (i.e. encode / decode)
 * and max number of channels
 */
#define XLNXSYNC_GET_CFG		_IOR(XLNXSYNC_MAGIC, 1,\
					     struct xlnxsync_config *)
/* This ioctl is used to get the channel status */
#define XLNXSYNC_CHAN_GET_STATUS	_IOR(XLNXSYNC_MAGIC, 2, u32 *)
/* This is used to set the framebuffer address for a channel */
#define XLNXSYNC_CHAN_SET_CONFIG	_IOW(XLNXSYNC_MAGIC, 3,\
					     struct xlnxsync_chan_config *)
/* Enable a channel. */
#define XLNXSYNC_CHAN_ENABLE		_IO(XLNXSYNC_MAGIC, 4)
/* Disable a channel. */
#define XLNXSYNC_CHAN_DISABLE		_IO(XLNXSYNC_MAGIC, 5)
/* This is used to clear the Sync and Watchdog errors  for a channel */
#define XLNXSYNC_CHAN_CLR_ERR		_IOW(XLNXSYNC_MAGIC, 6,\
					     struct xlnxsync_clr_err *)
/* This is used to get the framebuffer done status for a channel */
#define XLNXSYNC_CHAN_GET_FBDONE_STAT	_IOR(XLNXSYNC_MAGIC, 7,\
					     struct xlnxsync_fbdone *)
/* This is used to clear the framebuffer done status for a channel */
#define XLNXSYNC_CHAN_CLR_FBDONE_STAT	_IOW(XLNXSYNC_MAGIC, 8,\
					     struct xlnxsync_fbdone *)
/* This is used to set interrupt mask */
#define XLNXSYNC_CHAN_SET_INTR_MASK	_IOW(XLNXSYNC_MAGIC, 9,\
					     struct xlnxsync_intr *)
#endif
