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
#include <linux/io.h>
#include <linux/export.h>
#include <linux/clockchips.h>
#include <linux/cpu_pm.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/proc-fns.h>

#define XILINX_MAX_STATES	1

static DEFINE_PER_CPU(struct cpuidle_device, xilinx_cpuidle_device);

/* Actual code that puts the SoC in different idle states */
static int xilinx_enter_idle(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	struct timeval before, after;
	int idle_time;

	local_irq_disable();
	do_gettimeofday(&before);

	if (index == 0)
		/* Wait for interrupt state */
		cpu_do_idle();

	else if (index == 1) {
		unsigned int cpu_id = smp_processor_id();

		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &cpu_id);

		/* Devices must be stopped here */
		cpu_pm_enter();

		/* Add code for DDR self refresh start */

		cpu_do_idle();
		/*cpu_suspend(foo, bar);*/

		/* Add code for DDR self refresh stop */

		cpu_pm_exit();

		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &cpu_id);
	}

	do_gettimeofday(&after);
	local_irq_enable();
	idle_time = (after.tv_sec - before.tv_sec) * USEC_PER_SEC +
			(after.tv_usec - before.tv_usec);

	dev->last_residency = idle_time;
	return index;
}

static struct cpuidle_driver xilinx_idle_driver = {
	.name = "xilinx_idle",
	.owner = THIS_MODULE,
	.state_count = XILINX_MAX_STATES,
	/* Wait for interrupt state */
	.states[0] = {
		.enter = xilinx_enter_idle,
		.exit_latency = 1,
		.target_residency = 10000,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.name = "WFI",
		.desc = "Wait for interrupt",
	},
	/* Wait for interrupt and RAM self refresh state */
	.states[1] = {
		.enter = xilinx_enter_idle,
		.exit_latency = 10,
		.target_residency = 10000,
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.name = "RAM_SR",
		.desc = "WFI and RAM Self Refresh",
	},
};

/* Initialize CPU idle by registering the idle states */
static int xilinx_init_cpuidle(void)
{
	unsigned int cpu;
	struct cpuidle_device *device;
	int ret;

	ret = cpuidle_register_driver(&xilinx_idle_driver);
	if (ret) {
		pr_err("Registering Xilinx CpuIdle Driver failed.\n");
		return ret;
	}

	for_each_possible_cpu(cpu) {
		device = &per_cpu(xilinx_cpuidle_device, cpu);
		device->state_count = XILINX_MAX_STATES;
		device->cpu = cpu;
		ret = cpuidle_register_device(device);
		if (ret) {
			pr_err("xilinx_init_cpuidle: Failed registering\n");
			return ret;
		}
	}

	pr_info("Xilinx CpuIdle Driver started\n");
	return 0;
}
device_initcall(xilinx_init_cpuidle);
