/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RXE_H
#define RXE_H

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/crc32.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>

#include "rxe_net.h"
#include "rxe_opcode.h"
#include "rxe_hdr.h"
#include "rxe_param.h"
#include "rxe_verbs.h"

#define RXE_UVERBS_ABI_VERSION		(1)

#define IB_PHYS_STATE_LINK_UP		(5)
#define IB_PHYS_STATE_LINK_DOWN		(3)

#define RXE_ROCE_V2_SPORT		(0xc000)

int rxe_set_mtu(struct rxe_dev *rxe, unsigned int dev_mtu);

int rxe_add(struct rxe_dev *rxe, unsigned int mtu);
void rxe_remove(struct rxe_dev *rxe);
void rxe_remove_all(void);

int rxe_rcv(struct sk_buff *skb);

void rxe_dev_put(struct rxe_dev *rxe);
struct rxe_dev *net_to_rxe(struct net_device *ndev);
struct rxe_dev *get_rxe_by_name(const char* name);

void rxe_port_up(struct rxe_dev *rxe);
void rxe_port_down(struct rxe_dev *rxe);

#endif /* RXE_H */
