/*
 * arch/arm/mach-s3c24a0/register.c
 *
 * S3C24A0 register monitor & controller
 *
 * $Id: registers.c,v 1.3 2006/12/12 13:38:48 gerg Exp $
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
 	
#include <linux/kernel.h>
#include <linux/module.h>               /* because we are a module */
#include <linux/init.h>                 /* for the __init macros */
#include <linux/proc_fs.h>              /* all the /proc functions */
#include <linux/ioport.h>
#include <asm/uaccess.h>                /* to copy to/from userspace */
#include <asm/arch/hardware.h>

#define MODULE_NAME "regmon"
#define CPU_DIRNAME "cpu"
#define REG_DIRNAME "registers"

static ssize_t proc_read_reg(struct file * file, char * buf,
		size_t nbytes, loff_t *ppos);
static ssize_t proc_write_reg(struct file * file, const char * buffer,
		size_t count, loff_t *ppos);

static struct file_operations proc_reg_operations = {
	read:	proc_read_reg,
	write:	proc_write_reg
};

typedef struct elfin_reg_entry {
	u32 phyaddr;
	char* name;
	unsigned short low_ino;
} elfin_reg_entry_t;

static elfin_reg_entry_t elfin_regs[] =
{
/*	{ phyaddr,    name } */

	/* PLL clock */	
	{0x40000000, "LOCKTIME"},
	{0x40000004, "OSCWEST"},
	{0x40000010, "MPLLCON"},
	{0x40000014, "UPLLCON"},
	{0x40000020, "CLKCON"},
	{0x40000024, "CLKSRC"},
	{0x40000028, "CLKDIV"},
	{0x40000030, "PWRMAN"},
	{0x40000038, "SOFTRESET"},

	/* INT */
	{0x40200000, "SRCPND"},
	{0x40200004, "INTMOD"},
	{0x40200008, "INTMSK"},
	{0x4020000c, "PRIORITY"},
	{0x40200010, "INTPND"},
	{0x40200014, "INTOFFSET"},
	{0x40200018, "SUBSRCPND"},
	{0x4020001c, "INTSUBMSK"},
	{0x40200020, "VECINTMOD"},
	{0x40200024, "VECADDR"},
	{0x40200028, "NVECADDR"},
	{0x4020002c, "VAR"},

	/* SROM */
	{0x40c20000, "SROM_BW"},
	{0x40c20004, "SROM_BC0"},
	{0x40c20008, "SROM_BC1"},
	{0x40c2000c, "SROM_BC2"},

	/* PWM timer */
	{0x44000000, "TCFG0"},
	{0x44000004, "TCFG1"},
	{0x44000008, "TCON"},
	{0x4400000c, "TCNTB0"},
	{0x44000010, "TCMPB0"},
	{0x44000014, "TCNTO0"},
	{0x44000018, "TCNTB1"},
	{0x4400001c, "TCMPB1"},
	{0x44000020, "TCNTO1"},
	{0x44000024, "TCNTB2"},
	{0x44000028, "TCMPB2"},
	{0x4400002c, "TCNTO2"},
	{0x44000030, "TCNTB3"},
	{0x44000034, "TCMPB3"},
	{0x44000038, "TCNTO3"},
	{0x4400003c, "TCNTB4"},
	{0x44000040, "TCNTO4"},

	/* CamIF */
	{0x48000004, "CAM_STAY1"},
	{0x48000008, "CAM_STAY2"},
	{0x4800000c, "CAM_STAY3"},
	{0x48000010, "CAM_STAY4"},
	{0x48000000, "CAM_RDSTAT"},

	/* Post Processor */
	{0x4a100000, "VP_MODE"},
	{0x4a100004, "VP_RATIO_Y"},
	{0x4a100008, "VP_RATIO_CB"},
	{0x4a10000c, "VP_RATIO_CR"},
	{0x4a100010, "VP_SRC_WIDTH"},
	{0x4a100014, "VP_SRC_HEIGHT"},
	{0x4a100018, "VP_DST_WIDTH"},
	{0x4a10001c, "VP_DST_HEIGHT"},
	{0x4a100020, "VP_START_Y1"},
	{0x4a100024, "VP_START_Y2"},
	{0x4a100028, "VP_START_Y3"},
	{0x4a10002c, "VP_START_Y4"},
	{0x4a100030, "VP_START_CB1"},
	{0x4a100034, "VP_START_CB2"},
	{0x4a100038, "VP_START_CB3"},
	{0x4a10003c, "VP_START_CB4"},
	{0x4a100040, "VP_START_CR1"},
	{0x4a100044, "VP_START_CR2"},
	{0x4a100048, "VP_START_CR3"},
	{0x4a10004c, "VP_START_CR4"},
	{0x4a100050, "VP_START_RGB1"},
	{0x4a100054, "VP_START_RGB2"},
	{0x4a100058, "VP_START_RGB3"},
	{0x4a10005c, "VP_START_RGB4"},
	{0x4a100060, "VP_END_Y1"},
	{0x4a100064, "VP_END_Y2"},
	{0x4a100068, "VP_END_Y3"},
	{0x4a10006c, "VP_END_Y4"},
	{0x4a100070, "VP_END_CB1"},
	{0x4a100074, "VP_END_CB2"},
	{0x4a100078, "VP_END_CB3"},
	{0x4a10007c, "VP_END_CB4"},
	{0x4a100080, "VP_END_CR1"},
	{0x4a100084, "VP_END_CR2"},
	{0x4a100088, "VP_END_CR3"},
	{0x4a10008c, "VP_END_CR4"},
	{0x4a100090, "VP_END_RGB1"},
	{0x4a100094, "VP_END_RGB2"},
	{0x4a100098, "VP_END_RGB3"},
	{0x4a10009c, "VP_END_RGB4"},
	{0x4a1000f0, "VP_BYPASS"},
	{0x4a1000f4, "VP_OFS_Y"},
	{0x4a1000f8, "VP_OFS_CB"},
	{0x4a1000fc, "VP_OFS_CR"},
	{0x4a100100, "VP_OFS_RGB"},

	/* BUS matrix */
	{0x40ce0000, "BUS_PRIORITY0"},
	{0x40ce0004, "BUS_PRIORITY1"},
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#endif

static int proc_read_reg(struct file * file, char * buf,
		size_t nbytes, loff_t *ppos)
{
	int i_ino = (file->f_dentry->d_inode)->i_ino;
	char outputbuf[15];
	int count;
	int i;
	elfin_reg_entry_t* current_reg=NULL;
	if (*ppos>0) /* Assume reading completed in previous read*/
		return 0;
	for (i=0;i<ARRAY_SIZE(elfin_regs);i++) {
		if (elfin_regs[i].low_ino==i_ino) {
			current_reg = &elfin_regs[i];
			break;
		}
	}
	if (current_reg==NULL)
		return -EINVAL;

	count = sprintf(outputbuf, "0x%08lx\n",
			*((volatile unsigned long *) io_p2v(current_reg->phyaddr)));
	*ppos+=count;
	if (count>nbytes)  /* Assume output can be read at one time */
		return -EINVAL;
	if (copy_to_user(buf, outputbuf, count))
		return -EFAULT;
	return count;
}

static ssize_t proc_write_reg(struct file * file, const char * buffer,
		size_t count, loff_t *ppos)
{
	int i_ino = (file->f_dentry->d_inode)->i_ino;
	elfin_reg_entry_t* current_reg=NULL;
	int i;
	unsigned long newRegValue;
	char *endp;

	for (i=0;i<ARRAY_SIZE(elfin_regs);i++) {
		if (elfin_regs[i].low_ino==i_ino) {
			current_reg = &elfin_regs[i];
			break;
		}
	}
	if (current_reg==NULL)
		return -EINVAL;

	newRegValue = simple_strtoul(buffer,&endp,0);
	*((volatile unsigned long *) io_p2v(current_reg->phyaddr))=newRegValue;
	return (count+endp-buffer);
}

static struct proc_dir_entry *regdir;
static struct proc_dir_entry *cpudir;

static int __init init_reg_monitor(void)
{
	struct proc_dir_entry *entry;
	int i;

	cpudir = proc_mkdir(CPU_DIRNAME, &proc_root);
	if (cpudir == NULL) {
		printk(KERN_ERR MODULE_NAME": can't create /proc/" CPU_DIRNAME "\n");
		return(-ENOMEM);
	}

	regdir = proc_mkdir(REG_DIRNAME, cpudir);
	if (regdir == NULL) {
		printk(KERN_ERR MODULE_NAME": can't create /proc/" CPU_DIRNAME "/" REG_DIRNAME "\n");
		return(-ENOMEM);
	}

	for(i=0;i<ARRAY_SIZE(elfin_regs);i++) {
		entry = create_proc_entry(elfin_regs[i].name,
				S_IWUSR |S_IRUSR | S_IRGRP | S_IROTH,
				regdir);
		if(entry) {
			elfin_regs[i].low_ino = entry->low_ino;
			entry->proc_fops = &proc_reg_operations;
		} else {
			printk( KERN_ERR MODULE_NAME
				": can't create /proc/" REG_DIRNAME
				"/%s\n", elfin_regs[i].name);
			return(-ENOMEM);
		}
	}
	return (0);
}

static void __exit cleanup_reg_monitor(void)
{
	int i;
	for(i=0;i<ARRAY_SIZE(elfin_regs);i++)
		remove_proc_entry(elfin_regs[i].name,regdir);
	remove_proc_entry(REG_DIRNAME, cpudir);
	remove_proc_entry(CPU_DIRNAME, &proc_root);
}

module_init(init_reg_monitor);
module_exit(cleanup_reg_monitor);

MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
