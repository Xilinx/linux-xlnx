/*
 * common code for virtio vsock
 *
 * Copyright (C) 2013-2015 Red Hat, Inc.
 * Author: Asias He <asias@redhat.com>
 *         Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/list.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_vsock.h>

#include <net/sock.h>
#include <net/af_vsock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/vsock_virtio_transport_common.h>

/* How long to wait for graceful shutdown of a connection */
#define VSOCK_CLOSE_TIMEOUT (8 * HZ)

static const struct virtio_transport *virtio_transport_get_ops(void)
{
	const struct vsock_transport *t = vsock_core_get_transport();

	return container_of(t, struct virtio_transport, transport);
}

struct virtio_vsock_pkt *
virtio_transport_alloc_pkt(struct virtio_vsock_pkt_info *info,
			   size_t len,
			   u32 src_cid,
			   u32 src_port,
			   u32 dst_cid,
			   u32 dst_port)
{
	struct virtio_vsock_pkt *pkt;
	int err;

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return NULL;

	pkt->hdr.type		= cpu_to_le16(info->type);
	pkt->hdr.op		= cpu_to_le16(info->op);
	pkt->hdr.src_cid	= cpu_to_le64(src_cid);
	pkt->hdr.dst_cid	= cpu_to_le64(dst_cid);
	pkt->hdr.src_port	= cpu_to_le32(src_port);
	pkt->hdr.dst_port	= cpu_to_le32(dst_port);
	pkt->hdr.flags		= cpu_to_le32(info->flags);
	pkt->len		= len;
	pkt->hdr.len		= cpu_to_le32(len);
	pkt->reply		= info->reply;

	if (info->msg && len > 0) {
		pkt->buf = kmalloc(len, GFP_KERNEL);
		if (!pkt->buf)
			goto out_pkt;
		err = memcpy_from_msg(pkt->buf, info->msg, len);
		if (err)
			goto out;
	}

	trace_virtio_transport_alloc_pkt(src_cid, src_port,
					 dst_cid, dst_port,
					 len,
					 info->type,
					 info->op,
					 info->flags);

	return pkt;

out:
	kfree(pkt->buf);
out_pkt:
	kfree(pkt);
	return NULL;
}
EXPORT_SYMBOL_GPL(virtio_transport_alloc_pkt);

static int virtio_transport_send_pkt_info(struct vsock_sock *vsk,
					  struct virtio_vsock_pkt_info *info)
{
	u32 src_cid, src_port, dst_cid, dst_port;
	struct virtio_vsock_sock *vvs;
	struct virtio_vsock_pkt *pkt;
	u32 pkt_len = info->pkt_len;

	src_cid = vm_sockets_get_local_cid();
	src_port = vsk->local_addr.svm_port;
	if (!info->remote_cid) {
		dst_cid	= vsk->remote_addr.svm_cid;
		dst_port = vsk->remote_addr.svm_port;
	} else {
		dst_cid = info->remote_cid;
		dst_port = info->remote_port;
	}

	vvs = vsk->trans;

	/* we can send less than pkt_len bytes */
	if (pkt_len > VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE)
		pkt_len = VIRTIO_VSOCK_DEFAULT_RX_BUF_SIZE;

	/* virtio_transport_get_credit might return less than pkt_len credit */
	pkt_len = virtio_transport_get_credit(vvs, pkt_len);

	/* Do not send zero length OP_RW pkt */
	if (pkt_len == 0 && info->op == VIRTIO_VSOCK_OP_RW)
		return pkt_len;

	pkt = virtio_transport_alloc_pkt(info, pkt_len,
					 src_cid, src_port,
					 dst_cid, dst_port);
	if (!pkt) {
		virtio_transport_put_credit(vvs, pkt_len);
		return -ENOMEM;
	}

	virtio_transport_inc_tx_pkt(vvs, pkt);

	return virtio_transport_get_ops()->send_pkt(pkt);
}

static void virtio_transport_inc_rx_pkt(struct virtio_vsock_sock *vvs,
					struct virtio_vsock_pkt *pkt)
{
	vvs->rx_bytes += pkt->len;
}

static void virtio_transport_dec_rx_pkt(struct virtio_vsock_sock *vvs,
					struct virtio_vsock_pkt *pkt)
{
	vvs->rx_bytes -= pkt->len;
	vvs->fwd_cnt += pkt->len;
}

void virtio_transport_inc_tx_pkt(struct virtio_vsock_sock *vvs, struct virtio_vsock_pkt *pkt)
{
	spin_lock_bh(&vvs->tx_lock);
	pkt->hdr.fwd_cnt = cpu_to_le32(vvs->fwd_cnt);
	pkt->hdr.buf_alloc = cpu_to_le32(vvs->buf_alloc);
	spin_unlock_bh(&vvs->tx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_inc_tx_pkt);

u32 virtio_transport_get_credit(struct virtio_vsock_sock *vvs, u32 credit)
{
	u32 ret;

	spin_lock_bh(&vvs->tx_lock);
	ret = vvs->peer_buf_alloc - (vvs->tx_cnt - vvs->peer_fwd_cnt);
	if (ret > credit)
		ret = credit;
	vvs->tx_cnt += ret;
	spin_unlock_bh(&vvs->tx_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_credit);

void virtio_transport_put_credit(struct virtio_vsock_sock *vvs, u32 credit)
{
	spin_lock_bh(&vvs->tx_lock);
	vvs->tx_cnt -= credit;
	spin_unlock_bh(&vvs->tx_lock);
}
EXPORT_SYMBOL_GPL(virtio_transport_put_credit);

static int virtio_transport_send_credit_update(struct vsock_sock *vsk,
					       int type,
					       struct virtio_vsock_hdr *hdr)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_CREDIT_UPDATE,
		.type = type,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}

static ssize_t
virtio_transport_stream_do_dequeue(struct vsock_sock *vsk,
				   struct msghdr *msg,
				   size_t len)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	struct virtio_vsock_pkt *pkt;
	size_t bytes, total = 0;
	int err = -EFAULT;

	spin_lock_bh(&vvs->rx_lock);
	while (total < len && !list_empty(&vvs->rx_queue)) {
		pkt = list_first_entry(&vvs->rx_queue,
				       struct virtio_vsock_pkt, list);

		bytes = len - total;
		if (bytes > pkt->len - pkt->off)
			bytes = pkt->len - pkt->off;

		/* sk_lock is held by caller so no one else can dequeue.
		 * Unlock rx_lock since memcpy_to_msg() may sleep.
		 */
		spin_unlock_bh(&vvs->rx_lock);

		err = memcpy_to_msg(msg, pkt->buf + pkt->off, bytes);
		if (err)
			goto out;

		spin_lock_bh(&vvs->rx_lock);

		total += bytes;
		pkt->off += bytes;
		if (pkt->off == pkt->len) {
			virtio_transport_dec_rx_pkt(vvs, pkt);
			list_del(&pkt->list);
			virtio_transport_free_pkt(pkt);
		}
	}
	spin_unlock_bh(&vvs->rx_lock);

	/* Send a credit pkt to peer */
	virtio_transport_send_credit_update(vsk, VIRTIO_VSOCK_TYPE_STREAM,
					    NULL);

	return total;

out:
	if (total)
		err = total;
	return err;
}

ssize_t
virtio_transport_stream_dequeue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len, int flags)
{
	if (flags & MSG_PEEK)
		return -EOPNOTSUPP;

	return virtio_transport_stream_do_dequeue(vsk, msg, len);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_dequeue);

int
virtio_transport_dgram_dequeue(struct vsock_sock *vsk,
			       struct msghdr *msg,
			       size_t len, int flags)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_dequeue);

s64 virtio_transport_stream_has_data(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	spin_lock_bh(&vvs->rx_lock);
	bytes = vvs->rx_bytes;
	spin_unlock_bh(&vvs->rx_lock);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_data);

static s64 virtio_transport_has_space(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	bytes = vvs->peer_buf_alloc - (vvs->tx_cnt - vvs->peer_fwd_cnt);
	if (bytes < 0)
		bytes = 0;

	return bytes;
}

s64 virtio_transport_stream_has_space(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;
	s64 bytes;

	spin_lock_bh(&vvs->tx_lock);
	bytes = virtio_transport_has_space(vsk);
	spin_unlock_bh(&vvs->tx_lock);

	return bytes;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_has_space);

int virtio_transport_do_socket_init(struct vsock_sock *vsk,
				    struct vsock_sock *psk)
{
	struct virtio_vsock_sock *vvs;

	vvs = kzalloc(sizeof(*vvs), GFP_KERNEL);
	if (!vvs)
		return -ENOMEM;

	vsk->trans = vvs;
	vvs->vsk = vsk;
	if (psk) {
		struct virtio_vsock_sock *ptrans = psk->trans;

		vvs->buf_size	= ptrans->buf_size;
		vvs->buf_size_min = ptrans->buf_size_min;
		vvs->buf_size_max = ptrans->buf_size_max;
		vvs->peer_buf_alloc = ptrans->peer_buf_alloc;
	} else {
		vvs->buf_size = VIRTIO_VSOCK_DEFAULT_BUF_SIZE;
		vvs->buf_size_min = VIRTIO_VSOCK_DEFAULT_MIN_BUF_SIZE;
		vvs->buf_size_max = VIRTIO_VSOCK_DEFAULT_MAX_BUF_SIZE;
	}

	vvs->buf_alloc = vvs->buf_size;

	spin_lock_init(&vvs->rx_lock);
	spin_lock_init(&vvs->tx_lock);
	INIT_LIST_HEAD(&vvs->rx_queue);

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_do_socket_init);

u64 virtio_transport_get_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	return vvs->buf_size;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_buffer_size);

u64 virtio_transport_get_min_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	return vvs->buf_size_min;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_min_buffer_size);

u64 virtio_transport_get_max_buffer_size(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	return vvs->buf_size_max;
}
EXPORT_SYMBOL_GPL(virtio_transport_get_max_buffer_size);

void virtio_transport_set_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val < vvs->buf_size_min)
		vvs->buf_size_min = val;
	if (val > vvs->buf_size_max)
		vvs->buf_size_max = val;
	vvs->buf_size = val;
	vvs->buf_alloc = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_buffer_size);

void virtio_transport_set_min_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val > vvs->buf_size)
		vvs->buf_size = val;
	vvs->buf_size_min = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_min_buffer_size);

void virtio_transport_set_max_buffer_size(struct vsock_sock *vsk, u64 val)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	if (val > VIRTIO_VSOCK_MAX_BUF_SIZE)
		val = VIRTIO_VSOCK_MAX_BUF_SIZE;
	if (val < vvs->buf_size)
		vvs->buf_size = val;
	vvs->buf_size_max = val;
}
EXPORT_SYMBOL_GPL(virtio_transport_set_max_buffer_size);

int
virtio_transport_notify_poll_in(struct vsock_sock *vsk,
				size_t target,
				bool *data_ready_now)
{
	if (vsock_stream_has_data(vsk))
		*data_ready_now = true;
	else
		*data_ready_now = false;

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_poll_in);

int
virtio_transport_notify_poll_out(struct vsock_sock *vsk,
				 size_t target,
				 bool *space_avail_now)
{
	s64 free_space;

	free_space = vsock_stream_has_space(vsk);
	if (free_space > 0)
		*space_avail_now = true;
	else if (free_space == 0)
		*space_avail_now = false;

	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_poll_out);

int virtio_transport_notify_recv_init(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_init);

int virtio_transport_notify_recv_pre_block(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_pre_block);

int virtio_transport_notify_recv_pre_dequeue(struct vsock_sock *vsk,
	size_t target, struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_pre_dequeue);

int virtio_transport_notify_recv_post_dequeue(struct vsock_sock *vsk,
	size_t target, ssize_t copied, bool data_read,
	struct vsock_transport_recv_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_recv_post_dequeue);

int virtio_transport_notify_send_init(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_init);

int virtio_transport_notify_send_pre_block(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_pre_block);

int virtio_transport_notify_send_pre_enqueue(struct vsock_sock *vsk,
	struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_pre_enqueue);

int virtio_transport_notify_send_post_enqueue(struct vsock_sock *vsk,
	ssize_t written, struct vsock_transport_send_notify_data *data)
{
	return 0;
}
EXPORT_SYMBOL_GPL(virtio_transport_notify_send_post_enqueue);

u64 virtio_transport_stream_rcvhiwat(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	return vvs->buf_size;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_rcvhiwat);

bool virtio_transport_stream_is_active(struct vsock_sock *vsk)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_is_active);

bool virtio_transport_stream_allow(u32 cid, u32 port)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_allow);

int virtio_transport_dgram_bind(struct vsock_sock *vsk,
				struct sockaddr_vm *addr)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_bind);

bool virtio_transport_dgram_allow(u32 cid, u32 port)
{
	return false;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_allow);

int virtio_transport_connect(struct vsock_sock *vsk)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_REQUEST,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_connect);

int virtio_transport_shutdown(struct vsock_sock *vsk, int mode)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_SHUTDOWN,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.flags = (mode & RCV_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_RCV : 0) |
			 (mode & SEND_SHUTDOWN ?
			  VIRTIO_VSOCK_SHUTDOWN_SEND : 0),
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_shutdown);

int
virtio_transport_dgram_enqueue(struct vsock_sock *vsk,
			       struct sockaddr_vm *remote_addr,
			       struct msghdr *msg,
			       size_t dgram_len)
{
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL_GPL(virtio_transport_dgram_enqueue);

ssize_t
virtio_transport_stream_enqueue(struct vsock_sock *vsk,
				struct msghdr *msg,
				size_t len)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RW,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.msg = msg,
		.pkt_len = len,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}
EXPORT_SYMBOL_GPL(virtio_transport_stream_enqueue);

void virtio_transport_destruct(struct vsock_sock *vsk)
{
	struct virtio_vsock_sock *vvs = vsk->trans;

	kfree(vvs);
}
EXPORT_SYMBOL_GPL(virtio_transport_destruct);

static int virtio_transport_reset(struct vsock_sock *vsk,
				  struct virtio_vsock_pkt *pkt)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RST,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.reply = !!pkt,
	};

	/* Send RST only if the original pkt is not a RST pkt */
	if (pkt && le16_to_cpu(pkt->hdr.op) == VIRTIO_VSOCK_OP_RST)
		return 0;

	return virtio_transport_send_pkt_info(vsk, &info);
}

/* Normally packets are associated with a socket.  There may be no socket if an
 * attempt was made to connect to a socket that does not exist.
 */
static int virtio_transport_reset_no_sock(struct virtio_vsock_pkt *pkt)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RST,
		.type = le16_to_cpu(pkt->hdr.type),
		.reply = true,
	};

	/* Send RST only if the original pkt is not a RST pkt */
	if (le16_to_cpu(pkt->hdr.op) == VIRTIO_VSOCK_OP_RST)
		return 0;

	pkt = virtio_transport_alloc_pkt(&info, 0,
					 le32_to_cpu(pkt->hdr.dst_cid),
					 le32_to_cpu(pkt->hdr.dst_port),
					 le32_to_cpu(pkt->hdr.src_cid),
					 le32_to_cpu(pkt->hdr.src_port));
	if (!pkt)
		return -ENOMEM;

	return virtio_transport_get_ops()->send_pkt(pkt);
}

static void virtio_transport_wait_close(struct sock *sk, long timeout)
{
	if (timeout) {
		DEFINE_WAIT(wait);

		do {
			prepare_to_wait(sk_sleep(sk), &wait,
					TASK_INTERRUPTIBLE);
			if (sk_wait_event(sk, &timeout,
					  sock_flag(sk, SOCK_DONE)))
				break;
		} while (!signal_pending(current) && timeout);

		finish_wait(sk_sleep(sk), &wait);
	}
}

static void virtio_transport_do_close(struct vsock_sock *vsk,
				      bool cancel_timeout)
{
	struct sock *sk = sk_vsock(vsk);

	sock_set_flag(sk, SOCK_DONE);
	vsk->peer_shutdown = SHUTDOWN_MASK;
	if (vsock_stream_has_data(vsk) <= 0)
		sk->sk_state = SS_DISCONNECTING;
	sk->sk_state_change(sk);

	if (vsk->close_work_scheduled &&
	    (!cancel_timeout || cancel_delayed_work(&vsk->close_work))) {
		vsk->close_work_scheduled = false;

		vsock_remove_sock(vsk);

		/* Release refcnt obtained when we scheduled the timeout */
		sock_put(sk);
	}
}

static void virtio_transport_close_timeout(struct work_struct *work)
{
	struct vsock_sock *vsk =
		container_of(work, struct vsock_sock, close_work.work);
	struct sock *sk = sk_vsock(vsk);

	sock_hold(sk);
	lock_sock(sk);

	if (!sock_flag(sk, SOCK_DONE)) {
		(void)virtio_transport_reset(vsk, NULL);

		virtio_transport_do_close(vsk, false);
	}

	vsk->close_work_scheduled = false;

	release_sock(sk);
	sock_put(sk);
}

/* User context, vsk->sk is locked */
static bool virtio_transport_close(struct vsock_sock *vsk)
{
	struct sock *sk = &vsk->sk;

	if (!(sk->sk_state == SS_CONNECTED ||
	      sk->sk_state == SS_DISCONNECTING))
		return true;

	/* Already received SHUTDOWN from peer, reply with RST */
	if ((vsk->peer_shutdown & SHUTDOWN_MASK) == SHUTDOWN_MASK) {
		(void)virtio_transport_reset(vsk, NULL);
		return true;
	}

	if ((sk->sk_shutdown & SHUTDOWN_MASK) != SHUTDOWN_MASK)
		(void)virtio_transport_shutdown(vsk, SHUTDOWN_MASK);

	if (sock_flag(sk, SOCK_LINGER) && !(current->flags & PF_EXITING))
		virtio_transport_wait_close(sk, sk->sk_lingertime);

	if (sock_flag(sk, SOCK_DONE)) {
		return true;
	}

	sock_hold(sk);
	INIT_DELAYED_WORK(&vsk->close_work,
			  virtio_transport_close_timeout);
	vsk->close_work_scheduled = true;
	schedule_delayed_work(&vsk->close_work, VSOCK_CLOSE_TIMEOUT);
	return false;
}

void virtio_transport_release(struct vsock_sock *vsk)
{
	struct sock *sk = &vsk->sk;
	bool remove_sock = true;

	lock_sock(sk);
	if (sk->sk_type == SOCK_STREAM)
		remove_sock = virtio_transport_close(vsk);
	release_sock(sk);

	if (remove_sock)
		vsock_remove_sock(vsk);
}
EXPORT_SYMBOL_GPL(virtio_transport_release);

static int
virtio_transport_recv_connecting(struct sock *sk,
				 struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	int err;
	int skerr;

	switch (le16_to_cpu(pkt->hdr.op)) {
	case VIRTIO_VSOCK_OP_RESPONSE:
		sk->sk_state = SS_CONNECTED;
		sk->sk_socket->state = SS_CONNECTED;
		vsock_insert_connected(vsk);
		sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_INVALID:
		break;
	case VIRTIO_VSOCK_OP_RST:
		skerr = ECONNRESET;
		err = 0;
		goto destroy;
	default:
		skerr = EPROTO;
		err = -EINVAL;
		goto destroy;
	}
	return 0;

destroy:
	virtio_transport_reset(vsk, pkt);
	sk->sk_state = SS_UNCONNECTED;
	sk->sk_err = skerr;
	sk->sk_error_report(sk);
	return err;
}

static int
virtio_transport_recv_connected(struct sock *sk,
				struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct virtio_vsock_sock *vvs = vsk->trans;
	int err = 0;

	switch (le16_to_cpu(pkt->hdr.op)) {
	case VIRTIO_VSOCK_OP_RW:
		pkt->len = le32_to_cpu(pkt->hdr.len);
		pkt->off = 0;

		spin_lock_bh(&vvs->rx_lock);
		virtio_transport_inc_rx_pkt(vvs, pkt);
		list_add_tail(&pkt->list, &vvs->rx_queue);
		spin_unlock_bh(&vvs->rx_lock);

		sk->sk_data_ready(sk);
		return err;
	case VIRTIO_VSOCK_OP_CREDIT_UPDATE:
		sk->sk_write_space(sk);
		break;
	case VIRTIO_VSOCK_OP_SHUTDOWN:
		if (le32_to_cpu(pkt->hdr.flags) & VIRTIO_VSOCK_SHUTDOWN_RCV)
			vsk->peer_shutdown |= RCV_SHUTDOWN;
		if (le32_to_cpu(pkt->hdr.flags) & VIRTIO_VSOCK_SHUTDOWN_SEND)
			vsk->peer_shutdown |= SEND_SHUTDOWN;
		if (vsk->peer_shutdown == SHUTDOWN_MASK &&
		    vsock_stream_has_data(vsk) <= 0)
			sk->sk_state = SS_DISCONNECTING;
		if (le32_to_cpu(pkt->hdr.flags))
			sk->sk_state_change(sk);
		break;
	case VIRTIO_VSOCK_OP_RST:
		virtio_transport_do_close(vsk, true);
		break;
	default:
		err = -EINVAL;
		break;
	}

	virtio_transport_free_pkt(pkt);
	return err;
}

static void
virtio_transport_recv_disconnecting(struct sock *sk,
				    struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);

	if (le16_to_cpu(pkt->hdr.op) == VIRTIO_VSOCK_OP_RST)
		virtio_transport_do_close(vsk, true);
}

static int
virtio_transport_send_response(struct vsock_sock *vsk,
			       struct virtio_vsock_pkt *pkt)
{
	struct virtio_vsock_pkt_info info = {
		.op = VIRTIO_VSOCK_OP_RESPONSE,
		.type = VIRTIO_VSOCK_TYPE_STREAM,
		.remote_cid = le32_to_cpu(pkt->hdr.src_cid),
		.remote_port = le32_to_cpu(pkt->hdr.src_port),
		.reply = true,
	};

	return virtio_transport_send_pkt_info(vsk, &info);
}

/* Handle server socket */
static int
virtio_transport_recv_listen(struct sock *sk, struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct vsock_sock *vchild;
	struct sock *child;

	if (le16_to_cpu(pkt->hdr.op) != VIRTIO_VSOCK_OP_REQUEST) {
		virtio_transport_reset(vsk, pkt);
		return -EINVAL;
	}

	if (sk_acceptq_is_full(sk)) {
		virtio_transport_reset(vsk, pkt);
		return -ENOMEM;
	}

	child = __vsock_create(sock_net(sk), NULL, sk, GFP_KERNEL,
			       sk->sk_type, 0);
	if (!child) {
		virtio_transport_reset(vsk, pkt);
		return -ENOMEM;
	}

	sk->sk_ack_backlog++;

	lock_sock_nested(child, SINGLE_DEPTH_NESTING);

	child->sk_state = SS_CONNECTED;

	vchild = vsock_sk(child);
	vsock_addr_init(&vchild->local_addr, le32_to_cpu(pkt->hdr.dst_cid),
			le32_to_cpu(pkt->hdr.dst_port));
	vsock_addr_init(&vchild->remote_addr, le32_to_cpu(pkt->hdr.src_cid),
			le32_to_cpu(pkt->hdr.src_port));

	vsock_insert_connected(vchild);
	vsock_enqueue_accept(sk, child);
	virtio_transport_send_response(vchild, pkt);

	release_sock(child);

	sk->sk_data_ready(sk);
	return 0;
}

static bool virtio_transport_space_update(struct sock *sk,
					  struct virtio_vsock_pkt *pkt)
{
	struct vsock_sock *vsk = vsock_sk(sk);
	struct virtio_vsock_sock *vvs = vsk->trans;
	bool space_available;

	/* buf_alloc and fwd_cnt is always included in the hdr */
	spin_lock_bh(&vvs->tx_lock);
	vvs->peer_buf_alloc = le32_to_cpu(pkt->hdr.buf_alloc);
	vvs->peer_fwd_cnt = le32_to_cpu(pkt->hdr.fwd_cnt);
	space_available = virtio_transport_has_space(vsk);
	spin_unlock_bh(&vvs->tx_lock);
	return space_available;
}

/* We are under the virtio-vsock's vsock->rx_lock or vhost-vsock's vq->mutex
 * lock.
 */
void virtio_transport_recv_pkt(struct virtio_vsock_pkt *pkt)
{
	struct sockaddr_vm src, dst;
	struct vsock_sock *vsk;
	struct sock *sk;
	bool space_available;

	vsock_addr_init(&src, le32_to_cpu(pkt->hdr.src_cid),
			le32_to_cpu(pkt->hdr.src_port));
	vsock_addr_init(&dst, le32_to_cpu(pkt->hdr.dst_cid),
			le32_to_cpu(pkt->hdr.dst_port));

	trace_virtio_transport_recv_pkt(src.svm_cid, src.svm_port,
					dst.svm_cid, dst.svm_port,
					le32_to_cpu(pkt->hdr.len),
					le16_to_cpu(pkt->hdr.type),
					le16_to_cpu(pkt->hdr.op),
					le32_to_cpu(pkt->hdr.flags),
					le32_to_cpu(pkt->hdr.buf_alloc),
					le32_to_cpu(pkt->hdr.fwd_cnt));

	if (le16_to_cpu(pkt->hdr.type) != VIRTIO_VSOCK_TYPE_STREAM) {
		(void)virtio_transport_reset_no_sock(pkt);
		goto free_pkt;
	}

	/* The socket must be in connected or bound table
	 * otherwise send reset back
	 */
	sk = vsock_find_connected_socket(&src, &dst);
	if (!sk) {
		sk = vsock_find_bound_socket(&dst);
		if (!sk) {
			(void)virtio_transport_reset_no_sock(pkt);
			goto free_pkt;
		}
	}

	vsk = vsock_sk(sk);

	space_available = virtio_transport_space_update(sk, pkt);

	lock_sock(sk);

	/* Update CID in case it has changed after a transport reset event */
	vsk->local_addr.svm_cid = dst.svm_cid;

	if (space_available)
		sk->sk_write_space(sk);

	switch (sk->sk_state) {
	case VSOCK_SS_LISTEN:
		virtio_transport_recv_listen(sk, pkt);
		virtio_transport_free_pkt(pkt);
		break;
	case SS_CONNECTING:
		virtio_transport_recv_connecting(sk, pkt);
		virtio_transport_free_pkt(pkt);
		break;
	case SS_CONNECTED:
		virtio_transport_recv_connected(sk, pkt);
		break;
	case SS_DISCONNECTING:
		virtio_transport_recv_disconnecting(sk, pkt);
		virtio_transport_free_pkt(pkt);
		break;
	default:
		virtio_transport_free_pkt(pkt);
		break;
	}
	release_sock(sk);

	/* Release refcnt obtained when we fetched this socket out of the
	 * bound or connected list.
	 */
	sock_put(sk);
	return;

free_pkt:
	virtio_transport_free_pkt(pkt);
}
EXPORT_SYMBOL_GPL(virtio_transport_recv_pkt);

void virtio_transport_free_pkt(struct virtio_vsock_pkt *pkt)
{
	kfree(pkt->buf);
	kfree(pkt);
}
EXPORT_SYMBOL_GPL(virtio_transport_free_pkt);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Asias He");
MODULE_DESCRIPTION("common code for virtio vsock");
