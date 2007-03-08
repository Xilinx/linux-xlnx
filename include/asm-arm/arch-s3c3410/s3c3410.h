/*
 *  linux/include/asm-arm/arch-s3c3410/s3c3410.h
 *
 * Special function registers of the Samsung S3C3410X
 *
 * (C) 2003 sympat GmbH
 * by Thomas Eschenbacher <thomas.eschenbacher@sympat.de>
 *
 */

#ifndef __ASM_ARCH_S3C3410_H
#define __ASM_ARCH_S3C3410_H

#define S3C3410_MEM_SIZE     (CONFIG_DRAM_SIZE) 
#define MEM_SIZE            S3C3410_MEM_SIZE
#define PA_SDRAM_BASE         CONFIG_DRAM_BASE

/* Address offset for accessing external memory uncached (A27=1) */
#define S3C3410X_UNCACHED        0x08000000             /* (1 << 27) */

/*
 * SFR Base Address
 */
#define S3C3410X_BASE            0x07FF0000

/* ************************ */
/* System Manager Registers */
/* ************************ */
#define S3C3410X_SYSCFG          (S3C3410X_BASE+0x1000) /* System Configuration */

#define S3C3410X_SYSCFG_ST       0x00000001		/* Stall enable */
#define S3C3410X_SYSCFG_CE       0x00000002		/* Cache enable*/
#define S3C3410X_SYSCFG_WE       0x00000004		/* Write Buffer enable */
#define S3C3410X_SYSCFG_SFRSA    0x00007FF0		/* SYSCFG Address (SFR Start Address) */

#define S3C3410X_SYSCFG_CM_MASK  0x00018000		/* Cache Mode (MASK) */
#define S3C3410X_SYSCFG_CM_22    0x00000000		/* (Cache Mode) 2k Cache / 2k SRAM */
#define S3C3410X_SYSCFG_CM_CACHE 0x00008000		/* (Cache Mode) 4k Cache */
#define S3C3410X_SYSCFG_CM_SRAM  0x00010000		/* (Cache Mode) 4k SRAM */

#define S3C3410X_SYSCFG_AME      0x00020000		/* Address Mux Enable */
#define S3C3410X_SYSCFG_MT0_RFS  0x00000000		/* Memory Type 0 ROM/Flash/SRAM */
#define S3C3410X_SYSCFG_MT0_FP   0x00040000		/* Memory Type 0 FP DRAM */
#define S3C3410X_SYSCFG_MT0_EDO  0x00080000		/* Memory Type 0 EDO DRAM */
#define S3C3410X_SYSCFG_MT0_SD   0x000C0000		/* Memory Type 0 Sync. DRAM */
#define S3C3410X_SYSCFG_MT1_RFS  0x00000000		/* Memory Type 1 ROM/Flash/SRAM */
#define S3C3410X_SYSCFG_MT1_FP   0x00100000		/* Memory Type 1 FP DRAM */
#define S3C3410X_SYSCFG_MT1_EDO  0x00200000		/* Memory Type 1 EDO DRAM */
#define S3C3410X_SYSCFG_MT1_SD   0x00300000		/* Memory Type 1 Sync. DRAM */

#define S3C3410X_BANKCON0	(S3C3410X_BASE+0x2000) /* Memory Bank 0 Control */
#define S3C3410X_BANKCON1	(S3C3410X_BASE+0x2004) /* Memory Bank 1 Control */
#define S3C3410X_BANKCON2	(S3C3410X_BASE+0x2008) /* Memory Bank 2 Control */
#define S3C3410X_BANKCON3	(S3C3410X_BASE+0x200C) /* Memory Bank 3 Control */
#define S3C3410X_BANKCON4	(S3C3410X_BASE+0x2010) /* Memory Bank 4 Control */
#define S3C3410X_BANKCON5	(S3C3410X_BASE+0x2014) /* Memory Bank 5 Control */
#define S3C3410X_BANKCON6	(S3C3410X_BASE+0x2018) /* Memory Bank 6 Control */
#define S3C3410X_BANKCON7	(S3C3410X_BASE+0x201C) /* Memory Bank 7 Controlr */

#define S3C3410X_BANKCON_DBW	    0x00000001 	    	/* Data Bus Width (16Bit enable) else 8Bit */
#define S3C3410X_BANKCON_PMC_1D	    0x00000000 	    	/* Page Mode 1Data */
#define S3C3410X_BANKCON_PMC_4D	    0x00000002 	    	/* Page Mode 4Data */
#define S3C3410X_BANKCON_PMC_8D	    0x00000004 	    	/* Page Mode 8Data */
#define S3C3410X_BANKCON_PMC_16D    0x00000006 	    	/* Page Mode 16Data */
#define S3C3410X_BANKCON_SM	    0x00000008 	    	/* UB/LB Byte selection enable (see nWE,WE in Chipdesign) */
#define S3C3410X_BANKCON_TACC_DIS   0x00000000 	    	/* Access Cycle Timing Disabled */
#define S3C3410X_BANKCON_TACC_2C    0x00000010 	    	/* Access Cycle Timing 2Clocks */
#define S3C3410X_BANKCON_TACC_3C    0x00000020 	    	/* Access Cycle Timing 3Clocks */
#define S3C3410X_BANKCON_TACC_4C    0x00000030 	    	/* Access Cycle Timing 4Clocks */
#define S3C3410X_BANKCON_TACC_5C    0x00000040 	    	/* Access Cycle Timing 5Clocks */
#define S3C3410X_BANKCON_TACC_6C    0x00000050 	    	/* Access Cycle Timing 6Clocks */
#define S3C3410X_BANKCON_TACC_7C    0x00000060 	    	/* Access Cycle Timing 7Clocks */
#define S3C3410X_BANKCON_TACC_10C   0x00000070 	    	/* Access Cycle Timing 10Clocks */
#define S3C3410X_BANKCON_TACP_2C    0x00000080 	    	/* Page Mode Access Cycle Timing 2Clocks */
#define S3C3410X_BANKCON_TACP_3C    0x00000100 	    	/* Page Mode Access Cycle Timing 3Clocks */
#define S3C3410X_BANKCON_TACP_4C    0x00000180 	    	/* Page Mode Access Cycle Timing 4Clocks */
#define S3C3410X_BANKCON_TACP_5C    0x00000000 	    	/* Page Mode Access Cycle Timing 5Clocks */

#define S3C3410X_REFCON  	(S3C3410X_BASE+0x2020) /* DRAM Refresh Control */

#define S3C3410X_EXTCON0 	(S3C3410X_BASE+0x2030) /* Extra device control 0 */
#define S3C3410X_EXTCON1 	(S3C3410X_BASE+0x2034) /* Extra device control 1 */
#define S3C3410X_EXTPORT 	(S3C3410X_BASE+0x203E) /* External port data */

#define S3C3410X_EXTDAT0 	(S3C3410X_BASE+0x202C) /* Extra chip selection data 0 */
#define S3C3410X_EXTDAT1 	(S3C3410X_BASE+0x202E) /* Extra chip selection data 1 */

/* ************* */
/* DMA Registers */
/* ************* */
#define S3C3410X_DMACON0 	(S3C3410X_BASE+0x300C) /* DMA 0 control */
#define S3C3410X_DMASRC0 	(S3C3410X_BASE+0x3000) /* DMA 0 source address */
#define S3C3410X_DMADST0 	(S3C3410X_BASE+0x3004) /* DMA 0 destination address */
#define S3C3410X_DMACNT0 	(S3C3410X_BASE+0x3008) /* DMA 0 transfer count */

#define S3C3410X_DMACON1 	(S3C3410X_BASE+0x400C) /* DMA 1 control */
#define S3C3410X_DMASRC1 	(S3C3410X_BASE+0x4000) /* DMA 1 source address */
#define S3C3410X_DMADST1 	(S3C3410X_BASE+0x4004) /* DMA 1 destination address */
#define S3C3410X_DMACNT1 	(S3C3410X_BASE+0x4008) /* DMA 1 transfer count */

/* ******************* */
/* I/O Ports Registers */
/* ******************* */
#define S3C3410X_PDAT0   	(S3C3410X_BASE+0xB000) /* Port 0 data */
#define S3C3410X_PDAT1   	(S3C3410X_BASE+0xB001) /* Port 1 data */
#define S3C3410X_PDAT2   	(S3C3410X_BASE+0xB002) /* Port 2 data */
#define S3C3410X_PDAT3   	(S3C3410X_BASE+0xB003) /* Port 3 data */
#define S3C3410X_PDAT4   	(S3C3410X_BASE+0xB004) /* Port 4 data */
#define S3C3410X_PDAT5   	(S3C3410X_BASE+0xB005) /* Port 5 data */
#define S3C3410X_PDAT6   	(S3C3410X_BASE+0xB006) /* Port 6 data */
#define S3C3410X_PDAT7   	(S3C3410X_BASE+0xB007) /* Port 7 data */
#define S3C3410X_PDAT8   	(S3C3410X_BASE+0xB008) /* Port 8 data */
#define S3C3410X_PDAT9   	(S3C3410X_BASE+0xB009) /* Port 9 data */

#define S3C3410X_P7BR    	(S3C3410X_BASE+0xB00B) /* Port 7 buffer */

#define S3C3410X_PCON0   	(S3C3410X_BASE+0xB010) /* Port 0 control */
#define S3C3410X_PCON1   	(S3C3410X_BASE+0xB012) /* Port 1 control */
#define S3C3410X_PCON2   	(S3C3410X_BASE+0xB014) /* Port 2 control */
#define S3C3410X_PCON3   	(S3C3410X_BASE+0xB016) /* Port 3 control */
#define S3C3410X_PCON4   	(S3C3410X_BASE+0xB018) /* Port 4 control */
#define S3C3410X_PCON5   	(S3C3410X_BASE+0xB01C) /* Port 5 control */
#define S3C3410X_PCON6   	(S3C3410X_BASE+0xB020) /* Port 6 control */
#define S3C3410X_PCON7   	(S3C3410X_BASE+0xB024) /* Port 7 control */
#define S3C3410X_PCON8   	(S3C3410X_BASE+0xB026) /* Port 8 control */
#define S3C3410X_PCON9   	(S3C3410X_BASE+0xB027) /* Port 9 control */

#define S3C3410X_PUR0    	(S3C3410X_BASE+0xB028) /* Port 0 pull-up control */
#define S3C3410X_PDR1    	(S3C3410X_BASE+0xB029) /* Port 1 pull-down control */
#define S3C3410X_PUR2    	(S3C3410X_BASE+0xB02A) /* Port 2 pull-up control */
#define S3C3410X_PUR3    	(S3C3410X_BASE+0xB02B) /* Port 3 pull-up control */
#define S3C3410X_PDR4    	(S3C3410X_BASE+0xB02C) /* Port 4 pull-down control */
#define S3C3410X_PUR5    	(S3C3410X_BASE+0xB02D) /* Port 5 pull-up control */
#define S3C3410X_PUR6    	(S3C3410X_BASE+0xB02E) /* Port 6 pull-up control */
#define S3C3410X_PUR7    	(S3C3410X_BASE+0xB02F) /* Port 7 pull-up control */
#define S3C3410X_PUR8    	(S3C3410X_BASE+0xB03C) /* Port 8 pull-up control */

#define S3C3410X_EINTPND  	(S3C3410X_BASE+0xB031) /* External interrupt pending */
#define S3C3410X_EINTCON  	(S3C3410X_BASE+0xB032) /* External interrupt control */
#define S3C3410X_EINTMOD  	(S3C3410X_BASE+0xB034) /* External interrupt mode */

/* *************** */
/* Timer Registers */
/* *************** */

#define S3C3410X_TDAT0   	(S3C3410X_BASE+0x9000) /* Timer 0 data */
#define S3C3410X_TPRE0   	(S3C3410X_BASE+0x9002) /* Timer 0 prescaler */
#define S3C3410X_TCON0   	(S3C3410X_BASE+0x9003) /* Timer 0 control */
#define S3C3410X_TCNT0   	(S3C3410X_BASE+0x9006) /* Timer 0 counter */

#define S3C3410X_T16_ICS	0x00000004	/* 16Bit-Timer Input Select */
#define S3C3410X_T16_OMS_MODE	0x00000038	/* 16Bit-Timer Mode bits */
#define S3C3410X_T16_OMS_INTRV	0x00000000	/* 16Bit-Timer Mode (interval mode) */
#define S3C3410X_T16_OMS_MAOF	0x00000008	/* 16Bit-Timer Mode (match & overflow mode) */
#define S3C3410X_T16_OMS_MAD	0x00000010	/* 16Bit-Timer Mode (match & DMA mode) */
#define S3C3410X_T16_OMS_CAPF	0x00000020	/* 16Bit-Timer Mode (capture on falling edge of TCAP 0,1,2) */
#define S3C3410X_T16_OMS_CAPR	0x00000028	/* 16Bit-Timer Mode (capture on rising edge of TCAP 0,1,2) */
#define S3C3410X_T16_OMS_CAPRF	0x00000030	/* 16Bit-Timer Mode (capture on rising/falling edge of TCAP 0,1,2) */
#define S3C3410X_T16_CL		0x00000040	/* 16Bit-Timer Clear */
#define S3C3410X_T16_TEN	0x00000080	/* 16Bit-Timer Enable */

#define S3C3410X_TDAT1   	(S3C3410X_BASE+0x9010) /* Timer 1 data */
#define S3C3410X_TPRE1   	(S3C3410X_BASE+0x9012) /* Timer 1 prescaler */
#define S3C3410X_TCON1   	(S3C3410X_BASE+0x9013) /* Timer 1 control */
#define S3C3410X_TCNT1   	(S3C3410X_BASE+0x9016) /* Timer 1 counter */

#define S3C3410X_TDAT2   	(S3C3410X_BASE+0x9020) /* Timer 2 data */
#define S3C3410X_TPRE2   	(S3C3410X_BASE+0x9022) /* Timer 2 prescaler */
#define S3C3410X_TCON2   	(S3C3410X_BASE+0x9023) /* Timer 2 control */
#define S3C3410X_TCNT2   	(S3C3410X_BASE+0x9026) /* Timer 2 counter */

#define S3C3410X_TDAT3   	(S3C3410X_BASE+0x9030) /* Timer 3 data */
#define S3C3410X_TPRE3   	(S3C3410X_BASE+0x9032) /* Timer 3 prescaler */
#define S3C3410X_TCON3   	(S3C3410X_BASE+0x9033) /* Timer 3 control */
#define S3C3410X_TCNT3   	(S3C3410X_BASE+0x9037) /* Timer 3 counter */

#define S3C3410X_TDAT4   	(S3C3410X_BASE+0x9041) /* Timer 4 data */
#define S3C3410X_TPRE4   	(S3C3410X_BASE+0x9042) /* Timer 4 prescaler */
#define S3C3410X_TCON4   	(S3C3410X_BASE+0x9043) /* Timer 4 control */
#define S3C3410X_TCNT4   	(S3C3410X_BASE+0x9047) /* Timer 4 counter */
#define S3C3410X_TFCON   	(S3C3410X_BASE+0x904F) /* Timer 4 FIFO control */
#define S3C3410X_TFSTAT  	(S3C3410X_BASE+0x904E) /* Timer 4 FIFO status */
#define S3C3410X_TFB4    	(S3C3410X_BASE+0x904B) /* Timer 4 FIFO @ byte */
#define S3C3410X_TFHW4   	(S3C3410X_BASE+0x904A) /* Timer 4 FIFO @ half-word */
#define S3C3410X_TFW4    	(S3C3410X_BASE+0x9048) /* Timer 4 FIFO @ word */

/* #define TDATA0	TDAT0
   #define TMOD 	TCON0 */

/* ************** */
/* UART Registers */
/* ************** */

#define S3C3410X_UART_BASE	(S3C3410X_BASE) /* "virtual" start of UART registers */

#define S3C3410X_ULCON   	(0x5003) /* UART line control */
#define S3C3410X_UCON    	(0x5007) /* UART control */
#define S3C3410X_USTAT   	(0x500B) /* UART status */
#define S3C3410X_UFCON   	(0x500F) /* UART FIFO control */
#define S3C3410X_UFSTAT  	(0x5012) /* UART FIFO status */
#define S3C3410X_UTXH    	(0x5017) /* UART transmit holding */
#define S3C3410X_UTXH_B  	(0x5017) /* UART transmit FIFO @ byte */
#define S3C3410X_UTXH_HW 	(0x5016) /* UART transmit FIFO @ half-word */
#define S3C3410X_UTXH_W  	(0x5014) /* UART transmit FIFO @ word */
#define S3C3410X_URXH    	(0x501B) /* UART receive holding */
#define S3C3410X_URXH_B  	(0x501B) /* UART receive FIFO @ byte */
#define S3C3410X_URXH_HW 	(0x501A) /* UART receive FIFO @ half-word */
#define S3C3410X_URXH_W  	(0x5018) /* UART receive FIFO @ word */
#define S3C3410X_UBRDIV  	(0x501E) /* baud rate divisor */

/* UART Line Control Register Bits */
#define ULCON_WL_MASK		0x03	/* UART Word Length Mask */
#define ULCON_WL_5		0x00	/* UART Word Length: 5 bits */
#define ULCON_WL_6		0x01	/* UART Word Length: 6 bits */
#define ULCON_WL_7		0x02	/* UART Word Length: 7 bits */
#define ULCON_WL_8		0x03	/* UART Word Length: 8 bits */
#define ULCON_SB		0x04	/* UART Stop Bits */
#define ULCON_PMD_MASK		0x38	/* UART Parity Mode Mask */
#define ULCON_PMD_NONE		0x00	/* UART Parity Mode: None */
#define ULCON_PMD_ODD		0x20	/* UART Parity Mode: Odd */
#define ULCON_PMD_EVEN		0x28	/* UART Parity Mode: Even */
#define ULCON_IRM		0x40	/* UART Infrared Mode */
/* 				0x80	   unused */


/* UART Control Register Bits */
#define UCON_RM_MASK		0x03	/* UART Mask for Receive Mode */
#define UCON_RM_DISABLED	0x00	/* UART Receive Mode 0 : Disabled */
#define UCON_RM_IRQ_POLL	0x01	/* UART Receive Mode 1 : Interrupt or Polling Mode */
#define UCON_RM_DMA0		0x02	/* UART Receive Mode 2 : DMA0 request */
#define UCON_RM_DMA1		0x03	/* UART Receive Mode 3 : DMA1 request */

#define UCON_TM_MASK		0x0C	/* UART Mask for Transmit Mode */
#define UCON_TM_DISABLED	0x00	/* UART Transmit Mode 0 : Disabled */
#define UCON_TM_IRQ_POLL	0x04	/* UART Transmit Mode 1 : Interrupt or Polling Mode */
#define UCON_TM_DMA0		0x08	/* UART Transmit Mode 2 : DMA0 request */
#define UCON_TM_DMA1		0x0C	/* UART Transmit Mode 3 : DMA1 request */

#define UCON_SBS		0x10	/* UART Send Break Signal */
#define UCON_LBM		0x20	/* UART Loopback Mode */
#define UCON_RSIE		0x40	/* UART Rx Status Interrupt Enable */
#define UCON_RXTOEL		0x80	/* UART Rx Timeout Enable */

/* UART Status Register Bits */
#define USTAT_OE 		0x01	/* UART Overrun Error */
#define USTAT_PE 		0x02	/* UART Parity Error */
#define USTAT_FE 		0x04	/* UART Framing Error */
#define USTAT_BD 		0x08	/* UART Break Detect */
#define USTAT_RTO 		0x10	/* UART Receiver Time Out */
#define USTAT_RFDR 		0x20	/* UART Receive FIFO Data Ready / Rx Buffer Data Ready */
#define USTAT_TFE 		0x40	/* UART Transmit FIFO Empty / Tx Holding Register Empty */
#define USTAT_TSE 		0x80	/* UART Transmit Shift Register Empty */

/* UART FIFO Control Register Bits */
#define UFCON_FE		0x01	/* UART FIFO Enable */
#define UFCON_RFR		0x02	/* UART Rx FIFO Reset */
#define UFCON_TFR		0x04	/* UART Tx FIFO Reset */
/*				0x08	   reserved */
#define UFCON_RFTL_MASK		0x30	/* UART Receive FIFO Trigger Level Mask */
#define UFCON_RFTL_2		0x00	/* UART Receive FIFO Trigger Level: 2 byte */
#define UFCON_RFTL_4		0x10	/* UART Receive FIFO Trigger Level: 4 byte */
#define UFCON_RFTL_6		0x20	/* UART Receive FIFO Trigger Level: 6 byte */
#define UFCON_RFTL_8		0x30	/* UART Receive FIFO Trigger Level: 8 byte */

#define UFCON_TFTL_MASK		0x30	/* UART Transmit FIFO Trigger Level Mask */
#define UFCON_TFTL_0		0x00	/* UART Transmit FIFO Trigger Level: 0 byte */
#define UFCON_TFTL_2		0x10	/* UART Transmit FIFO Trigger Level: 2 byte */
#define UFCON_TFTL_4		0x20	/* UART Transmit FIFO Trigger Level: 4 byte */
#define UFCON_TFTL_6		0x30	/* UART Transmit FIFO Trigger Level: 6 byte */

#define UFSTAT_RFC_MASK		0x07    /* UART FIFO STATUS Rx FIFO count */
#define UFSTAT_TFC_MASK		0x38    /* UART FIFO STATUS Rx FIFO count */
#define UFSTAT_RFF		0x20	/* UART FIFO STATUS Receive FIFO FULL */
#define UFSTAT_TFF		0x40	/* UART FIFO STATUS Transmit FIFO FULL */
#define UFSTAT_EIF		0x80	/* UART FIFO STATUS Error in FIFO */


/* *********** */
/* SIO 0 and 1 */
/* *********** */

#define S3C3410X_ITVCNT0  	(S3C3410X_BASE+0x6000) /* SIO 0 interval counter */
#define S3C3410X_SBRDR0   	(S3C3410X_BASE+0x6001) /* SIO 0 baud rate prescaler */
#define S3C3410X_SIODAT0  	(S3C3410X_BASE+0x6002) /* SIO 0 data */
#define S3C3410X_SIOCON0  	(S3C3410X_BASE+0x6003) /* SIO 0 control */

#define S3C3410X_ITVCNT1  	(S3C3410X_BASE+0x7000) /* SIO 1 interval counter */
#define S3C3410X_SBRDR1   	(S3C3410X_BASE+0x7001) /* SIO 1 baud rate prescaler */
#define S3C3410X_SIODAT1  	(S3C3410X_BASE+0x7002) /* SIO 1 data */
#define S3C3410X_SIOCON1  	(S3C3410X_BASE+0x7003) /* SIO 1 control */

/* ****************************** */
/* Interrupt Controller Registers */
/* ****************************** */

#define S3C3410X_INTMOD		(S3C3410X_BASE+0xC000) /* Interrupt mode */
#define S3C3410X_INTPND		(S3C3410X_BASE+0xC004) /* Interrupt pending */
#define S3C3410X_INTMSK		(S3C3410X_BASE+0xC008) /* Interrupt mask */

#define S3C3410X_INTPRI0	(S3C3410X_BASE+0xC00C) /* Interrupt priority 0 */
#define S3C3410X_INTPRI1	(S3C3410X_BASE+0xC010) /* Interrupt priority 1 */
#define S3C3410X_INTPRI2	(S3C3410X_BASE+0xC014) /* Interrupt priority 2 */
#define S3C3410X_INTPRI3	(S3C3410X_BASE+0xC018) /* Interrupt priority 3 */
#define S3C3410X_INTPRI4	(S3C3410X_BASE+0xC01C) /* Interrupt priority 4 */
#define S3C3410X_INTPRI5	(S3C3410X_BASE+0xC020) /* Interrupt priority 5 */
#define S3C3410X_INTPRI6	(S3C3410X_BASE+0xC024) /* Interrupt priority 6 */
#define S3C3410X_INTPRI7	(S3C3410X_BASE+0xC028) /* Interrupt priority 7 */

/* *** */
/* ADC */
/* *** */

#define S3C3410X_ADCCON   	(S3C3410X_BASE+0x8002) /* A/D Converter control */
#define S3C3410X_ADCDAT   	(S3C3410X_BASE+0x8006) /* A/D Converter data */

/* *********** */
/* Basic Timer */
/* *********** */

#define S3C3410X_BTCON    	(S3C3410X_BASE+0xA002)	/* Basic Timer control */
#define S3C3410X_BTCON_WDTC     0x00000001		/* Watchdog Timer Clear */
#define S3C3410X_BTCON_BTC      0x00000002		/* Basic Timer Clear */
#define S3C3410X_BTCON_CS_13	0x00000000		/* Watchdog Clock source Fin / 2^13 */
#define S3C3410X_BTCON_CS_12	0x00000040		/* Watchdog Clock source Fin / 2^12 */
#define S3C3410X_BTCON_CS_11	0x00000080		/* Watchdog Clock source Fin / 2^11 */
#define S3C3410X_BTCON_CS_9	0x000000C0		/* Watchdog Clock source Fin / 2^9 */

/* When this value is written into BTCON it will disable the Timer. Any other value will enable it. */
#define S3C3410X_BTCON_WDTD     0x0000A500		/* Watchdog Timer Disable */
#define S3C3410X_BTCON_WDTE     0x0000FF00		/* Watchdog Timer Enable */

#define S3C3410X_BTCNT   	(S3C3410X_BASE+0xA007)	/* Basic Timer count */

/* ***************** */
/* I2C Bus Registers */
/* ***************** */

#define S3C3410X_IICCON   	(S3C3410X_BASE+0xE000) /* IIC-bus control */
#define S3C3410X_IICSTAT   	(S3C3410X_BASE+0xE001) /* IIC-bus status */
#define S3C3410X_IICDS    	(S3C3410X_BASE+0xE002) /* IIC-bus tx/rx data shift reg. */
#define S3C3410X_IICADD   	(S3C3410X_BASE+0xE003) /* IIC-bus tx/rx address */
#define S3C3410X_IICPC    	(S3C3410X_BASE+0xE004) /* IIC-bus Prescaler */
#define S3C3410X_IICPCNT  	(S3C3410X_BASE+0xE005) /* IIC-bus Prescaler counter */

/* *********************** */
/* System control register */
/* *********************** */

#define S3C3410X_SYSCON   	(S3C3410X_BASE+0xD003) /* System control */

#define S3C3410X_SYSCON_STOP      0x00000001 /* STOP-Mode */
#define S3C3410X_SYSCON_IDLE      0x00000002 /* IDLE-Mode */
#define S3C3410X_SYSCON_DMAIDLE   0x00000004 /* DMA-IDLE-Mode */
#define S3C3410X_SYSCON_MCLK16    0x00000000 /* system clock = MCLOCK / 16 */
#define S3C3410X_SYSCON_MCLK2     0x00000010 /* system clock = MCLOCK / 2 */
#define S3C3410X_SYSCON_MCLK1024  0x00000020 /* system clock = MCLOCK / 1024 */
#define S3C3410X_SYSCON_MCLK8     0x00000008 /* system clock = MCLOCK / 8 */
#define S3C3410X_SYSCON_MCLK      0x00000018 /* system clock = MCLOCK */
#define S3C3410X_SYSCON_GIE       0x00000040 /* Global Interrupt Enable (set when enabled !!) */

/* ************** */
/* Realtime Clock */
/* ************** */

#define S3C3410X_RTCCON   	(S3C3410X_BASE+0xA013) /* RTC control */
#define S3C3410X_RTCALM   	(S3C3410X_BASE+0xA012) /* RTC alarm control */
#define S3C3410X_ALMSEC   	(S3C3410X_BASE+0xA033) /* Alarm second */
#define S3C3410X_ALMMIN    	(S3C3410X_BASE+0xA032) /* Alarm minute */
#define S3C3410X_ALMHOUR    	(S3C3410X_BASE+0xA031) /* Alarm hour */
#define S3C3410X_ALMDAY   	(S3C3410X_BASE+0xA037) /* Alarm day */
#define S3C3410X_ALMMON   	(S3C3410X_BASE+0xA036) /* Alarm month */
#define S3C3410X_ALMYEAR   	(S3C3410X_BASE+0xA035) /* Alarm year */
#define S3C3410X_BCDSEC   	(S3C3410X_BASE+0xA023) /* BCD second */
#define S3C3410X_BCDMIN   	(S3C3410X_BASE+0xA022) /* BCD minute */
#define S3C3410X_BCDHOUR   	(S3C3410X_BASE+0xA021) /* BCD hour */
#define S3C3410X_BCDDAY   	(S3C3410X_BASE+0xA027) /* BCD day */
#define S3C3410X_BCDDATE   	(S3C3410X_BASE+0xA020) /* BCD date */
#define S3C3410X_BCDMON   	(S3C3410X_BASE+0xA026) /* BCD month */
#define S3C3410X_BCDYEAR   	(S3C3410X_BASE+0xA025) /* BCD year */

#define S3C3410X_RINTPND   	(S3C3410X_BASE+0xA010) /* RTC time interrupt pending */
#define S3C3410X_RINTCON   	(S3C3410X_BASE+0xA011) /* RTC time interrupt control */

#endif /* __ASM_ARCH_S3C3410_H */
