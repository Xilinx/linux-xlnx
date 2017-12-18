/*
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <a.ryabinin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "kasan test: %s " fmt, __func__

#include <linux/kernel.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/module.h>

/*
 * Note: test functions are marked noinline so that their names appear in
 * reports.
 */

static noinline void __init kmalloc_oob_right(void)
{
	char *ptr;
	size_t size = 123;

	pr_info("out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 'x';
	kfree(ptr);
}

static noinline void __init kmalloc_oob_left(void)
{
	char *ptr;
	size_t size = 15;

	pr_info("out-of-bounds to left\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	*ptr = *(ptr - 1);
	kfree(ptr);
}

static noinline void __init kmalloc_node_oob_right(void)
{
	char *ptr;
	size_t size = 4096;

	pr_info("kmalloc_node(): out-of-bounds to right\n");
	ptr = kmalloc_node(size, GFP_KERNEL, 0);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}

#ifdef CONFIG_SLUB
static noinline void __init kmalloc_pagealloc_oob_right(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE + 10;

	/* Allocate a chunk that does not fit into a SLUB cache to trigger
	 * the page allocator fallback.
	 */
	pr_info("kmalloc pagealloc allocation: out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}
#endif

static noinline void __init kmalloc_large_oob_right(void)
{
	char *ptr;
	size_t size = KMALLOC_MAX_CACHE_SIZE - 256;
	/* Allocate a chunk that is large enough, but still fits into a slab
	 * and does not trigger the page allocator fallback in SLUB.
	 */
	pr_info("kmalloc large allocation: out-of-bounds to right\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr[size] = 0;
	kfree(ptr);
}

static noinline void __init kmalloc_oob_krealloc_more(void)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 19;

	pr_info("out-of-bounds after krealloc more\n");
	ptr1 = kmalloc(size1, GFP_KERNEL);
	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		return;
	}

	ptr2[size2] = 'x';
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_krealloc_less(void)
{
	char *ptr1, *ptr2;
	size_t size1 = 17;
	size_t size2 = 15;

	pr_info("out-of-bounds after krealloc less\n");
	ptr1 = kmalloc(size1, GFP_KERNEL);
	ptr2 = krealloc(ptr1, size2, GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		return;
	}
	ptr2[size2] = 'x';
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_16(void)
{
	struct {
		u64 words[2];
	} *ptr1, *ptr2;

	pr_info("kmalloc out-of-bounds for 16-bytes access\n");
	ptr1 = kmalloc(sizeof(*ptr1) - 3, GFP_KERNEL);
	ptr2 = kmalloc(sizeof(*ptr2), GFP_KERNEL);
	if (!ptr1 || !ptr2) {
		pr_err("Allocation failed\n");
		kfree(ptr1);
		kfree(ptr2);
		return;
	}
	*ptr1 = *ptr2;
	kfree(ptr1);
	kfree(ptr2);
}

static noinline void __init kmalloc_oob_memset_2(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset2\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+7, 0, 2);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_memset_4(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset4\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+5, 0, 4);
	kfree(ptr);
}


static noinline void __init kmalloc_oob_memset_8(void)
{
	char *ptr;
	size_t size = 8;

	pr_info("out-of-bounds in memset8\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+1, 0, 8);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_memset_16(void)
{
	char *ptr;
	size_t size = 16;

	pr_info("out-of-bounds in memset16\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr+1, 0, 16);
	kfree(ptr);
}

static noinline void __init kmalloc_oob_in_memset(void)
{
	char *ptr;
	size_t size = 666;

	pr_info("out-of-bounds in memset\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	memset(ptr, 0, size+5);
	kfree(ptr);
}

static noinline void __init kmalloc_uaf(void)
{
	char *ptr;
	size_t size = 10;

	pr_info("use-after-free\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr);
	*(ptr + 8) = 'x';
}

static noinline void __init kmalloc_uaf_memset(void)
{
	char *ptr;
	size_t size = 33;

	pr_info("use-after-free in memset\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr);
	memset(ptr, 0, size);
}

static noinline void __init kmalloc_uaf2(void)
{
	char *ptr1, *ptr2;
	size_t size = 43;

	pr_info("use-after-free after another kmalloc\n");
	ptr1 = kmalloc(size, GFP_KERNEL);
	if (!ptr1) {
		pr_err("Allocation failed\n");
		return;
	}

	kfree(ptr1);
	ptr2 = kmalloc(size, GFP_KERNEL);
	if (!ptr2) {
		pr_err("Allocation failed\n");
		return;
	}

	ptr1[40] = 'x';
	if (ptr1 == ptr2)
		pr_err("Could not detect use-after-free: ptr1 == ptr2\n");
	kfree(ptr2);
}

static noinline void __init kmem_cache_oob(void)
{
	char *p;
	size_t size = 200;
	struct kmem_cache *cache = kmem_cache_create("test_cache",
						size, 0,
						0, NULL);
	if (!cache) {
		pr_err("Cache allocation failed\n");
		return;
	}
	pr_info("out-of-bounds in kmem_cache_alloc\n");
	p = kmem_cache_alloc(cache, GFP_KERNEL);
	if (!p) {
		pr_err("Allocation failed\n");
		kmem_cache_destroy(cache);
		return;
	}

	*p = p[size];
	kmem_cache_free(cache, p);
	kmem_cache_destroy(cache);
}

static char global_array[10];

static noinline void __init kasan_global_oob(void)
{
	volatile int i = 3;
	char *p = &global_array[ARRAY_SIZE(global_array) + i];

	pr_info("out-of-bounds global variable\n");
	*(volatile char *)p;
}

static noinline void __init kasan_stack_oob(void)
{
	char stack_array[10];
	volatile int i = 0;
	char *p = &stack_array[ARRAY_SIZE(stack_array) + i];

	pr_info("out-of-bounds on stack\n");
	*(volatile char *)p;
}

static noinline void __init ksize_unpoisons_memory(void)
{
	char *ptr;
	size_t size = 123, real_size = size;

	pr_info("ksize() unpoisons the whole allocated chunk\n");
	ptr = kmalloc(size, GFP_KERNEL);
	if (!ptr) {
		pr_err("Allocation failed\n");
		return;
	}
	real_size = ksize(ptr);
	/* This access doesn't trigger an error. */
	ptr[size] = 'x';
	/* This one does. */
	ptr[real_size] = 'y';
	kfree(ptr);
}

static noinline void __init copy_user_test(void)
{
	char *kmem;
	char __user *usermem;
	size_t size = 10;
	int unused;

	kmem = kmalloc(size, GFP_KERNEL);
	if (!kmem)
		return;

	usermem = (char __user *)vm_mmap(NULL, 0, PAGE_SIZE,
			    PROT_READ | PROT_WRITE | PROT_EXEC,
			    MAP_ANONYMOUS | MAP_PRIVATE, 0);
	if (IS_ERR(usermem)) {
		pr_err("Failed to allocate user memory\n");
		kfree(kmem);
		return;
	}

	pr_info("out-of-bounds in copy_from_user()\n");
	unused = copy_from_user(kmem, usermem, size + 1);

	pr_info("out-of-bounds in copy_to_user()\n");
	unused = copy_to_user(usermem, kmem, size + 1);

	pr_info("out-of-bounds in __copy_from_user()\n");
	unused = __copy_from_user(kmem, usermem, size + 1);

	pr_info("out-of-bounds in __copy_to_user()\n");
	unused = __copy_to_user(usermem, kmem, size + 1);

	pr_info("out-of-bounds in __copy_from_user_inatomic()\n");
	unused = __copy_from_user_inatomic(kmem, usermem, size + 1);

	pr_info("out-of-bounds in __copy_to_user_inatomic()\n");
	unused = __copy_to_user_inatomic(usermem, kmem, size + 1);

	pr_info("out-of-bounds in strncpy_from_user()\n");
	unused = strncpy_from_user(kmem, usermem, size + 1);

	vm_munmap((unsigned long)usermem, PAGE_SIZE);
	kfree(kmem);
}

static noinline void __init use_after_scope_test(void)
{
	volatile char *volatile p;

	pr_info("use-after-scope on int\n");
	{
		int local = 0;

		p = (char *)&local;
	}
	p[0] = 1;
	p[3] = 1;

	pr_info("use-after-scope on array\n");
	{
		char local[1024] = {0};

		p = local;
	}
	p[0] = 1;
	p[1023] = 1;
}

static int __init kmalloc_tests_init(void)
{
	kmalloc_oob_right();
	kmalloc_oob_left();
	kmalloc_node_oob_right();
#ifdef CONFIG_SLUB
	kmalloc_pagealloc_oob_right();
#endif
	kmalloc_large_oob_right();
	kmalloc_oob_krealloc_more();
	kmalloc_oob_krealloc_less();
	kmalloc_oob_16();
	kmalloc_oob_in_memset();
	kmalloc_oob_memset_2();
	kmalloc_oob_memset_4();
	kmalloc_oob_memset_8();
	kmalloc_oob_memset_16();
	kmalloc_uaf();
	kmalloc_uaf_memset();
	kmalloc_uaf2();
	kmem_cache_oob();
	kasan_stack_oob();
	kasan_global_oob();
	ksize_unpoisons_memory();
	copy_user_test();
	use_after_scope_test();
	return -EAGAIN;
}

module_init(kmalloc_tests_init);
MODULE_LICENSE("GPL");
