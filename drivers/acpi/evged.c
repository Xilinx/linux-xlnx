/*
 * Generic Event Device for ACPI.
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Generic Event Device allows platforms to handle interrupts in ACPI
 * ASL statements. It follows very similar to  _EVT method approach
 * from GPIO events. All interrupts are listed in _CRS and the handler
 * is written in _EVT method. Here is an example.
 *
 * Device (GED0)
 * {
 *
 *     Name (_HID, "ACPI0013")
 *     Name (_UID, 0)
 *     Method (_CRS, 0x0, Serialized)
 *     {
 *		Name (RBUF, ResourceTemplate ()
 *		{
 *		Interrupt(ResourceConsumer, Edge, ActiveHigh, Shared, , , )
 *		{123}
 *		}
 *     })
 *
 *     Method (_EVT, 1) {
 *             if (Lequal(123, Arg0))
 *             {
 *             }
 *     }
 * }
 *
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#define MODULE_NAME	"acpi-ged"

struct acpi_ged_event {
	struct list_head node;
	struct device *dev;
	unsigned int gsi;
	unsigned int irq;
	acpi_handle handle;
};

static irqreturn_t acpi_ged_irq_handler(int irq, void *data)
{
	struct acpi_ged_event *event = data;
	acpi_status acpi_ret;

	acpi_ret = acpi_execute_simple_method(event->handle, NULL, event->gsi);
	if (ACPI_FAILURE(acpi_ret))
		dev_err_once(event->dev, "IRQ method execution failed\n");

	return IRQ_HANDLED;
}

static acpi_status acpi_ged_request_interrupt(struct acpi_resource *ares,
					      void *context)
{
	struct acpi_ged_event *event;
	unsigned int irq;
	unsigned int gsi;
	unsigned int irqflags = IRQF_ONESHOT;
	struct device *dev = context;
	acpi_handle handle = ACPI_HANDLE(dev);
	acpi_handle evt_handle;
	struct resource r;
	struct acpi_resource_irq *p = &ares->data.irq;
	struct acpi_resource_extended_irq *pext = &ares->data.extended_irq;

	if (ares->type == ACPI_RESOURCE_TYPE_END_TAG)
		return AE_OK;

	if (!acpi_dev_resource_interrupt(ares, 0, &r)) {
		dev_err(dev, "unable to parse IRQ resource\n");
		return AE_ERROR;
	}
	if (ares->type == ACPI_RESOURCE_TYPE_IRQ)
		gsi = p->interrupts[0];
	else
		gsi = pext->interrupts[0];

	irq = r.start;

	if (ACPI_FAILURE(acpi_get_handle(handle, "_EVT", &evt_handle))) {
		dev_err(dev, "cannot locate _EVT method\n");
		return AE_ERROR;
	}

	dev_info(dev, "GED listening GSI %u @ IRQ %u\n", gsi, irq);

	event = devm_kzalloc(dev, sizeof(*event), GFP_KERNEL);
	if (!event)
		return AE_ERROR;

	event->gsi = gsi;
	event->dev = dev;
	event->irq = irq;
	event->handle = evt_handle;

	if (r.flags & IORESOURCE_IRQ_SHAREABLE)
		irqflags |= IRQF_SHARED;

	if (devm_request_threaded_irq(dev, irq, NULL, acpi_ged_irq_handler,
				      irqflags, "ACPI:Ged", event)) {
		dev_err(dev, "failed to setup event handler for irq %u\n", irq);
		return AE_ERROR;
	}

	return AE_OK;
}

static int ged_probe(struct platform_device *pdev)
{
	acpi_status acpi_ret;

	acpi_ret = acpi_walk_resources(ACPI_HANDLE(&pdev->dev), "_CRS",
				       acpi_ged_request_interrupt, &pdev->dev);
	if (ACPI_FAILURE(acpi_ret)) {
		dev_err(&pdev->dev, "unable to parse the _CRS record\n");
		return -EINVAL;
	}

	return 0;
}

static const struct acpi_device_id ged_acpi_ids[] = {
	{"ACPI0013"},
	{},
};

static struct platform_driver ged_driver = {
	.probe = ged_probe,
	.driver = {
		.name = MODULE_NAME,
		.acpi_match_table = ACPI_PTR(ged_acpi_ids),
	},
};
builtin_platform_driver(ged_driver);
