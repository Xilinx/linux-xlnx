/*
 *  linux/drivers/mmc/core/host.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MMC_CORE_HOST_H
#define _MMC_CORE_HOST_H
#include <linux/mmc/host.h>

int mmc_register_host_class(void);
void mmc_unregister_host_class(void);

void mmc_retune_enable(struct mmc_host *host);
void mmc_retune_disable(struct mmc_host *host);
void mmc_retune_hold(struct mmc_host *host);
void mmc_retune_release(struct mmc_host *host);
int mmc_retune(struct mmc_host *host);

#endif

