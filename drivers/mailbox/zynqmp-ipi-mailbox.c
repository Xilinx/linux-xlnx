// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Inter Processor Interrupt(IPI) Mailbox Driver
 *
 * Copyright (C) 2018 Xilinx, Inc.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
#include <linux/mailbox/zynqmp-ipi-message.h>

/* IPI agent ID any */
#define IPI_ID_ANY 0xFFUL

/* indicate if ZynqMP IPI mailbox driver uses SMC calls or HVC calls */
#define USE_SMC 0
#define USE_HVC 1

/* Default IPI SMC function IDs */
#define SMC_IPI_MAILBOX_OPEN		0x82001000U
#define SMC_IPI_MAILBOX_RELEASE		0x82001001U
#define SMC_IPI_MAILBOX_STATUS_ENQUIRY	0x82001002U
#define SMC_IPI_MAILBOX_NOTIFY		0x82001003U
#define SMC_IPI_MAILBOX_ACK		0x82001004U
#define SMC_IPI_MAILBOX_ENABLE_IRQ	0x82001005U
#define SMC_IPI_MAILBOX_DISABLE_IRQ	0x82001006U

/* IPI SMC Macros */
#define IPI_SMC_OPEN_IRQ_MASK		0x00000001UL /* IRQ enable bit in IPI
						      * open SMC call
						      */
#define IPI_SMC_NOTIFY_BLOCK_MASK	0x00000001UL /* Flag to indicate if
						      * IPI notification needs
						      * to be blocking.
						      */
#define IPI_SMC_ENQUIRY_DIRQ_MASK	0x00000001UL /* Flag to indicate if
						      * notification interrupt
						      * to be disabled.
						      */
#define IPI_SMC_ACK_EIRQ_MASK		0x00000001UL /* Flag to indicate if
						      * notification interrupt
						      * to be enabled.
						      */

/* IPI mailbox status */
#define IPI_MB_STATUS_IDLE		0
#define IPI_MB_STATUS_SEND_PENDING	1
#define IPI_MB_STATUS_RECV_PENDING	2

#define IPI_MB_CHNL_TX	0 /* IPI mailbox TX channel */
#define IPI_MB_CHNL_RX	1 /* IPI mailbox RX channel */

/**
 * struct zynqmp_ipi_mchan - Description of a Xilinx ZynqMP IPI mailbox channel
 * @is_opened: indicate if the IPI channel is opened
 * @req_buf: local to remote request buffer start address
 * @resp_buf: local to remote response buffer start address
 * @req_buf_size: request buffer size
 * @resp_buf_size: response buffer size
 * @chan_type: channel type
 */
struct zynqmp_ipi_mchan {
	int is_opened;
	void __iomem *req_buf;
	void __iomem *resp_buf;
	size_t req_buf_size;
	size_t resp_buf_size;
	unsigned int chan_type;
};

/**
 * struct zynqmp_ipi_mbox_pdata - Description of a ZynqMP IPI mailbox
 *                                platform data.
 * @dev:                  device pointer corresponding to the Xilinx ZynqMP
 *                        IPI mailbox
 * @local_id:             local IPI agent ID
 * @remote_id:            remote IPI agent ID
 * @method:               IPI SMC or HVC is going to be used
 * @mbox:                 mailbox Controller
 * @mchans:               array for channels, tx channel and rx channel.
 * @irq:                  IPI agent interrupt ID
 * @lock:                 IPI mailbox platform data lock
 */
struct zynqmp_ipi_mbox_pdata {
	struct device *dev;
	u32 local_id;
	u32 remote_id;
	unsigned int method;
	struct mbox_controller mbox;
	struct zynqmp_ipi_mchan mchans[2];
	int irq;
	spinlock_t lock; /* spin lock for local data */
};

static void zynqmp_ipi_fw_call(struct zynqmp_ipi_mbox_pdata *pdata,
			       unsigned long a0, unsigned long a3,
			       unsigned long a4, unsigned long a5,
			       unsigned long a6, unsigned long a7,
			       struct arm_smccc_res *res)
{
	unsigned long a1, a2;

	a1 = pdata->local_id;
	a2 = pdata->remote_id;
	if (pdata->method == USE_SMC)
		arm_smccc_smc(a0, a1, a2, a3, a4, a5, a6, a7, res);
	else
		arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

/**
 * zynqmp_ipi_interrupt - Interrupt handler for IPI notification
 *
 * @irq:  Interrupt number
 * @data: ZynqMP IPI mailbox platform data.
 *
 * Return: -EINVAL if there is no instance
 * IRQ_NONE if the interrupt is not ours.
 * IRQ_HANDLED if the rx interrupt was successfully handled.
 */
static irqreturn_t zynqmp_ipi_interrupt(int irq, void *data)
{
	struct zynqmp_ipi_mbox_pdata *pdata = data;
	struct mbox_chan *chan;
	struct zynqmp_ipi_mchan *mchan;
	struct zynqmp_ipi_message msg;
	u64 arg0, arg3;
	struct arm_smccc_res res;
	int ret;

	arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
	arg3 = IPI_SMC_ENQUIRY_DIRQ_MASK;
	zynqmp_ipi_fw_call(pdata, arg0, arg3, 0, 0, 0, 0, &res);
	ret = (int)(res.a0 & 0xFFFFFFFF);
	if (ret > 0 && ret & IPI_MB_STATUS_RECV_PENDING) {
		chan = &pdata->mbox.chans[IPI_MB_CHNL_RX];
		mchan = chan->con_priv;
		if (mchan->is_opened) {
			msg.len = mchan->req_buf_size;
			memcpy_fromio(&msg.data[0], mchan->req_buf, msg.len);
			/* Client will direclty copy data from
			 * IPI buffer to client data memory
			 */
			mbox_chan_received_data(chan, (void *)&msg);
			return IRQ_HANDLED;
		}
	}
	return IRQ_NONE;
}

/**
 * zynqmp_ipi_peek_data - Peek to see if there are any rx messages.
 *
 * @chan: Channel Pointer
 *
 * Return: 'true' if there is pending rx data, 'false' if there is none.
 */
static bool zynqmp_ipi_peek_data(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox_pdata *pdata = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	int ret;
	u64 arg0;
	struct arm_smccc_res res;

	if (WARN_ON(!pdata)) {
		dev_err(dev, "no platform drv data??\n");
		return false;
	}

	arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
	zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
	ret = (int)(res.a0 & 0xFFFFFFFF);

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* TX channel, check if the message has been acked
		 * by the remote, if yes, response is available.
		 */
		if (ret < 0 || ret & IPI_MB_STATUS_SEND_PENDING)
			return false;
		else
			return true;
	} else if (ret > 0 && ret & IPI_MB_STATUS_RECV_PENDING) {
		/* RX channel, check if there is message arrived. */
		return true;
	}
	return false;
}

/**
 * zynqmp_ipi_last_tx_done - See if the last tx message is sent
 *
 * @chan: Channel pointer
 *
 * Return: 'true' is no pending tx data, 'false' if there are any.
 */
static bool zynqmp_ipi_last_tx_done(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox_pdata *pdata = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	int ret;
	u64 arg0;
	struct arm_smccc_res res;
	struct zynqmp_ipi_message msg;

	if (WARN_ON(!pdata)) {
		dev_err(dev, "no platform drv data??\n");
		return false;
	}

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* We only need to check if the message been taken
		 * by the remote in the TX channel
		 */
		arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
		/* Check the SMC call status, a0 of the result */
		ret = (int)(res.a0 & 0xFFFFFFFF);
		if (ret < 0 || ret & IPI_MB_STATUS_SEND_PENDING)
			return false;

		msg.len = mchan->resp_buf_size;
		memcpy_fromio(&msg.data[0], mchan->resp_buf, msg.len);
		/* Client will direclty copy data from
		 * IPI buffer to client data memory
		 */
		mbox_chan_received_data(chan, (void *)&msg);
		return true;
	}
	/* Always true for the response message in RX channel */
	return true;
}

/**
 * zynqmp_ipi_send_data - Send data
 *
 * @chan: Channel Pointer
 * @data: Message Pointer
 *
 * Return: 0 if all goes good, else appropriate error messages.
 */
static int zynqmp_ipi_send_data(struct mbox_chan *chan, void *data)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox_pdata *pdata = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	struct zynqmp_ipi_message *msg = data;
	u64 arg0;
	struct arm_smccc_res res;
	u32 timeout;
	int ret;

	if (WARN_ON(!pdata)) {
		dev_err(dev, "no platform drv data??\n");
		return -EINVAL;
	}

	if (mchan->chan_type == IPI_MB_CHNL_TX) {
		/* Send request message */
		if (msg && msg->len > mchan->resp_buf_size) {
			dev_err(dev, "channel %d message length %u > max %lu\n",
				mchan->chan_type, (unsigned int)msg->len,
				mchan->resp_buf_size);
			return -EINVAL;
		}
		/* Enquire if the mailbox is free to send message */
		arg0 = SMC_IPI_MAILBOX_STATUS_ENQUIRY;
		timeout = 10;
		do {
			zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
			ret = res.a0 & 0xFFFFFFFF;
			if (ret >= 0 && !(ret & IPI_MB_STATUS_SEND_PENDING))
				break;
			usleep_range(1, 2);
			timeout--;
		} while (timeout);
		if (!timeout) {
			dev_warn(dev, "channel %d sending msg timesout.\n",
				 pdata->remote_id);
			return -ETIME;
		}
		/* Copy message to the request buffer */
		if (msg && msg->len)
			memcpy_toio(mchan->req_buf, &msg->data[0], msg->len);
		/* Kick IPI mailbox to send message */
		arg0 = SMC_IPI_MAILBOX_NOTIFY;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
	} else {
		/* Send response message */
		if (msg && msg->len > mchan->resp_buf_size) {
			dev_err(dev, "channel %d message length %u > max %lu\n",
				mchan->chan_type, (unsigned int)msg->len,
				mchan->resp_buf_size);
			return -EINVAL;
		}
		if (msg && msg->len)
			memcpy_toio(mchan->resp_buf, &msg->data[0], msg->len);
		arg0 = SMC_IPI_MAILBOX_NOTIFY;
		arg0 = SMC_IPI_MAILBOX_ACK;
		zynqmp_ipi_fw_call(pdata, arg0, IPI_SMC_ACK_EIRQ_MASK,
				   0, 0, 0, 0, &res);
	}
	return 0;
}

/**
 * zynqmp_ipi_startup - Startup the IPI channel
 *
 * @chan: Channel pointer
 *
 * Return: 0 if all goes good, else return corresponding error message
 */
static int zynqmp_ipi_startup(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox_pdata *pdata = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	u64 arg0;
	struct arm_smccc_res res;
	int ret = 0;
	unsigned long flags;
	unsigned int nchan_type;

	spin_lock_irqsave(&pdata->lock, flags);
	if (mchan->is_opened) {
		/* IPI mailbox has been opened */
		spin_unlock_irqrestore(&pdata->lock, flags);
		return -EBUSY;
	}

	/* If no channel has been opened, open the IPI mailbox */
	nchan_type = (mchan->chan_type + 1) % 2;
	if (!pdata->mchans[nchan_type].is_opened) {
		arg0 = SMC_IPI_MAILBOX_OPEN;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
		/* Check the SMC call status, a0 of the result */
		ret = (int)(res.a0 | 0xFFFFFFFF);
		if (res.a0 < 0) {
			dev_err(dev, "SMC to open the IPI channel failed.\n");
			ret = res.a0;
			spin_unlock_irqrestore(&pdata->lock, flags);
			return ret;
		}
		ret = 0;
	}

	/* If it is RX channel, enable the IPI notification interrupt */
	if (mchan->chan_type == IPI_MB_CHNL_RX) {
		arg0 = SMC_IPI_MAILBOX_ENABLE_IRQ;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
	}
	mchan->is_opened = 1;
	spin_unlock_irqrestore(&pdata->lock, flags);

	return ret;
}

/**
 * zynqmp_ipi_shutdown - Shutdown the IPI channel
 *
 * @chan: Channel pointer
 */
static void zynqmp_ipi_shutdown(struct mbox_chan *chan)
{
	struct device *dev = chan->mbox->dev;
	struct zynqmp_ipi_mbox_pdata *pdata = dev_get_drvdata(dev);
	struct zynqmp_ipi_mchan *mchan = chan->con_priv;
	u64 arg0;
	struct arm_smccc_res res;
	unsigned long flags;
	unsigned int chan_type;

	spin_lock_irqsave(&pdata->lock, flags);
	if (!mchan->is_opened) {
		spin_unlock_irqrestore(&pdata->lock, flags);
		return;
	}

	/* If it is RX channel, disable notification interrupt */
	chan_type = mchan->chan_type;
	if (chan_type == IPI_MB_CHNL_RX) {
		arg0 = SMC_IPI_MAILBOX_DISABLE_IRQ;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
	}
	/* Release IPI mailbox if no other channel is opened */
	chan_type = (chan_type + 1) % 2;
	if (!pdata->mchans[chan_type].is_opened) {
		arg0 = SMC_IPI_MAILBOX_RELEASE;
		zynqmp_ipi_fw_call(pdata, arg0, 0, 0, 0, 0, 0, &res);
	}

	mchan->is_opened = 0;
	spin_unlock_irqrestore(&pdata->lock, flags);
}

/* ZynqMP IPI mailbox operations */
static const struct mbox_chan_ops zynqmp_ipi_chan_ops = {
	.startup = zynqmp_ipi_startup,
	.shutdown = zynqmp_ipi_shutdown,
	.peek_data = zynqmp_ipi_peek_data,
	.last_tx_done = zynqmp_ipi_last_tx_done,
	.send_data = zynqmp_ipi_send_data,
};

/**
 * zynqmp_ipi_of_xlate - Translate of phandle to IPI mailbox channel
 *
 * @mbox: mailbox controller pointer
 * @p:    phandle pointer
 *
 * Return: Mailbox channel, else return error pointer.
 */
static struct mbox_chan *zynqmp_ipi_of_xlate(struct mbox_controller *mbox,
					     const struct of_phandle_args *p)
{
	struct zynqmp_ipi_mbox_pdata *pdata;
	struct mbox_chan *chan;
	struct device *dev = mbox->dev;
	unsigned int chan_type;

	pdata = container_of(mbox, struct zynqmp_ipi_mbox_pdata, mbox);

	/* Only supports TX and RX channels */
	chan_type = p->args[0];
	if (chan_type != IPI_MB_CHNL_TX && chan_type != IPI_MB_CHNL_RX) {
		dev_err(dev, "req chnl failure: invalid chnl type %u.\n",
			chan_type);
		return ERR_PTR(-EINVAL);
	}
	chan = &mbox->chans[chan_type];
	return chan;
}

static const struct of_device_id zynqmp_ipi_of_match[] = {
	{ .compatible = "xlnx,zynqmp-ipi-mailbox" },
	{},
};
MODULE_DEVICE_TABLE(of, zynqmp_ipi_of_match);

static int zynqmp_ipi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct zynqmp_ipi_mbox_pdata *pdata;
	struct zynqmp_ipi_mchan *mchan;
	struct mbox_chan *chans;
	struct mbox_controller *mbox;
	const unsigned char *prop;
	struct resource *res;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = dev;

	mchan = &pdata->mchans[IPI_MB_CHNL_TX];
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "local_request_region");
	mchan->req_buf = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mchan->req_buf)) {
		dev_err(dev, "Unable to map IPI buffer I/O memory\n");
		return PTR_ERR(mchan->req_buf);
	}
	mchan->req_buf_size = resource_size(res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "remote_response_region");
	mchan->resp_buf = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mchan->resp_buf)) {
		dev_err(dev, "Unable to map IPI buffer I/O memory\n");
		return PTR_ERR(mchan->resp_buf);
	}
	mchan->resp_buf_size = resource_size(res);

	mchan = &pdata->mchans[IPI_MB_CHNL_RX];
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "remote_request_region");
	mchan->req_buf = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mchan->req_buf)) {
		dev_err(dev, "Unable to map IPI buffer I/O memory\n");
		return PTR_ERR(mchan->req_buf);
	}
	mchan->req_buf_size = resource_size(res);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "local_response_region");
	mchan->resp_buf = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mchan->resp_buf)) {
		dev_err(dev, "Unable to map IPI buffer I/O memory\n");
		return PTR_ERR(mchan->resp_buf);
	}
	mchan->resp_buf_size = resource_size(res);

	/* Get the IPI local and remote agents IDs */
	ret = of_property_read_u32_index(np, "xlnx,ipi-ids", 0,
					 &pdata->local_id);
	if (ret < 0) {
		dev_err(dev, "No IPI local ID is specified.\n");
		return ret;
	}
	ret = of_property_read_u32_index(np, "xlnx,ipi-ids", 1,
					 &pdata->remote_id);
	if (ret < 0) {
		dev_err(dev, "No IPI remote ID is specified.\n");
		return ret;
	}

	/* Get how to access IPI agent method */
	prop = of_get_property(np, "method", NULL);
	if (!prop) {
		pdata->method = USE_SMC;
	} else if (!strcmp(prop, "smc")) {
		pdata->method = USE_SMC;
	} else if (!strcmp(prop, "hvc")) {
		pdata->method = USE_HVC;
	} else {
		dev_err(dev, "Invalid \"method\" %s.\n", prop);
		return ret;
	}

	/* IPI IRQ */
	pdata->irq = platform_get_irq(pdev, 0);
	if (pdata->irq < 0) {
		dev_err(dev, "unable to find IPI IRQ.\n");
		return pdata->irq;
	}
	ret = devm_request_irq(dev, pdata->irq, zynqmp_ipi_interrupt,
			       IRQF_SHARED, dev_name(dev), pdata);
	if (ret) {
		dev_err(dev, "IRQ %d is not requested successfully.\n",
			pdata->irq);
		return ret;
	}

	mbox = &pdata->mbox;
	mbox->dev = dev;
	mbox->ops = &zynqmp_ipi_chan_ops;
	/* Each mailbox has tx and rx channels. */
	mbox->num_chans = 2;
	mbox->txdone_irq = false;
	mbox->txdone_poll = true;
	mbox->txpoll_period = 5;
	mbox->of_xlate = zynqmp_ipi_of_xlate;
	chans = devm_kzalloc(dev, mbox->num_chans * sizeof(*chans), GFP_KERNEL);
	if (!chans)
		return -ENOMEM;
	mbox->chans = chans;
	mbox->chans[IPI_MB_CHNL_TX].con_priv = &pdata->mchans[IPI_MB_CHNL_TX];
	mbox->chans[IPI_MB_CHNL_RX].con_priv = &pdata->mchans[IPI_MB_CHNL_RX];
	pdata->mchans[IPI_MB_CHNL_TX].chan_type = IPI_MB_CHNL_TX;
	pdata->mchans[IPI_MB_CHNL_RX].chan_type = IPI_MB_CHNL_RX;
	spin_lock_init(&pdata->lock);
	platform_set_drvdata(pdev, pdata);
	ret = mbox_controller_register(mbox);
	if (ret)
		dev_err(dev, "Failed to register mbox_controller(%d)\n", ret);
	else
		dev_info(dev, "Probed ZynqMP IPI Mailbox driver.\n");
	return ret;
}

static int zynqmp_ipi_remove(struct platform_device *pdev)
{
	struct zynqmp_ipi_mbox_pdata *pdata;

	pdata = platform_get_drvdata(pdev);
	mbox_controller_unregister(&pdata->mbox);

	return 0;
}

static struct platform_driver zynqmp_ipi_driver = {
	.probe = zynqmp_ipi_probe,
	.remove = zynqmp_ipi_remove,
	.driver = {
		   .name = "zynqmp-ipi",
		   .of_match_table = of_match_ptr(zynqmp_ipi_of_match),
	},
};

static struct class zynqmp_ipi_class = { .name = "zynqmp_ipi_mbox", };

static int __init zynqmp_ipi_init(void)
{
	int err;

	err = class_register(&zynqmp_ipi_class);
	if (err)
		return err;

	return platform_driver_register(&zynqmp_ipi_driver);
}
subsys_initcall(zynqmp_ipi_init);

static void __exit zynqmp_ipi_exit(void)
{
	platform_driver_unregister(&zynqmp_ipi_driver);
	class_unregister(&zynqmp_ipi_class);
}
module_exit(zynqmp_ipi_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Xilinx ZynqMP IPI Mailbox driver");
MODULE_AUTHOR("Xilinx Inc.");
