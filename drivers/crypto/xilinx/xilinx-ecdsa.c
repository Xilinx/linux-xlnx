// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal ECDSA Driver.
 * Copyright (C) 2022 - 2024, Advanced Micro Devices, Inc.
 */

#include <crypto/ecdh.h>
#include <crypto/engine.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/ecc.h>
#include <crypto/ecdsa.h>
#include <linux/asn1_decoder.h>
#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include "xilinx_ecdsasig.asn1.h"

/* PLM supports 32-bit addresses only */
#define VERSAL_DMA_BIT_MASK			32U

/* PLM can process HASH and signature in multiples of 8 bytes */
#define ECDSA_P521_CURVE_ALIGN_BYTES		2U

struct xilinx_sign_gen_params {
	u64 hash_addr;
	u64 privkey_addr;
	u64 eprivkey_addr;
	u32 curve_type;
	u32 size;
};

struct xilinx_sign_verify_params {
	u64 hash_addr;
	u64 pubkey_addr;
	u64 sign_addr;
	u32 curve_type;
	u32 size;
};

enum xilinx_crv_typ {
	XSECURE_ECC_NIST_P384 = 4,
	XSECURE_ECC_NIST_P521 = 5,
};

enum xilinx_crv_class {
	XSECURE_ECDSA_PRIME = 0,
	XSECURE_ECDSA_BINARY = 1,
};

struct xilinx_ecdsa_drv_ctx {
	struct crypto_engine *engine;
	struct akcipher_engine_alg alg;
	struct device *dev;
};

enum xilinx_akcipher_op {
	XILINX_ECDSA_DECRYPT = 0,
	XILINX_ECDSA_ENCRYPT
};

struct xilinx_ecdsa_tfm_ctx {
	dma_addr_t priv_key_addr, pub_key_addr;
	struct crypto_akcipher *fbk_cipher;
	const struct ecc_curve *curve;
	unsigned int curve_id;
	struct device *dev;
	size_t key_size;
	char *pub_kbuf;
};

struct xilinx_ecdsa_req_ctx {
	enum xilinx_akcipher_op op;
};

static int xilinx_ecdsa_sign(struct akcipher_request *req)
{
	return 0;
}

int xilinx_ecdsa_get_signature_r(void *context, size_t hdrlen, unsigned char tag,
				 const void *value, size_t vlen)
{
	struct ecdsa_signature_ctx *sig = context;

	return ecdsa_get_signature_rs(sig->r, hdrlen, tag, value, vlen,
				      sig->curve->g.ndigits);
}

int xilinx_ecdsa_get_signature_s(void *context, size_t hdrlen, unsigned char tag,
			  const void *value, size_t vlen)
{
	struct ecdsa_signature_ctx *sig = context;

	return ecdsa_get_signature_rs(sig->s, hdrlen, tag, value, vlen,
				      sig->curve->g.ndigits);
}

static int xilinx_ecdsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct xilinx_ecdsa_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	size_t keylen = ctx->curve->g.ndigits * sizeof(u64);
	dma_addr_t dma_addr, dma_addr1, dma_addr2;
	struct xilinx_sign_verify_params *para;
	char *hash_buf, *sign_buf;
	u8 rawhash[ECC_MAX_BYTES];
	unsigned char *buffer;
	ssize_t diff;
	int ret;
	struct ecdsa_signature_ctx sig_ctx = {
		.curve = ctx->curve,
	};

	buffer = kmalloc(req->src_len + req->dst_len, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	sg_pcopy_to_buffer(req->src,
			   sg_nents_for_len(req->src,
					    req->src_len + req->dst_len),
			   buffer, req->src_len + req->dst_len, 0);

	ret = asn1_ber_decoder(&xilinx_ecdsasig_decoder, &sig_ctx,
			       buffer, req->src_len);
	if (ret < 0)
		goto error;

	/*
	 * If the hash is shorter then we will add leading zeros
	 * to fit to ndigits
	 */
	diff = keylen - req->dst_len;
	if (diff >= 0) {
		if (diff)
			memset(rawhash, 0, diff);
		memcpy(&rawhash[diff], buffer + req->src_len, req->dst_len);
	} else {
		/* Given hash is longer, we take the left-most bytes */
		memcpy(&rawhash, buffer + req->src_len, keylen);
	}

	para = dma_alloc_coherent(ctx->dev,
				  sizeof(struct xilinx_sign_verify_params),
				  &dma_addr, GFP_KERNEL);
	if (!para) {
		ret = -ENOMEM;
		goto error;
	}

	if (ctx->curve_id == XSECURE_ECC_NIST_P521)
		keylen = ((ctx->curve->g.ndigits - 1) * sizeof(u64)) + ECDSA_P521_CURVE_ALIGN_BYTES;

	hash_buf = dma_alloc_coherent(ctx->dev, keylen,
				      &dma_addr1, GFP_KERNEL);
	if (!hash_buf) {
		ret = -ENOMEM;
		goto hash_fail;
	}

	sign_buf = dma_alloc_coherent(ctx->dev, ctx->key_size,
				      &dma_addr2, GFP_KERNEL);
	if (!sign_buf) {
		ret = -ENOMEM;
		goto sign_fail;
	}

	para->pubkey_addr = ctx->pub_key_addr;
	para->curve_type = ctx->curve_id;
	para->sign_addr = dma_addr2;
	para->hash_addr = dma_addr1;
	para->size = keylen;

	memcpy(sign_buf, sig_ctx.r, keylen);
	memcpy(sign_buf + keylen, sig_ctx.s, keylen);

	ecc_swap_digits((u64 *)rawhash, (u64 *)hash_buf,
			ctx->curve->g.ndigits);

	ret = versal_pm_ecdsa_verify_sign(dma_addr);

	dma_free_coherent(ctx->dev, ctx->key_size, sign_buf, dma_addr2);

sign_fail:
	dma_free_coherent(ctx->dev, keylen, hash_buf, dma_addr1);

hash_fail:
	dma_free_coherent(ctx->dev, sizeof(struct xilinx_sign_verify_params),
			  para, dma_addr);

error:
	kfree(buffer);

	return ret;
}

static int xilinx_ecdsa_ctx_init(struct xilinx_ecdsa_tfm_ctx *ctx,
				 unsigned int curve_id)
{
	if (curve_id == ECC_CURVE_NIST_P384)
		ctx->curve_id = XSECURE_ECC_NIST_P384;
	else
		ctx->curve_id = XSECURE_ECC_NIST_P521;

	ctx->curve = ecc_get_curve(curve_id);
	if (!ctx->curve)
		return -EINVAL;

	return 0;
}

/*
 * Set the public key given the raw uncompressed key data from an X509
 * certificate. The key data contain the concatenated X and Y coordinates of
 * the public key.
 */
static int xilinx_ecdsa_set_pub_key(struct crypto_akcipher *tfm,
				    const void *key, unsigned int keylen)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);
	unsigned int ndigits, key_size;
	const unsigned char *d = key;

	if (keylen < 1 || ((keylen - 1) & 1) != 0)
		return -EINVAL;

	/* The key should be in uncompressed format indicated by '4' */
	if (d[0] != 4)
		return -EINVAL;

	keylen--;
	ctx->key_size = keylen;

	key_size = keylen >> 1;
	ndigits = DIV_ROUND_UP(key_size, sizeof(u64));
	if (ndigits != ctx->curve->g.ndigits)
		return -EINVAL;

	ctx->pub_kbuf = dma_alloc_coherent(ctx->dev, keylen,
					   &ctx->pub_key_addr, GFP_KERNEL);
	if (!ctx->pub_kbuf)
		return -ENOMEM;

	d++;

	ecc_digits_from_bytes(d, key_size, (u64 *)ctx->pub_kbuf, ndigits);
	ecc_digits_from_bytes(&d[key_size], key_size, (u64 *)(ctx->pub_kbuf + key_size), ndigits);

	return versal_pm_ecdsa_validate_key(ctx->pub_key_addr, ctx->curve_id);
}

static void xilinx_ecdsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);

	if (ctx->fbk_cipher) {
		crypto_free_akcipher(ctx->fbk_cipher);
		ctx->fbk_cipher = NULL;
	}

	if (ctx->pub_kbuf) {
		dma_free_coherent(ctx->dev, ctx->key_size,
				  ctx->pub_kbuf, ctx->pub_key_addr);
	}

	memzero_explicit(ctx, sizeof(struct xilinx_ecdsa_tfm_ctx));
}

static unsigned int xilinx_ecdsa_max_size(struct crypto_akcipher *tfm)
{
	const struct xilinx_ecdsa_tfm_ctx *ctx = akcipher_tfm_ctx(tfm);

	return DIV_ROUND_UP(ctx->curve->nbits, 8);
}

static int xilinx_ecdsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct xilinx_ecdsa_tfm_ctx *tfm_ctx =
		(struct xilinx_ecdsa_tfm_ctx *)akcipher_tfm_ctx(tfm);
	struct akcipher_alg *cipher_alg = crypto_akcipher_alg(tfm);
	struct xilinx_ecdsa_drv_ctx *drv_ctx;

	drv_ctx = container_of(cipher_alg, struct xilinx_ecdsa_drv_ctx, alg.base);
	tfm_ctx->dev = drv_ctx->dev;

	tfm_ctx->fbk_cipher = crypto_alloc_akcipher(drv_ctx->alg.base.base.cra_name,
						    0,
						    CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(tfm_ctx->fbk_cipher)) {
		pr_err("%s() Error: failed to allocate fallback for %s\n",
		       __func__, drv_ctx->alg.base.base.cra_name);
		return PTR_ERR(tfm_ctx->fbk_cipher);
	}

	akcipher_set_reqsize(tfm, max(sizeof(struct xilinx_ecdsa_req_ctx),
				      sizeof(struct akcipher_request) +
				      crypto_akcipher_reqsize(tfm_ctx->fbk_cipher)));

	if (strcmp(drv_ctx->alg.base.base.cra_name, "ecdsa-nist-p384") == 0)
		return xilinx_ecdsa_ctx_init(tfm_ctx, ECC_CURVE_NIST_P384);
	else
		return xilinx_ecdsa_ctx_init(tfm_ctx, ECC_CURVE_NIST_P521);
}

static int handle_ecdsa_req(struct crypto_engine *engine, void *req)
{
	struct akcipher_request *areq = container_of(req,
						     struct akcipher_request,
						     base);
	struct crypto_akcipher *akcipher = crypto_akcipher_reqtfm(req);
	const struct xilinx_ecdsa_tfm_ctx *tfm_ctx = akcipher_tfm_ctx(akcipher);
	const struct xilinx_ecdsa_req_ctx *rq_ctx = akcipher_request_ctx(areq);
	struct akcipher_request *subreq = akcipher_request_ctx(req);
	int err;

	akcipher_request_set_tfm(subreq, tfm_ctx->fbk_cipher);

	akcipher_request_set_callback(subreq, areq->base.flags, NULL, NULL);
	akcipher_request_set_crypt(subreq, areq->src, areq->dst,
				   areq->src_len, areq->dst_len);

	if (rq_ctx->op == XILINX_ECDSA_ENCRYPT)
		err = crypto_akcipher_encrypt(subreq);
	else if (rq_ctx->op == XILINX_ECDSA_DECRYPT)
		err = crypto_akcipher_decrypt(subreq);
	else
		err = -EOPNOTSUPP;

	crypto_finalize_akcipher_request(engine, areq, err);

	return 0;
}

static struct xilinx_ecdsa_drv_ctx versal_ecdsa_drv_ctx[] = {
	{
	.alg.base = {
		.verify = xilinx_ecdsa_verify,
		.set_pub_key = xilinx_ecdsa_set_pub_key,
		.max_size = xilinx_ecdsa_max_size,
		.init = xilinx_ecdsa_init_tfm,
		.exit = xilinx_ecdsa_exit_tfm,
		.sign = xilinx_ecdsa_sign,
		.base = {
			.cra_name = "ecdsa-nist-p384",
			.cra_driver_name = "xilinx-ecdsa-nist-p384",
			.cra_priority = 100,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_ALLOCATES_MEMORY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct xilinx_ecdsa_tfm_ctx),
		},
	},
	.alg.op = {
		.do_one_request = handle_ecdsa_req,
	}
	},
	{
	.alg.base = {
		.verify = xilinx_ecdsa_verify,
		.set_pub_key = xilinx_ecdsa_set_pub_key,
		.max_size = xilinx_ecdsa_max_size,
		.init = xilinx_ecdsa_init_tfm,
		.exit = xilinx_ecdsa_exit_tfm,
		.sign = xilinx_ecdsa_sign,
		.base = {
			.cra_name = "ecdsa-nist-p521",
			.cra_driver_name = "xilinx-ecdsa-nist-p521",
			.cra_priority = 100,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				     CRYPTO_ALG_KERN_DRIVER_ONLY |
				     CRYPTO_ALG_ALLOCATES_MEMORY |
				     CRYPTO_ALG_NEED_FALLBACK,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct xilinx_ecdsa_tfm_ctx),
		},
	},
	.alg.op = {
		.do_one_request = handle_ecdsa_req,
	},
	}
};

static struct xlnx_feature ecdsa_feature_map[] = {
	{
		.family = VERSAL_FAMILY_CODE,
		.subfamily = VERSAL_SUB_FAMILY_CODE,
		.feature_id = XSECURE_API_ELLIPTIC_VALIDATE_KEY,
		.data = &versal_ecdsa_drv_ctx,
	},
	{ /* sentinel */ }
};

static int xilinx_ecdsa_probe(struct platform_device *pdev)
{
	struct xilinx_ecdsa_drv_ctx *ecdsa_drv_ctx;
	struct device *dev = &pdev->dev;
	int ret, i;

	/* Verify the hardware is present */
	ecdsa_drv_ctx = xlnx_get_crypto_dev_data(ecdsa_feature_map);
	if (IS_ERR(ecdsa_drv_ctx)) {
		dev_err(dev, "ECDSA is not supported on the platform\n");
		return PTR_ERR(ecdsa_drv_ctx);
	}

	ret = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(VERSAL_DMA_BIT_MASK));
	if (ret < 0) {
		dev_err(dev, "no usable DMA configuration");
		return ret;
	}

	ecdsa_drv_ctx->engine = crypto_engine_alloc_init(dev, 1);
	if (!ecdsa_drv_ctx->engine) {
		dev_err(dev, "Cannot alloc ECDSA engine\n");
		return -ENOMEM;
	}

	ret = crypto_engine_start(ecdsa_drv_ctx->engine);
	if (ret) {
		dev_err(dev, "Cannot start ECDSA engine\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(versal_ecdsa_drv_ctx); i++) {
		ecdsa_drv_ctx[i].dev = dev;
		platform_set_drvdata(pdev, &ecdsa_drv_ctx[i]);
		ret = crypto_engine_register_akcipher(&ecdsa_drv_ctx[i].alg);

		if (ret) {
			dev_err(dev, "failed to register %s (%d)!\n",
				ecdsa_drv_ctx[i].alg.base.base.cra_name, ret);
			goto crypto_engine_cleanup;
		}
	}

	return 0;

crypto_engine_cleanup:
	crypto_engine_exit(ecdsa_drv_ctx->engine);
	for (--i; i >= 0; --i)
		crypto_engine_register_akcipher(&ecdsa_drv_ctx[i].alg);

	return ret;
}

static int xilinx_ecdsa_remove(struct platform_device *pdev)
{
	struct xilinx_ecdsa_drv_ctx *ecdsa_drv_ctx;

	ecdsa_drv_ctx = platform_get_drvdata(pdev);

	for (int i = 0; i < ARRAY_SIZE(versal_ecdsa_drv_ctx); i++)
		crypto_engine_unregister_akcipher(&ecdsa_drv_ctx[i].alg);

	return 0;
}

static struct platform_driver xilinx_ecdsa_driver = {
	.probe = xilinx_ecdsa_probe,
	.remove = xilinx_ecdsa_remove,
	.driver = {
		.name = "xilinx_ecdsa",
	},
};

static struct platform_device *platform_dev;

static int __init ecdsa_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&xilinx_ecdsa_driver);
	if (ret)
		return ret;

	platform_dev = platform_device_register_simple(xilinx_ecdsa_driver.driver.name,
					       0, NULL, 0);
	if (IS_ERR(platform_dev)) {
		ret = PTR_ERR(platform_dev);
		platform_driver_unregister(&xilinx_ecdsa_driver);
	}

	return ret;
}

static void __exit ecdsa_driver_exit(void)
{
	platform_device_unregister(platform_dev);
	platform_driver_unregister(&xilinx_ecdsa_driver);
}

module_init(ecdsa_driver_init)
module_exit(ecdsa_driver_exit);

MODULE_DESCRIPTION("Versal ECDSA hw acceleration support.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harsha <harsha.harsha@amd.com>");
