// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP AES Driver.
 * Copyright (c) 2018 Xilinx Inc.
 */

#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/firmware/xlnx-zynqmp.h>

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
#define ZYNQMP_AES_PUF_NOT_PROGRAMMED		0xE300

#define ZYNQMP_AES_BLOCKSIZE			0x04

#define ZYNQMP_KEY_SRC_SEL_KEY_LEN		1U

struct zynqmp_aes_dev *aes_dd;

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

struct zynqmp_aes_dev {
	struct device	*dev;
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

static struct zynqmp_aes_dev *zynqmp_aes_find_dev(struct zynqmp_aes_op *ctx)
{
	struct zynqmp_aes_dev *dd = aes_dd;

	if (!ctx->dd)
		ctx->dd = dd;
	else
		dd = ctx->dd;

	return dd;
}

static int zynqmp_setkey_blk(struct crypto_skcipher *tfm, const u8 *key,
			     unsigned int len)
{
	struct zynqmp_aes_op *op = crypto_skcipher_ctx(tfm);

	if (len == ZYNQMP_KEY_SRC_SEL_KEY_LEN) {
		op->keytype = *key;
	} else {
		op->keylen = len;
		if (len == ZYNQMP_AES_KEY_SIZE) {
			op->keytype = ZYNQMP_AES_KUP_KEY;
			memcpy(op->key, key, len);
		}
	}

	return 0;
}

static int zynqmp_aes_xcrypt(struct skcipher_request *req, unsigned int flags)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct zynqmp_aes_op *op = crypto_skcipher_ctx(tfm);
	struct zynqmp_aes_dev *dd = zynqmp_aes_find_dev(op);
	int err, ret, src_data = 0, dst_data = 0;
	dma_addr_t dma_addr, dma_addr_buf;
	struct zynqmp_aes_data *abuf;
	struct skcipher_walk walk = {0};
	unsigned int data_size;
	size_t dma_size;
	char *kbuf;

	if (op->keytype == ZYNQMP_AES_KUP_KEY)
		dma_size = req->cryptlen + ZYNQMP_AES_IV_SIZE + ZYNQMP_AES_KEY_SIZE;
	else
		dma_size = req->cryptlen + ZYNQMP_AES_IV_SIZE;

	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	abuf = dma_alloc_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
				  &dma_addr_buf, GFP_KERNEL);
	if (!abuf) {
		dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
		return -ENOMEM;
	}

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		goto END;
	op->iv = walk.iv;

	while ((data_size = walk.nbytes)) {
		op->src = walk.src.virt.addr;
		memcpy(kbuf + src_data, op->src, data_size);
		src_data = src_data + data_size;
		err = skcipher_walk_done(&walk, 0);
		if (err)
			goto END;
	}
	memcpy(kbuf + req->cryptlen, op->iv, ZYNQMP_AES_IV_SIZE);
	abuf->src = dma_addr;
	abuf->dst = dma_addr;
	abuf->iv = abuf->src + req->cryptlen;
	abuf->size = req->cryptlen - ZYNQMP_AES_GCM_SIZE;
	abuf->optype = flags;
	abuf->keysrc = op->keytype;

	if (op->keytype == ZYNQMP_AES_KUP_KEY) {
		memcpy(kbuf + req->cryptlen + ZYNQMP_AES_IV_SIZE,
		       op->key, ZYNQMP_AES_KEY_SIZE);

		abuf->key = abuf->src + req->cryptlen + ZYNQMP_AES_IV_SIZE;
	} else {
		abuf->key = 0;
	}

	zynqmp_pm_aes_engine(dma_addr_buf, (u32 *)&ret);

	if (ret != 0) {
		switch (ret) {
		case ZYNQMP_AES_GCM_TAG_MISMATCH_ERR:
			dev_err(dd->dev, "ERROR: Gcm Tag mismatch\n");
			break;
		case ZYNQMP_AES_SIZE_ERR:
			dev_err(dd->dev, "ERROR : Non word aligned data\n");
			break;
		case ZYNQMP_AES_WRONG_KEY_SRC_ERR:
			dev_err(dd->dev, "ERROR: Wrong KeySrc, enable secure mode\n");
			break;
		case ZYNQMP_AES_PUF_NOT_PROGRAMMED:
			dev_err(dd->dev, "ERROR: PUF is not registered\n");
			break;
		default:
			dev_err(dd->dev, "ERROR: Invalid\n");
			break;
		}
		goto END;
	}
	if (!flags)
		req->cryptlen = req->cryptlen - ZYNQMP_AES_GCM_SIZE;

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		goto END;

	while ((data_size = walk.nbytes)) {
		memcpy(walk.dst.virt.addr, kbuf + dst_data, data_size);
		dst_data = dst_data + data_size;
		err = skcipher_walk_done(&walk, 0);
		if (err)
			goto END;
	}
END:
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);
	dma_free_coherent(dd->dev, sizeof(struct zynqmp_aes_data),
			  abuf, dma_addr_buf);
	return err;
}

static int zynqmp_aes_decrypt(struct skcipher_request *req)
{
	return zynqmp_aes_xcrypt(req, ZYNQMP_AES_DECRYPT);
}

static int zynqmp_aes_encrypt(struct skcipher_request *req)
{
	return zynqmp_aes_xcrypt(req, ZYNQMP_AES_ENCRYPT);
}

static struct skcipher_alg zynqmp_alg = {
	.base.cra_name		=	"xilinx-zynqmp-aes",
	.base.cra_driver_name	=	"zynqmp-aes",
	.base.cra_priority	=	400,
	.base.cra_flags		=	CRYPTO_ALG_TYPE_SKCIPHER |
					CRYPTO_ALG_KERN_DRIVER_ONLY,
	.base.cra_blocksize	=	ZYNQMP_AES_BLOCKSIZE,
	.base.cra_ctxsize	=	sizeof(struct zynqmp_aes_op),
	.base.cra_alignmask	=	15,
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	0,
	.max_keysize		=	ZYNQMP_AES_KEY_SIZE,
	.setkey			=	zynqmp_setkey_blk,
	.encrypt		=	zynqmp_aes_encrypt,
	.decrypt		=	zynqmp_aes_decrypt,
	.ivsize			=	ZYNQMP_AES_IV_SIZE,
};

static const struct of_device_id zynqmp_aes_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-aes" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zynqmp_aes_dt_ids);

static int zynqmp_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	aes_dd = devm_kzalloc(dev, sizeof(*aes_dd), GFP_KERNEL);
	if (!aes_dd)
		return -ENOMEM;

	aes_dd->dev = dev;
	platform_set_drvdata(pdev, aes_dd);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	ret = crypto_register_skcipher(&zynqmp_alg);
	if (ret)
		goto err_algs;

	dev_info(dev, "AES Successfully Registered\n");
	return 0;

err_algs:
	dev_err(dev, "initialization failed.\n");

	return ret;
}

static int zynqmp_aes_remove(struct platform_device *pdev)
{
	aes_dd = platform_get_drvdata(pdev);
	if (!aes_dd)
		return -ENODEV;
	crypto_unregister_skcipher(&zynqmp_alg);
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
