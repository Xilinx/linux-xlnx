#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/autoconf.h>
#include <asm/cpuinfo.h>

#define CPUINFO(x) XPAR_MICROBLAZE_0_##x

static struct cpuinfo cpuinfo_static={
	use_barrel	: CPUINFO(USE_BARREL),
	use_divider	: CPUINFO(USE_DIV),
	use_mult	: CPUINFO(USE_HW_MUL)>0,
	use_fpu		: CPUINFO(USE_FPU),
	use_exception	: (CPUINFO(UNALIGNED_EXCEPTIONS) || \
				CPUINFO(ILL_OPCODE_EXCEPTION) ||
				CPUINFO(IOPB_BUS_EXCEPTION) ||
				CPUINFO(DOPB_BUS_EXCEPTION) ||
				CPUINFO(DIV_ZERO_EXCEPTION) ||
				CPUINFO(FPU_EXCEPTION) != 0),
	use_mul_64	: CPUINFO(USE_HW_MUL)==2,
	use_msr_instr	: CPUINFO(USE_MSR_INSTR),
	use_pcmp_instr  : CPUINFO(USE_PCMP_INSTR),
	ver_code	: -1,
	
	use_icache	: CPUINFO(USE_ICACHE),
	icache_tagbits	: CPUINFO(ADDR_TAG_BITS),
	icache_write	: CPUINFO(ALLOW_ICACHE_WR),
#if XPAR_MICROBLAZE_0_ICACHE_USE_FSL==1
	icache_line	: 16,
#else
	icache_line	: 4,
#endif
	icache_size	: CPUINFO(CACHE_BYTE_SIZE),
	icache_base	: CPUINFO(ICACHE_BASEADDR),
	icache_high	: CPUINFO(ICACHE_HIGHADDR),

	use_dcache	: CPUINFO(USE_DCACHE),
	dcache_tagbits	: CPUINFO(DCACHE_ADDR_TAG),
	dcache_write	: CPUINFO(ALLOW_DCACHE_WR),
#if XPAR_MICROBLAZE_0_DCACHE_USE_FSL==1
	dcache_line	: 16,
#else
	dcache_line	: 4,
#endif
	dcache_size	: CPUINFO(DCACHE_BYTE_SIZE),
	dcache_base	: CPUINFO(DCACHE_BASEADDR),
	dcache_high	: CPUINFO(DCACHE_HIGHADDR),

	/* Bus connections */
	use_dopb	: CPUINFO(D_OPB),
	use_iopb	: CPUINFO(I_OPB),
	use_dlmb	: CPUINFO(D_LMB),
	use_ilmb	: CPUINFO(I_LMB),
	num_fsl		: CPUINFO(FSL_LINKS),

	/* CPU interrupt line info */
	irq_edge	: CPUINFO(INTERRUPT_IS_EDGE),
	irq_positive	: CPUINFO(EDGE_IS_POSITIVE),

	area_optimised	: -1,

	/* HW support for CPU exceptions */
	opcode_0_illegal: -1,
	exc_unaligned	: CPUINFO(UNALIGNED_EXCEPTIONS),
	exc_ill_opcode	: CPUINFO(ILL_OPCODE_EXCEPTION),
	exc_iopb	: CPUINFO(IOPB_BUS_EXCEPTION),
	exc_dopb	: CPUINFO(DOPB_BUS_EXCEPTION),
	exc_div_zero	: CPUINFO(DIV_ZERO_EXCEPTION),
	exc_fpu		: CPUINFO(FPU_EXCEPTION),

	/* HW debug support */
	hw_debug	: CPUINFO(DEBUG_ENABLED),
// -wgr- 	num_pc_brk	: CPUINFO(NUMBER_OF_PC_BRK),
	num_rd_brk	: CPUINFO(NUMBER_OF_RD_ADDR_BRK),
	num_wr_brk	: CPUINFO(NUMBER_OF_WR_ADDR_BRK),

	/* FPGA family */
	fpga_family_code: -1,
};

void __init set_cpuinfo_static(struct cpuinfo *ci)
{
	printk(KERN_INFO "set_cpuinfo_static: Using static CPU info.\n");
	/* Copy our static CPUINFO descriptor into place */
	memcpy(ci,&cpuinfo_static,sizeof(struct cpuinfo));
}

