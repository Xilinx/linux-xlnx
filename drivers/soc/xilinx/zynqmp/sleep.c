/*
 * Suspend support for ZynqMP
 *
 *  Copyright (C) 2015 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/suspend.h>
#include <asm/cpuidle.h>

static int zynqmp_pm_enter(suspend_state_t suspend_state)
{
	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		cpu_suspend(0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct platform_suspend_ops zynqmp_pm_ops = {
	.enter		= zynqmp_pm_enter,
	.valid		= suspend_valid_only_mem,
};

static int __init zynqmp_pm_late_init(void)
{
	suspend_set_ops(&zynqmp_pm_ops);

	return 0;
}
late_initcall(zynqmp_pm_late_init);
