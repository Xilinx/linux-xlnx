#include <linux/pci.h>
#include <linux/acpi.h>
#include <linux/slab.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_bus.h>
#include <acpi/video.h>
#include <acpi/acpi.h>
#include <linux/mxm-wmi.h>

#include <linux/vga_switcheroo.h>

#include <drm/drm_edid.h>

#include "nouveau_drm.h"
#include "nouveau_acpi.h"

#define NOUVEAU_DSM_LED 0x02
#define NOUVEAU_DSM_LED_STATE 0x00
#define NOUVEAU_DSM_LED_OFF 0x10
#define NOUVEAU_DSM_LED_STAMINA 0x11
#define NOUVEAU_DSM_LED_SPEED 0x12

#define NOUVEAU_DSM_POWER 0x03
#define NOUVEAU_DSM_POWER_STATE 0x00
#define NOUVEAU_DSM_POWER_SPEED 0x01
#define NOUVEAU_DSM_POWER_STAMINA 0x02

#define NOUVEAU_DSM_OPTIMUS_CAPS 0x1A
#define NOUVEAU_DSM_OPTIMUS_FLAGS 0x1B

#define NOUVEAU_DSM_OPTIMUS_POWERDOWN_PS3 (3 << 24)
#define NOUVEAU_DSM_OPTIMUS_NO_POWERDOWN_PS3 (2 << 24)
#define NOUVEAU_DSM_OPTIMUS_FLAGS_CHANGED (1)

#define NOUVEAU_DSM_OPTIMUS_SET_POWERDOWN (NOUVEAU_DSM_OPTIMUS_POWERDOWN_PS3 | NOUVEAU_DSM_OPTIMUS_FLAGS_CHANGED)

/* result of the optimus caps function */
#define OPTIMUS_ENABLED (1 << 0)
#define OPTIMUS_STATUS_MASK (3 << 3)
#define OPTIMUS_STATUS_OFF  (0 << 3)
#define OPTIMUS_STATUS_ON_ENABLED  (1 << 3)
#define OPTIMUS_STATUS_PWR_STABLE  (3 << 3)
#define OPTIMUS_DISPLAY_HOTPLUG (1 << 6)
#define OPTIMUS_CAPS_MASK (7 << 24)
#define OPTIMUS_DYNAMIC_PWR_CAP (1 << 24)

#define OPTIMUS_AUDIO_CAPS_MASK (3 << 27)
#define OPTIMUS_HDA_CODEC_MASK (2 << 27) /* hda bios control */

static struct nouveau_dsm_priv {
	bool dsm_detected;
	bool optimus_detected;
	acpi_handle dhandle;
	acpi_handle other_handle;
	acpi_handle rom_handle;
} nouveau_dsm_priv;

bool nouveau_is_optimus(void) {
	return nouveau_dsm_priv.optimus_detected;
}

bool nouveau_is_v1_dsm(void) {
	return nouveau_dsm_priv.dsm_detected;
}

#define NOUVEAU_DSM_HAS_MUX 0x1
#define NOUVEAU_DSM_HAS_OPT 0x2

static const char nouveau_dsm_muid[] = {
	0xA0, 0xA0, 0x95, 0x9D, 0x60, 0x00, 0x48, 0x4D,
	0xB3, 0x4D, 0x7E, 0x5F, 0xEA, 0x12, 0x9F, 0xD4,
};

static const char nouveau_op_dsm_muid[] = {
	0xF8, 0xD8, 0x86, 0xA4, 0xDA, 0x0B, 0x1B, 0x47,
	0xA7, 0x2B, 0x60, 0x42, 0xA6, 0xB5, 0xBE, 0xE0,
};

static int nouveau_optimus_dsm(acpi_handle handle, int func, int arg, uint32_t *result)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	int i, err;
	char args_buff[4];

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(nouveau_op_dsm_muid);
	params[0].buffer.pointer = (char *)nouveau_op_dsm_muid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = 0x00000100;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_BUFFER;
	params[3].buffer.length = 4;
	/* ACPI is little endian, AABBCCDD becomes {DD,CC,BB,AA} */
	for (i = 0; i < 4; i++)
		args_buff[i] = (arg >> i * 8) & 0xFF;
	params[3].buffer.pointer = args_buff;

	err = acpi_evaluate_object(handle, "_DSM", &input, &output);
	if (err) {
		printk(KERN_INFO "failed to evaluate _DSM: %d\n", err);
		return err;
	}

	obj = (union acpi_object *)output.pointer;

	if (obj->type == ACPI_TYPE_INTEGER)
		if (obj->integer.value == 0x80000002) {
			return -ENODEV;
		}

	if (obj->type == ACPI_TYPE_BUFFER) {
		if (obj->buffer.length == 4 && result) {
			*result = 0;
			*result |= obj->buffer.pointer[0];
			*result |= (obj->buffer.pointer[1] << 8);
			*result |= (obj->buffer.pointer[2] << 16);
			*result |= (obj->buffer.pointer[3] << 24);
		}
	}

	kfree(output.pointer);
	return 0;
}

static int nouveau_dsm(acpi_handle handle, int func, int arg, uint32_t *result)
{
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_object_list input;
	union acpi_object params[4];
	union acpi_object *obj;
	int err;

	input.count = 4;
	input.pointer = params;
	params[0].type = ACPI_TYPE_BUFFER;
	params[0].buffer.length = sizeof(nouveau_dsm_muid);
	params[0].buffer.pointer = (char *)nouveau_dsm_muid;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = 0x00000102;
	params[2].type = ACPI_TYPE_INTEGER;
	params[2].integer.value = func;
	params[3].type = ACPI_TYPE_INTEGER;
	params[3].integer.value = arg;

	err = acpi_evaluate_object(handle, "_DSM", &input, &output);
	if (err) {
		printk(KERN_INFO "failed to evaluate _DSM: %d\n", err);
		return err;
	}

	obj = (union acpi_object *)output.pointer;

	if (obj->type == ACPI_TYPE_INTEGER)
		if (obj->integer.value == 0x80000002)
			return -ENODEV;

	if (obj->type == ACPI_TYPE_BUFFER) {
		if (obj->buffer.length == 4 && result) {
			*result = 0;
			*result |= obj->buffer.pointer[0];
			*result |= (obj->buffer.pointer[1] << 8);
			*result |= (obj->buffer.pointer[2] << 16);
			*result |= (obj->buffer.pointer[3] << 24);
		}
	}

	kfree(output.pointer);
	return 0;
}

/* Returns 1 if a DSM function is usable and 0 otherwise */
static int nouveau_test_dsm(acpi_handle test_handle,
	int (*dsm_func)(acpi_handle, int, int, uint32_t *),
	int sfnc)
{
	u32 result = 0;

	/* Function 0 returns a Buffer containing available functions. The args
	 * parameter is ignored for function 0, so just put 0 in it */
	if (dsm_func(test_handle, 0, 0, &result))
		return 0;

	/* ACPI Spec v4 9.14.1: if bit 0 is zero, no function is supported. If
	 * the n-th bit is enabled, function n is supported */
	return result & 1 && result & (1 << sfnc);
}

static int nouveau_dsm_switch_mux(acpi_handle handle, int mux_id)
{
	mxm_wmi_call_mxmx(mux_id == NOUVEAU_DSM_LED_STAMINA ? MXM_MXDS_ADAPTER_IGD : MXM_MXDS_ADAPTER_0);
	mxm_wmi_call_mxds(mux_id == NOUVEAU_DSM_LED_STAMINA ? MXM_MXDS_ADAPTER_IGD : MXM_MXDS_ADAPTER_0);
	return nouveau_dsm(handle, NOUVEAU_DSM_LED, mux_id, NULL);
}

static int nouveau_dsm_set_discrete_state(acpi_handle handle, enum vga_switcheroo_state state)
{
	int arg;
	if (state == VGA_SWITCHEROO_ON)
		arg = NOUVEAU_DSM_POWER_SPEED;
	else
		arg = NOUVEAU_DSM_POWER_STAMINA;
	nouveau_dsm(handle, NOUVEAU_DSM_POWER, arg, NULL);
	return 0;
}

static int nouveau_dsm_switchto(enum vga_switcheroo_client_id id)
{
	if (!nouveau_dsm_priv.dsm_detected)
		return 0;
	if (id == VGA_SWITCHEROO_IGD)
		return nouveau_dsm_switch_mux(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_LED_STAMINA);
	else
		return nouveau_dsm_switch_mux(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_LED_SPEED);
}

static int nouveau_dsm_power_state(enum vga_switcheroo_client_id id,
				   enum vga_switcheroo_state state)
{
	if (id == VGA_SWITCHEROO_IGD)
		return 0;

	/* Optimus laptops have the card already disabled in
	 * nouveau_switcheroo_set_state */
	if (!nouveau_dsm_priv.dsm_detected)
		return 0;

	return nouveau_dsm_set_discrete_state(nouveau_dsm_priv.dhandle, state);
}

static int nouveau_dsm_get_client_id(struct pci_dev *pdev)
{
	/* easy option one - intel vendor ID means Integrated */
	if (pdev->vendor == PCI_VENDOR_ID_INTEL)
		return VGA_SWITCHEROO_IGD;

	/* is this device on Bus 0? - this may need improving */
	if (pdev->bus->number == 0)
		return VGA_SWITCHEROO_IGD;

	return VGA_SWITCHEROO_DIS;
}

static struct vga_switcheroo_handler nouveau_dsm_handler = {
	.switchto = nouveau_dsm_switchto,
	.power_state = nouveau_dsm_power_state,
	.get_client_id = nouveau_dsm_get_client_id,
};

static int nouveau_dsm_pci_probe(struct pci_dev *pdev)
{
	acpi_handle dhandle;
	int retval = 0;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	if (!acpi_has_method(dhandle, "_DSM")) {
		nouveau_dsm_priv.other_handle = dhandle;
		return false;
	}
	if (nouveau_test_dsm(dhandle, nouveau_dsm, NOUVEAU_DSM_POWER))
		retval |= NOUVEAU_DSM_HAS_MUX;

	if (nouveau_test_dsm(dhandle, nouveau_optimus_dsm,
		NOUVEAU_DSM_OPTIMUS_CAPS))
		retval |= NOUVEAU_DSM_HAS_OPT;

	if (retval & NOUVEAU_DSM_HAS_OPT) {
		uint32_t result;
		nouveau_optimus_dsm(dhandle, NOUVEAU_DSM_OPTIMUS_CAPS, 0,
				    &result);
		dev_info(&pdev->dev, "optimus capabilities: %s, status %s%s\n",
			 (result & OPTIMUS_ENABLED) ? "enabled" : "disabled",
			 (result & OPTIMUS_DYNAMIC_PWR_CAP) ? "dynamic power, " : "",
			 (result & OPTIMUS_HDA_CODEC_MASK) ? "hda bios codec supported" : "");
	}
	if (retval)
		nouveau_dsm_priv.dhandle = dhandle;

	return retval;
}

static bool nouveau_dsm_detect(void)
{
	char acpi_method_name[255] = { 0 };
	struct acpi_buffer buffer = {sizeof(acpi_method_name), acpi_method_name};
	struct pci_dev *pdev = NULL;
	int has_dsm = 0;
	int has_optimus = 0;
	int vga_count = 0;
	bool guid_valid;
	int retval;
	bool ret = false;

	/* lookup the MXM GUID */
	guid_valid = mxm_wmi_supported();

	if (guid_valid)
		printk("MXM: GUID detected in BIOS\n");

	/* now do DSM detection */
	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_VGA << 8, pdev)) != NULL) {
		vga_count++;

		retval = nouveau_dsm_pci_probe(pdev);
		if (retval & NOUVEAU_DSM_HAS_MUX)
			has_dsm |= 1;
		if (retval & NOUVEAU_DSM_HAS_OPT)
			has_optimus = 1;
	}

	while ((pdev = pci_get_class(PCI_CLASS_DISPLAY_3D << 8, pdev)) != NULL) {
		vga_count++;

		retval = nouveau_dsm_pci_probe(pdev);
		if (retval & NOUVEAU_DSM_HAS_MUX)
			has_dsm |= 1;
		if (retval & NOUVEAU_DSM_HAS_OPT)
			has_optimus = 1;
	}

	/* find the optimus DSM or the old v1 DSM */
	if (has_optimus == 1) {
		acpi_get_name(nouveau_dsm_priv.dhandle, ACPI_FULL_PATHNAME,
			&buffer);
		printk(KERN_INFO "VGA switcheroo: detected Optimus DSM method %s handle\n",
			acpi_method_name);
		nouveau_dsm_priv.optimus_detected = true;
		ret = true;
	} else if (vga_count == 2 && has_dsm && guid_valid) {
		acpi_get_name(nouveau_dsm_priv.dhandle, ACPI_FULL_PATHNAME,
			&buffer);
		printk(KERN_INFO "VGA switcheroo: detected DSM switching method %s handle\n",
			acpi_method_name);
		nouveau_dsm_priv.dsm_detected = true;
		/*
		 * On some systems hotplug events are generated for the device
		 * being switched off when _DSM is executed.  They cause ACPI
		 * hotplug to trigger and attempt to remove the device from
		 * the system, which causes it to break down.  Prevent that from
		 * happening by setting the no_hotplug flag for the involved
		 * ACPI device objects.
		 */
		acpi_bus_no_hotplug(nouveau_dsm_priv.dhandle);
		acpi_bus_no_hotplug(nouveau_dsm_priv.other_handle);
		ret = true;
	}


	return ret;
}

void nouveau_register_dsm_handler(void)
{
	bool r;

	r = nouveau_dsm_detect();
	if (!r)
		return;

	vga_switcheroo_register_handler(&nouveau_dsm_handler);
}

/* Must be called for Optimus models before the card can be turned off */
void nouveau_switcheroo_optimus_dsm(void)
{
	u32 result = 0;
	if (!nouveau_dsm_priv.optimus_detected)
		return;

	nouveau_optimus_dsm(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_OPTIMUS_FLAGS,
			    0x3, &result);

	nouveau_optimus_dsm(nouveau_dsm_priv.dhandle, NOUVEAU_DSM_OPTIMUS_CAPS,
		NOUVEAU_DSM_OPTIMUS_SET_POWERDOWN, &result);

}

void nouveau_unregister_dsm_handler(void)
{
	if (nouveau_dsm_priv.optimus_detected || nouveau_dsm_priv.dsm_detected)
		vga_switcheroo_unregister_handler();
}

/* retrieve the ROM in 4k blocks */
static int nouveau_rom_call(acpi_handle rom_handle, uint8_t *bios,
			    int offset, int len)
{
	acpi_status status;
	union acpi_object rom_arg_elements[2], *obj;
	struct acpi_object_list rom_arg;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL};

	rom_arg.count = 2;
	rom_arg.pointer = &rom_arg_elements[0];

	rom_arg_elements[0].type = ACPI_TYPE_INTEGER;
	rom_arg_elements[0].integer.value = offset;

	rom_arg_elements[1].type = ACPI_TYPE_INTEGER;
	rom_arg_elements[1].integer.value = len;

	status = acpi_evaluate_object(rom_handle, NULL, &rom_arg, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_INFO "failed to evaluate ROM got %s\n", acpi_format_exception(status));
		return -ENODEV;
	}
	obj = (union acpi_object *)buffer.pointer;
	memcpy(bios+offset, obj->buffer.pointer, len);
	kfree(buffer.pointer);
	return len;
}

bool nouveau_acpi_rom_supported(struct pci_dev *pdev)
{
	acpi_status status;
	acpi_handle dhandle, rom_handle;

	if (!nouveau_dsm_priv.dsm_detected && !nouveau_dsm_priv.optimus_detected)
		return false;

	dhandle = ACPI_HANDLE(&pdev->dev);
	if (!dhandle)
		return false;

	status = acpi_get_handle(dhandle, "_ROM", &rom_handle);
	if (ACPI_FAILURE(status))
		return false;

	nouveau_dsm_priv.rom_handle = rom_handle;
	return true;
}

int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len)
{
	return nouveau_rom_call(nouveau_dsm_priv.rom_handle, bios, offset, len);
}

void *
nouveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector)
{
	struct acpi_device *acpidev;
	acpi_handle handle;
	int type, ret;
	void *edid;

	switch (connector->connector_type) {
	case DRM_MODE_CONNECTOR_LVDS:
	case DRM_MODE_CONNECTOR_eDP:
		type = ACPI_VIDEO_DISPLAY_LCD;
		break;
	default:
		return NULL;
	}

	handle = ACPI_HANDLE(&dev->pdev->dev);
	if (!handle)
		return NULL;

	ret = acpi_bus_get_device(handle, &acpidev);
	if (ret)
		return NULL;

	ret = acpi_video_get_edid(acpidev, type, -1, &edid);
	if (ret < 0)
		return NULL;

	return kmemdup(edid, EDID_LENGTH, GFP_KERNEL);
}
