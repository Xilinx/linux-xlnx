/*
 * hwlat_detector.c - A simple Hardware Latency detector.
 *
 * Use this module to detect large system latencies induced by the behavior of
 * certain underlying system hardware or firmware, independent of Linux itself.
 * The code was developed originally to detect the presence of SMIs on Intel
 * and AMD systems, although there is no dependency upon x86 herein.
 *
 * The classical example usage of this module is in detecting the presence of
 * SMIs or System Management Interrupts on Intel and AMD systems. An SMI is a
 * somewhat special form of hardware interrupt spawned from earlier CPU debug
 * modes in which the (BIOS/EFI/etc.) firmware arranges for the South Bridge
 * LPC (or other device) to generate a special interrupt under certain
 * circumstances, for example, upon expiration of a special SMI timer device,
 * due to certain external thermal readings, on certain I/O address accesses,
 * and other situations. An SMI hits a special CPU pin, triggers a special
 * SMI mode (complete with special memory map), and the OS is unaware.
 *
 * Although certain hardware-inducing latencies are necessary (for example,
 * a modern system often requires an SMI handler for correct thermal control
 * and remote management) they can wreak havoc upon any OS-level performance
 * guarantees toward low-latency, especially when the OS is not even made
 * aware of the presence of these interrupts. For this reason, we need a
 * somewhat brute force mechanism to detect these interrupts. In this case,
 * we do it by hogging all of the CPU(s) for configurable timer intervals,
 * sampling the built-in CPU timer, looking for discontiguous readings.
 *
 * WARNING: This implementation necessarily introduces latencies. Therefore,
 *          you should NEVER use this module in a production environment
 *          requiring any kind of low-latency performance guarantee(s).
 *
 * Copyright (C) 2008-2009 Jon Masters, Red Hat, Inc. <jcm@redhat.com>
 *
 * Includes useful feedback from Clark Williams <clark@redhat.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ring_buffer.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/trace_clock.h>

#define BUF_SIZE_DEFAULT	262144UL		/* 8K*(sizeof(entry)) */
#define BUF_FLAGS		(RB_FL_OVERWRITE)	/* no block on full */
#define U64STR_SIZE		22			/* 20 digits max */

#define VERSION			"1.0.0"
#define BANNER			"hwlat_detector: "
#define DRVNAME			"hwlat_detector"
#define DEFAULT_SAMPLE_WINDOW	1000000			/* 1s */
#define DEFAULT_SAMPLE_WIDTH	500000			/* 0.5s */
#define DEFAULT_LAT_THRESHOLD	10			/* 10us */

/* Module metadata */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Masters <jcm@redhat.com>");
MODULE_DESCRIPTION("A simple hardware latency detector");
MODULE_VERSION(VERSION);

/* Module parameters */

static int debug;
static int enabled;
static int threshold;

module_param(debug, int, 0);			/* enable debug */
module_param(enabled, int, 0);			/* enable detector */
module_param(threshold, int, 0);		/* latency threshold */

/* Buffering and sampling */

static struct ring_buffer *ring_buffer;		/* sample buffer */
static DEFINE_MUTEX(ring_buffer_mutex);		/* lock changes */
static unsigned long buf_size = BUF_SIZE_DEFAULT;
static struct task_struct *kthread;		/* sampling thread */

/* DebugFS filesystem entries */

static struct dentry *debug_dir;		/* debugfs directory */
static struct dentry *debug_max;		/* maximum TSC delta */
static struct dentry *debug_count;		/* total detect count */
static struct dentry *debug_sample_width;	/* sample width us */
static struct dentry *debug_sample_window;	/* sample window us */
static struct dentry *debug_sample;		/* raw samples us */
static struct dentry *debug_threshold;		/* threshold us */
static struct dentry *debug_enable;		/* enable/disable */

/* Individual samples and global state */

struct sample;					/* latency sample */
struct data;					/* Global state */

/* Sampling functions */
static int __buffer_add_sample(struct sample *sample);
static struct sample *buffer_get_sample(struct sample *sample);

/* Threading and state */
static int kthread_fn(void *unused);
static int start_kthread(void);
static int stop_kthread(void);
static void __reset_stats(void);
static int init_stats(void);

/* Debugfs interface */
static ssize_t simple_data_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos, const u64 *entry);
static ssize_t simple_data_write(struct file *filp, const char __user *ubuf,
				 size_t cnt, loff_t *ppos, u64 *entry);
static int debug_sample_fopen(struct inode *inode, struct file *filp);
static ssize_t debug_sample_fread(struct file *filp, char __user *ubuf,
				  size_t cnt, loff_t *ppos);
static int debug_sample_release(struct inode *inode, struct file *filp);
static int debug_enable_fopen(struct inode *inode, struct file *filp);
static ssize_t debug_enable_fread(struct file *filp, char __user *ubuf,
				  size_t cnt, loff_t *ppos);
static ssize_t debug_enable_fwrite(struct file *file,
				   const char __user *user_buffer,
				   size_t user_size, loff_t *offset);

/* Initialization functions */
static int init_debugfs(void);
static void free_debugfs(void);
static int detector_init(void);
static void detector_exit(void);

/* Individual latency samples are stored here when detected and packed into
 * the ring_buffer circular buffer, where they are overwritten when
 * more than buf_size/sizeof(sample) samples are received. */
struct sample {
	u64		seqnum;		/* unique sequence */
	u64		duration;	/* ktime delta */
	u64		outer_duration;	/* ktime delta (outer loop) */
	struct timespec	timestamp;	/* wall time */
	unsigned long   lost;
};

/* keep the global state somewhere. */
static struct data {

	struct mutex lock;		/* protect changes */

	u64	count;			/* total since reset */
	u64	max_sample;		/* max hardware latency */
	u64	threshold;		/* sample threshold level */

	u64	sample_window;		/* total sampling window (on+off) */
	u64	sample_width;		/* active sampling portion of window */

	atomic_t sample_open;		/* whether the sample file is open */

	wait_queue_head_t wq;		/* waitqeue for new sample values */

} data;

/**
 * __buffer_add_sample - add a new latency sample recording to the ring buffer
 * @sample: The new latency sample value
 *
 * This receives a new latency sample and records it in a global ring buffer.
 * No additional locking is used in this case.
 */
static int __buffer_add_sample(struct sample *sample)
{
	return ring_buffer_write(ring_buffer,
				 sizeof(struct sample), sample);
}

/**
 * buffer_get_sample - remove a hardware latency sample from the ring buffer
 * @sample: Pre-allocated storage for the sample
 *
 * This retrieves a hardware latency sample from the global circular buffer
 */
static struct sample *buffer_get_sample(struct sample *sample)
{
	struct ring_buffer_event *e = NULL;
	struct sample *s = NULL;
	unsigned int cpu = 0;

	if (!sample)
		return NULL;

	mutex_lock(&ring_buffer_mutex);
	for_each_online_cpu(cpu) {
		e = ring_buffer_consume(ring_buffer, cpu, NULL, &sample->lost);
		if (e)
			break;
	}

	if (e) {
		s = ring_buffer_event_data(e);
		memcpy(sample, s, sizeof(struct sample));
	} else
		sample = NULL;
	mutex_unlock(&ring_buffer_mutex);

	return sample;
}

#ifndef CONFIG_TRACING
#define time_type	ktime_t
#define time_get()	ktime_get()
#define time_to_us(x)	ktime_to_us(x)
#define time_sub(a, b)	ktime_sub(a, b)
#define init_time(a, b)	(a).tv64 = b
#define time_u64(a)	((a).tv64)
#else
#define time_type	u64
#define time_get()	trace_clock_local()
#define time_to_us(x)	div_u64(x, 1000)
#define time_sub(a, b)	((a) - (b))
#define init_time(a, b)	(a = b)
#define time_u64(a)	a
#endif
/**
 * get_sample - sample the CPU TSC and look for likely hardware latencies
 *
 * Used to repeatedly capture the CPU TSC (or similar), looking for potential
 * hardware-induced latency. Called with interrupts disabled and with
 * data.lock held.
 */
static int get_sample(void)
{
	time_type start, t1, t2, last_t2;
	s64 diff, total = 0;
	u64 sample = 0;
	u64 outer_sample = 0;
	int ret = -1;

	init_time(last_t2, 0);
	start = time_get(); /* start timestamp */

	do {

		t1 = time_get();	/* we'll look for a discontinuity */
		t2 = time_get();

		if (time_u64(last_t2)) {
			/* Check the delta from outer loop (t2 to next t1) */
			diff = time_to_us(time_sub(t1, last_t2));
			/* This shouldn't happen */
			if (diff < 0) {
				pr_err(BANNER "time running backwards\n");
				goto out;
			}
			if (diff > outer_sample)
				outer_sample = diff;
		}
		last_t2 = t2;

		total = time_to_us(time_sub(t2, start)); /* sample width */

		/* This checks the inner loop (t1 to t2) */
		diff = time_to_us(time_sub(t2, t1));     /* current diff */

		/* This shouldn't happen */
		if (diff < 0) {
			pr_err(BANNER "time running backwards\n");
			goto out;
		}

		if (diff > sample)
			sample = diff; /* only want highest value */

	} while (total <= data.sample_width);

	ret = 0;

	/* If we exceed the threshold value, we have found a hardware latency */
	if (sample > data.threshold || outer_sample > data.threshold) {
		struct sample s;

		ret = 1;

		data.count++;
		s.seqnum = data.count;
		s.duration = sample;
		s.outer_duration = outer_sample;
		s.timestamp = CURRENT_TIME;
		__buffer_add_sample(&s);

		/* Keep a running maximum ever recorded hardware latency */
		if (sample > data.max_sample)
			data.max_sample = sample;
	}

out:
	return ret;
}

/*
 * kthread_fn - The CPU time sampling/hardware latency detection kernel thread
 * @unused: A required part of the kthread API.
 *
 * Used to periodically sample the CPU TSC via a call to get_sample. We
 * disable interrupts, which does (intentionally) introduce latency since we
 * need to ensure nothing else might be running (and thus pre-empting).
 * Obviously this should never be used in production environments.
 *
 * Currently this runs on which ever CPU it was scheduled on, but most
 * real-worald hardware latency situations occur across several CPUs,
 * but we might later generalize this if we find there are any actualy
 * systems with alternate SMI delivery or other hardware latencies.
 */
static int kthread_fn(void *unused)
{
	int ret;
	u64 interval;

	while (!kthread_should_stop()) {

		mutex_lock(&data.lock);

		local_irq_disable();
		ret = get_sample();
		local_irq_enable();

		if (ret > 0)
			wake_up(&data.wq); /* wake up reader(s) */

		interval = data.sample_window - data.sample_width;
		do_div(interval, USEC_PER_MSEC); /* modifies interval value */

		mutex_unlock(&data.lock);

		if (msleep_interruptible(interval))
			break;
	}

	return 0;
}

/**
 * start_kthread - Kick off the hardware latency sampling/detector kthread
 *
 * This starts a kernel thread that will sit and sample the CPU timestamp
 * counter (TSC or similar) and look for potential hardware latencies.
 */
static int start_kthread(void)
{
	kthread = kthread_run(kthread_fn, NULL,
					DRVNAME);
	if (IS_ERR(kthread)) {
		pr_err(BANNER "could not start sampling thread\n");
		enabled = 0;
		return -ENOMEM;
	}

	return 0;
}

/**
 * stop_kthread - Inform the hardware latency samping/detector kthread to stop
 *
 * This kicks the running hardware latency sampling/detector kernel thread and
 * tells it to stop sampling now. Use this on unload and at system shutdown.
 */
static int stop_kthread(void)
{
	int ret;

	ret = kthread_stop(kthread);

	return ret;
}

/**
 * __reset_stats - Reset statistics for the hardware latency detector
 *
 * We use data to store various statistics and global state. We call this
 * function in order to reset those when "enable" is toggled on or off, and
 * also at initialization. Should be called with data.lock held.
 */
static void __reset_stats(void)
{
	data.count = 0;
	data.max_sample = 0;
	ring_buffer_reset(ring_buffer); /* flush out old sample entries */
}

/**
 * init_stats - Setup global state statistics for the hardware latency detector
 *
 * We use data to store various statistics and global state. We also use
 * a global ring buffer (ring_buffer) to keep raw samples of detected hardware
 * induced system latencies. This function initializes these structures and
 * allocates the global ring buffer also.
 */
static int init_stats(void)
{
	int ret = -ENOMEM;

	mutex_init(&data.lock);
	init_waitqueue_head(&data.wq);
	atomic_set(&data.sample_open, 0);

	ring_buffer = ring_buffer_alloc(buf_size, BUF_FLAGS);

	if (WARN(!ring_buffer, KERN_ERR BANNER
			       "failed to allocate ring buffer!\n"))
		goto out;

	__reset_stats();
	data.threshold = threshold ?: DEFAULT_LAT_THRESHOLD; /* threshold us */
	data.sample_window = DEFAULT_SAMPLE_WINDOW; /* window us */
	data.sample_width = DEFAULT_SAMPLE_WIDTH;   /* width us */

	ret = 0;

out:
	return ret;

}

/*
 * simple_data_read - Wrapper read function for global state debugfs entries
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 * @entry: The entry to read from
 *
 * This function provides a generic read implementation for the global state
 * "data" structure debugfs filesystem entries. It would be nice to use
 * simple_attr_read directly, but we need to make sure that the data.lock
 * is held during the actual read.
 */
static ssize_t simple_data_read(struct file *filp, char __user *ubuf,
				size_t cnt, loff_t *ppos, const u64 *entry)
{
	char buf[U64STR_SIZE];
	u64 val = 0;
	int len = 0;

	memset(buf, 0, sizeof(buf));

	if (!entry)
		return -EFAULT;

	mutex_lock(&data.lock);
	val = *entry;
	mutex_unlock(&data.lock);

	len = snprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)val);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);

}

/*
 * simple_data_write - Wrapper write function for global state debugfs entries
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to write value from
 * @cnt: The maximum number of bytes to write
 * @ppos: The current "file" position
 * @entry: The entry to write to
 *
 * This function provides a generic write implementation for the global state
 * "data" structure debugfs filesystem entries. It would be nice to use
 * simple_attr_write directly, but we need to make sure that the data.lock
 * is held during the actual write.
 */
static ssize_t simple_data_write(struct file *filp, const char __user *ubuf,
				 size_t cnt, loff_t *ppos, u64 *entry)
{
	char buf[U64STR_SIZE];
	int csize = min(cnt, sizeof(buf));
	u64 val = 0;
	int err = 0;

	memset(buf, '\0', sizeof(buf));
	if (copy_from_user(buf, ubuf, csize))
		return -EFAULT;

	buf[U64STR_SIZE-1] = '\0';			/* just in case */
	err = kstrtoull(buf, 10, &val);
	if (err)
		return -EINVAL;

	mutex_lock(&data.lock);
	*entry = val;
	mutex_unlock(&data.lock);

	return csize;
}

/**
 * debug_count_fopen - Open function for "count" debugfs entry
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "count" debugfs
 * interface to the hardware latency detector.
 */
static int debug_count_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_count_fread - Read function for "count" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "count" debugfs
 * interface to the hardware latency detector. Can be used to read the
 * number of latency readings exceeding the configured threshold since
 * the detector was last reset (e.g. by writing a zero into "count").
 */
static ssize_t debug_count_fread(struct file *filp, char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	return simple_data_read(filp, ubuf, cnt, ppos, &data.count);
}

/**
 * debug_count_fwrite - Write function for "count" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "count" debugfs
 * interface to the hardware latency detector. Can be used to write a
 * desired value, especially to zero the total count.
 */
static ssize_t  debug_count_fwrite(struct file *filp,
				       const char __user *ubuf,
				       size_t cnt,
				       loff_t *ppos)
{
	return simple_data_write(filp, ubuf, cnt, ppos, &data.count);
}

/**
 * debug_enable_fopen - Dummy open function for "enable" debugfs interface
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "enable" debugfs
 * interface to the hardware latency detector.
 */
static int debug_enable_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_enable_fread - Read function for "enable" debugfs interface
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "enable" debugfs
 * interface to the hardware latency detector. Can be used to determine
 * whether the detector is currently enabled ("0\n" or "1\n" returned).
 */
static ssize_t debug_enable_fread(struct file *filp, char __user *ubuf,
				      size_t cnt, loff_t *ppos)
{
	char buf[4];

	if ((cnt < sizeof(buf)) || (*ppos))
		return 0;

	buf[0] = enabled ? '1' : '0';
	buf[1] = '\n';
	buf[2] = '\0';
	if (copy_to_user(ubuf, buf, strlen(buf)))
		return -EFAULT;
	return *ppos = strlen(buf);
}

/**
 * debug_enable_fwrite - Write function for "enable" debugfs interface
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "enable" debugfs
 * interface to the hardware latency detector. Can be used to enable or
 * disable the detector, which will have the side-effect of possibly
 * also resetting the global stats and kicking off the measuring
 * kthread (on an enable) or the converse (upon a disable).
 */
static ssize_t  debug_enable_fwrite(struct file *filp,
					const char __user *ubuf,
					size_t cnt,
					loff_t *ppos)
{
	char buf[4];
	int csize = min(cnt, sizeof(buf));
	long val = 0;
	int err = 0;

	memset(buf, '\0', sizeof(buf));
	if (copy_from_user(buf, ubuf, csize))
		return -EFAULT;

	buf[sizeof(buf)-1] = '\0';			/* just in case */
	err = kstrtoul(buf, 10, &val);
	if (0 != err)
		return -EINVAL;

	if (val) {
		if (enabled)
			goto unlock;
		enabled = 1;
		__reset_stats();
		if (start_kthread())
			return -EFAULT;
	} else {
		if (!enabled)
			goto unlock;
		enabled = 0;
		err = stop_kthread();
		if (err) {
			pr_err(BANNER "cannot stop kthread\n");
			return -EFAULT;
		}
		wake_up(&data.wq);		/* reader(s) should return */
	}
unlock:
	return csize;
}

/**
 * debug_max_fopen - Open function for "max" debugfs entry
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "max" debugfs
 * interface to the hardware latency detector.
 */
static int debug_max_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_max_fread - Read function for "max" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "max" debugfs
 * interface to the hardware latency detector. Can be used to determine
 * the maximum latency value observed since it was last reset.
 */
static ssize_t debug_max_fread(struct file *filp, char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	return simple_data_read(filp, ubuf, cnt, ppos, &data.max_sample);
}

/**
 * debug_max_fwrite - Write function for "max" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "max" debugfs
 * interface to the hardware latency detector. Can be used to reset the
 * maximum or set it to some other desired value - if, then, subsequent
 * measurements exceed this value, the maximum will be updated.
 */
static ssize_t  debug_max_fwrite(struct file *filp,
				     const char __user *ubuf,
				     size_t cnt,
				     loff_t *ppos)
{
	return simple_data_write(filp, ubuf, cnt, ppos, &data.max_sample);
}


/**
 * debug_sample_fopen - An open function for "sample" debugfs interface
 * @inode: The in-kernel inode representation of this debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function handles opening the "sample" file within the hardware
 * latency detector debugfs directory interface. This file is used to read
 * raw samples from the global ring_buffer and allows the user to see a
 * running latency history. Can be opened blocking or non-blocking,
 * affecting whether it behaves as a buffer read pipe, or does not.
 * Implements simple locking to prevent multiple simultaneous use.
 */
static int debug_sample_fopen(struct inode *inode, struct file *filp)
{
	if (!atomic_add_unless(&data.sample_open, 1, 1))
		return -EBUSY;
	else
		return 0;
}

/**
 * debug_sample_fread - A read function for "sample" debugfs interface
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that will contain the samples read
 * @cnt: The maximum bytes to read from the debugfs "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function handles reading from the "sample" file within the hardware
 * latency detector debugfs directory interface. This file is used to read
 * raw samples from the global ring_buffer and allows the user to see a
 * running latency history. By default this will block pending a new
 * value written into the sample buffer, unless there are already a
 * number of value(s) waiting in the buffer, or the sample file was
 * previously opened in a non-blocking mode of operation.
 */
static ssize_t debug_sample_fread(struct file *filp, char __user *ubuf,
					size_t cnt, loff_t *ppos)
{
	int len = 0;
	char buf[64];
	struct sample *sample = NULL;

	if (!enabled)
		return 0;

	sample = kzalloc(sizeof(struct sample), GFP_KERNEL);
	if (!sample)
		return -ENOMEM;

	while (!buffer_get_sample(sample)) {

		DEFINE_WAIT(wait);

		if (filp->f_flags & O_NONBLOCK) {
			len = -EAGAIN;
			goto out;
		}

		prepare_to_wait(&data.wq, &wait, TASK_INTERRUPTIBLE);
		schedule();
		finish_wait(&data.wq, &wait);

		if (signal_pending(current)) {
			len = -EINTR;
			goto out;
		}

		if (!enabled) {			/* enable was toggled */
			len = 0;
			goto out;
		}
	}

	len = snprintf(buf, sizeof(buf), "%010lu.%010lu\t%llu\t%llu\n",
		       sample->timestamp.tv_sec,
		       sample->timestamp.tv_nsec,
		       sample->duration,
		       sample->outer_duration);


	/* handling partial reads is more trouble than it's worth */
	if (len > cnt)
		goto out;

	if (copy_to_user(ubuf, buf, len))
		len = -EFAULT;

out:
	kfree(sample);
	return len;
}

/**
 * debug_sample_release - Release function for "sample" debugfs interface
 * @inode: The in-kernel inode represenation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function completes the close of the debugfs interface "sample" file.
 * Frees the sample_open "lock" so that other users may open the interface.
 */
static int debug_sample_release(struct inode *inode, struct file *filp)
{
	atomic_dec(&data.sample_open);

	return 0;
}

/**
 * debug_threshold_fopen - Open function for "threshold" debugfs entry
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "threshold" debugfs
 * interface to the hardware latency detector.
 */
static int debug_threshold_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_threshold_fread - Read function for "threshold" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "threshold" debugfs
 * interface to the hardware latency detector. It can be used to determine
 * the current threshold level at which a latency will be recorded in the
 * global ring buffer, typically on the order of 10us.
 */
static ssize_t debug_threshold_fread(struct file *filp, char __user *ubuf,
					 size_t cnt, loff_t *ppos)
{
	return simple_data_read(filp, ubuf, cnt, ppos, &data.threshold);
}

/**
 * debug_threshold_fwrite - Write function for "threshold" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "threshold" debugfs
 * interface to the hardware latency detector. It can be used to configure
 * the threshold level at which any subsequently detected latencies will
 * be recorded into the global ring buffer.
 */
static ssize_t  debug_threshold_fwrite(struct file *filp,
					const char __user *ubuf,
					size_t cnt,
					loff_t *ppos)
{
	int ret;

	ret = simple_data_write(filp, ubuf, cnt, ppos, &data.threshold);

	if (enabled)
		wake_up_process(kthread);

	return ret;
}

/**
 * debug_width_fopen - Open function for "width" debugfs entry
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "width" debugfs
 * interface to the hardware latency detector.
 */
static int debug_width_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_width_fread - Read function for "width" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "width" debugfs
 * interface to the hardware latency detector. It can be used to determine
 * for how many us of the total window us we will actively sample for any
 * hardware-induced latecy periods. Obviously, it is not possible to
 * sample constantly and have the system respond to a sample reader, or,
 * worse, without having the system appear to have gone out to lunch.
 */
static ssize_t debug_width_fread(struct file *filp, char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	return simple_data_read(filp, ubuf, cnt, ppos, &data.sample_width);
}

/**
 * debug_width_fwrite - Write function for "width" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "width" debugfs
 * interface to the hardware latency detector. It can be used to configure
 * for how many us of the total window us we will actively sample for any
 * hardware-induced latency periods. Obviously, it is not possible to
 * sample constantly and have the system respond to a sample reader, or,
 * worse, without having the system appear to have gone out to lunch. It
 * is enforced that width is less that the total window size.
 */
static ssize_t  debug_width_fwrite(struct file *filp,
				       const char __user *ubuf,
				       size_t cnt,
				       loff_t *ppos)
{
	char buf[U64STR_SIZE];
	int csize = min(cnt, sizeof(buf));
	u64 val = 0;
	int err = 0;

	memset(buf, '\0', sizeof(buf));
	if (copy_from_user(buf, ubuf, csize))
		return -EFAULT;

	buf[U64STR_SIZE-1] = '\0';			/* just in case */
	err = kstrtoull(buf, 10, &val);
	if (0 != err)
		return -EINVAL;

	mutex_lock(&data.lock);
	if (val < data.sample_window)
		data.sample_width = val;
	else {
		mutex_unlock(&data.lock);
		return -EINVAL;
	}
	mutex_unlock(&data.lock);

	if (enabled)
		wake_up_process(kthread);

	return csize;
}

/**
 * debug_window_fopen - Open function for "window" debugfs entry
 * @inode: The in-kernel inode representation of the debugfs "file"
 * @filp: The active open file structure for the debugfs "file"
 *
 * This function provides an open implementation for the "window" debugfs
 * interface to the hardware latency detector. The window is the total time
 * in us that will be considered one sample period. Conceptually, windows
 * occur back-to-back and contain a sample width period during which
 * actual sampling occurs.
 */
static int debug_window_fopen(struct inode *inode, struct file *filp)
{
	return 0;
}

/**
 * debug_window_fread - Read function for "window" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The userspace provided buffer to read value into
 * @cnt: The maximum number of bytes to read
 * @ppos: The current "file" position
 *
 * This function provides a read implementation for the "window" debugfs
 * interface to the hardware latency detector. The window is the total time
 * in us that will be considered one sample period. Conceptually, windows
 * occur back-to-back and contain a sample width period during which
 * actual sampling occurs. Can be used to read the total window size.
 */
static ssize_t debug_window_fread(struct file *filp, char __user *ubuf,
				      size_t cnt, loff_t *ppos)
{
	return simple_data_read(filp, ubuf, cnt, ppos, &data.sample_window);
}

/**
 * debug_window_fwrite - Write function for "window" debugfs entry
 * @filp: The active open file structure for the debugfs "file"
 * @ubuf: The user buffer that contains the value to write
 * @cnt: The maximum number of bytes to write to "file"
 * @ppos: The current position in the debugfs "file"
 *
 * This function provides a write implementation for the "window" debufds
 * interface to the hardware latency detetector. The window is the total time
 * in us that will be considered one sample period. Conceptually, windows
 * occur back-to-back and contain a sample width period during which
 * actual sampling occurs. Can be used to write a new total window size. It
 * is enfoced that any value written must be greater than the sample width
 * size, or an error results.
 */
static ssize_t  debug_window_fwrite(struct file *filp,
					const char __user *ubuf,
					size_t cnt,
					loff_t *ppos)
{
	char buf[U64STR_SIZE];
	int csize = min(cnt, sizeof(buf));
	u64 val = 0;
	int err = 0;

	memset(buf, '\0', sizeof(buf));
	if (copy_from_user(buf, ubuf, csize))
		return -EFAULT;

	buf[U64STR_SIZE-1] = '\0';			/* just in case */
	err = kstrtoull(buf, 10, &val);
	if (0 != err)
		return -EINVAL;

	mutex_lock(&data.lock);
	if (data.sample_width < val)
		data.sample_window = val;
	else {
		mutex_unlock(&data.lock);
		return -EINVAL;
	}
	mutex_unlock(&data.lock);

	return csize;
}

/*
 * Function pointers for the "count" debugfs file operations
 */
static const struct file_operations count_fops = {
	.open		= debug_count_fopen,
	.read		= debug_count_fread,
	.write		= debug_count_fwrite,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "enable" debugfs file operations
 */
static const struct file_operations enable_fops = {
	.open		= debug_enable_fopen,
	.read		= debug_enable_fread,
	.write		= debug_enable_fwrite,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "max" debugfs file operations
 */
static const struct file_operations max_fops = {
	.open		= debug_max_fopen,
	.read		= debug_max_fread,
	.write		= debug_max_fwrite,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "sample" debugfs file operations
 */
static const struct file_operations sample_fops = {
	.open		= debug_sample_fopen,
	.read		= debug_sample_fread,
	.release	= debug_sample_release,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "threshold" debugfs file operations
 */
static const struct file_operations threshold_fops = {
	.open		= debug_threshold_fopen,
	.read		= debug_threshold_fread,
	.write		= debug_threshold_fwrite,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "width" debugfs file operations
 */
static const struct file_operations width_fops = {
	.open		= debug_width_fopen,
	.read		= debug_width_fread,
	.write		= debug_width_fwrite,
	.owner		= THIS_MODULE,
};

/*
 * Function pointers for the "window" debugfs file operations
 */
static const struct file_operations window_fops = {
	.open		= debug_window_fopen,
	.read		= debug_window_fread,
	.write		= debug_window_fwrite,
	.owner		= THIS_MODULE,
};

/**
 * init_debugfs - A function to initialize the debugfs interface files
 *
 * This function creates entries in debugfs for "hwlat_detector", including
 * files to read values from the detector, current samples, and the
 * maximum sample that has been captured since the hardware latency
 * dectector was started.
 */
static int init_debugfs(void)
{
	int ret = -ENOMEM;

	debug_dir = debugfs_create_dir(DRVNAME, NULL);
	if (!debug_dir)
		goto err_debug_dir;

	debug_sample = debugfs_create_file("sample", 0444,
					       debug_dir, NULL,
					       &sample_fops);
	if (!debug_sample)
		goto err_sample;

	debug_count = debugfs_create_file("count", 0444,
					      debug_dir, NULL,
					      &count_fops);
	if (!debug_count)
		goto err_count;

	debug_max = debugfs_create_file("max", 0444,
					    debug_dir, NULL,
					    &max_fops);
	if (!debug_max)
		goto err_max;

	debug_sample_window = debugfs_create_file("window", 0644,
						      debug_dir, NULL,
						      &window_fops);
	if (!debug_sample_window)
		goto err_window;

	debug_sample_width = debugfs_create_file("width", 0644,
						     debug_dir, NULL,
						     &width_fops);
	if (!debug_sample_width)
		goto err_width;

	debug_threshold = debugfs_create_file("threshold", 0644,
						  debug_dir, NULL,
						  &threshold_fops);
	if (!debug_threshold)
		goto err_threshold;

	debug_enable = debugfs_create_file("enable", 0644,
					       debug_dir, &enabled,
					       &enable_fops);
	if (!debug_enable)
		goto err_enable;

	else {
		ret = 0;
		goto out;
	}

err_enable:
	debugfs_remove(debug_threshold);
err_threshold:
	debugfs_remove(debug_sample_width);
err_width:
	debugfs_remove(debug_sample_window);
err_window:
	debugfs_remove(debug_max);
err_max:
	debugfs_remove(debug_count);
err_count:
	debugfs_remove(debug_sample);
err_sample:
	debugfs_remove(debug_dir);
err_debug_dir:
out:
	return ret;
}

/**
 * free_debugfs - A function to cleanup the debugfs file interface
 */
static void free_debugfs(void)
{
	/* could also use a debugfs_remove_recursive */
	debugfs_remove(debug_enable);
	debugfs_remove(debug_threshold);
	debugfs_remove(debug_sample_width);
	debugfs_remove(debug_sample_window);
	debugfs_remove(debug_max);
	debugfs_remove(debug_count);
	debugfs_remove(debug_sample);
	debugfs_remove(debug_dir);
}

/**
 * detector_init - Standard module initialization code
 */
static int detector_init(void)
{
	int ret = -ENOMEM;

	pr_info(BANNER "version %s\n", VERSION);

	ret = init_stats();
	if (0 != ret)
		goto out;

	ret = init_debugfs();
	if (0 != ret)
		goto err_stats;

	if (enabled)
		ret = start_kthread();

	goto out;

err_stats:
	ring_buffer_free(ring_buffer);
out:
	return ret;

}

/**
 * detector_exit - Standard module cleanup code
 */
static void detector_exit(void)
{
	int err;

	if (enabled) {
		enabled = 0;
		err = stop_kthread();
		if (err)
			pr_err(BANNER "cannot stop kthread\n");
	}

	free_debugfs();
	ring_buffer_free(ring_buffer);	/* free up the ring buffer */

}

module_init(detector_init);
module_exit(detector_exit);
