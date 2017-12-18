/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <core/device.h>

/**
 * nvkm_firmware_get - load firmware from the official nvidia/chip/ directory
 * @device	device that will use that firmware
 * @fwname	name of firmware file to load
 * @fw		firmware structure to load to
 *
 * Use this function to load firmware files in the form nvidia/chip/fwname.bin.
 * Firmware files released by NVIDIA will always follow this format.
 */
int
nvkm_firmware_get(struct nvkm_device *device, const char *fwname,
		  const struct firmware **fw)
{
	char f[64];
	char cname[16];
	int i;

	/* Convert device name to lowercase */
	strncpy(cname, device->chip->name, sizeof(cname));
	cname[sizeof(cname) - 1] = '\0';
	i = strlen(cname);
	while (i) {
		--i;
		cname[i] = tolower(cname[i]);
	}

	snprintf(f, sizeof(f), "nvidia/%s/%s.bin", cname, fwname);
	return request_firmware(fw, f, device->dev);
}

/**
 * nvkm_firmware_put - release firmware loaded with nvkm_firmware_get
 */
void
nvkm_firmware_put(const struct firmware *fw)
{
	release_firmware(fw);
}
