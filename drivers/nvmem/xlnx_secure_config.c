// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define AES_KEY_STRING_256_BYTES	64
#define AES_KEY_STRING_128_BYTES	32
#define AES_KEY_SIZE_256_BYTES		32
#define AES_KEY_SIZE_128_BYTES		16
#define EFUSE_IV_STRING_SIZE_BYTES	24
#define EFUSE_PPK_HASH_STRING_LEN_BYTES	64
#define EFUSE_ANLG_TRIM_SIZE_BYTES	8
#define EFUSE_BOOT_ENV_CTRL_SIZE_BYTES	8
#define EFUSE_MISC_CTRL_SIZE_BYTES	8
#define EFUSE_SECURITY_CTRL_SIZE_BYTES	8
#define XNVM_EFUSE_IV_LEN_IN_WORDS	3
#define XNVM_PUF_FORMATTED_SYN_DATA_LEN_IN_WORDS	127
#define XNVM_EFUSE_GLITCH_WR_LK_MASK			(0x80000000U)
#define EFUSE_MAXIMUM_STRING_LENGTH	1136

#define XNVM_EFUSE_BIT_ENABLE		1
#define XNVM_EFUSE_BIT_DISABLE		0

#define XNVM_EFUSE_AES_DISABLE_MASK		GENMASK(24, 24)
#define XNVM_EFUSE_JTAG_ERROROUT_DISABLE_MASK	GENMASK(25, 25)
#define XNVM_EFUSE_JTAG_DISABLE_MASK		GENMASK(26, 26)
#define XNVM_EFUSE_SECDBG_DISABLE_MASK		GENMASK(12, 11)
#define XNVM_EFUSE_SECLKDBG_DISABLE_MASK	GENMASK(14, 13)
#define XNVM_EFUSE_BOOTENVWRLK_DISABLE_MASK	GENMASK(4, 4)
#define XNVM_EFUSE_REGINIT_MASK			GENMASK(7, 6)
#define XNVM_EFUSE_PPK0_WRLK_MASK		GENMASK(30, 30)
#define XNVM_EFUSE_PPK1_WRLK_MASK		GENMASK(31, 31)
#define XNVM_EFUSE_PPK2_WRLK_MASK		GENMASK(16, 16)
#define XNVM_EFUSE_AES_CRCLK_MASK		GENMASK(18, 17)
#define XNVM_EFUSE_AES_WRLK_MASK		GENMASK(19, 19)
#define XNVM_EFUSE_USERKEY0_CRCLK_MASK		GENMASK(20, 20)
#define XNVM_EFUSE_USERKEY0_WRLK_MASK		GENMASK(21, 21)
#define XNVM_EFUSE_USERKEY1_CRCLK_MASK		GENMASK(22, 22)
#define XNVM_EFUSE_USERKEY1_WRLK_MASK		GENMASK(23, 23)
#define XNVM_EFUSE_HWTSTBITS_DISABLE_MASK	GENMASK(27, 27)
#define XNVM_EFUSE_PMCSC_ENABLE_MASK	(GENMASK(15, 15) | GENMASK(1, 0))

#define XNVM_EFUSE_GLITCHDET_HALTBOOT_ENABLE_MASK	GENMASK(7, 6)
#define XNVM_EFUSE_GLITCHDET_ROM_MONITOR_ENABLE_MASK	GENMASK(5, 5)
#define XNVM_EFUSE_HALTBOOT_ENABLE_MASK			GENMASK(14, 13)
#define XNVM_EFUSE_HALTBOOT_ENV_MASK			GENMASK(12, 11)
#define XNVM_EFUSE_CRYPTOKAT_ENABLE_MASK		GENMASK(19, 19)
#define XNVM_EFUSE_LBIST_ENABLE_MASK			GENMASK(22, 22)
#define XNVM_EFUSE_SAFTEY_MISSION_ENABLE_MASK		GENMASK(16, 16)
#define XNVM_EFUSE_PPK0_INVALID_MASK		GENMASK(27, 26)
#define XNVM_EFUSE_PPK1_INVALID_MASK		GENMASK(29, 28)
#define XNVM_EFUSE_PPK2_INVALID_MASK		GENMASK(31, 30)

#define XNVM_EFUSE_LPDMBIST_ENABLE_MASK	(GENMASK(19, 18) | GENMASK(20, 20))
#define XNVM_EFUSE_PMCMBIST_ENABLE_MASK	(GENMASK(1, 0) | GENMASK(15, 15))
#define XNVM_EFUSE_LPDNOCSC_ENABLE_MASK		GENMASK(30, 28)
#define XNVM_EFUSE_SYSMON_VOLTMON_ENABLE_MASK	GENMASK(27, 26)
#define XNVM_EFUSE_SYSMON_TEMPMON_ENABLE_MASK	GENMASK(25, 24)

#define BBRAM_ZEROIZE_OFFSET		0x4
#define BBRAM_KEY_OFFSET		0x10
#define BBRAM_USER_DATA_OFFSET		0x30
#define BBRAM_LOCK_DATA_OFFSET		0x48
#define AES_USER_KEY_0_OFFSET		0x110
#define AES_USER_KEY_1_OFFSET		0x130
#define AES_USER_KEY_2_OFFSET		0x150
#define AES_USER_KEY_3_OFFSET		0x170
#define AES_USER_KEY_4_OFFSET		0x190
#define AES_USER_KEY_5_OFFSET		0x1B0
#define AES_USER_KEY_6_OFFSET		0x1D0
#define AES_USER_KEY_7_OFFSET		0x1F0
#define EFUSE_MASK			GENMASK(16, 16)
#define EFUSE_OFFSET_MASK		0x1FFFF
#define ENV_DISABLE_MASK		GENMASK(17, 17)
#define EFUSE_CACHE_OFFSET_MASK		0x0FFF

#define EFUSE_PLM_IV_OFFSET		0x101DC
#define EFUSE_BLACK_IV_OFFSET		0x101D0
#define EFUSE_METAHEADER_IV_OFFSET	0x10180
#define EFUSE_DATA_PARTITION_IV_OFFSET	0x101E8
#define EFUSE_SECURITY_MISC_1_OFFSET	0x100E8
#define EFUSE_PUF_SYNDROME_DATA_OFFSET	0x10A04
#define EFUSE_PUF_CHASH_OFFSET		0x100A8
#define EFUSE_PUF_AUX_OFFSET		0x100A4
#define EFUSE_OFFCHIPID_0_OFFSET	0x10160
#define EFUSE_OFFCHIPID_7_OFFSET	0x1017C
#define EFUSE_REVOCATIONID_0_OFFSET	0x100B0
#define EFUSE_REVOCATIONID_7_OFFSET	0x100CC
#define EFUSE_USER_1_OFFSET		0x10204
#define EFUSE_USER_63_OFFSET		0x102FC
#define EFUSE_PUF_OFFSET		0x1FFFF
#define EFUSE_PPKHASH0_OFFSET		0x10100
#define EFUSE_PPKHASH1_OFFSET		0x10120
#define EFUSE_PPKHASH2_OFFSET		0x10140
#define EFUSE_ANLG_TRIM_3_OFFSET	0x10010
#define EFUSE_BOOT_ENV_CTRL_OFFSET	0x10094
#define EFUSE_MISC_CTRL_OFFSET		0x100A0
#define EFUSE_SECURITY_CONTROL_OFFSET	0x100AC
#define EFUSE_SECURITY_MISC_0_OFFSET	0x100E4

#define BBRAM_USER_DATA_SIZE		0x4
#define BBRAM_LOCK_DATA_SIZE		0x4
#define BBRAM_ZEROIZE_SIZE		0x4

#define XNVM_EFUSE_PPK_HASH_LEN_IN_WORDS 8
#define BBRAM_LOCK_DATA_VALUE		0x12345678
#define BBRAM_ZEROIZE_VALUE		0x87654321

#define PM_EFUSE_WRITE_IV_ACCESS_VERSAL			0xB18
#define PM_EFUSE_WRITE_MISC1_ACCESS_VERSAL		0xB19
#define PM_EFUSE_WRITE_OFFCHIP_ACCESS_VERSAL		0xB1B
#define PM_EFUSE_WRITE_REVOCATIONID_ACCESS_VERSAL	0xB1D
#define PM_EFUSE_WRITE_USER_ACCESS_VERSAL		0xB1C
#define PM_EFUSE_WRITE_PUF_ACCESS_VERSAL		0xB1A
#define PM_EFUSE_WRITE_PPK_ACCESS_VERSAL		0xB1E
#define PM_EFUSE_WRITE_ANLG_TRIM_ACCESS_VERSAL		0xB1F
#define PM_EFUSE_WRITE_BOOT_ENV_CTRL_ACCESS_VERSAL	0xB20
#define PM_EFUSE_WRITE_MISC_CTRL_ACCESS_VERSAL		0xB21
#define PM_EFUSE_WRITE_SECURITY_CTRL_ACCESS_VERSAL	0xB22
#define PM_EFUSE_WRITE_SECURITY_MISC0_ACCESS_VERSAL	0xB23

#define XNVM_EFUSE_SYSMONTEMP_ENABLE_MASK	GENMASK(13, 13)
#define XNVM_EFUSE_SYSMONVOLT_ENABLE_MASK	GENMASK(12, 12)
#define XNVM_EFUSE_SYSMONVOLTSOC_ENABLE_MASK	GENMASK(17, 17)
#define XNVM_EFUSE_SYSMONTEMP_HOT_MASK	GENMASK(10, 9)
#define XNVM_EFUSE_SYSMONTEMP_COLD_MASK	GENMASK(25, 24)
#define XNVM_EFUSE_SYSMONVOLTPMC_MASK	GENMASK(21, 20)
#define XNVM_EFUSE_SYSMONVOLTPSLP_MASK	GENMASK(19, 18)

#define XNVM_EFUSE_SYSMONTEMP_SHIFT_VALUE	13
#define XNVM_EFUSE_SYSMONVOLT_SHIFT_VALUE	12
#define XNVM_EFUSE_SYSMONVOLTSOC_SHIFT_VALUE	17
#define XNVM_EFUSE_SYSMONTEMP_HOT_SHIFT_VALUE	9
#define XNVM_EFUSE_SYSMONTEMP_COLD_SHIFT_VALUE	24
#define XNVM_EFUSE_SYSMONVOLTPMC_SHIFT_VALUE	20
#define XNVM_EFUSE_SYSMONVOLTPSLP_SHIFT_VALUE	18

#define EFUSE_SECURITY_MISC1_SIZE_BYTES	0x8
#define EFUSE_OFFCHIP_ID_SIZE_BYTES	0x8
#define EFUSE_USER_SIZE_BYTES		0x8
#define EFUSE_PUF_DATA_SIZE_BYTES	0x470
#define NVMEM_SIZE			0x50000

enum aes_keysrc {
	AES_USER_KEY_0 = 12,
	AES_USER_KEY_1 = 13,
	AES_USER_KEY_2 = 14,
	AES_USER_KEY_3 = 15,
	AES_USER_KEY_4 = 16,
	AES_USER_KEY_5 = 17,
	AES_USER_KEY_6 = 18,
	AES_USER_KEY_7 = 19
};

enum aes_keysize {
	AES_KEY_SIZE_128 = 0,
	AES_KEY_SIZE_256 = 2
};

struct xilinx_efuse_user {
	u32 startuserfusenum;
	u32 numofuserfuses;
	u32 *userfusedata;
};

struct xilinx_efuse_iv {
	u8 prgmmetaheaderiv;
	u8 prgmblkobfusiv;
	u8 prgmplmiv;
	u8 prgmdatapartitioniv;
	u32 metaheaderiv[XNVM_EFUSE_IV_LEN_IN_WORDS];
	u32 blkobfusiv[XNVM_EFUSE_IV_LEN_IN_WORDS];
	u32 plmiv[XNVM_EFUSE_IV_LEN_IN_WORDS];
	u32 datapartitioniv[XNVM_EFUSE_IV_LEN_IN_WORDS];
};

struct xilinx_efuse_puf {
	u32 chash;
	u32 aux;
	u32 efusesyndata[XNVM_PUF_FORMATTED_SYN_DATA_LEN_IN_WORDS];
};

struct xilinx_efuse_deconly {
	u8 prgmdeconly;
};

struct xilinx_efuse_secmisc1bits {
	u8 lpdmbisten;
	u8 pmcmbisten;
	u8 lpdnocscen;
	u8 sysmonvoltmonen;
	u8 sysmontempmonen;
};

struct xilinx_efuse_ids {
	u32 prgmid;
	u32 id[8];
};

struct xilinx_efuse_ppkhash {
	u8 prgmppk0hash;
	u8 prgmppk1hash;
	u8 prgmppk2hash;
	u32 ppk0hash[XNVM_EFUSE_PPK_HASH_LEN_IN_WORDS];
	u32 ppk1hash[XNVM_EFUSE_PPK_HASH_LEN_IN_WORDS];
	u32 ppk2hash[XNVM_EFUSE_PPK_HASH_LEN_IN_WORDS];
};

struct xilinx_efuse_miscctrlbits {
	u8 glitchdethaltbooten;
	u8 glitchdetrommonitoren;
	u8 haltbooterror;
	u8 haltbootenv;
	u8 cryptokaten;
	u8 lbisten;
	u8 safetymissionen;
	u8 ppk0invalid;
	u8 ppk1invalid;
	u8 ppk2invalid;
};

struct xilinx_efuse_bootenvctrlbits {
	u8 prgmsysmontemphot;
	u8 prgmsysmonvoltpmc;
	u8 prgmsysmonvoltpslp;
	u8 prgmsysmontempcold;
	u8 sysmontempen;
	u8 sysmonvolten;
	u8 sysmonvoltsoc;
	u8 sysmontemphot;
	u8 sysmonvoltpmc;
	u8 sysmonvoltpslp;
	u8 sysmontempcold;
};

struct xilinx_efuse_glitchcfgbits {
	u8 prgmglitch;
	u8 glitchdetwrlk;
	u32 glitchdettrim;
	u8 gdrommonitoren;
	u8 gdhaltbooten;
};

struct xilinx_efuse_secctrlbits {
	u8 aesdis;
	u8 jtagerroutdis;
	u8 jtagdis;
	u8 hwtstbitsdis;
	u8 ppk0wrlk;
	u8 ppk1wrlk;
	u8 ppk2wrlk;
	u8 aescrclk;
	u8 aeswrlk;
	u8 userkey0crclk;
	u8 userkey0wrlk;
	u8 userkey1crclk;
	u8 userkey1wrlk;
	u8 secdbgdis;
	u8 seclockdbgdis;
	u8 pmcscen;
	u8 bootenvwrlk;
	u8 reginitdis;
};

static u_int32_t convert_char_to_nibble(char in_char, unsigned char *num)
{
	if ((in_char >= '0') && (in_char <= '9'))
		*num = in_char - '0';
	else if ((in_char >= 'a') && (in_char <= 'f'))
		*num = in_char - 'a' + 10;
	else if ((in_char >= 'A') && (in_char <= 'F'))
		*num = in_char - 'A' + 10;
	else
		return -EINVAL;

	return 0;
}

static u_int32_t convert_string_to_hex_be(const char *str, unsigned char *buf, u_int32_t len)
{
	u32 converted_len;
	unsigned char lower_nibble = 0;
	unsigned char upper_nibble = 0;
	u32 str_length = strnlen(str, EFUSE_MAXIMUM_STRING_LENGTH);

	if (!str || !buf)
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	if ((str_length / 2) > len)
		return -EINVAL;

	converted_len = 0;
	while (converted_len < str_length) {
		if (convert_char_to_nibble(str[converted_len], &upper_nibble) == 0) {
			if (convert_char_to_nibble(str[converted_len + 1], &lower_nibble) == 0)
				buf[converted_len / 2] = (upper_nibble << 4) | lower_nibble;
			else
				return -EINVAL;
		} else {
			return -EINVAL;
		}

		converted_len += 2;
	}

	return 0;
}

static u_int32_t convert_string_to_hex_le(const char *str, unsigned char *buf, u_int32_t len)
{
	u32 converted_len;
	unsigned char lower_nibble = 0;
	unsigned char upper_nibble = 0;
	u32 str_index;
	u32 str_length = strnlen(str, EFUSE_MAXIMUM_STRING_LENGTH);

	if (!str || !buf)
		return -EINVAL;

	if (len == 0)
		return -EINVAL;

	if (str_length != len)
		return -EINVAL;

	str_index = len / 2 - 1;
	converted_len = 0;

	while (converted_len < (len)) {
		if (convert_char_to_nibble(str[converted_len], &upper_nibble) == 0) {
			if (convert_char_to_nibble(str[converted_len + 1], &lower_nibble) == 0) {
				buf[str_index] = (upper_nibble << 4) | lower_nibble;
				str_index = str_index - 1;
			} else {
				return -EINVAL;
			}
		} else {
			return -EINVAL;
		}

		converted_len += 2;
	}

	return 0;
}

static int sec_cfg_read(void *context, unsigned int offset, void *val,
			size_t bytes)
{
	struct device *dev = context;
	dma_addr_t dma_addr;
	int *data, ret;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!(offset & EFUSE_MASK)) {
		if (offset != BBRAM_USER_DATA_OFFSET && bytes != BBRAM_USER_DATA_SIZE)
			return -EOPNOTSUPP;

		ret = zynqmp_pm_bbram_read_usrdata(dma_addr);
		if (!ret)
			memcpy(val, data, bytes);
	} else {
		offset = offset & EFUSE_CACHE_OFFSET_MASK;
		ret = versal_pm_efuse_read(dma_addr, offset, bytes);
		if (!ret)
			memcpy(val, data, bytes);
	}

	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_puf_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_puf *pufdata;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_PUF_DATA_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		pufdata = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_puf),
					     &dma_buff, GFP_KERNEL);
		if (!pufdata) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		memcpy(pufdata, data, bytes / 2);
		ret = versal_pm_efuse_write(dma_buff, PM_EFUSE_WRITE_PUF_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_puf),
				  pufdata, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_security_misc1_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_secmisc1bits *misc1bits;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_SECURITY_MISC1_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		misc1bits = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_secmisc1bits),
					       &dma_buff, GFP_KERNEL);
		if (!misc1bits) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		misc1bits->lpdmbisten = ((*data) & XNVM_EFUSE_LPDMBIST_ENABLE_MASK) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		misc1bits->pmcmbisten = ((*data) & XNVM_EFUSE_PMCMBIST_ENABLE_MASK) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		misc1bits->lpdnocscen = ((*data) & XNVM_EFUSE_LPDNOCSC_ENABLE_MASK) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		misc1bits->sysmonvoltmonen = ((*data) &
					      XNVM_EFUSE_SYSMON_VOLTMON_ENABLE_MASK) == 0 ?
					      XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		misc1bits->sysmontempmonen = ((*data) &
					      XNVM_EFUSE_SYSMON_TEMPMON_ENABLE_MASK) == 0 ?
					      XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		ret = versal_pm_efuse_write(dma_buff, PM_EFUSE_WRITE_MISC1_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_secmisc1bits),
				  misc1bits, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_id_write(void *context, void *val, size_t bytes,
				  unsigned int offset, u8 envdis)
{
	struct xilinx_efuse_ids *offchipids;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_OFFCHIP_ID_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		offchipids = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_ids),
						&dma_buff, GFP_KERNEL);
		if (!offchipids) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}
		offchipids->prgmid = 1U;
		if (offset >= EFUSE_OFFCHIPID_0_OFFSET && offset <= EFUSE_OFFCHIPID_7_OFFSET) {
			offchipids->id[(offset - EFUSE_OFFCHIPID_0_OFFSET) / 4] = *data;
			ret = versal_pm_efuse_write(dma_buff,
						    PM_EFUSE_WRITE_OFFCHIP_ACCESS_VERSAL, envdis);
		} else {
			offchipids->id[(offset - EFUSE_REVOCATIONID_0_OFFSET) / 4] = *data;
			ret = versal_pm_efuse_write(dma_buff,
						    PM_EFUSE_WRITE_REVOCATIONID_ACCESS_VERSAL,
						    envdis);
		}
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_ids),
				  offchipids, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_userdata_write(void *context, void *val, size_t bytes,
					unsigned int offset, u8 envdis)
{
	struct xilinx_efuse_user *userdata = NULL;
	struct device *dev = context;
	dma_addr_t dma_buff = 0, dma_buff2, dma_addr;
	u8 *data, *userbuf;
	int ret;

	if (bytes != EFUSE_USER_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		userdata = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_user),
					      &dma_buff, GFP_KERNEL);
		if (!userdata) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}
		userdata->startuserfusenum = ((offset - EFUSE_USER_1_OFFSET) / 4) + 1;
		userdata->numofuserfuses = 1;
		userbuf = dma_alloc_coherent(dev, userdata->numofuserfuses * sizeof(u32),
					     &dma_buff2, GFP_KERNEL);
		if (!userbuf) {
			ret = -ENOMEM;
			goto buf_alloc_fail;
		}
		userdata->userfusedata = (u32 *)dma_buff2;
		memcpy(userbuf, data, bytes);
		ret = versal_pm_efuse_write(dma_buff, PM_EFUSE_WRITE_USER_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, userdata->numofuserfuses * sizeof(u32),
				  userbuf, dma_buff2);
	}

buf_alloc_fail:
	dma_free_coherent(dev, sizeof(struct xilinx_efuse_user),
			  userdata, dma_buff);
dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_ppkhash_write(void *context, void *val, size_t bytes,
				       unsigned int offset, u8 envdis)
{
	struct xilinx_efuse_ppkhash *ppkhash = NULL;
	struct device *dev = context;
	dma_addr_t dma_buff = 0, dma_addr;
	u8 *data;
	int ret;

	if (bytes != EFUSE_PPK_HASH_STRING_LEN_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		ppkhash = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_ppkhash),
					     &dma_buff, GFP_KERNEL);
		if (!ppkhash) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}
		if (offset == EFUSE_PPKHASH0_OFFSET) {
			ppkhash->prgmppk0hash = 1;
			memcpy(ppkhash->ppk0hash, data, bytes);
		} else if (offset == EFUSE_PPKHASH1_OFFSET) {
			ppkhash->prgmppk1hash = 1;
			memcpy(ppkhash->ppk1hash, data, bytes);
		} else if (offset == EFUSE_PPKHASH2_OFFSET) {
			ppkhash->prgmppk2hash = 1;
			memcpy(ppkhash->ppk2hash, data, bytes);
		} else {
			ret = -EINVAL;
			goto efuse_write_fail;
		}
		ret = versal_pm_efuse_write(dma_buff, PM_EFUSE_WRITE_PPK_ACCESS_VERSAL, envdis);
	}

efuse_write_fail:
	dma_free_coherent(dev, sizeof(struct xilinx_efuse_ppkhash),
			  ppkhash, dma_buff);
dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_anlg_trim3_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_glitchcfgbits *glitchcfg;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_ANLG_TRIM_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_le((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		glitchcfg = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_glitchcfgbits),
					       &dma_buff, GFP_KERNEL);
		if (!glitchcfg) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		glitchcfg->glitchdettrim = (*data) & (~XNVM_EFUSE_GLITCH_WR_LK_MASK);
		glitchcfg->glitchdetwrlk = ((*data) & (XNVM_EFUSE_GLITCH_WR_LK_MASK)) == 0 ?
					    XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		glitchcfg->prgmglitch = 1;
		ret = versal_pm_efuse_write(dma_buff,
					    PM_EFUSE_WRITE_ANLG_TRIM_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_glitchcfgbits),
				  glitchcfg, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_iv_write(void *context, void *val, size_t bytes,
				  unsigned int offset, u8 envdis)
{
	struct xilinx_efuse_iv *ivs = NULL;
	struct device *dev = context;
	dma_addr_t dma_buff = 0, dma_addr;
	u8 *data;
	int ret;

	if (bytes != (EFUSE_IV_STRING_SIZE_BYTES))
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes / 2, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		ivs =	dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_iv),
					   &dma_buff, GFP_KERNEL);
		if (!ivs) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		if (offset == EFUSE_PLM_IV_OFFSET) {
			ivs->prgmplmiv = 1;
			memcpy(ivs->plmiv, data, bytes);
		} else if (offset == EFUSE_BLACK_IV_OFFSET) {
			ivs->prgmblkobfusiv = 1;
			memcpy(ivs->blkobfusiv, data, bytes);
		} else if (offset == EFUSE_METAHEADER_IV_OFFSET) {
			ivs->prgmmetaheaderiv = 1;
			memcpy(ivs->metaheaderiv, data, bytes);
		} else if (offset == EFUSE_DATA_PARTITION_IV_OFFSET) {
			ivs->prgmdatapartitioniv = 1;
			memcpy(ivs->datapartitioniv, data, bytes);
		} else {
			ret = -EINVAL;
			goto efuse_write_fail;
		}
		ret = versal_pm_efuse_write(dma_buff, PM_EFUSE_WRITE_IV_ACCESS_VERSAL, envdis);
	}

efuse_write_fail:
	dma_free_coherent(dev, sizeof(struct xilinx_efuse_iv),
			  ivs, dma_buff);
dma_alloc_fail:
	dma_free_coherent(dev, bytes / 2, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_security_control_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_secctrlbits *secctrlbits;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_SECURITY_CTRL_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		secctrlbits = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_secctrlbits),
						 &dma_buff, GFP_KERNEL);
		if (!secctrlbits) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		secctrlbits->aesdis = ((*data) & (XNVM_EFUSE_AES_DISABLE_MASK)) == 0 ?
				       XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->jtagerroutdis = ((*data) &
					      (XNVM_EFUSE_JTAG_ERROROUT_DISABLE_MASK)) == 0 ?
					       XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->jtagdis = ((*data) & (XNVM_EFUSE_JTAG_DISABLE_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->secdbgdis = ((*data) & (XNVM_EFUSE_SECDBG_DISABLE_MASK)) == 0 ?
					   XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->seclockdbgdis = ((*data) & (XNVM_EFUSE_SECLKDBG_DISABLE_MASK)) == 0 ?
					       XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->bootenvwrlk = ((*data) & (XNVM_EFUSE_BOOTENVWRLK_DISABLE_MASK)) == 0 ?
					     XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->reginitdis = ((*data) & (XNVM_EFUSE_REGINIT_MASK)) == 0 ?
					    XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->ppk0wrlk = ((*data) & (XNVM_EFUSE_PPK0_WRLK_MASK)) == 0 ?
					  XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->ppk1wrlk = ((*data) & (XNVM_EFUSE_PPK1_WRLK_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->ppk2wrlk = ((*data) & (XNVM_EFUSE_PPK2_WRLK_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->aescrclk = ((*data) & (XNVM_EFUSE_AES_CRCLK_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->aeswrlk = ((*data) & (XNVM_EFUSE_AES_WRLK_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->userkey0crclk = ((*data) & (XNVM_EFUSE_USERKEY0_CRCLK_MASK)) == 0 ?
					       XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->userkey0wrlk = ((*data) & (XNVM_EFUSE_USERKEY0_WRLK_MASK)) == 0 ?
					      XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->userkey1crclk = ((*data) & (XNVM_EFUSE_USERKEY1_CRCLK_MASK)) == 0 ?
						XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->userkey1wrlk = ((*data) & (XNVM_EFUSE_USERKEY1_WRLK_MASK)) == 0 ?
					      XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->hwtstbitsdis = ((*data) & (XNVM_EFUSE_HWTSTBITS_DISABLE_MASK)) == 0 ?
					      XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		secctrlbits->pmcscen = ((*data) & (XNVM_EFUSE_PMCSC_ENABLE_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		ret = versal_pm_efuse_write(dma_buff,
					    PM_EFUSE_WRITE_SECURITY_CTRL_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_secctrlbits),
				  secctrlbits, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_misc_ctrl_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_miscctrlbits *miscctrl;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_MISC_CTRL_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		miscctrl = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_miscctrlbits),
					      &dma_buff, GFP_KERNEL);
		if (!miscctrl) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		miscctrl->glitchdethaltbooten = ((*data) &
						(XNVM_EFUSE_GLITCHDET_HALTBOOT_ENABLE_MASK)) == 0 ?
						 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->glitchdetrommonitoren = ((*data) &
						   (XNVM_EFUSE_GLITCHDET_ROM_MONITOR_ENABLE_MASK))
						    == 0 ? XNVM_EFUSE_BIT_DISABLE :
						    XNVM_EFUSE_BIT_ENABLE;
		miscctrl->haltbooterror = ((*data) & (XNVM_EFUSE_HALTBOOT_ENABLE_MASK)) == 0 ?
					   XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->haltbootenv = ((*data) & (XNVM_EFUSE_HALTBOOT_ENV_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->cryptokaten = ((*data) & (XNVM_EFUSE_CRYPTOKAT_ENABLE_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->lbisten = ((*data) & (XNVM_EFUSE_LBIST_ENABLE_MASK)) == 0 ?
				     XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->safetymissionen = ((*data) &
					     (XNVM_EFUSE_SAFTEY_MISSION_ENABLE_MASK)) == 0 ?
					     XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->ppk0invalid = ((*data) & (XNVM_EFUSE_PPK0_INVALID_MASK)) == 0 ?
					XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->ppk1invalid = ((*data) & (XNVM_EFUSE_PPK1_INVALID_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		miscctrl->ppk2invalid = ((*data) & (XNVM_EFUSE_PPK2_INVALID_MASK)) == 0 ?
					 XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		ret = versal_pm_efuse_write(dma_buff,
					    PM_EFUSE_WRITE_MISC_CTRL_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_miscctrlbits),
				  miscctrl, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_security_misc0_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_deconly *deconly;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_SECURITY_CTRL_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		deconly = dma_alloc_coherent(dev, sizeof(struct xilinx_efuse_deconly),
					     &dma_buff, GFP_KERNEL);
		if (!deconly) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		if ((*data) != 0)
			deconly->prgmdeconly = 1;
		ret = versal_pm_efuse_write(dma_buff,
					    PM_EFUSE_WRITE_SECURITY_MISC0_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_deconly),
				  deconly, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static int sec_cfg_efuse_boot_env_ctrl_write(void *context, void *val, size_t bytes, u8 envdis)
{
	struct xilinx_efuse_bootenvctrlbits *bootenvctrlbits = NULL;
	struct device *dev = context;
	dma_addr_t dma_buff, dma_addr;
	u32 *data;
	int ret;

	if (bytes != EFUSE_BOOT_ENV_CTRL_SIZE_BYTES)
		return -EINVAL;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	ret = convert_string_to_hex_be((const char *)val, (u8 *)data, bytes);
	if (!ret) {
		bootenvctrlbits = dma_alloc_coherent(dev,
						     sizeof(struct xilinx_efuse_bootenvctrlbits),
						     &dma_buff, GFP_KERNEL);
		if (!bootenvctrlbits) {
			ret = -ENOMEM;
			goto dma_alloc_fail;
		}

		bootenvctrlbits->sysmontempen = ((*data) & XNVM_EFUSE_SYSMONTEMP_ENABLE_MASK) >>
						 XNVM_EFUSE_SYSMONTEMP_SHIFT_VALUE;
		bootenvctrlbits->sysmonvolten = ((*data) & XNVM_EFUSE_SYSMONVOLT_ENABLE_MASK) >>
						 XNVM_EFUSE_SYSMONVOLT_SHIFT_VALUE;
		bootenvctrlbits->sysmonvoltsoc = ((*data) & XNVM_EFUSE_SYSMONVOLTSOC_ENABLE_MASK) >>
						 XNVM_EFUSE_SYSMONVOLTSOC_SHIFT_VALUE;
		bootenvctrlbits->sysmontemphot = ((*data) & XNVM_EFUSE_SYSMONTEMP_HOT_MASK) >>
						 XNVM_EFUSE_SYSMONTEMP_HOT_SHIFT_VALUE;
		bootenvctrlbits->sysmonvoltpmc = ((*data) & XNVM_EFUSE_SYSMONVOLTPMC_MASK) >>
						  XNVM_EFUSE_SYSMONVOLTPMC_SHIFT_VALUE;
		bootenvctrlbits->sysmonvoltpslp = ((*data) & XNVM_EFUSE_SYSMONVOLTPSLP_MASK) >>
						   XNVM_EFUSE_SYSMONVOLTPSLP_SHIFT_VALUE;
		bootenvctrlbits->sysmontempcold = ((*data) & XNVM_EFUSE_SYSMONTEMP_COLD_MASK) >>
						   XNVM_EFUSE_SYSMONTEMP_COLD_SHIFT_VALUE;

		if (bootenvctrlbits->sysmontemphot)
			bootenvctrlbits->prgmsysmontemphot = XNVM_EFUSE_BIT_ENABLE;
		if (bootenvctrlbits->sysmontempcold)
			bootenvctrlbits->prgmsysmontempcold = XNVM_EFUSE_BIT_ENABLE;
		if (bootenvctrlbits->sysmonvoltpslp)
			bootenvctrlbits->prgmsysmonvoltpslp = XNVM_EFUSE_BIT_ENABLE;
		if (bootenvctrlbits->sysmonvoltpmc)
			bootenvctrlbits->prgmsysmonvoltpmc = XNVM_EFUSE_BIT_ENABLE;

		ret = versal_pm_efuse_write(dma_buff,
					    PM_EFUSE_WRITE_BOOT_ENV_CTRL_ACCESS_VERSAL, envdis);
		dma_free_coherent(dev, sizeof(struct xilinx_efuse_bootenvctrlbits),
				  bootenvctrlbits, dma_buff);
	}

dma_alloc_fail:
	dma_free_coherent(dev, bytes, data, dma_addr);

	return ret;
}

static int sec_cfg_bbram_write(void *context, void *val, size_t bytes, unsigned int offset)
{
	struct device *dev = context;
	dma_addr_t dma_addr;
	u8 *data;
	int ret;

	data = dma_alloc_coherent(dev, bytes, &dma_addr, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	switch (offset) {
	case AES_USER_KEY_0_OFFSET:
	case AES_USER_KEY_1_OFFSET:
	case AES_USER_KEY_2_OFFSET:
	case AES_USER_KEY_3_OFFSET:
	case AES_USER_KEY_4_OFFSET:
	case AES_USER_KEY_5_OFFSET:
	case AES_USER_KEY_6_OFFSET:
	case AES_USER_KEY_7_OFFSET:
		if (bytes != AES_KEY_STRING_128_BYTES && bytes != AES_KEY_STRING_256_BYTES) {
			ret = -EINVAL;
		} else {
			ret = convert_string_to_hex_be((const char *)val, data, bytes);
			if (!ret) {
				u32 keylen, keysrc;

				keylen = (bytes == AES_KEY_STRING_128_BYTES) ?
					 AES_KEY_SIZE_128 : AES_KEY_SIZE_256;
				keysrc = ((offset - AES_USER_KEY_0_OFFSET) / 0x20) + 12;
				ret = versal_pm_aes_key_write(keylen, keysrc, dma_addr);
			}
		}
		break;
	case BBRAM_KEY_OFFSET:
		if (bytes != AES_KEY_STRING_256_BYTES) {
			ret = -EINVAL;
		} else {
			ret = convert_string_to_hex_le((const char *)val, data, bytes);
			if (!ret)
				ret = zynqmp_pm_bbram_write_aeskey(bytes, dma_addr);
		}
		break;
	case BBRAM_USER_DATA_OFFSET:
		if (bytes != BBRAM_USER_DATA_SIZE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_write_usrdata(*(u32 *)val);
		break;
	case BBRAM_LOCK_DATA_OFFSET:
		if (bytes != BBRAM_USER_DATA_SIZE || *(u32 *)val != BBRAM_LOCK_DATA_VALUE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_lock_userdata();
		break;
	case BBRAM_ZEROIZE_OFFSET:
		if (bytes != BBRAM_ZEROIZE_SIZE || *(u32 *)val != BBRAM_ZEROIZE_VALUE)
			ret = -EINVAL;
		else
			ret = zynqmp_pm_bbram_zeroize();
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int sec_cfg_efuse_write(void *context, void *val, size_t bytes,
			       unsigned int offset, u8 envdis)
{
	int ret;

	switch (offset) {
	case EFUSE_PLM_IV_OFFSET:
	case EFUSE_BLACK_IV_OFFSET:
	case EFUSE_METAHEADER_IV_OFFSET:
	case EFUSE_DATA_PARTITION_IV_OFFSET:
		ret = sec_cfg_efuse_iv_write(context, val, bytes, offset, envdis);
		break;
	case EFUSE_SECURITY_MISC_1_OFFSET:
		ret = sec_cfg_efuse_security_misc1_write(context, val, bytes, envdis);
		break;
	case EFUSE_OFFCHIPID_0_OFFSET...EFUSE_OFFCHIPID_7_OFFSET:
	case EFUSE_REVOCATIONID_0_OFFSET...EFUSE_REVOCATIONID_7_OFFSET:
		ret = sec_cfg_efuse_id_write(context, val, bytes, offset, envdis);
		break;
	case EFUSE_USER_1_OFFSET...EFUSE_USER_63_OFFSET:
		ret = sec_cfg_efuse_userdata_write(context, val, bytes, offset, envdis);
		break;
	case EFUSE_PUF_OFFSET:
		ret = sec_cfg_efuse_puf_write(context, val, bytes, envdis);
		break;
	case EFUSE_PPKHASH0_OFFSET:
	case EFUSE_PPKHASH1_OFFSET:
	case EFUSE_PPKHASH2_OFFSET:
		ret = sec_cfg_efuse_ppkhash_write(context, val, bytes, offset, envdis);
		break;
	case EFUSE_ANLG_TRIM_3_OFFSET:
		ret = sec_cfg_efuse_anlg_trim3_write(context, val, bytes, envdis);
		break;
	case EFUSE_BOOT_ENV_CTRL_OFFSET:
		ret = sec_cfg_efuse_boot_env_ctrl_write(context, val, bytes, envdis);
		break;
	case EFUSE_MISC_CTRL_OFFSET:
		ret = sec_cfg_efuse_misc_ctrl_write(context, val, bytes, envdis);
		break;
	case EFUSE_SECURITY_CONTROL_OFFSET:
		ret = sec_cfg_efuse_security_control_write(context, val, bytes, envdis);
		break;
	case EFUSE_SECURITY_MISC_0_OFFSET:
		ret = sec_cfg_efuse_security_misc0_write(context, val, bytes, envdis);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int sec_cfg_write(void *context, unsigned int offset, void *val, size_t bytes)
{
	int ret;
	u8 envdis;

	if (offset & EFUSE_MASK) {
		envdis = (offset & ENV_DISABLE_MASK) == 0 ?
			  XNVM_EFUSE_BIT_DISABLE : XNVM_EFUSE_BIT_ENABLE;
		offset = offset & EFUSE_OFFSET_MASK;
		ret = sec_cfg_efuse_write(context, val, bytes, offset, envdis);
	} else {
		ret = sec_cfg_bbram_write(context, val, bytes, offset);
	}

	return ret;
}

static struct nvmem_config secureconfig = {
	.name = "xilinx-secure-config",
	.owner = THIS_MODULE,
	.word_size = 1,
	.size = NVMEM_SIZE,
};

static const struct of_device_id sec_cfg_match[] = {
	{ .compatible = "xlnx,versal-sec-cfg", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sec_cfg_match);

static int sec_cfg_probe(struct platform_device *pdev)
{
	struct nvmem_device *nvmem;

	secureconfig.dev = &pdev->dev;
	secureconfig.priv = &pdev->dev;
	secureconfig.reg_read = sec_cfg_read;
	secureconfig.reg_write = sec_cfg_write;

	nvmem = nvmem_register(&secureconfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	dev_dbg(&pdev->dev, "Successfully registered driver to nvmem framework");

	return 0;
}

static struct platform_driver secure_config_driver = {
	.probe = sec_cfg_probe,
	.driver = {
		.name = "xilinx-secure-config",
		.of_match_table = sec_cfg_match,
	},
};

module_platform_driver(secure_config_driver);

MODULE_AUTHOR("Harsha <harsha.harsha@xilinx.com>");
MODULE_DESCRIPTION("Versal Secure Configuration driver");
MODULE_LICENSE("GPL v2");
