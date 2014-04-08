/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#include <drm/drmP.h>
#include "radeon.h"
#include "radeon_asic.h"
#include "r600d.h"

u32 r600_gpu_check_soft_reset(struct radeon_device *rdev);

/*
 * DMA
 * Starting with R600, the GPU has an asynchronous
 * DMA engine.  The programming model is very similar
 * to the 3D engine (ring buffer, IBs, etc.), but the
 * DMA controller has it's own packet format that is
 * different form the PM4 format used by the 3D engine.
 * It supports copying data, writing embedded data,
 * solid fills, and a number of other things.  It also
 * has support for tiling/detiling of buffers.
 */

/**
 * r600_dma_get_rptr - get the current read pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current rptr from the hardware (r6xx+).
 */
uint32_t r600_dma_get_rptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	return (radeon_ring_generic_get_rptr(rdev, ring) & 0x3fffc) >> 2;
}

/**
 * r600_dma_get_wptr - get the current write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Get the current wptr from the hardware (r6xx+).
 */
uint32_t r600_dma_get_wptr(struct radeon_device *rdev,
			   struct radeon_ring *ring)
{
	return (RREG32(ring->wptr_reg) & 0x3fffc) >> 2;
}

/**
 * r600_dma_set_wptr - commit the write pointer
 *
 * @rdev: radeon_device pointer
 * @ring: radeon ring pointer
 *
 * Write the wptr back to the hardware (r6xx+).
 */
void r600_dma_set_wptr(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	WREG32(ring->wptr_reg, (ring->wptr << 2) & 0x3fffc);
}

/**
 * r600_dma_stop - stop the async dma engine
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engine (r6xx-evergreen).
 */
void r600_dma_stop(struct radeon_device *rdev)
{
	u32 rb_cntl = RREG32(DMA_RB_CNTL);

	radeon_ttm_set_active_vram_size(rdev, rdev->mc.visible_vram_size);

	rb_cntl &= ~DMA_RB_ENABLE;
	WREG32(DMA_RB_CNTL, rb_cntl);

	rdev->ring[R600_RING_TYPE_DMA_INDEX].ready = false;
}

/**
 * r600_dma_resume - setup and start the async dma engine
 *
 * @rdev: radeon_device pointer
 *
 * Set up the DMA ring buffer and enable it. (r6xx-evergreen).
 * Returns 0 for success, error for failure.
 */
int r600_dma_resume(struct radeon_device *rdev)
{
	struct radeon_ring *ring = &rdev->ring[R600_RING_TYPE_DMA_INDEX];
	u32 rb_cntl, dma_cntl, ib_cntl;
	u32 rb_bufsz;
	int r;

	/* Reset dma */
	if (rdev->family >= CHIP_RV770)
		WREG32(SRBM_SOFT_RESET, RV770_SOFT_RESET_DMA);
	else
		WREG32(SRBM_SOFT_RESET, SOFT_RESET_DMA);
	RREG32(SRBM_SOFT_RESET);
	udelay(50);
	WREG32(SRBM_SOFT_RESET, 0);

	WREG32(DMA_SEM_INCOMPLETE_TIMER_CNTL, 0);
	WREG32(DMA_SEM_WAIT_FAIL_TIMER_CNTL, 0);

	/* Set ring buffer size in dwords */
	rb_bufsz = order_base_2(ring->ring_size / 4);
	rb_cntl = rb_bufsz << 1;
#ifdef __BIG_ENDIAN
	rb_cntl |= DMA_RB_SWAP_ENABLE | DMA_RPTR_WRITEBACK_SWAP_ENABLE;
#endif
	WREG32(DMA_RB_CNTL, rb_cntl);

	/* Initialize the ring buffer's read and write pointers */
	WREG32(DMA_RB_RPTR, 0);
	WREG32(DMA_RB_WPTR, 0);

	/* set the wb address whether it's enabled or not */
	WREG32(DMA_RB_RPTR_ADDR_HI,
	       upper_32_bits(rdev->wb.gpu_addr + R600_WB_DMA_RPTR_OFFSET) & 0xFF);
	WREG32(DMA_RB_RPTR_ADDR_LO,
	       ((rdev->wb.gpu_addr + R600_WB_DMA_RPTR_OFFSET) & 0xFFFFFFFC));

	if (rdev->wb.enabled)
		rb_cntl |= DMA_RPTR_WRITEBACK_ENABLE;

	WREG32(DMA_RB_BASE, ring->gpu_addr >> 8);

	/* enable DMA IBs */
	ib_cntl = DMA_IB_ENABLE;
#ifdef __BIG_ENDIAN
	ib_cntl |= DMA_IB_SWAP_ENABLE;
#endif
	WREG32(DMA_IB_CNTL, ib_cntl);

	dma_cntl = RREG32(DMA_CNTL);
	dma_cntl &= ~CTXEMPTY_INT_ENABLE;
	WREG32(DMA_CNTL, dma_cntl);

	if (rdev->family >= CHIP_RV770)
		WREG32(DMA_MODE, 1);

	ring->wptr = 0;
	WREG32(DMA_RB_WPTR, ring->wptr << 2);

	ring->rptr = RREG32(DMA_RB_RPTR) >> 2;

	WREG32(DMA_RB_CNTL, rb_cntl | DMA_RB_ENABLE);

	ring->ready = true;

	r = radeon_ring_test(rdev, R600_RING_TYPE_DMA_INDEX, ring);
	if (r) {
		ring->ready = false;
		return r;
	}

	radeon_ttm_set_active_vram_size(rdev, rdev->mc.real_vram_size);

	return 0;
}

/**
 * r600_dma_fini - tear down the async dma engine
 *
 * @rdev: radeon_device pointer
 *
 * Stop the async dma engine and free the ring (r6xx-evergreen).
 */
void r600_dma_fini(struct radeon_device *rdev)
{
	r600_dma_stop(rdev);
	radeon_ring_fini(rdev, &rdev->ring[R600_RING_TYPE_DMA_INDEX]);
}

/**
 * r600_dma_is_lockup - Check if the DMA engine is locked up
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Check if the async DMA engine is locked up.
 * Returns true if the engine appears to be locked up, false if not.
 */
bool r600_dma_is_lockup(struct radeon_device *rdev, struct radeon_ring *ring)
{
	u32 reset_mask = r600_gpu_check_soft_reset(rdev);

	if (!(reset_mask & RADEON_RESET_DMA)) {
		radeon_ring_lockup_update(ring);
		return false;
	}
	/* force ring activities */
	radeon_ring_force_activity(rdev, ring);
	return radeon_ring_test_lockup(rdev, ring);
}


/**
 * r600_dma_ring_test - simple async dma engine test
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test the DMA engine by writing using it to write an
 * value to memory. (r6xx-SI).
 * Returns 0 for success, error for failure.
 */
int r600_dma_ring_test(struct radeon_device *rdev,
		       struct radeon_ring *ring)
{
	unsigned i;
	int r;
	void __iomem *ptr = (void *)rdev->vram_scratch.ptr;
	u32 tmp;

	if (!ptr) {
		DRM_ERROR("invalid vram scratch pointer\n");
		return -EINVAL;
	}

	tmp = 0xCAFEDEAD;
	writel(tmp, ptr);

	r = radeon_ring_lock(rdev, ring, 4);
	if (r) {
		DRM_ERROR("radeon: dma failed to lock ring %d (%d).\n", ring->idx, r);
		return r;
	}
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 1));
	radeon_ring_write(ring, rdev->vram_scratch.gpu_addr & 0xfffffffc);
	radeon_ring_write(ring, upper_32_bits(rdev->vram_scratch.gpu_addr) & 0xff);
	radeon_ring_write(ring, 0xDEADBEEF);
	radeon_ring_unlock_commit(rdev, ring);

	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = readl(ptr);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}

	if (i < rdev->usec_timeout) {
		DRM_INFO("ring test on %d succeeded in %d usecs\n", ring->idx, i);
	} else {
		DRM_ERROR("radeon: ring %d test failed (0x%08X)\n",
			  ring->idx, tmp);
		r = -EINVAL;
	}
	return r;
}

/**
 * r600_dma_fence_ring_emit - emit a fence on the DMA ring
 *
 * @rdev: radeon_device pointer
 * @fence: radeon fence object
 *
 * Add a DMA fence packet to the ring to write
 * the fence seq number and DMA trap packet to generate
 * an interrupt if needed (r6xx-r7xx).
 */
void r600_dma_fence_ring_emit(struct radeon_device *rdev,
			      struct radeon_fence *fence)
{
	struct radeon_ring *ring = &rdev->ring[fence->ring];
	u64 addr = rdev->fence_drv[fence->ring].gpu_addr;

	/* write the fence */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_FENCE, 0, 0, 0));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, (upper_32_bits(addr) & 0xff));
	radeon_ring_write(ring, lower_32_bits(fence->seq));
	/* generate an interrupt */
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_TRAP, 0, 0, 0));
}

/**
 * r600_dma_semaphore_ring_emit - emit a semaphore on the dma ring
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 * @semaphore: radeon semaphore object
 * @emit_wait: wait or signal semaphore
 *
 * Add a DMA semaphore packet to the ring wait on or signal
 * other rings (r6xx-SI).
 */
bool r600_dma_semaphore_ring_emit(struct radeon_device *rdev,
				  struct radeon_ring *ring,
				  struct radeon_semaphore *semaphore,
				  bool emit_wait)
{
	u64 addr = semaphore->gpu_addr;
	u32 s = emit_wait ? 0 : 1;

	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_SEMAPHORE, 0, s, 0));
	radeon_ring_write(ring, addr & 0xfffffffc);
	radeon_ring_write(ring, upper_32_bits(addr) & 0xff);

	return true;
}

/**
 * r600_dma_ib_test - test an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ring: radeon_ring structure holding ring information
 *
 * Test a simple IB in the DMA ring (r6xx-SI).
 * Returns 0 on success, error on failure.
 */
int r600_dma_ib_test(struct radeon_device *rdev, struct radeon_ring *ring)
{
	struct radeon_ib ib;
	unsigned i;
	int r;
	void __iomem *ptr = (void *)rdev->vram_scratch.ptr;
	u32 tmp = 0;

	if (!ptr) {
		DRM_ERROR("invalid vram scratch pointer\n");
		return -EINVAL;
	}

	tmp = 0xCAFEDEAD;
	writel(tmp, ptr);

	r = radeon_ib_get(rdev, ring->idx, &ib, NULL, 256);
	if (r) {
		DRM_ERROR("radeon: failed to get ib (%d).\n", r);
		return r;
	}

	ib.ptr[0] = DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 1);
	ib.ptr[1] = rdev->vram_scratch.gpu_addr & 0xfffffffc;
	ib.ptr[2] = upper_32_bits(rdev->vram_scratch.gpu_addr) & 0xff;
	ib.ptr[3] = 0xDEADBEEF;
	ib.length_dw = 4;

	r = radeon_ib_schedule(rdev, &ib, NULL);
	if (r) {
		radeon_ib_free(rdev, &ib);
		DRM_ERROR("radeon: failed to schedule ib (%d).\n", r);
		return r;
	}
	r = radeon_fence_wait(ib.fence, false);
	if (r) {
		DRM_ERROR("radeon: fence wait failed (%d).\n", r);
		return r;
	}
	for (i = 0; i < rdev->usec_timeout; i++) {
		tmp = readl(ptr);
		if (tmp == 0xDEADBEEF)
			break;
		DRM_UDELAY(1);
	}
	if (i < rdev->usec_timeout) {
		DRM_INFO("ib test on ring %d succeeded in %u usecs\n", ib.fence->ring, i);
	} else {
		DRM_ERROR("radeon: ib test failed (0x%08X)\n", tmp);
		r = -EINVAL;
	}
	radeon_ib_free(rdev, &ib);
	return r;
}

/**
 * r600_dma_ring_ib_execute - Schedule an IB on the DMA engine
 *
 * @rdev: radeon_device pointer
 * @ib: IB object to schedule
 *
 * Schedule an IB in the DMA ring (r6xx-r7xx).
 */
void r600_dma_ring_ib_execute(struct radeon_device *rdev, struct radeon_ib *ib)
{
	struct radeon_ring *ring = &rdev->ring[ib->ring];

	if (rdev->wb.enabled) {
		u32 next_rptr = ring->wptr + 4;
		while ((next_rptr & 7) != 5)
			next_rptr++;
		next_rptr += 3;
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_WRITE, 0, 0, 1));
		radeon_ring_write(ring, ring->next_rptr_gpu_addr & 0xfffffffc);
		radeon_ring_write(ring, upper_32_bits(ring->next_rptr_gpu_addr) & 0xff);
		radeon_ring_write(ring, next_rptr);
	}

	/* The indirect buffer packet must end on an 8 DW boundary in the DMA ring.
	 * Pad as necessary with NOPs.
	 */
	while ((ring->wptr & 7) != 5)
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_NOP, 0, 0, 0));
	radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_INDIRECT_BUFFER, 0, 0, 0));
	radeon_ring_write(ring, (ib->gpu_addr & 0xFFFFFFE0));
	radeon_ring_write(ring, (ib->length_dw << 16) | (upper_32_bits(ib->gpu_addr) & 0xFF));

}

/**
 * r600_copy_dma - copy pages using the DMA engine
 *
 * @rdev: radeon_device pointer
 * @src_offset: src GPU address
 * @dst_offset: dst GPU address
 * @num_gpu_pages: number of GPU pages to xfer
 * @fence: radeon fence object
 *
 * Copy GPU paging using the DMA engine (r6xx).
 * Used by the radeon ttm implementation to move pages if
 * registered as the asic copy callback.
 */
int r600_copy_dma(struct radeon_device *rdev,
		  uint64_t src_offset, uint64_t dst_offset,
		  unsigned num_gpu_pages,
		  struct radeon_fence **fence)
{
	struct radeon_semaphore *sem = NULL;
	int ring_index = rdev->asic->copy.dma_ring_index;
	struct radeon_ring *ring = &rdev->ring[ring_index];
	u32 size_in_dw, cur_size_in_dw;
	int i, num_loops;
	int r = 0;

	r = radeon_semaphore_create(rdev, &sem);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		return r;
	}

	size_in_dw = (num_gpu_pages << RADEON_GPU_PAGE_SHIFT) / 4;
	num_loops = DIV_ROUND_UP(size_in_dw, 0xFFFE);
	r = radeon_ring_lock(rdev, ring, num_loops * 4 + 8);
	if (r) {
		DRM_ERROR("radeon: moving bo (%d).\n", r);
		radeon_semaphore_free(rdev, &sem, NULL);
		return r;
	}

	radeon_semaphore_sync_to(sem, *fence);
	radeon_semaphore_sync_rings(rdev, sem, ring->idx);

	for (i = 0; i < num_loops; i++) {
		cur_size_in_dw = size_in_dw;
		if (cur_size_in_dw > 0xFFFE)
			cur_size_in_dw = 0xFFFE;
		size_in_dw -= cur_size_in_dw;
		radeon_ring_write(ring, DMA_PACKET(DMA_PACKET_COPY, 0, 0, cur_size_in_dw));
		radeon_ring_write(ring, dst_offset & 0xfffffffc);
		radeon_ring_write(ring, src_offset & 0xfffffffc);
		radeon_ring_write(ring, (((upper_32_bits(dst_offset) & 0xff) << 16) |
					 (upper_32_bits(src_offset) & 0xff)));
		src_offset += cur_size_in_dw * 4;
		dst_offset += cur_size_in_dw * 4;
	}

	r = radeon_fence_emit(rdev, fence, ring->idx);
	if (r) {
		radeon_ring_unlock_undo(rdev, ring);
		return r;
	}

	radeon_ring_unlock_commit(rdev, ring);
	radeon_semaphore_free(rdev, &sem, *fence);

	return r;
}
