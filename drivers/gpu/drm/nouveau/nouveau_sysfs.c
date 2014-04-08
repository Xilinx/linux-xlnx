/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include "nouveau_sysfs.h"

#include <core/object.h>
#include <core/class.h>

static inline struct drm_device *
drm_device(struct device *d)
{
	return pci_get_drvdata(to_pci_dev(d));
}

#define snappendf(p,r,f,a...) do {                                             \
	snprintf(p, r, f, ##a);                                                \
	r -= strlen(p);                                                        \
	p += strlen(p);                                                        \
} while(0)

static ssize_t
nouveau_sysfs_pstate_get(struct device *d, struct device_attribute *a, char *b)
{
	struct nouveau_sysfs *sysfs = nouveau_sysfs(drm_device(d));
	struct nv_control_pstate_info info;
	size_t cnt = PAGE_SIZE;
	char *buf = b;
	int ret, i;

	ret = nv_exec(sysfs->ctrl, NV_CONTROL_PSTATE_INFO, &info, sizeof(info));
	if (ret)
		return ret;

	for (i = 0; i < info.count + 1; i++) {
		const s32 state = i < info.count ? i :
			NV_CONTROL_PSTATE_ATTR_STATE_CURRENT;
		struct nv_control_pstate_attr attr = {
			.state = state,
			.index = 0,
		};

		ret = nv_exec(sysfs->ctrl, NV_CONTROL_PSTATE_ATTR,
			     &attr, sizeof(attr));
		if (ret)
			return ret;

		if (i < info.count)
			snappendf(buf, cnt, "%02x:", attr.state);
		else
			snappendf(buf, cnt, "--:");

		attr.index = 0;
		do {
			attr.state = state;
			ret = nv_exec(sysfs->ctrl, NV_CONTROL_PSTATE_ATTR,
				     &attr, sizeof(attr));
			if (ret)
				return ret;

			snappendf(buf, cnt, " %s %d", attr.name, attr.min);
			if (attr.min != attr.max)
				snappendf(buf, cnt, "-%d", attr.max);
			snappendf(buf, cnt, " %s", attr.unit);
		} while (attr.index);

		if ((state >= 0 && info.pstate == state) ||
		    (state <  0 && info.ustate < 0))
			snappendf(buf, cnt, " *");
		snappendf(buf, cnt, "\n");
	}

	return strlen(b);
}

static ssize_t
nouveau_sysfs_pstate_set(struct device *d, struct device_attribute *a,
			 const char *buf, size_t count)
{
	struct nouveau_sysfs *sysfs = nouveau_sysfs(drm_device(d));
	struct nv_control_pstate_user args;
	long value, ret;
	char *tmp;

	if ((tmp = strchr(buf, '\n')))
		*tmp = '\0';

	if (!strcasecmp(buf, "none"))
		args.state = NV_CONTROL_PSTATE_USER_STATE_UNKNOWN;
	else
	if (!strcasecmp(buf, "auto"))
		args.state = NV_CONTROL_PSTATE_USER_STATE_PERFMON;
	else {
		ret = kstrtol(buf, 16, &value);
		if (ret)
			return ret;
		args.state = value;
	}

	ret = nv_exec(sysfs->ctrl, NV_CONTROL_PSTATE_USER, &args, sizeof(args));
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(pstate, S_IRUGO | S_IWUSR,
		   nouveau_sysfs_pstate_get, nouveau_sysfs_pstate_set);

void
nouveau_sysfs_fini(struct drm_device *dev)
{
	struct nouveau_sysfs *sysfs = nouveau_sysfs(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);

	if (sysfs->ctrl) {
		device_remove_file(&dev->pdev->dev, &dev_attr_pstate);
		nouveau_object_del(nv_object(drm), NVDRM_DEVICE, NVDRM_CONTROL);
	}

	drm->sysfs = NULL;
	kfree(sysfs);
}

int
nouveau_sysfs_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_sysfs *sysfs;
	int ret;

	sysfs = drm->sysfs = kzalloc(sizeof(*sysfs), GFP_KERNEL);
	if (!sysfs)
		return -ENOMEM;

	ret = nouveau_object_new(nv_object(drm), NVDRM_DEVICE, NVDRM_CONTROL,
				 NV_CONTROL_CLASS, NULL, 0, &sysfs->ctrl);
	if (ret == 0)
		device_create_file(&dev->pdev->dev, &dev_attr_pstate);

	return 0;
}
