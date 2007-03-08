/*
 * @(#) RFC2367 PF_KEYv2 Key management API domain socket I/F
 * Copyright (C) 1999, 2000, 2001  Richard Guy Briggs.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * RCSID $Id: pfkey_v2.c,v 1.97.2.8 2006/07/10 15:56:11 paul Exp $
 */

/*
 *		Template from /usr/src/linux-2.0.36/net/unix/af_unix.c.
 *		Hints from /usr/src/linux-2.0.36/net/ipv4/udp.c.
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/version.h>
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#include <linux/kernel.h>

#include "openswan/ipsec_param.h"

#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h> /* struct socket */
#include <linux/in.h>
#include <linux/fs.h>
#ifdef MALLOC_SLAB
# include <linux/slab.h> /* kmalloc() */
#else /* MALLOC_SLAB */
# include <linux/malloc.h> /* kmalloc() */
#endif /* MALLOC_SLAB */
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h> /* struct sock */
#include <net/protocol.h>
/* #include <net/tcp.h> */
#include <net/af_unix.h>
#ifdef CONFIG_PROC_FS
# include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */

#include <linux/types.h>
 
#include <openswan.h>

#include "openswan/radij.h"
#include "openswan/ipsec_encap.h"
#include "openswan/ipsec_sa.h"

#include <pfkeyv2.h>
#include <pfkey.h>

#include "openswan/ipsec_proto.h"
#include "openswan/ipsec_kern24.h"

#ifdef CONFIG_KLIPS_DEBUG
int debug_pfkey = 0;
extern int sysctl_ipsec_debug_verbose;
#endif /* CONFIG_KLIPS_DEBUG */

#define SENDERR(_x) do { error = -(_x); goto errlab; } while (0)

#ifndef SOCKOPS_WRAPPED
#define SOCKOPS_WRAPPED(name) name
#endif /* SOCKOPS_WRAPPED */

#ifdef NET_26
static rwlock_t pfkey_sock_lock = RW_LOCK_UNLOCKED;
HLIST_HEAD(pfkey_sock_list);
static DECLARE_WAIT_QUEUE_HEAD(pfkey_sock_wait);
static atomic_t pfkey_sock_users = ATOMIC_INIT(0);
#else
struct sock *pfkey_sock_list = NULL;
#endif

struct supported_list *pfkey_supported_list[SADB_SATYPE_MAX+1];

struct socket_list *pfkey_open_sockets = NULL;
struct socket_list *pfkey_registered_sockets[SADB_SATYPE_MAX+1];

int pfkey_msg_interp(struct sock *, struct sadb_msg *, struct sadb_msg **);

DEBUG_NO_STATIC int pfkey_create(struct socket *sock, int protocol);
DEBUG_NO_STATIC int pfkey_shutdown(struct socket *sock, int mode);
DEBUG_NO_STATIC int pfkey_release(struct socket *sock);

#ifdef NET_26
DEBUG_NO_STATIC int pfkey_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len);
DEBUG_NO_STATIC int pfkey_recvmsg(struct kiocb *kiocb, struct socket *sock, struct msghdr *msg
				  , size_t size, int flags);
#else
DEBUG_NO_STATIC int pfkey_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm);
DEBUG_NO_STATIC int pfkey_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags, struct scm_cookie *scm);
#endif

struct net_proto_family pfkey_family_ops = {
	PF_KEY,
	pfkey_create
};

struct proto_ops SOCKOPS_WRAPPED(pfkey_ops) = {
#ifdef NETDEV_23
	family:		PF_KEY,
	release:	pfkey_release,
	bind:		sock_no_bind,
	connect:	sock_no_connect,
	socketpair:	sock_no_socketpair,
	accept:		sock_no_accept,
	getname:	sock_no_getname,
	poll:		datagram_poll,
	ioctl:		sock_no_ioctl,
	listen:		sock_no_listen,
	shutdown:	pfkey_shutdown,
	setsockopt:	sock_no_setsockopt,
	getsockopt:	sock_no_getsockopt,
	sendmsg:	pfkey_sendmsg,
	recvmsg:	pfkey_recvmsg,
	mmap:		sock_no_mmap,
#else /* NETDEV_23 */
	PF_KEY,
	sock_no_dup,
	pfkey_release,
	sock_no_bind,
	sock_no_connect,
	sock_no_socketpair,
	sock_no_accept,
	sock_no_getname,
	datagram_poll,
	sock_no_ioctl,
	sock_no_listen,
	pfkey_shutdown,
	sock_no_setsockopt,
	sock_no_getsockopt,
	sock_no_fcntl,
	pfkey_sendmsg,
	pfkey_recvmsg
#endif /* NETDEV_23 */
};

#ifdef NETDEV_23
#include <linux/smp_lock.h>
SOCKOPS_WRAP(pfkey, PF_KEY);
#endif  /* NETDEV_23 */

#ifdef NET_26
static void pfkey_sock_list_grab(void)
{
	write_lock_bh(&pfkey_sock_lock);

	if (atomic_read(&pfkey_sock_users)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue_exclusive(&pfkey_sock_wait, &wait);
		for(;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (atomic_read(&pfkey_sock_users) == 0)
				break;
			write_unlock_bh(&pfkey_sock_lock);
			schedule();
			write_lock_bh(&pfkey_sock_lock);
		}

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&pfkey_sock_wait, &wait);
	}
}

static __inline__ void pfkey_sock_list_ungrab(void)
{
	write_unlock_bh(&pfkey_sock_lock);
	wake_up(&pfkey_sock_wait);
}

static __inline__ void pfkey_lock_sock_list(void)
{
	/* read_lock() synchronizes us to pfkey_table_grab */

	read_lock(&pfkey_sock_lock);
	atomic_inc(&pfkey_sock_users);
	read_unlock(&pfkey_sock_lock);
}

static __inline__ void pfkey_unlock_sock_list(void)
{
	if (atomic_dec_and_test(&pfkey_sock_users))
		wake_up(&pfkey_sock_wait);
}
#endif

int
pfkey_list_remove_socket(struct socket *socketp, struct socket_list **sockets)
{
	struct socket_list *socket_listp,*prev;

	if(!socketp) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_remove_socket: "
			    "NULL socketp handed in, failed.\n");
		return -EINVAL;
	}

	if(!sockets) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_remove_socket: "
			    "NULL sockets list handed in, failed.\n");
		return -EINVAL;
	}

	socket_listp = *sockets;
	prev = NULL;
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_list_remove_socket: "
		    "removing sock=0p%p\n",
		    socketp);
	
	while(socket_listp != NULL) {
		if(socket_listp->socketp == socketp) {
			if(prev != NULL) {
				prev->next = socket_listp->next;
			} else {
				*sockets = socket_listp->next;
			}
			
			kfree((void*)socket_listp);
			
			break;
		}
		prev = socket_listp;
		socket_listp = socket_listp->next;
	}

	return 0;
}

int
pfkey_list_insert_socket(struct socket *socketp, struct socket_list **sockets)
{
	struct socket_list *socket_listp;

	if(!socketp) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_socket: "
			    "NULL socketp handed in, failed.\n");
		return -EINVAL;
	}

	if(!sockets) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_socket: "
			    "NULL sockets list handed in, failed.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_list_insert_socket: "
		    "allocating %lu bytes for socketp=0p%p\n",
		    (unsigned long) sizeof(struct socket_list),
		    socketp);
	
	if((socket_listp = (struct socket_list *)kmalloc(sizeof(struct socket_list), GFP_KERNEL)) == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_socket: "
			    "memory allocation error.\n");
		return -ENOMEM;
	}
	
	socket_listp->socketp = socketp;
	socket_listp->next = *sockets;
	*sockets = socket_listp;

	return 0;
}
  
int
pfkey_list_remove_supported(struct ipsec_alg_supported *supported, struct supported_list **supported_list)
{
	struct supported_list *supported_listp = *supported_list, *prev = NULL;
	
	if(!supported) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_remove_supported: "
			    "NULL supported handed in, failed.\n");
		return -EINVAL;
	}

	if(!supported_list) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_remove_supported: "
			    "NULL supported_list handed in, failed.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_list_remove_supported: "
		    "removing supported=0p%p\n",
		    supported);
	
	while(supported_listp != NULL) {
		if(supported_listp->supportedp == supported) {
			if(prev != NULL) {
				prev->next = supported_listp->next;
			} else {
				*supported_list = supported_listp->next;
			}
			
			kfree((void*)supported_listp);
			
			break;
		}
		prev = supported_listp;
		supported_listp = supported_listp->next;
	}

	return 0;
}

int
pfkey_list_insert_supported(struct ipsec_alg_supported *supported
			    , struct supported_list **supported_list)
{
	struct supported_list *supported_listp;

	if(!supported) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_supported: "
			    "NULL supported handed in, failed.\n");
		return -EINVAL;
	}

	if(!supported_list) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_supported: "
			    "NULL supported_list handed in, failed.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_list_insert_supported: "
		    "allocating %lu bytes for incoming, supported=0p%p, supported_list=0p%p\n",
		    (unsigned long) sizeof(struct supported_list),
		    supported,
		    supported_list);
	
	supported_listp = (struct supported_list *)kmalloc(sizeof(struct supported_list), GFP_KERNEL);

	if(supported_listp == NULL)
	{
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_list_insert_supported: "
			    "memory allocation error.\n");
		return -ENOMEM;
	}
	
	supported_listp->supportedp = supported;
	supported_listp->next = *supported_list;
	*supported_list = supported_listp;
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_list_insert_supported: "
		    "outgoing, supported=0p%p, supported_list=0p%p\n",
		    supported,
		    supported_list);

	return 0;
}
  
#ifdef NET_26
DEBUG_NO_STATIC void
pfkey_insert_socket(struct sock *sk)
{
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_insert_socket: "
		    "sk=0p%p\n",
		    sk);
	pfkey_sock_list_grab();
	sk_add_node(sk, &pfkey_sock_list);
	pfkey_sock_list_ungrab();
}

DEBUG_NO_STATIC void
pfkey_remove_socket(struct sock *sk)
{
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_remove_socket: 0p%p\n", sk);
	pfkey_sock_list_grab();
	sk_del_node_init(sk);
	pfkey_sock_list_ungrab();
	return;
}
#else

DEBUG_NO_STATIC void
pfkey_insert_socket(struct sock *sk)
{
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_insert_socket: "
		    "sk=0p%p\n",
		    sk);
	cli();
	sk->next=pfkey_sock_list;
	pfkey_sock_list=sk;
	sti();
}
DEBUG_NO_STATIC void
pfkey_remove_socket(struct sock *sk)
{
	struct sock **s;

	s = NULL;
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_remove_socket: .\n");

	cli();
	s=&pfkey_sock_list;

	while(*s!=NULL) {
		if(*s==sk) {
			*s=sk->next;
			sk->next=NULL;
			sti();
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_remove_socket: "
				    "succeeded.\n");
			return;
		}
		s=&((*s)->next);
	}
	sti();

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_remove_socket: "
		    "not found.\n");
	return;
}
#endif

DEBUG_NO_STATIC void
pfkey_destroy_socket(struct sock *sk)
{
	struct sk_buff *skb;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_destroy_socket: 0p%p\n",sk);
	pfkey_remove_socket(sk);

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_destroy_socket: "
		    "pfkey_remove_socket called, sk=0p%p\n",sk);
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_destroy_socket: "
		    "sk(0p%p)->(&0p%p)receive_queue.{next=0p%p,prev=0p%p}.\n",
		    sk,
		    &(sk->sk_receive_queue),
		    sk->sk_receive_queue.next,
		    sk->sk_receive_queue.prev);

	while(sk && ((skb=skb_dequeue(&(sk->sk_receive_queue)))!=NULL)) {
#ifdef CONFIG_KLIPS_DEBUG
		if(debug_pfkey && sysctl_ipsec_debug_verbose) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_destroy_socket: "
				    "skb=0p%p dequeued.\n", skb);
			printk(KERN_INFO "klips_debug:pfkey_destroy_socket: "
			       "pfkey_skb contents:");
			printk(" next:0p%p", skb->next);
			printk(" prev:0p%p", skb->prev);
			printk(" sk:0p%p", skb->sk);
			printk(" dev:0p%p", skb->dev);
			if(skb->dev) {
				if(skb->dev->name) {
					printk(" dev->name:%s", skb->dev->name);
				} else {
					printk(" dev->name:NULL?");
				}
			} else {
				printk(" dev:NULL");
			}
			printk(" h:0p%p", skb->h.raw);
			printk(" nh:0p%p", skb->nh.raw);
			printk(" mac:0p%p", skb->mac.raw);
			printk(" dst:0p%p", skb->dst);
			if(sysctl_ipsec_debug_verbose) {
				int i;
				
				printk(" cb");
				for(i=0; i<48; i++) {
					printk(":%2x", skb->cb[i]);
				}
			}
			printk(" len:%d", skb->len);
			printk(" csum:%d", skb->csum);
#ifndef NETDEV_23
			printk(" used:%d", skb->used);
			printk(" is_clone:%d", skb->is_clone);
#endif /* NETDEV_23 */
			printk(" cloned:%d", skb->cloned);
			printk(" pkt_type:%d", skb->pkt_type);
			printk(" ip_summed:%d", skb->ip_summed);
			printk(" priority:%d", skb->priority);
			printk(" protocol:%d", skb->protocol);
#ifdef HAVE_SOCK_SECURITY
			printk(" security:%d", skb->security);
#endif
			printk(" truesize:%d", skb->truesize);
			printk(" head:0p%p", skb->head);
			printk(" data:0p%p", skb->data);
			printk(" tail:0p%p", skb->tail);
			printk(" end:0p%p", skb->end);
			if(sysctl_ipsec_debug_verbose) {
				unsigned char* i;
				printk(" data");
				for(i = skb->head; i < skb->end; i++) {
					printk(":%2x", (unsigned char)(*(i)));
				}
			}
			printk(" destructor:0p%p", skb->destructor);
			printk("\n");
		}
#endif /* CONFIG_KLIPS_DEBUG */
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_destroy_socket: "
			    "skb=0p%p freed.\n",
			    skb);
		ipsec_kfree_skb(skb);
	}

#ifdef NET_26
	sock_set_flag(sk, SOCK_DEAD);
#else
	sk->dead = 1;
#endif
	sk_free(sk);

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_destroy_socket: destroyed.\n");
}

int
pfkey_upmsg(struct socket *sock, struct sadb_msg *pfkey_msg)
{
	int error = 0;
	struct sk_buff * skb = NULL;
	struct sock *sk;

	if(sock == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_upmsg: "
			    "NULL socket passed in.\n");
		return -EINVAL;
	}

	if(pfkey_msg == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_upmsg: "
			    "NULL pfkey_msg passed in.\n");
		return -EINVAL;
	}

	sk = sock->sk;

	if(sk == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_upmsg: "
			    "NULL sock passed in.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_upmsg: "
		    "allocating %d bytes...\n",
		    (int)(pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN));
	if(!(skb = alloc_skb(pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN, GFP_ATOMIC) )) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_upmsg: "
			    "no buffers left to send up a message.\n");
		return -ENOBUFS;
	}
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_upmsg: "
		    "...allocated at 0p%p.\n",
		    skb);
	
	skb->dev = NULL;
	
	if(skb_tailroom(skb) < pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN) {
		printk(KERN_WARNING "klips_error:pfkey_upmsg: "
		       "tried to skb_put %ld, %d available.  This should never happen, please report.\n",
		       (unsigned long int)pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN,
		       skb_tailroom(skb));
		ipsec_kfree_skb(skb);
		return -ENOBUFS;
	}
	skb->h.raw = skb_put(skb, pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN);
	memcpy(skb->h.raw, pfkey_msg, pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN);

	if((error = sock_queue_rcv_skb(sk, skb)) < 0) {
		skb->sk=NULL;
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_upmsg: "
			    "error=%d calling sock_queue_rcv_skb with skb=0p%p.\n",
			    error,
			    skb);
		ipsec_kfree_skb(skb);
		return error;
	}
	return error;
}

#ifdef NET_26_12_SKALLOC
static struct proto key_proto = {
	.name	  = "KEY",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct sock),
	
};
#endif

DEBUG_NO_STATIC int
pfkey_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	if(sock == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_create: "
			    "socket NULL.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_create: "
		    "sock=0p%p type:%d state:%d flags:%ld protocol:%d\n",
		    sock,
		    sock->type,
		    (unsigned int)(sock->state),
		    sock->flags, protocol);

	if(sock->type != SOCK_RAW) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_create: "
			    "only SOCK_RAW supported.\n");
		return -ESOCKTNOSUPPORT;
	}

	if(protocol != PF_KEY_V2) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_create: "
			    "protocol not PF_KEY_V2.\n");
		return -EPROTONOSUPPORT;
	}

	if((current->uid != 0)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_create: "
			    "must be root to open pfkey sockets.\n");
		return -EACCES;
	}

	sock->state = SS_UNCONNECTED;

	KLIPS_INC_USE;

#ifdef NET_26
#ifdef NET_26_12_SKALLOC
	sk=(struct sock *)sk_alloc(PF_KEY, GFP_KERNEL, &key_proto, 1);
#else
	sk=(struct sock *)sk_alloc(PF_KEY, GFP_KERNEL, 1, NULL);
#endif
#else
	/* 2.4 interface */
	sk=(struct sock *)sk_alloc(PF_KEY, GFP_KERNEL, 1);
#endif

	if(sk == NULL)
	{
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_create: "
			    "Out of memory trying to allocate.\n");
		KLIPS_DEC_USE;
		return -ENOMEM;
	}

	sock_init_data(sock, sk);

	sk->sk_destruct = NULL;
	sk->sk_reuse = 1;
	sock->ops = &pfkey_ops;

	sk->sk_family = PF_KEY;
/*	sk->num = protocol; */
	sk->sk_protocol = protocol;
	key_pid(sk) = current->pid;
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_create: "
		    "sock->fasync_list=0p%p sk->sleep=0p%p.\n",
		    sock->fasync_list,
		    sk->sk_sleep);

	pfkey_insert_socket(sk);
	pfkey_list_insert_socket(sock, &pfkey_open_sockets);

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_create: "
		    "Socket sock=0p%p sk=0p%p initialised.\n", sock, sk);
	return 0;
}

DEBUG_NO_STATIC int
#ifdef NETDEV_23
pfkey_release(struct socket *sock)
#else /* NETDEV_23 */
pfkey_release(struct socket *sock, struct socket *peersock)
#endif /* NETDEV_23 */
{
	struct sock *sk;
	int i;

	if(sock==NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_release: "
			    "No socket attached.\n");
		return 0; /* -EINVAL; */
	}
		
	sk=sock->sk;
	
	/* May not have data attached */
	if(sk==NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_release: "
			    "No sk attached to sock=0p%p.\n", sock);
		return 0; /* -EINVAL; */
	}
		
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_release: "
		    "sock=0p%p sk=0p%p\n", sock, sk);

	if(sock_flag(sk, SOCK_DEAD))
		if(sk->sk_state_change) {
			sk->sk_state_change(sk);
		}

	sock->sk = NULL;

	/* Try to flush out this socket. Throw out buffers at least */
	pfkey_destroy_socket(sk);
	pfkey_list_remove_socket(sock, &pfkey_open_sockets);
	for(i = SADB_SATYPE_UNSPEC; i <= SADB_SATYPE_MAX; i++) {
		pfkey_list_remove_socket(sock, &(pfkey_registered_sockets[i]));
	}

	KLIPS_DEC_USE;
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_release: "
		    "succeeded.\n");

	return 0;
}

DEBUG_NO_STATIC int
pfkey_shutdown(struct socket *sock, int mode)
{
	struct sock *sk;

	if(sock == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_shutdown: "
			    "NULL socket passed in.\n");
		return -EINVAL;
	}

	sk=sock->sk;
	
	if(sk == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_shutdown: "
			    "No sock attached to socket.\n");
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_shutdown: "
		    "mode=%x.\n", mode);
	mode++;
	
	if(mode&SEND_SHUTDOWN) {
		sk->sk_shutdown|=SEND_SHUTDOWN;
		sk->sk_state_change(sk);
	}

	if(mode&RCV_SHUTDOWN) {
		sk->sk_shutdown|=RCV_SHUTDOWN;
		sk->sk_state_change(sk);
	}
	return 0;
}

/*
 *	Send PF_KEY data down.
 */
		
DEBUG_NO_STATIC int
#ifdef NET_26
pfkey_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len)
#else
pfkey_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm)
#endif
{
	struct sock *sk;
	int error = 0;
	struct sadb_msg *pfkey_msg = NULL, *pfkey_reply = NULL;
	
	if(sock == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "Null socket passed in.\n");
		SENDERR(EINVAL);
	}
	
	sk = sock->sk;

	if(sk == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "Null sock passed in.\n");
		SENDERR(EINVAL);
	}
	
	if(msg == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "Null msghdr passed in.\n");
		SENDERR(EINVAL);
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_sendmsg: .\n");
	if(sk->sk_err) {
		error = sock_error(sk);
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "sk->err is non-zero, returns %d.\n",
			    error);
		SENDERR(-error);
	}

	if((current->uid != 0)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "must be root to send messages to pfkey sockets.\n");
		SENDERR(EACCES);
	}

	if(msg->msg_control)
	{
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "can't set flags or set msg_control.\n");
		SENDERR(EINVAL);
	}
		
	if(sk->sk_shutdown & SEND_SHUTDOWN) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "shutdown.\n");
		send_sig(SIGPIPE, current, 0);
		SENDERR(EPIPE);
	}
	
	if(len < sizeof(struct sadb_msg)) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "bogus msg len of %d, too small.\n", (int)len);
		SENDERR(EMSGSIZE);
	}

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_sendmsg: "
		    "allocating %d bytes for downward message.\n",
		    (int)len);
	if((pfkey_msg = (struct sadb_msg*)kmalloc(len, GFP_KERNEL)) == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "memory allocation error.\n");
		SENDERR(ENOBUFS);
	}

	memcpy_fromiovec((void *)pfkey_msg, msg->msg_iov, len);

	if(pfkey_msg->sadb_msg_version != PF_KEY_V2) {
		KLIPS_PRINT(1 || debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "not PF_KEY_V2 msg, found %d, should be %d.\n",
			    pfkey_msg->sadb_msg_version,
			    PF_KEY_V2);
		kfree((void*)pfkey_msg);
		return -EINVAL;
	}

	if(len != pfkey_msg->sadb_msg_len * IPSEC_PFKEYv2_ALIGN) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "bogus msg len of %d, not %d byte aligned.\n",
			    (int)len, (int)IPSEC_PFKEYv2_ALIGN);
		SENDERR(EMSGSIZE);
	}

#if 0
	/* This check is questionable, since a downward message could be
	   the result of an ACQUIRE either from kernel (PID==0) or
	   userspace (some other PID). */
	/* check PID */
	if(pfkey_msg->sadb_msg_pid != current->pid) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "pid (%d) does not equal sending process pid (%d).\n",
			    pfkey_msg->sadb_msg_pid, current->pid);
		SENDERR(EINVAL);
	}
#endif

	if(pfkey_msg->sadb_msg_reserved) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "reserved field must be zero, set to %d.\n",
			    pfkey_msg->sadb_msg_reserved);
		SENDERR(EINVAL);
	}
	
	if((pfkey_msg->sadb_msg_type > SADB_MAX) || (!pfkey_msg->sadb_msg_type)){
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "msg type too large or small:%d.\n",
			    pfkey_msg->sadb_msg_type);
		SENDERR(EINVAL);
	}
	
	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:pfkey_sendmsg: "
		    "msg sent for parsing.\n");
	
	if((error = pfkey_msg_interp(sk, pfkey_msg, &pfkey_reply))) {
		struct socket_list *pfkey_socketsp;

		KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_sendmsg: "
			    "pfkey_msg_parse returns %d.\n",
			    error);

		if((pfkey_reply = (struct sadb_msg*)kmalloc(sizeof(struct sadb_msg), GFP_KERNEL)) == NULL) {
			KLIPS_PRINT(debug_pfkey,
				    "klips_debug:pfkey_sendmsg: "
				    "memory allocation error.\n");
			SENDERR(ENOBUFS);
		}
		memcpy((void*)pfkey_reply, (void*)pfkey_msg, sizeof(struct sadb_msg));
		pfkey_reply->sadb_msg_errno = -error;
		pfkey_reply->sadb_msg_len = sizeof(struct sadb_msg) / IPSEC_PFKEYv2_ALIGN;

		for(pfkey_socketsp = pfkey_open_sockets;
		    pfkey_socketsp;
		    pfkey_socketsp = pfkey_socketsp->next) {
			int error_upmsg = 0;
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_sendmsg: "
				    "sending up error=%d message=0p%p to socket=0p%p.\n",
				    error,
				    pfkey_reply,
				    pfkey_socketsp->socketp);
			if((error_upmsg = pfkey_upmsg(pfkey_socketsp->socketp, pfkey_reply))) {
				KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_sendmsg: "
					    "sending up error message to socket=0p%p failed with error=%d.\n",
					    pfkey_socketsp->socketp,
					    error_upmsg);
				/* pfkey_msg_free(&pfkey_reply); */
				/* SENDERR(-error); */
			}
			KLIPS_PRINT(debug_pfkey, "klips_debug:pfkey_sendmsg: "
				    "sending up error message to socket=0p%p succeeded.\n",
				    pfkey_socketsp->socketp);
		}
		
		pfkey_msg_free(&pfkey_reply);
		
		SENDERR(-error);
	}

 errlab:
	if (pfkey_msg) {
		kfree((void*)pfkey_msg);
	}
	
	if(error) {
		return error;
	} else {
		return len;
	}
}

/*
 *	Receive PF_KEY data up.
 */
		
DEBUG_NO_STATIC int
#ifdef NET_26
pfkey_recvmsg(struct kiocb *kiocb
	      , struct socket *sock
	      , struct msghdr *msg
	      , size_t size
	      , int flags)
#else
pfkey_recvmsg(struct socket *sock
	      , struct msghdr *msg
	      , int size, int flags
	      , struct scm_cookie *scm)
#endif
{
	struct sock *sk;
	int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	int error;

	if(sock == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_recvmsg: "
			    "Null socket passed in.\n");
		return -EINVAL;
	}

	sk = sock->sk;

	if(sk == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_recvmsg: "
			    "Null sock passed in for sock=0p%p.\n", sock);
		return -EINVAL;
	}

	if(msg == NULL) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_recvmsg: "
			    "Null msghdr passed in for sock=0p%p, sk=0p%p.\n",
			    sock, sk);
		return -EINVAL;
	}

	KLIPS_PRINT(debug_pfkey && sysctl_ipsec_debug_verbose,
		    "klips_debug:pfkey_recvmsg: sock=0p%p sk=0p%p msg=0p%p size=%d.\n",
		    sock, sk, msg, (int)size);
	if(flags & ~MSG_PEEK) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "flags (%d) other than MSG_PEEK not supported.\n",
			    flags);
		return -EOPNOTSUPP;
	}
		
	msg->msg_namelen = 0; /* sizeof(*ska); */
		
	if(sk->sk_err) {
		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:pfkey_sendmsg: "
			    "sk->sk_err=%d.\n", sk->sk_err);
		return sock_error(sk);
	}

	if((skb = skb_recv_datagram(sk, flags, noblock, &error) ) == NULL) {
                return error;
	}

	if(size > skb->len) {
		size = skb->len;
	}
	else if(size <skb->len) {
		msg->msg_flags |= MSG_TRUNC;
	}

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, size);
#ifdef HAVE_TSTAMP
	sk->sk_stamp.tv_sec  = skb->tstamp.off_sec;
	sk->sk_stamp.tv_usec = skb->tstamp.off_usec;
#else
        sk->sk_stamp=skb->stamp;
#endif

	skb_free_datagram(sk, skb);
	return size;
}

#ifdef CONFIG_PROC_FS
#ifndef PROC_FS_2325
DEBUG_NO_STATIC
#endif /* PROC_FS_2325 */
int
pfkey_get_info(char *buffer, char **start, off_t offset, int length
#ifndef  PROC_NO_DUMMY
, int dummy
#endif /* !PROC_NO_DUMMY */
)
{
	const int max_content = length > 0? length-1 : 0;	/* limit of useful snprintf output */
#ifdef NET_26
	struct hlist_node *node;
#endif
	off_t begin=0;
	int len=0;
	struct sock *sk;
	
#ifdef CONFIG_KLIPS_DEBUG
	if(!sysctl_ipsec_debug_verbose) {
#endif /* CONFIG_KLIPS_DEBUG */
	len += ipsec_snprintf(buffer, length,
		      "    sock   pid   socket     next     prev e n p sndbf    Flags     Type St\n");
#ifdef CONFIG_KLIPS_DEBUG
	} else {
	len += ipsec_snprintf(buffer, length,
		      "    sock   pid d    sleep   socket     next     prev e r z n p sndbf    stamp    Flags     Type St\n");
	}
#endif /* CONFIG_KLIPS_DEBUG */

	sk_for_each(sk, node, &pfkey_sock_list) {

#ifdef CONFIG_KLIPS_DEBUG
		if(!sysctl_ipsec_debug_verbose) {
#endif /* CONFIG_KLIPS_DEBUG */
		  len += ipsec_snprintf(buffer+len, length-len,
					"%8p %5d %8p %d %d %5d %08lX %8X %2X\n",
					sk,
					key_pid(sk),
					sk->sk_socket,
					sk->sk_err,
					sk->sk_protocol,
					sk->sk_sndbuf,
					sk->sk_socket->flags,
					sk->sk_socket->type,
					sk->sk_socket->state);
#ifdef CONFIG_KLIPS_DEBUG
		} else {
		  len += ipsec_snprintf(buffer+len, length-len,
					"%8p %5d %d %8p %8p %d %d %d %d %5d %d.%06d %08lX %8X %2X\n",
					sk,
					key_pid(sk),
					sock_flag(sk, SOCK_DEAD),
					sk->sk_sleep,
					sk->sk_socket,
					sk->sk_err,
					sk->sk_reuse,
#ifdef HAVE_SOCK_ZAPPED
					sock_flag(sk, SOCK_ZAPPED),
#else
					sk->sk_zapped,
#endif					
					sk->sk_protocol,
					sk->sk_sndbuf,
					(unsigned int)sk->sk_stamp.tv_sec,
					(unsigned int)sk->sk_stamp.tv_usec,
					sk->sk_socket->flags,
					sk->sk_socket->type,
					sk->sk_socket->state);
		}
#endif /* CONFIG_KLIPS_DEBUG */
		
		if (len >= max_content) {
			/* we've done all that can fit -- stop loop */
			len = max_content;	/* truncate crap */
			break;
		} else {
			const off_t pos = begin + len;	/* file position of end of what we've generated */

			if (pos <= offset) {
				/* all is before first interesting character:
				 * discard, but note where we are.
				 */
				len = 0;
				begin = pos;
			}
		}
	}

	*start = buffer + (offset - begin);	/* Start of wanted data */
	return len - (offset - begin);
}

#ifndef PROC_FS_2325
DEBUG_NO_STATIC
#endif /* PROC_FS_2325 */
int
pfkey_supported_get_info(char *buffer, char **start, off_t offset, int length
#ifndef  PROC_NO_DUMMY
, int dummy
#endif /* !PROC_NO_DUMMY */
)
{
	/* limit of useful snprintf output */
	const int max_content = length > 0? length-1 : 0;	
	off_t begin=0;
	int len=0;
	int satype;
	struct supported_list *ps;
	
	len += ipsec_snprintf(buffer, length,
		      "satype exttype alg_id ivlen minbits maxbits name\n");
	
	for(satype = SADB_SATYPE_UNSPEC; satype <= SADB_SATYPE_MAX; satype++) {
		ps = pfkey_supported_list[satype];
		while(ps) {
			struct ipsec_alg_supported *alg = ps->supportedp;
			unsigned char *n = alg->ias_name;
			if(n == NULL) n = "unknown";

			len += ipsec_snprintf(buffer+len, length-len,
					      "    %2d      %2d     %2d   %3d     %3d     %3d %20s\n",
					      satype,
					      alg->ias_exttype,
					      alg->ias_id,
					      alg->ias_ivlen,
					      alg->ias_keyminbits,
					      alg->ias_keymaxbits,
					      n);
			
			if (len >= max_content) {
				/* we've done all that can fit -- stop loop */
				len = max_content;	/* truncate crap */
				break;
			} else {
				const off_t pos = begin + len;	/* file position of end of what we've generated */

				if (pos <= offset) {
					/* all is before first interesting character:
					 * discard, but note where we are.
					 */
					len = 0;
					begin = pos;
				}
			}

			ps = ps->next;
		}
	}
	*start = buffer + (offset - begin);	/* Start of wanted data */
	return len - (offset - begin);
}

#ifndef PROC_FS_2325
DEBUG_NO_STATIC
#endif /* PROC_FS_2325 */
int
pfkey_registered_get_info(char *buffer, char **start, off_t offset, int length
#ifndef  PROC_NO_DUMMY
, int dummy
#endif /* !PROC_NO_DUMMY */
)
{
	const int max_content = length > 0? length-1 : 0;	/* limit of useful snprintf output */
	off_t begin=0;
	int len=0;
	int satype;
	struct socket_list *pfkey_sockets;
	
	len += ipsec_snprintf(buffer, length,
		      "satype   socket   pid       sk\n");
	
	for(satype = SADB_SATYPE_UNSPEC; satype <= SADB_SATYPE_MAX; satype++) {
		pfkey_sockets = pfkey_registered_sockets[satype];
		while(pfkey_sockets) {
			len += ipsec_snprintf(buffer+len, length-len,
				     "    %2d %8p %5d %8p\n",
				     satype,
				     pfkey_sockets->socketp,
				     key_pid(pfkey_sockets->socketp->sk),
				     pfkey_sockets->socketp->sk);
			
			if (len >= max_content) {
				/* we've done all that can fit -- stop loop (could stop two) */
				len = max_content;	/* truncate crap */
				break;
			} else {
				const off_t pos = begin + len;	/* file position of end of what we've generated */

				if (pos <= offset) {
					/* all is before first interesting character:
					 * discard, but note where we are.
					 */
					len = 0;
					begin = pos;
				}
			}

			pfkey_sockets = pfkey_sockets->next;
		}
	}
	*start = buffer + (offset - begin);	/* Start of wanted data */
	return len - (offset - begin);
}

#ifndef PROC_FS_2325
struct proc_dir_entry proc_net_pfkey =
{
	0,
	6, "pf_key",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	pfkey_get_info
};
struct proc_dir_entry proc_net_pfkey_supported =
{
	0,
	16, "pf_key_supported",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	pfkey_supported_get_info
};
struct proc_dir_entry proc_net_pfkey_registered =
{
	0,
	17, "pf_key_registered",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	pfkey_registered_get_info
};
#endif /* !PROC_FS_2325 */
#endif /* CONFIG_PROC_FS */

DEBUG_NO_STATIC int
supported_add_all(int satype, struct ipsec_alg_supported supported[], int size)
{
	int i;
	int error = 0;

	KLIPS_PRINT(debug_pfkey,
		    "klips_debug:init_pfkey: "
		    "sizeof(supported_init_<satype=%d>)[%d]/sizeof(struct ipsec_alg_supported)[%d]=%d.\n",
		    satype,
		    size,
		    (int)sizeof(struct ipsec_alg_supported),
		    (int)(size/sizeof(struct ipsec_alg_supported)));

	for(i = 0; i < size / sizeof(struct ipsec_alg_supported); i++) {

		unsigned char *n = supported[i].ias_name;
		if(n == NULL) n="unknown";

		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:init_pfkey: "
			    "i=%d inserting satype=%d exttype=%d id=%d ivlen=%d minbits=%d maxbits=%d name=%s.\n",
			    i,
			    satype,
			    supported[i].ias_exttype,
			    supported[i].ias_id,
			    supported[i].ias_ivlen,
			    supported[i].ias_keyminbits,
			    supported[i].ias_keymaxbits,
			    n);			    
			    
		error |= pfkey_list_insert_supported(&(supported[i]),
					    &(pfkey_supported_list[satype]));
	}
	return error;
}

DEBUG_NO_STATIC int
supported_remove_all(int satype)
{
	int error = 0;
	struct ipsec_alg_supported*supportedp;

	while(pfkey_supported_list[satype]) {
		unsigned char *n;
		supportedp = pfkey_supported_list[satype]->supportedp;

		n = supportedp->ias_name;
		if(n == NULL) n="unknown";

		KLIPS_PRINT(debug_pfkey,
			    "klips_debug:init_pfkey: "
			    "removing satype=%d exttype=%d id=%d ivlen=%d minbits=%d maxbits=%d name=%s.\n",
			    satype,
			    supportedp->ias_exttype,
			    supportedp->ias_id,
			    supportedp->ias_ivlen,
			    supportedp->ias_keyminbits,
			    supportedp->ias_keymaxbits, n);
			    
		error |= pfkey_list_remove_supported(supportedp,
					    &(pfkey_supported_list[satype]));
	}
	return error;
}

int
pfkey_init(void)
{
	int error = 0;
	int i;
	
	static struct ipsec_alg_supported supported_init_ah[] = {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		{SADB_EXT_SUPPORTED_AUTH, SADB_AALG_MD5HMAC, 0, 128, 128},
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		{SADB_EXT_SUPPORTED_AUTH, SADB_AALG_SHA1HMAC, 0, 160, 160}
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
	};
	static struct ipsec_alg_supported supported_init_esp[] = {
#ifdef CONFIG_KLIPS_AUTH_HMAC_MD5
		{SADB_EXT_SUPPORTED_AUTH, SADB_AALG_MD5HMAC, 0, 128, 128},
#endif /* CONFIG_KLIPS_AUTH_HMAC_MD5 */
#ifdef CONFIG_KLIPS_AUTH_HMAC_SHA1
		{SADB_EXT_SUPPORTED_AUTH, SADB_AALG_SHA1HMAC, 0, 160, 160},
#endif /* CONFIG_KLIPS_AUTH_HMAC_SHA1 */
#ifdef CONFIG_KLIPS_ENC_3DES
		{SADB_EXT_SUPPORTED_ENCRYPT, SADB_EALG_3DESCBC, 64, 168, 168},
#endif /* CONFIG_KLIPS_ENC_3DES */
	};
	static struct ipsec_alg_supported supported_init_ipip[] = {
		{SADB_EXT_SUPPORTED_ENCRYPT, SADB_X_TALG_IPv4_in_IPv4, 0, 32, 32}
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		, {SADB_EXT_SUPPORTED_ENCRYPT, SADB_X_TALG_IPv6_in_IPv4, 0, 128, 32}
		, {SADB_EXT_SUPPORTED_ENCRYPT, SADB_X_TALG_IPv4_in_IPv6, 0, 32, 128}
		, {SADB_EXT_SUPPORTED_ENCRYPT, SADB_X_TALG_IPv6_in_IPv6, 0, 128, 128}
#endif /* defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE) */
	};
#ifdef CONFIG_KLIPS_IPCOMP
	static struct ipsec_alg_supported supported_init_ipcomp[] = {
		{SADB_EXT_SUPPORTED_ENCRYPT, SADB_X_CALG_DEFLATE, 0, 1, 1}
	};
#endif /* CONFIG_KLIPS_IPCOMP */

#if 0
        printk(KERN_INFO
	       "klips_info:pfkey_init: "
	       "FreeS/WAN: initialising PF_KEYv2 domain sockets.\n");
#endif

	for(i = SADB_SATYPE_UNSPEC; i <= SADB_SATYPE_MAX; i++) {
		pfkey_registered_sockets[i] = NULL;
		pfkey_supported_list[i] = NULL;
	}

	error |= supported_add_all(SADB_SATYPE_AH, supported_init_ah, sizeof(supported_init_ah));
	error |= supported_add_all(SADB_SATYPE_ESP, supported_init_esp, sizeof(supported_init_esp));
#ifdef CONFIG_KLIPS_IPCOMP
	error |= supported_add_all(SADB_X_SATYPE_COMP, supported_init_ipcomp, sizeof(supported_init_ipcomp));
#endif /* CONFIG_KLIPS_IPCOMP */
	error |= supported_add_all(SADB_X_SATYPE_IPIP, supported_init_ipip, sizeof(supported_init_ipip));

        error |= sock_register(&pfkey_family_ops);

#ifdef CONFIG_PROC_FS
#  ifndef PROC_FS_2325
#    ifdef PROC_FS_21
	error |= proc_register(proc_net, &proc_net_pfkey);
	error |= proc_register(proc_net, &proc_net_pfkey_supported);
	error |= proc_register(proc_net, &proc_net_pfkey_registered);
#    else /* PROC_FS_21 */
	error |= proc_register_dynamic(&proc_net, &proc_net_pfkey);
	error |= proc_register_dynamic(&proc_net, &proc_net_pfkey_supported);
	error |= proc_register_dynamic(&proc_net, &proc_net_pfkey_registered);
#    endif /* PROC_FS_21 */
#  else /* !PROC_FS_2325 */
	proc_net_create ("pf_key", 0, pfkey_get_info);
	proc_net_create ("pf_key_supported", 0, pfkey_supported_get_info);
	proc_net_create ("pf_key_registered", 0, pfkey_registered_get_info);
#  endif /* !PROC_FS_2325 */
#endif /* CONFIG_PROC_FS */

	return error;
}

int
pfkey_cleanup(void)
{
	int error = 0;
	
        printk(KERN_INFO "klips_info:pfkey_cleanup: "
	       "shutting down PF_KEY domain sockets.\n");
        sock_unregister(PF_KEY);

	error |= supported_remove_all(SADB_SATYPE_AH);
	error |= supported_remove_all(SADB_SATYPE_ESP);
#ifdef CONFIG_KLIPS_IPCOMP
	error |= supported_remove_all(SADB_X_SATYPE_COMP);
#endif /* CONFIG_KLIPS_IPCOMP */
	error |= supported_remove_all(SADB_X_SATYPE_IPIP);

#ifdef CONFIG_PROC_FS
#  ifndef PROC_FS_2325
	if (proc_net_unregister(proc_net_pfkey.low_ino) != 0)
		printk("klips_debug:pfkey_cleanup: "
		       "cannot unregister /proc/net/pf_key\n");
	if (proc_net_unregister(proc_net_pfkey_supported.low_ino) != 0)
		printk("klips_debug:pfkey_cleanup: "
		       "cannot unregister /proc/net/pf_key_supported\n");
	if (proc_net_unregister(proc_net_pfkey_registered.low_ino) != 0)
		printk("klips_debug:pfkey_cleanup: "
		       "cannot unregister /proc/net/pf_key_registered\n");
#  else /* !PROC_FS_2325 */
	proc_net_remove ("pf_key");
	proc_net_remove ("pf_key_supported");
	proc_net_remove ("pf_key_registered");
#  endif /* !PROC_FS_2325 */
#endif /* CONFIG_PROC_FS */

	/* other module unloading cleanup happens here */
	return error;
}

#ifdef MODULE
#if 0
int
init_module(void)
{
	pfkey_init();
	return 0;
}

void
cleanup_module(void)
{
	pfkey_cleanup();
}
#endif /* 0 */
#else /* MODULE */
struct net_protocol;
void pfkey_proto_init(struct net_protocol *pro)
{
	pfkey_init();
}
#endif /* MODULE */

/*
 * $Log: pfkey_v2.c,v $
 * Revision 1.97.2.8  2006/07/10 15:56:11  paul
 * Fix for bug #642 by Bart.
 *
 * Revision 1.97.2.7  2006/04/04 11:34:19  ken
 * Backport SMP fixes + #ifdef cleanup from #public
 *
 * Revision 1.97.2.6  2006/02/15 05:00:20  paul
 * Fix for crasher on 2.6.12+ with klips (mostly seen on redhat kernels)
 *
 * Revision 1.97.2.5  2005/11/22 04:11:52  ken
 * Backport fixes for 2.6.14 kernels from HEAD
 *
 * Revision 1.97.2.4  2005/09/14 16:40:45  mcr
 *    pull up of compilation on 2.4
 *
 * Revision 1.97.2.3  2005/09/06 02:10:03  mcr
 *    pulled up possible SMP-related compilation fix
 *
 * Revision 1.97.2.2  2005/08/28 01:21:12  paul
 * Undid Ken's gcc4 fix in version 1.94 since it breaks linking KLIPS on
 * SMP kernels.
 *
 * Revision 1.97.2.1  2005/08/27 23:40:00  paul
 * recommited HAVE_SOCK_SECURITY fixes for linux 2.6.13
 *
 * Revision 1.102  2005/09/14 16:37:23  mcr
 * 	fix to compile on 2.4.
 *
 * Revision 1.101  2005/09/06 01:42:25  mcr
 *    removed additional SOCKOPS_WRAPPED code
 *
 * Revision 1.100  2005/08/30 18:10:15  mcr
 * 	remove SOCKOPS_WRAPPED() code, add proper locking to the
 * 	pfkey code. (cross fingers)
 *
 * Revision 1.99  2005/08/28 01:53:37  paul
 * Undid Ken's gcc4 fix in version 1.94 since it breaks linking KLIPS on SMP kernels.
 *
 * Revision 1.98  2005/08/27 23:07:21  paul
 * Somewhere between 2.6.12 and 2.6.13rc7 the unused security memnber in sk_buff
 * has been removed. This patch should fix compilation for both cases.
 *
 * Revision 1.97  2005/07/20 00:33:36  mcr
 * 	fixed typo in #ifdef for SKALLOC.
 *
 * Revision 1.96  2005/07/19 20:02:15  mcr
 * 	sk_alloc() interface change.
 *
 * Revision 1.95  2005/07/09 00:40:06  ken
 * Fix for GCC4 - it doesn't like the potential for duplicate declaration
 *
 * Revision 1.94  2005/07/09 00:14:04  ken
 * Casts for 64bit cleanliness
 *
 * Revision 1.93  2005/07/08 16:20:05  mcr
 * 	fix for 2.6.12 disapperance of sk_zapped field -> sock_flags.
 *
 * Revision 1.92  2005/05/21 03:29:39  mcr
 * 	fixed missing prototype definition.
 *
 * Revision 1.91  2005/05/11 01:43:45  mcr
 * 	removed "poor-man"s OOP in favour of proper C structures.
 *
 * Revision 1.90  2005/05/02 18:42:47  mcr
 * 	fix for cut&paste error with pfkey_v2.c "supported_name"
 *
 * Revision 1.89  2005/05/01 03:12:31  mcr
 * 	print name if it is available.
 *
 * Revision 1.88  2005/04/29 05:10:22  mcr
 * 	removed from extraenous includes to make unit testing easier.
 *
 * Revision 1.87  2005/04/15 19:57:10  mcr
 * 	make sure that address has 0p so that it will
 * 	sanitized.
 *
 * Revision 1.86  2005/04/08 18:28:36  mcr
 * 	some minor #ifdef simplification in pursuit of a possible bug.
 *
 * Revision 1.85  2004/12/03 21:25:57  mcr
 * 	compile time fixes for running on 2.6.
 * 	still experimental.
 *
 * Revision 1.84  2004/08/17 03:27:23  mcr
 * 	klips 2.6 edits.
 *
 * Revision 1.83  2004/08/04 15:57:07  mcr
 * 	moved des .h files to include/des/ *
 * 	included 2.6 protocol specific things
 * 	started at NAT-T support, but it will require a kernel patch.
 *
 * Revision 1.82  2004/07/10 19:11:18  mcr
 * 	CONFIG_IPSEC -> CONFIG_KLIPS.
 *
 * Revision 1.81  2004/04/25 21:23:11  ken
 * Pull in dhr's changes from FreeS/WAN 2.06
 *
 * Revision 1.80  2004/04/06 02:49:26  mcr
 * 	pullup of algo code from alg-branch.
 *
 * Revision 1.79.4.1  2003/12/22 15:25:52  jjo
 * . Merged algo-0.8.1-rc11-test1 into alg-branch
 *
 * Revision 1.79  2003/10/31 02:27:55  mcr
 * 	pulled up port-selector patches and sa_id elimination.
 *
 * Revision 1.78.4.1  2003/10/29 01:30:41  mcr
 * 	elimited "struct sa_id".
 *
 * Revision 1.78  2003/04/03 17:38:09  rgb
 * Centralised ipsec_kfree_skb and ipsec_dev_{get,put}.
 *
 * Revision 1.77  2002/10/17 16:49:36  mcr
 * 	sock->ops should reference the unwrapped options so that
 * 	we get hacked in locking on SMP systems.
 *
 * Revision 1.76  2002/10/12 23:11:53  dhr
 *
 * [KenB + DHR] more 64-bit cleanup
 *
 * Revision 1.75  2002/09/20 05:01:57  rgb
 * Added memory allocation debugging.
 *
 * Revision 1.74  2002/09/19 02:42:50  mcr
 * 	do not define the pfkey_ops function for now.
 *
 * Revision 1.73  2002/09/17 17:29:23  mcr
 * 	#if 0 out some dead code - pfkey_ops is never used as written.
 *
 * Revision 1.72  2002/07/24 18:44:54  rgb
 * Type fiddling to tame ia64 compiler.
 *
 * Revision 1.71  2002/05/23 07:14:11  rgb
 * Cleaned up %p variants to 0p%p for test suite cleanup.
 *
 * Revision 1.70  2002/04/24 07:55:32  mcr
 * 	#include patches and Makefiles for post-reorg compilation.
 *
 * Revision 1.69  2002/04/24 07:36:33  mcr
 * Moved from ./klips/net/ipsec/pfkey_v2.c,v
 *
 * Revision 1.68  2002/03/08 01:15:17  mcr
 * 	put some internal structure only debug messages behind
 * 	&& sysctl_ipsec_debug_verbose.
 *
 * Revision 1.67  2002/01/29 17:17:57  mcr
 * 	moved include of ipsec_param.h to after include of linux/kernel.h
 * 	otherwise, it seems that some option that is set in ipsec_param.h
 * 	screws up something subtle in the include path to kernel.h, and
 * 	it complains on the snprintf() prototype.
 *
 * Revision 1.66  2002/01/29 04:00:54  mcr
 * 	more excise of kversions.h header.
 *
 * Revision 1.65  2002/01/29 02:13:18  mcr
 * 	introduction of ipsec_kversion.h means that include of
 * 	ipsec_param.h must preceed any decisions about what files to
 * 	include to deal with differences in kernel source.
 *
 * Revision 1.64  2001/11/26 09:23:51  rgb
 * Merge MCR's ipsec_sa, eroute, proc and struct lifetime changes.
 *
 * Revision 1.61.2.1  2001/09/25 02:28:44  mcr
 * 	cleaned up includes.
 *
 * Revision 1.63  2001/11/12 19:38:00  rgb
 * Continue trying other sockets even if one fails and return only original
 * error.
 *
 * Revision 1.62  2001/10/18 04:45:22  rgb
 * 2.4.9 kernel deprecates linux/malloc.h in favour of linux/slab.h,
 * lib/freeswan.h version macros moved to lib/kversions.h.
 * Other compiler directive cleanups.
 *
 * Revision 1.61  2001/09/20 15:32:59  rgb
 * Min/max cleanup.
 *
 * Revision 1.60  2001/06/14 19:35:12  rgb
 * Update copyright date.
 *
 * Revision 1.59  2001/06/13 15:35:48  rgb
 * Fixed #endif comments.
 *
 * Revision 1.58  2001/05/04 16:37:24  rgb
 * Remove erroneous checking of return codes for proc_net_* in 2.4.
 *
 * Revision 1.57  2001/05/03 19:43:36  rgb
 * Initialise error return variable.
 * Check error return codes in startup and shutdown.
 * Standardise on SENDERR() macro.
 *
 * Revision 1.56  2001/04/21 23:05:07  rgb
 * Define out skb->used for 2.4 kernels.
 *
 * Revision 1.55  2001/02/28 05:03:28  rgb
 * Clean up and rationalise startup messages.
 *
 * Revision 1.54  2001/02/27 22:24:55  rgb
 * Re-formatting debug output (line-splitting, joining, 1arg/line).
 * Check for satoa() return codes.
 *
 * Revision 1.53  2001/02/27 06:48:18  rgb
 * Fixed pfkey socket unregister log message to reflect type and function.
 *
 * Revision 1.52  2001/02/26 22:34:38  rgb
 * Fix error return code that was getting overwritten by the error return
 * code of an upmsg.
 *
 * Revision 1.51  2001/01/30 23:42:47  rgb
 * Allow pfkey msgs from pid other than user context required for ACQUIRE
 * and subsequent ADD or UDATE.
 *
 * Revision 1.50  2001/01/23 20:22:59  rgb
 * 2.4 fix to remove removed is_clone member.
 *
 * Revision 1.49  2000/11/06 04:33:47  rgb
 * Changed non-exported functions to DEBUG_NO_STATIC.
 *
 * Revision 1.48  2000/09/29 19:47:41  rgb
 * Update copyright.
 *
 * Revision 1.47  2000/09/22 04:23:04  rgb
 * Added more debugging to pfkey_upmsg() call from pfkey_sendmsg() error.
 *
 * Revision 1.46  2000/09/21 04:20:44  rgb
 * Fixed array size off-by-one error.  (Thanks Svenning!)
 *
 * Revision 1.45  2000/09/20 04:01:26  rgb
 * Changed static functions to DEBUG_NO_STATIC for revealing function names
 * in oopsen.
 *
 * Revision 1.44  2000/09/19 00:33:17  rgb
 * 2.0 fixes.
 *
 * Revision 1.43  2000/09/16 01:28:13  rgb
 * Fixed use of 0 in p format warning.
 *
 * Revision 1.42  2000/09/16 01:09:41  rgb
 * Fixed debug format warning for pointers that was expecting ints.
 *
 * Revision 1.41  2000/09/13 15:54:00  rgb
 * Rewrote pfkey_get_info(), added pfkey_{supported,registered}_get_info().
 * Moved supported algos add and remove to functions.
 *
 * Revision 1.40  2000/09/12 18:49:28  rgb
 * Added IPIP tunnel and IPCOMP register support.
 *
 * Revision 1.39  2000/09/12 03:23:49  rgb
 * Converted #if0 debugs to sysctl.
 * Removed debug_pfkey initialisations that prevented no_debug loading or
 * linking.
 *
 * Revision 1.38  2000/09/09 06:38:02  rgb
 * Return positive errno in pfkey_reply error message.
 *
 * Revision 1.37  2000/09/08 19:19:09  rgb
 * Change references from DEBUG_IPSEC to CONFIG_IPSEC_DEBUG.
 * Clean-up of long-unused crud...
 * Create pfkey error message on on failure.
 * Give pfkey_list_{insert,remove}_{socket,supported}() some error
 * checking.
 *
 * Revision 1.36  2000/09/01 18:49:38  rgb
 * Reap experimental NET_21_ bits.
 * Turned registered sockets list into an array of one list per satype.
 * Remove references to deprecated sklist_{insert,remove}_socket.
 * Removed leaking socket debugging code.
 * Removed duplicate pfkey_insert_socket in pfkey_create.
 * Removed all references to pfkey msg->msg_name, since it is not used for
 * pfkey.
 * Added a supported algorithms array lists, one per satype and registered
 * existing algorithms.
 * Fixed pfkey_list_{insert,remove}_{socket,support}() to allow change to
 * list.
 * Only send pfkey_expire() messages to sockets registered for that satype.
 *
 * Revision 1.35  2000/08/24 17:03:00  rgb
 * Corrected message size error return code for PF_KEYv2.
 * Removed downward error prohibition.
 *
 * Revision 1.34  2000/08/21 16:32:26  rgb
 * Re-formatted for cosmetic consistency and readability.
 *
 * Revision 1.33  2000/08/20 21:38:24  rgb
 * Added a pfkey_reply parameter to pfkey_msg_interp(). (Momchil)
 * Extended the upward message initiation of pfkey_sendmsg(). (Momchil)
 *
 * Revision 1.32  2000/07/28 14:58:31  rgb
 * Changed kfree_s to kfree, eliminating extra arg to fix 2.4.0-test5.
 *
 * Revision 1.31  2000/05/16 03:04:00  rgb
 * Updates for 2.3.99pre8 from MB.
 *
 * Revision 1.30  2000/05/10 19:22:21  rgb
 * Use sklist private functions for 2.3.xx compatibility.
 *
 * Revision 1.29  2000/03/22 16:17:03  rgb
 * Fixed SOCKOPS_WRAPPED macro for SMP (MB).
 *
 * Revision 1.28  2000/02/21 19:30:45  rgb
 * Removed references to pkt_bridged for 2.3.47 compatibility.
 *
 * Revision 1.27  2000/02/14 21:07:00  rgb
 * Fixed /proc/net/pf-key legend spacing.
 *
 * Revision 1.26  2000/01/22 03:46:59  rgb
 * Fixed pfkey error return mechanism so that we are able to free the
 * local copy of the pfkey_msg, plugging a memory leak and silencing
 * the bad object free complaints.
 *
 * Revision 1.25  2000/01/21 06:19:44  rgb
 * Moved pfkey_list_remove_socket() calls to before MOD_USE_DEC_COUNT.
 * Added debugging to pfkey_upmsg.
 *
 * Revision 1.24  2000/01/10 16:38:23  rgb
 * MB fixups for 2.3.x.
 *
 * Revision 1.23  1999/12/09 23:22:16  rgb
 * Added more instrumentation for debugging 2.0 socket
 * selection/reading.
 * Removed erroneous 2.0 wait==NULL check bug in select.
 *
 * Revision 1.22  1999/12/08 20:32:16  rgb
 * Tidied up 2.0.xx support, after major pfkey work, eliminating
 * msg->msg_name twiddling in the process, since it is not defined
 * for PF_KEYv2.
 *
 * Revision 1.21  1999/12/01 22:17:19  rgb
 * Set skb->dev to zero on new skb in case it is a reused skb.
 * Added check for skb_put overflow and freeing to avoid upmsg on error.
 * Added check for wrong pfkey version and freeing to avoid upmsg on
 * error.
 * Shut off content dumping in pfkey_destroy.
 * Added debugging message for size of buffer allocated for upmsg.
 *
 * Revision 1.20  1999/11/27 12:11:00  rgb
 * Minor clean-up, enabling quiet operation of pfkey if desired.
 *
 * Revision 1.19  1999/11/25 19:04:21  rgb
 * Update proc_fs code for pfkey to use dynamic registration.
 *
 * Revision 1.18  1999/11/25 09:07:17  rgb
 * Implemented SENDERR macro for propagating error codes.
 * Fixed error return code bug.
 *
 * Revision 1.17  1999/11/23 23:07:20  rgb
 * Change name of pfkey_msg_parser to pfkey_msg_interp since it no longer
 * parses. (PJO)
 * Sort out pfkey and freeswan headers, putting them in a library path.
 *
 * Revision 1.16  1999/11/20 22:00:22  rgb
 * Moved socketlist type declarations and prototypes for shared use.
 * Renamed reformatted and generically extended for use by other socket
 * lists pfkey_{del,add}_open_socket to pfkey_list_{remove,insert}_socket.
 *
 * Revision 1.15  1999/11/18 04:15:09  rgb
 * Make pfkey_data_ready temporarily available for 2.2.x testing.
 * Clean up pfkey_destroy_socket() debugging statements.
 * Add Peter Onion's code to send messages up to all listening sockets.
 * Changed all occurrences of #include "../../../lib/freeswan.h"
 * to #include <freeswan.h> which works due to -Ilibfreeswan in the
 * klips/net/ipsec/Makefile.
 * Replaced all kernel version macros to shorter, readable form.
 * Added CONFIG_PROC_FS compiler directives in case it is shut off.
 *
 * Revision 1.14  1999/11/17 16:01:00  rgb
 * Make pfkey_data_ready temporarily available for 2.2.x testing.
 * Clean up pfkey_destroy_socket() debugging statements.
 * Add Peter Onion's code to send messages up to all listening sockets.
 * Changed #include "../../../lib/freeswan.h" to #include <freeswan.h>
 * which works due to -Ilibfreeswan in the klips/net/ipsec/Makefile.
 *
 * Revision 1.13  1999/10/27 19:59:51  rgb
 * Removed af_unix comments that are no longer relevant.
 * Added debug prink statements.
 * Added to the /proc output in pfkey_get_info.
 * Made most functions non-static to enable oops tracing.
 * Re-enable skb dequeueing and freeing.
 * Fix skb_alloc() and skb_put() size bug in pfkey_upmsg().
 *
 * Revision 1.12  1999/10/26 17:05:42  rgb
 * Complete re-ordering based on proto_ops structure order.
 * Separated out proto_ops structures for 2.0.x and 2.2.x for clarity.
 * Simplification to use built-in socket ops where possible for 2.2.x.
 * Add shorter macros for compiler directives to visually clean-up.
 * Add lots of sk skb dequeueing debugging statements.
 * Added to the /proc output in pfkey_get_info.
 *
 * Revision 1.11  1999/09/30 02:55:10  rgb
 * Bogus skb detection.
 * Fix incorrect /proc/net/ipsec-eroute printk message.
 *
 * Revision 1.10  1999/09/21 15:22:13  rgb
 * Temporary fix while I figure out the right way to destroy sockets.
 *
 * Revision 1.9  1999/07/08 19:19:44  rgb
 * Fix pointer format warning.
 * Fix missing member error under 2.0.xx kernels.
 *
 * Revision 1.8  1999/06/13 07:24:04  rgb
 * Add more debugging.
 *
 * Revision 1.7  1999/06/10 05:24:17  rgb
 * Clarified compiler directives.
 * Renamed variables to reduce confusion.
 * Used sklist_*_socket() kernel functions to simplify 2.2.x socket support.
 * Added lots of sanity checking.
 *
 * Revision 1.6  1999/06/03 18:59:50  rgb
 * More updates to 2.2.x socket support.  Almost works, oops at end of call.
 *
 * Revision 1.5  1999/05/25 22:44:05  rgb
 * Start fixing 2.2 sockets.
 *
 * Revision 1.4  1999/04/29 15:21:34  rgb
 * Move log to the end of the file.
 * Eliminate min/max redefinition in #include <net/tcp.h>.
 * Correct path for pfkey #includes
 * Standardise an error return method.
 * Add debugging instrumentation.
 * Move message type checking to pfkey_msg_parse().
 * Add check for errno incorrectly set.
 * Add check for valid PID.
 * Add check for reserved illegally set.
 * Add check for message out of bounds.
 *
 * Revision 1.3  1999/04/15 17:58:07  rgb
 * Add RCSID labels.
 *
 * Revision 1.2  1999/04/15 15:37:26  rgb
 * Forward check changes from POST1_00 branch.
 *
 * Revision 1.1.2.2  1999/04/13 20:37:12  rgb
 * Header Title correction.
 *
 * Revision 1.1.2.1  1999/03/26 20:58:55  rgb
 * Add pfkeyv2 support to KLIPS.
 *
 *
 * RFC 2367
 * PF_KEY_v2 Key Management API
 */
