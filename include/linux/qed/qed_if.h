/* QLogic qed NIC Driver
 *
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#ifndef _QED_IF_H
#define _QED_IF_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/qed_chain.h>

enum dcbx_protocol_type {
	DCBX_PROTOCOL_ISCSI,
	DCBX_PROTOCOL_FCOE,
	DCBX_PROTOCOL_ROCE,
	DCBX_PROTOCOL_ROCE_V2,
	DCBX_PROTOCOL_ETH,
	DCBX_MAX_PROTOCOL_TYPE
};

#define QED_ROCE_PROTOCOL_INDEX (3)

#ifdef CONFIG_DCB
#define QED_LLDP_CHASSIS_ID_STAT_LEN 4
#define QED_LLDP_PORT_ID_STAT_LEN 4
#define QED_DCBX_MAX_APP_PROTOCOL 32
#define QED_MAX_PFC_PRIORITIES 8
#define QED_DCBX_DSCP_SIZE 64

struct qed_dcbx_lldp_remote {
	u32 peer_chassis_id[QED_LLDP_CHASSIS_ID_STAT_LEN];
	u32 peer_port_id[QED_LLDP_PORT_ID_STAT_LEN];
	bool enable_rx;
	bool enable_tx;
	u32 tx_interval;
	u32 max_credit;
};

struct qed_dcbx_lldp_local {
	u32 local_chassis_id[QED_LLDP_CHASSIS_ID_STAT_LEN];
	u32 local_port_id[QED_LLDP_PORT_ID_STAT_LEN];
};

struct qed_dcbx_app_prio {
	u8 roce;
	u8 roce_v2;
	u8 fcoe;
	u8 iscsi;
	u8 eth;
};

struct qed_dbcx_pfc_params {
	bool willing;
	bool enabled;
	u8 prio[QED_MAX_PFC_PRIORITIES];
	u8 max_tc;
};

enum qed_dcbx_sf_ieee_type {
	QED_DCBX_SF_IEEE_ETHTYPE,
	QED_DCBX_SF_IEEE_TCP_PORT,
	QED_DCBX_SF_IEEE_UDP_PORT,
	QED_DCBX_SF_IEEE_TCP_UDP_PORT
};

struct qed_app_entry {
	bool ethtype;
	enum qed_dcbx_sf_ieee_type sf_ieee;
	bool enabled;
	u8 prio;
	u16 proto_id;
	enum dcbx_protocol_type proto_type;
};

struct qed_dcbx_params {
	struct qed_app_entry app_entry[QED_DCBX_MAX_APP_PROTOCOL];
	u16 num_app_entries;
	bool app_willing;
	bool app_valid;
	bool app_error;
	bool ets_willing;
	bool ets_enabled;
	bool ets_cbs;
	bool valid;
	u8 ets_pri_tc_tbl[QED_MAX_PFC_PRIORITIES];
	u8 ets_tc_bw_tbl[QED_MAX_PFC_PRIORITIES];
	u8 ets_tc_tsa_tbl[QED_MAX_PFC_PRIORITIES];
	struct qed_dbcx_pfc_params pfc;
	u8 max_ets_tc;
};

struct qed_dcbx_admin_params {
	struct qed_dcbx_params params;
	bool valid;
};

struct qed_dcbx_remote_params {
	struct qed_dcbx_params params;
	bool valid;
};

struct qed_dcbx_operational_params {
	struct qed_dcbx_app_prio app_prio;
	struct qed_dcbx_params params;
	bool valid;
	bool enabled;
	bool ieee;
	bool cee;
	u32 err;
};

struct qed_dcbx_get {
	struct qed_dcbx_operational_params operational;
	struct qed_dcbx_lldp_remote lldp_remote;
	struct qed_dcbx_lldp_local lldp_local;
	struct qed_dcbx_remote_params remote;
	struct qed_dcbx_admin_params local;
};
#endif

enum qed_led_mode {
	QED_LED_MODE_OFF,
	QED_LED_MODE_ON,
	QED_LED_MODE_RESTORE
};

#define DIRECT_REG_WR(reg_addr, val) writel((u32)val, \
					    (void __iomem *)(reg_addr))

#define DIRECT_REG_RD(reg_addr) readl((void __iomem *)(reg_addr))

#define QED_COALESCE_MAX 0xFF
#define QED_DEFAULT_RX_USECS 12

/* forward */
struct qed_dev;

struct qed_eth_pf_params {
	/* The following parameters are used during HW-init
	 * and these parameters need to be passed as arguments
	 * to update_pf_params routine invoked before slowpath start
	 */
	u16 num_cons;
};

/* Most of the the parameters below are described in the FW iSCSI / TCP HSI */
struct qed_iscsi_pf_params {
	u64 glbl_q_params_addr;
	u64 bdq_pbl_base_addr[2];
	u32 max_cwnd;
	u16 cq_num_entries;
	u16 cmdq_num_entries;
	u16 dup_ack_threshold;
	u16 tx_sws_timer;
	u16 min_rto;
	u16 min_rto_rt;
	u16 max_rto;

	/* The following parameters are used during HW-init
	 * and these parameters need to be passed as arguments
	 * to update_pf_params routine invoked before slowpath start
	 */
	u16 num_cons;
	u16 num_tasks;

	/* The following parameters are used during protocol-init */
	u16 half_way_close_timeout;
	u16 bdq_xoff_threshold[2];
	u16 bdq_xon_threshold[2];
	u16 cmdq_xoff_threshold;
	u16 cmdq_xon_threshold;
	u16 rq_buffer_size;

	u8 num_sq_pages_in_ring;
	u8 num_r2tq_pages_in_ring;
	u8 num_uhq_pages_in_ring;
	u8 num_queues;
	u8 log_page_size;
	u8 rqe_log_size;
	u8 max_fin_rt;
	u8 gl_rq_pi;
	u8 gl_cmd_pi;
	u8 debug_mode;
	u8 ll2_ooo_queue_id;
	u8 ooo_enable;

	u8 is_target;
	u8 bdq_pbl_num_entries[2];
};

struct qed_rdma_pf_params {
	/* Supplied to QED during resource allocation (may affect the ILT and
	 * the doorbell BAR).
	 */
	u32 min_dpis;		/* number of requested DPIs */
	u32 num_mrs;		/* number of requested memory regions */
	u32 num_qps;		/* number of requested Queue Pairs */
	u32 num_srqs;		/* number of requested SRQ */
	u8 roce_edpm_mode;	/* see QED_ROCE_EDPM_MODE_ENABLE */
	u8 gl_pi;		/* protocol index */

	/* Will allocate rate limiters to be used with QPs */
	u8 enable_dcqcn;
};

struct qed_pf_params {
	struct qed_eth_pf_params eth_pf_params;
	struct qed_iscsi_pf_params iscsi_pf_params;
	struct qed_rdma_pf_params rdma_pf_params;
};

enum qed_int_mode {
	QED_INT_MODE_INTA,
	QED_INT_MODE_MSIX,
	QED_INT_MODE_MSI,
	QED_INT_MODE_POLL,
};

struct qed_sb_info {
	struct status_block	*sb_virt;
	dma_addr_t		sb_phys;
	u32			sb_ack; /* Last given ack */
	u16			igu_sb_id;
	void __iomem		*igu_addr;
	u8			flags;
#define QED_SB_INFO_INIT        0x1
#define QED_SB_INFO_SETUP       0x2

	struct qed_dev		*cdev;
};

struct qed_dev_info {
	unsigned long	pci_mem_start;
	unsigned long	pci_mem_end;
	unsigned int	pci_irq;
	u8		num_hwfns;

	u8		hw_mac[ETH_ALEN];
	bool		is_mf_default;

	/* FW version */
	u16		fw_major;
	u16		fw_minor;
	u16		fw_rev;
	u16		fw_eng;

	/* MFW version */
	u32		mfw_rev;

	u32		flash_size;
	u8		mf_mode;
	bool		tx_switching;
	bool		rdma_supported;
};

enum qed_sb_type {
	QED_SB_TYPE_L2_QUEUE,
	QED_SB_TYPE_CNQ,
};

enum qed_protocol {
	QED_PROTOCOL_ETH,
	QED_PROTOCOL_ISCSI,
};

enum qed_link_mode_bits {
	QED_LM_FIBRE_BIT = BIT(0),
	QED_LM_Autoneg_BIT = BIT(1),
	QED_LM_Asym_Pause_BIT = BIT(2),
	QED_LM_Pause_BIT = BIT(3),
	QED_LM_1000baseT_Half_BIT = BIT(4),
	QED_LM_1000baseT_Full_BIT = BIT(5),
	QED_LM_10000baseKR_Full_BIT = BIT(6),
	QED_LM_25000baseKR_Full_BIT = BIT(7),
	QED_LM_40000baseLR4_Full_BIT = BIT(8),
	QED_LM_50000baseKR2_Full_BIT = BIT(9),
	QED_LM_100000baseKR4_Full_BIT = BIT(10),
	QED_LM_COUNT = 11
};

struct qed_link_params {
	bool	link_up;

#define QED_LINK_OVERRIDE_SPEED_AUTONEG         BIT(0)
#define QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS      BIT(1)
#define QED_LINK_OVERRIDE_SPEED_FORCED_SPEED    BIT(2)
#define QED_LINK_OVERRIDE_PAUSE_CONFIG          BIT(3)
#define QED_LINK_OVERRIDE_LOOPBACK_MODE         BIT(4)
	u32	override_flags;
	bool	autoneg;
	u32	adv_speeds;
	u32	forced_speed;
#define QED_LINK_PAUSE_AUTONEG_ENABLE           BIT(0)
#define QED_LINK_PAUSE_RX_ENABLE                BIT(1)
#define QED_LINK_PAUSE_TX_ENABLE                BIT(2)
	u32	pause_config;
#define QED_LINK_LOOPBACK_NONE                  BIT(0)
#define QED_LINK_LOOPBACK_INT_PHY               BIT(1)
#define QED_LINK_LOOPBACK_EXT_PHY               BIT(2)
#define QED_LINK_LOOPBACK_EXT                   BIT(3)
#define QED_LINK_LOOPBACK_MAC                   BIT(4)
	u32	loopback_mode;
};

struct qed_link_output {
	bool	link_up;

	/* In QED_LM_* defs */
	u32	supported_caps;
	u32	advertised_caps;
	u32	lp_caps;

	u32	speed;                  /* In Mb/s */
	u8	duplex;                 /* In DUPLEX defs */
	u8	port;                   /* In PORT defs */
	bool	autoneg;
	u32	pause_config;
};

struct qed_probe_params {
	enum qed_protocol protocol;
	u32 dp_module;
	u8 dp_level;
	bool is_vf;
};

#define QED_DRV_VER_STR_SIZE 12
struct qed_slowpath_params {
	u32	int_mode;
	u8	drv_major;
	u8	drv_minor;
	u8	drv_rev;
	u8	drv_eng;
	u8	name[QED_DRV_VER_STR_SIZE];
};

#define ILT_PAGE_SIZE_TCFC 0x8000 /* 32KB */

struct qed_int_info {
	struct msix_entry	*msix;
	u8			msix_cnt;

	/* This should be updated by the protocol driver */
	u8			used_cnt;
};

struct qed_common_cb_ops {
	void	(*link_update)(void			*dev,
			       struct qed_link_output	*link);
};

struct qed_selftest_ops {
/**
 * @brief selftest_interrupt - Perform interrupt test
 *
 * @param cdev
 *
 * @return 0 on success, error otherwise.
 */
	int (*selftest_interrupt)(struct qed_dev *cdev);

/**
 * @brief selftest_memory - Perform memory test
 *
 * @param cdev
 *
 * @return 0 on success, error otherwise.
 */
	int (*selftest_memory)(struct qed_dev *cdev);

/**
 * @brief selftest_register - Perform register test
 *
 * @param cdev
 *
 * @return 0 on success, error otherwise.
 */
	int (*selftest_register)(struct qed_dev *cdev);

/**
 * @brief selftest_clock - Perform clock test
 *
 * @param cdev
 *
 * @return 0 on success, error otherwise.
 */
	int (*selftest_clock)(struct qed_dev *cdev);
};

struct qed_common_ops {
	struct qed_selftest_ops *selftest;

	struct qed_dev*	(*probe)(struct pci_dev *dev,
				 struct qed_probe_params *params);

	void		(*remove)(struct qed_dev *cdev);

	int		(*set_power_state)(struct qed_dev *cdev,
					   pci_power_t state);

	void		(*set_id)(struct qed_dev *cdev,
				  char name[],
				  char ver_str[]);

	/* Client drivers need to make this call before slowpath_start.
	 * PF params required for the call before slowpath_start is
	 * documented within the qed_pf_params structure definition.
	 */
	void		(*update_pf_params)(struct qed_dev *cdev,
					    struct qed_pf_params *params);
	int		(*slowpath_start)(struct qed_dev *cdev,
					  struct qed_slowpath_params *params);

	int		(*slowpath_stop)(struct qed_dev *cdev);

	/* Requests to use `cnt' interrupts for fastpath.
	 * upon success, returns number of interrupts allocated for fastpath.
	 */
	int		(*set_fp_int)(struct qed_dev *cdev,
				      u16 cnt);

	/* Fills `info' with pointers required for utilizing interrupts */
	int		(*get_fp_int)(struct qed_dev *cdev,
				      struct qed_int_info *info);

	u32		(*sb_init)(struct qed_dev *cdev,
				   struct qed_sb_info *sb_info,
				   void *sb_virt_addr,
				   dma_addr_t sb_phy_addr,
				   u16 sb_id,
				   enum qed_sb_type type);

	u32		(*sb_release)(struct qed_dev *cdev,
				      struct qed_sb_info *sb_info,
				      u16 sb_id);

	void		(*simd_handler_config)(struct qed_dev *cdev,
					       void *token,
					       int index,
					       void (*handler)(void *));

	void		(*simd_handler_clean)(struct qed_dev *cdev,
					      int index);

	int (*dbg_all_data) (struct qed_dev *cdev, void *buffer);

	int (*dbg_all_data_size) (struct qed_dev *cdev);

/**
 * @brief can_link_change - can the instance change the link or not
 *
 * @param cdev
 *
 * @return true if link-change is allowed, false otherwise.
 */
	bool (*can_link_change)(struct qed_dev *cdev);

/**
 * @brief set_link - set links according to params
 *
 * @param cdev
 * @param params - values used to override the default link configuration
 *
 * @return 0 on success, error otherwise.
 */
	int		(*set_link)(struct qed_dev *cdev,
				    struct qed_link_params *params);

/**
 * @brief get_link - returns the current link state.
 *
 * @param cdev
 * @param if_link - structure to be filled with current link configuration.
 */
	void		(*get_link)(struct qed_dev *cdev,
				    struct qed_link_output *if_link);

/**
 * @brief - drains chip in case Tx completions fail to arrive due to pause.
 *
 * @param cdev
 */
	int		(*drain)(struct qed_dev *cdev);

/**
 * @brief update_msglvl - update module debug level
 *
 * @param cdev
 * @param dp_module
 * @param dp_level
 */
	void		(*update_msglvl)(struct qed_dev *cdev,
					 u32 dp_module,
					 u8 dp_level);

	int		(*chain_alloc)(struct qed_dev *cdev,
				       enum qed_chain_use_mode intended_use,
				       enum qed_chain_mode mode,
				       enum qed_chain_cnt_type cnt_type,
				       u32 num_elems,
				       size_t elem_size,
				       struct qed_chain *p_chain);

	void		(*chain_free)(struct qed_dev *cdev,
				      struct qed_chain *p_chain);

/**
 * @brief get_coalesce - Get coalesce parameters in usec
 *
 * @param cdev
 * @param rx_coal - Rx coalesce value in usec
 * @param tx_coal - Tx coalesce value in usec
 *
 */
	void (*get_coalesce)(struct qed_dev *cdev, u16 *rx_coal, u16 *tx_coal);

/**
 * @brief set_coalesce - Configure Rx coalesce value in usec
 *
 * @param cdev
 * @param rx_coal - Rx coalesce value in usec
 * @param tx_coal - Tx coalesce value in usec
 * @param qid - Queue index
 * @param sb_id - Status Block Id
 *
 * @return 0 on success, error otherwise.
 */
	int (*set_coalesce)(struct qed_dev *cdev, u16 rx_coal, u16 tx_coal,
			    u8 qid, u16 sb_id);

/**
 * @brief set_led - Configure LED mode
 *
 * @param cdev
 * @param mode - LED mode
 *
 * @return 0 on success, error otherwise.
 */
	int (*set_led)(struct qed_dev *cdev,
		       enum qed_led_mode mode);
};

#define MASK_FIELD(_name, _value) \
	((_value) &= (_name ## _MASK))

#define FIELD_VALUE(_name, _value) \
	((_value & _name ## _MASK) << _name ## _SHIFT)

#define SET_FIELD(value, name, flag)			       \
	do {						       \
		(value) &= ~(name ## _MASK << name ## _SHIFT); \
		(value) |= (((u64)flag) << (name ## _SHIFT));  \
	} while (0)

#define GET_FIELD(value, name) \
	(((value) >> (name ## _SHIFT)) & name ## _MASK)

/* Debug print definitions */
#define DP_ERR(cdev, fmt, ...)						     \
		pr_err("[%s:%d(%s)]" fmt,				     \
		       __func__, __LINE__,				     \
		       DP_NAME(cdev) ? DP_NAME(cdev) : "",		     \
		       ## __VA_ARGS__)					     \

#define DP_NOTICE(cdev, fmt, ...)				      \
	do {							      \
		if (unlikely((cdev)->dp_level <= QED_LEVEL_NOTICE)) { \
			pr_notice("[%s:%d(%s)]" fmt,		      \
				  __func__, __LINE__,		      \
				  DP_NAME(cdev) ? DP_NAME(cdev) : "", \
				  ## __VA_ARGS__);		      \
								      \
		}						      \
	} while (0)

#define DP_INFO(cdev, fmt, ...)					      \
	do {							      \
		if (unlikely((cdev)->dp_level <= QED_LEVEL_INFO)) {   \
			pr_notice("[%s:%d(%s)]" fmt,		      \
				  __func__, __LINE__,		      \
				  DP_NAME(cdev) ? DP_NAME(cdev) : "", \
				  ## __VA_ARGS__);		      \
		}						      \
	} while (0)

#define DP_VERBOSE(cdev, module, fmt, ...)				\
	do {								\
		if (unlikely(((cdev)->dp_level <= QED_LEVEL_VERBOSE) &&	\
			     ((cdev)->dp_module & module))) {		\
			pr_notice("[%s:%d(%s)]" fmt,			\
				  __func__, __LINE__,			\
				  DP_NAME(cdev) ? DP_NAME(cdev) : "",	\
				  ## __VA_ARGS__);			\
		}							\
	} while (0)

enum DP_LEVEL {
	QED_LEVEL_VERBOSE	= 0x0,
	QED_LEVEL_INFO		= 0x1,
	QED_LEVEL_NOTICE	= 0x2,
	QED_LEVEL_ERR		= 0x3,
};

#define QED_LOG_LEVEL_SHIFT     (30)
#define QED_LOG_VERBOSE_MASK    (0x3fffffff)
#define QED_LOG_INFO_MASK       (0x40000000)
#define QED_LOG_NOTICE_MASK     (0x80000000)

enum DP_MODULE {
	QED_MSG_SPQ	= 0x10000,
	QED_MSG_STATS	= 0x20000,
	QED_MSG_DCB	= 0x40000,
	QED_MSG_IOV	= 0x80000,
	QED_MSG_SP	= 0x100000,
	QED_MSG_STORAGE = 0x200000,
	QED_MSG_CXT	= 0x800000,
	QED_MSG_LL2	= 0x1000000,
	QED_MSG_ILT	= 0x2000000,
	QED_MSG_RDMA	= 0x4000000,
	QED_MSG_DEBUG	= 0x8000000,
	/* to be added...up to 0x8000000 */
};

enum qed_mf_mode {
	QED_MF_DEFAULT,
	QED_MF_OVLAN,
	QED_MF_NPAR,
};

struct qed_eth_stats {
	u64	no_buff_discards;
	u64	packet_too_big_discard;
	u64	ttl0_discard;
	u64	rx_ucast_bytes;
	u64	rx_mcast_bytes;
	u64	rx_bcast_bytes;
	u64	rx_ucast_pkts;
	u64	rx_mcast_pkts;
	u64	rx_bcast_pkts;
	u64	mftag_filter_discards;
	u64	mac_filter_discards;
	u64	tx_ucast_bytes;
	u64	tx_mcast_bytes;
	u64	tx_bcast_bytes;
	u64	tx_ucast_pkts;
	u64	tx_mcast_pkts;
	u64	tx_bcast_pkts;
	u64	tx_err_drop_pkts;
	u64	tpa_coalesced_pkts;
	u64	tpa_coalesced_events;
	u64	tpa_aborts_num;
	u64	tpa_not_coalesced_pkts;
	u64	tpa_coalesced_bytes;

	/* port */
	u64	rx_64_byte_packets;
	u64	rx_65_to_127_byte_packets;
	u64	rx_128_to_255_byte_packets;
	u64	rx_256_to_511_byte_packets;
	u64	rx_512_to_1023_byte_packets;
	u64	rx_1024_to_1518_byte_packets;
	u64	rx_1519_to_1522_byte_packets;
	u64	rx_1519_to_2047_byte_packets;
	u64	rx_2048_to_4095_byte_packets;
	u64	rx_4096_to_9216_byte_packets;
	u64	rx_9217_to_16383_byte_packets;
	u64	rx_crc_errors;
	u64	rx_mac_crtl_frames;
	u64	rx_pause_frames;
	u64	rx_pfc_frames;
	u64	rx_align_errors;
	u64	rx_carrier_errors;
	u64	rx_oversize_packets;
	u64	rx_jabbers;
	u64	rx_undersize_packets;
	u64	rx_fragments;
	u64	tx_64_byte_packets;
	u64	tx_65_to_127_byte_packets;
	u64	tx_128_to_255_byte_packets;
	u64	tx_256_to_511_byte_packets;
	u64	tx_512_to_1023_byte_packets;
	u64	tx_1024_to_1518_byte_packets;
	u64	tx_1519_to_2047_byte_packets;
	u64	tx_2048_to_4095_byte_packets;
	u64	tx_4096_to_9216_byte_packets;
	u64	tx_9217_to_16383_byte_packets;
	u64	tx_pause_frames;
	u64	tx_pfc_frames;
	u64	tx_lpi_entry_count;
	u64	tx_total_collisions;
	u64	brb_truncates;
	u64	brb_discards;
	u64	rx_mac_bytes;
	u64	rx_mac_uc_packets;
	u64	rx_mac_mc_packets;
	u64	rx_mac_bc_packets;
	u64	rx_mac_frames_ok;
	u64	tx_mac_bytes;
	u64	tx_mac_uc_packets;
	u64	tx_mac_mc_packets;
	u64	tx_mac_bc_packets;
	u64	tx_mac_ctrl_frames;
};

#define QED_SB_IDX              0x0002

#define RX_PI           0
#define TX_PI(tc)       (RX_PI + 1 + tc)

struct qed_sb_cnt_info {
	int	sb_cnt;
	int	sb_iov_cnt;
	int	sb_free_blk;
};

static inline u16 qed_sb_update_sb_idx(struct qed_sb_info *sb_info)
{
	u32 prod = 0;
	u16 rc = 0;

	prod = le32_to_cpu(sb_info->sb_virt->prod_index) &
	       STATUS_BLOCK_PROD_INDEX_MASK;
	if (sb_info->sb_ack != prod) {
		sb_info->sb_ack = prod;
		rc |= QED_SB_IDX;
	}

	/* Let SB update */
	mmiowb();
	return rc;
}

/**
 *
 * @brief This function creates an update command for interrupts that is
 *        written to the IGU.
 *
 * @param sb_info       - This is the structure allocated and
 *                 initialized per status block. Assumption is
 *                 that it was initialized using qed_sb_init
 * @param int_cmd       - Enable/Disable/Nop
 * @param upd_flg       - whether igu consumer should be
 *                 updated.
 *
 * @return inline void
 */
static inline void qed_sb_ack(struct qed_sb_info *sb_info,
			      enum igu_int_cmd int_cmd,
			      u8 upd_flg)
{
	struct igu_prod_cons_update igu_ack = { 0 };

	igu_ack.sb_id_and_flags =
		((sb_info->sb_ack << IGU_PROD_CONS_UPDATE_SB_INDEX_SHIFT) |
		 (upd_flg << IGU_PROD_CONS_UPDATE_UPDATE_FLAG_SHIFT) |
		 (int_cmd << IGU_PROD_CONS_UPDATE_ENABLE_INT_SHIFT) |
		 (IGU_SEG_ACCESS_REG <<
		  IGU_PROD_CONS_UPDATE_SEGMENT_ACCESS_SHIFT));

	DIRECT_REG_WR(sb_info->igu_addr, igu_ack.sb_id_and_flags);

	/* Both segments (interrupts & acks) are written to same place address;
	 * Need to guarantee all commands will be received (in-order) by HW.
	 */
	mmiowb();
	barrier();
}

static inline void __internal_ram_wr(void *p_hwfn,
				     void __iomem *addr,
				     int size,
				     u32 *data)

{
	unsigned int i;

	for (i = 0; i < size / sizeof(*data); i++)
		DIRECT_REG_WR(&((u32 __iomem *)addr)[i], data[i]);
}

static inline void internal_ram_wr(void __iomem *addr,
				   int size,
				   u32 *data)
{
	__internal_ram_wr(NULL, addr, size, data);
}

enum qed_rss_caps {
	QED_RSS_IPV4		= 0x1,
	QED_RSS_IPV6		= 0x2,
	QED_RSS_IPV4_TCP	= 0x4,
	QED_RSS_IPV6_TCP	= 0x8,
	QED_RSS_IPV4_UDP	= 0x10,
	QED_RSS_IPV6_UDP	= 0x20,
};

#define QED_RSS_IND_TABLE_SIZE 128
#define QED_RSS_KEY_SIZE 10 /* size in 32b chunks */
#endif
