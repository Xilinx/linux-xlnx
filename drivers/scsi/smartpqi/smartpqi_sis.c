/*
 *    driver for Microsemi PQI-based storage controllers
 *    Copyright (c) 2016 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to esc.storagedev@microsemi.com
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <scsi/scsi_device.h>
#include <asm/unaligned.h>
#include "smartpqi.h"
#include "smartpqi_sis.h"

/* legacy SIS interface commands */
#define SIS_CMD_GET_ADAPTER_PROPERTIES		0x19
#define SIS_CMD_INIT_BASE_STRUCT_ADDRESS	0x1b
#define SIS_CMD_GET_PQI_CAPABILITIES		0x3000

/* for submission of legacy SIS commands */
#define SIS_REENABLE_SIS_MODE			0x1
#define SIS_ENABLE_MSIX				0x40
#define SIS_SOFT_RESET				0x100
#define SIS_CMD_READY				0x200
#define SIS_CMD_COMPLETE			0x1000
#define SIS_CLEAR_CTRL_TO_HOST_DOORBELL		0x1000
#define SIS_CMD_STATUS_SUCCESS			0x1
#define SIS_CMD_COMPLETE_TIMEOUT_SECS		30
#define SIS_CMD_COMPLETE_POLL_INTERVAL_MSECS	10

/* used with SIS_CMD_GET_ADAPTER_PROPERTIES command */
#define SIS_EXTENDED_PROPERTIES_SUPPORTED	0x800000
#define SIS_SMARTARRAY_FEATURES_SUPPORTED	0x2
#define SIS_PQI_MODE_SUPPORTED			0x4
#define SIS_REQUIRED_EXTENDED_PROPERTIES	\
	(SIS_SMARTARRAY_FEATURES_SUPPORTED | SIS_PQI_MODE_SUPPORTED)

/* used with SIS_CMD_INIT_BASE_STRUCT_ADDRESS command */
#define SIS_BASE_STRUCT_REVISION		9
#define SIS_BASE_STRUCT_ALIGNMENT		16

#define SIS_CTRL_KERNEL_UP			0x80
#define SIS_CTRL_KERNEL_PANIC			0x100
#define SIS_CTRL_READY_TIMEOUT_SECS		30
#define SIS_CTRL_READY_POLL_INTERVAL_MSECS	10

#pragma pack(1)

/* for use with SIS_CMD_INIT_BASE_STRUCT_ADDRESS command */
struct sis_base_struct {
	__le32	revision;		/* revision of this structure */
	__le32	flags;			/* reserved */
	__le32	error_buffer_paddr_low;	/* lower 32 bits of physical memory */
					/* buffer for PQI error response */
					/* data */
	__le32	error_buffer_paddr_high;	/* upper 32 bits of physical */
						/* memory buffer for PQI */
						/* error response data */
	__le32	error_buffer_element_length;	/* length of each PQI error */
						/* response buffer element */
						/*   in bytes */
	__le32	error_buffer_num_elements;	/* total number of PQI error */
						/* response buffers available */
};

#pragma pack()

int sis_wait_for_ctrl_ready(struct pqi_ctrl_info *ctrl_info)
{
	unsigned long timeout;
	u32 status;

	timeout = (SIS_CTRL_READY_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		status = readl(&ctrl_info->registers->sis_firmware_status);
		if (status != ~0) {
			if (status & SIS_CTRL_KERNEL_PANIC) {
				dev_err(&ctrl_info->pci_dev->dev,
					"controller is offline: status code 0x%x\n",
					readl(
					&ctrl_info->registers->sis_mailbox[7]));
				return -ENODEV;
			}
			if (status & SIS_CTRL_KERNEL_UP)
				break;
		}
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		msleep(SIS_CTRL_READY_POLL_INTERVAL_MSECS);
	}

	return 0;
}

bool sis_is_firmware_running(struct pqi_ctrl_info *ctrl_info)
{
	bool running;
	u32 status;

	status = readl(&ctrl_info->registers->sis_firmware_status);

	if (status & SIS_CTRL_KERNEL_PANIC)
		running = false;
	else
		running = true;

	if (!running)
		dev_err(&ctrl_info->pci_dev->dev,
			"controller is offline: status code 0x%x\n",
			readl(&ctrl_info->registers->sis_mailbox[7]));

	return running;
}

/* used for passing command parameters/results when issuing SIS commands */
struct sis_sync_cmd_params {
	u32	mailbox[6];	/* mailboxes 0-5 */
};

static int sis_send_sync_cmd(struct pqi_ctrl_info *ctrl_info,
	u32 cmd, struct sis_sync_cmd_params *params)
{
	struct pqi_ctrl_registers __iomem *registers;
	unsigned int i;
	unsigned long timeout;
	u32 doorbell;
	u32 cmd_status;

	registers = ctrl_info->registers;

	/* Write the command to mailbox 0. */
	writel(cmd, &registers->sis_mailbox[0]);

	/*
	 * Write the command parameters to mailboxes 1-4 (mailbox 5 is not used
	 * when sending a command to the controller).
	 */
	for (i = 1; i <= 4; i++)
		writel(params->mailbox[i], &registers->sis_mailbox[i]);

	/* Clear the command doorbell. */
	writel(SIS_CLEAR_CTRL_TO_HOST_DOORBELL,
		&registers->sis_ctrl_to_host_doorbell_clear);

	/* Disable doorbell interrupts by masking all interrupts. */
	writel(~0, &registers->sis_interrupt_mask);

	/*
	 * Force the completion of the interrupt mask register write before
	 * submitting the command.
	 */
	readl(&registers->sis_interrupt_mask);

	/* Submit the command to the controller. */
	writel(SIS_CMD_READY, &registers->sis_host_to_ctrl_doorbell);

	/*
	 * Poll for command completion.  Note that the call to msleep() is at
	 * the top of the loop in order to give the controller time to start
	 * processing the command before we start polling.
	 */
	timeout = (SIS_CMD_COMPLETE_TIMEOUT_SECS * HZ) + jiffies;
	while (1) {
		msleep(SIS_CMD_COMPLETE_POLL_INTERVAL_MSECS);
		doorbell = readl(&registers->sis_ctrl_to_host_doorbell);
		if (doorbell & SIS_CMD_COMPLETE)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
	}

	/* Read the command status from mailbox 0. */
	cmd_status = readl(&registers->sis_mailbox[0]);
	if (cmd_status != SIS_CMD_STATUS_SUCCESS) {
		dev_err(&ctrl_info->pci_dev->dev,
			"SIS command failed for command 0x%x: status = 0x%x\n",
			cmd, cmd_status);
		return -EINVAL;
	}

	/*
	 * The command completed successfully, so save the command status and
	 * read the values returned in mailboxes 1-5.
	 */
	params->mailbox[0] = cmd_status;
	for (i = 1; i < ARRAY_SIZE(params->mailbox); i++)
		params->mailbox[i] = readl(&registers->sis_mailbox[i]);

	return 0;
}

/*
 * This function verifies that we are talking to a controller that speaks PQI.
 */

int sis_get_ctrl_properties(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	u32 properties;
	u32 extended_properties;
	struct sis_sync_cmd_params params;

	memset(&params, 0, sizeof(params));

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_GET_ADAPTER_PROPERTIES,
		&params);
	if (rc)
		return rc;

	properties = params.mailbox[1];

	if (!(properties & SIS_EXTENDED_PROPERTIES_SUPPORTED))
		return -ENODEV;

	extended_properties = params.mailbox[4];

	if ((extended_properties & SIS_REQUIRED_EXTENDED_PROPERTIES) !=
		SIS_REQUIRED_EXTENDED_PROPERTIES)
		return -ENODEV;

	return 0;
}

int sis_get_pqi_capabilities(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	struct sis_sync_cmd_params params;

	memset(&params, 0, sizeof(params));

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_GET_PQI_CAPABILITIES,
		&params);
	if (rc)
		return rc;

	ctrl_info->max_sg_entries = params.mailbox[1];
	ctrl_info->max_transfer_size = params.mailbox[2];
	ctrl_info->max_outstanding_requests = params.mailbox[3];
	ctrl_info->config_table_offset = params.mailbox[4];
	ctrl_info->config_table_length = params.mailbox[5];

	return 0;
}

int sis_init_base_struct_addr(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	void *base_struct_unaligned;
	struct sis_base_struct *base_struct;
	struct sis_sync_cmd_params params;
	unsigned long error_buffer_paddr;
	dma_addr_t bus_address;

	base_struct_unaligned = kzalloc(sizeof(*base_struct)
		+ SIS_BASE_STRUCT_ALIGNMENT - 1, GFP_KERNEL);
	if (!base_struct_unaligned)
		return -ENOMEM;

	base_struct = PTR_ALIGN(base_struct_unaligned,
		SIS_BASE_STRUCT_ALIGNMENT);
	error_buffer_paddr = (unsigned long)ctrl_info->error_buffer_dma_handle;

	put_unaligned_le32(SIS_BASE_STRUCT_REVISION, &base_struct->revision);
	put_unaligned_le32(lower_32_bits(error_buffer_paddr),
		&base_struct->error_buffer_paddr_low);
	put_unaligned_le32(upper_32_bits(error_buffer_paddr),
		&base_struct->error_buffer_paddr_high);
	put_unaligned_le32(PQI_ERROR_BUFFER_ELEMENT_LENGTH,
		&base_struct->error_buffer_element_length);
	put_unaligned_le32(ctrl_info->max_io_slots,
		&base_struct->error_buffer_num_elements);

	bus_address = pci_map_single(ctrl_info->pci_dev, base_struct,
		sizeof(*base_struct), PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(ctrl_info->pci_dev, bus_address)) {
		rc = -ENOMEM;
		goto out;
	}

	memset(&params, 0, sizeof(params));
	params.mailbox[1] = lower_32_bits((u64)bus_address);
	params.mailbox[2] = upper_32_bits((u64)bus_address);
	params.mailbox[3] = sizeof(*base_struct);

	rc = sis_send_sync_cmd(ctrl_info, SIS_CMD_INIT_BASE_STRUCT_ADDRESS,
		&params);

	pci_unmap_single(ctrl_info->pci_dev, bus_address, sizeof(*base_struct),
		PCI_DMA_TODEVICE);

out:
	kfree(base_struct_unaligned);

	return rc;
}

/* Enable MSI-X interrupts on the controller. */

void sis_enable_msix(struct pqi_ctrl_info *ctrl_info)
{
	u32 doorbell_register;

	doorbell_register =
		readl(&ctrl_info->registers->sis_host_to_ctrl_doorbell);
	doorbell_register |= SIS_ENABLE_MSIX;

	writel(doorbell_register,
		&ctrl_info->registers->sis_host_to_ctrl_doorbell);
}

/* Disable MSI-X interrupts on the controller. */

void sis_disable_msix(struct pqi_ctrl_info *ctrl_info)
{
	u32 doorbell_register;

	doorbell_register =
		readl(&ctrl_info->registers->sis_host_to_ctrl_doorbell);
	doorbell_register &= ~SIS_ENABLE_MSIX;

	writel(doorbell_register,
		&ctrl_info->registers->sis_host_to_ctrl_doorbell);
}

void sis_soft_reset(struct pqi_ctrl_info *ctrl_info)
{
	writel(SIS_SOFT_RESET,
		&ctrl_info->registers->sis_host_to_ctrl_doorbell);
}

#define SIS_MODE_READY_TIMEOUT_SECS	30

int sis_reenable_sis_mode(struct pqi_ctrl_info *ctrl_info)
{
	int rc;
	unsigned long timeout;
	struct pqi_ctrl_registers __iomem *registers;
	u32 doorbell;

	registers = ctrl_info->registers;

	writel(SIS_REENABLE_SIS_MODE,
		&registers->sis_host_to_ctrl_doorbell);

	rc = 0;
	timeout = (SIS_MODE_READY_TIMEOUT_SECS * HZ) + jiffies;

	while (1) {
		doorbell = readl(&registers->sis_ctrl_to_host_doorbell);
		if ((doorbell & SIS_REENABLE_SIS_MODE) == 0)
			break;
		if (time_after(jiffies, timeout)) {
			rc = -ETIMEDOUT;
			break;
		}
	}

	if (rc)
		dev_err(&ctrl_info->pci_dev->dev,
			"re-enabling SIS mode failed\n");

	return rc;
}

void sis_write_driver_scratch(struct pqi_ctrl_info *ctrl_info, u32 value)
{
	writel(value, &ctrl_info->registers->sis_driver_scratch);
}

u32 sis_read_driver_scratch(struct pqi_ctrl_info *ctrl_info)
{
	return readl(&ctrl_info->registers->sis_driver_scratch);
}

static void __attribute__((unused)) verify_structures(void)
{
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		revision) != 0x0);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		flags) != 0x4);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_paddr_low) != 0x8);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_paddr_high) != 0xc);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_element_length) != 0x10);
	BUILD_BUG_ON(offsetof(struct sis_base_struct,
		error_buffer_num_elements) != 0x14);
	BUILD_BUG_ON(sizeof(struct sis_base_struct) != 0x18);
}
