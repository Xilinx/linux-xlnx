// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */
/*-----------------------------------------------------------------------------
 * C Header bank BASE definitions
 *-----------------------------------------------------------------------------
 */
#define ROE_RADIO_CFG_BASE_ADDR 0x0 /* 0 */
#define ROE_RADIO_SOURCE_BASE_ADDR 0x4096 /* 4096 */

/*-----------------------------------------------------------------------------
 * C Header bank register definitions for bank roe_radio_cfg
 * with prefix cfg_ @ address 0x0
 *-----------------------------------------------------------------------------
 */
/* Type = roInt */
#define CFG_RADIO_ID_ADDR 0x0 /* 0 */
#define CFG_RADIO_ID_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_ID_OFFSET 0x0 /* 0 */
#define CFG_RADIO_ID_WIDTH 0x32 /* 32 */
#define CFG_RADIO_ID_DEFAULT 0x1179649 /* 1179649 */

/* Type = rw */
#define CFG_TIMEOUT_ENABLE_ADDR 0x4 /* 4 */
#define CFG_TIMEOUT_ENABLE_MASK 0x1 /* 1 */
#define CFG_TIMEOUT_ENABLE_OFFSET 0x0 /* 0 */
#define CFG_TIMEOUT_ENABLE_WIDTH 0x1 /* 1 */
#define CFG_TIMEOUT_ENABLE_DEFAULT 0x0 /* 0 */

/* Type = ro */
#define CFG_TIMEOUT_STATUS_ADDR 0x8 /* 8 */
#define CFG_TIMEOUT_STATUS_MASK 0x1 /* 1 */
#define CFG_TIMEOUT_STATUS_OFFSET 0x0 /* 0 */
#define CFG_TIMEOUT_STATUS_WIDTH 0x1 /* 1 */
#define CFG_TIMEOUT_STATUS_DEFAULT 0x1 /* 1 */

/* Type = rw */
#define CFG_TIMEOUT_VALUE_ADDR 0x12 /* 12 */
#define CFG_TIMEOUT_VALUE_MASK 0x4095 /* 4095 */
#define CFG_TIMEOUT_VALUE_OFFSET 0x0 /* 0 */
#define CFG_TIMEOUT_VALUE_WIDTH 0x12 /* 12 */
#define CFG_TIMEOUT_VALUE_DEFAULT 0x128 /* 128 */

/* Type = rw */
#define CFG_GPIO_CDC_LEDMODE2_ADDR 0x16 /* 16 */
#define CFG_GPIO_CDC_LEDMODE2_MASK 0x1 /* 1 */
#define CFG_GPIO_CDC_LEDMODE2_OFFSET 0x0 /* 0 */
#define CFG_GPIO_CDC_LEDMODE2_WIDTH 0x1 /* 1 */
#define CFG_GPIO_CDC_LEDMODE2_DEFAULT 0x0 /* 0 */

/* Type = rw */
#define CFG_GPIO_CDC_LEDGPIO_ADDR 0x16 /* 16 */
#define CFG_GPIO_CDC_LEDGPIO_MASK 0x48 /* 48 */
#define CFG_GPIO_CDC_LEDGPIO_OFFSET 0x4 /* 4 */
#define CFG_GPIO_CDC_LEDGPIO_WIDTH 0x2 /* 2 */
#define CFG_GPIO_CDC_LEDGPIO_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_GPIO_CDC_DIPSTATUS_ADDR 0x20 /* 20 */
#define CFG_GPIO_CDC_DIPSTATUS_MASK 0x255 /* 255 */
#define CFG_GPIO_CDC_DIPSTATUS_OFFSET 0x0 /* 0 */
#define CFG_GPIO_CDC_DIPSTATUS_WIDTH 0x8 /* 8 */
#define CFG_GPIO_CDC_DIPSTATUS_DEFAULT 0x0 /* 0 */

/* Type = wPlsH */
#define CFG_SW_TRIGGER_ADDR 0x32 /* 32 */
#define CFG_SW_TRIGGER_MASK 0x1 /* 1 */
#define CFG_SW_TRIGGER_OFFSET 0x0 /* 0 */
#define CFG_SW_TRIGGER_WIDTH 0x1 /* 1 */
#define CFG_SW_TRIGGER_DEFAULT 0x0 /* 0 */

/* Type = rw */
#define CFG_RADIO_CDC_ENABLE_ADDR 0x36 /* 36 */
#define CFG_RADIO_CDC_ENABLE_MASK 0x1 /* 1 */
#define CFG_RADIO_CDC_ENABLE_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_ENABLE_WIDTH 0x1 /* 1 */
#define CFG_RADIO_CDC_ENABLE_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_ERROR_ADDR 0x36 /* 36 */
#define CFG_RADIO_CDC_ERROR_MASK 0x2 /* 2 */
#define CFG_RADIO_CDC_ERROR_OFFSET 0x1 /* 1 */
#define CFG_RADIO_CDC_ERROR_WIDTH 0x1 /* 1 */
#define CFG_RADIO_CDC_ERROR_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_STATUS_ADDR 0x36 /* 36 */
#define CFG_RADIO_CDC_STATUS_MASK 0x4 /* 4 */
#define CFG_RADIO_CDC_STATUS_OFFSET 0x2 /* 2 */
#define CFG_RADIO_CDC_STATUS_WIDTH 0x1 /* 1 */
#define CFG_RADIO_CDC_STATUS_DEFAULT 0x0 /* 0 */

/* Type = rw */
#define CFG_RADIO_CDC_LOOPBACK_ADDR 0x40 /* 40 */
#define CFG_RADIO_CDC_LOOPBACK_MASK 0x1 /* 1 */
#define CFG_RADIO_CDC_LOOPBACK_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_LOOPBACK_WIDTH 0x1 /* 1 */
#define CFG_RADIO_CDC_LOOPBACK_DEFAULT 0x0 /* 0 */

/* Type = rw */
#define CFG_RADIO_SINK_ENABLE_ADDR 0x44 /* 44 */
#define CFG_RADIO_SINK_ENABLE_MASK 0x1 /* 1 */
#define CFG_RADIO_SINK_ENABLE_OFFSET 0x0 /* 0 */
#define CFG_RADIO_SINK_ENABLE_WIDTH 0x1 /* 1 */
#define CFG_RADIO_SINK_ENABLE_DEFAULT 0x1 /* 1 */

/* Type = roSig */
#define CFG_RADIO_CDC_ERROR_31_0_ADDR 0x48 /* 48 */
#define CFG_RADIO_CDC_ERROR_31_0_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_ERROR_31_0_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_ERROR_31_0_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_ERROR_31_0_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_ERROR_63_32_ADDR 0x52 /* 52 */
#define CFG_RADIO_CDC_ERROR_63_32_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_ERROR_63_32_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_ERROR_63_32_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_ERROR_63_32_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_ERROR_95_64_ADDR 0x56 /* 56 */
#define CFG_RADIO_CDC_ERROR_95_64_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_ERROR_95_64_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_ERROR_95_64_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_ERROR_95_64_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_ERROR_127_96_ADDR 0x60 /* 60 */
#define CFG_RADIO_CDC_ERROR_127_96_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_ERROR_127_96_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_ERROR_127_96_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_ERROR_127_96_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_STATUS_31_0_ADDR 0x64 /* 64 */
#define CFG_RADIO_CDC_STATUS_31_0_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_STATUS_31_0_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_STATUS_31_0_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_STATUS_31_0_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_STATUS_63_32_ADDR 0x68 /* 68 */
#define CFG_RADIO_CDC_STATUS_63_32_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_STATUS_63_32_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_STATUS_63_32_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_STATUS_63_32_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_STATUS_95_64_ADDR 0x72 /* 72 */
#define CFG_RADIO_CDC_STATUS_95_64_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_STATUS_95_64_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_STATUS_95_64_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_STATUS_95_64_DEFAULT 0x0 /* 0 */

/* Type = roSig */
#define CFG_RADIO_CDC_STATUS_127_96_ADDR 0x76 /* 76 */
#define CFG_RADIO_CDC_STATUS_127_96_MASK 0x4294967295 /* 4294967295 */
#define CFG_RADIO_CDC_STATUS_127_96_OFFSET 0x0 /* 0 */
#define CFG_RADIO_CDC_STATUS_127_96_WIDTH 0x32 /* 32 */
#define CFG_RADIO_CDC_STATUS_127_96_DEFAULT 0x0 /* 0 */

/*-----------------------------------------------------------------------------
 * C Header bank register definitions for bank roe_radio_source
 * with prefix fram_ @ address 0x1000
 *-----------------------------------------------------------------------------
 */
/* Type = rwpdef */
#define FRAM_PACKET_SIZE_ADDR 0x4096 /* 4096 */
#define FRAM_PACKET_SIZE_MASK 0x65535 /* 65535 */
#define FRAM_PACKET_SIZE_OFFSET 0x0 /* 0 */
#define FRAM_PACKET_SIZE_WIDTH 0x16 /* 16 */
#define FRAM_PACKET_SIZE_DEFAULT 0x0 /* 0 */

/* Type = rwpdef */
#define FRAM_PAUSE_SIZE_ADDR 0x4100 /* 4100 */
#define FRAM_PAUSE_SIZE_MASK 0x255 /* 255 */
#define FRAM_PAUSE_SIZE_OFFSET 0x0 /* 0 */
#define FRAM_PAUSE_SIZE_WIDTH 0x8 /* 8 */
#define FRAM_PAUSE_SIZE_DEFAULT 0x0 /* 0 */
