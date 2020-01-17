// SPDX-License-Identifier: GPL-2.0
/* clk-idt8t49n24x-debugfs.c - Debugfs support for 8T49N24x
 *
 * Copyright (C) 2018, Integrated Device Technology, Inc. <david.cater@idt.com>
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 * This program is distributed "AS IS" and  WITHOUT ANY WARRANTY;
 * including the implied warranties of MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, or NON-INFRINGEMENT.
 */

#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "clk-idt8t49n24x-debugfs.h"

static struct clk_idt24x_chip *idt24x_chip_fordebugfs;

static int idt24x_read_all_settings(
	struct clk_idt24x_chip *chip, char *output_buffer, int count)
{
	u8 settings[NUM_CONFIG_REGISTERS];
	int err = 0;
	int x;

	err = regmap_bulk_read(
		chip->regmap, 0x0, settings, NUM_CONFIG_REGISTERS);
	if (!err) {
		output_buffer[0] = '\0';
		for (x = 0; x < ARRAY_SIZE(settings); x++) {
			char dbg[4];

			if ((strlen(output_buffer) + 4) > count)
				return -EINVAL;
			sprintf(dbg, "%02x ", settings[x]);
			strcat(output_buffer, dbg);
		}
	}
	return err;
}

/**
 * idt24x_debugfs_writer_action - Write handler for the "action" debugfs file.
 * @fp:			file pointer
 * @user_buffer:	buffer of text written to file
 * @count:		size of text in buffer
 * @position:		pass in current position, return new position
 *
 * Return: result of call to simple_write_to_buffer
 *
 * Use the "action" file as a trigger for setting all requested
 * rates. The driver doesn't get any notification when the files
 * representing the Qx outputs are written to, so something else is
 * needed to notify the driver that the device should be udpated.
 *
 * It doesn't matter what you write to the action debugs file. When the
 * handler is called, the device will be updated.
 */
static ssize_t idt24x_debugfs_writer_action(
	struct file *fp, const char __user *user_buffer,
	size_t count, loff_t *position)
{
	int err = 0;
	int x;
	u32 freq;
	bool needs_update = true;
	struct i2c_client *client = idt24x_chip_fordebugfs->i2c_client;

	if (count > DEBUGFS_BUFFER_LENGTH)
		return -EINVAL;

	for (x = 0; x < NUM_OUTPUTS; x++) {
		freq = idt24x_chip_fordebugfs->clk[x].debug_freq;
		if (freq) {
			needs_update = false;
			dev_dbg(&client->dev,
				"%s: calling clk_set_rate with debug frequency for Q%i",
				__func__, x);
			err = clk_set_rate(
				idt24x_chip_fordebugfs->clk[x].hw.clk, freq);
			if (err) {
				dev_err(&client->dev,
					"error calling clk_set_rate for Q%i (%i)\n",
					x, err);
			}
		} else {
			needs_update = true;
			idt24x_chip_fordebugfs->clk[x].requested = 0;
			dev_dbg(&client->dev,
				"%s: debug frequency for Q%i not set; make sure clock is disabled",
				__func__, x);
		}
	}

	if (needs_update) {
		dev_dbg(&client->dev,
			"%s: calling idt24x_set_frequency to ensure any clocks that should be disabled are turned off.",
			__func__);
		err = idt24x_set_frequency(idt24x_chip_fordebugfs);
		if (err) {
			dev_err(&idt24x_chip_fordebugfs->i2c_client->dev,
				"%s: error calling idt24x_set_frequency (%i)\n",
				__func__, err);
			return err;
		}
	}

	return simple_write_to_buffer(
		idt24x_chip_fordebugfs->dbg_cache, DEBUGFS_BUFFER_LENGTH,
		position, user_buffer, count);
}

/**
 * idt24x_debugfs_reader_action - Read the "action" debugfs file.
 * @fp:			file pointer
 * @user_buffer:	buffer of text written to file
 * @count:		size of text in buffer
 * @position:		pass in current position, return new position
 *
 * Return: whatever was last written to the "action" debugfs file.
 */
static ssize_t idt24x_debugfs_reader_action(
	struct file *fp, char __user *user_buffer, size_t count,
	loff_t *position)
{
	return simple_read_from_buffer(
		user_buffer, count, position, idt24x_chip_fordebugfs->dbg_cache,
		DEBUGFS_BUFFER_LENGTH);
}

/**
 * idt24x_debugfs_reader_map - display the current registers on the device
 * @fp:			file pointer
 * @user_buffer:	buffer of text written to file
 * @count:		size of text in buffer
 * @position:		pass in current position, return new position
 *
 * Reads the current register map from the attached chip via I2C and
 * returns it.
 *
 * Return: result of call to simple_read_from_buffer
 */
static ssize_t idt24x_debugfs_reader_map(
	struct file *fp, char __user *user_buffer, size_t count,
	loff_t *position)
{
	int err = 0;
	char *buf = kzalloc(5000, GFP_KERNEL);

	dev_dbg(&idt24x_chip_fordebugfs->i2c_client->dev,
		"calling idt24x_read_all_settings (count: %zu)\n", count);
	err = idt24x_read_all_settings(idt24x_chip_fordebugfs, buf, 5000);
	if (err) {
		dev_err(&idt24x_chip_fordebugfs->i2c_client->dev,
			"error calling idt24x_read_all_settings (%i)\n", err);
		return 0;
	}
	/* TMGCDR-1456. We're returning 1 byte too few. */
	err = simple_read_from_buffer(
		user_buffer, count, position, buf, strlen(buf));
	kfree(buf);
	return err;
}

/**
 * idt24x_handle_i2c_debug_token - process "token" written to the i2c file
 * @dev:	pointer to device structure
 * @token:	pointer to current char being examined
 * @reg:	pass in current register, or return register from token.
 * @val:	resulting array of bytes being parsed
 * @nextbyte:	position in val array to store next byte
 *
 * Utility function to operate on the current "token" (from within a
 * space-delimited string) written to the i2c debugfs file. It will
 * either be a register offset or a byte to be added to the val array.
 * If it is added to the val array, auto-increment nextbyte.
 *
 * Return:	0 for success
 */
static int idt24x_handle_i2c_debug_token(
	const struct device *dev, char *token, unsigned int *reg,
	u8 val[], u16 *nextbyte)
{
	int err = 0;

	dev_dbg(dev, "got token (%s)\n", token);
	if (*reg == -1) {
		err = kstrtouint(token, 16, reg);
		if (!err)
			dev_dbg(dev, "hex register address == 0x%x\n", *reg);
	} else {
		u8 temp;

		err = kstrtou8(token, 16, &temp);
		if (!err) {
			dev_dbg(dev, "data byte == 0x%x\n", temp);
			val[*nextbyte] = temp;
			*nextbyte += 1;
		}
	}
	if (err == -ERANGE)
		dev_err(dev, "ERANGE error when parsing data\n");
	else if (err == -EINVAL)
		dev_err(dev, "EINVAL error when parsing data\n");
	else if (err)
		dev_err(dev, "error when parsing data: %i\n", err);
	return err;
}

/**
 * idt24x_debugfs_writer_i2c - debugfs handler for i2c file
 * @fp:			file pointer
 * @user_buffer:	buffer of text written to file
 * @count:		size of text in buffer
 * @position:		pass in current position, return new position
 *
 * Handler for the "i2c" debugfs file. Write to this file to write bytes
 * via I2C to a particular offset.
 *
 * Usage: echo 006c 01 02 0D FF > i2c
 *
 * First 4 chars are the 2-byte i2c register offset. Then follow that
 * with a sequence of 2-char bytes in hex format that you want to write
 * starting at that offset.
 *
 * Return: result of simple_write_to_buffer
 */
static ssize_t idt24x_debugfs_writer_i2c(struct file *fp,
					 const char __user *user_buffer,
					 size_t count, loff_t *position)
{
	int err = 0;
	int x = 0;
	int start = 0;
	ssize_t written;
	unsigned int reg = -1;
	u8 val[WRITE_BLOCK_SIZE];
	u16 nextbyte = 0;
	char token[16];

	if (count > DEBUGFS_BUFFER_LENGTH)
		return -EINVAL;

	written = simple_write_to_buffer(
		idt24x_chip_fordebugfs->dbg_cache, DEBUGFS_BUFFER_LENGTH,
		position, user_buffer, count);
	if (written != count) {
		dev_dbg(&idt24x_chip_fordebugfs->i2c_client->dev,
			"write count != expected count");
		return written;
	}

	for (x = 0; x < count; x++) {
		token[x - start] = idt24x_chip_fordebugfs->dbg_cache[x];
		if (idt24x_chip_fordebugfs->dbg_cache[x] == ' ') {
			token[x - start] = '\0';
			err = idt24x_handle_i2c_debug_token(
				&idt24x_chip_fordebugfs->i2c_client->dev,
				token, &reg, val, &nextbyte);
			if (err)
				break;
			start = x + 1;
		}
	}

	/* handle the last token */
	if (!err) {
		token[count - start] = '\0';
		err = idt24x_handle_i2c_debug_token(
			&idt24x_chip_fordebugfs->i2c_client->dev, token, &reg,
			val, &nextbyte);
	}

	if (!err && reg != -1 && nextbyte > 0) {
		err = i2cwritebulk(
			idt24x_chip_fordebugfs->i2c_client,
			idt24x_chip_fordebugfs->regmap,
			reg, val, nextbyte);
		if (err) {
			dev_err(&idt24x_chip_fordebugfs->i2c_client->dev,
				"error writing data chip (%i)\n", err);
			return err;
		}
		dev_dbg(&idt24x_chip_fordebugfs->i2c_client->dev,
			"successfully wrote i2c data to chip");
	}

	return written;
}

static const struct file_operations idt24x_fops_debug_action = {
	.read = idt24x_debugfs_reader_action,
	.write = idt24x_debugfs_writer_action,
};

static const struct file_operations idt24x_fops_debug_map = {
	.read = idt24x_debugfs_reader_map
};

static const struct file_operations idt24x_fops_debug_i2c = {
	.write = idt24x_debugfs_writer_i2c,
};

/**
 * idt24x_expose_via_debugfs - Set up all debugfs files
 * @client:	pointer to i2c_client structure
 * @chip:	Device data structure
 *
 * Sets up all debugfs files to use for debugging the driver.
 * Return: error code. 0 if success or debugfs doesn't appear to be enabled.
 */
int idt24x_expose_via_debugfs(struct i2c_client *client,
			      struct clk_idt24x_chip *chip)
{
	int output_num;

	/*
	 * create root directory in /sys/kernel/debugfs
	 */
	chip->debugfs_dirroot = debugfs_create_dir("idt24x", NULL);
	if (!chip->debugfs_dirroot) {
		/* debugfs probably not enabled. Don't fail the probe. */
		return 0;
	}

	/*
	 * create files in the root directory. This requires read and
	 * write file operations
	 */
	chip->debugfs_fileaction = debugfs_create_file(
		"action", 0644, chip->debugfs_dirroot, NULL,
		&idt24x_fops_debug_action);
	if (!chip->debugfs_fileaction) {
		dev_err(&client->dev,
			"%s: error creating action file", __func__);
		return (-ENODEV);
	}

	chip->debugfs_map = debugfs_create_file(
		"map", 0444, chip->debugfs_dirroot, NULL,
		&idt24x_fops_debug_map);
	if (!chip->debugfs_map) {
		dev_err(&client->dev,
			"%s: error creating map file", __func__);
		return (-ENODEV);
	}

	for (output_num = 0; output_num < NUM_OUTPUTS; output_num++) {
		char name[5];

		sprintf(name, "q%d", output_num);
		chip->debugfs_fileqfreq[output_num] = debugfs_create_u64(
			name, 0644, chip->debugfs_dirroot,
			&chip->clk[output_num].debug_freq);
		if (!chip->debugfs_fileqfreq[output_num]) {
			dev_err(&client->dev,
				"%s: error creating %s debugfs file",
				__func__, name);
			return (-ENODEV);
		}
	}

	chip->debugfs_filei2c = debugfs_create_file(
		"i2c", 0644, chip->debugfs_dirroot, NULL,
		&idt24x_fops_debug_i2c);
	if (!chip->debugfs_filei2c) {
		dev_err(&client->dev,
			"%s: error creating i2c file", __func__);
		return (-ENODEV);
	}

	dev_dbg(&client->dev, "%s: success", __func__);
	idt24x_chip_fordebugfs = chip;
	return 0;
}

void idt24x_cleanup_debugfs(struct clk_idt24x_chip *chip)
{
	debugfs_remove_recursive(chip->debugfs_dirroot);
}
