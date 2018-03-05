#ifndef _XILINX_PHY_H
#define _XILINX_PHY_H

/* Mask used for ID comparisons */
#define XILINX_PHY_ID_MASK		0xfffffff0

/* Known PHY IDs */
#define XILINX_PHY_ID			0x01740c00

/* struct phy_device dev_flags definitions */
#define XAE_PHY_TYPE_MII		0
#define XAE_PHY_TYPE_GMII		1
#define XAE_PHY_TYPE_RGMII_1_3		2
#define XAE_PHY_TYPE_RGMII_2_0		3
#define XAE_PHY_TYPE_SGMII		4
#define XAE_PHY_TYPE_1000BASE_X		5
#define XAE_PHY_TYPE_2500		6
#define XXE_PHY_TYPE_USXGMII		7

#endif /* _XILINX_PHY_H */
