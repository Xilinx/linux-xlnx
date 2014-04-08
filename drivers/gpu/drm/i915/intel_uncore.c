/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "i915_drv.h"
#include "intel_drv.h"

#define FORCEWAKE_ACK_TIMEOUT_MS 2

#define __raw_i915_read8(dev_priv__, reg__) readb((dev_priv__)->regs + (reg__))
#define __raw_i915_write8(dev_priv__, reg__, val__) writeb(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read16(dev_priv__, reg__) readw((dev_priv__)->regs + (reg__))
#define __raw_i915_write16(dev_priv__, reg__, val__) writew(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read32(dev_priv__, reg__) readl((dev_priv__)->regs + (reg__))
#define __raw_i915_write32(dev_priv__, reg__, val__) writel(val__, (dev_priv__)->regs + (reg__))

#define __raw_i915_read64(dev_priv__, reg__) readq((dev_priv__)->regs + (reg__))
#define __raw_i915_write64(dev_priv__, reg__, val__) writeq(val__, (dev_priv__)->regs + (reg__))

#define __raw_posting_read(dev_priv__, reg__) (void)__raw_i915_read32(dev_priv__, reg__)


static void __gen6_gt_wait_for_thread_c0(struct drm_i915_private *dev_priv)
{
	u32 gt_thread_status_mask;

	if (IS_HASWELL(dev_priv->dev))
		gt_thread_status_mask = GEN6_GT_THREAD_STATUS_CORE_MASK_HSW;
	else
		gt_thread_status_mask = GEN6_GT_THREAD_STATUS_CORE_MASK;

	/* w/a for a sporadic read returning 0 by waiting for the GT
	 * thread to wake up.
	 */
	if (wait_for_atomic_us((__raw_i915_read32(dev_priv, GEN6_GT_THREAD_STATUS_REG) & gt_thread_status_mask) == 0, 500))
		DRM_ERROR("GT thread status wait timed out\n");
}

static void __gen6_gt_force_wake_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE, 0);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);
}

static void __gen6_gt_force_wake_get(struct drm_i915_private *dev_priv)
{
	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK) & 1) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake old ack to clear.\n");

	__raw_i915_write32(dev_priv, FORCEWAKE, 1);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);

	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK) & 1),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake to ack request.\n");

	/* WaRsForcewakeWaitTC0:snb */
	__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void __gen6_gt_force_wake_mt_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_MT, _MASKED_BIT_DISABLE(0xffff));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);
}

static void __gen6_gt_force_wake_mt_get(struct drm_i915_private *dev_priv)
{
	u32 forcewake_ack;

	if (IS_HASWELL(dev_priv->dev) || IS_GEN8(dev_priv->dev))
		forcewake_ack = FORCEWAKE_ACK_HSW;
	else
		forcewake_ack = FORCEWAKE_MT_ACK;

	if (wait_for_atomic((__raw_i915_read32(dev_priv, forcewake_ack) & FORCEWAKE_KERNEL) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake old ack to clear.\n");

	__raw_i915_write32(dev_priv, FORCEWAKE_MT,
			   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);

	if (wait_for_atomic((__raw_i915_read32(dev_priv, forcewake_ack) & FORCEWAKE_KERNEL),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake to ack request.\n");

	/* WaRsForcewakeWaitTC0:ivb,hsw */
	if (INTEL_INFO(dev_priv->dev)->gen < 8)
		__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void gen6_gt_check_fifodbg(struct drm_i915_private *dev_priv)
{
	u32 gtfifodbg;

	gtfifodbg = __raw_i915_read32(dev_priv, GTFIFODBG);
	if (WARN(gtfifodbg & GT_FIFO_CPU_ERROR_MASK,
	     "MMIO read or write has been dropped %x\n", gtfifodbg))
		__raw_i915_write32(dev_priv, GTFIFODBG, GT_FIFO_CPU_ERROR_MASK);
}

static void __gen6_gt_force_wake_put(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE, 0);
	/* something from same cacheline, but !FORCEWAKE */
	__raw_posting_read(dev_priv, ECOBUS);
	gen6_gt_check_fifodbg(dev_priv);
}

static void __gen6_gt_force_wake_mt_put(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_MT,
			   _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));
	/* something from same cacheline, but !FORCEWAKE_MT */
	__raw_posting_read(dev_priv, ECOBUS);
	gen6_gt_check_fifodbg(dev_priv);
}

static int __gen6_gt_wait_for_fifo(struct drm_i915_private *dev_priv)
{
	int ret = 0;

	if (dev_priv->uncore.fifo_count < GT_FIFO_NUM_RESERVED_ENTRIES) {
		int loop = 500;
		u32 fifo = __raw_i915_read32(dev_priv, GT_FIFO_FREE_ENTRIES);
		while (fifo <= GT_FIFO_NUM_RESERVED_ENTRIES && loop--) {
			udelay(10);
			fifo = __raw_i915_read32(dev_priv, GT_FIFO_FREE_ENTRIES);
		}
		if (WARN_ON(loop < 0 && fifo <= GT_FIFO_NUM_RESERVED_ENTRIES))
			++ret;
		dev_priv->uncore.fifo_count = fifo;
	}
	dev_priv->uncore.fifo_count--;

	return ret;
}

static void vlv_force_wake_reset(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
			   _MASKED_BIT_DISABLE(0xffff));
	/* something from same cacheline, but !FORCEWAKE_VLV */
	__raw_posting_read(dev_priv, FORCEWAKE_ACK_VLV);
}

static void vlv_force_wake_get(struct drm_i915_private *dev_priv)
{
	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK_VLV) & FORCEWAKE_KERNEL) == 0,
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for forcewake old ack to clear.\n");

	__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
			   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));
	__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_VLV,
			   _MASKED_BIT_ENABLE(FORCEWAKE_KERNEL));

	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK_VLV) & FORCEWAKE_KERNEL),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for GT to ack forcewake request.\n");

	if (wait_for_atomic((__raw_i915_read32(dev_priv, FORCEWAKE_ACK_MEDIA_VLV) &
			     FORCEWAKE_KERNEL),
			    FORCEWAKE_ACK_TIMEOUT_MS))
		DRM_ERROR("Timed out waiting for media to ack forcewake request.\n");

	/* WaRsForcewakeWaitTC0:vlv */
	__gen6_gt_wait_for_thread_c0(dev_priv);
}

static void vlv_force_wake_put(struct drm_i915_private *dev_priv)
{
	__raw_i915_write32(dev_priv, FORCEWAKE_VLV,
			   _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));
	__raw_i915_write32(dev_priv, FORCEWAKE_MEDIA_VLV,
			   _MASKED_BIT_DISABLE(FORCEWAKE_KERNEL));
	/* The below doubles as a POSTING_READ */
	gen6_gt_check_fifodbg(dev_priv);
}

static void gen6_force_wake_work(struct work_struct *work)
{
	struct drm_i915_private *dev_priv =
		container_of(work, typeof(*dev_priv), uncore.force_wake_work.work);
	unsigned long irqflags;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	if (--dev_priv->uncore.forcewake_count == 0)
		dev_priv->uncore.funcs.force_wake_put(dev_priv);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

static void intel_uncore_forcewake_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (IS_VALLEYVIEW(dev)) {
		vlv_force_wake_reset(dev_priv);
	} else if (INTEL_INFO(dev)->gen >= 6) {
		__gen6_gt_force_wake_reset(dev_priv);
		if (IS_IVYBRIDGE(dev) || IS_HASWELL(dev))
			__gen6_gt_force_wake_mt_reset(dev_priv);
	}
}

void intel_uncore_early_sanitize(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_FPGA_DBG_UNCLAIMED(dev))
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);

	if (IS_HASWELL(dev) &&
	    (__raw_i915_read32(dev_priv, HSW_EDRAM_PRESENT) == 1)) {
		/* The docs do not explain exactly how the calculation can be
		 * made. It is somewhat guessable, but for now, it's always
		 * 128MB.
		 * NB: We can't write IDICR yet because we do not have gt funcs
		 * set up */
		dev_priv->ellc_size = 128;
		DRM_INFO("Found %zuMB of eLLC\n", dev_priv->ellc_size);
	}

	intel_uncore_forcewake_reset(dev);
}

void intel_uncore_sanitize(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 reg_val;

	intel_uncore_forcewake_reset(dev);

	/* BIOS often leaves RC6 enabled, but disable it for hw init */
	intel_disable_gt_powersave(dev);

	/* Turn off power gate, require especially for the BIOS less system */
	if (IS_VALLEYVIEW(dev)) {

		mutex_lock(&dev_priv->rps.hw_lock);
		reg_val = vlv_punit_read(dev_priv, PUNIT_REG_PWRGT_STATUS);

		if (reg_val & (RENDER_PWRGT | MEDIA_PWRGT | DISP2D_PWRGT))
			vlv_punit_write(dev_priv, PUNIT_REG_PWRGT_CTRL, 0x0);

		mutex_unlock(&dev_priv->rps.hw_lock);

	}
}

/*
 * Generally this is called implicitly by the register read function. However,
 * if some sequence requires the GT to not power down then this function should
 * be called at the beginning of the sequence followed by a call to
 * gen6_gt_force_wake_put() at the end of the sequence.
 */
void gen6_gt_force_wake_get(struct drm_i915_private *dev_priv)
{
	unsigned long irqflags;

	if (!dev_priv->uncore.funcs.force_wake_get)
		return;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	if (dev_priv->uncore.forcewake_count++ == 0)
		dev_priv->uncore.funcs.force_wake_get(dev_priv);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/*
 * see gen6_gt_force_wake_get()
 */
void gen6_gt_force_wake_put(struct drm_i915_private *dev_priv)
{
	unsigned long irqflags;

	if (!dev_priv->uncore.funcs.force_wake_put)
		return;

	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);
	if (--dev_priv->uncore.forcewake_count == 0) {
		dev_priv->uncore.forcewake_count++;
		mod_delayed_work(dev_priv->wq,
				 &dev_priv->uncore.force_wake_work,
				 1);
	}
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
}

/* We give fast paths for the really cool registers */
#define NEEDS_FORCE_WAKE(dev_priv, reg) \
	 ((reg) < 0x40000 && (reg) != FORCEWAKE)

static void
ilk_dummy_write(struct drm_i915_private *dev_priv)
{
	/* WaIssueDummyWriteToWakeupFromRC6:ilk Issue a dummy write to wake up
	 * the chip from rc6 before touching it for real. MI_MODE is masked,
	 * hence harmless to write 0 into. */
	__raw_i915_write32(dev_priv, MI_MODE, 0);
}

static void
hsw_unclaimed_reg_clear(struct drm_i915_private *dev_priv, u32 reg)
{
	if (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM) {
		DRM_ERROR("Unknown unclaimed register before writing to %x\n",
			  reg);
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}

static void
hsw_unclaimed_reg_check(struct drm_i915_private *dev_priv, u32 reg)
{
	if (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM) {
		DRM_ERROR("Unclaimed write to %x\n", reg);
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}

#define REG_READ_HEADER(x) \
	unsigned long irqflags; \
	u##x val = 0; \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define REG_READ_FOOTER \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
	trace_i915_reg_rw(false, reg, val, sizeof(val), trace); \
	return val

#define __gen4_read(x) \
static u##x \
gen4_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	val = __raw_i915_read##x(dev_priv, reg); \
	REG_READ_FOOTER; \
}

#define __gen5_read(x) \
static u##x \
gen5_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	ilk_dummy_write(dev_priv); \
	val = __raw_i915_read##x(dev_priv, reg); \
	REG_READ_FOOTER; \
}

#define __gen6_read(x) \
static u##x \
gen6_read##x(struct drm_i915_private *dev_priv, off_t reg, bool trace) { \
	REG_READ_HEADER(x); \
	if (NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		if (dev_priv->uncore.forcewake_count == 0) \
			dev_priv->uncore.funcs.force_wake_get(dev_priv); \
		val = __raw_i915_read##x(dev_priv, reg); \
		if (dev_priv->uncore.forcewake_count == 0) \
			dev_priv->uncore.funcs.force_wake_put(dev_priv); \
	} else { \
		val = __raw_i915_read##x(dev_priv, reg); \
	} \
	REG_READ_FOOTER; \
}

__gen6_read(8)
__gen6_read(16)
__gen6_read(32)
__gen6_read(64)
__gen5_read(8)
__gen5_read(16)
__gen5_read(32)
__gen5_read(64)
__gen4_read(8)
__gen4_read(16)
__gen4_read(32)
__gen4_read(64)

#undef __gen6_read
#undef __gen5_read
#undef __gen4_read
#undef REG_READ_FOOTER
#undef REG_READ_HEADER

#define REG_WRITE_HEADER \
	unsigned long irqflags; \
	trace_i915_reg_rw(true, reg, val, sizeof(val), trace); \
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags)

#define __gen4_write(x) \
static void \
gen4_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	REG_WRITE_HEADER; \
	__raw_i915_write##x(dev_priv, reg, val); \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
}

#define __gen5_write(x) \
static void \
gen5_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	REG_WRITE_HEADER; \
	ilk_dummy_write(dev_priv); \
	__raw_i915_write##x(dev_priv, reg, val); \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
}

#define __gen6_write(x) \
static void \
gen6_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	REG_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
}

#define __hsw_write(x) \
static void \
hsw_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	u32 __fifo_ret = 0; \
	REG_WRITE_HEADER; \
	if (NEEDS_FORCE_WAKE((dev_priv), (reg))) { \
		__fifo_ret = __gen6_gt_wait_for_fifo(dev_priv); \
	} \
	hsw_unclaimed_reg_clear(dev_priv, reg); \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (unlikely(__fifo_ret)) { \
		gen6_gt_check_fifodbg(dev_priv); \
	} \
	hsw_unclaimed_reg_check(dev_priv, reg); \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
}

static const u32 gen8_shadowed_regs[] = {
	FORCEWAKE_MT,
	GEN6_RPNSWREQ,
	GEN6_RC_VIDEO_FREQ,
	RING_TAIL(RENDER_RING_BASE),
	RING_TAIL(GEN6_BSD_RING_BASE),
	RING_TAIL(VEBOX_RING_BASE),
	RING_TAIL(BLT_RING_BASE),
	/* TODO: Other registers are not yet used */
};

static bool is_gen8_shadowed(struct drm_i915_private *dev_priv, u32 reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(gen8_shadowed_regs); i++)
		if (reg == gen8_shadowed_regs[i])
			return true;

	return false;
}

#define __gen8_write(x) \
static void \
gen8_write##x(struct drm_i915_private *dev_priv, off_t reg, u##x val, bool trace) { \
	bool __needs_put = !is_gen8_shadowed(dev_priv, reg); \
	REG_WRITE_HEADER; \
	if (__needs_put) { \
		dev_priv->uncore.funcs.force_wake_get(dev_priv); \
	} \
	__raw_i915_write##x(dev_priv, reg, val); \
	if (__needs_put) { \
		dev_priv->uncore.funcs.force_wake_put(dev_priv); \
	} \
	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags); \
}

__gen8_write(8)
__gen8_write(16)
__gen8_write(32)
__gen8_write(64)
__hsw_write(8)
__hsw_write(16)
__hsw_write(32)
__hsw_write(64)
__gen6_write(8)
__gen6_write(16)
__gen6_write(32)
__gen6_write(64)
__gen5_write(8)
__gen5_write(16)
__gen5_write(32)
__gen5_write(64)
__gen4_write(8)
__gen4_write(16)
__gen4_write(32)
__gen4_write(64)

#undef __gen8_write
#undef __hsw_write
#undef __gen6_write
#undef __gen5_write
#undef __gen4_write
#undef REG_WRITE_HEADER

void intel_uncore_init(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	INIT_DELAYED_WORK(&dev_priv->uncore.force_wake_work,
			  gen6_force_wake_work);

	if (IS_VALLEYVIEW(dev)) {
		dev_priv->uncore.funcs.force_wake_get = vlv_force_wake_get;
		dev_priv->uncore.funcs.force_wake_put = vlv_force_wake_put;
	} else if (IS_HASWELL(dev) || IS_GEN8(dev)) {
		dev_priv->uncore.funcs.force_wake_get = __gen6_gt_force_wake_mt_get;
		dev_priv->uncore.funcs.force_wake_put = __gen6_gt_force_wake_mt_put;
	} else if (IS_IVYBRIDGE(dev)) {
		u32 ecobus;

		/* IVB configs may use multi-threaded forcewake */

		/* A small trick here - if the bios hasn't configured
		 * MT forcewake, and if the device is in RC6, then
		 * force_wake_mt_get will not wake the device and the
		 * ECOBUS read will return zero. Which will be
		 * (correctly) interpreted by the test below as MT
		 * forcewake being disabled.
		 */
		mutex_lock(&dev->struct_mutex);
		__gen6_gt_force_wake_mt_get(dev_priv);
		ecobus = __raw_i915_read32(dev_priv, ECOBUS);
		__gen6_gt_force_wake_mt_put(dev_priv);
		mutex_unlock(&dev->struct_mutex);

		if (ecobus & FORCEWAKE_MT_ENABLE) {
			dev_priv->uncore.funcs.force_wake_get =
				__gen6_gt_force_wake_mt_get;
			dev_priv->uncore.funcs.force_wake_put =
				__gen6_gt_force_wake_mt_put;
		} else {
			DRM_INFO("No MT forcewake available on Ivybridge, this can result in issues\n");
			DRM_INFO("when using vblank-synced partial screen updates.\n");
			dev_priv->uncore.funcs.force_wake_get =
				__gen6_gt_force_wake_get;
			dev_priv->uncore.funcs.force_wake_put =
				__gen6_gt_force_wake_put;
		}
	} else if (IS_GEN6(dev)) {
		dev_priv->uncore.funcs.force_wake_get =
			__gen6_gt_force_wake_get;
		dev_priv->uncore.funcs.force_wake_put =
			__gen6_gt_force_wake_put;
	}

	switch (INTEL_INFO(dev)->gen) {
	default:
		dev_priv->uncore.funcs.mmio_writeb  = gen8_write8;
		dev_priv->uncore.funcs.mmio_writew  = gen8_write16;
		dev_priv->uncore.funcs.mmio_writel  = gen8_write32;
		dev_priv->uncore.funcs.mmio_writeq  = gen8_write64;
		dev_priv->uncore.funcs.mmio_readb  = gen6_read8;
		dev_priv->uncore.funcs.mmio_readw  = gen6_read16;
		dev_priv->uncore.funcs.mmio_readl  = gen6_read32;
		dev_priv->uncore.funcs.mmio_readq  = gen6_read64;
		break;
	case 7:
	case 6:
		if (IS_HASWELL(dev)) {
			dev_priv->uncore.funcs.mmio_writeb  = hsw_write8;
			dev_priv->uncore.funcs.mmio_writew  = hsw_write16;
			dev_priv->uncore.funcs.mmio_writel  = hsw_write32;
			dev_priv->uncore.funcs.mmio_writeq  = hsw_write64;
		} else {
			dev_priv->uncore.funcs.mmio_writeb  = gen6_write8;
			dev_priv->uncore.funcs.mmio_writew  = gen6_write16;
			dev_priv->uncore.funcs.mmio_writel  = gen6_write32;
			dev_priv->uncore.funcs.mmio_writeq  = gen6_write64;
		}
		dev_priv->uncore.funcs.mmio_readb  = gen6_read8;
		dev_priv->uncore.funcs.mmio_readw  = gen6_read16;
		dev_priv->uncore.funcs.mmio_readl  = gen6_read32;
		dev_priv->uncore.funcs.mmio_readq  = gen6_read64;
		break;
	case 5:
		dev_priv->uncore.funcs.mmio_writeb  = gen5_write8;
		dev_priv->uncore.funcs.mmio_writew  = gen5_write16;
		dev_priv->uncore.funcs.mmio_writel  = gen5_write32;
		dev_priv->uncore.funcs.mmio_writeq  = gen5_write64;
		dev_priv->uncore.funcs.mmio_readb  = gen5_read8;
		dev_priv->uncore.funcs.mmio_readw  = gen5_read16;
		dev_priv->uncore.funcs.mmio_readl  = gen5_read32;
		dev_priv->uncore.funcs.mmio_readq  = gen5_read64;
		break;
	case 4:
	case 3:
	case 2:
		dev_priv->uncore.funcs.mmio_writeb  = gen4_write8;
		dev_priv->uncore.funcs.mmio_writew  = gen4_write16;
		dev_priv->uncore.funcs.mmio_writel  = gen4_write32;
		dev_priv->uncore.funcs.mmio_writeq  = gen4_write64;
		dev_priv->uncore.funcs.mmio_readb  = gen4_read8;
		dev_priv->uncore.funcs.mmio_readw  = gen4_read16;
		dev_priv->uncore.funcs.mmio_readl  = gen4_read32;
		dev_priv->uncore.funcs.mmio_readq  = gen4_read64;
		break;
	}
}

void intel_uncore_fini(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	flush_delayed_work(&dev_priv->uncore.force_wake_work);

	/* Paranoia: make sure we have disabled everything before we exit. */
	intel_uncore_sanitize(dev);
}

static const struct register_whitelist {
	uint64_t offset;
	uint32_t size;
	uint32_t gen_bitmask; /* support gens, 0x10 for 4, 0x30 for 4 and 5, etc. */
} whitelist[] = {
	{ RING_TIMESTAMP(RENDER_RING_BASE), 8, 0xF0 },
};

int i915_reg_read_ioctl(struct drm_device *dev,
			void *data, struct drm_file *file)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_reg_read *reg = data;
	struct register_whitelist const *entry = whitelist;
	int i;

	for (i = 0; i < ARRAY_SIZE(whitelist); i++, entry++) {
		if (entry->offset == reg->offset &&
		    (1 << INTEL_INFO(dev)->gen & entry->gen_bitmask))
			break;
	}

	if (i == ARRAY_SIZE(whitelist))
		return -EINVAL;

	switch (entry->size) {
	case 8:
		reg->val = I915_READ64(reg->offset);
		break;
	case 4:
		reg->val = I915_READ(reg->offset);
		break;
	case 2:
		reg->val = I915_READ16(reg->offset);
		break;
	case 1:
		reg->val = I915_READ8(reg->offset);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

static int i965_reset_complete(struct drm_device *dev)
{
	u8 gdrst;
	pci_read_config_byte(dev->pdev, I965_GDRST, &gdrst);
	return (gdrst & GRDOM_RESET_ENABLE) == 0;
}

static int i965_do_reset(struct drm_device *dev)
{
	int ret;

	/*
	 * Set the domains we want to reset (GRDOM/bits 2 and 3) as
	 * well as the reset bit (GR/bit 0).  Setting the GR bit
	 * triggers the reset; when done, the hardware will clear it.
	 */
	pci_write_config_byte(dev->pdev, I965_GDRST,
			      GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret =  wait_for(i965_reset_complete(dev), 500);
	if (ret)
		return ret;

	/* We can't reset render&media without also resetting display ... */
	pci_write_config_byte(dev->pdev, I965_GDRST,
			      GRDOM_MEDIA | GRDOM_RESET_ENABLE);

	ret =  wait_for(i965_reset_complete(dev), 500);
	if (ret)
		return ret;

	pci_write_config_byte(dev->pdev, I965_GDRST, 0);

	return 0;
}

static int ironlake_do_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	u32 gdrst;
	int ret;

	gdrst = I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR);
	gdrst &= ~GRDOM_MASK;
	I915_WRITE(MCHBAR_MIRROR_BASE + ILK_GDSR,
		   gdrst | GRDOM_RENDER | GRDOM_RESET_ENABLE);
	ret = wait_for(I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR) & 0x1, 500);
	if (ret)
		return ret;

	/* We can't reset render&media without also resetting display ... */
	gdrst = I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR);
	gdrst &= ~GRDOM_MASK;
	I915_WRITE(MCHBAR_MIRROR_BASE + ILK_GDSR,
		   gdrst | GRDOM_MEDIA | GRDOM_RESET_ENABLE);
	return wait_for(I915_READ(MCHBAR_MIRROR_BASE + ILK_GDSR) & 0x1, 500);
}

static int gen6_do_reset(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int	ret;
	unsigned long irqflags;

	/* Hold uncore.lock across reset to prevent any register access
	 * with forcewake not set correctly
	 */
	spin_lock_irqsave(&dev_priv->uncore.lock, irqflags);

	/* Reset the chip */

	/* GEN6_GDRST is not in the gt power well, no need to check
	 * for fifo space for the write or forcewake the chip for
	 * the read
	 */
	__raw_i915_write32(dev_priv, GEN6_GDRST, GEN6_GRDOM_FULL);

	/* Spin waiting for the device to ack the reset request */
	ret = wait_for((__raw_i915_read32(dev_priv, GEN6_GDRST) & GEN6_GRDOM_FULL) == 0, 500);

	intel_uncore_forcewake_reset(dev);

	/* If reset with a user forcewake, try to restore, otherwise turn it off */
	if (dev_priv->uncore.forcewake_count)
		dev_priv->uncore.funcs.force_wake_get(dev_priv);
	else
		dev_priv->uncore.funcs.force_wake_put(dev_priv);

	/* Restore fifo count */
	dev_priv->uncore.fifo_count = __raw_i915_read32(dev_priv, GT_FIFO_FREE_ENTRIES);

	spin_unlock_irqrestore(&dev_priv->uncore.lock, irqflags);
	return ret;
}

int intel_gpu_reset(struct drm_device *dev)
{
	switch (INTEL_INFO(dev)->gen) {
	case 8:
	case 7:
	case 6: return gen6_do_reset(dev);
	case 5: return ironlake_do_reset(dev);
	case 4: return i965_do_reset(dev);
	default: return -ENODEV;
	}
}

void intel_uncore_clear_errors(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	/* XXX needs spinlock around caller's grouping */
	if (HAS_FPGA_DBG_UNCLAIMED(dev))
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
}

void intel_uncore_check_errors(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (HAS_FPGA_DBG_UNCLAIMED(dev) &&
	    (__raw_i915_read32(dev_priv, FPGA_DBG) & FPGA_DBG_RM_NOCLAIM)) {
		DRM_ERROR("Unclaimed register before interrupt\n");
		__raw_i915_write32(dev_priv, FPGA_DBG, FPGA_DBG_RM_NOCLAIM);
	}
}
