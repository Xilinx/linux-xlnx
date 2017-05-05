/*
 * dp159 redriver and retimer
 * Copyright (C) 2016, 2017 Leon Woestenberg <leon@sidebranch.com>
 *
 * based on code
 * Copyright (C) 2007 Hans Verkuil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of.h>
#include <linux/clk-provider.h>

MODULE_DESCRIPTION("i2c device driver for dp159 redriver and retimer");
MODULE_AUTHOR("Leon Woestenberg");
MODULE_LICENSE("GPL");

static bool debug;

module_param(debug, bool, 0644);

MODULE_PARM_DESC(debug, "Debugging messages, 0=Off (default), 1=On");

struct clk_tx_linerate {
	struct clk_hw hw;
	struct i2c_client *client;
	struct clk *clk;
	unsigned long rate;
};

static inline int dp159_write(struct i2c_client *client, u8 reg, u8 value)
{
	int rc;
	u8 readback;
	rc = i2c_smbus_write_byte_data(client, reg, value);
	return rc;
}

static inline int dp159_read(struct i2c_client *client, u8 reg)
{

	return i2c_smbus_read_byte_data(client, reg);
}

static int dp159_program(struct i2c_client *client, unsigned long rate)
{
	int r;
	r = dp159_write(client, 0x09, 0x06);

	if ((rate / (1000000)) > 3400) {
		printk(KERN_INFO "dp159_program(rate = %lu) for HDMI 2.0\n", rate);
		// Automatic retimer for HDMI 2.0
		r |= dp159_write(client, 0x0B, 0x1a);
		r |= dp159_write(client, 0x0C, 0xa1);
		r |= dp159_write(client, 0x0D, 0x00);
		r |= dp159_write(client, 0x0A, 0x36);
	} else {
		printk(KERN_INFO "dp159_program(rate = %lu) for HDMI 1.4\n", rate);
		//r = dp159_write(client, 0x0A, 0x34);			// The redriver mode must be selected to support low video rates
		/*datasheet has 0 by default. 0x1 disables DDC training and only allows HDMI1.4b/DVI, which is OK*/

		// Automatic redriver to retimer crossover at 1.0 Gbps
		r |= dp159_write(client, 0x0B, 0x01);
		// Set VSWING data decrease by 24%
		r |= dp159_write(client, 0x0C, 0xA0);
		r |= dp159_write(client, 0x0D, 0x00);
		r |= dp159_write(client, 0x0A, 0x35);
	}
	return r;
}

#define to_clk_tx_linerate(_hw) container_of(_hw, struct clk_tx_linerate, hw)

int clk_tx_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct clk_tx_linerate *clk;
	clk = to_clk_tx_linerate(hw);
	//printk(KERN_INFO "dp159: clk_tx_set_rate(): rate = %lu, parent_rate = %lu\n", rate, parent_rate);
	clk->rate = rate;
	dp159_program(clk->client, rate);
	return 0;
};

unsigned long clk_tx_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_tx_linerate *clk;
	clk = to_clk_tx_linerate(hw);
	//printk(KERN_INFO "dp159: clk_tx_recalc_rate(): parent_rate = %lu\n", parent_rate);
	return clk->rate;
};

long clk_tx_round_rate(struct clk_hw *hw,
	unsigned long rate,	unsigned long *parent_rate)
{
	struct clk_tx_linerate *clk;
	clk = to_clk_tx_linerate(hw);
	return rate;
};

struct clk_ops clk_tx_rate_ops = {
	.set_rate 		= &clk_tx_set_rate,
	.recalc_rate	= &clk_tx_recalc_rate,
	.round_rate		= &clk_tx_round_rate,
};

static int dp159_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct clk_tx_linerate *clk_tx;
	struct clk *clk;
	struct clk_init_data init;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	if ((dp159_read(client, 0) != 'D') || (dp159_read(client, 1) != 'P')) {
		dev_err(&client->dev, "Identification registers do not indicate DP159 presence.\n");
		return -ENODEV;
	}

	/* initialize to HDMI 1.4 */
	(void)dp159_write(client, 0x0A, 0x35);			// Automatic redriver to retimer crossover at 1.0 Gbps
	// @TODO this line was also commented-out in bare-metal
	// is this setting clock rate dependend?
	//r = dp159_write(client, 0x0A, 0x34);			// The redriver mode must be selected to support low video rates
	(void)dp159_write(client, 0x0B, 0x01);
	(void)dp159_write(client, 0x0C, 0xA0);				// Set VSWING data decrease by 24%
	(void)dp159_write(client, 0x0D, 0x00);

	/* allocate fixed-rate clock */
	clk_tx = kzalloc(sizeof(*clk_tx), GFP_KERNEL);
	if (!clk_tx)
		return -ENOMEM;

	init.name = "clk_tx_linerate";
	init.ops = &clk_tx_rate_ops;
	init.flags = 0; ///*flags | CLK_IS_BASIC*/;
	init.parent_names = NULL;
	init.num_parents = 0;

	clk_tx->hw.init = &init;

	/* register the clock */
	clk = clk_register(&client->dev, &clk_tx->hw);
	if (IS_ERR(clk)) {
		kfree(clk_tx);
		return PTR_ERR(clk);
	}
	/* reference to client in clock */
	clk_tx->client = client;
	clk_tx->clk = clk;
	/* reference to clk_tx in client */
	i2c_set_clientdata(client, (void *)clk_tx);
	//dev_info(&client->dev, "DP159 retimer.\n");

	ret = of_clk_add_provider(client->dev.of_node, of_clk_src_simple_get,
		to_clk_tx_linerate(&clk_tx->hw));
	if (ret) {
		dev_err(&client->dev, "unable to add clk provider\n");
	}

	return 0;
}

static int dp159_remove(struct i2c_client *client)
{
	struct clk_tx_linerate *clk_tx;
	clk_tx = (struct clk_tx_linerate *)i2c_get_clientdata(client);
	if (clk_tx)
		clk_unregister(clk_tx->clk);
	return 0;
}

static const struct i2c_device_id dp159_id[] = {
	{ "dp159", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, dp159_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id dp159_of_match[] = {
        { .compatible = "ti,dp159", },
        { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dp159_of_match);
#endif

static struct i2c_driver dp159_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name	= "dp159",
		.of_match_table = of_match_ptr(dp159_of_match),
	},
	.probe		= dp159_probe,
	.remove		= dp159_remove,
	.id_table	= dp159_id,
};

module_i2c_driver(dp159_driver);
