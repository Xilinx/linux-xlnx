/*
 * fslfifo.c -- FSL FIFO driver for Microblaze
 *
 * Copyright (C) 2015 Joachim Naulet <joachim.naulet.ext@zodiacaerospace.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *       
 */

/*
 * Simple driver to provide Linux-style FIFO interface to custom hardware
 * peripherals connected to Microblaze FSL ports.  See Microblaze user manual
 * for details of FSL architecture.
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/io.h>
#include <asm/fsl.h>
#include <asm/uaccess.h>
#include <asm/fslfifo_ioctl.h>

#include <linux/miscdevice.h>
#include <linux/major.h>

/* FSLFIFO main object */

typedef enum {
  RX_EMPTY = 0x01,
  TX_FULL  = 0x02
} fslfifo_status_t;

struct fsl_fifo {
  
#define FSL_FIFO_BUFSIZE    64
#define FSL_FIFO_DFLT_WIDTH sizeof(u32)
  
  int id;     /* Which FSL port */
  int exists; /* Is there something on the end? */
  int busy;   /* Is it already open */
  int irq;    /* Is it interrupt-driven ? */
  
  int rwidth; /* Data READ  width in bytes (1,2,4) */
  int wwidth; /* Data WRITE width in bytes (1,2,4) */

  /* FIFOs & waitqueues */
  struct kfifo to_user, from_user;
  wait_queue_head_t to_user_wq, from_user_wq;

  /* state machine */
  fslfifo_status_t status;
  
  /* Stats */
  u32 tx_ok, tx_fail;
  u32 rx_ok, rx_fail, rx_interrupt, rx_dump;
  
  /* miscdev */
  char name[5]; /* TODO : use constant */
};

/* Some globals... */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define FSLFIFO_MAX 8 /* Maximum number of FSL FIFOs */
static struct fsl_fifo fslfifo_table[FSLFIFO_MAX];
static spinlock_t fslfifo_lock;

#define fslfifo_lock(fifo, flags)				\
  if((fifo)->irq != -1) spin_lock_irqsave(&fslfifo_lock, flags)
#define fslfifo_unlock(fifo, flags)					\
  if((fifo)->irq != -1) spin_unlock_irqrestore(&fslfifo_lock, flags);

/* Basic functions */

static ssize_t fslfifo_read_value(struct fsl_fifo *fifo, u32 *retval) {

  ssize_t ret = -1;
  u32 value, status = 0;
  
  fsl_nget(fifo->id, value, status);
  /* Get data */
  if(fsl_nodata(status)){
    fifo->status |= RX_EMPTY;
    /* Not used in FPGA
    if(fsl_error(status))
      fifo->rx_fail++;
    */
    /* Empty and/or error */
    goto out;
  }
  
  /* Status */
  fifo->rx_ok++;
  *retval = value;
  ret = fifo->rwidth;
  /* Signal if required */
  fifo->status &= ~RX_EMPTY;
  
 out:
  /* Ret */
  return ret;
}

static ssize_t fslfifo_poll_read(struct fsl_fifo *fifo) {
  
  u32 value;
  ssize_t ret;
  unsigned long flags;

  /* Atomic */
  fslfifo_lock(fifo, flags);
  
  if(!(ret = kfifo_len(&fifo->to_user)) &&
     (ret = fslfifo_read_value(fifo, &value)) != -1)
    /* Push value somewhere */
    kfifo_in(&fifo->to_user, &value, ret);

  /* !Atomic */
  fslfifo_unlock(fifo, flags);
  return ret;
}

static ssize_t fslfifo_read_data(struct fsl_fifo *fifo,
				 u32 *retval, size_t size) {

  /* 3 cases */
  u32 value;
  ssize_t len, ret;
  unsigned long flags;

  /* Atomic */
  fslfifo_lock(fifo, flags);
  
  /* 1) There's pending data in fifo */
  if((len = kfifo_len(&fifo->to_user))){
    ret = kfifo_out(&fifo->to_user, retval, MIN(len, size));
    goto out;
  }
  
  /* 2) There's no pending data in fifo ... */
  if((ret = fslfifo_read_value(fifo, &value)) < 0)
    goto out;
  
  /* but we want unaligned / incomplete set of data */
  if(size < ret){
    kfifo_in(&fifo->to_user, &value, ret);
    ret = kfifo_out(&fifo->to_user, retval, size);
    goto out;
  }
  
  /* 3) Full data burst */
  *retval = value;
  
 out:
  /* !Atomic */
  fslfifo_unlock(fifo, flags);
  return ret;
}

static ssize_t fslfifo_write_value(struct fsl_fifo *fifo, u32 value) {

  u32 status = 0;
  ssize_t ret = -1;
  
  fsl_nput(fifo->id, value, status);
  /* Get data */
  if(fsl_nodata(status)){
    /* Not used in FPGA 
    if(fsl_error(status))
      fifo->tx_fail++;
    */
    /* Full and/or error */
    fifo->status |= TX_FULL;
    goto out;
  }
  
  /* Status */
  fifo->tx_ok++;
  ret = fifo->wwidth;
  fifo->status &= ~TX_FULL;
  
 out:
  /* ret */
  return ret;
}

#if 0
/*
 * TODO : Add this to avoid FSL TX overloading
 */
static ssize_t fslfifo_wpoll_block(struct fsl_fifo *fifo, u32 value) {
  
  ssize_t ret = fslfifo_write_value(fifo, value);
  if(ret < 0)
    /* Push value somewhere */
    kfifo_in(&fifo->from_user, &value, fifo->wwidth);
  
  return ret;
}

static ssize_t fslfifo_write_data(struct fsl_fifo *fifo,
				  u32 value, size_t size) {

  /* 3 cases */
  /* 1) There's pending data to send */
  while(!kfifo_is_empty(&fifo->from_user)){
    kfifo_peek(&fifo->from_user, &value, size);
    if((ret = fslfifo_write_value(fifo, value)) < 0)
      return ret;
    
    /* Confirm only if we successfully sent it */
    fifo_out(&fifo->from_user, &value, size);
  }
  
  /* 2) There's no pending data in fifo but we want
   * unaligned / incomplete set of data */
  if(size < fifo->wwidth)
    return kfifo_in(&fifo->from_user, &value, size);
  
  /* 3) Full data burst */
  return fslfifo_write_value(fifo, value);
}

#endif

/* IRQ management */

static irqreturn_t fsl_fifo_interrupt(int irq, void *private) {
  
  struct fsl_fifo *fifo = (struct fsl_fifo*)private;
  
  fifo->rx_interrupt++;
  fifo->status &= ~RX_EMPTY;
  wake_up_interruptible(&fifo->to_user_wq);
  
  return IRQ_HANDLED;
}

static int fsl_fifo_init_irq(struct fsl_fifo *f, int irq) {

  int res = 0;
  
  /* Allocate DATA irq */
  if((res = irq_create_mapping(NULL, irq)) < 0)
    pr_warn("ERROR when mapping hw irq FSL got DATA\n");
  
  if(request_irq(res, fsl_fifo_interrupt, IRQF_DISABLED, "fslfifo", f) < 0){
    pr_warn("Unable to install fslfifo interrupt handler!\n");
    return -1; /* force polling */
  }

  return res;
}

/* Basic init */

int fsl_fifo_init(struct fsl_fifo *f, int id, int irq) {

  f->id = id;
  f->busy = 0;
  f->irq = -1;
  f->rwidth = FSL_FIFO_DFLT_WIDTH; /* Assume word-wide FSL channel */
  f->wwidth = FSL_FIFO_DFLT_WIDTH; /* Assume word-wide FSL channel */

  /* FIXME : ? */
  if(kfifo_alloc(&f->to_user, FSL_FIFO_BUFSIZE, GFP_KERNEL)){
    pr_err("fslfifo: failed to allocate to_user kfifo");
    goto err0;
  }
  
  if(kfifo_alloc(&f->from_user, FSL_FIFO_BUFSIZE, GFP_KERNEL)){
    pr_err("fslfifo: failed to allocate from_user kfifo");
    goto err1;
  }

  /* Blocking IO */
  init_waitqueue_head(&f->to_user_wq);
  init_waitqueue_head(&f->from_user_wq);

  /* IRQ */
  if(irq != -1)
    f->irq = fsl_fifo_init_irq(f, irq);

  /* State machine */
  f->status = 0;
  
  /* Stats */
  f->tx_ok = 0;
  f->tx_fail = 0;
  f->rx_ok = 0;
  f->rx_fail = 0;
  f->rx_interrupt = 0;
  f->rx_dump = 0;
  
  /* miscdev */
  snprintf(f->name, sizeof f->name, "fsl%d", id);

  f->exists = 1;
  return 0;

 err1:
  kfifo_free(&f->to_user);
 err0:
  return -1;
}

static void fsl_fifo_free_irq(struct fsl_fifo *f) {

  free_irq(f->irq, f);
}

/* Useless but i like my objects clean */
void fsl_fifo_free(struct fsl_fifo *f) {

  f->exists = 0;

  /* IRQ */
  if(f->irq != -1)
    fsl_fifo_free_irq(f);
  
  kfifo_free(&f->to_user);
  kfifo_free(&f->from_user);
}

void fsl_fifo_flush(struct fsl_fifo *f) {

  u32 value, status;
  unsigned long flags;
  
  /* Atomic */
  fslfifo_lock(f, flags);

  kfifo_reset(&f->from_user);
  kfifo_reset(&f->to_user);

  do{ 
    fsl_nget(f->id, value, status);
    f->rx_dump++;
  } while(!fsl_nodata(status));
  /* FIXME : what if fsl_error(status) ? */
  
  /* Set new status */
  f->status |= RX_EMPTY;
  /* !Atomic */
  fslfifo_unlock(f, flags);
}

/* ** Driver ** */

/* Basic macros */

/* FSL fifo data channels are misc devices (major-10)
   minor channel is base+fsl_num (eg 192--199) */
#define FSLFIFO_MINOR_BASE 192
#define FSLFIFO_MINOR(id) (FSLFIFO_MINOR_BASE | ((id) & 0x7))
/* Return a dev->device code for a given fsl channel*/
#define FSLFIFO(id) MKDEV(MISC_MAJOR, FSLFIFO_MINOR(id))
/* Compute an fsl fifo ID from a device number */
#define FSLFIFO_ID(dev) (MINOR(dev) & (~FSLFIFO_MINOR_BASE))

/* Statically allocate descriptors - this is klunky but too bad */
static struct miscdevice fslfifo_miscdev[FSLFIFO_MAX];
static struct task_struct *fslfifo_task;

/* Kernel config parameters */
static struct { int id, irq; } fslfifo_config[] = {
#ifdef CONFIG_MICROBLAZE_FSLFIFO0
  { .id = 0, .irq = CONFIG_MICROBLAZE_FSLFIFO0_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO1
  { .id = 1, .irq = CONFIG_MICROBLAZE_FSLFIFO1_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO2
  { .id = 2, .irq = CONFIG_MICROBLAZE_FSLFIFO2_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO3
  { .id = 3, .irq = CONFIG_MICROBLAZE_FSLFIFO3_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO4
  { .id = 4, .irq = CONFIG_MICROBLAZE_FSLFIFO4_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO5
  { .id = 5, .irq = CONFIG_MICROBLAZE_FSLFIFO5_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO6
  { .id = 6, .irq = CONFIG_MICROBLAZE_FSLFIFO6_IRQ },
#endif
#ifdef CONFIG_MICROBLAZE_FSLFIFO7
  { .id = 7, .irq = CONFIG_MICROBLAZE_FSLFIFO7_IRQ },
#endif
  /* The end */
  {. id = -1, .irq = -1 }
};

/* Driver essentials */
/* Fops */
static int fslfifo_open(struct inode *inode, struct file *f) {
  
  int id;
  struct fsl_fifo *fifo;
  
  if((id = FSLFIFO_ID(inode->i_rdev)) >= FSLFIFO_MAX)
    return -ENODEV;
  
  fifo = &fslfifo_table[id];
  if(!fifo->exists)
    return -ENODEV;

  if(fifo->busy){
    pr_warn("fslfifo: fsl%d is already open\n", fifo->id);
    return -EBUSY;
  }
   
  /* Set the file's private data to be fslfifo's descriptor */
  f->private_data = (void*)fifo;
  fifo->busy++;
  /* TODO : flush fifo ? */
  
  /* pr_debug("fslfifo_open succeeded, id : %d\n", fifo->id); */
  try_module_get(THIS_MODULE);
  
  return 0;
}

static int fslfifo_release(struct inode *inode, struct file *f) {
  
  struct fsl_fifo *fifo = (struct fsl_fifo*)f->private_data;
  
  fifo->busy--;
  module_put(THIS_MODULE);
  
  /* Flush fifo on release ? */
  
  return 0;
}

/* Select management */
static unsigned int fslfifo_poll(struct file *f, poll_table *wait) {
  
  unsigned int mask = 0;
  struct fsl_fifo *fifo = (struct fsl_fifo*)f->private_data;

  if(!fifo->exists)
    return -ENODEV;

  /* pr("fslfifo: %d fslfifo_poll\n", fifo->id); */

  /* Force state machine checkup */
  fslfifo_poll_read(fifo);
  
  /* Simpler poll version */
  poll_wait(f, &fifo->to_user_wq, wait);
  poll_wait(f, &fifo->from_user_wq, wait);
  
  if(!(fifo->status & RX_EMPTY))
    mask |= POLLIN | POLLRDNORM;
  
  if(!(fifo->status & TX_FULL))
    mask |= POLLOUT | POLLWRNORM;
  
  /* pr_debug("fslfifo: %d fslfifo_poll [0x%04x]\n", fifo->id, mask); */
  return mask;
}

static ssize_t fslfifo_read(struct file *f, char *buf, size_t count,
			    loff_t *pos) {
  
  u32 value;
  int n = 0;
  ssize_t ret, len;
  struct fsl_fifo *fifo = (struct fsl_fifo*)f->private_data;
  
  if(!fifo->exists)
    return -ENODEV;

  /* Start by polling fifo in order to get correct state machine */
  fslfifo_poll_read(fifo);
  
  /* Manage nonblocking flag */
  if((f->f_flags & O_NONBLOCK) &&
     (fifo->status & RX_EMPTY))
    return -EAGAIN;
  
  do {
    /* blocking IO */
    ret = wait_event_interruptible(fifo->to_user_wq,
				   !(fifo->status & RX_EMPTY));
    
    /* If interrupted */
    if(ret)
      return ret;

    while(n < count){
      size_t size = MIN(count, fifo->rwidth);
      if((len = fslfifo_read_data(fifo, &value, size)) < 0)
	break;
      /* to userspace */
      copy_to_user(buf + n, &value, len);
      /* Inc */
      n += len;
    }
    /* We go back to wait_event if there's data left and blocking mode */
  } while((n < count) &&
	  !(f->f_flags & O_NONBLOCK));
  
  /* Return bytes read */
  return n;
}

static ssize_t fslfifo_write(struct file *f, const char *buf,
			     size_t count, loff_t *pos) {

  int i;
  u32 value;
  ssize_t ret;
  struct fsl_fifo *fifo = (struct fsl_fifo*)f->private_data;
  
  if(!fifo->exists)
    return -ENODEV;
  
  /* TODO : make blocking write ! */
  
  /* Manage nonblocking flag */
  if((f->f_flags & O_NONBLOCK) &&
     (fifo->status & TX_FULL))
    return -EAGAIN;
  
  /* do { */
  /* blocking io */
  /*
    ret = wait_event_interruptible(fifo->from_user_wq,
    !(fifo->status & TX_FULL));
  */
  
  /* Interrupted */
  /* if(ret)
     goto out; */
  
  for(i = 0; i < count; i += fifo->wwidth){
    copy_from_user(&value, buf + i, fifo->wwidth);
    if((ret = fslfifo_write_value(fifo, value)) < 0)
      break;
  }
  // } while((i < count)/* && (fifo->status & TX_FULL)*/);
  
  return i;
}

/* TODO : rewrite */
static long fslfifo_ioctl(struct file *f, unsigned int cmd,
			  unsigned long arg) {
  
  u32 status = 0;
  struct fsl_fifo *fifo = (struct fsl_fifo*)f->private_data;
  
  if(!fifo->exists){
    pr_warn("fsl%d doesn't exist\n", fifo->id);
    return -ENODEV;
  }

  if(fifo->id >= FSLFIFO_MAX)
    return -ENODEV;
  
  switch(cmd){
  case FSLFIFO_IOCRESET:		/* Reset */
    /* pr_debug("fslfifo: /dev/fsl%d FSLFIFO_IOCRESET\n", fifo->id); */
    fsl_fifo_flush(fifo);
    return 0;
    
  case FSLFIFO_IOCTCONTROL:	/* Write control value */
    /* Note this jumps the queue, and is blatted directly to the FSL
       port.  It does not get queued in the main SW buffer */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCTCONTROL\n", fifo->id); */
    /* FIXME : put this somewhere else (in kthread ?)  */
    fsl_ncput(fifo->id, arg, status);
    if(fsl_error(status))
      return -EIO;
    else if(fsl_nodata(status))
      return -EBUSY;
    
    return 0;
    
  case FSLFIFO_IOCQCONTROL:	/* Read control value */
    /* This bypasses the normal software buffers.  It is very unlikely
       to work unless those buffers are empty, and the tasklets are
       idling */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCQCONTROL\n", fifo->id); */
    /* FIXME : put this somewhere else (in kthread ?)  */
    fsl_ncget(fifo->id, arg, status);
    if(fsl_error(status)) /* Non-control value from FSL */
      return -EIO;
    else if(fsl_nodata(status)) /* Nothing from FSL */
      return -EBUSY;
    
    return arg;
    
  case FSLFIFO_IOCTRWIDTH: /* set data READ width */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCTRWIDTH (%ld)\n",
       fifo->id, arg); */
    if(arg != fifo->rwidth){
      if(arg == sizeof(u8) ||
	 arg == sizeof(u16) ||
	 arg == sizeof(u32)){
	/* TODO : find a better way to check these values */
	//fsl_fifo_flush(fifo); /* Warning : can result in data loss */
	fifo->rwidth = arg;
      }else
	return -EINVAL;
    }
    
    return 0;

  case FSLFIFO_IOCTWWIDTH: /* set data WRITE width */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCTWWIDTH (%ld)\n",
       fifo->id, arg); */
    if(arg != fifo->wwidth){
      if(arg == sizeof(u8) ||
	 arg == sizeof(u16) ||
	 arg == sizeof(u32)){
	//fsl_fifo_flush(fifo); /* Warning : can result in data loss */
	fifo->wwidth = arg;
      }else
	return -EINVAL;
    }

    return 0;
    
  case FSLFIFO_IOCQRWIDTH: /* get data READ  width */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCQRWIDTH\n", fifo->id); */
    return fifo->rwidth;
    
  case FSLFIFO_IOCQWWIDTH: /* get data WRITE width */
    /* pr_debug("fslfifo: /dev/fsl%d, FSLFIFO_IOCQWWIDTH\n", fifo->id); */
    return fifo->wwidth;
    
  default:
    return -EINVAL;
  }
  
  return 0;
}

static struct file_operations fslfifo_fops = {
  .owner          = THIS_MODULE,
  .read           = fslfifo_read,
  .write          = fslfifo_write,
  .unlocked_ioctl = fslfifo_ioctl,
  .open           = fslfifo_open,
  .release        = fslfifo_release,
  .poll           = fslfifo_poll
};

/* kthread */
#define FSLFIFO_LOOP_DELAY_MS 100 /* ms */

static int fslfifo_kthreadfn(void *data) {

  int i;
  
  /* Init thread */
  while(!kthread_should_stop()){
    /* Parse all devices */
    for(i = FSLFIFO_MAX; i--;){
      /* Use data ? */
      struct fsl_fifo *fifo = &fslfifo_table[i];
      if(!fifo->exists) /* Check if busy ? */
	continue;
      
      /* Polling mode, anyway (irq or not) */
      fslfifo_poll_read(fifo);
      /* TODO : poll tx too */
      if(!(fifo->status & RX_EMPTY))
	/* As it's polling, we gotta signal this */
	wake_up_interruptible(&fifo->to_user_wq);
      
      /* Don't forget to sleep */
      msleep_interruptible(FSLFIFO_LOOP_DELAY_MS);
    }
  }
  
  return 0;
}

/* Procfs */
static struct proc_dir_entry *fslfifo_proc_dir;
static struct proc_dir_entry *fslfifo_proc_stat;
static struct proc_dir_entry *fslfifo_proc_status;

static int fslfifo_proc_show(struct seq_file *m, void *v)
{
  int i;
  
  if((int)m->private){
    for(i = 0; i < FSLFIFO_MAX; i++){
      /* Human readable */
      struct fsl_fifo *fifo = &fslfifo_table[i];
      if(!fifo->exists)
	continue;
      
      seq_printf(m, "fsl%d ", fifo->id);
      seq_printf(m, "rx_ok %u ", fifo->rx_ok);
      seq_printf(m, "rx_fail %u ", fifo->rx_fail);
      seq_printf(m, "rx_total %u ", fifo->rx_ok + fifo->rx_fail);
      seq_printf(m, "tx_ok %u ", fifo->tx_ok);
      seq_printf(m, "tx_fail %u ", fifo->tx_fail);
      seq_printf(m, "tx_total %u ", fifo->tx_ok + fifo->tx_fail);
      seq_printf(m, "interrupt %u ", fifo->rx_interrupt);
      seq_printf(m, "dump %u\n", fifo->rx_dump);
    }
    
  }else{
    /* Parse all devices */
    for(i = 0; i < FSLFIFO_MAX; i++){
      struct fsl_fifo *fifo = &fslfifo_table[i];
      if(!fifo->exists)
	continue;
      
      seq_printf(m, "%d %u %u %u %u %u %u", fifo->id,
		 fifo->rx_fail, fifo->rx_ok,
		 fifo->tx_fail, fifo->tx_ok,
		 fifo->rx_interrupt,
		 fifo->rx_dump);
    }
    
    seq_printf(m, "\n");
  }
  
  return 0;
}

static int fslfifo_proc_open(struct inode *inode, struct file *file)
{
  return single_open(file, fslfifo_proc_show, PDE_DATA(inode));
}

static const struct file_operations fslfifo_proc_fops = {
  .open = fslfifo_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};

/* Init & free */

static int __init fslfifo_init_devices(void) {
  
  int i, res;
  
  /* Credits */
  pr_info("FSL FIFO Microblaze driver for linux 3.19\n");
  
  for(i = 0; fslfifo_config[i].id != -1; i++){
    /* Init */
    int id = fslfifo_config[i].id;
    int irq = fslfifo_config[i].irq;
    
    struct fsl_fifo *fifo = &fslfifo_table[id];
    if(!fsl_fifo_init(fifo, id, irq)){
      /* Set misc dev */
      fslfifo_miscdev[id].minor = FSLFIFO_MINOR(id);
      fslfifo_miscdev[id].name = fifo->name;
      fslfifo_miscdev[id].fops = &fslfifo_fops;
      
      /* Register if possible */
      if((res = misc_register(&fslfifo_miscdev[id]))){
	pr_err("fslfifo: Error registering fifo[%i] (%d)\n", id, res);
	continue;
      }
      
      /* Some output */
      pr_info("fslfifo: fifo #%i initialized\n", i);
    }
  }

  /* Procfs entries */
  /* TODO : Check */
  if((fslfifo_proc_dir = proc_mkdir("driver/fsl", NULL))){
    /* Stat file */
    fslfifo_proc_stat = proc_create_data("stat", 0666,
					 fslfifo_proc_dir,
					 &fslfifo_proc_fops,
					 (void*)0x0); /* Ugly */
    /* Human readable output */
    fslfifo_proc_status = proc_create_data("status", 0666,
					   fslfifo_proc_dir,
					   &fslfifo_proc_fops,
					   (void*)0x1); /* Ugly */
  }
  
  /* Run kthread */
  fslfifo_task = kthread_run(fslfifo_kthreadfn, fslfifo_table, "kfsld");
  if(fslfifo_task == ERR_PTR(-ENOMEM)) {
    pr_err("fslfifo: fslfifo_task thread creation failed");
    fslfifo_task = NULL;
  }

  /* Over */
  return 0;
}

/* Init / Exit */

static void __exit fslfifo_cleanup_devices(void) {

  int i;

  /* Kill kthread */
  kthread_stop(fslfifo_task);

  /* Remove procfs */
  proc_remove(fslfifo_proc_status);
  proc_remove(fslfifo_proc_stat);
  proc_remove(fslfifo_proc_dir);
  
  for(i = 0; fslfifo_config[i].id != -1; i++){
    int id = fslfifo_config[i].id;
    struct fsl_fifo *fifo = &fslfifo_table[id];
    misc_deregister(&fslfifo_miscdev[id]);
    fsl_fifo_free(fifo);
  }
}

static int __init fslfifo_init_dev(void){
  
  int rtn;

  /* Globals */
  spin_lock_init(&fslfifo_lock);
  memset(&fslfifo_table, 0, sizeof(fslfifo_table));
  
  if((rtn = fslfifo_init_devices())){
    pr_err("fslfifo: error registering devices (%d)\n", rtn);
    return -ENODEV;
  }

  /* ok */
  return 0;
}

static void __exit fslfifo_cleanup_dev(void)
{
  /* Just clean */
  fslfifo_cleanup_devices();
}

module_init(fslfifo_init_dev);
module_exit(fslfifo_cleanup_dev);
