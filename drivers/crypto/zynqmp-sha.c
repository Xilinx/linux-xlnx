// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Xilinx, Inc.
 */

#include <asm/cacheflush.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/of_device.h>
#include <linux/crypto.h>
#include <linux/cryptohash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <linux/firmware/xlnx-zynqmp.h>

#define ZYNQMP_SHA3_INIT	1
#define ZYNQMP_SHA3_UPDATE	2
#define ZYNQMP_SHA3_FINAL	4

#define ZYNQMP_SHA_QUEUE_LENGTH	1

static const struct zynqmp_eemi_ops *eemi_ops;
struct zynqmp_sha_dev;

/*
 * .statesize = sizeof(struct zynqmp_sha_reqctx) must be <= PAGE_SIZE / 8 as
 * tested by the ahash_prepare_alg() function.
 */
struct zynqmp_sha_reqctx {
	struct zynqmp_sha_dev	*dd;
	unsigned long		flags;
};

struct zynqmp_sha_ctx {
	struct zynqmp_sha_dev	*dd;
	unsigned long		flags;
};

struct zynqmp_sha_dev {
	struct list_head	list;
	struct device		*dev;
	/* the lock protects queue and dev list*/
	spinlock_t		lock;
	int			err;

	unsigned long		flags;
	struct crypto_queue	queue;
	struct ahash_request	*req;
};

struct zynqmp_sha_drv {
	struct list_head	dev_list;
	/* the lock protects queue and dev list*/
	spinlock_t		lock;
};

static struct zynqmp_sha_drv zynqmp_sha = {
	.dev_list = LIST_HEAD_INIT(zynqmp_sha.dev_list),
	.lock = __SPIN_LOCK_UNLOCKED(zynqmp_sha.lock),
};

static int zynqmp_sha_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_ctx *tctx = crypto_ahash_ctx(tfm);
	struct zynqmp_sha_reqctx *ctx = ahash_request_ctx(req);
	struct zynqmp_sha_dev *dd = NULL;
	struct zynqmp_sha_dev *tmp;
	int ret;

	if (!eemi_ops->sha_hash)
		return -ENOTSUPP;

	spin_lock_bh(&zynqmp_sha.lock);
	if (!tctx->dd) {
		list_for_each_entry(tmp, &zynqmp_sha.dev_list, list) {
			dd = tmp;
			break;
		}
		tctx->dd = dd;
	} else {
		dd = tctx->dd;
	}
	spin_unlock_bh(&zynqmp_sha.lock);

	ctx->dd = dd;
	dev_dbg(dd->dev, "init: digest size: %d\n",
		crypto_ahash_digestsize(tfm));

	ret = eemi_ops->sha_hash(0, 0, ZYNQMP_SHA3_INIT);

	return ret;
}

static int zynqmp_sha_update(struct ahash_request *req)
{
	struct zynqmp_sha_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct zynqmp_sha_dev *dd = tctx->dd;
	char *kbuf;
	size_t dma_size = req->nbytes;
	dma_addr_t dma_addr;
	int ret;

	if (!req->nbytes)
		return 0;

	if (!eemi_ops->sha_hash)
		return -ENOTSUPP;

	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	scatterwalk_map_and_copy(kbuf, req->src, 0, req->nbytes, 0);
	 __flush_cache_user_range((unsigned long)kbuf,
				  (unsigned long)kbuf + dma_size);
	ret = eemi_ops->sha_hash(dma_addr, req->nbytes, ZYNQMP_SHA3_UPDATE);
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);

	return ret;
}

static int zynqmp_sha_final(struct ahash_request *req)
{
	struct zynqmp_sha_ctx *tctx = crypto_tfm_ctx(req->base.tfm);
	struct zynqmp_sha_dev *dd = tctx->dd;
	char *kbuf;
	size_t dma_size = SHA384_DIGEST_SIZE;
	dma_addr_t dma_addr;
	int ret;

	if (!eemi_ops->sha_hash)
		return -ENOTSUPP;

	kbuf = dma_alloc_coherent(dd->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = eemi_ops->sha_hash(dma_addr, dma_size, ZYNQMP_SHA3_FINAL);
	memcpy(req->result, kbuf, 48);
	dma_free_coherent(dd->dev, dma_size, kbuf, dma_addr);

	return ret;
}

static int zynqmp_sha_finup(struct ahash_request *req)
{
	zynqmp_sha_update(req);
	zynqmp_sha_final(req);

	return 0;
}

static int zynqmp_sha_digest(struct ahash_request *req)
{
	zynqmp_sha_init(req);
	zynqmp_sha_update(req);
	zynqmp_sha_final(req);

	return 0;
}

static int zynqmp_sha_export(struct ahash_request *req, void *out)
{
	const struct zynqmp_sha_reqctx *ctx = ahash_request_ctx(req);

	memcpy(out, ctx, sizeof(*ctx));
	return 0;
}

static int zynqmp_sha_import(struct ahash_request *req, const void *in)
{
	struct zynqmp_sha_reqctx *ctx = ahash_request_ctx(req);

	memcpy(ctx, in, sizeof(*ctx));
	return 0;
}

static int zynqmp_sha_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct zynqmp_sha_reqctx));

	return 0;
}

static struct ahash_alg sha3_alg = {
	.init		= zynqmp_sha_init,
	.update		= zynqmp_sha_update,
	.final		= zynqmp_sha_final,
	.finup		= zynqmp_sha_finup,
	.digest		= zynqmp_sha_digest,
	.export		= zynqmp_sha_export,
	.import		= zynqmp_sha_import,
	.halg = {
		.digestsize	= SHA384_DIGEST_SIZE,
		.statesize	= sizeof(struct sha256_state),
		.base	= {
			.cra_name		= "xilinx-keccak-384",
			.cra_driver_name	= "zynqmp-keccak-384",
			.cra_priority		= 300,
			.cra_flags		= CRYPTO_ALG_ASYNC,
			.cra_blocksize		= SHA384_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct zynqmp_sha_ctx),
			.cra_alignmask		= 0,
			.cra_module		= THIS_MODULE,
			.cra_init		= zynqmp_sha_cra_init,
		}
	}
};

static const struct of_device_id zynqmp_sha_dt_ids[] = {
	{ .compatible = "xlnx,zynqmp-keccak-384" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, zynqmp_sha_dt_ids);

static int zynqmp_sha_probe(struct platform_device *pdev)
{
	struct zynqmp_sha_dev *sha_dd;
	struct device *dev = &pdev->dev;
	int err;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	sha_dd = devm_kzalloc(&pdev->dev, sizeof(*sha_dd), GFP_KERNEL);
	if (!sha_dd)
		return -ENOMEM;

	sha_dd->dev = dev;
	platform_set_drvdata(pdev, sha_dd);
	INIT_LIST_HEAD(&sha_dd->list);
	spin_lock_init(&sha_dd->lock);
	crypto_init_queue(&sha_dd->queue, ZYNQMP_SHA_QUEUE_LENGTH);
	spin_lock(&zynqmp_sha.lock);
	list_add_tail(&sha_dd->list, &zynqmp_sha.dev_list);
	spin_unlock(&zynqmp_sha.lock);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(44));
	if (err < 0)
		dev_err(dev, "no usable DMA configuration");

	err = crypto_register_ahash(&sha3_alg);
	if (err)
		goto err_algs;

	return 0;

err_algs:
	spin_lock(&zynqmp_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&zynqmp_sha.lock);
	dev_err(dev, "initialization failed.\n");

	return err;
}

static int zynqmp_sha_remove(struct platform_device *pdev)
{
	static struct zynqmp_sha_dev *sha_dd;

	sha_dd = platform_get_drvdata(pdev);

	if (!sha_dd)
		return -ENODEV;

	spin_lock(&zynqmp_sha.lock);
	list_del(&sha_dd->list);
	spin_unlock(&zynqmp_sha.lock);

	crypto_unregister_ahash(&sha3_alg);

	return 0;
}

static struct platform_driver zynqmp_sha_driver = {
	.probe		= zynqmp_sha_probe,
	.remove		= zynqmp_sha_remove,
	.driver		= {
		.name	= "zynqmp-keccak-384",
		.of_match_table	= of_match_ptr(zynqmp_sha_dt_ids),
	},
};

module_platform_driver(zynqmp_sha_driver);

MODULE_DESCRIPTION("ZynqMP SHA3 hw acceleration support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nava kishore Manne <navam@xilinx.com>");
