// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */
#include <linux/io.h>

#include "ai-engine-internal.h"
#include "linux/xlnx-ai-engine.h"

/**
 * aie_get_broadcast_event() - get event ID being broadcast on given
 *			       broadcast line.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @bc_id: broadcast ID.
 * @return: event ID.
 */
static u8 aie_get_broadcast_event(struct aie_partition *apart,
				  struct aie_location *loc,
				  enum aie_module_type module, u8 bc_id)
{
	const struct aie_event_attr *event_mod;
	u32 bcoff, regoff;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	bcoff = event_mod->bc_regoff + event_mod->bc_event.regoff + bc_id * 4U;
	regoff = aie_cal_regoff(apart->adev, *loc, bcoff);
	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_read_event_status() - get the status of event status registers.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @reg: array to store event status register values.
 */
static void aie_read_event_status(struct aie_partition *apart,
				  struct aie_location *loc,
				  enum aie_module_type module, u32 *reg)
{
	const struct aie_event_attr *event_mod;
	u8 offset;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	for (offset = 0; offset < (event_mod->num_events / 32); offset++) {
		u32 status_off = event_mod->status_regoff + offset * 4U;
		u32 regoff = aie_cal_regoff(apart->adev, *loc, status_off);

		reg[offset] = ioread32(apart->adev->base + regoff);
	}
}

/**
 * aie_check_group_errors_enabled() - get error events enabled in group error.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @return: bitmap of enabled error events.
 */
static u32 aie_check_group_errors_enabled(struct aie_partition *apart,
					  struct aie_location *loc,
					  enum aie_module_type module)
{
	const struct aie_event_attr *event_mod;
	u32 groff, regoff;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	groff = event_mod->group_regoff + event_mod->group_error.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, groff);
	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_set_error_event() - enable/disable error events in group error.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @bitmap: error event to enable/disable in group errors.
 */
static void aie_set_error_event(struct aie_partition *apart,
				struct aie_location *loc,
				enum aie_module_type module, u32 bitmap)
{
	const struct aie_event_attr *event_mod;
	u32 groff, regoff;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	groff = event_mod->group_regoff + event_mod->group_error.regoff;
	regoff = aie_cal_regoff(apart->adev, *loc, groff);
	iowrite32(bitmap, apart->adev->base + regoff);
}

/**
 * aie_get_error_event() - map group error status bit to actual error
 *			   event number.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @index: event index within group errors.
 * @return: true event ID.
 */
static u32 aie_get_error_event(struct aie_partition *apart,
			       struct aie_location *loc,
			       enum aie_module_type module, u8 index)
{
	const struct aie_event_attr *event_mod;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	return event_mod->base_error_event + index;
}

/**
 * aie_get_bc_event() - get the broadcast event ID.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @bc_id: broadcast line ID.
 * @return: broadcast event ID.
 */
static u32 aie_get_bc_event(struct aie_partition *apart,
			    struct aie_location *loc,
			    enum aie_module_type module, u8 bc_id)
{
	const struct aie_event_attr *event_mod;

	if (module == AIE_CORE_MOD)
		event_mod = apart->adev->core_events;
	else if (module == AIE_MEM_MOD)
		event_mod = apart->adev->mem_events;
	else
		event_mod = apart->adev->pl_events;

	return event_mod->base_bc_event + bc_id;
}

/**
 * aie_get_l1_event() - get event ID being broadcast on level 1 IRQ.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @sw: switch type.
 * @irq_id: IRQ event ID to be read.
 * @return: true event ID.
 */
static u8 aie_get_l1_event(struct aie_partition *apart,
			   struct aie_location *loc,
			   enum aie_shim_switch_type sw, u8 irq_id)
{
	const struct aie_l1_intr_ctrl_attr *intr_ctrl = apart->adev->l1_ctrl;
	u32 l1off, l1mask, regoff, reg_value;

	if (sw == AIE_SHIM_SWITCH_A) {
		l1off = intr_ctrl->regoff + intr_ctrl->swa_event.regoff;
		l1mask = intr_ctrl->swa_event.mask;
	} else {
		l1off = intr_ctrl->regoff + intr_ctrl->swb_event.regoff;
		l1mask = intr_ctrl->swb_event.mask;
	}

	regoff = aie_cal_regoff(apart->adev, *loc, l1off);
	reg_value = ioread32(apart->adev->base + regoff);
	reg_value &= l1mask << (irq_id * intr_ctrl->event_lsb);
	reg_value >>= (irq_id * intr_ctrl->event_lsb);
	return reg_value;
}

/**
 * aie_clear_l1_intr() - clear level 1 interrupt controller status.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @sw: switch type.
 * @irq_id: IRQ ID to be cleared.
 */
static void aie_clear_l1_intr(struct aie_partition *apart,
			      struct aie_location *loc,
			      enum aie_shim_switch_type sw, u8 irq_id)
{
	const struct aie_l1_intr_ctrl_attr *intr_ctrl = apart->adev->l1_ctrl;
	u32 l1off, regoff;

	if (sw == AIE_SHIM_SWITCH_A)
		l1off = intr_ctrl->regoff + intr_ctrl->swa_status.regoff;
	else
		l1off = intr_ctrl->regoff + intr_ctrl->swb_status.regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, l1off);
	iowrite32(BIT(irq_id), apart->adev->base + regoff);
}

/**
 * aie_get_l1_status() - get level 1 interrupt controller status value.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @sw: switch type.
 * @return: status value.
 */
static u32 aie_get_l1_status(struct aie_partition *apart,
			     struct aie_location *loc,
			     enum aie_shim_switch_type sw)
{
	const struct aie_l1_intr_ctrl_attr *intr_ctrl = apart->adev->l1_ctrl;
	u32 l1off, regoff;

	if (sw == AIE_SHIM_SWITCH_A)
		l1off = intr_ctrl->regoff + intr_ctrl->swa_status.regoff;
	else
		l1off = intr_ctrl->regoff + intr_ctrl->swb_status.regoff;

	regoff = aie_cal_regoff(apart->adev, *loc, l1off);
	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_clear_l2_intr() - clear level 2 interrupt controller status.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @bitmap_irq: IRQ bitmap. IRQ lines corresponding to set bits will be
 *		cleared.
 */
static void aie_clear_l2_intr(struct aie_partition *apart,
			      struct aie_location *loc, u32 bitmap_irq)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = apart->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->status.regoff;
	u32 regoff = aie_cal_regoff(apart->adev, *loc, l2off);

	iowrite32(bitmap_irq, apart->adev->base + regoff);
}

/**
 * aie_get_l2_status() - get level 2 interrupt controller status value.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @return: status value.
 */
static u32 aie_get_l2_status(struct aie_partition *apart,
			     struct aie_location *loc)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = apart->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->status.regoff;
	u32 regoff = aie_cal_regoff(apart->adev, *loc, l2off);

	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_get_l2_mask() - get level 2 interrupt controller mask value.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @return: mask value.
 */
static u32 aie_get_l2_mask(struct aie_partition *apart,
			   struct aie_location *loc)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = apart->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->mask.regoff;
	u32 regoff = aie_cal_regoff(apart->adev, *loc, l2off);

	return ioread32(apart->adev->base + regoff);
}

/**
 * aie_enable_l2_ctrl() - enable interrupts to level 2 interrupt controller.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @bit_map: bitmap of broadcast lines to enable.
 */
static void aie_enable_l2_ctrl(struct aie_partition *apart,
			       struct aie_location *loc, u32 bit_map)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = apart->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->enable.regoff;
	u32 regoff = aie_cal_regoff(apart->adev, *loc, l2off);

	bit_map &= intr_ctrl->enable.mask;
	iowrite32(bit_map, apart->adev->base + regoff);
}

/**
 * aie_disable_l2_ctrl() - disable interrupts to level 2 interrupt controller.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @bit_map: bitmap of broadcast lines to disable.
 */
static void aie_disable_l2_ctrl(struct aie_partition *apart,
				struct aie_location *loc, u32 bit_map)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = apart->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->disable.regoff;
	u32 regoff = aie_cal_regoff(apart->adev, *loc, l2off);

	bit_map &= intr_ctrl->disable.mask;
	iowrite32(bit_map, apart->adev->base + regoff);
}
