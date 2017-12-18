/*
 * Copyright (c) 2012, Intel Corporation
 * Copyright (c) 2015, Red Hat, Inc.
 * Copyright (c) 2015, 2016 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "ACPI: SPCR: " fmt

#include <linux/acpi.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>

/**
 * parse_spcr() - parse ACPI SPCR table and add preferred console
 *
 * @earlycon: set up earlycon for the console specified by the table
 *
 * For the architectures with support for ACPI, CONFIG_ACPI_SPCR_TABLE may be
 * defined to parse ACPI SPCR table.  As a result of the parsing preferred
 * console is registered and if @earlycon is true, earlycon is set up.
 *
 * When CONFIG_ACPI_SPCR_TABLE is defined, this function should be called
 * from arch inintialization code as soon as the DT/ACPI decision is made.
 *
 */
int __init parse_spcr(bool earlycon)
{
	static char opts[64];
	struct acpi_table_spcr *table;
	acpi_size table_size;
	acpi_status status;
	char *uart;
	char *iotype;
	int baud_rate;
	int err;

	if (acpi_disabled)
		return -ENODEV;

	status = acpi_get_table_with_size(ACPI_SIG_SPCR, 0,
					  (struct acpi_table_header **)&table,
					  &table_size);

	if (ACPI_FAILURE(status))
		return -ENOENT;

	if (table->header.revision < 2) {
		err = -ENOENT;
		pr_err("wrong table version\n");
		goto done;
	}

	iotype = table->serial_port.space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY ?
			"mmio" : "io";

	switch (table->interface_type) {
	case ACPI_DBG2_ARM_SBSA_32BIT:
		iotype = "mmio32";
		/* fall through */
	case ACPI_DBG2_ARM_PL011:
	case ACPI_DBG2_ARM_SBSA_GENERIC:
	case ACPI_DBG2_BCM2835:
		uart = "pl011";
		break;
	case ACPI_DBG2_16550_COMPATIBLE:
	case ACPI_DBG2_16550_SUBSET:
		uart = "uart";
		break;
	default:
		err = -ENOENT;
		goto done;
	}

	switch (table->baud_rate) {
	case 3:
		baud_rate = 9600;
		break;
	case 4:
		baud_rate = 19200;
		break;
	case 6:
		baud_rate = 57600;
		break;
	case 7:
		baud_rate = 115200;
		break;
	default:
		err = -ENOENT;
		goto done;
	}

	snprintf(opts, sizeof(opts), "%s,%s,0x%llx,%d", uart, iotype,
		 table->serial_port.address, baud_rate);

	pr_info("console: %s\n", opts);

	if (earlycon)
		setup_earlycon(opts);

	err = add_preferred_console(uart, 0, opts + strlen(uart) + 1);

done:
	early_acpi_os_unmap_memory((void __iomem *)table, table_size);
	return err;
}
