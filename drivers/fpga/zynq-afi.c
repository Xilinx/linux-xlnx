// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA AFI driver.
 * Copyright (c) 2018 Xilinx Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* Registers and special values for doing register-based operations */
#define AFI_RDCHAN_CTRL_OFFSET	0x00
#define AFI_WRCHAN_CTRL_OFFSET	0x14

#define AFI_BUSWIDTH_MASK	0x01

/**
 * struct afi_fpga - AFI register description
 * @membase:	pointer to register struct
 * @afi_width:	AFI bus width to be written
 */
struct zynq_afi_fpga {
	void __iomem	*membase;
	u32		afi_width;
};

static int zynq_afi_fpga_probe(struct platform_device *pdev)
{
	struct zynq_afi_fpga *afi_fpga;
	struct resource *res;
	u32 reg_val;
	u32 val;

	afi_fpga = devm_kzalloc(&pdev->dev, sizeof(*afi_fpga), GFP_KERNEL);
	if (!afi_fpga)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	afi_fpga->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(afi_fpga->membase))
		return PTR_ERR(afi_fpga->membase);

	val = device_property_read_u32(&pdev->dev, "xlnx,afi-width",
				       &afi_fpga->afi_width);
	if (val) {
		dev_err(&pdev->dev, "Fail to get the afi bus width\n");
		return -EINVAL;
	}

	reg_val = readl(afi_fpga->membase + AFI_RDCHAN_CTRL_OFFSET);
	reg_val &= ~AFI_BUSWIDTH_MASK;
	writel(reg_val | afi_fpga->afi_width,
	       afi_fpga->membase + AFI_RDCHAN_CTRL_OFFSET);
	reg_val = readl(afi_fpga->membase + AFI_WRCHAN_CTRL_OFFSET);
	reg_val &= ~AFI_BUSWIDTH_MASK;
	writel(reg_val | afi_fpga->afi_width,
	       afi_fpga->membase + AFI_WRCHAN_CTRL_OFFSET);

	return 0;
}

static const struct of_device_id zynq_afi_fpga_ids[] = {
	{ .compatible = "xlnx,zynq-afi-fpga" },
	{ },
};
MODULE_DEVICE_TABLE(of, zynq_afi_fpga_ids);

static struct platform_driver zynq_afi_fpga_driver = {
	.driver = {
		.name = "zynq-afi-fpga",
		.of_match_table = zynq_afi_fpga_ids,
	},
	.probe = zynq_afi_fpga_probe,
};
module_platform_driver(zynq_afi_fpga_driver);

MODULE_DESCRIPTION("ZYNQ FPGA AFI module");
MODULE_AUTHOR("Nava kishore Manne <nava.manne@xilinx.com>");
MODULE_LICENSE("GPL v2");
