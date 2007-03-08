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

void *memset(void *d, int k, __kernel_size_t c)
{
  void *r = d;
  unsigned int wk;

  /* Truncate k to 8 bits */
  wk=k=(k & 0xFF);

  /* Make a repeating word out of it */
  wk |= wk << 8;
  wk |= wk << 8;
  wk |= wk << 8;

  if (c >= 4)
  {
	unsigned x, a, h, align;

	/* Align the destination to a word boundry. */
	/* This is done in an endian independant manner. */
	switch ((unsigned) d & 3)
	{
	 case 1: *((char *)d)++ = k; c--;
	 case 2: *((char *)d)++ = k; c--;
	 case 3: *((char *)d)++ = k; c--;  
	}

        /* Do as many full-word copies as we can */
	for (; c >= 4; c -= 4)
          *((unsigned *)d)++ = wk;

  }

  /* Finish off the rest as byte copies */
  switch(c)
  {
    case 3: *((char *)d)++ = k;	/* fall through */
    case 2: *((char *)d)++ = k;
    case 1: *((char *)d)++ = k;
  }
  return r;
}


