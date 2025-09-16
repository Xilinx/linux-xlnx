// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */
#include <linux/bitmap.h>
#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "ai-engine-internal.h"
#include "linux/xlnx-ai-engine.h"
#include "ai-engine-trace.h"

#define AIE_ARRAY_TILE_ERROR_BC_ID		0U
#define AIE_SHIM_UC_EVENT_BC_ID			1U
#define AIE_SHIM_USER_EVENT1_BC_ID		2U

#define AIE_SHIM_INTR_BC_MAX			5U
#define AIE_L2_MASK_REG_BITS			32U

/* BIT(16) in 1st level IRQ event A, col 0, 2, 3, 4... */
#define AIE_SHIM_TILE_ERROR_L1_IRQ_EVENT_ID	0U
#define AIE_SHIM_TILE_ERROR_IRQ_ID		(16 + AIE_SHIM_TILE_ERROR_L1_IRQ_EVENT_ID)

/* BIT(16) in 1st level IRQ event A, only for col 1 */
#define AIE_SHIM_USER_EVENT1_L1_IRQ_EVENT_ID	2U
#define AIE_SHIM_USER_EVENT1_IRQ_ID		(16 + AIE_SHIM_USER_EVENT1_L1_IRQ_EVENT_ID)

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
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);
	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	bcoff = event_mod->bc_regoff + event_mod->bc_event.regoff + bc_id * 4U;
	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, bcoff);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_read_event_status() - get the status of event status registers.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @reg: array to store event status register values.
 */
void aie_read_event_status(struct aie_partition *apart,
			   struct aie_location *loc,
			   enum aie_module_type module, u32 *reg)
{
	const struct aie_event_attr *event_mod;
	u8 offset;
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	for (offset = 0; offset < (event_mod->num_events / 32); offset++) {
		u32 status_off = event_mod->status_regoff + offset * 4U;
		u32 regoff = aie_aperture_cal_regoff(apart->aperture, *loc,
						     status_off);

		reg[offset] = ioread32(apart->aperture->base + regoff);
	}
}

/**
 * aie_clear_event_status() - clears the status of event.
 * @apart: AIE partition pointer.
 * @loc: pointer to tile location.
 * @module: module type.
 * @event: event ID.
 */
static void aie_clear_event_status(struct aie_partition *apart,
				   struct aie_location *loc,
				   enum aie_module_type module, u8 event)
{
	const struct aie_event_attr *event_mod;
	u32 status_off, regoff;
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	if (event >= event_mod->num_events)
		return;

	status_off = event_mod->status_regoff + (event / 32) * 4U;
	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, status_off);
	iowrite32(BIT(event % 32), apart->aperture->base + regoff);
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
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	groff = event_mod->group_regoff + event_mod->group_error.regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, groff);
	return ioread32(apart->aperture->base + regoff);
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
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	groff = event_mod->group_regoff + event_mod->group_error.regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, groff);
	iowrite32(bitmap, apart->aperture->base + regoff);
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
	u32 ttype;

	ttype = apart->adev->ops->get_tile_type(apart->adev, loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	return event_mod->base_error_event + index;
}

/**
 * aie_get_bc_event() - get the broadcast event ID.
 * @apart: AIE partition pointer.
 * @ttype: tile type.
 * @module: module type.
 * @bc_id: broadcast line ID.
 * @return: broadcast event ID.
 */
static u32 aie_get_bc_event(struct aie_partition *apart, u32 ttype,
			    enum aie_module_type module, u8 bc_id)
{
	const struct aie_event_attr *event_mod;

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (module == AIE_CORE_MOD)
			event_mod = apart->adev->core_events;
		else
			event_mod = apart->adev->mem_events;
	} else if (ttype == AIE_TILE_TYPE_MEMORY) {
		event_mod = apart->adev->memtile_events;
	} else {
		event_mod = apart->adev->pl_events;
	}

	if (!event_mod)
		return 0;

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

	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, l1off);
	reg_value = ioread32(apart->aperture->base + regoff);
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

	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, l1off);
	iowrite32(BIT(irq_id), apart->aperture->base + regoff);
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

	regoff = aie_aperture_cal_regoff(apart->aperture, *loc, l1off);
	return ioread32(apart->aperture->base + regoff);
}

/**
 * aie_aperture_clear_l2_intr() - clear level 2 interrupt controller status.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @bitmap_irq: IRQ bitmap. IRQ lines corresponding to set bits will be
 *		cleared.
 */
static void aie_aperture_clear_l2_intr(struct aie_aperture *aperture,
				       struct aie_location *loc, u32 bitmap_irq)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = aperture->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->status.regoff;
	u32 regoff = aie_aperture_cal_regoff(aperture, *loc, l2off);

	iowrite32(bitmap_irq, aperture->base + regoff);
}

/**
 * aie_aperture_get_l2_status() - get level 2 interrupt controller status value.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @return: status value.
 */
static u32 aie_aperture_get_l2_status(struct aie_aperture *aperture,
				      struct aie_location *loc)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = aperture->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->status.regoff;
	u32 regoff = aie_aperture_cal_regoff(aperture, *loc, l2off);

	return ioread32(aperture->base + regoff);
}

/**
 * aie_aperture_get_l2_mask() - get level 2 interrupt controller mask value.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @return: mask value.
 */
static u32 aie_aperture_get_l2_mask(struct aie_aperture *aperture,
				    struct aie_location *loc)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = aperture->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->mask.regoff;
	u32 regoff = aie_aperture_cal_regoff(aperture, *loc, l2off);

	return ioread32(aperture->base + regoff);
}

/**
 * aie_aperture_enable_l2_ctrl() - enable interrupts to level 2 interrupt
 *				   controller.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @bit_map: bitmap of broadcast lines to enable.
 */
static void aie_aperture_enable_l2_ctrl(struct aie_aperture *aperture,
					struct aie_location *loc, u32 bit_map)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = aperture->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->enable.regoff;
	u32 regoff = aie_aperture_cal_regoff(aperture, *loc, l2off);

	bit_map &= intr_ctrl->enable.mask;
	iowrite32(bit_map, aperture->base + regoff);
}

/**
 * aie_aperture_disable_l2_ctrl() - disable interrupts to level 2 interrupt
 *				    controller.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @bit_map: bitmap of broadcast lines to disable.
 */
static void aie_aperture_disable_l2_ctrl(struct aie_aperture *aperture,
					 struct aie_location *loc, u32 bit_map)
{
	const struct aie_l2_intr_ctrl_attr *intr_ctrl = aperture->adev->l2_ctrl;
	u32 l2off = intr_ctrl->regoff + intr_ctrl->disable.regoff;
	u32 regoff = aie_aperture_cal_regoff(aperture, *loc, l2off);

	bit_map &= intr_ctrl->disable.mask;
	iowrite32(bit_map, aperture->base + regoff);
}

/**
 * aie_part_set_event_bitmap() - set the status of event in local event
 *				 bitmap.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @event: event ID to be logged.
 */
static void aie_part_set_event_bitmap(struct aie_partition *apart,
				      struct aie_location loc,
				      enum aie_module_type module, u8 event)
{
	u8 row, col, mod_num_events;
	struct aie_resource *event_sts;
	u32 offset;

	if (module == AIE_CORE_MOD) {
		event_sts = &apart->core_event_status;
		mod_num_events = apart->adev->core_events->num_events;
		row = loc.row - apart->range.start.row - 1;
	} else if (module == AIE_MEM_MOD) {
		event_sts = &apart->mem_event_status;
		mod_num_events = apart->adev->mem_events->num_events;
		row = loc.row - apart->range.start.row - 1;
	} else {
		event_sts = &apart->pl_event_status;
		mod_num_events = apart->adev->pl_events->num_events;
		row = loc.row;
	}

	col = loc.col - apart->range.start.col;

	offset = (col + row * apart->range.size.col) * mod_num_events + event;
	aie_resource_set(event_sts, offset, 1);
}

/**
 * aie_check_error_bitmap() - check the status of event in local event bitmap.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @event: event ID to check.
 * @return: true if event has happened, else false.
 */
bool aie_check_error_bitmap(struct aie_partition *apart,
			    struct aie_location loc,
			    enum aie_module_type module, u8 event)
{
	struct aie_resource *event_sts;
	u32 offset;
	u8 row, col, mod_num_events;

	if (module == AIE_CORE_MOD) {
		event_sts = &apart->core_event_status;
		mod_num_events = apart->adev->core_events->num_events;
		row = loc.row - apart->range.start.row - 1;
	} else if (module == AIE_MEM_MOD) {
		event_sts = &apart->mem_event_status;
		mod_num_events = apart->adev->mem_events->num_events;
		row = loc.row - apart->range.start.row - 1;
	} else {
		event_sts = &apart->pl_event_status;
		mod_num_events = apart->adev->pl_events->num_events;
		row = loc.row;
	}

	col = loc.col - apart->range.start.col;

	offset = (col + row * apart->range.size.col) * mod_num_events + event;
	return aie_resource_testbit(event_sts, offset);
}

/**
 * aie_tile_backtrack() - if error was asserted on a broadcast line in
 *			  the given array tile,
 *				* disable the error from the group errors
 *				* record the error event in local bitmap
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @sw: switch type.
 * @bc_id: broadcast ID.
 * @status: tile status register.
 * @return: true if error was asserted, else return false.
 */
static bool aie_tile_backtrack(struct aie_partition *apart,
			       struct aie_location loc,
			       enum aie_module_type module,
			       enum aie_shim_switch_type sw, u8 bc_id,
			       u32 *status)
{
	unsigned long grenabled;
	u8 n, grevent, eevent;
	bool ret = false;

	trace_aie_tile_backtrack(apart, loc, module, sw, bc_id);
	if (module == AIE_PL_MOD)
		grevent = aie_get_l1_event(apart, &loc, sw, bc_id);
	else
		grevent = aie_get_broadcast_event(apart, &loc, module, bc_id);

	aie_read_event_status(apart, &loc, module, status);
	trace_aie_tile_status(apart, &loc, module, status);

	if (!(status[grevent / 32] & BIT(grevent % 32)))
		return ret;

	grenabled = aie_check_group_errors_enabled(apart, &loc, module);
	trace_aie_tile_grenabled(apart, &loc, module, grenabled);
	for_each_set_bit(n, &grenabled, 32) {
		eevent = aie_get_error_event(apart, &loc, module, n);
		if (!(status[eevent / 32] & BIT(eevent % 32)))
			continue;
		trace_aie_tile_eevent(apart, &loc, module, eevent);
		grenabled &= ~BIT(n);
		aie_part_set_event_bitmap(apart, loc, module, eevent);
		ret = true;

		dev_err_ratelimited(&apart->adev->dev,
				    "Asserted tile error event %d at col %d row %d\n",
				    eevent, loc.col, loc.row);
	}
	aie_set_error_event(apart, &loc, module, grenabled);

	return ret;
}

/**
 * aie_map_l2_to_l1() - map the status bit set in level 2 interrupt controller
 *		        to a level 1 interrupt controller.
 * @apart: AIE partition pointer.
 * @set_pos: position of level 2 set bit.
 * @l2_col: level 2 interrupt controller column ID.
 * @l1_col: pointer to return corresponding level 1 column ID.
 * @sw: pointer to return the level 1 interrupt controller switch ID.
 *
 * This API implementation is tightly coupled with the level 2 to level 1
 * static mapping created when AIE application CDOs are generated.
 */
static void aie_map_l2_to_l1(struct aie_partition *apart, u32 set_pos,
			     u32 l2_col, u32 *l1_col,
			     enum aie_shim_switch_type *sw)
{
	if (l2_col + 3 >= apart->range.start.col + apart->range.size.col) {
		*l1_col = l2_col + (set_pos % 6) / 2;
		*sw = (set_pos % 6) % 2;
	} else if (l2_col % 2 == 0) {
		/* set bit position could be 0 - 5 */
		*l1_col = l2_col - (2 - (set_pos % 6) / 2);
		*sw = (set_pos % 6) % 2;
	} else {
		/* set bit position could be 0 - 1 */
		*l1_col = l2_col;
		*sw = set_pos;
	}
}

/**
 * aie_l1_backtrack() - backtrack AIE array tiles or shim tile based on
 *			the level 2 status bit set.
 * @apart: AIE partition pointer.
 * @loc: tile location of level 2 interrupt controller.
 * @set_pos: set bit position in level 2 controller status.
 * @return: true if error was asserted, else return false.
 */
static bool aie_l1_backtrack(struct aie_partition *apart,
			     struct aie_location loc, u32 set_pos)
{
	u32 mem_srow, mem_erow, aie_srow, aie_erow;
	enum aie_shim_switch_type sw;
	struct aie_location l1_ctrl;
	enum aie_module_type module;
	bool ret = false;
	u32 bc_event;
	u32 l1_status;

	mem_srow = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;
	mem_erow = mem_srow +
		   apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;
	aie_srow = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	aie_erow = aie_srow +
		   apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;

	/*
	 * Based on the set status bit find which level 1 interrupt
	 * controller has generated an interrupt
	 */
	l1_ctrl.row = 0;
	aie_map_l2_to_l1(apart, set_pos, loc.col, &l1_ctrl.col, &sw);
	module = (sw == AIE_SHIM_SWITCH_A) ? AIE_CORE_MOD : AIE_MEM_MOD;
	loc = l1_ctrl;
	trace_aie_l1_backtrack(apart, loc.col, module);

	/*
	 * This should not be the case if the routing is generated based on
	 * the partition. In case, the routing is generated with different
	 * partition which is not likely, if doesn't have this checking, it
	 * can access the tiles outside the partition.
	 */
	if (l1_ctrl.col >= (apart->range.start.col + apart->range.size.col))
		return false;

	l1_status = aie_get_l1_status(apart, &l1_ctrl, sw);
	trace_aie_l1_status(apart, l1_ctrl.col, sw, l1_status);

	if (l1_status & BIT(AIE_SHIM_TILE_ERROR_IRQ_ID)) {
		u32 status[AIE_NUM_EVENT_STS_SHIMTILE] = {0};

		aie_clear_l1_intr(apart, &l1_ctrl, sw,
				  AIE_SHIM_TILE_ERROR_IRQ_ID);
		if (aie_tile_backtrack(apart, l1_ctrl, AIE_PL_MOD, sw,
				       AIE_SHIM_TILE_ERROR_IRQ_ID, status))
			ret = true;
	}

	if (!(l1_status & BIT(AIE_ARRAY_TILE_ERROR_BC_ID)))
		return ret;

	aie_clear_l1_intr(apart, &l1_ctrl, sw, AIE_ARRAY_TILE_ERROR_BC_ID);

	if (sw != AIE_SHIM_SWITCH_A)
		goto backtrack_aie_tile;

	bc_event = aie_get_bc_event(apart, AIE_TILE_TYPE_MEMORY, AIE_MEM_MOD,
				    AIE_ARRAY_TILE_ERROR_BC_ID);
	for (loc.row = mem_srow; loc.row < mem_erow; loc.row++) {
		u32 status[AIE_NUM_EVENT_STS_MEMTILE] = {0};

		if (!aie_part_check_clk_enable_loc(apart, &loc))
			continue;
		ret |= aie_tile_backtrack(apart, loc, AIE_MEM_MOD, sw,
					  AIE_ARRAY_TILE_ERROR_BC_ID, status);
		aie_clear_event_status(apart, &loc, AIE_MEM_MOD, bc_event);
	}

backtrack_aie_tile:
	bc_event = aie_get_bc_event(apart, AIE_TILE_TYPE_TILE, module,
				    AIE_ARRAY_TILE_ERROR_BC_ID);
	for (loc.row = aie_srow; loc.row < aie_erow; loc.row++) {
		u32 status[AIE_NUM_EVENT_STS_CORETILE] = {0};

		if (!aie_part_check_clk_enable_loc(apart, &loc))
			continue;
		ret |= aie_tile_backtrack(apart, loc, module, sw,
					  AIE_ARRAY_TILE_ERROR_BC_ID, status);
		if (!(status[bc_event / 32] & BIT(bc_event % 32)))
			break;
		aie_clear_event_status(apart, &loc, module, bc_event);
	}

	return ret;
}

/**
 * aie_range_get_num_nocs() - get number of shim NOC tiles of AI enigne range
 * @range: AI engine tiles range pointer
 * @aperture: AI engine aperture pointer
 * @l2_mask_off: return l2 mask start offset of the range
 * @return: number of shim NOC tiles of the AI engine partition.
 */
static u32 aie_range_get_num_nocs(const struct aie_range *range,
				  const struct aie_aperture *aperture,
				  u32 *l2_mask_off)
{
	struct aie_location loc;
	struct aie_device *adev = aperture->adev;
	u32 num_nocs = 0;

	for (loc.col = range->start.col, loc.row = 0;
	     loc.col < range->start.col + range->size.col; loc.col++) {
		u32 ttype;

		ttype = adev->ops->get_tile_type(adev, &loc);
		if (ttype != AIE_TILE_TYPE_SHIMNOC)
			continue;
		num_nocs++;
	}

	if (num_nocs && l2_mask_off) {
		*l2_mask_off = 0;
		for (loc.col = aperture->range.start.col, loc.row = 0;
		     loc.col < range->start.col; loc.col++) {
			u32 ttype;

			ttype = adev->ops->get_tile_type(adev, &loc);
			if (ttype != AIE_TILE_TYPE_SHIMNOC)
				continue;
			*l2_mask_off += 1;
		}
	}

	return num_nocs;
}

/**
 * aie_l2_backtrack() - iterate through each level 2 interrupt controller
 *			in a given partition and backtrack its
 *			corresponding level 1 interrupt controller.
 * @apart: AIE partition pointer
 */
static void aie_l2_backtrack(struct aie_partition *apart)
{
	struct aie_aperture *aperture = apart->aperture;
	u32 *aperture_l2_mask = aperture->l2_mask.val;
	struct aie_location loc;
	u32 l2_mask_index = 0;
	u32 n, ttype, num_nocs;
	int ret;

	trace_aie_l2_backtrack(apart);
	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err_ratelimited(&apart->dev,
				    "Failed to acquire lock. Process was interrupted by fatal signals\n");
		return;
	}

	num_nocs = aie_range_get_num_nocs(&apart->range, aperture,
					  &l2_mask_index);
	if (!num_nocs) {
		mutex_unlock(&apart->mlock);
		return;
	}

	for (loc.col = apart->range.start.col, loc.row = 0;
	     loc.col < apart->range.start.col + apart->range.size.col;
	     loc.col++) {
		unsigned long l2_mask;

		ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
		if (ttype != AIE_TILE_TYPE_SHIMNOC)
			continue;
		if (l2_mask_index >= aperture->l2_mask.count)
			break;

		l2_mask = aperture_l2_mask[l2_mask_index];
		for_each_set_bit(n, &l2_mask,
				 apart->adev->l2_ctrl->num_broadcasts) {
			if (aie_l1_backtrack(apart, loc, n))
				apart->error_to_report = 1;
		}
		aperture_l2_mask[l2_mask_index] = 0;
		l2_mask_index++;
		aie_aperture_enable_l2_ctrl(aperture, &loc, l2_mask);
	}

	mutex_unlock(&apart->mlock);

	/*
	 * If error was asserted or there are errors pending to be reported to
	 * the application, then invoke callback.
	 */
	if (apart->error_cb.cb && apart->error_to_report) {
		apart->error_to_report = 0;
		apart->error_cb.cb(apart->error_cb.priv);
	}
}

/**
 * aie2ps_col1_shim_backtrack() - if error was asserted on a broadcast line in
 *			  the given array tile,
 *				* disable the error from the group errors
 *				* record the error event in local bitmap
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @sw: switch type.
 * @bc_id: broadcast ID.
 * @status: tile status register.
 * @return: true if error was asserted, else return false.
 */
static bool aie2ps_col1_shim_backtrack(struct aie_partition *apart, struct aie_location loc,
				       enum aie_module_type module, enum aie_shim_switch_type sw,
				       u8 bc_id, u32 *status)
{
	unsigned long grenabled;
	u8 n, grevent, eevent;
	bool ret = false;

	trace_aie_tile_backtrack(apart, loc, module, sw, bc_id);
	grevent = aie_get_broadcast_event(apart, &loc, module, bc_id);

	aie_read_event_status(apart, &loc, module, status);
	trace_aie_tile_status(apart, &loc, module, status);

	if (!(status[grevent / 32] & BIT(grevent % 32)))
		return ret;

	grenabled = aie_check_group_errors_enabled(apart, &loc, module);
	trace_aie_tile_grenabled(apart, &loc, module, grenabled);
	for_each_set_bit(n, &grenabled, 32) {
		eevent = aie_get_error_event(apart, &loc, module, n);
		if (!(status[eevent / 32] & BIT(eevent % 32)))
			continue;
		trace_aie_tile_eevent(apart, &loc, module, eevent);
		grenabled &= ~BIT(n);
		aie_part_set_event_bitmap(apart, loc, module, eevent);
		ret = true;

		dev_err_ratelimited(&apart->adev->dev,
				    "Asserted tile error event %d at col %d row %d\n",
				    eevent, loc.col, loc.row);
	}
	aie_set_error_event(apart, &loc, module, grenabled);

	return ret;
}

static void aie2ps_l1_backtrack(struct aie_partition *apart, u32 col, enum aie_shim_switch_type sw)
{
	struct aie_location loc = {.col = col, .row = 0};
	u32 mem_srow, mem_erow, aie_srow, aie_erow;
	enum aie_module_type module;
	u32 bc_event;
	u32 status;
	bool ret = false;

	mem_srow = apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].start_row;
	mem_erow = mem_srow +
		   apart->adev->ttype_attr[AIE_TILE_TYPE_MEMORY].num_rows;
	aie_srow = apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].start_row;
	aie_erow = aie_srow +
		   apart->adev->ttype_attr[AIE_TILE_TYPE_TILE].num_rows;

	module = (sw == AIE_SHIM_SWITCH_A) ? AIE_CORE_MOD : AIE_MEM_MOD;

	trace_aie_l1_backtrack(apart, loc.col, module);
	status = aie_get_l1_status(apart, &loc, sw);
	trace_aie_l1_status(apart, loc.col, sw, status);

	if (col == (apart->range.start.col + 1)) {
		u32 event_status[AIE_NUM_EVENT_STS_SHIMTILE] = {};

		ret |= aie2ps_col1_shim_backtrack(apart, loc, AIE_PL_MOD, sw,
						  AIE_ARRAY_TILE_ERROR_BC_ID, event_status);
	}

	/* Clear SHIM error */
	if (status & BIT(AIE_SHIM_TILE_ERROR_IRQ_ID)) {
		u32 event_status[AIE_NUM_EVENT_STS_SHIMTILE] = {};

		aie_clear_l1_intr(apart, &loc, sw, AIE_SHIM_TILE_ERROR_IRQ_ID);
		if (aie_tile_backtrack(apart, loc, AIE_PL_MOD, sw, AIE_SHIM_TILE_ERROR_IRQ_ID,
				       event_status))
			ret = true;
	}

	if (!(status & BIT(AIE_ARRAY_TILE_ERROR_BC_ID)) &&
	    col != (apart->range.start.col + 1))
		return;

	if (col != (apart->range.start.col + 1))
		aie_clear_l1_intr(apart, &loc, sw, AIE_ARRAY_TILE_ERROR_BC_ID);

	if (sw != AIE_SHIM_SWITCH_A)
		goto backtrack_aie_tile;

	/* mem tiles errors */
	bc_event = aie_get_bc_event(apart, AIE_TILE_TYPE_MEMORY, AIE_MEM_MOD,
				    AIE_ARRAY_TILE_ERROR_BC_ID);
	for (loc.row = mem_srow; loc.row < mem_erow; loc.row++) {
		u32 event_status[AIE_NUM_EVENT_STS_MEMTILE] = {};

		if (!aie_part_check_clk_enable_loc(apart, &loc))
			continue;
		ret |= aie_tile_backtrack(apart, loc, module, sw,
					  AIE_ARRAY_TILE_ERROR_BC_ID, event_status);
		aie_clear_event_status(apart, &loc, AIE_MEM_MOD, bc_event);
	}

backtrack_aie_tile:
	bc_event = aie_get_bc_event(apart, AIE_TILE_TYPE_TILE, module,
				    AIE_ARRAY_TILE_ERROR_BC_ID);
	for (loc.row = aie_srow; loc.row < aie_erow; loc.row++) {
		u32 event_status[AIE_NUM_EVENT_STS_CORETILE] = {};

		if (!aie_part_check_clk_enable_loc(apart, &loc))
			continue;
		ret |= aie_tile_backtrack(apart, loc, module, sw,
					  AIE_ARRAY_TILE_ERROR_BC_ID, event_status);
		if (!(event_status[bc_event / 32] & BIT(bc_event % 32)))
			break;
		aie_clear_event_status(apart, &loc, module, bc_event);
	}
	apart->error_to_report |= ret;
}

static void _aie2ps_interrupt_user_event1(struct aie_partition *apart)
{
	const struct aie_event_attr *event_mod;
	u32 status[AIE_NUM_EVENT_STS_SHIMTILE];
	struct aie_location loc;
	bool complete = false;
	int end_col;

	if (apart->range.size.col < 2) {
		dev_err_ratelimited(&apart->adev->dev, "Cannot have partition with less than 2 cols.");
		return;
	}

	loc.col = apart->range.start.col + 1;
	loc.row = 0;
	event_mod = apart->adev->pl_events;
	end_col = apart->range.start.col + apart->range.size.col;

	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_A, AIE_SHIM_USER_EVENT1_BC_ID);
	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_B, AIE_SHIM_USER_EVENT1_BC_ID);
	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_A, AIE_SHIM_USER_EVENT1_IRQ_ID);

	for (loc.col = apart->range.start.col; loc.col < end_col; loc.col++) {
		aie_read_event_status(apart, &loc, AIE_PL_MOD, status);
		if (!(status[event_mod->user_event1 / 32] &
		    BIT(event_mod->user_event1 % 32))) {
			continue;
		}
		complete = true;
		aie_clear_event_status(apart, &loc, AIE_PL_MOD, event_mod->user_event1);
		dev_err(&apart->dev, "USER_EVENT1 on col: %d", loc.col);
	}
	if (complete && apart->user_event1_complete)
		apart->user_event1_complete(apart->partition_id, apart->user_event1_priv);
}

/**
 * aie2ps_partition_backtrack() - backtrack partition to find the source of error interrupt.
 * @apart: pointer to the partition structure.
 *
 * This task will re-enable IRQ after errors in all partitions has been
 * serviced.
 */
static void aie2ps_partition_backtrack(struct aie_partition *apart)
{
	struct aie_aperture *aperture = apart->aperture;
	u32 *aperture_l2_mask = aperture->l2_mask.val;
	int l2_mask_count = aperture->l2_mask.count;
	u32 l2_mask_index = 0;
	u32 col;
	int ret;

	trace_aie_l2_backtrack(apart);
	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err_ratelimited(&apart->dev,
				    "Failed to acquire lock. Process was interrupted by fatal signals\n");
		return;
	}

	/*
	 * If partition isn't requested yet, then only record the
	 * occurrence of error interrupt. Such errors can only be
	 * backtracked when the tiles in-use are known. Based on the
	 * error_to_report value a task is scheduled in the workqueue
	 * to backtrack this error interrupt when partition is
	 * requested.
	 */
	if (!apart->status)
		goto out;

	_aie2ps_interrupt_user_event1(apart);
	for (col = apart->range.start.col; col < apart->range.size.col; col++) {
		struct aie_location loc = {.col = col, .row = 0};
		u32 ttype, l2_mask;

		aie2ps_l1_backtrack(apart, col, AIE_SHIM_SWITCH_A);
		aie2ps_l1_backtrack(apart, col, AIE_SHIM_SWITCH_B);

		ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
		if (ttype != AIE_TILE_TYPE_SHIMNOC)
			continue;
		l2_mask = aperture_l2_mask[l2_mask_index];
		if (l2_mask_index >= l2_mask_count)
			break;

		l2_mask_index++;
		if (l2_mask)
			aie_aperture_enable_l2_ctrl(aperture, &loc, l2_mask);
	}

	/*
	 * If error was asserted or there are errors pending to be reported to
	 * the application, then invoke callback.
	 */
	if (apart->error_cb.cb && apart->error_to_report) {
		apart->error_to_report = 0;
		apart->error_cb.cb(apart->error_cb.priv);
	}
out:
	mutex_unlock(&apart->mlock);
}

/**
 * aie_part_backtrack() - backtrack a individual.
 * @apart: AIE partition pointer.
 */
static void aie_part_backtrack(struct aie_partition *apart)
{
	aie_l2_backtrack(apart);
}

/**
 * aie_aperture_backtrack() - backtrack each partition to find the source of
 *			      error interrupt.
 * @work: pointer to the work structure.
 *
 * This task will re-enable IRQ after errors in all partitions has been
 * serviced.
 */
void aie_aperture_backtrack(struct work_struct *work)
{
	struct aie_aperture *aperture;
	struct aie_partition *apart;
	int ret;

	aperture = container_of(work, struct aie_aperture, backtrack);
	trace_aie_aperture_backtrack(aperture->adev);

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret) {
		dev_err_ratelimited(&aperture->dev,
				    "Failed to acquire lock. Process was interrupted by fatal signals\n");
		return;
	}

	list_for_each_entry(apart, &aperture->partitions, node) {
		/*
		 * If partition isn't requested yet, then only record the
		 * occurrence of error interrupt. Such errors can only be
		 * backtracked when the tiles in-use are known. Based on the
		 * error_to_report value a task is scheduled in the workqueue
		 * to backtrack this error interrupt when partition is
		 * requested.
		 */
		if (!apart->status)
			continue;
		aie_part_backtrack(apart);
	}

	mutex_unlock(&aperture->mlock);
}

static int aie_aperture_clr_hw_err(struct aie_aperture *aperture, struct aie_location *loc,
				   u16 status)
{
	struct aie_op_start_num_col *op_range;
	struct aie_op_hw_err *hw_err;
	dma_addr_t pkt_dma;
	void *pkt_va;
	size_t size;
	int ret;

	size = sizeof(*op_range) + sizeof(*hw_err);
	pkt_va = dmam_alloc_coherent(&aperture->dev, size, &pkt_dma, GFP_KERNEL);
	if (!pkt_va)
		return -ENOMEM;

	op_range = pkt_va;
	op_range->type = XILINX_AIE_OPS_START_NUM_COL;
	op_range->len = sizeof(*op_range);
	op_range->start_col = loc->col;
	op_range->num_col = 1;

	hw_err = pkt_va + sizeof(*op_range);
	hw_err->type = XILINX_AIE_OPS_CLR_HW_ERR_STS;
	hw_err->len = sizeof(*hw_err);
	hw_err->val = status;

	trace_aie_pm_ops(aperture->node_id, pkt_va, size, pkt_dma);
	ret = versal2_pm_aie2ps_operation(aperture->node_id, size,
					  upper_32_bits(pkt_dma),
					  lower_32_bits(pkt_dma));

	dmam_free_coherent(&aperture->dev, size, pkt_va, pkt_dma);

	return ret;
}

/**
 * aie_aperture_get_hw_err_status() - get hw error status value.
 * @aperture: AIE aperture pointer.
 * @loc: pointer to tile location.
 * @return: status value.
 */
static u32 aie_aperture_get_hw_err_status(struct aie_aperture *aperture,
					  struct aie_location *loc)
{
	u32 hw_err_status_off;
	u32 regoff;

	if (!aperture->adev->hw_err_status)
		return 0;

	hw_err_status_off = aperture->adev->hw_err_status->regoff;
	regoff = aie_aperture_cal_regoff(aperture, *loc, hw_err_status_off);

	return ioread32(aperture->base + regoff);
}

static irqreturn_t aie2ps_hw_err(struct aie_aperture *aperture)
{
	const u32 end_col = aperture->range.start.col + aperture->range.size.col;
	const u32 start_col = aperture->range.start.col;
	irqreturn_t handled = IRQ_NONE;
	struct aie_location loc;
	u32 status;
	int ret;

	loc.row = 0;
	for (loc.col = start_col; loc.col < end_col; loc.col++) {
		status = aie_aperture_get_hw_err_status(aperture, &loc);
		if (status) {
			handled = IRQ_HANDLED;
			dev_err(&aperture->dev, "Received Hw err: 0x%x on col: %d",
				status, loc.col);
			ret = aie_aperture_clr_hw_err(aperture, &loc, (u16)status);
			if (ret) {
				dev_err(&aperture->dev, "Failed to clear hw error: 0x%x on col: %d, err: %d",
					status, loc.col, ret);
			}
		}
	}

	return handled;
}

static void aie2ps_aperture_backtrack(struct aie_aperture *aperture)
{
	struct aie_partition *apart;

	trace_aie_aperture_backtrack(aperture->adev);
	list_for_each_entry(apart, &aperture->partitions, node) {
		if (!apart->status)
			continue;
		aie2ps_partition_backtrack(apart);
	}
}

/**
 * aie2ps_interrupt_user_event1() - interrupt handler for AIE2PS for interence.
 * @irq: Interrupt number.
 * @data: AI engine partition struct.
 * @return: IRQ_HANDLED.
 *
 * This thread function disables level 2 interrupt controllers and ack the l2
 * controller. Clear the status of USER_EVENT1 event register. Call the
 * registered call back for interence completion.
 */
irqreturn_t aie2ps_interrupt_user_event1(int irq, void *data)
{
	const struct aie_event_attr *event_mod;
	u32 status[AIE_NUM_EVENT_STS_SHIMTILE];
	struct aie_partition *apart = data;
	struct aie_aperture *aperture;
	struct aie_location loc;
	int l2_mask, l2_status;
	bool complete = false;
	int end_col;

	aperture = apart->aperture;
	mutex_lock(&apart->mlock);
	if (!apart->status) {
		dev_err_ratelimited(&apart->dev, "USER_EVENT1 ISR: apart not active");
		goto out;
	}
	if (apart->range.size.col < 2) {
		dev_err_ratelimited(&apart->adev->dev, "Cannot have partition with less than 2 cols.");
		goto out;
	}

	loc.col = apart->range.start.col + 1;
	loc.row = 0;
	event_mod = apart->adev->pl_events;
	end_col = apart->range.start.col + apart->range.size.col;

	l2_mask = aie_aperture_get_l2_mask(aperture, &loc);
	if (!l2_mask)
		goto out;

	aie_aperture_disable_l2_ctrl(aperture, &loc, l2_mask);
	l2_status = aie_aperture_get_l2_status(aperture, &loc);
	if (!l2_status) {
		aie_aperture_enable_l2_ctrl(aperture, &loc, l2_mask);
		goto out;
	}
	aie_aperture_clear_l2_intr(aperture, &loc, l2_status);

	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_A, AIE_SHIM_USER_EVENT1_BC_ID);
	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_B, AIE_SHIM_USER_EVENT1_BC_ID);
	aie_clear_l1_intr(apart, &loc, AIE_SHIM_SWITCH_A, AIE_SHIM_USER_EVENT1_IRQ_ID);

	for (loc.col = apart->range.start.col; loc.col < end_col; loc.col++) {
		aie_read_event_status(apart, &loc, AIE_PL_MOD, status);
		if (!(status[event_mod->user_event1 / 32] &
		    BIT(event_mod->user_event1 % 32)))
			continue;
		complete = true;
		aie_clear_event_status(apart, &loc, AIE_PL_MOD,
				       event_mod->user_event1);
	}
	mutex_unlock(&apart->mlock);
	if (complete && apart->user_event1_complete)
		apart->user_event1_complete(apart->partition_id, apart->user_event1_priv);
	loc.col = apart->range.start.col + 1;
	loc.row = 0;
	aie_aperture_enable_l2_ctrl(aperture, &loc, l2_mask);

	return complete ? IRQ_HANDLED : IRQ_NONE;
out:
	mutex_unlock(&apart->mlock);
	return complete ? IRQ_HANDLED : IRQ_NONE;
}

irqreturn_t aie2ps_interrupt_fn(int irq, void *data)
{
	struct aie_aperture *aperture = data;
	struct aie_device *adev = aperture->adev;
	u32 end_col = aperture->range.start.col + aperture->range.size.col;
	struct aie_location loc;
	u32 *apart_l2_mask = aperture->l2_mask.val;
	int l2_mask_count = aperture->l2_mask.count;
	int l2_mask_index = 0;
	irqreturn_t ret = IRQ_NONE;
	bool backtrack = false;

	trace_aie_interrupt(adev);
	mutex_lock(&aperture->mlock);

	ret = aie2ps_hw_err(aperture);
	for (loc.col = aperture->range.start.col, loc.row = 0;
	     loc.col < end_col; loc.col++) {
		u32 ttype, l2_status, l2_mask;

		ttype = adev->ops->get_tile_type(adev, &loc);
		if (ttype != AIE_TILE_TYPE_SHIMNOC)
			continue;

		if (l2_mask_index >= l2_mask_count)
			break;

		l2_mask = aie_aperture_get_l2_mask(aperture, &loc);
		trace_aie_l2_mask(adev, loc.col, l2_mask);
		if (l2_mask) {
			apart_l2_mask[l2_mask_index] = l2_mask;
			aie_aperture_disable_l2_ctrl(aperture, &loc, l2_mask);
		}

		l2_status = aie_aperture_get_l2_status(aperture, &loc);
		trace_aie_l2_status(adev, loc.col, l2_status);
		if (l2_status) {
			aie_aperture_clear_l2_intr(aperture, &loc,
						   l2_status);
			backtrack = true;
		} else {
			aie_aperture_enable_l2_ctrl(aperture, &loc, l2_mask);
		}
		l2_mask_index++;
	}

	if (backtrack) {
		aie2ps_aperture_backtrack(aperture);
		ret = IRQ_HANDLED;
	}

	mutex_unlock(&aperture->mlock);
	return ret;
}

/**
 * aie_interrupt() - interrupt handler for AIE.
 * @irq: Interrupt number.
 * @data: AI engine aperture structure.
 * @return: IRQ_HANDLED.
 *
 * This thread function disables level 2 interrupt controllers and schedules a
 * task in workqueue to backtrack the source of error interrupt. Disabled
 * interrupts are re-enabled after successful completion of bottom half.
 */
irqreturn_t aie_interrupt(int irq, void *data)
{
	struct aie_aperture *aperture = data;
	struct aie_device *adev = aperture->adev;
	struct aie_location loc;
	bool sched_work = false;
	u32 *aperture_l2_mask = aperture->l2_mask.val;
	int l2_mask_count = aperture->l2_mask.count;
	int l2_mask_index = 0;

	trace_aie_interrupt(adev);
	for (loc.col = aperture->range.start.col, loc.row = 0;
	     loc.col < aperture->range.start.col + aperture->range.size.col;
	     loc.col++) {
		u32 ttype, l2_status, l2_mask;

		ttype = adev->ops->get_tile_type(adev, &loc);
		if (ttype != AIE_TILE_TYPE_SHIMNOC)
			continue;

		if (l2_mask_index >= l2_mask_count)
			break;

		l2_mask = aie_aperture_get_l2_mask(aperture, &loc);
		trace_aie_l2_mask(adev, loc.col, l2_mask);
		if (l2_mask) {
			aperture_l2_mask[l2_mask_index] = l2_mask;
			aie_aperture_disable_l2_ctrl(aperture, &loc, l2_mask);
		}

		l2_status = aie_aperture_get_l2_status(aperture, &loc);
		trace_aie_l2_status(adev, loc.col, l2_status);
		if (l2_status) {
			aie_aperture_clear_l2_intr(aperture, &loc,
						   l2_status);
			sched_work = true;
		} else {
			aie_aperture_enable_l2_ctrl(aperture, &loc,
						    l2_mask);
		}
		l2_mask_index++;
	}

	if (sched_work)
		schedule_work(&aperture->backtrack);

	return IRQ_HANDLED;
}

/**
 * aie_interrupt_callback() - S100/S200 callback.
 * @payload: payload data.
 * @data: AI engine aperture structure.
 *
 * This function calls aie_interrupt to disables level 2 interrupt controllers
 * and schedules a task in workqueue to backtrack the source of error interrupt.
 * Disabled interrupts are re-enabled after successful completion of bottom half.
 */
void aie_interrupt_callback(const u32 *payload, void *data)
{
	aie_interrupt(0, data);
}

/**
 * aie_part_has_error() - check if AI engine partition has errors raised
 * @apart: AIE partition pointer
 * @return: true if AI engine partition has errors, false otherwise.
 *
 * This function checkes the aperture struct @l2_mask field, this field will
 * be set when there is error interrupt generated from the SHIM NOC, and it
 * will be cleared in the partition errors backtrack. And thus, if it is set
 * it means there is error raised from the partition and backtrack is not done
 * yet.
 * It will request the aperture lock. Caller needs to make sure aperture lock
 * is released before calling this function.
 */
bool aie_part_has_error(struct aie_partition *apart)
{
	struct aie_aperture *aperture = apart->aperture;
	int ret;
	u32 *l2_mask = aperture->l2_mask.val;
	int i;

	ret = mutex_lock_interruptible(&aperture->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return false;
	}

	for (i = 0; i < aperture->l2_mask.count; i++) {
		if (l2_mask[i]) {
			ret = true;
			break;
		}
	}

	mutex_unlock(&aperture->mlock);
	return ret;
}

/**
 * aie_aperture_create_l2_mask() - create bitmaps to record mask and status
 *				     values for level 2 interrupt controllers.
 * @aperture: AI engine aperture
 * @return: 0 for success, and negative value for failure.
 */
int aie_aperture_create_l2_mask(struct aie_aperture *aperture)
{
	u32 num_nocs;

	num_nocs = aie_range_get_num_nocs(&aperture->range, aperture, NULL);
	if (!num_nocs)
		return 0;

	aperture->l2_mask.val = kcalloc(num_nocs, sizeof(*aperture->l2_mask.val),
					GFP_KERNEL);
	aperture->l2_mask.count = num_nocs;
	if (!aperture->l2_mask.val)
		return -ENOMEM;
	return 0;
}

/**
 * aie_get_module_error_count() - get the total count of errors in a module
 *				  from local bitmap.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @err_attr: error attribute for given module type.
 * @return: total number of errors found.
 */
u32 aie_get_module_error_count(struct aie_partition *apart,
			       struct aie_location loc,
			       enum aie_module_type module,
			       const struct aie_error_attr *err_attr)
{
	u32 count = 0;
	u8 i, j;

	for (i = 0; i < err_attr->num_err_categories; i++) {
		for (j = 0; j < err_attr->err_category[i].num_events; j++) {
			u8 event = err_attr->err_category[i].prop[j].event;

			if (aie_check_error_bitmap(apart, loc, module, event))
				count++;
		}
	}
	return count;
}

/**
 * aie_check_module_error() - check if a given module has an active error.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @err_attr: error attribute for given module type.
 * @return: true if tile has active errors.
 */
static bool aie_check_module_error(struct aie_partition *apart,
				   struct aie_location loc,
				   enum aie_module_type module,
				   const struct aie_error_attr *err_attr)
{
	u8 i, j;

	for (i = 0; i < err_attr->num_err_categories; i++) {
		for (j = 0; j < err_attr->err_category[i].num_events; j++) {
			u8 event = err_attr->err_category[i].prop[j].event;

			if (aie_check_error_bitmap(apart, loc, module, event))
				return true;
		}
	}
	return false;
}

/**
 * aie_check_tile_error() - check if a given tile location has an active error.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @return: true if tile has active errors.
 */
bool aie_check_tile_error(struct aie_partition *apart, struct aie_location loc)
{
	const struct aie_error_attr *core_errs = apart->adev->core_errors;
	const struct aie_error_attr *mem_errs = apart->adev->mem_errors;
	const struct aie_error_attr *shim_errs = apart->adev->shim_errors;
	u32 ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);

	if (ttype == AIE_TILE_TYPE_TILE) {
		if (aie_check_module_error(apart, loc, AIE_CORE_MOD, core_errs))
			return true;

		if (aie_check_module_error(apart, loc, AIE_MEM_MOD, mem_errs))
			return true;
	} else {
		if (aie_check_module_error(apart, loc, AIE_PL_MOD, shim_errs))
			return true;
	}
	return false;
}

/**
 * aie_get_error_count() - get the total count of errors in a partition from
 *			   local bitmap.
 * @apart: AIE partition pointer.
 * @return: total number of errors found.
 */
u32 aie_get_error_count(struct aie_partition *apart)
{
	const struct aie_error_attr *core_errs = apart->adev->core_errors;
	const struct aie_error_attr *mem_errs = apart->adev->mem_errors;
	const struct aie_error_attr *memtile_errs = apart->adev->memtile_errors;
	const struct aie_error_attr *shim_errs = apart->adev->shim_errors;
	struct aie_location loc;
	u32 ttype, num = 0;

	for (loc.col = apart->range.start.col;
	     loc.col < apart->range.start.col + apart->range.size.col;
	     loc.col++) {
		for (loc.row = apart->range.start.row;
		     loc.row < apart->range.size.row; loc.row++) {
			ttype = apart->adev->ops->get_tile_type(apart->adev,
								&loc);
			if (ttype == AIE_TILE_TYPE_TILE) {
				num += aie_get_module_error_count(apart, loc,
								  AIE_CORE_MOD,
								  core_errs);
				num += aie_get_module_error_count(apart, loc,
								  AIE_MEM_MOD,
								  mem_errs);
			} else if (ttype == AIE_TILE_TYPE_MEMORY) {
				num += aie_get_module_error_count(apart, loc,
								  AIE_MEM_MOD,
								  memtile_errs);
			} else {
				num += aie_get_module_error_count(apart, loc,
								  AIE_PL_MOD,
								  shim_errs);
			}
		}
	}

	return num;
}

/**
 * aie_get_errors_from_bitmap() - get status of errors from local bitmap.
 * @apart: AIE partition pointer.
 * @loc: tile location.
 * @module: module type.
 * @err_attr: error attribute for given module type.
 * @aie_err: pointer to array of error structure.
 * @return: total number of errors found.
 *
 * This function parses local bitmaps and initializes @aie_err structures with
 * the tile location of error event, module type and, its event ID.
 */
static u32 aie_get_errors_from_bitmap(struct aie_partition *apart,
				      struct aie_location loc,
				      enum aie_module_type module,
				      const struct aie_error_attr *err_attr,
				      struct aie_error *aie_err)
{
	u8 i, j;
	u32 num_err = 0;

	for (i = 0; i < err_attr->num_err_categories; i++) {
		const struct aie_err_category *category;

		category = &err_attr->err_category[i];
		for (j = 0; j < category->num_events; j++) {
			u8 event = category->prop[j].event;

			if (!aie_check_error_bitmap(apart, loc, module, event))
				continue;

			aie_err[num_err].loc.col = loc.col;
			aie_err[num_err].loc.row = loc.row;
			aie_err[num_err].module = module;
			aie_err[num_err].error_id = event;
			aie_err[num_err].category = category->err_category;
			num_err++;
		}
	}
	return num_err;
}

/**
 * aie_get_module_errors() - get errors for a given module type
 *			     in a partition.
 * @apart: AIE partition pointer.
 * @module: module type.
 * @aie_err: pointer to array of error structure.
 * @return: total number of errors found.
 *
 * This function parses local bitmaps and initializes @aie_err structures.
 */
static u32 aie_get_module_errors(struct aie_partition *apart,
				 enum aie_module_type module,
				 struct aie_error *aie_err)
{
	const struct aie_error_attr *err_attr;
	struct aie_location loc;
	u32 srow, erow, scol, ecol, num_err = 0;
	u32 ttype;

	if (module == AIE_CORE_MOD || module == AIE_MEM_MOD) {
		srow = apart->range.start.row + 1;
		erow = srow + apart->range.size.row - 1;
	} else {
		srow = 0;
		erow = 0;
	}

	scol = apart->range.start.col;
	ecol = apart->range.start.col + apart->range.size.col - 1;

	for (loc.col = scol; loc.col <= ecol; loc.col++) {
		for (loc.row = srow; loc.row <= erow; loc.row++) {
			ttype = apart->adev->ops->get_tile_type(apart->adev,
									&loc);
			if (ttype == AIE_TILE_TYPE_TILE) {
				if (module == AIE_CORE_MOD)
					err_attr = apart->adev->core_errors;
				else
					err_attr = apart->adev->mem_errors;
			} else if (ttype == AIE_TILE_TYPE_MEMORY) {
				if (module == AIE_MEM_MOD)
					err_attr = apart->adev->memtile_errors;
				else
					continue;
			} else {
				err_attr = apart->adev->shim_errors;
			}

			num_err +=
				aie_get_errors_from_bitmap(apart, loc,
							   module, err_attr,
							   &aie_err[num_err]);
		}
	}
	return num_err;
}

/**
 * aie_part_clear_cached_events() - clear cached events in a partition.
 * @apart: AIE partition pointer
 *
 * This function will clear the cached events in a partition.
 */
void aie_part_clear_cached_events(struct aie_partition *apart)
{
	aie_resource_clear_all(&apart->core_event_status);
	aie_resource_clear_all(&apart->mem_event_status);
	aie_resource_clear_all(&apart->pl_event_status);
}

/**
 * aie_part_set_intr_rscs() - set broadcast resources used by interrupt
 * @apart: AIE partition pointer
 * @return: 0 for sueccess, and negative value for failure
 *
 * This function reserves interrupt broadcast channels resources.
 */
int aie_part_set_intr_rscs(struct aie_partition *apart)
{
	u32 c, r;
	int ret;

	for (c = 0; c < apart->range.size.col; c++) {
		u32 b;
		struct aie_location l = {
			.col = apart->range.start.col + c,
			.row = 0,
		};

		/* reserved broadcast channels 0 - 5 for SHIM */
		for (b = 0; b <= AIE_SHIM_INTR_BC_MAX; b++) {
			ret = aie_part_rscmgr_set_tile_broadcast(apart, l,
								 AIE_PL_MOD,
								 b);
			if (ret)
				return ret;
		}

		for (r = 1; r < apart->range.size.row; r++) {
			struct aie_device *adev = apart->adev;
			struct aie_tile_attr *tattr;
			u32 m, ttype;

			b = AIE_ARRAY_TILE_ERROR_BC_ID;
			l.row = apart->range.start.row + r;
			ttype = adev->ops->get_tile_type(apart->adev, &l);

			if (WARN_ON(ttype >= AIE_TILE_TYPE_MAX))
				return -EINVAL;

			tattr = &adev->ttype_attr[ttype];
			for (m = 0; m < tattr->num_mods; m++) {
				enum aie_module_type mod = tattr->mods[m];

				ret = aie_part_rscmgr_set_tile_broadcast(apart,
									 l, mod,
									 b);
				if (ret)
					return ret;
			}
		}
	}

	return 0;
}

/**
 * aie_register_error_notification() - register a callback for error
 *				       notification.
 * @dev: AIE partition device.
 * @cb: pointer to a function that accepts pointer to private data as an
 *	argument. callbacks are called in the bottom half without locks.
 * @priv: private data to be passed to the callback.
 * @return: 0 for success, and negative value for failure.
 */
int aie_register_error_notification(struct device *dev,
				    void (*cb)(void *priv), void *priv)
{
	struct aie_partition *apart;
	int ret;

	if (!cb || !dev)
		return -EINVAL;

	apart = container_of(dev, struct aie_partition, dev);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return ret;
	}

	if (apart->error_cb.cb) {
		dev_err(&apart->dev,
			"Error callback already registered. Unregister the existing callback to register a new one.\n");
		ret = -EINVAL;
		goto exit;
	}

	apart->error_cb.cb = cb;
	apart->error_cb.priv = priv;

	/*
	 * Errors during configuration are logged even for the partitions
	 * which are not requested. Such errors must be reported back to the
	 * application when a valid callback is registered.
	 */
	if (apart->error_to_report) {
		mutex_unlock(&apart->mlock);
		schedule_work(&apart->aperture->backtrack);
		return ret;
	}

exit:
	mutex_unlock(&apart->mlock);
	return ret;
}
EXPORT_SYMBOL_GPL(aie_register_error_notification);

/**
 * aie_unregister_error_notification() - Unregister the callback for error
 *					 notification.
 * @dev: AIE partition device.
 * @return: 0 for success, and negative value for failure.
 */
int aie_unregister_error_notification(struct device *dev)
{
	struct aie_partition *apart;
	int ret;

	if (!dev)
		return -EINVAL;

	apart = container_of(dev, struct aie_partition, dev);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return ret;
	}

	apart->error_cb.cb = NULL;
	apart->error_cb.priv = NULL;

	mutex_unlock(&apart->mlock);
	return 0;
}
EXPORT_SYMBOL_GPL(aie_unregister_error_notification);

/**
 * aie_get_errors() - get errors that has happened.
 * @dev: AIE partition device.
 * @return: struct pointer of type aie_errors.
 *
 * This API allocates and initializes data structures by parsing local
 * bitmaps. Allocated data structure must be deallocated using
 * aie_free_errors() function.
 */
struct aie_errors *aie_get_errors(struct device *dev)
{
	struct aie_partition *apart;
	struct aie_errors *aie_errs;
	struct aie_error *error;
	u32 num_errs, count = 0;
	int ret;

	if (!dev)
		return ERR_PTR(-EINVAL);

	apart = container_of(dev, struct aie_partition, dev);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return ERR_PTR(ret);
	}

	num_errs = aie_get_error_count(apart);
	if (!num_errs) {
		mutex_unlock(&apart->mlock);
		return ERR_PTR(-EINVAL);
	}

	aie_errs = kzalloc(sizeof(*aie_errs), GFP_KERNEL);
	if (!aie_errs) {
		mutex_unlock(&apart->mlock);
		return ERR_PTR(-ENOMEM);
	}

	error = kcalloc(num_errs, sizeof(*error), GFP_KERNEL);
	if (!error) {
		kfree(aie_errs);
		mutex_unlock(&apart->mlock);
		return ERR_PTR(-ENOMEM);
	}

	count += aie_get_module_errors(apart, AIE_MEM_MOD, &error[count]);
	count += aie_get_module_errors(apart, AIE_CORE_MOD, &error[count]);
	count += aie_get_module_errors(apart, AIE_PL_MOD, &error[count]);

	aie_errs->dev = dev;
	aie_errs->errors = error;
	aie_errs->num_err = count;

	mutex_unlock(&apart->mlock);
	return aie_errs;
}
EXPORT_SYMBOL_GPL(aie_get_errors);

/**
 * aie_get_error_categories() - Get the error categories. Error information
 *				returned by aie_get_errors() could be
 *				abstracted by classifying errors into various
 *				categories. All DMA channel error are
 *				classified as AIE_ERROR_CATEGORY_DMA, program
 *				and data memory ECC errors are classified as
 *				AIE_ERROR_CATEGORY_ECC, and so on.
 * @aie_errs: AIE errors structure.
 * @return: Bitmap of category of error events.
 */
u32 aie_get_error_categories(struct aie_errors *aie_errs)
{
	u32 e, ret = 0;

	if (!aie_errs || !aie_errs->errors)
		return 0;

	for (e = 0; e < aie_errs->num_err; e++) {
		struct aie_error *error = &aie_errs->errors[e];

		ret |= BIT(error->category);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(aie_get_error_categories);

/**
 * aie_get_error_string() - get error string corresponding an error.
 * @aie_errs: pointer to an array of error structure.
 * @aie_err: AIE error.
 * @return: string corresponding to @aie_err.
 */
const char *aie_get_error_string(struct aie_errors *aie_errs,
				 struct aie_error *aie_err)
{
	struct aie_partition *apart;
	const struct aie_error_attr *err_attr;
	u8 i, j;
	int ret;

	if (!aie_errs || !aie_errs->dev || !aie_err)
		return ERR_PTR(-EINVAL);

	apart = container_of(aie_errs->dev, struct aie_partition, dev);

	ret = mutex_lock_interruptible(&apart->mlock);
	if (ret) {
		dev_err(&apart->dev,
			"Failed to acquire lock. Process was interrupted by fatal signals\n");
		return ERR_PTR(ret);
	}

	if (aie_err->module == AIE_CORE_MOD)
		err_attr = apart->adev->core_errors;
	else if (aie_err->module == AIE_MEM_MOD)
		err_attr = apart->adev->mem_errors;
	else
		err_attr = apart->adev->shim_errors;

	for (i = 0; i < err_attr->num_err_categories; i++) {
		for (j = 0; j < err_attr->err_category[i].num_events; j++) {
			u8 event = err_attr->err_category[i].prop[j].event;

			if (event != aie_err->error_id)
				continue;

			mutex_unlock(&apart->mlock);
			return err_attr->err_category[i].prop[j].event_str;
		}
	}

	mutex_unlock(&apart->mlock);
	return NULL;
}
EXPORT_SYMBOL_GPL(aie_get_error_string);

/**
 * aie_flush_errors() - Flush all pending errors.
 * @dev: AIE partition device.
 * @return: 0 for success, and negative value for failure.
 *
 * This function backtracks a given partition, updates local event status
 * bitmaps and, invokes the registered callback function for any error event.
 */
int aie_flush_errors(struct device *dev)
{
	struct aie_partition *apart;

	if (!dev)
		return -EINVAL;

	apart = container_of(dev, struct aie_partition, dev);
	aie_part_backtrack(apart);

	return 0;
}
EXPORT_SYMBOL_GPL(aie_flush_errors);

/**
 * aie_free_errors() - Free allocated AIE error structure.
 * @aie_errs: AIE error structure.
 */
void aie_free_errors(struct aie_errors *aie_errs)
{
	if (!aie_errs || !aie_errs->errors)
		return;

	kfree(aie_errs->errors);
	kfree(aie_errs);
}
EXPORT_SYMBOL_GPL(aie_free_errors);

static int aie_intr_ctrl_l1_broadcast_block(struct aie_partition *apart,
					    struct aie_location loc,
					    enum aie_shim_switch_type sw,
					    u32 bcast_bitmap)
{
	const struct aie_l1_intr_ctrl_attr *l1_ctrl;
	u32 regoff;

	l1_ctrl = apart->adev->l1_ctrl;

	if (!l1_ctrl) {
		dev_err(&apart->dev, "%s: %d: no l1 ctrl for [%d, %d]: sw: %d bcast_bitmap: 0x%x",
			__func__, __LINE__, loc.col, loc.row, sw, bcast_bitmap);
		return -ENODEV;
	}
	switch (sw) {
	case AIE_SHIM_SWITCH_A:
		regoff = l1_ctrl->block_north_a_set.regoff;
		break;
	case AIE_SHIM_SWITCH_B:
		regoff = l1_ctrl->block_north_b_set.regoff;
		break;
	default:
		dev_err(&apart->dev, "%s: %d: invalid sw: [%d, %d]: sw: %d bcast_bitmap: 0x%x",
			__func__, __LINE__, loc.col, loc.row, sw, bcast_bitmap);
		return -ENODEV;
	}
	regoff += l1_ctrl->regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);

	iowrite32(bcast_bitmap, apart->aperture->base + regoff);
	return 0;
}

static int aie_enable_l1_intr(struct aie_partition *apart,
			      struct aie_location loc,
			      enum aie_shim_switch_type sw,
			      u8 irq_id)
{
	const struct aie_l1_intr_ctrl_attr *l1_ctrl;
	u32 regoff;

	l1_ctrl = apart->adev->l1_ctrl;

	if (!l1_ctrl) {
		dev_err(&apart->dev, "l1 ctrl enabled failed: no l1 ctrl: [%d, %d]: sw: %d irq_id: %d",
			loc.col, loc.row, sw, irq_id);
		return -ENODEV;
	}
	switch (sw) {
	case AIE_SHIM_SWITCH_A:
		regoff = l1_ctrl->enable_a.regoff;
		break;
	case AIE_SHIM_SWITCH_B:
		regoff = l1_ctrl->enable_b.regoff;
		break;
	default:
		dev_err(&apart->dev, "l1 ctrl enabled failed: invalid sw: [%d, %d]: sw: %d irq_id: %d",
			loc.col, loc.row, sw, irq_id);
		return -ENODEV;
	}
	regoff += l1_ctrl->regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);

	iowrite32(BIT(irq_id), apart->aperture->base + regoff);
	return 0;
}

static int aie_set_l1_ctrl_irq_id(struct aie_partition *apart,
				  struct aie_location loc,
				  enum aie_shim_switch_type sw,
				  u8 irq_id)
{
	const struct aie_l1_intr_ctrl_attr *l1_ctrl;
	u32 regoff;

	l1_ctrl = apart->adev->l1_ctrl;

	if (!l1_ctrl) {
		dev_err(&apart->dev, "%s: %d: no l1 ctrl: [%d, %d]: sw: %d irq_id: %d",
			__func__, __LINE__, loc.col, loc.row, sw, irq_id);
		return -ENODEV;
	}
	switch (sw) {
	case AIE_SHIM_SWITCH_A:
		regoff = l1_ctrl->irq_no_a.regoff;
		break;
	case AIE_SHIM_SWITCH_B:
		regoff = l1_ctrl->irq_no_b.regoff;
		break;
	default:
		dev_err(&apart->dev, "%s: %d: invalid sw: [%d, %d]: sw: %d irq_id: %d",
			__func__, __LINE__, loc.col, loc.row, sw, irq_id);

		return -ENODEV;
	}
	regoff += l1_ctrl->regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);

	iowrite32(irq_id, apart->aperture->base + regoff);
	return 0;
}

static int aie_set_l1_ctrl_irq_event(struct aie_partition *apart, struct aie_location loc,
				     enum aie_shim_switch_type sw, u8 irq_id, u8 event)
{
	const struct aie_l1_intr_ctrl_attr *intr_ctrl = apart->adev->l1_ctrl;
	u32 regval, regoff, irq_event;

	if (irq_id > sizeof(u32) / sizeof(u8))
		return -EINVAL;

	regval = event << (BITS_PER_BYTE * irq_id);
	switch (sw) {
	case AIE_SHIM_SWITCH_A:
		regoff = intr_ctrl->irq_event_a.regoff;
		break;
	case AIE_SHIM_SWITCH_B:
		regoff = intr_ctrl->irq_event_b.regoff;
		break;
	default:
		return -EINVAL;
	}
	regoff += intr_ctrl->regoff;
	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
	irq_event = ioread32(apart->aperture->base + regoff);
	irq_event &= ~(0xFF << (BITS_PER_BYTE * irq_id));
	regval |= irq_event;
	iowrite32(regval, apart->aperture->base + regoff);

	return 0;
}

static int aie2ps_init_l1_ctrl(struct aie_partition *apart, struct aie_location loc)
{
	const struct aie_event_attr *attr;
	int ret;
	u32 bcast_bitmap;

	attr = apart->adev->pl_events;

	switch (loc.col - apart->range.start.col) {
	case 1:
		bcast_bitmap = BIT(AIE_SHIM_UC_EVENT_BC_ID) |
			       BIT(AIE_SHIM_USER_EVENT1_BC_ID);
		ret = aie_intr_ctrl_l1_broadcast_block(apart, loc, AIE_SHIM_SWITCH_A,
						       bcast_bitmap);
		if (ret)
			return ret;
		ret = aie_intr_ctrl_l1_broadcast_block(apart, loc, AIE_SHIM_SWITCH_B,
						       bcast_bitmap);
		if (ret)
			return ret;

		ret = aie_set_l1_ctrl_irq_event(apart, loc, AIE_SHIM_SWITCH_A,
						AIE_SHIM_USER_EVENT1_L1_IRQ_EVENT_ID,
						attr->user_event1);
		if (ret)
			return ret;
		ret = aie_enable_l1_intr(apart, loc, AIE_SHIM_SWITCH_A,
					 AIE_SHIM_USER_EVENT1_IRQ_ID);
		if (ret)
			return ret;
		ret = aie_set_l1_ctrl_irq_id(apart, loc, AIE_SHIM_SWITCH_A,
					     AIE_SHIM_USER_EVENT1_BC_ID);
		if (ret)
			return ret;
		break;
	default:
		bcast_bitmap = BIT(AIE_ARRAY_TILE_ERROR_BC_ID) |
			       BIT(AIE_SHIM_UC_EVENT_BC_ID) |
			       BIT(AIE_SHIM_USER_EVENT1_BC_ID);
		ret = aie_intr_ctrl_l1_broadcast_block(apart, loc, AIE_SHIM_SWITCH_A,
						       bcast_bitmap);
		if (ret)
			return ret;
		ret = aie_intr_ctrl_l1_broadcast_block(apart, loc, AIE_SHIM_SWITCH_B,
						       bcast_bitmap);
		if (ret)
			return ret;

		ret = aie_enable_l1_intr(apart, loc, AIE_SHIM_SWITCH_A,
					 AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;
		ret = aie_enable_l1_intr(apart, loc, AIE_SHIM_SWITCH_B,
					 AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;
		ret = aie_set_l1_ctrl_irq_event(apart, loc, AIE_SHIM_SWITCH_A,
						AIE_SHIM_TILE_ERROR_L1_IRQ_EVENT_ID,
						attr->base_error_group);
		if (ret)
			return ret;
		ret = aie_enable_l1_intr(apart, loc, AIE_SHIM_SWITCH_A,
					 AIE_SHIM_TILE_ERROR_IRQ_ID);
		if (ret)
			return ret;
		ret = aie_set_l1_ctrl_irq_id(apart, loc, AIE_SHIM_SWITCH_A,
					     AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;
	}

	return 0;
}

static int aie_event_bc_block(struct aie_partition *apart, struct aie_location loc,
			      enum aie_shim_switch_type sw, u32 bcast_mask, u8 dir)
{
	struct aie_event_bc_block *block;
	u32 ttype;
	u64 regoff;

	ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
	switch (ttype) {
	case AIE_TILE_TYPE_SHIMNOC:
	case AIE_TILE_TYPE_SHIMPL:
		regoff = sw == AIE_SHIM_SWITCH_A ? apart->adev->pl_events->bc_block_a.regoff :
						  apart->adev->pl_events->bc_block_b.regoff;
		break;
	case AIE_TILE_TYPE_TILE:
		regoff = sw == AIE_SHIM_SWITCH_A ? apart->adev->core_events->bc_block_a.regoff :
						  apart->adev->mem_events->bc_block_b.regoff;

		break;
	case AIE_TILE_TYPE_MEMORY:
		regoff = sw == AIE_SHIM_SWITCH_A ? apart->adev->memtile_events->bc_block_a.regoff :
						  apart->adev->memtile_events->bc_block_b.regoff;

		break;
	default:
		dev_err(&apart->dev, "%s: %d: Unknown tile type for [%d, %d]: %d",
			__func__, __LINE__, loc.col, loc.row, ttype);
		return -ENODEV;
	}

	block = (struct aie_event_bc_block *)regoff;

	if (dir & AIE_EVENT_BROADCAST_SOUTH) {
		regoff = (u64)&block->south_set;
		regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
		iowrite32(bcast_mask, apart->aperture->base + regoff);
	}
	if (dir & AIE_EVENT_BROADCAST_WEST) {
		regoff = (u64)&block->west_set;
		regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
		iowrite32(bcast_mask, apart->aperture->base + regoff);
	}
	if (dir & AIE_EVENT_BROADCAST_NORTH) {
		regoff = (u64)&block->north_set;
		regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
		iowrite32(bcast_mask, apart->aperture->base + regoff);
	}
	if (dir & AIE_EVENT_BROADCAST_EAST) {
		regoff = (u64)&block->east_set;
		regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
		iowrite32(bcast_mask, apart->aperture->base + regoff);
	}

	return 0;
}

static int aie2ps_init_shim_tile_lead_col(struct aie_partition *apart, struct aie_location loc)
{
	u32 bcast_mask;
	u32 dir;
	int ret;

	bcast_mask = BIT(AIE_ARRAY_TILE_ERROR_BC_ID) |
		     BIT(AIE_SHIM_UC_EVENT_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_EAST |
	      AIE_EVENT_BROADCAST_SOUTH;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_WEST |
	      AIE_EVENT_BROADCAST_SOUTH;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_ALL;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	return 0;
}

static int aie2ps_init_shim_tile(struct aie_partition *apart, struct aie_location loc)
{
	u32 bcast_mask;
	u32 dir;
	int ret;

	bcast_mask = BIT(AIE_ARRAY_TILE_ERROR_BC_ID) |
		     BIT(AIE_SHIM_UC_EVENT_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_WEST |
	      AIE_EVENT_BROADCAST_SOUTH;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	dir = AIE_EVENT_BROADCAST_ALL;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_EAST |
	      AIE_EVENT_BROADCAST_SOUTH;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	return 0;
}

static int aie2ps_init_shim_tile_col0(struct aie_partition *apart, struct aie_location loc)
{
	u32 bcast_mask;
	u32 dir;
	int ret;

	bcast_mask = BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_WEST |
	      AIE_EVENT_BROADCAST_SOUTH;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_ARRAY_TILE_ERROR_BC_ID) |
		     BIT(AIE_SHIM_UC_EVENT_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_WEST |
	      AIE_EVENT_BROADCAST_SOUTH;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	dir = AIE_EVENT_BROADCAST_ALL;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	return 0;
}

static int aie2ps_init_aie_tile(struct aie_partition *apart, struct aie_location loc)
{
	u32 bcast_mask;
	u32 dir;
	int ret;

	bcast_mask = BIT(AIE_ARRAY_TILE_ERROR_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_EAST |
	      AIE_EVENT_BROADCAST_WEST;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_SHIM_UC_EVENT_BC_ID) |
		     BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_ALL;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	return 0;
}

static int aie2ps_init_mem_tile(struct aie_partition *apart, struct aie_location loc)
{
	u32 bcast_mask;
	u32 dir;
	int ret;

	bcast_mask = BIT(AIE_ARRAY_TILE_ERROR_BC_ID);
	dir = AIE_EVENT_BROADCAST_NORTH |
	      AIE_EVENT_BROADCAST_EAST |
	      AIE_EVENT_BROADCAST_WEST;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	bcast_mask = BIT(AIE_SHIM_UC_EVENT_BC_ID) |
		     BIT(AIE_SHIM_USER_EVENT1_BC_ID);
	dir = AIE_EVENT_BROADCAST_ALL;
	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_A, bcast_mask, dir);
	if (ret)
		return ret;

	ret = aie_event_bc_block(apart, loc, AIE_SHIM_SWITCH_B, bcast_mask, dir);
	if (ret)
		return ret;

	return 0;
}

static int aie_config_error_halt_event(struct aie_partition *apart)
{
	u32 ttype;
	struct aie_location loc;
	u32 start_col = apart->range.start.col;
	u32 end_col = start_col + apart->range.size.col;
	const struct aie_event_attr *attr;
	u32 event_regoff;
	u32 regoff;
	u32 val;

	attr = apart->adev->core_events;
	event_regoff = attr->error_halt_event.regoff;
	val = attr->error_halt_event_group;
	if (!val || !event_regoff) {
		dev_err(&apart->dev, "%s: %d: No error halt event present",
			__func__, __LINE__);
		return -ENODEV;
	}

	for (loc.col = start_col; loc.col < end_col; loc.col++) {
		for (loc.row = 0; loc.row < apart->range.size.row; loc.row++) {
			if (!aie_part_check_clk_enable_loc(apart, &loc))
				continue;

			ttype = apart->adev->ops->get_tile_type(apart->adev,
								&loc);
			if (ttype != AIE_TILE_TYPE_TILE)
				continue;
			regoff = aie_aperture_cal_regoff(apart->aperture, loc,
							 event_regoff);
			iowrite32(val, apart->aperture->base + regoff);
		}
	}
	return 0;
}

static int aie2ps_priv_error_handling_init(struct aie_partition *apart)
{
	struct aie_range range = {};
	int ret = 0;
	u16 data;

	/*
	 * Set NOC L2 interrupt
	 *
	 * For col 1, use irq 2 or 3.
	 * For rest of the cols, use irq 1.
	 */
	range.start.col = apart->range.start.col;
	range.size.col = 1;
	data = 1;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	range.start.col = apart->range.start.col + 1;
	range.size.col = 1;
	data = (apart->partition_id % AIE_USER_EVENT1_NUM_IRQ) + 2;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	range.start.col = apart->range.start.col + 2;
	range.size.col = apart->range.size.col - 2;
	data = 1;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_SET_L2_IRQ, range, 0);
	if (ret)
		return ret;

	/**
	 * Set HW error NPI intr
	 * Use npi interrupt 1 for all hw errors.
	 */
	data = 1;
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_HW_ERR_INT, apart->range, 0);
	if (ret)
		return ret;
	/*
	 * set HW error mask
	 * Mask Hw_Correctable_Errors - BIT(1)
	 */
	data = BIT(1);
	ret = aie_part_pm_ops(apart, &data, AIE_PART_INIT_OPT_HW_ERR_MASK, apart->range, 1);

	return ret;
}

static int aie_set_broadcast_event(struct aie_partition *apart,
				   struct aie_location loc,
				   const struct aie_event_attr *attr,
				   u32 error_group, u8 bc_id)
{
	u32 regoff;

	if (!error_group) {
		dev_err(&apart->dev, "%s: %d: No error group present for [%d, %d]",
			__func__, __LINE__, loc.col, loc.row);
		return -ENODEV;
	}
	if (bc_id >= attr->num_broadcasts) {
		dev_err(&apart->dev, "%s: %d: invalid bc_id: %d for [%d, %d]",
			__func__, __LINE__, bc_id, loc.col, loc.row);
		return -ENODEV;
	}

	regoff = attr->bc_regoff + attr->bc_event.regoff + bc_id * 4U;
	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
	iowrite32(error_group, apart->aperture->base + regoff);
	return 0;
}

static int aie_event_group_error0_enable(struct aie_partition *apart,
					 struct aie_location loc,
					 const struct aie_event_attr *attr)
{
	u32 regoff;
	u32 val;

	if (!attr) {
		dev_err(&apart->dev, "%s: %d: attr not found for [%d, %d]",
			__func__, __LINE__, loc.col, loc.row);
		return -ENODEV;
	}
	regoff = attr->event_group_error0_enable.regoff;
	val = attr->event_group_error0_enable_default;
	if (!val || !regoff) {
		dev_err(&apart->dev, "%s: %d: regoff and val for [%d, %d]",
			__func__, __LINE__, loc.col, loc.row);
		return -ENODEV;
	}

	regoff = aie_aperture_cal_regoff(apart->aperture, loc, regoff);
	iowrite32(val, apart->aperture->base + regoff);
	return 0;
}

static int aie_group_error_init_loc(struct aie_partition *apart,
				    struct aie_location loc)
{
	int ret = 0;
	u32 ttype;
	const struct aie_event_attr *attr;

	if (!aie_part_check_clk_enable_loc(apart, &loc))
		return 0;
	ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
	switch (ttype) {
	case AIE_TILE_TYPE_SHIMNOC:
	case AIE_TILE_TYPE_SHIMPL:
		attr = apart->adev->pl_events;
		ret = aie_event_group_error0_enable(apart, loc, attr);
		if (ret)
			return ret;
		ret = aie_set_broadcast_event(apart, loc, attr,
					      attr->base_error_group,
					      AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;
		if (loc.col != (apart->range.start.col + 1)) {
			ret = aie_set_broadcast_event(apart, loc, attr,
						      attr->user_event1,
						      AIE_SHIM_USER_EVENT1_BC_ID);
			if (ret)
				return ret;
		}

		break;
	case AIE_TILE_TYPE_TILE:
		attr = apart->adev->mem_events;
		ret = aie_event_group_error0_enable(apart, loc, attr);
		if (ret)
			return ret;
		ret = aie_set_broadcast_event(apart, loc, attr,
					      attr->base_error_group,
					      AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;

		attr = apart->adev->core_events;
		ret = aie_event_group_error0_enable(apart, loc, attr);
		if (ret)
			return ret;
		ret = aie_set_broadcast_event(apart, loc, attr,
					      attr->base_error_group,
					      AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;

		break;
	case AIE_TILE_TYPE_MEMORY:
		attr = apart->adev->memtile_events;
		ret = aie_event_group_error0_enable(apart, loc, attr);
		if (ret)
			return ret;
		ret = aie_set_broadcast_event(apart, loc, attr,
					      attr->base_error_group,
					      AIE_ARRAY_TILE_ERROR_BC_ID);
		if (ret)
			return ret;
		break;
	default:
		dev_err(&apart->dev, "Invalid tile type for [%d, %d]: %d",
			loc.col, loc.row, ttype);
		return -ENODEV;
	}

	return ret;
}

static int aie_group_error_init(struct aie_partition *apart)
{
	int ret;
	struct aie_location loc;
	u32 start_col = apart->range.start.col;
	u32 end_col = start_col + apart->range.size.col;

	for (loc.col = start_col; loc.col < end_col; loc.col++) {
		for (loc.row = 0; loc.row < apart->range.size.row; loc.row++) {
			ret = aie_group_error_init_loc(apart, loc);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int aie2ps_error_handling_init(struct aie_partition *apart)
{
	int ret = 0;
	u32 ttype;
	struct aie_location loc;
	u32 l2_enable;
	u32 start_col = apart->range.start.col;
	u32 end_col = start_col + apart->range.size.col;

	for (loc.col = start_col; loc.col < end_col; loc.col++) {
		for (loc.row = 0; loc.row < apart->range.size.row; loc.row++) {
			if (!aie_part_check_clk_enable_loc(apart, &loc))
				continue;

			ttype = apart->adev->ops->get_tile_type(apart->adev, &loc);
			switch (ttype) {
			case AIE_TILE_TYPE_SHIMNOC:
			case AIE_TILE_TYPE_SHIMPL:
				ret = aie2ps_init_l1_ctrl(apart, loc);
				if (ret)
					goto out;
				if (loc.col == (start_col + 1))
					l2_enable = BIT(AIE_SHIM_USER_EVENT1_L1_IRQ_EVENT_ID);
				else
					l2_enable = BIT(AIE_ARRAY_TILE_ERROR_BC_ID);
				aie_aperture_enable_l2_ctrl(apart->aperture, &loc, l2_enable);
				if (loc.col == (start_col + 1)) {
					ret = aie2ps_init_shim_tile_lead_col(apart, loc);
					if (ret)
						goto out;
				} else if (loc.col == start_col) {
					ret = aie2ps_init_shim_tile_col0(apart, loc);
					if (ret)
						goto out;
				} else {
					ret = aie2ps_init_shim_tile(apart, loc);
					if (ret)
						goto out;
				}
				break;
			case AIE_TILE_TYPE_TILE:
				ret = aie2ps_init_aie_tile(apart, loc);
				if (ret)
					goto out;
				break;
			case AIE_TILE_TYPE_MEMORY:
				ret = aie2ps_init_mem_tile(apart, loc);
				if (ret)
					goto out;
				break;
			default:
				dev_err(&apart->dev, "Invalid tile type for [%d, %d]: %d",
					loc.col, loc.row, ttype);
				ret = -ENODEV;
				break;
			}
		}
	}
	ret = aie2ps_priv_error_handling_init(apart);

out:
	return ret;
}

int aie_error_handling_init(struct aie_partition *apart)
{
	int ret;

	switch (apart->adev->dev_gen) {
	case AIE_DEVICE_GEN_AIE2PS:
		ret = aie2ps_error_handling_init(apart);
		break;
	default:
		return 0;
	}

	if (ret)
		return ret;
	ret = aie_group_error_init(apart);
	if (ret)
		return ret;
	ret = aie_config_error_halt_event(apart);

	return ret;
}
