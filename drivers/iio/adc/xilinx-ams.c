/*
 * Xilinx AMS driver
 *
 * Licensed under the GPL-2
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/iopoll.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include <linux/io.h>

#include "xilinx-ams.h"
#include <linux/delay.h>

static const unsigned int AMS_UNMASK_TIMEOUT = 500;

static inline void ams_read_reg(struct ams *ams, unsigned int offset, u32 *data)
{
	*data = readl(ams->base + offset);
}

static inline void ams_write_reg(struct ams *ams, unsigned int offset, u32 data)
{
	writel(data, ams->base + offset);
}

static inline void ams_update_reg(struct ams *ams, unsigned int offset,
				  u32 mask, u32 data)
{
	u32 val;

	ams_read_reg(ams, offset, &val);
	ams_write_reg(ams, offset, (val & ~mask) | (mask & data));
}

static inline void ams_ps_read_reg(struct ams *ams, unsigned int offset,
				   u32 *data)
{
	*data = readl(ams->ps_base + offset);
}

static inline void ams_ps_write_reg(struct ams *ams, unsigned int offset,
				    u32 data)
{
	writel(data, ams->ps_base + offset);
}

static inline void ams_ps_update_reg(struct ams *ams, unsigned int offset,
				     u32 mask, u32 data)
{
	u32 val;

	ams_ps_read_reg(ams, offset, &val);
	ams_ps_write_reg(ams, offset, (val & ~mask) | (data & mask));
}

static inline void ams_apb_pl_read_reg(struct ams *ams, unsigned int offset,
				       u32 *data)
{
	*data = readl(ams->pl_base + offset);
}

static inline void ams_apb_pl_write_reg(struct ams *ams, unsigned int offset,
					u32 data)
{
	writel(data, ams->pl_base + offset);
}

static inline void ams_apb_pl_update_reg(struct ams *ams, unsigned int offset,
					 u32 mask, u32 data)
{
	u32 val;

	ams_apb_pl_read_reg(ams, offset, &val);
	ams_apb_pl_write_reg(ams, offset, (val & ~mask) | (data & mask));
}

static void ams_update_intrmask(struct ams *ams, u64 mask, u64 val)
{
	/* intr_mask variable in ams represent bit in AMS regisetr IDR0 and IDR1
	 * first 32 biit will be of IDR0, next one are of IDR1 register.
	 */
	ams->intr_mask &= ~mask;
	ams->intr_mask |= (val & mask);

	ams_write_reg(ams, AMS_IER_0, ~(ams->intr_mask | ams->masked_alarm));
	ams_write_reg(ams, AMS_IER_1,
		      ~(ams->intr_mask >> AMS_ISR1_INTR_MASK_SHIFT));
	ams_write_reg(ams, AMS_IDR_0, ams->intr_mask | ams->masked_alarm);
	ams_write_reg(ams, AMS_IDR_1,
		      ams->intr_mask >> AMS_ISR1_INTR_MASK_SHIFT);
}

static void iio_ams_disable_all_alarm(struct ams *ams)
{
	/* disable PS module alarm */
	if (ams->ps_base) {
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK,
				  AMS_REGCFG1_ALARM_MASK);
		ams_ps_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK,
				  AMS_REGCFG3_ALARM_MASK);
	}

	/* disable PL module alarm */
	if (ams->pl_base) {
		ams->pl_bus->update(ams, AMS_REG_CONFIG1,
				    AMS_REGCFG1_ALARM_MASK,
				    AMS_REGCFG1_ALARM_MASK);
		ams->pl_bus->update(ams, AMS_REG_CONFIG3,
				    AMS_REGCFG3_ALARM_MASK,
				    AMS_REGCFG3_ALARM_MASK);
	}
}

static void iio_ams_update_alarm(struct ams *ams, unsigned long alarm_mask)
{
	u32 cfg;
	unsigned long flags;
	unsigned long pl_alarm_mask;

	if (ams->ps_base) {
		/* Configuring PS alarm enable */
		cfg = ~((alarm_mask & AMS_ISR0_ALARM_2_TO_0_MASK) <<
			       AMS_CONF1_ALARM_2_TO_0_SHIFT);
		cfg &= ~((alarm_mask & AMS_ISR0_ALARM_6_TO_3_MASK) <<
				AMS_CONF1_ALARM_6_TO_3_SHIFT);
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_REGCFG1_ALARM_MASK,
				  cfg);

		cfg = ~((alarm_mask >> AMS_CONF3_ALARM_12_TO_7_SHIFT) &
				AMS_ISR0_ALARM_12_TO_7_MASK);
		ams_ps_update_reg(ams, AMS_REG_CONFIG3, AMS_REGCFG3_ALARM_MASK,
				  cfg);
	}

	if (ams->pl_base) {
		pl_alarm_mask = (alarm_mask >> AMS_PL_ALARM_START);
		/* Configuring PL alarm enable */
		cfg = ~((pl_alarm_mask & AMS_ISR0_ALARM_2_TO_0_MASK) <<
			       AMS_CONF1_ALARM_2_TO_0_SHIFT);
		cfg &= ~((pl_alarm_mask & AMS_ISR0_ALARM_6_TO_3_MASK) <<
				AMS_CONF1_ALARM_6_TO_3_SHIFT);
		ams->pl_bus->update(ams, AMS_REG_CONFIG1,
				AMS_REGCFG1_ALARM_MASK, cfg);

		cfg = ~((pl_alarm_mask >> AMS_CONF3_ALARM_12_TO_7_SHIFT) &
				AMS_ISR0_ALARM_12_TO_7_MASK);
		ams->pl_bus->update(ams, AMS_REG_CONFIG3,
				AMS_REGCFG3_ALARM_MASK, cfg);
	}

	spin_lock_irqsave(&ams->lock, flags);
	ams_update_intrmask(ams, AMS_ISR0_ALARM_MASK, ~alarm_mask);
	spin_unlock_irqrestore(&ams->lock, flags);
}

static void ams_enable_channel_sequence(struct ams *ams)
{
	int i;
	unsigned long long scan_mask;
	struct iio_dev *indio_dev = iio_priv_to_dev(ams);

	/* Enable channel sequence. First 22 bit of scan_mask represent
	 * PS channels, and  next remaining bit represents PL channels.
	 */

	/* Run calibration of PS & PL as part of the sequence */
	scan_mask = 1 | (1 << PS_SEQ_MAX);
	for (i = 0; i < indio_dev->num_channels; i++)
		scan_mask |= BIT(indio_dev->channels[i].scan_index);

	if (ams->ps_base) {
		/* put sysmon in a soft reset to change the sequence */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);

		/* configure basic channels */
		ams_ps_write_reg(ams, AMS_REG_SEQ_CH0,
				 scan_mask & AMS_REG_SEQ0_MASK);
		ams_ps_write_reg(ams, AMS_REG_SEQ_CH2, AMS_REG_SEQ2_MASK &
				(scan_mask >> AMS_REG_SEQ2_MASK_SHIFT));

		/* set continuous sequence mode */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_CONTINUOUS);
	}

	if (ams->pl_base) {
		/* put sysmon in a soft reset to change the sequence */
		ams->pl_bus->update(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				    AMS_CONF1_SEQ_DEFAULT);

		/* configure basic channels */
		scan_mask = (scan_mask >> PS_SEQ_MAX);
		ams->pl_bus->write(ams, AMS_REG_SEQ_CH0,
				scan_mask & AMS_REG_SEQ0_MASK);
		ams->pl_bus->write(ams, AMS_REG_SEQ_CH2, AMS_REG_SEQ2_MASK &
				(scan_mask >> AMS_REG_SEQ2_MASK_SHIFT));
		ams->pl_bus->write(ams, AMS_REG_SEQ_CH1, AMS_REG_SEQ1_MASK &
				(scan_mask >> AMS_REG_SEQ1_MASK_SHIFT));

		/* set continuous sequence mode */
		ams->pl_bus->update(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				AMS_CONF1_SEQ_CONTINUOUS);
	}
}

static int iio_ams_init_device(struct ams *ams)
{
	int ret = 0;
	u32 reg;

	/* reset AMS */
	if (ams->ps_base) {
		ams_ps_write_reg(ams, AMS_VP_VN, AMS_PS_RESET_VALUE);

		ret = readl_poll_timeout(ams->base + AMS_PS_CSTS, reg,
					 (reg & AMS_PS_CSTS_PS_READY) ==
					 AMS_PS_CSTS_PS_READY, 0,
					 AMS_INIT_TIMEOUT);
		if (ret)
			return ret;

		/* put sysmon in a default state */
		ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				  AMS_CONF1_SEQ_DEFAULT);
	}

	if (ams->pl_base) {
		ams->pl_bus->write(ams, AMS_VP_VN, AMS_PL_RESET_VALUE);

		ret = readl_poll_timeout(ams->base + AMS_PL_CSTS, reg,
					 (reg & AMS_PL_CSTS_ACCESS_MASK) ==
					 AMS_PL_CSTS_ACCESS_MASK, 0,
					 AMS_INIT_TIMEOUT);
		if (ret)
			return ret;

		/* put sysmon in a default state */
		ams->pl_bus->update(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
				    AMS_CONF1_SEQ_DEFAULT);
	}

	iio_ams_disable_all_alarm(ams);

	/* Disable interrupt */
	ams_update_intrmask(ams, ~0, ~0);

	/* Clear any pending interrupt */
	ams_write_reg(ams, AMS_ISR_0, AMS_ISR0_ALARM_MASK);
	ams_write_reg(ams, AMS_ISR_1, AMS_ISR1_ALARM_MASK);

	return ret;
}

static void ams_enable_single_channel(struct ams *ams, unsigned int offset)
{
	u8 channel_num = 0;

	switch (offset) {
	case AMS_VCC_PSPLL0:
		channel_num = AMS_VCC_PSPLL0_CH;
		break;
	case AMS_VCC_PSPLL3:
		channel_num = AMS_VCC_PSPLL3_CH;
		break;
	case AMS_VCCINT:
		channel_num = AMS_VCCINT_CH;
		break;
	case AMS_VCCBRAM:
		channel_num = AMS_VCCBRAM_CH;
		break;
	case AMS_VCCAUX:
		channel_num = AMS_VCCAUX_CH;
		break;
	case AMS_PSDDRPLL:
		channel_num = AMS_PSDDRPLL_CH;
		break;
	case AMS_PSINTFPDDR:
		channel_num = AMS_PSINTFPDDR_CH;
		break;
	default:
		break;
	}

	/* set single channel, sequencer off mode */
	ams_ps_update_reg(ams, AMS_REG_CONFIG1, AMS_CONF1_SEQ_MASK,
			AMS_CONF1_SEQ_SINGLE_CHANNEL);

	/* write the channel number */
	ams_ps_update_reg(ams, AMS_REG_CONFIG0, AMS_CONF0_CHANNEL_NUM_MASK,
			channel_num);
	mdelay(1);
}

static void ams_read_vcc_reg(struct ams *ams, unsigned int offset, u32 *data)
{
	ams_enable_single_channel(ams, offset);
	ams_read_reg(ams, offset, data);
	ams_enable_channel_sequence(ams);
}

static int ams_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	struct ams *ams = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&ams->mutex);
		if (chan->scan_index >= (PS_SEQ_MAX * 3))
			ams_read_vcc_reg(ams, chan->address, val);
		else if (chan->scan_index >= PS_SEQ_MAX)
			ams->pl_bus->read(ams, chan->address, val);
		else
			ams_ps_read_reg(ams, chan->address, val);
		mutex_unlock(&ams->mutex);

		*val2 = 0;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_VOLTAGE:
			switch (chan->address) {
			case AMS_SUPPLY1:
			case AMS_SUPPLY2:
			case AMS_SUPPLY3:
			case AMS_SUPPLY4:
				*val = AMS_SUPPLY_SCALE_3VOLT;
				break;
			case AMS_SUPPLY5:
			case AMS_SUPPLY6:
				if (chan->scan_index < PS_SEQ_MAX)
					*val = AMS_SUPPLY_SCALE_6VOLT;
				else
					*val = AMS_SUPPLY_SCALE_3VOLT;
				break;
			case AMS_SUPPLY7:
			case AMS_SUPPLY8:
				*val = AMS_SUPPLY_SCALE_6VOLT;
				break;
			case AMS_SUPPLY9:
			case AMS_SUPPLY10:
				if (chan->scan_index < PS_SEQ_MAX)
					*val = AMS_SUPPLY_SCALE_3VOLT;
				else
					*val = AMS_SUPPLY_SCALE_6VOLT;
				break;
			default:
				if (chan->scan_index >= (PS_SEQ_MAX * 3))
					*val = AMS_SUPPLY_SCALE_3VOLT;
				else
					*val = AMS_SUPPLY_SCALE_1VOLT;
				break;
			}
			*val2 = AMS_SUPPLY_SCALE_DIV_BIT;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_TEMP:
			*val = AMS_TEMP_SCALE;
			*val2 = AMS_TEMP_SCALE_DIV_BIT;
			return IIO_VAL_FRACTIONAL_LOG2;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		/* Only the temperature channel has an offset */
		*val = AMS_TEMP_OFFSET;
		*val2 = 0;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int ams_get_alarm_offset(int scan_index, enum iio_event_direction dir)
{
	int offset = 0;

	if (scan_index >= PS_SEQ_MAX)
		scan_index -= PS_SEQ_MAX;

	if (dir == IIO_EV_DIR_FALLING) {
		if (scan_index < AMS_SEQ_SUPPLY7)
			offset = AMS_ALARM_THRESOLD_OFF_10;
		else
			offset = AMS_ALARM_THRESOLD_OFF_20;
	}

	switch (scan_index) {
	case AMS_SEQ_TEMP:
		return (AMS_ALARM_TEMP + offset);
	case AMS_SEQ_SUPPLY1:
		return (AMS_ALARM_SUPPLY1 + offset);
	case AMS_SEQ_SUPPLY2:
		return (AMS_ALARM_SUPPLY2 + offset);
	case AMS_SEQ_SUPPLY3:
		return (AMS_ALARM_SUPPLY3 + offset);
	case AMS_SEQ_SUPPLY4:
		return (AMS_ALARM_SUPPLY4 + offset);
	case AMS_SEQ_SUPPLY5:
		return (AMS_ALARM_SUPPLY5 + offset);
	case AMS_SEQ_SUPPLY6:
		return (AMS_ALARM_SUPPLY6 + offset);
	case AMS_SEQ_SUPPLY7:
		return (AMS_ALARM_SUPPLY7 + offset);
	case AMS_SEQ_SUPPLY8:
		return (AMS_ALARM_SUPPLY8 + offset);
	case AMS_SEQ_SUPPLY9:
		return (AMS_ALARM_SUPPLY9 + offset);
	case AMS_SEQ_SUPPLY10:
		return (AMS_ALARM_SUPPLY10 + offset);
	case AMS_SEQ_VCCAMS:
		return (AMS_ALARM_VCCAMS + offset);
	case AMS_SEQ_TEMP_REMOTE:
		return (AMS_ALARM_TEMP_REMOTE + offset);
	}

	return 0;
}

static const struct iio_chan_spec *ams_event_to_channel(
		struct iio_dev *indio_dev, u32 event)
{
	int scan_index = 0, i;

	if (event >= AMS_PL_ALARM_START) {
		event -= AMS_PL_ALARM_START;
		scan_index = PS_SEQ_MAX;
	}

	switch (event) {
	case AMS_ALARM_BIT_TEMP:
		scan_index += AMS_SEQ_TEMP;
		break;
	case AMS_ALARM_BIT_SUPPLY1:
		scan_index += AMS_SEQ_SUPPLY1;
		break;
	case AMS_ALARM_BIT_SUPPLY2:
		scan_index += AMS_SEQ_SUPPLY2;
		break;
	case AMS_ALARM_BIT_SUPPLY3:
		scan_index += AMS_SEQ_SUPPLY3;
		break;
	case AMS_ALARM_BIT_SUPPLY4:
		scan_index += AMS_SEQ_SUPPLY4;
		break;
	case AMS_ALARM_BIT_SUPPLY5:
		scan_index += AMS_SEQ_SUPPLY5;
		break;
	case AMS_ALARM_BIT_SUPPLY6:
		scan_index += AMS_SEQ_SUPPLY6;
		break;
	case AMS_ALARM_BIT_SUPPLY7:
		scan_index += AMS_SEQ_SUPPLY7;
		break;
	case AMS_ALARM_BIT_SUPPLY8:
		scan_index += AMS_SEQ_SUPPLY8;
		break;
	case AMS_ALARM_BIT_SUPPLY9:
		scan_index += AMS_SEQ_SUPPLY9;
		break;
	case AMS_ALARM_BIT_SUPPLY10:
		scan_index += AMS_SEQ_SUPPLY10;
		break;
	case AMS_ALARM_BIT_VCCAMS:
		scan_index += AMS_SEQ_VCCAMS;
		break;
	case AMS_ALARM_BIT_TEMP_REMOTE:
		scan_index += AMS_SEQ_TEMP_REMOTE;
		break;
	}

	for (i = 0; i < indio_dev->num_channels; i++)
		if (indio_dev->channels[i].scan_index == scan_index)
			break;

	return &indio_dev->channels[i];
}

static int ams_get_alarm_mask(int scan_index)
{
	int bit = 0;

	if (scan_index >= PS_SEQ_MAX) {
		bit = AMS_PL_ALARM_START;
		scan_index -= PS_SEQ_MAX;
	}

	switch (scan_index) {
	case AMS_SEQ_TEMP:
		return BIT(AMS_ALARM_BIT_TEMP + bit);
	case AMS_SEQ_SUPPLY1:
		return BIT(AMS_ALARM_BIT_SUPPLY1 + bit);
	case AMS_SEQ_SUPPLY2:
		return BIT(AMS_ALARM_BIT_SUPPLY2 + bit);
	case AMS_SEQ_SUPPLY3:
		return BIT(AMS_ALARM_BIT_SUPPLY3 + bit);
	case AMS_SEQ_SUPPLY4:
		return BIT(AMS_ALARM_BIT_SUPPLY4 + bit);
	case AMS_SEQ_SUPPLY5:
		return BIT(AMS_ALARM_BIT_SUPPLY5 + bit);
	case AMS_SEQ_SUPPLY6:
		return BIT(AMS_ALARM_BIT_SUPPLY6 + bit);
	case AMS_SEQ_SUPPLY7:
		return BIT(AMS_ALARM_BIT_SUPPLY7 + bit);
	case AMS_SEQ_SUPPLY8:
		return BIT(AMS_ALARM_BIT_SUPPLY8 + bit);
	case AMS_SEQ_SUPPLY9:
		return BIT(AMS_ALARM_BIT_SUPPLY9 + bit);
	case AMS_SEQ_SUPPLY10:
		return BIT(AMS_ALARM_BIT_SUPPLY10 + bit);
	case AMS_SEQ_VCCAMS:
		return BIT(AMS_ALARM_BIT_VCCAMS + bit);
	case AMS_SEQ_TEMP_REMOTE:
		return BIT(AMS_ALARM_BIT_TEMP_REMOTE + bit);
	}

	return 0;
}

static int ams_read_event_config(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir)
{
	struct ams *ams = iio_priv(indio_dev);

	return (ams->alarm_mask & ams_get_alarm_mask(chan->scan_index)) ? 1 : 0;
}

static int ams_write_event_config(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  int state)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int alarm;

	alarm = ams_get_alarm_mask(chan->scan_index);

	mutex_lock(&ams->mutex);

	if (state)
		ams->alarm_mask |= alarm;
	else
		ams->alarm_mask &= ~alarm;

	iio_ams_update_alarm(ams, ams->alarm_mask);

	mutex_unlock(&ams->mutex);

	return 0;
}

static int ams_read_event_value(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir,
				enum iio_event_info info, int *val, int *val2)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int offset = ams_get_alarm_offset(chan->scan_index, dir);

	mutex_lock(&ams->mutex);

	if (chan->scan_index >= PS_SEQ_MAX)
		ams->pl_bus->read(ams, offset, val);
	else
		ams_ps_read_reg(ams, offset, val);

	mutex_unlock(&ams->mutex);

	*val2 = 0;
	return IIO_VAL_INT;
}

static int ams_write_event_value(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int val, int val2)
{
	struct ams *ams = iio_priv(indio_dev);
	unsigned int offset;

	mutex_lock(&ams->mutex);

	/* Set temperature channel threshold to direct threshold */
	if (chan->type == IIO_TEMP) {
		offset = ams_get_alarm_offset(chan->scan_index,
					      IIO_EV_DIR_FALLING);

		if (chan->scan_index >= PS_SEQ_MAX)
			ams->pl_bus->update(ams, offset,
					    AMS_ALARM_THR_DIRECT_MASK,
					    AMS_ALARM_THR_DIRECT_MASK);
		else
			ams_ps_update_reg(ams, offset,
					  AMS_ALARM_THR_DIRECT_MASK,
					  AMS_ALARM_THR_DIRECT_MASK);
	}

	offset = ams_get_alarm_offset(chan->scan_index, dir);
	if (chan->scan_index >= PS_SEQ_MAX)
		ams->pl_bus->write(ams, offset, val);
	else
		ams_ps_write_reg(ams, offset, val);

	mutex_unlock(&ams->mutex);

	return 0;
}

static void ams_handle_event(struct iio_dev *indio_dev, u32 event)
{
	const struct iio_chan_spec *chan;

	chan = ams_event_to_channel(indio_dev, event);

	if (chan->type == IIO_TEMP) {
		/* The temperature channel only supports over-temperature
		 * events
		 */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(chan->type, chan->channel,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			iio_get_time_ns(indio_dev));
	} else {
		/* For other channels we don't know whether it is a upper or
		 * lower threshold event. Userspace will have to check the
		 * channel value if it wants to know.
		 */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(chan->type, chan->channel,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			iio_get_time_ns(indio_dev));
	}
}

static void ams_handle_events(struct iio_dev *indio_dev, unsigned long events)
{
	unsigned int bit;

	for_each_set_bit(bit, &events, AMS_NO_OF_ALARMS)
		ams_handle_event(indio_dev, bit);
}

/**
 * ams_unmask_worker - ams alarm interrupt unmask worker
 * @work :		work to be done
 *
 * The ZynqMP threshold interrupts are level sensitive. Since we can't make the
 * threshold condition go way from within the interrupt handler, this means as
 * soon as a threshold condition is present we would enter the interrupt handler
 * again and again. To work around this we mask all active thresholds interrupts
 * in the interrupt handler and start a timer. In this timer we poll the
 * interrupt status and only if the interrupt is inactive we unmask it again.
 */
static void ams_unmask_worker(struct work_struct *work)
{
	struct ams *ams = container_of(work, struct ams, ams_unmask_work.work);
	unsigned int status, unmask;

	spin_lock_irq(&ams->lock);

	ams_read_reg(ams, AMS_ISR_0, &status);

	/* Clear those bits which are not active anymore */
	unmask = (ams->masked_alarm ^ status) & ams->masked_alarm;

	/* clear status of disabled alarm */
	unmask |= ams->intr_mask;

	ams->masked_alarm &= status;

	/* Also clear those which are masked out anyway */
	ams->masked_alarm &= ~ams->intr_mask;

	/* Clear the interrupts before we unmask them */
	ams_write_reg(ams, AMS_ISR_0, unmask);

	ams_update_intrmask(ams, 0, 0);

	spin_unlock_irq(&ams->lock);

	/* if still pending some alarm re-trigger the timer */
	if (ams->masked_alarm)
		schedule_delayed_work(&ams->ams_unmask_work,
				      msecs_to_jiffies(AMS_UNMASK_TIMEOUT));
}

static irqreturn_t ams_iio_irq(int irq, void *data)
{
	unsigned int isr0, isr1;
	struct iio_dev *indio_dev = data;
	struct ams *ams = iio_priv(indio_dev);

	spin_lock(&ams->lock);

	ams_read_reg(ams, AMS_ISR_0, &isr0);
	ams_read_reg(ams, AMS_ISR_1, &isr1);

	/* only process alarm that are not masked */
	isr0 &= ~((ams->intr_mask & AMS_ISR0_ALARM_MASK) | ams->masked_alarm);
	isr1 &= ~(ams->intr_mask >> AMS_ISR1_INTR_MASK_SHIFT);

	/* clear interrupt */
	ams_write_reg(ams, AMS_ISR_0, isr0);
	ams_write_reg(ams, AMS_ISR_1, isr1);

	if (isr0) {
		/* Once the alarm interrupt occurred, mask until get cleared */
		ams->masked_alarm |= isr0;
		ams_update_intrmask(ams, 0, 0);

		ams_handle_events(indio_dev, isr0);

		schedule_delayed_work(&ams->ams_unmask_work,
				      msecs_to_jiffies(AMS_UNMASK_TIMEOUT));
	}

	spin_unlock(&ams->lock);

	return IRQ_HANDLED;
}

static const struct iio_event_spec ams_temp_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_event_spec ams_voltage_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec ams_ps_channels[] = {
	AMS_PS_CHAN_TEMP(AMS_SEQ_TEMP, AMS_TEMP, "ps_temp"),
	AMS_PS_CHAN_TEMP(AMS_SEQ_TEMP_REMOTE, AMS_TEMP_REMOTE, "remote_temp"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY1, AMS_SUPPLY1, "vccpsintlp"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY2, AMS_SUPPLY2, "vccpsintfp"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY3, AMS_SUPPLY3, "vccpsaux"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY4, AMS_SUPPLY4, "vccpsddr"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY5, AMS_SUPPLY5, "vccpsio3"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY6, AMS_SUPPLY6, "vccpsio0"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY7, AMS_SUPPLY7, "vccpsio1"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY8, AMS_SUPPLY8, "vccpsio2"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY9, AMS_SUPPLY9, "psmgtravcc"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_SUPPLY10, AMS_SUPPLY10, "psmgtravtt"),
	AMS_PS_CHAN_VOLTAGE(AMS_SEQ_VCCAMS, AMS_VCCAMS, "vccams"),
};

static const struct iio_chan_spec ams_pl_channels[] = {
	AMS_PL_CHAN_TEMP(AMS_SEQ_TEMP, AMS_TEMP, "pl_temp"),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY1, AMS_SUPPLY1, "vccint", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY2, AMS_SUPPLY2, "vccaux", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VREFP, AMS_VREFP, "vccvrefp", false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VREFN, AMS_VREFN, "vccvrefn", false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY3, AMS_SUPPLY3, "vccbram", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY4, AMS_SUPPLY4, "vccplintlp", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY5, AMS_SUPPLY5, "vccplintfp", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY6, AMS_SUPPLY6, "vccplaux", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VCCAMS, AMS_VCCAMS, "vccams", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_VP_VN, AMS_VP_VN, "vccvpvn", false),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY7, AMS_SUPPLY7, "vuser0", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY8, AMS_SUPPLY8, "vuser1", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY9, AMS_SUPPLY9, "vuser2", true),
	AMS_PL_CHAN_VOLTAGE(AMS_SEQ_SUPPLY10, AMS_SUPPLY10, "vuser3", true),
	AMS_PL_AUX_CHAN_VOLTAGE(0, "vccaux0"),
	AMS_PL_AUX_CHAN_VOLTAGE(1, "vccaux1"),
	AMS_PL_AUX_CHAN_VOLTAGE(2, "vccaux2"),
	AMS_PL_AUX_CHAN_VOLTAGE(3, "vccaux3"),
	AMS_PL_AUX_CHAN_VOLTAGE(4, "vccaux4"),
	AMS_PL_AUX_CHAN_VOLTAGE(5, "vccaux5"),
	AMS_PL_AUX_CHAN_VOLTAGE(6, "vccaux6"),
	AMS_PL_AUX_CHAN_VOLTAGE(7, "vccaux7"),
	AMS_PL_AUX_CHAN_VOLTAGE(8, "vccaux8"),
	AMS_PL_AUX_CHAN_VOLTAGE(9, "vccaux9"),
	AMS_PL_AUX_CHAN_VOLTAGE(10, "vccaux10"),
	AMS_PL_AUX_CHAN_VOLTAGE(11, "vccaux11"),
	AMS_PL_AUX_CHAN_VOLTAGE(12, "vccaux12"),
	AMS_PL_AUX_CHAN_VOLTAGE(13, "vccaux13"),
	AMS_PL_AUX_CHAN_VOLTAGE(14, "vccaux14"),
	AMS_PL_AUX_CHAN_VOLTAGE(15, "vccaux15"),
};

static const struct iio_chan_spec ams_ctrl_channels[] = {
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCC_PSPLL, AMS_VCC_PSPLL0, "vcc_pspll0"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCC_PSBATT, AMS_VCC_PSPLL3, "vcc_psbatt"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCINT, AMS_VCCINT, "vccint"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCBRAM, AMS_VCCBRAM, "vccbram"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_VCCAUX, AMS_VCCAUX, "vccaux"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_PSDDRPLL, AMS_PSDDRPLL, "vcc_psddrpll"),
	AMS_CTRL_CHAN_VOLTAGE(AMS_SEQ_INTDDR, AMS_PSINTFPDDR, "vccpsintfpddr"),
};

static int ams_init_module(struct iio_dev *indio_dev, struct device_node *np,
			   struct iio_chan_spec *channels)
{
	struct ams *ams = iio_priv(indio_dev);
	struct device_node *chan_node, *child;
	int ret, num_channels = 0;
	unsigned int reg;

	if (of_device_is_compatible(np, "xlnx,zynqmp-ams-ps")) {
		ams->ps_base = of_iomap(np, 0);
		if (!ams->ps_base)
			return -ENXIO;

		/* add PS channels to iio device channels */
		memcpy(channels + num_channels, ams_ps_channels,
		       sizeof(ams_ps_channels));
		num_channels += ARRAY_SIZE(ams_ps_channels);
	} else if (of_device_is_compatible(np, "xlnx,zynqmp-ams-pl")) {
		ams->pl_base = of_iomap(np, 0);
		if (!ams->pl_base)
			return -ENXIO;

		/* Copy only first 10 fix channels */
		memcpy(channels + num_channels, ams_pl_channels,
		       AMS_PL_MAX_FIXED_CHANNEL * sizeof(*channels));
		num_channels += AMS_PL_MAX_FIXED_CHANNEL;

		chan_node = of_get_child_by_name(np, "xlnx,ext-channels");
		if (chan_node) {
			for_each_child_of_node(chan_node, child) {
				ret = of_property_read_u32(child, "reg", &reg);
				if (ret || reg > AMS_PL_MAX_EXT_CHANNEL)
					continue;

				memcpy(&channels[num_channels],
				       &ams_pl_channels[reg +
				       AMS_PL_MAX_FIXED_CHANNEL],
				       sizeof(*channels));

				if (of_property_read_bool(child,
							  "xlnx,bipolar"))
					channels[num_channels].
						scan_type.sign = 's';

				num_channels += 1;
			}
		}
		of_node_put(chan_node);
	} else if (of_device_is_compatible(np, "xlnx,zynqmp-ams")) {
		/* add AMS channels to iio device channels */
		memcpy(channels + num_channels, ams_ctrl_channels,
				sizeof(ams_ctrl_channels));
		num_channels += ARRAY_SIZE(ams_ctrl_channels);
	} else {
		return -EINVAL;
	}

	return num_channels;
}

static int ams_parse_dt(struct iio_dev *indio_dev, struct platform_device *pdev)
{
	struct ams *ams = iio_priv(indio_dev);
	struct iio_chan_spec *ams_channels, *dev_channels;
	struct device_node *child_node = NULL, *np = pdev->dev.of_node;
	int ret, chan_vol = 0, chan_temp = 0, i, rising_off, falling_off;
	unsigned int num_channels = 0;

	/* Initialize buffer for channel specification */
	ams_channels = kzalloc(sizeof(ams_ps_channels) +
			       sizeof(ams_pl_channels) +
			       sizeof(ams_ctrl_channels), GFP_KERNEL);
	if (!ams_channels)
		return -ENOMEM;

	if (of_device_is_available(np)) {
		ret = ams_init_module(indio_dev, np, ams_channels);
		if (ret < 0) {
			kfree(ams_channels);
			return ret;
		}

		num_channels += ret;
	}

	for_each_child_of_node(np, child_node) {
		if (of_device_is_available(child_node)) {
			ret = ams_init_module(indio_dev, child_node,
					      ams_channels + num_channels);
			if (ret < 0) {
				kfree(ams_channels);
				return ret;
			}

			num_channels += ret;
		}
	}

	for (i = 0; i < num_channels; i++) {
		if (ams_channels[i].type == IIO_VOLTAGE)
			ams_channels[i].channel = chan_vol++;
		else
			ams_channels[i].channel = chan_temp++;

		if (ams_channels[i].scan_index < (PS_SEQ_MAX * 3)) {
			/* set threshold to max and min for each channel */
			falling_off = ams_get_alarm_offset(
					ams_channels[i].scan_index,
					IIO_EV_DIR_FALLING);
			rising_off = ams_get_alarm_offset(
					ams_channels[i].scan_index,
					IIO_EV_DIR_RISING);
			if (ams_channels[i].scan_index >= PS_SEQ_MAX) {
				ams->pl_bus->write(ams, falling_off,
						AMS_ALARM_THR_MIN);
				ams->pl_bus->write(ams, rising_off,
						AMS_ALARM_THR_MAX);
			} else {
				ams_ps_write_reg(ams, falling_off,
						AMS_ALARM_THR_MIN);
				ams_ps_write_reg(ams, rising_off,
						AMS_ALARM_THR_MAX);
			}
		}
	}

	dev_channels = devm_kzalloc(&pdev->dev, sizeof(*dev_channels) *
				    num_channels, GFP_KERNEL);
	if (!dev_channels) {
		kfree(ams_channels);
		return -ENOMEM;
	}

	memcpy(dev_channels, ams_channels,
	       sizeof(*ams_channels) * num_channels);
	kfree(ams_channels);
	indio_dev->channels = dev_channels;
	indio_dev->num_channels = num_channels;

	return 0;
}

static const struct iio_info iio_pl_info = {
	.read_raw = &ams_read_raw,
	.read_event_config = &ams_read_event_config,
	.write_event_config = &ams_write_event_config,
	.read_event_value = &ams_read_event_value,
	.write_event_value = &ams_write_event_value,
};

static const struct ams_pl_bus_ops ams_pl_apb = {
	.read = ams_apb_pl_read_reg,
	.write = ams_apb_pl_write_reg,
	.update = ams_apb_pl_update_reg,
};

static const struct of_device_id ams_of_match_table[] = {
	{ .compatible = "xlnx,zynqmp-ams", &ams_pl_apb },
	{ }
};
MODULE_DEVICE_TABLE(of, ams_of_match_table);

static int ams_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct ams *ams;
	struct resource *res;
	const struct of_device_id *id;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	id = of_match_node(ams_of_match_table, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*ams));
	if (!indio_dev)
		return -ENOMEM;

	ams = iio_priv(indio_dev);
	ams->pl_bus = id->data;
	mutex_init(&ams->mutex);
	spin_lock_init(&ams->lock);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->name = "ams";

	indio_dev->info = &iio_pl_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ams-base");
	ams->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ams->base))
		return PTR_ERR(ams->base);

	INIT_DELAYED_WORK(&ams->ams_unmask_work, ams_unmask_worker);

	ams->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ams->clk))
		return PTR_ERR(ams->clk);
	clk_prepare_enable(ams->clk);

	ret = iio_ams_init_device(ams);
	if (ret) {
		dev_err(&pdev->dev, "failed to initialize AMS\n");
		goto clk_disable;
	}

	ret = ams_parse_dt(indio_dev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "failure in parsing DT\n");
		goto clk_disable;
	}

	ams_enable_channel_sequence(ams);

	ams->irq = platform_get_irq_byname(pdev, "ams-irq");
	ret = devm_request_irq(&pdev->dev, ams->irq, &ams_iio_irq, 0, "ams-irq",
			       indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register interrupt\n");
		goto clk_disable;
	}

	platform_set_drvdata(pdev, indio_dev);

	return iio_device_register(indio_dev);

clk_disable:
	clk_disable_unprepare(ams->clk);
	return ret;
}

static int ams_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct ams *ams = iio_priv(indio_dev);

	cancel_delayed_work(&ams->ams_unmask_work);

	/* Unregister the device */
	iio_device_unregister(indio_dev);
	clk_disable_unprepare(ams->clk);
	return 0;
}

static int __maybe_unused ams_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ams *ams = iio_priv(indio_dev);

	clk_disable_unprepare(ams->clk);

	return 0;
}

static int __maybe_unused ams_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ams *ams = iio_priv(indio_dev);

	clk_prepare_enable(ams->clk);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ams_pm_ops, ams_suspend, ams_resume);

static struct platform_driver ams_driver = {
	.probe = ams_probe,
	.remove = ams_remove,
	.driver = {
		.name = "ams",
		.pm	= &ams_pm_ops,
		.of_match_table = ams_of_match_table,
	},
};
module_platform_driver(ams_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rajnikant Bhojani <rajnikant.bhojani@xilinx.com>");
