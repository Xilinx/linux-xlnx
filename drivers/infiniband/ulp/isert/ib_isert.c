/*******************************************************************************
 * This file contains iSCSI extentions for RDMA (iSER) Verbs
 *
 * (c) Copyright 2013 Datera, Inc.
 *
 * Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ****************************************************************************/

#include <linux/string.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/llist.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/iscsi/iscsi_transport.h>

#include "isert_proto.h"
#include "ib_isert.h"

#define	ISERT_MAX_CONN		8
#define ISER_MAX_RX_CQ_LEN	(ISERT_QP_MAX_RECV_DTOS * ISERT_MAX_CONN)
#define ISER_MAX_TX_CQ_LEN	(ISERT_QP_MAX_REQ_DTOS  * ISERT_MAX_CONN)

static DEFINE_MUTEX(device_list_mutex);
static LIST_HEAD(device_list);
static struct workqueue_struct *isert_rx_wq;
static struct workqueue_struct *isert_comp_wq;

static void
isert_unmap_cmd(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn);
static int
isert_map_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr);
static void
isert_unreg_rdma_frwr(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn);
static int
isert_reg_rdma_frwr(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		    struct isert_rdma_wr *wr);

static void
isert_qp_event_callback(struct ib_event *e, void *context)
{
	struct isert_conn *isert_conn = (struct isert_conn *)context;

	pr_err("isert_qp_event_callback event: %d\n", e->event);
	switch (e->event) {
	case IB_EVENT_COMM_EST:
		rdma_notify(isert_conn->conn_cm_id, IB_EVENT_COMM_EST);
		break;
	case IB_EVENT_QP_LAST_WQE_REACHED:
		pr_warn("Reached TX IB_EVENT_QP_LAST_WQE_REACHED:\n");
		break;
	default:
		break;
	}
}

static int
isert_query_device(struct ib_device *ib_dev, struct ib_device_attr *devattr)
{
	int ret;

	ret = ib_query_device(ib_dev, devattr);
	if (ret) {
		pr_err("ib_query_device() failed: %d\n", ret);
		return ret;
	}
	pr_debug("devattr->max_sge: %d\n", devattr->max_sge);
	pr_debug("devattr->max_sge_rd: %d\n", devattr->max_sge_rd);

	return 0;
}

static int
isert_conn_setup_qp(struct isert_conn *isert_conn, struct rdma_cm_id *cma_id)
{
	struct isert_device *device = isert_conn->conn_device;
	struct ib_qp_init_attr attr;
	int ret, index, min_index = 0;

	mutex_lock(&device_list_mutex);
	for (index = 0; index < device->cqs_used; index++)
		if (device->cq_active_qps[index] <
		    device->cq_active_qps[min_index])
			min_index = index;
	device->cq_active_qps[min_index]++;
	pr_debug("isert_conn_setup_qp: Using min_index: %d\n", min_index);
	mutex_unlock(&device_list_mutex);

	memset(&attr, 0, sizeof(struct ib_qp_init_attr));
	attr.event_handler = isert_qp_event_callback;
	attr.qp_context = isert_conn;
	attr.send_cq = device->dev_tx_cq[min_index];
	attr.recv_cq = device->dev_rx_cq[min_index];
	attr.cap.max_send_wr = ISERT_QP_MAX_REQ_DTOS;
	attr.cap.max_recv_wr = ISERT_QP_MAX_RECV_DTOS;
	/*
	 * FIXME: Use devattr.max_sge - 2 for max_send_sge as
	 * work-around for RDMA_READ..
	 */
	attr.cap.max_send_sge = device->dev_attr.max_sge - 2;
	isert_conn->max_sge = attr.cap.max_send_sge;

	attr.cap.max_recv_sge = 1;
	attr.sq_sig_type = IB_SIGNAL_REQ_WR;
	attr.qp_type = IB_QPT_RC;

	pr_debug("isert_conn_setup_qp cma_id->device: %p\n",
		 cma_id->device);
	pr_debug("isert_conn_setup_qp conn_pd->device: %p\n",
		 isert_conn->conn_pd->device);

	ret = rdma_create_qp(cma_id, isert_conn->conn_pd, &attr);
	if (ret) {
		pr_err("rdma_create_qp failed for cma_id %d\n", ret);
		return ret;
	}
	isert_conn->conn_qp = cma_id->qp;
	pr_debug("rdma_create_qp() returned success >>>>>>>>>>>>>>>>>>>>>>>>>.\n");

	return 0;
}

static void
isert_cq_event_callback(struct ib_event *e, void *context)
{
	pr_debug("isert_cq_event_callback event: %d\n", e->event);
}

static int
isert_alloc_rx_descriptors(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_rx_desc *rx_desc;
	struct ib_sge *rx_sg;
	u64 dma_addr;
	int i, j;

	isert_conn->conn_rx_descs = kzalloc(ISERT_QP_MAX_RECV_DTOS *
				sizeof(struct iser_rx_desc), GFP_KERNEL);
	if (!isert_conn->conn_rx_descs)
		goto fail;

	rx_desc = isert_conn->conn_rx_descs;

	for (i = 0; i < ISERT_QP_MAX_RECV_DTOS; i++, rx_desc++)  {
		dma_addr = ib_dma_map_single(ib_dev, (void *)rx_desc,
					ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
		if (ib_dma_mapping_error(ib_dev, dma_addr))
			goto dma_map_fail;

		rx_desc->dma_addr = dma_addr;

		rx_sg = &rx_desc->rx_sg;
		rx_sg->addr = rx_desc->dma_addr;
		rx_sg->length = ISER_RX_PAYLOAD_SIZE;
		rx_sg->lkey = isert_conn->conn_mr->lkey;
	}

	isert_conn->conn_rx_desc_head = 0;
	return 0;

dma_map_fail:
	rx_desc = isert_conn->conn_rx_descs;
	for (j = 0; j < i; j++, rx_desc++) {
		ib_dma_unmap_single(ib_dev, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	}
	kfree(isert_conn->conn_rx_descs);
	isert_conn->conn_rx_descs = NULL;
fail:
	return -ENOMEM;
}

static void
isert_free_rx_descriptors(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_rx_desc *rx_desc;
	int i;

	if (!isert_conn->conn_rx_descs)
		return;

	rx_desc = isert_conn->conn_rx_descs;
	for (i = 0; i < ISERT_QP_MAX_RECV_DTOS; i++, rx_desc++)  {
		ib_dma_unmap_single(ib_dev, rx_desc->dma_addr,
				    ISER_RX_PAYLOAD_SIZE, DMA_FROM_DEVICE);
	}

	kfree(isert_conn->conn_rx_descs);
	isert_conn->conn_rx_descs = NULL;
}

static void isert_cq_tx_work(struct work_struct *);
static void isert_cq_tx_callback(struct ib_cq *, void *);
static void isert_cq_rx_work(struct work_struct *);
static void isert_cq_rx_callback(struct ib_cq *, void *);

static int
isert_create_device_ib_res(struct isert_device *device)
{
	struct ib_device *ib_dev = device->ib_device;
	struct isert_cq_desc *cq_desc;
	struct ib_device_attr *dev_attr;
	int ret = 0, i, j;

	dev_attr = &device->dev_attr;
	ret = isert_query_device(ib_dev, dev_attr);
	if (ret)
		return ret;

	/* asign function handlers */
	if (dev_attr->device_cap_flags & IB_DEVICE_MEM_MGT_EXTENSIONS) {
		device->use_frwr = 1;
		device->reg_rdma_mem = isert_reg_rdma_frwr;
		device->unreg_rdma_mem = isert_unreg_rdma_frwr;
	} else {
		device->use_frwr = 0;
		device->reg_rdma_mem = isert_map_rdma;
		device->unreg_rdma_mem = isert_unmap_cmd;
	}

	device->cqs_used = min_t(int, num_online_cpus(),
				 device->ib_device->num_comp_vectors);
	device->cqs_used = min(ISERT_MAX_CQ, device->cqs_used);
	pr_debug("Using %d CQs, device %s supports %d vectors support FRWR %d\n",
		 device->cqs_used, device->ib_device->name,
		 device->ib_device->num_comp_vectors, device->use_frwr);
	device->cq_desc = kzalloc(sizeof(struct isert_cq_desc) *
				device->cqs_used, GFP_KERNEL);
	if (!device->cq_desc) {
		pr_err("Unable to allocate device->cq_desc\n");
		return -ENOMEM;
	}
	cq_desc = device->cq_desc;

	device->dev_pd = ib_alloc_pd(ib_dev);
	if (IS_ERR(device->dev_pd)) {
		ret = PTR_ERR(device->dev_pd);
		pr_err("ib_alloc_pd failed for dev_pd: %d\n", ret);
		goto out_cq_desc;
	}

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc[i].device = device;
		cq_desc[i].cq_index = i;

		INIT_WORK(&cq_desc[i].cq_rx_work, isert_cq_rx_work);
		device->dev_rx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_rx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						ISER_MAX_RX_CQ_LEN, i);
		if (IS_ERR(device->dev_rx_cq[i])) {
			ret = PTR_ERR(device->dev_rx_cq[i]);
			device->dev_rx_cq[i] = NULL;
			goto out_cq;
		}

		INIT_WORK(&cq_desc[i].cq_tx_work, isert_cq_tx_work);
		device->dev_tx_cq[i] = ib_create_cq(device->ib_device,
						isert_cq_tx_callback,
						isert_cq_event_callback,
						(void *)&cq_desc[i],
						ISER_MAX_TX_CQ_LEN, i);
		if (IS_ERR(device->dev_tx_cq[i])) {
			ret = PTR_ERR(device->dev_tx_cq[i]);
			device->dev_tx_cq[i] = NULL;
			goto out_cq;
		}

		ret = ib_req_notify_cq(device->dev_rx_cq[i], IB_CQ_NEXT_COMP);
		if (ret)
			goto out_cq;

		ret = ib_req_notify_cq(device->dev_tx_cq[i], IB_CQ_NEXT_COMP);
		if (ret)
			goto out_cq;
	}

	device->dev_mr = ib_get_dma_mr(device->dev_pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(device->dev_mr)) {
		ret = PTR_ERR(device->dev_mr);
		pr_err("ib_get_dma_mr failed for dev_mr: %d\n", ret);
		goto out_cq;
	}

	return 0;

out_cq:
	for (j = 0; j < i; j++) {
		cq_desc = &device->cq_desc[j];

		if (device->dev_rx_cq[j]) {
			cancel_work_sync(&cq_desc->cq_rx_work);
			ib_destroy_cq(device->dev_rx_cq[j]);
		}
		if (device->dev_tx_cq[j]) {
			cancel_work_sync(&cq_desc->cq_tx_work);
			ib_destroy_cq(device->dev_tx_cq[j]);
		}
	}
	ib_dealloc_pd(device->dev_pd);

out_cq_desc:
	kfree(device->cq_desc);

	return ret;
}

static void
isert_free_device_ib_res(struct isert_device *device)
{
	struct isert_cq_desc *cq_desc;
	int i;

	for (i = 0; i < device->cqs_used; i++) {
		cq_desc = &device->cq_desc[i];

		cancel_work_sync(&cq_desc->cq_rx_work);
		cancel_work_sync(&cq_desc->cq_tx_work);
		ib_destroy_cq(device->dev_rx_cq[i]);
		ib_destroy_cq(device->dev_tx_cq[i]);
		device->dev_rx_cq[i] = NULL;
		device->dev_tx_cq[i] = NULL;
	}

	ib_dereg_mr(device->dev_mr);
	ib_dealloc_pd(device->dev_pd);
	kfree(device->cq_desc);
}

static void
isert_device_try_release(struct isert_device *device)
{
	mutex_lock(&device_list_mutex);
	device->refcount--;
	if (!device->refcount) {
		isert_free_device_ib_res(device);
		list_del(&device->dev_node);
		kfree(device);
	}
	mutex_unlock(&device_list_mutex);
}

static struct isert_device *
isert_device_find_by_ib_dev(struct rdma_cm_id *cma_id)
{
	struct isert_device *device;
	int ret;

	mutex_lock(&device_list_mutex);
	list_for_each_entry(device, &device_list, dev_node) {
		if (device->ib_device->node_guid == cma_id->device->node_guid) {
			device->refcount++;
			mutex_unlock(&device_list_mutex);
			return device;
		}
	}

	device = kzalloc(sizeof(struct isert_device), GFP_KERNEL);
	if (!device) {
		mutex_unlock(&device_list_mutex);
		return ERR_PTR(-ENOMEM);
	}

	INIT_LIST_HEAD(&device->dev_node);

	device->ib_device = cma_id->device;
	ret = isert_create_device_ib_res(device);
	if (ret) {
		kfree(device);
		mutex_unlock(&device_list_mutex);
		return ERR_PTR(ret);
	}

	device->refcount++;
	list_add_tail(&device->dev_node, &device_list);
	mutex_unlock(&device_list_mutex);

	return device;
}

static void
isert_conn_free_frwr_pool(struct isert_conn *isert_conn)
{
	struct fast_reg_descriptor *fr_desc, *tmp;
	int i = 0;

	if (list_empty(&isert_conn->conn_frwr_pool))
		return;

	pr_debug("Freeing conn %p frwr pool", isert_conn);

	list_for_each_entry_safe(fr_desc, tmp,
				 &isert_conn->conn_frwr_pool, list) {
		list_del(&fr_desc->list);
		ib_free_fast_reg_page_list(fr_desc->data_frpl);
		ib_dereg_mr(fr_desc->data_mr);
		kfree(fr_desc);
		++i;
	}

	if (i < isert_conn->conn_frwr_pool_size)
		pr_warn("Pool still has %d regions registered\n",
			isert_conn->conn_frwr_pool_size - i);
}

static int
isert_conn_create_frwr_pool(struct isert_conn *isert_conn)
{
	struct fast_reg_descriptor *fr_desc;
	struct isert_device *device = isert_conn->conn_device;
	int i, ret;

	INIT_LIST_HEAD(&isert_conn->conn_frwr_pool);
	isert_conn->conn_frwr_pool_size = 0;
	for (i = 0; i < ISCSI_DEF_XMIT_CMDS_MAX; i++) {
		fr_desc = kzalloc(sizeof(*fr_desc), GFP_KERNEL);
		if (!fr_desc) {
			pr_err("Failed to allocate fast_reg descriptor\n");
			ret = -ENOMEM;
			goto err;
		}

		fr_desc->data_frpl =
			ib_alloc_fast_reg_page_list(device->ib_device,
						    ISCSI_ISER_SG_TABLESIZE);
		if (IS_ERR(fr_desc->data_frpl)) {
			pr_err("Failed to allocate fr_pg_list err=%ld\n",
			       PTR_ERR(fr_desc->data_frpl));
			ret = PTR_ERR(fr_desc->data_frpl);
			goto err;
		}

		fr_desc->data_mr = ib_alloc_fast_reg_mr(device->dev_pd,
					ISCSI_ISER_SG_TABLESIZE);
		if (IS_ERR(fr_desc->data_mr)) {
			pr_err("Failed to allocate frmr err=%ld\n",
			       PTR_ERR(fr_desc->data_mr));
			ret = PTR_ERR(fr_desc->data_mr);
			ib_free_fast_reg_page_list(fr_desc->data_frpl);
			goto err;
		}
		pr_debug("Create fr_desc %p page_list %p\n",
			 fr_desc, fr_desc->data_frpl->page_list);

		fr_desc->valid = true;
		list_add_tail(&fr_desc->list, &isert_conn->conn_frwr_pool);
		isert_conn->conn_frwr_pool_size++;
	}

	pr_debug("Creating conn %p frwr pool size=%d",
		 isert_conn, isert_conn->conn_frwr_pool_size);

	return 0;

err:
	isert_conn_free_frwr_pool(isert_conn);
	return ret;
}

static int
isert_connect_request(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	struct iscsi_np *np = cma_id->context;
	struct isert_np *isert_np = np->np_context;
	struct isert_conn *isert_conn;
	struct isert_device *device;
	struct ib_device *ib_dev = cma_id->device;
	int ret = 0;

	pr_debug("Entering isert_connect_request cma_id: %p, context: %p\n",
		 cma_id, cma_id->context);

	isert_conn = kzalloc(sizeof(struct isert_conn), GFP_KERNEL);
	if (!isert_conn) {
		pr_err("Unable to allocate isert_conn\n");
		return -ENOMEM;
	}
	isert_conn->state = ISER_CONN_INIT;
	INIT_LIST_HEAD(&isert_conn->conn_accept_node);
	init_completion(&isert_conn->conn_login_comp);
	init_waitqueue_head(&isert_conn->conn_wait);
	init_waitqueue_head(&isert_conn->conn_wait_comp_err);
	kref_init(&isert_conn->conn_kref);
	kref_get(&isert_conn->conn_kref);
	mutex_init(&isert_conn->conn_mutex);
	mutex_init(&isert_conn->conn_comp_mutex);
	spin_lock_init(&isert_conn->conn_lock);

	cma_id->context = isert_conn;
	isert_conn->conn_cm_id = cma_id;
	isert_conn->responder_resources = event->param.conn.responder_resources;
	isert_conn->initiator_depth = event->param.conn.initiator_depth;
	pr_debug("Using responder_resources: %u initiator_depth: %u\n",
		 isert_conn->responder_resources, isert_conn->initiator_depth);

	isert_conn->login_buf = kzalloc(ISCSI_DEF_MAX_RECV_SEG_LEN +
					ISER_RX_LOGIN_SIZE, GFP_KERNEL);
	if (!isert_conn->login_buf) {
		pr_err("Unable to allocate isert_conn->login_buf\n");
		ret = -ENOMEM;
		goto out;
	}

	isert_conn->login_req_buf = isert_conn->login_buf;
	isert_conn->login_rsp_buf = isert_conn->login_buf +
				    ISCSI_DEF_MAX_RECV_SEG_LEN;
	pr_debug("Set login_buf: %p login_req_buf: %p login_rsp_buf: %p\n",
		 isert_conn->login_buf, isert_conn->login_req_buf,
		 isert_conn->login_rsp_buf);

	isert_conn->login_req_dma = ib_dma_map_single(ib_dev,
				(void *)isert_conn->login_req_buf,
				ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_FROM_DEVICE);

	ret = ib_dma_mapping_error(ib_dev, isert_conn->login_req_dma);
	if (ret) {
		pr_err("ib_dma_mapping_error failed for login_req_dma: %d\n",
		       ret);
		isert_conn->login_req_dma = 0;
		goto out_login_buf;
	}

	isert_conn->login_rsp_dma = ib_dma_map_single(ib_dev,
					(void *)isert_conn->login_rsp_buf,
					ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);

	ret = ib_dma_mapping_error(ib_dev, isert_conn->login_rsp_dma);
	if (ret) {
		pr_err("ib_dma_mapping_error failed for login_rsp_dma: %d\n",
		       ret);
		isert_conn->login_rsp_dma = 0;
		goto out_req_dma_map;
	}

	device = isert_device_find_by_ib_dev(cma_id);
	if (IS_ERR(device)) {
		ret = PTR_ERR(device);
		goto out_rsp_dma_map;
	}

	isert_conn->conn_device = device;
	isert_conn->conn_pd = device->dev_pd;
	isert_conn->conn_mr = device->dev_mr;

	if (device->use_frwr) {
		ret = isert_conn_create_frwr_pool(isert_conn);
		if (ret) {
			pr_err("Conn: %p failed to create frwr_pool\n", isert_conn);
			goto out_frwr;
		}
	}

	ret = isert_conn_setup_qp(isert_conn, cma_id);
	if (ret)
		goto out_conn_dev;

	mutex_lock(&isert_np->np_accept_mutex);
	list_add_tail(&isert_np->np_accept_list, &isert_conn->conn_accept_node);
	mutex_unlock(&isert_np->np_accept_mutex);

	pr_debug("isert_connect_request() waking up np_accept_wq: %p\n", np);
	wake_up(&isert_np->np_accept_wq);
	return 0;

out_conn_dev:
	if (device->use_frwr)
		isert_conn_free_frwr_pool(isert_conn);
out_frwr:
	isert_device_try_release(device);
out_rsp_dma_map:
	ib_dma_unmap_single(ib_dev, isert_conn->login_rsp_dma,
			    ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);
out_req_dma_map:
	ib_dma_unmap_single(ib_dev, isert_conn->login_req_dma,
			    ISCSI_DEF_MAX_RECV_SEG_LEN, DMA_FROM_DEVICE);
out_login_buf:
	kfree(isert_conn->login_buf);
out:
	kfree(isert_conn);
	return ret;
}

static void
isert_connect_release(struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_device *device = isert_conn->conn_device;
	int cq_index;

	pr_debug("Entering isert_connect_release(): >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");

	if (device && device->use_frwr)
		isert_conn_free_frwr_pool(isert_conn);

	if (isert_conn->conn_qp) {
		cq_index = ((struct isert_cq_desc *)
			isert_conn->conn_qp->recv_cq->cq_context)->cq_index;
		pr_debug("isert_connect_release: cq_index: %d\n", cq_index);
		isert_conn->conn_device->cq_active_qps[cq_index]--;

		rdma_destroy_qp(isert_conn->conn_cm_id);
	}

	isert_free_rx_descriptors(isert_conn);
	rdma_destroy_id(isert_conn->conn_cm_id);

	if (isert_conn->login_buf) {
		ib_dma_unmap_single(ib_dev, isert_conn->login_rsp_dma,
				    ISER_RX_LOGIN_SIZE, DMA_TO_DEVICE);
		ib_dma_unmap_single(ib_dev, isert_conn->login_req_dma,
				    ISCSI_DEF_MAX_RECV_SEG_LEN,
				    DMA_FROM_DEVICE);
		kfree(isert_conn->login_buf);
	}
	kfree(isert_conn);

	if (device)
		isert_device_try_release(device);

	pr_debug("Leaving isert_connect_release >>>>>>>>>>>>\n");
}

static void
isert_connected_handler(struct rdma_cm_id *cma_id)
{
	return;
}

static void
isert_release_conn_kref(struct kref *kref)
{
	struct isert_conn *isert_conn = container_of(kref,
				struct isert_conn, conn_kref);

	pr_debug("Calling isert_connect_release for final kref %s/%d\n",
		 current->comm, current->pid);

	isert_connect_release(isert_conn);
}

static void
isert_put_conn(struct isert_conn *isert_conn)
{
	kref_put(&isert_conn->conn_kref, isert_release_conn_kref);
}

static void
isert_disconnect_work(struct work_struct *work)
{
	struct isert_conn *isert_conn = container_of(work,
				struct isert_conn, conn_logout_work);

	pr_debug("isert_disconnect_work(): >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	mutex_lock(&isert_conn->conn_mutex);
	isert_conn->state = ISER_CONN_DOWN;

	if (isert_conn->post_recv_buf_count == 0 &&
	    atomic_read(&isert_conn->post_send_buf_count) == 0) {
		pr_debug("Calling wake_up(&isert_conn->conn_wait);\n");
		mutex_unlock(&isert_conn->conn_mutex);
		goto wake_up;
	}
	if (!isert_conn->conn_cm_id) {
		mutex_unlock(&isert_conn->conn_mutex);
		isert_put_conn(isert_conn);
		return;
	}
	if (!isert_conn->logout_posted) {
		pr_debug("Calling rdma_disconnect for !logout_posted from"
			 " isert_disconnect_work\n");
		rdma_disconnect(isert_conn->conn_cm_id);
		mutex_unlock(&isert_conn->conn_mutex);
		iscsit_cause_connection_reinstatement(isert_conn->conn, 0);
		goto wake_up;
	}
	mutex_unlock(&isert_conn->conn_mutex);

wake_up:
	wake_up(&isert_conn->conn_wait);
	isert_put_conn(isert_conn);
}

static void
isert_disconnected_handler(struct rdma_cm_id *cma_id)
{
	struct isert_conn *isert_conn = (struct isert_conn *)cma_id->context;

	INIT_WORK(&isert_conn->conn_logout_work, isert_disconnect_work);
	schedule_work(&isert_conn->conn_logout_work);
}

static int
isert_cma_handler(struct rdma_cm_id *cma_id, struct rdma_cm_event *event)
{
	int ret = 0;

	pr_debug("isert_cma_handler: event %d status %d conn %p id %p\n",
		 event->event, event->status, cma_id->context, cma_id);

	switch (event->event) {
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		pr_debug("RDMA_CM_EVENT_CONNECT_REQUEST: >>>>>>>>>>>>>>>\n");
		ret = isert_connect_request(cma_id, event);
		break;
	case RDMA_CM_EVENT_ESTABLISHED:
		pr_debug("RDMA_CM_EVENT_ESTABLISHED >>>>>>>>>>>>>>\n");
		isert_connected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		pr_debug("RDMA_CM_EVENT_DISCONNECTED: >>>>>>>>>>>>>>\n");
		isert_disconnected_handler(cma_id);
		break;
	case RDMA_CM_EVENT_DEVICE_REMOVAL:
	case RDMA_CM_EVENT_ADDR_CHANGE:
		break;
	case RDMA_CM_EVENT_CONNECT_ERROR:
	default:
		pr_err("Unknown RDMA CMA event: %d\n", event->event);
		break;
	}

	if (ret != 0) {
		pr_err("isert_cma_handler failed RDMA_CM_EVENT: 0x%08x %d\n",
		       event->event, ret);
		dump_stack();
	}

	return ret;
}

static int
isert_post_recv(struct isert_conn *isert_conn, u32 count)
{
	struct ib_recv_wr *rx_wr, *rx_wr_failed;
	int i, ret;
	unsigned int rx_head = isert_conn->conn_rx_desc_head;
	struct iser_rx_desc *rx_desc;

	for (rx_wr = isert_conn->conn_rx_wr, i = 0; i < count; i++, rx_wr++) {
		rx_desc		= &isert_conn->conn_rx_descs[rx_head];
		rx_wr->wr_id	= (unsigned long)rx_desc;
		rx_wr->sg_list	= &rx_desc->rx_sg;
		rx_wr->num_sge	= 1;
		rx_wr->next	= rx_wr + 1;
		rx_head = (rx_head + 1) & (ISERT_QP_MAX_RECV_DTOS - 1);
	}

	rx_wr--;
	rx_wr->next = NULL; /* mark end of work requests list */

	isert_conn->post_recv_buf_count += count;
	ret = ib_post_recv(isert_conn->conn_qp, isert_conn->conn_rx_wr,
				&rx_wr_failed);
	if (ret) {
		pr_err("ib_post_recv() failed with ret: %d\n", ret);
		isert_conn->post_recv_buf_count -= count;
	} else {
		pr_debug("isert_post_recv(): Posted %d RX buffers\n", count);
		isert_conn->conn_rx_desc_head = rx_head;
	}
	return ret;
}

static int
isert_post_send(struct isert_conn *isert_conn, struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_send_wr send_wr, *send_wr_failed;
	int ret;

	ib_dma_sync_single_for_device(ib_dev, tx_desc->dma_addr,
				      ISER_HEADERS_LEN, DMA_TO_DEVICE);

	send_wr.next	= NULL;
	send_wr.wr_id	= (unsigned long)tx_desc;
	send_wr.sg_list	= tx_desc->tx_sg;
	send_wr.num_sge	= tx_desc->num_sge;
	send_wr.opcode	= IB_WR_SEND;
	send_wr.send_flags = IB_SEND_SIGNALED;

	atomic_inc(&isert_conn->post_send_buf_count);

	ret = ib_post_send(isert_conn->conn_qp, &send_wr, &send_wr_failed);
	if (ret) {
		pr_err("ib_post_send() failed, ret: %d\n", ret);
		atomic_dec(&isert_conn->post_send_buf_count);
	}

	return ret;
}

static void
isert_create_send_desc(struct isert_conn *isert_conn,
		       struct isert_cmd *isert_cmd,
		       struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	ib_dma_sync_single_for_cpu(ib_dev, tx_desc->dma_addr,
				   ISER_HEADERS_LEN, DMA_TO_DEVICE);

	memset(&tx_desc->iser_header, 0, sizeof(struct iser_hdr));
	tx_desc->iser_header.flags = ISER_VER;

	tx_desc->num_sge = 1;
	tx_desc->isert_cmd = isert_cmd;

	if (tx_desc->tx_sg[0].lkey != isert_conn->conn_mr->lkey) {
		tx_desc->tx_sg[0].lkey = isert_conn->conn_mr->lkey;
		pr_debug("tx_desc %p lkey mismatch, fixing\n", tx_desc);
	}
}

static int
isert_init_tx_hdrs(struct isert_conn *isert_conn,
		   struct iser_tx_desc *tx_desc)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	u64 dma_addr;

	dma_addr = ib_dma_map_single(ib_dev, (void *)tx_desc,
			ISER_HEADERS_LEN, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(ib_dev, dma_addr)) {
		pr_err("ib_dma_mapping_error() failed\n");
		return -ENOMEM;
	}

	tx_desc->dma_addr = dma_addr;
	tx_desc->tx_sg[0].addr	= tx_desc->dma_addr;
	tx_desc->tx_sg[0].length = ISER_HEADERS_LEN;
	tx_desc->tx_sg[0].lkey = isert_conn->conn_mr->lkey;

	pr_debug("isert_init_tx_hdrs: Setup tx_sg[0].addr: 0x%llx length: %u"
		 " lkey: 0x%08x\n", tx_desc->tx_sg[0].addr,
		 tx_desc->tx_sg[0].length, tx_desc->tx_sg[0].lkey);

	return 0;
}

static void
isert_init_send_wr(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		   struct ib_send_wr *send_wr, bool coalesce)
{
	struct iser_tx_desc *tx_desc = &isert_cmd->tx_desc;

	isert_cmd->rdma_wr.iser_ib_op = ISER_IB_SEND;
	send_wr->wr_id = (unsigned long)&isert_cmd->tx_desc;
	send_wr->opcode = IB_WR_SEND;
	send_wr->sg_list = &tx_desc->tx_sg[0];
	send_wr->num_sge = isert_cmd->tx_desc.num_sge;
	/*
	 * Coalesce send completion interrupts by only setting IB_SEND_SIGNALED
	 * bit for every ISERT_COMP_BATCH_COUNT number of ib_post_send() calls.
	 */
	mutex_lock(&isert_conn->conn_comp_mutex);
	if (coalesce &&
	    ++isert_conn->conn_comp_batch < ISERT_COMP_BATCH_COUNT) {
		llist_add(&tx_desc->comp_llnode, &isert_conn->conn_comp_llist);
		mutex_unlock(&isert_conn->conn_comp_mutex);
		return;
	}
	isert_conn->conn_comp_batch = 0;
	tx_desc->comp_llnode_batch = llist_del_all(&isert_conn->conn_comp_llist);
	mutex_unlock(&isert_conn->conn_comp_mutex);

	send_wr->send_flags = IB_SEND_SIGNALED;
}

static int
isert_rdma_post_recvl(struct isert_conn *isert_conn)
{
	struct ib_recv_wr rx_wr, *rx_wr_fail;
	struct ib_sge sge;
	int ret;

	memset(&sge, 0, sizeof(struct ib_sge));
	sge.addr = isert_conn->login_req_dma;
	sge.length = ISER_RX_LOGIN_SIZE;
	sge.lkey = isert_conn->conn_mr->lkey;

	pr_debug("Setup sge: addr: %llx length: %d 0x%08x\n",
		sge.addr, sge.length, sge.lkey);

	memset(&rx_wr, 0, sizeof(struct ib_recv_wr));
	rx_wr.wr_id = (unsigned long)isert_conn->login_req_buf;
	rx_wr.sg_list = &sge;
	rx_wr.num_sge = 1;

	isert_conn->post_recv_buf_count++;
	ret = ib_post_recv(isert_conn->conn_qp, &rx_wr, &rx_wr_fail);
	if (ret) {
		pr_err("ib_post_recv() failed: %d\n", ret);
		isert_conn->post_recv_buf_count--;
	}

	pr_debug("ib_post_recv(): returned success >>>>>>>>>>>>>>>>>>>>>>>>\n");
	return ret;
}

static int
isert_put_login_tx(struct iscsi_conn *conn, struct iscsi_login *login,
		   u32 length)
{
	struct isert_conn *isert_conn = conn->context;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iser_tx_desc *tx_desc = &isert_conn->conn_login_tx_desc;
	int ret;

	isert_create_send_desc(isert_conn, NULL, tx_desc);

	memcpy(&tx_desc->iscsi_header, &login->rsp[0],
	       sizeof(struct iscsi_hdr));

	isert_init_tx_hdrs(isert_conn, tx_desc);

	if (length > 0) {
		struct ib_sge *tx_dsg = &tx_desc->tx_sg[1];

		ib_dma_sync_single_for_cpu(ib_dev, isert_conn->login_rsp_dma,
					   length, DMA_TO_DEVICE);

		memcpy(isert_conn->login_rsp_buf, login->rsp_buf, length);

		ib_dma_sync_single_for_device(ib_dev, isert_conn->login_rsp_dma,
					      length, DMA_TO_DEVICE);

		tx_dsg->addr	= isert_conn->login_rsp_dma;
		tx_dsg->length	= length;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		tx_desc->num_sge = 2;
	}
	if (!login->login_failed) {
		if (login->login_complete) {
			ret = isert_alloc_rx_descriptors(isert_conn);
			if (ret)
				return ret;

			ret = isert_post_recv(isert_conn, ISERT_MIN_POSTED_RX);
			if (ret)
				return ret;

			isert_conn->state = ISER_CONN_UP;
			goto post_send;
		}

		ret = isert_rdma_post_recvl(isert_conn);
		if (ret)
			return ret;
	}
post_send:
	ret = isert_post_send(isert_conn, tx_desc);
	if (ret)
		return ret;

	return 0;
}

static void
isert_rx_login_req(struct iser_rx_desc *rx_desc, int rx_buflen,
		   struct isert_conn *isert_conn)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_login *login = conn->conn_login;
	int size;

	if (!login) {
		pr_err("conn->conn_login is NULL\n");
		dump_stack();
		return;
	}

	if (login->first_request) {
		struct iscsi_login_req *login_req =
			(struct iscsi_login_req *)&rx_desc->iscsi_header;
		/*
		 * Setup the initial iscsi_login values from the leading
		 * login request PDU.
		 */
		login->leading_connection = (!login_req->tsih) ? 1 : 0;
		login->current_stage =
			(login_req->flags & ISCSI_FLAG_LOGIN_CURRENT_STAGE_MASK)
			 >> 2;
		login->version_min	= login_req->min_version;
		login->version_max	= login_req->max_version;
		memcpy(login->isid, login_req->isid, 6);
		login->cmd_sn		= be32_to_cpu(login_req->cmdsn);
		login->init_task_tag	= login_req->itt;
		login->initial_exp_statsn = be32_to_cpu(login_req->exp_statsn);
		login->cid		= be16_to_cpu(login_req->cid);
		login->tsih		= be16_to_cpu(login_req->tsih);
	}

	memcpy(&login->req[0], (void *)&rx_desc->iscsi_header, ISCSI_HDR_LEN);

	size = min(rx_buflen, MAX_KEY_VALUE_PAIRS);
	pr_debug("Using login payload size: %d, rx_buflen: %d MAX_KEY_VALUE_PAIRS: %d\n",
		 size, rx_buflen, MAX_KEY_VALUE_PAIRS);
	memcpy(login->req_buf, &rx_desc->data[0], size);

	if (login->first_request) {
		complete(&isert_conn->conn_login_comp);
		return;
	}
	schedule_delayed_work(&conn->login_work, 0);
}

static struct iscsi_cmd
*isert_allocate_cmd(struct iscsi_conn *conn, gfp_t gfp)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_cmd *isert_cmd;
	struct iscsi_cmd *cmd;

	cmd = iscsit_allocate_cmd(conn, gfp);
	if (!cmd) {
		pr_err("Unable to allocate iscsi_cmd + isert_cmd\n");
		return NULL;
	}
	isert_cmd = iscsit_priv_cmd(cmd);
	isert_cmd->conn = isert_conn;
	isert_cmd->iscsi_cmd = cmd;

	return cmd;
}

static int
isert_handle_scsi_cmd(struct isert_conn *isert_conn,
		      struct isert_cmd *isert_cmd, struct iscsi_cmd *cmd,
		      struct iser_rx_desc *rx_desc, unsigned char *buf)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_scsi_req *hdr = (struct iscsi_scsi_req *)buf;
	struct scatterlist *sg;
	int imm_data, imm_data_len, unsol_data, sg_nents, rc;
	bool dump_payload = false;

	rc = iscsit_setup_scsi_cmd(conn, cmd, buf);
	if (rc < 0)
		return rc;

	imm_data = cmd->immediate_data;
	imm_data_len = cmd->first_burst_len;
	unsol_data = cmd->unsolicited_data;

	rc = iscsit_process_scsi_cmd(conn, cmd, hdr);
	if (rc < 0) {
		return 0;
	} else if (rc > 0) {
		dump_payload = true;
		goto sequence_cmd;
	}

	if (!imm_data)
		return 0;

	sg = &cmd->se_cmd.t_data_sg[0];
	sg_nents = max(1UL, DIV_ROUND_UP(imm_data_len, PAGE_SIZE));

	pr_debug("Copying Immediate SG: %p sg_nents: %u from %p imm_data_len: %d\n",
		 sg, sg_nents, &rx_desc->data[0], imm_data_len);

	sg_copy_from_buffer(sg, sg_nents, &rx_desc->data[0], imm_data_len);

	cmd->write_data_done += imm_data_len;

	if (cmd->write_data_done == cmd->se_cmd.data_length) {
		spin_lock_bh(&cmd->istate_lock);
		cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
		cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
		spin_unlock_bh(&cmd->istate_lock);
	}

sequence_cmd:
	rc = iscsit_sequence_cmd(conn, cmd, buf, hdr->cmdsn);

	if (!rc && dump_payload == false && unsol_data)
		iscsit_set_unsoliticed_dataout(cmd);

	return 0;
}

static int
isert_handle_iscsi_dataout(struct isert_conn *isert_conn,
			   struct iser_rx_desc *rx_desc, unsigned char *buf)
{
	struct scatterlist *sg_start;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_cmd *cmd = NULL;
	struct iscsi_data *hdr = (struct iscsi_data *)buf;
	u32 unsol_data_len = ntoh24(hdr->dlength);
	int rc, sg_nents, sg_off, page_off;

	rc = iscsit_check_dataout_hdr(conn, buf, &cmd);
	if (rc < 0)
		return rc;
	else if (!cmd)
		return 0;
	/*
	 * FIXME: Unexpected unsolicited_data out
	 */
	if (!cmd->unsolicited_data) {
		pr_err("Received unexpected solicited data payload\n");
		dump_stack();
		return -1;
	}

	pr_debug("Unsolicited DataOut unsol_data_len: %u, write_data_done: %u, data_length: %u\n",
		 unsol_data_len, cmd->write_data_done, cmd->se_cmd.data_length);

	sg_off = cmd->write_data_done / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = max(1UL, DIV_ROUND_UP(unsol_data_len, PAGE_SIZE));
	page_off = cmd->write_data_done % PAGE_SIZE;
	/*
	 * FIXME: Non page-aligned unsolicited_data out
	 */
	if (page_off) {
		pr_err("Received unexpected non-page aligned data payload\n");
		dump_stack();
		return -1;
	}
	pr_debug("Copying DataOut: sg_start: %p, sg_off: %u sg_nents: %u from %p %u\n",
		 sg_start, sg_off, sg_nents, &rx_desc->data[0], unsol_data_len);

	sg_copy_from_buffer(sg_start, sg_nents, &rx_desc->data[0],
			    unsol_data_len);

	rc = iscsit_check_dataout_payload(cmd, hdr, false);
	if (rc < 0)
		return rc;

	return 0;
}

static int
isert_handle_nop_out(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		     struct iscsi_cmd *cmd, struct iser_rx_desc *rx_desc,
		     unsigned char *buf)
{
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_nopout *hdr = (struct iscsi_nopout *)buf;
	int rc;

	rc = iscsit_setup_nop_out(conn, cmd, hdr);
	if (rc < 0)
		return rc;
	/*
	 * FIXME: Add support for NOPOUT payload using unsolicited RDMA payload
	 */

	return iscsit_process_nop_out(conn, cmd, hdr);
}

static int
isert_handle_text_cmd(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		      struct iscsi_cmd *cmd, struct iser_rx_desc *rx_desc,
		      struct iscsi_text *hdr)
{
	struct iscsi_conn *conn = isert_conn->conn;
	u32 payload_length = ntoh24(hdr->dlength);
	int rc;
	unsigned char *text_in;

	rc = iscsit_setup_text_cmd(conn, cmd, hdr);
	if (rc < 0)
		return rc;

	text_in = kzalloc(payload_length, GFP_KERNEL);
	if (!text_in) {
		pr_err("Unable to allocate text_in of payload_length: %u\n",
		       payload_length);
		return -ENOMEM;
	}
	cmd->text_in_ptr = text_in;

	memcpy(cmd->text_in_ptr, &rx_desc->data[0], payload_length);

	return iscsit_process_text_cmd(conn, cmd, hdr);
}

static int
isert_rx_opcode(struct isert_conn *isert_conn, struct iser_rx_desc *rx_desc,
		uint32_t read_stag, uint64_t read_va,
		uint32_t write_stag, uint64_t write_va)
{
	struct iscsi_hdr *hdr = &rx_desc->iscsi_header;
	struct iscsi_conn *conn = isert_conn->conn;
	struct iscsi_session *sess = conn->sess;
	struct iscsi_cmd *cmd;
	struct isert_cmd *isert_cmd;
	int ret = -EINVAL;
	u8 opcode = (hdr->opcode & ISCSI_OPCODE_MASK);

	if (sess->sess_ops->SessionType &&
	   (!(opcode & ISCSI_OP_TEXT) || !(opcode & ISCSI_OP_LOGOUT))) {
		pr_err("Got illegal opcode: 0x%02x in SessionType=Discovery,"
		       " ignoring\n", opcode);
		return 0;
	}

	switch (opcode) {
	case ISCSI_OP_SCSI_CMD:
		cmd = isert_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		isert_cmd->read_stag = read_stag;
		isert_cmd->read_va = read_va;
		isert_cmd->write_stag = write_stag;
		isert_cmd->write_va = write_va;

		ret = isert_handle_scsi_cmd(isert_conn, isert_cmd, cmd,
					rx_desc, (unsigned char *)hdr);
		break;
	case ISCSI_OP_NOOP_OUT:
		cmd = isert_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		ret = isert_handle_nop_out(isert_conn, isert_cmd, cmd,
					   rx_desc, (unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		ret = isert_handle_iscsi_dataout(isert_conn, rx_desc,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		cmd = isert_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		ret = iscsit_handle_task_mgt_cmd(conn, cmd,
						(unsigned char *)hdr);
		break;
	case ISCSI_OP_LOGOUT:
		cmd = isert_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		ret = iscsit_handle_logout_cmd(conn, cmd, (unsigned char *)hdr);
		if (ret > 0)
			wait_for_completion_timeout(&conn->conn_logout_comp,
						    SECONDS_FOR_LOGOUT_COMP *
						    HZ);
		break;
	case ISCSI_OP_TEXT:
		cmd = isert_allocate_cmd(conn, GFP_KERNEL);
		if (!cmd)
			break;

		isert_cmd = iscsit_priv_cmd(cmd);
		ret = isert_handle_text_cmd(isert_conn, isert_cmd, cmd,
					    rx_desc, (struct iscsi_text *)hdr);
		break;
	default:
		pr_err("Got unknown iSCSI OpCode: 0x%02x\n", opcode);
		dump_stack();
		break;
	}

	return ret;
}

static void
isert_rx_do_work(struct iser_rx_desc *rx_desc, struct isert_conn *isert_conn)
{
	struct iser_hdr *iser_hdr = &rx_desc->iser_header;
	uint64_t read_va = 0, write_va = 0;
	uint32_t read_stag = 0, write_stag = 0;
	int rc;

	switch (iser_hdr->flags & 0xF0) {
	case ISCSI_CTRL:
		if (iser_hdr->flags & ISER_RSV) {
			read_stag = be32_to_cpu(iser_hdr->read_stag);
			read_va = be64_to_cpu(iser_hdr->read_va);
			pr_debug("ISER_RSV: read_stag: 0x%08x read_va: 0x%16llx\n",
				 read_stag, (unsigned long long)read_va);
		}
		if (iser_hdr->flags & ISER_WSV) {
			write_stag = be32_to_cpu(iser_hdr->write_stag);
			write_va = be64_to_cpu(iser_hdr->write_va);
			pr_debug("ISER_WSV: write__stag: 0x%08x write_va: 0x%16llx\n",
				 write_stag, (unsigned long long)write_va);
		}

		pr_debug("ISER ISCSI_CTRL PDU\n");
		break;
	case ISER_HELLO:
		pr_err("iSER Hello message\n");
		break;
	default:
		pr_warn("Unknown iSER hdr flags: 0x%02x\n", iser_hdr->flags);
		break;
	}

	rc = isert_rx_opcode(isert_conn, rx_desc,
			     read_stag, read_va, write_stag, write_va);
}

static void
isert_rx_completion(struct iser_rx_desc *desc, struct isert_conn *isert_conn,
		    unsigned long xfer_len)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iscsi_hdr *hdr;
	u64 rx_dma;
	int rx_buflen, outstanding;

	if ((char *)desc == isert_conn->login_req_buf) {
		rx_dma = isert_conn->login_req_dma;
		rx_buflen = ISER_RX_LOGIN_SIZE;
		pr_debug("ISER login_buf: Using rx_dma: 0x%llx, rx_buflen: %d\n",
			 rx_dma, rx_buflen);
	} else {
		rx_dma = desc->dma_addr;
		rx_buflen = ISER_RX_PAYLOAD_SIZE;
		pr_debug("ISER req_buf: Using rx_dma: 0x%llx, rx_buflen: %d\n",
			 rx_dma, rx_buflen);
	}

	ib_dma_sync_single_for_cpu(ib_dev, rx_dma, rx_buflen, DMA_FROM_DEVICE);

	hdr = &desc->iscsi_header;
	pr_debug("iSCSI opcode: 0x%02x, ITT: 0x%08x, flags: 0x%02x dlen: %d\n",
		 hdr->opcode, hdr->itt, hdr->flags,
		 (int)(xfer_len - ISER_HEADERS_LEN));

	if ((char *)desc == isert_conn->login_req_buf)
		isert_rx_login_req(desc, xfer_len - ISER_HEADERS_LEN,
				   isert_conn);
	else
		isert_rx_do_work(desc, isert_conn);

	ib_dma_sync_single_for_device(ib_dev, rx_dma, rx_buflen,
				      DMA_FROM_DEVICE);

	isert_conn->post_recv_buf_count--;
	pr_debug("iSERT: Decremented post_recv_buf_count: %d\n",
		 isert_conn->post_recv_buf_count);

	if ((char *)desc == isert_conn->login_req_buf)
		return;

	outstanding = isert_conn->post_recv_buf_count;
	if (outstanding + ISERT_MIN_POSTED_RX <= ISERT_QP_MAX_RECV_DTOS) {
		int err, count = min(ISERT_QP_MAX_RECV_DTOS - outstanding,
				ISERT_MIN_POSTED_RX);
		err = isert_post_recv(isert_conn, count);
		if (err) {
			pr_err("isert_post_recv() count: %d failed, %d\n",
			       count, err);
		}
	}
}

static void
isert_unmap_cmd(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	pr_debug("isert_unmap_cmd: %p\n", isert_cmd);
	if (wr->sge) {
		pr_debug("isert_unmap_cmd: %p unmap_sg op\n", isert_cmd);
		ib_dma_unmap_sg(ib_dev, wr->sge, wr->num_sge,
				(wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
				DMA_TO_DEVICE : DMA_FROM_DEVICE);
		wr->sge = NULL;
	}

	if (wr->send_wr) {
		pr_debug("isert_unmap_cmd: %p free send_wr\n", isert_cmd);
		kfree(wr->send_wr);
		wr->send_wr = NULL;
	}

	if (wr->ib_sge) {
		pr_debug("isert_unmap_cmd: %p free ib_sge\n", isert_cmd);
		kfree(wr->ib_sge);
		wr->ib_sge = NULL;
	}
}

static void
isert_unreg_rdma_frwr(struct isert_cmd *isert_cmd, struct isert_conn *isert_conn)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	LIST_HEAD(unmap_list);

	pr_debug("unreg_frwr_cmd: %p\n", isert_cmd);

	if (wr->fr_desc) {
		pr_debug("unreg_frwr_cmd: %p free fr_desc %p\n",
			 isert_cmd, wr->fr_desc);
		spin_lock_bh(&isert_conn->conn_lock);
		list_add_tail(&wr->fr_desc->list, &isert_conn->conn_frwr_pool);
		spin_unlock_bh(&isert_conn->conn_lock);
		wr->fr_desc = NULL;
	}

	if (wr->sge) {
		pr_debug("unreg_frwr_cmd: %p unmap_sg op\n", isert_cmd);
		ib_dma_unmap_sg(ib_dev, wr->sge, wr->num_sge,
				(wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
				DMA_TO_DEVICE : DMA_FROM_DEVICE);
		wr->sge = NULL;
	}

	wr->ib_sge = NULL;
	wr->send_wr = NULL;
}

static void
isert_put_cmd(struct isert_cmd *isert_cmd)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct iscsi_conn *conn = isert_conn->conn;
	struct isert_device *device = isert_conn->conn_device;

	pr_debug("Entering isert_put_cmd: %p\n", isert_cmd);

	switch (cmd->iscsi_opcode) {
	case ISCSI_OP_SCSI_CMD:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		if (cmd->data_direction == DMA_TO_DEVICE)
			iscsit_stop_dataout_timer(cmd);

		device->unreg_rdma_mem(isert_cmd, isert_conn);
		transport_generic_free_cmd(&cmd->se_cmd, 0);
		break;
	case ISCSI_OP_SCSI_TMFUNC:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		transport_generic_free_cmd(&cmd->se_cmd, 0);
		break;
	case ISCSI_OP_REJECT:
	case ISCSI_OP_NOOP_OUT:
	case ISCSI_OP_TEXT:
		spin_lock_bh(&conn->cmd_lock);
		if (!list_empty(&cmd->i_conn_node))
			list_del(&cmd->i_conn_node);
		spin_unlock_bh(&conn->cmd_lock);

		/*
		 * Handle special case for REJECT when iscsi_add_reject*() has
		 * overwritten the original iscsi_opcode assignment, and the
		 * associated cmd->se_cmd needs to be released.
		 */
		if (cmd->se_cmd.se_tfo != NULL) {
			pr_debug("Calling transport_generic_free_cmd from"
				 " isert_put_cmd for 0x%02x\n",
				 cmd->iscsi_opcode);
			transport_generic_free_cmd(&cmd->se_cmd, 0);
			break;
		}
		/*
		 * Fall-through
		 */
	default:
		iscsit_release_cmd(cmd);
		break;
	}
}

static void
isert_unmap_tx_desc(struct iser_tx_desc *tx_desc, struct ib_device *ib_dev)
{
	if (tx_desc->dma_addr != 0) {
		pr_debug("Calling ib_dma_unmap_single for tx_desc->dma_addr\n");
		ib_dma_unmap_single(ib_dev, tx_desc->dma_addr,
				    ISER_HEADERS_LEN, DMA_TO_DEVICE);
		tx_desc->dma_addr = 0;
	}
}

static void
isert_completion_put(struct iser_tx_desc *tx_desc, struct isert_cmd *isert_cmd,
		     struct ib_device *ib_dev)
{
	if (isert_cmd->pdu_buf_dma != 0) {
		pr_debug("Calling ib_dma_unmap_single for isert_cmd->pdu_buf_dma\n");
		ib_dma_unmap_single(ib_dev, isert_cmd->pdu_buf_dma,
				    isert_cmd->pdu_buf_len, DMA_TO_DEVICE);
		isert_cmd->pdu_buf_dma = 0;
	}

	isert_unmap_tx_desc(tx_desc, ib_dev);
	isert_put_cmd(isert_cmd);
}

static void
isert_completion_rdma_read(struct iser_tx_desc *tx_desc,
			   struct isert_cmd *isert_cmd)
{
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct isert_device *device = isert_conn->conn_device;

	iscsit_stop_dataout_timer(cmd);
	device->unreg_rdma_mem(isert_cmd, isert_conn);
	cmd->write_data_done = wr->cur_rdma_length;

	pr_debug("Cmd: %p RDMA_READ comp calling execute_cmd\n", isert_cmd);
	spin_lock_bh(&cmd->istate_lock);
	cmd->cmd_flags |= ICF_GOT_LAST_DATAOUT;
	cmd->i_state = ISTATE_RECEIVED_LAST_DATAOUT;
	spin_unlock_bh(&cmd->istate_lock);

	target_execute_cmd(se_cmd);
}

static void
isert_do_control_comp(struct work_struct *work)
{
	struct isert_cmd *isert_cmd = container_of(work,
			struct isert_cmd, comp_work);
	struct isert_conn *isert_conn = isert_cmd->conn;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;

	switch (cmd->i_state) {
	case ISTATE_SEND_TASKMGTRSP:
		pr_debug("Calling iscsit_tmr_post_handler >>>>>>>>>>>>>>>>>\n");

		atomic_dec(&isert_conn->post_send_buf_count);
		iscsit_tmr_post_handler(cmd, cmd->conn);

		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev);
		break;
	case ISTATE_SEND_REJECT:
		pr_debug("Got isert_do_control_comp ISTATE_SEND_REJECT: >>>\n");
		atomic_dec(&isert_conn->post_send_buf_count);

		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev);
		break;
	case ISTATE_SEND_LOGOUTRSP:
		pr_debug("Calling iscsit_logout_post_handler >>>>>>>>>>>>>>\n");
		/*
		 * Call atomic_dec(&isert_conn->post_send_buf_count)
		 * from isert_free_conn()
		 */
		isert_conn->logout_posted = true;
		iscsit_logout_post_handler(cmd, cmd->conn);
		break;
	case ISTATE_SEND_TEXTRSP:
		atomic_dec(&isert_conn->post_send_buf_count);
		cmd->i_state = ISTATE_SENT_STATUS;
		isert_completion_put(&isert_cmd->tx_desc, isert_cmd, ib_dev);
		break;
	default:
		pr_err("Unknown do_control_comp i_state %d\n", cmd->i_state);
		dump_stack();
		break;
	}
}

static void
isert_response_completion(struct iser_tx_desc *tx_desc,
			  struct isert_cmd *isert_cmd,
			  struct isert_conn *isert_conn,
			  struct ib_device *ib_dev)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;

	if (cmd->i_state == ISTATE_SEND_TASKMGTRSP ||
	    cmd->i_state == ISTATE_SEND_LOGOUTRSP ||
	    cmd->i_state == ISTATE_SEND_REJECT ||
	    cmd->i_state == ISTATE_SEND_TEXTRSP) {
		isert_unmap_tx_desc(tx_desc, ib_dev);

		INIT_WORK(&isert_cmd->comp_work, isert_do_control_comp);
		queue_work(isert_comp_wq, &isert_cmd->comp_work);
		return;
	}
	atomic_dec(&isert_conn->post_send_buf_count);

	cmd->i_state = ISTATE_SENT_STATUS;
	isert_completion_put(tx_desc, isert_cmd, ib_dev);
}

static void
__isert_send_completion(struct iser_tx_desc *tx_desc,
		        struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct isert_cmd *isert_cmd = tx_desc->isert_cmd;
	struct isert_rdma_wr *wr;

	if (!isert_cmd) {
		atomic_dec(&isert_conn->post_send_buf_count);
		isert_unmap_tx_desc(tx_desc, ib_dev);
		return;
	}
	wr = &isert_cmd->rdma_wr;

	switch (wr->iser_ib_op) {
	case ISER_IB_RECV:
		pr_err("isert_send_completion: Got ISER_IB_RECV\n");
		dump_stack();
		break;
	case ISER_IB_SEND:
		pr_debug("isert_send_completion: Got ISER_IB_SEND\n");
		isert_response_completion(tx_desc, isert_cmd,
					  isert_conn, ib_dev);
		break;
	case ISER_IB_RDMA_WRITE:
		pr_err("isert_send_completion: Got ISER_IB_RDMA_WRITE\n");
		dump_stack();
		break;
	case ISER_IB_RDMA_READ:
		pr_debug("isert_send_completion: Got ISER_IB_RDMA_READ:\n");

		atomic_dec(&isert_conn->post_send_buf_count);
		isert_completion_rdma_read(tx_desc, isert_cmd);
		break;
	default:
		pr_err("Unknown wr->iser_ib_op: 0x%02x\n", wr->iser_ib_op);
		dump_stack();
		break;
	}
}

static void
isert_send_completion(struct iser_tx_desc *tx_desc,
		      struct isert_conn *isert_conn)
{
	struct llist_node *llnode = tx_desc->comp_llnode_batch;
	struct iser_tx_desc *t;
	/*
	 * Drain coalesced completion llist starting from comp_llnode_batch
	 * setup in isert_init_send_wr(), and then complete trailing tx_desc.
	 */
	while (llnode) {
		t = llist_entry(llnode, struct iser_tx_desc, comp_llnode);
		llnode = llist_next(llnode);
		__isert_send_completion(t, isert_conn);
	}
	__isert_send_completion(tx_desc, isert_conn);
}

static void
isert_cq_comp_err(struct iser_tx_desc *tx_desc, struct isert_conn *isert_conn)
{
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;

	if (tx_desc) {
		struct isert_cmd *isert_cmd = tx_desc->isert_cmd;

		if (!isert_cmd)
			isert_unmap_tx_desc(tx_desc, ib_dev);
		else
			isert_completion_put(tx_desc, isert_cmd, ib_dev);
	}

	if (isert_conn->post_recv_buf_count == 0 &&
	    atomic_read(&isert_conn->post_send_buf_count) == 0) {
		pr_debug("isert_cq_comp_err >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		pr_debug("Calling wake_up from isert_cq_comp_err\n");

		mutex_lock(&isert_conn->conn_mutex);
		if (isert_conn->state != ISER_CONN_DOWN)
			isert_conn->state = ISER_CONN_TERMINATING;
		mutex_unlock(&isert_conn->conn_mutex);

		wake_up(&isert_conn->conn_wait_comp_err);
	}
}

static void
isert_cq_tx_work(struct work_struct *work)
{
	struct isert_cq_desc *cq_desc = container_of(work,
				struct isert_cq_desc, cq_tx_work);
	struct isert_device *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;
	struct ib_cq *tx_cq = device->dev_tx_cq[cq_index];
	struct isert_conn *isert_conn;
	struct iser_tx_desc *tx_desc;
	struct ib_wc wc;

	while (ib_poll_cq(tx_cq, 1, &wc) == 1) {
		tx_desc = (struct iser_tx_desc *)(unsigned long)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			isert_send_completion(tx_desc, isert_conn);
		} else {
			pr_debug("TX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			pr_debug("TX wc.status: 0x%08x\n", wc.status);
			pr_debug("TX wc.vendor_err: 0x%08x\n", wc.vendor_err);
			atomic_dec(&isert_conn->post_send_buf_count);
			isert_cq_comp_err(tx_desc, isert_conn);
		}
	}

	ib_req_notify_cq(tx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_tx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	queue_work(isert_comp_wq, &cq_desc->cq_tx_work);
}

static void
isert_cq_rx_work(struct work_struct *work)
{
	struct isert_cq_desc *cq_desc = container_of(work,
			struct isert_cq_desc, cq_rx_work);
	struct isert_device *device = cq_desc->device;
	int cq_index = cq_desc->cq_index;
	struct ib_cq *rx_cq = device->dev_rx_cq[cq_index];
	struct isert_conn *isert_conn;
	struct iser_rx_desc *rx_desc;
	struct ib_wc wc;
	unsigned long xfer_len;

	while (ib_poll_cq(rx_cq, 1, &wc) == 1) {
		rx_desc = (struct iser_rx_desc *)(unsigned long)wc.wr_id;
		isert_conn = wc.qp->qp_context;

		if (wc.status == IB_WC_SUCCESS) {
			xfer_len = (unsigned long)wc.byte_len;
			isert_rx_completion(rx_desc, isert_conn, xfer_len);
		} else {
			pr_debug("RX wc.status != IB_WC_SUCCESS >>>>>>>>>>>>>>\n");
			if (wc.status != IB_WC_WR_FLUSH_ERR) {
				pr_debug("RX wc.status: 0x%08x\n", wc.status);
				pr_debug("RX wc.vendor_err: 0x%08x\n",
					 wc.vendor_err);
			}
			isert_conn->post_recv_buf_count--;
			isert_cq_comp_err(NULL, isert_conn);
		}
	}

	ib_req_notify_cq(rx_cq, IB_CQ_NEXT_COMP);
}

static void
isert_cq_rx_callback(struct ib_cq *cq, void *context)
{
	struct isert_cq_desc *cq_desc = (struct isert_cq_desc *)context;

	queue_work(isert_rx_wq, &cq_desc->cq_rx_work);
}

static int
isert_post_response(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd)
{
	struct ib_send_wr *wr_failed;
	int ret;

	atomic_inc(&isert_conn->post_send_buf_count);

	ret = ib_post_send(isert_conn->conn_qp, &isert_cmd->tx_desc.send_wr,
			   &wr_failed);
	if (ret) {
		pr_err("ib_post_send failed with %d\n", ret);
		atomic_dec(&isert_conn->post_send_buf_count);
		return ret;
	}
	return ret;
}

static int
isert_put_response(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct iscsi_scsi_rsp *hdr = (struct iscsi_scsi_rsp *)
				&isert_cmd->tx_desc.iscsi_header;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_rsp_pdu(cmd, conn, true, hdr);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	/*
	 * Attach SENSE DATA payload to iSCSI Response PDU
	 */
	if (cmd->se_cmd.sense_buffer &&
	    ((cmd->se_cmd.se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ||
	    (cmd->se_cmd.se_cmd_flags & SCF_EMULATED_TASK_SENSE))) {
		struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
		struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
		u32 padding, pdu_len;

		put_unaligned_be16(cmd->se_cmd.scsi_sense_length,
				   cmd->sense_buffer);
		cmd->se_cmd.scsi_sense_length += sizeof(__be16);

		padding = -(cmd->se_cmd.scsi_sense_length) & 3;
		hton24(hdr->dlength, (u32)cmd->se_cmd.scsi_sense_length);
		pdu_len = cmd->se_cmd.scsi_sense_length + padding;

		isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
				(void *)cmd->sense_buffer, pdu_len,
				DMA_TO_DEVICE);

		isert_cmd->pdu_buf_len = pdu_len;
		tx_dsg->addr	= isert_cmd->pdu_buf_dma;
		tx_dsg->length	= pdu_len;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		isert_cmd->tx_desc.num_sge = 2;
	}

	isert_init_send_wr(isert_conn, isert_cmd, send_wr, true);

	pr_debug("Posting SCSI Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_nopin(struct iscsi_cmd *cmd, struct iscsi_conn *conn,
		bool nopout_response)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_nopin_rsp(cmd, conn, (struct iscsi_nopin *)
			       &isert_cmd->tx_desc.iscsi_header,
			       nopout_response);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr, false);

	pr_debug("Posting NOPIN Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_logout_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_logout_rsp(cmd, conn, (struct iscsi_logout_rsp *)
				&isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr, false);

	pr_debug("Posting Logout Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_tm_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_task_mgt_rsp(cmd, conn, (struct iscsi_tm_rsp *)
				  &isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd, send_wr, false);

	pr_debug("Posting Task Management Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_reject(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
	struct iscsi_reject *hdr =
		(struct iscsi_reject *)&isert_cmd->tx_desc.iscsi_header;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_reject(cmd, conn, hdr);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);

	hton24(hdr->dlength, ISCSI_HDR_LEN);
	isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
			(void *)cmd->buf_ptr, ISCSI_HDR_LEN,
			DMA_TO_DEVICE);
	isert_cmd->pdu_buf_len = ISCSI_HDR_LEN;
	tx_dsg->addr	= isert_cmd->pdu_buf_dma;
	tx_dsg->length	= ISCSI_HDR_LEN;
	tx_dsg->lkey	= isert_conn->conn_mr->lkey;
	isert_cmd->tx_desc.num_sge = 2;

	isert_init_send_wr(isert_conn, isert_cmd, send_wr, false);

	pr_debug("Posting Reject IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_put_text_rsp(struct iscsi_cmd *cmd, struct iscsi_conn *conn)
{
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_send_wr *send_wr = &isert_cmd->tx_desc.send_wr;
	struct iscsi_text_rsp *hdr =
		(struct iscsi_text_rsp *)&isert_cmd->tx_desc.iscsi_header;
	u32 txt_rsp_len;
	int rc;

	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	rc = iscsit_build_text_rsp(cmd, conn, hdr);
	if (rc < 0)
		return rc;

	txt_rsp_len = rc;
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);

	if (txt_rsp_len) {
		struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
		struct ib_sge *tx_dsg = &isert_cmd->tx_desc.tx_sg[1];
		void *txt_rsp_buf = cmd->buf_ptr;

		isert_cmd->pdu_buf_dma = ib_dma_map_single(ib_dev,
				txt_rsp_buf, txt_rsp_len, DMA_TO_DEVICE);

		isert_cmd->pdu_buf_len = txt_rsp_len;
		tx_dsg->addr	= isert_cmd->pdu_buf_dma;
		tx_dsg->length	= txt_rsp_len;
		tx_dsg->lkey	= isert_conn->conn_mr->lkey;
		isert_cmd->tx_desc.num_sge = 2;
	}
	isert_init_send_wr(isert_conn, isert_cmd, send_wr, false);

	pr_debug("Posting Text Response IB_WR_SEND >>>>>>>>>>>>>>>>>>>>>>\n");

	return isert_post_response(isert_conn, isert_cmd);
}

static int
isert_build_rdma_wr(struct isert_conn *isert_conn, struct isert_cmd *isert_cmd,
		    struct ib_sge *ib_sge, struct ib_send_wr *send_wr,
		    u32 data_left, u32 offset)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct scatterlist *sg_start, *tmp_sg;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	u32 sg_off, page_off;
	int i = 0, sg_nents;

	sg_off = offset / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = min(cmd->se_cmd.t_data_nents - sg_off, isert_conn->max_sge);
	page_off = offset % PAGE_SIZE;

	send_wr->sg_list = ib_sge;
	send_wr->num_sge = sg_nents;
	send_wr->wr_id = (unsigned long)&isert_cmd->tx_desc;
	/*
	 * Perform mapping of TCM scatterlist memory ib_sge dma_addr.
	 */
	for_each_sg(sg_start, tmp_sg, sg_nents, i) {
		pr_debug("ISER RDMA from SGL dma_addr: 0x%16llx dma_len: %u, page_off: %u\n",
			 (unsigned long long)tmp_sg->dma_address,
			 tmp_sg->length, page_off);

		ib_sge->addr = ib_sg_dma_address(ib_dev, tmp_sg) + page_off;
		ib_sge->length = min_t(u32, data_left,
				ib_sg_dma_len(ib_dev, tmp_sg) - page_off);
		ib_sge->lkey = isert_conn->conn_mr->lkey;

		pr_debug("RDMA ib_sge: addr: 0x%16llx  length: %u lkey: %08x\n",
			 ib_sge->addr, ib_sge->length, ib_sge->lkey);
		page_off = 0;
		data_left -= ib_sge->length;
		ib_sge++;
		pr_debug("Incrementing ib_sge pointer to %p\n", ib_sge);
	}

	pr_debug("Set outgoing sg_list: %p num_sg: %u from TCM SGLs\n",
		 send_wr->sg_list, send_wr->num_sge);

	return sg_nents;
}

static int
isert_map_rdma(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
	       struct isert_rdma_wr *wr)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_send_wr *send_wr;
	struct ib_sge *ib_sge;
	struct scatterlist *sg_start;
	u32 sg_off = 0, sg_nents;
	u32 offset = 0, data_len, data_left, rdma_write_max, va_offset = 0;
	int ret = 0, count, i, ib_sge_cnt;

	if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
		data_left = se_cmd->data_length;
	} else {
		sg_off = cmd->write_data_done / PAGE_SIZE;
		data_left = se_cmd->data_length - cmd->write_data_done;
		offset = cmd->write_data_done;
		isert_cmd->tx_desc.isert_cmd = isert_cmd;
	}

	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = se_cmd->t_data_nents - sg_off;

	count = ib_dma_map_sg(ib_dev, sg_start, sg_nents,
			      (wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
			      DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (unlikely(!count)) {
		pr_err("Cmd: %p unrable to map SGs\n", isert_cmd);
		return -EINVAL;
	}
	wr->sge = sg_start;
	wr->num_sge = sg_nents;
	wr->cur_rdma_length = data_left;
	pr_debug("Mapped cmd: %p count: %u sg: %p sg_nents: %u rdma_len %d\n",
		 isert_cmd, count, sg_start, sg_nents, data_left);

	ib_sge = kzalloc(sizeof(struct ib_sge) * sg_nents, GFP_KERNEL);
	if (!ib_sge) {
		pr_warn("Unable to allocate ib_sge\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}
	wr->ib_sge = ib_sge;

	wr->send_wr_num = DIV_ROUND_UP(sg_nents, isert_conn->max_sge);
	wr->send_wr = kzalloc(sizeof(struct ib_send_wr) * wr->send_wr_num,
				GFP_KERNEL);
	if (!wr->send_wr) {
		pr_debug("Unable to allocate wr->send_wr\n");
		ret = -ENOMEM;
		goto unmap_sg;
	}

	wr->isert_cmd = isert_cmd;
	rdma_write_max = isert_conn->max_sge * PAGE_SIZE;

	for (i = 0; i < wr->send_wr_num; i++) {
		send_wr = &isert_cmd->rdma_wr.send_wr[i];
		data_len = min(data_left, rdma_write_max);

		send_wr->send_flags = 0;
		if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
			send_wr->opcode = IB_WR_RDMA_WRITE;
			send_wr->wr.rdma.remote_addr = isert_cmd->read_va + offset;
			send_wr->wr.rdma.rkey = isert_cmd->read_stag;
			if (i + 1 == wr->send_wr_num)
				send_wr->next = &isert_cmd->tx_desc.send_wr;
			else
				send_wr->next = &wr->send_wr[i + 1];
		} else {
			send_wr->opcode = IB_WR_RDMA_READ;
			send_wr->wr.rdma.remote_addr = isert_cmd->write_va + va_offset;
			send_wr->wr.rdma.rkey = isert_cmd->write_stag;
			if (i + 1 == wr->send_wr_num)
				send_wr->send_flags = IB_SEND_SIGNALED;
			else
				send_wr->next = &wr->send_wr[i + 1];
		}

		ib_sge_cnt = isert_build_rdma_wr(isert_conn, isert_cmd, ib_sge,
					send_wr, data_len, offset);
		ib_sge += ib_sge_cnt;

		offset += data_len;
		va_offset += data_len;
		data_left -= data_len;
	}

	return 0;
unmap_sg:
	ib_dma_unmap_sg(ib_dev, sg_start, sg_nents,
			(wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE);
	return ret;
}

static int
isert_map_fr_pagelist(struct ib_device *ib_dev,
		      struct scatterlist *sg_start, int sg_nents, u64 *fr_pl)
{
	u64 start_addr, end_addr, page, chunk_start = 0;
	struct scatterlist *tmp_sg;
	int i = 0, new_chunk, last_ent, n_pages;

	n_pages = 0;
	new_chunk = 1;
	last_ent = sg_nents - 1;
	for_each_sg(sg_start, tmp_sg, sg_nents, i) {
		start_addr = ib_sg_dma_address(ib_dev, tmp_sg);
		if (new_chunk)
			chunk_start = start_addr;
		end_addr = start_addr + ib_sg_dma_len(ib_dev, tmp_sg);

		pr_debug("SGL[%d] dma_addr: 0x%16llx len: %u\n",
			 i, (unsigned long long)tmp_sg->dma_address,
			 tmp_sg->length);

		if ((end_addr & ~PAGE_MASK) && i < last_ent) {
			new_chunk = 0;
			continue;
		}
		new_chunk = 1;

		page = chunk_start & PAGE_MASK;
		do {
			fr_pl[n_pages++] = page;
			pr_debug("Mapped page_list[%d] page_addr: 0x%16llx\n",
				 n_pages - 1, page);
			page += PAGE_SIZE;
		} while (page < end_addr);
	}

	return n_pages;
}

static int
isert_fast_reg_mr(struct fast_reg_descriptor *fr_desc,
		  struct isert_cmd *isert_cmd, struct isert_conn *isert_conn,
		  struct ib_sge *ib_sge, u32 offset, unsigned int data_len)
{
	struct iscsi_cmd *cmd = isert_cmd->iscsi_cmd;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct scatterlist *sg_start;
	u32 sg_off, page_off;
	struct ib_send_wr fr_wr, inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	u8 key;
	int ret, sg_nents, pagelist_len;

	sg_off = offset / PAGE_SIZE;
	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = min_t(unsigned int, cmd->se_cmd.t_data_nents - sg_off,
			 ISCSI_ISER_SG_TABLESIZE);
	page_off = offset % PAGE_SIZE;

	pr_debug("Cmd: %p use fr_desc %p sg_nents %d sg_off %d offset %u\n",
		 isert_cmd, fr_desc, sg_nents, sg_off, offset);

	pagelist_len = isert_map_fr_pagelist(ib_dev, sg_start, sg_nents,
					     &fr_desc->data_frpl->page_list[0]);

	if (!fr_desc->valid) {
		memset(&inv_wr, 0, sizeof(inv_wr));
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.ex.invalidate_rkey = fr_desc->data_mr->rkey;
		wr = &inv_wr;
		/* Bump the key */
		key = (u8)(fr_desc->data_mr->rkey & 0x000000FF);
		ib_update_fast_reg_key(fr_desc->data_mr, ++key);
	}

	/* Prepare FASTREG WR */
	memset(&fr_wr, 0, sizeof(fr_wr));
	fr_wr.opcode = IB_WR_FAST_REG_MR;
	fr_wr.wr.fast_reg.iova_start =
		fr_desc->data_frpl->page_list[0] + page_off;
	fr_wr.wr.fast_reg.page_list = fr_desc->data_frpl;
	fr_wr.wr.fast_reg.page_list_len = pagelist_len;
	fr_wr.wr.fast_reg.page_shift = PAGE_SHIFT;
	fr_wr.wr.fast_reg.length = data_len;
	fr_wr.wr.fast_reg.rkey = fr_desc->data_mr->rkey;
	fr_wr.wr.fast_reg.access_flags = IB_ACCESS_LOCAL_WRITE;

	if (!wr)
		wr = &fr_wr;
	else
		wr->next = &fr_wr;

	ret = ib_post_send(isert_conn->conn_qp, wr, &bad_wr);
	if (ret) {
		pr_err("fast registration failed, ret:%d\n", ret);
		return ret;
	}
	fr_desc->valid = false;

	ib_sge->lkey = fr_desc->data_mr->lkey;
	ib_sge->addr = fr_desc->data_frpl->page_list[0] + page_off;
	ib_sge->length = data_len;

	pr_debug("RDMA ib_sge: addr: 0x%16llx  length: %u lkey: %08x\n",
		 ib_sge->addr, ib_sge->length, ib_sge->lkey);

	return ret;
}

static int
isert_reg_rdma_frwr(struct iscsi_conn *conn, struct iscsi_cmd *cmd,
		    struct isert_rdma_wr *wr)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct ib_device *ib_dev = isert_conn->conn_cm_id->device;
	struct ib_send_wr *send_wr;
	struct ib_sge *ib_sge;
	struct scatterlist *sg_start;
	struct fast_reg_descriptor *fr_desc;
	u32 sg_off = 0, sg_nents;
	u32 offset = 0, data_len, data_left, rdma_write_max;
	int ret = 0, count;
	unsigned long flags;

	if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
		data_left = se_cmd->data_length;
	} else {
		sg_off = cmd->write_data_done / PAGE_SIZE;
		data_left = se_cmd->data_length - cmd->write_data_done;
		offset = cmd->write_data_done;
		isert_cmd->tx_desc.isert_cmd = isert_cmd;
	}

	sg_start = &cmd->se_cmd.t_data_sg[sg_off];
	sg_nents = se_cmd->t_data_nents - sg_off;

	count = ib_dma_map_sg(ib_dev, sg_start, sg_nents,
			      (wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
			      DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (unlikely(!count)) {
		pr_err("Cmd: %p unrable to map SGs\n", isert_cmd);
		return -EINVAL;
	}
	wr->sge = sg_start;
	wr->num_sge = sg_nents;
	pr_debug("Mapped cmd: %p count: %u sg: %p sg_nents: %u rdma_len %d\n",
		 isert_cmd, count, sg_start, sg_nents, data_left);

	memset(&wr->s_ib_sge, 0, sizeof(*ib_sge));
	ib_sge = &wr->s_ib_sge;
	wr->ib_sge = ib_sge;

	wr->send_wr_num = 1;
	memset(&wr->s_send_wr, 0, sizeof(*send_wr));
	wr->send_wr = &wr->s_send_wr;

	wr->isert_cmd = isert_cmd;
	rdma_write_max = ISCSI_ISER_SG_TABLESIZE * PAGE_SIZE;

	send_wr = &isert_cmd->rdma_wr.s_send_wr;
	send_wr->sg_list = ib_sge;
	send_wr->num_sge = 1;
	send_wr->wr_id = (unsigned long)&isert_cmd->tx_desc;
	if (wr->iser_ib_op == ISER_IB_RDMA_WRITE) {
		send_wr->opcode = IB_WR_RDMA_WRITE;
		send_wr->wr.rdma.remote_addr = isert_cmd->read_va;
		send_wr->wr.rdma.rkey = isert_cmd->read_stag;
		send_wr->send_flags = 0;
		send_wr->next = &isert_cmd->tx_desc.send_wr;
	} else {
		send_wr->opcode = IB_WR_RDMA_READ;
		send_wr->wr.rdma.remote_addr = isert_cmd->write_va;
		send_wr->wr.rdma.rkey = isert_cmd->write_stag;
		send_wr->send_flags = IB_SEND_SIGNALED;
	}

	data_len = min(data_left, rdma_write_max);
	wr->cur_rdma_length = data_len;

	/* if there is a single dma entry, dma mr is sufficient */
	if (count == 1) {
		ib_sge->addr = ib_sg_dma_address(ib_dev, &sg_start[0]);
		ib_sge->length = ib_sg_dma_len(ib_dev, &sg_start[0]);
		ib_sge->lkey = isert_conn->conn_mr->lkey;
		wr->fr_desc = NULL;
	} else {
		spin_lock_irqsave(&isert_conn->conn_lock, flags);
		fr_desc = list_first_entry(&isert_conn->conn_frwr_pool,
					   struct fast_reg_descriptor, list);
		list_del(&fr_desc->list);
		spin_unlock_irqrestore(&isert_conn->conn_lock, flags);
		wr->fr_desc = fr_desc;

		ret = isert_fast_reg_mr(fr_desc, isert_cmd, isert_conn,
				  ib_sge, offset, data_len);
		if (ret) {
			list_add_tail(&fr_desc->list, &isert_conn->conn_frwr_pool);
			goto unmap_sg;
		}
	}

	return 0;

unmap_sg:
	ib_dma_unmap_sg(ib_dev, sg_start, sg_nents,
			(wr->iser_ib_op == ISER_IB_RDMA_WRITE) ?
			DMA_TO_DEVICE : DMA_FROM_DEVICE);
	return ret;
}

static int
isert_put_datain(struct iscsi_conn *conn, struct iscsi_cmd *cmd)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;
	struct ib_send_wr *wr_failed;
	int rc;

	pr_debug("Cmd: %p RDMA_WRITE data_length: %u\n",
		 isert_cmd, se_cmd->data_length);
	wr->iser_ib_op = ISER_IB_RDMA_WRITE;
	rc = device->reg_rdma_mem(conn, cmd, wr);
	if (rc) {
		pr_err("Cmd: %p failed to prepare RDMA res\n", isert_cmd);
		return rc;
	}

	/*
	 * Build isert_conn->tx_desc for iSCSI response PDU and attach
	 */
	isert_create_send_desc(isert_conn, isert_cmd, &isert_cmd->tx_desc);
	iscsit_build_rsp_pdu(cmd, conn, true, (struct iscsi_scsi_rsp *)
			     &isert_cmd->tx_desc.iscsi_header);
	isert_init_tx_hdrs(isert_conn, &isert_cmd->tx_desc);
	isert_init_send_wr(isert_conn, isert_cmd,
			   &isert_cmd->tx_desc.send_wr, true);

	atomic_inc(&isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_WRITE\n");
		atomic_dec(&isert_conn->post_send_buf_count);
	}
	pr_debug("Cmd: %p posted RDMA_WRITE + Response for iSER Data READ\n",
		 isert_cmd);

	return 1;
}

static int
isert_get_dataout(struct iscsi_conn *conn, struct iscsi_cmd *cmd, bool recovery)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct isert_cmd *isert_cmd = iscsit_priv_cmd(cmd);
	struct isert_rdma_wr *wr = &isert_cmd->rdma_wr;
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	struct isert_device *device = isert_conn->conn_device;
	struct ib_send_wr *wr_failed;
	int rc;

	pr_debug("Cmd: %p RDMA_READ data_length: %u write_data_done: %u\n",
		 isert_cmd, se_cmd->data_length, cmd->write_data_done);
	wr->iser_ib_op = ISER_IB_RDMA_READ;
	rc = device->reg_rdma_mem(conn, cmd, wr);
	if (rc) {
		pr_err("Cmd: %p failed to prepare RDMA res\n", isert_cmd);
		return rc;
	}

	atomic_inc(&isert_conn->post_send_buf_count);

	rc = ib_post_send(isert_conn->conn_qp, wr->send_wr, &wr_failed);
	if (rc) {
		pr_warn("ib_post_send() failed for IB_WR_RDMA_READ\n");
		atomic_dec(&isert_conn->post_send_buf_count);
	}
	pr_debug("Cmd: %p posted RDMA_READ memory for ISER Data WRITE\n",
		 isert_cmd);

	return 0;
}

static int
isert_immediate_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

	switch (state) {
	case ISTATE_SEND_NOPIN_WANT_RESPONSE:
		ret = isert_put_nopin(cmd, conn, false);
		break;
	default:
		pr_err("Unknown immediate state: 0x%02x\n", state);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
isert_response_queue(struct iscsi_conn *conn, struct iscsi_cmd *cmd, int state)
{
	int ret;

	switch (state) {
	case ISTATE_SEND_LOGOUTRSP:
		ret = isert_put_logout_rsp(cmd, conn);
		if (!ret) {
			pr_debug("Returning iSER Logout -EAGAIN\n");
			ret = -EAGAIN;
		}
		break;
	case ISTATE_SEND_NOPIN:
		ret = isert_put_nopin(cmd, conn, true);
		break;
	case ISTATE_SEND_TASKMGTRSP:
		ret = isert_put_tm_rsp(cmd, conn);
		break;
	case ISTATE_SEND_REJECT:
		ret = isert_put_reject(cmd, conn);
		break;
	case ISTATE_SEND_TEXTRSP:
		ret = isert_put_text_rsp(cmd, conn);
		break;
	case ISTATE_SEND_STATUS:
		/*
		 * Special case for sending non GOOD SCSI status from TX thread
		 * context during pre se_cmd excecution failure.
		 */
		ret = isert_put_response(conn, cmd);
		break;
	default:
		pr_err("Unknown response state: 0x%02x\n", state);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int
isert_setup_np(struct iscsi_np *np,
	       struct __kernel_sockaddr_storage *ksockaddr)
{
	struct isert_np *isert_np;
	struct rdma_cm_id *isert_lid;
	struct sockaddr *sa;
	int ret;

	isert_np = kzalloc(sizeof(struct isert_np), GFP_KERNEL);
	if (!isert_np) {
		pr_err("Unable to allocate struct isert_np\n");
		return -ENOMEM;
	}
	init_waitqueue_head(&isert_np->np_accept_wq);
	mutex_init(&isert_np->np_accept_mutex);
	INIT_LIST_HEAD(&isert_np->np_accept_list);
	init_completion(&isert_np->np_login_comp);

	sa = (struct sockaddr *)ksockaddr;
	pr_debug("ksockaddr: %p, sa: %p\n", ksockaddr, sa);
	/*
	 * Setup the np->np_sockaddr from the passed sockaddr setup
	 * in iscsi_target_configfs.c code..
	 */
	memcpy(&np->np_sockaddr, ksockaddr,
	       sizeof(struct __kernel_sockaddr_storage));

	isert_lid = rdma_create_id(isert_cma_handler, np, RDMA_PS_TCP,
				IB_QPT_RC);
	if (IS_ERR(isert_lid)) {
		pr_err("rdma_create_id() for isert_listen_handler failed: %ld\n",
		       PTR_ERR(isert_lid));
		ret = PTR_ERR(isert_lid);
		goto out;
	}

	ret = rdma_bind_addr(isert_lid, sa);
	if (ret) {
		pr_err("rdma_bind_addr() for isert_lid failed: %d\n", ret);
		goto out_lid;
	}

	ret = rdma_listen(isert_lid, ISERT_RDMA_LISTEN_BACKLOG);
	if (ret) {
		pr_err("rdma_listen() for isert_lid failed: %d\n", ret);
		goto out_lid;
	}

	isert_np->np_cm_id = isert_lid;
	np->np_context = isert_np;
	pr_debug("Setup isert_lid->context: %p\n", isert_lid->context);

	return 0;

out_lid:
	rdma_destroy_id(isert_lid);
out:
	kfree(isert_np);
	return ret;
}

static int
isert_check_accept_queue(struct isert_np *isert_np)
{
	int empty;

	mutex_lock(&isert_np->np_accept_mutex);
	empty = list_empty(&isert_np->np_accept_list);
	mutex_unlock(&isert_np->np_accept_mutex);

	return empty;
}

static int
isert_rdma_accept(struct isert_conn *isert_conn)
{
	struct rdma_cm_id *cm_id = isert_conn->conn_cm_id;
	struct rdma_conn_param cp;
	int ret;

	memset(&cp, 0, sizeof(struct rdma_conn_param));
	cp.responder_resources = isert_conn->responder_resources;
	cp.initiator_depth = isert_conn->initiator_depth;
	cp.retry_count = 7;
	cp.rnr_retry_count = 7;

	pr_debug("Before rdma_accept >>>>>>>>>>>>>>>>>>>>.\n");

	ret = rdma_accept(cm_id, &cp);
	if (ret) {
		pr_err("rdma_accept() failed with: %d\n", ret);
		return ret;
	}

	pr_debug("After rdma_accept >>>>>>>>>>>>>>>>>>>>>.\n");

	return 0;
}

static int
isert_get_login_rx(struct iscsi_conn *conn, struct iscsi_login *login)
{
	struct isert_conn *isert_conn = (struct isert_conn *)conn->context;
	int ret;

	pr_debug("isert_get_login_rx before conn_login_comp conn: %p\n", conn);
	/*
	 * For login requests after the first PDU, isert_rx_login_req() will
	 * kick schedule_delayed_work(&conn->login_work) as the packet is
	 * received, which turns this callback from iscsi_target_do_login_rx()
	 * into a NOP.
	 */
	if (!login->first_request)
		return 0;

	ret = wait_for_completion_interruptible(&isert_conn->conn_login_comp);
	if (ret)
		return ret;

	pr_debug("isert_get_login_rx processing login->req: %p\n", login->req);
	return 0;
}

static void
isert_set_conn_info(struct iscsi_np *np, struct iscsi_conn *conn,
		    struct isert_conn *isert_conn)
{
	struct rdma_cm_id *cm_id = isert_conn->conn_cm_id;
	struct rdma_route *cm_route = &cm_id->route;
	struct sockaddr_in *sock_in;
	struct sockaddr_in6 *sock_in6;

	conn->login_family = np->np_sockaddr.ss_family;

	if (np->np_sockaddr.ss_family == AF_INET6) {
		sock_in6 = (struct sockaddr_in6 *)&cm_route->addr.dst_addr;
		snprintf(conn->login_ip, sizeof(conn->login_ip), "%pI6c",
			 &sock_in6->sin6_addr.in6_u);
		conn->login_port = ntohs(sock_in6->sin6_port);

		sock_in6 = (struct sockaddr_in6 *)&cm_route->addr.src_addr;
		snprintf(conn->local_ip, sizeof(conn->local_ip), "%pI6c",
			 &sock_in6->sin6_addr.in6_u);
		conn->local_port = ntohs(sock_in6->sin6_port);
	} else {
		sock_in = (struct sockaddr_in *)&cm_route->addr.dst_addr;
		sprintf(conn->login_ip, "%pI4",
			&sock_in->sin_addr.s_addr);
		conn->login_port = ntohs(sock_in->sin_port);

		sock_in = (struct sockaddr_in *)&cm_route->addr.src_addr;
		sprintf(conn->local_ip, "%pI4",
			&sock_in->sin_addr.s_addr);
		conn->local_port = ntohs(sock_in->sin_port);
	}
}

static int
isert_accept_np(struct iscsi_np *np, struct iscsi_conn *conn)
{
	struct isert_np *isert_np = (struct isert_np *)np->np_context;
	struct isert_conn *isert_conn;
	int max_accept = 0, ret;

accept_wait:
	ret = wait_event_interruptible(isert_np->np_accept_wq,
			!isert_check_accept_queue(isert_np) ||
			np->np_thread_state == ISCSI_NP_THREAD_RESET);
	if (max_accept > 5)
		return -ENODEV;

	spin_lock_bh(&np->np_thread_lock);
	if (np->np_thread_state == ISCSI_NP_THREAD_RESET) {
		spin_unlock_bh(&np->np_thread_lock);
		pr_err("ISCSI_NP_THREAD_RESET for isert_accept_np\n");
		return -ENODEV;
	}
	spin_unlock_bh(&np->np_thread_lock);

	mutex_lock(&isert_np->np_accept_mutex);
	if (list_empty(&isert_np->np_accept_list)) {
		mutex_unlock(&isert_np->np_accept_mutex);
		max_accept++;
		goto accept_wait;
	}
	isert_conn = list_first_entry(&isert_np->np_accept_list,
			struct isert_conn, conn_accept_node);
	list_del_init(&isert_conn->conn_accept_node);
	mutex_unlock(&isert_np->np_accept_mutex);

	conn->context = isert_conn;
	isert_conn->conn = conn;
	max_accept = 0;

	ret = isert_rdma_post_recvl(isert_conn);
	if (ret)
		return ret;

	ret = isert_rdma_accept(isert_conn);
	if (ret)
		return ret;

	isert_set_conn_info(np, conn, isert_conn);

	pr_debug("Processing isert_accept_np: isert_conn: %p\n", isert_conn);
	return 0;
}

static void
isert_free_np(struct iscsi_np *np)
{
	struct isert_np *isert_np = (struct isert_np *)np->np_context;

	rdma_destroy_id(isert_np->np_cm_id);

	np->np_context = NULL;
	kfree(isert_np);
}

static int isert_check_state(struct isert_conn *isert_conn, int state)
{
	int ret;

	mutex_lock(&isert_conn->conn_mutex);
	ret = (isert_conn->state == state);
	mutex_unlock(&isert_conn->conn_mutex);

	return ret;
}

static void isert_free_conn(struct iscsi_conn *conn)
{
	struct isert_conn *isert_conn = conn->context;

	pr_debug("isert_free_conn: Starting \n");
	/*
	 * Decrement post_send_buf_count for special case when called
	 * from isert_do_control_comp() -> iscsit_logout_post_handler()
	 */
	mutex_lock(&isert_conn->conn_mutex);
	if (isert_conn->logout_posted)
		atomic_dec(&isert_conn->post_send_buf_count);

	if (isert_conn->conn_cm_id && isert_conn->state != ISER_CONN_DOWN) {
		pr_debug("Calling rdma_disconnect from isert_free_conn\n");
		rdma_disconnect(isert_conn->conn_cm_id);
	}
	/*
	 * Only wait for conn_wait_comp_err if the isert_conn made it
	 * into full feature phase..
	 */
	if (isert_conn->state == ISER_CONN_UP) {
		pr_debug("isert_free_conn: Before wait_event comp_err %d\n",
			 isert_conn->state);
		mutex_unlock(&isert_conn->conn_mutex);

		wait_event(isert_conn->conn_wait_comp_err,
			  (isert_check_state(isert_conn, ISER_CONN_TERMINATING)));

		wait_event(isert_conn->conn_wait,
			  (isert_check_state(isert_conn, ISER_CONN_DOWN)));

		isert_put_conn(isert_conn);
		return;
	}
	if (isert_conn->state == ISER_CONN_INIT) {
		mutex_unlock(&isert_conn->conn_mutex);
		isert_put_conn(isert_conn);
		return;
	}
	pr_debug("isert_free_conn: wait_event conn_wait %d\n",
		 isert_conn->state);
	mutex_unlock(&isert_conn->conn_mutex);

	wait_event(isert_conn->conn_wait,
		  (isert_check_state(isert_conn, ISER_CONN_DOWN)));

	isert_put_conn(isert_conn);
}

static struct iscsit_transport iser_target_transport = {
	.name			= "IB/iSER",
	.transport_type		= ISCSI_INFINIBAND,
	.priv_size		= sizeof(struct isert_cmd),
	.owner			= THIS_MODULE,
	.iscsit_setup_np	= isert_setup_np,
	.iscsit_accept_np	= isert_accept_np,
	.iscsit_free_np		= isert_free_np,
	.iscsit_free_conn	= isert_free_conn,
	.iscsit_get_login_rx	= isert_get_login_rx,
	.iscsit_put_login_tx	= isert_put_login_tx,
	.iscsit_immediate_queue	= isert_immediate_queue,
	.iscsit_response_queue	= isert_response_queue,
	.iscsit_get_dataout	= isert_get_dataout,
	.iscsit_queue_data_in	= isert_put_datain,
	.iscsit_queue_status	= isert_put_response,
};

static int __init isert_init(void)
{
	int ret;

	isert_rx_wq = alloc_workqueue("isert_rx_wq", 0, 0);
	if (!isert_rx_wq) {
		pr_err("Unable to allocate isert_rx_wq\n");
		return -ENOMEM;
	}

	isert_comp_wq = alloc_workqueue("isert_comp_wq", 0, 0);
	if (!isert_comp_wq) {
		pr_err("Unable to allocate isert_comp_wq\n");
		ret = -ENOMEM;
		goto destroy_rx_wq;
	}

	iscsit_register_transport(&iser_target_transport);
	pr_debug("iSER_TARGET[0] - Loaded iser_target_transport\n");
	return 0;

destroy_rx_wq:
	destroy_workqueue(isert_rx_wq);
	return ret;
}

static void __exit isert_exit(void)
{
	destroy_workqueue(isert_comp_wq);
	destroy_workqueue(isert_rx_wq);
	iscsit_unregister_transport(&iser_target_transport);
	pr_debug("iSER_TARGET[0] - Released iser_target_transport\n");
}

MODULE_DESCRIPTION("iSER-Target for mainline target infrastructure");
MODULE_VERSION("0.1");
MODULE_AUTHOR("nab@Linux-iSCSI.org");
MODULE_LICENSE("GPL");

module_init(isert_init);
module_exit(isert_exit);
