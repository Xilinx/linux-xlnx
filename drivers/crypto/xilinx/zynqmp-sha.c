// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx ZynqMP SHA Driver.
 * Copyright (c) 2022 Xilinx Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */
#include <linux/cacheflush.h>
#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/sha3.h>
#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define CONTINUE_PACKET		BIT(31)
#define FIRST_PACKET		BIT(30)
#define FINAL_PACKET		0
#define RESET			0

#define ZYNQMP_DMA_BIT_MASK		32U
#define VERSAL_DMA_BIT_MASK		64U
#define ZYNQMP_DMA_ALLOC_FIXED_SIZE	0x1000U

enum zynqmp_sha_op {
	ZYNQMP_SHA3_INIT = 1,
	ZYNQMP_SHA3_UPDATE = 2,
	ZYNQMP_SHA3_FINAL = 4,
};

struct xilinx_sha_drv_ctx {
	struct ahash_engine_alg sha3_384;
	struct crypto_engine *engine;
	struct device *dev;
	u8 dma_addr_size;
};

struct zynqmp_sha_tfm_ctx {
	struct device *dev;
	struct crypto_ahash *fbk_tfm;
};

struct zynqmp_sha_desc_ctx {
	struct ahash_request fallback_req;
};

static dma_addr_t update_dma_addr, final_dma_addr;
static char *ubuf, *fbuf;

static int zynqmp_sha_init_tfm(struct crypto_tfm *tfm)
{
	const char *fallback_driver_name = crypto_tfm_alg_name(tfm);
	struct zynqmp_sha_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);
	struct hash_alg_common *alg = crypto_hash_alg_common(__crypto_ahash_cast(tfm));
	struct crypto_ahash *fallback_tfm;
	struct xilinx_sha_drv_ctx *drv_ctx;

	drv_ctx = container_of(alg, struct xilinx_sha_drv_ctx, sha3_384.base.halg);
	tfm_ctx->dev = drv_ctx->dev;

	/* Allocate a fallback and abort if it failed. */
	fallback_tfm = crypto_alloc_ahash(fallback_driver_name, CRYPTO_ALG_TYPE_SHASH,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm))
		return PTR_ERR(fallback_tfm);

	tfm_ctx->fbk_tfm = fallback_tfm;
	crypto_ahash_set_statesize(__crypto_ahash_cast(tfm),
				   crypto_ahash_statesize(fallback_tfm));
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 crypto_ahash_reqsize(tfm_ctx->fbk_tfm) +
				 sizeof(struct zynqmp_sha_desc_ctx));

	return 0;
}

static void zynqmp_sha_exit_tfm(struct crypto_tfm *tfm)
{
	struct zynqmp_sha_tfm_ctx *tfm_ctx = crypto_tfm_ctx(tfm);

	if (tfm_ctx->fbk_tfm) {
		crypto_free_ahash(tfm_ctx->fbk_tfm);
		tfm_ctx->fbk_tfm = NULL;
	}

	memzero_explicit(tfm_ctx, sizeof(struct zynqmp_sha_tfm_ctx));
}

static int zynqmp_sha_init(struct ahash_request *req)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;
	return crypto_ahash_init(&dctx->fallback_req);
}

static int zynqmp_sha_update(struct ahash_request *req)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;
	dctx->fallback_req.nbytes = req->nbytes;
	dctx->fallback_req.src = req->src;
	return crypto_ahash_update(&dctx->fallback_req);
}

static int zynqmp_sha_final(struct ahash_request *req)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;
	dctx->fallback_req.result = req->result;

	return crypto_ahash_final(&dctx->fallback_req);
}

static int zynqmp_sha_finup(struct ahash_request *req)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;

	dctx->fallback_req.nbytes = req->nbytes;
	dctx->fallback_req.src = req->src;
	dctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&dctx->fallback_req);
}

static int zynqmp_sha_import(struct ahash_request *req, const void *in)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_import(&dctx->fallback_req, in);
}

static int zynqmp_sha_export(struct ahash_request *req, void *out)
{
	struct zynqmp_sha_desc_ctx *dctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct zynqmp_sha_tfm_ctx *tctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&dctx->fallback_req, tctx->fbk_tfm);
	dctx->fallback_req.base.flags = req->base.flags &
		CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_export(&dctx->fallback_req, out);
}

static int sha_digest(struct ahash_request *req)
{
	struct crypto_tfm *tfm = crypto_ahash_tfm(crypto_ahash_reqtfm(req));
	struct hash_alg_common *alg = crypto_hash_alg_common(__crypto_ahash_cast(tfm));
	struct xilinx_sha_drv_ctx *drv_ctx;

	drv_ctx = container_of(alg, struct xilinx_sha_drv_ctx, sha3_384.base.halg);

	return crypto_transfer_hash_request_to_engine(drv_ctx->engine, req);
}

static int zynqmp_sha_digest(struct ahash_request *req)
{
	unsigned int processed = 0;
	unsigned int remaining_len;
	int update_size;
	int ret;

	remaining_len = req->nbytes;
	ret = zynqmp_pm_sha_hash(0, 0, ZYNQMP_SHA3_INIT);
	if (ret)
		return ret;

	while (remaining_len) {
		if (remaining_len >= ZYNQMP_DMA_ALLOC_FIXED_SIZE)
			update_size = ZYNQMP_DMA_ALLOC_FIXED_SIZE;
		else
			update_size = remaining_len;
		sg_pcopy_to_buffer(req->src, sg_nents(req->src), ubuf, update_size, processed);
		flush_icache_range((unsigned long)ubuf, (unsigned long)ubuf + update_size);
		ret = zynqmp_pm_sha_hash(update_dma_addr, update_size, ZYNQMP_SHA3_UPDATE);
		if (ret)
			return ret;

		remaining_len -= update_size;
		processed += update_size;
	}

	ret = zynqmp_pm_sha_hash(final_dma_addr, SHA3_384_DIGEST_SIZE, ZYNQMP_SHA3_FINAL);
	memcpy(req->result, fbuf, SHA3_384_DIGEST_SIZE);
	memzero_explicit(fbuf, SHA3_384_DIGEST_SIZE);

	return ret;
}

static int versal_sha_digest(struct ahash_request *req)
{
	int update_size, ret, flag = FIRST_PACKET;
	unsigned int processed = 0;
	unsigned int remaining_len;

	remaining_len = req->nbytes;
	while (remaining_len) {
		if (remaining_len >= ZYNQMP_DMA_ALLOC_FIXED_SIZE)
			update_size = ZYNQMP_DMA_ALLOC_FIXED_SIZE;
		else
			update_size = remaining_len;

		sg_pcopy_to_buffer(req->src, sg_nents(req->src), ubuf, update_size, processed);
		flush_icache_range((unsigned long)ubuf,
				   (unsigned long)ubuf + update_size);

		flag |= CONTINUE_PACKET;
		ret = versal_pm_sha_hash(update_dma_addr, 0,
					 update_size | flag);
		if (ret)
			return ret;

		remaining_len -= update_size;
		processed += update_size;
		flag = RESET;
	}

	flag |= FINAL_PACKET;
	ret = versal_pm_sha_hash(0, final_dma_addr, flag);
	if (ret)
		return ret;

	memcpy(req->result, fbuf, SHA3_384_DIGEST_SIZE);
	memzero_explicit(fbuf, SHA3_384_DIGEST_SIZE);

	return 0;
}

static int handle_zynqmp_sha_engine_req(struct crypto_engine *engine, void *req)
{
	int err;

	err = zynqmp_sha_digest(req);
	local_bh_disable();
	crypto_finalize_hash_request(engine, req, err);
	local_bh_enable();

	return 0;
}

static int handle_versal_sha_engine_req(struct crypto_engine *engine, void *req)
{
	int err;

	err = versal_sha_digest(req);
	local_bh_disable();
	crypto_finalize_hash_request(engine, req, err);
	local_bh_enable();

	return 0;
}

static struct xilinx_sha_drv_ctx zynqmp_sha3_drv_ctx = {
	.sha3_384.base = {
		.init = zynqmp_sha_init,
		.update = zynqmp_sha_update,
		.final = zynqmp_sha_final,
		.finup = zynqmp_sha_finup,
		.digest = sha_digest,
		.export = zynqmp_sha_export,
		.import = zynqmp_sha_import,
		.halg = {
			.digestsize = SHA3_384_DIGEST_SIZE,
			.statesize = sizeof(struct sha3_state),
			.base.cra_init = zynqmp_sha_init_tfm,
			.base.cra_exit = zynqmp_sha_exit_tfm,
			.base.cra_name = "sha3-384",
			.base.cra_driver_name = "zynqmp-sha3-384",
			.base.cra_priority = 300,
			.base.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY |
				CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize = SHA3_384_BLOCK_SIZE,
			.base.cra_ctxsize = sizeof(struct zynqmp_sha_tfm_ctx),
			.base.cra_module = THIS_MODULE,
		}
	},
	.sha3_384.op = {
		.do_one_request = handle_zynqmp_sha_engine_req,
	},
	.dma_addr_size = ZYNQMP_DMA_BIT_MASK,
};

static struct xilinx_sha_drv_ctx versal_sha3_drv_ctx = {
	.sha3_384.base = {
		.init = zynqmp_sha_init,
		.update = zynqmp_sha_update,
		.final = zynqmp_sha_final,
		.finup = zynqmp_sha_finup,
		.digest = sha_digest,
		.export = zynqmp_sha_export,
		.import = zynqmp_sha_import,
		.halg = {
			.base.cra_init = zynqmp_sha_init_tfm,
			.base.cra_exit = zynqmp_sha_exit_tfm,
			.base.cra_name = "sha3-384",
			.base.cra_driver_name = "versal-sha3-384",
			.base.cra_priority = 300,
			.base.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY |
				CRYPTO_ALG_ALLOCATES_MEMORY |
				CRYPTO_ALG_NEED_FALLBACK,
			.base.cra_blocksize = SHA3_384_BLOCK_SIZE,
			.base.cra_ctxsize = sizeof(struct zynqmp_sha_tfm_ctx),
			.base.cra_module = THIS_MODULE,
			.statesize = sizeof(struct sha3_state),
			.digestsize = SHA3_384_DIGEST_SIZE,
		}
	},
	.sha3_384.op = {
		.do_one_request = handle_versal_sha_engine_req,
	},
	.dma_addr_size = VERSAL_DMA_BIT_MASK,
};

static struct xlnx_feature sha_feature_map[] = {
	{
		.family = ZYNQMP_FAMILY_CODE,
		.subfamily = ALL_SUB_FAMILY_CODE,
		.feature_id = PM_SECURE_SHA,
		.data = &zynqmp_sha3_drv_ctx,
	},
	{
		.family = VERSAL_FAMILY_CODE,
		.subfamily = VERSAL_SUB_FAMILY_CODE,
		.feature_id = XSECURE_API_SHA3_UPDATE,
		.data = &versal_sha3_drv_ctx,
	},
	{ /* sentinel */ }
};

static int zynqmp_sha_probe(struct platform_device *pdev)
{
	struct xilinx_sha_drv_ctx *sha3_drv_ctx;
	struct device *dev = &pdev->dev;
	int err;

	/* Verify the hardware is present */
	sha3_drv_ctx = xlnx_get_crypto_dev_data(sha_feature_map);
	if (IS_ERR(sha3_drv_ctx)) {
		dev_err(dev, "SHA is not supported on the platform\n");
		return PTR_ERR(sha3_drv_ctx);
	}

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(sha3_drv_ctx->dma_addr_size));
	if (err < 0) {
		dev_err(dev, "No usable DMA configuration\n");
		return err;
	}

	sha3_drv_ctx->dev = dev;
	platform_set_drvdata(pdev, sha3_drv_ctx);

	ubuf = dma_alloc_coherent(dev, ZYNQMP_DMA_ALLOC_FIXED_SIZE, &update_dma_addr, GFP_KERNEL);
	if (!ubuf) {
		err = -ENOMEM;
		return err;
	}

	fbuf = dma_alloc_coherent(dev, SHA3_384_DIGEST_SIZE, &final_dma_addr, GFP_KERNEL);
	if (!fbuf) {
		err = -ENOMEM;
		goto err_mem;
	}

	sha3_drv_ctx->engine = crypto_engine_alloc_init(dev, 1);
	if (!sha3_drv_ctx->engine) {
		dev_err(dev, "Cannot alloc Crypto engine\n");
		err = -ENOMEM;
		goto err_engine;
	}

	err = crypto_engine_start(sha3_drv_ctx->engine);
	if (err) {
		dev_err(dev, "Cannot start AES engine\n");
		goto err_start;
	}

	err = crypto_engine_register_ahash(&sha3_drv_ctx->sha3_384);
	if (err < 0) {
		dev_err(dev, "Failed to register sha3 alg.\n");
		goto err_start;
	}

	return 0;

err_start:
	crypto_engine_exit(sha3_drv_ctx->engine);
err_engine:
	dma_free_coherent(dev, SHA3_384_DIGEST_SIZE, fbuf, final_dma_addr);

err_mem:
	dma_free_coherent(dev, ZYNQMP_DMA_ALLOC_FIXED_SIZE, ubuf, update_dma_addr);

	return err;
}

static void zynqmp_sha_remove(struct platform_device *pdev)
{
	struct xilinx_sha_drv_ctx *sha3_drv_ctx;

	sha3_drv_ctx = platform_get_drvdata(pdev);
	crypto_engine_unregister_ahash(&sha3_drv_ctx->sha3_384);
	crypto_engine_exit(sha3_drv_ctx->engine);
	dma_free_coherent(sha3_drv_ctx->dev, ZYNQMP_DMA_ALLOC_FIXED_SIZE, ubuf, update_dma_addr);
	dma_free_coherent(sha3_drv_ctx->dev, SHA3_384_DIGEST_SIZE, fbuf, final_dma_addr);
}

static struct platform_driver zynqmp_sha_driver = {
	.probe = zynqmp_sha_probe,
	.remove_new = zynqmp_sha_remove,
	.driver = {
		.name = "zynqmp-sha3-384",
	},
};

static struct platform_device *platform_dev;

static int __init sha_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&zynqmp_sha_driver);
	if (ret)
		return ret;

	platform_dev = platform_device_register_simple(zynqmp_sha_driver.driver.name,
						       0, NULL, 0);
	if (IS_ERR(platform_dev)) {
		ret = PTR_ERR(platform_dev);
		platform_driver_unregister(&zynqmp_sha_driver);
	}

	return ret;
}

static void __exit sha_driver_exit(void)
{
	platform_device_unregister(platform_dev);
	platform_driver_unregister(&zynqmp_sha_driver);
}

module_init(sha_driver_init);
module_exit(sha_driver_exit);

MODULE_DESCRIPTION("ZynqMP SHA3 hardware acceleration support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Harsha <harsha.harsha@xilinx.com>");
