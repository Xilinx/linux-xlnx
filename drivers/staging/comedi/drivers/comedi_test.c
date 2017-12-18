/*
 * comedi/drivers/comedi_test.c
 *
 * Generates fake waveform signals that can be read through
 * the command interface.  It does _not_ read from any board;
 * it just generates deterministic waveforms.
 * Useful for various testing purposes.
 *
 * Copyright (C) 2002 Joachim Wuttke <Joachim.Wuttke@icn.siemens.de>
 * Copyright (C) 2002 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: comedi_test
 * Description: generates fake waveforms
 * Author: Joachim Wuttke <Joachim.Wuttke@icn.siemens.de>, Frank Mori Hess
 *   <fmhess@users.sourceforge.net>, ds
 * Devices:
 * Status: works
 * Updated: Sat, 16 Mar 2002 17:34:48 -0800
 *
 * This driver is mainly for testing purposes, but can also be used to
 * generate sample waveforms on systems that don't have data acquisition
 * hardware.
 *
 * Configuration options:
 *   [0] - Amplitude in microvolts for fake waveforms (default 1 volt)
 *   [1] - Period in microseconds for fake waveforms (default 0.1 sec)
 *
 * Generates a sawtooth wave on channel 0, square wave on channel 1, additional
 * waveforms could be added to other channels (currently they return flatline
 * zero volts).
 */

#include <linux/module.h>
#include "../comedidev.h"

#include <asm/div64.h>

#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/jiffies.h>

#define N_CHANS 8

/* Data unique to this driver */
struct waveform_private {
	struct timer_list ai_timer;	/* timer for AI commands */
	u64 ai_convert_time;		/* time of next AI conversion in usec */
	unsigned int wf_amplitude;	/* waveform amplitude in microvolts */
	unsigned int wf_period;		/* waveform period in microseconds */
	unsigned int wf_current;	/* current time in waveform period */
	unsigned int ai_scan_period;	/* AI scan period in usec */
	unsigned int ai_convert_period;	/* AI conversion period in usec */
	struct timer_list ao_timer;	/* timer for AO commands */
	u64 ao_last_scan_time;		/* time of previous AO scan in usec */
	unsigned int ao_scan_period;	/* AO scan period in usec */
	unsigned short ao_loopbacks[N_CHANS];
};

/* fake analog input ranges */
static const struct comedi_lrange waveform_ai_ranges = {
	2, {
		BIP_RANGE(10),
		BIP_RANGE(5)
	}
};

static unsigned short fake_sawtooth(struct comedi_device *dev,
				    unsigned int range_index,
				    unsigned int current_time)
{
	struct waveform_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int offset = s->maxdata / 2;
	u64 value;
	const struct comedi_krange *krange =
	    &s->range_table->range[range_index];
	u64 binary_amplitude;

	binary_amplitude = s->maxdata;
	binary_amplitude *= devpriv->wf_amplitude;
	do_div(binary_amplitude, krange->max - krange->min);

	value = current_time;
	value *= binary_amplitude * 2;
	do_div(value, devpriv->wf_period);
	value += offset;
	/* get rid of sawtooth's dc offset and clamp value */
	if (value < binary_amplitude) {
		value = 0;			/* negative saturation */
	} else {
		value -= binary_amplitude;
		if (value > s->maxdata)
			value = s->maxdata;	/* positive saturation */
	}

	return value;
}

static unsigned short fake_squarewave(struct comedi_device *dev,
				      unsigned int range_index,
				      unsigned int current_time)
{
	struct waveform_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int offset = s->maxdata / 2;
	u64 value;
	const struct comedi_krange *krange =
	    &s->range_table->range[range_index];

	value = s->maxdata;
	value *= devpriv->wf_amplitude;
	do_div(value, krange->max - krange->min);

	/* get one of two values for square-wave and clamp */
	if (current_time < devpriv->wf_period / 2) {
		if (offset < value)
			value = 0;		/* negative saturation */
		else
			value = offset - value;
	} else {
		value += offset;
		if (value > s->maxdata)
			value = s->maxdata;	/* positive saturation */
	}

	return value;
}

static unsigned short fake_flatline(struct comedi_device *dev,
				    unsigned int range_index,
				    unsigned int current_time)
{
	return dev->read_subdev->maxdata / 2;
}

/* generates a different waveform depending on what channel is read */
static unsigned short fake_waveform(struct comedi_device *dev,
				    unsigned int channel, unsigned int range,
				    unsigned int current_time)
{
	enum {
		SAWTOOTH_CHAN,
		SQUARE_CHAN,
	};
	switch (channel) {
	case SAWTOOTH_CHAN:
		return fake_sawtooth(dev, range, current_time);
	case SQUARE_CHAN:
		return fake_squarewave(dev, range, current_time);
	default:
		break;
	}

	return fake_flatline(dev, range, current_time);
}

/*
 * This is the background routine used to generate arbitrary data.
 * It should run in the background; therefore it is scheduled by
 * a timer mechanism.
 */
static void waveform_ai_timer(unsigned long arg)
{
	struct comedi_device *dev = (struct comedi_device *)arg;
	struct waveform_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	u64 now;
	unsigned int nsamples;
	unsigned int time_increment;

	now = ktime_to_us(ktime_get());
	nsamples = comedi_nsamples_left(s, UINT_MAX);

	while (nsamples && devpriv->ai_convert_time < now) {
		unsigned int chanspec = cmd->chanlist[async->cur_chan];
		unsigned short sample;

		sample = fake_waveform(dev, CR_CHAN(chanspec),
				       CR_RANGE(chanspec), devpriv->wf_current);
		if (comedi_buf_write_samples(s, &sample, 1) == 0)
			goto overrun;
		time_increment = devpriv->ai_convert_period;
		if (async->scan_progress == 0) {
			/* done last conversion in scan, so add dead time */
			time_increment += devpriv->ai_scan_period -
					  devpriv->ai_convert_period *
					  cmd->scan_end_arg;
		}
		devpriv->wf_current += time_increment;
		if (devpriv->wf_current >= devpriv->wf_period)
			devpriv->wf_current %= devpriv->wf_period;
		devpriv->ai_convert_time += time_increment;
		nsamples--;
	}

	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg) {
		async->events |= COMEDI_CB_EOA;
	} else {
		if (devpriv->ai_convert_time > now)
			time_increment = devpriv->ai_convert_time - now;
		else
			time_increment = 1;
		mod_timer(&devpriv->ai_timer,
			  jiffies + usecs_to_jiffies(time_increment));
	}

overrun:
	comedi_handle_events(dev, s);
}

static int waveform_ai_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg, limit;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_FOLLOW | TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_NOW | TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (cmd->scan_begin_src == TRIG_FOLLOW && cmd->convert_src == TRIG_NOW)
		err |= -EINVAL;		/* scan period would be 0 */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->convert_src == TRIG_NOW) {
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	} else {	/* cmd->convert_src == TRIG_TIMER */
		if (cmd->scan_begin_src == TRIG_FOLLOW) {
			err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
							    NSEC_PER_USEC);
		}
	}

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	} else {	/* cmd->scan_begin_src == TRIG_TIMER */
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    NSEC_PER_USEC);
	}

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* cmd->stop_src == TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		/* round convert_arg to nearest microsecond */
		arg = cmd->convert_arg;
		arg = min(arg,
			  rounddown(UINT_MAX, (unsigned int)NSEC_PER_USEC));
		arg = NSEC_PER_USEC * DIV_ROUND_CLOSEST(arg, NSEC_PER_USEC);
		if (cmd->scan_begin_arg == TRIG_TIMER) {
			/* limit convert_arg to keep scan_begin_arg in range */
			limit = UINT_MAX / cmd->scan_end_arg;
			limit = rounddown(limit, (unsigned int)NSEC_PER_SEC);
			arg = min(arg, limit);
		}
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/* round scan_begin_arg to nearest microsecond */
		arg = cmd->scan_begin_arg;
		arg = min(arg,
			  rounddown(UINT_MAX, (unsigned int)NSEC_PER_USEC));
		arg = NSEC_PER_USEC * DIV_ROUND_CLOSEST(arg, NSEC_PER_USEC);
		if (cmd->convert_src == TRIG_TIMER) {
			/* but ensure scan_begin_arg is large enough */
			arg = max(arg, cmd->convert_arg * cmd->scan_end_arg);
		}
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	return 0;
}

static int waveform_ai_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct waveform_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int first_convert_time;
	u64 wf_current;

	if (cmd->flags & CMDF_PRIORITY) {
		dev_err(dev->class_dev,
			"commands at RT priority not supported in this driver\n");
		return -1;
	}

	if (cmd->convert_src == TRIG_NOW)
		devpriv->ai_convert_period = 0;
	else		/* cmd->convert_src == TRIG_TIMER */
		devpriv->ai_convert_period = cmd->convert_arg / NSEC_PER_USEC;

	if (cmd->scan_begin_src == TRIG_FOLLOW) {
		devpriv->ai_scan_period = devpriv->ai_convert_period *
					  cmd->scan_end_arg;
	} else {	/* cmd->scan_begin_src == TRIG_TIMER */
		devpriv->ai_scan_period = cmd->scan_begin_arg / NSEC_PER_USEC;
	}

	/*
	 * Simulate first conversion to occur at convert period after
	 * conversion timer starts.  If scan_begin_src is TRIG_FOLLOW, assume
	 * the conversion timer starts immediately.  If scan_begin_src is
	 * TRIG_TIMER, assume the conversion timer starts after the scan
	 * period.
	 */
	first_convert_time = devpriv->ai_convert_period;
	if (cmd->scan_begin_src == TRIG_TIMER)
		first_convert_time += devpriv->ai_scan_period;
	devpriv->ai_convert_time = ktime_to_us(ktime_get()) +
				   first_convert_time;

	/* Determine time within waveform period at time of conversion. */
	wf_current = devpriv->ai_convert_time;
	devpriv->wf_current = do_div(wf_current, devpriv->wf_period);

	/*
	 * Schedule timer to expire just after first conversion time.
	 * Seem to need an extra jiffy here, otherwise timer expires slightly
	 * early!
	 */
	devpriv->ai_timer.expires =
		jiffies + usecs_to_jiffies(devpriv->ai_convert_period) + 1;
	add_timer(&devpriv->ai_timer);
	return 0;
}

static int waveform_ai_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct waveform_private *devpriv = dev->private;

	if (in_softirq()) {
		/* Assume we were called from the timer routine itself. */
		del_timer(&devpriv->ai_timer);
	} else {
		del_timer_sync(&devpriv->ai_timer);
	}
	return 0;
}

static int waveform_ai_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct waveform_private *devpriv = dev->private;
	int i, chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_loopbacks[chan];

	return insn->n;
}

/*
 * This is the background routine to handle AO commands, scheduled by
 * a timer mechanism.
 */
static void waveform_ao_timer(unsigned long arg)
{
	struct comedi_device *dev = (struct comedi_device *)arg;
	struct waveform_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->write_subdev;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	u64 now;
	u64 scans_since;
	unsigned int scans_avail = 0;

	/* determine number of scan periods since last time */
	now = ktime_to_us(ktime_get());
	scans_since = now - devpriv->ao_last_scan_time;
	do_div(scans_since, devpriv->ao_scan_period);
	if (scans_since) {
		unsigned int i;

		/* determine scans in buffer, limit to scans to do this time */
		scans_avail = comedi_nscans_left(s, 0);
		if (scans_avail > scans_since)
			scans_avail = scans_since;
		if (scans_avail) {
			/* skip all but the last scan to save processing time */
			if (scans_avail > 1) {
				unsigned int skip_bytes, nbytes;

				skip_bytes =
				comedi_samples_to_bytes(s, cmd->scan_end_arg *
							   (scans_avail - 1));
				nbytes = comedi_buf_read_alloc(s, skip_bytes);
				comedi_buf_read_free(s, nbytes);
				comedi_inc_scan_progress(s, nbytes);
				if (nbytes < skip_bytes) {
					/* unexpected underrun! (cancelled?) */
					async->events |= COMEDI_CB_OVERFLOW;
					goto underrun;
				}
			}
			/* output the last scan */
			for (i = 0; i < cmd->scan_end_arg; i++) {
				unsigned int chan = CR_CHAN(cmd->chanlist[i]);

				if (comedi_buf_read_samples(s,
							    &devpriv->
							     ao_loopbacks[chan],
							    1) == 0) {
					/* unexpected underrun! (cancelled?) */
					async->events |= COMEDI_CB_OVERFLOW;
					goto underrun;
				}
			}
			/* advance time of last scan */
			devpriv->ao_last_scan_time +=
				(u64)scans_avail * devpriv->ao_scan_period;
		}
	}
	if (cmd->stop_src == TRIG_COUNT && async->scans_done >= cmd->stop_arg) {
		async->events |= COMEDI_CB_EOA;
	} else if (scans_avail < scans_since) {
		async->events |= COMEDI_CB_OVERFLOW;
	} else {
		unsigned int time_inc = devpriv->ao_last_scan_time +
					devpriv->ao_scan_period - now;

		mod_timer(&devpriv->ao_timer,
			  jiffies + usecs_to_jiffies(time_inc));
	}

underrun:
	comedi_handle_events(dev, s);
}

static int waveform_ao_inttrig_start(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int trig_num)
{
	struct waveform_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	async->inttrig = NULL;

	devpriv->ao_last_scan_time = ktime_to_us(ktime_get());
	devpriv->ao_timer.expires =
		jiffies + usecs_to_jiffies(devpriv->ao_scan_period);
	add_timer(&devpriv->ao_timer);

	return 1;
}

static int waveform_ao_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_INT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
					    NSEC_PER_USEC);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* cmd->stop_src == TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* round scan_begin_arg to nearest microsecond */
	arg = cmd->scan_begin_arg;
	arg = min(arg, rounddown(UINT_MAX, (unsigned int)NSEC_PER_USEC));
	arg = NSEC_PER_USEC * DIV_ROUND_CLOSEST(arg, NSEC_PER_USEC);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);

	if (err)
		return 4;

	return 0;
}

static int waveform_ao_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct waveform_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (cmd->flags & CMDF_PRIORITY) {
		dev_err(dev->class_dev,
			"commands at RT priority not supported in this driver\n");
		return -1;
	}

	devpriv->ao_scan_period = cmd->scan_begin_arg / NSEC_PER_USEC;
	s->async->inttrig = waveform_ao_inttrig_start;
	return 0;
}

static int waveform_ao_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct waveform_private *devpriv = dev->private;

	s->async->inttrig = NULL;
	if (in_softirq()) {
		/* Assume we were called from the timer routine itself. */
		del_timer(&devpriv->ao_timer);
	} else {
		del_timer_sync(&devpriv->ao_timer);
	}
	return 0;
}

static int waveform_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct waveform_private *devpriv = dev->private;
	int i, chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		devpriv->ao_loopbacks[chan] = data[i];

	return insn->n;
}

static int waveform_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct waveform_private *devpriv;
	struct comedi_subdevice *s;
	int amplitude = it->options[0];
	int period = it->options[1];
	int i;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* set default amplitude and period */
	if (amplitude <= 0)
		amplitude = 1000000;	/* 1 volt */
	if (period <= 0)
		period = 100000;	/* 0.1 sec */

	devpriv->wf_amplitude = amplitude;
	devpriv->wf_period = period;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_CMD_READ;
	s->n_chan = N_CHANS;
	s->maxdata = 0xffff;
	s->range_table = &waveform_ai_ranges;
	s->len_chanlist = s->n_chan * 2;
	s->insn_read = waveform_ai_insn_read;
	s->do_cmd = waveform_ai_cmd;
	s->do_cmdtest = waveform_ai_cmdtest;
	s->cancel = waveform_ai_cancel;

	s = &dev->subdevices[1];
	dev->write_subdev = s;
	/* analog output subdevice (loopback) */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	s->n_chan = N_CHANS;
	s->maxdata = 0xffff;
	s->range_table = &waveform_ai_ranges;
	s->len_chanlist = s->n_chan;
	s->insn_write = waveform_ao_insn_write;
	s->insn_read = waveform_ai_insn_read;	/* do same as AI insn_read */
	s->do_cmd = waveform_ao_cmd;
	s->do_cmdtest = waveform_ao_cmdtest;
	s->cancel = waveform_ao_cancel;

	/* Our default loopback value is just a 0V flatline */
	for (i = 0; i < s->n_chan; i++)
		devpriv->ao_loopbacks[i] = s->maxdata / 2;

	setup_timer(&devpriv->ai_timer, waveform_ai_timer, (unsigned long)dev);
	setup_timer(&devpriv->ao_timer, waveform_ao_timer, (unsigned long)dev);

	dev_info(dev->class_dev,
		 "%s: %u microvolt, %u microsecond waveform attached\n",
		 dev->board_name,
		 devpriv->wf_amplitude, devpriv->wf_period);

	return 0;
}

static void waveform_detach(struct comedi_device *dev)
{
	struct waveform_private *devpriv = dev->private;

	if (devpriv) {
		del_timer_sync(&devpriv->ai_timer);
		del_timer_sync(&devpriv->ao_timer);
	}
}

static struct comedi_driver waveform_driver = {
	.driver_name	= "comedi_test",
	.module		= THIS_MODULE,
	.attach		= waveform_attach,
	.detach		= waveform_detach,
};
module_comedi_driver(waveform_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
