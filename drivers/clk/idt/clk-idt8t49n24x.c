// SPDX-License-Identifier: GPL-2.0
/* clk-idt8t49n24x.c - Program 8T49N24x settings via I2C.
 *
 * Copyright (C) 2018, Integrated Device Technology, Inc. <david.cater@idt.com>
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 * This program is distributed "AS IS" and  WITHOUT ANY WARRANTY;
 * including the implied warranties of MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, or NON-INFRINGEMENT.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "clk-idt8t49n24x-core.h"
#include "clk-idt8t49n24x-debugfs.h"

#define OUTPUTMODE_HIGHZ	0
#define OUTPUTMODE_LVDS		2
#define IDT24x_MIN_FREQ		1000000L
#define IDT24x_MAX_FREQ		300000000L
#define DRV_NAME		"idt8t49n24x"

enum clk_idt24x_variant {
	idt24x
};

static u32 mask_and_shift(u32 value, u8 mask)
{
	value &= mask;
	return value >> bits_to_shift(mask);
}

/**
 * idt24x_set_output_mode - Set the mode for a particular clock
 * output in the register.
 * @reg:	The current register value before setting the mode.
 * @mask:	The bitmask identifying where in the register the
 *		output mode is stored.
 * @mode:	The mode to set.
 *
 * Return: the new register value with the specified mode bits set.
 */
static int idt24x_set_output_mode(u32 reg, u8 mask, u8 mode)
{
	if (((reg & mask) >> bits_to_shift(mask)) == OUTPUTMODE_HIGHZ) {
		reg = reg & ~mask;
		reg |= (OUTPUTMODE_LVDS << bits_to_shift(mask));
	}
	return reg;
}

/**
 * idt24x_read_from_hw - Get the current values on the hw
 * @chip:	Device data structure
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int idt24x_read_from_hw(struct clk_idt24x_chip *chip)
{
	int err;
	struct i2c_client *client = chip->i2c_client;
	u32 tmp, tmp2;
	u8 output;

	err = regmap_read(chip->regmap, IDT24x_REG_DSM_INT_8,
			  &chip->reg_dsm_int_8);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_DSM_INT_8: %i",
			__func__, err);
		return err;
	}
	dev_dbg(&client->dev, "%s: reg_dsm_int_8: 0x%x",
		__func__, chip->reg_dsm_int_8);

	err = regmap_read(chip->regmap, IDT24x_REG_DSMFRAC_20_16_MASK,
			  &chip->reg_dsm_frac_20_16);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_DSMFRAC_20_16_MASK: %i",
			__func__, err);
		return err;
	}
	dev_dbg(&client->dev, "%s: reg_dsm_frac_20_16: 0x%x",
		__func__, chip->reg_dsm_frac_20_16);

	err = regmap_read(chip->regmap, IDT24x_REG_OUTEN, &chip->reg_out_en_x);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_OUTEN: %i",
			__func__, err);
		return err;
	}
	dev_dbg(&client->dev, "%s: reg_out_en_x: 0x%x",
		__func__, chip->reg_out_en_x);

	err = regmap_read(chip->regmap, IDT24x_REG_OUTMODE0_1, &tmp);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_OUTMODE0_1: %i",
			__func__, err);
		return err;
	}

	tmp2 = idt24x_set_output_mode(
		tmp, IDT24x_REG_OUTMODE0_MASK, OUTPUTMODE_LVDS);
	tmp2 = idt24x_set_output_mode(
		tmp2, IDT24x_REG_OUTMODE1_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"%s: reg_out_mode_0_1 original: 0x%x. After setting OUT0/1 to LVDS if necessary: 0x%x",
		__func__, tmp, tmp2);
	chip->reg_out_mode_0_1 = tmp2;

	err = regmap_read(chip->regmap, IDT24x_REG_OUTMODE2_3, &tmp);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_OUTMODE2_3: %i",
			__func__, err);
		return err;
	}

	tmp2 = idt24x_set_output_mode(
		tmp, IDT24x_REG_OUTMODE2_MASK, OUTPUTMODE_LVDS);
	tmp2 = idt24x_set_output_mode(
		tmp2, IDT24x_REG_OUTMODE3_MASK, OUTPUTMODE_LVDS);
	dev_dbg(&client->dev,
		"%s: reg_out_mode_2_3 original: 0x%x. After setting OUT2/3 to LVDS if necessary: 0x%x",
		__func__, tmp, tmp2);
	chip->reg_out_mode_2_3 = tmp2;

	err = regmap_read(chip->regmap, IDT24x_REG_Q_DIS, &chip->reg_qx_dis);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_Q_DIS: %i",
			__func__, err);
		return err;
	}
	dev_dbg(&client->dev, "%s: reg_qx_dis: 0x%x",
		__func__, chip->reg_qx_dis);

	err = regmap_read(chip->regmap, IDT24x_REG_NS1_Q0, &chip->reg_ns1_q0);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_NS1_Q0: %i",
			__func__, err);
		return err;
	}
	dev_dbg(&client->dev, "%s: reg_ns1_q0: 0x%x",
		__func__, chip->reg_ns1_q0);

	for (output = 1; output <= 3; output++) {
		struct clk_register_offsets offsets;

		err = idt24x_get_offsets(output, &offsets);
		if (err) {
			dev_err(&client->dev,
				"%s: error calling idt24x_get_offsets: %i",
				__func__, err);
			return err;
		}

		err = regmap_read(chip->regmap, offsets.n_17_16_offset,
				  &chip->reg_n_qx_17_16[output - 1]);
		if (err) {
			dev_err(&client->dev,
				"%s: error reading n_17_16_offset for output %d (offset: 0x%x): %i",
				__func__, output, offsets.n_17_16_offset, err);
			return err;
		}
		dev_dbg(&client->dev,
			"%s: reg_n_qx_17_16[Q%u]: 0x%x",
			__func__, output, chip->reg_n_qx_17_16[output - 1]);

		err = regmap_read(chip->regmap, offsets.nfrac_27_24_offset,
				  &chip->reg_nfrac_qx_27_24[output - 1]);
		if (err) {
			dev_err(&client->dev,
				"%s: error reading nfrac_27_24_offset for output %d (offset: 0x%x): %i",
				__func__, output,
				offsets.nfrac_27_24_offset, err);
			return err;
		}
		dev_dbg(&client->dev,
			"%s: reg_nfrac_qx_27_24[Q%u]: 0x%x",
			__func__, output,
			chip->reg_nfrac_qx_27_24[output - 1]);
	}

	dev_info(&client->dev,
		 "%s: initial values read from chip successfully",
		 __func__);

	/* Also read DBL_DIS to determine whether the doubler is disabled. */
	err = regmap_read(chip->regmap, IDT24x_REG_DBL_DIS, &tmp);
	if (err) {
		dev_err(&client->dev,
			"%s: error reading IDT24x_REG_DBL_DIS: %i",
			__func__, err);
		return err;
	}
	chip->doubler_disabled = mask_and_shift(tmp, IDT24x_REG_DBL_DIS_MASK);
	dev_dbg(&client->dev, "%s: doubler_disabled: %d",
		__func__, chip->doubler_disabled);

	return 0;
}

/**
 * idt24x_set_rate - Sets the specified output clock to the specified rate.
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @rate:	the rate (in Hz) for the specified clock.
 * @parent_rate:(not sure) the rate for a parent signal (e.g.,
 *		the VCO feeding the output)
 *
 * This function will call idt24_set_frequency, which means it will
 * calculate divider for all requested outputs and update the attached
 * device (issue I2C commands to update the registers).
 *
 * Return: 0 on success.
 */
static int idt24x_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	int err = 0;

	/*
	 * hw->clk is the pointer to the specific output clock the user is
	 * requesting. We use hw to get back to the output structure for
	 * the output clock. Set the requested rate in the output structure.
	 * Note that container_of cannot be used to find the device structure
	 * (clk_idt24x_chip) from clk_hw, because clk_idt24x_chip has an array
	 * of idt24x_output structs. That is why it is necessary to use
	 * output->chip to access the device structure.
	 */
	struct idt24x_output *output = to_idt24x_output(hw);
	struct i2c_client *client = output->chip->i2c_client;

	if (rate < output->chip->min_freq || rate > output->chip->max_freq) {
		dev_err(&client->dev,
			"requested frequency (%luHz) is out of range\n", rate);
		return -EINVAL;
	}

	/*
	 * Set the requested frequency in the output data structure, and then
	 * call idt24x_set_frequency. idt24x_set_frequency considers all
	 * requested frequencies when deciding on a vco frequency and
	 * calculating dividers.
	 */
	output->requested = rate;

	/*
	 * Also set in the memory location used by the debugfs file
	 * that exposes the output clock frequency. That allows querying
	 * the current rate via debugfs.
	 */
	output->debug_freq = rate;

	dev_info(&client->dev,
		 "%s. calling idt24x_set_frequency for Q%u. rate: %lu",
		 __func__, output->index, rate);
	err = idt24x_set_frequency(output->chip);

	if (err != 0)
		dev_err(&client->dev, "error calling set_frequency: %d", err);

	return err;
}

/**
 * idt24x_round_rate - get valid rate that is closest to the requested rate
 * @hw:		clk_hw struct that identifies the specific output clock.
 * @rate:	the rate (in Hz) for the specified clock.
 * @parent_rate:(not sure) the rate for a parent signal (e.g., the VCO
 *		feeding the output). This is an i/o param.
 *		If the driver supports a parent clock for the output (e.g.,
 *		the VCO(?), then set this param to indicate what the rate of
 *		the parent would be (e.g., the VCO frequency) if the rounded
 *		rate is used.
 *
 * Returns the closest rate to the requested rate actually supported by the
 * chip.
 *
 * Return: adjusted rate
 */
static long idt24x_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	/*
	 * The chip has fractional output dividers, so assume it
	 * can provide the requested rate.
	 *
	 * TODO: figure out the closest rate that chip can support
	 * within a low error threshold and return that rate.
	 */
	return rate;
}

/**
 * idt24x_recalc_rate - return the frequency being provided by the clock.
 * @hw:			clk_hw struct that identifies the specific output clock.
 * @parent_rate:	(not sure) the rate for a parent signal (e.g., the
 *			VCO feeding the output)
 *
 * This API appears to be used to read the current values from the hardware
 * and report the frequency being provided by the clock. Without this function,
 * the clock will be initialized to 0 by default. The OS appears to be
 * calling this to find out what the current value of the clock is at
 * startup, so it can determine when .set_rate is actually changing the
 * frequency.
 *
 * Return: the frequency of the specified clock.
 */
static unsigned long idt24x_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct idt24x_output *output = to_idt24x_output(hw);

	return output->requested;
}

/*
 * Note that .prepare and .unprepare appear to be used more in Gates.
 * They do not appear to be necessary for this device.
 * Instead, update the device when .set_rate is called.
 */
static const struct clk_ops idt24x_clk_ops = {
	.recalc_rate = idt24x_recalc_rate,
	.round_rate = idt24x_round_rate,
	.set_rate = idt24x_set_rate,
};

static bool idt24x_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	return false;
}

static bool idt24x_regmap_is_writeable(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config idt24x_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
	.max_register = 0xff,
	.writeable_reg = idt24x_regmap_is_writeable,
	.volatile_reg = idt24x_regmap_is_volatile,
};

/**
 * idt24x_clk_notifier_cb - Clock rate change callback
 * @nb:		Pointer to notifier block
 * @event:	Notification reason
 * @data:	Pointer to notification data object
 *
 * This function is called when the input clock frequency changes.
 * The callback checks whether a valid bus frequency can be generated after the
 * change. If so, the change is acknowledged, otherwise the change is aborted.
 * New dividers are written to the HW in the pre- or post change notification
 * depending on the scaling direction.
 *
 * Return:	NOTIFY_STOP if the rate change should be aborted, NOTIFY_OK
 *		to acknowledge the change, NOTIFY_DONE if the notification is
 *		considered irrelevant.
 */
static int idt24x_clk_notifier_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct clk_idt24x_chip *chip = to_clk_idt24x_from_nb(nb);
	int err = 0;

	dev_info(&chip->i2c_client->dev,
		 "%s: input frequency changed: %lu Hz. event: %lu",
		 __func__, ndata->new_rate, event);

	switch (event) {
	case PRE_RATE_CHANGE: {
		dev_dbg(&chip->i2c_client->dev, "PRE_RATE_CHANGE\n");
		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		chip->input_clk_freq = ndata->new_rate;
		/*
		 * Can't call clock API clk_set_rate here; I believe
		 * it will be ignored if the rate is the same as we
		 * set previously. Need to call our internal function.
		 */
		dev_dbg(&chip->i2c_client->dev,
			"POST_RATE_CHANGE. Calling idt24x_set_frequency\n");
		err = idt24x_set_frequency(chip);
		if (err)
			dev_err(&chip->i2c_client->dev,
				"error calling idt24x_set_frequency (%i)\n",
				err);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct clk_hw *of_clk_idt24x_get(
	struct of_phandle_args *clkspec, void *_data)
{
	struct clk_idt24x_chip *chip = _data;
	unsigned int idx = clkspec->args[0];

	if (idx >= ARRAY_SIZE(chip->clk)) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &chip->clk[idx].hw;
}

/**
 * idt24x_probe - main entry point for ccf driver
 * @client:	pointer to i2c_client structure
 * @id:		pointer to i2c_device_id structure
 *
 * Main entry point function that gets called to initialize the driver.
 *
 * Return: 0 for success.
 */
static int idt24x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct clk_idt24x_chip *chip;
	struct clk_init_data init;

	int err = 0;
	int x;
	char buf[6];

	dev_info(&client->dev, "%s", __func__);
	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	init.ops = &idt24x_clk_ops;
	init.flags = 0;
	init.num_parents = 0;
	chip->i2c_client = client;

	chip->min_freq = IDT24x_MIN_FREQ;
	chip->max_freq = IDT24x_MAX_FREQ;

	for (x = 0; x < NUM_INPUTS + 1; x++) {
		char name[12];

		sprintf(name, x == NUM_INPUTS ? "input-xtal" : "input-clk%i",
			x);
		dev_dbg(&client->dev, "attempting to get %s", name);
		chip->input_clk = devm_clk_get(&client->dev, name);
		if (IS_ERR(chip->input_clk)) {
			err = PTR_ERR(chip->input_clk);
			/*
			 * TODO: Handle EPROBE_DEFER error, which indicates
			 * that the input_clk isn't available now but may be
			 * later when the appropriate module is loaded.
			 */
		} else {
			err = 0;
			chip->input_clk_num = x;
			break;
		}
	}

	if (err) {
		dev_err(&client->dev, "Unable to get input clock (%u).", err);
		chip->input_clk = NULL;
		return err;
	}

	chip->input_clk_freq = clk_get_rate(chip->input_clk);
	dev_dbg(&client->dev, "Got input-freq from input-clk in device tree: %uHz",
		chip->input_clk_freq);

	chip->input_clk_nb.notifier_call = idt24x_clk_notifier_cb;
	if (clk_notifier_register(chip->input_clk, &chip->input_clk_nb))
		dev_warn(&client->dev,
			 "Unable to register clock notifier for input_clk.");

	dev_dbg(&client->dev, "%s: about to read settings: %zu",
		__func__, ARRAY_SIZE(chip->settings));

	err = of_property_read_u8_array(
		client->dev.of_node, "settings", chip->settings,
		ARRAY_SIZE(chip->settings));
	if (!err) {
		dev_dbg(&client->dev, "settings property specified in DT");
		chip->has_settings = true;
	} else {
		if (err == -EOVERFLOW) {
			dev_alert(&client->dev,
				  "EOVERFLOW error trying to read the settings. ARRAY_SIZE: %zu",
				  ARRAY_SIZE(chip->settings));
			return err;
		}
		dev_dbg(&client->dev,
			"settings property not specified in DT (or there was an error that can be ignored: %i). The settings property is optional.",
			err);
	}

	/*
	 * Requested output frequencies cannot be specified in the DT.
	 * Either a consumer needs to use the clock API to request the rate,
	 * or use debugfs to set the rate from user space. Use clock-names in
	 * DT to specify the output clock.
	 */

	chip->regmap = devm_regmap_init_i2c(client, &idt24x_regmap_config);
	if (IS_ERR(chip->regmap)) {
		dev_err(&client->dev, "failed to allocate register map\n");
		return PTR_ERR(chip->regmap);
	}

	dev_dbg(&client->dev, "%s: call i2c_set_clientdata", __func__);
	i2c_set_clientdata(client, chip);

	if (chip->has_settings) {
		/*
		 * A raw settings array was specified in the DT. Write the
		 * settings to the device immediately.
		 */
		err = i2cwritebulk(
			chip->i2c_client, chip->regmap, 0, chip->settings,
			ARRAY_SIZE(chip->settings));
		if (err) {
			dev_err(&client->dev,
				"error writing all settings to chip (%i)\n",
				err);
			return err;
		}
		dev_dbg(&client->dev, "successfully wrote full settings array");
	}

	/*
	 * Whether or not settings were written to the device, read all
	 * current values from the hw.
	 */
	dev_dbg(&client->dev, "read from HW");
	err = idt24x_read_from_hw(chip);
	if (err) {
		dev_err(&client->dev,
			"failed calling idt24x_read_from_hw (%i)\n", err);
		return err;
	}

	/* Create all 4 clocks */
	for (x = 0; x < NUM_OUTPUTS; x++) {
		init.name = kasprintf(
			GFP_KERNEL, "%s.Q%i", client->dev.of_node->name, x);
		chip->clk[x].chip = chip;
		chip->clk[x].hw.init = &init;
		chip->clk[x].index = x;
		err = devm_clk_hw_register(&client->dev, &chip->clk[x].hw);
		kfree(init.name); /* clock framework made a copy of the name */
		if (err) {
			dev_err(&client->dev, "clock registration failed\n");
			return err;
		}
		dev_dbg(&client->dev, "successfully registered Q%i", x);
	}

	if (err) {
		dev_err(&client->dev, "clock registration failed\n");
		return err;
	}

	err = of_clk_add_hw_provider(
		client->dev.of_node, of_clk_idt24x_get, chip);
	if (err) {
		dev_err(&client->dev, "unable to add clk provider\n");
		return err;
	}

	err = idt24x_expose_via_debugfs(client, chip);
	if (err) {
		dev_err(&client->dev,
			"error calling idt24x_expose_via_debugfs: %i\n", err);
		return err;
	}

	if (chip->input_clk_num == NUM_INPUTS)
		sprintf(buf, "XTAL");
	else
		sprintf(buf, "CLK%i", chip->input_clk_num);
	dev_info(&client->dev, "probe success. input freq: %uHz (%s), settings string? %s\n",
		 chip->input_clk_freq, buf,
		 chip->has_settings ? "true" : "false");
	return 0;
}

static int idt24x_remove(struct i2c_client *client)
{
	struct clk_idt24x_chip *chip = to_clk_idt24x_from_client(&client);

	dev_info(&client->dev, "%s", __func__);
	of_clk_del_provider(client->dev.of_node);
	idt24x_cleanup_debugfs(chip);

	if (!chip->input_clk)
		clk_notifier_unregister(
			chip->input_clk, &chip->input_clk_nb);
	return 0;
}

static const struct i2c_device_id idt24x_id[] = {
	{ "idt8t49n24x", idt24x },
	{ }
};
MODULE_DEVICE_TABLE(i2c, idt24x_id);

static const struct of_device_id idt24x_of_match[] = {
	{ .compatible = "idt,idt8t49n241" },
	{},
};
MODULE_DEVICE_TABLE(of, idt24x_of_match);

static struct i2c_driver idt24x_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = idt24x_of_match,
	},
	.probe = idt24x_probe,
	.remove = idt24x_remove,
	.id_table = idt24x_id,
};

module_i2c_driver(idt24x_driver);

MODULE_DESCRIPTION("8T49N24x ccf driver");
MODULE_AUTHOR("David Cater <david.cater@idt.com>");
MODULE_LICENSE("GPL v2");
