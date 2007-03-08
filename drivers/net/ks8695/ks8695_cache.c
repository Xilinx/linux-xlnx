/*
	Copyright (c) 2002, Micrel Kendin Operations

	Written 2002 by LIQUN RUAN

	This software may be used and distributed according to the terms of 
	the GNU General Public License (GPL), incorporated herein by reference.
	Drivers based on or derived from this code fall under the GPL and must
	retain the authorship, copyright and license notice. This file is not
	a complete program and may only be used when the entire operating
	system is licensed under the GPL.

	The author may be reached as lruan@kendin.com
	Micrel Kendin Operations
	486 Mercury Dr.
	Sunnyvale, CA 94085

	This driver is for Kendin's KS8695 SOHO Router Chipset as ethernet driver.

	Support and updates available at
	www.kendin.com or www.micrel.com

*/
#include "ks8695_drv.h"
#include "ks8695_cache.h"

/* for 922T, the values are fixed */
#if	0
static uint32_t	uICacheLineLen = 8;		/* 8 dwords, 32 bytes */
static uint32_t	uICacheSize = 8192;		/* 8K cache size */
#endif

static uint32_t	bPowerSaving = FALSE;
static uint32_t	bAllowPowerSaving = FALSE;

/*
 * ks8695_icache_read_c9
 *	This function is use to read lockdown register
 *
 * Argument(s)
 *	NONE
 *
 * Return(s)
 *	NONE
 */
void ks8695_icache_read_c9(void)
{
	register int base;

	__asm__(
		"mrc p15, 0, %0, c9, c0, 1"
		 : "=r" (base)
		);

	DRV_INFO("%s: lockdown index=%d", __FUNCTION__, (base >> 26));
}

#if	0
/*
 * ks8695_icache_lock
 *	This function is use to lock given icache
 *
 * Argument(s)
 *	icache_start	pointer to starting icache address
 *	icache_end		pointer to ending icache address
 *
 * Return(s)
 *	0		if success
 *	error	otherwise
 */
int ks8695_icache_lock(void *icache_start, void *icache_end)
{
	uint32_t victim_base = (ICACHE_VICTIM_BASE << ICACHE_VICTIM_INDEX);
	int	len;
	spinlock_t	lock = SPIN_LOCK_UNLOCKED;
	unsigned long flags;

	len = (int)(icache_end - icache_start);
	DRV_INFO("%s: start=%p, end=%p, len=%d, victim=0x%x", __FUNCTION__, icache_start, icache_end, len, victim_base);
	
	/* if lockdown lines are more than half of max associtivity */
	if ((len / ICACHE_BYTES_PER_LINE) > (ICACHE_ASSOCITIVITY >> 1)) {
		DRV_WARN("%s: lockdown lines = %d is too many, (Assoc=%d)", __FUNCTION__, (len / ICACHE_BYTES_PER_LINE), ICACHE_ASSOCITIVITY);
		return -1;
	}

	spin_lock_irqsave(&lock, flags);

	__asm__(
		" \n\
		ADRL	r0, ks8695_isr \n\
		ADRL	r1, ks8695_isre \n\
		MOV	r2, %1 \n\
		MCR	p15, 0, r2, c9, c4, 1 \n\
lock_loop: \n\
		MCR	p15, 0, r0, c7, c13, 1 \n\
		ADD	r0, r0, #32 \n\
 \n\
		AND	r3, r0, #0x60 \n\
		CMP	r3, #0x0 \n\
		ADDEQ	r2, r2, #0x1<<26 \n\
		MCREQ	p15, 0, r2, c9, c0, 1 \n\
		  \n\
		CMP	r0, r1 \n\
		BLE	lock_loop \n\
 \n\
		CMP	r3, #0x0 \n\
		ADDNE	r2, r2, #0x1<<26 \n\
		MCRNE	p15, 0, r2, c9, c0,	1 \n\
		"
		: "=r" (len)
		: "r" (victim_base)
		: "r0", "r1", "r2", "r3"
		);

		ks8695_icache_read_c9();

#if	0
	/* following are the assemble code for icache lock down, a C version should be implemented, accordingly */
	ADRL	r0, start_address			; address pointer
	ADRL	r1, end_address
	MOV		r2, #lockdown_base<<26		; victim pointer
	MCR		p15, 0, r2, c9, c0, 1

loop
	MCR		p15, 0, r0, c7, c13, 1		; prefetch ICache line
	ADD		r0, r0, #32					; increment address pointer to next ICache line

	; do we need to increment the victim pointer ?
	; thest for segment 0, and if so, increment the victim pointer
	; and write the Icache victime and lockdown base

	AND		r3, r0, #0x60				; extract the segment bits from the addr.
	CMP		r3, #0x0					; test the segment
	ADDEQ	r2, r2, #0x1<<26			; if segment 0, increment victim pointer
	MCREQ	p15, 0, r2, c9, c0, 1		; and write ICaceh victim and lockdown

	; have we linefilled enough code?
	; test for the address pointer being less than or equal to the end_address
	; pointer and if so, loop and perform another linefill

	CMP		r0, r1						; test for less than or equal to end_address
	BLE		loop						; if not, loop

	; have we exited with r3 pointer to segment 0?
	; if so, the ICaceh victim and lockdown base has already been set to one higher
	; than the last entry written.
	; if not, increment the victim pointer and write the ICache victim and
	; lockdown base.

	CMP		r3, #0x0					; test for segments 1 to 3
	ADDNE	r2, r2, #0x1 << 26			; if address is segment 1 to 3
	MCRNE	p15, 0, r2, c9, c0,	1		; write ICache victim and lockdown base.

#endif

	spin_unlock_irqrestore(&lock, flags);

	return 0;
}
#endif

/*
 * ks8695_icache_unlock
 *	This function is use to unlock the icache locked previously
 *
 * Argument(s)
 *	NONE.
 *
 * Return(s)
 *	NONE.
 */
void ks8695_icache_unlock(void)
{
#ifdef	DEBUG_THIS
	DRV_INFO("%s", __FUNCTION__);
#endif

	__asm__(
		" \n\
		MOV r1, #0 \n\
		MCR	p15, 0, r1, c9, c0, 1	/* reset victim base to 0 */ \n\
		"
	);

	DRV_INFO("%s", __FUNCTION__);
}

/*
 * ks8695_icache_change_policy
 *	This function is use to change cache policy for ARM chipset
 *
 * Argument(s)
 *	bRoundRobin		round robin or random mode
 *
 * Return(s)
 *	NONE
 */
void ks8695_icache_change_policy(int bRoundRobin)
{
	uint32_t tmp;

	__asm__ ( 
		" \n\
		mrc p15, 0, r1, c1, c0, 0 \n\
		mov	r2, %1 \n\
		cmp	r2, #0 \n\
		orrne r1, r1, #0x4000 \n\
		biceq r1, r1, #0x4000 \n\
		mov	%0, r1 \n\
		/* Write this to the control register */ \n\
		mcr p15, 0, r1, c1, c0, 0 \n\
		/* Make sure the pipeline is clear of any cached entries */ \n\
		nop \n\
		nop \n\
		nop" 
		: "=r" (tmp)
		: "r" (bRoundRobin)
		: "r1", "r2"
		);

/*#ifdef	DEBUG_THIS*/
	DRV_INFO("Icache mode = %s", bRoundRobin ? "roundrobin" : "random");
/*#endif*/
}

/*
 * ks8695_enable_power_saving
 *	This function is use to enable/disable power saving
 *
 * Argument(s)
 *	bSaving		
 *
 * Return(s)
 *	NONE
 */
void ks8695_enable_power_saving(int bEnablePowerSaving)
{
	bAllowPowerSaving = bEnablePowerSaving;
}

/*
 * ks8695_power_saving
 *	This function is use to put ARM chipset in low power mode (wait for interrupt)
 *
 * Argument(s)
 *	bSaving		
 *
 * Return(s)
 *	NONE
 */
void ks8695_power_saving(int bSaving)
{
	uint32_t tmp;

	/* if not allowed by configuration option */
	if (!bAllowPowerSaving)
		return;

	/* if already set */
	if (bPowerSaving == bSaving)
		return;

	bPowerSaving = bSaving;

	__asm__ ( 
		" \n\
		mov	r1, %1 \n\
		mcr p15, 0, r1, c7, c0, 4 \n\
		/* Make sure the pipeline is clear of any cached entries */ \n\
		nop \n\
		nop \n\
		nop" 
		: "=r" (tmp)
		: "r" (bSaving)
		: "r1", "r2"
		);

	DRV_INFO("%s: power saving = %s", __FUNCTION__, bSaving ? "enabled" : "disabled");
}
