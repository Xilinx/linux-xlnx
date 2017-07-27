/*
 * ZynqMP pin controller
 *
 *  Copyright (C) 2017 Xilinx
 *
 *  Chirag Parekh <chirag.parekh@xilinx.com>
 *
 * This file is based on the drivers/pinctrl/pinctrl-zynq.c
 * (c) 2014 Xilinx, Sören Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/soc/xilinx/zynqmp/firmware.h>
#include <linux/of_address.h>
#include <dt-bindings/pinctrl/pinctrl-zynqmp.h>
#include "pinctrl-utils.h"
#include "core.h"

#define ZYNQMP_NUM_MIOS            78

#define ZYNQMP_PINMUX_MUX_SHIFT    1
#define ZYNQMP_PINMUX_MUX_MASK     0x7f

#define ZYNQMP_IOSTD_BIT_MASK     0x01

/**
 * struct zynqmp_pinctrl - driver data
 * @pctrl:              Pinctrl device
 * @groups:             Pingroups
 * @ngroups:            Number of @groups
 * @funcs:              Pinmux functions
 * @nfuncs:             Number of @funcs
 * @iouaddr:            Base address of IOU SLCR
 */
struct zynqmp_pinctrl {
	struct pinctrl_dev *pctrl;
	const struct zynqmp_pctrl_group *groups;
	unsigned int ngroups;
	const struct zynqmp_pinmux_function *funcs;
	unsigned int nfuncs;
	u32 iouaddr;
};

struct zynqmp_pctrl_group {
	const char *name;
	const unsigned int *pins;
	const unsigned int npins;
};

/**
 * struct zynqmp_pinmux_function - a pinmux function
 * @name:       Name of the pinmux function
 * @groups:     List of pingroups for this function
 * @ngroups:    Number of entries in @groups
 * @mux_val:    Selector for this function
 */
struct zynqmp_pinmux_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	unsigned int mux_val;
};

enum zynqmp_pinmux_functions {
	ZYNQMP_PMUX_can0,
	ZYNQMP_PMUX_can1,
	ZYNQMP_PMUX_ethernet0,
	ZYNQMP_PMUX_ethernet1,
	ZYNQMP_PMUX_ethernet2,
	ZYNQMP_PMUX_ethernet3,
	ZYNQMP_PMUX_gemtsu0,
	ZYNQMP_PMUX_gpio0,
	ZYNQMP_PMUX_i2c0,
	ZYNQMP_PMUX_i2c1,
	ZYNQMP_PMUX_mdio0,
	ZYNQMP_PMUX_mdio1,
	ZYNQMP_PMUX_mdio2,
	ZYNQMP_PMUX_mdio3,
	ZYNQMP_PMUX_qspi0,
	ZYNQMP_PMUX_qspi_fbclk,
	ZYNQMP_PMUX_qspi_ss,
	ZYNQMP_PMUX_spi0,
	ZYNQMP_PMUX_spi1,
	ZYNQMP_PMUX_spi0_ss,
	ZYNQMP_PMUX_spi1_ss,
	ZYNQMP_PMUX_sdio0,
	ZYNQMP_PMUX_sdio0_pc,
	ZYNQMP_PMUX_sdio0_cd,
	ZYNQMP_PMUX_sdio0_wp,
	ZYNQMP_PMUX_sdio1,
	ZYNQMP_PMUX_sdio1_pc,
	ZYNQMP_PMUX_sdio1_cd,
	ZYNQMP_PMUX_sdio1_wp,
	ZYNQMP_PMUX_nand0,
	ZYNQMP_PMUX_nand0_ce,
	ZYNQMP_PMUX_nand0_rb,
	ZYNQMP_PMUX_nand0_dqs,
	ZYNQMP_PMUX_ttc0_clk,
	ZYNQMP_PMUX_ttc0_wav,
	ZYNQMP_PMUX_ttc1_clk,
	ZYNQMP_PMUX_ttc1_wav,
	ZYNQMP_PMUX_ttc2_clk,
	ZYNQMP_PMUX_ttc2_wav,
	ZYNQMP_PMUX_ttc3_clk,
	ZYNQMP_PMUX_ttc3_wav,
	ZYNQMP_PMUX_uart0,
	ZYNQMP_PMUX_uart1,
	ZYNQMP_PMUX_usb0,
	ZYNQMP_PMUX_usb1,
	ZYNQMP_PMUX_swdt0_clk,
	ZYNQMP_PMUX_swdt0_rst,
	ZYNQMP_PMUX_swdt1_clk,
	ZYNQMP_PMUX_swdt1_rst,
	ZYNQMP_PMUX_pmu0,
	ZYNQMP_PMUX_pcie0,
	ZYNQMP_PMUX_csu0,
	ZYNQMP_PMUX_dpaux0,
	ZYNQMP_PMUX_pjtag0,
	ZYNQMP_PMUX_trace0,
	ZYNQMP_PMUX_trace0_clk,
	ZYNQMP_PMUX_testscan0,
	ZYNQMP_PMUX_MAX_FUNC
};

static const struct pinctrl_pin_desc zynqmp_pins[] = {
	PINCTRL_PIN(0,   "MIO0"),
	PINCTRL_PIN(1,   "MIO1"),
	PINCTRL_PIN(2,   "MIO2"),
	PINCTRL_PIN(3,   "MIO3"),
	PINCTRL_PIN(4,   "MIO4"),
	PINCTRL_PIN(5,   "MIO5"),
	PINCTRL_PIN(6,   "MIO6"),
	PINCTRL_PIN(7,   "MIO7"),
	PINCTRL_PIN(8,   "MIO8"),
	PINCTRL_PIN(9,   "MIO9"),
	PINCTRL_PIN(10, "MIO10"),
	PINCTRL_PIN(11, "MIO11"),
	PINCTRL_PIN(12, "MIO12"),
	PINCTRL_PIN(13, "MIO13"),
	PINCTRL_PIN(14, "MIO14"),
	PINCTRL_PIN(15, "MIO15"),
	PINCTRL_PIN(16, "MIO16"),
	PINCTRL_PIN(17, "MIO17"),
	PINCTRL_PIN(18, "MIO18"),
	PINCTRL_PIN(19, "MIO19"),
	PINCTRL_PIN(20, "MIO20"),
	PINCTRL_PIN(21, "MIO21"),
	PINCTRL_PIN(22, "MIO22"),
	PINCTRL_PIN(23, "MIO23"),
	PINCTRL_PIN(24, "MIO24"),
	PINCTRL_PIN(25, "MIO25"),
	PINCTRL_PIN(26, "MIO26"),
	PINCTRL_PIN(27, "MIO27"),
	PINCTRL_PIN(28, "MIO28"),
	PINCTRL_PIN(29, "MIO29"),
	PINCTRL_PIN(30, "MIO30"),
	PINCTRL_PIN(31, "MIO31"),
	PINCTRL_PIN(32, "MIO32"),
	PINCTRL_PIN(33, "MIO33"),
	PINCTRL_PIN(34, "MIO34"),
	PINCTRL_PIN(35, "MIO35"),
	PINCTRL_PIN(36, "MIO36"),
	PINCTRL_PIN(37, "MIO37"),
	PINCTRL_PIN(38, "MIO38"),
	PINCTRL_PIN(39, "MIO39"),
	PINCTRL_PIN(40, "MIO40"),
	PINCTRL_PIN(41, "MIO41"),
	PINCTRL_PIN(42, "MIO42"),
	PINCTRL_PIN(43, "MIO43"),
	PINCTRL_PIN(44, "MIO44"),
	PINCTRL_PIN(45, "MIO45"),
	PINCTRL_PIN(46, "MIO46"),
	PINCTRL_PIN(47, "MIO47"),
	PINCTRL_PIN(48, "MIO48"),
	PINCTRL_PIN(49, "MIO49"),
	PINCTRL_PIN(50, "MIO50"),
	PINCTRL_PIN(51, "MIO51"),
	PINCTRL_PIN(52, "MIO52"),
	PINCTRL_PIN(53, "MIO53"),
	PINCTRL_PIN(54, "MIO54"),
	PINCTRL_PIN(55, "MIO55"),
	PINCTRL_PIN(56, "MIO56"),
	PINCTRL_PIN(57, "MIO57"),
	PINCTRL_PIN(58, "MIO58"),
	PINCTRL_PIN(59, "MIO59"),
	PINCTRL_PIN(60, "MIO60"),
	PINCTRL_PIN(61, "MIO61"),
	PINCTRL_PIN(62, "MIO62"),
	PINCTRL_PIN(63, "MIO63"),
	PINCTRL_PIN(64, "MIO64"),
	PINCTRL_PIN(65, "MIO65"),
	PINCTRL_PIN(66, "MIO66"),
	PINCTRL_PIN(67, "MIO67"),
	PINCTRL_PIN(68, "MIO68"),
	PINCTRL_PIN(69, "MIO69"),
	PINCTRL_PIN(70, "MIO70"),
	PINCTRL_PIN(71, "MIO71"),
	PINCTRL_PIN(72, "MIO72"),
	PINCTRL_PIN(73, "MIO73"),
	PINCTRL_PIN(74, "MIO74"),
	PINCTRL_PIN(75, "MIO75"),
	PINCTRL_PIN(76, "MIO76"),
	PINCTRL_PIN(77, "MIO77"),
};

/* pin groups */
static const unsigned int ethernet0_0_pins[] = {26, 27, 28, 29, 30, 31, 32, 33,
						34, 35, 36, 37};
static const unsigned int ethernet1_0_pins[] = {38, 39, 40, 41, 42, 43, 44, 45,
						46, 47, 48, 49};
static const unsigned int ethernet2_0_pins[] = {52, 53, 54, 55, 56, 57, 58, 59,
						60, 61, 62, 63};
static const unsigned int ethernet3_0_pins[] = {64, 65, 66, 67, 68, 69, 70, 71,
						72, 73,	74, 75};

static const unsigned int gemtsu0_0_pins[] = {26};
static const unsigned int gemtsu0_1_pins[] = {50};
static const unsigned int gemtsu0_2_pins[] = {51};

static const unsigned int mdio0_0_pins[] = {76, 77};
static const unsigned int mdio1_0_pins[] = {50, 51};
static const unsigned int mdio1_1_pins[] = {76, 77};
static const unsigned int mdio2_0_pins[] = {76, 77};
static const unsigned int mdio3_0_pins[] = {76, 77};

static const unsigned int qspi0_0_pins[] = {0, 1, 2, 3, 4, 8, 9, 10, 11, 12};
static const unsigned int qspi_ss_pins[] = {5, 7};
static const unsigned int qspi_fbclk_pins[] = {6};

static const unsigned int spi0_0_pins[] = {0, 4, 5};
static const unsigned int spi0_0_ss0_pins[] = {3};
static const unsigned int spi0_0_ss1_pins[] = {2};
static const unsigned int spi0_0_ss2_pins[] = {1};
static const unsigned int spi0_1_pins[] = {12, 16, 17};
static const unsigned int spi0_1_ss0_pins[] = {15};
static const unsigned int spi0_1_ss1_pins[] = {14};
static const unsigned int spi0_1_ss2_pins[] = {13};
static const unsigned int spi0_2_pins[] = {26, 30, 31};
static const unsigned int spi0_2_ss0_pins[] = {29};
static const unsigned int spi0_2_ss1_pins[] = {28};
static const unsigned int spi0_2_ss2_pins[] = {27};
static const unsigned int spi0_3_pins[] = {38, 42, 43};
static const unsigned int spi0_3_ss0_pins[] = {41};
static const unsigned int spi0_3_ss1_pins[] = {40};
static const unsigned int spi0_3_ss2_pins[] = {39};
static const unsigned int spi0_4_pins[] = {52, 56, 57};
static const unsigned int spi0_4_ss0_pins[] = {55};
static const unsigned int spi0_4_ss1_pins[] = {54};
static const unsigned int spi0_4_ss2_pins[] = {53};
static const unsigned int spi0_5_pins[] = {64, 68, 69};
static const unsigned int spi0_5_ss0_pins[] = {67};
static const unsigned int spi0_5_ss1_pins[] = {66};
static const unsigned int spi0_5_ss2_pins[] = {65};
static const unsigned int spi1_0_pins[] = {6, 10, 11};
static const unsigned int spi1_0_ss0_pins[] = {9};
static const unsigned int spi1_0_ss1_pins[] = {8};
static const unsigned int spi1_0_ss2_pins[] = {7};
static const unsigned int spi1_1_pins[] = {18, 19, 20, 21, 22, 23};
static const unsigned int spi1_1_ss0_pins[] = {21};
static const unsigned int spi1_1_ss1_pins[] = {20};
static const unsigned int spi1_1_ss2_pins[] = {19};
static const unsigned int spi1_2_pins[] = {32, 36, 37};
static const unsigned int spi1_2_ss0_pins[] = {35};
static const unsigned int spi1_2_ss1_pins[] = {34};
static const unsigned int spi1_2_ss2_pins[] = {33};
static const unsigned int spi1_3_pins[] = {44, 48, 49};
static const unsigned int spi1_3_ss0_pins[] = {47};
static const unsigned int spi1_3_ss1_pins[] = {46};
static const unsigned int spi1_3_ss2_pins[] = {45};
static const unsigned int spi1_4_pins[] = {58, 62, 63};
static const unsigned int spi1_4_ss0_pins[] = {61};
static const unsigned int spi1_4_ss1_pins[] = {60};
static const unsigned int spi1_4_ss2_pins[] = {59};
static const unsigned int spi1_5_pins[] = {70, 74, 75};
static const unsigned int spi1_5_ss0_pins[] = {73};
static const unsigned int spi1_5_ss1_pins[] = {72};
static const unsigned int spi1_5_ss2_pins[] = {71};

/* NOTE:
 * sdio supports 1bit, 4bit or 8bit data lines.
 * Hence the pins for this are classified into 3 groups:
 * sdiox_x_pins:        8bit data lines
 * sdiox_4bit_x_x_pins: 4bit data lines
 * sdiox_1bit_x_x_pins: 1bit data lines
 *
 * As per the number of data lines to be used one of the groups from this
 * has to be specified in device tree.
 */
static const unsigned int sdio0_0_pins[] = {13, 14, 15, 16, 17, 18, 19, 20,
						 21, 22};
static const unsigned int sdio0_4bit_0_0_pins[] = {13, 14, 15, 16, 21, 22};
static const unsigned int sdio0_4bit_0_1_pins[] = {17, 18, 19, 20, 21, 22};
static const unsigned int sdio0_1bit_0_0_pins[] = {13, 21, 22};
static const unsigned int sdio0_1bit_0_1_pins[] = {14, 21, 22};
static const unsigned int sdio0_1bit_0_2_pins[] = {15, 21, 22};
static const unsigned int sdio0_1bit_0_3_pins[] = {16, 21, 22};
static const unsigned int sdio0_1bit_0_4_pins[] = {17, 21, 22};
static const unsigned int sdio0_1bit_0_5_pins[] = {18, 21, 22};
static const unsigned int sdio0_1bit_0_6_pins[] = {19, 21, 22};
static const unsigned int sdio0_1bit_0_7_pins[] = {20, 21, 22};
static const unsigned int sdio0_0_pc_pins[] = {23};
static const unsigned int sdio0_0_cd_pins[] = {24};
static const unsigned int sdio0_0_wp_pins[] = {25};
static const unsigned int sdio0_1_pins[] = {38, 40, 41, 42, 43, 44, 45, 46,
						 47, 48};
static const unsigned int sdio0_4bit_1_0_pins[] = {38, 40, 41, 42, 43, 44};
static const unsigned int sdio0_4bit_1_1_pins[] = {38, 40, 45, 46, 47, 48};
static const unsigned int sdio0_1bit_1_0_pins[] = {38, 40, 41};
static const unsigned int sdio0_1bit_1_1_pins[] = {38, 40, 42};
static const unsigned int sdio0_1bit_1_2_pins[] = {38, 40, 43};
static const unsigned int sdio0_1bit_1_3_pins[] = {38, 40, 44};
static const unsigned int sdio0_1bit_1_4_pins[] = {38, 40, 45};
static const unsigned int sdio0_1bit_1_5_pins[] = {38, 40, 46};
static const unsigned int sdio0_1bit_1_6_pins[] = {38, 40, 47};
static const unsigned int sdio0_1bit_1_7_pins[] = {38, 40, 48};
static const unsigned int sdio0_1_pc_pins[] = {49};
static const unsigned int sdio0_1_cd_pins[] = {39};
static const unsigned int sdio0_1_wp_pins[] = {50};
static const unsigned int sdio0_2_pins[] = {64, 66, 67, 68, 69, 70, 71, 72,
						 73, 74};
static const unsigned int sdio0_4bit_2_0_pins[] = {64, 66, 67, 68, 69, 70};
static const unsigned int sdio0_4bit_2_1_pins[] = {64, 66, 71, 72, 73, 74};
static const unsigned int sdio0_1bit_2_0_pins[] = {64, 66, 67};
static const unsigned int sdio0_1bit_2_1_pins[] = {64, 66, 68};
static const unsigned int sdio0_1bit_2_2_pins[] = {64, 66, 69};
static const unsigned int sdio0_1bit_2_3_pins[] = {64, 66, 70};
static const unsigned int sdio0_1bit_2_4_pins[] = {64, 66, 71};
static const unsigned int sdio0_1bit_2_5_pins[] = {64, 66, 72};
static const unsigned int sdio0_1bit_2_6_pins[] = {64, 66, 73};
static const unsigned int sdio0_1bit_2_7_pins[] = {64, 66, 74};
static const unsigned int sdio0_2_pc_pins[] = {75};
static const unsigned int sdio0_2_cd_pins[] = {65};
static const unsigned int sdio0_2_wp_pins[] = {76};
static const unsigned int sdio1_0_pins[] = {39, 40, 41, 42, 46, 47, 48, 49,
						 50, 51};
static const unsigned int sdio1_4bit_0_0_pins[] = {39, 40, 41, 42, 50, 51};
static const unsigned int sdio1_4bit_0_1_pins[] = {46, 47, 48, 49, 50, 51};
static const unsigned int sdio1_1bit_0_0_pins[] = {39, 50, 51};
static const unsigned int sdio1_1bit_0_1_pins[] = {40, 50, 51};
static const unsigned int sdio1_1bit_0_2_pins[] = {41, 50, 51};
static const unsigned int sdio1_1bit_0_3_pins[] = {42, 50, 51};
static const unsigned int sdio1_1bit_0_4_pins[] = {46, 50, 51};
static const unsigned int sdio1_1bit_0_5_pins[] = {47, 50, 51};
static const unsigned int sdio1_1bit_0_6_pins[] = {48, 50, 51};
static const unsigned int sdio1_1bit_0_7_pins[] = {49, 50, 51};
static const unsigned int sdio1_0_pc_pins[] = {43};
static const unsigned int sdio1_0_cd_pins[] = {45};
static const unsigned int sdio1_0_wp_pins[] = {44};
static const unsigned int sdio1_4bit_1_0_pins[] = {71, 72, 73, 74, 75, 76};
static const unsigned int sdio1_1bit_1_0_pins[] = {71, 75, 76};
static const unsigned int sdio1_1bit_1_1_pins[] = {72, 75, 76};
static const unsigned int sdio1_1bit_1_2_pins[] = {73, 75, 76};
static const unsigned int sdio1_1bit_1_3_pins[] = {74, 75, 76};
static const unsigned int sdio1_1_pc_pins[] = {70};
static const unsigned int sdio1_1_cd_pins[] = {77};
static const unsigned int sdio1_1_wp_pins[] = {69};

static const unsigned int nand0_0_pins[] = {13, 14, 15, 16, 17, 18, 19, 20,
						21, 22, 23, 24, 25};
static const unsigned int nand0_0_ce_pins[] = {9};
static const unsigned int nand0_0_rb_pins[] = {10, 11};
static const unsigned int nand0_0_dqs_pins[] = {12};
static const unsigned int nand0_1_ce_pins[] = {26};
static const unsigned int nand0_1_rb_pins[] = {27, 28};
static const unsigned int nand0_1_dqs_pins[] = {32};

static const unsigned int can0_0_pins[] = {2, 3};
static const unsigned int can0_1_pins[] = {6, 7};
static const unsigned int can0_2_pins[] = {10, 11};
static const unsigned int can0_3_pins[] = {14, 15};
static const unsigned int can0_4_pins[] = {18, 19};
static const unsigned int can0_5_pins[] = {22, 23};
static const unsigned int can0_6_pins[] = {26, 27};
static const unsigned int can0_7_pins[] = {30, 31};
static const unsigned int can0_8_pins[] = {34, 35};
static const unsigned int can0_9_pins[] = {38, 39};
static const unsigned int can0_10_pins[] = {42, 43};
static const unsigned int can0_11_pins[] = {46, 47};
static const unsigned int can0_12_pins[] = {50, 51};
static const unsigned int can0_13_pins[] = {54, 55};
static const unsigned int can0_14_pins[] = {58, 59};
static const unsigned int can0_15_pins[] = {62, 63};
static const unsigned int can0_16_pins[] = {66, 67};
static const unsigned int can0_17_pins[] = {70, 71};
static const unsigned int can0_18_pins[] = {74, 75};
static const unsigned int can1_0_pins[] = {0, 1};
static const unsigned int can1_1_pins[] = {4, 5};
static const unsigned int can1_2_pins[] = {8, 9};
static const unsigned int can1_3_pins[] = {12, 13};
static const unsigned int can1_4_pins[] = {16, 17};
static const unsigned int can1_5_pins[] = {20, 21};
static const unsigned int can1_6_pins[] = {24, 25};
static const unsigned int can1_7_pins[] = {28, 29};
static const unsigned int can1_8_pins[] = {32, 33};
static const unsigned int can1_9_pins[] = {36, 37};
static const unsigned int can1_10_pins[] = {40, 41};
static const unsigned int can1_11_pins[] = {44, 45};
static const unsigned int can1_12_pins[] = {48, 49};
static const unsigned int can1_13_pins[] = {52, 53};
static const unsigned int can1_14_pins[] = {56, 57};
static const unsigned int can1_15_pins[] = {60, 61};
static const unsigned int can1_16_pins[] = {64, 65};
static const unsigned int can1_17_pins[] = {68, 69};
static const unsigned int can1_18_pins[] = {72, 73};
static const unsigned int can1_19_pins[] = {76, 77};

static const unsigned int uart0_0_pins[] = {2, 3};
static const unsigned int uart0_1_pins[] = {6, 7};
static const unsigned int uart0_2_pins[] = {10, 11};
static const unsigned int uart0_3_pins[] = {14, 15};
static const unsigned int uart0_4_pins[] = {18, 19};
static const unsigned int uart0_5_pins[] = {22, 23};
static const unsigned int uart0_6_pins[] = {26, 27};
static const unsigned int uart0_7_pins[] = {30, 31};
static const unsigned int uart0_8_pins[] = {34, 35};
static const unsigned int uart0_9_pins[] = {38, 39};
static const unsigned int uart0_10_pins[] = {42, 43};
static const unsigned int uart0_11_pins[] = {46, 47};
static const unsigned int uart0_12_pins[] = {50, 51};
static const unsigned int uart0_13_pins[] = {54, 55};
static const unsigned int uart0_14_pins[] = {58, 59};
static const unsigned int uart0_15_pins[] = {62, 63};
static const unsigned int uart0_16_pins[] = {66, 67};
static const unsigned int uart0_17_pins[] = {70, 71};
static const unsigned int uart0_18_pins[] = {74, 75};
static const unsigned int uart1_0_pins[] = {0, 1};
static const unsigned int uart1_1_pins[] = {4, 5};
static const unsigned int uart1_2_pins[] = {8, 9};
static const unsigned int uart1_3_pins[] = {12, 13};
static const unsigned int uart1_4_pins[] = {16, 17};
static const unsigned int uart1_5_pins[] = {20, 21};
static const unsigned int uart1_6_pins[] = {24, 25};
static const unsigned int uart1_7_pins[] = {28, 29};
static const unsigned int uart1_8_pins[] = {32, 33};
static const unsigned int uart1_9_pins[] = {36, 37};
static const unsigned int uart1_10_pins[] = {40, 41};
static const unsigned int uart1_11_pins[] = {44, 45};
static const unsigned int uart1_12_pins[] = {48, 49};
static const unsigned int uart1_13_pins[] = {52, 53};
static const unsigned int uart1_14_pins[] = {56, 57};
static const unsigned int uart1_15_pins[] = {60, 61};
static const unsigned int uart1_16_pins[] = {64, 65};
static const unsigned int uart1_17_pins[] = {68, 69};
static const unsigned int uart1_18_pins[] = {72, 73};

static const unsigned int i2c0_0_pins[] = {2, 3};
static const unsigned int i2c0_1_pins[] = {6, 7};
static const unsigned int i2c0_2_pins[] = {10, 11};
static const unsigned int i2c0_3_pins[] = {14, 15};
static const unsigned int i2c0_4_pins[] = {18, 19};
static const unsigned int i2c0_5_pins[] = {22, 23};
static const unsigned int i2c0_6_pins[] = {26, 27};
static const unsigned int i2c0_7_pins[] = {30, 31};
static const unsigned int i2c0_8_pins[] = {34, 35};
static const unsigned int i2c0_9_pins[] = {38, 39};
static const unsigned int i2c0_10_pins[] = {42, 43};
static const unsigned int i2c0_11_pins[] = {46, 47};
static const unsigned int i2c0_12_pins[] = {50, 51};
static const unsigned int i2c0_13_pins[] = {54, 55};
static const unsigned int i2c0_14_pins[] = {58, 59};
static const unsigned int i2c0_15_pins[] = {62, 63};
static const unsigned int i2c0_16_pins[] = {66, 67};
static const unsigned int i2c0_17_pins[] = {70, 71};
static const unsigned int i2c0_18_pins[] = {74, 75};
static const unsigned int i2c1_0_pins[] = {0, 1};
static const unsigned int i2c1_1_pins[] = {4, 5};
static const unsigned int i2c1_2_pins[] = {8, 9};
static const unsigned int i2c1_3_pins[] = {12, 13};
static const unsigned int i2c1_4_pins[] = {16, 17};
static const unsigned int i2c1_5_pins[] = {20, 21};
static const unsigned int i2c1_6_pins[] = {24, 25};
static const unsigned int i2c1_7_pins[] = {28, 29};
static const unsigned int i2c1_8_pins[] = {32, 33};
static const unsigned int i2c1_9_pins[] = {36, 37};
static const unsigned int i2c1_10_pins[] = {40, 41};
static const unsigned int i2c1_11_pins[] = {44, 45};
static const unsigned int i2c1_12_pins[] = {48, 49};
static const unsigned int i2c1_13_pins[] = {52, 53};
static const unsigned int i2c1_14_pins[] = {56, 57};
static const unsigned int i2c1_15_pins[] = {60, 61};
static const unsigned int i2c1_16_pins[] = {64, 65};
static const unsigned int i2c1_17_pins[] = {68, 69};
static const unsigned int i2c1_18_pins[] = {72, 73};
static const unsigned int i2c1_19_pins[] = {76, 77};

static const unsigned int ttc0_0_clk_pins[] = {6};
static const unsigned int ttc0_0_wav_pins[] = {7};
static const unsigned int ttc0_1_clk_pins[] = {14};
static const unsigned int ttc0_1_wav_pins[] = {15};
static const unsigned int ttc0_2_clk_pins[] = {22};
static const unsigned int ttc0_2_wav_pins[] = {23};
static const unsigned int ttc0_3_clk_pins[] = {30};
static const unsigned int ttc0_3_wav_pins[] = {31};
static const unsigned int ttc0_4_clk_pins[] = {38};
static const unsigned int ttc0_4_wav_pins[] = {39};
static const unsigned int ttc0_5_clk_pins[] = {46};
static const unsigned int ttc0_5_wav_pins[] = {47};
static const unsigned int ttc0_6_clk_pins[] = {54};
static const unsigned int ttc0_6_wav_pins[] = {55};
static const unsigned int ttc0_7_clk_pins[] = {62};
static const unsigned int ttc0_7_wav_pins[] = {63};
static const unsigned int ttc0_8_clk_pins[] = {70};
static const unsigned int ttc0_8_wav_pins[] = {71};
static const unsigned int ttc1_0_clk_pins[] = {4};
static const unsigned int ttc1_0_wav_pins[] = {5};
static const unsigned int ttc1_1_clk_pins[] = {12};
static const unsigned int ttc1_1_wav_pins[] = {13};
static const unsigned int ttc1_2_clk_pins[] = {20};
static const unsigned int ttc1_2_wav_pins[] = {21};
static const unsigned int ttc1_3_clk_pins[] = {28};
static const unsigned int ttc1_3_wav_pins[] = {29};
static const unsigned int ttc1_4_clk_pins[] = {36};
static const unsigned int ttc1_4_wav_pins[] = {37};
static const unsigned int ttc1_5_clk_pins[] = {44};
static const unsigned int ttc1_5_wav_pins[] = {45};
static const unsigned int ttc1_6_clk_pins[] = {52};
static const unsigned int ttc1_6_wav_pins[] = {53};
static const unsigned int ttc1_7_clk_pins[] = {60};
static const unsigned int ttc1_7_wav_pins[] = {61};
static const unsigned int ttc1_8_clk_pins[] = {68};
static const unsigned int ttc1_8_wav_pins[] = {69};
static const unsigned int ttc2_0_clk_pins[] = {2};
static const unsigned int ttc2_0_wav_pins[] = {3};
static const unsigned int ttc2_1_clk_pins[] = {10};
static const unsigned int ttc2_1_wav_pins[] = {11};
static const unsigned int ttc2_2_clk_pins[] = {18};
static const unsigned int ttc2_2_wav_pins[] = {19};
static const unsigned int ttc2_3_clk_pins[] = {26};
static const unsigned int ttc2_3_wav_pins[] = {27};
static const unsigned int ttc2_4_clk_pins[] = {34};
static const unsigned int ttc2_4_wav_pins[] = {35};
static const unsigned int ttc2_5_clk_pins[] = {42};
static const unsigned int ttc2_5_wav_pins[] = {43};
static const unsigned int ttc2_6_clk_pins[] = {50};
static const unsigned int ttc2_6_wav_pins[] = {51};
static const unsigned int ttc2_7_clk_pins[] = {58};
static const unsigned int ttc2_7_wav_pins[] = {59};
static const unsigned int ttc2_8_clk_pins[] = {66};
static const unsigned int ttc2_8_wav_pins[] = {67};
static const unsigned int ttc3_0_clk_pins[] = {0};
static const unsigned int ttc3_0_wav_pins[] = {1};
static const unsigned int ttc3_1_clk_pins[] = {8};
static const unsigned int ttc3_1_wav_pins[] = {9};
static const unsigned int ttc3_2_clk_pins[] = {16};
static const unsigned int ttc3_2_wav_pins[] = {17};
static const unsigned int ttc3_3_clk_pins[] = {24};
static const unsigned int ttc3_3_wav_pins[] = {25};
static const unsigned int ttc3_4_clk_pins[] = {32};
static const unsigned int ttc3_4_wav_pins[] = {33};
static const unsigned int ttc3_5_clk_pins[] = {40};
static const unsigned int ttc3_5_wav_pins[] = {41};
static const unsigned int ttc3_6_clk_pins[] = {48};
static const unsigned int ttc3_6_wav_pins[] = {49};
static const unsigned int ttc3_7_clk_pins[] = {56};
static const unsigned int ttc3_7_wav_pins[] = {57};
static const unsigned int ttc3_8_clk_pins[] = {64};
static const unsigned int ttc3_8_wav_pins[] = {65};

static const unsigned int swdt0_0_clk_pins[] = {6};
static const unsigned int swdt0_0_rst_pins[] = {7};
static const unsigned int swdt0_1_clk_pins[] = {10};
static const unsigned int swdt0_1_rst_pins[] = {11};
static const unsigned int swdt0_2_clk_pins[] = {18};
static const unsigned int swdt0_2_rst_pins[] = {19};
static const unsigned int swdt0_3_clk_pins[] = {22};
static const unsigned int swdt0_3_rst_pins[] = {23};
static const unsigned int swdt0_4_clk_pins[] = {30};
static const unsigned int swdt0_4_rst_pins[] = {31};
static const unsigned int swdt0_5_clk_pins[] = {34};
static const unsigned int swdt0_5_rst_pins[] = {35};
static const unsigned int swdt0_6_clk_pins[] = {42};
static const unsigned int swdt0_6_rst_pins[] = {43};
static const unsigned int swdt0_7_clk_pins[] = {46};
static const unsigned int swdt0_7_rst_pins[] = {47};
static const unsigned int swdt0_8_clk_pins[] = {50};
static const unsigned int swdt0_8_rst_pins[] = {51};
static const unsigned int swdt0_9_clk_pins[] = {62};
static const unsigned int swdt0_9_rst_pins[] = {63};
static const unsigned int swdt0_10_clk_pins[] = {66};
static const unsigned int swdt0_10_rst_pins[] = {67};
static const unsigned int swdt0_11_clk_pins[] = {70};
static const unsigned int swdt0_11_rst_pins[] = {71};
static const unsigned int swdt0_12_clk_pins[] = {74};
static const unsigned int swdt0_12_rst_pins[] = {75};
static const unsigned int swdt1_0_clk_pins[] = {4};
static const unsigned int swdt1_0_rst_pins[] = {5};
static const unsigned int swdt1_1_clk_pins[] = {8};
static const unsigned int swdt1_1_rst_pins[] = {9};
static const unsigned int swdt1_2_clk_pins[] = {16};
static const unsigned int swdt1_2_rst_pins[] = {17};
static const unsigned int swdt1_3_clk_pins[] = {20};
static const unsigned int swdt1_3_rst_pins[] = {21};
static const unsigned int swdt1_4_clk_pins[] = {24};
static const unsigned int swdt1_4_rst_pins[] = {25};
static const unsigned int swdt1_5_clk_pins[] = {32};
static const unsigned int swdt1_5_rst_pins[] = {33};
static const unsigned int swdt1_6_clk_pins[] = {36};
static const unsigned int swdt1_6_rst_pins[] = {37};
static const unsigned int swdt1_7_clk_pins[] = {44};
static const unsigned int swdt1_7_rst_pins[] = {45};
static const unsigned int swdt1_8_clk_pins[] = {48};
static const unsigned int swdt1_8_rst_pins[] = {49};
static const unsigned int swdt1_9_clk_pins[] = {56};
static const unsigned int swdt1_9_rst_pins[] = {57};
static const unsigned int swdt1_10_clk_pins[] = {64};
static const unsigned int swdt1_10_rst_pins[] = {65};
static const unsigned int swdt1_11_clk_pins[] = {68};
static const unsigned int swdt1_11_rst_pins[] = {69};
static const unsigned int swdt1_12_clk_pins[] = {72};
static const unsigned int swdt1_12_rst_pins[] = {73};

static const unsigned int gpio0_0_pins[] = {0};
static const unsigned int gpio0_1_pins[] = {1};
static const unsigned int gpio0_2_pins[] = {2};
static const unsigned int gpio0_3_pins[] = {3};
static const unsigned int gpio0_4_pins[] = {4};
static const unsigned int gpio0_5_pins[] = {5};
static const unsigned int gpio0_6_pins[] = {6};
static const unsigned int gpio0_7_pins[] = {7};
static const unsigned int gpio0_8_pins[] = {8};
static const unsigned int gpio0_9_pins[] = {9};
static const unsigned int gpio0_10_pins[] = {10};
static const unsigned int gpio0_11_pins[] = {11};
static const unsigned int gpio0_12_pins[] = {12};
static const unsigned int gpio0_13_pins[] = {13};
static const unsigned int gpio0_14_pins[] = {14};
static const unsigned int gpio0_15_pins[] = {15};
static const unsigned int gpio0_16_pins[] = {16};
static const unsigned int gpio0_17_pins[] = {17};
static const unsigned int gpio0_18_pins[] = {18};
static const unsigned int gpio0_19_pins[] = {19};
static const unsigned int gpio0_20_pins[] = {20};
static const unsigned int gpio0_21_pins[] = {21};
static const unsigned int gpio0_22_pins[] = {22};
static const unsigned int gpio0_23_pins[] = {23};
static const unsigned int gpio0_24_pins[] = {24};
static const unsigned int gpio0_25_pins[] = {25};
static const unsigned int gpio0_26_pins[] = {26};
static const unsigned int gpio0_27_pins[] = {27};
static const unsigned int gpio0_28_pins[] = {28};
static const unsigned int gpio0_29_pins[] = {29};
static const unsigned int gpio0_30_pins[] = {30};
static const unsigned int gpio0_31_pins[] = {31};
static const unsigned int gpio0_32_pins[] = {32};
static const unsigned int gpio0_33_pins[] = {33};
static const unsigned int gpio0_34_pins[] = {34};
static const unsigned int gpio0_35_pins[] = {35};
static const unsigned int gpio0_36_pins[] = {36};
static const unsigned int gpio0_37_pins[] = {37};
static const unsigned int gpio0_38_pins[] = {38};
static const unsigned int gpio0_39_pins[] = {39};
static const unsigned int gpio0_40_pins[] = {40};
static const unsigned int gpio0_41_pins[] = {41};
static const unsigned int gpio0_42_pins[] = {42};
static const unsigned int gpio0_43_pins[] = {43};
static const unsigned int gpio0_44_pins[] = {44};
static const unsigned int gpio0_45_pins[] = {45};
static const unsigned int gpio0_46_pins[] = {46};
static const unsigned int gpio0_47_pins[] = {47};
static const unsigned int gpio0_48_pins[] = {48};
static const unsigned int gpio0_49_pins[] = {49};
static const unsigned int gpio0_50_pins[] = {50};
static const unsigned int gpio0_51_pins[] = {51};
static const unsigned int gpio0_52_pins[] = {52};
static const unsigned int gpio0_53_pins[] = {53};
static const unsigned int gpio0_54_pins[] = {54};
static const unsigned int gpio0_55_pins[] = {55};
static const unsigned int gpio0_56_pins[] = {56};
static const unsigned int gpio0_57_pins[] = {57};
static const unsigned int gpio0_58_pins[] = {58};
static const unsigned int gpio0_59_pins[] = {59};
static const unsigned int gpio0_60_pins[] = {60};
static const unsigned int gpio0_61_pins[] = {61};
static const unsigned int gpio0_62_pins[] = {62};
static const unsigned int gpio0_63_pins[] = {63};
static const unsigned int gpio0_64_pins[] = {64};
static const unsigned int gpio0_65_pins[] = {65};
static const unsigned int gpio0_66_pins[] = {66};
static const unsigned int gpio0_67_pins[] = {67};
static const unsigned int gpio0_68_pins[] = {68};
static const unsigned int gpio0_69_pins[] = {69};
static const unsigned int gpio0_70_pins[] = {70};
static const unsigned int gpio0_71_pins[] = {71};
static const unsigned int gpio0_72_pins[] = {72};
static const unsigned int gpio0_73_pins[] = {73};
static const unsigned int gpio0_74_pins[] = {74};
static const unsigned int gpio0_75_pins[] = {75};
static const unsigned int gpio0_76_pins[] = {76};
static const unsigned int gpio0_77_pins[] = {77};

static const unsigned int usb0_0_pins[] = {52, 53, 54, 55, 56, 57, 58, 59, 60,
						61, 62, 63};
static const unsigned int usb1_0_pins[] = {64, 65, 66, 67, 68, 69, 70, 71, 72,
						73, 74, 75};

static const unsigned int pmu0_0_pins[] = {26};
static const unsigned int pmu0_1_pins[] = {27};
static const unsigned int pmu0_2_pins[] = {28};
static const unsigned int pmu0_3_pins[] = {29};
static const unsigned int pmu0_4_pins[] = {30};
static const unsigned int pmu0_5_pins[] = {31};
static const unsigned int pmu0_6_pins[] = {32};
static const unsigned int pmu0_7_pins[] = {33};
static const unsigned int pmu0_8_pins[] = {34};
static const unsigned int pmu0_9_pins[] = {35};
static const unsigned int pmu0_10_pins[] = {36};
static const unsigned int pmu0_11_pins[] = {37};

static const unsigned int pcie0_0_pins[] = {29};
static const unsigned int pcie0_1_pins[] = {30};
static const unsigned int pcie0_2_pins[] = {31};
static const unsigned int pcie0_3_pins[] = {33};
static const unsigned int pcie0_4_pins[] = {34};
static const unsigned int pcie0_5_pins[] = {35};
static const unsigned int pcie0_6_pins[] = {36};
static const unsigned int pcie0_7_pins[] = {37};

static const unsigned int csu0_0_pins[] = {18};
static const unsigned int csu0_1_pins[] = {19};
static const unsigned int csu0_2_pins[] = {20};
static const unsigned int csu0_3_pins[] = {21};
static const unsigned int csu0_4_pins[] = {22};
static const unsigned int csu0_5_pins[] = {23};
static const unsigned int csu0_6_pins[] = {24};
static const unsigned int csu0_7_pins[] = {25};
static const unsigned int csu0_8_pins[] = {26};
static const unsigned int csu0_9_pins[] = {31};
static const unsigned int csu0_10_pins[] = {32};
static const unsigned int csu0_11_pins[] = {33};

static const unsigned int dpaux0_0_pins[] = {27, 28};
static const unsigned int dpaux0_1_pins[] = {29, 30};
static const unsigned int dpaux0_2_pins[] = {34, 35};
static const unsigned int dpaux0_3_pins[] = {36, 37};

static const unsigned int pjtag0_0_pins[] = {0, 1, 2, 3};
static const unsigned int pjtag0_1_pins[] = {12, 13, 14, 15};
static const unsigned int pjtag0_2_pins[] = {26, 27, 28, 29};
static const unsigned int pjtag0_3_pins[] = {38, 39, 40, 41};
static const unsigned int pjtag0_4_pins[] = {52, 53, 54, 55};
static const unsigned int pjtag0_5_pins[] = {58, 59, 60, 61};

static const unsigned int trace0_0_pins[] = {2, 3, 4, 5, 6, 7, 8, 9, 10,
						11, 12, 13, 14, 15, 16, 17};
static const unsigned int trace0_0_clk_pins[] = {0, 1};
static const unsigned int trace0_1_pins[] = {26, 27, 28, 29, 30, 31, 32, 33, 34,
						35, 36, 37, 40, 41, 42, 43};
static const unsigned int trace0_1_clk_pins[] = {38, 39};
static const unsigned int trace0_2_pins[] = {54, 55, 56, 57, 58, 59, 60, 61, 62,
						63, 64, 65, 66, 67, 68, 69};
static const unsigned int trace0_2_clk_pins[] = {52, 53};

static const unsigned int testscan0_0_pins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
						10, 11, 12, 13, 14, 15, 16, 17,
						18, 19, 20, 21, 22, 23, 24, 25,
						26, 27, 28, 29, 30, 31, 32, 33,
						34, 35, 36, 37};

#define DEFINE_ZYNQMP_PINCTRL_GRP(nm) { \
		.name = #nm "_grp", \
		.pins = nm ## _pins, \
		.npins = ARRAY_SIZE(nm ## _pins), \
	}

static const struct zynqmp_pctrl_group zynqmp_pctrl_groups[] = {
	DEFINE_ZYNQMP_PINCTRL_GRP(ethernet0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(ethernet1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(ethernet2_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(ethernet3_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(gemtsu0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(gemtsu0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(gemtsu0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(mdio0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(mdio1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(mdio1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(mdio2_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(mdio3_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(qspi0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(qspi_ss),
	DEFINE_ZYNQMP_PINCTRL_GRP(qspi_fbclk),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_0_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_0_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_0_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_1_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_1_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_1_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_2_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_2_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_2_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_3_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_3_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_3_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_4_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_4_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_4_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_5_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_5_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi0_5_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_0_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_0_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_0_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_1_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_1_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_1_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_2_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_2_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_2_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_3_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_3_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_3_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_4_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_4_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_4_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_5_ss0),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_5_ss1),
	DEFINE_ZYNQMP_PINCTRL_GRP(spi1_5_ss2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_0_pc),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_0_cd),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_0_wp),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_1_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1_pc),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1_cd),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1_wp),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_2_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_4bit_2_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_1bit_2_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_2_pc),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_2_cd),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio0_2_wp),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_4bit_0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_4bit_0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_0_pc),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_0_cd),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_0_wp),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_4bit_1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1bit_1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1_pc),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1_cd),
	DEFINE_ZYNQMP_PINCTRL_GRP(sdio1_1_wp),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_0_ce),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_0_rb),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_0_dqs),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_1_ce),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_1_rb),
	DEFINE_ZYNQMP_PINCTRL_GRP(nand0_1_dqs),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(can0_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(can1_19),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart0_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(uart1_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c0_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(i2c1_19),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_0_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_1_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_2_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_3_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_4_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_5_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_6_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_7_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc0_8_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_0_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_1_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_2_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_3_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_4_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_5_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_6_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_7_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc1_8_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_0_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_1_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_2_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_3_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_4_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_5_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_6_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_7_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc2_8_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_0_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_1_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_2_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_3_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_4_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_5_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_6_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_7_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(ttc3_8_wav),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_0_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_1_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_2_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_3_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_4_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_5_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_6_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_7_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_8_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_9_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_9_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_10_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_10_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_11_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_11_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_12_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt0_12_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_0_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_1_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_2_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_3_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_3_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_4_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_4_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_5_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_5_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_6_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_6_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_7_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_7_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_8_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_8_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_9_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_9_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_10_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_10_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_11_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_11_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_12_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(swdt1_12_rst),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_12),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_13),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_14),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_15),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_16),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_17),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_18),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_19),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_20),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_21),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_22),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_23),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_24),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_25),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_26),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_27),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_28),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_29),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_30),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_31),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_32),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_33),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_34),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_35),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_36),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_37),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_38),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_39),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_40),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_41),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_42),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_43),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_44),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_45),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_46),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_47),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_48),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_49),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_50),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_51),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_52),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_53),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_54),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_55),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_56),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_57),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_58),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_59),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_60),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_61),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_62),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_63),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_64),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_65),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_66),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_67),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_68),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_69),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_70),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_71),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_72),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_73),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_74),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_75),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_76),
	DEFINE_ZYNQMP_PINCTRL_GRP(gpio0_77),
	DEFINE_ZYNQMP_PINCTRL_GRP(usb0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(usb1_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(pmu0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(pcie0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_6),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_7),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_8),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_9),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_10),
	DEFINE_ZYNQMP_PINCTRL_GRP(csu0_11),
	DEFINE_ZYNQMP_PINCTRL_GRP(dpaux0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(dpaux0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(dpaux0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(dpaux0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_3),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_4),
	DEFINE_ZYNQMP_PINCTRL_GRP(pjtag0_5),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_0),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_0_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_1),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_1_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_2),
	DEFINE_ZYNQMP_PINCTRL_GRP(trace0_2_clk),
	DEFINE_ZYNQMP_PINCTRL_GRP(testscan0_0),
};

/* function groups */
static const char * const ethernet0_groups[] = {"ethernet0_0_grp"};
static const char * const ethernet1_groups[] = {"ethernet1_0_grp"};
static const char * const ethernet2_groups[] = {"ethernet2_0_grp"};
static const char * const ethernet3_groups[] = {"ethernet3_0_grp"};

static const char * const gemtsu0_groups[] = {"gemtsu0_0_grp", "gemtsu0_1_grp",
						"gemtsu0_2_grp"};

static const char * const usb0_groups[] = {"usb0_0_grp"};
static const char * const usb1_groups[] = {"usb1_0_grp"};

static const char * const mdio0_groups[] = {"mdio0_0_grp"};
static const char * const mdio1_groups[] = {"mdio1_0_grp", "mdio1_1_grp"};
static const char * const mdio2_groups[] = {"mdio2_0_grp"};
static const char * const mdio3_groups[] = {"mdio3_0_grp"};

static const char * const qspi0_groups[] = {"qspi0_0_grp"};
static const char * const qspi_fbclk_groups[] = {"qspi_fbclk_grp"};
static const char * const qspi_ss_groups[] = {"qspi_ss_grp"};

static const char * const spi0_groups[] = {"spi0_0_grp", "spi0_1_grp",
					   "spi0_2_grp", "spi0_3_grp",
					   "spi0_4_grp", "spi0_5_grp"};
static const char * const spi1_groups[] = {"spi1_0_grp", "spi1_1_grp",
					   "spi1_2_grp", "spi1_3_grp",
					   "spi1_4_grp", "spi1_5_grp"};
static const char * const spi0_ss_groups[] = {"spi0_0_ss0_grp",
		"spi0_0_ss1_grp", "spi0_0_ss2_grp", "spi0_1_ss0_grp",
		"spi0_1_ss1_grp", "spi0_1_ss2_grp", "spi0_2_ss0_grp",
		"spi0_2_ss1_grp", "spi0_2_ss2_grp", "spi0_3_ss0_grp",
		"spi0_3_ss1_grp", "spi0_3_ss2_grp", "spi0_4_ss0_grp",
		"spi0_4_ss1_grp", "spi0_4_ss2_grp", "spi0_5_ss0_grp",
		"spi0_5_ss1_grp", "spi0_5_ss2_grp"};
static const char * const spi1_ss_groups[] = {"spi1_0_ss0_grp",
		"spi1_0_ss1_grp", "spi1_0_ss2_grp", "spi1_1_ss0_grp",
		"spi1_1_ss1_grp", "spi1_1_ss2_grp", "spi1_2_ss0_grp",
		"spi1_2_ss1_grp", "spi1_2_ss2_grp", "spi1_3_ss0_grp",
		"spi1_3_ss1_grp", "spi1_3_ss2_grp", "spi1_4_ss0_grp",
		"spi1_4_ss1_grp", "spi1_4_ss2_grp", "spi1_5_ss0_grp",
		"spi1_5_ss1_grp", "spi1_5_ss2_grp"};

static const char * const sdio0_groups[] = {"sdio0_0_grp",
				"sdio0_1_grp", "sdio0_2_grp",
				"sdio0_4bit_0_0_grp", "sdio0_4bit_0_1_grp",
				"sdio0_4bit_1_0_grp", "sdio0_4bit_1_1_grp",
				"sdio0_4bit_2_0_grp", "sdio0_4bit_2_1_grp",
				"sdio0_1bit_0_0_grp", "sdio0_1bit_0_1_grp",
				"sdio0_1bit_0_2_grp", "sdio0_1bit_0_3_grp",
				"sdio0_1bit_0_4_grp", "sdio0_1bit_0_5_grp",
				"sdio0_1bit_0_6_grp", "sdio0_1bit_0_7_grp",
				"sdio0_1bit_1_0_grp", "sdio0_1bit_1_1_grp",
				"sdio0_1bit_1_2_grp", "sdio0_1bit_1_3_grp",
				"sdio0_1bit_1_4_grp", "sdio0_1bit_1_5_grp",
				"sdio0_1bit_1_6_grp", "sdio0_1bit_1_7_grp",
				"sdio0_1bit_2_0_grp", "sdio0_1bit_2_1_grp",
				"sdio0_1bit_2_2_grp", "sdio0_1bit_2_3_grp",
				"sdio0_1bit_2_4_grp", "sdio0_1bit_2_5_grp",
				"sdio0_1bit_2_6_grp", "sdio0_1bit_2_7_grp"};
static const char * const sdio1_groups[] = {"sdio1_0_grp",
				"sdio1_4bit_0_0_grp", "sdio1_4bit_0_1_grp",
				"sdio1_4bit_1_0_grp",
				"sdio1_1bit_0_0_grp", "sdio1_1bit_0_1_grp",
				"sdio1_1bit_0_2_grp", "sdio1_1bit_0_3_grp",
				"sdio1_1bit_0_4_grp", "sdio1_1bit_0_5_grp",
				"sdio1_1bit_0_6_grp", "sdio1_1bit_0_7_grp",
				"sdio1_1bit_1_0_grp", "sdio1_1bit_1_1_grp",
				"sdio1_1bit_1_2_grp", "sdio1_1bit_1_3_grp"};
static const char * const sdio0_pc_groups[] = {"sdio0_0_pc_grp",
		"sdio0_1_pc_grp", "sdio0_2_pc_grp"};
static const char * const sdio1_pc_groups[] = {"sdio1_0_pc_grp",
		"sdio1_1_pc_grp"};
static const char * const sdio0_cd_groups[] = {"sdio0_0_cd_grp",
		"sdio0_1_cd_grp", "sdio0_2_cd_grp"};
static const char * const sdio1_cd_groups[] = {"sdio1_0_cd_grp",
		"sdio1_1_cd_grp"};
static const char * const sdio0_wp_groups[] = {"sdio0_0_wp_grp",
		"sdio0_1_wp_grp", "sdio0_2_wp_grp"};
static const char * const sdio1_wp_groups[] = {"sdio1_0_wp_grp",
		"sdio1_1_wp_grp"};

static const char * const nand0_groups[] = {"nand0_0_grp"};
static const char * const nand0_ce_groups[] = {"nand0_0_ce_grp",
						"nand0_1_ce_grp"};
static const char * const nand0_rb_groups[] = {"nand0_0_rb_grp",
						"nand0_1_rb_grp"};
static const char * const nand0_dqs_groups[] = {"nand0_0_dqs_grp",
						"nand0_1_dqs_grp"};

static const char * const can0_groups[] = {"can0_0_grp", "can0_1_grp",
		"can0_2_grp", "can0_3_grp", "can0_4_grp", "can0_5_grp",
		"can0_6_grp", "can0_7_grp", "can0_8_grp", "can0_9_grp",
		"can0_10_grp", "can0_11_grp", "can0_12_grp", "can0_13_grp",
		"can0_14_grp", "can0_15_grp", "can0_16_grp", "can0_17_grp",
		"can0_18_grp"};
static const char * const can1_groups[] = {"can1_0_grp", "can1_1_grp",
		"can1_2_grp", "can1_3_grp", "can1_4_grp", "can1_5_grp",
		"can1_6_grp", "can1_7_grp", "can1_8_grp", "can1_9_grp",
		"can1_10_grp", "can1_11_grp", "can1_12_grp", "can1_13_grp",
		"can1_14_grp", "can1_15_grp", "can1_16_grp", "can1_17_grp",
		"can1_18_grp", "can1_19_grp"};

static const char * const uart0_groups[] = {"uart0_0_grp", "uart0_1_grp",
		"uart0_2_grp", "uart0_3_grp", "uart0_4_grp", "uart0_5_grp",
		"uart0_6_grp", "uart0_7_grp", "uart0_8_grp", "uart0_9_grp",
		"uart0_10_grp", "uart0_11_grp", "uart0_12_grp", "uart0_13_grp",
		"uart0_14_grp", "uart0_15_grp", "uart0_16_grp", "uart0_17_grp",
		"uart0_18_grp"};
static const char * const uart1_groups[] = {"uart1_0_grp", "uart1_1_grp",
		"uart1_2_grp", "uart1_3_grp", "uart1_4_grp", "uart1_5_grp",
		"uart1_6_grp", "uart1_7_grp", "uart1_8_grp", "uart1_9_grp",
		"uart1_10_grp", "uart1_11_grp", "uart1_12_grp", "uart1_13_grp",
		"uart1_14_grp", "uart1_15_grp", "uart1_16_grp", "uart1_17_grp",
		"uart1_18_grp"};

static const char * const i2c0_groups[] = {"i2c0_0_grp", "i2c0_1_grp",
		"i2c0_2_grp", "i2c0_3_grp", "i2c0_4_grp", "i2c0_5_grp",
		"i2c0_6_grp", "i2c0_7_grp", "i2c0_8_grp", "i2c0_9_grp",
		"i2c0_10_grp", "i2c0_11_grp", "i2c0_12_grp", "i2c0_13_grp",
		"i2c0_14_grp", "i2c0_15_grp", "i2c0_16_grp", "i2c0_17_grp",
		"i2c0_18_grp"};
static const char * const i2c1_groups[] = {"i2c1_0_grp", "i2c1_1_grp",
		"i2c1_2_grp", "i2c1_3_grp", "i2c1_4_grp", "i2c1_5_grp",
		"i2c1_6_grp", "i2c1_7_grp", "i2c1_8_grp", "i2c1_9_grp",
		"i2c1_10_grp", "i2c1_11_grp", "i2c1_12_grp", "i2c1_13_grp",
		"i2c1_14_grp", "i2c1_15_grp", "i2c1_16_grp", "i2c1_17_grp",
		"i2c1_18_grp", "i2c1_19_grp"};

static const char * const ttc0_clk_groups[] = {"ttc0_0_clk_grp",
					"ttc0_1_clk_grp", "ttc0_2_clk_grp",
					"ttc0_3_clk_grp", "ttc0_4_clk_grp",
					"ttc0_5_clk_grp", "ttc0_6_clk_grp",
					"ttc0_7_clk_grp", "ttc0_8_clk_grp"};
static const char * const ttc0_wav_groups[] = {"ttc0_0_wav_grp",
					"ttc0_1_wav_grp", "ttc0_2_wav_grp",
					"ttc0_3_wav_grp", "ttc0_4_wav_grp",
					"ttc0_5_wav_grp", "ttc0_6_wav_grp",
					"ttc0_7_wav_grp", "ttc0_8_wav_grp"};
static const char * const ttc1_clk_groups[] = {"ttc1_0_clk_grp",
					"ttc1_1_clk_grp", "ttc1_2_clk_grp",
					"ttc1_3_clk_grp", "ttc1_4_clk_grp",
					"ttc1_5_clk_grp", "ttc1_6_clk_grp",
					"ttc1_7_clk_grp", "ttc1_8_clk_grp"};
static const char * const ttc1_wav_groups[] = {"ttc1_0_wav_grp",
					"ttc1_1_wav_grp", "ttc1_2_wav_grp",
					"ttc1_3_wav_grp", "ttc1_4_wav_grp",
					"ttc1_5_wav_grp", "ttc1_6_wav_grp",
					"ttc1_7_wav_grp", "ttc1_8_wav_grp"};
static const char * const ttc2_clk_groups[] = {"ttc2_0_clk_grp",
					"ttc2_1_clk_grp", "ttc2_2_clk_grp",
					"ttc2_3_clk_grp", "ttc2_4_clk_grp",
					"ttc2_5_clk_grp", "ttc2_6_clk_grp",
					"ttc2_7_clk_grp", "ttc2_8_clk_grp"};
static const char * const ttc2_wav_groups[] = {"ttc2_0_wav_grp",
					"ttc2_1_wav_grp", "ttc2_2_wav_grp",
					"ttc2_3_wav_grp", "ttc2_4_wav_grp",
					"ttc2_5_wav_grp", "ttc2_6_wav_grp",
					"ttc2_7_wav_grp", "ttc2_8_wav_grp"};
static const char * const ttc3_clk_groups[] = {"ttc3_0_clk_grp",
					"ttc3_1_clk_grp", "ttc3_2_clk_grp",
					"ttc3_3_clk_grp", "ttc3_4_clk_grp",
					"ttc3_5_clk_grp", "ttc3_6_clk_grp",
					"ttc3_7_clk_grp", "ttc3_8_clk_grp"};
static const char * const ttc3_wav_groups[] = {"ttc3_0_wav_grp",
					"ttc3_1_wav_grp", "ttc3_2_wav_grp",
					"ttc3_3_wav_grp", "ttc3_4_wav_grp",
					"ttc3_5_wav_grp", "ttc3_6_wav_grp",
					"ttc3_7_wav_grp", "ttc3_8_wav_grp"};

static const char * const swdt0_clk_groups[] = {"swdt0_0_clk_grp",
		"swdt0_1_clk_grp", "swdt0_2_clk_grp", "swdt0_3_clk_grp",
		"swdt0_4_clk_grp", "swdt0_5_clk_grp", "swdt0_6_clk_grp",
		"swdt0_7_clk_grp", "swdt0_8_clk_grp", "swdt0_9_clk_grp",
		"swdt0_10_clk_grp", "swdt0_11_clk_grp", "swdt0_12_clk_grp"};
static const char * const swdt0_rst_groups[] = {"swdt0_0_rst_grp",
		"swdt0_1_rst_grp", "swdt0_2_rst_grp", "swdt0_3_rst_grp",
		"swdt0_4_rst_grp", "swdt0_5_rst_grp", "swdt0_6_rst_grp",
		"swdt0_7_rst_grp", "swdt0_8_rst_grp", "swdt0_9_rst_grp",
		"swdt0_10_rst_grp", "swdt0_11_rst_grp", "swdt0_12_rst_grp"};
static const char * const swdt1_clk_groups[] = {"swdt1_0_clk_grp",
		"swdt1_1_clk_grp", "swdt1_2_clk_grp", "swdt1_3_clk_grp",
		"swdt1_4_clk_grp", "swdt1_5_clk_grp", "swdt1_6_clk_grp",
		"swdt1_7_clk_grp", "swdt1_8_clk_grp", "swdt1_9_clk_grp",
		"swdt1_10_clk_grp", "swdt1_11_clk_grp", "swdt1_12_clk_grp"};
static const char * const swdt1_rst_groups[] = {"swdt1_0_rst_grp",
		"swdt1_1_rst_grp", "swdt1_2_rst_grp", "swdt1_3_rst_grp",
		"swdt1_4_rst_grp", "swdt1_5_rst_grp", "swdt1_6_rst_grp",
		"swdt1_7_rst_grp", "swdt1_8_rst_grp", "swdt1_9_rst_grp",
		"swdt1_10_rst_grp", "swdt1_11_rst_grp", "swdt1_12_rst_grp"};

static const char * const gpio0_groups[] = {"gpio0_0_grp",
		"gpio0_2_grp", "gpio0_4_grp", "gpio0_6_grp",
		"gpio0_8_grp", "gpio0_10_grp", "gpio0_12_grp",
		"gpio0_14_grp", "gpio0_16_grp", "gpio0_18_grp",
		"gpio0_20_grp", "gpio0_22_grp", "gpio0_24_grp",
		"gpio0_26_grp", "gpio0_28_grp", "gpio0_30_grp",
		"gpio0_32_grp", "gpio0_34_grp", "gpio0_36_grp",
		"gpio0_38_grp", "gpio0_40_grp", "gpio0_42_grp",
		"gpio0_44_grp", "gpio0_46_grp", "gpio0_48_grp",
		"gpio0_50_grp", "gpio0_52_grp", "gpio0_54_grp",
		"gpio0_56_grp", "gpio0_58_grp", "gpio0_60_grp",
		"gpio0_62_grp", "gpio0_64_grp", "gpio0_66_grp",
		"gpio0_68_grp", "gpio0_70_grp", "gpio0_72_grp",
		"gpio0_74_grp", "gpio0_76_grp", "gpio0_1_grp",
		"gpio0_3_grp", "gpio0_5_grp", "gpio0_7_grp",
		"gpio0_9_grp", "gpio0_11_grp", "gpio0_13_grp",
		"gpio0_15_grp", "gpio0_17_grp", "gpio0_19_grp",
		"gpio0_21_grp", "gpio0_23_grp", "gpio0_25_grp",
		"gpio0_27_grp", "gpio0_29_grp", "gpio0_31_grp",
		"gpio0_33_grp", "gpio0_35_grp", "gpio0_37_grp",
		"gpio0_39_grp", "gpio0_41_grp", "gpio0_43_grp",
		"gpio0_45_grp", "gpio0_47_grp", "gpio0_49_grp",
		"gpio0_51_grp", "gpio0_53_grp", "gpio0_55_grp",
		"gpio0_57_grp", "gpio0_59_grp", "gpio0_61_grp",
		"gpio0_63_grp", "gpio0_65_grp", "gpio0_67_grp",
		"gpio0_69_grp", "gpio0_71_grp", "gpio0_73_grp",
		"gpio0_75_grp", "gpio0_77_grp"};

static const char * const pmu0_groups[] = {"pmu0_0_grp", "pmu0_1_grp",
						"pmu0_2_grp", "pmu0_3_grp",
						"pmu0_4_grp", "pmu0_5_grp",
						"pmu0_6_grp", "pmu0_7_grp",
						"pmu0_8_grp", "pmu0_9_grp",
						"pmu0_10_grp", "pmu0_11_grp"};

static const char * const pcie0_groups[] = {"pcie0_0_grp", "pcie0_1_grp",
						"pcie0_2_grp", "pcie0_3_grp",
						"pcie0_4_grp", "pcie0_5_grp",
						"pcie0_6_grp", "pcie0_7_grp"};

static const char * const csu0_groups[] = {"csu0_0_grp", "csu0_1_grp",
						"csu0_2_grp", "csu0_3_grp",
						"csu0_4_grp", "csu0_5_grp",
						"csu0_6_grp", "csu0_7_grp",
						"csu0_8_grp", "csu0_9_grp",
						"csu0_10_grp", "csu0_11_grp"};

static const char * const dpaux0_groups[] = {"dpaux0_0_grp", "dpaux0_1_grp",
						"dpaux0_2_grp", "dpaux0_3_grp"};

static const char * const pjtag0_groups[] = {"pjtag0_0_grp", "pjtag0_1_grp",
						"pjtag0_2_grp", "pjtag0_3_grp",
						"pjtag0_4_grp", "pjtag0_5_grp"};

static const char * const trace0_groups[] = {"trace0_0_grp", "trace0_1_grp",
						"trace0_2_grp"};
static const char * const trace0_clk_groups[] = {"trace0_0_clk_grp",
							"trace0_1_clk_grp",
							"trace0_2_clk_grp"};

static const char * const testscan0_groups[] = {"testscan0_0_grp"};

#define DEFINE_ZYNQMP_PINMUX_FUNCTION(fname, mval) \
	[ZYNQMP_PMUX_##fname] = { \
		.name = #fname, \
		.groups = fname##_groups, \
		.ngroups = ARRAY_SIZE(fname##_groups), \
		.mux_val = mval, \
	}

static const struct zynqmp_pinmux_function zynqmp_pmux_functions[] = {
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ethernet0, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ethernet1, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ethernet2, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ethernet3, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(gemtsu0, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(usb0, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(usb1, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(mdio0, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(mdio1, 0x40),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(mdio2, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(mdio3, 0x60),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(qspi0, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(qspi_fbclk, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(qspi_ss, 0x01),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(spi0, 0x40),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(spi1, 0x40),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(spi0_ss, 0x40),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(spi1_ss, 0x40),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio0, 0x04),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio0_pc, 0x04),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio0_wp, 0x04),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio0_cd, 0x04),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio1, 0x08),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio1_pc, 0x08),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio1_wp, 0x08),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(sdio1_cd, 0x08),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(nand0, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(nand0_ce, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(nand0_rb, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(nand0_dqs, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(can0, 0x10),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(can1, 0x10),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(uart0, 0x60),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(uart1, 0x60),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(i2c0, 0x20),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(i2c1, 0x20),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc0_clk, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc0_wav, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc1_clk, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc1_wav, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc2_clk, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc2_wav, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc3_clk, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(ttc3_wav, 0x50),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(swdt0_clk, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(swdt0_rst, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(swdt1_clk, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(swdt1_rst, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(gpio0, 0x00),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(pmu0, 0x04),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(pcie0, 0x02),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(csu0, 0x0C),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(dpaux0, 0x0C),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(pjtag0, 0x30),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(trace0, 0x70),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(trace0_clk, 0x70),
	DEFINE_ZYNQMP_PINMUX_FUNCTION(testscan0, 0x80),
};

/* pinctrl */
static int zynqmp_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->ngroups;
}

static const char *zynqmp_pctrl_get_group_name(struct pinctrl_dev *pctldev,
							unsigned int selector)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->groups[selector].name;
}

static int zynqmp_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
			unsigned int selector, const unsigned int **pins,
			unsigned int *num_pins)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->groups[selector].pins;
	*num_pins = pctrl->groups[selector].npins;

	return 0;
}

static const struct pinctrl_ops zynqmp_pctrl_ops = {
	.get_groups_count = zynqmp_pctrl_get_groups_count,
	.get_group_name = zynqmp_pctrl_get_group_name,
	.get_group_pins = zynqmp_pctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

/* Write/Read access to Registers */
static inline int zynqmp_pctrl_writereg(u32 val, u32 reg, u32 mask)
{
	return zynqmp_pm_mmio_write(reg, mask, val);
}

static inline int zynqmp_pctrl_readreg(u32 *val, u32 reg)
{
	return zynqmp_pm_mmio_read(reg, val);
}

/* pinmux */
static int zynqmp_pmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->nfuncs;
}

static const char *zynqmp_pmux_get_function_name(struct pinctrl_dev *pctldev,
							 unsigned int selector)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->funcs[selector].name;
}

static int zynqmp_pmux_get_function_groups(struct pinctrl_dev *pctldev,
			unsigned int selector, const char * const **groups,
						unsigned * const num_groups)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->funcs[selector].groups;
	*num_groups = pctrl->funcs[selector].ngroups;
	return 0;
}

static int zynqmp_pinmux_set_mux(struct pinctrl_dev *pctldev,
				unsigned int function, unsigned int group)
{
	int i, ret;
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct zynqmp_pctrl_group *pgrp = &pctrl->groups[group];
	const struct zynqmp_pinmux_function *func = &pctrl->funcs[function];

	for (i = 0; i < pgrp->npins; i++) {
		unsigned int pin = pgrp->pins[i];
		u32 reg, addr_offset, mask;

		addr_offset = pctrl->iouaddr + 4 * pin;
		reg = func->mux_val << ZYNQMP_PINMUX_MUX_SHIFT;
		mask = ZYNQMP_PINMUX_MUX_MASK << ZYNQMP_PINMUX_MUX_SHIFT;

		ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
		if (ret) {
			dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}
	}

	return 0;
}

static int zynqmp_pinmux_free_pin(struct pinctrl_dev *pctldev, unsigned int pin)
{
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 addr_offset, mask;
	int ret;

	addr_offset = pctrl->iouaddr + 4 * pin;
	mask = ZYNQMP_PINMUX_MUX_MASK << ZYNQMP_PINMUX_MUX_SHIFT;

	/* Reset MIO pin mux to release it from peripheral mapping */
	ret = zynqmp_pctrl_writereg(0, addr_offset, mask);
	if (ret) {
		dev_err(pctldev->dev, "write failed at 0x%x\n", addr_offset);
		return -EIO;
	}

	return 0;
}

static const struct pinmux_ops zynqmp_pinmux_ops = {
	.get_functions_count = zynqmp_pmux_get_functions_count,
	.get_function_name = zynqmp_pmux_get_function_name,
	.get_function_groups = zynqmp_pmux_get_function_groups,
	.set_mux = zynqmp_pinmux_set_mux,
	.free = zynqmp_pinmux_free_pin,
};

/* pinconfig */
#define ZYNQMP_DRVSTRN0_REG_OFF    0
#define ZYNQMP_DRVSTRN1_REG_OFF    4
#define ZYNQMP_SCHCMOS_REG_OFF     8
#define ZYNQMP_PULLCTRL_REG_OFF    12
#define ZYNQMP_PULLSTAT_REG_OFF    16
#define ZYNQMP_SLEWCTRL_REG_OFF    20
#define ZYNQMP_IOSTAT_REG_OFF      24
#define MAX_PIN_PER_REG            26
#define ZYNQMP_BANK_ADDR_STEP      28

#define ZYNQMP_ADDR_OFFSET(addr, reg, pin) \
		((addr) + 4 * ZYNQMP_NUM_MIOS + ZYNQMP_BANK_ADDR_STEP * \
					((pin) / MAX_PIN_PER_REG) + (reg))
#define ZYNQMP_PIN_OFFSET(pin) \
			((pin) - (MAX_PIN_PER_REG * ((pin) / MAX_PIN_PER_REG)))

#define ENABLE_CONFIG_VAL(pin)      (1 << ZYNQMP_PIN_OFFSET(pin))
#define DISABLE_CONFIG_VAL(pin)     (0 << ZYNQMP_PIN_OFFSET(pin))

/**
 * enum zynqmp_pin_config_param - possible pin configuration parameters
 * @PIN_CONFIG_IOSTANDARD: if the pin can select an IO standard, the argument
 *      to this parameter (on a custom format) tells the driver which
 *      alternative IO standard to use
 * @PIN_CONFIG_SCHMITTCMOS: this parameter (on a custom format) allows to
 *      select schmitt or cmos input for MIO pins
 */
enum zynqmp_pin_config_param {
	PIN_CONFIG_IOSTANDARD = PIN_CONFIG_END + 1,
	PIN_CONFIG_SCHMITTCMOS,
};

static const struct pinconf_generic_params zynqmp_dt_params[] = {
	{"io-standard", PIN_CONFIG_IOSTANDARD, IO_STANDARD_LVCMOS18},
	{"schmitt-cmos", PIN_CONFIG_SCHMITTCMOS, PIN_INPUT_TYPE_SCHMITT},
};

#ifdef CONFIG_DEBUG_FS
static const struct
pin_config_item zynqmp_conf_items[ARRAY_SIZE(zynqmp_dt_params)] = {
	PCONFDUMP(PIN_CONFIG_IOSTANDARD, "IO-standard", NULL, true),
	PCONFDUMP(PIN_CONFIG_SCHMITTCMOS, "schmitt-cmos", NULL, true),
};
#endif

static int zynqmp_pinconf_cfg_get(struct pinctrl_dev *pctldev,
					unsigned int pin, unsigned long *config)
{
	int ret;
	u32 reg, bit0, bit1, addr_offset;
	unsigned int arg = 0, param = pinconf_to_config_param(*config);
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	if (pin >= ZYNQMP_NUM_MIOS)
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_SLEW_RATE:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_SLEWCTRL_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		arg = reg & (1 << ZYNQMP_PIN_OFFSET(pin));
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLCTRL_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		if (!(reg & (1 << ZYNQMP_PIN_OFFSET(pin))))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLCTRL_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		if (reg & (1 << ZYNQMP_PIN_OFFSET(pin)))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLSTAT_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		if (reg & (1 << ZYNQMP_PIN_OFFSET(pin)))
			return -EINVAL;
		break;
	case PIN_CONFIG_IOSTANDARD:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_IOSTAT_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		arg = reg & ZYNQMP_IOSTD_BIT_MASK;
		break;
	case PIN_CONFIG_SCHMITTCMOS:
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_SCHCMOS_REG_OFF, pin);

		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		arg = reg & (1 << ZYNQMP_PIN_OFFSET(pin));
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		/* Drive strength configurations are distributed in 2 registers
		 * and requires to be merged
		 */
		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_DRVSTRN0_REG_OFF, pin);
		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		bit1 = (reg & (1 << ZYNQMP_PIN_OFFSET(pin))) >>
							ZYNQMP_PIN_OFFSET(pin);

		addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_DRVSTRN1_REG_OFF, pin);
		ret = zynqmp_pctrl_readreg(&reg, addr_offset);
		if (ret) {
			dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
			return -EIO;
		}

		bit0 = (reg & (1 << ZYNQMP_PIN_OFFSET(pin))) >>
							ZYNQMP_PIN_OFFSET(pin);

		arg = (bit1 << 1) | bit0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int zynqmp_pinconf_cfg_set(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *configs,
				unsigned int num_configs)
{
	int i, ret;
	u32 reg, reg2, addr_offset, mask;
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	if (pin >= ZYNQMP_NUM_MIOS)
		return -ENOTSUPP;

	mask = 1 << ZYNQMP_PIN_OFFSET(pin);

	for (i = 0; i < num_configs; i++) {
		unsigned int param = pinconf_to_config_param(configs[i]);
		unsigned int arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_SLEW_RATE:
			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_SLEWCTRL_REG_OFF, pin);

			if (arg != SLEW_RATE_SLOW && arg != SLEW_RATE_FAST) {
				dev_warn(pctldev->dev,
				"Invalid Slew rate requested for pin %d\n",
				 pin);
				break;
			}

			if (arg == SLEW_RATE_SLOW)
				reg = ENABLE_CONFIG_VAL(pin);
			else
				reg = DISABLE_CONFIG_VAL(pin);

			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret) {
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			}
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLSTAT_REG_OFF, pin);

			reg = ENABLE_CONFIG_VAL(pin);
			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret) {
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
				break;
			}

			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLCTRL_REG_OFF, pin);

			if (param == PIN_CONFIG_BIAS_PULL_DOWN)
				reg = DISABLE_CONFIG_VAL(pin);

			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret)
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_PULLSTAT_REG_OFF, pin);

			reg = DISABLE_CONFIG_VAL(pin);
			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret)
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			break;
		case PIN_CONFIG_SCHMITTCMOS:
			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_SCHCMOS_REG_OFF, pin);

			if (arg != PIN_INPUT_TYPE_CMOS &&
					arg != PIN_INPUT_TYPE_SCHMITT) {
				dev_warn(pctldev->dev,
				"Invalid input type requested for pin %d\n",
				pin);
				break;
			}

			if (arg == PIN_INPUT_TYPE_SCHMITT)
				reg = ENABLE_CONFIG_VAL(pin);
			else
				reg = DISABLE_CONFIG_VAL(pin);

			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret)
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			switch (arg) {
			case DRIVE_STRENGTH_2MA:
				reg = DISABLE_CONFIG_VAL(pin);
				reg2 = DISABLE_CONFIG_VAL(pin);
				break;
			case DRIVE_STRENGTH_4MA:
				reg = DISABLE_CONFIG_VAL(pin);
				reg2 = ENABLE_CONFIG_VAL(pin);
				break;
			case DRIVE_STRENGTH_8MA:
				reg = ENABLE_CONFIG_VAL(pin);
				reg2 = DISABLE_CONFIG_VAL(pin);
				break;
			case DRIVE_STRENGTH_12MA:
				reg = ENABLE_CONFIG_VAL(pin);
				reg2 = ENABLE_CONFIG_VAL(pin);
				break;
			default:
				/* Invalid drive strength */
				dev_warn(pctldev->dev,
					 "Invalid drive strength for pin %d\n",
					 pin);
				return -EINVAL;
			}

			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_DRVSTRN0_REG_OFF, pin);
			ret = zynqmp_pctrl_writereg(reg, addr_offset, mask);
			if (ret) {
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
				break;
			}

			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_DRVSTRN1_REG_OFF, pin);
			ret = zynqmp_pctrl_writereg(reg2, addr_offset, mask);
			if (ret) {
				dev_err(pctldev->dev, "write failed at 0x%x\n",
								addr_offset);
			}
			break;
		case PIN_CONFIG_IOSTANDARD:
			/* This parameter is read only, so the requested IO
			 * Standard is validated against the pre configured IO
			 * Standard and warned if mismatched
			 */
			addr_offset = ZYNQMP_ADDR_OFFSET(pctrl->iouaddr,
						ZYNQMP_IOSTAT_REG_OFF, pin);

			ret = zynqmp_pctrl_readreg(&reg, addr_offset);
			if (ret) {
				dev_err(pctldev->dev, "read failed at 0x%x\n",
								addr_offset);
				break;
			}

			reg &= ZYNQMP_IOSTD_BIT_MASK;

			if (arg != reg)
				dev_warn(pctldev->dev,
				 "Invalid IO Standard requested for pin %d\n",
				 pin);
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		case PIN_CONFIG_LOW_POWER_MODE:
			/*
			 * This cases are mentioned in dts but configurable
			 * registers are unknown. So falling through to ignore
			 * boot time warnings as of now.
			 */
			break;
		default:
			dev_warn(pctldev->dev,
				 "unsupported configuration parameter '%u'\n",
				 param);
			break;
		}
	}

	return 0;
}

static int zynqmp_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned int selector, unsigned long *configs,
				unsigned int num_configs)
{
	int i, ret;
	struct zynqmp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct zynqmp_pctrl_group *pgrp = &pctrl->groups[selector];

	for (i = 0; i < pgrp->npins; i++) {
		ret = zynqmp_pinconf_cfg_set(pctldev, pgrp->pins[i], configs,
					   num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops zynqmp_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = zynqmp_pinconf_cfg_get,
	.pin_config_set = zynqmp_pinconf_cfg_set,
	.pin_config_group_set = zynqmp_pinconf_group_set,
};

static struct pinctrl_desc zynqmp_desc = {
	.name = "zynqmp_pinctrl",
	.pins = zynqmp_pins,
	.npins = ARRAY_SIZE(zynqmp_pins),
	.pctlops = &zynqmp_pctrl_ops,
	.pmxops = &zynqmp_pinmux_ops,
	.confops = &zynqmp_pinconf_ops,
	.num_custom_params = ARRAY_SIZE(zynqmp_dt_params),
	.custom_params = zynqmp_dt_params,
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items = zynqmp_conf_items,
#endif
	.owner = THIS_MODULE,
};

static int zynqmp_pinctrl_probe(struct platform_device *pdev)
{
	int ret;
	struct zynqmp_pinctrl *pctrl;
	struct resource res;

	ret = of_address_to_resource(pdev->dev.of_node, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "no pin control resource address\n");
		return ret;
	}

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->iouaddr = res.start;
	pctrl->groups = zynqmp_pctrl_groups;
	pctrl->ngroups = ARRAY_SIZE(zynqmp_pctrl_groups);
	pctrl->funcs = zynqmp_pmux_functions;
	pctrl->nfuncs = ARRAY_SIZE(zynqmp_pmux_functions);

	pctrl->pctrl = pinctrl_register(&zynqmp_desc, &pdev->dev, pctrl);
	if (IS_ERR(pctrl->pctrl))
		return PTR_ERR(pctrl->pctrl);

	platform_set_drvdata(pdev, pctrl);

	dev_info(&pdev->dev, "zynqmp pinctrl initialized\n");

	return 0;
}

static int zynqmp_pinctrl_remove(struct platform_device *pdev)
{
	struct zynqmp_pinctrl *pctrl = platform_get_drvdata(pdev);

	pinctrl_unregister(pctrl->pctrl);

	return 0;
}

static const struct of_device_id zynqmp_pinctrl_of_match[] = {
	{ .compatible = "xlnx,pinctrl-zynqmp" },
	{ }
};

static struct platform_driver zynqmp_pinctrl_driver = {
	.driver = {
		.name = "zynqmp-pinctrl",
		.of_match_table = zynqmp_pinctrl_of_match,
	},
	.probe = zynqmp_pinctrl_probe,
	.remove = zynqmp_pinctrl_remove,
};

static int __init zynqmp_pinctrl_init(void)
{
	return platform_driver_register(&zynqmp_pinctrl_driver);
}
arch_initcall(zynqmp_pinctrl_init);

