// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SPINOR_OP_GD_DTR_RD	0xfd	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_GD_RD_ANY_REG	0x85	/* Read volatile register */
#define SPINOR_OP_GD_WR_ANY_REG	0x81	/* Write volatile register */
#define SPINOR_REG_GD_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_REG_GD_CFR1V	0x01	/* For setting dummy cycles */
#define SPINOR_GD_OCT_DTR	0xe7	/* Enable Octal DTR. */
#define SPINOR_GD_EXSPI		0xff	/* Enable Extended SPI (default) */

static int spi_nor_gigadevice_phy_enable(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		goto ret;

	buf[0] = SPINOR_GD_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_GD_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_GD_CFR0V, 1),
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

static int spi_nor_gigadevice_octal_dtr_enable(struct spi_nor *nor, bool enable)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	if (enable) {
		/* Use 20 dummy cycles for memory array reads. */
		ret = spi_nor_write_enable(nor);
		if (ret)
			return ret;

		*buf = 20;
		op = (struct spi_mem_op)
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_GD_WR_ANY_REG, 1),
				   SPI_MEM_OP_ADDR(3, SPINOR_REG_GD_CFR1V, 1),
				   SPI_MEM_OP_NO_DUMMY,
				   SPI_MEM_OP_DATA_OUT(1, buf, 1));

		ret = spi_mem_exec_op(nor->spimem, &op);
		if (ret)
			return ret;

		ret = spi_nor_wait_till_ready(nor);
		if (ret)
			return ret;
	}

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	if (enable)
		*buf = SPINOR_GD_OCT_DTR;
	else
		*buf = SPINOR_GD_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_GD_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(enable ? 3 : 4,
					   SPINOR_REG_GD_CFR0V, 1),
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
			   SPI_MEM_OP_NO_ADDR,
			   SPI_MEM_OP_DUMMY(enable ? 8 : 0, 1),
			   SPI_MEM_OP_DATA_IN(round_up(nor->info->id->len, 2),
					      buf, 1));

	if (enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (memcmp(buf, nor->info->id->bytes, nor->info->id->len))
		return -EINVAL;

	return 0;
}

static int gd25lx256e_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
{
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		return ret;

	ret = spi_nor_set_4byte_addr_mode(nor, enable);
	if (ret)
		return ret;

	return spi_nor_write_disable(nor);
}

static void gd25lx256e_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	params->set_octal_dtr = spi_nor_gigadevice_octal_dtr_enable;
	params->set_4byte_addr_mode = gd25lx256e_set_4byte_addr_mode;
	params->phy_enable = spi_nor_gigadevice_phy_enable;
}

static int gd25lx256e_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	/* Set the Fast Read settings. */
	params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_GD_DTR_RD,
				  SNOR_PROTO_8_8_8_DTR);

	nor->cmd_ext_type = SPI_NOR_EXT_REPEAT;
	params->rdsr_dummy = 8;
	params->rdsr_addr_nbytes = 0;

	/*
	 * The BFPT quad enable field is set to a reserved value so the quad
	 * enable function is ignored by spi_nor_parse_bfpt(). Make sure we
	 * disable it.
	 */
	params->quad_enable = NULL;
	params->n_dice = 1;

	return 0;
}

static void gd25lx512_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	params->set_octal_dtr = spi_nor_gigadevice_octal_dtr_enable;
	params->set_4byte_addr_mode = gd25lx256e_set_4byte_addr_mode;
}

static void gd25b512_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	params->set_4byte_addr_mode = gd25lx256e_set_4byte_addr_mode;
}

static struct spi_nor_fixups gd25lx256e_fixups = {
	.default_init = gd25lx256e_default_init,
	.post_sfdp = gd25lx256e_post_sfdp_fixup,
};

static struct spi_nor_fixups gd25b512_fixups = {
	.default_init = gd25b512_default_init,
};

static struct spi_nor_fixups gd25lx512_fixups = {
	.default_init = gd25lx512_default_init,
	.post_sfdp = gd25lx256e_post_sfdp_fixup
};

static int
gd25q256_post_bfpt(struct spi_nor *nor,
		   const struct sfdp_parameter_header *bfpt_header,
		   const struct sfdp_bfpt *bfpt)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	/*
	 * GD25Q256C supports the first version of JESD216 which does not define
	 * the Quad Enable methods. Overwrite the default Quad Enable method.
	 *
	 * GD25Q256 GENERATION | SFDP MAJOR VERSION | SFDP MINOR VERSION
	 *      GD25Q256C      | SFDP_JESD216_MAJOR | SFDP_JESD216_MINOR
	 *      GD25Q256D      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MINOR
	 *      GD25Q256E      | SFDP_JESD216_MAJOR | SFDP_JESD216B_MINOR
	 */
	if (bfpt_header->major == SFDP_JESD216_MAJOR &&
	    bfpt_header->minor == SFDP_JESD216_MINOR)
		params->quad_enable = spi_nor_sr1_bit6_quad_enable;

	return 0;
}

static const struct spi_nor_fixups gd25q256_fixups = {
	.post_bfpt = gd25q256_post_bfpt,
};

static const struct flash_info gigadevice_nor_parts[] = {
	{
		.id = SNOR_ID(0xc8, 0x40, 0x15),
		.name = "gd25q16",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x16),
		.name = "gd25q32",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x17),
		.name = "gd25q64",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x18),
		.name = "gd25q128",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x40, 0x19),
		.name = "gd25q256",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6,
		.fixups = &gd25q256_fixups,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x16),
		.name = "gd25lq32",
		.size = SZ_4M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x17),
		.name = "gd25lq64c",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0xc8, 0x60, 0x18),
		.name = "gd25lq128d",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			   SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
	}, {
		.id = SNOR_ID(0xc8, 0x68, 0x19),
		.name = "gd25lx256e",
		.size = SZ_32M,
		.mfr_flags = USE_FSR,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			   SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &gd25lx256e_fixups,
	}, {
		.id = SNOR_ID(0xc8, 0x68, 0x1a),
		.name = "gd25lx512m",
		.size = SZ_64M,
		.mfr_flags = USE_FSR,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
				SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
					SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &gd25lx512_fixups,
	}, {
		.id = SNOR_ID(0xc8, 0x68, 0x1b),
		.name = "gd55lx01g",
		.size = SZ_128M,
		.mfr_flags = USE_FSR,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
				SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
					SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &gd25lx512_fixups,
	}, {
		.id = SNOR_ID(0xc8, 0x68, 0x1c),
		.name = "gd55lx02g",
		.size = SZ_128M,
		.mfr_flags = USE_FSR,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
				SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
					SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.fixups = &gd25lx512_fixups,
	}, {
		.id = SNOR_ID(0xc8, 0x47, 0x1a),
		.name = "gd25b512",
		.size = SZ_64M,
		.mfr_flags = USE_FSR,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6 |
		      SPI_NOR_4BIT_BP | SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.fixups = &gd25b512_fixups,
	}
};

const struct spi_nor_manufacturer spi_nor_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_nor_parts,
	.nparts = ARRAY_SIZE(gigadevice_nor_parts),
};
