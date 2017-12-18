/*
 * Allwinner SoCs SRAM Controller Driver
 *
 * Copyright (C) 2015 Maxime Ripard
 *
 * Author: Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <linux/soc/sunxi/sunxi_sram.h>

struct sunxi_sram_func {
	char	*func;
	u8	val;
};

struct sunxi_sram_data {
	char			*name;
	u8			reg;
	u8			offset;
	u8			width;
	struct sunxi_sram_func	*func;
	struct list_head	list;
};

struct sunxi_sram_desc {
	struct sunxi_sram_data	data;
	bool			claimed;
};

#define SUNXI_SRAM_MAP(_val, _func)				\
	{							\
		.func = _func,					\
		.val = _val,					\
	}

#define SUNXI_SRAM_DATA(_name, _reg, _off, _width, ...)		\
	{							\
		.name = _name,					\
		.reg = _reg,					\
		.offset = _off,					\
		.width = _width,				\
		.func = (struct sunxi_sram_func[]){		\
			__VA_ARGS__, { } },			\
	}

static struct sunxi_sram_desc sun4i_a10_sram_a3_a4 = {
	.data	= SUNXI_SRAM_DATA("A3-A4", 0x4, 0x4, 2,
				  SUNXI_SRAM_MAP(0, "cpu"),
				  SUNXI_SRAM_MAP(1, "emac")),
};

static struct sunxi_sram_desc sun4i_a10_sram_d = {
	.data	= SUNXI_SRAM_DATA("D", 0x4, 0x0, 1,
				  SUNXI_SRAM_MAP(0, "cpu"),
				  SUNXI_SRAM_MAP(1, "usb-otg")),
};

static const struct of_device_id sunxi_sram_dt_ids[] = {
	{
		.compatible	= "allwinner,sun4i-a10-sram-a3-a4",
		.data		= &sun4i_a10_sram_a3_a4.data,
	},
	{
		.compatible	= "allwinner,sun4i-a10-sram-d",
		.data		= &sun4i_a10_sram_d.data,
	},
	{}
};

static struct device *sram_dev;
static LIST_HEAD(claimed_sram);
static DEFINE_SPINLOCK(sram_lock);
static void __iomem *base;

static int sunxi_sram_show(struct seq_file *s, void *data)
{
	struct device_node *sram_node, *section_node;
	const struct sunxi_sram_data *sram_data;
	const struct of_device_id *match;
	struct sunxi_sram_func *func;
	const __be32 *sram_addr_p, *section_addr_p;
	u32 val;

	seq_puts(s, "Allwinner sunXi SRAM\n");
	seq_puts(s, "--------------------\n\n");

	for_each_child_of_node(sram_dev->of_node, sram_node) {
		sram_addr_p = of_get_address(sram_node, 0, NULL, NULL);

		seq_printf(s, "sram@%08x\n",
			   be32_to_cpu(*sram_addr_p));

		for_each_child_of_node(sram_node, section_node) {
			match = of_match_node(sunxi_sram_dt_ids, section_node);
			if (!match)
				continue;
			sram_data = match->data;

			section_addr_p = of_get_address(section_node, 0,
							NULL, NULL);

			seq_printf(s, "\tsection@%04x\t(%s)\n",
				   be32_to_cpu(*section_addr_p),
				   sram_data->name);

			val = readl(base + sram_data->reg);
			val >>= sram_data->offset;
			val &= GENMASK(sram_data->width - 1, 0);

			for (func = sram_data->func; func->func; func++) {
				seq_printf(s, "\t\t%s%c\n", func->func,
					   func->val == val ? '*' : ' ');
			}
		}

		seq_puts(s, "\n");
	}

	return 0;
}

static int sunxi_sram_open(struct inode *inode, struct file *file)
{
	return single_open(file, sunxi_sram_show, inode->i_private);
}

static const struct file_operations sunxi_sram_fops = {
	.open = sunxi_sram_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static inline struct sunxi_sram_desc *to_sram_desc(const struct sunxi_sram_data *data)
{
	return container_of(data, struct sunxi_sram_desc, data);
}

static const struct sunxi_sram_data *sunxi_sram_of_parse(struct device_node *node,
							 unsigned int *value)
{
	const struct of_device_id *match;
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_fixed_args(node, "allwinner,sram", 1, 0,
					       &args);
	if (ret)
		return ERR_PTR(ret);

	if (!of_device_is_available(args.np)) {
		ret = -EBUSY;
		goto err;
	}

	if (value)
		*value = args.args[0];

	match = of_match_node(sunxi_sram_dt_ids, args.np);
	if (!match) {
		ret = -EINVAL;
		goto err;
	}

	of_node_put(args.np);
	return match->data;

err:
	of_node_put(args.np);
	return ERR_PTR(ret);
}

int sunxi_sram_claim(struct device *dev)
{
	const struct sunxi_sram_data *sram_data;
	struct sunxi_sram_desc *sram_desc;
	unsigned int device;
	u32 val, mask;

	if (IS_ERR(base))
		return -EPROBE_DEFER;

	if (!dev || !dev->of_node)
		return -EINVAL;

	sram_data = sunxi_sram_of_parse(dev->of_node, &device);
	if (IS_ERR(sram_data))
		return PTR_ERR(sram_data);

	sram_desc = to_sram_desc(sram_data);

	spin_lock(&sram_lock);

	if (sram_desc->claimed) {
		spin_unlock(&sram_lock);
		return -EBUSY;
	}

	mask = GENMASK(sram_data->offset + sram_data->width - 1,
		       sram_data->offset);
	val = readl(base + sram_data->reg);
	val &= ~mask;
	writel(val | ((device << sram_data->offset) & mask),
	       base + sram_data->reg);

	spin_unlock(&sram_lock);

	return 0;
}
EXPORT_SYMBOL(sunxi_sram_claim);

int sunxi_sram_release(struct device *dev)
{
	const struct sunxi_sram_data *sram_data;
	struct sunxi_sram_desc *sram_desc;

	if (!dev || !dev->of_node)
		return -EINVAL;

	sram_data = sunxi_sram_of_parse(dev->of_node, NULL);
	if (IS_ERR(sram_data))
		return -EINVAL;

	sram_desc = to_sram_desc(sram_data);

	spin_lock(&sram_lock);
	sram_desc->claimed = false;
	spin_unlock(&sram_lock);

	return 0;
}
EXPORT_SYMBOL(sunxi_sram_release);

static int sunxi_sram_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct dentry *d;

	sram_dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);

	d = debugfs_create_file("sram", S_IRUGO, NULL, NULL,
				&sunxi_sram_fops);
	if (!d)
		return -ENOMEM;

	return 0;
}

static const struct of_device_id sunxi_sram_dt_match[] = {
	{ .compatible = "allwinner,sun4i-a10-sram-controller" },
	{ },
};
MODULE_DEVICE_TABLE(of, sunxi_sram_dt_match);

static struct platform_driver sunxi_sram_driver = {
	.driver = {
		.name		= "sunxi-sram",
		.of_match_table	= sunxi_sram_dt_match,
	},
	.probe	= sunxi_sram_probe,
};
module_platform_driver(sunxi_sram_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner sunXi SRAM Controller Driver");
MODULE_LICENSE("GPL");
