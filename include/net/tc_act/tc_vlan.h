/*
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __NET_TC_VLAN_H
#define __NET_TC_VLAN_H

#include <net/act_api.h>
#include <linux/tc_act/tc_vlan.h>

#define VLAN_F_POP		0x1
#define VLAN_F_PUSH		0x2

struct tcf_vlan {
	struct tc_action	common;
	int			tcfv_action;
	u16			tcfv_push_vid;
	__be16			tcfv_push_proto;
	u8			tcfv_push_prio;
};
#define to_vlan(a) ((struct tcf_vlan *)a)

static inline bool is_tcf_vlan(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_VLAN)
		return true;
#endif
	return false;
}

static inline u32 tcf_vlan_action(const struct tc_action *a)
{
	return to_vlan(a)->tcfv_action;
}

static inline u16 tcf_vlan_push_vid(const struct tc_action *a)
{
	return to_vlan(a)->tcfv_push_vid;
}

static inline __be16 tcf_vlan_push_proto(const struct tc_action *a)
{
	return to_vlan(a)->tcfv_push_proto;
}

#endif /* __NET_TC_VLAN_H */
