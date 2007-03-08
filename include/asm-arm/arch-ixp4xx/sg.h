/*
 * include/asm-arm/arch-ixp4xx/sg.h
 *
 * Secure Computing/SnapGear platform specific definitions
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif

#include <asm/sizes.h>

/*
 * ixp4xx_exp_bus_size is not available during uncompress,
 * but it is always 16M for this platform.
 */
#define SG565_WATCHDOG_EXP_CS		IXP4XX_EXP_CS7
#define SG565_WATCHDOG_BASE_PHYS	(IXP4XX_EXP_BUS_BASE_PHYS + (7 * SZ_16M))

