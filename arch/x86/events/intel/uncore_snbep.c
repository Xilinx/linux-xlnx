/* SandyBridge-EP/IvyTown uncore support */
#include "uncore.h"

/* SNB-EP pci bus to socket mapping */
#define SNBEP_CPUNODEID			0x40
#define SNBEP_GIDNIDMAP			0x54

/* SNB-EP Box level control */
#define SNBEP_PMON_BOX_CTL_RST_CTRL	(1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS	(1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ		(1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN	(1 << 16)
#define SNBEP_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS | \
					 SNBEP_PMON_BOX_CTL_FRZ_EN)
/* SNB-EP event control */
#define SNBEP_PMON_CTL_EV_SEL_MASK	0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK	0x0000ff00
#define SNBEP_PMON_CTL_RST		(1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET		(1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT	(1 << 21)
#define SNBEP_PMON_CTL_EN		(1 << 22)
#define SNBEP_PMON_CTL_INVERT		(1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK	0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK	(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_INVERT | \
					 SNBEP_PMON_CTL_TRESH_MASK)

/* SNB-EP Ubox event control */
#define SNBEP_U_MSR_PMON_CTL_TRESH_MASK		0x1f000000
#define SNBEP_U_MSR_PMON_RAW_EVENT_MASK		\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)

#define SNBEP_CBO_PMON_CTL_TID_EN		(1 << 19)
#define SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK	(SNBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

/* SNB-EP PCU event control */
#define SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK	0x0000c000
#define SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK	0x1f000000
#define SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT	(1 << 30)
#define SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET	(1 << 31)
#define SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PMON_CTL_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)

#define SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_RAW_EVENT_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT)

/* SNB-EP pci control register */
#define SNBEP_PCI_PMON_BOX_CTL			0xf4
#define SNBEP_PCI_PMON_CTL0			0xd8
/* SNB-EP pci counter register */
#define SNBEP_PCI_PMON_CTR0			0xa0

/* SNB-EP home agent register */
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0	0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1	0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH	0x48
/* SNB-EP memory controller register */
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTL		0xf0
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTR		0xd0
/* SNB-EP QPI register */
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH0		0x228
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH1		0x22c
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK0		0x238
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK1		0x23c

/* SNB-EP Ubox register */
#define SNBEP_U_MSR_PMON_CTR0			0xc16
#define SNBEP_U_MSR_PMON_CTL0			0xc10

#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTL		0xc08
#define SNBEP_U_MSR_PMON_UCLK_FIXED_CTR		0xc09

/* SNB-EP Cbo register */
#define SNBEP_C0_MSR_PMON_CTR0			0xd16
#define SNBEP_C0_MSR_PMON_CTL0			0xd10
#define SNBEP_C0_MSR_PMON_BOX_CTL		0xd04
#define SNBEP_C0_MSR_PMON_BOX_FILTER		0xd14
#define SNBEP_CBO_MSR_OFFSET			0x20

#define SNBEP_CB0_MSR_PMON_BOX_FILTER_TID	0x1f
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_NID	0x3fc00
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_STATE	0x7c0000
#define SNBEP_CB0_MSR_PMON_BOX_FILTER_OPC	0xff800000

#define SNBEP_CBO_EVENT_EXTRA_REG(e, m, i) {	\
	.event = (e),				\
	.msr = SNBEP_C0_MSR_PMON_BOX_FILTER,	\
	.config_mask = (m),			\
	.idx = (i)				\
}

/* SNB-EP PCU register */
#define SNBEP_PCU_MSR_PMON_CTR0			0xc36
#define SNBEP_PCU_MSR_PMON_CTL0			0xc30
#define SNBEP_PCU_MSR_PMON_BOX_CTL		0xc24
#define SNBEP_PCU_MSR_PMON_BOX_FILTER		0xc34
#define SNBEP_PCU_MSR_PMON_BOX_FILTER_MASK	0xffffffff
#define SNBEP_PCU_MSR_CORE_C3_CTR		0x3fc
#define SNBEP_PCU_MSR_CORE_C6_CTR		0x3fd

/* IVBEP event control */
#define IVBEP_PMON_BOX_CTL_INT		(SNBEP_PMON_BOX_CTL_RST_CTRL | \
					 SNBEP_PMON_BOX_CTL_RST_CTRS)
#define IVBEP_PMON_RAW_EVENT_MASK		(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_TRESH_MASK)
/* IVBEP Ubox */
#define IVBEP_U_MSR_PMON_GLOBAL_CTL		0xc00
#define IVBEP_U_PMON_GLOBAL_FRZ_ALL		(1 << 31)
#define IVBEP_U_PMON_GLOBAL_UNFRZ_ALL		(1 << 29)

#define IVBEP_U_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PMON_CTL_UMASK_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_U_MSR_PMON_CTL_TRESH_MASK)
/* IVBEP Cbo */
#define IVBEP_CBO_MSR_PMON_RAW_EVENT_MASK		(IVBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

#define IVBEP_CB0_MSR_PMON_BOX_FILTER_TID		(0x1fULL << 0)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 5)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_STATE	(0x3fULL << 17)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_NID		(0xffffULL << 32)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_OPC		(0x1ffULL << 52)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define IVBEP_CB0_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)

/* IVBEP home agent */
#define IVBEP_HA_PCI_PMON_CTL_Q_OCC_RST		(1 << 16)
#define IVBEP_HA_PCI_PMON_RAW_EVENT_MASK		\
				(IVBEP_PMON_RAW_EVENT_MASK | \
				 IVBEP_HA_PCI_PMON_CTL_Q_OCC_RST)
/* IVBEP PCU */
#define IVBEP_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(SNBEP_PMON_CTL_EV_SEL_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)
/* IVBEP QPI */
#define IVBEP_QPI_PCI_PMON_RAW_EVENT_MASK	\
				(IVBEP_PMON_RAW_EVENT_MASK | \
				 SNBEP_PMON_CTL_EV_SEL_EXT)

#define __BITS_VALUE(x, i, n)  ((typeof(x))(((x) >> ((i) * (n))) & \
				((1ULL << (n)) - 1)))

/* Haswell-EP Ubox */
#define HSWEP_U_MSR_PMON_CTR0			0x709
#define HSWEP_U_MSR_PMON_CTL0			0x705
#define HSWEP_U_MSR_PMON_FILTER			0x707

#define HSWEP_U_MSR_PMON_UCLK_FIXED_CTL		0x703
#define HSWEP_U_MSR_PMON_UCLK_FIXED_CTR		0x704

#define HSWEP_U_MSR_PMON_BOX_FILTER_TID		(0x1 << 0)
#define HSWEP_U_MSR_PMON_BOX_FILTER_CID		(0x1fULL << 1)
#define HSWEP_U_MSR_PMON_BOX_FILTER_MASK \
					(HSWEP_U_MSR_PMON_BOX_FILTER_TID | \
					 HSWEP_U_MSR_PMON_BOX_FILTER_CID)

/* Haswell-EP CBo */
#define HSWEP_C0_MSR_PMON_CTR0			0xe08
#define HSWEP_C0_MSR_PMON_CTL0			0xe01
#define HSWEP_C0_MSR_PMON_BOX_CTL			0xe00
#define HSWEP_C0_MSR_PMON_BOX_FILTER0		0xe05
#define HSWEP_CBO_MSR_OFFSET			0x10


#define HSWEP_CB0_MSR_PMON_BOX_FILTER_TID		(0x3fULL << 0)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 6)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_STATE	(0x7fULL << 17)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_NID		(0xffffULL << 32)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_OPC		(0x1ffULL << 52)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define HSWEP_CB0_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)


/* Haswell-EP Sbox */
#define HSWEP_S0_MSR_PMON_CTR0			0x726
#define HSWEP_S0_MSR_PMON_CTL0			0x721
#define HSWEP_S0_MSR_PMON_BOX_CTL			0x720
#define HSWEP_SBOX_MSR_OFFSET			0xa
#define HSWEP_S_MSR_PMON_RAW_EVENT_MASK		(SNBEP_PMON_RAW_EVENT_MASK | \
						 SNBEP_CBO_PMON_CTL_TID_EN)

/* Haswell-EP PCU */
#define HSWEP_PCU_MSR_PMON_CTR0			0x717
#define HSWEP_PCU_MSR_PMON_CTL0			0x711
#define HSWEP_PCU_MSR_PMON_BOX_CTL		0x710
#define HSWEP_PCU_MSR_PMON_BOX_FILTER		0x715

/* KNL Ubox */
#define KNL_U_MSR_PMON_RAW_EVENT_MASK \
					(SNBEP_U_MSR_PMON_RAW_EVENT_MASK | \
						SNBEP_CBO_PMON_CTL_TID_EN)
/* KNL CHA */
#define KNL_CHA_MSR_OFFSET			0xc
#define KNL_CHA_MSR_PMON_CTL_QOR		(1 << 16)
#define KNL_CHA_MSR_PMON_RAW_EVENT_MASK \
					(SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK | \
					 KNL_CHA_MSR_PMON_CTL_QOR)
#define KNL_CHA_MSR_PMON_BOX_FILTER_TID		0x1ff
#define KNL_CHA_MSR_PMON_BOX_FILTER_STATE	(7 << 18)
#define KNL_CHA_MSR_PMON_BOX_FILTER_OP		(0xfffffe2aULL << 32)
#define KNL_CHA_MSR_PMON_BOX_FILTER_REMOTE_NODE	(0x1ULL << 32)
#define KNL_CHA_MSR_PMON_BOX_FILTER_LOCAL_NODE	(0x1ULL << 33)
#define KNL_CHA_MSR_PMON_BOX_FILTER_NNC		(0x1ULL << 37)

/* KNL EDC/MC UCLK */
#define KNL_UCLK_MSR_PMON_CTR0_LOW		0x400
#define KNL_UCLK_MSR_PMON_CTL0			0x420
#define KNL_UCLK_MSR_PMON_BOX_CTL		0x430
#define KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW	0x44c
#define KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL	0x454
#define KNL_PMON_FIXED_CTL_EN			0x1

/* KNL EDC */
#define KNL_EDC0_ECLK_MSR_PMON_CTR0_LOW		0xa00
#define KNL_EDC0_ECLK_MSR_PMON_CTL0		0xa20
#define KNL_EDC0_ECLK_MSR_PMON_BOX_CTL		0xa30
#define KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_LOW	0xa3c
#define KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_CTL	0xa44

/* KNL MC */
#define KNL_MC0_CH0_MSR_PMON_CTR0_LOW		0xb00
#define KNL_MC0_CH0_MSR_PMON_CTL0		0xb20
#define KNL_MC0_CH0_MSR_PMON_BOX_CTL		0xb30
#define KNL_MC0_CH0_MSR_PMON_FIXED_LOW		0xb3c
#define KNL_MC0_CH0_MSR_PMON_FIXED_CTL		0xb44

/* KNL IRP */
#define KNL_IRP_PCI_PMON_BOX_CTL		0xf0
#define KNL_IRP_PCI_PMON_RAW_EVENT_MASK		(SNBEP_PMON_RAW_EVENT_MASK | \
						 KNL_CHA_MSR_PMON_CTL_QOR)
/* KNL PCU */
#define KNL_PCU_PMON_CTL_EV_SEL_MASK		0x0000007f
#define KNL_PCU_PMON_CTL_USE_OCC_CTR		(1 << 7)
#define KNL_PCU_MSR_PMON_CTL_TRESH_MASK		0x3f000000
#define KNL_PCU_MSR_PMON_RAW_EVENT_MASK	\
				(KNL_PCU_PMON_CTL_EV_SEL_MASK | \
				 KNL_PCU_PMON_CTL_USE_OCC_CTR | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
				 SNBEP_PMON_CTL_EDGE_DET | \
				 SNBEP_CBO_PMON_CTL_TID_EN | \
				 SNBEP_PMON_CTL_INVERT | \
				 KNL_PCU_MSR_PMON_CTL_TRESH_MASK | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
				 SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)

/* SKX pci bus to socket mapping */
#define SKX_CPUNODEID			0xc0
#define SKX_GIDNIDMAP			0xd4

/* SKX CHA */
#define SKX_CHA_MSR_PMON_BOX_FILTER_TID		(0x1ffULL << 0)
#define SKX_CHA_MSR_PMON_BOX_FILTER_LINK	(0xfULL << 9)
#define SKX_CHA_MSR_PMON_BOX_FILTER_STATE	(0x3ffULL << 17)
#define SKX_CHA_MSR_PMON_BOX_FILTER_REM		(0x1ULL << 32)
#define SKX_CHA_MSR_PMON_BOX_FILTER_LOC		(0x1ULL << 33)
#define SKX_CHA_MSR_PMON_BOX_FILTER_ALL_OPC	(0x1ULL << 35)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NM		(0x1ULL << 36)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NOT_NM	(0x1ULL << 37)
#define SKX_CHA_MSR_PMON_BOX_FILTER_OPC0	(0x3ffULL << 41)
#define SKX_CHA_MSR_PMON_BOX_FILTER_OPC1	(0x3ffULL << 51)
#define SKX_CHA_MSR_PMON_BOX_FILTER_C6		(0x1ULL << 61)
#define SKX_CHA_MSR_PMON_BOX_FILTER_NC		(0x1ULL << 62)
#define SKX_CHA_MSR_PMON_BOX_FILTER_ISOC	(0x1ULL << 63)

/* SKX IIO */
#define SKX_IIO0_MSR_PMON_CTL0		0xa48
#define SKX_IIO0_MSR_PMON_CTR0		0xa41
#define SKX_IIO0_MSR_PMON_BOX_CTL	0xa40
#define SKX_IIO_MSR_OFFSET		0x20

#define SKX_PMON_CTL_TRESH_MASK		(0xff << 24)
#define SKX_PMON_CTL_TRESH_MASK_EXT	(0xf)
#define SKX_PMON_CTL_CH_MASK		(0xff << 4)
#define SKX_PMON_CTL_FC_MASK		(0x7 << 12)
#define SKX_IIO_PMON_RAW_EVENT_MASK	(SNBEP_PMON_CTL_EV_SEL_MASK | \
					 SNBEP_PMON_CTL_UMASK_MASK | \
					 SNBEP_PMON_CTL_EDGE_DET | \
					 SNBEP_PMON_CTL_INVERT | \
					 SKX_PMON_CTL_TRESH_MASK)
#define SKX_IIO_PMON_RAW_EVENT_MASK_EXT	(SKX_PMON_CTL_TRESH_MASK_EXT | \
					 SKX_PMON_CTL_CH_MASK | \
					 SKX_PMON_CTL_FC_MASK)

/* SKX IRP */
#define SKX_IRP0_MSR_PMON_CTL0		0xa5b
#define SKX_IRP0_MSR_PMON_CTR0		0xa59
#define SKX_IRP0_MSR_PMON_BOX_CTL	0xa58
#define SKX_IRP_MSR_OFFSET		0x20

/* SKX UPI */
#define SKX_UPI_PCI_PMON_CTL0		0x350
#define SKX_UPI_PCI_PMON_CTR0		0x318
#define SKX_UPI_PCI_PMON_BOX_CTL	0x378
#define SKX_PMON_CTL_UMASK_EXT		0xff

/* SKX M2M */
#define SKX_M2M_PCI_PMON_CTL0		0x228
#define SKX_M2M_PCI_PMON_CTR0		0x200
#define SKX_M2M_PCI_PMON_BOX_CTL	0x258

DEFINE_UNCORE_FORMAT_ATTR(event, event, "config:0-7");
DEFINE_UNCORE_FORMAT_ATTR(event2, event, "config:0-6");
DEFINE_UNCORE_FORMAT_ATTR(event_ext, event, "config:0-7,21");
DEFINE_UNCORE_FORMAT_ATTR(use_occ_ctr, use_occ_ctr, "config:7");
DEFINE_UNCORE_FORMAT_ATTR(umask, umask, "config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(umask_ext, umask, "config:8-15,32-39");
DEFINE_UNCORE_FORMAT_ATTR(qor, qor, "config:16");
DEFINE_UNCORE_FORMAT_ATTR(edge, edge, "config:18");
DEFINE_UNCORE_FORMAT_ATTR(tid_en, tid_en, "config:19");
DEFINE_UNCORE_FORMAT_ATTR(inv, inv, "config:23");
DEFINE_UNCORE_FORMAT_ATTR(thresh9, thresh, "config:24-35");
DEFINE_UNCORE_FORMAT_ATTR(thresh8, thresh, "config:24-31");
DEFINE_UNCORE_FORMAT_ATTR(thresh6, thresh, "config:24-29");
DEFINE_UNCORE_FORMAT_ATTR(thresh5, thresh, "config:24-28");
DEFINE_UNCORE_FORMAT_ATTR(occ_sel, occ_sel, "config:14-15");
DEFINE_UNCORE_FORMAT_ATTR(occ_invert, occ_invert, "config:30");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge, occ_edge, "config:14-51");
DEFINE_UNCORE_FORMAT_ATTR(occ_edge_det, occ_edge_det, "config:31");
DEFINE_UNCORE_FORMAT_ATTR(ch_mask, ch_mask, "config:36-43");
DEFINE_UNCORE_FORMAT_ATTR(fc_mask, fc_mask, "config:44-46");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid, filter_tid, "config1:0-4");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid2, filter_tid, "config1:0");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid3, filter_tid, "config1:0-5");
DEFINE_UNCORE_FORMAT_ATTR(filter_tid4, filter_tid, "config1:0-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_cid, filter_cid, "config1:5");
DEFINE_UNCORE_FORMAT_ATTR(filter_link, filter_link, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_link2, filter_link, "config1:6-8");
DEFINE_UNCORE_FORMAT_ATTR(filter_link3, filter_link, "config1:12");
DEFINE_UNCORE_FORMAT_ATTR(filter_link4, filter_link, "config1:9-12");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid, filter_nid, "config1:10-17");
DEFINE_UNCORE_FORMAT_ATTR(filter_nid2, filter_nid, "config1:32-47");
DEFINE_UNCORE_FORMAT_ATTR(filter_state, filter_state, "config1:18-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_state2, filter_state, "config1:17-22");
DEFINE_UNCORE_FORMAT_ATTR(filter_state3, filter_state, "config1:17-23");
DEFINE_UNCORE_FORMAT_ATTR(filter_state4, filter_state, "config1:18-20");
DEFINE_UNCORE_FORMAT_ATTR(filter_state5, filter_state, "config1:17-26");
DEFINE_UNCORE_FORMAT_ATTR(filter_rem, filter_rem, "config1:32");
DEFINE_UNCORE_FORMAT_ATTR(filter_loc, filter_loc, "config1:33");
DEFINE_UNCORE_FORMAT_ATTR(filter_nm, filter_nm, "config1:36");
DEFINE_UNCORE_FORMAT_ATTR(filter_not_nm, filter_not_nm, "config1:37");
DEFINE_UNCORE_FORMAT_ATTR(filter_local, filter_local, "config1:33");
DEFINE_UNCORE_FORMAT_ATTR(filter_all_op, filter_all_op, "config1:35");
DEFINE_UNCORE_FORMAT_ATTR(filter_nnm, filter_nnm, "config1:37");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc, filter_opc, "config1:23-31");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc2, filter_opc, "config1:52-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc3, filter_opc, "config1:41-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc_0, filter_opc0, "config1:41-50");
DEFINE_UNCORE_FORMAT_ATTR(filter_opc_1, filter_opc1, "config1:51-60");
DEFINE_UNCORE_FORMAT_ATTR(filter_nc, filter_nc, "config1:62");
DEFINE_UNCORE_FORMAT_ATTR(filter_c6, filter_c6, "config1:61");
DEFINE_UNCORE_FORMAT_ATTR(filter_isoc, filter_isoc, "config1:63");
DEFINE_UNCORE_FORMAT_ATTR(filter_band0, filter_band0, "config1:0-7");
DEFINE_UNCORE_FORMAT_ATTR(filter_band1, filter_band1, "config1:8-15");
DEFINE_UNCORE_FORMAT_ATTR(filter_band2, filter_band2, "config1:16-23");
DEFINE_UNCORE_FORMAT_ATTR(filter_band3, filter_band3, "config1:24-31");
DEFINE_UNCORE_FORMAT_ATTR(match_rds, match_rds, "config1:48-51");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid30, match_rnid30, "config1:32-35");
DEFINE_UNCORE_FORMAT_ATTR(match_rnid4, match_rnid4, "config1:31");
DEFINE_UNCORE_FORMAT_ATTR(match_dnid, match_dnid, "config1:13-17");
DEFINE_UNCORE_FORMAT_ATTR(match_mc, match_mc, "config1:9-12");
DEFINE_UNCORE_FORMAT_ATTR(match_opc, match_opc, "config1:5-8");
DEFINE_UNCORE_FORMAT_ATTR(match_vnw, match_vnw, "config1:3-4");
DEFINE_UNCORE_FORMAT_ATTR(match0, match0, "config1:0-31");
DEFINE_UNCORE_FORMAT_ATTR(match1, match1, "config1:32-63");
DEFINE_UNCORE_FORMAT_ATTR(mask_rds, mask_rds, "config2:48-51");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid30, mask_rnid30, "config2:32-35");
DEFINE_UNCORE_FORMAT_ATTR(mask_rnid4, mask_rnid4, "config2:31");
DEFINE_UNCORE_FORMAT_ATTR(mask_dnid, mask_dnid, "config2:13-17");
DEFINE_UNCORE_FORMAT_ATTR(mask_mc, mask_mc, "config2:9-12");
DEFINE_UNCORE_FORMAT_ATTR(mask_opc, mask_opc, "config2:5-8");
DEFINE_UNCORE_FORMAT_ATTR(mask_vnw, mask_vnw, "config2:3-4");
DEFINE_UNCORE_FORMAT_ATTR(mask0, mask0, "config2:0-31");
DEFINE_UNCORE_FORMAT_ATTR(mask1, mask1, "config2:32-63");

static void snbep_uncore_pci_disable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);
	u32 config = 0;

	if (!pci_read_config_dword(pdev, box_ctl, &config)) {
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		pci_write_config_dword(pdev, box_ctl, config);
	}
}

static void snbep_uncore_pci_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_pci_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, hwc->config_base, hwc->config);
}

static u64 snbep_uncore_pci_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hwc->event_base, (u32 *)&count);
	pci_read_config_dword(pdev, hwc->event_base + 4, (u32 *)&count + 1);

	return count;
}

static void snbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, SNBEP_PMON_BOX_CTL_INT);
}

static void snbep_uncore_msr_disable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config |= SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_box(struct intel_uncore_box *box)
{
	u64 config;
	unsigned msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config &= ~SNBEP_PMON_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void snbep_uncore_msr_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE)
		wrmsrl(reg1->reg, uncore_shared_reg_config(box, 0));

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static void snbep_uncore_msr_disable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
}

static void snbep_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);

	if (msr)
		wrmsrl(msr, SNBEP_PMON_BOX_CTL_INT);
}

static struct attribute *snbep_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *snbep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *snbep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_nid.attr,
	&format_attr_filter_state.attr,
	&format_attr_filter_opc.attr,
	NULL,
};

static struct attribute *snbep_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *snbep_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static struct uncore_event_desc snbep_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0xff,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x03"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.unit, "MiB"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x0c"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.unit, "MiB"),
	{ /* end: all zeroes */ },
};

static struct uncore_event_desc snbep_uncore_qpi_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,       "event=0x14"),
	INTEL_UNCORE_EVENT_DESC(txl_flits_active, "event=0x00,umask=0x06"),
	INTEL_UNCORE_EVENT_DESC(drs_data,         "event=0x102,umask=0x08"),
	INTEL_UNCORE_EVENT_DESC(ncb_data,         "event=0x103,umask=0x04"),
	{ /* end: all zeroes */ },
};

static struct attribute_group snbep_uncore_format_group = {
	.name = "format",
	.attrs = snbep_uncore_formats_attr,
};

static struct attribute_group snbep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_ubox_formats_attr,
};

static struct attribute_group snbep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = snbep_uncore_cbox_formats_attr,
};

static struct attribute_group snbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = snbep_uncore_pcu_formats_attr,
};

static struct attribute_group snbep_uncore_qpi_format_group = {
	.name = "format",
	.attrs = snbep_uncore_qpi_formats_attr,
};

#define __SNBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

#define SNBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	__SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),			\
	.init_box	= snbep_uncore_msr_init_box		\

static struct intel_uncore_ops snbep_uncore_msr_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
};

#define SNBEP_UNCORE_PCI_OPS_COMMON_INIT()			\
	.init_box	= snbep_uncore_pci_init_box,		\
	.disable_box	= snbep_uncore_pci_disable_box,		\
	.enable_box	= snbep_uncore_pci_enable_box,		\
	.disable_event	= snbep_uncore_pci_disable_event,	\
	.read_counter	= snbep_uncore_pci_read_counter

static struct intel_uncore_ops snbep_uncore_pci_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event	= snbep_uncore_pci_enable_event,	\
};

static struct event_constraint snbep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x02, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x04, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x05, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1b, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1c, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1d, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0xc),
	EVENT_CONSTRAINT_OVERLAP(0x1f, 0xe, 0xff),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x35, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x3b, 0x1),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	EVENT_CONSTRAINT_END
};

static struct event_constraint snbep_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2a, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2b, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x30, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type snbep_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &snbep_uncore_msr_ops,
	.format_group	= &snbep_uncore_ubox_format_group,
};

static struct extra_reg snbep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0x6),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0xa),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x2),
	EVENT_EXTRA_END
};

static void snbep_cbox_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i;

	if (uncore_box_is_fake(box))
		return;

	for (i = 0; i < 5; i++) {
		if (reg1->alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	reg1->alloc = 0;
}

static struct event_constraint *
__snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event,
			    u64 (*cbox_filter_mask)(int fields))
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	int i, alloc = 0;
	unsigned long flags;
	u64 mask;

	if (reg1->idx == EXTRA_REG_NONE)
		return NULL;

	raw_spin_lock_irqsave(&er->lock, flags);
	for (i = 0; i < 5; i++) {
		if (!(reg1->idx & (0x1 << i)))
			continue;
		if (!uncore_box_is_fake(box) && (reg1->alloc & (0x1 << i)))
			continue;

		mask = cbox_filter_mask(0x1 << i);
		if (!__BITS_VALUE(atomic_read(&er->ref), i, 6) ||
		    !((reg1->config ^ er->config) & mask)) {
			atomic_add(1 << (i * 6), &er->ref);
			er->config &= ~mask;
			er->config |= reg1->config & mask;
			alloc |= (0x1 << i);
		} else {
			break;
		}
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);
	if (i < 5)
		goto fail;

	if (!uncore_box_is_fake(box))
		reg1->alloc |= alloc;

	return NULL;
fail:
	for (; i >= 0; i--) {
		if (alloc & (0x1 << i))
			atomic_sub(1 << (i * 6), &er->ref);
	}
	return &uncore_constraint_empty;
}

static u64 snbep_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x4)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= SNBEP_CB0_MSR_PMON_BOX_FILTER_OPC;

	return mask;
}

static struct event_constraint *
snbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, snbep_cbox_filter_mask);
}

static int snbep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = snbep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & snbep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_cbox_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_cbox_hw_config,
	.get_constraint		= snbep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type snbep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &snbep_uncore_cbox_ops,
	.format_group		= &snbep_uncore_cbox_format_group,
};

static u64 snbep_pcu_alter_er(struct perf_event *event, int new_idx, bool modify)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	u64 config = reg1->config;

	if (new_idx > reg1->idx)
		config <<= 8 * (new_idx - reg1->idx);
	else
		config >>= 8 * (reg1->idx - new_idx);

	if (modify) {
		hwc->config += new_idx - reg1->idx;
		reg1->config = config;
		reg1->idx = new_idx;
	}
	return config;
}

static struct event_constraint *
snbep_pcu_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];
	unsigned long flags;
	int idx = reg1->idx;
	u64 mask, config1 = reg1->config;
	bool ok = false;

	if (reg1->idx == EXTRA_REG_NONE ||
	    (!uncore_box_is_fake(box) && reg1->alloc))
		return NULL;
again:
	mask = 0xffULL << (idx * 8);
	raw_spin_lock_irqsave(&er->lock, flags);
	if (!__BITS_VALUE(atomic_read(&er->ref), idx, 8) ||
	    !((config1 ^ er->config) & mask)) {
		atomic_add(1 << (idx * 8), &er->ref);
		er->config &= ~mask;
		er->config |= config1 & mask;
		ok = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (!ok) {
		idx = (idx + 1) % 4;
		if (idx != reg1->idx) {
			config1 = snbep_pcu_alter_er(event, idx, false);
			goto again;
		}
		return &uncore_constraint_empty;
	}

	if (!uncore_box_is_fake(box)) {
		if (idx != reg1->idx)
			snbep_pcu_alter_er(event, idx, true);
		reg1->alloc = 1;
	}
	return NULL;
}

static void snbep_pcu_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct intel_uncore_extra_reg *er = &box->shared_regs[0];

	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	atomic_sub(1 << (reg1->idx * 8), &er->ref);
	reg1->alloc = 0;
}

static int snbep_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = SNBEP_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << (reg1->idx * 8));
	}
	return 0;
}

static struct intel_uncore_ops snbep_uncore_pcu_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type snbep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *snbep_msr_uncores[] = {
	&snbep_uncore_ubox,
	&snbep_uncore_cbox,
	&snbep_uncore_pcu,
	NULL,
};

void snbep_uncore_cpu_init(void)
{
	if (snbep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		snbep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = snbep_msr_uncores;
}

enum {
	SNBEP_PCI_QPI_PORT0_FILTER,
	SNBEP_PCI_QPI_PORT1_FILTER,
	HSWEP_PCI_PCU_3,
};

static int snbep_qpi_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if ((hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK) == 0x38) {
		reg1->idx = 0;
		reg1->reg = SNBEP_Q_Py_PCI_PMON_PKT_MATCH0;
		reg1->config = event->attr.config1;
		reg2->reg = SNBEP_Q_Py_PCI_PMON_PKT_MASK0;
		reg2->config = event->attr.config2;
	}
	return 0;
}

static void snbep_qpi_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	struct hw_perf_event_extra *reg2 = &hwc->branch_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		int idx = box->pmu->pmu_idx + SNBEP_PCI_QPI_PORT0_FILTER;
		int pkg = topology_phys_to_logical_pkg(box->pci_phys_id);
		struct pci_dev *filter_pdev = uncore_extra_pci_dev[pkg].dev[idx];

		if (filter_pdev) {
			pci_write_config_dword(filter_pdev, reg1->reg,
						(u32)reg1->config);
			pci_write_config_dword(filter_pdev, reg1->reg + 4,
						(u32)(reg1->config >> 32));
			pci_write_config_dword(filter_pdev, reg2->reg,
						(u32)reg2->config);
			pci_write_config_dword(filter_pdev, reg2->reg + 4,
						(u32)(reg2->config >> 32));
		}
	}

	pci_write_config_dword(pdev, hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops snbep_uncore_qpi_ops = {
	SNBEP_UNCORE_PCI_OPS_COMMON_INIT(),
	.enable_event		= snbep_qpi_enable_event,
	.hw_config		= snbep_qpi_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

#define SNBEP_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &snbep_uncore_pci_ops,		\
	.format_group	= &snbep_uncore_format_group

static struct intel_uncore_type snbep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= snbep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.event_descs		= snbep_uncore_qpi_events,
	.format_group		= &snbep_uncore_qpi_format_group,
};


static struct intel_uncore_type snbep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type snbep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	SNBEP_PCI_UNCORE_HA,
	SNBEP_PCI_UNCORE_IMC,
	SNBEP_PCI_UNCORE_QPI,
	SNBEP_PCI_UNCORE_R2PCIE,
	SNBEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *snbep_pci_uncores[] = {
	[SNBEP_PCI_UNCORE_HA]		= &snbep_uncore_ha,
	[SNBEP_PCI_UNCORE_IMC]		= &snbep_uncore_imc,
	[SNBEP_PCI_UNCORE_QPI]		= &snbep_uncore_qpi,
	[SNBEP_PCI_UNCORE_R2PCIE]	= &snbep_uncore_r2pcie,
	[SNBEP_PCI_UNCORE_R3QPI]	= &snbep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id snbep_uncore_pci_ids[] = {
	{ /* Home Agent */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_HA),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_HA, 0),
	},
	{ /* MC Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC2),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_IMC3),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_IMC, 3),
	},
	{ /* QPI Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_QPI, 1),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R2PCIE),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI0),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_UNC_R3QPI1),
		.driver_data = UNCORE_PCI_DEV_DATA(SNBEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3c96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver snbep_uncore_pci_driver = {
	.name		= "snbep_uncore",
	.id_table	= snbep_uncore_pci_ids,
};

/*
 * build pci bus to socket mapping
 */
static int snbep_pci2phy_map_init(int devid, int nodeid_loc, int idmap_loc, bool reverse)
{
	struct pci_dev *ubox_dev = NULL;
	int i, bus, nodeid, segment;
	struct pci2phy_map *map;
	int err = 0;
	u32 config = 0;

	while (1) {
		/* find the UBOX device */
		ubox_dev = pci_get_device(PCI_VENDOR_ID_INTEL, devid, ubox_dev);
		if (!ubox_dev)
			break;
		bus = ubox_dev->bus->number;
		/* get the Node ID of the local register */
		err = pci_read_config_dword(ubox_dev, nodeid_loc, &config);
		if (err)
			break;
		nodeid = config;
		/* get the Node ID mapping */
		err = pci_read_config_dword(ubox_dev, idmap_loc, &config);
		if (err)
			break;

		segment = pci_domain_nr(ubox_dev->bus);
		raw_spin_lock(&pci2phy_map_lock);
		map = __find_pci2phy_map(segment);
		if (!map) {
			raw_spin_unlock(&pci2phy_map_lock);
			err = -ENOMEM;
			break;
		}

		/*
		 * every three bits in the Node ID mapping register maps
		 * to a particular node.
		 */
		for (i = 0; i < 8; i++) {
			if (nodeid == ((config >> (3 * i)) & 0x7)) {
				map->pbus_to_physid[bus] = i;
				break;
			}
		}
		raw_spin_unlock(&pci2phy_map_lock);
	}

	if (!err) {
		/*
		 * For PCI bus with no UBOX device, find the next bus
		 * that has UBOX device and use its mapping.
		 */
		raw_spin_lock(&pci2phy_map_lock);
		list_for_each_entry(map, &pci2phy_map_head, list) {
			i = -1;
			if (reverse) {
				for (bus = 255; bus >= 0; bus--) {
					if (map->pbus_to_physid[bus] >= 0)
						i = map->pbus_to_physid[bus];
					else
						map->pbus_to_physid[bus] = i;
				}
			} else {
				for (bus = 0; bus <= 255; bus++) {
					if (map->pbus_to_physid[bus] >= 0)
						i = map->pbus_to_physid[bus];
					else
						map->pbus_to_physid[bus] = i;
				}
			}
		}
		raw_spin_unlock(&pci2phy_map_lock);
	}

	pci_dev_put(ubox_dev);

	return err ? pcibios_err_to_errno(err) : 0;
}

int snbep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x3ce0, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = snbep_pci_uncores;
	uncore_pci_driver = &snbep_uncore_pci_driver;
	return 0;
}
/* end of Sandy Bridge-EP uncore support */

/* IvyTown uncore support */
static void ivbep_uncore_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);
	if (msr)
		wrmsrl(msr, IVBEP_PMON_BOX_CTL_INT);
}

static void ivbep_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	pci_write_config_dword(pdev, SNBEP_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

#define IVBEP_UNCORE_MSR_OPS_COMMON_INIT()			\
	.init_box	= ivbep_uncore_msr_init_box,		\
	.disable_box	= snbep_uncore_msr_disable_box,		\
	.enable_box	= snbep_uncore_msr_enable_box,		\
	.disable_event	= snbep_uncore_msr_disable_event,	\
	.enable_event	= snbep_uncore_msr_enable_event,	\
	.read_counter	= uncore_msr_read_counter

static struct intel_uncore_ops ivbep_uncore_msr_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
};

static struct intel_uncore_ops ivbep_uncore_pci_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

#define IVBEP_UNCORE_PCI_COMMON_INIT()				\
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,			\
	.event_ctl	= SNBEP_PCI_PMON_CTL0,			\
	.event_mask	= IVBEP_PMON_RAW_EVENT_MASK,		\
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,		\
	.ops		= &ivbep_uncore_pci_ops,			\
	.format_group	= &ivbep_uncore_format_group

static struct attribute *ivbep_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute *ivbep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute *ivbep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid.attr,
	&format_attr_filter_link.attr,
	&format_attr_filter_state2.attr,
	&format_attr_filter_nid2.attr,
	&format_attr_filter_opc2.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_c6.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static struct attribute *ivbep_uncore_pcu_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_thresh5.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge.attr,
	&format_attr_filter_band0.attr,
	&format_attr_filter_band1.attr,
	&format_attr_filter_band2.attr,
	&format_attr_filter_band3.attr,
	NULL,
};

static struct attribute *ivbep_uncore_qpi_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_thresh8.attr,
	&format_attr_match_rds.attr,
	&format_attr_match_rnid30.attr,
	&format_attr_match_rnid4.attr,
	&format_attr_match_dnid.attr,
	&format_attr_match_mc.attr,
	&format_attr_match_opc.attr,
	&format_attr_match_vnw.attr,
	&format_attr_match0.attr,
	&format_attr_match1.attr,
	&format_attr_mask_rds.attr,
	&format_attr_mask_rnid30.attr,
	&format_attr_mask_rnid4.attr,
	&format_attr_mask_dnid.attr,
	&format_attr_mask_mc.attr,
	&format_attr_mask_opc.attr,
	&format_attr_mask_vnw.attr,
	&format_attr_mask0.attr,
	&format_attr_mask1.attr,
	NULL,
};

static struct attribute_group ivbep_uncore_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_formats_attr,
};

static struct attribute_group ivbep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_ubox_formats_attr,
};

static struct attribute_group ivbep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_cbox_formats_attr,
};

static struct attribute_group ivbep_uncore_pcu_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_pcu_formats_attr,
};

static struct attribute_group ivbep_uncore_qpi_format_group = {
	.name = "format",
	.attrs = ivbep_uncore_qpi_formats_attr,
};

static struct intel_uncore_type ivbep_uncore_ubox = {
	.name		= "ubox",
	.num_counters   = 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.fixed_ctr_bits	= 48,
	.perf_ctr	= SNBEP_U_MSR_PMON_CTR0,
	.event_ctl	= SNBEP_U_MSR_PMON_CTL0,
	.event_mask	= IVBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= SNBEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops		= &ivbep_uncore_msr_ops,
	.format_group	= &ivbep_uncore_ubox_format_group,
};

static struct extra_reg ivbep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1031, 0x10ff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5134, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4334, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4534, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4934, 0xffff, 0xc),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5036, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x8),
	EVENT_EXTRA_END
};

static u64 ivbep_cbox_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x10) {
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_OPC;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_NC;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_C6;
		mask |= IVBEP_CB0_MSR_PMON_BOX_FILTER_ISOC;
	}

	return mask;
}

static struct event_constraint *
ivbep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, ivbep_cbox_filter_mask);
}

static int ivbep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = ivbep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = SNBEP_C0_MSR_PMON_BOX_FILTER +
			SNBEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & ivbep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static void ivbep_cbox_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		u64 filter = uncore_shared_reg_config(box, 0);
		wrmsrl(reg1->reg, filter & 0xffffffff);
		wrmsrl(reg1->reg + 6, filter >> 32);
	}

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops ivbep_uncore_cbox_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= ivbep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= ivbep_cbox_hw_config,
	.get_constraint		= ivbep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 15,
	.perf_ctr_bits		= 44,
	.event_ctl		= SNBEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= SNBEP_C0_MSR_PMON_CTR0,
	.event_mask		= IVBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= SNBEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= snbep_uncore_cbox_constraints,
	.ops			= &ivbep_uncore_cbox_ops,
	.format_group		= &ivbep_uncore_cbox_format_group,
};

static struct intel_uncore_ops ivbep_uncore_pcu_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= snbep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= SNBEP_PCU_MSR_PMON_CTL0,
	.event_mask		= IVBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_pcu_ops,
	.format_group		= &ivbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *ivbep_msr_uncores[] = {
	&ivbep_uncore_ubox,
	&ivbep_uncore_cbox,
	&ivbep_uncore_pcu,
	NULL,
};

void ivbep_uncore_cpu_init(void)
{
	if (ivbep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		ivbep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = ivbep_msr_uncores;
}

static struct intel_uncore_type ivbep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivbep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= snbep_uncore_imc_events,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

/* registers in IRP boxes are not properly aligned */
static unsigned ivbep_uncore_irp_ctls[] = {0xd8, 0xdc, 0xe0, 0xe4};
static unsigned ivbep_uncore_irp_ctrs[] = {0xa0, 0xb0, 0xb8, 0xc0};

static void ivbep_uncore_irp_enable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivbep_uncore_irp_ctls[hwc->idx],
			       hwc->config | SNBEP_PMON_CTL_EN);
}

static void ivbep_uncore_irp_disable_event(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	pci_write_config_dword(pdev, ivbep_uncore_irp_ctls[hwc->idx], hwc->config);
}

static u64 ivbep_uncore_irp_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, ivbep_uncore_irp_ctrs[hwc->idx], (u32 *)&count);
	pci_read_config_dword(pdev, ivbep_uncore_irp_ctrs[hwc->idx] + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops ivbep_uncore_irp_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= ivbep_uncore_irp_disable_event,
	.enable_event	= ivbep_uncore_irp_enable_event,
	.read_counter	= ivbep_uncore_irp_read_counter,
};

static struct intel_uncore_type ivbep_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= IVBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &ivbep_uncore_irp_ops,
	.format_group		= &ivbep_uncore_format_group,
};

static struct intel_uncore_ops ivbep_uncore_qpi_ops = {
	.init_box	= ivbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_qpi_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
	.hw_config	= snbep_qpi_hw_config,
	.get_constraint	= uncore_get_constraint,
	.put_constraint	= uncore_put_constraint,
};

static struct intel_uncore_type ivbep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= IVBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_qpi_ops,
	.format_group		= &ivbep_uncore_qpi_format_group,
};

static struct intel_uncore_type ivbep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r2pcie_constraints,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type ivbep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 2,
	.perf_ctr_bits	= 44,
	.constraints	= snbep_uncore_r3qpi_constraints,
	IVBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	IVBEP_PCI_UNCORE_HA,
	IVBEP_PCI_UNCORE_IMC,
	IVBEP_PCI_UNCORE_IRP,
	IVBEP_PCI_UNCORE_QPI,
	IVBEP_PCI_UNCORE_R2PCIE,
	IVBEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *ivbep_pci_uncores[] = {
	[IVBEP_PCI_UNCORE_HA]	= &ivbep_uncore_ha,
	[IVBEP_PCI_UNCORE_IMC]	= &ivbep_uncore_imc,
	[IVBEP_PCI_UNCORE_IRP]	= &ivbep_uncore_irp,
	[IVBEP_PCI_UNCORE_QPI]	= &ivbep_uncore_qpi,
	[IVBEP_PCI_UNCORE_R2PCIE]	= &ivbep_uncore_r2pcie,
	[IVBEP_PCI_UNCORE_R3QPI]	= &ivbep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id ivbep_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe30),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe38),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xeb1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef4),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef5),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef0),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 4 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xef1),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe39),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe32),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe33),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3a),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe34),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe36),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe37),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe3e),
		.driver_data = UNCORE_PCI_DEV_DATA(IVBEP_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0xe96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver ivbep_uncore_pci_driver = {
	.name		= "ivbep_uncore",
	.id_table	= ivbep_uncore_pci_ids,
};

int ivbep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x0e1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = ivbep_pci_uncores;
	uncore_pci_driver = &ivbep_uncore_pci_driver;
	return 0;
}
/* end of IvyTown uncore support */

/* KNL uncore support */
static struct attribute *knl_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	NULL,
};

static struct attribute_group knl_uncore_ubox_format_group = {
	.name = "format",
	.attrs = knl_uncore_ubox_formats_attr,
};

static struct intel_uncore_type knl_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= KNL_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops			= &snbep_uncore_msr_ops,
	.format_group		= &knl_uncore_ubox_format_group,
};

static struct attribute *knl_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_qor.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid4.attr,
	&format_attr_filter_link3.attr,
	&format_attr_filter_state4.attr,
	&format_attr_filter_local.attr,
	&format_attr_filter_all_op.attr,
	&format_attr_filter_nnm.attr,
	&format_attr_filter_opc3.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static struct attribute_group knl_uncore_cha_format_group = {
	.name = "format",
	.attrs = knl_uncore_cha_formats_attr,
};

static struct event_constraint knl_uncore_cha_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg knl_uncore_cha_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x3d, 0xff, 0x2),
	SNBEP_CBO_EVENT_EXTRA_REG(0x35, 0xff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x36, 0xff, 0x4),
	EVENT_EXTRA_END
};

static u64 knl_cha_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x4)
		mask |= KNL_CHA_MSR_PMON_BOX_FILTER_OP;
	return mask;
}

static struct event_constraint *
knl_cha_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, knl_cha_filter_mask);
}

static int knl_cha_hw_config(struct intel_uncore_box *box,
			     struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = knl_uncore_cha_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    KNL_CHA_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & knl_cha_filter_mask(idx);

		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_REMOTE_NODE;
		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_LOCAL_NODE;
		reg1->config |= KNL_CHA_MSR_PMON_BOX_FILTER_NNC;
		reg1->idx = idx;
	}
	return 0;
}

static void hswep_cbox_enable_event(struct intel_uncore_box *box,
				    struct perf_event *event);

static struct intel_uncore_ops knl_uncore_cha_ops = {
	.init_box		= snbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= knl_cha_hw_config,
	.get_constraint		= knl_cha_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type knl_uncore_cha = {
	.name			= "cha",
	.num_counters		= 4,
	.num_boxes		= 38,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= KNL_CHA_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= KNL_CHA_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= knl_uncore_cha_constraints,
	.ops			= &knl_uncore_cha_ops,
	.format_group		= &knl_uncore_cha_format_group,
};

static struct attribute *knl_uncore_pcu_formats_attr[] = {
	&format_attr_event2.attr,
	&format_attr_use_occ_ctr.attr,
	&format_attr_occ_sel.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh6.attr,
	&format_attr_occ_invert.attr,
	&format_attr_occ_edge_det.attr,
	NULL,
};

static struct attribute_group knl_uncore_pcu_format_group = {
	.name = "format",
	.attrs = knl_uncore_pcu_formats_attr,
};

static struct intel_uncore_type knl_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= KNL_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.ops			= &snbep_uncore_msr_ops,
	.format_group		= &knl_uncore_pcu_format_group,
};

static struct intel_uncore_type *knl_msr_uncores[] = {
	&knl_uncore_ubox,
	&knl_uncore_cha,
	&knl_uncore_pcu,
	NULL,
};

void knl_uncore_cpu_init(void)
{
	uncore_msr_uncores = knl_msr_uncores;
}

static void knl_uncore_imc_enable_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;
	int box_ctl = uncore_pci_box_ctl(box);

	pci_write_config_dword(pdev, box_ctl, 0);
}

static void knl_uncore_imc_enable_event(struct intel_uncore_box *box,
					struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;

	if ((event->attr.config & SNBEP_PMON_CTL_EV_SEL_MASK)
							== UNCORE_FIXED_EVENT)
		pci_write_config_dword(pdev, hwc->config_base,
				       hwc->config | KNL_PMON_FIXED_CTL_EN);
	else
		pci_write_config_dword(pdev, hwc->config_base,
				       hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops knl_uncore_imc_ops = {
	.init_box	= snbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= knl_uncore_imc_enable_box,
	.read_counter	= snbep_uncore_pci_read_counter,
	.enable_event	= knl_uncore_imc_enable_event,
	.disable_event	= snbep_uncore_pci_disable_event,
};

static struct intel_uncore_type knl_uncore_imc_uclk = {
	.name			= "imc_uclk",
	.num_counters		= 4,
	.num_boxes		= 2,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_UCLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_UCLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW,
	.fixed_ctl		= KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL,
	.box_ctl		= KNL_UCLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_imc_dclk = {
	.name			= "imc",
	.num_counters		= 4,
	.num_boxes		= 6,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_MC0_CH0_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_MC0_CH0_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_MC0_CH0_MSR_PMON_FIXED_LOW,
	.fixed_ctl		= KNL_MC0_CH0_MSR_PMON_FIXED_CTL,
	.box_ctl		= KNL_MC0_CH0_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_edc_uclk = {
	.name			= "edc_uclk",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_UCLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_UCLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_UCLK_MSR_PMON_UCLK_FIXED_LOW,
	.fixed_ctl		= KNL_UCLK_MSR_PMON_UCLK_FIXED_CTL,
	.box_ctl		= KNL_UCLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type knl_uncore_edc_eclk = {
	.name			= "edc_eclk",
	.num_counters		= 4,
	.num_boxes		= 8,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= KNL_EDC0_ECLK_MSR_PMON_CTR0_LOW,
	.event_ctl		= KNL_EDC0_ECLK_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_LOW,
	.fixed_ctl		= KNL_EDC0_ECLK_MSR_PMON_ECLK_FIXED_CTL,
	.box_ctl		= KNL_EDC0_ECLK_MSR_PMON_BOX_CTL,
	.ops			= &knl_uncore_imc_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct event_constraint knl_uncore_m2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type knl_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= knl_uncore_m2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct attribute *knl_uncore_irp_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_qor.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute_group knl_uncore_irp_format_group = {
	.name = "format",
	.attrs = knl_uncore_irp_formats_attr,
};

static struct intel_uncore_type knl_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= KNL_IRP_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= KNL_IRP_PCI_PMON_BOX_CTL,
	.ops			= &snbep_uncore_pci_ops,
	.format_group		= &knl_uncore_irp_format_group,
};

enum {
	KNL_PCI_UNCORE_MC_UCLK,
	KNL_PCI_UNCORE_MC_DCLK,
	KNL_PCI_UNCORE_EDC_UCLK,
	KNL_PCI_UNCORE_EDC_ECLK,
	KNL_PCI_UNCORE_M2PCIE,
	KNL_PCI_UNCORE_IRP,
};

static struct intel_uncore_type *knl_pci_uncores[] = {
	[KNL_PCI_UNCORE_MC_UCLK]	= &knl_uncore_imc_uclk,
	[KNL_PCI_UNCORE_MC_DCLK]	= &knl_uncore_imc_dclk,
	[KNL_PCI_UNCORE_EDC_UCLK]	= &knl_uncore_edc_uclk,
	[KNL_PCI_UNCORE_EDC_ECLK]	= &knl_uncore_edc_eclk,
	[KNL_PCI_UNCORE_M2PCIE]		= &knl_uncore_m2pcie,
	[KNL_PCI_UNCORE_IRP]		= &knl_uncore_irp,
	NULL,
};

/*
 * KNL uses a common PCI device ID for multiple instances of an Uncore PMU
 * device type. prior to KNL, each instance of a PMU device type had a unique
 * device ID.
 *
 *	PCI Device ID	Uncore PMU Devices
 *	----------------------------------
 *	0x7841		MC0 UClk, MC1 UClk
 *	0x7843		MC0 DClk CH 0, MC0 DClk CH 1, MC0 DClk CH 2,
 *			MC1 DClk CH 0, MC1 DClk CH 1, MC1 DClk CH 2
 *	0x7833		EDC0 UClk, EDC1 UClk, EDC2 UClk, EDC3 UClk,
 *			EDC4 UClk, EDC5 UClk, EDC6 UClk, EDC7 UClk
 *	0x7835		EDC0 EClk, EDC1 EClk, EDC2 EClk, EDC3 EClk,
 *			EDC4 EClk, EDC5 EClk, EDC6 EClk, EDC7 EClk
 *	0x7817		M2PCIe
 *	0x7814		IRP
*/

static const struct pci_device_id knl_uncore_pci_ids[] = {
	{ /* MC0 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7841),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 0, KNL_PCI_UNCORE_MC_UCLK, 0),
	},
	{ /* MC1 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7841),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(11, 0, KNL_PCI_UNCORE_MC_UCLK, 1),
	},
	{ /* MC0 DClk CH 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 2, KNL_PCI_UNCORE_MC_DCLK, 0),
	},
	{ /* MC0 DClk CH 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 3, KNL_PCI_UNCORE_MC_DCLK, 1),
	},
	{ /* MC0 DClk CH 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 4, KNL_PCI_UNCORE_MC_DCLK, 2),
	},
	{ /* MC1 DClk CH 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 2, KNL_PCI_UNCORE_MC_DCLK, 3),
	},
	{ /* MC1 DClk CH 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 3, KNL_PCI_UNCORE_MC_DCLK, 4),
	},
	{ /* MC1 DClk CH 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7843),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 4, KNL_PCI_UNCORE_MC_DCLK, 5),
	},
	{ /* EDC0 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(15, 0, KNL_PCI_UNCORE_EDC_UCLK, 0),
	},
	{ /* EDC1 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(16, 0, KNL_PCI_UNCORE_EDC_UCLK, 1),
	},
	{ /* EDC2 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(17, 0, KNL_PCI_UNCORE_EDC_UCLK, 2),
	},
	{ /* EDC3 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 0, KNL_PCI_UNCORE_EDC_UCLK, 3),
	},
	{ /* EDC4 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(19, 0, KNL_PCI_UNCORE_EDC_UCLK, 4),
	},
	{ /* EDC5 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(20, 0, KNL_PCI_UNCORE_EDC_UCLK, 5),
	},
	{ /* EDC6 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 0, KNL_PCI_UNCORE_EDC_UCLK, 6),
	},
	{ /* EDC7 UClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7833),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(22, 0, KNL_PCI_UNCORE_EDC_UCLK, 7),
	},
	{ /* EDC0 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(24, 2, KNL_PCI_UNCORE_EDC_ECLK, 0),
	},
	{ /* EDC1 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(25, 2, KNL_PCI_UNCORE_EDC_ECLK, 1),
	},
	{ /* EDC2 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(26, 2, KNL_PCI_UNCORE_EDC_ECLK, 2),
	},
	{ /* EDC3 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(27, 2, KNL_PCI_UNCORE_EDC_ECLK, 3),
	},
	{ /* EDC4 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(28, 2, KNL_PCI_UNCORE_EDC_ECLK, 4),
	},
	{ /* EDC5 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(29, 2, KNL_PCI_UNCORE_EDC_ECLK, 5),
	},
	{ /* EDC6 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(30, 2, KNL_PCI_UNCORE_EDC_ECLK, 6),
	},
	{ /* EDC7 EClk */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7835),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(31, 2, KNL_PCI_UNCORE_EDC_ECLK, 7),
	},
	{ /* M2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7817),
		.driver_data = UNCORE_PCI_DEV_DATA(KNL_PCI_UNCORE_M2PCIE, 0),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x7814),
		.driver_data = UNCORE_PCI_DEV_DATA(KNL_PCI_UNCORE_IRP, 0),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver knl_uncore_pci_driver = {
	.name		= "knl_uncore",
	.id_table	= knl_uncore_pci_ids,
};

int knl_uncore_pci_init(void)
{
	int ret;

	/* All KNL PCI based PMON units are on the same PCI bus except IRP */
	ret = snb_pci2phy_map_init(0x7814); /* IRP */
	if (ret)
		return ret;
	ret = snb_pci2phy_map_init(0x7817); /* M2PCIe */
	if (ret)
		return ret;
	uncore_pci_uncores = knl_pci_uncores;
	uncore_pci_driver = &knl_uncore_pci_driver;
	return 0;
}

/* end of KNL uncore support */

/* Haswell-EP uncore support */
static struct attribute *hswep_uncore_ubox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh5.attr,
	&format_attr_filter_tid2.attr,
	&format_attr_filter_cid.attr,
	NULL,
};

static struct attribute_group hswep_uncore_ubox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_ubox_formats_attr,
};

static int hswep_ubox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	reg1->reg = HSWEP_U_MSR_PMON_FILTER;
	reg1->config = event->attr.config1 & HSWEP_U_MSR_PMON_BOX_FILTER_MASK;
	reg1->idx = 0;
	return 0;
}

static struct intel_uncore_ops hswep_uncore_ubox_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_ubox_hw_config,
	.get_constraint		= uncore_get_constraint,
	.put_constraint		= uncore_put_constraint,
};

static struct intel_uncore_type hswep_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 44,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.num_shared_regs	= 1,
	.ops			= &hswep_uncore_ubox_ops,
	.format_group		= &hswep_uncore_ubox_format_group,
};

static struct attribute *hswep_uncore_cbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid3.attr,
	&format_attr_filter_link2.attr,
	&format_attr_filter_state3.attr,
	&format_attr_filter_nid2.attr,
	&format_attr_filter_opc2.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_c6.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static struct attribute_group hswep_uncore_cbox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_cbox_formats_attr,
};

static struct event_constraint hswep_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x3b, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x3e, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg hswep_uncore_cbox_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(SNBEP_CBO_PMON_CTL_TID_EN,
				  SNBEP_CBO_PMON_CTL_TID_EN, 0x1),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4037, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4028, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4032, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4029, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4033, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x402A, 0x40ff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0135, 0xffff, 0x12),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4135, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4435, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4835, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5035, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4335, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a35, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8335, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8135, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4136, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4436, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4836, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4336, 0xffff, 0x18),
	SNBEP_CBO_EVENT_EXTRA_REG(0x4a36, 0xffff, 0x8),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8336, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8136, 0xffff, 0x10),
	SNBEP_CBO_EVENT_EXTRA_REG(0x5036, 0xffff, 0x8),
	EVENT_EXTRA_END
};

static u64 hswep_cbox_filter_mask(int fields)
{
	u64 mask = 0;
	if (fields & 0x1)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_STATE;
	if (fields & 0x8)
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_NID;
	if (fields & 0x10) {
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_OPC;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_NC;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_C6;
		mask |= HSWEP_CB0_MSR_PMON_BOX_FILTER_ISOC;
	}
	return mask;
}

static struct event_constraint *
hswep_cbox_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, hswep_cbox_filter_mask);
}

static int hswep_cbox_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = hswep_uncore_cbox_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    HSWEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & hswep_cbox_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static void hswep_cbox_enable_event(struct intel_uncore_box *box,
				  struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;

	if (reg1->idx != EXTRA_REG_NONE) {
		u64 filter = uncore_shared_reg_config(box, 0);
		wrmsrl(reg1->reg, filter & 0xffffffff);
		wrmsrl(reg1->reg + 1, filter >> 32);
	}

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops hswep_uncore_cbox_ops = {
	.init_box		= snbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= hswep_cbox_hw_config,
	.get_constraint		= hswep_cbox_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type hswep_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 18,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= hswep_uncore_cbox_constraints,
	.ops			= &hswep_uncore_cbox_ops,
	.format_group		= &hswep_uncore_cbox_format_group,
};

/*
 * Write SBOX Initialization register bit by bit to avoid spurious #GPs
 */
static void hswep_uncore_sbox_msr_init_box(struct intel_uncore_box *box)
{
	unsigned msr = uncore_msr_box_ctl(box);

	if (msr) {
		u64 init = SNBEP_PMON_BOX_CTL_INT;
		u64 flags = 0;
		int i;

		for_each_set_bit(i, (unsigned long *)&init, 64) {
			flags |= (1ULL << i);
			wrmsrl(msr, flags);
		}
	}
}

static struct intel_uncore_ops hswep_uncore_sbox_msr_ops = {
	__SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.init_box		= hswep_uncore_sbox_msr_init_box
};

static struct attribute *hswep_uncore_sbox_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute_group hswep_uncore_sbox_format_group = {
	.name = "format",
	.attrs = hswep_uncore_sbox_formats_attr,
};

static struct intel_uncore_type hswep_uncore_sbox = {
	.name			= "sbox",
	.num_counters		= 4,
	.num_boxes		= 4,
	.perf_ctr_bits		= 44,
	.event_ctl		= HSWEP_S0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_S0_MSR_PMON_CTR0,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_S0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_SBOX_MSR_OFFSET,
	.ops			= &hswep_uncore_sbox_msr_ops,
	.format_group		= &hswep_uncore_sbox_format_group,
};

static int hswep_pcu_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *reg1 = &hwc->extra_reg;
	int ev_sel = hwc->config & SNBEP_PMON_CTL_EV_SEL_MASK;

	if (ev_sel >= 0xb && ev_sel <= 0xe) {
		reg1->reg = HSWEP_PCU_MSR_PMON_BOX_FILTER;
		reg1->idx = ev_sel - 0xb;
		reg1->config = event->attr.config1 & (0xff << reg1->idx);
	}
	return 0;
}

static struct intel_uncore_ops hswep_uncore_pcu_ops = {
	SNBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type hswep_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &hswep_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *hswep_msr_uncores[] = {
	&hswep_uncore_ubox,
	&hswep_uncore_cbox,
	&hswep_uncore_sbox,
	&hswep_uncore_pcu,
	NULL,
};

void hswep_uncore_cpu_init(void)
{
	int pkg = topology_phys_to_logical_pkg(0);

	if (hswep_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		hswep_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;

	/* Detect 6-8 core systems with only two SBOXes */
	if (uncore_extra_pci_dev[pkg].dev[HSWEP_PCI_PCU_3]) {
		u32 capid4;

		pci_read_config_dword(uncore_extra_pci_dev[pkg].dev[HSWEP_PCI_PCU_3],
				      0x94, &capid4);
		if (((capid4 >> 6) & 0x3) == 0)
			hswep_uncore_sbox.num_boxes = 2;
	}

	uncore_msr_uncores = hswep_msr_uncores;
}

static struct intel_uncore_type hswep_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct uncore_event_desc hswep_uncore_imc_events[] = {
	INTEL_UNCORE_EVENT_DESC(clockticks,      "event=0x00,umask=0x00"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read,  "event=0x04,umask=0x03"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_read.unit, "MiB"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write, "event=0x04,umask=0x0c"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.scale, "6.103515625e-5"),
	INTEL_UNCORE_EVENT_DESC(cas_count_write.unit, "MiB"),
	{ /* end: all zeroes */ },
};

static struct intel_uncore_type hswep_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static unsigned hswep_uncore_irp_ctrs[] = {0xa0, 0xa8, 0xb0, 0xb8};

static u64 hswep_uncore_irp_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	struct pci_dev *pdev = box->pci_dev;
	struct hw_perf_event *hwc = &event->hw;
	u64 count = 0;

	pci_read_config_dword(pdev, hswep_uncore_irp_ctrs[hwc->idx], (u32 *)&count);
	pci_read_config_dword(pdev, hswep_uncore_irp_ctrs[hwc->idx] + 4, (u32 *)&count + 1);

	return count;
}

static struct intel_uncore_ops hswep_uncore_irp_ops = {
	.init_box	= snbep_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= ivbep_uncore_irp_disable_event,
	.enable_event	= ivbep_uncore_irp_enable_event,
	.read_counter	= hswep_uncore_irp_read_counter,
};

static struct intel_uncore_type hswep_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &hswep_uncore_irp_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type hswep_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.format_group		= &snbep_uncore_qpi_format_group,
};

static struct event_constraint hswep_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x24, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x27, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2a, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x2b, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x35, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type hswep_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= hswep_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct event_constraint hswep_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x08, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0a, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x12, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x14, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x15, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x31, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x32, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type hswep_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 44,
	.constraints	= hswep_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	HSWEP_PCI_UNCORE_HA,
	HSWEP_PCI_UNCORE_IMC,
	HSWEP_PCI_UNCORE_IRP,
	HSWEP_PCI_UNCORE_QPI,
	HSWEP_PCI_UNCORE_R2PCIE,
	HSWEP_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *hswep_pci_uncores[] = {
	[HSWEP_PCI_UNCORE_HA]	= &hswep_uncore_ha,
	[HSWEP_PCI_UNCORE_IMC]	= &hswep_uncore_imc,
	[HSWEP_PCI_UNCORE_IRP]	= &hswep_uncore_irp,
	[HSWEP_PCI_UNCORE_QPI]	= &hswep_uncore_qpi,
	[HSWEP_PCI_UNCORE_R2PCIE]	= &hswep_uncore_r2pcie,
	[HSWEP_PCI_UNCORE_R3QPI]	= &hswep_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id hswep_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f30),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f38),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb0),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb1),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb4),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fb5),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd0),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd1),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd4),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fd5),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f39),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f32),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f33),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f3a),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f34),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f36),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f37),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f3e),
		.driver_data = UNCORE_PCI_DEV_DATA(HSWEP_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT0_FILTER),
	},
	{ /* QPI Port 1 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2f96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   SNBEP_PCI_QPI_PORT1_FILTER),
	},
	{ /* PCU.3 (for Capability registers) */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2fc0),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV,
						   HSWEP_PCI_PCU_3),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver hswep_uncore_pci_driver = {
	.name		= "hswep_uncore",
	.id_table	= hswep_uncore_pci_ids,
};

int hswep_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x2f1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);
	if (ret)
		return ret;
	uncore_pci_uncores = hswep_pci_uncores;
	uncore_pci_driver = &hswep_uncore_pci_driver;
	return 0;
}
/* end of Haswell-EP uncore support */

/* BDX uncore support */

static struct intel_uncore_type bdx_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.num_shared_regs	= 1,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_ubox_format_group,
};

static struct event_constraint bdx_uncore_cbox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x09, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x3e, 0x1),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_cbox = {
	.name			= "cbox",
	.num_counters		= 4,
	.num_boxes		= 24,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= bdx_uncore_cbox_constraints,
	.ops			= &hswep_uncore_cbox_ops,
	.format_group		= &hswep_uncore_cbox_format_group,
};

static struct intel_uncore_type *bdx_msr_uncores[] = {
	&bdx_uncore_ubox,
	&bdx_uncore_cbox,
	&hswep_uncore_pcu,
	NULL,
};

void bdx_uncore_cpu_init(void)
{
	if (bdx_uncore_cbox.num_boxes > boot_cpu_data.x86_max_cores)
		bdx_uncore_cbox.num_boxes = boot_cpu_data.x86_max_cores;
	uncore_msr_uncores = bdx_msr_uncores;
}

static struct intel_uncore_type bdx_uncore_ha = {
	.name		= "ha",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type bdx_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 8,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct intel_uncore_type bdx_uncore_irp = {
	.name			= "irp",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.ops			= &hswep_uncore_irp_ops,
	.format_group		= &snbep_uncore_format_group,
};

static struct intel_uncore_type bdx_uncore_qpi = {
	.name			= "qpi",
	.num_counters		= 4,
	.num_boxes		= 3,
	.perf_ctr_bits		= 48,
	.perf_ctr		= SNBEP_PCI_PMON_CTR0,
	.event_ctl		= SNBEP_PCI_PMON_CTL0,
	.event_mask		= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.box_ctl		= SNBEP_PCI_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &snbep_uncore_qpi_ops,
	.format_group		= &snbep_uncore_qpi_format_group,
};

static struct event_constraint bdx_uncore_r2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_r2pcie = {
	.name		= "r2pcie",
	.num_counters   = 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.constraints	= bdx_uncore_r2pcie_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

static struct event_constraint bdx_uncore_r3qpi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x01, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x07, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x08, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x09, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0a, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x0e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x10, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x11, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x13, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x14, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x15, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x1f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x20, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x21, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x22, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x25, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x26, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x28, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x29, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2c, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2d, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2e, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x2f, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x33, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x34, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x37, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x38, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x39, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type bdx_uncore_r3qpi = {
	.name		= "r3qpi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.constraints	= bdx_uncore_r3qpi_constraints,
	SNBEP_UNCORE_PCI_COMMON_INIT(),
};

enum {
	BDX_PCI_UNCORE_HA,
	BDX_PCI_UNCORE_IMC,
	BDX_PCI_UNCORE_IRP,
	BDX_PCI_UNCORE_QPI,
	BDX_PCI_UNCORE_R2PCIE,
	BDX_PCI_UNCORE_R3QPI,
};

static struct intel_uncore_type *bdx_pci_uncores[] = {
	[BDX_PCI_UNCORE_HA]	= &bdx_uncore_ha,
	[BDX_PCI_UNCORE_IMC]	= &bdx_uncore_imc,
	[BDX_PCI_UNCORE_IRP]	= &bdx_uncore_irp,
	[BDX_PCI_UNCORE_QPI]	= &bdx_uncore_qpi,
	[BDX_PCI_UNCORE_R2PCIE]	= &bdx_uncore_r2pcie,
	[BDX_PCI_UNCORE_R3QPI]	= &bdx_uncore_r3qpi,
	NULL,
};

static const struct pci_device_id bdx_uncore_pci_ids[] = {
	{ /* Home Agent 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f30),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_HA, 0),
	},
	{ /* Home Agent 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f38),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_HA, 1),
	},
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb0),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb1),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb4),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 2),
	},
	{ /* MC0 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fb5),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd0),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd1),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 5),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd4),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 6),
	},
	{ /* MC1 Channel 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6fd5),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IMC, 7),
	},
	{ /* IRP */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f39),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_IRP, 0),
	},
	{ /* QPI0 Port 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f32),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 0),
	},
	{ /* QPI0 Port 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f33),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 1),
	},
	{ /* QPI1 Port 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f3a),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_QPI, 2),
	},
	{ /* R2PCIe */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f34),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R2PCIE, 0),
	},
	{ /* R3QPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f36),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 0),
	},
	{ /* R3QPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f37),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 1),
	},
	{ /* R3QPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f3e),
		.driver_data = UNCORE_PCI_DEV_DATA(BDX_PCI_UNCORE_R3QPI, 2),
	},
	{ /* QPI Port 0 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f86),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV, 0),
	},
	{ /* QPI Port 1 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f96),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV, 1),
	},
	{ /* QPI Port 2 filter  */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x6f46),
		.driver_data = UNCORE_PCI_DEV_DATA(UNCORE_EXTRA_PCI_DEV, 2),
	},
	{ /* end: all zeroes */ }
};

static struct pci_driver bdx_uncore_pci_driver = {
	.name		= "bdx_uncore",
	.id_table	= bdx_uncore_pci_ids,
};

int bdx_uncore_pci_init(void)
{
	int ret = snbep_pci2phy_map_init(0x6f1e, SNBEP_CPUNODEID, SNBEP_GIDNIDMAP, true);

	if (ret)
		return ret;
	uncore_pci_uncores = bdx_pci_uncores;
	uncore_pci_driver = &bdx_uncore_pci_driver;
	return 0;
}

/* end of BDX uncore support */

/* SKX uncore support */

static struct intel_uncore_type skx_uncore_ubox = {
	.name			= "ubox",
	.num_counters		= 2,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.fixed_ctr_bits		= 48,
	.perf_ctr		= HSWEP_U_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_U_MSR_PMON_CTL0,
	.event_mask		= SNBEP_U_MSR_PMON_RAW_EVENT_MASK,
	.fixed_ctr		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTR,
	.fixed_ctl		= HSWEP_U_MSR_PMON_UCLK_FIXED_CTL,
	.ops			= &ivbep_uncore_msr_ops,
	.format_group		= &ivbep_uncore_ubox_format_group,
};

static struct attribute *skx_uncore_cha_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_tid_en.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	&format_attr_filter_tid4.attr,
	&format_attr_filter_link4.attr,
	&format_attr_filter_state5.attr,
	&format_attr_filter_rem.attr,
	&format_attr_filter_loc.attr,
	&format_attr_filter_nm.attr,
	&format_attr_filter_all_op.attr,
	&format_attr_filter_not_nm.attr,
	&format_attr_filter_opc_0.attr,
	&format_attr_filter_opc_1.attr,
	&format_attr_filter_nc.attr,
	&format_attr_filter_c6.attr,
	&format_attr_filter_isoc.attr,
	NULL,
};

static struct attribute_group skx_uncore_chabox_format_group = {
	.name = "format",
	.attrs = skx_uncore_cha_formats_attr,
};

static struct event_constraint skx_uncore_chabox_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x11, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x36, 0x1),
	EVENT_CONSTRAINT_END
};

static struct extra_reg skx_uncore_cha_extra_regs[] = {
	SNBEP_CBO_EVENT_EXTRA_REG(0x0334, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0534, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x0934, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x1134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x2134, 0xffff, 0x4),
	SNBEP_CBO_EVENT_EXTRA_REG(0x8134, 0xffff, 0x4),
};

static u64 skx_cha_filter_mask(int fields)
{
	u64 mask = 0;

	if (fields & 0x1)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_TID;
	if (fields & 0x2)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_LINK;
	if (fields & 0x4)
		mask |= SKX_CHA_MSR_PMON_BOX_FILTER_STATE;
	return mask;
}

static struct event_constraint *
skx_cha_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	return __snbep_cbox_get_constraint(box, event, skx_cha_filter_mask);
}

static int skx_cha_hw_config(struct intel_uncore_box *box, struct perf_event *event)
{
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct extra_reg *er;
	int idx = 0;

	for (er = skx_uncore_cha_extra_regs; er->msr; er++) {
		if (er->event != (event->hw.config & er->config_mask))
			continue;
		idx |= er->idx;
	}

	if (idx) {
		reg1->reg = HSWEP_C0_MSR_PMON_BOX_FILTER0 +
			    HSWEP_CBO_MSR_OFFSET * box->pmu->pmu_idx;
		reg1->config = event->attr.config1 & skx_cha_filter_mask(idx);
		reg1->idx = idx;
	}
	return 0;
}

static struct intel_uncore_ops skx_uncore_chabox_ops = {
	/* There is no frz_en for chabox ctl */
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= hswep_cbox_enable_event,
	.read_counter		= uncore_msr_read_counter,
	.hw_config		= skx_cha_hw_config,
	.get_constraint		= skx_cha_get_constraint,
	.put_constraint		= snbep_cbox_put_constraint,
};

static struct intel_uncore_type skx_uncore_chabox = {
	.name			= "cha",
	.num_counters		= 4,
	.perf_ctr_bits		= 48,
	.event_ctl		= HSWEP_C0_MSR_PMON_CTL0,
	.perf_ctr		= HSWEP_C0_MSR_PMON_CTR0,
	.event_mask		= HSWEP_S_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_C0_MSR_PMON_BOX_CTL,
	.msr_offset		= HSWEP_CBO_MSR_OFFSET,
	.num_shared_regs	= 1,
	.constraints		= skx_uncore_chabox_constraints,
	.ops			= &skx_uncore_chabox_ops,
	.format_group		= &skx_uncore_chabox_format_group,
};

static struct attribute *skx_uncore_iio_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh9.attr,
	&format_attr_ch_mask.attr,
	&format_attr_fc_mask.attr,
	NULL,
};

static struct attribute_group skx_uncore_iio_format_group = {
	.name = "format",
	.attrs = skx_uncore_iio_formats_attr,
};

static struct event_constraint skx_uncore_iio_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x83, 0x3),
	UNCORE_EVENT_CONSTRAINT(0x88, 0xc),
	UNCORE_EVENT_CONSTRAINT(0x95, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc0, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xc5, 0xc),
	UNCORE_EVENT_CONSTRAINT(0xd4, 0xc),
	EVENT_CONSTRAINT_END
};

static void skx_iio_enable_event(struct intel_uncore_box *box,
				 struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config | SNBEP_PMON_CTL_EN);
}

static struct intel_uncore_ops skx_uncore_iio_ops = {
	.init_box		= ivbep_uncore_msr_init_box,
	.disable_box		= snbep_uncore_msr_disable_box,
	.enable_box		= snbep_uncore_msr_enable_box,
	.disable_event		= snbep_uncore_msr_disable_event,
	.enable_event		= skx_iio_enable_event,
	.read_counter		= uncore_msr_read_counter,
};

static struct intel_uncore_type skx_uncore_iio = {
	.name			= "iio",
	.num_counters		= 4,
	.num_boxes		= 5,
	.perf_ctr_bits		= 48,
	.event_ctl		= SKX_IIO0_MSR_PMON_CTL0,
	.perf_ctr		= SKX_IIO0_MSR_PMON_CTR0,
	.event_mask		= SKX_IIO_PMON_RAW_EVENT_MASK,
	.event_mask_ext		= SKX_IIO_PMON_RAW_EVENT_MASK_EXT,
	.box_ctl		= SKX_IIO0_MSR_PMON_BOX_CTL,
	.msr_offset		= SKX_IIO_MSR_OFFSET,
	.constraints		= skx_uncore_iio_constraints,
	.ops			= &skx_uncore_iio_ops,
	.format_group		= &skx_uncore_iio_format_group,
};

static struct attribute *skx_uncore_formats_attr[] = {
	&format_attr_event.attr,
	&format_attr_umask.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute_group skx_uncore_format_group = {
	.name = "format",
	.attrs = skx_uncore_formats_attr,
};

static struct intel_uncore_type skx_uncore_irp = {
	.name			= "irp",
	.num_counters		= 2,
	.num_boxes		= 5,
	.perf_ctr_bits		= 48,
	.event_ctl		= SKX_IRP0_MSR_PMON_CTL0,
	.perf_ctr		= SKX_IRP0_MSR_PMON_CTR0,
	.event_mask		= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl		= SKX_IRP0_MSR_PMON_BOX_CTL,
	.msr_offset		= SKX_IRP_MSR_OFFSET,
	.ops			= &skx_uncore_iio_ops,
	.format_group		= &skx_uncore_format_group,
};

static struct intel_uncore_ops skx_uncore_pcu_ops = {
	IVBEP_UNCORE_MSR_OPS_COMMON_INIT(),
	.hw_config		= hswep_pcu_hw_config,
	.get_constraint		= snbep_pcu_get_constraint,
	.put_constraint		= snbep_pcu_put_constraint,
};

static struct intel_uncore_type skx_uncore_pcu = {
	.name			= "pcu",
	.num_counters		= 4,
	.num_boxes		= 1,
	.perf_ctr_bits		= 48,
	.perf_ctr		= HSWEP_PCU_MSR_PMON_CTR0,
	.event_ctl		= HSWEP_PCU_MSR_PMON_CTL0,
	.event_mask		= SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK,
	.box_ctl		= HSWEP_PCU_MSR_PMON_BOX_CTL,
	.num_shared_regs	= 1,
	.ops			= &skx_uncore_pcu_ops,
	.format_group		= &snbep_uncore_pcu_format_group,
};

static struct intel_uncore_type *skx_msr_uncores[] = {
	&skx_uncore_ubox,
	&skx_uncore_chabox,
	&skx_uncore_iio,
	&skx_uncore_irp,
	&skx_uncore_pcu,
	NULL,
};

static int skx_count_chabox(void)
{
	struct pci_dev *chabox_dev = NULL;
	int bus, count = 0;

	while (1) {
		chabox_dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x208d, chabox_dev);
		if (!chabox_dev)
			break;
		if (count == 0)
			bus = chabox_dev->bus->number;
		if (bus != chabox_dev->bus->number)
			break;
		count++;
	}

	pci_dev_put(chabox_dev);
	return count;
}

void skx_uncore_cpu_init(void)
{
	skx_uncore_chabox.num_boxes = skx_count_chabox();
	uncore_msr_uncores = skx_msr_uncores;
}

static struct intel_uncore_type skx_uncore_imc = {
	.name		= "imc",
	.num_counters   = 4,
	.num_boxes	= 6,
	.perf_ctr_bits	= 48,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTR,
	.fixed_ctl	= SNBEP_MC_CHy_PCI_PMON_FIXED_CTL,
	.event_descs	= hswep_uncore_imc_events,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct attribute *skx_upi_uncore_formats_attr[] = {
	&format_attr_event_ext.attr,
	&format_attr_umask_ext.attr,
	&format_attr_edge.attr,
	&format_attr_inv.attr,
	&format_attr_thresh8.attr,
	NULL,
};

static struct attribute_group skx_upi_uncore_format_group = {
	.name = "format",
	.attrs = skx_upi_uncore_formats_attr,
};

static void skx_upi_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, SKX_UPI_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

static struct intel_uncore_ops skx_upi_uncore_pci_ops = {
	.init_box	= skx_upi_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct intel_uncore_type skx_uncore_upi = {
	.name		= "upi",
	.num_counters   = 4,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SKX_UPI_PCI_PMON_CTR0,
	.event_ctl	= SKX_UPI_PCI_PMON_CTL0,
	.event_mask	= SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK,
	.event_mask_ext = SKX_PMON_CTL_UMASK_EXT,
	.box_ctl	= SKX_UPI_PCI_PMON_BOX_CTL,
	.ops		= &skx_upi_uncore_pci_ops,
	.format_group	= &skx_upi_uncore_format_group,
};

static void skx_m2m_uncore_pci_init_box(struct intel_uncore_box *box)
{
	struct pci_dev *pdev = box->pci_dev;

	__set_bit(UNCORE_BOX_FLAG_CTL_OFFS8, &box->flags);
	pci_write_config_dword(pdev, SKX_M2M_PCI_PMON_BOX_CTL, IVBEP_PMON_BOX_CTL_INT);
}

static struct intel_uncore_ops skx_m2m_uncore_pci_ops = {
	.init_box	= skx_m2m_uncore_pci_init_box,
	.disable_box	= snbep_uncore_pci_disable_box,
	.enable_box	= snbep_uncore_pci_enable_box,
	.disable_event	= snbep_uncore_pci_disable_event,
	.enable_event	= snbep_uncore_pci_enable_event,
	.read_counter	= snbep_uncore_pci_read_counter,
};

static struct intel_uncore_type skx_uncore_m2m = {
	.name		= "m2m",
	.num_counters   = 4,
	.num_boxes	= 2,
	.perf_ctr_bits	= 48,
	.perf_ctr	= SKX_M2M_PCI_PMON_CTR0,
	.event_ctl	= SKX_M2M_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SKX_M2M_PCI_PMON_BOX_CTL,
	.ops		= &skx_m2m_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct event_constraint skx_uncore_m2pcie_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x23, 0x3),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type skx_uncore_m2pcie = {
	.name		= "m2pcie",
	.num_counters   = 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.constraints	= skx_uncore_m2pcie_constraints,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

static struct event_constraint skx_uncore_m3upi_constraints[] = {
	UNCORE_EVENT_CONSTRAINT(0x1d, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x1e, 0x1),
	UNCORE_EVENT_CONSTRAINT(0x40, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4e, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x4f, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x50, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x51, 0x7),
	UNCORE_EVENT_CONSTRAINT(0x52, 0x7),
	EVENT_CONSTRAINT_END
};

static struct intel_uncore_type skx_uncore_m3upi = {
	.name		= "m3upi",
	.num_counters   = 3,
	.num_boxes	= 3,
	.perf_ctr_bits	= 48,
	.constraints	= skx_uncore_m3upi_constraints,
	.perf_ctr	= SNBEP_PCI_PMON_CTR0,
	.event_ctl	= SNBEP_PCI_PMON_CTL0,
	.event_mask	= SNBEP_PMON_RAW_EVENT_MASK,
	.box_ctl	= SNBEP_PCI_PMON_BOX_CTL,
	.ops		= &ivbep_uncore_pci_ops,
	.format_group	= &skx_uncore_format_group,
};

enum {
	SKX_PCI_UNCORE_IMC,
	SKX_PCI_UNCORE_M2M,
	SKX_PCI_UNCORE_UPI,
	SKX_PCI_UNCORE_M2PCIE,
	SKX_PCI_UNCORE_M3UPI,
};

static struct intel_uncore_type *skx_pci_uncores[] = {
	[SKX_PCI_UNCORE_IMC]	= &skx_uncore_imc,
	[SKX_PCI_UNCORE_M2M]	= &skx_uncore_m2m,
	[SKX_PCI_UNCORE_UPI]	= &skx_uncore_upi,
	[SKX_PCI_UNCORE_M2PCIE]	= &skx_uncore_m2pcie,
	[SKX_PCI_UNCORE_M3UPI]	= &skx_uncore_m3upi,
	NULL,
};

static const struct pci_device_id skx_uncore_pci_ids[] = {
	{ /* MC0 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2042),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 2, SKX_PCI_UNCORE_IMC, 0),
	},
	{ /* MC0 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2046),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(10, 6, SKX_PCI_UNCORE_IMC, 1),
	},
	{ /* MC0 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(11, 2, SKX_PCI_UNCORE_IMC, 2),
	},
	{ /* MC1 Channel 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2042),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 2, SKX_PCI_UNCORE_IMC, 3),
	},
	{ /* MC1 Channel 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2046),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(12, 6, SKX_PCI_UNCORE_IMC, 4),
	},
	{ /* MC1 Channel 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204a),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(13, 2, SKX_PCI_UNCORE_IMC, 5),
	},
	{ /* M2M0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2066),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(8, 0, SKX_PCI_UNCORE_M2M, 0),
	},
	{ /* M2M1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2066),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(9, 0, SKX_PCI_UNCORE_M2M, 1),
	},
	{ /* UPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(14, 0, SKX_PCI_UNCORE_UPI, 0),
	},
	{ /* UPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(15, 0, SKX_PCI_UNCORE_UPI, 1),
	},
	{ /* UPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2058),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(16, 0, SKX_PCI_UNCORE_UPI, 2),
	},
	{ /* M2PCIe 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 1, SKX_PCI_UNCORE_M2PCIE, 0),
	},
	{ /* M2PCIe 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(22, 1, SKX_PCI_UNCORE_M2PCIE, 1),
	},
	{ /* M2PCIe 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(23, 1, SKX_PCI_UNCORE_M2PCIE, 2),
	},
	{ /* M2PCIe 3 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x2088),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(21, 5, SKX_PCI_UNCORE_M2PCIE, 3),
	},
	{ /* M3UPI0 Link 0 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204C),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 0, SKX_PCI_UNCORE_M3UPI, 0),
	},
	{ /* M3UPI0 Link 1 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204D),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 1, SKX_PCI_UNCORE_M3UPI, 1),
	},
	{ /* M3UPI1 Link 2 */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x204C),
		.driver_data = UNCORE_PCI_DEV_FULL_DATA(18, 4, SKX_PCI_UNCORE_M3UPI, 2),
	},
	{ /* end: all zeroes */ }
};


static struct pci_driver skx_uncore_pci_driver = {
	.name		= "skx_uncore",
	.id_table	= skx_uncore_pci_ids,
};

int skx_uncore_pci_init(void)
{
	/* need to double check pci address */
	int ret = snbep_pci2phy_map_init(0x2014, SKX_CPUNODEID, SKX_GIDNIDMAP, false);

	if (ret)
		return ret;

	uncore_pci_uncores = skx_pci_uncores;
	uncore_pci_driver = &skx_uncore_pci_driver;
	return 0;
}

/* end of SKX uncore support */
