/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Based on 5.2.0, Family Programming Guide (Sept 30, 2020)
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */
#ifndef HAVE_IDT8A340_REG
#define HAVE_IDT8A340_REG

#define SCSR_BASE			  0x20100000
#define SCSR_ADDR(x)			  ((x) & 0xffff)

#define HW_REVISION                       0x20108180
#define REV_ID                            0x007a

#define HW_DPLL_0                         (0x20108a00)
#define HW_DPLL_1                         (0x20108b00)
#define HW_DPLL_2                         (0x20108c00)
#define HW_DPLL_3                         (0x20108d00)
#define HW_DPLL_4                         (0x20108e00)
#define HW_DPLL_5                         (0x20108f00)
#define HW_DPLL_6                         (0x20109000)
#define HW_DPLL_7                         (0x20109100)

#define HW_DPLL_TOD_SW_TRIG_ADDR__0       (0x080)
#define HW_DPLL_TOD_CTRL_1                (0x089)
#define HW_DPLL_TOD_CTRL_2                (0x08A)
#define HW_DPLL_TOD_OVR__0                (0x098)
#define HW_DPLL_TOD_OUT_0__0              (0x0B0)

#define HW_Q0_Q1_CH_SYNC_CTRL_0           (0x2010a740)
#define HW_Q0_Q1_CH_SYNC_CTRL_1           (0x2010a741)
#define HW_Q2_Q3_CH_SYNC_CTRL_0           (0x2010a742)
#define HW_Q2_Q3_CH_SYNC_CTRL_1           (0x2010a743)
#define HW_Q4_Q5_CH_SYNC_CTRL_0           (0x2010a744)
#define HW_Q4_Q5_CH_SYNC_CTRL_1           (0x2010a745)
#define HW_Q6_Q7_CH_SYNC_CTRL_0           (0x2010a746)
#define HW_Q6_Q7_CH_SYNC_CTRL_1           (0x2010a747)
#define HW_Q8_CH_SYNC_CTRL_0              (0x2010a748)
#define HW_Q8_CH_SYNC_CTRL_1              (0x2010a749)
#define HW_Q9_CH_SYNC_CTRL_0              (0x2010a74a)
#define HW_Q9_CH_SYNC_CTRL_1              (0x2010a74b)
#define HW_Q10_CH_SYNC_CTRL_0             (0x2010a74c)
#define HW_Q10_CH_SYNC_CTRL_1             (0x2010a74d)
#define HW_Q11_CH_SYNC_CTRL_0             (0x2010a74e)
#define HW_Q11_CH_SYNC_CTRL_1             (0x2010a74f)

#define SYNC_SOURCE_DPLL0_TOD_PPS	0x14
#define SYNC_SOURCE_DPLL1_TOD_PPS	0x15
#define SYNC_SOURCE_DPLL2_TOD_PPS	0x16
#define SYNC_SOURCE_DPLL3_TOD_PPS	0x17

#define SYNCTRL1_MASTER_SYNC_RST	BIT(7)
#define SYNCTRL1_MASTER_SYNC_TRIG	BIT(5)
#define SYNCTRL1_TOD_SYNC_TRIG		BIT(4)
#define SYNCTRL1_FBDIV_FRAME_SYNC_TRIG	BIT(3)
#define SYNCTRL1_FBDIV_SYNC_TRIG	BIT(2)
#define SYNCTRL1_Q1_DIV_SYNC_TRIG	BIT(1)
#define SYNCTRL1_Q0_DIV_SYNC_TRIG	BIT(0)

#define HW_Q8_CTRL_SPARE  (0x2010a7d4)
#define HW_Q11_CTRL_SPARE (0x2010a7ec)

/**
 * Select FOD5 as sync_trigger for Q8 divider.
 * Transition from logic zero to one
 * sets trigger to sync Q8 divider.
 *
 * Unused when FOD4 is driving Q8 divider (normal operation).
 */
#define Q9_TO_Q8_SYNC_TRIG  BIT(1)

/**
 * Enable FOD5 as driver for clock and sync for Q8 divider.
 * Enable fanout buffer for FOD5.
 *
 * Unused when FOD4 is driving Q8 divider (normal operation).
 */
#define Q9_TO_Q8_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK  (BIT(0) | BIT(2))

/**
 * Select FOD6 as sync_trigger for Q11 divider.
 * Transition from logic zero to one
 * sets trigger to sync Q11 divider.
 *
 * Unused when FOD7 is driving Q11 divider (normal operation).
 */
#define Q10_TO_Q11_SYNC_TRIG  BIT(1)

/**
 * Enable FOD6 as driver for clock and sync for Q11 divider.
 * Enable fanout buffer for FOD6.
 *
 * Unused when FOD7 is driving Q11 divider (normal operation).
 */
#define Q10_TO_Q11_FANOUT_AND_CLOCK_SYNC_ENABLE_MASK  (BIT(0) | BIT(2))

#define RESET_CTRL                        0x2010c000
#define SM_RESET                          0x0012
#define SM_RESET_V520                     0x0013
#define SM_RESET_CMD                      0x5A

#define GENERAL_STATUS                    0x2010c014
#define BOOT_STATUS                       0x0000
#define HW_REV_ID                         0x000A
#define BOND_ID                           0x000B
#define HW_CSR_ID                         0x000C
#define HW_IRQ_ID                         0x000E
#define MAJ_REL                           0x0010
#define MIN_REL                           0x0011
#define HOTFIX_REL                        0x0012
#define PIPELINE_ID                       0x0014
#define BUILD_ID                          0x0018
#define JTAG_DEVICE_ID                    0x001c
#define PRODUCT_ID                        0x001e
#define OTP_SCSR_CONFIG_SELECT            0x0022

#define STATUS                            0x2010c03c
#define IN0_MON_STATUS                    0x0008
#define IN1_MON_STATUS                    0x0009
#define IN2_MON_STATUS                    0x000a
#define IN3_MON_STATUS                    0x000b
#define IN4_MON_STATUS                    0x000c
#define IN5_MON_STATUS                    0x000d
#define IN6_MON_STATUS                    0x000e
#define IN7_MON_STATUS                    0x000f
#define IN8_MON_STATUS                    0x0010
#define IN9_MON_STATUS                    0x0011
#define IN10_MON_STATUS                   0x0012
#define IN11_MON_STATUS                   0x0013
#define IN12_MON_STATUS                   0x0014
#define IN13_MON_STATUS                   0x0015
#define IN14_MON_STATUS                   0x0016
#define IN15_MON_STATUS                   0x0017
#define DPLL0_STATUS                      0x0018
#define DPLL1_STATUS                      0x0019
#define DPLL2_STATUS                      0x001a
#define DPLL3_STATUS                      0x001b
#define DPLL4_STATUS                      0x001c
#define DPLL5_STATUS                      0x001d
#define DPLL6_STATUS                      0x001e
#define DPLL7_STATUS                      0x001f
#define DPLL_SYS_STATUS                   0x0020
#define DPLL_SYS_APLL_STATUS              0x0021
#define DPLL0_REF_STATUS                  0x0022
#define DPLL1_REF_STATUS                  0x0023
#define DPLL2_REF_STATUS                  0x0024
#define DPLL3_REF_STATUS                  0x0025
#define DPLL4_REF_STATUS                  0x0026
#define DPLL5_REF_STATUS                  0x0027
#define DPLL6_REF_STATUS                  0x0028
#define DPLL7_REF_STATUS                  0x0029
#define DPLL_SYS_REF_STATUS               0x002a
#define DPLL0_FILTER_STATUS               0x0044
#define DPLL1_FILTER_STATUS               0x004c
#define DPLL2_FILTER_STATUS               0x0054
#define DPLL3_FILTER_STATUS               0x005c
#define DPLL4_FILTER_STATUS               0x0064
#define DPLL5_FILTER_STATUS               0x006c
#define DPLL6_FILTER_STATUS               0x0074
#define DPLL7_FILTER_STATUS               0x007c
#define DPLLSYS_FILTER_STATUS             0x0084
#define USER_GPIO0_TO_7_STATUS            0x008a
#define USER_GPIO8_TO_15_STATUS           0x008b

#define GPIO_USER_CONTROL                 0x2010c160
#define GPIO0_TO_7_OUT                    0x0000
#define GPIO8_TO_15_OUT                   0x0001
#define GPIO0_TO_7_OUT_V520               0x0002
#define GPIO8_TO_15_OUT_V520              0x0003

#define STICKY_STATUS_CLEAR               0x2010c164

#define GPIO_TOD_NOTIFICATION_CLEAR       0x2010c16c

#define ALERT_CFG                         0x2010c188

#define SYS_DPLL_XO                       0x2010c194

#define SYS_APLL                          0x2010c19c

#define INPUT_0                           0x2010c1b0
#define INPUT_1                           0x2010c1c0
#define INPUT_2                           0x2010c1d0
#define INPUT_3                           0x2010c200
#define INPUT_4                           0x2010c210
#define INPUT_5                           0x2010c220
#define INPUT_6                           0x2010c230
#define INPUT_7                           0x2010c240
#define INPUT_8                           0x2010c250
#define INPUT_9                           0x2010c260
#define INPUT_10                          0x2010c280
#define INPUT_11                          0x2010c290
#define INPUT_12                          0x2010c2a0
#define INPUT_13                          0x2010c2b0
#define INPUT_14                          0x2010c2c0
#define INPUT_15                          0x2010c2d0

#define REF_MON_0                         0x2010c2e0
#define REF_MON_1                         0x2010c2ec
#define REF_MON_2                         0x2010c300
#define REF_MON_3                         0x2010c30c
#define REF_MON_4                         0x2010c318
#define REF_MON_5                         0x2010c324
#define REF_MON_6                         0x2010c330
#define REF_MON_7                         0x2010c33c
#define REF_MON_8                         0x2010c348
#define REF_MON_9                         0x2010c354
#define REF_MON_10                        0x2010c360
#define REF_MON_11                        0x2010c36c
#define REF_MON_12                        0x2010c380
#define REF_MON_13                        0x2010c38c
#define REF_MON_14                        0x2010c398
#define REF_MON_15                        0x2010c3a4

#define DPLL_0                            0x2010c3b0
#define DPLL_CTRL_REG_0                   0x0002
#define DPLL_CTRL_REG_1                   0x0003
#define DPLL_CTRL_REG_2                   0x0004
#define DPLL_REF_PRIORITY_0               0x000f
#define DPLL_REF_PRIORITY_1               0x0010
#define DPLL_REF_PRIORITY_2               0x0011
#define DPLL_REF_PRIORITY_3               0x0012
#define DPLL_REF_PRIORITY_4               0x0013
#define DPLL_REF_PRIORITY_5               0x0014
#define DPLL_REF_PRIORITY_6               0x0015
#define DPLL_REF_PRIORITY_7               0x0016
#define DPLL_REF_PRIORITY_8               0x0017
#define DPLL_REF_PRIORITY_9               0x0018
#define DPLL_REF_PRIORITY_10              0x0019
#define DPLL_REF_PRIORITY_11              0x001a
#define DPLL_REF_PRIORITY_12              0x001b
#define DPLL_REF_PRIORITY_13              0x001c
#define DPLL_REF_PRIORITY_14              0x001d
#define DPLL_REF_PRIORITY_15              0x001e
#define DPLL_REF_PRIORITY_16              0x001f
#define DPLL_REF_PRIORITY_17              0x0020
#define DPLL_REF_PRIORITY_18              0x0021
#define DPLL_MAX_FREQ_OFFSET              0x0025
#define DPLL_WF_TIMER                     0x002c
#define DPLL_WP_TIMER                     0x002e
#define DPLL_TOD_SYNC_CFG                 0x0031
#define DPLL_COMBO_SLAVE_CFG_0            0x0032
#define DPLL_COMBO_SLAVE_CFG_1            0x0033
#define DPLL_SLAVE_REF_CFG                0x0034
#define DPLL_REF_MODE                     0x0035
#define DPLL_PHASE_MEASUREMENT_CFG        0x0036
#define DPLL_MODE                         0x0037
#define DPLL_MODE_V520                    0x003B
#define DPLL_1                            0x2010c400
#define DPLL_2                            0x2010c438
#define DPLL_2_V520                       0x2010c43c
#define DPLL_3                            0x2010c480
#define DPLL_4                            0x2010c4b8
#define DPLL_4_V520                       0x2010c4bc
#define DPLL_5                            0x2010c500
#define DPLL_6                            0x2010c538
#define DPLL_6_V520                       0x2010c53c
#define DPLL_7                            0x2010c580
#define SYS_DPLL                          0x2010c5b8
#define SYS_DPLL_V520                     0x2010c5bc

#define DPLL_CTRL_0                       0x2010c600
#define DPLL_CTRL_DPLL_MANU_REF_CFG       0x0001
#define DPLL_CTRL_DPLL_FOD_FREQ           0x001c
#define DPLL_CTRL_COMBO_MASTER_CFG        0x003a
#define DPLL_CTRL_1                       0x2010c63c
#define DPLL_CTRL_2                       0x2010c680
#define DPLL_CTRL_3                       0x2010c6bc
#define DPLL_CTRL_4                       0x2010c700
#define DPLL_CTRL_5                       0x2010c73c
#define DPLL_CTRL_6                       0x2010c780
#define DPLL_CTRL_7                       0x2010c7bc
#define SYS_DPLL_CTRL                     0x2010c800

#define DPLL_PHASE_0                      0x2010c818
/* Signed 42-bit FFO in units of 2^(-53) */
#define DPLL_WR_PHASE                     0x0000
#define DPLL_PHASE_1                      0x2010c81c
#define DPLL_PHASE_2                      0x2010c820
#define DPLL_PHASE_3                      0x2010c824
#define DPLL_PHASE_4                      0x2010c828
#define DPLL_PHASE_5                      0x2010c82c
#define DPLL_PHASE_6                      0x2010c830
#define DPLL_PHASE_7                      0x2010c834

#define DPLL_FREQ_0                       0x2010c838
/* Signed 42-bit FFO in units of 2^(-53) */
#define DPLL_WR_FREQ                      0x0000
#define DPLL_FREQ_1                       0x2010c840
#define DPLL_FREQ_2                       0x2010c848
#define DPLL_FREQ_3                       0x2010c850
#define DPLL_FREQ_4                       0x2010c858
#define DPLL_FREQ_5                       0x2010c860
#define DPLL_FREQ_6                       0x2010c868
#define DPLL_FREQ_7                       0x2010c870

#define DPLL_PHASE_PULL_IN_0              0x2010c880
#define PULL_IN_OFFSET                    0x0000 /* Signed 32 bit */
#define PULL_IN_SLOPE_LIMIT               0x0004 /* Unsigned 24 bit */
#define PULL_IN_CTRL                      0x0007
#define DPLL_PHASE_PULL_IN_1              0x2010c888
#define DPLL_PHASE_PULL_IN_2              0x2010c890
#define DPLL_PHASE_PULL_IN_3              0x2010c898
#define DPLL_PHASE_PULL_IN_4              0x2010c8a0
#define DPLL_PHASE_PULL_IN_5              0x2010c8a8
#define DPLL_PHASE_PULL_IN_6              0x2010c8b0
#define DPLL_PHASE_PULL_IN_7              0x2010c8b8

#define GPIO_CFG                          0x2010c8c0
#define GPIO_CFG_GBL                      0x0000
#define GPIO_0                            0x2010c8c2
#define GPIO_DCO_INC_DEC                  0x0000
#define GPIO_OUT_CTRL_0                   0x0001
#define GPIO_OUT_CTRL_1                   0x0002
#define GPIO_TOD_TRIG                     0x0003
#define GPIO_DPLL_INDICATOR               0x0004
#define GPIO_LOS_INDICATOR                0x0005
#define GPIO_REF_INPUT_DSQ_0              0x0006
#define GPIO_REF_INPUT_DSQ_1              0x0007
#define GPIO_REF_INPUT_DSQ_2              0x0008
#define GPIO_REF_INPUT_DSQ_3              0x0009
#define GPIO_MAN_CLK_SEL_0                0x000a
#define GPIO_MAN_CLK_SEL_1                0x000b
#define GPIO_MAN_CLK_SEL_2                0x000c
#define GPIO_SLAVE                        0x000d
#define GPIO_ALERT_OUT_CFG                0x000e
#define GPIO_TOD_NOTIFICATION_CFG         0x000f
#define GPIO_CTRL                         0x0010
#define GPIO_CTRL_V520                    0x0011
#define GPIO_1                            0x2010c8d4
#define GPIO_2                            0x2010c8e6
#define GPIO_3                            0x2010c900
#define GPIO_4                            0x2010c912
#define GPIO_5                            0x2010c924
#define GPIO_6                            0x2010c936
#define GPIO_7                            0x2010c948
#define GPIO_8                            0x2010c95a
#define GPIO_9                            0x2010c980
#define GPIO_10                           0x2010c992
#define GPIO_11                           0x2010c9a4
#define GPIO_12                           0x2010c9b6
#define GPIO_13                           0x2010c9c8
#define GPIO_14                           0x2010c9da
#define GPIO_15                           0x2010ca00

#define OUT_DIV_MUX                       0x2010ca12
#define OUTPUT_0                          0x2010ca14
#define OUTPUT_0_V520                     0x2010ca20
/* FOD frequency output divider value */
#define OUT_DIV                           0x0000
#define OUT_DUTY_CYCLE_HIGH               0x0004
#define OUT_CTRL_0                        0x0008
#define OUT_CTRL_1                        0x0009
/* Phase adjustment in FOD cycles */
#define OUT_PHASE_ADJ                     0x000c
#define OUTPUT_1                          0x2010ca24
#define OUTPUT_1_V520                     0x2010ca30
#define OUTPUT_2                          0x2010ca34
#define OUTPUT_2_V520                     0x2010ca40
#define OUTPUT_3                          0x2010ca44
#define OUTPUT_3_V520                     0x2010ca50
#define OUTPUT_4                          0x2010ca54
#define OUTPUT_4_V520                     0x2010ca60
#define OUTPUT_5                          0x2010ca64
#define OUTPUT_5_V520                     0x2010ca80
#define OUTPUT_6                          0x2010ca80
#define OUTPUT_6_V520                     0x2010ca90
#define OUTPUT_7                          0x2010ca90
#define OUTPUT_7_V520                     0x2010caa0
#define OUTPUT_8                          0x2010caa0
#define OUTPUT_8_V520                     0x2010cab0
#define OUTPUT_9                          0x2010cab0
#define OUTPUT_9_V520                     0x2010cac0
#define OUTPUT_10                         0x2010cac0
#define OUTPUT_10_V520                    0x2010cad0
#define OUTPUT_11                         0x2010cad0
#define OUTPUT_11_V520                    0x2010cae0

#define SERIAL                            0x2010cae0
#define SERIAL_V520                       0x2010caf0

#define PWM_ENCODER_0                     0x2010cb00
#define PWM_ENCODER_1                     0x2010cb08
#define PWM_ENCODER_2                     0x2010cb10
#define PWM_ENCODER_3                     0x2010cb18
#define PWM_ENCODER_4                     0x2010cb20
#define PWM_ENCODER_5                     0x2010cb28
#define PWM_ENCODER_6                     0x2010cb30
#define PWM_ENCODER_7                     0x2010cb38
#define PWM_DECODER_0                     0x2010cb40
#define PWM_DECODER_1                     0x2010cb48
#define PWM_DECODER_1_V520                0x2010cb4a
#define PWM_DECODER_2                     0x2010cb50
#define PWM_DECODER_2_V520                0x2010cb54
#define PWM_DECODER_3                     0x2010cb58
#define PWM_DECODER_3_V520                0x2010cb5e
#define PWM_DECODER_4                     0x2010cb60
#define PWM_DECODER_4_V520                0x2010cb68
#define PWM_DECODER_5                     0x2010cb68
#define PWM_DECODER_5_V520                0x2010cb80
#define PWM_DECODER_6                     0x2010cb70
#define PWM_DECODER_6_V520                0x2010cb8a
#define PWM_DECODER_7                     0x2010cb80
#define PWM_DECODER_7_V520                0x2010cb94
#define PWM_DECODER_8                     0x2010cb88
#define PWM_DECODER_8_V520                0x2010cb9e
#define PWM_DECODER_9                     0x2010cb90
#define PWM_DECODER_9_V520                0x2010cba8
#define PWM_DECODER_10                    0x2010cb98
#define PWM_DECODER_10_V520               0x2010cbb2
#define PWM_DECODER_11                    0x2010cba0
#define PWM_DECODER_11_V520               0x2010cbbc
#define PWM_DECODER_12                    0x2010cba8
#define PWM_DECODER_12_V520               0x2010cbc6
#define PWM_DECODER_13                    0x2010cbb0
#define PWM_DECODER_13_V520               0x2010cbd0
#define PWM_DECODER_14                    0x2010cbb8
#define PWM_DECODER_14_V520               0x2010cbda
#define PWM_DECODER_15                    0x2010cbc0
#define PWM_DECODER_15_V520               0x2010cbe4
#define PWM_USER_DATA                     0x2010cbc8
#define PWM_USER_DATA_V520                0x2010cbf0

#define TOD_0                             0x2010cbcc
#define TOD_0_V520                        0x2010cc00
/* Enable TOD counter, output channel sync and even-PPS mode */
#define TOD_CFG                           0x0000
#define TOD_CFG_V520                      0x0001
#define TOD_1                             0x2010cbce
#define TOD_1_V520                        0x2010cc02
#define TOD_2                             0x2010cbd0
#define TOD_2_V520                        0x2010cc04
#define TOD_3                             0x2010cbd2
#define TOD_3_V520                        0x2010cc06

#define TOD_WRITE_0                       0x2010cc00
#define TOD_WRITE_0_V520                  0x2010cc10
/* 8-bit subns, 32-bit ns, 48-bit seconds */
#define TOD_WRITE                         0x0000
/* Counter increments after TOD write is completed */
#define TOD_WRITE_COUNTER                 0x000c
/* TOD write trigger configuration */
#define TOD_WRITE_SELECT_CFG_0            0x000d
/* TOD write trigger selection */
#define TOD_WRITE_CMD                     0x000f
#define TOD_WRITE_1                       0x2010cc10
#define TOD_WRITE_1_V520                  0x2010cc20
#define TOD_WRITE_2                       0x2010cc20
#define TOD_WRITE_2_V520                  0x2010cc30
#define TOD_WRITE_3                       0x2010cc30
#define TOD_WRITE_3_V520                  0x2010cc40

#define TOD_READ_PRIMARY_0                0x2010cc40
#define TOD_READ_PRIMARY_0_V520           0x2010cc50
/* 8-bit subns, 32-bit ns, 48-bit seconds */
#define TOD_READ_PRIMARY_BASE             0x0000
/* Counter increments after TOD write is completed */
#define TOD_READ_PRIMARY_COUNTER          0x000b
/* Read trigger configuration */
#define TOD_READ_PRIMARY_SEL_CFG_0        0x000c
/* Read trigger selection */
#define TOD_READ_PRIMARY_CMD              0x000e
#define TOD_READ_PRIMARY_CMD_V520         0x000f
#define TOD_READ_PRIMARY_1                0x2010cc50
#define TOD_READ_PRIMARY_1_V520           0x2010cc60
#define TOD_READ_PRIMARY_2                0x2010cc60
#define TOD_READ_PRIMARY_2_V520           0x2010cc80
#define TOD_READ_PRIMARY_3                0x2010cc80
#define TOD_READ_PRIMARY_3_V520           0x2010cc90

#define TOD_READ_SECONDARY_0              0x2010cc90
#define TOD_READ_SECONDARY_0_V520         0x2010cca0
/* 8-bit subns, 32-bit ns, 48-bit seconds */
#define TOD_READ_SECONDARY_BASE           0x0000
/* Counter increments after TOD write is completed */
#define TOD_READ_SECONDARY_COUNTER        0x000b
/* Read trigger configuration */
#define TOD_READ_SECONDARY_SEL_CFG_0      0x000c
/* Read trigger selection */
#define TOD_READ_SECONDARY_CMD            0x000e
#define TOD_READ_SECONDARY_CMD_V520       0x000f

#define TOD_READ_SECONDARY_1              0x2010cca0
#define TOD_READ_SECONDARY_1_V520         0x2010ccb0
#define TOD_READ_SECONDARY_2              0x2010ccb0
#define TOD_READ_SECONDARY_2_V520         0x2010ccc0
#define TOD_READ_SECONDARY_3              0x2010ccc0
#define TOD_READ_SECONDARY_3_V520         0x2010ccd0

#define OUTPUT_TDC_CFG                    0x2010ccd0
#define OUTPUT_TDC_CFG_V520               0x2010cce0
#define OUTPUT_TDC_0                      0x2010cd00
#define OUTPUT_TDC_1                      0x2010cd08
#define OUTPUT_TDC_2                      0x2010cd10
#define OUTPUT_TDC_3                      0x2010cd18

#define OUTPUT_TDC_CTRL_4                 0x0006
#define OUTPUT_TDC_CTRL_4_V520            0x0007

#define INPUT_TDC                         0x2010cd20

#define SCRATCH                           0x2010cf50
#define SCRATCH_V520                      0x2010cf4c

#define EEPROM                            0x2010cf68
#define EEPROM_V520                       0x2010cf64

#define OTP                               0x2010cf70

#define BYTE                              0x2010cf80

/* Bit definitions for the MAJ_REL register */
#define MAJOR_SHIFT                       (1)
#define MAJOR_MASK                        (0x7f)
#define PR_BUILD                          BIT(0)

/* Bit definitions for the USER_GPIO0_TO_7_STATUS register */
#define GPIO0_LEVEL                       BIT(0)
#define GPIO1_LEVEL                       BIT(1)
#define GPIO2_LEVEL                       BIT(2)
#define GPIO3_LEVEL                       BIT(3)
#define GPIO4_LEVEL                       BIT(4)
#define GPIO5_LEVEL                       BIT(5)
#define GPIO6_LEVEL                       BIT(6)
#define GPIO7_LEVEL                       BIT(7)

/* Bit definitions for the USER_GPIO8_TO_15_STATUS register */
#define GPIO8_LEVEL                       BIT(0)
#define GPIO9_LEVEL                       BIT(1)
#define GPIO10_LEVEL                      BIT(2)
#define GPIO11_LEVEL                      BIT(3)
#define GPIO12_LEVEL                      BIT(4)
#define GPIO13_LEVEL                      BIT(5)
#define GPIO14_LEVEL                      BIT(6)
#define GPIO15_LEVEL                      BIT(7)

/* Bit definitions for the GPIO0_TO_7_OUT register */
#define GPIO0_DRIVE_LEVEL                 BIT(0)
#define GPIO1_DRIVE_LEVEL                 BIT(1)
#define GPIO2_DRIVE_LEVEL                 BIT(2)
#define GPIO3_DRIVE_LEVEL                 BIT(3)
#define GPIO4_DRIVE_LEVEL                 BIT(4)
#define GPIO5_DRIVE_LEVEL                 BIT(5)
#define GPIO6_DRIVE_LEVEL                 BIT(6)
#define GPIO7_DRIVE_LEVEL                 BIT(7)

/* Bit definitions for the GPIO8_TO_15_OUT register */
#define GPIO8_DRIVE_LEVEL                 BIT(0)
#define GPIO9_DRIVE_LEVEL                 BIT(1)
#define GPIO10_DRIVE_LEVEL                BIT(2)
#define GPIO11_DRIVE_LEVEL                BIT(3)
#define GPIO12_DRIVE_LEVEL                BIT(4)
#define GPIO13_DRIVE_LEVEL                BIT(5)
#define GPIO14_DRIVE_LEVEL                BIT(6)
#define GPIO15_DRIVE_LEVEL                BIT(7)

/* Bit definitions for the DPLL_TOD_SYNC_CFG register */
#define TOD_SYNC_SOURCE_SHIFT             (1)
#define TOD_SYNC_SOURCE_MASK              (0x3)
#define TOD_SYNC_EN                       BIT(0)

/* Bit definitions for the DPLL_MODE register */
#define WRITE_TIMER_MODE                  BIT(6)
#define PLL_MODE_SHIFT                    (3)
#define PLL_MODE_MASK                     (0x7)
#define STATE_MODE_SHIFT                  (0)
#define STATE_MODE_MASK                   (0x7)

/* Bit definitions for the DPLL_MANU_REF_CFG register */
#define MANUAL_REFERENCE_SHIFT            (0)
#define MANUAL_REFERENCE_MASK             (0x1f)

/* Bit definitions for the GPIO_CFG_GBL register */
#define SUPPLY_MODE_SHIFT                 (0)
#define SUPPLY_MODE_MASK                  (0x3)

/* Bit definitions for the GPIO_DCO_INC_DEC register */
#define INCDEC_DPLL_INDEX_SHIFT           (0)
#define INCDEC_DPLL_INDEX_MASK            (0x7)

/* Bit definitions for the GPIO_OUT_CTRL_0 register */
#define CTRL_OUT_0                        BIT(0)
#define CTRL_OUT_1                        BIT(1)
#define CTRL_OUT_2                        BIT(2)
#define CTRL_OUT_3                        BIT(3)
#define CTRL_OUT_4                        BIT(4)
#define CTRL_OUT_5                        BIT(5)
#define CTRL_OUT_6                        BIT(6)
#define CTRL_OUT_7                        BIT(7)

/* Bit definitions for the GPIO_OUT_CTRL_1 register */
#define CTRL_OUT_8                        BIT(0)
#define CTRL_OUT_9                        BIT(1)
#define CTRL_OUT_10                       BIT(2)
#define CTRL_OUT_11                       BIT(3)
#define CTRL_OUT_12                       BIT(4)
#define CTRL_OUT_13                       BIT(5)
#define CTRL_OUT_14                       BIT(6)
#define CTRL_OUT_15                       BIT(7)

/* Bit definitions for the GPIO_TOD_TRIG register */
#define TOD_TRIG_0                        BIT(0)
#define TOD_TRIG_1                        BIT(1)
#define TOD_TRIG_2                        BIT(2)
#define TOD_TRIG_3                        BIT(3)

/* Bit definitions for the GPIO_DPLL_INDICATOR register */
#define IND_DPLL_INDEX_SHIFT              (0)
#define IND_DPLL_INDEX_MASK               (0x7)

/* Bit definitions for the GPIO_LOS_INDICATOR register */
#define REFMON_INDEX_SHIFT                (0)
#define REFMON_INDEX_MASK                 (0xf)
/* Active level of LOS indicator, 0=low 1=high */
#define ACTIVE_LEVEL                      BIT(4)

/* Bit definitions for the GPIO_REF_INPUT_DSQ_0 register */
#define DSQ_INP_0                         BIT(0)
#define DSQ_INP_1                         BIT(1)
#define DSQ_INP_2                         BIT(2)
#define DSQ_INP_3                         BIT(3)
#define DSQ_INP_4                         BIT(4)
#define DSQ_INP_5                         BIT(5)
#define DSQ_INP_6                         BIT(6)
#define DSQ_INP_7                         BIT(7)

/* Bit definitions for the GPIO_REF_INPUT_DSQ_1 register */
#define DSQ_INP_8                         BIT(0)
#define DSQ_INP_9                         BIT(1)
#define DSQ_INP_10                        BIT(2)
#define DSQ_INP_11                        BIT(3)
#define DSQ_INP_12                        BIT(4)
#define DSQ_INP_13                        BIT(5)
#define DSQ_INP_14                        BIT(6)
#define DSQ_INP_15                        BIT(7)

/* Bit definitions for the GPIO_REF_INPUT_DSQ_2 register */
#define DSQ_DPLL_0                        BIT(0)
#define DSQ_DPLL_1                        BIT(1)
#define DSQ_DPLL_2                        BIT(2)
#define DSQ_DPLL_3                        BIT(3)
#define DSQ_DPLL_4                        BIT(4)
#define DSQ_DPLL_5                        BIT(5)
#define DSQ_DPLL_6                        BIT(6)
#define DSQ_DPLL_7                        BIT(7)

/* Bit definitions for the GPIO_REF_INPUT_DSQ_3 register */
#define DSQ_DPLL_SYS                      BIT(0)
#define GPIO_DSQ_LEVEL                    BIT(1)

/* Bit definitions for the GPIO_TOD_NOTIFICATION_CFG register */
#define DPLL_TOD_SHIFT                    (0)
#define DPLL_TOD_MASK                     (0x3)
#define TOD_READ_SECONDARY                BIT(2)
#define GPIO_ASSERT_LEVEL                 BIT(3)

/* Bit definitions for the GPIO_CTRL register */
#define GPIO_FUNCTION_EN                  BIT(0)
#define GPIO_CMOS_OD_MODE                 BIT(1)
#define GPIO_CONTROL_DIR                  BIT(2)
#define GPIO_PU_PD_MODE                   BIT(3)
#define GPIO_FUNCTION_SHIFT               (4)
#define GPIO_FUNCTION_MASK                (0xf)

/* Bit definitions for the OUT_CTRL_1 register */
#define OUT_SYNC_DISABLE                  BIT(7)
#define SQUELCH_VALUE                     BIT(6)
#define SQUELCH_DISABLE                   BIT(5)
#define PAD_VDDO_SHIFT                    (2)
#define PAD_VDDO_MASK                     (0x7)
#define PAD_CMOSDRV_SHIFT                 (0)
#define PAD_CMOSDRV_MASK                  (0x3)

/* Bit definitions for the TOD_CFG register */
#define TOD_EVEN_PPS_MODE                 BIT(2)
#define TOD_OUT_SYNC_ENABLE               BIT(1)
#define TOD_ENABLE                        BIT(0)

/* Bit definitions for the TOD_WRITE_SELECT_CFG_0 register */
#define WR_PWM_DECODER_INDEX_SHIFT        (4)
#define WR_PWM_DECODER_INDEX_MASK         (0xf)
#define WR_REF_INDEX_SHIFT                (0)
#define WR_REF_INDEX_MASK                 (0xf)

/* Bit definitions for the TOD_WRITE_CMD register */
#define TOD_WRITE_SELECTION_SHIFT         (0)
#define TOD_WRITE_SELECTION_MASK          (0xf)
/* 4.8.7 */
#define TOD_WRITE_TYPE_SHIFT              (4)
#define TOD_WRITE_TYPE_MASK               (0x3)

/* Bit definitions for the TOD_READ_PRIMARY_SEL_CFG_0 register */
#define RD_PWM_DECODER_INDEX_SHIFT        (4)
#define RD_PWM_DECODER_INDEX_MASK         (0xf)
#define RD_REF_INDEX_SHIFT                (0)
#define RD_REF_INDEX_MASK                 (0xf)

/* Bit definitions for the TOD_READ_PRIMARY_CMD register */
#define TOD_READ_TRIGGER_MODE             BIT(4)
#define TOD_READ_TRIGGER_SHIFT            (0)
#define TOD_READ_TRIGGER_MASK             (0xf)

/* Bit definitions for the DPLL_CTRL_COMBO_MASTER_CFG register */
#define COMBO_MASTER_HOLD                 BIT(0)

/* Bit definitions for DPLL_SYS_STATUS register */
#define DPLL_SYS_STATE_MASK               (0xf)

/* Bit definitions for SYS_APLL_STATUS register */
#define SYS_APLL_LOSS_LOCK_LIVE_MASK       BIT(0)
#define SYS_APLL_LOSS_LOCK_LIVE_LOCKED     0
#define SYS_APLL_LOSS_LOCK_LIVE_UNLOCKED   1

/* Bit definitions for the DPLL0_STATUS register */
#define DPLL_STATE_MASK                   (0xf)
#define DPLL_STATE_SHIFT                  (0x0)

/* Bit definitions for the DPLL0_REF_STAT register */
#define DPLL_REF_STATUS_MASK              (0x1f)

/* Bit definitions for the DPLL register */
#define DPLL_REF_PRIORITY_ENABLE_SHIFT       (0)
#define DPLL_REF_PRIORITY_REF_SHIFT          (1)
#define DPLL_REF_PRIORITY_GROUP_NUMBER_SHIFT (6)

/* Bit definitions for the IN0_MON_STATUS register */
#define IN_MON_STATUS_LOS_SHIFT       (0)
#define IN_MON_STATUS_NO_ACT_SHIFT    (1)
#define IN_MON_STATUS_FFO_LIMIT_SHIFT (2)

#define DEFAULT_PRIORITY_GROUP (0)
#define MAX_PRIORITY_GROUP     (3)

#define MAX_REF_PRIORITIES (19)

#define MAX_ELECTRICAL_REFERENCES (16)

#define NO_REFERENCE (0x1f)

/*
 * Return register address based on passed in firmware version
 */
#define IDTCM_FW_REG(FW, VER, REG)	(((FW) < (VER)) ? (REG) : (REG##_##VER))
enum fw_version {
	V_DEFAULT = 0,
	V487 = 1,
	V520 = 2,
};

/* Values of DPLL_N.DPLL_MODE.PLL_MODE */
enum pll_mode {
	PLL_MODE_MIN = 0,
	PLL_MODE_PLL = PLL_MODE_MIN,
	PLL_MODE_WRITE_PHASE = 1,
	PLL_MODE_WRITE_FREQUENCY = 2,
	PLL_MODE_GPIO_INC_DEC = 3,
	PLL_MODE_SYNTHESIS = 4,
	PLL_MODE_PHASE_MEASUREMENT = 5,
	PLL_MODE_DISABLED = 6,
	PLL_MODE_MAX = PLL_MODE_DISABLED,
};

/* Values of DPLL_CTRL_n.DPLL_MANU_REF_CFG.MANUAL_REFERENCE */
enum manual_reference {
	MANU_REF_MIN = 0,
	MANU_REF_CLK0 = MANU_REF_MIN,
	MANU_REF_CLK1,
	MANU_REF_CLK2,
	MANU_REF_CLK3,
	MANU_REF_CLK4,
	MANU_REF_CLK5,
	MANU_REF_CLK6,
	MANU_REF_CLK7,
	MANU_REF_CLK8,
	MANU_REF_CLK9,
	MANU_REF_CLK10,
	MANU_REF_CLK11,
	MANU_REF_CLK12,
	MANU_REF_CLK13,
	MANU_REF_CLK14,
	MANU_REF_CLK15,
	MANU_REF_WRITE_PHASE,
	MANU_REF_WRITE_FREQUENCY,
	MANU_REF_XO_DPLL,
	MANU_REF_MAX = MANU_REF_XO_DPLL,
};

enum hw_tod_write_trig_sel {
	HW_TOD_WR_TRIG_SEL_MIN = 0,
	HW_TOD_WR_TRIG_SEL_MSB = HW_TOD_WR_TRIG_SEL_MIN,
	HW_TOD_WR_TRIG_SEL_RESERVED = 1,
	HW_TOD_WR_TRIG_SEL_TOD_PPS = 2,
	HW_TOD_WR_TRIG_SEL_IRIGB_PPS = 3,
	HW_TOD_WR_TRIG_SEL_PWM_PPS = 4,
	HW_TOD_WR_TRIG_SEL_GPIO = 5,
	HW_TOD_WR_TRIG_SEL_FOD_SYNC = 6,
	WR_TRIG_SEL_MAX = HW_TOD_WR_TRIG_SEL_FOD_SYNC,
};

enum scsr_read_trig_sel {
	/* CANCEL CURRENT TOD READ; MODULE BECOMES IDLE - NO TRIGGER OCCURS */
	SCSR_TOD_READ_TRIG_SEL_DISABLE = 0,
	/* TRIGGER IMMEDIATELY */
	SCSR_TOD_READ_TRIG_SEL_IMMEDIATE = 1,
	/* TRIGGER ON RISING EDGE OF INTERNAL TOD PPS SIGNAL */
	SCSR_TOD_READ_TRIG_SEL_TODPPS = 2,
	/* TRGGER ON RISING EDGE OF SELECTED REFERENCE INPUT */
	SCSR_TOD_READ_TRIG_SEL_REFCLK = 3,
	/* TRIGGER ON RISING EDGE OF SELECTED PWM DECODER 1PPS OUTPUT */
	SCSR_TOD_READ_TRIG_SEL_PWMPPS = 4,
	SCSR_TOD_READ_TRIG_SEL_RESERVED = 5,
	/* TRIGGER WHEN WRITE FREQUENCY EVENT OCCURS  */
	SCSR_TOD_READ_TRIG_SEL_WRITEFREQUENCYEVENT = 6,
	/* TRIGGER ON SELECTED GPIO */
	SCSR_TOD_READ_TRIG_SEL_GPIO = 7,
	SCSR_TOD_READ_TRIG_SEL_MAX = SCSR_TOD_READ_TRIG_SEL_GPIO,
};

/* Values STATUS.DPLL_SYS_STATUS.DPLL_SYS_STATE */
enum dpll_state {
	DPLL_STATE_MIN = 0,
	DPLL_STATE_FREERUN = DPLL_STATE_MIN,
	DPLL_STATE_LOCKACQ = 1,
	DPLL_STATE_LOCKREC = 2,
	DPLL_STATE_LOCKED = 3,
	DPLL_STATE_HOLDOVER = 4,
	DPLL_STATE_OPEN_LOOP = 5,
	DPLL_STATE_MAX = DPLL_STATE_OPEN_LOOP,
};

/* 4.8.7 only */
enum scsr_tod_write_trig_sel {
	SCSR_TOD_WR_TRIG_SEL_DISABLE = 0,
	SCSR_TOD_WR_TRIG_SEL_IMMEDIATE = 1,
	SCSR_TOD_WR_TRIG_SEL_REFCLK = 2,
	SCSR_TOD_WR_TRIG_SEL_PWMPPS = 3,
	SCSR_TOD_WR_TRIG_SEL_TODPPS = 4,
	SCSR_TOD_WR_TRIG_SEL_SYNCFOD = 5,
	SCSR_TOD_WR_TRIG_SEL_GPIO = 6,
	SCSR_TOD_WR_TRIG_SEL_MAX = SCSR_TOD_WR_TRIG_SEL_GPIO,
};

/* 4.8.7 only */
enum scsr_tod_write_type_sel {
	SCSR_TOD_WR_TYPE_SEL_ABSOLUTE = 0,
	SCSR_TOD_WR_TYPE_SEL_DELTA_PLUS = 1,
	SCSR_TOD_WR_TYPE_SEL_DELTA_MINUS = 2,
	SCSR_TOD_WR_TYPE_SEL_MAX = SCSR_TOD_WR_TYPE_SEL_DELTA_MINUS,
};

/* firmware interface */
struct idtcm_fwrc {
	u8 hiaddr;
	u8 loaddr;
	u8 value;
	u8 reserved;
} __packed;

#define SET_U16_LSB(orig, val8) (orig = (0xff00 & (orig)) | (val8))
#define SET_U16_MSB(orig, val8) (orig = (0x00ff & (orig)) | (val8 << 8))

#define TOD_MASK_ADDR		(0xFFA5)
#define DEFAULT_TOD_MASK	(0x04)

#define TOD0_PTP_PLL_ADDR		(0xFFA8)
#define TOD1_PTP_PLL_ADDR		(0xFFA9)
#define TOD2_PTP_PLL_ADDR		(0xFFAA)
#define TOD3_PTP_PLL_ADDR		(0xFFAB)

#define TOD0_OUT_ALIGN_MASK_ADDR	(0xFFB0)
#define TOD1_OUT_ALIGN_MASK_ADDR	(0xFFB2)
#define TOD2_OUT_ALIGN_MASK_ADDR	(0xFFB4)
#define TOD3_OUT_ALIGN_MASK_ADDR	(0xFFB6)

#define DEFAULT_OUTPUT_MASK_PLL0	(0x003)
#define DEFAULT_OUTPUT_MASK_PLL1	(0x00c)
#define DEFAULT_OUTPUT_MASK_PLL2	(0x030)
#define DEFAULT_OUTPUT_MASK_PLL3	(0x0c0)

#define DEFAULT_TOD0_PTP_PLL		(0)
#define DEFAULT_TOD1_PTP_PLL		(1)
#define DEFAULT_TOD2_PTP_PLL		(2)
#define DEFAULT_TOD3_PTP_PLL		(3)

#endif
