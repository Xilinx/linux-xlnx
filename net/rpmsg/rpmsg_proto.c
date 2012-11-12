/*
 * AF_RPMSG: Remote processor messaging sockets
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)    "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rwlock.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <net/sock.h>
#include <net/rpmsg.h>
#include <linux/radix-tree.h>

#define RPMSG_CB(skb)	(*(struct sockaddr_rpmsg *)&((skb)->cb))

/*
 * A two-level radix-tree-based scheme is used to maintain the rpmsg channels
 * we're exposing to userland. The first radix tree maps vproc index id
 * to its channels, and the second radix tree associates each channel
 * with its destination addresses (so sockaddr_rpmsg lookups are quick).
 *
 * Currently only channels with a valid dst address are supported (aka 'client'
 * channels as opposed to 'server' channels which usually only have a valid
 * src address).
 */
static RADIX_TREE(rpmsg_channels, GFP_KERNEL);

/*
 * Synchronization of access to the tree is achieved using a mutex,
 * because we're using non-atomic radix tree allocations.
 */
static DEFINE_MUTEX(rpmsg_channels_lock);

static struct proto rpmsg_proto = {
	.name		= "RPMSG",
	.owner		= THIS_MODULE,
	.obj_size = sizeof(struct rpmsg_socket),
};

static int rpmsg_sock_connect(struct socket *sock, struct sockaddr *addr,
							int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct rpmsg_socket *rpsk;
	struct sockaddr_rpmsg *sa;
	int err = 0;
	struct radix_tree_root *vrp_channels;
	struct rpmsg_channel *rpdev;

	pr_debug("sk %p\n", sk);

	if (sk->sk_state != RPMSG_OPEN)
		return -EBADFD;

	if (sk->sk_type != SOCK_SEQPACKET)
		return -EINVAL;

	if (!addr || addr->sa_family != AF_RPMSG)
		return -EINVAL;

	if (alen < sizeof(*sa))
		return -EINVAL;

	sa = (struct sockaddr_rpmsg *) addr;

	lock_sock(sk);

	rpsk = container_of(sk, struct rpmsg_socket, sk);

	mutex_lock(&rpmsg_channels_lock);

	/* find the set of channels exposed by this remote processor */
	vrp_channels = radix_tree_lookup(&rpmsg_channels, sa->vproc_id);
	if (!vrp_channels) {
		err = -EINVAL;
		goto out;
	}

	/* find the specific channel we need to connect with */
	rpdev = radix_tree_lookup(vrp_channels, sa->addr);
	if (!rpdev) {
		err = -EINVAL;
		goto out;
	}

	rpsk->rpdev = rpdev;

	/* bind this socket with its rpmsg endpoint */
	rpdev->ept->priv = sk;

	/* XXX take care of disconnection state too */
	sk->sk_state = RPMSG_CONNECTED;

out:
	mutex_unlock(&rpmsg_channels_lock);
	release_sock(sk);
	return err;
}

static int rpmsg_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
					struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct rpmsg_socket *rpsk;
	char payload[512];/* todo: sane payload length methodology */
	int err;

	pr_debug("sk %p len %d\n", sk, len);

	/* XXX check for sock_error as well ? */
	/* XXX handle noblock ? */
	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* no payload ? */
	if (msg->msg_iov->iov_base == NULL)
		return -EINVAL;

	lock_sock(sk);

	/* we don't support loopback at this point */
	if (sk->sk_state != RPMSG_CONNECTED) {
		release_sock(sk);
		return -ENOTCONN;
	}

	rpsk = container_of(sk, struct rpmsg_socket, sk);

	/* XXX for now, ignore the peer address. later use it
	 * with rpmsg_sendto, but only if user is root */

	err = memcpy_fromiovec(payload, msg->msg_iov, len);
	if (err)
		goto out;

	/* XXX add length validation */
	err = rpmsg_send(rpsk->rpdev, payload, len);
	if (err)
		pr_err("rpmsg_send failed: %d\n", err);

out:
	release_sock(sk);
	return err;
}

static int rpmsg_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_rpmsg *sa;
	struct sk_buff *skb;
	int noblock = flags & MSG_DONTWAIT;
	int ret;

	pr_debug("sk %p len %d\n", sk, len);

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	msg->msg_namelen = 0;

	skb = skb_recv_datagram(sk, flags, noblock, &ret);
	if (!skb)
		/* check for shutdown ? */
		return ret;

	if (msg->msg_name) {
		msg->msg_namelen = sizeof(*sa);
		sa = (struct sockaddr_rpmsg *) msg->msg_name;
		sa->vproc_id = RPMSG_CB(skb).vproc_id;
		sa->addr = RPMSG_CB(skb).addr;
		sa->family = AF_RPMSG;
	}

	if (len > skb->len) {
		len = skb->len;
	} else if (len < skb->len) {
		pr_warn("user buffer is too small\n");
		/* XXX truncate or error ? */
		msg->msg_flags |= MSG_TRUNC;
	}

	ret = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, len);
	if (ret) {
		pr_warn("error copying skb data: %d\n", ret);
		goto out_free;
	}

	ret = len;

out_free:
	skb_free_datagram(sk, skb);
	return ret;
}

static unsigned int rpmsg_sock_poll(struct file *file, struct socket *sock,
							poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;

	pr_debug("sk %p\n", sk);

	poll_wait(file, sk_sleep(sk), wait);

	/* exceptional events? */
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == RPMSG_CLOSED)
		mask |= POLLHUP;

	/*
	 * XXX is writable ?
	 * this depends on the destination processor.
	 * if loopback: we're writable unless no memory
	 * if to remote: we need enabled rpmsg buffer or user supplied bufs
	 * for now, let's always be writable.
	 */
	mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}
EXPORT_SYMBOL(rpmsg_sock_poll);

/*
 * return bound socket address information, either local or remote
 * note: len is just an output parameter, doesn't carry any input value
 */
static int rpmsg_sock_getname(struct socket *sock, struct sockaddr *addr,
							int *len, int peer)
{
	struct sock *sk = sock->sk;
	struct rpmsg_socket *rpsk;
	struct rpmsg_channel *rpdev;
	struct sockaddr_rpmsg *sa;

	pr_debug("sk %p\n", sk);

	rpsk = container_of(sk, struct rpmsg_socket, sk);
	rpdev = rpsk->rpdev;

	if (!rpdev)
		return -ENOTCONN;

	addr->sa_family = AF_RPMSG;

	sa = (struct sockaddr_rpmsg *) addr;

	*len = sizeof(*sa);

	if (peer) {
		sa->vproc_id = get_virtproc_id(rpdev->vrp);
		sa->addr = rpdev->dst;
	} else {
		sa->vproc_id = RPMSG_LOCALHOST;
		sa->addr = rpsk->rpdev->src;
	}

	return 0;
}

static int rpmsg_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct rpmsg_socket *rpsk = container_of(sk, struct rpmsg_socket, sk);

	pr_debug("sk %p\n", sk);

	if (!sk)
		return 0;

	if (rpsk->unregister_rpdev)
		device_unregister(&rpsk->rpdev->dev);

	sock_put(sock->sk);

	return 0;
}

/*
 * Notes:
 * - calling connect after bind isn't currently supported (is it even needed ?).
 * - userspace arguments to bind aren't intuitive: one needs to provide
 *   the vproc id of the remote processor he wants the channel to be shared
 *   with, and the -local- address he wants the channel to be bind with
 */
static int
rpmsg_sock_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct rpmsg_socket *rpsk = container_of(sk, struct rpmsg_socket, sk);
	struct rpmsg_channel *rpdev;
	struct sockaddr_rpmsg *sa = (struct sockaddr_rpmsg *)uaddr;

	pr_debug("sk %p\n", sk);

	if (sock->state == SS_CONNECTED)
		return -EINVAL;

	if (addr_len != sizeof(*sa))
		return -EINVAL;

	if (sa->family != AF_RPMSG)
		return -EINVAL;

	if (rpsk->rpdev)
		return -EBUSY;

	if (sk->sk_state != RPMSG_OPEN)
		return -EINVAL;

	rpdev = rpmsg_create_channel(sa->vproc_id, "rpmsg-proto", sa->addr,
							RPMSG_ADDR_ANY);
	if (!rpdev)
		return -EINVAL;

	rpsk->rpdev = rpdev;
	rpsk->unregister_rpdev = true;

	/* bind this socket with its rpmsg endpoint */
	rpdev->ept->priv = sk;

	sk->sk_state = RPMSG_LISTENING;

	return 0;
}

static const struct proto_ops rpmsg_sock_ops = {
	.family		= PF_RPMSG,
	.owner		= THIS_MODULE,

	.release	= rpmsg_sock_release,
	.connect	= rpmsg_sock_connect,
	.getname	= rpmsg_sock_getname,
	.sendmsg	= rpmsg_sock_sendmsg,
	.recvmsg	= rpmsg_sock_recvmsg,
	.poll		= rpmsg_sock_poll,
	.bind		= rpmsg_sock_bind,

	.listen		= sock_no_listen,
	.accept		= sock_no_accept,
	.ioctl		= sock_no_ioctl,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt
};

static void rpmsg_sock_destruct(struct sock *sk)
{
}

static int rpmsg_sock_create(struct net *net, struct socket *sock, int proto,
				int kern)
{
	struct sock *sk;

	if (sock->type != SOCK_SEQPACKET)
		return -ESOCKTNOSUPPORT;
	if (proto != 0)
		return -EPROTONOSUPPORT;

	sk = sk_alloc(net, PF_RPMSG, GFP_KERNEL, &rpmsg_proto);
	if (!sk)
		return -ENOMEM;

	pr_debug("sk %p\n", sk);

	sock->state = SS_UNCONNECTED;
	sock->ops = &rpmsg_sock_ops;
	sock_init_data(sock, sk);

	sk->sk_destruct = rpmsg_sock_destruct;
	sk->sk_protocol = proto;

	sk->sk_state = RPMSG_OPEN;

	return 0;
}

static const struct net_proto_family rpmsg_proto_family = {
	.family = PF_RPMSG,
	.create	= rpmsg_sock_create,
	.owner = THIS_MODULE,
};

static void __rpmsg_proto_cb(struct device *dev, int from_vproc_id, void *data,
					int len, struct sock *sk, u32 src)
{
	struct rpmsg_socket *rpsk = container_of(sk, struct rpmsg_socket, sk);
	struct sk_buff *skb;
	int ret;

	print_hex_dump(KERN_DEBUG, __func__, DUMP_PREFIX_NONE, 16, 1,
		       data, len,  true);

	lock_sock(sk);

	switch (sk->sk_state) {
	case RPMSG_CONNECTED:
		if (rpsk->rpdev->dst != src)
			dev_warn(dev, "unexpected source address: %d\n", src);
		break;
	case RPMSG_LISTENING:
		/* When an inbound message is received while we're listening,
		 * we implicitly become connected */
		sk->sk_state = RPMSG_CONNECTED;
		rpsk->rpdev->dst = src;
		break;
	default:
		dev_warn(dev, "unexpected inbound message (from %d)\n", src);
		break;
	}

	skb = sock_alloc_send_skb(sk, len, 1, &ret);
	if (!skb) {
		dev_err(dev, "sock_alloc_send_skb failed: %d\n", ret);
		ret = -ENOMEM;
		goto out;
	}

	RPMSG_CB(skb).vproc_id = from_vproc_id;
	RPMSG_CB(skb).addr = src;
	RPMSG_CB(skb).family = AF_RPMSG;

	memcpy(skb_put(skb, len), data, len);

	ret = sock_queue_rcv_skb(sk, skb);
	if (ret) {
		dev_err(dev, "sock_queue_rcv_skb failed: %d\n", ret);
		kfree_skb(skb);
	}

out:
	release_sock(sk);
}

static void rpmsg_proto_cb(struct rpmsg_channel *rpdev, void *data, int len,
						void *priv, u32 src)
{
	int id = get_virtproc_id(rpdev->vrp);

	__rpmsg_proto_cb(&rpdev->dev, id, data, len, priv, src);
}

/* every channel we're probed with is exposed to userland via the Socket API */
static int rpmsg_proto_probe(struct rpmsg_channel *rpdev)
{
	struct device *dev = &rpdev->dev;
	int ret, dst = rpdev->dst, id;
	struct radix_tree_root *vrp_channels;

	if (dst == RPMSG_ADDR_ANY)
		return 0;

	id = get_virtproc_id(rpdev->vrp);

	mutex_lock(&rpmsg_channels_lock);

	/* are we exposing channels for this remote processor yet ? */
	vrp_channels = radix_tree_lookup(&rpmsg_channels, id);
	/* not yet ? let's prepare the 2nd radix tree level then */
	if (!vrp_channels) {
		vrp_channels = kzalloc(sizeof(*vrp_channels), GFP_KERNEL);
		INIT_RADIX_TREE(vrp_channels, GFP_KERNEL);
		/* now let's associate the new channel with its vrp */
		ret = radix_tree_insert(&rpmsg_channels, id, vrp_channels);
		if (ret) {
			dev_err(dev, "radix_tree_insert failed: %d\n", ret);
			kfree(vrp_channels);
			return ret;
		}
	}

	/* let's associate the new channel with its dst */
	ret = radix_tree_insert(vrp_channels, dst, rpdev);
	if (ret)
		dev_err(dev, "failed to add rpmsg addr %d: %d\n", dst, ret);

	mutex_unlock(&rpmsg_channels_lock);

	return ret;
}

static void rpmsg_proto_remove(struct rpmsg_channel *rpdev)
{
	struct device *dev = &rpdev->dev;
	int id, dst = rpdev->dst;
	struct radix_tree_root *vrp_channels;

	if (dst == RPMSG_ADDR_ANY)
		return;

	id = get_virtproc_id(rpdev->vrp);

	mutex_lock(&rpmsg_channels_lock);

	vrp_channels = radix_tree_lookup(&rpmsg_channels, id);
	if (!vrp_channels) {
		dev_err(dev, "can't find channels for this vrp: %d\n", id);
		goto out;
	}

	if (!radix_tree_delete(vrp_channels, dst))
		dev_err(dev, "failed to delete rpmsg %d\n", dst);

out:
	mutex_unlock(&rpmsg_channels_lock);
}

static struct rpmsg_device_id rpmsg_proto_id_table[] = {
	{ .name	= "rpmsg-proto" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_proto_id_table);

static struct rpmsg_driver rpmsg_proto_drv = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_proto_id_table,
	.probe		= rpmsg_proto_probe,
	.callback	= rpmsg_proto_cb,
	.remove		= rpmsg_proto_remove,
};

int __init rpmsg_proto_init(void)
{
	int ret;

	ret = proto_register(&rpmsg_proto, 0);
	if (ret) {
		pr_err("proto_register failed: %d\n", ret);
		return ret;
	}

	ret = sock_register(&rpmsg_proto_family);
	if (ret) {
		pr_err("sock_register failed: %d\n", ret);
		goto proto_unreg;
	}

	/* gimme rpmsg channels to expose ! */
	ret = register_rpmsg_driver(&rpmsg_proto_drv);
	if (ret) {
		pr_err("register_rpmsg_driver failed: %d\n", ret);
		goto sock_unreg;
	}

	return 0;

sock_unreg:
	sock_unregister(PF_RPMSG);
proto_unreg:
	proto_unregister(&rpmsg_proto);
	return ret;
}

void __exit rpmsg_proto_exit(void)
{
	unregister_rpmsg_driver(&rpmsg_proto_drv);
	sock_unregister(PF_RPMSG);
	proto_unregister(&rpmsg_proto);
}

module_init(rpmsg_proto_init);
module_exit(rpmsg_proto_exit);

MODULE_DESCRIPTION("Remote processor messaging protocol");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_NETPROTO(AF_RPMSG);
