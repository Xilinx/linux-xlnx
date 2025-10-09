// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005, Intec Automation Inc.
 * Copyright (C) 2014, Freescale Semiconductor, Inc.
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

#define SPINOR_OP_IS_DTR_RD	0xfd	/* Fast Read opcode in DTR mode */
#define SPINOR_OP_IS_RD_ANY_REG	0x85	/* Read volatile register */
#define SPINOR_OP_IS_WR_ANY_REG	0x81	/* Write volatile register */
#define SPINOR_REG_IS_CFR0V	0x00	/* For setting octal DTR mode */
#define SPINOR_REG_IS_CFR1V	0x01	/* For setting dummy cycles */
#define SPINOR_IS_OCT_DTR	0xe7	/* Enable Octal DTR. */
#define SPINOR_IS_EXSPI		0xff	/* Enable Extended SPI (default) */

static int spi_nor_issi_phy_enable(struct spi_nor *nor)
{
	struct spi_mem_op op;
	u8 *buf = nor->bouncebuf;
	int ret;

	ret = spi_nor_write_enable(nor);
	if (ret)
		goto ret;

	buf[0] = SPINOR_IS_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_IS_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(4, SPINOR_REG_IS_CFR0V, 1),
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
static int spi_nor_issi_octal_dtr_enable(struct spi_nor *nor, bool enable)
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
			SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_IS_WR_ANY_REG, 1),
				   SPI_MEM_OP_ADDR(3, SPINOR_REG_IS_CFR1V, 1),
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
		*buf = SPINOR_IS_OCT_DTR;
	else
		*buf = SPINOR_IS_EXSPI;

	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_IS_WR_ANY_REG, 1),
			   SPI_MEM_OP_ADDR(enable ? 3 : 4,
					   SPINOR_REG_IS_CFR0V, 1),
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

static int is25wx256_set_4byte_addr_mode(struct spi_nor *nor, bool enable)
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

static void is25wx256_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	params->set_octal_dtr = spi_nor_issi_octal_dtr_enable;
	params->set_4byte_addr_mode = is25wx256_set_4byte_addr_mode;
	params->phy_enable = spi_nor_issi_phy_enable;
}

static int is25wx256_post_sfdp_fixup(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	/* Set the Fast Read settings. */
	params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_IS_DTR_RD,
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

	return 0;
}

static struct spi_nor_fixups is25wx256_fixups = {
	.default_init = is25wx256_default_init,
	.post_sfdp = is25wx256_post_sfdp_fixup,
};

static int
is25lp256_post_bfpt_fixups(struct spi_nor *nor,
			   const struct sfdp_parameter_header *bfpt_header,
			   const struct sfdp_bfpt *bfpt)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	/*
	 * IS25LP256 supports 4B opcodes, but the BFPT advertises
	 * BFPT_DWORD1_ADDRESS_BYTES_3_ONLY.
	 * Overwrite the number of address bytes advertised by the BFPT.
	 */
	if ((bfpt->dwords[SFDP_DWORD(1)] & BFPT_DWORD1_ADDRESS_BYTES_MASK) ==
		BFPT_DWORD1_ADDRESS_BYTES_3_ONLY)
		params->addr_nbytes = 4;

	return 0;
}

static const struct spi_nor_fixups is25lp256_fixups = {
	.post_bfpt = is25lp256_post_bfpt_fixups,
};

static int pm25lv_nor_late_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);
	struct spi_nor_erase_map *map = &params->erase_map;
	int i;

	/* The PM25LV series has a different 4k sector erase opcode */
	for (i = 0; i < SNOR_ERASE_TYPE_MAX; i++)
		if (map->erase_type[i].size == 4096)
			map->erase_type[i].opcode = SPINOR_OP_BE_4K_PMC;

	return 0;
}

static const struct spi_nor_fixups pm25lv_nor_fixups = {
	.late_init = pm25lv_nor_late_init,
};

static const struct flash_info issi_nor_parts[] = {
	/* ISSI */
	{
		.name = "pm25lv512",
		.sector_size = SZ_32K,
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
		.fixups = &pm25lv_nor_fixups
	}, {
		.name = "pm25lv010",
		.sector_size = SZ_32K,
		.size = SZ_128K,
		.no_sfdp_flags = SECT_4K,
		.fixups = &pm25lv_nor_fixups
	}, {
		.id = SNOR_ID(0x7f, 0x9d, 0x20),
		.name = "is25cd512",
		.sector_size = SZ_32K,
		.size = SZ_64K,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x7f, 0x9d, 0x46),
		.name = "pm25lq032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K,
	}, {
		.id = SNOR_ID(0x9d, 0x40, 0x13),
		.name = "is25lq040b",
		.size = SZ_512K,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x14),
		.name = "is25lp080d",
		.size = SZ_1M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x15),
		.name = "is25lp016d",
		.size = SZ_2M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x16),
		.name = "is25lp032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x17),
		.name = "is25lp064",
		.size = SZ_8M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x18),
		.name = "is25lp128",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x19),
		.name = "is25lp256",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
		.fixups = &is25lp256_fixups,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x1a),
		.name = "is25lp512m",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x1b),
		.name = "is25lp01g",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x21),
		.name = "is25lp01gg",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x60, 0x22),
		.name = "is25lp02g",
		.size = SZ_256M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x16),
		.name = "is25wp032",
		.size = SZ_4M,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x17),
		.size = SZ_8M,
		.name = "is25wp064",
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x18),
		.name = "is25wp128",
		.size = SZ_16M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_BP3_SR_BIT5,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x19),
		.name = "is25wp256",
		.size = SZ_32M,
		.flags = SPI_NOR_QUAD_PP | SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB |
				SPI_NOR_4BIT_BP | SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixups = &is25lp256_fixups,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x1a),
		.name = "is25wp512m",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x70, 0x1b),
		.name = "is25wp01g",
		.size = SZ_128M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_TB_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ,
		.fixup_flags = SPI_NOR_4B_OPCODES,
	}, {
		.id = SNOR_ID(0x9d, 0x5b, 0x19),
		.name = "is25wx256",
		.size = SZ_32M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
				SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
			   SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.mfr_flags = USE_FSR,
		.fixups = &is25wx256_fixups,
	}, {
		.id = SNOR_ID(0x9d, 0x5a, 0x1a),
		.name = "is25lx512m",
		.size = SZ_64M,
		.flags = SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB | SPI_NOR_4BIT_BP |
			SPI_NOR_BP3_SR_BIT6,
		.no_sfdp_flags = SECT_4K | SPI_NOR_OCTAL_READ |
				SPI_NOR_OCTAL_DTR_READ | SPI_NOR_OCTAL_DTR_PP,
		.fixup_flags = SPI_NOR_4B_OPCODES | SPI_NOR_IO_MODE_EN_VOLATILE,
		.mfr_flags = USE_FSR,
		.fixups = &is25wx256_fixups,
	}
};

static void issi_nor_default_init(struct spi_nor *nor)
{
	struct spi_nor_flash_parameter *params = spi_nor_get_params(nor, 0);

	nor->flags &= ~SNOR_F_HAS_16BIT_SR;
	params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static const struct spi_nor_fixups issi_fixups = {
	.default_init = issi_nor_default_init,
};

const struct spi_nor_manufacturer spi_nor_issi = {
	.name = "issi",
	.parts = issi_nor_parts,
	.nparts = ARRAY_SIZE(issi_nor_parts),
	.fixups = &issi_fixups,
};
