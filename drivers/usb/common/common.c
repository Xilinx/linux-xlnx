/*
 * Provides code common for host and device side USB.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 * If either host side (ie. CONFIG_USB=y) or device side USB stack
 * (ie. CONFIG_USB_GADGET=y) is compiled in the kernel, this module is
 * compiled-in as well.  Otherwise, if either of the two stacks is
 * compiled as module, this file is compiled as module as well.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/usb/ch9.h>
#include <linux/usb/of.h>
#include <linux/usb/otg.h>
#include <linux/of_platform.h>

const char *usb_otg_state_string(enum usb_otg_state state)
{
	static const char *const names[] = {
		[OTG_STATE_A_IDLE] = "a_idle",
		[OTG_STATE_A_WAIT_VRISE] = "a_wait_vrise",
		[OTG_STATE_A_WAIT_BCON] = "a_wait_bcon",
		[OTG_STATE_A_HOST] = "a_host",
		[OTG_STATE_A_SUSPEND] = "a_suspend",
		[OTG_STATE_A_PERIPHERAL] = "a_peripheral",
		[OTG_STATE_A_WAIT_VFALL] = "a_wait_vfall",
		[OTG_STATE_A_VBUS_ERR] = "a_vbus_err",
		[OTG_STATE_B_IDLE] = "b_idle",
		[OTG_STATE_B_SRP_INIT] = "b_srp_init",
		[OTG_STATE_B_PERIPHERAL] = "b_peripheral",
		[OTG_STATE_B_WAIT_ACON] = "b_wait_acon",
		[OTG_STATE_B_HOST] = "b_host",
	};

	if (state < 0 || state >= ARRAY_SIZE(names))
		return "UNDEFINED";

	return names[state];
}
EXPORT_SYMBOL_GPL(usb_otg_state_string);

static const char *const speed_names[] = {
	[USB_SPEED_UNKNOWN] = "UNKNOWN",
	[USB_SPEED_LOW] = "low-speed",
	[USB_SPEED_FULL] = "full-speed",
	[USB_SPEED_HIGH] = "high-speed",
	[USB_SPEED_WIRELESS] = "wireless",
	[USB_SPEED_SUPER] = "super-speed",
	[USB_SPEED_SUPER_PLUS] = "super-speed-plus",
};

const char *usb_speed_string(enum usb_device_speed speed)
{
	if (speed < 0 || speed >= ARRAY_SIZE(speed_names))
		speed = USB_SPEED_UNKNOWN;
	return speed_names[speed];
}
EXPORT_SYMBOL_GPL(usb_speed_string);

enum usb_device_speed usb_get_maximum_speed(struct device *dev)
{
	const char *maximum_speed;
	int ret;

	ret = device_property_read_string(dev, "maximum-speed", &maximum_speed);
	if (ret < 0)
		return USB_SPEED_UNKNOWN;

	ret = match_string(speed_names, ARRAY_SIZE(speed_names), maximum_speed);

	return (ret < 0) ? USB_SPEED_UNKNOWN : ret;
}
EXPORT_SYMBOL_GPL(usb_get_maximum_speed);

const char *usb_state_string(enum usb_device_state state)
{
	static const char *const names[] = {
		[USB_STATE_NOTATTACHED] = "not attached",
		[USB_STATE_ATTACHED] = "attached",
		[USB_STATE_POWERED] = "powered",
		[USB_STATE_RECONNECTING] = "reconnecting",
		[USB_STATE_UNAUTHENTICATED] = "unauthenticated",
		[USB_STATE_DEFAULT] = "default",
		[USB_STATE_ADDRESS] = "addressed",
		[USB_STATE_CONFIGURED] = "configured",
		[USB_STATE_SUSPENDED] = "suspended",
	};

	if (state < 0 || state >= ARRAY_SIZE(names))
		return "UNKNOWN";

	return names[state];
}
EXPORT_SYMBOL_GPL(usb_state_string);

static const char *const usb_dr_modes[] = {
	[USB_DR_MODE_UNKNOWN]		= "",
	[USB_DR_MODE_HOST]		= "host",
	[USB_DR_MODE_PERIPHERAL]	= "peripheral",
	[USB_DR_MODE_OTG]		= "otg",
};

static enum usb_dr_mode usb_get_dr_mode_from_string(const char *str)
{
	int ret;

	ret = match_string(usb_dr_modes, ARRAY_SIZE(usb_dr_modes), str);
	return (ret < 0) ? USB_DR_MODE_UNKNOWN : ret;
}

enum usb_dr_mode usb_get_dr_mode(struct device *dev)
{
	const char *dr_mode;
	int err;

	err = device_property_read_string(dev, "dr_mode", &dr_mode);
	if (err < 0)
		return USB_DR_MODE_UNKNOWN;

	return usb_get_dr_mode_from_string(dr_mode);
}
EXPORT_SYMBOL_GPL(usb_get_dr_mode);

#ifdef CONFIG_OF
/**
 * of_usb_get_dr_mode_by_phy - Get dual role mode for the controller device
 * which is associated with the given phy device_node
 * @np:	Pointer to the given phy device_node
 * @arg0: phandle args[0] for phy's with #phy-cells >= 1, or -1 for
 *        phys which do not have phy-cells
 *
 * In dts a usb controller associates with phy devices.  The function gets
 * the string from property 'dr_mode' of the controller associated with the
 * given phy device node, and returns the correspondig enum usb_dr_mode.
 */
enum usb_dr_mode of_usb_get_dr_mode_by_phy(struct device_node *np, int arg0)
{
	struct device_node *controller = NULL;
	struct of_phandle_args args;
	const char *dr_mode;
	int index;
	int err;

	do {
		controller = of_find_node_with_property(controller, "phys");
		index = 0;
		do {
			if (arg0 == -1) {
				args.np = of_parse_phandle(controller, "phys",
							index);
				args.args_count = 0;
			} else {
				err = of_parse_phandle_with_args(controller,
							"phys", "#phy-cells",
							index, &args);
				if (err)
					break;
			}

			of_node_put(args.np);
			if (args.np == np && (args.args_count == 0 ||
					      args.args[0] == arg0))
				goto finish;
			index++;
		} while (args.np);
	} while (controller);

finish:
	err = of_property_read_string(controller, "dr_mode", &dr_mode);
	of_node_put(controller);

	if (err < 0)
		return USB_DR_MODE_UNKNOWN;

	return usb_get_dr_mode_from_string(dr_mode);
}
EXPORT_SYMBOL_GPL(of_usb_get_dr_mode_by_phy);

/**
 * of_usb_host_tpl_support - to get if Targeted Peripheral List is supported
 * for given targeted hosts (non-PC hosts)
 * @np: Pointer to the given device_node
 *
 * The function gets if the targeted hosts support TPL or not
 */
bool of_usb_host_tpl_support(struct device_node *np)
{
	if (of_find_property(np, "tpl-support", NULL))
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(of_usb_host_tpl_support);

/**
 * of_usb_update_otg_caps - to update usb otg capabilities according to
 * the passed properties in DT.
 * @np: Pointer to the given device_node
 * @otg_caps: Pointer to the target usb_otg_caps to be set
 *
 * The function updates the otg capabilities
 */
int of_usb_update_otg_caps(struct device_node *np,
			struct usb_otg_caps *otg_caps)
{
	u32 otg_rev;

	if (!otg_caps)
		return -EINVAL;

	if (!of_property_read_u32(np, "otg-rev", &otg_rev)) {
		switch (otg_rev) {
		case 0x0100:
		case 0x0120:
		case 0x0130:
		case 0x0200:
			/* Choose the lesser one if it's already been set */
			if (otg_caps->otg_rev)
				otg_caps->otg_rev = min_t(u16, otg_rev,
							otg_caps->otg_rev);
			else
				otg_caps->otg_rev = otg_rev;
			break;
		default:
			pr_err("%s: unsupported otg-rev: 0x%x\n",
						np->full_name, otg_rev);
			return -EINVAL;
		}
	} else {
		/*
		 * otg-rev is mandatory for otg properties, if not passed
		 * we set it to be 0 and assume it's a legacy otg device.
		 * Non-dt platform can set it afterwards.
		 */
		otg_caps->otg_rev = 0;
	}

	if (of_find_property(np, "hnp-disable", NULL))
		otg_caps->hnp_support = false;
	if (of_find_property(np, "srp-disable", NULL))
		otg_caps->srp_support = false;
	if (of_find_property(np, "adp-disable", NULL) ||
				(otg_caps->otg_rev < 0x0200))
		otg_caps->adp_support = false;

	return 0;
}
EXPORT_SYMBOL_GPL(of_usb_update_otg_caps);

#endif

MODULE_LICENSE("GPL");
