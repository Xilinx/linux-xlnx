/*
 * Xilinx PSS GPIO Header File
 * arch/arm/mach-xilinx/gpio.h
 *
 * 2009 (c) Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_GPIO_H
#define __ASM_ARCH_GPIO_H


#define ARCH_NR_GPIOS		512
#define XGPIOPS_IRQBASE		128


extern int gpio_direction_input(unsigned gpio);
extern int gpio_direction_output(unsigned gpio, int value);
extern int __gpio_get_value(unsigned gpio);
extern void __gpio_set_value(unsigned gpio, int value);
extern int __gpio_cansleep(unsigned gpio);

static inline int gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

static inline int gpio_cansleep(unsigned int gpio)
{
	return __gpio_cansleep(gpio);
}

#include <asm-generic/gpio.h>

static inline unsigned int gpio_to_irq(unsigned int pin)
{
	return pin + XGPIOPS_IRQBASE;
}

static inline unsigned int irq_to_gpio(unsigned int irq)
{
	return irq - XGPIOPS_IRQBASE;
}

void xgpiodf_set_bypass_mode(struct gpio_chip *chip, unsigned int pin);
void xgpiodf_set_normal_mode(struct gpio_chip *chip, unsigned int pin);

#endif /* __ASM_ARCH_GPIO_H */
