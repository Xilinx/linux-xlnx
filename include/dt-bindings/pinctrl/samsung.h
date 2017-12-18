/*
 * Samsung's Exynos pinctrl bindings
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Author: Krzysztof Kozlowski <krzk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __DT_BINDINGS_PINCTRL_SAMSUNG_H__
#define __DT_BINDINGS_PINCTRL_SAMSUNG_H__

#define EXYNOS_PIN_PULL_NONE		0
#define EXYNOS_PIN_PULL_DOWN		1
#define EXYNOS_PIN_PULL_UP		3

#define S3C64XX_PIN_PULL_NONE		0
#define S3C64XX_PIN_PULL_DOWN		1
#define S3C64XX_PIN_PULL_UP		2

/* Pin function in power down mode */
#define EXYNOS_PIN_PDN_OUT0		0
#define EXYNOS_PIN_PDN_OUT1		1
#define EXYNOS_PIN_PDN_INPUT		2
#define EXYNOS_PIN_PDN_PREV		3

/* Drive strengths for Exynos3250, Exynos4 (all) and Exynos5250 */
#define EXYNOS4_PIN_DRV_LV1		0
#define EXYNOS4_PIN_DRV_LV2		2
#define EXYNOS4_PIN_DRV_LV3		1
#define EXYNOS4_PIN_DRV_LV4		3

/* Drive strengths for Exynos5260 */
#define EXYNOS5260_PIN_DRV_LV1		0
#define EXYNOS5260_PIN_DRV_LV2		1
#define EXYNOS5260_PIN_DRV_LV4		2
#define EXYNOS5260_PIN_DRV_LV6		3

/* Drive strengths for Exynos5410, Exynos542x and Exynos5800 */
#define EXYNOS5420_PIN_DRV_LV1		0
#define EXYNOS5420_PIN_DRV_LV2		1
#define EXYNOS5420_PIN_DRV_LV3		2
#define EXYNOS5420_PIN_DRV_LV4		3

#define EXYNOS_PIN_FUNC_INPUT		0
#define EXYNOS_PIN_FUNC_OUTPUT		1
#define EXYNOS_PIN_FUNC_2		2
#define EXYNOS_PIN_FUNC_3		3
#define EXYNOS_PIN_FUNC_4		4
#define EXYNOS_PIN_FUNC_5		5
#define EXYNOS_PIN_FUNC_6		6
#define EXYNOS_PIN_FUNC_F		0xf

#endif /* __DT_BINDINGS_PINCTRL_SAMSUNG_H__ */
