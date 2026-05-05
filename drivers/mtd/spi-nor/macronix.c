// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define MXIC_NOR_OP_WR_CR2	0x72		/* Write configuration register 2 opcode */
#define MXIC_NOR_ADDR_CR2_MODE	0x00000000	/* CR2 address for setting spi/sopi/dopi mode */
#define MXIC_NOR_ADDR_CR2_DC	0x00000300	/* CR2 address for setting dummy cycles */
#define MXIC_NOR_REG_DOPI_EN	0x2		/* Enable Octal DTR */
#define MXIC_NOR_REG_SPI_EN	0x0		/* Enable SPI */

/* Convert dummy cycles to bit pattern */
#define MXIC_NOR_REG_DC(p) \
	((20 - (p)) >> 1)

#define MXIC_NOR_WR_CR2(addr, ndata, buf)			\
	SPI_MEM_OP(SPI_MEM_OP_CMD(MXIC_NOR_OP_WR_CR2, 0),	\
		   SPI_MEM_OP_ADDR(4, addr, 0),			\
		   SPI_MEM_OP_NO_DUMMY,				\
		   SPI_MEM_OP_DATA_OUT(ndata, buf, 0))
#define SPINOR_OP_MX_DTR_RD	0xee	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_MX_RD_ANY_REG	0x71	/* Read volatile register */
#define SPINOR_REG_MX_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_MX_OCT_DTR	0x02	/* Enable Octal DTR. */
#define SPINOR_REG_MX_CFR2V		0x00000300
#define SPINOR_REG_MX_CFR2V_ECC		0x00000000
#define SPINOR_MX_CFR2_DC_VALUE		0x000  /* For setting dummy cycles to 20(default) */

static int spi_nor_macronix_phy_enable(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		goto ret;

	buf[0] = MXIC_NOR_REG_SPI_EN;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(MXIC_NOR_OP_WR_CR2, 1),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_MX_CFR0V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(1, buf, 1));

	spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_1_1_1);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		goto ret;

	nor->spimem->spi->controller->flags |= SPI_CONTROLLER_SDR_PHY;
	/* Read flash ID to make sure the switch was successful. */
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 1),
			   SPI_MEM_OP_NO_ADDR,
			   SPI_MEM_OP_DUMMY(0, 1),
			   SPI_MEM_OP_DATA_IN(nor->info->id->len, buf, 1));

	spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_1_1_1);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		goto ret;

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
		goto ret;

	return 0;
ret:
	nor->spimem->spi->controller->flags &= ~SPI_CONTROLLER_SDR_PHY;
	return 0;
}

static int mx25um51345g_set_4byte(struct spi_nor *nor, bool enable)
{
	(void)enable;

	return 0;
}

static void mx25um51345g_default_init_fixups(struct spi_nor *nor)
{
	u8 id_byte1, id_byte2;

	nor->params->set_4byte_addr_mode = mx25um51345g_set_4byte;

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

	spi_nor_set_erase_type(&nor->params->erase_map.erase_type[1],
			       nor->info->sector_size, SPINOR_OP_BE_4K_4B);
	nor->params->page_programs[SNOR_CMD_PP_8_8_8_DTR].opcode =
				SPINOR_OP_PP_4B;
	nor->params->phy_enable = spi_nor_macronix_phy_enable;
}

static int mx25um51345g_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/* Set the Fast Read settings. */
	params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_MX_DTR_RD,
				  SNOR_PROTO_8_8_8_DTR);

	nor->cmd_ext_type = SPI_NOR_EXT_INVERT;
	params->rdsr_dummy = 8;
	params->rdsr_addr_nbytes = 0;

	/*
	 * The BFPT quad enable field is set to a reserved value so the quad
	 * enable function is ignored by spi_nor_parse_bfpt(). Make sure we
	 * disable it.
	 */
	params->quad_enable = NULL;

	/*
	 * On some Macronix xSPI devices (e.g. MX66UM2G45G), SFDP/BFPT density
	 * underreports actual flash capacity. When the part table size differs from
	 * SFDP-parsed params->size, trust the table and refresh sizing so the full
	 * array is reachable without overwriting a correct SFDP size when they match.
	 */
	if (nor->info->size != params->size) {
		params->size = nor->info->size;
		params->bank_size = params->size;
		/*
		 * uniform_region is populated only when SFDP found no Sector
		 * Map Parameter Table. Skip for non-uniform erase maps where
		 * erase_mask is zero and this write would be a silent no-op.
		 */
		if (params->erase_map.uniform_region.erase_mask)
			params->erase_map.uniform_region.size = params->size;
	}

	return 0;
}

static int mx25um51345g_config_dummy(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;
	struct spi_mem_op op;
	int ret;
	u8 *buf = nor->bouncebuf;

	params->writesize = 1;
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MX_RD_ANY_REG, 0),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_MX_CFR2V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_IN(1, buf, 1));

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	*(buf) &= SPINOR_MX_CFR2_DC_VALUE;
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(MXIC_NOR_OP_WR_CR2, 1),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_MX_CFR2V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(1, buf, 1));

	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MX_RD_ANY_REG, 0),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_MX_CFR2V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_IN(1, buf, 1));

	ret = spi_nor_read_any_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	return 0;
}

static int mx25um51345g_late_init(struct spi_nor *nor)
{
	int ret = 0;

	ret = mx25um51345g_config_dummy(nor);
	if (ret)
		return ret;

	return 0;
}

static struct spi_nor_fixups mx25uw51345g_fixups = {
	.default_init = mx25um51345g_default_init_fixups,
	.post_sfdp = mx25um51345g_post_sfdp_fixup,
	.late_init = mx25um51345g_late_init,
};

static struct spi_nor_fixups mx25um51345g_fixups = {
	.default_init = mx25um51345g_default_init_fixups,
	.post_sfdp = mx25um51345g_post_sfdp_fixup,
};

static int
mx25l25635_post_bfpt_fixups(struct spi_nor *nor,
			    const struct sfdp_parameter_header *bfpt_header,
			    const struct sfdp_bfpt *bfpt)
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
	if (bfpt->dwords[SFDP_DWORD(5)] & BFPT_DWORD5_FAST_READ_4_4_4)
		nor->flags |= SNOR_F_4B_OPCODES;

	return 0;
}

static int
macronix_qpp4b_post_sfdp_fixups(struct spi_nor *nor)
{
	/* PP_1_1_4_4B is supported but missing in 4BAIT. */
	struct spi_nor_flash_parameter *params = nor->params;

	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_1_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_1_4],
				SPINOR_OP_PP_1_1_4_4B, SNOR_PROTO_1_1_4);

	return 0;
}

static int
mx25l3255e_late_init_fixups(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = nor->params;

	/*
	 * SFDP of MX25L3255E is JESD216, which does not include the Quad
	 * Enable bit Requirement in BFPT. As a result, during BFPT parsing,
	 * the quad_enable method is not set to spi_nor_sr1_bit6_quad_enable.
	 * Therefore, it is necessary to correct this setting by late_init.
	 */
	params->quad_enable = spi_nor_sr1_bit6_quad_enable;

	/*
	 * In addition, MX25L3255E also supports 1-4-4 page program in 3-byte
	 * address mode. However, since the 3-byte address 1-4-4 page program
	 * is not defined in SFDP, it needs to be configured in late_init.
	 */
	params->hwcaps.mask |= SNOR_HWCAPS_PP_1_4_4;
	spi_nor_set_pp_settings(&params->page_programs[SNOR_CMD_PP_1_4_4],
				SPINOR_OP_PP_1_4_4, SNOR_PROTO_1_4_4);

	return 0;
}

static const struct spi_nor_fixups mx25l25635_fixups = {
	.post_bfpt = mx25l25635_post_bfpt_fixups,
	.post_sfdp = macronix_qpp4b_post_sfdp_fixups,
};

static const struct spi_nor_fixups macronix_qpp4b_fixups = {
	.post_sfdp = macronix_qpp4b_post_sfdp_fixups,
};

static const struct spi_nor_fixups mx25l3255e_fixups = {
	.late_init = mx25l3255e_late_init_fixups,
};

static const struct flash_info macronix_nor_parts[] = {
	{
		.id = SNOR_ID(0xc2, 0x20, 0x10),
		.name = "mx25l512e",
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x12),
		.name = "mx25l2005a",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x13),
		.name = "mx25l4005a",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x14),
		.name = "mx25l8005",
		.size = SZ_1M,
	}, {
		/* MX25L1606E */
		.id = SNOR_ID(0xc2, 0x20, 0x15),
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x16),
		.name = "mx25l3205d",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x17),
		.name = "mx25l6405d",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
	}, {
		/* MX25L12805D */
		.id = SNOR_ID(0xc2, 0x20, 0x18),
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_4BIT_BP,
	}, {
		/* MX25L25635E, MX25L25645G */
		.id = SNOR_ID(0xc2, 0x20, 0x19),
		.fixups = &mx25l25635_fixups
	}, {
		/* MX66L51235F */
		.id = SNOR_ID(0xc2, 0x20, 0x1a),
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.fixups = &macronix_qpp4b_fixups,
	}, {
		/* MX66L1G45G */
		.id = SNOR_ID(0xc2, 0x20, 0x1b),
		.fixups = &macronix_qpp4b_fixups,
		.name = "mx66l1g45g",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x1c),
		.name = "mx66l2g45g",
		.size = SZ_256M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x3b),
		.name = "mx66u1g45g",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x3c),
		.name = "mx66u2g45g",
		.size = SZ_256M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x23, 0x14),
		.name = "mx25v8035f",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x32),
		.name = "mx25u2033e",
		.size = SZ_256K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x33),
		.name = "mx25u4035",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x34),
		.name = "mx25u8035",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x36),
		.name = "mx25u3235f",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x37),
		.name = "mx25u6435f",
		.size = SZ_8M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x38),
		.name = "mx25u12835f",
		.size = SZ_16M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* MX25U51245G */
		.id = SNOR_ID(0xc2, 0x25, 0x3a),
		.fixups = &macronix_qpp4b_fixups,
	}, {
		/* MX66U1G45G */
		.id = SNOR_ID(0xc2, 0x25, 0x3b),
		.fixups = &macronix_qpp4b_fixups,
	}, {
		/* MX66U2G45G */
		.id = SNOR_ID(0xc2, 0x25, 0x3c),
		.fixups = &macronix_qpp4b_fixups,
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x18),
		.name = "mx25l12855e",
		.size = SZ_16M,
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x19),
		.name = "mx25l25655e",
		.size = SZ_32M,
	}, {
		.id = SNOR_ID(0xc2, 0x26, 0x1b),
		.name = "mx66l1g55g",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x80, 0x3c),
		.name = "mx66um2g45g",
		.size = SZ_256M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5 | SPI_NOR_HAS_CR_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &mx25um51345g_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x94, 0x3c),
		.name = "mx66uw2g345gxrix0",
		.size = SZ_256M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5 | SPI_NOR_HAS_CR_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &mx25uw51345g_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x81, 0x3a),
		.name = "mx25um51345g",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5 | SPI_NOR_HAS_CR_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixups = &mx25um51345g_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x28, 0x15),
		.name = "mx25r1635f",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc2, 0x28, 0x16),
		.name = "mx25r3235f",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		/* MX25UW51245G */
		.id = SNOR_ID(0xc2, 0x81, 0x3a),
		.n_banks = 4,
		.flags = SPI_NOR_RWW,
	}, {
		/* MX25L3255E */
		.id = SNOR_ID(0xc2, 0x9e, 0x16),
		.name = "mx25l3255e",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
		.fixups = &mx25l3255e_fixups,
	},
};

static int macronix_nor_octal_dtr_en(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf, i;
	int ret;

	/* Use dummy cycles which is parse by SFDP and convert to bit pattern. */
	buf[0] = MXIC_NOR_REG_DC(nor->params->reads[SNOR_CMD_READ_8_8_8_DTR].num_wait_states);
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_DC, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Set the octal and DTR enable bits. */
	buf[0] = MXIC_NOR_REG_DOPI_EN;
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_MODE, 1, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, nor->reg_proto);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, nor->addr_nbytes, 4, buf,
			      SNOR_PROTO_8_8_8_DTR);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after enabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	/* Macronix SPI-NOR flash 8D-8D-8D read ID would get 6 bytes data A-A-B-B-C-C */
	for (i = 0; i < nor->info->id->len; i++)
		if (buf[i * 2] != buf[(i * 2) + 1] || buf[i * 2] != nor->info->id->bytes[i])
			return -EINVAL;

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	nor->params->wrsr_dummy = 4;

	return 0;
}

static int macronix_nor_octal_dtr_dis(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	/*
	 * The register is 1-byte wide, but 1-byte transactions are not
	 * allowed in 8D-8D-8D mode. Since there is no register at the
	 * next location, just initialize the value to 0 and let the
	 * transaction go on.
	 */
	buf[0] = MXIC_NOR_REG_SPI_EN;
	buf[1] = 0x0;
	op = (struct spi_mem_op)MXIC_NOR_WR_CR2(MXIC_NOR_ADDR_CR2_MODE, 2, buf);
	ret = spi_nor_write_any_volatile_reg(nor, &op, SNOR_PROTO_8_8_8_DTR);
	if (ret)
		return ret;

	/* Read flash ID to make sure the switch was successful. */
	ret = spi_nor_read_id(nor, 0, 0, buf, SNOR_PROTO_1_1_1);
	if (ret) {
		dev_dbg(nor->dev, "error %d reading JEDEC ID after disabling 8D-8D-8D mode\n", ret);
		return ret;
	}

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
		return -EINVAL;

	return 0;
}

static int macronix_nor_set_octal_dtr(struct spi_nor *nor, bool enable)
{
	return enable ? macronix_nor_octal_dtr_en(nor) : macronix_nor_octal_dtr_dis(nor);
}

static void macronix_nor_default_init(struct spi_nor *nor)
{
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static int macronix_nor_late_init(struct spi_nor *nor)
{
	if (!nor->params->set_4byte_addr_mode)
		nor->params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode_en4b_ex4b;
	nor->params->set_octal_dtr = macronix_nor_set_octal_dtr;

	return 0;
}

static const struct spi_nor_fixups macronix_nor_fixups = {
	.default_init = macronix_nor_default_init,
	.late_init = macronix_nor_late_init,
};

const struct spi_nor_manufacturer spi_nor_macronix = {
	.name = "macronix",
	.parts = macronix_nor_parts,
	.nparts = ARRAY_SIZE(macronix_nor_parts),
	.fixups = &macronix_nor_fixups,
};
