// SPDX-License-Identifier: GPL-2.0+
/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016-2018 Xilinx
 *
 * Based on drivers/clk/zynq/clkc.c
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/zynqmp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>

#define MAX_PARENT			100
#define MAX_NODES			6
#define MAX_NAME_LEN			50
#define MAX_CLOCK			300

#define CLK_INIT_ENABLE_SHIFT		1
#define CLK_TYPE_SHIFT			2

#define PM_API_PAYLOAD_LEN		3

#define NA_PARENT			-1
#define DUMMY_PARENT			-2

#define CLK_TYPE_FIELD_LEN		4
#define CLK_TOPOLOGY_NODE_OFFSET	16
#define NODES_PER_RESP			3

#define CLK_TYPE_FIELD_MASK		0xF
#define CLK_FLAG_FIELD_SHIFT		8
#define CLK_FLAG_FIELD_MASK		0x3FFF
#define CLK_TYPE_FLAG_FIELD_SHIFT	24
#define CLK_TYPE_FLAG_FIELD_MASK	0xFF

#define CLK_PARENTS_ID_LEN		16
#define CLK_PARENTS_ID_MASK		0xFFFF

/* Flags for parents */
#define PARENT_CLK_SELF			0
#define PARENT_CLK_NODE1		1
#define PARENT_CLK_NODE2		2
#define PARENT_CLK_NODE3		3
#define PARENT_CLK_NODE4		4
#define PARENT_CLK_EXTERNAL		5

#define END_OF_CLK_NAME			"END_OF_CLK"
#define RESERVED_CLK_NAME		""

#define CLK_VALID_MASK			0x1
#define CLK_INIT_ENABLE_MASK		(0x1 << CLK_INIT_ENABLE_SHIFT)

enum clk_type {
	CLK_TYPE_OUTPUT,
	CLK_TYPE_EXTERNAL,
};

/**
 * struct clock_parent - Structure for parent of clock
 * @name:	Parent name
 * @id:		Parent clock ID
 * @flag:	Parent flags
 */
struct clock_parent {
	char name[MAX_NAME_LEN];
	int id;
	u32 flag;
};

/**
 * struct clock_topology - Structure for topology of clock
 * @type:	Type of topology
 * @flag:	Topology flags
 * @type_flag:	Topology type specific flag
 */
struct clock_topology {
	u32 type;
	u32 flag;
	u32 type_flag;
};

/**
 * struct zynqmp_clock - Structure for clock
 * @clk_name:		Clock name
 * @valid:		Validity flag of clock
 * @init_enable:	init_enable flag of clock
 * @type:		Clock type (Output/External)
 * @node:		Clock tolology nodes
 * @num_nodes:		Number of nodes present in topology
 * @parent:		structure of parent of clock
 * @num_parents:	Number of parents of clock
 */
struct zynqmp_clock {
	char clk_name[MAX_NAME_LEN];
	u32 valid;
	u32 init_enable;
	enum clk_type type;
	struct clock_topology node[MAX_NODES];
	u32 num_nodes;
	struct clock_parent parent[MAX_PARENT];
	u32 num_parents;
};

static const char clk_type_postfix[][10] = {
	[TYPE_INVALID] = "",
	[TYPE_MUX] = "_mux",
	[TYPE_GATE] = "",
	[TYPE_DIV1] = "_div1",
	[TYPE_DIV2] = "_div2",
	[TYPE_FIXEDFACTOR] = "_ff",
	[TYPE_PLL] = ""
};

static struct zynqmp_clock clock[MAX_CLOCK];
static struct clk_onecell_data zynqmp_clk_data;
static struct clk *zynqmp_clks[MAX_CLOCK];
static unsigned int clock_max_idx;
static const struct zynqmp_eemi_ops *eemi_ops;

/**
 * is_valid_clock() - Check whether clock is valid or not
 * @clk_id:	Clock index.
 * @valid:	1: if clock is valid.
 *		0: invalid clock.
 *
 * Return: 0 on success else error code.
 */
static int is_valid_clock(u32 clk_id, u32 *valid)
{
	if (clk_id < 0 || clk_id > clock_max_idx)
		return -ENODEV;

	*valid = clock[clk_id].valid;

	return *valid ? 0 : -EINVAL;
}

/**
 * zynqmp_get_clock_name() - Get name of clock from Clock index
 * @clk_id:	Clock index.
 * @clk_name:	Name of clock.
 *
 * Return: 0 on success else error code.
 */
static int zynqmp_get_clock_name(u32 clk_id, char *clk_name)
{
	int ret;
	u32 valid;

	ret = is_valid_clock(clk_id, &valid);
	if (!ret && valid) {
		strncpy(clk_name, clock[clk_id].clk_name, MAX_NAME_LEN);
		return 0;
	} else {
		return ret;
	}
}

/**
 * get_clock_type() - Get type of clock
 * @clk_id:	Clock index.
 * @type:	Clock type: CLK_TYPE_OUTPUT or CLK_TYPE_EXTERNAL.
 *
 * Return: 0 on success else error code.
 */
static int get_clock_type(u32 clk_id, u32 *type)
{
	int ret;
	u32 valid;

	ret = is_valid_clock(clk_id, &valid);
	if (!ret && valid) {
		*type = clock[clk_id].type;
		return 0;
	} else {
		return ret;
	}
}

/**
 * zynqmp_pm_clock_get_name() - Get the name of clock for given id
 * @clock_id:	ID of the clock to be queried.
 * @name:	Name of given clock.
 *
 * This function is used to get name of clock specified by given
 * clock ID.
 *
 * Return: Returns 0, in case of error name would be 0.
 */
static int zynqmp_pm_clock_get_name(u32 clock_id, char *name)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];

	qdata.qid = PM_QID_CLOCK_GET_NAME;
	qdata.arg1 = clock_id;

	eemi_ops->query_data(qdata, ret_payload);
	memcpy(name, ret_payload, CLK_GET_NAME_RESP_LEN);

	return 0;
}

/**
 * zynqmp_pm_clock_get_topology() - Get the topology of clock for given id
 * @clock_id:	ID of the clock to be queried.
 * @index:	Node index of clock topology.
 * @topology:	Buffer to store nodes in topology and flags.
 *
 * This function is used to get topology information for the clock
 * specified by given clock ID.
 *
 * This API will return 3 node of topology with a single response. To get
 * other nodes, master should call same API in loop with new
 * index till error is returned. E.g First call should have
 * index 0 which will return nodes 0,1 and 2. Next call, index
 * should be 3 which will return nodes 3,4 and 5 and so on.
 *
 * Return: Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_get_topology(u32 clock_id, u32 index, u32 *topology)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_TOPOLOGY;
	qdata.arg1 = clock_id;
	qdata.arg2 = index;

	ret = eemi_ops->query_data(qdata, ret_payload);
	memcpy(topology, &ret_payload[1], CLK_GET_TOPOLOGY_RESP_WORDS * 4);

	return ret;
}

/**
 * zynqmp_pm_clock_get_fixedfactor_params() - Get clock's fixed factor params
 * @clock_id:	Clock ID.
 * @mul:	Multiplication value.
 * @div:	Divisor value.
 *
 * This function is used to get fixed factor parameers for the fixed
 * clock. This API is application only for the fixed clock.
 *
 * Return: Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_get_fixedfactor_params(u32 clock_id,
						  u32 *mul,
						  u32 *div)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_FIXEDFACTOR_PARAMS;
	qdata.arg1 = clock_id;

	ret = eemi_ops->query_data(qdata, ret_payload);
	*mul = ret_payload[1];
	*div = ret_payload[2];

	return ret;
}

/**
 * zynqmp_pm_clock_get_parents() - Get the first 3 parents of clock for given id
 * @clock_id:	Clock ID.
 * @index:	Parent index.
 * @parents:	3 parents of the given clock.
 *
 * This function is used to get 3 parents for the clock specified by
 * given clock ID.
 *
 * This API will return 3 parents with a single response. To get
 * other parents, master should call same API in loop with new
 * parent index till error is returned. E.g First call should have
 * index 0 which will return parents 0,1 and 2. Next call, index
 * should be 3 which will return parent 3,4 and 5 and so on.
 *
 * Return: Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_get_parents(u32 clock_id, u32 index, u32 *parents)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_PARENTS;
	qdata.arg1 = clock_id;
	qdata.arg2 = index;

	ret = eemi_ops->query_data(qdata, ret_payload);
	memcpy(parents, &ret_payload[1], CLK_GET_PARENTS_RESP_WORDS * 4);

	return ret;
}

/**
 * zynqmp_pm_clock_get_attributes() - Get the attributes of clock for given id
 * @clock_id:	Clock ID.
 * @attr:	Clock attributes.
 *
 * This function is used to get clock's attributes(e.g. valid, clock type, etc).
 *
 * Return: Returns status, either success or error+reason.
 */
static int zynqmp_pm_clock_get_attributes(u32 clock_id, u32 *attr)
{
	struct zynqmp_pm_query_data qdata = {0};
	u32 ret_payload[PAYLOAD_ARG_CNT];
	int ret;

	qdata.qid = PM_QID_CLOCK_GET_ATTRIBUTES;
	qdata.arg1 = clock_id;

	ret = eemi_ops->query_data(qdata, ret_payload);
	memcpy(attr, &ret_payload[1], CLK_GET_ATTR_RESP_WORDS * 4);

	return ret;
}

/**
 * clock_get_topology() - Get topology of clock from firmware using PM_API
 * @clk_id:	Clock index.
 * @clk_topology: Structure of clock topology.
 * @num_nodes: number of nodes.
 *
 * Return: Returns status, either success or error+reason.
 */
static int clock_get_topology(u32 clk_id, struct clock_topology *clk_topology,
			      u32 *num_nodes)
{
	int j, k = 0, ret;
	u32 pm_resp[PM_API_PAYLOAD_LEN] = {0};

	*num_nodes = 0;
	for (j = 0; j <= MAX_NODES; j += 3) {
		ret = zynqmp_pm_clock_get_topology(clk_id, j, pm_resp);
		if (ret)
			return ret;
		for (k = 0; k < PM_API_PAYLOAD_LEN; k++) {
			if (!(pm_resp[k] & CLK_TYPE_FIELD_MASK))
				goto done;
			clk_topology[*num_nodes].type = pm_resp[k] &
							CLK_TYPE_FIELD_MASK;
			clk_topology[*num_nodes].flag =
					(pm_resp[k] >> CLK_FLAG_FIELD_SHIFT) &
					CLK_FLAG_FIELD_MASK;
			clk_topology[*num_nodes].type_flag =
				(pm_resp[k] >> CLK_TYPE_FLAG_FIELD_SHIFT) &
				CLK_TYPE_FLAG_FIELD_MASK;
			(*num_nodes)++;
		}
	}
done:
	return 0;
}

/**
 * clock_get_parents() - Get parents info from firmware using PM_API
 * @clk_id:		Clock index.
 * @parents:		Structure of parent information.
 * @num_parents:	Total number of parents.
 *
 * Return: Returns status, either success or error+reason.
 */
static int clock_get_parents(u32 clk_id, struct clock_parent *parents,
			     u32 *num_parents)
{
	int j = 0, k, ret, total_parents = 0;
	u32 pm_resp[PM_API_PAYLOAD_LEN] = {0};

	do {
		/* Get parents from firmware */
		ret = zynqmp_pm_clock_get_parents(clk_id, j, pm_resp);
		if (ret)
			return ret;

		for (k = 0; k < PM_API_PAYLOAD_LEN; k++) {
			if (pm_resp[k] == (u32)NA_PARENT) {
				*num_parents = total_parents;
				return 0;
			}

			parents[k + j].id = pm_resp[k] & CLK_PARENTS_ID_MASK;
			if (pm_resp[k] == (u32)DUMMY_PARENT) {
				strncpy(parents[k + j].name,
					"dummy_name", MAX_NAME_LEN);
				parents[k + j].flag = 0;
			} else {
				parents[k + j].flag = pm_resp[k] >>
							CLK_PARENTS_ID_LEN;
				if (zynqmp_get_clock_name(parents[k + j].id,
							  parents[k + j].name))
					continue;
			}
			total_parents++;
		}
		j += PM_API_PAYLOAD_LEN;
	} while (total_parents <= MAX_PARENT);
	return 0;
}

/**
 * get_parent_list() - Create list of parents name
 * @np:			Device node.
 * @clk_id:		Clock index.
 * @parent_list:	List of parent's name.
 * @num_parents:	Total number of parents.
 *
 * Return: Returns status, either success or error+reason.
 */
static int get_parent_list(struct device_node *np, u32 clk_id,
			   const char **parent_list, u32 *num_parents)
{
	int i = 0, ret;
	u32 total_parents = clock[clk_id].num_parents;
	struct clock_topology *clk_nodes;
	struct clock_parent *parents;

	clk_nodes = clock[clk_id].node;
	parents = clock[clk_id].parent;

	for (i = 0; i < total_parents; i++) {
		if (!parents[i].flag) {
			parent_list[i] = parents[i].name;
		} else if (parents[i].flag == PARENT_CLK_EXTERNAL) {
			ret = of_property_match_string(np, "clock-names",
						       parents[i].name);
			if (ret < 0)
				strncpy(parents[i].name,
					"dummy_name", MAX_NAME_LEN);
			parent_list[i] = parents[i].name;
		} else {
			strcat(parents[i].name,
			       clk_type_postfix[clk_nodes[parents[i].flag - 1].
			       type]);
			parent_list[i] = parents[i].name;
		}
	}

	*num_parents = total_parents;
	return 0;
}

/**
 * zynqmp_register_clk_topology() - Register clock topology
 * @clk_id:		Clock index.
 * @clk_name:		Clock Name.
 * @num_parents:	Total number of parents.
 * @parent_names:	List of parents name.
 *
 * Return: Returns status, either success or error+reason.
 */
static struct clk *zynqmp_register_clk_topology(int clk_id, char *clk_name,
						int num_parents,
						const char **parent_names)
{
	int j, ret;
	u32 num_nodes, mult, div;
	char *clk_out = NULL;
	struct clock_topology *nodes;
	struct clk *clk = NULL;

	nodes = clock[clk_id].node;
	num_nodes = clock[clk_id].num_nodes;

	for (j = 0; j < num_nodes; j++) {
		if (j != (num_nodes - 1)) {
			clk_out = kasprintf(GFP_KERNEL, "%s%s", clk_name,
					    clk_type_postfix[nodes[j].type]);
		} else {
			clk_out = kasprintf(GFP_KERNEL, "%s", clk_name);
		}

		switch (nodes[j].type) {
		case TYPE_MUX:
			clk = zynqmp_clk_register_mux(NULL, clk_out,
						      clk_id, parent_names,
						      num_parents,
						      nodes[j].flag,
						      nodes[j].type_flag);
			break;
		case TYPE_PLL:
			clk = clk_register_zynqmp_pll(clk_out, clk_id,
						      parent_names, 1,
						      nodes[j].flag);
			break;
		case TYPE_FIXEDFACTOR:
			ret = zynqmp_pm_clock_get_fixedfactor_params(clk_id,
								     &mult,
								     &div);
			clk = clk_register_fixed_factor(NULL, clk_out,
							parent_names[0],
							nodes[j].flag, mult,
							div);
			break;
		case TYPE_DIV1:
		case TYPE_DIV2:
			clk = zynqmp_clk_register_divider(NULL, clk_out, clk_id,
							  nodes[j].type,
							  parent_names, 1,
							  nodes[j].flag,
							  nodes[j].type_flag);
			break;
		case TYPE_GATE:
			clk = zynqmp_clk_register_gate(NULL, clk_out, clk_id,
						       parent_names, 1,
						       nodes[j].flag,
						       nodes[j].type_flag);
			break;
		default:
			pr_err("%s() Unknown topology for %s\n",
			       __func__, clk_out);
			break;
		}
		if (IS_ERR(clk))
			pr_warn_once("%s() %s register fail with %ld\n",
				     __func__, clk_name, PTR_ERR(clk));

		parent_names[0] = clk_out;
	}
	kfree(clk_out);
	return clk;
}

/**
 * zynqmp_register_clocks() - Register clocks
 * @np:		Device node.
 *
 * Return: 0 on success else error code
 */
static int zynqmp_register_clocks(struct device_node *np)
{
	int ret;
	u32 i, total_parents = 0, type = 0;
	const char *parent_names[MAX_PARENT];

	for (i = 0; i < clock_max_idx; i++) {
		char clk_name[MAX_NAME_LEN];

		/* get clock name, continue to next clock if name not found */
		if (zynqmp_get_clock_name(i, clk_name))
			continue;

		/* Check if clock is valid and output clock.
		 * Do not regiter invalid or external clock.
		 */
		ret = get_clock_type(i, &type);
		if (ret || type != CLK_TYPE_OUTPUT)
			continue;

		/* Get parents of clock*/
		if (get_parent_list(np, i, parent_names, &total_parents)) {
			WARN_ONCE(1, "No parents found for %s\n",
				  clock[i].clk_name);
			continue;
		}

		zynqmp_clks[i] = zynqmp_register_clk_topology(i, clk_name,
							      total_parents,
							      parent_names);

		/* Enable clock if init_enable flag is 1 */
		if (clock[i].init_enable)
			clk_prepare_enable(zynqmp_clks[i]);
	}

	for (i = 0; i < clock_max_idx; i++) {
		if (IS_ERR(zynqmp_clks[i])) {
			pr_err("Zynq Ultrascale+ MPSoC clk %s: register failed with %ld\n",
			       clock[i].clk_name, PTR_ERR(zynqmp_clks[i]));
			WARN_ON(1);
		}
	}
	return 0;
}

/**
 * zynqmp_get_clock_info() - Get clock information from firmware using PM_API
 */
static void zynqmp_get_clock_info(void)
{
	int i, ret;
	u32 attr, type = 0;

	memset(clock, 0, sizeof(clock));
	for (i = 0; i < MAX_CLOCK; i++) {
		zynqmp_pm_clock_get_name(i, clock[i].clk_name);
		if (!strncmp(clock[i].clk_name, END_OF_CLK_NAME,
			     MAX_NAME_LEN)) {
			clock_max_idx = i;
			break;
		} else if (!strncmp(clock[i].clk_name, RESERVED_CLK_NAME,
				    MAX_NAME_LEN)) {
			continue;
		}

		ret = zynqmp_pm_clock_get_attributes(i, &attr);
		if (ret)
			continue;

		clock[i].valid = attr & CLK_VALID_MASK;
		clock[i].init_enable = !!(attr & CLK_INIT_ENABLE_MASK);
		clock[i].type = attr >> CLK_TYPE_SHIFT ? CLK_TYPE_EXTERNAL :
							CLK_TYPE_OUTPUT;
	}

	/* Get topology of all clock */
	for (i = 0; i < clock_max_idx; i++) {
		ret = get_clock_type(i, &type);
		if (ret || type != CLK_TYPE_OUTPUT)
			continue;

		ret = clock_get_topology(i, clock[i].node, &clock[i].num_nodes);
		if (ret)
			continue;

		ret = clock_get_parents(i, clock[i].parent,
					&clock[i].num_parents);
		if (ret)
			continue;
	}
}

/**
 * zynqmp_clk_setup() - Setup the clock framework and register clocks
 * @np:		Device node
 */
static void __init zynqmp_clk_setup(struct device_node *np)
{
	int idx;

	idx = of_property_match_string(np, "clock-names", "pss_ref_clk");
	if (idx < 0) {
		pr_err("pss_ref_clk not provided\n");
		return;
	}
	idx = of_property_match_string(np, "clock-names", "video_clk");
	if (idx < 0) {
		pr_err("video_clk not provided\n");
		return;
	}
	idx = of_property_match_string(np, "clock-names", "pss_alt_ref_clk");
	if (idx < 0) {
		pr_err("pss_alt_ref_clk not provided\n");
		return;
	}
	idx = of_property_match_string(np, "clock-names", "aux_ref_clk");
	if (idx < 0) {
		pr_err("aux_ref_clk not provided\n");
		return;
	}
	idx = of_property_match_string(np, "clock-names", "gt_crx_ref_clk");
	if (idx < 0) {
		pr_err("aux_ref_clk not provided\n");
		return;
	}

	zynqmp_get_clock_info();
	zynqmp_register_clocks(np);

	zynqmp_clk_data.clks = zynqmp_clks;
	zynqmp_clk_data.clk_num = clock_max_idx;
	of_clk_add_provider(np, of_clk_src_onecell_get, &zynqmp_clk_data);
}

/**
 * zynqmp_clock_init() - Initialize zynqmp clocks
 *
 * Return: 0 always
 */
static int __init zynqmp_clock_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp");
	if (!np)
		return 0;
	of_node_put(np);

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp-clkc");
	if (np)
		panic("%s: %s binding is deprecated, please use new DT binding\n",
		       __func__, np->name);

	np = of_find_compatible_node(NULL, NULL, "xlnx,zynqmp-clk");
	if (!np) {
		pr_err("%s: clk node not found\n", __func__);
		of_node_put(np);
		return 0;
	}

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (!eemi_ops || !eemi_ops->query_data) {
		pr_err("%s: clk data not found\n", __func__);
		of_node_put(np);
		return 0;
	}

	zynqmp_clk_setup(np);

	return 0;
}
arch_initcall(zynqmp_clock_init);
