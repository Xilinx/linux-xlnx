/***************************************************************************
                      									DS1305RTC.c
              DS1305 RTC character driver for uClinux mcfqspi control
              These functions rely on the MCF_QSPI module for the SPI layer.
              Modified from the IPAC DS1305 module for hcs12 communication 
              with the DS1305
   ---------------------------
    begin                				: Thur April 28 2005
    email                				: ngustavson@emacinc.com
    (C) Copyright 2005		: EMAC.Inc - www.emacinc.com   
   ---------------------------
                                                                          
    This program is free software; you can redistribute it and/or modify  
    it under the terms of the GNU General Public License as published by  
    the Free Software Foundation; either version 2 of the License, or     
    (at your option) any later version.                                   
                                                                          
 ***************************************************************************/
#include <asm/DS1305RTC.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <asm/uaccess.h> 
#include <linux/miscdevice.h>

 MODULE_LICENSE("GPL");

/***************************************************************************
 * External functions
 ***************************************************************************/
extern qspi_dev *qspi_create_device(void);
extern int qspi_destroy_device(qspi_dev *device);
extern ssize_t qspi_internal_read(qspi_dev *dev,char *buffer, size_t length,
		loff_t *off,int qcr_cs);
extern	ssize_t qspi_internal_write(qspi_dev *dev,const char *buffer, size_t length,
     	loff_t *off,int qcr_cs);
extern u16 qspi_BAUD(int desired);
extern void qspi_mutex_down(void);
extern void qspi_mutex_up(void);


/*!Write to a RTC register
@param device qspi device the RTC1305 belongs too
@param reg the register to write to(from the enumerated registers above)
@param the data to write to it
@return SUCCESS
*/
static int RTC_Write_Register(rtc_qspi_device *dev,int reg, u8 data)
{
u8 output[2];
output[0] = reg + WRITE_OFFSET;
output[1] = data;	
RTC_CE_ON();
qspi_internal_write(dev->qspi, output, sizeof(output),0,0);
RTC_CE_OFF();	
return SUCCESS;
}

/*!Read from a RTC register
@param device qspi device the RTC1305 belongs too
@param reg he register to write to(from the enumerated registers above)
@return the byte that was read
*/
static u8 RTC_Read_Register(rtc_qspi_device *dev,int reg)
{
u8 buffer[2];
buffer[0] = reg;
buffer[1] = 0;
dev->qspi->read_data.length = sizeof(buffer); 
dev->qspi->read_data.buf = buffer;

RTC_CE_ON();
qspi_internal_read(dev->qspi,buffer, sizeof(buffer),0,0);
RTC_CE_OFF();

dev->qspi->read_data.length=0;
dev->qspi->read_data.buf =NULL;

return buffer[1];
}

static rtc_qspi_device *RTC_create_device(void)
{
rtc_qspi_device *dev;	
qspi_dev *spi =  qspi_create_device();
 
if(spi==NULL)
	return(NULL); 
        
if ((dev = kmalloc(sizeof(rtc_qspi_device), GFP_KERNEL)) == NULL) {  
	return(NULL);
}
       		
dev->qspi = spi;

spi->poll_mod = 1;
spi->baud = qspi_BAUD(2000000);    /* intial baud rate 2M */
spi->cpha      =  1;    /* SPI clock phase */
                
RTC_CE_SETUP();

RTC_Write_Register(dev,CONTROL,INTCN);
return dev;
}


static rtc_qspi_device  *RTC_destroy_device(rtc_qspi_device *dev){
if(dev==NULL)
	return NULL;
qspi_destroy_device(dev->qspi);
kfree(dev);	
return NULL;
}
/*!Get the current time from the RTC
This function reads the current time stored in the RTC's internal registers.
Alternatively each individual register can be returned using the RTC_ macros below
@param time a rtc_time structure that GetTime fills with data
@return  SUCCESS
*/
static int RTC_GetTime(rtc_qspi_device *dev,struct rtc_time *time)
{
time->tm_sec =   (RTC_SECONDS(dev));
time->tm_min =    (RTC_MINUTES(dev));
time->tm_hour =       (RTC_HOURS(dev));
time->tm_wday =         (RTC_DAY(dev));
time->tm_mday =        (RTC_DATE(dev));
time->tm_mon =      (RTC_MONTH(dev));
time->tm_year =        (RTC_YEAR(dev));
time->tm_sec = RTC2TIME(time->tm_sec);
time->tm_min = RTC2TIME(time->tm_min);
time->tm_hour = RTC2TIME(time->tm_hour);
time->tm_wday = RTC2TIME(time->tm_wday);
time->tm_mday = RTC2TIME(time->tm_mday);
time->tm_mon = RTC2TIME(time->tm_mon);
time->tm_year = RTC2TIME(time->tm_year);
return SUCCESS;
}

/*!Set the current time of the RTC
This function sets the current time stored in the RTC's internal registers.
Alternatively each individual register can be written using the
RTC_Write_Register function.
@param time a rtc_time structure SetTime writes to the clock
@return  SUCCESS
@see RTC_Write_Register function.
*/
static int RTC_SetTime(rtc_qspi_device *dev,struct rtc_time *time)
{
RTC_SET_SECONDS(dev,time->tm_sec);
RTC_SET_MINUTES(dev,time->tm_min);
RTC_SET_HOURS(dev,time->tm_hour);
RTC_SET_DAY(dev,time->tm_wday);
RTC_SET_DATE(dev,time->tm_mday);
RTC_SET_MONTH(dev,time->tm_mon);
RTC_SET_YEAR(dev,time->tm_year);
return SUCCESS;
}


static int ds1305_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
		     unsigned long arg)
{
	rtc_qspi_device  *dev = filp->private_data;
	struct rtc_time wtime; 
	
	switch(cmd){
		
		case RTC_RD_TIME:	/* Read the time/date from RTC	*/
	{
		memset(&wtime, 0, sizeof(struct rtc_time));
		RTC_GetTime(dev,&wtime);
		if(copy_to_user((void __user *)arg, &wtime, sizeof wtime))
			return -EFAULT;
		return 0;
	}
	case RTC_SET_TIME:	/* Set the RTC */
	{
		if (copy_from_user(&wtime, (struct rtc_time __user *)arg,
				   sizeof(struct rtc_time)))
			return -EFAULT;
		RTC_SetTime(dev,&wtime);
		return 0;
	}     	
		     	
	default:
			return -EINVAL;
	}     	
		     	
return 0;		     	
}


static int ds1305_open (struct inode *inode, struct file *filp){
rtc_qspi_device  *dev = RTC_create_device();
if(dev==NULL)
	return -ENOMEM;
filp->private_data = dev;	
return 0;
}

static int ds1305_release(struct inode *inode, struct file *filp){
RTC_destroy_device(filp->private_data);
return 0;	
}

struct file_operations ds1305_fops={
  ioctl:   		ds1305_ioctl,
  open:    	ds1305_open,
  release: 	ds1305_release,
};

static struct miscdevice ds1305rtc_dev=
{
	RTC_MINOR,
	DSNAME ,
	&ds1305_fops
};


static int __init ds1305_init(void){

//SET_MODULE_OWNER(&ds1305_fops);

  /* the drivers (main) function*/

printk(" "DSNAME " driver version " DS_DRIVER_V " (c) " __DATE__  "\n");
	printk(" N.Z. Gustavson (ngustavson@emacinc.com), EMAC.inc\n");

//DSMajor = register_chrdev(DSMAJORNUM, DSNAME, &ds1305_fops);
if (misc_register(&ds1305rtc_dev)) {
printk(DSNAME" driver failed to register"); 
return -ENODEV;
}

printk(DSNAME" Driver Registered\n");	
return 0;
}

static void __exit ds1305_exit (void){
//unregister_chrdev(DSMajor,DSNAME);
misc_deregister(&ds1305rtc_dev);
printk(DSNAME" driver unloaded\n");
}

module_init(ds1305_init);
module_exit(ds1305_exit);
MODULE_ALIAS_MISCDEV(RTC_MINOR);

