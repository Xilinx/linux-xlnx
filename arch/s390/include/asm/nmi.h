/*
 *   Machine check handler definitions
 *
 *    Copyright IBM Corp. 2000, 2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#ifndef _ASM_S390_NMI_H
#define _ASM_S390_NMI_H

#include <linux/const.h>
#include <linux/types.h>

#define MCCK_CODE_SYSTEM_DAMAGE		_BITUL(63)
#define MCCK_CODE_CPU_TIMER_VALID	_BITUL(63 - 46)
#define MCCK_CODE_PSW_MWP_VALID		_BITUL(63 - 20)
#define MCCK_CODE_PSW_IA_VALID		_BITUL(63 - 23)

#ifndef __ASSEMBLY__

union mci {
	unsigned long val;
	struct {
		u64 sd :  1; /* 00 system damage */
		u64 pd :  1; /* 01 instruction-processing damage */
		u64 sr :  1; /* 02 system recovery */
		u64    :  1; /* 03 */
		u64 cd :  1; /* 04 timing-facility damage */
		u64 ed :  1; /* 05 external damage */
		u64    :  1; /* 06 */
		u64 dg :  1; /* 07 degradation */
		u64 w  :  1; /* 08 warning pending */
		u64 cp :  1; /* 09 channel-report pending */
		u64 sp :  1; /* 10 service-processor damage */
		u64 ck :  1; /* 11 channel-subsystem damage */
		u64    :  2; /* 12-13 */
		u64 b  :  1; /* 14 backed up */
		u64    :  1; /* 15 */
		u64 se :  1; /* 16 storage error uncorrected */
		u64 sc :  1; /* 17 storage error corrected */
		u64 ke :  1; /* 18 storage-key error uncorrected */
		u64 ds :  1; /* 19 storage degradation */
		u64 wp :  1; /* 20 psw mwp validity */
		u64 ms :  1; /* 21 psw mask and key validity */
		u64 pm :  1; /* 22 psw program mask and cc validity */
		u64 ia :  1; /* 23 psw instruction address validity */
		u64 fa :  1; /* 24 failing storage address validity */
		u64 vr :  1; /* 25 vector register validity */
		u64 ec :  1; /* 26 external damage code validity */
		u64 fp :  1; /* 27 floating point register validity */
		u64 gr :  1; /* 28 general register validity */
		u64 cr :  1; /* 29 control register validity */
		u64    :  1; /* 30 */
		u64 st :  1; /* 31 storage logical validity */
		u64 ie :  1; /* 32 indirect storage error */
		u64 ar :  1; /* 33 access register validity */
		u64 da :  1; /* 34 delayed access exception */
		u64    :  7; /* 35-41 */
		u64 pr :  1; /* 42 tod programmable register validity */
		u64 fc :  1; /* 43 fp control register validity */
		u64 ap :  1; /* 44 ancillary report */
		u64    :  1; /* 45 */
		u64 ct :  1; /* 46 cpu timer validity */
		u64 cc :  1; /* 47 clock comparator validity */
		u64    : 16; /* 47-63 */
	};
};

struct pt_regs;

extern void s390_handle_mcck(void);
extern void s390_do_machine_check(struct pt_regs *regs);

#endif /* __ASSEMBLY__ */
#endif /* _ASM_S390_NMI_H */
