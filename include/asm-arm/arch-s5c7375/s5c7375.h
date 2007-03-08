/*
 *  linux/include/asm-arm/arch-s5c7375/s5c7375.h
 *
 *  Copyright (C) 2003 SAMSUNG ELECTRONICS
 *                         Hyok S. Choi (hyok.choi@samsung.com)
 *
 */

#ifndef __S5C7375_H
#define __S5C7375_H

/* keywoard : ClockParameter */
#define  FCLK 162000000
#define ECLK 27000000
#define  BUSWIDTH (32)


/* keywoard : Phy2Vir */
#define S5C7375_MEM_SIZE     (CONFIG_DRAM_SIZE) 
#define MEM_SIZE            S5C7375_MEM_SIZE

#define PA_SDRAM_BASE         CONFIG_DRAM_BASE/* used in asm/arch/arch.c     */

#ifndef HYOK_ROMFS_BOOT
#define ZIP_RAMDISK_SIZE      (256*1024)  /* used in asm/arch/arch.c  */
#define RAMDISK_DN_ADDR       (PA_SDRAM_BASE + 0x00400000 - ZIP_RAMDISK_SIZE) /* used in asm/arch/arch.c     */
#else
#define ZIP_RAMDISK_SIZE	(0x00040000)  /* used in asm/arch/arch.c  */
#define RAMDISK_DN_ADDR	(0x00400000 - ZIP_RAMDISK_SIZE)
#endif

/* if CONFIG_BLK_DEV_RAM_SIZE not defined */
#define BLK_DEV_RAM_SIZE      (256*1024)  

	#define rSCRBase     0x40000000     /* base of the System Configuration register */
	#define rASICBase    0x0C000000	    /* base of all I/O module register */
	#define rPTEAKBase   0x08F00000     /* preload TeakLite base address */
	
	
	/* define the base of each I/O devices */
	#define rARM920T     rASICBase
	#define rMEMBase     (rASICBase+0x10000) /* Memory controller */
	#define rDMABase     (rASICBase+0x20000) /* DMA controller */
	#define rLCDBase     (rASICBase+0x30000) /* LCD controller */
	#define rEMACBase    (rASICBase+0x40000) /* Ethernet MAC controller */
	#define rXMEMBase    (rASICBase+0x50000) /* TeakLite X memory(16KB) */
	#define rYMEMBase    (rASICBase+0x60000) /* TeakLite Y memory(8KB) */
	#define rPMEMBase    (rASICBase+0x70000) /* TeakLite P memory(32KB) */
	#define rAPBBase     (rASICBase+0x80000) /* AHB2APB bridge */
	
	/* Preload TeakLite address */
	#define rPPDataArea  (rPTEAKBase+0x00)   /* preload TeakLite P data area */
	#define rPXDataArea  (rPTEAKBase+0x40000)/* preload TeakLite X data area */
	#define rPYDataArea  (rPTEAKBase+0x60000)/* preload TeakLite Y data area */
	
	/************ AHB2APB bridge I/O device register *************/ 
	#define rBRIDGEBase  (rAPBBase+0x000)    /* APB Bridge interface */
	#define rSCIBase     (rAPBBase+0x400)    /* Smart card interface */
	#define rUSBBase     (rAPBBase+0x5800)    /* USB */
	#define rPPIBase     (rAPBBase+0xC00)    /* IEE1284, Parallel port */
	#define rIICBase     (rAPBBase+0x1000)   /* IIC */
	#define rTIMEBase    (rAPBBase+0x1400)   /* Timer */
	#define rRTCBase     (rAPBBase+0x1800)   /* Real time clock */
	#define rWDTBase     (rAPBBase+0x1C00)   /* Watch Dog Timer */
	#define rIOPBase     (rAPBBase+0x2000)   /* Programmable I/O port */
	#define rRPCBase     (rAPBBase+0x2400)   /* Memory remap */ 
	#define rINTBase     (rAPBBase+0x2800)   /* Interrupt controller */
	#define rSSPBase     (rAPBBase+0x2C00)   /* SSP interface */
	#define rKMI0Base    (rAPBBase+0x3000)   /* KMI0 */
	#define rUART0Base   (rAPBBase+0x3400)   /* UART0 */
	#define rUART1Base   (rAPBBase+0x3800)   /* UART1 */
	#define rPMBase      (rAPBBase+0x3C00)   /* Power manager */
	#define rTEAKBase    (rAPBBase+0x4000)   /* Teaklite Z space */
	#define rKMI1Base    (rAPBBase+0x4800)   /* KMI1 */
	#define rMISCBase    (rAPBBase+0x4C00)   /* Miscellaneous */
	#define rEXT0Base    (rAPBBase+0x5000)   /* External 0 */
	#define rEXT1Base    (rAPBBase+0x5400)   /* External 1 */
	#define rEXT2Base    (rAPBBase+0x5800)   /* External 2 */
	#define rEXT3Base    (rAPBBase+0x5C00)   /* External 3 */
	#define rPCMCIABase  (rAPBBase+0x9000)   /* Programmable I/O port */
	
	/* Memory Controller(32bit)*/
	#define rSDRAMCFG0    (*(volatile unsigned *)(rMEMBase+0x00))	/* SDRAM config0 register */
	#define rSDRAMCFG1    (*(volatile unsigned *)(rMEMBase+0x04))	/* SDRAM config1 register */
	#define rSDRAMRefresh (*(volatile unsigned *)(rMEMBase+0x08))	/* SDRAM refresh register */
	#define rSDRAMWB      (*(volatile unsigned *)(rMEMBase+0x0C))	/* SDRAM WB timeout register */
	// new static memory controller version
	#define rSMCBANK0     (*(volatile unsigned *)(rMEMBase+0x10))
	#define rSMCBANK1     (*(volatile unsigned *)(rMEMBase+0x14))
	#define rSMCBANK2     (*(volatile unsigned *)(rMEMBase+0x18))
	#define rSMCBANK3     (*(volatile unsigned *)(rMEMBase+0x1C))
	
	/* DMA Controller(32bit)*/ 
	// DMA control register 
	#define rDMACON0     (*(volatile unsigned *)(rDMABase+0x00))	
	#define rDMACON1     (*(volatile unsigned *)(rDMABase+0x40))	
	#define rDMACON2     (*(volatile unsigned *)(rDMABase+0x80))	
	#define rDMACON3     (*(volatile unsigned *)(rDMABase+0xC0))	
	#define rDMACON4     (*(volatile unsigned *)(rDMABase+0x100))	
	#define rDMACON5     (*(volatile unsigned *)(rDMABase+0x140))	
	
	// DMA Source Start address register 
	#define rDMASAR0     (*(volatile unsigned *)(rDMABase+0x04))	
	#define rDMASAR1     (*(volatile unsigned *)(rDMABase+0x44))	
	#define rDMASAR2     (*(volatile unsigned *)(rDMABase+0x84))	
	#define rDMASAR3     (*(volatile unsigned *)(rDMABase+0xC4))	
	#define rDMASAR4     (*(volatile unsigned *)(rDMABase+0x104))	
	#define rDMASAR5     (*(volatile unsigned *)(rDMABase+0x144))	
	
	// DMA Destination address register
	#define rDMADAR0     (*(volatile unsigned *)(rDMABase+0x08))
	#define rDMADAR1     (*(volatile unsigned *)(rDMABase+0x48))
	#define rDMADAR2     (*(volatile unsigned *)(rDMABase+0x88))
	#define rDMADAR3     (*(volatile unsigned *)(rDMABase+0xC8))
	#define rDMADAR4     (*(volatile unsigned *)(rDMABase+0x108))
	#define rDMADAR5     (*(volatile unsigned *)(rDMABase+0x148))
	
	// DMA Terminal Counter register 
	#define rDMATCR0     (*(volatile unsigned *)(rDMABase+0x0C))
	#define rDMATCR1     (*(volatile unsigned *)(rDMABase+0x4C))
	#define rDMATCR2     (*(volatile unsigned *)(rDMABase+0x8C))
	#define rDMATCR3     (*(volatile unsigned *)(rDMABase+0xCC))
	#define rDMATCR4     (*(volatile unsigned *)(rDMABase+0x10C))
	#define rDMATCR5     (*(volatile unsigned *)(rDMABase+0x14C))
	
	// DMA pending & priority register
	#define rDMAPRI      (*(volatile unsigned *)(rDMABase+0x1F8))
	#define rDMAPND      (*(volatile unsigned *)(rDMABase+0x1FC))
	
	/* LCD(primecell) Controller(32bit)*/
	
	/* APB Base register map */ 
	/* 1.APB pulse width control register */
	#define rAPBCON0     (*(volatile unsigned *)(rBRIDGEBase+0x00))
	#define rAPBCON1     (*(volatile unsigned *)(rBRIDGEBase+0x04))
	#define rAPBCON2     (*(volatile unsigned *)(rBRIDGEBase+0x08))
	#define rAPBCON3     (*(volatile unsigned *)(rBRIDGEBase+0x0C))
	
	/* 2.Smart card interface */
	
	
	/* 3.USB register(8bit)*/

	
	// Non indexed registers
	#define USB_R0       (volatile unsigned *)(rUSBBase+0x00)    // 0x00 - Function address register 
	#define USB_R1       (volatile unsigned *)(rUSBBase+0x04)    // 0x01 - Power management register 
	
	#define USB_R2       (volatile unsigned *)(rUSBBase+0x08)    // 0x02 - Endpoint interrupt1 register (EP0-EP7) 
	#define USB_R3       (volatile unsigned *)(rUSBBase+0x0c)    // 0x03 - Endpoint interrupt2 register (EP8-EP15)
	#define USB_R4       (volatile unsigned *)(rUSBBase+0x10)    //- Out interrupt register bank1 
	#define USB_R5       (volatile unsigned *)(rUSBBase+0x14)    //- Out interrupt register bank2
	#define USB_R6       (volatile unsigned *)(rUSBBase+0x18)    // 0x06 - USB interrpt register 
	
	#define USB_R7       (volatile unsigned *)(rUSBBase+0x1C)    // 0x07 - Endpoint interrupt enable1 register (EP0-EP7)
	#define USB_R8       (volatile unsigned *)(rUSBBase+0x20)    // 0x08 - Endpoint interrupt enable2 register (EP8-EP15) 
	#define USB_R9       (volatile unsigned *)(rUSBBase+0x24)    //- Out interrupt enable register bank1 
	#define USB_R10      (volatile unsigned *)(rUSBBase+0x28)    //- Out interrupt enable register bank2 
	#define USB_R11      (volatile unsigned *)(rUSBBase+0x2C)    // 0x0B - USB interrupt enable register 
	
	#define USB_R12      (volatile unsigned *)(rUSBBase+0x30)    // 0x0C - Frame number1 register 
	#define USB_R13      (volatile unsigned *)(rUSBBase+0x34)    // 0x0D - Frame number2 register 
	#define USB_R14      (volatile unsigned *)(rUSBBase+0x38)    // 0x0E - Index register 
	
	// Common indexed registers
	#define USB_IR1      (volatile unsigned *)(rUSBBase+0x40)    // 0x10 - Max packet register 
	
	// In indexed registers
	#define USB_IR2      (volatile unsigned *)(rUSBBase+0x44)    // 0x11 - IN CSR1 register (EP0 CSR register)
	#define USB_IR3      (volatile unsigned *)(rUSBBase+0x48)    // 0x12 - IN CSR2 register 
	
	// Out indexed registers 
	#define USB_OR1      (volatile unsigned *)(rUSBBase+0x4C)    //- OUT max packet register 
	#define USB_OR2      (volatile unsigned *)(rUSBBase+0x50)    // 0x14 - OUT CSR1 register 
	#define USB_OR3      (volatile unsigned *)(rUSBBase+0x54)    // 0x15 - OUT CSR2 register 
	#define USB_OR4      (volatile unsigned *)(rUSBBase+0x58)    // 0x16 - OUT FIFO write Count1 register 
	#define USB_OR5      (volatile unsigned *)(rUSBBase+0x5c)    // 0x17 - OUT FIFO write Count2 register 
	
	// FIFO registers
	#define EP0_FIFO     (volatile unsigned *)(rUSBBase+0x80)    // 0x20 - EP0 FIFO
	#define EP1_FIFO     (volatile unsigned *)(rUSBBase+0x84)	 // 0x21 - EP1 FIFO
	#define EP2_FIFO     (volatile unsigned *)(rUSBBase+0x88)	 // 0x22 - EP2 FIFO
	#define EP3_FIFO     (volatile unsigned *)(rUSBBase+0x8C)	 // 0x23 - EP3 FIFO

	
	/* 4.IEEE 1284(PPI)(8bit)*/
	
	/* 5.IIC register(8bit)*/
	/* 6.TIMER register(16bit)*/
	#define rT0CTR       (*(volatile int *)(rTIMEBase+0x00))
	#define rT0PSR       (*(volatile int *)(rTIMEBase+0x04))
	#define rT0LDR       (*(volatile int *)(rTIMEBase+0x08))
	#define rT0ISR       (*(volatile int *)(rTIMEBase+0x0C))
	
	#define rT1CTR       (*(volatile int *)(rTIMEBase+0x10))
	#define rT1PSR       (*(volatile int *)(rTIMEBase+0x14))
	#define rT1LDR       (*(volatile int *)(rTIMEBase+0x18))
	#define rT1ISR       (*(volatile int *)(rTIMEBase+0x1C))
	
	#define rT2CTR       (*(volatile int *)(rTIMEBase+0x20))
	#define rT2PSR       (*(volatile int *)(rTIMEBase+0x24))
	#define rT2LDR       (*(volatile int *)(rTIMEBase+0x28))
	#define rT2ISR       (*(volatile int *)(rTIMEBase+0x2C))
	
	#define rT3CTR       (*(volatile int *)(rTIMEBase+0x30))
	#define rT3PSR       (*(volatile int *)(rTIMEBase+0x34))
	#define rT3LDR       (*(volatile int *)(rTIMEBase+0x38))
	#define rT3ISR       (*(volatile int *)(rTIMEBase+0x3C))
	
	#define rT4CTR       (*(volatile int *)(rTIMEBase+0x40))
	#define rT4PSR       (*(volatile int *)(rTIMEBase+0x44))
	#define rT4LDR       (*(volatile int *)(rTIMEBase+0x48))
	#define rT4ISR       (*(volatile int *)(rTIMEBase+0x4C))
	
	#define rTTMR       (*(volatile int *)(rTIMEBase+0x80))
	#define rTTIR       (*(volatile int *)(rTIMEBase+0x84))
	#define rTTCR       (*(volatile int *)(rTIMEBase+0x88))
	
	
	/* 7.RTC register(8bit)*/
	#define rRTCCON	     (*(volatile unsigned *)(rRTCBase+0x00)) /* RTC control register */
	#define rRTCRST	     (*(volatile unsigned *)(rRTCBase+0x04)) /* RTC round reset register */
	#define rRTCALM	     (*(volatile unsigned *)(rRTCBase+0x08)) /* RTC alarm register */
	#define rALMSEC	     (*(volatile unsigned *)(rRTCBase+0x0C)) /* Alarm second data register */
	#define rALMMIN	     (*(volatile unsigned *)(rRTCBase+0x10))
	#define rALMHOUR     (*(volatile unsigned *)(rRTCBase+0x14)) /* Alarm hour data register */
	#define rALMDATE     (*(volatile unsigned *)(rRTCBase+0x18)) /* Alarm date data register */
	#define rALMDAY	     (*(volatile unsigned *)(rRTCBase+0x1C)) /* Alarm day data register */
	#define rALMMON	     (*(volatile unsigned *)(rRTCBase+0x20)) /* Alarm mon data register */
	#define rALMYEAR     (*(volatile unsigned *)(rRTCBase+0x24)) /* Alarm year data register */
	#define rBCDSEC	     (*(volatile unsigned *)(rRTCBase+0x28)) /* BCD second data register */
	#define rBCDMIN	     (*(volatile unsigned *)(rRTCBase+0x2C)) /* BCD minute data register */
	#define rBCDHOUR     (*(volatile unsigned *)(rRTCBase+0x30)) /* BCD hour data register */
	#define rBCDDATE     (*(volatile unsigned *)(rRTCBase+0x34)) /* BCD day data register */
	#define rBCDDAY	     (*(volatile unsigned *)(rRTCBase+0x38)) /* BCD day data register */
	#define rBCDMON	     (*(volatile unsigned *)(rRTCBase+0x3C)) /* BCD month data register */
	#define rBCDYEAR     (*(volatile unsigned *)(rRTCBase+0x40)) /* BCD year  data register */
	#define rRTCIM       (*(volatile unsigned *)(rRTCBase+0x44)) /* BCD year  data register */
	#define rRTCPEND     (*(volatile unsigned *)(rRTCBase+0x48)) /* BCD year  data register */
	
	
	/* 8.Watch Dog Timer register(8bit)*/
	
	/* 9.Programmable I/O port */
	#define rGIOPCON     (*(volatile unsigned *)(rIOPBase+0x00)) /* Port direction register */
	#define rGIOPDATA    (*(volatile unsigned *)(rIOPBase+0x04)) /* Data register */
	#define rGIOPINTEN   (*(volatile unsigned *)(rIOPBase+0x08)) /* Interrupt enable register */
	#define rGIOPLEVEL   (*(volatile unsigned *)(rIOPBase+0x0C)) /* Ative level indication register */
	#define rGIOPPEND    (*(volatile unsigned *)(rIOPBase+0x10)) /* Interrupt pending register */
	
	
	/* 10.Interrupt controller */		//0xc082800
	#define rINTCON      (*(volatile unsigned *)(rINTBase+0x00)) /* interrupt control register */
	#define rINTPND	     (*(volatile unsigned *)(rINTBase+0x04)) /* interrupt pending register */
	#define rINTMOD	     (*(volatile unsigned *)(rINTBase+0x08)) /* interrupt mode register */
	#define rINTMSK	     (*(volatile unsigned *)(rINTBase+0x0C)) /* interrupt mask register */
	#define rINTLEVEL    (*(volatile unsigned *)(rINTBase+0x10)) 
	#define rIRQPSLV0    (*(volatile unsigned *)(rINTBase+0x14)) /* IRQ priority of slave register0 */
	#define rIRQPSLV1    (*(volatile unsigned *)(rINTBase+0x18)) /* IRQ priority of slave register1 */
	#define rIRQPSLV2    (*(volatile unsigned *)(rINTBase+0x1C)) /* IRQ priority of slave register2 */
	#define rIRQPSLV3    (*(volatile unsigned *)(rINTBase+0x20)) /* IRQ priority of slave register3 */
	#define rIRQPMST     (*(volatile unsigned *)(rINTBase+0x24)) /* IRQ priority of master register */
	#define rIRQCSLV0    (*(volatile unsigned *)(rINTBase+0x28)) /* current IRQ priority of slave register0 */
	#define rIRQCSLV1    (*(volatile unsigned *)(rINTBase+0x2C)) /* current IRQ priority of slave register1 */
	#define rIRQCSLV2    (*(volatile unsigned *)(rINTBase+0x30)) /* current IRQ priority of slave register2 */
	#define rIRQCSLV3    (*(volatile unsigned *)(rINTBase+0x34)) /* current IRQ priority of slave register3 */
	#define rIRQCMST     (*(volatile unsigned *)(rINTBase+0x38)) /* current IRQ priority of master register */
	#define rIRQISPR     (*(volatile unsigned *)(rINTBase+0x3C)) /* IRQ service pending register */
	#define rIRQISPC     (*(volatile unsigned *)(rINTBase+0x40)) /* IRQ service clear register */
	#define rFIQPSLV0    (*(volatile unsigned *)(rINTBase+0x44)) /* FIQ priority of slave register0 */
	#define rFIQPSLV1    (*(volatile unsigned *)(rINTBase+0x48)) /* FIQ priority of slave register1 */
	#define rFIQPSLV2    (*(volatile unsigned *)(rINTBase+0x4C)) /* FIQ priority of slave register2 */
	#define rFIQPSLV3    (*(volatile unsigned *)(rINTBase+0x50)) /* FIQ priority of slave register3 */
	#define rFIQPMST     (*(volatile unsigned *)(rINTBase+0x54)) /* FIQ priority of master register */
	#define rFIQCSLV0    (*(volatile unsigned *)(rINTBase+0x58)) /* current FIQ priority of slave register0 */
	#define rFIQCSLV1    (*(volatile unsigned *)(rINTBase+0x5C)) /* current FIQ priority of slave register1 */
	#define rFIQCSLV2    (*(volatile unsigned *)(rINTBase+0x60)) /* current FIQ priority of slave register2 */
	#define rFIQCSLV3    (*(volatile unsigned *)(rINTBase+0x64)) /* current FIQ priority of slave register3 */
	#define rFIQCMST     (*(volatile unsigned *)(rINTBase+0x68)) /* current FIQ priority of master register */
	#define rFIQISPR     (*(volatile unsigned *)(rINTBase+0x6C)) /* FIQ service pending register */
	#define rFIQISPC     (*(volatile unsigned *)(rINTBase+0x70)) /* FIQ service clear register */
	#define rPOLARITY    (*(volatile unsigned *)(rINTBase+0x74))
	#define rIVEC_ADDR   (*(volatile unsigned *)(rINTBase+0x78))
	#define rFVEC_ADDR   (*(volatile unsigned *)(rINTBase+0x7C))
	
	/* 11.SSP(prime cell) Synchronous serial port register(16bit) */
	
	
	/* 12.KMI0(prime cell) Keyboard/Mouse interface register(8bit) */
	
	/* 13.KMI1(prime cell) Keyboard/Mouse interface register(8bit) */
	
	
	/* 14.UART0(prime cell) register(8bit)*/
	
	
	/* 15.UART1(prime cell) register(8bit)*/
	
	
	/* 16.Power manager register */
	#define rPLLCON      (*(volatile unsigned *)(rPMBase+0x00)) /* pll configuration register */
	#define rMODCON      (*(volatile unsigned *)(rPMBase+0x04)) /* mode control register */
	#define rLOCKCON     (*(volatile unsigned *)(rPMBase+0x08)) /* Lock-up timer */
	#define rHCLKCON     (*(volatile unsigned *)(rPMBase+0x0C)) /* Normal system clock control register */
	
	
	/* 17.Teak base register */
	
	//-    timer register 
	/*
	 *	Bits 	Name 	Type 	Function 	 
	 *	15:12 	-	Read 	Reserved. Read only as zero 	 
	 *	11:10 	M 	Read/write 	Operating mode :
	 *				00 : Free running timer mode(default) 	01 : Periodic timer mode. 	 
	 *				10 : Free running counter mode. 		11 : Periodic counter mode. 	 
	 *	9:8 	ES 	Read/write 	External input active edge selection. 
	 *				00 : Positive edge(default). 01 : Negative edge.
	 *				10 : Both positive and negative edge. 11 : unused. 	 
	 *	7 	-	Read 	Reserved. Read only as zero 	 
	 *	6 	OM 	Read/write 	Time output mode. 0 : Toggle mode(default). 1 : Pulse mode. 	 
	 *	5 	UDS 	Read/write 	Up/down counting control selection. 
	 *				0 : Up/down is controlled by UD field of TxCTR register(default).
	 *				1 : Up/down is controlled by EXTUD[4:0]input register. 	 
	 *	4 	UD 	Read/write 	Up/down counting selection. 
	 *				0 : Down counting(default). 1 : Up counting. 	 
	 *				This bit affects the counting of timer only when UDS bit is LOW. 	 
	 *	3 	-	Read 	Reserved. Read only as zero 	 
	 *	2 	OE 	Read/write 	Output enable.
	 *				0 : Disable timer outputs(default). 1 : Enable timer outputs. 	 
	 *				This bit affects the generation of timer interrupt only when TE bit is HIGH. 	 
	 *	1 	IE 	Read/write 	Interrupt enable. 0 : Toggle mode(default). 1 : Pulse mode. 	 
	 *				This bit affects the generation of timer output only when TE bit is HIGH. 	 
	 *	0 	TE 	Read/write 	Timer enable. 0 : Diable timer(default). 1 : Enable timer. 	 
	 */
	#define TMR_TE_DISABLE				0x0000
	#define TMR_TE_ENABLE				0x0001
	
	#define TMR_IE_TOGGLE				0x0000
	#define TMR_IE_PULSE				0x0002
	
	#define TMR_OE_DISABLE				0x0000
	#define TMR_OE_ENABLE				0x0004
	
	#define TMR_UD_DOWN				0x0000
	#define TMR_UD_UP					0x0010
	
	#define TMR_UDS_TxCTR				0x0000
	#define TMR_UDS_EXTUD				0x0020
	
	#define TMR_OM_TOGGLE				0x0000
	#define TMR_OM_PULSE				0x0040
	
	#define TMR_ES_POS					0x0000
	#define TMR_ES_NEG					0x0100
	#define TMR_ES_BOTH				0x0200
	
	#define TMR_M_FREE_TIMER			0x0000
	#define TMR_M_PERIODIC_TIMER		0x0400
	#define TMR_M_FREE_COUNTER		0x0800
	#define TMR_M_PERIODIC_COUNTER	0x0C00

	
	/* 18.memory stick Host controller-External Device 1*/
	#define COMD_REG  (*(volatile unsigned short *)(rEXT1Base+0x0))     /* Command Register */
	#define STAT_REG  (*(volatile unsigned short *)(rEXT1Base+0x4))  /* Status Register */
	#define CONT_REG  (*(volatile unsigned short *)(rEXT1Base+0x4))  /* Control Register */
	#define RECV_REG  (*(volatile unsigned short *)(rEXT1Base+0x8))  /* Receive Data Register */
	#define SEND_REG  (*(volatile unsigned short *)(rEXT1Base+0x8))  /* Send Data Register */
	#define INTD_REG  (*(volatile unsigned short *)(rEXT1Base+0xc))  /* Interrupt Data Register */
	#define INTC_REG  (*(volatile unsigned short *)(rEXT1Base+0xc))  /* Interrupt Control Register */
	#define PARD_REG  (*(volatile unsigned short *)(rEXT1Base+0x10)) /* Parallel Data Register */
	#define PARC_REG  (*(volatile unsigned short *)(rEXT1Base+0x10)) /* Parallel Control Register */
	#define CONT2_REG  (*(volatile unsigned short *)(rEXT1Base+0x14))
	#define ACD_REG  (*(volatile unsigned short *)(rEXT1Base+0x18))


	/* Searching Keyword: OS_Timer */
	#define SYS_TIMER03_PRESCALER   0x6B      /* for System Timer, 4usec(3.996)  */
	#define SYS_TIMER03_DIVIDER	0x01

	#define RESCHED_PERIOD          10      /* 10 ms */
	#define __KERNEL_HZ			100

#endif /* __S5C7375_H */
