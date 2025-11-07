/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

#ifndef _LINUX_LUO_INTERNAL_H
#define _LINUX_LUO_INTERNAL_H

#include <linux/liveupdate.h>

void *luo_alloc_preserve(size_t size);
void luo_free_unpreserve(void *mem, size_t size);
void luo_free_restore(void *mem, size_t size);

#endif /* _LINUX_LUO_INTERNAL_H */
