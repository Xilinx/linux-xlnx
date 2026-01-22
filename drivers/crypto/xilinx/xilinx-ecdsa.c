// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal ECDSA Driver.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#include <crypto/ecdh.h>
#include <crypto/sig.h>
#include <crypto/sha2.h>
#include <crypto/internal/sig.h>
#include <crypto/internal/ecc.h>
#include <linux/crypto.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

/* PLM supports 64-bit addresses */
#define VERSAL_DMA_BIT_MASK			64U

/* PLM can process HASH and signature in multiples of 8 bytes */
#define ECDSA_P521_CURVE_ALIGN_BYTES		2U
/* Includes size for x and y coordinate. */
#define ECDSA_MAX_KEY_SIZE (ECC_MAX_BYTES << 1)

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

struct xilinx_ecdsa_alg {
	struct sig_alg alg;
	struct device *dev;
};

struct xilinx_ecdsa_tfm_ctx {
	dma_addr_t priv_key_addr, pub_key_addr;
	const struct ecc_curve *curve;
	unsigned int curve_id;
	struct device *dev;
	size_t key_size;
	char *pub_kbuf;
};

static unsigned int xilinx_ecdsa_key_size(struct crypto_sig *tfm)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = crypto_sig_ctx(tfm);

	return ctx->curve->nbits;
}

static unsigned int ecdsa_digest_size(struct crypto_sig *tfm)
{
	/*
	 * ECDSA key sizes are much smaller than RSA, and thus could
	 * operate on (hashed) inputs that are larger than the key size.
	 * E.g. SHA384-hashed input used with secp256r1 based keys.
	 * Return the largest supported hash size (SHA512).
	 */
	return SHA512_DIGEST_SIZE;
}

static int xilinx_ecdsa_verify(struct crypto_sig *tfm,
			       const void *src, unsigned int slen,
			       const void *digest, unsigned int dlen)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = crypto_sig_ctx(tfm);
	size_t keylen = ctx->curve->g.ndigits * sizeof(u64);
	struct xilinx_sign_verify_params *para;
	u8 rawhash[ECC_MAX_BYTES] = {0};
	char *hash_buf, *sign_buf;
	dma_addr_t dma_addr;
	void *dmabuf = NULL;
	ssize_t diff;
	u32 buflen;
	int ret;
	const struct ecdsa_raw_sig *sig = src;
	/*
	 * If the hash is shorter then we will add leading zeros
	 * to fit to ndigits
	 */
	diff = keylen - dlen;

	if (ctx->curve_id == XSECURE_ECC_NIST_P521)
		keylen = ((ctx->curve->g.ndigits - 1) * sizeof(u64)) + ECDSA_P521_CURVE_ALIGN_BYTES;

	if (diff >= 0) {
		memcpy(&rawhash[diff], digest, dlen);
	} else {
		/* Given hash is longer, we take the left-most bytes */
		memcpy(&rawhash, digest, keylen);
	}
	/* ecc_swap_digits operates on u64 size data buffer */
	buflen = sizeof(struct xilinx_sign_verify_params) + round_up(keylen, sizeof(u64)) +
		ctx->key_size;
	dmabuf = kmalloc(buflen, GFP_KERNEL);
	if (!dmabuf) {
		ret = -ENOMEM;
		goto error;
	}
	para = dmabuf;
	hash_buf = dmabuf + sizeof(struct xilinx_sign_verify_params);
	sign_buf = dmabuf + sizeof(struct xilinx_sign_verify_params) +
		   round_up(keylen, sizeof(u64));
	dma_addr = dma_map_single(ctx->dev, dmabuf, buflen, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(ctx->dev, dma_addr))) {
		ret = -ENOMEM;
		goto error;
	}

	para->pubkey_addr = ctx->pub_key_addr;
	para->curve_type = ctx->curve_id;
	para->sign_addr = dma_addr + sizeof(struct xilinx_sign_verify_params) +
			  round_up(keylen, sizeof(u64));
	para->hash_addr = dma_addr + sizeof(struct xilinx_sign_verify_params);
	para->size = keylen;

	memcpy(sign_buf, sig->r, keylen);
	memcpy(sign_buf + keylen, sig->s, keylen);

	ecc_swap_digits((u64 *)rawhash, (u64 *)hash_buf,
			ctx->curve->g.ndigits);
	dma_sync_single_for_device(ctx->dev, dma_addr, buflen, DMA_BIDIRECTIONAL);
	ret = versal_pm_ecdsa_verify_sign(dma_addr);
	dma_unmap_single(ctx->dev, dma_addr, buflen, DMA_BIDIRECTIONAL);
error:
	kfree(dmabuf);
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
static int xilinx_ecdsa_set_pub_key(struct crypto_sig *tfm,
				    const void *key, unsigned int keylen)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = crypto_sig_ctx(tfm);
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
	d++;

	ecc_digits_from_bytes(d, key_size, (u64 *)ctx->pub_kbuf, ndigits);
	ecc_digits_from_bytes(&d[key_size], key_size, (u64 *)(ctx->pub_kbuf + key_size), ndigits);
	dma_sync_single_for_device(ctx->dev, ctx->pub_key_addr, ECDSA_MAX_KEY_SIZE, DMA_TO_DEVICE);

	return versal_pm_ecdsa_validate_key(ctx->pub_key_addr, ctx->curve_id);
}

static void xilinx_ecdsa_exit_tfm(struct crypto_sig *tfm)
{
	struct xilinx_ecdsa_tfm_ctx *ctx = crypto_sig_ctx(tfm);

	if (ctx->pub_kbuf)
		dma_unmap_single(ctx->dev, ctx->pub_key_addr, ECDSA_MAX_KEY_SIZE, DMA_TO_DEVICE);

	memzero_explicit(ctx, sizeof(struct xilinx_ecdsa_tfm_ctx));
}

static int xilinx_ecdsa_init_tfm(struct crypto_sig *tfm)
{
	struct xilinx_ecdsa_tfm_ctx *tfm_ctx = crypto_sig_ctx(tfm);
	struct sig_alg *cipher_alg = crypto_sig_alg(tfm);
	struct xilinx_ecdsa_alg *drv_ctx;
	int ret;

	drv_ctx = container_of(cipher_alg, struct xilinx_ecdsa_alg, alg);
	tfm_ctx->dev = drv_ctx->dev;

	tfm_ctx->pub_kbuf = kmalloc(ECDSA_MAX_KEY_SIZE, GFP_KERNEL);
	if (!tfm_ctx->pub_kbuf)
		return -ENOMEM;
	tfm_ctx->pub_key_addr = dma_map_single(tfm_ctx->dev, tfm_ctx->pub_kbuf, ECDSA_MAX_KEY_SIZE,
					       DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(tfm_ctx->dev, tfm_ctx->pub_key_addr))) {
		ret = -ENOMEM;
		goto free;
	}

	if (strcmp(drv_ctx->alg.base.cra_name, "ecdsa-nist-p384") == 0)
		ret = xilinx_ecdsa_ctx_init(tfm_ctx, ECC_CURVE_NIST_P384);
	else
		ret = xilinx_ecdsa_ctx_init(tfm_ctx, ECC_CURVE_NIST_P521);
	if (ret)
		goto unmap;

	return ret;
unmap:
	dma_unmap_single(tfm_ctx->dev, tfm_ctx->pub_key_addr, ECDSA_MAX_KEY_SIZE, DMA_TO_DEVICE);
free:
	kfree(tfm_ctx->pub_kbuf);
	tfm_ctx->pub_kbuf = NULL;
	return ret;
}

static struct xilinx_ecdsa_alg versal_ecdsa_alg[] = {
	{
		.alg = {
			.verify = xilinx_ecdsa_verify,
			.set_pub_key = xilinx_ecdsa_set_pub_key,
			.key_size = xilinx_ecdsa_key_size,
			.digest_size = ecdsa_digest_size,
			.init = xilinx_ecdsa_init_tfm,
			.exit = xilinx_ecdsa_exit_tfm,
			.base = {
				.cra_name = "ecdsa-nist-p384",
				.cra_driver_name = "versal-ecdsa-nist-p384",
				.cra_priority = 100,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct xilinx_ecdsa_tfm_ctx),
				.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY |
					     CRYPTO_ALG_ALLOCATES_MEMORY,
			},
		},
	},
	{
		.alg = {
			.verify = xilinx_ecdsa_verify,
			.set_pub_key = xilinx_ecdsa_set_pub_key,
			.key_size = xilinx_ecdsa_key_size,
			.digest_size = ecdsa_digest_size,
			.init = xilinx_ecdsa_init_tfm,
			.exit = xilinx_ecdsa_exit_tfm,
			.base = {
				.cra_name = "ecdsa-nist-p521",
				.cra_driver_name = "versal-ecdsa-nist-p521",
				.cra_priority = 100,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct xilinx_ecdsa_tfm_ctx),
				.cra_flags = CRYPTO_ALG_KERN_DRIVER_ONLY |
					     CRYPTO_ALG_ALLOCATES_MEMORY,
			},
		},
	},
};

static struct xlnx_feature ecdsa_feature_map[] = {
	{
		.family = PM_VERSAL_FAMILY_CODE,
		.feature_id = XSECURE_API_ELLIPTIC_VALIDATE_KEY,
		.data = &versal_ecdsa_alg,
	},
	{ /* sentinel */ }
};

static int xilinx_ecdsa_probe(struct platform_device *pdev)
{
	struct xilinx_ecdsa_alg *ecdsa_drv_ctx;
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

	platform_set_drvdata(pdev, ecdsa_drv_ctx);

	for (i = 0; i < ARRAY_SIZE(versal_ecdsa_alg); i++) {
		ecdsa_drv_ctx[i].dev = dev;
		ret = crypto_register_sig(&ecdsa_drv_ctx[i].alg);
		if (ret) {
			dev_err(dev, "failed to register %s (%d)!\n",
				ecdsa_drv_ctx[i].alg.base.cra_name, ret);
					goto unregister_algs;
		}
	}

	return 0;

unregister_algs:
	for (--i; i >= 0; --i)
		crypto_unregister_sig(&ecdsa_drv_ctx[i].alg);

	return ret;
}

static void xilinx_ecdsa_remove(struct platform_device *pdev)
{
	struct xilinx_ecdsa_alg *ecdsa_drv_ctx;

	ecdsa_drv_ctx = platform_get_drvdata(pdev);
	for (int i = 0; i < ARRAY_SIZE(versal_ecdsa_alg); i++)
		crypto_unregister_sig(&ecdsa_drv_ctx[i].alg);
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
