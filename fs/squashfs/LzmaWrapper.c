/* 
   LzmaTest.c
   Test application for LZMA Decoder
   LZMA SDK 4.01 Copyright (c) 1999-2004 Igor Pavlov (2004-02-15)
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include "LzmaDecode.h"
#include "LzmaWrapper.h"

static int	internal_size;
static unsigned char	*internal_data=0;
//static int	dictionary_size;

int lzma_workspace_size(void)
{
//	int pb=2;
	int lp=0;
	int lc=3;

	return (LZMA_BASE_SIZE + (LZMA_LIT_SIZE << (lc + lp)))* sizeof(CProb);
}

int lzma_init(unsigned char *data, int size)
{
	internal_data=data;
	internal_size=size;
	return 0;
}

int lzma_inflate(
	unsigned char *source,
	int s_len,
	unsigned char *dest,
	int *d_len)
{
	/* Get the properties */
	unsigned int check_internal_size;
	unsigned char properties[5];
	unsigned char prop0;
	int lc, lp, pb;
	int res, i;
	unsigned int dictionary_size = 0;
	unsigned int encoded_size;

	if(s_len<5){
		/* Can't even get properities, just exit */
		return LZMA_ERROR;
	}

	/* Get the size of the uncompressed buffer */
	encoded_size =
		source[0] | (source[1] << 8) | (source[2] << 16) | (source[3] << 24);
	source+=4;

	/* We use this to check that the size of internal data structures
	   are big enough. If it isn't, then we flag it. */
	memcpy(properties, source, sizeof(properties));
	prop0 = properties[0];
	if (prop0 >= (9*5*5))
		return LZMA_ERROR;
	for (pb = 0; prop0 >= (9 * 5); 
			pb++, prop0 -= (9 * 5));
	for (lp = 0; prop0 >= 9; 
			lp++, prop0 -= 9);
	lc = prop0;

	source += 5;

	check_internal_size = 
		(LZMA_BASE_SIZE + (LZMA_LIT_SIZE << (lc + lp)))* sizeof(CProb);

	for (i = 0; i < 4; i++)
		dictionary_size += (UInt32)(properties[1 + i]) << (i * 8);

	if(check_internal_size > internal_size){
		printk("internal_size = %d, header size = %d\n", internal_size, check_internal_size);
		printk("lc = %d, lp=%d, pb=%d\n", lc, lp, pb);
		printk("byte=%x, dictionary size = %8.8x\n", prop0, dictionary_size);
		return LZMA_TOO_BIG;
	}

	res = LzmaDecode(internal_data, internal_size,
			lc, lp, pb,
			(unsigned char *)source, s_len,
			(unsigned char *)dest, encoded_size, d_len);

	return res;
}
