/*
 * linux/include/asm-arm/arch-p2001/hardware.h
 *
 *  Copyright (C) 2004 Tobias Lorenz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

#ifndef __ASSEMBLY__

/* DMA descriptor */
typedef struct {
	u32	stat;			/* status: own, start, end, offset, status */
	u32	cntl;			/* control: loop, int, type, channel, length */
	char	*buf;			/* buffer */
	void	*next;			/* nextdsc */
} DMA_DSC;


/* The address definitions are from asic_bf.h */
typedef struct {					// 0x00100000U
	volatile unsigned int reserved1[0x3];
	volatile unsigned int ArmDmaPri;		// 0x0000000CU
	volatile unsigned int SDRAM_Ctrl;		// 0x00000010U
	volatile unsigned int ExtMem_Ctrl;		// 0x00000014U
	volatile unsigned int WaitState_Ext;		// 0x00000018U
	volatile unsigned int WaitState_Asic;		// 0x0000001CU
	volatile unsigned int TOP;			// 0x00000020U
	volatile unsigned int reserved2[0x3];
	volatile unsigned int Adr1_EQ_30Bit;		// 0x00000030U
	volatile unsigned int Adr2_EQ_30Bit;		// 0x00000034U
	volatile unsigned int Adr3_EQ_30Bit;		// 0x00000038U
	volatile unsigned int Dat3_EQ_32Bit;		// 0x0000003CU
	volatile unsigned int Adr4_HE_20Bit;		// 0x00000040U
	volatile unsigned int Adr4_LT_20Bit;		// 0x00000044U
	volatile unsigned int Adr5_HE_20Bit;		// 0x00000048U
	volatile unsigned int Adr5_LT_20Bit;		// 0x0000004CU
	volatile unsigned int Adr_Control;		// 0x00000050U
	volatile unsigned int ABORT_IA_32Bit;		// 0x00000054U	
} *P2001_SYS_regs_ptr;
#define P2001_SYS ((volatile P2001_SYS_regs_ptr) 0x00100000UL)

typedef struct {					// 0x00110000U
	volatile unsigned int Timer1;			// 0x00000000U
	volatile unsigned int Timer2;			// 0x00000004U
	volatile unsigned int TIMER_PRELOAD;		// 0x00000008U
	volatile unsigned int Timer12_PreDiv;		// 0x0000000CU
	volatile unsigned int TIMER_INT;		// 0x00000010U
	volatile unsigned int Freerun_Timer;		// 0x00000014U
	volatile unsigned int WatchDog_Timer;		// 0x00000018U
	volatile unsigned int reserved1[0x1];
	volatile unsigned int PWM_CNT;			// 0x00000020U
	volatile unsigned int PWM_CNT2;			// 0x00000024U
	volatile unsigned int reserved2[0x2];
	volatile unsigned int PLL_12000_config;		// 0x00000030U
	volatile unsigned int PLL_12288_config;		// 0x00000034U
	volatile unsigned int DIV_12288_config;		// 0x00000038U
	volatile unsigned int MOD_CNT_768;		// 0x0000003CU
	volatile unsigned int FSC_IRQ_STATUS;		// 0x00000040U
	volatile unsigned int FSC_CONFIG;		// 0x00000044U
	volatile unsigned int FSC_CONSTRUCT;		// 0x00000048U
	volatile unsigned int FSC_base_clk_reg;		// 0x0000004CU
	volatile unsigned int SYSCLK_SHAPE;		// 0x00000050U
	volatile unsigned int SDRAMCLK_SHAPE;		// 0x00000054U
	volatile unsigned int RING_OSZI;		// 0x00000058U
} *P2001_TIMER_regs_ptr;
#define P2001_TIMER ((volatile P2001_TIMER_regs_ptr) 0x00110000UL)

typedef struct {					// 0x00120000U
	volatile unsigned int reserved1[0x5];
	volatile unsigned int GPIO_Config;		// 0x00000014U
	volatile unsigned int GPIO_INT;			// 0x00000018U
	volatile unsigned int GPIO_Out;			// 0x0000001CU
	volatile unsigned int GPIO_IN;			// 0x00000020U
	volatile unsigned int GPIO_En;			// 0x00000024U
	volatile unsigned int PIN_MUX;			// 0x00000028U
	volatile unsigned int NRES_OUT;			// 0x0000002CU
	volatile unsigned int GPIO2_Out;		// 0x00000030U
	volatile unsigned int GPIO2_IN;			// 0x00000034U
	volatile unsigned int GPIO2_En;			// 0x00000038U
	volatile unsigned int GPIO_INT_SEL;		// 0x0000003CU
	volatile unsigned int GPI3_IN;			// 0x00000040U
	volatile unsigned int GPO4_OUT;			// 0x00000044U
} *P2001_GPIO_regs_ptr;
#define P2001_GPIO ((volatile P2001_GPIO_regs_ptr) 0x00120000UL)

typedef struct {					// 0x00130000U
	volatile unsigned int Main_NFIQ_Int_Ctrl;	// 0x00000000U
	volatile unsigned int Main_NIRQ_Int_Ctrl;	// 0x00000004U
	volatile unsigned int Status_NFIQ;		// 0x00000008U
	volatile unsigned int Status_NIRQ;		// 0x0000000CU
} *P2001_INT_CTRL_regs_ptr;
#define P2001_INT_CTRL ((volatile P2001_INT_CTRL_regs_ptr) 0x00130000UL)

typedef struct {					// 0x00130000U
	volatile unsigned int IRQ_STATUS;		// 0x00000000U
	volatile unsigned int FIQ_STATUS;		// 0x00000004U
	volatile unsigned int RAW_INTR;			// 0x00000008U
	volatile unsigned int INT_SELECT;		// 0x0000000CU
	volatile unsigned int INT_ENABLE;		// 0x00000010U
	volatile unsigned int INT_ENCLEAR;		// 0x00000014U
	volatile unsigned int SOFTINT;			// 0x00000018U
	volatile unsigned int SOFTINT_CLEAR;		// 0x0000001CU
	volatile unsigned int PROTECTION;		// 0x00000020U
	volatile unsigned int reserved1[0x3];
	volatile unsigned int CUR_VECT_ADDR;		// 0x00000030U
	volatile unsigned int DEF_VECT_ADDR;		// 0x00000034U
	volatile unsigned int reserved2[0x32];
	volatile unsigned int VECT_ADDR[16];		// 0x00000100U - 0x013CU
	volatile unsigned int reserved3[0x30];
	volatile unsigned int VECT_CNTL[16];		// 0x00000200U - 0x023CU
} *P2001_LPEC_VIC_regs_ptr;
#define P2001_LPEC_VIC ((volatile P2001_LPEC_VIC_regs_ptr) 0x00130000UL)

typedef union {						// 0x00140000U
	struct {	// write
		volatile unsigned int TX[4];		// 0x00000000U-0x000CU
		volatile unsigned int Baudrate;		// 0x00000010U
		volatile unsigned int reserved1[0x3];
		volatile unsigned int Config;		// 0x00000020U
		volatile unsigned int Clear;		// 0x00000024U
		volatile unsigned int Echo_EN;		// 0x00000028U
		volatile unsigned int IRQ_Status;	// 0x0000002CU
	} w;		// write
	
	struct {	// read
		volatile unsigned int RX[4];		// 0x00000000U-0x000CU
		volatile unsigned int reserved1[0x4];
		volatile unsigned int PRE_STATUS;	// 0x00000020U
		volatile unsigned int STATUS;		// 0x00000024U
		volatile unsigned int reserved2[0x1];
		volatile unsigned int IRQ_Status;	// 0x0000002CU
	} r;		// read
} *P2001_UART_regs_ptr;
#define P2001_UART ((volatile P2001_UART_regs_ptr) 0x00140000UL)

typedef struct {					// 0x00150000U
	struct {
		volatile unsigned char S[0x100];	// 0x00000000U
		volatile unsigned char H[0x100];	// 0x00000100U
	} BASE[8];
	struct {
		volatile unsigned char S[0x100];	// 0x00001000U
		volatile unsigned char H[0x100];	// 0x00001100U
	} HDLC;
	struct {
		volatile unsigned char S[0x100];	// 0x00001200U
		volatile unsigned char H[0x100];	// 0x00001300U
	} DTMF;
	volatile unsigned int reserved1[0x300];
	struct {
		volatile unsigned int Control;		// 0x00002000U
		volatile unsigned int Timeslot_Enable;	// 0x00002004U
		volatile unsigned int Status;		// 0x00002008U
		volatile unsigned int reserved1[0x1];
	} CTS[8];
	struct {
		volatile unsigned int Control;		// 0x00002080U
		volatile unsigned int Status;		// 0x00002084U
		volatile unsigned int reserved1[0x2];
	} HDLC_WB;
	struct {
		volatile unsigned int Control;		// 0x00002080U
		volatile unsigned int Status;		// 0x00002084U
		volatile unsigned int reserved1[0x2];
	} DTMF_WB;
	volatile unsigned int Peripheral_Frame_Sync[4];	// 0x000020A0U - 0x20ACU
	volatile unsigned int BSCK_FSC_Select;		// 0x000020B0U
} *P2001_PCM_HW_regs_ptr;
#define P2001_PCM_HW ((volatile P2001_PCM_HW_regs_ptr) 0x00150000UL)

typedef struct {					// 0x00160000U
	volatile unsigned int COEF_1394_697;		// 0x00000000U
	volatile unsigned int COEF_1540_770;		// 0x00000004U
	volatile unsigned int COEF_1704_852;		// 0x00000008U
	volatile unsigned int COEF_1882_941;		// 0x0000000CU
	volatile unsigned int COEF_2418_1209;		// 0x00000010U
	volatile unsigned int COEF_2672_1336;		// 0x00000014U
	volatile unsigned int COEF_2954_1477;		// 0x00000018U
	volatile unsigned int COEF_3266_1633;		// 0x0000001CU
	volatile unsigned int COEF_SIGNS;		// 0x00000020U
	volatile unsigned int RECURSION_COUNTER;	// 0x00000024U
	volatile unsigned int LAW_SCALE;		// 0x00000028U
	volatile unsigned int reserved1[0x3];
	volatile unsigned int MAC_TABLE_LO_N;		// 0x00000038U
	volatile unsigned int MAC_TABLE_HI_N;		// 0x0000003CU
	volatile unsigned int MAG_TONE[8];		// 0x00000040U
	volatile unsigned int MAG_OVERTONE[8];		// 0x00000060U
	/* Basetone T = 0:697Hz / 1:770Hz / ... / 7:1633Hz */
	struct {
		volatile unsigned int TAP1;		// 0x00000080U
		volatile unsigned int TAP2;		// 0x00000084U
	} TONE[8];
	/* Overtone OT= 0:1394Hz / 1:1540Hz / ... / 7:3266Hz */
	struct {
		volatile unsigned int TAP1;		// 0x000000C0U
		volatile unsigned int TAP2;		// 0x000000C4U
	} OVERTONE[8];
} *P2001_DTMF_COEF_regs_ptr;
#define P2001_DTMF_COEF(x) ((volatile P2001_DTMF_COEF_regs_ptr) ((unsigned int) 0x00160000UL+(0x100UL*(x)))) /* x = 0..31 */

typedef struct {					// 0x00162000U
	volatile unsigned int ENA_REG;			// 0x00000000U
	volatile unsigned int IRQ_STAT_REG;		// 0x00000004U
} *P2001_DTMF_regs_ptr;
#define P2001_DTMF ((volatile P2001_DTMF_regs_ptr) 0x00162000UL)

typedef struct {					// 0x00164000U
	volatile unsigned int VAL_LO;			// 0x00000000U
	volatile unsigned int VAL_HI;			// 0x00000004U
	volatile unsigned int RES;			// 0x00000008U
} *P2001_MAC_CMP_regs_ptr;
#define P2001_MAC_CMP ((volatile P2001_MAC_CMP_regs_ptr) 0x00164000UL)

typedef struct {					// 0x00170_00U _=0,4
	volatile unsigned int B1_REC;			// 0x00000000U
	volatile unsigned int B1_SEND;			// 0x00000004U
	volatile unsigned int B2_REC;			// 0x00000008U
	volatile unsigned int B2_SEND;			// 0x0000000CU
	volatile unsigned int D_REC;			// 0x00000010U
	volatile unsigned int D_SEND;			// 0x00000014U
	volatile unsigned int E_REC;			// 0x00000018U
	volatile unsigned int CTRL;			// 0x0000001CU
	volatile unsigned int INT_EN;			// 0x00000020U
	volatile unsigned int INT_STATUS;		// 0x00000024U
	volatile unsigned int FSC_PHASE;		// 0x00000028U
	volatile unsigned int reserved1[0x25];
	/* HFC-S+ Registers */
	volatile unsigned int STATES;			// 0x000000C0U (HFC-S+ Adr 30)
	volatile unsigned int SCTRL;			// 0x000000C4U (HFC-S+ Adr 31)
	volatile unsigned int SCTRL_E;			// 0x000000C8U (HFC-S+ Adr 32)
	volatile unsigned int SCTRL_R;			// 0x000000CCU (HFC-S+ Adr 33)
	volatile unsigned int SQ_REC_SEND;		// 0x000000D0U (HFC-S+ Adr 34)
	volatile unsigned int reserved2[0x2];
	volatile unsigned int CLKDEL;			// 0x000000DCU (HFC-S+ Adr 37)
} *P2001_S0_regs_ptr;
#define P2001_S0(x) ((volatile P2001_S0_regs_ptr) ((unsigned int) 0x00170000UL+(0x400UL*(x)))) /* x = 0..1 */

typedef struct {					// 0x0018_000U _=0,1,2,3
	volatile DMA_DSC *    RMAC_DMA_DESC;		// 0x00000000U
	volatile unsigned int RMAC_DMA_CNTL;		// 0x00000004U
	volatile unsigned int RMAC_DMA_STAT;		// 0x00000008U
	volatile unsigned int RMAC_DMA_EN;		// 0x0000000CU
	volatile unsigned int RMAC_CNTL;		// 0x00000010U
	volatile unsigned int RMAC_TLEN;		// 0x00000014U
	volatile unsigned int RMAC_PHYU;		// 0x00000018U
	volatile unsigned int RMAC_PHYL;		// 0x0000001CU
	volatile unsigned int RMAC_PFM[8];		// 0x00000020U-0x003CU
	volatile unsigned int RMAC_MIB[6];		// 0x00000040U-0x0054U
	volatile unsigned int reserved1[0x1e8];
	volatile unsigned int RMAC_DMA_DATA;		// 0x000007F8U
	volatile unsigned int RMAC_DMA_ADR;		// 0x000007FCU
	volatile DMA_DSC *    TMAC_DMA_DESC;		// 0x00000800U
	volatile unsigned int TMAC_DMA_CNTL;		// 0x00000804U
	volatile unsigned int TMAC_DMA_STAT;		// 0x00000808U
	volatile unsigned int TMAC_DMA_EN;		// 0x0000080CU
	volatile unsigned int TMAC_CNTL;		// 0x00000810U
	volatile unsigned int TMAC_MIB[2];		// 0x00000814U-0x0818U
	volatile unsigned int reserved2[0x1];
	volatile unsigned int MU_CNTL;			// 0x00000820U
	volatile unsigned int MU_DATA;			// 0x00000824U
	volatile unsigned int MU_DIV;			// 0x00000828U
	volatile unsigned int CONF_RMII;		// 0x0000082CU
	volatile unsigned int reserved3[0x1f2];
	volatile unsigned int TMAC_DMA_DATA;		// 0x00000FF8U
	volatile unsigned int TMAC_DMA_ADR;		// 0x00000FFCU
} *P2001_ETH_regs_ptr;
#define P2001_EU(x) ((volatile P2001_ETH_regs_ptr) ((unsigned int) 0x00180000UL+(0x1000UL*(x)))) /* x = 0..3 */
#define P2001_MU  P2001_EU(0)

typedef struct {				// 0x00184__0U _=00,...7C
	volatile unsigned int v_tx_dma_desc;	// 0x00000000U
	volatile unsigned int reserved1[0x1];
	volatile unsigned int v_tx_dma_stat;	// 0x00000008U
	volatile unsigned int v_tx_dma_en;	// 0x0000000CU
	volatile unsigned int v_rx_dma_desc;	// 0x00000010U
	volatile unsigned int v_rx_dma_cntl;	// 0x00000014U
	volatile unsigned int v_rx_dma_stat;	// 0x00000018U
	volatile unsigned int v_rx_dma_en;	// 0x0000001CU
	volatile unsigned int v_mode;		// 0x00000020U
	volatile unsigned int v_es_reg;		// 0x00000024U
	volatile unsigned int v_es_stat;	// 0x00000028U
	volatile unsigned int reserved2[0x5];
} *P2001_HDLC_DMA_regs_ptr[32];
#define P2001_HDLC_DMA ((volatile P2001_HDLC_regs_ptr) 0x00184000UL)

typedef struct {				// 0x001847F0U
	volatile unsigned int reserved1[0x2];
	volatile unsigned int rx_data;		// 0x000007F8U
	volatile unsigned int rx_adr;		// 0x000007FCU
	volatile unsigned int mts_tsa_base;	// 0x00000800U
	volatile unsigned int reserved2[0x183];
	volatile unsigned int pcm_cntl;		// 0x00000E10U
	volatile unsigned int reserved3[0x1];
	volatile unsigned int frame_end;	// 0x00000E18U
	volatile unsigned int v_data_stat;	// 0x00000E1CU
	volatile unsigned int v_err_stat;	// 0x00000E20U
	volatile unsigned int reserved4[0x75];
	volatile unsigned int tx_data;		// 0x00000FF8U
	volatile unsigned int tx_adr;		// 0x00000FFCU
} *P2001_HDLC_regs_ptr;
#define P2001_HDLC ((volatile P2001_HDLC_regs_ptr) 0x001847F0UL)

typedef struct {					// 0x00190000U
	volatile unsigned int FUNC_ADDR;		// 0x00000000U
	volatile unsigned int MODE_CTRL;		// 0x00000004U
	volatile unsigned int CTRL;			// 0x00000008U
	volatile unsigned int MAIN_EVENT;		// 0x0000000CU
	volatile unsigned int MAIN_EVENT_MSK;		// 0x00000010U
	volatile unsigned int STATIC_EVENT;		// 0x00000014U
	volatile unsigned int STATIC_EVENT_MSK;		// 0x00000018U
	volatile unsigned int FRM_TIMER;		// 0x0000001CU
	volatile unsigned int OUT_EP_SEL;		// 0x00000020U
	volatile unsigned int OUT_DATA;			// 0x00000024U
	volatile unsigned int OUT_CMD;			// 0x00000028U
	volatile unsigned int OUT_STAT;			// 0x0000002CU
	volatile unsigned int IN_EP_SEL;		// 0x00000030U
	volatile unsigned int IN_DATA;			// 0x00000034U
	volatile unsigned int IN_CMD;			// 0x00000038U
	volatile unsigned int reserved1[0x1];
	volatile unsigned int OEP_ENA;			// 0x00000040U
	volatile unsigned int IEP_ENA;			// 0x00000044U
	volatile unsigned int OEP_STALL;		// 0x00000048U
	volatile unsigned int IEP_STALL;		// 0x0000004CU
	volatile unsigned int OUT_EVENT;		// 0x00000050U
	volatile unsigned int OUT_EVENT_MSK;		// 0x00000054U
	volatile unsigned int IN_EVENT;			// 0x00000058U
	volatile unsigned int IN_EVENT_MSK;		// 0x0000005CU
	volatile unsigned int IN_ISO_CONF_REG;		// 0x00000060U
	volatile unsigned int OUT_ISO_CONF_REG;		// 0x00000064U
	volatile unsigned int IN_PTR_REG;		// 0x00000068U
	volatile unsigned int OUT_PTR_REG;		// 0x0000006CU
	volatile unsigned int reserved2[0x3];
	volatile unsigned int CTRL_REG;			// 0x0000007CU
} *P2001_USB_regs_ptr;
#define P2001_USB ((volatile P2001_USB_regs_ptr) 0x00190000UL)

typedef volatile unsigned char *P2001_USB_FIFO_ptr[64];
#define P2001_USB_EPxIN(x)  ((volatile P2001_USB_FIFO_ptr) ((unsigned int) 0x001A0000UL+(0x40UL*(x)))) /* x = 0..5 */
#define P2001_USB_EPxOUT(x) ((volatile P2001_USB_FIFO_ptr) ((unsigned int) 0x001A0180UL+(0x40UL*(x)))) /* x = 0..5 */

#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
