/*
 * crc32.h
 * See linux/lib/crc32.c for license and changes
 */
#ifndef _LINUX_CRC32_H
#define _LINUX_CRC32_H

#include <linux/types.h>
#include <linux/bitrev.h>

extern u32  crc32_le(u32 crc, unsigned char const *p, size_t len);
extern u32  crc32_be(u32 crc, unsigned char const *p, size_t len);

/**
 * crc32_le_combine - Combine two crc32 check values into one. For two
 * 		      sequences of bytes, seq1 and seq2 with lengths len1
 * 		      and len2, crc32_le() check values were calculated
 * 		      for each, crc1 and crc2.
 *
 * @crc1: crc32 of the first block
 * @crc2: crc32 of the second block
 * @len2: length of the second block
 *
 * Return: The crc32_le() check value of seq1 and seq2 concatenated,
 * 	   requiring only crc1, crc2, and len2. Note: If seq_full denotes
 * 	   the concatenated memory area of seq1 with seq2, and crc_full
 * 	   the crc32_le() value of seq_full, then crc_full ==
 * 	   crc32_le_combine(crc1, crc2, len2) when crc_full was seeded
 * 	   with the same initializer as crc1, and crc2 seed was 0. See
 * 	   also crc32_combine_test().
 */
extern u32  crc32_le_combine(u32 crc1, u32 crc2, size_t len2);

extern u32  __crc32c_le(u32 crc, unsigned char const *p, size_t len);

/**
 * __crc32c_le_combine - Combine two crc32c check values into one. For two
 * 			 sequences of bytes, seq1 and seq2 with lengths len1
 * 			 and len2, __crc32c_le() check values were calculated
 * 			 for each, crc1 and crc2.
 *
 * @crc1: crc32c of the first block
 * @crc2: crc32c of the second block
 * @len2: length of the second block
 *
 * Return: The __crc32c_le() check value of seq1 and seq2 concatenated,
 * 	   requiring only crc1, crc2, and len2. Note: If seq_full denotes
 * 	   the concatenated memory area of seq1 with seq2, and crc_full
 * 	   the __crc32c_le() value of seq_full, then crc_full ==
 * 	   __crc32c_le_combine(crc1, crc2, len2) when crc_full was
 * 	   seeded with the same initializer as crc1, and crc2 seed
 * 	   was 0. See also crc32c_combine_test().
 */
extern u32  __crc32c_le_combine(u32 crc1, u32 crc2, size_t len2);

#define crc32(seed, data, length)  crc32_le(seed, (unsigned char const *)(data), length)

/*
 * Helpers for hash table generation of ethernet nics:
 *
 * Ethernet sends the least significant bit of a byte first, thus crc32_le
 * is used. The output of crc32_le is bit reversed [most significant bit
 * is in bit nr 0], thus it must be reversed before use. Except for
 * nics that bit swap the result internally...
 */
#define ether_crc(length, data)    bitrev32(crc32_le(~0, data, length))
#define ether_crc_le(length, data) crc32_le(~0, data, length)

#endif /* _LINUX_CRC32_H */
