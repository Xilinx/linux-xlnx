/* Filename: memset.c
 *
 * Reasonably optimised generic C-code for memset on Microblaze
 * This is generic C code to do efficient, alignment-aware memcpy.
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

/* Filename: memcpy.c
 *
 * Reasonably optimised generic C-code for memset on Microblaze
 * Based on demo code originally Copyright 2001 by Intel Corp.
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

void *memset(void *s, int c, __kernel_size_t n)
{
	void *r = s;
	unsigned int w32;

	/* Truncate c to 8 bits */
	w32 = c = (c & 0xFF);

	/* Make a repeating word out of it */
	w32 |= w32 << 8;
	w32 |= w32 << 8;
	w32 |= w32 << 8;

	if (n >= 4) {
		/* Align the destination to a word boundary. */
		/* This is done in an endian independant manner. */
		switch ((unsigned) s & 3) {
		case 1: *(char *)s = c; s = (char *)s+1; n--;
		case 2: *(char *)s = c; s = (char *)s+1; n--;
		case 3: *(char *)s = c; s = (char *)s+1; n--;
		}

		/* Do as many full-word copies as we can */
		for (; n >= 4; n -= 4) {
			*(unsigned *)s = w32;
			s = (unsigned *)s +1;
		}

	}

	/* Finish off the rest as byte sets */
	while (n--) {
		*(char *)s = c;
		s = (char *)s+1;
	}
	return r;
}

