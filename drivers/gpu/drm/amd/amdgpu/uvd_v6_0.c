/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * Authors: Christian König <christian.koenig@amd.com>
 */

#include <linux/firmware.h>
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_uvd.h"
#include "vid.h"
#include "uvd/uvd_6_0_d.h"
#include "uvd/uvd_6_0_sh_mask.h"
#include "oss/oss_2_0_d.h"
#include "oss/oss_2_0_sh_mask.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "bif/bif_5_1_d.h"
#include "gmc/gmc_8_1_d.h"
#include "vi.h"

static void uvd_v6_0_set_ring_funcs(struct amdgpu_device *adev);
static void uvd_v6_0_set_irq_funcs(struct amdgpu_device *adev);
static int uvd_v6_0_start(struct amdgpu_device *adev);
static void uvd_v6_0_stop(struct amdgpu_device *adev);
static void uvd_v6_0_set_sw_clock_gating(struct amdgpu_device *adev);

/**
 * uvd_v6_0_ring_get_rptr - get read pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware read pointer
 */
static uint32_t uvd_v6_0_ring_get_rptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(mmUVD_RBC_RB_RPTR);
}

/**
 * uvd_v6_0_ring_get_wptr - get write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Returns the current hardware write pointer
 */
static uint32_t uvd_v6_0_ring_get_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	return RREG32(mmUVD_RBC_RB_WPTR);
}

/**
 * uvd_v6_0_ring_set_wptr - set write pointer
 *
 * @ring: amdgpu_ring pointer
 *
 * Commits the write pointer to the hardware
 */
static void uvd_v6_0_ring_set_wptr(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;

	WREG32(mmUVD_RBC_RB_WPTR, ring->wptr);
}

static int uvd_v6_0_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	uvd_v6_0_set_ring_funcs(adev);
	uvd_v6_0_set_irq_funcs(adev);

	return 0;
}

static int uvd_v6_0_sw_init(void *handle)
{
	struct amdgpu_ring *ring;
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* UVD TRAP */
	r = amdgpu_irq_add_id(adev, 124, &adev->uvd.irq);
	if (r)
		return r;

	r = amdgpu_uvd_sw_init(adev);
	if (r)
		return r;

	r = amdgpu_uvd_resume(adev);
	if (r)
		return r;

	ring = &adev->uvd.ring;
	sprintf(ring->name, "uvd");
	r = amdgpu_ring_init(adev, ring, 512, PACKET0(mmUVD_NO_OP, 0), 0xf,
			     &adev->uvd.irq, 0, AMDGPU_RING_TYPE_UVD);

	return r;
}

static int uvd_v6_0_sw_fini(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_uvd_suspend(adev);
	if (r)
		return r;

	r = amdgpu_uvd_sw_fini(adev);
	if (r)
		return r;

	return r;
}

/**
 * uvd_v6_0_hw_init - start and test UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * Initialize the hardware, boot up the VCPU and do some testing
 */
static int uvd_v6_0_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->uvd.ring;
	uint32_t tmp;
	int r;

	r = uvd_v6_0_start(adev);
	if (r)
		goto done;

	ring->ready = true;
	r = amdgpu_ring_test_ring(ring);
	if (r) {
		ring->ready = false;
		goto done;
	}

	r = amdgpu_ring_alloc(ring, 10);
	if (r) {
		DRM_ERROR("amdgpu: ring failed to lock UVD ring (%d).\n", r);
		goto done;
	}

	tmp = PACKET0(mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	tmp = PACKET0(mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL, 0);
	amdgpu_ring_write(ring, tmp);
	amdgpu_ring_write(ring, 0xFFFFF);

	/* Clear timeout status bits */
	amdgpu_ring_write(ring, PACKET0(mmUVD_SEMA_TIMEOUT_STATUS, 0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring, PACKET0(mmUVD_SEMA_CNTL, 0));
	amdgpu_ring_write(ring, 3);

	amdgpu_ring_commit(ring);

done:
	if (!r)
		DRM_INFO("UVD initialized successfully.\n");

	return r;
}

/**
 * uvd_v6_0_hw_fini - stop the hardware block
 *
 * @adev: amdgpu_device pointer
 *
 * Stop the UVD block, mark ring as not ready any more
 */
static int uvd_v6_0_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct amdgpu_ring *ring = &adev->uvd.ring;

	uvd_v6_0_stop(adev);
	ring->ready = false;

	return 0;
}

static int uvd_v6_0_suspend(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = uvd_v6_0_hw_fini(adev);
	if (r)
		return r;

	/* Skip this for APU for now */
	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_uvd_suspend(adev);
		if (r)
			return r;
	}

	return r;
}

static int uvd_v6_0_resume(void *handle)
{
	int r;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	/* Skip this for APU for now */
	if (!(adev->flags & AMD_IS_APU)) {
		r = amdgpu_uvd_resume(adev);
		if (r)
			return r;
	}
	r = uvd_v6_0_hw_init(adev);
	if (r)
		return r;

	return r;
}

/**
 * uvd_v6_0_mc_resume - memory controller programming
 *
 * @adev: amdgpu_device pointer
 *
 * Let the UVD memory controller know it's offsets
 */
static void uvd_v6_0_mc_resume(struct amdgpu_device *adev)
{
	uint64_t offset;
	uint32_t size;

	/* programm memory controller bits 0-27 */
	WREG32(mmUVD_LMI_VCPU_CACHE_64BIT_BAR_LOW,
			lower_32_bits(adev->uvd.gpu_addr));
	WREG32(mmUVD_LMI_VCPU_CACHE_64BIT_BAR_HIGH,
			upper_32_bits(adev->uvd.gpu_addr));

	offset = AMDGPU_UVD_FIRMWARE_OFFSET;
	size = AMDGPU_GPU_PAGE_ALIGN(adev->uvd.fw->size + 4);
	WREG32(mmUVD_VCPU_CACHE_OFFSET0, offset >> 3);
	WREG32(mmUVD_VCPU_CACHE_SIZE0, size);

	offset += size;
	size = AMDGPU_UVD_HEAP_SIZE;
	WREG32(mmUVD_VCPU_CACHE_OFFSET1, offset >> 3);
	WREG32(mmUVD_VCPU_CACHE_SIZE1, size);

	offset += size;
	size = AMDGPU_UVD_STACK_SIZE +
	       (AMDGPU_UVD_SESSION_SIZE * adev->uvd.max_handles);
	WREG32(mmUVD_VCPU_CACHE_OFFSET2, offset >> 3);
	WREG32(mmUVD_VCPU_CACHE_SIZE2, size);

	WREG32(mmUVD_UDEC_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmUVD_UDEC_DB_ADDR_CONFIG, adev->gfx.config.gb_addr_config);
	WREG32(mmUVD_UDEC_DBW_ADDR_CONFIG, adev->gfx.config.gb_addr_config);

	WREG32(mmUVD_GP_SCRATCH4, adev->uvd.max_handles);
}

#if 0
static void cz_set_uvd_clock_gating_branches(struct amdgpu_device *adev,
		bool enable)
{
	u32 data, data1;

	data = RREG32(mmUVD_CGC_GATE);
	data1 = RREG32(mmUVD_SUVD_CGC_GATE);
	if (enable) {
		data |= UVD_CGC_GATE__SYS_MASK |
				UVD_CGC_GATE__UDEC_MASK |
				UVD_CGC_GATE__MPEG2_MASK |
				UVD_CGC_GATE__RBC_MASK |
				UVD_CGC_GATE__LMI_MC_MASK |
				UVD_CGC_GATE__IDCT_MASK |
				UVD_CGC_GATE__MPRD_MASK |
				UVD_CGC_GATE__MPC_MASK |
				UVD_CGC_GATE__LBSI_MASK |
				UVD_CGC_GATE__LRBBM_MASK |
				UVD_CGC_GATE__UDEC_RE_MASK |
				UVD_CGC_GATE__UDEC_CM_MASK |
				UVD_CGC_GATE__UDEC_IT_MASK |
				UVD_CGC_GATE__UDEC_DB_MASK |
				UVD_CGC_GATE__UDEC_MP_MASK |
				UVD_CGC_GATE__WCB_MASK |
				UVD_CGC_GATE__VCPU_MASK |
				UVD_CGC_GATE__SCPU_MASK;
		data1 |= UVD_SUVD_CGC_GATE__SRE_MASK |
				UVD_SUVD_CGC_GATE__SIT_MASK |
				UVD_SUVD_CGC_GATE__SMP_MASK |
				UVD_SUVD_CGC_GATE__SCM_MASK |
				UVD_SUVD_CGC_GATE__SDB_MASK |
				UVD_SUVD_CGC_GATE__SRE_H264_MASK |
				UVD_SUVD_CGC_GATE__SRE_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SIT_H264_MASK |
				UVD_SUVD_CGC_GATE__SIT_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SCM_H264_MASK |
				UVD_SUVD_CGC_GATE__SCM_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SDB_H264_MASK |
				UVD_SUVD_CGC_GATE__SDB_HEVC_MASK;
	} else {
		data &= ~(UVD_CGC_GATE__SYS_MASK |
				UVD_CGC_GATE__UDEC_MASK |
				UVD_CGC_GATE__MPEG2_MASK |
				UVD_CGC_GATE__RBC_MASK |
				UVD_CGC_GATE__LMI_MC_MASK |
				UVD_CGC_GATE__LMI_UMC_MASK |
				UVD_CGC_GATE__IDCT_MASK |
				UVD_CGC_GATE__MPRD_MASK |
				UVD_CGC_GATE__MPC_MASK |
				UVD_CGC_GATE__LBSI_MASK |
				UVD_CGC_GATE__LRBBM_MASK |
				UVD_CGC_GATE__UDEC_RE_MASK |
				UVD_CGC_GATE__UDEC_CM_MASK |
				UVD_CGC_GATE__UDEC_IT_MASK |
				UVD_CGC_GATE__UDEC_DB_MASK |
				UVD_CGC_GATE__UDEC_MP_MASK |
				UVD_CGC_GATE__WCB_MASK |
				UVD_CGC_GATE__VCPU_MASK |
				UVD_CGC_GATE__SCPU_MASK);
		data1 &= ~(UVD_SUVD_CGC_GATE__SRE_MASK |
				UVD_SUVD_CGC_GATE__SIT_MASK |
				UVD_SUVD_CGC_GATE__SMP_MASK |
				UVD_SUVD_CGC_GATE__SCM_MASK |
				UVD_SUVD_CGC_GATE__SDB_MASK |
				UVD_SUVD_CGC_GATE__SRE_H264_MASK |
				UVD_SUVD_CGC_GATE__SRE_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SIT_H264_MASK |
				UVD_SUVD_CGC_GATE__SIT_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SCM_H264_MASK |
				UVD_SUVD_CGC_GATE__SCM_HEVC_MASK |
				UVD_SUVD_CGC_GATE__SDB_H264_MASK |
				UVD_SUVD_CGC_GATE__SDB_HEVC_MASK);
	}
	WREG32(mmUVD_CGC_GATE, data);
	WREG32(mmUVD_SUVD_CGC_GATE, data1);
}
#endif

/**
 * uvd_v6_0_start - start UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * Setup and start the UVD block
 */
static int uvd_v6_0_start(struct amdgpu_device *adev)
{
	struct amdgpu_ring *ring = &adev->uvd.ring;
	uint32_t rb_bufsz, tmp;
	uint32_t lmi_swap_cntl;
	uint32_t mp_swap_cntl;
	int i, j, r;

	/* disable DPG */
	WREG32_P(mmUVD_POWER_STATUS, 0, ~UVD_POWER_STATUS__UVD_PG_MODE_MASK);

	/* disable byte swapping */
	lmi_swap_cntl = 0;
	mp_swap_cntl = 0;

	uvd_v6_0_mc_resume(adev);

	/* disable clock gating */
	WREG32_FIELD(UVD_CGC_CTRL, DYN_CLOCK_MODE, 0);

	/* disable interupt */
	WREG32_FIELD(UVD_MASTINT_EN, VCPU_EN, 0);

	/* stall UMC and register bus before resetting VCPU */
	WREG32_FIELD(UVD_LMI_CTRL2, STALL_ARB_UMC, 1);
	mdelay(1);

	/* put LMI, VCPU, RBC etc... into reset */
	WREG32(mmUVD_SOFT_RESET,
		UVD_SOFT_RESET__LMI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LBSI_SOFT_RESET_MASK |
		UVD_SOFT_RESET__RBC_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CSM_SOFT_RESET_MASK |
		UVD_SOFT_RESET__CXW_SOFT_RESET_MASK |
		UVD_SOFT_RESET__TAP_SOFT_RESET_MASK |
		UVD_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK);
	mdelay(5);

	/* take UVD block out of reset */
	WREG32_FIELD(SRBM_SOFT_RESET, SOFT_RESET_UVD, 0);
	mdelay(5);

	/* initialize UVD memory controller */
	WREG32(mmUVD_LMI_CTRL,
		(0x40 << UVD_LMI_CTRL__WRITE_CLEAN_TIMER__SHIFT) |
		UVD_LMI_CTRL__WRITE_CLEAN_TIMER_EN_MASK |
		UVD_LMI_CTRL__DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK |
		UVD_LMI_CTRL__REQ_MODE_MASK |
		UVD_LMI_CTRL__DISABLE_ON_FWV_FAIL_MASK);

#ifdef __BIG_ENDIAN
	/* swap (8 in 32) RB and IB */
	lmi_swap_cntl = 0xa;
	mp_swap_cntl = 0;
#endif
	WREG32(mmUVD_LMI_SWAP_CNTL, lmi_swap_cntl);
	WREG32(mmUVD_MP_SWAP_CNTL, mp_swap_cntl);

	WREG32(mmUVD_MPC_SET_MUXA0, 0x40c2040);
	WREG32(mmUVD_MPC_SET_MUXA1, 0x0);
	WREG32(mmUVD_MPC_SET_MUXB0, 0x40c2040);
	WREG32(mmUVD_MPC_SET_MUXB1, 0x0);
	WREG32(mmUVD_MPC_SET_ALU, 0);
	WREG32(mmUVD_MPC_SET_MUX, 0x88);

	/* take all subblocks out of reset, except VCPU */
	WREG32(mmUVD_SOFT_RESET, UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* enable VCPU clock */
	WREG32(mmUVD_VCPU_CNTL, UVD_VCPU_CNTL__CLK_EN_MASK);

	/* enable UMC */
	WREG32_FIELD(UVD_LMI_CTRL2, STALL_ARB_UMC, 0);

	/* boot up the VCPU */
	WREG32(mmUVD_SOFT_RESET, 0);
	mdelay(10);

	for (i = 0; i < 10; ++i) {
		uint32_t status;

		for (j = 0; j < 100; ++j) {
			status = RREG32(mmUVD_STATUS);
			if (status & 2)
				break;
			mdelay(10);
		}
		r = 0;
		if (status & 2)
			break;

		DRM_ERROR("UVD not responding, trying to reset the VCPU!!!\n");
		WREG32_FIELD(UVD_SOFT_RESET, VCPU_SOFT_RESET, 1);
		mdelay(10);
		WREG32_FIELD(UVD_SOFT_RESET, VCPU_SOFT_RESET, 0);
		mdelay(10);
		r = -1;
	}

	if (r) {
		DRM_ERROR("UVD not responding, giving up!!!\n");
		return r;
	}
	/* enable master interrupt */
	WREG32_P(mmUVD_MASTINT_EN,
		(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK),
		~(UVD_MASTINT_EN__VCPU_EN_MASK|UVD_MASTINT_EN__SYS_EN_MASK));

	/* clear the bit 4 of UVD_STATUS */
	WREG32_P(mmUVD_STATUS, 0, ~(2 << UVD_STATUS__VCPU_REPORT__SHIFT));

	/* force RBC into idle state */
	rb_bufsz = order_base_2(ring->ring_size);
	tmp = REG_SET_FIELD(0, UVD_RBC_RB_CNTL, RB_BUFSZ, rb_bufsz);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_BLKSZ, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_FETCH, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_WPTR_POLL_EN, 0);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_NO_UPDATE, 1);
	tmp = REG_SET_FIELD(tmp, UVD_RBC_RB_CNTL, RB_RPTR_WR_EN, 1);
	WREG32(mmUVD_RBC_RB_CNTL, tmp);

	/* set the write pointer delay */
	WREG32(mmUVD_RBC_RB_WPTR_CNTL, 0);

	/* set the wb address */
	WREG32(mmUVD_RBC_RB_RPTR_ADDR, (upper_32_bits(ring->gpu_addr) >> 2));

	/* programm the RB_BASE for ring buffer */
	WREG32(mmUVD_LMI_RBC_RB_64BIT_BAR_LOW,
			lower_32_bits(ring->gpu_addr));
	WREG32(mmUVD_LMI_RBC_RB_64BIT_BAR_HIGH,
			upper_32_bits(ring->gpu_addr));

	/* Initialize the ring buffer's read and write pointers */
	WREG32(mmUVD_RBC_RB_RPTR, 0);

	ring->wptr = RREG32(mmUVD_RBC_RB_RPTR);
	WREG32(mmUVD_RBC_RB_WPTR, ring->wptr);

	WREG32_FIELD(UVD_RBC_RB_CNTL, RB_NO_FETCH, 0);

	return 0;
}

/**
 * uvd_v6_0_stop - stop UVD block
 *
 * @adev: amdgpu_device pointer
 *
 * stop the UVD block
 */
static void uvd_v6_0_stop(struct amdgpu_device *adev)
{
	/* force RBC into idle state */
	WREG32(mmUVD_RBC_RB_CNTL, 0x11010101);

	/* Stall UMC and register bus before resetting VCPU */
	WREG32_P(mmUVD_LMI_CTRL2, 1 << 8, ~(1 << 8));
	mdelay(1);

	/* put VCPU into reset */
	WREG32(mmUVD_SOFT_RESET, UVD_SOFT_RESET__VCPU_SOFT_RESET_MASK);
	mdelay(5);

	/* disable VCPU clock */
	WREG32(mmUVD_VCPU_CNTL, 0x0);

	/* Unstall UMC and register bus */
	WREG32_P(mmUVD_LMI_CTRL2, 0, ~(1 << 8));
}

/**
 * uvd_v6_0_ring_emit_fence - emit an fence & trap command
 *
 * @ring: amdgpu_ring pointer
 * @fence: fence to emit
 *
 * Write a fence and a trap command to the ring.
 */
static void uvd_v6_0_ring_emit_fence(struct amdgpu_ring *ring, u64 addr, u64 seq,
				     unsigned flags)
{
	WARN_ON(flags & AMDGPU_FENCE_FLAG_64BIT);

	amdgpu_ring_write(ring, PACKET0(mmUVD_CONTEXT_ID, 0));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, addr & 0xffffffff);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, upper_32_bits(addr) & 0xff);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0);

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 2);
}

/**
 * uvd_v6_0_ring_emit_hdp_flush - emit an hdp flush
 *
 * @ring: amdgpu_ring pointer
 *
 * Emits an hdp flush.
 */
static void uvd_v6_0_ring_emit_hdp_flush(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET0(mmHDP_MEM_COHERENCY_FLUSH_CNTL, 0));
	amdgpu_ring_write(ring, 0);
}

/**
 * uvd_v6_0_ring_hdp_invalidate - emit an hdp invalidate
 *
 * @ring: amdgpu_ring pointer
 *
 * Emits an hdp invalidate.
 */
static void uvd_v6_0_ring_emit_hdp_invalidate(struct amdgpu_ring *ring)
{
	amdgpu_ring_write(ring, PACKET0(mmHDP_DEBUG0, 0));
	amdgpu_ring_write(ring, 1);
}

/**
 * uvd_v6_0_ring_test_ring - register write test
 *
 * @ring: amdgpu_ring pointer
 *
 * Test if we can successfully write to the context register
 */
static int uvd_v6_0_ring_test_ring(struct amdgpu_ring *ring)
{
	struct amdgpu_device *adev = ring->adev;
	uint32_t tmp = 0;
	unsigned i;
	int r;

	WREG32(mmUVD_CONTEXT_ID, 0xCAFEDEAD);
	r = amdgpu_ring_alloc(ring, 3);
	if (r) {
		DRM_ERROR("amdgpu: cp failed to lock ring %d (%d).\n",
			  ring->idx, r);
		return r;
	}
	amdgpu_ring_write(ring, PACKET0(mmUVD_CONTEXT_ID, 0));
	amdgpu_ring_write(ring, 0xDEADBEEF);
	amdgpu_ring_commit(ring);
	for (i = 0; i < adev->usec_timeout; i++) {
		tmp = RREG32(mmUVD_CONTEXT_ID);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < adev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n",
			 ring->idx, i);
	} else {
		DRM_ERROR("amdgpu: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * uvd_v6_0_ring_emit_ib - execute indirect buffer
 *
 * @ring: amdgpu_ring pointer
 * @ib: indirect buffer to execute
 *
 * Write ring commands to execute the indirect buffer
 */
static void uvd_v6_0_ring_emit_ib(struct amdgpu_ring *ring,
				  struct amdgpu_ib *ib,
				  unsigned vm_id, bool ctx_switch)
{
	amdgpu_ring_write(ring, PACKET0(mmUVD_LMI_RBC_IB_VMID, 0));
	amdgpu_ring_write(ring, vm_id);

	amdgpu_ring_write(ring, PACKET0(mmUVD_LMI_RBC_IB_64BIT_BAR_LOW, 0));
	amdgpu_ring_write(ring, lower_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, PACKET0(mmUVD_LMI_RBC_IB_64BIT_BAR_HIGH, 0));
	amdgpu_ring_write(ring, upper_32_bits(ib->gpu_addr));
	amdgpu_ring_write(ring, PACKET0(mmUVD_RBC_IB_SIZE, 0));
	amdgpu_ring_write(ring, ib->length_dw);
}

static void uvd_v6_0_ring_emit_vm_flush(struct amdgpu_ring *ring,
					 unsigned vm_id, uint64_t pd_addr)
{
	uint32_t reg;

	if (vm_id < 8)
		reg = mmVM_CONTEXT0_PAGE_TABLE_BASE_ADDR + vm_id;
	else
		reg = mmVM_CONTEXT8_PAGE_TABLE_BASE_ADDR + vm_id - 8;

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, reg << 2);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, pd_addr >> 12);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST << 2);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, 1 << vm_id);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0x8);

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, mmVM_INVALIDATE_REQUEST << 2);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, 0);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GP_SCRATCH8, 0));
	amdgpu_ring_write(ring, 1 << vm_id); /* mask */
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0xC);
}

static void uvd_v6_0_ring_emit_pipeline_sync(struct amdgpu_ring *ring)
{
	uint32_t seq = ring->fence_drv.sync_seq;
	uint64_t addr = ring->fence_drv.gpu_addr;

	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA0, 0));
	amdgpu_ring_write(ring, lower_32_bits(addr));
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_DATA1, 0));
	amdgpu_ring_write(ring, upper_32_bits(addr));
	amdgpu_ring_write(ring, PACKET0(mmUVD_GP_SCRATCH8, 0));
	amdgpu_ring_write(ring, 0xffffffff); /* mask */
	amdgpu_ring_write(ring, PACKET0(mmUVD_GP_SCRATCH9, 0));
	amdgpu_ring_write(ring, seq);
	amdgpu_ring_write(ring, PACKET0(mmUVD_GPCOM_VCPU_CMD, 0));
	amdgpu_ring_write(ring, 0xE);
}

static unsigned uvd_v6_0_ring_get_emit_ib_size(struct amdgpu_ring *ring)
{
	return
		8; /* uvd_v6_0_ring_emit_ib */
}

static unsigned uvd_v6_0_ring_get_dma_frame_size(struct amdgpu_ring *ring)
{
	return
		2 + /* uvd_v6_0_ring_emit_hdp_flush */
		2 + /* uvd_v6_0_ring_emit_hdp_invalidate */
		10 + /* uvd_v6_0_ring_emit_pipeline_sync */
		14; /* uvd_v6_0_ring_emit_fence x1 no user fence */
}

static unsigned uvd_v6_0_ring_get_dma_frame_size_vm(struct amdgpu_ring *ring)
{
	return
		2 + /* uvd_v6_0_ring_emit_hdp_flush */
		2 + /* uvd_v6_0_ring_emit_hdp_invalidate */
		10 + /* uvd_v6_0_ring_emit_pipeline_sync */
		20 + /* uvd_v6_0_ring_emit_vm_flush */
		14 + 14; /* uvd_v6_0_ring_emit_fence x2 vm fence */
}

static bool uvd_v6_0_is_idle(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	return !(RREG32(mmSRBM_STATUS) & SRBM_STATUS__UVD_BUSY_MASK);
}

static int uvd_v6_0_wait_for_idle(void *handle)
{
	unsigned i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	for (i = 0; i < adev->usec_timeout; i++) {
		if (uvd_v6_0_is_idle(handle))
			return 0;
	}
	return -ETIMEDOUT;
}

#define AMDGPU_UVD_STATUS_BUSY_MASK    0xfd
static bool uvd_v6_0_check_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset = 0;
	u32 tmp = RREG32(mmSRBM_STATUS);

	if (REG_GET_FIELD(tmp, SRBM_STATUS, UVD_RQ_PENDING) ||
	    REG_GET_FIELD(tmp, SRBM_STATUS, UVD_BUSY) ||
	    (RREG32(mmUVD_STATUS) & AMDGPU_UVD_STATUS_BUSY_MASK))
		srbm_soft_reset = REG_SET_FIELD(srbm_soft_reset, SRBM_SOFT_RESET, SOFT_RESET_UVD, 1);

	if (srbm_soft_reset) {
		adev->uvd.srbm_soft_reset = srbm_soft_reset;
		return true;
	} else {
		adev->uvd.srbm_soft_reset = 0;
		return false;
	}
}

static int uvd_v6_0_pre_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->uvd.srbm_soft_reset)
		return 0;

	uvd_v6_0_stop(adev);
	return 0;
}

static int uvd_v6_0_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	u32 srbm_soft_reset;

	if (!adev->uvd.srbm_soft_reset)
		return 0;
	srbm_soft_reset = adev->uvd.srbm_soft_reset;

	if (srbm_soft_reset) {
		u32 tmp;

		tmp = RREG32(mmSRBM_SOFT_RESET);
		tmp |= srbm_soft_reset;
		dev_info(adev->dev, "SRBM_SOFT_RESET=0x%08X\n", tmp);
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		udelay(50);

		tmp &= ~srbm_soft_reset;
		WREG32(mmSRBM_SOFT_RESET, tmp);
		tmp = RREG32(mmSRBM_SOFT_RESET);

		/* Wait a little for things to settle down */
		udelay(50);
	}

	return 0;
}

static int uvd_v6_0_post_soft_reset(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!adev->uvd.srbm_soft_reset)
		return 0;

	mdelay(5);

	return uvd_v6_0_start(adev);
}

static int uvd_v6_0_set_interrupt_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned type,
					enum amdgpu_interrupt_state state)
{
	// TODO
	return 0;
}

static int uvd_v6_0_process_interrupt(struct amdgpu_device *adev,
				      struct amdgpu_irq_src *source,
				      struct amdgpu_iv_entry *entry)
{
	DRM_DEBUG("IH: UVD TRAP\n");
	amdgpu_fence_process(&adev->uvd.ring);
	return 0;
}

static void uvd_v6_0_set_sw_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data, data1, data2, suvd_flags;

	data = RREG32(mmUVD_CGC_CTRL);
	data1 = RREG32(mmUVD_SUVD_CGC_GATE);
	data2 = RREG32(mmUVD_SUVD_CGC_CTRL);

	data &= ~(UVD_CGC_CTRL__CLK_OFF_DELAY_MASK |
		  UVD_CGC_CTRL__CLK_GATE_DLY_TIMER_MASK);

	suvd_flags = UVD_SUVD_CGC_GATE__SRE_MASK |
		     UVD_SUVD_CGC_GATE__SIT_MASK |
		     UVD_SUVD_CGC_GATE__SMP_MASK |
		     UVD_SUVD_CGC_GATE__SCM_MASK |
		     UVD_SUVD_CGC_GATE__SDB_MASK;

	data |= UVD_CGC_CTRL__DYN_CLOCK_MODE_MASK |
		(1 << REG_FIELD_SHIFT(UVD_CGC_CTRL, CLK_GATE_DLY_TIMER)) |
		(4 << REG_FIELD_SHIFT(UVD_CGC_CTRL, CLK_OFF_DELAY));

	data &= ~(UVD_CGC_CTRL__UDEC_RE_MODE_MASK |
			UVD_CGC_CTRL__UDEC_CM_MODE_MASK |
			UVD_CGC_CTRL__UDEC_IT_MODE_MASK |
			UVD_CGC_CTRL__UDEC_DB_MODE_MASK |
			UVD_CGC_CTRL__UDEC_MP_MODE_MASK |
			UVD_CGC_CTRL__SYS_MODE_MASK |
			UVD_CGC_CTRL__UDEC_MODE_MASK |
			UVD_CGC_CTRL__MPEG2_MODE_MASK |
			UVD_CGC_CTRL__REGS_MODE_MASK |
			UVD_CGC_CTRL__RBC_MODE_MASK |
			UVD_CGC_CTRL__LMI_MC_MODE_MASK |
			UVD_CGC_CTRL__LMI_UMC_MODE_MASK |
			UVD_CGC_CTRL__IDCT_MODE_MASK |
			UVD_CGC_CTRL__MPRD_MODE_MASK |
			UVD_CGC_CTRL__MPC_MODE_MASK |
			UVD_CGC_CTRL__LBSI_MODE_MASK |
			UVD_CGC_CTRL__LRBBM_MODE_MASK |
			UVD_CGC_CTRL__WCB_MODE_MASK |
			UVD_CGC_CTRL__VCPU_MODE_MASK |
			UVD_CGC_CTRL__JPEG_MODE_MASK |
			UVD_CGC_CTRL__SCPU_MODE_MASK |
			UVD_CGC_CTRL__JPEG2_MODE_MASK);
	data2 &= ~(UVD_SUVD_CGC_CTRL__SRE_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SIT_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SMP_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SCM_MODE_MASK |
			UVD_SUVD_CGC_CTRL__SDB_MODE_MASK);
	data1 |= suvd_flags;

	WREG32(mmUVD_CGC_CTRL, data);
	WREG32(mmUVD_CGC_GATE, 0);
	WREG32(mmUVD_SUVD_CGC_GATE, data1);
	WREG32(mmUVD_SUVD_CGC_CTRL, data2);
}

#if 0
static void uvd_v6_0_set_hw_clock_gating(struct amdgpu_device *adev)
{
	uint32_t data, data1, cgc_flags, suvd_flags;

	data = RREG32(mmUVD_CGC_GATE);
	data1 = RREG32(mmUVD_SUVD_CGC_GATE);

	cgc_flags = UVD_CGC_GATE__SYS_MASK |
		UVD_CGC_GATE__UDEC_MASK |
		UVD_CGC_GATE__MPEG2_MASK |
		UVD_CGC_GATE__RBC_MASK |
		UVD_CGC_GATE__LMI_MC_MASK |
		UVD_CGC_GATE__IDCT_MASK |
		UVD_CGC_GATE__MPRD_MASK |
		UVD_CGC_GATE__MPC_MASK |
		UVD_CGC_GATE__LBSI_MASK |
		UVD_CGC_GATE__LRBBM_MASK |
		UVD_CGC_GATE__UDEC_RE_MASK |
		UVD_CGC_GATE__UDEC_CM_MASK |
		UVD_CGC_GATE__UDEC_IT_MASK |
		UVD_CGC_GATE__UDEC_DB_MASK |
		UVD_CGC_GATE__UDEC_MP_MASK |
		UVD_CGC_GATE__WCB_MASK |
		UVD_CGC_GATE__VCPU_MASK |
		UVD_CGC_GATE__SCPU_MASK |
		UVD_CGC_GATE__JPEG_MASK |
		UVD_CGC_GATE__JPEG2_MASK;

	suvd_flags = UVD_SUVD_CGC_GATE__SRE_MASK |
				UVD_SUVD_CGC_GATE__SIT_MASK |
				UVD_SUVD_CGC_GATE__SMP_MASK |
				UVD_SUVD_CGC_GATE__SCM_MASK |
				UVD_SUVD_CGC_GATE__SDB_MASK;

	data |= cgc_flags;
	data1 |= suvd_flags;

	WREG32(mmUVD_CGC_GATE, data);
	WREG32(mmUVD_SUVD_CGC_GATE, data1);
}
#endif

static void uvd_v6_set_bypass_mode(struct amdgpu_device *adev, bool enable)
{
	u32 tmp = RREG32_SMC(ixGCK_DFS_BYPASS_CNTL);

	if (enable)
		tmp |= (GCK_DFS_BYPASS_CNTL__BYPASSDCLK_MASK |
			GCK_DFS_BYPASS_CNTL__BYPASSVCLK_MASK);
	else
		tmp &= ~(GCK_DFS_BYPASS_CNTL__BYPASSDCLK_MASK |
			 GCK_DFS_BYPASS_CNTL__BYPASSVCLK_MASK);

	WREG32_SMC(ixGCK_DFS_BYPASS_CNTL, tmp);
}

static int uvd_v6_0_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->asic_type == CHIP_FIJI ||
	    adev->asic_type == CHIP_POLARIS10)
		uvd_v6_set_bypass_mode(adev, state == AMD_CG_STATE_GATE ? true : false);

	if (!(adev->cg_flags & AMD_CG_SUPPORT_UVD_MGCG))
		return 0;

	if (state == AMD_CG_STATE_GATE) {
		/* disable HW gating and enable Sw gating */
		uvd_v6_0_set_sw_clock_gating(adev);
	} else {
		/* wait for STATUS to clear */
		if (uvd_v6_0_wait_for_idle(handle))
			return -EBUSY;

		/* enable HW gates because UVD is idle */
/*		uvd_v6_0_set_hw_clock_gating(adev); */
	}

	return 0;
}

static int uvd_v6_0_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	/* This doesn't actually powergate the UVD block.
	 * That's done in the dpm code via the SMC.  This
	 * just re-inits the block as necessary.  The actual
	 * gating still happens in the dpm code.  We should
	 * revisit this when there is a cleaner line between
	 * the smc and the hw blocks
	 */
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (!(adev->pg_flags & AMD_PG_SUPPORT_UVD))
		return 0;

	WREG32(mmUVD_POWER_STATUS, UVD_POWER_STATUS__UVD_PG_EN_MASK);

	if (state == AMD_PG_STATE_GATE) {
		uvd_v6_0_stop(adev);
		return 0;
	} else {
		return uvd_v6_0_start(adev);
	}
}

const struct amd_ip_funcs uvd_v6_0_ip_funcs = {
	.name = "uvd_v6_0",
	.early_init = uvd_v6_0_early_init,
	.late_init = NULL,
	.sw_init = uvd_v6_0_sw_init,
	.sw_fini = uvd_v6_0_sw_fini,
	.hw_init = uvd_v6_0_hw_init,
	.hw_fini = uvd_v6_0_hw_fini,
	.suspend = uvd_v6_0_suspend,
	.resume = uvd_v6_0_resume,
	.is_idle = uvd_v6_0_is_idle,
	.wait_for_idle = uvd_v6_0_wait_for_idle,
	.check_soft_reset = uvd_v6_0_check_soft_reset,
	.pre_soft_reset = uvd_v6_0_pre_soft_reset,
	.soft_reset = uvd_v6_0_soft_reset,
	.post_soft_reset = uvd_v6_0_post_soft_reset,
	.set_clockgating_state = uvd_v6_0_set_clockgating_state,
	.set_powergating_state = uvd_v6_0_set_powergating_state,
};

static const struct amdgpu_ring_funcs uvd_v6_0_ring_phys_funcs = {
	.get_rptr = uvd_v6_0_ring_get_rptr,
	.get_wptr = uvd_v6_0_ring_get_wptr,
	.set_wptr = uvd_v6_0_ring_set_wptr,
	.parse_cs = amdgpu_uvd_ring_parse_cs,
	.emit_ib = uvd_v6_0_ring_emit_ib,
	.emit_fence = uvd_v6_0_ring_emit_fence,
	.emit_hdp_flush = uvd_v6_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = uvd_v6_0_ring_emit_hdp_invalidate,
	.test_ring = uvd_v6_0_ring_test_ring,
	.test_ib = amdgpu_uvd_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_uvd_ring_begin_use,
	.end_use = amdgpu_uvd_ring_end_use,
	.get_emit_ib_size = uvd_v6_0_ring_get_emit_ib_size,
	.get_dma_frame_size = uvd_v6_0_ring_get_dma_frame_size,
};

static const struct amdgpu_ring_funcs uvd_v6_0_ring_vm_funcs = {
	.get_rptr = uvd_v6_0_ring_get_rptr,
	.get_wptr = uvd_v6_0_ring_get_wptr,
	.set_wptr = uvd_v6_0_ring_set_wptr,
	.parse_cs = NULL,
	.emit_ib = uvd_v6_0_ring_emit_ib,
	.emit_fence = uvd_v6_0_ring_emit_fence,
	.emit_vm_flush = uvd_v6_0_ring_emit_vm_flush,
	.emit_pipeline_sync = uvd_v6_0_ring_emit_pipeline_sync,
	.emit_hdp_flush = uvd_v6_0_ring_emit_hdp_flush,
	.emit_hdp_invalidate = uvd_v6_0_ring_emit_hdp_invalidate,
	.test_ring = uvd_v6_0_ring_test_ring,
	.test_ib = amdgpu_uvd_ring_test_ib,
	.insert_nop = amdgpu_ring_insert_nop,
	.pad_ib = amdgpu_ring_generic_pad_ib,
	.begin_use = amdgpu_uvd_ring_begin_use,
	.end_use = amdgpu_uvd_ring_end_use,
	.get_emit_ib_size = uvd_v6_0_ring_get_emit_ib_size,
	.get_dma_frame_size = uvd_v6_0_ring_get_dma_frame_size_vm,
};

static void uvd_v6_0_set_ring_funcs(struct amdgpu_device *adev)
{
	if (adev->asic_type >= CHIP_POLARIS10) {
		adev->uvd.ring.funcs = &uvd_v6_0_ring_vm_funcs;
		DRM_INFO("UVD is enabled in VM mode\n");
	} else {
		adev->uvd.ring.funcs = &uvd_v6_0_ring_phys_funcs;
		DRM_INFO("UVD is enabled in physical mode\n");
	}
}

static const struct amdgpu_irq_src_funcs uvd_v6_0_irq_funcs = {
	.set = uvd_v6_0_set_interrupt_state,
	.process = uvd_v6_0_process_interrupt,
};

static void uvd_v6_0_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->uvd.irq.num_types = 1;
	adev->uvd.irq.funcs = &uvd_v6_0_irq_funcs;
}
