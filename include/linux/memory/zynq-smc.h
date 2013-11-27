/*
 * Xilinx Zynq SMC Driver Header
 *
 * Copyright (C) 2012 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __LINUX_MEMORY_ZYNQ_SMC_H
#define __LINUX_MEMORY_ZYNQ_SMC_H

enum zynq_smc_ecc_mode {
	ZYNQ_SMC_ECCMODE_BYPASS = 0,
	ZYNQ_SMC_ECCMODE_APB = 1,
	ZYNQ_SMC_ECCMODE_MEM = 2
};

u32 zynq_smc_get_ecc_val(int ecc_reg);
int zynq_smc_ecc_is_busy(void);
int zynq_smc_get_nand_int_status_raw(void);
void zynq_smc_clr_nand_int(void);
int zynq_smc_set_ecc_mode(enum zynq_smc_ecc_mode mode);
int zynq_smc_set_ecc_pg_size(unsigned int pg_sz);

#endif
