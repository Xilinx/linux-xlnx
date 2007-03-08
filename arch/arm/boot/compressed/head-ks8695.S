#include <asm/mach-types.h>

	.section	".start", "ax"
	mov	r7, #(MACH_TYPE_KS8695 & 0xff)
	orr	r7, r7, #(MACH_TYPE_KS8695 & 0xff00)
