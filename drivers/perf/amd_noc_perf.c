// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for AMD NOC Performance Monitoring Unit
 *
 * Copyright (C) 2026 Advanced Micro Devices, Inc.
 *
 */

#include <linux/cpuhotplug.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#define DRIVER_NAME "amd_noc_pmu"

#define MAX_NMU_COMPONENTS	58
#define MAX_DDRMC_NSU_COMPONENTS	16
#define MAX_NOC_COMPONENTS	(MAX_NMU_COMPONENTS + MAX_DDRMC_NSU_COMPONENTS)

enum counters {
	CNT0_LATENCY = 1,
	CNT0_BURST   = 2,
	CNT0_BYTE    = 3,
	CNT1_LATENCY = 4,
	CNT1_BURST   = 5,
	CNT1_BYTE    = 6,
	CNT_MAX,
};

enum comp_types {
	COMP_TYPE_NMU = 1,
	COMP_TYPE_DDRMC_NSU0 = 2,
	COMP_TYPE_DDRMC_NSU1 = 3,
	COMP_TYPE_DDRMC_NSU2 = 4,
	COMP_TYPE_DDRMC_NSU3 = 5,
	COMP_TYPE_MAX,
};

#define COUNTER_MASK		GENMASK(7, 0)
#define COMP_TYPE_MASK		GENMASK(15, 8)
#define COMP_TYPE_SHIFT		8
#define COMP_ID_MASK		GENMASK(23, 16)
#define COMP_ID_SHIFT		16

#define HW_INDEX_MASK		GENMASK(31, 0)

/* NMU config1[16:0] Filter configuration */
#define NMU_AXI_ID_FLT		BIT(0)
#define NMU_AXLEN_FLT		BIT(1)
#define NMU_AXSIZE_FLT		BIT(2)
#define NMU_AXBURST_FLT		BIT(3)
#define NMU_AXPROT_FLT		BIT(4)
#define NMU_AXCACHE_FLT		BIT(5)
#define NMU_AXQOS_FLT		BIT(6)
#define NMU_AXLOCK_FLT		BIT(7)
#define NMU_DEST_ID_FLT		BIT(8)
#define NMU_TDEST_FLT		BIT(9)

/* DDRMC-NSU config1[16:0] Filter configuration */
#define NSU_AXI_ID_FLT		BIT(0)
#define NSU_AXLEN_FLT		BIT(1)
#define NSU_AXBURST_FLT		BIT(2)
#define NSU_AXPROT_FLT		BIT(3)
#define NSU_AXLOCK_FLT		BIT(4)
#define NSU_SRC_ID_FLT		BIT(5)

#define NMU_FLT_MASK		GENMASK(9, 0)
#define NSU_FLT_MASK		GENMASK(5, 0)
#define CONFIG1_MASK		GENMASK(63, 32)
#define CONFIG1_SHIFT		32

/* NMU config1[63:32] */
#define NMU_AX_ID_FLT_MASK	GENMASK(47, 32)
#define NMU_AXI_ID_FLT_SHIFT	32
#define NMU_AXLENMIN_FLT_MASK	GENMASK(55, 48)
#define NMU_AXLENMIN_FLT_SHIFT	48
#define NMU_AXSZMIN_FLT_MASK	GENMASK(58, 56)
#define NMU_AXSZMIN_FLT_SHIFT	56
#define NMU_AXBURST_FLT_MASK	GENMASK(60, 59)
#define NMU_AXBURST_FLT_SHIFT	59
#define NMU_AXPROT_FLT_MASK	GENMASK(63, 61)
#define NMU_AXPROT_FLT_SHIFT	61

/* NSU config1[61:32] */
#define NSU_AX_ID_FLT_MASK	GENMASK(47, 32)
#define NSU_AXI_ID_FLT_SHIFT	32
#define NSU_AXLENMIN_FLT_MASK	GENMASK(51, 48)
#define NSU_AXLENMIN_FLT_SHIFT	48
#define NSU_AXLENMAX_FLT_MASK	GENMASK(55, 52)
#define NSU_AXLENMAX_FLT_SHIFT	52
#define NSU_AXBURST_FLT_MASK	GENMASK(57, 56)
#define NSU_AXBURST_FLT_SHIFT	56
#define NSU_AXPROT_FLT_MASK	GENMASK(60, 58)
#define NSU_AXPROT_FLT_SHIFT	58
#define NSU_AXLOCK_FLT_MASK	BIT(61)
#define NSU_AXLOCK_FLT_SHIFT	61

/* NMU config2[63:0] */
#define NMU_AXCACHE_FLT_MASK	GENMASK(3, 0)
#define NMU_AXCACHE_FLT_SHIFT	0
#define NMU_AXQOSMIN_FLT_MASK	GENMASK(7, 4)
#define NMU_AXQOSMIN_FLT_SHIFT	4
#define NMU_AXLOCK_FLT_MASK	BIT(8)
#define NMU_AXLOCK_FLT_SHIFT	8
#define NMU_DEST_ID_FLT_MASK	GENMASK(20, 9)
#define NMU_DEST_ID_FLT_SHIFT	9
#define NMU_TDEST_FLT_MASK	GENMASK(30, 21)
#define NMU_TDEST_FLT_SHIFT	21
#define NMU_AXLENMAX_FLT_MASK	GENMASK(39, 32)
#define NMU_AXLENMAX_FLT_SHIFT	32
#define NMU_AXQOSMAX_FLT_MASK	GENMASK(43, 40)
#define NMU_AXQOSMAX_FLT_SHIFT	40
#define NMU_AXSZMAX_FLT_MASK	GENMASK(46, 44)
#define NMU_AXSZMAX_FLT_SHIFT	44

/* NSU config2[63:0] */
#define NSU_SRC_ID_FLT_MASK	GENMASK(11, 0)
#define NSU_SRC_ID_FLT_SHIFT	0

#define NSU0_PERF_MON_BASE	0x4D8
#define NSU1_PERF_MON_BASE	0x518
#define NSU2_PERF_MON_BASE	0x558
#define NSU3_PERF_MON_BASE	0x598

/* Register offsets */
#define PCSR_LOCK_OFFSET		0xC
#define PCSR_UNLOCK_CODE		0xF9E8D7C6
#define PCSR_LOCK_CODE			0x0
#define NMU_CNT0_FILT0_OFFSET		0x8B0
#define NMU_CNT1_FILT0_OFFSET		0x8C0
#define NMU_CNT0_MON_CTRL_OFFSET	0x88C
#define NMU_CNT1_MON_CTRL_OFFSET	0x8AC

#define NMU_CNT0_LATENCY_UPPER_OFFSET	0x878
#define NMU_CNT0_LATENCY_LOWER_OFFSET	0x87C
#define NMU_CNT0_BURST_OFFSET		0x880
#define NMU_CNT0_BYTE_UPPER_OFFSET	0x884
#define NMU_CNT0_BYTE_LOWER_OFFSET	0x888
#define NMU_CNT1_LATENCY_UPPER_OFFSET	0x898
#define NMU_CNT1_LATENCY_LOWER_OFFSET	0x89C
#define NMU_CNT1_BURST_OFFSET		0x8A0
#define NMU_CNT1_BYTE_UPPER_OFFSET	0x8A4
#define NMU_CNT1_BYTE_LOWER_OFFSET	0x8A8

#define NMU_FILT1_OFFSET		0x4
#define NMU_FILT2_OFFSET		0x8
#define NMU_FILTEN_OFFSET		0xC

#define NSU_CNT0_MON0_CTRL_OFFSET	0x0
#define NSU_CNT0_MON1_CTRL_OFFSET	0x4
#define NSU_CNT0_FILT0_OFFSET		0x8
#define NSU_CNT0_LATENCY_OFFSET		0x14
#define NSU_CNT0_BURST_OFFSET		0x18
#define NSU_CNT0_BYTE_OFFSET		0x1C
#define NSU_CNT1_MON0_CTRL_OFFSET	0x20
#define NSU_CNT1_MON1_CTRL_OFFSET	0x24
#define NSU_CNT1_FILT0_OFFSET		0x28
#define NSU_CNT1_LATENCY_OFFSET		0x34
#define NSU_CNT1_BURST_OFFSET		0x38
#define NSU_CNT1_BYTE_OFFSET		0x3C

#define PERF_MON_ENABLE			BIT(0)
#define NSU_MON_CONTINUOUS_MODE		BIT(1)
#define NSU_MON_CTRL_READ_TRAFFIC	0x30
#define NSU_MON_CTRL_WRITE_TRAFFIC	0xC0

#define NSU_FILT1_OFFSET		0x4
#define NSU_FILTEN_OFFSET		0x8

#define NUM_COUNTERS	6
#define DDRMC_MAX_NSU_PORTS		4
#define DDRMC_MAX_NSU_PORT_ID		3

/**
 * struct noc_component - NOC hardware component descriptor
 * @base: Virtual address from fwnode_iomap
 * @label: Optional label from DT for event naming
 * @id: Component ID (NMU_3, DDRMC_0, etc)
 * @type: Component type (COMP_TYPE_NMU, COMP_TYPE_DDRMC_NSU0-3)
 * @port: NSU port number for DDRMC components
 */
struct noc_component {
	void __iomem *base;
	const char *label;
	u32 id;
	u8 type;
	u8 port;
};

enum amd_noc_attr_groups {
	AMD_NOC_ATTR_GROUP_FORMAT,
	AMD_NOC_ATTR_GROUP_EVENTS,
	AMD_NOC_ATTR_GROUP_CPUMASK,
	AMD_NOC_NR_ATTR_GROUPS
};

/**
 * struct amd_noc_pmu - Per-instance PMU data
 * @pmu: Perf PMU structure
 * @node: Hotplug node for CPU migration
 * @components: Array of all registered NoC components
 * @num_components: Total number of components in this instance
 * @events_group: Sysfs attribute group for events
 * @event_attrs: Dynamically created event attributes
 * @attr_groups: PMU attribute groups array (NULL-terminated)
 * @cpu: CPU to which this PMU is bound
 * @dev: Device pointer
 */
struct amd_noc_pmu {
	struct pmu pmu;
	struct hlist_node node;
	struct noc_component components[MAX_NOC_COMPONENTS];
	int num_components;
	struct attribute_group events_group;
	struct attribute **event_attrs;
	const struct attribute_group *attr_groups[AMD_NOC_NR_ATTR_GROUPS + 1];
	unsigned int cpu;
	struct device *dev;
};

static inline struct amd_noc_pmu *to_amd_noc_pmu(struct pmu *pmu)
{
	return container_of(pmu, struct amd_noc_pmu, pmu);
}

static void __iomem *get_component_address(struct amd_noc_pmu *instance,
					   u8 comp_type, u32 comp_id)
{
	u32 i;

	for (i = 0; i < instance->num_components; i++)
		if (instance->components[i].type == comp_type &&
		    instance->components[i].id == comp_id)
			return instance->components[i].base;

	return NULL;
}

static ssize_t amd_noc_pmu_event_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(buf, "event=0x%llx\n", pmu_attr->id);
}

static struct attribute *create_event_attr(const char *name, u64 config_value)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = kzalloc(sizeof(*pmu_attr), GFP_KERNEL);
	if (!pmu_attr)
		return NULL;

	pmu_attr->attr.attr.name = kstrdup(name, GFP_KERNEL);
	if (!pmu_attr->attr.attr.name) {
		kfree(pmu_attr);
		return NULL;
	}

	pmu_attr->attr.attr.mode = 0444;
	pmu_attr->attr.show = amd_noc_pmu_event_show;
	pmu_attr->id = config_value;

	return &pmu_attr->attr.attr;
}

static void free_event_attrs(struct attribute **attrs)
{
	struct perf_pmu_events_attr *pmu_attr;
	int i;

	if (!attrs)
		return;

	for (i = 0; attrs[i]; i++) {
		pmu_attr = container_of(attrs[i], struct perf_pmu_events_attr,
					attr.attr);
		kfree(pmu_attr->attr.attr.name);
		kfree(pmu_attr);
	}

	kfree(attrs);
}

static u64 encode_config(u8 comp_type, u32 comp_id, u8 counter)
{
	return (counter & COUNTER_MASK) |
	       ((comp_type << COMP_TYPE_SHIFT) & COMP_TYPE_MASK) |
	       ((comp_id << COMP_ID_SHIFT) & COMP_ID_MASK);
}

static inline int add_event_attr(struct amd_noc_pmu *instance, int idx,
				 const char *name, u64 config)
{
	instance->event_attrs[idx] = create_event_attr(name, config);
	if (!instance->event_attrs[idx])
		return -ENOMEM;

	return 0;
}

static const struct {
	enum counters counter;
	const char *suffix;
} noc_counter_descs[] = {
	{ CNT0_LATENCY, "read_latency" },
	{ CNT0_BURST,   "read_burst" },
	{ CNT0_BYTE,    "read_byte" },
	{ CNT1_LATENCY, "write_latency" },
	{ CNT1_BURST,   "write_burst" },
	{ CNT1_BYTE,    "write_byte" },
};

static int create_dynamic_events(struct amd_noc_pmu *instance)
{
	int total_events, idx = 0, ret;
	char name[64];
	u64 config;
	u32 i, j;

	total_events = instance->num_components * NUM_COUNTERS + 1;

	instance->event_attrs = kcalloc(total_events, sizeof(struct attribute *), GFP_KERNEL);
	if (!instance->event_attrs)
		return -ENOMEM;

	for (i = 0; i < instance->num_components; i++) {
		struct noc_component *comp = &instance->components[i];

		for (j = 0; j < ARRAY_SIZE(noc_counter_descs); j++) {
			if (comp->type == COMP_TYPE_NMU)
				snprintf(name, sizeof(name), "%s_%d_%s",
					 comp->label ?: "nmu", comp->id,
					 noc_counter_descs[j].suffix);
			else
				snprintf(name, sizeof(name), "%s_%d_nsu%d_%s",
					 comp->label ?: "ddrmc", comp->id,
					 comp->port,
					 noc_counter_descs[j].suffix);

			config = encode_config(comp->type, comp->id,
					       noc_counter_descs[j].counter);
			ret = add_event_attr(instance, idx++, name, config);
			if (ret)
				goto free_attrs;
		}
	}

	instance->event_attrs[idx] = NULL;
	instance->events_group.name = "events";
	instance->events_group.attrs = instance->event_attrs;

	return 0;

free_attrs:
	free_event_attrs(instance->event_attrs);
	instance->event_attrs = NULL;

	return -ENOMEM;
}

static int configure_nmu_filters(struct perf_event *event, u8 counter)
{
	void __iomem *compaddr, *filteraddr;
	u64 config1, config2;
	u32 filter_en;

	compaddr = (void __iomem *)event->hw.config_base;
	if (counter == CNT0_LATENCY || counter == CNT0_BYTE || counter == CNT0_BURST)
		filteraddr = compaddr + NMU_CNT0_FILT0_OFFSET;
	else if (counter == CNT1_LATENCY || counter == CNT1_BYTE || counter == CNT1_BURST)
		filteraddr = compaddr + NMU_CNT1_FILT0_OFFSET;
	else
		return -EINVAL;

	config1 = event->attr.config1;
	config2 = event->attr.config2;

	writel(PCSR_UNLOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
	filter_en = config1 & NMU_FLT_MASK;
	if (filter_en) {
		writel((u32)(config1 >> CONFIG1_SHIFT), filteraddr);
		writel((u32)config2, filteraddr + NMU_FILT1_OFFSET);
		writel((u32)(config2 >> 32) & GENMASK(15, 0), filteraddr + NMU_FILT2_OFFSET);
	}

	writel(filter_en, filteraddr + NMU_FILTEN_OFFSET);
	writel(PCSR_LOCK_CODE, compaddr + PCSR_LOCK_OFFSET);

	return 0;
}

static inline int get_nsu_offset(u8 comptype)
{
	switch (comptype) {
	case COMP_TYPE_DDRMC_NSU0:
		return NSU0_PERF_MON_BASE;
	case COMP_TYPE_DDRMC_NSU1:
		return NSU1_PERF_MON_BASE;
	case COMP_TYPE_DDRMC_NSU2:
		return NSU2_PERF_MON_BASE;
	case COMP_TYPE_DDRMC_NSU3:
		return NSU3_PERF_MON_BASE;
	default:
		return -EINVAL;
	}
}

static int configure_ddrmc_nsu_filters(struct perf_event *event, u8 counter, u8 comptype)
{
	void __iomem *compaddr, *filteraddr, *nsuaddr;
	u64 config1, config2;
	u32 filter_en;
	int offset;

	offset = get_nsu_offset(comptype);
	if (offset < 0)
		return offset;

	compaddr = (void __iomem *)event->hw.config_base;
	nsuaddr = compaddr + offset;

	config1 = event->attr.config1;
	config2 = event->attr.config2;

	writel(PCSR_UNLOCK_CODE, compaddr + PCSR_LOCK_OFFSET);

	if (counter == CNT0_LATENCY || counter == CNT0_BYTE || counter == CNT0_BURST) {
		filteraddr = nsuaddr + NSU_CNT0_FILT0_OFFSET;
		writel(NSU_MON_CONTINUOUS_MODE, nsuaddr + NSU_CNT0_MON0_CTRL_OFFSET);
		writel(NSU_MON_CTRL_READ_TRAFFIC, nsuaddr + NSU_CNT0_MON1_CTRL_OFFSET);
	} else if (counter == CNT1_LATENCY || counter == CNT1_BYTE || counter == CNT1_BURST) {
		filteraddr = nsuaddr + NSU_CNT1_FILT0_OFFSET;
		writel(NSU_MON_CONTINUOUS_MODE, nsuaddr + NSU_CNT1_MON0_CTRL_OFFSET);
		writel(NSU_MON_CTRL_WRITE_TRAFFIC, nsuaddr + NSU_CNT1_MON1_CTRL_OFFSET);
	} else {
		writel(PCSR_LOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
		return -EINVAL;
	}
	filter_en = config1 & NSU_FLT_MASK;
	if (filter_en) {
		writel((u32)(config1 >> CONFIG1_SHIFT), filteraddr);
		writel(config2 & NSU_SRC_ID_FLT_MASK, filteraddr + NSU_FILT1_OFFSET);
	}

	writel(filter_en, filteraddr + NSU_FILTEN_OFFSET);
	writel(PCSR_LOCK_CODE, compaddr + PCSR_LOCK_OFFSET);

	return 0;
}

static void configure_nmu_event(struct hw_perf_event *hw, u8 counter, u8 enable)
{
	void __iomem *compaddr;
	u32 offset, reg;

	compaddr = (void __iomem *)hw->config_base;
	if (counter == CNT0_LATENCY || counter == CNT0_BYTE || counter == CNT0_BURST)
		offset = NMU_CNT0_MON_CTRL_OFFSET;
	else
		offset = NMU_CNT1_MON_CTRL_OFFSET;

	reg = readl(compaddr + offset);
	if (enable)
		reg |= PERF_MON_ENABLE;
	else
		reg &= ~PERF_MON_ENABLE;

	writel(PCSR_UNLOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
	writel(reg, compaddr + offset);
	writel(PCSR_LOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
}

static void configure_nsu_event(struct hw_perf_event *hw, u8 counter, u8 enable, u8 comptype)
{
	void __iomem *compaddr, *nsuaddr;
	u32 ctrl_offset, val = 0;
	int offset;

	offset = get_nsu_offset(comptype);
	if (offset < 0)
		return;

	compaddr = (void __iomem *)hw->config_base;
	nsuaddr = compaddr + offset;

	if (enable)
		val = PERF_MON_ENABLE | NSU_MON_CONTINUOUS_MODE;

	if (counter == CNT0_LATENCY || counter == CNT0_BYTE || counter == CNT0_BURST)
		ctrl_offset = NSU_CNT0_MON0_CTRL_OFFSET;
	else
		ctrl_offset = NSU_CNT1_MON0_CTRL_OFFSET;

	writel(PCSR_UNLOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
	writel(val, nsuaddr + ctrl_offset);
	writel(PCSR_LOCK_CODE, compaddr + PCSR_LOCK_OFFSET);
}

static u64 read_nmu_counter(struct hw_perf_event *hw, u8 counter)
{
	u32 offset1 = 0, offset2 = 0;
	void __iomem *compaddr;
	u64 val;

	compaddr = (void __iomem *)hw->config_base;
	switch (counter) {
	case CNT0_LATENCY:
		offset1 = NMU_CNT0_LATENCY_LOWER_OFFSET;
		offset2 = NMU_CNT0_LATENCY_UPPER_OFFSET;
		break;
	case CNT0_BYTE:
		offset1 = NMU_CNT0_BYTE_LOWER_OFFSET;
		offset2 = NMU_CNT0_BYTE_UPPER_OFFSET;
		break;
	case CNT0_BURST:
		offset1 = NMU_CNT0_BURST_OFFSET;
		break;
	case CNT1_LATENCY:
		offset1 = NMU_CNT1_LATENCY_LOWER_OFFSET;
		offset2 = NMU_CNT1_LATENCY_UPPER_OFFSET;
		break;
	case CNT1_BYTE:
		offset1 = NMU_CNT1_BYTE_LOWER_OFFSET;
		offset2 = NMU_CNT1_BYTE_UPPER_OFFSET;
		break;
	case CNT1_BURST:
		offset1 = NMU_CNT1_BURST_OFFSET;
		break;
	}

	val = readl(compaddr + offset1);
	if (offset2)
		val |= ((u64)readl(compaddr + offset2) << 32);

	return val;
}

static u64 read_ddrmc_nsu_counter(struct hw_perf_event *hw, u8 counter, u8 comptype)
{
	void __iomem *compaddr, *nsuaddr;
	u32 offset = 0;
	int nsuoffset;

	nsuoffset = get_nsu_offset(comptype);
	if (nsuoffset < 0)
		return 0;

	compaddr = (void __iomem *)hw->config_base;
	nsuaddr = compaddr + nsuoffset;

	switch (counter) {
	case CNT0_LATENCY:
		offset = NSU_CNT0_LATENCY_OFFSET;
		break;
	case CNT0_BYTE:
		offset = NSU_CNT0_BYTE_OFFSET;
		break;
	case CNT0_BURST:
		offset = NSU_CNT0_BURST_OFFSET;
		break;
	case CNT1_LATENCY:
		offset = NSU_CNT1_LATENCY_OFFSET;
		break;
	case CNT1_BYTE:
		offset = NSU_CNT1_BYTE_OFFSET;
		break;
	case CNT1_BURST:
		offset = NSU_CNT1_BURST_OFFSET;
		break;
	}

	return readl(nsuaddr + offset);
}

static void amd_noc_pmu_event_read(struct perf_event *event)
{
	u64 prev_count, new_count, delta;
	u32 counter, comptype;

	comptype = (event->hw.config & COMP_TYPE_MASK) >> COMP_TYPE_SHIFT;
	counter = event->hw.config & COUNTER_MASK;

	if (comptype == COMP_TYPE_NMU)
		new_count = read_nmu_counter(&event->hw, counter);
	else
		new_count = read_ddrmc_nsu_counter(&event->hw, counter, comptype);

	prev_count = local64_read(&event->hw.prev_count);
	delta = new_count - prev_count;
	local64_add(delta, &event->count);
	local64_set(&event->hw.prev_count, new_count);
	event->hw.state |= PERF_HES_UPTODATE;
}

static void amd_noc_pmu_event_start(struct perf_event *event, int flags)
{
	u32 counter, comptype;
	u64 count;

	counter = event->hw.config & COUNTER_MASK;
	comptype = (event->hw.config & COMP_TYPE_MASK) >> COMP_TYPE_SHIFT;

	if (event == event->group_leader) {
		if (comptype == COMP_TYPE_NMU)
			configure_nmu_event(&event->hw, counter, 1);
		else
			configure_nsu_event(&event->hw, counter, 1, comptype);
	}

	if (comptype == COMP_TYPE_NMU)
		count = read_nmu_counter(&event->hw, counter);
	else
		count = read_ddrmc_nsu_counter(&event->hw, counter, comptype);

	local64_set(&event->hw.prev_count, count);
	event->hw.state = 0;
}

static void amd_noc_pmu_event_stop(struct perf_event *event, int flags)
{
	u32 counter, comptype;

	if (event->hw.state & PERF_HES_STOPPED)
		return;

	counter = event->hw.config & COUNTER_MASK;
	comptype = (event->hw.config & COMP_TYPE_MASK) >> COMP_TYPE_SHIFT;

	if (event == event->group_leader) {
		if (comptype == COMP_TYPE_NMU)
			configure_nmu_event(&event->hw, counter, 0);
		else
			configure_nsu_event(&event->hw, counter, 0, comptype);
	}

	if (flags & PERF_EF_UPDATE)
		amd_noc_pmu_event_read(event);

	event->hw.state |= PERF_HES_STOPPED;
}

static int amd_noc_pmu_event_init(struct perf_event *event)
{
	struct amd_noc_pmu *instance = to_amd_noc_pmu(event->pmu);
	u64 config = event->attr.config;
	void __iomem *addr = NULL;
	u8 counter, comptype;
	u32 comp_id;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	counter = config & COUNTER_MASK;
	comptype = (config & COMP_TYPE_MASK) >> COMP_TYPE_SHIFT;
	comp_id = (config & COMP_ID_MASK) >> COMP_ID_SHIFT;

	if (counter < CNT0_LATENCY || counter >= CNT_MAX)
		return -EINVAL;

	if (comptype == COMP_TYPE_NMU) {
		addr = get_component_address(instance, COMP_TYPE_NMU, comp_id);
		if (!addr) {
			dev_dbg(instance->dev, "NMU_%u not found in %s\n",
				comp_id, instance->pmu.name);
			return -EINVAL;
		}
	} else if (comptype >= COMP_TYPE_DDRMC_NSU0 &&
		   comptype <= COMP_TYPE_DDRMC_NSU3) {
		u8 nsuport = comptype - COMP_TYPE_DDRMC_NSU0;

		addr = get_component_address(instance, comptype, comp_id);
		if (!addr) {
			dev_dbg(instance->dev, "DDRMC_%u_NSU%u not found in %s\n",
				comp_id, nsuport, instance->pmu.name);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	event->hw.config_base = (unsigned long)addr;

	return 0;
}

static int amd_noc_pmu_event_add(struct perf_event *event, int flags)
{
	u8 counter, comptype;
	int ret = 0;

	if (!event->hw.config_base)
		return -EINVAL;

	counter = event->attr.config & COUNTER_MASK;
	comptype = (event->attr.config & COMP_TYPE_MASK) >> COMP_TYPE_SHIFT;

	event->hw.config = event->attr.config;
	event->hw.idx = event->attr.config & HW_INDEX_MASK;
	event->hw.state = PERF_HES_STOPPED;

	if (event == event->group_leader) {
		if (comptype == COMP_TYPE_NMU)
			ret = configure_nmu_filters(event, counter);
		else
			ret = configure_ddrmc_nsu_filters(event, counter, comptype);

		if (ret)
			return ret;
	}

	if (flags & PERF_EF_START)
		amd_noc_pmu_event_start(event, flags);

	return 0;
}

static void amd_noc_pmu_event_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->idx < 0)
		return;

	if (!(hwc->state & PERF_HES_STOPPED))
		amd_noc_pmu_event_stop(event, PERF_EF_UPDATE);

	hwc->idx = -1;
	hwc->state = PERF_HES_STOPPED;
}

/* PMU format attributes */
PMU_FORMAT_ATTR(event, "config:0-23");
PMU_FORMAT_ATTR(counter, "config:0-7");
PMU_FORMAT_ATTR(component, "config:8-15");
PMU_FORMAT_ATTR(comp_id, "config:16-23");

static struct attribute *amd_noc_pmu_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_counter.attr,
	&format_attr_component.attr,
	&format_attr_comp_id.attr,
	NULL,
};

static const struct attribute_group amd_noc_pmu_format_group = {
	.name = "format",
	.attrs = amd_noc_pmu_format_attrs,
};

static ssize_t amd_noc_pmu_cpumask_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct amd_noc_pmu *noc_pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(noc_pmu->cpu));
}

static struct device_attribute amd_noc_pmu_cpumask_attr =
	__ATTR(cpumask, 0444, amd_noc_pmu_cpumask_show, NULL);

static struct attribute *amd_noc_pmu_cpumask_attrs[] = {
	&amd_noc_pmu_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group amd_noc_pmu_cpumask_group = {
	.attrs = amd_noc_pmu_cpumask_attrs,
};

static int amd_noc_pmu_cpuhp_state;

static void amd_noc_iounmap(void *data)
{
	iounmap(data);
}

static int parse_nmu_node(struct device *dev, struct amd_noc_pmu *instance,
			  struct fwnode_handle *child)
{
	const char *label = NULL;
	void __iomem *base;
	u32 nmu_id;
	int ret;

	if (instance->num_components >= MAX_NOC_COMPONENTS) {
		dev_err(dev, "Too many NoC components\n");
		return -ENOSPC;
	}

	if (fwnode_property_read_u32(child, "amd,nmu-id", &nmu_id))
		return -EINVAL;

	base = fwnode_iomap(child, 0);
	if (!base)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, amd_noc_iounmap, base);
	if (ret)
		return ret;

	fwnode_property_read_string(child, "label", &label);

	instance->components[instance->num_components].id = nmu_id;
	instance->components[instance->num_components].base = base;
	instance->components[instance->num_components].type = COMP_TYPE_NMU;
	instance->components[instance->num_components].label = label;
	instance->num_components++;

	return 0;
}

static int parse_ddrmc_node(struct device *dev, struct amd_noc_pmu *instance,
			    struct fwnode_handle *child)
{
	u32 ports[DDRMC_MAX_NSU_PORTS];
	const char *label = NULL;
	void __iomem *base;
	int ret, num_ports;
	u32 ddrmc_id, i;

	if (fwnode_property_read_u32(child, "amd,ddrmc-id", &ddrmc_id))
		return -EINVAL;

	num_ports = fwnode_property_count_u32(child, "amd,nsu-ports");
	if (num_ports <= 0 || num_ports > DDRMC_MAX_NSU_PORTS)
		return -EINVAL;

	base = fwnode_iomap(child, 0);
	if (!base)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, amd_noc_iounmap, base);
	if (ret)
		return ret;

	fwnode_property_read_string(child, "label", &label);

	ret = fwnode_property_read_u32_array(child, "amd,nsu-ports", ports, num_ports);
	if (ret)
		return ret;

	for (i = 0; i < num_ports; i++) {
		if (instance->num_components >= MAX_NOC_COMPONENTS) {
			dev_err(dev, "Too many NoC components\n");
			return -ENOSPC;
		}

		if (ports[i] > DDRMC_MAX_NSU_PORT_ID) {
			dev_err(dev, "Invalid port %d for DDRMC_%d\n", ports[i], ddrmc_id);
			return -EINVAL;
		}

		instance->components[instance->num_components].id = ddrmc_id;
		instance->components[instance->num_components].base = base;
		instance->components[instance->num_components].type =
				COMP_TYPE_DDRMC_NSU0 + ports[i];
		instance->components[instance->num_components].port = ports[i];
		instance->components[instance->num_components].label = label;
		instance->num_components++;
	}

	return 0;
}

static int amd_noc_pmu_probe(struct platform_device *pdev)
{
	struct amd_noc_pmu *instance;
	const char *node_name;
	int ret;

	instance = devm_kzalloc(&pdev->dev, sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return -ENOMEM;

	instance->dev = &pdev->dev;
	instance->pmu.name = dev_name(&pdev->dev);

	device_for_each_child_node_scoped(&pdev->dev, child) {
		node_name = fwnode_get_name(child);
		if (str_has_prefix(node_name, "nmu")) {
			ret = parse_nmu_node(&pdev->dev, instance, child);
		} else if (str_has_prefix(node_name, "ddrmc-nsu")) {
			ret = parse_ddrmc_node(&pdev->dev, instance, child);
		} else {
			dev_err(&pdev->dev, "Invalid child node name: %s\n", node_name);
			ret = -EINVAL;
		}

		if (ret)
			return ret;
	}

	if (instance->num_components == 0) {
		dev_err(&pdev->dev, "No NoC components found\n");
		return -ENODEV;
	}

	instance->pmu.module = THIS_MODULE;
	instance->pmu.task_ctx_nr = perf_invalid_context;
	instance->pmu.event_init = amd_noc_pmu_event_init;
	instance->pmu.add = amd_noc_pmu_event_add;
	instance->pmu.del = amd_noc_pmu_event_del;
	instance->pmu.start = amd_noc_pmu_event_start;
	instance->pmu.stop = amd_noc_pmu_event_stop;
	instance->pmu.read = amd_noc_pmu_event_read;
	instance->pmu.capabilities = PERF_PMU_CAP_NO_INTERRUPT | PERF_PMU_CAP_NO_EXCLUDE;

	ret = create_dynamic_events(instance);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create events: %d\n", ret);
		return ret;
	}

	instance->attr_groups[AMD_NOC_ATTR_GROUP_FORMAT] = &amd_noc_pmu_format_group;
	instance->attr_groups[AMD_NOC_ATTR_GROUP_EVENTS] = &instance->events_group;
	instance->attr_groups[AMD_NOC_ATTR_GROUP_CPUMASK] = &amd_noc_pmu_cpumask_group;
	instance->attr_groups[AMD_NOC_NR_ATTR_GROUPS] = NULL;
	instance->pmu.attr_groups = instance->attr_groups;

	instance->cpu = raw_smp_processor_id();
	platform_set_drvdata(pdev, instance);

	ret = cpuhp_state_add_instance_nocalls(amd_noc_pmu_cpuhp_state,
					       &instance->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		goto err_free_attrs;
	}

	ret = perf_pmu_register(&instance->pmu, instance->pmu.name, -1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register PMU: %d\n", ret);
		goto err_remove_cpuhp;
	}

	dev_dbg(&pdev->dev, "AMD NoC PMU %s: %d components\n",
		instance->pmu.name, instance->num_components);

	return 0;

err_remove_cpuhp:
	cpuhp_state_remove_instance_nocalls(amd_noc_pmu_cpuhp_state,
					    &instance->node);
err_free_attrs:
	free_event_attrs(instance->event_attrs);
	instance->event_attrs = NULL;

	return ret;
}

static void amd_noc_pmu_remove(struct platform_device *pdev)
{
	struct amd_noc_pmu *instance = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(amd_noc_pmu_cpuhp_state,
					    &instance->node);
	perf_pmu_unregister(&instance->pmu);

	if (instance->event_attrs) {
		free_event_attrs(instance->event_attrs);
		instance->event_attrs = NULL;
	}
}

static const struct of_device_id amd_noc_pmu_of_match[] = {
	{ .compatible = "amd,noc-pmu" },
	{}
};
MODULE_DEVICE_TABLE(of, amd_noc_pmu_of_match);

static struct platform_driver amd_noc_pmu_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = amd_noc_pmu_of_match,
	},
	.probe = amd_noc_pmu_probe,
	.remove = amd_noc_pmu_remove,
};

static int amd_noc_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct amd_noc_pmu *pmu = hlist_entry_safe(node, struct amd_noc_pmu,
						   node);
	unsigned int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;

	return 0;
}

static int __init amd_noc_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/amd/noc:online", NULL,
				      amd_noc_pmu_offline_cpu);
	if (ret < 0)
		return ret;

	amd_noc_pmu_cpuhp_state = ret;

	ret = platform_driver_register(&amd_noc_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(amd_noc_pmu_cpuhp_state);

	return ret;
}

static void __exit amd_noc_pmu_exit(void)
{
	platform_driver_unregister(&amd_noc_pmu_driver);
	cpuhp_remove_multi_state(amd_noc_pmu_cpuhp_state);
}

module_init(amd_noc_pmu_init);
module_exit(amd_noc_pmu_exit);

MODULE_AUTHOR("Sai Krishna Potthuri <sai.krishna.potthuri@amd.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PMU driver for AMD NoC");
