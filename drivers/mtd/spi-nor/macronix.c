// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SPINOR_OP_MX_DTR_RD	0xee	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_MX_RD_ANY_REG	0x71	/* Read volatile register */
#define SPINOR_OP_MX_WR_ANY_REG	0x72	/* Write volatile register */
#define SPINOR_REG_MX_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_MX_OCT_DTR	0x02	/* Enable Octal DTR. */
#define SPINOR_MX_EXSPI		0x00	/* Enable Extended SPI (default) */
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

	buf[0] = SPINOR_MX_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MX_WR_ANY_REG, 1),
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

static int spi_nor_macronix_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (enable)
		*buf = SPINOR_MX_OCT_DTR;
	else
		*buf = SPINOR_MX_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MX_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_MX_CFR0V, 1),
			   SPI_MEM_OP_NO_DUMMY,
			   SPI_MEM_OP_DATA_OUT(1, buf, 1));

	if (!enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if ((nor->flags & SNOR_F_HAS_STACKED) && nor->spimem->spi->cs_index_mask == 1)
		return 0;

	/* Read flash ID to make sure the switch was successful. */
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 1),
			   SPI_MEM_OP_ADDR(enable ? 4 : 0, 0, enable ? 1 : 0),
			   SPI_MEM_OP_DUMMY(enable ? 4 : 0, 1),
			   SPI_MEM_OP_DATA_IN(round_up(nor->info->id->len, 2),
					      buf, 1));

	if (enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (enable) {
		if (memcmp(buf, nor->spimem->device_id, nor->info->id->len))
			return -EINVAL;
	} else {
		if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
			return -EINVAL;
	}

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	params->wrsr_dummy = 4;

	return 0;
}

static int mx25um51345g_set_4byte(struct spi_nor *nor, bool enable)
{
	(void)enable;

	return 0;
}

static void mx25um51345g_default_init_fixups(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);
	u8 id_byte1, id_byte2;

	params->set_4byte_addr_mode = mx25um51345g_set_4byte;

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

	spi_nor_set_erase_type(&params->erase_map.erase_type[1],
			       nor->info->sector_size, SPINOR_OP_BE_4K_4B);
	params->page_programs[SNOR_CMD_PP_8_8_8_DTR].opcode =
				SPINOR_OP_PP_4B;

	params->set_octal_dtr = spi_nor_macronix_octal_dtr_enable;
	params->phy_enable = spi_nor_macronix_phy_enable;
}

static int mx25um51345g_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

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

	return 0;
}

static int mx25um51345g_config_dummy(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);
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
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_MX_WR_ANY_REG, 1),
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

static const struct spi_nor_fixups mx25l25635_fixups = {
	.post_bfpt = mx25l25635_post_bfpt_fixups,
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
		.id = SNOR_ID(0xc2, 0x20, 0x15),
		.name = "mx25l1606e",
		.size = SZ_2M,
		.no_sfdp_flags = SECT_4K,
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
		.id = SNOR_ID(0xc2, 0x20, 0x18),
		.name = "mx25l12805d",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_4BIT_BP,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x19),
		.name = "mx25l25635e",
		.size = SZ_32M,
		.no_sfdp_flags = SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixups = &mx25l25635_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x1a),
		.name = "mx66l51235f",
		.size = SZ_64M,
		.no_sfdp_flags = SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x20, 0x1b),
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
		.id = SNOR_ID(0xc2, 0x25, 0x39),
		.name = "mx25u25635f",
		.size = SZ_32M,
		.no_sfdp_flags = SECT_4K,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x3a),
		.name = "mx25u51245g",
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x3a),
		.name = "mx66u51235f",
		.size = SZ_64M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc2, 0x25, 0x3c),
		.name = "mx66u2g45g",
		.size = SZ_256M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
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
		.size = SZ_256,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5 | SPI_NOR_HAS_CR_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &mx25um51345g_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x94, 0x3c),
		.name = "mx66uw2g345gxrix0",
		.size = SZ_256,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
			SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5 | SPI_NOR_HAS_CR_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &mx25uw51345g_fixups
	}, {
		.id = SNOR_ID(0xc2, 0x81, 0x3a),
		.name = "mx25um51345g",
		.size = SZ_4M,
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
		.id = SNOR_ID(0xc2, 0x81, 0x3a),
		.name = "mx25uw51245g",
		.n_banks = 4,
		.flags = SPI_NOR_RWW,
	}, {
		.id = SNOR_ID(0xc2, 0x9e, 0x16),
		.name = "mx25l3255e",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	},
};

static void macronix_nor_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static int macronix_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	if (!params->set_4byte_addr_mode)
		params->set_4byte_addr_mode = spi_nor_set_4byte_addr_mode_en4b_ex4b;

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
