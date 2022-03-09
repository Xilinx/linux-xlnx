// SPDX-License-Identifier: GPL-2.0
/*
 * Driver to configure cadence TTC timer as PWM
 * generator
 *
 * Copyright (c) 2021 Xilinx, Inc.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/of_address.h>

#define TTC_CLK_CNTRL_OFFSET		0x00
#define TTC_CNT_CNTRL_OFFSET		0x0C
#define TTC_MATCH_CNT_VAL_OFFSET	0x30
#define TTC_COUNT_VAL_OFFSET		0x18
#define TTC_INTR_VAL_OFFSET		0x24
#define TTC_ISR_OFFSET			0x54
#define TTC_IER_OFFSET			0x60
#define TTC_PWM_CHANNEL_OFFSET		0x4

#define TTC_CLK_CNTRL_CSRC_MASK		BIT(5)
#define TTC_CLK_CNTRL_PSV_MASK		GENMASK(4, 1)

#define TTC_CNTR_CTRL_DIS_MASK		BIT(0)
#define TTC_CNTR_CTRL_INTR_MODE_EN_MASK	BIT(1)
#define TTC_CNTR_CTRL_MATCH_MODE_EN_MASK	BIT(3)
#define TTC_CNTR_CTRL_RST_MASK		BIT(4)
#define TTC_CNTR_CTRL_WAVE_EN_MASK	BIT(5)
#define TTC_CNTR_CTRL_WAVE_POL_MASK	BIT(6)

#define TTC_CLK_CNTRL_PSV_SHIFT		1

#define TTC_PWM_MAX_CH			3

/**
 * struct ttc_pwm_priv - Private data for TTC PWM drivers
 * @chip:	PWM chip structure representing PWM controller
 * @clk:	TTC input clock
 * @max:	Maximum value of the counters
 * @base:	Base address of TTC instance
 */
struct ttc_pwm_priv {
	struct pwm_chip chip;
	struct clk *clk;
	u32 max;
	void __iomem *base;
};

static inline u32 ttc_pwm_readl(struct ttc_pwm_priv *priv,
				unsigned long offset)
{
	return readl_relaxed(priv->base + offset);
}

static inline void ttc_pwm_writel(struct ttc_pwm_priv *priv,
				  unsigned long offset,
				  unsigned long val)
{
	writel_relaxed(val, priv->base + offset);
}

static inline u32 ttc_pwm_ch_readl(struct ttc_pwm_priv *priv,
				   struct pwm_device *pwm,
				   unsigned long offset)
{
	unsigned long pwm_ch_offset = offset +
				       (TTC_PWM_CHANNEL_OFFSET * pwm->hwpwm);

	return ttc_pwm_readl(priv, pwm_ch_offset);
}

static inline void ttc_pwm_ch_writel(struct ttc_pwm_priv *priv,
				     struct pwm_device *pwm,
				     unsigned long offset,
				     unsigned long val)
{
	unsigned long pwm_ch_offset = offset +
				       (TTC_PWM_CHANNEL_OFFSET * pwm->hwpwm);

	ttc_pwm_writel(priv, pwm_ch_offset, val);
}

static inline struct ttc_pwm_priv *xilinx_pwm_chip_to_priv(struct pwm_chip *chip)
{
	return container_of(chip, struct ttc_pwm_priv, chip);
}

static void ttc_pwm_enable(struct ttc_pwm_priv *priv, struct pwm_device *pwm)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm, TTC_CNT_CNTRL_OFFSET);
	ctrl_reg |= (TTC_CNTR_CTRL_INTR_MODE_EN_MASK
				 | TTC_CNTR_CTRL_MATCH_MODE_EN_MASK | TTC_CNTR_CTRL_RST_MASK);
	ctrl_reg &= (~(TTC_CNTR_CTRL_DIS_MASK | TTC_CNTR_CTRL_WAVE_EN_MASK));
	ttc_pwm_ch_writel(priv, pwm, TTC_CNT_CNTRL_OFFSET, ctrl_reg);
}

static void ttc_pwm_disable(struct ttc_pwm_priv *priv, struct pwm_device *pwm)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm, TTC_CNT_CNTRL_OFFSET);
	ctrl_reg |= TTC_CNTR_CTRL_DIS_MASK;

	ttc_pwm_ch_writel(priv, pwm, TTC_CNT_CNTRL_OFFSET, ctrl_reg);
}

static void ttc_pwm_rev_polarity(struct ttc_pwm_priv *priv, struct pwm_device *pwm)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm, TTC_CNT_CNTRL_OFFSET);
	ctrl_reg ^= TTC_CNTR_CTRL_WAVE_POL_MASK;
	ttc_pwm_ch_writel(priv, pwm, TTC_CNT_CNTRL_OFFSET, ctrl_reg);
}

static void ttc_pwm_set_counters(struct ttc_pwm_priv *priv,
				 struct pwm_device *pwm,
				 u32 div,
				 u32 period_cycles,
				 u32 duty_cycles)
{
	u32 clk_reg;

	/* Set up prescalar */
	clk_reg = ttc_pwm_ch_readl(priv, pwm, TTC_CLK_CNTRL_OFFSET);
	clk_reg &= ~TTC_CLK_CNTRL_PSV_MASK;
	clk_reg |= div;
	ttc_pwm_ch_writel(priv, pwm, TTC_CLK_CNTRL_OFFSET, clk_reg);

	/* Set up period */
	ttc_pwm_ch_writel(priv, pwm, TTC_INTR_VAL_OFFSET, period_cycles);

	/* Set up duty cycle */
	ttc_pwm_ch_writel(priv, pwm, TTC_MATCH_CNT_VAL_OFFSET, duty_cycles);
}

static int ttc_pwm_apply(struct pwm_chip *chip,
			 struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	u32 div = 0;
	u64 period_cycles;
	u64 duty_cycles;
	unsigned long rate;
	struct pwm_state cstate;
	struct ttc_pwm_priv *priv = xilinx_pwm_chip_to_priv(chip);

	pwm_get_state(pwm, &cstate);

	if (state->polarity != cstate.polarity) {
		if (cstate.enabled)
			ttc_pwm_disable(priv, pwm);

		ttc_pwm_rev_polarity(priv, pwm);

		if (cstate.enabled)
			ttc_pwm_enable(priv, pwm);
	}

	if (state->period != cstate.period ||
	    state->duty_cycle != cstate.duty_cycle) {
		rate = clk_get_rate(priv->clk);

		/* Prevent overflow by limiting to the maximum possible period */
		period_cycles = min_t(u64, state->period, ULONG_MAX * NSEC_PER_SEC);
		period_cycles = mul_u64_u64_div_u64(period_cycles, rate, NSEC_PER_SEC);

		if (period_cycles > priv->max) {
			/* prescale frequency to fit requested period cycles within limit */
			div = 1;

			while ((period_cycles  > priv->max) && (div < 65536)) {
				rate = DIV_ROUND_CLOSEST(rate, BIT(div));
				period_cycles = mul_u64_u32_div(state->period,
								rate, NSEC_PER_SEC);
				if (period_cycles < priv->max)
					break;
				div++;
			}

			if (period_cycles  > priv->max)
				return -ERANGE;
		}

		duty_cycles = mul_u64_u32_div(state->duty_cycle,
					      rate, NSEC_PER_SEC);

		if (cstate.enabled)
			ttc_pwm_disable(priv, pwm);

		ttc_pwm_set_counters(priv, pwm, div, period_cycles, duty_cycles);

		if (cstate.enabled)
			ttc_pwm_enable(priv, pwm);
	}

	if (state->enabled != cstate.enabled) {
		if (state->enabled)
			ttc_pwm_enable(priv, pwm);
		else
			ttc_pwm_disable(priv, pwm);
	}

	return 0;
}

static void ttc_pwm_get_state(struct pwm_chip *chip,
			      struct pwm_device *pwm,
			      struct pwm_state *state)
{
	struct ttc_pwm_priv *priv = xilinx_pwm_chip_to_priv(chip);
	unsigned long rate;
	u32 value;
	u64 tmp;

	value = ttc_pwm_ch_readl(priv, pwm, TTC_CNT_CNTRL_OFFSET);

	if (value & TTC_CNTR_CTRL_WAVE_POL_MASK)
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	if (value & TTC_CNTR_CTRL_DIS_MASK)
		state->enabled = false;
	else
		state->enabled = true;

	rate = clk_get_rate(priv->clk);

	tmp = ttc_pwm_ch_readl(priv, pwm, TTC_INTR_VAL_OFFSET);
	state->period = DIV_ROUND_CLOSEST_ULL(tmp, rate);

	tmp = ttc_pwm_ch_readl(priv, pwm, TTC_MATCH_CNT_VAL_OFFSET);
	state->duty_cycle = DIV_ROUND_CLOSEST_ULL(tmp, rate);
}

static struct pwm_device *
ttc_pwm_of_xlate(struct pwm_chip *chip, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	if (args->args[0] >= TTC_PWM_MAX_CH)
		return NULL;

	pwm = pwm_request_from_chip(chip, args->args[0], NULL);
	if (IS_ERR(pwm))
		return pwm;

	if (args->args[1])
		pwm->args.period = args->args[1];

	if (args->args[2])
		pwm->args.polarity = args->args[2];

	return pwm;
}

static const struct pwm_ops ttc_pwm_ops = {
	.apply = ttc_pwm_apply,
	.get_state = ttc_pwm_get_state,
	.owner = THIS_MODULE,
};

static int ttc_pwm_probe(struct platform_device *pdev)
{
	int ret;
	int clksel;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ttc_pwm_priv *priv;
	u32 pwm_cells;
	u32 timer_width;
	struct clk *clk_cs;

	ret = of_property_read_u32(np, "#pwm-cells", &pwm_cells);
	if (ret == -EINVAL)
		return -ENODEV;

	if (ret)
		return dev_err_probe(dev, ret, "could not read #pwm-cells\n");

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	ret = of_property_read_u32(np, "timer-width", &timer_width);
	if (ret)
		timer_width = 16;

	priv->max = BIT(timer_width) - 1;

	clksel = ttc_pwm_readl(priv, TTC_CLK_CNTRL_OFFSET);
	clksel = !!(clksel & TTC_CLK_CNTRL_CSRC_MASK);
	clk_cs = of_clk_get(np, clksel);
	if (IS_ERR(clk_cs))
		return dev_err_probe(dev, PTR_ERR(clk_cs),
				     "ERROR: timer input clock not found\n");

	priv->clk = clk_cs;
	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return dev_err_probe(dev, ret, "Clock enable failed\n");

	clk_rate_exclusive_get(priv->clk);

	priv->chip.dev = dev;
	priv->chip.ops = &ttc_pwm_ops;
	priv->chip.npwm = TTC_PWM_MAX_CH;
	priv->chip.of_xlate = ttc_pwm_of_xlate;
	ret = pwmchip_add(&priv->chip);
	if (ret) {
		clk_rate_exclusive_put(priv->clk);
		clk_disable_unprepare(priv->clk);
		return dev_err_probe(dev, ret, "Could not register PWM chip\n");
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int ttc_pwm_remove(struct platform_device *pdev)
{
	struct ttc_pwm_priv *priv = platform_get_drvdata(pdev);

	pwmchip_remove(&priv->chip);
	clk_rate_exclusive_put(priv->clk);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id ttc_pwm_of_match[] = {
	{ .compatible = "cdns,ttc"},
	{},
};
MODULE_DEVICE_TABLE(of, ttc_pwm_of_match);

static struct platform_driver ttc_pwm_driver = {
	.probe = ttc_pwm_probe,
	.remove = ttc_pwm_remove,
	.driver = {
		.name = "ttc-pwm",
		.of_match_table = of_match_ptr(ttc_pwm_of_match),
	},
};
module_platform_driver(ttc_pwm_driver);

MODULE_AUTHOR("Mubin Usman Sayyed <mubin.usman.sayyed@xilinx.com>");
MODULE_DESCRIPTION("Cadence TTC PWM driver");
MODULE_LICENSE("GPL v2");
