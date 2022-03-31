// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Xilinx, Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <crypto/scatterwalk.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define ZYNQMP_RSA_QUEUE_LENGTH	1
#define ZYNQMP_RSA_MAX_KEY_SIZE	1024
#define ZYNQMP_RSA_BLOCKSIZE	64

/* Key size in bytes */
#define XSECURE_RSA_512_KEY_SIZE	(512U / 8U)
#define XSECURE_RSA_576_KEY_SIZE	(576U / 8U)
#define XSECURE_RSA_704_KEY_SIZE	(704U / 8U)
#define XSECURE_RSA_768_KEY_SIZE	(768U / 8U)
#define XSECURE_RSA_992_KEY_SIZE	(992U / 8U)
#define XSECURE_RSA_1024_KEY_SIZE	(1024U / 8U)
#define XSECURE_RSA_1152_KEY_SIZE	(1152U / 8U)
#define XSECURE_RSA_1408_KEY_SIZE	(1408U / 8U)
#define XSECURE_RSA_1536_KEY_SIZE	(1536U / 8U)
#define XSECURE_RSA_1984_KEY_SIZE	(1984U / 8U)
#define XSECURE_RSA_2048_KEY_SIZE	(2048U / 8U)
#define XSECURE_RSA_3072_KEY_SIZE	(3072U / 8U)
#define XSECURE_RSA_4096_KEY_SIZE	(4096U / 8U)

static struct zynqmp_rsa_dev *rsa_dd;

struct zynqmp_rsa_op {
	struct zynqmp_rsa_dev    *dd;
	void *src;
	void *dst;
	int len;
	u8 key[ZYNQMP_RSA_MAX_KEY_SIZE];
	u8 *iv;
	u32 keylen;
};

struct zynqmp_rsa_dev {
	struct list_head        list;
	struct device           *dev;
	/* the lock protects queue and dev list*/
	spinlock_t              lock;
	struct crypto_queue     queue;
};

struct zynqmp_rsa_drv {
	struct list_head        dev_list;
	/* the lock protects queue and dev list*/
	spinlock_t              lock;
};

static struct zynqmp_rsa_drv zynqmp_rsa = {
	.dev_list = LIST_HEAD_INIT(zynqmp_rsa.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(zynqmp_rsa.lock),
};

static struct zynqmp_rsa_dev *zynqmp_rsa_find_dev(struct zynqmp_rsa_op *ctx)
{
	struct zynqmp_rsa_dev *dd = rsa_dd;

	spin_lock_bh(&zynqmp_rsa.lock);
	if (!ctx->dd)
		ctx->dd = dd;
	else
		dd = ctx->dd;
	spin_unlock_bh(&zynqmp_rsa.lock);

	return dd;
}

static int zynqmp_setkey_blk(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int len)
{
	struct zynqmp_rsa_op *op = crypto_skcipher_ctx(tfm);

	op->keylen = len;
	memcpy(op->key, key, len);
	return 0;
}

static int zynqmp_rsa_xcrypt(struct skcipher_request *req, unsigned int flags)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct zynqmp_rsa_op *op = crypto_skcipher_ctx(tfm);
	struct zynqmp_rsa_dev *dd = zynqmp_rsa_find_dev(op);
	int err, datasize, src_data = 0, dst_data = 0;
	struct skcipher_walk walk = {0};
	unsigned int nbytes;
	char *kbuf;
	size_t dma_size;
	dma_addr_t dma_addr;

	nbytes = req->cryptlen;
	dma_size = nbytes + op->keylen;
	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		goto out;

	while ((datasize = walk.nbytes)) {
		op->src = walk.src.virt.addr;
		memcpy(kbuf + src_data, op->src, datasize);
		src_data = src_data + datasize;
		err = skcipher_walk_done(&walk, 0);
		if (err)
			goto out;
	}
	memcpy(kbuf + nbytes, op->key, op->keylen);

	if (nbytes != XSECURE_RSA_512_KEY_SIZE ||
	    nbytes != XSECURE_RSA_576_KEY_SIZE ||
	    nbytes != XSECURE_RSA_704_KEY_SIZE ||
	    nbytes != XSECURE_RSA_768_KEY_SIZE ||
	    nbytes != XSECURE_RSA_992_KEY_SIZE ||
	    nbytes != XSECURE_RSA_1024_KEY_SIZE ||
	    nbytes != XSECURE_RSA_1152_KEY_SIZE ||
	    nbytes != XSECURE_RSA_1408_KEY_SIZE ||
	    nbytes != XSECURE_RSA_1536_KEY_SIZE ||
	    nbytes != XSECURE_RSA_1984_KEY_SIZE ||
	    nbytes != XSECURE_RSA_2048_KEY_SIZE ||
	    nbytes != XSECURE_RSA_3072_KEY_SIZE ||
	    nbytes != XSECURE_RSA_4096_KEY_SIZE) {
		return -EOPNOTSUPP;
	}

	zynqmp_pm_rsa(dma_addr, nbytes, flags);

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		goto out;

	while ((datasize = walk.nbytes)) {
		memcpy(walk.dst.virt.addr, kbuf + dst_data, datasize);
		dst_data = dst_data + datasize;
		err = skcipher_walk_done(&walk, 0);
	}

out:
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
	return err;
}

static int zynqmp_rsa_decrypt(struct skcipher_request *req)
{
	return zynqmp_rsa_xcrypt(req, 0);
}

static int zynqmp_rsa_encrypt(struct skcipher_request *req)
{
	return zynqmp_rsa_xcrypt(req, 1);
}

static struct skcipher_alg zynqmp_alg = {
	.base.cra_name		=	"xilinx-zynqmp-rsa",
	.base.cra_driver_name	=	"zynqmp-rsa",
	.base.cra_priority	=	400,
	.base.cra_flags		=	CRYPTO_ALG_TYPE_SKCIPHER |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
	.base.cra_blocksize	=	ZYNQMP_RSA_BLOCKSIZE,
	.base.cra_ctxsize	=	sizeof(struct zynqmp_rsa_op),
	.base.cra_alignmask	=	15,
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	0,
	.max_keysize		=	ZYNQMP_RSA_MAX_KEY_SIZE,
	.setkey			=	zynqmp_setkey_blk,
	.encrypt		=	zynqmp_rsa_encrypt,
	.decrypt		=	zynqmp_rsa_decrypt,
	.ivsize			=	1,
};

static const struct of_device_id zynqmp_rsa_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-rsa" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, zynqmp_rsa_dt_ids);

static int zynqmp_rsa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	rsa_dd = devm_kzalloc(&pdev->dev, sizeof(*rsa_dd), GFP_KERNEL);
	if (!rsa_dd)
		return -ENOMEM;

	rsa_dd->dev = dev;
	platform_set_drvdata(pdev, rsa_dd);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret < 0)
		dev_err(dev, "no usable DMA configuration");

	INIT_LIST_HEAD(&rsa_dd->list);
	spin_lock_init(&rsa_dd->lock);
	crypto_init_queue(&rsa_dd->queue, ZYNQMP_RSA_QUEUE_LENGTH);
	spin_lock(&zynqmp_rsa.lock);
	list_add_tail(&rsa_dd->list, &zynqmp_rsa.dev_list);
	spin_unlock(&zynqmp_rsa.lock);

	ret = crypto_register_skcipher(&zynqmp_alg);
	if (ret)
		goto err_algs;

	return 0;

err_algs:
	spin_lock(&zynqmp_rsa.lock);
	list_del(&rsa_dd->list);
	spin_unlock(&zynqmp_rsa.lock);
	dev_err(dev, "initialization failed.\n");
	return ret;
}

static int zynqmp_rsa_remove(struct platform_device *pdev)
{
	crypto_unregister_skcipher(&zynqmp_alg);
	return 0;
}

static struct platform_driver xilinx_rsa_driver = {
	.probe = zynqmp_rsa_probe,
	.remove = zynqmp_rsa_remove,
	.driver = {
		.name = "zynqmp_rsa",
		.of_match_table = of_match_ptr(zynqmp_rsa_dt_ids),
	},
};

module_platform_driver(xilinx_rsa_driver);

MODULE_DESCRIPTION("ZynqMP RSA hw acceleration support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nava kishore Manne <navam@xilinx.com>");
