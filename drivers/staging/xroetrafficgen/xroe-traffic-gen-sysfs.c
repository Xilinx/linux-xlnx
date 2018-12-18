// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Xilinx, Inc.
 *
 * Vasileios Bimpikas <vasileios.bimpikas@xilinx.com>
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include "roe_radio_ctrl.h"
#include "xroe-traffic-gen.h"

static int xroe_size;
static char xroe_tmp[XROE_SIZE_MAX];

/**
 * utils_sysfs_store_wrapper - Wraps the storing function for sysfs entries
 * @dev:	The structure containing the device's information
 * @address:	The address of the register to be written
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be written
 * @value:	The value to be written to the register
 *
 * Wraps the core functionality of all "store" functions of sysfs entries.
 */
static void utils_sysfs_store_wrapper(struct device *dev, u32 address,
				      u32 offset, u32 mask, u32 value)
{
	void __iomem *working_address;
	u32 read_register_value = 0;
	u32 register_value_to_write = 0;
	u32 delta = 0;
	u32 buffer = 0;
	struct xroe_traffic_gen_local *lp = dev_get_drvdata(dev);

	working_address = (void __iomem *)(lp->base_addr + address);
	read_register_value = ioread32(working_address);
	buffer = (value << offset);
	register_value_to_write = read_register_value & ~mask;
	delta = buffer & mask;
	register_value_to_write |= delta;
	iowrite32(register_value_to_write, working_address);
}

/**
 * utils_sysfs_show_wrapper - Wraps the "show" function for sysfs entries
 * @dev:	The structure containing the device's information
 * @address:	The address of the register to be read
 * @offset:	The offset from the address of the register
 * @mask:	The mask to be used on the value to be read
 *
 * Wraps the core functionality of all "show" functions of sysfs entries.
 *
 * Return: The value designated by the address, offset and mask
 */
static u32 utils_sysfs_show_wrapper(struct device *dev, u32 address, u32 offset,
				    u32 mask)
{
	void __iomem *working_address;
	u32 buffer;
	struct xroe_traffic_gen_local *lp = dev_get_drvdata(dev);

	working_address = (void __iomem *)(lp->base_addr + address);
	buffer = ioread32(working_address);
	return (buffer & mask) >> offset;
}

/**
 * radio_id_show - Returns the block's ID number
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the ID number string
 *
 * Returns the traffic gen's ID (0x1179649 by default)
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_id_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	u32 radio_id;

	radio_id = utils_sysfs_show_wrapper(dev, RADIO_ID_ADDR,
					    RADIO_ID_OFFSET,
					    RADIO_ID_MASK);
	return sprintf(buf, "%d\n", radio_id);
}
static DEVICE_ATTR_RO(radio_id);

/**
 * timeout_enable_show - Returns the traffic gen's timeout enable status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's timeout enable status to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t timeout_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u32 timeout_enable;

	timeout_enable = utils_sysfs_show_wrapper(dev,
						  RADIO_TIMEOUT_ENABLE_ADDR,
						  RADIO_TIMEOUT_ENABLE_OFFSET,
						  RADIO_TIMEOUT_ENABLE_MASK);
	return sprintf(buf, "%d\n", timeout_enable);
}

/**
 * timeout_enable_store - Writes to the traffic gens's timeout enable
 * status register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's timeout enable
 * status to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t timeout_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u32 enable = 0;

	strncpy(xroe_tmp, buf, xroe_size);
	if (strncmp(xroe_tmp, "true", xroe_size) == 0)
		enable = 1;
	else if (strncmp(xroe_tmp, "false", xroe_size) == 0)
		enable = 0;
	utils_sysfs_store_wrapper(dev, RADIO_TIMEOUT_ENABLE_ADDR,
				  RADIO_TIMEOUT_ENABLE_OFFSET,
				  RADIO_TIMEOUT_ENABLE_MASK, enable);
	return count;
}
static DEVICE_ATTR_RW(timeout_enable);

/**
 * timeout_status_show - Returns the timeout status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the ID number string
 *
 * Returns the traffic gen's timeout status (0x1 by default)
 *
 * Return: The number of characters printed on success
 */
static ssize_t timeout_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u32 timeout;

	timeout = utils_sysfs_show_wrapper(dev, RADIO_TIMEOUT_STATUS_ADDR,
					   RADIO_TIMEOUT_STATUS_OFFSET,
					   RADIO_TIMEOUT_STATUS_MASK);
	return sprintf(buf, "%d\n", timeout);
}
static DEVICE_ATTR_RO(timeout_status);

/**
 * timeout_enable_show - Returns the traffic gen's timeout value
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's timeout value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t timeout_value_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u32 timeout_value;

	timeout_value = utils_sysfs_show_wrapper(dev, RADIO_TIMEOUT_VALUE_ADDR,
						 RADIO_TIMEOUT_VALUE_OFFSET,
						 RADIO_TIMEOUT_VALUE_MASK);
	return sprintf(buf, "%d\n", timeout_value);
}

/**
 * timeout_enable_store - Writes to the traffic gens's timeout value
 * status register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's timeout value
 * to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t timeout_value_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	u32 timeout_value;
	int ret;

	ret = kstrtouint(buf, 10, &timeout_value);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_TIMEOUT_VALUE_ADDR,
				  RADIO_TIMEOUT_VALUE_OFFSET,
				  RADIO_TIMEOUT_VALUE_MASK, timeout_value);
	return count;
}
static DEVICE_ATTR_RW(timeout_value);

/**
 * ledmode_show - Returns the current LED mode
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's LED mode value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t ledmode_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u32 ledmode;

	ledmode = utils_sysfs_show_wrapper(dev, RADIO_GPIO_CDC_LEDMODE2_ADDR,
					   RADIO_GPIO_CDC_LEDMODE2_OFFSET,
					   RADIO_GPIO_CDC_LEDMODE2_MASK);
	return sprintf(buf, "%d\n", ledmode);
}

/**
 * ledmode_store - Writes to the current LED mode register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's LED mode value
 * to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t ledmode_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u32 ledmode;
	int ret;

	ret = kstrtouint(buf, 10, &ledmode);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_GPIO_CDC_LEDMODE2_ADDR,
				  RADIO_GPIO_CDC_LEDMODE2_OFFSET,
				  RADIO_GPIO_CDC_LEDMODE2_MASK, ledmode);
	return count;
}
static DEVICE_ATTR_RW(ledmode);

/**
 * ledgpio_show - Returns the current LED gpio
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's LED gpio value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t ledgpio_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u32 ledgpio;

	ledgpio = utils_sysfs_show_wrapper(dev, RADIO_GPIO_CDC_LEDGPIO_ADDR,
					   RADIO_GPIO_CDC_LEDGPIO_OFFSET,
					   RADIO_GPIO_CDC_LEDGPIO_MASK);
	return sprintf(buf, "%d\n", ledgpio);
}

/**
 * ledgpio_store - Writes to the current LED gpio register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's LED gpio value
 * to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t ledgpio_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	u32 ledgpio;
	int ret;

	ret = kstrtouint(buf, 10, &ledgpio);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_GPIO_CDC_LEDGPIO_ADDR,
				  RADIO_GPIO_CDC_LEDGPIO_OFFSET,
				  RADIO_GPIO_CDC_LEDGPIO_MASK, ledgpio);
	return count;
}
static DEVICE_ATTR_RW(ledgpio);

/**
 * dip_status_show - Returns the current DIP switch value
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the GPIO DIP switch value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t dip_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u32 dip_status;

	dip_status = utils_sysfs_show_wrapper(dev, RADIO_GPIO_CDC_LEDGPIO_ADDR,
					      RADIO_GPIO_CDC_LEDGPIO_OFFSET,
					      RADIO_GPIO_CDC_LEDGPIO_MASK);
	return sprintf(buf, "0x%08x\n", dip_status);
}
static DEVICE_ATTR_RO(dip_status);

/**
 * sw_trigger_show - Returns the current SW trigger status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's SW trigger status value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t sw_trigger_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u32 sw_trigger;

	sw_trigger = utils_sysfs_show_wrapper(dev, RADIO_SW_TRIGGER_ADDR,
					      RADIO_SW_TRIGGER_OFFSET,
					      RADIO_SW_TRIGGER_MASK);
	return sprintf(buf, "%d\n", sw_trigger);
}

/**
 * sw_trigger_store - Writes to the SW trigger status register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's SW trigger
 * value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t sw_trigger_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 sw_trigger;
	int ret;

	ret = kstrtouint(buf, 10, &sw_trigger);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_SW_TRIGGER_ADDR,
				  RADIO_SW_TRIGGER_OFFSET,
				  RADIO_SW_TRIGGER_MASK, sw_trigger);
	return count;
}
static DEVICE_ATTR_RW(sw_trigger);

/**
 * radio_enable_show - Returns the current radio enable status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the enable status
 *
 * Reads and writes the traffic gen's radio enable value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	u32 radio_enable;

	radio_enable = utils_sysfs_show_wrapper(dev, RADIO_CDC_ENABLE_ADDR,
						RADIO_CDC_ENABLE_OFFSET,
						RADIO_CDC_ENABLE_MASK);
	return sprintf(buf, "%d\n", radio_enable);
}

/**
 * radio_enable_store - Writes to the radio enable register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's radio enable
 * value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t radio_enable_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	u32 radio_enable;
	int ret;

	ret = kstrtouint(buf, 10, &radio_enable);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_CDC_ENABLE_ADDR,
				  RADIO_CDC_ENABLE_OFFSET,
				  RADIO_CDC_ENABLE_MASK,
				  radio_enable);
	return count;
}
static DEVICE_ATTR_RW(radio_enable);

/**
 * radio_error_show - Returns the current radio error status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the error status
 *
 * Reads and writes the traffic gen's radio error value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_error_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u32 radio_error;

	radio_error = utils_sysfs_show_wrapper(dev, RADIO_CDC_ERROR_ADDR,
					       RADIO_CDC_STATUS_OFFSET,
					       RADIO_CDC_STATUS_MASK);
	return sprintf(buf, "%d\n", radio_error);
}
static DEVICE_ATTR_RO(radio_error);

/**
 * radio_status_show - Returns the current radio status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the status
 *
 * Reads and writes the traffic gen's radio status value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	u32 radio_status;

	radio_status = utils_sysfs_show_wrapper(dev, RADIO_CDC_STATUS_ADDR,
						RADIO_CDC_STATUS_OFFSET,
						RADIO_CDC_STATUS_MASK);
	return sprintf(buf, "%d\n", radio_status);
}
static DEVICE_ATTR_RO(radio_status);

/**
 * radio_loopback_show - Returns the current radio loopback status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the loopback status
 *
 * Reads and writes the traffic gen's radio loopback value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_loopback_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u32 radio_loopback;

	radio_loopback = utils_sysfs_show_wrapper(dev,
						  RADIO_CDC_LOOPBACK_ADDR,
						  RADIO_CDC_LOOPBACK_OFFSET,
						  RADIO_CDC_LOOPBACK_MASK);
	return sprintf(buf, "%d\n", radio_loopback);
}

/**
 * radio_loopback_store - Writes to the radio loopback register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's radio loopback
 * value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t radio_loopback_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	u32 radio_loopback;
	int ret;

	ret = kstrtouint(buf, 10, &radio_loopback);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_CDC_LOOPBACK_ADDR,
				  RADIO_CDC_LOOPBACK_OFFSET,
				  RADIO_CDC_LOOPBACK_MASK, radio_loopback);
	return count;
}
static DEVICE_ATTR_RW(radio_loopback);

/**
 * radio_sink_enable_show - Returns the current radio sink enable status
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the loopback status
 *
 * Reads and writes the traffic gen's radio sink enable value to the sysfs entry
 *
 * Return: The number of characters printed on success
 */
static ssize_t radio_sink_enable_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	u32 sink_enable;

	sink_enable = utils_sysfs_show_wrapper(dev, RADIO_SINK_ENABLE_ADDR,
					       RADIO_SINK_ENABLE_OFFSET,
					       RADIO_SINK_ENABLE_MASK);
	return sprintf(buf, "%d\n", sink_enable);
}

/**
 * radio_sink_enable_store - Writes to the radio sink enable register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's radio sink
 * enable value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t radio_sink_enable_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	u32 sink_enable;
	int ret;

	ret = kstrtouint(buf, 10, &sink_enable);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, RADIO_SINK_ENABLE_ADDR,
				  RADIO_SINK_ENABLE_OFFSET,
				  RADIO_SINK_ENABLE_MASK, sink_enable);
	return count;
}
static DEVICE_ATTR_RW(radio_sink_enable);

/**
 * antenna_status_show - Returns the status for all antennas
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the ID number string
 *
 * Returns the traffic gen's status for all antennas
 *
 * Return: The number of characters printed on success
 */
static ssize_t antenna_status_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	u32 status_0_31;
	u32 status_63_32;
	u32 status_95_64;
	u32 status_127_96;

	status_0_31 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_STATUS_31_0_ADDR,
				 RADIO_CDC_STATUS_31_0_OFFSET,
				 lower_32_bits(RADIO_CDC_STATUS_31_0_MASK));
	status_63_32 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_STATUS_63_32_ADDR,
				 RADIO_CDC_STATUS_63_32_OFFSET,
				 lower_32_bits(RADIO_CDC_STATUS_63_32_MASK));
	status_95_64 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_STATUS_95_64_ADDR,
				 RADIO_CDC_STATUS_95_64_OFFSET,
				 lower_32_bits(RADIO_CDC_STATUS_95_64_MASK));
	status_127_96 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_STATUS_127_96_ADDR,
				 RADIO_CDC_STATUS_127_96_OFFSET,
				 lower_32_bits(RADIO_CDC_STATUS_127_96_MASK));

	return sprintf(buf, "0x%08x 0x%08x 0x%08x 0x%08x\n",
		       status_0_31, status_63_32, status_95_64, status_127_96);
}
static DEVICE_ATTR_RO(antenna_status);

/**
 * antenna_error_show - Returns the error for all antennas
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the ID number string
 *
 * Returns the traffic gen's error for all antennas
 *
 * Return: The number of characters printed on success
 */
static ssize_t antenna_error_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u32 error_0_31;
	u32 error_63_32;
	u32 error_95_64;
	u32 error_127_96;

	error_0_31 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_ERROR_31_0_ADDR,
				 RADIO_CDC_ERROR_31_0_OFFSET,
				 lower_32_bits(RADIO_CDC_ERROR_31_0_MASK));
	error_63_32 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_ERROR_63_32_ADDR,
				 RADIO_CDC_ERROR_63_32_OFFSET,
				 lower_32_bits(RADIO_CDC_ERROR_63_32_MASK));
	error_95_64 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_ERROR_95_64_ADDR,
				 RADIO_CDC_ERROR_95_64_OFFSET,
				 lower_32_bits(RADIO_CDC_ERROR_95_64_MASK));
	error_127_96 =
	utils_sysfs_show_wrapper(dev, RADIO_CDC_ERROR_127_96_ADDR,
				 RADIO_CDC_ERROR_127_96_OFFSET,
				 lower_32_bits(RADIO_CDC_ERROR_127_96_MASK));

	return sprintf(buf, "0x%08x 0x%08x 0x%08x 0x%08x\n",
		       error_0_31, error_63_32, error_95_64, error_127_96);
}
static DEVICE_ATTR_RO(antenna_error);

/**
 * framer_packet_size_show - Returns the size of the framer's packet
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the loopback status
 *
 * Reads and writes the traffic gen's framer packet size value
 *
 * Return: The number of characters printed on success
 */
static ssize_t framer_packet_size_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	u32 packet_size;

	packet_size = utils_sysfs_show_wrapper(dev, FRAM_PACKET_DATA_SIZE_ADDR,
					       FRAM_PACKET_DATA_SIZE_OFFSET,
					       FRAM_PACKET_DATA_SIZE_MASK);
	return sprintf(buf, "%d\n", packet_size);
}

/**
 * framer_packet_size_store - Writes to the framer's packet size register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's framer packet
 * size value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t framer_packet_size_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	u32 packet_size;
	int ret;

	ret = kstrtouint(buf, 10, &packet_size);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, FRAM_PACKET_DATA_SIZE_ADDR,
				  FRAM_PACKET_DATA_SIZE_OFFSET,
				  FRAM_PACKET_DATA_SIZE_MASK, packet_size);
	return count;
}
static DEVICE_ATTR_RW(framer_packet_size);

/**
 * framer_pause_size_show - Returns the size of the framer's pause
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the loopback status
 *
 * Reads and writes the traffic gen's framer pause size value
 *
 * Return: The number of characters printed on success
 */
static ssize_t framer_pause_size_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	u32 pause_size;

	pause_size = utils_sysfs_show_wrapper(dev, FRAM_PAUSE_DATA_SIZE_ADDR,
					      FRAM_PAUSE_DATA_SIZE_OFFSET,
					      FRAM_PAUSE_DATA_SIZE_MASK);
	return sprintf(buf, "%d\n", pause_size);
}

/**
 * framer_pause_size_store - Writes to the framer's pause size register
 * @dev:	The device's structure
 * @attr:	The attributes of the kernel object
 * @buf:	The buffer containing the timeout value
 * @count:	The number of characters typed by the user
 *
 * Reads the user input and accordingly writes the traffic gens's framer pause
 * size value to the sysfs entry
 *
 * Return: The number of characters of the entry (count) on success
 */
static ssize_t framer_pause_size_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	u32 pause_size;
	int ret;

	ret = kstrtouint(buf, 10, &pause_size);
	if (ret)
		return ret;
	utils_sysfs_store_wrapper(dev, FRAM_PAUSE_DATA_SIZE_ADDR,
				  FRAM_PAUSE_DATA_SIZE_OFFSET,
				  FRAM_PAUSE_DATA_SIZE_MASK, pause_size);
	return count;
}
static DEVICE_ATTR_RW(framer_pause_size);

static struct attribute *xroe_traffic_gen_attrs[] = {
	&dev_attr_radio_id.attr,
	&dev_attr_timeout_enable.attr,
	&dev_attr_timeout_status.attr,
	&dev_attr_timeout_value.attr,
	&dev_attr_ledmode.attr,
	&dev_attr_ledgpio.attr,
	&dev_attr_dip_status.attr,
	&dev_attr_sw_trigger.attr,
	&dev_attr_radio_enable.attr,
	&dev_attr_radio_error.attr,
	&dev_attr_radio_status.attr,
	&dev_attr_radio_loopback.attr,
	&dev_attr_radio_sink_enable.attr,
	&dev_attr_antenna_status.attr,
	&dev_attr_antenna_error.attr,
	&dev_attr_framer_packet_size.attr,
	&dev_attr_framer_pause_size.attr,
	NULL,
};
ATTRIBUTE_GROUPS(xroe_traffic_gen);

/**
 * xroe_traffic_gen_sysfs_init - Creates the xroe sysfs directory and entries
 * @dev:	The device's structure
 *
 * Return: 0 on success, negative value in case of failure to
 * create the sysfs group
 *
 * Creates the xroetrafficgen sysfs directory and entries
 */
int xroe_traffic_gen_sysfs_init(struct device *dev)
{
	int ret;

	dev->groups = xroe_traffic_gen_groups;
	ret = sysfs_create_group(&dev->kobj, *xroe_traffic_gen_groups);
	if (ret)
		dev_err(dev, "sysfs creation failed\n");

	return ret;
}

/**
 * xroe_traffic_gen_sysfs_exit - Deletes the xroe sysfs directory and entries
 * @dev:	The device's structure
 *
 * Deletes the xroetrafficgen sysfs directory and entries
 */
void xroe_traffic_gen_sysfs_exit(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, *xroe_traffic_gen_groups);
}
