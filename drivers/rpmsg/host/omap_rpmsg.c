/*
 * Remote processor messaging transport (OMAP platform-specific bits)
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 * Copyright (C) 2011 Google, Inc.
 *
 * Ohad Ben-Cohen <ohad@wizery.com>
 * Brian Swetland <swetland@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/init.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/remoteproc.h>

#include <plat/mailbox.h>
#include <plat/dsp.h>

#include "omap_rpmsg.h"

/**
 * struct omap_rpmsg_vproc - omap's virtio remote processor state
 * @vdev: virtio device
 * @vring: phys address of two vrings; first one used for rx, 2nd one for tx
 * @buf_paddr: physical address of the IPC buffer region
 * @buf_size: size of IPC buffer region
 * @buf_mapped: kernel (ioremap'ed) address of IPC buffer region
 * @mbox_name: name of omap mailbox device to use with this vproc
 * @rproc_name: name of remote proc device to use with this vproc
 * @mbox: omap mailbox handle
 * @rproc: remoteproc handle
 * @nb: notifier block that will be invoked on inbound mailbox messages
 * @vq: virtio's virtqueues
 * @base_vq_id: index of first virtqueue that belongs to this vproc
 * @num_of_vqs: number of virtqueues this vproc owns
 * @static_chnls: table of static channels for this vproc
 */
struct omap_rpmsg_vproc {
	struct virtio_device vdev;
	unsigned int vring[2]; /* mpu owns first vring, ipu owns the 2nd */
	unsigned int buf_paddr;
	unsigned int buf_size; /* size must be page-aligned */
	void *buf_mapped;
	char *mbox_name;
	char *rproc_name;
	struct omap_mbox *mbox;
	struct rproc *rproc;
	struct notifier_block nb;
	struct virtqueue *vq[2];
	int base_vq_id;
	int num_of_vqs;
	struct rpmsg_channel_info *static_chnls;
};

#define to_omap_vproc(vd) container_of(vd, struct omap_rpmsg_vproc, vdev)

/**
 * struct omap_rpmsg_vq_info - virtqueue state
 * @num: number of buffers supported by the vring
 * @vq_id: a unique index of this virtqueue
 * @addr: address where the vring is mapped onto
 * @vproc: the virtual remote processor state
 *
 * Such a struct will be maintained for every virtqueue we're
 * using to communicate with the remote processor
 */
struct omap_rpmsg_vq_info {
	__u16 num;
	__u16 vq_id;
	void *addr;
	struct omap_rpmsg_vproc *vproc;
};

/*
 * For now, allocate 256 buffers of 512 bytes for each side. each buffer
 * will then have 16B for the msg header and 496B for the payload.
 * This will require a total space of 256KB for the buffers themselves, and
 * 3 pages for every vring (the size of the vring depends on the number of
 * buffers it supports).
 */
#define RPMSG_NUM_BUFS		(512)
#define RPMSG_BUF_SIZE		(512)
#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

/*
 * The alignment between the consumer and producer parts of the vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your BIOS image as well
 */
#define RPMSG_VRING_ALIGN	(4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RPMSG_RING_SIZE	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, \
				RPMSG_VRING_ALIGN), PAGE_SIZE)) * PAGE_SIZE)

/* The total IPC space needed to communicate with a remote processor */
#define RPMSG_IPC_MEM	(RPMSG_BUFS_SPACE + 2 * RPMSG_RING_SIZE)

/*
 * Provide rpmsg core with platform-specific configuration.
 * Since user data is at stake here, bugs can't be tolerated. hence
 * the BUG_ON approach on invalid lengths.
 *
 * For more info on these configuration requests, see enum
 * rpmsg_platform_requests.
 */
static void omap_rpmsg_get(struct virtio_device *vdev, unsigned int request,
		   void *buf, unsigned len)
{
	struct omap_rpmsg_vproc *vproc = to_omap_vproc(vdev);
	int tmp;

	switch (request) {
	case VPROC_BUF_ADDR:
		BUG_ON(len != sizeof(vproc->buf_mapped));
		memcpy(buf, &vproc->buf_mapped, len);
		break;
	case VPROC_BUF_PADDR:
		BUG_ON(len != sizeof(vproc->buf_paddr));
		memcpy(buf, &vproc->buf_paddr, len);
		break;
	case VPROC_BUF_NUM:
		BUG_ON(len != sizeof(tmp));
		tmp = RPMSG_NUM_BUFS;
		memcpy(buf, &tmp, len);
		break;
	case VPROC_BUF_SZ:
		BUG_ON(len != sizeof(tmp));
		tmp = RPMSG_BUF_SIZE;
		memcpy(buf, &tmp, len);
		break;
	case VPROC_STATIC_CHANNELS:
		BUG_ON(len != sizeof(vproc->static_chnls));
		memcpy(buf, &vproc->static_chnls, len);
		break;
	default:
		dev_err(&vdev->dev, "invalid request: %d\n", request);
	}
}

/* kick the remote processor, and let it know which virtqueue to poke at */
static void omap_rpmsg_notify(struct virtqueue *vq)
{
	struct omap_rpmsg_vq_info *rpvq = vq->priv;
	int ret;

	pr_debug("sending mailbox msg: %d\n", rpvq->vq_id);
	/* send the index of the triggered virtqueue in the mailbox payload */
	ret = omap_mbox_msg_send(rpvq->vproc->mbox, rpvq->vq_id);
	if (ret)
		pr_err("ugh, omap_mbox_msg_send() failed: %d\n", ret);
}

/**
 * omap_rpmsg_mbox_callback() - inbound mailbox message handler
 * @this: notifier block
 * @index: unused
 * @data: mailbox payload
 *
 * This handler is invoked by omap's mailbox driver whenever a mailbox
 * message is received. Usually, the mailbox payload simply contains
 * the index of the virtqueue that is kicked by the remote processor,
 * and we let virtio handle it.
 *
 * In addition to virtqueue indices, we also have some out-of-band values
 * that indicates different events. Those values are deliberately very
 * big so they don't coincide with virtqueue indices. Moreover,
 * they are rarely used, if used at all, and their necessity should
 * be revisited.
 */
static int omap_rpmsg_mbox_callback(struct notifier_block *this,
					unsigned long index, void *data)
{
	mbox_msg_t msg = (mbox_msg_t) data;
	struct omap_rpmsg_vproc *vproc;

	vproc = container_of(this, struct omap_rpmsg_vproc, nb);

	pr_debug("mbox msg: 0x%x\n", msg);

	switch (msg) {
	case RP_MBOX_CRASH:
		pr_err("%s has just crashed !\n", vproc->rproc_name);
		/* todo: smarter error handling here */
		break;
	case RP_MBOX_ECHO_REPLY:
		pr_info("received echo reply from %s !\n", vproc->rproc_name);
		break;
	case RP_MBOX_PENDING_MSG:
		/*
		 * a new inbound message is waiting in our rx vring (1st vring).
		 * Let's pretend the message explicitly contained the rx vring
		 * index number and handle it generically.
		 */
		msg = vproc->base_vq_id;
		/* intentional fall-through */
	default:
		/* ignore vq indices which are clearly not for us */
		if (msg < vproc->base_vq_id)
			break;

		msg -= vproc->base_vq_id;

		/*
		 * Currently both PENDING_MSG and explicit-virtqueue-index
		 * messaging are supported.
		 * Whatever approach is taken, at this point 'msg' contains
		 * the index of the vring which was just triggered.
		 */
		if (msg < vproc->num_of_vqs)
			vring_interrupt(msg, vproc->vq[msg]);
	}

	return NOTIFY_DONE;
}

/* prepare a virtqueue */
static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name)
{
	struct omap_rpmsg_vproc *vproc = to_omap_vproc(vdev);
	struct omap_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int err;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	rpvq->addr = (__force void *) ioremap_nocache(vproc->vring[index],
							RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	memset(rpvq->addr, 0, RPMSG_RING_SIZE);

	pr_debug("vring%d: phys 0x%x, virt 0x%x\n", index, vproc->vring[index],
					(unsigned int) rpvq->addr);

	vq = vring_new_virtqueue(RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN, vdev,
				rpvq->addr, omap_rpmsg_notify, callback, name);
	if (!vq) {
		pr_err("vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	vproc->vq[index] = vq;
	vq->priv = rpvq;
	/* unique id for this virtqueue */
	rpvq->vq_id = vproc->base_vq_id + index;
	rpvq->vproc = vproc;

	return vq;

unmap_vring:
	/* iounmap normal memory, so make sparse happy */
	iounmap((__force void __iomem *) rpvq->addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(err);
}

static void omap_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct omap_rpmsg_vproc *vproc = to_omap_vproc(vdev);

	if (vproc->rproc)
		rproc_put(vproc->rproc);

	if (vproc->mbox)
		omap_mbox_put(vproc->mbox, &vproc->nb);

	if (vproc->buf_mapped)
		/* iounmap normal memory, so make sparse happy */
		iounmap((__force void __iomem *)vproc->buf_mapped);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct omap_rpmsg_vq_info *rpvq = vq->priv;
		vring_del_virtqueue(vq);
		/* iounmap normal memory, so make sparse happy */
		iounmap((__force void __iomem *) rpvq->addr);
		kfree(rpvq);
	}
}

static int omap_rpmsg_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char *names[])
{
	struct omap_rpmsg_vproc *vproc = to_omap_vproc(vdev);
	int i, err;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	vproc->num_of_vqs = nvqs;

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	vproc->buf_mapped = (__force void *) ioremap_nocache(vproc->buf_paddr,
							vproc->buf_size);
	if (!vproc->buf_mapped) {
		pr_err("ioremap failed\n");
		err = -ENOMEM;
		goto error;
	}

	/* for now, use mailbox's notifiers. later that can be optimized */
	vproc->nb.notifier_call = omap_rpmsg_mbox_callback;
	vproc->mbox = omap_mbox_get(vproc->mbox_name, &vproc->nb);
	if (IS_ERR(vproc->mbox)) {
		pr_err("failed to get mailbox %s\n", vproc->mbox_name);
		err = -EINVAL;
		goto error;
	}

	pr_debug("buf: phys 0x%x, virt 0x%x\n", vproc->buf_paddr,
					(unsigned int) vproc->buf_mapped);

	/* tell the M3 we're ready (so M3 will know we're sane) */
	err = omap_mbox_msg_send(vproc->mbox, RP_MBOX_READY);
	if (err) {
		pr_err("ugh, omap_mbox_msg_send() failed: %d\n", err);
		goto error;
	}

	/* send it the physical address of the vrings + IPC buffer */
	err = omap_mbox_msg_send(vproc->mbox, (mbox_msg_t) vproc->buf_paddr);
	if (err) {
		pr_err("ugh, omap_mbox_msg_send() failed: %d\n", err);
		goto error;
	}

	/* ping the remote processor. this is only for sanity-sake;
	 * there is no functional effect whatsoever */
	err = omap_mbox_msg_send(vproc->mbox, RP_MBOX_ECHO_REQUEST);
	if (err) {
		pr_err("ugh, omap_mbox_msg_send() failed: %d\n", err);
		goto error;
	}

	/* now load the firmware, and boot the M3 */
	vproc->rproc = rproc_get(vproc->rproc_name);
	if (!vproc->rproc) {
		pr_err("failed to get rproc %s\n", vproc->rproc_name);
		err = -EINVAL;
		goto error;
	}

	return 0;

error:
	omap_rpmsg_del_vqs(vdev);
	return err;
}

/*
 * should be nice to add firmware support for these handlers.
 * for now provide them so virtio doesn't crash
 */
static u8 omap_rpmsg_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void omap_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "new status: %d\n", status);
}

static void omap_rpmsg_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset !\n");
}

static u32 omap_rpmsg_get_features(struct virtio_device *vdev)
{
	/* for now, use hardcoded bitmap. later this should be provided
	 * by the firmware itself */
	return 1 << VIRTIO_RPMSG_F_NS;
}

static void omap_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
}

static void omap_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static struct virtio_config_ops omap_rpmsg_config_ops = {
	.get_features	= omap_rpmsg_get_features,
	.finalize_features = omap_rpmsg_finalize_features,
	.get		= omap_rpmsg_get,
	.find_vqs	= omap_rpmsg_find_vqs,
	.del_vqs	= omap_rpmsg_del_vqs,
	.reset		= omap_rpmsg_reset,
	.set_status	= omap_rpmsg_set_status,
	.get_status	= omap_rpmsg_get_status,
};

/*
 * Populating the static channels table.
 *
 * This is not always required, and the example below just demonstrates
 * how to populate it with a static server channel.
 *
 * This example should be moved to Documentation/rpmsh.h before merging.
 *
 * For more info, see 'struct rpmsg_channel_info'.
 */
static struct rpmsg_channel_info omap_ipuc0_static_chnls[] = {
	RMSG_SERVER_CHNL("rpmsg-server-sample", 137),
	{ },
};

static struct rpmsg_channel_info omap_ipuc1_static_chnls[] = {
	{ },
};

static struct omap_rpmsg_vproc omap_rpmsg_vprocs[] = {
	/* ipu_c0's rpmsg backend */
	{
		.vdev.id.device	= VIRTIO_ID_RPMSG,
		.vdev.config	= &omap_rpmsg_config_ops,
		.mbox_name	= "mailbox-1",
		.rproc_name	= "ipu",
		/* core 0 is using indices 0 + 1 for its vqs */
		.base_vq_id	= 0,
		.static_chnls = omap_ipuc0_static_chnls,
	},
	/* ipu_c1's rpmsg backend */
	{
		.vdev.id.device	= VIRTIO_ID_RPMSG,
		.vdev.config	= &omap_rpmsg_config_ops,
		.mbox_name	= "mailbox-1",
		.rproc_name	= "ipu",
		/* core 1 is using indices 2 + 3 for its vqs */
		.base_vq_id	= 2,
		.static_chnls = omap_ipuc1_static_chnls,
	},
};

static int __init omap_rpmsg_ini(void)
{
	int i, ret = 0;
	/*
	 * This whole area generally needs some rework.
	 * E.g, consider using dma_alloc_coherent for the IPC buffers and
	 * vrings, use CMA, etc...
	 */
	phys_addr_t paddr = omap_dsp_get_mempool_base();
	phys_addr_t psize = omap_dsp_get_mempool_size();

	/*
	 * allocate carverout memory for the buffers and vring, and
	 * then register the vproc virtio device
	 */
	for (i = 0; i < ARRAY_SIZE(omap_rpmsg_vprocs); i++) {
		struct omap_rpmsg_vproc *vproc = &omap_rpmsg_vprocs[i];

		if (psize < RPMSG_IPC_MEM) {
			pr_err("out of carveout memory: %d (%d)\n", psize, i);
			return -ENOMEM;
		}

		vproc->buf_paddr = paddr;
		vproc->buf_size = RPMSG_BUFS_SPACE;
		vproc->vring[0] = paddr + RPMSG_BUFS_SPACE;
		vproc->vring[1] = paddr + RPMSG_BUFS_SPACE + RPMSG_RING_SIZE;

		paddr += RPMSG_IPC_MEM;
		psize -= RPMSG_IPC_MEM;

		pr_debug("vproc%d: buf 0x%x, vring0 0x%x, vring1 0x%x\n", i,
			vproc->buf_paddr, vproc->vring[0], vproc->vring[1]);

		vproc->vdev.dev.release = omap_rpmsg_vproc_release;

		ret = register_virtio_device(&vproc->vdev);
		if (ret) {
			pr_err("failed to register vproc: %d\n", ret);
			break;
		}
	}

	return ret;
}
module_init(omap_rpmsg_ini);

static void __exit omap_rpmsg_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(omap_rpmsg_vprocs); i++) {
		struct omap_rpmsg_vproc *vproc = &omap_rpmsg_vprocs[i];

		unregister_virtio_device(&vproc->vdev);
	}
}
module_exit(omap_rpmsg_fini);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OMAP Remote processor messaging virtio device");
