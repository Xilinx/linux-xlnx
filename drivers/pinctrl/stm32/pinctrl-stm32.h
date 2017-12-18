/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 */
#ifndef __PINCTRL_STM32_H
#define __PINCTRL_STM32_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>

#define STM32_PIN_NO(x) ((x) << 8)
#define STM32_GET_PIN_NO(x) ((x) >> 8)
#define STM32_GET_PIN_FUNC(x) ((x) & 0xff)

#define STM32_PIN_GPIO		0
#define STM32_PIN_AF(x)		((x) + 1)
#define STM32_PIN_ANALOG	(STM32_PIN_AF(15) + 1)

struct stm32_desc_function {
	const char *name;
	const unsigned char num;
};

struct stm32_desc_pin {
	struct pinctrl_pin_desc pin;
	const struct stm32_desc_function *functions;
};

#define STM32_PIN(_pin, ...)					\
	{							\
		.pin = _pin,					\
		.functions = (struct stm32_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define STM32_FUNCTION(_num, _name)		\
	{							\
		.num = _num,					\
		.name = _name,					\
	}

struct stm32_pinctrl_match_data {
	const struct stm32_desc_pin *pins;
	const unsigned int npins;
};

int stm32_pctl_probe(struct platform_device *pdev);

#endif /* __PINCTRL_STM32_H */

