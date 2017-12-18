/*
 * tcp_diag.c	Module for monitoring TCP transport protocols sockets.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>

#include <linux/tcp.h>

#include <net/tcp.h>

static void tcp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			      void *_info)
{
	struct tcp_info *info = _info;

	if (sk_state_load(sk) == TCP_LISTEN) {
		r->idiag_rqueue = sk->sk_ack_backlog;
		r->idiag_wqueue = sk->sk_max_ack_backlog;
	} else if (sk->sk_type == SOCK_STREAM) {
		const struct tcp_sock *tp = tcp_sk(sk);

		r->idiag_rqueue = max_t(int, tp->rcv_nxt - tp->copied_seq, 0);
		r->idiag_wqueue = tp->write_seq - tp->snd_una;
	}
	if (info)
		tcp_get_info(sk, info);
}

static void tcp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			  const struct inet_diag_req_v2 *r, struct nlattr *bc)
{
	inet_diag_dump_icsk(&tcp_hashinfo, skb, cb, r, bc);
}

static int tcp_diag_dump_one(struct sk_buff *in_skb, const struct nlmsghdr *nlh,
			     const struct inet_diag_req_v2 *req)
{
	return inet_diag_dump_one_icsk(&tcp_hashinfo, in_skb, nlh, req);
}

#ifdef CONFIG_INET_DIAG_DESTROY
static int tcp_diag_destroy(struct sk_buff *in_skb,
			    const struct inet_diag_req_v2 *req)
{
	struct net *net = sock_net(in_skb->sk);
	struct sock *sk = inet_diag_find_one_icsk(net, &tcp_hashinfo, req);
	int err;

	if (IS_ERR(sk))
		return PTR_ERR(sk);

	err = sock_diag_destroy(sk, ECONNABORTED);

	sock_gen_put(sk);

	return err;
}
#endif

static const struct inet_diag_handler tcp_diag_handler = {
	.dump		 = tcp_diag_dump,
	.dump_one	 = tcp_diag_dump_one,
	.idiag_get_info	 = tcp_diag_get_info,
	.idiag_type	 = IPPROTO_TCP,
	.idiag_info_size = sizeof(struct tcp_info),
#ifdef CONFIG_INET_DIAG_DESTROY
	.destroy	 = tcp_diag_destroy,
#endif
};

static int __init tcp_diag_init(void)
{
	return inet_diag_register(&tcp_diag_handler);
}

static void __exit tcp_diag_exit(void)
{
	inet_diag_unregister(&tcp_diag_handler);
}

module_init(tcp_diag_init);
module_exit(tcp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-6 /* AF_INET - IPPROTO_TCP */);
