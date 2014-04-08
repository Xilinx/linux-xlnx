/*
 *	SUCS NET3:
 *
 *	Generic datagram handling routines. These are generic for all
 *	protocols. Possibly a generic IP version on top of these would
 *	make sense. Not tonight however 8-).
 *	This is used because UDP, RAW, PACKET, DDP, IPX, AX.25 and
 *	NetROM layer all have identical poll code and mostly
 *	identical recvmsg() code. So we share it here. The poll was
 *	shared before but buried in udp.c so I moved it.
 *
 *	Authors:	Alan Cox <alan@lxorguk.ukuu.org.uk>. (datagram_poll() from old
 *						     udp.c code)
 *
 *	Fixes:
 *		Alan Cox	:	NULL return from skb_peek_copy()
 *					understood
 *		Alan Cox	:	Rewrote skb_read_datagram to avoid the
 *					skb_peek_copy stuff.
 *		Alan Cox	:	Added support for SOCK_SEQPACKET.
 *					IPX can no longer use the SO_TYPE hack
 *					but AX.25 now works right, and SPX is
 *					feasible.
 *		Alan Cox	:	Fixed write poll of non IP protocol
 *					crash.
 *		Florian  La Roche:	Changed for my new skbuff handling.
 *		Darryl Miles	:	Fixed non-blocking SOCK_SEQPACKET.
 *		Linus Torvalds	:	BSD semantic fixes.
 *		Alan Cox	:	Datagram iovec handling
 *		Darryl Miles	:	Fixed non-blocking SOCK_STREAM.
 *		Alan Cox	:	POSIXisms
 *		Pete Wyckoff    :       Unconnected accept() fix.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/poll.h>
#include <linux/highmem.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pagemap.h>

#include <net/protocol.h>
#include <linux/skbuff.h>

#include <net/checksum.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <trace/events/skb.h>
#include <net/busy_poll.h>

/*
 *	Is a socket 'connection oriented' ?
 */
static inline int connection_based(struct sock *sk)
{
	return sk->sk_type == SOCK_SEQPACKET || sk->sk_type == SOCK_STREAM;
}

static int receiver_wake_function(wait_queue_t *wait, unsigned int mode, int sync,
				  void *key)
{
	unsigned long bits = (unsigned long)key;

	/*
	 * Avoid a wakeup if event not interesting for us
	 */
	if (bits && !(bits & (POLLIN | POLLERR)))
		return 0;
	return autoremove_wake_function(wait, mode, sync, key);
}
/*
 * Wait for the last received packet to be different from skb
 */
static int wait_for_more_packets(struct sock *sk, int *err, long *timeo_p,
				 const struct sk_buff *skb)
{
	int error;
	DEFINE_WAIT_FUNC(wait, receiver_wake_function);

	prepare_to_wait_exclusive(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

	/* Socket errors? */
	error = sock_error(sk);
	if (error)
		goto out_err;

	if (sk->sk_receive_queue.prev != skb)
		goto out;

	/* Socket shut down? */
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		goto out_noerr;

	/* Sequenced packets can come disconnected.
	 * If so we report the problem
	 */
	error = -ENOTCONN;
	if (connection_based(sk) &&
	    !(sk->sk_state == TCP_ESTABLISHED || sk->sk_state == TCP_LISTEN))
		goto out_err;

	/* handle signals */
	if (signal_pending(current))
		goto interrupted;

	error = 0;
	*timeo_p = schedule_timeout(*timeo_p);
out:
	finish_wait(sk_sleep(sk), &wait);
	return error;
interrupted:
	error = sock_intr_errno(*timeo_p);
out_err:
	*err = error;
	goto out;
out_noerr:
	*err = 0;
	error = 1;
	goto out;
}

/**
 *	__skb_recv_datagram - Receive a datagram skbuff
 *	@sk: socket
 *	@flags: MSG_ flags
 *	@peeked: returns non-zero if this packet has been seen before
 *	@off: an offset in bytes to peek skb from. Returns an offset
 *	      within an skb where data actually starts
 *	@err: error code returned
 *
 *	Get a datagram skbuff, understands the peeking, nonblocking wakeups
 *	and possible races. This replaces identical code in packet, raw and
 *	udp, as well as the IPX AX.25 and Appletalk. It also finally fixes
 *	the long standing peek and read race for datagram sockets. If you
 *	alter this routine remember it must be re-entrant.
 *
 *	This function will lock the socket if a skb is returned, so the caller
 *	needs to unlock the socket in that case (usually by calling
 *	skb_free_datagram)
 *
 *	* It does not lock socket since today. This function is
 *	* free of race conditions. This measure should/can improve
 *	* significantly datagram socket latencies at high loads,
 *	* when data copying to user space takes lots of time.
 *	* (BTW I've just killed the last cli() in IP/IPv6/core/netlink/packet
 *	*  8) Great win.)
 *	*			                    --ANK (980729)
 *
 *	The order of the tests when we find no data waiting are specified
 *	quite explicitly by POSIX 1003.1g, don't change them without having
 *	the standard around please.
 */
struct sk_buff *__skb_recv_datagram(struct sock *sk, unsigned int flags,
				    int *peeked, int *off, int *err)
{
	struct sk_buff *skb, *last;
	long timeo;
	/*
	 * Caller is allowed not to check sk->sk_err before skb_recv_datagram()
	 */
	int error = sock_error(sk);

	if (error)
		goto no_packet;

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	do {
		/* Again only user level code calls this function, so nothing
		 * interrupt level will suddenly eat the receive_queue.
		 *
		 * Look at current nfs client by the way...
		 * However, this function was correct in any case. 8)
		 */
		unsigned long cpu_flags;
		struct sk_buff_head *queue = &sk->sk_receive_queue;
		int _off = *off;

		last = (struct sk_buff *)queue;
		spin_lock_irqsave(&queue->lock, cpu_flags);
		skb_queue_walk(queue, skb) {
			last = skb;
			*peeked = skb->peeked;
			if (flags & MSG_PEEK) {
				if (_off >= skb->len && (skb->len || _off ||
							 skb->peeked)) {
					_off -= skb->len;
					continue;
				}
				skb->peeked = 1;
				atomic_inc(&skb->users);
			} else
				__skb_unlink(skb, queue);

			spin_unlock_irqrestore(&queue->lock, cpu_flags);
			*off = _off;
			return skb;
		}
		spin_unlock_irqrestore(&queue->lock, cpu_flags);

		if (sk_can_busy_loop(sk) &&
		    sk_busy_loop(sk, flags & MSG_DONTWAIT))
			continue;

		/* User doesn't want to wait */
		error = -EAGAIN;
		if (!timeo)
			goto no_packet;

	} while (!wait_for_more_packets(sk, err, &timeo, last));

	return NULL;

no_packet:
	*err = error;
	return NULL;
}
EXPORT_SYMBOL(__skb_recv_datagram);

struct sk_buff *skb_recv_datagram(struct sock *sk, unsigned int flags,
				  int noblock, int *err)
{
	int peeked, off = 0;

	return __skb_recv_datagram(sk, flags | (noblock ? MSG_DONTWAIT : 0),
				   &peeked, &off, err);
}
EXPORT_SYMBOL(skb_recv_datagram);

void skb_free_datagram(struct sock *sk, struct sk_buff *skb)
{
	consume_skb(skb);
	sk_mem_reclaim_partial(sk);
}
EXPORT_SYMBOL(skb_free_datagram);

void skb_free_datagram_locked(struct sock *sk, struct sk_buff *skb)
{
	bool slow;

	if (likely(atomic_read(&skb->users) == 1))
		smp_rmb();
	else if (likely(!atomic_dec_and_test(&skb->users)))
		return;

	slow = lock_sock_fast(sk);
	skb_orphan(skb);
	sk_mem_reclaim_partial(sk);
	unlock_sock_fast(sk, slow);

	/* skb is now orphaned, can be freed outside of locked section */
	__kfree_skb(skb);
}
EXPORT_SYMBOL(skb_free_datagram_locked);

/**
 *	skb_kill_datagram - Free a datagram skbuff forcibly
 *	@sk: socket
 *	@skb: datagram skbuff
 *	@flags: MSG_ flags
 *
 *	This function frees a datagram skbuff that was received by
 *	skb_recv_datagram.  The flags argument must match the one
 *	used for skb_recv_datagram.
 *
 *	If the MSG_PEEK flag is set, and the packet is still on the
 *	receive queue of the socket, it will be taken off the queue
 *	before it is freed.
 *
 *	This function currently only disables BH when acquiring the
 *	sk_receive_queue lock.  Therefore it must not be used in a
 *	context where that lock is acquired in an IRQ context.
 *
 *	It returns 0 if the packet was removed by us.
 */

int skb_kill_datagram(struct sock *sk, struct sk_buff *skb, unsigned int flags)
{
	int err = 0;

	if (flags & MSG_PEEK) {
		err = -ENOENT;
		spin_lock_bh(&sk->sk_receive_queue.lock);
		if (skb == skb_peek(&sk->sk_receive_queue)) {
			__skb_unlink(skb, &sk->sk_receive_queue);
			atomic_dec(&skb->users);
			err = 0;
		}
		spin_unlock_bh(&sk->sk_receive_queue.lock);
	}

	kfree_skb(skb);
	atomic_inc(&sk->sk_drops);
	sk_mem_reclaim_partial(sk);

	return err;
}
EXPORT_SYMBOL(skb_kill_datagram);

/**
 *	skb_copy_datagram_iovec - Copy a datagram to an iovec.
 *	@skb: buffer to copy
 *	@offset: offset in the buffer to start copying from
 *	@to: io vector to copy to
 *	@len: amount of data to copy from buffer to iovec
 *
 *	Note: the iovec is modified during the copy.
 */
int skb_copy_datagram_iovec(const struct sk_buff *skb, int offset,
			    struct iovec *to, int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;

	trace_skb_copy_datagram_iovec(skb, len);

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		if (memcpy_toiovec(to, skb->data + offset, copy))
			goto fault;
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			int err;
			u8  *vaddr;
			struct page *page = skb_frag_page(frag);

			if (copy > len)
				copy = len;
			vaddr = kmap(page);
			err = memcpy_toiovec(to, vaddr + frag->page_offset +
					     offset - start, copy);
			kunmap(page);
			if (err)
				goto fault;
			if (!(len -= copy))
				return 0;
			offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (skb_copy_datagram_iovec(frag_iter,
						    offset - start,
						    to, copy))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_copy_datagram_iovec);

/**
 *	skb_copy_datagram_const_iovec - Copy a datagram to an iovec.
 *	@skb: buffer to copy
 *	@offset: offset in the buffer to start copying from
 *	@to: io vector to copy to
 *	@to_offset: offset in the io vector to start copying to
 *	@len: amount of data to copy from buffer to iovec
 *
 *	Returns 0 or -EFAULT.
 *	Note: the iovec is not modified during the copy.
 */
int skb_copy_datagram_const_iovec(const struct sk_buff *skb, int offset,
				  const struct iovec *to, int to_offset,
				  int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		if (memcpy_toiovecend(to, skb->data + offset, to_offset, copy))
			goto fault;
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
		to_offset += copy;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			int err;
			u8  *vaddr;
			struct page *page = skb_frag_page(frag);

			if (copy > len)
				copy = len;
			vaddr = kmap(page);
			err = memcpy_toiovecend(to, vaddr + frag->page_offset +
						offset - start, to_offset, copy);
			kunmap(page);
			if (err)
				goto fault;
			if (!(len -= copy))
				return 0;
			offset += copy;
			to_offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (skb_copy_datagram_const_iovec(frag_iter,
							  offset - start,
							  to, to_offset,
							  copy))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			to_offset += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_copy_datagram_const_iovec);

/**
 *	skb_copy_datagram_from_iovec - Copy a datagram from an iovec.
 *	@skb: buffer to copy
 *	@offset: offset in the buffer to start copying to
 *	@from: io vector to copy to
 *	@from_offset: offset in the io vector to start copying from
 *	@len: amount of data to copy to buffer from iovec
 *
 *	Returns 0 or -EFAULT.
 *	Note: the iovec is not modified during the copy.
 */
int skb_copy_datagram_from_iovec(struct sk_buff *skb, int offset,
				 const struct iovec *from, int from_offset,
				 int len)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;

	/* Copy header. */
	if (copy > 0) {
		if (copy > len)
			copy = len;
		if (memcpy_fromiovecend(skb->data + offset, from, from_offset,
					copy))
			goto fault;
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
		from_offset += copy;
	}

	/* Copy paged appendix. Hmm... why does this look so complicated? */
	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			int err;
			u8  *vaddr;
			struct page *page = skb_frag_page(frag);

			if (copy > len)
				copy = len;
			vaddr = kmap(page);
			err = memcpy_fromiovecend(vaddr + frag->page_offset +
						  offset - start,
						  from, from_offset, copy);
			kunmap(page);
			if (err)
				goto fault;

			if (!(len -= copy))
				return 0;
			offset += copy;
			from_offset += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			if (copy > len)
				copy = len;
			if (skb_copy_datagram_from_iovec(frag_iter,
							 offset - start,
							 from,
							 from_offset,
							 copy))
				goto fault;
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			from_offset += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_copy_datagram_from_iovec);

/**
 *	zerocopy_sg_from_iovec - Build a zerocopy datagram from an iovec
 *	@skb: buffer to copy
 *	@from: io vector to copy from
 *	@offset: offset in the io vector to start copying from
 *	@count: amount of vectors to copy to buffer from
 *
 *	The function will first copy up to headlen, and then pin the userspace
 *	pages and build frags through them.
 *
 *	Returns 0, -EFAULT or -EMSGSIZE.
 *	Note: the iovec is not modified during the copy
 */
int zerocopy_sg_from_iovec(struct sk_buff *skb, const struct iovec *from,
				  int offset, size_t count)
{
	int len = iov_length(from, count) - offset;
	int copy = min_t(int, skb_headlen(skb), len);
	int size;
	int i = 0;

	/* copy up to skb headlen */
	if (skb_copy_datagram_from_iovec(skb, 0, from, offset, copy))
		return -EFAULT;

	if (len == copy)
		return 0;

	offset += copy;
	while (count--) {
		struct page *page[MAX_SKB_FRAGS];
		int num_pages;
		unsigned long base;
		unsigned long truesize;

		/* Skip over from offset and copied */
		if (offset >= from->iov_len) {
			offset -= from->iov_len;
			++from;
			continue;
		}
		len = from->iov_len - offset;
		base = (unsigned long)from->iov_base + offset;
		size = ((base & ~PAGE_MASK) + len + ~PAGE_MASK) >> PAGE_SHIFT;
		if (i + size > MAX_SKB_FRAGS)
			return -EMSGSIZE;
		num_pages = get_user_pages_fast(base, size, 0, &page[i]);
		if (num_pages != size) {
			release_pages(&page[i], num_pages, 0);
			return -EFAULT;
		}
		truesize = size * PAGE_SIZE;
		skb->data_len += len;
		skb->len += len;
		skb->truesize += truesize;
		atomic_add(truesize, &skb->sk->sk_wmem_alloc);
		while (len) {
			int off = base & ~PAGE_MASK;
			int size = min_t(int, len, PAGE_SIZE - off);
			skb_fill_page_desc(skb, i, page[i], off, size);
			base += size;
			len -= size;
			i++;
		}
		offset = 0;
		++from;
	}
	return 0;
}
EXPORT_SYMBOL(zerocopy_sg_from_iovec);

static int skb_copy_and_csum_datagram(const struct sk_buff *skb, int offset,
				      u8 __user *to, int len,
				      __wsum *csump)
{
	int start = skb_headlen(skb);
	int i, copy = start - offset;
	struct sk_buff *frag_iter;
	int pos = 0;

	/* Copy header. */
	if (copy > 0) {
		int err = 0;
		if (copy > len)
			copy = len;
		*csump = csum_and_copy_to_user(skb->data + offset, to, copy,
					       *csump, &err);
		if (err)
			goto fault;
		if ((len -= copy) == 0)
			return 0;
		offset += copy;
		to += copy;
		pos = copy;
	}

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		int end;
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		WARN_ON(start > offset + len);

		end = start + skb_frag_size(frag);
		if ((copy = end - offset) > 0) {
			__wsum csum2;
			int err = 0;
			u8  *vaddr;
			struct page *page = skb_frag_page(frag);

			if (copy > len)
				copy = len;
			vaddr = kmap(page);
			csum2 = csum_and_copy_to_user(vaddr +
							frag->page_offset +
							offset - start,
						      to, copy, 0, &err);
			kunmap(page);
			if (err)
				goto fault;
			*csump = csum_block_add(*csump, csum2, pos);
			if (!(len -= copy))
				return 0;
			offset += copy;
			to += copy;
			pos += copy;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		int end;

		WARN_ON(start > offset + len);

		end = start + frag_iter->len;
		if ((copy = end - offset) > 0) {
			__wsum csum2 = 0;
			if (copy > len)
				copy = len;
			if (skb_copy_and_csum_datagram(frag_iter,
						       offset - start,
						       to, copy,
						       &csum2))
				goto fault;
			*csump = csum_block_add(*csump, csum2, pos);
			if ((len -= copy) == 0)
				return 0;
			offset += copy;
			to += copy;
			pos += copy;
		}
		start = end;
	}
	if (!len)
		return 0;

fault:
	return -EFAULT;
}

__sum16 __skb_checksum_complete_head(struct sk_buff *skb, int len)
{
	__sum16 sum;

	sum = csum_fold(skb_checksum(skb, 0, len, skb->csum));
	if (likely(!sum)) {
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE))
			netdev_rx_csum_fault(skb->dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	return sum;
}
EXPORT_SYMBOL(__skb_checksum_complete_head);

__sum16 __skb_checksum_complete(struct sk_buff *skb)
{
	return __skb_checksum_complete_head(skb, skb->len);
}
EXPORT_SYMBOL(__skb_checksum_complete);

/**
 *	skb_copy_and_csum_datagram_iovec - Copy and checkum skb to user iovec.
 *	@skb: skbuff
 *	@hlen: hardware length
 *	@iov: io vector
 *
 *	Caller _must_ check that skb will fit to this iovec.
 *
 *	Returns: 0       - success.
 *		 -EINVAL - checksum failure.
 *		 -EFAULT - fault during copy. Beware, in this case iovec
 *			   can be modified!
 */
int skb_copy_and_csum_datagram_iovec(struct sk_buff *skb,
				     int hlen, struct iovec *iov)
{
	__wsum csum;
	int chunk = skb->len - hlen;

	if (!chunk)
		return 0;

	/* Skip filled elements.
	 * Pretty silly, look at memcpy_toiovec, though 8)
	 */
	while (!iov->iov_len)
		iov++;

	if (iov->iov_len < chunk) {
		if (__skb_checksum_complete(skb))
			goto csum_error;
		if (skb_copy_datagram_iovec(skb, hlen, iov, chunk))
			goto fault;
	} else {
		csum = csum_partial(skb->data, hlen, skb->csum);
		if (skb_copy_and_csum_datagram(skb, hlen, iov->iov_base,
					       chunk, &csum))
			goto fault;
		if (csum_fold(csum))
			goto csum_error;
		if (unlikely(skb->ip_summed == CHECKSUM_COMPLETE))
			netdev_rx_csum_fault(skb->dev);
		iov->iov_len -= chunk;
		iov->iov_base += chunk;
	}
	return 0;
csum_error:
	return -EINVAL;
fault:
	return -EFAULT;
}
EXPORT_SYMBOL(skb_copy_and_csum_datagram_iovec);

/**
 * 	datagram_poll - generic datagram poll
 *	@file: file struct
 *	@sock: socket
 *	@wait: poll table
 *
 *	Datagram poll: Again totally generic. This also handles
 *	sequenced packet sockets providing the socket receive queue
 *	is only ever holding data ready to receive.
 *
 *	Note: when you _don't_ use this routine for this protocol,
 *	and you use a different write policy from sock_writeable()
 *	then please supply your own write_space callback.
 */
unsigned int datagram_poll(struct file *file, struct socket *sock,
			   poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask;

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	/* exceptional events? */
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR |
			(sock_flag(sk, SOCK_SELECT_ERR_QUEUE) ? POLLPRI : 0);

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP | POLLIN | POLLRDNORM;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue))
		mask |= POLLIN | POLLRDNORM;

	/* Connection-based need to check for termination and startup */
	if (connection_based(sk)) {
		if (sk->sk_state == TCP_CLOSE)
			mask |= POLLHUP;
		/* connection hasn't started yet? */
		if (sk->sk_state == TCP_SYN_SENT)
			return mask;
	}

	/* writable? */
	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}
EXPORT_SYMBOL(datagram_poll);
