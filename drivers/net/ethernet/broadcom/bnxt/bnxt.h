/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_H
#define BNXT_H

#define DRV_MODULE_NAME		"bnxt_en"
#define DRV_MODULE_VERSION	"1.5.0"

#define DRV_VER_MAJ	1
#define DRV_VER_MIN	5
#define DRV_VER_UPD	0

struct tx_bd {
	__le32 tx_bd_len_flags_type;
	#define TX_BD_TYPE					(0x3f << 0)
	 #define TX_BD_TYPE_SHORT_TX_BD				 (0x00 << 0)
	 #define TX_BD_TYPE_LONG_TX_BD				 (0x10 << 0)
	#define TX_BD_FLAGS_PACKET_END				(1 << 6)
	#define TX_BD_FLAGS_NO_CMPL				(1 << 7)
	#define TX_BD_FLAGS_BD_CNT				(0x1f << 8)
	 #define TX_BD_FLAGS_BD_CNT_SHIFT			 8
	#define TX_BD_FLAGS_LHINT				(3 << 13)
	 #define TX_BD_FLAGS_LHINT_SHIFT			 13
	 #define TX_BD_FLAGS_LHINT_512_AND_SMALLER		 (0 << 13)
	 #define TX_BD_FLAGS_LHINT_512_TO_1023			 (1 << 13)
	 #define TX_BD_FLAGS_LHINT_1024_TO_2047			 (2 << 13)
	 #define TX_BD_FLAGS_LHINT_2048_AND_LARGER		 (3 << 13)
	#define TX_BD_FLAGS_COAL_NOW				(1 << 15)
	#define TX_BD_LEN					(0xffff << 16)
	 #define TX_BD_LEN_SHIFT				 16

	u32 tx_bd_opaque;
	__le64 tx_bd_haddr;
} __packed;

struct tx_bd_ext {
	__le32 tx_bd_hsize_lflags;
	#define TX_BD_FLAGS_TCP_UDP_CHKSUM			(1 << 0)
	#define TX_BD_FLAGS_IP_CKSUM				(1 << 1)
	#define TX_BD_FLAGS_NO_CRC				(1 << 2)
	#define TX_BD_FLAGS_STAMP				(1 << 3)
	#define TX_BD_FLAGS_T_IP_CHKSUM				(1 << 4)
	#define TX_BD_FLAGS_LSO					(1 << 5)
	#define TX_BD_FLAGS_IPID_FMT				(1 << 6)
	#define TX_BD_FLAGS_T_IPID				(1 << 7)
	#define TX_BD_HSIZE					(0xff << 16)
	 #define TX_BD_HSIZE_SHIFT				 16

	__le32 tx_bd_mss;
	__le32 tx_bd_cfa_action;
	#define TX_BD_CFA_ACTION				(0xffff << 16)
	 #define TX_BD_CFA_ACTION_SHIFT				 16

	__le32 tx_bd_cfa_meta;
	#define TX_BD_CFA_META_MASK                             0xfffffff
	#define TX_BD_CFA_META_VID_MASK                         0xfff
	#define TX_BD_CFA_META_PRI_MASK                         (0xf << 12)
	 #define TX_BD_CFA_META_PRI_SHIFT                        12
	#define TX_BD_CFA_META_TPID_MASK                        (3 << 16)
	 #define TX_BD_CFA_META_TPID_SHIFT                       16
	#define TX_BD_CFA_META_KEY                              (0xf << 28)
	 #define TX_BD_CFA_META_KEY_SHIFT			 28
	#define TX_BD_CFA_META_KEY_VLAN                         (1 << 28)
};

struct rx_bd {
	__le32 rx_bd_len_flags_type;
	#define RX_BD_TYPE					(0x3f << 0)
	 #define RX_BD_TYPE_RX_PACKET_BD			 0x4
	 #define RX_BD_TYPE_RX_BUFFER_BD			 0x5
	 #define RX_BD_TYPE_RX_AGG_BD				 0x6
	 #define RX_BD_TYPE_16B_BD_SIZE				 (0 << 4)
	 #define RX_BD_TYPE_32B_BD_SIZE				 (1 << 4)
	 #define RX_BD_TYPE_48B_BD_SIZE				 (2 << 4)
	 #define RX_BD_TYPE_64B_BD_SIZE				 (3 << 4)
	#define RX_BD_FLAGS_SOP					(1 << 6)
	#define RX_BD_FLAGS_EOP					(1 << 7)
	#define RX_BD_FLAGS_BUFFERS				(3 << 8)
	 #define RX_BD_FLAGS_1_BUFFER_PACKET			 (0 << 8)
	 #define RX_BD_FLAGS_2_BUFFER_PACKET			 (1 << 8)
	 #define RX_BD_FLAGS_3_BUFFER_PACKET			 (2 << 8)
	 #define RX_BD_FLAGS_4_BUFFER_PACKET			 (3 << 8)
	#define RX_BD_LEN					(0xffff << 16)
	 #define RX_BD_LEN_SHIFT				 16

	u32 rx_bd_opaque;
	__le64 rx_bd_haddr;
};

struct tx_cmp {
	__le32 tx_cmp_flags_type;
	#define CMP_TYPE					(0x3f << 0)
	 #define CMP_TYPE_TX_L2_CMP				 0
	 #define CMP_TYPE_RX_L2_CMP				 17
	 #define CMP_TYPE_RX_AGG_CMP				 18
	 #define CMP_TYPE_RX_L2_TPA_START_CMP			 19
	 #define CMP_TYPE_RX_L2_TPA_END_CMP			 21
	 #define CMP_TYPE_STATUS_CMP				 32
	 #define CMP_TYPE_REMOTE_DRIVER_REQ			 34
	 #define CMP_TYPE_REMOTE_DRIVER_RESP			 36
	 #define CMP_TYPE_ERROR_STATUS				 48
	 #define CMPL_BASE_TYPE_STAT_EJECT			 0x1aUL
	 #define CMPL_BASE_TYPE_HWRM_DONE			 0x20UL
	 #define CMPL_BASE_TYPE_HWRM_FWD_REQ			 0x22UL
	 #define CMPL_BASE_TYPE_HWRM_FWD_RESP			 0x24UL
	 #define CMPL_BASE_TYPE_HWRM_ASYNC_EVENT		 0x2eUL

	#define TX_CMP_FLAGS_ERROR				(1 << 6)
	#define TX_CMP_FLAGS_PUSH				(1 << 7)

	u32 tx_cmp_opaque;
	__le32 tx_cmp_errors_v;
	#define TX_CMP_V					(1 << 0)
	#define TX_CMP_ERRORS_BUFFER_ERROR			(7 << 1)
	 #define TX_CMP_ERRORS_BUFFER_ERROR_NO_ERROR		 0
	 #define TX_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT		 2
	 #define TX_CMP_ERRORS_BUFFER_ERROR_INVALID_STAG	 4
	 #define TX_CMP_ERRORS_BUFFER_ERROR_STAG_BOUNDS		 5
	 #define TX_CMP_ERRORS_ZERO_LENGTH_PKT			 (1 << 4)
	 #define TX_CMP_ERRORS_EXCESSIVE_BD_LEN			 (1 << 5)
	 #define TX_CMP_ERRORS_DMA_ERROR			 (1 << 6)
	 #define TX_CMP_ERRORS_HINT_TOO_SHORT			 (1 << 7)

	__le32 tx_cmp_unsed_3;
};

struct rx_cmp {
	__le32 rx_cmp_len_flags_type;
	#define RX_CMP_CMP_TYPE					(0x3f << 0)
	#define RX_CMP_FLAGS_ERROR				(1 << 6)
	#define RX_CMP_FLAGS_PLACEMENT				(7 << 7)
	#define RX_CMP_FLAGS_RSS_VALID				(1 << 10)
	#define RX_CMP_FLAGS_UNUSED				(1 << 11)
	 #define RX_CMP_FLAGS_ITYPES_SHIFT			 12
	 #define RX_CMP_FLAGS_ITYPE_UNKNOWN			 (0 << 12)
	 #define RX_CMP_FLAGS_ITYPE_IP				 (1 << 12)
	 #define RX_CMP_FLAGS_ITYPE_TCP				 (2 << 12)
	 #define RX_CMP_FLAGS_ITYPE_UDP				 (3 << 12)
	 #define RX_CMP_FLAGS_ITYPE_FCOE			 (4 << 12)
	 #define RX_CMP_FLAGS_ITYPE_ROCE			 (5 << 12)
	 #define RX_CMP_FLAGS_ITYPE_PTP_WO_TS			 (8 << 12)
	 #define RX_CMP_FLAGS_ITYPE_PTP_W_TS			 (9 << 12)
	#define RX_CMP_LEN					(0xffff << 16)
	 #define RX_CMP_LEN_SHIFT				 16

	u32 rx_cmp_opaque;
	__le32 rx_cmp_misc_v1;
	#define RX_CMP_V1					(1 << 0)
	#define RX_CMP_AGG_BUFS					(0x1f << 1)
	 #define RX_CMP_AGG_BUFS_SHIFT				 1
	#define RX_CMP_RSS_HASH_TYPE				(0x7f << 9)
	 #define RX_CMP_RSS_HASH_TYPE_SHIFT			 9
	#define RX_CMP_PAYLOAD_OFFSET				(0xff << 16)
	 #define RX_CMP_PAYLOAD_OFFSET_SHIFT			 16

	__le32 rx_cmp_rss_hash;
};

#define RX_CMP_HASH_VALID(rxcmp)				\
	((rxcmp)->rx_cmp_len_flags_type & cpu_to_le32(RX_CMP_FLAGS_RSS_VALID))

#define RSS_PROFILE_ID_MASK	0x1f

#define RX_CMP_HASH_TYPE(rxcmp)					\
	(((le32_to_cpu((rxcmp)->rx_cmp_misc_v1) & RX_CMP_RSS_HASH_TYPE) >>\
	  RX_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

struct rx_cmp_ext {
	__le32 rx_cmp_flags2;
	#define RX_CMP_FLAGS2_IP_CS_CALC			0x1
	#define RX_CMP_FLAGS2_L4_CS_CALC			(0x1 << 1)
	#define RX_CMP_FLAGS2_T_IP_CS_CALC			(0x1 << 2)
	#define RX_CMP_FLAGS2_T_L4_CS_CALC			(0x1 << 3)
	#define RX_CMP_FLAGS2_META_FORMAT_VLAN			(0x1 << 4)
	__le32 rx_cmp_meta_data;
	#define RX_CMP_FLAGS2_METADATA_VID_MASK			0xfff
	#define RX_CMP_FLAGS2_METADATA_TPID_MASK		0xffff0000
	 #define RX_CMP_FLAGS2_METADATA_TPID_SFT		 16
	__le32 rx_cmp_cfa_code_errors_v2;
	#define RX_CMP_V					(1 << 0)
	#define RX_CMPL_ERRORS_MASK				(0x7fff << 1)
	 #define RX_CMPL_ERRORS_SFT				 1
	#define RX_CMPL_ERRORS_BUFFER_ERROR_MASK		(0x7 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_NO_BUFFER		 (0x0 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_DID_NOT_FIT	 (0x1 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_NOT_ON_CHIP	 (0x2 << 1)
	 #define RX_CMPL_ERRORS_BUFFER_ERROR_BAD_FORMAT		 (0x3 << 1)
	#define RX_CMPL_ERRORS_IP_CS_ERROR			(0x1 << 4)
	#define RX_CMPL_ERRORS_L4_CS_ERROR			(0x1 << 5)
	#define RX_CMPL_ERRORS_T_IP_CS_ERROR			(0x1 << 6)
	#define RX_CMPL_ERRORS_T_L4_CS_ERROR			(0x1 << 7)
	#define RX_CMPL_ERRORS_CRC_ERROR			(0x1 << 8)
	#define RX_CMPL_ERRORS_T_PKT_ERROR_MASK			(0x7 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_NO_ERROR		 (0x0 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_VERSION	 (0x1 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_HDR_LEN	 (0x2 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_TUNNEL_TOTAL_ERROR	 (0x3 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_IP_TOTAL_ERROR	 (0x4 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_UDP_TOTAL_ERROR	 (0x5 << 9)
	 #define RX_CMPL_ERRORS_T_PKT_ERROR_T_L3_BAD_TTL	 (0x6 << 9)
	#define RX_CMPL_ERRORS_PKT_ERROR_MASK			(0xf << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_NO_ERROR		 (0x0 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_VERSION	 (0x1 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_HDR_LEN	 (0x2 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L3_BAD_TTL		 (0x3 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_IP_TOTAL_ERROR	 (0x4 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_UDP_TOTAL_ERROR	 (0x5 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN	 (0x6 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_HDR_LEN_TOO_SMALL (0x7 << 12)
	 #define RX_CMPL_ERRORS_PKT_ERROR_L4_BAD_OPT_LEN	 (0x8 << 12)

	#define RX_CMPL_CFA_CODE_MASK				(0xffff << 16)
	 #define RX_CMPL_CFA_CODE_SFT				 16

	__le32 rx_cmp_unused3;
};

#define RX_CMP_L2_ERRORS						\
	cpu_to_le32(RX_CMPL_ERRORS_BUFFER_ERROR_MASK | RX_CMPL_ERRORS_CRC_ERROR)

#define RX_CMP_L4_CS_BITS						\
	(cpu_to_le32(RX_CMP_FLAGS2_L4_CS_CALC | RX_CMP_FLAGS2_T_L4_CS_CALC))

#define RX_CMP_L4_CS_ERR_BITS						\
	(cpu_to_le32(RX_CMPL_ERRORS_L4_CS_ERROR | RX_CMPL_ERRORS_T_L4_CS_ERROR))

#define RX_CMP_L4_CS_OK(rxcmp1)						\
	    (((rxcmp1)->rx_cmp_flags2 &	RX_CMP_L4_CS_BITS) &&		\
	     !((rxcmp1)->rx_cmp_cfa_code_errors_v2 & RX_CMP_L4_CS_ERR_BITS))

#define RX_CMP_ENCAP(rxcmp1)						\
	    ((le32_to_cpu((rxcmp1)->rx_cmp_flags2) &			\
	     RX_CMP_FLAGS2_T_L4_CS_CALC) >> 3)

struct rx_agg_cmp {
	__le32 rx_agg_cmp_len_flags_type;
	#define RX_AGG_CMP_TYPE					(0x3f << 0)
	#define RX_AGG_CMP_LEN					(0xffff << 16)
	 #define RX_AGG_CMP_LEN_SHIFT				 16
	u32 rx_agg_cmp_opaque;
	__le32 rx_agg_cmp_v;
	#define RX_AGG_CMP_V					(1 << 0)
	__le32 rx_agg_cmp_unused;
};

struct rx_tpa_start_cmp {
	__le32 rx_tpa_start_cmp_len_flags_type;
	#define RX_TPA_START_CMP_TYPE				(0x3f << 0)
	#define RX_TPA_START_CMP_FLAGS				(0x3ff << 6)
	 #define RX_TPA_START_CMP_FLAGS_SHIFT			 6
	#define RX_TPA_START_CMP_FLAGS_PLACEMENT		(0x7 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_SHIFT		 7
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_JUMBO		 (0x1 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_HDS		 (0x2 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_JUMBO	 (0x5 << 7)
	 #define RX_TPA_START_CMP_FLAGS_PLACEMENT_GRO_HDS	 (0x6 << 7)
	#define RX_TPA_START_CMP_FLAGS_RSS_VALID		(0x1 << 10)
	#define RX_TPA_START_CMP_FLAGS_ITYPES			(0xf << 12)
	 #define RX_TPA_START_CMP_FLAGS_ITYPES_SHIFT		 12
	 #define RX_TPA_START_CMP_FLAGS_ITYPE_TCP		 (0x2 << 12)
	#define RX_TPA_START_CMP_LEN				(0xffff << 16)
	 #define RX_TPA_START_CMP_LEN_SHIFT			 16

	u32 rx_tpa_start_cmp_opaque;
	__le32 rx_tpa_start_cmp_misc_v1;
	#define RX_TPA_START_CMP_V1				(0x1 << 0)
	#define RX_TPA_START_CMP_RSS_HASH_TYPE			(0x7f << 9)
	 #define RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT		 9
	#define RX_TPA_START_CMP_AGG_ID				(0x7f << 25)
	 #define RX_TPA_START_CMP_AGG_ID_SHIFT			 25

	__le32 rx_tpa_start_cmp_rss_hash;
};

#define TPA_START_HASH_VALID(rx_tpa_start)				\
	((rx_tpa_start)->rx_tpa_start_cmp_len_flags_type &		\
	 cpu_to_le32(RX_TPA_START_CMP_FLAGS_RSS_VALID))

#define TPA_START_HASH_TYPE(rx_tpa_start)				\
	(((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	   RX_TPA_START_CMP_RSS_HASH_TYPE) >>				\
	  RX_TPA_START_CMP_RSS_HASH_TYPE_SHIFT) & RSS_PROFILE_ID_MASK)

#define TPA_START_AGG_ID(rx_tpa_start)					\
	((le32_to_cpu((rx_tpa_start)->rx_tpa_start_cmp_misc_v1) &	\
	 RX_TPA_START_CMP_AGG_ID) >> RX_TPA_START_CMP_AGG_ID_SHIFT)

struct rx_tpa_start_cmp_ext {
	__le32 rx_tpa_start_cmp_flags2;
	#define RX_TPA_START_CMP_FLAGS2_IP_CS_CALC		(0x1 << 0)
	#define RX_TPA_START_CMP_FLAGS2_L4_CS_CALC		(0x1 << 1)
	#define RX_TPA_START_CMP_FLAGS2_T_IP_CS_CALC		(0x1 << 2)
	#define RX_TPA_START_CMP_FLAGS2_T_L4_CS_CALC		(0x1 << 3)
	#define RX_TPA_START_CMP_FLAGS2_IP_TYPE			(0x1 << 8)

	__le32 rx_tpa_start_cmp_metadata;
	__le32 rx_tpa_start_cmp_cfa_code_v2;
	#define RX_TPA_START_CMP_V2				(0x1 << 0)
	#define RX_TPA_START_CMP_CFA_CODE			(0xffff << 16)
	 #define RX_TPA_START_CMPL_CFA_CODE_SHIFT		 16
	__le32 rx_tpa_start_cmp_hdr_info;
};

struct rx_tpa_end_cmp {
	__le32 rx_tpa_end_cmp_len_flags_type;
	#define RX_TPA_END_CMP_TYPE				(0x3f << 0)
	#define RX_TPA_END_CMP_FLAGS				(0x3ff << 6)
	 #define RX_TPA_END_CMP_FLAGS_SHIFT			 6
	#define RX_TPA_END_CMP_FLAGS_PLACEMENT			(0x7 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_SHIFT		 7
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_JUMBO		 (0x1 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_HDS		 (0x2 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_JUMBO	 (0x5 << 7)
	 #define RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_HDS		 (0x6 << 7)
	#define RX_TPA_END_CMP_FLAGS_RSS_VALID			(0x1 << 10)
	#define RX_TPA_END_CMP_FLAGS_ITYPES			(0xf << 12)
	 #define RX_TPA_END_CMP_FLAGS_ITYPES_SHIFT		 12
	 #define RX_TPA_END_CMP_FLAGS_ITYPE_TCP			 (0x2 << 12)
	#define RX_TPA_END_CMP_LEN				(0xffff << 16)
	 #define RX_TPA_END_CMP_LEN_SHIFT			 16

	u32 rx_tpa_end_cmp_opaque;
	__le32 rx_tpa_end_cmp_misc_v1;
	#define RX_TPA_END_CMP_V1				(0x1 << 0)
	#define RX_TPA_END_CMP_AGG_BUFS				(0x3f << 1)
	 #define RX_TPA_END_CMP_AGG_BUFS_SHIFT			 1
	#define RX_TPA_END_CMP_TPA_SEGS				(0xff << 8)
	 #define RX_TPA_END_CMP_TPA_SEGS_SHIFT			 8
	#define RX_TPA_END_CMP_PAYLOAD_OFFSET			(0xff << 16)
	 #define RX_TPA_END_CMP_PAYLOAD_OFFSET_SHIFT		 16
	#define RX_TPA_END_CMP_AGG_ID				(0x7f << 25)
	 #define RX_TPA_END_CMP_AGG_ID_SHIFT			 25

	__le32 rx_tpa_end_cmp_tsdelta;
	#define RX_TPA_END_GRO_TS				(0x1 << 31)
};

#define TPA_END_AGG_ID(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_AGG_ID) >> RX_TPA_END_CMP_AGG_ID_SHIFT)

#define TPA_END_TPA_SEGS(rx_tpa_end)					\
	((le32_to_cpu((rx_tpa_end)->rx_tpa_end_cmp_misc_v1) &		\
	 RX_TPA_END_CMP_TPA_SEGS) >> RX_TPA_END_CMP_TPA_SEGS_SHIFT)

#define RX_TPA_END_CMP_FLAGS_PLACEMENT_ANY_GRO				\
	cpu_to_le32(RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_JUMBO &		\
		    RX_TPA_END_CMP_FLAGS_PLACEMENT_GRO_HDS)

#define TPA_END_GRO(rx_tpa_end)						\
	((rx_tpa_end)->rx_tpa_end_cmp_len_flags_type &			\
	 RX_TPA_END_CMP_FLAGS_PLACEMENT_ANY_GRO)

#define TPA_END_GRO_TS(rx_tpa_end)					\
	(!!((rx_tpa_end)->rx_tpa_end_cmp_tsdelta &			\
	    cpu_to_le32(RX_TPA_END_GRO_TS)))

struct rx_tpa_end_cmp_ext {
	__le32 rx_tpa_end_cmp_dup_acks;
	#define RX_TPA_END_CMP_TPA_DUP_ACKS			(0xf << 0)

	__le32 rx_tpa_end_cmp_seg_len;
	#define RX_TPA_END_CMP_TPA_SEG_LEN			(0xffff << 0)

	__le32 rx_tpa_end_cmp_errors_v2;
	#define RX_TPA_END_CMP_V2				(0x1 << 0)
	#define RX_TPA_END_CMP_ERRORS				(0x7fff << 1)
	#define RX_TPA_END_CMPL_ERRORS_SHIFT			 1

	u32 rx_tpa_end_cmp_start_opaque;
};

#define DB_IDX_MASK						0xffffff
#define DB_IDX_VALID						(0x1 << 26)
#define DB_IRQ_DIS						(0x1 << 27)
#define DB_KEY_TX						(0x0 << 28)
#define DB_KEY_RX						(0x1 << 28)
#define DB_KEY_CP						(0x2 << 28)
#define DB_KEY_ST						(0x3 << 28)
#define DB_KEY_TX_PUSH						(0x4 << 28)
#define DB_LONG_TX_PUSH						(0x2 << 24)

#define INVALID_HW_RING_ID	((u16)-1)

/* The hardware supports certain page sizes.  Use the supported page sizes
 * to allocate the rings.
 */
#if (PAGE_SHIFT < 12)
#define BNXT_PAGE_SHIFT	12
#elif (PAGE_SHIFT <= 13)
#define BNXT_PAGE_SHIFT	PAGE_SHIFT
#elif (PAGE_SHIFT < 16)
#define BNXT_PAGE_SHIFT	13
#else
#define BNXT_PAGE_SHIFT	16
#endif

#define BNXT_PAGE_SIZE	(1 << BNXT_PAGE_SHIFT)

/* The RXBD length is 16-bit so we can only support page sizes < 64K */
#if (PAGE_SHIFT > 15)
#define BNXT_RX_PAGE_SHIFT 15
#else
#define BNXT_RX_PAGE_SHIFT PAGE_SHIFT
#endif

#define BNXT_RX_PAGE_SIZE (1 << BNXT_RX_PAGE_SHIFT)

#define BNXT_MIN_PKT_SIZE	52

#define BNXT_NUM_TESTS(bp)	0

#define BNXT_DEFAULT_RX_RING_SIZE	511
#define BNXT_DEFAULT_TX_RING_SIZE	511

#define MAX_TPA		64

#if (BNXT_PAGE_SHIFT == 16)
#define MAX_RX_PAGES	1
#define MAX_RX_AGG_PAGES	4
#define MAX_TX_PAGES	1
#define MAX_CP_PAGES	8
#else
#define MAX_RX_PAGES	8
#define MAX_RX_AGG_PAGES	32
#define MAX_TX_PAGES	8
#define MAX_CP_PAGES	64
#endif

#define RX_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct rx_bd))
#define TX_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct tx_bd))
#define CP_DESC_CNT (BNXT_PAGE_SIZE / sizeof(struct tx_cmp))

#define SW_RXBD_RING_SIZE (sizeof(struct bnxt_sw_rx_bd) * RX_DESC_CNT)
#define HW_RXBD_RING_SIZE (sizeof(struct rx_bd) * RX_DESC_CNT)

#define SW_RXBD_AGG_RING_SIZE (sizeof(struct bnxt_sw_rx_agg_bd) * RX_DESC_CNT)

#define SW_TXBD_RING_SIZE (sizeof(struct bnxt_sw_tx_bd) * TX_DESC_CNT)
#define HW_TXBD_RING_SIZE (sizeof(struct tx_bd) * TX_DESC_CNT)

#define HW_CMPD_RING_SIZE (sizeof(struct tx_cmp) * CP_DESC_CNT)

#define BNXT_MAX_RX_DESC_CNT		(RX_DESC_CNT * MAX_RX_PAGES - 1)
#define BNXT_MAX_RX_JUM_DESC_CNT	(RX_DESC_CNT * MAX_RX_AGG_PAGES - 1)
#define BNXT_MAX_TX_DESC_CNT		(TX_DESC_CNT * MAX_TX_PAGES - 1)

#define RX_RING(x)	(((x) & ~(RX_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define RX_IDX(x)	((x) & (RX_DESC_CNT - 1))

#define TX_RING(x)	(((x) & ~(TX_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define TX_IDX(x)	((x) & (TX_DESC_CNT - 1))

#define CP_RING(x)	(((x) & ~(CP_DESC_CNT - 1)) >> (BNXT_PAGE_SHIFT - 4))
#define CP_IDX(x)	((x) & (CP_DESC_CNT - 1))

#define TX_CMP_VALID(txcmp, raw_cons)					\
	(!!((txcmp)->tx_cmp_errors_v & cpu_to_le32(TX_CMP_V)) ==	\
	 !((raw_cons) & bp->cp_bit))

#define RX_CMP_VALID(rxcmp1, raw_cons)					\
	(!!((rxcmp1)->rx_cmp_cfa_code_errors_v2 & cpu_to_le32(RX_CMP_V)) ==\
	 !((raw_cons) & bp->cp_bit))

#define RX_AGG_CMP_VALID(agg, raw_cons)				\
	(!!((agg)->rx_agg_cmp_v & cpu_to_le32(RX_AGG_CMP_V)) ==	\
	 !((raw_cons) & bp->cp_bit))

#define TX_CMP_TYPE(txcmp)					\
	(le32_to_cpu((txcmp)->tx_cmp_flags_type) & CMP_TYPE)

#define RX_CMP_TYPE(rxcmp)					\
	(le32_to_cpu((rxcmp)->rx_cmp_len_flags_type) & RX_CMP_CMP_TYPE)

#define NEXT_RX(idx)		(((idx) + 1) & bp->rx_ring_mask)

#define NEXT_RX_AGG(idx)	(((idx) + 1) & bp->rx_agg_ring_mask)

#define NEXT_TX(idx)		(((idx) + 1) & bp->tx_ring_mask)

#define ADV_RAW_CMP(idx, n)	((idx) + (n))
#define NEXT_RAW_CMP(idx)	ADV_RAW_CMP(idx, 1)
#define RING_CMP(idx)		((idx) & bp->cp_ring_mask)
#define NEXT_CMP(idx)		RING_CMP(ADV_RAW_CMP(idx, 1))

#define BNXT_HWRM_MAX_REQ_LEN		(bp->hwrm_max_req_len)
#define DFLT_HWRM_CMD_TIMEOUT		500
#define HWRM_CMD_TIMEOUT		(bp->hwrm_cmd_timeout)
#define HWRM_RESET_TIMEOUT		((HWRM_CMD_TIMEOUT) * 4)
#define HWRM_RESP_ERR_CODE_MASK		0xffff
#define HWRM_RESP_LEN_OFFSET		4
#define HWRM_RESP_LEN_MASK		0xffff0000
#define HWRM_RESP_LEN_SFT		16
#define HWRM_RESP_VALID_MASK		0xff000000
#define HWRM_SEQ_ID_INVALID		-1
#define BNXT_HWRM_REQ_MAX_SIZE		128
#define BNXT_HWRM_REQS_PER_PAGE		(BNXT_PAGE_SIZE /	\
					 BNXT_HWRM_REQ_MAX_SIZE)

struct bnxt_sw_tx_bd {
	struct sk_buff		*skb;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	u8			is_gso;
	u8			is_push;
	unsigned short		nr_frags;
};

struct bnxt_sw_rx_bd {
	u8			*data;
	DEFINE_DMA_UNMAP_ADDR(mapping);
};

struct bnxt_sw_rx_agg_bd {
	struct page		*page;
	unsigned int		offset;
	dma_addr_t		mapping;
};

struct bnxt_ring_struct {
	int			nr_pages;
	int			page_size;
	void			**pg_arr;
	dma_addr_t		*dma_arr;

	__le64			*pg_tbl;
	dma_addr_t		pg_tbl_map;

	int			vmem_size;
	void			**vmem;

	u16			fw_ring_id; /* Ring id filled by Chimp FW */
	u8			queue_id;
};

struct tx_push_bd {
	__le32			doorbell;
	__le32			tx_bd_len_flags_type;
	u32			tx_bd_opaque;
	struct tx_bd_ext	txbd2;
};

struct tx_push_buffer {
	struct tx_push_bd	push_bd;
	u32			data[25];
};

struct bnxt_tx_ring_info {
	struct bnxt_napi	*bnapi;
	u16			tx_prod;
	u16			tx_cons;
	void __iomem		*tx_doorbell;

	struct tx_bd		*tx_desc_ring[MAX_TX_PAGES];
	struct bnxt_sw_tx_bd	*tx_buf_ring;

	dma_addr_t		tx_desc_mapping[MAX_TX_PAGES];

	struct tx_push_buffer	*tx_push;
	dma_addr_t		tx_push_mapping;
	__le64			data_mapping;

#define BNXT_DEV_STATE_CLOSING	0x1
	u32			dev_state;

	struct bnxt_ring_struct	tx_ring_struct;
};

struct bnxt_tpa_info {
	u8			*data;
	dma_addr_t		mapping;
	u16			len;
	unsigned short		gso_type;
	u32			flags2;
	u32			metadata;
	enum pkt_hash_types	hash_type;
	u32			rss_hash;
	u32			hdr_info;

#define BNXT_TPA_L4_SIZE(hdr_info)	\
	(((hdr_info) & 0xf8000000) ? ((hdr_info) >> 27) : 32)

#define BNXT_TPA_INNER_L3_OFF(hdr_info)	\
	(((hdr_info) >> 18) & 0x1ff)

#define BNXT_TPA_INNER_L2_OFF(hdr_info)	\
	(((hdr_info) >> 9) & 0x1ff)

#define BNXT_TPA_OUTER_L3_OFF(hdr_info)	\
	((hdr_info) & 0x1ff)
};

struct bnxt_rx_ring_info {
	struct bnxt_napi	*bnapi;
	u16			rx_prod;
	u16			rx_agg_prod;
	u16			rx_sw_agg_prod;
	u16			rx_next_cons;
	void __iomem		*rx_doorbell;
	void __iomem		*rx_agg_doorbell;

	struct rx_bd		*rx_desc_ring[MAX_RX_PAGES];
	struct bnxt_sw_rx_bd	*rx_buf_ring;

	struct rx_bd		*rx_agg_desc_ring[MAX_RX_AGG_PAGES];
	struct bnxt_sw_rx_agg_bd	*rx_agg_ring;

	unsigned long		*rx_agg_bmap;
	u16			rx_agg_bmap_size;

	struct page		*rx_page;
	unsigned int		rx_page_offset;

	dma_addr_t		rx_desc_mapping[MAX_RX_PAGES];
	dma_addr_t		rx_agg_desc_mapping[MAX_RX_AGG_PAGES];

	struct bnxt_tpa_info	*rx_tpa;

	struct bnxt_ring_struct	rx_ring_struct;
	struct bnxt_ring_struct	rx_agg_ring_struct;
};

struct bnxt_cp_ring_info {
	u32			cp_raw_cons;
	void __iomem		*cp_doorbell;

	struct tx_cmp		*cp_desc_ring[MAX_CP_PAGES];

	dma_addr_t		cp_desc_mapping[MAX_CP_PAGES];

	struct ctx_hw_stats	*hw_stats;
	dma_addr_t		hw_stats_map;
	u32			hw_stats_ctx_id;
	u64			rx_l4_csum_errors;

	struct bnxt_ring_struct	cp_ring_struct;
};

struct bnxt_napi {
	struct napi_struct	napi;
	struct bnxt		*bp;

	int			index;
	struct bnxt_cp_ring_info	cp_ring;
	struct bnxt_rx_ring_info	*rx_ring;
	struct bnxt_tx_ring_info	*tx_ring;

#ifdef CONFIG_NET_RX_BUSY_POLL
	atomic_t		poll_state;
#endif
	bool			in_reset;
};

#ifdef CONFIG_NET_RX_BUSY_POLL
enum bnxt_poll_state_t {
	BNXT_STATE_IDLE = 0,
	BNXT_STATE_NAPI,
	BNXT_STATE_POLL,
	BNXT_STATE_DISABLE,
};
#endif

struct bnxt_irq {
	irq_handler_t	handler;
	unsigned int	vector;
	u8		requested;
	char		name[IFNAMSIZ + 2];
};

#define HWRM_RING_ALLOC_TX	0x1
#define HWRM_RING_ALLOC_RX	0x2
#define HWRM_RING_ALLOC_AGG	0x4
#define HWRM_RING_ALLOC_CMPL	0x8

#define INVALID_STATS_CTX_ID	-1

struct bnxt_ring_grp_info {
	u16	fw_stats_ctx;
	u16	fw_grp_id;
	u16	rx_fw_ring_id;
	u16	agg_fw_ring_id;
	u16	cp_fw_ring_id;
};

struct bnxt_vnic_info {
	u16		fw_vnic_id; /* returned by Chimp during alloc */
#define BNXT_MAX_CTX_PER_VNIC	2
	u16		fw_rss_cos_lb_ctx[BNXT_MAX_CTX_PER_VNIC];
	u16		fw_l2_ctx_id;
#define BNXT_MAX_UC_ADDRS	4
	__le64		fw_l2_filter_id[BNXT_MAX_UC_ADDRS];
				/* index 0 always dev_addr */
	u16		uc_filter_count;
	u8		*uc_list;

	u16		*fw_grp_ids;
	u16		hash_type;
	dma_addr_t	rss_table_dma_addr;
	__le16		*rss_table;
	dma_addr_t	rss_hash_key_dma_addr;
	u64		*rss_hash_key;
	u32		rx_mask;

	u8		*mc_list;
	int		mc_list_size;
	int		mc_list_count;
	dma_addr_t	mc_list_mapping;
#define BNXT_MAX_MC_ADDRS	16

	u32		flags;
#define BNXT_VNIC_RSS_FLAG	1
#define BNXT_VNIC_RFS_FLAG	2
#define BNXT_VNIC_MCAST_FLAG	4
#define BNXT_VNIC_UCAST_FLAG	8
};

#if defined(CONFIG_BNXT_SRIOV)
struct bnxt_vf_info {
	u16	fw_fid;
	u8	mac_addr[ETH_ALEN];
	u16	max_rsscos_ctxs;
	u16	max_cp_rings;
	u16	max_tx_rings;
	u16	max_rx_rings;
	u16	max_hw_ring_grps;
	u16	max_l2_ctxs;
	u16	max_irqs;
	u16	max_vnics;
	u16	max_stat_ctxs;
	u16	vlan;
	u32	flags;
#define BNXT_VF_QOS		0x1
#define BNXT_VF_SPOOFCHK	0x2
#define BNXT_VF_LINK_FORCED	0x4
#define BNXT_VF_LINK_UP		0x8
	u32	func_flags; /* func cfg flags */
	u32	min_tx_rate;
	u32	max_tx_rate;
	void	*hwrm_cmd_req_addr;
	dma_addr_t	hwrm_cmd_req_dma_addr;
};
#endif

struct bnxt_pf_info {
#define BNXT_FIRST_PF_FID	1
#define BNXT_FIRST_VF_FID	128
	u16	fw_fid;
	u16	port_id;
	u8	mac_addr[ETH_ALEN];
	u16	max_rsscos_ctxs;
	u16	max_cp_rings;
	u16	max_tx_rings; /* HW assigned max tx rings for this PF */
	u16	max_rx_rings; /* HW assigned max rx rings for this PF */
	u16	max_hw_ring_grps;
	u16	max_irqs;
	u16	max_l2_ctxs;
	u16	max_vnics;
	u16	max_stat_ctxs;
	u32	first_vf_id;
	u16	active_vfs;
	u16	max_vfs;
	u32	max_encap_records;
	u32	max_decap_records;
	u32	max_tx_em_flows;
	u32	max_tx_wm_flows;
	u32	max_rx_em_flows;
	u32	max_rx_wm_flows;
	unsigned long	*vf_event_bmap;
	u16	hwrm_cmd_req_pages;
	void			*hwrm_cmd_req_addr[4];
	dma_addr_t		hwrm_cmd_req_dma_addr[4];
	struct bnxt_vf_info	*vf;
};

struct bnxt_ntuple_filter {
	struct hlist_node	hash;
	u8			dst_mac_addr[ETH_ALEN];
	u8			src_mac_addr[ETH_ALEN];
	struct flow_keys	fkeys;
	__le64			filter_id;
	u16			sw_id;
	u8			l2_fltr_idx;
	u16			rxq;
	u32			flow_id;
	unsigned long		state;
#define BNXT_FLTR_VALID		0
#define BNXT_FLTR_UPDATE	1
};

struct bnxt_link_info {
	u8			phy_type;
	u8			media_type;
	u8			transceiver;
	u8			phy_addr;
	u8			phy_link_status;
#define BNXT_LINK_NO_LINK	PORT_PHY_QCFG_RESP_LINK_NO_LINK
#define BNXT_LINK_SIGNAL	PORT_PHY_QCFG_RESP_LINK_SIGNAL
#define BNXT_LINK_LINK		PORT_PHY_QCFG_RESP_LINK_LINK
	u8			wire_speed;
	u8			loop_back;
	u8			link_up;
	u8			duplex;
#define BNXT_LINK_DUPLEX_HALF	PORT_PHY_QCFG_RESP_DUPLEX_HALF
#define BNXT_LINK_DUPLEX_FULL	PORT_PHY_QCFG_RESP_DUPLEX_FULL
	u8			pause;
#define BNXT_LINK_PAUSE_TX	PORT_PHY_QCFG_RESP_PAUSE_TX
#define BNXT_LINK_PAUSE_RX	PORT_PHY_QCFG_RESP_PAUSE_RX
#define BNXT_LINK_PAUSE_BOTH	(PORT_PHY_QCFG_RESP_PAUSE_RX | \
				 PORT_PHY_QCFG_RESP_PAUSE_TX)
	u8			lp_pause;
	u8			auto_pause_setting;
	u8			force_pause_setting;
	u8			duplex_setting;
	u8			auto_mode;
#define BNXT_AUTO_MODE(mode)	((mode) > BNXT_LINK_AUTO_NONE && \
				 (mode) <= BNXT_LINK_AUTO_MSK)
#define BNXT_LINK_AUTO_NONE     PORT_PHY_QCFG_RESP_AUTO_MODE_NONE
#define BNXT_LINK_AUTO_ALLSPDS	PORT_PHY_QCFG_RESP_AUTO_MODE_ALL_SPEEDS
#define BNXT_LINK_AUTO_ONESPD	PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_SPEED
#define BNXT_LINK_AUTO_ONEORBELOW PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_OR_BELOW
#define BNXT_LINK_AUTO_MSK	PORT_PHY_QCFG_RESP_AUTO_MODE_SPEED_MASK
#define PHY_VER_LEN		3
	u8			phy_ver[PHY_VER_LEN];
	u16			link_speed;
#define BNXT_LINK_SPEED_100MB	PORT_PHY_QCFG_RESP_LINK_SPEED_100MB
#define BNXT_LINK_SPEED_1GB	PORT_PHY_QCFG_RESP_LINK_SPEED_1GB
#define BNXT_LINK_SPEED_2GB	PORT_PHY_QCFG_RESP_LINK_SPEED_2GB
#define BNXT_LINK_SPEED_2_5GB	PORT_PHY_QCFG_RESP_LINK_SPEED_2_5GB
#define BNXT_LINK_SPEED_10GB	PORT_PHY_QCFG_RESP_LINK_SPEED_10GB
#define BNXT_LINK_SPEED_20GB	PORT_PHY_QCFG_RESP_LINK_SPEED_20GB
#define BNXT_LINK_SPEED_25GB	PORT_PHY_QCFG_RESP_LINK_SPEED_25GB
#define BNXT_LINK_SPEED_40GB	PORT_PHY_QCFG_RESP_LINK_SPEED_40GB
#define BNXT_LINK_SPEED_50GB	PORT_PHY_QCFG_RESP_LINK_SPEED_50GB
	u16			support_speeds;
	u16			auto_link_speeds;
#define BNXT_LINK_SPEED_MSK_100MB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100MB
#define BNXT_LINK_SPEED_MSK_1GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_1GB
#define BNXT_LINK_SPEED_MSK_2GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2GB
#define BNXT_LINK_SPEED_MSK_10GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_10GB
#define BNXT_LINK_SPEED_MSK_2_5GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2_5GB
#define BNXT_LINK_SPEED_MSK_20GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_20GB
#define BNXT_LINK_SPEED_MSK_25GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_25GB
#define BNXT_LINK_SPEED_MSK_40GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_40GB
#define BNXT_LINK_SPEED_MSK_50GB PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_50GB
	u16			support_auto_speeds;
	u16			lp_auto_link_speeds;
	u16			force_link_speed;
	u32			preemphasis;
	u8			module_status;

	/* copy of requested setting from ethtool cmd */
	u8			autoneg;
#define BNXT_AUTONEG_SPEED		1
#define BNXT_AUTONEG_FLOW_CTRL		2
	u8			req_duplex;
	u8			req_flow_ctrl;
	u16			req_link_speed;
	u32			advertising;
	bool			force_link_chng;

	/* a copy of phy_qcfg output used to report link
	 * info to VF
	 */
	struct hwrm_port_phy_qcfg_output phy_qcfg_resp;
};

#define BNXT_MAX_QUEUE	8

struct bnxt_queue_info {
	u8	queue_id;
	u8	queue_profile;
};

#define BNXT_GRCPF_REG_WINDOW_BASE_OUT	0x400
#define BNXT_CAG_REG_LEGACY_INT_STATUS	0x4014
#define BNXT_CAG_REG_BASE		0x300000

struct bnxt {
	void __iomem		*bar0;
	void __iomem		*bar1;
	void __iomem		*bar2;

	u32			reg_base;
	u16			chip_num;
#define CHIP_NUM_57301		0x16c8
#define CHIP_NUM_57302		0x16c9
#define CHIP_NUM_57304		0x16ca
#define CHIP_NUM_58700		0x16cd
#define CHIP_NUM_57402		0x16d0
#define CHIP_NUM_57404		0x16d1
#define CHIP_NUM_57406		0x16d2

#define CHIP_NUM_57311		0x16ce
#define CHIP_NUM_57312		0x16cf
#define CHIP_NUM_57314		0x16df
#define CHIP_NUM_57412		0x16d6
#define CHIP_NUM_57414		0x16d7
#define CHIP_NUM_57416		0x16d8
#define CHIP_NUM_57417		0x16d9

#define BNXT_CHIP_NUM_5730X(chip_num)		\
	((chip_num) >= CHIP_NUM_57301 &&	\
	 (chip_num) <= CHIP_NUM_57304)

#define BNXT_CHIP_NUM_5740X(chip_num)		\
	((chip_num) >= CHIP_NUM_57402 &&	\
	 (chip_num) <= CHIP_NUM_57406)

#define BNXT_CHIP_NUM_5731X(chip_num)		\
	((chip_num) == CHIP_NUM_57311 ||	\
	 (chip_num) == CHIP_NUM_57312 ||	\
	 (chip_num) == CHIP_NUM_57314)

#define BNXT_CHIP_NUM_5741X(chip_num)		\
	((chip_num) >= CHIP_NUM_57412 &&	\
	 (chip_num) <= CHIP_NUM_57417)

#define BNXT_CHIP_NUM_57X0X(chip_num)		\
	(BNXT_CHIP_NUM_5730X(chip_num) || BNXT_CHIP_NUM_5740X(chip_num))

#define BNXT_CHIP_NUM_57X1X(chip_num)		\
	(BNXT_CHIP_NUM_5731X(chip_num) || BNXT_CHIP_NUM_5741X(chip_num))

	struct net_device	*dev;
	struct pci_dev		*pdev;

	atomic_t		intr_sem;

	u32			flags;
	#define BNXT_FLAG_DCB_ENABLED	0x1
	#define BNXT_FLAG_VF		0x2
	#define BNXT_FLAG_LRO		0x4
#ifdef CONFIG_INET
	#define BNXT_FLAG_GRO		0x8
#else
	/* Cannot support hardware GRO if CONFIG_INET is not set */
	#define BNXT_FLAG_GRO		0x0
#endif
	#define BNXT_FLAG_TPA		(BNXT_FLAG_LRO | BNXT_FLAG_GRO)
	#define BNXT_FLAG_JUMBO		0x10
	#define BNXT_FLAG_STRIP_VLAN	0x20
	#define BNXT_FLAG_AGG_RINGS	(BNXT_FLAG_JUMBO | BNXT_FLAG_GRO | \
					 BNXT_FLAG_LRO)
	#define BNXT_FLAG_USING_MSIX	0x40
	#define BNXT_FLAG_MSIX_CAP	0x80
	#define BNXT_FLAG_RFS		0x100
	#define BNXT_FLAG_SHARED_RINGS	0x200
	#define BNXT_FLAG_PORT_STATS	0x400
	#define BNXT_FLAG_EEE_CAP	0x1000
	#define BNXT_FLAG_CHIP_NITRO_A0	0x1000000

	#define BNXT_FLAG_ALL_CONFIG_FEATS (BNXT_FLAG_TPA |		\
					    BNXT_FLAG_RFS |		\
					    BNXT_FLAG_STRIP_VLAN)

#define BNXT_PF(bp)		(!((bp)->flags & BNXT_FLAG_VF))
#define BNXT_VF(bp)		((bp)->flags & BNXT_FLAG_VF)
#define BNXT_NPAR(bp)		((bp)->port_partition_type)
#define BNXT_SINGLE_PF(bp)	(BNXT_PF(bp) && !BNXT_NPAR(bp))
#define BNXT_CHIP_TYPE_NITRO_A0(bp) ((bp)->flags & BNXT_FLAG_CHIP_NITRO_A0)

	struct bnxt_napi	**bnapi;

	struct bnxt_rx_ring_info	*rx_ring;
	struct bnxt_tx_ring_info	*tx_ring;

	struct sk_buff *	(*gro_func)(struct bnxt_tpa_info *, int, int,
					    struct sk_buff *);

	u32			rx_buf_size;
	u32			rx_buf_use_size;	/* useable size */
	u32			rx_ring_size;
	u32			rx_agg_ring_size;
	u32			rx_copy_thresh;
	u32			rx_ring_mask;
	u32			rx_agg_ring_mask;
	int			rx_nr_pages;
	int			rx_agg_nr_pages;
	int			rx_nr_rings;
	int			rsscos_nr_ctxs;

	u32			tx_ring_size;
	u32			tx_ring_mask;
	int			tx_nr_pages;
	int			tx_nr_rings;
	int			tx_nr_rings_per_tc;

	int			tx_wake_thresh;
	int			tx_push_thresh;
	int			tx_push_size;

	u32			cp_ring_size;
	u32			cp_ring_mask;
	u32			cp_bit;
	int			cp_nr_pages;
	int			cp_nr_rings;

	int			num_stat_ctxs;

	/* grp_info indexed by completion ring index */
	struct bnxt_ring_grp_info	*grp_info;
	struct bnxt_vnic_info	*vnic_info;
	int			nr_vnics;

	u8			max_tc;
	struct bnxt_queue_info	q_info[BNXT_MAX_QUEUE];

	unsigned int		current_interval;
#define BNXT_TIMER_INTERVAL	HZ

	struct timer_list	timer;

	unsigned long		state;
#define BNXT_STATE_OPEN		0
#define BNXT_STATE_IN_SP_TASK	1
#define BNXT_STATE_FN_RST_DONE	2

	struct bnxt_irq	*irq_tbl;
	u8			mac_addr[ETH_ALEN];

	u32			msg_enable;

	u32			hwrm_spec_code;
	u16			hwrm_cmd_seq;
	u32			hwrm_intr_seq_id;
	void			*hwrm_cmd_resp_addr;
	dma_addr_t		hwrm_cmd_resp_dma_addr;
	void			*hwrm_dbg_resp_addr;
	dma_addr_t		hwrm_dbg_resp_dma_addr;
#define HWRM_DBG_REG_BUF_SIZE	128

	struct rx_port_stats	*hw_rx_port_stats;
	struct tx_port_stats	*hw_tx_port_stats;
	dma_addr_t		hw_rx_port_stats_map;
	dma_addr_t		hw_tx_port_stats_map;
	int			hw_port_stats_size;

	u16			hwrm_max_req_len;
	int			hwrm_cmd_timeout;
	struct mutex		hwrm_cmd_lock;	/* serialize hwrm messages */
	struct hwrm_ver_get_output	ver_resp;
#define FW_VER_STR_LEN		32
#define BC_HWRM_STR_LEN		21
#define PHY_VER_STR_LEN         (FW_VER_STR_LEN - BC_HWRM_STR_LEN)
	char			fw_ver_str[FW_VER_STR_LEN];
	__be16			vxlan_port;
	u8			vxlan_port_cnt;
	__le16			vxlan_fw_dst_port_id;
	__be16			nge_port;
	u8			nge_port_cnt;
	__le16			nge_fw_dst_port_id;
	u8			port_partition_type;

	u16			rx_coal_ticks;
	u16			rx_coal_ticks_irq;
	u16			rx_coal_bufs;
	u16			rx_coal_bufs_irq;
	u16			tx_coal_ticks;
	u16			tx_coal_ticks_irq;
	u16			tx_coal_bufs;
	u16			tx_coal_bufs_irq;

#define BNXT_USEC_TO_COAL_TIMER(x)	((x) * 25 / 2)

	u32			stats_coal_ticks;
#define BNXT_DEF_STATS_COAL_TICKS	 1000000
#define BNXT_MIN_STATS_COAL_TICKS	  250000
#define BNXT_MAX_STATS_COAL_TICKS	 1000000

	struct work_struct	sp_task;
	unsigned long		sp_event;
#define BNXT_RX_MASK_SP_EVENT		0
#define BNXT_RX_NTP_FLTR_SP_EVENT	1
#define BNXT_LINK_CHNG_SP_EVENT		2
#define BNXT_HWRM_EXEC_FWD_REQ_SP_EVENT	3
#define BNXT_VXLAN_ADD_PORT_SP_EVENT	4
#define BNXT_VXLAN_DEL_PORT_SP_EVENT	5
#define BNXT_RESET_TASK_SP_EVENT	6
#define BNXT_RST_RING_SP_EVENT		7
#define BNXT_HWRM_PF_UNLOAD_SP_EVENT	8
#define BNXT_PERIODIC_STATS_SP_EVENT	9
#define BNXT_HWRM_PORT_MODULE_SP_EVENT	10
#define BNXT_RESET_TASK_SILENT_SP_EVENT	11
#define BNXT_GENEVE_ADD_PORT_SP_EVENT	12
#define BNXT_GENEVE_DEL_PORT_SP_EVENT	13

	struct bnxt_pf_info	pf;
#ifdef CONFIG_BNXT_SRIOV
	int			nr_vfs;
	struct bnxt_vf_info	vf;
	wait_queue_head_t	sriov_cfg_wait;
	bool			sriov_cfg;
#define BNXT_SRIOV_CFG_WAIT_TMO	msecs_to_jiffies(10000)
#endif

#define BNXT_NTP_FLTR_MAX_FLTR	4096
#define BNXT_NTP_FLTR_HASH_SIZE	512
#define BNXT_NTP_FLTR_HASH_MASK	(BNXT_NTP_FLTR_HASH_SIZE - 1)
	struct hlist_head	ntp_fltr_hash_tbl[BNXT_NTP_FLTR_HASH_SIZE];
	spinlock_t		ntp_fltr_lock;	/* for hash table add, del */

	unsigned long		*ntp_fltr_bmap;
	int			ntp_fltr_count;

	struct bnxt_link_info	link_info;
	struct ethtool_eee	eee;
	u32			lpi_tmr_lo;
	u32			lpi_tmr_hi;
};

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void bnxt_enable_poll(struct bnxt_napi *bnapi)
{
	atomic_set(&bnapi->poll_state, BNXT_STATE_IDLE);
}

/* called from the NAPI poll routine to get ownership of a bnapi */
static inline bool bnxt_lock_napi(struct bnxt_napi *bnapi)
{
	int rc = atomic_cmpxchg(&bnapi->poll_state, BNXT_STATE_IDLE,
				BNXT_STATE_NAPI);

	return rc == BNXT_STATE_IDLE;
}

static inline void bnxt_unlock_napi(struct bnxt_napi *bnapi)
{
	atomic_set(&bnapi->poll_state, BNXT_STATE_IDLE);
}

/* called from the busy poll routine to get ownership of a bnapi */
static inline bool bnxt_lock_poll(struct bnxt_napi *bnapi)
{
	int rc = atomic_cmpxchg(&bnapi->poll_state, BNXT_STATE_IDLE,
				BNXT_STATE_POLL);

	return rc == BNXT_STATE_IDLE;
}

static inline void bnxt_unlock_poll(struct bnxt_napi *bnapi)
{
	atomic_set(&bnapi->poll_state, BNXT_STATE_IDLE);
}

static inline bool bnxt_busy_polling(struct bnxt_napi *bnapi)
{
	return atomic_read(&bnapi->poll_state) == BNXT_STATE_POLL;
}

static inline void bnxt_disable_poll(struct bnxt_napi *bnapi)
{
	int old;

	while (1) {
		old = atomic_cmpxchg(&bnapi->poll_state, BNXT_STATE_IDLE,
				     BNXT_STATE_DISABLE);
		if (old == BNXT_STATE_IDLE)
			break;
		usleep_range(500, 5000);
	}
}

#else

static inline void bnxt_enable_poll(struct bnxt_napi *bnapi)
{
}

static inline bool bnxt_lock_napi(struct bnxt_napi *bnapi)
{
	return true;
}

static inline void bnxt_unlock_napi(struct bnxt_napi *bnapi)
{
}

static inline bool bnxt_lock_poll(struct bnxt_napi *bnapi)
{
	return false;
}

static inline void bnxt_unlock_poll(struct bnxt_napi *bnapi)
{
}

static inline bool bnxt_busy_polling(struct bnxt_napi *bnapi)
{
	return false;
}

static inline void bnxt_disable_poll(struct bnxt_napi *bnapi)
{
}

#endif

#define I2C_DEV_ADDR_A0				0xa0
#define I2C_DEV_ADDR_A2				0xa2
#define SFP_EEPROM_SFF_8472_COMP_ADDR		0x5e
#define SFP_EEPROM_SFF_8472_COMP_SIZE		1
#define SFF_MODULE_ID_SFP			0x3
#define SFF_MODULE_ID_QSFP			0xc
#define SFF_MODULE_ID_QSFP_PLUS			0xd
#define SFF_MODULE_ID_QSFP28			0x11
#define BNXT_MAX_PHY_I2C_RESP_SIZE		64

void bnxt_set_ring_params(struct bnxt *);
void bnxt_hwrm_cmd_hdr_init(struct bnxt *, void *, u16, u16, u16);
int _hwrm_send_message(struct bnxt *, void *, u32, int);
int hwrm_send_message(struct bnxt *, void *, u32, int);
int hwrm_send_message_silent(struct bnxt *, void *, u32, int);
int bnxt_hwrm_set_coal(struct bnxt *);
int bnxt_hwrm_func_qcaps(struct bnxt *);
int bnxt_hwrm_set_pause(struct bnxt *);
int bnxt_hwrm_set_link_setting(struct bnxt *, bool, bool);
int bnxt_hwrm_fw_set_time(struct bnxt *);
int bnxt_open_nic(struct bnxt *, bool, bool);
int bnxt_close_nic(struct bnxt *, bool, bool);
int bnxt_get_max_rings(struct bnxt *, int *, int *, bool);
#endif
