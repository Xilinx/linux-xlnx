/*
 * Generic driver functionality for logiI2S FPGA IP core
 *
 * Copyright (C) 2014 Xylon d.o.o.
 * Author: Davor Joja <davor.joja@logicbricks.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/types.h>

#include "logii2s.h"

/*
 * Default sample rate
 */
#define DEFAULT_SAMPLE_RATE	44100

/*
 * Function resets the logiI2S port - resets the FIFO, bit clock and
 * transmission/reception logic, clears Interrupt Enable Register and
 * Interrupt Clear Register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_reset(struct logii2s_port *port)
{
	u32 ctrl = logii2s_read(port->base, LOGII2S_CTRL_ROFF);

	/*
	* The word select signal must be left unchanged due to
	* the possibility of it being used by another i2s instance
	*/
	ctrl &= LOGII2S_CTRL_WS_MASK;
	ctrl |= LOGII2S_CTRL_SWR;
	logii2s_write(port->base, LOGII2S_CTRL_ROFF, ctrl);
	logii2s_write(port->base, LOGII2S_IMR_ROFF, LOGII2S_INT_MASK_ALL);
	logii2s_write(port->base, LOGII2S_ISR_ROFF, LOGII2S_INT_ACK_ALL);
}

/*
 * @param	core_clock is I2S core clock.
 * @param	clock_freq is I2S bus clock.
 * @param	ws_master is I2S word select master flag.
 * @param	prescale is the prescaler value set in logiI2S.
 * @param	sample_rate is PCM sampling rate.
 * @param	ws_left
 * @param	ws_right
 *
 * @return	None.
 *
 * @note	None.
 */
static void logii2s_port_calc_ws_value(unsigned int core_clock_freq,
				       unsigned int clock_freq,
				       bool clock_master,
				       unsigned int prescale,
				       unsigned int sample_rate,
				       u32 *ws_left,
				       u32 *ws_right)
{
	if (clock_master)
		clock_freq = core_clock_freq / (2 * prescale);

	*ws_left = clock_freq / (2 * sample_rate);
	*ws_right = *ws_left;
}

/*
 * Function sets the word select length for the right and the left channel
 * depending on the selected sample rate, only if the logiI2S port is
 * configured as a clock master. The word select length parameter decides the
 * number of cycles before the word select signal switches for right respective
 * left side.
 * If the logiI2S port can't drive requested sampling rate, sampling rate will
 * be set to 48000Hz.
 *
 * @param	port is a pointer to the logii2s_port information structure.
 * @param	sample_rate is a desired sampling rate.
 *
 * @return	Used PCM sample rate.
 *
 * @note	None.
 */
unsigned int logii2s_port_init_clock(struct logii2s_port *port,
				     unsigned int core_clock_freq,
				     unsigned int sample_rate)
{
	u32 ctrl = logii2s_read(port->base, LOGII2S_CTRL_ROFF);
	u32 prescale, ws_left, ws_right;

	if (!(ctrl & (LOGII2S_CTRL_CLKMASTER | LOGII2S_CTRL_WSMASTER)))
		return 0;

	if (sample_rate < 8000 || sample_rate > 192000)
		sample_rate = DEFAULT_SAMPLE_RATE;

	prescale = (core_clock_freq / (2 * port->clock_freq));

	logii2s_port_calc_ws_value(core_clock_freq, port->clock_freq,
				   (ctrl & LOGII2S_CTRL_CLKMASTER),
				   prescale, sample_rate, &ws_left, &ws_right);
	ws_left <<= 14;
	ws_right <<= 4;

	ctrl &= (~LOGII2S_CTRL_WS_MASK);
	ctrl |= (ws_left | ws_right);

	logii2s_write(port->base, LOGII2S_PRESCALE_ROFF, prescale);
	logii2s_write(port->base, LOGII2S_CTRL_ROFF, ctrl);

	return sample_rate;
}

/*
 * Function gets the content of the Interrupt Unit Register.
 * This register indicates which logiI2S instance has generated an interrupt.
 * This information is used in further logiI2S registers accessing.
 *
 * @param	base is a pointer to the I2S device base address.
 *
 * @return	I2S instance interrupt register.
 *
 * @note	None.
 */
u32 logii2s_get_device_iur(void __iomem *base)
{
	return logii2s_read(base, LOGII2S_INST_INT_ROFF);
}

/*
 * Function enables the specified interrupts in the Interrupt Mask Register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	bit_mask is the bitmask of the interrupts to be enabled.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_unmask_int(struct logii2s_port *port, u32 bit_mask)
{
	u32 imr = logii2s_read(port->base, LOGII2S_IMR_ROFF);

	imr &= (~bit_mask);
	logii2s_write(port->base, LOGII2S_IMR_ROFF, imr);
}

/*
 * Function disables specified interrupts in the Interrupt Enable Register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	bit_mask is the bitmask of the interrupts to be enabled.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_mask_int(struct logii2s_port *port, u32 bit_mask)
{
	u32 imr = logii2s_read(port->base, LOGII2S_IMR_ROFF);

	imr |= bit_mask;
	logii2s_write(port->base, LOGII2S_IMR_ROFF, imr);
}

/*
 * Function gets the content of the Interrupt Status Register.
 * This register indicates the status of interrupt sources for the I2S.
 * The status is independent of whether interrupts are enabled such
 * that the status register may also be polled when interrupts are not enabled.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	A status which contains the value read from the Interrupt
 *		Status Register.
 *
 * @note	None.
 */
u32 logii2s_port_get_isr(struct logii2s_port *port)
{
	return logii2s_read(port->base, LOGII2S_ISR_ROFF);
}

/*
 * Function clears the specified interrupts in the Interrupt Clear Register.
 * The interrupt is cleared by writing to this register with the bits
 * to be cleared set to a 1 and all others to 0.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	bit_mask is the bitmask for interrupts to be cleared.
 *		Writing "1" clears the interrupt.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_clear_isr(struct logii2s_port *port, u32 bit_mask)
{
	logii2s_write(port->base, LOGII2S_ISR_ROFF, bit_mask);
}

/*
 * Function enables reception.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_enable_xfer(struct logii2s_port *port)
{
	u32 ctrl = logii2s_read(port->base, LOGII2S_CTRL_ROFF);

	ctrl |= LOGII2S_CTRL_ENABLE;
	logii2s_write(port->base, LOGII2S_CTRL_ROFF, ctrl);
}

/*
 *
 * Function disables reception.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	None.
 *
 * @note	When reception is disabled, the corresponding right side word is
 *		received before stopping.
 */
void logii2s_port_disable_xfer(struct logii2s_port *port)
{
	u32 ctrl = logii2s_read(port->base, LOGII2S_CTRL_ROFF);

	ctrl &= (~LOGII2S_CTRL_ENABLE);
	logii2s_write(port->base, LOGII2S_CTRL_ROFF, ctrl);
}

/*
 * Function checks logiI2S port direction configuration.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	0 if receiver or 1 if transmitter.
 *
 * @note	None.
 */
unsigned int logii2s_port_direction(struct logii2s_port *port)
{
	u32 ctrl = logii2s_read(port->base, LOGII2S_CTRL_ROFF);

	return ctrl & LOGII2S_CTRL_DIR ?
	       LOGII2S_TX_INSTANCE : LOGII2S_RX_INSTANCE;
}

/*
 * Function gets the logiI2S IP core version.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	A 32-bit value which contains the content of the
 *		Hardware Version Register.
 *
 * @note	16 upper bits represents the major version and the 16 lower
 *		bits represent the minor version.
 */
u32 logii2s_port_get_version(struct logii2s_port *port)
{
	return logii2s_read(port->base, LOGII2S_HW_VERSION_ROFF);
}

/*
 * Function reads one data word from the FIFO register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 *
 * @return	A 32-bit value representing the content read from the FIFO register.
 *
 * @note	None.
 */
u32 logii2s_port_read_fifo_word(struct logii2s_port *port)
{
	return logii2s_read(port->base, LOGII2S_FIFO_ROFF);
}

/*
 * Function writes one data word to the FIFO register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	data is a 32-bit value to write to the FIFO register.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_write_fifo_word(struct logii2s_port *port, u32 data)
{
	logii2s_write(port->base, LOGII2S_FIFO_ROFF, data);
}

/*
 * Function reads count of the data words from the FIFO register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	data is a pointer to 32-bit value data for reading from the FIFO
 *		register.
 * @param	count is number of data words.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_read_fifo(struct logii2s_port *port, u32 *data,
			    unsigned int count)
{
	int i;

	for (i = 0; i < count; i++)
		data[i] = logii2s_read(port->base, LOGII2S_FIFO_ROFF);
}

/*
 * Function writes count of the data words to the FIFO register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	data is a pointer to 32-bit value data for writing to the FIFO
 *		register.
 * @param	count is number of data words.
 *
 * @return	None.
 *
 * @note	None.
 */
void logii2s_port_write_fifo(struct logii2s_port *port, u32 *data,
			     unsigned int count)
{
	int i;

	for (i = 0; i < count; i++)
		logii2s_write(port->base, LOGII2S_FIFO_ROFF, data[i]);
}

/*
 * Function performs data transfer to or from FIFO register.
 *
 * @param	port is a pointer to the logii2s_port structure.
 * @param	data is a pointer to 32-bit value data for writing or reading the
 *		FIFO register.
 *
 * @return	Number of transfered bytes.
 *
 * @note	None.
 */
unsigned int logii2s_port_transfer_data(struct logii2s_port *port,
					u32 *data, unsigned int size)
{
	unsigned int direction = logii2s_port_direction(port);
	unsigned int samples;

	if (direction == LOGII2S_TX_INSTANCE) {
		if (logii2s_port_get_isr(port) & LOGII2S_INT_FE)
			samples = port->fifo_size;
		else if (logii2s_port_get_isr(port) & LOGII2S_INT_FAE)
			samples = port->fifo_size - port->almost_empty;
		else
			samples = 0;

		if (size != 0)
			if ((samples != 0) && (size < samples))
				samples = size;

		logii2s_port_write_fifo(port, data, samples);
		logii2s_port_clear_isr(port,
				       (LOGII2S_INT_FE | LOGII2S_INT_FAE));
	} else if (direction == LOGII2S_RX_INSTANCE) {
		if (logii2s_port_get_isr(port) & LOGII2S_INT_FF)
			samples = port->fifo_size;
		else if (logii2s_port_get_isr(port) & LOGII2S_INT_FAF)
			samples = port->almost_full;
		else
			samples = 0;

		if (size != 0)
			if ((samples != 0) && (size < samples))
				samples = size;

		logii2s_port_read_fifo(port, data, samples);
		logii2s_port_clear_isr(port,
				       (LOGII2S_INT_FF | LOGII2S_INT_FAF));
	} else {
		return 0;
	}

	return samples * 4;
}
