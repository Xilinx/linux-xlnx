#ifndef __NET_TC_MIR_H
#define __NET_TC_MIR_H

#include <net/act_api.h>
#include <linux/tc_act/tc_mirred.h>

struct tcf_mirred {
	struct tc_action	common;
	int			tcfm_eaction;
	int			tcfm_ifindex;
	int			tcfm_ok_push;
	struct net_device __rcu	*tcfm_dev;
	struct list_head	tcfm_list;
};
#define to_mirred(a) ((struct tcf_mirred *)a)

static inline bool is_tcf_mirred_redirect(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_EGRESS_REDIR;
#endif
	return false;
}

static inline bool is_tcf_mirred_mirror(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_MIRRED)
		return to_mirred(a)->tcfm_eaction == TCA_EGRESS_MIRROR;
#endif
	return false;
}

static inline int tcf_mirred_ifindex(const struct tc_action *a)
{
	return to_mirred(a)->tcfm_ifindex;
}

#endif /* __NET_TC_MIR_H */
