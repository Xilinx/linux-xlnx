#ifndef __NVKM_INSTMEM_H__
#define __NVKM_INSTMEM_H__
#include <core/subdev.h>
struct nvkm_memory;

struct nvkm_instmem {
	const struct nvkm_instmem_func *func;
	struct nvkm_subdev subdev;

	spinlock_t lock;
	struct list_head list;
	u32 reserved;

	struct nvkm_memory *vbios;
	struct nvkm_ramht  *ramht;
	struct nvkm_memory *ramro;
	struct nvkm_memory *ramfc;
};

u32 nvkm_instmem_rd32(struct nvkm_instmem *, u32 addr);
void nvkm_instmem_wr32(struct nvkm_instmem *, u32 addr, u32 data);
int nvkm_instobj_new(struct nvkm_instmem *, u32 size, u32 align, bool zero,
		     struct nvkm_memory **);


int nv04_instmem_new(struct nvkm_device *, int, struct nvkm_instmem **);
int nv40_instmem_new(struct nvkm_device *, int, struct nvkm_instmem **);
int nv50_instmem_new(struct nvkm_device *, int, struct nvkm_instmem **);
int gk20a_instmem_new(struct nvkm_device *, int, struct nvkm_instmem **);
#endif
