/*
 * cpu.h: Values of the PRId register used to match up
 *	  various MIPS cpu types.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 2004, 2013  Maciej W. Rozycki
 */
#ifndef _ASM_CPU_H
#define _ASM_CPU_H

/*
   As of the MIPS32 and MIPS64 specs from MTI, the PRId register (CP0
   register 15, select 0) is defined in this (backwards compatible) way:

  +----------------+----------------+----------------+----------------+
  | Company Options| Company ID	    | Processor ID   | Revision	      |
  +----------------+----------------+----------------+----------------+
   31		 24 23		  16 15		    8 7

   I don't have docs for all the previous processors, but my impression is
   that bits 16-23 have been 0 for all MIPS processors before the MIPS32/64
   spec.
*/

#define PRID_OPT_MASK		0xff000000

/*
 * Assigned Company values for bits 23:16 of the PRId register.
 */

#define PRID_COMP_MASK		0xff0000

#define PRID_COMP_LEGACY	0x000000
#define PRID_COMP_MIPS		0x010000
#define PRID_COMP_BROADCOM	0x020000
#define PRID_COMP_ALCHEMY	0x030000
#define PRID_COMP_SIBYTE	0x040000
#define PRID_COMP_SANDCRAFT	0x050000
#define PRID_COMP_NXP		0x060000
#define PRID_COMP_TOSHIBA	0x070000
#define PRID_COMP_LSI		0x080000
#define PRID_COMP_LEXRA		0x0b0000
#define PRID_COMP_NETLOGIC	0x0c0000
#define PRID_COMP_CAVIUM	0x0d0000
#define PRID_COMP_LOONGSON	0x140000
#define PRID_COMP_INGENIC_D0	0xd00000	/* JZ4740, JZ4750 */
#define PRID_COMP_INGENIC_D1	0xd10000	/* JZ4770, JZ4775 */
#define PRID_COMP_INGENIC_E1	0xe10000	/* JZ4780 */

/*
 * Assigned Processor ID (implementation) values for bits 15:8 of the PRId
 * register.  In order to detect a certain CPU type exactly eventually
 * additional registers may need to be examined.
 */

#define PRID_IMP_MASK		0xff00

/*
 * These are valid when 23:16 == PRID_COMP_LEGACY
 */

#define PRID_IMP_R2000		0x0100
#define PRID_IMP_AU1_REV1	0x0100
#define PRID_IMP_AU1_REV2	0x0200
#define PRID_IMP_R3000		0x0200		/* Same as R2000A  */
#define PRID_IMP_R6000		0x0300		/* Same as R3000A  */
#define PRID_IMP_R4000		0x0400
#define PRID_IMP_R6000A		0x0600
#define PRID_IMP_R10000		0x0900
#define PRID_IMP_R4300		0x0b00
#define PRID_IMP_VR41XX		0x0c00
#define PRID_IMP_R12000		0x0e00
#define PRID_IMP_R14000		0x0f00		/* R14K && R16K */
#define PRID_IMP_R8000		0x1000
#define PRID_IMP_PR4450		0x1200
#define PRID_IMP_R4600		0x2000
#define PRID_IMP_R4700		0x2100
#define PRID_IMP_TX39		0x2200
#define PRID_IMP_R4640		0x2200
#define PRID_IMP_R4650		0x2200		/* Same as R4640 */
#define PRID_IMP_R5000		0x2300
#define PRID_IMP_TX49		0x2d00
#define PRID_IMP_SONIC		0x2400
#define PRID_IMP_MAGIC		0x2500
#define PRID_IMP_RM7000		0x2700
#define PRID_IMP_NEVADA		0x2800		/* RM5260 ??? */
#define PRID_IMP_RM9000		0x3400
#define PRID_IMP_LOONGSON_32	0x4200  /* Loongson-1 */
#define PRID_IMP_R5432		0x5400
#define PRID_IMP_R5500		0x5500
#define PRID_IMP_LOONGSON_64	0x6300  /* Loongson-2/3 */

#define PRID_IMP_UNKNOWN	0xff00

/*
 * These are the PRID's for when 23:16 == PRID_COMP_MIPS
 */

#define PRID_IMP_QEMU_GENERIC	0x0000
#define PRID_IMP_4KC		0x8000
#define PRID_IMP_5KC		0x8100
#define PRID_IMP_20KC		0x8200
#define PRID_IMP_4KEC		0x8400
#define PRID_IMP_4KSC		0x8600
#define PRID_IMP_25KF		0x8800
#define PRID_IMP_5KE		0x8900
#define PRID_IMP_4KECR2		0x9000
#define PRID_IMP_4KEMPR2	0x9100
#define PRID_IMP_4KSD		0x9200
#define PRID_IMP_24K		0x9300
#define PRID_IMP_34K		0x9500
#define PRID_IMP_24KE		0x9600
#define PRID_IMP_74K		0x9700
#define PRID_IMP_1004K		0x9900
#define PRID_IMP_1074K		0x9a00
#define PRID_IMP_M14KC		0x9c00
#define PRID_IMP_M14KEC		0x9e00
#define PRID_IMP_INTERAPTIV_UP	0xa000
#define PRID_IMP_INTERAPTIV_MP	0xa100
#define PRID_IMP_PROAPTIV_UP	0xa200
#define PRID_IMP_PROAPTIV_MP	0xa300
#define PRID_IMP_P6600		0xa400
#define PRID_IMP_M5150		0xa700
#define PRID_IMP_P5600		0xa800
#define PRID_IMP_I6400		0xa900
#define PRID_IMP_M6250		0xab00

/*
 * These are the PRID's for when 23:16 == PRID_COMP_SIBYTE
 */

#define PRID_IMP_SB1		0x0100
#define PRID_IMP_SB1A		0x1100

/*
 * These are the PRID's for when 23:16 == PRID_COMP_SANDCRAFT
 */

#define PRID_IMP_SR71000	0x0400

/*
 * These are the PRID's for when 23:16 == PRID_COMP_BROADCOM
 */

#define PRID_IMP_BMIPS32_REV4	0x4000
#define PRID_IMP_BMIPS32_REV8	0x8000
#define PRID_IMP_BMIPS3300	0x9000
#define PRID_IMP_BMIPS3300_ALT	0x9100
#define PRID_IMP_BMIPS3300_BUG	0x0000
#define PRID_IMP_BMIPS43XX	0xa000
#define PRID_IMP_BMIPS5000	0x5a00
#define PRID_IMP_BMIPS5200	0x5b00

#define PRID_REV_BMIPS4380_LO	0x0040
#define PRID_REV_BMIPS4380_HI	0x006f

/*
 * These are the PRID's for when 23:16 == PRID_COMP_CAVIUM
 */

#define PRID_IMP_CAVIUM_CN38XX 0x0000
#define PRID_IMP_CAVIUM_CN31XX 0x0100
#define PRID_IMP_CAVIUM_CN30XX 0x0200
#define PRID_IMP_CAVIUM_CN58XX 0x0300
#define PRID_IMP_CAVIUM_CN56XX 0x0400
#define PRID_IMP_CAVIUM_CN50XX 0x0600
#define PRID_IMP_CAVIUM_CN52XX 0x0700
#define PRID_IMP_CAVIUM_CN63XX 0x9000
#define PRID_IMP_CAVIUM_CN68XX 0x9100
#define PRID_IMP_CAVIUM_CN66XX 0x9200
#define PRID_IMP_CAVIUM_CN61XX 0x9300
#define PRID_IMP_CAVIUM_CNF71XX 0x9400
#define PRID_IMP_CAVIUM_CN78XX 0x9500
#define PRID_IMP_CAVIUM_CN70XX 0x9600
#define PRID_IMP_CAVIUM_CN73XX 0x9700
#define PRID_IMP_CAVIUM_CNF75XX 0x9800

/*
 * These are the PRID's for when 23:16 == PRID_COMP_INGENIC_*
 */

#define PRID_IMP_JZRISC	       0x0200

/*
 * These are the PRID's for when 23:16 == PRID_COMP_NETLOGIC
 */
#define PRID_IMP_NETLOGIC_XLR732	0x0000
#define PRID_IMP_NETLOGIC_XLR716	0x0200
#define PRID_IMP_NETLOGIC_XLR532	0x0900
#define PRID_IMP_NETLOGIC_XLR308	0x0600
#define PRID_IMP_NETLOGIC_XLR532C	0x0800
#define PRID_IMP_NETLOGIC_XLR516C	0x0a00
#define PRID_IMP_NETLOGIC_XLR508C	0x0b00
#define PRID_IMP_NETLOGIC_XLR308C	0x0f00
#define PRID_IMP_NETLOGIC_XLS608	0x8000
#define PRID_IMP_NETLOGIC_XLS408	0x8800
#define PRID_IMP_NETLOGIC_XLS404	0x8c00
#define PRID_IMP_NETLOGIC_XLS208	0x8e00
#define PRID_IMP_NETLOGIC_XLS204	0x8f00
#define PRID_IMP_NETLOGIC_XLS108	0xce00
#define PRID_IMP_NETLOGIC_XLS104	0xcf00
#define PRID_IMP_NETLOGIC_XLS616B	0x4000
#define PRID_IMP_NETLOGIC_XLS608B	0x4a00
#define PRID_IMP_NETLOGIC_XLS416B	0x4400
#define PRID_IMP_NETLOGIC_XLS412B	0x4c00
#define PRID_IMP_NETLOGIC_XLS408B	0x4e00
#define PRID_IMP_NETLOGIC_XLS404B	0x4f00
#define PRID_IMP_NETLOGIC_AU13XX	0x8000

#define PRID_IMP_NETLOGIC_XLP8XX	0x1000
#define PRID_IMP_NETLOGIC_XLP3XX	0x1100
#define PRID_IMP_NETLOGIC_XLP2XX	0x1200
#define PRID_IMP_NETLOGIC_XLP9XX	0x1500
#define PRID_IMP_NETLOGIC_XLP5XX	0x1300

/*
 * Particular Revision values for bits 7:0 of the PRId register.
 */

#define PRID_REV_MASK		0x00ff

/*
 * Definitions for 7:0 on legacy processors
 */

#define PRID_REV_TX4927		0x0022
#define PRID_REV_TX4937		0x0030
#define PRID_REV_R4400		0x0040
#define PRID_REV_R3000A		0x0030
#define PRID_REV_R3000		0x0020
#define PRID_REV_R2000A		0x0010
#define PRID_REV_TX3912		0x0010
#define PRID_REV_TX3922		0x0030
#define PRID_REV_TX3927		0x0040
#define PRID_REV_VR4111		0x0050
#define PRID_REV_VR4181		0x0050	/* Same as VR4111 */
#define PRID_REV_VR4121		0x0060
#define PRID_REV_VR4122		0x0070
#define PRID_REV_VR4181A	0x0070	/* Same as VR4122 */
#define PRID_REV_VR4130		0x0080
#define PRID_REV_34K_V1_0_2	0x0022
#define PRID_REV_LOONGSON1B	0x0020
#define PRID_REV_LOONGSON1C	0x0020	/* Same as Loongson-1B */
#define PRID_REV_LOONGSON2E	0x0002
#define PRID_REV_LOONGSON2F	0x0003
#define PRID_REV_LOONGSON3A_R1	0x0005
#define PRID_REV_LOONGSON3B_R1	0x0006
#define PRID_REV_LOONGSON3B_R2	0x0007
#define PRID_REV_LOONGSON3A_R2	0x0008

/*
 * Older processors used to encode processor version and revision in two
 * 4-bit bitfields, the 4K seems to simply count up and even newer MTI cores
 * have switched to use the 8-bits as 3:3:2 bitfield with the last field as
 * the patch number.  *ARGH*
 */
#define PRID_REV_ENCODE_44(ver, rev)					\
	((ver) << 4 | (rev))
#define PRID_REV_ENCODE_332(ver, rev, patch)				\
	((ver) << 5 | (rev) << 2 | (patch))

/*
 * FPU implementation/revision register (CP1 control register 0).
 *
 * +---------------------------------+----------------+----------------+
 * | 0				     | Implementation | Revision       |
 * +---------------------------------+----------------+----------------+
 *  31				   16 15	     8 7	      0
 */

#define FPIR_IMP_MASK		0xff00

#define FPIR_IMP_NONE		0x0000

#if !defined(__ASSEMBLY__)

enum cpu_type_enum {
	CPU_UNKNOWN,

	/*
	 * R2000 class processors
	 */
	CPU_R2000, CPU_R3000, CPU_R3000A, CPU_R3041, CPU_R3051, CPU_R3052,
	CPU_R3081, CPU_R3081E,

	/*
	 * R6000 class processors
	 */
	CPU_R6000, CPU_R6000A,

	/*
	 * R4000 class processors
	 */
	CPU_R4000PC, CPU_R4000SC, CPU_R4000MC, CPU_R4200, CPU_R4300, CPU_R4310,
	CPU_R4400PC, CPU_R4400SC, CPU_R4400MC, CPU_R4600, CPU_R4640, CPU_R4650,
	CPU_R4700, CPU_R5000, CPU_R5500, CPU_NEVADA, CPU_R5432, CPU_R10000,
	CPU_R12000, CPU_R14000, CPU_R16000, CPU_VR41XX, CPU_VR4111, CPU_VR4121,
	CPU_VR4122, CPU_VR4131, CPU_VR4133, CPU_VR4181, CPU_VR4181A, CPU_RM7000,
	CPU_SR71000, CPU_TX49XX,

	/*
	 * R8000 class processors
	 */
	CPU_R8000,

	/*
	 * TX3900 class processors
	 */
	CPU_TX3912, CPU_TX3922, CPU_TX3927,

	/*
	 * MIPS32 class processors
	 */
	CPU_4KC, CPU_4KEC, CPU_4KSC, CPU_24K, CPU_34K, CPU_1004K, CPU_74K,
	CPU_ALCHEMY, CPU_PR4450, CPU_BMIPS32, CPU_BMIPS3300, CPU_BMIPS4350,
	CPU_BMIPS4380, CPU_BMIPS5000, CPU_JZRISC, CPU_LOONGSON1, CPU_M14KC,
	CPU_M14KEC, CPU_INTERAPTIV, CPU_P5600, CPU_PROAPTIV, CPU_1074K,
	CPU_M5150, CPU_I6400, CPU_P6600, CPU_M6250,

	/*
	 * MIPS64 class processors
	 */
	CPU_5KC, CPU_5KE, CPU_20KC, CPU_25KF, CPU_SB1, CPU_SB1A, CPU_LOONGSON2,
	CPU_LOONGSON3, CPU_CAVIUM_OCTEON, CPU_CAVIUM_OCTEON_PLUS,
	CPU_CAVIUM_OCTEON2, CPU_CAVIUM_OCTEON3, CPU_XLR, CPU_XLP,

	CPU_QEMU_GENERIC,

	CPU_LAST
};

#endif /* !__ASSEMBLY */

/*
 * ISA Level encodings
 *
 */
#define MIPS_CPU_ISA_II		0x00000001
#define MIPS_CPU_ISA_III	0x00000002
#define MIPS_CPU_ISA_IV		0x00000004
#define MIPS_CPU_ISA_V		0x00000008
#define MIPS_CPU_ISA_M32R1	0x00000010
#define MIPS_CPU_ISA_M32R2	0x00000020
#define MIPS_CPU_ISA_M64R1	0x00000040
#define MIPS_CPU_ISA_M64R2	0x00000080
#define MIPS_CPU_ISA_M32R6	0x00000100
#define MIPS_CPU_ISA_M64R6	0x00000200

#define MIPS_CPU_ISA_32BIT (MIPS_CPU_ISA_II | MIPS_CPU_ISA_M32R1 | \
	MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M32R6)
#define MIPS_CPU_ISA_64BIT (MIPS_CPU_ISA_III | MIPS_CPU_ISA_IV | \
	MIPS_CPU_ISA_V | MIPS_CPU_ISA_M64R1 | MIPS_CPU_ISA_M64R2 | \
	MIPS_CPU_ISA_M64R6)

/*
 * Private version of BIT_ULL() to escape include file recursion hell.
 * We soon will have to switch to another mechanism that will work with
 * more than 64 bits anyway.
 */
#define MBIT_ULL(bit)		(1ULL << (bit))

/*
 * CPU Option encodings
 */
#define MIPS_CPU_TLB		MBIT_ULL( 0)	/* CPU has TLB */
#define MIPS_CPU_4KEX		MBIT_ULL( 1)	/* "R4K" exception model */
#define MIPS_CPU_3K_CACHE	MBIT_ULL( 2)	/* R3000-style caches */
#define MIPS_CPU_4K_CACHE	MBIT_ULL( 3)	/* R4000-style caches */
#define MIPS_CPU_TX39_CACHE	MBIT_ULL( 4)	/* TX3900-style caches */
#define MIPS_CPU_FPU		MBIT_ULL( 5)	/* CPU has FPU */
#define MIPS_CPU_32FPR		MBIT_ULL( 6)	/* 32 dbl. prec. FP registers */
#define MIPS_CPU_COUNTER	MBIT_ULL( 7)	/* Cycle count/compare */
#define MIPS_CPU_WATCH		MBIT_ULL( 8)	/* watchpoint registers */
#define MIPS_CPU_DIVEC		MBIT_ULL( 9)	/* dedicated interrupt vector */
#define MIPS_CPU_VCE		MBIT_ULL(10)	/* virt. coherence conflict possible */
#define MIPS_CPU_CACHE_CDEX_P	MBIT_ULL(11)	/* Create_Dirty_Exclusive CACHE op */
#define MIPS_CPU_CACHE_CDEX_S	MBIT_ULL(12)	/* ... same for seconary cache ... */
#define MIPS_CPU_MCHECK		MBIT_ULL(13)	/* Machine check exception */
#define MIPS_CPU_EJTAG		MBIT_ULL(14)	/* EJTAG exception */
#define MIPS_CPU_NOFPUEX	MBIT_ULL(15)	/* no FPU exception */
#define MIPS_CPU_LLSC		MBIT_ULL(16)	/* CPU has ll/sc instructions */
#define MIPS_CPU_INCLUSIVE_CACHES	MBIT_ULL(17)	/* P-cache subset enforced */
#define MIPS_CPU_PREFETCH	MBIT_ULL(18)	/* CPU has usable prefetch */
#define MIPS_CPU_VINT		MBIT_ULL(19)	/* CPU supports MIPSR2 vectored interrupts */
#define MIPS_CPU_VEIC		MBIT_ULL(20)	/* CPU supports MIPSR2 external interrupt controller mode */
#define MIPS_CPU_ULRI		MBIT_ULL(21)	/* CPU has ULRI feature */
#define MIPS_CPU_PCI		MBIT_ULL(22)	/* CPU has Perf Ctr Int indicator */
#define MIPS_CPU_RIXI		MBIT_ULL(23)	/* CPU has TLB Read/eXec Inhibit */
#define MIPS_CPU_MICROMIPS	MBIT_ULL(24)	/* CPU has microMIPS capability */
#define MIPS_CPU_TLBINV		MBIT_ULL(25)	/* CPU supports TLBINV/F */
#define MIPS_CPU_SEGMENTS	MBIT_ULL(26)	/* CPU supports Segmentation Control registers */
#define MIPS_CPU_EVA		MBIT_ULL(27)	/* CPU supports Enhanced Virtual Addressing */
#define MIPS_CPU_HTW		MBIT_ULL(28)	/* CPU support Hardware Page Table Walker */
#define MIPS_CPU_RIXIEX		MBIT_ULL(29)	/* CPU has unique exception codes for {Read, Execute}-Inhibit exceptions */
#define MIPS_CPU_MAAR		MBIT_ULL(30)	/* MAAR(I) registers are present */
#define MIPS_CPU_FRE		MBIT_ULL(31)	/* FRE & UFE bits implemented */
#define MIPS_CPU_RW_LLB		MBIT_ULL(32)	/* LLADDR/LLB writes are allowed */
#define MIPS_CPU_LPA		MBIT_ULL(33)	/* CPU supports Large Physical Addressing */
#define MIPS_CPU_CDMM		MBIT_ULL(34)	/* CPU has Common Device Memory Map */
#define MIPS_CPU_BP_GHIST	MBIT_ULL(35)	/* R12K+ Branch Prediction Global History */
#define MIPS_CPU_SP		MBIT_ULL(36)	/* Small (1KB) page support */
#define MIPS_CPU_FTLB		MBIT_ULL(37)	/* CPU has Fixed-page-size TLB */
#define MIPS_CPU_NAN_LEGACY	MBIT_ULL(38)	/* Legacy NaN implemented */
#define MIPS_CPU_NAN_2008	MBIT_ULL(39)	/* 2008 NaN implemented */
#define MIPS_CPU_VP		MBIT_ULL(40)	/* MIPSr6 Virtual Processors (multi-threading) */
#define MIPS_CPU_LDPTE		MBIT_ULL(41)	/* CPU has ldpte/lddir instructions */
#define MIPS_CPU_MVH		MBIT_ULL(42)	/* CPU supports MFHC0/MTHC0 */
#define MIPS_CPU_EBASE_WG	MBIT_ULL(43)	/* CPU has EBase.WG */
#define MIPS_CPU_BADINSTR	MBIT_ULL(44)	/* CPU has BadInstr register */
#define MIPS_CPU_BADINSTRP	MBIT_ULL(45)	/* CPU has BadInstrP register */
#define MIPS_CPU_CTXTC		MBIT_ULL(46)	/* CPU has [X]ConfigContext registers */
#define MIPS_CPU_PERF		MBIT_ULL(47)	/* CPU has MIPS performance counters */
#define MIPS_CPU_GUESTCTL0EXT	MBIT_ULL(48)	/* CPU has VZ GuestCtl0Ext register */
#define MIPS_CPU_GUESTCTL1	MBIT_ULL(49)	/* CPU has VZ GuestCtl1 register */
#define MIPS_CPU_GUESTCTL2	MBIT_ULL(50)	/* CPU has VZ GuestCtl2 register */
#define MIPS_CPU_GUESTID	MBIT_ULL(51)	/* CPU uses VZ ASE GuestID feature */
#define MIPS_CPU_DRG		MBIT_ULL(52)	/* CPU has VZ Direct Root to Guest (DRG) */

/*
 * CPU ASE encodings
 */
#define MIPS_ASE_MIPS16		0x00000001 /* code compression */
#define MIPS_ASE_MDMX		0x00000002 /* MIPS digital media extension */
#define MIPS_ASE_MIPS3D		0x00000004 /* MIPS-3D */
#define MIPS_ASE_SMARTMIPS	0x00000008 /* SmartMIPS */
#define MIPS_ASE_DSP		0x00000010 /* Signal Processing ASE */
#define MIPS_ASE_MIPSMT		0x00000020 /* CPU supports MIPS MT */
#define MIPS_ASE_DSP2P		0x00000040 /* Signal Processing ASE Rev 2 */
#define MIPS_ASE_VZ		0x00000080 /* Virtualization ASE */
#define MIPS_ASE_MSA		0x00000100 /* MIPS SIMD Architecture */
#define MIPS_ASE_DSP3		0x00000200 /* Signal Processing ASE Rev 3*/

#endif /* _ASM_CPU_H */
