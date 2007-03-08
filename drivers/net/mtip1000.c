/*----------------------------------------------------------------------
 . mtip1000.c
 .
 . Driver: MoreThanIP 10/100/1000Mbps Emac IP
 .
 . Copyright (C) 2004 Microtronix Datacom Ltd.
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
 . Sources:
 .    o   MoreThanIP 10/100/1000Mbps Reference Guide V3.2 - May 2003
 .    o   MoreThanIP Altera Plugs sources
 .          - mtip_10_100_1000.c
 .          - mtip_10_100_1000_adapter.c
 .          - mac_stream_test.c
 .    o   Smc9111 uClinux port(s)
 .
 . History:
 .    o   Apr2004   DGT Microtronix Datacom - Linux 2.6.5
 .
 -----------------------------------------------------------------------*/

static const char version[] =
    "MoreThanIP 10/100/1000 Driver"
    "(v1.0)"
        ", Linux 2.6.5 Apr2004\n";

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/system.h>

#include <asm/io.h>

#ifdef CONFIG_EXCALIBUR
    #include <asm/nios.h>
    #include <asm/ndma.h>
    #include <asm/cacheflush.h>
#endif  // CONFIG_EXCALIBUR

#include <linux/errno.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "mtip1000.h"
#include "stdphy.h"

#define ANNOUNCEPHY
//#undef  ANNOUNCEPHY

//#undef MTIPPHYIRQ_AVAIL
#if 1
  #if defined (na_mii_irq_irq)
      #define MTIPPHYIRQ_AVAIL
    #define mtip_mii_control_port ((unsigned int *) (((unsigned int) (na_mii_irq)) | 0x80000000))
  #endif    // na_mii_irq_irq
#else
  #undef na_mii_irq_irq
  #undef na_mii_irq
#endif

//#define NS83865PHY
#define TDK78Q2120PHY

#ifdef NS83865PHY
    #include "ns83865phy.h"
    #define PHYTYPE "NS83865"
#else
    #ifdef TDK78Q2120PHY
        #include "tdk78phy.h"
    #define PHYTYPE "TDK78Q2120"
    #endif
#endif

#ifdef CONFIG_SYSCTL
    #include <linux/proc_fs.h>
    #include <linux/sysctl.h>
#endif  // CONFIG_SYSCTL

//#undef na_dma                   // Force "Pio" mode
#if defined (na_dma)
    #define MTIPDMA_AVAIL
    #define mtip_dma_control_port   ((np_dma *) (((unsigned int) (na_dma)) | 0x80000000))
    #define IOTYPE "DMA"

#else
    #undef  MTIPDMA_AVAIL
    #define IOTYPE "PIO"
#endif  // na_dma


/*----------------------------------------------------------------------
 . DEBUGGING LEVELS
 .
 . 0 for normal operation
 . 1 for slightly more details
 . 2 for interrupt tracking, status flags
 . 3 for packet info
 . 4 for complete packet dumps
 -----------------------------------------------------------------------*/
//#define MTIP_DEBUG 4
//#define MTIP_DEBUG 3
//#define MTIP_DEBUG 2
//#define MTIP_DEBUG 1
#define MTIP_DEBUG 0

#if (MTIP_DEBUG > 2 )
    #define PRINTK3(args...) printk(args)
#else
    #define PRINTK3(args...)
#endif

#if MTIP_DEBUG > 1
    #define PRINTK2(args...) printk(args)
#else
    #define PRINTK2(args...)
#endif

#ifdef MTIP_DEBUG
    #define PRINTK(args...) printk(args)
#else
    #define PRINTK(args...)
#endif


typedef unsigned char           byte;
typedef unsigned short          word;
typedef unsigned long int       dword;


/*-----------------------------------------------------------
 Port address(es). Array must end in zero.
*/
#if defined(CONFIG_EXCALIBUR)
    static unsigned int mtip_portlist[] =
        {  ((int)
             ((((unsigned int) (na_mtip_mac_control_port)) | 0x80000000)))
         , 0 };
    static unsigned int mtip_irqlist[]  =
        { na_mtip_mac_rxFIFO_irq, 0 };
    #define PIO_port_rxFIFO  (((unsigned int) (na_mtip_mac_rxFIFO)) | 0x80000000)
    #define PIO_port_txFIFO  (((unsigned int) (na_mtip_mac_txFIFO)) | 0x80000000)
#else
    #define PIO_port_rxFIFO  (((unsigned int) (na_mtip_mac_rxFIFO)))
    #define PIO_port_txFIFO  (((unsigned int) (na_mtip_mac_txFIFO)))
  ............
#endif  // CONFIG_EXCALIBUR


struct mtip_local
  {
    struct   net_device_stats   stats;

#ifdef CONFIG_SYSCTL

    // Root directory /proc/sys/dev
    // Second entry must be null to terminate the table
    ctl_table root_table[2];

    // Directory for this device /proc/sys/dev/ethX
    // Again the second entry must be zero to terminate
    ctl_table eth_table[2];

    // This is the parameters (file) table
    ctl_table param_table[CTL_MTIP_LAST_ENTRY];

    // Saves the sysctl header returned by register_sysctl_table()
    // we send this to unregister_sysctl_table()
    struct ctl_table_header *sysctl_header;

    // Parameter variables (files) go here
    char ctl_info[1024];    // ?...?

/*
....tbd.............
    int ctl_xxxxxx's;
......................*/

#endif // CONFIG_SYSCTL
  };


/*-----------------------------------------------------------
 | Print out a packet.
*/
#if MTIP_DEBUG > 3
  static void print_packet( byte * buf, int length )
    {
      #if 1
        #if MTIP_DEBUG > 3
            int i;
            int remainder;
            int lines;
        #endif

        printk("Packet length %d \n", length );

        #if MTIP_DEBUG > 3
            lines = length / 16;
            remainder = length % 16;

            for ( i = 0; i < lines ; i ++ ) {
                int cur;

                for ( cur = 0; cur < 8; cur ++ ) {
                    byte a, b;

                    a = *(buf ++ );
                    b = *(buf ++ );
                    printk("%02x %02x ", a, b );
                }
                printk("\n");
            }
            for ( i = 0; i < remainder/2 ; i++ ) {
                byte a, b;

                a = *(buf ++ );
                b = *(buf ++ );
                printk("%02x %02x ", a, b );
            }
            printk("\n");
        #endif
      #endif
    }
#endif


/*-----------------------------------------------------------
*/
#define PRINTSTDPHYREGS(pmac)                                   \
                                                                \
    printk("    PhyCtl0: %04X"                                  \
            "   PhySts1:  %04X"                                 \
            "  PhyID1:     %04X\n",                             \
           (pmac->mdio0).CONTROL,                               \
           (pmac->mdio0).STATUS,                                \
           (pmac->mdio0).PHY_ID1);                              \
    printk("    PhyID2:  %04X"                                  \
            "   PhyAdv4:  %04X"                                 \
            "  PhyRemcap5: %04X\n",                             \
           (pmac->mdio0).PHY_ID2,                               \
           (pmac->mdio0).ADV,                                   \
           (pmac->mdio0).REMADV);


#ifdef NS83865PHY
  #define PRINTNS83PHYREGS(pmac,                                \
                           nsIntstsReg20,                       \
                           nsIntieReg21,                        \
                           nsLnkstsReg17)                       \
                                                                \
      PRINTSTDPHYREGS(pmac);                                    \
                                                                \
        printk("    Ns20Ists:%04X"                              \
                "   Ns21Intie:%04X"                             \
                "  Ns17Lnksts: %04X\n",                         \
               nsIntstsReg20,                                   \
               nsIntieReg21,                                    \
               nsLnkstsReg17);
#endif


#ifdef TDK78Q2120PHY
    #define PRINTTDKPHYREGS(pmac,                               \
                            tdkintCtlStsReg17,                  \
                            tdkDiagReg18)                       \
                                                                \
        PRINTSTDPHYREGS(pmac);                                  \
                                                                \
        printk("    Tdk16:   %04X"                              \
                "   Tdk17Int: %04X"                             \
                "  Tdk18Diag:  %04X\n",                         \
               (pmac->mdio0).reg10,                             \
               tdkintCtlStsReg17,                               \
               tdkDiagReg18);
#endif


/*-----------------------------------------------------------
*/
unsigned int       gMtipDisabledRxints;
unsigned int       gMtipDisabledTxints;
unsigned long      gMtipDiscardSink;            // RxFifo discard
unsigned int       gMtipDmaints;
unsigned int       gMtipDmaintsBusy;
unsigned int       gMtipDmaintsBusyDone;
unsigned int       gMtipDmaintsNoDone;
unsigned int       gMtipRxints;
unsigned int       gMtipRxintsRxdmaBusy;
unsigned int       gMtipRxintsRxdmaQued;
unsigned int       gMtipRxNoints;
int                gMtipRxSkbFifoNumL32s;
word               gMtipRxSkbframelenbyts;
unsigned int       gMtipTxints;
unsigned int       gMtipTxintsIncomplete;
int                gMtipTxSkbFifoNumL32s;
word               gMtipTxSkbframelenbyts;
unsigned int       gMtipUnexpDmaints;
unsigned int       gMtipUnexpRxints;
unsigned int       gMtipUnexpTxints;
byte              *gpMtipRxData;
struct   sk_buff  *gpMtipRxSkbInProg;
byte              *gpMtipTxData;
struct   sk_buff  *gpMtipTxSkbInProg;

#if defined (MTIPDMA_AVAIL)

unsigned int       gMtipDmaQ;
unsigned int       gMtipDmaState;

unsigned char gMtipTmpDmaBuf[MTIP_MAC_MAX_FRAME_SIZE + MTIP_MI_XBUF_BYTS];
  // Nios alignment requirements...very inconvenient...
  // for now...but should be in "on chip sram" if any...
  //  (perhaps used as "ring[s]"...
  // for now...rx and tx share same temporary 1 only "dma buffer"

enum
  {
      Mtip_DmaQ_TxSkb2Tmp        = (1 << 0)
    , Mtip_DmaQ_RxFifo2Tmp       = (1 << 1)
    , Mtip_DmaQ_RxFifo2Trash     = (1 << 2)
  };

enum
  {
      Mtip_DmaState_Idle         = 0
    , Mtip_DmaState_RxFifo2Tmp   = 1
    , Mtip_DmaState_RxTmp2Skb    = 2
    , Mtip_DmaState_TxSkb2Tmp    = 3
    , Mtip_DmaState_TxTmp2Fifo   = 4
    , Mtip_DmaState_RxFifo2Trash = 5
  };


/*-----------------------------------------------------------
*/
static void dma_start
    (int      bytes_per_transfer,
     void    *source_address,
     void    *destination_address,
     int      transfer_count,       // In units of bytes_per_transfer
     int      mode)                 // wcon, rcon, i_en, ... bit(s)
  {
    // Caller must have already flushed any "memory" range
    //  involved in this transfer that stands at risk.

    int         control_bits    = 0;
    np_dma     *dma             = mtip_dma_control_port;

    dma->np_dmacontrol = 0;
      // | 1. Halt anything that's going on

    dma->np_dmastatus       = 0;
    dma->np_dmareadaddress  = (int)source_address;
    dma->np_dmawriteaddress = (int)destination_address;
    dma->np_dmalength       = transfer_count * bytes_per_transfer;

    control_bits =
        mode
      | (bytes_per_transfer  &  7)  // low three bits of control reg
      | ((bytes_per_transfer &  8) ? np_dmacontrol_doubleword_mask : 0)
      | ((bytes_per_transfer & 16) ? np_dmacontrol_quadword_mask   : 0)
      | np_dmacontrol_leen_mask     // enable length
//      | np_dmacontrol_reen_mask     //;dgt;tmp;test; Enable read end-of-packet
//      | np_dmacontrol_ween_mask     //;dgt;tmp;test; Enable write end-of-packet
      | np_dmacontrol_go_mask;      // and... go!

    dma->np_dmacontrol = control_bits;

    return;
  }

/*-----------------------------------------------------------
*/
void dma_start_RxFifo2Tmp(void)
  {
    // Caller must have already set gMtipDmaState
    //  to Mtip_DmaState_RxFifo2Tmp (under semaphore),
    //  and disabled Rx ready interrupts.

    dcache_push (((unsigned long) (gpMtipRxData)),
                 gMtipRxSkbframelenbyts);

    dma_start
        (4,                               // Byts per transfer
         ((void *) (na_mtip_mac_rxFIFO)), // 32 bit source fifo
         gMtipTmpDmaBuf,                  // 32 bit aligned dest
         gMtipRxSkbFifoNumL32s,           // # of 4 byte transfers
         (  np_dmacontrol_rcon_mask       // Source is a Fifo
          | np_dmacontrol_i_en_mask       // Dma done:interrupt
          ));
  }

/*-----------------------------------------------------------
*/
void dma_start_TxSkb2Tmp(void)
  {
    // Caller must have already set gMtipDmaState
    //  to Mtip_DmaState_TxSkb2Tmp (under semaphore)

    dcache_push (((unsigned long) (gpMtipTxData)),
                 gMtipTxSkbframelenbyts);
    dma_start
        (2,                               // Byts per transfer
         gpMtipTxData,                    // 16 bit aligned src,
         gMtipTmpDmaBuf,                  // 32 bit aligned dest,
         (gMtipTxSkbFifoNumL32s << 1),    // # of 2 byte transfers
         (  0                             // Neither end a fifo
          | np_dmacontrol_i_en_mask       // Dma done:interrupt
          ));
      // At the risk of possibly incurring twice the copy
      //  time, save some cpu cycles by assuming outbound
      //  data starts on a 16 bit boundary (Have never
      //  empirically observed it on anything but...)...
  }

/*-----------------------------------------------------------
*/
void dma_start_RxFifo2Trash(void)
  {
    // Caller must have already set gMtipDmaState
    //  to Mtip_DmaState_RxFifo2Trash (under semaphore),
    //  and disabled Rx ready interrupts.

    dma_start
        (4,                               // Byts per transfer
         ((void *) (na_mtip_mac_rxFIFO)), // 32 bit source fifo
         ((unsigned char *)
            &(gMtipDiscardSink)),         // 32 bit aligned dest
         gMtipRxSkbFifoNumL32s,           // # of 4 byte transfers
         (  np_dmacontrol_rcon_mask       // Source is a Fifo
          | np_dmacontrol_wcon_mask       // Dest is a sinkhole
          | np_dmacontrol_i_en_mask       // Dma done:interrupt
          ));
  }
#endif  // MTIPDMA_AVAIL

/*-----------------------------------------------------------
 | Entry condition: Cpu interrupts DISabled.
*/
static void mtip_NuRxReady(struct   net_device  *dev,
                           unsigned int          cmplnstatus)
  {
    // Caller must have already verified cmplnstatus's
    //  mmac_rcs_VALID_mask bit is SET.

    struct   mtip_local    *lp;
    np_mtip_mac            *pmac;

    lp   = (struct mtip_local *)dev->priv;
    pmac = ((np_mtip_mac *) dev->base_addr);

    gMtipRxSkbframelenbyts = ( cmplnstatus & mmac_rcs_FRAME_LENGTH_mask );

    PRINTK3
//  printk
           (
            "mtip_NuRxReady:%s, asts:0x%04X, csts:0x%08X, Len:%d\n",
            dev->name,
            pmac->AVL_STATUS,
            cmplnstatus,
            gMtipRxSkbframelenbyts);

    if(gMtipRxSkbframelenbyts == 0)
      {
//      PRINTK3
        printk
               (
                "mtip_NuRxReady:%s, ZERO len frame,"
                    " asts:0x%04X, csts:0x%08X\n",
                dev->name,
                pmac->AVL_STATUS,
                cmplnstatus);
      }

    gMtipRxSkbFifoNumL32s = ((gMtipRxSkbframelenbyts + 3) >> 2);

    if( cmplnstatus & mmac_rcs_ERROR_mask )
      {
        PRINTK3
//      printk
              (
               "mtip_NuRxReady:%s, Bad frame:0x%08X, Len:%d\n",
               dev->name,
               cmplnstatus,
               gMtipRxSkbframelenbyts);

        lp->stats.rx_errors++;

        //;...can't differentiate...lp->stats.rx_frame_errors++;
        //;...can't differentiate...lp->stats.rx_length_errors++;
        //;...can't differentiate...lp->stats.rx_crc_errors++;

      #if defined (MTIPDMA_AVAIL)
        pmac->IRQ_CONFIG &= (~(mmac_ic_EN_RX_FRAME_AVAILABLE_mask));
          // disable rx ready interrupt

        if(gMtipDmaState == Mtip_DmaState_Idle)
          {
            gMtipDmaState = Mtip_DmaState_RxFifo2Trash;

            dma_start_RxFifo2Trash();
          }
          else
          {
            gMtipDmaQ |= Mtip_DmaQ_RxFifo2Trash;
          }

        return;
      #else
        {
          int       FifoL32     = 0;
          pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
          while( (pmac->RX_CMD_STAT) & mmac_rcs_READ_CMD_mask )
            {
              FifoL32 |= *((volatile unsigned long *) PIO_port_rxFIFO);
            }
          gMtipDiscardSink = FifoL32;
        }

        return;
      #endif  // MTIPDMA_AVAIL
      }

    //;...?...lp->stats.multicast++;

    if(gMtipRxSkbframelenbyts > MTIP_MAC_MAX_FRAME_SIZE)
      {
//      PRINTK3
        printk
              (
//             KERN_NOTICE
                 "mtip_NuRxReady:%s, oversized %d byte packet.\n",
               dev->name,
               gMtipRxSkbframelenbyts);
        goto Dropfrm_label;
      }

    gpMtipRxSkbInProg =
        dev_alloc_skb( (gMtipRxSkbFifoNumL32s << 2) +
                       MTIP_SKB_XBUF_BYTS );
      // Extra bytes: Dma requirements

    if ( gpMtipRxSkbInProg == NULL )
      {
        PRINTK3
//      printk
              (
//             KERN_NOTICE
                 "mtip_NuRxReady:%s, Low memory, packet dropped.\n",
               dev->name);
        goto Dropfrm_label;
      }

    skb_reserve( gpMtipRxSkbInProg, 2 );   /* 16 bit alignment */

    gpMtipRxSkbInProg->dev = dev;

    gpMtipRxData = skb_put( gpMtipRxSkbInProg, gMtipRxSkbframelenbyts );

    lp->stats.rx_packets++;

    #if defined (MTIPDMA_AVAIL)
      pmac->IRQ_CONFIG &= (~(mmac_ic_EN_RX_FRAME_AVAILABLE_mask));
        // disable rx ready interrupt

      if(gMtipDmaState == Mtip_DmaState_Idle)
        {
          gMtipDmaState = Mtip_DmaState_RxFifo2Tmp;

          dma_start_RxFifo2Tmp();
        }
        else
        {
          gMtipDmaQ |= Mtip_DmaQ_RxFifo2Tmp;
        }

      return;
    #else
      insl(((unsigned long) PIO_port_rxFIFO),
           gpMtipRxData,
           gMtipRxSkbFifoNumL32s);

      PRINTK3
            (
             "%s:Received %d byte Packet 0x%08X\n",
             dev->name,
             gMtipRxSkbframelenbyts,
             ((unsigned long) gpMtipRxData));

      #if MTIP_DEBUG > 3
        print_packet( gpMtipRxData, gMtipRxSkbframelenbyts );
      #endif

      gpMtipRxSkbInProg->protocol =
          eth_type_trans(gpMtipRxSkbInProg, dev );

      netif_rx(gpMtipRxSkbInProg);

      pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
        // acknowledge frame reception

      return;
    #endif  // MTIPDMA_AVAIL

Dropfrm_label:

    /* Oversized Rx frame, or dev_alloc_skb(...) failure                */

    lp->stats.rx_dropped++;

    #if defined (MTIPDMA_AVAIL)
      pmac->IRQ_CONFIG &= (~(mmac_ic_EN_RX_FRAME_AVAILABLE_mask));
        // disable rx ready interrupt

      if(gMtipDmaState == Mtip_DmaState_Idle)
        {
          gMtipDmaState = Mtip_DmaState_RxFifo2Trash;

          dma_start_RxFifo2Trash();
        }
        else
        {
          gMtipDmaQ |= Mtip_DmaQ_RxFifo2Trash;
        }

      return;
    #else
      {
        int           FifoL32     = 0;
        int           FifoLoop;
        for ( FifoLoop = 0; FifoLoop < gMtipRxSkbFifoNumL32s ; FifoLoop++ )
          {
            FifoL32 |= *((volatile unsigned long *) PIO_port_rxFIFO);
          }
        gMtipDiscardSink = FifoL32;
      }

      pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
        // acknowledge frame reception

      return;
    #endif  // MTIPDMA_AVAIL
  }

#if defined (MTIPDMA_AVAIL)
/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t mtip_DmaInterrupt(int             irq,
                                     void           *dev_id,
                                     struct pt_regs *regs)
  {
    unsigned int       cmplnstatus;
    struct net_device *dev      = dev_id;
    np_dma            *dma      = mtip_dma_control_port;
    int                old_dmastatus;
    np_mtip_mac       *pmac;

    ++gMtipDmaints;

    pmac          = ((np_mtip_mac *) dev->base_addr);
    old_dmastatus = dma->np_dmastatus;

    if(old_dmastatus & np_dmastatus_busy_mask)
      {
        ++gMtipDmaintsBusy;

        if(old_dmastatus & np_dmastatus_done_mask)
          {
            ++gMtipDmaintsBusyDone;
          }

        return IRQ_HANDLED;
          // ...This could be interesting...!
      }

    if(!(old_dmastatus & np_dmastatus_done_mask))
      {
        ++gMtipDmaintsNoDone;
          // presumably gMtipDmaState .eq. Mtip_DmaState_Idle
      }

    dma->np_dmastatus = 0;  // Clear done bit (and ack the interrupt)

    switch (gMtipDmaState)
      {
        case Mtip_DmaState_RxFifo2Tmp:

          pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
            // acknowledge frame reception

          gMtipDmaState = Mtip_DmaState_RxTmp2Skb;
          dma_start
              (2,                               // Byts per transfer
               gMtipTmpDmaBuf,                  // 32 bit aligned src
               gpMtipRxData,                    // 16 bit aligned dest
               (gMtipRxSkbFifoNumL32s << 1),    // # of 2 byte transfers
               (  0                             // Neither end a fifo
                | np_dmacontrol_i_en_mask       // Dma done:interrupt
                ));

          goto DmaIntExit_label;

        case Mtip_DmaState_RxTmp2Skb:

//;see state RxFifo2Tmp; pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
            // acknowledge frame reception

          PRINTK3
                (
                 "%s:Received %d byte Packet 0x%08X\n",
                 dev->name,
                 gMtipRxSkbframelenbyts,
                 ((unsigned long) gpMtipRxData));

          #if MTIP_DEBUG > 3
            print_packet( gpMtipRxData, gMtipRxSkbframelenbyts );
          #endif

          gpMtipRxSkbInProg->protocol =
              eth_type_trans(gpMtipRxSkbInProg, dev );

          netif_rx(gpMtipRxSkbInProg);

          pmac->IRQ_CONFIG |= mmac_ic_EN_RX_FRAME_AVAILABLE_mask;
            // enable rx ready interrupt
          // Note DmaMaybe2Idle_label will find NEITHER
          //  of gMtipDmaQ's Mtip_DmaQ_RxFifo2Tmp NOR
          //  Mtip_DmaQ_RxFifo2Trash bits set!

          goto DmaMaybe2Idle_label;

        case Mtip_DmaState_TxSkb2Tmp:

          gMtipDmaState = Mtip_DmaState_TxTmp2Fifo;
          pmac->TX_CMD_STAT = (gMtipTxSkbframelenbyts   |
                               mmac_tcs_FRAME_COMPLETE_mask);
          dma_start
              (4,                               // Byts per transfer
               gMtipTmpDmaBuf,                  // 32 bit aligned src
               ((void *) (na_mtip_mac_txFIFO)), // 32 bit dest fifo
               gMtipTxSkbFifoNumL32s,           // # of 4 byte transfers
               (  np_dmacontrol_wcon_mask       // Dest is a Fifo
                | np_dmacontrol_i_en_mask       // Dma done:interrupt
                ));

          goto DmaIntExit_label;

        case Mtip_DmaState_TxTmp2Fifo:

          pmac->IRQ_CONFIG |= mmac_ic_EN_TX_FIFO_EMPTY_mask;
            // enable tx done interrupt

          goto DmaMaybe2Idle_label;

        case Mtip_DmaState_RxFifo2Trash:

          pmac->RX_CMD_STAT = mmac_rcs_READ_CMD_mask;
            // acknowledge frame reception

          pmac->IRQ_CONFIG |= mmac_ic_EN_RX_FRAME_AVAILABLE_mask;
            // enable rx ready interrupt
          // Note DmaMaybe2Idle_label will find NEITHER
          //  of gMtipDmaQ's Mtip_DmaQ_RxFifo2Tmp NOR
          //  Mtip_DmaQ_RxFifo2Trash bits set!
          goto DmaMaybe2Idle_label;

//      case Mtip_DmaState_Idle:
        default:

          ++gMtipUnexpDmaints;

          PRINTK3
//        printk
                (
                 "mtip_DmaInterrupt:%s,"
                    " Unexpected state:0x%02X"
                    " (sts:0x%02X,"
                    " ctl:0x%04X)\n",
                 dev->name,
                 gMtipDmaState,
                 dma->np_dmastatus,
                 dma->np_dmacontrol);

          // fall thru to DmaMaybe2Idle_label
      }

DmaMaybe2Idle_label:

    if(gMtipDmaQ & Mtip_DmaQ_TxSkb2Tmp)
      {
        gMtipDmaQ &= (~(Mtip_DmaQ_TxSkb2Tmp));

        gMtipDmaState = Mtip_DmaState_TxSkb2Tmp;

        dma_start_TxSkb2Tmp();
      }
    else if (gMtipDmaQ & Mtip_DmaQ_RxFifo2Tmp)
      {
        gMtipDmaQ &= (~(Mtip_DmaQ_RxFifo2Tmp));

        gMtipDmaState = Mtip_DmaState_RxFifo2Tmp;

        dma_start_RxFifo2Tmp();
      }
    else if (gMtipDmaQ & Mtip_DmaQ_RxFifo2Trash)
      {
        gMtipDmaQ &= (~(Mtip_DmaQ_RxFifo2Trash));

        gMtipDmaState = Mtip_DmaState_RxFifo2Trash;

        dma_start_RxFifo2Trash();
      }
    else
      {
        gMtipDmaState = Mtip_DmaState_Idle;

        if(( (pmac->AVL_STATUS) & (mmac_as_RX_FRAME_AVAILABLE_mask) ))
          {
            cmplnstatus = pmac->RX_CMD_STAT;
            if(( cmplnstatus & mmac_rcs_VALID_mask ))
              {
                ++gMtipRxNoints;

                mtip_NuRxReady(dev, cmplnstatus);
                  // mtip_NuRxReady (re)disables Rx interrupts...
              }
          }
      }

DmaIntExit_label:

    return IRQ_HANDLED;
  }
#endif  // MTIPDMA_AVAIL


#ifdef CONFIG_SYSCTL
/*-----------------------------------------------------------
 |
*/
static const char mtip_info_string[] =
"\n"
"info           Provides this information blurb\n"
"....           Remind author to complete\n"
" ...           ...\n"
"....           Remind author to complete\n"
"";
#endif /* endif CONFIG_SYSCTL */


#ifdef CONFIG_SYSCTL
/*-----------------------------------------------------------
 | Sysctl handler for all integer parameters
*/
static int mtip_sysctl_handler(ctl_table    *ctl,
                               int           write,
                               struct file  *filp,
                               void         *buffer,
                               size_t       *lenp)
{
    int                ret;

    ret = 0;
/*
....tbd.............
.....................*/

    return ret;
}
#endif /* endif CONFIG_SYSCTL */


#ifdef CONFIG_SYSCTL
/*-----------------------------------------------------------
 | Sysctl registration function for all parameters (files)
 |
 | Initilizes device's sysctl proc filesystem
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_sysctl_register(struct net_device *dev)
{
    struct mtip_local *lp = (struct mtip_local *)dev->priv;
    static int ctl_name = CTL_MTIP1000;
    ctl_table* ct;
    int i;

    // Make sure the ctl_tables start out as all zeros
    memset(lp->root_table, 0, sizeof lp->root_table);
    memset(lp->eth_table, 0, sizeof lp->eth_table);
    memset(lp->param_table, 0, sizeof lp->param_table);

    // Initialize the root table
    ct = lp->root_table;
    ct->ctl_name = CTL_DEV;
    ct->procname = "dev";
    ct->maxlen = 0;
    ct->mode = 0555;
    ct->child = lp->eth_table;
    // remaining fields are zero

    // Initialize the ethX table (this device's table)
    ct = lp->eth_table;
    ct->ctl_name = ctl_name++; // Must be unique
    ct->procname = dev->name;
    ct->maxlen = 0;
    ct->mode = 0555;
    ct->child = lp->param_table;
    // remaining fields are zero

    // Initialize the parameter (files) table
    // Make sure the last entry remains null
    ct = lp->param_table;
    for (i = 0; i < (CTL_MTIP_LAST_ENTRY-1); ++i)
        {
        // Initialize fields common to all table entries
        ct[i].proc_handler = mtip_sysctl_handler;
        ct[i].extra1 = (void*)dev; // Save our device pointer
        ct[i].extra2 = (void*)lp;  // Save our mtip_local data pointer
        }

    // INFO - this is our only string parameter
    i = 0;
    ct[i].proc_handler = proc_dostring; // use default handler
    ct[i].ctl_name = CTL_MTIP_INFO;
    ct[i].procname = "info";
    ct[i].data = (void*)mtip_info_string;
    ct[i].maxlen = sizeof mtip_info_string;
    ct[i].mode = 0444; // Read only

    // SWVER
    ++i;
    ct[i].proc_handler = proc_dostring; // use default handler
    ct[i].ctl_name = CTL_MTIP_SWVER;
    ct[i].procname = "swver";
    ct[i].data = (void*)version;
    ct[i].maxlen = sizeof version;
    ct[i].mode = 0444; // Read only

/*
....tbd.............
.....................*/
  #ifdef MTIP_DEBUG
/*
....tbd.............
.....................*/
  #endif // MTIP_DEBUG

    // Register /proc/sys/dev/ethX
    lp->sysctl_header = register_sysctl_table(lp->root_table, 1);
}
#endif /* endif CONFIG_SYSCTL */


#ifdef CONFIG_SYSCTL
/*-----------------------------------------------------------
 | Sysctl unregistration when driver closed
*/
static void mtip_sysctl_unregister(struct net_device *dev)
{
    struct mtip_local *lp = (struct mtip_local *)dev->priv;

    unregister_sysctl_table(lp->sysctl_header);
}
#endif /* endif CONFIG_SYSCTL */


void mtip_phymac_synch (struct net_device *dev, int callerflg)
  {
    unsigned long        cmdcfg;
    unsigned long        phy100mbitflg;
    unsigned long        phyfulldupflg;
    unsigned long        phymr1sts;
    unsigned long        phyanegfailedflg;
    np_mtip_mac         *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    phymr1sts = (pmac->mdio0).STATUS;
    /* Read twice to get CURRENT status...?...                          */
    phymr1sts = (pmac->mdio0).STATUS;

    cmdcfg = pmac->COMMAND_CONFIG;

    #ifdef NS83865PHY
       unsigned long        phymr17linkan;

       phymr17linkan = (pmac->mdio0).reg11;

       phy100mbitflg    = (phymr17linkan & 0x0008);
       phyfulldupflg    = (phymr17linkan & 0x0002);

       phyanegfailedflg = (((pmac->mdio0).reg14) & 0x0100);
    #else
       #if defined(TDK78Q2120PHY)
         {
           unsigned long        phymr18diag;

           phymr18diag = (pmac->mdio0).reg12;

           phy100mbitflg    = (phymr18diag & 0x0400);
           phyanegfailedflg = (phymr18diag & 0x1000);
           phyfulldupflg    = (phymr18diag & 0x0800);
         }
       #else
         ...?...
         ...?...
       #endif   // TDK78Q2120PHY
    #endif  // NS83865PHY

    if(callerflg == 0)
      {
        // Caller = NOT Phy interrupt handler

        if((phymr1sts & 0x00000004) != 0)
          {
            // Link is ostensibly OK

            if((((pmac->mdio0).CONTROL) & 0x00001000) != 0)
              {
                // Auto negotiation ostensibly enabled

                if((phymr1sts & 0x00000020) != 0)
                  {
                    // Auto negotiation ostensibly has completed

                    if(phyanegfailedflg)
                      {
                        // Auto negotiation ostensibly has failed

                        if(phy100mbitflg | phyfulldupflg)
                          {
                            // Auto negotiation failure expected to
                            //  have fallen back to 10 mbit half
                            //  duplex - perhaps phy registers aren't
                            //  actually available, and we've been
                            //  reading 0xFFFF's...

                            // A 10 mbit, 1/2 duplex remote partner
                            //  mandates a 1/2 duplex Emac (else any
                            //  amount of traffic at all will almost
                            //  certainly collide up a storm...)
                            // 100 mbit remote partners seem to allow
                            //  duplex mismatches without severe
                            //  loss, at least at the low end of
                            //  their nominal capacity.
                            // A 10 mbit, full duplex remote partner
                            //  probably also requires a matched Emac,
                            //  but hasn't been confirmed...

                            printk("\nmtip_phymac_synch:%s"
                                     " No phyregs?-assuming HalfD\n",
                                   dev->name);

                            pmac->COMMAND_CONFIG |= mmac_cc_HD_ENA_mask;

                            if((pmac->COMMAND_CONFIG  &
                                mmac_cc_HD_ENA_mask) == 0)
                              {
                                printk("\nmtip_phymac_synch:%s"
                                         " HalfD phy, but FullD emac\n",
                                       dev->name);
                              }

                            return;
                          }
                      }
                  }
              }
          }
      }
//    else
//    {
//      // Caller = Phy interrupt handler
//      // If we've got a phy interrupt, then we're so likely
//      //  to also have actual phy registers that we won't
//      //  bother trying to confirm...
//    }

    #if defined(ANNOUNCEPHY)
      printk("\nmtip_phymac_synch:%s  MR1: 0x%08lX\n",
             dev->name,
             phymr1sts);
      if((phymr1sts & 0x00000002) != 0)
        {
          printk("                               Jabber\n");
        }
      if((phymr1sts & 0x00000010) != 0)
        {
          printk("                               Remote Fault\n");
        }
      if((phymr1sts & 0x00000020) != 0)
        {
          printk("                               Autoneg'd\n");
        }
    #endif

    if((phymr1sts & 0x00000004) != 0)
      {
        /* Phy MR1 (status register) indicates link is (now) OK.        */

        #if defined(ANNOUNCEPHY)
          printk("             Link OK:\n");
        #endif

        if(phyfulldupflg)
          {
            /* Link is (now) running full duplex.                    */

            pmac->COMMAND_CONFIG = (cmdcfg &
                                    (~(mmac_cc_HD_ENA_mask)));

            #if defined(ANNOUNCEPHY)
              printk("             FullD\n");
            #endif
          }
          else
          {
            /* Link is (now) running half duplex.                    */

            pmac->COMMAND_CONFIG = (cmdcfg | mmac_cc_HD_ENA_mask);

            if((pmac->COMMAND_CONFIG & mmac_cc_HD_ENA_mask) == 0)
              {
                printk("\nmtip_phymac_synch:%s"
                         " HalfD phy, but FullD emac\n",
                       dev->name);
              }

            #if defined(ANNOUNCEPHY)
              printk("             HalfD\n");
            #endif
          }

        #if defined(ANNOUNCEPHY)
          printk("             %s\n",
                 (phy100mbitflg) ? "100BASE-TX" : "10BASE-T");
        #endif
      }
    #if defined(ANNOUNCEPHY)
      else
      {
        printk("             Link Down\n");

        // ...what if link comes up without a phy interrupt
        // ... and the emac/phy duplexs don't match...?...
      }
    #endif

    #if defined(ANNOUNCEPHY)
      printk("             CMDCF: 0x%08X\n",
             pmac->COMMAND_CONFIG);
      printk("\n");
    #endif

    return;
  }


/*-----------------------------------------------------------
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_phy_configure(struct net_device* dev)
  {
    /* No need to (re)configure advertisement register or (re)start     */
    /*  auto negotiation after the reset that our caller has probably   */
    /*  recently performed if auto negotiation is enabled by default    */
    /*  and all capabilities are to be advertised. Advertisement        */
    /*  register has already defaulted to our capabilities on last      */
    /*  reset, and phy automatically renegotiates when reset, and/or    */
    /*  when link comes (back) up, etc.                                 */

    /* If phy interrupts are required, DO need to reconfigure the       */
    /*  phy's interrupt control register after the reset our caller     */
    /*  has probably recently performed.                                */

    unsigned int    msecswaited;
    unsigned int    my_ad_caps;     // My Advertised capabilities
    unsigned int    my_phy_caps;    // My PHY capabilities
    np_mtip_mac    *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    my_ad_caps  = PHY_ADV_CSMA;
    my_phy_caps = (pmac->mdio0).STATUS;

    /* Note Mx TDK phy board's (9) switches control its inherrent       */
    /*  capabilibles (at the moment, prototype:all off =                */
    /*  all capabilities available)..                                   */

    if(my_phy_caps & (PHY_STS_CAP_TXF_MASK))
      {
//      if(..we're allowing 100mbit full duplex...)
          {
            my_ad_caps |= PHY_ADV_TX_FDX;
          }
      }

    if(my_phy_caps & (PHY_STS_CAP_TXH_MASK))
      {
//      if(..we're allowing 100mbit half duplex...)
          {
            my_ad_caps |= PHY_ADV_TX_HDX;
          }
      }

    if(my_phy_caps & (PHY_STS_CAP_TF_MASK))
      {
//      if(..we're allowing 10mbit full duplex...)
          {
            my_ad_caps |= PHY_ADV_10_FDX;
          }
      }

    if(my_phy_caps & (PHY_STS_CAP_TH_MASK))
      {
//      if(..we're allowing 10mbit half duplex...)
          {
            my_ad_caps |= PHY_ADV_10_HDX;
          }
      }

    (pmac->mdio0).ADV = my_ad_caps;

  #if defined (MTIPPHYIRQ_AVAIL)
    #ifdef NS83865PHY
            (pmac->mdio0).reg15 =
                (  0
                 | NS883865_INTIE_ANEGDONE_MASK
                 | NS883865_INTIE_LSCHG_MASK
                );
    #else
        #ifdef TDK78Q2120PHY
            (pmac->mdio0).reg11 =
                (  0
                 | TDK78_INTIE_ANEGDONE_MASK
                 | TDK78_INTIE_LSCHG_MASK
//               | TDK78_INTIE_RXER_MASK
                );
        #else
            ......
        #endif  // TDK78Q2120PHY
    #endif  // NS83865PHY
  #endif  // MTIPPHYIRQ_AVAIL

    (pmac->mdio0).CONTROL =
        (   0
          | PHY_CTL_ANEG_EN_MASK
          | PHY_CTL_ANEG_RST_MASK
        );

    msecswaited = 0;
    while(((((pmac->mdio0).STATUS) & 0x00000020) == 0) &
          (msecswaited < 15000))
      {
	    mdelay(100);
        msecswaited += 100;
      }

  #if 1
    if((((pmac->mdio0).STATUS) & 0x00000020) != 0)
      {
        printk("mtip_phy_configure:%s, autoneg complete\n",
               dev->name);
      }
      else
      {
        printk("mtip_phy_configure:%s, autoneg started\n",
               dev->name);
      }

    #ifdef NS83865PHY
        PRINTNS83PHYREGS(pmac,
                        ((unsigned int) ((pmac->mdio0).reg14)),
                        ((unsigned int) ((pmac->mdio0).reg15)),
                        ((unsigned int) ((pmac->mdio0).reg11)));
    #else
      #ifdef TDK78Q2120PHY
        PRINTTDKPHYREGS(pmac,
                        ((unsigned int) ((pmac->mdio0).reg11)),
                        ((unsigned int) ((pmac->mdio0).reg12)));
      #else
        ......
      #endif
    #endif
  #endif
  }


/*-----------------------------------------------------------
 | Enable Receive and Transmit, and Rx Interrupts
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_enable( struct net_device *dev )
{
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    PRINTK2("%s:mtip_enable\n", dev->name);

    pmac->COMMAND_CONFIG =
        (  mmac_cc_TX_ENA_mask      // enable transmit
         | mmac_cc_RX_ENA_mask      // enable receive
         | mmac_cc_TX_ADDR_INS_mask // always overwrite source MAC addr
        );

    pmac->IRQ_CONFIG = mmac_ic_EN_RX_FRAME_AVAILABLE_mask;
      // enable rx ready interrupt

  #if defined (MTIPPHYIRQ_AVAIL)
    #if defined (na_mii_irq)
      (*(volatile unsigned long *)
           (((unsigned long *)
                ((((char *)
                      (((int) (mtip_mii_control_port)) +
                       0x0008))))))) =
              ((unsigned long) (0x0001));
        // Enable phy interrupt pass thru to na_mii_irq
    #endif        // na_mii_irq
  #endif        // MTIPPHYIRQ_AVAIL
}


/*-----------------------------------------------------------
 | Perform a software Reset.
 | Takes some time so we need to wait until it is finshed.
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_mac_SwReset( struct net_device* dev )
  {
////    struct mtip_local  *lp      = (struct mtip_local *)dev->priv;
    np_mtip_mac        *pmac;
    int                 timeout;

    pmac = ((np_mtip_mac *) dev->base_addr);

    (pmac->mdio0).CONTROL = PHY_CTL_RST_MASK; // Reset the phy

    // set reset and Gig-Speed bits to make sure we have an
    //  incoming clock on tx side.
    // If there is a 10/100 PHY, we will still have a valid clock on
    //  tx_clk no matter what setting we have here, but on a Gig phy the
    // MII clock may be missing.

    pmac->COMMAND_CONFIG = mmac_cc_SW_RESET_mask | mmac_cc_ETH_SPEED_mask;

    // wait for completion with fallback in case there is no PHY or it is
    // not connected and hence might not provide any clocks at all.
    timeout=0;
    while( (pmac->COMMAND_CONFIG & mmac_cc_SW_RESET_mask) != 0 &&
           timeout < 10000) timeout++;

    pmac->COMMAND_CONFIG = 0;

    // Cleanup pending "forgotten" DMAs

    #if defined (MTIPDMA_AVAIL)
        ((np_dma *) (na_dma))->np_dmacontrol = 0;
        ((np_dma *) (na_dma))->np_dmastatus  = 0;
        gMtipDmaState                        = Mtip_DmaState_Idle;
    #endif  // MTIPDMA_AVAIL

    // flush RX FIFO

/*
....?...tbd....?............
............................*/
  }


/*-----------------------------------------------------------
 | soft reset the device
 |
 | Sets the Emac to its normal state.
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_reset( struct net_device* dev )
{
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    PRINTK2("%s:mtip_reset\n", dev->name);

    mtip_mac_SwReset( dev );

    // disable all IRQs
    //
    pmac->IRQ_CONFIG = 0;
    //
    #ifdef NS83865PHY
        (pmac->mdio0).reg15 = 0;
    #else
      #ifdef TDK78Q2120PHY
        (pmac->mdio0).reg11 = 0;
      #else
        ......
      #endif
    #endif
    //
  #if defined (MTIPPHYIRQ_AVAIL)
    #if defined (na_mii_irq)
      (*(volatile unsigned long *)
           (((unsigned long *)
                ((((char *)
                      (((int) (mtip_mii_control_port)) +
                       0x0008))))))) =
              ((unsigned long) (0x0000));
        // Disable phy interrupt pass thru to na_mii_irq
    #endif        // na_mii_irq
  #endif        // MTIPPHYIRQ_AVAIL
}


/*-----------------------------------------------------------
 */
static void mtip_reset_config(struct net_device *dev)
  {
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    mtip_reset( dev );

    pmac->MAC_0             =
        ((unsigned int)
            (*((unsigned long *)
                  (&(((unsigned char *)
                         (dev->dev_addr))[0])))));
    pmac->MAC_1             =
        ((unsigned int)
            (*((unsigned short *)
                  (&(((unsigned char *)
                         (dev->dev_addr))[4])))));

    pmac->FRM_LENGTH        = MTIP_MAC_MAX_FRAME_SIZE;
    pmac->PAUSE_QUANT       = 0xff00;

    pmac->RX_SECTION_EMPTY  = 0;        // Auto tx pause DISabled
//  pmac->RX_SECTION_EMPTY  = 1900/4;
      // If RX FIFO fills to this level, PAUSE frame is sent

    pmac->RX_SECTION_FULL   = 0;        // Store&forward (must be zero)
    pmac->TX_SECTION_EMPTY  = (256-16)/4;

    pmac->TX_SECTION_FULL   = 0;        // NO Early start tx
//  pmac->TX_SECTION_FULL   = 128/4;    // Early start tx: 128 bytes in FIFO
      // If TxFifo smaller than outbound packet, early start Tx
      //  MUST be enabled.
      // Slow memory feeding TxFifo must NOT enable early start Tx.

    pmac->RX_ALMOST_EMPTY   = 8;        //
    pmac->RX_ALMOST_FULL    = 10;       //
    pmac->TX_ALMOST_EMPTY   = 8;        //
    pmac->TX_ALMOST_FULL    = 16;       // Need at least 14 to cope
                                        //  with Avalon/DMA latency
    return;
  }


/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts ENabled
 */
static void mtip_timeout (struct net_device *dev)
{
//  PRINTK3
    printk
          (
//         KERN_WARNING
               "%s:mtip_timeout\n",
           dev->name);

    /* If we get here, some higher level has decided we are broken. */

  #if 0
....FIXME:...preemption sensitive stuff to be accessed with
.... cpu interrupts DISabled...
....If Dma state one of the TX states, must stop the
.... Dma and change to idle state or handle gMtipDmaQ
.... Mtip_DmaQ_RxFifo2Tmp/Mtip_DmaQ_RxFifo2Trash flag(s)
....Ensure Tx "done" interrupt is DISabled
....
....Empirical observation: we are toast no matter
.... what we do [not]do...........
    if(gpMtipTxSkbInProg)
      {
        #if 0
          printk("%s:mtip_timeout, txSkb 0x%08X freed\n",
                 dev->name,
                 ((unsigned int) (gpMtipTxSkbInProg)));
        #endif

        dev_kfree_skb_any (gpMtipTxSkbInProg);
        gpMtipTxSkbInProg = NULL;

        #if defined (MTIPDMA_AVAIL)
          gMtipDmaQ &= (~(Mtip_DmaQ_TxSkb2Tmp));
        #endif
      }

    mtip_reset_config( dev );
    mtip_enable( dev );
    mtip_phy_configure(dev);

    mtip_phymac_synch(dev,
                      0);  // Caller = NOT Phy interrupt handler

    netif_wake_queue(dev);
  #endif
}


/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts ENabled.
 */
static int mtip_hard_start_xmit( struct sk_buff    * skb,
                                 struct net_device * dev )
  {
    unsigned long      flags;
    struct mtip_local *lp       = (struct mtip_local *)dev->priv;
    np_mtip_mac       *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    dev->trans_start = jiffies;
    netif_stop_queue(dev);

    PRINTK3("%s:mtip_hard_start_xmit\n", dev->name);

    if ( gpMtipTxSkbInProg ) {
        lp->stats.tx_aborted_errors++;
        printk("mtip_hard_start_xmit:%s, - tx request while busy.\n",
               dev->name);
        return 1;   // Tell caller to retry "later" (if/after someone
                    //  invokes netif_wake_queue(...))...
    }

    gMtipTxSkbframelenbyts = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;

    if(gMtipTxSkbframelenbyts > MTIP_MAC_MAX_FRAME_SIZE)
      {
        printk("mtip_hard_start_xmit:%s, oversized %d byte packet.\n",
               dev->name,
               gMtipTxSkbframelenbyts);
        dev_kfree_skb (skb);
        netif_wake_queue(dev);
        return 0;
      }

    gpMtipTxData = skb->data;

    PRINTK3
          (
           "%s:Transmitting %d byte Packet 0x%08X\n",
           dev->name,
           gMtipTxSkbframelenbyts,
           ((unsigned long) (gpMtipTxData)));
    #if MTIP_DEBUG > 3
      print_packet( gpMtipTxData, gMtipTxSkbframelenbyts );
    #endif

    gMtipTxSkbFifoNumL32s = ((gMtipTxSkbframelenbyts + 3) >> 2);

    ++(lp->stats.tx_packets);

    local_irq_save(flags);

    gpMtipTxSkbInProg = skb;

    #if defined (MTIPDMA_AVAIL)

      if(gMtipDmaState == Mtip_DmaState_Idle)
        {
          gMtipDmaState = Mtip_DmaState_TxSkb2Tmp;
          local_irq_restore(flags);

          dma_start_TxSkb2Tmp();
        }
        else
        {
          gMtipDmaQ |= Mtip_DmaQ_TxSkb2Tmp;

          local_irq_restore(flags);
        }
    #else
      pmac->TX_CMD_STAT = (gMtipTxSkbframelenbyts  |
                           mmac_tcs_FRAME_COMPLETE_mask);

      outsl(((unsigned long) PIO_port_txFIFO),
            gpMtipTxData,
            gMtipTxSkbFifoNumL32s );
        // If TxFifo smaller than outbound packet, early start Tx
        //  must be enabled, else we'll overrun the TxFifo (which
        //  "could" happen anyway with a "fast" cpu).
        // If early start Tx enabled, preemption must be disabled,
        //  else we might underrun the TxFifo.

      local_irq_restore(flags);

      pmac->IRQ_CONFIG |= mmac_ic_EN_TX_FIFO_EMPTY_mask;
        // enable tx done interrupt
    #endif  // MTIPDMA_AVAIL

    return 0;
  }


/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t mtip_RxInterrupt(int             irq,
                                    void           *dev_id,
                                    struct pt_regs *regs)
  {
    unsigned int            cmplnstatus;
    struct   net_device    *dev         = dev_id;
    np_mtip_mac            *pmac;

    /* RxInterrupt condition self clears if/when RxFifo accessed.       */

    pmac = ((np_mtip_mac *) dev->base_addr);

    ++gMtipRxints;

    if(!((pmac->IRQ_CONFIG) & (mmac_ic_EN_RX_FRAME_AVAILABLE_mask)))
      {
        ++gMtipDisabledRxints;
      }

    #if defined (MTIPDMA_AVAIL)

    switch (gMtipDmaState)
      {
        case Mtip_DmaState_RxFifo2Tmp:
        case Mtip_DmaState_RxTmp2Skb:
        case Mtip_DmaState_RxFifo2Trash:

          ++gMtipRxintsRxdmaBusy;

          return IRQ_HANDLED;
            // ...This could be interesting...!

//      case Mtip_DmaState_TxSkb2Tmp:
//      case Mtip_DmaState_TxTmp2Fifo:
//      case Mtip_DmaState_Idle:
//      default:
      }

    if (gMtipDmaQ & (Mtip_DmaQ_TxSkb2Tmp    |
                     Mtip_DmaQ_RxFifo2Tmp   |
                     Mtip_DmaQ_RxFifo2Trash))
      {
        ++gMtipRxintsRxdmaQued;

        return IRQ_HANDLED;
          // ...This could be interesting...!

      }
    #endif  // MTIPDMA_AVAIL

    cmplnstatus = pmac->RX_CMD_STAT;

    if(!( cmplnstatus & mmac_rcs_VALID_mask ))
      {
        ++gMtipUnexpRxints;

        PRINTK3
//      printk
              (
               "mtip_RxInterrupt:%s, but RxStatus:0x%08X INvalid\n",
               dev->name,
               cmplnstatus);

        /* ?...RxInterrupt condition...?                                */
      }
      else
      {
        mtip_NuRxReady(dev, cmplnstatus);
      }

    return IRQ_HANDLED;
  }


/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t mtip_TxInterrupt(int             irq,
                                    void           *dev_id,
                                    struct pt_regs *regs)
  {
    unsigned long        cmdcfg;
    struct   net_device *dev        = dev_id;
    struct   mtip_local *lp;
    int                  old_txcmdstat;
    np_mtip_mac         *pmac;

    // Mtip's Tx fifo supposedly could alternatively
    //  "flow control" the Dma adequately enough to let
    //  us save this interrupt (if we don't need tx done
    //  error tallying).
    //...Jun2004...NOT the case?...perhaps early tx start
    //... plays a part therein...?...

    lp   = (struct mtip_local *)dev->priv;
    pmac = ((np_mtip_mac *) dev->base_addr);

    ++gMtipTxints;

    if(!((pmac->IRQ_CONFIG) & (mmac_ic_EN_TX_FIFO_EMPTY_mask)))
      {
        ++gMtipDisabledTxints;
      }

    cmdcfg = pmac->COMMAND_CONFIG;
    if((cmdcfg & mmac_cc_EXCESS_COL_mask) != 0)
      {
        ++(lp->stats.collisions);
      }
    if((cmdcfg & mmac_cc_LATE_COL_mask) != 0)
      {
		lp->stats.tx_window_errors++;
          // ifconfig displays as "carrier" errors.

        PRINTK3
//      printk
              (
//             KERN_NOTICE
			   "mtip_TxInterrupt:%s, Late collision on last xmit.\n",
			   dev->name);
              }

    old_txcmdstat = pmac->TX_CMD_STAT;

    if(gpMtipTxSkbInProg)
      {
        if(!(old_txcmdstat & mmac_tcs_FRAME_COMPLETE_mask))
          {
            dev_kfree_skb_any (gpMtipTxSkbInProg);
            gpMtipTxSkbInProg = NULL;
          }
          else
          {
            ++gMtipTxintsIncomplete;

            return IRQ_HANDLED;
              // ...This could be interesting...!
          }
      }
      else
      {
        ++gMtipUnexpTxints;
      }

    pmac->IRQ_CONFIG &= (~(mmac_ic_EN_TX_FIFO_EMPTY_mask));

    netif_wake_queue(dev);

    return IRQ_HANDLED;
  }


#if defined (MTIPPHYIRQ_AVAIL)
/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t mtip_PhyInterrupt(int             irq,
                                     void           *dev_id,
                                     struct pt_regs *regs)
  {
    struct   net_device    *dev      = dev_id;
    np_mtip_mac            *pmac;
  #ifdef NS83865PHY
      unsigned int          nsIntstsReg20;
      unsigned int          nsIntieReg21;
      unsigned int          nsLnkstsReg17;
  #else
    #ifdef TDK78Q2120PHY
      unsigned int          tdkDiagReg18;
      unsigned int          tdkintCtlStsReg17;
    #endif
  #endif

    pmac = ((np_mtip_mac *) dev->base_addr);

    #if defined(ANNOUNCEPHY)
      printk
             (
              "mtip_PhyInterrupt:%s\n",
              dev->name);
    #endif

    #ifdef NS83865PHY
        nsIntstsReg20 = (pmac->mdio0).reg14;
        nsIntieReg21  = (pmac->mdio0).reg15;
        nsLnkstsReg17 = (pmac->mdio0).reg11;

        #if 0
          PRINTNS83PHYREGS(pmac,
                           nsIntstsReg20,
                           nsIntieReg21,
                           nsLnkstsReg17);
        #endif

        (pmac->mdio0).reg17 = nsIntstsReg20;
          // Ack (all) interrupt condition(s)
    #else
      #ifdef TDK78Q2120PHY
        tdkintCtlStsReg17 = (pmac->mdio0).reg11;
          // Read TDK 78Q2120 interrupt control/status register
          // Also acks the interrupt condition(s)

        tdkDiagReg18 = (pmac->mdio0).reg12;
          // Read TDK 78Q2120 diagnostic register
          // Also clears auto-negotiation failed bit

        #if 0
          PRINTTDKPHYREGS(pmac, tdkintCtlStsReg17, tdkDiagReg18);
        #endif
      #else
          ......
      #endif
    #endif

    mtip_phymac_synch((struct net_device *) dev_id,
                      1);  // Caller = Phy interrupt handler

    return IRQ_HANDLED;
  }
#endif // MTIPPHYIRQ_AVAIL


/*-----------------------------------------------------------
 | Driver entry point (eg: ifconfig ethX up).
 |                    (eg: ifattach ...)
 |
 | Open and Initialize the board
 |
 | Set up everything, reset the card, etc ..
 |
 | Entry condition: Cpu interrupts ENabled.
 */
static int mtip_open(struct net_device *dev)
{
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    PRINTK2("%s:mtip_open\n", dev->name);

    memset(dev->priv, 0, sizeof(struct mtip_local));


  #ifdef CONFIG_SYSCTL
    // Set default parameters (files)
/*
....tbd.............
.....................*/
  #endif

    mtip_reset_config( dev );
    mtip_enable( dev );
    mtip_phy_configure(dev);

    mtip_phymac_synch(dev,
                      0);  // Caller = NOT Phy interrupt handler

#ifdef CONFIG_SYSCTL
    mtip_sysctl_register(dev);
#endif /* CONFIG_SYSCTL */

    netif_start_queue(dev);

    return 0;
}


/*-----------------------------------------------------------
 | close down the Emac
 |
 | put the device in an inactive state
 |
*/
static void mtip_shutdown( struct net_device *dev )
{
    PRINTK2("%s:mtip_shutdown\n", dev->name);

/*
....tbd.............
...........................*/
}


/*-----------------------------------------------------------
 | Driver entry point (eg: ifconfig ethX down).
 |
 | Clean up everything from the open routine
*/
static int mtip_close(struct net_device *dev)
{
    PRINTK2("%s:mtip_close\n", dev->name);

    netif_stop_queue(dev);

    if(gpMtipTxSkbInProg)
      {
        dev_kfree_skb_any (gpMtipTxSkbInProg);
        gpMtipTxSkbInProg = NULL;

        #if defined (MTIPDMA_AVAIL)
          gMtipDmaQ &= (~(Mtip_DmaQ_TxSkb2Tmp));
        #endif
      }

  #if defined (MTIPDMA_AVAIL)
    gMtipDmaQ         = 0;
    gMtipDmaState     = Mtip_DmaState_Idle;
  #endif        // MTIPDMA_AVAIL

#ifdef CONFIG_SYSCTL
    mtip_sysctl_unregister(dev);
#endif /* CONFIG_SYSCTL */

    /* clear everything */
    mtip_shutdown( dev );

    /* Update the statistics here. */

    return 0;
}


/*-----------------------------------------------------------
 | Driver entry point (re unregister_netdev()).
 |
 | Cleaning up before driver finally unregistered and discarded.
 |
 |   Input parameters:
 |  dev, pointer to the device structure
 |
 |   Output:
 |  None.
 |
 ---------------------------------------------------------------------------
*/
void mtip_destructor(struct net_device *dev)
{
    PRINTK2("%s:mtip_destructor\n", dev->name);
}


/*-----------------------------------------------------------
 | Driver entry point.
 |
 | Get the current statistics.
 |
 | Allows proc file system to query the driver's statistics.
 |
 | May be called with the card open or closed.
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static struct net_device_stats* mtip_query_statistics(struct net_device *dev)
  {
    struct mtip_local *lp = (struct mtip_local *)dev->priv;

    PRINTK2("%s:mtip_query_statistics\n", dev->name);

    return &lp->stats;
  }


/*-----------------------------------------------------------
 | Clear the multicast table to disable multicast reception.
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_mac_clearMulticast(struct net_device *dev)
  {
    int           i;
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    for(i=0; i < 64; i++ )
      {
        (pmac->hashtable)[i] = 0;
      }
  }


/*-----------------------------------------------------------
 | Fill the multicast table to enable reception of all multicast frames.
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_mac_promiscuousMulticast(struct net_device *dev)
  {
    int           i;
    np_mtip_mac  *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    for(i=0; i < 64; i++ )
      {
        (pmac->hashtable)[i] = 1;
      }
  }


/*-----------------------------------------------------------
 | Reprogram multicast mac addresses into hardware multicast table
 |
 | Caller has already cleared existing hash entries as appropriate
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_setmulticast( struct net_device    *dev,
                                      int            count,
                               struct dev_mc_list   *addrs )
  {
    int                   a;
    int                   bit;
    int                   bitidx;
    int                   hash;
    int                   i;
    char                  ch;
    struct   dev_mc_list *cur_addr;
    np_mtip_mac          *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    PRINTK2
          (
           "mtip_setmulticast:%s\n",
           dev->name);

    cur_addr = addrs;

    for ( i = 0; i < count ; i ++, cur_addr = cur_addr->next )
      {
        /* do we have a pointer here? */
        if ( !cur_addr )
            break;

        /* make sure this is a multicast address    */
        if ( !( *cur_addr->dmi_addr & 1 ) )
            continue;

        hash = 0; // the hash value

        for(a=5; a >= 0; a--)
          {
            // loop through all 6 bytes of this mac address

            bit = 0; // the bit calculated from the byte
            ch = (cur_addr->dmi_addr)[a];

            for(bitidx=0;bitidx < 8;bitidx++)
              {
                bit ^= (int)((ch >> bitidx) & 0x01);
              }

            hash = (hash << 1) | bit;
          }

          #if (MTIP_DEBUG > 2 )
//          printk("  ");
            printk("mtip_setmulticast: ");
            for(a=0; a < 6; a++)
              {
                ch = (cur_addr->dmi_addr)[a];

                printk("  %02X",ch);
              }
            printk("  hash(%d)\n",hash);
          #endif

        (pmac->hashtable)[ hash ] = 1;
      }
  }


/*-----------------------------------------------------------
 | Driver entry point.
 |
 | This routine will, depending on the values passed to it,
 | either make it accept multicast packets, go into
 | promiscuous mode ( for TCPDUMP and cousins ) or accept
 | a select set of multicast packets
 |
 | Entry condition: Cpu interrupts ENabled.
*/
static void mtip_set_multicast_list(struct net_device *dev)
  {
    np_mtip_mac          *pmac;

    pmac = ((np_mtip_mac *) dev->base_addr);

    PRINTK2
          (
           "%s:mtip_set_multicast_list\n",
           dev->name);

    if ( dev->flags & IFF_PROMISC )
      {
        PRINTK2
//      printk
               (
                "%s:mtip_set_multicast_list:RCR_PRMS\n", dev->name);

        mtip_mac_setPromiscuous(pmac);
      }
      else
      {
        PRINTK2
//      printk
              (
               "%s:mtip_set_multicast_list:~RCR_PRMS\n", dev->name);

        mtip_mac_clearPromiscuous(pmac);
      }

    if (dev->flags & IFF_ALLMULTI)
      {
        PRINTK2
//      printk
              (
               "%s:mtip_set_multicast_list:RCR_ALMUL\n", dev->name);

        mtip_mac_promiscuousMulticast(dev);
      }
      else
      {
        PRINTK2
              (
               "%s:mtip_set_multicast_list:~RCR_ALMUL\n", dev->name);

        mtip_mac_clearMulticast(dev); // Clear any existing hash entries

        if(dev->mc_count)
          {
            mtip_setmulticast( dev,
                               dev->mc_count,
                               dev->mc_list );
          }
      }
  }


/*-----------------------------------------------------------
 | Entry condition: Cpu interrupts ENabled
 |                   (in SPITE of claims disabled...).
*/
static int __init mtip_probe(struct net_device *dev, unsigned int ioaddr )
{
    int              i;
    np_mtip_mac     *pmac;
    int              retval;
    static unsigned  version_printed        = 0;


    pmac = ((np_mtip_mac *) ioaddr);

    PRINTK2("%s:mtip_probe\n", dev->name);

    SET_MODULE_OWNER (dev);

#if 1
    #ifdef CONFIG_EXCALIBUR
        printk("mtip_probe:%s, %d Khz Nios (%s) (%s)\n",
               dev->name,
               nasys_clock_freq_1000,
               PHYTYPE,
               IOTYPE);
    #endif
#endif

    /* Grab the region so that no one else tries to probe our ioports. */
    if (!request_region(ioaddr,
                        MTIP1000_IO_EXTENT,
                        dev->name)) return -EBUSY;

    if (version_printed++ == 0) printk("%s", version);

    dev->base_addr = ioaddr;

  #ifdef NS83865PHY
    {
      int            oldmdioaddr0;
      int            phyaddr                = -1;

      // Empirical observation: expect NS83865PHY's phy address = 2

      oldmdioaddr0 = pmac->MDIO_ADDR0;

      for (i = 0; i <= 31; i++)
        {
          pmac->MDIO_ADDR0 = i;

          if(((pmac->mdio0).PHY_ID1) == 0x2000)
            {
              phyaddr = i;

              break;
            }
        }

      if(phyaddr >= 0)
        {
          pmac->MDIO_ADDR0 = phyaddr;
        }
        else
        {
          pmac->MDIO_ADDR0 = oldmdioaddr0;

          printk("mtip_probe:%s, (%s) phy not found"
                   ", defaulting to addr:0x%02X\n",
                 dev->name,
                 PHYTYPE,
                 pmac->MDIO_ADDR0);
        }
    }
    #else
      #ifdef TDK78Q2120PHY
        // TDK78Q2120PHY's respond to the "broadcast" phy address 0,
        //  so leave pmac->MDIO_ADDR0 at its default value 0
      #else
          ......
      #endif
  #endif    // NS83865PHY

    mtip_reset( dev );

    printk("mtip_probe:%s, REV=0x%08x, (%s) Phyaddr:0x%02X\n",
           dev->name,
           pmac->REV,
           PHYTYPE,
           pmac->MDIO_ADDR0);

  #if 0
    #ifdef NS83865PHY
        PRINTNS83PHYREGS(pmac,
                        ((unsigned int) ((pmac->mdio0).reg14)),
                        ((unsigned int) ((pmac->mdio0).reg15)),
                        ((unsigned int) ((pmac->mdio0).reg11)));
    #else
      #ifdef TDK78Q2120PHY
        PRINTTDKPHYREGS(pmac,
                        ((unsigned int) ((pmac->mdio0).reg11)),
                        ((unsigned int) ((pmac->mdio0).reg12)));
      #else
          ......
      #endif
    #endif
  #endif

#ifdef CONFIG_EXCALIBUR
    {
      extern unsigned char *excalibur_enet_hwaddr;

      memcpy(dev->dev_addr, excalibur_enet_hwaddr, 6);
    }
#else
    .........
#endif

    /*
     . Print the Ethernet address
    */
    printk("    ADDR: ");
    for (i = 0; i < 5; i++)
        printk("%2.2x:", dev->dev_addr[i] );
    printk("%2.2x \n", dev->dev_addr[5] );

    /* set the private data to zero by default */
    memset(dev->priv, 0, sizeof(struct mtip_local));

    /* Fill in the fields of the device structure with ethernet values. */
    ether_setup(dev);

    /* Grab the RxIRQ */
    retval = request_irq(dev->irq,
                         mtip_RxInterrupt,
                         0,
                         dev->name,
                         dev);
    if (retval) {
          printk("mtip_probe:%s unable to hook RxIRQ %d (retval=%d).\n",
                 dev->name,
                 dev->irq,
                 retval);

          goto frepriv_err_out;
    }

    retval = request_irq(na_mtip_mac_txFIFO_irq,
                         mtip_TxInterrupt,
                         0,
                         dev->name,
                         dev);
    if (retval) {
          printk("mtip_probe:%s unable to hook TxIRQ %d (retval=%d).\n",
                 dev->name,
                 na_mtip_mac_txFIFO_irq,
                 retval);

          goto freRxIrq_err_out;
    }

  #if defined (MTIPPHYIRQ_AVAIL)
    /* Grab the PhyIRQ */
    retval = request_irq(na_mii_irq_irq,
                         mtip_PhyInterrupt,
                         0,
                         dev->name,
                         dev);
    if (retval)
      {
        printk("mtip_probe:%s unable to hook PhyIRQ %d (retval=%d).\n",
               dev->name,
               na_mii_irq_irq,
               retval);

        goto freTxIrq_err_out;
      }
  #endif    // MTIPPHYIRQ_AVAIL

    gMtipDisabledRxints   = 0;
    gMtipDisabledTxints   = 0;
    gMtipDmaints          = 0;
    gMtipDmaintsBusy      = 0;
    gMtipDmaintsBusyDone  = 0;
    gMtipDmaintsNoDone    = 0;
    gMtipRxints           = 0;
    gMtipRxintsRxdmaBusy  = 0;
    gMtipRxintsRxdmaQued  = 0;
    gMtipRxNoints         = 0;
    gMtipTxints           = 0;
    gMtipTxintsIncomplete = 0;
    gMtipUnexpDmaints     = 0;
    gMtipUnexpRxints      = 0;
    gMtipUnexpTxints      = 0;
    gpMtipTxSkbInProg     = NULL;

  #if defined (MTIPDMA_AVAIL)
    gMtipDmaQ         = 0;
    gMtipDmaState     = Mtip_DmaState_Idle;

    /* Grab the DmaIRQ */
    retval = request_irq(na_dma_irq,
                         mtip_DmaInterrupt,
                         0,
                         dev->name,
                         dev);
    if (retval) {
        printk("mtip_probe:%s unable to hook DmaIRQ %d (retval=%d).\n",
               dev->name,
               na_dma_irq,
               retval);

        goto frePhyIrq_err_out;
    }
  #endif        // MTIPDMA_AVAIL

//  see mtip_phymac_synch for full/half duplex coordination

    dev->open               = mtip_open;
    dev->stop               = mtip_close;
    dev->hard_start_xmit    = mtip_hard_start_xmit;
    dev->tx_timeout         = mtip_timeout;
    dev->get_stats          = mtip_query_statistics;
  #ifdef    HAVE_MULTICAST
    dev->set_multicast_list = &mtip_set_multicast_list;
  #endif

    return 0;

  #if defined (MTIPPHYIRQ_AVAIL)
    #if defined (MTIPDMA_AVAIL)
frePhyIrq_err_out:
    #endif      // MTIPDMA_AVAIL

    free_irq(na_mii_irq_irq, dev);

freTxIrq_err_out:
  #endif      // MTIPPHYIRQ_AVAIL

    free_irq(na_mtip_mac_txFIFO_irq,
             dev);

freRxIrq_err_out:

    free_irq(na_mtip_mac_rxFIFO_irq,
             dev);

frepriv_err_out:

    kfree (dev->priv);
    dev->priv = NULL;

//err_out:
    release_region (ioaddr, MTIP1000_IO_EXTENT);
    return retval;
}


/*-----------------------------------------------------------
 | Driver entry point (called by ethif_probe2()).
 |
 | Return:  0   success.
 |
 | Entry condition: Cpu interrupts ENabled
 |                   (in SPITE of claims disabled...).
*/
struct net_device * __init mtip1000_init(int unit)
{
    struct net_device *dev = alloc_etherdev(sizeof(struct mtip_local));
    int                err = 0;
    int                i;

    if (!dev) return ERR_PTR(-ENODEV);

    sprintf(dev->name, "eth%d", unit);
    netdev_boot_setup_check(dev);

    PRINTK2("%s:mtip1000_init\n", dev->name);

    for (i = 0; mtip_portlist[i]; i++) {
        dev->irq = mtip_irqlist[i];

        if (mtip_probe(dev, mtip_portlist[i]) == 0) break;
    }

    if (!mtip_portlist[i]) err = -ENODEV;
    if (err) goto out;

    err = register_netdev(dev);
    if (err) goto out;
    return dev;
out:
    free_netdev(dev);
    //  printk(KERN_WARNING "mtip1000: no emac unit %d detected.\n",unit);
    return ERR_PTR(err);
}
