/*
 * Panasonic MN88473 DVB-T/T2/C demodulator driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "mn88473_priv.h"

static int mn88473_get_tune_settings(struct dvb_frontend *fe,
				     struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	return 0;
}

static int mn88473_set_frontend(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i;
	unsigned int uitmp;
	u32 if_frequency;
	u8 delivery_system_val, if_val[3], *conf_val_ptr;
	u8 reg_bank2_2d_val, reg_bank0_d2_val;

	dev_dbg(&client->dev,
		"delivery_system=%u modulation=%u frequency=%u bandwidth_hz=%u symbol_rate=%u inversion=%d stream_id=%d\n",
		c->delivery_system, c->modulation, c->frequency,
		c->bandwidth_hz, c->symbol_rate, c->inversion, c->stream_id);

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
		delivery_system_val = 0x02;
		reg_bank2_2d_val = 0x23;
		reg_bank0_d2_val = 0x2a;
		break;
	case SYS_DVBT2:
		delivery_system_val = 0x03;
		reg_bank2_2d_val = 0x3b;
		reg_bank0_d2_val = 0x29;
		break;
	case SYS_DVBC_ANNEX_A:
		delivery_system_val = 0x04;
		reg_bank2_2d_val = 0x3b;
		reg_bank0_d2_val = 0x29;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		switch (c->bandwidth_hz) {
		case 6000000:
			conf_val_ptr = "\xe9\x55\x55\x1c\x29\x1c\x29";
			break;
		case 7000000:
			conf_val_ptr = "\xc8\x00\x00\x17\x0a\x17\x0a";
			break;
		case 8000000:
			conf_val_ptr = "\xaf\x00\x00\x11\xec\x11\xec";
			break;
		default:
			ret = -EINVAL;
			goto err;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		conf_val_ptr = "\x10\xab\x0d\xae\x1d\x9d";
		break;
	default:
		break;
	}

	/* Program tuner */
	if (fe->ops.tuner_ops.set_params) {
		ret = fe->ops.tuner_ops.set_params(fe);
		if (ret)
			goto err;
	}

	if (fe->ops.tuner_ops.get_if_frequency) {
		ret = fe->ops.tuner_ops.get_if_frequency(fe, &if_frequency);
		if (ret)
			goto err;

		dev_dbg(&client->dev, "get_if_frequency=%u\n", if_frequency);
	} else {
		ret = -EINVAL;
		goto err;
	}

	/* Calculate IF registers */
	uitmp = DIV_ROUND_CLOSEST_ULL((u64) if_frequency * 0x1000000, dev->clk);
	if_val[0] = (uitmp >> 16) & 0xff;
	if_val[1] = (uitmp >>  8) & 0xff;
	if_val[2] = (uitmp >>  0) & 0xff;

	ret = regmap_write(dev->regmap[2], 0x05, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0xfb, 0x13);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0xef, 0x13);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0xf9, 0x13);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x00, 0x18);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x01, 0x01);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x02, 0x21);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x03, delivery_system_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x0b, 0x00);
	if (ret)
		goto err;

	for (i = 0; i < sizeof(if_val); i++) {
		ret = regmap_write(dev->regmap[2], 0x10 + i, if_val[i]);
		if (ret)
			goto err;
	}

	switch (c->delivery_system) {
	case SYS_DVBT:
	case SYS_DVBT2:
		for (i = 0; i < 7; i++) {
			ret = regmap_write(dev->regmap[2], 0x13 + i,
					   conf_val_ptr[i]);
			if (ret)
				goto err;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_bulk_write(dev->regmap[1], 0x10, conf_val_ptr, 6);
		if (ret)
			goto err;
		break;
	default:
		break;
	}

	ret = regmap_write(dev->regmap[2], 0x2d, reg_bank2_2d_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x2e, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x56, 0x0d);
	if (ret)
		goto err;
	ret = regmap_bulk_write(dev->regmap[0], 0x01,
				"\xba\x13\x80\xba\x91\xdd\xe7\x28", 8);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x0a, 0x1a);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x13, 0x1f);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x19, 0x03);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x1d, 0xb0);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x2a, 0x72);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x2d, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x3c, 0x00);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0x3f, 0xf8);
	if (ret)
		goto err;
	ret = regmap_bulk_write(dev->regmap[0], 0x40, "\xf4\x08", 2);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xd2, reg_bank0_d2_val);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xd4, 0x55);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[1], 0xbe, 0x08);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xb2, 0x37);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[0], 0xd7, 0x04);
	if (ret)
		goto err;

	/* Reset FSM */
	ret = regmap_write(dev->regmap[2], 0xf8, 0x9f);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	unsigned int uitmp;

	if (!dev->active) {
		ret = -EAGAIN;
		goto err;
	}

	*status = 0;

	switch (c->delivery_system) {
	case SYS_DVBT:
		ret = regmap_read(dev->regmap[0], 0x62, &uitmp);
		if (ret)
			goto err;

		if (!(uitmp & 0xa0)) {
			if ((uitmp & 0x0f) >= 0x09)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					  FE_HAS_VITERBI | FE_HAS_SYNC |
					  FE_HAS_LOCK;
			else if ((uitmp & 0x0f) >= 0x03)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		}
		break;
	case SYS_DVBT2:
		ret = regmap_read(dev->regmap[2], 0x8b, &uitmp);
		if (ret)
			goto err;

		if (!(uitmp & 0x40)) {
			if ((uitmp & 0x0f) >= 0x0d)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					  FE_HAS_VITERBI | FE_HAS_SYNC |
					  FE_HAS_LOCK;
			else if ((uitmp & 0x0f) >= 0x0a)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					  FE_HAS_VITERBI;
			else if ((uitmp & 0x0f) >= 0x07)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		ret = regmap_read(dev->regmap[1], 0x85, &uitmp);
		if (ret)
			goto err;

		if (!(uitmp & 0x40)) {
			ret = regmap_read(dev->regmap[1], 0x89, &uitmp);
			if (ret)
				goto err;

			if (uitmp & 0x01)
				*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
					  FE_HAS_VITERBI | FE_HAS_SYNC |
					  FE_HAS_LOCK;
		}
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_init(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret, len, remain;
	unsigned int uitmp;
	const struct firmware *fw;
	const char *name = MN88473_FIRMWARE;

	dev_dbg(&client->dev, "\n");

	/* Check if firmware is already running */
	ret = regmap_read(dev->regmap[0], 0xf5, &uitmp);
	if (ret)
		goto err;

	if (!(uitmp & 0x01))
		goto warm;

	/* Request the firmware, this will block and timeout */
	ret = request_firmware(&fw, name, &client->dev);
	if (ret) {
		dev_err(&client->dev, "firmware file '%s' not found\n", name);
		goto err;
	}

	dev_info(&client->dev, "downloading firmware from file '%s'\n", name);

	ret = regmap_write(dev->regmap[0], 0xf5, 0x03);
	if (ret)
		goto err_release_firmware;

	for (remain = fw->size; remain > 0; remain -= (dev->i2c_wr_max - 1)) {
		len = min(dev->i2c_wr_max - 1, remain);
		ret = regmap_bulk_write(dev->regmap[0], 0xf6,
					&fw->data[fw->size - remain], len);
		if (ret) {
			dev_err(&client->dev, "firmware download failed %d\n",
				ret);
			goto err_release_firmware;
		}
	}

	release_firmware(fw);

	/* Parity check of firmware */
	ret = regmap_read(dev->regmap[0], 0xf8, &uitmp);
	if (ret)
		goto err;

	if (uitmp & 0x10) {
		dev_err(&client->dev, "firmware parity check failed\n");
		ret = -EINVAL;
		goto err;
	}

	ret = regmap_write(dev->regmap[0], 0xf5, 0x00);
	if (ret)
		goto err;
warm:
	/* TS config */
	ret = regmap_write(dev->regmap[2], 0x09, 0x08);
	if (ret)
		goto err;
	ret = regmap_write(dev->regmap[2], 0x08, 0x1d);
	if (ret)
		goto err;

	dev->active = true;

	return 0;
err_release_firmware:
	release_firmware(fw);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_sleep(struct dvb_frontend *fe)
{
	struct i2c_client *client = fe->demodulator_priv;
	struct mn88473_dev *dev = i2c_get_clientdata(client);
	int ret;

	dev_dbg(&client->dev, "\n");

	dev->active = false;

	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static const struct dvb_frontend_ops mn88473_ops = {
	.delsys = {SYS_DVBT, SYS_DVBT2, SYS_DVBC_ANNEX_A},
	.info = {
		.name = "Panasonic MN88473",
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 7200000,
		.caps =	FE_CAN_FEC_1_2                 |
			FE_CAN_FEC_2_3                 |
			FE_CAN_FEC_3_4                 |
			FE_CAN_FEC_5_6                 |
			FE_CAN_FEC_7_8                 |
			FE_CAN_FEC_AUTO                |
			FE_CAN_QPSK                    |
			FE_CAN_QAM_16                  |
			FE_CAN_QAM_32                  |
			FE_CAN_QAM_64                  |
			FE_CAN_QAM_128                 |
			FE_CAN_QAM_256                 |
			FE_CAN_QAM_AUTO                |
			FE_CAN_TRANSMISSION_MODE_AUTO  |
			FE_CAN_GUARD_INTERVAL_AUTO     |
			FE_CAN_HIERARCHY_AUTO          |
			FE_CAN_MUTE_TS                 |
			FE_CAN_2G_MODULATION
	},

	.get_tune_settings = mn88473_get_tune_settings,

	.init = mn88473_init,
	.sleep = mn88473_sleep,

	.set_frontend = mn88473_set_frontend,

	.read_status = mn88473_read_status,
};

static int mn88473_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct mn88473_config *config = client->dev.platform_data;
	struct mn88473_dev *dev;
	int ret;
	unsigned int uitmp;
	static const struct regmap_config regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};

	dev_dbg(&client->dev, "\n");

	/* Caller really need to provide pointer for frontend we create */
	if (config->fe == NULL) {
		dev_err(&client->dev, "frontend pointer not defined\n");
		ret = -EINVAL;
		goto err;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	if (config->i2c_wr_max)
		dev->i2c_wr_max = config->i2c_wr_max;
	else
		dev->i2c_wr_max = ~0;

	if (config->xtal)
		dev->clk = config->xtal;
	else
		dev->clk = 25000000;
	dev->client[0] = client;
	dev->regmap[0] = regmap_init_i2c(dev->client[0], &regmap_config);
	if (IS_ERR(dev->regmap[0])) {
		ret = PTR_ERR(dev->regmap[0]);
		goto err_kfree;
	}

	/* Check demod answers with correct chip id */
	ret = regmap_read(dev->regmap[0], 0xff, &uitmp);
	if (ret)
		goto err_regmap_0_regmap_exit;

	dev_dbg(&client->dev, "chip id=%02x\n", uitmp);

	if (uitmp != 0x03) {
		ret = -ENODEV;
		goto err_regmap_0_regmap_exit;
	}

	/*
	 * Chip has three I2C addresses for different register banks. Used
	 * addresses are 0x18, 0x1a and 0x1c. We register two dummy clients,
	 * 0x1a and 0x1c, in order to get own I2C client for each register bank.
	 *
	 * Also, register bank 2 do not support sequential I/O. Only single
	 * register write or read is allowed to that bank.
	 */
	dev->client[1] = i2c_new_dummy(client->adapter, 0x1a);
	if (dev->client[1] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "I2C registration failed\n");
		if (ret)
			goto err_regmap_0_regmap_exit;
	}
	dev->regmap[1] = regmap_init_i2c(dev->client[1], &regmap_config);
	if (IS_ERR(dev->regmap[1])) {
		ret = PTR_ERR(dev->regmap[1]);
		goto err_client_1_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[1], dev);

	dev->client[2] = i2c_new_dummy(client->adapter, 0x1c);
	if (dev->client[2] == NULL) {
		ret = -ENODEV;
		dev_err(&client->dev, "2nd I2C registration failed\n");
		if (ret)
			goto err_regmap_1_regmap_exit;
	}
	dev->regmap[2] = regmap_init_i2c(dev->client[2], &regmap_config);
	if (IS_ERR(dev->regmap[2])) {
		ret = PTR_ERR(dev->regmap[2]);
		goto err_client_2_i2c_unregister_device;
	}
	i2c_set_clientdata(dev->client[2], dev);

	/* Sleep because chip is active by default */
	ret = regmap_write(dev->regmap[2], 0x05, 0x3e);
	if (ret)
		goto err_regmap_2_regmap_exit;

	/* Create dvb frontend */
	memcpy(&dev->frontend.ops, &mn88473_ops, sizeof(dev->frontend.ops));
	dev->frontend.demodulator_priv = client;
	*config->fe = &dev->frontend;
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "Panasonic MN88473 successfully identified\n");

	return 0;
err_regmap_2_regmap_exit:
	regmap_exit(dev->regmap[2]);
err_client_2_i2c_unregister_device:
	i2c_unregister_device(dev->client[2]);
err_regmap_1_regmap_exit:
	regmap_exit(dev->regmap[1]);
err_client_1_i2c_unregister_device:
	i2c_unregister_device(dev->client[1]);
err_regmap_0_regmap_exit:
	regmap_exit(dev->regmap[0]);
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int mn88473_remove(struct i2c_client *client)
{
	struct mn88473_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	regmap_exit(dev->regmap[2]);
	i2c_unregister_device(dev->client[2]);

	regmap_exit(dev->regmap[1]);
	i2c_unregister_device(dev->client[1]);

	regmap_exit(dev->regmap[0]);

	kfree(dev);

	return 0;
}

static const struct i2c_device_id mn88473_id_table[] = {
	{"mn88473", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, mn88473_id_table);

static struct i2c_driver mn88473_driver = {
	.driver = {
		.name	             = "mn88473",
		.suppress_bind_attrs = true,
	},
	.probe		= mn88473_probe,
	.remove		= mn88473_remove,
	.id_table	= mn88473_id_table,
};

module_i2c_driver(mn88473_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Panasonic MN88473 DVB-T/T2/C demodulator driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MN88473_FIRMWARE);
