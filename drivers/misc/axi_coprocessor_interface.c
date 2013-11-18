#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/misc/axi_coprocessor_interface.h>

#define DEVICE_NAME "axi_coprocessor_interface"

struct interface_priv{
	void *mmio;
};

static struct interface_priv interface_private_data;

static u32 interface_get_register(struct interface_priv *dev, u32 register_offset)
{
	volatile u32 *reg;
	reg = (u32*)( (u32)(dev->mmio) + register_offset);
	return *reg;
}

static void interface_set_register(struct interface_priv *dev, u32 register_offset, u32 register_value)
{
	volatile u32 *reg;
	reg = (u32*)( (u32)(dev->mmio) + register_offset);
	*reg = register_value;
}

static u32 interface_get_data(struct interface_priv *dev)
{
	interface_set_register(dev, INTERFACE_CONTROL_REGISTER, INTERFACE_LATCH_FIFO_OUT_DATA_PIN);
	interface_set_register(dev, INTERFACE_CONTROL_REGISTER, INTERFACE_LATCH_FIFO_OUT_DATA_PIN);
	return interface_get_register(dev, INTERFACE_FIFO_OUT_REGISTER);
}

/* char driver api */

static int interface_open(struct inode *inode, struct file *file)
{
	file->private_data = &interface_private_data;
	return 0;
}

static int interface_close(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static ssize_t interface_read(struct file *file, const char __user *buffer, size_t length, loff_t *offset)
{
	u32 reg;
	u32 *buf = (u32*)kmalloc(length + (4 - (length % 4)), GFP_KERNEL);
	u32 copied_data = 0;
	struct interface_priv *priv;
	
	if(!buf)
		return -ENOMEM;
	priv = (struct interface_priv*)file->private_data;
	
	reg = interface_get_register(priv, INTERFACE_STATUS_REGISTER);
	while( (!(reg & INTERFACE_FIFO_OUT_EMPTY_MASK)) && (copied_data < length) )
	{
		buf[copied_data/4] = interface_get_data(priv);
		copied_data += 4;
		reg = interface_get_register(priv, INTERFACE_STATUS_REGISTER);
	}

	if( copy_to_user(buffer, buf, copied_data) != 0 )
		copied_data =  -EFAULT;	

	kfree(buf);
	return copied_data;
}

static ssize_t interface_write(struct file *file, const char __user *buffer, size_t length, loff_t *offset)
{
	u32 reg;
	u32 *buf = (u32*)kmalloc(length + (4 - (length % 4)), GFP_KERNEL); //data must be aligned to 4
	u32 copied_data = 0;
	struct interface_priv *priv;

	if(!buf)
		return -ENOMEM;

	priv = (struct interface_priv*)file->private_data;
	if( copy_from_user(buf, buffer, length) != 0)
		length = -EFAULT;
		
	reg = interface_get_register(priv, INTERFACE_STATUS_REGISTER);
	while( (!(reg & INTERFACE_FIFO_IN_FULL_MASK)) && (copied_data < length))
	{
		interface_set_register(priv, INTERFACE_FIFO_IN_REGISTER, buf[copied_data/4]);
		reg = interface_get_register(priv, INTERFACE_STATUS_REGISTER);
		copied_data += 4;
	}
	kfree(buf);
	return copied_data;
}

static int interface_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
	struct interface_ioctl_data ioctl_arg;
	struct interface_priv *priv;

	priv = (struct interface_priv*) file->private_data;
	if ( copy_from_user(&ioctl_arg, (struct interface_ioctl_data*) ioctl_param, sizeof(struct interface_ioctl_data)) )
	{
		return -EACCES;
	}
	switch(ioctl_num)
	{
		case INTERFACE_SET_REGISTER:
			interface_set_register(priv, (u32)ioctl_arg.register_offset, (u32)ioctl_arg.register_value);
			break;
		case INTERFACE_GET_REGISTER:
			if(ioctl_arg.register_offset == INTERFACE_FIFO_OUT_REGISTER)
			{
				ioctl_arg.register_value = interface_get_data(priv);
			}
			else ioctl_arg.register_value = interface_get_register(priv, ioctl_arg.register_offset);

			if( copy_to_user((struct interface_ioctl_data*)ioctl_param, &ioctl_arg,sizeof(struct interface_ioctl_data)) )
			{
				return -EACCES;
			}
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

struct file_operations fops = {
				.read = interface_read,
				.write = interface_write,
				.unlocked_ioctl = interface_ioctl,
				.open = interface_open,
				.release = interface_close,};

/* platform driver api */

static int interface_probe(struct platform_driver *pdev)
{
	int ret = 0;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
	{
		printk(KERN_ERR"Failed to get device resource\n");
		return -ENODEV;
	}

	interface_private_data.mmio = ioremap(res->start, INTERFACE_REGISTER_SPACE);
	
	ret = register_chrdev(INTERFACE_MAJOR_NUMBER, DEVICE_NAME, &fops);
	if (ret < 0)
	{
		printk(KERN_ERR"Char device registration failed\n");
		return -ENODEV;
	}
	return 0;
}

static int interface_remove(struct platform_driver *pdev)
{
	unregister_chrdev(INTERFACE_MAJOR_NUMBER, DEVICE_NAME);
	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id interface_of_match[] = {
         { .compatible = "kik,axi_coprocessor_interface", },
         {}
};
MODULE_DEVICE_TABLE(of, interface_of_match);
 
static struct platform_driver interface_platform_driver = {
         .probe   = interface_probe,               /* Probe method */
         .remove  = interface_remove,              /* Detach method */
         .driver  = {
                 .owner = THIS_MODULE,
                 .name = DEVICE_NAME,           /* Driver name */
                 .of_match_table = interface_of_match,
                 },
};

/* module api */

static int __init interface_init(void)
{
	return platform_driver_register(&interface_platform_driver);
}

static void __exit interface_exit(void)
{
	platform_driver_unregister(&interface_platform_driver);
}

module_init(interface_init);
module_exit(interface_exit);

MODULE_AUTHOR("Karol Gugala <karol.gugala@put.poznan.pl>");
MODULE_DESCRIPTION("AXI coprocessor interface");
MODULE_LICENSE("GPL v2");
