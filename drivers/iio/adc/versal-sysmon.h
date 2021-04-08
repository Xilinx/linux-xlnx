/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Xilinx SYSMON for Versal
 *
 * Copyright (C) 2019 - 2021 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SYSMON on Versal. The driver supports INDIO Mode
 * and supports voltage and temperature monitoring via IIO sysfs interface.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/adc/versal-sysmon-events.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_address.h>

/* Channel IDs for Temp Channels */
/* TEMP_MAX gives the current temperature for Production
 * silicon.
 * TEMP_MAX gives the current maximum temperature for ES1
 * silicon.
 */
#define TEMP_MAX	160

/* TEMP_MIN is not applicable for Production silicon.
 * TEMP_MIN gives the current minimum temperature for ES1 silicon.
 */
#define TEMP_MIN	161

#define TEMP_MAX_MAX	162
#define TEMP_MIN_MIN	163
#define TEMP_EVENT	164
#define OT_EVENT	165

/* Register Unlock Code */
#define NPI_UNLOCK	0xF9E8D7C6

/* Register Offsets */
#define SYSMON_NPI_LOCK		0x000C
#define SYSMON_ISR		0x0044
#define SYSMON_TEMP_MASK	0x300
#define SYSMON_IMR		0x0048
#define SYSMON_IER		0x004C
#define SYSMON_IDR		0x0050
#define SYSMON_ALARM_FLAG	0x1018
#define SYSMON_TEMP_MAX		0x1030
#define SYSMON_TEMP_MIN		0x1034
#define SYSMON_SUPPLY_BASE	0x1040
#define SYSMON_ALARM_REG	0x1940
#define SYSMON_TEMP_TH_LOW	0x1970
#define SYSMON_TEMP_TH_UP	0x1974
#define SYSMON_OT_TH_LOW	0x1978
#define SYSMON_OT_TH_UP		0x197C
#define SYSMON_SUPPLY_TH_LOW	0x1980
#define SYSMON_SUPPLY_TH_UP	0x1C80
#define SYSMON_TEMP_MAX_MAX	0x1F90
#define SYSMON_TEMP_MIN_MIN	0x1F8C
#define SYSMON_TEMP_EV_CFG	0x1F84
#define SYSMON_NODE_OFFSET	0x1FAC
#define SYSMON_STATUS_RESET	0x1F94

#define SYSMON_NO_OF_EVENTS	32

/* Supply Voltage Conversion macros */
#define SYSMON_MANTISSA_MASK		0xFFFF
#define SYSMON_FMT_MASK			0x10000
#define SYSMON_FMT_SHIFT		16
#define SYSMON_MODE_MASK		0x60000
#define SYSMON_MODE_SHIFT		17
#define SYSMON_MANTISSA_SIGN_SHIFT	15
#define SYSMON_UPPER_SATURATION_SIGNED	32767
#define SYSMON_LOWER_SATURATION_SIGNED	-32768
#define SYSMON_UPPER_SATURATION		65535
#define SYSMON_LOWER_SATURATION		0

#define SYSMON_CHAN_TEMP_EVENT(_address, _ext, _events) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = _address, \
	.channel = _address, \
	.event_spec = _events, \
	.num_event_specs = ARRAY_SIZE(_events), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 15, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
	}

#define SYSMON_CHAN_TEMP(_address, _ext) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.address = _address, \
	.channel = _address, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_PROCESSED), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 15, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.extend_name = _ext, \
}

#define twoscomp(val) ((((val) ^ 0xFFFF) + 1) & 0x0000FFFF)
#define ALARM_REG(address) ((address) / 32)
#define ALARM_SHIFT(address) ((address) % 32)

#define compare(val, thresh) (((val) & 0x8000) || ((thresh) & 0x8000) ? \
			      ((val) < (thresh)) : ((val) > (thresh)))  \

enum sysmon_alarm_bit {
	SYSMON_BIT_ALARM0 = 0,
	SYSMON_BIT_ALARM1 = 1,
	SYSMON_BIT_ALARM2 = 2,
	SYSMON_BIT_ALARM3 = 3,
	SYSMON_BIT_ALARM4 = 4,
	SYSMON_BIT_ALARM5 = 5,
	SYSMON_BIT_ALARM6 = 6,
	SYSMON_BIT_ALARM7 = 7,
	SYSMON_BIT_OT = 8,
	SYSMON_BIT_TEMP = 9,
};

/**
 * struct sysmon - Driver data for Sysmon
 * @base: physical base address of device
 * @dev: pointer to device struct
 * @mutex: to handle multiple user interaction
 * @lock: to help manage interrupt registers correctly
 * @irq: interrupt number of the sysmon
 * @region_list: list of the regions of sysmon
 * @masked_temp: currently masked due to alarm
 * @temp_mask: temperature based interrupt configuration
 * @sysmon_unmask_work: re-enables event once the event condition disappears
 *
 * This structure contains necessary state for Sysmon driver to operate
 */
struct sysmon {
	void __iomem *base;
	struct device *dev;
	/* kernel doc above */
	struct mutex mutex;
	/* kernel doc above*/
	spinlock_t lock;
	int irq;
	struct list_head region_list;
	unsigned int masked_temp;
	unsigned int temp_mask;
	struct delayed_work sysmon_unmask_work;
};

int sysmon_register_temp_ops(void (*cb)(void *data, struct regional_node *node),
			     void *data, enum sysmon_region region_id);
int sysmon_unregister_temp_ops(enum sysmon_region region_id);
struct list_head *sysmon_nodes_by_region(enum sysmon_region region_id);
int sysmon_get_node_value(int sat_id);
