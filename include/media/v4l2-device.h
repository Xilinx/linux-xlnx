/*
    V4L2 device support header.

    Copyright (C) 2008  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _V4L2_DEVICE_H
#define _V4L2_DEVICE_H

#include <media/media-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dev.h>

#define V4L2_DEVICE_NAME_SIZE (20 + 16)

struct v4l2_ctrl_handler;

/**
 * struct v4l2_device - main struct to for V4L2 device drivers
 *
 * @dev: pointer to struct device.
 * @mdev: pointer to struct media_device
 * @subdevs: used to keep track of the registered subdevs
 * @lock: lock this struct; can be used by the driver as well
 *	if this struct is embedded into a larger struct.
 * @name: unique device name, by default the driver name + bus ID
 * @notify: notify callback called by some sub-devices.
 * @ctrl_handler: The control handler. May be %NULL.
 * @prio: Device's priority state
 * @ref: Keep track of the references to this struct.
 * @release: Release function that is called when the ref count
 *	goes to 0.
 *
 * Each instance of a V4L2 device should create the v4l2_device struct,
 * either stand-alone or embedded in a larger struct.
 *
 * It allows easy access to sub-devices (see v4l2-subdev.h) and provides
 * basic V4L2 device-level support.
 *
 * .. note::
 *
 *    #) @dev->driver_data points to this struct.
 *    #) @dev might be %NULL if there is no parent device
 */

struct v4l2_device {
	struct device *dev;
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_device *mdev;
#endif
	struct list_head subdevs;
	spinlock_t lock;
	char name[V4L2_DEVICE_NAME_SIZE];
	void (*notify)(struct v4l2_subdev *sd,
			unsigned int notification, void *arg);
	struct v4l2_ctrl_handler *ctrl_handler;
	struct v4l2_prio_state prio;
	struct kref ref;
	void (*release)(struct v4l2_device *v4l2_dev);
};

/**
 * v4l2_device_get - gets a V4L2 device reference
 *
 * @v4l2_dev: pointer to struct &v4l2_device
 *
 * This is an ancillary routine meant to increment the usage for the
 * struct &v4l2_device pointed by @v4l2_dev.
 */
static inline void v4l2_device_get(struct v4l2_device *v4l2_dev)
{
	kref_get(&v4l2_dev->ref);
}

/**
 * v4l2_device_put - putss a V4L2 device reference
 *
 * @v4l2_dev: pointer to struct &v4l2_device
 *
 * This is an ancillary routine meant to decrement the usage for the
 * struct &v4l2_device pointed by @v4l2_dev.
 */
int v4l2_device_put(struct v4l2_device *v4l2_dev);

/**
 * v4l2_device_register - Initialize v4l2_dev and make @dev->driver_data
 *	point to @v4l2_dev.
 *
 * @dev: pointer to struct &device
 * @v4l2_dev: pointer to struct &v4l2_device
 *
 * .. note::
 *	@dev may be %NULL in rare cases (ISA devices).
 *	In such case the caller must fill in the @v4l2_dev->name field
 *	before calling this function.
 */
int __must_check v4l2_device_register(struct device *dev,
				      struct v4l2_device *v4l2_dev);

/**
 * v4l2_device_set_name - Optional function to initialize the
 *	name field of struct &v4l2_device
 *
 * @v4l2_dev: pointer to struct &v4l2_device
 * @basename: base name for the device name
 * @instance: pointer to a static atomic_t var with the instance usage for
 *	the device driver.
 *
 * v4l2_device_set_name() initializes the name field of struct &v4l2_device
 * using the driver name and a driver-global atomic_t instance.
 *
 * This function will increment the instance counter and returns the
 * instance value used in the name.
 *
 * Example:
 *
 *   static atomic_t drv_instance = ATOMIC_INIT(0);
 *
 *   ...
 *
 *   instance = v4l2_device_set_name(&\ v4l2_dev, "foo", &\ drv_instance);
 *
 * The first time this is called the name field will be set to foo0 and
 * this function returns 0. If the name ends with a digit (e.g. cx18),
 * then the name will be set to cx18-0 since cx180 would look really odd.
 */
int v4l2_device_set_name(struct v4l2_device *v4l2_dev, const char *basename,
			 atomic_t *instance);

/**
 * v4l2_device_disconnect - Change V4L2 device state to disconnected.
 *
 * @v4l2_dev: pointer to struct v4l2_device
 *
 * Should be called when the USB parent disconnects.
 * Since the parent disappears, this ensures that @v4l2_dev doesn't have
 * an invalid parent pointer.
 *
 * .. note:: This function sets @v4l2_dev->dev to NULL.
 */
void v4l2_device_disconnect(struct v4l2_device *v4l2_dev);

/**
 *  v4l2_device_unregister - Unregister all sub-devices and any other
 *	 resources related to @v4l2_dev.
 *
 * @v4l2_dev: pointer to struct v4l2_device
 */
void v4l2_device_unregister(struct v4l2_device *v4l2_dev);

/**
 * v4l2_device_register_subdev - Registers a subdev with a v4l2 device.
 *
 * @v4l2_dev: pointer to struct &v4l2_device
 * @sd: pointer to struct &v4l2_subdev
 *
 * While registered, the subdev module is marked as in-use.
 *
 * An error is returned if the module is no longer loaded on any attempts
 * to register it.
 */
int __must_check v4l2_device_register_subdev(struct v4l2_device *v4l2_dev,
					     struct v4l2_subdev *sd);

/**
 * v4l2_device_unregister_subdev - Unregisters a subdev with a v4l2 device.
 *
 * @sd: pointer to struct &v4l2_subdev
 *
 * .. note ::
 *
 *	Can also be called if the subdev wasn't registered. In such
 *	case, it will do nothing.
 */
void v4l2_device_unregister_subdev(struct v4l2_subdev *sd);

/**
 * v4l2_device_register_subdev_nodes - Registers device nodes for all subdevs
 *	of the v4l2 device that are marked with
 *	the %V4L2_SUBDEV_FL_HAS_DEVNODE flag.
 *
 * @v4l2_dev: pointer to struct v4l2_device
 */
int __must_check
v4l2_device_register_subdev_nodes(struct v4l2_device *v4l2_dev);

/**
 * v4l2_subdev_notify - Sends a notification to v4l2_device.
 *
 * @sd: pointer to struct &v4l2_subdev
 * @notification: type of notification. Please notice that the notification
 *	type is driver-specific.
 * @arg: arguments for the notification. Those are specific to each
 *	notification type.
 */
static inline void v4l2_subdev_notify(struct v4l2_subdev *sd,
				      unsigned int notification, void *arg)
{
	if (sd && sd->v4l2_dev && sd->v4l2_dev->notify)
		sd->v4l2_dev->notify(sd, notification, arg);
}

/* Iterate over all subdevs. */
#define v4l2_device_for_each_subdev(sd, v4l2_dev)			\
	list_for_each_entry(sd, &(v4l2_dev)->subdevs, list)

/* Call the specified callback for all subdevs matching the condition.
   Ignore any errors. Note that you cannot add or delete a subdev
   while walking the subdevs list. */
#define __v4l2_device_call_subdevs_p(v4l2_dev, sd, cond, o, f, args...)	\
	do {								\
		list_for_each_entry((sd), &(v4l2_dev)->subdevs, list)	\
			if ((cond) && (sd)->ops->o && (sd)->ops->o->f)	\
				(sd)->ops->o->f((sd) , ##args);		\
	} while (0)

#define __v4l2_device_call_subdevs(v4l2_dev, cond, o, f, args...)	\
	do {								\
		struct v4l2_subdev *__sd;				\
									\
		__v4l2_device_call_subdevs_p(v4l2_dev, __sd, cond, o,	\
						f , ##args);		\
	} while (0)

/* Call the specified callback for all subdevs matching the condition.
   If the callback returns an error other than 0 or -ENOIOCTLCMD, then
   return with that error code. Note that you cannot add or delete a
   subdev while walking the subdevs list. */
#define __v4l2_device_call_subdevs_until_err_p(v4l2_dev, sd, cond, o, f, args...) \
({									\
	long __err = 0;							\
									\
	list_for_each_entry((sd), &(v4l2_dev)->subdevs, list) {		\
		if ((cond) && (sd)->ops->o && (sd)->ops->o->f)		\
			__err = (sd)->ops->o->f((sd) , ##args);		\
		if (__err && __err != -ENOIOCTLCMD)			\
			break;						\
	}								\
	(__err == -ENOIOCTLCMD) ? 0 : __err;				\
})

#define __v4l2_device_call_subdevs_until_err(v4l2_dev, cond, o, f, args...) \
({									\
	struct v4l2_subdev *__sd;					\
	__v4l2_device_call_subdevs_until_err_p(v4l2_dev, __sd, cond, o,	\
						f , ##args);		\
})

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). Ignore any errors. Note that you cannot add or delete
   a subdev while walking the subdevs list. */
#define v4l2_device_call_all(v4l2_dev, grpid, o, f, args...)		\
	do {								\
		struct v4l2_subdev *__sd;				\
									\
		__v4l2_device_call_subdevs_p(v4l2_dev, __sd,		\
			!(grpid) || __sd->grp_id == (grpid), o, f ,	\
			##args);					\
	} while (0)

/* Call the specified callback for all subdevs matching grp_id (if 0, then
   match them all). If the callback returns an error other than 0 or
   -ENOIOCTLCMD, then return with that error code. Note that you cannot
   add or delete a subdev while walking the subdevs list. */
#define v4l2_device_call_until_err(v4l2_dev, grpid, o, f, args...)	\
({									\
	struct v4l2_subdev *__sd;					\
	__v4l2_device_call_subdevs_until_err_p(v4l2_dev, __sd,		\
			!(grpid) || __sd->grp_id == (grpid), o, f ,	\
			##args);					\
})

/*
 * Call the specified callback for all subdevs where grp_id & grpmsk != 0
 * (if grpmsk == `0, then match them all). Ignore any errors. Note that you
 * cannot add or delete a subdev while walking the subdevs list.
 */
#define v4l2_device_mask_call_all(v4l2_dev, grpmsk, o, f, args...)	\
	do {								\
		struct v4l2_subdev *__sd;				\
									\
		__v4l2_device_call_subdevs_p(v4l2_dev, __sd,		\
			!(grpmsk) || (__sd->grp_id & (grpmsk)), o, f ,	\
			##args);					\
	} while (0)

/*
 * Call the specified callback for all subdevs where grp_id & grpmsk != 0
 * (if grpmsk == 0, then match them all). If the callback returns an error
 * other than 0 or %-ENOIOCTLCMD, then return with that error code. Note that
 * you cannot add or delete a subdev while walking the subdevs list.
 */
#define v4l2_device_mask_call_until_err(v4l2_dev, grpmsk, o, f, args...) \
({									\
	struct v4l2_subdev *__sd;					\
	__v4l2_device_call_subdevs_until_err_p(v4l2_dev, __sd,		\
			!(grpmsk) || (__sd->grp_id & (grpmsk)), o, f ,	\
			##args);					\
})

/*
 * Does any subdev with matching grpid (or all if grpid == 0) has the given
 * op?
 */
#define v4l2_device_has_op(v4l2_dev, grpid, o, f)			\
({									\
	struct v4l2_subdev *__sd;					\
	bool __result = false;						\
	list_for_each_entry(__sd, &(v4l2_dev)->subdevs, list) {		\
		if ((grpid) && __sd->grp_id != (grpid))			\
			continue;					\
		if (v4l2_subdev_has_op(__sd, o, f)) {			\
			__result = true;				\
			break;						\
		}							\
	}								\
	__result;							\
})

/*
 * Does any subdev with matching grpmsk (or all if grpmsk == 0) has the given
 * op?
 */
#define v4l2_device_mask_has_op(v4l2_dev, grpmsk, o, f)			\
({									\
	struct v4l2_subdev *__sd;					\
	bool __result = false;						\
	list_for_each_entry(__sd, &(v4l2_dev)->subdevs, list) {		\
		if ((grpmsk) && !(__sd->grp_id & (grpmsk)))		\
			continue;					\
		if (v4l2_subdev_has_op(__sd, o, f)) {			\
			__result = true;				\
			break;						\
		}							\
	}								\
	__result;							\
})

#endif
