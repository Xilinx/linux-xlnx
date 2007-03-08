#include <linux/module.h>
#include <linux/errno.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/mc146818rtc.h>
#include <linux/netdevice.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/delay.h>

#define LCD_On 1
#define LCD_Off 2
#define LCD_Clear 3
#define LCD_Reset 4
#define LCD_Cursor_Left 5
#define LCD_Cursor_Right 6
#define LCD_Disp_Left 7
#define LCD_Disp_Right 8
#define LCD_Get_Cursor 9
#define LCD_Set_Cursor 10
#define LCD_Home 11
#define LCD_Read 12
#define LCD_Write 13
#define LCD_Cursor_Off 14
#define LCD_Cursor_On 15
#define LCD_Get_Cursor_Pos 16
#define LCD_Set_Cursor_Pos 17
#define LCD_Blink_Off 18

#define kLCD_IR na_lcd_16207_0
#define kLCD_DR (na_lcd_16207_0 + 8)

#define LCDWriteData(x) outl(x , kLCD_DR)
#define LCDWriteInst(x) outl(x , kLCD_IR)

#define LCDReadData inl(kLCD_DR)
#define LCDReadInst inl(kLCD_IR)


#define Major 250

static int Device_Open = 0;

static int lcd_16207_ioctl(struct inode *inode,struct file *filp,
			   unsigned int cmd,unsigned long arg);

static int lcd_16207_open(struct inode *inode,struct file *filp)
{
  static int counter = 0;
  if(Device_Open)return -EBUSY;
  Device_Open++;
  printk("You have open the device %d times\n",counter++);
  return 0;
}

static int lcd_16207_release(struct inode *inode,struct file *filp)
{
  Device_Open--;
  printk("You have release the device\n");
  return 0;
}

static int lcd_16207_ioctl(struct inode *inode,struct file *filp,
			   unsigned int cmd,unsigned long arg)
{
  volatile unsigned long display;

  switch (cmd) {
  case LCD_On:
    if (copy_from_user
	(&display, (unsigned long *) arg,
	 sizeof(display)))
      return -EFAULT;

    LCDWriteInst(display);
    break;

  case LCD_Off:
    if (copy_from_user
	(&display, (unsigned long *) arg,
	 sizeof(display)))
      return -EFAULT;

    LCDWriteData(display);
    break;


  default:
    return -EINVAL;
  }

  return 0;
}

static struct file_operations lcd_16207_fops={
  .ioctl = lcd_16207_ioctl,
  .open = lcd_16207_open,
  .release = lcd_16207_release,
};

static int lcd_16207_init(void)
{
  int ret = register_chrdev(Major,"LCD_PIO",&lcd_16207_fops);
  if(ret<0)
    {
      printk("Registering the device failed with %d\n",Major);
      return Major;
    }
  printk("You have init Device %d\n",Major);
  return 0;
}

static void lcd_16207_exit(void)
{
  if(unregister_chrdev(Major,"LCD_PIO"))
    printk("exit failed");
}

module_init(lcd_16207_init);
module_exit(lcd_16207_exit);

MODULE_AUTHOR("Andrew Bose");
MODULE_LICENSE("GPL");
