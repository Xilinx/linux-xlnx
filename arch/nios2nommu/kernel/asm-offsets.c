/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/hardirq.h>
#include <asm/nios.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{

	/* offsets into the task struct */
	DEFINE(TASK_STATE, offsetof(struct task_struct, state));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(TASK_BLOCKED, offsetof(struct task_struct, blocked));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_THREAD_INFO, offsetof(struct task_struct, thread_info));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));

	/* offsets into the kernel_stat struct */
	DEFINE(STAT_IRQ, offsetof(struct kernel_stat, irqs));

	/* offsets into the irq_cpustat_t struct */
	DEFINE(CPUSTAT_SOFTIRQ_PENDING, offsetof(irq_cpustat_t, __softirq_pending));

	/* offsets into the irq_node struct */
	DEFINE(IRQ_HANDLER, offsetof(struct irq_hand, handler));
	DEFINE(IRQ_FLAGS, offsetof(struct irq_hand, flags));
	DEFINE(IRQ_DEV_ID, offsetof(struct irq_hand, dev_id));
	DEFINE(IRQ_DEVNAME, offsetof(struct irq_hand, devname));

	/* offsets into the thread struct */
	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	DEFINE(THREAD_KPSR, offsetof(struct thread_struct, kpsr));
	DEFINE(THREAD_KESR, offsetof(struct thread_struct, kesr));
	DEFINE(THREAD_FLAGS, offsetof(struct thread_struct, flags));

	/* offsets into the pt_regs */
	DEFINE(PT_ORIG_R2, offsetof(struct pt_regs, orig_r2));
	DEFINE(PT_R1, offsetof(struct pt_regs, r1));
	DEFINE(PT_R2, offsetof(struct pt_regs, r2));
	DEFINE(PT_R3, offsetof(struct pt_regs, r3));
	DEFINE(PT_R4, offsetof(struct pt_regs, r4));
	DEFINE(PT_R5, offsetof(struct pt_regs, r5));
	DEFINE(PT_R6, offsetof(struct pt_regs, r6));
	DEFINE(PT_R7, offsetof(struct pt_regs, r7));
	DEFINE(PT_R8, offsetof(struct pt_regs, r8));
	DEFINE(PT_R9, offsetof(struct pt_regs, r9));
	DEFINE(PT_R10, offsetof(struct pt_regs, r10));
	DEFINE(PT_R11, offsetof(struct pt_regs, r11));
	DEFINE(PT_R12, offsetof(struct pt_regs, r12));
	DEFINE(PT_R13, offsetof(struct pt_regs, r13));
	DEFINE(PT_R14, offsetof(struct pt_regs, r14));
	DEFINE(PT_R15, offsetof(struct pt_regs, r15));
	DEFINE(PT_EA, offsetof(struct pt_regs, ea));
	DEFINE(PT_RA, offsetof(struct pt_regs, ra));
	DEFINE(PT_FP, offsetof(struct pt_regs, fp));
	DEFINE(PT_SP, offsetof(struct pt_regs, sp));
	DEFINE(PT_GP, offsetof(struct pt_regs, gp));
	DEFINE(PT_ESTATUS, offsetof(struct pt_regs, estatus));
	DEFINE(PT_STATUS_EXTENSION, offsetof(struct pt_regs, status_extension));
	DEFINE(PT_REGS_SIZE, sizeof(struct pt_regs));

	/* offsets into the switch_stack */
	DEFINE(SW_R16, offsetof(struct switch_stack, r16));
	DEFINE(SW_R17, offsetof(struct switch_stack, r17));
	DEFINE(SW_R18, offsetof(struct switch_stack, r18));
	DEFINE(SW_R19, offsetof(struct switch_stack, r19));
	DEFINE(SW_R20, offsetof(struct switch_stack, r20));
	DEFINE(SW_R21, offsetof(struct switch_stack, r21));
	DEFINE(SW_R22, offsetof(struct switch_stack, r22));
	DEFINE(SW_R23, offsetof(struct switch_stack, r23));
	DEFINE(SW_FP, offsetof(struct switch_stack, fp));
	DEFINE(SW_GP, offsetof(struct switch_stack, gp));
	DEFINE(SW_RA, offsetof(struct switch_stack, ra));
	DEFINE(SWITCH_STACK_SIZE, sizeof(struct switch_stack));

	DEFINE(PS_S_ASM, PS_S);

	DEFINE(NIOS2_STATUS_PIE_MSK_ASM, NIOS2_STATUS_PIE_MSK);  
	DEFINE(NIOS2_STATUS_PIE_OFST_ASM, NIOS2_STATUS_PIE_OFST); 
	DEFINE(NIOS2_STATUS_U_MSK_ASM, NIOS2_STATUS_U_MSK);    
	DEFINE(NIOS2_STATUS_U_OFST_ASM, NIOS2_STATUS_U_OFST);   

	/* offsets into the kernel_stat struct */
	DEFINE(STAT_IRQ, offsetof(struct kernel_stat, irqs));

	/* Offsets in thread_info structure, used in assembly code */
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_EXECDOMAIN, offsetof(struct thread_info, exec_domain));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
	DEFINE(TI_PREEMPT_COUNT, offsetof(struct thread_info, preempt_count));

	DEFINE(PREEMPT_ACTIVE_ASM, PREEMPT_ACTIVE);

	DEFINE(THREAD_SIZE_ASM, THREAD_SIZE);

	DEFINE(TIF_SYSCALL_TRACE_ASM, TIF_SYSCALL_TRACE);
	DEFINE(TIF_NOTIFY_RESUME_ASM, TIF_NOTIFY_RESUME);
	DEFINE(TIF_SIGPENDING_ASM, TIF_SIGPENDING);
	DEFINE(TIF_NEED_RESCHED_ASM, TIF_NEED_RESCHED);
	DEFINE(TIF_POLLING_NRFLAG_ASM, TIF_POLLING_NRFLAG);

	DEFINE(_TIF_SYSCALL_TRACE_ASM, _TIF_SYSCALL_TRACE);
	DEFINE(_TIF_NOTIFY_RESUME_ASM, _TIF_NOTIFY_RESUME);
	DEFINE(_TIF_SIGPENDING_ASM, _TIF_SIGPENDING);
	DEFINE(_TIF_NEED_RESCHED_ASM, _TIF_NEED_RESCHED);
	DEFINE(_TIF_POLLING_NRFLAG_ASM, _TIF_POLLING_NRFLAG);

	DEFINE(_TIF_WORK_MASK_ASM, _TIF_WORK_MASK);

#if defined(na_flash_kernel) && defined(na_flash_kernel_end)
	/* the flash chip */
	DEFINE(NIOS_FLASH_START, na_flash_kernel);
	DEFINE(NIOS_FLASH_END, na_flash_kernel_end);
	
	/* the kernel placement in the flash*/
	DEFINE(KERNEL_FLASH_START, na_flash_kernel);
	DEFINE(KERNEL_FLASH_LEN, 0x200000);
	
	/* the romdisk placement in the flash */
	DEFINE(LINUX_ROMFS_START, na_flash_kernel+0x200000);
	DEFINE(LINUX_ROMFS_END, na_flash_kernel_end);
#else
#error Sorry,you dont have na_flash_kernel or na_flash_kernel_end defined in the core.
#endif
	
#if defined(nasys_program_mem) && defined(nasys_program_mem_end)
	/* the sdram */
	DEFINE(LINUX_SDRAM_START, nasys_program_mem);
	DEFINE(LINUX_SDRAM_END, nasys_program_mem_end);
#else
#error Sorry,you dont have nasys_program_mem or nasys_program_mem_end defined in the core.
#endif	

	DEFINE(NIOS2_ICACHE_SIZE, nasys_icache_size);
	DEFINE(NIOS2_ICACHE_LINE_SIZE, nasys_icache_line_size);
	DEFINE(NIOS2_DCACHE_SIZE, nasys_dcache_size);
	DEFINE(NIOS2_DCACHE_LINE_SIZE, nasys_dcache_line_size);
	
#if defined(na_enet)
	DEFINE(NA_ENET_ASM, na_enet);
#endif	

#if defined(na_enet_reset)
	DEFINE(NA_ENET_RESET_ASM, na_enet_reset);
#endif	

#if defined(na_enet_reset_n)
	DEFINE(NA_ENET_RESET_N_ASM, na_enet_reset_n);
#endif	

#if defined(na_ide_interface)
	DEFINE(NA_IDE_INTERFACE_ASM, na_ide_interface);
#endif	

#if defined(na_timer0)
	DEFINE(NA_TIMER0_ASM, na_timer0);
	DEFINE(NP_TIMERCONTROL_ASM, offsetof(np_timer, np_timercontrol));
	DEFINE(NP_TIMERSTATUS_ASM,  offsetof(np_timer, np_timerstatus));
#endif	

#if defined(na_uart0)
	DEFINE(NA_UART0_ASM, na_uart0);
	DEFINE(NP_UARTCONTROL_ASM,  offsetof(np_uart,  np_uartcontrol));
	DEFINE(NP_UARTSTATUS_ASM,   offsetof(np_uart,  np_uartstatus));
#endif	

#if defined(na_uart1)
	DEFINE(NA_UART1_ASM, na_uart1);
#endif	

#if defined(na_uart2)
	DEFINE(NA_UART2_ASM, na_uart2);
#endif	

#if defined(na_uart3)
	DEFINE(NA_UART3_ASM, na_uart3);
#endif	

	return 0;
}
