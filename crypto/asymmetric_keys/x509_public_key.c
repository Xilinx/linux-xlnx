/* Instantiate a public key crypto key from an X.509 Certificate
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "X.509: "fmt
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mpi.h>
#include <linux/asn1_decoder.h>
#include <keys/asymmetric-subtype.h>
#include <keys/asymmetric-parser.h>
#include <crypto/hash.h>
#include "asymmetric_keys.h"
#include "public_key.h"
#include "x509_parser.h"

/*
 * Set up the signature parameters in an X.509 certificate.  This involves
 * digesting the signed data and extracting the signature.
 */
int x509_get_sig_params(struct x509_certificate *cert)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t digest_size, desc_size;
	void *digest;
	int ret;

	pr_devel("==>%s()\n", __func__);

	if (cert->sig.rsa.s)
		return 0;

	cert->sig.rsa.s = mpi_read_raw_data(cert->raw_sig, cert->raw_sig_size);
	if (!cert->sig.rsa.s)
		return -ENOMEM;
	cert->sig.nr_mpi = 1;

	/* Allocate the hashing algorithm we're going to need and find out how
	 * big the hash operational data will be.
	 */
	tfm = crypto_alloc_shash(hash_algo_name[cert->sig.pkey_hash_algo], 0, 0);
	if (IS_ERR(tfm))
		return (PTR_ERR(tfm) == -ENOENT) ? -ENOPKG : PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	digest_size = crypto_shash_digestsize(tfm);

	/* We allocate the hash operational data storage on the end of the
	 * digest storage space.
	 */
	ret = -ENOMEM;
	digest = kzalloc(digest_size + desc_size, GFP_KERNEL);
	if (!digest)
		goto error;

	cert->sig.digest = digest;
	cert->sig.digest_size = digest_size;

	desc = digest + digest_size;
	desc->tfm = tfm;
	desc->flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error;
	might_sleep();
	ret = crypto_shash_finup(desc, cert->tbs, cert->tbs_size, digest);
error:
	crypto_free_shash(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(x509_get_sig_params);

/*
 * Check the signature on a certificate using the provided public key
 */
int x509_check_signature(const struct public_key *pub,
			 struct x509_certificate *cert)
{
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = x509_get_sig_params(cert);
	if (ret < 0)
		return ret;

	ret = public_key_verify_signature(pub, &cert->sig);
	pr_debug("Cert Verification: %d\n", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(x509_check_signature);

/*
 * Attempt to parse a data blob for a key as an X509 certificate.
 */
static int x509_key_preparse(struct key_preparsed_payload *prep)
{
	struct x509_certificate *cert;
	size_t srlen, sulen;
	char *desc = NULL;
	int ret;

	cert = x509_cert_parse(prep->data, prep->datalen);
	if (IS_ERR(cert))
		return PTR_ERR(cert);

	pr_devel("Cert Issuer: %s\n", cert->issuer);
	pr_devel("Cert Subject: %s\n", cert->subject);

	if (cert->pub->pkey_algo >= PKEY_ALGO__LAST ||
	    cert->sig.pkey_algo >= PKEY_ALGO__LAST ||
	    cert->sig.pkey_hash_algo >= PKEY_HASH__LAST ||
	    !pkey_algo[cert->pub->pkey_algo] ||
	    !pkey_algo[cert->sig.pkey_algo] ||
	    !hash_algo_name[cert->sig.pkey_hash_algo]) {
		ret = -ENOPKG;
		goto error_free_cert;
	}

	pr_devel("Cert Key Algo: %s\n", pkey_algo_name[cert->pub->pkey_algo]);
	pr_devel("Cert Valid From: %04ld-%02d-%02d %02d:%02d:%02d\n",
		 cert->valid_from.tm_year + 1900, cert->valid_from.tm_mon + 1,
		 cert->valid_from.tm_mday, cert->valid_from.tm_hour,
		 cert->valid_from.tm_min,  cert->valid_from.tm_sec);
	pr_devel("Cert Valid To: %04ld-%02d-%02d %02d:%02d:%02d\n",
		 cert->valid_to.tm_year + 1900, cert->valid_to.tm_mon + 1,
		 cert->valid_to.tm_mday, cert->valid_to.tm_hour,
		 cert->valid_to.tm_min,  cert->valid_to.tm_sec);
	pr_devel("Cert Signature: %s + %s\n",
		 pkey_algo_name[cert->sig.pkey_algo],
		 hash_algo_name[cert->sig.pkey_hash_algo]);

	if (!cert->fingerprint) {
		pr_warn("Cert for '%s' must have a SubjKeyId extension\n",
			cert->subject);
		ret = -EKEYREJECTED;
		goto error_free_cert;
	}

	cert->pub->algo = pkey_algo[cert->pub->pkey_algo];
	cert->pub->id_type = PKEY_ID_X509;

	/* Check the signature on the key if it appears to be self-signed */
	if (!cert->authority ||
	    strcmp(cert->fingerprint, cert->authority) == 0) {
		ret = x509_check_signature(cert->pub, cert);
		if (ret < 0)
			goto error_free_cert;
	}

	/* Propose a description */
	sulen = strlen(cert->subject);
	srlen = strlen(cert->fingerprint);
	ret = -ENOMEM;
	desc = kmalloc(sulen + 2 + srlen + 1, GFP_KERNEL);
	if (!desc)
		goto error_free_cert;
	memcpy(desc, cert->subject, sulen);
	desc[sulen] = ':';
	desc[sulen + 1] = ' ';
	memcpy(desc + sulen + 2, cert->fingerprint, srlen);
	desc[sulen + 2 + srlen] = 0;

	/* We're pinning the module by being linked against it */
	__module_get(public_key_subtype.owner);
	prep->type_data[0] = &public_key_subtype;
	prep->type_data[1] = cert->fingerprint;
	prep->payload = cert->pub;
	prep->description = desc;
	prep->quotalen = 100;

	/* We've finished with the certificate */
	cert->pub = NULL;
	cert->fingerprint = NULL;
	desc = NULL;
	ret = 0;

error_free_cert:
	x509_free_certificate(cert);
	return ret;
}

static struct asymmetric_key_parser x509_key_parser = {
	.owner	= THIS_MODULE,
	.name	= "x509",
	.parse	= x509_key_preparse,
};

/*
 * Module stuff
 */
static int __init x509_key_init(void)
{
	return register_asymmetric_key_parser(&x509_key_parser);
}

static void __exit x509_key_exit(void)
{
	unregister_asymmetric_key_parser(&x509_key_parser);
}

module_init(x509_key_init);
module_exit(x509_key_exit);

MODULE_DESCRIPTION("X.509 certificate parser");
MODULE_LICENSE("GPL");
