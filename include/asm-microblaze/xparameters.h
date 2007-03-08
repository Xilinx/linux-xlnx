#ifndef _ASM_XPARAMETERS_H
#define _ASM_XPARAMETERS_H

#if defined (CONFIG_SP3E)
#include <asm/xparameters-sp3e.h>
#else
#error "No xparameters for this target."
#endif

#endif /* _ASM_XPARAMETERS_H */
