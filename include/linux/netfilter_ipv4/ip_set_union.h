#ifndef __IP_SET_UNION_H
#define __IP_SET_UNION_H

#ifdef __KERNEL__
#include <linux/list.h>
#endif
#include <linux/netfilter_ipv4/ip_set.h>

#define SETTYPE_NAME "union"
#define MAX_RANGE 0x0000FFFF

#ifdef __KERNEL__
struct ip_set_union_elem {
	struct list_head list;
	ip_set_id_t index;
};
#endif

struct ip_set_union {
#ifdef __KERNEL__
	struct list_head members;
#endif
};

struct ip_set_req_union_create {
};

struct ip_set_req_union {
	char name[IP_SET_MAXNAMELEN];
};

#endif	/* __IP_SET_UNION_H */
