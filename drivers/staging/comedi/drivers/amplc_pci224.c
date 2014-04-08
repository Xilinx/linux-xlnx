/*
    comedi/drivers/amplc_pci224.c
    Driver for Amplicon PCI224 and PCI234 AO boards.

    Copyright (C) 2005 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998,2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: amplc_pci224
Description: Amplicon PCI224, PCI234
Author: Ian Abbott <abbotti@mev.co.uk>
Devices: [Amplicon] PCI224 (amplc_pci224 or pci224),
  PCI234 (amplc_pci224 or pci234)
Updated: Wed, 22 Oct 2008 12:25:08 +0100
Status: works, but see caveats

Supports:

  - ao_insn read/write
  - ao_do_cmd mode with the following sources:

    - start_src         TRIG_INT        TRIG_EXT
    - scan_begin_src    TRIG_TIMER      TRIG_EXT
    - convert_src       TRIG_NOW
    - scan_end_src      TRIG_COUNT
    - stop_src          TRIG_COUNT      TRIG_EXT        TRIG_NONE

    The channel list must contain at least one channel with no repeated
    channels.  The scan end count must equal the number of channels in
    the channel list.

    There is only one external trigger source so only one of start_src,
    scan_begin_src or stop_src may use TRIG_EXT.

Configuration options - PCI224:
  [0] - PCI bus of device (optional).
  [1] - PCI slot of device (optional).
          If bus/slot is not specified, the first available PCI device
          will be used.
  [2] - Select available ranges according to jumper LK1.  All channels
        are set to the same range:
        0=Jumper position 1-2 (factory default), 4 software-selectable
          internal voltage references, giving 4 bipolar and 4 unipolar
          ranges:
            [-10V,+10V], [-5V,+5V], [-2.5V,+2.5V], [-1.25V,+1.25V],
            [0,+10V], [0,+5V], [0,+2.5V], [0,1.25V].
        1=Jumper position 2-3, 1 external voltage reference, giving
          1 bipolar and 1 unipolar range:
            [-Vext,+Vext], [0,+Vext].

Configuration options - PCI234:
  [0] - PCI bus of device (optional).
  [1] - PCI slot of device (optional).
          If bus/slot is not specified, the first available PCI device
          will be used.
  [2] - Select internal or external voltage reference according to
        jumper LK1.  This affects all channels:
        0=Jumper position 1-2 (factory default), Vref=5V internal.
        1=Jumper position 2-3, Vref=Vext external.
  [3] - Select channel 0 range according to jumper LK2:
        0=Jumper position 2-3 (factory default), range [-2*Vref,+2*Vref]
          (10V bipolar when options[2]=0).
        1=Jumper position 1-2, range [-Vref,+Vref]
          (5V bipolar when options[2]=0).
  [4] - Select channel 1 range according to jumper LK3: cf. options[3].
  [5] - Select channel 2 range according to jumper LK4: cf. options[3].
  [6] - Select channel 3 range according to jumper LK5: cf. options[3].

Passing a zero for an option is the same as leaving it unspecified.

Caveats:

  1) All channels on the PCI224 share the same range.  Any change to the
     range as a result of insn_write or a streaming command will affect
     the output voltages of all channels, including those not specified
     by the instruction or command.

  2) For the analog output command,  the first scan may be triggered
     falsely at the start of acquisition.  This occurs when the DAC scan
     trigger source is switched from 'none' to 'timer' (scan_begin_src =
     TRIG_TIMER) or 'external' (scan_begin_src == TRIG_EXT) at the start
     of acquisition and the trigger source is at logic level 1 at the
     time of the switch.  This is very likely for TRIG_TIMER.  For
     TRIG_EXT, it depends on the state of the external line and whether
     the CR_INVERT flag has been set.  The remaining scans are triggered
     correctly.
*/

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "8253.h"

#define DRIVER_NAME	"amplc_pci224"

/*
 * PCI IDs.
 */
#define PCI_DEVICE_ID_AMPLICON_PCI224 0x0007
#define PCI_DEVICE_ID_AMPLICON_PCI234 0x0008
#define PCI_DEVICE_ID_INVALID 0xffff

/*
 * PCI224/234 i/o space 1 (PCIBAR2) registers.
 */
#define PCI224_IO1_SIZE	0x20	/* Size of i/o space 1 (8-bit registers) */
#define PCI224_Z2_CT0	0x14	/* 82C54 counter/timer 0 */
#define PCI224_Z2_CT1	0x15	/* 82C54 counter/timer 1 */
#define PCI224_Z2_CT2	0x16	/* 82C54 counter/timer 2 */
#define PCI224_Z2_CTC	0x17	/* 82C54 counter/timer control word */
#define PCI224_ZCLK_SCE	0x1A	/* Group Z Clock Configuration Register */
#define PCI224_ZGAT_SCE	0x1D	/* Group Z Gate Configuration Register */
#define PCI224_INT_SCE	0x1E	/* ISR Interrupt source mask register */
				/* /Interrupt status */

/*
 * PCI224/234 i/o space 2 (PCIBAR3) 16-bit registers.
 */
#define PCI224_IO2_SIZE	0x10	/* Size of i/o space 2 (16-bit registers). */
#define PCI224_DACDATA	0x00	/* (w-o) DAC FIFO data. */
#define PCI224_SOFTTRIG	0x00	/* (r-o) DAC software scan trigger. */
#define PCI224_DACCON	0x02	/* (r/w) DAC status/configuration. */
#define PCI224_FIFOSIZ	0x04	/* (w-o) FIFO size for wraparound mode. */
#define PCI224_DACCEN	0x06	/* (w-o) DAC channel enable register. */

/*
 * DACCON values.
 */
/* (r/w) Scan trigger. */
#define PCI224_DACCON_TRIG_MASK		(7 << 0)
#define PCI224_DACCON_TRIG_NONE		(0 << 0)	/* none */
#define PCI224_DACCON_TRIG_SW		(1 << 0)	/* software trig */
#define PCI224_DACCON_TRIG_EXTP		(2 << 0)	/* ext +ve edge */
#define PCI224_DACCON_TRIG_EXTN		(3 << 0)	/* ext -ve edge */
#define PCI224_DACCON_TRIG_Z2CT0	(4 << 0)	/* Z2 CT0 out */
#define PCI224_DACCON_TRIG_Z2CT1	(5 << 0)	/* Z2 CT1 out */
#define PCI224_DACCON_TRIG_Z2CT2	(6 << 0)	/* Z2 CT2 out */
/* (r/w) Polarity (PCI224 only, PCI234 always bipolar!). */
#define PCI224_DACCON_POLAR_MASK	(1 << 3)
#define PCI224_DACCON_POLAR_UNI		(0 << 3)	/* range [0,Vref] */
#define PCI224_DACCON_POLAR_BI		(1 << 3)	/* range [-Vref,Vref] */
/* (r/w) Internal Vref (PCI224 only, when LK1 in position 1-2). */
#define PCI224_DACCON_VREF_MASK		(3 << 4)
#define PCI224_DACCON_VREF_1_25		(0 << 4)	/* Vref = 1.25V */
#define PCI224_DACCON_VREF_2_5		(1 << 4)	/* Vref = 2.5V */
#define PCI224_DACCON_VREF_5		(2 << 4)	/* Vref = 5V */
#define PCI224_DACCON_VREF_10		(3 << 4)	/* Vref = 10V */
/* (r/w) Wraparound mode enable (to play back stored waveform). */
#define PCI224_DACCON_FIFOWRAP		(1 << 7)
/* (r/w) FIFO enable.  It MUST be set! */
#define PCI224_DACCON_FIFOENAB		(1 << 8)
/* (r/w) FIFO interrupt trigger level (most values are not very useful). */
#define PCI224_DACCON_FIFOINTR_MASK	(7 << 9)
#define PCI224_DACCON_FIFOINTR_EMPTY	(0 << 9)	/* when empty */
#define PCI224_DACCON_FIFOINTR_NEMPTY	(1 << 9)	/* when not empty */
#define PCI224_DACCON_FIFOINTR_NHALF	(2 << 9)	/* when not half full */
#define PCI224_DACCON_FIFOINTR_HALF	(3 << 9)	/* when half full */
#define PCI224_DACCON_FIFOINTR_NFULL	(4 << 9)	/* when not full */
#define PCI224_DACCON_FIFOINTR_FULL	(5 << 9)	/* when full */
/* (r-o) FIFO fill level. */
#define PCI224_DACCON_FIFOFL_MASK	(7 << 12)
#define PCI224_DACCON_FIFOFL_EMPTY	(1 << 12)	/* 0 */
#define PCI224_DACCON_FIFOFL_ONETOHALF	(0 << 12)	/* [1,2048] */
#define PCI224_DACCON_FIFOFL_HALFTOFULL	(4 << 12)	/* [2049,4095] */
#define PCI224_DACCON_FIFOFL_FULL	(6 << 12)	/* 4096 */
/* (r-o) DAC busy flag. */
#define PCI224_DACCON_BUSY		(1 << 15)
/* (w-o) FIFO reset. */
#define PCI224_DACCON_FIFORESET		(1 << 12)
/* (w-o) Global reset (not sure what it does). */
#define PCI224_DACCON_GLOBALRESET	(1 << 13)

/*
 * DAC FIFO size.
 */
#define PCI224_FIFO_SIZE	4096

/*
 * DAC FIFO guaranteed minimum room available, depending on reported fill level.
 * The maximum room available depends on the reported fill level and how much
 * has been written!
 */
#define PCI224_FIFO_ROOM_EMPTY		PCI224_FIFO_SIZE
#define PCI224_FIFO_ROOM_ONETOHALF	(PCI224_FIFO_SIZE / 2)
#define PCI224_FIFO_ROOM_HALFTOFULL	1
#define PCI224_FIFO_ROOM_FULL		0

/*
 * Counter/timer clock input configuration sources.
 */
#define CLK_CLK		0	/* reserved (channel-specific clock) */
#define CLK_10MHZ	1	/* internal 10 MHz clock */
#define CLK_1MHZ	2	/* internal 1 MHz clock */
#define CLK_100KHZ	3	/* internal 100 kHz clock */
#define CLK_10KHZ	4	/* internal 10 kHz clock */
#define CLK_1KHZ	5	/* internal 1 kHz clock */
#define CLK_OUTNM1	6	/* output of channel-1 modulo total */
#define CLK_EXT		7	/* external clock */
/* Macro to construct clock input configuration register value. */
#define CLK_CONFIG(chan, src)	((((chan) & 3) << 3) | ((src) & 7))

/*
 * Counter/timer gate input configuration sources.
 */
#define GAT_VCC		0	/* VCC (i.e. enabled) */
#define GAT_GND		1	/* GND (i.e. disabled) */
#define GAT_EXT		2	/* reserved (external gate input) */
#define GAT_NOUTNM2	3	/* inverted output of channel-2 modulo total */
/* Macro to construct gate input configuration register value. */
#define GAT_CONFIG(chan, src)	((((chan) & 3) << 3) | ((src) & 7))

/*
 * Summary of CLK_OUTNM1 and GAT_NOUTNM2 connections for PCI224 and PCI234:
 *
 *              Channel's       Channel's
 *              clock input     gate input
 * Channel      CLK_OUTNM1      GAT_NOUTNM2
 * -------      ----------      -----------
 * Z2-CT0       Z2-CT2-OUT      /Z2-CT1-OUT
 * Z2-CT1       Z2-CT0-OUT      /Z2-CT2-OUT
 * Z2-CT2       Z2-CT1-OUT      /Z2-CT0-OUT
 */

/*
 * Interrupt enable/status bits
 */
#define PCI224_INTR_EXT		0x01	/* rising edge on external input */
#define PCI224_INTR_DAC		0x04	/* DAC (FIFO) interrupt */
#define PCI224_INTR_Z2CT1	0x20	/* rising edge on Z2-CT1 output */

#define PCI224_INTR_EDGE_BITS	(PCI224_INTR_EXT | PCI224_INTR_Z2CT1)
#define PCI224_INTR_LEVEL_BITS	PCI224_INTR_DACFIFO

/*
 * Handy macros.
 */

/* Combine old and new bits. */
#define COMBINE(old, new, mask)	(((old) & ~(mask)) | ((new) & (mask)))

/* Current CPU.  XXX should this be hard_smp_processor_id()? */
#define THISCPU		smp_processor_id()

/* State bits for use with atomic bit operations. */
#define AO_CMD_STARTED	0

/*
 * Range tables.
 */

/* The software selectable internal ranges for PCI224 (option[2] == 0). */
static const struct comedi_lrange range_pci224_internal = {
	8,
	{
	 BIP_RANGE(10),
	 BIP_RANGE(5),
	 BIP_RANGE(2.5),
	 BIP_RANGE(1.25),
	 UNI_RANGE(10),
	 UNI_RANGE(5),
	 UNI_RANGE(2.5),
	 UNI_RANGE(1.25),
	 }
};

static const unsigned short hwrange_pci224_internal[8] = {
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_10,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_5,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_2_5,
	PCI224_DACCON_POLAR_BI | PCI224_DACCON_VREF_1_25,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_10,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_5,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_2_5,
	PCI224_DACCON_POLAR_UNI | PCI224_DACCON_VREF_1_25,
};

/* The software selectable external ranges for PCI224 (option[2] == 1). */
static const struct comedi_lrange range_pci224_external = {
	2,
	{
	 RANGE_ext(-1, 1),	/* bipolar [-Vref,+Vref] */
	 RANGE_ext(0, 1),	/* unipolar [0,+Vref] */
	 }
};

static const unsigned short hwrange_pci224_external[2] = {
	PCI224_DACCON_POLAR_BI,
	PCI224_DACCON_POLAR_UNI,
};

/* The hardware selectable Vref*2 external range for PCI234
 * (option[2] == 1, option[3+n] == 0). */
static const struct comedi_lrange range_pci234_ext2 = {
	1,
	{
	 RANGE_ext(-2, 2),
	 }
};

/* The hardware selectable Vref external range for PCI234
 * (option[2] == 1, option[3+n] == 1). */
static const struct comedi_lrange range_pci234_ext = {
	1,
	{
	 RANGE_ext(-1, 1),
	 }
};

/* This serves for all the PCI234 ranges. */
static const unsigned short hwrange_pci234[1] = {
	PCI224_DACCON_POLAR_BI,	/* bipolar - hardware ignores it! */
};

/*
 * Board descriptions.
 */

enum pci224_model { any_model, pci224_model, pci234_model };

struct pci224_board {
	const char *name;
	unsigned short devid;
	enum pci224_model model;
	unsigned int ao_chans;
	unsigned int ao_bits;
};

static const struct pci224_board pci224_boards[] = {
	{
	 .name = "pci224",
	 .devid = PCI_DEVICE_ID_AMPLICON_PCI224,
	 .model = pci224_model,
	 .ao_chans = 16,
	 .ao_bits = 12,
	 },
	{
	 .name = "pci234",
	 .devid = PCI_DEVICE_ID_AMPLICON_PCI234,
	 .model = pci234_model,
	 .ao_chans = 4,
	 .ao_bits = 16,
	 },
	{
	 .name = DRIVER_NAME,
	 .devid = PCI_DEVICE_ID_INVALID,
	 .model = any_model,	/* wildcard */
	 },
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct pci224_private {
	const unsigned short *hwrange;
	unsigned long iobase1;
	unsigned long state;
	spinlock_t ao_spinlock;
	unsigned int *ao_readback;
	unsigned short *ao_scan_vals;
	unsigned char *ao_scan_order;
	int intr_cpuid;
	short intr_running;
	unsigned short daccon;
	unsigned int cached_div1;
	unsigned int cached_div2;
	unsigned int ao_stop_count;
	short ao_stop_continuous;
	unsigned short ao_enab;	/* max 16 channels so 'short' will do */
	unsigned char intsce;
};

/*
 * Called from the 'insn_write' function to perform a single write.
 */
static void
pci224_ao_set_data(struct comedi_device *dev, int chan, int range,
		   unsigned int data)
{
	const struct pci224_board *thisboard = comedi_board(dev);
	struct pci224_private *devpriv = dev->private;
	unsigned short mangled;

	/* Store unmangled data for readback. */
	devpriv->ao_readback[chan] = data;
	/* Enable the channel. */
	outw(1 << chan, dev->iobase + PCI224_DACCEN);
	/* Set range and reset FIFO. */
	devpriv->daccon = COMBINE(devpriv->daccon, devpriv->hwrange[range],
				  (PCI224_DACCON_POLAR_MASK |
				   PCI224_DACCON_VREF_MASK));
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);
	/*
	 * Mangle the data.  The hardware expects:
	 * - bipolar: 16-bit 2's complement
	 * - unipolar: 16-bit unsigned
	 */
	mangled = (unsigned short)data << (16 - thisboard->ao_bits);
	if ((devpriv->daccon & PCI224_DACCON_POLAR_MASK) ==
	    PCI224_DACCON_POLAR_BI) {
		mangled ^= 0x8000;
	}
	/* Write mangled data to the FIFO. */
	outw(mangled, dev->iobase + PCI224_DACDATA);
	/* Trigger the conversion. */
	inw(dev->iobase + PCI224_SOFTTRIG);
}

/*
 * 'insn_write' function for AO subdevice.
 */
static int
pci224_ao_insn_write(struct comedi_device *dev, struct comedi_subdevice *s,
		     struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan, range;

	/* Unpack channel and range. */
	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++)
		pci224_ao_set_data(dev, chan, range, data[i]);

	return i;
}

/*
 * 'insn_read' function for AO subdevice.
 *
 * N.B. The value read will not be valid if the DAC channel has
 * never been written successfully since the device was attached
 * or since the channel has been used by an AO streaming write
 * command.
 */
static int
pci224_ao_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	struct pci224_private *devpriv = dev->private;
	int i;
	int chan;

	chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];


	return i;
}

/*
 * Just a wrapper for the inline function 'i8253_cascade_ns_to_timer'.
 */
static void
pci224_cascade_ns_to_timer(int osc_base, unsigned int *d1, unsigned int *d2,
			   unsigned int *nanosec, int round_mode)
{
	i8253_cascade_ns_to_timer(osc_base, d1, d2, nanosec, round_mode);
}

/*
 * Kills a command running on the AO subdevice.
 */
static void pci224_ao_stop(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	unsigned long flags;

	if (!test_and_clear_bit(AO_CMD_STARTED, &devpriv->state))
		return;


	spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	/* Kill the interrupts. */
	devpriv->intsce = 0;
	outb(0, devpriv->iobase1 + PCI224_INT_SCE);
	/*
	 * Interrupt routine may or may not be running.  We may or may not
	 * have been called from the interrupt routine (directly or
	 * indirectly via a comedi_events() callback routine).  It's highly
	 * unlikely that we've been called from some other interrupt routine
	 * but who knows what strange things coders get up to!
	 *
	 * If the interrupt routine is currently running, wait for it to
	 * finish, unless we appear to have been called via the interrupt
	 * routine.
	 */
	while (devpriv->intr_running && devpriv->intr_cpuid != THISCPU) {
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
	}
	spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
	/* Reconfigure DAC for insn_write usage. */
	outw(0, dev->iobase + PCI224_DACCEN);	/* Disable channels. */
	devpriv->daccon = COMBINE(devpriv->daccon,
				  PCI224_DACCON_TRIG_SW |
				  PCI224_DACCON_FIFOINTR_EMPTY,
				  PCI224_DACCON_TRIG_MASK |
				  PCI224_DACCON_FIFOINTR_MASK);
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);
}

/*
 * Handles start of acquisition for the AO subdevice.
 */
static void pci224_ao_start(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;

	set_bit(AO_CMD_STARTED, &devpriv->state);
	if (!devpriv->ao_stop_continuous && devpriv->ao_stop_count == 0) {
		/* An empty acquisition! */
		pci224_ao_stop(dev, s);
		s->async->events |= COMEDI_CB_EOA;
		comedi_event(dev, s);
	} else {
		/* Enable interrupts. */
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		if (cmd->stop_src == TRIG_EXT)
			devpriv->intsce = PCI224_INTR_EXT | PCI224_INTR_DAC;
		else
			devpriv->intsce = PCI224_INTR_DAC;

		outb(devpriv->intsce, devpriv->iobase1 + PCI224_INT_SCE);
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
	}
}

/*
 * Handles interrupts from the DAC FIFO.
 */
static void pci224_ao_handle_fifo(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int num_scans;
	unsigned int room;
	unsigned short dacstat;
	unsigned int i, n;
	unsigned int bytes_per_scan;

	if (cmd->chanlist_len) {
		bytes_per_scan = cmd->chanlist_len * sizeof(short);
	} else {
		/* Shouldn't get here! */
		bytes_per_scan = sizeof(short);
	}
	/* Determine number of scans available in buffer. */
	num_scans = comedi_buf_read_n_available(s->async) / bytes_per_scan;
	if (!devpriv->ao_stop_continuous) {
		/* Fixed number of scans. */
		if (num_scans > devpriv->ao_stop_count)
			num_scans = devpriv->ao_stop_count;

	}

	/* Determine how much room is in the FIFO (in samples). */
	dacstat = inw(dev->iobase + PCI224_DACCON);
	switch (dacstat & PCI224_DACCON_FIFOFL_MASK) {
	case PCI224_DACCON_FIFOFL_EMPTY:
		room = PCI224_FIFO_ROOM_EMPTY;
		if (!devpriv->ao_stop_continuous && devpriv->ao_stop_count == 0) {
			/* FIFO empty at end of counted acquisition. */
			pci224_ao_stop(dev, s);
			s->async->events |= COMEDI_CB_EOA;
			comedi_event(dev, s);
			return;
		}
		break;
	case PCI224_DACCON_FIFOFL_ONETOHALF:
		room = PCI224_FIFO_ROOM_ONETOHALF;
		break;
	case PCI224_DACCON_FIFOFL_HALFTOFULL:
		room = PCI224_FIFO_ROOM_HALFTOFULL;
		break;
	default:
		room = PCI224_FIFO_ROOM_FULL;
		break;
	}
	if (room >= PCI224_FIFO_ROOM_ONETOHALF) {
		/* FIFO is less than half-full. */
		if (num_scans == 0) {
			/* Nothing left to put in the FIFO. */
			pci224_ao_stop(dev, s);
			s->async->events |= COMEDI_CB_OVERFLOW;
			dev_err(dev->class_dev, "AO buffer underrun\n");
		}
	}
	/* Determine how many new scans can be put in the FIFO. */
	if (cmd->chanlist_len)
		room /= cmd->chanlist_len;

	/* Determine how many scans to process. */
	if (num_scans > room)
		num_scans = room;

	/* Process scans. */
	for (n = 0; n < num_scans; n++) {
		cfc_read_array_from_buffer(s, &devpriv->ao_scan_vals[0],
					   bytes_per_scan);
		for (i = 0; i < cmd->chanlist_len; i++) {
			outw(devpriv->ao_scan_vals[devpriv->ao_scan_order[i]],
			     dev->iobase + PCI224_DACDATA);
		}
	}
	if (!devpriv->ao_stop_continuous) {
		devpriv->ao_stop_count -= num_scans;
		if (devpriv->ao_stop_count == 0) {
			/*
			 * Change FIFO interrupt trigger level to wait
			 * until FIFO is empty.
			 */
			devpriv->daccon = COMBINE(devpriv->daccon,
						  PCI224_DACCON_FIFOINTR_EMPTY,
						  PCI224_DACCON_FIFOINTR_MASK);
			outw(devpriv->daccon, dev->iobase + PCI224_DACCON);
		}
	}
	if ((devpriv->daccon & PCI224_DACCON_TRIG_MASK) ==
	    PCI224_DACCON_TRIG_NONE) {
		unsigned short trig;

		/*
		 * This is the initial DAC FIFO interrupt at the
		 * start of the acquisition.  The DAC's scan trigger
		 * has been set to 'none' up until now.
		 *
		 * Now that data has been written to the FIFO, the
		 * DAC's scan trigger source can be set to the
		 * correct value.
		 *
		 * BUG: The first scan will be triggered immediately
		 * if the scan trigger source is at logic level 1.
		 */
		if (cmd->scan_begin_src == TRIG_TIMER) {
			trig = PCI224_DACCON_TRIG_Z2CT0;
		} else {
			/* cmd->scan_begin_src == TRIG_EXT */
			if (cmd->scan_begin_arg & CR_INVERT)
				trig = PCI224_DACCON_TRIG_EXTN;
			else
				trig = PCI224_DACCON_TRIG_EXTP;

		}
		devpriv->daccon = COMBINE(devpriv->daccon, trig,
					  PCI224_DACCON_TRIG_MASK);
		outw(devpriv->daccon, dev->iobase + PCI224_DACCON);
	}
	if (s->async->events)
		comedi_event(dev, s);

}

/*
 * Internal trigger function to start acquisition on AO subdevice.
 */
static int
pci224_ao_inttrig_start(struct comedi_device *dev, struct comedi_subdevice *s,
			unsigned int trignum)
{
	if (trignum != 0)
		return -EINVAL;

	s->async->inttrig = NULL;
	pci224_ao_start(dev, s);

	return 1;
}

#define MAX_SCAN_PERIOD		0xFFFFFFFFU
#define MIN_SCAN_PERIOD		2500
#define CONVERT_PERIOD		625

/*
 * 'do_cmdtest' function for AO subdevice.
 */
static int
pci224_ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		  struct comedi_cmd *cmd)
{
	struct pci224_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_INT | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_EXT | TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src,
					TRIG_COUNT | TRIG_EXT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/*
	 * There's only one external trigger signal (which makes these
	 * tests easier).  Only one thing can use it.
	 */
	tmp = 0;
	if (cmd->start_src & TRIG_EXT)
		tmp++;
	if (cmd->scan_begin_src & TRIG_EXT)
		tmp++;
	if (cmd->stop_src & TRIG_EXT)
		tmp++;
	if (tmp > 1)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_INT:
		err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if ((cmd->start_arg & ~CR_FLAGS_MASK) != 0) {
			cmd->start_arg = COMBINE(cmd->start_arg, 0,
						 ~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* The only flag allowed is CR_EDGE, which is ignored. */
		if ((cmd->start_arg & CR_FLAGS_MASK & ~CR_EDGE) != 0) {
			cmd->start_arg = COMBINE(cmd->start_arg, 0,
						 CR_FLAGS_MASK & ~CR_EDGE);
			err |= -EINVAL;
		}
		break;
	}

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 MAX_SCAN_PERIOD);

		tmp = cmd->chanlist_len * CONVERT_PERIOD;
		if (tmp < MIN_SCAN_PERIOD)
			tmp = MIN_SCAN_PERIOD;
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg, tmp);
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if ((cmd->scan_begin_arg & ~CR_FLAGS_MASK) != 0) {
			cmd->scan_begin_arg = COMBINE(cmd->scan_begin_arg, 0,
						      ~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* Only allow flags CR_EDGE and CR_INVERT.  Ignore CR_EDGE. */
		if ((cmd->scan_begin_arg & CR_FLAGS_MASK &
		     ~(CR_EDGE | CR_INVERT)) != 0) {
			cmd->scan_begin_arg = COMBINE(cmd->scan_begin_arg, 0,
						      CR_FLAGS_MASK &
						      ~(CR_EDGE | CR_INVERT));
			err |= -EINVAL;
		}
		break;
	}

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* Any count allowed. */
		break;
	case TRIG_EXT:
		/* Force to external trigger 0. */
		if ((cmd->stop_arg & ~CR_FLAGS_MASK) != 0) {
			cmd->stop_arg = COMBINE(cmd->stop_arg, 0,
						~CR_FLAGS_MASK);
			err |= -EINVAL;
		}
		/* The only flag allowed is CR_EDGE, which is ignored. */
		if ((cmd->stop_arg & CR_FLAGS_MASK & ~CR_EDGE) != 0) {
			cmd->stop_arg = COMBINE(cmd->stop_arg, 0,
						CR_FLAGS_MASK & ~CR_EDGE);
		}
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	}

	if (err)
		return 3;

	/* Step 4: fix up any arguments. */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		unsigned int div1, div2, round;
		int round_mode = cmd->flags & TRIG_ROUND_MASK;

		tmp = cmd->scan_begin_arg;
		/* Check whether to use a single timer. */
		switch (round_mode) {
		case TRIG_ROUND_NEAREST:
		default:
			round = I8254_OSC_BASE_10MHZ / 2;
			break;
		case TRIG_ROUND_DOWN:
			round = 0;
			break;
		case TRIG_ROUND_UP:
			round = I8254_OSC_BASE_10MHZ - 1;
			break;
		}
		/* Be careful to avoid overflow! */
		div2 = cmd->scan_begin_arg / I8254_OSC_BASE_10MHZ;
		div2 += (round + cmd->scan_begin_arg % I8254_OSC_BASE_10MHZ) /
			I8254_OSC_BASE_10MHZ;
		if (div2 <= 0x10000) {
			/* A single timer will suffice. */
			if (div2 < 2)
				div2 = 2;
			cmd->scan_begin_arg = div2 * I8254_OSC_BASE_10MHZ;
			if (cmd->scan_begin_arg < div2 ||
			    cmd->scan_begin_arg < I8254_OSC_BASE_10MHZ) {
				/* Overflow! */
				cmd->scan_begin_arg = MAX_SCAN_PERIOD;
			}
		} else {
			/* Use two timers. */
			div1 = devpriv->cached_div1;
			div2 = devpriv->cached_div2;
			pci224_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
						   &div1, &div2,
						   &cmd->scan_begin_arg,
						   round_mode);
			devpriv->cached_div1 = div1;
			devpriv->cached_div2 = div2;
		}
		if (tmp != cmd->scan_begin_arg)
			err++;

	}

	if (err)
		return 4;

	/* Step 5: check channel list. */

	if (cmd->chanlist && (cmd->chanlist_len > 0)) {
		unsigned int range;
		enum { range_err = 1, dupchan_err = 2, };
		unsigned errors;
		unsigned int n;
		unsigned int ch;

		/*
		 * Check all channels have the same range index.  Don't care
		 * about analogue reference, as we can't configure it.
		 *
		 * Check the list has no duplicate channels.
		 */
		range = CR_RANGE(cmd->chanlist[0]);
		errors = 0;
		tmp = 0;
		for (n = 0; n < cmd->chanlist_len; n++) {
			ch = CR_CHAN(cmd->chanlist[n]);
			if (tmp & (1U << ch))
				errors |= dupchan_err;

			tmp |= (1U << ch);
			if (CR_RANGE(cmd->chanlist[n]) != range)
				errors |= range_err;

		}
		if (errors) {
			if (errors & dupchan_err) {
				DPRINTK("comedi%d: " DRIVER_NAME
					": ao_cmdtest: "
					"entries in chanlist must contain no "
					"duplicate channels\n", dev->minor);
			}
			if (errors & range_err) {
				DPRINTK("comedi%d: " DRIVER_NAME
					": ao_cmdtest: "
					"entries in chanlist must all have "
					"the same range index\n", dev->minor);
			}
			err++;
		}
	}

	if (err)
		return 5;

	return 0;
}

/*
 * 'do_cmd' function for AO subdevice.
 */
static int pci224_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pci224_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int range;
	unsigned int i, j;
	unsigned int ch;
	unsigned int rank;
	unsigned long flags;

	/* Cannot handle null/empty chanlist. */
	if (cmd->chanlist == NULL || cmd->chanlist_len == 0)
		return -EINVAL;


	/* Determine which channels are enabled and their load order.  */
	devpriv->ao_enab = 0;

	for (i = 0; i < cmd->chanlist_len; i++) {
		ch = CR_CHAN(cmd->chanlist[i]);
		devpriv->ao_enab |= 1U << ch;
		rank = 0;
		for (j = 0; j < cmd->chanlist_len; j++) {
			if (CR_CHAN(cmd->chanlist[j]) < ch)
				rank++;

		}
		devpriv->ao_scan_order[rank] = i;
	}

	/* Set enabled channels. */
	outw(devpriv->ao_enab, dev->iobase + PCI224_DACCEN);

	/* Determine range and polarity.  All channels the same.  */
	range = CR_RANGE(cmd->chanlist[0]);

	/*
	 * Set DAC range and polarity.
	 * Set DAC scan trigger source to 'none'.
	 * Set DAC FIFO interrupt trigger level to 'not half full'.
	 * Reset DAC FIFO.
	 *
	 * N.B. DAC FIFO interrupts are currently disabled.
	 */
	devpriv->daccon = COMBINE(devpriv->daccon,
				  (devpriv->
				   hwrange[range] | PCI224_DACCON_TRIG_NONE |
				   PCI224_DACCON_FIFOINTR_NHALF),
				  (PCI224_DACCON_POLAR_MASK |
				   PCI224_DACCON_VREF_MASK |
				   PCI224_DACCON_TRIG_MASK |
				   PCI224_DACCON_FIFOINTR_MASK));
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		unsigned int div1, div2, round;
		unsigned int ns = cmd->scan_begin_arg;
		int round_mode = cmd->flags & TRIG_ROUND_MASK;

		/* Check whether to use a single timer. */
		switch (round_mode) {
		case TRIG_ROUND_NEAREST:
		default:
			round = I8254_OSC_BASE_10MHZ / 2;
			break;
		case TRIG_ROUND_DOWN:
			round = 0;
			break;
		case TRIG_ROUND_UP:
			round = I8254_OSC_BASE_10MHZ - 1;
			break;
		}
		/* Be careful to avoid overflow! */
		div2 = cmd->scan_begin_arg / I8254_OSC_BASE_10MHZ;
		div2 += (round + cmd->scan_begin_arg % I8254_OSC_BASE_10MHZ) /
			I8254_OSC_BASE_10MHZ;
		if (div2 <= 0x10000) {
			/* A single timer will suffice. */
			if (div2 < 2)
				div2 = 2;
			div2 &= 0xffff;
			div1 = 1;	/* Flag that single timer to be used. */
		} else {
			/* Use two timers. */
			div1 = devpriv->cached_div1;
			div2 = devpriv->cached_div2;
			pci224_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
						   &div1, &div2,
						   &ns, round_mode);
		}

		/*
		 * The output of timer Z2-0 will be used as the scan trigger
		 * source.
		 */
		/* Make sure Z2-0 is gated on.  */
		outb(GAT_CONFIG(0, GAT_VCC),
		     devpriv->iobase1 + PCI224_ZGAT_SCE);
		if (div1 == 1) {
			/* Not cascading.  Z2-0 needs 10 MHz clock. */
			outb(CLK_CONFIG(0, CLK_10MHZ),
			     devpriv->iobase1 + PCI224_ZCLK_SCE);
		} else {
			/* Cascading with Z2-2. */
			/* Make sure Z2-2 is gated on.  */
			outb(GAT_CONFIG(2, GAT_VCC),
			     devpriv->iobase1 + PCI224_ZGAT_SCE);
			/* Z2-2 needs 10 MHz clock. */
			outb(CLK_CONFIG(2, CLK_10MHZ),
			     devpriv->iobase1 + PCI224_ZCLK_SCE);
			/* Load Z2-2 mode (2) and counter (div1). */
			i8254_load(devpriv->iobase1 + PCI224_Z2_CT0, 0,
				   2, div1, 2);
			/* Z2-0 is clocked from Z2-2's output. */
			outb(CLK_CONFIG(0, CLK_OUTNM1),
			     devpriv->iobase1 + PCI224_ZCLK_SCE);
		}
		/* Load Z2-0 mode (2) and counter (div2). */
		i8254_load(devpriv->iobase1 + PCI224_Z2_CT0, 0, 0, div2, 2);
	}

	/*
	 * Sort out end of acquisition.
	 */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* Fixed number of scans.  */
		devpriv->ao_stop_continuous = 0;
		devpriv->ao_stop_count = cmd->stop_arg;
		break;
	default:
		/* Continuous scans. */
		devpriv->ao_stop_continuous = 1;
		devpriv->ao_stop_count = 0;
		break;
	}

	/*
	 * Sort out start of acquisition.
	 */
	switch (cmd->start_src) {
	case TRIG_INT:
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		s->async->inttrig = &pci224_ao_inttrig_start;
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		break;
	case TRIG_EXT:
		/* Enable external interrupt trigger to start acquisition. */
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		devpriv->intsce |= PCI224_INTR_EXT;
		outb(devpriv->intsce, devpriv->iobase1 + PCI224_INT_SCE);
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		break;
	}

	return 0;
}

/*
 * 'cancel' function for AO subdevice.
 */
static int pci224_ao_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	pci224_ao_stop(dev, s);
	return 0;
}

/*
 * 'munge' data for AO command.
 */
static void
pci224_ao_munge(struct comedi_device *dev, struct comedi_subdevice *s,
		void *data, unsigned int num_bytes, unsigned int chan_index)
{
	const struct pci224_board *thisboard = comedi_board(dev);
	struct pci224_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	unsigned short *array = data;
	unsigned int length = num_bytes / sizeof(*array);
	unsigned int offset;
	unsigned int shift;
	unsigned int i;

	/* The hardware expects 16-bit numbers. */
	shift = 16 - thisboard->ao_bits;
	/* Channels will be all bipolar or all unipolar. */
	if ((devpriv->hwrange[CR_RANGE(async->cmd.chanlist[0])] &
	     PCI224_DACCON_POLAR_MASK) == PCI224_DACCON_POLAR_UNI) {
		/* Unipolar */
		offset = 0;
	} else {
		/* Bipolar */
		offset = 32768;
	}
	/* Munge the data. */
	for (i = 0; i < length; i++)
		array[i] = (array[i] << shift) - offset;

}

/*
 * Interrupt handler.
 */
static irqreturn_t pci224_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pci224_private *devpriv = dev->private;
	struct comedi_subdevice *s = &dev->subdevices[0];
	struct comedi_cmd *cmd;
	unsigned char intstat, valid_intstat;
	unsigned char curenab;
	int retval = 0;
	unsigned long flags;

	intstat = inb(devpriv->iobase1 + PCI224_INT_SCE) & 0x3F;
	if (intstat) {
		retval = 1;
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		valid_intstat = devpriv->intsce & intstat;
		/* Temporarily disable interrupt sources. */
		curenab = devpriv->intsce & ~intstat;
		outb(curenab, devpriv->iobase1 + PCI224_INT_SCE);
		devpriv->intr_running = 1;
		devpriv->intr_cpuid = THISCPU;
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
		if (valid_intstat != 0) {
			cmd = &s->async->cmd;
			if (valid_intstat & PCI224_INTR_EXT) {
				devpriv->intsce &= ~PCI224_INTR_EXT;
				if (cmd->start_src == TRIG_EXT)
					pci224_ao_start(dev, s);
				else if (cmd->stop_src == TRIG_EXT)
					pci224_ao_stop(dev, s);

			}
			if (valid_intstat & PCI224_INTR_DAC)
				pci224_ao_handle_fifo(dev, s);

		}
		/* Reenable interrupt sources. */
		spin_lock_irqsave(&devpriv->ao_spinlock, flags);
		if (curenab != devpriv->intsce) {
			outb(devpriv->intsce,
			     devpriv->iobase1 + PCI224_INT_SCE);
		}
		devpriv->intr_running = 0;
		spin_unlock_irqrestore(&devpriv->ao_spinlock, flags);
	}
	return IRQ_RETVAL(retval);
}

/*
 * This function looks for a board matching the supplied PCI device.
 */
static const struct pci224_board
*pci224_find_pci_board(struct pci_dev *pci_dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pci224_boards); i++)
		if (pci_dev->device == pci224_boards[i].devid)
			return &pci224_boards[i];
	return NULL;
}

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
static struct pci_dev *pci224_find_pci_dev(struct comedi_device *dev,
					   struct comedi_devconfig *it)
{
	const struct pci224_board *thisboard = comedi_board(dev);
	struct pci_dev *pci_dev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];

	for_each_pci_dev(pci_dev) {
		if (bus || slot) {
			if (bus != pci_dev->bus->number ||
			    slot != PCI_SLOT(pci_dev->devfn))
				continue;
		}
		if (pci_dev->vendor != PCI_VENDOR_ID_AMPLICON)
			continue;

		if (thisboard->model == any_model) {
			/* Match any supported model. */
			const struct pci224_board *board_ptr;

			board_ptr = pci224_find_pci_board(pci_dev);
			if (board_ptr == NULL)
				continue;
			/* Change board_ptr to matched board. */
			dev->board_ptr = board_ptr;
		} else {
			/* Match specific model name. */
			if (thisboard->devid != pci_dev->device)
				continue;
		}
		return pci_dev;
	}
	dev_err(dev->class_dev,
		"No supported board found! (req. bus %d, slot %d)\n",
		bus, slot);
	return NULL;
}

static void pci224_report_attach(struct comedi_device *dev, unsigned int irq)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	char tmpbuf[30];

	if (irq)
		snprintf(tmpbuf, sizeof(tmpbuf), "irq %u%s", irq,
			 (dev->irq ? "" : " UNAVAILABLE"));
	else
		snprintf(tmpbuf, sizeof(tmpbuf), "no irq");
	dev_info(dev->class_dev, "%s (pci %s) (%s) attached\n",
		 dev->board_name, pci_name(pcidev), tmpbuf);
}

/*
 * Common part of attach and auto_attach.
 */
static int pci224_attach_common(struct comedi_device *dev,
				struct pci_dev *pci_dev, int *options)
{
	const struct pci224_board *thisboard = comedi_board(dev);
	struct pci224_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	unsigned int irq;
	unsigned n;
	int ret;

	comedi_set_hw_dev(dev, &pci_dev->dev);

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	spin_lock_init(&devpriv->ao_spinlock);

	devpriv->iobase1 = pci_resource_start(pci_dev, 2);
	dev->iobase = pci_resource_start(pci_dev, 3);
	irq = pci_dev->irq;

	/* Allocate readback buffer for AO channels. */
	devpriv->ao_readback = kmalloc(sizeof(devpriv->ao_readback[0]) *
				       thisboard->ao_chans, GFP_KERNEL);
	if (!devpriv->ao_readback)
		return -ENOMEM;


	/* Allocate buffer to hold values for AO channel scan. */
	devpriv->ao_scan_vals = kmalloc(sizeof(devpriv->ao_scan_vals[0]) *
					thisboard->ao_chans, GFP_KERNEL);
	if (!devpriv->ao_scan_vals)
		return -ENOMEM;


	/* Allocate buffer to hold AO channel scan order. */
	devpriv->ao_scan_order = kmalloc(sizeof(devpriv->ao_scan_order[0]) *
					 thisboard->ao_chans, GFP_KERNEL);
	if (!devpriv->ao_scan_order)
		return -ENOMEM;


	/* Disable interrupt sources. */
	devpriv->intsce = 0;
	outb(0, devpriv->iobase1 + PCI224_INT_SCE);

	/* Initialize the DAC hardware. */
	outw(PCI224_DACCON_GLOBALRESET, dev->iobase + PCI224_DACCON);
	outw(0, dev->iobase + PCI224_DACCEN);
	outw(0, dev->iobase + PCI224_FIFOSIZ);
	devpriv->daccon = (PCI224_DACCON_TRIG_SW | PCI224_DACCON_POLAR_BI |
			   PCI224_DACCON_FIFOENAB |
			   PCI224_DACCON_FIFOINTR_EMPTY);
	outw(devpriv->daccon | PCI224_DACCON_FIFORESET,
	     dev->iobase + PCI224_DACCON);

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* Analog output subdevice. */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_CMD_WRITE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = (1 << thisboard->ao_bits) - 1;
	s->insn_write = &pci224_ao_insn_write;
	s->insn_read = &pci224_ao_insn_read;
	s->len_chanlist = s->n_chan;

	dev->write_subdev = s;
	s->do_cmd = &pci224_ao_cmd;
	s->do_cmdtest = &pci224_ao_cmdtest;
	s->cancel = &pci224_ao_cancel;
	s->munge = &pci224_ao_munge;

	/* Sort out channel range options. */
	if (thisboard->model == pci234_model) {
		/* PCI234 range options. */
		const struct comedi_lrange **range_table_list;

		s->range_table_list = range_table_list =
		    kmalloc(sizeof(struct comedi_lrange *) * s->n_chan,
			    GFP_KERNEL);
		if (!s->range_table_list)
			return -ENOMEM;

		if (options) {
			for (n = 2; n < 3 + s->n_chan; n++) {
				if (options[n] < 0 || options[n] > 1) {
					dev_warn(dev->class_dev, DRIVER_NAME
						 ": warning! bad options[%u]=%d\n",
						 n, options[n]);
				}
			}
		}
		for (n = 0; n < s->n_chan; n++) {
			if (n < COMEDI_NDEVCONFOPTS - 3 && options &&
			    options[3 + n] == 1) {
				if (options[2] == 1)
					range_table_list[n] = &range_pci234_ext;
				else
					range_table_list[n] = &range_bipolar5;

			} else {
				if (options && options[2] == 1) {
					range_table_list[n] =
					    &range_pci234_ext2;
				} else {
					range_table_list[n] = &range_bipolar10;
				}
			}
		}
		devpriv->hwrange = hwrange_pci234;
	} else {
		/* PCI224 range options. */
		if (options && options[2] == 1) {
			s->range_table = &range_pci224_external;
			devpriv->hwrange = hwrange_pci224_external;
		} else {
			if (options && options[2] != 0) {
				dev_warn(dev->class_dev, DRIVER_NAME
					 ": warning! bad options[2]=%d\n",
					 options[2]);
			}
			s->range_table = &range_pci224_internal;
			devpriv->hwrange = hwrange_pci224_internal;
		}
	}

	dev->board_name = thisboard->name;

	if (irq) {
		ret = request_irq(irq, pci224_interrupt, IRQF_SHARED,
				  DRIVER_NAME, dev);
		if (ret < 0) {
			dev_err(dev->class_dev,
				"error! unable to allocate irq %u\n", irq);
			return ret;
		} else {
			dev->irq = irq;
		}
	}

	pci224_report_attach(dev, irq);
	return 1;
}

static int pci224_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pci224_private *devpriv;
	struct pci_dev *pci_dev;

	dev_info(dev->class_dev, DRIVER_NAME ": attach\n");

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	pci_dev = pci224_find_pci_dev(dev, it);
	if (!pci_dev)
		return -EIO;

	return pci224_attach_common(dev, pci_dev, it->options);
}

static int
pci224_auto_attach(struct comedi_device *dev, unsigned long context_unused)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	struct pci224_private *devpriv;

	dev_info(dev->class_dev, DRIVER_NAME ": attach pci %s\n",
		 pci_name(pci_dev));

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	dev->board_ptr = pci224_find_pci_board(pci_dev);
	if (dev->board_ptr == NULL) {
		dev_err(dev->class_dev,
			DRIVER_NAME ": BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	/*
	 * Need to 'get' the PCI device to match the 'put' in pci224_detach().
	 * TODO: Remove the pci_dev_get() and matching pci_dev_put() once
	 * support for manual attachment of PCI devices via pci224_attach()
	 * has been removed.
	 */
	pci_dev_get(pci_dev);
	return pci224_attach_common(dev, pci_dev, NULL);
}

static void pci224_detach(struct comedi_device *dev)
{
	struct pci224_private *devpriv = dev->private;
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->subdevices) {
		struct comedi_subdevice *s;

		s = &dev->subdevices[0];
		/* AO subdevice */
		kfree(s->range_table_list);
	}
	if (devpriv) {
		kfree(devpriv->ao_readback);
		kfree(devpriv->ao_scan_vals);
		kfree(devpriv->ao_scan_order);
	}
	comedi_pci_disable(dev);
	if (pcidev)
		pci_dev_put(pcidev);
}

static struct comedi_driver amplc_pci224_driver = {
	.driver_name	= "amplc_pci224",
	.module		= THIS_MODULE,
	.attach		= pci224_attach,
	.detach		= pci224_detach,
	.auto_attach	= pci224_auto_attach,
	.board_name	= &pci224_boards[0].name,
	.offset		= sizeof(struct pci224_board),
	.num_names	= ARRAY_SIZE(pci224_boards),
};

static int amplc_pci224_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &amplc_pci224_driver,
				      id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(amplc_pci224_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI224) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI234) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, amplc_pci224_pci_table);

static struct pci_driver amplc_pci224_pci_driver = {
	.name		= "amplc_pci224",
	.id_table	= amplc_pci224_pci_table,
	.probe		= amplc_pci224_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(amplc_pci224_driver, amplc_pci224_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
