
/* linux/drivers/net/cs89x0_defs.h: cs89x0 specific settings for embedded boards
 *
 * Copyright (C) 2004  Georges Menie
 *
 */

#ifndef _CS89X0_DEFS_H_
#define _CS89X0_DEFS_H_

#ifdef CONFIG_M68328
#include <asm/irq.h>
#include <asm/MC68328.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_M68EZ328
#include <asm/irq.h>
#include <asm/MC68EZ328.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_M68VZ328
#include <asm/irq.h>
#include <asm/MC68VZ328.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_EXCALIBUR
#include <asm/nios.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_ARCH_TA7S
#include <asm/arch/arch.h>
#include <asm/arch/irqs.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_HYPERSTONE 
#include <asm/irq.h>
#include <asm/io.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef CONFIG_MACH_DM270
#include <asm/arch/irq.h>
#include <asm/arch/dm270.h>
#define _CS89X0_DEFS_EMBED_
#endif

#ifdef _CS89X0_DEFS_EMBED_

/* suppress debugging output */
#undef DEBUGGING
#define DEBUGGING	0

/* suppress DMA support */
#undef ALLOW_DMA
#define ALLOW_DMA	0

/* suppress EEPROM support */
#define NO_EPROM

/* suppress request_region() call */
#define NO_REQUEST_REGION

/* use static IRQ mapping */
#define MONO_IRQ_MAP

/* don't start sending packet before the whole data
 * has been written to the cs89x0 registers
 */
#define USE_TX_AFTER_ALL

/* place a hook into the cs89x0_probe1 function
 * to call cs89x_hw_init_hook() for hardware initialisation
 */
#define HW_INIT_HOOK

#endif /* _CS89X0_DEFS_EMBED_ */
#endif /* _CS89X0_DEFS_H_ */
