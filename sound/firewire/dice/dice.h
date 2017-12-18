/*
 * dice.h - a part of driver for Dice based devices
 *
 * Copyright (c) Clemens Ladisch
 * Copyright (c) 2014 Takashi Sakamoto
 *
 * Licensed under the terms of the GNU General Public License, version 2.
 */

#ifndef SOUND_DICE_H_INCLUDED
#define SOUND_DICE_H_INCLUDED

#include <linux/compat.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <sound/control.h>
#include <sound/core.h>
#include <sound/firewire.h>
#include <sound/hwdep.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/rawmidi.h>

#include "../amdtp-am824.h"
#include "../iso-resources.h"
#include "../lib.h"
#include "dice-interface.h"

/*
 * This module support maximum 2 pairs of tx/rx isochronous streams for
 * our convinience.
 *
 * In documents for ASICs called with a name of 'DICE':
 *  - ASIC for DICE II:
 *   - Maximum 2 tx and 4 rx are supported.
 *   - A packet supports maximum 16 data channels.
 *  - TCD2210/2210-E (so-called 'Dice Mini'):
 *   - Maximum 2 tx and 2 rx are supported.
 *   - A packet supports maximum 16 data channels.
 *  - TCD2220/2220-E (so-called 'Dice Jr.')
 *   - 2 tx and 2 rx are supported.
 *   - A packet supports maximum 16 data channels.
 *  - TCD3070-CH (so-called 'Dice III')
 *   - Maximum 2 tx and 2 rx are supported.
 *   - A packet supports maximum 32 data channels.
 *
 * For the above, MIDI conformant data channel is just on the first isochronous
 * stream.
 */
#define MAX_STREAMS	2

struct snd_dice {
	struct snd_card *card;
	struct fw_unit *unit;
	spinlock_t lock;
	struct mutex mutex;

	bool registered;
	struct delayed_work dwork;

	/* Offsets for sub-addresses */
	unsigned int global_offset;
	unsigned int rx_offset;
	unsigned int tx_offset;
	unsigned int sync_offset;
	unsigned int rsrv_offset;

	unsigned int clock_caps;

	struct fw_address_handler notification_handler;
	int owner_generation;
	u32 notification_bits;

	/* For uapi */
	int dev_lock_count; /* > 0 driver, < 0 userspace */
	bool dev_lock_changed;
	wait_queue_head_t hwdep_wait;

	/* For streaming */
	struct fw_iso_resources tx_resources[MAX_STREAMS];
	struct fw_iso_resources rx_resources[MAX_STREAMS];
	struct amdtp_stream tx_stream[MAX_STREAMS];
	struct amdtp_stream rx_stream[MAX_STREAMS];
	bool global_enabled;
	struct completion clock_accepted;
	unsigned int substreams_counter;

	bool force_two_pcms;
};

enum snd_dice_addr_type {
	SND_DICE_ADDR_TYPE_PRIVATE,
	SND_DICE_ADDR_TYPE_GLOBAL,
	SND_DICE_ADDR_TYPE_TX,
	SND_DICE_ADDR_TYPE_RX,
	SND_DICE_ADDR_TYPE_SYNC,
	SND_DICE_ADDR_TYPE_RSRV,
};

int snd_dice_transaction_write(struct snd_dice *dice,
			       enum snd_dice_addr_type type,
			       unsigned int offset,
			       void *buf, unsigned int len);
int snd_dice_transaction_read(struct snd_dice *dice,
			      enum snd_dice_addr_type type, unsigned int offset,
			      void *buf, unsigned int len);

static inline int snd_dice_transaction_write_global(struct snd_dice *dice,
						    unsigned int offset,
						    void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice,
					  SND_DICE_ADDR_TYPE_GLOBAL, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_global(struct snd_dice *dice,
						   unsigned int offset,
						   void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice,
					 SND_DICE_ADDR_TYPE_GLOBAL, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_tx(struct snd_dice *dice,
						unsigned int offset,
						void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_TX, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_tx(struct snd_dice *dice,
					       unsigned int offset,
					       void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_TX, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_rx(struct snd_dice *dice,
						unsigned int offset,
						void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_RX, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_rx(struct snd_dice *dice,
					       unsigned int offset,
					       void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_RX, offset,
					 buf, len);
}
static inline int snd_dice_transaction_write_sync(struct snd_dice *dice,
						  unsigned int offset,
						  void *buf, unsigned int len)
{
	return snd_dice_transaction_write(dice, SND_DICE_ADDR_TYPE_SYNC, offset,
					  buf, len);
}
static inline int snd_dice_transaction_read_sync(struct snd_dice *dice,
						 unsigned int offset,
						 void *buf, unsigned int len)
{
	return snd_dice_transaction_read(dice, SND_DICE_ADDR_TYPE_SYNC, offset,
					 buf, len);
}

int snd_dice_transaction_get_clock_source(struct snd_dice *dice,
					  unsigned int *source);
int snd_dice_transaction_get_rate(struct snd_dice *dice, unsigned int *rate);
int snd_dice_transaction_set_enable(struct snd_dice *dice);
void snd_dice_transaction_clear_enable(struct snd_dice *dice);
int snd_dice_transaction_init(struct snd_dice *dice);
int snd_dice_transaction_reinit(struct snd_dice *dice);
void snd_dice_transaction_destroy(struct snd_dice *dice);

#define SND_DICE_RATES_COUNT	7
extern const unsigned int snd_dice_rates[SND_DICE_RATES_COUNT];

int snd_dice_stream_start_duplex(struct snd_dice *dice, unsigned int rate);
void snd_dice_stream_stop_duplex(struct snd_dice *dice);
int snd_dice_stream_init_duplex(struct snd_dice *dice);
void snd_dice_stream_destroy_duplex(struct snd_dice *dice);
void snd_dice_stream_update_duplex(struct snd_dice *dice);

int snd_dice_stream_lock_try(struct snd_dice *dice);
void snd_dice_stream_lock_release(struct snd_dice *dice);

int snd_dice_create_pcm(struct snd_dice *dice);

int snd_dice_create_hwdep(struct snd_dice *dice);

void snd_dice_create_proc(struct snd_dice *dice);

int snd_dice_create_midi(struct snd_dice *dice);

#endif
