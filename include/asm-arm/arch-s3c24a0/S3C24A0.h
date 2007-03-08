/*
 * linux/include/asm-arm/arch-s3c24a0/S3C24A0.h
 *
 * $Id: S3C24A0.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 *
 */

#ifndef _S3C24A0_H_
#define _S3C24A0_H_

#include "hardware.h"
#include "bitfield.h"

/*
 * clock and power ( chapter 32 )
 */

#define LOCKTIME        __REG(0x40000000)
#define XTALWSET        __REG(0x40000004)
#define MPLLCON         __REG(0x40000010)
#define UPLLCON         __REG(0x40000014)
#define CLKCON          __REG(0x40000020)
#define CLKSRC          __REG(0x40000024)
#define CLKDIVN         __REG(0x40000028)
#define POWERMAN        __REG(0x40000030)
#define SOFTRST         __REG(0x40000038)

/* fields */
#define fLOCK_U         Fld(12,16) /* UPLL lock time in LOCKTIME */
#define fLOCK_M         Fld(12,0)  /* MPLL lock time in LOCKTIME */
#define fXTAL_U         Fld(16,16) /* UPLL wait time in XTALWSET */
#define fXTAL_M         Fld(16,0)  /* MPLL wait time in XTALWSET */
#define fPLL_MDIV       Fld(8,12)
#define fPLL_PDIV       Fld(6,4)
#define fPLL_SDIV       Fld(2,0)
#define fEXTDIV         Fld(3,0)   /* external clock div. in CLKSRC */
#define fCLK_CAMDIV     Fld(4,8)   /* CAM clock div. in CLKDIV */
#define fCLK_MP4DIV     Fld(4,4)   /* MPEG4 clock div. in CLKDIV */
#define fCNFG_BF        Fld(2,9)   /* battery fault handling config in PWRMAN */
#define fSLEEP_CODE     Fld(8,0)   /* sleep mode setting code in PWRMAN */
/* bits */
#define CLKCON_VPOST    (1<<25) /* CLKCON */
#define CLKCON_MPEG4IF  (1<<24)
#define CLKCON_CAM_UPLL (1<<23)
#define CLKCON_LCD      (1<<22)
#define CLKCON_CAM_HCLK (1<<21)
#define CLKCON_MPEG4    (1<<20)
#define CLKCON_KEYPAD   (1<<19)
#define CLKCON_ADC      (1<<18)
#define CLKCON_SD       (1<<17)
#define CLKCON_MS       (1<<16)      /* memory stick */
#define CLKCON_USBD     (1<<15)
#define CLKCON_GPIO     (1<<14)
#define CLKCON_IIS      (1<<13)
#define CLKCON_IIC      (1<<12)
#define CLKCON_SPI      (1<<11)
#define CLKCON_UART1    (1<<10)
#define CLKCON_UART0    (1<<9)
#define CLKCON_PWM      (1<<8)
#define CLKCON_USBH     (1<<7)
#define CLKCON_AC97     (1<<6)
#define CLKCON_EAHB     (1<<5)
#define CLKCON_IrDA     (1<<4)
#define CLKCON_IDLE     (1<<2)
#define CLKCON_MON      (1<<1)
#define CLKCON_STOP     (1<<0)
#define CLKSRC_OSC      (1<<8) /* CLKSRC */
#define CLKSRC_nUPLL    (1<<7)
#define CLKSRC_nPLL     (1<<5)
#define CLKSRC_EXT      (1<<4)
#define CLKDIV_HCLK     (1<<1) /* CLKDIV */
#define CLKDIV_PCLK     (1<<0)
#define PWRMAN_MASKTS   (1<<8) /* PWRMAN */

//#define fCLKDIVN_BUS    Fld(2,0)      /* S3C24A0X */
#define fCLKDIVN_BUS    Fld(3,0)        /* SW.LEE: S3C24A0A */
#define CLKDIVN_BUS     FExtr(CLKDIVN, fCLKDIVN_BUS)
#define CLKDIVN_CAM(x)  FInsrt((x), fCLK_CAMDIV)
#define CLKDIVN_CAM_MSK FMsk(fCLK_CAMDIV)
#define CLKDIVN_CAM_VAL FExtr(CLKDIVN, fCLK_CAMDIV)
#define CLKDIVN_MP4(x)  FInsrt((x), fCLK_MP4DIV)
#define CLKDIVN_MP4_MSK FMsk(fCLK_MP4DIV)


/*
 * PWM timer ( chapter 7 )
 *
 * five 16bit timers.
 * two 8bit prescalers, four 4bit dividers
 * programmable duty control of output waveform
 * auto-load mode, one-shot pulse mode
 * dead-zone generator
 */
#define bPWM_TIMER(Nb)      __REG(0x44000000 + (Nb))
#define bPWM_BUFn(Nb,x)     bPWM_TIMER(0x0c + (Nb)*0x0c + (x))
/* Registers */
#define TCFG0           __REG(0x44000000)
#define TCFG1           __REG(0x44000004)
#define TCON            __REG(0x44000008)
#define TCNTB0          __REG(0x4400000C)
#define TCMPB0          __REG(0x44000010)
#define TCNTO0          __REG(0x44000014)
#define TCNTB1          bPWM_BUFn(1,0x0)
#define TCMPB1          bPWM_BUFn(1,0x4)
#define TCNTO1          bPWM_BUFn(1,0x8)
#define TCNTB2          bPWM_BUFn(2,0x0)
#define TCMPB2          bPWM_BUFn(2,0x4)
#define TCNTO2          bPWM_BUFn(2,0x8)
#define TCNTB3          bPWM_BUFn(3,0x0)
#define TCMPB3          bPWM_BUFn(3,0x4)
#define TCNTO3          bPWM_BUFn(3,0x8)
#define TCNTB4          bPWM_BUFn(4,0x0)
#define TCNTO4          bPWM_BUFn(4,0x4)

#define fTCFG0_DZONE    Fld(8,16) /* the dead zone length (= timer 0) */
#define fTCFG0_PRE1     Fld(8,8)  /* prescaler value for time 2,3,4 */
#define fTCFG0_PRE0     Fld(8,0)  /* prescaler value for time 0,1 */
#define SET_PRESCALER0(x)       ({ TCFG0 = (TCFG0 & ~(0xff)) | (x); })
#define GET_PRESCALER0()        FExtr(TCFG0, fTCFG0_PRE0)
#define SET_PRESCALER1(x)       ({ TCFG0 = (TCFG0 & ~(0xff << 8)) | ((x) << 8); })
#define GET_PRESCALER1()        FExtr(TCFG0, fTCFG0_PRE0)

#define fTCFG1_DMA      Fld(4,20) /* select DMA request channel */
#define fTCFG1_T4MUX    Fld(4,16) /* timer4 input mux */
#define fTCFG1_T3MUX    Fld(4,12) /* timer3 input mux */
#define fTCFG1_T2MUX    Fld(4,8)  /* timer2 input mux */
#define fTCFG1_T1MUX    Fld(4,4)  /* timer1 input mux */
#define fTCFG1_T0MUX    Fld(4,0)  /* timer0 input mux */
#define TIMER0_DIV(x)   FInsrt((x), fTCFG1_T0MUX)
#define TIMER1_DIV(x)   FInsrt((x), fTCFG1_T1MUX)
#define TIMER2_DIV(x)   FInsrt((x), fTCFG1_T2MUX)
#define TIMER3_DIV(x)   FInsrt((x), fTCFG1_T3MUX)
#define TIMER4_DIV(x)   FInsrt((x), fTCFG1_T4MUX)

#define fTCON_TIMER4    Fld(3,20)
#define fTCON_TIMER3    Fld(4,16)
#define fTCON_TIMER2    Fld(4,12)
#define fTCON_TIMER1    Fld(4,8)
#define fTCON_TIMER0    Fld(5,0)

#define fCNTB           Fld(16,0)
#define fCNTO           Fld(16,0)
#define fCMPB           Fld(16,0)

#define TCFG0_DZONE(x)  FInsrt((x), fTCFG0_DZONE)
#define TCFG0_PRE1(x)   FInsrt((x), fTCFG0_PRE1)
#define TCFG0_PRE0(x)   FInsrt((x), fTCFG0_PRE0)
#define TCON_4_AUTO             (1 << 22)       /* auto reload on/off for Timer 4 */
#define TCON_4_UPDATE   (1 << 21)       /* manual Update TCNTB4 */
#define TCON_4_ONOFF    (1 << 20)       /* 0: Stop, 1: start Timer 4 */
#define COUNT_4_ON              (TCON_4_ONOFF*1)
#define COUNT_4_OFF             (TCON_4_ONOFF*0)
#define TCON_3_AUTO             (1 << 19)       /* auto reload on/off for Timer 3 */
#define TCON_3_INVERT   (1 << 18)       /* 1: Inverter on for TOUT3 */
#define TCON_3_MAN              (1 << 17)       /* manual Update TCNTB3,TCMPB3 */
#define TCON_3_ONOFF    (1 << 16)       /* 0: Stop, 1: start Timer 3 */
#define TCON_2_AUTO             (1 << 15)       /* auto reload on/off for Timer 3 */
#define TCON_2_INVERT   (1 << 14)       /* 1: Inverter on for TOUT3 */
#define TCON_2_MAN              (1 << 13)       /* manual Update TCNTB3,TCMPB3 */
#define TCON_2_ONOFF    (1 << 12)       /* 0: Stop, 1: start Timer 3 */
#define TCON_1_AUTO             (1 << 11)       /* auto reload on/off for Timer 3 */
#define TCON_1_INVERT   (1 << 10)       /* 1: Inverter on for TOUT3 */
#define TCON_1_MAN              (1 << 9)        /* manual Update TCNTB3,TCMPB3 */
#define TCON_1_ONOFF    (1 << 8)        /* 0: Stop, 1: start Timer 3 */
#define TCON_0_AUTO             (1 << 3)        /* auto reload on/off for Timer 3 */
#define TCON_0_INVERT   (1 << 2)        /* 1: Inverter on for TOUT3 */
#define TCON_0_MAN              (1 << 1)        /* manual Update TCNTB3,TCMPB3 */
#define TCON_0_ONOFF    (1 << 0)        /* 0: Stop, 1: start Timer 3 */

#define TIMER3_ATLOAD_ON        (TCON_3_AUTO*1)
#define TIMER3_ATLAOD_OFF       FClrBit(TCON, TCON_3_AUTO)
#define TIMER3_IVT_ON           (TCON_3_INVERT*1)
#define TIMER3_IVT_OFF          (FClrBit(TCON, TCON_3_INVERT))
#define TIMER3_MANUP            (TCON_3_MAN*1)
#define TIMER3_NOP              (FClrBit(TCON, TCON_3_MAN))
#define TIMER3_ON               (TCON_3_ONOFF*1)
#define TIMER3_OFF              (FClrBit(TCON, TCON_3_ONOFF))
#define TIMER2_ATLOAD_ON        (TCON_2_AUTO*1)
#define TIMER2_ATLAOD_OFF       FClrBit(TCON, TCON_2_AUTO)
#define TIMER2_IVT_ON           (TCON_2_INVERT*1)
#define TIMER2_IVT_OFF          (FClrBit(TCON, TCON_2_INVERT))
#define TIMER2_MANUP            (TCON_2_MAN*1)
#define TIMER2_NOP              (FClrBit(TCON, TCON_2_MAN))
#define TIMER2_ON               (TCON_2_ONOFF*1)
#define TIMER2_OFF              (FClrBit(TCON, TCON_2_ONOFF))
#define TIMER1_ATLOAD_ON        (TCON_1_AUTO*1)
#define TIMER1_ATLAOD_OFF       FClrBit(TCON, TCON_1_AUTO)
#define TIMER1_IVT_ON           (TCON_1_INVERT*1)
#define TIMER1_IVT_OFF          (FClrBit(TCON, TCON_1_INVERT))
#define TIMER1_MANUP            (TCON_1_MAN*1)
#define TIMER1_NOP              (FClrBit(TCON, TCON_1_MAN))
#define TIMER1_ON               (TCON_1_ONOFF*1)
#define TIMER1_OFF              (FClrBit(TCON, TCON_1_ONOFF))
#define TIMER0_ATLOAD_ON        (TCON_0_AUTO*1)
#define TIMER0_ATLAOD_OFF       FClrBit(TCON, TCON_0_AUTO)
#define TIMER0_IVT_ON           (TCON_0_INVERT*1)
#define TIMER0_IVT_OFF          (FClrBit(TCON, TCON_0_INVERT))
#define TIMER0_MANUP            (TCON_0_MAN*1)
#define TIMER0_NOP              (FClrBit(TCON, TCON_0_MAN))
#define TIMER0_ON               (TCON_0_ONOFF*1)
#define TIMER0_OFF              (FClrBit(TCON, TCON_0_ONOFF))

#define TCON_TIMER1_CLR         FClrFld(TCON, fTCON_TIMER1);
#define TCON_TIMER2_CLR         FClrFld(TCON, fTCON_TIMER2);
#define TCON_TIMER3_CLR         FClrFld(TCON, fTCON_TIMER3);


/*
 * NAND ( chapter 4 )
 *
 */
#include "s3c24a0_nand.h"

/* S3C24A0-A LCD CONTROLLER DEVICE ONLY */
#ifdef  CONFIG_ARCH_S3C24A0A
#define bLCD_CTL(Nb)            __REG(0x4a000000 + (Nb))
#define LCDCON1                 bLCD_CTL(0x00) /* LCD CONTROL 1 */
#define LCDCON2                 bLCD_CTL(0x04) /* LCD CONTROL 2 */
#define LCDTCON1                bLCD_CTL(0x08) /* LCD TIME CONTROL 1 */
#define LCDTCON2                bLCD_CTL(0x0c) /* LCD TIME CONTROL 2 */
#define LCDTCON3                bLCD_CTL(0x10) /* LCD TIME CONTROL 3 */
#define LCDOSD1                 bLCD_CTL(0x14) /* LCD OSD CONTROL REGISTER */
#define LCDOSD2                 bLCD_CTL(0x18) /* Foreground image(OSD Image) left top position set */
#define LCDOSD3                 bLCD_CTL(0x1c) /* Foreground image(OSD Image) right bottom position set */
#define LCDSADDRB1              bLCD_CTL(0x20) /* Frame buffer start address 1 (Background buffer 1) */
#define LCDSADDRB2              bLCD_CTL(0x24) /* Frame buffer start address 2 (Background buffer 2) */
#define LCDSADDRF1              bLCD_CTL(0x28) /* Frame buffer start address 1 (Foreground buffer 1) */
#define LCDSADDRF2              bLCD_CTL(0x2c) /* Frame buffer start address 2 (Foreground buffer 2) */
#define LCDEADDRB1              bLCD_CTL(0x30) /* Frame buffer end address 1 (Background buffer 1) */
#define LCDEADDRB2              bLCD_CTL(0x34) /* Frame buffer end address 2 (Background buffer 2) */
#define LCDEADDRF1              bLCD_CTL(0x38) /* Frame buffer end address 1 (Foreground buffer 1) */
#define LCDEADDRF3              bLCD_CTL(0x3c) /* Frame buffer end address 2 (Foreground buffer 2) */
#define LCDVSCRB1               bLCD_CTL(0x40) /* Virture Screen OFFSIZE and PAGE WIDTH (Background buffer 1) */
#define LCDVSCRB2               bLCD_CTL(0x44) /* Virture Screen OFFSIZE and PAGE WIDTH (Background buffer 2) */
#define LCDVSCRF1               bLCD_CTL(0x48) /* Virture Screen OFFSIZE and PAGE WIDTH (Foreground buffer 1) */
#define LCDVSCRF2               bLCD_CTL(0x4c) /* Virture Screen OFFSIZE and PAGE WIDTH (Foreground buffer 2) */
#define LCDINTCON               bLCD_CTL(0x50) /* LCD Interrupt Control */
#define LCDKEYCON               bLCD_CTL(0x54) /* COLOR KEY CONTROL 1 */
#define LCDKEYVAL               bLCD_CTL(0x58) /* COLOR KEY CONTROL 2 */
#define LCDBGCON                bLCD_CTL(0x5c) /* Background color Control */
#define LCDFGCON                bLCD_CTL(0x60) /* Foreground color Control */
#define LCDDITHCON              bLCD_CTL(0x64) /* LCD Dithering control active Matrix */

#define PALETTEBG                       0x4A001000 //Background Palette start address
#define PALETTEFG                       0x4A002000 //Background Palette start address

/*  LCDCON1 */
#define fBURSTLEN       Fld(2,28)       /* DMA's BURST length selection*/
#define BURSTLEN4       FInsrt(0x2, fBURSTLEN)
#define BURSTLEN8       FInsrt(0x1, fBURSTLEN)
#define BURSTLEN16      FInsrt(0x0, fBURSTLEN)
#define BDBCON_BUF1     (0 << 21)       /* Active frame slect control background image */
#define BDBCON_BUF2     (1 << 21)     /* it will be adoted from next frame data */
#define FDBCON_BUF1     (0 << 20)     /* Active frame select control foreground image */
#define FDBCON_BUF2     (1 << 20)       /* it will adopted from next frame data  */
#define DIVEN           (1 << 19)       /* 1:ENABLE 0:Disable */
#define DIVDIS          (0 << 19)       /* 0:disable */
#define fCLKVAL         Fld(6,13)
#define CLKVALMSK       FMsk(fCLKVAL)   /* clk value bit clear */
#define CLKVAL(x)       FInsrt((x), fCLKVAL) /*  VCLK = HCLK / [(CLKVAL+1)x2] */
#define CLKDIR_DIVIDE   (1 << 12)    /* Select the clk src as 0:direct or 1:divide using CLKVAl register*/
#define CLKDIR_DIRECT   (0 << 12)    /* Select the clk src as 0:direct or 1:divide using CLKVAl register*/
#define fPNRMODE        Fld(2,9)        /* Select Disaplay mode */
#define PNRMODE_PRGB    FInsrt(0x00, fPNRMODE)  /* parallel RGB */
#define PNRMODE_PBGR    FInsrt(0x01, fPNRMODE)  /* parallel BGR */
#define PNRMODE_SRGB    FInsrt(0x02, fPNRMODE)  /* Serial RGB */
#define PNRMODE_SBGR    FInsrt(0x03, fPNRMODE)  /* Serial RGB */
#define fBPPMODEF       Fld(3,6)        /* SELECT THE BPP MODE FOR FOREGROUND IMAGE (OSD)*/
#define BPPMODEF_8_P    FInsrt(0x3, fBPPMODEF)  /* 8BPP palettized */
#define BPPMODEF_8_NP   FInsrt(0x4, fBPPMODEF)  /* 8BPP non palettized RGB-3:3:2 */
#define BPPMODEF_565    FInsrt(0x5, fBPPMODEF)  /* 16BPP NON palettized RGB-5:6:5 */
#define BPPMODEF_5551   FInsrt(0x6, fBPPMODEF)  /* 16BPP NON palettized RGB-5:5:5:1*/
#define BPPMODEF_18_UP  FInsrt(0x7, fBPPMODEF)  /* unpaked 18BPP non-palettized */
#define fBPPMODEB       Fld(4,2)        /* select the BPP mode for fore ground image*/
#define MPPMODEB_1      FInsrt(0x00, fBPPMODEB) /* 1bpp */
#define MPPMODEB_2      FInsrt(0x01, fBPPMODEB) /* 2bpp */
#define MPPMODEB_4      FInsrt(0x02, fBPPMODEB) /* 4bpp */
#define MPPMODEB_8      FInsrt(0x03, fBPPMODEB) /* 8bpp palettized */
#define MPPMODEB_8N     FInsrt(0x04, fBPPMODEB) /* 8bpp non palettized 3:3:2*/
#define MPPMODEB_565    FInsrt(0x05, fBPPMODEB) /* 16bpp non palettized 5:6:5*/
#define MPPMODEB_5551   FInsrt(0x06, fBPPMODEB) /* 16bpp non palettized 5:5:5:1*/
#define MPPMODEB_18     FInsrt(0x07, fBPPMODEB) /* unpacked 18bpp */
#define ENVID           (1 << 1) /* 0:Disable 1:Enable LCD video output and logic immediatly */
#define ENVID_F         (1 << 0) /* 0:Dis 1:Ena wait until Current frame end. */

/* LCDCON2 */
#define fPALFRM         Fld(2,9) /* this bit determines the size of the palette data*/
#define PALFRM_666      FInsrt(0x01, fPALFRM)   /* 18 BIT RGB-6:6:6 */
#define PALFRM_565      FInsrt(0x02, fPALFRM)   /* 16 BIT RGB-5:6:5 */
#define PALFRM_5551     FInsrt(0x03, fPALFRM)   /* 16 BIT RGB-5:5:5:1 */
#define IVCLK_RISING    (1 << 7) /* this bit controls the polarity of the VCLK active edge */
#define IVCLK_FALLING   (0 << 7) /* 1 :rising edge 0: falling edge */
#define IHSYNC_INVERT   (1 << 6) /* HSYNC polarity inverted */
#define IHSYNC_NORMAL   (0 << 6) /* HSYNC polarity normal  */
#define IVSYNC_INVERT   (1 << 5) /* VSYNC polarity inverted */
#define IVSYNC_NORMAL   (0 << 5) /* VSYNC polarity normal  */
#define IVDE_INVERT     (1 << 3) /* DE polarity inverted */
#define IVDE_NORMAL     (0 << 3) /* DE polarity normal  */
#define BITSWP_EN       (1 << 2) /* 1:BIT Swap Enable */
#define BITSWP_DIS      (0 << 2) /* 0:BIT Swap Disable */
#define BYTESWP_EN      (1 << 1) /* 1:BYTE Swap Enable */
#define BUTESWP_DIS     (0 << 1) /* 0:BYTE Swap Disable */
#define HAWSWP_EN       (1 << 0) /* 1:HALF WORD Swap Enable */
#define HAWSWP_DIS      (0 << 0) /* 0:HALF WORD swap Disable */

/* LCD Time Control 1 Register */
#define VBPD(x)         FInsrt((x), Fld(8,16))  /* VSync Back porch */
#define VFPD(x)         FInsrt((x), Fld(8, 8))  /* VSync Front porch */
#define VSPW(x)         FInsrt((x), Fld(8, 0))  /* VSync level width */
/* LCD Time Control 2 Register */
#define HBPD(x)         FInsrt((x), Fld(8,16))  /* VSync Back porch */
#define HFPD(x)         FInsrt((x), Fld(8, 8))  /* VSync Front porch */
#define HSPW(x)         FInsrt((x), Fld(8, 0))  /* VSync level width */
/* LCD Time Control 3 register */
#define LINEVAL(x)      FInsrt((x), Fld(11,11)) /* these bits determine the vertical size of lcd panel */
#define HOZVAL(x)       FInsrt((x), Fld(11, 0)) /* these bits determine the horizontal size of lcd panel*//* LCD OSD Control 1 register */
#define OSDEN           (1 << 9)        /* OSD  Enable */
#define OSDDIS          (0 << 9)        /* OSD Disable */
#define OSD_BLD_PIX     (1 << 8)        /* BLENDING MODE Per pixel blending (18 BPP only) */
#define OSD_BLD_PLANE   (0 << 8)        /* Per plane blending (8/16/18 BPP mode) */
#define OSD_ALPHA(x)    FInsrt((x), Fld(8,0))   /* 8-bit Alpha value for Per plane defined by Equation 28-1. */
/* LCD OSD Control 2 Register */
#define OSD_LEFTTOP_X(x)  FInsrt((x), Fld(11,11)) /*Horizontal screen coordinate for left top pixel of OSD image*/
#define OSD_LEFTTOP_Y(x)  FInsrt((y), Fld(11, 0)) /* Vertical screen coordinate for left top pixel of OSD image*/
/* LCD OSD Control 3 Register */
/*OSD_RIGHTBOT_X,_Y <= LCD Panel size of X, Y */
#define OSD_RIGHTBOT_X(x) FInsrt((x), Fld(11,11)) /*Hor scr coordinate for right bottom pixel of OSD image. */
#define OSD_RIGHTBOT_Y(y) FInsrt((y), Fld(11, 0)) /* Ver scr coordinate for right bottom pixel of OSD image.*/
/* FRAME Buffer start address Register
        LCDSADDRB1 Frame buffer start address register for Background buffer 1
        LCDSADDRB2 Frame buffer start address register for Background buffer 2
        LCDSADDRF1 Frame buffer start address register for Foreground(OSD) buffer 1
        LCDSADDRF2 Frame buffer start address register for Foreground(OSD) buffer 2*/
#define LCDBANK(x)      FInsrt((x), Fld( 8,24)) /* the bank location for the video buffer in the system memory. */
#define LCDBASEU(x)     FInsrt((x), Fld(24, 0)) /* the start address of the LCD frame buffer. */
/* FRAME BUFFER END address Register
        LCDEADDRB1 Frame buffer end address register for Background buffer 1
        LCDEADDRB2 Frame buffer end address register for Background buffer 2
        LCDEADDRF1 Frame buffer end address register for Foreground(OSD)  buffer 1
        LCDEADDRF2 Frame buffer end address register for Foreground(OSD) buffer 2

        LCDBASEL = LCDBASEU + (PAGEWIDTH+OFFSIZE) x (LINEVAL+1)    */
#define LCDBASEL(x)     FInsrt((x), Fld(24,0)) /* the end address of the LCD frame buffer. */
/* Virture Screen offsize and page width registers
        LCDVSCRB1 Virtual screen OFFSIZE and PAGEWIDTH for Background buffer 1
        LCDVSCRB2 Virtual screen OFFSIZE and PAGEWIDTH for Background buffer 2
        LCDVSCRF1 Virtual screen OFFSIZE and PAGEWIDTH for Foreground(OSD) buffer 1
        LCDVSCRF2 Virtual screen OFFSIZE and PAGEWIDTH for Foreground(OSD) buffer 2*/
#define OFFSIZE(x)      FInsrt((x), Fld(13,13)) /* Virtual screen offset size (the number of byte). */
#define PAGEWIDTH(x)    FInsrt((x), Fld(13, 0)) /* Virtual screen page width (the number of byte). */
/* LCD Interrupt Control Register */
#define fFRAME_INT2     Fld(2,10)       /* LCD Frame Interrupt 2 at start of  */
#define FRAMESEL0_BP    FInsrt(0x0, fFRAME_INT2) /* BACK Porch */
#define FRAMESEL0_VS    FInsrt(0x1, fFRAME_INT2) /* VSYNC */
#define FRAMESEL0_ACT   FInsrt(0x2, fFRAME_INT2) /* ACTIVE */
#define FRAMESEL0_FP    FInsrt(0x3, fFRAME_INT2) /* FRONT  */
#define fFRAME_INT1     Fld(2,8)        /* LCD Frame Interrupt 1 at start of */
#define FRAMESEL1_BP    FInsrt(0x1, fFRAME_INT1) /* BACK Porch */
#define FRAMESEL1_VS    FInsrt(0x2, fFRAME_INT1) /* VSYNC */
#define FRAMESEL1_FP    FInsrt(0x3, fFRAME_INT1) /* FRONTPorch */
#define INTFRAME_EN     (1 << 7) /* LCD Frame interrupt Enable */
#define INTFRAME_DIS    (0 << 7) /* LCD Frame interrupt Disable */
#define fFIFOSEL        Fld(2,5) /* LCD FIFO INTERRUPT SELECT BIT */
#define FIFO_ALL        FInsrt(0x00, fFIFOSEL) /* All fifi or CASE */
#define FIFO_BG         FInsrt(0x01, fFIFOSEL) /* Background only */
#define FIFO_FG         FInsrt(0x02, fFIFOSEL) /* FOREGROUND FIFO ONLY */
#define fFIFOLEVEL      Fld(3,2) /* LCD FIFO interrupt level select 1~128 word */
#define FIFO_32W        FInsrt(0x00, fFIFOLEVEL) /* 32 WORD LEFT */
#define FIFO_64W        FInsrt(0x01, fFIFOLEVEL) /* 64 WORD */
#define FIFO_96W        FInsrt(0x02, fFIFOLEVEL) /* 96 WORD */
#define FIFO_OR         FInsrt(0x03, fFIFOLEVEL) /* 32,64,96 WORD */
#define INTFIFO_EN      (1<<1)
#define INTFIFO_DIS     (1<<1)
#define LCD_INTEN       (1 << 0) /* LCD interrupt Enable  */
#define LCD_INTDIS      (0 << 0) /* LCD Interrupt Disable */
/* LCD color key LCDKEYCON 1 register */
#define KEYEN           (1 << 25) /* color key enable, blending disable */
#define KEYDIS          (0 << 25) /* color key disable, blending enable */
#define DIRCON_FORE     (1 << 24) /* pixel from foreground image is displayed (only in OSD area) */
#define DIRCON_BACK     (0 << 24) /* pixel from background image is displayed (only in OSD area) */
#define COMPKEY(x)      FInsrt((X), Fld(24,0)) /* Each bit is correspond to the COLVAL[23:0]. */
/* color key 2 register LCDCOLVAL */
#define COLVAL(x)       FInsrt((x), Fld(24,0)) /* Color key value for the transparent pixel effect. */
/* Background Color MAP */
#define BGCOLEN         (1 << 24) /* Background color mapping control bit enable */
#define BGCOLDIS        (0 << 24) /* Background color mapping control bit disable */
#define BGCOLOR(x)      FInsrt((x), Fld(24,0)) /* Color Value */
/* Foreground Color MAP LCDFGCON */
#define FGCOLEN         (1 << 24) /* Foreground color mapping control bit Enable. */
#define FGCOLDIS        (0 << 24) /* Foreground color mapping control bit Disable */
#define FGCOLOR(x)      FInsrt((x), Fld(24,0)) /* Color Value  */
/* Dithering Contrl 1 Register LCD DITHERING MODE */
#define RDITHPOS_6BIT   FInsrt(0x01, Fld(2,5)) /* Red Dither bit control 6bit */
#define RDITHPOS_5BIT   FInsrt(0x02, Fld(2,5)) /* Red Dither bit control 5bit */
#define GDITHPOS_6BIT   FInsrt(0x01, Fld(2,3)) /* Green Dither bit control 6bit */
#define GDITHPOS_5BIT   FInsrt(0x02, Fld(2,3)) /* Green Dither bit control 5bit */
#define BDITHPOS_6BIT   FInsrt(0x01, Fld(2,5)) /* Blue Dither bit control 6bit */
#define BDITHPOS_5BIT   FInsrt(0x02, Fld(2,5)) /* Blue Dither bit control 5bit */
#define DITHEN          (1 << 0) /* Dithering Enable bit */
#define DITHDIS         (0 << 0) /* Dithering Disable bit */

#else
/* S3C24A0-X DEVICE ONLY */
/*
 * LCD (chapter 27 )
 */
#define bLCD_CTL(Nb)     __REG(0x4a000000 + (Nb))
#define LCDCON1          bLCD_CTL(0x00)
#define LCDCON2          bLCD_CTL(0x04)
#define LCDCON3          bLCD_CTL(0x08)
#define LCDCON4          bLCD_CTL(0x0c)
#define LCDCON5          bLCD_CTL(0x10)
#define LCDADDR1         bLCD_CTL(0x14)
#define LCDADDR2         bLCD_CTL(0x18)
#define LCDADDR3         bLCD_CTL(0x1c)
#define TPAL             bLCD_CTL(0x50)
#define LCDINTPND        bLCD_CTL(0x54)
#define LCDSRCPND        bLCD_CTL(0x58)
#define LCDINTMSK        bLCD_CTL(0x5c)
#define OSD_SADDR        bLCD_CTL(0x6c)
#define OSD_EADDR        bLCD_CTL(0x70)
#define OSD_LT           bLCD_CTL(0x74) /* left top */
#define OSD_RB           bLCD_CTL(0x78) /* right bottom & control */
#define LCD_PAL          bLCD_CTL(0x400) /* palette register */

#define fLCD1_LINECNT    Fld(10,18)     /* the status of the line counter */
#define  LCD1_LINECNT    FMsk(fLCD_LINECNT)
#define fLCD1_CLKVAL     Fld(10,8)      /* rates of VCLK and CLKVAL[9:0] */
#define  LCD1_CLKVAL(x)  FInsrt((x), fLCD1_CLKVAL)
#define  LCD1_CLKVAL_MSK FMsk(fLCD1_CLKVAL)
#define fLCD1_PNR        Fld(2,5)       /* select the display mode */
#define  LCD1_PNR_TFT    FInsrt(0x3, fLCD1_PNR) /* TFT LCD */
#define fLCD1_BPP        Fld(4,1)       /* select BPP(Bit Per Pixel) */
#define  LCD1_BPP_1T     FInsrt(0x8, fLCD1_BPP) /* TFT: 1 bpp */
#define  LCD1_BPP_2T     FInsrt(0x9, fLCD1_BPP) /* TFT: 2 bpp */
#define  LCD1_BPP_4T     FInsrt(0xa, fLCD1_BPP) /* TFT: 4 bpp */
#define  LCD1_BPP_8T     FInsrt(0xb, fLCD1_BPP) /* TFT: 8 bpp */
#define  LCD1_BPP_16T    FInsrt(0xc, fLCD1_BPP) /* TFT: 16 bpp */
#define LCD1_ENVID       (1 << 0)       /* 1: Enable the video output */
#define fLCD2_VBPD       Fld(8,24)      /* Vertical Back Porch */
#define  LCD2_VBPD(x)    FInsrt(((x)-1), fLCD2_VBPD)
#define fLCD2_LINEVAL    Fld(10,14)  /* vertical size of LCD */
#define  LCD2_LINEVAL_MSK FMsk(fLCD2_LINEVAL)
#define  LCD2_LINEVAL(x) FInsrt(((x)-1), fLCD2_LINEVAL)
#define fLCD2_VFPD       Fld(8,6)    /* Vertical Front Porch */
#define  LCD2_VFPD(x)    FInsrt(((x)-1), fLCD2_VFPD)
#define fLCD2_VSPW       Fld(6,0)    /* Vertical Sync Pulse Width */
#define  LCD2_VSPW(x)    FInsrt(((x)-1), fLCD2_VSPW)
#define fLCD3_HBPD       Fld(7,19)   /* Horizontal Back Porch */
#define  LCD3_HBPD(x)    FInsrt(((x)-1), fLCD3_HBPD)
#define fLCD3_HOZVAL     Fld(11,8)      /* horizontal size of LCD */
#define  LCD3_HOZVAL_MSK FMsk(fLCD3_HOZVAL)
#define  LCD3_HOZVAL(x)  FInsrt(((x)-1), fLCD3_HOZVAL)
#define fLCD3_HFPD       Fld(8,0)       /* Horizontal Front Porch */
#define  LCD3_HFPD(x)    FInsrt(((x)-1), fLCD3_HFPD)
#define fLCD4_HSPW       Fld(8,0)       /* Horizontal Sync Pulse Width */
#define  LCD4_HSPW(x)    FInsrt(((x)-1), fLCD4_HSPW)
#define fLCD5_VSTAT      Fld(2,15)      /* Vertical Status (ReadOnly) */
#define  LCD5_VSTAT      FMsk(fLCD5_VSTAT)
#define  LCD5_VSTAT_VS   0x00   /* VSYNC */
#define  LCD5_VSTAT_BP   0x01   /* Back Porch */
#define  LCD5_VSTAT_AC   0x02   /* Active */
#define  LCD5_VSTAT_FP   0x03   /* Front Porch */
#define fLCD5_HSTAT      Fld(2,13)      /* Horizontal Status (ReadOnly) */
#define  LCD5_HSTAT      FMsk(fLCD5_HSTAT)
#define  LCD5_HSTAT_HS   0x00   /* HSYNC */
#define  LCD5_HSTAT_BP   0x01   /* Back Porch */
#define  LCD5_HSTAT_AC   0x02   /* Active */
#define  LCD5_HSTAT_FP   0x03   /* Front Porch */
#define LCD5_FRM565      (1 << 11) /* 1 : RGB 5:6:5 , 0 : RGB 5:5:5:1 */
#define LCD5_INVVCL      (1 << 10)      /*
                              1 : video data is fetched at VCLK falling edge
                              0 : video data is fetched at VCLK rising edge */
#define LCD5_HSYNC       (1 << 9) /* 1: HSYNC pulse polarity is inverted */
#define LCD5_VSYNC       (1 << 8) /* 1: VSYNC pulse polarity is inverted */
#define LCD5_INVVD       (1 << 7) /* 1: VD pulse polarity is inverted */
#define LCD5_INVVDEN     (1 << 6) /* 1: VDEN signal polarity is inverted */
#define LCD5_INVPWREN    (1 << 5) /* 1: PWREN signal polarity is inverted */
#define LCD5_INVLEND     (1 << 4) /* 1: LEND signal polarity is inverted */
#define LCD5_PWREN       (1 << 3) /* 1: enable PWREN signal */
#define LCD5_LEND        (1 << 2) /* 1: enable LEND signal */
#define LCD5_BSWP        (1 << 1) /* 1: Byte swap enable */
#define LCD5_HWSWP       (1 << 0) /* 1: HalfWord swap enable */

#define fLCDADDR_BANK     Fld(9,21)     /* bank location for video buffer */
#define  LCDADDR_BANK(x)  FInsrt((x), fLCDADDR_BANK)
#define fLCDADDR_BASEU    Fld(21,0)     /* address of upper left corner */
#define  LCDADDR_BASEU(x) FInsrt((x), fLCDADDR_BASEU)
#define fLCDADDR_BASEL    Fld(21,0)     /* address of lower right corner */
#define  LCDADDR_BASEL(x) FInsrt((x), fLCDADDR_BASEL)
#define fLCDADDR_OFFSET   Fld(11,11)    /* Virtual screen offset size
                                           (# of half words) */
#define  LCDADDR_OFFSET(x) FInsrt((x), fLCDADDR_OFFSET)
#define fLCDADDR_PAGE     Fld(11,0)     /* Virtual screen page width
                                           (# of half words) */
#define  LCDADDR_PAGE(x)  FInsrt((x), fLCDADDR_PAGE)

#define TPAL_LEN           (1 << 24)    /* 1 : Temp. Pallete Register enable */
#define fTPAL_VAL          Fld(24,0)    /* Temp. Pallete Register value */
#define  TPAL_VAL(x)       FInsrt((x), fTPAL_VAL)
#define  TPAL_VAL_RED(x)   FInsrt((x), Fld(8,16))
#define  TPAL_VAL_GREEN(x) FInsrt((x), Fld(8,8))
#define  TPAL_VAL_BLUE(x)  FInsrt((x), Fld(8,0))

#define fOSD_SADDR       Fld(30,0)  /* OSD DMA start address of A[30:1] */
#define  OSD_Saddr(x)    FInsrt((x), fOSD_SADDR)
#define fOSD_EADDR       Fld(30,0)  /* OSD DMA end address of A[30:1] */
#define  OSD_Eaddr(x)    FInsrt((x), fOSD_EADDR)
#define OSD_BLD          (1<<24) /* 0: per plane blending */
#define fOSD_ALPHA       Fld(4,20) /* 4-bit alpha value */
#define  OSD_ALPHA(x)    FInsrt((x), fOSD_ALPHA)
#define fOSD_LT_X        Fld(10,10) /* left-top X */
#define  OSD_LT_X(x)     FInsrt((x), fOSD_LT_X)
#define fOSD_LT_Y        Fld(10,0) /* left-top Y */
#define  OSD_LT_Y(x)     FInsrt((x), fOSD_LT_Y)
#define OSD_EN           (1<<31)  /* 1: enable OSD */
#define fOSD_WIDTH       Fld(11,20) /* OSD width . # of half words */
#define  OSD_WIDTH(x)    FInsrt((x), fOSD_WIDTH)
#define fOSD_RB_X        Fld(10,10) /* right bottom X */
#define  OSD_RB_X(x)     FInsrt((x), fOSD_RB_X)
#define fOSD_RB_Y        Fld(10,0)  /* right bottom Y */
#define  OSD_RB_Y(x)     FInsrt((x), fOSD_RB_Y)
#endif

/*
 * UART ( chapter 11 )
 */
#define UART_CTL_BASE     0x44400000
#define UART0_CTL_BASE    UART_CTL_BASE
#define UART1_CTL_BASE    (UART_CTL_BASE + 0x4000)
#define bUART(x, Nb)      __REG(UART_CTL_BASE + (x)*0x4000 + (Nb))
/* offset */
#define oULCON         0x00
#define oUCON          0x04
#define oUFCON         0x08
#define oUMCON         0x0c
#define oUTRSTAT       0x10
#define oUERSTAT       0x14
#define oUFSTAT        0x18
#define oUMSTAT        0x1c
#define oUTXH          0x20
#define oURXH          0x24
#define oUBRDIV        0x28
/* Registers */
#define ULCON0         bUART(0, oULCON)
#define UCON0          bUART(0, oUCON)
#define UFCON0         bUART(0, oUFCON)
#define UMCON0         bUART(0, oUMCON)
#define UTRSTAT0       bUART(0, oUTRSTAT)
#define UERSTAT0       bUART(0, oUERSTAT)
#define UFSTAT0        bUART(0, oUFSTAT)
#define UMSTAT0        bUART(0, oUMSTAT)
#define UTXH0          bUART(0, oUTXH)
#define URXH0          bUART(0, oURXH)
#define UBRDIV0        bUART(0, oUBRDIV)
#define ULCON1         bUART(1, oULCON)
#define UCON1          bUART(1, oUCON)
#define UFCON1         bUART(1, oUFCON)
#define UMCON1         bUART(1, oUMCON)
#define UTRSTAT1       bUART(1, oUTRSTAT)
#define UERSTAT1       bUART(1, oUERSTAT)
#define UFSTAT1        bUART(1, oUFSTAT)
#define UMSTAT1        bUART(1, oUMSTAT)
#define UTXH1          bUART(1, oUTXH)
#define URXH1          bUART(1, oURXH)
#define UBRDIV1        bUART(1, oUBRDIV)
/* ... */

#define ULCON_IR        (1 << 6)        /* use Infra-Red mode */
#define fULCON_PAR      Fld(3,3)        /* what parity mode? */
#define  ULCON_PAR      FMsk(fULCON_PAR)
#define  ULCON_PAR_NONE FInsrt(0x0, fULCON_PAR) /* No Parity */
#define  ULCON_PAR_ODD  FInsrt(0x4, fULCON_PAR) /* Odd Parity */
#define  ULCON_PAR_EVEN FInsrt(0x5, fULCON_PAR) /* Even Parity */
#define  ULCON_PAR_1    FInsrt(0x6, fULCON_PAR) /* Parity force/checked as 1 */
#define  ULCON_PAR_0    FInsrt(0x7, fULCON_PAR) /* Parity force/checked as 0 */
#define ULCON_STOP      (1 << 2)        /* The number of stop bits */
#define ULCON_ONE_STOP  (0 << 2)        /* 1 stop bit */
#define ULCON_TWO_STOP  (1 << 2)        /* 2 stop bit */
#define fULCON_WL       Fld(2, 0)       /* word length */
#define  ULCON_WL       FMsk(fULCON_WL)
#define  ULCON_WL5      FInsrt(0x0, fULCON_WL)  /* 5 bits */
#define  ULCON_WL6      FInsrt(0x1, fULCON_WL)  /* 6 bits */
#define  ULCON_WL7      FInsrt(0x2, fULCON_WL)  /* 7 bits */
#define  ULCON_WL8      FInsrt(0x3, fULCON_WL)  /* 8 bits */

#define ULCON_CFGMASK   (ULCON_IR | ULCON_PAR | ULCON_WL)

#define UCON_CLK_SEL      (1 << 10)     /* select clock for UART */
#define UCON_CLK_PCLK     (0 << 10)     /* PCLK for UART baud rate */
#define UCON_CLK_UCLK     (1 << 10)     /* UCLK for UART baud rate */
#define UCON_TX_INT_TYPE  (1 << 9)      /* TX Interrupt request type */
#define UCON_TX_INT_PLS   (0 << 9)      /* Pulse */
#define UCON_TX_INT_LVL   (1 << 9)      /* Level */
#define UCON_RX_INT_TYPE  (1 << 8)      /* RX Interrupt request type */
#define UCON_RX_INT_PLS   (0 << 8)      /* Pulse */
#define UCON_RX_INT_LVL   (1 << 8)      /* Level */
#define UCON_RX_TIMEOUT   (1 << 7)      /* RX timeout enable */
#define UCON_RX_ERR_INT   (1 << 6)      /* RX error status interrupt enable */
#define UCON_LOOPBACK     (1 << 5)      /* to enter the loop-back mode */
#define UCON_BRK_SIG      (1 << 4)      /* to send a break during 1 frame time */
#define fUCON_TX          Fld(2,2) /* function to write Tx data to the buffer */
#define  UCON_TX          FMsk(fUCON_TX)
#define  UCON_TX_DIS      FInsrt(0x0, fUCON_TX) /* Disable */
#define  UCON_TX_INT      FInsrt(0x1, fUCON_TX) /* Interrupt or polling */
#define  UCON_TX_DMA02    FInsrt(0x2, fUCON_TX) /* DMA0,2 for UART0 */
#define  UCON_TX_DMA13    FInsrt(0x3, fUCON_TX) /* DMA1,3 for UART1 */
#define fUCON_RX          Fld(2,0) /* function to read Rx data from buffer */
#define  UCON_RX          FMsk(fUCON_RX)
#define  UCON_RX_DIS      FInsrt(0x0, fUCON_RX) /* Disable */
#define  UCON_RX_INT      FInsrt(0x1, fUCON_RX) /* Interrupt or polling */
#define  UCON_RX_DMA02    FInsrt(0x2, fUCON_RX) /* DMA0,2 for UART0 */
#define  UCON_RX_DMA13    FInsrt(0x3, fUCON_RX) /* DMA1,3 for UART1 */

#define fUFCON_TX_TR      Fld(2,6)      /* trigger level of transmit FIFO */
#define  UFCON_TX_TR      FMsk(fUFCON_TX_TR)
#define  UFCON_TX_TR0     FInsrt(0x0, fUFCON_TX_TR)     /* Empty */
#define  UFCON_TX_TR16    FInsrt(0x1, fUFCON_TX_TR)     /* 16-byte */
#define  UFCON_TX_TR32    FInsrt(0x2, fUFCON_TX_TR)     /* 32-byte */
#define  UFCON_TX_TR48    FInsrt(0x3, fUFCON_TX_TR)     /* 48-byte */
#define fUFCON_RX_TR      Fld(2,4)      /* trigger level of receive FIFO */
#define  UFCON_RX_TR      FMsk(fUFCON_RX_TR)
#define  UFCON_RX_TR1     FInsrt(0x0, fUFCON_RX_TR)     /* 1-byte */
#define  UFCON_RX_TR8     FInsrt(0x1, fUFCON_RX_TR)     /* 8-byte */
#define  UFCON_RX_TR16    FInsrt(0x2, fUFCON_RX_TR)     /* 16-byte */
#define  UFCON_RX_TR32    FInsrt(0x3, fUFCON_RX_TR)     /* 32-byte */
#define UFCON_TX_CLR      (1 << 2)      /* auto-cleared after resetting FIFO */
#define UFCON_RX_CLR      (1 << 1)      /* auto-cleared after resetting FIFO */
#define UFCON_FIFO_EN     (1 << 0)      /* FIFO Enable */

#define UMCON_AFC         (1 << 4) /* Enable Auto Flow Control */
#define UMCON_SEND        (1 << 0) /* if no AFC, set nRTS 1:'L' 0:'H' level */

#define UTRSTAT_TR_EMP    (1 << 2)      /* 1: Transmitter buffer &
                                                shifter register empty */
#define UTRSTAT_TX_EMP    (1 << 1)      /* Transmit buffer reg. is empty */
#define UTRSTAT_RX_RDY    (1 << 0)      /* Receive buffer reg. has data */

#define UERSTAT_OVERRUN   (1 << 0)      /* Overrun Error */
#define UERSTAT_ERR_MASK  UERSTAT_OVERRUN

#define UFSTAT_TX_FULL    (1 << 14)     /* Transmit FIFO is full */
#define fUFSTAT_TX_CNT    Fld(6,8)      /* Number of data in Tx FIFO */
#define  UFSTAT_TX_CNT    FMsk(fUFSTAT_TX_CNT)
#define UFSTAT_RX_FULL    (1 << 6)      /* Receive FIFO is full */
#define fUFSTAT_RX_CNT    Fld(6,0)      /* Number of data in Rx FIFO */
#define  UFSTAT_RX_CNT    FMsk(fUFSTAT_RX_CNT)
#define UART1_TXFIFO_CNT()      FExtr(UFSTAT1, fUFSTAT_TX_CNT)
#define UART1_RXFIFO_CNT()      FExtr(UFSTAT1, fUFSTAT_RX_CNT)

#define UMSTAT_dCTS       (1 << 4)      /* delta CTS */
#define UMSTAT_CTS        (1 << 0)      /* CTS(Clear to Send) signal */

#define UTXH_DATA         0x000000FF    /* Transmit data for UARTn */
#define URXH_DATA         0x000000FF    /* Receive data for UARTn */
#define UBRDIVn           0x0000FFFF    /* Baud rate division value (> 0) */

/*
 * GPIO ( chapter 20 )
 */
#define GPIO_CONL_NUM        (2)
#define GPIO_CONM_NUM        (1)
#define GPIO_CONU_NUM        (0)
#define GPIO_CONL_BASE       (0<<3)
#define GPIO_CONM_BASE       (11<<3)
#define GPIO_CONU_BASE       (19<<3)
#define GPIO_CONL            (GPIO_CONL_NUM | GPIO_CONL_BASE)
#define GPIO_CONM            (GPIO_CONM_NUM | GPIO_CONM_BASE)
#define GPIO_CONU            (GPIO_CONU_NUM | GPIO_CONU_BASE)

#define GPCON(x)             __REG(0x44800000 + (x) * 0x4)
#define GPCONU               __REG(0x44800000)
#define GPCONM               __REG(0x44800004)
#define GPCONL               __REG(0x44800008)
#define GPDAT                __REG(0x4480000c)
#define GPUP                 __REG(0x44800010)
#define GPIO_OFS_SHIFT       0
#define GPIO_CON_SHIFT       8
#define GPIO_PULLUP_SHIFT    16
#define GPIO_MODE_SHIFT      24
#define GPIO_OFS_MASK        0x000000ff
#define GPIO_CON_MASK        0x0000ff00
#define GPIO_PULLUP_MASK     0x00ff0000
#define GPIO_MODE_MASK       0xff000000
#define GPIO_MODE_IN         (0 << GPIO_MODE_SHIFT)
#define GPIO_MODE_OUT        (1 << GPIO_MODE_SHIFT)
#define GPIO_MODE_ALT0       (2 << GPIO_MODE_SHIFT)
#define GPIO_MODE_ALT1       (3 << GPIO_MODE_SHIFT)
#define GPIO_PULLUP_EN       (0 << GPIO_PULLUP_SHIFT)
#define GPIO_PULLUP_DIS      (1 << GPIO_PULLUP_SHIFT)

#define MAKE_GPIO_NUM(c, o)  ((c << GPIO_CON_SHIFT) | (o << GPIO_OFS_SHIFT))

#define GRAB_MODE(x)    (((x) & GPIO_MODE_MASK) >> GPIO_MODE_SHIFT)
#define GRAB_PULLUP(x)  (((x) & GPIO_PULLUP_MASK) >> GPIO_PULLUP_SHIFT)
#define GRAB_OFS(x)     (((x) & GPIO_OFS_MASK) >> GPIO_OFS_SHIFT)
#define GRAB_CON_NUM(x) ((((x) & GPIO_CON_MASK) >> GPIO_CON_SHIFT) & 0x07)
#define GRAB_CON_OFS(x) (GRAB_OFS(x) - (((x) & GPIO_CON_MASK) >> (GPIO_CON_SHIFT+3)))

#define GPIO_0      MAKE_GPIO_NUM(GPIO_CONL, 0)
#define GPIO_1      MAKE_GPIO_NUM(GPIO_CONL, 1)
#define GPIO_2      MAKE_GPIO_NUM(GPIO_CONL, 2)
#define GPIO_3      MAKE_GPIO_NUM(GPIO_CONL, 3)
#define GPIO_4      MAKE_GPIO_NUM(GPIO_CONL, 4)
#define GPIO_5      MAKE_GPIO_NUM(GPIO_CONL, 5)
#define GPIO_6      MAKE_GPIO_NUM(GPIO_CONL, 6)
#define GPIO_7      MAKE_GPIO_NUM(GPIO_CONL, 7)
#define GPIO_8      MAKE_GPIO_NUM(GPIO_CONL, 8)
#define GPIO_9      MAKE_GPIO_NUM(GPIO_CONL, 9)
#define GPIO_10     MAKE_GPIO_NUM(GPIO_CONL, 10)
#define GPIO_11     MAKE_GPIO_NUM(GPIO_CONM, 11)
#define GPIO_12     MAKE_GPIO_NUM(GPIO_CONM, 12)
#define GPIO_13     MAKE_GPIO_NUM(GPIO_CONM, 13)
#define GPIO_14     MAKE_GPIO_NUM(GPIO_CONM, 14)
#define GPIO_15     MAKE_GPIO_NUM(GPIO_CONM, 15)
#define GPIO_16     MAKE_GPIO_NUM(GPIO_CONM, 16)
#define GPIO_17     MAKE_GPIO_NUM(GPIO_CONM, 17)
#define GPIO_18     MAKE_GPIO_NUM(GPIO_CONM, 18)
#define GPIO_19     MAKE_GPIO_NUM(GPIO_CONU, 19)
#define GPIO_20     MAKE_GPIO_NUM(GPIO_CONU, 20)
#define GPIO_21     MAKE_GPIO_NUM(GPIO_CONU, 21)
#define GPIO_22     MAKE_GPIO_NUM(GPIO_CONU, 22)
#define GPIO_23     MAKE_GPIO_NUM(GPIO_CONU, 23)
#define GPIO_24     MAKE_GPIO_NUM(GPIO_CONU, 24)
#define GPIO_25     MAKE_GPIO_NUM(GPIO_CONU, 25)
#define GPIO_26     MAKE_GPIO_NUM(GPIO_CONU, 26)
#define GPIO_27     MAKE_GPIO_NUM(GPIO_CONU, 27)
#define GPIO_28     MAKE_GPIO_NUM(GPIO_CONU, 28)
#define GPIO_29     MAKE_GPIO_NUM(GPIO_CONU, 29)
#define GPIO_30     MAKE_GPIO_NUM(GPIO_CONU, 30)
#define GPIO_31     MAKE_GPIO_NUM(GPIO_CONU, 31)
/* major alt. */
#define GPIO_MODE_EINT            GPIO_MODE_ALT0
#define GPIO_MODE_RTC_ALARMINT    GPIO_MODE_ALT1
#define GPIO_MODE_IrDA            GPIO_MODE_ALT1
#define GPIO_MODE_PWM             GPIO_MODE_ALT0
#define GPIO_MODE_SPI             GPIO_MODE_ALT1
#define GPIO_MODE_EXT_DMA         GPIO_MODE_ALT0
#define GPIO_MODE_EXT_KEYP        GPIO_MODE_ALT1
#define GPIO_MODE_UART            GPIO_MODE_ALT0
/* canonical */
#define GPIO_MODE_IrDA_SDBW       GPIO_MODE_IrDA
#define GPIO_MODE_IrDA_TXD        GPIO_MODE_IrDA
#define GPIO_MODE_IrDA_RXD        GPIO_MODE_IrDA
#define GPIO_MODE_PWM_ECLK        GPIO_MODE_PWM
#define GPIO_MODE_PWM_TOUT        GPIO_MODE_PWM
#define GPIO_MODE_PWM_TOUT0       GPIO_MODE_PWM
#define GPIO_MODE_PWM_TOUT1       GPIO_MODE_PWM
#define GPIO_MODE_PWM_TOUT2       GPIO_MODE_PWM
#define GPIO_MODE_PWM_TOUT3       GPIO_MODE_PWM
#define GPIO_MODE_SPI_MODI        GPIO_MODE_SPI
#define GPIO_MODE_SPI_MISO        GPIO_MODE_SPI
#define GPIO_MODE_DMAREQ0         GPIO_MODE_EXT_DMA
#define GPIO_MODE_DMAREQ1         GPIO_MODE_EXT_DMA
#define GPIO_MODE_DMAACK0         GPIO_MODE_EXT_DMA
#define GPIO_MODE_DMAACK1         GPIO_MODE_EXT_DMA
#define GPIO_MODE_KEYP_ROW0       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_ROW1       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_ROW2       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_ROW3       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_ROW4       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_COL0       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_COL1       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_COL2       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_COL3       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_KEYP_COL4       GPIO_MODE_EXT_KEYP
#define GPIO_MODE_uCTSn1          GPIO_MODE_UART
#define GPIO_MODE_uRTSn1          GPIO_MODE_UART
#define GPIO_MODE_uTXD1           GPIO_MODE_UART
#define GPIO_MODE_uRXD1           GPIO_MODE_UART

#define ENPU          __REG(0x44800040) /* normal port pullup in sleep */
#define ENPU_EN       __REG(0x44800064) /* ENPU enable */
#define GPDAT_S       __REG(0x44800048) /* GPDAT in sleep */
#define GPDAT_SEN     __REG(0x4480004c) /* GPDAT_S enable */
#define GPUP_S        __REG(0x44800050) /* GPUP in sleep */
#define DATR0_S       __REG(0x44800054) /* data in sleep */
#define DATR1_S       __REG(0x44800058) /* data in sleep */
#define OEN0_S        __REG(0x4480005c) /* output in sleep */
#define OEN1_S        __REG(0x44800060) /* output in sleep */

#define ALIVECON      __REG(0x44800044) /* clock for alive mode in sleep */
#define RSTCNT        __REG(0x44800068) /* reset count for power settle-down */

#define set_gpio_ctrl(x) \
        ({ GPCON(GRAB_CON_NUM((x))) &= ~(0x3 << (GRAB_CON_OFS((x))*2)); \
           GPCON(GRAB_CON_NUM(x)) |= (GRAB_MODE(x) << (GRAB_CON_OFS((x))*2)); \
           GPUP &= ~(1 << GRAB_OFS((x))); \
           GPUP |= (GRAB_PULLUP((x)) << GRAB_OFS((x))); })
#define read_gpio_bit(x)        ((GPDAT & (1<<GRAB_OFS((x)))) >> GRAB_OFS((x)))
#define write_gpio_bit(x, v) \
        ({ GPDAT &= ~(0x1 << GRAB_OFS((x))); \
           GPDAT |= ((v) << GRAB_OFS((x))); })

/*
 * USB Host ( chapter 17 )
 *  - OHCI 1.0
 *  - USB 1.1
 */
#define USB_OHCI_BASE      __REG(0x41000000)

/*
 * SROM Bank (chapter 2) for CS8900A
 */
#define SROM_BW  __REG(0x40c20000)
#define SROM_BC1 __REG(0x40c20008)


/*
 * Interrupt ( chpater 6 )
 */
#define SRCPND                  __REG(0x40200000)
#define INTMOD                  __REG(0x40200004)
#define INTMSK                  __REG(0x40200008)
#define PRIORITY                __REG(0x4020000c)
#define INTPND                  __REG(0x40200010)
#define INTOFFSET               __REG(0x40200014)
#define SUBSRCPND               __REG(0x40200018)
#define INTSUBMSK               __REG(0x4020001c)

#define EINTMASK                __REG(0x44800034)
#define EINTPEND                __REG(0x44800038)

#define EINTCR0                 __REG(0x44800018)
#define EINTCR1                 __REG(0x4480001c)
#define EINTCR2                 __REG(0x44800020)


/*
 * BUS Martrix
 */

#define PRIORITY0               __REG(0x40ce0000)
#define PRIORITY1               __REG(0x40ce0004)
#define PRIORITY_S_FIX           0x0
#define PRIORITY_I_FIX           0x2

/*
 * Watchdog timer ( chapter 8 )
 */
#define WTCON                   __REG(0x44100000)
#define WTDAT                   __REG(0x44100004)
#define WTCNT                   __REG(0x44100008)

/*
 * Real time clock ( chapter 10 )
 *
 * Note: All RTC registers have to be accessed by byte unit
 *       using STRB and LDRB instructions or char type pointer (page on 10-4)
 */
#define RTCCON                  __REG(0x44200040)
#define TICNT                   __REG(0x44200044)
#define RTCALM                  __REG(0x44200050)
#define ALMSEC                  __REG(0x44200054)
#define ALMMIN                  __REG(0x44200058)
#define ALMHOUR                 __REG(0x4420005c)
#define ALMDATE                 __REG(0x44200060)
#define ALMMON                  __REG(0x44200064)
#define ALMYEAR                 __REG(0x44200068)
#define RTCRST                  __REG(0x4420006c)
#define BCDSEC                  __REG(0x44200070)
#define BCDMIN                  __REG(0x44200074)
#define BCDHOUR                 __REG(0x44200078)
#define BCDDATE                 __REG(0x4420007c)
#define BCDDAY                  __REG(0x44200080)
#define BCDMON                  __REG(0x44200084)
#define BCDYEAR                 __REG(0x44200088)

/* Fields */
#define fRTC_SEC                Fld(7,0)
#define fRTC_MIN                Fld(7,0)
#define fRTC_HOUR               Fld(6,0)
#define fRTC_DATE               Fld(6,0)
#define fRTC_DAY                Fld(2,0)
#define fRTC_MON                Fld(5,0)
#define fRTC_YEAR               Fld(8,0)
/* Mask */
#define Msk_RTCSEC              FMsk(fRTC_SEC)
#define Msk_RTCMIN              FMsk(fRTC_MIN)
#define Msk_RTCHOUR             FMsk(fRTC_HOUR)
#define Msk_RTCDAY              FMsk(fRTC_DAY)
#define Msk_RTCDATE             FMsk(fRTC_DATE)
#define Msk_RTCMON              FMsk(fRTC_MON)
#define Msk_RTCYEAR             FMsk(fRTC_YEAR)
/* bits */
#define RTCCON_EN               (1 << 0) /* RTC Control Enable */
#define RTCCON_CLKSEL           (1 << 1) /* BCD clock as XTAL 1/2^25 clock */
#define RTCCON_CNTSEL           (1 << 2) /* 0: Merge BCD counters */
#define RTCCON_CLKRST           (1 << 3) /* RTC clock count reset */

/* Tick Time count register */
#define RTCALM_GLOBAL           (1 << 6) /* Global alarm enable */
#define RTCALM_YEAR                     (1 << 5) /* Year alarm enable */
#define RTCALM_MON                      (1 << 4) /* Month alarm enable */
#define RTCALM_DAY                      (1 << 3) /* Day alarm enable */
#define RTCALM_HOUR                     (1 << 2) /* Hour alarm enable */
#define RTCALM_MIN                      (1 << 1) /* Minute alarm enable */
#define RTCALM_SEC                      (1 << 0) /* Second alarm enable */
#define RTCALM_EN                       (RTCALM_GLOBAL | RTCALM_YEAR | RTCALM_MON |\
                                                        RTCALM_DAY | RTCALM_HOUR | RTCALM_MIN |\
                                                        RTCALM_SEC)
#define RTCALM_DIS                      (~RTCALM_EN)

/* ADC and Touch Screen Interface */
#define ADC_CTL_BASE    0x45800000
#define bADC_CTL(Nb)    __REG(ADC_CTL_BASE + (Nb))
// Registers
#define ADCCON          bADC_CTL(0x00) // R/W, ADC control register
#define ADCTSC          bADC_CTL(0x04) // R/W, ADC touch screen ctl reg
#define ADCDLY          bADC_CTL(0x08) // R/W, ADC start or interval delay reg
#define ADCDAX          bADC_CTL(0x0c) // R  , ADC conversion data reg
#define ADCDAY          bADC_CTL(0x10) // R  , ADC conversion data reg
// ADCCON
#define fECFLG          Fld(1, 15)      // R , End of conversion flag
#define ECFLG_VAL       FExtr(ADCCON, fECFLG)
#define CONV_PROCESS    0
#define CONV_END        1

#define fPRSCEN         Fld(1, 14)
#define PRSCEN_DIS      FInsrt(0, fPRSCEN)
#define PRSCEN_EN       FInsrt(1, fPRSCEN)

#define fPRSCVL         Fld(8, 6)
#define PRSCVL(x)       FInsrt(x, fPRSCVL)

#define fSEL_MUX        Fld(3, 3)
#define ADC_IN_SEL(x)   FInsrt(x, fSEL_MUX)
#define ADC_IN0         0
#define ADC_IN1         1
#define ADC_IN2         2
#define ADC_IN3         3
#define ADC_IN4         4
#define ADC_IN5         5
#define ADC_IN6         6
#define ADC_IN7         7

#define fSTDBM          Fld(1, 2) // Standby mode select
#define STDBM_NORMAL    FInsrt(0, fSTDBM)
#define STDBM_STANDBY   FInsrt(1, fSTDBM)

#define fREAD_START     Fld(1, 1)
#define READ_START_DIS  FInsrt(0, fREAD_START)
#define READ_START_EN   FInsrt(1, fREAD_START)

#define fENABLE_START   Fld(1, 0)
#define ENABLE_START_NOOP    FInsrt(0, fENABLE_START)
#define ENABLE_START_START   FInsrt(1, fENABLE_START)

// ADCTSC
#define fYM_SEN         Fld(1, 7)
#define YM_HIZ          FInsrt(0, fYM_SEN)
#define YM_GND          FInsrt(1, fYM_SEN)
#define fYP_SEN         Fld(1, 6)
#define YP_EXTVLT       FInsrt(0, fYP_SEN)
#define YP_AIN5         FInsrt(1, fYP_SEN)
#define fXM_SEN         Fld(1, 5)
#define XM_HIZ          FInsrt(0, fXM_SEN)
#define XM_GND          FInsrt(1, fXM_SEN)
#define fXP_SEN         Fld(1, 4)
#define XP_EXTVLT       FInsrt(0, fXP_SEN)
#define XP_AIN7         FInsrt(1, fXP_SEN)
#define fPULL_UP        Fld(1, 3)
#define XP_PULL_UP_EN   FInsrt(0, fPULL_UP)
#define XP_PULL_UP_DIS  FInsrt(1, fPULL_UP)
#define fAUTO_PST       Fld(1, 2)
#define AUTO_PST_NORMAL FInsrt(0, fAUTO_PST)
#define AUTO_PST_AUTO   FInsrt(1, fAUTO_PST)
#define fXY_PST         Fld(2, 0)
#define XY_PST_NOOP     FInsrt(0, fXY_PST)
#define XY_PST_X_POS    FInsrt(1, fXY_PST)
#define XY_PST_Y_POS    FInsrt(2, fXY_PST)
#define XY_PST_WAIT_INT FInsrt(3, fXY_PST)

// ADC Conversion DATA Field, commons
#define fUPDOWN         Fld(1, 15)
#define fDAT_AUTO_PST   Fld(1, 14)
#define fDAT_XY_PST     Fld(2, 12)
#define fPST_DATA       Fld(10, 0) // PST : position

#define PST_DAT_MSK     0x3FF
#define PST_DAT_VAL(x)  (FExtr(x, fPST_DATA) & PST_DAT_MSK)
// ADCDAX
#define XPDATA          PST_DAT_VAL(ADCDAX)
// ADCDAY
#define YPDATA          PST_DAT_VAL(ADCDAY)

/*
 * IIS Bus Interface  ( chapter 14 )
 */
#define IISCON          __REG(0x44700000)
#define IISMOD          __REG(0x44700004)
#define IISPSR          __REG(0x44700008)
#define IISFIFOC        __REG(0x4470000c)
#define IISFIFOE        __REG(0x44700010)

#define IISCON_CH_RIGHT (1 << 8)        /* Right channel */
#define IISCON_CH_LEFT  (0 << 8)        /* Left channel */
#define IISCON_TX_RDY   (1 << 7)        /* Transmit FIFO is ready(not empty) */
#define IISCON_RX_RDY   (1 << 6)        /* Receive FIFO is ready (not full) */
#define IISCON_TX_DMA   (1 << 5)        /* Transmit DMA service reqeust */
#define IISCON_RX_DMA   (1 << 4)        /* Receive DMA service reqeust */
#define IISCON_TX_IDLE  (1 << 3)        /* Transmit Channel idle */
#define IISCON_RX_IDLE  (1 << 2)        /* Receive Channel idle */
#define IISCON_PRESCALE (1 << 1)        /* IIS Prescaler Enable */
#define IISCON_EN       (1 << 0)        /* IIS enable(start) */

#define IISMOD_SEL_MA   (0 << 8)        /* Master mode
                                                                                      (IISLRCK, IISCLK are Output) */
#define IISMOD_SEL_SL   (1 << 8)        /* Slave mode
                                                                                      (IISLRCK, IISCLK are Input) */
#define fIISMOD_SEL_TR  Fld(2, 6)       /* Transmit/Receive mode */
#define IISMOD_SEL_TR   FMsk(fIISMOD_SEL_TR)
#define IISMOD_SEL_NO   FInsrt(0x0, fIISMOD_SEL_TR)     /* No Transfer */
#define IISMOD_SEL_RX   FInsrt(0x1, fIISMOD_SEL_TR)     /* Receive */
#define IISMOD_SEL_TX   FInsrt(0x2, fIISMOD_SEL_TR)     /* Transmit */
#define IISMOD_SEL_BOTH FInsrt(0x3, fIISMOD_SEL_TR)     /* Tx & Rx */
#define IISMOD_CH_RIGHT (0 << 5)        /* high for right channel */
#define IISMOD_CH_LEFT  (1 << 5)        /* high for left channel */
#define IISMOD_FMT_IIS  (0 << 4)        /* IIS-compatible format */
#define IISMOD_FMT_MSB  (1 << 4)        /* MSB(left)-justified format */
#define IISMOD_BIT_8    (0 << 3)        /* Serial data bit/channel is 8 bit*/
#define IISMOD_BIT_16   (1 << 3)        /* Serial data bit/channel is 16 bit*/
#define IISMOD_FREQ_256 (0 << 2)        /* Master clock freq = 256 fs */
#define IISMOD_FREQ_384 (1 << 2)        /* Master clock freq = 384 fs */
#define fIISMOD_SFREQ   Fld(2, 0)       /* Serial bit clock frequency */
#define IISMOD_SFREQ    FMsk(fIISMOD_SFREQ)     /* fs = sampling frequency */
#define IISMOD_SFREQ_16 FInsrt(0x0, fIISMOD_SFREQ)      /* 16 fs */
#define IISMOD_SFREQ_32 FInsrt(0x1, fIISMOD_SFREQ)      /* 32 fs */
#define IISMOD_SFREQ_48 FInsrt(0x2, fIISMOD_SFREQ)      /* 48 fs */

#define fIISPSR_A       Fld(5, 5)       /* Prescaler Control A */
#define IISPSR_A(x)     FInsrt((x), fIISPSR_A)
#define fIISPSR_B       Fld(5, 0)       /* Prescaler Control B */
#define IISPSR_B(x)     FInsrt((x), fIISPSR_B)

#define IISFCON_TX_NORM (0 << 15)       /* Transmit FIFO access mode: normal */
#define IISFCON_TX_DMA  (1 << 15)       /* Transmit FIFO access mode: DMA */
#define IISFCON_RX_NORM (0 << 14)       /* Receive FIFO access mode: normal */
#define IISFCON_RX_DMA  (1 << 14)       /* Receive FIFO access mode: DMA */
#define IISFCON_TX_EN   (1 << 13)        /* Transmit FIFO enable */
#define IISFCON_RX_EN   (1 << 12)        /* Recevice FIFO enable */
#define fIISFCON_TX_CNT Fld(6, 6)       /* Tx FIFO data count (Read-Only) */
#define IISFCON_TX_CNT  FMsk(fIISFCON_TX_CNT)
#define fIISFCON_RX_CNT Fld(6, 0)       /* Rx FIFO data count (Read-Only) */
#define IISFCON_RX_CNT  FMsk(fIISFCON_RX_CNT)

/*
 * DMA controller ( chapter 9 )
 */
#define DMA_CTL_BASE    0x40400000
#define bDMA_CTL(Nb,x)  __REG(DMA_CTL_BASE + (0x100000*Nb) + (x))
/* DMA channel 0 */
#define DISRC0                  bDMA_CTL(0, 0x00)
#define DISRCC0                 bDMA_CTL(0, 0x04)
#define DIDST0                  bDMA_CTL(0, 0x08)
#define DIDSTC0                 bDMA_CTL(0, 0x0c)
#define DCON0                   bDMA_CTL(0, 0x10)
#define DSTAT0                  bDMA_CTL(0, 0x14)
#define DCSRC0                  bDMA_CTL(0, 0x18)
#define DCDST0                  bDMA_CTL(0, 0x1c)
#define DMTRIG0                 bDMA_CTL(0, 0x20)
/* DMA channel 1 */
#define DISRC1                  bDMA_CTL(1, 0x00)
#define DISRCC1                 bDMA_CTL(1, 0x04)
#define DIDST1                  bDMA_CTL(1, 0x08)
#define DIDSTC1                 bDMA_CTL(1, 0x0c)
#define DCON1                   bDMA_CTL(1, 0x10)
#define DSTAT1                  bDMA_CTL(1, 0x14)
#define DCSRC1                  bDMA_CTL(1, 0x18)
#define DCDST1                  bDMA_CTL(1, 0x1c)
#define DMTRIG1                 bDMA_CTL(1, 0x20)
/* DMA channel 2 */
#define DISRC2                  bDMA_CTL(2, 0x00)
#define DISRCC2                 bDMA_CTL(2, 0x04)
#define DIDST2                  bDMA_CTL(2, 0x08)
#define DIDSTC2                 bDMA_CTL(2, 0x0c)
#define DCON2                   bDMA_CTL(2, 0x10)
#define DSTAT2                  bDMA_CTL(2, 0x14)
#define DCSRC2                  bDMA_CTL(2, 0x18)
#define DCDST2                  bDMA_CTL(2, 0x1c)
#define DMTRIG2                 bDMA_CTL(2, 0x20)
/* DMA channel 3 */
#define DISRC3                  bDMA_CTL(3, 0x00)
#define DISRCC3                 bDMA_CTL(3, 0x04)
#define DIDST3                  bDMA_CTL(3, 0x08)
#define DIDSTC3                 bDMA_CTL(3, 0x0c)
#define DCON3                   bDMA_CTL(3, 0x10)
#define DSTAT3                  bDMA_CTL(3, 0x14)
#define DCSRC3                  bDMA_CTL(3, 0x18)
#define DCDST3                  bDMA_CTL(3, 0x1c)
#define DMTRIG3                 bDMA_CTL(3, 0x20)

/* DISRC, DIDST Control registers */
#define fDMA_BASE_ADDR          Fld(30, 0)      /* base address of src/dst data */
#define DMA_BASE_ADDR(x)        FInsrt(x, fDMA_BASE_ADDR)
#define LOC_SRC                 (1 << 1)        /* select the location of source */
#define ON_AHB                  (LOC_SRC*0)
#define ON_APB                  (LOC_SRC*1)
#define ADDR_MODE               (1 << 0)       /* select the address increment */
#define ADDR_INC                (ADDR_MODE*0)
#define ADDR_FIX                (ADDR_MODE*1)

/* DCON Definitions */
#define DCON_MODE               (1 << 31)       /* 0: demand, 1: handshake */
#define DEMAND_MODE             (DCON_MODE*0)
#define HS_MODE                 (DCON_MODE*1)
#define DCON_SYNC               (1 << 30)       /* sync to 0:PCLK, 1:HCLK */
#define SYNC_PCLK               (DCON_SYNC*0)
#define SYNC_HCLK               (DCON_SYNC*1)
#define DCON_INT                (1 << 29)
#define POLLING_MODE            (DCON_INT*0)
#define INT_MODE                (DCON_INT*1)
#define DCON_TSZ                (1 << 28)       /* tx size 0: a unit, 1: burst */
#define TSZ_UNIT                (DCON_TSZ*0)
#define TSZ_BURST               (DCON_TSZ*1)
#define DCON_SERVMODE           (1 << 27)       /* 0: single, 1: whole service */
#define SINGLE_SERVICE          (DCON_SERVMODE*0)
#define WHOLE_SERVICE           (DCON_SERVMODE*1)
#define fDCON_HWSRC             Fld(3, 24)      /* select request source */
#define CH0_nXDREQ0             0
#define CH0_UART0               1
#define CH0_I2SSDI              2
#define CH0_TIMER               3
#define CH0_USBEP1              4
#define CH0_AC97_PCMOUT         5
#define CH0_MSTICK              6
#define CH0_IRDA                7
#define CH1_nXDREQ1             0
#define CH1_UART1               1
#define CH1_I2SSDO              2
#define CH1_SPI                 3
#define CH1_USBEP2              4
#define CH1_AC97_PCMIN          5
#define CH1_AC97_PCMOUT         6
#define CH1_IRDA                7
#define CH2_UART0               0
#define CH2_I2SSDO              1
#define CH2_SDMMC               2
#define CH2_TIMER               3
#define CH2_USBEP3              4
#define CH2_AC97_MICIN          5
#define CH2_AC97_PCMIN          6
#define CH3_UART1               0
#define CH3_SDMMC               1
#define CH3_SPI                 2
#define CH3_TIMER               3
#define CH3_USBEP4              4
#define CH3_MSTICK              5
#define CH3_AC97_MICIN          6
#define HWSRC(x)                FInsrt(x, fDCON_HWSRC)
#define DCON_SWHW_SEL           (1 << 23)       /* DMA src 0: s/w 1: h/w */
#define DMA_SRC_SW              (DCON_SWHW_SEL*0)
#define DMA_SRC_HW              (DCON_SWHW_SEL*1)
#define DCON_RELOAD             (1 << 22)       /* set auto-reload */
#define SET_ATRELOAD            (DCON_RELOAD*0)
#define CLR_ATRELOAD            (DCON_RELOAD*1)
#define fDCON_DSZ               Fld(2, 20)
#define DSZ_BYTE                0
#define DSZ_HALFWORD            1
#define DSZ_WORD                2
#define DSZ(x)                  FInsrt(x, fDCON_DSZ)
#define readDSZ(x)              FExtr(x, fDCON_DSZ)
#define fDCON_TC                Fld(20,0)
#define TX_CNT(x)               FInsrt(x, fDCON_TC)
/* STATUS Register Definitions  */
#define fDSTAT_ST               Fld(2,20)       /* Status of DMA Controller */
#define fDSTAT_TC               Fld(20,0)       /* Current value of transfer count */
#define DMA_STATUS(chan)        FExtr((DSTAT0 + (0x20 * chan)), fDSTAT_ST)
#define DMA_BUSY                (1 << 0)
#define DMA_READY               (0 << 0)
#define DMA_CURR_TC(chan)       FExtr((DSTAT0 + (0x20 * chan)), fDSTAT_TC)
/* DMA Trigger Register Definitions */
#define DMASKTRIG_STOP          (1 << 2)        /* Stop the DMA operation */
#define DMA_STOP                (DMASKTRIG_STOP*1)
#define DMA_STOP_CLR            (DMASKTRIG_STOP*0)
#define DMASKTRIG_ONOFF         (1 << 1)        /* DMA channel on/off */
#define CHANNEL_ON              (DMASKTRIG_ONOFF*1)
#define CHANNEL_OFF             (DMASKTRIG_ONOFF*0)
#define DMASKTRIG_SW            (1 << 0)        /* Trigger DMA ch. in S/W req. mode */
#define DMA_SW_REQ_CLR          (DMASKTRIG_SW*0)
#define DMA_SW_REQ              (DMASKTRIG_SW*1)

/*
 * KeyIF - keypad interface
 * chapter 28
 */
#define KEYDAT    __REG(0x44900000)
#define KEYINTC   __REG(0x44900004)
#define KEYFLT0   __REG(0x44900008)
#define KEYFLT1   __REG(0x4490000C)
#define  fKEYDAT_KEYS       Fld(5,0) /* RO : intput decoding data */
#define KEYDAT_KEYS         FExtr(KEYDAT, fKEYDAT_KEYS)
#define KEYDAT_KEYVAL       (1<<5) /* RO : 0=valid, 1=invalid */
#define KEYDAT_KEYCLR       (1<<6) /* WO : 0=noaction, 1=clear */
#define KEYDAT_KEYEN        (1<<7) /* RW : 0=disable, 1=enable */
#define  fKEYINTLV          Fld(3,0)
#define  KEYINTLV_LL        0 /* low level */
#define  KEYINTLV_HL        1 /* high level */
#define  KEYINTLV_RE        2 /* rising edge */
#define  KEYINTLV_FE        4 /* falling edge */
#define  KEYINTLV_BE        6 /* both edges */
#define KEYINTLV(x)         FInsrt((x), fKEYINTLV)
#define KEYINTEN            (1<<3) /* interrupt enable */
#define  fKEYFLT_SELCLK     Fld(1,0)
#define  SELCLK_RTC         0
#define  SELCLK_GCLK        1
#define KEYFLT_SELCLK(x)    FInsrt((x),fKEYFLT_SELCLK)
#define KEYFLT_FILEN        (1<<1)
#define  fKEYFLT_WIDTH Fld( 14,0)
#define KEYFLT_WIDTH(x)     FInsrt((x), fKEYFLT_WIDTH)
#define  KEYP_STAT                      (((GPDAT & 0x00FF0000) >> 16 ) & 0x7D )

/*
 * Video Post Processor ?
 *
 * chapter 26.
 */
#define VP_MODE       __REG(0x4a100000) /* RW */
#define VP_RATIO_Y    __REG(0x4a100004) /* RW */
#define VP_RATIO_CB   __REG(0x4a100008) /* RW */
#define VP_RATIO_CR   __REG(0x4a10000c) /* RW */
#define VP_SRC_WIDTH  __REG(0x4a100010) /* RW */
#define VP_SRC_HEIGHT __REG(0x4a100014) /* RW */
#define VP_DST_WIDTH  __REG(0x4a100018) /* RW */
#define VP_DST_HEIGHT __REG(0x4a10001c) /* RW */
#define VP_START_Y1   __REG(0x4a100020) /* RW */
#define VP_START_Y2   __REG(0x4a100024) /* RW */
#define VP_START_Y3   __REG(0x4a100028) /* RW */
#define VP_START_Y4   __REG(0x4a10002c) /* RW */
#define VP_START_CB1  __REG(0x4a100030) /* RW */
#define VP_START_CB2  __REG(0x4a100034) /* RW */
#define VP_START_CB3  __REG(0x4a100038) /* RW */
#define VP_START_CB4  __REG(0x4a10003c) /* RW */
#define VP_START_CR1  __REG(0x4a100040) /* RW */
#define VP_START_CR2  __REG(0x4a100044) /* RW */
#define VP_START_CR3  __REG(0x4a100048) /* RW */
#define VP_START_CR4  __REG(0x4a10004c) /* RW */
#define VP_START_RGB1 __REG(0x4a100050) /* RW */
#define VP_START_RGB2 __REG(0x4a100054) /* RW */
#define VP_START_RGB3 __REG(0x4a100058) /* RW */
#define VP_START_RGB4 __REG(0x4a10005c) /* RW */
#define VP_END_Y1     __REG(0x4a100060) /* RW */
#define VP_END_Y2     __REG(0x4a100064) /* RW */
#define VP_END_Y3     __REG(0x4a100068) /* RW */
#define VP_END_Y4     __REG(0x4a10006c) /* RW */
#define VP_END_CB1    __REG(0x4a100070) /* RW */
#define VP_END_CB2    __REG(0x4a100074) /* RW */
#define VP_END_CB3    __REG(0x4a100078) /* RW */
#define VP_END_CB4    __REG(0x4a10007c) /* RW */
#define VP_END_CR1    __REG(0x4a100080) /* RW */
#define VP_END_CR2    __REG(0x4a100084) /* RW */
#define VP_END_CR3    __REG(0x4a100088) /* RW */
#define VP_END_CR4    __REG(0x4a10008c) /* RW */
#define VP_END_RGB1   __REG(0x4a100090) /* RW */
#define VP_END_RGB2   __REG(0x4a100094) /* RW */
#define VP_END_RGB3   __REG(0x4a100098) /* RW */
#define VP_END_RGB4   __REG(0x4a10009c) /* RW */
#define VP_BYPASS     __REG(0x4a1000f0) /* RW */
#define VP_OFS_Y      __REG(0x4a1000f4) /* RW */
#define VP_OFS_CB     __REG(0x4a1000f8) /* RW */
#define VP_OFS_CR     __REG(0x4a1000fc) /* RW */
#define VP_OFS_RGB    __REG(0x4a100100) /* RW */

#define VP_STY(__x)   __REG(0x4a100020 + 4*(__x))
#define VP_STCB(__x)  __REG(0x4a100030 + 4*(__x))
#define VP_STCR(__x)  __REG(0x4a100040 + 4*(__x))
#define VP_STRGB(__x) __REG(0x4a100050 + 4*(__x))
#define VP_EDY(__x)   __REG(0x4a100060 + 4*(__x))
#define VP_EDCB(__x)  __REG(0x4a100070 + 4*(__x))
#define VP_EDCR(__x)  __REG(0x4a100080 + 4*(__x))
#define VP_EDRGB(__x) __REG(0x4a100090 + 4*(__x))

#define  fVP_MODE_FRMCNT  Fld(2,10)
#define VP_MODE_FRMCNT(x) FExtr((x), fVP_MODE_FRMCNT)
#define VP_MODE_BYPASSFC  (1<<9)
#define VP_MODE_BYPASSCSC (1<<8)
#define VP_MODE_INT       (1<<7)
#define VP_MODE_INTPND    (1<<6)
#define VP_MODE_EN        (1<<5)
#define VP_MODE_ORGB24    (1<<4) /* output : 0=RGB16(565) , 1=RGB24 */
#define VP_MODE_IFMT      (1<<3) /* input  : 0=YUV , 1=RGB */
#define VP_MODE_INTLV     (1<<2)
#define VP_MODE_IRGB24    (1<<1) /* input  : 0=RGB16(565) , 1=RGB24 */
#define VP_MODE_IYUV      (1<<0) /* if (VP_MODE_IFMT==0 && VP_MODE_INTLV==1)
                                       0=YUYV , 1=UYVY */

#define  fVP_RATIO_V      Fld(16,16)
#define  fVP_RATIO_H      Fld(16,0)
#define VP_RATIO_V(x)     FInsrt((x), fVP_RATIO_V)
#define VP_RATIO_H(x)     FInsrt((x), fVP_RATIO_H)

#define  fVP_IMG_SIZE_Y   Fld(10,20)
#define  fVP_IMG_SIZE_CB  Fld(10,10)
#define  fVP_IMG_SIZE_CR  Fld(10,0)
#define VP_IMG_SIZE_Y(x)  FInsrt((x), fVP_IMG_SIZE_Y)
#define VP_IMG_SIZE_CB(x) FInsrt((x), fVP_IMG_SIZE_CB)
#define VP_IMG_SIZE_CR(x) FInsrt((x), fVP_IMG_SIZE_CR)
#define VP_IMG_SIZE_R(x)  FInsrt((x), fVP_IMG_SIZE_Y)
#define VP_IMG_SIZE_G(x)  FInsrt((x), fVP_IMG_SIZE_CB)
#define VP_IMG_SIZE_B(x)  FInsrt((x), fVP_IMG_SIZE_CR)
#define VP_SIZE_XX(__x)   ((__x)-1)

#define VP_BYPASS_EN      (1<<24)
#define  fVP_BYPASS_LOW   Fld(12,12)
#define  fVP_BYPASS_HIGH  Fld(12,0)
#define VP_BYPASS_LOW(x)  FInsrt((x), fVP_BYPASS_LOW)
#define VP_BYPASS_HIGH(x) FInsrt((x), fVP_BYPASS_HIGH)


/*
 * IrDA Controller (Chapter 12)
 */
#define IRDACNT         __REG(0x41800000)       /* Control */
#define IRDAMDR         __REG(0x41800004)       /* Mode definition */
#define IRDACNF         __REG(0x41800008)       /* IRQ//DMA configuration */
#define IRDAIER         __REG(0x4180000c)       /* IRQ enable */
#define IRDAIIR         __REG(0x41800010)       /* IRQ indentification */
#define IRDALSR         __REG(0x41800014)       /* Line status */
#define IRDAFCR         __REG(0x41800018)       /* FIFO control */
#define IRDAPRL         __REG(0x4180001c)       /* Preamble length */
#define IRDARBR         __REG(0x41800020)       /* Tx/Rx Buffer */
#define IRDATXNO        __REG(0x41800024)       /* Total number of data bytes remained in Tx FIFO */
#define IRDARXNO        __REG(0x41800028)       /* Total number of data remained in Rx FIFO (in bytes) */
#define IRDATXFLL       __REG(0x4180002c)       /* Tx frame length (Low) */
#define IRDATXFLH       __REG(0x41800030)       /* Tx frame length (High) */
#define IRDARXFLL       __REG(0x41800034)       /* Rx frame length (Low) */
#define IRDARXFLH       __REG(0x41800038)       /* Rx frame length (High */
#define IRDATIME        __REG(0x4180003c)       /* Timing control */

/*
 * SPI Interface
 */
#define SPCON0          __REG(0x44500000)
#define SPSTA0          __REG(0x44500004)
#define SPPIN0          __REG(0x44500008)
#define SPPRE0          __REG(0x4450000C)
#define SPTDAT0         __REG(0x44500010)
#define SPRDAT0         __REG(0x44500014)
#define SPCON1          __REG(0x44500020)
#define SPSTA1          __REG(0x44500024)
#define SPPIN1          __REG(0x44500028)
#define SPPRE1          __REG(0x4450002C)
#define SPTDAT1         __REG(0x44500030)
#define SPRDAT1         __REG(0x44500034)

#define fSPCON_SMOD     Fld(2,5)                /* SPI mode select */
#define SPCON_SMOD      FMsk(fSPCON_SMOD)
#define SPCON_SMOD_POLL FInsrt(0x0, fSPCON_SMOD)
#define SPCON_SMOD_INT  FInsrt(0x1, fSPCON_SMOD)
#define SPCON_SMOD_DMA  FInsrt(0x2, fSPCON_SMOD)
#define SPCON_ENSCK     (1 << 4)
#define SPCON_MSTR      (1 << 3)
#define SPCON_CPOL      (1 << 2)
#define SPCON_CPOL_LOW  (1 << 2)
#define SPCON_CPOL_HIGH (0 << 2)
#define SPCON_CPHA      (1 << 1)
#define SPCON_CPHA_FMTA (0 << 1)
#define SPCON_CPHA_FMTB (1 << 1)
#define SPCON_TAGD      (1 << 0)

#define SPSTA_DCOL      (1 << 2)                /* Data Collision Error */
#define SPSTA_MULF      (1 << 1)                /* Multi Master Error */
#define SPSTA_READY     (1 << 0)                /* Data Tx/Rx ready */

/*
 * IIC Controller (Chapter 13)
 */

#define IICCON          __REG(0x44600000)
#define IICSTAT         __REG(0x44600004)
#define IICADD          __REG(0x44600008)
#define IICDS           __REG(0x4460000C)
#define IICADADLY       __REG(0x44600010)

/*
 * Memory Stick Controller (Chapter 31)
 */
#define _MS_BASE0       0x46100000
#define _MS_BASE1       0x46108000
#define bMS_CTL0(x)     __REG(_MS_BASE0 + (x))
#define bMS_CTL1(x)     __REG(_MS_BASE1 + 4*(x))
#define MSPRE           bMS_CTL0(0)     /* Prescaler Control */
#define MSFINTCON       bMS_CTL0(4)     /* FIFO Interrupt Control */
#define MS_TP_CMD       bMS_CTL1(0)     /* Transfer Protocol Command */
#define MS_CTRL_STA     bMS_CTL1(1)     /* Control1 and Status */
#define MS_DAT          bMS_CTL1(2)     /* Data FIFO */
#define MS_INT          bMS_CTL1(3)     /* Interrupt Control and Status */
#define MS_INS          bMS_CTL1(4)     /* INS port Control and Status */
#define MS_ACMD         bMS_CTL1(5)     /* Auto Command/Polarity Control */
#define MS_ATP_CMD      bMS_CTL1(6)     /* Auto Transfer Protocol Command */

#define MSPRE_EN        (1 << 2)        /* Prescaler control,
                                           0:Disable, 1:Enable. */
#define MSPRE_VAL       (0x3 << 0)      /* Prescaler value */
#define MSPRE_VAL1      (0x0 << 0)      /* 1/1 */
#define MSPRE_VAL2      (0x1 << 0)      /* 1/2 */
#define MSPRE_VAL4      (0x2 << 0)      /* 1/4 */
#define MSPRE_VAL8      (0x3 << 0)      /* 1/8 */

#define MSFINTCON_EN    0x1             /* FIFO interrupt control,
                                           0:only for XINT,
                                           1:FIFO interrupt enable */

#define MS_CTRL_RST     (1 << 15)       /* Internal logic reset */
#define MS_CTRL_PWS     (1 << 14)       /* Power save mode */
#define MS_CTRL_SIEN    (1 << 13)       /* Serial interface enable */
#define MS_CTRL_NOCRC   (1 << 11)       /* INT_CRC disable */
#define MS_CTRL_BSYCNT  (0x7 << 8)      /* Busy timeout count
                                           timeout time = BSYCNT*4+2[pclks] */
#define fMS_CTRL_BSYCNT Fld(3,8)
#define MS_CTRL_BSY(x)  FInsrt((x), fMS_CTRL_BSYCNT)
#define MS_INT_STA      (1 << 7)        /* interrupt generated */
#define MS_DRQ_STA      (1 << 6)        /* DMA requested */
#define MS_RBE_STA      (1 << 3)        /* Receive buffer empty */
#define MS_RBF_STA      (1 << 2)        /* Receive buffer full */
#define MS_TBE_STA      (1 << 1)        /* Transmit buffer empty */
#define MS_TBF_STA      (1 << 0)        /* Transmit buffer full */

#define MS_INT_EN       (1 << 15)       /* Memory stick Interrupt enable */
#define MS_TR_INTEN     (1 << 14)       /* Data transfer interrupt enable */
#define MS_INS_INTEN    (1 << 13)       /* Insertion interrupt enable */
#define MS_INT_P_END    (1 << 7)        /* Protocol end interrupt status
                                           0 = In progress 1 = Complete */
#define MS_INT_SIF      (1 << 6)        /* Serial interface receive INTd */
#define MS_INT_TR       (1 << 5)        /* Data transfer request INTd */
#define MS_INT_INS      (1 << 4)        /* Insertion INTd */
#define MS_INT_CRC      (1 << 1)        /* INT_CRC error */
#define MS_INT_TOE      (1 << 0)        /* BUSY timeout error */

#define MS_INS_EN       (1 << 12)       /* INS port enable */
#define MS_INS_STA      (1 << 4)        /* INS port status. 1:Low(Insert) */

#define MS_ACMD_EN      (1 << 15)       /* Auto Command op. enable */
#define MS_ACMD_RISING  (0 << 14)       /* serial data input is rising Edge */
#define MS_ACMD_FALLING (1 << 14)       /* serial data input is falling Edge */

/*
 * Modem Interface (Chapter 19)
 */
#define INT2AP          __REG(0x41180000)
#define INT2MDM         __REG(0x41180004)

#define fINT2AP_ADR     Fld(11,0)       /* IRQ to AP address */
#define INT2AP_ADR      FMsk(fINT2AP_ADR)
#define fINT2MDM_ADR    Fld(11,0)       /* IRQ to Modem address */
#define INT2MDM_ADR     FMsk(fINT2MDM_ADR)

/*
 * Power management
 */
#define ALIVECON        __REG(0x44800044)
#define GPDATINSLEEP    __REG(0x44800048)
#define ENGPINSLEEP     __REG(0x4480004c)
#define GPUPINSLEEP     __REG(0x44800050)
#define DATRINSLEEP0    __REG(0x44800054)
#define DATRINSLEEP1    __REG(0x44800058)
#define OENINSLEEP0     __REG(0x4480005c)
#define OENINSLEEP1     __REG(0x44800060)
#define ENPUINSLEEP     __REG(0x44800064)
#define RSTCNT          __REG(0x44800068)

#endif /* _S3C24A0_H_ */
