/*
 * I2C Link Layer for ST NCI NFC controller familly based Driver
 * Copyright (C) 2014-2015 STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/nfc.h>
#include <linux/platform_data/st-nci.h>

#include "st-nci.h"

#define DRIVER_DESC "NCI NFC driver for ST_NCI"

/* ndlc header */
#define ST_NCI_FRAME_HEADROOM 1
#define ST_NCI_FRAME_TAILROOM 0

#define ST_NCI_I2C_MIN_SIZE 4   /* PCB(1) + NCI Packet header(3) */
#define ST_NCI_I2C_MAX_SIZE 250 /* req 4.2.1 */

#define ST_NCI_I2C_DRIVER_NAME "st_nci_i2c"

#define ST_NCI_GPIO_NAME_RESET "reset"

struct st_nci_i2c_phy {
	struct i2c_client *i2c_dev;
	struct llt_ndlc *ndlc;

	bool irq_active;

	unsigned int gpio_reset;
	unsigned int irq_polarity;

	struct st_nci_se_status se_status;
};

static int st_nci_i2c_enable(void *phy_id)
{
	struct st_nci_i2c_phy *phy = phy_id;

	gpio_set_value(phy->gpio_reset, 0);
	usleep_range(10000, 15000);
	gpio_set_value(phy->gpio_reset, 1);
	usleep_range(80000, 85000);

	if (phy->ndlc->powered == 0 && phy->irq_active == 0) {
		enable_irq(phy->i2c_dev->irq);
		phy->irq_active = true;
	}

	return 0;
}

static void st_nci_i2c_disable(void *phy_id)
{
	struct st_nci_i2c_phy *phy = phy_id;

	disable_irq_nosync(phy->i2c_dev->irq);
	phy->irq_active = false;
}

/*
 * Writing a frame must not return the number of written bytes.
 * It must return either zero for success, or <0 for error.
 * In addition, it must not alter the skb
 */
static int st_nci_i2c_write(void *phy_id, struct sk_buff *skb)
{
	int r = -1;
	struct st_nci_i2c_phy *phy = phy_id;
	struct i2c_client *client = phy->i2c_dev;

	if (phy->ndlc->hard_fault != 0)
		return phy->ndlc->hard_fault;

	r = i2c_master_send(client, skb->data, skb->len);
	if (r < 0) {  /* Retry, chip was in standby */
		usleep_range(1000, 4000);
		r = i2c_master_send(client, skb->data, skb->len);
	}

	if (r >= 0) {
		if (r != skb->len)
			r = -EREMOTEIO;
		else
			r = 0;
	}

	return r;
}

/*
 * Reads an ndlc frame and returns it in a newly allocated sk_buff.
 * returns:
 * 0 : if received frame is complete
 * -EREMOTEIO : i2c read error (fatal)
 * -EBADMSG : frame was incorrect and discarded
 * -ENOMEM : cannot allocate skb, frame dropped
 */
static int st_nci_i2c_read(struct st_nci_i2c_phy *phy,
				 struct sk_buff **skb)
{
	int r;
	u8 len;
	u8 buf[ST_NCI_I2C_MAX_SIZE];
	struct i2c_client *client = phy->i2c_dev;

	r = i2c_master_recv(client, buf, ST_NCI_I2C_MIN_SIZE);
	if (r < 0) {  /* Retry, chip was in standby */
		usleep_range(1000, 4000);
		r = i2c_master_recv(client, buf, ST_NCI_I2C_MIN_SIZE);
	}

	if (r != ST_NCI_I2C_MIN_SIZE)
		return -EREMOTEIO;

	len = be16_to_cpu(*(__be16 *) (buf + 2));
	if (len > ST_NCI_I2C_MAX_SIZE) {
		nfc_err(&client->dev, "invalid frame len\n");
		return -EBADMSG;
	}

	*skb = alloc_skb(ST_NCI_I2C_MIN_SIZE + len, GFP_KERNEL);
	if (*skb == NULL)
		return -ENOMEM;

	skb_reserve(*skb, ST_NCI_I2C_MIN_SIZE);
	skb_put(*skb, ST_NCI_I2C_MIN_SIZE);
	memcpy((*skb)->data, buf, ST_NCI_I2C_MIN_SIZE);

	if (!len)
		return 0;

	r = i2c_master_recv(client, buf, len);
	if (r != len) {
		kfree_skb(*skb);
		return -EREMOTEIO;
	}

	skb_put(*skb, len);
	memcpy((*skb)->data + ST_NCI_I2C_MIN_SIZE, buf, len);

	return 0;
}

/*
 * Reads an ndlc frame from the chip.
 *
 * On ST_NCI, IRQ goes in idle state when read starts.
 */
static irqreturn_t st_nci_irq_thread_fn(int irq, void *phy_id)
{
	struct st_nci_i2c_phy *phy = phy_id;
	struct i2c_client *client;
	struct sk_buff *skb = NULL;
	int r;

	if (!phy || !phy->ndlc || irq != phy->i2c_dev->irq) {
		WARN_ON_ONCE(1);
		return IRQ_NONE;
	}

	client = phy->i2c_dev;
	dev_dbg(&client->dev, "IRQ\n");

	if (phy->ndlc->hard_fault)
		return IRQ_HANDLED;

	if (!phy->ndlc->powered) {
		st_nci_i2c_disable(phy);
		return IRQ_HANDLED;
	}

	r = st_nci_i2c_read(phy, &skb);
	if (r == -EREMOTEIO || r == -ENOMEM || r == -EBADMSG)
		return IRQ_HANDLED;

	ndlc_recv(phy->ndlc, skb);

	return IRQ_HANDLED;
}

static struct nfc_phy_ops i2c_phy_ops = {
	.write = st_nci_i2c_write,
	.enable = st_nci_i2c_enable,
	.disable = st_nci_i2c_disable,
};

static int st_nci_i2c_acpi_request_resources(struct i2c_client *client)
{
	struct st_nci_i2c_phy *phy = i2c_get_clientdata(client);
	struct gpio_desc *gpiod_reset;
	struct device *dev = &client->dev;
	u8 tmp;

	/* Get RESET GPIO from ACPI */
	gpiod_reset = devm_gpiod_get_index(dev, ST_NCI_GPIO_NAME_RESET, 1,
					   GPIOD_OUT_HIGH);
	if (IS_ERR(gpiod_reset)) {
		nfc_err(dev, "Unable to get RESET GPIO\n");
		return -ENODEV;
	}

	phy->gpio_reset = desc_to_gpio(gpiod_reset);

	phy->irq_polarity = irq_get_trigger_type(client->irq);

	phy->se_status.is_ese_present = false;
	phy->se_status.is_uicc_present = false;

	if (device_property_present(dev, "ese-present")) {
		device_property_read_u8(dev, "ese-present", &tmp);
		phy->se_status.is_ese_present = tmp;
	}

	if (device_property_present(dev, "uicc-present")) {
		device_property_read_u8(dev, "uicc-present", &tmp);
		phy->se_status.is_uicc_present = tmp;
	}

	return 0;
}

static int st_nci_i2c_of_request_resources(struct i2c_client *client)
{
	struct st_nci_i2c_phy *phy = i2c_get_clientdata(client);
	struct device_node *pp;
	int gpio;
	int r;

	pp = client->dev.of_node;
	if (!pp)
		return -ENODEV;

	/* Get GPIO from device tree */
	gpio = of_get_named_gpio(pp, "reset-gpios", 0);
	if (gpio < 0) {
		nfc_err(&client->dev,
			"Failed to retrieve reset-gpios from device tree\n");
		return gpio;
	}

	/* GPIO request and configuration */
	r = devm_gpio_request_one(&client->dev, gpio,
				GPIOF_OUT_INIT_HIGH, ST_NCI_GPIO_NAME_RESET);
	if (r) {
		nfc_err(&client->dev, "Failed to request reset pin\n");
		return r;
	}
	phy->gpio_reset = gpio;

	phy->irq_polarity = irq_get_trigger_type(client->irq);

	phy->se_status.is_ese_present =
				of_property_read_bool(pp, "ese-present");
	phy->se_status.is_uicc_present =
				of_property_read_bool(pp, "uicc-present");

	return 0;
}

static int st_nci_i2c_request_resources(struct i2c_client *client)
{
	struct st_nci_nfc_platform_data *pdata;
	struct st_nci_i2c_phy *phy = i2c_get_clientdata(client);
	int r;

	pdata = client->dev.platform_data;
	if (pdata == NULL) {
		nfc_err(&client->dev, "No platform data\n");
		return -EINVAL;
	}

	/* store for later use */
	phy->gpio_reset = pdata->gpio_reset;
	phy->irq_polarity = pdata->irq_polarity;

	r = devm_gpio_request_one(&client->dev,
			phy->gpio_reset, GPIOF_OUT_INIT_HIGH,
			ST_NCI_GPIO_NAME_RESET);
	if (r) {
		pr_err("%s : reset gpio_request failed\n", __FILE__);
		return r;
	}

	phy->se_status.is_ese_present = pdata->is_ese_present;
	phy->se_status.is_uicc_present = pdata->is_uicc_present;

	return 0;
}

static int st_nci_i2c_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct st_nci_i2c_phy *phy;
	struct st_nci_nfc_platform_data *pdata;
	int r;

	dev_dbg(&client->dev, "%s\n", __func__);
	dev_dbg(&client->dev, "IRQ: %d\n", client->irq);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		nfc_err(&client->dev, "Need I2C_FUNC_I2C\n");
		return -ENODEV;
	}

	phy = devm_kzalloc(&client->dev, sizeof(struct st_nci_i2c_phy),
			   GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->i2c_dev = client;

	i2c_set_clientdata(client, phy);

	pdata = client->dev.platform_data;
	if (!pdata && client->dev.of_node) {
		r = st_nci_i2c_of_request_resources(client);
		if (r) {
			nfc_err(&client->dev, "No platform data\n");
			return r;
		}
	} else if (pdata) {
		r = st_nci_i2c_request_resources(client);
		if (r) {
			nfc_err(&client->dev,
				"Cannot get platform resources\n");
			return r;
		}
	} else if (ACPI_HANDLE(&client->dev)) {
		r = st_nci_i2c_acpi_request_resources(client);
		if (r) {
			nfc_err(&client->dev, "Cannot get ACPI data\n");
			return r;
		}
	} else {
		nfc_err(&client->dev,
			"st_nci platform resources not available\n");
		return -ENODEV;
	}

	r = ndlc_probe(phy, &i2c_phy_ops, &client->dev,
			ST_NCI_FRAME_HEADROOM, ST_NCI_FRAME_TAILROOM,
			&phy->ndlc, &phy->se_status);
	if (r < 0) {
		nfc_err(&client->dev, "Unable to register ndlc layer\n");
		return r;
	}

	phy->irq_active = true;
	r = devm_request_threaded_irq(&client->dev, client->irq, NULL,
				st_nci_irq_thread_fn,
				phy->irq_polarity | IRQF_ONESHOT,
				ST_NCI_DRIVER_NAME, phy);
	if (r < 0)
		nfc_err(&client->dev, "Unable to register IRQ handler\n");

	return r;
}

static int st_nci_i2c_remove(struct i2c_client *client)
{
	struct st_nci_i2c_phy *phy = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __func__);

	ndlc_remove(phy->ndlc);

	return 0;
}

static struct i2c_device_id st_nci_i2c_id_table[] = {
	{ST_NCI_DRIVER_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, st_nci_i2c_id_table);

static const struct acpi_device_id st_nci_i2c_acpi_match[] = {
	{"SMO2101"},
	{"SMO2102"},
	{}
};
MODULE_DEVICE_TABLE(acpi, st_nci_i2c_acpi_match);

static const struct of_device_id of_st_nci_i2c_match[] = {
	{ .compatible = "st,st21nfcb-i2c", },
	{ .compatible = "st,st21nfcb_i2c", },
	{ .compatible = "st,st21nfcc-i2c", },
	{}
};
MODULE_DEVICE_TABLE(of, of_st_nci_i2c_match);

static struct i2c_driver st_nci_i2c_driver = {
	.driver = {
		.name = ST_NCI_I2C_DRIVER_NAME,
		.of_match_table = of_match_ptr(of_st_nci_i2c_match),
		.acpi_match_table = ACPI_PTR(st_nci_i2c_acpi_match),
	},
	.probe = st_nci_i2c_probe,
	.id_table = st_nci_i2c_id_table,
	.remove = st_nci_i2c_remove,
};
module_i2c_driver(st_nci_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
