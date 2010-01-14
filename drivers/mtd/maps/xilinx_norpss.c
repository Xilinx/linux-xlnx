/*
 *
 * Xilinx PSS NOR Flash Controller Driver
 *
 * (c) 2009 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <mach/smc.h>

/*
 * Register offset definitions is available in smc.h file which exists in
 * arch\arm\mach-xilinx\include\mach folder
 */

/*
 * Register values for using NOR interface of SMC Controller
 */
#define SET_CYCLES_REG ((0x0 << 20) | /* set_t6 or we_time from sram_cycles */ \
			(0x0 << 17) | /* set_t5 or t_tr from sram_cycles */    \
			(0x1 << 14) | /* set_t4 or t_pc from sram_cycles */    \
			(0x5 << 11) | /* set_t3 or t_wp from sram_cycles */    \
			(0x1 << 8)  | /* set_t2 t_ceoe from sram_cycles */     \
			(0x7 << 4)  | /* set_t1 t_wc from sram_cycles */       \
			(0x7))	      /* set_t0 t_rc from sram_cycles */
				      /* 0x00006977 */
#define SET_OPMODE_REG ((0x1 << 13) | /* set_burst_align,set to 32 beats */    \
			(0x0 << 12) | /* set_bls,set to default */	       \
			(0x0 << 11) | /* set_adv bit, set to default */	       \
			(0x0 << 10) | /* set_baa, we don't use baa_n */	       \
			(0x0 << 7)  | /* set_wr_bl,write brust len,set to 0 */ \
			(0x0 << 6)  | /* set_wr_sync, set to 0 */	       \
			(0x0 << 3)  | /* set_rd_bl,read brust len,set to 0 */  \
			(0x0 << 2)  | /* set_rd_sync, set to 0 */	       \
			(0x1))	      /* set_mw, memory width, 16bits width*/
				      /* 0x00002001 */
#define DIRECT_CMD_REG ((0x0 << 23) | /* Chip 0 from interface 0 */	       \
			(0x2 << 21) | /* UpdateRegs operation */	       \
			(0x0 << 20) | /* No ModeReg write */		       \
			(0x0))	      /* Addr, not used in UpdateRegs */
				      /* 0x00400000 */


/**
 * struct xnorpss_info - This definition defines NOR flash driver instance
 * @mtd:	Pointer	to the mtd_info structure
 * @map:	map_info structure for the flash device
 * @parts:	Pointer	to the mtd_partition structure
 * @smc_regs:	Virtual address of the SMC controller registers
 **/
struct xnorpss_info {
	struct mtd_info		*mtd;
	struct map_info		map;
#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition	*parts;
#endif
	void __iomem *smc_regs;
};


/**
 * xnorpss_init_nor_flash - Initialize the NOR flash interface
 * @smc_regs:	Virtual address of the SMC controller registers
 *
 * Initialize interface 0 of SMC controller and set controller registers for the
 * flash device.
 **/
static void xnorpss_init_nor_flash(void __iomem *smc_regs)
{
	__raw_writel(SET_CYCLES_REG, smc_regs + XSMCPSS_MC_SET_CYCLES);
	__raw_writel(SET_OPMODE_REG, smc_regs + XSMCPSS_MC_SET_OPMODE);
	__raw_writel(DIRECT_CMD_REG, smc_regs + XSMCPSS_MC_DIRECT_CMD);
}

/**
 * xnorpss_probe - Probe method for the NOR flash driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function initializes the hardware, sets the driver data structures and
 * creates the partition on NOR flash device. The partitions are created only if
 * support is enabled in the kernel and partition info is available in the
 * command line.
 *
 * returns:	0 on success or error value on error
 **/
static int __devinit xnorpss_probe(struct platform_device *pdev)
{
	int err;
	struct xnorpss_info *info;
	struct resource *nor_res, *smc_res;
	unsigned long flash_size;
#ifdef CONFIG_MTD_PARTITIONS
	int  nr_parts;
	static const char *part_probe_types[] = { "cmdlinepart", NULL };
#endif
	info = kzalloc(sizeof(struct xnorpss_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Get the NOR Flash virtual address */
	nor_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (nor_res == NULL) {
		err = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource for NOR failed\n");
		goto out_free_info;
	}

	flash_size = nor_res->end - nor_res->start + 1;
	if (!request_mem_region(nor_res->start, flash_size, pdev->name)) {
		err = -EBUSY;
		dev_err(&pdev->dev, "request_mem_region for NOR failed\n");
		goto out_free_info;
	}

	info->map.virt = ioremap(nor_res->start, flash_size);
	if (!info->map.virt) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "ioremap for NOR failed\n");
		goto out_release_nor_mem_region;
	}

	/* Get the SMC controller virtual address */
	smc_res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (smc_res == NULL) {
		err = -ENODEV;
		dev_err(&pdev->dev, "platform_get_resource for SMC failed\n");
		goto out_nor_iounmap;
	}

	if (!request_mem_region(smc_res->start,
		smc_res->end - smc_res->start + 1, pdev->name)) {
		err = -EBUSY;
		dev_err(&pdev->dev, "request_mem_region for SMC failed\n");
		goto out_nor_iounmap;
	}

	info->smc_regs = ioremap(smc_res->start,
					smc_res->end - smc_res->start + 1);
	if (!info->smc_regs) {
		err = -ENOMEM;
		dev_err(&pdev->dev, "ioremap for SMC failed\n");
		goto out_release_smc_mem_region;
	}

	/* Initialize the NOR flash interface on SMC controller */
	xnorpss_init_nor_flash(info->smc_regs);

	/* Set the NOR flash mapping information */
	info->map.name		= pdev->dev.bus_id;
	info->map.phys		= nor_res->start;
	info->map.size		= flash_size;
	info->map.bankwidth	= *((int *)(pdev->dev.platform_data));
	info->map.set_vpp	= NULL;

	simple_map_init(&info->map);

	info->mtd = do_map_probe("cfi_probe", &info->map);
	if (!info->mtd) {
		err = -EIO;
		dev_err(&pdev->dev, "do_map_probe failed\n");
		goto out_smc_iounmap;
	}

	info->mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	/* Get the partition information from command line argument */
	nr_parts = parse_mtd_partitions(info->mtd, part_probe_types,
					&info->parts, 0);
	if (nr_parts > 0) {
		dev_info(&pdev->dev, "found %d number of partition information"
		" through command line", nr_parts);
		add_mtd_partitions(info->mtd, info->parts, nr_parts);
	} else {
		dev_info(&pdev->dev, "Command line partition table is not"
		" available, creating single partition on flash\n");

#endif
		add_mtd_device(info->mtd);
#ifdef CONFIG_MTD_PARTITIONS
	}
#endif
	platform_set_drvdata(pdev, info);
	dev_info(&pdev->dev, "at 0x%08X mapped to 0x%08X, Size=%ldMB\n",
		info->map.phys, (u32 __force) info->map.virt, flash_size >> 20);
	return 0;

out_smc_iounmap:
	iounmap(info->smc_regs);
out_release_smc_mem_region:
	release_mem_region(smc_res->start, smc_res->end - smc_res->start + 1);
out_nor_iounmap:
	iounmap(info->map.virt);
out_release_nor_mem_region:
	release_mem_region(nor_res->start, flash_size);
out_free_info:
	kfree(info);

	return err;
}

/**
 * xnorpss_remove - Remove method for the NOR flash driver
 * @pdev:	Pointer to the platform_device structure
 *
 * This function is called if a device is physically removed from the system or
 * if the driver module is being unloaded. It frees all resources allocated to
 * the device.
 *
 * returns:	0 on success or error value on error
 **/
static int __devexit xnorpss_remove(struct platform_device *pdev)
{
	struct xnorpss_info *info = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (info) {
#ifdef CONFIG_MTD_PARTITIONS
		/* Remove the partitions */
		if (info->parts) {
			del_mtd_partitions(info->mtd);
			kfree(info->parts);
		} else
#endif
			del_mtd_device(info->mtd);
		map_destroy(info->mtd);
		release_mem_region(info->map.phys, info->map.size);
		iounmap(info->map.virt);
		kfree(info);
	}
	return 0;
}


/*
 * xnorpss_driver - This structure defines the NOR flash platform driver
 */
static struct platform_driver xnorpss_driver = {
	.probe  = xnorpss_probe,
	.remove	= __devexit_p(xnorpss_remove),
	.driver = {
		.name	= "xnorpss",
		.owner	= THIS_MODULE,
	},
};


/**
 * xnorpss_init_mtd - NOR flash driver module initialization function
 *
 * returns:	0 on success or error value on error
 **/
static int __init xnorpss_init_mtd(void)
{
	return platform_driver_register(&xnorpss_driver);
}

/**
 * xnorpss_cleanup_mtd - NOR flash driver module exit function
 **/
static void __exit xnorpss_cleanup_mtd(void)
{
	platform_driver_unregister(&xnorpss_driver);
}


module_init(xnorpss_init_mtd);
module_exit(xnorpss_cleanup_mtd);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Xilinx, Inc.");
MODULE_ALIAS("platform:xnorpss");
MODULE_DESCRIPTION("MTD map driver for NOR Flash on PSS");

