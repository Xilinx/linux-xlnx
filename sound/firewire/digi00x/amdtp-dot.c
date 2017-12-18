/*
 * amdtp-dot.c - a part of driver for Digidesign Digi 002/003 family
 *
 * Copyright (c) 2014-2015 Takashi Sakamoto
 * Copyright (C) 2012 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2012 Damien Zammit <damien@zamaudio.com>
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#include <sound/pcm.h>
#include "digi00x.h"

#define CIP_FMT_AM		0x10

/* 'Clock-based rate control mode' is just supported. */
#define AMDTP_FDF_AM824		0x00

/*
 * Nominally 3125 bytes/second, but the MIDI port's clock might be
 * 1% too slow, and the bus clock 100 ppm too fast.
 */
#define MIDI_BYTES_PER_SECOND	3093

/*
 * Several devices look only at the first eight data blocks.
 * In any case, this is more than enough for the MIDI data rate.
 */
#define MAX_MIDI_RX_BLOCKS	8

/*
 * The double-oh-three algorithm was discovered by Robin Gareus and Damien
 * Zammit in 2012, with reverse-engineering for Digi 003 Rack.
 */
struct dot_state {
	u8 carry;
	u8 idx;
	unsigned int off;
};

struct amdtp_dot {
	unsigned int pcm_channels;
	struct dot_state state;

	unsigned int midi_ports;
	/* 2 = MAX(DOT_MIDI_IN_PORTS, DOT_MIDI_OUT_PORTS) */
	struct snd_rawmidi_substream *midi[2];
	int midi_fifo_used[2];
	int midi_fifo_limit;

	void (*transfer_samples)(struct amdtp_stream *s,
				 struct snd_pcm_substream *pcm,
				 __be32 *buffer, unsigned int frames);
};

/*
 * double-oh-three look up table
 *
 * @param idx index byte (audio-sample data) 0x00..0xff
 * @param off channel offset shift
 * @return salt to XOR with given data
 */
#define BYTE_PER_SAMPLE (4)
#define MAGIC_DOT_BYTE (2)
#define MAGIC_BYTE_OFF(x) (((x) * BYTE_PER_SAMPLE) + MAGIC_DOT_BYTE)
static u8 dot_scrt(const u8 idx, const unsigned int off)
{
	/*
	 * the length of the added pattern only depends on the lower nibble
	 * of the last non-zero data
	 */
	static const u8 len[16] = {0, 1, 3, 5, 7, 9, 11, 13, 14,
				   12, 10, 8, 6, 4, 2, 0};

	/*
	 * the lower nibble of the salt. Interleaved sequence.
	 * this is walked backwards according to len[]
	 */
	static const u8 nib[15] = {0x8, 0x7, 0x9, 0x6, 0xa, 0x5, 0xb, 0x4,
				   0xc, 0x3, 0xd, 0x2, 0xe, 0x1, 0xf};

	/* circular list for the salt's hi nibble. */
	static const u8 hir[15] = {0x0, 0x6, 0xf, 0x8, 0x7, 0x5, 0x3, 0x4,
				   0xc, 0xd, 0xe, 0x1, 0x2, 0xb, 0xa};

	/*
	 * start offset for upper nibble mapping.
	 * note: 9 is /special/. In the case where the high nibble == 0x9,
	 * hir[] is not used and - coincidentally - the salt's hi nibble is
	 * 0x09 regardless of the offset.
	 */
	static const u8 hio[16] = {0, 11, 12, 6, 7, 5, 1, 4,
				   3, 0x00, 14, 13, 8, 9, 10, 2};

	const u8 ln = idx & 0xf;
	const u8 hn = (idx >> 4) & 0xf;
	const u8 hr = (hn == 0x9) ? 0x9 : hir[(hio[hn] + off) % 15];

	if (len[ln] < off)
		return 0x00;

	return ((nib[14 + off - len[ln]]) | (hr << 4));
}

static void dot_encode_step(struct dot_state *state, __be32 *const buffer)
{
	u8 * const data = (u8 *) buffer;

	if (data[MAGIC_DOT_BYTE] != 0x00) {
		state->off = 0;
		state->idx = data[MAGIC_DOT_BYTE] ^ state->carry;
	}
	data[MAGIC_DOT_BYTE] ^= state->carry;
	state->carry = dot_scrt(state->idx, ++(state->off));
}

int amdtp_dot_set_parameters(struct amdtp_stream *s, unsigned int rate,
			     unsigned int pcm_channels)
{
	struct amdtp_dot *p = s->protocol;
	int err;

	if (amdtp_stream_running(s))
		return -EBUSY;

	/*
	 * A first data channel is for MIDI conformant data channel, the rest is
	 * Multi Bit Linear Audio data channel.
	 */
	err = amdtp_stream_set_parameters(s, rate, pcm_channels + 1);
	if (err < 0)
		return err;

	s->fdf = AMDTP_FDF_AM824 | s->sfc;

	p->pcm_channels = pcm_channels;

	if (s->direction == AMDTP_IN_STREAM)
		p->midi_ports = DOT_MIDI_IN_PORTS;
	else
		p->midi_ports = DOT_MIDI_OUT_PORTS;

	/*
	 * We do not know the actual MIDI FIFO size of most devices.  Just
	 * assume two bytes, i.e., one byte can be received over the bus while
	 * the previous one is transmitted over MIDI.
	 * (The value here is adjusted for midi_ratelimit_per_packet().)
	 */
	p->midi_fifo_limit = rate - MIDI_BYTES_PER_SECOND * s->syt_interval + 1;

	return 0;
}

static void write_pcm_s32(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			  __be32 *buffer, unsigned int frames)
{
	struct amdtp_dot *p = s->protocol;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u32 *src;

	channels = p->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	buffer++;
	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			buffer[c] = cpu_to_be32((*src >> 8) | 0x40000000);
			dot_encode_step(&p->state, &buffer[c]);
			src++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void write_pcm_s16(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			  __be32 *buffer, unsigned int frames)
{
	struct amdtp_dot *p = s->protocol;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	const u16 *src;

	channels = p->pcm_channels;
	src = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	buffer++;
	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			buffer[c] = cpu_to_be32((*src << 8) | 0x40000000);
			dot_encode_step(&p->state, &buffer[c]);
			src++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			src = (void *)runtime->dma_area;
	}
}

static void read_pcm_s32(struct amdtp_stream *s, struct snd_pcm_substream *pcm,
			 __be32 *buffer, unsigned int frames)
{
	struct amdtp_dot *p = s->protocol;
	struct snd_pcm_runtime *runtime = pcm->runtime;
	unsigned int channels, remaining_frames, i, c;
	u32 *dst;

	channels = p->pcm_channels;
	dst  = (void *)runtime->dma_area +
			frames_to_bytes(runtime, s->pcm_buffer_pointer);
	remaining_frames = runtime->buffer_size - s->pcm_buffer_pointer;

	buffer++;
	for (i = 0; i < frames; ++i) {
		for (c = 0; c < channels; ++c) {
			*dst = be32_to_cpu(buffer[c]) << 8;
			dst++;
		}
		buffer += s->data_block_quadlets;
		if (--remaining_frames == 0)
			dst = (void *)runtime->dma_area;
	}
}

static void write_pcm_silence(struct amdtp_stream *s, __be32 *buffer,
			      unsigned int data_blocks)
{
	struct amdtp_dot *p = s->protocol;
	unsigned int channels, i, c;

	channels = p->pcm_channels;

	buffer++;
	for (i = 0; i < data_blocks; ++i) {
		for (c = 0; c < channels; ++c)
			buffer[c] = cpu_to_be32(0x40000000);
		buffer += s->data_block_quadlets;
	}
}

static bool midi_ratelimit_per_packet(struct amdtp_stream *s, unsigned int port)
{
	struct amdtp_dot *p = s->protocol;
	int used;

	used = p->midi_fifo_used[port];
	if (used == 0)
		return true;

	used -= MIDI_BYTES_PER_SECOND * s->syt_interval;
	used = max(used, 0);
	p->midi_fifo_used[port] = used;

	return used < p->midi_fifo_limit;
}

static inline void midi_use_bytes(struct amdtp_stream *s,
				  unsigned int port, unsigned int count)
{
	struct amdtp_dot *p = s->protocol;

	p->midi_fifo_used[port] += amdtp_rate_table[s->sfc] * count;
}

static void write_midi_messages(struct amdtp_stream *s, __be32 *buffer,
				unsigned int data_blocks)
{
	struct amdtp_dot *p = s->protocol;
	unsigned int f, port;
	int len;
	u8 *b;

	for (f = 0; f < data_blocks; f++) {
		port = (s->data_block_counter + f) % 8;
		b = (u8 *)&buffer[0];

		len = 0;
		if (port < p->midi_ports &&
		    midi_ratelimit_per_packet(s, port) &&
		    p->midi[port] != NULL)
			len = snd_rawmidi_transmit(p->midi[port], b + 1, 2);

		if (len > 0) {
			b[3] = (0x10 << port) | len;
			midi_use_bytes(s, port, len);
		} else {
			b[1] = 0;
			b[2] = 0;
			b[3] = 0;
		}
		b[0] = 0x80;

		buffer += s->data_block_quadlets;
	}
}

static void read_midi_messages(struct amdtp_stream *s, __be32 *buffer,
			       unsigned int data_blocks)
{
	struct amdtp_dot *p = s->protocol;
	unsigned int f, port, len;
	u8 *b;

	for (f = 0; f < data_blocks; f++) {
		b = (u8 *)&buffer[0];
		port = b[3] >> 4;
		len = b[3] & 0x0f;

		if (port < p->midi_ports && p->midi[port] && len > 0)
			snd_rawmidi_receive(p->midi[port], b + 1, len);

		buffer += s->data_block_quadlets;
	}
}

int amdtp_dot_add_pcm_hw_constraints(struct amdtp_stream *s,
				     struct snd_pcm_runtime *runtime)
{
	int err;

	/* This protocol delivers 24 bit data in 32bit data channel. */
	err = snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	if (err < 0)
		return err;

	return amdtp_stream_add_pcm_hw_constraints(s, runtime);
}

void amdtp_dot_set_pcm_format(struct amdtp_stream *s, snd_pcm_format_t format)
{
	struct amdtp_dot *p = s->protocol;

	if (WARN_ON(amdtp_stream_pcm_running(s)))
		return;

	switch (format) {
	default:
		WARN_ON(1);
		/* fall through */
	case SNDRV_PCM_FORMAT_S16:
		if (s->direction == AMDTP_OUT_STREAM) {
			p->transfer_samples = write_pcm_s16;
			break;
		}
		WARN_ON(1);
		/* fall through */
	case SNDRV_PCM_FORMAT_S32:
		if (s->direction == AMDTP_OUT_STREAM)
			p->transfer_samples = write_pcm_s32;
		else
			p->transfer_samples = read_pcm_s32;
		break;
	}
}

void amdtp_dot_midi_trigger(struct amdtp_stream *s, unsigned int port,
			  struct snd_rawmidi_substream *midi)
{
	struct amdtp_dot *p = s->protocol;

	if (port < p->midi_ports)
		ACCESS_ONCE(p->midi[port]) = midi;
}

static unsigned int process_tx_data_blocks(struct amdtp_stream *s,
					   __be32 *buffer,
					   unsigned int data_blocks,
					   unsigned int *syt)
{
	struct amdtp_dot *p = (struct amdtp_dot *)s->protocol;
	struct snd_pcm_substream *pcm;
	unsigned int pcm_frames;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm) {
		p->transfer_samples(s, pcm, buffer, data_blocks);
		pcm_frames = data_blocks;
	} else {
		pcm_frames = 0;
	}

	read_midi_messages(s, buffer, data_blocks);

	return pcm_frames;
}

static unsigned int process_rx_data_blocks(struct amdtp_stream *s,
					   __be32 *buffer,
					   unsigned int data_blocks,
					   unsigned int *syt)
{
	struct amdtp_dot *p = (struct amdtp_dot *)s->protocol;
	struct snd_pcm_substream *pcm;
	unsigned int pcm_frames;

	pcm = ACCESS_ONCE(s->pcm);
	if (pcm) {
		p->transfer_samples(s, pcm, buffer, data_blocks);
		pcm_frames = data_blocks;
	} else {
		write_pcm_silence(s, buffer, data_blocks);
		pcm_frames = 0;
	}

	write_midi_messages(s, buffer, data_blocks);

	return pcm_frames;
}

int amdtp_dot_init(struct amdtp_stream *s, struct fw_unit *unit,
		 enum amdtp_stream_direction dir)
{
	amdtp_stream_process_data_blocks_t process_data_blocks;
	enum cip_flags flags;

	/* Use different mode between incoming/outgoing. */
	if (dir == AMDTP_IN_STREAM) {
		flags = CIP_NONBLOCKING;
		process_data_blocks = process_tx_data_blocks;
	} else {
		flags = CIP_BLOCKING;
		process_data_blocks = process_rx_data_blocks;
	}

	return amdtp_stream_init(s, unit, dir, flags, CIP_FMT_AM,
				 process_data_blocks, sizeof(struct amdtp_dot));
}

void amdtp_dot_reset(struct amdtp_stream *s)
{
	struct amdtp_dot *p = s->protocol;

	p->state.carry = 0x00;
	p->state.idx = 0x00;
	p->state.off = 0;
}
