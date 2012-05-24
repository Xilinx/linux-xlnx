/*
 * based on arch/arm/mach-at91/cpuidle.c
 *
 * CPU idle support for Xilinx
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The cpu idle uses wait-for-interrupt and RAM self refresh in order
 * to implement two idle states -
 * #1 wait-for-interrupt
 * #2 wait-for-interrupt and RAM self refresh
 *
 * Note that this code is only intended as a prototype and is not tested
 * well yet, or tuned for the Xilinx Cortex A9. Also note that for a 
 * tickless kernel, high res timers must not be turned on. The cpuidle
 * framework must also be turned on in the kernel.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <asm/proc-fns.h>
#include <linux/io.h>
#include <linux/export.h>

#define XILINX_MAX_STATES	2

static DEFINE_PER_CPU(struct cpuidle_device, xilinx_cpuidle_device);

static struct cpuidle_driver xilinx_idle_driver = {
	.name = "xilinx_idle",
	.owner = THIS_MODULE,
};

/* Actual code that puts the SoC in different idle states */
static int xilinx_enter_idle(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			       int index)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);
	
	if (index == 0)
		/* Wait for interrupt state */
		cpu_do_idle();

	else if (index == 1) {

		/* verify this is right for A9? */

		asm("b 1f; .align 5; 1:");
		asm("mcr p15, 0, r0, c7, c10, 4");	/* drain write buffer */

		/* Add code for DDR self refresh start */

		cpu_do_idle();

		/* Add code for DDR self refresh stop */
	}

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
			(after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

/* Initialize CPU idle by registering the idle states */
static int xilinx_init_cpuidle(void)
{
	struct cpuidle_device *device;
	struct cpuidle_driver *driver = &xilinx_idle_driver;

	device = &per_cpu(xilinx_cpuidle_device, smp_processor_id());
	device->state_count = XILINX_MAX_STATES;
	driver->state_count = XILINX_MAX_STATES;

	/* Wait for interrupt state */
	driver->states[0].enter = xilinx_enter_idle;
	driver->states[0].exit_latency = 1;
	driver->states[0].target_residency = 10000;
	driver->states[0].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[0].name, "WFI");
	strcpy(driver->states[0].desc, "Wait for interrupt");

	/* Wait for interrupt and RAM self refresh state */
	driver->states[1].enter = xilinx_enter_idle;
	driver->states[1].exit_latency = 10;
	driver->states[1].target_residency = 10000;
	driver->states[1].flags = CPUIDLE_FLAG_TIME_VALID;
	strcpy(driver->states[1].name, "RAM_SR");
	strcpy(driver->states[1].desc, "WFI and RAM Self Refresh");

	cpuidle_register_driver(&xilinx_idle_driver);

	if (cpuidle_register_device(device)) {
		printk(KERN_ERR "xilinx_init_cpuidle: Failed registering\n");
		return -EIO;
	}

	pr_info("Xilinx CpuIdle Driver started\n");
	return 0;
}

device_initcall(xilinx_init_cpuidle);
