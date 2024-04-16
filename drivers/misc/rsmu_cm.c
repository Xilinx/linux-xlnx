// SPDX-License-Identifier: GPL-2.0+
/*
 * This driver is developed for the IDT ClockMatrix(TM) of
 * timing and synchronization devices.
 *
 * Copyright (C) 2019 Integrated Device Technology, Inc., a Renesas Company.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/mfd/idt8a340_reg.h>
#include <linux/mfd/rsmu.h>
#include <asm/unaligned.h>

#include "rsmu_cdev.h"

#define FW_FILENAME	"rsmu8A34xxx.bin"

static int check_and_set_masks(struct rsmu_cdev *rsmu,
			       u16 regaddr,
			       u8 val)
{
	int err = 0;

	return err;
}

static int get_dpll_reg_offset(u8 fw_version, u8 dpll, u32 *dpll_reg_offset)
{
	switch (dpll) {
	case 0:
		*dpll_reg_offset = DPLL_0;
		break;
	case 1:
		*dpll_reg_offset = DPLL_1;
		break;
	case 2:
		*dpll_reg_offset = IDTCM_FW_REG(fw_version, V520, DPLL_2);
		break;
	case 3:
		*dpll_reg_offset = DPLL_3;
		break;
	case 4:
		*dpll_reg_offset = IDTCM_FW_REG(fw_version, V520, DPLL_4);
		break;
	case 5:
		*dpll_reg_offset = DPLL_5;
		break;
	case 6:
		*dpll_reg_offset = IDTCM_FW_REG(fw_version, V520, DPLL_6);
		break;
	case 7:
		*dpll_reg_offset = DPLL_7;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int get_dpll_ctrl_reg_offset(u8 dpll, u32 *dpll_ctrl_reg_offset)
{
	switch (dpll) {
	case 0:
		*dpll_ctrl_reg_offset = DPLL_CTRL_0;
		break;
	case 1:
		*dpll_ctrl_reg_offset = DPLL_CTRL_1;
		break;
	case 2:
		*dpll_ctrl_reg_offset = DPLL_CTRL_2;
		break;
	case 3:
		*dpll_ctrl_reg_offset = DPLL_CTRL_3;
		break;
	case 4:
		*dpll_ctrl_reg_offset = DPLL_CTRL_4;
		break;
	case 5:
		*dpll_ctrl_reg_offset = DPLL_CTRL_5;
		break;
	case 6:
		*dpll_ctrl_reg_offset = DPLL_CTRL_6;
		break;
	case 7:
		*dpll_ctrl_reg_offset = DPLL_CTRL_7;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rsmu_cm_set_combomode(struct rsmu_cdev *rsmu, u8 dpll, u8 mode)
{
	u32 dpll_ctrl_reg_addr;
	u8 reg;
	int err;

	err = get_dpll_ctrl_reg_offset(dpll, &dpll_ctrl_reg_addr);
	if (err)
		return err;

	if (mode >= E_COMBOMODE_MAX)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, dpll_ctrl_reg_addr + DPLL_CTRL_COMBO_MASTER_CFG,
						&reg, sizeof(reg));
	if (err)
		return err;

	/* Only need to enable/disable COMBO_MODE_HOLD. */
	if (mode)
		reg |= COMBO_MASTER_HOLD;
	else
		reg &= ~COMBO_MASTER_HOLD;

	return regmap_bulk_write(rsmu->regmap, dpll_ctrl_reg_addr + DPLL_CTRL_COMBO_MASTER_CFG,
						&reg, sizeof(reg));
}

static int rsmu_cm_set_holdover_mode(struct rsmu_cdev *rsmu, u8 dpll, u8 enable, u8 mode)
{
	/* This function enables or disables holdover. The mode is ignored. */
	u32 dpll_reg_addr;
	u8 dpll_mode_reg_off;
	u8 state_mode;
	u8 reg;
	int err;

	(void)mode;

	err = get_dpll_reg_offset(rsmu->fw_version, dpll, &dpll_reg_addr);
	if (err)
		return err;

	dpll_mode_reg_off = IDTCM_FW_REG(rsmu->fw_version, V520, DPLL_MODE);

	err = regmap_bulk_read(rsmu->regmap, dpll_reg_addr + dpll_mode_reg_off, &reg, sizeof(reg));
	if (err)
		return err;

	/*
	 * To enable holdover, set state_mode (bits [0, 2]) to force_holdover (3).
	 * To disable holdover, set state_mode (bits [0, 2]) to automatic (0).
	 */
	state_mode = reg & 0x7;
	if (enable) {
		if (state_mode == 3)
			return 0;
	} else {
		if (state_mode == 0)
			return 0;
	}

	/* Set state_mode to 0 */
	reg &= 0xF8;
	if (enable)
		reg |= 3;

	return regmap_bulk_write(rsmu->regmap, dpll_reg_addr + dpll_mode_reg_off, &reg, sizeof(reg));
}

static int rsmu_cm_set_output_tdc_go(struct rsmu_cdev *rsmu, u8 tdc, u8 enable)
{
	/* This function enables or disables output tdc alignment. */
	u8 tdc_ctrl4_offset;
	u32 tdc_n;
	u8 reg;
	int err;

	tdc_ctrl4_offset = IDTCM_FW_REG(rsmu->fw_version, V520, OUTPUT_TDC_CTRL_4);

	switch (tdc) {
	case 0:
		tdc_n = OUTPUT_TDC_0;
		break;
	case 1:
		tdc_n = OUTPUT_TDC_1;
		break;
	case 2:
		tdc_n = OUTPUT_TDC_2;
		break;
	case 3:
		tdc_n = OUTPUT_TDC_3;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, tdc_n + tdc_ctrl4_offset,
			       &reg, sizeof(reg));

	if (enable)
		reg |= 0x01;
	else
		reg &= ~0x01;

	return regmap_bulk_write(rsmu->regmap, tdc_n + tdc_ctrl4_offset,
				 &reg, sizeof(reg));
}

static int rsmu_cm_get_dpll_state(struct rsmu_cdev *rsmu, u8 dpll, u8 *state)
{
	u8 reg;
	int err;

	/* 8 is sys dpll */
	if (dpll > 8)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, STATUS + DPLL0_STATUS + dpll, &reg, sizeof(reg));
	if (err)
		return err;

	switch (reg & DPLL_STATE_MASK) {
	case DPLL_STATE_FREERUN:
		*state = E_SRVLOUNQUALIFIEDSTATE;
		break;
	case DPLL_STATE_LOCKACQ:
	case DPLL_STATE_LOCKREC:
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

static int rsmu_cm_get_dpll_ffo(struct rsmu_cdev *rsmu, u8 dpll,
				struct rsmu_get_ffo *ffo)
{
	u8 buf[8] = {0};
	s64 fcw = 0;
	u16 dpll_filter_status;
	int err;

	switch (dpll) {
	case 0:
		dpll_filter_status = DPLL0_FILTER_STATUS;
		break;
	case 1:
		dpll_filter_status = DPLL1_FILTER_STATUS;
		break;
	case 2:
		dpll_filter_status = DPLL2_FILTER_STATUS;
		break;
	case 3:
		dpll_filter_status = DPLL3_FILTER_STATUS;
		break;
	case 4:
		dpll_filter_status = DPLL4_FILTER_STATUS;
		break;
	case 5:
		dpll_filter_status = DPLL5_FILTER_STATUS;
		break;
	case 6:
		dpll_filter_status = DPLL6_FILTER_STATUS;
		break;
	case 7:
		dpll_filter_status = DPLL7_FILTER_STATUS;
		break;
	case 8:
		dpll_filter_status = DPLLSYS_FILTER_STATUS;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_bulk_read(rsmu->regmap, STATUS + dpll_filter_status, buf, 6);
	if (err)
		return err;

	/* Convert to frequency control word (FCW) */
	fcw = sign_extend64(get_unaligned_le64(buf), 47);

	/* FCW unit is 2 ^ -53 = 1.1102230246251565404236316680908e-16 */
	ffo->ffo = fcw * 111;

	return 0;
}

static int rsmu_cm_get_fw_version(struct rsmu_cdev *rsmu)
{
	int err;
	u8 major;
	u8 minor;
	u8 hotfix;

	err = regmap_bulk_read(rsmu->regmap, GENERAL_STATUS + MAJ_REL,
			       &major, sizeof(major));
	if (err)
		return err;
	major >>= 1;

	err = regmap_bulk_read(rsmu->regmap, GENERAL_STATUS + MIN_REL,
			       &minor, sizeof(minor));
	if (err)
		return err;

	err = regmap_bulk_read(rsmu->regmap, GENERAL_STATUS + HOTFIX_REL,
			       &hotfix, sizeof(hotfix));
	if (err)
		return err;

	if (major >= 5 && minor >= 2) {
		rsmu->fw_version = V520;
		return 0;
	}

	if (major == 4 && minor >= 8) {
		rsmu->fw_version = V487;
		return 0;
	}

	rsmu->fw_version = V_DEFAULT;
	return 0;
}

static int rsmu_cm_load_firmware(struct rsmu_cdev *rsmu,
				 char fwname[FW_NAME_LEN_MAX])
{
	u16 scratch = IDTCM_FW_REG(rsmu->fw_version, V520, SCRATCH);
	char fname[FW_NAME_LEN_MAX] = FW_FILENAME;
	const struct firmware *fw;
	struct idtcm_fwrc *rec;
	u32 regaddr;
	int err;
	s32 len;
	u8 val;
	u8 loaddr;

	if (fwname) /* Module parameter */
		snprintf(fname, sizeof(fname), "%s", fwname);

	dev_info(rsmu->dev, "requesting firmware '%s'", fname);

	err = request_firmware(&fw, fname, rsmu->dev);
	if (err) {
		dev_err(rsmu->dev, "Loading firmware %s failed !!!", fname);
		return err;
	}

	dev_dbg(rsmu->dev, "firmware size %zu bytes", fw->size);

	rec = (struct idtcm_fwrc *) fw->data;

	for (len = fw->size; len > 0; len -= sizeof(*rec)) {
		if (rec->reserved) {
			dev_err(rsmu->dev,
				"bad firmware, reserved field non-zero");
			err = -EINVAL;
		} else {
			regaddr = rec->hiaddr << 8;
			regaddr |= rec->loaddr;

			val = rec->value;
			loaddr = rec->loaddr;

			rec++;

			err = check_and_set_masks(rsmu, regaddr, val);
		}

		if (err != -EINVAL) {
			err = 0;

			/* Top (status registers) and bottom are read-only */
			if (regaddr < SCSR_ADDR(GPIO_USER_CONTROL) || regaddr >= scratch)
				continue;

			/* Page size 128, last 4 bytes of page skipped */
			if ((loaddr > 0x7b && loaddr <= 0x7f) || loaddr > 0xfb)
				continue;

			err = regmap_bulk_write(rsmu->regmap, SCSR_BASE + regaddr,
					       &val, sizeof(val));
		}

		if (err)
			goto out;
	}

out:
	release_firmware(fw);
	return err;
}

static int rsmu_cm_get_clock_index(struct rsmu_cdev *rsmu,
				u8 dpll,
				s8 *clock_index)
{
	u8 reg;
	int err;

	/* 8 is sys dpll */
	if (dpll > 8)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, STATUS + DPLL0_REF_STATUS + dpll,
						&reg, sizeof(reg));
	if (err)
		return err;

	reg &= DPLL_REF_STATUS_MASK;

	if (reg > (MAX_ELECTRICAL_REFERENCES - 1))
		*clock_index = -1;
	else
		*clock_index = reg;

	return err;
}

static int rsmu_cm_set_clock_priorities(struct rsmu_cdev *rsmu, u8 dpll, u8 number_entries,
				struct rsmu_priority_entry *priority_entry)
{
	u32 dpll_reg_addr;
	u8 dpll_mode_reg_off;
	int priority_index;
	int prev_priority_group = 0;
	u8 current_priority_group;
	int prev_priority = -1;
	u8 reg;
	int err;

	err = get_dpll_reg_offset(rsmu->fw_version, dpll, &dpll_reg_addr);
	if (err)
		return err;

	/* MAX_REF_PRIORITIES is maximum number of priorities */
	if (number_entries > MAX_REF_PRIORITIES)
		return -EINVAL;

	dpll_mode_reg_off = IDTCM_FW_REG(rsmu->fw_version, V520, DPLL_MODE);

	for (priority_index = 0; priority_index < number_entries; priority_index++) {
		if ((priority_entry->clock_index >= MAX_ELECTRICAL_REFERENCES) || (priority_entry->priority >= MAX_REF_PRIORITIES))
			return -EINVAL;

		if (priority_entry->priority == prev_priority) {
			current_priority_group = prev_priority_group;
		} else if ((priority_index < (number_entries - 1)) && (priority_entry->priority == (priority_entry + 1)->priority)) {
			if (prev_priority_group < MAX_PRIORITY_GROUP) {
				prev_priority_group++;
				current_priority_group = prev_priority_group;
			} else {
				current_priority_group = DEFAULT_PRIORITY_GROUP;
			}
		} else {
			current_priority_group = DEFAULT_PRIORITY_GROUP;
		}

		prev_priority = priority_entry->priority;

		reg = (1 << DPLL_REF_PRIORITY_ENABLE_SHIFT) |
			(priority_entry->clock_index << DPLL_REF_PRIORITY_REF_SHIFT) |
			(current_priority_group << DPLL_REF_PRIORITY_GROUP_NUMBER_SHIFT);

		err = regmap_bulk_write(rsmu->regmap, dpll_reg_addr + DPLL_REF_PRIORITY_0 + priority_index,
							&reg, sizeof(reg));
		if (err)
			return err;

		priority_entry++;
	}

	for (; priority_index < MAX_REF_PRIORITIES; priority_index++) {
		reg = (0 << DPLL_REF_PRIORITY_ENABLE_SHIFT) |
			(0 << DPLL_REF_PRIORITY_REF_SHIFT) |
			(DEFAULT_PRIORITY_GROUP << DPLL_REF_PRIORITY_GROUP_NUMBER_SHIFT);

		err = regmap_bulk_write(rsmu->regmap, dpll_reg_addr + DPLL_REF_PRIORITY_0 + priority_index,
							&reg, sizeof(reg));
		if (err)
			return err;
	}

	err = regmap_bulk_read(rsmu->regmap, dpll_reg_addr + dpll_mode_reg_off, &reg, sizeof(reg));
	if (err)
		return err;

	return regmap_bulk_write(rsmu->regmap, dpll_reg_addr + dpll_mode_reg_off, &reg, sizeof(reg));
}

static int rsmu_cm_get_reference_monitor_status(struct rsmu_cdev *rsmu, u8 clock_index,
				struct rsmu_reference_monitor_status_alarms *alarms)
{
	u8 reg;
	int err;

	/* MAX_ELECTRICAL_REFERENCES is maximum number of electrical references */
	if (clock_index >= MAX_ELECTRICAL_REFERENCES)
		return -EINVAL;

	err = regmap_bulk_read(rsmu->regmap, STATUS + IN0_MON_STATUS + clock_index,
						&reg, sizeof(reg));
	if (err)
		return err;

	alarms->los = (reg >> IN_MON_STATUS_LOS_SHIFT) & 1;
	alarms->no_activity = (reg >> IN_MON_STATUS_NO_ACT_SHIFT) & 1;
	alarms->frequency_offset_limit = (reg >> IN_MON_STATUS_FFO_LIMIT_SHIFT) & 1;

	return err;
}

struct rsmu_ops cm_ops = {
	.type = RSMU_CM,
	.set_combomode = rsmu_cm_set_combomode,
	.get_dpll_state = rsmu_cm_get_dpll_state,
	.get_dpll_ffo = rsmu_cm_get_dpll_ffo,
	.set_holdover_mode = rsmu_cm_set_holdover_mode,
	.set_output_tdc_go = rsmu_cm_set_output_tdc_go,
	.get_fw_version = rsmu_cm_get_fw_version,
	.load_firmware = rsmu_cm_load_firmware,
	.get_clock_index = rsmu_cm_get_clock_index,
	.set_clock_priorities = rsmu_cm_set_clock_priorities,
	.get_reference_monitor_status = rsmu_cm_get_reference_monitor_status
};
