/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <core/option.h>

#include <subdev/i2c.h>
#include <subdev/mxm.h>
#include <subdev/bios.h>
#include <subdev/bios/mxm.h>

#include "mxms.h"

static bool
mxm_shadow_rom_fetch(struct nouveau_i2c_port *i2c, u8 addr,
		     u8 offset, u8 size, u8 *data)
{
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &offset },
		{ .addr = addr, .flags = I2C_M_RD, .len = size, .buf = data, },
	};

	return i2c_transfer(&i2c->adapter, msgs, 2) == 2;
}

static bool
mxm_shadow_rom(struct nouveau_mxm *mxm, u8 version)
{
	struct nouveau_bios *bios = nouveau_bios(mxm);
	struct nouveau_i2c *i2c = nouveau_i2c(mxm);
	struct nouveau_i2c_port *port = NULL;
	u8 i2cidx, mxms[6], addr, size;

	i2cidx = mxm_ddc_map(bios, 1 /* LVDS_DDC */) & 0x0f;
	if (i2cidx < 0x0f)
		port = i2c->find(i2c, i2cidx);
	if (!port)
		return false;

	addr = 0x54;
	if (!mxm_shadow_rom_fetch(port, addr, 0, 6, mxms)) {
		addr = 0x56;
		if (!mxm_shadow_rom_fetch(port, addr, 0, 6, mxms))
			return false;
	}

	mxm->mxms = mxms;
	size = mxms_headerlen(mxm) + mxms_structlen(mxm);
	mxm->mxms = kmalloc(size, GFP_KERNEL);

	if (mxm->mxms &&
	    mxm_shadow_rom_fetch(port, addr, 0, size, mxm->mxms))
		return true;

	kfree(mxm->mxms);
	mxm->mxms = NULL;
	return false;
}

#if defined(CONFIG_ACPI)
static bool
mxm_shadow_dsm(struct nouveau_mxm *mxm, u8 version)
{
	struct nouveau_device *device = nv_device(mxm);
	static char muid[] = {
		0x00, 0xA4, 0x04, 0x40, 0x7D, 0x91, 0xF2, 0x4C,
		0xB8, 0x9C, 0x79, 0xB6, 0x2F, 0xD5, 0x56, 0x65
	};
	u32 mxms_args[] = { 0x00000000 };
	union acpi_object args[4] = {
		/* _DSM MUID */
		{ .buffer.type = 3,
		  .buffer.length = sizeof(muid),
		  .buffer.pointer = muid,
		},
		/* spec says this can be zero to mean "highest revision", but
		 * of course there's at least one bios out there which fails
		 * unless you pass in exactly the version it supports..
		 */
		{ .integer.type = ACPI_TYPE_INTEGER,
		  .integer.value = (version & 0xf0) << 4 | (version & 0x0f),
		},
		/* MXMS function */
		{ .integer.type = ACPI_TYPE_INTEGER,
		  .integer.value = 0x00000010,
		},
		/* Pointer to MXMS arguments */
		{ .buffer.type = ACPI_TYPE_BUFFER,
		  .buffer.length = sizeof(mxms_args),
		  .buffer.pointer = (char *)mxms_args,
		},
	};
	struct acpi_object_list list = { ARRAY_SIZE(args), args };
	struct acpi_buffer retn = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_handle handle;
	int ret;

	handle = ACPI_HANDLE(&device->pdev->dev);
	if (!handle)
		return false;

	ret = acpi_evaluate_object(handle, "_DSM", &list, &retn);
	if (ret) {
		nv_debug(mxm, "DSM MXMS failed: %d\n", ret);
		return false;
	}

	obj = retn.pointer;
	if (obj->type == ACPI_TYPE_BUFFER) {
		mxm->mxms = kmemdup(obj->buffer.pointer,
					 obj->buffer.length, GFP_KERNEL);
	} else
	if (obj->type == ACPI_TYPE_INTEGER) {
		nv_debug(mxm, "DSM MXMS returned 0x%llx\n", obj->integer.value);
	}

	kfree(obj);
	return mxm->mxms != NULL;
}
#endif

#if defined(CONFIG_ACPI_WMI) || defined(CONFIG_ACPI_WMI_MODULE)

#define WMI_WMMX_GUID "F6CB5C3C-9CAE-4EBD-B577-931EA32A2CC0"

static u8
wmi_wmmx_mxmi(struct nouveau_mxm *mxm, u8 version)
{
	u32 mxmi_args[] = { 0x494D584D /* MXMI */, version, 0 };
	struct acpi_buffer args = { sizeof(mxmi_args), mxmi_args };
	struct acpi_buffer retn = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_evaluate_method(WMI_WMMX_GUID, 0, 0, &args, &retn);
	if (ACPI_FAILURE(status)) {
		nv_debug(mxm, "WMMX MXMI returned %d\n", status);
		return 0x00;
	}

	obj = retn.pointer;
	if (obj->type == ACPI_TYPE_INTEGER) {
		version = obj->integer.value;
		nv_debug(mxm, "WMMX MXMI version %d.%d\n",
			     (version >> 4), version & 0x0f);
	} else {
		version = 0;
		nv_debug(mxm, "WMMX MXMI returned non-integer\n");
	}

	kfree(obj);
	return version;
}

static bool
mxm_shadow_wmi(struct nouveau_mxm *mxm, u8 version)
{
	u32 mxms_args[] = { 0x534D584D /* MXMS */, version, 0 };
	struct acpi_buffer args = { sizeof(mxms_args), mxms_args };
	struct acpi_buffer retn = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	if (!wmi_has_guid(WMI_WMMX_GUID)) {
		nv_debug(mxm, "WMMX GUID not found\n");
		return false;
	}

	mxms_args[1] = wmi_wmmx_mxmi(mxm, 0x00);
	if (!mxms_args[1])
		mxms_args[1] = wmi_wmmx_mxmi(mxm, version);
	if (!mxms_args[1])
		return false;

	status = wmi_evaluate_method(WMI_WMMX_GUID, 0, 0, &args, &retn);
	if (ACPI_FAILURE(status)) {
		nv_debug(mxm, "WMMX MXMS returned %d\n", status);
		return false;
	}

	obj = retn.pointer;
	if (obj->type == ACPI_TYPE_BUFFER) {
		mxm->mxms = kmemdup(obj->buffer.pointer,
					 obj->buffer.length, GFP_KERNEL);
	}

	kfree(obj);
	return mxm->mxms != NULL;
}
#endif

static struct mxm_shadow_h {
	const char *name;
	bool (*exec)(struct nouveau_mxm *, u8 version);
} _mxm_shadow[] = {
	{ "ROM", mxm_shadow_rom },
#if defined(CONFIG_ACPI)
	{ "DSM", mxm_shadow_dsm },
#endif
#if defined(CONFIG_ACPI_WMI) || defined(CONFIG_ACPI_WMI_MODULE)
	{ "WMI", mxm_shadow_wmi },
#endif
	{}
};

static int
mxm_shadow(struct nouveau_mxm *mxm, u8 version)
{
	struct mxm_shadow_h *shadow = _mxm_shadow;
	do {
		nv_debug(mxm, "checking %s\n", shadow->name);
		if (shadow->exec(mxm, version)) {
			if (mxms_valid(mxm))
				return 0;
			kfree(mxm->mxms);
			mxm->mxms = NULL;
		}
	} while ((++shadow)->name);
	return -ENOENT;
}

int
nouveau_mxm_create_(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, int length, void **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_bios *bios = nouveau_bios(device);
	struct nouveau_mxm *mxm;
	u8  ver, len;
	u16 data;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "MXM", "mxm",
				     length, pobject);
	mxm = *pobject;
	if (ret)
		return ret;

	data = mxm_table(bios, &ver, &len);
	if (!data || !(ver = nv_ro08(bios, data))) {
		nv_debug(mxm, "no VBIOS data, nothing to do\n");
		return 0;
	}

	nv_info(mxm, "BIOS version %d.%d\n", ver >> 4, ver & 0x0f);

	if (mxm_shadow(mxm, ver)) {
		nv_info(mxm, "failed to locate valid SIS\n");
#if 0
		/* we should, perhaps, fall back to some kind of limited
		 * mode here if the x86 vbios hasn't already done the
		 * work for us (so we prevent loading with completely
		 * whacked vbios tables).
		 */
		return -EINVAL;
#else
		return 0;
#endif
	}

	nv_info(mxm, "MXMS Version %d.%d\n",
		mxms_version(mxm) >> 8, mxms_version(mxm) & 0xff);
	mxms_foreach(mxm, 0, NULL, NULL);

	if (nouveau_boolopt(device->cfgopt, "NvMXMDCB", true))
		mxm->action |= MXM_SANITISE_DCB;
	return 0;
}
