#ifndef __NVKM_FB_RAM_PRIV_H__
#define __NVKM_FB_RAM_PRIV_H__
#include "priv.h"

int  nvkm_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		   enum nvkm_ram_type, u64 size, u32 tags,
		   struct nvkm_ram *);
int  nvkm_ram_new_(const struct nvkm_ram_func *, struct nvkm_fb *,
		   enum nvkm_ram_type, u64 size, u32 tags,
		   struct nvkm_ram **);
void nvkm_ram_del(struct nvkm_ram **);
int  nvkm_ram_init(struct nvkm_ram *);

extern const struct nvkm_ram_func nv04_ram_func;

int  nv50_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		   struct nvkm_ram *);
int  nv50_ram_get(struct nvkm_ram *, u64, u32, u32, u32, struct nvkm_mem **);
void nv50_ram_put(struct nvkm_ram *, struct nvkm_mem **);
void __nv50_ram_put(struct nvkm_ram *, struct nvkm_mem *);

int  gf100_ram_ctor(const struct nvkm_ram_func *, struct nvkm_fb *,
		    u32, struct nvkm_ram *);
int  gf100_ram_get(struct nvkm_ram *, u64, u32, u32, u32, struct nvkm_mem **);
void gf100_ram_put(struct nvkm_ram *, struct nvkm_mem **);

int  gk104_ram_init(struct nvkm_ram *ram);

/* RAM type-specific MR calculation routines */
int nvkm_sddr2_calc(struct nvkm_ram *);
int nvkm_sddr3_calc(struct nvkm_ram *);
int nvkm_gddr3_calc(struct nvkm_ram *);
int nvkm_gddr5_calc(struct nvkm_ram *, bool nuts);

int nv04_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv10_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv1a_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv20_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv40_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv41_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv44_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv49_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv4e_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int nv50_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gt215_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int mcp77_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gf100_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gk104_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gm107_ram_new(struct nvkm_fb *, struct nvkm_ram **);
int gp100_ram_new(struct nvkm_fb *, struct nvkm_ram **);
#endif
