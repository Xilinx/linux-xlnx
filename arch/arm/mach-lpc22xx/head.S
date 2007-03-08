/*
 *  linux/arch/arm/mach-lpc22xx/head.S
 *
 *  uClinux kernel startup code for lpc22xx
 *  which has no proper bootloader for linux startup
 *  because of XIP.
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/mach-types.h>
#include <asm/procinfo.h>
#include <asm/ptrace.h>
#include <asm/mach/arch.h>
#include <asm/arch/hardware.h>
/*
 * Kernel startup entry point.
 */
	__INIT
	.type	stext, #function
ENTRY(stext)

	mov	r12, r0
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE	@ make sure svc mode
	msr	cpsr_c, r0			@ and all irqs disabled

/* Do basic initialization */
/* Set the parameters of PLL, PINSEL, and MEMMAP carefully according to your board */
/* These settings can be done in your bootloader instead. */

		ldr	r2,rREG_VPBDIV
#if((LPC22xx_Fcclk /4)/LPC22xx_Fpclk)==1
		mov	r1,#0x0
#endif		
#if((LPC22xx_Fcclk /4)/LPC22xx_Fpclk)==2
		mov	r1,#0x2
#endif	
#if((LPC22xx_Fcclk /4)/LPC22xx_Fpclk)==4
		mov	r1,#0x1
#endif	
		str	r1,[r2]

		ldr	r2,rREG_PLLCFG
		
#if(LPC22xx_Fcco/LPC22xx_Fcclk/2)==1
		mov	r1,#(((LPC22xx_Fcclk/CONFIG_LPC22xx_Fosc)-1))
#endif    
#if(LPC22xx_Fcco/LPC22xx_Fcclk/2)==2
		mov	r1,#(((LPC22xx_Fcclk/CONFIG_LPC22xx_Fosc)-1)|(1<<5))
#endif    
#if(LPC22xx_Fcco/LPC22xx_Fcclk/2)==4
		mov	r1,#(((LPC22xx_Fcclk/CONFIG_LPC22xx_Fosc)-1)|(2<<5))
#endif    
#if(LPC22xx_Fcco/LPC22xx_Fcclk/2)==8
		mov	r1,#(((LPC22xx_Fcclk/CONFIG_LPC22xx_Fosc)-1)|(3<<5))
#endif    

		str	r1,[r2]
		
		ldr	r2,rREG_PLLCON
		mov	r1,#0x01
		str	r1,[r2]

		ldr	r2,rREG_PLLFEED
		mov	r1,#0xAA
		str	r1,[r2]
		mov	r1,#0x55
		str	r1,[r2]

1:		ldr	r2,rREG_PLLSTAT
		ldr	r1,[r2]
		ands	r1,r1,#0x400
		beq	1b

		ldr	r2,rREG_PLLCON
		mov	r1,#0x03
		str	r1,[r2]
		
		ldr	r2,rREG_PLLFEED
		mov	r1,#0xAA
		str	r1,[r2]
		mov	r1,#0x55
		str	r1,[r2]
	
		ldr	r2,rREG_PINSEL0
		ldr	r1,[r2]
		and	r1,r1,#~0x0f
		orr	r1,r1,#0x05
		str	r1,[r2]
	
		ldr	r2,rREG_MEMMAP
		mov	r1,#0x03
		str	r1,[r2]

		b	8f

rREG_PLLCFG:
	.long	REG_PLLCFG
rREG_PLLCON:
	.long	REG_PLLCON
rREG_PLLFEED:
	.long	REG_PLLFEED
rREG_VPBDIV:
	.long	REG_VPBDIV
rREG_PINSEL0:
	.long	REG_PINSEL0
rREG_MEMMAP:
	.long	REG_MEMMAP
rREG_PLLSTAT:
	.long	REG_PLLSTAT
		
VPBDIV_DAT:
	.long		0x00			/* one fourth of cclk */
PLLCFG_DAT:
	.long		0x23			/* Fosc=10Mhz cclk=40Mhz*/	
PLLCON_DAT:
	.long		0x03			/* PLL enable & connecte*/

8:	

    adr	r5, LC0
    ldmia	r5, {r5, r6, r8, r9, sp}			@ Setup stack

    /*  Copy data sections to their new home.  */


    /*  Clear BSS */
    mov	r4, #0
1:  cmp	r5, r8					
    strcc	r4, [r5],#4
    bcc	1b

  /*  Pretend we know what our processor code is (for arm_id)   */

	ldr	r2, LPC22xx_PROCESSOR_TYPE

	str     r2, [r6]
	ldr     r2, LPC22xx_MACH_TYPE
	str     r2, [r9]

        mov fp, #0
  	b	start_kernel
        
LC0:
	.long	__bss_start
        .long	processor_id
        .long	_end
	.long   __machine_arch_type
        .long	init_thread_union+8192

	.align	8
	
LPC22xx_PROCESSOR_TYPE:
	.long	0x22000000
LPC22xx_MACH_TYPE:
	.long	MACH_TYPE_LPC22xx

