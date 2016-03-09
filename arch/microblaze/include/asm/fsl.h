/*
 * include/asm-microblaze/fsl.h -- Low-level FSL ops
 *
 *  Copyright (C) 2004  John Williams (jwilliams@itee.uq.edu.au)
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef _FSL_H
#define _FSL_H

/*
 * We provide a bunch of kernel macros to make life easier when writing
 * device drivers for FSL-based cores.  Specifically, we have:
 *
 * Put values onto a particular FSL channel (id):
 *	fsl_nput(id,value,status)	- data put
 *	fsl_ncput(id,value,status)	- control put
 *
 * Get values from an FSL channel:
 *	fsl_nget(id,value,status)	- data get
 *	fsl_ncget(id,value,status)	- control get
 *
 * Interpret status value returned from the above macros:
 *	fsl_error(status) 		- error (usually control/data mismatch)
 *	fsl_nodata(status)		- no data (or FSL full)
 */

#include <asm/macrology.h>

#ifndef __KERNEL__
#warning You really should not use these from userspace! 
#endif

/* How to do this?  FSL channel ID is embedded in opcode...

   Do it as a bunch of macros, using a case statement.  gcc at optimisation
   level 2 seems to make a pretty good job of it.

   This creates cryptic (ab)use of the C preprocessor.
   cc can detect and optimise for the situation where the fsl port ID is a 
   compile-time constant.  Otherwise, it generates the full packet of guff...

   Goran promised that if I had to write self-modifying code, he would 
	alter the instruction set!
*/

/* Bit positions within Microblaze MSR */
#define MSR_CARRY_MASK		((1 << 2))
#define MSR_FSL_ERROR_MASK	((1 << 4))

/* Operators to query return status from fsl operations */
#define fsl_error(status) (((status) & MSR_FSL_ERROR_MASK) ? 1 : 0)
#define fsl_nodata(status) (((status) & MSR_CARRY_MASK) ? 1 : 0)

/* "raw" nget and nput macros, using macro params to specify the 
   FSL channel number. Obviously, "id" must be statically and numerically 
   specified at compile time.  status is just a sample of the MSR immediately 
   following the fsl operation.  No idea what happens if we get interrupted
   in between, but really don't want to do save_flags_cli()/restore_flags()...
   Hopefully the interrupt handler is well behaved and saves/restores flags reg

   Blocking gets and puts are not supported - they could (will) lock up 
   the processor harder than you can imagine!  Not even an NMI will recover
   an FSL blocked microblaze - only reset!

   We support control and data get/put ops.
*/

/* Form a string like rFSL{id} */
#define __fsl_id(id)							\
	macrology_stringify(macrology_paste(rFSL,id))

/* Construct the standard sequences of "fsl op; get msr" */
#define __fsl_nget_t(id, value, status)					\
	__asm__ __volatile__ ("tnget  %0, " __fsl_id(id) ";\n		\
				mfs %1, rMSR; \n"			\
				: "=r" (value), "=r" (status)		\
				:					\
				: "memory");

#define __fsl_nget(id, value, status)					\
	__asm__ __volatile__ ("	nget  %0, " __fsl_id(id) ";\n		\
				mfs %1, rMSR; \n"			\
				: "=r" (value), "=r" (status)		\
				:					\
				: "memory");

#define __fsl_ncget(id, value, status)					\
	__asm__ __volatile__ ("	ncget	%0, " __fsl_id(id) ";\n		\
				mfs	%1, rMSR;\n"			\
				: "=r" (value), "=r" (status)		\
				:					\
				: "memory");

#define __fsl_nput(id, value, status)					\
	__asm__ __volatile__ ("nput	%1, " __fsl_id(id) " ;\n	\
				mfs	%0, rMSR;\n"			\
				: "=r" (status)				\
				: "r" (value)				\
				: "memory");


#define __fsl_nput_t(id, value, status)					\
	__asm__ __volatile__ ("tnput " __fsl_id(id) " ;\n	\
				mfs	%0, rMSR;\n"			\
				: "=r" (status)				\
				: "r" (value)				\
				: "memory");


#define __fsl_ncput(id, value, status)					\
	__asm__ __volatile__ ("ncput	%1, "  __fsl_id(id) ";\n	\
				mfs	%0, rMSR;\n"			\
				: "=r" (status)				\
				: "r" (value)				\
				: "memory");


/* Some more hideous C preproc stuff to save typing 

  op is the fsl op (get|put}
  id is the fsl id (0-7)
  iscont is either 'c' or blank - is it a control put/get?
*/
#define __fsl_op(op,iscont)						\
	macrology_paste(__fsl_n,macrology_paste(iscont,op))

#define __fsl_case_helper(id,value,status,op,iscont)			\
	case id:							\
		__fsl_op(op,iscont)(id,value,status);			\
		break;

#define fsl_op(id,value,status,op,iscont)				\
{									\
	switch((id))							\
	{								\
		__fsl_case_helper(0,value,status,op,iscont);		\
		__fsl_case_helper(1,value,status,op,iscont);		\
		__fsl_case_helper(2,value,status,op,iscont);		\
		__fsl_case_helper(3,value,status,op,iscont);		\
		__fsl_case_helper(4,value,status,op,iscont);		\
		__fsl_case_helper(5,value,status,op,iscont);		\
		__fsl_case_helper(6,value,status,op,iscont);		\
		__fsl_case_helper(7,value,status,op,iscont);		\
		default:						\
			status=~0;					\
	}								\
}

/* The main user "routines"  The nice thing about this is that at -O2, if
   a constant is passed as the fsl ID, the entire case structure gets
   optimised away, reducing to just the specific fsl and rmsr opcodes 
   for the operation.  Very nice :)
*/

#define fsl_nput_t(id,value,status)					\
	{fsl_op(id,value,status,put_t,)}

#define fsl_nput(id,value,status)					\
	{fsl_op(id,value,status,put,)}

#define fsl_ncput(id,value,status)					\
	{fsl_op(id,value,status,put,c)}

#define fsl_nget_t(id,value,status)					\
	{fsl_op(id,value,status,get_t,)}

#define fsl_nget(id,value,status)					\
	{fsl_op(id,value,status,get,)}

#define fsl_ncget(id,value,status)					\
	{fsl_op(id,value,status,get,c)}

#endif


