/*
 * Cortex A57 and A53 EDAC
 *
 * Copyright (c) 2015, Advanced Micro Devices
 * Author: Brijesh Singh <brijeshkumar.singh@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <ras/ras_event.h>

#include "edac_module.h"

#define DRV_NAME			"cortex_edac"

#define CPUMERRSR_EL1_INDEX(x, y)	((x) & (y))
#define CPUMERRSR_EL1_BANK_WAY(x, y)	(((x) >> 18) & (y))
#define CPUMERRSR_EL1_RAMID(x)		(((x) >> 24) & 0x7f)
#define CPUMERRSR_EL1_VALID(x)		((x) & (1 << 31))
#define CPUMERRSR_EL1_REPEAT(x)		(((x) >> 32) & 0x7f)
#define CPUMERRSR_EL1_OTHER(x)		(((x) >> 40) & 0xff)
#define CPUMERRSR_EL1_FATAL(x)		((x) & (1UL << 63))
#define L1_I_TAG_RAM			0x00
#define L1_I_DATA_RAM			0x01
#define L1_D_TAG_RAM			0x08
#define L1_D_DATA_RAM			0x09
#define L1_D_DIRTY_RAM			0x14
#define TLB_RAM				0x18

#define L2MERRSR_EL1_CPUID_WAY(x)	(((x) >> 18) & 0xf)
#define L2MERRSR_EL1_RAMID(x)		(((x) >> 24) & 0x7f)
#define L2MERRSR_EL1_VALID(x)		((x) & (1 << 31))
#define L2MERRSR_EL1_REPEAT(x)		(((x) >> 32) & 0xff)
#define L2MERRSR_EL1_OTHER(x)		(((x) >> 40) & 0xff)
#define L2MERRSR_EL1_FATAL(x)		((x) & (1UL << 63))
#define L2_TAG_RAM			0x10
#define L2_DATA_RAM			0x11
#define L2_SNOOP_RAM			0x12
#define L2_DIRTY_RAM			0x14
#define L2_INCLUSION_PF_RAM		0x18

#define L1_CACHE			0
#define L2_CACHE			1

#define EDAC_MOD_STR			DRV_NAME

static int poll_msec = 100;

struct cortex_arm64_edac {
	struct edac_device_ctl_info *edac_ctl;
};

static inline u64 read_cpumerrsr_el1(void)
{
	u64 val;

	asm volatile("mrs %0, s3_1_c15_c2_2" : "=r" (val));
	return val;
}

static inline void write_cpumerrsr_el1(u64 val)
{
	asm volatile("msr s3_1_c15_c2_2, %0" :: "r" (val));
}

static inline u64 read_l2merrsr_el1(void)
{
	u64 val;

	asm volatile("mrs %0, s3_1_c15_c2_3" : "=r" (val));
	return val;
}

static inline void write_l2merrsr_el1(u64 val)
{
	asm volatile("msr s3_1_c15_c2_3, %0" :: "r" (val));
}

static void parse_cpumerrsr(void *arg)
{
	int cpu, partnum, way;
	unsigned int index = 0;
	u64 val = read_cpumerrsr_el1();
	int repeat_err, other_err;

	/* we do not support fatal error handling so far */
	if (CPUMERRSR_EL1_FATAL(val))
		return;

	/* check if we have valid error before continuing */
	if (!CPUMERRSR_EL1_VALID(val))
		return;

	cpu = smp_processor_id();
	partnum = read_cpuid_part_number();
	repeat_err = CPUMERRSR_EL1_REPEAT(val);
	other_err = CPUMERRSR_EL1_OTHER(val);

	/* way/bank and index address bit ranges are different between
	 * A57 and A53 */
	if (partnum == ARM_CPU_PART_CORTEX_A57) {
		index = CPUMERRSR_EL1_INDEX(val, 0x1ffff);
		way = CPUMERRSR_EL1_BANK_WAY(val, 0x1f);
	} else {
		index = CPUMERRSR_EL1_INDEX(val, 0xfff);
		way = CPUMERRSR_EL1_BANK_WAY(val, 0x7);
	}

	edac_printk(KERN_CRIT, EDAC_MOD_STR, "CPU%d L1 error detected!\n", cpu);
	edac_printk(KERN_CRIT, EDAC_MOD_STR, "index=%#x, RAMID=", index);

	switch (CPUMERRSR_EL1_RAMID(val)) {
	case L1_I_TAG_RAM:
		pr_cont("'L1-I Tag RAM' (way %d)", way);
		break;
	case L1_I_DATA_RAM:
		pr_cont("'L1-I Data RAM' (bank %d)", way);
		break;
	case L1_D_TAG_RAM:
		pr_cont("'L1-D Tag RAM' (way %d)", way);
		break;
	case L1_D_DATA_RAM:
		pr_cont("'L1-D Data RAM' (bank %d)", way);
		break;
	case L1_D_DIRTY_RAM:
		pr_cont("'L1 Dirty RAM'");
		break;
	case TLB_RAM:
		pr_cont("'TLB RAM'");
		break;
	default:
		pr_cont("'unknown'");
		break;
	}

	pr_cont(", repeat=%d, other=%d (CPUMERRSR_EL1=%#llx)\n", repeat_err,
		other_err, val);

	trace_mc_event(HW_EVENT_ERR_CORRECTED, "L1 non-fatal error",
		       "", repeat_err, 0, 0, 0, -1, index, 0, 0, DRV_NAME);
	write_cpumerrsr_el1(0);
}

static void a57_parse_l2merrsr_way(u8 ramid, u8 val)
{
	switch (ramid) {
	case L2_TAG_RAM:
	case L2_DATA_RAM:
	case L2_DIRTY_RAM:
		pr_cont("(cpu%d tag, way %d)", val / 2, val % 2);
		break;
	case L2_SNOOP_RAM:
		pr_cont("(cpu%d tag, way %d)", (val & 0x6) >> 1,
			(val & 0x1));
		break;
	}
}

static void a53_parse_l2merrsr_way(u8 ramid, u8 val)
{
	switch (ramid) {
	case L2_TAG_RAM:
		pr_cont("(way %d)", val);
		fallthrough;
	case L2_DATA_RAM:
		pr_cont("(bank %d)", val);
		break;
	case L2_SNOOP_RAM:
		pr_cont("(cpu%d tag, way %d)", val / 2, val % 4);
		break;
	}
}

static void parse_l2merrsr(void *arg)
{
	int cpu, partnum;
	unsigned int index;
	int repeat_err, other_err;
	u64 val = read_l2merrsr_el1();

	/* we do not support fatal error handling so far */
	if (L2MERRSR_EL1_FATAL(val))
		return;

	/* check if we have valid error before continuing */
	if (!L2MERRSR_EL1_VALID(val))
		return;

	cpu = smp_processor_id();
	partnum = read_cpuid_part_number();
	repeat_err = L2MERRSR_EL1_REPEAT(val);
	other_err = L2MERRSR_EL1_OTHER(val);

	/* index address range is different between A57 and A53 */
	if (partnum == ARM_CPU_PART_CORTEX_A57)
		index = val & 0x1ffff;
	else
		index = (val >> 3) & 0x3fff;

	edac_printk(KERN_CRIT, EDAC_MOD_STR, "CPU%d L2 error detected!\n", cpu);
	edac_printk(KERN_CRIT, EDAC_MOD_STR, "index=%#x RAMID=", index);

	switch (L2MERRSR_EL1_RAMID(val)) {
	case L2_TAG_RAM:
		pr_cont("'L2 Tag RAM'");
		break;
	case L2_DATA_RAM:
		pr_cont("'L2 Data RAM'");
		break;
	case L2_SNOOP_RAM:
		pr_cont("'L2 Snoop tag RAM'");
		break;
	case L2_DIRTY_RAM:
		pr_cont("'L2 Dirty RAM'");
		break;
	case L2_INCLUSION_PF_RAM:
		pr_cont("'L2 inclusion PF RAM'");
		break;
	default:
		pr_cont("unknown");
		break;
	}

	/* cpuid/way bit description is different between A57 and A53 */
	if (partnum == ARM_CPU_PART_CORTEX_A57)
		a57_parse_l2merrsr_way(L2MERRSR_EL1_RAMID(val),
				       L2MERRSR_EL1_CPUID_WAY(val));
	else
		a53_parse_l2merrsr_way(L2MERRSR_EL1_RAMID(val),
				       L2MERRSR_EL1_CPUID_WAY(val));

	pr_cont(", repeat=%d, other=%d (L2MERRSR_EL1=%#llx)\n", repeat_err,
		other_err, val);
	trace_mc_event(HW_EVENT_ERR_CORRECTED, "L2 non-fatal error",
		       "", repeat_err, 0, 0, 0, -1, index, 0, 0, DRV_NAME);
	write_l2merrsr_el1(0);
}

static void cortex_arm64_edac_check(struct edac_device_ctl_info *edac_ctl)
{
	int cpu;
	struct cpumask cluster_mask, old_mask;

	cpumask_clear(&cluster_mask);
	cpumask_clear(&old_mask);

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		/* Check CPU L1 error */
		smp_call_function_single(cpu, parse_cpumerrsr, NULL, 0);
		cpumask_copy(&cluster_mask, topology_core_cpumask(cpu));
		if (cpumask_equal(&cluster_mask, &old_mask))
			continue;
		cpumask_copy(&old_mask, &cluster_mask);
		/* Check CPU L2 error */
		smp_call_function_any(&cluster_mask, parse_l2merrsr, NULL, 0);
	}
	cpus_read_unlock();
}

static int cortex_arm64_edac_probe(struct platform_device *pdev)
{
	int rc;
	struct cortex_arm64_edac *drv;
	struct device *dev = &pdev->dev;

	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	/* Only POLL mode is supported */
	edac_op_state = EDAC_OPSTATE_POLL;

	drv->edac_ctl = edac_device_alloc_ctl_info(0, "cpu_cache", 1, "L", 2,
						   0, NULL, 0,
						   edac_device_alloc_index());
	if (IS_ERR(drv->edac_ctl))
		return -ENOMEM;

	drv->edac_ctl->poll_msec = poll_msec;
	drv->edac_ctl->edac_check = cortex_arm64_edac_check;
	drv->edac_ctl->dev = dev;
	drv->edac_ctl->mod_name = dev_name(dev);
	drv->edac_ctl->dev_name = dev_name(dev);
	drv->edac_ctl->ctl_name = "cache_err";
	platform_set_drvdata(pdev, drv);

	rc = edac_device_add_device(drv->edac_ctl);
	if (rc)
		edac_device_free_ctl_info(drv->edac_ctl);

	return rc;
}

static int cortex_arm64_edac_remove(struct platform_device *pdev)
{
	struct cortex_arm64_edac *drv = dev_get_drvdata(&pdev->dev);
	struct edac_device_ctl_info *edac_ctl = drv->edac_ctl;

	edac_device_del_device(edac_ctl->dev);
	edac_device_free_ctl_info(edac_ctl);

	return 0;
}

static const struct of_device_id cortex_arm64_edac_of_match[] = {
	{ .compatible = "arm,cortex-a57-edac" },
	{ .compatible = "arm,cortex-a53-edac" },
	{}
};
MODULE_DEVICE_TABLE(of, cortex_arm64_edac_of_match);

static struct platform_driver cortex_arm64_edac_driver = {
	.probe = cortex_arm64_edac_probe,
	.remove = cortex_arm64_edac_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = cortex_arm64_edac_of_match,
	},
};
module_platform_driver(cortex_arm64_edac_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Brijesh Singh <brijeshkumar.singh@amd.com>");
MODULE_DESCRIPTION("Cortex A57 and A53 EDAC driver");
module_param(poll_msec, int, 0444);
MODULE_PARM_DESC(poll_msec, "EDAC monitor poll interval in msec");
