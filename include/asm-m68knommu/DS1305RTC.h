/***************************************************************************
                          DS1305RTC.h  -
              Real time clock control functions .
              These functions rely on the SPI module for communication.
                             -------------------
    begin                : Tue Oct 14 2003
    email                : ngustavson@emacinc.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         
 *   This program is free software; you can redistribute it and/or modify  
 *   it under the terms of the GNU General Public License as published by  
 *   the Free Software Foundation; either version 2 of the License, or   
 *   (at your option) any later version.                                   
 *                                                                         
 ***************************************************************************/
#ifndef DS1305RTC_H
#define DS1305RTC_H
#include <asm/coldfire.h> 
#include <asm/mcf_qspi.h>            
#include <linux/rtc.h>


#define WRITE_OFFSET 0x80
enum registers { SEC,MIN,HOURS,DAY,DATE,MONTH,YEAR,
SEC_ALARM0,MIN_ALARM0,HOUR_ALARM0,DAY_ALARM0, //alarm0
SEC_ALARM1,MIN_ALARM1,HOUR_ALARM1,DAY_ALARM1, //alarm1
CONTROL,STATUS,TRICKLE_CHARGER};


#define DSNAME "ds1305rtc"
#define DS_DRIVER_V "1.0" 
#define DSMAJORNUM 0

/*CONTROL bits*/
#define EOSC 0x80
#define WP     0x40
#define INTCN 4
#define AIE1   2
#define AIE0   1

#define INT0  0x04
#define INT1  0x08

#define DDQS   *(volatile u8 *)(MCF_MBAR + 0x100021)
#define SETQS   *(volatile u8 *)(MCF_MBAR + 0x100035)
#define CLEARQS   *(volatile u8 *)(MCF_MBAR + 0x100049)

#define RTC_CS_MASK 0x40
#define RTC_CE_SETUP() DDQS|=RTC_CS_MASK
#define RTC_CE_ON()  qspi_mutex_down();SETQS=RTC_CS_MASK;
#define RTC_CE_OFF() CLEARQS=~RTC_CS_MASK;qspi_mutex_up();	


typedef struct rtc_qspi_device{
qspi_dev *qspi;	
}rtc_qspi_device;

#define SUCCESS 1

#define TIME2RTC(data) (((data/10)<<4)+(data%10))
#define RTC2TIME(data) (((data>>4)*10)+(data&0x0f))

#define RTC_SECONDS(dev)    RTC_Read_Register(dev,SEC)
#define RTC_MINUTES(dev)      RTC_Read_Register(dev,MIN)
#define RTC_HOURS(dev)         RTC_Read_Register(dev,HOURS)
#define RTC_DAY(dev)             	RTC_Read_Register(dev,DAY)
#define RTC_DATE(dev)           	RTC_Read_Register(dev,DATE)
#define RTC_MONTH(dev)        RTC_Read_Register(dev,MONTH)
#define RTC_YEAR(dev)           	RTC_Read_Register(dev,YEAR)


#define RTC_SET_SECONDS(dev,data)   RTC_Write_Register(dev,SEC,TIME2RTC(data))
#define RTC_SET_MINUTES(dev,data)     RTC_Write_Register(dev,MIN,TIME2RTC(data))
#define RTC_SET_HOURS(dev,data)        RTC_Write_Register(dev,HOURS,TIME2RTC(data))
#define RTC_SET_DAY(dev,data)            RTC_Write_Register(dev,DAY,TIME2RTC(data))
#define RTC_SET_DATE(dev,data)          RTC_Write_Register(dev,DATE,TIME2RTC(data))
#define RTC_SET_MONTH(dev,data)       RTC_Write_Register(dev,MONTH,TIME2RTC(data))
#define RTC_SET_YEAR(dev,data)          RTC_Write_Register(dev,YEAR,TIME2RTC(data))

#endif /*DS1305RTC_H*/