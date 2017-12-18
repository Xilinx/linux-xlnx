#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/pinctrl/consumer.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/anon_inodes.h>
#include <linux/file.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include <linux/timekeeping.h>
#include <uapi/linux/gpio.h>

#include "gpiolib.h"

#define CREATE_TRACE_POINTS
#include <trace/events/gpio.h>

/* Implementation infrastructure for GPIO interfaces.
 *
 * The GPIO programming interface allows for inlining speed-critical
 * get/set operations for common cases, so that access to SOC-integrated
 * GPIOs can sometimes cost only an instruction or two per bit.
 */


/* When debugging, extend minimal trust to callers and platform code.
 * Also emit diagnostic messages that may help initial bringup, when
 * board setup or driver bugs are most common.
 *
 * Otherwise, minimize overhead in what may be bitbanging codepaths.
 */
#ifdef	DEBUG
#define	extra_checks	1
#else
#define	extra_checks	0
#endif

/* Device and char device-related information */
static DEFINE_IDA(gpio_ida);
static dev_t gpio_devt;
#define GPIO_DEV_MAX 256 /* 256 GPIO chip devices supported */
static struct bus_type gpio_bus_type = {
	.name = "gpio",
};

/* gpio_lock prevents conflicts during gpio_desc[] table updates.
 * While any GPIO is requested, its gpio_chip is not removable;
 * each GPIO's "requested" flag serves as a lock and refcount.
 */
DEFINE_SPINLOCK(gpio_lock);

static DEFINE_MUTEX(gpio_lookup_lock);
static LIST_HEAD(gpio_lookup_list);
LIST_HEAD(gpio_devices);

static void gpiochip_free_hogs(struct gpio_chip *chip);
static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip);
static int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gpiochip);
static void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gpiochip);

static bool gpiolib_initialized;

static inline void desc_set_label(struct gpio_desc *d, const char *label)
{
	d->label = label;
}

/**
 * Convert a GPIO number to its descriptor
 */
struct gpio_desc *gpio_to_desc(unsigned gpio)
{
	struct gpio_device *gdev;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	list_for_each_entry(gdev, &gpio_devices, list) {
		if (gdev->base <= gpio &&
		    gdev->base + gdev->ngpio > gpio) {
			spin_unlock_irqrestore(&gpio_lock, flags);
			return &gdev->descs[gpio - gdev->base];
		}
	}

	spin_unlock_irqrestore(&gpio_lock, flags);

	if (!gpio_is_valid(gpio))
		WARN(1, "invalid GPIO %d\n", gpio);

	return NULL;
}
EXPORT_SYMBOL_GPL(gpio_to_desc);

/**
 * Get the GPIO descriptor corresponding to the given hw number for this chip.
 */
struct gpio_desc *gpiochip_get_desc(struct gpio_chip *chip,
				    u16 hwnum)
{
	struct gpio_device *gdev = chip->gpiodev;

	if (hwnum >= gdev->ngpio)
		return ERR_PTR(-EINVAL);

	return &gdev->descs[hwnum];
}

/**
 * Convert a GPIO descriptor to the integer namespace.
 * This should disappear in the future but is needed since we still
 * use GPIO numbers for error messages and sysfs nodes
 */
int desc_to_gpio(const struct gpio_desc *desc)
{
	return desc->gdev->base + (desc - &desc->gdev->descs[0]);
}
EXPORT_SYMBOL_GPL(desc_to_gpio);


/**
 * gpiod_to_chip - Return the GPIO chip to which a GPIO descriptor belongs
 * @desc:	descriptor to return the chip of
 */
struct gpio_chip *gpiod_to_chip(const struct gpio_desc *desc)
{
	if (!desc || !desc->gdev || !desc->gdev->chip)
		return NULL;
	return desc->gdev->chip;
}
EXPORT_SYMBOL_GPL(gpiod_to_chip);

/* dynamic allocation of GPIOs, e.g. on a hotplugged device */
static int gpiochip_find_base(int ngpio)
{
	struct gpio_device *gdev;
	int base = ARCH_NR_GPIOS - ngpio;

	list_for_each_entry_reverse(gdev, &gpio_devices, list) {
		/* found a free space? */
		if (gdev->base + gdev->ngpio <= base)
			break;
		else
			/* nope, check the space right before the chip */
			base = gdev->base - ngpio;
	}

	if (gpio_is_valid(base)) {
		pr_debug("%s: found new base at %d\n", __func__, base);
		return base;
	} else {
		pr_err("%s: cannot find free range\n", __func__);
		return -ENOSPC;
	}
}

/**
 * gpiod_get_direction - return the current direction of a GPIO
 * @desc:	GPIO to get the direction of
 *
 * Return GPIOF_DIR_IN or GPIOF_DIR_OUT, or an error code in case of error.
 *
 * This function may sleep if gpiod_cansleep() is true.
 */
int gpiod_get_direction(struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	unsigned		offset;
	int			status = -EINVAL;

	chip = gpiod_to_chip(desc);
	offset = gpio_chip_hwgpio(desc);

	if (!chip->get_direction)
		return status;

	status = chip->get_direction(chip, offset);
	if (status > 0) {
		/* GPIOF_DIR_IN, or other positive */
		status = 1;
		clear_bit(FLAG_IS_OUT, &desc->flags);
	}
	if (status == 0) {
		/* GPIOF_DIR_OUT */
		set_bit(FLAG_IS_OUT, &desc->flags);
	}
	return status;
}
EXPORT_SYMBOL_GPL(gpiod_get_direction);

/*
 * Add a new chip to the global chips list, keeping the list of chips sorted
 * by range(means [base, base + ngpio - 1]) order.
 *
 * Return -EBUSY if the new chip overlaps with some other chip's integer
 * space.
 */
static int gpiodev_add_to_list(struct gpio_device *gdev)
{
	struct gpio_device *prev, *next;

	if (list_empty(&gpio_devices)) {
		/* initial entry in list */
		list_add_tail(&gdev->list, &gpio_devices);
		return 0;
	}

	next = list_entry(gpio_devices.next, struct gpio_device, list);
	if (gdev->base + gdev->ngpio <= next->base) {
		/* add before first entry */
		list_add(&gdev->list, &gpio_devices);
		return 0;
	}

	prev = list_entry(gpio_devices.prev, struct gpio_device, list);
	if (prev->base + prev->ngpio <= gdev->base) {
		/* add behind last entry */
		list_add_tail(&gdev->list, &gpio_devices);
		return 0;
	}

	list_for_each_entry_safe(prev, next, &gpio_devices, list) {
		/* at the end of the list */
		if (&next->list == &gpio_devices)
			break;

		/* add between prev and next */
		if (prev->base + prev->ngpio <= gdev->base
				&& gdev->base + gdev->ngpio <= next->base) {
			list_add(&gdev->list, &prev->list);
			return 0;
		}
	}

	dev_err(&gdev->dev, "GPIO integer space overlap, cannot add chip\n");
	return -EBUSY;
}

/**
 * Convert a GPIO name to its descriptor
 */
static struct gpio_desc *gpio_name_to_desc(const char * const name)
{
	struct gpio_device *gdev;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);

	list_for_each_entry(gdev, &gpio_devices, list) {
		int i;

		for (i = 0; i != gdev->ngpio; ++i) {
			struct gpio_desc *desc = &gdev->descs[i];

			if (!desc->name || !name)
				continue;

			if (!strcmp(desc->name, name)) {
				spin_unlock_irqrestore(&gpio_lock, flags);
				return desc;
			}
		}
	}

	spin_unlock_irqrestore(&gpio_lock, flags);

	return NULL;
}

/*
 * Takes the names from gc->names and checks if they are all unique. If they
 * are, they are assigned to their gpio descriptors.
 *
 * Warning if one of the names is already used for a different GPIO.
 */
static int gpiochip_set_desc_names(struct gpio_chip *gc)
{
	struct gpio_device *gdev = gc->gpiodev;
	int i;

	if (!gc->names)
		return 0;

	/* First check all names if they are unique */
	for (i = 0; i != gc->ngpio; ++i) {
		struct gpio_desc *gpio;

		gpio = gpio_name_to_desc(gc->names[i]);
		if (gpio)
			dev_warn(&gdev->dev,
				 "Detected name collision for GPIO name '%s'\n",
				 gc->names[i]);
	}

	/* Then add all names to the GPIO descriptors */
	for (i = 0; i != gc->ngpio; ++i)
		gdev->descs[i].name = gc->names[i];

	return 0;
}

/*
 * GPIO line handle management
 */

/**
 * struct linehandle_state - contains the state of a userspace handle
 * @gdev: the GPIO device the handle pertains to
 * @label: consumer label used to tag descriptors
 * @descs: the GPIO descriptors held by this handle
 * @numdescs: the number of descriptors held in the descs array
 */
struct linehandle_state {
	struct gpio_device *gdev;
	const char *label;
	struct gpio_desc *descs[GPIOHANDLES_MAX];
	u32 numdescs;
};

#define GPIOHANDLE_REQUEST_VALID_FLAGS \
	(GPIOHANDLE_REQUEST_INPUT | \
	GPIOHANDLE_REQUEST_OUTPUT | \
	GPIOHANDLE_REQUEST_ACTIVE_LOW | \
	GPIOHANDLE_REQUEST_OPEN_DRAIN | \
	GPIOHANDLE_REQUEST_OPEN_SOURCE)

static long linehandle_ioctl(struct file *filep, unsigned int cmd,
			     unsigned long arg)
{
	struct linehandle_state *lh = filep->private_data;
	void __user *ip = (void __user *)arg;
	struct gpiohandle_data ghd;
	int i;

	if (cmd == GPIOHANDLE_GET_LINE_VALUES_IOCTL) {
		int val;

		memset(&ghd, 0, sizeof(ghd));

		/* TODO: check if descriptors are really input */
		for (i = 0; i < lh->numdescs; i++) {
			val = gpiod_get_value_cansleep(lh->descs[i]);
			if (val < 0)
				return val;
			ghd.values[i] = val;
		}

		if (copy_to_user(ip, &ghd, sizeof(ghd)))
			return -EFAULT;

		return 0;
	} else if (cmd == GPIOHANDLE_SET_LINE_VALUES_IOCTL) {
		int vals[GPIOHANDLES_MAX];

		/* TODO: check if descriptors are really output */
		if (copy_from_user(&ghd, ip, sizeof(ghd)))
			return -EFAULT;

		/* Clamp all values to [0,1] */
		for (i = 0; i < lh->numdescs; i++)
			vals[i] = !!ghd.values[i];

		/* Reuse the array setting function */
		gpiod_set_array_value_complex(false,
					      true,
					      lh->numdescs,
					      lh->descs,
					      vals);
		return 0;
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long linehandle_ioctl_compat(struct file *filep, unsigned int cmd,
			     unsigned long arg)
{
	return linehandle_ioctl(filep, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int linehandle_release(struct inode *inode, struct file *filep)
{
	struct linehandle_state *lh = filep->private_data;
	struct gpio_device *gdev = lh->gdev;
	int i;

	for (i = 0; i < lh->numdescs; i++)
		gpiod_free(lh->descs[i]);
	kfree(lh->label);
	kfree(lh);
	put_device(&gdev->dev);
	return 0;
}

static const struct file_operations linehandle_fileops = {
	.release = linehandle_release,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = linehandle_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = linehandle_ioctl_compat,
#endif
};

static int linehandle_create(struct gpio_device *gdev, void __user *ip)
{
	struct gpiohandle_request handlereq;
	struct linehandle_state *lh;
	struct file *file;
	int fd, i, ret;

	if (copy_from_user(&handlereq, ip, sizeof(handlereq)))
		return -EFAULT;
	if ((handlereq.lines == 0) || (handlereq.lines > GPIOHANDLES_MAX))
		return -EINVAL;

	lh = kzalloc(sizeof(*lh), GFP_KERNEL);
	if (!lh)
		return -ENOMEM;
	lh->gdev = gdev;
	get_device(&gdev->dev);

	/* Make sure this is terminated */
	handlereq.consumer_label[sizeof(handlereq.consumer_label)-1] = '\0';
	if (strlen(handlereq.consumer_label)) {
		lh->label = kstrdup(handlereq.consumer_label,
				    GFP_KERNEL);
		if (!lh->label) {
			ret = -ENOMEM;
			goto out_free_lh;
		}
	}

	/* Request each GPIO */
	for (i = 0; i < handlereq.lines; i++) {
		u32 offset = handlereq.lineoffsets[i];
		u32 lflags = handlereq.flags;
		struct gpio_desc *desc;

		if (offset >= gdev->ngpio) {
			ret = -EINVAL;
			goto out_free_descs;
		}

		/* Return an error if a unknown flag is set */
		if (lflags & ~GPIOHANDLE_REQUEST_VALID_FLAGS) {
			ret = -EINVAL;
			goto out_free_descs;
		}

		desc = &gdev->descs[offset];
		ret = gpiod_request(desc, lh->label);
		if (ret)
			goto out_free_descs;
		lh->descs[i] = desc;

		if (lflags & GPIOHANDLE_REQUEST_ACTIVE_LOW)
			set_bit(FLAG_ACTIVE_LOW, &desc->flags);
		if (lflags & GPIOHANDLE_REQUEST_OPEN_DRAIN)
			set_bit(FLAG_OPEN_DRAIN, &desc->flags);
		if (lflags & GPIOHANDLE_REQUEST_OPEN_SOURCE)
			set_bit(FLAG_OPEN_SOURCE, &desc->flags);

		/*
		 * Lines have to be requested explicitly for input
		 * or output, else the line will be treated "as is".
		 */
		if (lflags & GPIOHANDLE_REQUEST_OUTPUT) {
			int val = !!handlereq.default_values[i];

			ret = gpiod_direction_output(desc, val);
			if (ret)
				goto out_free_descs;
		} else if (lflags & GPIOHANDLE_REQUEST_INPUT) {
			ret = gpiod_direction_input(desc);
			if (ret)
				goto out_free_descs;
		}
		dev_dbg(&gdev->dev, "registered chardev handle for line %d\n",
			offset);
	}
	/* Let i point at the last handle */
	i--;
	lh->numdescs = handlereq.lines;

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_free_descs;
	}

	file = anon_inode_getfile("gpio-linehandle",
				  &linehandle_fileops,
				  lh,
				  O_RDONLY | O_CLOEXEC);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_put_unused_fd;
	}

	handlereq.fd = fd;
	if (copy_to_user(ip, &handlereq, sizeof(handlereq))) {
		/*
		 * fput() will trigger the release() callback, so do not go onto
		 * the regular error cleanup path here.
		 */
		fput(file);
		put_unused_fd(fd);
		return -EFAULT;
	}

	fd_install(fd, file);

	dev_dbg(&gdev->dev, "registered chardev handle for %d lines\n",
		lh->numdescs);

	return 0;

out_put_unused_fd:
	put_unused_fd(fd);
out_free_descs:
	for (; i >= 0; i--)
		gpiod_free(lh->descs[i]);
	kfree(lh->label);
out_free_lh:
	kfree(lh);
	put_device(&gdev->dev);
	return ret;
}

/*
 * GPIO line event management
 */

/**
 * struct lineevent_state - contains the state of a userspace event
 * @gdev: the GPIO device the event pertains to
 * @label: consumer label used to tag descriptors
 * @desc: the GPIO descriptor held by this event
 * @eflags: the event flags this line was requested with
 * @irq: the interrupt that trigger in response to events on this GPIO
 * @wait: wait queue that handles blocking reads of events
 * @events: KFIFO for the GPIO events
 * @read_lock: mutex lock to protect reads from colliding with adding
 * new events to the FIFO
 */
struct lineevent_state {
	struct gpio_device *gdev;
	const char *label;
	struct gpio_desc *desc;
	u32 eflags;
	int irq;
	wait_queue_head_t wait;
	DECLARE_KFIFO(events, struct gpioevent_data, 16);
	struct mutex read_lock;
};

#define GPIOEVENT_REQUEST_VALID_FLAGS \
	(GPIOEVENT_REQUEST_RISING_EDGE | \
	GPIOEVENT_REQUEST_FALLING_EDGE)

static unsigned int lineevent_poll(struct file *filep,
				   struct poll_table_struct *wait)
{
	struct lineevent_state *le = filep->private_data;
	unsigned int events = 0;

	poll_wait(filep, &le->wait, wait);

	if (!kfifo_is_empty(&le->events))
		events = POLLIN | POLLRDNORM;

	return events;
}


static ssize_t lineevent_read(struct file *filep,
			      char __user *buf,
			      size_t count,
			      loff_t *f_ps)
{
	struct lineevent_state *le = filep->private_data;
	unsigned int copied;
	int ret;

	if (count < sizeof(struct gpioevent_data))
		return -EINVAL;

	do {
		if (kfifo_is_empty(&le->events)) {
			if (filep->f_flags & O_NONBLOCK)
				return -EAGAIN;

			ret = wait_event_interruptible(le->wait,
					!kfifo_is_empty(&le->events));
			if (ret)
				return ret;
		}

		if (mutex_lock_interruptible(&le->read_lock))
			return -ERESTARTSYS;
		ret = kfifo_to_user(&le->events, buf, count, &copied);
		mutex_unlock(&le->read_lock);

		if (ret)
			return ret;

		/*
		 * If we couldn't read anything from the fifo (a different
		 * thread might have been faster) we either return -EAGAIN if
		 * the file descriptor is non-blocking, otherwise we go back to
		 * sleep and wait for more data to arrive.
		 */
		if (copied == 0 && (filep->f_flags & O_NONBLOCK))
			return -EAGAIN;

	} while (copied == 0);

	return copied;
}

static int lineevent_release(struct inode *inode, struct file *filep)
{
	struct lineevent_state *le = filep->private_data;
	struct gpio_device *gdev = le->gdev;

	free_irq(le->irq, le);
	gpiod_free(le->desc);
	kfree(le->label);
	kfree(le);
	put_device(&gdev->dev);
	return 0;
}

static long lineevent_ioctl(struct file *filep, unsigned int cmd,
			    unsigned long arg)
{
	struct lineevent_state *le = filep->private_data;
	void __user *ip = (void __user *)arg;
	struct gpiohandle_data ghd;

	/*
	 * We can get the value for an event line but not set it,
	 * because it is input by definition.
	 */
	if (cmd == GPIOHANDLE_GET_LINE_VALUES_IOCTL) {
		int val;

		memset(&ghd, 0, sizeof(ghd));

		val = gpiod_get_value_cansleep(le->desc);
		if (val < 0)
			return val;
		ghd.values[0] = val;

		if (copy_to_user(ip, &ghd, sizeof(ghd)))
			return -EFAULT;

		return 0;
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long lineevent_ioctl_compat(struct file *filep, unsigned int cmd,
				   unsigned long arg)
{
	return lineevent_ioctl(filep, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations lineevent_fileops = {
	.release = lineevent_release,
	.read = lineevent_read,
	.poll = lineevent_poll,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = lineevent_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lineevent_ioctl_compat,
#endif
};

static irqreturn_t lineevent_irq_thread(int irq, void *p)
{
	struct lineevent_state *le = p;
	struct gpioevent_data ge;
	int ret;

	ge.timestamp = ktime_get_real_ns();

	if (le->eflags & GPIOEVENT_REQUEST_BOTH_EDGES) {
		int level = gpiod_get_value_cansleep(le->desc);

		if (level)
			/* Emit low-to-high event */
			ge.id = GPIOEVENT_EVENT_RISING_EDGE;
		else
			/* Emit high-to-low event */
			ge.id = GPIOEVENT_EVENT_FALLING_EDGE;
	} else if (le->eflags & GPIOEVENT_REQUEST_RISING_EDGE) {
		/* Emit low-to-high event */
		ge.id = GPIOEVENT_EVENT_RISING_EDGE;
	} else if (le->eflags & GPIOEVENT_REQUEST_FALLING_EDGE) {
		/* Emit high-to-low event */
		ge.id = GPIOEVENT_EVENT_FALLING_EDGE;
	} else {
		return IRQ_NONE;
	}

	ret = kfifo_put(&le->events, ge);
	if (ret != 0)
		wake_up_poll(&le->wait, POLLIN);

	return IRQ_HANDLED;
}

static int lineevent_create(struct gpio_device *gdev, void __user *ip)
{
	struct gpioevent_request eventreq;
	struct lineevent_state *le;
	struct gpio_desc *desc;
	struct file *file;
	u32 offset;
	u32 lflags;
	u32 eflags;
	int fd;
	int ret;
	int irqflags = 0;

	if (copy_from_user(&eventreq, ip, sizeof(eventreq)))
		return -EFAULT;

	le = kzalloc(sizeof(*le), GFP_KERNEL);
	if (!le)
		return -ENOMEM;
	le->gdev = gdev;
	get_device(&gdev->dev);

	/* Make sure this is terminated */
	eventreq.consumer_label[sizeof(eventreq.consumer_label)-1] = '\0';
	if (strlen(eventreq.consumer_label)) {
		le->label = kstrdup(eventreq.consumer_label,
				    GFP_KERNEL);
		if (!le->label) {
			ret = -ENOMEM;
			goto out_free_le;
		}
	}

	offset = eventreq.lineoffset;
	lflags = eventreq.handleflags;
	eflags = eventreq.eventflags;

	if (offset >= gdev->ngpio) {
		ret = -EINVAL;
		goto out_free_label;
	}

	/* Return an error if a unknown flag is set */
	if ((lflags & ~GPIOHANDLE_REQUEST_VALID_FLAGS) ||
	    (eflags & ~GPIOEVENT_REQUEST_VALID_FLAGS)) {
		ret = -EINVAL;
		goto out_free_label;
	}

	/* This is just wrong: we don't look for events on output lines */
	if (lflags & GPIOHANDLE_REQUEST_OUTPUT) {
		ret = -EINVAL;
		goto out_free_label;
	}

	desc = &gdev->descs[offset];
	ret = gpiod_request(desc, le->label);
	if (ret)
		goto out_free_desc;
	le->desc = desc;
	le->eflags = eflags;

	if (lflags & GPIOHANDLE_REQUEST_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);
	if (lflags & GPIOHANDLE_REQUEST_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);
	if (lflags & GPIOHANDLE_REQUEST_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	ret = gpiod_direction_input(desc);
	if (ret)
		goto out_free_desc;

	le->irq = gpiod_to_irq(desc);
	if (le->irq <= 0) {
		ret = -ENODEV;
		goto out_free_desc;
	}

	if (eflags & GPIOEVENT_REQUEST_RISING_EDGE)
		irqflags |= IRQF_TRIGGER_RISING;
	if (eflags & GPIOEVENT_REQUEST_FALLING_EDGE)
		irqflags |= IRQF_TRIGGER_FALLING;
	irqflags |= IRQF_ONESHOT;
	irqflags |= IRQF_SHARED;

	INIT_KFIFO(le->events);
	init_waitqueue_head(&le->wait);
	mutex_init(&le->read_lock);

	/* Request a thread to read the events */
	ret = request_threaded_irq(le->irq,
			NULL,
			lineevent_irq_thread,
			irqflags,
			le->label,
			le);
	if (ret)
		goto out_free_desc;

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto out_free_irq;
	}

	file = anon_inode_getfile("gpio-event",
				  &lineevent_fileops,
				  le,
				  O_RDONLY | O_CLOEXEC);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto out_put_unused_fd;
	}

	eventreq.fd = fd;
	if (copy_to_user(ip, &eventreq, sizeof(eventreq))) {
		/*
		 * fput() will trigger the release() callback, so do not go onto
		 * the regular error cleanup path here.
		 */
		fput(file);
		put_unused_fd(fd);
		return -EFAULT;
	}

	fd_install(fd, file);

	return 0;

out_put_unused_fd:
	put_unused_fd(fd);
out_free_irq:
	free_irq(le->irq, le);
out_free_desc:
	gpiod_free(le->desc);
out_free_label:
	kfree(le->label);
out_free_le:
	kfree(le);
	put_device(&gdev->dev);
	return ret;
}

/**
 * gpio_ioctl() - ioctl handler for the GPIO chardev
 */
static long gpio_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct gpio_device *gdev = filp->private_data;
	struct gpio_chip *chip = gdev->chip;
	void __user *ip = (void __user *)arg;

	/* We fail any subsequent ioctl():s when the chip is gone */
	if (!chip)
		return -ENODEV;

	/* Fill in the struct and pass to userspace */
	if (cmd == GPIO_GET_CHIPINFO_IOCTL) {
		struct gpiochip_info chipinfo;

		memset(&chipinfo, 0, sizeof(chipinfo));

		strncpy(chipinfo.name, dev_name(&gdev->dev),
			sizeof(chipinfo.name));
		chipinfo.name[sizeof(chipinfo.name)-1] = '\0';
		strncpy(chipinfo.label, gdev->label,
			sizeof(chipinfo.label));
		chipinfo.label[sizeof(chipinfo.label)-1] = '\0';
		chipinfo.lines = gdev->ngpio;
		if (copy_to_user(ip, &chipinfo, sizeof(chipinfo)))
			return -EFAULT;
		return 0;
	} else if (cmd == GPIO_GET_LINEINFO_IOCTL) {
		struct gpioline_info lineinfo;
		struct gpio_desc *desc;

		if (copy_from_user(&lineinfo, ip, sizeof(lineinfo)))
			return -EFAULT;
		if (lineinfo.line_offset >= gdev->ngpio)
			return -EINVAL;

		desc = &gdev->descs[lineinfo.line_offset];
		if (desc->name) {
			strncpy(lineinfo.name, desc->name,
				sizeof(lineinfo.name));
			lineinfo.name[sizeof(lineinfo.name)-1] = '\0';
		} else {
			lineinfo.name[0] = '\0';
		}
		if (desc->label) {
			strncpy(lineinfo.consumer, desc->label,
				sizeof(lineinfo.consumer));
			lineinfo.consumer[sizeof(lineinfo.consumer)-1] = '\0';
		} else {
			lineinfo.consumer[0] = '\0';
		}

		/*
		 * Userspace only need to know that the kernel is using
		 * this GPIO so it can't use it.
		 */
		lineinfo.flags = 0;
		if (test_bit(FLAG_REQUESTED, &desc->flags) ||
		    test_bit(FLAG_IS_HOGGED, &desc->flags) ||
		    test_bit(FLAG_USED_AS_IRQ, &desc->flags) ||
		    test_bit(FLAG_EXPORT, &desc->flags) ||
		    test_bit(FLAG_SYSFS, &desc->flags))
			lineinfo.flags |= GPIOLINE_FLAG_KERNEL;
		if (test_bit(FLAG_IS_OUT, &desc->flags))
			lineinfo.flags |= GPIOLINE_FLAG_IS_OUT;
		if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
			lineinfo.flags |= GPIOLINE_FLAG_ACTIVE_LOW;
		if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
			lineinfo.flags |= GPIOLINE_FLAG_OPEN_DRAIN;
		if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
			lineinfo.flags |= GPIOLINE_FLAG_OPEN_SOURCE;

		if (copy_to_user(ip, &lineinfo, sizeof(lineinfo)))
			return -EFAULT;
		return 0;
	} else if (cmd == GPIO_GET_LINEHANDLE_IOCTL) {
		return linehandle_create(gdev, ip);
	} else if (cmd == GPIO_GET_LINEEVENT_IOCTL) {
		return lineevent_create(gdev, ip);
	}
	return -EINVAL;
}

#ifdef CONFIG_COMPAT
static long gpio_ioctl_compat(struct file *filp, unsigned int cmd,
			      unsigned long arg)
{
	return gpio_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}
#endif

/**
 * gpio_chrdev_open() - open the chardev for ioctl operations
 * @inode: inode for this chardev
 * @filp: file struct for storing private data
 * Returns 0 on success
 */
static int gpio_chrdev_open(struct inode *inode, struct file *filp)
{
	struct gpio_device *gdev = container_of(inode->i_cdev,
					      struct gpio_device, chrdev);

	/* Fail on open if the backing gpiochip is gone */
	if (!gdev || !gdev->chip)
		return -ENODEV;
	get_device(&gdev->dev);
	filp->private_data = gdev;
	return 0;
}

/**
 * gpio_chrdev_release() - close chardev after ioctl operations
 * @inode: inode for this chardev
 * @filp: file struct for storing private data
 * Returns 0 on success
 */
static int gpio_chrdev_release(struct inode *inode, struct file *filp)
{
	struct gpio_device *gdev = container_of(inode->i_cdev,
					      struct gpio_device, chrdev);

	if (!gdev)
		return -ENODEV;
	put_device(&gdev->dev);
	return 0;
}


static const struct file_operations gpio_fileops = {
	.release = gpio_chrdev_release,
	.open = gpio_chrdev_open,
	.owner = THIS_MODULE,
	.llseek = noop_llseek,
	.unlocked_ioctl = gpio_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = gpio_ioctl_compat,
#endif
};

static void gpiodevice_release(struct device *dev)
{
	struct gpio_device *gdev = dev_get_drvdata(dev);

	list_del(&gdev->list);
	ida_simple_remove(&gpio_ida, gdev->id);
	kfree(gdev->label);
	kfree(gdev->descs);
	kfree(gdev);
}

static int gpiochip_setup_dev(struct gpio_device *gdev)
{
	int status;

	cdev_init(&gdev->chrdev, &gpio_fileops);
	gdev->chrdev.owner = THIS_MODULE;
	gdev->chrdev.kobj.parent = &gdev->dev.kobj;
	gdev->dev.devt = MKDEV(MAJOR(gpio_devt), gdev->id);
	status = cdev_add(&gdev->chrdev, gdev->dev.devt, 1);
	if (status < 0)
		chip_warn(gdev->chip, "failed to add char device %d:%d\n",
			  MAJOR(gpio_devt), gdev->id);
	else
		chip_dbg(gdev->chip, "added GPIO chardev (%d:%d)\n",
			 MAJOR(gpio_devt), gdev->id);
	status = device_add(&gdev->dev);
	if (status)
		goto err_remove_chardev;

	status = gpiochip_sysfs_register(gdev);
	if (status)
		goto err_remove_device;

	/* From this point, the .release() function cleans up gpio_device */
	gdev->dev.release = gpiodevice_release;
	pr_debug("%s: registered GPIOs %d to %d on device: %s (%s)\n",
		 __func__, gdev->base, gdev->base + gdev->ngpio - 1,
		 dev_name(&gdev->dev), gdev->chip->label ? : "generic");

	return 0;

err_remove_device:
	device_del(&gdev->dev);
err_remove_chardev:
	cdev_del(&gdev->chrdev);
	return status;
}

static void gpiochip_setup_devs(void)
{
	struct gpio_device *gdev;
	int err;

	list_for_each_entry(gdev, &gpio_devices, list) {
		err = gpiochip_setup_dev(gdev);
		if (err)
			pr_err("%s: Failed to initialize gpio device (%d)\n",
			       dev_name(&gdev->dev), err);
	}
}

/**
 * gpiochip_add_data() - register a gpio_chip
 * @chip: the chip to register, with chip->base initialized
 * Context: potentially before irqs will work
 *
 * Returns a negative errno if the chip can't be registered, such as
 * because the chip->base is invalid or already associated with a
 * different chip.  Otherwise it returns zero as a success code.
 *
 * When gpiochip_add_data() is called very early during boot, so that GPIOs
 * can be freely used, the chip->parent device must be registered before
 * the gpio framework's arch_initcall().  Otherwise sysfs initialization
 * for GPIOs will fail rudely.
 *
 * gpiochip_add_data() must only be called after gpiolib initialization,
 * ie after core_initcall().
 *
 * If chip->base is negative, this requests dynamic assignment of
 * a range of valid GPIOs.
 */
int gpiochip_add_data(struct gpio_chip *chip, void *data)
{
	unsigned long	flags;
	int		status = 0;
	unsigned	i;
	int		base = chip->base;
	struct gpio_device *gdev;

	/*
	 * First: allocate and populate the internal stat container, and
	 * set up the struct device.
	 */
	gdev = kzalloc(sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;
	gdev->dev.bus = &gpio_bus_type;
	gdev->chip = chip;
	chip->gpiodev = gdev;
	if (chip->parent) {
		gdev->dev.parent = chip->parent;
		gdev->dev.of_node = chip->parent->of_node;
	}

#ifdef CONFIG_OF_GPIO
	/* If the gpiochip has an assigned OF node this takes precedence */
	if (chip->of_node)
		gdev->dev.of_node = chip->of_node;
#endif

	gdev->id = ida_simple_get(&gpio_ida, 0, 0, GFP_KERNEL);
	if (gdev->id < 0) {
		status = gdev->id;
		goto err_free_gdev;
	}
	dev_set_name(&gdev->dev, "gpiochip%d", gdev->id);
	device_initialize(&gdev->dev);
	dev_set_drvdata(&gdev->dev, gdev);
	if (chip->parent && chip->parent->driver)
		gdev->owner = chip->parent->driver->owner;
	else if (chip->owner)
		/* TODO: remove chip->owner */
		gdev->owner = chip->owner;
	else
		gdev->owner = THIS_MODULE;

	gdev->descs = kcalloc(chip->ngpio, sizeof(gdev->descs[0]), GFP_KERNEL);
	if (!gdev->descs) {
		status = -ENOMEM;
		goto err_free_gdev;
	}

	if (chip->ngpio == 0) {
		chip_err(chip, "tried to insert a GPIO chip with zero lines\n");
		status = -EINVAL;
		goto err_free_descs;
	}

	if (chip->label)
		gdev->label = kstrdup(chip->label, GFP_KERNEL);
	else
		gdev->label = kstrdup("unknown", GFP_KERNEL);
	if (!gdev->label) {
		status = -ENOMEM;
		goto err_free_descs;
	}

	gdev->ngpio = chip->ngpio;
	gdev->data = data;

	spin_lock_irqsave(&gpio_lock, flags);

	/*
	 * TODO: this allocates a Linux GPIO number base in the global
	 * GPIO numberspace for this chip. In the long run we want to
	 * get *rid* of this numberspace and use only descriptors, but
	 * it may be a pipe dream. It will not happen before we get rid
	 * of the sysfs interface anyways.
	 */
	if (base < 0) {
		base = gpiochip_find_base(chip->ngpio);
		if (base < 0) {
			status = base;
			spin_unlock_irqrestore(&gpio_lock, flags);
			goto err_free_label;
		}
		/*
		 * TODO: it should not be necessary to reflect the assigned
		 * base outside of the GPIO subsystem. Go over drivers and
		 * see if anyone makes use of this, else drop this and assign
		 * a poison instead.
		 */
		chip->base = base;
	}
	gdev->base = base;

	status = gpiodev_add_to_list(gdev);
	if (status) {
		spin_unlock_irqrestore(&gpio_lock, flags);
		goto err_free_label;
	}

	spin_unlock_irqrestore(&gpio_lock, flags);

	for (i = 0; i < chip->ngpio; i++) {
		struct gpio_desc *desc = &gdev->descs[i];

		desc->gdev = gdev;
		/*
		 * REVISIT: most hardware initializes GPIOs as inputs
		 * (often with pullups enabled) so power usage is
		 * minimized. Linux code should set the gpio direction
		 * first thing; but until it does, and in case
		 * chip->get_direction is not set, we may expose the
		 * wrong direction in sysfs.
		 */

		if (chip->get_direction) {
			/*
			 * If we have .get_direction, set up the initial
			 * direction flag from the hardware.
			 */
			int dir = chip->get_direction(chip, i);

			if (!dir)
				set_bit(FLAG_IS_OUT, &desc->flags);
		} else if (!chip->direction_input) {
			/*
			 * If the chip lacks the .direction_input callback
			 * we logically assume all lines are outputs.
			 */
			set_bit(FLAG_IS_OUT, &desc->flags);
		}
	}

#ifdef CONFIG_PINCTRL
	INIT_LIST_HEAD(&gdev->pin_ranges);
#endif

	status = gpiochip_set_desc_names(chip);
	if (status)
		goto err_remove_from_list;

	status = gpiochip_irqchip_init_valid_mask(chip);
	if (status)
		goto err_remove_from_list;

	status = of_gpiochip_add(chip);
	if (status)
		goto err_remove_chip;

	acpi_gpiochip_add(chip);

	/*
	 * By first adding the chardev, and then adding the device,
	 * we get a device node entry in sysfs under
	 * /sys/bus/gpio/devices/gpiochipN/dev that can be used for
	 * coldplug of device nodes and other udev business.
	 * We can do this only if gpiolib has been initialized.
	 * Otherwise, defer until later.
	 */
	if (gpiolib_initialized) {
		status = gpiochip_setup_dev(gdev);
		if (status)
			goto err_remove_chip;
	}
	return 0;

err_remove_chip:
	acpi_gpiochip_remove(chip);
	gpiochip_free_hogs(chip);
	of_gpiochip_remove(chip);
	gpiochip_irqchip_free_valid_mask(chip);
err_remove_from_list:
	spin_lock_irqsave(&gpio_lock, flags);
	list_del(&gdev->list);
	spin_unlock_irqrestore(&gpio_lock, flags);
err_free_label:
	kfree(gdev->label);
err_free_descs:
	kfree(gdev->descs);
err_free_gdev:
	ida_simple_remove(&gpio_ida, gdev->id);
	/* failures here can mean systems won't boot... */
	pr_err("%s: GPIOs %d..%d (%s) failed to register\n", __func__,
	       gdev->base, gdev->base + gdev->ngpio - 1,
	       chip->label ? : "generic");
	kfree(gdev);
	return status;
}
EXPORT_SYMBOL_GPL(gpiochip_add_data);

/**
 * gpiochip_get_data() - get per-subdriver data for the chip
 */
void *gpiochip_get_data(struct gpio_chip *chip)
{
	return chip->gpiodev->data;
}
EXPORT_SYMBOL_GPL(gpiochip_get_data);

/**
 * gpiochip_remove() - unregister a gpio_chip
 * @chip: the chip to unregister
 *
 * A gpio_chip with any GPIOs still requested may not be removed.
 */
void gpiochip_remove(struct gpio_chip *chip)
{
	struct gpio_device *gdev = chip->gpiodev;
	struct gpio_desc *desc;
	unsigned long	flags;
	unsigned	i;
	bool		requested = false;

	/* FIXME: should the legacy sysfs handling be moved to gpio_device? */
	gpiochip_sysfs_unregister(gdev);
	/* Numb the device, cancelling all outstanding operations */
	gdev->chip = NULL;
	gpiochip_irqchip_remove(chip);
	acpi_gpiochip_remove(chip);
	gpiochip_remove_pin_ranges(chip);
	gpiochip_free_hogs(chip);
	of_gpiochip_remove(chip);
	/*
	 * We accept no more calls into the driver from this point, so
	 * NULL the driver data pointer
	 */
	gdev->data = NULL;

	spin_lock_irqsave(&gpio_lock, flags);
	for (i = 0; i < gdev->ngpio; i++) {
		desc = &gdev->descs[i];
		if (test_bit(FLAG_REQUESTED, &desc->flags))
			requested = true;
	}
	spin_unlock_irqrestore(&gpio_lock, flags);

	if (requested)
		dev_crit(&gdev->dev,
			 "REMOVING GPIOCHIP WITH GPIOS STILL REQUESTED\n");

	/*
	 * The gpiochip side puts its use of the device to rest here:
	 * if there are no userspace clients, the chardev and device will
	 * be removed, else it will be dangling until the last user is
	 * gone.
	 */
	cdev_del(&gdev->chrdev);
	device_del(&gdev->dev);
	put_device(&gdev->dev);
}
EXPORT_SYMBOL_GPL(gpiochip_remove);

static void devm_gpio_chip_release(struct device *dev, void *res)
{
	struct gpio_chip *chip = *(struct gpio_chip **)res;

	gpiochip_remove(chip);
}

static int devm_gpio_chip_match(struct device *dev, void *res, void *data)

{
	struct gpio_chip **r = res;

	if (!r || !*r) {
		WARN_ON(!r || !*r);
		return 0;
	}

	return *r == data;
}

/**
 * devm_gpiochip_add_data() - Resource manager piochip_add_data()
 * @dev: the device pointer on which irq_chip belongs to.
 * @chip: the chip to register, with chip->base initialized
 * Context: potentially before irqs will work
 *
 * Returns a negative errno if the chip can't be registered, such as
 * because the chip->base is invalid or already associated with a
 * different chip.  Otherwise it returns zero as a success code.
 *
 * The gpio chip automatically be released when the device is unbound.
 */
int devm_gpiochip_add_data(struct device *dev, struct gpio_chip *chip,
			   void *data)
{
	struct gpio_chip **ptr;
	int ret;

	ptr = devres_alloc(devm_gpio_chip_release, sizeof(*ptr),
			     GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = gpiochip_add_data(chip, data);
	if (ret < 0) {
		devres_free(ptr);
		return ret;
	}

	*ptr = chip;
	devres_add(dev, ptr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_gpiochip_add_data);

/**
 * devm_gpiochip_remove() - Resource manager of gpiochip_remove()
 * @dev: device for which which resource was allocated
 * @chip: the chip to remove
 *
 * A gpio_chip with any GPIOs still requested may not be removed.
 */
void devm_gpiochip_remove(struct device *dev, struct gpio_chip *chip)
{
	int ret;

	ret = devres_release(dev, devm_gpio_chip_release,
			     devm_gpio_chip_match, chip);
	if (!ret)
		WARN_ON(ret);
}
EXPORT_SYMBOL_GPL(devm_gpiochip_remove);

/**
 * gpiochip_find() - iterator for locating a specific gpio_chip
 * @data: data to pass to match function
 * @callback: Callback function to check gpio_chip
 *
 * Similar to bus_find_device.  It returns a reference to a gpio_chip as
 * determined by a user supplied @match callback.  The callback should return
 * 0 if the device doesn't match and non-zero if it does.  If the callback is
 * non-zero, this function will return to the caller and not iterate over any
 * more gpio_chips.
 */
struct gpio_chip *gpiochip_find(void *data,
				int (*match)(struct gpio_chip *chip,
					     void *data))
{
	struct gpio_device *gdev;
	struct gpio_chip *chip = NULL;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	list_for_each_entry(gdev, &gpio_devices, list)
		if (gdev->chip && match(gdev->chip, data)) {
			chip = gdev->chip;
			break;
		}

	spin_unlock_irqrestore(&gpio_lock, flags);

	return chip;
}
EXPORT_SYMBOL_GPL(gpiochip_find);

static int gpiochip_match_name(struct gpio_chip *chip, void *data)
{
	const char *name = data;

	return !strcmp(chip->label, name);
}

static struct gpio_chip *find_chip_by_name(const char *name)
{
	return gpiochip_find((void *)name, gpiochip_match_name);
}

#ifdef CONFIG_GPIOLIB_IRQCHIP

/*
 * The following is irqchip helper code for gpiochips.
 */

static int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gpiochip)
{
	int i;

	if (!gpiochip->irq_need_valid_mask)
		return 0;

	gpiochip->irq_valid_mask = kcalloc(BITS_TO_LONGS(gpiochip->ngpio),
					   sizeof(long), GFP_KERNEL);
	if (!gpiochip->irq_valid_mask)
		return -ENOMEM;

	/* Assume by default all GPIOs are valid */
	for (i = 0; i < gpiochip->ngpio; i++)
		set_bit(i, gpiochip->irq_valid_mask);

	return 0;
}

static void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gpiochip)
{
	kfree(gpiochip->irq_valid_mask);
	gpiochip->irq_valid_mask = NULL;
}

static bool gpiochip_irqchip_irq_valid(const struct gpio_chip *gpiochip,
				       unsigned int offset)
{
	/* No mask means all valid */
	if (likely(!gpiochip->irq_valid_mask))
		return true;
	return test_bit(offset, gpiochip->irq_valid_mask);
}

/**
 * gpiochip_set_chained_irqchip() - sets a chained irqchip to a gpiochip
 * @gpiochip: the gpiochip to set the irqchip chain to
 * @irqchip: the irqchip to chain to the gpiochip
 * @parent_irq: the irq number corresponding to the parent IRQ for this
 * chained irqchip
 * @parent_handler: the parent interrupt handler for the accumulated IRQ
 * coming out of the gpiochip. If the interrupt is nested rather than
 * cascaded, pass NULL in this handler argument
 */
void gpiochip_set_chained_irqchip(struct gpio_chip *gpiochip,
				  struct irq_chip *irqchip,
				  int parent_irq,
				  irq_flow_handler_t parent_handler)
{
	unsigned int offset;

	if (!gpiochip->irqdomain) {
		chip_err(gpiochip, "called %s before setting up irqchip\n",
			 __func__);
		return;
	}

	if (parent_handler) {
		if (gpiochip->can_sleep) {
			chip_err(gpiochip,
				 "you cannot have chained interrupts on a "
				 "chip that may sleep\n");
			return;
		}
		/*
		 * The parent irqchip is already using the chip_data for this
		 * irqchip, so our callbacks simply use the handler_data.
		 */
		irq_set_chained_handler_and_data(parent_irq, parent_handler,
						 gpiochip);

		gpiochip->irq_parent = parent_irq;
	}

	/* Set the parent IRQ for all affected IRQs */
	for (offset = 0; offset < gpiochip->ngpio; offset++) {
		if (!gpiochip_irqchip_irq_valid(gpiochip, offset))
			continue;
		irq_set_parent(irq_find_mapping(gpiochip->irqdomain, offset),
			       parent_irq);
	}
}
EXPORT_SYMBOL_GPL(gpiochip_set_chained_irqchip);

/**
 * gpiochip_irq_map() - maps an IRQ into a GPIO irqchip
 * @d: the irqdomain used by this irqchip
 * @irq: the global irq number used by this GPIO irqchip irq
 * @hwirq: the local IRQ/GPIO line offset on this gpiochip
 *
 * This function will set up the mapping for a certain IRQ line on a
 * gpiochip by assigning the gpiochip as chip data, and using the irqchip
 * stored inside the gpiochip.
 */
static int gpiochip_irq_map(struct irq_domain *d, unsigned int irq,
			    irq_hw_number_t hwirq)
{
	struct gpio_chip *chip = d->host_data;

	irq_set_chip_data(irq, chip);
	/*
	 * This lock class tells lockdep that GPIO irqs are in a different
	 * category than their parents, so it won't report false recursion.
	 */
	irq_set_lockdep_class(irq, chip->lock_key);
	irq_set_chip_and_handler(irq, chip->irqchip, chip->irq_handler);
	/* Chips that can sleep need nested thread handlers */
	if (chip->can_sleep && !chip->irq_not_threaded)
		irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);

	/*
	 * No set-up of the hardware will happen if IRQ_TYPE_NONE
	 * is passed as default type.
	 */
	if (chip->irq_default_type != IRQ_TYPE_NONE)
		irq_set_irq_type(irq, chip->irq_default_type);

	return 0;
}

static void gpiochip_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	struct gpio_chip *chip = d->host_data;

	if (chip->can_sleep)
		irq_set_nested_thread(irq, 0);
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops gpiochip_domain_ops = {
	.map	= gpiochip_irq_map,
	.unmap	= gpiochip_irq_unmap,
	/* Virtually all GPIO irqchips are twocell:ed */
	.xlate	= irq_domain_xlate_twocell,
};

static int gpiochip_irq_reqres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	if (!try_module_get(chip->gpiodev->owner))
		return -ENODEV;

	if (gpiochip_lock_as_irq(chip, d->hwirq)) {
		chip_err(chip,
			"unable to lock HW IRQ %lu for IRQ\n",
			d->hwirq);
		module_put(chip->gpiodev->owner);
		return -EINVAL;
	}
	return 0;
}

static void gpiochip_irq_relres(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(d);

	gpiochip_unlock_as_irq(chip, d->hwirq);
	module_put(chip->gpiodev->owner);
}

static int gpiochip_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_find_mapping(chip->irqdomain, offset);
}

/**
 * gpiochip_irqchip_remove() - removes an irqchip added to a gpiochip
 * @gpiochip: the gpiochip to remove the irqchip from
 *
 * This is called only from gpiochip_remove()
 */
static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip)
{
	unsigned int offset;

	acpi_gpiochip_free_interrupts(gpiochip);

	if (gpiochip->irq_parent) {
		irq_set_chained_handler(gpiochip->irq_parent, NULL);
		irq_set_handler_data(gpiochip->irq_parent, NULL);
	}

	/* Remove all IRQ mappings and delete the domain */
	if (gpiochip->irqdomain) {
		for (offset = 0; offset < gpiochip->ngpio; offset++) {
			if (!gpiochip_irqchip_irq_valid(gpiochip, offset))
				continue;
			irq_dispose_mapping(
				irq_find_mapping(gpiochip->irqdomain, offset));
		}
		irq_domain_remove(gpiochip->irqdomain);
	}

	if (gpiochip->irqchip) {
		gpiochip->irqchip->irq_request_resources = NULL;
		gpiochip->irqchip->irq_release_resources = NULL;
		gpiochip->irqchip = NULL;
	}

	gpiochip_irqchip_free_valid_mask(gpiochip);
}

/**
 * gpiochip_irqchip_add() - adds an irqchip to a gpiochip
 * @gpiochip: the gpiochip to add the irqchip to
 * @irqchip: the irqchip to add to the gpiochip
 * @first_irq: if not dynamically assigned, the base (first) IRQ to
 * allocate gpiochip irqs from
 * @handler: the irq handler to use (often a predefined irq core function)
 * @type: the default type for IRQs on this irqchip, pass IRQ_TYPE_NONE
 * to have the core avoid setting up any default type in the hardware.
 * @lock_key: lockdep class
 *
 * This function closely associates a certain irqchip with a certain
 * gpiochip, providing an irq domain to translate the local IRQs to
 * global irqs in the gpiolib core, and making sure that the gpiochip
 * is passed as chip data to all related functions. Driver callbacks
 * need to use gpiochip_get_data() to get their local state containers back
 * from the gpiochip passed as chip data. An irqdomain will be stored
 * in the gpiochip that shall be used by the driver to handle IRQ number
 * translation. The gpiochip will need to be initialized and registered
 * before calling this function.
 *
 * This function will handle two cell:ed simple IRQs and assumes all
 * the pins on the gpiochip can generate a unique IRQ. Everything else
 * need to be open coded.
 */
int _gpiochip_irqchip_add(struct gpio_chip *gpiochip,
			  struct irq_chip *irqchip,
			  unsigned int first_irq,
			  irq_flow_handler_t handler,
			  unsigned int type,
			  struct lock_class_key *lock_key)
{
	struct device_node *of_node;
	bool irq_base_set = false;
	unsigned int offset;
	unsigned irq_base = 0;

	if (!gpiochip || !irqchip)
		return -EINVAL;

	if (!gpiochip->parent) {
		pr_err("missing gpiochip .dev parent pointer\n");
		return -EINVAL;
	}
	of_node = gpiochip->parent->of_node;
#ifdef CONFIG_OF_GPIO
	/*
	 * If the gpiochip has an assigned OF node this takes precedence
	 * FIXME: get rid of this and use gpiochip->parent->of_node
	 * everywhere
	 */
	if (gpiochip->of_node)
		of_node = gpiochip->of_node;
#endif
	/*
	 * Specifying a default trigger is a terrible idea if DT or ACPI is
	 * used to configure the interrupts, as you may end-up with
	 * conflicting triggers. Tell the user, and reset to NONE.
	 */
	if (WARN(of_node && type != IRQ_TYPE_NONE,
		 "%s: Ignoring %d default trigger\n", of_node->full_name, type))
		type = IRQ_TYPE_NONE;
	if (has_acpi_companion(gpiochip->parent) && type != IRQ_TYPE_NONE) {
		acpi_handle_warn(ACPI_HANDLE(gpiochip->parent),
				 "Ignoring %d default trigger\n", type);
		type = IRQ_TYPE_NONE;
	}

	gpiochip->irqchip = irqchip;
	gpiochip->irq_handler = handler;
	gpiochip->irq_default_type = type;
	gpiochip->to_irq = gpiochip_to_irq;
	gpiochip->lock_key = lock_key;
	gpiochip->irqdomain = irq_domain_add_simple(of_node,
					gpiochip->ngpio, first_irq,
					&gpiochip_domain_ops, gpiochip);
	if (!gpiochip->irqdomain) {
		gpiochip->irqchip = NULL;
		return -EINVAL;
	}

	/*
	 * It is possible for a driver to override this, but only if the
	 * alternative functions are both implemented.
	 */
	if (!irqchip->irq_request_resources &&
	    !irqchip->irq_release_resources) {
		irqchip->irq_request_resources = gpiochip_irq_reqres;
		irqchip->irq_release_resources = gpiochip_irq_relres;
	}

	/*
	 * Prepare the mapping since the irqchip shall be orthogonal to
	 * any gpiochip calls. If the first_irq was zero, this is
	 * necessary to allocate descriptors for all IRQs.
	 */
	for (offset = 0; offset < gpiochip->ngpio; offset++) {
		if (!gpiochip_irqchip_irq_valid(gpiochip, offset))
			continue;
		irq_base = irq_create_mapping(gpiochip->irqdomain, offset);
		if (!irq_base_set) {
			/*
			 * Store the base into the gpiochip to be used when
			 * unmapping the irqs.
			 */
			gpiochip->irq_base = irq_base;
			irq_base_set = true;
		}
	}

	acpi_gpiochip_request_interrupts(gpiochip);

	return 0;
}
EXPORT_SYMBOL_GPL(_gpiochip_irqchip_add);

#else /* CONFIG_GPIOLIB_IRQCHIP */

static void gpiochip_irqchip_remove(struct gpio_chip *gpiochip) {}
static inline int gpiochip_irqchip_init_valid_mask(struct gpio_chip *gpiochip)
{
	return 0;
}
static inline void gpiochip_irqchip_free_valid_mask(struct gpio_chip *gpiochip)
{ }

#endif /* CONFIG_GPIOLIB_IRQCHIP */

/**
 * gpiochip_generic_request() - request the gpio function for a pin
 * @chip: the gpiochip owning the GPIO
 * @offset: the offset of the GPIO to request for GPIO function
 */
int gpiochip_generic_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->gpiodev->base + offset);
}
EXPORT_SYMBOL_GPL(gpiochip_generic_request);

/**
 * gpiochip_generic_free() - free the gpio function from a pin
 * @chip: the gpiochip to request the gpio function for
 * @offset: the offset of the GPIO to free from GPIO function
 */
void gpiochip_generic_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->gpiodev->base + offset);
}
EXPORT_SYMBOL_GPL(gpiochip_generic_free);

#ifdef CONFIG_PINCTRL

/**
 * gpiochip_add_pingroup_range() - add a range for GPIO <-> pin mapping
 * @chip: the gpiochip to add the range for
 * @pctldev: the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_group: name of the pin group inside the pin controller
 */
int gpiochip_add_pingroup_range(struct gpio_chip *chip,
			struct pinctrl_dev *pctldev,
			unsigned int gpio_offset, const char *pin_group)
{
	struct gpio_pin_range *pin_range;
	struct gpio_device *gdev = chip->gpiodev;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(chip, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = chip;
	pin_range->range.name = chip->label;
	pin_range->range.base = gdev->base + gpio_offset;
	pin_range->pctldev = pctldev;

	ret = pinctrl_get_group_pins(pctldev, pin_group,
					&pin_range->range.pins,
					&pin_range->range.npins);
	if (ret < 0) {
		kfree(pin_range);
		return ret;
	}

	pinctrl_add_gpio_range(pctldev, &pin_range->range);

	chip_dbg(chip, "created GPIO range %d->%d ==> %s PINGRP %s\n",
		 gpio_offset, gpio_offset + pin_range->range.npins - 1,
		 pinctrl_dev_get_devname(pctldev), pin_group);

	list_add_tail(&pin_range->node, &gdev->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pingroup_range);

/**
 * gpiochip_add_pin_range() - add a range for GPIO <-> pin mapping
 * @chip: the gpiochip to add the range for
 * @pinctrl_name: the dev_name() of the pin controller to map to
 * @gpio_offset: the start offset in the current gpio_chip number space
 * @pin_offset: the start offset in the pin controller number space
 * @npins: the number of pins from the offset of each pin space (GPIO and
 *	pin controller) to accumulate in this range
 */
int gpiochip_add_pin_range(struct gpio_chip *chip, const char *pinctl_name,
			   unsigned int gpio_offset, unsigned int pin_offset,
			   unsigned int npins)
{
	struct gpio_pin_range *pin_range;
	struct gpio_device *gdev = chip->gpiodev;
	int ret;

	pin_range = kzalloc(sizeof(*pin_range), GFP_KERNEL);
	if (!pin_range) {
		chip_err(chip, "failed to allocate pin ranges\n");
		return -ENOMEM;
	}

	/* Use local offset as range ID */
	pin_range->range.id = gpio_offset;
	pin_range->range.gc = chip;
	pin_range->range.name = chip->label;
	pin_range->range.base = gdev->base + gpio_offset;
	pin_range->range.pin_base = pin_offset;
	pin_range->range.npins = npins;
	pin_range->pctldev = pinctrl_find_and_add_gpio_range(pinctl_name,
			&pin_range->range);
	if (IS_ERR(pin_range->pctldev)) {
		ret = PTR_ERR(pin_range->pctldev);
		chip_err(chip, "could not create pin range\n");
		kfree(pin_range);
		return ret;
	}
	chip_dbg(chip, "created GPIO range %d->%d ==> %s PIN %d->%d\n",
		 gpio_offset, gpio_offset + npins - 1,
		 pinctl_name,
		 pin_offset, pin_offset + npins - 1);

	list_add_tail(&pin_range->node, &gdev->pin_ranges);

	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_add_pin_range);

/**
 * gpiochip_remove_pin_ranges() - remove all the GPIO <-> pin mappings
 * @chip: the chip to remove all the mappings for
 */
void gpiochip_remove_pin_ranges(struct gpio_chip *chip)
{
	struct gpio_pin_range *pin_range, *tmp;
	struct gpio_device *gdev = chip->gpiodev;

	list_for_each_entry_safe(pin_range, tmp, &gdev->pin_ranges, node) {
		list_del(&pin_range->node);
		pinctrl_remove_gpio_range(pin_range->pctldev,
				&pin_range->range);
		kfree(pin_range);
	}
}
EXPORT_SYMBOL_GPL(gpiochip_remove_pin_ranges);

#endif /* CONFIG_PINCTRL */

/* These "optional" allocation calls help prevent drivers from stomping
 * on each other, and help provide better diagnostics in debugfs.
 * They're called even less than the "set direction" calls.
 */
static int __gpiod_request(struct gpio_desc *desc, const char *label)
{
	struct gpio_chip	*chip = desc->gdev->chip;
	int			status;
	unsigned long		flags;

	spin_lock_irqsave(&gpio_lock, flags);

	/* NOTE:  gpio_request() can be called in early boot,
	 * before IRQs are enabled, for non-sleeping (SOC) GPIOs.
	 */

	if (test_and_set_bit(FLAG_REQUESTED, &desc->flags) == 0) {
		desc_set_label(desc, label ? : "?");
		status = 0;
	} else {
		status = -EBUSY;
		goto done;
	}

	if (chip->request) {
		/* chip->request may sleep */
		spin_unlock_irqrestore(&gpio_lock, flags);
		status = chip->request(chip, gpio_chip_hwgpio(desc));
		spin_lock_irqsave(&gpio_lock, flags);

		if (status < 0) {
			desc_set_label(desc, NULL);
			clear_bit(FLAG_REQUESTED, &desc->flags);
			goto done;
		}
	}
	if (chip->get_direction) {
		/* chip->get_direction may sleep */
		spin_unlock_irqrestore(&gpio_lock, flags);
		gpiod_get_direction(desc);
		spin_lock_irqsave(&gpio_lock, flags);
	}
done:
	spin_unlock_irqrestore(&gpio_lock, flags);
	return status;
}

/*
 * This descriptor validation needs to be inserted verbatim into each
 * function taking a descriptor, so we need to use a preprocessor
 * macro to avoid endless duplication. If the desc is NULL it is an
 * optional GPIO and calls should just bail out.
 */
#define VALIDATE_DESC(desc) do { \
	if (!desc) \
		return 0; \
	if (IS_ERR(desc)) {						\
		pr_warn("%s: invalid GPIO (errorpointer)\n", __func__); \
		return PTR_ERR(desc); \
	} \
	if (!desc->gdev) { \
		pr_warn("%s: invalid GPIO (no device)\n", __func__); \
		return -EINVAL; \
	} \
	if ( !desc->gdev->chip ) { \
		dev_warn(&desc->gdev->dev, \
			 "%s: backing chip is gone\n", __func__); \
		return 0; \
	} } while (0)

#define VALIDATE_DESC_VOID(desc) do { \
	if (!desc) \
		return; \
	if (IS_ERR(desc)) {						\
		pr_warn("%s: invalid GPIO (errorpointer)\n", __func__); \
		return; \
	} \
	if (!desc->gdev) { \
		pr_warn("%s: invalid GPIO (no device)\n", __func__); \
		return; \
	} \
	if (!desc->gdev->chip) { \
		dev_warn(&desc->gdev->dev, \
			 "%s: backing chip is gone\n", __func__); \
		return; \
	} } while (0)


int gpiod_request(struct gpio_desc *desc, const char *label)
{
	int status = -EPROBE_DEFER;
	struct gpio_device *gdev;

	VALIDATE_DESC(desc);
	gdev = desc->gdev;

	if (try_module_get(gdev->owner)) {
		status = __gpiod_request(desc, label);
		if (status < 0)
			module_put(gdev->owner);
		else
			get_device(&gdev->dev);
	}

	if (status)
		gpiod_dbg(desc, "%s: status %d\n", __func__, status);

	return status;
}

static bool __gpiod_free(struct gpio_desc *desc)
{
	bool			ret = false;
	unsigned long		flags;
	struct gpio_chip	*chip;

	might_sleep();

	gpiod_unexport(desc);

	spin_lock_irqsave(&gpio_lock, flags);

	chip = desc->gdev->chip;
	if (chip && test_bit(FLAG_REQUESTED, &desc->flags)) {
		if (chip->free) {
			spin_unlock_irqrestore(&gpio_lock, flags);
			might_sleep_if(chip->can_sleep);
			chip->free(chip, gpio_chip_hwgpio(desc));
			spin_lock_irqsave(&gpio_lock, flags);
		}
		desc_set_label(desc, NULL);
		clear_bit(FLAG_ACTIVE_LOW, &desc->flags);
		clear_bit(FLAG_REQUESTED, &desc->flags);
		clear_bit(FLAG_OPEN_DRAIN, &desc->flags);
		clear_bit(FLAG_OPEN_SOURCE, &desc->flags);
		clear_bit(FLAG_IS_HOGGED, &desc->flags);
		ret = true;
	}

	spin_unlock_irqrestore(&gpio_lock, flags);
	return ret;
}

void gpiod_free(struct gpio_desc *desc)
{
	if (desc && desc->gdev && __gpiod_free(desc)) {
		module_put(desc->gdev->owner);
		put_device(&desc->gdev->dev);
	} else {
		WARN_ON(extra_checks);
	}
}

/**
 * gpiochip_is_requested - return string iff signal was requested
 * @chip: controller managing the signal
 * @offset: of signal within controller's 0..(ngpio - 1) range
 *
 * Returns NULL if the GPIO is not currently requested, else a string.
 * The string returned is the label passed to gpio_request(); if none has been
 * passed it is a meaningless, non-NULL constant.
 *
 * This function is for use by GPIO controller drivers.  The label can
 * help with diagnostics, and knowing that the signal is used as a GPIO
 * can help avoid accidentally multiplexing it to another controller.
 */
const char *gpiochip_is_requested(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_desc *desc;

	if (offset >= chip->ngpio)
		return NULL;

	desc = &chip->gpiodev->descs[offset];

	if (test_bit(FLAG_REQUESTED, &desc->flags) == 0)
		return NULL;
	return desc->label;
}
EXPORT_SYMBOL_GPL(gpiochip_is_requested);

/**
 * gpiochip_request_own_desc - Allow GPIO chip to request its own descriptor
 * @desc: GPIO descriptor to request
 * @label: label for the GPIO
 *
 * Function allows GPIO chip drivers to request and use their own GPIO
 * descriptors via gpiolib API. Difference to gpiod_request() is that this
 * function will not increase reference count of the GPIO chip module. This
 * allows the GPIO chip module to be unloaded as needed (we assume that the
 * GPIO chip driver handles freeing the GPIOs it has requested).
 */
struct gpio_desc *gpiochip_request_own_desc(struct gpio_chip *chip, u16 hwnum,
					    const char *label)
{
	struct gpio_desc *desc = gpiochip_get_desc(chip, hwnum);
	int err;

	if (IS_ERR(desc)) {
		chip_err(chip, "failed to get GPIO descriptor\n");
		return desc;
	}

	err = __gpiod_request(desc, label);
	if (err < 0)
		return ERR_PTR(err);

	return desc;
}
EXPORT_SYMBOL_GPL(gpiochip_request_own_desc);

/**
 * gpiochip_free_own_desc - Free GPIO requested by the chip driver
 * @desc: GPIO descriptor to free
 *
 * Function frees the given GPIO requested previously with
 * gpiochip_request_own_desc().
 */
void gpiochip_free_own_desc(struct gpio_desc *desc)
{
	if (desc)
		__gpiod_free(desc);
}
EXPORT_SYMBOL_GPL(gpiochip_free_own_desc);

/*
 * Drivers MUST set GPIO direction before making get/set calls.  In
 * some cases this is done in early boot, before IRQs are enabled.
 *
 * As a rule these aren't called more than once (except for drivers
 * using the open-drain emulation idiom) so these are natural places
 * to accumulate extra debugging checks.  Note that we can't (yet)
 * rely on gpio_request() having been called beforehand.
 */

/**
 * gpiod_direction_input - set the GPIO direction to input
 * @desc:	GPIO to set to input
 *
 * Set the direction of the passed GPIO to input, such as gpiod_get_value() can
 * be called safely on it.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_input(struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int			status = -EINVAL;

	VALIDATE_DESC(desc);
	chip = desc->gdev->chip;

	if (!chip->get || !chip->direction_input) {
		gpiod_warn(desc,
			"%s: missing get() or direction_input() operations\n",
			__func__);
		return -EIO;
	}

	status = chip->direction_input(chip, gpio_chip_hwgpio(desc));
	if (status == 0)
		clear_bit(FLAG_IS_OUT, &desc->flags);

	trace_gpio_direction(desc_to_gpio(desc), 1, status);

	return status;
}
EXPORT_SYMBOL_GPL(gpiod_direction_input);

static int _gpiod_direction_output_raw(struct gpio_desc *desc, int value)
{
	struct gpio_chip *gc = desc->gdev->chip;
	int ret;

	/* GPIOs used for IRQs shall not be set as output */
	if (test_bit(FLAG_USED_AS_IRQ, &desc->flags)) {
		gpiod_err(desc,
			  "%s: tried to set a GPIO tied to an IRQ as output\n",
			  __func__);
		return -EIO;
	}

	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags)) {
		/* First see if we can enable open drain in hardware */
		if (gc->set_single_ended) {
			ret = gc->set_single_ended(gc, gpio_chip_hwgpio(desc),
						   LINE_MODE_OPEN_DRAIN);
			if (!ret)
				goto set_output_value;
		}
		/* Emulate open drain by not actively driving the line high */
		if (value)
			return gpiod_direction_input(desc);
	}
	else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags)) {
		if (gc->set_single_ended) {
			ret = gc->set_single_ended(gc, gpio_chip_hwgpio(desc),
						   LINE_MODE_OPEN_SOURCE);
			if (!ret)
				goto set_output_value;
		}
		/* Emulate open source by not actively driving the line low */
		if (!value)
			return gpiod_direction_input(desc);
	} else {
		/* Make sure to disable open drain/source hardware, if any */
		if (gc->set_single_ended)
			gc->set_single_ended(gc,
					     gpio_chip_hwgpio(desc),
					     LINE_MODE_PUSH_PULL);
	}

set_output_value:
	if (!gc->set || !gc->direction_output) {
		gpiod_warn(desc,
		       "%s: missing set() or direction_output() operations\n",
		       __func__);
		return -EIO;
	}

	ret = gc->direction_output(gc, gpio_chip_hwgpio(desc), value);
	if (!ret)
		set_bit(FLAG_IS_OUT, &desc->flags);
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	trace_gpio_direction(desc_to_gpio(desc), 0, ret);
	return ret;
}

/**
 * gpiod_direction_output_raw - set the GPIO direction to output
 * @desc:	GPIO to set to output
 * @value:	initial output value of the GPIO
 *
 * Set the direction of the passed GPIO to output, such as gpiod_set_value() can
 * be called safely on it. The initial value of the output must be specified
 * as raw value on the physical line without regard for the ACTIVE_LOW status.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_output_raw(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC(desc);
	return _gpiod_direction_output_raw(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_direction_output_raw);

/**
 * gpiod_direction_output - set the GPIO direction to output
 * @desc:	GPIO to set to output
 * @value:	initial output value of the GPIO
 *
 * Set the direction of the passed GPIO to output, such as gpiod_set_value() can
 * be called safely on it. The initial value of the output must be specified
 * as the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account.
 *
 * Return 0 in case of success, else an error code.
 */
int gpiod_direction_output(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC(desc);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	return _gpiod_direction_output_raw(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_direction_output);

/**
 * gpiod_set_debounce - sets @debounce time for a @gpio
 * @gpio: the gpio to set debounce time
 * @debounce: debounce time is microseconds
 *
 * returns -ENOTSUPP if the controller does not support setting
 * debounce.
 */
int gpiod_set_debounce(struct gpio_desc *desc, unsigned debounce)
{
	struct gpio_chip	*chip;

	VALIDATE_DESC(desc);
	chip = desc->gdev->chip;
	if (!chip->set || !chip->set_debounce) {
		gpiod_dbg(desc,
			  "%s: missing set() or set_debounce() operations\n",
			  __func__);
		return -ENOTSUPP;
	}

	return chip->set_debounce(chip, gpio_chip_hwgpio(desc), debounce);
}
EXPORT_SYMBOL_GPL(gpiod_set_debounce);

/**
 * gpiod_is_active_low - test whether a GPIO is active-low or not
 * @desc: the gpio descriptor to test
 *
 * Returns 1 if the GPIO is active-low, 0 otherwise.
 */
int gpiod_is_active_low(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	return test_bit(FLAG_ACTIVE_LOW, &desc->flags);
}
EXPORT_SYMBOL_GPL(gpiod_is_active_low);

/* I/O calls are only valid after configuration completed; the relevant
 * "is this a valid GPIO" error checks should already have been done.
 *
 * "Get" operations are often inlinable as reading a pin value register,
 * and masking the relevant bit in that register.
 *
 * When "set" operations are inlinable, they involve writing that mask to
 * one register to set a low value, or a different register to set it high.
 * Otherwise locking is needed, so there may be little value to inlining.
 *
 *------------------------------------------------------------------------
 *
 * IMPORTANT!!!  The hot paths -- get/set value -- assume that callers
 * have requested the GPIO.  That can include implicit requesting by
 * a direction setting call.  Marking a gpio as requested locks its chip
 * in memory, guaranteeing that these table lookups need no more locking
 * and that gpiochip_remove() will fail.
 *
 * REVISIT when debugging, consider adding some instrumentation to ensure
 * that the GPIO was actually requested.
 */

static int _gpiod_get_raw_value(const struct gpio_desc *desc)
{
	struct gpio_chip	*chip;
	int offset;
	int value;

	chip = desc->gdev->chip;
	offset = gpio_chip_hwgpio(desc);
	value = chip->get ? chip->get(chip, offset) : -EIO;
	value = value < 0 ? value : !!value;
	trace_gpio_value(desc_to_gpio(desc), 1, value);
	return value;
}

/**
 * gpiod_get_raw_value() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status, or negative errno on failure.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_raw_value(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	/* Should be using gpio_get_value_cansleep() */
	WARN_ON(desc->gdev->chip->can_sleep);
	return _gpiod_get_raw_value(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value);

/**
 * gpiod_get_value() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account, or negative errno on failure.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
int gpiod_get_value(const struct gpio_desc *desc)
{
	int value;

	VALIDATE_DESC(desc);
	/* Should be using gpio_get_value_cansleep() */
	WARN_ON(desc->gdev->chip->can_sleep);

	value = _gpiod_get_raw_value(desc);
	if (value < 0)
		return value;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value);

/*
 *  _gpio_set_open_drain_value() - Set the open drain gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherwise it will set to LOW.
 */
static void _gpio_set_open_drain_value(struct gpio_desc *desc, bool value)
{
	int err = 0;
	struct gpio_chip *chip = desc->gdev->chip;
	int offset = gpio_chip_hwgpio(desc);

	if (value) {
		err = chip->direction_input(chip, offset);
		if (!err)
			clear_bit(FLAG_IS_OUT, &desc->flags);
	} else {
		err = chip->direction_output(chip, offset, 0);
		if (!err)
			set_bit(FLAG_IS_OUT, &desc->flags);
	}
	trace_gpio_direction(desc_to_gpio(desc), value, err);
	if (err < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open drain err %d\n",
			  __func__, err);
}

/*
 *  _gpio_set_open_source_value() - Set the open source gpio's value.
 * @desc: gpio descriptor whose state need to be set.
 * @value: Non-zero for setting it HIGH otherwise it will set to LOW.
 */
static void _gpio_set_open_source_value(struct gpio_desc *desc, bool value)
{
	int err = 0;
	struct gpio_chip *chip = desc->gdev->chip;
	int offset = gpio_chip_hwgpio(desc);

	if (value) {
		err = chip->direction_output(chip, offset, 1);
		if (!err)
			set_bit(FLAG_IS_OUT, &desc->flags);
	} else {
		err = chip->direction_input(chip, offset);
		if (!err)
			clear_bit(FLAG_IS_OUT, &desc->flags);
	}
	trace_gpio_direction(desc_to_gpio(desc), !value, err);
	if (err < 0)
		gpiod_err(desc,
			  "%s: Error in set_value for open source err %d\n",
			  __func__, err);
}

static void _gpiod_set_raw_value(struct gpio_desc *desc, bool value)
{
	struct gpio_chip	*chip;

	chip = desc->gdev->chip;
	trace_gpio_value(desc_to_gpio(desc), 0, value);
	if (test_bit(FLAG_OPEN_DRAIN, &desc->flags))
		_gpio_set_open_drain_value(desc, value);
	else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags))
		_gpio_set_open_source_value(desc, value);
	else
		chip->set(chip, gpio_chip_hwgpio(desc), value);
}

/*
 * set multiple outputs on the same chip;
 * use the chip's set_multiple function if available;
 * otherwise set the outputs sequentially;
 * @mask: bit mask array; one bit per output; BITS_PER_LONG bits per word
 *        defines which outputs are to be changed
 * @bits: bit value array; one bit per output; BITS_PER_LONG bits per word
 *        defines the values the outputs specified by mask are to be set to
 */
static void gpio_chip_set_multiple(struct gpio_chip *chip,
				   unsigned long *mask, unsigned long *bits)
{
	if (chip->set_multiple) {
		chip->set_multiple(chip, mask, bits);
	} else {
		int i;
		for (i = 0; i < chip->ngpio; i++) {
			if (mask[BIT_WORD(i)] == 0) {
				/* no more set bits in this mask word;
				 * skip ahead to the next word */
				i = (BIT_WORD(i) + 1) * BITS_PER_LONG - 1;
				continue;
			}
			/* set outputs if the corresponding mask bit is set */
			if (__test_and_clear_bit(i, mask))
				chip->set(chip, i, test_bit(i, bits));
		}
	}
}

void gpiod_set_array_value_complex(bool raw, bool can_sleep,
				   unsigned int array_size,
				   struct gpio_desc **desc_array,
				   int *value_array)
{
	int i = 0;

	while (i < array_size) {
		struct gpio_chip *chip = desc_array[i]->gdev->chip;
		unsigned long mask[BITS_TO_LONGS(chip->ngpio)];
		unsigned long bits[BITS_TO_LONGS(chip->ngpio)];
		int count = 0;

		if (!can_sleep)
			WARN_ON(chip->can_sleep);

		memset(mask, 0, sizeof(mask));
		do {
			struct gpio_desc *desc = desc_array[i];
			int hwgpio = gpio_chip_hwgpio(desc);
			int value = value_array[i];

			if (!raw && test_bit(FLAG_ACTIVE_LOW, &desc->flags))
				value = !value;
			trace_gpio_value(desc_to_gpio(desc), 0, value);
			/*
			 * collect all normal outputs belonging to the same chip
			 * open drain and open source outputs are set individually
			 */
			if (test_bit(FLAG_OPEN_DRAIN, &desc->flags)) {
				_gpio_set_open_drain_value(desc, value);
			} else if (test_bit(FLAG_OPEN_SOURCE, &desc->flags)) {
				_gpio_set_open_source_value(desc, value);
			} else {
				__set_bit(hwgpio, mask);
				if (value)
					__set_bit(hwgpio, bits);
				else
					__clear_bit(hwgpio, bits);
				count++;
			}
			i++;
		} while ((i < array_size) &&
			 (desc_array[i]->gdev->chip == chip));
		/* push collected bits to outputs */
		if (count != 0)
			gpio_chip_set_multiple(chip, mask, bits);
	}
}

/**
 * gpiod_set_raw_value() - assign a gpio's raw value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the raw value of the GPIO, i.e. the value of its physical line without
 * regard for its ACTIVE_LOW status.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_raw_value(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC_VOID(desc);
	/* Should be using gpiod_set_value_cansleep() */
	WARN_ON(desc->gdev->chip->can_sleep);
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_value);

/**
 * gpiod_set_value() - assign a gpio's value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_value(struct gpio_desc *desc, int value)
{
	VALIDATE_DESC_VOID(desc);
	/* Should be using gpiod_set_value_cansleep() */
	WARN_ON(desc->gdev->chip->can_sleep);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value);

/**
 * gpiod_set_raw_array_value() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor / value arrays
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @value_array: array of values to assign
 *
 * Set the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_raw_array_value(unsigned int array_size,
			 struct gpio_desc **desc_array, int *value_array)
{
	if (!desc_array)
		return;
	gpiod_set_array_value_complex(true, false, array_size, desc_array,
				      value_array);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_array_value);

/**
 * gpiod_set_array_value() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor / value arrays
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @value_array: array of values to assign
 *
 * Set the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.
 *
 * This function should be called from contexts where we cannot sleep, and will
 * complain if the GPIO chip functions potentially sleep.
 */
void gpiod_set_array_value(unsigned int array_size,
			   struct gpio_desc **desc_array, int *value_array)
{
	if (!desc_array)
		return;
	gpiod_set_array_value_complex(false, false, array_size, desc_array,
				      value_array);
}
EXPORT_SYMBOL_GPL(gpiod_set_array_value);

/**
 * gpiod_cansleep() - report whether gpio value access may sleep
 * @desc: gpio to check
 *
 */
int gpiod_cansleep(const struct gpio_desc *desc)
{
	VALIDATE_DESC(desc);
	return desc->gdev->chip->can_sleep;
}
EXPORT_SYMBOL_GPL(gpiod_cansleep);

/**
 * gpiod_to_irq() - return the IRQ corresponding to a GPIO
 * @desc: gpio whose IRQ will be returned (already requested)
 *
 * Return the IRQ corresponding to the passed GPIO, or an error code in case of
 * error.
 */
int gpiod_to_irq(const struct gpio_desc *desc)
{
	struct gpio_chip *chip;
	int offset;

	/*
	 * Cannot VALIDATE_DESC() here as gpiod_to_irq() consumer semantics
	 * requires this function to not return zero on an invalid descriptor
	 * but rather a negative error number.
	 */
	if (!desc || IS_ERR(desc) || !desc->gdev || !desc->gdev->chip)
		return -EINVAL;

	chip = desc->gdev->chip;
	offset = gpio_chip_hwgpio(desc);
	if (chip->to_irq) {
		int retirq = chip->to_irq(chip, offset);

		/* Zero means NO_IRQ */
		if (!retirq)
			return -ENXIO;

		return retirq;
	}
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(gpiod_to_irq);

/**
 * gpiochip_lock_as_irq() - lock a GPIO to be used as IRQ
 * @chip: the chip the GPIO to lock belongs to
 * @offset: the offset of the GPIO to lock as IRQ
 *
 * This is used directly by GPIO drivers that want to lock down
 * a certain GPIO line to be used for IRQs.
 */
int gpiochip_lock_as_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_desc *desc;

	desc = gpiochip_get_desc(chip, offset);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	/*
	 * If it's fast: flush the direction setting if something changed
	 * behind our back
	 */
	if (!chip->can_sleep && chip->get_direction) {
		int dir = chip->get_direction(chip, offset);

		if (dir)
			clear_bit(FLAG_IS_OUT, &desc->flags);
		else
			set_bit(FLAG_IS_OUT, &desc->flags);
	}

	if (test_bit(FLAG_IS_OUT, &desc->flags)) {
		chip_err(chip,
			  "%s: tried to flag a GPIO set as output for IRQ\n",
			  __func__);
		return -EIO;
	}

	set_bit(FLAG_USED_AS_IRQ, &desc->flags);
	return 0;
}
EXPORT_SYMBOL_GPL(gpiochip_lock_as_irq);

/**
 * gpiochip_unlock_as_irq() - unlock a GPIO used as IRQ
 * @chip: the chip the GPIO to lock belongs to
 * @offset: the offset of the GPIO to lock as IRQ
 *
 * This is used directly by GPIO drivers that want to indicate
 * that a certain GPIO is no longer used exclusively for IRQ.
 */
void gpiochip_unlock_as_irq(struct gpio_chip *chip, unsigned int offset)
{
	if (offset >= chip->ngpio)
		return;

	clear_bit(FLAG_USED_AS_IRQ, &chip->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_unlock_as_irq);

bool gpiochip_line_is_irq(struct gpio_chip *chip, unsigned int offset)
{
	if (offset >= chip->ngpio)
		return false;

	return test_bit(FLAG_USED_AS_IRQ, &chip->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_irq);

bool gpiochip_line_is_open_drain(struct gpio_chip *chip, unsigned int offset)
{
	if (offset >= chip->ngpio)
		return false;

	return test_bit(FLAG_OPEN_DRAIN, &chip->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_open_drain);

bool gpiochip_line_is_open_source(struct gpio_chip *chip, unsigned int offset)
{
	if (offset >= chip->ngpio)
		return false;

	return test_bit(FLAG_OPEN_SOURCE, &chip->gpiodev->descs[offset].flags);
}
EXPORT_SYMBOL_GPL(gpiochip_line_is_open_source);

/**
 * gpiod_get_raw_value_cansleep() - return a gpio's raw value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's raw value, i.e. the value of the physical line disregarding
 * its ACTIVE_LOW status, or negative errno on failure.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_raw_value_cansleep(const struct gpio_desc *desc)
{
	might_sleep_if(extra_checks);
	VALIDATE_DESC(desc);
	return _gpiod_get_raw_value(desc);
}
EXPORT_SYMBOL_GPL(gpiod_get_raw_value_cansleep);

/**
 * gpiod_get_value_cansleep() - return a gpio's value
 * @desc: gpio whose value will be returned
 *
 * Return the GPIO's logical value, i.e. taking the ACTIVE_LOW status into
 * account, or negative errno on failure.
 *
 * This function is to be called from contexts that can sleep.
 */
int gpiod_get_value_cansleep(const struct gpio_desc *desc)
{
	int value;

	might_sleep_if(extra_checks);
	VALIDATE_DESC(desc);
	value = _gpiod_get_raw_value(desc);
	if (value < 0)
		return value;

	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;

	return value;
}
EXPORT_SYMBOL_GPL(gpiod_get_value_cansleep);

/**
 * gpiod_set_raw_value_cansleep() - assign a gpio's raw value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the raw value of the GPIO, i.e. the value of its physical line without
 * regard for its ACTIVE_LOW status.
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_raw_value_cansleep(struct gpio_desc *desc, int value)
{
	might_sleep_if(extra_checks);
	VALIDATE_DESC_VOID(desc);
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_value_cansleep);

/**
 * gpiod_set_value_cansleep() - assign a gpio's value
 * @desc: gpio whose value will be assigned
 * @value: value to assign
 *
 * Set the logical value of the GPIO, i.e. taking its ACTIVE_LOW status into
 * account
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_value_cansleep(struct gpio_desc *desc, int value)
{
	might_sleep_if(extra_checks);
	VALIDATE_DESC_VOID(desc);
	if (test_bit(FLAG_ACTIVE_LOW, &desc->flags))
		value = !value;
	_gpiod_set_raw_value(desc, value);
}
EXPORT_SYMBOL_GPL(gpiod_set_value_cansleep);

/**
 * gpiod_set_raw_array_value_cansleep() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor / value arrays
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @value_array: array of values to assign
 *
 * Set the raw values of the GPIOs, i.e. the values of the physical lines
 * without regard for their ACTIVE_LOW status.
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_raw_array_value_cansleep(unsigned int array_size,
					struct gpio_desc **desc_array,
					int *value_array)
{
	might_sleep_if(extra_checks);
	if (!desc_array)
		return;
	gpiod_set_array_value_complex(true, true, array_size, desc_array,
				      value_array);
}
EXPORT_SYMBOL_GPL(gpiod_set_raw_array_value_cansleep);

/**
 * gpiod_set_array_value_cansleep() - assign values to an array of GPIOs
 * @array_size: number of elements in the descriptor / value arrays
 * @desc_array: array of GPIO descriptors whose values will be assigned
 * @value_array: array of values to assign
 *
 * Set the logical values of the GPIOs, i.e. taking their ACTIVE_LOW status
 * into account.
 *
 * This function is to be called from contexts that can sleep.
 */
void gpiod_set_array_value_cansleep(unsigned int array_size,
				    struct gpio_desc **desc_array,
				    int *value_array)
{
	might_sleep_if(extra_checks);
	if (!desc_array)
		return;
	gpiod_set_array_value_complex(false, true, array_size, desc_array,
				      value_array);
}
EXPORT_SYMBOL_GPL(gpiod_set_array_value_cansleep);

/**
 * gpiod_add_lookup_table() - register GPIO device consumers
 * @table: table of consumers to register
 */
void gpiod_add_lookup_table(struct gpiod_lookup_table *table)
{
	mutex_lock(&gpio_lookup_lock);

	list_add_tail(&table->list, &gpio_lookup_list);

	mutex_unlock(&gpio_lookup_lock);
}

/**
 * gpiod_remove_lookup_table() - unregister GPIO device consumers
 * @table: table of consumers to unregister
 */
void gpiod_remove_lookup_table(struct gpiod_lookup_table *table)
{
	mutex_lock(&gpio_lookup_lock);

	list_del(&table->list);

	mutex_unlock(&gpio_lookup_lock);
}

static struct gpiod_lookup_table *gpiod_find_lookup_table(struct device *dev)
{
	const char *dev_id = dev ? dev_name(dev) : NULL;
	struct gpiod_lookup_table *table;

	mutex_lock(&gpio_lookup_lock);

	list_for_each_entry(table, &gpio_lookup_list, list) {
		if (table->dev_id && dev_id) {
			/*
			 * Valid strings on both ends, must be identical to have
			 * a match
			 */
			if (!strcmp(table->dev_id, dev_id))
				goto found;
		} else {
			/*
			 * One of the pointers is NULL, so both must be to have
			 * a match
			 */
			if (dev_id == table->dev_id)
				goto found;
		}
	}
	table = NULL;

found:
	mutex_unlock(&gpio_lookup_lock);
	return table;
}

static struct gpio_desc *gpiod_find(struct device *dev, const char *con_id,
				    unsigned int idx,
				    enum gpio_lookup_flags *flags)
{
	struct gpio_desc *desc = ERR_PTR(-ENOENT);
	struct gpiod_lookup_table *table;
	struct gpiod_lookup *p;

	table = gpiod_find_lookup_table(dev);
	if (!table)
		return desc;

	for (p = &table->table[0]; p->chip_label; p++) {
		struct gpio_chip *chip;

		/* idx must always match exactly */
		if (p->idx != idx)
			continue;

		/* If the lookup entry has a con_id, require exact match */
		if (p->con_id && (!con_id || strcmp(p->con_id, con_id)))
			continue;

		chip = find_chip_by_name(p->chip_label);

		if (!chip) {
			dev_err(dev, "cannot find GPIO chip %s\n",
				p->chip_label);
			return ERR_PTR(-ENODEV);
		}

		if (chip->ngpio <= p->chip_hwnum) {
			dev_err(dev,
				"requested GPIO %d is out of range [0..%d] for chip %s\n",
				idx, chip->ngpio, chip->label);
			return ERR_PTR(-EINVAL);
		}

		desc = gpiochip_get_desc(chip, p->chip_hwnum);
		*flags = p->flags;

		return desc;
	}

	return desc;
}

static int dt_gpio_count(struct device *dev, const char *con_id)
{
	int ret;
	char propname[32];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(gpio_suffixes); i++) {
		if (con_id)
			snprintf(propname, sizeof(propname), "%s-%s",
				 con_id, gpio_suffixes[i]);
		else
			snprintf(propname, sizeof(propname), "%s",
				 gpio_suffixes[i]);

		ret = of_gpio_named_count(dev->of_node, propname);
		if (ret >= 0)
			break;
	}
	return ret;
}

static int platform_gpio_count(struct device *dev, const char *con_id)
{
	struct gpiod_lookup_table *table;
	struct gpiod_lookup *p;
	unsigned int count = 0;

	table = gpiod_find_lookup_table(dev);
	if (!table)
		return -ENOENT;

	for (p = &table->table[0]; p->chip_label; p++) {
		if ((con_id && p->con_id && !strcmp(con_id, p->con_id)) ||
		    (!con_id && !p->con_id))
			count++;
	}
	if (!count)
		return -ENOENT;

	return count;
}

/**
 * gpiod_count - return the number of GPIOs associated with a device / function
 *		or -ENOENT if no GPIO has been assigned to the requested function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 */
int gpiod_count(struct device *dev, const char *con_id)
{
	int count = -ENOENT;

	if (IS_ENABLED(CONFIG_OF) && dev && dev->of_node)
		count = dt_gpio_count(dev, con_id);
	else if (IS_ENABLED(CONFIG_ACPI) && dev && ACPI_HANDLE(dev))
		count = acpi_gpio_count(dev, con_id);

	if (count < 0)
		count = platform_gpio_count(dev, con_id);

	return count;
}
EXPORT_SYMBOL_GPL(gpiod_count);

/**
 * gpiod_get - obtain a GPIO for a given GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * Return the GPIO descriptor corresponding to the function con_id of device
 * dev, -ENOENT if no GPIO has been assigned to the requested function, or
 * another IS_ERR() code if an error occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get(struct device *dev, const char *con_id,
					 enum gpiod_flags flags)
{
	return gpiod_get_index(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(gpiod_get);

/**
 * gpiod_get_optional - obtain an optional GPIO for a given GPIO function
 * @dev: GPIO consumer, can be NULL for system-global GPIOs
 * @con_id: function within the GPIO consumer
 * @flags: optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get(), except that when no GPIO was assigned to
 * the requested function it will return NULL. This is convenient for drivers
 * that need to handle optional GPIOs.
 */
struct gpio_desc *__must_check gpiod_get_optional(struct device *dev,
						  const char *con_id,
						  enum gpiod_flags flags)
{
	return gpiod_get_index_optional(dev, con_id, 0, flags);
}
EXPORT_SYMBOL_GPL(gpiod_get_optional);


/**
 * gpiod_configure_flags - helper function to configure a given GPIO
 * @desc:	gpio whose value will be assigned
 * @con_id:	function within the GPIO consumer
 * @lflags:	gpio_lookup_flags - returned from of_find_gpio() or
 *		of_get_gpio_hog()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 *
 * Return 0 on success, -ENOENT if no GPIO has been assigned to the
 * requested function and/or index, or another IS_ERR() code if an error
 * occurred while trying to acquire the GPIO.
 */
static int gpiod_configure_flags(struct gpio_desc *desc, const char *con_id,
		unsigned long lflags, enum gpiod_flags dflags)
{
	int status;

	if (lflags & GPIO_ACTIVE_LOW)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);
	if (lflags & GPIO_OPEN_DRAIN)
		set_bit(FLAG_OPEN_DRAIN, &desc->flags);
	if (lflags & GPIO_OPEN_SOURCE)
		set_bit(FLAG_OPEN_SOURCE, &desc->flags);

	/* No particular flag request, return here... */
	if (!(dflags & GPIOD_FLAGS_BIT_DIR_SET)) {
		pr_debug("no flags found for %s\n", con_id);
		return 0;
	}

	/* Process flags */
	if (dflags & GPIOD_FLAGS_BIT_DIR_OUT)
		status = gpiod_direction_output(desc,
					      dflags & GPIOD_FLAGS_BIT_DIR_VAL);
	else
		status = gpiod_direction_input(desc);

	return status;
}

/**
 * gpiod_get_index - obtain a GPIO from a multi-index GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @idx:	index of the GPIO to obtain in the consumer
 * @flags:	optional GPIO initialization flags
 *
 * This variant of gpiod_get() allows to access GPIOs other than the first
 * defined one for functions that define several GPIOs.
 *
 * Return a valid GPIO descriptor, -ENOENT if no GPIO has been assigned to the
 * requested function and/or index, or another IS_ERR() code if an error
 * occurred while trying to acquire the GPIO.
 */
struct gpio_desc *__must_check gpiod_get_index(struct device *dev,
					       const char *con_id,
					       unsigned int idx,
					       enum gpiod_flags flags)
{
	struct gpio_desc *desc = NULL;
	int status;
	enum gpio_lookup_flags lookupflags = 0;

	dev_dbg(dev, "GPIO lookup for consumer %s\n", con_id);

	if (dev) {
		/* Using device tree? */
		if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
			dev_dbg(dev, "using device tree for GPIO lookup\n");
			desc = of_find_gpio(dev, con_id, idx, &lookupflags);
		} else if (ACPI_COMPANION(dev)) {
			dev_dbg(dev, "using ACPI for GPIO lookup\n");
			desc = acpi_find_gpio(dev, con_id, idx, flags, &lookupflags);
		}
	}

	/*
	 * Either we are not using DT or ACPI, or their lookup did not return
	 * a result. In that case, use platform lookup as a fallback.
	 */
	if (!desc || desc == ERR_PTR(-ENOENT)) {
		dev_dbg(dev, "using lookup tables for GPIO lookup\n");
		desc = gpiod_find(dev, con_id, idx, &lookupflags);
	}

	if (IS_ERR(desc)) {
		dev_dbg(dev, "lookup for GPIO %s failed\n", con_id);
		return desc;
	}

	status = gpiod_request(desc, con_id);
	if (status < 0)
		return ERR_PTR(status);

	status = gpiod_configure_flags(desc, con_id, lookupflags, flags);
	if (status < 0) {
		dev_dbg(dev, "setup of GPIO %s failed\n", con_id);
		gpiod_put(desc);
		return ERR_PTR(status);
	}

	return desc;
}
EXPORT_SYMBOL_GPL(gpiod_get_index);

/**
 * fwnode_get_named_gpiod - obtain a GPIO from firmware node
 * @fwnode:	handle of the firmware node
 * @propname:	name of the firmware property representing the GPIO
 *
 * This function can be used for drivers that get their configuration
 * from firmware.
 *
 * Function properly finds the corresponding GPIO using whatever is the
 * underlying firmware interface and then makes sure that the GPIO
 * descriptor is requested before it is returned to the caller.
 *
 * In case of error an ERR_PTR() is returned.
 */
struct gpio_desc *fwnode_get_named_gpiod(struct fwnode_handle *fwnode,
					 const char *propname)
{
	struct gpio_desc *desc = ERR_PTR(-ENODEV);
	bool active_low = false;
	bool single_ended = false;
	int ret;

	if (!fwnode)
		return ERR_PTR(-EINVAL);

	if (is_of_node(fwnode)) {
		enum of_gpio_flags flags;

		desc = of_get_named_gpiod_flags(to_of_node(fwnode), propname, 0,
						&flags);
		if (!IS_ERR(desc)) {
			active_low = flags & OF_GPIO_ACTIVE_LOW;
			single_ended = flags & OF_GPIO_SINGLE_ENDED;
		}
	} else if (is_acpi_node(fwnode)) {
		struct acpi_gpio_info info;

		desc = acpi_node_get_gpiod(fwnode, propname, 0, &info);
		if (!IS_ERR(desc))
			active_low = info.polarity == GPIO_ACTIVE_LOW;
	}

	if (IS_ERR(desc))
		return desc;

	ret = gpiod_request(desc, NULL);
	if (ret)
		return ERR_PTR(ret);

	if (active_low)
		set_bit(FLAG_ACTIVE_LOW, &desc->flags);

	if (single_ended) {
		if (active_low)
			set_bit(FLAG_OPEN_DRAIN, &desc->flags);
		else
			set_bit(FLAG_OPEN_SOURCE, &desc->flags);
	}

	return desc;
}
EXPORT_SYMBOL_GPL(fwnode_get_named_gpiod);

/**
 * gpiod_get_index_optional - obtain an optional GPIO from a multi-index GPIO
 *                            function
 * @dev: GPIO consumer, can be NULL for system-global GPIOs
 * @con_id: function within the GPIO consumer
 * @index: index of the GPIO to obtain in the consumer
 * @flags: optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get_index(), except that when no GPIO with the
 * specified index was assigned to the requested function it will return NULL.
 * This is convenient for drivers that need to handle optional GPIOs.
 */
struct gpio_desc *__must_check gpiod_get_index_optional(struct device *dev,
							const char *con_id,
							unsigned int index,
							enum gpiod_flags flags)
{
	struct gpio_desc *desc;

	desc = gpiod_get_index(dev, con_id, index, flags);
	if (IS_ERR(desc)) {
		if (PTR_ERR(desc) == -ENOENT)
			return NULL;
	}

	return desc;
}
EXPORT_SYMBOL_GPL(gpiod_get_index_optional);

/**
 * gpiod_hog - Hog the specified GPIO desc given the provided flags
 * @desc:	gpio whose value will be assigned
 * @name:	gpio line name
 * @lflags:	gpio_lookup_flags - returned from of_find_gpio() or
 *		of_get_gpio_hog()
 * @dflags:	gpiod_flags - optional GPIO initialization flags
 */
int gpiod_hog(struct gpio_desc *desc, const char *name,
	      unsigned long lflags, enum gpiod_flags dflags)
{
	struct gpio_chip *chip;
	struct gpio_desc *local_desc;
	int hwnum;
	int status;

	chip = gpiod_to_chip(desc);
	hwnum = gpio_chip_hwgpio(desc);

	local_desc = gpiochip_request_own_desc(chip, hwnum, name);
	if (IS_ERR(local_desc)) {
		status = PTR_ERR(local_desc);
		pr_err("requesting hog GPIO %s (chip %s, offset %d) failed, %d\n",
		       name, chip->label, hwnum, status);
		return status;
	}

	status = gpiod_configure_flags(desc, name, lflags, dflags);
	if (status < 0) {
		pr_err("setup of hog GPIO %s (chip %s, offset %d) failed, %d\n",
		       name, chip->label, hwnum, status);
		gpiochip_free_own_desc(desc);
		return status;
	}

	/* Mark GPIO as hogged so it can be identified and removed later */
	set_bit(FLAG_IS_HOGGED, &desc->flags);

	pr_info("GPIO line %d (%s) hogged as %s%s\n",
		desc_to_gpio(desc), name,
		(dflags&GPIOD_FLAGS_BIT_DIR_OUT) ? "output" : "input",
		(dflags&GPIOD_FLAGS_BIT_DIR_OUT) ?
		  (dflags&GPIOD_FLAGS_BIT_DIR_VAL) ? "/high" : "/low":"");

	return 0;
}

/**
 * gpiochip_free_hogs - Scan gpio-controller chip and release GPIO hog
 * @chip:	gpio chip to act on
 *
 * This is only used by of_gpiochip_remove to free hogged gpios
 */
static void gpiochip_free_hogs(struct gpio_chip *chip)
{
	int id;

	for (id = 0; id < chip->ngpio; id++) {
		if (test_bit(FLAG_IS_HOGGED, &chip->gpiodev->descs[id].flags))
			gpiochip_free_own_desc(&chip->gpiodev->descs[id]);
	}
}

/**
 * gpiod_get_array - obtain multiple GPIOs from a multi-index GPIO function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * This function acquires all the GPIOs defined under a given function.
 *
 * Return a struct gpio_descs containing an array of descriptors, -ENOENT if
 * no GPIO has been assigned to the requested function, or another IS_ERR()
 * code if an error occurred while trying to acquire the GPIOs.
 */
struct gpio_descs *__must_check gpiod_get_array(struct device *dev,
						const char *con_id,
						enum gpiod_flags flags)
{
	struct gpio_desc *desc;
	struct gpio_descs *descs;
	int count;

	count = gpiod_count(dev, con_id);
	if (count < 0)
		return ERR_PTR(count);

	descs = kzalloc(sizeof(*descs) + sizeof(descs->desc[0]) * count,
			GFP_KERNEL);
	if (!descs)
		return ERR_PTR(-ENOMEM);

	for (descs->ndescs = 0; descs->ndescs < count; ) {
		desc = gpiod_get_index(dev, con_id, descs->ndescs, flags);
		if (IS_ERR(desc)) {
			gpiod_put_array(descs);
			return ERR_CAST(desc);
		}
		descs->desc[descs->ndescs] = desc;
		descs->ndescs++;
	}
	return descs;
}
EXPORT_SYMBOL_GPL(gpiod_get_array);

/**
 * gpiod_get_array_optional - obtain multiple GPIOs from a multi-index GPIO
 *                            function
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 * @flags:	optional GPIO initialization flags
 *
 * This is equivalent to gpiod_get_array(), except that when no GPIO was
 * assigned to the requested function it will return NULL.
 */
struct gpio_descs *__must_check gpiod_get_array_optional(struct device *dev,
							const char *con_id,
							enum gpiod_flags flags)
{
	struct gpio_descs *descs;

	descs = gpiod_get_array(dev, con_id, flags);
	if (IS_ERR(descs) && (PTR_ERR(descs) == -ENOENT))
		return NULL;

	return descs;
}
EXPORT_SYMBOL_GPL(gpiod_get_array_optional);

/**
 * gpiod_put - dispose of a GPIO descriptor
 * @desc:	GPIO descriptor to dispose of
 *
 * No descriptor can be used after gpiod_put() has been called on it.
 */
void gpiod_put(struct gpio_desc *desc)
{
	gpiod_free(desc);
}
EXPORT_SYMBOL_GPL(gpiod_put);

/**
 * gpiod_put_array - dispose of multiple GPIO descriptors
 * @descs:	struct gpio_descs containing an array of descriptors
 */
void gpiod_put_array(struct gpio_descs *descs)
{
	unsigned int i;

	for (i = 0; i < descs->ndescs; i++)
		gpiod_put(descs->desc[i]);

	kfree(descs);
}
EXPORT_SYMBOL_GPL(gpiod_put_array);

static int __init gpiolib_dev_init(void)
{
	int ret;

	/* Register GPIO sysfs bus */
	ret  = bus_register(&gpio_bus_type);
	if (ret < 0) {
		pr_err("gpiolib: could not register GPIO bus type\n");
		return ret;
	}

	ret = alloc_chrdev_region(&gpio_devt, 0, GPIO_DEV_MAX, "gpiochip");
	if (ret < 0) {
		pr_err("gpiolib: failed to allocate char dev region\n");
		bus_unregister(&gpio_bus_type);
	} else {
		gpiolib_initialized = true;
		gpiochip_setup_devs();
	}
	return ret;
}
core_initcall(gpiolib_dev_init);

#ifdef CONFIG_DEBUG_FS

static void gpiolib_dbg_show(struct seq_file *s, struct gpio_device *gdev)
{
	unsigned		i;
	struct gpio_chip	*chip = gdev->chip;
	unsigned		gpio = gdev->base;
	struct gpio_desc	*gdesc = &gdev->descs[0];
	int			is_out;
	int			is_irq;

	for (i = 0; i < gdev->ngpio; i++, gpio++, gdesc++) {
		if (!test_bit(FLAG_REQUESTED, &gdesc->flags)) {
			if (gdesc->name) {
				seq_printf(s, " gpio-%-3d (%-20.20s)\n",
					   gpio, gdesc->name);
			}
			continue;
		}

		gpiod_get_direction(gdesc);
		is_out = test_bit(FLAG_IS_OUT, &gdesc->flags);
		is_irq = test_bit(FLAG_USED_AS_IRQ, &gdesc->flags);
		seq_printf(s, " gpio-%-3d (%-20.20s|%-20.20s) %s %s %s",
			gpio, gdesc->name ? gdesc->name : "", gdesc->label,
			is_out ? "out" : "in ",
			chip->get
				? (chip->get(chip, i) ? "hi" : "lo")
				: "?  ",
			is_irq ? "IRQ" : "   ");
		seq_printf(s, "\n");
	}
}

static void *gpiolib_seq_start(struct seq_file *s, loff_t *pos)
{
	unsigned long flags;
	struct gpio_device *gdev = NULL;
	loff_t index = *pos;

	s->private = "";

	spin_lock_irqsave(&gpio_lock, flags);
	list_for_each_entry(gdev, &gpio_devices, list)
		if (index-- == 0) {
			spin_unlock_irqrestore(&gpio_lock, flags);
			return gdev;
		}
	spin_unlock_irqrestore(&gpio_lock, flags);

	return NULL;
}

static void *gpiolib_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned long flags;
	struct gpio_device *gdev = v;
	void *ret = NULL;

	spin_lock_irqsave(&gpio_lock, flags);
	if (list_is_last(&gdev->list, &gpio_devices))
		ret = NULL;
	else
		ret = list_entry(gdev->list.next, struct gpio_device, list);
	spin_unlock_irqrestore(&gpio_lock, flags);

	s->private = "\n";
	++*pos;

	return ret;
}

static void gpiolib_seq_stop(struct seq_file *s, void *v)
{
}

static int gpiolib_seq_show(struct seq_file *s, void *v)
{
	struct gpio_device *gdev = v;
	struct gpio_chip *chip = gdev->chip;
	struct device *parent;

	if (!chip) {
		seq_printf(s, "%s%s: (dangling chip)", (char *)s->private,
			   dev_name(&gdev->dev));
		return 0;
	}

	seq_printf(s, "%s%s: GPIOs %d-%d", (char *)s->private,
		   dev_name(&gdev->dev),
		   gdev->base, gdev->base + gdev->ngpio - 1);
	parent = chip->parent;
	if (parent)
		seq_printf(s, ", parent: %s/%s",
			   parent->bus ? parent->bus->name : "no-bus",
			   dev_name(parent));
	if (chip->label)
		seq_printf(s, ", %s", chip->label);
	if (chip->can_sleep)
		seq_printf(s, ", can sleep");
	seq_printf(s, ":\n");

	if (chip->dbg_show)
		chip->dbg_show(s, chip);
	else
		gpiolib_dbg_show(s, gdev);

	return 0;
}

static const struct seq_operations gpiolib_seq_ops = {
	.start = gpiolib_seq_start,
	.next = gpiolib_seq_next,
	.stop = gpiolib_seq_stop,
	.show = gpiolib_seq_show,
};

static int gpiolib_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &gpiolib_seq_ops);
}

static const struct file_operations gpiolib_operations = {
	.owner		= THIS_MODULE,
	.open		= gpiolib_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init gpiolib_debugfs_init(void)
{
	/* /sys/kernel/debug/gpio */
	(void) debugfs_create_file("gpio", S_IFREG | S_IRUGO,
				NULL, NULL, &gpiolib_operations);
	return 0;
}
subsys_initcall(gpiolib_debugfs_init);

#endif	/* DEBUG_FS */
