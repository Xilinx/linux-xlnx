/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __XLNXSYNC_H__
#define __XLNXSYNC_H__

/* Bit offset in channel status byte */
/* x = channel */
#define XLNXSYNC_CHX_FB0_MASK(x)		BIT(0 + ((x) << 3))
#define XLNXSYNC_CHX_FB1_MASK(x)		BIT(1 + ((x) << 3))
#define XLNXSYNC_CHX_FB2_MASK(x)		BIT(2 + ((x) << 3))
#define XLNXSYNC_CHX_ENB_MASK(x)		BIT(3 + ((x) << 3))
#define XLNXSYNC_CHX_SYNC_ERR_MASK(x)		BIT(4 + ((x) << 3))
#define XLNXSYNC_CHX_WDG_ERR_MASK(x)		BIT(5 + ((x) << 3))

/*
 * This is set in the fb_id or channel_id of struct xlnxsync_chan_config when
 * configuring the channel. This makes the driver auto search for the free
 * framebuffer or channel slot.
 */
#define XLNXSYNC_AUTO_SEARCH		0xFF

#define XLNXSYNC_MAX_ENC_CHANNEL	4
#define XLNXSYNC_MAX_DEC_CHANNEL	2
#define XLNXSYNC_BUF_PER_CHANNEL	3

/**
 * struct xlnxsync_chan_config - Synchronizer channel configuration struct
 * @luma_start_address: Start address of Luma buffer
 * @chroma_start_address: Start address of Chroma buffer
 * @luma_end_address: End address of Luma buffer
 * @chroma_end_address: End address of Chroma buffer
 * @luma_margin: Margin for Luma buffer
 * @chroma_margin: Margin for Chroma buffer
 * @fb_id: Framebuffer index. Valid values 0/1/2/XLNXSYNC_AUTO_SEARCH
 * @channel_id: Channel index to be configured.
 * Valid 0..3 & XLNXSYNC_AUTO_SEARCH
 * @ismono: Flag to indicate if buffer is Luma only.
 *
 * This structure contains the configuration for monitoring a particular
 * framebuffer on a particular channel.
 */
struct xlnxsync_chan_config {
	u64 luma_start_address;
	u64 chroma_start_address;
	u64 luma_end_address;
	u64 chroma_end_address;
	u32 luma_margin;
	u32 chroma_margin;
	u8 fb_id;
	u8 channel_id;
	u8 ismono;
};

/**
 * struct xlnxsync_clr_err - Clear channel error
 * @channel_id: Channel id whose error needs to be cleared
 * @sync_err: Set this to clear sync error
 * @wdg_err: Set this to clear watchdog error
 */
struct xlnxsync_clr_err {
	u8 channel_id;
	u8 sync_err;
	u8 wdg_err;
};

/**
 * struct xlnxsync_fbdone - Framebuffer Done
 * @status: Framebuffer Done status
 */
struct xlnxsync_fbdone {
	u8 status[XLNXSYNC_MAX_ENC_CHANNEL][XLNXSYNC_BUF_PER_CHANNEL];
};

/**
 * struct xlnxsync_config - Synchronizer IP configuration
 * @encode: true if encoder type, false for decoder type
 * @max_channels: Maximum channels this IP supports
 */
struct xlnxsync_config {
	u8	encode;
	u8	max_channels;
};

#define XLNXSYNC_MAGIC			'X'

/*
 * This ioctl is used to get the IP config (i.e. encode / decode)
 * and max number of channels
 */
#define XLNXSYNC_GET_CFG		_IOR(XLNXSYNC_MAGIC, 1,\
					     struct xlnxsync_config *)
/* This ioctl is used to get the channel status */
#define XLNXSYNC_GET_CHAN_STATUS	_IOR(XLNXSYNC_MAGIC, 2, u32 *)
/* This is used to set the framebuffer address for a channel */
#define XLNXSYNC_SET_CHAN_CONFIG	_IOW(XLNXSYNC_MAGIC, 3,\
					     struct xlnxsync_chan_config *)
/* Enable a channel. The argument is channel number between 0 and 3 */
#define XLNXSYNC_CHAN_ENABLE		_IOR(XLNXSYNC_MAGIC, 4, u8)
/* Enable a channel. The argument is channel number between 0 and 3 */
#define XLNXSYNC_CHAN_DISABLE		_IOR(XLNXSYNC_MAGIC, 5, u8)
/* This is used to clear the Sync and Watchdog errors  for a channel */
#define XLNXSYNC_CLR_CHAN_ERR		_IOW(XLNXSYNC_MAGIC, 6,\
					     struct xlnxsync_clr_err *)
/* This is used to get the framebuffer done status for a channel */
#define XLNXSYNC_GET_CHAN_FBDONE_STAT	_IOR(XLNXSYNC_MAGIC, 7,\
					     struct xlnxsync_fbdone *)
/* This is used to clear the framebuffer done status for a channel */
#define XLNXSYNC_CLR_CHAN_FBDONE_STAT	_IOW(XLNXSYNC_MAGIC, 8,\
					     struct xlnxsync_fbdone *)

#endif
