// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP AES Driver.
 * Copyright (c) 2018 Xilinx Inc.
 */

#include <crypto/aes.h>
#include <crypto/scatterwalk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/firmware/xilinx/zynqmp/firmware.h>

#define ZYNQMP_AES_QUEUE_LENGTH			1
#define ZYNQMP_AES_IV_SIZE			12
#define ZYNQMP_AES_GCM_SIZE			16
#define ZYNQMP_AES_KEY_SIZE			32

#define ZYNQMP_AES_DECRYPT			0
#define ZYNQMP_AES_ENCRYPT			1

#define ZYNQMP_AES_KUP_KEY			0

#define ZYNQMP_AES_GCM_TAG_MISMATCH_ERR		0x01
#define ZYNQMP_AES_SIZE_ERR			0x06
#define ZYNQMP_AES_WRONG_KEY_SRC_ERR		0x13

struct zynqmp_aes_dev {
	struct list_head list;
	struct device *dev;
	/* the lock protects queue and dev list */
	spinlock_t lock;
	struct crypto_queue queue;
};

struct zynqmp_aes_op {
	struct zynqmp_aes_dev *dd;
	void *src;
	void *dst;
	int len;
	u8 key[ZYNQMP_AES_KEY_SIZE];
	u8 *iv;
	u32 keylen;
	u32 keytype;
};

struct zynqmp_aes_data {
	u64 src;
	u64 iv;
	u64 key;
	u64 dst;
	u64 size;
	u64 optype;
	u64 keysrc;
};

struct zynqmp_aes_drv {
	struct list_head dev_list;
	/* the lock protects dev list */
	spinlock_t lock;
};

static struct zynqmp_aes_drv zynqmp_aes = {
	.dev_list = LIST_HEAD_INIT(zynqmp_aes.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(zynqmp_aes.lock),
};

static struct zynqmp_aes_dev *zynqmp_aes_find_dev(struct zynqmp_aes_op *ctx)
{
	struct zynqmp_aes_dev *aes_dd = NULL;
	struct zynqmp_aes_dev *tmp;

	spin_lock_bh(&zynqmp_aes.lock);
	if (!ctx->dd) {
		list_for_each_entry(tmp, &zynqmp_aes.dev_list, list) {
			aes_dd = tmp;
			break;
		}
		ctx->dd = aes_dd;
	} else {
		aes_dd = ctx->dd;
	}
	spin_unlock_bh(&zynqmp_aes.lock);

	return aes_dd;
}

static int zynqmp_setkey_blk(struct crypto_tfm *tfm, const u8 *key,
			     unsigned int len)
{
	struct zynqmp_aes_op *op = crypto_tfm_ctx(tfm);

	op->keylen = len;
	memcpy(op->key, key, len);

	return 0;
}

static int zynqmp_setkeytype(struct crypto_tfm *tfm, const u8 *keytype,
			     unsigned int len)
{
	struct zynqmp_aes_op *op = crypto_tfm_ctx(tfm);

	op->keytype = (u32)(*keytype);

	return 0;
}

static int zynqmp_aes_decrypt(struct blkcipher_desc *desc,
			      struct scatterlist *dst,
			      struct scatterlist *src,
			      unsigned int nbytes)
{
	/* TODO: Move the common functionality to one API */
	struct zynqmp_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct zynqmp_aes_dev *dd = zynqmp_aes_find_dev(op);
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	dma_addr_t dma_addr, dma_addr_buf;
	struct zynqmp_aes_data *abuf;
	struct blkcipher_walk walk;
	int err, ret, keytype;
	size_t dma_size;
	char *kbuf;

	if (!eemi_ops || !eemi_ops->aes)
		return -ENOTSUPP;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	op->iv = walk.iv;
	op->src = walk.src.virt.addr;
	op->dst = walk.dst.virt.addr;
	keytype = op->keytype;

	if (keytype == ZYNQMP_AES_KUP_KEY)
		dma_size = nbytes + ZYNQMP_AES_KEY_SIZE + ZYNQMP_AES_IV_SIZE;
	else
		dma_size = nbytes + ZYNQMP_AES_IV_SIZE;

	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	abuf = dma_alloc_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
				  &dma_addr_buf, GFP_KERNEL);
	if (!abuf) {
		dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
		return -ENOMEM;
	}
	memcpy(kbuf, op->src, nbytes);
	memcpy(kbuf + nbytes, op->iv, ZYNQMP_AES_IV_SIZE);

	abuf->src = dma_addr;
	abuf->dst = dma_addr;
	abuf->iv = abuf->src + nbytes;
	abuf->size = nbytes - ZYNQMP_AES_GCM_SIZE;
	abuf->optype = ZYNQMP_AES_DECRYPT;
	abuf->keysrc = keytype;
	if (keytype == ZYNQMP_AES_KUP_KEY) {
		memcpy(kbuf + nbytes + ZYNQMP_AES_IV_SIZE,
		       op->key, ZYNQMP_AES_KEY_SIZE);
		abuf->key = abuf->iv + ZYNQMP_AES_IV_SIZE;
	} else {
		abuf->key = 0;
	}
	eemi_ops->aes(dma_addr_buf, &ret);
	if (ret != 0) {
		switch (ret) {
		case ZYNQMP_AES_GCM_TAG_MISMATCH_ERR:
			dev_err(dd->dev, "ERROR: Gcm Tag mismatch\n\r");
			break;
		case ZYNQMP_AES_SIZE_ERR:
			dev_err(dd->dev, "ERROR : Non word aligned data\n\r");
			break;
		case ZYNQMP_AES_WRONG_KEY_SRC_ERR:
			dev_err(dd->dev, "ERROR: Wrong KeySrc, enable secure mode\n\r");
			break;
		default:
			dev_err(dd->dev, "ERROR: Invalid");
			break;
		}
		goto END;
	}
	memcpy(walk.dst.virt.addr, kbuf, nbytes - ZYNQMP_AES_GCM_SIZE);
END:
	err = blkcipher_walk_done(desc, &walk, 0);
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
	dma_free_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
			  abuf, dma_addr_buf);
	return err;
}

static int zynqmp_aes_encrypt(struct blkcipher_desc *desc,
			      struct scatterlist *dst,
			      struct scatterlist *src,
			      unsigned int nbytes)
{
	struct zynqmp_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct zynqmp_aes_dev *dd = zynqmp_aes_find_dev(op);
	const struct zynqmp_eemi_ops *eemi_ops = zynqmp_pm_get_eemi_ops();
	dma_addr_t dma_addr, dma_addr_buf;
	struct zynqmp_aes_data *abuf;
	struct blkcipher_walk walk;
	unsigned int data_size;
	size_t dma_size;
	u32 keytype;
	char *kbuf;
	int err, ret;

	if (!eemi_ops || !eemi_ops->aes)
		return -ENOTSUPP;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	op->iv = walk.iv;
	op->src = walk.src.virt.addr;
	op->dst = walk.dst.virt.addr;
	keytype = op->keytype;

	if (keytype == ZYNQMP_AES_KUP_KEY)
		dma_size = nbytes + ZYNQMP_AES_KEY_SIZE
			+ ZYNQMP_AES_IV_SIZE;
	else
		dma_size = nbytes + ZYNQMP_AES_IV_SIZE;

	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	abuf = dma_alloc_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
				  &dma_addr_buf, GFP_KERNEL);
	if (!abuf) {
		dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
		return -ENOMEM;
	}
	data_size = nbytes - ZYNQMP_AES_GCM_SIZE;
	memcpy(kbuf, op->src, data_size);
	memcpy(kbuf + data_size, op->iv, ZYNQMP_AES_IV_SIZE);

	abuf->src = dma_addr;
	abuf->dst = dma_addr;
	abuf->iv = abuf->src + data_size;
	abuf->size = data_size;
	abuf->optype = ZYNQMP_AES_ENCRYPT;
	abuf->keysrc = keytype;

	if (keytype == ZYNQMP_AES_KUP_KEY) {
		memcpy(kbuf + data_size + ZYNQMP_AES_IV_SIZE,
		       op->key, ZYNQMP_AES_KEY_SIZE);

		abuf->key = abuf->src + data_size + ZYNQMP_AES_IV_SIZE;
	} else {
		abuf->key = 0;
	}
	eemi_ops->aes(dma_addr_buf, &ret);
	if (ret != 0) {
		switch (ret) {
		case ZYNQMP_AES_SIZE_ERR:
			dev_err(dd->dev, "ERROR : Non word aligned data\n\r");
			break;
		case ZYNQMP_AES_WRONG_KEY_SRC_ERR:
			dev_err(dd->dev, "ERROR: Wrong KeySrc, enable secure mode\n\r");
			break;
		default:
			dev_err(dd->dev, "ERROR: Invalid");
			break;
		}
		goto END;
	}
	memcpy(walk.dst.virt.addr, kbuf, nbytes);
END:
	err = blkcipher_walk_done(desc, &walk, 0);
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
	dma_free_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
			  abuf, dma_addr_buf);

	return err;
}

static struct crypto_alg zynqmp_alg = {
	.cra_name		=	"xilinx-zynqmp-aes",
	.cra_driver_name	=	"zynqmp-aes",
	.cra_priority		=	400,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
	.cra_blocksize		=	4,
	.cra_ctxsize		=	sizeof(struct zynqmp_aes_op),
	.cra_alignmask		=	15,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
	.blkcipher	=	{
			.min_keysize	=	0,
			.max_keysize	=	ZYNQMP_AES_KEY_SIZE,
			.setkey		=	zynqmp_setkey_blk,
			.setkeytype	=	zynqmp_setkeytype,
			.encrypt	=	zynqmp_aes_encrypt,
			.decrypt	=	zynqmp_aes_decrypt,
			.ivsize		=	ZYNQMP_AES_IV_SIZE,
		}
	}
};

static const struct of_device_id zynqmp_aes_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-aes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zynqmp_aes_dt_ids);

static int zynqmp_aes_probe(struct platform_device *pdev)
{
	struct zynqmp_aes_dev *aes_dd;
	struct device *dev = &pdev->dev;
	int ret;

	aes_dd = devm_kzalloc(dev, sizeof(*aes_dd), GFP_KERNEL);
	if (!aes_dd)
		return -ENOMEM;

	aes_dd->dev = dev;
	platform_set_drvdata(pdev, aes_dd);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(44));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	INIT_LIST_HEAD(&aes_dd->list);
	crypto_init_queue(&aes_dd->queue, ZYNQMP_AES_QUEUE_LENGTH);
	list_add_tail(&aes_dd->list, &zynqmp_aes.dev_list);

	ret = crypto_register_alg(&zynqmp_alg);
	if (ret)
		goto err_algs;

	dev_info(dev, "AES Successfully Registered\n\r");
	return 0;

err_algs:
	list_del(&aes_dd->list);
	dev_err(dev, "initialization failed.\n");

	return ret;
}

static int zynqmp_aes_remove(struct platform_device *pdev)
{
	struct zynqmp_aes_dev *aes_dd;

	aes_dd = platform_get_drvdata(pdev);
	if (!aes_dd)
		return -ENODEV;
	list_del(&aes_dd->list);
	crypto_unregister_alg(&zynqmp_alg);
	return 0;
}

static struct platform_driver xilinx_aes_driver = {
	.probe = zynqmp_aes_probe,
	.remove = zynqmp_aes_remove,
	.driver = {
		.name = "zynqmp_aes",
		.of_match_table = of_match_ptr(zynqmp_aes_dt_ids),
	},
};

module_platform_driver(xilinx_aes_driver);

MODULE_DESCRIPTION("Xilinx ZynqMP AES hw acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nava kishore Manne <nava.manne@xilinx.com>");
MODULE_AUTHOR("Kalyani Akula <kalyani.akula@xilinx.com>");
