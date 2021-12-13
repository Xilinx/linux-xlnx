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

	/* Read flash ID to make sure the switch was successful. */
	op = (struct spi_mem_op)
		SPI_MEM_OP(SPI_MEM_OP_CMD(SPINOR_OP_RDID, 1),
			   SPI_MEM_OP_NO_ADDR,
			   SPI_MEM_OP_DUMMY(enable ? 8 : 0, 1),
			   SPI_MEM_OP_DATA_IN(round_up(nor->info->id_len, 2),
					      buf, 1));

	if (enable)
		spi_nor_spimem_setup_op(nor, &op, SNOR_PROTO_8_8_8_DTR);

	ret = spi_mem_exec_op(nor->spimem, &op);
	if (ret)
		return ret;

	if (memcmp(buf, nor->info->id, nor->info->id_len))
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
	nor->params->octal_dtr_enable = spi_nor_gigadevice_octal_dtr_enable;
	nor->params->set_4byte_addr_mode = gd25lx256e_set_4byte_addr_mode;
}

static void gd25lx256e_post_sfdp_fixup(struct spi_nor *nor)
{
	/* Set the Fast Read settings. */
	nor->params->hwcaps.mask |= SNOR_HWCAPS_READ_8_8_8_DTR;
	spi_nor_set_read_settings(&nor->params->reads[SNOR_CMD_READ_8_8_8_DTR],
				  0, 20, SPINOR_OP_GD_DTR_RD,
				  SNOR_PROTO_8_8_8_DTR);

	nor->cmd_ext_type = SPI_NOR_EXT_REPEAT;
	nor->params->rdsr_dummy = 8;
	nor->params->rdsr_addr_nbytes = 0;

	/*
	 * The BFPT quad enable field is set to a reserved value so the quad
	 * enable function is ignored by spi_nor_parse_bfpt(). Make sure we
	 * disable it.
	 */
	nor->params->quad_enable = NULL;
}

static struct spi_nor_fixups gd25lx256e_fixups = {
	.default_init = gd25lx256e_default_init,
	.post_sfdp = gd25lx256e_post_sfdp_fixup,
};

static void gd25q256_default_init(struct spi_nor *nor)
{
	/*
	 * Some manufacturer like GigaDevice may use different
	 * bit to set QE on different memories, so the MFR can't
	 * indicate the quad_enable method for this case, we need
	 * to set it in the default_init fixup hook.
	 */
	nor->params->quad_enable = spi_nor_sr1_bit6_quad_enable;
}

static struct spi_nor_fixups gd25q256_fixups = {
	.default_init = gd25q256_default_init,
};

static const struct flash_info gigadevice_parts[] = {
	{ "gd25q16", INFO(0xc84015, 0, 64 * 1024,  32,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q32", INFO(0xc84016, 0, 64 * 1024,  64,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq32", INFO(0xc86016, 0, 64 * 1024, 64,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q64", INFO(0xc84017, 0, 64 * 1024, 128,
			  SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			  SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq64c", INFO(0xc86017, 0, 64 * 1024, 128,
			    SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			    SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25lq128d", INFO(0xc86018, 0, 64 * 1024, 256,
			     SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			     SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q128", INFO(0xc84018, 0, 64 * 1024, 256,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB) },
	{ "gd25q256", INFO(0xc84019, 0, 64 * 1024, 512,
			   SECT_4K | SPI_NOR_DUAL_READ | SPI_NOR_QUAD_READ |
			   SPI_NOR_4B_OPCODES | SPI_NOR_HAS_LOCK |
			   SPI_NOR_HAS_TB | SPI_NOR_TB_SR_BIT6)
		.fixups = &gd25q256_fixups },
	{ "gd25lx256e",  INFO(0xc86819, 0, 64 * 1024, 512,
			      SECT_4K | USE_FSR | SPI_NOR_OCTAL_READ |
			      SPI_NOR_4B_OPCODES | SPI_NOR_OCTAL_DTR_READ |
			      SPI_NOR_OCTAL_DTR_PP |
			      SPI_NOR_IO_MODE_EN_VOLATILE)
		.fixups = &gd25lx256e_fixups },
};

const struct spi_nor_manufacturer spi_nor_gigadevice = {
	.name = "gigadevice",
	.parts = gigadevice_parts,
	.nparts = ARRAY_SIZE(gigadevice_parts),
};
