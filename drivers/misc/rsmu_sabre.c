// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver is developed for the IDT 82P33XXX series of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/mfd/idt82p33_reg.h>
#include <linux/mfd/rsmu.h>
#include <asm/unaligned.h>

#include "rsmu_cdev.h"

#define FW_FILENAME	"rsmu82p33xxx.bin"

static u8 dpll_operating_mode_cnfg_prev[2] = {0xff, 0xff};

static int check_and_set_masks(struct rsmu_cdev *rsmu, u8 page, u8 offset, u8 val)
{
	int err = 0;

	return err;
}

static int reg_readwrite(struct rsmu_cdev *rsmu, u16 offset, u8 *val8, u8 write)
{
	int err;

	if (write)
		err = regmap_bulk_write(rsmu->regmap, offset, val8, sizeof(u8));
	else
		err = regmap_bulk_read(rsmu->regmap, offset, val8, sizeof(u8));

	return err;
}

static int reg_rmw(struct rsmu_cdev *rsmu, u16 offset, u8 mask, u8 lsb, u8 *val8, u8 *prev8)
{
	u8 tmp_val8;
	int err;

	err = reg_readwrite(rsmu, offset, &tmp_val8, 0);
	if (err)
		return err;

	if (prev8)
		*prev8 = tmp_val8;

	rsmu_set_bitfield(tmp_val8, mask, lsb, *val8);

	return reg_readwrite(rsmu, offset, &tmp_val8, 1);
}


static int reg_dpll_operating_mode_cnfg_offset(u8 dpll, u16 *offset)
{
	switch (dpll) {
	case 0:
		*offset = DPLL1_OPERATING_MODE_CNFG;
		break;
	case 1:
		*offset = DPLL2_OPERATING_MODE_CNFG;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rmw_reg_dpll_operating_mode_cnfg(struct rsmu_cdev *rsmu, u8 dpll, u8 mask, u8 lsb, u8 val8, u8 *prev8)
{
	u16 offset;
	int err;

	err = reg_dpll_operating_mode_cnfg_offset(dpll, &offset);
	if (err)
		return err;

	return reg_rmw(rsmu, offset, mask, lsb, &val8, prev8);
}

static int reg_dpll_holdover_mode_cnfg_msb_offset(u8 dpll, u16 *offset)
{
	switch (dpll) {
	case 0:
		*offset = DPLL1_HOLDOVER_MODE_CNFG_MSB;
		break;
	case 1:
		*offset = DPLL2_HOLDOVER_MODE_CNFG_MSB;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rmw_reg_dpll_holdover_mode_cnfg_msb(struct rsmu_cdev *rsmu, u8 dpll, u8 mask, u8 lsb, u8 val8, u8 *prev8)
{
	u16 offset;
	int err;

	err = reg_dpll_holdover_mode_cnfg_msb_offset(dpll, &offset);
	if (err)
		return err;

	return reg_rmw(rsmu, offset, mask, lsb, &val8, prev8);
}

static int set_dpll_oper_mode(struct rsmu_cdev *rsmu, u8 dpll, enum pll_mode mode, bool save_prev)
{
	if (save_prev)
		return rmw_reg_dpll_operating_mode_cnfg(rsmu, dpll, 0x1f, 0, mode, &dpll_operating_mode_cnfg_prev[dpll]);
	else
		return rmw_reg_dpll_operating_mode_cnfg(rsmu, dpll, 0x1f, 0, mode, NULL);
}

static int set_manual_holdover_mode(struct rsmu_cdev *rsmu, u8 dpll,
				    enum holdover_mode mode)
{
	/* Set dpll{1:2}_man_holdover Bit 7 */
	return rmw_reg_dpll_holdover_mode_cnfg_msb(rsmu, dpll, 0x80, 7,
						   mode, NULL);
}

static int rsmu_sabre_set_combomode(struct rsmu_cdev *rsmu, u8 dpll, u8 mode)
{
	u16 dpll_ctrl_n;
	u8 cfg;
	int err;

	switch (dpll) {
	case 0:
		dpll_ctrl_n = DPLL1_OPERATING_MODE_CNFG;
		break;
	case 1:
		dpll_ctrl_n = DPLL2_OPERATING_MODE_CNFG;
		break;
	default:
		return -EINVAL;
	}

	if (mode >= E_COMBOMODE_MAX)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, dpll_ctrl_n, &cfg, sizeof(cfg));
	if (err)
		return err;

	cfg &= ~(COMBO_MODE_MASK << COMBO_MODE_SHIFT);
	cfg |= mode << COMBO_MODE_SHIFT;

	return regmap_bulk_write(rsmu->regmap, dpll_ctrl_n, &cfg, sizeof(cfg));
}

static int rsmu_sabre_get_dpll_state(struct rsmu_cdev *rsmu, u8 dpll, u8 *state)
{
	u16 dpll_sts_n;
	u8 cfg;
	int err;

	switch (dpll) {
	case 0:
		dpll_sts_n = DPLL1_OPERATING_STS;
		break;
	case 1:
		dpll_sts_n = DPLL2_OPERATING_STS;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, dpll_sts_n, &cfg, sizeof(cfg));
	if (err)
		return err;

	switch (cfg & OPERATING_STS_MASK) {
	case DPLL_STATE_FREERUN:
		*state = E_SRVLOUNQUALIFIEDSTATE;
		break;
	case DPLL_STATE_PRELOCKED2:
	case DPLL_STATE_PRELOCKED:
		*state = E_SRVLOLOCKACQSTATE;
		break;
	case DPLL_STATE_LOCKED:
		*state = E_SRVLOTIMELOCKEDSTATE;
		break;
	case DPLL_STATE_HOLDOVER:
		*state = E_SRVLOHOLDOVERINSPECSTATE;
		break;
	default:
		*state = E_SRVLOSTATEINVALID;
		break;
	}

	return 0;
}

static int rsmu_sabre_get_dpll_ffo(struct rsmu_cdev *rsmu, u8 dpll,
				   struct rsmu_get_ffo *ffo)
{
	u8 buf[8] = {0};
	s64 fcw = 0;
	u16 dpll_freq_n;
	int err;

	/*
	 * IDTDpll_GetCurrentDpllFreqOffset retrieves the FFO integrator only.
	 * In order to get Proportional + Integrator, use the holdover FFO with
	 * the filter bandwidth 0.5 Hz set by TCS file.
	 */
	switch (dpll) {
	case 0:
		dpll_freq_n = DPLL1_HOLDOVER_FREQ_CNFG;
		break;
	case 1:
		dpll_freq_n = DPLL2_HOLDOVER_FREQ_CNFG;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, dpll_freq_n, buf, 5);
	if (err)
		return err;

	/* Convert to frequency control word */
	fcw = sign_extend64(get_unaligned_le64(buf), 39);

	/* FCW unit is 77760 / ( 1638400 * 2^48) = 1.68615121864946 * 10^-16 */
	ffo->ffo = div_s64(fcw * 2107689, 12500);

	return 0;
}

static int rsmu_sabre_set_holdover_mode(struct rsmu_cdev *rsmu, u8 dpll, u8 enable, u8 mode)
{
	int err = -EINVAL;
	u8 prev_mode;

	if (mode > HOLDOVER_MODE_MAX)
		return -EINVAL;

	if (enable) {
		err = set_manual_holdover_mode(rsmu, dpll, (enum holdover_mode) mode);
		if (err)
			return err;

		err = set_dpll_oper_mode(rsmu, dpll, PLL_MODE_FORCE_HOLDOVER, true);
	} else {
		prev_mode = rsmu_get_bitfield(dpll_operating_mode_cnfg_prev[dpll], 0x1f, 0);

		switch (prev_mode) {
		case PLL_MODE_DCO:
			err = set_dpll_oper_mode(rsmu, dpll, PLL_MODE_DCO, false);
			if (err)
				return err;
			err = set_manual_holdover_mode(rsmu, dpll, HOLDOVER_MODE_MANUAL);
			break;

		case PLL_MODE_WPH:
			err = set_dpll_oper_mode(rsmu, dpll, PLL_MODE_WPH, false);
			break;

		case PLL_MODE_AUTOMATIC:
			err = set_dpll_oper_mode(rsmu, dpll, PLL_MODE_AUTOMATIC, false);
			break;

		default:
			/* Do nothing*/
			dev_err(rsmu->dev, "%s: Unsupported operating mode 0x%02x", __func__, prev_mode);
			break;
		}
	}
	return err;
}

static int rsmu_sabre_load_firmware(struct rsmu_cdev *rsmu,
				    char fwname[FW_NAME_LEN_MAX])
{
	char fname[128] = FW_FILENAME;
	const struct firmware *fw;
	struct idt82p33_fwrc *rec;
	u8 loaddr, page, val;
	int err;
	s32 len;

	if (fwname) /* module parameter */
		snprintf(fname, sizeof(fname), "%s", fwname);

	dev_info(rsmu->dev, "requesting firmware '%s'\n", fname);

	err = request_firmware(&fw, fname, rsmu->dev);

	if (err) {
		dev_err(rsmu->dev,
			"Failed in %s with err %d!\n", __func__, err);
		return err;
	}

	dev_dbg(rsmu->dev, "firmware size %zu bytes\n", fw->size);

	rec = (struct idt82p33_fwrc *) fw->data;

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {

		if (rec->reserved) {
			dev_err(rsmu->dev,
				"bad firmware, reserved field non-zero\n");
			err = -EINVAL;
		} else {
			val = rec->value;
			loaddr = rec->loaddr;
			page = rec->hiaddr;

			rec++;

			err = check_and_set_masks(rsmu, page, loaddr, val);
		}

		if (err == 0) {
			/* Page size 128, last 4 bytes of page skipped */
			if (loaddr > 0x7b)
				continue;
			err = regmap_bulk_write(rsmu->regmap, REG_ADDR(page, loaddr),
						&val, sizeof(val));
		}

		if (err)
			goto out;
	}

out:
	release_firmware(fw);
	return err;
}

struct rsmu_ops sabre_ops = {
	.type = RSMU_SABRE,
	.set_combomode = rsmu_sabre_set_combomode,
	.get_dpll_state = rsmu_sabre_get_dpll_state,
	.get_dpll_ffo = rsmu_sabre_get_dpll_ffo,
	.set_holdover_mode = rsmu_sabre_set_holdover_mode,
	.get_fw_version = NULL,
	.load_firmware = rsmu_sabre_load_firmware,
};
