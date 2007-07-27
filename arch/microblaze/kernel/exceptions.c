/*
 *	arch/microblaze/kernel/exceptions.c - HW exception handling
 *
 * Copyright 2007 Xilinx, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <asm/exceptions.h>
#include <asm/entry.h>		/* For KM CPU var */

/* Initialize_exception_handlers() - called from setup.c/trap_init() */
void initialize_exception_handlers(void)
{
}

#define MICROBLAZE_ILL_OPCODE_EXCEPTION	0x02
#define MICROBLAZE_IOPB_BUS_EXCEPTION	0x03
#define MICROBLAZE_DOPB_BUS_EXCEPTION	0x04
#define MICROBLAZE_DIV_ZERO_EXCEPTION	0x05
#define MICROBLAZE_FPU_EXCEPTION	0x06

static void handle_unexpected_exception(unsigned int esr,
					unsigned int kernel_mode, unsigned int addr)
{
	printk(KERN_WARNING "Unexpected exception %02x in %s mode, PC=%08x\n",
	       esr, kernel_mode ? "kernel" : "user", addr);
}

static void handle_exception(const char *message, int signal,
			     unsigned int kernel_mode, unsigned int addr)
{
	if (kernel_mode) {
		panic("%s in the kernel mode, PC=%08x\n", message, addr);
	} else {
		force_sig(signal, current);
	}
}

asmlinkage void other_exception_handler(unsigned int esr, unsigned int addr)
{
	unsigned int kernel_mode = per_cpu(KM,0); 

	switch (esr) {

	case MICROBLAZE_ILL_OPCODE_EXCEPTION:
		handle_exception("Illegal instruction", SIGILL, kernel_mode, addr);
		break;

	case MICROBLAZE_IOPB_BUS_EXCEPTION:
		handle_exception("Instruction bus error", SIGBUS, kernel_mode, addr);
		break;

	case MICROBLAZE_DOPB_BUS_EXCEPTION:
		handle_exception("Data bus error", SIGBUS, kernel_mode, addr);
		break;

	case MICROBLAZE_DIV_ZERO_EXCEPTION:
		handle_exception("Divide by zero", SIGILL, kernel_mode, addr);
		break;

	case MICROBLAZE_FPU_EXCEPTION:
		handle_exception("FPU error", SIGFPE, kernel_mode, addr);
		break;

	default:
		handle_unexpected_exception(esr, kernel_mode, addr);
	}

	return;
}
