/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * Common Header for S3C64XX machines
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_S3C64XX_COMMON_H
#define __ARCH_ARM_MACH_S3C64XX_COMMON_H

#include <linux/reboot.h>

void s3c64xx_init_irq(u32 vic0, u32 vic1);
void s3c64xx_init_io(struct map_desc *mach_desc, int size);

void s3c64xx_restart(enum reboot_mode mode, const char *cmd);
void s3c64xx_init_late(void);

void s3c64xx_clk_init(struct device_node *np, unsigned long xtal_f,
	unsigned long xusbxti_f, bool is_s3c6400, void __iomem *reg_base);
void s3c64xx_set_xtal_freq(unsigned long freq);
void s3c64xx_set_xusbxti_freq(unsigned long freq);

#ifdef CONFIG_CPU_S3C6400

extern  int s3c6400_init(void);
extern void s3c6400_init_irq(void);
extern void s3c6400_map_io(void);

#else
#define s3c6400_map_io NULL
#define s3c6400_init NULL
#endif

#ifdef CONFIG_CPU_S3C6410

extern  int s3c6410_init(void);
extern void s3c6410_init_irq(void);
extern void s3c6410_map_io(void);

#else
#define s3c6410_map_io NULL
#define s3c6410_init NULL
#endif

#ifdef CONFIG_PM
int __init s3c64xx_pm_late_initcall(void);
#else
static inline int s3c64xx_pm_late_initcall(void) { return 0; }
#endif

#endif /* __ARCH_ARM_MACH_S3C64XX_COMMON_H */
