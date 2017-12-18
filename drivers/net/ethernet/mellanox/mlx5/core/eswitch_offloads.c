/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/etherdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "mlx5_core.h"
#include "eswitch.h"

enum {
	FDB_FAST_PATH = 0,
	FDB_SLOW_PATH
};

struct mlx5_flow_rule *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_esw_flow_attr *attr)
{
	struct mlx5_flow_destination dest = { 0 };
	struct mlx5_fc *counter = NULL;
	struct mlx5_flow_rule *rule;
	void *misc;
	int action;

	if (esw->mode != SRIOV_OFFLOADS)
		return ERR_PTR(-EOPNOTSUPP);

	/* per flow vlan pop/push is emulated, don't set that into the firmware */
	action = attr->action & ~(MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH | MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);

	if (action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
		dest.vport_num = attr->out_rep->vport;
		action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	} else if (action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_create(esw->dev, true);
		if (IS_ERR(counter))
			return ERR_CAST(counter);
		dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
		dest.counter = counter;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port, attr->in_rep->vport);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS |
				      MLX5_MATCH_MISC_PARAMETERS;

	rule = mlx5_add_flow_rule((struct mlx5_flow_table *)esw->fdb_table.fdb,
				  spec, action, 0, &dest);

	if (IS_ERR(rule))
		mlx5_fc_destroy(esw->dev, counter);

	return rule;
}

static int esw_set_global_vlan_pop(struct mlx5_eswitch *esw, u8 val)
{
	struct mlx5_eswitch_rep *rep;
	int vf_vport, err = 0;

	esw_debug(esw->dev, "%s applying global %s policy\n", __func__, val ? "pop" : "none");
	for (vf_vport = 1; vf_vport < esw->enabled_vports; vf_vport++) {
		rep = &esw->offloads.vport_reps[vf_vport];
		if (!rep->valid)
			continue;

		err = __mlx5_eswitch_set_vport_vlan(esw, rep->vport, 0, 0, val);
		if (err)
			goto out;
	}

out:
	return err;
}

static struct mlx5_eswitch_rep *
esw_vlan_action_get_vport(struct mlx5_esw_flow_attr *attr, bool push, bool pop)
{
	struct mlx5_eswitch_rep *in_rep, *out_rep, *vport = NULL;

	in_rep  = attr->in_rep;
	out_rep = attr->out_rep;

	if (push)
		vport = in_rep;
	else if (pop)
		vport = out_rep;
	else
		vport = in_rep;

	return vport;
}

static int esw_add_vlan_action_check(struct mlx5_esw_flow_attr *attr,
				     bool push, bool pop, bool fwd)
{
	struct mlx5_eswitch_rep *in_rep, *out_rep;

	if ((push || pop) && !fwd)
		goto out_notsupp;

	in_rep  = attr->in_rep;
	out_rep = attr->out_rep;

	if (push && in_rep->vport == FDB_UPLINK_VPORT)
		goto out_notsupp;

	if (pop && out_rep->vport == FDB_UPLINK_VPORT)
		goto out_notsupp;

	/* vport has vlan push configured, can't offload VF --> wire rules w.o it */
	if (!push && !pop && fwd)
		if (in_rep->vlan && out_rep->vport == FDB_UPLINK_VPORT)
			goto out_notsupp;

	/* protects against (1) setting rules with different vlans to push and
	 * (2) setting rules w.o vlans (attr->vlan = 0) && w. vlans to push (!= 0)
	 */
	if (push && in_rep->vlan_refcount && (in_rep->vlan != attr->vlan))
		goto out_notsupp;

	return 0;

out_notsupp:
	return -ENOTSUPP;
}

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);

	err = esw_add_vlan_action_check(attr, push, pop, fwd);
	if (err)
		return err;

	attr->vlan_handled = false;

	vport = esw_vlan_action_get_vport(attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (attr->out_rep->vport == FDB_UPLINK_VPORT) {
			vport->vlan_refcount++;
			attr->vlan_handled = true;
		}

		return 0;
	}

	if (!push && !pop)
		return 0;

	if (!(offloads->vlan_push_pop_refcount)) {
		/* it's the 1st vlan rule, apply global vlan pop policy */
		err = esw_set_global_vlan_pop(esw, SET_VLAN_STRIP);
		if (err)
			goto out;
	}
	offloads->vlan_push_pop_refcount++;

	if (push) {
		if (vport->vlan_refcount)
			goto skip_set_push;

		err = __mlx5_eswitch_set_vport_vlan(esw, vport->vport, attr->vlan, 0,
						    SET_VLAN_INSERT | SET_VLAN_STRIP);
		if (err)
			goto out;
		vport->vlan = attr->vlan;
skip_set_push:
		vport->vlan_refcount++;
	}
out:
	if (!err)
		attr->vlan_handled = true;
	return err;
}

int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr)
{
	struct offloads_fdb *offloads = &esw->fdb_table.offloads;
	struct mlx5_eswitch_rep *vport = NULL;
	bool push, pop, fwd;
	int err = 0;

	if (!attr->vlan_handled)
		return 0;

	push = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH);
	pop  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_VLAN_POP);
	fwd  = !!(attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);

	vport = esw_vlan_action_get_vport(attr, push, pop);

	if (!push && !pop && fwd) {
		/* tracks VF --> wire rules without vlan push action */
		if (attr->out_rep->vport == FDB_UPLINK_VPORT)
			vport->vlan_refcount--;

		return 0;
	}

	if (push) {
		vport->vlan_refcount--;
		if (vport->vlan_refcount)
			goto skip_unset_push;

		vport->vlan = 0;
		err = __mlx5_eswitch_set_vport_vlan(esw, vport->vport,
						    0, 0, SET_VLAN_STRIP);
		if (err)
			goto out;
	}

skip_unset_push:
	offloads->vlan_push_pop_refcount--;
	if (offloads->vlan_push_pop_refcount)
		return 0;

	/* no more vlan rules, stop global vlan pop policy */
	err = esw_set_global_vlan_pop(esw, 0);

out:
	return err;
}

static struct mlx5_flow_rule *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *esw, int vport, u32 sqn)
{
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		esw_warn(esw->dev, "FDB: Failed to alloc match parameters\n");
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_sqn, sqn);
	MLX5_SET(fte_match_set_misc, misc, source_port, 0x0); /* source vport is 0 */

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_sqn);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = vport;

	flow_rule = mlx5_add_flow_rule(esw->fdb_table.offloads.fdb, spec,
				       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				       0, &dest);
	if (IS_ERR(flow_rule))
		esw_warn(esw->dev, "FDB: Failed to add send to vport rule err %ld\n", PTR_ERR(flow_rule));
out:
	kvfree(spec);
	return flow_rule;
}

void mlx5_eswitch_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep)
{
	struct mlx5_esw_sq *esw_sq, *tmp;

	if (esw->mode != SRIOV_OFFLOADS)
		return;

	list_for_each_entry_safe(esw_sq, tmp, &rep->vport_sqs_list, list) {
		mlx5_del_flow_rule(esw_sq->send_to_vport_rule);
		list_del(&esw_sq->list);
		kfree(esw_sq);
	}
}

int mlx5_eswitch_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u16 *sqns_array, int sqns_num)
{
	struct mlx5_flow_rule *flow_rule;
	struct mlx5_esw_sq *esw_sq;
	int err;
	int i;

	if (esw->mode != SRIOV_OFFLOADS)
		return 0;

	for (i = 0; i < sqns_num; i++) {
		esw_sq = kzalloc(sizeof(*esw_sq), GFP_KERNEL);
		if (!esw_sq) {
			err = -ENOMEM;
			goto out_err;
		}

		/* Add re-inject rule to the PF/representor sqs */
		flow_rule = mlx5_eswitch_add_send_to_vport_rule(esw,
								rep->vport,
								sqns_array[i]);
		if (IS_ERR(flow_rule)) {
			err = PTR_ERR(flow_rule);
			kfree(esw_sq);
			goto out_err;
		}
		esw_sq->send_to_vport_rule = flow_rule;
		list_add(&esw_sq->list, &rep->vport_sqs_list);
	}
	return 0;

out_err:
	mlx5_eswitch_sqs2vport_stop(esw, rep);
	return err;
}

static int esw_add_fdb_miss_rule(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule = NULL;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		esw_warn(esw->dev, "FDB: Failed to alloc match parameters\n");
		err = -ENOMEM;
		goto out;
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_VPORT;
	dest.vport_num = 0;

	flow_rule = mlx5_add_flow_rule(esw->fdb_table.offloads.fdb, spec,
				       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				       0, &dest);
	if (IS_ERR(flow_rule)) {
		err = PTR_ERR(flow_rule);
		esw_warn(esw->dev,  "FDB: Failed to add miss flow rule err %d\n", err);
		goto out;
	}

	esw->fdb_table.offloads.miss_rule = flow_rule;
out:
	kvfree(spec);
	return err;
}

#define MAX_PF_SQ 256
#define ESW_OFFLOADS_NUM_ENTRIES (1 << 13) /* 8K */
#define ESW_OFFLOADS_NUM_GROUPS  4

static int esw_create_offloads_fdb_table(struct mlx5_eswitch *esw, int nvports)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_core_dev *dev = esw->dev;
	struct mlx5_flow_namespace *root_ns;
	struct mlx5_flow_table *fdb = NULL;
	struct mlx5_flow_group *g;
	u32 *flow_group_in;
	void *match_criteria;
	int table_size, ix, err = 0;

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;

	root_ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_FDB);
	if (!root_ns) {
		esw_warn(dev, "Failed to get FDB flow namespace\n");
		goto ns_err;
	}

	esw_debug(dev, "Create offloads FDB table, log_max_size(%d)\n",
		  MLX5_CAP_ESW_FLOWTABLE_FDB(dev, log_max_ft_size));

	fdb = mlx5_create_auto_grouped_flow_table(root_ns, FDB_FAST_PATH,
						  ESW_OFFLOADS_NUM_ENTRIES,
						  ESW_OFFLOADS_NUM_GROUPS, 0);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create Fast path FDB Table err %d\n", err);
		goto fast_fdb_err;
	}
	esw->fdb_table.fdb = fdb;

	table_size = nvports + MAX_PF_SQ + 1;
	fdb = mlx5_create_flow_table(root_ns, FDB_SLOW_PATH, table_size, 0);
	if (IS_ERR(fdb)) {
		err = PTR_ERR(fdb);
		esw_warn(dev, "Failed to create slow path FDB Table err %d\n", err);
		goto slow_fdb_err;
	}
	esw->fdb_table.offloads.fdb = fdb;

	/* create send-to-vport group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);

	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_sqn);
	MLX5_SET_TO_ONES(fte_match_param, match_criteria, misc_parameters.source_port);

	ix = nvports + MAX_PF_SQ;
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ix - 1);

	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create send-to-vport flow group err(%d)\n", err);
		goto send_vport_err;
	}
	esw->fdb_table.offloads.send_to_vport_grp = g;

	/* create miss group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable, 0);

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, ix);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, ix + 1);

	g = mlx5_create_flow_group(fdb, flow_group_in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		esw_warn(dev, "Failed to create miss flow group err(%d)\n", err);
		goto miss_err;
	}
	esw->fdb_table.offloads.miss_grp = g;

	err = esw_add_fdb_miss_rule(esw);
	if (err)
		goto miss_rule_err;

	return 0;

miss_rule_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);
miss_err:
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
send_vport_err:
	mlx5_destroy_flow_table(esw->fdb_table.offloads.fdb);
slow_fdb_err:
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
fast_fdb_err:
ns_err:
	kvfree(flow_group_in);
	return err;
}

static void esw_destroy_offloads_fdb_table(struct mlx5_eswitch *esw)
{
	if (!esw->fdb_table.fdb)
		return;

	esw_debug(esw->dev, "Destroy offloads FDB Table\n");
	mlx5_del_flow_rule(esw->fdb_table.offloads.miss_rule);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.send_to_vport_grp);
	mlx5_destroy_flow_group(esw->fdb_table.offloads.miss_grp);

	mlx5_destroy_flow_table(esw->fdb_table.offloads.fdb);
	mlx5_destroy_flow_table(esw->fdb_table.fdb);
}

static int esw_create_offloads_table(struct mlx5_eswitch *esw)
{
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_core_dev *dev = esw->dev;
	int err = 0;

	ns = mlx5_get_flow_namespace(dev, MLX5_FLOW_NAMESPACE_OFFLOADS);
	if (!ns) {
		esw_warn(esw->dev, "Failed to get offloads flow namespace\n");
		return -ENOMEM;
	}

	ft_offloads = mlx5_create_flow_table(ns, 0, dev->priv.sriov.num_vfs + 2, 0);
	if (IS_ERR(ft_offloads)) {
		err = PTR_ERR(ft_offloads);
		esw_warn(esw->dev, "Failed to create offloads table, err %d\n", err);
		return err;
	}

	esw->offloads.ft_offloads = ft_offloads;
	return 0;
}

static void esw_destroy_offloads_table(struct mlx5_eswitch *esw)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;

	mlx5_destroy_flow_table(offloads->ft_offloads);
}

static int esw_create_vport_rx_group(struct mlx5_eswitch *esw)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_flow_group *g;
	struct mlx5_priv *priv = &esw->dev->priv;
	u32 *flow_group_in;
	void *match_criteria, *misc;
	int err = 0;
	int nvports = priv->sriov.num_vfs + 2;

	flow_group_in = mlx5_vzalloc(inlen);
	if (!flow_group_in)
		return -ENOMEM;

	/* create vport rx group */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, match_criteria_enable,
		 MLX5_MATCH_MISC_PARAMETERS);

	match_criteria = MLX5_ADDR_OF(create_flow_group_in, flow_group_in, match_criteria);
	misc = MLX5_ADDR_OF(fte_match_param, match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, 0);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, nvports - 1);

	g = mlx5_create_flow_group(esw->offloads.ft_offloads, flow_group_in);

	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_warn(esw->dev, "Failed to create vport rx group err %d\n", err);
		goto out;
	}

	esw->offloads.vport_rx_group = g;
out:
	kfree(flow_group_in);
	return err;
}

static void esw_destroy_vport_rx_group(struct mlx5_eswitch *esw)
{
	mlx5_destroy_flow_group(esw->offloads.vport_rx_group);
}

struct mlx5_flow_rule *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, int vport, u32 tirn)
{
	struct mlx5_flow_destination dest;
	struct mlx5_flow_rule *flow_rule;
	struct mlx5_flow_spec *spec;
	void *misc;

	spec = mlx5_vzalloc(sizeof(*spec));
	if (!spec) {
		esw_warn(esw->dev, "Failed to alloc match parameters\n");
		flow_rule = ERR_PTR(-ENOMEM);
		goto out;
	}

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_value, misc_parameters);
	MLX5_SET(fte_match_set_misc, misc, source_port, vport);

	misc = MLX5_ADDR_OF(fte_match_param, spec->match_criteria, misc_parameters);
	MLX5_SET_TO_ONES(fte_match_set_misc, misc, source_port);

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tirn;

	flow_rule = mlx5_add_flow_rule(esw->offloads.ft_offloads, spec,
				       MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				       0, &dest);
	if (IS_ERR(flow_rule)) {
		esw_warn(esw->dev, "fs offloads: Failed to add vport rx rule err %ld\n", PTR_ERR(flow_rule));
		goto out;
	}

out:
	kvfree(spec);
	return flow_rule;
}

static int esw_offloads_start(struct mlx5_eswitch *esw)
{
	int err, err1, num_vfs = esw->dev->priv.sriov.num_vfs;

	if (esw->mode != SRIOV_LEGACY) {
		esw_warn(esw->dev, "Can't set offloads mode, SRIOV legacy not enabled\n");
		return -EINVAL;
	}

	mlx5_eswitch_disable_sriov(esw);
	err = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_OFFLOADS);
	if (err) {
		esw_warn(esw->dev, "Failed setting eswitch to offloads, err %d\n", err);
		err1 = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_LEGACY);
		if (err1)
			esw_warn(esw->dev, "Failed setting eswitch back to legacy, err %d\n", err);
	}
	return err;
}

int esw_offloads_init(struct mlx5_eswitch *esw, int nvports)
{
	struct mlx5_eswitch_rep *rep;
	int vport;
	int err;

	err = esw_create_offloads_fdb_table(esw, nvports);
	if (err)
		return err;

	err = esw_create_offloads_table(esw);
	if (err)
		goto create_ft_err;

	err = esw_create_vport_rx_group(esw);
	if (err)
		goto create_fg_err;

	for (vport = 0; vport < nvports; vport++) {
		rep = &esw->offloads.vport_reps[vport];
		if (!rep->valid)
			continue;

		err = rep->load(esw, rep);
		if (err)
			goto err_reps;
	}
	return 0;

err_reps:
	for (vport--; vport >= 0; vport--) {
		rep = &esw->offloads.vport_reps[vport];
		if (!rep->valid)
			continue;
		rep->unload(esw, rep);
	}
	esw_destroy_vport_rx_group(esw);

create_fg_err:
	esw_destroy_offloads_table(esw);

create_ft_err:
	esw_destroy_offloads_fdb_table(esw);
	return err;
}

static int esw_offloads_stop(struct mlx5_eswitch *esw)
{
	int err, err1, num_vfs = esw->dev->priv.sriov.num_vfs;

	mlx5_eswitch_disable_sriov(esw);
	err = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_LEGACY);
	if (err) {
		esw_warn(esw->dev, "Failed setting eswitch to legacy, err %d\n", err);
		err1 = mlx5_eswitch_enable_sriov(esw, num_vfs, SRIOV_OFFLOADS);
		if (err1)
			esw_warn(esw->dev, "Failed setting eswitch back to offloads, err %d\n", err);
	}

	return err;
}

void esw_offloads_cleanup(struct mlx5_eswitch *esw, int nvports)
{
	struct mlx5_eswitch_rep *rep;
	int vport;

	for (vport = 0; vport < nvports; vport++) {
		rep = &esw->offloads.vport_reps[vport];
		if (!rep->valid)
			continue;
		rep->unload(esw, rep);
	}

	esw_destroy_vport_rx_group(esw);
	esw_destroy_offloads_table(esw);
	esw_destroy_offloads_fdb_table(esw);
}

static int esw_mode_from_devlink(u16 mode, u16 *mlx5_mode)
{
	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		*mlx5_mode = SRIOV_LEGACY;
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		*mlx5_mode = SRIOV_OFFLOADS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int esw_mode_to_devlink(u16 mlx5_mode, u16 *mode)
{
	switch (mlx5_mode) {
	case SRIOV_LEGACY:
		*mode = DEVLINK_ESWITCH_MODE_LEGACY;
		break;
	case SRIOV_OFFLOADS:
		*mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode)
{
	struct mlx5_core_dev *dev;
	u16 cur_mlx5_mode, mlx5_mode = 0;

	dev = devlink_priv(devlink);

	if (!MLX5_CAP_GEN(dev, vport_group_manager))
		return -EOPNOTSUPP;

	cur_mlx5_mode = dev->priv.eswitch->mode;

	if (cur_mlx5_mode == SRIOV_NONE)
		return -EOPNOTSUPP;

	if (esw_mode_from_devlink(mode, &mlx5_mode))
		return -EINVAL;

	if (cur_mlx5_mode == mlx5_mode)
		return 0;

	if (mode == DEVLINK_ESWITCH_MODE_SWITCHDEV)
		return esw_offloads_start(dev->priv.eswitch);
	else if (mode == DEVLINK_ESWITCH_MODE_LEGACY)
		return esw_offloads_stop(dev->priv.eswitch);
	else
		return -EINVAL;
}

int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct mlx5_core_dev *dev;

	dev = devlink_priv(devlink);

	if (!MLX5_CAP_GEN(dev, vport_group_manager))
		return -EOPNOTSUPP;

	if (dev->priv.eswitch->mode == SRIOV_NONE)
		return -EOPNOTSUPP;

	return esw_mode_to_devlink(dev->priv.eswitch->mode, mode);
}

void mlx5_eswitch_register_vport_rep(struct mlx5_eswitch *esw,
				     int vport_index,
				     struct mlx5_eswitch_rep *__rep)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;
	struct mlx5_eswitch_rep *rep;

	rep = &offloads->vport_reps[vport_index];

	memset(rep, 0, sizeof(*rep));

	rep->load   = __rep->load;
	rep->unload = __rep->unload;
	rep->vport  = __rep->vport;
	rep->priv_data = __rep->priv_data;
	ether_addr_copy(rep->hw_id, __rep->hw_id);

	INIT_LIST_HEAD(&rep->vport_sqs_list);
	rep->valid = true;
}

void mlx5_eswitch_unregister_vport_rep(struct mlx5_eswitch *esw,
				       int vport_index)
{
	struct mlx5_esw_offload *offloads = &esw->offloads;
	struct mlx5_eswitch_rep *rep;

	rep = &offloads->vport_reps[vport_index];

	if (esw->mode == SRIOV_OFFLOADS && esw->vports[vport_index].enabled)
		rep->unload(esw, rep);

	rep->valid = false;
}
