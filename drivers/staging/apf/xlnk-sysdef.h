#ifndef XLNK_SYSDEF_H
#define XLNK_SYSDEF_H

#if __SIZEOF_POINTER__  == 4
	#define XLNK_SYS_BIT_WIDTH 32
#elif __SIZEOF_POINTER__  == 8
	#define XLNK_SYS_BIT_WIDTH 64
#endif

#include <linux/types.h>

#if XLNK_SYS_BIT_WIDTH == 32

	typedef __aligned(4) u32 xlnk_intptr_type;
	typedef __aligned(4) s32 xlnk_int_type;
	typedef __aligned(4) u32 xlnk_uint_type;
	typedef u8 xlnk_byte_type;
	typedef s8 xlnk_char_type;
	#define xlnk_enum_type __aligned(4) s32

#elif XLNK_SYS_BIT_WIDTH == 64

	typedef __aligned(8) u64 xlnk_intptr_type;
	typedef __aligned(4) s32 xlnk_int_type;
	typedef __aligned(4) u32 xlnk_uint_type;
	typedef u8 xlnk_byte_type;
	typedef s8 xlnk_char_type;
	#define xlnk_enum_type __aligned(4) s32

#else
	#error "Please define application bit width and system bit width"
#endif

#endif
