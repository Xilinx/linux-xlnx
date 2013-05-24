/*
 * Remote processor messaging sockets
 *
 * Copyright (C) 2011 Texas Instruments, Inc
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

#ifndef __NET_RPMSG_H
#define __NET_RPMSG_H

#include <linux/types.h>
#include <linux/socket.h>

/* user space needs this */
#ifndef AF_RPMSG
#define AF_RPMSG	41
#define PF_RPMSG	AF_RPMSG
#endif

/* Connection and socket states */
enum {
	RPMSG_CONNECTED = 1, /* wait_for_packet() wants this... */
	RPMSG_OPEN,
	RPMSG_LISTENING,
	RPMSG_CLOSED,
};

struct sockaddr_rpmsg {
	sa_family_t family;
	__u32 vproc_id;
	__u32 addr;
};

#define RPMSG_LOCALHOST ((__u32) ~0UL)

#ifdef __KERNEL__

#include <net/sock.h>
#include <linux/rpmsg.h>

struct rpmsg_socket {
	struct sock sk;
	struct rpmsg_channel *rpdev;
	bool unregister_rpdev;
};

#endif /* __KERNEL__ */
#endif /* __NET_RPMSG_H */
