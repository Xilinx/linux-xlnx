/*
 * include/asm-arm/mach/i2c-gpio.h
 *
 * Several platforms use GPIO pins to implement bit-bang I2C
 * controllers. This file defines a structure that can be passed
 * via the device model to provide the gpio pins to the I2C drivers.
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2003-2004 (c) MontaVista, Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */
#ifndef ASMARM_MACH_I2C_GPIO_H
#define ASMARM_MACH_I2C_GPIO_H

struct i2c_gpio_pins {
	unsigned long sda_pin;
	unsigned long scl_pin;
};

#endif
