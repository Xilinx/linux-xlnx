/*
 * Pin controller and GPIO driver for Amlogic Meson GXBB.
 *
 * Copyright (C) 2016 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <dt-bindings/gpio/meson-gxbb-gpio.h>
#include "pinctrl-meson.h"

#define EE_OFF	14

static const struct pinctrl_pin_desc meson_gxbb_periphs_pins[] = {
	MESON_PIN(GPIOZ_0, EE_OFF),
	MESON_PIN(GPIOZ_1, EE_OFF),
	MESON_PIN(GPIOZ_2, EE_OFF),
	MESON_PIN(GPIOZ_3, EE_OFF),
	MESON_PIN(GPIOZ_4, EE_OFF),
	MESON_PIN(GPIOZ_5, EE_OFF),
	MESON_PIN(GPIOZ_6, EE_OFF),
	MESON_PIN(GPIOZ_7, EE_OFF),
	MESON_PIN(GPIOZ_8, EE_OFF),
	MESON_PIN(GPIOZ_9, EE_OFF),
	MESON_PIN(GPIOZ_10, EE_OFF),
	MESON_PIN(GPIOZ_11, EE_OFF),
	MESON_PIN(GPIOZ_12, EE_OFF),
	MESON_PIN(GPIOZ_13, EE_OFF),
	MESON_PIN(GPIOZ_14, EE_OFF),
	MESON_PIN(GPIOZ_15, EE_OFF),

	MESON_PIN(GPIOH_0, EE_OFF),
	MESON_PIN(GPIOH_1, EE_OFF),
	MESON_PIN(GPIOH_2, EE_OFF),
	MESON_PIN(GPIOH_3, EE_OFF),

	MESON_PIN(BOOT_0, EE_OFF),
	MESON_PIN(BOOT_1, EE_OFF),
	MESON_PIN(BOOT_2, EE_OFF),
	MESON_PIN(BOOT_3, EE_OFF),
	MESON_PIN(BOOT_4, EE_OFF),
	MESON_PIN(BOOT_5, EE_OFF),
	MESON_PIN(BOOT_6, EE_OFF),
	MESON_PIN(BOOT_7, EE_OFF),
	MESON_PIN(BOOT_8, EE_OFF),
	MESON_PIN(BOOT_9, EE_OFF),
	MESON_PIN(BOOT_10, EE_OFF),
	MESON_PIN(BOOT_11, EE_OFF),
	MESON_PIN(BOOT_12, EE_OFF),
	MESON_PIN(BOOT_13, EE_OFF),
	MESON_PIN(BOOT_14, EE_OFF),
	MESON_PIN(BOOT_15, EE_OFF),
	MESON_PIN(BOOT_16, EE_OFF),
	MESON_PIN(BOOT_17, EE_OFF),

	MESON_PIN(CARD_0, EE_OFF),
	MESON_PIN(CARD_1, EE_OFF),
	MESON_PIN(CARD_2, EE_OFF),
	MESON_PIN(CARD_3, EE_OFF),
	MESON_PIN(CARD_4, EE_OFF),
	MESON_PIN(CARD_5, EE_OFF),
	MESON_PIN(CARD_6, EE_OFF),

	MESON_PIN(GPIODV_0, EE_OFF),
	MESON_PIN(GPIODV_1, EE_OFF),
	MESON_PIN(GPIODV_2, EE_OFF),
	MESON_PIN(GPIODV_3, EE_OFF),
	MESON_PIN(GPIODV_4, EE_OFF),
	MESON_PIN(GPIODV_5, EE_OFF),
	MESON_PIN(GPIODV_6, EE_OFF),
	MESON_PIN(GPIODV_7, EE_OFF),
	MESON_PIN(GPIODV_8, EE_OFF),
	MESON_PIN(GPIODV_9, EE_OFF),
	MESON_PIN(GPIODV_10, EE_OFF),
	MESON_PIN(GPIODV_11, EE_OFF),
	MESON_PIN(GPIODV_12, EE_OFF),
	MESON_PIN(GPIODV_13, EE_OFF),
	MESON_PIN(GPIODV_14, EE_OFF),
	MESON_PIN(GPIODV_15, EE_OFF),
	MESON_PIN(GPIODV_16, EE_OFF),
	MESON_PIN(GPIODV_17, EE_OFF),
	MESON_PIN(GPIODV_19, EE_OFF),
	MESON_PIN(GPIODV_20, EE_OFF),
	MESON_PIN(GPIODV_21, EE_OFF),
	MESON_PIN(GPIODV_22, EE_OFF),
	MESON_PIN(GPIODV_23, EE_OFF),
	MESON_PIN(GPIODV_24, EE_OFF),
	MESON_PIN(GPIODV_25, EE_OFF),
	MESON_PIN(GPIODV_26, EE_OFF),
	MESON_PIN(GPIODV_27, EE_OFF),
	MESON_PIN(GPIODV_28, EE_OFF),
	MESON_PIN(GPIODV_29, EE_OFF),

	MESON_PIN(GPIOY_0, EE_OFF),
	MESON_PIN(GPIOY_1, EE_OFF),
	MESON_PIN(GPIOY_2, EE_OFF),
	MESON_PIN(GPIOY_3, EE_OFF),
	MESON_PIN(GPIOY_4, EE_OFF),
	MESON_PIN(GPIOY_5, EE_OFF),
	MESON_PIN(GPIOY_6, EE_OFF),
	MESON_PIN(GPIOY_7, EE_OFF),
	MESON_PIN(GPIOY_8, EE_OFF),
	MESON_PIN(GPIOY_9, EE_OFF),
	MESON_PIN(GPIOY_10, EE_OFF),
	MESON_PIN(GPIOY_11, EE_OFF),
	MESON_PIN(GPIOY_12, EE_OFF),
	MESON_PIN(GPIOY_13, EE_OFF),
	MESON_PIN(GPIOY_14, EE_OFF),
	MESON_PIN(GPIOY_15, EE_OFF),
	MESON_PIN(GPIOY_16, EE_OFF),

	MESON_PIN(GPIOX_0, EE_OFF),
	MESON_PIN(GPIOX_1, EE_OFF),
	MESON_PIN(GPIOX_2, EE_OFF),
	MESON_PIN(GPIOX_3, EE_OFF),
	MESON_PIN(GPIOX_4, EE_OFF),
	MESON_PIN(GPIOX_5, EE_OFF),
	MESON_PIN(GPIOX_6, EE_OFF),
	MESON_PIN(GPIOX_7, EE_OFF),
	MESON_PIN(GPIOX_8, EE_OFF),
	MESON_PIN(GPIOX_9, EE_OFF),
	MESON_PIN(GPIOX_10, EE_OFF),
	MESON_PIN(GPIOX_11, EE_OFF),
	MESON_PIN(GPIOX_12, EE_OFF),
	MESON_PIN(GPIOX_13, EE_OFF),
	MESON_PIN(GPIOX_14, EE_OFF),
	MESON_PIN(GPIOX_15, EE_OFF),
	MESON_PIN(GPIOX_16, EE_OFF),
	MESON_PIN(GPIOX_17, EE_OFF),
	MESON_PIN(GPIOX_18, EE_OFF),
	MESON_PIN(GPIOX_19, EE_OFF),
	MESON_PIN(GPIOX_20, EE_OFF),
	MESON_PIN(GPIOX_21, EE_OFF),
	MESON_PIN(GPIOX_22, EE_OFF),

	MESON_PIN(GPIOCLK_0, EE_OFF),
	MESON_PIN(GPIOCLK_1, EE_OFF),
	MESON_PIN(GPIOCLK_2, EE_OFF),
	MESON_PIN(GPIOCLK_3, EE_OFF),

	MESON_PIN(GPIO_TEST_N, EE_OFF),
};

static const unsigned int emmc_nand_d07_pins[] = {
	PIN(BOOT_0, EE_OFF), PIN(BOOT_1, EE_OFF), PIN(BOOT_2, EE_OFF),
	PIN(BOOT_3, EE_OFF), PIN(BOOT_4, EE_OFF), PIN(BOOT_5, EE_OFF),
	PIN(BOOT_6, EE_OFF), PIN(BOOT_7, EE_OFF),
};
static const unsigned int emmc_clk_pins[] = { PIN(BOOT_8, EE_OFF) };
static const unsigned int emmc_cmd_pins[] = { PIN(BOOT_10, EE_OFF) };
static const unsigned int emmc_ds_pins[] = { PIN(BOOT_15, EE_OFF) };

static const unsigned int nor_d_pins[]		= { PIN(BOOT_11, EE_OFF) };
static const unsigned int nor_q_pins[]		= { PIN(BOOT_12, EE_OFF) };
static const unsigned int nor_c_pins[]		= { PIN(BOOT_13, EE_OFF) };
static const unsigned int nor_cs_pins[]		= { PIN(BOOT_15, EE_OFF) };

static const unsigned int sdcard_d0_pins[] = { PIN(CARD_1, EE_OFF) };
static const unsigned int sdcard_d1_pins[] = { PIN(CARD_0, EE_OFF) };
static const unsigned int sdcard_d2_pins[] = { PIN(CARD_5, EE_OFF) };
static const unsigned int sdcard_d3_pins[] = { PIN(CARD_4, EE_OFF) };
static const unsigned int sdcard_cmd_pins[] = { PIN(CARD_3, EE_OFF) };
static const unsigned int sdcard_clk_pins[] = { PIN(CARD_2, EE_OFF) };

static const unsigned int sdio_d0_pins[] = { PIN(GPIOX_0, EE_OFF) };
static const unsigned int sdio_d1_pins[] = { PIN(GPIOX_1, EE_OFF) };
static const unsigned int sdio_d2_pins[] = { PIN(GPIOX_2, EE_OFF) };
static const unsigned int sdio_d3_pins[] = { PIN(GPIOX_3, EE_OFF) };
static const unsigned int sdio_cmd_pins[] = { PIN(GPIOX_4, EE_OFF) };
static const unsigned int sdio_clk_pins[] = { PIN(GPIOX_5, EE_OFF) };
static const unsigned int sdio_irq_pins[] = { PIN(GPIOX_7, EE_OFF) };

static const unsigned int nand_ce0_pins[]	= { PIN(BOOT_8, EE_OFF) };
static const unsigned int nand_ce1_pins[]	= { PIN(BOOT_9, EE_OFF) };
static const unsigned int nand_rb0_pins[]	= { PIN(BOOT_10, EE_OFF) };
static const unsigned int nand_ale_pins[]	= { PIN(BOOT_11, EE_OFF) };
static const unsigned int nand_cle_pins[]	= { PIN(BOOT_12, EE_OFF) };
static const unsigned int nand_wen_clk_pins[]	= { PIN(BOOT_13, EE_OFF) };
static const unsigned int nand_ren_wr_pins[]	= { PIN(BOOT_14, EE_OFF) };
static const unsigned int nand_dqs_pins[]	= { PIN(BOOT_15, EE_OFF) };

static const unsigned int uart_tx_a_pins[]	= { PIN(GPIOX_12, EE_OFF) };
static const unsigned int uart_rx_a_pins[]	= { PIN(GPIOX_13, EE_OFF) };
static const unsigned int uart_cts_a_pins[]	= { PIN(GPIOX_14, EE_OFF) };
static const unsigned int uart_rts_a_pins[]	= { PIN(GPIOX_15, EE_OFF) };

static const unsigned int uart_tx_b_pins[]	= { PIN(GPIODV_24, EE_OFF) };
static const unsigned int uart_rx_b_pins[]	= { PIN(GPIODV_25, EE_OFF) };
static const unsigned int uart_cts_b_pins[]	= { PIN(GPIODV_26, EE_OFF) };
static const unsigned int uart_rts_b_pins[]	= { PIN(GPIODV_27, EE_OFF) };

static const unsigned int uart_tx_c_pins[]	= { PIN(GPIOY_13, EE_OFF) };
static const unsigned int uart_rx_c_pins[]	= { PIN(GPIOY_14, EE_OFF) };
static const unsigned int uart_cts_c_pins[]	= { PIN(GPIOX_11, EE_OFF) };
static const unsigned int uart_rts_c_pins[]	= { PIN(GPIOX_12, EE_OFF) };

static const unsigned int i2c_sck_a_pins[]	= { PIN(GPIODV_25, EE_OFF) };
static const unsigned int i2c_sda_a_pins[]	= { PIN(GPIODV_24, EE_OFF) };

static const unsigned int i2c_sck_b_pins[]	= { PIN(GPIODV_27, EE_OFF) };
static const unsigned int i2c_sda_b_pins[]	= { PIN(GPIODV_26, EE_OFF) };

static const unsigned int i2c_sck_c_pins[]	= { PIN(GPIODV_29, EE_OFF) };
static const unsigned int i2c_sda_c_pins[]	= { PIN(GPIODV_28, EE_OFF) };

static const unsigned int eth_mdio_pins[]	= { PIN(GPIOZ_0, EE_OFF) };
static const unsigned int eth_mdc_pins[]	= { PIN(GPIOZ_1, EE_OFF) };
static const unsigned int eth_clk_rx_clk_pins[]	= { PIN(GPIOZ_2, EE_OFF) };
static const unsigned int eth_rx_dv_pins[]	= { PIN(GPIOZ_3, EE_OFF) };
static const unsigned int eth_rxd0_pins[]	= { PIN(GPIOZ_4, EE_OFF) };
static const unsigned int eth_rxd1_pins[]	= { PIN(GPIOZ_5, EE_OFF) };
static const unsigned int eth_rxd2_pins[]	= { PIN(GPIOZ_6, EE_OFF) };
static const unsigned int eth_rxd3_pins[]	= { PIN(GPIOZ_7, EE_OFF) };
static const unsigned int eth_rgmii_tx_clk_pins[] = { PIN(GPIOZ_8, EE_OFF) };
static const unsigned int eth_tx_en_pins[]	= { PIN(GPIOZ_9, EE_OFF) };
static const unsigned int eth_txd0_pins[]	= { PIN(GPIOZ_10, EE_OFF) };
static const unsigned int eth_txd1_pins[]	= { PIN(GPIOZ_11, EE_OFF) };
static const unsigned int eth_txd2_pins[]	= { PIN(GPIOZ_12, EE_OFF) };
static const unsigned int eth_txd3_pins[]	= { PIN(GPIOZ_13, EE_OFF) };

static const unsigned int pwm_a_x_pins[]	= { PIN(GPIOX_6, EE_OFF) };
static const unsigned int pwm_a_y_pins[]	= { PIN(GPIOY_16, EE_OFF) };
static const unsigned int pwm_b_pins[]		= { PIN(GPIODV_29, EE_OFF) };
static const unsigned int pwm_d_pins[]		= { PIN(GPIODV_28, EE_OFF) };
static const unsigned int pwm_e_pins[]		= { PIN(GPIOX_19, EE_OFF) };
static const unsigned int pwm_f_x_pins[]	= { PIN(GPIOX_7, EE_OFF) };
static const unsigned int pwm_f_y_pins[]	= { PIN(GPIOY_15, EE_OFF) };

static const struct pinctrl_pin_desc meson_gxbb_aobus_pins[] = {
	MESON_PIN(GPIOAO_0, 0),
	MESON_PIN(GPIOAO_1, 0),
	MESON_PIN(GPIOAO_2, 0),
	MESON_PIN(GPIOAO_3, 0),
	MESON_PIN(GPIOAO_4, 0),
	MESON_PIN(GPIOAO_5, 0),
	MESON_PIN(GPIOAO_6, 0),
	MESON_PIN(GPIOAO_7, 0),
	MESON_PIN(GPIOAO_8, 0),
	MESON_PIN(GPIOAO_9, 0),
	MESON_PIN(GPIOAO_10, 0),
	MESON_PIN(GPIOAO_11, 0),
	MESON_PIN(GPIOAO_12, 0),
	MESON_PIN(GPIOAO_13, 0),
};

static const unsigned int uart_tx_ao_a_pins[]	= { PIN(GPIOAO_0, 0) };
static const unsigned int uart_rx_ao_a_pins[]	= { PIN(GPIOAO_1, 0) };
static const unsigned int uart_cts_ao_a_pins[]	= { PIN(GPIOAO_2, 0) };
static const unsigned int uart_rts_ao_a_pins[]	= { PIN(GPIOAO_3, 0) };
static const unsigned int uart_tx_ao_b_pins[]	= { PIN(GPIOAO_0, 0) };
static const unsigned int uart_rx_ao_b_pins[]	= { PIN(GPIOAO_1, 0),
						    PIN(GPIOAO_5, 0) };
static const unsigned int uart_cts_ao_b_pins[]	= { PIN(GPIOAO_2, 0) };
static const unsigned int uart_rts_ao_b_pins[]	= { PIN(GPIOAO_3, 0) };

static const unsigned int i2c_sck_ao_pins[] = {PIN(GPIOAO_4, 0) };
static const unsigned int i2c_sda_ao_pins[] = {PIN(GPIOAO_5, 0) };
static const unsigned int i2c_slave_sck_ao_pins[] = {PIN(GPIOAO_4, 0) };
static const unsigned int i2c_slave_sda_ao_pins[] = {PIN(GPIOAO_5, 0) };

static const unsigned int remote_input_ao_pins[] = {PIN(GPIOAO_7, 0) };

static const unsigned int pwm_ao_a_3_pins[]	= { PIN(GPIOAO_3, 0) };
static const unsigned int pwm_ao_a_6_pins[]	= { PIN(GPIOAO_6, 0) };
static const unsigned int pwm_ao_a_12_pins[]	= { PIN(GPIOAO_12, 0) };
static const unsigned int pwm_ao_b_pins[]	= { PIN(GPIOAO_13, 0) };

static struct meson_pmx_group meson_gxbb_periphs_groups[] = {
	GPIO_GROUP(GPIOZ_0, EE_OFF),
	GPIO_GROUP(GPIOZ_1, EE_OFF),
	GPIO_GROUP(GPIOZ_2, EE_OFF),
	GPIO_GROUP(GPIOZ_3, EE_OFF),
	GPIO_GROUP(GPIOZ_4, EE_OFF),
	GPIO_GROUP(GPIOZ_5, EE_OFF),
	GPIO_GROUP(GPIOZ_6, EE_OFF),
	GPIO_GROUP(GPIOZ_7, EE_OFF),
	GPIO_GROUP(GPIOZ_8, EE_OFF),
	GPIO_GROUP(GPIOZ_9, EE_OFF),
	GPIO_GROUP(GPIOZ_10, EE_OFF),
	GPIO_GROUP(GPIOZ_11, EE_OFF),
	GPIO_GROUP(GPIOZ_12, EE_OFF),
	GPIO_GROUP(GPIOZ_13, EE_OFF),
	GPIO_GROUP(GPIOZ_14, EE_OFF),
	GPIO_GROUP(GPIOZ_15, EE_OFF),

	GPIO_GROUP(GPIOH_0, EE_OFF),
	GPIO_GROUP(GPIOH_1, EE_OFF),
	GPIO_GROUP(GPIOH_2, EE_OFF),
	GPIO_GROUP(GPIOH_3, EE_OFF),

	GPIO_GROUP(BOOT_0, EE_OFF),
	GPIO_GROUP(BOOT_1, EE_OFF),
	GPIO_GROUP(BOOT_2, EE_OFF),
	GPIO_GROUP(BOOT_3, EE_OFF),
	GPIO_GROUP(BOOT_4, EE_OFF),
	GPIO_GROUP(BOOT_5, EE_OFF),
	GPIO_GROUP(BOOT_6, EE_OFF),
	GPIO_GROUP(BOOT_7, EE_OFF),
	GPIO_GROUP(BOOT_8, EE_OFF),
	GPIO_GROUP(BOOT_9, EE_OFF),
	GPIO_GROUP(BOOT_10, EE_OFF),
	GPIO_GROUP(BOOT_11, EE_OFF),
	GPIO_GROUP(BOOT_12, EE_OFF),
	GPIO_GROUP(BOOT_13, EE_OFF),
	GPIO_GROUP(BOOT_14, EE_OFF),
	GPIO_GROUP(BOOT_15, EE_OFF),
	GPIO_GROUP(BOOT_16, EE_OFF),
	GPIO_GROUP(BOOT_17, EE_OFF),

	GPIO_GROUP(CARD_0, EE_OFF),
	GPIO_GROUP(CARD_1, EE_OFF),
	GPIO_GROUP(CARD_2, EE_OFF),
	GPIO_GROUP(CARD_3, EE_OFF),
	GPIO_GROUP(CARD_4, EE_OFF),
	GPIO_GROUP(CARD_5, EE_OFF),
	GPIO_GROUP(CARD_6, EE_OFF),

	GPIO_GROUP(GPIODV_0, EE_OFF),
	GPIO_GROUP(GPIODV_1, EE_OFF),
	GPIO_GROUP(GPIODV_2, EE_OFF),
	GPIO_GROUP(GPIODV_3, EE_OFF),
	GPIO_GROUP(GPIODV_4, EE_OFF),
	GPIO_GROUP(GPIODV_5, EE_OFF),
	GPIO_GROUP(GPIODV_6, EE_OFF),
	GPIO_GROUP(GPIODV_7, EE_OFF),
	GPIO_GROUP(GPIODV_8, EE_OFF),
	GPIO_GROUP(GPIODV_9, EE_OFF),
	GPIO_GROUP(GPIODV_10, EE_OFF),
	GPIO_GROUP(GPIODV_11, EE_OFF),
	GPIO_GROUP(GPIODV_12, EE_OFF),
	GPIO_GROUP(GPIODV_13, EE_OFF),
	GPIO_GROUP(GPIODV_14, EE_OFF),
	GPIO_GROUP(GPIODV_15, EE_OFF),
	GPIO_GROUP(GPIODV_16, EE_OFF),
	GPIO_GROUP(GPIODV_17, EE_OFF),
	GPIO_GROUP(GPIODV_19, EE_OFF),
	GPIO_GROUP(GPIODV_20, EE_OFF),
	GPIO_GROUP(GPIODV_21, EE_OFF),
	GPIO_GROUP(GPIODV_22, EE_OFF),
	GPIO_GROUP(GPIODV_23, EE_OFF),
	GPIO_GROUP(GPIODV_24, EE_OFF),
	GPIO_GROUP(GPIODV_25, EE_OFF),
	GPIO_GROUP(GPIODV_26, EE_OFF),
	GPIO_GROUP(GPIODV_27, EE_OFF),
	GPIO_GROUP(GPIODV_28, EE_OFF),
	GPIO_GROUP(GPIODV_29, EE_OFF),

	GPIO_GROUP(GPIOY_0, EE_OFF),
	GPIO_GROUP(GPIOY_1, EE_OFF),
	GPIO_GROUP(GPIOY_2, EE_OFF),
	GPIO_GROUP(GPIOY_3, EE_OFF),
	GPIO_GROUP(GPIOY_4, EE_OFF),
	GPIO_GROUP(GPIOY_5, EE_OFF),
	GPIO_GROUP(GPIOY_6, EE_OFF),
	GPIO_GROUP(GPIOY_7, EE_OFF),
	GPIO_GROUP(GPIOY_8, EE_OFF),
	GPIO_GROUP(GPIOY_9, EE_OFF),
	GPIO_GROUP(GPIOY_10, EE_OFF),
	GPIO_GROUP(GPIOY_11, EE_OFF),
	GPIO_GROUP(GPIOY_12, EE_OFF),
	GPIO_GROUP(GPIOY_13, EE_OFF),
	GPIO_GROUP(GPIOY_14, EE_OFF),
	GPIO_GROUP(GPIOY_15, EE_OFF),
	GPIO_GROUP(GPIOY_16, EE_OFF),

	GPIO_GROUP(GPIOX_0, EE_OFF),
	GPIO_GROUP(GPIOX_1, EE_OFF),
	GPIO_GROUP(GPIOX_2, EE_OFF),
	GPIO_GROUP(GPIOX_3, EE_OFF),
	GPIO_GROUP(GPIOX_4, EE_OFF),
	GPIO_GROUP(GPIOX_5, EE_OFF),
	GPIO_GROUP(GPIOX_6, EE_OFF),
	GPIO_GROUP(GPIOX_7, EE_OFF),
	GPIO_GROUP(GPIOX_8, EE_OFF),
	GPIO_GROUP(GPIOX_9, EE_OFF),
	GPIO_GROUP(GPIOX_10, EE_OFF),
	GPIO_GROUP(GPIOX_11, EE_OFF),
	GPIO_GROUP(GPIOX_12, EE_OFF),
	GPIO_GROUP(GPIOX_13, EE_OFF),
	GPIO_GROUP(GPIOX_14, EE_OFF),
	GPIO_GROUP(GPIOX_15, EE_OFF),
	GPIO_GROUP(GPIOX_16, EE_OFF),
	GPIO_GROUP(GPIOX_17, EE_OFF),
	GPIO_GROUP(GPIOX_18, EE_OFF),
	GPIO_GROUP(GPIOX_19, EE_OFF),
	GPIO_GROUP(GPIOX_20, EE_OFF),
	GPIO_GROUP(GPIOX_21, EE_OFF),
	GPIO_GROUP(GPIOX_22, EE_OFF),

	GPIO_GROUP(GPIOCLK_0, EE_OFF),
	GPIO_GROUP(GPIOCLK_1, EE_OFF),
	GPIO_GROUP(GPIOCLK_2, EE_OFF),
	GPIO_GROUP(GPIOCLK_3, EE_OFF),

	GPIO_GROUP(GPIO_TEST_N, EE_OFF),

	/* Bank X */
	GROUP(sdio_d0,		8,	5),
	GROUP(sdio_d1,		8,	4),
	GROUP(sdio_d2,		8,	3),
	GROUP(sdio_d3,		8,	2),
	GROUP(sdio_cmd,		8,	1),
	GROUP(sdio_clk,		8,	0),
	GROUP(sdio_irq,		8,	11),
	GROUP(uart_tx_a,	4,	13),
	GROUP(uart_rx_a,	4,	12),
	GROUP(uart_cts_a,	4,	11),
	GROUP(uart_rts_a,	4,	10),
	GROUP(pwm_a_x,		3,	17),
	GROUP(pwm_e,		2,	30),
	GROUP(pwm_f_x,		3,	18),

	/* Bank Y */
	GROUP(uart_cts_c,	1,	19),
	GROUP(uart_rts_c,	1,	18),
	GROUP(uart_tx_c,	1,	17),
	GROUP(uart_rx_c,	1,	16),
	GROUP(pwm_a_y,		1,	21),
	GROUP(pwm_f_y,		1,	20),

	/* Bank Z */
	GROUP(eth_mdio,		6,	1),
	GROUP(eth_mdc,		6,	0),
	GROUP(eth_clk_rx_clk,	6,	13),
	GROUP(eth_rx_dv,	6,	12),
	GROUP(eth_rxd0,		6,	11),
	GROUP(eth_rxd1,		6,	10),
	GROUP(eth_rxd2,		6,	9),
	GROUP(eth_rxd3,		6,	8),
	GROUP(eth_rgmii_tx_clk,	6,	7),
	GROUP(eth_tx_en,	6,	6),
	GROUP(eth_txd0,		6,	5),
	GROUP(eth_txd1,		6,	4),
	GROUP(eth_txd2,		6,	3),
	GROUP(eth_txd3,		6,	2),

	/* Bank DV */
	GROUP(uart_tx_b,	2,	29),
	GROUP(uart_rx_b,	2,	28),
	GROUP(uart_cts_b,	2,	27),
	GROUP(uart_rts_b,	2,	26),
	GROUP(pwm_b,		3,	21),
	GROUP(pwm_d,		3,	20),
	GROUP(i2c_sck_a,	7,	27),
	GROUP(i2c_sda_a,	7,	26),
	GROUP(i2c_sck_b,	7,	25),
	GROUP(i2c_sda_b,	7,	24),
	GROUP(i2c_sck_c,	7,	23),
	GROUP(i2c_sda_c,	7,	22),

	/* Bank BOOT */
	GROUP(emmc_nand_d07,	4,	30),
	GROUP(emmc_clk,		4,	18),
	GROUP(emmc_cmd,		4,	19),
	GROUP(emmc_ds,		4,	31),
	GROUP(nor_d,		5,	1),
	GROUP(nor_q,		5,	3),
	GROUP(nor_c,		5,	2),
	GROUP(nor_cs,		5,	0),
	GROUP(nand_ce0,		4,	26),
	GROUP(nand_ce1,		4,	27),
	GROUP(nand_rb0,		4,	25),
	GROUP(nand_ale,		4,	24),
	GROUP(nand_cle,		4,	23),
	GROUP(nand_wen_clk,	4,	22),
	GROUP(nand_ren_wr,	4,	21),
	GROUP(nand_dqs,		4,	20),

	/* Bank CARD */
	GROUP(sdcard_d1,	2,	14),
	GROUP(sdcard_d0,	2,	15),
	GROUP(sdcard_d3,	2,	12),
	GROUP(sdcard_d2,	2,	13),
	GROUP(sdcard_cmd,	2,	10),
	GROUP(sdcard_clk,	2,	11),
};

static struct meson_pmx_group meson_gxbb_aobus_groups[] = {
	GPIO_GROUP(GPIOAO_0, 0),
	GPIO_GROUP(GPIOAO_1, 0),
	GPIO_GROUP(GPIOAO_2, 0),
	GPIO_GROUP(GPIOAO_3, 0),
	GPIO_GROUP(GPIOAO_4, 0),
	GPIO_GROUP(GPIOAO_5, 0),
	GPIO_GROUP(GPIOAO_6, 0),
	GPIO_GROUP(GPIOAO_7, 0),
	GPIO_GROUP(GPIOAO_8, 0),
	GPIO_GROUP(GPIOAO_9, 0),
	GPIO_GROUP(GPIOAO_10, 0),
	GPIO_GROUP(GPIOAO_11, 0),
	GPIO_GROUP(GPIOAO_12, 0),
	GPIO_GROUP(GPIOAO_13, 0),

	/* bank AO */
	GROUP(uart_tx_ao_b,	0,	26),
	GROUP(uart_rx_ao_b,	0,	25),
	GROUP(uart_tx_ao_a,	0,	12),
	GROUP(uart_rx_ao_a,	0,	11),
	GROUP(uart_cts_ao_a,	0,	10),
	GROUP(uart_rts_ao_a,	0,	9),
	GROUP(uart_cts_ao_b,	0,	8),
	GROUP(uart_rts_ao_b,	0,	7),
	GROUP(i2c_sck_ao,	0,	6),
	GROUP(i2c_sda_ao,	0,	5),
	GROUP(i2c_slave_sck_ao, 0,	2),
	GROUP(i2c_slave_sda_ao, 0,	1),
	GROUP(remote_input_ao,	0,	0),
	GROUP(pwm_ao_a_3,	0,	22),
	GROUP(pwm_ao_a_6,	0,	18),
	GROUP(pwm_ao_a_12,	0,	17),
	GROUP(pwm_ao_b,		0,	3),
};

static const char * const gpio_periphs_groups[] = {
	"GPIOZ_0", "GPIOZ_1", "GPIOZ_2", "GPIOZ_3", "GPIOZ_4",
	"GPIOZ_5", "GPIOZ_6", "GPIOZ_7", "GPIOZ_8", "GPIOZ_9",
	"GPIOZ_10", "GPIOZ_11", "GPIOZ_12", "GPIOZ_13", "GPIOZ_14",
	"GPIOZ_15",

	"GPIOH_0", "GPIOH_1", "GPIOH_2", "GPIOH_3",

	"BOOT_0", "BOOT_1", "BOOT_2", "BOOT_3", "BOOT_4",
	"BOOT_5", "BOOT_6", "BOOT_7", "BOOT_8", "BOOT_9",
	"BOOT_10", "BOOT_11", "BOOT_12", "BOOT_13", "BOOT_14",
	"BOOT_15", "BOOT_16", "BOOT_17",

	"CARD_0", "CARD_1", "CARD_2", "CARD_3", "CARD_4",
	"CARD_5", "CARD_6",

	"GPIODV_0", "GPIODV_1", "GPIODV_2", "GPIODV_3", "GPIODV_4",
	"GPIODV_5", "GPIODV_6", "GPIODV_7", "GPIODV_8", "GPIODV_9",
	"GPIODV_10", "GPIODV_11", "GPIODV_12", "GPIODV_13", "GPIODV_14",
	"GPIODV_15", "GPIODV_16", "GPIODV_17", "GPIODV_18", "GPIODV_19",
	"GPIODV_20", "GPIODV_21", "GPIODV_22", "GPIODV_23", "GPIODV_24",
	"GPIODV_25", "GPIODV_26", "GPIODV_27", "GPIODV_28", "GPIODV_29",

	"GPIOY_0", "GPIOY_1", "GPIOY_2", "GPIOY_3", "GPIOY_4",
	"GPIOY_5", "GPIOY_6", "GPIOY_7", "GPIOY_8", "GPIOY_9",
	"GPIOY_10", "GPIOY_11", "GPIOY_12", "GPIOY_13", "GPIOY_14",
	"GPIOY_15", "GPIOY_16",

	"GPIOX_0", "GPIOX_1", "GPIOX_2", "GPIOX_3", "GPIOX_4",
	"GPIOX_5", "GPIOX_6", "GPIOX_7", "GPIOX_8", "GPIOX_9",
	"GPIOX_10", "GPIOX_11", "GPIOX_12", "GPIOX_13", "GPIOX_14",
	"GPIOX_15", "GPIOX_16", "GPIOX_17", "GPIOX_18", "GPIOX_19",
	"GPIOX_20", "GPIOX_21", "GPIOX_22",

	"GPIO_TEST_N",
};

static const char * const emmc_groups[] = {
	"emmc_nand_d07", "emmc_clk", "emmc_cmd", "emmc_ds",
};

static const char * const nor_groups[] = {
	"nor_d", "nor_q", "nor_c", "nor_cs",
};

static const char * const sdcard_groups[] = {
	"sdcard_d0", "sdcard_d1", "sdcard_d2", "sdcard_d3",
	"sdcard_cmd", "sdcard_clk",
};

static const char * const sdio_groups[] = {
	"sdio_d0", "sdio_d1", "sdio_d2", "sdio_d3",
	"sdio_cmd", "sdio_clk", "sdio_irq",
};

static const char * const nand_groups[] = {
	"nand_ce0", "nand_ce1", "nand_rb0", "nand_ale", "nand_cle",
	"nand_wen_clk", "nand_ren_wr", "nand_dqs",
};

static const char * const uart_a_groups[] = {
	"uart_tx_a", "uart_rx_a", "uart_cts_a", "uart_rts_a",
};

static const char * const uart_b_groups[] = {
	"uart_tx_b", "uart_rx_b", "uart_cts_b", "uart_rts_b",
};

static const char * const uart_c_groups[] = {
	"uart_tx_c", "uart_rx_c", "uart_cts_c", "uart_rts_c",
};

static const char * const i2c_a_groups[] = {
	"i2c_sck_a", "i2c_sda_a",
};

static const char * const i2c_b_groups[] = {
	"i2c_sck_b", "i2c_sda_b",
};

static const char * const i2c_c_groups[] = {
	"i2c_sck_c", "i2c_sda_c",
};

static const char * const eth_groups[] = {
	"eth_mdio", "eth_mdc", "eth_clk_rx_clk", "eth_rx_dv",
	"eth_rxd0", "eth_rxd1", "eth_rxd2", "eth_rxd3",
	"eth_rgmii_tx_clk", "eth_tx_en",
	"eth_txd0", "eth_txd1", "eth_txd2", "eth_txd3",
};

static const char * const pwm_a_x_groups[] = {
	"pwm_a_x",
};

static const char * const pwm_a_y_groups[] = {
	"pwm_a_y",
};

static const char * const pwm_b_groups[] = {
	"pwm_b",
};

static const char * const pwm_d_groups[] = {
	"pwm_d",
};

static const char * const pwm_e_groups[] = {
	"pwm_e",
};

static const char * const pwm_f_x_groups[] = {
	"pwm_f_x",
};

static const char * const pwm_f_y_groups[] = {
	"pwm_f_y",
};

static const char * const gpio_aobus_groups[] = {
	"GPIOAO_0", "GPIOAO_1", "GPIOAO_2", "GPIOAO_3", "GPIOAO_4",
	"GPIOAO_5", "GPIOAO_6", "GPIOAO_7", "GPIOAO_8", "GPIOAO_9",
	"GPIOAO_10", "GPIOAO_11", "GPIOAO_12", "GPIOAO_13",
};

static const char * const uart_ao_groups[] = {
	"uart_tx_ao_a", "uart_rx_ao_a", "uart_cts_ao_a", "uart_rts_ao_a",
};

static const char * const uart_ao_b_groups[] = {
	"uart_tx_ao_b", "uart_rx_ao_b", "uart_cts_ao_b", "uart_rts_ao_b",
};

static const char * const i2c_ao_groups[] = {
	"i2c_sdk_ao", "i2c_sda_ao",
};

static const char * const i2c_slave_ao_groups[] = {
	"i2c_slave_sdk_ao", "i2c_slave_sda_ao",
};

static const char * const remote_input_ao_groups[] = {
	"remote_input_ao",
};

static const char * const pwm_ao_a_3_groups[] = {
	"pwm_ao_a_3",
};

static const char * const pwm_ao_a_6_groups[] = {
	"pwm_ao_a_6",
};

static const char * const pwm_ao_a_12_groups[] = {
	"pwm_ao_a_12",
};

static const char * const pwm_ao_b_groups[] = {
	"pwm_ao_b",
};

static struct meson_pmx_func meson_gxbb_periphs_functions[] = {
	FUNCTION(gpio_periphs),
	FUNCTION(emmc),
	FUNCTION(nor),
	FUNCTION(sdcard),
	FUNCTION(sdio),
	FUNCTION(nand),
	FUNCTION(uart_a),
	FUNCTION(uart_b),
	FUNCTION(uart_c),
	FUNCTION(i2c_a),
	FUNCTION(i2c_b),
	FUNCTION(i2c_c),
	FUNCTION(eth),
	FUNCTION(pwm_a_x),
	FUNCTION(pwm_a_y),
	FUNCTION(pwm_b),
	FUNCTION(pwm_d),
	FUNCTION(pwm_e),
	FUNCTION(pwm_f_x),
	FUNCTION(pwm_f_y),
};

static struct meson_pmx_func meson_gxbb_aobus_functions[] = {
	FUNCTION(gpio_aobus),
	FUNCTION(uart_ao),
	FUNCTION(uart_ao_b),
	FUNCTION(i2c_ao),
	FUNCTION(i2c_slave_ao),
	FUNCTION(remote_input_ao),
	FUNCTION(pwm_ao_a_3),
	FUNCTION(pwm_ao_a_6),
	FUNCTION(pwm_ao_a_12),
	FUNCTION(pwm_ao_b),
};

static struct meson_bank meson_gxbb_periphs_banks[] = {
	/*   name    first                      last                    pullen  pull    dir     out     in  */
	BANK("X",    PIN(GPIOX_0, EE_OFF),	PIN(GPIOX_22, EE_OFF),  4,  0,  4,  0,  12, 0,  13, 0,  14, 0),
	BANK("Y",    PIN(GPIOY_0, EE_OFF),	PIN(GPIOY_16, EE_OFF),  1,  0,  1,  0,  3,  0,  4,  0,  5,  0),
	BANK("DV",   PIN(GPIODV_0, EE_OFF),	PIN(GPIODV_29, EE_OFF), 0,  0,  0,  0,  0,  0,  1,  0,  2,  0),
	BANK("H",    PIN(GPIOH_0, EE_OFF),	PIN(GPIOH_3, EE_OFF),   1, 20,  1, 20,  3, 20,  4, 20,  5, 20),
	BANK("Z",    PIN(GPIOZ_0, EE_OFF),	PIN(GPIOZ_15, EE_OFF),  3,  0,  3,  0,  9,  0,  10, 0, 11,  0),
	BANK("CARD", PIN(CARD_0, EE_OFF),	PIN(CARD_6, EE_OFF),    2, 20,  2, 20,  6, 20,  7, 20,  8, 20),
	BANK("BOOT", PIN(BOOT_0, EE_OFF),	PIN(BOOT_17, EE_OFF),   2,  0,  2,  0,  6,  0,  7,  0,  8,  0),
	BANK("CLK",  PIN(GPIOCLK_0, EE_OFF),	PIN(GPIOCLK_3, EE_OFF), 3, 28,  3, 28,  9, 28, 10, 28, 11, 28),
};

static struct meson_bank meson_gxbb_aobus_banks[] = {
	/*   name    first              last               pullen  pull    dir     out     in  */
	BANK("AO",   PIN(GPIOAO_0, 0),  PIN(GPIOAO_13, 0), 0,  0,  0, 16,  0,  0,  0, 16,  1,  0),
};

struct meson_pinctrl_data meson_gxbb_periphs_pinctrl_data = {
	.name		= "periphs-banks",
	.pin_base	= 14,
	.pins		= meson_gxbb_periphs_pins,
	.groups		= meson_gxbb_periphs_groups,
	.funcs		= meson_gxbb_periphs_functions,
	.banks		= meson_gxbb_periphs_banks,
	.num_pins	= ARRAY_SIZE(meson_gxbb_periphs_pins),
	.num_groups	= ARRAY_SIZE(meson_gxbb_periphs_groups),
	.num_funcs	= ARRAY_SIZE(meson_gxbb_periphs_functions),
	.num_banks	= ARRAY_SIZE(meson_gxbb_periphs_banks),
};

struct meson_pinctrl_data meson_gxbb_aobus_pinctrl_data = {
	.name		= "aobus-banks",
	.pin_base	= 0,
	.pins		= meson_gxbb_aobus_pins,
	.groups		= meson_gxbb_aobus_groups,
	.funcs		= meson_gxbb_aobus_functions,
	.banks		= meson_gxbb_aobus_banks,
	.num_pins	= ARRAY_SIZE(meson_gxbb_aobus_pins),
	.num_groups	= ARRAY_SIZE(meson_gxbb_aobus_groups),
	.num_funcs	= ARRAY_SIZE(meson_gxbb_aobus_functions),
	.num_banks	= ARRAY_SIZE(meson_gxbb_aobus_banks),
};
