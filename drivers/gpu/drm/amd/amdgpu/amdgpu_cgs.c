/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include <drm/drmP.h>
#include <linux/firmware.h>
#include <drm/amdgpu_drm.h>
#include "amdgpu.h"
#include "cgs_linux.h"
#include "atom.h"
#include "amdgpu_ucode.h"

struct amdgpu_cgs_device {
	struct cgs_device base;
	struct amdgpu_device *adev;
};

#define CGS_FUNC_ADEV							\
	struct amdgpu_device *adev =					\
		((struct amdgpu_cgs_device *)cgs_device)->adev

static int amdgpu_cgs_gpu_mem_info(struct cgs_device *cgs_device, enum cgs_gpu_mem_type type,
				   uint64_t *mc_start, uint64_t *mc_size,
				   uint64_t *mem_size)
{
	CGS_FUNC_ADEV;
	switch(type) {
	case CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__VISIBLE_FB:
		*mc_start = 0;
		*mc_size = adev->mc.visible_vram_size;
		*mem_size = adev->mc.visible_vram_size - adev->vram_pin_size;
		break;
	case CGS_GPU_MEM_TYPE__INVISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__INVISIBLE_FB:
		*mc_start = adev->mc.visible_vram_size;
		*mc_size = adev->mc.real_vram_size - adev->mc.visible_vram_size;
		*mem_size = *mc_size;
		break;
	case CGS_GPU_MEM_TYPE__GART_CACHEABLE:
	case CGS_GPU_MEM_TYPE__GART_WRITECOMBINE:
		*mc_start = adev->mc.gtt_start;
		*mc_size = adev->mc.gtt_size;
		*mem_size = adev->mc.gtt_size - adev->gart_pin_size;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int amdgpu_cgs_gmap_kmem(struct cgs_device *cgs_device, void *kmem,
				uint64_t size,
				uint64_t min_offset, uint64_t max_offset,
				cgs_handle_t *kmem_handle, uint64_t *mcaddr)
{
	CGS_FUNC_ADEV;
	int ret;
	struct amdgpu_bo *bo;
	struct page *kmem_page = vmalloc_to_page(kmem);
	int npages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;

	struct sg_table *sg = drm_prime_pages_to_sg(&kmem_page, npages);
	ret = amdgpu_bo_create(adev, size, PAGE_SIZE, false,
			       AMDGPU_GEM_DOMAIN_GTT, 0, sg, NULL, &bo);
	if (ret)
		return ret;
	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		return ret;

	/* pin buffer into GTT */
	ret = amdgpu_bo_pin_restricted(bo, AMDGPU_GEM_DOMAIN_GTT,
				       min_offset, max_offset, mcaddr);
	amdgpu_bo_unreserve(bo);

	*kmem_handle = (cgs_handle_t)bo;
	return ret;
}

static int amdgpu_cgs_gunmap_kmem(struct cgs_device *cgs_device, cgs_handle_t kmem_handle)
{
	struct amdgpu_bo *obj = (struct amdgpu_bo *)kmem_handle;

	if (obj) {
		int r = amdgpu_bo_reserve(obj, false);
		if (likely(r == 0)) {
			amdgpu_bo_unpin(obj);
			amdgpu_bo_unreserve(obj);
		}
		amdgpu_bo_unref(&obj);

	}
	return 0;
}

static int amdgpu_cgs_alloc_gpu_mem(struct cgs_device *cgs_device,
				    enum cgs_gpu_mem_type type,
				    uint64_t size, uint64_t align,
				    uint64_t min_offset, uint64_t max_offset,
				    cgs_handle_t *handle)
{
	CGS_FUNC_ADEV;
	uint16_t flags = 0;
	int ret = 0;
	uint32_t domain = 0;
	struct amdgpu_bo *obj;
	struct ttm_placement placement;
	struct ttm_place place;

	if (min_offset > max_offset) {
		BUG_ON(1);
		return -EINVAL;
	}

	/* fail if the alignment is not a power of 2 */
	if (((align != 1) && (align & (align - 1)))
	    || size == 0 || align == 0)
		return -EINVAL;


	switch(type) {
	case CGS_GPU_MEM_TYPE__VISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__VISIBLE_FB:
		flags = AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (max_offset > adev->mc.real_vram_size)
			return -EINVAL;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
			TTM_PL_FLAG_VRAM;
		break;
	case CGS_GPU_MEM_TYPE__INVISIBLE_CONTIG_FB:
	case CGS_GPU_MEM_TYPE__INVISIBLE_FB:
		flags = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (adev->mc.visible_vram_size < adev->mc.real_vram_size) {
			place.fpfn =
				max(min_offset, adev->mc.visible_vram_size) >> PAGE_SHIFT;
			place.lpfn =
				min(max_offset, adev->mc.real_vram_size) >> PAGE_SHIFT;
			place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
				TTM_PL_FLAG_VRAM;
		}

		break;
	case CGS_GPU_MEM_TYPE__GART_CACHEABLE:
		domain = AMDGPU_GEM_DOMAIN_GTT;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT;
		break;
	case CGS_GPU_MEM_TYPE__GART_WRITECOMBINE:
		flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC;
		domain = AMDGPU_GEM_DOMAIN_GTT;
		place.fpfn = min_offset >> PAGE_SHIFT;
		place.lpfn = max_offset >> PAGE_SHIFT;
		place.flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_TT |
			TTM_PL_FLAG_UNCACHED;
		break;
	default:
		return -EINVAL;
	}


	*handle = 0;

	placement.placement = &place;
	placement.num_placement = 1;
	placement.busy_placement = &place;
	placement.num_busy_placement = 1;

	ret = amdgpu_bo_create_restricted(adev, size, PAGE_SIZE,
					  true, domain, flags,
					  NULL, &placement, NULL,
					  &obj);
	if (ret) {
		DRM_ERROR("(%d) bo create failed\n", ret);
		return ret;
	}
	*handle = (cgs_handle_t)obj;

	return ret;
}

static int amdgpu_cgs_free_gpu_mem(struct cgs_device *cgs_device, cgs_handle_t handle)
{
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;

	if (obj) {
		int r = amdgpu_bo_reserve(obj, false);
		if (likely(r == 0)) {
			amdgpu_bo_kunmap(obj);
			amdgpu_bo_unpin(obj);
			amdgpu_bo_unreserve(obj);
		}
		amdgpu_bo_unref(&obj);

	}
	return 0;
}

static int amdgpu_cgs_gmap_gpu_mem(struct cgs_device *cgs_device, cgs_handle_t handle,
				   uint64_t *mcaddr)
{
	int r;
	u64 min_offset, max_offset;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;

	WARN_ON_ONCE(obj->placement.num_placement > 1);

	min_offset = obj->placements[0].fpfn << PAGE_SHIFT;
	max_offset = obj->placements[0].lpfn << PAGE_SHIFT;

	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_pin_restricted(obj, AMDGPU_GEM_DOMAIN_GTT,
				     min_offset, max_offset, mcaddr);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_gunmap_gpu_mem(struct cgs_device *cgs_device, cgs_handle_t handle)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_unpin(obj);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_kmap_gpu_mem(struct cgs_device *cgs_device, cgs_handle_t handle,
				   void **map)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	r = amdgpu_bo_kmap(obj, map);
	amdgpu_bo_unreserve(obj);
	return r;
}

static int amdgpu_cgs_kunmap_gpu_mem(struct cgs_device *cgs_device, cgs_handle_t handle)
{
	int r;
	struct amdgpu_bo *obj = (struct amdgpu_bo *)handle;
	r = amdgpu_bo_reserve(obj, false);
	if (unlikely(r != 0))
		return r;
	amdgpu_bo_kunmap(obj);
	amdgpu_bo_unreserve(obj);
	return r;
}

static uint32_t amdgpu_cgs_read_register(struct cgs_device *cgs_device, unsigned offset)
{
	CGS_FUNC_ADEV;
	return RREG32(offset);
}

static void amdgpu_cgs_write_register(struct cgs_device *cgs_device, unsigned offset,
				      uint32_t value)
{
	CGS_FUNC_ADEV;
	WREG32(offset, value);
}

static uint32_t amdgpu_cgs_read_ind_register(struct cgs_device *cgs_device,
					     enum cgs_ind_reg space,
					     unsigned index)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return RREG32_IDX(index);
	case CGS_IND_REG__PCIE:
		return RREG32_PCIE(index);
	case CGS_IND_REG__SMC:
		return RREG32_SMC(index);
	case CGS_IND_REG__UVD_CTX:
		return RREG32_UVD_CTX(index);
	case CGS_IND_REG__DIDT:
		return RREG32_DIDT(index);
	case CGS_IND_REG_GC_CAC:
		return RREG32_GC_CAC(index);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return 0;
	}
	WARN(1, "Invalid indirect register space");
	return 0;
}

static void amdgpu_cgs_write_ind_register(struct cgs_device *cgs_device,
					  enum cgs_ind_reg space,
					  unsigned index, uint32_t value)
{
	CGS_FUNC_ADEV;
	switch (space) {
	case CGS_IND_REG__MMIO:
		return WREG32_IDX(index, value);
	case CGS_IND_REG__PCIE:
		return WREG32_PCIE(index, value);
	case CGS_IND_REG__SMC:
		return WREG32_SMC(index, value);
	case CGS_IND_REG__UVD_CTX:
		return WREG32_UVD_CTX(index, value);
	case CGS_IND_REG__DIDT:
		return WREG32_DIDT(index, value);
	case CGS_IND_REG_GC_CAC:
		return WREG32_GC_CAC(index, value);
	case CGS_IND_REG__AUDIO_ENDPT:
		DRM_ERROR("audio endpt register access not implemented.\n");
		return;
	}
	WARN(1, "Invalid indirect register space");
}

static uint8_t amdgpu_cgs_read_pci_config_byte(struct cgs_device *cgs_device, unsigned addr)
{
	CGS_FUNC_ADEV;
	uint8_t val;
	int ret = pci_read_config_byte(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_byte error"))
		return 0;
	return val;
}

static uint16_t amdgpu_cgs_read_pci_config_word(struct cgs_device *cgs_device, unsigned addr)
{
	CGS_FUNC_ADEV;
	uint16_t val;
	int ret = pci_read_config_word(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_word error"))
		return 0;
	return val;
}

static uint32_t amdgpu_cgs_read_pci_config_dword(struct cgs_device *cgs_device,
						 unsigned addr)
{
	CGS_FUNC_ADEV;
	uint32_t val;
	int ret = pci_read_config_dword(adev->pdev, addr, &val);
	if (WARN(ret, "pci_read_config_dword error"))
		return 0;
	return val;
}

static void amdgpu_cgs_write_pci_config_byte(struct cgs_device *cgs_device, unsigned addr,
					     uint8_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_byte(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_byte error");
}

static void amdgpu_cgs_write_pci_config_word(struct cgs_device *cgs_device, unsigned addr,
					     uint16_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_word(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_word error");
}

static void amdgpu_cgs_write_pci_config_dword(struct cgs_device *cgs_device, unsigned addr,
					      uint32_t value)
{
	CGS_FUNC_ADEV;
	int ret = pci_write_config_dword(adev->pdev, addr, value);
	WARN(ret, "pci_write_config_dword error");
}


static int amdgpu_cgs_get_pci_resource(struct cgs_device *cgs_device,
				       enum cgs_resource_type resource_type,
				       uint64_t size,
				       uint64_t offset,
				       uint64_t *resource_base)
{
	CGS_FUNC_ADEV;

	if (resource_base == NULL)
		return -EINVAL;

	switch (resource_type) {
	case CGS_RESOURCE_TYPE_MMIO:
		if (adev->rmmio_size == 0)
			return -ENOENT;
		if ((offset + size) > adev->rmmio_size)
			return -EINVAL;
		*resource_base = adev->rmmio_base;
		return 0;
	case CGS_RESOURCE_TYPE_DOORBELL:
		if (adev->doorbell.size == 0)
			return -ENOENT;
		if ((offset + size) > adev->doorbell.size)
			return -EINVAL;
		*resource_base = adev->doorbell.base;
		return 0;
	case CGS_RESOURCE_TYPE_FB:
	case CGS_RESOURCE_TYPE_IO:
	case CGS_RESOURCE_TYPE_ROM:
	default:
		return -EINVAL;
	}
}

static const void *amdgpu_cgs_atom_get_data_table(struct cgs_device *cgs_device,
						  unsigned table, uint16_t *size,
						  uint8_t *frev, uint8_t *crev)
{
	CGS_FUNC_ADEV;
	uint16_t data_start;

	if (amdgpu_atom_parse_data_header(
		    adev->mode_info.atom_context, table, size,
		    frev, crev, &data_start))
		return (uint8_t*)adev->mode_info.atom_context->bios +
			data_start;

	return NULL;
}

static int amdgpu_cgs_atom_get_cmd_table_revs(struct cgs_device *cgs_device, unsigned table,
					      uint8_t *frev, uint8_t *crev)
{
	CGS_FUNC_ADEV;

	if (amdgpu_atom_parse_cmd_header(
		    adev->mode_info.atom_context, table,
		    frev, crev))
		return 0;

	return -EINVAL;
}

static int amdgpu_cgs_atom_exec_cmd_table(struct cgs_device *cgs_device, unsigned table,
					  void *args)
{
	CGS_FUNC_ADEV;

	return amdgpu_atom_execute_table(
		adev->mode_info.atom_context, table, args);
}

static int amdgpu_cgs_create_pm_request(struct cgs_device *cgs_device, cgs_handle_t *request)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_destroy_pm_request(struct cgs_device *cgs_device, cgs_handle_t request)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_set_pm_request(struct cgs_device *cgs_device, cgs_handle_t request,
				     int active)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_pm_request_clock(struct cgs_device *cgs_device, cgs_handle_t request,
				       enum cgs_clock clock, unsigned freq)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_pm_request_engine(struct cgs_device *cgs_device, cgs_handle_t request,
					enum cgs_engine engine, int powered)
{
	/* TODO */
	return 0;
}



static int amdgpu_cgs_pm_query_clock_limits(struct cgs_device *cgs_device,
					    enum cgs_clock clock,
					    struct cgs_clock_limits *limits)
{
	/* TODO */
	return 0;
}

static int amdgpu_cgs_set_camera_voltages(struct cgs_device *cgs_device, uint32_t mask,
					  const uint32_t *voltages)
{
	DRM_ERROR("not implemented");
	return -EPERM;
}

struct cgs_irq_params {
	unsigned src_id;
	cgs_irq_source_set_func_t set;
	cgs_irq_handler_func_t handler;
	void *private_data;
};

static int cgs_set_irq_state(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *src,
			     unsigned type,
			     enum amdgpu_interrupt_state state)
{
	struct cgs_irq_params *irq_params =
		(struct cgs_irq_params *)src->data;
	if (!irq_params)
		return -EINVAL;
	if (!irq_params->set)
		return -EINVAL;
	return irq_params->set(irq_params->private_data,
			       irq_params->src_id,
			       type,
			       (int)state);
}

static int cgs_process_irq(struct amdgpu_device *adev,
			   struct amdgpu_irq_src *source,
			   struct amdgpu_iv_entry *entry)
{
	struct cgs_irq_params *irq_params =
		(struct cgs_irq_params *)source->data;
	if (!irq_params)
		return -EINVAL;
	if (!irq_params->handler)
		return -EINVAL;
	return irq_params->handler(irq_params->private_data,
				   irq_params->src_id,
				   entry->iv_entry);
}

static const struct amdgpu_irq_src_funcs cgs_irq_funcs = {
	.set = cgs_set_irq_state,
	.process = cgs_process_irq,
};

static int amdgpu_cgs_add_irq_source(struct cgs_device *cgs_device, unsigned src_id,
				     unsigned num_types,
				     cgs_irq_source_set_func_t set,
				     cgs_irq_handler_func_t handler,
				     void *private_data)
{
	CGS_FUNC_ADEV;
	int ret = 0;
	struct cgs_irq_params *irq_params;
	struct amdgpu_irq_src *source =
		kzalloc(sizeof(struct amdgpu_irq_src), GFP_KERNEL);
	if (!source)
		return -ENOMEM;
	irq_params =
		kzalloc(sizeof(struct cgs_irq_params), GFP_KERNEL);
	if (!irq_params) {
		kfree(source);
		return -ENOMEM;
	}
	source->num_types = num_types;
	source->funcs = &cgs_irq_funcs;
	irq_params->src_id = src_id;
	irq_params->set = set;
	irq_params->handler = handler;
	irq_params->private_data = private_data;
	source->data = (void *)irq_params;
	ret = amdgpu_irq_add_id(adev, src_id, source);
	if (ret) {
		kfree(irq_params);
		kfree(source);
	}

	return ret;
}

static int amdgpu_cgs_irq_get(struct cgs_device *cgs_device, unsigned src_id, unsigned type)
{
	CGS_FUNC_ADEV;
	return amdgpu_irq_get(adev, adev->irq.sources[src_id], type);
}

static int amdgpu_cgs_irq_put(struct cgs_device *cgs_device, unsigned src_id, unsigned type)
{
	CGS_FUNC_ADEV;
	return amdgpu_irq_put(adev, adev->irq.sources[src_id], type);
}

static int amdgpu_cgs_set_clockgating_state(struct cgs_device *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_clockgating_state state)
{
	CGS_FUNC_ADEV;
	int i, r = -1;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_block_status[i].valid)
			continue;

		if (adev->ip_blocks[i].type == block_type) {
			r = adev->ip_blocks[i].funcs->set_clockgating_state(
								(void *)adev,
									state);
			break;
		}
	}
	return r;
}

static int amdgpu_cgs_set_powergating_state(struct cgs_device *cgs_device,
				  enum amd_ip_block_type block_type,
				  enum amd_powergating_state state)
{
	CGS_FUNC_ADEV;
	int i, r = -1;

	for (i = 0; i < adev->num_ip_blocks; i++) {
		if (!adev->ip_block_status[i].valid)
			continue;

		if (adev->ip_blocks[i].type == block_type) {
			r = adev->ip_blocks[i].funcs->set_powergating_state(
								(void *)adev,
									state);
			break;
		}
	}
	return r;
}


static uint32_t fw_type_convert(struct cgs_device *cgs_device, uint32_t fw_type)
{
	CGS_FUNC_ADEV;
	enum AMDGPU_UCODE_ID result = AMDGPU_UCODE_ID_MAXIMUM;

	switch (fw_type) {
	case CGS_UCODE_ID_SDMA0:
		result = AMDGPU_UCODE_ID_SDMA0;
		break;
	case CGS_UCODE_ID_SDMA1:
		result = AMDGPU_UCODE_ID_SDMA1;
		break;
	case CGS_UCODE_ID_CP_CE:
		result = AMDGPU_UCODE_ID_CP_CE;
		break;
	case CGS_UCODE_ID_CP_PFP:
		result = AMDGPU_UCODE_ID_CP_PFP;
		break;
	case CGS_UCODE_ID_CP_ME:
		result = AMDGPU_UCODE_ID_CP_ME;
		break;
	case CGS_UCODE_ID_CP_MEC:
	case CGS_UCODE_ID_CP_MEC_JT1:
		result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case CGS_UCODE_ID_CP_MEC_JT2:
		if (adev->asic_type == CHIP_TONGA || adev->asic_type == CHIP_POLARIS11
		  || adev->asic_type == CHIP_POLARIS10)
			result = AMDGPU_UCODE_ID_CP_MEC2;
		else
			result = AMDGPU_UCODE_ID_CP_MEC1;
		break;
	case CGS_UCODE_ID_RLC_G:
		result = AMDGPU_UCODE_ID_RLC_G;
		break;
	default:
		DRM_ERROR("Firmware type not supported\n");
	}
	return result;
}

static int amdgpu_cgs_rel_firmware(struct cgs_device *cgs_device, enum cgs_ucode_id type)
{
	CGS_FUNC_ADEV;
	if ((CGS_UCODE_ID_SMU == type) || (CGS_UCODE_ID_SMU_SK == type)) {
		release_firmware(adev->pm.fw);
		return 0;
	}
	/* cannot release other firmware because they are not created by cgs */
	return -EINVAL;
}

static uint16_t amdgpu_get_firmware_version(struct cgs_device *cgs_device,
					enum cgs_ucode_id type)
{
	CGS_FUNC_ADEV;
	uint16_t fw_version;

	switch (type) {
		case CGS_UCODE_ID_SDMA0:
			fw_version = adev->sdma.instance[0].fw_version;
			break;
		case CGS_UCODE_ID_SDMA1:
			fw_version = adev->sdma.instance[1].fw_version;
			break;
		case CGS_UCODE_ID_CP_CE:
			fw_version = adev->gfx.ce_fw_version;
			break;
		case CGS_UCODE_ID_CP_PFP:
			fw_version = adev->gfx.pfp_fw_version;
			break;
		case CGS_UCODE_ID_CP_ME:
			fw_version = adev->gfx.me_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC_JT1:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_CP_MEC_JT2:
			fw_version = adev->gfx.mec_fw_version;
			break;
		case CGS_UCODE_ID_RLC_G:
			fw_version = adev->gfx.rlc_fw_version;
			break;
		default:
			DRM_ERROR("firmware type %d do not have version\n", type);
			fw_version = 0;
	}
	return fw_version;
}

static int amdgpu_cgs_get_firmware_info(struct cgs_device *cgs_device,
					enum cgs_ucode_id type,
					struct cgs_firmware_info *info)
{
	CGS_FUNC_ADEV;

	if ((CGS_UCODE_ID_SMU != type) && (CGS_UCODE_ID_SMU_SK != type)) {
		uint64_t gpu_addr;
		uint32_t data_size;
		const struct gfx_firmware_header_v1_0 *header;
		enum AMDGPU_UCODE_ID id;
		struct amdgpu_firmware_info *ucode;

		id = fw_type_convert(cgs_device, type);
		ucode = &adev->firmware.ucode[id];
		if (ucode->fw == NULL)
			return -EINVAL;

		gpu_addr  = ucode->mc_addr;
		header = (const struct gfx_firmware_header_v1_0 *)ucode->fw->data;
		data_size = le32_to_cpu(header->header.ucode_size_bytes);

		if ((type == CGS_UCODE_ID_CP_MEC_JT1) ||
		    (type == CGS_UCODE_ID_CP_MEC_JT2)) {
			gpu_addr += le32_to_cpu(header->jt_offset) << 2;
			data_size = le32_to_cpu(header->jt_size) << 2;
		}
		info->mc_addr = gpu_addr;
		info->image_size = data_size;
		info->version = (uint16_t)le32_to_cpu(header->header.ucode_version);
		info->fw_version = amdgpu_get_firmware_version(cgs_device, type);
		info->feature_version = (uint16_t)le32_to_cpu(header->ucode_feature_version);
	} else {
		char fw_name[30] = {0};
		int err = 0;
		uint32_t ucode_size;
		uint32_t ucode_start_address;
		const uint8_t *src;
		const struct smc_firmware_header_v1_0 *hdr;

		if (!adev->pm.fw) {
			switch (adev->asic_type) {
			case CHIP_TOPAZ:
				if (((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0x81)) ||
				    ((adev->pdev->device == 0x6900) && (adev->pdev->revision == 0x83)) ||
				    ((adev->pdev->device == 0x6907) && (adev->pdev->revision == 0x87)))
					strcpy(fw_name, "amdgpu/topaz_k_smc.bin");
				else
					strcpy(fw_name, "amdgpu/topaz_smc.bin");
				break;
			case CHIP_TONGA:
				if (((adev->pdev->device == 0x6939) && (adev->pdev->revision == 0xf1)) ||
				    ((adev->pdev->device == 0x6938) && (adev->pdev->revision == 0xf1)))
					strcpy(fw_name, "amdgpu/tonga_k_smc.bin");
				else
					strcpy(fw_name, "amdgpu/tonga_smc.bin");
				break;
			case CHIP_FIJI:
				strcpy(fw_name, "amdgpu/fiji_smc.bin");
				break;
			case CHIP_POLARIS11:
				if (type == CGS_UCODE_ID_SMU)
					strcpy(fw_name, "amdgpu/polaris11_smc.bin");
				else if (type == CGS_UCODE_ID_SMU_SK)
					strcpy(fw_name, "amdgpu/polaris11_smc_sk.bin");
				break;
			case CHIP_POLARIS10:
				if (type == CGS_UCODE_ID_SMU)
					strcpy(fw_name, "amdgpu/polaris10_smc.bin");
				else if (type == CGS_UCODE_ID_SMU_SK)
					strcpy(fw_name, "amdgpu/polaris10_smc_sk.bin");
				break;
			default:
				DRM_ERROR("SMC firmware not supported\n");
				return -EINVAL;
			}

			err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
			if (err) {
				DRM_ERROR("Failed to request firmware\n");
				return err;
			}

			err = amdgpu_ucode_validate(adev->pm.fw);
			if (err) {
				DRM_ERROR("Failed to load firmware \"%s\"", fw_name);
				release_firmware(adev->pm.fw);
				adev->pm.fw = NULL;
				return err;
			}
		}

		hdr = (const struct smc_firmware_header_v1_0 *)	adev->pm.fw->data;
		amdgpu_ucode_print_smc_hdr(&hdr->header);
		adev->pm.fw_version = le32_to_cpu(hdr->header.ucode_version);
		ucode_size = le32_to_cpu(hdr->header.ucode_size_bytes);
		ucode_start_address = le32_to_cpu(hdr->ucode_start_addr);
		src = (const uint8_t *)(adev->pm.fw->data +
		       le32_to_cpu(hdr->header.ucode_array_offset_bytes));

		info->version = adev->pm.fw_version;
		info->image_size = ucode_size;
		info->ucode_start_address = ucode_start_address;
		info->kptr = (void *)src;
	}
	return 0;
}

static int amdgpu_cgs_query_system_info(struct cgs_device *cgs_device,
					struct cgs_system_info *sys_info)
{
	CGS_FUNC_ADEV;

	if (NULL == sys_info)
		return -ENODEV;

	if (sizeof(struct cgs_system_info) != sys_info->size)
		return -ENODEV;

	switch (sys_info->info_id) {
	case CGS_SYSTEM_INFO_ADAPTER_BDF_ID:
		sys_info->value = adev->pdev->devfn | (adev->pdev->bus->number << 8);
		break;
	case CGS_SYSTEM_INFO_PCIE_GEN_INFO:
		sys_info->value = adev->pm.pcie_gen_mask;
		break;
	case CGS_SYSTEM_INFO_PCIE_MLW:
		sys_info->value = adev->pm.pcie_mlw_mask;
		break;
	case CGS_SYSTEM_INFO_PCIE_DEV:
		sys_info->value = adev->pdev->device;
		break;
	case CGS_SYSTEM_INFO_PCIE_REV:
		sys_info->value = adev->pdev->revision;
		break;
	case CGS_SYSTEM_INFO_CG_FLAGS:
		sys_info->value = adev->cg_flags;
		break;
	case CGS_SYSTEM_INFO_PG_FLAGS:
		sys_info->value = adev->pg_flags;
		break;
	case CGS_SYSTEM_INFO_GFX_CU_INFO:
		sys_info->value = adev->gfx.cu_info.number;
		break;
	case CGS_SYSTEM_INFO_GFX_SE_INFO:
		sys_info->value = adev->gfx.config.max_shader_engines;
		break;
	case CGS_SYSTEM_INFO_PCIE_SUB_SYS_ID:
		sys_info->value = adev->pdev->subsystem_device;
		break;
	case CGS_SYSTEM_INFO_PCIE_SUB_SYS_VENDOR_ID:
		sys_info->value = adev->pdev->subsystem_vendor;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static int amdgpu_cgs_get_active_displays_info(struct cgs_device *cgs_device,
					  struct cgs_display_info *info)
{
	CGS_FUNC_ADEV;
	struct amdgpu_crtc *amdgpu_crtc;
	struct drm_device *ddev = adev->ddev;
	struct drm_crtc *crtc;
	uint32_t line_time_us, vblank_lines;
	struct cgs_mode_info *mode_info;

	if (info == NULL)
		return -EINVAL;

	mode_info = info->mode_info;

	if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc,
				&ddev->mode_config.crtc_list, head) {
			amdgpu_crtc = to_amdgpu_crtc(crtc);
			if (crtc->enabled) {
				info->active_display_mask |= (1 << amdgpu_crtc->crtc_id);
				info->display_count++;
			}
			if (mode_info != NULL &&
				crtc->enabled && amdgpu_crtc->enabled &&
				amdgpu_crtc->hw_mode.clock) {
				line_time_us = (amdgpu_crtc->hw_mode.crtc_htotal * 1000) /
							amdgpu_crtc->hw_mode.clock;
				vblank_lines = amdgpu_crtc->hw_mode.crtc_vblank_end -
							amdgpu_crtc->hw_mode.crtc_vdisplay +
							(amdgpu_crtc->v_border * 2);
				mode_info->vblank_time_us = vblank_lines * line_time_us;
				mode_info->refresh_rate = drm_mode_vrefresh(&amdgpu_crtc->hw_mode);
				mode_info->ref_clock = adev->clock.spll.reference_freq;
				mode_info = NULL;
			}
		}
	}

	return 0;
}


static int amdgpu_cgs_notify_dpm_enabled(struct cgs_device *cgs_device, bool enabled)
{
	CGS_FUNC_ADEV;

	adev->pm.dpm_enabled = enabled;

	return 0;
}

/** \brief evaluate acpi namespace object, handle or pathname must be valid
 *  \param cgs_device
 *  \param info input/output arguments for the control method
 *  \return status
 */

#if defined(CONFIG_ACPI)
static int amdgpu_cgs_acpi_eval_object(struct cgs_device *cgs_device,
				    struct cgs_acpi_method_info *info)
{
	CGS_FUNC_ADEV;
	acpi_handle handle;
	struct acpi_object_list input;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *params, *obj;
	uint8_t name[5] = {'\0'};
	struct cgs_acpi_method_argument *argument;
	uint32_t i, count;
	acpi_status status;
	int result;

	handle = ACPI_HANDLE(&adev->pdev->dev);
	if (!handle)
		return -ENODEV;

	memset(&input, 0, sizeof(struct acpi_object_list));

	/* validate input info */
	if (info->size != sizeof(struct cgs_acpi_method_info))
		return -EINVAL;

	input.count = info->input_count;
	if (info->input_count > 0) {
		if (info->pinput_argument == NULL)
			return -EINVAL;
		argument = info->pinput_argument;
		for (i = 0; i < info->input_count; i++) {
			if (((argument->type == ACPI_TYPE_STRING) ||
			     (argument->type == ACPI_TYPE_BUFFER)) &&
			    (argument->pointer == NULL))
				return -EINVAL;
			argument++;
		}
	}

	if (info->output_count > 0) {
		if (info->poutput_argument == NULL)
			return -EINVAL;
		argument = info->poutput_argument;
		for (i = 0; i < info->output_count; i++) {
			if (((argument->type == ACPI_TYPE_STRING) ||
				(argument->type == ACPI_TYPE_BUFFER))
				&& (argument->pointer == NULL))
				return -EINVAL;
			argument++;
		}
	}

	/* The path name passed to acpi_evaluate_object should be null terminated */
	if ((info->field & CGS_ACPI_FIELD_METHOD_NAME) != 0) {
		strncpy(name, (char *)&(info->name), sizeof(uint32_t));
		name[4] = '\0';
	}

	/* parse input parameters */
	if (input.count > 0) {
		input.pointer = params =
				kzalloc(sizeof(union acpi_object) * input.count, GFP_KERNEL);
		if (params == NULL)
			return -EINVAL;

		argument = info->pinput_argument;

		for (i = 0; i < input.count; i++) {
			params->type = argument->type;
			switch (params->type) {
			case ACPI_TYPE_INTEGER:
				params->integer.value = argument->value;
				break;
			case ACPI_TYPE_STRING:
				params->string.length = argument->data_length;
				params->string.pointer = argument->pointer;
				break;
			case ACPI_TYPE_BUFFER:
				params->buffer.length = argument->data_length;
				params->buffer.pointer = argument->pointer;
				break;
			default:
				break;
			}
			params++;
			argument++;
		}
	}

	/* parse output info */
	count = info->output_count;
	argument = info->poutput_argument;

	/* evaluate the acpi method */
	status = acpi_evaluate_object(handle, name, &input, &output);

	if (ACPI_FAILURE(status)) {
		result = -EIO;
		goto free_input;
	}

	/* return the output info */
	obj = output.pointer;

	if (count > 1) {
		if ((obj->type != ACPI_TYPE_PACKAGE) ||
			(obj->package.count != count)) {
			result = -EIO;
			goto free_obj;
		}
		params = obj->package.elements;
	} else
		params = obj;

	if (params == NULL) {
		result = -EIO;
		goto free_obj;
	}

	for (i = 0; i < count; i++) {
		if (argument->type != params->type) {
			result = -EIO;
			goto free_obj;
		}
		switch (params->type) {
		case ACPI_TYPE_INTEGER:
			argument->value = params->integer.value;
			break;
		case ACPI_TYPE_STRING:
			if ((params->string.length != argument->data_length) ||
				(params->string.pointer == NULL)) {
				result = -EIO;
				goto free_obj;
			}
			strncpy(argument->pointer,
				params->string.pointer,
				params->string.length);
			break;
		case ACPI_TYPE_BUFFER:
			if (params->buffer.pointer == NULL) {
				result = -EIO;
				goto free_obj;
			}
			memcpy(argument->pointer,
				params->buffer.pointer,
				argument->data_length);
			break;
		default:
			break;
		}
		argument++;
		params++;
	}

	result = 0;
free_obj:
	kfree(obj);
free_input:
	kfree((void *)input.pointer);
	return result;
}
#else
static int amdgpu_cgs_acpi_eval_object(struct cgs_device *cgs_device,
				struct cgs_acpi_method_info *info)
{
	return -EIO;
}
#endif

static int amdgpu_cgs_call_acpi_method(struct cgs_device *cgs_device,
					uint32_t acpi_method,
					uint32_t acpi_function,
					void *pinput, void *poutput,
					uint32_t output_count,
					uint32_t input_size,
					uint32_t output_size)
{
	struct cgs_acpi_method_argument acpi_input[2] = { {0}, {0} };
	struct cgs_acpi_method_argument acpi_output = {0};
	struct cgs_acpi_method_info info = {0};

	acpi_input[0].type = CGS_ACPI_TYPE_INTEGER;
	acpi_input[0].data_length = sizeof(uint32_t);
	acpi_input[0].value = acpi_function;

	acpi_input[1].type = CGS_ACPI_TYPE_BUFFER;
	acpi_input[1].data_length = input_size;
	acpi_input[1].pointer = pinput;

	acpi_output.type = CGS_ACPI_TYPE_BUFFER;
	acpi_output.data_length = output_size;
	acpi_output.pointer = poutput;

	info.size = sizeof(struct cgs_acpi_method_info);
	info.field = CGS_ACPI_FIELD_METHOD_NAME | CGS_ACPI_FIELD_INPUT_ARGUMENT_COUNT;
	info.input_count = 2;
	info.name = acpi_method;
	info.pinput_argument = acpi_input;
	info.output_count = output_count;
	info.poutput_argument = &acpi_output;

	return amdgpu_cgs_acpi_eval_object(cgs_device, &info);
}

static const struct cgs_ops amdgpu_cgs_ops = {
	amdgpu_cgs_gpu_mem_info,
	amdgpu_cgs_gmap_kmem,
	amdgpu_cgs_gunmap_kmem,
	amdgpu_cgs_alloc_gpu_mem,
	amdgpu_cgs_free_gpu_mem,
	amdgpu_cgs_gmap_gpu_mem,
	amdgpu_cgs_gunmap_gpu_mem,
	amdgpu_cgs_kmap_gpu_mem,
	amdgpu_cgs_kunmap_gpu_mem,
	amdgpu_cgs_read_register,
	amdgpu_cgs_write_register,
	amdgpu_cgs_read_ind_register,
	amdgpu_cgs_write_ind_register,
	amdgpu_cgs_read_pci_config_byte,
	amdgpu_cgs_read_pci_config_word,
	amdgpu_cgs_read_pci_config_dword,
	amdgpu_cgs_write_pci_config_byte,
	amdgpu_cgs_write_pci_config_word,
	amdgpu_cgs_write_pci_config_dword,
	amdgpu_cgs_get_pci_resource,
	amdgpu_cgs_atom_get_data_table,
	amdgpu_cgs_atom_get_cmd_table_revs,
	amdgpu_cgs_atom_exec_cmd_table,
	amdgpu_cgs_create_pm_request,
	amdgpu_cgs_destroy_pm_request,
	amdgpu_cgs_set_pm_request,
	amdgpu_cgs_pm_request_clock,
	amdgpu_cgs_pm_request_engine,
	amdgpu_cgs_pm_query_clock_limits,
	amdgpu_cgs_set_camera_voltages,
	amdgpu_cgs_get_firmware_info,
	amdgpu_cgs_rel_firmware,
	amdgpu_cgs_set_powergating_state,
	amdgpu_cgs_set_clockgating_state,
	amdgpu_cgs_get_active_displays_info,
	amdgpu_cgs_notify_dpm_enabled,
	amdgpu_cgs_call_acpi_method,
	amdgpu_cgs_query_system_info,
};

static const struct cgs_os_ops amdgpu_cgs_os_ops = {
	amdgpu_cgs_add_irq_source,
	amdgpu_cgs_irq_get,
	amdgpu_cgs_irq_put
};

struct cgs_device *amdgpu_cgs_create_device(struct amdgpu_device *adev)
{
	struct amdgpu_cgs_device *cgs_device =
		kmalloc(sizeof(*cgs_device), GFP_KERNEL);

	if (!cgs_device) {
		DRM_ERROR("Couldn't allocate CGS device structure\n");
		return NULL;
	}

	cgs_device->base.ops = &amdgpu_cgs_ops;
	cgs_device->base.os_ops = &amdgpu_cgs_os_ops;
	cgs_device->adev = adev;

	return (struct cgs_device *)cgs_device;
}

void amdgpu_cgs_destroy_device(struct cgs_device *cgs_device)
{
	kfree(cgs_device);
}
