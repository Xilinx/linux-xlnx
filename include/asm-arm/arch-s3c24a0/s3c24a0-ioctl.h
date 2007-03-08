/*
 * include/asm-arm/arch-s3c24a0/s3c24a0-ioctl.h
 * 
 * ioctl's defintion.
 *
 * $Id: s3c24a0-ioctl.h,v 1.2 2005/11/28 03:55:11 gerg Exp $
 * 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */
#include <linux/ioctl.h>
#include "s3c24a0-common.h"

#ifndef _INCLUDE_LINUETTE_IOCTL_H_
#define _INCLUDE_LINUETTE_IOCTL_H_
#ifndef __ASSEMBLY__

/*
 * see Documentation/ioctl-number.txt
 */
#define IOC_MAGIC		('h')

/*
 * for touch devices
 */
typedef struct {
  unsigned short pressure;
  unsigned short x;
  unsigned short y;
  unsigned short pad;
} TS_RET;

typedef struct {
  int xscale;
  int xtrans;
  int yscale;
  int ytrans;
  int xyswap;
} TS_CAL;

#define TS_GET_CAL		_IOR(IOC_MAGIC, 0x81, TS_CAL)
#define TS_SET_CAL		_IOW(IOC_MAGIC, 0x82, TS_CAL)
#define TS_ENABLE		_IO (IOC_MAGIC, 0x90)
#define TS_DISABLE		_IO (IOC_MAGIC, 0x91)

/*
 * below ioctl function is for hacker and iom
 */
/*
 * brightness control
 */
#define GET_BRIGHTNESS	_IOR(IOC_MAGIC, 0x83, unsigned int)
#define SET_BRIGHTNESS	_IOW(IOC_MAGIC, 0x84, unsigned int)
#define GET_BRIGHTNESS_INFO	_IOR(IOC_MAGIC, 0x8e, unsigned int)

/*
    . BATTERY_RET.level
          0~   : valid (usually, values from ADC or 0 ~ 100%)
          -1   : Unknown
      _ Remaining battery life

    . BATTERY_RET.ac
          0x00 : Off-line
          0x01 : On-line
          0xff : Unknown
      _ AC line status

    . BATTERY_RET.battery
          0x01 : Full		(== 100%)
          0x02 : Critical	(sleep definitely)
          0x03 : Charging
          0x04 : Low		(warning)
          0x10 : backup battery is low, change it
          0x40 : backup battery is present
          0x80 : system battery is present
          0xff : Unknown
      _ Battery status
*/

#define AC_OFF_LINE		0x00
#define AC_ON_LINE		0x01
#define AC_UNKNOWN		0xff

#define BATTERY_FULL		0x01
#define BATTERY_CRIT		0x02
#define BATTERY_CHARGE		0x03
#define BATTERY_LOW		0x04
#define battery_stat(x)	((x) & 0xf)
#define BATTERY_BAK_LOW		0x10
#define BATTERY_BAK		0x40
#define BATTERY_SYS		0x80
#define BATTERY_UNKNOWN		0xff

#define BATTERY_TIMER_STOP	0	/* unit: sec. */

typedef struct {
    int level, voltage, raw;
    unsigned char ac;
    unsigned char battery;
} BATTERY_RET;
#define GET_BATTERY_STATUS	_IOR(IOC_MAGIC, 0x85, BATTERY_RET)
#define SET_BATTERY_TIMER	_IOR(IOC_MAGIC, 0x8f, unsigned int)

/*
 * for apm_bios
 */
#define PM_STATE_QUERY	0x20
#define PM_STATE_D0		0
#define PM_STATE_D1		1
#define PM_STATE_D2		2
#define PM_STATE_D3		3
#define PM_STATE_UNKNOWN	(-1)

struct pm_usr_dev {
    unsigned long dev;
    unsigned long type, id;
    int state;
};
#define PM_DEV		_IOW(IOC_MAGIC, 0x86, struct pm_usr_dev) 

/* if some devices gives veto, do not sleep */
#define USR_SUSPEND		_IO (IOC_MAGIC, 0x87)
/* sleep simply */
#define SYS_SUSPEND		_IO (IOC_MAGIC, 0x88)
/* LCD/INPUT/removable sleep
   or if not, sleep as soon as possible */
#define STANDBY		_IO (IOC_MAGIC, 0x89)
/* wakeup devices */
#define RESUME		_IO (IOC_MAGIC, 0x8a)

/*
 * for /dev/misc/apm_bios
 */
#define LED_ON		0x01
#define LED_OFF		0x00
#define LED_BLINK		0x04
#define LED_BLINK_RATE	0x08	/* variable-rate blink */
#define LED_READ_ONLY	0x80
#define LED_COLOR		0x40

typedef struct {
  unsigned int index;		/* LED index to control */
  unsigned int stat;		/* control command or current status */
  unsigned int rate;		/* blinking rate */
  unsigned int color;		/* LED color */
  unsigned int info;		/* capable function */
} LED_RET;

#define GET_LED_NO		_IOR(IOC_MAGIC, 0x8b, unsigned int)
#define GET_LED_STATUS	_IOR(IOC_MAGIC, 0x8c, LED_RET)
#define SET_LED_STATUS	_IOW(IOC_MAGIC, 0x8d, LED_RET)

#include "s3c24a0-machine.h"
#endif	/* __ASSEMBLY__ */
#endif /* _INCLUDE_LINUETTE_IOCTL_H_ */
