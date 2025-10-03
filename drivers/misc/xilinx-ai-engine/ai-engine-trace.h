/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xilinx_ai_engine

#if !defined(_AI_ENGINE_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _AI_ENGINE_TRACE_H_

#include <linux/tracepoint.h>

#include "ai-engine-internal.h"

TRACE_EVENT(aie_part_initialize,
	TP_PROTO(struct aie_partition *apart, __u32 opts, __u32 num_tiles),
	TP_ARGS(apart, opts, num_tiles),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, opts)
		__field(__u32, num_tiles)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->opts = opts;
		__entry->num_tiles = num_tiles;
	),
	TP_printk("id: %d opts:  0x%x, num_tiles: %d",
		  __entry->partition_id, __entry->opts, __entry->num_tiles)
);

TRACE_EVENT(aie_part_ioctl,
	TP_PROTO(struct aie_partition *apart, __u32 cmd),
	TP_ARGS(apart, cmd),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, cmd)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->cmd = cmd;
	),
	TP_printk("id: %d cmd: %d", __entry->partition_id, __entry->cmd)
);

TRACE_EVENT(aie_part_access_reg,
	TP_PROTO(struct aie_partition *apart, __u32 cmd),
	TP_ARGS(apart, cmd),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, cmd)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->cmd = cmd;
	),
	TP_printk("id: %d cmd: %d", __entry->partition_id, __entry->cmd)
);

TRACE_EVENT(aie_part_request_tiles,
	TP_PROTO(struct aie_partition *apart, int num_tiles),
	TP_ARGS(apart, num_tiles),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(int, num_tiles)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->num_tiles = num_tiles;
	),
	TP_printk("id: %d  num_tiles: %d", __entry->partition_id, __entry->num_tiles)
);

TRACE_EVENT(aie_part_request_tile,
	TP_PROTO(struct aie_partition *apart, struct aie_location loc),
	TP_ARGS(apart, loc),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc.col;
		__entry->row = loc.row;
	),
	TP_printk("id: %d  loc: [%d, %d]", __entry->partition_id, __entry->col, __entry->row)
);

TRACE_EVENT(aie_part_release_tiles,
	TP_PROTO(struct aie_partition *apart, int num_tiles),
	TP_ARGS(apart, num_tiles),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(int, num_tiles)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->num_tiles = num_tiles;
	),
	TP_printk("id: %d  num_tiles: %d", __entry->partition_id, __entry->num_tiles)
);

TRACE_EVENT(aie_part_release_tile,
	TP_PROTO(struct aie_partition *apart, struct aie_location loc),
	TP_ARGS(apart, loc),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc.col;
		__entry->row = loc.row;
	),
	TP_printk("id: %d  loc: [%d, %d]", __entry->partition_id, __entry->col, __entry->row)
);

TRACE_EVENT(aie_part_set_column_clock_from_user,
	TP_PROTO(struct aie_partition *apart, struct aie_column_args *args),
	TP_ARGS(apart, args),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
		__field(__u8, enable)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = args->start_col;
		__entry->num_cols = args->num_cols;
		__entry->enable = args->enable;
	),
	TP_printk("id: %d  start_col: %u num_cols: %u enable: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols, __entry->enable)
);

TRACE_EVENT(aie_part_initialize_tiles,
	TP_PROTO(struct aie_partition *apart, struct aie_location loc),
	TP_ARGS(apart, loc),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc.col;
		__entry->row = loc.row;
	),
	TP_printk("id: %d  [%d, %d]", __entry->partition_id, __entry->col, __entry->row)
);

TRACE_EVENT(aie_part_set_shimdma_bd,
	TP_PROTO(struct aie_partition *apart, struct aie_location loc, u32 bd_id, u32 bd, u32 i),
	TP_ARGS(apart, loc, bd_id, bd, i),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
		__field(__u32, bd_id)
		__field(__u32, bd)
		__field(__u32, i)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc.col;
		__entry->row = loc.row;
		__entry->bd_id = bd_id;
		__entry->bd = bd;
		__entry->i = i;
	),
	TP_printk("id: %d  [%d, %d]: bd_id: %d bd[%d]: 0x%x",
		  __entry->partition_id, __entry->col, __entry->row, __entry->bd_id, __entry->i,
		  __entry->bd)
);

TRACE_EVENT(aie_part_write_register,
	TP_PROTO(struct aie_partition *apart, size_t offset,
		size_t len, void *data, u32 mask),
	TP_ARGS(apart, offset, len, data, mask),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(size_t, offset)
		__field(size_t, len)
		__field(void *, data)
		__field(__u32, mask)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->offset = offset;
		__entry->len = len;
		__entry->data = data;
		__entry->mask = mask;
	),
	TP_printk("id: %d  offset: %zx, len: %zx, data: 0x%pK, mask: 0x%x",
		  __entry->partition_id, __entry->offset, __entry->len,
		  __entry->data, __entry->mask)
);

TRACE_EVENT(aie_part_write_register_data,
	TP_PROTO(struct aie_partition *apart, u32 index, u32 data, size_t offset),
	TP_ARGS(apart, index, data, offset),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, data)
		__field(__u32, index)
		__field(size_t, regoff)
		__field(__u32, col)
		__field(__u32, row)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->data = data;
		__entry->index = index;
		__entry->regoff = aie_cal_tile_reg(apart->adev, offset);
		__entry->col = aie_tile_reg_field_get(aie_col_mask(apart->adev),
						      apart->adev->col_shift, offset);
		__entry->row = aie_tile_reg_field_get(aie_row_mask(apart->adev),
						      apart->adev->row_shift, offset);
	),
	TP_printk("id: %d  [%d, %d]: regoff: 0x%zx data[%d]: 0x%x", __entry->partition_id,
		  __entry->col, __entry->row, __entry->regoff, __entry->index, __entry->data)
);

TRACE_EVENT(aie_part_attach_dmabuf_req,
	TP_PROTO(struct aie_partition *apart, int fd),
	TP_ARGS(apart, fd),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(int, fd)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->fd = fd;
	),
	TP_printk("id: %d fd: %d", __entry->partition_id, __entry->fd)
);

TRACE_EVENT(aie_part_detach_dmabuf_req,
	TP_PROTO(struct aie_partition *apart, int fd),
	TP_ARGS(apart, fd),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(int, fd)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->fd = fd;
	),
	TP_printk("id: %d fd: %d", __entry->partition_id, __entry->fd)
);

TRACE_EVENT(aie_part_rsc_req_rsp,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc_req_rsp *req),
	TP_ARGS(apart, req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u8, col)
		__field(__u8, row)
		__field(__u32, mod)
		__field(__u32, type)
		__field(__u32, num_rscs)
		__field(__u8, flag)
		__field(__u64, rscs)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = req->req.loc.col;
		__entry->row = req->req.loc.row;
		__entry->mod = req->req.mod;
		__entry->type = req->req.type;
		__entry->num_rscs = req->req.num_rscs;
		__entry->flag = req->req.flag;
		__entry->rscs = req->rscs;
	),
	TP_printk("id: %d [%d, %d]: mod: %d type: %d num_rscs: %d flag: %d rscs: 0x%llx",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->type, __entry->num_rscs, __entry->flag, __entry->rscs)
);

TRACE_EVENT(aie_part_rsc_req,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc_req *req),
	TP_ARGS(apart, req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u8, col)
		__field(__u8, row)
		__field(__u32, mod)
		__field(__u32, type)
		__field(__u32, num_rscs)
		__field(__u8, flag)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = req->loc.col;
		__entry->row = req->loc.row;
		__entry->mod = req->mod;
		__entry->type = req->type;
		__entry->num_rscs = req->num_rscs;
		__entry->flag = req->flag;
	),
	TP_printk("id: %d [%d, %d]: mod: %d type: %d num_rscs: %d flag: %d",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->type, __entry->num_rscs, __entry->flag)
);

TRACE_EVENT(aie_part_rsc_bc_rsp,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc_bc_req *req),
	TP_ARGS(apart, req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u64, rscs)
		__field(__u32, num_rscs)
		__field(__u32, flag)
		__field(__u32, id)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->rscs = req->rscs;
		__entry->num_rscs = req->num_rscs;
		__entry->flag = req->flag;
		__entry->id = req->id;
	),
	TP_printk("id: %d rscs: 0x%llx num_rscs: %d flag: 0x%x id: %d",
		  __entry->partition_id, __entry->rscs, __entry->num_rscs,
		  __entry->flag, __entry->id)
);

TRACE_EVENT(aie_part_rsc_user_stat_array,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc_user_stat_array *req),
	TP_ARGS(apart, req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u64, stats)
		__field(__u32, num_stats)
		__field(__u32, stats_type)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->stats = req->stats;
		__entry->num_stats = req->num_stats;
		__entry->stats_type = req->stats_type;
	),
	TP_printk("id: %d stats: 0x%llx num_stats: %d stats_type: %d",
		  __entry->partition_id, __entry->stats, __entry->num_stats,
		  __entry->stats_type)
);

TRACE_EVENT(aie_part_user_stat,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc_user_stat *req),
	TP_ARGS(apart, req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u8, col)
		__field(__u8, row)
		__field(__u8, mod)
		__field(__u8, type)
		__field(__u8, num_rscs)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = req->loc.col;
		__entry->row = req->loc.row;
		__entry->mod = req->mod;
		__entry->type = req->type;
		__entry->num_rscs = req->num_rscs;
	),
	TP_printk("id: %d [%d, %d]: mod: %d type: %d num_rscs: %d",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->type, __entry->num_rscs)
);

TRACE_EVENT(aie_part_rsc,
	TP_PROTO(struct aie_partition *apart, struct aie_rsc *rsc),
	TP_ARGS(apart, rsc),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u8, col)
		__field(__u8, row)
		__field(__u32, mod)
		__field(__u32, type)
		__field(__u32, id)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = rsc->loc.col;
		__entry->row = rsc->loc.row;
		__entry->mod = rsc->mod;
		__entry->type = rsc->type;
		__entry->id = rsc->id;
	),
	TP_printk("id: %d [%d, %d]: mod: %d type: %d id: %d",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->type, __entry->id)
);

TRACE_EVENT(xilinx_ai_engine_ioctl,
	TP_PROTO(struct aie_device *adev, unsigned int cmd),
	TP_ARGS(adev, cmd),
	TP_STRUCT__entry(
		__string(devname, dev_name(&adev->dev))
		__field(unsigned int, cmd)
	),
	TP_fast_assign(
		__assign_str(devname);
		__entry->cmd = cmd;
	),
	TP_printk("%s: cmd: %d NR: %d",
		  __get_str(devname), __entry->cmd, _IOC_NR(__entry->cmd))
);

TRACE_EVENT(aie_interrupt,
	TP_PROTO(struct aie_device *adev),
	TP_ARGS(adev),
	TP_STRUCT__entry(
		__string(devname, dev_name(&adev->dev))
	),
	TP_fast_assign(
		__assign_str(devname);
	),
	TP_printk("%s", __get_str(devname))
);

TRACE_EVENT(aie_aperture_backtrack,
	TP_PROTO(struct aie_device *adev),
	TP_ARGS(adev),
	TP_STRUCT__entry(
		__string(devname, dev_name(&adev->dev))
	),
	TP_fast_assign(
		__assign_str(devname);
	),
	TP_printk("%s", __get_str(devname))
);

TRACE_EVENT(aie_l2_backtrack,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
	),
	TP_printk("id: %d", __entry->partition_id)
);

TRACE_EVENT(aie_l1_backtrack,
	TP_PROTO(struct aie_partition *apart, u32 col, enum aie_module_type mod),
	TP_ARGS(apart, col, mod),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u8, mod)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = col;
		__entry->mod = mod;
	),
	TP_printk("id: %d [%d]: mod: %d", __entry->partition_id, __entry->col, __entry->mod)
);

TRACE_EVENT(aie_tile_backtrack,
	TP_PROTO(struct aie_partition *apart, struct aie_location loc, enum aie_module_type mod,
		 enum aie_shim_switch_type sw, u8 bc_id),
	TP_ARGS(apart, loc, mod, sw, bc_id),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
		__field(__u8, mod)
		__field(__u8, sw)
		__field(__u8, bc_id)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc.col;
		__entry->row = loc.row;
		__entry->mod = mod;
		__entry->sw = sw;
		__entry->bc_id = bc_id;
	),
	TP_printk("id: %d [%d, %d]: mod: %d sw: %d bc_id: %d",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->sw, __entry->bc_id)
);

TRACE_EVENT(aie_tile_status,
	TP_PROTO(struct aie_partition *apart, struct aie_location *loc, enum aie_module_type mod,
		 u32 *status),
	TP_ARGS(apart, loc, mod, status),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
		__field(__u8, mod)
		__dynamic_array(u32, status, aie_get_tile_status_size(apart, loc))
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc->col;
		__entry->row = loc->row;
		__entry->mod = mod;
		memcpy(__get_dynamic_array(status), status, __get_dynamic_array_len(status));
	),
	TP_printk("id: %d [%d, %d]: mod: %d status: %s",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __print_array(__get_dynamic_array(status),
				__get_dynamic_array_len(status) / sizeof(u32), sizeof(u32)))
);

TRACE_EVENT(aie_pm_ops,
	TP_PROTO(u32 node_id, void *pkt_va, size_t size, dma_addr_t pkt_dma),
	TP_ARGS(node_id, pkt_va, size, pkt_dma),
	TP_STRUCT__entry(
		__field(__u32, node_id)
		__field(void *, pkt_va)
		__field(size_t, size)
		__field(dma_addr_t, pkt_dma)
		__dynamic_array(u8, pkt, size)
	),
	TP_fast_assign(
		__entry->node_id = node_id;
		__entry->pkt_va = pkt_va;
		__entry->size = size;
		__entry->pkt_dma = pkt_dma;
		memcpy(__get_dynamic_array(pkt), pkt_va, size);
	),
	TP_printk("node_id: 0x%x pkt_va: %pK pkt_dma: 0x%llx pkt: %s",
		  __entry->node_id, __entry->pkt_va, __entry->pkt_dma,
		  __print_array(__get_dynamic_array(pkt), __get_dynamic_array_len(pkt),
				sizeof(u8)))
);

TRACE_EVENT(aie_tile_grenabled,
	TP_PROTO(struct aie_partition *apart, struct aie_location *loc, enum aie_module_type mod,
		 u32 grenabled),
	TP_ARGS(apart, loc, mod, grenabled),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
		__field(__u8, mod)
		__field(u32, grenabled)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc->col;
		__entry->row = loc->row;
		__entry->mod = mod;
		__entry->grenabled = grenabled;
	),
	TP_printk("id: %d [%d, %d]: mod: %d grenabled: 0x%x",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->grenabled)
);

TRACE_EVENT(aie_tile_eevent,
	TP_PROTO(struct aie_partition *apart, struct aie_location *loc, enum aie_module_type mod,
		 u32 eevent),
	TP_ARGS(apart, loc, mod, eevent),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u32, row)
		__field(__u8, mod)
		__field(u32, eevent)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = loc->col;
		__entry->row = loc->row;
		__entry->mod = mod;
		__entry->eevent = eevent;
	),
	TP_printk("id: %d [%d, %d]: mod: %d error event: %u",
		  __entry->partition_id, __entry->col, __entry->row, __entry->mod,
		  __entry->eevent)
);

TRACE_EVENT(aie_l1_status,
	TP_PROTO(struct aie_partition *apart, u32 col, enum aie_shim_switch_type sw, u32 status),
	TP_ARGS(apart, col, sw, status),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, col)
		__field(__u8, sw)
		__field(__u32, status)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->col = col;
		__entry->sw = sw;
		__entry->status = status;
	),
	TP_printk("id: %d [%d]: sw: %d status: 0x%x",
		  __entry->partition_id, __entry->col, __entry->sw, __entry->status)
);

TRACE_EVENT(aie_l2_mask,
	TP_PROTO(struct aie_device *adev, u32 col, u32 mask),
	TP_ARGS(adev, col, mask),
	TP_STRUCT__entry(
		__string(devname, dev_name(&adev->dev))
		__field(__u32, col)
		__field(__u32, mask)
	),
	TP_fast_assign(
		__assign_str(devname);
		__entry->col = col;
		__entry->mask = mask;
	),
	TP_printk("%s:  [%d]: mask: 0x%x",
		  __get_str(devname), __entry->col, __entry->mask)
);

TRACE_EVENT(aie_l2_status,
	TP_PROTO(struct aie_device *adev, u32 col, u32 status),
	TP_ARGS(adev, col, status),
	TP_STRUCT__entry(
		__string(devname, dev_name(&adev->dev))
		__field(__u32, col)
		__field(__u32, status)
	),
	TP_fast_assign(
		__assign_str(devname);
		__entry->col = col;
		__entry->status = status;
	),
	TP_printk("%s:  [%d]: status: 0x%x",
		  __get_str(devname), __entry->col, __entry->status)
);

TRACE_EVENT(aie_partition_request,
	TP_PROTO(struct aie_partition_req *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
		__field(__u32, uid)
		__field(__u64, meta_data)
		__field(__u32, flag)
	),
	TP_fast_assign(
		__entry->partition_id = req->partition_id;
		__entry->start_col = aie_part_id_get_start_col(req->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(req->partition_id);
		__entry->uid = req->uid;
		__entry->meta_data = req->meta_data;
		__entry->flag = req->flag;
	),
	TP_printk("id: %d start_col: %d num_cols: %d uid: %d meta_data: 0x%llx flag: 0x%x",
		  __entry->partition_id, __entry->start_col, __entry->num_cols,
		  __entry->uid, __entry->meta_data, __entry->flag)
);

TRACE_EVENT(aie_partition_is_available,
	TP_PROTO(struct aie_partition_req *req),
	TP_ARGS(req),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
		__field(__u32, uid)
		__field(__u64, meta_data)
		__field(__u32, flag)
	),
	TP_fast_assign(
		__entry->partition_id = req->partition_id;
		__entry->start_col = aie_part_id_get_start_col(req->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(req->partition_id);
		__entry->uid = req->uid;
		__entry->meta_data = req->meta_data;
		__entry->flag = req->flag;
	),
	TP_printk("id: %d start_col: %d num_cols: %d uid: %d meta_data: 0x%llx flag: 0x%x",
		  __entry->partition_id, __entry->start_col, __entry->num_cols,
		  __entry->uid, __entry->meta_data, __entry->flag)
);

TRACE_EVENT(aie_part_release,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_partition_release,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_part_release_device,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_part_release_device_done,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_partition_release_done,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_partition_reset,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_partition_post_reinit,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_part_teardown,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

TRACE_EVENT(aie_partition_get_fd,
	TP_PROTO(struct aie_partition *apart),
	TP_ARGS(apart),
	TP_STRUCT__entry(
		__field(__u32, partition_id)
		__field(__u32, start_col)
		__field(__u32, num_cols)
	),
	TP_fast_assign(
		__entry->partition_id = apart->partition_id;
		__entry->start_col = aie_part_id_get_start_col(apart->partition_id);
		__entry->num_cols = aie_part_id_get_num_cols(apart->partition_id);
	),
	TP_printk("id: %d start_col: %d num_cols: %d",
		  __entry->partition_id, __entry->start_col, __entry->num_cols)
);

#endif /* _AI_ENGINE_TRACE_H_ */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE ai-engine-trace
#include <trace/define_trace.h>
