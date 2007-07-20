#ifndef _ASM_SERIAL_H
#define _ASM_SERIAL_H

#include <asm/xparameters.h>

#if defined (CONFIG_SP3E)
#define BASE_BAUD	((XPAR_CPU_CLOCK_FREQ) / 16)
#endif

#endif /* _ASM_SERIAL_H */
