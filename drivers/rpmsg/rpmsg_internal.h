/*
 * remote processor messaging bus internals
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RPMSG_INTERNAL_H__
#define __RPMSG_INTERNAL_H__

#include <linux/rpmsg.h>

#define to_rpmsg_device(d) container_of(d, struct rpmsg_device, dev)
#define to_rpmsg_driver(d) container_of(d, struct rpmsg_driver, drv)

/**
 * struct rpmsg_device_ops - indirection table for the rpmsg_device operations
 * @create_ept:		create backend-specific endpoint, requried
 * @announce_create:	announce presence of new channel, optional
 * @announce_destroy:	announce destruction of channel, optional
 *
 * Indirection table for the operations that a rpmsg backend should implement.
 * @announce_create and @announce_destroy are optional as the backend might
 * advertise new channels implicitly by creating the endpoints.
 */
struct rpmsg_device_ops {
	struct rpmsg_endpoint *(*create_ept)(struct rpmsg_device *rpdev,
					    rpmsg_rx_cb_t cb, void *priv,
					    struct rpmsg_channel_info chinfo);

	int (*announce_create)(struct rpmsg_device *ept);
	int (*announce_destroy)(struct rpmsg_device *ept);
};

/**
 * struct rpmsg_endpoint_ops - indirection table for rpmsg_endpoint operations
 * @destroy_ept:	destroy the given endpoint, required
 * @send:		see @rpmsg_send(), required
 * @sendto:		see @rpmsg_sendto(), optional
 * @send_offchannel:	see @rpmsg_send_offchannel(), optional
 * @trysend:		see @rpmsg_trysend(), required
 * @trysendto:		see @rpmsg_trysendto(), optional
 * @trysend_offchannel:	see @rpmsg_trysend_offchannel(), optional
 *
 * Indirection table for the operations that a rpmsg backend should implement.
 * In addition to @destroy_ept, the backend must at least implement @send and
 * @trysend, while the variants sending data off-channel are optional.
 */
struct rpmsg_endpoint_ops {
	void (*destroy_ept)(struct rpmsg_endpoint *ept);

	int (*send)(struct rpmsg_endpoint *ept, void *data, int len);
	int (*sendto)(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
	int (*send_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst,
				  void *data, int len);

	int (*trysend)(struct rpmsg_endpoint *ept, void *data, int len);
	int (*trysendto)(struct rpmsg_endpoint *ept, void *data, int len, u32 dst);
	int (*trysend_offchannel)(struct rpmsg_endpoint *ept, u32 src, u32 dst,
			     void *data, int len);
};

int rpmsg_register_device(struct rpmsg_device *rpdev);
int rpmsg_unregister_device(struct device *parent,
			    struct rpmsg_channel_info *chinfo);

struct device *rpmsg_find_device(struct device *parent,
				 struct rpmsg_channel_info *chinfo);

#endif
