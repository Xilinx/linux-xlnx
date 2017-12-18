/*
 * Test cases for printf facility.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <linux/bitmap.h>
#include <linux/dcache.h>
#include <linux/socket.h>
#include <linux/in.h>

#include <linux/gfp.h>
#include <linux/mm.h>

#define BUF_SIZE 256
#define PAD_SIZE 16
#define FILL_CHAR '$'

#define PTR1 ((void*)0x01234567)
#define PTR2 ((void*)(long)(int)0xfedcba98)

#if BITS_PER_LONG == 64
#define PTR1_ZEROES "000000000"
#define PTR1_SPACES "         "
#define PTR1_STR "1234567"
#define PTR2_STR "fffffffffedcba98"
#define PTR_WIDTH 16
#else
#define PTR1_ZEROES "0"
#define PTR1_SPACES " "
#define PTR1_STR "1234567"
#define PTR2_STR "fedcba98"
#define PTR_WIDTH 8
#endif
#define PTR_WIDTH_STR stringify(PTR_WIDTH)

static unsigned total_tests __initdata;
static unsigned failed_tests __initdata;
static char *test_buffer __initdata;
static char *alloced_buffer __initdata;

static int __printf(4, 0) __init
do_test(int bufsize, const char *expect, int elen,
	const char *fmt, va_list ap)
{
	va_list aq;
	int ret, written;

	total_tests++;

	memset(alloced_buffer, FILL_CHAR, BUF_SIZE + 2*PAD_SIZE);
	va_copy(aq, ap);
	ret = vsnprintf(test_buffer, bufsize, fmt, aq);
	va_end(aq);

	if (ret != elen) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) returned %d, expected %d\n",
			bufsize, fmt, ret, elen);
		return 1;
	}

	if (memchr_inv(alloced_buffer, FILL_CHAR, PAD_SIZE)) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote before buffer\n", bufsize, fmt);
		return 1;
	}

	if (!bufsize) {
		if (memchr_inv(test_buffer, FILL_CHAR, BUF_SIZE + PAD_SIZE)) {
			pr_warn("vsnprintf(buf, 0, \"%s\", ...) wrote to buffer\n",
				fmt);
			return 1;
		}
		return 0;
	}

	written = min(bufsize-1, elen);
	if (test_buffer[written]) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) did not nul-terminate buffer\n",
			bufsize, fmt);
		return 1;
	}

	if (memchr_inv(test_buffer + written + 1, FILL_CHAR, BUF_SIZE + PAD_SIZE - (written + 1))) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote beyond the nul-terminator\n",
			bufsize, fmt);
		return 1;
	}

	if (memcmp(test_buffer, expect, written)) {
		pr_warn("vsnprintf(buf, %d, \"%s\", ...) wrote '%s', expected '%.*s'\n",
			bufsize, fmt, test_buffer, written, expect);
		return 1;
	}
	return 0;
}

static void __printf(3, 4) __init
__test(const char *expect, int elen, const char *fmt, ...)
{
	va_list ap;
	int rand;
	char *p;

	if (elen >= BUF_SIZE) {
		pr_err("error in test suite: expected output length %d too long. Format was '%s'.\n",
		       elen, fmt);
		failed_tests++;
		return;
	}

	va_start(ap, fmt);

	/*
	 * Every fmt+args is subjected to four tests: Three where we
	 * tell vsnprintf varying buffer sizes (plenty, not quite
	 * enough and 0), and then we also test that kvasprintf would
	 * be able to print it as expected.
	 */
	failed_tests += do_test(BUF_SIZE, expect, elen, fmt, ap);
	rand = 1 + prandom_u32_max(elen+1);
	/* Since elen < BUF_SIZE, we have 1 <= rand <= BUF_SIZE. */
	failed_tests += do_test(rand, expect, elen, fmt, ap);
	failed_tests += do_test(0, expect, elen, fmt, ap);

	p = kvasprintf(GFP_KERNEL, fmt, ap);
	if (p) {
		total_tests++;
		if (memcmp(p, expect, elen+1)) {
			pr_warn("kvasprintf(..., \"%s\", ...) returned '%s', expected '%s'\n",
				fmt, p, expect);
			failed_tests++;
		}
		kfree(p);
	}
	va_end(ap);
}

#define test(expect, fmt, ...)					\
	__test(expect, strlen(expect), fmt, ##__VA_ARGS__)

static void __init
test_basic(void)
{
	/* Work around annoying "warning: zero-length gnu_printf format string". */
	char nul = '\0';

	test("", &nul);
	test("100%", "100%%");
	test("xxx%yyy", "xxx%cyyy", '%');
	__test("xxx\0yyy", 7, "xxx%cyyy", '\0');
}

static void __init
test_number(void)
{
	test("0x1234abcd  ", "%#-12x", 0x1234abcd);
	test("  0x1234abcd", "%#12x", 0x1234abcd);
	test("0|001| 12|+123| 1234|-123|-1234", "%d|%03d|%3d|%+d|% d|%+d|% d", 0, 1, 12, 123, 1234, -123, -1234);
	test("0|1|1|128|255", "%hhu|%hhu|%hhu|%hhu|%hhu", 0, 1, 257, 128, -1);
	test("0|1|1|-128|-1", "%hhd|%hhd|%hhd|%hhd|%hhd", 0, 1, 257, 128, -1);
	test("2015122420151225", "%ho%ho%#ho", 1037, 5282, -11627);
	/*
	 * POSIX/C99: »The result of converting zero with an explicit
	 * precision of zero shall be no characters.« Hence the output
	 * from the below test should really be "00|0||| ". However,
	 * the kernel's printf also produces a single 0 in that
	 * case. This test case simply documents the current
	 * behaviour.
	 */
	test("00|0|0|0|0", "%.2d|%.1d|%.0d|%.*d|%1.0d", 0, 0, 0, 0, 0, 0);
#ifndef __CHAR_UNSIGNED__
	{
		/*
		 * Passing a 'char' to a %02x specifier doesn't do
		 * what was presumably the intention when char is
		 * signed and the value is negative. One must either &
		 * with 0xff or cast to u8.
		 */
		char val = -16;
		test("0xfffffff0|0xf0|0xf0", "%#02x|%#02x|%#02x", val, val & 0xff, (u8)val);
	}
#endif
}

static void __init
test_string(void)
{
	test("", "%s%.0s", "", "123");
	test("ABCD|abc|123", "%s|%.3s|%.*s", "ABCD", "abcdef", 3, "123456");
	test("1  |  2|3  |  4|5  ", "%-3s|%3s|%-*s|%*s|%*s", "1", "2", 3, "3", 3, "4", -3, "5");
	test("1234      ", "%-10.4s", "123456");
	test("      1234", "%10.4s", "123456");
	/*
	 * POSIX and C99 say that a negative precision (which is only
	 * possible to pass via a * argument) should be treated as if
	 * the precision wasn't present, and that if the precision is
	 * omitted (as in %.s), the precision should be taken to be
	 * 0. However, the kernel's printf behave exactly opposite,
	 * treating a negative precision as 0 and treating an omitted
	 * precision specifier as if no precision was given.
	 *
	 * These test cases document the current behaviour; should
	 * anyone ever feel the need to follow the standards more
	 * closely, this can be revisited.
	 */
	test("    ", "%4.*s", -5, "123456");
	test("123456", "%.s", "123456");
	test("a||", "%.s|%.0s|%.*s", "a", "b", 0, "c");
	test("a  |   |   ", "%-3.s|%-3.0s|%-3.*s", "a", "b", 0, "c");
}

static void __init
plain(void)
{
	test(PTR1_ZEROES PTR1_STR " " PTR2_STR, "%p %p", PTR1, PTR2);
	/*
	 * The field width is overloaded for some %p extensions to
	 * pass another piece of information. For plain pointers, the
	 * behaviour is slightly odd: One cannot pass either the 0
	 * flag nor a precision to %p without gcc complaining, and if
	 * one explicitly gives a field width, the number is no longer
	 * zero-padded.
	 */
	test("|" PTR1_STR PTR1_SPACES "  |  " PTR1_SPACES PTR1_STR "|",
	     "|%-*p|%*p|", PTR_WIDTH+2, PTR1, PTR_WIDTH+2, PTR1);
	test("|" PTR2_STR "  |  " PTR2_STR "|",
	     "|%-*p|%*p|", PTR_WIDTH+2, PTR2, PTR_WIDTH+2, PTR2);

	/*
	 * Unrecognized %p extensions are treated as plain %p, but the
	 * alphanumeric suffix is ignored (that is, does not occur in
	 * the output.)
	 */
	test("|"PTR1_ZEROES PTR1_STR"|", "|%p0y|", PTR1);
	test("|"PTR2_STR"|", "|%p0y|", PTR2);
}

static void __init
symbol_ptr(void)
{
}

static void __init
kernel_ptr(void)
{
}

static void __init
struct_resource(void)
{
}

static void __init
addr(void)
{
}

static void __init
escaped_str(void)
{
}

static void __init
hex_string(void)
{
	const char buf[3] = {0xc0, 0xff, 0xee};

	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%3ph|%3phC|%3phD|%3phN", buf, buf, buf, buf);
	test("c0 ff ee|c0:ff:ee|c0-ff-ee|c0ffee",
	     "%*ph|%*phC|%*phD|%*phN", 3, buf, 3, buf, 3, buf, 3, buf);
}

static void __init
mac(void)
{
	const u8 addr[6] = {0x2d, 0x48, 0xd6, 0xfc, 0x7a, 0x05};

	test("2d:48:d6:fc:7a:05", "%pM", addr);
	test("05:7a:fc:d6:48:2d", "%pMR", addr);
	test("2d-48-d6-fc-7a-05", "%pMF", addr);
	test("2d48d6fc7a05", "%pm", addr);
	test("057afcd6482d", "%pmR", addr);
}

static void __init
ip4(void)
{
	struct sockaddr_in sa;

	sa.sin_family = AF_INET;
	sa.sin_port = cpu_to_be16(12345);
	sa.sin_addr.s_addr = cpu_to_be32(0x7f000001);

	test("127.000.000.001|127.0.0.1", "%pi4|%pI4", &sa.sin_addr, &sa.sin_addr);
	test("127.000.000.001|127.0.0.1", "%piS|%pIS", &sa, &sa);
	sa.sin_addr.s_addr = cpu_to_be32(0x01020304);
	test("001.002.003.004:12345|1.2.3.4:12345", "%piSp|%pISp", &sa, &sa);
}

static void __init
ip6(void)
{
}

static void __init
ip(void)
{
	ip4();
	ip6();
}

static void __init
uuid(void)
{
	const char uuid[16] = {0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			       0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

	test("00010203-0405-0607-0809-0a0b0c0d0e0f", "%pUb", uuid);
	test("00010203-0405-0607-0809-0A0B0C0D0E0F", "%pUB", uuid);
	test("03020100-0504-0706-0809-0a0b0c0d0e0f", "%pUl", uuid);
	test("03020100-0504-0706-0809-0A0B0C0D0E0F", "%pUL", uuid);
}

static struct dentry test_dentry[4] __initdata = {
	{ .d_parent = &test_dentry[0],
	  .d_name = QSTR_INIT(test_dentry[0].d_iname, 3),
	  .d_iname = "foo" },
	{ .d_parent = &test_dentry[0],
	  .d_name = QSTR_INIT(test_dentry[1].d_iname, 5),
	  .d_iname = "bravo" },
	{ .d_parent = &test_dentry[1],
	  .d_name = QSTR_INIT(test_dentry[2].d_iname, 4),
	  .d_iname = "alfa" },
	{ .d_parent = &test_dentry[2],
	  .d_name = QSTR_INIT(test_dentry[3].d_iname, 5),
	  .d_iname = "romeo" },
};

static void __init
dentry(void)
{
	test("foo", "%pd", &test_dentry[0]);
	test("foo", "%pd2", &test_dentry[0]);

	test("romeo", "%pd", &test_dentry[3]);
	test("alfa/romeo", "%pd2", &test_dentry[3]);
	test("bravo/alfa/romeo", "%pd3", &test_dentry[3]);
	test("/bravo/alfa/romeo", "%pd4", &test_dentry[3]);
	test("/bravo/alfa", "%pd4", &test_dentry[2]);

	test("bravo/alfa  |bravo/alfa  ", "%-12pd2|%*pd2", &test_dentry[2], -12, &test_dentry[2]);
	test("  bravo/alfa|  bravo/alfa", "%12pd2|%*pd2", &test_dentry[2], 12, &test_dentry[2]);
}

static void __init
struct_va_format(void)
{
}

static void __init
struct_clk(void)
{
}

static void __init
large_bitmap(void)
{
	const int nbits = 1 << 16;
	unsigned long *bits = kcalloc(BITS_TO_LONGS(nbits), sizeof(long), GFP_KERNEL);
	if (!bits)
		return;

	bitmap_set(bits, 1, 20);
	bitmap_set(bits, 60000, 15);
	test("1-20,60000-60014", "%*pbl", nbits, bits);
	kfree(bits);
}

static void __init
bitmap(void)
{
	DECLARE_BITMAP(bits, 20);
	const int primes[] = {2,3,5,7,11,13,17,19};
	int i;

	bitmap_zero(bits, 20);
	test("00000|00000", "%20pb|%*pb", bits, 20, bits);
	test("|", "%20pbl|%*pbl", bits, 20, bits);

	for (i = 0; i < ARRAY_SIZE(primes); ++i)
		set_bit(primes[i], bits);
	test("a28ac|a28ac", "%20pb|%*pb", bits, 20, bits);
	test("2-3,5,7,11,13,17,19|2-3,5,7,11,13,17,19", "%20pbl|%*pbl", bits, 20, bits);

	bitmap_fill(bits, 20);
	test("fffff|fffff", "%20pb|%*pb", bits, 20, bits);
	test("0-19|0-19", "%20pbl|%*pbl", bits, 20, bits);

	large_bitmap();
}

static void __init
netdev_features(void)
{
}

static void __init
flags(void)
{
	unsigned long flags;
	gfp_t gfp;
	char *cmp_buffer;

	flags = 0;
	test("", "%pGp", &flags);

	/* Page flags should filter the zone id */
	flags = 1UL << NR_PAGEFLAGS;
	test("", "%pGp", &flags);

	flags |= 1UL << PG_uptodate | 1UL << PG_dirty | 1UL << PG_lru
		| 1UL << PG_active | 1UL << PG_swapbacked;
	test("uptodate|dirty|lru|active|swapbacked", "%pGp", &flags);


	flags = VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC
			| VM_DENYWRITE;
	test("read|exec|mayread|maywrite|mayexec|denywrite", "%pGv", &flags);

	gfp = GFP_TRANSHUGE;
	test("GFP_TRANSHUGE", "%pGg", &gfp);

	gfp = GFP_ATOMIC|__GFP_DMA;
	test("GFP_ATOMIC|GFP_DMA", "%pGg", &gfp);

	gfp = __GFP_ATOMIC;
	test("__GFP_ATOMIC", "%pGg", &gfp);

	cmp_buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (!cmp_buffer)
		return;

	/* Any flags not translated by the table should remain numeric */
	gfp = ~__GFP_BITS_MASK;
	snprintf(cmp_buffer, BUF_SIZE, "%#lx", (unsigned long) gfp);
	test(cmp_buffer, "%pGg", &gfp);

	snprintf(cmp_buffer, BUF_SIZE, "__GFP_ATOMIC|%#lx",
							(unsigned long) gfp);
	gfp |= __GFP_ATOMIC;
	test(cmp_buffer, "%pGg", &gfp);

	kfree(cmp_buffer);
}

static void __init
test_pointer(void)
{
	plain();
	symbol_ptr();
	kernel_ptr();
	struct_resource();
	addr();
	escaped_str();
	hex_string();
	mac();
	ip();
	uuid();
	dentry();
	struct_va_format();
	struct_clk();
	bitmap();
	netdev_features();
	flags();
}

static int __init
test_printf_init(void)
{
	alloced_buffer = kmalloc(BUF_SIZE + 2*PAD_SIZE, GFP_KERNEL);
	if (!alloced_buffer)
		return -ENOMEM;
	test_buffer = alloced_buffer + PAD_SIZE;

	test_basic();
	test_number();
	test_string();
	test_pointer();

	kfree(alloced_buffer);

	if (failed_tests == 0)
		pr_info("all %u tests passed\n", total_tests);
	else
		pr_warn("failed %u out of %u tests\n", failed_tests, total_tests);

	return failed_tests ? -EINVAL : 0;
}

module_init(test_printf_init);

MODULE_AUTHOR("Rasmus Villemoes <linux@rasmusvillemoes.dk>");
MODULE_LICENSE("GPL");
