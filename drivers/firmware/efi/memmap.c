/*
 * Common EFI memory map functions.
 */

#define pr_fmt(fmt) "efi: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/io.h>
#include <asm/early_ioremap.h>

/**
 * __efi_memmap_init - Common code for mapping the EFI memory map
 * @data: EFI memory map data
 * @late: Use early or late mapping function?
 *
 * This function takes care of figuring out which function to use to
 * map the EFI memory map in efi.memmap based on how far into the boot
 * we are.
 *
 * During bootup @late should be %false since we only have access to
 * the early_memremap*() functions as the vmalloc space isn't setup.
 * Once the kernel is fully booted we can fallback to the more robust
 * memremap*() API.
 *
 * Returns zero on success, a negative error code on failure.
 */
static int __init
__efi_memmap_init(struct efi_memory_map_data *data, bool late)
{
	struct efi_memory_map map;
	phys_addr_t phys_map;

	if (efi_enabled(EFI_PARAVIRT))
		return 0;

	phys_map = data->phys_map;

	if (late)
		map.map = memremap(phys_map, data->size, MEMREMAP_WB);
	else
		map.map = early_memremap(phys_map, data->size);

	if (!map.map) {
		pr_err("Could not map the memory map!\n");
		return -ENOMEM;
	}

	map.phys_map = data->phys_map;
	map.nr_map = data->size / data->desc_size;
	map.map_end = map.map + data->size;

	map.desc_version = data->desc_version;
	map.desc_size = data->desc_size;
	map.late = late;

	set_bit(EFI_MEMMAP, &efi.flags);

	efi.memmap = map;

	return 0;
}

/**
 * efi_memmap_init_early - Map the EFI memory map data structure
 * @data: EFI memory map data
 *
 * Use early_memremap() to map the passed in EFI memory map and assign
 * it to efi.memmap.
 */
int __init efi_memmap_init_early(struct efi_memory_map_data *data)
{
	/* Cannot go backwards */
	WARN_ON(efi.memmap.late);

	return __efi_memmap_init(data, false);
}

void __init efi_memmap_unmap(void)
{
	if (!efi.memmap.late) {
		unsigned long size;

		size = efi.memmap.desc_size * efi.memmap.nr_map;
		early_memunmap(efi.memmap.map, size);
	} else {
		memunmap(efi.memmap.map);
	}

	efi.memmap.map = NULL;
	clear_bit(EFI_MEMMAP, &efi.flags);
}

/**
 * efi_memmap_init_late - Map efi.memmap with memremap()
 * @phys_addr: Physical address of the new EFI memory map
 * @size: Size in bytes of the new EFI memory map
 *
 * Setup a mapping of the EFI memory map using ioremap_cache(). This
 * function should only be called once the vmalloc space has been
 * setup and is therefore not suitable for calling during early EFI
 * initialise, e.g. in efi_init(). Additionally, it expects
 * efi_memmap_init_early() to have already been called.
 *
 * The reason there are two EFI memmap initialisation
 * (efi_memmap_init_early() and this late version) is because the
 * early EFI memmap should be explicitly unmapped once EFI
 * initialisation is complete as the fixmap space used to map the EFI
 * memmap (via early_memremap()) is a scarce resource.
 *
 * This late mapping is intended to persist for the duration of
 * runtime so that things like efi_mem_desc_lookup() and
 * efi_mem_attributes() always work.
 *
 * Returns zero on success, a negative error code on failure.
 */
int __init efi_memmap_init_late(phys_addr_t addr, unsigned long size)
{
	struct efi_memory_map_data data = {
		.phys_map = addr,
		.size = size,
	};

	/* Did we forget to unmap the early EFI memmap? */
	WARN_ON(efi.memmap.map);

	/* Were we already called? */
	WARN_ON(efi.memmap.late);

	/*
	 * It makes no sense to allow callers to register different
	 * values for the following fields. Copy them out of the
	 * existing early EFI memmap.
	 */
	data.desc_version = efi.memmap.desc_version;
	data.desc_size = efi.memmap.desc_size;

	return __efi_memmap_init(&data, true);
}

/**
 * efi_memmap_install - Install a new EFI memory map in efi.memmap
 * @addr: Physical address of the memory map
 * @nr_map: Number of entries in the memory map
 *
 * Unlike efi_memmap_init_*(), this function does not allow the caller
 * to switch from early to late mappings. It simply uses the existing
 * mapping function and installs the new memmap.
 *
 * Returns zero on success, a negative error code on failure.
 */
int __init efi_memmap_install(phys_addr_t addr, unsigned int nr_map)
{
	struct efi_memory_map_data data;

	efi_memmap_unmap();

	data.phys_map = addr;
	data.size = efi.memmap.desc_size * nr_map;
	data.desc_version = efi.memmap.desc_version;
	data.desc_size = efi.memmap.desc_size;

	return __efi_memmap_init(&data, efi.memmap.late);
}

/**
 * efi_memmap_split_count - Count number of additional EFI memmap entries
 * @md: EFI memory descriptor to split
 * @range: Address range (start, end) to split around
 *
 * Returns the number of additional EFI memmap entries required to
 * accomodate @range.
 */
int __init efi_memmap_split_count(efi_memory_desc_t *md, struct range *range)
{
	u64 m_start, m_end;
	u64 start, end;
	int count = 0;

	start = md->phys_addr;
	end = start + (md->num_pages << EFI_PAGE_SHIFT) - 1;

	/* modifying range */
	m_start = range->start;
	m_end = range->end;

	if (m_start <= start) {
		/* split into 2 parts */
		if (start < m_end && m_end < end)
			count++;
	}

	if (start < m_start && m_start < end) {
		/* split into 3 parts */
		if (m_end < end)
			count += 2;
		/* split into 2 parts */
		if (end <= m_end)
			count++;
	}

	return count;
}

/**
 * efi_memmap_insert - Insert a memory region in an EFI memmap
 * @old_memmap: The existing EFI memory map structure
 * @buf: Address of buffer to store new map
 * @mem: Memory map entry to insert
 *
 * It is suggested that you call efi_memmap_split_count() first
 * to see how large @buf needs to be.
 */
void __init efi_memmap_insert(struct efi_memory_map *old_memmap, void *buf,
			      struct efi_mem_range *mem)
{
	u64 m_start, m_end, m_attr;
	efi_memory_desc_t *md;
	u64 start, end;
	void *old, *new;

	/* modifying range */
	m_start = mem->range.start;
	m_end = mem->range.end;
	m_attr = mem->attribute;

	/*
	 * The EFI memory map deals with regions in EFI_PAGE_SIZE
	 * units. Ensure that the region described by 'mem' is aligned
	 * correctly.
	 */
	if (!IS_ALIGNED(m_start, EFI_PAGE_SIZE) ||
	    !IS_ALIGNED(m_end + 1, EFI_PAGE_SIZE)) {
		WARN_ON(1);
		return;
	}

	for (old = old_memmap->map, new = buf;
	     old < old_memmap->map_end;
	     old += old_memmap->desc_size, new += old_memmap->desc_size) {

		/* copy original EFI memory descriptor */
		memcpy(new, old, old_memmap->desc_size);
		md = new;
		start = md->phys_addr;
		end = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - 1;

		if (m_start <= start && end <= m_end)
			md->attribute |= m_attr;

		if (m_start <= start &&
		    (start < m_end && m_end < end)) {
			/* first part */
			md->attribute |= m_attr;
			md->num_pages = (m_end - md->phys_addr + 1) >>
				EFI_PAGE_SHIFT;
			/* latter part */
			new += old_memmap->desc_size;
			memcpy(new, old, old_memmap->desc_size);
			md = new;
			md->phys_addr = m_end + 1;
			md->num_pages = (end - md->phys_addr + 1) >>
				EFI_PAGE_SHIFT;
		}

		if ((start < m_start && m_start < end) && m_end < end) {
			/* first part */
			md->num_pages = (m_start - md->phys_addr) >>
				EFI_PAGE_SHIFT;
			/* middle part */
			new += old_memmap->desc_size;
			memcpy(new, old, old_memmap->desc_size);
			md = new;
			md->attribute |= m_attr;
			md->phys_addr = m_start;
			md->num_pages = (m_end - m_start + 1) >>
				EFI_PAGE_SHIFT;
			/* last part */
			new += old_memmap->desc_size;
			memcpy(new, old, old_memmap->desc_size);
			md = new;
			md->phys_addr = m_end + 1;
			md->num_pages = (end - m_end) >>
				EFI_PAGE_SHIFT;
		}

		if ((start < m_start && m_start < end) &&
		    (end <= m_end)) {
			/* first part */
			md->num_pages = (m_start - md->phys_addr) >>
				EFI_PAGE_SHIFT;
			/* latter part */
			new += old_memmap->desc_size;
			memcpy(new, old, old_memmap->desc_size);
			md = new;
			md->phys_addr = m_start;
			md->num_pages = (end - md->phys_addr + 1) >>
				EFI_PAGE_SHIFT;
			md->attribute |= m_attr;
		}
	}
}
