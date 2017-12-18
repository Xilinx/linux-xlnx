/*
 * System Trace Module (STM) userspace interfaces
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#ifndef _UAPI_LINUX_STM_H
#define _UAPI_LINUX_STM_H

#include <linux/types.h>

/**
 * struct stp_policy_id - identification for the STP policy
 * @size:	size of the structure including real id[] length
 * @master:	assigned master
 * @channel:	first assigned channel
 * @width:	number of requested channels
 * @id:		identification string
 *
 * User must calculate the total size of the structure and put it into
 * @size field, fill out the @id and desired @width. In return, kernel
 * fills out @master, @channel and @width.
 */
struct stp_policy_id {
	__u32		size;
	__u16		master;
	__u16		channel;
	__u16		width;
	/* padding */
	__u16		__reserved_0;
	__u32		__reserved_1;
	char		id[0];
};

#define STP_POLICY_ID_SET	_IOWR('%', 0, struct stp_policy_id)
#define STP_POLICY_ID_GET	_IOR('%', 1, struct stp_policy_id)
#define STP_SET_OPTIONS		_IOW('%', 2, __u64)

#endif /* _UAPI_LINUX_STM_H */
