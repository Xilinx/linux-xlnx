// SPDX-License-Identifier: GPL-2.0
/*
 * TPS544B25 power regulator driver
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/regulator/driver.h>
#include "pmbus.h"

#define TPS544_NUM_PAGES		1

struct tps544_data {
	struct device *dev;
	u16 vout_min[TPS544_NUM_PAGES], vout_max[TPS544_NUM_PAGES];
	struct pmbus_driver_info info;
};

struct vlut {
	int vol;
	u16 vloop;
	u16 v_ovfault;
	u16 v_ovwarn;
	u16 vmax;
	u16 mfr_vmin;
	u16 v_uvwarn;
	u16 v_uvfault;
};

#if IS_ENABLED(CONFIG_SENSORS_TPS544_REGULATOR)
#define TPS544_MFR_VOUT_MIN		0xA4
#define TPS544_MFR_RESTORE_DEF_ALL	0x12
#define TPS544_MFR_IOUT_CAL_OFFSET	0x39

#define TPS544_VOUTREAD_MULTIPLIER	1950
#define TPS544_IOUTREAD_MULTIPLIER	62500
#define TPS544_IOUTREAD_MASK		GENMASK(9, 0)

#define TPS544_VOUT_LIMIT		5300000

#define to_tps544_data(x) container_of(x, struct tps544_data, info)

/*
 * This currently supports 3 voltage out buckets:
 * 0.5V to 1.3V
 * 1.3V to 2.6V
 * 2.6V to 5.3V
 * Any requested voltage will be mapped to one of these buckets and
 * VOUT will be programmed with 0.1V granularity.
 */
static const struct vlut tps544_vout[3] = {
	{500000, 0xF004, 0x0290, 0x0285, 0x0300, 0x0100, 0x00CD, 0x009A},
	{1300000, 0xF002, 0x059A, 0x0566, 0x0600, 0x0100, 0x0143, 0x0130},
	{2600000, 0xF001, 0x0B00, 0x0A9A, 0x0A00, 0x0100, 0x0143, 0x0130}
};
#endif

static int tps544_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	return pmbus_read_word_data(client, page, phase, reg);
}

static int tps544_read_byte_data(struct i2c_client *client, int page, int reg)
{
	return pmbus_read_byte_data(client, page, reg);
}

static int tps544_write_byte(struct i2c_client *client, int page, u8 byte)
{
	return pmbus_write_byte(client, page, byte);
}

static int tps544_write_word_data(struct i2c_client *client, int page,
				  int reg, u16 word)
{
	int ret;

	ret = pmbus_write_word_data(client, page, reg, word);
	/* TODO - Define new PMBUS virtual register entries for these */

	return ret;
}

#if IS_ENABLED(CONFIG_SENSORS_TPS544_REGULATOR)
static int tps544_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct device *dev = rdev_get_dev(rdev);
	struct i2c_client *client = to_i2c_client(dev->parent);
	int page = 0;

	return pmbus_read_word_data(client, page, 0xff, PMBUS_READ_VOUT);
}

static int tps544_regulator_set_voltage(struct regulator_dev *rdev, int min_uV,
					int max_uV, unsigned int *selector)
{
	struct device *dev = rdev_get_dev(rdev);
	struct i2c_client *client = to_i2c_client(dev->parent);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct tps544_data *data = to_tps544_data(info);
	int index, page = 0;
	u16 vout;

	/* voltage will be set close to min value requested */
	vout = (u16)((min_uV * 512) / 1000000);

	/* Check voltage bucket */
	if (min_uV >= tps544_vout[2].vol)
		index = 2;
	else if (min_uV >= tps544_vout[1].vol)
		index = 1;
	else if (min_uV >= tps544_vout[0].vol)
		index = 0;
	else
		return -EINVAL;

	pmbus_write_word_data(client, page, PMBUS_VOUT_SCALE_LOOP,
			      tps544_vout[index].vloop);
	/* Use delay after setting scale loop; this is derived from testing */
	msleep(2000);
	pmbus_write_word_data(client, page, PMBUS_VOUT_OV_FAULT_LIMIT,
			      tps544_vout[index].v_ovfault);
	pmbus_write_word_data(client, page, PMBUS_VOUT_OV_WARN_LIMIT,
			      tps544_vout[index].v_ovwarn);
	pmbus_write_word_data(client, page, PMBUS_VOUT_MAX,
			      tps544_vout[index].vmax);
	pmbus_write_word_data(client, page, PMBUS_VOUT_COMMAND, vout);
	tps544_write_word_data(client, page, TPS544_MFR_VOUT_MIN,
			       tps544_vout[index].mfr_vmin);
	pmbus_write_word_data(client, page, PMBUS_VOUT_UV_WARN_LIMIT,
			      tps544_vout[index].v_uvwarn);
	pmbus_write_word_data(client, page, PMBUS_VOUT_UV_FAULT_LIMIT,
			      tps544_vout[index].v_uvfault);

	data->vout_min[page] = min_uV;
	data->vout_max[page] = max_uV;

	return 0;
}

static ssize_t tps544_setv_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	int vout;

	vout = tps544_regulator_get_voltage(rdev) * TPS544_VOUTREAD_MULTIPLIER;
	return sprintf(buf, "%d\n", vout);
}

static ssize_t tps544_setv_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct regulator_dev *rdev = dev_get_drvdata(dev);
	int val;
	int err;

	err = kstrtoint(buf, 0, &val);
	if (err)
		return err;
	if (val > TPS544_VOUT_LIMIT)
		return -EINVAL;

	err = tps544_regulator_set_voltage(rdev, val, val, NULL);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_RW(tps544_setv);

static ssize_t tps544_restorev_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	int err;

	err = pmbus_write_byte(client, 0, TPS544_MFR_RESTORE_DEF_ALL);
	if (err)
		return err;

	return count;
}

static DEVICE_ATTR_WO(tps544_restorev);

static ssize_t tps544_geti_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	u16 reg_iout;

	reg_iout = pmbus_read_word_data(client, 0, 0xff, PMBUS_READ_IOUT) &
			TPS544_IOUTREAD_MASK;

	return sprintf(buf, "%d\n", reg_iout * TPS544_IOUTREAD_MULTIPLIER);
}

static DEVICE_ATTR_RO(tps544_geti);

static ssize_t tps544_setcali_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	int reg_cali;

	reg_cali = pmbus_read_word_data(client, 0, 0xff, TPS544_MFR_IOUT_CAL_OFFSET);

	return sprintf(buf, "Current: 0x%x; Set value in hex to calibrate\n",
		       reg_cali);
}

static ssize_t tps544_setcali_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev->parent);
	u16 val;
	int err;

	err = kstrtou16(buf, 0x0, &val);
	if (err)
		return err;

	err = pmbus_write_word_data(client, 0, TPS544_MFR_IOUT_CAL_OFFSET, val);
	if (err)
		return err;

	return (ssize_t)count;
}

static DEVICE_ATTR_RW(tps544_setcali);

static struct attribute *reg_attrs[] = {
	&dev_attr_tps544_setv.attr,
	&dev_attr_tps544_restorev.attr,
	&dev_attr_tps544_geti.attr,
	&dev_attr_tps544_setcali.attr,
	NULL,
};

ATTRIBUTE_GROUPS(reg);

static const struct regulator_desc tps544_reg_desc[] = {
	PMBUS_REGULATOR("vout", 0),
};
#endif /* CONFIG_SENSORS_TPS544_REGULATOR */

static int tps544_probe(struct i2c_client *client)
{
	unsigned int i;
	struct device *dev = &client->dev;
	struct tps544_data *data;
	struct pmbus_driver_info *info;
#if IS_ENABLED(CONFIG_SENSORS_TPS544_REGULATOR)
	int ret;
	struct regulator_dev *rdev;
	struct regulator_config rconfig = { };
#endif

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct tps544_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;

	info = &data->info;
	info->write_word_data = tps544_write_word_data;
	/* TODO - remove these 3 hooks maybe unnecessary */
	info->write_byte = tps544_write_byte;
	info->read_word_data = tps544_read_word_data;
	info->read_byte_data = tps544_read_byte_data;

	for (i = 0; i < ARRAY_SIZE(data->vout_min); i++)
		data->vout_min[i] = 0xffff;

	info->pages = TPS544_NUM_PAGES;
	info->func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;

#if IS_ENABLED(CONFIG_SENSORS_TPS544_REGULATOR)
	rconfig.dev = dev;
	rconfig.driver_data = data;
	info->num_regulators = info->pages;
	info->reg_desc = tps544_reg_desc;
	if (info->num_regulators > (int)ARRAY_SIZE(tps544_reg_desc)) {
		dev_err(&client->dev, "num_regulators too large!");
		info->num_regulators = ARRAY_SIZE(tps544_reg_desc);
	}

	rdev = devm_regulator_register(dev, tps544_reg_desc, &rconfig);
	if (IS_ERR(rdev)) {
		dev_err(dev, "Failed to register %s regulator\n",
			info->reg_desc[0].name);
		return (int)PTR_ERR(rdev);
	}

	ret = sysfs_create_groups(&rdev->dev.kobj, reg_groups);
	if (ret)
		return ret;

	dev_set_drvdata(dev, rdev);
#endif

	return pmbus_do_probe(client, info);
}

static int tps544_remove(struct i2c_client *client)
{
#if IS_ENABLED(CONFIG_SENSORS_TPS544_REGULATOR)
	struct device *dev = &client->dev;
	struct regulator_dev *rdev = dev_get_drvdata(dev);

	sysfs_remove_groups(&rdev->dev.kobj, reg_groups);
#endif

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id tps544_of_match[] = {
	{ .compatible = "ti,tps544" },
	{ }
};
MODULE_DEVICE_TABLE(of, tps544_of_match);
#endif

static const struct i2c_device_id tps544_id[] = {
	{"tps544", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, tps544_id);

static struct i2c_driver tps544_driver = {
	.driver = {
		.name = "tps544",
		.of_match_table = of_match_ptr(tps544_of_match),
	},
	.probe_new = tps544_probe,
	.remove = tps544_remove,
	.id_table = tps544_id,
};

module_i2c_driver(tps544_driver);

MODULE_AUTHOR("Harini Katakam");
MODULE_DESCRIPTION("PMBus regulator driver for TPS544");
MODULE_LICENSE("GPL v2");
