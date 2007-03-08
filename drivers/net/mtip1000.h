/*----------------------------------------------------------------------
 . mtip1000.c
 .
 . Driver: MoreThanIP 10/100/1000Mbps Emac IP
 .
 . Copyright ...
    to
        be
            completed
                        ...
 .
 . Sources:
 .    o   MoreThanIP 10/100/1000Mbps Reference Guide V3.2 - May 2003
 .    o   MoreThanIP Altera Plugs sources
 .    o   Smc9111 uClinux port(s)
 .
 . History:
 .    o   Apr2004   DGT Microtronix Datacom
 .
 -----------------------------------------------------------------------*/

#ifndef _MTIP1000_H_
    #define _MTIP1000_H_

/*----------------------------------------------------------------------*/

#ifndef na_mtip_mac_control_port
  #if defined (na_mip_mac_control_port)
    #define na_mtip_mac_control_port    na_mip_mac_control_port
  #endif
#endif
#ifndef na_mtip_mac_rxFIFO
  #if defined (na_mip_mac_rxFIFO)
    #define na_mtip_mac_rxFIFO    na_mip_mac_rxFIFO
  #endif
#endif
#ifndef na_mtip_mac_rxFIFO_irq
  #if defined (na_mip_mac_rxFIFO_irq)
    #define na_mtip_mac_rxFIFO_irq    na_mip_mac_rxFIFO_irq
  #endif
#endif
#ifndef na_mtip_mac_txFIFO
  #if defined (na_mip_mac_txFIFO)
    #define na_mtip_mac_txFIFO    na_mip_mac_txFIFO
  #endif
#endif
#ifndef na_mtip_mac_txFIFO_irq
  #if defined (na_mip_mac_txFIFO_irq)
    #define na_mtip_mac_txFIFO_irq    na_mip_mac_txFIFO_irq
  #endif
#endif

/*----------------------------------------------------------------------*/

// Number of bytes the largest frame can have.
// For receive, should be at least the MAC's FRAME_LENGTH
//  programmed value + 8.
#define MTIP_MAC_MAX_FRAME_SIZE     1524
//
// Receive buffer must be at least 'maximum possible frame size'+16.
#define MTIP_MI_XBUF_BYTS           24
#define MTIP_SKB_XBUF_BYTS          MTIP_MI_XBUF_BYTS

// MDIO registers within MAC register Space
// memory mapped access
//
typedef volatile struct
{
  unsigned int CONTROL;
  unsigned int STATUS;
  unsigned int PHY_ID1;
  unsigned int PHY_ID2;
  unsigned int ADV;
  unsigned int REMADV;

  unsigned int reg6;
  unsigned int reg7;
  unsigned int reg8;
  unsigned int reg9;
  unsigned int rega;
  unsigned int regb;
  unsigned int regc;
  unsigned int regd;
  unsigned int rege;
  unsigned int regf;
  unsigned int reg10;
  unsigned int reg11;
  unsigned int reg12;
  unsigned int reg13;
  unsigned int reg14;
  unsigned int reg15;
  unsigned int reg16;
  unsigned int reg17;
  unsigned int reg18;
  unsigned int reg19;
  unsigned int reg1a;
  unsigned int reg1b;
  unsigned int reg1c;
  unsigned int reg1d;
  unsigned int reg1e;
  unsigned int reg1f;

} np_mtip_mdio;

// MAC Registers - 32 Bit
//
typedef volatile struct
{
  unsigned int REV;
  unsigned int SCRATCH;
  unsigned int COMMAND_CONFIG;
  unsigned int MAC_0;
  unsigned int MAC_1;
  unsigned int FRM_LENGTH;
  unsigned int PAUSE_QUANT;
  unsigned int RX_SECTION_EMPTY;
  unsigned int RX_SECTION_FULL;
  unsigned int TX_SECTION_EMPTY;
  unsigned int TX_SECTION_FULL;
  unsigned int RX_ALMOST_EMPTY;
  unsigned int RX_ALMOST_FULL;
  unsigned int TX_ALMOST_EMPTY;
  unsigned int TX_ALMOST_FULL;
  unsigned int MDIO_ADDR0;
  unsigned int MDIO_ADDR1;

  unsigned int AUTONEG_CNTL;
    // only if 100/1000 BaseX PCS, reserved otherwise

  unsigned int AN_ABILITY_INT;
  unsigned int LP_ABILITY_INT;
  unsigned int LINK_TIMER_INT;

  unsigned int reservedx54;
  unsigned int reservedx58;
  unsigned int reservedx5C;

  unsigned int aMACID_1;
  unsigned int aMACID_2;
  unsigned int aFramesTransmittedOK;
  unsigned int aFramesReceivedOK;
  unsigned int aFramesCheckSequenceErrors; 
  unsigned int aAlignmentErrors;
  unsigned int aOctetsTransmittedOK;
  unsigned int aOctetsReceivedOK;
  unsigned int aTxPAUSEMACCtrlFrames;
  unsigned int aRxPAUSEMACCtrlFrames;
  unsigned int ifInErrors;
  unsigned int ifOutErrors;
  unsigned int ifInUcastPkts;
  unsigned int ifInBroadcastPkts;
  unsigned int ifInMulticastPkts;
  unsigned int ifOutDiscards;
  unsigned int ifOutUcastPkts;
  unsigned int ifOutBroadcastPkts;
  unsigned int ifOutMulticastPkts;
  unsigned int etherStatsDropEvent;
  unsigned int etherStatsOctets;
  unsigned int etherStatsPkts;
  unsigned int etherStatsUndersizePkts;
  unsigned int etherStatsOversizePkts;
  unsigned int etherStatsPkts64Octets;
  unsigned int etherStatsPkts65to127Octets;
  unsigned int etherStatsPkts128to255Octets;
  unsigned int etherStatsPkts256to511Octets;
  unsigned int etherStatsPkts512to1023Octets;
  unsigned int etherStatsPkts1024to1518Octets;

  unsigned int reservedxD8;
  unsigned int reservedxDC;

  unsigned int AVL_STATUS;
  unsigned int IRQ_CONFIG;
           int TX_CMD_STAT;
           int RX_CMD_STAT;

  unsigned int reservedxF0;
  unsigned int reservedxF4;
  unsigned int reservedxF8;
  unsigned int reservedxFC;

  unsigned int hashtable[64];

  np_mtip_mdio mdio0;
  np_mtip_mdio mdio1;

} np_mtip_mac;


// Base-Structure for all library functions

typedef struct {

  np_mtip_mac     *mac;
#ifdef nasys_dma_0
  np_dma          *dma;
  np_dma          *dma_rx;
#else
  int             *dma;
  int             *dma_rx;
#endif
  volatile unsigned int *rxFIFO;
  volatile unsigned int *txFIFO;

  unsigned int    cfgflags;
     // flags or'ed during initialization of COMMAND_CONFIG

  int            *rxbuf;        // receive buffer to use

} mtip_mac_trans_info;

// COMMAND_CONFIG Register Bits
//
enum
{
  mmac_cc_TX_ENA_bit        = 0,
  mmac_cc_RX_ENA_bit        = 1,
  mmac_cc_XOFF_GEN_bit      = 2,
  mmac_cc_ETH_SPEED_bit     = 3,
  mmac_cc_PROMIS_EN_bit     = 4,
  mmac_cc_PAD_EN_bit        = 5,
  mmac_cc_CRC_FWD_bit       = 6,
  mmac_cc_PAUSE_FWD_bit     = 7,
  mmac_cc_PAUSE_IGNORE_bit  = 8,
  mmac_cc_TX_ADDR__INS_bit  = 9,
  mmac_cc_HD_ENA_bit        = 10,
  mmac_cc_EXCESS_COL_bit    = 11,
  mmac_cc_LATE_COL_bit      = 12,
  mmac_cc_SW_RESET_bit      = 13,
//;dgt2;  mmac_cc_MHASH_SEL_bit     = 13,
  mmac_cc_MHASH_SEL_bit     = 14,   //;dgt2;
  mmac_cc_LOOPBACK_bit      = 15,
  mmac_cc_TX_ADDR_SEL_bit   = 16,   // bits 18:16 = address select
  mmac_cc_MAGIC_ENA_bit     = 19,
  mmac_cc_SLEEP_ENA_bit     = 20,

  mmac_cc_TX_ENA_mask       = (1 << 0), // enable TX
  mmac_cc_RX_ENA_mask       = (1 << 1), // enable RX
  mmac_cc_XOFF_GEN_mask     = (1 << 2), // generate Pause frame with Quanta
  mmac_cc_ETH_SPEED_mask    = (1 << 3), // Select Gigabit
  mmac_cc_PROMIS_EN_mask    = (1 << 4), // enable Promiscuous mode
  mmac_cc_PAD_EN_mask       = (1 << 5), // enable padding remove on RX
  mmac_cc_CRC_FWD_mask      = (1 << 6), // forward CRC to application on RX (as opposed to stripping it off)
  mmac_cc_PAUSE_FWD_mask    = (1 << 7), // forward Pause frames to application
  mmac_cc_PAUSE_IGNORE_mask = (1 << 8), // ignore Pause frames
  mmac_cc_TX_ADDR_INS_mask  = (1 << 9), // MAC overwrites bytes 6 to 12 of frame with address on all transmitted frames
  mmac_cc_HD_ENA_mask       = (1 << 10),// enable half-duplex operation
  mmac_cc_EXCESS_COL_mask   = (1 << 11),// indicator
  mmac_cc_LATE_COL_mask     = (1 << 12),// indicator
  mmac_cc_SW_RESET_mask     = (1 << 13),// issue register and counter reset
  mmac_cc_MHASH_SEL_mask    = (1 << 14),// select multicast hash method
  mmac_cc_LOOPBACK_mask     = (1 << 15),// enable GMII loopback
  mmac_cc_MAGIC_ENA_mask    = (1 << 19),// enable magic packet detect
  mmac_cc_SLEEP_ENA_mask    = (1 << 20) // enter sleep mode
};

// AVL_STATUS Register Bits

enum
{
  mmac_as_RX_FRAME_AVAILABLE_mask   = (1 << 0),
  mmac_as_TX_FIFO_EMPTY_mask        = (1 << 1),
  mmac_as_TX_FIFO_SEPTY_mask        = (1 << 2)
};


// IRQ_CONFIG Register Bits

enum
{
  mmac_ic_EN_RX_FRAME_AVAILABLE_mask = (1 << 0),  // rx frame available (status FIFO)
  mmac_ic_EN_TX_FIFO_EMPTY_mask      = (1 << 1),  // tx section empty
  mmac_ic_EN_RX_MAGIC_FRAME_mask     = (1 << 2),  // magic frame received interrupt when in sleep mode
  mmac_ic_OR_WRITE                   = (1 << 30), // if set, write data is OR'ed with current register bits
  mmac_ic_AND_WRITE                  = (1 << 31)  // if set, write data is AND'ed with current register bits
};


// TX_CMD_STAT Register bits
//
enum{
  mmac_tcs_LENGTH_mask            = 0x3fff,        // length portion
  mmac_tcs_FRAME_COMPLETE_mask    = (1 << 31),     // negative as long as frame is not complete
  mmac_tcs_SET_ERROR_mask         = (1 << 16),
  mmac_tcs_OMIT_CRC_mask          = (1 << 17)
};


// RX_CMD_STAT Register bits
//
enum{
  mmac_rcs_FRAME_LENGTH_mask    = 0x0000ffff,
  mmac_rcs_ERROR_mask           = (1 << 16),
  //mmac_rcs_FIFO_OVERFLOW_mask   = (1 << xx),  //;dgt2; ???
  mmac_rcs_VLAN_mask            = (1 << 17),
  mmac_rcs_MCAST_mask           = (1 << 18),
  mmac_rcs_BCAST_mask           = (1 << 19),
  mmac_rcs_UNICAST_mask         = (1 << 20),    //;dgt2; 20 or 23 ?...
  mmac_rcs_READ_CMD_mask        = (1 << 24),
  mmac_rcs_VALID_mask           = (1 << 31)
};


/** extracts AVL_STATUS' VALID bit to detect if there is a frame in the RX fifo. */
//
//;dgt2;nix;
//;dgt2;-only if;
//;dgt2;  Avl_Status;
//;dgt2;   says frame;
//;dgt2;    available...?...;
//;dgt2; #define mtip_mac_isFrameAvail( pmtip_mac ) ( pmtip_mac->RX_CMD_STAT & mmac_rcs_VALID_mask )

/** extracts length of frame currently available in the FIFO. */
//
#define mtip_mac_getFrameLength( pmtip_mac) ( pmtip_mac->RX_CMD_STAT & mmac_rcs_FRAME_LENGTH_mask )

/** set promiscous bit. */
//
#define mtip_mac_setPromiscuous( pmtip_mac) ( pmtip_mac->COMMAND_CONFIG |= mmac_cc_PROMIS_EN_mask )

/** clear promiscuous bit. */
//
#define mtip_mac_clearPromiscuous(pmtip_mac) ( pmtip_mac->COMMAND_CONFIG &= ~mmac_cc_PROMIS_EN_mask )


/** switch MAC into MII (10/100) mode. */
//
#define mtip_mac_setMIImode( pmtip_mac ) ( pmtip_mac->COMMAND_CONFIG &= ~mmac_cc_ETH_SPEED_mask )

/** switch MAC into GMII (Gigabit) mode. */
#define mtip_mac_setGMIImode( pmtip_mac ) ( pmtip_mac->COMMAND_CONFIG |= mmac_cc_ETH_SPEED_mask )


// PCS --------------

/** PCS Control Register Bits. IEEE 802.3 Clause 22.2.4.1
 */
enum {
        PCS_CTL_speed1      = 1<<6,  // speed select
        PCS_CTL_speed0      = 1<<13, 
        PCS_CTL_fullduplex  = 1<<8,  // fullduplex mode select
        PCS_CTL_an_restart  = 1<<9,  // Autonegotiation restart command
        PCS_CTL_isolate     = 1<<10, // isolate command
        PCS_CTL_powerdown   = 1<<11, // powerdown command
        PCS_CTL_an_enable   = 1<<12, // Autonegotiation enable
        PCS_CTL_rx_slpbk    = 1<<14, // Serial Loopback enable
        PCS_CTL_sw_reset    = 1<<15  // perform soft reset

  };

/** PCS Status Register Bits. IEEE 801.2 Clause 22.2.4.2
 */
enum {
        PCS_ST_has_extcap   = 1<<0,  // PHY has extended capabilities registers 
        PCS_ST_rx_sync      = 1<<2,  // RX is in sync (8B/10B codes o.k.)
        PCS_ST_an_ability   = 1<<3,  // PHY supports autonegotiation
        PCS_ST_rem_fault    = 1<<4,  // Autonegotiation completed
        PCS_ST_an_done      = 1<<5

  };

/** Autonegotiation Capabilities Register Bits. IEEE 802.3 Clause 37.2.1 */

enum {
        ANCAP_NEXTPAGE      = 1 << 15,
        ANCAP_ACK           = 1 << 14,
        ANCAP_RF2           = 1 << 13,
        ANCAP_RF1           = 1 << 12,
        ANCAP_PS2           = 1 << 8,
        ANCAP_PS1           = 1 << 7,
        ANCAP_HD            = 1 << 6,
        ANCAP_FD            = 1 << 5
        // all others are reserved
  }; 

/*----------------------------------------------------------------------*/

    #define MTIP1000_IO_EXTENT      (sizeof(np_mtip_mac))

/*----------------------------------------------------------------------*/

  #ifdef CONFIG_SYSCTL

    /*
     * Declarations for the sysctl interface, which allows users to
     * control the finer aspects of the Emac. Since the 
     * module registers its sysctl table dynamically, the sysctl path
     * for module FOO is /proc/sys/dev/ethX/FOO
     */
    #define CTL_MTIP1000    (CTL_BUS+1389)
      // arbitrary and hopefully unused

    enum
      {
        CTL_MTIP_INFO = 1,  // Sysctl files information
        CTL_MTIP_SWVER,     // Driver Software Version Info
        //...fixme...
      #ifdef MTIP_DEBUG
        // Register access for debugging
        //...fixme...
      #endif
        // ---------------------------------------------------
        CTL_MTIP_LAST_ENTRY // Add new entries above the line
      };

  #endif // CONFIG_SYSCTL

#endif  /* _MTIP1000_H_ */
