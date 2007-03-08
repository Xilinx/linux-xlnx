/*
 *  linux/arch/arm/mach-s3c44b0x/arch.c
 *  	nickmit_zheng@eastday.com
 *  		based on
 *	Hyok S. Choi (hyok.choi@samsung.com)
 * 	linux 2.6 armnommu porting
 */
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysdev.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>
#include <asm/mach/map.h>

#include <linux/tty.h>
#include <asm/elf.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>

// used by sysctl
//#define DEFAULT_MAX_MAP_COUNT	65536
//int sysctl_max_map_count = DEFAULT_MAX_MAP_COUNT;

int s3c44b0x_fMHZ 	= CONFIG_ARM_CLK / 1000000;
int s3c44b0x_finMHZ	= CONFIG_ARM_CLK_FIN / 1000000;

extern void __init s3c44b0x_init_irq(void);
extern void s3c44b0x_time_init(void);

void __init s3c44b0x_init_machine(void)
{
}

#if CONFIG_DEBUG_NICKMIT
// char my_cmdline[] = "root=/dev/ram rw initrd=0x0c700000,512K";
char my_cmdline[1024] = "root=/dev/nfs "
	"nfsroot=192.168.1.24:/armboot "
	"ip=192.168.1.8:192.168.1.24:192.168.1.1:255.255.255.0:arm:eth0:off";
	
void __init change_cmdline(char **cmdline)
{
	int magic_addr[] = {0xcf00000, 0xce000000, 0x1e0000};
	char magic_head[] = "Kernel cmdline:";
	int i;
	int cnt = sizeof (magic_addr) / sizeof (int);
	char *p, *d;
	for(i=0;i<cnt;i++) {
		p = (char *) magic_addr[i];
		if (strncmp(p, magic_head, (sizeof magic_head) - 1) != 0)
			continue;
		p += sizeof magic_head - 1;
		d = my_cmdline;
		while (*p != '\r' && *p != '\n' && (d - my_cmdline - sizeof my_cmdline))
			*d++ = *p++;

		*d = 0;
		*cmdline = my_cmdline;
		return;
	}
	*cmdline = my_cmdline;
}

void __init load_initrd(void *src, void *dst, size_t count)
{
	int verify = 1;
	printk("Load initrd image from flash(%08x) to SDRAM(%08x), Length = %d ...", src, dst, count);
	memmove(dst, src, count);
	if (verify) {
		printk("Verify ...");
		printk("%s\n", memcmp(src, dst, count) == 0 ? "Done" : "Failed");
	} else {
		printk("Done\n");
	}
}
#endif

void __init s3c44b0x_fixup(struct machine_desc *desc, struct param_struct *params, char **cmdline, struct meminfo *mi)
{
#if CONFIG_NICKMIT_DEBUG
	change_cmdline(cmdline);
//	load_initrd(0x1000, 0x0c700000, 0x100000);
#endif
}

MACHINE_START(S3C44B0, "S3C44B0X Development Board")
	MAINTAINER("nickmit <nickmit_zheng@eastday.com>")
	FIXUP(s3c44b0x_fixup)
	INITIRQ(s3c44b0x_init_irq)
	INIT_MACHINE(s3c44b0x_init_machine)
	INITTIME(s3c44b0x_time_init)
MACHINE_END
