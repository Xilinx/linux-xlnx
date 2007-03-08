#ifndef __NIOS2_CHECKSUM_H
#define __NIOS2_CHECKSUM_H

/*  checksum.h:  IP/UDP/TCP checksum routines on the NIOS.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1995 Miguel de Icaza
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 2001 Ken Hill
 *  Copyright(C) 2004 Microtronix Datacom Ltd.
 *
 * derived from:
 *	Alpha checksum c-code
 *      ix86 inline assembly
 *      Spar nommu
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

extern inline unsigned short csum_tcpudp_magic(unsigned long saddr,
					       unsigned long daddr,
					       unsigned short len,
					       unsigned short proto,
					       unsigned int sum)
{
    barrier();
	__asm__ __volatile__(
"		add	%0, %3, %0\n"
"		bgeu	%0, %3, 1f\n"
"		addi	%0, %0, 1\n"
"1:		add	%0, %4, %0\n"
"		bgeu	%0, %4, 1f\n"
"		addi	%0, %0, 1\n"
"1:		add	%0, %5, %0\n"
"		bgeu	%0, %5, 1f\n"
"		addi	%0, %0, 1\n"
"1:\n"
/*
		We need the carry from the addition of 16-bit
		significant addition, so we zap out the low bits
		in one half, zap out the high bits in another,
		shift them both up to the top 16-bits of a word
		and do the carry producing addition, finally
		shift the result back down to the low 16-bits.

		Actually, we can further optimize away two shifts
		because we know the low bits of the original
		value will be added to zero-only bits so cannot
		affect the addition result nor the final carry
		bit.
*/
"		slli	%1,%0, 16\n"		/* Need a copy to fold with */
						/* Bring the LOW 16 bits up */
"		add	%0, %1, %0\n"		/* add and set carry, neat eh? */
"		cmpltu	r15, %0, %1\n"		/* get remaining carry bit */
"		srli	%0, %0, 16\n"		/* shift back down the result */
"		add	%0, %0, r15\n"
"		nor	%0, %0, %0\n"		/* negate */
	        : "=&r" (sum), "=&r" (saddr)
		: "0" (sum), "1" (saddr), "r" (ntohl(len+proto)), "r" (daddr)
		: "r15");
		return ((unsigned short) sum); 
    barrier();
}


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


  extern inline unsigned short from32to16(unsigned long x)
  {
    barrier();
	__asm__ __volatile__(
		"add	%0, %1, %0\n"
		"cmpltu	r15, %0, %1\n"
		"srli	%0, %0, 16\n"
		"add	%0, %0, r15\n"
		: "=r" (x)
		: "r" (x << 16), "0" (x)
		: "r15");
	return x;
    barrier();
  }


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


extern inline unsigned long do_csum(const unsigned char * buff, int len)
{
 int odd, count;
 unsigned long result = 0;

    barrier();
 if (len <= 0)
 	goto out;
 odd = 1 & (unsigned long) buff;
 if (odd) {
////result = *buff;                     // dgt: Big    endian
 	result = *buff << 8;                // dgt: Little endian

 	len--;
 	buff++;
 }
 count = len >> 1;		/* nr of 16-bit words.. */
 if (count) {
 	if (2 & (unsigned long) buff) {
 		result += *(unsigned short *) buff;
 		count--;
 		len -= 2;
 		buff += 2;
 	}
 	count >>= 1;		/* nr of 32-bit words.. */
 	if (count) {
 	        unsigned long carry = 0;
 		do {
 			unsigned long w = *(unsigned long *) buff;
 			count--;
 			buff += 4;
 			result += carry;
 			result += w;
 			carry = (w > result);
 		} while (count);
 		result += carry;
 		result = (result & 0xffff) + (result >> 16);
 	}
 	if (len & 2) {
 		result += *(unsigned short *) buff;
 		buff += 2;
 	}
 }
 if (len & 1)
 	result += *buff;  /* This is little machine, byte is right */
 result = from32to16(result);
 if (odd)
 	result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
    barrier();
  }


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


/* ihl is always 5 or greater, almost always is 5, iph is always word
 * aligned but can fail to be dword aligned very often.
 */

  extern inline unsigned short ip_fast_csum(const unsigned char *iph, unsigned int ihl)
  {
	unsigned int sum;

    barrier();
	__asm__ __volatile__(
"		andi	r8, %1, 2\n"	/* Remember original alignment */
"		ldw	%0, (%1)\n"	/* 16 or 32 bit boundary */
"		beq	r8, r0, 1f\n"	/* Aligned on 32 bit boundary, go */
"		srli	%0, %0, 16\n"	/* Get correct 16 bits */
"		addi	%2, %2, -1\n"	/* Take off for 4 bytes, pickup last 2 at end */
"		addi	%1, %1, 2\n"	/* Adjust pointer to 32 bit boundary */
"		br	2f\n"
"1:\n"
"		addi	%2, %2, -1\n"
"		addi	%1, %1, 4\n"	/* Bump ptr a long word */
"2:\n"
"		ldw     r9, (%1)\n"
"1:\n"
"		add     %0, r9, %0\n"
"		bgeu	%0, r9, 2f\n"
"		addi	%0, %0, 1\n"
"2:\n"
"		addi	%1, %1, 4\n"
"		addi	%2, %2, -1\n"
"		ldw     r9, (%1)\n"
"		bne	%2, r0, 1b\n"
"		beq	r8, r0, 1f\n"	/* 32 bit boundary time to leave */
"		srli	r9, r9, 16\n"	/* 16 bit boundary, get correct 16 bits */
"		add     %0, r9, %0\n"
"		bgeu	%0, r9, 1f\n"
"		addi	%0, %0, 1\n"
"1:\n"
"		slli	%2, %0, 16\n"
"		add     %0, %2, %0\n"
"		cmpltu	r8, %0, %2\n"
"		srli	%0, %0, 16\n"
"		add	%0, %0, r8\n"
"		nor     %0, %0, %0\n"
		: "=&r" (sum), "=&r" (iph), "=&r" (ihl)
		: "1" (iph), "2" (ihl)
		: "r8", "r9");
	return sum;
    barrier();
  }

/*these 2 functions are now in checksum.c */
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);
unsigned int csum_partial_copy(const char *src, char *dst, int len, int sum);

/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/*
 * the same as csum_partial_copy, but copies from user space.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern inline unsigned int
csum_partial_copy_from_user(const char *src, char *dst, int len, int sum, int *csum_err)
{
    barrier();
	if (csum_err) *csum_err = 0;
	memcpy(dst, src, len);
	return csum_partial(dst, len, sum);
    barrier();
}

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy ((src), (dst), (len), (sum))


/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

extern inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
    barrier();
 return ~from32to16(do_csum(buff,len));
    barrier();
}


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#define csum_partial_copy_fromuser(s, d, l, w)  \
                     csum_partial_copy((char *) (s), (d), (l), (w))


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


/*
 *	Fold a partial checksum without adding pseudo headers
 */
extern __inline__ unsigned int csum_fold(unsigned int sum)
{
    barrier();
	__asm__ __volatile__(
		"add	%0, %1, %0\n"
		"cmpltu	r8, %0, %1\n"
		"srli	%0, %0, 16\n"
		"add	%0, %0, r8\n"
		"nor	%0, %0, %0\n"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum)
		: "r8"); 
	return sum;
    barrier();
}


/*- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


extern __inline__ unsigned long csum_tcpudp_nofold(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
    barrier();
	__asm__ __volatile__(
		"add	%0, %1, %0\n"
		"cmpltu	r8, %0, %1\n"
		"add	%0, %0, r8\n"	/* add carry */
		"add	%0, %2, %0\n"
		"cmpltu	r8, %0, %2\n"
		"add	%0, %0, r8\n"	/* add carry */
		"add	%0, %3, %0\n"
		"cmpltu	r8, %0, %3\n"
		"add	%0, %0, r8\n"	/* add carry */
		: "=r" (sum), "=r" (saddr)
		: "r" (daddr), "r" ( (ntohs(len)<<16) + (proto*256) ),
		  "0" (sum),
		  "1" (saddr)
		: "r8");

	return sum;
    barrier();
}


#endif /* (__NIOS2_CHECKSUM_H) */
