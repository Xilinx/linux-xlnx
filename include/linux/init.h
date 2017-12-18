#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

#include <linux/compiler.h>
#include <linux/types.h>

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata or __initconst between the variable name
 * and equal sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static const char linux_logo[] __initconst = { 0x32, 0x36, ... };
 *
 * Don't forget to initialize data not at file scope, i.e. within a function,
 * as gcc otherwise puts the data into the bss section and not into the init
 * section.
 */

/* These are for everybody (although not all archs will actually
   discard it in modules) */
#define __init		__section(.init.text) __cold notrace __latent_entropy
#define __initdata	__section(.init.data)
#define __initconst	__section(.init.rodata)
#define __exitdata	__section(.exit.data)
#define __exit_call	__used __section(.exitcall.exit)

/*
 * modpost check for section mismatches during the kernel build.
 * A section mismatch happens when there are references from a
 * code or data section to an init section (both code or data).
 * The init sections are (for most archs) discarded by the kernel
 * when early init has completed so all such references are potential bugs.
 * For exit sections the same issue exists.
 *
 * The following markers are used for the cases where the reference to
 * the *init / *exit section (code or data) is valid and will teach
 * modpost not to issue a warning.  Intended semantics is that a code or
 * data tagged __ref* can reference code or data from init section without
 * producing a warning (of course, no warning does not mean code is
 * correct, so optimally document why the __ref is needed and why it's OK).
 *
 * The markers follow same syntax rules as __init / __initdata.
 */
#define __ref            __section(.ref.text) noinline
#define __refdata        __section(.ref.data)
#define __refconst       __section(.ref.rodata)

#ifdef MODULE
#define __exitused
#else
#define __exitused  __used
#endif

#define __exit          __section(.exit.text) __exitused __cold notrace

/* Used for MEMORY_HOTPLUG */
#define __meminit        __section(.meminit.text) __cold notrace \
						  __latent_entropy
#define __meminitdata    __section(.meminit.data)
#define __meminitconst   __section(.meminit.rodata)
#define __memexit        __section(.memexit.text) __exitused __cold notrace
#define __memexitdata    __section(.memexit.data)
#define __memexitconst   __section(.memexit.rodata)

/* For assembly routines */
#define __HEAD		.section	".head.text","ax"
#define __INIT		.section	".init.text","ax"
#define __FINIT		.previous

#define __INITDATA	.section	".init.data","aw",%progbits
#define __INITRODATA	.section	".init.rodata","a",%progbits
#define __FINITDATA	.previous

#define __MEMINIT        .section	".meminit.text", "ax"
#define __MEMINITDATA    .section	".meminit.data", "aw"
#define __MEMINITRODATA  .section	".meminit.rodata", "a"

/* silence warnings when references are OK */
#define __REF            .section       ".ref.text", "ax"
#define __REFDATA        .section       ".ref.data", "aw"
#define __REFCONST       .section       ".ref.rodata", "a"

#ifndef __ASSEMBLY__
/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);
typedef void (*exitcall_t)(void);

extern initcall_t __con_initcall_start[], __con_initcall_end[];
extern initcall_t __security_initcall_start[], __security_initcall_end[];

/* Used for contructor calls. */
typedef void (*ctor_fn_t)(void);

/* Defined in init/main.c */
extern int do_one_initcall(initcall_t fn);
extern char __initdata boot_command_line[];
extern char *saved_command_line;
extern unsigned int reset_devices;

/* used by init/main.c */
void setup_arch(char **);
void prepare_namespace(void);
void __init load_default_modules(void);
int __init init_rootfs(void);

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
#endif

extern void (*late_time_init)(void);

extern bool initcall_debug;

#endif
  
#ifndef MODULE

#ifndef __ASSEMBLY__

/*
 * initcalls are now grouped by functionality into separate
 * subsections. Ordering inside the subsections is determined
 * by link order. 
 * For backwards compatibility, initcall() puts the call in 
 * the device init subsection.
 *
 * The `id' arg to __define_initcall() is needed so that multiple initcalls
 * can point at the same handler without causing duplicate-symbol build errors.
 *
 * Initcalls are run by placing pointers in initcall sections that the
 * kernel iterates at runtime. The linker can do dead code / data elimination
 * and remove that completely, so the initcall sections have to be marked
 * as KEEP() in the linker script.
 */

#define __define_initcall(fn, id) \
	static initcall_t __initcall_##fn##id __used \
	__attribute__((__section__(".initcall" #id ".init"))) = fn;

/*
 * Early initcalls run before initializing SMP.
 *
 * Only for built-in code, not modules.
 */
#define early_initcall(fn)		__define_initcall(fn, early)

/*
 * A "pure" initcall has no dependencies on anything else, and purely
 * initializes variables that couldn't be statically initialized.
 *
 * This only exists for built-in code, not for modules.
 * Keep main.c:initcall_level_names[] in sync.
 */
#define pure_initcall(fn)		__define_initcall(fn, 0)

#define core_initcall(fn)		__define_initcall(fn, 1)
#define core_initcall_sync(fn)		__define_initcall(fn, 1s)
#define postcore_initcall(fn)		__define_initcall(fn, 2)
#define postcore_initcall_sync(fn)	__define_initcall(fn, 2s)
#define arch_initcall(fn)		__define_initcall(fn, 3)
#define arch_initcall_sync(fn)		__define_initcall(fn, 3s)
#define subsys_initcall(fn)		__define_initcall(fn, 4)
#define subsys_initcall_sync(fn)	__define_initcall(fn, 4s)
#define fs_initcall(fn)			__define_initcall(fn, 5)
#define fs_initcall_sync(fn)		__define_initcall(fn, 5s)
#define rootfs_initcall(fn)		__define_initcall(fn, rootfs)
#define device_initcall(fn)		__define_initcall(fn, 6)
#define device_initcall_sync(fn)	__define_initcall(fn, 6s)
#define late_initcall(fn)		__define_initcall(fn, 7)
#define late_initcall_sync(fn)		__define_initcall(fn, 7s)

#define __initcall(fn) device_initcall(fn)

#define __exitcall(fn)						\
	static exitcall_t __exitcall_##fn __exit_call = fn

#define console_initcall(fn)					\
	static initcall_t __initcall_##fn			\
	__used __section(.con_initcall.init) = fn

#define security_initcall(fn)					\
	static initcall_t __initcall_##fn			\
	__used __section(.security_initcall.init) = fn

struct obs_kernel_param {
	const char *str;
	int (*setup_func)(char *);
	int early;
};

/*
 * Only for really core code.  See moduleparam.h for the normal way.
 *
 * Force the alignment so the compiler doesn't space elements of the
 * obs_kernel_param "array" too far apart in .init.setup.
 */
#define __setup_param(str, unique_id, fn, early)			\
	static const char __setup_str_##unique_id[] __initconst		\
		__aligned(1) = str; 					\
	static struct obs_kernel_param __setup_##unique_id		\
		__used __section(.init.setup)				\
		__attribute__((aligned((sizeof(long)))))		\
		= { __setup_str_##unique_id, fn, early }

#define __setup(str, fn)						\
	__setup_param(str, fn, fn, 0)

/*
 * NOTE: fn is as per module_param, not __setup!
 * Emits warning if fn returns non-zero.
 */
#define early_param(str, fn)						\
	__setup_param(str, fn, fn, 1)

#define early_param_on_off(str_on, str_off, var, config)		\
									\
	int var = IS_ENABLED(config);					\
									\
	static int __init parse_##var##_on(char *arg)			\
	{								\
		var = 1;						\
		return 0;						\
	}								\
	__setup_param(str_on, parse_##var##_on, parse_##var##_on, 1);	\
									\
	static int __init parse_##var##_off(char *arg)			\
	{								\
		var = 0;						\
		return 0;						\
	}								\
	__setup_param(str_off, parse_##var##_off, parse_##var##_off, 1)

/* Relies on boot_command_line being set */
void __init parse_early_param(void);
void __init parse_early_options(char *cmdline);
#endif /* __ASSEMBLY__ */

#else /* MODULE */

#define __setup_param(str, unique_id, fn)	/* nothing */
#define __setup(str, func) 			/* nothing */
#endif

/* Data marked not to be saved by software suspend */
#define __nosavedata __section(.data..nosave)

#ifdef MODULE
#define __exit_p(x) x
#else
#define __exit_p(x) NULL
#endif

#endif /* _LINUX_INIT_H */
