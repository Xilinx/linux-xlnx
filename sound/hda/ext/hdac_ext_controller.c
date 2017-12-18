/*
 *  hdac-ext-controller.c - HD-audio extended controller functions.
 *
 *  Copyright (C) 2014-2015 Intel Corp
 *  Author: Jeeja KP <jeeja.kp@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <sound/hda_register.h>
#include <sound/hdaudio_ext.h>

/*
 * maximum HDAC capablities we should parse to avoid endless looping:
 * currently we have 4 extended caps, so this is future proof for now.
 * extend when this limit is seen meeting in real HW
 */
#define HDAC_MAX_CAPS 10

/*
 * processing pipe helpers - these helpers are useful for dealing with HDA
 * new capability of processing pipelines
 */

/**
 * snd_hdac_ext_bus_ppcap_enable - enable/disable processing pipe capability
 * @ebus: HD-audio extended core bus
 * @enable: flag to turn on/off the capability
 */
void snd_hdac_ext_bus_ppcap_enable(struct hdac_ext_bus *ebus, bool enable)
{
	struct hdac_bus *bus = &ebus->bus;

	if (!bus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL");
		return;
	}

	if (enable)
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL, 0, AZX_PPCTL_GPROCEN);
	else
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL, AZX_PPCTL_GPROCEN, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_enable);

/**
 * snd_hdac_ext_bus_ppcap_int_enable - ppcap interrupt enable/disable
 * @ebus: HD-audio extended core bus
 * @enable: flag to enable/disable interrupt
 */
void snd_hdac_ext_bus_ppcap_int_enable(struct hdac_ext_bus *ebus, bool enable)
{
	struct hdac_bus *bus = &ebus->bus;

	if (!bus->ppcap) {
		dev_err(bus->dev, "Address of PP capability is NULL\n");
		return;
	}

	if (enable)
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL, 0, AZX_PPCTL_PIE);
	else
		snd_hdac_updatel(bus->ppcap, AZX_REG_PP_PPCTL, AZX_PPCTL_PIE, 0);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_ppcap_int_enable);

/*
 * Multilink helpers - these helpers are useful for dealing with HDA
 * new multilink capability
 */

/**
 * snd_hdac_ext_bus_get_ml_capabilities - get multilink capability
 * @ebus: HD-audio extended core bus
 *
 * This will parse all links and read the mlink capabilities and add them
 * in hlink_list of extended hdac bus
 * Note: this will be freed on bus exit by driver
 */
int snd_hdac_ext_bus_get_ml_capabilities(struct hdac_ext_bus *ebus)
{
	int idx;
	u32 link_count;
	struct hdac_ext_link *hlink;
	struct hdac_bus *bus = &ebus->bus;

	link_count = readl(bus->mlcap + AZX_REG_ML_MLCD) + 1;

	dev_dbg(bus->dev, "In %s Link count: %d\n", __func__, link_count);

	for (idx = 0; idx < link_count; idx++) {
		hlink  = kzalloc(sizeof(*hlink), GFP_KERNEL);
		if (!hlink)
			return -ENOMEM;
		hlink->index = idx;
		hlink->bus = bus;
		hlink->ml_addr = bus->mlcap + AZX_ML_BASE +
					(AZX_ML_INTERVAL * idx);
		hlink->lcaps  = readl(hlink->ml_addr + AZX_REG_ML_LCAP);
		hlink->lsdiid = readw(hlink->ml_addr + AZX_REG_ML_LSDIID);

		/* since link in On, update the ref */
		hlink->ref_count = 1;

		list_add_tail(&hlink->list, &ebus->hlink_list);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_ml_capabilities);

/**
 * snd_hdac_link_free_all- free hdac extended link objects
 *
 * @ebus: HD-audio ext core bus
 */

void snd_hdac_link_free_all(struct hdac_ext_bus *ebus)
{
	struct hdac_ext_link *l;

	while (!list_empty(&ebus->hlink_list)) {
		l = list_first_entry(&ebus->hlink_list, struct hdac_ext_link, list);
		list_del(&l->list);
		kfree(l);
	}
}
EXPORT_SYMBOL_GPL(snd_hdac_link_free_all);

/**
 * snd_hdac_ext_bus_get_link_index - get link based on codec name
 * @ebus: HD-audio extended core bus
 * @codec_name: codec name
 */
struct hdac_ext_link *snd_hdac_ext_bus_get_link(struct hdac_ext_bus *ebus,
						 const char *codec_name)
{
	int i;
	struct hdac_ext_link *hlink = NULL;
	int bus_idx, addr;

	if (sscanf(codec_name, "ehdaudio%dD%d", &bus_idx, &addr) != 2)
		return NULL;
	if (ebus->idx != bus_idx)
		return NULL;

	list_for_each_entry(hlink, &ebus->hlink_list, list) {
		for (i = 0; i < HDA_MAX_CODECS; i++) {
			if (hlink->lsdiid & (0x1 << addr))
				return hlink;
		}
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_get_link);

static int check_hdac_link_power_active(struct hdac_ext_link *link, bool enable)
{
	int timeout;
	u32 val;
	int mask = (1 << AZX_MLCTL_CPA);

	udelay(3);
	timeout = 150;

	do {
		val = readl(link->ml_addr + AZX_REG_ML_LCTL);
		if (enable) {
			if (((val & mask) >> AZX_MLCTL_CPA))
				return 0;
		} else {
			if (!((val & mask) >> AZX_MLCTL_CPA))
				return 0;
		}
		udelay(3);
	} while (--timeout);

	return -EIO;
}

/**
 * snd_hdac_ext_bus_link_power_up -power up hda link
 * @link: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_up(struct hdac_ext_link *link)
{
	snd_hdac_updatel(link->ml_addr, AZX_REG_ML_LCTL, 0, AZX_MLCTL_SPA);

	return check_hdac_link_power_active(link, true);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_up);

/**
 * snd_hdac_ext_bus_link_power_down -power down hda link
 * @link: HD-audio extended link
 */
int snd_hdac_ext_bus_link_power_down(struct hdac_ext_link *link)
{
	snd_hdac_updatel(link->ml_addr, AZX_REG_ML_LCTL, AZX_MLCTL_SPA, 0);

	return check_hdac_link_power_active(link, false);
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_down);

/**
 * snd_hdac_ext_bus_link_power_up_all -power up all hda link
 * @ebus: HD-audio extended bus
 */
int snd_hdac_ext_bus_link_power_up_all(struct hdac_ext_bus *ebus)
{
	struct hdac_ext_link *hlink = NULL;
	int ret;

	list_for_each_entry(hlink, &ebus->hlink_list, list) {
		snd_hdac_updatel(hlink->ml_addr,
				AZX_REG_ML_LCTL, 0, AZX_MLCTL_SPA);
		ret = check_hdac_link_power_active(hlink, true);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_up_all);

/**
 * snd_hdac_ext_bus_link_power_down_all -power down all hda link
 * @ebus: HD-audio extended bus
 */
int snd_hdac_ext_bus_link_power_down_all(struct hdac_ext_bus *ebus)
{
	struct hdac_ext_link *hlink = NULL;
	int ret;

	list_for_each_entry(hlink, &ebus->hlink_list, list) {
		snd_hdac_updatel(hlink->ml_addr, AZX_REG_ML_LCTL, AZX_MLCTL_SPA, 0);
		ret = check_hdac_link_power_active(hlink, false);
		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_power_down_all);

int snd_hdac_ext_bus_link_get(struct hdac_ext_bus *ebus,
				struct hdac_ext_link *link)
{
	int ret = 0;

	mutex_lock(&ebus->lock);

	/*
	 * if we move from 0 to 1, count will be 1 so power up this link
	 * as well, also check the dma status and trigger that
	 */
	if (++link->ref_count == 1) {
		if (!ebus->cmd_dma_state) {
			snd_hdac_bus_init_cmd_io(&ebus->bus);
			ebus->cmd_dma_state = true;
		}

		ret = snd_hdac_ext_bus_link_power_up(link);
	}

	mutex_unlock(&ebus->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_get);

int snd_hdac_ext_bus_link_put(struct hdac_ext_bus *ebus,
				struct hdac_ext_link *link)
{
	int ret = 0;
	struct hdac_ext_link *hlink;
	bool link_up = false;

	mutex_lock(&ebus->lock);

	/*
	 * if we move from 1 to 0, count will be 0
	 * so power down this link as well
	 */
	if (--link->ref_count == 0) {
		ret = snd_hdac_ext_bus_link_power_down(link);

		/*
		 * now check if all links are off, if so turn off
		 * cmd dma as well
		 */
		list_for_each_entry(hlink, &ebus->hlink_list, list) {
			if (hlink->ref_count) {
				link_up = true;
				break;
			}
		}

		if (!link_up) {
			snd_hdac_bus_stop_cmd_io(&ebus->bus);
			ebus->cmd_dma_state = false;
		}
	}

	mutex_unlock(&ebus->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hdac_ext_bus_link_put);
