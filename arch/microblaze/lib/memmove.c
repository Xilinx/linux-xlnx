/* Filename: memmove.c
 *
 * Reasonably optimised generic C-code for memcpy on Microblaze
 * This is generic C code to do efficient, alignment-aware memmove.
 *
 * It is based on demo code originally Copyright 2001 by Intel Corp, taken from
 * http://www.embedded.com/showArticle.jhtml?articleID=19205567
 * 
 * Attempts were made, unsuccesfully, to contact the original 
 * author of this code (Michael Morrow, Intel).  Below is the original
 * copyright notice.
 *
 * This software has been developed by Intel Corporation.
 * Intel specifically disclaims all warranties, express or
 * implied, and all liability, including consequential and
 * other indirect damages, for the use of this program, including
 * liability for infringement of any proprietary rights,
 * and including the warranties of merchantability and fitness
 * for a particular purpose. Intel does not assume any
 * responsibility for and errors which may appear in this program
 * not any responsibility to update it.
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/compiler.h>

#include <asm/string.h>

/* Macro's for bytebliting unaligned data blocks */

/* Big-endian MACROS */

#define BYTE_BLIT_INIT(s,h,o) \
(h) = *(--(unsigned *)(s)) >> (32-(o))

#define BYTE_BLIT_STEP(d,s,h,o) \
{ register unsigned _v_; _v_ = *(--(unsigned *)(s)); \
 *(--(unsigned *)(d)) = _v_ << (o) | (h); \
 (h) = _v_ >> (32-(o)); \
}

void *memmove(void *d, const void *s, __kernel_size_t c)
{
  void *r = d;

  /* Use memcpy when source is higher than dest */
  if(d <= s)
    return memcpy(d,s,c);

  /* Do a descending copy - this is a bit trickier! */
  d += c;
  s += c;

  if (c >= 4)
  {
	unsigned x, a, h, align;

	/* Align the destination to a word boundry. */
	/* This is done in an endian independant manner. */
	switch ((unsigned) d & 3)
	{
         case 3: *(--(char *)d) = *(--(char *)s); c--;
         case 2: *(--(char *)d) = *(--(char *)s); c--;
         case 1: *(--(char *)d) = *(--(char *)s); c--;
	}
	/* Choose a copy scheme based on the source */
	/* alignment relative to destination. */
	switch((unsigned) s & 3)
	{
	 case 0x0: /* Both byte offsets are aligned */

	   for (; c >= 4; c -= 4)
             *(--(unsigned *)d) = *(--(unsigned *)s);

	   break;

	 case 0x1: /* Unaligned - Off by 1 */
	   /* Word align the source */
	   a = (unsigned) (s+4) & ~3;

	   /* Load the holding buffer */
	   BYTE_BLIT_INIT(a,h,8);

	   for (; c >= 4; c -= 4)
             BYTE_BLIT_STEP(d,a,h,8);

	   /* Realign the source */
	   (unsigned) s = a + 1;
	   break;

	 case 0x2: /* Unaligned - Off by 2 */
	   /* Word align the source */
	   a = (unsigned) (s+4) & ~3;

	   /* Load the holding buffer */
	   BYTE_BLIT_INIT(a,h,16);

	   for (; c >= 4; c -= 4)
             BYTE_BLIT_STEP(d,a,h,16);

	   /* Realign the source */
	   (unsigned) s = a + 2;
	   break;

	 case 0x3: /* Unaligned - Off by 3 */
	   /* Word align the source */
	   a = (unsigned) (s+4) & ~3;

	   /* Load the holding buffer */
	   BYTE_BLIT_INIT(a,h,24);

	   for (; c >= 4; c -= 4)
             BYTE_BLIT_STEP(d,a,h,24);

	   /* Realign the source */
	   (unsigned) s = a + 3;
	   break;
	}

#if 0
	/* Finish off any remaining bytes */
	c &= 3;
	goto finish;
#endif

  }

	/* simple fast copy, ... unless a cache boundry is crossed */
	switch(c)
	{
	 case 4: *(--(char *)d) = *(--(char *)s);
	 case 3: *(--(char *)d) = *(--(char *)s);
	 case 2: *(--(char *)d) = *(--(char *)s);
	 case 1: *(--(char *)d) = *(--(char *)s);
	}
  return r;
}

