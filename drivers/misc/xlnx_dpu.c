// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Xilinx Vivado Flow Deep learning Processing Unit (DPU) Driver
 *
 * Copyright (C) 2022 Xilinx, Inc. All rights reserved.
 *
 * Authors:
 *    Ye Yang <ye.yang@xilinx.com>
 *
 * This file is dual-licensed; you may select either the GNU General Public
 * License version 2 or Apache License, Version 2.0.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/iopoll.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/nospec.h>
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif
#include "xlnx_dpu.h"

#define DEVICE_NAME "dpu"
#define DRV_NAME "xlnx-dpu"
#define DRIVER_DESC "Xilinx Deep Learning Processing Unit driver"

static int timeout = 5;
module_param(timeout, int, 0644);
MODULE_PARM_DESC(timeout, "Set DPU timeout val in secs (default 5s)");

static bool force_poll;
/*
 * this parameter is intended to be used only at probe time as there is
 * no way to disable interrupts from DPU at run time.
 */
module_param(force_poll, bool, 0444);
MODULE_PARM_DESC(force_poll, "polling or interrupt mode (default interrupt)");

/**
 * struct cu - Computer Unit (cu) structure
 * @mutex: protects from simultaneous access
 * @done: completion of cu
 * @irq: indicates cu IRQ number
 */
struct cu {
	struct mutex	mutex; /* protects from simultaneous accesses */
	struct completion	done;
	int	irq;
};

/**
 * struct xdpu_dev - Driver data for DPU
 * @dev: pointer to device struct
 * @regs: virtual base address for the dpu regmap
 * @head: indicates dma memory pool list
 * @cu: indicates computer unit struct
 * @axi_clk: AXI Lite clock
 * @dpu_clk: DPU clock used for DPUCZDX8G general logic
 * @dsp_clk: DSP clock used for DSP blocks
 * @miscdev: misc device handle
 * @root: debugfs dentry
 * @dpu_cnt: indicates how many dpu core/cu enabled in IP, up to 4
 * @sfm_cnt: indicates softmax core enabled or not
 */
struct xdpu_dev {
	struct device	*dev;
	void __iomem	*regs;
	struct list_head	head;
	struct cu	cu[MAX_CU_NUM];
	struct clk	*axi_clk;
	struct clk	*dpu_clk;
	struct clk	*dsp_clk;
	struct miscdevice	miscdev;
#ifdef CONFIG_DEBUG_FS
	struct dentry	*root;
#endif
	u8	dpu_cnt;
	u8	sfm_cnt;
};

/**
 * struct dpu_buffer_block - DPU buffer block
 * @head: list head
 * @vaddr: virtual address of the blocks memory
 * @dma_addr: dma address of the blocks memory
 * @capacity: total size of the block in bytes
 */
struct dpu_buffer_block {
	struct list_head	head;
	void	*vaddr;
	dma_addr_t	dma_addr;
	size_t	capacity;
};

#ifdef CONFIG_DEBUG_FS
static int dpu_debugfs_init(struct xdpu_dev *xdpu);
#endif

/**
 * xlnx_dpu_regs_init - initialize dpu register
 * @xdpu:	dpu structure
 */
static void xlnx_dpu_regs_init(struct xdpu_dev *xdpu)
{
	int cu;

	iowrite32(0, xdpu->regs + DPU_PMU_IP_RST);

	for (cu = 0; cu < xdpu->dpu_cnt; cu++) {
		iowrite32(DPU_HPBUS_VAL, xdpu->regs + DPU_HPBUS(cu));
		iowrite32(0, xdpu->regs + DPU_IPSTART(cu));
	}

	iowrite32(DPU_RST_ALL_CORES, xdpu->regs + DPU_PMU_IP_RST);
	iowrite32(0, xdpu->regs + DPU_SFM_RESET);
	iowrite32(1, xdpu->regs + DPU_SFM_RESET);
}

/**
 * xlnx_dpu_dump_regs - dump all dpu registers
 * @p:	dpu structure
 */
static void xlnx_dpu_dump_regs(struct xdpu_dev *p)
{
	struct device *dev = p->dev;
	int i;

#define FMT8	"%-27s %08x\n"
#define FMT16	"%-27s %016llx\n"
	dev_warn(dev, "------------[ cut here ]------------\n");
	dev_warn(dev, "Dump DPU Registers:\n");
	dev_info(dev, FMT16, "TARGET_ID",
		 lo_hi_readq(p->regs + DPU_TARGETID_L));
	dev_info(dev, FMT8, "PMU_RST", ioread32(p->regs + DPU_PMU_IP_RST));
	dev_info(dev, FMT8, "IP_VER_INFO", ioread32(p->regs + DPU_IPVER_INFO));
	dev_info(dev, FMT8, "IP_FREQENCY", ioread32(p->regs + DPU_IPFREQENCY));
	dev_info(dev, FMT8, "INT_STS", ioread32(p->regs + DPU_INT_STS));
	dev_info(dev, FMT8, "INT_MSK", ioread32(p->regs + DPU_INT_MSK));
	dev_info(dev, FMT8, "INT_RAW", ioread32(p->regs + DPU_INT_RAW));
	dev_info(dev, FMT8, "INT_ICR", ioread32(p->regs + DPU_INT_ICR));
	for (i = 0; i < p->dpu_cnt; i++) {
		dev_warn(dev, "[CU-%d]\n", i);
		dev_info(dev, FMT8, "HPBUS", ioread32(p->regs + DPU_HPBUS(i)));
		dev_info(dev, FMT8, "INSTR",
			 ioread32(p->regs + DPU_INSADDR(i)));
		dev_info(dev, FMT8, "START",
			 ioread32(p->regs + DPU_IPSTART(i)));
		dev_info(dev, FMT16, "ADDR0",
			 lo_hi_readq(p->regs + DPU_ADDR0_L(i)));
		dev_info(dev, FMT16, "ADDR1",
			 lo_hi_readq(p->regs + DPU_ADDR1_L(i)));
		dev_info(dev, FMT16, "ADDR2",
			 lo_hi_readq(p->regs + DPU_ADDR2_L(i)));
		dev_info(dev, FMT16, "ADDR3",
			 lo_hi_readq(p->regs + DPU_ADDR3_L(i)));
		dev_info(dev, FMT16, "ADDR4",
			 lo_hi_readq(p->regs + DPU_ADDR4_L(i)));
		dev_info(dev, FMT16, "ADDR5",
			 lo_hi_readq(p->regs + DPU_ADDR5_L(i)));
		dev_info(dev, FMT16, "ADDR6",
			 lo_hi_readq(p->regs + DPU_ADDR6_L(i)));
		dev_info(dev, FMT16, "ADDR7",
			 lo_hi_readq(p->regs + DPU_ADDR7_L(i)));
		dev_info(dev, FMT8, "PSTART",
			 ioread32(p->regs + DPU_P_STA_C(i)));
		dev_info(dev, FMT8, "PEND",
			 ioread32(p->regs + DPU_P_END_C(i)));
		dev_info(dev, FMT8, "CSTART",
			 ioread32(p->regs + DPU_C_STA_C(i)));
		dev_info(dev, FMT8, "CEND",
			 ioread32(p->regs + DPU_C_END_C(i)));
		dev_info(dev, FMT8, "SSTART",
			 ioread32(p->regs + DPU_S_STA_C(i)));
		dev_info(dev, FMT8, "SEND",
			 ioread32(p->regs + DPU_S_END_C(i)));
		dev_info(dev, FMT8, "LSTART",
			 ioread32(p->regs + DPU_L_STA_C(i)));
		dev_info(dev, FMT8, "LEND",
			 ioread32(p->regs + DPU_L_END_C(i)));
		dev_info(dev, FMT16, "CYCLE",
			 lo_hi_readq(p->regs + DPU_CYCLE_L(i)));
		dev_info(dev, FMT8, "AXI", ioread32(p->regs + DPU_AXI_STS(i)));
	}
	dev_warn(dev, "[SOFTMAX]\n");
	if (p->sfm_cnt) {
#define DUMPREG(r) \
	dev_info(dev, FMT8, #r, ioread32(p->regs + DPU_SFM_##r))
		DUMPREG(INT_DONE);
		DUMPREG(CMD_XLEN);
		DUMPREG(CMD_YLEN);
		DUMPREG(SRC_ADDR);
		DUMPREG(DST_ADDR);
		DUMPREG(CMD_SCAL);
		DUMPREG(CMD_OFF);
		DUMPREG(INT_CLR);
		DUMPREG(START);
		DUMPREG(RESET);
#undef DUMPREG
	}
	dev_warn(dev, "------------[ cut here ]------------\n");
}

/**
 * xlnx_dpu_int_clear - clean DPU interrupt
 * @xdpu:	dpu structure
 * @id:	indicates which cu needs to be clean interrupt
 */
static void xlnx_dpu_int_clear(struct xdpu_dev *xdpu, int id)
{
	iowrite32(BIT(id), xdpu->regs + DPU_INT_ICR);
	iowrite32(0, xdpu->regs + DPU_IPSTART(id));

	/* make sure have enough time to receive the INT level */
	udelay(1);

	iowrite32(ioread32(xdpu->regs + DPU_INT_ICR) & ~BIT(id),
		  xdpu->regs + DPU_INT_ICR);
}

/**
 * xlnx_sfm_int_clear - clean softmax interrupt
 * @xdpu:	dpu structure
 */
static void xlnx_sfm_int_clear(struct xdpu_dev *xdpu)
{
	iowrite32(1, xdpu->regs + DPU_SFM_INT_CLR);
	iowrite32(0, xdpu->regs + DPU_SFM_INT_CLR);
}

/**
 * xlnx_dpu_softmax - softmax calculation acceleration using softmax IP
 * @xdpu:	dpu structure
 * @p :	softmax pmeter structure
 *
 * Return:	0 if successful; otherwise -errno
 */
static int xlnx_dpu_softmax(struct xdpu_dev *xdpu, struct ioc_softmax_t *p)
{
	int ret = -ETIMEDOUT;
	int val;

	iowrite32(p->width, xdpu->regs + DPU_SFM_CMD_XLEN);
	iowrite32(p->height, xdpu->regs + DPU_SFM_CMD_YLEN);

	/* ip limition - softmax supports up to 32-bit addressing */
	iowrite32(p->input, xdpu->regs + DPU_SFM_SRC_ADDR);
	iowrite32(p->output, xdpu->regs + DPU_SFM_DST_ADDR);
	iowrite32(p->scale, xdpu->regs + DPU_SFM_CMD_SCAL);
	iowrite32(p->offset, xdpu->regs + DPU_SFM_CMD_OFF);
	iowrite32(1, xdpu->regs + DPU_SFM_RESET);
	iowrite32(0, xdpu->regs + DPU_SFM_MODE);

	iowrite32(1, xdpu->regs + DPU_SFM_START);
	iowrite32(0, xdpu->regs + DPU_SFM_START);

	if (!force_poll) {
		if (!wait_for_completion_timeout(&xdpu->cu[xdpu->dpu_cnt].done,
						 TIMEOUT))
			goto err_out;
	} else {
		ret = readx_poll_timeout(ioread32,
					 xdpu->regs + DPU_SFM_INT_DONE,
					 val,
					 val & 0x1,
					 POLL_PERIOD_US,
					 TIMEOUT_US);
		if (ret < 0)
			goto err_out;

		xlnx_sfm_int_clear(xdpu);
	}

	dev_dbg(xdpu->dev, "%s: PID=%d CPU=%d\n",
		__func__, current->pid, raw_smp_processor_id());

	return 0;

err_out:
	dev_warn(xdpu->dev, "timeout waiting for softmax\n");
	xlnx_dpu_dump_regs(xdpu);

	return ret;
}

/**
 * xlnx_dpu_run - run dpu
 * @xdpu:	dpu structure
 * @p:	dpu run struct, contains the necessary address info
 * @id:	indicates which cu is running
 *
 * Return:	0 if successful; otherwise -errno
 */
static int xlnx_dpu_run(struct xdpu_dev *xdpu, struct ioc_kernel_run_t *p,
			int id)
{
	int val, ret;

	iowrite32(p->addr_code >> DPU_INSTR_OFFSET,
		  xdpu->regs + DPU_INSADDR(id));

	/*
	 * Addr0: bias/weights
	 * Addr1: the inter-layer workspacce
	 * Addr2: the 1st input layer
	 * Addr3: the output layer
	 * AddrX: ULLONG_MAX as default
	 */
	lo_hi_writeq(p->addr0, xdpu->regs + DPU_ADDR0_L(id));
	lo_hi_writeq(p->addr1, xdpu->regs + DPU_ADDR1_L(id));
	lo_hi_writeq(p->addr2, xdpu->regs + DPU_ADDR2_L(id));
	lo_hi_writeq(p->addr3, xdpu->regs + DPU_ADDR3_L(id));

	if (p->addr4 != ULLONG_MAX)
		lo_hi_writeq(p->addr4, xdpu->regs + DPU_ADDR4_L(id));
	if (p->addr5 != ULLONG_MAX)
		lo_hi_writeq(p->addr5, xdpu->regs + DPU_ADDR5_L(id));
	if (p->addr6 != ULLONG_MAX)
		lo_hi_writeq(p->addr6, xdpu->regs + DPU_ADDR6_L(id));
	if (p->addr7 != ULLONG_MAX)
		lo_hi_writeq(p->addr7, xdpu->regs + DPU_ADDR7_L(id));

	iowrite32(1, xdpu->regs + DPU_IPSTART(id));

	p->time_start = ktime_get();

	if (!force_poll) {
		if (!wait_for_completion_timeout(&xdpu->cu[id].done,
						 TIMEOUT))
			goto err_out;
	} else {
		ret = readx_poll_timeout(ioread32,
					 xdpu->regs + DPU_INT_RAW,
					 val,
					 val & BIT(id),
					 POLL_PERIOD_US,
					 TIMEOUT_US);
		if (ret < 0)
			goto err_out;

		xlnx_dpu_int_clear(xdpu, id);
	}

	p->time_end = ktime_get();
	p->core_id = id;
	p->pend_cnt = ioread32(xdpu->regs + DPU_P_END_C(id));
	p->cend_cnt = ioread32(xdpu->regs + DPU_C_END_C(id));
	p->send_cnt = ioread32(xdpu->regs + DPU_S_END_C(id));
	p->lend_cnt = ioread32(xdpu->regs + DPU_L_END_C(id));
	p->pstart_cnt = ioread32(xdpu->regs + DPU_P_STA_C(id));
	p->cstart_cnt = ioread32(xdpu->regs + DPU_C_STA_C(id));
	p->sstart_cnt = ioread32(xdpu->regs + DPU_S_STA_C(id));
	p->lstart_cnt = ioread32(xdpu->regs + DPU_L_STA_C(id));
	p->counter = lo_hi_readq(xdpu->regs + DPU_CYCLE_L(id));

	dev_dbg(xdpu->dev,
		"%s: PID=%d DPU=%d CPU=%d TIME=%lldms complete!\n",
		__func__, current->pid, id, raw_smp_processor_id(),
		ktime_ms_delta(p->time_end, p->time_start));

	return 0;

err_out:
	dev_warn(xdpu->dev, "cu[%d] timeout", id);
	xlnx_dpu_dump_regs(xdpu);

	return -ETIMEDOUT;
}

/**
 * xlnx_dpu_alloc_bo - alloc contiguous physical memory for dpu
 * @xdpu:	dpu structure
 * @req:	dpcma_req_alloc struct, contains the request info
 *
 * Return:	0 if successful; otherwise -errno
 */
static long xlnx_dpu_alloc_bo(struct xdpu_dev *xdpu,
			      struct dpcma_req_alloc __user *req)
{
	struct dpu_buffer_block *pb;
	size_t size;

	pb = kzalloc(sizeof(*pb), GFP_KERNEL);
	if (!pb)
		return -ENOMEM;

	if (get_user(size, &req->size))
		goto err_pb;

	if (size > SIZE_MAX - PAGE_SIZE)
		goto err_pb;

	pb->capacity = PAGE_ALIGN(size);

	if (put_user(pb->capacity, &req->capacity))
		goto err_pb;

	pb->vaddr = dma_alloc_coherent(xdpu->dev, pb->capacity, &pb->dma_addr,
				       GFP_KERNEL);
	if (!pb->vaddr)
		goto err_pb;

	if (put_user(pb->dma_addr, &req->phy_addr))
		goto err_out;

	list_add(&pb->head, &xdpu->head);

	return 0;

err_out:
	dma_free_coherent(xdpu->dev, pb->capacity, pb->vaddr, pb->dma_addr);
err_pb:
	kfree(pb);
	return -EFAULT;
}

/**
 * xlnx_dpu_free_bo - free contiguous physical memory allocated
 * @xdpu:	dpu structure
 * @req:	dpcma_req_free struct, contains the request info
 *
 * Return:	0 if successful; otherwise -errno
 */
static long xlnx_dpu_free_bo(struct xdpu_dev *xdpu,
			     struct dpcma_req_free __user *req)
{
	struct list_head *pos = NULL, *next = NULL;
	u64 phy_addr = 0;
	struct dpu_buffer_block *h;

	if (get_user(phy_addr, &req->phy_addr))
		return -EFAULT;

	list_for_each_safe(pos, next, &xdpu->head) {
		h = list_entry(pos, struct dpu_buffer_block, head);
		if (phy_addr == h->dma_addr) {
			dma_free_coherent(xdpu->dev, h->capacity, h->vaddr,
					  h->dma_addr);
			list_del(pos);
			kfree(h);
		}
	}

	return 0;
}

/**
 * xlnx_dpu_sync_bo - flush/invalidate cache for allocated memory
 * @xdpu:	dpu structure
 * @req:	dpcma_req_sync struct, contains the request info
 *
 * Return:	0 if successful; otherwise -errno
 */
static long xlnx_dpu_sync_bo(struct xdpu_dev *xdpu,
			     struct dpcma_req_sync __user *req)
{
	struct list_head *pos = NULL;
	long phy_addr;
	int dir;
	size_t size, offset;
	struct dpu_buffer_block *h;

	if (get_user(phy_addr, &req->phy_addr) ||
	    get_user(size, &req->size) || get_user(dir, &req->direction))
		return -EFAULT;

	if (dir != DPU_TO_CPU && dir != CPU_TO_DPU) {
		dev_err(xdpu->dev, "invalid direction. direction = %d\n", dir);
		return -EINVAL;
	}

	list_for_each(pos, &xdpu->head) {
		h = list_entry(pos, struct dpu_buffer_block, head);
		if (phy_addr >= h->dma_addr &&
		    phy_addr < h->dma_addr + h->capacity) {
			offset = phy_addr;
			if (dir == DPU_TO_CPU)
				dma_sync_single_for_cpu(xdpu->dev,
							offset,
							size,
							DMA_FROM_DEVICE);
			else
				dma_sync_single_for_device(xdpu->dev,
							   offset,
							   size,
							   DMA_TO_DEVICE);
		}
	}
	return 0;
}

/**
 * xlnx_dpu_ioctl - control ioctls for the DPU
 * @file:	file handle of the DPU device
 * @cmd:	ioctl code
 * @arg:	pointer to user passed structure
 *
 * Return:	0 if successful; otherwise -errno
 */
static long xlnx_dpu_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	int ret = 0;
	struct xdpu_dev *xdpu;
	void __user *data = NULL;

	xdpu = container_of(file->private_data, struct xdpu_dev, miscdev);

	if (_IOC_TYPE(cmd) != DPU_IOC_MAGIC)
		return -ENOTTY;

	/* check if ioctl argument is present and valid */
	if (_IOC_DIR(cmd) != _IOC_NONE) {
		data = (void __user *)arg;
		if (!data)
			return -EINVAL;
	}

	switch (cmd) {
	case DPUIOC_RUN:
	{
		struct ioc_kernel_run_t t;
		int id;

		if (copy_from_user(&t, data,
				   sizeof(struct ioc_kernel_run_t))) {
			return -EINVAL;
		}

		id = t.core_id;
		if (id >= xdpu->dpu_cnt)
			return -EINVAL;

		dev_dbg(xdpu->dev,
			"%s: PID=%d DPU=%d CPU=%d Comm=%.20s waiting",
			__func__, current->pid, id, raw_smp_processor_id(),
			current->comm);

		id = array_index_nospec(id, xdpu->dpu_cnt);
		/* Allows one process to run the cu by using a mutex */
		mutex_lock(&xdpu->cu[id].mutex);

		ret = xlnx_dpu_run(xdpu, &t, id);

		mutex_unlock(&xdpu->cu[id].mutex);

		if (copy_to_user(data, &t, sizeof(struct ioc_kernel_run_t)))
			return -EINVAL;

		break;
	}
	case DPUIOC_CREATE_BO:
		return xlnx_dpu_alloc_bo(xdpu,
					 (struct dpcma_req_alloc __user *)arg);
	case DPUIOC_FREE_BO:
		return xlnx_dpu_free_bo(xdpu,
					(struct dpcma_req_free __user *)arg);
	case DPUIOC_SYNC_BO:
		return xlnx_dpu_sync_bo(xdpu,
					(struct dpcma_req_sync __user *)arg);
	case DPUIOC_G_INFO:
	{
		u32 dpu_info = ioread32(xdpu->regs + DPU_IPVER_INFO);

		if (copy_to_user(data, &dpu_info, sizeof(dpu_info)))
			return -EFAULT;
		break;
	}
	case DPUIOC_G_TGTID:
	{
		u64 fingerprint = lo_hi_readq(xdpu->regs + DPU_TARGETID_L);

		if (copy_to_user(data, &fingerprint, sizeof(fingerprint)))
			return -EFAULT;
		break;
	}
	case DPUIOC_RUN_SOFTMAX:
	{
		struct ioc_softmax_t t;

		if (copy_from_user(&t, data,
				   sizeof(struct ioc_softmax_t))) {
			dev_err(xdpu->dev, "copy_from_user softmax_t fail\n");
			return -EINVAL;
		}

		mutex_lock(&xdpu->cu[xdpu->dpu_cnt].mutex);

		ret = xlnx_dpu_softmax(xdpu, &t);

		mutex_unlock(&xdpu->cu[xdpu->dpu_cnt].mutex);

		break;
	}
	case DPUIOC_REG_READ:
	{
		u32 val = 0;
		u32 off = 0;

		if (copy_from_user(&off, data, sizeof(off))) {
			dev_err(xdpu->dev, "copy_from_user off failed\n");
			return -EINVAL;
		}

		val = ioread32(xdpu->regs + off);

		if (copy_to_user(data, &val, sizeof(val)))
			return -EFAULT;
		break;
	}
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

/**
 * xlnx_dpu_isr - interrupt handler for DPU.
 * @irq:	Interrupt number.
 * @data:	DPU device structure.
 *
 * Return: IRQ_HANDLED.
 */
static irqreturn_t xlnx_dpu_isr(int irq, void *data)
{
	struct xdpu_dev *xdpu = data;
	int i;

	for (i = 0; i < xdpu->dpu_cnt; i++) {
		if (irq == xdpu->cu[i].irq) {
			xlnx_dpu_int_clear(xdpu, i);
			dev_dbg(xdpu->dev, "%s: DPU=%d IRQ=%d",
				__func__, i, irq);
			complete(&xdpu->cu[i].done);
		}
	}

	if (irq == xdpu->cu[xdpu->dpu_cnt].irq) {
		xlnx_sfm_int_clear(xdpu);
		dev_dbg(xdpu->dev, "%s: softmax IRQ=%d", __func__, irq);
		complete(&xdpu->cu[xdpu->dpu_cnt].done);
	}

	return IRQ_HANDLED;
}

/**
 * xlnx_dpu_mmap - maps cma ranges into userspace
 * @file:	file structure for the device
 * @vma:	VMA to map the registers into
 *
 * Return:	0 if successful; otherwise -errno
 */
static int xlnx_dpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;
	phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

	if ((offset >> PAGE_SHIFT) != vma->vm_pgoff)
		return -EINVAL;

	if ((offset + (phys_addr_t)size - 1) < offset)
		return -EINVAL;

	if (!((vma->vm_pgoff + size) <= __pa(high_memory)))
		return -EINVAL;

	if (remap_pfn_range(vma,
			    vma->vm_start,
			    vma->vm_pgoff,
			    size,
			    vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}

static const struct file_operations dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = xlnx_dpu_ioctl,
	.mmap = xlnx_dpu_mmap,
};

/**
 * get_irq - get irq
 * @pdev:	dpu platform device
 * @xdpu:	dpu structure
 *
 * Return:	0 if successful; otherwise -errno
 */
static int get_irq(struct platform_device *pdev, struct xdpu_dev *xdpu)
{
	int ret, i;
	int sfm_no = xdpu->dpu_cnt;
	struct device *dev = xdpu->dev;

	if (force_poll) {
		dev_warn(dev, "no IRQ, using polling mode\n");
		return 0;
	}

	for (i = 0; i < xdpu->dpu_cnt; i++) {
		xdpu->cu[i].irq = platform_get_irq(pdev, i);
		if (xdpu->cu[i].irq <= 0)
			return -EINVAL;

		ret = devm_request_irq(dev,
				       xdpu->cu[i].irq,
				       xlnx_dpu_isr,
				       0,
				       devm_kasprintf(dev,
						      GFP_KERNEL,
						      "%s-cu[%d]",
						      dev_name(dev),
						      i),
				       xdpu);
		if (ret < 0)
			return ret;
	}

	if (xdpu->sfm_cnt) {
		xdpu->cu[sfm_no].irq = platform_get_irq(pdev, sfm_no);
		if (xdpu->cu[sfm_no].irq <= 0)
			return -EINVAL;

		ret = devm_request_irq(dev,
				       xdpu->cu[sfm_no].irq,
				       xlnx_dpu_isr,
				       0,
				       devm_kasprintf(dev,
						      GFP_KERNEL,
						      "%s-softmax",
						      dev_name(dev)),
				       xdpu);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * xlnx_dpu_probe - probe dpu device
 * @pdev: Pointer to dpu platform device structure
 *
 * Return: 0 if successful; otherwise -errno
 */
static int xlnx_dpu_probe(struct platform_device *pdev)
{
	int i, ret;
	u32 val;
	struct xdpu_dev *xdpu;
	struct resource *res;
	struct device *dev = &pdev->dev;

	xdpu = devm_kzalloc(dev, sizeof(*xdpu), GFP_KERNEL);
	if (!xdpu)
		return -ENOMEM;

	xdpu->dev = dev;

	xdpu->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(xdpu->regs))
		return -ENOMEM;

	/*
	 * DTG doesn't generate clock nodes in DT for Microblaze(MB),
	 * using devm_clk_get_optional to return NULL in MB case, and return
	 * required clock in ZynqMP case.
	 */
	xdpu->axi_clk = devm_clk_get_optional(xdpu->dev, "s_axi_aclk");
	if (IS_ERR(xdpu->axi_clk))
		return dev_err_probe(xdpu->dev, PTR_ERR(xdpu->axi_clk),
				     "unable to get axi reference clock\n");

	ret = clk_prepare_enable(xdpu->axi_clk);
	if (ret) {
		dev_err(xdpu->dev, "failed to enable s_axi_aclk(%d)\n", ret);
		return ret;
	}

	xdpu->dpu_clk = devm_clk_get_optional(xdpu->dev, "m_axi_dpu_aclk");
	if (IS_ERR(xdpu->dpu_clk))
		return dev_err_probe(xdpu->dev, PTR_ERR(xdpu->dpu_clk),
				     "unable to get m_axi_dpu_aclk\n");

	ret = clk_prepare_enable(xdpu->dpu_clk);
	if (ret) {
		dev_err(xdpu->dev, "unable to enable dpu_clk(%d)\n", ret);
		goto err_dpuclk;
	}

	xdpu->dsp_clk = devm_clk_get_optional(xdpu->dev, "dpu_2x_clk");
	if (IS_ERR(xdpu->dsp_clk))
		return dev_err_probe(xdpu->dev, PTR_ERR(xdpu->dsp_clk),
				     "unable to get dsp clock\n");

	ret = clk_prepare_enable(xdpu->dsp_clk);
	if (ret) {
		dev_err(xdpu->dev, "unable to enable dpu_2x_clk(%d)\n", ret);
		goto err_dspclk;
	}

	/* dsp_clk should be dpu_clk * 2 */
	if (xdpu->axi_clk && xdpu->dpu_clk && xdpu->dsp_clk)
		dev_dbg(xdpu->dev,
			"Freq: axilite: %lu MHz, dpu: %lu MHz, dsp: %lu MHz",
			clk_get_rate(xdpu->axi_clk) / 1000000,
			clk_get_rate(xdpu->dpu_clk) / 1000000,
			clk_get_rate(xdpu->dsp_clk) / 1000000);

	val = ioread32(xdpu->regs + DPU_IPVER_INFO);
	if (DPU_VER(val) < DPU_IP_V3_4) {
		dev_err(dev, "DPU IP need upgrade to 3.4 or later");
		goto err_out;
	}

	xdpu->dpu_cnt = DPU_NUM(val);
	xdpu->sfm_cnt = SFM_NUM(val);

	val = ioread32(xdpu->regs + DPU_IPFREQENCY);
	dev_dbg(dev, "found %d dpu core @%ldMHz and %d softmax core",
		xdpu->dpu_cnt, DPU_FREQ(val), xdpu->sfm_cnt);

	if (get_irq(pdev, xdpu))
		goto err_out;

	/* Try the reserved memory. Proceed if there's none */
	ret = of_reserved_mem_device_init(dev);
	if (ret && ret != -ENODEV)
		goto err_out;

	/* Vivado flow DPU ip is capable of 40-bit physical addresses only */
	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(40))) {
		/* fall back to 32-bit DMA mask */
		if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
			goto err_out;
	}

	for (i = 0; i < xdpu->dpu_cnt + xdpu->sfm_cnt; i++) {
		init_completion(&xdpu->cu[i].done);
		mutex_init(&xdpu->cu[i].mutex);
	}

	INIT_LIST_HEAD(&xdpu->head);

	xdpu->miscdev.minor = MISC_DYNAMIC_MINOR;
	xdpu->miscdev.name = DEVICE_NAME;
	xdpu->miscdev.fops = &dev_fops;
	xdpu->miscdev.parent = dev;

	if (misc_register(&xdpu->miscdev))
		goto err_out;

	xlnx_dpu_regs_init(xdpu);

#ifdef CONFIG_DEBUG_FS
	ret = dpu_debugfs_init(xdpu);
	if (ret) {
		dev_err(xdpu->dev, "failed to init dpu_debugfs)\n");
		goto err_debugfs;
	}
#endif

	platform_set_drvdata(pdev, xdpu);

	dev_dbg(dev, "dpu registered as /dev/dpu successfully");

	return 0;

#ifdef CONFIG_DEBUG_FS
err_debugfs:
	misc_deregister(&xdpu->miscdev);
#endif
err_out:
	clk_disable_unprepare(xdpu->dsp_clk);
err_dspclk:
	clk_disable_unprepare(xdpu->dpu_clk);
err_dpuclk:
	clk_disable_unprepare(xdpu->axi_clk);

	return -EINVAL;
}

/**
 * xlnx_dpu_remove - clean up structures
 * @pdev:	The structure containing the device's details
 *
 * Return: 0 on success. -EINVAL for invalid value.
 */
static int xlnx_dpu_remove(struct platform_device *pdev)
{
	struct xdpu_dev *xdpu = platform_get_drvdata(pdev);
	int i;

	/* clean all regs */
	for (i = 0; i < DPU_REG_END; i += 4)
		iowrite32(0, xdpu->regs + i);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(xdpu->root);
#endif
	platform_set_drvdata(pdev, NULL);
	misc_deregister(&xdpu->miscdev);

	dev_dbg(xdpu->dev, "%s: device /dev/dpu unregistered\n", __func__);
	return 0;
}

static const struct of_device_id dpu_of_match[] = {
	{ .compatible = "xlnx,dpuczdx8g-3.4" },
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, dpu_of_match);

static struct platform_driver xlnx_dpu_drv = {
	.probe = xlnx_dpu_probe,
	.remove = xlnx_dpu_remove,

	.driver = {
		.name = DRV_NAME,
		.of_match_table = dpu_of_match,
	},
};

module_platform_driver(xlnx_dpu_drv);

#ifdef CONFIG_DEBUG_FS

#define dump_register(n)			\
{						\
	.name	= #n,				\
	.offset	= DPU_##n,				\
}

static const struct debugfs_reg32 cu_regs[4][38] = {
	{
	dump_register(IPVER_INFO),
	dump_register(IPFREQENCY),
	dump_register(TARGETID_L),
	dump_register(TARGETID_H),
	dump_register(IPSTART(0)),
	dump_register(INSADDR(0)),
	dump_register(ADDR0_L(0)),
	dump_register(ADDR0_H(0)),
	dump_register(ADDR1_L(0)),
	dump_register(ADDR1_H(0)),
	dump_register(ADDR2_L(0)),
	dump_register(ADDR2_H(0)),
	dump_register(ADDR3_L(0)),
	dump_register(ADDR3_H(0)),
	dump_register(ADDR4_L(0)),
	dump_register(ADDR4_H(0)),
	dump_register(ADDR5_L(0)),
	dump_register(ADDR5_H(0)),
	dump_register(ADDR6_L(0)),
	dump_register(ADDR6_H(0)),
	dump_register(ADDR7_L(0)),
	dump_register(ADDR7_H(0)),
	dump_register(CYCLE_L(0)),
	dump_register(CYCLE_H(0)),
	dump_register(P_STA_C(0)),
	dump_register(P_END_C(0)),
	dump_register(C_STA_C(0)),
	dump_register(C_END_C(0)),
	dump_register(S_STA_C(0)),
	dump_register(S_END_C(0)),
	dump_register(L_STA_C(0)),
	dump_register(L_END_C(0)),
	dump_register(AXI_STS(0)),
	dump_register(HPBUS(0)),
	dump_register(INT_STS),
	dump_register(INT_MSK),
	dump_register(INT_RAW),
	dump_register(INT_ICR),
	},
	{
	dump_register(IPVER_INFO),
	dump_register(IPFREQENCY),
	dump_register(TARGETID_L),
	dump_register(TARGETID_H),
	dump_register(IPSTART(1)),
	dump_register(INSADDR(1)),
	dump_register(ADDR0_L(1)),
	dump_register(ADDR0_H(1)),
	dump_register(ADDR1_L(1)),
	dump_register(ADDR1_H(1)),
	dump_register(ADDR2_L(1)),
	dump_register(ADDR2_H(1)),
	dump_register(ADDR3_L(1)),
	dump_register(ADDR3_H(1)),
	dump_register(ADDR4_L(1)),
	dump_register(ADDR4_H(1)),
	dump_register(ADDR5_L(1)),
	dump_register(ADDR5_H(1)),
	dump_register(ADDR6_L(1)),
	dump_register(ADDR6_H(1)),
	dump_register(ADDR7_L(1)),
	dump_register(ADDR7_H(1)),
	dump_register(CYCLE_L(1)),
	dump_register(CYCLE_H(1)),
	dump_register(P_STA_C(1)),
	dump_register(P_END_C(1)),
	dump_register(C_STA_C(1)),
	dump_register(C_END_C(1)),
	dump_register(S_STA_C(1)),
	dump_register(S_END_C(1)),
	dump_register(L_STA_C(1)),
	dump_register(L_END_C(1)),
	dump_register(AXI_STS(1)),
	dump_register(HPBUS(1)),
	dump_register(INT_STS),
	dump_register(INT_MSK),
	dump_register(INT_RAW),
	dump_register(INT_ICR),
	},
	{
	dump_register(IPVER_INFO),
	dump_register(IPFREQENCY),
	dump_register(TARGETID_L),
	dump_register(TARGETID_H),
	dump_register(IPSTART(2)),
	dump_register(INSADDR(2)),
	dump_register(ADDR0_L(2)),
	dump_register(ADDR0_H(2)),
	dump_register(ADDR1_L(2)),
	dump_register(ADDR1_H(2)),
	dump_register(ADDR2_L(2)),
	dump_register(ADDR2_H(2)),
	dump_register(ADDR3_L(2)),
	dump_register(ADDR3_H(2)),
	dump_register(ADDR4_L(2)),
	dump_register(ADDR4_H(2)),
	dump_register(ADDR5_L(2)),
	dump_register(ADDR5_H(2)),
	dump_register(ADDR6_L(2)),
	dump_register(ADDR6_H(2)),
	dump_register(ADDR7_L(2)),
	dump_register(ADDR7_H(2)),
	dump_register(CYCLE_L(2)),
	dump_register(CYCLE_H(2)),
	dump_register(P_STA_C(2)),
	dump_register(P_END_C(2)),
	dump_register(C_STA_C(2)),
	dump_register(C_END_C(2)),
	dump_register(S_STA_C(2)),
	dump_register(S_END_C(2)),
	dump_register(L_STA_C(2)),
	dump_register(L_END_C(2)),
	dump_register(AXI_STS(2)),
	dump_register(HPBUS(2)),
	dump_register(INT_STS),
	dump_register(INT_MSK),
	dump_register(INT_RAW),
	dump_register(INT_ICR),
	},
	{
	dump_register(IPVER_INFO),
	dump_register(IPFREQENCY),
	dump_register(TARGETID_L),
	dump_register(TARGETID_H),
	dump_register(IPSTART(3)),
	dump_register(INSADDR(3)),
	dump_register(ADDR0_L(3)),
	dump_register(ADDR0_H(3)),
	dump_register(ADDR1_L(3)),
	dump_register(ADDR1_H(3)),
	dump_register(ADDR2_L(3)),
	dump_register(ADDR2_H(3)),
	dump_register(ADDR3_L(3)),
	dump_register(ADDR3_H(3)),
	dump_register(ADDR4_L(3)),
	dump_register(ADDR4_H(3)),
	dump_register(ADDR5_L(3)),
	dump_register(ADDR5_H(3)),
	dump_register(ADDR6_L(3)),
	dump_register(ADDR6_H(3)),
	dump_register(ADDR7_L(3)),
	dump_register(ADDR7_H(3)),
	dump_register(CYCLE_L(3)),
	dump_register(CYCLE_H(3)),
	dump_register(P_STA_C(3)),
	dump_register(P_END_C(3)),
	dump_register(C_STA_C(3)),
	dump_register(C_END_C(3)),
	dump_register(S_STA_C(3)),
	dump_register(S_END_C(3)),
	dump_register(L_STA_C(3)),
	dump_register(L_END_C(3)),
	dump_register(AXI_STS(3)),
	dump_register(HPBUS(3)),
	dump_register(INT_STS),
	dump_register(INT_MSK),
	dump_register(INT_RAW),
	dump_register(INT_ICR),
	},
};

static const struct debugfs_reg32 sfm_regs[] = {
	dump_register(IPVER_INFO),
	dump_register(IPFREQENCY),
	dump_register(TARGETID_L),
	dump_register(TARGETID_H),
	dump_register(SFM_INT_DONE),
	dump_register(SFM_CMD_XLEN),
	dump_register(SFM_CMD_YLEN),
	dump_register(SFM_SRC_ADDR),
	dump_register(SFM_DST_ADDR),
	dump_register(SFM_CMD_SCAL),
	dump_register(SFM_CMD_OFF),
	dump_register(SFM_INT_CLR),
	dump_register(SFM_START),
	dump_register(SFM_RESET),
	dump_register(SFM_MODE),
	dump_register(INT_STS),
	dump_register(INT_MSK),
	dump_register(INT_RAW),
	dump_register(INT_ICR),
};

static int dump_show(struct seq_file *seq, void *v)
{
	struct xdpu_dev *xdpu = seq->private;
	struct dpu_buffer_block *h;
	static const char units[] = "KMG";
	const char *unit = units;
	unsigned long delta = 0;

	seq_puts(seq,
		 "Virtual Address\t\t\t\tRequest Mem\t\tPhysical Address\n");
	list_for_each_entry(h, &xdpu->head, head) {
		delta = (h->capacity) >> 10;
		while (!(delta & 1023) && unit[1]) {
			delta >>= 10;
			unit++;
		}
		seq_printf(seq, "%p-%p   %9lu%c         %016llx-%016llx\n",
			   h->vaddr, h->vaddr + h->capacity,
			   delta, *unit,
			   (u64)h->dma_addr, (u64)(h->dma_addr + h->capacity));
		delta = 0;
		unit = units;
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(dump);

/**
 * dpu_debugfs_init - create DPU debugfs directory.
 * @xdpu:	dpu structure
 *
 * Create DPU debugfs directory. Returns zero in case of success and a negative
 * error code in case of failure.
 *
 * Return:	0 if successful; otherwise -errno
 */
static int dpu_debugfs_init(struct xdpu_dev *xdpu)
{
	char buf[32];
	struct debugfs_regset32 *regset;
	struct dentry *dentry;
	int i;

	xdpu->root = debugfs_create_dir("dpu", NULL);
	if (IS_ERR(xdpu->root)) {
		dev_err(xdpu->dev, "failed to create debugfs root\n");
		return -ENODEV;
	}

	debugfs_create_file("dma_pool", 0444, xdpu->root, xdpu, &dump_fops);

	for (i = 0; i < xdpu->dpu_cnt; i++) {
		if (snprintf(buf, 32, "cu-%d", i) < 0)
			return -EINVAL;

		dentry = debugfs_create_dir(buf, xdpu->root);
		regset = devm_kzalloc(xdpu->dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOMEM;

		regset->regs = cu_regs[i];
		regset->nregs = ARRAY_SIZE(cu_regs[i]);
		regset->base = xdpu->regs;
		debugfs_create_regset32("registers", 0444, dentry, regset);
	}

	if (xdpu->sfm_cnt) {
		dentry = debugfs_create_dir("softmax", xdpu->root);
		regset = devm_kzalloc(xdpu->dev, sizeof(*regset), GFP_KERNEL);
		if (!regset)
			return -ENOMEM;

		regset->regs = sfm_regs;
		regset->nregs = ARRAY_SIZE(sfm_regs);
		regset->base = xdpu->regs;
		debugfs_create_regset32("registers", 0444, dentry, regset);
	}
	return 0;
}
#endif

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Ye Yang <ye.yang@xilinx.com>");
MODULE_LICENSE("GPL v2");
