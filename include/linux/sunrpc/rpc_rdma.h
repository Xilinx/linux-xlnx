/*
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _LINUX_SUNRPC_RPC_RDMA_H
#define _LINUX_SUNRPC_RPC_RDMA_H

#include <linux/types.h>
#include <linux/bitops.h>

#define RPCRDMA_VERSION		1
#define rpcrdma_version		cpu_to_be32(RPCRDMA_VERSION)

enum {
	RPCRDMA_V1_DEF_INLINE_SIZE	= 1024,
};

struct rpcrdma_segment {
	__be32 rs_handle;	/* Registered memory handle */
	__be32 rs_length;	/* Length of the chunk in bytes */
	__be64 rs_offset;	/* Chunk virtual address or offset */
};

/*
 * read chunk(s), encoded as a linked list.
 */
struct rpcrdma_read_chunk {
	__be32 rc_discrim;	/* 1 indicates presence */
	__be32 rc_position;	/* Position in XDR stream */
	struct rpcrdma_segment rc_target;
};

/*
 * write chunk, and reply chunk.
 */
struct rpcrdma_write_chunk {
	struct rpcrdma_segment wc_target;
};

/*
 * write chunk(s), encoded as a counted array.
 */
struct rpcrdma_write_array {
	__be32 wc_discrim;	/* 1 indicates presence */
	__be32 wc_nchunks;	/* Array count */
	struct rpcrdma_write_chunk wc_array[0];
};

struct rpcrdma_msg {
	__be32 rm_xid;	/* Mirrors the RPC header xid */
	__be32 rm_vers;	/* Version of this protocol */
	__be32 rm_credit;	/* Buffers requested/granted */
	__be32 rm_type;	/* Type of message (enum rpcrdma_proc) */
	union {

		struct {			/* no chunks */
			__be32 rm_empty[3];	/* 3 empty chunk lists */
		} rm_nochunks;

		struct {			/* no chunks and padded */
			__be32 rm_align;	/* Padding alignment */
			__be32 rm_thresh;	/* Padding threshold */
			__be32 rm_pempty[3];	/* 3 empty chunk lists */
		} rm_padded;

		struct {
			__be32 rm_err;
			__be32 rm_vers_low;
			__be32 rm_vers_high;
		} rm_error;

		__be32 rm_chunks[0];	/* read, write and reply chunks */

	} rm_body;
};

/*
 * Smallest RPC/RDMA header: rm_xid through rm_type, then rm_nochunks
 */
#define RPCRDMA_HDRLEN_MIN	(sizeof(__be32) * 7)
#define RPCRDMA_HDRLEN_ERR	(sizeof(__be32) * 5)

enum rpcrdma_errcode {
	ERR_VERS = 1,
	ERR_CHUNK = 2
};

enum rpcrdma_proc {
	RDMA_MSG = 0,		/* An RPC call or reply msg */
	RDMA_NOMSG = 1,		/* An RPC call or reply msg - separate body */
	RDMA_MSGP = 2,		/* An RPC call or reply msg with padding */
	RDMA_DONE = 3,		/* Client signals reply completion */
	RDMA_ERROR = 4		/* An RPC RDMA encoding error */
};

#define rdma_msg	cpu_to_be32(RDMA_MSG)
#define rdma_nomsg	cpu_to_be32(RDMA_NOMSG)
#define rdma_msgp	cpu_to_be32(RDMA_MSGP)
#define rdma_done	cpu_to_be32(RDMA_DONE)
#define rdma_error	cpu_to_be32(RDMA_ERROR)

/*
 * Private extension to RPC-over-RDMA Version One.
 * Message passed during RDMA-CM connection set-up.
 *
 * Add new fields at the end, and don't permute existing
 * fields.
 */
struct rpcrdma_connect_private {
	__be32			cp_magic;
	u8			cp_version;
	u8			cp_flags;
	u8			cp_send_size;
	u8			cp_recv_size;
} __packed;

#define rpcrdma_cmp_magic	__cpu_to_be32(0xf6ab0e18)

enum {
	RPCRDMA_CMP_VERSION		= 1,
	RPCRDMA_CMP_F_SND_W_INV_OK	= BIT(0),
};

static inline u8
rpcrdma_encode_buffer_size(unsigned int size)
{
	return (size >> 10) - 1;
}

static inline unsigned int
rpcrdma_decode_buffer_size(u8 val)
{
	return ((unsigned int)val + 1) << 10;
}

#endif				/* _LINUX_SUNRPC_RPC_RDMA_H */
