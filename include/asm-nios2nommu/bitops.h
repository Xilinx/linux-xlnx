#ifndef __ASM_NIOS2NOMMU_BITOPS_H
#define __ASM_NIOS2NOMMU_BITOPS_H

#ifdef __KERNEL__
#include <asm/system.h>
#include <asm-generic/bitops.h>
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()
#endif /* __KERNEL__ */

#endif /* __ASM_NIOS2NOMMU_BITOPS_H */
