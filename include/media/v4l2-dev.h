/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 * Moved from videodev2.h
 *
 *	Some commonly needed functions for drivers (v4l2-common.o module)
 */
#ifndef _V4L2_DEV_H
#define _V4L2_DEV_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>

#define VIDEO_MAJOR	81

#define VFL_TYPE_GRABBER	0
#define VFL_TYPE_VBI		1
#define VFL_TYPE_RADIO		2
#define VFL_TYPE_SUBDEV		3
#define VFL_TYPE_SDR		4
#define VFL_TYPE_TOUCH		5
#define VFL_TYPE_MAX		6

/* Is this a receiver, transmitter or mem-to-mem? */
/* Ignored for VFL_TYPE_SUBDEV. */
#define VFL_DIR_RX		0
#define VFL_DIR_TX		1
#define VFL_DIR_M2M		2

struct v4l2_ioctl_callbacks;
struct video_device;
struct v4l2_device;
struct v4l2_ctrl_handler;

/* Flag to mark the video_device struct as registered.
   Drivers can clear this flag if they want to block all future
   device access. It is cleared by video_unregister_device. */
#define V4L2_FL_REGISTERED	(0)
/* file->private_data points to struct v4l2_fh */
#define V4L2_FL_USES_V4L2_FH	(1)

/* Priority helper functions */

/**
 * struct v4l2_prio_state - stores the priority states
 *
 * @prios: array with elements to store the array priorities
 *
 *
 * .. note::
 *    The size of @prios array matches the number of priority types defined
 *    by enum &v4l2_priority.
 */
struct v4l2_prio_state {
	atomic_t prios[4];
};

/**
 * v4l2_prio_init - initializes a struct v4l2_prio_state
 *
 * @global: pointer to &struct v4l2_prio_state
 */
void v4l2_prio_init(struct v4l2_prio_state *global);

/**
 * v4l2_prio_change - changes the v4l2 file handler priority
 *
 * @global: pointer to the &struct v4l2_prio_state of the device node.
 * @local: pointer to the desired priority, as defined by enum &v4l2_priority
 * @new: Priority type requested, as defined by enum &v4l2_priority.
 *
 * .. note::
 *	This function should be used only by the V4L2 core.
 */
int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new);

/**
 * v4l2_prio_open - Implements the priority logic for a file handler open
 *
 * @global: pointer to the &struct v4l2_prio_state of the device node.
 * @local: pointer to the desired priority, as defined by enum &v4l2_priority
 *
 * .. note::
 *	This function should be used only by the V4L2 core.
 */
void v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local);

/**
 * v4l2_prio_close - Implements the priority logic for a file handler close
 *
 * @global: pointer to the &struct v4l2_prio_state of the device node.
 * @local: priority to be released, as defined by enum &v4l2_priority
 *
 * .. note::
 *	This function should be used only by the V4L2 core.
 */
void v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority local);

/**
 * v4l2_prio_max - Return the maximum priority, as stored at the @global array.
 *
 * @global: pointer to the &struct v4l2_prio_state of the device node.
 *
 * .. note::
 *	This function should be used only by the V4L2 core.
 */
enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global);

/**
 * v4l2_prio_check - Implements the priority logic for a file handler close
 *
 * @global: pointer to the &struct v4l2_prio_state of the device node.
 * @local: desired priority, as defined by enum &v4l2_priority local
 *
 * .. note::
 *	This function should be used only by the V4L2 core.
 */
int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority local);

/**
 * struct v4l2_file_operations - fs operations used by a V4L2 device
 *
 * @owner: pointer to struct module
 * @read: operations needed to implement the read() syscall
 * @write: operations needed to implement the write() syscall
 * @poll: operations needed to implement the poll() syscall
 * @unlocked_ioctl: operations needed to implement the ioctl() syscall
 * @compat_ioctl32: operations needed to implement the ioctl() syscall for
 *	the special case where the Kernel uses 64 bits instructions, but
 *	the userspace uses 32 bits.
 * @get_unmapped_area: called by the mmap() syscall, used when %!CONFIG_MMU
 * @mmap: operations needed to implement the mmap() syscall
 * @open: operations needed to implement the open() syscall
 * @release: operations needed to implement the release() syscall
 *
 * .. note::
 *
 *	Those operations are used to implemente the fs struct file_operations
 *	at the V4L2 drivers. The V4L2 core overrides the fs ops with some
 *	extra logic needed by the subsystem.
 */
struct v4l2_file_operations {
	struct module *owner;
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
#ifdef CONFIG_COMPAT
	long (*compat_ioctl32) (struct file *, unsigned int, unsigned long);
#endif
	unsigned long (*get_unmapped_area) (struct file *, unsigned long,
				unsigned long, unsigned long, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct file *);
	int (*release) (struct file *);
};

/*
 * Newer version of video_device, handled by videodev2.c
 * 	This version moves redundant code from video device code to
 *	the common handler
 */

/**
 * struct video_device - Structure used to create and manage the V4L2 device
 *	nodes.
 *
 * @entity: &struct media_entity
 * @intf_devnode: pointer to &struct media_intf_devnode
 * @pipe: &struct media_pipeline
 * @fops: pointer to &struct v4l2_file_operations for the video device
 * @device_caps: device capabilities as used in v4l2_capabilities
 * @dev: &struct device for the video device
 * @cdev: character device
 * @v4l2_dev: pointer to &struct v4l2_device parent
 * @dev_parent: pointer to &struct device parent
 * @ctrl_handler: Control handler associated with this device node.
 *	 May be NULL.
 * @queue: &struct vb2_queue associated with this device node. May be NULL.
 * @prio: pointer to &struct v4l2_prio_state with device's Priority state.
 *	 If NULL, then v4l2_dev->prio will be used.
 * @name: video device name
 * @vfl_type: V4L device type
 * @vfl_dir: V4L receiver, transmitter or m2m
 * @minor: device node 'minor'. It is set to -1 if the registration failed
 * @num: number of the video device node
 * @flags: video device flags. Use bitops to set/clear/test flags
 * @index: attribute to differentiate multiple indices on one physical device
 * @fh_lock: Lock for all v4l2_fhs
 * @fh_list: List of &struct v4l2_fh
 * @dev_debug: Internal device debug flags, not for use by drivers
 * @tvnorms: Supported tv norms
 *
 * @release: video device release() callback
 * @ioctl_ops: pointer to &struct v4l2_ioctl_ops with ioctl callbacks
 *
 * @valid_ioctls: bitmap with the valid ioctls for this device
 * @disable_locking: bitmap with the ioctls that don't require locking
 * @lock: pointer to &struct mutex serialization lock
 *
 * .. note::
 *	Only set @dev_parent if that can't be deduced from @v4l2_dev.
 */

struct video_device
{
#if defined(CONFIG_MEDIA_CONTROLLER)
	struct media_entity entity;
	struct media_intf_devnode *intf_devnode;
	struct media_pipeline pipe;
#endif
	const struct v4l2_file_operations *fops;

	u32 device_caps;

	/* sysfs */
	struct device dev;
	struct cdev *cdev;

	struct v4l2_device *v4l2_dev;
	struct device *dev_parent;

	struct v4l2_ctrl_handler *ctrl_handler;

	struct vb2_queue *queue;

	struct v4l2_prio_state *prio;

	/* device info */
	char name[32];
	int vfl_type;
	int vfl_dir;
	int minor;
	u16 num;
	unsigned long flags;
	int index;

	/* V4L2 file handles */
	spinlock_t		fh_lock;
	struct list_head	fh_list;

	int dev_debug;

	v4l2_std_id tvnorms;

	/* callbacks */
	void (*release)(struct video_device *vdev);
	const struct v4l2_ioctl_ops *ioctl_ops;
	DECLARE_BITMAP(valid_ioctls, BASE_VIDIOC_PRIVATE);

	DECLARE_BITMAP(disable_locking, BASE_VIDIOC_PRIVATE);
	struct mutex *lock;
};

#define media_entity_to_video_device(__e) \
	container_of(__e, struct video_device, entity)
/* dev to video-device */
#define to_video_device(cd) container_of(cd, struct video_device, dev)

/**
 * __video_register_device - register video4linux devices
 *
 * @vdev: struct video_device to register
 * @type: type of device to register
 * @nr:   which device node number is desired:
 * 	(0 == /dev/video0, 1 == /dev/video1, ..., -1 == first free)
 * @warn_if_nr_in_use: warn if the desired device node number
 *        was already in use and another number was chosen instead.
 * @owner: module that owns the video device node
 *
 * The registration code assigns minor numbers and device node numbers
 * based on the requested type and registers the new device node with
 * the kernel.
 *
 * This function assumes that struct video_device was zeroed when it
 * was allocated and does not contain any stale date.
 *
 * An error is returned if no free minor or device node number could be
 * found, or if the registration of the device node failed.
 *
 * Returns 0 on success.
 *
 * Valid values for @type are:
 *
 *	- %VFL_TYPE_GRABBER - A frame grabber
 *	- %VFL_TYPE_VBI - Vertical blank data (undecoded)
 *	- %VFL_TYPE_RADIO - A radio card
 *	- %VFL_TYPE_SUBDEV - A subdevice
 *	- %VFL_TYPE_SDR - Software Defined Radio
 *	- %VFL_TYPE_TOUCH - A touch sensor
 *
 * .. note::
 *
 *	This function is meant to be used only inside the V4L2 core.
 *	Drivers should use video_register_device() or
 *	video_register_device_no_warn().
 */
int __must_check __video_register_device(struct video_device *vdev, int type,
		int nr, int warn_if_nr_in_use, struct module *owner);

/**
 *  video_register_device - register video4linux devices
 *
 * @vdev: struct video_device to register
 * @type: type of device to register
 * @nr:   which device node number is desired:
 * 	(0 == /dev/video0, 1 == /dev/video1, ..., -1 == first free)
 *
 * Internally, it calls __video_register_device(). Please see its
 * documentation for more details.
 *
 * .. note::
 * 	if video_register_device fails, the release() callback of
 *	&struct video_device structure is *not* called, so the caller
 *	is responsible for freeing any data. Usually that means that
 *	you video_device_release() should be called on failure.
 */
static inline int __must_check video_register_device(struct video_device *vdev,
		int type, int nr)
{
	return __video_register_device(vdev, type, nr, 1, vdev->fops->owner);
}

/**
 *  video_register_device_no_warn - register video4linux devices
 *
 * @vdev: struct video_device to register
 * @type: type of device to register
 * @nr:   which device node number is desired:
 * 	(0 == /dev/video0, 1 == /dev/video1, ..., -1 == first free)
 *
 * This function is identical to video_register_device() except that no
 * warning is issued if the desired device node number was already in use.
 *
 * Internally, it calls __video_register_device(). Please see its
 * documentation for more details.
 *
 * .. note::
 * 	if video_register_device fails, the release() callback of
 *	&struct video_device structure is *not* called, so the caller
 *	is responsible for freeing any data. Usually that means that
 *	you video_device_release() should be called on failure.
 */
static inline int __must_check video_register_device_no_warn(
		struct video_device *vdev, int type, int nr)
{
	return __video_register_device(vdev, type, nr, 0, vdev->fops->owner);
}

/**
 * video_unregister_device - Unregister video devices.
 *
 * @vdev: &struct video_device to register
 *
 * Does nothing if vdev == NULL or if video_is_registered() returns false.
 */
void video_unregister_device(struct video_device *vdev);

/**
 * video_device_alloc - helper function to alloc &struct video_device
 *
 * Returns NULL if %-ENOMEM or a &struct video_device on success.
 */
struct video_device * __must_check video_device_alloc(void);

/**
 * video_device_release - helper function to release &struct video_device
 *
 * @vdev: pointer to &struct video_device
 *
 * Can also be used for video_device->release\(\).
 */
void video_device_release(struct video_device *vdev);

/**
 * video_device_release_empty - helper function to implement the
 * 	video_device->release\(\) callback.
 *
 * @vdev: pointer to &struct video_device
 *
 * This release function does nothing.
 *
 * It should be used when the video_device is a static global struct.
 *
 * .. note::
 *	Having a static video_device is a dubious construction at best.
 */
void video_device_release_empty(struct video_device *vdev);

/**
 * v4l2_is_known_ioctl - Checks if a given cmd is a known V4L ioctl
 *
 * @cmd: ioctl command
 *
 * returns true if cmd is a known V4L2 ioctl
 */
bool v4l2_is_known_ioctl(unsigned int cmd);

/** v4l2_disable_ioctl_locking - mark that a given command
 *	shouldn't use core locking
 *
 * @vdev: pointer to &struct video_device
 * @cmd: ioctl command
 */
static inline void v4l2_disable_ioctl_locking(struct video_device *vdev,
					      unsigned int cmd)
{
	if (_IOC_NR(cmd) < BASE_VIDIOC_PRIVATE)
		set_bit(_IOC_NR(cmd), vdev->disable_locking);
}

/**
 * v4l2_disable_ioctl- mark that a given command isn't implemented.
 *	shouldn't use core locking
 *
 * @vdev: pointer to &struct video_device
 * @cmd: ioctl command
 *
 * This function allows drivers to provide just one v4l2_ioctl_ops struct, but
 * disable ioctls based on the specific card that is actually found.
 *
 * .. note::
 *
 *    This must be called before video_register_device.
 *    See also the comments for determine_valid_ioctls().
 */
static inline void v4l2_disable_ioctl(struct video_device *vdev,
				      unsigned int cmd)
{
	if (_IOC_NR(cmd) < BASE_VIDIOC_PRIVATE)
		set_bit(_IOC_NR(cmd), vdev->valid_ioctls);
}

/**
 * video_get_drvdata - gets private data from &struct video_device.
 *
 * @vdev: pointer to &struct video_device
 *
 * returns a pointer to the private data
 */
static inline void *video_get_drvdata(struct video_device *vdev)
{
	return dev_get_drvdata(&vdev->dev);
}

/**
 * video_set_drvdata - sets private data from &struct video_device.
 *
 * @vdev: pointer to &struct video_device
 * @data: private data pointer
 */
static inline void video_set_drvdata(struct video_device *vdev, void *data)
{
	dev_set_drvdata(&vdev->dev, data);
}

/**
 * video_devdata - gets &struct video_device from struct file.
 *
 * @file: pointer to struct file
 */
struct video_device *video_devdata(struct file *file);

/**
 * video_drvdata - gets private data from &struct video_device using the
 *	struct file.
 *
 * @file: pointer to struct file
 *
 * This is function combines both video_get_drvdata() and video_devdata()
 * as this is used very often.
 */
static inline void *video_drvdata(struct file *file)
{
	return video_get_drvdata(video_devdata(file));
}

/**
 * video_device_node_name - returns the video device name
 *
 * @vdev: pointer to &struct video_device
 *
 * Returns the device name string
 */
static inline const char *video_device_node_name(struct video_device *vdev)
{
	return dev_name(&vdev->dev);
}

/**
 * video_is_registered - returns true if the &struct video_device is registered.
 *
 *
 * @vdev: pointer to &struct video_device
 */
static inline int video_is_registered(struct video_device *vdev)
{
	return test_bit(V4L2_FL_REGISTERED, &vdev->flags);
}

#endif /* _V4L2_DEV_H */
