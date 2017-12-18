/*
 * net/sched/act_api.c	Packet action API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Jamal Hadi Salim
 *
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/sch_generic.h>
#include <net/act_api.h>
#include <net/netlink.h>

static void free_tcf(struct rcu_head *head)
{
	struct tc_action *p = container_of(head, struct tc_action, tcfa_rcu);

	free_percpu(p->cpu_bstats);
	free_percpu(p->cpu_qstats);
	kfree(p);
}

static void tcf_hash_destroy(struct tcf_hashinfo *hinfo, struct tc_action *p)
{
	spin_lock_bh(&hinfo->lock);
	hlist_del(&p->tcfa_head);
	spin_unlock_bh(&hinfo->lock);
	gen_kill_estimator(&p->tcfa_bstats,
			   &p->tcfa_rate_est);
	/*
	 * gen_estimator est_timer() might access p->tcfa_lock
	 * or bstats, wait a RCU grace period before freeing p
	 */
	call_rcu(&p->tcfa_rcu, free_tcf);
}

int __tcf_hash_release(struct tc_action *p, bool bind, bool strict)
{
	int ret = 0;

	if (p) {
		if (bind)
			p->tcfa_bindcnt--;
		else if (strict && p->tcfa_bindcnt > 0)
			return -EPERM;

		p->tcfa_refcnt--;
		if (p->tcfa_bindcnt <= 0 && p->tcfa_refcnt <= 0) {
			if (p->ops->cleanup)
				p->ops->cleanup(p, bind);
			tcf_hash_destroy(p->hinfo, p);
			ret = ACT_P_DELETED;
		}
	}

	return ret;
}
EXPORT_SYMBOL(__tcf_hash_release);

static int tcf_dump_walker(struct tcf_hashinfo *hinfo, struct sk_buff *skb,
			   struct netlink_callback *cb)
{
	int err = 0, index = -1, i = 0, s_i = 0, n_i = 0;
	struct nlattr *nest;

	spin_lock_bh(&hinfo->lock);

	s_i = cb->args[0];

	for (i = 0; i < (hinfo->hmask + 1); i++) {
		struct hlist_head *head;
		struct tc_action *p;

		head = &hinfo->htab[tcf_hash(i, hinfo->hmask)];

		hlist_for_each_entry_rcu(p, head, tcfa_head) {
			index++;
			if (index < s_i)
				continue;

			nest = nla_nest_start(skb, n_i);
			if (nest == NULL)
				goto nla_put_failure;
			err = tcf_action_dump_1(skb, p, 0, 0);
			if (err < 0) {
				index--;
				nlmsg_trim(skb, nest);
				goto done;
			}
			nla_nest_end(skb, nest);
			n_i++;
			if (n_i >= TCA_ACT_MAX_PRIO)
				goto done;
		}
	}
done:
	spin_unlock_bh(&hinfo->lock);
	if (n_i)
		cb->args[0] += n_i;
	return n_i;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	goto done;
}

static int tcf_del_walker(struct tcf_hashinfo *hinfo, struct sk_buff *skb,
			  const struct tc_action_ops *ops)
{
	struct nlattr *nest;
	int i = 0, n_i = 0;
	int ret = -EINVAL;

	nest = nla_nest_start(skb, 0);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put_string(skb, TCA_KIND, ops->kind))
		goto nla_put_failure;
	for (i = 0; i < (hinfo->hmask + 1); i++) {
		struct hlist_head *head;
		struct hlist_node *n;
		struct tc_action *p;

		head = &hinfo->htab[tcf_hash(i, hinfo->hmask)];
		hlist_for_each_entry_safe(p, n, head, tcfa_head) {
			ret = __tcf_hash_release(p, false, true);
			if (ret == ACT_P_DELETED) {
				module_put(p->ops->owner);
				n_i++;
			} else if (ret < 0)
				goto nla_put_failure;
		}
	}
	if (nla_put_u32(skb, TCA_FCNT, n_i))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	return n_i;
nla_put_failure:
	nla_nest_cancel(skb, nest);
	return ret;
}

int tcf_generic_walker(struct tc_action_net *tn, struct sk_buff *skb,
		       struct netlink_callback *cb, int type,
		       const struct tc_action_ops *ops)
{
	struct tcf_hashinfo *hinfo = tn->hinfo;

	if (type == RTM_DELACTION) {
		return tcf_del_walker(hinfo, skb, ops);
	} else if (type == RTM_GETACTION) {
		return tcf_dump_walker(hinfo, skb, cb);
	} else {
		WARN(1, "tcf_generic_walker: unknown action %d\n", type);
		return -EINVAL;
	}
}
EXPORT_SYMBOL(tcf_generic_walker);

static struct tc_action *tcf_hash_lookup(u32 index, struct tcf_hashinfo *hinfo)
{
	struct tc_action *p = NULL;
	struct hlist_head *head;

	spin_lock_bh(&hinfo->lock);
	head = &hinfo->htab[tcf_hash(index, hinfo->hmask)];
	hlist_for_each_entry_rcu(p, head, tcfa_head)
		if (p->tcfa_index == index)
			break;
	spin_unlock_bh(&hinfo->lock);

	return p;
}

u32 tcf_hash_new_index(struct tc_action_net *tn)
{
	struct tcf_hashinfo *hinfo = tn->hinfo;
	u32 val = hinfo->index;

	do {
		if (++val == 0)
			val = 1;
	} while (tcf_hash_lookup(val, hinfo));

	hinfo->index = val;
	return val;
}
EXPORT_SYMBOL(tcf_hash_new_index);

int tcf_hash_search(struct tc_action_net *tn, struct tc_action **a, u32 index)
{
	struct tcf_hashinfo *hinfo = tn->hinfo;
	struct tc_action *p = tcf_hash_lookup(index, hinfo);

	if (p) {
		*a = p;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(tcf_hash_search);

bool tcf_hash_check(struct tc_action_net *tn, u32 index, struct tc_action **a,
		    int bind)
{
	struct tcf_hashinfo *hinfo = tn->hinfo;
	struct tc_action *p = NULL;

	if (index && (p = tcf_hash_lookup(index, hinfo)) != NULL) {
		if (bind)
			p->tcfa_bindcnt++;
		p->tcfa_refcnt++;
		*a = p;
		return true;
	}
	return false;
}
EXPORT_SYMBOL(tcf_hash_check);

void tcf_hash_cleanup(struct tc_action *a, struct nlattr *est)
{
	if (est)
		gen_kill_estimator(&a->tcfa_bstats,
				   &a->tcfa_rate_est);
	call_rcu(&a->tcfa_rcu, free_tcf);
}
EXPORT_SYMBOL(tcf_hash_cleanup);

int tcf_hash_create(struct tc_action_net *tn, u32 index, struct nlattr *est,
		    struct tc_action **a, const struct tc_action_ops *ops,
		    int bind, bool cpustats)
{
	struct tc_action *p = kzalloc(ops->size, GFP_KERNEL);
	struct tcf_hashinfo *hinfo = tn->hinfo;
	int err = -ENOMEM;

	if (unlikely(!p))
		return -ENOMEM;
	p->tcfa_refcnt = 1;
	if (bind)
		p->tcfa_bindcnt = 1;

	if (cpustats) {
		p->cpu_bstats = netdev_alloc_pcpu_stats(struct gnet_stats_basic_cpu);
		if (!p->cpu_bstats) {
err1:
			kfree(p);
			return err;
		}
		p->cpu_qstats = alloc_percpu(struct gnet_stats_queue);
		if (!p->cpu_qstats) {
err2:
			free_percpu(p->cpu_bstats);
			goto err1;
		}
	}
	spin_lock_init(&p->tcfa_lock);
	INIT_HLIST_NODE(&p->tcfa_head);
	p->tcfa_index = index ? index : tcf_hash_new_index(tn);
	p->tcfa_tm.install = jiffies;
	p->tcfa_tm.lastuse = jiffies;
	p->tcfa_tm.firstuse = 0;
	if (est) {
		err = gen_new_estimator(&p->tcfa_bstats, p->cpu_bstats,
					&p->tcfa_rate_est,
					&p->tcfa_lock, NULL, est);
		if (err) {
			free_percpu(p->cpu_qstats);
			goto err2;
		}
	}

	p->hinfo = hinfo;
	p->ops = ops;
	INIT_LIST_HEAD(&p->list);
	*a = p;
	return 0;
}
EXPORT_SYMBOL(tcf_hash_create);

void tcf_hash_insert(struct tc_action_net *tn, struct tc_action *a)
{
	struct tcf_hashinfo *hinfo = tn->hinfo;
	unsigned int h = tcf_hash(a->tcfa_index, hinfo->hmask);

	spin_lock_bh(&hinfo->lock);
	hlist_add_head(&a->tcfa_head, &hinfo->htab[h]);
	spin_unlock_bh(&hinfo->lock);
}
EXPORT_SYMBOL(tcf_hash_insert);

void tcf_hashinfo_destroy(const struct tc_action_ops *ops,
			  struct tcf_hashinfo *hinfo)
{
	int i;

	for (i = 0; i < hinfo->hmask + 1; i++) {
		struct tc_action *p;
		struct hlist_node *n;

		hlist_for_each_entry_safe(p, n, &hinfo->htab[i], tcfa_head) {
			int ret;

			ret = __tcf_hash_release(p, false, true);
			if (ret == ACT_P_DELETED)
				module_put(ops->owner);
			else if (ret < 0)
				return;
		}
	}
	kfree(hinfo->htab);
}
EXPORT_SYMBOL(tcf_hashinfo_destroy);

static LIST_HEAD(act_base);
static DEFINE_RWLOCK(act_mod_lock);

int tcf_register_action(struct tc_action_ops *act,
			struct pernet_operations *ops)
{
	struct tc_action_ops *a;
	int ret;

	if (!act->act || !act->dump || !act->init || !act->walk || !act->lookup)
		return -EINVAL;

	/* We have to register pernet ops before making the action ops visible,
	 * otherwise tcf_action_init_1() could get a partially initialized
	 * netns.
	 */
	ret = register_pernet_subsys(ops);
	if (ret)
		return ret;

	write_lock(&act_mod_lock);
	list_for_each_entry(a, &act_base, head) {
		if (act->type == a->type || (strcmp(act->kind, a->kind) == 0)) {
			write_unlock(&act_mod_lock);
			unregister_pernet_subsys(ops);
			return -EEXIST;
		}
	}
	list_add_tail(&act->head, &act_base);
	write_unlock(&act_mod_lock);

	return 0;
}
EXPORT_SYMBOL(tcf_register_action);

int tcf_unregister_action(struct tc_action_ops *act,
			  struct pernet_operations *ops)
{
	struct tc_action_ops *a;
	int err = -ENOENT;

	write_lock(&act_mod_lock);
	list_for_each_entry(a, &act_base, head) {
		if (a == act) {
			list_del(&act->head);
			err = 0;
			break;
		}
	}
	write_unlock(&act_mod_lock);
	if (!err)
		unregister_pernet_subsys(ops);
	return err;
}
EXPORT_SYMBOL(tcf_unregister_action);

/* lookup by name */
static struct tc_action_ops *tc_lookup_action_n(char *kind)
{
	struct tc_action_ops *a, *res = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		list_for_each_entry(a, &act_base, head) {
			if (strcmp(kind, a->kind) == 0) {
				if (try_module_get(a->owner))
					res = a;
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return res;
}

/* lookup by nlattr */
static struct tc_action_ops *tc_lookup_action(struct nlattr *kind)
{
	struct tc_action_ops *a, *res = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		list_for_each_entry(a, &act_base, head) {
			if (nla_strcmp(kind, a->kind) == 0) {
				if (try_module_get(a->owner))
					res = a;
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return res;
}

int tcf_action_exec(struct sk_buff *skb, struct tc_action **actions,
		    int nr_actions, struct tcf_result *res)
{
	int ret = -1, i;

	if (skb->tc_verd & TC_NCLS) {
		skb->tc_verd = CLR_TC_NCLS(skb->tc_verd);
		ret = TC_ACT_OK;
		goto exec_done;
	}
	for (i = 0; i < nr_actions; i++) {
		const struct tc_action *a = actions[i];

repeat:
		ret = a->ops->act(skb, a, res);
		if (ret == TC_ACT_REPEAT)
			goto repeat;	/* we need a ttl - JHS */
		if (ret != TC_ACT_PIPE)
			goto exec_done;
	}
exec_done:
	return ret;
}
EXPORT_SYMBOL(tcf_action_exec);

int tcf_action_destroy(struct list_head *actions, int bind)
{
	struct tc_action *a, *tmp;
	int ret = 0;

	list_for_each_entry_safe(a, tmp, actions, list) {
		ret = __tcf_hash_release(a, bind, true);
		if (ret == ACT_P_DELETED)
			module_put(a->ops->owner);
		else if (ret < 0)
			return ret;
	}
	return ret;
}

int
tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	return a->ops->dump(skb, a, bind, ref);
}

int
tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	if (nla_put_string(skb, TCA_KIND, a->ops->kind))
		goto nla_put_failure;
	if (tcf_action_copy_stats(skb, a, 0))
		goto nla_put_failure;
	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	err = tcf_action_dump_old(skb, a, bind, ref);
	if (err > 0) {
		nla_nest_end(skb, nest);
		return err;
	}

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}
EXPORT_SYMBOL(tcf_action_dump_1);

int tcf_action_dump(struct sk_buff *skb, struct list_head *actions,
		    int bind, int ref)
{
	struct tc_action *a;
	int err = -EINVAL;
	struct nlattr *nest;

	list_for_each_entry(a, actions, list) {
		nest = nla_nest_start(skb, a->order);
		if (nest == NULL)
			goto nla_put_failure;
		err = tcf_action_dump_1(skb, a, bind, ref);
		if (err < 0)
			goto errout;
		nla_nest_end(skb, nest);
	}

	return 0;

nla_put_failure:
	err = -EINVAL;
errout:
	nla_nest_cancel(skb, nest);
	return err;
}

struct tc_action *tcf_action_init_1(struct net *net, struct nlattr *nla,
				    struct nlattr *est, char *name, int ovr,
				    int bind)
{
	struct tc_action *a;
	struct tc_action_ops *a_o;
	char act_name[IFNAMSIZ];
	struct nlattr *tb[TCA_ACT_MAX + 1];
	struct nlattr *kind;
	int err;

	if (name == NULL) {
		err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
		if (err < 0)
			goto err_out;
		err = -EINVAL;
		kind = tb[TCA_ACT_KIND];
		if (kind == NULL)
			goto err_out;
		if (nla_strlcpy(act_name, kind, IFNAMSIZ) >= IFNAMSIZ)
			goto err_out;
	} else {
		err = -EINVAL;
		if (strlcpy(act_name, name, IFNAMSIZ) >= IFNAMSIZ)
			goto err_out;
	}

	a_o = tc_lookup_action_n(act_name);
	if (a_o == NULL) {
#ifdef CONFIG_MODULES
		rtnl_unlock();
		request_module("act_%s", act_name);
		rtnl_lock();

		a_o = tc_lookup_action_n(act_name);

		/* We dropped the RTNL semaphore in order to
		 * perform the module load.  So, even if we
		 * succeeded in loading the module we have to
		 * tell the caller to replay the request.  We
		 * indicate this using -EAGAIN.
		 */
		if (a_o != NULL) {
			err = -EAGAIN;
			goto err_mod;
		}
#endif
		err = -ENOENT;
		goto err_out;
	}

	/* backward compatibility for policer */
	if (name == NULL)
		err = a_o->init(net, tb[TCA_ACT_OPTIONS], est, &a, ovr, bind);
	else
		err = a_o->init(net, nla, est, &a, ovr, bind);
	if (err < 0)
		goto err_mod;

	/* module count goes up only when brand new policy is created
	 * if it exists and is only bound to in a_o->init() then
	 * ACT_P_CREATED is not returned (a zero is).
	 */
	if (err != ACT_P_CREATED)
		module_put(a_o->owner);

	return a;

err_mod:
	module_put(a_o->owner);
err_out:
	return ERR_PTR(err);
}

static void cleanup_a(struct list_head *actions, int ovr)
{
	struct tc_action *a;

	if (!ovr)
		return;

	list_for_each_entry(a, actions, list)
		a->tcfa_refcnt--;
}

int tcf_action_init(struct net *net, struct nlattr *nla, struct nlattr *est,
		    char *name, int ovr, int bind, struct list_head *actions)
{
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *act;
	int err;
	int i;

	err = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL);
	if (err < 0)
		return err;

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_init_1(net, tb[i], est, name, ovr, bind);
		if (IS_ERR(act)) {
			err = PTR_ERR(act);
			goto err;
		}
		act->order = i;
		if (ovr)
			act->tcfa_refcnt++;
		list_add_tail(&act->list, actions);
	}

	/* Remove the temp refcnt which was necessary to protect against
	 * destroying an existing action which was being replaced
	 */
	cleanup_a(actions, ovr);
	return 0;

err:
	tcf_action_destroy(actions, bind);
	return err;
}

int tcf_action_copy_stats(struct sk_buff *skb, struct tc_action *p,
			  int compat_mode)
{
	int err = 0;
	struct gnet_dump d;

	if (p == NULL)
		goto errout;

	/* compat_mode being true specifies a call that is supposed
	 * to add additional backward compatibility statistic TLVs.
	 */
	if (compat_mode) {
		if (p->type == TCA_OLD_COMPAT)
			err = gnet_stats_start_copy_compat(skb, 0,
							   TCA_STATS,
							   TCA_XSTATS,
							   &p->tcfa_lock, &d,
							   TCA_PAD);
		else
			return 0;
	} else
		err = gnet_stats_start_copy(skb, TCA_ACT_STATS,
					    &p->tcfa_lock, &d, TCA_ACT_PAD);

	if (err < 0)
		goto errout;

	if (gnet_stats_copy_basic(NULL, &d, p->cpu_bstats, &p->tcfa_bstats) < 0 ||
	    gnet_stats_copy_rate_est(&d, &p->tcfa_bstats,
				     &p->tcfa_rate_est) < 0 ||
	    gnet_stats_copy_queue(&d, p->cpu_qstats,
				  &p->tcfa_qstats,
				  p->tcfa_qstats.qlen) < 0)
		goto errout;

	if (gnet_stats_finish_copy(&d) < 0)
		goto errout;

	return 0;

errout:
	return -1;
}

static int tca_get_fill(struct sk_buff *skb, struct list_head *actions,
			u32 portid, u32 seq, u16 flags, int event, int bind,
			int ref)
{
	struct tcamsg *t;
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*t), flags);
	if (!nlh)
		goto out_nlmsg_trim;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_nlmsg_trim;

	if (tcf_action_dump(skb, actions, bind, ref) < 0)
		goto out_nlmsg_trim;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
	nlmsg_trim(skb, b);
	return -1;
}

static int
act_get_notify(struct net *net, u32 portid, struct nlmsghdr *n,
	       struct list_head *actions, int event)
{
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;
	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, 0, event,
			 0, 0) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnl_unicast(skb, net, portid);
}

static struct tc_action *tcf_action_get_1(struct net *net, struct nlattr *nla,
					  struct nlmsghdr *n, u32 portid)
{
	struct nlattr *tb[TCA_ACT_MAX + 1];
	const struct tc_action_ops *ops;
	struct tc_action *a;
	int index;
	int err;

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	if (tb[TCA_ACT_INDEX] == NULL ||
	    nla_len(tb[TCA_ACT_INDEX]) < sizeof(index))
		goto err_out;
	index = nla_get_u32(tb[TCA_ACT_INDEX]);

	err = -EINVAL;
	ops = tc_lookup_action(tb[TCA_ACT_KIND]);
	if (!ops) /* could happen in batch of actions */
		goto err_out;
	err = -ENOENT;
	if (ops->lookup(net, &a, index) == 0)
		goto err_mod;

	module_put(ops->owner);
	return a;

err_mod:
	module_put(ops->owner);
err_out:
	return ERR_PTR(err);
}

static int tca_action_flush(struct net *net, struct nlattr *nla,
			    struct nlmsghdr *n, u32 portid)
{
	struct sk_buff *skb;
	unsigned char *b;
	struct nlmsghdr *nlh;
	struct tcamsg *t;
	struct netlink_callback dcb;
	struct nlattr *nest;
	struct nlattr *tb[TCA_ACT_MAX + 1];
	const struct tc_action_ops *ops;
	struct nlattr *kind;
	int err = -ENOMEM;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		pr_debug("tca_action_flush: failed skb alloc\n");
		return err;
	}

	b = skb_tail_pointer(skb);

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	kind = tb[TCA_ACT_KIND];
	ops = tc_lookup_action(kind);
	if (!ops) /*some idjot trying to flush unknown action */
		goto err_out;

	nlh = nlmsg_put(skb, portid, n->nlmsg_seq, RTM_DELACTION,
			sizeof(*t), 0);
	if (!nlh)
		goto out_module_put;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_module_put;

	err = ops->walk(net, skb, &dcb, RTM_DELACTION, ops);
	if (err < 0)
		goto out_module_put;
	if (err == 0)
		goto noflush_out;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	nlh->nlmsg_flags |= NLM_F_ROOT;
	module_put(ops->owner);
	err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (err > 0)
		return 0;

	return err;

out_module_put:
	module_put(ops->owner);
err_out:
noflush_out:
	kfree_skb(skb);
	return err;
}

static int
tcf_del_notify(struct net *net, struct nlmsghdr *n, struct list_head *actions,
	       u32 portid)
{
	int ret;
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, 0, RTM_DELACTION,
			 0, 1) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	/* now do the delete */
	ret = tcf_action_destroy(actions, 0);
	if (ret < 0) {
		kfree_skb(skb);
		return ret;
	}

	ret = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (ret > 0)
		return 0;
	return ret;
}

static int
tca_action_gd(struct net *net, struct nlattr *nla, struct nlmsghdr *n,
	      u32 portid, int event)
{
	int i, ret;
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *act;
	LIST_HEAD(actions);

	ret = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL);
	if (ret < 0)
		return ret;

	if (event == RTM_DELACTION && n->nlmsg_flags & NLM_F_ROOT) {
		if (tb[1] != NULL)
			return tca_action_flush(net, tb[1], n, portid);
		else
			return -EINVAL;
	}

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_get_1(net, tb[i], n, portid);
		if (IS_ERR(act)) {
			ret = PTR_ERR(act);
			goto err;
		}
		act->order = i;
		if (event == RTM_GETACTION)
			act->tcfa_refcnt++;
		list_add_tail(&act->list, &actions);
	}

	if (event == RTM_GETACTION)
		ret = act_get_notify(net, portid, n, &actions, event);
	else { /* delete */
		ret = tcf_del_notify(net, n, &actions, portid);
		if (ret)
			goto err;
		return ret;
	}
err:
	tcf_action_destroy(&actions, 0);
	return ret;
}

static int
tcf_add_notify(struct net *net, struct nlmsghdr *n, struct list_head *actions,
	       u32 portid)
{
	struct sk_buff *skb;
	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, n->nlmsg_flags,
			 RTM_NEWACTION, 0, 0) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (err > 0)
		err = 0;
	return err;
}

static int tcf_action_add(struct net *net, struct nlattr *nla,
			  struct nlmsghdr *n, u32 portid, int ovr)
{
	int ret = 0;
	LIST_HEAD(actions);

	ret = tcf_action_init(net, nla, NULL, NULL, ovr, 0, &actions);
	if (ret)
		return ret;

	return tcf_add_notify(net, n, &actions, portid);
}

static int tc_ctl_action(struct sk_buff *skb, struct nlmsghdr *n)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_ACT_MAX + 1];
	u32 portid = skb ? NETLINK_CB(skb).portid : 0;
	int ret = 0, ovr = 0;

	if ((n->nlmsg_type != RTM_GETACTION) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	ret = nlmsg_parse(n, sizeof(struct tcamsg), tca, TCA_ACT_MAX, NULL);
	if (ret < 0)
		return ret;

	if (tca[TCA_ACT_TAB] == NULL) {
		pr_notice("tc_ctl_action: received NO action attribs\n");
		return -EINVAL;
	}

	/* n->nlmsg_flags & NLM_F_CREATE */
	switch (n->nlmsg_type) {
	case RTM_NEWACTION:
		/* we are going to assume all other flags
		 * imply create only if it doesn't exist
		 * Note that CREATE | EXCL implies that
		 * but since we want avoid ambiguity (eg when flags
		 * is zero) then just set this
		 */
		if (n->nlmsg_flags & NLM_F_REPLACE)
			ovr = 1;
replay:
		ret = tcf_action_add(net, tca[TCA_ACT_TAB], n, portid, ovr);
		if (ret == -EAGAIN)
			goto replay;
		break;
	case RTM_DELACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    portid, RTM_DELACTION);
		break;
	case RTM_GETACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    portid, RTM_GETACTION);
		break;
	default:
		BUG();
	}

	return ret;
}

static struct nlattr *find_dump_kind(const struct nlmsghdr *n)
{
	struct nlattr *tb1, *tb2[TCA_ACT_MAX + 1];
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct nlattr *nla[TCAA_MAX + 1];
	struct nlattr *kind;

	if (nlmsg_parse(n, sizeof(struct tcamsg), nla, TCAA_MAX, NULL) < 0)
		return NULL;
	tb1 = nla[TCA_ACT_TAB];
	if (tb1 == NULL)
		return NULL;

	if (nla_parse(tb, TCA_ACT_MAX_PRIO, nla_data(tb1),
		      NLMSG_ALIGN(nla_len(tb1)), NULL) < 0)
		return NULL;

	if (tb[1] == NULL)
		return NULL;
	if (nla_parse_nested(tb2, TCA_ACT_MAX, tb[1], NULL) < 0)
		return NULL;
	kind = tb2[TCA_ACT_KIND];

	return kind;
}

static int tc_dump_action(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;
	struct tc_action_ops *a_o;
	int ret = 0;
	struct tcamsg *t = (struct tcamsg *) nlmsg_data(cb->nlh);
	struct nlattr *kind = find_dump_kind(cb->nlh);

	if (kind == NULL) {
		pr_info("tc_dump_action: action bad kind\n");
		return 0;
	}

	a_o = tc_lookup_action(kind);
	if (a_o == NULL)
		return 0;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			cb->nlh->nlmsg_type, sizeof(*t), 0);
	if (!nlh)
		goto out_module_put;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_module_put;

	ret = a_o->walk(net, skb, cb, RTM_GETACTION, a_o);
	if (ret < 0)
		goto out_module_put;

	if (ret > 0) {
		nla_nest_end(skb, nest);
		ret = skb->len;
	} else
		nlmsg_trim(skb, b);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	if (NETLINK_CB(cb->skb).portid && ret)
		nlh->nlmsg_flags |= NLM_F_MULTI;
	module_put(a_o->owner);
	return skb->len;

out_module_put:
	module_put(a_o->owner);
	nlmsg_trim(skb, b);
	return skb->len;
}

static int __init tc_action_init(void)
{
	rtnl_register(PF_UNSPEC, RTM_NEWACTION, tc_ctl_action, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELACTION, tc_ctl_action, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETACTION, tc_ctl_action, tc_dump_action,
		      NULL);

	return 0;
}

subsys_initcall(tc_action_init);
