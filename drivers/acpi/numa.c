/*
 *  acpi_numa.c - ACPI NUMA support
 *
 *  Copyright (C) 2002 Takayoshi Kochi <t-kochi@bq.jp.nec.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/numa.h>
#include <linux/nodemask.h>
#include <linux/topology.h>

static nodemask_t nodes_found_map = NODE_MASK_NONE;

/* maps to convert between proximity domain and logical node ID */
static int pxm_to_node_map[MAX_PXM_DOMAINS]
			= { [0 ... MAX_PXM_DOMAINS - 1] = NUMA_NO_NODE };
static int node_to_pxm_map[MAX_NUMNODES]
			= { [0 ... MAX_NUMNODES - 1] = PXM_INVAL };

unsigned char acpi_srat_revision __initdata;
int acpi_numa __initdata;

int pxm_to_node(int pxm)
{
	if (pxm < 0)
		return NUMA_NO_NODE;
	return pxm_to_node_map[pxm];
}

int node_to_pxm(int node)
{
	if (node < 0)
		return PXM_INVAL;
	return node_to_pxm_map[node];
}

static void __acpi_map_pxm_to_node(int pxm, int node)
{
	if (pxm_to_node_map[pxm] == NUMA_NO_NODE || node < pxm_to_node_map[pxm])
		pxm_to_node_map[pxm] = node;
	if (node_to_pxm_map[node] == PXM_INVAL || pxm < node_to_pxm_map[node])
		node_to_pxm_map[node] = pxm;
}

int acpi_map_pxm_to_node(int pxm)
{
	int node;

	if (pxm < 0 || pxm >= MAX_PXM_DOMAINS)
		return NUMA_NO_NODE;

	node = pxm_to_node_map[pxm];

	if (node == NUMA_NO_NODE) {
		if (nodes_weight(nodes_found_map) >= MAX_NUMNODES)
			return NUMA_NO_NODE;
		node = first_unset_node(nodes_found_map);
		__acpi_map_pxm_to_node(pxm, node);
		node_set(node, nodes_found_map);
	}

	return node;
}

/**
 * acpi_map_pxm_to_online_node - Map proximity ID to online node
 * @pxm: ACPI proximity ID
 *
 * This is similar to acpi_map_pxm_to_node(), but always returns an online
 * node.  When the mapped node from a given proximity ID is offline, it
 * looks up the node distance table and returns the nearest online node.
 *
 * ACPI device drivers, which are called after the NUMA initialization has
 * completed in the kernel, can call this interface to obtain their device
 * NUMA topology from ACPI tables.  Such drivers do not have to deal with
 * offline nodes.  A node may be offline when a device proximity ID is
 * unique, SRAT memory entry does not exist, or NUMA is disabled, ex.
 * "numa=off" on x86.
 */
int acpi_map_pxm_to_online_node(int pxm)
{
	int node, n, dist, min_dist;

	node = acpi_map_pxm_to_node(pxm);

	if (node == NUMA_NO_NODE)
		node = 0;

	if (!node_online(node)) {
		min_dist = INT_MAX;
		for_each_online_node(n) {
			dist = node_distance(node, n);
			if (dist < min_dist) {
				min_dist = dist;
				node = n;
			}
		}
	}

	return node;
}
EXPORT_SYMBOL(acpi_map_pxm_to_online_node);

static void __init
acpi_table_print_srat_entry(struct acpi_subtable_header *header)
{
	switch (header->type) {
	case ACPI_SRAT_TYPE_CPU_AFFINITY:
		{
			struct acpi_srat_cpu_affinity *p =
			    (struct acpi_srat_cpu_affinity *)header;
			pr_debug("SRAT Processor (id[0x%02x] eid[0x%02x]) in proximity domain %d %s\n",
				 p->apic_id, p->local_sapic_eid,
				 p->proximity_domain_lo,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
		{
			struct acpi_srat_mem_affinity *p =
			    (struct acpi_srat_mem_affinity *)header;
			pr_debug("SRAT Memory (0x%lx length 0x%lx) in proximity domain %d %s%s%s\n",
				 (unsigned long)p->base_address,
				 (unsigned long)p->length,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_MEM_ENABLED) ?
				 "enabled" : "disabled",
				 (p->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) ?
				 " hot-pluggable" : "",
				 (p->flags & ACPI_SRAT_MEM_NON_VOLATILE) ?
				 " non-volatile" : "");
		}
		break;

	case ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY:
		{
			struct acpi_srat_x2apic_cpu_affinity *p =
			    (struct acpi_srat_x2apic_cpu_affinity *)header;
			pr_debug("SRAT Processor (x2apicid[0x%08x]) in proximity domain %d %s\n",
				 p->apic_id,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_CPU_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	case ACPI_SRAT_TYPE_GICC_AFFINITY:
		{
			struct acpi_srat_gicc_affinity *p =
			    (struct acpi_srat_gicc_affinity *)header;
			pr_debug("SRAT Processor (acpi id[0x%04x]) in proximity domain %d %s\n",
				 p->acpi_processor_uid,
				 p->proximity_domain,
				 (p->flags & ACPI_SRAT_GICC_ENABLED) ?
				 "enabled" : "disabled");
		}
		break;

	default:
		pr_warn("Found unsupported SRAT entry (type = 0x%x)\n",
			header->type);
		break;
	}
}

/*
 * A lot of BIOS fill in 10 (= no distance) everywhere. This messes
 * up the NUMA heuristics which wants the local node to have a smaller
 * distance than the others.
 * Do some quick checks here and only use the SLIT if it passes.
 */
static int __init slit_valid(struct acpi_table_slit *slit)
{
	int i, j;
	int d = slit->locality_count;
	for (i = 0; i < d; i++) {
		for (j = 0; j < d; j++)  {
			u8 val = slit->entry[d*i + j];
			if (i == j) {
				if (val != LOCAL_DISTANCE)
					return 0;
			} else if (val <= LOCAL_DISTANCE)
				return 0;
		}
	}
	return 1;
}

void __init bad_srat(void)
{
	pr_err("SRAT: SRAT not used.\n");
	acpi_numa = -1;
}

int __init srat_disabled(void)
{
	return acpi_numa < 0;
}

#if defined(CONFIG_X86) || defined(CONFIG_ARM64)
/*
 * Callback for SLIT parsing.  pxm_to_node() returns NUMA_NO_NODE for
 * I/O localities since SRAT does not list them.  I/O localities are
 * not supported at this point.
 */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	for (i = 0; i < slit->locality_count; i++) {
		const int from_node = pxm_to_node(i);

		if (from_node == NUMA_NO_NODE)
			continue;

		for (j = 0; j < slit->locality_count; j++) {
			const int to_node = pxm_to_node(j);

			if (to_node == NUMA_NO_NODE)
				continue;

			numa_set_distance(from_node, to_node,
				slit->entry[slit->locality_count * i + j]);
		}
	}
}

/*
 * Default callback for parsing of the Proximity Domain <-> Memory
 * Area mappings
 */
int __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	u64 start, end;
	u32 hotpluggable;
	int node, pxm;

	if (srat_disabled())
		goto out_err;
	if (ma->header.length < sizeof(struct acpi_srat_mem_affinity)) {
		pr_err("SRAT: Unexpected header length: %d\n",
		       ma->header.length);
		goto out_err_bad_srat;
	}
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		goto out_err;
	hotpluggable = ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE;
	if (hotpluggable && !IS_ENABLED(CONFIG_MEMORY_HOTPLUG))
		goto out_err;

	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	if (acpi_srat_revision <= 1)
		pxm &= 0xff;

	node = acpi_map_pxm_to_node(pxm);
	if (node == NUMA_NO_NODE || node >= MAX_NUMNODES) {
		pr_err("SRAT: Too many proximity domains.\n");
		goto out_err_bad_srat;
	}

	if (numa_add_memblk(node, start, end) < 0) {
		pr_err("SRAT: Failed to add memblk to node %u [mem %#010Lx-%#010Lx]\n",
		       node, (unsigned long long) start,
		       (unsigned long long) end - 1);
		goto out_err_bad_srat;
	}

	node_set(node, numa_nodes_parsed);

	pr_info("SRAT: Node %u PXM %u [mem %#010Lx-%#010Lx]%s%s\n",
		node, pxm,
		(unsigned long long) start, (unsigned long long) end - 1,
		hotpluggable ? " hotplug" : "",
		ma->flags & ACPI_SRAT_MEM_NON_VOLATILE ? " non-volatile" : "");

	/* Mark hotplug range in memblock. */
	if (hotpluggable && memblock_mark_hotplug(start, ma->length))
		pr_warn("SRAT: Failed to mark hotplug range [mem %#010Lx-%#010Lx] in memblock\n",
			(unsigned long long)start, (unsigned long long)end - 1);

	max_possible_pfn = max(max_possible_pfn, PFN_UP(end - 1));

	return 0;
out_err_bad_srat:
	bad_srat();
out_err:
	return -EINVAL;
}
#endif /* defined(CONFIG_X86) || defined (CONFIG_ARM64) */

static int __init acpi_parse_slit(struct acpi_table_header *table)
{
	struct acpi_table_slit *slit = (struct acpi_table_slit *)table;

	if (!slit_valid(slit)) {
		pr_info("SLIT table looks invalid. Not used.\n");
		return -EINVAL;
	}
	acpi_numa_slit_init(slit);

	return 0;
}

void __init __weak
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	pr_warn("Found unsupported x2apic [0x%08x] SRAT entry\n", pa->apic_id);
}

static int __init
acpi_parse_x2apic_affinity(struct acpi_subtable_header *header,
			   const unsigned long end)
{
	struct acpi_srat_x2apic_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_x2apic_cpu_affinity *)header;
	if (!processor_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	acpi_numa_x2apic_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_processor_affinity(struct acpi_subtable_header *header,
			      const unsigned long end)
{
	struct acpi_srat_cpu_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_cpu_affinity *)header;
	if (!processor_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	acpi_numa_processor_affinity_init(processor_affinity);

	return 0;
}

static int __init
acpi_parse_gicc_affinity(struct acpi_subtable_header *header,
			 const unsigned long end)
{
	struct acpi_srat_gicc_affinity *processor_affinity;

	processor_affinity = (struct acpi_srat_gicc_affinity *)header;
	if (!processor_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	acpi_numa_gicc_affinity_init(processor_affinity);

	return 0;
}

static int __initdata parsed_numa_memblks;

static int __init
acpi_parse_memory_affinity(struct acpi_subtable_header * header,
			   const unsigned long end)
{
	struct acpi_srat_mem_affinity *memory_affinity;

	memory_affinity = (struct acpi_srat_mem_affinity *)header;
	if (!memory_affinity)
		return -EINVAL;

	acpi_table_print_srat_entry(header);

	/* let architecture-dependent part to do it */
	if (!acpi_numa_memory_affinity_init(memory_affinity))
		parsed_numa_memblks++;
	return 0;
}

static int __init acpi_parse_srat(struct acpi_table_header *table)
{
	struct acpi_table_srat *srat = (struct acpi_table_srat *)table;

	acpi_srat_revision = srat->header.revision;

	/* Real work done in acpi_table_parse_srat below. */

	return 0;
}

static int __init
acpi_table_parse_srat(enum acpi_srat_type id,
		      acpi_tbl_entry_handler handler, unsigned int max_entries)
{
	return acpi_table_parse_entries(ACPI_SIG_SRAT,
					    sizeof(struct acpi_table_srat), id,
					    handler, max_entries);
}

int __init acpi_numa_init(void)
{
	int cnt = 0;

	if (acpi_disabled)
		return -EINVAL;

	/*
	 * Should not limit number with cpu num that is from NR_CPUS or nr_cpus=
	 * SRAT cpu entries could have different order with that in MADT.
	 * So go over all cpu entries in SRAT to get apicid to node mapping.
	 */

	/* SRAT: Static Resource Affinity Table */
	if (!acpi_table_parse(ACPI_SIG_SRAT, acpi_parse_srat)) {
		struct acpi_subtable_proc srat_proc[3];

		memset(srat_proc, 0, sizeof(srat_proc));
		srat_proc[0].id = ACPI_SRAT_TYPE_CPU_AFFINITY;
		srat_proc[0].handler = acpi_parse_processor_affinity;
		srat_proc[1].id = ACPI_SRAT_TYPE_X2APIC_CPU_AFFINITY;
		srat_proc[1].handler = acpi_parse_x2apic_affinity;
		srat_proc[2].id = ACPI_SRAT_TYPE_GICC_AFFINITY;
		srat_proc[2].handler = acpi_parse_gicc_affinity;

		acpi_table_parse_entries_array(ACPI_SIG_SRAT,
					sizeof(struct acpi_table_srat),
					srat_proc, ARRAY_SIZE(srat_proc), 0);

		cnt = acpi_table_parse_srat(ACPI_SRAT_TYPE_MEMORY_AFFINITY,
					    acpi_parse_memory_affinity,
					    NR_NODE_MEMBLKS);
	}

	/* SLIT: System Locality Information Table */
	acpi_table_parse(ACPI_SIG_SLIT, acpi_parse_slit);

	if (cnt < 0)
		return cnt;
	else if (!parsed_numa_memblks)
		return -ENOENT;
	return 0;
}

static int acpi_get_pxm(acpi_handle h)
{
	unsigned long long pxm;
	acpi_status status;
	acpi_handle handle;
	acpi_handle phandle = h;

	do {
		handle = phandle;
		status = acpi_evaluate_integer(handle, "_PXM", NULL, &pxm);
		if (ACPI_SUCCESS(status))
			return pxm;
		status = acpi_get_parent(handle, &phandle);
	} while (ACPI_SUCCESS(status));
	return -1;
}

int acpi_get_node(acpi_handle handle)
{
	int pxm;

	pxm = acpi_get_pxm(handle);

	return acpi_map_pxm_to_node(pxm);
}
EXPORT_SYMBOL(acpi_get_node);
