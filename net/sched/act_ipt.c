/*
 * net/sched/act_ipt.c		iptables target interface
 *
 *TODO: Add other tables. For now we only support the ipv4 table targets
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Copyright:	Jamal Hadi Salim (2002-13)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_ipt.h>
#include <net/tc_act/tc_ipt.h>

#include <linux/netfilter_ipv4/ip_tables.h>


#define IPT_TAB_MASK     15

static int ipt_net_id;
static struct tc_action_ops act_ipt_ops;

static int xt_net_id;
static struct tc_action_ops act_xt_ops;

static int ipt_init_target(struct xt_entry_target *t, char *table,
			   unsigned int hook)
{
	struct xt_tgchk_param par;
	struct xt_target *target;
	int ret = 0;

	target = xt_request_find_target(AF_INET, t->u.user.name,
					t->u.user.revision);
	if (IS_ERR(target))
		return PTR_ERR(target);

	t->u.kernel.target = target;
	par.table     = table;
	par.entryinfo = NULL;
	par.target    = target;
	par.targinfo  = t->data;
	par.hook_mask = hook;
	par.family    = NFPROTO_IPV4;

	ret = xt_check_target(&par, t->u.target_size - sizeof(*t), 0, false);
	if (ret < 0) {
		module_put(t->u.kernel.target->me);
		return ret;
	}
	return 0;
}

static void ipt_destroy_target(struct xt_entry_target *t)
{
	struct xt_tgdtor_param par = {
		.target   = t->u.kernel.target,
		.targinfo = t->data,
		.family   = NFPROTO_IPV4,
	};
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
}

static void tcf_ipt_release(struct tc_action *a, int bind)
{
	struct tcf_ipt *ipt = to_ipt(a);
	ipt_destroy_target(ipt->tcfi_t);
	kfree(ipt->tcfi_tname);
	kfree(ipt->tcfi_t);
}

static const struct nla_policy ipt_policy[TCA_IPT_MAX + 1] = {
	[TCA_IPT_TABLE]	= { .type = NLA_STRING, .len = IFNAMSIZ },
	[TCA_IPT_HOOK]	= { .type = NLA_U32 },
	[TCA_IPT_INDEX]	= { .type = NLA_U32 },
	[TCA_IPT_TARG]	= { .len = sizeof(struct xt_entry_target) },
};

static int __tcf_ipt_init(struct tc_action_net *tn, struct nlattr *nla,
			  struct nlattr *est, struct tc_action **a,
			  const struct tc_action_ops *ops, int ovr, int bind)
{
	struct nlattr *tb[TCA_IPT_MAX + 1];
	struct tcf_ipt *ipt;
	struct xt_entry_target *td, *t;
	char *tname;
	bool exists = false;
	int ret = 0, err;
	u32 hook = 0;
	u32 index = 0;

	if (nla == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_IPT_MAX, nla, ipt_policy);
	if (err < 0)
		return err;

	if (tb[TCA_IPT_INDEX] != NULL)
		index = nla_get_u32(tb[TCA_IPT_INDEX]);

	exists = tcf_hash_check(tn, index, a, bind);
	if (exists && bind)
		return 0;

	if (tb[TCA_IPT_HOOK] == NULL || tb[TCA_IPT_TARG] == NULL) {
		if (exists)
			tcf_hash_release(*a, bind);
		return -EINVAL;
	}

	td = (struct xt_entry_target *)nla_data(tb[TCA_IPT_TARG]);
	if (nla_len(tb[TCA_IPT_TARG]) < td->u.target_size) {
		if (exists)
			tcf_hash_release(*a, bind);
		return -EINVAL;
	}

	if (!exists) {
		ret = tcf_hash_create(tn, index, est, a, ops, bind,
				      false);
		if (ret)
			return ret;
		ret = ACT_P_CREATED;
	} else {
		if (bind)/* dont override defaults */
			return 0;
		tcf_hash_release(*a, bind);

		if (!ovr)
			return -EEXIST;
	}
	hook = nla_get_u32(tb[TCA_IPT_HOOK]);

	err = -ENOMEM;
	tname = kmalloc(IFNAMSIZ, GFP_KERNEL);
	if (unlikely(!tname))
		goto err1;
	if (tb[TCA_IPT_TABLE] == NULL ||
	    nla_strlcpy(tname, tb[TCA_IPT_TABLE], IFNAMSIZ) >= IFNAMSIZ)
		strcpy(tname, "mangle");

	t = kmemdup(td, td->u.target_size, GFP_KERNEL);
	if (unlikely(!t))
		goto err2;

	err = ipt_init_target(t, tname, hook);
	if (err < 0)
		goto err3;

	ipt = to_ipt(*a);

	spin_lock_bh(&ipt->tcf_lock);
	if (ret != ACT_P_CREATED) {
		ipt_destroy_target(ipt->tcfi_t);
		kfree(ipt->tcfi_tname);
		kfree(ipt->tcfi_t);
	}
	ipt->tcfi_tname = tname;
	ipt->tcfi_t     = t;
	ipt->tcfi_hook  = hook;
	spin_unlock_bh(&ipt->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(tn, *a);
	return ret;

err3:
	kfree(t);
err2:
	kfree(tname);
err1:
	if (ret == ACT_P_CREATED)
		tcf_hash_cleanup(*a, est);
	return err;
}

static int tcf_ipt_init(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action **a, int ovr,
			int bind)
{
	struct tc_action_net *tn = net_generic(net, ipt_net_id);

	return __tcf_ipt_init(tn, nla, est, a, &act_ipt_ops, ovr, bind);
}

static int tcf_xt_init(struct net *net, struct nlattr *nla,
		       struct nlattr *est, struct tc_action **a, int ovr,
		       int bind)
{
	struct tc_action_net *tn = net_generic(net, xt_net_id);

	return __tcf_ipt_init(tn, nla, est, a, &act_xt_ops, ovr, bind);
}

static int tcf_ipt(struct sk_buff *skb, const struct tc_action *a,
		   struct tcf_result *res)
{
	int ret = 0, result = 0;
	struct tcf_ipt *ipt = to_ipt(a);
	struct xt_action_param par;

	if (skb_unclone(skb, GFP_ATOMIC))
		return TC_ACT_UNSPEC;

	spin_lock(&ipt->tcf_lock);

	tcf_lastuse_update(&ipt->tcf_tm);
	bstats_update(&ipt->tcf_bstats, skb);

	/* yes, we have to worry about both in and out dev
	 * worry later - danger - this API seems to have changed
	 * from earlier kernels
	 */
	par.net	     = dev_net(skb->dev);
	par.in       = skb->dev;
	par.out      = NULL;
	par.hooknum  = ipt->tcfi_hook;
	par.target   = ipt->tcfi_t->u.kernel.target;
	par.targinfo = ipt->tcfi_t->data;
	par.family   = NFPROTO_IPV4;
	ret = par.target->target(skb, &par);

	switch (ret) {
	case NF_ACCEPT:
		result = TC_ACT_OK;
		break;
	case NF_DROP:
		result = TC_ACT_SHOT;
		ipt->tcf_qstats.drops++;
		break;
	case XT_CONTINUE:
		result = TC_ACT_PIPE;
		break;
	default:
		net_notice_ratelimited("tc filter: Bogus netfilter code %d assume ACCEPT\n",
				       ret);
		result = TC_ACT_OK;
		break;
	}
	spin_unlock(&ipt->tcf_lock);
	return result;

}

static int tcf_ipt_dump(struct sk_buff *skb, struct tc_action *a, int bind,
			int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_ipt *ipt = to_ipt(a);
	struct xt_entry_target *t;
	struct tcf_t tm;
	struct tc_cnt c;

	/* for simple targets kernel size == user size
	 * user name = target name
	 * for foolproof you need to not assume this
	 */

	t = kmemdup(ipt->tcfi_t, ipt->tcfi_t->u.user.target_size, GFP_ATOMIC);
	if (unlikely(!t))
		goto nla_put_failure;

	c.bindcnt = ipt->tcf_bindcnt - bind;
	c.refcnt = ipt->tcf_refcnt - ref;
	strcpy(t->u.user.name, ipt->tcfi_t->u.kernel.target->name);

	if (nla_put(skb, TCA_IPT_TARG, ipt->tcfi_t->u.user.target_size, t) ||
	    nla_put_u32(skb, TCA_IPT_INDEX, ipt->tcf_index) ||
	    nla_put_u32(skb, TCA_IPT_HOOK, ipt->tcfi_hook) ||
	    nla_put(skb, TCA_IPT_CNT, sizeof(struct tc_cnt), &c) ||
	    nla_put_string(skb, TCA_IPT_TABLE, ipt->tcfi_tname))
		goto nla_put_failure;

	tcf_tm_dump(&tm, &ipt->tcf_tm);
	if (nla_put_64bit(skb, TCA_IPT_TM, sizeof(tm), &tm, TCA_IPT_PAD))
		goto nla_put_failure;

	kfree(t);
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	kfree(t);
	return -1;
}

static int tcf_ipt_walker(struct net *net, struct sk_buff *skb,
			  struct netlink_callback *cb, int type,
			  const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, ipt_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tcf_ipt_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, ipt_net_id);

	return tcf_hash_search(tn, a, index);
}

static struct tc_action_ops act_ipt_ops = {
	.kind		=	"ipt",
	.type		=	TCA_ACT_IPT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_ipt,
	.dump		=	tcf_ipt_dump,
	.cleanup	=	tcf_ipt_release,
	.init		=	tcf_ipt_init,
	.walk		=	tcf_ipt_walker,
	.lookup		=	tcf_ipt_search,
	.size		=	sizeof(struct tcf_ipt),
};

static __net_init int ipt_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, ipt_net_id);

	return tc_action_net_init(tn, &act_ipt_ops, IPT_TAB_MASK);
}

static void __net_exit ipt_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, ipt_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations ipt_net_ops = {
	.init = ipt_init_net,
	.exit = ipt_exit_net,
	.id   = &ipt_net_id,
	.size = sizeof(struct tc_action_net),
};

static int tcf_xt_walker(struct net *net, struct sk_buff *skb,
			 struct netlink_callback *cb, int type,
			 const struct tc_action_ops *ops)
{
	struct tc_action_net *tn = net_generic(net, xt_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops);
}

static int tcf_xt_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, xt_net_id);

	return tcf_hash_search(tn, a, index);
}

static struct tc_action_ops act_xt_ops = {
	.kind		=	"xt",
	.type		=	TCA_ACT_XT,
	.owner		=	THIS_MODULE,
	.act		=	tcf_ipt,
	.dump		=	tcf_ipt_dump,
	.cleanup	=	tcf_ipt_release,
	.init		=	tcf_xt_init,
	.walk		=	tcf_xt_walker,
	.lookup		=	tcf_xt_search,
	.size		=	sizeof(struct tcf_ipt),
};

static __net_init int xt_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, xt_net_id);

	return tc_action_net_init(tn, &act_xt_ops, IPT_TAB_MASK);
}

static void __net_exit xt_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, xt_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations xt_net_ops = {
	.init = xt_init_net,
	.exit = xt_exit_net,
	.id   = &xt_net_id,
	.size = sizeof(struct tc_action_net),
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-13)");
MODULE_DESCRIPTION("Iptables target actions");
MODULE_LICENSE("GPL");
MODULE_ALIAS("act_xt");

static int __init ipt_init_module(void)
{
	int ret1, ret2;

	ret1 = tcf_register_action(&act_xt_ops, &xt_net_ops);
	if (ret1 < 0)
		pr_err("Failed to load xt action\n");

	ret2 = tcf_register_action(&act_ipt_ops, &ipt_net_ops);
	if (ret2 < 0)
		pr_err("Failed to load ipt action\n");

	if (ret1 < 0 && ret2 < 0) {
		return ret1;
	} else
		return 0;
}

static void __exit ipt_cleanup_module(void)
{
	tcf_unregister_action(&act_ipt_ops, &ipt_net_ops);
	tcf_unregister_action(&act_xt_ops, &xt_net_ops);
}

module_init(ipt_init_module);
module_exit(ipt_cleanup_module);
