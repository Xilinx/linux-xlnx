/*--------------------------------------------------------------------
 * open_eth.c
 *
 * Ethernet driver for Open Ethernet Controller (www.opencores.org).
 *
 * Copyright (c) 2002 Simon Srot (simons@opencores.org)
 * Copyright (C) 2004 Microtronix Datacom Ltd
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
 *
 *
 * History:
 *    Jun/20/2004   DGT Microtronix Datacom NiosII
 *
 ---------------------------------------------------------------------*/


/* Ethernet configuration registers */
typedef struct _oeth_regs {
        uint    moder;          /* Mode Register */
        uint    int_src;        /* Interrupt Source Register */
        uint    int_mask;       /* Interrupt Mask Register */
        uint    ipgt;           /* Back to Bak Inter Packet Gap Register */
        uint    ipgr1;          /* Non Back to Back Inter Packet Gap Register 1 */
        uint    ipgr2;          /* Non Back to Back Inter Packet Gap Register 2 */
        uint    packet_len;     /* Packet Length Register (min. and max.) */
        uint    collconf;       /* Collision and Retry Configuration Register */
        uint    tx_bd_num;      /* Transmit Buffer Descriptor Number Register */
        uint    ctrlmoder;      /* Control Module Mode Register */
        uint    miimoder;       /* MII Mode Register */
        uint    miicommand;     /* MII Command Register */
        uint    miiaddress;     /* MII Address Register */
        uint    miitx_data;     /* MII Transmit Data Register */
        uint    miirx_data;     /* MII Receive Data Register */
        uint    miistatus;      /* MII Status Register */
        uint    mac_addr0;      /* MAC Individual Address Register 0 */
        uint    mac_addr1;      /* MAC Individual Address Register 1 */
        uint    hash_addr0;     /* Hash Register 0 */
        uint    hash_addr1;     /* Hash Register 1 */                           
} oeth_regs;

/* Ethernet buffer descriptor */
typedef struct _oeth_bd {
#if 0
        ushort  len;            /* Buffer length */
        ushort  status;         /* Buffer status */
#else
        uint    len_status;
#endif
        uint    addr;           /* Buffer address */
} oeth_bd;

#define OETH_REG_BASE           ETH_BASE_ADD
#define OETH_BD_BASE            (ETH_BASE_ADD + 0x400)
#define OETH_TOTAL_BD           128
#define OETH_MAXBUF_LEN         0x600
                                
/* Tx BD */                     
#define OETH_TX_BD_READY        0x8000 /* Tx BD Ready */
#define OETH_TX_BD_IRQ          0x4000 /* Tx BD IRQ Enable */
#define OETH_TX_BD_WRAP         0x2000 /* Tx BD Wrap (last BD) */
#define OETH_TX_BD_PAD          0x1000 /* Tx BD Pad Enable */
#define OETH_TX_BD_CRC          0x0800 /* Tx BD CRC Enable */
                                
#define OETH_TX_BD_UNDERRUN     0x0100 /* Tx BD Underrun Status */
#define OETH_TX_BD_RETRY        0x00F0 /* Tx BD Retry Status */
#define OETH_TX_BD_RETLIM       0x0008 /* Tx BD Retransmission Limit Status */
#define OETH_TX_BD_LATECOL      0x0004 /* Tx BD Late Collision Status */
#define OETH_TX_BD_DEFER        0x0002 /* Tx BD Defer Status */
#define OETH_TX_BD_CARRIER      0x0001 /* Tx BD Carrier Sense Lost Status */
#define OETH_TX_BD_STATS        (OETH_TX_BD_UNDERRUN            | \
                                OETH_TX_BD_RETRY                | \
                                OETH_TX_BD_RETLIM               | \
                                OETH_TX_BD_LATECOL              | \
                                OETH_TX_BD_DEFER                | \
                                OETH_TX_BD_CARRIER)
                                
/* Rx BD */                     
#define OETH_RX_BD_EMPTY        0x8000 /* Rx BD Empty */
#define OETH_RX_BD_IRQ          0x4000 /* Rx BD IRQ Enable */
#define OETH_RX_BD_WRAP         0x2000 /* Rx BD Wrap (last BD) */
                                
#define OETH_RX_BD_MISS         0x0080 /* Rx BD Miss Status */
#define OETH_RX_BD_OVERRUN      0x0040 /* Rx BD Overrun Status */
#define OETH_RX_BD_INVSIMB      0x0020 /* Rx BD Invalid Symbol Status */
#define OETH_RX_BD_DRIBBLE      0x0010 /* Rx BD Dribble Nibble Status */
#define OETH_RX_BD_TOOLONG      0x0008 /* Rx BD Too Long Status */
#define OETH_RX_BD_SHORT        0x0004 /* Rx BD Too Short Frame Status */
#define OETH_RX_BD_CRCERR       0x0002 /* Rx BD CRC Error Status */
#define OETH_RX_BD_LATECOL      0x0001 /* Rx BD Late Collision Status */
#define OETH_RX_BD_STATS        (OETH_RX_BD_MISS                | \
                                OETH_RX_BD_OVERRUN              | \
                                OETH_RX_BD_INVSIMB              | \
                                OETH_RX_BD_DRIBBLE              | \
                                OETH_RX_BD_TOOLONG              | \
                                OETH_RX_BD_SHORT                | \
                                OETH_RX_BD_CRCERR               | \
                                OETH_RX_BD_LATECOL)

/* MODER Register */
#define OETH_MODER_RXEN         0x00000001 /* Receive Enable  */
#define OETH_MODER_TXEN         0x00000002 /* Transmit Enable */
#define OETH_MODER_NOPRE        0x00000004 /* No Preamble  */
#define OETH_MODER_BRO          0x00000008 /* Reject Broadcast */
#define OETH_MODER_IAM          0x00000010 /* Use Individual Hash */
#define OETH_MODER_PRO          0x00000020 /* Promiscuous (receive all) */
#define OETH_MODER_IFG          0x00000040 /* Min. IFG not required */
#define OETH_MODER_LOOPBCK      0x00000080 /* Loop Back */
#define OETH_MODER_NOBCKOF      0x00000100 /* No Backoff */
#define OETH_MODER_EXDFREN      0x00000200 /* Excess Defer */
#define OETH_MODER_FULLD        0x00000400 /* Full Duplex */
#define OETH_MODER_RST          0x00000800 /* Reset MAC */
#define OETH_MODER_DLYCRCEN     0x00001000 /* Delayed CRC Enable */
#define OETH_MODER_CRCEN        0x00002000 /* CRC Enable */
#define OETH_MODER_HUGEN        0x00004000 /* Huge Enable */
#define OETH_MODER_PAD          0x00008000 /* Pad Enable */
#define OETH_MODER_RECSMALL     0x00010000 /* Receive Small */
 
/* Interrupt Source Register */
#define OETH_INT_TXB            0x00000001 /* Transmit Buffer IRQ */
#define OETH_INT_TXE            0x00000002 /* Transmit Error IRQ */
#define OETH_INT_RXF            0x00000004 /* Receive Frame IRQ */
#define OETH_INT_RXE            0x00000008 /* Receive Error IRQ */
#define OETH_INT_BUSY           0x00000010 /* Busy IRQ */
#define OETH_INT_TXC            0x00000020 /* Transmit Control Frame IRQ */
#define OETH_INT_RXC            0x00000040 /* Received Control Frame IRQ */

/* Interrupt Mask Register */
#define OETH_INT_MASK_TXB       0x00000001 /* Transmit Buffer IRQ Mask */
#define OETH_INT_MASK_TXE       0x00000002 /* Transmit Error IRQ Mask */
#define OETH_INT_MASK_RXF       0x00000004 /* Receive Frame IRQ Mask */
#define OETH_INT_MASK_RXE       0x00000008 /* Receive Error IRQ Mask */
#define OETH_INT_MASK_BUSY      0x00000010 /* Busy IRQ Mask */
#define OETH_INT_MASK_TXC       0x00000020 /* Transmit Control Frame IRQ Mask */
#define OETH_INT_MASK_RXC       0x00000040 /* Received Control Frame IRQ Mask */
 
/* Control Module Mode Register */
#define OETH_CTRLMODER_PASSALL  0x00000001 /* Pass Control Frames */
#define OETH_CTRLMODER_RXFLOW   0x00000002 /* Receive Control Flow Enable */
#define OETH_CTRLMODER_TXFLOW   0x00000004 /* Transmit Control Flow Enable */
                               
/* MII Mode Register */        
#define OETH_MIIMODER_CLKDIV    0x000000FF /* Clock Divider */
#define OETH_MIIMODER_NOPRE     0x00000100 /* No Preamble */
#define OETH_MIIMODER_RST       0x00000200 /* MIIM Reset */
 
/* MII Command Register */
#define OETH_MIICOMMAND_SCANSTAT  0x00000001 /* Scan Status */
#define OETH_MIICOMMAND_RSTAT     0x00000002 /* Read Status */
#define OETH_MIICOMMAND_WCTRLDATA 0x00000004 /* Write Control Data */
 
/* MII Address Register */
#define OETH_MIIADDRESS_FIAD    0x0000001F /* PHY Address */
  //;dgt;May2005-note Mx Altera Nios (CONFIG_EXCALIBUR) with
  //; a TDK78Q2120 phy does NOT use OETH_MIIADDRESS_FIAD, but
  //; instead uses its own PHY_ADDRESS (0x00 = "broadcast"
  //; phy address) invention
  //;dgt:IIRC, Mx Tdk's USED to use address 0x1F but changed some
  //; undocumented time ago...
#define OETH_MIIADDRESS_RGAD    0x00001F00 /* RGAD Address */
 
/* MII Status Register */
#define OETH_MIISTATUS_LINKFAIL 0x00000001 /* Link Fail */
#define OETH_MIISTATUS_BUSY     0x00000002 /* MII Busy */
#define OETH_MIISTATUS_NVALID   0x00000004 /* Data in MII Status Register is invalid */

/*----------------------------------------------------------------------*/

    #define OETH_IO_EXTENT      (sizeof(oeth_regs))

/*----------------------------------------------------------------------*/
