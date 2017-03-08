/*
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct fclk_state {
	struct device	*dev;
	struct clk	*pl;
};

/* Match table for of_platform binding */
static const struct of_device_id fclk_of_match[] = {
	{ .compatible = "xlnx,fclk",},
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, fclk_of_match);

static int fclk_probe(struct platform_device *pdev)
{
	struct fclk_state *st;
	int ret;

	st = devm_kzalloc(&pdev->dev, sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->dev = &pdev->dev;

	platform_set_drvdata(pdev, st);

	st->pl = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(st->pl))
		return PTR_ERR(st->pl);

	ret = clk_prepare_enable(st->pl);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		return ret;
	}

	return 0;
}

static int fclk_remove(struct platform_device *pdev)
{
	struct fclk_state *st = platform_get_drvdata(pdev);

	clk_disable_unprepare(st->pl);
	return 0;
}

static struct platform_driver fclk_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = fclk_of_match,
	},
	.probe		= fclk_probe,
	.remove		= fclk_remove,
};

module_platform_driver(fclk_driver);

MODULE_AUTHOR("Shubhrajyoti Datta <shubhrajyoti.datta@xilinx.com>");
MODULE_DESCRIPTION("fclk enable");
MODULE_LICENSE("GPL v2");
