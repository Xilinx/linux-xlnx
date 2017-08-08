/*
 * XILINX PS PCIe driver
 *
 * Copyright (C) 2017 Xilinx, Inc. All rights reserved.
 *
 * Description
 * PS PCIe DMA is memory mapped DMA used to execute PS to PL transfers
 * on ZynqMP UltraScale+ Devices.
 * This PCIe driver creates a platform device with specific platform
 * info enabling creation of DMA device corresponding to the channel
 * information provided in the properties
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation
 */

#include "xilinx_ps_pcie.h"
#include "../dmaengine.h"

#define DRV_MODULE_NAME		  "ps_pcie_dma"

static int ps_pcie_dma_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent);
static void ps_pcie_dma_remove(struct pci_dev *pdev);

static u32 channel_properties_pcie_axi[] = {
	(u32)(PCIE_AXI_DIRECTION), (u32)(NUMBER_OF_BUFFER_DESCRIPTORS),
	(u32)(DEFAULT_DMA_QUEUES), (u32)(CHANNEL_COAELSE_COUNT),
	(u32)(CHANNEL_POLL_TIMER_FREQUENCY) };

static u32 channel_properties_axi_pcie[] = {
	(u32)(AXI_PCIE_DIRECTION), (u32)(NUMBER_OF_BUFFER_DESCRIPTORS),
	(u32)(DEFAULT_DMA_QUEUES), (u32)(CHANNEL_COAELSE_COUNT),
	(u32)(CHANNEL_POLL_TIMER_FREQUENCY) };

static struct property_entry generic_pcie_ep_property[] = {
	PROPERTY_ENTRY_U32("numchannels", (u32)MAX_NUMBER_OF_CHANNELS),
	PROPERTY_ENTRY_U32_ARRAY("ps_pcie_channel0",
				 channel_properties_pcie_axi),
	PROPERTY_ENTRY_U32_ARRAY("ps_pcie_channel1",
				 channel_properties_axi_pcie),
	PROPERTY_ENTRY_U32_ARRAY("ps_pcie_channel2",
				 channel_properties_pcie_axi),
	PROPERTY_ENTRY_U32_ARRAY("ps_pcie_channel3",
				 channel_properties_axi_pcie),
	{ },
};

static const struct platform_device_info xlnx_std_platform_dev_info = {
	.name           = XLNX_PLATFORM_DRIVER_NAME,
	.properties     = generic_pcie_ep_property,
};

/**
 * ps_pcie_dma_probe - Driver probe function
 * @pdev: Pointer to the pci_dev structure
 * @ent: pci device id
 *
 * Return: '0' on success and failure value on error
 */
static int ps_pcie_dma_probe(struct pci_dev *pdev,
			     const struct pci_device_id *ent)
{
	int err;
	struct platform_device *platform_dev;
	struct platform_device_info platform_dev_info;

	dev_info(&pdev->dev, "PS PCIe DMA Driver probe\n");

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		return err;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_info(&pdev->dev, "Cannot set 64 bit DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "DMA mask set error\n");
			return err;
		}
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_info(&pdev->dev, "Cannot set 64 bit consistent DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Cannot set consistent DMA mask\n");
			return err;
		}
	}

	pci_set_master(pdev);

	/* For Root DMA platform device will be created through device tree */
	if (pdev->vendor == PCI_VENDOR_ID_XILINX &&
	    pdev->device == ZYNQMP_RC_DMA_DEVID)
		return 0;

	memcpy(&platform_dev_info, &xlnx_std_platform_dev_info,
	       sizeof(xlnx_std_platform_dev_info));

	/* Do device specific channel configuration changes to
	 * platform_dev_info.properties if required
	 * More information on channel properties can be found
	 * at Documentation/devicetree/bindings/dma/xilinx/ps-pcie-dma.txt
	 */

	platform_dev_info.parent = &pdev->dev;
	platform_dev_info.data = &pdev;
	platform_dev_info.size_data = sizeof(struct pci_dev **);

	platform_dev = platform_device_register_full(&platform_dev_info);
	if (IS_ERR(platform_dev)) {
		dev_err(&pdev->dev,
			"Cannot create platform device, aborting\n");
		return PTR_ERR(platform_dev);
	}

	pci_set_drvdata(pdev, platform_dev);

	dev_info(&pdev->dev, "PS PCIe DMA driver successfully probed\n");

	return 0;
}

static struct pci_device_id ps_pcie_dma_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, ZYNQMP_DMA_DEVID) },
	{ PCI_DEVICE(PCI_VENDOR_ID_XILINX, ZYNQMP_RC_DMA_DEVID) },
	{ }
};

static struct pci_driver ps_pcie_dma_driver = {
	.name     = DRV_MODULE_NAME,
	.id_table = ps_pcie_dma_tbl,
	.probe    = ps_pcie_dma_probe,
	.remove   = ps_pcie_dma_remove,
};

/**
 * ps_pcie_init - Driver init function
 *
 * Return: 0 on success. Non zero on failure
 */
static int __init ps_pcie_init(void)
{
	int ret;

	pr_info("%s init()\n", DRV_MODULE_NAME);

	ret = pci_register_driver(&ps_pcie_dma_driver);
	if (ret)
		return ret;

	ret = dma_platform_driver_register();
	if (ret)
		pci_unregister_driver(&ps_pcie_dma_driver);

	return ret;
}

/**
 * ps_pcie_dma_remove - Driver remove function
 * @pdev: Pointer to the pci_dev structure
 *
 * Return: void
 */
static void ps_pcie_dma_remove(struct pci_dev *pdev)
{
	struct platform_device *platform_dev;

	platform_dev = (struct platform_device *)pci_get_drvdata(pdev);

	if (platform_dev)
		platform_device_unregister(platform_dev);
}

/**
 * ps_pcie_exit - Driver exit function
 *
 * Return: void
 */
static void __exit ps_pcie_exit(void)
{
	pr_info("%s exit()\n", DRV_MODULE_NAME);

	dma_platform_driver_unregister();
	pci_unregister_driver(&ps_pcie_dma_driver);
}

module_init(ps_pcie_init);
module_exit(ps_pcie_exit);

MODULE_AUTHOR("Xilinx Inc");
MODULE_DESCRIPTION("Xilinx PS PCIe DMA Driver");
MODULE_LICENSE("GPL v2");
