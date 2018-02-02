// SPDX-License-Identifier: GPL-2.0+
/*
 * Xilinx Zynq MPSoC Firmware layer for debugfs APIs
 *
 *  Copyright (C) 2014-2018 Xilinx, Inc.
 *
 *  Michal Simek <michal.simek@xilinx.com>
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajanv@xilinx.com>
 */

#include <linux/module.h>
#include <linux/soc/xilinx/zynqmp/firmware-debug.h>

/**
 * zynqmp_pm_self_suspend - PM call for master to suspend itself
 * @node:	Node ID of the master or subsystem
 * @latency:	Requested maximum wakeup latency (not supported)
 * @state:	Requested state (not supported)
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_self_suspend(const u32 node,
			   const u32 latency,
			   const u32 state)
{
	return invoke_pm_fn(SELF_SUSPEND, node, latency, state, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_self_suspend);

/**
 * zynqmp_pm_abort_suspend - PM call to announce that a prior suspend request
 *				is to be aborted.
 * @reason:	Reason for the abort
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_abort_suspend(const enum zynqmp_pm_abort_reason reason)
{
	return invoke_pm_fn(ABORT_SUSPEND, reason, 0, 0, 0, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_abort_suspend);

/**
 * zynqmp_pm_register_notifier - Register the PU to be notified of PM events
 * @node:	Node ID of the slave
 * @event:	The event to be notified about
 * @wake:	Wake up on event
 * @enable:	Enable or disable the notifier
 *
 * Return:	Returns status, either success or error+reason
 */
int zynqmp_pm_register_notifier(const u32 node, const u32 event,
				const u32 wake, const u32 enable)
{
	return invoke_pm_fn(REGISTER_NOTIFIER, node, event,
						wake, enable, NULL);
}
EXPORT_SYMBOL_GPL(zynqmp_pm_register_notifier);
