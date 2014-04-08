#ifndef __PERF_SYMBOL
#define __PERF_SYMBOL 1

#include <linux/types.h>
#include <stdbool.h>
#include <stdint.h>
#include "map.h"
#include "../perf.h"
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <byteswap.h>
#include <libgen.h>
#include "build-id.h"

#ifdef HAVE_LIBELF_SUPPORT
#include <libelf.h>
#include <gelf.h>
#endif
#include <elf.h>

#include "dso.h"

#ifdef HAVE_CPLUS_DEMANGLE_SUPPORT
extern char *cplus_demangle(const char *, int);

static inline char *bfd_demangle(void __maybe_unused *v, const char *c, int i)
{
	return cplus_demangle(c, i);
}
#else
#ifdef NO_DEMANGLE
static inline char *bfd_demangle(void __maybe_unused *v,
				 const char __maybe_unused *c,
				 int __maybe_unused i)
{
	return NULL;
}
#else
#define PACKAGE 'perf'
#include <bfd.h>
#endif
#endif

/*
 * libelf 0.8.x and earlier do not support ELF_C_READ_MMAP;
 * for newer versions we can use mmap to reduce memory usage:
 */
#ifdef HAVE_LIBELF_MMAP_SUPPORT
# define PERF_ELF_C_READ_MMAP ELF_C_READ_MMAP
#else
# define PERF_ELF_C_READ_MMAP ELF_C_READ
#endif

#ifndef DMGL_PARAMS
#define DMGL_PARAMS      (1 << 0)       /* Include function args */
#define DMGL_ANSI        (1 << 1)       /* Include const, volatile, etc */
#endif

/** struct symbol - symtab entry
 *
 * @ignore - resolvable but tools ignore it (e.g. idle routines)
 */
struct symbol {
	struct rb_node	rb_node;
	u64		start;
	u64		end;
	u16		namelen;
	u8		binding;
	bool		ignore;
	char		name[0];
};

void symbol__delete(struct symbol *sym);
void symbols__delete(struct rb_root *symbols);

static inline size_t symbol__size(const struct symbol *sym)
{
	return sym->end - sym->start + 1;
}

struct strlist;

struct symbol_conf {
	unsigned short	priv_size;
	unsigned short	nr_events;
	bool		try_vmlinux_path,
			ignore_vmlinux,
			show_kernel_path,
			use_modules,
			sort_by_name,
			show_nr_samples,
			show_total_period,
			use_callchain,
			exclude_other,
			show_cpu_utilization,
			initialized,
			kptr_restrict,
			annotate_asm_raw,
			annotate_src,
			event_group,
			demangle;
	const char	*vmlinux_name,
			*kallsyms_name,
			*source_prefix,
			*field_sep;
	const char	*default_guest_vmlinux_name,
			*default_guest_kallsyms,
			*default_guest_modules;
	const char	*guestmount;
	const char	*dso_list_str,
			*comm_list_str,
			*sym_list_str,
			*col_width_list_str;
       struct strlist	*dso_list,
			*comm_list,
			*sym_list,
			*dso_from_list,
			*dso_to_list,
			*sym_from_list,
			*sym_to_list;
	const char	*symfs;
};

extern struct symbol_conf symbol_conf;
extern int vmlinux_path__nr_entries;
extern char **vmlinux_path;

static inline void *symbol__priv(struct symbol *sym)
{
	return ((void *)sym) - symbol_conf.priv_size;
}

struct ref_reloc_sym {
	const char	*name;
	u64		addr;
	u64		unrelocated_addr;
};

struct map_symbol {
	struct map    *map;
	struct symbol *sym;
	bool	      unfolded;
	bool	      has_children;
};

struct addr_map_symbol {
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	u64	      al_addr;
};

struct branch_info {
	struct addr_map_symbol from;
	struct addr_map_symbol to;
	struct branch_flags flags;
};

struct mem_info {
	struct addr_map_symbol iaddr;
	struct addr_map_symbol daddr;
	union perf_mem_data_src data_src;
};

struct addr_location {
	struct thread *thread;
	struct map    *map;
	struct symbol *sym;
	u64	      addr;
	char	      level;
	bool	      filtered;
	u8	      cpumode;
	s32	      cpu;
};

struct symsrc {
	char *name;
	int fd;
	enum dso_binary_type type;

#ifdef HAVE_LIBELF_SUPPORT
	Elf *elf;
	GElf_Ehdr ehdr;

	Elf_Scn *opdsec;
	size_t opdidx;
	GElf_Shdr opdshdr;

	Elf_Scn *symtab;
	GElf_Shdr symshdr;

	Elf_Scn *dynsym;
	size_t dynsym_idx;
	GElf_Shdr dynshdr;

	bool adjust_symbols;
#endif
};

void symsrc__destroy(struct symsrc *ss);
int symsrc__init(struct symsrc *ss, struct dso *dso, const char *name,
		 enum dso_binary_type type);
bool symsrc__has_symtab(struct symsrc *ss);
bool symsrc__possibly_runtime(struct symsrc *ss);

int dso__load(struct dso *dso, struct map *map, symbol_filter_t filter);
int dso__load_vmlinux(struct dso *dso, struct map *map,
		      const char *vmlinux, symbol_filter_t filter);
int dso__load_vmlinux_path(struct dso *dso, struct map *map,
			   symbol_filter_t filter);
int dso__load_kallsyms(struct dso *dso, const char *filename, struct map *map,
		       symbol_filter_t filter);

struct symbol *dso__find_symbol(struct dso *dso, enum map_type type,
				u64 addr);
struct symbol *dso__find_symbol_by_name(struct dso *dso, enum map_type type,
					const char *name);
struct symbol *dso__first_symbol(struct dso *dso, enum map_type type);

int filename__read_build_id(const char *filename, void *bf, size_t size);
int sysfs__read_build_id(const char *filename, void *bf, size_t size);
int kallsyms__parse(const char *filename, void *arg,
		    int (*process_symbol)(void *arg, const char *name,
					  char type, u64 start));
int modules__parse(const char *filename, void *arg,
		   int (*process_module)(void *arg, const char *name,
					 u64 start));
int filename__read_debuglink(const char *filename, char *debuglink,
			     size_t size);

int symbol__init(void);
void symbol__exit(void);
void symbol__elf_init(void);
struct symbol *symbol__new(u64 start, u64 len, u8 binding, const char *name);
size_t symbol__fprintf_symname_offs(const struct symbol *sym,
				    const struct addr_location *al, FILE *fp);
size_t symbol__fprintf_symname(const struct symbol *sym, FILE *fp);
size_t symbol__fprintf(struct symbol *sym, FILE *fp);
bool symbol_type__is_a(char symbol_type, enum map_type map_type);
bool symbol__restricted_filename(const char *filename,
				 const char *restricted_filename);

int dso__load_sym(struct dso *dso, struct map *map, struct symsrc *syms_ss,
		  struct symsrc *runtime_ss, symbol_filter_t filter,
		  int kmodule);
int dso__synthesize_plt_symbols(struct dso *dso, struct symsrc *ss,
				struct map *map, symbol_filter_t filter);

void symbols__insert(struct rb_root *symbols, struct symbol *sym);
void symbols__fixup_duplicate(struct rb_root *symbols);
void symbols__fixup_end(struct rb_root *symbols);
void __map_groups__fixup_end(struct map_groups *mg, enum map_type type);

typedef int (*mapfn_t)(u64 start, u64 len, u64 pgoff, void *data);
int file__read_maps(int fd, bool exe, mapfn_t mapfn, void *data,
		    bool *is_64_bit);

#define PERF_KCORE_EXTRACT "/tmp/perf-kcore-XXXXXX"

struct kcore_extract {
	char *kcore_filename;
	u64 addr;
	u64 offs;
	u64 len;
	char extract_filename[sizeof(PERF_KCORE_EXTRACT)];
	int fd;
};

int kcore_extract__create(struct kcore_extract *kce);
void kcore_extract__delete(struct kcore_extract *kce);

int kcore_copy(const char *from_dir, const char *to_dir);
int compare_proc_modules(const char *from, const char *to);

#endif /* __PERF_SYMBOL */
