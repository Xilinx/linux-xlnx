#ifndef __NVKM_ICCSENSE_H__
#define __NVKM_ICCSENSE_H__

#include <core/subdev.h>

struct nvkm_iccsense {
	struct nvkm_subdev subdev;
	bool data_valid;
	struct list_head sensors;
	struct list_head rails;
};

int gf100_iccsense_new(struct nvkm_device *, int index, struct nvkm_iccsense **);
int nvkm_iccsense_read_all(struct nvkm_iccsense *iccsense);
#endif
