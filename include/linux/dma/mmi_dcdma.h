/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DMA_MMI_DCDMA_H
#define __LINUX_DMA_MMI_DCDMA_H

#include <linux/types.h>

/**
 * struct mmi_dcdma_peripheral_config - MMI DCDMA peripheral configuration
 * @video_group: flag if multi-channel operation is needed for video channels
 * @num_audio_channels: number of active channels in the audio stream, 1 to 8,
 *	or 0 when no stream is configured
 * @audio_src_vaddr: CPU virtual address of the cyclic audio buffer
 * @audio_buffer_bytes: size of the cyclic audio buffer
 * @audio_period_bytes: size of one period of the cyclic audio buffer
 *
 * The audio fields describe the buffer the DC audio driver hands to the DMA
 * engine. They are only meaningful on the audio channel and are ignored
 * elsewhere.
 *
 * A stream carrying fewer than 8 channels has to be expanded before DCDMA can
 * transfer it, so 1 to 7 select the expansion path in the DMA driver while 8 is
 * transferred out of @audio_src_vaddr directly.
 *
 * @audio_src_vaddr stays valid until the DC replaces the configuration, which
 * it does from both hw_params() and hw_free(), so the pointer does not outlive
 * the managed PCM buffer it names.
 */
struct mmi_dcdma_peripheral_config {
	bool		video_group;
	u32		num_audio_channels;
	void		*audio_src_vaddr;
	size_t		audio_buffer_bytes;
	size_t		audio_period_bytes;
};

#endif /* __LINUX_DMA_MMI_DCDMA_H */
