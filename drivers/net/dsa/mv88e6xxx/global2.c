/*
 * Marvell 88E6xxx Switch Global 2 Registers support (device address 0x1C)
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mv88e6xxx.h"
#include "global2.h"

#define ADDR_GLOBAL2	0x1c

static int mv88e6xxx_g2_read(struct mv88e6xxx_chip *chip, int reg, u16 *val)
{
	return mv88e6xxx_read(chip, ADDR_GLOBAL2, reg, val);
}

static int mv88e6xxx_g2_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	return mv88e6xxx_write(chip, ADDR_GLOBAL2, reg, val);
}

static int mv88e6xxx_g2_update(struct mv88e6xxx_chip *chip, int reg, u16 update)
{
	return mv88e6xxx_update(chip, ADDR_GLOBAL2, reg, update);
}

static int mv88e6xxx_g2_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask)
{
	return mv88e6xxx_wait(chip, ADDR_GLOBAL2, reg, mask);
}

/* Offset 0x06: Device Mapping Table register */

static int mv88e6xxx_g2_device_mapping_write(struct mv88e6xxx_chip *chip,
					     int target, int port)
{
	u16 val = (target << 8) | (port & 0xf);

	return mv88e6xxx_g2_update(chip, GLOBAL2_DEVICE_MAPPING, val);
}

static int mv88e6xxx_g2_set_device_mapping(struct mv88e6xxx_chip *chip)
{
	int target, port;
	int err;

	/* Initialize the routing port to the 32 possible target devices */
	for (target = 0; target < 32; ++target) {
		port = 0xf;

		if (target < DSA_MAX_SWITCHES) {
			port = chip->ds->rtable[target];
			if (port == DSA_RTABLE_NONE)
				port = 0xf;
		}

		err = mv88e6xxx_g2_device_mapping_write(chip, target, port);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x07: Trunk Mask Table register */

static int mv88e6xxx_g2_trunk_mask_write(struct mv88e6xxx_chip *chip, int num,
					 bool hask, u16 mask)
{
	const u16 port_mask = BIT(mv88e6xxx_num_ports(chip)) - 1;
	u16 val = (num << 12) | (mask & port_mask);

	if (hask)
		val |= GLOBAL2_TRUNK_MASK_HASK;

	return mv88e6xxx_g2_update(chip, GLOBAL2_TRUNK_MASK, val);
}

/* Offset 0x08: Trunk Mapping Table register */

static int mv88e6xxx_g2_trunk_mapping_write(struct mv88e6xxx_chip *chip, int id,
					    u16 map)
{
	const u16 port_mask = BIT(mv88e6xxx_num_ports(chip)) - 1;
	u16 val = (id << 11) | (map & port_mask);

	return mv88e6xxx_g2_update(chip, GLOBAL2_TRUNK_MAPPING, val);
}

static int mv88e6xxx_g2_clear_trunk(struct mv88e6xxx_chip *chip)
{
	const u16 port_mask = BIT(mv88e6xxx_num_ports(chip)) - 1;
	int i, err;

	/* Clear all eight possible Trunk Mask vectors */
	for (i = 0; i < 8; ++i) {
		err = mv88e6xxx_g2_trunk_mask_write(chip, i, false, port_mask);
		if (err)
			return err;
	}

	/* Clear all sixteen possible Trunk ID routing vectors */
	for (i = 0; i < 16; ++i) {
		err = mv88e6xxx_g2_trunk_mapping_write(chip, i, 0);
		if (err)
			return err;
	}

	return 0;
}

/* Offset 0x09: Ingress Rate Command register
 * Offset 0x0A: Ingress Rate Data register
 */

static int mv88e6xxx_g2_clear_irl(struct mv88e6xxx_chip *chip)
{
	int port, err;

	/* Init all Ingress Rate Limit resources of all ports */
	for (port = 0; port < mv88e6xxx_num_ports(chip); ++port) {
		/* XXX newer chips (like 88E6390) have different 2-bit ops */
		err = mv88e6xxx_g2_write(chip, GLOBAL2_IRL_CMD,
					 GLOBAL2_IRL_CMD_OP_INIT_ALL |
					 (port << 8));
		if (err)
			break;

		/* Wait for the operation to complete */
		err = mv88e6xxx_g2_wait(chip, GLOBAL2_IRL_CMD,
					GLOBAL2_IRL_CMD_BUSY);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x0D: Switch MAC/WoL/WoF register */

static int mv88e6xxx_g2_switch_mac_write(struct mv88e6xxx_chip *chip,
					 unsigned int pointer, u8 data)
{
	u16 val = (pointer << 8) | data;

	return mv88e6xxx_g2_update(chip, GLOBAL2_SWITCH_MAC, val);
}

int mv88e6xxx_g2_set_switch_mac(struct mv88e6xxx_chip *chip, u8 *addr)
{
	int i, err;

	for (i = 0; i < 6; i++) {
		err = mv88e6xxx_g2_switch_mac_write(chip, i, addr[i]);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x0F: Priority Override Table */

static int mv88e6xxx_g2_pot_write(struct mv88e6xxx_chip *chip, int pointer,
				  u8 data)
{
	u16 val = (pointer << 8) | (data & 0x7);

	return mv88e6xxx_g2_update(chip, GLOBAL2_PRIO_OVERRIDE, val);
}

static int mv88e6xxx_g2_clear_pot(struct mv88e6xxx_chip *chip)
{
	int i, err;

	/* Clear all sixteen possible Priority Override entries */
	for (i = 0; i < 16; i++) {
		err = mv88e6xxx_g2_pot_write(chip, i, 0);
		if (err)
			break;
	}

	return err;
}

/* Offset 0x14: EEPROM Command
 * Offset 0x15: EEPROM Data
 */

static int mv88e6xxx_g2_eeprom_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, GLOBAL2_EEPROM_CMD,
				 GLOBAL2_EEPROM_CMD_BUSY |
				 GLOBAL2_EEPROM_CMD_RUNNING);
}

static int mv88e6xxx_g2_eeprom_cmd(struct mv88e6xxx_chip *chip, u16 cmd)
{
	int err;

	err = mv88e6xxx_g2_write(chip, GLOBAL2_EEPROM_CMD, cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_eeprom_wait(chip);
}

static int mv88e6xxx_g2_eeprom_read16(struct mv88e6xxx_chip *chip,
				      u8 addr, u16 *data)
{
	u16 cmd = GLOBAL2_EEPROM_CMD_OP_READ | addr;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_eeprom_cmd(chip, cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_read(chip, GLOBAL2_EEPROM_DATA, data);
}

static int mv88e6xxx_g2_eeprom_write16(struct mv88e6xxx_chip *chip,
				       u8 addr, u16 data)
{
	u16 cmd = GLOBAL2_EEPROM_CMD_OP_WRITE | addr;
	int err;

	err = mv88e6xxx_g2_eeprom_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, GLOBAL2_EEPROM_DATA, data);
	if (err)
		return err;

	return mv88e6xxx_g2_eeprom_cmd(chip, cmd);
}

int mv88e6xxx_g2_get_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	u16 val;
	int err;

	eeprom->len = 0;

	if (offset & 1) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = (val >> 8) & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = val & 0xff;
		*data++ = (val >> 8) & 0xff;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		*data++ = val & 0xff;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

int mv88e6xxx_g2_set_eeprom16(struct mv88e6xxx_chip *chip,
			      struct ethtool_eeprom *eeprom, u8 *data)
{
	unsigned int offset = eeprom->offset;
	unsigned int len = eeprom->len;
	u16 val;
	int err;

	/* Ensure the RO WriteEn bit is set */
	err = mv88e6xxx_g2_read(chip, GLOBAL2_EEPROM_CMD, &val);
	if (err)
		return err;

	if (!(val & GLOBAL2_EEPROM_CMD_WRITE_EN))
		return -EROFS;

	eeprom->len = 0;

	if (offset & 1) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		val = (*data++ << 8) | (val & 0xff);

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset++;
		len--;
		eeprom->len++;
	}

	while (len >= 2) {
		val = *data++;
		val |= *data++ << 8;

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset += 2;
		len -= 2;
		eeprom->len += 2;
	}

	if (len) {
		err = mv88e6xxx_g2_eeprom_read16(chip, offset >> 1, &val);
		if (err)
			return err;

		val = (val & 0xff00) | *data++;

		err = mv88e6xxx_g2_eeprom_write16(chip, offset >> 1, val);
		if (err)
			return err;

		offset++;
		len--;
		eeprom->len++;
	}

	return 0;
}

/* Offset 0x18: SMI PHY Command Register
 * Offset 0x19: SMI PHY Data Register
 */

static int mv88e6xxx_g2_smi_phy_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g2_wait(chip, GLOBAL2_SMI_PHY_CMD,
				 GLOBAL2_SMI_PHY_CMD_BUSY);
}

static int mv88e6xxx_g2_smi_phy_cmd(struct mv88e6xxx_chip *chip, u16 cmd)
{
	int err;

	err = mv88e6xxx_g2_write(chip, GLOBAL2_SMI_PHY_CMD, cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_wait(chip);
}

int mv88e6xxx_g2_smi_phy_read(struct mv88e6xxx_chip *chip, int addr, int reg,
			      u16 *val)
{
	u16 cmd = GLOBAL2_SMI_PHY_CMD_OP_22_READ_DATA | (addr << 5) | reg;
	int err;

	err = mv88e6xxx_g2_smi_phy_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_smi_phy_cmd(chip, cmd);
	if (err)
		return err;

	return mv88e6xxx_g2_read(chip, GLOBAL2_SMI_PHY_DATA, val);
}

int mv88e6xxx_g2_smi_phy_write(struct mv88e6xxx_chip *chip, int addr, int reg,
			       u16 val)
{
	u16 cmd = GLOBAL2_SMI_PHY_CMD_OP_22_WRITE_DATA | (addr << 5) | reg;
	int err;

	err = mv88e6xxx_g2_smi_phy_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g2_write(chip, GLOBAL2_SMI_PHY_DATA, val);
	if (err)
		return err;

	return mv88e6xxx_g2_smi_phy_cmd(chip, cmd);
}

int mv88e6xxx_g2_setup(struct mv88e6xxx_chip *chip)
{
	u16 reg;
	int err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_2X)) {
		/* Consider the frames with reserved multicast destination
		 * addresses matching 01:80:c2:00:00:2x as MGMT.
		 */
		err = mv88e6xxx_g2_write(chip, GLOBAL2_MGMT_EN_2X, 0xffff);
		if (err)
			return err;
	}

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_0X)) {
		/* Consider the frames with reserved multicast destination
		 * addresses matching 01:80:c2:00:00:0x as MGMT.
		 */
		err = mv88e6xxx_g2_write(chip, GLOBAL2_MGMT_EN_0X, 0xffff);
		if (err)
			return err;
	}

	/* Ignore removed tag data on doubly tagged packets, disable
	 * flow control messages, force flow control priority to the
	 * highest, and send all special multicast frames to the CPU
	 * port at the highest priority.
	 */
	reg = GLOBAL2_SWITCH_MGMT_FORCE_FLOW_CTRL_PRI | (0x7 << 4);
	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_0X) ||
	    mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_MGMT_EN_2X))
		reg |= GLOBAL2_SWITCH_MGMT_RSVD2CPU | 0x7;
	err = mv88e6xxx_g2_write(chip, GLOBAL2_SWITCH_MGMT, reg);
	if (err)
		return err;

	/* Program the DSA routing table. */
	err = mv88e6xxx_g2_set_device_mapping(chip);
	if (err)
		return err;

	/* Clear all trunk masks and mapping. */
	err = mv88e6xxx_g2_clear_trunk(chip);
	if (err)
		return err;

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAGS_IRL)) {
		/* Disable ingress rate limiting by resetting all per port
		 * ingress rate limit resources to their initial state.
		 */
		err = mv88e6xxx_g2_clear_irl(chip);
			if (err)
				return err;
	}

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAGS_PVT)) {
		/* Initialize Cross-chip Port VLAN Table to reset defaults */
		err = mv88e6xxx_g2_write(chip, GLOBAL2_PVT_ADDR,
					 GLOBAL2_PVT_ADDR_OP_INIT_ONES);
		if (err)
			return err;
	}

	if (mv88e6xxx_has(chip, MV88E6XXX_FLAG_G2_POT)) {
		/* Clear the priority override table. */
		err = mv88e6xxx_g2_clear_pot(chip);
		if (err)
			return err;
	}

	return 0;
}
