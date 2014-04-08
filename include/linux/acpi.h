/*
 * acpi.h - ACPI Interface
 *
 * Copyright (C) 2001 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#ifndef _LINUX_ACPI_H
#define _LINUX_ACPI_H

#include <linux/errno.h>
#include <linux/ioport.h>	/* for struct resource */
#include <linux/device.h>

#ifdef	CONFIG_ACPI

#ifndef _LINUX
#define _LINUX
#endif

#include <linux/list.h>
#include <linux/mod_devicetable.h>

#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/acpi_numa.h>
#include <asm/acpi.h>

static inline acpi_handle acpi_device_handle(struct acpi_device *adev)
{
	return adev ? adev->handle : NULL;
}

#define ACPI_COMPANION(dev)		((dev)->acpi_node.companion)
#define ACPI_COMPANION_SET(dev, adev)	ACPI_COMPANION(dev) = (adev)
#define ACPI_HANDLE(dev)		acpi_device_handle(ACPI_COMPANION(dev))

static inline const char *acpi_dev_name(struct acpi_device *adev)
{
	return dev_name(&adev->dev);
}

enum acpi_irq_model_id {
	ACPI_IRQ_MODEL_PIC = 0,
	ACPI_IRQ_MODEL_IOAPIC,
	ACPI_IRQ_MODEL_IOSAPIC,
	ACPI_IRQ_MODEL_PLATFORM,
	ACPI_IRQ_MODEL_COUNT
};

extern enum acpi_irq_model_id	acpi_irq_model;

enum acpi_interrupt_id {
	ACPI_INTERRUPT_PMI	= 1,
	ACPI_INTERRUPT_INIT,
	ACPI_INTERRUPT_CPEI,
	ACPI_INTERRUPT_COUNT
};

#define	ACPI_SPACE_MEM		0

enum acpi_address_range_id {
	ACPI_ADDRESS_RANGE_MEMORY = 1,
	ACPI_ADDRESS_RANGE_RESERVED = 2,
	ACPI_ADDRESS_RANGE_ACPI = 3,
	ACPI_ADDRESS_RANGE_NVS	= 4,
	ACPI_ADDRESS_RANGE_COUNT
};


/* Table Handlers */

typedef int (*acpi_tbl_table_handler)(struct acpi_table_header *table);

typedef int (*acpi_tbl_entry_handler)(struct acpi_subtable_header *header,
				      const unsigned long end);

#ifdef CONFIG_ACPI_INITRD_TABLE_OVERRIDE
void acpi_initrd_override(void *data, size_t size);
#else
static inline void acpi_initrd_override(void *data, size_t size)
{
}
#endif

char * __acpi_map_table (unsigned long phys_addr, unsigned long size);
void __acpi_unmap_table(char *map, unsigned long size);
int early_acpi_boot_init(void);
int acpi_boot_init (void);
void acpi_boot_table_init (void);
int acpi_mps_check (void);
int acpi_numa_init (void);

int acpi_table_init (void);
int acpi_table_parse(char *id, acpi_tbl_table_handler handler);
int __init acpi_table_parse_entries(char *id, unsigned long table_size,
				    int entry_id,
				    acpi_tbl_entry_handler handler,
				    unsigned int max_entries);
int acpi_table_parse_madt(enum acpi_madt_type id,
			  acpi_tbl_entry_handler handler,
			  unsigned int max_entries);
int acpi_parse_mcfg (struct acpi_table_header *header);
void acpi_table_print_madt_entry (struct acpi_subtable_header *madt);

/* the following four functions are architecture-dependent */
void acpi_numa_slit_init (struct acpi_table_slit *slit);
void acpi_numa_processor_affinity_init (struct acpi_srat_cpu_affinity *pa);
void acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa);
int acpi_numa_memory_affinity_init (struct acpi_srat_mem_affinity *ma);
void acpi_numa_arch_fixup(void);

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/* Arch dependent functions for cpu hotplug support */
int acpi_map_lsapic(acpi_handle handle, int physid, int *pcpu);
int acpi_unmap_lsapic(int cpu);
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

int acpi_register_ioapic(acpi_handle handle, u64 phys_addr, u32 gsi_base);
int acpi_unregister_ioapic(acpi_handle handle, u32 gsi_base);
void acpi_irq_stats_init(void);
extern u32 acpi_irq_handled;
extern u32 acpi_irq_not_handled;

extern int sbf_port;
extern unsigned long acpi_realmode_flags;

int acpi_register_gsi (struct device *dev, u32 gsi, int triggering, int polarity);
int acpi_gsi_to_irq (u32 gsi, unsigned int *irq);
int acpi_isa_irq_to_gsi (unsigned isa_irq, u32 *gsi);

#ifdef CONFIG_X86_IO_APIC
extern int acpi_get_override_irq(u32 gsi, int *trigger, int *polarity);
#else
#define acpi_get_override_irq(gsi, trigger, polarity) (-1)
#endif
/*
 * This function undoes the effect of one call to acpi_register_gsi().
 * If this matches the last registration, any IRQ resources for gsi
 * are freed.
 */
void acpi_unregister_gsi (u32 gsi);

struct pci_dev;

int acpi_pci_irq_enable (struct pci_dev *dev);
void acpi_penalize_isa_irq(int irq, int active);

void acpi_pci_irq_disable (struct pci_dev *dev);

extern int ec_read(u8 addr, u8 *val);
extern int ec_write(u8 addr, u8 val);
extern int ec_transaction(u8 command,
                          const u8 *wdata, unsigned wdata_len,
                          u8 *rdata, unsigned rdata_len);
extern acpi_handle ec_get_handle(void);

#if defined(CONFIG_ACPI_WMI) || defined(CONFIG_ACPI_WMI_MODULE)

typedef void (*wmi_notify_handler) (u32 value, void *context);

extern acpi_status wmi_evaluate_method(const char *guid, u8 instance,
					u32 method_id,
					const struct acpi_buffer *in,
					struct acpi_buffer *out);
extern acpi_status wmi_query_block(const char *guid, u8 instance,
					struct acpi_buffer *out);
extern acpi_status wmi_set_block(const char *guid, u8 instance,
					const struct acpi_buffer *in);
extern acpi_status wmi_install_notify_handler(const char *guid,
					wmi_notify_handler handler, void *data);
extern acpi_status wmi_remove_notify_handler(const char *guid);
extern acpi_status wmi_get_event_data(u32 event, struct acpi_buffer *out);
extern bool wmi_has_guid(const char *guid);

#endif	/* CONFIG_ACPI_WMI */

#define ACPI_VIDEO_OUTPUT_SWITCHING			0x0001
#define ACPI_VIDEO_DEVICE_POSTING			0x0002
#define ACPI_VIDEO_ROM_AVAILABLE			0x0004
#define ACPI_VIDEO_BACKLIGHT				0x0008
#define ACPI_VIDEO_BACKLIGHT_FORCE_VENDOR		0x0010
#define ACPI_VIDEO_BACKLIGHT_FORCE_VIDEO		0x0020
#define ACPI_VIDEO_OUTPUT_SWITCHING_FORCE_VENDOR	0x0040
#define ACPI_VIDEO_OUTPUT_SWITCHING_FORCE_VIDEO		0x0080
#define ACPI_VIDEO_BACKLIGHT_DMI_VENDOR			0x0100
#define ACPI_VIDEO_BACKLIGHT_DMI_VIDEO			0x0200
#define ACPI_VIDEO_OUTPUT_SWITCHING_DMI_VENDOR		0x0400
#define ACPI_VIDEO_OUTPUT_SWITCHING_DMI_VIDEO		0x0800

#if defined(CONFIG_ACPI_VIDEO) || defined(CONFIG_ACPI_VIDEO_MODULE)

extern long acpi_video_get_capabilities(acpi_handle graphics_dev_handle);
extern long acpi_is_video_device(acpi_handle handle);
extern void acpi_video_dmi_promote_vendor(void);
extern void acpi_video_dmi_demote_vendor(void);
extern int acpi_video_backlight_support(void);
extern int acpi_video_display_switch_support(void);

#else

static inline long acpi_video_get_capabilities(acpi_handle graphics_dev_handle)
{
	return 0;
}

static inline long acpi_is_video_device(acpi_handle handle)
{
	return 0;
}

static inline void acpi_video_dmi_promote_vendor(void)
{
}

static inline void acpi_video_dmi_demote_vendor(void)
{
}

static inline int acpi_video_backlight_support(void)
{
	return 0;
}

static inline int acpi_video_display_switch_support(void)
{
	return 0;
}

#endif /* defined(CONFIG_ACPI_VIDEO) || defined(CONFIG_ACPI_VIDEO_MODULE) */

extern int acpi_blacklisted(void);
extern void acpi_dmi_osi_linux(int enable, const struct dmi_system_id *d);
extern void acpi_osi_setup(char *str);

#ifdef CONFIG_ACPI_NUMA
int acpi_get_pxm(acpi_handle handle);
int acpi_get_node(acpi_handle *handle);
#else
static inline int acpi_get_pxm(acpi_handle handle)
{
	return 0;
}
static inline int acpi_get_node(acpi_handle *handle)
{
	return 0;
}
#endif
extern int acpi_paddr_to_node(u64 start_addr, u64 size);

extern int pnpacpi_disabled;

#define PXM_INVAL	(-1)

bool acpi_dev_resource_memory(struct acpi_resource *ares, struct resource *res);
bool acpi_dev_resource_io(struct acpi_resource *ares, struct resource *res);
bool acpi_dev_resource_address_space(struct acpi_resource *ares,
				     struct resource *res);
bool acpi_dev_resource_ext_address_space(struct acpi_resource *ares,
					 struct resource *res);
unsigned long acpi_dev_irq_flags(u8 triggering, u8 polarity, u8 shareable);
bool acpi_dev_resource_interrupt(struct acpi_resource *ares, int index,
				 struct resource *res);

struct resource_list_entry {
	struct list_head node;
	struct resource res;
};

void acpi_dev_free_resource_list(struct list_head *list);
int acpi_dev_get_resources(struct acpi_device *adev, struct list_head *list,
			   int (*preproc)(struct acpi_resource *, void *),
			   void *preproc_data);

int acpi_check_resource_conflict(const struct resource *res);

int acpi_check_region(resource_size_t start, resource_size_t n,
		      const char *name);

int acpi_resources_are_enforced(void);

#ifdef CONFIG_HIBERNATION
void __init acpi_no_s4_hw_signature(void);
#endif

#ifdef CONFIG_PM_SLEEP
void __init acpi_old_suspend_ordering(void);
void __init acpi_nvs_nosave(void);
void __init acpi_nvs_nosave_s3(void);
#endif /* CONFIG_PM_SLEEP */

struct acpi_osc_context {
	char *uuid_str;			/* UUID string */
	int rev;
	struct acpi_buffer cap;		/* list of DWORD capabilities */
	struct acpi_buffer ret;		/* free by caller if success */
};

acpi_status acpi_str_to_uuid(char *str, u8 *uuid);
acpi_status acpi_run_osc(acpi_handle handle, struct acpi_osc_context *context);

/* Indexes into _OSC Capabilities Buffer (DWORDs 2 & 3 are device-specific) */
#define OSC_QUERY_DWORD				0	/* DWORD 1 */
#define OSC_SUPPORT_DWORD			1	/* DWORD 2 */
#define OSC_CONTROL_DWORD			2	/* DWORD 3 */

/* _OSC Capabilities DWORD 1: Query/Control and Error Returns (generic) */
#define OSC_QUERY_ENABLE			0x00000001  /* input */
#define OSC_REQUEST_ERROR			0x00000002  /* return */
#define OSC_INVALID_UUID_ERROR			0x00000004  /* return */
#define OSC_INVALID_REVISION_ERROR		0x00000008  /* return */
#define OSC_CAPABILITIES_MASK_ERROR		0x00000010  /* return */

/* Platform-Wide Capabilities _OSC: Capabilities DWORD 2: Support Field */
#define OSC_SB_PAD_SUPPORT			0x00000001
#define OSC_SB_PPC_OST_SUPPORT			0x00000002
#define OSC_SB_PR3_SUPPORT			0x00000004
#define OSC_SB_HOTPLUG_OST_SUPPORT		0x00000008
#define OSC_SB_APEI_SUPPORT			0x00000010
#define OSC_SB_CPC_SUPPORT			0x00000020

extern bool osc_sb_apei_support_acked;

/* PCI Host Bridge _OSC: Capabilities DWORD 2: Support Field */
#define OSC_PCI_EXT_CONFIG_SUPPORT		0x00000001
#define OSC_PCI_ASPM_SUPPORT			0x00000002
#define OSC_PCI_CLOCK_PM_SUPPORT		0x00000004
#define OSC_PCI_SEGMENT_GROUPS_SUPPORT		0x00000008
#define OSC_PCI_MSI_SUPPORT			0x00000010
#define OSC_PCI_SUPPORT_MASKS			0x0000001f

/* PCI Host Bridge _OSC: Capabilities DWORD 3: Control Field */
#define OSC_PCI_EXPRESS_NATIVE_HP_CONTROL	0x00000001
#define OSC_PCI_SHPC_NATIVE_HP_CONTROL		0x00000002
#define OSC_PCI_EXPRESS_PME_CONTROL		0x00000004
#define OSC_PCI_EXPRESS_AER_CONTROL		0x00000008
#define OSC_PCI_EXPRESS_CAPABILITY_CONTROL	0x00000010
#define OSC_PCI_CONTROL_MASKS			0x0000001f

extern acpi_status acpi_pci_osc_control_set(acpi_handle handle,
					     u32 *mask, u32 req);

/* Enable _OST when all relevant hotplug operations are enabled */
#if defined(CONFIG_ACPI_HOTPLUG_CPU) &&			\
	defined(CONFIG_ACPI_HOTPLUG_MEMORY) &&		\
	defined(CONFIG_ACPI_CONTAINER)
#define ACPI_HOTPLUG_OST
#endif

/* _OST Source Event Code (OSPM Action) */
#define ACPI_OST_EC_OSPM_SHUTDOWN		0x100
#define ACPI_OST_EC_OSPM_EJECT			0x103
#define ACPI_OST_EC_OSPM_INSERTION		0x200

/* _OST General Processing Status Code */
#define ACPI_OST_SC_SUCCESS			0x0
#define ACPI_OST_SC_NON_SPECIFIC_FAILURE	0x1
#define ACPI_OST_SC_UNRECOGNIZED_NOTIFY		0x2

/* _OST OS Shutdown Processing (0x100) Status Code */
#define ACPI_OST_SC_OS_SHUTDOWN_DENIED		0x80
#define ACPI_OST_SC_OS_SHUTDOWN_IN_PROGRESS	0x81
#define ACPI_OST_SC_OS_SHUTDOWN_COMPLETED	0x82
#define ACPI_OST_SC_OS_SHUTDOWN_NOT_SUPPORTED	0x83

/* _OST Ejection Request (0x3, 0x103) Status Code */
#define ACPI_OST_SC_EJECT_NOT_SUPPORTED		0x80
#define ACPI_OST_SC_DEVICE_IN_USE		0x81
#define ACPI_OST_SC_DEVICE_BUSY			0x82
#define ACPI_OST_SC_EJECT_DEPENDENCY_BUSY	0x83
#define ACPI_OST_SC_EJECT_IN_PROGRESS		0x84

/* _OST Insertion Request (0x200) Status Code */
#define ACPI_OST_SC_INSERT_IN_PROGRESS		0x80
#define ACPI_OST_SC_DRIVER_LOAD_FAILURE		0x81
#define ACPI_OST_SC_INSERT_NOT_SUPPORTED	0x82

extern void acpi_early_init(void);

extern int acpi_nvs_register(__u64 start, __u64 size);

extern int acpi_nvs_for_each_region(int (*func)(__u64, __u64, void *),
				    void *data);

const struct acpi_device_id *acpi_match_device(const struct acpi_device_id *ids,
					       const struct device *dev);

static inline bool acpi_driver_match_device(struct device *dev,
					    const struct device_driver *drv)
{
	return !!acpi_match_device(drv->acpi_match_table, dev);
}

#define ACPI_PTR(_ptr)	(_ptr)

#else	/* !CONFIG_ACPI */

#define acpi_disabled 1

#define ACPI_COMPANION(dev)		(NULL)
#define ACPI_COMPANION_SET(dev, adev)	do { } while (0)
#define ACPI_HANDLE(dev)		(NULL)

static inline const char *acpi_dev_name(struct acpi_device *adev)
{
	return NULL;
}

static inline void acpi_early_init(void) { }

static inline int early_acpi_boot_init(void)
{
	return 0;
}
static inline int acpi_boot_init(void)
{
	return 0;
}

static inline void acpi_boot_table_init(void)
{
	return;
}

static inline int acpi_mps_check(void)
{
	return 0;
}

static inline int acpi_check_resource_conflict(struct resource *res)
{
	return 0;
}

static inline int acpi_check_region(resource_size_t start, resource_size_t n,
				    const char *name)
{
	return 0;
}

struct acpi_table_header;
static inline int acpi_table_parse(char *id,
				int (*handler)(struct acpi_table_header *))
{
	return -1;
}

static inline int acpi_nvs_register(__u64 start, __u64 size)
{
	return 0;
}

static inline int acpi_nvs_for_each_region(int (*func)(__u64, __u64, void *),
					   void *data)
{
	return 0;
}

struct acpi_device_id;

static inline const struct acpi_device_id *acpi_match_device(
	const struct acpi_device_id *ids, const struct device *dev)
{
	return NULL;
}

static inline bool acpi_driver_match_device(struct device *dev,
					    const struct device_driver *drv)
{
	return false;
}

#define ACPI_PTR(_ptr)	(NULL)

#endif	/* !CONFIG_ACPI */

#ifdef CONFIG_ACPI
void acpi_os_set_prepare_sleep(int (*func)(u8 sleep_state,
			       u32 pm1a_ctrl,  u32 pm1b_ctrl));

acpi_status acpi_os_prepare_sleep(u8 sleep_state,
				  u32 pm1a_control, u32 pm1b_control);

void acpi_os_set_prepare_extended_sleep(int (*func)(u8 sleep_state,
				        u32 val_a,  u32 val_b));

acpi_status acpi_os_prepare_extended_sleep(u8 sleep_state,
					   u32 val_a, u32 val_b);

#ifdef CONFIG_X86
void arch_reserve_mem_area(acpi_physical_address addr, size_t size);
#else
static inline void arch_reserve_mem_area(acpi_physical_address addr,
					  size_t size)
{
}
#endif /* CONFIG_X86 */
#else
#define acpi_os_set_prepare_sleep(func, pm1a_ctrl, pm1b_ctrl) do { } while (0)
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_PM_RUNTIME)
int acpi_dev_runtime_suspend(struct device *dev);
int acpi_dev_runtime_resume(struct device *dev);
int acpi_subsys_runtime_suspend(struct device *dev);
int acpi_subsys_runtime_resume(struct device *dev);
#else
static inline int acpi_dev_runtime_suspend(struct device *dev) { return 0; }
static inline int acpi_dev_runtime_resume(struct device *dev) { return 0; }
static inline int acpi_subsys_runtime_suspend(struct device *dev) { return 0; }
static inline int acpi_subsys_runtime_resume(struct device *dev) { return 0; }
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_PM_SLEEP)
int acpi_dev_suspend_late(struct device *dev);
int acpi_dev_resume_early(struct device *dev);
int acpi_subsys_prepare(struct device *dev);
int acpi_subsys_suspend_late(struct device *dev);
int acpi_subsys_resume_early(struct device *dev);
#else
static inline int acpi_dev_suspend_late(struct device *dev) { return 0; }
static inline int acpi_dev_resume_early(struct device *dev) { return 0; }
static inline int acpi_subsys_prepare(struct device *dev) { return 0; }
static inline int acpi_subsys_suspend_late(struct device *dev) { return 0; }
static inline int acpi_subsys_resume_early(struct device *dev) { return 0; }
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_PM)
struct acpi_device *acpi_dev_pm_get_node(struct device *dev);
int acpi_dev_pm_attach(struct device *dev, bool power_on);
void acpi_dev_pm_detach(struct device *dev, bool power_off);
#else
static inline struct acpi_device *acpi_dev_pm_get_node(struct device *dev)
{
	return NULL;
}
static inline int acpi_dev_pm_attach(struct device *dev, bool power_on)
{
	return -ENODEV;
}
static inline void acpi_dev_pm_detach(struct device *dev, bool power_off) {}
#endif

#ifdef CONFIG_ACPI
__printf(3, 4)
void acpi_handle_printk(const char *level, acpi_handle handle,
			const char *fmt, ...);
#else	/* !CONFIG_ACPI */
static inline __printf(3, 4) void
acpi_handle_printk(const char *level, void *handle, const char *fmt, ...) {}
#endif	/* !CONFIG_ACPI */

/*
 * acpi_handle_<level>: Print message with ACPI prefix and object path
 *
 * These interfaces acquire the global namespace mutex to obtain an object
 * path.  In interrupt context, it shows the object path as <n/a>.
 */
#define acpi_handle_emerg(handle, fmt, ...)				\
	acpi_handle_printk(KERN_EMERG, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_alert(handle, fmt, ...)				\
	acpi_handle_printk(KERN_ALERT, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_crit(handle, fmt, ...)				\
	acpi_handle_printk(KERN_CRIT, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_err(handle, fmt, ...)				\
	acpi_handle_printk(KERN_ERR, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_warn(handle, fmt, ...)				\
	acpi_handle_printk(KERN_WARNING, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_notice(handle, fmt, ...)				\
	acpi_handle_printk(KERN_NOTICE, handle, fmt, ##__VA_ARGS__)
#define acpi_handle_info(handle, fmt, ...)				\
	acpi_handle_printk(KERN_INFO, handle, fmt, ##__VA_ARGS__)

/* REVISIT: Support CONFIG_DYNAMIC_DEBUG when necessary */
#if defined(DEBUG) || defined(CONFIG_DYNAMIC_DEBUG)
#define acpi_handle_debug(handle, fmt, ...)				\
	acpi_handle_printk(KERN_DEBUG, handle, fmt, ##__VA_ARGS__)
#else
#define acpi_handle_debug(handle, fmt, ...)				\
({									\
	if (0)								\
		acpi_handle_printk(KERN_DEBUG, handle, fmt, ##__VA_ARGS__); \
	0;								\
})
#endif

#endif	/*_LINUX_ACPI_H*/
