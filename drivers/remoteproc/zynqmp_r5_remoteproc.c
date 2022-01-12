// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq R5 Remote Processor driver
 *
 * Based on origin OMAP and Zynq Remote Processor driver
 *
 */

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/zynqmp-ipi-message.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include <linux/skbuff.h>
#include <linux/sysfs.h>

#include "remoteproc_internal.h"

#define MAX_RPROCS	2 /* Support up to 2 RPU */
#define BANK_LIST_PROP	"sram"
#define DDR_LIST_PROP	"memory-region"
#define PD_PROP		"power-domain"

/* IPI buffer MAX length */
#define IPI_BUF_LEN_MAX	32U
/* RX mailbox client buffer max length */
#define RX_MBOX_CLIENT_BUF_MAX	(IPI_BUF_LEN_MAX + \
				 sizeof(struct zynqmp_ipi_message))
#define MAX_BANKS 4U

/**
 * struct xlnx_r5_data - match data to handle SoC variations
 * @versal_soc: flag to denote if running on Versal SoC.
 */
struct xlnx_r5_data {
	bool versal_soc;
};

/*
 * Map each Xilinx on-chip SRAM  Bank address to their own respective
 * pm_node_id.
 */
struct sram_addr_data {
	enum pm_node_id ids[MAX_BANKS];
};

/**
 * struct zynqmp_r5_rproc - ZynqMP R5 core structure
 *
 * @rx_mc_buf: rx mailbox client buffer to save the rx message
 * @tx_mc: tx mailbox client
 * @rx_mc: rx mailbox client
 * @mbox_work: mbox_work for the RPU remoteproc
 * @tx_mc_skbs: socket buffers for tx mailbox client
 * @dev: device of RPU instance
 * @rproc: rproc handle
 * @tx_chan: tx mailbox channel
 * @rx_chan: rx mailbox channel
 * @pnode_id: RPU CPU power domain id
 * @elem: linked list item
 * @versal: flag that if on, denotes this driver is for Versal SoC.
 */
struct zynqmp_r5_rproc {
	unsigned char rx_mc_buf[RX_MBOX_CLIENT_BUF_MAX];
	struct mbox_client tx_mc;
	struct mbox_client rx_mc;
	struct work_struct mbox_work;
	struct sk_buff_head tx_mc_skbs;
	struct device *dev;
	struct rproc *rproc;
	struct mbox_chan *tx_chan;
	struct mbox_chan *rx_chan;
	u32 pnode_id;
	struct list_head elem;
	bool versal;
};

/*
 * r5_set_mode - set RPU operation mode
 * @z_rproc: Remote processor private data
 * @rpu_mode: mode specified by device tree to configure the RPU to
 *
 * set RPU operation mode
 *
 * Return: 0 for success, negative value for failure
 */
static int r5_set_mode(struct zynqmp_r5_rproc *z_rproc,
		       enum rpu_oper_mode rpu_mode)
{
	enum rpu_tcm_comb tcm_mode;
	enum rpu_oper_mode cur_rpu_mode;
	int ret;

	ret = zynqmp_pm_get_rpu_mode(z_rproc->pnode_id, &cur_rpu_mode);
	if (ret < 0)
		return ret;

	if (rpu_mode != cur_rpu_mode) {
		ret = zynqmp_pm_set_rpu_mode(z_rproc->pnode_id,
					     rpu_mode);
		if (ret < 0)
			return ret;
	}

	tcm_mode = (rpu_mode == PM_RPU_MODE_LOCKSTEP) ?
		    PM_RPU_TCM_COMB : PM_RPU_TCM_SPLIT;
	return zynqmp_pm_set_tcm_config(z_rproc->pnode_id, tcm_mode);
}

/*
 * zynqmp_r5_rproc_mem_release
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem entry to unmap
 *
 * Unmap SRAM banks when powering down R5 core.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int sram_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;
	struct device *dev = &rproc->dev;
	unsigned int i;
	int ret = 0;
	struct sram_addr_data *sram_banks = (struct sram_addr_data *)mem->priv;
	u32 pnode_id, status, usage = 0;

	iounmap(mem->va);
	/*
	 * This loop is to go over each bank in sram property.
	 *
	 * Note that each of the functions zynqmp_pm_get_node_status and
	 * zynqmp_pm_release_node will not error out and only report with
	 * dev_warn as there may be other nodes remaining to attempt to
	 * power off.
	 *
	 * If zynqmp_pm_get_node_status fails, then the subsequent call to
	 * zynqmp_pm_release_node should also fail so error out in this case.
	 */

	for (i = 0; i < MAX_BANKS; i++) {
		pnode_id = sram_banks->ids[i];

		if (!pnode_id)
			continue;

		if (z_rproc->versal) {
			/* only request node if not already requested */
			ret = zynqmp_pm_get_node_status(pnode_id, &status, NULL, &usage);
			if (ret) {
				dev_warn(dev, "Unable to get status for node %d\n",
					 pnode_id);
				continue;
			}
		}

		if (usage || !z_rproc->versal) {
			ret = zynqmp_pm_release_node(pnode_id);
			if (ret < 0)
				dev_warn(dev, "Unable to release node %d\n",
					 pnode_id);
		}
	}

	return ret;
}

/*
 * zynqmp_r5_rproc_start
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Start R5 Core from designated boot address.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_start(struct rproc *rproc)
{
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;
	enum rpu_boot_mem bootmem;
	int ret;
	u32 status;

	bootmem = (rproc->bootaddr & 0xF0000000) == 0xF0000000 ?
		  PM_RPU_BOOTMEM_HIVEC : PM_RPU_BOOTMEM_LOVEC;

	dev_dbg(rproc->dev.parent, "RPU boot from %s.",
		bootmem == PM_RPU_BOOTMEM_HIVEC ? "OCM" : "TCM");

	/* If core is already powered on, turn off before re-wake. */
	ret = zynqmp_pm_get_node_status(z_rproc->pnode_id, &status, NULL, NULL);
	if (ret)
		return ret;

	if (status) {
		ret = zynqmp_pm_force_pwrdwn(z_rproc->pnode_id,
					     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		if (ret)
			return ret;
	}

	return zynqmp_pm_request_wake(z_rproc->pnode_id, 1,
				     bootmem, ZYNQMP_PM_REQUEST_ACK_NO);
}

/*
 * zynqmp_r5_rproc_stop
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Power down  R5 Core.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_stop(struct rproc *rproc)
{
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;

	return zynqmp_pm_force_pwrdwn(z_rproc->pnode_id,
				     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
}

/*
 * zynqmp_r5_rproc_mem_alloc
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem entry to map
 *
 * Callback to map va for memory-region's carveout.
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_mem_alloc(struct rproc *rproc,
				     struct rproc_mem_entry *mem)
{
	void *va;

	va = ioremap_wc(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va))
		return -ENOMEM;

	mem->va = va;

	return 0;
}

/*
 * zynqmp_r5_rproc_mem_release
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem entry to unmap
 *
 * Unmap memory-region carveout
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_rproc_mem_release(struct rproc *rproc,
				       struct rproc_mem_entry *mem)
{
	iounmap(mem->va);
	return 0;
}

/*
 * parse_mem_regions
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Construct rproc mem carveouts from carveout provided in
 * memory-region property
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int parse_mem_regions(struct rproc *rproc)
{
	int num_mems, i;
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;
	struct device *dev = &rproc->dev;
	struct device_node *np = z_rproc->dev->of_node;
	struct rproc_mem_entry *mem;

	num_mems = of_count_phandle_with_args(np, DDR_LIST_PROP, NULL);
	if (num_mems <= 0)
		return 0;

	for (i = 0; i < num_mems; i++) {
		struct device_node *node;
		struct reserved_mem *rmem;

		node = of_parse_phandle(np, DDR_LIST_PROP, i);
		if (!node)
			return -EINVAL;

		rmem = of_reserved_mem_lookup(node);
		if (!rmem)
			return -EINVAL;

		if (strstr(node->name, "vdev0vring")) {
			int vring_id;
			char name[16];

			/*
			 * expecting form of "rpuXvdev0vringX as documented
			 * in xilinx remoteproc device tree binding
			 */
			if (strlen(node->name) < 15) {
				dev_err(dev, "%pOF is less than 14 chars",
					node);
				of_node_put(node);
				return -EINVAL;
			}

			/*
			 * can be 1 of multiple vring IDs per IPC channel
			 * e.g. 'vdev0vring0' and 'vdev0vring1'
			 */
			vring_id = node->name[14] - '0';
			snprintf(name, sizeof(name), "vdev0vring%d", vring_id);
			/* Register vring */
			mem = rproc_mem_entry_init(dev, NULL,
						   (dma_addr_t)rmem->base,
						   rmem->size, rmem->base,
						   zynqmp_r5_rproc_mem_alloc,
						   zynqmp_r5_rproc_mem_release,
						   name);
		} else {
			/* Register DMA region */
			int (*alloc)(struct rproc *r,
				     struct rproc_mem_entry *rme);
			int (*release)(struct rproc *r,
				       struct rproc_mem_entry *rme);
			char name[20];

			if (strstr(node->name, "vdev0buffer")) {
				alloc = NULL;
				release = NULL;
				strcpy(name, "vdev0buffer");
			} else {
				alloc = zynqmp_r5_rproc_mem_alloc;
				release = zynqmp_r5_rproc_mem_release;
				strcpy(name, node->name);
			}

			mem = rproc_mem_entry_init(dev, NULL,
						   (dma_addr_t)rmem->base,
						   rmem->size, rmem->base,
						   alloc, release, name);
		}

		of_node_put(node);

		if (!mem)
			return -ENOMEM;

		rproc_add_carveout(rproc, mem);
	}

	return 0;
}

/**
 * zynqmp_r5_pm_request_sram - power up SRAMS for R5.
 * @dev: Device pointer of rproc instance.
 * @dt_node: node with power domain to request use for power-on.
 * @versal: denote whether to use Versal or ZU+ platform IDs
 * @sram_banks: list of srams associated with carveout
 * Given sram, attempt to power up the bank with its corresponding Xilinx
 * Platform Management ID.
 *
 * Ensure SRAMs associated with the core are powered on.
 *
 * Return: return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_pm_request_sram(struct device *dev,
				     struct device_node *dt_node, bool versal,
				     struct sram_addr_data *sram_banks)
{
	int ret, index, num_banks, sram_index = 0;
	u32 pnode_id, status, usage = 0;

	if (!of_get_property(dt_node, PD_PROP, NULL))
		return -EINVAL;

	num_banks = of_count_phandle_with_args(dt_node, PD_PROP, NULL);

	/*
	 * As the elements are in the order of:
	 * <SoC_firmware> <power-domain> ...
	 * Only use even elements in this list.
	 *
	 * Note that each of the functions zynqmp_pm_get_node_status and
	 * zynqmp_pm_request_node will not error out and only report with
	 * dev_warn as there may be other nodes remaining to attempt to
	 * power on.
	 *
	 * If zynqmp_pm_get_node_status fails, then the subsequent call to
	 * zynqmp_pm_request_node should also fail so error out in this case.
	 */
	for (index = 1; index < num_banks; index += 2) {
		of_property_read_u32_index(dt_node, PD_PROP, index, &pnode_id);

		if (versal) {
			/* only request node if not already requested */
			ret = zynqmp_pm_get_node_status(pnode_id, &status, NULL,
							&usage);
			if (ret) {
				dev_err(dev, "get status for node %d, %d\n",
					pnode_id, ret);
				return ret;
			}
		}

		if (!versal || usage == PM_USAGE_NO_MASTER) {
			ret = zynqmp_pm_request_node(pnode_id,
						     ZYNQMP_PM_CAPABILITY_ACCESS,
						     0,
						     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
			if (ret < 0) {
				dev_err(dev, "Unable to request node %d\n",
					pnode_id);
				return ret;
			}
		} else {
			ret = 0;
		}

		/* Only have 4 elements in sram_banks so use indices 0-3. */
		sram_banks->ids[sram_index++] = pnode_id;
	}

	return 0;
}

/*
 * sram_mem_alloc
 * @rproc: single R5 core's corresponding rproc instance
 * @mem: mem entry to initialize the va and da fields of
 *
 * Given SRAM bank entry,
 * this callback will set device address for R5 running on TCM
 * and also setup virtual address for TCM bank remoteproc carveout
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int sram_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	void *va;
	struct device *dev = rproc->dev.parent;

	va = ioremap_wc(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va))
		return -ENOMEM;

	/* Update memory entry va */
	mem->va = va;

	va = devm_ioremap_wc(dev, mem->da, mem->len);
	if (!va)
		return -ENOMEM;
	/*
	 * Handle TCM translation for R5-relative addresses. Here go from
	 * start of TM0A to end of TCM1B in mapping.
	 */
	if (mem->da >= 0xffe00000UL && mem->da < 0xffeb0000UL + 0x10000UL) {
		/* As R5 is 32 bit, wipe out extra high bits */
		mem->da &= 0x000fffff;
		/*
		 * The R5s expect their TCM banks to be at address 0x0 and 0x2000,
		 * while on the Linux side they are at 0xffexxxxx. Zero out the high
		 * 12 bits of the address.
		 */

		/*
		 * TCM Banks 1A and 1B (0xffe90000 and 0xffeb0000) still
		 * need to be translated to 0x0 and 0x20000
		 */
		if (mem->da == 0x90000 || mem->da == 0xB0000)
			mem->da -= 0x90000;

		/* if translated TCM bank address is not valid report error */
		if (mem->da != 0x0 && mem->da != 0x20000) {
			dev_err(dev, "invalid TCM bank address: %x\n", mem->da);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * parse_tcm_banks()
 * @rproc: single R5 core's corresponding rproc instance
 *
 * Given R5 node in remoteproc instance
 * allocate remoteproc carveout for TCM memory
 * needed for firmware to be loaded
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int parse_tcm_banks(struct rproc *rproc)
{
	int i, num_banks, ret = 0;
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;
	struct device *dev = &rproc->dev;
	struct device_node *r5_node = z_rproc->dev->of_node;
	struct sram_addr_data *sram_banks;

	/* go through TCM banks for r5 node */
	num_banks = of_count_phandle_with_args(r5_node, BANK_LIST_PROP, NULL);
	if (num_banks <= 0) {
		dev_err(dev, "need to specify TCM banks\n");
		return -EINVAL;
	}
	for (i = 0; i < num_banks; i++) {
		struct resource rsc;
		resource_size_t size;
		struct device_node *dt_node;
		struct rproc_mem_entry *mem;
		int ret;

		dt_node = of_parse_phandle(r5_node, BANK_LIST_PROP, i);
		if (!dt_node)
			return -EINVAL;

		if (of_device_is_available(dt_node)) {
			ret = of_address_to_resource(dt_node, 0, &rsc);
			if (ret < 0)
				return ret;

			size = resource_size(&rsc);

			/*
			 * This is used later to power off the banks when
			 * stopping rpu core.
			 *
			 * This will be managed as a result of 'devm' so does
			 * not need a free.
			 */
			sram_banks = devm_kzalloc(dev, sizeof(struct sram_addr_data),
						  GFP_KERNEL);
			if (!sram_banks) {
				of_node_put(dt_node);
				return -ENOMEM;
			}

			ret = zynqmp_r5_pm_request_sram(dev, dt_node,
							z_rproc->versal,
							sram_banks);
			if (ret < 0) {
				of_node_put(dt_node);
				goto error;
			}

			/* add carveout */
			mem = rproc_mem_entry_init(dev, NULL, rsc.start,
						   (int)size, rsc.start,
						   sram_mem_alloc,
						   sram_mem_release,
						   rsc.name);
			if (!mem)
				return -ENOMEM;

			mem->priv = (void *)sram_banks;
			rproc_add_carveout(rproc, mem);
		}
		of_node_put(dt_node);
	}
	return 0;
error:
	return ret;
}

/*
 * zynqmp_r5_parse_fw()
 * @rproc: single R5 core's corresponding rproc instance
 * @fw: ptr to firmware to be loaded onto r5 core
 *
 * When loading firmware, ensure the necessary carveouts are in remoteproc
 *
 * return 0 on success, otherwise non-zero value on failure
 */
static int zynqmp_r5_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	int ret;

	ret = parse_tcm_banks(rproc);
	if (ret)
		return ret;

	ret = parse_mem_regions(rproc);
	if (ret)
		return ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		/*
		 * resource table only required for IPC.
		 * if not present, this is not necessarily an error;
		 * for example, loading r5 hello world application
		 * so simply inform user and keep going.
		 */
		dev_info(&rproc->dev, "no resource table found.\n");
		ret = 0;
	}
	return ret;
}

/*
 * zynqmp_r5_rproc_kick() - kick a firmware if mbox is provided
 * @rproc: r5 core's corresponding rproc structure
 * @vqid: virtqueue ID
 */
static void zynqmp_r5_rproc_kick(struct rproc *rproc, int vqid)
{
	struct sk_buff *skb = NULL;
	unsigned int skb_len = 0;
	struct zynqmp_ipi_message *mb_msg = NULL;
	int ret = 0;

	struct device *dev = rproc->dev.parent;
	struct zynqmp_r5_rproc *z_rproc = rproc->priv;

	if (of_property_read_bool(dev->of_node, "mboxes")) {
		skb_len = (unsigned int)(sizeof(vqid) + sizeof(mb_msg));
		skb = alloc_skb(skb_len, GFP_ATOMIC);
		if (!skb)
			return;

		mb_msg = (struct zynqmp_ipi_message *)skb_put(skb, skb_len);
		mb_msg->len = sizeof(vqid);
		memcpy(mb_msg->data, &vqid, sizeof(vqid));

		skb_queue_tail(&z_rproc->tx_mc_skbs, skb);
		ret = mbox_send_message(z_rproc->tx_chan, mb_msg);
		if (ret < 0) {
			dev_warn(dev, "Failed to kick remote.\n");
			skb_dequeue_tail(&z_rproc->tx_mc_skbs);
			kfree_skb(skb);
		}
	} else {
		(void)skb;
		(void)skb_len;
		(void)mb_msg;
		(void)ret;
		(void)vqid;
	}
}

static struct rproc_ops zynqmp_r5_rproc_ops = {
	.start		= zynqmp_r5_rproc_start,
	.stop		= zynqmp_r5_rproc_stop,
	.load		= rproc_elf_load_segments,
	.parse_fw	= zynqmp_r5_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
	.kick		= zynqmp_r5_rproc_kick,
};

/**
 * event_notified_idr_cb() - event notified idr callback
 * @id: idr id
 * @ptr: pointer to idr private data
 * @data: data passed to idr_for_each callback
 *
 * Pass notification to remoteproc virtio
 *
 * Return: 0. having return is to satisfy the idr_for_each() function
 *          pointer input argument requirement.
 **/
static int event_notified_idr_cb(int id, void *ptr, void *data)
{
	struct rproc *rproc = data;

	(void)rproc_vq_interrupt(rproc, id);
	return 0;
}

/**
 * handle_event_notified() - remoteproc notification work function
 * @work: pointer to the work structure
 *
 * It checks each registered remoteproc notify IDs.
 */
static void handle_event_notified(struct work_struct *work)
{
	struct rproc *rproc;
	struct zynqmp_r5_rproc *z_rproc;

	z_rproc = container_of(work, struct zynqmp_r5_rproc, mbox_work);

	(void)mbox_send_message(z_rproc->rx_chan, NULL);
	rproc = z_rproc->rproc;
	/*
	 * We only use IPI for interrupt. The firmware side may or may
	 * not write the notifyid when it trigger IPI.
	 * And thus, we scan through all the registered notifyids.
	 */
	idr_for_each(&rproc->notifyids, event_notified_idr_cb, rproc);
}

/**
 * zynqmp_r5_mb_rx_cb() - Receive channel mailbox callback
 * @cl: mailbox client
 * @msg: message pointer
 *
 * It will schedule the R5 notification work.
 */
static void zynqmp_r5_mb_rx_cb(struct mbox_client *cl, void *msg)
{
	struct zynqmp_r5_rproc *z_rproc;

	z_rproc = container_of(cl, struct zynqmp_r5_rproc, rx_mc);
	if (msg) {
		struct zynqmp_ipi_message *ipi_msg, *buf_msg;
		size_t len;

		ipi_msg = (struct zynqmp_ipi_message *)msg;
		buf_msg = (struct zynqmp_ipi_message *)z_rproc->rx_mc_buf;
		len = (ipi_msg->len >= IPI_BUF_LEN_MAX) ?
		      IPI_BUF_LEN_MAX : ipi_msg->len;
		buf_msg->len = len;
		memcpy(buf_msg->data, ipi_msg->data, len);
	}
	schedule_work(&z_rproc->mbox_work);
}

/**
 * zynqmp_r5_mb_tx_done() - Request has been sent to the remote
 * @cl: mailbox client
 * @msg: pointer to the message which has been sent
 * @r: status of last TX - OK or error
 *
 * It will be called by the mailbox framework when the last TX has done.
 */
static void zynqmp_r5_mb_tx_done(struct mbox_client *cl, void *msg, int r)
{
	struct zynqmp_r5_rproc *z_rproc;
	struct sk_buff *skb;

	if (!msg)
		return;
	z_rproc = container_of(cl, struct zynqmp_r5_rproc, tx_mc);
	skb = skb_dequeue(&z_rproc->tx_mc_skbs);
	kfree_skb(skb);
}

/**
 * zynqmp_r5_setup_mbox() - Setup mailboxes
 *			    this is used for each individual R5 core
 *
 * @z_rproc: pointer to the ZynqMP R5 processor platform data
 * @node: pointer of the device node
 *
 * Function to setup mailboxes to talk to RPU.
 *
 * Return: 0 for success, negative value for failure.
 */
static int zynqmp_r5_setup_mbox(struct zynqmp_r5_rproc *z_rproc,
				struct device_node *node)
{
	struct mbox_client *mclient;

	/* Setup TX mailbox channel client */
	mclient = &z_rproc->tx_mc;
	mclient->rx_callback = NULL;
	mclient->tx_block = false;
	mclient->knows_txdone = false;
	mclient->tx_done = zynqmp_r5_mb_tx_done;
	mclient->dev = z_rproc->dev;

	/* Setup TX mailbox channel client */
	mclient = &z_rproc->rx_mc;
	mclient->dev = z_rproc->dev;
	mclient->rx_callback = zynqmp_r5_mb_rx_cb;
	mclient->tx_block = false;
	mclient->knows_txdone = false;

	INIT_WORK(&z_rproc->mbox_work, handle_event_notified);

	/* Request TX and RX channels */
	z_rproc->tx_chan = mbox_request_channel_byname(&z_rproc->tx_mc, "tx");
	if (IS_ERR(z_rproc->tx_chan)) {
		dev_err(z_rproc->dev, "failed to request mbox tx channel.\n");
		z_rproc->tx_chan = NULL;
		return -EINVAL;
	}

	z_rproc->rx_chan = mbox_request_channel_byname(&z_rproc->rx_mc, "rx");
	if (IS_ERR(z_rproc->rx_chan)) {
		dev_err(z_rproc->dev, "failed to request mbox rx channel.\n");
		z_rproc->rx_chan = NULL;
		return -EINVAL;
	}
	skb_queue_head_init(&z_rproc->tx_mc_skbs);

	return 0;
}

/**
 * zynqmp_r5_probe() - Probes ZynqMP R5 processor device node
 *		       this is called for each individual R5 core to
 *		       set up mailbox, Xilinx platform manager unique ID,
 *		       add to rproc core
 *
 * @pdev: domain platform device for current R5 core
 * @node: pointer of the device node for current R5 core
 * @rpu_mode: mode to configure RPU, split or lockstep
 * @data: structure to hold SoC specific data
 * @z_rproc: Xilinx specific remoteproc structure used later to link
 *           in to cluster of cores
 *
 * Return: 0 for success, negative value for failure.
 */
static int zynqmp_r5_probe(struct platform_device *pdev,
			   struct device_node *node,
			   enum rpu_oper_mode rpu_mode,
			   const struct xlnx_r5_data *data,
			   struct zynqmp_r5_rproc **z_rproc)
{
	int ret, r5_entries, pnode_id;
	struct device *dev = &pdev->dev;
	struct rproc *rproc_ptr;

	/* Allocate remoteproc instance */
	rproc_ptr = devm_rproc_alloc(dev, dev_name(dev), &zynqmp_r5_rproc_ops,
				     NULL, sizeof(struct zynqmp_r5_rproc));
	if (!rproc_ptr) {
		ret = -ENOMEM;
		goto error;
	}

	rproc_ptr->auto_boot = false;
	*z_rproc = rproc_ptr->priv;
	(*z_rproc)->rproc = rproc_ptr;
	(*z_rproc)->dev = dev;
	/* Set up DMA mask */
	ret = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (ret)
		goto error;

	if (!of_get_property(node, PD_PROP, NULL)) {
		ret = -EINVAL;
		goto error;
	}

	/* expect 2 elements for RPU power-domain */
	r5_entries = of_count_phandle_with_args(node, PD_PROP, NULL);
	if (r5_entries < 2) {
		ret = -EINVAL;
		goto error;
	}

	/*
	 * Get R5 power domain node. As the elements are in the order of:
	 * SoC_firmware power-domain ...
	 * Only use every other element in this list.
	 */
	of_property_read_u32_index(node, PD_PROP, 1U, &pnode_id);
	(*z_rproc)->pnode_id = pnode_id;

	(*z_rproc)->versal = data->versal_soc;

	ret = r5_set_mode(*z_rproc, rpu_mode);
	if (ret)
		goto error;

	if (of_property_read_bool(node, "mboxes")) {
		ret = zynqmp_r5_setup_mbox(*z_rproc, node);
		if (ret)
			goto error;
	}

	/* Add R5 remoteproc */
	ret = devm_rproc_add(dev, rproc_ptr);
	if (ret)
		goto error;

	/*
	 * In Versal SoC, the Xilinx platform management firmware will power
	 * off the R5 cores if they are not requested. In this case, this call
	 * notifies Xilinx platform management firmware that the R5 core will
	 * be used and should be powered on.
	 *
	 * On ZynqMP platform this is not needed as the R5 cores are not
	 * powered off by default.
	 */
	if ((*z_rproc)->versal) {
		ret = zynqmp_pm_request_node((*z_rproc)->pnode_id,
					     ZYNQMP_PM_CAPABILITY_ACCESS, 0,
					     ZYNQMP_PM_REQUEST_ACK_BLOCKING);
		if (ret < 0)
			goto error;
	}

	return 0;
error:
	*z_rproc = NULL;
	return ret;
}

/*
 * zynqmp_r5_remoteproc_probe()
 *
 * @pdev: domain platform device for R5 cluster
 *
 * called when driver is probed, for each R5 core specified in DT,
 * setup as needed to do remoteproc-related operations
 *
 * Return: 0 for success, negative value for failure.
 */
static int zynqmp_r5_remoteproc_probe(struct platform_device *pdev)
{
	int ret, core_count;
	struct device *dev = &pdev->dev;
	struct device_node *nc;
	enum rpu_oper_mode rpu_mode = PM_RPU_MODE_LOCKSTEP;
	struct list_head *cluster; /* list to track each core's rproc */
	struct zynqmp_r5_rproc *z_rproc = NULL;
	struct platform_device *child_pdev;
	struct list_head *pos;
	const struct xlnx_r5_data *data;

	ret = of_property_read_u32(dev->of_node, "xlnx,cluster-mode", &rpu_mode);
	if (ret < 0 || (rpu_mode != PM_RPU_MODE_LOCKSTEP &&
			rpu_mode != PM_RPU_MODE_SPLIT)) {
		dev_err(dev, "invalid format cluster mode: ret %d mode %x\n",
			ret, rpu_mode);
		return ret;
	}

	dev_dbg(dev, "RPU configuration: %s\n",
		rpu_mode == PM_RPU_MODE_LOCKSTEP ? "lockstep" : "split");

	/*
	 * if 2 RPUs provided but one is lockstep, then we have an
	 * invalid configuration.
	 */

	core_count = of_get_available_child_count(dev->of_node);
	if ((rpu_mode == PM_RPU_MODE_LOCKSTEP && core_count != 1) ||
	    core_count > MAX_RPROCS)
		return -EINVAL;

	cluster = devm_kzalloc(dev, sizeof(*cluster), GFP_KERNEL);
	if (!cluster)
		return -ENOMEM;
	INIT_LIST_HEAD(cluster);

	ret = devm_of_platform_populate(dev);
	if (ret) {
		dev_err(dev, "devm_of_platform_populate failed, ret = %d\n",
			ret);
		return ret;
	}

	/*
	 * SoC specific information. For now, just flag to determine if
	 * on versal platform for node management.
	 */
	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(dev, "SoC-specific data is not defined\n");
		return -ENODEV;
	}

	/* probe each individual r5 core's remoteproc-related info */
	for_each_available_child_of_node(dev->of_node, nc) {
		child_pdev = of_find_device_by_node(nc);
		if (!child_pdev) {
			dev_err(dev, "could not get R5 core platform device\n");
			ret = -ENODEV;
			goto out;
		}
		ret = zynqmp_r5_probe(child_pdev, nc, rpu_mode, data,
				      &z_rproc);
		dev_dbg(dev, "%s to probe rpu %pOF\n",
			ret ? "Failed" : "Able",
			nc);
		if (!z_rproc)
			ret = -EINVAL;
		if (ret) {
			put_device(&child_pdev->dev);
			goto out;
		}

		list_add_tail(&z_rproc->elem, cluster);
		put_device(&child_pdev->dev);
	}
	/* wire in so each core can be cleaned up at driver remove */
	platform_set_drvdata(pdev, cluster);
	return 0;
out:
	/*
	 * undo core0 upon any failures on core1 in split-mode
	 *
	 * in zynqmp_r5_probe z_rproc is set to null
	 * and ret to non-zero value if error
	 */
	if (ret && !z_rproc && rpu_mode == PM_RPU_MODE_SPLIT &&
	    !list_empty(cluster)) {
		list_for_each(pos, cluster) {
			z_rproc = list_entry(pos, struct zynqmp_r5_rproc, elem);
			if (of_property_read_bool(z_rproc->dev->of_node, "mboxes")) {
				mbox_free_channel(z_rproc->tx_chan);
				mbox_free_channel(z_rproc->rx_chan);
			}
		}
	}
	return ret;
}

/*
 * zynqmp_r5_remoteproc_remove()
 *
 * @pdev: domain platform device for R5 cluster
 *
 * When the driver is unloaded, clean up the mailboxes for each
 * remoteproc that was initially probed.
 */
static int zynqmp_r5_remoteproc_remove(struct platform_device *pdev)
{
	struct list_head *pos, *temp, *cluster = (struct list_head *)
						 platform_get_drvdata(pdev);
	struct zynqmp_r5_rproc *z_rproc = NULL;

	list_for_each_safe(pos, temp, cluster) {
		z_rproc = list_entry(pos, struct zynqmp_r5_rproc, elem);

		/*
		 * For Versal platform, the Xilinx platform management
		 * firmware needs to have a release call to match the
		 * corresponding reque in order to power down the core.
		 */
		if (z_rproc->versal)
			zynqmp_pm_release_node(z_rproc->pnode_id);

		if (of_property_read_bool(z_rproc->dev->of_node, "mboxes")) {
			mbox_free_channel(z_rproc->tx_chan);
			mbox_free_channel(z_rproc->rx_chan);
		}
		list_del(pos);
	}
	return 0;
}

static const struct xlnx_r5_data versal_data = {
	.versal_soc = true,
};

static const struct xlnx_r5_data zynqmp_data = {
	.versal_soc = false,
};

static const struct of_device_id xilinx_r5_of_match[] = {
	{ .compatible = "xlnx,zynqmp-r5-remoteproc", .data = &zynqmp_data, },
	{ .compatible = "xlnx,versal-r5-remoteproc", .data = &versal_data, },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, xilinx_r5_of_match);

/* Match table for OF platform binding */
static struct platform_driver zynqmp_r5_remoteproc_driver = {
	.probe = zynqmp_r5_remoteproc_probe,
	.remove = zynqmp_r5_remoteproc_remove,
	.driver = {
		.name = "zynqmp_r5_remoteproc",
		.of_match_table = xilinx_r5_of_match,
	},
};
module_platform_driver(zynqmp_r5_remoteproc_driver);

MODULE_AUTHOR("Ben Levinsky <ben.levinsky@xilinx.com>");
MODULE_LICENSE("GPL v2");
