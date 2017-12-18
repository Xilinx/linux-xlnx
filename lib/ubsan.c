/*
 * UBSAN error reporting functions
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>

#include "ubsan.h"

const char *type_check_kinds[] = {
	"load of",
	"store to",
	"reference binding to",
	"member access within",
	"member call on",
	"constructor call on",
	"downcast of",
	"downcast of"
};

#define REPORTED_BIT 31

#if (BITS_PER_LONG == 64) && defined(__BIG_ENDIAN)
#define COLUMN_MASK (~(1U << REPORTED_BIT))
#define LINE_MASK   (~0U)
#else
#define COLUMN_MASK   (~0U)
#define LINE_MASK (~(1U << REPORTED_BIT))
#endif

#define VALUE_LENGTH 40

static bool was_reported(struct source_location *location)
{
	return test_and_set_bit(REPORTED_BIT, &location->reported);
}

static void print_source_location(const char *prefix,
				struct source_location *loc)
{
	pr_err("%s %s:%d:%d\n", prefix, loc->file_name,
		loc->line & LINE_MASK, loc->column & COLUMN_MASK);
}

static bool suppress_report(struct source_location *loc)
{
	return current->in_ubsan || was_reported(loc);
}

static bool type_is_int(struct type_descriptor *type)
{
	return type->type_kind == type_kind_int;
}

static bool type_is_signed(struct type_descriptor *type)
{
	WARN_ON(!type_is_int(type));
	return  type->type_info & 1;
}

static unsigned type_bit_width(struct type_descriptor *type)
{
	return 1 << (type->type_info >> 1);
}

static bool is_inline_int(struct type_descriptor *type)
{
	unsigned inline_bits = sizeof(unsigned long)*8;
	unsigned bits = type_bit_width(type);

	WARN_ON(!type_is_int(type));

	return bits <= inline_bits;
}

static s_max get_signed_val(struct type_descriptor *type, unsigned long val)
{
	if (is_inline_int(type)) {
		unsigned extra_bits = sizeof(s_max)*8 - type_bit_width(type);
		return ((s_max)val) << extra_bits >> extra_bits;
	}

	if (type_bit_width(type) == 64)
		return *(s64 *)val;

	return *(s_max *)val;
}

static bool val_is_negative(struct type_descriptor *type, unsigned long val)
{
	return type_is_signed(type) && get_signed_val(type, val) < 0;
}

static u_max get_unsigned_val(struct type_descriptor *type, unsigned long val)
{
	if (is_inline_int(type))
		return val;

	if (type_bit_width(type) == 64)
		return *(u64 *)val;

	return *(u_max *)val;
}

static void val_to_string(char *str, size_t size, struct type_descriptor *type,
	unsigned long value)
{
	if (type_is_int(type)) {
		if (type_bit_width(type) == 128) {
#if defined(CONFIG_ARCH_SUPPORTS_INT128) && defined(__SIZEOF_INT128__)
			u_max val = get_unsigned_val(type, value);

			scnprintf(str, size, "0x%08x%08x%08x%08x",
				(u32)(val >> 96),
				(u32)(val >> 64),
				(u32)(val >> 32),
				(u32)(val));
#else
			WARN_ON(1);
#endif
		} else if (type_is_signed(type)) {
			scnprintf(str, size, "%lld",
				(s64)get_signed_val(type, value));
		} else {
			scnprintf(str, size, "%llu",
				(u64)get_unsigned_val(type, value));
		}
	}
}

static bool location_is_valid(struct source_location *loc)
{
	return loc->file_name != NULL;
}

static DEFINE_SPINLOCK(report_lock);

static void ubsan_prologue(struct source_location *location,
			unsigned long *flags)
{
	current->in_ubsan++;
	spin_lock_irqsave(&report_lock, *flags);

	pr_err("========================================"
		"========================================\n");
	print_source_location("UBSAN: Undefined behaviour in", location);
}

static void ubsan_epilogue(unsigned long *flags)
{
	dump_stack();
	pr_err("========================================"
		"========================================\n");
	spin_unlock_irqrestore(&report_lock, *flags);
	current->in_ubsan--;
}

static void handle_overflow(struct overflow_data *data, unsigned long lhs,
			unsigned long rhs, char op)
{

	struct type_descriptor *type = data->type;
	unsigned long flags;
	char lhs_val_str[VALUE_LENGTH];
	char rhs_val_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(lhs_val_str, sizeof(lhs_val_str), type, lhs);
	val_to_string(rhs_val_str, sizeof(rhs_val_str), type, rhs);
	pr_err("%s integer overflow:\n",
		type_is_signed(type) ? "signed" : "unsigned");
	pr_err("%s %c %s cannot be represented in type %s\n",
		lhs_val_str,
		op,
		rhs_val_str,
		type->type_name);

	ubsan_epilogue(&flags);
}

void __ubsan_handle_add_overflow(struct overflow_data *data,
				unsigned long lhs,
				unsigned long rhs)
{

	handle_overflow(data, lhs, rhs, '+');
}
EXPORT_SYMBOL(__ubsan_handle_add_overflow);

void __ubsan_handle_sub_overflow(struct overflow_data *data,
				unsigned long lhs,
				unsigned long rhs)
{
	handle_overflow(data, lhs, rhs, '-');
}
EXPORT_SYMBOL(__ubsan_handle_sub_overflow);

void __ubsan_handle_mul_overflow(struct overflow_data *data,
				unsigned long lhs,
				unsigned long rhs)
{
	handle_overflow(data, lhs, rhs, '*');
}
EXPORT_SYMBOL(__ubsan_handle_mul_overflow);

void __ubsan_handle_negate_overflow(struct overflow_data *data,
				unsigned long old_val)
{
	unsigned long flags;
	char old_val_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(old_val_str, sizeof(old_val_str), data->type, old_val);

	pr_err("negation of %s cannot be represented in type %s:\n",
		old_val_str, data->type->type_name);

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_negate_overflow);


void __ubsan_handle_divrem_overflow(struct overflow_data *data,
				unsigned long lhs,
				unsigned long rhs)
{
	unsigned long flags;
	char rhs_val_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(rhs_val_str, sizeof(rhs_val_str), data->type, rhs);

	if (type_is_signed(data->type) && get_signed_val(data->type, rhs) == -1)
		pr_err("division of %s by -1 cannot be represented in type %s\n",
			rhs_val_str, data->type->type_name);
	else
		pr_err("division by zero\n");

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_divrem_overflow);

static void handle_null_ptr_deref(struct type_mismatch_data *data)
{
	unsigned long flags;

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	pr_err("%s null pointer of type %s\n",
		type_check_kinds[data->type_check_kind],
		data->type->type_name);

	ubsan_epilogue(&flags);
}

static void handle_missaligned_access(struct type_mismatch_data *data,
				unsigned long ptr)
{
	unsigned long flags;

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	pr_err("%s misaligned address %p for type %s\n",
		type_check_kinds[data->type_check_kind],
		(void *)ptr, data->type->type_name);
	pr_err("which requires %ld byte alignment\n", data->alignment);

	ubsan_epilogue(&flags);
}

static void handle_object_size_mismatch(struct type_mismatch_data *data,
					unsigned long ptr)
{
	unsigned long flags;

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);
	pr_err("%s address %p with insufficient space\n",
		type_check_kinds[data->type_check_kind],
		(void *) ptr);
	pr_err("for an object of type %s\n", data->type->type_name);
	ubsan_epilogue(&flags);
}

void __ubsan_handle_type_mismatch(struct type_mismatch_data *data,
				unsigned long ptr)
{

	if (!ptr)
		handle_null_ptr_deref(data);
	else if (data->alignment && !IS_ALIGNED(ptr, data->alignment))
		handle_missaligned_access(data, ptr);
	else
		handle_object_size_mismatch(data, ptr);
}
EXPORT_SYMBOL(__ubsan_handle_type_mismatch);

void __ubsan_handle_nonnull_return(struct nonnull_return_data *data)
{
	unsigned long flags;

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	pr_err("null pointer returned from function declared to never return null\n");

	if (location_is_valid(&data->attr_location))
		print_source_location("returns_nonnull attribute specified in",
				&data->attr_location);

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_nonnull_return);

void __ubsan_handle_vla_bound_not_positive(struct vla_bound_data *data,
					unsigned long bound)
{
	unsigned long flags;
	char bound_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(bound_str, sizeof(bound_str), data->type, bound);
	pr_err("variable length array bound value %s <= 0\n", bound_str);

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_vla_bound_not_positive);

void __ubsan_handle_out_of_bounds(struct out_of_bounds_data *data,
				unsigned long index)
{
	unsigned long flags;
	char index_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(index_str, sizeof(index_str), data->index_type, index);
	pr_err("index %s is out of range for type %s\n", index_str,
		data->array_type->type_name);
	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_out_of_bounds);

void __ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data,
					unsigned long lhs, unsigned long rhs)
{
	unsigned long flags;
	struct type_descriptor *rhs_type = data->rhs_type;
	struct type_descriptor *lhs_type = data->lhs_type;
	char rhs_str[VALUE_LENGTH];
	char lhs_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(rhs_str, sizeof(rhs_str), rhs_type, rhs);
	val_to_string(lhs_str, sizeof(lhs_str), lhs_type, lhs);

	if (val_is_negative(rhs_type, rhs))
		pr_err("shift exponent %s is negative\n", rhs_str);

	else if (get_unsigned_val(rhs_type, rhs) >=
		type_bit_width(lhs_type))
		pr_err("shift exponent %s is too large for %u-bit type %s\n",
			rhs_str,
			type_bit_width(lhs_type),
			lhs_type->type_name);
	else if (val_is_negative(lhs_type, lhs))
		pr_err("left shift of negative value %s\n",
			lhs_str);
	else
		pr_err("left shift of %s by %s places cannot be"
			" represented in type %s\n",
			lhs_str, rhs_str,
			lhs_type->type_name);

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_shift_out_of_bounds);


void __noreturn
__ubsan_handle_builtin_unreachable(struct unreachable_data *data)
{
	unsigned long flags;

	ubsan_prologue(&data->location, &flags);
	pr_err("calling __builtin_unreachable()\n");
	ubsan_epilogue(&flags);
	panic("can't return from __builtin_unreachable()");
}
EXPORT_SYMBOL(__ubsan_handle_builtin_unreachable);

void __ubsan_handle_load_invalid_value(struct invalid_value_data *data,
				unsigned long val)
{
	unsigned long flags;
	char val_str[VALUE_LENGTH];

	if (suppress_report(&data->location))
		return;

	ubsan_prologue(&data->location, &flags);

	val_to_string(val_str, sizeof(val_str), data->type, val);

	pr_err("load of value %s is not a valid value for type %s\n",
		val_str, data->type->type_name);

	ubsan_epilogue(&flags);
}
EXPORT_SYMBOL(__ubsan_handle_load_invalid_value);
