/* 
LzmaDecode.h
LZMA Decoder interface
LZMA SDK 4.01 Copyright (c) 1999-2004 Igor Pavlov (2004-02-15)
*/

#ifndef __LZMADECODE_H
#define __LZMADECODE_H

/* #define _LZMA_IN_CB */
/* Use callback for input data */

/* #define _LZMA_OUT_READ */
/* Use read function for output data */

/* #define _LZMA_PROB32 */
/* It can increase speed on some 32-bit CPUs, 
   but memory usage will be doubled in that case */

/* #define _LZMA_LOC_OPT */
/* Enable local speed optimizations inside code */

#ifndef UInt32
#ifdef _LZMA_UINT32_IS_ULONG
#define UInt32 unsigned long
#else
#define UInt32 unsigned int
#endif
#endif

#ifdef _LZMA_PROB32
#define CProb UInt32
#else
#define CProb unsigned short
#endif

#define LZMA_RESULT_OK 0
#define LZMA_RESULT_DATA_ERROR 1
#define LZMA_RESULT_NOT_ENOUGH_MEM 2

#ifdef _LZMA_IN_CB
typedef struct _ILzmaInCallback
{
  int (*Read)(void *object, unsigned char **buffer, UInt32 *bufferSize);
} ILzmaInCallback;
#endif

#define LZMA_BASE_SIZE 1846
#define LZMA_LIT_SIZE 768

/* 
bufferSize = (LZMA_BASE_SIZE + (LZMA_LIT_SIZE << (lc + lp)))* sizeof(CProb)
bufferSize += 100 in case of _LZMA_OUT_READ
by default CProb is unsigned short, 
but if specify _LZMA_PROB_32, CProb will be UInt32(unsigned int)
*/

#ifdef _LZMA_OUT_READ
int LzmaDecoderInit(
    unsigned char *buffer, UInt32 bufferSize,
    int lc, int lp, int pb,
    unsigned char *dictionary, UInt32 dictionarySize,
  #ifdef _LZMA_IN_CB
    ILzmaInCallback *inCallback
  #else
    unsigned char *inStream, UInt32 inSize
  #endif
);
#endif

int LzmaDecode(
    unsigned char *buffer, 
  #ifndef _LZMA_OUT_READ
    UInt32 bufferSize,
    int lc, int lp, int pb,
  #ifdef _LZMA_IN_CB
    ILzmaInCallback *inCallback,
  #else
    unsigned char *inStream, UInt32 inSize,
  #endif
  #endif
    unsigned char *outStream, UInt32 outSize,
    UInt32 *outSizeProcessed);

#endif
