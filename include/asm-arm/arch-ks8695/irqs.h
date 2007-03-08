/*
 *  linux/include/asm-arm/arch-ks8695/irqs.h
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H 1

/*
 * IRQ definitions
 */
#define KS8695_INT_EXT_INT0                    2
#define KS8695_INT_EXT_INT1                    3
#define KS8695_INT_EXT_INT2                    4
#define KS8695_INT_EXT_INT3                    5
#define KS8695_INT_TIMERINT0                   6
#define KS8695_INT_TIMERINT1                   7 
#define KS8695_INT_UART_TX                     8
#define KS8695_INT_UART_RX                     9
#define KS8695_INT_UART_LINE_ERR               10
#define KS8695_INT_UART_MODEMS                 11
#define KS8695_INT_LAN_STOP_RX                 12
#define KS8695_INT_LAN_STOP_TX                 13
#define KS8695_INT_LAN_BUF_RX_STATUS           14
#define KS8695_INT_LAN_BUF_TX_STATUS           15
#define KS8695_INT_LAN_RX_STATUS               16
#define KS8695_INT_LAN_TX_STATUS               17
#define KS8695_INT_HPAN_STOP_RX                18
#define KS8695_INT_HPNA_STOP_TX                19
#define KS8695_INT_HPNA_BUF_RX_STATUS          20
#define KS8695_INT_HPNA_BUF_TX_STATUS          21
#define KS8695_INT_HPNA_RX_STATUS              22
#define KS8695_INT_HPNA_TX_STATUS              23
#define KS8695_INT_BUS_ERROR                   24
#define KS8695_INT_WAN_STOP_RX                 25
#define KS8695_INT_WAN_STOP_TX                 26
#define KS8695_INT_WAN_BUF_RX_STATUS           27
#define KS8695_INT_WAN_BUF_TX_STATUS           28
#define KS8695_INT_WAN_RX_STATUS               29
#define KS8695_INT_WAN_TX_STATUS               30

#define KS8695_INT_UART                        KS8695_INT_UART_TX

/*
 * IRQ bit masks
 */
#define KS8695_INTMASK_EXT_INT0                (1 << KS8695_INT_EXT_INT0)
#define KS8695_INTMASK_EXT_INT1                (1 << KS8695_INT_EXT_INT1)
#define KS8695_INTMASK_EXT_INT2                (1 << KS8695_INT_EXT_INT2)
#define KS8695_INTMASK_EXT_INT3                (1 << KS8695_INT_EXT_INT3)
#define KS8695_INTMASK_TIMERINT0               (1 << KS8695_INT_TIMERINT0)
#define KS8695_INTMASK_TIMERINT1               (1 << KS8695_INT_TIMERINT1)
#define KS8695_INTMASK_UART_TX                 (1 << KS8695_INT_UART_TX)
#define KS8695_INTMASK_UART_RX                 (1 << KS8695_INT_UART_RX)
#define KS8695_INTMASK_UART_LINE_ERR           (1 << KS8695_INT_UART_LINE_ERR)
#define KS8695_INTMASK_UART_MODEMS             (1 << KS8695_INT_UART_MODEMS)
#define KS8695_INTMASK_LAN_STOP_RX             (1 << KS8695_INT_LAN_STOP_RX)
#define KS8695_INTMASK_LAN_STOP_TX             (1 << KS8695_INT_LAN_STOP_TX)
#define KS8695_INTMASK_LAN_BUF_RX_STATUS       (1 << KS8695_INT_LAN_BUF_RX_STATUS)
#define KS8695_INTMASK_LAN_BUF_TX_STATUS       (1 << KS8695_INT_LAN_BUF_TX_STATUS)
#define KS8695_INTMASK_LAN_RX_STATUS           (1 << KS8695_INT_LAN_RX_STATUS)
#define KS8695_INTMASK_LAN_TX_STATUS           (1 << KS8695_INT_LAN_RX_STATUS)
#define KS8695_INTMASK_HPAN_STOP_RX            (1 << KS8695_INT_HPAN_STOP_RX)
#define KS8695_INTMASK_HPNA_STOP_TX            (1 << KS8695_INT_HPNA_STOP_TX)
#define KS8695_INTMASK_HPNA_BUF_RX_STATUS      (1 << KS8695_INT_HPNA_BUF_RX_STATUS)
#define KS8695_INTMAKS_HPNA_BUF_TX_STATUS      (1 << KS8695_INT_HPNA_BUF_TX_STATUS)
#define KS8695_INTMASK_HPNA_RX_STATUS          (1 << KS8695_INT_HPNA_RX_STATUS)
#define KS8695_INTMASK_HPNA_TX_STATUS          (1 << KS8695_INT_HPNA_TX_STATUS)
#define KS8695_INTMASK_BUS_ERROR               (1 << KS8695_INT_BUS_ERROR)
#define KS8695_INTMASK_WAN_STOP_RX             (1 << KS8695_INT_WAN_STOP_RX)
#define KS8695_INTMASK_WAN_STOP_TX             (1 << KS8695_INT_WAN_STOP_TX)
#define KS8695_INTMASK_WAN_BUF_RX_STATUS       (1 << KS8695_INT_WAN_BUF_RX_STATUS)
#define KS8695_INTMASK_WAN_BUF_TX_STATUS       (1 << KS8695_INT_WAN_BUF_TX_STATUS)
#define KS8695_INTMASK_WAN_RX_STATUS           (1 << KS8695_INT_WAN_RX_STATUS)
#define KS8695_INTMASK_WAN_TX_STATUS           (1 << KS8695_INT_WAN_TX_STATUS)

#define KS8695_SC_VALID_INT                    0xFFFFFFFF


#define NR_IRQS		(32)

#endif /* __ASM_ARCH_IRQS_H */
