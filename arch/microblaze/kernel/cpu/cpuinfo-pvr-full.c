#include <linux/init.h>
#include <linux/string.h>
#include <linux/autoconf.h>
#include <asm/pvr.h>
#include <asm/cpuinfo.h>

/* Helper macro to map between fields in our struct cpuinfo, and
   the PVR macros in pvr.h.
   */
#define CI(c,p) ci->c=PVR_##p(pvr)

void set_cpuinfo_pvr_full(struct cpuinfo *ci)
{
	struct pvr_s pvr;
	get_pvr(&pvr);

	CI(use_barrel,USE_BARREL);
	CI(use_divider,USE_DIV);
	CI(use_mult,USE_HW_MUL);
	CI(use_fpu,USE_FPU);

	CI(use_mul_64,USE_MUL64);
	CI(use_msr_instr,USE_MSR_INSTR);
	CI(use_pcmp_instr, USE_PCMP_INSTR);
	CI(ver_code, VERSION);

	CI(use_icache, USE_ICACHE);
	CI(icache_tagbits, ICACHE_ADDR_TAG_BITS);
	CI(icache_write, ICACHE_ALLOW_WR);
	CI(icache_line, ICACHE_LINE_LEN);
	CI(icache_size, ICACHE_BYTE_SIZE);
	CI(icache_base, ICACHE_BASEADDR);
	CI(icache_high, ICACHE_HIGHADDR);

	CI(use_dcache, USE_DCACHE);
	CI(dcache_tagbits, DCACHE_ADDR_TAG_BITS);
	CI(dcache_write, DCACHE_ALLOW_WR);
	CI(dcache_line, DCACHE_LINE_LEN);
	CI(dcache_size, DCACHE_BYTE_SIZE);
	CI(dcache_base, DCACHE_BASEADDR);
	CI(dcache_high, DCACHE_HIGHADDR);

	CI(use_dopb, D_OPB);
	CI(use_iopb, I_OPB);
	CI(use_dlmb, D_LMB);
	CI(use_ilmb, I_LMB);
	CI(num_fsl, FSL_LINKS);

	CI(irq_edge, INTERRUPT_IS_EDGE);
	CI(irq_positive, EDGE_IS_POSITIVE);

	CI(area_optimised, AREA_OPTIMISED);
	CI(opcode_0_illegal, OPCODE_0x0_ILLEGAL);
	CI(exc_unaligned, UNALIGNED_EXCEPTION);
	CI(exc_ill_opcode, ILL_OPCODE_EXCEPTION);
	CI(exc_iopb, IOPB_BUS_EXCEPTION);
	CI(exc_dopb, DOPB_BUS_EXCEPTION);
	CI(exc_div_zero, DIV_ZERO_EXCEPTION);
	CI(exc_fpu, FPU_EXCEPTION);

	CI(hw_debug, DEBUG_ENABLED);
	CI(num_pc_brk, NUMBER_OF_PC_BRK);
	CI(num_rd_brk, NUMBER_OF_RD_ADDR_BRK);
	CI(num_wr_brk, NUMBER_OF_WR_ADDR_BRK);

	CI(fpga_family_code, TARGET_FAMILY);
}

