/*
  21Mar2001    1.1    dgt/microtronix: Altera Excalibur/Nios32 port
  30Jun2003           kenw/microtronix: Remove cmdline check in flash
*/

/*
 *  linux/arch/niosnommu/kernel/setup.c
 *
 *  Copyright (C) 2004       Microtronix Datacom Ltd.
 *  Copyright (C) 2001       Vic Phillips {vic@microtronix.com}
 *  Copyleft  (C) 2000       James D. Schettine {james@telos-systems.com}
 *  Copyright (C) 1999       Greg Ungerer (gerg@moreton.com.au)
 *  Copyright (C) 1998,2000  D. Jeff Dionne <jeff@lineo.ca>
 *  Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 *  Copyright (C) 1995       Hamish Macdonald
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * This file handles the architecture-dependent parts of system setup
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/genhd.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <linux/seq_file.h>

#include <asm/irq.h>
#include <asm/byteorder.h>
//#include <asm/niosconf.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_BLK_DEV_INITRD
#include <asm/pgtable.h>
#endif

#ifdef CONFIG_NIOS_SPI
#include <asm/spi.h>
extern ssize_t spi_write(struct file *filp, const char *buf, size_t count, loff_t *ppos);
extern ssize_t spi_read (struct file *filp, char *buf, size_t count, loff_t *ppos);
extern loff_t spi_lseek (struct file *filp, loff_t offset, int origin);
extern int spi_open     (struct inode *inode, struct file *filp);
extern int spi_release  (struct inode *inode, struct file *filp);
#endif

#ifdef CONFIG_CONSOLE
extern struct consw *conswitchp;
#endif

unsigned long rom_length;
unsigned long memory_start;
unsigned long memory_end;

EXPORT_SYMBOL(memory_start);
EXPORT_SYMBOL(memory_end);

#ifndef CONFIG_CMDLINE
#define CONFIG_CMDLINE	"CONSOLE=/dev/ttyS0 root=/dev/rom0 ro"
#endif

#ifndef CONFIG_PASS_CMDLINE
static char default_command_line[] = CONFIG_CMDLINE;
#endif
static char command_line[COMMAND_LINE_SIZE] = { 0, };


/*				   r1  r2  r3  r4  r5  r6  r7  r8  r9 r10 r11*/
/*				   r12 r13 r14 r15 or2                      ra  fp  sp  gp es  ste  ea*/
static struct pt_regs fake_regs = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,\
				    0,  0,  0,  0,  0, (unsigned long)cpu_idle,  0,  0,  0, 0,   0,  0};

#define CPU "NIOS2"

#if defined (CONFIG_CS89x0) || defined (CONFIG_SMC91111) || defined (CONFIG_OPEN_ETH) || defined (CONFIG_MTIP1000_ETH) || defined (CONFIG_DM9000_ETH) || defined (CONFIG_SMC91X) || defined (CONFIG_DM9000) || defined (CONFIG_DM9KS)
    #if defined (CONFIG_MTIP1000_ETH)                       //;dgt3;
        #include <../drivers/net/mtip1000.h>                //;dgt3;
    #endif                                                  //;dgt3;

    unsigned char *excalibur_enet_hwaddr;
    unsigned char excalibur_enet_hwaddr_array[6];
#endif

// save args passed from u-boot, called from head.S
void nios2_boot_init(unsigned r4,unsigned r5,unsigned r6,unsigned r7)
{
#if defined(CONFIG_PASS_CMDLINE)
  if (r4 == 0x534f494e)   // r4 is magic NIOS, to become board info check in the future
    {
#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r5) {
		initrd_start = r5;
		initrd_end = r6;
	}
#endif				/* CONFIG_BLK_DEV_INITRD */
	if (r7)
		strncpy(command_line, (char *)r7, COMMAND_LINE_SIZE);
    }
#endif
}

inline void flash_command(int base, int offset, short data)
{
	volatile unsigned short * ptr=(unsigned short*) (base);

	ptr[0x555]=0xaa;
	ptr[0x2aa]=0x55;
	ptr[offset]=data;
}

inline void exit_se_flash(int base)
{
	flash_command(base, 0x555, 0x90);
	*(unsigned short*)base=0;
}

void setup_arch(char **cmdline_p)
{
	int bootmap_size;
	extern int _stext, _etext;
	extern int _edata, _end;
	extern int _ramend;
#ifdef DEBUG
	extern int _sdata, _sbss, _ebss;
#ifdef CONFIG_BLK_DEV_BLKMEM
	extern int *romarray;
#endif
#endif
#if 0							    // krh
	unsigned char *psrc=(unsigned char *)((NIOS_FLASH_START + NIOS_FLASH_END)>>1);
	int i=0;
#endif							    // krh

	memory_start = (unsigned long)&_end;
	memory_end = (unsigned long) &_ramend;

#if 0                                                       //;kenw;
	/* copy the command line from booting paramter region */
    #if defined (nasys_am29lv065d_flash_0)                  //;dgt;
      {                                                     //;dgt;
        // ...TBA...                                        //;dgt;
      }                                                     //;dgt;
    #else                                                   //;dgt;
	    flash_command((int)psrc, 0x555, 0x88);
	    while ((*psrc!=0xFF) && (i<sizeof(command_line))) {
	    	command_line[i++]=*psrc++;
	    }
	    command_line[i]=0;
	    exit_se_flash(((NIOS_FLASH_START + NIOS_FLASH_END)>>1) );
	    if (command_line[0]==0)
    #endif                                                  //;dgt;
#endif                                                      //;kenw;
#ifndef CONFIG_PASS_CMDLINE
		memcpy(command_line, default_command_line, sizeof(default_command_line));
#endif

	printk("\x0F\r\n\nuClinux/Nios II\n");
	printk("Altera Nios II support (C) 2004 Microtronix Datacom Ltd.\n");

#ifdef DEBUG
	printk("KERNEL -> TEXT=0x%08x-0x%08x DATA=0x%08x-0x%08x "
		"BSS=0x%08x-0x%08x\n", (int) &_stext, (int) &_etext,
		(int) &_sdata, (int) &_edata,
		(int) &_sbss, (int) &_ebss);
	printk("KERNEL -> MEM=0x%06x-0x%06x STACK=0x%06x-0x%06x\n",
		(int) memory_start, (int) memory_end,
		(int) memory_end, (int) nasys_program_mem_end);
#endif

	init_mm.start_code = (unsigned long) &_stext;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) 0;
	init_task.thread.kregs = &fake_regs;

#if 0
	ROOT_DEV = MKDEV(BLKMEM_MAJOR,0);
#endif

	/* Keep a copy of command line */
	*cmdline_p = &command_line[0];

	memcpy(saved_command_line, command_line, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = 0;

#ifdef DEBUG
	if (strlen(*cmdline_p))
		printk("Command line: '%s'\n", *cmdline_p);
	else
		printk("No Command line passed\n");
#endif


#if defined (CONFIG_CS89x0) || defined (CONFIG_SMC91111) || defined (CONFIG_OPEN_ETH) || defined (CONFIG_MTIP1000_ETH) || defined (CONFIG_DM9000_ETH) || defined (CONFIG_SMC91X) || defined (CONFIG_DM9000) || defined (CONFIG_DM9KS)

    #if defined (CONFIG_MTIP1000_ETH)                       //;dgt3;
        (*((np_mtip_mac *)                                  //;dgt3;
                (na_mtip_mac_control_port))).               //;dgt3;
                    COMMAND_CONFIG = 0;                     //;dgt3;
    #endif                                                  //;dgt3;

	/* now read the hwaddr of the ethernet --wentao*/

    #if 1                                                   //;dgt2;
//    #if defined (nasys_am29lv065d_flash_0)                //;dgt;
      {                                                     //;dgt;
        unsigned char   *flashptr               =           //;dgt;
            ((unsigned char *)                              //;dgt;
                ((                                          //;dgt;
                  #if defined (na_flash_kernel_end)         //;dgt2;
                      na_flash_kernel_end                   //;dgt2;
                  #else                                     //;dgt2;
                    #if defined (na_flash_kernel_base)      //;dgt2;
                      na_flash_kernel_base      +           //;dgt;
                    #else                                   //;dgt2;
                      na_flash_kernel           +           //;dgt2;
                    #endif                                  //;dgt2;
                      na_flash_kernel_size                  //;dgt2;
                  #endif                                    //;dgt2;
                      - 0x00010000)));                      //;dgt;
          // last 64K of Altera stratix/cyclone flash       //;dgt;
                                                            //;dgt;
        if((*((unsigned long *) flashptr)) == 0x00005AFE)   //;dgt;
          {                                                 //;dgt;
            memcpy(excalibur_enet_hwaddr_array,             //;dgt;
                   ((void*) (flashptr+4)),6);               //;dgt;
          }                                                 //;dgt;
          else                                              //;dgt;
          {                                                 //;dgt;
            printk("\nsetup_arch: No persistant network"    //;dgt;
                        " settings signature at %08lX\n",   //;dgt;
                   ((unsigned long) flashptr));             //;dgt;
            *((unsigned long *)                             //;dgt;
                 (&(excalibur_enet_hwaddr_array[0]))) =     //;dgt;
                    0x00ED0700;                             //;dgt2;
                      /* 0x00-07-ED: Altera Corporation.    //;dgt;     */
            *((unsigned short *)                            //;dgt;
                 (&(excalibur_enet_hwaddr_array[4]))) =     //;dgt;
                    0x0000;                                 //;dgt;
            /* Should be: 0x-00-07-ED-0A-03-(Random# 0-256) //;dgt2;    */
            /* 0x-00-07-ED-0A-xx-yy   Vermont boards        //;dgt2;    */
            /* 0x-00-07-ED-0B-xx-yy   Rhode Island boards   //;dgt2;    */
            /* 0x-00-07-ED-0C-xx-yy   Delaware boards       //;dgt2;    */
            /*                00        Internal Altera     //;dgt2;    */
            /*                01        Beta, pre-production//;dgt2;    */
            /*                02        Beta, pre-production//;dgt2;    */
            /*                03        Customer use        //;dgt2;    */
          }                                                 //;dgt;
      }                                                     //;dgt;
    #else                                                   //;dgt;
	  flash_command(NIOS_FLASH_START, 0x555, 0x88);
	  memcpy(excalibur_enet_hwaddr_array,(void*)NIOS_FLASH_START,6);
	  exit_se_flash(NIOS_FLASH_START);;
    #endif                                                  //;dgt;

	/* now do the checking, make sure we got a valid addr */
	if (excalibur_enet_hwaddr_array[0] & (unsigned char)1)
	{
		printk("Ethernet hardware address:Clearing invalid bit #0\n");
		excalibur_enet_hwaddr_array[0] ^= (unsigned char)1;
	}
	excalibur_enet_hwaddr=excalibur_enet_hwaddr_array;
#ifdef DEBUG
	printk("Setup the hardware addr for ethernet\n\t %02x %02x %02x %02x %02x %02x\n",
		excalibur_enet_hwaddr[0],excalibur_enet_hwaddr[1],
		excalibur_enet_hwaddr[2],excalibur_enet_hwaddr[3],
		excalibur_enet_hwaddr[4],excalibur_enet_hwaddr[5]);
#endif
#endif


	/*
	 * give all the memory to the bootmap allocator,  tell it to put the
	 * boot mem_map at the start of memory
	 */
	bootmap_size = init_bootmem_node(
			NODE_DATA(0),
			memory_start >> PAGE_SHIFT, /* map goes here */
			PAGE_OFFSET >> PAGE_SHIFT,	/* 0 on coldfire */
			memory_end >> PAGE_SHIFT);
	/*
	 * free the usable memory,  we have to make sure we do not free
	 * the bootmem bitmap so we then reserve it after freeing it :-)
	 */
	free_bootmem(memory_start, memory_end - memory_start);
	reserve_bootmem(memory_start, bootmap_size);
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) reserve_bootmem(virt_to_phys((void *)initrd_start), initrd_end - initrd_start);
#endif /* CONFIG_BLK_DEV_INITRD */
	/*
	 * get kmalloc into gear
	 */
	paging_init();
#ifdef CONFIG_VT
#if defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

#ifdef DEBUG
	printk("Done setup_arch\n");
#endif

}

int get_cpuinfo(char * buffer)
{
    char *cpu, *mmu, *fpu;
    u_long clockfreq;

    cpu = CPU;
    mmu = "none";
    fpu = "none";

    clockfreq = nasys_clock_freq;

    return(sprintf(buffer, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   (loops_per_jiffy*HZ)/500000,((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ)));

}

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
    char *cpu, *mmu, *fpu;
    u_long clockfreq;

    cpu = CPU;
    mmu = "none";
    fpu = "none";

    clockfreq = nasys_clock_freq;

    seq_printf(m, "CPU:\t\t%s\n"
		   "MMU:\t\t%s\n"
		   "FPU:\t\t%s\n"
		   "Clocking:\t%lu.%1luMHz\n"
		   "BogoMips:\t%lu.%02lu\n"
		   "Calibration:\t%lu loops\n",
		   cpu, mmu, fpu,
		   clockfreq/1000000,(clockfreq/100000)%10,
		   (loops_per_jiffy*HZ)/500000,((loops_per_jiffy*HZ)/5000)%100,
		   (loops_per_jiffy*HZ));

	return 0;
}

#ifdef CONFIG_NIOS_SPI

static int bcd2char( int x )
{
        if ( (x & 0xF) > 0x90 || (x & 0x0F) > 0x09 )
                return 99;

        return (((x & 0xF0) >> 4) * 10) + (x & 0x0F);
}

#endif // CONFIG_NIOS_SPI


void arch_gettod(int *year, int *month, int *date, int *hour, int *min, int *sec)
{
#ifdef CONFIG_NIOS_SPI
        /********************************************************************/
  	/* Read the CMOS clock on the Microtronix Datacom O/S Support card. */
  	/* Use the SPI driver code, but circumvent the file system by using */
        /* its internal functions.                                          */
        /********************************************************************/
        int  hr;

	struct                               /*********************************/
        {                                    /* The SPI payload. Warning: the */
	      unsigned short register_addr;  /* sizeof() operator will return */
	      unsigned char  value;          /* a length of 4 instead of 3!   */
        } spi_data;                          /*********************************/


	if ( spi_open( NULL, NULL ) )
	{
	    printk( "Cannot open SPI driver to read system CMOS clock.\n" );
	    *year = *month = *date = *hour = *min = *sec = 0;
	    return;
	}

	spi_lseek( NULL, clockCS, 0 /* == SEEK_SET */ );

	spi_data.register_addr = clock_write_control;
	spi_data.value         = 0x40; // Write protect
	spi_write( NULL, (const char *)&spi_data, 3, NULL  );

	spi_data.register_addr = clock_read_sec;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL );
	*sec = (int)bcd2char( spi_data.value );

	spi_data.register_addr = clock_read_min;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL  );
	*min = (int)bcd2char( spi_data.value );

	spi_data.register_addr = clock_read_hour;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL  );
	hr = (int)bcd2char( spi_data.value );
	if ( hr & 0x40 )  // Check 24-hr bit
 	    hr = (hr & 0x3F) + 12;     // Convert to 24-hr

	*hour = hr;



	spi_data.register_addr = clock_read_date;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL  );
	*date = (int)bcd2char( spi_data.value );

	spi_data.register_addr = clock_read_month;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL  );
	*month = (int)bcd2char( spi_data.value );

	spi_data.register_addr = clock_read_year;
	spi_data.value         = 0;
	spi_read( NULL, (char *)&spi_data, 3, NULL  );
	*year = (int)bcd2char( spi_data.value );


	spi_release( NULL, NULL );
#else
	*year = *month = *date = *hour = *min = *sec = 0;

#endif
}

static void *cpuinfo_start (struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? ((void *) 0x12345678) : NULL;
}

static void *cpuinfo_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return cpuinfo_start (m, pos);
}

static void cpuinfo_stop (struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	start:	cpuinfo_start,
	next:	cpuinfo_next,
	stop:	cpuinfo_stop,
	show:	show_cpuinfo
};


// adapted from linux/arch/arm/mach-versatile/core.c and mach-bast
// note, hardware MAC address is still undefined

#if defined(CONFIG_SMC91X) && defined(na_enet)

#ifndef LAN91C111_REGISTERS_OFFSET
#define LAN91C111_REGISTERS_OFFSET 0x300
#endif

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= na_enet + LAN91C111_REGISTERS_OFFSET,
		.end		= na_enet + LAN91C111_REGISTERS_OFFSET + 0x100 - 1,    // 32bits,64k, LAN91C111_REGISTERS_OFFSET 0x0300 ?
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= na_enet_irq,
		.end		= na_enet_irq,
		.flags		= IORESOURCE_IRQ,
	},
};
static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};
static int __init smc91x_device_init(void)
{
	/* customizes platform devices, or adds new ones */
	platform_device_register(&smc91x_device);
	return 0;
}
arch_initcall(smc91x_device_init);
#endif // CONFIG_SMC91X


#if defined(na_DM9000A) && !defined(na_dm9000)   // defs for DE2
#define na_dm9000 na_DM9000A
#define na_dm9000_irq na_DM9000A_irq
#endif

#if defined(CONFIG_DM9000) && defined(na_dm9000)
#include <linux/dm9000.h>
static struct resource dm9k_resource[] = {
	[0] = {
		.start = na_dm9000,
		.end   = na_dm9000 + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = na_dm9000 + 4,
		.end   = na_dm9000 + 4 + 3,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = na_dm9000_irq,
		.end   = na_dm9000_irq,
		.flags = IORESOURCE_IRQ,
	}

};
static struct dm9000_plat_data dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};
static struct platform_device dm9k_device = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9k_resource),
	.resource	= dm9k_resource,
	.dev		= {
		.platform_data = &dm9k_platdata,
	}
};
static int __init dm9k_device_init(void)
{
	/* customizes platform devices, or adds new ones */
	platform_device_register(&dm9k_device);
	return 0;
}
arch_initcall(dm9k_device_init);
#endif // CONFIG_DM9000


#if defined(CONFIG_SERIO_ALTPS2) && defined(na_ps2_0)

static struct resource altps2_0_resources[] = {
	[0] = {
		.start		= na_ps2_0,
		.end		= na_ps2_0 + 0x8 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= na_ps2_0_irq,
		.end		= na_ps2_0_irq,
		.flags		= IORESOURCE_IRQ,
	},
};
static struct platform_device altps2_0_device = {
	.name		= "altps2",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(altps2_0_resources),
	.resource	= altps2_0_resources,
};

#if defined(na_ps2_1)
static struct resource altps2_1_resources[] = {
	[0] = {
		.start		= na_ps2_1,
		.end		= na_ps2_1 + 0x8 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= na_ps2_1_irq,
		.end		= na_ps2_1_irq,
		.flags		= IORESOURCE_IRQ,
	},
};
static struct platform_device altps2_1_device = {
	.name		= "altps2",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(altps2_1_resources),
	.resource	= altps2_1_resources,
};
#endif // na_ps2_1

static int __init altps2_device_init(void)
{
	/* customizes platform devices, or adds new ones */
	platform_device_register(&altps2_0_device);
#if defined(na_ps2_1)
	platform_device_register(&altps2_1_device);
#endif // na_ps2_1
	return 0;
}
arch_initcall(altps2_device_init);
#endif // CONFIG_SERIO_ALTPS2

#if defined(CONFIG_I2C_GPIO) && defined(na_gpio_0)
#include <asm/gpio.h>

static struct gpio_i2c_pins i2c_gpio_0_pins = {
        .sda_pin	= (na_gpio_0+(0<<2)),
	.scl_pin	= (na_gpio_0+(1<<2)),
};

static struct platform_device i2c_gpio_0_controller = {
	.name		= "GPIO-I2C",
	.id		= 0,
	.dev		= {
		.platform_data = &i2c_gpio_0_pins,
	},
	.num_resources	= 0
};

static int __init i2c_gpio_device_init(void)
{
	/* customizes platform devices, or adds new ones */
	platform_device_register(&i2c_gpio_0_controller);
	return 0;
}
arch_initcall(i2c_gpio_device_init);

#endif // CONFIG_I2C_GPIO
