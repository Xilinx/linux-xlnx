/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_ETH_IF_H
#define _QED_ETH_IF_H

#include <linux/list.h>
#include <linux/if_link.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_iov_if.h>

struct qed_dev_eth_info {
	struct qed_dev_info common;

	u8	num_queues;
	u8	num_tc;

	u8	port_mac[ETH_ALEN];
	u8	num_vlan_filters;

	/* Legacy VF - this affects the datapath, so qede has to know */
	bool is_legacy;
};

struct qed_update_vport_rss_params {
	u16	rss_ind_table[128];
	u32	rss_key[10];
	u8	rss_caps;
};

struct qed_update_vport_params {
	u8 vport_id;
	u8 update_vport_active_flg;
	u8 vport_active_flg;
	u8 update_tx_switching_flg;
	u8 tx_switching_flg;
	u8 update_accept_any_vlan_flg;
	u8 accept_any_vlan;
	u8 update_rss_flg;
	struct qed_update_vport_rss_params rss_params;
};

struct qed_start_vport_params {
	bool remove_inner_vlan;
	bool gro_enable;
	bool drop_ttl0;
	u8 vport_id;
	u16 mtu;
	bool clear_stats;
};

struct qed_stop_rxq_params {
	u8 rss_id;
	u8 rx_queue_id;
	u8 vport_id;
	bool eq_completion_only;
};

struct qed_stop_txq_params {
	u8 rss_id;
	u8 tx_queue_id;
};

enum qed_filter_rx_mode_type {
	QED_FILTER_RX_MODE_TYPE_REGULAR,
	QED_FILTER_RX_MODE_TYPE_MULTI_PROMISC,
	QED_FILTER_RX_MODE_TYPE_PROMISC,
};

enum qed_filter_xcast_params_type {
	QED_FILTER_XCAST_TYPE_ADD,
	QED_FILTER_XCAST_TYPE_DEL,
	QED_FILTER_XCAST_TYPE_REPLACE,
};

struct qed_filter_ucast_params {
	enum qed_filter_xcast_params_type type;
	u8 vlan_valid;
	u16 vlan;
	u8 mac_valid;
	unsigned char mac[ETH_ALEN];
};

struct qed_filter_mcast_params {
	enum qed_filter_xcast_params_type type;
	u8 num;
	unsigned char mac[64][ETH_ALEN];
};

union qed_filter_type_params {
	enum qed_filter_rx_mode_type accept_flags;
	struct qed_filter_ucast_params ucast;
	struct qed_filter_mcast_params mcast;
};

enum qed_filter_type {
	QED_FILTER_TYPE_UCAST,
	QED_FILTER_TYPE_MCAST,
	QED_FILTER_TYPE_RX_MODE,
	QED_MAX_FILTER_TYPES,
};

struct qed_filter_params {
	enum qed_filter_type type;
	union qed_filter_type_params filter;
};

struct qed_queue_start_common_params {
	u8 rss_id;
	u8 queue_id;
	u8 vport_id;
	u16 sb;
	u16 sb_idx;
	u16 vf_qid;
};

struct qed_tunn_params {
	u16 vxlan_port;
	u8 update_vxlan_port;
	u16 geneve_port;
	u8 update_geneve_port;
};

struct qed_eth_cb_ops {
	struct qed_common_cb_ops common;
	void (*force_mac) (void *dev, u8 *mac);
};

#ifdef CONFIG_DCB
/* Prototype declaration of qed_eth_dcbnl_ops should match with the declaration
 * of dcbnl_rtnl_ops structure.
 */
struct qed_eth_dcbnl_ops {
	/* IEEE 802.1Qaz std */
	int (*ieee_getpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_setpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_getets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_setets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_peer_getets)(struct qed_dev *cdev, struct ieee_ets *ets);
	int (*ieee_peer_getpfc)(struct qed_dev *cdev, struct ieee_pfc *pfc);
	int (*ieee_getapp)(struct qed_dev *cdev, struct dcb_app *app);
	int (*ieee_setapp)(struct qed_dev *cdev, struct dcb_app *app);

	/* CEE std */
	u8 (*getstate)(struct qed_dev *cdev);
	u8 (*setstate)(struct qed_dev *cdev, u8 state);
	void (*getpgtccfgtx)(struct qed_dev *cdev, int prio, u8 *prio_type,
			     u8 *pgid, u8 *bw_pct, u8 *up_map);
	void (*getpgbwgcfgtx)(struct qed_dev *cdev, int pgid, u8 *bw_pct);
	void (*getpgtccfgrx)(struct qed_dev *cdev, int prio, u8 *prio_type,
			     u8 *pgid, u8 *bw_pct, u8 *up_map);
	void (*getpgbwgcfgrx)(struct qed_dev *cdev, int pgid, u8 *bw_pct);
	void (*getpfccfg)(struct qed_dev *cdev, int prio, u8 *setting);
	void (*setpfccfg)(struct qed_dev *cdev, int prio, u8 setting);
	u8 (*getcap)(struct qed_dev *cdev, int capid, u8 *cap);
	int (*getnumtcs)(struct qed_dev *cdev, int tcid, u8 *num);
	u8 (*getpfcstate)(struct qed_dev *cdev);
	int (*getapp)(struct qed_dev *cdev, u8 idtype, u16 id);
	u8 (*getfeatcfg)(struct qed_dev *cdev, int featid, u8 *flags);

	/* DCBX configuration */
	u8 (*getdcbx)(struct qed_dev *cdev);
	void (*setpgtccfgtx)(struct qed_dev *cdev, int prio,
			     u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map);
	void (*setpgtccfgrx)(struct qed_dev *cdev, int prio,
			     u8 pri_type, u8 pgid, u8 bw_pct, u8 up_map);
	void (*setpgbwgcfgtx)(struct qed_dev *cdev, int pgid, u8 bw_pct);
	void (*setpgbwgcfgrx)(struct qed_dev *cdev, int pgid, u8 bw_pct);
	u8 (*setall)(struct qed_dev *cdev);
	int (*setnumtcs)(struct qed_dev *cdev, int tcid, u8 num);
	void (*setpfcstate)(struct qed_dev *cdev, u8 state);
	int (*setapp)(struct qed_dev *cdev, u8 idtype, u16 idval, u8 up);
	u8 (*setdcbx)(struct qed_dev *cdev, u8 state);
	u8 (*setfeatcfg)(struct qed_dev *cdev, int featid, u8 flags);

	/* Peer apps */
	int (*peer_getappinfo)(struct qed_dev *cdev,
			       struct dcb_peer_app_info *info,
			       u16 *app_count);
	int (*peer_getapptable)(struct qed_dev *cdev, struct dcb_app *table);

	/* CEE peer */
	int (*cee_peer_getpfc)(struct qed_dev *cdev, struct cee_pfc *pfc);
	int (*cee_peer_getpg)(struct qed_dev *cdev, struct cee_pg *pg);
};
#endif

struct qed_eth_ops {
	const struct qed_common_ops *common;
#ifdef CONFIG_QED_SRIOV
	const struct qed_iov_hv_ops *iov;
#endif
#ifdef CONFIG_DCB
	const struct qed_eth_dcbnl_ops *dcb;
#endif

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_eth_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_eth_cb_ops *ops,
			     void *cookie);

	 bool(*check_mac) (struct qed_dev *cdev, u8 *mac);

	int (*vport_start)(struct qed_dev *cdev,
			   struct qed_start_vport_params *params);

	int (*vport_stop)(struct qed_dev *cdev,
			  u8 vport_id);

	int (*vport_update)(struct qed_dev *cdev,
			    struct qed_update_vport_params *params);

	int (*q_rx_start)(struct qed_dev *cdev,
			  struct qed_queue_start_common_params *params,
			  u16 bd_max_bytes,
			  dma_addr_t bd_chain_phys_addr,
			  dma_addr_t cqe_pbl_addr,
			  u16 cqe_pbl_size,
			  void __iomem **pp_prod);

	int (*q_rx_stop)(struct qed_dev *cdev,
			 struct qed_stop_rxq_params *params);

	int (*q_tx_start)(struct qed_dev *cdev,
			  struct qed_queue_start_common_params *params,
			  dma_addr_t pbl_addr,
			  u16 pbl_size,
			  void __iomem **pp_doorbell);

	int (*q_tx_stop)(struct qed_dev *cdev,
			 struct qed_stop_txq_params *params);

	int (*filter_config)(struct qed_dev *cdev,
			     struct qed_filter_params *params);

	int (*fastpath_stop)(struct qed_dev *cdev);

	int (*eth_cqe_completion)(struct qed_dev *cdev,
				  u8 rss_id,
				  struct eth_slow_path_rx_cqe *cqe);

	void (*get_vport_stats)(struct qed_dev *cdev,
				struct qed_eth_stats *stats);

	int (*tunn_config)(struct qed_dev *cdev,
			   struct qed_tunn_params *params);
};

const struct qed_eth_ops *qed_get_eth_ops(void);
void qed_put_eth_ops(void);

#endif
