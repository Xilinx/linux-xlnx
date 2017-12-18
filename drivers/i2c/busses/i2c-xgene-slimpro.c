/*
 * X-Gene SLIMpro I2C Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Feng Kan <fkan@apm.com>
 * Author: Hieu Le <hnle@apm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver provides support for X-Gene SLIMpro I2C device access
 * using the APM X-Gene SLIMpro mailbox driver.
 *
 */
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#define MAILBOX_OP_TIMEOUT		1000	/* Operation time out in ms */
#define MAILBOX_I2C_INDEX		0
#define SLIMPRO_IIC_BUS			1	/* Use I2C bus 1 only */

#define SMBUS_CMD_LEN			1
#define BYTE_DATA			1
#define WORD_DATA			2
#define BLOCK_DATA			3

#define SLIMPRO_IIC_I2C_PROTOCOL	0
#define SLIMPRO_IIC_SMB_PROTOCOL	1

#define SLIMPRO_IIC_READ		0
#define SLIMPRO_IIC_WRITE		1

#define IIC_SMB_WITHOUT_DATA_LEN	0
#define IIC_SMB_WITH_DATA_LEN		1

#define SLIMPRO_DEBUG_MSG		0
#define SLIMPRO_MSG_TYPE_SHIFT		28
#define SLIMPRO_DBG_SUBTYPE_I2C1READ	4
#define SLIMPRO_DBGMSG_TYPE_SHIFT	24
#define SLIMPRO_DBGMSG_TYPE_MASK	0x0F000000U
#define SLIMPRO_IIC_DEV_SHIFT		23
#define SLIMPRO_IIC_DEV_MASK		0x00800000U
#define SLIMPRO_IIC_DEVID_SHIFT		13
#define SLIMPRO_IIC_DEVID_MASK		0x007FE000U
#define SLIMPRO_IIC_RW_SHIFT		12
#define SLIMPRO_IIC_RW_MASK		0x00001000U
#define SLIMPRO_IIC_PROTO_SHIFT		11
#define SLIMPRO_IIC_PROTO_MASK		0x00000800U
#define SLIMPRO_IIC_ADDRLEN_SHIFT	8
#define SLIMPRO_IIC_ADDRLEN_MASK	0x00000700U
#define SLIMPRO_IIC_DATALEN_SHIFT	0
#define SLIMPRO_IIC_DATALEN_MASK	0x000000FFU

/*
 * SLIMpro I2C message encode
 *
 * dev		- Controller number (0-based)
 * chip		- I2C chip address
 * op		- SLIMPRO_IIC_READ or SLIMPRO_IIC_WRITE
 * proto	- SLIMPRO_IIC_SMB_PROTOCOL or SLIMPRO_IIC_I2C_PROTOCOL
 * addrlen	- Length of the address field
 * datalen	- Length of the data field
 */
#define SLIMPRO_IIC_ENCODE_MSG(dev, chip, op, proto, addrlen, datalen) \
	((SLIMPRO_DEBUG_MSG << SLIMPRO_MSG_TYPE_SHIFT) | \
	((SLIMPRO_DBG_SUBTYPE_I2C1READ << SLIMPRO_DBGMSG_TYPE_SHIFT) & \
	SLIMPRO_DBGMSG_TYPE_MASK) | \
	((dev << SLIMPRO_IIC_DEV_SHIFT) & SLIMPRO_IIC_DEV_MASK) | \
	((chip << SLIMPRO_IIC_DEVID_SHIFT) & SLIMPRO_IIC_DEVID_MASK) | \
	((op << SLIMPRO_IIC_RW_SHIFT) & SLIMPRO_IIC_RW_MASK) | \
	((proto << SLIMPRO_IIC_PROTO_SHIFT) & SLIMPRO_IIC_PROTO_MASK) | \
	((addrlen << SLIMPRO_IIC_ADDRLEN_SHIFT) & SLIMPRO_IIC_ADDRLEN_MASK) | \
	((datalen << SLIMPRO_IIC_DATALEN_SHIFT) & SLIMPRO_IIC_DATALEN_MASK))

/*
 * Encode for upper address for block data
 */
#define SLIMPRO_IIC_ENCODE_FLAG_BUFADDR			0x80000000
#define SLIMPRO_IIC_ENCODE_FLAG_WITH_DATA_LEN(a)	((u32) (((a) << 30) \
								& 0x40000000))
#define SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(a)		((u32) (((a) >> 12) \
								& 0x3FF00000))
#define SLIMPRO_IIC_ENCODE_ADDR(a)			((a) & 0x000FFFFF)

struct slimpro_i2c_dev {
	struct i2c_adapter adapter;
	struct device *dev;
	struct mbox_chan *mbox_chan;
	struct mbox_client mbox_client;
	struct completion rd_complete;
	u8 dma_buffer[I2C_SMBUS_BLOCK_MAX + 1]; /* dma_buffer[0] is used for length */
	u32 *resp_msg;
};

#define to_slimpro_i2c_dev(cl)	\
		container_of(cl, struct slimpro_i2c_dev, mbox_client)

static void slimpro_i2c_rx_cb(struct mbox_client *cl, void *mssg)
{
	struct slimpro_i2c_dev *ctx = to_slimpro_i2c_dev(cl);

	/*
	 * Response message format:
	 * mssg[0] is the return code of the operation
	 * mssg[1] is the first data word
	 * mssg[2] is NOT used
	 */
	if (ctx->resp_msg)
		*ctx->resp_msg = ((u32 *)mssg)[1];

	if (ctx->mbox_client.tx_block)
		complete(&ctx->rd_complete);
}

static int start_i2c_msg_xfer(struct slimpro_i2c_dev *ctx)
{
	if (ctx->mbox_client.tx_block) {
		if (!wait_for_completion_timeout(&ctx->rd_complete,
						 msecs_to_jiffies(MAILBOX_OP_TIMEOUT)))
			return -ETIMEDOUT;
	}

	/* Check of invalid data or no device */
	if (*ctx->resp_msg == 0xffffffff)
		return -ENODEV;

	return 0;
}

static int slimpro_i2c_rd(struct slimpro_i2c_dev *ctx, u32 chip,
			  u32 addr, u32 addrlen, u32 protocol,
			  u32 readlen, u32 *data)
{
	u32 msg[3];
	int rc;

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip,
					SLIMPRO_IIC_READ, protocol, addrlen, readlen);
	msg[1] = SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = 0;
	ctx->resp_msg = data;
	rc = mbox_send_message(ctx->mbox_chan, &msg);
	if (rc < 0)
		goto err;

	rc = start_i2c_msg_xfer(ctx);
err:
	ctx->resp_msg = NULL;
	return rc;
}

static int slimpro_i2c_wr(struct slimpro_i2c_dev *ctx, u32 chip,
			  u32 addr, u32 addrlen, u32 protocol, u32 writelen,
			  u32 data)
{
	u32 msg[3];
	int rc;

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip,
					SLIMPRO_IIC_WRITE, protocol, addrlen, writelen);
	msg[1] = SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = data;
	ctx->resp_msg = msg;

	rc = mbox_send_message(ctx->mbox_chan, &msg);
	if (rc < 0)
		goto err;

	rc = start_i2c_msg_xfer(ctx);
err:
	ctx->resp_msg = NULL;
	return rc;
}

static int slimpro_i2c_blkrd(struct slimpro_i2c_dev *ctx, u32 chip, u32 addr,
			     u32 addrlen, u32 protocol, u32 readlen,
			     u32 with_data_len, void *data)
{
	dma_addr_t paddr;
	u32 msg[3];
	int rc;

	paddr = dma_map_single(ctx->dev, ctx->dma_buffer, readlen, DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->dev, paddr)) {
		dev_err(&ctx->adapter.dev, "Error in mapping dma buffer %p\n",
			ctx->dma_buffer);
		rc = -ENOMEM;
		goto err;
	}

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip, SLIMPRO_IIC_READ,
					protocol, addrlen, readlen);
	msg[1] = SLIMPRO_IIC_ENCODE_FLAG_BUFADDR |
		 SLIMPRO_IIC_ENCODE_FLAG_WITH_DATA_LEN(with_data_len) |
		 SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(paddr) |
		 SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = (u32)paddr;
	ctx->resp_msg = msg;

	rc = mbox_send_message(ctx->mbox_chan, &msg);
	if (rc < 0)
		goto err_unmap;

	rc = start_i2c_msg_xfer(ctx);

	/* Copy to destination */
	memcpy(data, ctx->dma_buffer, readlen);

err_unmap:
	dma_unmap_single(ctx->dev, paddr, readlen, DMA_FROM_DEVICE);
err:
	ctx->resp_msg = NULL;
	return rc;
}

static int slimpro_i2c_blkwr(struct slimpro_i2c_dev *ctx, u32 chip,
			     u32 addr, u32 addrlen, u32 protocol, u32 writelen,
			     void *data)
{
	dma_addr_t paddr;
	u32 msg[3];
	int rc;

	memcpy(ctx->dma_buffer, data, writelen);
	paddr = dma_map_single(ctx->dev, ctx->dma_buffer, writelen,
			       DMA_TO_DEVICE);
	if (dma_mapping_error(ctx->dev, paddr)) {
		dev_err(&ctx->adapter.dev, "Error in mapping dma buffer %p\n",
			ctx->dma_buffer);
		rc = -ENOMEM;
		goto err;
	}

	msg[0] = SLIMPRO_IIC_ENCODE_MSG(SLIMPRO_IIC_BUS, chip, SLIMPRO_IIC_WRITE,
					protocol, addrlen, writelen);
	msg[1] = SLIMPRO_IIC_ENCODE_FLAG_BUFADDR |
		 SLIMPRO_IIC_ENCODE_UPPER_BUFADDR(paddr) |
		 SLIMPRO_IIC_ENCODE_ADDR(addr);
	msg[2] = (u32)paddr;
	ctx->resp_msg = msg;

	if (ctx->mbox_client.tx_block)
		reinit_completion(&ctx->rd_complete);

	rc = mbox_send_message(ctx->mbox_chan, &msg);
	if (rc < 0)
		goto err_unmap;

	rc = start_i2c_msg_xfer(ctx);

err_unmap:
	dma_unmap_single(ctx->dev, paddr, writelen, DMA_TO_DEVICE);
err:
	ctx->resp_msg = NULL;
	return rc;
}

static int xgene_slimpro_i2c_xfer(struct i2c_adapter *adap, u16 addr,
				  unsigned short flags, char read_write,
				  u8 command, int size,
				  union i2c_smbus_data *data)
{
	struct slimpro_i2c_dev *ctx = i2c_get_adapdata(adap);
	int ret = -EOPNOTSUPP;
	u32 val;

	switch (size) {
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, 0, 0,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, &val);
			data->byte = val;
		} else {
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     0, 0);
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, &val);
			data->byte = val;
		} else {
			val = data->byte;
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     BYTE_DATA, val);
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_rd(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     WORD_DATA, &val);
			data->word = val;
		} else {
			val = data->word;
			ret = slimpro_i2c_wr(ctx, addr, command, SMBUS_CMD_LEN,
					     SLIMPRO_IIC_SMB_PROTOCOL,
					     WORD_DATA, val);
		}
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_blkrd(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_SMB_PROTOCOL,
						I2C_SMBUS_BLOCK_MAX + 1,
						IIC_SMB_WITH_DATA_LEN,
						&data->block[0]);

		} else {
			ret = slimpro_i2c_blkwr(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_SMB_PROTOCOL,
						data->block[0] + 1,
						&data->block[0]);
		}
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			ret = slimpro_i2c_blkrd(ctx, addr,
						command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_I2C_PROTOCOL,
						I2C_SMBUS_BLOCK_MAX,
						IIC_SMB_WITHOUT_DATA_LEN,
						&data->block[1]);
		} else {
			ret = slimpro_i2c_blkwr(ctx, addr, command,
						SMBUS_CMD_LEN,
						SLIMPRO_IIC_I2C_PROTOCOL,
						data->block[0],
						&data->block[1]);
		}
		break;
	default:
		break;
	}
	return ret;
}

/*
* Return list of supported functionality.
*/
static u32 xgene_slimpro_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK;
}

static struct i2c_algorithm xgene_slimpro_i2c_algorithm = {
	.smbus_xfer = xgene_slimpro_i2c_xfer,
	.functionality = xgene_slimpro_i2c_func,
};

static int xgene_slimpro_i2c_probe(struct platform_device *pdev)
{
	struct slimpro_i2c_dev *ctx;
	struct i2c_adapter *adapter;
	struct mbox_client *cl;
	int rc;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = &pdev->dev;
	platform_set_drvdata(pdev, ctx);
	cl = &ctx->mbox_client;

	/* Request mailbox channel */
	cl->dev = &pdev->dev;
	cl->rx_callback = slimpro_i2c_rx_cb;
	cl->tx_block = true;
	init_completion(&ctx->rd_complete);
	cl->tx_tout = MAILBOX_OP_TIMEOUT;
	cl->knows_txdone = false;
	ctx->mbox_chan = mbox_request_channel(cl, MAILBOX_I2C_INDEX);
	if (IS_ERR(ctx->mbox_chan)) {
		dev_err(&pdev->dev, "i2c mailbox channel request failed\n");
		return PTR_ERR(ctx->mbox_chan);
	}

	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (rc)
		dev_warn(&pdev->dev, "Unable to set dma mask\n");

	/* Setup I2C adapter */
	adapter = &ctx->adapter;
	snprintf(adapter->name, sizeof(adapter->name), "MAILBOX I2C");
	adapter->algo = &xgene_slimpro_i2c_algorithm;
	adapter->class = I2C_CLASS_HWMON;
	adapter->dev.parent = &pdev->dev;
	i2c_set_adapdata(adapter, ctx);
	rc = i2c_add_adapter(adapter);
	if (rc) {
		mbox_free_channel(ctx->mbox_chan);
		return rc;
	}

	dev_info(&pdev->dev, "Mailbox I2C Adapter registered\n");
	return 0;
}

static int xgene_slimpro_i2c_remove(struct platform_device *pdev)
{
	struct slimpro_i2c_dev *ctx = platform_get_drvdata(pdev);

	i2c_del_adapter(&ctx->adapter);

	mbox_free_channel(ctx->mbox_chan);

	return 0;
}

static const struct of_device_id xgene_slimpro_i2c_dt_ids[] = {
	{.compatible = "apm,xgene-slimpro-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, xgene_slimpro_i2c_dt_ids);

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_slimpro_i2c_acpi_ids[] = {
	{"APMC0D40", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, xgene_slimpro_i2c_acpi_ids);
#endif

static struct platform_driver xgene_slimpro_i2c_driver = {
	.probe	= xgene_slimpro_i2c_probe,
	.remove	= xgene_slimpro_i2c_remove,
	.driver	= {
		.name	= "xgene-slimpro-i2c",
		.of_match_table = of_match_ptr(xgene_slimpro_i2c_dt_ids),
		.acpi_match_table = ACPI_PTR(xgene_slimpro_i2c_acpi_ids)
	},
};

module_platform_driver(xgene_slimpro_i2c_driver);

MODULE_DESCRIPTION("APM X-Gene SLIMpro I2C driver");
MODULE_AUTHOR("Feng Kan <fkan@apm.com>");
MODULE_AUTHOR("Hieu Le <hnle@apm.com>");
MODULE_LICENSE("GPL");
