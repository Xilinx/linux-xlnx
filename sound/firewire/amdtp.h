#ifndef SOUND_FIREWIRE_AMDTP_H_INCLUDED
#define SOUND_FIREWIRE_AMDTP_H_INCLUDED

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <sound/asound.h>
#include "packets-buffer.h"

/**
 * enum cip_out_flags - describes details of the streaming protocol
 * @CIP_NONBLOCKING: In non-blocking mode, each packet contains
 *	sample_rate/8000 samples, with rounding up or down to adjust
 *	for clock skew and left-over fractional samples.  This should
 *	be used if supported by the device.
 * @CIP_BLOCKING: In blocking mode, each packet contains either zero or
 *	SYT_INTERVAL samples, with these two types alternating so that
 *	the overall sample rate comes out right.
 * @CIP_HI_DUALWIRE: At rates above 96 kHz, pretend that the stream runs
 *	at half the actual sample rate with twice the number of channels;
 *	two samples of a channel are stored consecutively in the packet.
 *	Requires blocking mode and SYT_INTERVAL-aligned PCM buffer size.
 */
enum cip_out_flags {
	CIP_NONBLOCKING	= 0x00,
	CIP_BLOCKING	= 0x01,
	CIP_HI_DUALWIRE	= 0x02,
};

/**
 * enum cip_sfc - a stream's sample rate
 */
enum cip_sfc {
	CIP_SFC_32000  = 0,
	CIP_SFC_44100  = 1,
	CIP_SFC_48000  = 2,
	CIP_SFC_88200  = 3,
	CIP_SFC_96000  = 4,
	CIP_SFC_176400 = 5,
	CIP_SFC_192000 = 6,
	CIP_SFC_COUNT
};

#define AMDTP_OUT_PCM_FORMAT_BITS	(SNDRV_PCM_FMTBIT_S16 | \
					 SNDRV_PCM_FMTBIT_S32)

struct fw_unit;
struct fw_iso_context;
struct snd_pcm_substream;

struct amdtp_out_stream {
	struct fw_unit *unit;
	enum cip_out_flags flags;
	struct fw_iso_context *context;
	struct mutex mutex;

	enum cip_sfc sfc;
	bool dual_wire;
	unsigned int data_block_quadlets;
	unsigned int pcm_channels;
	unsigned int midi_ports;
	void (*transfer_samples)(struct amdtp_out_stream *s,
				 struct snd_pcm_substream *pcm,
				 __be32 *buffer, unsigned int frames);

	unsigned int syt_interval;
	unsigned int transfer_delay;
	unsigned int source_node_id_field;
	struct iso_packets_buffer buffer;

	struct snd_pcm_substream *pcm;
	struct tasklet_struct period_tasklet;

	int packet_index;
	unsigned int data_block_counter;

	unsigned int data_block_state;

	unsigned int last_syt_offset;
	unsigned int syt_offset_state;

	unsigned int pcm_buffer_pointer;
	unsigned int pcm_period_pointer;
	bool pointer_flush;
};

int amdtp_out_stream_init(struct amdtp_out_stream *s, struct fw_unit *unit,
			  enum cip_out_flags flags);
void amdtp_out_stream_destroy(struct amdtp_out_stream *s);

void amdtp_out_stream_set_parameters(struct amdtp_out_stream *s,
				     unsigned int rate,
				     unsigned int pcm_channels,
				     unsigned int midi_ports);
unsigned int amdtp_out_stream_get_max_payload(struct amdtp_out_stream *s);

int amdtp_out_stream_start(struct amdtp_out_stream *s, int channel, int speed);
void amdtp_out_stream_update(struct amdtp_out_stream *s);
void amdtp_out_stream_stop(struct amdtp_out_stream *s);

void amdtp_out_stream_set_pcm_format(struct amdtp_out_stream *s,
				     snd_pcm_format_t format);
void amdtp_out_stream_pcm_prepare(struct amdtp_out_stream *s);
unsigned long amdtp_out_stream_pcm_pointer(struct amdtp_out_stream *s);
void amdtp_out_stream_pcm_abort(struct amdtp_out_stream *s);

extern const unsigned int amdtp_syt_intervals[CIP_SFC_COUNT];

static inline bool amdtp_out_stream_running(struct amdtp_out_stream *s)
{
	return !IS_ERR(s->context);
}

/**
 * amdtp_out_streaming_error - check for streaming error
 * @s: the AMDTP output stream
 *
 * If this function returns true, the stream's packet queue has stopped due to
 * an asynchronous error.
 */
static inline bool amdtp_out_streaming_error(struct amdtp_out_stream *s)
{
	return s->packet_index < 0;
}

/**
 * amdtp_out_stream_pcm_trigger - start/stop playback from a PCM device
 * @s: the AMDTP output stream
 * @pcm: the PCM device to be started, or %NULL to stop the current device
 *
 * Call this function on a running isochronous stream to enable the actual
 * transmission of PCM data.  This function should be called from the PCM
 * device's .trigger callback.
 */
static inline void amdtp_out_stream_pcm_trigger(struct amdtp_out_stream *s,
						struct snd_pcm_substream *pcm)
{
	ACCESS_ONCE(s->pcm) = pcm;
}

static inline bool cip_sfc_is_base_44100(enum cip_sfc sfc)
{
	return sfc & 1;
}

#endif
