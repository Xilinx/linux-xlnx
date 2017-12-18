/* Kerberos-based RxRPC security
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/skcipher.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/scatterlist.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <keys/rxrpc-type.h>
#include "ar-internal.h"

#define RXKAD_VERSION			2
#define MAXKRB5TICKETLEN		1024
#define RXKAD_TKT_TYPE_KERBEROS_V5	256
#define ANAME_SZ			40	/* size of authentication name */
#define INST_SZ				40	/* size of principal's instance */
#define REALM_SZ			40	/* size of principal's auth domain */
#define SNAME_SZ			40	/* size of service name */

struct rxkad_level1_hdr {
	__be32	data_size;	/* true data size (excluding padding) */
};

struct rxkad_level2_hdr {
	__be32	data_size;	/* true data size (excluding padding) */
	__be32	checksum;	/* decrypted data checksum */
};

/*
 * this holds a pinned cipher so that keventd doesn't get called by the cipher
 * alloc routine, but since we have it to hand, we use it to decrypt RESPONSE
 * packets
 */
static struct crypto_skcipher *rxkad_ci;
static DEFINE_MUTEX(rxkad_ci_mutex);

/*
 * initialise connection security
 */
static int rxkad_init_connection_security(struct rxrpc_connection *conn)
{
	struct crypto_skcipher *ci;
	struct rxrpc_key_token *token;
	int ret;

	_enter("{%d},{%x}", conn->debug_id, key_serial(conn->params.key));

	token = conn->params.key->payload.data[0];
	conn->security_ix = token->security_index;

	ci = crypto_alloc_skcipher("pcbc(fcrypt)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(ci)) {
		_debug("no cipher");
		ret = PTR_ERR(ci);
		goto error;
	}

	if (crypto_skcipher_setkey(ci, token->kad->session_key,
				   sizeof(token->kad->session_key)) < 0)
		BUG();

	switch (conn->params.security_level) {
	case RXRPC_SECURITY_PLAIN:
		break;
	case RXRPC_SECURITY_AUTH:
		conn->size_align = 8;
		conn->security_size = sizeof(struct rxkad_level1_hdr);
		break;
	case RXRPC_SECURITY_ENCRYPT:
		conn->size_align = 8;
		conn->security_size = sizeof(struct rxkad_level2_hdr);
		break;
	default:
		ret = -EKEYREJECTED;
		goto error;
	}

	conn->cipher = ci;
	ret = 0;
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * prime the encryption state with the invariant parts of a connection's
 * description
 */
static int rxkad_prime_packet_security(struct rxrpc_connection *conn)
{
	struct rxrpc_key_token *token;
	SKCIPHER_REQUEST_ON_STACK(req, conn->cipher);
	struct scatterlist sg;
	struct rxrpc_crypt iv;
	__be32 *tmpbuf;
	size_t tmpsize = 4 * sizeof(__be32);

	_enter("");

	if (!conn->params.key)
		return 0;

	tmpbuf = kmalloc(tmpsize, GFP_KERNEL);
	if (!tmpbuf)
		return -ENOMEM;

	token = conn->params.key->payload.data[0];
	memcpy(&iv, token->kad->session_key, sizeof(iv));

	tmpbuf[0] = htonl(conn->proto.epoch);
	tmpbuf[1] = htonl(conn->proto.cid);
	tmpbuf[2] = 0;
	tmpbuf[3] = htonl(conn->security_ix);

	sg_init_one(&sg, tmpbuf, tmpsize);
	skcipher_request_set_tfm(req, conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, tmpsize, iv.x);
	crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);

	memcpy(&conn->csum_iv, tmpbuf + 2, sizeof(conn->csum_iv));
	kfree(tmpbuf);
	_leave(" = 0");
	return 0;
}

/*
 * partially encrypt a packet (level 1 security)
 */
static int rxkad_secure_packet_auth(const struct rxrpc_call *call,
				    struct sk_buff *skb,
				    u32 data_size,
				    void *sechdr)
{
	struct rxrpc_skb_priv *sp;
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxkad_level1_hdr hdr;
	struct rxrpc_crypt iv;
	struct scatterlist sg;
	u16 check;

	sp = rxrpc_skb(skb);

	_enter("");

	check = sp->hdr.seq ^ call->call_id;
	data_size |= (u32)check << 16;

	hdr.data_size = htonl(data_size);
	memcpy(sechdr, &hdr, sizeof(hdr));

	/* start the encryption afresh */
	memset(&iv, 0, sizeof(iv));

	sg_init_one(&sg, sechdr, 8);
	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, 8, iv.x);
	crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);

	_leave(" = 0");
	return 0;
}

/*
 * wholly encrypt a packet (level 2 security)
 */
static int rxkad_secure_packet_encrypt(const struct rxrpc_call *call,
				       struct sk_buff *skb,
				       u32 data_size,
				       void *sechdr)
{
	const struct rxrpc_key_token *token;
	struct rxkad_level2_hdr rxkhdr;
	struct rxrpc_skb_priv *sp;
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist sg[16];
	struct sk_buff *trailer;
	unsigned int len;
	u16 check;
	int nsg;
	int err;

	sp = rxrpc_skb(skb);

	_enter("");

	check = sp->hdr.seq ^ call->call_id;

	rxkhdr.data_size = htonl(data_size | (u32)check << 16);
	rxkhdr.checksum = 0;
	memcpy(sechdr, &rxkhdr, sizeof(rxkhdr));

	/* encrypt from the session key */
	token = call->conn->params.key->payload.data[0];
	memcpy(&iv, token->kad->session_key, sizeof(iv));

	sg_init_one(&sg[0], sechdr, sizeof(rxkhdr));
	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg[0], &sg[0], sizeof(rxkhdr), iv.x);
	crypto_skcipher_encrypt(req);

	/* we want to encrypt the skbuff in-place */
	nsg = skb_cow_data(skb, 0, &trailer);
	err = -ENOMEM;
	if (nsg < 0 || nsg > 16)
		goto out;

	len = data_size + call->conn->size_align - 1;
	len &= ~(call->conn->size_align - 1);

	sg_init_table(sg, nsg);
	skb_to_sgvec(skb, sg, 0, len);
	skcipher_request_set_crypt(req, sg, sg, len, iv.x);
	crypto_skcipher_encrypt(req);

	_leave(" = 0");
	err = 0;

out:
	skcipher_request_zero(req);
	return err;
}

/*
 * checksum an RxRPC packet header
 */
static int rxkad_secure_packet(struct rxrpc_call *call,
			       struct sk_buff *skb,
			       size_t data_size,
			       void *sechdr)
{
	struct rxrpc_skb_priv *sp;
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist sg;
	u32 x, y;
	int ret;

	sp = rxrpc_skb(skb);

	_enter("{%d{%x}},{#%u},%zu,",
	       call->debug_id, key_serial(call->conn->params.key),
	       sp->hdr.seq, data_size);

	if (!call->conn->cipher)
		return 0;

	ret = key_validate(call->conn->params.key);
	if (ret < 0)
		return ret;

	/* continue encrypting from where we left off */
	memcpy(&iv, call->conn->csum_iv.x, sizeof(iv));

	/* calculate the security checksum */
	x = (call->cid & RXRPC_CHANNELMASK) << (32 - RXRPC_CIDSHIFT);
	x |= sp->hdr.seq & 0x3fffffff;
	call->crypto_buf[0] = htonl(call->call_id);
	call->crypto_buf[1] = htonl(x);

	sg_init_one(&sg, call->crypto_buf, 8);
	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, 8, iv.x);
	crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);

	y = ntohl(call->crypto_buf[1]);
	y = (y >> 16) & 0xffff;
	if (y == 0)
		y = 1; /* zero checksums are not permitted */
	sp->hdr.cksum = y;

	switch (call->conn->params.security_level) {
	case RXRPC_SECURITY_PLAIN:
		ret = 0;
		break;
	case RXRPC_SECURITY_AUTH:
		ret = rxkad_secure_packet_auth(call, skb, data_size, sechdr);
		break;
	case RXRPC_SECURITY_ENCRYPT:
		ret = rxkad_secure_packet_encrypt(call, skb, data_size,
						  sechdr);
		break;
	default:
		ret = -EPERM;
		break;
	}

	_leave(" = %d [set %hx]", ret, y);
	return ret;
}

/*
 * decrypt partial encryption on a packet (level 1 security)
 */
static int rxkad_verify_packet_1(struct rxrpc_call *call, struct sk_buff *skb,
				 unsigned int offset, unsigned int len,
				 rxrpc_seq_t seq)
{
	struct rxkad_level1_hdr sechdr;
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist sg[16];
	struct sk_buff *trailer;
	u32 data_size, buf;
	u16 check;
	int nsg;

	_enter("");

	if (len < 8) {
		rxrpc_abort_call("V1H", call, seq, RXKADSEALEDINCON, EPROTO);
		goto protocol_error;
	}

	/* Decrypt the skbuff in-place.  TODO: We really want to decrypt
	 * directly into the target buffer.
	 */
	nsg = skb_cow_data(skb, 0, &trailer);
	if (nsg < 0 || nsg > 16)
		goto nomem;

	sg_init_table(sg, nsg);
	skb_to_sgvec(skb, sg, offset, 8);

	/* start the decryption afresh */
	memset(&iv, 0, sizeof(iv));

	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, 8, iv.x);
	crypto_skcipher_decrypt(req);
	skcipher_request_zero(req);

	/* Extract the decrypted packet length */
	if (skb_copy_bits(skb, offset, &sechdr, sizeof(sechdr)) < 0) {
		rxrpc_abort_call("XV1", call, seq, RXKADDATALEN, EPROTO);
		goto protocol_error;
	}
	offset += sizeof(sechdr);
	len -= sizeof(sechdr);

	buf = ntohl(sechdr.data_size);
	data_size = buf & 0xffff;

	check = buf >> 16;
	check ^= seq ^ call->call_id;
	check &= 0xffff;
	if (check != 0) {
		rxrpc_abort_call("V1C", call, seq, RXKADSEALEDINCON, EPROTO);
		goto protocol_error;
	}

	if (data_size > len) {
		rxrpc_abort_call("V1L", call, seq, RXKADDATALEN, EPROTO);
		goto protocol_error;
	}

	_leave(" = 0 [dlen=%x]", data_size);
	return 0;

protocol_error:
	rxrpc_send_abort_packet(call);
	_leave(" = -EPROTO");
	return -EPROTO;

nomem:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * wholly decrypt a packet (level 2 security)
 */
static int rxkad_verify_packet_2(struct rxrpc_call *call, struct sk_buff *skb,
				 unsigned int offset, unsigned int len,
				 rxrpc_seq_t seq)
{
	const struct rxrpc_key_token *token;
	struct rxkad_level2_hdr sechdr;
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist _sg[4], *sg;
	struct sk_buff *trailer;
	u32 data_size, buf;
	u16 check;
	int nsg;

	_enter(",{%d}", skb->len);

	if (len < 8) {
		rxrpc_abort_call("V2H", call, seq, RXKADSEALEDINCON, EPROTO);
		goto protocol_error;
	}

	/* Decrypt the skbuff in-place.  TODO: We really want to decrypt
	 * directly into the target buffer.
	 */
	nsg = skb_cow_data(skb, 0, &trailer);
	if (nsg < 0)
		goto nomem;

	sg = _sg;
	if (unlikely(nsg > 4)) {
		sg = kmalloc(sizeof(*sg) * nsg, GFP_NOIO);
		if (!sg)
			goto nomem;
	}

	sg_init_table(sg, nsg);
	skb_to_sgvec(skb, sg, offset, len);

	/* decrypt from the session key */
	token = call->conn->params.key->payload.data[0];
	memcpy(&iv, token->kad->session_key, sizeof(iv));

	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, len, iv.x);
	crypto_skcipher_decrypt(req);
	skcipher_request_zero(req);
	if (sg != _sg)
		kfree(sg);

	/* Extract the decrypted packet length */
	if (skb_copy_bits(skb, offset, &sechdr, sizeof(sechdr)) < 0) {
		rxrpc_abort_call("XV2", call, seq, RXKADDATALEN, EPROTO);
		goto protocol_error;
	}
	offset += sizeof(sechdr);
	len -= sizeof(sechdr);

	buf = ntohl(sechdr.data_size);
	data_size = buf & 0xffff;

	check = buf >> 16;
	check ^= seq ^ call->call_id;
	check &= 0xffff;
	if (check != 0) {
		rxrpc_abort_call("V2C", call, seq, RXKADSEALEDINCON, EPROTO);
		goto protocol_error;
	}

	if (data_size > len) {
		rxrpc_abort_call("V2L", call, seq, RXKADDATALEN, EPROTO);
		goto protocol_error;
	}

	_leave(" = 0 [dlen=%x]", data_size);
	return 0;

protocol_error:
	rxrpc_send_abort_packet(call);
	_leave(" = -EPROTO");
	return -EPROTO;

nomem:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}

/*
 * Verify the security on a received packet or subpacket (if part of a
 * jumbo packet).
 */
static int rxkad_verify_packet(struct rxrpc_call *call, struct sk_buff *skb,
			       unsigned int offset, unsigned int len,
			       rxrpc_seq_t seq, u16 expected_cksum)
{
	SKCIPHER_REQUEST_ON_STACK(req, call->conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist sg;
	u16 cksum;
	u32 x, y;

	_enter("{%d{%x}},{#%u}",
	       call->debug_id, key_serial(call->conn->params.key), seq);

	if (!call->conn->cipher)
		return 0;

	/* continue encrypting from where we left off */
	memcpy(&iv, call->conn->csum_iv.x, sizeof(iv));

	/* validate the security checksum */
	x = (call->cid & RXRPC_CHANNELMASK) << (32 - RXRPC_CIDSHIFT);
	x |= seq & 0x3fffffff;
	call->crypto_buf[0] = htonl(call->call_id);
	call->crypto_buf[1] = htonl(x);

	sg_init_one(&sg, call->crypto_buf, 8);
	skcipher_request_set_tfm(req, call->conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, 8, iv.x);
	crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);

	y = ntohl(call->crypto_buf[1]);
	cksum = (y >> 16) & 0xffff;
	if (cksum == 0)
		cksum = 1; /* zero checksums are not permitted */

	if (cksum != expected_cksum) {
		rxrpc_abort_call("VCK", call, seq, RXKADSEALEDINCON, EPROTO);
		rxrpc_send_abort_packet(call);
		_leave(" = -EPROTO [csum failed]");
		return -EPROTO;
	}

	switch (call->conn->params.security_level) {
	case RXRPC_SECURITY_PLAIN:
		return 0;
	case RXRPC_SECURITY_AUTH:
		return rxkad_verify_packet_1(call, skb, offset, len, seq);
	case RXRPC_SECURITY_ENCRYPT:
		return rxkad_verify_packet_2(call, skb, offset, len, seq);
	default:
		return -ENOANO;
	}
}

/*
 * Locate the data contained in a packet that was partially encrypted.
 */
static void rxkad_locate_data_1(struct rxrpc_call *call, struct sk_buff *skb,
				unsigned int *_offset, unsigned int *_len)
{
	struct rxkad_level1_hdr sechdr;

	if (skb_copy_bits(skb, *_offset, &sechdr, sizeof(sechdr)) < 0)
		BUG();
	*_offset += sizeof(sechdr);
	*_len = ntohl(sechdr.data_size) & 0xffff;
}

/*
 * Locate the data contained in a packet that was completely encrypted.
 */
static void rxkad_locate_data_2(struct rxrpc_call *call, struct sk_buff *skb,
				unsigned int *_offset, unsigned int *_len)
{
	struct rxkad_level2_hdr sechdr;

	if (skb_copy_bits(skb, *_offset, &sechdr, sizeof(sechdr)) < 0)
		BUG();
	*_offset += sizeof(sechdr);
	*_len = ntohl(sechdr.data_size) & 0xffff;
}

/*
 * Locate the data contained in an already decrypted packet.
 */
static void rxkad_locate_data(struct rxrpc_call *call, struct sk_buff *skb,
			      unsigned int *_offset, unsigned int *_len)
{
	switch (call->conn->params.security_level) {
	case RXRPC_SECURITY_AUTH:
		rxkad_locate_data_1(call, skb, _offset, _len);
		return;
	case RXRPC_SECURITY_ENCRYPT:
		rxkad_locate_data_2(call, skb, _offset, _len);
		return;
	default:
		return;
	}
}

/*
 * issue a challenge
 */
static int rxkad_issue_challenge(struct rxrpc_connection *conn)
{
	struct rxkad_challenge challenge;
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[2];
	size_t len;
	u32 serial;
	int ret;

	_enter("{%d,%x}", conn->debug_id, key_serial(conn->params.key));

	ret = key_validate(conn->params.key);
	if (ret < 0)
		return ret;

	get_random_bytes(&conn->security_nonce, sizeof(conn->security_nonce));

	challenge.version	= htonl(2);
	challenge.nonce		= htonl(conn->security_nonce);
	challenge.min_level	= htonl(0);
	challenge.__padding	= 0;

	msg.msg_name	= &conn->params.peer->srx.transport.sin;
	msg.msg_namelen	= sizeof(conn->params.peer->srx.transport.sin);
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(conn->proto.epoch);
	whdr.cid	= htonl(conn->proto.cid);
	whdr.callNumber	= 0;
	whdr.seq	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_CHALLENGE;
	whdr.flags	= conn->out_clientflag;
	whdr.userStatus	= 0;
	whdr.securityIndex = conn->security_ix;
	whdr._rsvd	= 0;
	whdr.serviceId	= htons(conn->params.service_id);

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= &challenge;
	iov[1].iov_len	= sizeof(challenge);

	len = iov[0].iov_len + iov[1].iov_len;

	serial = atomic_inc_return(&conn->serial);
	whdr.serial = htonl(serial);
	_proto("Tx CHALLENGE %%%u", serial);

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 2, len);
	if (ret < 0) {
		_debug("sendmsg failed: %d", ret);
		return -EAGAIN;
	}

	_leave(" = 0");
	return 0;
}

/*
 * send a Kerberos security response
 */
static int rxkad_send_response(struct rxrpc_connection *conn,
			       struct rxrpc_host_header *hdr,
			       struct rxkad_response *resp,
			       const struct rxkad_key *s2)
{
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[3];
	size_t len;
	u32 serial;
	int ret;

	_enter("");

	msg.msg_name	= &conn->params.peer->srx.transport.sin;
	msg.msg_namelen	= sizeof(conn->params.peer->srx.transport.sin);
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	memset(&whdr, 0, sizeof(whdr));
	whdr.epoch	= htonl(hdr->epoch);
	whdr.cid	= htonl(hdr->cid);
	whdr.type	= RXRPC_PACKET_TYPE_RESPONSE;
	whdr.flags	= conn->out_clientflag;
	whdr.securityIndex = hdr->securityIndex;
	whdr.serviceId	= htons(hdr->serviceId);

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= resp;
	iov[1].iov_len	= sizeof(*resp);
	iov[2].iov_base	= (void *)s2->ticket;
	iov[2].iov_len	= s2->ticket_len;

	len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;

	serial = atomic_inc_return(&conn->serial);
	whdr.serial = htonl(serial);
	_proto("Tx RESPONSE %%%u", serial);

	ret = kernel_sendmsg(conn->params.local->socket, &msg, iov, 3, len);
	if (ret < 0) {
		_debug("sendmsg failed: %d", ret);
		return -EAGAIN;
	}

	_leave(" = 0");
	return 0;
}

/*
 * calculate the response checksum
 */
static void rxkad_calc_response_checksum(struct rxkad_response *response)
{
	u32 csum = 1000003;
	int loop;
	u8 *p = (u8 *) response;

	for (loop = sizeof(*response); loop > 0; loop--)
		csum = csum * 0x10204081 + *p++;

	response->encrypted.checksum = htonl(csum);
}

/*
 * encrypt the response packet
 */
static void rxkad_encrypt_response(struct rxrpc_connection *conn,
				   struct rxkad_response *resp,
				   const struct rxkad_key *s2)
{
	SKCIPHER_REQUEST_ON_STACK(req, conn->cipher);
	struct rxrpc_crypt iv;
	struct scatterlist sg[1];

	/* continue encrypting from where we left off */
	memcpy(&iv, s2->session_key, sizeof(iv));

	sg_init_table(sg, 1);
	sg_set_buf(sg, &resp->encrypted, sizeof(resp->encrypted));
	skcipher_request_set_tfm(req, conn->cipher);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, sizeof(resp->encrypted), iv.x);
	crypto_skcipher_encrypt(req);
	skcipher_request_zero(req);
}

/*
 * respond to a challenge packet
 */
static int rxkad_respond_to_challenge(struct rxrpc_connection *conn,
				      struct sk_buff *skb,
				      u32 *_abort_code)
{
	const struct rxrpc_key_token *token;
	struct rxkad_challenge challenge;
	struct rxkad_response resp
		__attribute__((aligned(8))); /* must be aligned for crypto */
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	u32 version, nonce, min_level, abort_code;
	int ret;

	_enter("{%d,%x}", conn->debug_id, key_serial(conn->params.key));

	if (!conn->params.key) {
		_leave(" = -EPROTO [no key]");
		return -EPROTO;
	}

	ret = key_validate(conn->params.key);
	if (ret < 0) {
		*_abort_code = RXKADEXPIRED;
		return ret;
	}

	abort_code = RXKADPACKETSHORT;
	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  &challenge, sizeof(challenge)) < 0)
		goto protocol_error;

	version = ntohl(challenge.version);
	nonce = ntohl(challenge.nonce);
	min_level = ntohl(challenge.min_level);

	_proto("Rx CHALLENGE %%%u { v=%u n=%u ml=%u }",
	       sp->hdr.serial, version, nonce, min_level);

	abort_code = RXKADINCONSISTENCY;
	if (version != RXKAD_VERSION)
		goto protocol_error;

	abort_code = RXKADLEVELFAIL;
	if (conn->params.security_level < min_level)
		goto protocol_error;

	token = conn->params.key->payload.data[0];

	/* build the response packet */
	memset(&resp, 0, sizeof(resp));

	resp.version			= htonl(RXKAD_VERSION);
	resp.encrypted.epoch		= htonl(conn->proto.epoch);
	resp.encrypted.cid		= htonl(conn->proto.cid);
	resp.encrypted.securityIndex	= htonl(conn->security_ix);
	resp.encrypted.inc_nonce	= htonl(nonce + 1);
	resp.encrypted.level		= htonl(conn->params.security_level);
	resp.kvno			= htonl(token->kad->kvno);
	resp.ticket_len			= htonl(token->kad->ticket_len);

	resp.encrypted.call_id[0] = htonl(conn->channels[0].call_counter);
	resp.encrypted.call_id[1] = htonl(conn->channels[1].call_counter);
	resp.encrypted.call_id[2] = htonl(conn->channels[2].call_counter);
	resp.encrypted.call_id[3] = htonl(conn->channels[3].call_counter);

	/* calculate the response checksum and then do the encryption */
	rxkad_calc_response_checksum(&resp);
	rxkad_encrypt_response(conn, &resp, token->kad);
	return rxkad_send_response(conn, &sp->hdr, &resp, token->kad);

protocol_error:
	*_abort_code = abort_code;
	_leave(" = -EPROTO [%d]", abort_code);
	return -EPROTO;
}

/*
 * decrypt the kerberos IV ticket in the response
 */
static int rxkad_decrypt_ticket(struct rxrpc_connection *conn,
				void *ticket, size_t ticket_len,
				struct rxrpc_crypt *_session_key,
				time_t *_expiry,
				u32 *_abort_code)
{
	struct skcipher_request *req;
	struct rxrpc_crypt iv, key;
	struct scatterlist sg[1];
	struct in_addr addr;
	unsigned int life;
	time_t issue, now;
	bool little_endian;
	int ret;
	u8 *p, *q, *name, *end;

	_enter("{%d},{%x}", conn->debug_id, key_serial(conn->server_key));

	*_expiry = 0;

	ret = key_validate(conn->server_key);
	if (ret < 0) {
		switch (ret) {
		case -EKEYEXPIRED:
			*_abort_code = RXKADEXPIRED;
			goto error;
		default:
			*_abort_code = RXKADNOAUTH;
			goto error;
		}
	}

	ASSERT(conn->server_key->payload.data[0] != NULL);
	ASSERTCMP((unsigned long) ticket & 7UL, ==, 0);

	memcpy(&iv, &conn->server_key->payload.data[2], sizeof(iv));

	req = skcipher_request_alloc(conn->server_key->payload.data[0],
				     GFP_NOFS);
	if (!req) {
		*_abort_code = RXKADNOAUTH;
		ret = -ENOMEM;
		goto error;
	}

	sg_init_one(&sg[0], ticket, ticket_len);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, ticket_len, iv.x);
	crypto_skcipher_decrypt(req);
	skcipher_request_free(req);

	p = ticket;
	end = p + ticket_len;

#define Z(size)						\
	({						\
		u8 *__str = p;				\
		q = memchr(p, 0, end - p);		\
		if (!q || q - p > (size))		\
			goto bad_ticket;		\
		for (; p < q; p++)			\
			if (!isprint(*p))		\
				goto bad_ticket;	\
		p++;					\
		__str;					\
	})

	/* extract the ticket flags */
	_debug("KIV FLAGS: %x", *p);
	little_endian = *p & 1;
	p++;

	/* extract the authentication name */
	name = Z(ANAME_SZ);
	_debug("KIV ANAME: %s", name);

	/* extract the principal's instance */
	name = Z(INST_SZ);
	_debug("KIV INST : %s", name);

	/* extract the principal's authentication domain */
	name = Z(REALM_SZ);
	_debug("KIV REALM: %s", name);

	if (end - p < 4 + 8 + 4 + 2)
		goto bad_ticket;

	/* get the IPv4 address of the entity that requested the ticket */
	memcpy(&addr, p, sizeof(addr));
	p += 4;
	_debug("KIV ADDR : %pI4", &addr);

	/* get the session key from the ticket */
	memcpy(&key, p, sizeof(key));
	p += 8;
	_debug("KIV KEY  : %08x %08x", ntohl(key.n[0]), ntohl(key.n[1]));
	memcpy(_session_key, &key, sizeof(key));

	/* get the ticket's lifetime */
	life = *p++ * 5 * 60;
	_debug("KIV LIFE : %u", life);

	/* get the issue time of the ticket */
	if (little_endian) {
		__le32 stamp;
		memcpy(&stamp, p, 4);
		issue = le32_to_cpu(stamp);
	} else {
		__be32 stamp;
		memcpy(&stamp, p, 4);
		issue = be32_to_cpu(stamp);
	}
	p += 4;
	now = get_seconds();
	_debug("KIV ISSUE: %lx [%lx]", issue, now);

	/* check the ticket is in date */
	if (issue > now) {
		*_abort_code = RXKADNOAUTH;
		ret = -EKEYREJECTED;
		goto error;
	}

	if (issue < now - life) {
		*_abort_code = RXKADEXPIRED;
		ret = -EKEYEXPIRED;
		goto error;
	}

	*_expiry = issue + life;

	/* get the service name */
	name = Z(SNAME_SZ);
	_debug("KIV SNAME: %s", name);

	/* get the service instance name */
	name = Z(INST_SZ);
	_debug("KIV SINST: %s", name);

	ret = 0;
error:
	_leave(" = %d", ret);
	return ret;

bad_ticket:
	*_abort_code = RXKADBADTICKET;
	ret = -EBADMSG;
	goto error;
}

/*
 * decrypt the response packet
 */
static void rxkad_decrypt_response(struct rxrpc_connection *conn,
				   struct rxkad_response *resp,
				   const struct rxrpc_crypt *session_key)
{
	SKCIPHER_REQUEST_ON_STACK(req, rxkad_ci);
	struct scatterlist sg[1];
	struct rxrpc_crypt iv;

	_enter(",,%08x%08x",
	       ntohl(session_key->n[0]), ntohl(session_key->n[1]));

	ASSERT(rxkad_ci != NULL);

	mutex_lock(&rxkad_ci_mutex);
	if (crypto_skcipher_setkey(rxkad_ci, session_key->x,
				   sizeof(*session_key)) < 0)
		BUG();

	memcpy(&iv, session_key, sizeof(iv));

	sg_init_table(sg, 1);
	sg_set_buf(sg, &resp->encrypted, sizeof(resp->encrypted));
	skcipher_request_set_tfm(req, rxkad_ci);
	skcipher_request_set_callback(req, 0, NULL, NULL);
	skcipher_request_set_crypt(req, sg, sg, sizeof(resp->encrypted), iv.x);
	crypto_skcipher_decrypt(req);
	skcipher_request_zero(req);

	mutex_unlock(&rxkad_ci_mutex);

	_leave("");
}

/*
 * verify a response
 */
static int rxkad_verify_response(struct rxrpc_connection *conn,
				 struct sk_buff *skb,
				 u32 *_abort_code)
{
	struct rxkad_response response
		__attribute__((aligned(8))); /* must be aligned for crypto */
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_crypt session_key;
	time_t expiry;
	void *ticket;
	u32 abort_code, version, kvno, ticket_len, level;
	__be32 csum;
	int ret, i;

	_enter("{%d,%x}", conn->debug_id, key_serial(conn->server_key));

	abort_code = RXKADPACKETSHORT;
	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  &response, sizeof(response)) < 0)
		goto protocol_error;
	if (!pskb_pull(skb, sizeof(response)))
		BUG();

	version = ntohl(response.version);
	ticket_len = ntohl(response.ticket_len);
	kvno = ntohl(response.kvno);
	_proto("Rx RESPONSE %%%u { v=%u kv=%u tl=%u }",
	       sp->hdr.serial, version, kvno, ticket_len);

	abort_code = RXKADINCONSISTENCY;
	if (version != RXKAD_VERSION)
		goto protocol_error;

	abort_code = RXKADTICKETLEN;
	if (ticket_len < 4 || ticket_len > MAXKRB5TICKETLEN)
		goto protocol_error;

	abort_code = RXKADUNKNOWNKEY;
	if (kvno >= RXKAD_TKT_TYPE_KERBEROS_V5)
		goto protocol_error;

	/* extract the kerberos ticket and decrypt and decode it */
	ticket = kmalloc(ticket_len, GFP_NOFS);
	if (!ticket)
		return -ENOMEM;

	abort_code = RXKADPACKETSHORT;
	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  ticket, ticket_len) < 0)
		goto protocol_error_free;

	ret = rxkad_decrypt_ticket(conn, ticket, ticket_len, &session_key,
				   &expiry, &abort_code);
	if (ret < 0) {
		*_abort_code = abort_code;
		kfree(ticket);
		return ret;
	}

	/* use the session key from inside the ticket to decrypt the
	 * response */
	rxkad_decrypt_response(conn, &response, &session_key);

	abort_code = RXKADSEALEDINCON;
	if (ntohl(response.encrypted.epoch) != conn->proto.epoch)
		goto protocol_error_free;
	if (ntohl(response.encrypted.cid) != conn->proto.cid)
		goto protocol_error_free;
	if (ntohl(response.encrypted.securityIndex) != conn->security_ix)
		goto protocol_error_free;
	csum = response.encrypted.checksum;
	response.encrypted.checksum = 0;
	rxkad_calc_response_checksum(&response);
	if (response.encrypted.checksum != csum)
		goto protocol_error_free;

	spin_lock(&conn->channel_lock);
	for (i = 0; i < RXRPC_MAXCALLS; i++) {
		struct rxrpc_call *call;
		u32 call_id = ntohl(response.encrypted.call_id[i]);

		if (call_id > INT_MAX)
			goto protocol_error_unlock;

		if (call_id < conn->channels[i].call_counter)
			goto protocol_error_unlock;
		if (call_id > conn->channels[i].call_counter) {
			call = rcu_dereference_protected(
				conn->channels[i].call,
				lockdep_is_held(&conn->channel_lock));
			if (call && call->state < RXRPC_CALL_COMPLETE)
				goto protocol_error_unlock;
			conn->channels[i].call_counter = call_id;
		}
	}
	spin_unlock(&conn->channel_lock);

	abort_code = RXKADOUTOFSEQUENCE;
	if (ntohl(response.encrypted.inc_nonce) != conn->security_nonce + 1)
		goto protocol_error_free;

	abort_code = RXKADLEVELFAIL;
	level = ntohl(response.encrypted.level);
	if (level > RXRPC_SECURITY_ENCRYPT)
		goto protocol_error_free;
	conn->params.security_level = level;

	/* create a key to hold the security data and expiration time - after
	 * this the connection security can be handled in exactly the same way
	 * as for a client connection */
	ret = rxrpc_get_server_data_key(conn, &session_key, expiry, kvno);
	if (ret < 0) {
		kfree(ticket);
		return ret;
	}

	kfree(ticket);
	_leave(" = 0");
	return 0;

protocol_error_unlock:
	spin_unlock(&conn->channel_lock);
protocol_error_free:
	kfree(ticket);
protocol_error:
	*_abort_code = abort_code;
	_leave(" = -EPROTO [%d]", abort_code);
	return -EPROTO;
}

/*
 * clear the connection security
 */
static void rxkad_clear(struct rxrpc_connection *conn)
{
	_enter("");

	if (conn->cipher)
		crypto_free_skcipher(conn->cipher);
}

/*
 * Initialise the rxkad security service.
 */
static int rxkad_init(void)
{
	/* pin the cipher we need so that the crypto layer doesn't invoke
	 * keventd to go get it */
	rxkad_ci = crypto_alloc_skcipher("pcbc(fcrypt)", 0, CRYPTO_ALG_ASYNC);
	return PTR_ERR_OR_ZERO(rxkad_ci);
}

/*
 * Clean up the rxkad security service.
 */
static void rxkad_exit(void)
{
	if (rxkad_ci)
		crypto_free_skcipher(rxkad_ci);
}

/*
 * RxRPC Kerberos-based security
 */
const struct rxrpc_security rxkad = {
	.name				= "rxkad",
	.security_index			= RXRPC_SECURITY_RXKAD,
	.init				= rxkad_init,
	.exit				= rxkad_exit,
	.init_connection_security	= rxkad_init_connection_security,
	.prime_packet_security		= rxkad_prime_packet_security,
	.secure_packet			= rxkad_secure_packet,
	.verify_packet			= rxkad_verify_packet,
	.locate_data			= rxkad_locate_data,
	.issue_challenge		= rxkad_issue_challenge,
	.respond_to_challenge		= rxkad_respond_to_challenge,
	.verify_response		= rxkad_verify_response,
	.clear				= rxkad_clear,
};
