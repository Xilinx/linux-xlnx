/*
 * include/asm-arm/arch-s3c24a0/smdk.h
 *
 * Changes
 *
 * 2004/06/10 <heechul.yun@samsung.com>  CPLD IDE code added
 *
 */

#ifndef _SMDK24A0_H_
#define _SMDK24A0_H_

/* Externl clock frequency used by CPU */
#define FIN     12000000

/*
 * on SMDK24A0,
 * there are so many cross-interference jumpers (h/w switch).
 */

/*
 * This is for SPJ Board - hcyun

 XgpIO0  <------ EINT0
 XgpIO1  <------ EINT1
 XgpIO2  <------ SD_INT
 XgpIO3  ------> XGPIO_nSS              <-- not used
 XgpIO4  ------> LED0
 XgpIO5  ------> LED1
 XgpIO6  ------> LED2
 XgpIO7  ------> LED3
 XgpIO8  <----->
 XgpIO9  ------>
 XgpIO10 <------ EINT10                 <-- not used
 XgpIO11 <------ EINT11                 <-- not used
 XgpIO12 <------ MODEM_INT              <-- not used
 XgpIO13 <------ ETHER_INT
 XgpIO14 <------ SMC_INT                <-- not used
 XgpIO15 ------> SMC_WP                 <-- l3-bit-elfin.c I2C??? l3 bus
 XgpIO16 <------ SPJ IDE                <-- IDE & l3-bit-elfin.c I2C??? l3 bus
 XgpIO17 <------ SPJ USB                <-- USB
 XgpIO18 <-----> KP_ROW0
 XgpIO19 <-----> KP_ROW1                <-- s3c24a0_keyif.c
 XgpIO20 <-----> KP_ROW2                <-- s3c24a0_keyif.c
 XgpIO21 <-----> KP_ROW3
 XgpIO22 <-----> KP_ROW4
 XgpIO23 <-----> KP_COL0
 XgpIO24 <-----> KP_COL1
 XgpIO25 <-----> KP_COL2
 XgpIO26 <-----> KP_COL3
 XgpIO27 <-----> KP_COL4
 XgpIO28 <----->
 XgpIO29 <----->
 XgpIO30 <----->
 XgpIO31 <----->

 *
 */

#define SMDK_SMC_WP      GPIO_15    /* O   : SMC Write-Protect */

#define SMDK_CAM_SCL     GPIO_9     /* O   : Camera I2C/SCCB clock */
#define SMDK_CAM_SDA     GPIO_8     /* I/O : Camera I2C/SCCB data */
#define SMDK_LED7        GPIO_7     /* O   : LED3, Low-Active */
#define SMDK_LED6        GPIO_6     /* O   : LED2, LOw-Active */
#define SMDK_LED5        GPIO_5     /* O   : LED1, LOw-Active */
#define SMDK_LED4        GPIO_4     /* O   : LED0, Low-Active */

/* GPIO buttons. EINT 0,1,10,11 */
#define SMDK_EINT0_IRQ  IRQ_EINT0
#define SMDK_EINT1_IRQ  IRQ_EINT1
#define SMDK_EINT10_IRQ IRQ_EINT10
#define SMDK_EINT11_IRQ IRQ_EINT11
#define SMDK_EINT0_GPIO GPIO_0
#define SMDK_EINT1_GPIO GPIO_1
#define SMDK_EINT10_GPIO GPIO_10
#define SMDK_EINT11_GPIO GPIO_11

#ifdef CONFIG_MMU
  #define SROM_BANK1_PBASE                        0x04000000
  #define SROM_BANK1_VBASE                        0xf0000000
#else /* UCLINUX */
  #define SROM_BANK1_PBASE                        0x04000000
  #define SROM_BANK1_VBASE                        0x04000000
#endif /* CONFIG_MMU */

#ifndef __ASSEMBLY__
/*
 * BANK1 control for cs89x0, IDE, USB2.0 - hcyun
 */
typedef struct {
        unsigned long bw;
        unsigned long bc;
} bank_param_t;

#define B1_STATE_NONE -1
#define B1_IDE_PIO0 0
#define B1_IDE_PIO4 1
#define B1_CS89x0       2
#define B1_USB2         3
#define B1_STATE_LIMIT 3

#endif


/* CPLD IDE - hcyun
 * 0x07000000   [0] : IDE reset
 *                              [1] : 0 - USB, 1 - IDE
 */

#define SMDK_CPLD_IDE_IRQ_GPIO          GPIO_4
#define SMDK_CPLD_IDE_IRQ               IRQ_EINT4
#define SMDK_CPLD_IDE_VIO               (SROM_BANK1_VBASE + 0x03000000) // 0xf3000000
#define SMDK_CPLD_IDE_PIO               (SROM_BANK1_PBASE + 0x03000000) // 0x04000000


/* CPLD USB - hcyun
 * 0x06000000   [0] : USB reset
 */
/*seo 20040616 */
#define SMDK_CPLD_USB_IRQ_GPIO          GPIO_5
#define SMDK_CPLD_USB_IRQ               IRQ_EINT5
#define SMDK_CPLD_USB_VIO               (SROM_BANK1_VBASE + 0x02000000)
#define SMDK_CPLD_USB_PIO               (SROM_BANK1_PBASE + 0x02000000)


/* CS8900A */
#define SMDK_CS8900_IRQ_GPIO   GPIO_13
#define SMDK_CS8900_IRQ        IRQ_EINT13
#define SMDK_CS8900_VIO        SROM_BANK1_VBASE
#define SMDK_CS8900_PIO        (SROM_BANK1_PBASE | (1<<24))

/* IRDA */
#define SMDK_IRDA_SDBW          (GPIO_MODE_IrDA_SDBW | GPIO_16 | GPIO_PULLUP_DIS)
#define SMDK_IRDA_TXD           (GPIO_MODE_IrDA_TXD | GPIO_17 | GPIO_PULLUP_DIS)
#define SMDK_IRDA_RXD           (GPIO_MODE_IrDA_RXD | GPIO_18 | GPIO_PULLUP_DIS)

/* UART */
#define SMDK_UART1_nCTS         (GPIO_MODE_UART | GPIO_28 | GPIO_PULLUP_DIS)
#define SMDK_UART1_nRTS         (GPIO_MODE_UART | GPIO_29 | GPIO_PULLUP_DIS)
#define SMDK_UART1_TXD          (GPIO_MODE_UART | GPIO_30 | GPIO_PULLUP_DIS)
#define SMDK_UART1_RXD          (GPIO_MODE_UART | GPIO_31 | GPIO_PULLUP_DIS)

#endif /* _SMDK24A0_H_ */
