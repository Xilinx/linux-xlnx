/*
 *  linux/arch/arm/mm/cache-s3c44b0.S
 *
 *  caution: s3c44b0x DONT have a coprocessor!
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/hardware.h>
#include <asm/page.h>

ENTRY(v3_flush_user_cache_all)
ENTRY(v3_flush_kern_cache_all)
ENTRY(v3_flush_user_cache_range)

ENTRY(v3_coherent_kern_range)

ENTRY(v3_flush_kern_dcache_page)
ENTRY(v3_dma_inv_range)
ENTRY(v3_dma_flush_range)
ENTRY(v3_dma_clean_range)
	mov	pc, lr

	__INITDATA

	.type	v3_cache_fns, #object
ENTRY(v3_cache_fns)
	.long	v3_flush_kern_cache_all
	.long	v3_flush_user_cache_all
	.long	v3_flush_user_cache_range
	.long	v3_coherent_kern_range
	.long	v3_flush_kern_dcache_page
	.long	v3_dma_inv_range
	.long	v3_dma_clean_range
	.long	v3_dma_flush_range
	.size	v3_cache_fns, . - v3_cache_fns
