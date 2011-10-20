/*
 * OMX offloading remote processor driver
 *
 * Copyright(c) 2011 Texas Instruments. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name Texas Instruments nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
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

#ifndef RPMSG_OMX_H
#define RPMSG_OMX_H

#include <linux/ioctl.h>

#define OMX_IOC_MAGIC	'X'

#define OMX_IOCCONNECT	_IOW(OMX_IOC_MAGIC, 1, char *)

#define OMX_IOC_MAXNR	(1)

#ifdef __KERNEL__

/**
 * enum omx_msg_types - various message types currently supported
 *
 * @OMX_CONN_REQ: a connection request message type. the message should carry
 * the name of the OMX service which we try to connect to. An instance of
 * that service will be created remotely, and its address will be sent as
 * a reply.
 *
 * @OMX_CONN_RSP: a response to a connection request. the message will carry
 * an error code (success/failure), and if connection established successfully,
 * the addr field will carry the address of the newly created OMX instance.
 *
 * @OMX_DISCONNECT: disconnect remote OMX instance. this message tells
 * remote processor to release the resources coupled with this connection
 *
 * @OMX_RAW_MSG: a message that should be propagated as-is to the user.
 * this would immediately enable user space development to start.
 * as we progress, most likely this message won't be needed anymore.
 */
enum omx_msg_types {
	OMX_CONN_REQ = 0,
	OMX_CONN_RSP = 1,
	OMX_DISCONNECT = 4,
	OMX_RAW_MSG = 5,
	/* todo: do we need a disconnect response ? ION refcounts should allow
	 * asynchronous release of relevant buffers */
};

/**
 * enum omx_error_codes - various error codes that will be used
 *
 * @OMX_SUCCESS: success
 *
 * @OMX_NOTSUPP: not supported
 *
 * @OMX_NOMEM: remote processor is out of memory
 */
enum omx_error_codes {
	OMX_SUCCESS = 0,
	OMX_NOTSUPP = 1,
	OMX_NOMEM = 2,
};

/* keep documenting... */
enum omx_state {
	OMX_UNCONNECTED,
	OMX_CONNECTED,
	OMX_FAIL,
};

/**
 * struct omx_msg_hdr - common header for all OMX messages
 * @type:	type of message, see enum omx_msg_types
 * @flags:	currently unused, should be zero
 * @len:	length of msg payload (in bytes)
 * @data:	the msg payload (depends on the message type)
 *
 * All OMX messages will start with this common header (which will begin
 * right after the standard rpmsg header ends).
 */
struct omx_msg_hdr {
	u32 type;
	u32 flags;
	u32 len;
	char data[0];
} __packed;

struct omx_conn_rsp {
	u32 status;
	u32 addr;
} __packed;

struct omx_disc_req {
	u32 addr;
} __packed;


#endif /* __KERNEL__ */

/* temporarily exposed to user space too */
struct omx_conn_req {
	char name[48];
} __packed;

#endif /* RPMSG_OMX_H */
