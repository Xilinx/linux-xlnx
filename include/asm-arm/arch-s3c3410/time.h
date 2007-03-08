/*
 *  linux/include/asm-arm/arch-s3c3410/time.h
 *
 * 2003 Thomas Eschenbacher <thomas.eschenbacher@gmx.de>
 * modifed by Hyok S. Choi <hyok.choi@samsung.com>
 *
 * Setup for 16 bit timer 0, used as system timer.
 *
 */

#ifndef __ASM_ARCH_TIME_H__
#define __ASM_ARCH_TIME_H__

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/hardware.h>
#include <asm/arch/timex.h>

#define CLOCKS_PER_USEC	(CONFIG_ARM_CLK/1000000)

#define S3C3410X_TIMER0_PRESCALER 100

#endif
