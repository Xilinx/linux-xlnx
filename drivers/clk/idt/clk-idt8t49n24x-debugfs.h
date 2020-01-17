/* SPDX-License-Identifier: GPL-2.0 */
/* clk-idt8t49n24x-debugfs.h - Debugfs support for 8T49N24x
 *
 * Copyright (C) 2018, Integrated Device Technology, Inc. <david.cater@idt.com>
 *
 * See https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html
 * This program is distributed "AS IS" and  WITHOUT ANY WARRANTY;
 * including the implied warranties of MERCHANTABILITY, FITNESS FOR
 * A PARTICULAR PURPOSE, or NON-INFRINGEMENT.
 */

#ifndef __IDT_CLK_IDT8T49N24X_DEBUGFS_H_
#define __IDT_CLK_IDT8T49N24X_DEBUGFS_H_

#include "clk-idt8t49n24x-core.h"

int idt24x_expose_via_debugfs(struct i2c_client *client,
			      struct clk_idt24x_chip *chip);
void idt24x_cleanup_debugfs(struct clk_idt24x_chip *chip);

#endif /* __IDT_CLK_IDT8T49N24X_DEBUGFS_H_*/
