/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define gk20a_pmu(p) container_of((p), struct gk20a_pmu, base.subdev)
#include "priv.h"

#include <subdev/clk.h>
#include <subdev/timer.h>
#include <subdev/volt.h>

#define BUSY_SLOT	0
#define CLK_SLOT	7

struct gk20a_pmu_dvfs_data {
	int p_load_target;
	int p_load_max;
	int p_smooth;
	unsigned int avg_load;
};

struct gk20a_pmu {
	struct nvkm_pmu base;
	struct nvkm_alarm alarm;
	struct gk20a_pmu_dvfs_data *data;
};

struct gk20a_pmu_dvfs_dev_status {
	unsigned long total;
	unsigned long busy;
	int cur_state;
};

static int
gk20a_pmu_dvfs_target(struct gk20a_pmu *pmu, int *state)
{
	struct nvkm_clk *clk = pmu->base.subdev.device->clk;

	return nvkm_clk_astate(clk, *state, 0, false);
}

static int
gk20a_pmu_dvfs_get_cur_state(struct gk20a_pmu *pmu, int *state)
{
	struct nvkm_clk *clk = pmu->base.subdev.device->clk;

	*state = clk->pstate;
	return 0;
}

static int
gk20a_pmu_dvfs_get_target_state(struct gk20a_pmu *pmu,
				int *state, int load)
{
	struct gk20a_pmu_dvfs_data *data = pmu->data;
	struct nvkm_clk *clk = pmu->base.subdev.device->clk;
	int cur_level, level;

	/* For GK20A, the performance level is directly mapped to pstate */
	level = cur_level = clk->pstate;

	if (load > data->p_load_max) {
		level = min(clk->state_nr - 1, level + (clk->state_nr / 3));
	} else {
		level += ((load - data->p_load_target) * 10 /
				data->p_load_target) / 2;
		level = max(0, level);
		level = min(clk->state_nr - 1, level);
	}

	nvkm_trace(&pmu->base.subdev, "cur level = %d, new level = %d\n",
		   cur_level, level);

	*state = level;

	if (level == cur_level)
		return 0;
	else
		return 1;
}

static int
gk20a_pmu_dvfs_get_dev_status(struct gk20a_pmu *pmu,
			      struct gk20a_pmu_dvfs_dev_status *status)
{
	struct nvkm_device *device = pmu->base.subdev.device;
	status->busy = nvkm_rd32(device, 0x10a508 + (BUSY_SLOT * 0x10));
	status->total= nvkm_rd32(device, 0x10a508 + (CLK_SLOT * 0x10));
	return 0;
}

static void
gk20a_pmu_dvfs_reset_dev_status(struct gk20a_pmu *pmu)
{
	struct nvkm_device *device = pmu->base.subdev.device;
	nvkm_wr32(device, 0x10a508 + (BUSY_SLOT * 0x10), 0x80000000);
	nvkm_wr32(device, 0x10a508 + (CLK_SLOT * 0x10), 0x80000000);
}

static void
gk20a_pmu_dvfs_work(struct nvkm_alarm *alarm)
{
	struct gk20a_pmu *pmu =
		container_of(alarm, struct gk20a_pmu, alarm);
	struct gk20a_pmu_dvfs_data *data = pmu->data;
	struct gk20a_pmu_dvfs_dev_status status;
	struct nvkm_subdev *subdev = &pmu->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_clk *clk = device->clk;
	struct nvkm_timer *tmr = device->timer;
	struct nvkm_volt *volt = device->volt;
	u32 utilization = 0;
	int state, ret;

	/*
	 * The PMU is initialized before CLK and VOLT, so we have to make sure the
	 * CLK and VOLT are ready here.
	 */
	if (!clk || !volt)
		goto resched;

	ret = gk20a_pmu_dvfs_get_dev_status(pmu, &status);
	if (ret) {
		nvkm_warn(subdev, "failed to get device status\n");
		goto resched;
	}

	if (status.total)
		utilization = div_u64((u64)status.busy * 100, status.total);

	data->avg_load = (data->p_smooth * data->avg_load) + utilization;
	data->avg_load /= data->p_smooth + 1;
	nvkm_trace(subdev, "utilization = %d %%, avg_load = %d %%\n",
		   utilization, data->avg_load);

	ret = gk20a_pmu_dvfs_get_cur_state(pmu, &state);
	if (ret) {
		nvkm_warn(subdev, "failed to get current state\n");
		goto resched;
	}

	if (gk20a_pmu_dvfs_get_target_state(pmu, &state, data->avg_load)) {
		nvkm_trace(subdev, "set new state to %d\n", state);
		gk20a_pmu_dvfs_target(pmu, &state);
	}

resched:
	gk20a_pmu_dvfs_reset_dev_status(pmu);
	nvkm_timer_alarm(tmr, 100000000, alarm);
}

static int
gk20a_pmu_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct gk20a_pmu *pmu = gk20a_pmu(subdev);
	nvkm_timer_alarm_cancel(subdev->device->timer, &pmu->alarm);
	return 0;
}

static void *
gk20a_pmu_dtor(struct nvkm_subdev *subdev)
{
	return gk20a_pmu(subdev);
}

static int
gk20a_pmu_init(struct nvkm_subdev *subdev)
{
	struct gk20a_pmu *pmu = gk20a_pmu(subdev);
	struct nvkm_device *device = pmu->base.subdev.device;

	/* init pwr perf counter */
	nvkm_wr32(device, 0x10a504 + (BUSY_SLOT * 0x10), 0x00200001);
	nvkm_wr32(device, 0x10a50c + (BUSY_SLOT * 0x10), 0x00000002);
	nvkm_wr32(device, 0x10a50c + (CLK_SLOT * 0x10), 0x00000003);

	nvkm_timer_alarm(device->timer, 2000000000, &pmu->alarm);
	return 0;
}

static struct gk20a_pmu_dvfs_data
gk20a_dvfs_data= {
	.p_load_target = 70,
	.p_load_max = 90,
	.p_smooth = 1,
};

static const struct nvkm_subdev_func
gk20a_pmu = {
	.init = gk20a_pmu_init,
	.fini = gk20a_pmu_fini,
	.dtor = gk20a_pmu_dtor,
};

int
gk20a_pmu_new(struct nvkm_device *device, int index, struct nvkm_pmu **ppmu)
{
	static const struct nvkm_pmu_func func = {};
	struct gk20a_pmu *pmu;

	if (!(pmu = kzalloc(sizeof(*pmu), GFP_KERNEL)))
		return -ENOMEM;
	pmu->base.func = &func;
	*ppmu = &pmu->base;

	nvkm_subdev_ctor(&gk20a_pmu, device, index, &pmu->base.subdev);
	pmu->data = &gk20a_dvfs_data;
	nvkm_alarm_init(&pmu->alarm, gk20a_pmu_dvfs_work);
	return 0;
}
