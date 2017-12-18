/*
 * Contains CPU feature definitions
 *
 * Copyright (C) 2015 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "CPU features: " fmt

#include <linux/bsearch.h>
#include <linux/cpumask.h>
#include <linux/sort.h>
#include <linux/stop_machine.h>
#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cpufeature.h>
#include <asm/cpu_ops.h>
#include <asm/mmu_context.h>
#include <asm/processor.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

unsigned long elf_hwcap __read_mostly;
EXPORT_SYMBOL_GPL(elf_hwcap);

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP_DEFAULT	\
				(COMPAT_HWCAP_HALF|COMPAT_HWCAP_THUMB|\
				 COMPAT_HWCAP_FAST_MULT|COMPAT_HWCAP_EDSP|\
				 COMPAT_HWCAP_TLS|COMPAT_HWCAP_VFP|\
				 COMPAT_HWCAP_VFPv3|COMPAT_HWCAP_VFPv4|\
				 COMPAT_HWCAP_NEON|COMPAT_HWCAP_IDIV|\
				 COMPAT_HWCAP_LPAE)
unsigned int compat_elf_hwcap __read_mostly = COMPAT_ELF_HWCAP_DEFAULT;
unsigned int compat_elf_hwcap2 __read_mostly;
#endif

DECLARE_BITMAP(cpu_hwcaps, ARM64_NCAPS);

DEFINE_STATIC_KEY_ARRAY_FALSE(cpu_hwcap_keys, ARM64_NCAPS);
EXPORT_SYMBOL(cpu_hwcap_keys);

#define __ARM64_FTR_BITS(SIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	{						\
		.sign = SIGNED,				\
		.strict = STRICT,			\
		.type = TYPE,				\
		.shift = SHIFT,				\
		.width = WIDTH,				\
		.safe_val = SAFE_VAL,			\
	}

/* Define a feature with unsigned values */
#define ARM64_FTR_BITS(STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_UNSIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

/* Define a feature with a signed value */
#define S_ARM64_FTR_BITS(STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_SIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

#define ARM64_FTR_END					\
	{						\
		.width = 0,				\
	}

/* meta feature for alternatives */
static bool __maybe_unused
cpufeature_pan_not_uao(const struct arm64_cpu_capabilities *entry, int __unused);


static const struct arm64_ftr_bits ftr_id_aa64isar0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64ISAR0_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 24, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_ATOMICS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),	/* RAZ */
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64pfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_GIC_SHIFT, 4, 0),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_ASIMD_SHIFT, 4, ID_AA64PFR0_ASIMD_NI),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_FP_SHIFT, 4, ID_AA64PFR0_FP_NI),
	/* Linux doesn't care about the EL3 */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, ID_AA64PFR0_EL3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL1_SHIFT, 4, ID_AA64PFR0_EL1_64BIT_ONLY),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL0_SHIFT, 4, ID_AA64PFR0_EL0_64BIT_ONLY),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN4_SHIFT, 4, ID_AA64MMFR0_TGRAN4_NI),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN64_SHIFT, 4, ID_AA64MMFR0_TGRAN64_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN16_SHIFT, 4, ID_AA64MMFR0_TGRAN16_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_BIGENDEL0_SHIFT, 4, 0),
	/* Linux shouldn't care about secure memory */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, ID_AA64MMFR0_SNSMEM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_BIGENDEL_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_ASID_SHIFT, 4, 0),
	/*
	 * Differing PARange is fine as long as all peripherals and memory are mapped
	 * within the minimum PARange of all CPUs
	 */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_PARANGE_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr1[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_PAN_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_LOR_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_HPD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_VHE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_VMIDBITS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_HADBS_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64mmfr2[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR2_LVA_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR2_IESB_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR2_LSM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR2_UAO_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR2_CNP_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_ctr[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 31, 1, 1),	/* RAO */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 3, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_HIGHER_SAFE, 24, 4, 0),	/* CWG */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),	/* ERG */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 1),	/* DminLine */
	/*
	 * Linux can handle differing I-cache policies. Userspace JITs will
	 * make use of *minLine.
	 * If we have differing I-cache policies, report it as the weakest - AIVIVT.
	 */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, 14, 2, ICACHE_POLICY_AIVIVT),	/* L1Ip */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 10, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),	/* IminLine */
	ARM64_FTR_END,
};

struct arm64_ftr_reg arm64_ftr_reg_ctrel0 = {
	.name		= "SYS_CTR_EL0",
	.ftr_bits	= ftr_ctr
};

static const struct arm64_ftr_bits ftr_id_mmfr0[] = {
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 4, 0xf),	/* InnerShr */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 24, 4, 0),	/* FCSE */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_LOWER_SAFE, 20, 4, 0),	/* AuxReg */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 16, 4, 0),	/* TCM */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 12, 4, 0),	/* ShareLvl */
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 4, 0xf),	/* OuterShr */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),	/* PMSA */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),	/* VMSA */
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_aa64dfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_CTX_CMPS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_WRPS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_BRPS_SHIFT, 4, 0),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_PMUVER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_TRACEVER_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_DEBUGVER_SHIFT, 4, 0x6),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_mvfr2[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 24, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* FPMisc */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* SIMDMisc */
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_dczid[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 5, 27, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 1, 1),		/* DZP */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),	/* BS */
	ARM64_FTR_END,
};


static const struct arm64_ftr_bits ftr_id_isar5[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 20, 4, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SEVL_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_mmfr4[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 24, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* ac2 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* RAZ */
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_pfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 16, 16, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 12, 4, 0),	/* State3 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 4, 0),		/* State2 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* State1 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* State0 */
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_id_dfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 28, 4, 0),
	S_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 24, 4, 0xf),	/* PerfMon */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 12, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 8, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 4, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),
	ARM64_FTR_END,
};

/*
 * Common ftr bits for a 32bit register with all hidden, strict
 * attributes, with 4bit feature fields and a default safe value of
 * 0. Covers the following 32bit registers:
 * id_isar[0-4], id_mmfr[1-3], id_pfr1, mvfr[0-1]
 */
static const struct arm64_ftr_bits ftr_generic_32bits[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 28, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 24, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 12, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 8, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 4, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_generic[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 64, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_generic32[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 32, 0),
	ARM64_FTR_END,
};

static const struct arm64_ftr_bits ftr_aa64raz[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 64, 0),
	ARM64_FTR_END,
};

#define ARM64_FTR_REG(id, table) {		\
	.sys_id = id,				\
	.reg = 	&(struct arm64_ftr_reg){	\
		.name = #id,			\
		.ftr_bits = &((table)[0]),	\
	}}

static const struct __ftr_reg_entry {
	u32			sys_id;
	struct arm64_ftr_reg 	*reg;
} arm64_ftr_regs[] = {

	/* Op1 = 0, CRn = 0, CRm = 1 */
	ARM64_FTR_REG(SYS_ID_PFR0_EL1, ftr_id_pfr0),
	ARM64_FTR_REG(SYS_ID_PFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_DFR0_EL1, ftr_id_dfr0),
	ARM64_FTR_REG(SYS_ID_MMFR0_EL1, ftr_id_mmfr0),
	ARM64_FTR_REG(SYS_ID_MMFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_MMFR2_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_MMFR3_EL1, ftr_generic_32bits),

	/* Op1 = 0, CRn = 0, CRm = 2 */
	ARM64_FTR_REG(SYS_ID_ISAR0_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR2_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR3_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR4_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR5_EL1, ftr_id_isar5),
	ARM64_FTR_REG(SYS_ID_MMFR4_EL1, ftr_id_mmfr4),

	/* Op1 = 0, CRn = 0, CRm = 3 */
	ARM64_FTR_REG(SYS_MVFR0_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_MVFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_MVFR2_EL1, ftr_mvfr2),

	/* Op1 = 0, CRn = 0, CRm = 4 */
	ARM64_FTR_REG(SYS_ID_AA64PFR0_EL1, ftr_id_aa64pfr0),
	ARM64_FTR_REG(SYS_ID_AA64PFR1_EL1, ftr_aa64raz),

	/* Op1 = 0, CRn = 0, CRm = 5 */
	ARM64_FTR_REG(SYS_ID_AA64DFR0_EL1, ftr_id_aa64dfr0),
	ARM64_FTR_REG(SYS_ID_AA64DFR1_EL1, ftr_generic),

	/* Op1 = 0, CRn = 0, CRm = 6 */
	ARM64_FTR_REG(SYS_ID_AA64ISAR0_EL1, ftr_id_aa64isar0),
	ARM64_FTR_REG(SYS_ID_AA64ISAR1_EL1, ftr_aa64raz),

	/* Op1 = 0, CRn = 0, CRm = 7 */
	ARM64_FTR_REG(SYS_ID_AA64MMFR0_EL1, ftr_id_aa64mmfr0),
	ARM64_FTR_REG(SYS_ID_AA64MMFR1_EL1, ftr_id_aa64mmfr1),
	ARM64_FTR_REG(SYS_ID_AA64MMFR2_EL1, ftr_id_aa64mmfr2),

	/* Op1 = 3, CRn = 0, CRm = 0 */
	{ SYS_CTR_EL0, &arm64_ftr_reg_ctrel0 },
	ARM64_FTR_REG(SYS_DCZID_EL0, ftr_dczid),

	/* Op1 = 3, CRn = 14, CRm = 0 */
	ARM64_FTR_REG(SYS_CNTFRQ_EL0, ftr_generic32),
};

static int search_cmp_ftr_reg(const void *id, const void *regp)
{
	return (int)(unsigned long)id - (int)((const struct __ftr_reg_entry *)regp)->sys_id;
}

/*
 * get_arm64_ftr_reg - Lookup a feature register entry using its
 * sys_reg() encoding. With the array arm64_ftr_regs sorted in the
 * ascending order of sys_id , we use binary search to find a matching
 * entry.
 *
 * returns - Upon success,  matching ftr_reg entry for id.
 *         - NULL on failure. It is upto the caller to decide
 *	     the impact of a failure.
 */
static struct arm64_ftr_reg *get_arm64_ftr_reg(u32 sys_id)
{
	const struct __ftr_reg_entry *ret;

	ret = bsearch((const void *)(unsigned long)sys_id,
			arm64_ftr_regs,
			ARRAY_SIZE(arm64_ftr_regs),
			sizeof(arm64_ftr_regs[0]),
			search_cmp_ftr_reg);
	if (ret)
		return ret->reg;
	return NULL;
}

static u64 arm64_ftr_set_value(const struct arm64_ftr_bits *ftrp, s64 reg,
			       s64 ftr_val)
{
	u64 mask = arm64_ftr_mask(ftrp);

	reg &= ~mask;
	reg |= (ftr_val << ftrp->shift) & mask;
	return reg;
}

static s64 arm64_ftr_safe_value(const struct arm64_ftr_bits *ftrp, s64 new,
				s64 cur)
{
	s64 ret = 0;

	switch (ftrp->type) {
	case FTR_EXACT:
		ret = ftrp->safe_val;
		break;
	case FTR_LOWER_SAFE:
		ret = new < cur ? new : cur;
		break;
	case FTR_HIGHER_SAFE:
		ret = new > cur ? new : cur;
		break;
	default:
		BUG();
	}

	return ret;
}

static void __init sort_ftr_regs(void)
{
	int i;

	/* Check that the array is sorted so that we can do the binary search */
	for (i = 1; i < ARRAY_SIZE(arm64_ftr_regs); i++)
		BUG_ON(arm64_ftr_regs[i].sys_id < arm64_ftr_regs[i - 1].sys_id);
}

/*
 * Initialise the CPU feature register from Boot CPU values.
 * Also initiliases the strict_mask for the register.
 */
static void __init init_cpu_ftr_reg(u32 sys_reg, u64 new)
{
	u64 val = 0;
	u64 strict_mask = ~0x0ULL;
	const struct arm64_ftr_bits *ftrp;
	struct arm64_ftr_reg *reg = get_arm64_ftr_reg(sys_reg);

	BUG_ON(!reg);

	for (ftrp  = reg->ftr_bits; ftrp->width; ftrp++) {
		s64 ftr_new = arm64_ftr_value(ftrp, new);

		val = arm64_ftr_set_value(ftrp, val, ftr_new);
		if (!ftrp->strict)
			strict_mask &= ~arm64_ftr_mask(ftrp);
	}
	reg->sys_val = val;
	reg->strict_mask = strict_mask;
}

void __init init_cpu_features(struct cpuinfo_arm64 *info)
{
	/* Before we start using the tables, make sure it is sorted */
	sort_ftr_regs();

	init_cpu_ftr_reg(SYS_CTR_EL0, info->reg_ctr);
	init_cpu_ftr_reg(SYS_DCZID_EL0, info->reg_dczid);
	init_cpu_ftr_reg(SYS_CNTFRQ_EL0, info->reg_cntfrq);
	init_cpu_ftr_reg(SYS_ID_AA64DFR0_EL1, info->reg_id_aa64dfr0);
	init_cpu_ftr_reg(SYS_ID_AA64DFR1_EL1, info->reg_id_aa64dfr1);
	init_cpu_ftr_reg(SYS_ID_AA64ISAR0_EL1, info->reg_id_aa64isar0);
	init_cpu_ftr_reg(SYS_ID_AA64ISAR1_EL1, info->reg_id_aa64isar1);
	init_cpu_ftr_reg(SYS_ID_AA64MMFR0_EL1, info->reg_id_aa64mmfr0);
	init_cpu_ftr_reg(SYS_ID_AA64MMFR1_EL1, info->reg_id_aa64mmfr1);
	init_cpu_ftr_reg(SYS_ID_AA64MMFR2_EL1, info->reg_id_aa64mmfr2);
	init_cpu_ftr_reg(SYS_ID_AA64PFR0_EL1, info->reg_id_aa64pfr0);
	init_cpu_ftr_reg(SYS_ID_AA64PFR1_EL1, info->reg_id_aa64pfr1);

	if (id_aa64pfr0_32bit_el0(info->reg_id_aa64pfr0)) {
		init_cpu_ftr_reg(SYS_ID_DFR0_EL1, info->reg_id_dfr0);
		init_cpu_ftr_reg(SYS_ID_ISAR0_EL1, info->reg_id_isar0);
		init_cpu_ftr_reg(SYS_ID_ISAR1_EL1, info->reg_id_isar1);
		init_cpu_ftr_reg(SYS_ID_ISAR2_EL1, info->reg_id_isar2);
		init_cpu_ftr_reg(SYS_ID_ISAR3_EL1, info->reg_id_isar3);
		init_cpu_ftr_reg(SYS_ID_ISAR4_EL1, info->reg_id_isar4);
		init_cpu_ftr_reg(SYS_ID_ISAR5_EL1, info->reg_id_isar5);
		init_cpu_ftr_reg(SYS_ID_MMFR0_EL1, info->reg_id_mmfr0);
		init_cpu_ftr_reg(SYS_ID_MMFR1_EL1, info->reg_id_mmfr1);
		init_cpu_ftr_reg(SYS_ID_MMFR2_EL1, info->reg_id_mmfr2);
		init_cpu_ftr_reg(SYS_ID_MMFR3_EL1, info->reg_id_mmfr3);
		init_cpu_ftr_reg(SYS_ID_PFR0_EL1, info->reg_id_pfr0);
		init_cpu_ftr_reg(SYS_ID_PFR1_EL1, info->reg_id_pfr1);
		init_cpu_ftr_reg(SYS_MVFR0_EL1, info->reg_mvfr0);
		init_cpu_ftr_reg(SYS_MVFR1_EL1, info->reg_mvfr1);
		init_cpu_ftr_reg(SYS_MVFR2_EL1, info->reg_mvfr2);
	}

}

static void update_cpu_ftr_reg(struct arm64_ftr_reg *reg, u64 new)
{
	const struct arm64_ftr_bits *ftrp;

	for (ftrp = reg->ftr_bits; ftrp->width; ftrp++) {
		s64 ftr_cur = arm64_ftr_value(ftrp, reg->sys_val);
		s64 ftr_new = arm64_ftr_value(ftrp, new);

		if (ftr_cur == ftr_new)
			continue;
		/* Find a safe value */
		ftr_new = arm64_ftr_safe_value(ftrp, ftr_new, ftr_cur);
		reg->sys_val = arm64_ftr_set_value(ftrp, reg->sys_val, ftr_new);
	}

}

static int check_update_ftr_reg(u32 sys_id, int cpu, u64 val, u64 boot)
{
	struct arm64_ftr_reg *regp = get_arm64_ftr_reg(sys_id);

	BUG_ON(!regp);
	update_cpu_ftr_reg(regp, val);
	if ((boot & regp->strict_mask) == (val & regp->strict_mask))
		return 0;
	pr_warn("SANITY CHECK: Unexpected variation in %s. Boot CPU: %#016llx, CPU%d: %#016llx\n",
			regp->name, boot, cpu, val);
	return 1;
}

/*
 * Update system wide CPU feature registers with the values from a
 * non-boot CPU. Also performs SANITY checks to make sure that there
 * aren't any insane variations from that of the boot CPU.
 */
void update_cpu_features(int cpu,
			 struct cpuinfo_arm64 *info,
			 struct cpuinfo_arm64 *boot)
{
	int taint = 0;

	/*
	 * The kernel can handle differing I-cache policies, but otherwise
	 * caches should look identical. Userspace JITs will make use of
	 * *minLine.
	 */
	taint |= check_update_ftr_reg(SYS_CTR_EL0, cpu,
				      info->reg_ctr, boot->reg_ctr);

	/*
	 * Userspace may perform DC ZVA instructions. Mismatched block sizes
	 * could result in too much or too little memory being zeroed if a
	 * process is preempted and migrated between CPUs.
	 */
	taint |= check_update_ftr_reg(SYS_DCZID_EL0, cpu,
				      info->reg_dczid, boot->reg_dczid);

	/* If different, timekeeping will be broken (especially with KVM) */
	taint |= check_update_ftr_reg(SYS_CNTFRQ_EL0, cpu,
				      info->reg_cntfrq, boot->reg_cntfrq);

	/*
	 * The kernel uses self-hosted debug features and expects CPUs to
	 * support identical debug features. We presently need CTX_CMPs, WRPs,
	 * and BRPs to be identical.
	 * ID_AA64DFR1 is currently RES0.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64DFR0_EL1, cpu,
				      info->reg_id_aa64dfr0, boot->reg_id_aa64dfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64DFR1_EL1, cpu,
				      info->reg_id_aa64dfr1, boot->reg_id_aa64dfr1);
	/*
	 * Even in big.LITTLE, processors should be identical instruction-set
	 * wise.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64ISAR0_EL1, cpu,
				      info->reg_id_aa64isar0, boot->reg_id_aa64isar0);
	taint |= check_update_ftr_reg(SYS_ID_AA64ISAR1_EL1, cpu,
				      info->reg_id_aa64isar1, boot->reg_id_aa64isar1);

	/*
	 * Differing PARange support is fine as long as all peripherals and
	 * memory are mapped within the minimum PARange of all CPUs.
	 * Linux should not care about secure memory.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64MMFR0_EL1, cpu,
				      info->reg_id_aa64mmfr0, boot->reg_id_aa64mmfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64MMFR1_EL1, cpu,
				      info->reg_id_aa64mmfr1, boot->reg_id_aa64mmfr1);
	taint |= check_update_ftr_reg(SYS_ID_AA64MMFR2_EL1, cpu,
				      info->reg_id_aa64mmfr2, boot->reg_id_aa64mmfr2);

	/*
	 * EL3 is not our concern.
	 * ID_AA64PFR1 is currently RES0.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64PFR0_EL1, cpu,
				      info->reg_id_aa64pfr0, boot->reg_id_aa64pfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64PFR1_EL1, cpu,
				      info->reg_id_aa64pfr1, boot->reg_id_aa64pfr1);

	/*
	 * If we have AArch32, we care about 32-bit features for compat.
	 * If the system doesn't support AArch32, don't update them.
	 */
	if (id_aa64pfr0_32bit_el0(read_system_reg(SYS_ID_AA64PFR0_EL1)) &&
		id_aa64pfr0_32bit_el0(info->reg_id_aa64pfr0)) {

		taint |= check_update_ftr_reg(SYS_ID_DFR0_EL1, cpu,
					info->reg_id_dfr0, boot->reg_id_dfr0);
		taint |= check_update_ftr_reg(SYS_ID_ISAR0_EL1, cpu,
					info->reg_id_isar0, boot->reg_id_isar0);
		taint |= check_update_ftr_reg(SYS_ID_ISAR1_EL1, cpu,
					info->reg_id_isar1, boot->reg_id_isar1);
		taint |= check_update_ftr_reg(SYS_ID_ISAR2_EL1, cpu,
					info->reg_id_isar2, boot->reg_id_isar2);
		taint |= check_update_ftr_reg(SYS_ID_ISAR3_EL1, cpu,
					info->reg_id_isar3, boot->reg_id_isar3);
		taint |= check_update_ftr_reg(SYS_ID_ISAR4_EL1, cpu,
					info->reg_id_isar4, boot->reg_id_isar4);
		taint |= check_update_ftr_reg(SYS_ID_ISAR5_EL1, cpu,
					info->reg_id_isar5, boot->reg_id_isar5);

		/*
		 * Regardless of the value of the AuxReg field, the AIFSR, ADFSR, and
		 * ACTLR formats could differ across CPUs and therefore would have to
		 * be trapped for virtualization anyway.
		 */
		taint |= check_update_ftr_reg(SYS_ID_MMFR0_EL1, cpu,
					info->reg_id_mmfr0, boot->reg_id_mmfr0);
		taint |= check_update_ftr_reg(SYS_ID_MMFR1_EL1, cpu,
					info->reg_id_mmfr1, boot->reg_id_mmfr1);
		taint |= check_update_ftr_reg(SYS_ID_MMFR2_EL1, cpu,
					info->reg_id_mmfr2, boot->reg_id_mmfr2);
		taint |= check_update_ftr_reg(SYS_ID_MMFR3_EL1, cpu,
					info->reg_id_mmfr3, boot->reg_id_mmfr3);
		taint |= check_update_ftr_reg(SYS_ID_PFR0_EL1, cpu,
					info->reg_id_pfr0, boot->reg_id_pfr0);
		taint |= check_update_ftr_reg(SYS_ID_PFR1_EL1, cpu,
					info->reg_id_pfr1, boot->reg_id_pfr1);
		taint |= check_update_ftr_reg(SYS_MVFR0_EL1, cpu,
					info->reg_mvfr0, boot->reg_mvfr0);
		taint |= check_update_ftr_reg(SYS_MVFR1_EL1, cpu,
					info->reg_mvfr1, boot->reg_mvfr1);
		taint |= check_update_ftr_reg(SYS_MVFR2_EL1, cpu,
					info->reg_mvfr2, boot->reg_mvfr2);
	}

	/*
	 * Mismatched CPU features are a recipe for disaster. Don't even
	 * pretend to support them.
	 */
	WARN_TAINT_ONCE(taint, TAINT_CPU_OUT_OF_SPEC,
			"Unsupported CPU feature variation.\n");
}

u64 read_system_reg(u32 id)
{
	struct arm64_ftr_reg *regp = get_arm64_ftr_reg(id);

	/* We shouldn't get a request for an unsupported register */
	BUG_ON(!regp);
	return regp->sys_val;
}

/*
 * __raw_read_system_reg() - Used by a STARTING cpu before cpuinfo is populated.
 * Read the system register on the current CPU
 */
static u64 __raw_read_system_reg(u32 sys_id)
{
	switch (sys_id) {
	case SYS_ID_PFR0_EL1:		return read_cpuid(ID_PFR0_EL1);
	case SYS_ID_PFR1_EL1:		return read_cpuid(ID_PFR1_EL1);
	case SYS_ID_DFR0_EL1:		return read_cpuid(ID_DFR0_EL1);
	case SYS_ID_MMFR0_EL1:		return read_cpuid(ID_MMFR0_EL1);
	case SYS_ID_MMFR1_EL1:		return read_cpuid(ID_MMFR1_EL1);
	case SYS_ID_MMFR2_EL1:		return read_cpuid(ID_MMFR2_EL1);
	case SYS_ID_MMFR3_EL1:		return read_cpuid(ID_MMFR3_EL1);
	case SYS_ID_ISAR0_EL1:		return read_cpuid(ID_ISAR0_EL1);
	case SYS_ID_ISAR1_EL1:		return read_cpuid(ID_ISAR1_EL1);
	case SYS_ID_ISAR2_EL1:		return read_cpuid(ID_ISAR2_EL1);
	case SYS_ID_ISAR3_EL1:		return read_cpuid(ID_ISAR3_EL1);
	case SYS_ID_ISAR4_EL1:		return read_cpuid(ID_ISAR4_EL1);
	case SYS_ID_ISAR5_EL1:		return read_cpuid(ID_ISAR4_EL1);
	case SYS_MVFR0_EL1:		return read_cpuid(MVFR0_EL1);
	case SYS_MVFR1_EL1:		return read_cpuid(MVFR1_EL1);
	case SYS_MVFR2_EL1:		return read_cpuid(MVFR2_EL1);

	case SYS_ID_AA64PFR0_EL1:	return read_cpuid(ID_AA64PFR0_EL1);
	case SYS_ID_AA64PFR1_EL1:	return read_cpuid(ID_AA64PFR0_EL1);
	case SYS_ID_AA64DFR0_EL1:	return read_cpuid(ID_AA64DFR0_EL1);
	case SYS_ID_AA64DFR1_EL1:	return read_cpuid(ID_AA64DFR0_EL1);
	case SYS_ID_AA64MMFR0_EL1:	return read_cpuid(ID_AA64MMFR0_EL1);
	case SYS_ID_AA64MMFR1_EL1:	return read_cpuid(ID_AA64MMFR1_EL1);
	case SYS_ID_AA64MMFR2_EL1:	return read_cpuid(ID_AA64MMFR2_EL1);
	case SYS_ID_AA64ISAR0_EL1:	return read_cpuid(ID_AA64ISAR0_EL1);
	case SYS_ID_AA64ISAR1_EL1:	return read_cpuid(ID_AA64ISAR1_EL1);

	case SYS_CNTFRQ_EL0:		return read_cpuid(CNTFRQ_EL0);
	case SYS_CTR_EL0:		return read_cpuid(CTR_EL0);
	case SYS_DCZID_EL0:		return read_cpuid(DCZID_EL0);
	default:
		BUG();
		return 0;
	}
}

#include <linux/irqchip/arm-gic-v3.h>

static bool
feature_matches(u64 reg, const struct arm64_cpu_capabilities *entry)
{
	int val = cpuid_feature_extract_field(reg, entry->field_pos, entry->sign);

	return val >= entry->min_field_value;
}

static bool
has_cpuid_feature(const struct arm64_cpu_capabilities *entry, int scope)
{
	u64 val;

	WARN_ON(scope == SCOPE_LOCAL_CPU && preemptible());
	if (scope == SCOPE_SYSTEM)
		val = read_system_reg(entry->sys_reg);
	else
		val = __raw_read_system_reg(entry->sys_reg);

	return feature_matches(val, entry);
}

static bool has_useable_gicv3_cpuif(const struct arm64_cpu_capabilities *entry, int scope)
{
	bool has_sre;

	if (!has_cpuid_feature(entry, scope))
		return false;

	has_sre = gic_enable_sre();
	if (!has_sre)
		pr_warn_once("%s present but disabled by higher exception level\n",
			     entry->desc);

	return has_sre;
}

static bool has_no_hw_prefetch(const struct arm64_cpu_capabilities *entry, int __unused)
{
	u32 midr = read_cpuid_id();
	u32 rv_min, rv_max;

	/* Cavium ThunderX pass 1.x and 2.x */
	rv_min = 0;
	rv_max = (1 << MIDR_VARIANT_SHIFT) | MIDR_REVISION_MASK;

	return MIDR_IS_CPU_MODEL_RANGE(midr, MIDR_THUNDERX, rv_min, rv_max);
}

static bool runs_at_el2(const struct arm64_cpu_capabilities *entry, int __unused)
{
	return is_kernel_in_hyp_mode();
}

static bool hyp_offset_low(const struct arm64_cpu_capabilities *entry,
			   int __unused)
{
	phys_addr_t idmap_addr = virt_to_phys(__hyp_idmap_text_start);

	/*
	 * Activate the lower HYP offset only if:
	 * - the idmap doesn't clash with it,
	 * - the kernel is not running at EL2.
	 */
	return idmap_addr > GENMASK(VA_BITS - 2, 0) && !is_kernel_in_hyp_mode();
}

static const struct arm64_cpu_capabilities arm64_features[] = {
	{
		.desc = "GIC system register CPU interface",
		.capability = ARM64_HAS_SYSREG_GIC_CPUIF,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_useable_gicv3_cpuif,
		.sys_reg = SYS_ID_AA64PFR0_EL1,
		.field_pos = ID_AA64PFR0_GIC_SHIFT,
		.sign = FTR_UNSIGNED,
		.min_field_value = 1,
	},
#ifdef CONFIG_ARM64_PAN
	{
		.desc = "Privileged Access Never",
		.capability = ARM64_HAS_PAN,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64MMFR1_EL1,
		.field_pos = ID_AA64MMFR1_PAN_SHIFT,
		.sign = FTR_UNSIGNED,
		.min_field_value = 1,
		.enable = cpu_enable_pan,
	},
#endif /* CONFIG_ARM64_PAN */
#if defined(CONFIG_AS_LSE) && defined(CONFIG_ARM64_LSE_ATOMICS)
	{
		.desc = "LSE atomic instructions",
		.capability = ARM64_HAS_LSE_ATOMICS,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64ISAR0_EL1,
		.field_pos = ID_AA64ISAR0_ATOMICS_SHIFT,
		.sign = FTR_UNSIGNED,
		.min_field_value = 2,
	},
#endif /* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */
	{
		.desc = "Software prefetching using PRFM",
		.capability = ARM64_HAS_NO_HW_PREFETCH,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_no_hw_prefetch,
	},
#ifdef CONFIG_ARM64_UAO
	{
		.desc = "User Access Override",
		.capability = ARM64_HAS_UAO,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64MMFR2_EL1,
		.field_pos = ID_AA64MMFR2_UAO_SHIFT,
		.min_field_value = 1,
		.enable = cpu_enable_uao,
	},
#endif /* CONFIG_ARM64_UAO */
#ifdef CONFIG_ARM64_PAN
	{
		.capability = ARM64_ALT_PAN_NOT_UAO,
		.def_scope = SCOPE_SYSTEM,
		.matches = cpufeature_pan_not_uao,
	},
#endif /* CONFIG_ARM64_PAN */
	{
		.desc = "Virtualization Host Extensions",
		.capability = ARM64_HAS_VIRT_HOST_EXTN,
		.def_scope = SCOPE_SYSTEM,
		.matches = runs_at_el2,
	},
	{
		.desc = "32-bit EL0 Support",
		.capability = ARM64_HAS_32BIT_EL0,
		.def_scope = SCOPE_SYSTEM,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64PFR0_EL1,
		.sign = FTR_UNSIGNED,
		.field_pos = ID_AA64PFR0_EL0_SHIFT,
		.min_field_value = ID_AA64PFR0_EL0_32BIT_64BIT,
	},
	{
		.desc = "Reduced HYP mapping offset",
		.capability = ARM64_HYP_OFFSET_LOW,
		.def_scope = SCOPE_SYSTEM,
		.matches = hyp_offset_low,
	},
	{},
};

#define HWCAP_CAP(reg, field, s, min_value, type, cap)	\
	{							\
		.desc = #cap,					\
		.def_scope = SCOPE_SYSTEM,			\
		.matches = has_cpuid_feature,			\
		.sys_reg = reg,					\
		.field_pos = field,				\
		.sign = s,					\
		.min_field_value = min_value,			\
		.hwcap_type = type,				\
		.hwcap = cap,					\
	}

static const struct arm64_cpu_capabilities arm64_elf_hwcaps[] = {
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_AES_SHIFT, FTR_UNSIGNED, 2, CAP_HWCAP, HWCAP_PMULL),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_AES_SHIFT, FTR_UNSIGNED, 1, CAP_HWCAP, HWCAP_AES),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_SHA1_SHIFT, FTR_UNSIGNED, 1, CAP_HWCAP, HWCAP_SHA1),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_SHA2_SHIFT, FTR_UNSIGNED, 1, CAP_HWCAP, HWCAP_SHA2),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_CRC32_SHIFT, FTR_UNSIGNED, 1, CAP_HWCAP, HWCAP_CRC32),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_ATOMICS_SHIFT, FTR_UNSIGNED, 2, CAP_HWCAP, HWCAP_ATOMICS),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_FP_SHIFT, FTR_SIGNED, 0, CAP_HWCAP, HWCAP_FP),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_FP_SHIFT, FTR_SIGNED, 1, CAP_HWCAP, HWCAP_FPHP),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_ASIMD_SHIFT, FTR_SIGNED, 0, CAP_HWCAP, HWCAP_ASIMD),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_ASIMD_SHIFT, FTR_SIGNED, 1, CAP_HWCAP, HWCAP_ASIMDHP),
	{},
};

static const struct arm64_cpu_capabilities compat_elf_hwcaps[] = {
#ifdef CONFIG_COMPAT
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_AES_SHIFT, FTR_UNSIGNED, 2, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_PMULL),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_AES_SHIFT, FTR_UNSIGNED, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_AES),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_SHA1_SHIFT, FTR_UNSIGNED, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_SHA1),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_SHA2_SHIFT, FTR_UNSIGNED, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_SHA2),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_CRC32_SHIFT, FTR_UNSIGNED, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_CRC32),
#endif
	{},
};

static void __init cap_set_elf_hwcap(const struct arm64_cpu_capabilities *cap)
{
	switch (cap->hwcap_type) {
	case CAP_HWCAP:
		elf_hwcap |= cap->hwcap;
		break;
#ifdef CONFIG_COMPAT
	case CAP_COMPAT_HWCAP:
		compat_elf_hwcap |= (u32)cap->hwcap;
		break;
	case CAP_COMPAT_HWCAP2:
		compat_elf_hwcap2 |= (u32)cap->hwcap;
		break;
#endif
	default:
		WARN_ON(1);
		break;
	}
}

/* Check if we have a particular HWCAP enabled */
static bool cpus_have_elf_hwcap(const struct arm64_cpu_capabilities *cap)
{
	bool rc;

	switch (cap->hwcap_type) {
	case CAP_HWCAP:
		rc = (elf_hwcap & cap->hwcap) != 0;
		break;
#ifdef CONFIG_COMPAT
	case CAP_COMPAT_HWCAP:
		rc = (compat_elf_hwcap & (u32)cap->hwcap) != 0;
		break;
	case CAP_COMPAT_HWCAP2:
		rc = (compat_elf_hwcap2 & (u32)cap->hwcap) != 0;
		break;
#endif
	default:
		WARN_ON(1);
		rc = false;
	}

	return rc;
}

static void __init setup_elf_hwcaps(const struct arm64_cpu_capabilities *hwcaps)
{
	for (; hwcaps->matches; hwcaps++)
		if (hwcaps->matches(hwcaps, hwcaps->def_scope))
			cap_set_elf_hwcap(hwcaps);
}

void update_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info)
{
	for (; caps->matches; caps++) {
		if (!caps->matches(caps, caps->def_scope))
			continue;

		if (!cpus_have_cap(caps->capability) && caps->desc)
			pr_info("%s %s\n", info, caps->desc);
		cpus_set_cap(caps->capability);
	}
}

/*
 * Run through the enabled capabilities and enable() it on all active
 * CPUs
 */
void __init enable_cpu_capabilities(const struct arm64_cpu_capabilities *caps)
{
	for (; caps->matches; caps++)
		if (caps->enable && cpus_have_cap(caps->capability))
			/*
			 * Use stop_machine() as it schedules the work allowing
			 * us to modify PSTATE, instead of on_each_cpu() which
			 * uses an IPI, giving us a PSTATE that disappears when
			 * we return.
			 */
			stop_machine(caps->enable, NULL, cpu_online_mask);
}

/*
 * Flag to indicate if we have computed the system wide
 * capabilities based on the boot time active CPUs. This
 * will be used to determine if a new booting CPU should
 * go through the verification process to make sure that it
 * supports the system capabilities, without using a hotplug
 * notifier.
 */
static bool sys_caps_initialised;

static inline void set_sys_caps_initialised(void)
{
	sys_caps_initialised = true;
}

/*
 * Check for CPU features that are used in early boot
 * based on the Boot CPU value.
 */
static void check_early_cpu_features(void)
{
	verify_cpu_run_el();
	verify_cpu_asid_bits();
}

static void
verify_local_elf_hwcaps(const struct arm64_cpu_capabilities *caps)
{

	for (; caps->matches; caps++)
		if (cpus_have_elf_hwcap(caps) && !caps->matches(caps, SCOPE_LOCAL_CPU)) {
			pr_crit("CPU%d: missing HWCAP: %s\n",
					smp_processor_id(), caps->desc);
			cpu_die_early();
		}
}

static void
verify_local_cpu_features(const struct arm64_cpu_capabilities *caps)
{
	for (; caps->matches; caps++) {
		if (!cpus_have_cap(caps->capability))
			continue;
		/*
		 * If the new CPU misses an advertised feature, we cannot proceed
		 * further, park the cpu.
		 */
		if (!caps->matches(caps, SCOPE_LOCAL_CPU)) {
			pr_crit("CPU%d: missing feature: %s\n",
					smp_processor_id(), caps->desc);
			cpu_die_early();
		}
		if (caps->enable)
			caps->enable(NULL);
	}
}

/*
 * Run through the enabled system capabilities and enable() it on this CPU.
 * The capabilities were decided based on the available CPUs at the boot time.
 * Any new CPU should match the system wide status of the capability. If the
 * new CPU doesn't have a capability which the system now has enabled, we
 * cannot do anything to fix it up and could cause unexpected failures. So
 * we park the CPU.
 */
static void verify_local_cpu_capabilities(void)
{
	verify_local_cpu_errata_workarounds();
	verify_local_cpu_features(arm64_features);
	verify_local_elf_hwcaps(arm64_elf_hwcaps);
	if (system_supports_32bit_el0())
		verify_local_elf_hwcaps(compat_elf_hwcaps);
}

void check_local_cpu_capabilities(void)
{
	/*
	 * All secondary CPUs should conform to the early CPU features
	 * in use by the kernel based on boot CPU.
	 */
	check_early_cpu_features();

	/*
	 * If we haven't finalised the system capabilities, this CPU gets
	 * a chance to update the errata work arounds.
	 * Otherwise, this CPU should verify that it has all the system
	 * advertised capabilities.
	 */
	if (!sys_caps_initialised)
		update_cpu_errata_workarounds();
	else
		verify_local_cpu_capabilities();
}

static void __init setup_feature_capabilities(void)
{
	update_cpu_capabilities(arm64_features, "detected feature:");
	enable_cpu_capabilities(arm64_features);
}

/*
 * Check if the current CPU has a given feature capability.
 * Should be called from non-preemptible context.
 */
bool this_cpu_has_cap(unsigned int cap)
{
	const struct arm64_cpu_capabilities *caps;

	if (WARN_ON(preemptible()))
		return false;

	for (caps = arm64_features; caps->desc; caps++)
		if (caps->capability == cap && caps->matches)
			return caps->matches(caps, SCOPE_LOCAL_CPU);

	return false;
}

void __init setup_cpu_features(void)
{
	u32 cwg;
	int cls;

	/* Set the CPU feature capabilies */
	setup_feature_capabilities();
	enable_errata_workarounds();
	setup_elf_hwcaps(arm64_elf_hwcaps);

	if (system_supports_32bit_el0())
		setup_elf_hwcaps(compat_elf_hwcaps);

	/* Advertise that we have computed the system capabilities */
	set_sys_caps_initialised();

	/*
	 * Check for sane CTR_EL0.CWG value.
	 */
	cwg = cache_type_cwg();
	cls = cache_line_size();
	if (!cwg)
		pr_warn("No Cache Writeback Granule information, assuming cache line size %d\n",
			cls);
	if (L1_CACHE_BYTES < cls)
		pr_warn("L1_CACHE_BYTES smaller than the Cache Writeback Granule (%d < %d)\n",
			L1_CACHE_BYTES, cls);
}

static bool __maybe_unused
cpufeature_pan_not_uao(const struct arm64_cpu_capabilities *entry, int __unused)
{
	return (cpus_have_cap(ARM64_HAS_PAN) && !cpus_have_cap(ARM64_HAS_UAO));
}
