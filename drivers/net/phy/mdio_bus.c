/* MDIO Bus interface
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#include <asm/irq.h>

int mdiobus_register_device(struct mdio_device *mdiodev)
{
	if (mdiodev->bus->mdio_map[mdiodev->addr])
		return -EBUSY;

	mdiodev->bus->mdio_map[mdiodev->addr] = mdiodev;

	return 0;
}
EXPORT_SYMBOL(mdiobus_register_device);

int mdiobus_unregister_device(struct mdio_device *mdiodev)
{
	if (mdiodev->bus->mdio_map[mdiodev->addr] != mdiodev)
		return -EINVAL;

	mdiodev->bus->mdio_map[mdiodev->addr] = NULL;

	return 0;
}
EXPORT_SYMBOL(mdiobus_unregister_device);

struct phy_device *mdiobus_get_phy(struct mii_bus *bus, int addr)
{
	struct mdio_device *mdiodev = bus->mdio_map[addr];

	if (!mdiodev)
		return NULL;

	if (!(mdiodev->flags & MDIO_DEVICE_FLAG_PHY))
		return NULL;

	return container_of(mdiodev, struct phy_device, mdio);
}
EXPORT_SYMBOL(mdiobus_get_phy);

bool mdiobus_is_registered_device(struct mii_bus *bus, int addr)
{
	return bus->mdio_map[addr];
}
EXPORT_SYMBOL(mdiobus_is_registered_device);

/**
 * mdiobus_alloc_size - allocate a mii_bus structure
 * @size: extra amount of memory to allocate for private storage.
 * If non-zero, then bus->priv is points to that memory.
 *
 * Description: called by a bus driver to allocate an mii_bus
 * structure to fill in.
 */
struct mii_bus *mdiobus_alloc_size(size_t size)
{
	struct mii_bus *bus;
	size_t aligned_size = ALIGN(sizeof(*bus), NETDEV_ALIGN);
	size_t alloc_size;
	int i;

	/* If we alloc extra space, it should be aligned */
	if (size)
		alloc_size = aligned_size + size;
	else
		alloc_size = sizeof(*bus);

	bus = kzalloc(alloc_size, GFP_KERNEL);
	if (!bus)
		return NULL;

	bus->state = MDIOBUS_ALLOCATED;
	if (size)
		bus->priv = (void *)bus + aligned_size;

	/* Initialise the interrupts to polling */
	for (i = 0; i < PHY_MAX_ADDR; i++)
		bus->irq[i] = PHY_POLL;

	return bus;
}
EXPORT_SYMBOL(mdiobus_alloc_size);

static void _devm_mdiobus_free(struct device *dev, void *res)
{
	mdiobus_free(*(struct mii_bus **)res);
}

static int devm_mdiobus_match(struct device *dev, void *res, void *data)
{
	struct mii_bus **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

/**
 * devm_mdiobus_alloc_size - Resource-managed mdiobus_alloc_size()
 * @dev:		Device to allocate mii_bus for
 * @sizeof_priv:	Space to allocate for private structure.
 *
 * Managed mdiobus_alloc_size. mii_bus allocated with this function is
 * automatically freed on driver detach.
 *
 * If an mii_bus allocated with this function needs to be freed separately,
 * devm_mdiobus_free() must be used.
 *
 * RETURNS:
 * Pointer to allocated mii_bus on success, NULL on failure.
 */
struct mii_bus *devm_mdiobus_alloc_size(struct device *dev, int sizeof_priv)
{
	struct mii_bus **ptr, *bus;

	ptr = devres_alloc(_devm_mdiobus_free, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	/* use raw alloc_dr for kmalloc caller tracing */
	bus = mdiobus_alloc_size(sizeof_priv);
	if (bus) {
		*ptr = bus;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return bus;
}
EXPORT_SYMBOL_GPL(devm_mdiobus_alloc_size);

/**
 * devm_mdiobus_free - Resource-managed mdiobus_free()
 * @dev:		Device this mii_bus belongs to
 * @bus:		the mii_bus associated with the device
 *
 * Free mii_bus allocated with devm_mdiobus_alloc_size().
 */
void devm_mdiobus_free(struct device *dev, struct mii_bus *bus)
{
	int rc;

	rc = devres_release(dev, _devm_mdiobus_free,
			    devm_mdiobus_match, bus);
	WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_mdiobus_free);

/**
 * mdiobus_release - mii_bus device release callback
 * @d: the target struct device that contains the mii_bus
 *
 * Description: called when the last reference to an mii_bus is
 * dropped, to free the underlying memory.
 */
static void mdiobus_release(struct device *d)
{
	struct mii_bus *bus = to_mii_bus(d);
	BUG_ON(bus->state != MDIOBUS_RELEASED &&
	       /* for compatibility with error handling in drivers */
	       bus->state != MDIOBUS_ALLOCATED);
	kfree(bus);
}

static struct class mdio_bus_class = {
	.name		= "mdio_bus",
	.dev_release	= mdiobus_release,
};

#if IS_ENABLED(CONFIG_OF_MDIO)
/* Helper function for of_mdio_find_bus */
static int of_mdio_bus_match(struct device *dev, const void *mdio_bus_np)
{
	return dev->of_node == mdio_bus_np;
}
/**
 * of_mdio_find_bus - Given an mii_bus node, find the mii_bus.
 * @mdio_bus_np: Pointer to the mii_bus.
 *
 * Returns a reference to the mii_bus, or NULL if none found.  The
 * embedded struct device will have its reference count incremented,
 * and this must be put once the bus is finished with.
 *
 * Because the association of a device_node and mii_bus is made via
 * of_mdiobus_register(), the mii_bus cannot be found before it is
 * registered with of_mdiobus_register().
 *
 */
struct mii_bus *of_mdio_find_bus(struct device_node *mdio_bus_np)
{
	struct device *d;

	if (!mdio_bus_np)
		return NULL;

	d = class_find_device(&mdio_bus_class, NULL,  mdio_bus_np,
			      of_mdio_bus_match);

	return d ? to_mii_bus(d) : NULL;
}
EXPORT_SYMBOL(of_mdio_find_bus);

/* Walk the list of subnodes of a mdio bus and look for a node that
 * matches the mdio device's address with its 'reg' property. If
 * found, set the of_node pointer for the mdio device. This allows
 * auto-probed phy devices to be supplied with information passed in
 * via DT.
 */
static void of_mdiobus_link_mdiodev(struct mii_bus *bus,
				    struct mdio_device *mdiodev)
{
	struct device *dev = &mdiodev->dev;
	struct device_node *child;

	if (dev->of_node || !bus->dev.of_node)
		return;

	for_each_available_child_of_node(bus->dev.of_node, child) {
		int addr;
		int ret;

		ret = of_property_read_u32(child, "reg", &addr);
		if (ret < 0) {
			dev_err(dev, "%s has invalid MDIO address\n",
				child->full_name);
			continue;
		}

		/* A MDIO device must have a reg property in the range [0-31] */
		if (addr >= PHY_MAX_ADDR) {
			dev_err(dev, "%s MDIO address %i is too large\n",
				child->full_name, addr);
			continue;
		}

		if (addr == mdiodev->addr) {
			dev->of_node = child;
			return;
		}
	}
}
#else /* !IS_ENABLED(CONFIG_OF_MDIO) */
static inline void of_mdiobus_link_mdiodev(struct mii_bus *mdio,
					   struct mdio_device *mdiodev)
{
}
#endif

/**
 * __mdiobus_register - bring up all the PHYs on a given bus and attach them to bus
 * @bus: target mii_bus
 * @owner: module containing bus accessor functions
 *
 * Description: Called by a bus driver to bring up all the PHYs
 *   on a given bus, and attach them to the bus. Drivers should use
 *   mdiobus_register() rather than __mdiobus_register() unless they
 *   need to pass a specific owner module. MDIO devices which are not
 *   PHYs will not be brought up by this function. They are expected to
 *   to be explicitly listed in DT and instantiated by of_mdiobus_register().
 *
 * Returns 0 on success or < 0 on error.
 */
int __mdiobus_register(struct mii_bus *bus, struct module *owner)
{
	struct mdio_device *mdiodev;
	int i, err;

	if (NULL == bus || NULL == bus->name ||
	    NULL == bus->read || NULL == bus->write)
		return -EINVAL;

	BUG_ON(bus->state != MDIOBUS_ALLOCATED &&
	       bus->state != MDIOBUS_UNREGISTERED);

	bus->owner = owner;
	bus->dev.parent = bus->parent;
	bus->dev.class = &mdio_bus_class;
	bus->dev.groups = NULL;
	dev_set_name(&bus->dev, "%s", bus->id);

	err = device_register(&bus->dev);
	if (err) {
		pr_err("mii_bus %s failed to register\n", bus->id);
		put_device(&bus->dev);
		return -EINVAL;
	}

	mutex_init(&bus->mdio_lock);

	if (bus->reset)
		bus->reset(bus);

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		if ((bus->phy_mask & (1 << i)) == 0) {
			struct phy_device *phydev;

			phydev = mdiobus_scan(bus, i);
			if (IS_ERR(phydev) && (PTR_ERR(phydev) != -ENODEV)) {
				err = PTR_ERR(phydev);
				goto error;
			}
		}
	}

	bus->state = MDIOBUS_REGISTERED;
	pr_info("%s: probed\n", bus->name);
	return 0;

error:
	while (--i >= 0) {
		mdiodev = bus->mdio_map[i];
		if (!mdiodev)
			continue;

		mdiodev->device_remove(mdiodev);
		mdiodev->device_free(mdiodev);
	}
	device_del(&bus->dev);
	return err;
}
EXPORT_SYMBOL(__mdiobus_register);

void mdiobus_unregister(struct mii_bus *bus)
{
	struct mdio_device *mdiodev;
	int i;

	BUG_ON(bus->state != MDIOBUS_REGISTERED);
	bus->state = MDIOBUS_UNREGISTERED;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		mdiodev = bus->mdio_map[i];
		if (!mdiodev)
			continue;

		mdiodev->device_remove(mdiodev);
		mdiodev->device_free(mdiodev);
	}
	device_del(&bus->dev);
}
EXPORT_SYMBOL(mdiobus_unregister);

/**
 * mdiobus_free - free a struct mii_bus
 * @bus: mii_bus to free
 *
 * This function releases the reference to the underlying device
 * object in the mii_bus.  If this is the last reference, the mii_bus
 * will be freed.
 */
void mdiobus_free(struct mii_bus *bus)
{
	/* For compatibility with error handling in drivers. */
	if (bus->state == MDIOBUS_ALLOCATED) {
		kfree(bus);
		return;
	}

	BUG_ON(bus->state != MDIOBUS_UNREGISTERED);
	bus->state = MDIOBUS_RELEASED;

	put_device(&bus->dev);
}
EXPORT_SYMBOL(mdiobus_free);

/**
 * mdiobus_scan - scan a bus for MDIO devices.
 * @bus: mii_bus to scan
 * @addr: address on bus to scan
 *
 * This function scans the MDIO bus, looking for devices which can be
 * identified using a vendor/product ID in registers 2 and 3. Not all
 * MDIO devices have such registers, but PHY devices typically
 * do. Hence this function assumes anything found is a PHY, or can be
 * treated as a PHY. Other MDIO devices, such as switches, will
 * probably not be found during the scan.
 */
struct phy_device *mdiobus_scan(struct mii_bus *bus, int addr)
{
	struct phy_device *phydev;
	int err;

	phydev = get_phy_device(bus, addr, false);
	if (IS_ERR(phydev))
		return phydev;

	/*
	 * For DT, see if the auto-probed phy has a correspoding child
	 * in the bus node, and set the of_node pointer in this case.
	 */
	of_mdiobus_link_mdiodev(bus, &phydev->mdio);

	err = phy_device_register(phydev);
	if (err) {
		phy_device_free(phydev);
		return ERR_PTR(-ENODEV);
	}

	return phydev;
}
EXPORT_SYMBOL(mdiobus_scan);

/**
 * mdiobus_read_nested - Nested version of the mdiobus_read function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_read_nested(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	BUG_ON(in_interrupt());

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	retval = bus->read(bus, addr, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_read_nested);

/**
 * mdiobus_read - Convenience function for reading a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to read
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_read(struct mii_bus *bus, int addr, u32 regnum)
{
	int retval;

	BUG_ON(in_interrupt());

	mutex_lock(&bus->mdio_lock);
	retval = bus->read(bus, addr, regnum);
	mutex_unlock(&bus->mdio_lock);

	return retval;
}
EXPORT_SYMBOL(mdiobus_read);

/**
 * mdiobus_write_nested - Nested version of the mdiobus_write function
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * In case of nested MDIO bus access avoid lockdep false positives by
 * using mutex_lock_nested().
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_write_nested(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	BUG_ON(in_interrupt());

	mutex_lock_nested(&bus->mdio_lock, MDIO_MUTEX_NESTED);
	err = bus->write(bus, addr, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_write_nested);

/**
 * mdiobus_write - Convenience function for writing a given MII mgmt register
 * @bus: the mii_bus struct
 * @addr: the phy address
 * @regnum: register number to write
 * @val: value to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int mdiobus_write(struct mii_bus *bus, int addr, u32 regnum, u16 val)
{
	int err;

	BUG_ON(in_interrupt());

	mutex_lock(&bus->mdio_lock);
	err = bus->write(bus, addr, regnum, val);
	mutex_unlock(&bus->mdio_lock);

	return err;
}
EXPORT_SYMBOL(mdiobus_write);

/**
 * mdio_bus_match - determine if given MDIO driver supports the given
 *		    MDIO device
 * @dev: target MDIO device
 * @drv: given MDIO driver
 *
 * Description: Given a MDIO device, and a MDIO driver, return 1 if
 *   the driver supports the device.  Otherwise, return 0. This may
 *   require calling the devices own match function, since different classes
 *   of MDIO devices have different match criteria.
 */
static int mdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct mdio_device *mdio = to_mdio_device(dev);

	if (of_driver_match_device(dev, drv))
		return 1;

	if (mdio->bus_match)
		return mdio->bus_match(dev, drv);

	return 0;
}

#ifdef CONFIG_PM
static int mdio_bus_suspend(struct device *dev)
{
	struct mdio_device *mdio = to_mdio_device(dev);

	if (mdio->pm_ops && mdio->pm_ops->suspend)
		return mdio->pm_ops->suspend(dev);

	return 0;
}

static int mdio_bus_resume(struct device *dev)
{
	struct mdio_device *mdio = to_mdio_device(dev);

	if (mdio->pm_ops && mdio->pm_ops->resume)
		return mdio->pm_ops->resume(dev);

	return 0;
}

static int mdio_bus_restore(struct device *dev)
{
	struct mdio_device *mdio = to_mdio_device(dev);

	if (mdio->pm_ops && mdio->pm_ops->restore)
		return mdio->pm_ops->restore(dev);

	return 0;
}

static const struct dev_pm_ops mdio_bus_pm_ops = {
	.suspend = mdio_bus_suspend,
	.resume = mdio_bus_resume,
	.freeze = mdio_bus_suspend,
	.thaw = mdio_bus_resume,
	.restore = mdio_bus_restore,
};

#define MDIO_BUS_PM_OPS (&mdio_bus_pm_ops)

#else

#define MDIO_BUS_PM_OPS NULL

#endif /* CONFIG_PM */

struct bus_type mdio_bus_type = {
	.name		= "mdio_bus",
	.match		= mdio_bus_match,
	.pm		= MDIO_BUS_PM_OPS,
};
EXPORT_SYMBOL(mdio_bus_type);

int __init mdio_bus_init(void)
{
	int ret;

	ret = class_register(&mdio_bus_class);
	if (!ret) {
		ret = bus_register(&mdio_bus_type);
		if (ret)
			class_unregister(&mdio_bus_class);
	}

	return ret;
}

void mdio_bus_exit(void)
{
	class_unregister(&mdio_bus_class);
	bus_unregister(&mdio_bus_type);
}
