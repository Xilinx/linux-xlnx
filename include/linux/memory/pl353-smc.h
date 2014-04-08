/*
 * ARM PL353 SMC Driver Header
 *
 * Copyright (C) 2012 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __LINUX_MEMORY_PL353_SMC_H
#define __LINUX_MEMORY_PL353_SMC_H

enum pl353_smc_ecc_mode {
	PL353_SMC_ECCMODE_BYPASS = 0,
	PL353_SMC_ECCMODE_APB = 1,
	PL353_SMC_ECCMODE_MEM = 2
};

enum pl353_smc_mem_width {
	PL353_SMC_MEM_WIDTH_8 = 0,
	PL353_SMC_MEM_WIDTH_16 = 1
};

u32 pl353_smc_get_ecc_val(int ecc_reg);
int pl353_smc_ecc_is_busy(void);
int pl353_smc_get_nand_int_status_raw(void);
void pl353_smc_clr_nand_int(void);
int pl353_smc_set_ecc_mode(enum pl353_smc_ecc_mode mode);
int pl353_smc_set_ecc_pg_size(unsigned int pg_sz);
int pl353_smc_set_buswidth(unsigned int bw);

#endif
