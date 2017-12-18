/*
 * Crypto acceleration support for Rockchip RK3288
 *
 * Copyright (c) 2015, Fuzhou Rockchip Electronics Co., Ltd
 *
 * Author: Zain Wang <zain.wang@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * Some ideas are from marvell-cesa.c and s5p-sss.c driver.
 */
#include "rk3288_crypto.h"

#define RK_CRYPTO_DEC			BIT(0)

static void rk_crypto_complete(struct rk_crypto_info *dev, int err)
{
	if (dev->ablk_req->base.complete)
		dev->ablk_req->base.complete(&dev->ablk_req->base, err);
}

static int rk_handle_req(struct rk_crypto_info *dev,
			 struct ablkcipher_request *req)
{
	unsigned long flags;
	int err;

	if (!IS_ALIGNED(req->nbytes, dev->align_size))
		return -EINVAL;

	dev->left_bytes = req->nbytes;
	dev->total = req->nbytes;
	dev->sg_src = req->src;
	dev->first = req->src;
	dev->nents = sg_nents(req->src);
	dev->sg_dst = req->dst;
	dev->aligned = 1;
	dev->ablk_req = req;

	spin_lock_irqsave(&dev->lock, flags);
	err = ablkcipher_enqueue_request(&dev->queue, req);
	spin_unlock_irqrestore(&dev->lock, flags);
	tasklet_schedule(&dev->crypto_tasklet);
	return err;
}

static int rk_aes_setkey(struct crypto_ablkcipher *cipher,
			 const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	if (keylen != AES_KEYSIZE_128 && keylen != AES_KEYSIZE_192 &&
	    keylen != AES_KEYSIZE_256) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	ctx->keylen = keylen;
	memcpy_toio(ctx->dev->reg + RK_CRYPTO_AES_KEY_0, key, keylen);
	return 0;
}

static int rk_tdes_setkey(struct crypto_ablkcipher *cipher,
			  const u8 *key, unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	u32 tmp[DES_EXPKEY_WORDS];

	if (keylen != DES_KEY_SIZE && keylen != DES3_EDE_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	if (keylen == DES_KEY_SIZE) {
		if (!des_ekey(tmp, key) &&
		    (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
			tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
			return -EINVAL;
		}
	}

	ctx->keylen = keylen;
	memcpy_toio(ctx->dev->reg + RK_CRYPTO_TDES_KEY1_0, key, keylen);
	return 0;
}

static int rk_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_AES_ECB_MODE;
	return rk_handle_req(dev, req);
}

static int rk_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_AES_ECB_MODE | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_AES_CBC_MODE;
	return rk_handle_req(dev, req);
}

static int rk_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_AES_CBC_MODE | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = 0;
	return rk_handle_req(dev, req);
}

static int rk_des_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_CHAINMODE_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_CHAINMODE_CBC | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_SELECT;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_ecb_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_TDES_CHAINMODE_CBC;
	return rk_handle_req(dev, req);
}

static int rk_des3_ede_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct rk_crypto_info *dev = ctx->dev;

	dev->mode = RK_CRYPTO_TDES_SELECT | RK_CRYPTO_TDES_CHAINMODE_CBC |
		    RK_CRYPTO_DEC;
	return rk_handle_req(dev, req);
}

static void rk_ablk_hw_init(struct rk_crypto_info *dev)
{
	struct crypto_ablkcipher *cipher =
		crypto_ablkcipher_reqtfm(dev->ablk_req);
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct rk_cipher_ctx *ctx = crypto_ablkcipher_ctx(cipher);
	u32 ivsize, block, conf_reg = 0;

	block = crypto_tfm_alg_blocksize(tfm);
	ivsize = crypto_ablkcipher_ivsize(cipher);

	if (block == DES_BLOCK_SIZE) {
		dev->mode |= RK_CRYPTO_TDES_FIFO_MODE |
			     RK_CRYPTO_TDES_BYTESWAP_KEY |
			     RK_CRYPTO_TDES_BYTESWAP_IV;
		CRYPTO_WRITE(dev, RK_CRYPTO_TDES_CTRL, dev->mode);
		memcpy_toio(dev->reg + RK_CRYPTO_TDES_IV_0,
			    dev->ablk_req->info, ivsize);
		conf_reg = RK_CRYPTO_DESSEL;
	} else {
		dev->mode |= RK_CRYPTO_AES_FIFO_MODE |
			     RK_CRYPTO_AES_KEY_CHANGE |
			     RK_CRYPTO_AES_BYTESWAP_KEY |
			     RK_CRYPTO_AES_BYTESWAP_IV;
		if (ctx->keylen == AES_KEYSIZE_192)
			dev->mode |= RK_CRYPTO_AES_192BIT_key;
		else if (ctx->keylen == AES_KEYSIZE_256)
			dev->mode |= RK_CRYPTO_AES_256BIT_key;
		CRYPTO_WRITE(dev, RK_CRYPTO_AES_CTRL, dev->mode);
		memcpy_toio(dev->reg + RK_CRYPTO_AES_IV_0,
			    dev->ablk_req->info, ivsize);
	}
	conf_reg |= RK_CRYPTO_BYTESWAP_BTFIFO |
		    RK_CRYPTO_BYTESWAP_BRFIFO;
	CRYPTO_WRITE(dev, RK_CRYPTO_CONF, conf_reg);
	CRYPTO_WRITE(dev, RK_CRYPTO_INTENA,
		     RK_CRYPTO_BCDMA_ERR_ENA | RK_CRYPTO_BCDMA_DONE_ENA);
}

static void crypto_dma_start(struct rk_crypto_info *dev)
{
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAS, dev->addr_in);
	CRYPTO_WRITE(dev, RK_CRYPTO_BRDMAL, dev->count / 4);
	CRYPTO_WRITE(dev, RK_CRYPTO_BTDMAS, dev->addr_out);
	CRYPTO_WRITE(dev, RK_CRYPTO_CTRL, RK_CRYPTO_BLOCK_START |
		     _SBF(RK_CRYPTO_BLOCK_START, 16));
}

static int rk_set_data_start(struct rk_crypto_info *dev)
{
	int err;

	err = dev->load_data(dev, dev->sg_src, dev->sg_dst);
	if (!err)
		crypto_dma_start(dev);
	return err;
}

static int rk_ablk_start(struct rk_crypto_info *dev)
{
	unsigned long flags;
	int err;

	spin_lock_irqsave(&dev->lock, flags);
	rk_ablk_hw_init(dev);
	err = rk_set_data_start(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	return err;
}

static void rk_iv_copyback(struct rk_crypto_info *dev)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(dev->ablk_req);
	u32 ivsize = crypto_ablkcipher_ivsize(tfm);

	if (ivsize == DES_BLOCK_SIZE)
		memcpy_fromio(dev->ablk_req->info,
			      dev->reg + RK_CRYPTO_TDES_IV_0, ivsize);
	else if (ivsize == AES_BLOCK_SIZE)
		memcpy_fromio(dev->ablk_req->info,
			      dev->reg + RK_CRYPTO_AES_IV_0, ivsize);
}

/* return:
 *	true	some err was occurred
 *	fault	no err, continue
 */
static int rk_ablk_rx(struct rk_crypto_info *dev)
{
	int err = 0;

	dev->unload_data(dev);
	if (!dev->aligned) {
		if (!sg_pcopy_from_buffer(dev->ablk_req->dst, dev->nents,
					  dev->addr_vir, dev->count,
					  dev->total - dev->left_bytes -
					  dev->count)) {
			err = -EINVAL;
			goto out_rx;
		}
	}
	if (dev->left_bytes) {
		if (dev->aligned) {
			if (sg_is_last(dev->sg_src)) {
				dev_err(dev->dev, "[%s:%d] Lack of data\n",
					__func__, __LINE__);
				err = -ENOMEM;
				goto out_rx;
			}
			dev->sg_src = sg_next(dev->sg_src);
			dev->sg_dst = sg_next(dev->sg_dst);
		}
		err = rk_set_data_start(dev);
	} else {
		rk_iv_copyback(dev);
		/* here show the calculation is over without any err */
		dev->complete(dev, 0);
	}
out_rx:
	return err;
}

static int rk_ablk_cra_init(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct rk_crypto_tmp *algt;

	algt = container_of(alg, struct rk_crypto_tmp, alg.crypto);

	ctx->dev = algt->dev;
	ctx->dev->align_size = crypto_tfm_alg_alignmask(tfm) + 1;
	ctx->dev->start = rk_ablk_start;
	ctx->dev->update = rk_ablk_rx;
	ctx->dev->complete = rk_crypto_complete;
	ctx->dev->addr_vir = (char *)__get_free_page(GFP_KERNEL);

	return ctx->dev->addr_vir ? ctx->dev->enable_clk(ctx->dev) : -ENOMEM;
}

static void rk_ablk_cra_exit(struct crypto_tfm *tfm)
{
	struct rk_cipher_ctx *ctx = crypto_tfm_ctx(tfm);

	free_page((unsigned long)ctx->dev->addr_vir);
	ctx->dev->disable_clk(ctx->dev);
}

struct rk_crypto_tmp rk_ecb_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(aes)",
		.cra_driver_name	= "ecb-aes-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x0f,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= rk_aes_setkey,
			.encrypt	= rk_aes_ecb_encrypt,
			.decrypt	= rk_aes_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_cbc_aes_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(aes)",
		.cra_driver_name	= "cbc-aes-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= AES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x0f,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= rk_aes_setkey,
			.encrypt	= rk_aes_cbc_encrypt,
			.decrypt	= rk_aes_cbc_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_ecb_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(des)",
		.cra_driver_name	= "ecb-des-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des_ecb_encrypt,
			.decrypt	= rk_des_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_cbc_des_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(des)",
		.cra_driver_name	= "cbc-des-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des_cbc_encrypt,
			.decrypt	= rk_des_cbc_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_ecb_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "ecb(des3_ede)",
		.cra_driver_name	= "ecb-des3-ede-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des3_ede_ecb_encrypt,
			.decrypt	= rk_des3_ede_ecb_decrypt,
		}
	}
};

struct rk_crypto_tmp rk_cbc_des3_ede_alg = {
	.type = ALG_TYPE_CIPHER,
	.alg.crypto = {
		.cra_name		= "cbc(des3_ede)",
		.cra_driver_name	= "cbc-des3-ede-rk",
		.cra_priority		= 300,
		.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER |
					  CRYPTO_ALG_ASYNC,
		.cra_blocksize		= DES_BLOCK_SIZE,
		.cra_ctxsize		= sizeof(struct rk_cipher_ctx),
		.cra_alignmask		= 0x07,
		.cra_type		= &crypto_ablkcipher_type,
		.cra_module		= THIS_MODULE,
		.cra_init		= rk_ablk_cra_init,
		.cra_exit		= rk_ablk_cra_exit,
		.cra_u.ablkcipher	= {
			.min_keysize	= DES3_EDE_KEY_SIZE,
			.max_keysize	= DES3_EDE_KEY_SIZE,
			.ivsize		= DES_BLOCK_SIZE,
			.setkey		= rk_tdes_setkey,
			.encrypt	= rk_des3_ede_cbc_encrypt,
			.decrypt	= rk_des3_ede_cbc_decrypt,
		}
	}
};
