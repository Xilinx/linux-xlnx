/*--------------------------------------------------------------------
 * open_eth.c
 *
 * Ethernet driver for Open Ethernet Controller (www.opencores.org).
 *
 * Based on:
 *
 * Ethernet driver for Motorola MPC8xx.
 *      Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * mcen302.c: A Linux network driver for Mototrola 68EN302 MCU
 *
 *      Copyright (C) 1999 Aplio S.A. Written by Vadim Lebedev
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


/*--------------------------------------------------------------------
 * Rigt now XXBUFF_PREALLOC must be used, bacause MAC does not
 * handle unaligned buffers yet.  Also the cache inhibit calls
 * should be used some day.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/bitops.h>
#include <asm/page.h>


#define ANNOUNCEPHYINT
//#undef  ANNOUNCEPHYINT

#ifdef CONFIG_EXCALIBUR
    #include <asm/nios.h>
    #include <asm/cacheflush.h>
    #define _print              printk
    #define MACIRQ_NUM          na_igor_mac_irq
    #define PHYIRQ_NUM          na_mii_irq_irq
    #define ETH_BASE_ADD        na_igor_mac
    #ifdef na_mii_irq
      #define PHYIRQ_BASE_ADDR  na_mii_irq
    #else
      #ifdef na_mii_irq_base
        #define PHYIRQ_BASE_ADDR  na_mii_irq_base
      #else
        ...?...
      #endif
    #endif
    #define TDK78Q2120PHY
      #define NUM_PHY_REGS                            19
        /* Numbered 0, 1, ...       (NUM_PHY_REGS - 1)                  */
//    #define PHY_ADDRESS                           0x1F
      // TDK78Q2120PHY's respond to the "'broadcast" phy address 0
      #define PHY_ADDRESS                           0x00
#endif  // CONFIG_EXCALIBUR

#include "open_eth.h"

#define __clear_user(add,len) memset((add),0,(len))

#define OETH_DEBUG 0
//#define OETH_DEBUG 1

#if OETH_DEBUG > 1
    #define PRINTK2(args...) printk(args)
#else
    #define PRINTK2(args...)
#endif  // OETH_DEBUG > 1

#ifdef OETH_DEBUG
    #define PRINTK(args...) printk(args)
#else
    #define PRINTK(args...)
#endif  // OETH_DEBUG

#undef SANCHKEPKT
//#define SANCHKEPKT

#define RXBUFF_PREALLOC 1
#define TXBUFF_PREALLOC 1

/* The transmitter timeout
 */
#define TX_TIMEOUT  (2*HZ)

/* Buffer number (must be 2^n)
 */
//;dgt;;;#define OETH_RXBD_NUM      8
#define OETH_RXBD_NUM       16
//;dgt;;;#define OETH_TXBD_NUM      8
#define OETH_TXBD_NUM       16

#define OETH_RXBD_NUM_MASK  (OETH_RXBD_NUM-1)
#define OETH_TXBD_NUM_MASK  (OETH_TXBD_NUM-1)

/* Buffer size
 */
#define OETH_RX_BUFF_SIZE   2048
#define OETH_TX_BUFF_SIZE   2048

/* How many buffers per page
 */
#define OETH_RX_BUFF_PPGAE  (PAGE_SIZE/OETH_RX_BUFF_SIZE)
#define OETH_TX_BUFF_PPGAE  (PAGE_SIZE/OETH_TX_BUFF_SIZE)

/* How many pages is needed for buffers
 */
#define OETH_RX_BUFF_PAGE_NUM   (OETH_RXBD_NUM/OETH_RX_BUFF_PPGAE)
#define OETH_TX_BUFF_PAGE_NUM   (OETH_TXBD_NUM/OETH_TX_BUFF_PPGAE)

/* Buffer size  (if not XXBUF_PREALLOC
 */
#define MAX_FRAME_SIZE      1518

#ifdef CONFIG_EXCALIBUR
  #define TOTBYTSALLRXBUFS  (OETH_RXBD_NUM * OETH_RX_BUFF_SIZE)
  #define TOTBYTSALLTXBUFS  (OETH_TXBD_NUM * OETH_TX_BUFF_SIZE)
  #define TOTBYTSALLBUFS    (TOTBYTSALLRXBUFS + TOTBYTSALLTXBUFS)
  #if(na_sram_size >= TOTBYTSALLBUFS)
    #define SRAM_BUFF   1
    #define SRAM_BUFF_BASE  (na_sram_base)
  #else
    #undef SRAM_BUFF
    #undef SRAM_BUFF_BASE
  #endif
#else
  //#define SRAM_BUFF   1
  //#define SRAM_BUFF_BASE  (FBMEM_BASE_ADD + 0x80000)
#endif

/* The buffer descriptors track the ring buffers.
 */
struct oeth_private {
    struct sk_buff  *rx_skbuff[OETH_RXBD_NUM];
    struct sk_buff  *tx_skbuff[OETH_TXBD_NUM];

    ushort           tx_next;   /* Next buffer to be sent */
    ushort           tx_last;   /* Next buffer to be checked if packet sent */
    ushort           tx_full;   /* Buffer ring fuul indicator */
    ushort           rx_cur;    /* Next buffer to be checked if packet received */

    oeth_regs       *regs;      /* Address of controller registers. */
    oeth_bd         *rx_bd_base;/* Address of Rx BDs. */
    oeth_bd         *tx_bd_base;/* Address of Tx BDs. */

    struct net_device_stats stats;
};

#ifdef SANCHKEPKT

  #ifndef UCHAR
    #define UCHAR                   unsigned char
  #endif  // UCHAR

  #ifndef USHORT
    #define USHORT                  unsigned short
  #endif  // USHORT

  #ifndef ULONG
    #define ULONG                   unsigned long
  #endif  // ULONG

  #ifndef IP_TYPE_HFMT
    #define IP_TYPE_HFMT            0x0800
  #endif  // IP_TYPE_HFMT

  #ifndef ICMP_PROTOCOL
    #define ICMP_PROTOCOL           1
  #endif  // ICMP_PROTOCOL

  #ifndef TCP_PROTOCOL
    #define TCP_PROTOCOL            6
  #endif  // TCP_PROTOCOL

  #ifndef UDP_PROTOCOL
    #define UDP_PROTOCOL            17
  #endif  // UDP_PROTOCOL

  #ifdef CONFIG_EXCALIBUR

    #define read_b8(addr)   (*(volatile unsigned char *) (addr))

    #define rd8(addr)       read_b8((volatile unsigned char *) (addr))

    static USHORT read_w16(volatile USHORT *addr)
      {
        if((((ULONG) (addr)) & 0x00000001) == 0)
          {
            return *((volatile USHORT *) (addr));
          }
          else
          {
            return (rd8(                   addr)             |
                   (rd8(((unsigned char *) addr) + 1) << 8));
          }
      }

    #define rd16(addr)   read_w16((volatile USHORT *) (addr))

//    static ULONG read_l32(volatile ULONG *addr)
//      {
//        if((((ULONG) (addr)) & 0x00000003) == 0)
//          {
//            return *((volatile ULONG *) (addr));
//          }
//          else
//          {
//            return (rd16(                     addr)                 |
//                   (rd16(((((unsigned char *) addr) + 2))) << 16));
//          }
//      }
//
//    #define rd32(addr)   read_l32((volatile ULONG *) (addr))

  #endif  // CONFIG_EXCALIBUR

  USHORT onessum(const UCHAR      *buf,
                 const USHORT      len,
                 const USHORT      inisum)
  {
    USHORT      iLen16      = (len >> 1);
    USHORT      iLen2_8;
    USHORT      i;
    ULONG       finalsum;
    ULONG       sum         = inisum;
    USHORT      din;

    iLen2_8 = (iLen16 << 1);

    for (i = 0; i < iLen2_8; i += 2)
      {
        din = htons(rd16(buf + i));
        sum = sum + din;
      }

    if((len & 1) != 0)
      {
        sum = sum + ((buf [iLen2_8]) << 8);
      }

    finalsum = (sum & 0xffff) + ((sum >> 16) & 0xffff);

    sum = (finalsum & 0xffff) + ((finalsum >> 16) & 0xffff);
      // Addition of carries can in turn produce yet another
      //  (at the most 1) carry (whose addition in turn can
      //  produce no further carries).

    sum &= 0xffff;

    /* (Final) caller must complement our return value (and, if,        */
    /*  applicable, complement once again if zero and Udp)              */

    return (USHORT)sum;
  }

  USHORT psuchksum(const UCHAR    *tcpudpbuf,
                   const USHORT    tcpudplen,
                   const UCHAR    *ipbuf,
                   const USHORT    uoset2chksm)
  {
    unsigned int            uiChksum;
    unsigned int            uiTmp;


    // Tcp/Udp pseudo header

    uiChksum = onessum((ipbuf + 0x0C),4, 0);

    uiChksum = onessum((ipbuf + 0x10),4, uiChksum);

    uiTmp = ((ipbuf [0x09]) << 8);
    uiChksum = onessum(((UCHAR *) (&(uiTmp))),2, uiChksum);

    uiTmp    = htons(tcpudplen);
    uiChksum = onessum(((UCHAR *) (&(uiTmp))),2, uiChksum);
      // Txp/Udp message length, including real header

    // Real header and payload

    uiChksum = onessum(tcpudpbuf,
                       uoset2chksm,
                       uiChksum);

    uiChksum = (onessum((tcpudpbuf + (uoset2chksm + 2)),
                        (tcpudplen - (uoset2chksm + 2)),
                        uiChksum)
               ^ 0xffff);

    if(uoset2chksm == 6)
      {
        /* UDP                                                          */

        if(uiChksum == 0)
          {
            uiChksum = 0xFFFF;
          }
      }

    return uiChksum;
  }

  USHORT icmpchksum(const UCHAR   *icmpbuf,
                    const USHORT   icmplen)
  {
    unsigned int            uiChksum;


    uiChksum = onessum(icmpbuf,
                       0x02,
                       0);

    uiChksum = (onessum((icmpbuf + 0x04),
                        (icmplen     -
                         0x04),
                        uiChksum)
               ^ 0xffff);

    return uiChksum;
  }

  USHORT ipchksum(const UCHAR     *iphdrbuf,
                  const USHORT     iphdrlen)
  {
    unsigned int            uiChksum;


    uiChksum = onessum(iphdrbuf,
                       0x0A,
                       0);

    uiChksum = (onessum((iphdrbuf + 0x0C),
                        (iphdrlen     -
                         0x0C),
                        uiChksum)
               ^ 0xffff);

    return uiChksum;
  }

  static void DoSanchkEpkt(const char            *ptrEpkt,
                           const unsigned long    ulBytcntNOcrc,
                           const char            *ptrBfnam)
  {
    int             iActChksum;
    int             iExptdChksum;
    int             iLenIphdrbyts;
    int             iLenIpinclhdrbyts;

    if(ulBytcntNOcrc >= 0x12)
      {
        if(rd16(&(ptrEpkt [0x0C])) == htons(IP_TYPE_HFMT))
          {
            unsigned short      ui16motoipflgsfrgo;

            // Bluebook etyp 0x0008 (Moto(0x0800)) = IP

            if(((ptrEpkt [0x0E]) & 0xF0) == 0x40)
              {
                // IP version 4

                iLenIphdrbyts = ((ptrEpkt [0x0E]) & 0x0F) * 4;

                iLenIpinclhdrbyts =
                    ntohs(rd16(&(ptrEpkt [0x10])));

                if((iLenIphdrbyts     >= 20)                      &&
                   (iLenIpinclhdrbyts >= iLenIphdrbyts)           &&
                   (ulBytcntNOcrc     >= (iLenIpinclhdrbyts + 0x0E)))
                                            // Dix Machdr 14 bytes
                  {
                    iExptdChksum = ipchksum(&(ptrEpkt [0x0E]),
                                            iLenIphdrbyts);

                    iActChksum = ntohs(rd16(&(ptrEpkt [0x18])));

                    if(iActChksum != iExptdChksum)
                      {
                        if((iExptdChksum != 0x0000)   &&
                           (iExptdChksum != 0xFFFF)   &&
                           (iActChksum   != 0x0000)   &&
                           (iActChksum   != 0xFFFF))
                          {
                            printk("\n...IP %s{0x%08X} xptd"
                                       " csum: 0x%04X"
                                       ", 0x%04X seen (%ld ebyts)\n",
                                   ptrBfnam,
                                   ((unsigned long) ptrEpkt),
                                   iExptdChksum,
                                   iActChksum,
                                   ulBytcntNOcrc);

                            return;
                          }
                      }
                  }
                  else
                  {
                    printk("\n...Malformed IP %s{0x%08X}"
                               " header (%ld ebyts)\n",
                           ptrBfnam,
                           ((unsigned long) ptrEpkt),
                           ulBytcntNOcrc);

                    return;
                  }

                ui16motoipflgsfrgo = rd16(&(ptrEpkt [0x14]));

                if((ntohs(ui16motoipflgsfrgo) & 0x2000) == 0)
                  {
                    /* Final or only IP fragment                        */

                    if(((ntohs(ui16motoipflgsfrgo) & 0x1FFF)) == 0)
                      {
                        // One and only IP fragment

                        if(ptrEpkt [0x17] == ICMP_PROTOCOL)
                          {
                            // IP payload 0x01 = ICMP

                            if((iLenIpinclhdrbyts - iLenIphdrbyts) >= 4)
                              {
                                iExptdChksum =
                                    icmpchksum(&(ptrEpkt
                                                   [0x0E          +
                                                    iLenIphdrbyts]),
                                               (iLenIpinclhdrbyts  -
                                                iLenIphdrbyts));

                                iActChksum =
                                    ntohs(rd16(&(ptrEpkt
                                                   [0x0E          +
                                                    iLenIphdrbyts +
                                                    2])));

                                if(iActChksum != iExptdChksum)
                                  {
                                    if((iExptdChksum != 0x0000)   &&
                                       (iExptdChksum != 0xFFFF)   &&
                                       (iActChksum   != 0x0000)   &&
                                       (iActChksum   != 0xFFFF))
                                      {
                                        printk("\n...ICMP %s{0x%08X}"
                                                 " xptd csum:"
                                                 " 0x%04X, 0x%04X"
                                                 " seen (%ld ebyts)\n",
                                               ptrBfnam,
                                               ((unsigned long) ptrEpkt),
                                               iExptdChksum,
                                               iActChksum,
                                               ulBytcntNOcrc);

                                        return;
                                      }
                                  }
                              }
                              else
                              {
                                printk("\n...Malformed ICMP"
                                         " %s{0x%08X}"
                                         " pkt (%ld ebyts)\n",
                                       ptrBfnam,
                                       ((unsigned long) ptrEpkt),
                                       ulBytcntNOcrc);

                                return;
                              }

                            return;
                          }

                        if(ptrEpkt [0x17] == TCP_PROTOCOL)
                          {
                            // IP payload 0x06 = TCP

                            if((iLenIpinclhdrbyts - iLenIphdrbyts) >= 20)
                              {
                                iActChksum =
                                    ntohs(rd16(&(ptrEpkt [0x0E        +
                                                         iLenIphdrbyts +
                                                         16])));

                                iExptdChksum =
                                    psuchksum(&(ptrEpkt
                                                  [0x0E           +
                                                   iLenIphdrbyts]),
                                              (iLenIpinclhdrbyts  -
                                               iLenIphdrbyts),
                                              &(ptrEpkt [0x0E]),
                                              16);

                                if(iActChksum != iExptdChksum)
                                  {
                                   printk("\n...TCP %s{0x%08X}"
                                            " xptd csum:"
                                            " 0x%04X, 0x%04X"
                                            " seen (%ld ebyts)\n",
                                          ptrBfnam,
                                          ((unsigned long) ptrEpkt),
                                          iExptdChksum,
                                          iActChksum,
                                          ulBytcntNOcrc);

                                    return;
                                  }
                              }
                              else
                              {
                                printk("\n...Malformed TCP"
                                         " %s{0x%08X}"
                                         " pkt (%ld ebyts)\n",
                                       ptrBfnam,
                                       ((unsigned long) ptrEpkt),
                                       ulBytcntNOcrc);

                                return;
                              }
                          }

                        if(ptrEpkt [0x17] == UDP_PROTOCOL)
                          {
                            // IP payload 0x11 = UDP

                            if((iLenIpinclhdrbyts - iLenIphdrbyts) >= 8)
                              {
                                iActChksum =
                                    ntohs(rd16(&(ptrEpkt [0x0E        +
                                                         iLenIphdrbyts +
                                                         6])));

                                if(iActChksum != 0x0000)
                                  {
                                    iExptdChksum =
                                        psuchksum(&(ptrEpkt
                                                      [0x0E           +
                                                       iLenIphdrbyts]),
                                                  (iLenIpinclhdrbyts  -
                                                   iLenIphdrbyts),
                                                  &(ptrEpkt [0x0E]),
                                                  6);

                                    if(iActChksum != iExptdChksum)
                                      {
                                       printk("\n...UDP %s{0x%08X}"
                                                " xptd csum:"
                                                " 0x%04X, 0x%04X"
                                                " seen (%ld ebyts)\n",
                                              ptrBfnam,
                                              ((unsigned long) ptrEpkt),
                                              iExptdChksum,
                                              iActChksum,
                                              ulBytcntNOcrc);

                                        return;
                                      }
                                  }
                              }
                              else
                              {
                                printk("\n...Malformed UDP"
                                         " %s{0x%08X}"
                                         " pkt (%ld ebyts)\n",
                                       ptrBfnam,
                                       ((unsigned long) ptrEpkt),
                                       ulBytcntNOcrc);

                                return;
                              }
                          }
                      }
                      else
                      {
                        // Final of many IP fragments
                      }

                    return;
                  }
                  else
                  {
                    // One of many IP fragments

                    return;
                  }
              }

            return;
          }
      }

    return;
  }
#endif // SANCHKEPKT

#if OETH_DEBUG
static void
oeth_print_packet(unsigned long add, int len)
{
    int i;

    _print("ipacket: add = %x len = %d\n", add, len);
    for(i = 0; i < len; i++) {
        if(!(i % 16))
                _print("\n");
        _print(" %.2x", *(((unsigned char *)add) + i));
    }
    _print("\n");
}
#endif

// Read a phy register
#if defined(TDK78Q2120PHY)
int eth_mdread(struct net_device *dev,
               int                fiad_phy_addr,
               int                phyreg)
  {
    int                  rdata;
    volatile oeth_regs  *regs = (oeth_regs *)dev->base_addr;

    // ensure busy not set
    do
      {
        rdata = regs->miistatus;

      } while (rdata & OETH_MIISTATUS_BUSY);

    regs->miiaddress = ((unsigned long) (((phyreg << 8) |
                                          fiad_phy_addr)));
    regs->miicommand = ((unsigned long) (OETH_MIICOMMAND_RSTAT));

    // wait while busy set
    do
      {
        rdata = regs->miistatus;

      } while (rdata & OETH_MIISTATUS_BUSY);

    rdata = regs->miirx_data;

    return rdata;
  }
#else
  ...?...
#endif

// Write a phy register
#if defined(TDK78Q2120PHY)
void eth_mdwrite(struct net_device *dev,
                 int                fiad_phy_addr,
                 int                phyreg,
                 int                wdata)
  {
    int                  rdata;
    volatile oeth_regs  *regs = (oeth_regs *)dev->base_addr;

    // ensure busy not set
    do
      {
        rdata = regs->miistatus;

      } while (rdata & OETH_MIISTATUS_BUSY);

    regs->miiaddress = ((unsigned long) (((phyreg << 8) |
                                         fiad_phy_addr)));
    regs->miitx_data = ((unsigned long) (wdata));
    regs->miicommand = ((unsigned long) (OETH_MIICOMMAND_WCTRLDATA));

    // wait while busy set
    do
      {
        rdata = regs->miistatus;

      } while (rdata & OETH_MIISTATUS_BUSY);

    return;
  }
#else
  ...?...
#endif

void oeth_phymac_synch (struct net_device *dev, int callerflg)
  {
    volatile oeth_regs  *regs = (oeth_regs *)dev->base_addr;
    unsigned long        ulmoderval;
    unsigned long        ulmr1sts;
    unsigned long        ulphydiagval;

    ulmr1sts = eth_mdread(dev, PHY_ADDRESS, 1);
    /* Read twice to get CURRENT status                                 */
    ulmr1sts = eth_mdread(dev, PHY_ADDRESS, 1);

    ulmoderval = regs->moder;

    #if defined(TDK78Q2120PHY)
      ulphydiagval = eth_mdread(dev, PHY_ADDRESS, 18);
    #else
      ...?...
    #endif

    if(callerflg == 0)
      {
        // Caller = NOT Phy interrupt handler

        if((ulmr1sts & 0x00000004) != 0)
          {
            // Link is ostensibly OK

            if((eth_mdread(dev, PHY_ADDRESS, 0) & 0x00001000) != 0)
              {
                // Auto negotiation ostensibly enabled

                if((ulmr1sts & 0x00000020) != 0)
                  {
                    // Auto negotiation ostensibly has completed

                    #if defined(TDK78Q2120PHY)

                      if((ulphydiagval & 0x1000) != 0)
                        {
                          // Auto negotiation ostensibly has failed

                          if((ulphydiagval & (0x0800 | 0x0400 )) != 0)
                            {
                              // Auto negotiation failure expected to
                              //  have fallen back to 10 mbit half
                              //  duplex - perhaps phy registers aren't
                              //  actually available, and we've been
                              //  reading 0xFFFF's ?

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

                              printk("\noeth_phymac_synch:%s"
                                       " No phyregs?-assuming HalfD\n",
                                     dev->name);

                              regs->moder =
                                  ((unsigned long)
                                        (ulmoderval &
                                         (~(OETH_MODER_FULLD))));
                              // FIXME:...
                              // ...Note manual says not supposed
                              // ... to "change registers after ModeR's
                              // ... TxEn or RxEn bit(s) have been set"
                              regs->ipgt = ((unsigned long) (0x00000012));

                              return;
                            }
                        }
                    #else
                      ...?...
                    #endif
                  }
              }
          }
      }
//    else
//    {
//      // Caller = Phy interrupt handler
//      // If we've got a phy interrupt, then we're so likely
//      //  to also have actual phy registers that we won't
//      //  bother trying to confirm.
//    }

    #if defined(ANNOUNCEPHYINT)
      printk("\noeth_phymac_synch:%s  MR1: 0x%08lX\n",
             dev->name,
             ulmr1sts);
      if((ulmr1sts & 0x00000002) != 0)
        {
          printk("                               Jabber\n");
        }
      if((ulmr1sts & 0x00000010) != 0)
        {
          printk("                               Remote Fault\n");
        }
      if((ulmr1sts & 0x00000020) != 0)
        {
          printk("                               Autoneg'd\n");
        }
    #endif

    if((ulmr1sts & 0x00000004) != 0)
      {
        /* Phy MR1 (status register) indicates link is (now) OK.        */

        /* (dgt:07FEB2003) miistatus:                                   */
        /*  ...will NOT show current OETH_MIISTATUS_LINKFAIL            */
        /*  ... status, no matter how many times it's read...           */
        /*  ...one must first read phy status register (MR1),           */
        /*  ... then read miistatus...                                  */
        /* ... so, we just use phy status directly.                     */

        #if defined(ANNOUNCEPHYINT)
          printk("             Link OK: MODER: 0x%08lX\n",
                 ulmoderval);
        #endif

        /* Recommended ipgt register (0x000c) value:
         * Back to Back Inter Packet Gap
         *  Full Duplex: 0x15: 0.96 uSecs IPG for 100 mbps
         *                     9.60 uSecs IPG for 10  mbps
         *      Desired period in nibble times minus 6
         *       96 bits = 24 nibbles - 6 = 18 = 0x12 (but
         *          reference guide says 0x15 (what's backwards,
         *          6, or 0x15 ?))
         *  Half Duplex: 0x12: 0.96 uSecs IPG for 100 mbps
         *                     9.60 uSecs IPG for 10  mbps
         *  Desired period in nibble times minus 3
         *       96 bits = 24 nibbles - 3 = 21 = 0x15 (but
         *          reference guide says 0x12 (what's backwards,
         *          3, or 0x12 ?))
         */

        #if defined(TDK78Q2120PHY)

          if((ulphydiagval & 0x0800) != 0)
            {
              /* Phy MR18 (diagnostics register) indicates              */
              /*  link is (now) running full duplex.                    */

              if((ulmoderval & (OETH_MODER_FULLD)) == 0)
                {
                  regs->moder = ((unsigned long) (ulmoderval |
                                                  (OETH_MODER_FULLD)));
                }
              // FIXME:...
              // ...Note manual says not supposed to "change
              // ... registers after ModeR's TxEn or RxEn
              // ...  bit(s) have been set"
              if(regs->ipgt != ((unsigned long) (0x00000015)))
                {
                  regs->ipgt = ((unsigned long) (0x00000015));
                }

              #if defined(ANNOUNCEPHYINT)
                printk("             FullD:    MR18: 0x%08lX\n",
                       ulphydiagval);
              #endif
            }
            else
            {
              /* Phy MR18 (diagnostics register) indicates              */
              /*  link is (now) running half duplex.                    */

              if((ulmoderval & (OETH_MODER_FULLD)) != 0)
                {
                  regs->moder = ((unsigned long) (ulmoderval &
                                                  (~(OETH_MODER_FULLD))));
                }
              // FIXME:...
              // ...Note manual says not supposed to "change
              // ... registers after ModeR's TxEn or RxEn
              // ...  bit(s) have been set"
              if(regs->ipgt != ((unsigned long) (0x00000012)))
                {
                  regs->ipgt = ((unsigned long) (0x00000012));
                }

              #if defined(ANNOUNCEPHYINT)
                printk("             HalfD:    MR18: 0x%08lX\n",
                       ulphydiagval);
              #endif
            }

          #if defined(ANNOUNCEPHYINT)
            printk("             %s\n",
                   (ulphydiagval & 0x0400) ? "100BASE-TX" : "10BASE-T");
          #endif
        #else
          ...?...
        #endif
      }
    #if defined(ANNOUNCEPHYINT)
      else
      {
        printk("                               Link Down\n");
      }
    #endif

    #if defined(ANNOUNCEPHYINT)
      printk("\n");
    #endif

    return;
  }

#if defined(PHYIRQ_NUM)
/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t oeth_PhyInterrupt(int             irq,
                                     void           *dev_id,
                                     struct pt_regs *regs)
  {
    #if defined(TDK78Q2120PHY)

      unsigned long                  ulmr17sts;
      struct   net_device           *dev        = dev_id;
      volatile struct oeth_private  *cep;

      cep = (struct oeth_private *)dev->priv;

      ulmr17sts = eth_mdread(((struct net_device *) dev_id),
                             PHY_ADDRESS, 17);
        // Read Clears (no ack req'd)

      if((ulmr17sts & 0x00000040) != 0)
        {
          cep->stats.rx_frame_errors++;
        }

      #if defined(ANNOUNCEPHYINT)
        printk("\noeth_PhyInterrupt:%s  MR17: 0x%08lX\n",
               dev->name,
               ulmr17sts);
        if((ulmr17sts & 0x00000080) != 0)
          {
            printk("                               Jabber\n");
          }
        if((ulmr17sts & 0x00000040) != 0)
          {
            printk("                               Rxer\n");
          }
        if((ulmr17sts & 0x00000020) != 0)
          {
            printk("                               Pagerec\n");
          }
        if((ulmr17sts & 0x00000010) != 0)
          {
            printk("                               Pfd\n");
          }
        if((ulmr17sts & 0x00000008) != 0)
          {
            printk("                               Lpack\n");
          }
        if((ulmr17sts & 0x00000004) != 0)
          {
            printk("                               Lschg\n");
          }
        if((ulmr17sts & 0x00000002) != 0)
          {
            printk("                               Rfault\n");
          }
        if((ulmr17sts & 0x00000001) != 0)
          {
            printk("                               Anegcomp\n");
          }
      #endif

      oeth_phymac_synch((struct net_device *) dev_id,
                        1);  // Caller = Phy interrupt handler
    #else
      ...?...
    #endif

    return IRQ_HANDLED;
  }
#endif  // PHYIRQ_NUM


/*
    Entered at interrupt level
*/
static void
oeth_tx(struct net_device *dev)
{
    volatile struct oeth_private *cep;
    volatile oeth_bd *bdp;

#ifndef TXBUFF_PREALLOC
    struct  sk_buff *skb;
#endif

    cep = (struct oeth_private *)dev->priv;

    for (;; cep->tx_last = (cep->tx_last + 1) & OETH_TXBD_NUM_MASK)
      {
        bdp = cep->tx_bd_base + cep->tx_last;

        if ((bdp->len_status & OETH_TX_BD_READY) ||
            ((cep->tx_last == cep->tx_next) && !cep->tx_full))
            break;

        /* Check status for errors
         */
        if (bdp->len_status & OETH_TX_BD_LATECOL)
            cep->stats.tx_window_errors++;
            //;dgt - ifconfig doesn't report tx_window_errors ?
                //;dgt - but we (also) include same in tx_errors.

        if (bdp->len_status & OETH_TX_BD_RETLIM)
            cep->stats.tx_aborted_errors++;
            //;dgt - ifconfig doesn't report tx_aborted_errors ?
                //;dgt - but we (also) include same in tx_errors.

        if (bdp->len_status & OETH_TX_BD_UNDERRUN)
            cep->stats.tx_fifo_errors++;

        if (bdp->len_status & OETH_TX_BD_CARRIER)
            cep->stats.tx_carrier_errors++;

        //;dgt - OETH_TX_BD_DEFER neither counted nor ifconfig reported

        if (bdp->len_status & (OETH_TX_BD_LATECOL   |
                               OETH_TX_BD_RETLIM    |
                               OETH_TX_BD_UNDERRUN))
            cep->stats.tx_errors++;

        cep->stats.tx_packets++;
        cep->stats.collisions += (bdp->len_status >> 4) & 0x000f;

#ifndef TXBUFF_PREALLOC
        skb = cep->tx_skbuff[cep->tx_last];

        /* Free the sk buffer associated with this last transmit.
        */
        dev_kfree_skb(skb);
#endif

        if (cep->tx_full)
            cep->tx_full = 0;
      }
}

/*
    Entered at interrupt level
*/
static void
oeth_rx(struct net_device *dev)
{
    volatile struct oeth_private *cep;
    volatile        oeth_bd      *bdp;
    struct          sk_buff      *skb;
    int                           pkt_len;
    int                           bad;                          //;dgt
    int                           netif_rx_rtnsts;              //;dgt
  #ifndef RXBUFF_PREALLOC
    struct          sk_buff      *small_skb;
  #endif

    cep = (struct oeth_private *)dev->priv;

    /* First, grab all of the stats for the incoming packet.
     * These get messed up if we get called due to a busy condition.
     */
    for (;;cep->rx_cur = (cep->rx_cur + 1) & OETH_RXBD_NUM_MASK)
      {
        bad = 0;                                                //;dgt
        bdp = cep->rx_bd_base + cep->rx_cur;

  #ifndef RXBUFF_PREALLOC
        skb = cep->rx_skbuff[cep->rx_cur];

        if (skb == NULL)
          {
            skb = dev_alloc_skb(MAX_FRAME_SIZE);

            if (skb != NULL)
            {
                bdp->addr = (unsigned long) skb->tail;

                dcache_push (((unsigned long) (bdp->addr)),
                             MAX_FRAME_SIZE);

                bdp->len_status |= OETH_RX_BD_EMPTY;
            }

            continue;
          }
  #endif

        if (bdp->len_status & OETH_RX_BD_EMPTY)
            break;

        /* Check status for errors.
         */
        if (bdp->len_status & (OETH_RX_BD_TOOLONG | OETH_RX_BD_SHORT)) {
            cep->stats.rx_length_errors++;
            //;dgt - ifconfig doesn't report rx_length_errors ?
            //;dgt - but we (also) include same in rx_errors.
            bad = 1;
        }
        if (bdp->len_status & OETH_RX_BD_DRIBBLE) {
            cep->stats.rx_frame_errors++;
            bad = 1;
        }
        if (bdp->len_status & OETH_RX_BD_CRCERR) {
            cep->stats.rx_crc_errors++;
            //;dgt - ifconfig doesn't report rx_crc_errors ?
            //;dgt - but we (also) include same in rx_errors.
            bad = 1;
        }
        if (bdp->len_status & OETH_RX_BD_OVERRUN) {
            cep->stats.rx_crc_errors++;
            //;dgt - ifconfig doesn't report rx_crc_errors ?
            //;dgt - but we (also) include same in rx_errors.
            bad = 1;
        }
        if (bdp->len_status & OETH_RX_BD_MISS)
          {
            //;dgt - identifies a packet received in promiscuous
            //;dgt    mode (would not have otherwise been accepted)
          }
        if (bdp->len_status & OETH_RX_BD_LATECOL) {
            cep->stats.rx_frame_errors++;
            bad = 1;
        }
        if (bdp->len_status & OETH_RX_BD_INVSIMB) {             //;dgt
            cep->stats.rx_frame_errors++;                       //;dgt
            bad = 1;                                            //;dgt
        }                                                       //;dgt
        if (bdp->len_status & (OETH_RX_BD_TOOLONG   |           //;dgt
                               OETH_RX_BD_SHORT     |           //;dgt
                               OETH_RX_BD_CRCERR    |           //;dgt
                               OETH_RX_BD_OVERRUN))             //;dgt
            cep->stats.rx_errors++;                             //;dgt

        if (bad)
          {
            bdp->len_status &= ~OETH_RX_BD_STATS;

            dcache_push (((unsigned long) (bdp->addr)),
                         OETH_RX_BUFF_SIZE);

            bdp->len_status |= OETH_RX_BD_EMPTY;

            continue;
          }

        /* Process the incoming frame.
         */
        pkt_len = bdp->len_status >> 16;

#ifdef RXBUFF_PREALLOC
    #ifdef CONFIG_EXCALIBUR
//;dgt  skb = dev_alloc_skb(pkt_len);
        skb = dev_alloc_skb(pkt_len + 2 + 3 + 4);   //;dgt
          //;dgt Over allocate 2 extra bytes to
          //;dgt  32 bit align Nios 32 bit
          //;dgt  IP/TCP fields.
          //;dgt Over allocate 3 extra bytes to
          //;dgt  allow packet to be treated as
          //;dgt  as an even number of bytes or
          //;dgt  16 bit words if so desired.
          //;dgt Plus another extra 4 paranoia bytes.
    #else
        skb = dev_alloc_skb(pkt_len);
    #endif

        if (skb == NULL)
          {
            printk("%s: Memory squeeze, dropping packet.\n", dev->name);
            cep->stats.rx_dropped++;
          }
        else
          {
            skb->dev = dev;
#if OETH_DEBUG
            _print("RX\n");
            oeth_print_packet(bdp->addr, pkt_len);
#endif
        #ifdef CONFIG_EXCALIBUR
          {
            unsigned short     *dst;
            unsigned short     *src;

            #ifdef SANCHKEPKT
              unsigned short   *savddst;
            #endif // SANCHKEPKT

            skb_reserve( skb, 2 );
              //;dgt 32 bit align Nios 32 bit IP/TCP fields

            dst = ((unsigned short *) (skb_put(skb, pkt_len)));
            src = ((unsigned short *) (__va(bdp->addr)));

            #ifdef SANCHKEPKT
              savddst = dst;
              DoSanchkEpkt(((const char *) src),
                           pkt_len,
                           "DmaRx");
            #endif // SANCHKEPKT

            //;dgt...FIXME...simple 16 bit copy loop
            //;dgt... = approx 20% overall throughput
            //;dgt... improvement over memcpy...
            //;dgt...A more advanced copy from 32 bit
            //;dgt... aligned source to 16 bit aligned
            //;dgt... destination still needed though...
            //;dgt...Note destination bears enough extra
            //;dgt... room to hold ((pkt_len + 3) >> 2)
            //;dgt... longwords...
            //;dgt;Mar2005;Custom NiosII instruction now
            //;dgt; available or imminent for a "really"
            //;dgt; optimized memcpy, even under mismatched
            //;dgt; src/dst alignments
            #if 0
              {
                unsigned int        uiloop;
                for(uiloop = 0;
                    (uiloop < ((pkt_len + 1) >> 1));
                    uiloop++)
                  {
                    *dst++ = *src++;
                  }
              }
            #else
              memcpy(dst, src, pkt_len);
            #endif

            #ifdef SANCHKEPKT
              DoSanchkEpkt(((const char *) savddst),
                           pkt_len,
                           "SkbRx");
            #endif // SANCHKEPKT
          }
        #else
            memcpy(skb_put(skb, pkt_len),
                   (unsigned char *)__va(bdp->addr),
                   pkt_len);
        #endif

            skb->protocol = eth_type_trans(skb,dev);

            netif_rx_rtnsts = netif_rx(skb);                    //;dgt

              switch (netif_rx_rtnsts)                          //;dgt
                {                                               //;dgt
                  case NET_RX_DROP:                     // 0x01 //;dgt
                    cep->stats.rx_dropped++;                    //;dgt
                    // memo: netif_rx has: kfree_skb(skb);      //;dgt
                    #if 0                                       //;dgt
                      printk("%s: netif_rx dropped"             //;dgt
                               " %d byte packet.\n",            //;dgt
                             dev->name,                         //;dgt
                             pkt_len);                          //;dgt
                    #endif                                      //;dgt
                    break;                                      //;dgt
                  #if 0                                         //;dgt
                    case NET_RX_SUCCESS:                // 0x00 //;dgt
                      break;                                    //;dgt
                    default:                                    //;dgt
//                  case NET_RX_CN_LOW:                 // 0x02 //;dgt
//                  case NET_RX_CN_MOD:                 // 0x03 //;dgt
//                  case NET_RX_CN_HIGH:                // 0x04 //;dgt
//                  case NET_RX_BAD:                    // 0x05 //;dgt
                      printk("%s: netif_rx rtnsts: %08lX\n",    //;dgt
                             dev->name,                         //;dgt
                             netif_rx_rtnsts);                  //;dgt
                      break;                                    //;dgt
                  #endif                                        //;dgt
                }                                               //;dgt

            cep->stats.rx_packets++;
          }

        dcache_push (((unsigned long) (bdp->addr)),
                     pkt_len);

        bdp->len_status &= ~OETH_RX_BD_STATS;
        bdp->len_status |= OETH_RX_BD_EMPTY;
#else
        if (pkt_len < 128)
          {
            small_skb = dev_alloc_skb(pkt_len);

            if (small_skb)
              {
                small_skb->dev = dev;
  #if OETH_DEBUG
                _print("RX short\n");
                oeth_print_packet(bdp->addr, bdp->len_status >> 16);
  #endif
                memcpy(skb_put(small_skb, pkt_len),
                       (unsigned char *)__va(bdp->addr),
                       pkt_len);
                small_skb->protocol = eth_type_trans(small_skb,dev);
                netif_rx(small_skb);
                cep->stats.rx_packets++;
              }
            else
              {
                printk("%s: Memory squeeze, dropping packet.\n", dev->name);
                cep->stats.rx_dropped++;
              }

            dcache_push (((unsigned long) (bdp->addr)),
                         pkt_len);

            bdp->len_status &= ~OETH_RX_BD_STATS;
            bdp->len_status |=  OETH_RX_BD_EMPTY;
          }
        else
          {
            skb->dev = dev;
            skb_put(skb, bdp->len_status >> 16);
            skb->protocol = eth_type_trans(skb,dev);
            netif_rx(skb);
            cep->stats.rx_packets++;
  #if OETH_DEBUG
            _print("RX long\n");
            oeth_print_packet(bdp->addr, bdp->len_status >> 16);
  #endif
            skb = dev_alloc_skb(MAX_FRAME_SIZE);

            bdp->len_status &= ~OETH_RX_BD_STATS;

            if (skb)
              {
                cep->rx_skbuff[cep->rx_cur] = skb;

                bdp->addr = (unsigned long)skb->tail;

                dcache_push (((unsigned long) (bdp->addr)),
                             MAX_FRAME_SIZE);

                bdp->len_status |= OETH_RX_BD_EMPTY;
              }
            else
              {
                cep->rx_skbuff[cep->rx_cur] = NULL;
              }
          }
#endif
      }
}


/*-----------------------------------------------------------
 | Driver entry point
 |
 | Entry condition: Cpu interrupts DISabled.
 */
static irqreturn_t oeth_interrupt(int             irq,
                                  void           *dev_id,
                                  struct pt_regs *regs)
{
    struct  net_device *dev = dev_id;
    volatile struct oeth_private *cep;
    uint    int_events;

    cep = (struct oeth_private *)dev->priv;

    /* Get the interrupt events that caused us to be here.
     */
    int_events = cep->regs->int_src;
    cep->regs->int_src = int_events;

    /* Handle receive event in its own function.
     */
    if (int_events & (OETH_INT_RXF | OETH_INT_RXE))
        oeth_rx(dev_id);

    /* Handle transmit event in its own function.
     */
    if (int_events & (OETH_INT_TXB | OETH_INT_TXE)) {
        oeth_tx(dev_id);

        if(((cep->tx_next + 1) & OETH_TXBD_NUM_MASK) != cep->tx_last)
          {
            netif_wake_queue(dev);
          }
//        else
//        {
//          Tx done interrupt but no tx BD's released ?
//        }
    }

    /* Check for receive busy, i.e. packets coming but no place to
     * put them.
     */
    if (int_events & OETH_INT_BUSY)
      {
        cep->stats.rx_dropped++;                                //;dgt

        if (!(int_events & (OETH_INT_RXF | OETH_INT_RXE)))
          {
            oeth_rx(dev_id);
          }
      }

    return IRQ_HANDLED;
}


static int
oeth_open(struct net_device *dev)
{
    volatile oeth_regs *regs = (oeth_regs *)dev->base_addr;

#ifndef RXBUFF_PREALLOC
    volatile struct oeth_private *cep = (struct oeth_private *)dev->priv;
    struct  sk_buff *skb;
    volatile oeth_bd *rx_bd;
    int i;

    rx_bd = cep->rx_bd_base;

    for(i = 0; i < OETH_RXBD_NUM; i++)
      {
        skb = dev_alloc_skb(MAX_FRAME_SIZE);

        if (skb == NULL)
            rx_bd[i].len_status = (0 << 16) | OETH_RX_BD_IRQ;
        else
            dcache_push (((unsigned long) (rx_bd[i].addr)),
                         MAX_FRAME_SIZE);

            rx_bd[i].len_status = (0 << 16)         |
                                  OETH_RX_BD_EMPTY  |
                                  OETH_RX_BD_IRQ;
            // FIXME...Should we really let the rx ring
            //      ... completely fill...?...
            //      ...Can we actually prevent...?...

        cep->rx_skbuff[i] = skb;
        rx_bd[i].addr     = (unsigned long)skb->tail;
      }
    rx_bd[OETH_RXBD_NUM - 1].len_status |= OETH_RX_BD_WRAP;
#endif

    /* Install our interrupt handler.
     */
    request_irq(MACIRQ_NUM, oeth_interrupt, 0, "eth", (void *)dev);

    // enable phy interrupts
    //
    #if defined(TDK78Q2120PHY)
      #if defined(PHYIRQ_NUM)
        request_irq(PHYIRQ_NUM,
                    oeth_PhyInterrupt,
                    0, "eth", (void *)dev);

        eth_mdread (dev, PHY_ADDRESS, 17);      // Clear any junk ?

        eth_mdwrite(dev, PHY_ADDRESS, 17, 0xff00);
          // Enable Jabber     0x8000(0x0080)
          //        Rxer       0x4000(0x0040)
          //        Prx        0x2000(0x0020)
          //        Pfd        0x1000(0x0010)
          //        Lpack      0x0800(0x0008)
          //        Lschg      0x0400(0x0004)
          //        Rfault     0x0200(0x0002)
          //        Anegcomp   0x0100(0x0001)
          //  Ints

        (*(volatile unsigned long *)
             (((unsigned long *)
                  ((((char *)
                        (((int) (PHYIRQ_BASE_ADDR)) +
                         0x0008))))))) =
                ((unsigned long) (0x0001));
          // Enable phy interrupt pass thru to PHYIRQ_NUM
      #endif
    #else
      ...?...
    #endif

    oeth_phymac_synch(dev,
                      0);  // Caller = NOT Phy interrupt handler

    /* Enable receiver and transmiter
     */
    regs->moder |= OETH_MODER_RXEN | OETH_MODER_TXEN;

    netif_start_queue(dev);

    return 0;
}

static int
oeth_close(struct net_device *dev)
{
    volatile struct oeth_private *cep = (struct oeth_private *)dev->priv;
    volatile oeth_regs *regs = (oeth_regs *)dev->base_addr;
    volatile oeth_bd *bdp;
    int i;

    netif_stop_queue(dev);

    /* Free phy interrupt handler
     */
    #if defined(TDK78Q2120PHY)
      #if defined(PHYIRQ_NUM)
        (*(volatile unsigned long *)
             (((unsigned long *)
                  ((((char *)
                        (((int) (PHYIRQ_BASE_ADDR)) +
                         0x0008))))))) =
                ((unsigned long) (0x0000));
          // Disable phy interrupt pass thru to PHYIRQ_NUM

        free_irq(PHYIRQ_NUM, (void *)dev);
      #endif
    #else
      ...?...
    #endif

    /* Free interrupt hadler
     */
    free_irq(MACIRQ_NUM, (void *)dev);

    /* Disable receiver and transmitesr
     */
    regs->moder &= ~(OETH_MODER_RXEN | OETH_MODER_TXEN);

    bdp = cep->rx_bd_base;
    for (i = 0; i < OETH_RXBD_NUM; i++) {
        bdp->len_status &= ~(OETH_TX_BD_STATS | OETH_TX_BD_READY);
        bdp++;
    }

    bdp = cep->tx_bd_base;
    for (i = 0; i < OETH_TXBD_NUM; i++) {

        bdp->len_status &= ~(OETH_RX_BD_STATS | OETH_RX_BD_EMPTY);

        bdp++;
    }

#ifndef RXBUFF_PREALLOC

    /* Free all alocated rx buffers
     */
    for (i = 0; i < OETH_RXBD_NUM; i++) {

        if (cep->rx_skbuff[i] != NULL)
            dev_kfree_skb(cep->rx_skbuff[i]);

    }
#endif  // RXBUFF_PREALLOC

#ifndef TXBUFF_PREALLOC

    /* Free all alocated tx buffers
     */
    for (i = 0; i < OETH_TXBD_NUM; i++) {

        if (cep->tx_skbuff[i] != NULL)
            dev_kfree_skb(cep->tx_skbuff[i]);
    }
#endif  // TXBUFF_PREALLOC

    return 0;
}

static int
oeth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
    volatile struct oeth_private *cep = (struct oeth_private *)dev->priv;
    volatile        oeth_bd      *bdp;
    unsigned        long          flags;
    unsigned        int           lenSkbDataByts;

    netif_stop_queue(dev);

    if (cep->tx_full) {
        //;dgt-"Impossible", but in any event, queue may have been
        //;dgt- reawakened by now.
        /* All transmit buffers are full.  Bail out.
         */
        printk("%s: tx queue full!.\n", dev->name);
        return 1;
    }

    lenSkbDataByts = skb->len;

    /* Fill in a Tx ring entry
     */
    bdp = cep->tx_bd_base + cep->tx_next;

    /* Clear all of the status flags.
     */
    bdp->len_status &= ~OETH_TX_BD_STATS;

    /* If the frame is short, tell CPM to pad it.
     */
    if (lenSkbDataByts <= ETH_ZLEN)
        bdp->len_status |= OETH_TX_BD_PAD;
    else
        bdp->len_status &= ~OETH_TX_BD_PAD;

#if OETH_DEBUG
    _print("TX\n");
    oeth_print_packet(skb->data, lenSkbDataByts);
#endif  // OETH_DEBUG

#ifdef TXBUFF_PREALLOC

    /* Copy data in preallocated buffer */
    if (lenSkbDataByts > OETH_TX_BUFF_SIZE)
      {
        printk("%s: %d byte tx frame too long (max:%d)!.\n",
               dev->name,
               lenSkbDataByts,
               OETH_TX_BUFF_SIZE);

//;dgt  return 1;                               //;dgt infinite loop!
        dev_kfree_skb(skb);                     //;dgt
        netif_wake_queue(dev);
        return 0;                               //;dgt
      }
      else
      {
        #ifdef CONFIG_EXCALIBUR
          #ifdef SANCHKEPKT
            DoSanchkEpkt(((const char *) (skb->data)),
                         lenSkbDataByts,
                         "SkbTx");
          #endif // SANCHKEPKT

          if((((unsigned long) (skb->data)) & 1) == 0)
            {
              unsigned short     *dst;
              unsigned short     *src;

              dst = ((unsigned short *) (bdp->addr));
              src = ((unsigned short *) (skb->data));

              //;dgt...FIXME...simple 16 bit copy loop
              //;dgt... = approx 20% overall throughput
              //;dgt... improvement...
              //;dgt...A more advanced copy from 16 bit
              //;dgt... aligned source to 32 bit aligned
              //;dgt... destination still needed though...
              //;dgt;Mar2005;Custom NiosII instruction now
              //;dgt; available or imminent for a "really"
              //;dgt; optimized memcpy, even under mismatched
              //;dgt; src/dst alignments
              #if 0
                {
                  unsigned int        uiloop;
                  for(uiloop = 0;
                      (uiloop < (lenSkbDataByts >> 1));
                      uiloop++)
                    {
                      *dst++ = *src++;
                    }

                  if(((lenSkbDataByts) & 1) != 0)
                    {
                      *((unsigned char *) dst) =
                           *((unsigned char *) src);
                    }
                }
              #else
                memcpy(dst, src, lenSkbDataByts);
              #endif
            }
            else
            {
              memcpy((unsigned char *)bdp->addr,
                     skb->data,
                     lenSkbDataByts);
            }

          #ifdef SANCHKEPKT
            DoSanchkEpkt(((const char *) (bdp->addr)),
                         lenSkbDataByts,
                         "DmaTx");
          #endif // SANCHKEPKT

        #else
          memcpy((unsigned char *)bdp->addr, skb->data, lenSkbDataByts);
        #endif  // CONFIG_EXCALIBUR
      }

    bdp->len_status =   (bdp->len_status & 0x0000ffff)
                      | (lenSkbDataByts << 16);

    dev_kfree_skb(skb);
#else
    /* Set buffer length and buffer pointer.
     */
    bdp->len_status =   (bdp->len_status & 0x0000ffff)
                      | (lenSkbDataByts << 16);
    bdp->addr = (uint)__pa(skb->data);

    /* Save skb pointer.
     */
    cep->tx_skbuff[cep->tx_next] = skb;
#endif  // TXBUFF_PREALLOC

//;dgt  cep->tx_next = (cep->tx_next + 1) & OETH_TXBD_NUM_MASK;

    local_irq_save(flags);

    cep->tx_next = (cep->tx_next + 1) & OETH_TXBD_NUM_MASK;     //;dgt

    if (cep->tx_next == cep->tx_last)
      {
        cep->tx_full = 1;
      }
      else
      {
        if(((cep->tx_next + 1) & OETH_TXBD_NUM_MASK) != cep->tx_last)
          {
            netif_wake_queue(dev);
          }
//        else
//        {
//          Don't let the tx ring completely fill
//        }
      }

    /* Send it on its way.  Tell controller its ready, interrupt when done,
     * and to put the CRC on the end.
     */
    dcache_push (((unsigned long) (bdp->addr)),
                 lenSkbDataByts);

    bdp->len_status |= (  0
                        | OETH_TX_BD_READY
                        | OETH_TX_BD_IRQ
                        | OETH_TX_BD_CRC
                       );

    dev->trans_start = jiffies;

    local_irq_restore(flags);

    return 0;
}


#if 1                                                           //;dgt
    //                                                          //;dgt
#else                                                           //;dgt
static int calc_crc(char *mac_addr)
{
    // FIXME....                                                //;dgt
    // ...must calculate the PRE postconditioned AutoDinII      //;dgt
    // ... Crc-32 of the 6 bytes pointed to by mac_addr,        //;dgt
    // ... then extract and reverse bit #'s 3-8 inclusive       //;dgt
    // ... (with due regard to the varying frames of reference  //;dgt
    // ... within various crc publications, etc)...             //;dgt
    // Eg: 01-80-C2-00-00-01 (IEEE 802 Mac control multicast)   //;dgt
    //   CRC-32:     0x9FC42B70 before Postconditioning         //;dgt
    //    Last_on_wire_9F    70_First (If were to go onto wire) //;dgt
    //                       Bits #'s 0-16: 0x2B70              //;dgt
    //                                 .......98 7654 3210      //;dgt
    //                                 0010 1011 0111 0000      //;dgt
    //                       Bits #'s 3-8:  1 0111 0            //;dgt
    //                        Reversed:     0 1110 1 = 0x1D     //;dgt
    //   Return multicast hash bit offset: 29 decimal           //;dgt
    // Eg: 01-00-5E-00-00-09 (IETF Rip2 multicast)              //;dgt
    //   CRC-32      0xD76F4DCC  before Postconditioning        //;dgt
    //   Return multicast hash bit offset: 39 decimal           //;dgt
    // At least it so appears.                                  //;dgt

    int result = 0;
    return (result & 0x3f);
}
#endif                                                      //;dgt

static struct net_device_stats *oeth_get_stats(struct net_device *dev)
{
    volatile struct oeth_private *cep = (struct oeth_private *)dev->priv;

    return &cep->stats;
}

static void oeth_set_multicast_list(struct net_device *dev)
{
    volatile struct oeth_private *cep;
    volatile oeth_regs *regs;

    cep = (struct oeth_private *)dev->priv;

    /* Get pointer of controller registers.
     */
    regs = (oeth_regs *)dev->base_addr;

    if (dev->flags & IFF_PROMISC) {

        /* Log any net taps.
         */
        printk("%s: Promiscuous mode enabled.\n", dev->name);
        regs->moder |= OETH_MODER_PRO;
    } else {

        regs->moder &= ~OETH_MODER_PRO;

        if (dev->flags & IFF_ALLMULTI) {

            /* Catch all multicast addresses, so set the
             * filter to all 1's.
             */
            regs->hash_addr0 = 0xffffffff;
            regs->hash_addr1 = 0xffffffff;
        }
        else if (dev->mc_count) {

          #if 1                                         //;dgt
            // FIXME...for now, until broken            //;dgt
            //  calc_crc(...) fixed...                  //;dgt
            regs->hash_addr0 = 0xffffffff;              //;dgt
            regs->hash_addr1 = 0xffffffff;              //;dgt
          #else                                         //;dgt
            struct  dev_mc_list *dmi;
            int                  i;

            /* Clear filter and add the addresses in the list.
             */
            regs->hash_addr0 = 0x00000000;
//;dgt      regs->hash_addr0 = 0x00000000;
            regs->hash_addr1 = 0x00000000;              //;dgt

            dmi = dev->mc_list;

            for (i = 0; i < dev->mc_count; i++) {

                int hash_b;

                /* Only support group multicast for now.
                 */
                if (!(dmi->dmi_addr[0] & 1))
                    continue;

                hash_b = calc_crc(dmi->dmi_addr);
                if(hash_b >= 32)
                    regs->hash_addr1 |= 1 << (hash_b - 32);
                else
                    regs->hash_addr0 |= 1 << hash_b;
            }
          #endif                                        //;dgt
        }
    }
}

static void oeth_set_mac_add(struct net_device *dev, void *p)
{
    struct sockaddr *addr=p;
    volatile oeth_regs *regs;

    memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);

    regs = (oeth_regs *)dev->base_addr;

    regs->mac_addr1 = (dev->dev_addr[0]) <<  8 |
                      (dev->dev_addr[1]);
    regs->mac_addr0 = (dev->dev_addr[2]) << 24 |
                      (dev->dev_addr[3]) << 16 |
                      (dev->dev_addr[4]) <<  8 |
                      (dev->dev_addr[5]);
}


/*-----------------------------------------------------------
 | Entry condition: Cpu interrupts ENabled
 |                   (in SPITE of claims disabled).
*/
static int __init oeth_probe(struct net_device *dev)
{
    volatile struct oeth_private  *cep;
    volatile        oeth_regs     *regs;
    volatile        oeth_bd       *tx_bd, *rx_bd;
    int                            i, j, k;
  #ifdef SRAM_BUFF
    unsigned long mem_addr = SRAM_BUFF_BASE;
  #else
    unsigned long mem_addr;
  #endif    // SRAM_BUFF

    PRINTK2("%s:oeth_probe\n", dev->name);

    SET_MODULE_OWNER (dev);

    cep = (struct oeth_private *)dev->priv;

    if (!request_region(OETH_REG_BASE,
                        OETH_IO_EXTENT,
                        dev->name)) return -EBUSY;

    printk("%s: Open Ethernet Core Version 1.0\n", dev->name);

  #ifdef CONFIG_EXCALIBUR
    printk("  oeth_probe: %d Khz Nios: %d RX, %d TX",
           nasys_clock_freq_1000,
           OETH_RXBD_NUM,
           OETH_TXBD_NUM);
  #endif    // CONFIG_EXCALIBUR

    dev->base_addr = OETH_REG_BASE;

    __clear_user(cep,sizeof(*cep));

    /* Get pointer ethernet controller configuration registers.
     */
    cep->regs = (oeth_regs *)(OETH_REG_BASE);
    regs = (oeth_regs *)(OETH_REG_BASE);

    /* Reset the controller.
     */
    regs->moder  =  OETH_MODER_RST;     /* Reset ON */
    regs->moder &= ~OETH_MODER_RST;     /* Reset OFF */

    /* Setting TXBD base to OETH_TXBD_NUM.
     */
    regs->tx_bd_num = OETH_TXBD_NUM;

    /* Initialize TXBD pointer
     */
    cep->tx_bd_base = (oeth_bd *)OETH_BD_BASE;
    tx_bd =  (volatile oeth_bd *)OETH_BD_BASE;

    /* Initialize RXBD pointer
     */
    cep->rx_bd_base = ((oeth_bd *)OETH_BD_BASE) + OETH_TXBD_NUM;
    rx_bd =  ((volatile oeth_bd *)OETH_BD_BASE) + OETH_TXBD_NUM;

    /* Initialize transmit pointers.
     */
    cep->rx_cur = 0;
    cep->tx_next = 0;
    cep->tx_last = 0;
    cep->tx_full = 0;

    /* Set min/max packet length
     */
    regs->packet_len = 0x00400600;

//;dgt-see oeth_phymac_synch /* Set IPGT register to recomended value */
//;dgt-see oeth_phymac_synch regs->ipgt = 0x00000012;

    /* Set IPGR1 register to recomended value
     */
    regs->ipgr1 = 0x0000000c;

    /* Set IPGR2 register to recomended value
     */
    regs->ipgr2 = 0x00000012;

    /* Set COLLCONF register to recomended value
     */
    regs->collconf = 0x000f003f;

    /* Set control module mode
     */
  #if 0
    regs->ctrlmoder = OETH_CTRLMODER_TXFLOW | OETH_CTRLMODER_RXFLOW;
  #else
    regs->ctrlmoder = 0;
  #endif

    #if defined(TDK78Q2120PHY)
      // TDK78Q2120 reset values:
      //
      //   MR0  (Control):      0x3100:
      //       0x00100  FullDuplexIfnoneg
      //       0x01000  AutonegEnabled
      //       0x02000  100BaseTxIfnoneg
      //
      //   MR18 (Diagnostics):  0x0000

      /* TDK78Q2120 LEDs (seven?) NOT configurable?                     */
    #else
      ...Intel LXT971A phy...?...

      /* Set PHY to show Tx status, Rx status and Link status */
      regs->miiaddress = 20<<8;
      regs->miitx_data = 0x1422;
      regs->miicommand = OETH_MIICOMMAND_WCTRLDATA;
    #endif  // TDK78Q2120PHY

#ifdef TXBUFF_PREALLOC

    /* Initialize TXBDs.
     */
    for(i = 0, k = 0; i < OETH_TX_BUFF_PAGE_NUM; i++) {

  #ifndef SRAM_BUFF
        mem_addr = __get_free_page(GFP_KERNEL);
  #endif    // SRAM_BUFF

        for(j = 0; j < OETH_TX_BUFF_PPGAE; j++, k++) {
            tx_bd[k].len_status = OETH_TX_BD_PAD |
                                  OETH_TX_BD_CRC |
                                  OETH_RX_BD_IRQ;
            tx_bd[k].addr = __pa(mem_addr);
            mem_addr += OETH_TX_BUFF_SIZE;
        }
    }
    tx_bd[OETH_TXBD_NUM - 1].len_status |= OETH_TX_BD_WRAP;
#else

    /* Initialize TXBDs.
     */
    for(i = 0; i < OETH_TXBD_NUM; i++) {

        cep->tx_skbuff[i] = NULL;

        tx_bd[i].len_status = (0 << 16)      |
                              OETH_TX_BD_PAD |
                              OETH_TX_BD_CRC |
                              OETH_RX_BD_IRQ;
        tx_bd[i].addr = 0;
    }
    tx_bd[OETH_TXBD_NUM - 1].len_status |= OETH_TX_BD_WRAP;
#endif  // TXBUFF_PREALLOC

#ifdef RXBUFF_PREALLOC

    /* Initialize RXBDs.
     */
    for(i = 0, k = 0; i < OETH_RX_BUFF_PAGE_NUM; i++) {

  #ifndef SRAM_BUFF
        mem_addr = __get_free_page(GFP_KERNEL);
  #endif    // SRAM_BUFF

        for(j = 0; j < OETH_RX_BUFF_PPGAE; j++, k++)
          {
            rx_bd[k].addr = __pa(mem_addr);

            dcache_push (((unsigned long) (rx_bd[k].addr)),
                         OETH_RX_BUFF_SIZE);

            rx_bd[k].len_status = OETH_RX_BD_EMPTY | OETH_RX_BD_IRQ;
              // FIXME...Should we really let the rx ring
              //      ... completely fill...?...
              //      ...Can we actually prevent ?

            mem_addr += OETH_RX_BUFF_SIZE;
          }
    }
    rx_bd[OETH_RXBD_NUM - 1].len_status |= OETH_RX_BD_WRAP;

#else
    /* Initialize RXBDs.
     */
    for(i = 0; i < OETH_RXBD_NUM; i++)
      {
        rx_bd[i].len_status = (0 << 16) | OETH_RX_BD_IRQ;
        cep->rx_skbuff[i]   = NULL;
        rx_bd[i].addr       = 0;
      }
    rx_bd[OETH_RXBD_NUM - 1].len_status |= OETH_RX_BD_WRAP;

#endif  // RXBUFF_PREALLOC

    /* Set default ethernet station address.
     */

  #ifdef CONFIG_EXCALIBUR
   {
    extern unsigned char *excalibur_enet_hwaddr;
    memcpy(dev->dev_addr, excalibur_enet_hwaddr, 6);
   }
  #else
    dev->dev_addr[0] = MACADDR0;
    dev->dev_addr[1] = MACADDR1;
    dev->dev_addr[2] = MACADDR2;
    dev->dev_addr[3] = MACADDR3;
    dev->dev_addr[4] = MACADDR4;
    dev->dev_addr[5] = MACADDR5;
  #endif    // CONFIG_EXCALIBUR

    regs->mac_addr1 = (dev->dev_addr[0]) <<  8 |
                      (dev->dev_addr[1]);
    regs->mac_addr0 = (dev->dev_addr[2]) << 24 |
                      (dev->dev_addr[3]) << 16 |
                      (dev->dev_addr[4]) <<  8 |
                      (dev->dev_addr[5]);

    /* Clear all pending interrupts
     */
    regs->int_src = 0xffffffff;

    /* Promisc, IFG, CRCEn
     */
    regs->moder |= OETH_MODER_PAD | OETH_MODER_IFG | OETH_MODER_CRCEN;

    /* Enable interrupt sources.
     */
    regs->int_mask = OETH_INT_MASK_TXB   |
                     OETH_INT_MASK_TXE   |
                     OETH_INT_MASK_RXF   |
                     OETH_INT_MASK_RXE   |
                     OETH_INT_MASK_BUSY  |
                     OETH_INT_MASK_TXC   |
                     OETH_INT_MASK_RXC;

    /* Fill in the fields of the device structure with ethernet values.
     */
    ether_setup(dev);

    dev->base_addr = (unsigned long)OETH_REG_BASE;

    /* The Open Ethernet specific entries in the device structure.
     */
    dev->open = oeth_open;
    dev->hard_start_xmit = oeth_start_xmit;
    dev->stop = oeth_close;
    dev->get_stats = oeth_get_stats;
    dev->set_multicast_list = oeth_set_multicast_list;
    dev->set_mac_address = oeth_set_mac_add;

#ifdef CONFIG_EXCALIBUR
  #ifdef SRAM_BUFF
    printk(" SRAM @0x%08X",SRAM_BUFF_BASE);
  #endif
    printk(" buffs\n");
    printk("              %s Custom HW ALIGN.\n",
           #if defined(ALT_CI_ALIGN_32_N)
             "WITH"
           #else
             "NO"
           #endif
          );
    printk("              CONFIG_NIOS2_HW_MULX    %sdefined.\n",
           #ifdef CONFIG_NIOS2_HW_MULX
             ""
           #else
             "NOT "
           #endif
          );
    printk("              CONFIG_NIOS2_HW_MUL_OFF %sdefined.\n",
           #ifdef CONFIG_NIOS2_HW_MUL_OFF
             ""
           #else
             "NOT "
           #endif
          );
#endif
#ifdef SANCHKEPKT
    printk("              SANCHKEPKT defined.\n");
#endif

    return 0;
}


/*-----------------------------------------------------------
 | Driver entry point (called by ethif_probe2()).
 |
 | Return:  0   success.
 |
 | Entry condition: Cpu interrupts ENabled
 |                   (in SPITE of claims to contrary).
*/
struct net_device * __init oeth_init(int unit)
{
    struct net_device *dev = alloc_etherdev(sizeof(struct oeth_private));
    int                err;

    if (!dev) return ERR_PTR(-ENODEV);

    sprintf(dev->name, "eth%d", unit);
    netdev_boot_setup_check(dev);

    PRINTK2("%s:oeth_init\n", dev->name);

    if (oeth_probe(dev) != 0)
      {
        err = -ENODEV;
        goto out;
      }

    err = register_netdev(dev);
    if (!err) return dev;
out:
    free_netdev(dev);
    return ERR_PTR(err);
}
