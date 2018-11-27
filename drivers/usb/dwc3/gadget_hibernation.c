/**
 * gadget_hibernation.c - DesignWare USB3 DRD Controller gadget hibernation file
 *
 * This file has routines to handle hibernation and wakeup events in gadget mode
 *
 * Author: Mayank Adesara <madesara@xilinx.com>
 * Author: Anurag Kumar Vulisha <anuragku@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "core.h"
#include "gadget.h"
#include "debug.h"
#include "io.h"

/* array of registers to save on hibernation and restore them on wakeup */
static u32 save_reg_addr[] = {
	DWC3_DCTL,
	DWC3_DCFG,
	DWC3_DEVTEN
};

/*
 * wait_timeout - Waits until timeout
 * @wait_time: time to wait in jiffies
 */
static void wait_timeout(unsigned long wait_time)
{
	unsigned long timeout = jiffies + wait_time;

	while (!time_after_eq(jiffies, timeout))
		cpu_relax();
}

/**
 * save_regs - Saves registers on hibernation
 * @dwc: pointer to our controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
static int save_regs(struct dwc3 *dwc)
{
	int i;

	if (!dwc->saved_regs) {
		dwc->saved_regs = devm_kmalloc(dwc->dev,
					       sizeof(save_reg_addr),
					       GFP_KERNEL);
		if (!dwc->saved_regs) {
			dev_err(dwc->dev, "Not enough memory to save regs\n");
			return -ENOMEM;
		}
	}

	for (i = 0; i < ARRAY_SIZE(save_reg_addr); i++)
		dwc->saved_regs[i] = dwc3_readl(dwc->regs,
						save_reg_addr[i]);
	return 0;
}

/**
 * restore_regs - Restores registers on wakeup
 * @dwc: pointer to our controller context structure
 */
static void restore_regs(struct dwc3 *dwc)
{
	int i;

	if (!dwc->saved_regs) {
		dev_warn(dwc->dev, "Regs not saved\n");
		return;
	}

	for (i = 0; i < ARRAY_SIZE(save_reg_addr); i++)
		dwc3_writel(dwc->regs, save_reg_addr[i],
			    dwc->saved_regs[i]);
}

/**
 * restart_ep0_trans - Restarts EP0 transfer on wakeup
 * @dwc: pointer to our controller context structure
 * epnum: endpoint number
 *
 * Returns 0 on success otherwise negative errno.
 */
static int restart_ep0_trans(struct dwc3 *dwc, int epnum)
{
	struct dwc3_ep *dep = dwc->eps[epnum];
	struct dwc3_trb *trb = dwc->ep0_trb;
	struct dwc3_gadget_ep_cmd_params params;
	int ret;
	u32 cmd;

	memset(&params, 0, sizeof(params));
	params.param0 = upper_32_bits(dwc->ep0_trb_addr);
	params.param1 = lower_32_bits(dwc->ep0_trb_addr);

	/* set HWO bit back to 1 and restart transfer */
	trb->ctrl |= DWC3_TRB_CTRL_HWO;

	/* Clear the TRBSTS feild */
	trb->size &= ~(0x0F << 28);

	cmd = DWC3_DEPCMD_STARTTRANSFER | DWC3_DEPCMD_PARAM(0);
	ret = dwc3_send_gadget_ep_cmd(dep, cmd, &params);
	if (ret < 0) {
		dev_err(dwc->dev, "failed to restart transfer on %s\n",
			dep->name);
		return ret;
	}

	dwc3_gadget_ep_get_transfer_index(dep);

	return 0;
}

extern dma_addr_t dwc3_trb_dma_offset(struct dwc3_ep *dep,
				      struct dwc3_trb *trb);
/**
 * restore_eps - Restores non EP0 eps in the same state as they were before
 * hibernation
 * @dwc: pointer to our controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
static int restore_eps(struct dwc3 *dwc)
{
	int epnum, ret;

	for (epnum = 2; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		/* Enable the endpoint */
		struct dwc3_ep *dep = dwc->eps[epnum];

		if (!dep)
			continue;

		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		ret = __dwc3_gadget_ep_enable(dep, true);
		if (ret) {
			dev_err(dwc->dev, "failed to enable %s\n", dep->name);
			return ret;
		}
	}

	for (epnum = 2; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		struct dwc3_ep *dep = dwc->eps[epnum];

		if (!dep)
			continue;

		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		if (dep->flags & DWC3_EP_STALL) {
			/* Set stall for the endpoint */
			struct dwc3_gadget_ep_cmd_params	params;

			memset(&params, 0x00, sizeof(params));

			ret = dwc3_send_gadget_ep_cmd(dep, DWC3_DEPCMD_SETSTALL,
						      &params);
			if (ret) {
				dev_err(dwc->dev, "failed to set STALL on %s\n",
					dep->name);
				return ret;
			}
		} else {
			u32 cmd;
			struct dwc3_gadget_ep_cmd_params params;
			struct dwc3_trb *trb;
			u8 trb_dequeue = dep->trb_dequeue;

			trb = &dep->trb_pool[trb_dequeue];

			/*
			 * check the last processed TRBSTS field has value
			 * 4 (TRBInProgress), if yes resubmit the same TRB
			 */
			if (DWC3_TRB_SIZE_TRBSTS(trb->size) ==
					DWC3_TRB_STS_XFER_IN_PROG) {
				/* Set the HWO bit */
				trb->ctrl |= DWC3_TRB_CTRL_HWO;

				/* Clear the TRBSTS field */
				trb->size &= ~(0x0F << 28);

				memset(&params, 0, sizeof(params));

				/* Issue starttransfer */
				params.param0 =
					upper_32_bits(dwc3_trb_dma_offset(dep,
									  trb));
				params.param1 =
					lower_32_bits(dwc3_trb_dma_offset(dep,
									  trb));

				cmd = DWC3_DEPCMD_STARTTRANSFER |
					DWC3_DEPCMD_PARAM(0);

				dwc3_send_gadget_ep_cmd(dep, cmd, &params);

				dwc3_gadget_ep_get_transfer_index(dep);
			} else {
				ret = __dwc3_gadget_kick_transfer(dep);
				if (ret) {
					dev_err(dwc->dev,
						"%s: restart transfer failed\n",
						dep->name);
					return ret;
				}
			}
		}
	}

	return 0;
}

/**
 * restore_ep0 - Restores EP0 in the same state as they were before hibernation
 * @dwc: pointer to our controller context structure
 *
 * Returns 0 on success otherwise negative errno.
 */
static int restore_ep0(struct dwc3 *dwc)
{
	int epnum, ret;

	for (epnum = 0; epnum < 2; epnum++) {
		struct dwc3_ep *dep = dwc->eps[epnum];

		if (!dep)
			continue;

		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		ret = __dwc3_gadget_ep_enable(dep, true);
		if (ret) {
			dev_err(dwc->dev, "failed to enable %s\n", dep->name);
			return ret;
		}

		if (dep->flags & DWC3_EP_STALL) {
			struct dwc3_gadget_ep_cmd_params        params;

			memset(&params, 0x00, sizeof(params));

			ret = dwc3_send_gadget_ep_cmd(dep, DWC3_DEPCMD_SETSTALL,
						      &params);
			if (ret) {
				dev_err(dwc->dev, "failed to set STALL on %s\n",
					dep->name);
				return ret;
			}
		} else {
			if (!dep->resource_index && epnum)
				continue;

			ret = restart_ep0_trans(dwc, epnum);
			if (ret) {
				dev_err(dwc->dev,
					"failed to restart transfer on: %s\n",
					dep->name);
				return ret;
			}
		}
	}

	return 0;
}

/**
 * save_endpoint_state - Saves ep state on hibernation
 * @dep: endpoint to get state
 *
 * Returns 0 on success otherwise negative errno.
 */
static int save_endpoint_state(struct dwc3_ep *dep)
{
	struct dwc3 *dwc = dep->dwc;
	struct dwc3_gadget_ep_cmd_params params;
	int ret;

	memset(&params, 0, sizeof(params));
	ret = dwc3_send_gadget_ep_cmd(dep, DWC3_DEPCMD_GETEPSTATE,
				      &params);
	if (ret) {
		dev_err(dwc->dev, "Failed to get endpoint state on %s\n",
			dep->name);
		return ret;
	}

	dep->saved_state = dwc3_readl(dep->regs, DWC3_DEPCMDPAR2);
	return 0;
}

/**
 * gadget_hibernation_interrupt - Interrupt handler of hibernation
 * @dwc: pointer to our controller context structure
 */
void gadget_hibernation_interrupt(struct dwc3 *dwc)
{
	u32 epnum, reg;
	int retries, ret;

	/* Check if the link state is valid before hibernating */
	switch (dwc3_gadget_get_link_state(dwc)) {
	case DWC3_LINK_STATE_U3:
	case DWC3_LINK_STATE_SS_DIS:
		break;
	default:
		dev_dbg(dwc->dev,
			"%s: Got fake hiber event\n", __func__);
		return;
	}

	/* stop all active transfers and save endpoint status */
	for (epnum = 0; epnum < DWC3_ENDPOINTS_NUM; epnum++) {
		struct dwc3_ep *dep = dwc->eps[epnum];

		if (!dep)
			continue;

		if (!(dep->flags & DWC3_EP_ENABLED))
			continue;

		if (dep->flags & DWC3_EP_TRANSFER_STARTED)
			dwc3_stop_active_transfer(dep, false);

		save_endpoint_state(dep);
	}

	/* stop the controller */
	dwc3_gadget_run_stop(dwc, false, true);
	dwc->is_hibernated = true;

	/*
	 * ack events, don't process them; h/w decrements the count by the value
	 * written
	 */
	reg = dwc3_readl(dwc->regs, DWC3_GEVNTCOUNT(0));
	dwc3_writel(dwc->regs, DWC3_GEVNTCOUNT(0), reg);
	dwc->ev_buf->count = 0;
	dwc->ev_buf->flags &= ~DWC3_EVENT_PENDING;

	reg = dwc3_readl(dwc->regs, DWC3_DCTL);

	/* disable keep connect if we are disconnected right now */
	if (dwc3_gadget_get_link_state(dwc) == DWC3_LINK_STATE_SS_DIS) {
		reg &= ~DWC3_DCTL_KEEP_CONNECT;
		dwc3_writel(dwc->regs, DWC3_DCTL, reg);
	} else {
		reg |= DWC3_DCTL_KEEP_CONNECT;
		 dwc3_writel(dwc->regs, DWC3_DCTL, reg);
	}

	/* save generic registers */
	save_regs(dwc);

	/* initiate controller save state */
	reg |= DWC3_DCTL_CSS;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	/* wait till controller saves state */
	retries = DWC3_NON_STICKY_SAVE_RETRIES;
	do {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);
		if (!(reg & DWC3_DSTS_SSS))
			break;

		udelay(DWC3_NON_STICKY_SAVE_DELAY);
	} while (--retries);

	if (retries < 0) {
		dev_err(dwc->dev, "USB core failed to save state\n");
		goto err;
	}

	/* Set the controller as wakeup capable */
	dwc3_simple_wakeup_capable(dwc->dev, true);

	/* set USB core power state to D3 - power down */
	ret = dwc3_set_usb_core_power(dwc, false);
	if (ret < 0) {
		dev_err(dwc->dev, "%s: Failed to hibernate\n", __func__);
		/* call wakeup handler */
		gadget_wakeup_interrupt(dwc);
		return;
	}

	dev_info(dwc->dev, "Hibernated!\n");
	return;

err:
	dev_err(dwc->dev, "Fail in handling Hibernation Interrupt\n");
}

/**
 * gadget_wakeup_interrupt - Interrupt handler of wakeup
 * @dwc: pointer to our controller context structure
 */
void gadget_wakeup_interrupt(struct dwc3 *dwc)
{
	u32 reg, link_state;
	int ret, retries;
	bool enter_hiber = false;

	/* On USB 2.0 we observed back to back wakeup interrupts */
	if (!dwc->is_hibernated) {
		dev_err(dwc->dev, "Not in hibernated state\n");
		goto err;
	}

	/* Restore power to USB core */
	if (dwc3_set_usb_core_power(dwc, true)) {
		dev_err(dwc->dev, "Failed to restore USB core power\n");
		goto err;
	}

	/* Clear the controller wakeup capable flag */
	dwc3_simple_wakeup_capable(dwc->dev, false);

	/* Initialize the core and restore the saved registers */
	dwc3_core_init(dwc);
	restore_regs(dwc);

	/* ask controller to save the non-sticky registers */
	reg = dwc3_readl(dwc->regs, DWC3_DCTL);
	reg |= DWC3_DCTL_CRS;
	dwc3_writel(dwc->regs, DWC3_DCTL, reg);

	/* Wait till non-sticky registers are restored */
	retries = DWC3_NON_STICKY_RESTORE_RETRIES;
	do {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);
		if (!(reg & DWC3_DSTS_RSS))
			break;

		udelay(DWC3_NON_STICKY_RESTORE_DELAY);
	} while (--retries);

	if (retries < 0 || (reg & DWC3_DSTS_SRE)) {
		dev_err(dwc->dev, "Failed to restore non-sticky regs\n");
		goto err;
	}

	/* restore ep0 endpoints */
	ret = restore_ep0(dwc);
	if (ret) {
		dev_err(dwc->dev, "Failed in restorig EP0 states\n");
		goto err;
	}

	/* start the controller */
	ret = dwc3_gadget_run_stop(dwc, true, false);
	if (ret < 0) {
		dev_err(dwc->dev, "USB core failed to start on wakeup\n");
		goto err;
	}

	/* Wait until device controller is ready */
	retries = DWC3_DEVICE_CTRL_READY_RETRIES;
	while (--retries) {
		reg = dwc3_readl(dwc->regs, DWC3_DSTS);
		if (reg & DWC3_DSTS_DCNRD)
			udelay(DWC3_DEVICE_CTRL_READY_DELAY);
		else
			break;
	}

	if (retries < 0) {
		dev_err(dwc->dev, "USB core failed to restore controller\n");
		goto err;
	}

	/*
	 * As some suprious signals also cause wakeup event, wait for some time
	 * and check the link state to confirm if the wakeup signal is real
	 */
	wait_timeout(msecs_to_jiffies(10));

	link_state = dwc3_gadget_get_link_state(dwc);

	/* check if the link state is in a valid state */
	switch (link_state) {
	case DWC3_LINK_STATE_RESET:
		/* Reset devaddr */
		reg = dwc3_readl(dwc->regs, DWC3_DCFG);
		reg &= ~(DWC3_DCFG_DEVADDR_MASK);
		dwc3_writel(dwc->regs, DWC3_DCFG, reg);

		/* issue recovery on the link */
		ret = dwc3_gadget_set_link_state(dwc, DWC3_LINK_STATE_RECOV);
		if (ret < 0) {
			dev_err(dwc->dev,
				"Failed to set link state to Recovery\n");
			goto err;
		}

		break;

	case DWC3_LINK_STATE_SS_DIS:
		/* Clear keep connect from reconnecting to HOST */
		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		reg &= ~DWC3_DCTL_KEEP_CONNECT;
		dwc3_writel(dwc->regs, DWC3_DCTL, reg);
		/* fall through */
	case DWC3_LINK_STATE_U3:
		/* Ignore wakeup event as the link is still in U3 state */
		dev_dbg(dwc->dev, "False wakeup event %d\n", link_state);

		if (!dwc->force_hiber_wake)
			enter_hiber = true;
		break;

	default:
		/* issue recovery on the link */
		ret = dwc3_gadget_set_link_state(dwc, DWC3_LINK_STATE_RECOV);
		if (ret < 0) {
			dev_err(dwc->dev,
				"Failed to set link state to Recovery\n");
			goto err;
		}

		break;
	}

	if (link_state != DWC3_LINK_STATE_SS_DIS) {
		/* Restore non EP0 EPs */
		ret = restore_eps(dwc);
		if (ret) {
			dev_err(dwc->dev, "Failed restoring non-EP0 states\n");
			goto err;
		}
	}

	/* clear the flag */
	dwc->is_hibernated = false;

	if (enter_hiber) {
		/*
		 * as the wakeup was because of the spurious signals,
		 * enter hibernation again
		 */
		gadget_hibernation_interrupt(dwc);
		return;
	}

	dev_info(dwc->dev, "We are back from hibernation!\n");
	return;

err:
	dev_err(dwc->dev, "Fail in handling Wakeup Interrupt\n");
}
