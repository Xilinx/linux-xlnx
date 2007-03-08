/*
 * arch/microblaze/kernel/vmlinux.lds.S
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

OUTPUT_FORMAT("elf32-microblaze", "elf32-microblaze", "elf32-microblaze")
OUTPUT_ARCH(microblaze)
ENTRY(_start)

#include <asm/xparameters.h>
#include <linux/autoconf.h>
#include <asm-generic/vmlinux.lds.h>

jiffies = jiffies_64 + 4;

SECTIONS {

	. = CONFIG_XILINX_ERAM_START;

#ifdef CONFIG_BLUECAT_LOADER
	. += 0x7000;
#endif
	.text : {
		_stext = . ;
        	*(.text)
		*(.text.exit)
		*(.text.lock)
		*(.exitcall.exit)
		SCHED_TEXT
		LOCK_TEXT
		. = ALIGN (4) ;
		_etext = . ;
	}

	. = ALIGN(16);
	RODATA

	/* sdata2 section can go anywhere, but must be word aligned
	   and SDA2_BASE must point to the middle of it */
	.sdata2 : {
		_ssrw = .;
		. = ALIGN(0x8);
		*(.sdata2)
	. = ALIGN(8);
	_essrw = .;
	_ssrw_size = _essrw - _ssrw;
	_KERNEL_SDA2_BASE_ = _ssrw + (_ssrw_size / 2);
	}


	_sdata = . ;
	.data ALIGN (0x4) : {
        	*(.data)
	}
	. = ALIGN(32);
	.data.cacheline_aligned : { *(.data.cacheline_aligned) }
	_edata = . ;

	/* The initial task */
	. = ALIGN(8192);
	.data.init_task : { *(.data.init_task) }

	/* Under the microblaze ABI, .sdata and .sbss must be contiguous */
	. = ALIGN(8);
	.sdata : {
		_ssro = .;
		*(.sdata)
	}

	.sbss :	{
		_ssbss = .;
		*(.sbss)
		_esbss = .;
		_essro = .;
		_ssro_size = _essro - _ssro ;
		_KERNEL_SDA_BASE_ = _ssro + (_ssro_size / 2) ;
	}

	. = ALIGN(16);
	__start___ex_table = .;
	__ex_table : { *(__ex_table) }
	__stop___ex_table = .;

	__init_begin = .;

	. = ALIGN(4096);
	.init.text : {
		_sinittext = . ;
		*(.init.text)
		*(.exit.text)
		*(.exit.data)
		_einittext = .;
	}

	.init.data : { *(.init.data) }

	. = ALIGN(4);
	.init.ivt : {
		__ivt_start = .;
		*(.init.ivt)
		__ivt_end = .;
	}

	.init.setup : {
		__setup_start = .;
		*(.init.setup)
		__setup_end = .;
	}

	.initcall.init : {
		__initcall_start = .;
		*(.initcall1.init)
		*(.initcall2.init)
		*(.initcall3.init)
		*(.initcall4.init)
		*(.initcall5.init)
		*(.initcall6.init)
		*(.initcall7.init)
		__initcall_end = .;
	}

	.con_initcall.init : {
		__con_initcall_start = .;
		*(.con_initcall.init)
		__con_initcall_end = .;
	}

	__init_end_before_initramfs = .;

	.init.ramfs ALIGN(4096) : {
		__initramfs_start = .;
		*(.init.ramfs)
		__initramfs_end = .;
		. = ALIGN(4);
		LONG(0);
	}

	__init_end = .;

	.bss ALIGN (4096) : {
		__bss_start = . ;
			*(.bss*)
			*(COMMON)
		. = ALIGN (4) ;
		__bss_stop = . ;
		_ebss = . ;
 	}
	. = ALIGN(4096);
	_end = .;
}


