#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/cred.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/poll.h>
#include <linux/skbuff.h>
#include <linux/smp.h>
#include <linux/socket.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <net/inet_common.h>

#include "pvcalls-front.h"

static int
pvcalls_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	int ret;
	ret = pvcalls_front_socket(sock);
	if (ret < 0)
		return ret;
	return pvcalls_front_bind(sock, addr, addr_len);
}

static int pvcalls_stream_connect(struct socket *sock, struct sockaddr *addr,
				int addr_len, int flags)
{
	int ret;
	ret = pvcalls_front_socket(sock);
	if (ret < 0)
		return ret;
	return pvcalls_front_connect(sock, addr, addr_len, flags);
}

static int pvcalls_accept(struct socket *sock, struct socket *newsock, int flags, bool kern)
{
	return pvcalls_front_accept(sock, newsock, flags);
}

static int pvcalls_getname(struct socket *sock,
			 struct sockaddr *uaddr, int peer)
{
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, uaddr);

	sin->sin_family = AF_INET;
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
	return 0;
}

static unsigned int pvcalls_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	return pvcalls_front_poll(file, sock, wait);
}

static int pvcalls_listen(struct socket *sock, int backlog)
{
	return pvcalls_front_listen(sock, backlog);
}

static int pvcalls_stream_sendmsg(struct socket *sock, struct msghdr *msg,
				size_t len)
{
	return pvcalls_front_sendmsg(sock, msg, len);
}

static int
pvcalls_stream_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		     int flags)
{
	return pvcalls_front_recvmsg(sock, msg, len, flags);
}

static int pvcalls_release(struct socket *s)
{
	return pvcalls_front_release(s);
}

static int pvcalls_shutdown(struct socket *s, int h)
{
	return -ENOTSUPP;
}

static int sock_no_setsockopt(struct socket *sock, int level, int optname,
			   sockptr_t optval, unsigned int optlen)
{
	return -ENOTSUPP;
}

static int sock_no_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	return -ENOTSUPP;
}

const struct proto_ops pvcalls_stream_ops = {
	.family = PF_INET,
	.owner = THIS_MODULE,
	.release = pvcalls_release,
	.bind = pvcalls_bind,
	.connect = pvcalls_stream_connect,
	.socketpair = sock_no_socketpair,
	.accept = pvcalls_accept,
	.getname = pvcalls_getname,
	.poll = pvcalls_poll,
	.ioctl = sock_no_ioctl,
	.listen = pvcalls_listen,
	.shutdown = pvcalls_shutdown,
	.setsockopt = sock_no_setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = pvcalls_stream_sendmsg,
	.recvmsg = pvcalls_stream_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

bool pvcalls = false;
static __init int xen_parse_pvcalls(char *arg)
{
       pvcalls = true;
       return 0;
}
early_param("pvcalls", xen_parse_pvcalls);
