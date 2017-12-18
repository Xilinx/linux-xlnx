/*
 * VGIC: KVM DEVICE API
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <linux/uaccess.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/* common helpers */

int vgic_check_ioaddr(struct kvm *kvm, phys_addr_t *ioaddr,
		      phys_addr_t addr, phys_addr_t alignment)
{
	if (addr & ~KVM_PHYS_MASK)
		return -E2BIG;

	if (!IS_ALIGNED(addr, alignment))
		return -EINVAL;

	if (!IS_VGIC_ADDR_UNDEF(*ioaddr))
		return -EEXIST;

	return 0;
}

/**
 * kvm_vgic_addr - set or get vgic VM base addresses
 * @kvm:   pointer to the vm struct
 * @type:  the VGIC addr type, one of KVM_VGIC_V[23]_ADDR_TYPE_XXX
 * @addr:  pointer to address value
 * @write: if true set the address in the VM address space, if false read the
 *          address
 *
 * Set or get the vgic base addresses for the distributor and the virtual CPU
 * interface in the VM physical address space.  These addresses are properties
 * of the emulated core/SoC and therefore user space initially knows this
 * information.
 * Check them for sanity (alignment, double assignment). We can't check for
 * overlapping regions in case of a virtual GICv3 here, since we don't know
 * the number of VCPUs yet, so we defer this check to map_resources().
 */
int kvm_vgic_addr(struct kvm *kvm, unsigned long type, u64 *addr, bool write)
{
	int r = 0;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	int type_needed;
	phys_addr_t *addr_ptr, alignment;

	mutex_lock(&kvm->lock);
	switch (type) {
	case KVM_VGIC_V2_ADDR_TYPE_DIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V2;
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_4K;
		break;
	case KVM_VGIC_V2_ADDR_TYPE_CPU:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V2;
		addr_ptr = &vgic->vgic_cpu_base;
		alignment = SZ_4K;
		break;
	case KVM_VGIC_V3_ADDR_TYPE_DIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V3;
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_64K;
		break;
	case KVM_VGIC_V3_ADDR_TYPE_REDIST:
		type_needed = KVM_DEV_TYPE_ARM_VGIC_V3;
		addr_ptr = &vgic->vgic_redist_base;
		alignment = SZ_64K;
		break;
	default:
		r = -ENODEV;
		goto out;
	}

	if (vgic->vgic_model != type_needed) {
		r = -ENODEV;
		goto out;
	}

	if (write) {
		r = vgic_check_ioaddr(kvm, addr_ptr, *addr, alignment);
		if (!r)
			*addr_ptr = *addr;
	} else {
		*addr = *addr_ptr;
	}

out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int vgic_set_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		r = kvm_vgic_addr(dev->kvm, type, &addr, true);
		return (r == -ENODEV) ? -ENXIO : r;
	}
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 val;
		int ret = 0;

		if (get_user(val, uaddr))
			return -EFAULT;

		/*
		 * We require:
		 * - at least 32 SPIs on top of the 16 SGIs and 16 PPIs
		 * - at most 1024 interrupts
		 * - a multiple of 32 interrupts
		 */
		if (val < (VGIC_NR_PRIVATE_IRQS + 32) ||
		    val > VGIC_MAX_RESERVED ||
		    (val & 31))
			return -EINVAL;

		mutex_lock(&dev->kvm->lock);

		if (vgic_ready(dev->kvm) || dev->kvm->arch.vgic.nr_spis)
			ret = -EBUSY;
		else
			dev->kvm->arch.vgic.nr_spis =
				val - VGIC_NR_PRIVATE_IRQS;

		mutex_unlock(&dev->kvm->lock);

		return ret;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL: {
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			mutex_lock(&dev->kvm->lock);
			r = vgic_init(dev->kvm);
			mutex_unlock(&dev->kvm->lock);
			return r;
		}
		break;
	}
	}

	return -ENXIO;
}

static int vgic_get_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r = -ENXIO;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		r = kvm_vgic_addr(dev->kvm, type, &addr, false);
		if (r)
			return (r == -ENODEV) ? -ENXIO : r;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;

		r = put_user(dev->kvm->arch.vgic.nr_spis +
			     VGIC_NR_PRIVATE_IRQS, uaddr);
		break;
	}
	}

	return r;
}

static int vgic_create(struct kvm_device *dev, u32 type)
{
	return kvm_vgic_create(dev->kvm, type);
}

static void vgic_destroy(struct kvm_device *dev)
{
	kfree(dev);
}

int kvm_register_vgic_device(unsigned long type)
{
	int ret = -ENODEV;

	switch (type) {
	case KVM_DEV_TYPE_ARM_VGIC_V2:
		ret = kvm_register_device_ops(&kvm_arm_vgic_v2_ops,
					      KVM_DEV_TYPE_ARM_VGIC_V2);
		break;
	case KVM_DEV_TYPE_ARM_VGIC_V3:
		ret = kvm_register_device_ops(&kvm_arm_vgic_v3_ops,
					      KVM_DEV_TYPE_ARM_VGIC_V3);

#ifdef CONFIG_KVM_ARM_VGIC_V3_ITS
		if (ret)
			break;
		ret = kvm_vgic_register_its_device();
#endif
		break;
	}

	return ret;
}

struct vgic_reg_attr {
	struct kvm_vcpu *vcpu;
	gpa_t addr;
};

static int parse_vgic_v2_attr(struct kvm_device *dev,
			      struct kvm_device_attr *attr,
			      struct vgic_reg_attr *reg_attr)
{
	int cpuid;

	cpuid = (attr->attr & KVM_DEV_ARM_VGIC_CPUID_MASK) >>
		 KVM_DEV_ARM_VGIC_CPUID_SHIFT;

	if (cpuid >= atomic_read(&dev->kvm->online_vcpus))
		return -EINVAL;

	reg_attr->vcpu = kvm_get_vcpu(dev->kvm, cpuid);
	reg_attr->addr = attr->attr & KVM_DEV_ARM_VGIC_OFFSET_MASK;

	return 0;
}

/* unlocks vcpus from @vcpu_lock_idx and smaller */
static void unlock_vcpus(struct kvm *kvm, int vcpu_lock_idx)
{
	struct kvm_vcpu *tmp_vcpu;

	for (; vcpu_lock_idx >= 0; vcpu_lock_idx--) {
		tmp_vcpu = kvm_get_vcpu(kvm, vcpu_lock_idx);
		mutex_unlock(&tmp_vcpu->mutex);
	}
}

static void unlock_all_vcpus(struct kvm *kvm)
{
	unlock_vcpus(kvm, atomic_read(&kvm->online_vcpus) - 1);
}

/* Returns true if all vcpus were locked, false otherwise */
static bool lock_all_vcpus(struct kvm *kvm)
{
	struct kvm_vcpu *tmp_vcpu;
	int c;

	/*
	 * Any time a vcpu is run, vcpu_load is called which tries to grab the
	 * vcpu->mutex.  By grabbing the vcpu->mutex of all VCPUs we ensure
	 * that no other VCPUs are run and fiddle with the vgic state while we
	 * access it.
	 */
	kvm_for_each_vcpu(c, tmp_vcpu, kvm) {
		if (!mutex_trylock(&tmp_vcpu->mutex)) {
			unlock_vcpus(kvm, c - 1);
			return false;
		}
	}

	return true;
}

/**
 * vgic_attr_regs_access_v2 - allows user space to access VGIC v2 state
 *
 * @dev:      kvm device handle
 * @attr:     kvm device attribute
 * @reg:      address the value is read or written
 * @is_write: true if userspace is writing a register
 */
static int vgic_attr_regs_access_v2(struct kvm_device *dev,
				    struct kvm_device_attr *attr,
				    u32 *reg, bool is_write)
{
	struct vgic_reg_attr reg_attr;
	gpa_t addr;
	struct kvm_vcpu *vcpu;
	int ret;

	ret = parse_vgic_v2_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	mutex_lock(&dev->kvm->lock);

	ret = vgic_init(dev->kvm);
	if (ret)
		goto out;

	if (!lock_all_vcpus(dev->kvm)) {
		ret = -EBUSY;
		goto out;
	}

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		ret = vgic_v2_cpuif_uaccess(vcpu, is_write, addr, reg);
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		ret = vgic_v2_dist_uaccess(vcpu, is_write, addr, reg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	unlock_all_vcpus(dev->kvm);
out:
	mutex_unlock(&dev->kvm->lock);
	return ret;
}

static int vgic_v2_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_set_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 reg;

		if (get_user(reg, uaddr))
			return -EFAULT;

		return vgic_attr_regs_access_v2(dev, attr, &reg, true);
	}
	}

	return -ENXIO;
}

static int vgic_v2_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_get_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 reg = 0;

		ret = vgic_attr_regs_access_v2(dev, attr, &reg, false);
		if (ret)
			return ret;
		return put_user(reg, uaddr);
	}
	}

	return -ENXIO;
}

static int vgic_v2_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V2_ADDR_TYPE_DIST:
		case KVM_VGIC_V2_ADDR_TYPE_CPU:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		return vgic_v2_has_attr_regs(dev, attr);
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v2_ops = {
	.name = "kvm-arm-vgic-v2",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v2_set_attr,
	.get_attr = vgic_v2_get_attr,
	.has_attr = vgic_v2_has_attr,
};

static int vgic_v3_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return vgic_set_common_attr(dev, attr);
}

static int vgic_v3_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	return vgic_get_common_attr(dev, attr);
}

static int vgic_v3_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V3_ADDR_TYPE_DIST:
		case KVM_VGIC_V3_ADDR_TYPE_REDIST:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v3_ops = {
	.name = "kvm-arm-vgic-v3",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v3_set_attr,
	.get_attr = vgic_v3_get_attr,
	.has_attr = vgic_v3_has_attr,
};
