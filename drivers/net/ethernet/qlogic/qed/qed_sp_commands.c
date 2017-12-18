/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include "qed.h"
#include <linux/qed/qed_chain.h>
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"

int qed_sp_init_request(struct qed_hwfn *p_hwfn,
			struct qed_spq_entry **pp_ent,
			u8 cmd, u8 protocol, struct qed_sp_init_data *p_data)
{
	u32 opaque_cid = p_data->opaque_fid << 16 | p_data->cid;
	struct qed_spq_entry *p_ent = NULL;
	int rc;

	if (!pp_ent)
		return -ENOMEM;

	rc = qed_spq_get_entry(p_hwfn, pp_ent);

	if (rc)
		return rc;

	p_ent = *pp_ent;

	p_ent->elem.hdr.cid		= cpu_to_le32(opaque_cid);
	p_ent->elem.hdr.cmd_id		= cmd;
	p_ent->elem.hdr.protocol_id	= protocol;

	p_ent->priority		= QED_SPQ_PRIORITY_NORMAL;
	p_ent->comp_mode	= p_data->comp_mode;
	p_ent->comp_done.done	= 0;

	switch (p_ent->comp_mode) {
	case QED_SPQ_MODE_EBLOCK:
		p_ent->comp_cb.cookie = &p_ent->comp_done;
		break;

	case QED_SPQ_MODE_BLOCK:
		if (!p_data->p_comp_data)
			return -EINVAL;

		p_ent->comp_cb.cookie = p_data->p_comp_data->cookie;
		break;

	case QED_SPQ_MODE_CB:
		if (!p_data->p_comp_data)
			p_ent->comp_cb.function = NULL;
		else
			p_ent->comp_cb = *p_data->p_comp_data;
		break;

	default:
		DP_NOTICE(p_hwfn, "Unknown SPQE completion mode %d\n",
			  p_ent->comp_mode);
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Initialized: CID %08x cmd %02x protocol %02x data_addr %lu comp_mode [%s]\n",
		   opaque_cid, cmd, protocol,
		   (unsigned long)&p_ent->ramrod,
		   D_TRINE(p_ent->comp_mode, QED_SPQ_MODE_EBLOCK,
			   QED_SPQ_MODE_BLOCK, "MODE_EBLOCK", "MODE_BLOCK",
			   "MODE_CB"));

	memset(&p_ent->ramrod, 0, sizeof(p_ent->ramrod));

	return 0;
}

static enum tunnel_clss qed_tunn_get_clss_type(u8 type)
{
	switch (type) {
	case QED_TUNN_CLSS_MAC_VLAN:
		return TUNNEL_CLSS_MAC_VLAN;
	case QED_TUNN_CLSS_MAC_VNI:
		return TUNNEL_CLSS_MAC_VNI;
	case QED_TUNN_CLSS_INNER_MAC_VLAN:
		return TUNNEL_CLSS_INNER_MAC_VLAN;
	case QED_TUNN_CLSS_INNER_MAC_VNI:
		return TUNNEL_CLSS_INNER_MAC_VNI;
	default:
		return TUNNEL_CLSS_MAC_VLAN;
	}
}

static void
qed_tunn_set_pf_fix_tunn_mode(struct qed_hwfn *p_hwfn,
			      struct qed_tunn_update_params *p_src,
			      struct pf_update_tunnel_config *p_tunn_cfg)
{
	unsigned long cached_tunn_mode = p_hwfn->cdev->tunn_mode;
	unsigned long update_mask = p_src->tunn_mode_update_mask;
	unsigned long tunn_mode = p_src->tunn_mode;
	unsigned long new_tunn_mode = 0;

	if (test_bit(QED_MODE_L2GRE_TUNN, &update_mask)) {
		if (test_bit(QED_MODE_L2GRE_TUNN, &tunn_mode))
			__set_bit(QED_MODE_L2GRE_TUNN, &new_tunn_mode);
	} else {
		if (test_bit(QED_MODE_L2GRE_TUNN, &cached_tunn_mode))
			__set_bit(QED_MODE_L2GRE_TUNN, &new_tunn_mode);
	}

	if (test_bit(QED_MODE_IPGRE_TUNN, &update_mask)) {
		if (test_bit(QED_MODE_IPGRE_TUNN, &tunn_mode))
			__set_bit(QED_MODE_IPGRE_TUNN, &new_tunn_mode);
	} else {
		if (test_bit(QED_MODE_IPGRE_TUNN, &cached_tunn_mode))
			__set_bit(QED_MODE_IPGRE_TUNN, &new_tunn_mode);
	}

	if (test_bit(QED_MODE_VXLAN_TUNN, &update_mask)) {
		if (test_bit(QED_MODE_VXLAN_TUNN, &tunn_mode))
			__set_bit(QED_MODE_VXLAN_TUNN, &new_tunn_mode);
	} else {
		if (test_bit(QED_MODE_VXLAN_TUNN, &cached_tunn_mode))
			__set_bit(QED_MODE_VXLAN_TUNN, &new_tunn_mode);
	}

	if (p_src->update_geneve_udp_port) {
		p_tunn_cfg->set_geneve_udp_port_flg = 1;
		p_tunn_cfg->geneve_udp_port =
				cpu_to_le16(p_src->geneve_udp_port);
	}

	if (test_bit(QED_MODE_L2GENEVE_TUNN, &update_mask)) {
		if (test_bit(QED_MODE_L2GENEVE_TUNN, &tunn_mode))
			__set_bit(QED_MODE_L2GENEVE_TUNN, &new_tunn_mode);
	} else {
		if (test_bit(QED_MODE_L2GENEVE_TUNN, &cached_tunn_mode))
			__set_bit(QED_MODE_L2GENEVE_TUNN, &new_tunn_mode);
	}

	if (test_bit(QED_MODE_IPGENEVE_TUNN, &update_mask)) {
		if (test_bit(QED_MODE_IPGENEVE_TUNN, &tunn_mode))
			__set_bit(QED_MODE_IPGENEVE_TUNN, &new_tunn_mode);
	} else {
		if (test_bit(QED_MODE_IPGENEVE_TUNN, &cached_tunn_mode))
			__set_bit(QED_MODE_IPGENEVE_TUNN, &new_tunn_mode);
	}

	p_src->tunn_mode = new_tunn_mode;
}

static void
qed_tunn_set_pf_update_params(struct qed_hwfn *p_hwfn,
			      struct qed_tunn_update_params *p_src,
			      struct pf_update_tunnel_config *p_tunn_cfg)
{
	unsigned long tunn_mode = p_src->tunn_mode;
	enum tunnel_clss type;

	qed_tunn_set_pf_fix_tunn_mode(p_hwfn, p_src, p_tunn_cfg);
	p_tunn_cfg->update_rx_pf_clss = p_src->update_rx_pf_clss;
	p_tunn_cfg->update_tx_pf_clss = p_src->update_tx_pf_clss;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_vxlan);
	p_tunn_cfg->tunnel_clss_vxlan  = type;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_l2gre);
	p_tunn_cfg->tunnel_clss_l2gre = type;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_ipgre);
	p_tunn_cfg->tunnel_clss_ipgre = type;

	if (p_src->update_vxlan_udp_port) {
		p_tunn_cfg->set_vxlan_udp_port_flg = 1;
		p_tunn_cfg->vxlan_udp_port = cpu_to_le16(p_src->vxlan_udp_port);
	}

	if (test_bit(QED_MODE_L2GRE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_l2gre = 1;

	if (test_bit(QED_MODE_IPGRE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_ipgre = 1;

	if (test_bit(QED_MODE_VXLAN_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_vxlan = 1;

	if (p_src->update_geneve_udp_port) {
		p_tunn_cfg->set_geneve_udp_port_flg = 1;
		p_tunn_cfg->geneve_udp_port =
				cpu_to_le16(p_src->geneve_udp_port);
	}

	if (test_bit(QED_MODE_L2GENEVE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_l2geneve = 1;

	if (test_bit(QED_MODE_IPGENEVE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_ipgeneve = 1;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_l2geneve);
	p_tunn_cfg->tunnel_clss_l2geneve = type;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_ipgeneve);
	p_tunn_cfg->tunnel_clss_ipgeneve = type;
}

static void qed_set_hw_tunn_mode(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 unsigned long tunn_mode)
{
	u8 l2gre_enable = 0, ipgre_enable = 0, vxlan_enable = 0;
	u8 l2geneve_enable = 0, ipgeneve_enable = 0;

	if (test_bit(QED_MODE_L2GRE_TUNN, &tunn_mode))
		l2gre_enable = 1;

	if (test_bit(QED_MODE_IPGRE_TUNN, &tunn_mode))
		ipgre_enable = 1;

	if (test_bit(QED_MODE_VXLAN_TUNN, &tunn_mode))
		vxlan_enable = 1;

	qed_set_gre_enable(p_hwfn, p_ptt, l2gre_enable, ipgre_enable);
	qed_set_vxlan_enable(p_hwfn, p_ptt, vxlan_enable);

	if (test_bit(QED_MODE_L2GENEVE_TUNN, &tunn_mode))
		l2geneve_enable = 1;

	if (test_bit(QED_MODE_IPGENEVE_TUNN, &tunn_mode))
		ipgeneve_enable = 1;

	qed_set_geneve_enable(p_hwfn, p_ptt, l2geneve_enable,
			      ipgeneve_enable);
}

static void
qed_tunn_set_pf_start_params(struct qed_hwfn *p_hwfn,
			     struct qed_tunn_start_params *p_src,
			     struct pf_start_tunnel_config *p_tunn_cfg)
{
	unsigned long tunn_mode;
	enum tunnel_clss type;

	if (!p_src)
		return;

	tunn_mode = p_src->tunn_mode;
	type = qed_tunn_get_clss_type(p_src->tunn_clss_vxlan);
	p_tunn_cfg->tunnel_clss_vxlan = type;
	type = qed_tunn_get_clss_type(p_src->tunn_clss_l2gre);
	p_tunn_cfg->tunnel_clss_l2gre = type;
	type = qed_tunn_get_clss_type(p_src->tunn_clss_ipgre);
	p_tunn_cfg->tunnel_clss_ipgre = type;

	if (p_src->update_vxlan_udp_port) {
		p_tunn_cfg->set_vxlan_udp_port_flg = 1;
		p_tunn_cfg->vxlan_udp_port = cpu_to_le16(p_src->vxlan_udp_port);
	}

	if (test_bit(QED_MODE_L2GRE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_l2gre = 1;

	if (test_bit(QED_MODE_IPGRE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_ipgre = 1;

	if (test_bit(QED_MODE_VXLAN_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_vxlan = 1;

	if (p_src->update_geneve_udp_port) {
		p_tunn_cfg->set_geneve_udp_port_flg = 1;
		p_tunn_cfg->geneve_udp_port =
				cpu_to_le16(p_src->geneve_udp_port);
	}

	if (test_bit(QED_MODE_L2GENEVE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_l2geneve = 1;

	if (test_bit(QED_MODE_IPGENEVE_TUNN, &tunn_mode))
		p_tunn_cfg->tx_enable_ipgeneve = 1;

	type = qed_tunn_get_clss_type(p_src->tunn_clss_l2geneve);
	p_tunn_cfg->tunnel_clss_l2geneve = type;
	type = qed_tunn_get_clss_type(p_src->tunn_clss_ipgeneve);
	p_tunn_cfg->tunnel_clss_ipgeneve = type;
}

int qed_sp_pf_start(struct qed_hwfn *p_hwfn,
		    struct qed_tunn_start_params *p_tunn,
		    enum qed_mf_mode mode, bool allow_npar_tx_switch)
{
	struct pf_start_ramrod_data *p_ramrod = NULL;
	u16 sb = qed_int_get_sp_sb_id(p_hwfn);
	u8 sb_index = p_hwfn->p_eq->eq_sb_index;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;
	u8 page_cnt;

	/* update initial eq producer */
	qed_eq_prod_update(p_hwfn,
			   qed_chain_get_prod_idx(&p_hwfn->p_eq->chain));

	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_START,
				 PROTOCOLID_COMMON, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.pf_start;

	p_ramrod->event_ring_sb_id	= cpu_to_le16(sb);
	p_ramrod->event_ring_sb_index	= sb_index;
	p_ramrod->path_id		= QED_PATH_ID(p_hwfn);
	p_ramrod->dont_log_ramrods	= 0;
	p_ramrod->log_type_mask		= cpu_to_le16(0xf);

	switch (mode) {
	case QED_MF_DEFAULT:
	case QED_MF_NPAR:
		p_ramrod->mf_mode = MF_NPAR;
		break;
	case QED_MF_OVLAN:
		p_ramrod->mf_mode = MF_OVLAN;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unsupported MF mode, init as DEFAULT\n");
		p_ramrod->mf_mode = MF_NPAR;
	}
	p_ramrod->outer_tag = p_hwfn->hw_info.ovlan;

	/* Place EQ address in RAMROD */
	DMA_REGPAIR_LE(p_ramrod->event_ring_pbl_addr,
		       p_hwfn->p_eq->chain.pbl.p_phys_table);
	page_cnt = (u8)qed_chain_get_page_cnt(&p_hwfn->p_eq->chain);
	p_ramrod->event_ring_num_pages = page_cnt;
	DMA_REGPAIR_LE(p_ramrod->consolid_q_pbl_addr,
		       p_hwfn->p_consq->chain.pbl.p_phys_table);

	qed_tunn_set_pf_start_params(p_hwfn, p_tunn, &p_ramrod->tunnel_config);

	if (IS_MF_SI(p_hwfn))
		p_ramrod->allow_npar_tx_switching = allow_npar_tx_switch;

	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH:
		p_ramrod->personality = PERSONALITY_ETH;
		break;
	case QED_PCI_ISCSI:
		p_ramrod->personality = PERSONALITY_ISCSI;
		break;
	case QED_PCI_ETH_ROCE:
		p_ramrod->personality = PERSONALITY_RDMA_AND_ETH;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unkown personality %d\n",
			  p_hwfn->hw_info.personality);
		p_ramrod->personality = PERSONALITY_ETH;
	}

	if (p_hwfn->cdev->p_iov_info) {
		struct qed_hw_sriov_info *p_iov = p_hwfn->cdev->p_iov_info;

		p_ramrod->base_vf_id = (u8) p_iov->first_vf_in_pf;
		p_ramrod->num_vfs = (u8) p_iov->total_vfs;
	}
	p_ramrod->hsi_fp_ver.major_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MAJOR;
	p_ramrod->hsi_fp_ver.minor_ver_arr[ETH_VER_KEY] = ETH_HSI_VER_MINOR;

	DP_VERBOSE(p_hwfn, QED_MSG_SPQ,
		   "Setting event_ring_sb [id %04x index %02x], outer_tag [%d]\n",
		   sb, sb_index, p_ramrod->outer_tag);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	if (p_tunn) {
		qed_set_hw_tunn_mode(p_hwfn, p_hwfn->p_main_ptt,
				     p_tunn->tunn_mode);
		p_hwfn->cdev->tunn_mode = p_tunn->tunn_mode;
	}

	return rc;
}

int qed_sp_pf_update(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_CB;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	qed_dcbx_set_pf_update_params(&p_hwfn->p_dcbx_info->results,
				      &p_ent->ramrod.pf_update);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

/* Set pf update ramrod command params */
int qed_sp_pf_update_tunn_cfg(struct qed_hwfn *p_hwfn,
			      struct qed_tunn_update_params *p_tunn,
			      enum spq_mode comp_mode,
			      struct qed_spq_comp_cb *p_comp_data)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_data;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_UPDATE, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	qed_tunn_set_pf_update_params(p_hwfn, p_tunn,
				      &p_ent->ramrod.pf_update.tunnel_config);

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	if (rc)
		return rc;

	if (p_tunn->update_vxlan_udp_port)
		qed_set_vxlan_dest_port(p_hwfn, p_hwfn->p_main_ptt,
					p_tunn->vxlan_udp_port);
	if (p_tunn->update_geneve_udp_port)
		qed_set_geneve_dest_port(p_hwfn, p_hwfn->p_main_ptt,
					 p_tunn->geneve_udp_port);

	qed_set_hw_tunn_mode(p_hwfn, p_hwfn->p_main_ptt, p_tunn->tunn_mode);
	p_hwfn->cdev->tunn_mode = p_tunn->tunn_mode;

	return rc;
}

int qed_sp_pf_stop(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_PF_STOP, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

int qed_sp_heartbeat_ramrod(struct qed_hwfn *p_hwfn)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = QED_SPQ_MODE_EBLOCK;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 COMMON_RAMROD_EMPTY, PROTOCOLID_COMMON,
				 &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}
