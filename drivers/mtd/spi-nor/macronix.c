// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static int
mx25l25635_post_bfpt_fixups(struct spi_nor *nor,
			    const struct sfdp_parameter_header *bfpt_header,
			    const struct sfdp_bfpt *bfpt,
			    struct spi_nor_flash_parameter *params)
{
	/*
	 * MX25L25635F supports 4B opcodes but MX25L25635E does not.
	 * Unfortunately, Macronix has re-used the same JEDEC ID for both
	 * variants which prevents us from defining a new entry in the parts
	 * table.
	 * We need a way to differentiate MX25L25635E and MX25L25635F, and it
	 * seems that the F version advertises support for Fast Read 4-4-4 in
	 * its BFPT table.
	 */
	if (bfpt->dwords[BFPT_DWORD(5)] & BFPT_DWORD5_FAST_READ_4_4_4)
		nor->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static struct spi_nor_fixups mx25l25635_fixups = {
	.post_bfpt = mx25l25635_post_bfpt_fixups,
};

static int mx25um51345g_set_4byte(struct spi_nor *nor, bool enable)
{
	(void)enable;

	return 0;
}

static void mx25um51345g_default_init_fixups(struct spi_nor *nor)
{
	u8 id_byte1, id_byte2;

	nor->params->set_4byte_addr_mode = mx25um51345g_set_4byte;
	/* Octal SPI Macronix require dual-byte opcode for DDR mode */
	nor->spimem->spi->master->flags |= SPI_DUAL_BYTE_OP;

	/*
	 * Macronix Read Id bytes are always output in STR mode. Since tuning
	 * is based on Read Id command, adjust the Read Id bytes that will
	 * match the Read Id output in DTR mode.
	 */
	id_byte1 = nor->spimem->device_id[1];
	id_byte2 = nor->spimem->device_id[2];
	nor->spimem->device_id[1] = nor->spimem->device_id[0];
	nor->spimem->device_id[2] = id_byte1;
	nor->spimem->device_id[3] = id_byte1;
	nor->spimem->device_id[4] = id_byte2;
	nor->spimem->device_id[5] = id_byte2;

	nor->params->reads[SNOR_CMD_READ_8_8_8].num_wait_states = 20;
	nor->params->reads[SNOR_CMD_READ_8_8_8].opcode =
			SPINOR_OP_READ_1_4_4_DTR_4B;
	nor->params->page_programs[SNOR_CMD_PP_8_8_8].opcode =
			SPINOR_OP_PP_4B;
	spi_nor_set_erase_type(&nor->params->erase_map.erase_type[1],
			       nor->info->sector_size, SPINOR_OP_BE_4K_4B);
}

static struct spi_nor_fixups mx25um51345g_fixups = {
	.default_init = mx25um51345g_default_init_fixups,
};

static struct flash_info macronix_parts[] = {
	/* Macronix */
	{ "mx25l512e",   INFO(0xc22010, 0, 64 * 1024,   1, SECT_4K) },
	{ "mx25l2005a",  INFO(0xc22012, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25l4005a",  INFO(0xc22013, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25l8005",   INFO(0xc22014, 0, 64 * 1024,  16, 0) },
	{ "mx25l1606e",  INFO(0xc22015, 0, 64 * 1024,  32, SECT_4K) },
	{ "mx25l3205d",  INFO(0xc22016, 0, 64 * 1024,  64, SECT_4K) },
	{ "mx25l3255e",  INFO(0xc29e16, 0, 64 * 1024,  64, SECT_4K) },
	{ "mx25l6405d",  INFO(0xc22017, 0, 64 * 1024, 128, SECT_4K) },
	{ "mx25u2033e",  INFO(0xc22532, 0, 64 * 1024,   4, SECT_4K) },
	{ "mx25u3235f",	 INFO(0xc22536, 0, 64 * 1024,  64,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25u4035",   INFO(0xc22533, 0, 64 * 1024,   8, SECT_4K) },
	{ "mx25u8035",   INFO(0xc22534, 0, 64 * 1024,  16, SECT_4K) },
	{ "mx25u6435f",  INFO(0xc22537, 0, 64 * 1024, 128, SECT_4K) },
	{ "mx25l12805d", INFO(0xc22018, 0, 64 * 1024, 256, SECT_4K) },
	{ "mx25l12855e", INFO(0xc22618, 0, 64 * 1024, 256, 0) },
	{ "mx25r1635f",  INFO(0xc22815, 0, 64 * 1024,  32,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25r3235f",  INFO(0xc22816, 0, 64 * 1024,  64,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25u12835f", INFO(0xc22538, 0, 64 * 1024, 256,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25l25635e", INFO(0xc22019, 0, 64 * 1024, 512,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ)
		.fixups = &mx25l25635_fixups },
	{ "mx25u25635f", INFO(0xc22539, 0, 64 * 1024, 512,
			      SECT_4K | SPI_NOR_4B_OPCODES) },
	{ "mx25u51245g", INFO(0xc2253a, 0, 64 * 1024, 1024,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "mx25v8035f",  INFO(0xc22314, 0, 64 * 1024,  16,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx25l25655e", INFO(0xc22619, 0, 64 * 1024, 512, 0) },
	{ "mx25l51245g", INFO(0xc2201a, 0, 64 * 1024, 1024,
			      SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      SPI_NOR_4B_OPCODES) },
	{ "mx66l51235l", INFO(0xc2201a, 0, 64 * 1024, 1024,
			      SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			      SPI_NOR_4B_OPCODES) },
	{ "mx66u51235f", INFO(0xc2253a, 0, 64 * 1024, 1024,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "mx66l1g45g",  INFO(0xc2201b, 0, 64 * 1024, 2048,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ) },
	{ "mx66u1g45g",  INFO(0xc2253b, 0, 64 * 1024, 2048, SECT_4K |
			SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ) },
	{ "mx66l1g55g",  INFO(0xc2261b, 0, 64 * 1024, 2048,
			      SPI_NOR_QUAD_READ) },
	{ "mx66u2g45g",	 INFO(0xc2253c, 0, 64 * 1024, 4096,
			      SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ | SPI_NOR_4B_OPCODES) },
	{ "mx25um51345g",  INFO(0xc2813a, 0, 4 * 1024, 16384,
				SECT_4K | SPI_NOR_OCTAL_READ |
				SPI_NOR_OCTAL_WRITE | SPI_NOR_4B_OPCODES)
		.fixups = &mx25um51345g_fixups },
};

static void macronix_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
	nor->params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode;
}

static const struct spi_nor_fixups macronix_fixups = {
	.default_init = macronix_default_init,
};

const struct spi_nor_manufacturer spi_nor_macronix = {
	.name = "macronix",
	.parts = macronix_parts,
	.nparts = ARRAY_SIZE(macronix_parts),
	.fixups = &macronix_fixups,
};
