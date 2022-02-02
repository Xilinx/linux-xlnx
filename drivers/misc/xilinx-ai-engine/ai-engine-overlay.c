// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx AI Engine device driver
 *
 * Copyright (C) 2020 Xilinx, Inc.
 */
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/xlnx-ai-engine.h>
#include <uapi/linux/xlnx-ai-engine.h>

#include "ai-engine-internal.h"

/**
 * of_aie_notify_pre_remove() - pre-remove overlay notification
 *
 * @adev: AI engine device
 * @nd: overlay notification data
 * @return: returns 0 for success, and error code for failure.
 *
 * Called when an overlay targeted to an AI engine device is about to be
 * removed. Remove AI engine apertures specified in the device tree overlay to
 * the target AI engine device.
 */
static int of_aie_notify_pre_remove(struct aie_device *adev,
				    struct of_overlay_notify_data *nd)
{
	struct device_node *nc;

	for_each_available_child_of_node(nd->overlay, nc) {
		struct list_head *node, *pos;
		int ret;

		if (!of_node_test_and_set_flag(nc, OF_POPULATED))
			continue;

		ret = mutex_lock_interruptible(&adev->mlock);
		if (ret)
			return ret;

		list_for_each_safe(pos, node, &adev->apertures) {
			struct aie_aperture *aperture;

			aperture = list_entry(pos, struct aie_aperture, node);
			ret = aie_aperture_remove(aperture);
			if (ret) {
				mutex_unlock(&adev->mlock);
				return ret;
			}
		}

		mutex_unlock(&adev->mlock);
	}

	return 0;
}

/**
 * of_aie_notify() - AI engine notifier for dynamic DT changes
 * @nb: notifier block
 * @action: notifier action
 * @arg: reconfig data
 * @return: NOTIFY_OK for success, error code for failure
 *
 * This notifier handles AI engine device node overlay.
 */
static int of_aie_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	struct of_overlay_notify_data *nd = arg;
	struct aie_device *adev;
	int ret = 0;

	switch (action) {
	case OF_OVERLAY_POST_APPLY:
		adev = of_ai_engine_class_find(nd->target);
		if (!adev)
			return NOTIFY_BAD;

		of_xilinx_ai_engine_aperture_probe(adev);
		break;
	case OF_OVERLAY_PRE_REMOVE:
		adev = of_ai_engine_class_find(nd->target);
		if (!adev)
			return NOTIFY_BAD;

		ret = of_aie_notify_pre_remove(adev, nd);
		break;
	default:
		return NOTIFY_OK;
	}

	if (ret)
		return notifier_from_errno(ret);

	return NOTIFY_OK;
}

static struct notifier_block aie_of_nb = {
	.notifier_call = of_aie_notify,
};

/**
 * aie_overlay_register_notifier() - register AI engine device tree overlay
 *				     notifier
 * @return: 0 for success, error code for failure
 */
int aie_overlay_register_notifier(void)
{
	return of_overlay_notifier_register(&aie_of_nb);
}

/**
 * aie_overlay_unregister_notifier() - unregister AI engine device tree overlay
 *				       notifier
 */
void aie_overlay_unregister_notifier(void)
{
	of_overlay_notifier_unregister(&aie_of_nb);
}
