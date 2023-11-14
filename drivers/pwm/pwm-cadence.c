// SPDX-License-Identifier: GPL-2.0
/*
 * Driver to configure cadence TTC timer as PWM
 * generator
 *
 * Limitations:
 * - When PWM is stopped, timer counter gets stopped immediately. This
 *   doesn't allow the current PWM period to complete and stops abruptly.
 * - Disabled PWM emits inactive level.
 * - When user requests a change in  any parameter of PWM (period/duty cycle/polarity)
 *   while PWM is in enabled state:
 *	- PWM is stopped abruptly.
 *	- Requested parameter is changed.
 *	- Fresh PWM cycle is started.
 *
 * Copyright (C) 2023, Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/of_address.h>

#define TTC_CLK_CNTRL		0x00
#define TTC_CNT_CNTRL		0x0C
#define TTC_MATCH_CNT_VAL	0x30
#define TTC_COUNT_VAL		0x18
#define TTC_INTR_VAL		0x24
#define TTC_ISR			0x54
#define TTC_IER			0x60
#define TTC_PWM_CHANNEL		0x4

#define TTC_CLK_CNTRL_CSRC		BIT(5)
#define TTC_CLK_CNTRL_PSV		GENMASK(4, 1)
#define TTC_CLK_CNTRL_PS_EN		BIT(0)

#define TTC_CNTR_CTRL_DIS		BIT(0)
#define TTC_CNTR_CTRL_INTR_MODE_EN	BIT(1)
#define TTC_CNTR_CTRL_MATCH_MODE_EN	BIT(3)
#define TTC_CNTR_CTRL_RST		BIT(4)
#define TTC_CNTR_CTRL_WAVE_EN	BIT(5)
#define TTC_CNTR_CTRL_WAVE_POL	BIT(6)

#define TTC_CNTR_CTRL_WAVE_POL_SHIFT	6
#define TTC_CNTR_CTRL_PRESCALE_SHIFT	1
#define TTC_PWM_MAX_CH			3

/**
 * struct ttc_pwm_priv - Private data for TTC PWM drivers
 * @chip:	PWM chip structure representing PWM controller
 * @clk:	TTC input clock
 * @rate:	TTC input clock rate
 * @max:	Maximum value of the counters
 * @base:	Base address of TTC instance
 */
struct ttc_pwm_priv {
	struct pwm_chip chip;
	struct clk *clk;
	unsigned long rate;
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
				   unsigned int chnum,
				   unsigned long offset)
{
	unsigned long pwm_ch_offset = offset +
				       (TTC_PWM_CHANNEL * chnum);

	return ttc_pwm_readl(priv, pwm_ch_offset);
}

static inline void ttc_pwm_ch_writel(struct ttc_pwm_priv *priv,
				     unsigned int chnum,
				     unsigned long offset,
				     unsigned long val)
{
	unsigned long pwm_ch_offset = offset +
				       (TTC_PWM_CHANNEL * chnum);

	ttc_pwm_writel(priv, pwm_ch_offset, val);
}

static inline struct ttc_pwm_priv *xilinx_pwm_chip_to_priv(struct pwm_chip *chip)
{
	return container_of(chip, struct ttc_pwm_priv, chip);
}

static void ttc_pwm_enable(struct ttc_pwm_priv *priv, struct pwm_device *pwm)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CNT_CNTRL);
	ctrl_reg |= (TTC_CNTR_CTRL_INTR_MODE_EN
				 | TTC_CNTR_CTRL_MATCH_MODE_EN | TTC_CNTR_CTRL_RST);
	ctrl_reg &= ~(TTC_CNTR_CTRL_DIS | TTC_CNTR_CTRL_WAVE_EN);
	ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_CNT_CNTRL, ctrl_reg);
}

static void ttc_pwm_disable(struct ttc_pwm_priv *priv, struct pwm_device *pwm)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CNT_CNTRL);
	ctrl_reg |= TTC_CNTR_CTRL_DIS;

	ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_CNT_CNTRL, ctrl_reg);
}

static void ttc_pwm_set_polarity(struct ttc_pwm_priv *priv, struct pwm_device *pwm,
				 enum pwm_polarity polarity)
{
	u32 ctrl_reg;

	ctrl_reg = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CNT_CNTRL);

	if (polarity == PWM_POLARITY_NORMAL)
		ctrl_reg |= TTC_CNTR_CTRL_WAVE_POL;
	else
		ctrl_reg &= (~TTC_CNTR_CTRL_WAVE_POL);

	ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_CNT_CNTRL, ctrl_reg);
}

static void ttc_pwm_set_counters(struct ttc_pwm_priv *priv,
				 struct pwm_device *pwm,
				 u32 period_cycles,
				 u32 duty_cycles)
{
	/* Set up period */
	ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_INTR_VAL, period_cycles);

	/* Set up duty cycle */
	ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_MATCH_CNT_VAL, duty_cycles);
}

static void ttc_pwm_set_prescalar(struct ttc_pwm_priv *priv,
				  struct pwm_device *pwm,
				  u32 div, bool is_enable)
{
	u32 clk_reg;

	if (is_enable) {
		/* Set up prescalar */
		clk_reg = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CLK_CNTRL);
		clk_reg &= ~TTC_CLK_CNTRL_PSV;
		clk_reg |= (div << TTC_CNTR_CTRL_PRESCALE_SHIFT);
		clk_reg |= TTC_CLK_CNTRL_PS_EN;
		ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_CLK_CNTRL, clk_reg);
	} else {
		/* Disable prescalar */
		clk_reg = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CLK_CNTRL);
		clk_reg &= ~TTC_CLK_CNTRL_PS_EN;
		ttc_pwm_ch_writel(priv, pwm->hwpwm, TTC_CLK_CNTRL, clk_reg);
	}
}

static int ttc_pwm_apply(struct pwm_chip *chip,
			 struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct ttc_pwm_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u64 duty_cycles, period_cycles;
	struct pwm_state cstate;
	unsigned long rate;
	bool flag = false;
	u32 div = 0;

	cstate = pwm->state;

	if (state->polarity != cstate.polarity) {
		if (cstate.enabled)
			ttc_pwm_disable(priv, pwm);

		ttc_pwm_set_polarity(priv, pwm, state->polarity);
	}

	rate = priv->rate;

	/* Prevent overflow by limiting to the maximum possible period */
	period_cycles = min_t(u64, state->period, ULONG_MAX * NSEC_PER_SEC);
	period_cycles = mul_u64_u64_div_u64(period_cycles, rate, NSEC_PER_SEC);

	if (period_cycles > priv->max) {
		/*
		 * Prescale frequency to fit requested period cycles within limit.
		 * Prescalar divides input clock by 2^(prescale_value + 1). Maximum
		 * supported prescalar value is 15.
		 */
		div = mul_u64_u64_div_u64(state->period, rate, (NSEC_PER_SEC * priv->max));
		div = order_base_2(div);
		if (div)
			div -= 1;

		if (div > 15)
			return -ERANGE;

		rate = DIV_ROUND_CLOSEST(rate, BIT(div + 1));
		period_cycles = mul_u64_u64_div_u64(state->period, rate,
						    NSEC_PER_SEC);
		flag = true;
	}

	if (cstate.enabled)
		ttc_pwm_disable(priv, pwm);

	duty_cycles = mul_u64_u64_div_u64(state->duty_cycle, rate,
					  NSEC_PER_SEC);
	ttc_pwm_set_counters(priv, pwm, period_cycles, duty_cycles);

	ttc_pwm_set_prescalar(priv, pwm, div, flag);

	if (state->enabled)
		ttc_pwm_enable(priv, pwm);
	else
		ttc_pwm_disable(priv, pwm);

	return 0;
}

static int ttc_pwm_get_state(struct pwm_chip *chip,
			     struct pwm_device *pwm,
			     struct pwm_state *state)
{
	struct ttc_pwm_priv *priv = xilinx_pwm_chip_to_priv(chip);
	u32 value, pres_en, pres = 1;
	unsigned long rate;
	u64 tmp;

	value = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CNT_CNTRL);

	if (value & TTC_CNTR_CTRL_WAVE_POL)
		state->polarity = PWM_POLARITY_NORMAL;
	else
		state->polarity = PWM_POLARITY_INVERSED;

	if (value & TTC_CNTR_CTRL_DIS)
		state->enabled = false;
	else
		state->enabled = true;

	rate = priv->rate;

	pres_en =  ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CLK_CNTRL);
	pres_en	&= TTC_CLK_CNTRL_PS_EN;

	if (pres_en) {
		pres = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_CLK_CNTRL) & TTC_CLK_CNTRL_PSV;
		pres >>= TTC_CNTR_CTRL_PRESCALE_SHIFT;
		/* If prescale is enabled, the count rate is divided by 2^(pres + 1) */
		pres = BIT(pres + 1);
	}

	tmp = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_INTR_VAL);
	tmp *= pres;
	state->period = DIV64_U64_ROUND_UP(tmp * NSEC_PER_SEC, rate);

	tmp = ttc_pwm_ch_readl(priv, pwm->hwpwm, TTC_MATCH_CNT_VAL);
	tmp *= pres;
	state->duty_cycle = DIV64_U64_ROUND_UP(tmp * NSEC_PER_SEC, rate);

	return 0;
}

static const struct pwm_ops ttc_pwm_ops = {
	.apply = ttc_pwm_apply,
	.get_state = ttc_pwm_get_state,
	.owner = THIS_MODULE,
};

static int ttc_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 pwm_cells, timer_width;
	struct ttc_pwm_priv *priv;
	int ret;

	/*
	 * If pwm-cells property is not present in TTC node,
	 * it would be treated as clocksource/clockevent
	 * device.
	 */
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

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "ERROR: timer input clock not found\n");

	priv->rate = clk_get_rate(priv->clk);

	clk_rate_exclusive_get(priv->clk);

	priv->chip.dev = dev;
	priv->chip.ops = &ttc_pwm_ops;
	priv->chip.npwm = TTC_PWM_MAX_CH;
	ret = pwmchip_add(&priv->chip);
	if (ret) {
		clk_rate_exclusive_put(priv->clk);
		return dev_err_probe(dev, ret, "Could not register PWM chip\n");
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static void ttc_pwm_remove(struct platform_device *pdev)
{
	struct ttc_pwm_priv *priv = platform_get_drvdata(pdev);

	pwmchip_remove(&priv->chip);
	clk_rate_exclusive_put(priv->clk);
}

static const struct of_device_id __maybe_unused ttc_pwm_of_match[] = {
	{ .compatible = "cdns,ttc"},
	{},
};
MODULE_DEVICE_TABLE(of, ttc_pwm_of_match);

static struct platform_driver ttc_pwm_driver = {
	.probe = ttc_pwm_probe,
	.remove_new = ttc_pwm_remove,
	.driver = {
		.name = "ttc-pwm",
		.of_match_table = of_match_ptr(ttc_pwm_of_match),
	},
};
module_platform_driver(ttc_pwm_driver);

MODULE_AUTHOR("Mubin Sayyed <mubin.sayyed@amd.com>");
MODULE_DESCRIPTION("Cadence TTC PWM driver");
MODULE_LICENSE("GPL");
