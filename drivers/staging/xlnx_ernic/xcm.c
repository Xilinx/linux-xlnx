// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA Xilinx RDMA NIC driver
 *
 * Copyright (c) 2018-2019 Xilinx Pvt., Ltd
 *
 */

#include "xcommon.h"

unsigned int psn_num;
unsigned int mad_tid = 0x11223344;
/*****************************************************************************/

/**
 * xrnic_cm_prepare_mra() - Prepares Message Receipt Acknowledgment packet
 * @qp_attr: qp info for which mra packet is prepared
 * @msg : message being MRAed. 0x0- REQ, 0x1-REP, 0x2-LAP
 * @rq_buf: Buffer to store the message
 */
static void xrnic_cm_prepare_mra(struct xrnic_qp_attr *qp_attr,
				 enum xrnic_msg_mra msg, void *rq_buf)
{
	struct mra *mra;
	unsigned short temp;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;

	DEBUG_LOG("Entering %s\n", __func__);

	if (qp_attr->ip_addr_type == AF_INET) {
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
						&qp_attr->send_sgl_temp;
		mra = (struct mra *)&send_sgl_temp_ipv4->mad.data;
		temp = htons(MSG_RSP_ACK);
		send_sgl_temp_ipv4->mad.attribute_id = temp;
	} else {
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
						&qp_attr->send_sgl_temp;
		mra = (struct mra *)&send_sgl_temp_ipv6->mad.data;
		temp = htons(MSG_RSP_ACK);
		send_sgl_temp_ipv6->mad.attribute_id = temp;
	}
	mra->local_cm_id = qp_attr->local_cm_id;
	mra->remote_comm_id = qp_attr->remote_cm_id;
	pr_info("[%d %s] remote_comm_id 0%x\n", __LINE__, __func__,
		mra->remote_comm_id);
	mra->message_mraed = msg;
	mra->service_timeout = XRNIC_MRA_SERVICE_TIMEOUT;
	/*4.096 ìS*2 Service Timeout*/

	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_prepare_rep() - Prepares Reply packet
 * @qp_attr: qp info for which reply packet is prepared
 * @rq_buf: Buffer to store the data indicating the acceptance
 */
static void xrnic_cm_prepare_rep(struct xrnic_qp_attr *qp_attr, void *rq_buf)
{
	struct rdma_qp_attr *rdma_qp_attr = (struct rdma_qp_attr *)
		&((struct xrnic_reg_map *)xrnic_dev->xrnic_mmap.xrnic_regs)
		->rdma_qp_attr[qp_attr->qp_num - 2];
	struct ethhdr_t *eth_hdr;
	struct ipv4hdr *ipv4 = NULL;
	struct ipv6hdr *ipv6 = NULL;
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4 = NULL;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6 = NULL;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;
	struct rep *rep;
	struct req *req;
	unsigned short temp;
	unsigned char rq_opcode;
	unsigned int config_value, start_psn_value;
	struct xrnic_rdma_cm_id *cm_id = qp_attr->cm_id;

	if (qp_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
					&qp_attr->send_sgl_temp;
		rep = (struct rep *)&send_sgl_temp_ipv4->mad.data;
		eth_hdr = (struct ethhdr_t *)(recv_qp_pkt_ipv4);
		ipv4 = (struct ipv4hdr *)
				((char *)recv_qp_pkt_ipv4 + XRNIC_ETH_HLEN);
		req = (struct req *)&recv_qp_pkt_ipv4->mad.data;
		temp = htons(CONNECT_REPLY);
		send_sgl_temp_ipv4->mad.attribute_id = temp;
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
					&qp_attr->send_sgl_temp;
		rep = (struct rep *)&send_sgl_temp_ipv6->mad.data;
		eth_hdr = (struct ethhdr_t *)(recv_qp_pkt_ipv6);
		ipv6 = (struct ipv6hdr *)
				((char *)recv_qp_pkt_ipv6 + XRNIC_ETH_HLEN);
		req = (struct req *)&recv_qp_pkt_ipv6->mad.data;
		temp = htons(CONNECT_REPLY);
		send_sgl_temp_ipv6->mad.attribute_id = temp;
	}

	DEBUG_LOG("Entering %s\n", __func__);
	DEBUG_LOG("qp_num:%x\n", qp_attr->qp_num);

	rep->local_cm_id = qp_attr->local_cm_id;
	rep->remote_comm_id = qp_attr->remote_cm_id;

	rep->local_qpn = ((qp_attr->qp_num >> 16) & 0xFF) |
		(((qp_attr->qp_num >> 8) & 0xFF) << 8) |
		((qp_attr->qp_num & 0xFF) << 16);
	DEBUG_LOG("local_qpn %d qp_num %d\n",
		  rep->local_qpn, qp_attr->qp_num);

	memcpy((void *)rep->private_data,
	       (void *)&cm_id->conn_param.private_data,
	       cm_id->conn_param.private_data_len);

	DEBUG_LOG("cm_id->conn_param.private_data_len %d\n",
		  cm_id->conn_param.private_data_len);
	DEBUG_LOG("cm_id->conn_param.responder_resources %d\n",
		  cm_id->conn_param.responder_resources);
	DEBUG_LOG("cm_id->conn_param.initiator_depth %d\n",
		  cm_id->conn_param.initiator_depth);
	DEBUG_LOG("cm_id->conn_param.flow_control %d\n",
		  cm_id->conn_param.flow_control);
	DEBUG_LOG("cm_id->conn_param.retry_count %d\n",
		  cm_id->conn_param.retry_count);
	DEBUG_LOG("cm_id->conn_param.rnr_retry_count %d\n",
		  cm_id->conn_param.rnr_retry_count);

	/*Inititator depth not rquired for Target.*/
	rep->initiator_depth = cm_id->conn_param.initiator_depth;
	rep->responder_resources = cm_id->conn_param.responder_resources;
	rep->end_end_flow_control = cm_id->conn_param.flow_control;
	rep->rnr_retry_count = cm_id->conn_param.rnr_retry_count;
	rep->target_ack_delay = XRNIC_REP_TARGET_ACK_DELAY;
	rep->fail_over_accepted = XRNIC_REP_FAIL_OVER_ACCEPTED;

	DEBUG_LOG("req->initiator_depth %x\n", rep->initiator_depth);
	DEBUG_LOG("rep->responder_resources %x\n", rep->responder_resources);

	rep->sqr = XRNIC_REQ_SRQ;
	rep->local_ca_guid[0] = 0x7c;
	rep->local_ca_guid[1] = 0xfe;
	rep->local_ca_guid[2] = 0x90;
	rep->local_ca_guid[3] = 0x03;
	rep->local_ca_guid[4] = 0x00;
	rep->local_ca_guid[5] = 0xb8;
	rep->local_ca_guid[6] = 0x57;
	rep->local_ca_guid[7] = 0x70;

	qp_attr->remote_qpn = req->local_qpn;

	DEBUG_LOG("local_qpn [0x%x] [%d]\n", req->local_qpn,
		  ntohl(req->local_qpn));
	config_value =  ((req->local_qpn &  0xFF) << 16)
		|  (((req->local_qpn >> 8) & 0xFF) << 8)
		| ((req->local_qpn >> 16) & 0xFF);

	pr_info("config_value:%d req->local_qpn %d qp_attr->remote_qpn %d\n",
		config_value, req->local_qpn, qp_attr->remote_qpn);
	iowrite32(config_value, ((void *)(&rdma_qp_attr->dest_qp_conf)));

	/* Set the MAC address */
	config_value =  eth_hdr->h_source[5] | (eth_hdr->h_source[4] << 8) |
			(eth_hdr->h_source[3] << 16) |
			(eth_hdr->h_source[2] << 24);
	iowrite32(config_value, ((void *)(&rdma_qp_attr->mac_dest_addr_lsb)));
	DEBUG_LOG("mac_xrnic_src_addr_lsb->0x%x\n", config_value);

	config_value = eth_hdr->h_source[1] | (eth_hdr->h_source[0] << 8);
	iowrite32(config_value, ((void *)(&rdma_qp_attr->mac_dest_addr_msb)));
	DEBUG_LOG("mac_xrnic_src_addr_msb->0x%x\n", config_value);

	config_value = 0;
	DEBUG_LOG("req->start_psn:%x %x %x\n", req->start_psn[0],
		  req->start_psn[1], req->start_psn[2]);
	config_value = (req->start_psn[2] | (req->start_psn[1] << 8) |
			(req->start_psn[0] << 16));
	DEBUG_LOG("req->start psn 0x%x\n", config_value);
	start_psn_value = config_value;
	iowrite32(config_value, ((void *)(&rdma_qp_attr->sq_psn)));
	memcpy(rep->start_psn, req->start_psn, 3);

	if (qp_attr->ip_addr_type == AF_INET) {
		config_value = ipv4->src_addr;
		DEBUG_LOG("ipaddress:%x\n", config_value);
		iowrite32(htonl(config_value),
			  ((void *)(&rdma_qp_attr->ip_dest_addr1)));
		config_value = ioread32((void *)&rdma_qp_attr->ip_dest_addr1);
		DEBUG_LOG("read ipaddress:%x\n", config_value);
	} else {
		config_value = ipv6->saddr.in6_u.u6_addr32[3];
		DEBUG_LOG("ipaddress1:%x\n", config_value);
		iowrite32(htonl(config_value),
			  ((void *)(&rdma_qp_attr->ip_dest_addr1)));
		config_value = ipv6->saddr.in6_u.u6_addr32[2];
		DEBUG_LOG("ipaddress:%x\n", config_value);
		iowrite32(htonl(config_value),
			  ((void *)(&rdma_qp_attr->ip_dest_addr2)));
		config_value = ipv6->saddr.in6_u.u6_addr32[1];
		DEBUG_LOG("ipaddress:%x\n", config_value);
		iowrite32(htonl(config_value),
			  ((void *)(&rdma_qp_attr->ip_dest_addr3)));
		config_value = ipv6->saddr.in6_u.u6_addr32[0];
		DEBUG_LOG("ipaddress:%x\n", config_value);
		iowrite32(htonl(config_value),
			  ((void *)(&rdma_qp_attr->ip_dest_addr4)));
		config_value = ioread32((void *)&rdma_qp_attr->qp_conf);
		config_value = config_value | XRNIC_QP_CONFIG_IPV6_EN;
		iowrite32(config_value, ((void *)(&rdma_qp_attr->qp_conf)));
		DEBUG_LOG("read ipaddress:%x\n", config_value);
	}
	rq_opcode = XRNIC_RDMA_READ;
	config_value = ((start_psn_value - 1) | (rq_opcode << 24));
	iowrite32(config_value, ((void *)(&rdma_qp_attr->last_rq_req)));
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_prepare_rej() - Prepares Reject packet
 * @qp_attr: qp info for which reply packet is prepared
 * @reason: reason for the rejection
 * @msg: message whose contents cause sendor to reject communication
 *		0x0-REQ, 0x1-REP, 0x2-No message
 */
void xrnic_cm_prepare_rej(struct xrnic_qp_attr *qp_attr,
			  enum xrnic_rej_reason reason, enum xrnic_msg_rej msg)
{
	struct rej *rej;
	unsigned short temp;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;

	DEBUG_LOG("Entering %s\n", __func__);
	if (qp_attr->ip_addr_type == AF_INET) {
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
					&qp_attr->send_sgl_temp;
		rej = (struct rej *)&send_sgl_temp_ipv4->mad.data;
		temp = htons(CONNECT_REJECT);
		send_sgl_temp_ipv4->mad.attribute_id = temp;
	} else {
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
					&qp_attr->send_sgl_temp;
		rej = (struct rej *)&send_sgl_temp_ipv6->mad.data;
		temp = htons(CONNECT_REJECT);
		send_sgl_temp_ipv6->mad.attribute_id = temp;
	}
	pr_info("Sending rej\n");

	rej->local_cm_id = qp_attr->local_cm_id;
	rej->remote_comm_id = qp_attr->remote_cm_id;
	rej->message_rejected = msg;
	rej->reason = htons(reason);
	rej->reject_info_length = XRNIC_REJ_INFO_LEN;
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_prepare_initial_headers() - Retrieves information from the response
 * @qp_attr: qp info on which the response is sent
 * @rq_buf: receive queue buffer
 */
void xrnic_prepare_initial_headers(struct xrnic_qp_attr *qp_attr, void *rq_buf)
{
	struct mad *mad;
	unsigned char temp;
	struct ethhdr_t *eth_hdr;
	struct ipv4hdr  *ipv4;
	struct ipv6hdr  *ipv6;
	struct udphdr *udp;
	struct bth *bthp;
	struct deth *dethp;
	unsigned short *ipv4_hdr_ptr;
	unsigned int ipv4_hdr_chksum;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;
	int i;

	DEBUG_LOG("Entering %s\n", __func__);

	if (qp_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		eth_hdr = (struct ethhdr_t *)(recv_qp_pkt_ipv4);
		ipv4 = (struct ipv4hdr *)
				((char *)recv_qp_pkt_ipv4 + XRNIC_ETH_HLEN);
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
					&qp_attr->send_sgl_temp;
		/* In the ethernet header swap source and desitnation MAC */
		memcpy(send_sgl_temp_ipv4->eth.h_source,
		       eth_hdr->h_dest, XRNIC_ETH_ALEN);
		memcpy(send_sgl_temp_ipv4->eth.h_dest,
		       eth_hdr->h_source, XRNIC_ETH_ALEN);
		/* Copy the ethernet type field */
		send_sgl_temp_ipv4->eth.eth_type = eth_hdr->eth_type;

		/* In the IP  header swap source IP and desitnation IP */
		memcpy(&send_sgl_temp_ipv4->ipv4, ipv4,
		       sizeof(struct ipv4hdr));
		send_sgl_temp_ipv4->ipv4.dest_addr = ipv4->src_addr;
		send_sgl_temp_ipv4->ipv4.src_addr = ipv4->dest_addr;
		ipv4->total_length = (sizeof(struct ipv4hdr) +
				sizeof(struct udphdr) + sizeof(struct bth) +
				sizeof(struct deth) + sizeof(struct mad)) + 4;
		DEBUG_LOG("ipv4->total_length:%d\n", ipv4->total_length);
		DEBUG_LOG("ipv4 length:%d\n", sizeof(struct ipv4hdr));
		DEBUG_LOG("udp length:%d\n", sizeof(struct udphdr));
		DEBUG_LOG("ethhdr length:%d\n", sizeof(struct ethhdr_t));
		DEBUG_LOG("bth  length:%d\n", sizeof(struct bth));
		DEBUG_LOG("deth length:%d\n", sizeof(struct deth));

		send_sgl_temp_ipv4->ipv4.total_length =
					htons(ipv4->total_length);
		send_sgl_temp_ipv4->ipv4.hdr_chksum = 0;
		send_sgl_temp_ipv4->ipv4.id = ipv4->id;

		ipv4_hdr_ptr = (unsigned short *)
				(&send_sgl_temp_ipv4->ipv4);
		ipv4_hdr_chksum = 0;

		for (i = 0; i < 10; i++) {
			ipv4_hdr_chksum += *ipv4_hdr_ptr;
			ipv4_hdr_ptr++;
		}

		ipv4_hdr_chksum = ~((ipv4_hdr_chksum & 0x0000FFFF) +
				    (ipv4_hdr_chksum >> 16));
		send_sgl_temp_ipv4->ipv4.hdr_chksum = ipv4_hdr_chksum;
		DEBUG_LOG("check sum :%x\n", ipv4_hdr_chksum);
		udp = (struct udphdr *)((char *)recv_qp_pkt_ipv4 +
			XRNIC_ETH_HLEN + sizeof(struct ipv4hdr));
		/* Copy the UDP packets and update length field */
		send_sgl_temp_ipv4->udp.source = udp->source;
		send_sgl_temp_ipv4->udp.dest = udp->dest;
		udp->len = sizeof(struct udphdr) + sizeof(struct bth) +
			   sizeof(struct deth) + sizeof(struct mad) +
			   XRNIC_ICRC_SIZE;
		DEBUG_LOG("udp total_length:%x\n", udp->len);
		DEBUG_LOG("mad size:%d\n", sizeof(struct mad));
		send_sgl_temp_ipv4->udp.len = htons(udp->len);
		udp->check = 0;
		send_sgl_temp_ipv4->udp.check = htons(udp->check);

		/* Base Transport header setings */
		bthp = (struct bth *)((char *)udp + sizeof(struct udphdr));

		/* Fill bth fields */
		send_sgl_temp_ipv4->bth.opcode = IB_OPCODE_UD_SEND_ONLY;
		send_sgl_temp_ipv4->bth.solicited_event =
						XRNIC_SET_SOLICT_EVENT;
		send_sgl_temp_ipv4->bth.migration_req =
							XRNIC_MIGRATION_REQ;
		send_sgl_temp_ipv4->bth.pad_count = XRNIC_PAD_COUNT;
		send_sgl_temp_ipv4->bth.transport_hdr_ver =
						XRNIC_TRANSPORT_HDR_VER;
		DEBUG_LOG("bth transport hdr ver:%x\n",
			  bthp->transport_hdr_ver);
		send_sgl_temp_ipv4->bth.transport_hdr_ver =
						bthp->transport_hdr_ver;
		send_sgl_temp_ipv4->bth.destination_qp[0] = 0;
		send_sgl_temp_ipv4->bth.destination_qp[1] = 0;
		send_sgl_temp_ipv4->bth.destination_qp[2] =
							XRNIC_DESTINATION_QP;
		send_sgl_temp_ipv4->bth.reserved1 = XRNIC_RESERVED1;
		send_sgl_temp_ipv4->bth.ack_request = XRNIC_ACK_REQ;
		send_sgl_temp_ipv4->bth.reserved2 = XRNIC_RESERVED2;
		send_sgl_temp_ipv4->bth.pkt_seq_num = 1;
		send_sgl_temp_ipv4->bth.partition_key = 65535;

		/* DETH  setings */
		dethp = (struct deth *)((char *)bthp + sizeof(struct bth));
		send_sgl_temp_ipv4->deth.q_key = dethp->q_key;
		send_sgl_temp_ipv4->deth.reserved = XRNIC_DETH_RESERVED;
		send_sgl_temp_ipv4->deth.src_qp = dethp->src_qp;

		/* MAD  setings */
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
		send_sgl_temp_ipv4->mad.base_ver = XRNIC_MAD_BASE_VER;
		send_sgl_temp_ipv4->mad.class_version = 2;
		DEBUG_LOG("class:%x\n", send_sgl_temp_ipv4->mad.class_version);
		send_sgl_temp_ipv4->mad.mgmt_class = XRNIC_MAD_MGMT_CLASS;
		temp = (XRNIC_MAD_RESP_BIT << 7) | XRNIC_MAD_COMM_SEND;
		send_sgl_temp_ipv4->mad.resp_bit_method = temp;
		DEBUG_LOG("mad method:%x\n",
			  send_sgl_temp_ipv4->mad.resp_bit_method);
		send_sgl_temp_ipv4->mad.reserved = XRNIC_MAD_RESERVED;
		send_sgl_temp_ipv4->mad.transaction_id = mad->transaction_id;
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		eth_hdr = (struct ethhdr_t *)(recv_qp_pkt_ipv6);
		ipv6 = (struct ipv6hdr *)
				((char *)recv_qp_pkt_ipv6 + XRNIC_ETH_HLEN);
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
						&qp_attr->send_sgl_temp;
		/* In the ethernet header swap source and desitnation MAC */
		memcpy(send_sgl_temp_ipv6->eth.h_source,
		       eth_hdr->h_dest, XRNIC_ETH_ALEN);
		memcpy(send_sgl_temp_ipv6->eth.h_dest,
		       eth_hdr->h_source, XRNIC_ETH_ALEN);
		send_sgl_temp_ipv6->eth.eth_type = eth_hdr->eth_type;
		memcpy(&send_sgl_temp_ipv6->ipv6, ipv6,
		       sizeof(struct ipv6hdr));
		/* In the ethernet header swap source IP and desitnation IP */
		memcpy(&send_sgl_temp_ipv6->ipv6.daddr, &ipv6->saddr,
		       sizeof(struct in6_addr));
		memcpy(&send_sgl_temp_ipv6->ipv6.saddr, &ipv6->daddr,
		       sizeof(struct in6_addr));
		udp = (struct udphdr *)((char *)recv_qp_pkt_ipv6 +
				XRNIC_ETH_HLEN + sizeof(struct ipv6hdr));
		/* Copy the UDP packets and update length field */
		send_sgl_temp_ipv6->udp.source = udp->source;
		send_sgl_temp_ipv6->udp.dest = udp->dest;
		udp->len = sizeof(struct udphdr) + sizeof(struct bth) +
			   sizeof(struct deth) + sizeof(struct mad) +
			   XRNIC_ICRC_SIZE;
		DEBUG_LOG("udp total_length:%x\n", udp->len);
		DEBUG_LOG("mad size:%d\n", sizeof(struct mad));
		send_sgl_temp_ipv6->udp.len = htons(udp->len);
		udp->check = 0;
		send_sgl_temp_ipv6->udp.check = htons(udp->check);

		/* Base Transport header setings */
		bthp = (struct bth *)((char *)udp + sizeof(struct udphdr));

		/* Fill bth fields */
		send_sgl_temp_ipv6->bth.opcode = IB_OPCODE_UD_SEND_ONLY;
		send_sgl_temp_ipv6->bth.solicited_event =
							XRNIC_SET_SOLICT_EVENT;
		send_sgl_temp_ipv6->bth.migration_req = XRNIC_MIGRATION_REQ;
		send_sgl_temp_ipv6->bth.pad_count = XRNIC_PAD_COUNT;
		send_sgl_temp_ipv6->bth.transport_hdr_ver =
						XRNIC_TRANSPORT_HDR_VER;
		DEBUG_LOG("bth transport_hdr_ver:%x\n",
			  bthp->transport_hdr_ver);
		send_sgl_temp_ipv6->bth.transport_hdr_ver =
						bthp->transport_hdr_ver;
		send_sgl_temp_ipv6->bth.destination_qp[0] = 0;
		send_sgl_temp_ipv6->bth.destination_qp[1] = 0;
		send_sgl_temp_ipv6->bth.destination_qp[2] =
							XRNIC_DESTINATION_QP;
		send_sgl_temp_ipv6->bth.reserved1 = XRNIC_RESERVED1;
		send_sgl_temp_ipv6->bth.ack_request = XRNIC_ACK_REQ;
		send_sgl_temp_ipv6->bth.reserved2 = XRNIC_RESERVED2;
		send_sgl_temp_ipv6->bth.pkt_seq_num = 1;
		send_sgl_temp_ipv6->bth.partition_key = 65535;

		/* DETH  setings */
		dethp = (struct deth *)((char *)bthp + sizeof(struct bth));
		send_sgl_temp_ipv6->deth.q_key = dethp->q_key;
		send_sgl_temp_ipv6->deth.reserved = XRNIC_DETH_RESERVED;
		send_sgl_temp_ipv6->deth.src_qp = dethp->src_qp;

		/* MAD  setings */
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
		send_sgl_temp_ipv6->mad.base_ver = XRNIC_MAD_BASE_VER;
		send_sgl_temp_ipv6->mad.class_version = 2;
		DEBUG_LOG("class:%x\n", send_sgl_temp_ipv6->mad.class_version);
		send_sgl_temp_ipv6->mad.mgmt_class = XRNIC_MAD_MGMT_CLASS;
		temp = (XRNIC_MAD_RESP_BIT << 7) | XRNIC_MAD_COMM_SEND;
		send_sgl_temp_ipv6->mad.resp_bit_method = temp;
		DEBUG_LOG("mad method:%x\n",
			  send_sgl_temp_ipv6->mad.resp_bit_method);
		send_sgl_temp_ipv6->mad.reserved = XRNIC_MAD_RESERVED;
		send_sgl_temp_ipv6->mad.transaction_id = mad->transaction_id;
	}

	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_prepare_dreq() - Prepares Disconnection Request Packet
 * @qp_attr: qp info to be released
 */
static void xrnic_cm_prepare_dreq(struct xrnic_qp_attr *qp_attr)
{
	struct dreq *dreq;
	unsigned short temp;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;

	DEBUG_LOG("Entering %s\n", __func__);

	if (qp_attr->ip_addr_type == AF_INET) {
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
						&qp_attr->send_sgl_temp;
		dreq = (struct dreq *)&send_sgl_temp_ipv4->mad.data;
		temp = htons(DISCONNECT_REQUEST);
		send_sgl_temp_ipv4->mad.attribute_id = temp;
	} else {
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
						&qp_attr->send_sgl_temp;
		dreq = (struct dreq *)&send_sgl_temp_ipv6->mad.data;
		temp = htons(DISCONNECT_REQUEST);
		send_sgl_temp_ipv6->mad.attribute_id = temp;
	}
	dreq->local_cm_id = qp_attr->local_cm_id;
	dreq->remote_comm_id = qp_attr->remote_cm_id;
	dreq->remote_qpn_eecn = qp_attr->remote_qpn;

	DEBUG_LOG("Exiting %s %d %d\n",
		  __func__, qp_attr->remote_qpn, dreq->remote_qpn_eecn);
}

/**
 * xrnic_cm_disconnect_send_handler() - Sends Disconnection Request and frees
 * all the attributes related to the qp
 * @qp_attr: qp info to be released by dreq
 */
void xrnic_cm_disconnect_send_handler(struct xrnic_qp_attr *qp_attr)
{
	int	qp1_send_pkt_size;

	DEBUG_LOG("Entering %s\n", __func__);
	if (qp_attr->ip_addr_type == AF_INET)
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv4);
	else
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv6);

	xrnic_cm_prepare_dreq(qp_attr);
	xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
			       qp_attr->qp1_attr,
				qp1_send_pkt_size);
	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_DREQ_SENT;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	qp_attr->qp_timer.expires =  jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
		(1 << XRNIC_CM_TIMER_TIMEOUT));
	add_timer(&qp_attr->qp_timer);
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_prepare_drep() - Prepares disconnect reply packet
 * @qp_attr: qp info for which drep packet is prepared
 * @rq_buf: receive queue buffer
 */
static void xrnic_cm_prepare_drep(struct xrnic_qp_attr *qp_attr,
				  void *rq_buf)
{
	struct drep *drep;
	unsigned short temp;
	struct qp_cm_pkt_hdr_ipv4 *send_sgl_temp_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *send_sgl_temp_ipv6;

	DEBUG_LOG("Enteing %s\n", __func__);
	if (qp_attr->ip_addr_type == AF_INET) {
		send_sgl_temp_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)
						&qp_attr->send_sgl_temp;
		drep = (struct drep *)&send_sgl_temp_ipv4->mad.data;
		temp = htons(DISCONNECT_REPLY);
		send_sgl_temp_ipv4->mad.attribute_id = temp;
	} else {
		send_sgl_temp_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)
						&qp_attr->send_sgl_temp;
		drep = (struct drep *)&send_sgl_temp_ipv6->mad.data;
		temp = htons(DISCONNECT_REPLY);
		send_sgl_temp_ipv6->mad.attribute_id = temp;
	}
	drep->local_cm_id = qp_attr->local_cm_id;
	drep->remote_comm_id = qp_attr->remote_cm_id;

	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_disconnect_request_handler() - Handles Disconnection Request.
 * @qp_attr: qp info on which the reply is to be sent
 * @rq_buf: receive queue buffer
 */
static void xrnic_cm_disconnect_request_handler(struct xrnic_qp_attr *qp_attr,
						void  *rq_buf)
{
	int	qp1_send_pkt_size;
	struct xrnic_rdma_cm_id_info *cm_id_info;

	DEBUG_LOG("Entering %s qp_num %d\n", __func__, qp_attr->qp_num);
	if (qp_attr->cm_id) {
		DEBUG_LOG("cm id is not clean qp_num %d\n", qp_attr->qp_num);
		cm_id_info = qp_attr->cm_id->cm_id_info;
		cm_id_info->conn_event_info.cm_event = XRNIC_DREQ_RCVD;
		cm_id_info->conn_event_info.status = 0;
		cm_id_info->conn_event_info.private_data_len = 0;
		cm_id_info->conn_event_info.private_data = NULL;
		qp_attr->cm_id->xrnic_cm_handler(qp_attr->cm_id,
						&cm_id_info->conn_event_info);
		qp_attr->cm_id = NULL;
	} else {
		pr_err("CM ID is NULL\n");
	}
	if (qp_attr->ip_addr_type == AF_INET)
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv4);
	else
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv6);
	qp_attr->curr_state = XRNIC_DREQ_RCVD;
	xrnic_cm_prepare_drep(qp_attr, rq_buf);
	xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
			       qp_attr->qp1_attr,
				qp1_send_pkt_size);

	qp_attr->curr_state = XRNIC_TIMEWAIT;
	qp_attr->resend_count = 0;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	qp_attr->qp_timer.expires =  jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
		(1 << XRNIC_CM_TIMER_TIMEOUT));
	add_timer(&qp_attr->qp_timer);
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_disconnect_reply_handler() - Handles disconnect reply packets.
 * @qp_attr: qp info of which qp to be destroyed
 * @rq_buf: receive queue buffer
 */
static void xrnic_cm_disconnect_reply_handler(struct xrnic_qp_attr *qp_attr,
					      void *rq_buf)
{
	DEBUG_LOG("Entering %s\n", __func__);
	qp_attr->curr_state = XRNIC_DREQ_RCVD;
	/*Call back to nvmeof. */

	/*TBD: Need to Change state while handling with Rimer.*/
	qp_attr->curr_state = XRNIC_TIMEWAIT;
	qp_attr->resend_count = 0;

	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	qp_attr->qp_timer.expires =  jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
		(1 << XRNIC_CM_TIMER_TIMEOUT));
	add_timer(&qp_attr->qp_timer);
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_connect_reject_handler() - Handles connect reject packets.
 * @qp_attr: qp info
 * @rq_buf: receive queue buffer
 */
static void xrnic_cm_connect_reject_handler(struct xrnic_qp_attr *qp_attr,
					    void *rq_buf)
{
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;
	struct mad *mad;
	struct rej *rej;
	struct xrnic_rdma_cm_id_info *cm_id_info;

	DEBUG_LOG("Entering %s\n", __func__);

	if (qp_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
		rej = (struct rej *)&mad->data;
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
		rej = (struct rej *)&mad->data;
	}

	if (rej->message_rejected == XRNIC_REJ_REP ||
	    rej->message_rejected == XRNIC_REJ_REQ ||
	    rej->message_rejected == XRNIC_REJ_OTHERS) {
		qp_attr->resend_count = 0;
		qp_attr->remote_cm_id = 0;
		qp_attr->cm_id = NULL;
		xrnic_reset_io_qp(qp_attr);
		memset((void *)&qp_attr->mac_addr, 0, XRNIC_ETH_ALEN);
		qp_attr->ip_addr_type = 0;
		xrnic_qp_app_configuration(qp_attr->qp_num,
					   XRNIC_HW_QP_DISABLE);
		qp_attr->curr_state = XRNIC_LISTEN;
		if (timer_pending(&qp_attr->qp_timer))
			del_timer_sync(&qp_attr->qp_timer);
		if (qp_attr->cm_id) {
			cm_id_info = qp_attr->cm_id->cm_id_info;
			cm_id_info->conn_event_info.cm_event = XRNIC_REJ_RECV;
			cm_id_info->conn_event_info.status = 0;
			cm_id_info->conn_event_info.private_data_len = 0;
			cm_id_info->conn_event_info.private_data = NULL;
			qp_attr->cm_id->xrnic_cm_handler(qp_attr->cm_id,
						&cm_id_info->conn_event_info);
		} else {
			pr_err("%s CM_ID is NULL\n", __func__);
		}
	}
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_msg_rsp_ack_handler() - Handles message response packets.
 * @qp_attr: qp info
 * @rq_buf: receive queue buffer
 */
void xrnic_cm_msg_rsp_ack_handler(struct xrnic_qp_attr *qp_attr, void *rq_buf)
{
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;
	struct mad *mad;
	struct mra *mra;

	DEBUG_LOG("Enter ing %s\n", __func__);
	if (qp_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
		mra = (struct mra *)&mad->data;
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
		mra = (struct mra *)&mad->data;
	}

	if (mra->message_mraed == XRNIC_MRA_REP) {
		qp_attr->curr_state = XRNIC_MRA_RCVD;
		qp_attr->resend_count = 0;
		if (timer_pending(&qp_attr->qp_timer))
			del_timer_sync(&qp_attr->qp_timer);
			qp_attr->qp_timer.expires =  jiffies +
			usecs_to_jiffies(XRNIC_CM_TIMEOUT *
			(1 << XRNIC_CM_TIMER_TIMEOUT));
			add_timer(&qp_attr->qp_timer);
	}
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_connect_rep_handler() - handles connect reply packets
 * @qp_attr : qp info
 * @rq_buf : receive queue buffer
 */
static void xrnic_cm_connect_rep_handler(struct xrnic_qp_attr *qp_attr,
					 void *rq_buf)
{
	struct xrnic_rdma_cm_id_info *cm_id_info;

	DEBUG_LOG("Entering %s\n", __func__);
	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_REP_RCVD;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	if (qp_attr->cm_id) {
		cm_id_info = qp_attr->cm_id->cm_id_info;
		cm_id_info->conn_event_info.cm_event = XRNIC_REP_RCVD;
		cm_id_info->conn_event_info.status = 0;
		cm_id_info->conn_event_info.private_data_len = 0;
		cm_id_info->conn_event_info.private_data = NULL;
		qp_attr->cm_id->xrnic_cm_handler(qp_attr->cm_id,
				&cm_id_info->conn_event_info);
	} else {
		pr_err("%s CM_ID is NULL\n", __func__);
	}
	pr_info("Connection Established Local QPn=%#x\n", qp_attr->qp_num);
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_cm_ready_to_use_handler() - handles ready to use packets
 * @qp_attr : qp info
 * @rq_buf : receive queue buffer
 */
static void xrnic_cm_ready_to_use_handler(struct xrnic_qp_attr *qp_attr,
					  void *rq_buf)
{
	struct xrnic_rdma_cm_id_info *cm_id_info;

	DEBUG_LOG("Entering %s\n", __func__);
	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_ESTABLISHD;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	if (qp_attr->cm_id) {
		cm_id_info = qp_attr->cm_id->cm_id_info;
		cm_id_info->conn_event_info.cm_event = XRNIC_ESTABLISHD;
		cm_id_info->conn_event_info.status = 0;
		cm_id_info->conn_event_info.private_data_len = 0;
		cm_id_info->conn_event_info.private_data = NULL;
		qp_attr->cm_id->xrnic_cm_handler(qp_attr->cm_id,
			&cm_id_info->conn_event_info);
	} else {
		pr_err("%s CM_ID is NULL\n", __func__);
	}
	pr_info("Connection Established Local QPn=%x\n", qp_attr->qp_num);
	DEBUG_LOG("Exiting %s\n", __func__);
}

/**
 * xrnic_create_child_cm() - creates child cm.
 * @cm_id_info : to update child cm info after creation
 */
static void xrnic_create_child_cm(struct xrnic_rdma_cm_id_info *cm_id_info)
{
	struct xrnic_rdma_cm_id *ch_cm;

	ch_cm = kzalloc(sizeof(*ch_cm), GFP_ATOMIC);
	cm_id_info->child_cm_id = ch_cm;
}

/**
 * xrnic_cm_connect_request_handler() - handles connect request packets.
 * @qp_attr : qp info
 * @rq_buf : receive queue buffer
 */
static void xrnic_cm_connect_request_handler(struct xrnic_qp_attr *qp_attr,
					     void *rq_buf)
{
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4 = NULL;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6 = NULL;
	struct mad *mad = NULL;
	struct req *req = NULL;
	int	qp1_send_pkt_size, child_qp_num, status;
	enum xrnic_rej_reason reason = XRNIC_REJ_CONSUMER_REJECT;
	enum xrnic_msg_rej msg_rej;
	enum xrnic_msg_mra msg_mra;
	u16 port_num;
	void *temp;
	struct xrnic_rdma_cm_id *child_cm_id;
	struct xrnic_rdma_cm_id *parent_cm_id;
	struct xrnic_rdma_cm_id_info *child_cm_id_info;

	DEBUG_LOG("Entering %s\n", __func__);
	if (qp_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
		req = (struct req *)&mad->data;
		qp1_send_pkt_size =  sizeof(struct qp_cm_pkt_hdr_ipv4);
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
		req = (struct req *)&mad->data;
		qp1_send_pkt_size =  sizeof(struct qp_cm_pkt_hdr_ipv6);
	}

	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_REQ_RCVD;

	DEBUG_LOG("req-> local_cm_resp_tout:%x.\n", req->local_cm_resp_tout);
	DEBUG_LOG("req-> path_packet_payload_mtu:%x.\n",
		  req->path_packet_payload_mtu);
	if (req->remote_cm_resp_tout < XRNIC_REQ_REMOTE_CM_RESP_TOUT) {
		pr_info("remote_cm_resp_tout:%x", req->remote_cm_resp_tout);

		msg_mra = XRNIC_MRA_REQ;
		xrnic_cm_prepare_mra(qp_attr, msg_mra, rq_buf);
		xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
				       qp_attr->qp1_attr, qp1_send_pkt_size);
		qp_attr->curr_state = XRNIC_MRA_SENT;
	}

	temp = (char *)&req->private_data;
	temp += 36;
	port_num = htons(req->service_id[6] | req->service_id[7] << 8);
	DEBUG_LOG("req-> service_id[0]:%x.\n", req->service_id[0]);
	DEBUG_LOG("req-> service_id[1]:%x.\n", req->service_id[1]);
	DEBUG_LOG("req-> service_id[2]:%x.\n", req->service_id[2]);
	DEBUG_LOG("req-> service_id[3]:%x.\n", req->service_id[3]);
	DEBUG_LOG("req-> service_id[4]:%x.\n", req->service_id[4]);
	DEBUG_LOG("req-> service_id[5]:%x.\n", req->service_id[5]);
	DEBUG_LOG("req-> service_id[6]:%x.\n", req->service_id[6]);
	DEBUG_LOG("req-> service_id[7]:%x.\n", req->service_id[7]);
	DEBUG_LOG("req->port_num:%d,%x\n", port_num, port_num);

	if (xrnic_dev->port_status[port_num - 1] == XRNIC_PORT_QP_FREE ||
	    port_num < 1 || port_num > XRNIC_MAX_PORT_SUPPORT) {
		/*We need to validate that.*/
		pr_err("PORT number is not correct sending rej.\n");
		reason = XRNIC_REJ_PRIM_LID_PORT_NOT_EXIST;
		msg_rej = XRNIC_REJ_REQ;
		goto send_rep_rej;
	}

	xrnic_create_child_cm(xrnic_dev->cm_id_info[port_num - 1]);
	child_qp_num =
	    xrnic_dev->cm_id_info[port_num - 1]->parent_cm_id.child_qp_num++;
	child_cm_id = xrnic_dev->cm_id_info[port_num - 1]->child_cm_id;
	parent_cm_id = &xrnic_dev->cm_id_info[port_num - 1]->parent_cm_id;
	child_cm_id->cm_id_info = xrnic_dev->cm_id_info[port_num - 1];
	child_cm_id->cm_context = parent_cm_id->cm_context;
	child_cm_id->ps = parent_cm_id->ps;
	child_cm_id->xrnic_cm_handler = parent_cm_id->xrnic_cm_handler;
	child_cm_id->local_cm_id = qp_attr->local_cm_id;
	child_cm_id->port_num = port_num;
	child_cm_id->child_qp_num = child_qp_num + 1;
	child_cm_id->qp_info.qp_num = qp_attr->qp_num;
	child_cm_id->qp_status = XRNIC_PORT_QP_FREE;
	child_cm_id_info = child_cm_id->cm_id_info;
	child_cm_id_info->conn_event_info.cm_event = XRNIC_REQ_RCVD;
	child_cm_id_info->conn_event_info.status = 0;
	child_cm_id_info->conn_event_info.private_data = (void *)temp;
	child_cm_id_info->conn_event_info.private_data_len = 32;
	list_add_tail(&child_cm_id->list, &cm_id_list);
	status = parent_cm_id->xrnic_cm_handler(child_cm_id,
					&child_cm_id_info->conn_event_info);
	if (status) {
		pr_err("xrnic_cm_handler failed sending rej.\n");
		reason = XRNIC_REJ_CONSUMER_REJECT;
		msg_rej = XRNIC_REJ_REQ;
		goto send_rep_rej;
	}

	qp_attr->remote_cm_id = req->local_cm_id;
	qp_attr->cm_id = child_cm_id;

	if (qp_attr->ip_addr_type == AF_INET) {
		qp_attr->ipv4_addr = recv_qp_pkt_ipv4->ipv4.src_addr;
		memcpy(&qp_attr->mac_addr,
		       &recv_qp_pkt_ipv4->eth.h_source, XRNIC_ETH_ALEN);
		qp_attr->source_qp_num = recv_qp_pkt_ipv4->deth.src_qp;
	} else {
		memcpy(&qp_attr->ipv6_addr,
		       &recv_qp_pkt_ipv6->ipv6.saddr,
		       sizeof(struct in6_addr));
		memcpy(&qp_attr->mac_addr,
		       &recv_qp_pkt_ipv6->eth.h_source, XRNIC_ETH_ALEN);
		qp_attr->source_qp_num = recv_qp_pkt_ipv6->deth.src_qp;
	}

	xrnic_cm_prepare_rep(qp_attr, rq_buf);
	xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
			       qp_attr->qp1_attr, qp1_send_pkt_size);

	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_REP_SENT;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	qp_attr->qp_timer.expires =  jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
		(1 << XRNIC_CM_TIMER_TIMEOUT));
	add_timer(&qp_attr->qp_timer);
	DEBUG_LOG("Exiting %s\n", __func__);
	return;
send_rep_rej:

	qp_attr->remote_cm_id = req->local_cm_id;

	xrnic_cm_prepare_rej(qp_attr, msg_rej, reason);
	/* Reject code added end */
	xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
			       qp_attr->qp1_attr, qp1_send_pkt_size);
	xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp, qp_attr->qp1_attr,
			       qp1_send_pkt_size);

	qp_attr->resend_count = 0;
	qp_attr->curr_state = XRNIC_REJ_SENT;
	if (timer_pending(&qp_attr->qp_timer))
		del_timer_sync(&qp_attr->qp_timer);
	qp_attr->qp_timer.expires =  jiffies +
		usecs_to_jiffies(XRNIC_CM_TIMEOUT *
		(1 << XRNIC_CM_TIMER_TIMEOUT));
	add_timer(&qp_attr->qp_timer);
	DEBUG_LOG("Exiting %s with reject reason [%d]\n", __func__, reason);
}

/**
 * fill_cm_rtu_data() - Fills rtu data to send rtu packet.
 * @cm_id : CM ID
 * @send_sgl_qp1 : data pointer
 * @cm_req_size : total header size
 * @return: send_sgl_qp1 data pointer
 */
static char *fill_cm_rtu_data(struct xrnic_rdma_cm_id *cm_id,
			      char *send_sgl_qp1, int cm_req_size)
{
	struct cma_rtu *rtu_data;

	SET_CM_HDR(send_sgl_qp1);
	rtu_data = (struct cma_rtu *)send_sgl_qp1;
	memset(rtu_data, 0, sizeof(*rtu_data));
	rtu_data->local_comm_id = cm_id->local_cm_id;
	rtu_data->remote_comm_id = cm_id->remote_cm_id;
	return send_sgl_qp1;
}

/**
 * fill_cm_req_data() - Fills request data to send in request packet.
 * @cm_id : CM ID
 * @send_sgl_qp1 : data pointer
 * @cm_req_size : total header size
 * @return: send_sgl_qp1 data pointer
 */
static char *fill_cm_req_data(struct xrnic_rdma_cm_id *cm_id,
			      char *send_sgl_qp1, int cm_req_size)
{
	struct ernic_cm_req *cm_req;
	struct cma_hdr data;
	int val;
	int sgid, dgid;
	unsigned int psn;
	struct sockaddr_in *sin4, *din4;

	sin4 = (struct sockaddr_in *)&cm_id->route.s_addr;
	din4 = (struct sockaddr_in *)&cm_id->route.d_addr;

	SET_CM_HDR(send_sgl_qp1);
	cm_req = (struct ernic_cm_req *)send_sgl_qp1;
	memset(cm_req, 0, sizeof(*cm_req));

	cm_req->local_comm_id = cpu_to_be32(cm_id->local_cm_id);
	cm_req->service_id = cpu_to_be64((cm_id->ps << 16) |
						be16_to_cpu(din4->sin_port));
	ether_addr_copy(&cm_req->local_ca_guid, &cm_id->route.smac);
	cm_req->local_qkey = 0;
	cm_req->offset32 = cpu_to_be32((cm_id->local_cm_id << 8) |
					cm_id->conn_param.responder_resources);
	cm_req->offset36 = cpu_to_be32 (cm_id->conn_param.initiator_depth);

	val = (XRNIC_REQ_LOCAL_CM_RESP_TOUT | (XRNIC_SVC_TYPE_UC << 5) |
		(cm_id->conn_param.flow_control << 7));
	cm_req->offset40 = cpu_to_be32(val);
	get_random_bytes(&psn, 24);
	psn &= 0xFFFFFF;
	val = ((psn << 8) | XRNIC_REQ_REMOTE_CM_RESP_TOUT |
		(cm_id->conn_param.retry_count << 5));
	cm_req->offset44 = cpu_to_be32(val);
	cm_id->qp_info.starting_psn = psn;

	cm_req->pkey = 0xFFFF;
	cm_req->offset50 = ((1 << 4) |
			    (cm_id->conn_param.rnr_retry_count << 5));
	cm_req->offset51 = (1 << 4);
	cm_req->local_lid = cpu_to_be16(0xFFFF);
	cm_req->remote_lid = cpu_to_be16(0xFFFF);
	sgid = sin4->sin_addr.s_addr;
	dgid = din4->sin_addr.s_addr;
	val = cpu_to_be32(0xFFFF);
	memcpy(cm_req->local_gid.raw + 8,  &val, 4);
	memcpy(cm_req->local_gid.raw + 12,  &sgid, 4);
	memcpy(cm_req->remote_gid.raw + 8,  &val, 4);
	memcpy(cm_req->remote_gid.raw + 12,  &dgid, 4);
	cm_req->offset88 = cpu_to_be32(1 << 2);
	cm_req->traffic_class = 0;
	cm_req->hop_limit = 0x40;
	cm_req->offset94 = 0;
	cm_req->offset95 = 0x18;

	data.cma_version = CMA_VERSION;
	data.ip_version = (4 << 4);
	data.port = din4->sin_port;
	data.src_addr.ip4.addr = sin4->sin_addr.s_addr;
	data.dst_addr.ip4.addr = din4->sin_addr.s_addr;
	memcpy(cm_req->private_data, &data, sizeof(data));

	return send_sgl_qp1;
}

/**
 * fill_ipv4_cm_req() - fills cm request data for rdma connect.
 * @cm_id : CM ID
 * @send_sgl_qp1 : data pointer
 * @cm_req_size : total header size
 */
void fill_ipv4_cm_req(struct xrnic_rdma_cm_id *cm_id,
		      char *send_sgl_qp1, int cm_req_size)
{
	send_sgl_qp1 = fill_ipv4_headers(cm_id, send_sgl_qp1, cm_req_size);
	send_sgl_qp1 = fill_mad_common_header(cm_id, send_sgl_qp1,
					      cm_req_size, CM_REQ_ATTR_ID);
	send_sgl_qp1 = fill_cm_req_data(cm_id, send_sgl_qp1, cm_req_size);
}

/**
 * xrnic_cm_send_rtu() - Sends Ready to use packet.
 * @cm_id :  CM ID
 * @cm_rep : IPV4 mad data
 */
static void xrnic_cm_send_rtu(struct xrnic_rdma_cm_id *cm_id,
			      struct rep *cm_rep)
{
	int cm_req_size;
	char *send_sgl_qp1, *head;

	cm_req_size = sizeof(struct ethhdr) + sizeof(struct iphdr) +
			sizeof(struct udphdr) + IB_BTH_BYTES + IB_DETH_BYTES +
			sizeof(struct ib_mad_hdr) + sizeof(struct cma_rtu) +
			EXTRA_PKT_LEN;

	head = kmalloc(cm_req_size, GFP_ATOMIC);
	send_sgl_qp1 = head;
	send_sgl_qp1 = fill_ipv4_headers(cm_id, send_sgl_qp1, cm_req_size);
	send_sgl_qp1 = fill_mad_common_header(cm_id, send_sgl_qp1,
					      cm_req_size, CM_RTU_ATTR_ID);
	send_sgl_qp1 = fill_cm_rtu_data(cm_id, send_sgl_qp1, cm_req_size);
	xrnic_send_mad(head, cm_req_size - EXTRA_PKT_LEN);
}

/*
 * xrnic_rdma_accept() - This function implements incoming connect request.
 * accept functionality
 * @cm_id : CM ID of the incoming connect request
 * @conn_param : Connection parameters
 * @return: XRNIC_SUCCESS if successfully accepts the connection,
 *		otherwise error representative value
 */
int xrnic_rdma_accept(struct xrnic_rdma_cm_id *cm_id,
		      struct xrnic_rdma_conn_param *conn_param)
{
	struct xrnic_qp_info   *qp_info;

	if (xrnic_dev->port_status[cm_id->port_num - 1] !=
	    XRNIC_PORT_QP_IN_USE)
		return -XRNIC_INVALID_CM_ID;

	if (cm_id->qp_status == XRNIC_PORT_QP_IN_USE)
		return -XRNIC_INVALID_QP_ID;

	qp_info = &cm_id->qp_info;
	if (qp_info->qp_num < 2 ||
	    (qp_info->qp_num > XRNIC_MAX_QP_SUPPORT + 2))
		return -XRNIC_INVALID_QP_ID;

	if (qp_info->sq_depth > XRNIC_MAX_SQ_DEPTH ||
	    qp_info->rq_depth > XRNIC_MAX_RQ_DEPTH ||
	    qp_info->send_sge_size > XRNIC_MAX_SEND_SGL_SIZE ||
	    qp_info->send_pkt_size > XRNIC_MAX_SEND_PKT_SIZE)
		return -XRNIC_INVALID_QP_INIT_ATTR;

	/*Return Error if wrong conn_param is coming.*/
	if (conn_param->private_data_len > XRNIC_CM_PRVATE_DATA_LENGTH ||
	    conn_param->responder_resources > XRNIC_RESPONDER_RESOURCES ||
	    conn_param->initiator_depth > XRNIC_INITIATOR_DEPTH ||
	    conn_param->flow_control > 1 ||
	    conn_param->retry_count > XRNIC_REQ_RETRY_COUNT ||
	    conn_param->rnr_retry_count > XRNIC_REP_RNR_RETRY_COUNT)
		return -XRNIC_INVALID_QP_CONN_PARAM;

	memcpy((void *)&cm_id->conn_param.private_data,
	       (void *)&conn_param->private_data,
	       conn_param->private_data_len);
	cm_id->conn_param.private_data_len = conn_param->private_data_len;
	cm_id->conn_param.responder_resources =
					conn_param->responder_resources;
	cm_id->conn_param.initiator_depth = conn_param->initiator_depth;
	cm_id->conn_param.flow_control = conn_param->flow_control;
	cm_id->conn_param.retry_count = conn_param->retry_count;
	cm_id->conn_param.rnr_retry_count = conn_param->rnr_retry_count;

	xrnic_qp_app_configuration(qp_info->qp_num, XRNIC_HW_QP_ENABLE);

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_accept);

/*
 * xrnic_rdma_disconnect() - This function implements RDMA disconnect.
 * @cm_id : CM ID to destroy or disconnect
 * @return: XRNIC_SUCCESS if successfully disconnects
 * otherwise error representative value
 */
int xrnic_rdma_disconnect(struct xrnic_rdma_cm_id *cm_id)
{
	struct xrnic_rdma_cm_id_info *cm_id_info;
	int i;

	if (xrnic_dev->port_status[cm_id->port_num - 1]) {
		if (cm_id->local_cm_id >= 2) {
			if (cm_id->child_qp_num < 1)
				return -XRNIC_INVALID_CM_ID;

			if (cm_id->qp_info.qp_num) {
				pr_err("CM ID of QP is not destroyed\n");
				return -XRNIC_INVALID_CM_ID;
			}
			if (cm_id->qp_status == XRNIC_PORT_QP_FREE) {
				pr_err("CM ID is already destroyed\n");
				return -XRNIC_INVALID_CM_ID;
			}
			pr_info("Free local cm id[%d] ", cm_id->local_cm_id);
			pr_info("Child qp number [%d] ", cm_id->child_qp_num);
			pr_info("qp_num [%d]\n", cm_id->qp_info.qp_num);
			cm_id->qp_status = XRNIC_PORT_QP_FREE;
		} else if (cm_id->local_cm_id == 1) {
			if (cm_id->qp_status == XRNIC_PORT_QP_FREE) {
				pr_err("CM ID is already destroyed\n");
				return -XRNIC_INVALID_CM_ID;
			}
			cm_id_info = (struct xrnic_rdma_cm_id_info *)
							cm_id->cm_id_info;
			for (i = 0; i < cm_id_info->num_child; i++) {
				if (cm_id_info->child_cm_id[i].qp_status ==
							XRNIC_PORT_QP_IN_USE){
					pr_err("child CM IDs not destroyed\n");
					return -XRNIC_INVALID_CM_ID;
				}
			}
			cm_id->qp_status = XRNIC_PORT_QP_FREE;
		} else {
			pr_err("Received invalid CM ID\n");
			return -XRNIC_INVALID_CM_ID;
		}
	} else {
		pr_err("Received invalid Port ID\n");
		return -XRNIC_INVALID_CM_ID;
	}

	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_disconnect);

/*
 * xrnic_rdma_destroy_id() - Function destroys CM ID of the channel.
 * @cm_id : CM ID of the incoming connect request
 * @flag : Flag to indicate disconnect send
 * @return: XRNIC_SUCCESS if successfully,
 * otherwise error representative value
 */
int xrnic_rdma_destroy_id(struct xrnic_rdma_cm_id *cm_id, int flag)
{
	struct xrnic_rdma_cm_id_info *cm_id_info;
	int i;
	u32 local_cm_id = cm_id->local_cm_id;

	if (xrnic_dev->port_status[cm_id->port_num - 1]) {
		if (local_cm_id >= 2) {
			if (cm_id->child_qp_num < 1)
				return -XRNIC_INVALID_CM_ID;
			if (cm_id->qp_status == XRNIC_PORT_QP_IN_USE) {
				pr_err("CM ID is not destroyed\n");
				return -XRNIC_INVALID_CM_ID;
			}
			if (flag)
				xrnic_cm_disconnect_send_handler
					(&xrnic_dev->qp_attr[local_cm_id - 2]);

			pr_info("Free local cm id[%d] ", cm_id->local_cm_id);
			pr_info("Child qp number [%d] ", cm_id->child_qp_num);
			pr_info("qp_num [%d]\n", cm_id->qp_info.qp_num);

			cm_id_info =
				xrnic_dev->cm_id_info[cm_id->port_num - 1];
			cm_id_info->parent_cm_id.child_qp_num--;
			__list_del_entry(&cm_id->list);
			kfree(cm_id);
		} else if (local_cm_id == 1) {
			if (cm_id->qp_status == XRNIC_PORT_QP_IN_USE) {
				pr_err("CM ID is already destroyed\n");
				return -XRNIC_INVALID_CM_ID;
			}

			cm_id_info = (struct xrnic_rdma_cm_id_info *)
							cm_id->cm_id_info;
			for (i = 0; i < cm_id_info->num_child; i++) {
				if (cm_id_info->child_cm_id[i].qp_status ==
							XRNIC_PORT_QP_IN_USE) {
					pr_err("child CM IDs not destroyed\n");
					return XRNIC_INVALID_CM_ID;
				}
			}
			xrnic_dev->io_qp_count = xrnic_dev->io_qp_count +
						cm_id_info->num_child;
			xrnic_dev->cm_id_info[cm_id->port_num - 1] = NULL;
			xrnic_dev->port_status[cm_id->port_num - 1] =
						XRNIC_PORT_QP_FREE;
			__list_del_entry(&cm_id->list);
			kfree(cm_id_info->child_cm_id);
			kfree(cm_id_info);
		} else {
			pr_err("Received invalid CM ID\n");
			return -XRNIC_INVALID_CM_ID;
		}
	} else {
		return -XRNIC_INVALID_CM_ID;
	}
	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_destroy_id);

/*
 * xrnic_send_mad() - This function initiates sending a management packet on
 *			QP1.
 * @send_buf : Input buffer to fill
 * @size : Size of the send buffer
 */
void xrnic_send_mad(void *send_buf, u32 size)
{
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;

	xrnic_qp1_send_mad_pkt(send_buf, qp1_attr, size);
}
EXPORT_SYMBOL(xrnic_send_mad);

/*
 * xrnic_identify_remote_host () - This function searches internal data.
 * structures for remote info
 * @rq_buf : received data buffer from other end
 * @qp_num : QP number on which packet has been received
 * @return: XRNIC_SUCCESS if remote end info is available,
 *		XRNIC_FAILED otherwise
 */
int xrnic_identify_remote_host(void *rq_buf, int qp_num)
{
	/* First find our Which IP version came from IPV packet and accrdingly
	 * Compare IP address from eiither AF_INET or AF_INET6.
	 */
	/* It may be two condition of failure, either we just bypass this
	 * CONNECT_REQUEST as we have alrady there or there
	 * is no QP free at all.
	 */
	struct mad *mad;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;

	if (qp1_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
	}

	if (htons(mad->attribute_id) == CONNECT_REQUEST) {
		if (qp1_attr->ip_addr_type == AF_INET6) {
			if (mad->data[0] ==
			    xrnic_dev->qp_attr[qp_num].remote_cm_id &&
			    xrnic_dev->qp1_attr.source_qp_num ==
			    xrnic_dev->qp_attr[qp_num].source_qp_num &&
			    (strcmp(xrnic_dev->qp1_attr.mac_addr,
				    xrnic_dev->qp_attr[qp_num].mac_addr)
				    == 0) &&
			    (!memcmp(&xrnic_dev->qp1_attr.ipv6_addr,
				     &xrnic_dev->qp_attr[qp_num].ipv6_addr,
			     sizeof(struct in6_addr))))
				return XRNIC_SUCCESS;
		} else {
			if (mad->data[0] ==
			    xrnic_dev->qp_attr[qp_num].remote_cm_id &&
			    xrnic_dev->qp1_attr.source_qp_num ==
			    xrnic_dev->qp_attr[qp_num].source_qp_num &&
			    (strcmp(xrnic_dev->qp1_attr.mac_addr,
				    xrnic_dev->qp_attr[qp_num].mac_addr)
				    == 0) &&
			    xrnic_dev->qp1_attr.ipv4_addr ==
			     xrnic_dev->qp_attr[qp_num].ipv4_addr)
				return XRNIC_SUCCESS;
		}
	} else {
		/* Need to Compare udp->source_port,ethernet->source_mac,
		 * ip->source_ip, deth->source_qp == 1, local_cm_id is le
		 */

		if (qp1_attr->ip_addr_type == AF_INET6) {
			if (mad->data[0] ==
			    xrnic_dev->qp_attr[qp_num].remote_cm_id &&
			    mad->data[1] ==
			    xrnic_dev->qp_attr[qp_num].local_cm_id &&
			    xrnic_dev->qp1_attr.source_qp_num ==
			    xrnic_dev->qp_attr[qp_num].source_qp_num &&
			    (strcmp(xrnic_dev->qp1_attr.mac_addr,
				    xrnic_dev->qp_attr[qp_num].mac_addr)
				    == 0) &&
			    (!memcmp(&xrnic_dev->qp1_attr.ipv6_addr,
			     &xrnic_dev->qp_attr[qp_num].ipv6_addr,
			     sizeof(struct in6_addr))))

				return XRNIC_SUCCESS;
		} else {
			if (mad->data[0] ==
			    xrnic_dev->qp_attr[qp_num].remote_cm_id &&
			    mad->data[1] ==
			    xrnic_dev->qp_attr[qp_num].local_cm_id &&
			    xrnic_dev->qp1_attr.source_qp_num ==
			    xrnic_dev->qp_attr[qp_num].source_qp_num &&
			    (strcmp(xrnic_dev->qp1_attr.mac_addr,
				    xrnic_dev->qp_attr[qp_num].mac_addr)
				    == 0) &&
			    xrnic_dev->qp1_attr.ipv4_addr ==
			    xrnic_dev->qp_attr[qp_num].ipv4_addr)

				return XRNIC_SUCCESS;
		}
	}
	return XRNIC_FAILED;
}

/*
 * xrnic_rdma_resolve_addr() - This function looks for a destination.
 * address and initiates ARP if required
 * @cm_id : CM channel ID which is being used for connection set up
 * @src_addr : IPV4/IPV6 address of the source
 * @dst_addr : IPV4/IPV6 address of the destination
 * @timeout : Address resolve timeout
 * @return: SUCCESS value if route resolved or error representative value
 * otherwise
 */
int xrnic_rdma_resolve_addr(struct xrnic_rdma_cm_id *cm_id,
			    struct sockaddr *src_addr,
			struct sockaddr *dst_addr, int timeout)
{
	struct flowi4 fl4;
	struct rtable *rt;
	struct neighbour *n;
	int arp_retry = 3;
	int ret = 0;
	struct sockaddr_in sin4, *din4;
	struct net_device *net_dev;
	struct xrnic_rdma_cm_event_info event;

	net_dev = dev_get_by_name(&init_net, "eth0");
	memset(&fl4, 0, sizeof(fl4));
	din4 = (struct sockaddr_in *)dst_addr;
	fl4.daddr = din4->sin_addr.s_addr;
	rt = ip_route_output_key(&init_net, &fl4);
	if (IS_ERR(rt)) {
		event.cm_event = XRNIC_CM_EVENT_ADDR_ERROR;
		event.status = PTR_ERR(rt);
		cm_id->xrnic_cm_handler(cm_id, &event);
		ret = PTR_ERR(rt);
		goto err;
	}

	event.cm_event = XRNIC_CM_EVENT_ADDR_RESOLVED;
	event.status = 0;
	cm_id->xrnic_cm_handler(cm_id, &event);

	sin4.sin_addr.s_addr = fl4.saddr;
	sin4.sin_port = cpu_to_be16(ERNIC_UDP_SRC_PORT);
	sin4.sin_family = dst_addr->sa_family;

	/* HACK: ARP is not resolved for the first time, retries are needed */
	do {
		n = rt->dst.ops->neigh_lookup(&rt->dst, NULL, &fl4.daddr);
	} while (arp_retry-- > 0);

	if (IS_ERR(n))
		pr_info("ERNIC neigh lookup failed\n");

	memcpy(&cm_id->route.s_addr, &sin4, sizeof(sin4));
	memcpy(&cm_id->route.d_addr, dst_addr, sizeof(*dst_addr));
	ether_addr_copy(cm_id->route.smac, net_dev->dev_addr);
	ether_addr_copy(cm_id->route.dmac, n->ha);
	event.cm_event = XRNIC_CM_EVENT_ROUTE_RESOLVED;
	event.status = 0;
	cm_id->xrnic_cm_handler(cm_id, &event);
err:
	return ret;
}
EXPORT_SYMBOL(xrnic_rdma_resolve_addr);

/*
 * fill_ipv4_headers() - This function fills the IPV4 address for an
 *				outgoing packet.
 * @cm_id : CM ID info for addresses
 * @send_sgl_qp1 : SGL info
 * @cm_req_size : request size
 * @return: pointer to SGL info
 */
char *fill_ipv4_headers(struct xrnic_rdma_cm_id *cm_id,
			char *send_sgl_qp1, int cm_req_size)
{
	struct ethhdr *eth;
	struct iphdr *iph;
	struct udphdr *udph;
	struct sockaddr_in *sin4, *din4;

	sin4 = (struct sockaddr_in *)&cm_id->route.s_addr;
	din4 = (struct sockaddr_in *)&cm_id->route.d_addr;

	SET_ETH_HDR(send_sgl_qp1);
	eth = (struct ethhdr *)send_sgl_qp1;
	ether_addr_copy(&eth->h_dest, &cm_id->route.dmac);
	ether_addr_copy(&eth->h_source, &cm_id->route.smac);
	eth->h_proto = cpu_to_be16(ETH_P_IP);

	SET_IP_HDR(send_sgl_qp1);
	iph = (struct iphdr *)send_sgl_qp1;
	iph->ihl = 5;
	iph->version = 4;
	iph->ttl = 32;
	iph->tos = 0;
	iph->protocol = IPPROTO_UDP;
	iph->saddr = sin4->sin_addr.s_addr;
	iph->daddr = din4->sin_addr.s_addr;
	iph->id = 0;
	iph->frag_off = cpu_to_be16(0x2 << 13);
	iph->tot_len = cpu_to_be16(cm_req_size - ETH_HLEN);

	ip_send_check(iph);

	SET_NET_HDR(send_sgl_qp1);
	udph = (struct udphdr *)send_sgl_qp1;
	udph->source = sin4->sin_port;
	udph->dest = din4->sin_port;
	udph->len = cpu_to_be16(cm_req_size - ETH_HLEN - (iph->ihl * 4));
	udph->check = 0;

	return send_sgl_qp1;
}

/*
 * fill_mad_common_header() - This function fills the MAD headers.
 * @cm_id : CM ID info
 * @send_sgl_qp1 : SGL info
 * @cm_req_size : request size
 * @cm_attr : cm attribute ID
 * @return: pointer to SGL info
 */
char *fill_mad_common_header(struct xrnic_rdma_cm_id *cm_id,
			     char *send_sgl_qp1, int cm_req_size,
			     int cm_attr)
{
	struct ib_bth  *bth;
	struct ib_deth *deth;
	struct ib_mad_hdr *madh;
	int val;

	SET_BTH_HDR(send_sgl_qp1);
	bth = (struct ib_bth *)send_sgl_qp1;
	memset(bth, 0, sizeof(*bth));
	val = (BTH_SET(OPCODE, IB_OPCODE_UD_SEND_ONLY) |
		BTH_SET(SE, XRNIC_SET_SOLICT_EVENT)     |
		BTH_SET(MIG, XRNIC_MIGRATION_REQ)	|
		BTH_SET(PAD, XRNIC_PAD_COUNT)		|
		BTH_SET(TVER, XRNIC_TRANSPORT_HDR_VER)	|
		BTH_SET(PKEY, 65535));
	bth->offset0 = cpu_to_be32(val);
	bth->offset4 = cpu_to_be32(BTH_SET(DEST_QP, 1));
	bth->offset8 = cpu_to_be32(BTH_SET(PSN, psn_num++));

	SET_DETH_HDR(send_sgl_qp1);
	deth = (struct ib_deth *)send_sgl_qp1;
	deth->offset0 = cpu_to_be32 (IB_ENFORCED_QEY);
	deth->offset4 = cpu_to_be32 (DETH_SET(SQP, 2));

	SET_MAD_HDR(send_sgl_qp1);
	madh = (struct ib_mad_hdr *)send_sgl_qp1;
	memset(madh, 0, sizeof(*madh));
	madh->base_version = IB_MGMT_BASE_VERSION;
	madh->mgmt_class = IB_MGMT_CLASS_CM;
	madh->class_version = IB_CM_CLASS_VER;
	madh->method = IB_MGMT_METHOD_SEND;
	madh->attr_id = cm_attr;
	madh->tid = cpu_to_be64(mad_tid++);
	madh->status = 0;
	madh->class_specific = 0;
	madh->attr_mod = 0;

	return send_sgl_qp1;
}

/*
 * xrnic_rdma_connect() - This function initiates connetion process.
 * @cm_id : CM ID info
 * @conn_param : Connection parameters for the new connection
 * @return: XRNIC_SUCCESS
 */
int xrnic_rdma_connect(struct xrnic_rdma_cm_id *cm_id,
		       struct xrnic_rdma_conn_param *conn_param)
{
	int cm_req_size;
	char *send_sgl_qp1, *head;

	cm_req_size =	sizeof(struct ethhdr) + sizeof(struct iphdr) +
			sizeof(struct udphdr) + IB_BTH_BYTES + IB_DETH_BYTES +
			sizeof(struct ib_mad_hdr) +
			sizeof(struct ernic_cm_req) + EXTRA_PKT_LEN;

	head = kmalloc(cm_req_size, GFP_ATOMIC);
	send_sgl_qp1 = head;
	memcpy(&cm_id->conn_param, conn_param, sizeof(*conn_param));
	fill_ipv4_cm_req(cm_id, send_sgl_qp1, cm_req_size);
	xrnic_send_mad(head, cm_req_size - EXTRA_PKT_LEN);
	return XRNIC_SUCCESS;
}
EXPORT_SYMBOL(xrnic_rdma_connect);

/*
 * xrnic_process_mad_pkt() - This function process a received MAD packet.
 * @rq_buf : receive queue pointer
 * @return: XRNIC_SUCCESS if successfully processed the MAD packet otherwise
 * XRNIC_FAILED
 */
static int xrnic_process_mad_pkt(void *rq_buf)
{
	int ret = 0;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct deth *deth;
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;

	if (qp1_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		deth = (struct deth *)&recv_qp_pkt_ipv4->deth;
		qp1_attr->ipv4_addr = recv_qp_pkt_ipv4->ipv4.src_addr;
		memcpy(&qp1_attr->mac_addr,
		       &recv_qp_pkt_ipv4->eth.h_source, XRNIC_ETH_ALEN);
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		deth = (struct deth *)&recv_qp_pkt_ipv6->deth;
		memcpy(&qp1_attr->ipv6_addr,
		       &recv_qp_pkt_ipv6->ipv6.saddr,
		       sizeof(struct in6_addr));
		memcpy(&qp1_attr->mac_addr,
		       &recv_qp_pkt_ipv6->eth.h_source,
		       XRNIC_ETH_ALEN);
	}
	qp1_attr->source_qp_num = deth->src_qp;

	ret = xrnic_cm_establishment_handler(rq_buf);
	if (ret) {
		pr_err("cm establishment failed with ret code %d\n", ret);
		return XRNIC_FAILED;
	}

	return XRNIC_SUCCESS;
}

/*
 * xrnic_mad_pkt_recv_intr_handler() - Interrupt handler for MAD packet
 * interrupt type
 * @data : XRNIC device info
 */
void xrnic_mad_pkt_recv_intr_handler(unsigned long data)
{
	struct xrnic_dev_info *xrnic_dev = (struct xrnic_dev_info *)data;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	struct xrnic_memory_map *xrnic_mmap = (struct xrnic_memory_map *)
			qp1_attr->xrnic_mmap;
	struct rdma_qp1_attr *rdma_qp1_attr = (struct rdma_qp1_attr *)
				&xrnic_mmap->xrnic_regs->rdma_qp1_attr;
	u32 config_value = 0;
	u8	rq_buf[XRNIC_RECV_PKT_SIZE];
	void *rq_buf_temp, *rq_buf_unaligned;
	int ret = 0, j, rq_pkt_num = 0, rq_pkt_count = 0;
	struct ethhdr_t *ethhdr;
	unsigned long flag;

	spin_lock_irqsave(&qp1_attr->qp_lock, flag);
	rq_buf_unaligned = (void *)rq_buf;

	/* We need to maintain sq_cmpl_db_local as per hardware update
	 * for Queue spesific sq_cmpl_db_local register
	 * Also in case of resend some packect we
	 * need to maintain this variable
	 */
	config_value = ioread32((char *)xrnic_mmap->rq_wrptr_db_add +
				(4 * (qp1_attr->qp_num - 1)));
	pr_info("config_value = %d, db_local = %d\n",
		config_value, qp1_attr->rq_wrptr_db_local);
	if (qp1_attr->rq_wrptr_db_local == config_value) {
		spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
		return;
	}

	if (qp1_attr->rq_wrptr_db_local > config_value)
		rq_pkt_count = (config_value + XRNIC_RQ_DEPTH) -
			qp1_attr->rq_wrptr_db_local;
	else
		rq_pkt_count = config_value - qp1_attr->rq_wrptr_db_local;

	DEBUG_LOG("rx pkt count = 0x%x\n", rq_pkt_count);
	for (j = 0 ; j < rq_pkt_count ; j++) {
		config_value = ioread32((char *)xrnic_mmap->sq_cmpl_db_add +
					(4 * (qp1_attr->qp_num - 1)));

		rq_pkt_num = qp1_attr->rq_wrptr_db_local;
		if (rq_pkt_num >= XRNIC_RQ_DEPTH)
			rq_pkt_num = rq_pkt_num - XRNIC_RQ_DEPTH;

		ethhdr = (struct ethhdr_t *)((char *)qp1_attr->rq_buf_ba_ca +
				(rq_pkt_num * XRNIC_RECV_PKT_SIZE));

		if (ethhdr->eth_type == htons(XRNIC_ETH_P_IP)) {
			rq_buf_temp = (char *)qp1_attr->rq_buf_ba_ca +
				(rq_pkt_num * XRNIC_RECV_PKT_SIZE);
			memcpy((char *)rq_buf_unaligned,
			       (char *)rq_buf_temp, XRNIC_RECV_PKT_SIZE);
			qp1_attr->ip_addr_type = AF_INET;
		} else {
			rq_buf_temp = (char *)qp1_attr->rq_buf_ba_ca +
					(rq_pkt_num * XRNIC_RECV_PKT_SIZE);
			memcpy((char *)rq_buf_unaligned,
			       (char *)rq_buf_temp, XRNIC_RECV_PKT_SIZE);
			qp1_attr->ip_addr_type = AF_INET6;
		}
		ret = xrnic_process_mad_pkt(rq_buf_unaligned);

		if (ret) {
			DEBUG_LOG("MAD pkt processing failed for pkt num %d\n",
				  rq_pkt_num);
		}

		qp1_attr->rq_wrptr_db_local = qp1_attr->rq_wrptr_db_local + 1;
		config_value = qp1_attr->rq_wrptr_db_local;
		iowrite32(config_value, ((void *)(&rdma_qp1_attr->rq_ci_db)));

		if (qp1_attr->rq_wrptr_db_local == XRNIC_RQ_DEPTH)
			qp1_attr->rq_wrptr_db_local = 0;
	}

	spin_unlock_irqrestore(&qp1_attr->qp_lock, flag);
}

/**
 * xrnic_cm_establishment_handler() - handles the state after the
 *					communication is established.
 * @rq_buf : receive queue buffer
 * @return: 0 on success, -1 incase of failure
 */
int xrnic_cm_establishment_handler(void *rq_buf)
{
	struct qp_cm_pkt_hdr_ipv4 *recv_qp_pkt_ipv4;
	struct qp_cm_pkt_hdr_ipv6 *recv_qp_pkt_ipv6;
	struct mad *mad;
	struct req *req;
	struct rep *rep;
	struct deth *deth;
	struct xrnic_qp_attr *qp_attr;
	int i = 0, ret;
	enum xrnic_rej_reason reason;
	enum xrnic_msg_rej msg;
	struct xrnic_qp_attr *qp1_attr = &xrnic_dev->qp1_attr;
	int	qp1_send_pkt_size;
	struct xrnic_rdma_cm_id *cm_id, *tmp;
	struct sockaddr_in *din4;

	DEBUG_LOG("Entering %s\n", __func__);

	if (qp1_attr->ip_addr_type == AF_INET) {
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv4->mad;
		req = (struct req *)&mad->data;
		qp1_send_pkt_size =  sizeof(struct qp_cm_pkt_hdr_ipv4);
	} else {
		recv_qp_pkt_ipv6 = (struct qp_cm_pkt_hdr_ipv6 *)rq_buf;
		mad = (struct mad *)&recv_qp_pkt_ipv6->mad;
		req = (struct req *)&mad->data;
		qp1_send_pkt_size = sizeof(struct qp_cm_pkt_hdr_ipv6);
	}
	switch (htons(mad->attribute_id)) {
	case CONNECT_REQUEST:
		DEBUG_LOG("Connect request recevied\n");
		for (i = 0 ; i < XRNIC_MAX_QP_SUPPORT ; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}

		if (i == XRNIC_MAX_QP_SUPPORT) {
			ret =  xrnic_find_free_qp();
			DEBUG_LOG("Q pair no:%x, i = %d\n", ret, i);
			if (ret < 0) {
				qp_attr = qp1_attr;
				qp_attr->ip_addr_type = qp1_attr->ip_addr_type;
				xrnic_prepare_initial_headers(qp_attr, rq_buf);
				pr_err("no QP is free for connection.\n");
				reason = XRNIC_REJ_NO_QP_AVAILABLE;
				msg = XRNIC_REJ_REQ;
				qp_attr->remote_cm_id = req->local_cm_id;
				xrnic_cm_prepare_rej(qp_attr, msg, reason);
				xrnic_qp1_send_mad_pkt(&qp_attr->send_sgl_temp,
						       qp_attr->qp1_attr,
							qp1_send_pkt_size);
				return XRNIC_FAILED;
			}
			i = ret;
		}

		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_LISTEN ||
		    qp_attr->curr_state == XRNIC_MRA_SENT ||
		    qp_attr->curr_state == XRNIC_REJ_SENT ||
		    qp_attr->curr_state == XRNIC_REP_SENT ||
		    qp_attr->curr_state == XRNIC_ESTABLISHD) {
			qp_attr->ip_addr_type = qp1_attr->ip_addr_type;
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_connect_request_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state for Connect Request\n");
			return XRNIC_FAILED;
		}
		break;

	case READY_TO_USE:
		DEBUG_LOG("RTU received\n");

		for (i = 0 ; i < XRNIC_MAX_QP_SUPPORT ; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}
		if (i == XRNIC_MAX_QP_SUPPORT) {
			pr_err("no QP is free for connection. in RTU\n");
			return XRNIC_FAILED;
		}
		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_REP_SENT ||
		    qp_attr->curr_state == XRNIC_MRA_RCVD) {
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_ready_to_use_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state to serve RTU\n");
			return XRNIC_FAILED;
		}
		break;

	case MSG_RSP_ACK:
		DEBUG_LOG("Message received Ack interrupt\n");
		for (i = 0 ; i < XRNIC_MAX_QP_SUPPORT ; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}

		if (i == XRNIC_MAX_QP_SUPPORT) {
			pr_err("no QP is free for connection\n");
			return XRNIC_FAILED;
		}
		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_REP_SENT) {
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_msg_rsp_ack_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state to serve MSG RSP ACK\n");
			return XRNIC_FAILED;
		}
		break;

	case CONNECT_REPLY:
		DEBUG_LOG("Connect reply received\n");
		recv_qp_pkt_ipv4 = (struct qp_cm_pkt_hdr_ipv4 *)rq_buf;
		rep = (struct rep *)&recv_qp_pkt_ipv4->mad.data;
		deth = (struct deth *)&recv_qp_pkt_ipv4->deth;
		list_for_each_entry_safe(cm_id, tmp, &cm_id_list, list) {
			if (cm_id->local_cm_id ==
			    be32_to_cpu(rep->remote_comm_id))
				break;
		}
		/* Something wrong if qp num is 0. Don't send Reply
		 * TODO: Send Reject instead of muting the Reply
		 */
		if (cm_id->qp_info.qp_num == 0)
			goto done;
		cm_id->local_cm_id = rep->remote_comm_id;
		cm_id->remote_cm_id = rep->local_cm_id;
		qp_attr = &xrnic_dev->qp_attr[(cm_id->qp_info.qp_num - 2)];
		qp_attr->local_cm_id = rep->remote_comm_id;
		qp_attr->remote_cm_id = rep->local_cm_id;
		qp_attr->remote_qp = (be32_to_cpu(rep->local_qpn) >> 8);
		qp_attr->source_qp_num = (deth->src_qp);
		qp_attr->starting_psn = (cm_id->qp_info.starting_psn - 1);
		qp_attr->rem_starting_psn = (rep->start_psn[2] |
					     rep->start_psn[1] << 8 |
					     rep->start_psn[0] << 16);
		ether_addr_copy(qp_attr->mac_addr, cm_id->route.dmac);
		din4 = &cm_id->route.d_addr;
		cm_id->port_num = be16_to_cpu(din4->sin_port);
		xrnic_dev->port_status[cm_id->port_num - 1] =
						XRNIC_PORT_QP_IN_USE;
		qp_attr->ipv4_addr = din4->sin_addr.s_addr;
		qp_attr->ip_addr_type = AF_INET;
		qp_attr->cm_id = cm_id;
		xrnic_qp_app_configuration(cm_id->qp_info.qp_num,
					   XRNIC_HW_QP_ENABLE);
		xrnic_cm_connect_rep_handler(qp_attr, NULL);
		xrnic_cm_send_rtu(cm_id, rep);
		qp_attr->curr_state = XRNIC_ESTABLISHD;
done:
		break;

	case CONNECT_REJECT:
		DEBUG_LOG("Connect Reject received\n");
		for (i = 0 ; i < XRNIC_MAX_QP_SUPPORT ; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}

		if (i == XRNIC_MAX_QP_SUPPORT) {
			pr_err("no QP is free for connection.\n");
			return XRNIC_FAILED;
		}
		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_MRA_SENT ||
		    qp_attr->curr_state == XRNIC_REP_SENT ||
		    qp_attr->curr_state == XRNIC_MRA_RCVD) {
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_connect_reject_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state to serve connect reject\n");
			return XRNIC_FAILED;
		}

		break;

	case DISCONNECT_REQUEST:
		DEBUG_LOG("Disconnect request received\n");
		for (i = 0; i < XRNIC_MAX_QP_SUPPORT; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}

		if (i == XRNIC_MAX_QP_SUPPORT) {
			pr_err("no QPis free for connection.\n");
			return XRNIC_FAILED;
		}
		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_ESTABLISHD ||
		    qp_attr->curr_state == XRNIC_DREQ_SENT ||
		    qp_attr->curr_state == XRNIC_TIMEWAIT) {
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_disconnect_request_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state to for Disconnect request\n");
			return XRNIC_FAILED;
		}
		break;

	case DISCONNECT_REPLY:
		DEBUG_LOG("Disconnect reply received\n");
		for (i = 0; i < XRNIC_MAX_QP_SUPPORT; i++) {
			if (!xrnic_identify_remote_host(rq_buf, i))
				break;
		}
		if (i == XRNIC_MAX_QP_SUPPORT) {
			pr_err("no QP is free for connection.\n");
			return XRNIC_FAILED;
		}
		qp_attr = &xrnic_dev->qp_attr[i];

		if (qp_attr->curr_state == XRNIC_DREQ_SENT) {
			xrnic_prepare_initial_headers(qp_attr, rq_buf);
			xrnic_cm_disconnect_reply_handler(qp_attr, rq_buf);
		} else {
			pr_err("Invalid QP state to for Disconnect reply\n");
			return XRNIC_FAILED;
		}
		break;

	case SERVICE_ID_RESOLUTION_REQ:
		DEBUG_LOG("Received service ID resolution request\n");
		pr_err("Not handling service ID resolution request\n");
		return XRNIC_FAILED;

	case SERVICE_ID_RESOLUTION_REQ_REPLY:
		DEBUG_LOG("Received service ID resolution reply\n");
		pr_err("Not handling service ID resolution reply\n");
		return XRNIC_FAILED;

	case LOAD_ALTERNATE_PATH:
		DEBUG_LOG("Received Load Alternate Path request\n");
		pr_err("Not handling Load Alternate Path request\n");
		return XRNIC_FAILED;

	case ALTERNATE_PATH_RESPONSE:
		DEBUG_LOG("Received LAP response\n");
		pr_err("Not handling LAP response\n");
		return XRNIC_FAILED;

	default:
		pr_err("default mad attribute 0x%x\n", mad->attribute_id);
		break;
	}

	DEBUG_LOG("Exiting %s\n", __func__);
	return XRNIC_SUCCESS;
}
