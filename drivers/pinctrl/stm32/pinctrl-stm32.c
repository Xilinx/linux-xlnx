/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Heavily based on Mediatek's pinctrl driver
 */
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "pinctrl-stm32.h"

#define STM32_GPIO_MODER	0x00
#define STM32_GPIO_TYPER	0x04
#define STM32_GPIO_SPEEDR	0x08
#define STM32_GPIO_PUPDR	0x0c
#define STM32_GPIO_IDR		0x10
#define STM32_GPIO_ODR		0x14
#define STM32_GPIO_BSRR		0x18
#define STM32_GPIO_LCKR		0x1c
#define STM32_GPIO_AFRL		0x20
#define STM32_GPIO_AFRH		0x24

#define STM32_GPIO_PINS_PER_BANK 16
#define STM32_GPIO_IRQ_LINE	 16

#define gpio_range_to_bank(chip) \
		container_of(chip, struct stm32_gpio_bank, range)

static const char * const stm32_gpio_functions[] = {
	"gpio", "af0", "af1",
	"af2", "af3", "af4",
	"af5", "af6", "af7",
	"af8", "af9", "af10",
	"af11", "af12", "af13",
	"af14", "af15", "analog",
};

struct stm32_pinctrl_group {
	const char *name;
	unsigned long config;
	unsigned pin;
};

struct stm32_gpio_bank {
	void __iomem *base;
	struct clk *clk;
	spinlock_t lock;
	struct gpio_chip gpio_chip;
	struct pinctrl_gpio_range range;
	struct fwnode_handle *fwnode;
	struct irq_domain *domain;
};

struct stm32_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_desc pctl_desc;
	struct stm32_pinctrl_group *groups;
	unsigned ngroups;
	const char **grp_names;
	struct stm32_gpio_bank *banks;
	unsigned nbanks;
	const struct stm32_pinctrl_match_data *match_data;
	struct irq_domain	*domain;
	struct regmap		*regmap;
	struct regmap_field	*irqmux[STM32_GPIO_PINS_PER_BANK];
};

static inline int stm32_gpio_pin(int gpio)
{
	return gpio % STM32_GPIO_PINS_PER_BANK;
}

static inline u32 stm32_gpio_get_mode(u32 function)
{
	switch (function) {
	case STM32_PIN_GPIO:
		return 0;
	case STM32_PIN_AF(0) ... STM32_PIN_AF(15):
		return 2;
	case STM32_PIN_ANALOG:
		return 3;
	}

	return 0;
}

static inline u32 stm32_gpio_get_alt(u32 function)
{
	switch (function) {
	case STM32_PIN_GPIO:
		return 0;
	case STM32_PIN_AF(0) ... STM32_PIN_AF(15):
		return function - 1;
	case STM32_PIN_ANALOG:
		return 0;
	}

	return 0;
}

/* GPIO functions */

static inline void __stm32_gpio_set(struct stm32_gpio_bank *bank,
	unsigned offset, int value)
{
	if (!value)
		offset += STM32_GPIO_PINS_PER_BANK;

	clk_enable(bank->clk);

	writel_relaxed(BIT(offset), bank->base + STM32_GPIO_BSRR);

	clk_disable(bank->clk);
}

static int stm32_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void stm32_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int stm32_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	int ret;

	clk_enable(bank->clk);

	ret = !!(readl_relaxed(bank->base + STM32_GPIO_IDR) & BIT(offset));

	clk_disable(bank->clk);

	return ret;
}

static void stm32_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);

	__stm32_gpio_set(bank, offset, value);
}

static int stm32_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int stm32_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);

	__stm32_gpio_set(bank, offset, value);
	pinctrl_gpio_direction_output(chip->base + offset);

	return 0;
}


static int stm32_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	struct irq_fwspec fwspec;

	fwspec.fwnode = bank->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = offset;
	fwspec.param[1] = IRQ_TYPE_NONE;

	return irq_create_fwspec_mapping(&fwspec);
}

static const struct gpio_chip stm32_gpio_template = {
	.request		= stm32_gpio_request,
	.free			= stm32_gpio_free,
	.get			= stm32_gpio_get,
	.set			= stm32_gpio_set,
	.direction_input	= stm32_gpio_direction_input,
	.direction_output	= stm32_gpio_direction_output,
	.to_irq			= stm32_gpio_to_irq,
};

static struct irq_chip stm32_gpio_irq_chip = {
	.name           = "stm32gpio",
	.irq_eoi	= irq_chip_eoi_parent,
	.irq_mask       = irq_chip_mask_parent,
	.irq_unmask     = irq_chip_unmask_parent,
	.irq_set_type   = irq_chip_set_type_parent,
};

static int stm32_gpio_domain_translate(struct irq_domain *d,
				       struct irq_fwspec *fwspec,
				       unsigned long *hwirq,
				       unsigned int *type)
{
	if ((fwspec->param_count != 2) ||
	    (fwspec->param[0] >= STM32_GPIO_IRQ_LINE))
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];
	return 0;
}

static void stm32_gpio_domain_activate(struct irq_domain *d,
				       struct irq_data *irq_data)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);

	regmap_field_write(pctl->irqmux[irq_data->hwirq], bank->range.id);
}

static int stm32_gpio_domain_alloc(struct irq_domain *d,
				   unsigned int virq,
				   unsigned int nr_irqs, void *data)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t hwirq;
	int ret;

	hwirq = fwspec->param[0];
	parent_fwspec.fwnode = d->parent->fwnode;
	parent_fwspec.param_count = 2;
	parent_fwspec.param[0] = fwspec->param[0];
	parent_fwspec.param[1] = fwspec->param[1];

	irq_domain_set_hwirq_and_chip(d, virq, hwirq, &stm32_gpio_irq_chip,
				      bank);

	ret = gpiochip_lock_as_irq(&bank->gpio_chip, hwirq);
	if (ret) {
		dev_err(pctl->dev, "Unable to configure STM32 %s%ld as IRQ\n",
			bank->gpio_chip.label, hwirq);
		return ret;
	}

	ret = irq_domain_alloc_irqs_parent(d, virq, nr_irqs, &parent_fwspec);
	if (ret)
		gpiochip_unlock_as_irq(&bank->gpio_chip, hwirq);

	return ret;
}

static void stm32_gpio_domain_free(struct irq_domain *d, unsigned int virq,
				   unsigned int nr_irqs)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct irq_data *data = irq_get_irq_data(virq);

	irq_domain_free_irqs_common(d, virq, nr_irqs);
	gpiochip_unlock_as_irq(&bank->gpio_chip, data->hwirq);
}

static const struct irq_domain_ops stm32_gpio_domain_ops = {
	.translate      = stm32_gpio_domain_translate,
	.alloc          = stm32_gpio_domain_alloc,
	.free           = stm32_gpio_domain_free,
	.activate	= stm32_gpio_domain_activate,
};

/* Pinctrl functions */
static struct stm32_pinctrl_group *
stm32_pctrl_find_group_by_pin(struct stm32_pinctrl *pctl, u32 pin)
{
	int i;

	for (i = 0; i < pctl->ngroups; i++) {
		struct stm32_pinctrl_group *grp = pctl->groups + i;

		if (grp->pin == pin)
			return grp;
	}

	return NULL;
}

static bool stm32_pctrl_is_function_valid(struct stm32_pinctrl *pctl,
		u32 pin_num, u32 fnum)
{
	int i;

	for (i = 0; i < pctl->match_data->npins; i++) {
		const struct stm32_desc_pin *pin = pctl->match_data->pins + i;
		const struct stm32_desc_function *func = pin->functions;

		if (pin->pin.number != pin_num)
			continue;

		while (func && func->name) {
			if (func->num == fnum)
				return true;
			func++;
		}

		break;
	}

	return false;
}

static int stm32_pctrl_dt_node_to_map_func(struct stm32_pinctrl *pctl,
		u32 pin, u32 fnum, struct stm32_pinctrl_group *grp,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps)
{
	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = grp->name;

	if (!stm32_pctrl_is_function_valid(pctl, pin, fnum)) {
		dev_err(pctl->dev, "invalid function %d on pin %d .\n",
				fnum, pin);
		return -EINVAL;
	}

	(*map)[*num_maps].data.mux.function = stm32_gpio_functions[fnum];
	(*num_maps)++;

	return 0;
}

static int stm32_pctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				      struct device_node *node,
				      struct pinctrl_map **map,
				      unsigned *reserved_maps,
				      unsigned *num_maps)
{
	struct stm32_pinctrl *pctl;
	struct stm32_pinctrl_group *grp;
	struct property *pins;
	u32 pinfunc, pin, func;
	unsigned long *configs;
	unsigned int num_configs;
	bool has_config = 0;
	unsigned reserve = 0;
	int num_pins, num_funcs, maps_per_pin, i, err;

	pctl = pinctrl_dev_get_drvdata(pctldev);

	pins = of_find_property(node, "pinmux", NULL);
	if (!pins) {
		dev_err(pctl->dev, "missing pins property in node %s .\n",
				node->name);
		return -EINVAL;
	}

	err = pinconf_generic_parse_dt_config(node, pctldev, &configs,
		&num_configs);
	if (err)
		return err;

	if (num_configs)
		has_config = 1;

	num_pins = pins->length / sizeof(u32);
	num_funcs = num_pins;
	maps_per_pin = 0;
	if (num_funcs)
		maps_per_pin++;
	if (has_config && num_pins >= 1)
		maps_per_pin++;

	if (!num_pins || !maps_per_pin)
		return -EINVAL;

	reserve = num_pins * maps_per_pin;

	err = pinctrl_utils_reserve_map(pctldev, map,
			reserved_maps, num_maps, reserve);
	if (err)
		return err;

	for (i = 0; i < num_pins; i++) {
		err = of_property_read_u32_index(node, "pinmux",
				i, &pinfunc);
		if (err)
			return err;

		pin = STM32_GET_PIN_NO(pinfunc);
		func = STM32_GET_PIN_FUNC(pinfunc);

		if (pin >= pctl->match_data->npins) {
			dev_err(pctl->dev, "invalid pin number.\n");
			return -EINVAL;
		}

		if (!stm32_pctrl_is_function_valid(pctl, pin, func)) {
			dev_err(pctl->dev, "invalid function.\n");
			return -EINVAL;
		}

		grp = stm32_pctrl_find_group_by_pin(pctl, pin);
		if (!grp) {
			dev_err(pctl->dev, "unable to match pin %d to group\n",
					pin);
			return -EINVAL;
		}

		err = stm32_pctrl_dt_node_to_map_func(pctl, pin, func, grp, map,
				reserved_maps, num_maps);
		if (err)
			return err;

		if (has_config) {
			err = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, grp->name,
					configs, num_configs,
					PIN_MAP_TYPE_CONFIGS_GROUP);
			if (err)
				return err;
		}
	}

	return 0;
}

static int stm32_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct device_node *np;
	unsigned reserved_maps;
	int ret;

	*map = NULL;
	*num_maps = 0;
	reserved_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = stm32_pctrl_dt_subnode_to_map(pctldev, np, map,
				&reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

static int stm32_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *stm32_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned group)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int stm32_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned group,
				      const unsigned **pins,
				      unsigned *num_pins)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = (unsigned *)&pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops stm32_pctrl_ops = {
	.dt_node_to_map		= stm32_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
	.get_groups_count	= stm32_pctrl_get_groups_count,
	.get_group_name		= stm32_pctrl_get_group_name,
	.get_group_pins		= stm32_pctrl_get_group_pins,
};


/* Pinmux functions */

static int stm32_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(stm32_gpio_functions);
}

static const char *stm32_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned selector)
{
	return stm32_gpio_functions[selector];
}

static int stm32_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned function,
				     const char * const **groups,
				     unsigned * const num_groups)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->grp_names;
	*num_groups = pctl->ngroups;

	return 0;
}

static void stm32_pmx_set_mode(struct stm32_gpio_bank *bank,
		int pin, u32 mode, u32 alt)
{
	u32 val;
	int alt_shift = (pin % 8) * 4;
	int alt_offset = STM32_GPIO_AFRL + (pin / 8) * 4;
	unsigned long flags;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + alt_offset);
	val &= ~GENMASK(alt_shift + 3, alt_shift);
	val |= (alt << alt_shift);
	writel_relaxed(val, bank->base + alt_offset);

	val = readl_relaxed(bank->base + STM32_GPIO_MODER);
	val &= ~GENMASK(pin * 2 + 1, pin * 2);
	val |= mode << (pin * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_MODER);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static void stm32_pmx_get_mode(struct stm32_gpio_bank *bank,
		int pin, u32 *mode, u32 *alt)
{
	u32 val;
	int alt_shift = (pin % 8) * 4;
	int alt_offset = STM32_GPIO_AFRL + (pin / 8) * 4;
	unsigned long flags;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + alt_offset);
	val &= GENMASK(alt_shift + 3, alt_shift);
	*alt = val >> alt_shift;

	val = readl_relaxed(bank->base + STM32_GPIO_MODER);
	val &= GENMASK(pin * 2 + 1, pin * 2);
	*mode = val >> (pin * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static int stm32_pmx_set_mux(struct pinctrl_dev *pctldev,
			    unsigned function,
			    unsigned group)
{
	bool ret;
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct stm32_pinctrl_group *g = pctl->groups + group;
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	u32 mode, alt;
	int pin;

	ret = stm32_pctrl_is_function_valid(pctl, g->pin, function);
	if (!ret) {
		dev_err(pctl->dev, "invalid function %d on group %d .\n",
				function, group);
		return -EINVAL;
	}

	range = pinctrl_find_gpio_range_from_pin(pctldev, g->pin);
	bank = gpio_range_to_bank(range);
	pin = stm32_gpio_pin(g->pin);

	mode = stm32_gpio_get_mode(function);
	alt = stm32_gpio_get_alt(function);

	stm32_pmx_set_mode(bank, pin, mode, alt);

	return 0;
}

static int stm32_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned gpio,
			bool input)
{
	struct stm32_gpio_bank *bank = gpio_range_to_bank(range);
	int pin = stm32_gpio_pin(gpio);

	stm32_pmx_set_mode(bank, pin, !input, 0);

	return 0;
}

static const struct pinmux_ops stm32_pmx_ops = {
	.get_functions_count	= stm32_pmx_get_funcs_cnt,
	.get_function_name	= stm32_pmx_get_func_name,
	.get_function_groups	= stm32_pmx_get_func_groups,
	.set_mux		= stm32_pmx_set_mux,
	.gpio_set_direction	= stm32_pmx_gpio_set_direction,
};

/* Pinconf functions */

static void stm32_pconf_set_driving(struct stm32_gpio_bank *bank,
	unsigned offset, u32 drive)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_TYPER);
	val &= ~BIT(offset);
	val |= drive << offset;
	writel_relaxed(val, bank->base + STM32_GPIO_TYPER);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static u32 stm32_pconf_get_driving(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_TYPER);
	val &= BIT(offset);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> offset);
}

static void stm32_pconf_set_speed(struct stm32_gpio_bank *bank,
	unsigned offset, u32 speed)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_SPEEDR);
	val &= ~GENMASK(offset * 2 + 1, offset * 2);
	val |= speed << (offset * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_SPEEDR);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static u32 stm32_pconf_get_speed(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_SPEEDR);
	val &= GENMASK(offset * 2 + 1, offset * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> (offset * 2));
}

static void stm32_pconf_set_bias(struct stm32_gpio_bank *bank,
	unsigned offset, u32 bias)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_PUPDR);
	val &= ~GENMASK(offset * 2 + 1, offset * 2);
	val |= bias << (offset * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_PUPDR);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static u32 stm32_pconf_get_bias(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_PUPDR);
	val &= GENMASK(offset * 2 + 1, offset * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> (offset * 2));
}

static bool stm32_pconf_get(struct stm32_gpio_bank *bank,
	unsigned int offset, bool dir)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (dir)
		val = !!(readl_relaxed(bank->base + STM32_GPIO_IDR) &
			 BIT(offset));
	else
		val = !!(readl_relaxed(bank->base + STM32_GPIO_ODR) &
			 BIT(offset));

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return val;
}

static int stm32_pconf_parse_conf(struct pinctrl_dev *pctldev,
		unsigned int pin, enum pin_config_param param,
		enum pin_config_param arg)
{
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	int offset, ret = 0;

	range = pinctrl_find_gpio_range_from_pin(pctldev, pin);
	bank = gpio_range_to_bank(range);
	offset = stm32_gpio_pin(pin);

	switch (param) {
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		stm32_pconf_set_driving(bank, offset, 0);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		stm32_pconf_set_driving(bank, offset, 1);
		break;
	case PIN_CONFIG_SLEW_RATE:
		stm32_pconf_set_speed(bank, offset, arg);
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		stm32_pconf_set_bias(bank, offset, 0);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		stm32_pconf_set_bias(bank, offset, 1);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		stm32_pconf_set_bias(bank, offset, 2);
		break;
	case PIN_CONFIG_OUTPUT:
		__stm32_gpio_set(bank, offset, arg);
		ret = stm32_pmx_gpio_set_direction(pctldev, NULL, pin, false);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int stm32_pconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *config)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*config = pctl->groups[group].config;

	return 0;
}

static int stm32_pconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				 unsigned long *configs, unsigned num_configs)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct stm32_pinctrl_group *g = &pctl->groups[group];
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		ret = stm32_pconf_parse_conf(pctldev, g->pin,
			pinconf_to_config_param(configs[i]),
			pinconf_to_config_argument(configs[i]));
		if (ret < 0)
			return ret;

		g->config = configs[i];
	}

	return 0;
}

static void stm32_pconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s,
				 unsigned int pin)
{
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	int offset;
	u32 mode, alt, drive, speed, bias;
	static const char * const modes[] = {
			"input", "output", "alternate", "analog" };
	static const char * const speeds[] = {
			"low", "medium", "high", "very high" };
	static const char * const biasing[] = {
			"floating", "pull up", "pull down", "" };
	bool val;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	bank = gpio_range_to_bank(range);
	offset = stm32_gpio_pin(pin);

	stm32_pmx_get_mode(bank, offset, &mode, &alt);
	bias = stm32_pconf_get_bias(bank, offset);

	seq_printf(s, "%s ", modes[mode]);

	switch (mode) {
	/* input */
	case 0:
		val = stm32_pconf_get(bank, offset, true);
		seq_printf(s, "- %s - %s",
			   val ? "high" : "low",
			   biasing[bias]);
		break;

	/* output */
	case 1:
		drive = stm32_pconf_get_driving(bank, offset);
		speed = stm32_pconf_get_speed(bank, offset);
		val = stm32_pconf_get(bank, offset, false);
		seq_printf(s, "- %s - %s - %s - %s %s",
			   val ? "high" : "low",
			   drive ? "open drain" : "push pull",
			   biasing[bias],
			   speeds[speed], "speed");
		break;

	/* alternate */
	case 2:
		drive = stm32_pconf_get_driving(bank, offset);
		speed = stm32_pconf_get_speed(bank, offset);
		seq_printf(s, "%d - %s - %s - %s %s", alt,
			   drive ? "open drain" : "push pull",
			   biasing[bias],
			   speeds[speed], "speed");
		break;

	/* analog */
	case 3:
		break;
	}
}


static const struct pinconf_ops stm32_pconf_ops = {
	.pin_config_group_get	= stm32_pconf_group_get,
	.pin_config_group_set	= stm32_pconf_group_set,
	.pin_config_dbg_show	= stm32_pconf_dbg_show,
};

static int stm32_gpiolib_register_bank(struct stm32_pinctrl *pctl,
	struct device_node *np)
{
	int bank_nr = pctl->nbanks;
	struct stm32_gpio_bank *bank = &pctl->banks[bank_nr];
	struct pinctrl_gpio_range *range = &bank->range;
	struct device *dev = pctl->dev;
	struct resource res;
	struct reset_control *rstc;
	int err, npins;

	rstc = of_reset_control_get(np, NULL);
	if (!IS_ERR(rstc))
		reset_control_deassert(rstc);

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;

	bank->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(bank->base))
		return PTR_ERR(bank->base);

	bank->clk = of_clk_get_by_name(np, NULL);
	if (IS_ERR(bank->clk)) {
		dev_err(dev, "failed to get clk (%ld)\n", PTR_ERR(bank->clk));
		return PTR_ERR(bank->clk);
	}

	err = clk_prepare(bank->clk);
	if (err) {
		dev_err(dev, "failed to prepare clk (%d)\n", err);
		return err;
	}

	npins = pctl->match_data->npins;
	npins -= bank_nr * STM32_GPIO_PINS_PER_BANK;
	if (npins < 0)
		return -EINVAL;
	else if (npins > STM32_GPIO_PINS_PER_BANK)
		npins = STM32_GPIO_PINS_PER_BANK;

	bank->gpio_chip = stm32_gpio_template;
	bank->gpio_chip.base = bank_nr * STM32_GPIO_PINS_PER_BANK;
	bank->gpio_chip.ngpio = npins;
	bank->gpio_chip.of_node = np;
	bank->gpio_chip.parent = dev;
	spin_lock_init(&bank->lock);

	of_property_read_string(np, "st,bank-name", &range->name);
	bank->gpio_chip.label = range->name;

	range->id = bank_nr;
	range->pin_base = range->base = range->id * STM32_GPIO_PINS_PER_BANK;
	range->npins = bank->gpio_chip.ngpio;
	range->gc = &bank->gpio_chip;

	/* create irq hierarchical domain */
	bank->fwnode = of_node_to_fwnode(np);

	bank->domain = irq_domain_create_hierarchy(pctl->domain, 0,
					STM32_GPIO_IRQ_LINE, bank->fwnode,
					&stm32_gpio_domain_ops, bank);

	if (!bank->domain)
		return -ENODEV;

	err = gpiochip_add_data(&bank->gpio_chip, bank);
	if (err) {
		dev_err(dev, "Failed to add gpiochip(%d)!\n", bank_nr);
		return err;
	}

	dev_info(dev, "%s bank added\n", range->name);
	return 0;
}

static int stm32_pctrl_dt_setup_irq(struct platform_device *pdev,
			   struct stm32_pinctrl *pctl)
{
	struct device_node *np = pdev->dev.of_node, *parent;
	struct device *dev = &pdev->dev;
	struct regmap *rm;
	int offset, ret, i;

	parent = of_irq_find_parent(np);
	if (!parent)
		return -ENXIO;

	pctl->domain = irq_find_host(parent);
	if (!pctl->domain)
		return -ENXIO;

	pctl->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(pctl->regmap))
		return PTR_ERR(pctl->regmap);

	rm = pctl->regmap;

	ret = of_property_read_u32_index(np, "st,syscfg", 1, &offset);
	if (ret)
		return ret;

	for (i = 0; i < STM32_GPIO_PINS_PER_BANK; i++) {
		struct reg_field mux;

		mux.reg = offset + (i / 4) * 4;
		mux.lsb = (i % 4) * 4;
		mux.msb = mux.lsb + 3;

		pctl->irqmux[i] = devm_regmap_field_alloc(dev, rm, mux);
		if (IS_ERR(pctl->irqmux[i]))
			return PTR_ERR(pctl->irqmux[i]);
	}

	return 0;
}

static int stm32_pctrl_build_state(struct platform_device *pdev)
{
	struct stm32_pinctrl *pctl = platform_get_drvdata(pdev);
	int i;

	pctl->ngroups = pctl->match_data->npins;

	/* Allocate groups */
	pctl->groups = devm_kcalloc(&pdev->dev, pctl->ngroups,
				    sizeof(*pctl->groups), GFP_KERNEL);
	if (!pctl->groups)
		return -ENOMEM;

	/* We assume that one pin is one group, use pin name as group name. */
	pctl->grp_names = devm_kcalloc(&pdev->dev, pctl->ngroups,
				       sizeof(*pctl->grp_names), GFP_KERNEL);
	if (!pctl->grp_names)
		return -ENOMEM;

	for (i = 0; i < pctl->match_data->npins; i++) {
		const struct stm32_desc_pin *pin = pctl->match_data->pins + i;
		struct stm32_pinctrl_group *group = pctl->groups + i;

		group->name = pin->pin.name;
		group->pin = pin->pin.number;

		pctl->grp_names[i] = pin->pin.name;
	}

	return 0;
}

int stm32_pctl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct stm32_pinctrl *pctl;
	struct pinctrl_pin_desc *pins;
	int i, ret, banks = 0;

	if (!np)
		return -EINVAL;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	if (!of_find_property(np, "pins-are-numbered", NULL)) {
		dev_err(dev, "only support pins-are-numbered format\n");
		return -EINVAL;
	}

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctl);

	pctl->dev = dev;
	pctl->match_data = match->data;
	ret = stm32_pctrl_build_state(pdev);
	if (ret) {
		dev_err(dev, "build state failed: %d\n", ret);
		return -EINVAL;
	}

	if (of_find_property(np, "interrupt-parent", NULL)) {
		ret = stm32_pctrl_dt_setup_irq(pdev, pctl);
		if (ret)
			return ret;
	}

	for_each_child_of_node(np, child)
		if (of_property_read_bool(child, "gpio-controller"))
			banks++;

	if (!banks) {
		dev_err(dev, "at least one GPIO bank is required\n");
		return -EINVAL;
	}

	pctl->banks = devm_kcalloc(dev, banks, sizeof(*pctl->banks),
			GFP_KERNEL);
	if (!pctl->banks)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		if (of_property_read_bool(child, "gpio-controller")) {
			ret = stm32_gpiolib_register_bank(pctl, child);
			if (ret)
				return ret;

			pctl->nbanks++;
		}
	}

	pins = devm_kcalloc(&pdev->dev, pctl->match_data->npins, sizeof(*pins),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < pctl->match_data->npins; i++)
		pins[i] = pctl->match_data->pins[i].pin;

	pctl->pctl_desc.name = dev_name(&pdev->dev);
	pctl->pctl_desc.owner = THIS_MODULE;
	pctl->pctl_desc.pins = pins;
	pctl->pctl_desc.npins = pctl->match_data->npins;
	pctl->pctl_desc.confops = &stm32_pconf_ops;
	pctl->pctl_desc.pctlops = &stm32_pctrl_ops;
	pctl->pctl_desc.pmxops = &stm32_pmx_ops;
	pctl->dev = &pdev->dev;

	pctl->pctl_dev = devm_pinctrl_register(&pdev->dev, &pctl->pctl_desc,
					       pctl);
	if (IS_ERR(pctl->pctl_dev)) {
		dev_err(&pdev->dev, "Failed pinctrl registration\n");
		return PTR_ERR(pctl->pctl_dev);
	}

	for (i = 0; i < pctl->nbanks; i++)
		pinctrl_add_gpio_range(pctl->pctl_dev, &pctl->banks[i].range);

	dev_info(dev, "Pinctrl STM32 initialized\n");

	return 0;
}

