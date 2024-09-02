// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 - 2024, Advanced Micro Devices, Inc.
 */

#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <crypto/engine.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>
#include <crypto/scatterwalk.h>

#define XILINX_DMA_BIT_MASK	32U
#define XILINX_RSA_MAX_KEY_SIZE	1024
#define XILINX_RSA_BLOCKSIZE	64

/* Key size in bytes */
#define XSECURE_RSA_2048_KEY_SIZE	(2048U / 8U)
#define XSECURE_RSA_3072_KEY_SIZE	(3072U / 8U)
#define XSECURE_RSA_4096_KEY_SIZE	(4096U / 8U)

enum xilinx_akcipher_op {
	XILINX_RSA_DECRYPT = 0,
	XILINX_RSA_ENCRYPT,
	XILINX_RSA_SIGN,
	XILINX_RSA_VERIFY
};

struct versal_rsa_in_param {
	u64 key_addr;
	u64 data_addr;
	u32 size;
};

struct xilinx_rsa_drv_ctx {
	struct akcipher_engine_alg alg;
	struct device *dev;
	struct crypto_engine *engine;
	int (*xilinx_rsa_xcrypt)(struct akcipher_request *req);
};

struct xilinx_rsa_tfm_ctx {
	struct device *dev;
	struct crypto_akcipher *fbk_cipher;
	u8 *e_buf;
	u8 *n_buf;
	u8 *d_buf;
	unsigned int key_len; /* in bits */
	unsigned int e_len;
	unsigned int n_len;
	unsigned int d_len;
};

struct xilinx_rsa_req_ctx {
	enum xilinx_akcipher_op op;
};

static int zynqmp_rsa_xcrypt(struct akcipher_request *req)
{
	struct xilinx_rsa_req_ctx *rq_ctx = akcipher_request_ctx(req);
	unsigned int len, offset, diff = req->dst_len - req->src_len;
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct xilinx_rsa_tfm_ctx *tctx = akcipher_tfm_ctx(tfm);
	dma_addr_t dma_addr;
	char *kbuf;
	const char *buf;
	size_t dma_size;
	u8 padding = 0;
	int ret;

	if (rq_ctx->op == XILINX_RSA_ENCRYPT) {
		padding = tctx->e_len % 2;
		buf = tctx->e_buf;
		len = tctx->e_len;
	} else {
		buf = tctx->d_buf;
		len = tctx->d_len;
	}

	dma_size = req->dst_len + tctx->n_len + len + padding;
	offset = dma_size - len;

	kbuf = dma_alloc_coherent(tctx->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	scatterwalk_map_and_copy(kbuf + diff, req->src, 0, req->src_len, 0);
	memcpy(kbuf + req->dst_len, tctx->n_buf, tctx->n_len);

	memcpy(kbuf + offset, buf, len);

	ret = zynqmp_pm_rsa(dma_addr, tctx->n_len, rq_ctx->op);
	if (ret == 0) {
		sg_copy_from_buffer(req->dst, sg_nents(req->dst), kbuf,
				    req->dst_len);
	}

	dma_free_coherent(tctx->dev, dma_size, kbuf, dma_addr);

	return ret;
}

static int versal_rsa_xcrypt(struct akcipher_request *req)
{
	const struct xilinx_rsa_req_ctx *rq_ctx = akcipher_request_ctx(req);
	unsigned int len, offset, diff = req->dst_len - req->src_len;
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct xilinx_rsa_tfm_ctx *tctx = akcipher_tfm_ctx(tfm);
	struct versal_rsa_in_param *para;
	dma_addr_t dma_addr, dma_addr1;
	char *kbuf;
	const char *buf;
	size_t dma_size;
	u8 padding = 0;
	int ret = 0;

	para = dma_alloc_coherent(tctx->dev,
				  sizeof(struct versal_rsa_in_param),
				  &dma_addr1, GFP_KERNEL);
	if (!para)
		return -ENOMEM;

	if (rq_ctx->op == XILINX_RSA_ENCRYPT) {
		padding = tctx->e_len % 2;
		buf = tctx->e_buf;
		len = tctx->e_len;
	} else {
		buf = tctx->d_buf;
		len = tctx->d_len;
	}

	dma_size = req->dst_len + tctx->n_len + len + padding;
	offset = dma_size - len;

	kbuf = dma_alloc_coherent(tctx->dev, dma_size, &dma_addr, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto kbuf_fail;
	}

	scatterwalk_map_and_copy(kbuf + diff, req->src, 0, req->src_len, 0);

	memcpy(kbuf + req->dst_len, tctx->n_buf, tctx->n_len);

	memcpy(kbuf + offset, buf, len);

	para->key_addr = (u64)(dma_addr + req->dst_len);
	para->data_addr = (u64)dma_addr;
	para->size = req->dst_len;

	if (rq_ctx->op == XILINX_RSA_ENCRYPT)
		ret = versal_pm_rsa_encrypt(dma_addr1, dma_addr);
	else
		ret = versal_pm_rsa_decrypt(dma_addr1, dma_addr);

	if (ret == 0) {
		sg_copy_from_buffer(req->dst, sg_nents(req->dst), kbuf,
				    req->dst_len);
	}
	dma_free_coherent(tctx->dev, dma_size, kbuf, dma_addr);

kbuf_fail:
	dma_free_coherent(tctx->dev, sizeof(struct versal_rsa_in_param),
			  para, dma_addr1);

	return ret;
}

static int xilinx_rsa_decrypt(struct akcipher_request *req)
{
	struct xilinx_rsa_req_ctx *rctx = akcipher_request_ctx(req);
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	struct xilinx_rsa_drv_ctx *drv_ctx;

	rctx->op = XILINX_RSA_DECRYPT;
	drv_ctx = container_of(alg, struct xilinx_rsa_drv_ctx, alg.base);

	return crypto_transfer_akcipher_request_to_engine(drv_ctx->engine, req);
}

static int xilinx_rsa_encrypt(struct akcipher_request *req)
{
	struct xilinx_rsa_req_ctx *rctx = akcipher_request_ctx(req);
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *alg = crypto_akcipher_alg(tfm);
	struct xilinx_rsa_drv_ctx *drv_ctx;

	rctx->op = XILINX_RSA_ENCRYPT;
	drv_ctx = container_of(alg, struct xilinx_rsa_drv_ctx, alg.base);

	return crypto_transfer_akcipher_request_to_engine(drv_ctx->engine, req);
}

static unsigned int xilinx_rsa_max_size(struct crypto_akcipher *tfm)
{
	const struct xilinx_rsa_tfm_ctx *tctx = akcipher_tfm_ctx(tfm);

	return tctx->n_len;
}

static inline int xilinx_copy_and_save_keypart(u8 **kpbuf, unsigned int *kplen,
					       const u8 *buf, size_t sz)
{
	int nskip;

	for (nskip = 0; nskip < sz; nskip++)
		if (buf[nskip])
			break;

	*kplen = sz - nskip;
	*kpbuf = kmemdup(buf + nskip, *kplen, GFP_KERNEL);
	if (!*kpbuf)
		return -ENOMEM;

	return 0;
}

static int xilinx_check_key_length(unsigned int len)
{
	if (len < 8 || len > 4096)
		return -EINVAL;
	return 0;
}

static void xilinx_rsa_free_key_bufs(struct xilinx_rsa_tfm_ctx *ctx)
{
	/* Clean up old key data */
	kfree_sensitive(ctx->e_buf);
	ctx->e_buf = NULL;
	ctx->e_len = 0;
	kfree_sensitive(ctx->n_buf);
	ctx->n_buf = NULL;
	ctx->n_len = 0;
	kfree_sensitive(ctx->d_buf);
	ctx->d_buf = NULL;
	ctx->d_len = 0;
}

static int xilinx_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen, bool private)
{
	struct xilinx_rsa_tfm_ctx *tctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key;
	int ret;

	if (private)
		ret = rsa_parse_priv_key(&raw_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&raw_key, key, keylen);
	if (ret)
		return ret;

	ret = xilinx_copy_and_save_keypart(&tctx->n_buf, &tctx->n_len,
					   raw_key.n, raw_key.n_sz);
	if (ret)
		return ret;

	/* convert to bits */
	tctx->key_len = tctx->n_len << 3;
	if (xilinx_check_key_length(tctx->key_len)) {
		ret = -EINVAL;
		goto key_err;
	}

	ret = xilinx_copy_and_save_keypart(&tctx->e_buf, &tctx->e_len,
					   raw_key.e, raw_key.e_sz);
	if (ret)
		goto key_err;

	if (private) {
		ret = xilinx_copy_and_save_keypart(&tctx->d_buf, &tctx->d_len,
						   raw_key.d, raw_key.d_sz);
		if (ret)
			goto key_err;
	}

	return 0;

key_err:
	xilinx_rsa_free_key_bufs(tctx);
	return ret;
}

static int xilinx_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				   unsigned int keylen)
{
	struct xilinx_rsa_tfm_ctx *tfm_ctx = akcipher_tfm_ctx(tfm);
	int ret;

	tfm_ctx->fbk_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	tfm_ctx->fbk_cipher->base.crt_flags |= (tfm->base.crt_flags &
						CRYPTO_TFM_REQ_MASK);

	ret = crypto_akcipher_set_priv_key(tfm_ctx->fbk_cipher, key, keylen);
	if (ret)
		return ret;

	return xilinx_rsa_setkey(tfm, key, keylen, true);
}

static int xilinx_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				  unsigned int keylen)
{
	struct xilinx_rsa_tfm_ctx *tfm_ctx = akcipher_tfm_ctx(tfm);
	int ret;

	tfm_ctx->fbk_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	tfm_ctx->fbk_cipher->base.crt_flags |= (tfm->base.crt_flags &
						CRYPTO_TFM_REQ_MASK);

	ret = crypto_akcipher_set_pub_key(tfm_ctx->fbk_cipher, key, keylen);
	if (ret)
		return ret;

	return xilinx_rsa_setkey(tfm, key, keylen, false);
}

static int xilinx_fallback_check(const struct xilinx_rsa_tfm_ctx *tfm_ctx,
				 const struct akcipher_request *areq)
{
	/* Return 1 if fallback to crypto engine for performing requested operation */
	if (tfm_ctx->n_len != XSECURE_RSA_2048_KEY_SIZE &&
	    tfm_ctx->n_len != XSECURE_RSA_3072_KEY_SIZE &&
	    tfm_ctx->n_len != XSECURE_RSA_4096_KEY_SIZE)
		return 1;

	if (areq->src_len > areq->dst_len)
		return 1;

	return 0;
}

static int handle_rsa_req(struct crypto_engine *engine,
			  void *req)
{
	struct akcipher_request *areq = container_of(req,
						     struct akcipher_request,
						     base);
	struct crypto_akcipher *akcipher = crypto_akcipher_reqtfm(req);
	struct akcipher_alg *cipher_alg = crypto_akcipher_alg(akcipher);
	const struct xilinx_rsa_tfm_ctx *tfm_ctx = akcipher_tfm_ctx(akcipher);
	const struct xilinx_rsa_req_ctx *rq_ctx = akcipher_request_ctx(areq);
	struct akcipher_request *subreq = akcipher_request_ctx(req);
	struct xilinx_rsa_drv_ctx *drv_ctx;
	int need_fallback, err;

	drv_ctx = container_of(cipher_alg, struct xilinx_rsa_drv_ctx, alg.base);

	need_fallback = xilinx_fallback_check(tfm_ctx, areq);
	if (need_fallback) {
		akcipher_request_set_tfm(subreq, tfm_ctx->fbk_cipher);

		akcipher_request_set_callback(subreq, areq->base.flags,
					      NULL, NULL);
		akcipher_request_set_crypt(subreq, areq->src, areq->dst,
					   areq->src_len, areq->dst_len);

		if (rq_ctx->op == XILINX_RSA_ENCRYPT)
			err = crypto_akcipher_encrypt(subreq);
		else if (rq_ctx->op == XILINX_RSA_DECRYPT)
			err = crypto_akcipher_decrypt(subreq);
		else
			err = -EOPNOTSUPP;
	} else {
		err = drv_ctx->xilinx_rsa_xcrypt(areq);
	}

	crypto_finalize_akcipher_request(engine, areq, err);

	return 0;
}

static int xilinx_rsa_init(struct crypto_akcipher *tfm)
{
	struct xilinx_rsa_tfm_ctx *tfm_ctx =
		(struct xilinx_rsa_tfm_ctx *)akcipher_tfm_ctx(tfm);
	struct akcipher_alg *cipher_alg = crypto_akcipher_alg(tfm);
	struct xilinx_rsa_drv_ctx *drv_ctx;

	drv_ctx = container_of(cipher_alg, struct xilinx_rsa_drv_ctx, alg.base);
	tfm_ctx->dev = drv_ctx->dev;
	tfm_ctx->fbk_cipher = crypto_alloc_akcipher(drv_ctx->alg.base.base.cra_name,
						    0,
						    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tfm_ctx->fbk_cipher)) {
		pr_err("%s() Error: failed to allocate fallback for %s\n",
		       __func__, drv_ctx->alg.base.base.cra_name);
		return PTR_ERR(tfm_ctx->fbk_cipher);
	}

	akcipher_set_reqsize(tfm, max(sizeof(struct xilinx_rsa_req_ctx),
			     sizeof(struct akcipher_request) +
			     crypto_akcipher_reqsize(tfm_ctx->fbk_cipher)));

	return 0;
}

static void xilinx_rsa_exit(struct crypto_akcipher *tfm)
{
	struct xilinx_rsa_tfm_ctx *tfm_ctx =
			(struct xilinx_rsa_tfm_ctx *)akcipher_tfm_ctx(tfm);

	xilinx_rsa_free_key_bufs(tfm_ctx);

	if (tfm_ctx->fbk_cipher) {
		crypto_free_akcipher(tfm_ctx->fbk_cipher);
		tfm_ctx->fbk_cipher = NULL;
	}
	memzero_explicit(tfm_ctx, sizeof(struct xilinx_rsa_tfm_ctx));
}

static struct xilinx_rsa_drv_ctx zynqmp_rsa_drv_ctx = {
	.xilinx_rsa_xcrypt = zynqmp_rsa_xcrypt,
	.alg.base = {
		.init = xilinx_rsa_init,
		.set_pub_key = xilinx_rsa_set_pub_key,
		.set_priv_key = xilinx_rsa_set_priv_key,
		.max_size = xilinx_rsa_max_size,
		.decrypt = xilinx_rsa_decrypt,
		.encrypt = xilinx_rsa_encrypt,
		.sign = xilinx_rsa_decrypt,
		.verify = xilinx_rsa_encrypt,
		.exit = xilinx_rsa_exit,
		.base = {
			.cra_name = "rsa",
			.cra_driver_name = "zynqmp-rsa",
			.cra_priority = 200,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_ALLOCATES_MEMORY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = XILINX_RSA_BLOCKSIZE,
			.cra_ctxsize = sizeof(struct xilinx_rsa_tfm_ctx),
			.cra_alignmask = 15,
			.cra_module = THIS_MODULE,
		},
	},
	.alg.op = {
		.do_one_request = handle_rsa_req,
	},
};

static struct xilinx_rsa_drv_ctx versal_rsa_drv_ctx = {
	.xilinx_rsa_xcrypt = versal_rsa_xcrypt,
	.alg.base = {
		.init = xilinx_rsa_init,
		.set_pub_key = xilinx_rsa_set_pub_key,
		.set_priv_key = xilinx_rsa_set_priv_key,
		.max_size = xilinx_rsa_max_size,
		.decrypt = xilinx_rsa_decrypt,
		.encrypt = xilinx_rsa_encrypt,
		.sign = xilinx_rsa_decrypt,
		.verify = xilinx_rsa_encrypt,
		.exit = xilinx_rsa_exit,
		.base = {
			.cra_name = "rsa",
			.cra_driver_name = "versal-rsa",
			.cra_priority = 200,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_ALLOCATES_MEMORY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_blocksize = XILINX_RSA_BLOCKSIZE,
			.cra_ctxsize = sizeof(struct xilinx_rsa_tfm_ctx),
			.cra_alignmask = 15,
			.cra_module = THIS_MODULE,
		},
	},
	.alg.op = {
		.do_one_request = handle_rsa_req,
	},
};

static struct xlnx_feature rsa_feature_map[] = {
	{
		.family = ZYNQMP_FAMILY_CODE,
		.subfamily = ALL_SUB_FAMILY_CODE,
		.feature_id = PM_SECURE_RSA,
		.data = &zynqmp_rsa_drv_ctx,
	},
	{
		.family = VERSAL_FAMILY_CODE,
		.subfamily = VERSAL_SUB_FAMILY_CODE,
		.feature_id = XSECURE_API_RSA_PUBLIC_ENCRYPT,
		.data = &versal_rsa_drv_ctx,
	},
	{ /* sentinel */ }
};

static int xilinx_rsa_probe(struct platform_device *pdev)
{
	struct xilinx_rsa_drv_ctx *rsa_drv_ctx;
	struct device *dev = &pdev->dev;
	int ret;

	/* Verify the hardware is present */
	rsa_drv_ctx = xlnx_get_crypto_dev_data(rsa_feature_map);
	if (IS_ERR(rsa_drv_ctx)) {
		dev_err(dev, "RSA is not supported on the platform\n");
		return PTR_ERR(rsa_drv_ctx);
	}

	ret = dma_set_mask_and_coherent(dev,
					DMA_BIT_MASK(XILINX_DMA_BIT_MASK));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	rsa_drv_ctx->engine = crypto_engine_alloc_init(dev, 1);
	if (!rsa_drv_ctx->engine) {
		dev_err(dev, "Cannot alloc RSA engine\n");
		return -ENOMEM;
	}

	ret = crypto_engine_start(rsa_drv_ctx->engine);
	if (ret) {
		dev_err(dev, "Cannot start RSA engine\n");
		goto out;
	}

	rsa_drv_ctx->dev = dev;
	platform_set_drvdata(pdev, rsa_drv_ctx);

	ret = crypto_engine_register_akcipher(&rsa_drv_ctx->alg);
	if (ret < 0) {
		dev_err(dev, "Failed to register akcipher alg.\n");
		goto out;
	}

	return 0;

out:
	crypto_engine_exit(rsa_drv_ctx->engine);

	return ret;
}

static int xilinx_rsa_remove(struct platform_device *pdev)
{
	struct xilinx_rsa_drv_ctx *rsa_drv_ctx;

	rsa_drv_ctx = platform_get_drvdata(pdev);

	crypto_engine_exit(rsa_drv_ctx->engine);

	crypto_engine_unregister_akcipher(&rsa_drv_ctx->alg);

	return 0;
}

static struct platform_driver xilinx_rsa_driver = {
	.probe = xilinx_rsa_probe,
	.remove = xilinx_rsa_remove,
	.driver = {
		.name = "xilinx_rsa",
	},
};

static struct platform_device *platform_dev;

static int __init xilinx_rsa_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&xilinx_rsa_driver);
	if (ret)
		return ret;

	platform_dev = platform_device_register_simple(xilinx_rsa_driver.driver.name,
					       0, NULL, 0);
	if (IS_ERR(platform_dev)) {
		ret = PTR_ERR(platform_dev);
		platform_driver_unregister(&xilinx_rsa_driver);
	}

	return ret;
}

static void __exit xilinx_rsa_driver_exit(void)
{
	platform_device_unregister(platform_dev);
	platform_driver_unregister(&xilinx_rsa_driver);
}

module_init(xilinx_rsa_driver_init);
module_exit(xilinx_rsa_driver_exit);

MODULE_DESCRIPTION("Xilinx RSA hw acceleration support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harsha <harsha.harsha@amd.com>");
