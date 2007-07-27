#include <linux/init.h>
#include <linux/slab.h>
#include <linux/autoconf.h>
#include <asm/cpuinfo.h>
#include <asm/pvr.h>

static struct cpuinfo the_cpuinfo;
struct cpuinfo *cpuinfo = &the_cpuinfo;

void __init setup_cpuinfo(void)
{
	int pvr_level = cpu_has_pvr();
	printk(KERN_INFO "%s: initialising\n", __FUNCTION__);

	switch(pvr_level) {
		case 0 : 
			printk(KERN_WARNING "%s: No PVR support in CPU.  Using static compile-time info\n", __FUNCTION__);
			set_cpuinfo_static(cpuinfo);
			break;
#if 0
		case 1 : set_cpuinfo_pvr_partial(cpuinfo);
			break;
#endif
		case 2 : 
			printk(KERN_INFO "%s: Using full CPU PVR support\n",__FUNCTION__);
			set_cpuinfo_pvr_full(cpuinfo);
			break;
		default:
			WARN_ON(1);
			set_cpuinfo_static(cpuinfo);
	}
}

