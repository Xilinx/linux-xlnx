#ifndef _ASM_POWERPC_CHECKSUM_H
#define _ASM_POWERPC_CHECKSUM_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef CONFIG_GENERIC_CSUM
#include <asm-generic/checksum.h>
#else
/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit), while copying the block to dst.
 * If an access exception occurs on src or dst, it stores -EFAULT
 * to *src_err or *dst_err respectively (if that pointer is not
 * NULL), and, for an error on src, zeroes the rest of dst.
 *
 * Like csum_partial, this must be called with even lengths,
 * except for the last fragment.
 */
extern __wsum csum_partial_copy_generic(const void *src, void *dst,
					      int len, __wsum sum,
					      int *src_err, int *dst_err);

#define _HAVE_ARCH_COPY_AND_CSUM_FROM_USER
extern __wsum csum_and_copy_from_user(const void __user *src, void *dst,
				      int len, __wsum sum, int *err_ptr);
#define HAVE_CSUM_COPY_USER
extern __wsum csum_and_copy_to_user(const void *src, void __user *dst,
				    int len, __wsum sum, int *err_ptr);

#define csum_partial_copy_nocheck(src, dst, len, sum)   \
        csum_partial_copy_generic((src), (dst), (len), (sum), NULL, NULL)


/*
 * turns a 32-bit partial checksum (e.g. from csum_partial) into a
 * 1's complement 16-bit checksum.
 */
static inline __sum16 csum_fold(__wsum sum)
{
	unsigned int tmp;

	/* swap the two 16-bit halves of sum */
	__asm__("rlwinm %0,%1,16,0,31" : "=r" (tmp) : "r" (sum));
	/* if there is a carry from adding the two 16-bit halves,
	   it will carry from the lower half into the upper half,
	   giving us the correct sum in the upper half. */
	return (__force __sum16)(~((__force u32)sum + tmp) >> 16);
}

static inline __wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr, __u32 len,
					__u8 proto, __wsum sum)
{
#ifdef __powerpc64__
	unsigned long s = (__force u32)sum;

	s += (__force u32)saddr;
	s += (__force u32)daddr;
	s += proto + len;
	s += (s >> 32);
	return (__force __wsum) s;
#else
    __asm__("\n\
	addc %0,%0,%1 \n\
	adde %0,%0,%2 \n\
	adde %0,%0,%3 \n\
	addze %0,%0 \n\
	"
	: "=r" (sum)
	: "r" (daddr), "r"(saddr), "r"(proto + len), "0"(sum));
	return sum;
#endif
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline __sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr, __u32 len,
					__u8 proto, __wsum sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

#define HAVE_ARCH_CSUM_ADD
static inline __wsum csum_add(__wsum csum, __wsum addend)
{
#ifdef __powerpc64__
	u64 res = (__force u64)csum;
#endif
	if (__builtin_constant_p(csum) && csum == 0)
		return addend;
	if (__builtin_constant_p(addend) && addend == 0)
		return csum;

#ifdef __powerpc64__
	res += (__force u64)addend;
	return (__force __wsum)((u32)res + (res >> 32));
#else
	asm("addc %0,%0,%1;"
	    "addze %0,%0;"
	    : "+r" (csum) : "r" (addend) : "xer");
	return csum;
#endif
}

/*
 * This is a version of ip_compute_csum() optimized for IP headers,
 * which always checksum on 4 octet boundaries.  ihl is the number
 * of 32-bit words and is always >= 5.
 */
static inline __wsum ip_fast_csum_nofold(const void *iph, unsigned int ihl)
{
	const u32 *ptr = (const u32 *)iph + 1;
#ifdef __powerpc64__
	unsigned int i;
	u64 s = *(const u32 *)iph;

	for (i = 0; i < ihl - 1; i++, ptr++)
		s += *ptr;
	s += (s >> 32);
	return (__force __wsum)s;
#else
	__wsum sum, tmp;

	asm("mtctr %3;"
	    "addc %0,%4,%5;"
	    "1: lwzu %1, 4(%2);"
	    "adde %0,%0,%1;"
	    "bdnz 1b;"
	    "addze %0,%0;"
	    : "=r" (sum), "=r" (tmp), "+b" (ptr)
	    : "r" (ihl - 2), "r" (*(const u32 *)iph), "r" (*ptr)
	    : "ctr", "xer", "memory");

	return sum;
#endif
}

static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	return csum_fold(ip_fast_csum_nofold(iph, ihl));
}

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
__wsum __csum_partial(const void *buff, int len, __wsum sum);

static inline __wsum csum_partial(const void *buff, int len, __wsum sum)
{
	if (__builtin_constant_p(len) && len <= 16 && (len & 1) == 0) {
		if (len == 2)
			sum = csum_add(sum, (__force __wsum)*(const u16 *)buff);
		if (len >= 4)
			sum = csum_add(sum, (__force __wsum)*(const u32 *)buff);
		if (len == 6)
			sum = csum_add(sum, (__force __wsum)
					    *(const u16 *)(buff + 4));
		if (len >= 8)
			sum = csum_add(sum, (__force __wsum)
					    *(const u32 *)(buff + 4));
		if (len == 10)
			sum = csum_add(sum, (__force __wsum)
					    *(const u16 *)(buff + 8));
		if (len >= 12)
			sum = csum_add(sum, (__force __wsum)
					    *(const u32 *)(buff + 8));
		if (len == 14)
			sum = csum_add(sum, (__force __wsum)
					    *(const u16 *)(buff + 12));
		if (len >= 16)
			sum = csum_add(sum, (__force __wsum)
					    *(const u32 *)(buff + 12));
	} else if (__builtin_constant_p(len) && (len & 3) == 0) {
		sum = csum_add(sum, ip_fast_csum_nofold(buff, len >> 2));
	} else {
		sum = __csum_partial(buff, len, sum);
	}
	return sum;
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline __sum16 ip_compute_csum(const void *buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#endif
#endif /* __KERNEL__ */
#endif
