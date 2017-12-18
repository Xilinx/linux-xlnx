#ifndef __NVKM_FIFO_PRIV_H__
#define __NVKM_FIFO_PRIV_H__
#define nvkm_fifo(p) container_of((p), struct nvkm_fifo, engine)
#include <engine/fifo.h>

int nvkm_fifo_ctor(const struct nvkm_fifo_func *, struct nvkm_device *,
		   int index, int nr, struct nvkm_fifo *);
void nvkm_fifo_uevent(struct nvkm_fifo *);

struct nvkm_fifo_chan_oclass;
struct nvkm_fifo_func {
	void *(*dtor)(struct nvkm_fifo *);
	int (*oneinit)(struct nvkm_fifo *);
	void (*init)(struct nvkm_fifo *);
	void (*fini)(struct nvkm_fifo *);
	void (*intr)(struct nvkm_fifo *);
	void (*pause)(struct nvkm_fifo *, unsigned long *);
	void (*start)(struct nvkm_fifo *, unsigned long *);
	void (*uevent_init)(struct nvkm_fifo *);
	void (*uevent_fini)(struct nvkm_fifo *);
	int (*class_get)(struct nvkm_fifo *, int index,
			 const struct nvkm_fifo_chan_oclass **);
	const struct nvkm_fifo_chan_oclass *chan[];
};

void nv04_fifo_intr(struct nvkm_fifo *);
void nv04_fifo_pause(struct nvkm_fifo *, unsigned long *);
void nv04_fifo_start(struct nvkm_fifo *, unsigned long *);
#endif
