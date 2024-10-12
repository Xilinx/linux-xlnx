/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Driver for Xilinx PUF device.
 *
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 *
 * Description:
 * This driver is developed for PUF registration and regeneration support.
 */

#ifndef _PUF_UAPI_H_
#define _PUF_UAPI_H_

#include <linux/types.h>

#define PUF_MAX_SYNDROME_DATA_LEN_IN_WORDS 140
#define PUF_EFUSE_TRIM_SYN_DATA_IN_WORDS   127
#define PUF_ID_LEN_IN_WORDS                8
#define PUF_ID_LEN_IN_BYTES                32
#define PUF_REGIS                          0
#define PUF_REGEN                          1
#define PUF_REGEN_ID                       2

/**
 * struct puf_usrparams - user parameters for PUF from user space.
 * @pufoperation: PUF registration or regeneration operation.
 * @globalvarfilter: global variation filter.
 * @readoption: option to read PUF data from efuse cache or ram address.
 * @shuttervalue: shutter value for PUF registration/regeneration.
 * @pufdataaddr: address to store/get the puf data during registration/regeneration.
 * @pufidaddr: puf id will be stored either during registration/regeneration.
 * @trimsyndataaddr: used during puf registration to store trimmed data.
 */
struct puf_usrparams {
	__u8 pufoperation;
	__u8 globalvarfilter;
	__u8 readoption;
	__u32 shuttervalue;
	__u64 pufdataaddr;
	__u64 pufidaddr;
	__u64 trimsyndataaddr;
};

/**
 * struct puf_helperdata - parameters for puf helper data.
 * @syndata: PUF syndrome data.
 * @chash: PUF chash.
 * @aux: PUF aux.
 */
struct puf_helperdata {
	__u32 syndata[PUF_MAX_SYNDROME_DATA_LEN_IN_WORDS];
	__u32 chash;
	__u32 aux;
};

/**
 * struct pufdata - parameters for puf data.
 * @pufhd: puf helper data of type struct puf_helperdata.
 * @pufid: PUF id.
 * @efusesyndata: PUF efuse syndrome data.
 */
struct pufdata {
	struct puf_helperdata pufhd;
	__u32 pufid[PUF_ID_LEN_IN_WORDS];
	__u32 efusesyndata[PUF_EFUSE_TRIM_SYN_DATA_IN_WORDS];
};

enum pufreadoption {
	PUF_READ_FROM_RAM = 0,
	PUF_READ_FROM_EFUSE_CACHE = 1
};

#define PUF_IOC_MAGIC 'P'

#define PUF_REGISTRATION _IOWR(PUF_IOC_MAGIC, 1, struct xpuf_usrparams *)
#define PUF_REGENERATION _IOWR(PUF_IOC_MAGIC, 2, struct xpuf_usrparams *)
#define PUF_REGEN_ID_ONLY _IOWR(PUF_IOC_MAGIC, 3, struct xpuf_usrparams *)
#define PUF_CLEAR_ID _IOWR(PUF_IOC_MAGIC, 4, struct puf_usrparams *)
#define PUF_CLEAR_KEY _IOWR(PUF_IOC_MAGIC, 5, struct puf_usrparams *)

#endif /* _PUF_UAPI_H_ */
