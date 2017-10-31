/******************************************************************************
*
 *
 * Copyright (C) 2015, 2016, 2017 Xilinx, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details
*
******************************************************************************/
/*****************************************************************************/
/**
* @file sha2.c
*
* This file contains the implementation of the SHA-2 Secure Hashing Algorithm.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date     Changes
* ----- ---- -------- -----------------------------------------------
* 1.00  MH   10/30/15 First Release
*</pre>
*
*****************************************************************************/

/***************************** Include Files ********************************/
#include <linux/string.h>
#include "xil_types.h"

/**************************** Type Definitions ******************************/
typedef struct {
   u8 data[64];
   u32 datalen;
   u32 bitlen[2];
   u32 state[8];
} Sha256Type;

/***************** Macros (Inline Functions) Definitions ********************/
// DBL_INT_ADD treats two unsigned ints a and b as one 64-bit integer and adds c to it
#define DBL_INT_ADD(a,b,c) if (a > 0xffffffff - (c)) ++b; a += c;
#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

/************************** Variable Definitions ****************************/
static const u32 k[64] = {
   0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
   0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
   0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
   0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
   0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
   0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
   0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
   0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/************************** Function Prototypes *****************************/

/* SHA-256 Hashing */
static void Sha256Transform(Sha256Type *Ctx, u8 *Data);
static void Sha256Init(Sha256Type *Ctx);
static void Sha256Update(Sha256Type *Ctx, const u8 *Data, u32 Len);
static void Sha256Final(Sha256Type *Ctx, u8 *Hash);

/************************** Function Implementation *****************************/

/*****************************************************************************/
/**
*
* This function computes a SHA256 hash on a array of data.
*
* @param  Data is the data on which a hash is calculated.
* @param  DataSize is the size of the data array..
* @param  HashedData is a 256-bits size hash.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
void XHdcp22Cmn_Sha256Hash(const u8 *Data, u32 DataSize, u8 *HashedData)
{
	Sha256Type Ctx;

	Sha256Init(&Ctx);

	Sha256Update(&Ctx, Data, DataSize);
	Sha256Final(&Ctx, HashedData);
}

/*****************************************************************************/
/**
* This function executes a SHA256 transformation.
*
* @param  Ctx is the context data for SHA256.
* @param  Data is the data to transform.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void Sha256Transform(Sha256Type *Ctx, u8 *Data)
{
   u32 a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];

   for (i=0,j=0; i < 16; ++i, j += 4)
      m[i] = (Data[j] << 24) | (Data[j+1] << 16) | (Data[j+2] << 8) | (Data[j+3]);
   for ( ; i < 64; ++i)
      m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];

   a = Ctx->state[0];
   b = Ctx->state[1];
   c = Ctx->state[2];
   d = Ctx->state[3];
   e = Ctx->state[4];
   f = Ctx->state[5];
   g = Ctx->state[6];
   h = Ctx->state[7];

   for (i = 0; i < 64; ++i) {
      t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
      t2 = EP0(a) + MAJ(a,b,c);
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
   }

   Ctx->state[0] += a;
   Ctx->state[1] += b;
   Ctx->state[2] += c;
   Ctx->state[3] += d;
   Ctx->state[4] += e;
   Ctx->state[5] += f;
   Ctx->state[6] += g;
   Ctx->state[7] += h;
}

/*****************************************************************************/
/**
* This function initializes the context data for a SHA 256 hash calculation.
*
* @param  Ctx is the context data for SHA256.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void Sha256Init(Sha256Type *Ctx)
{
   Ctx->datalen = 0;
   Ctx->bitlen[0] = 0;
   Ctx->bitlen[1] = 0;
   Ctx->state[0] = 0x6a09e667;
   Ctx->state[1] = 0xbb67ae85;
   Ctx->state[2] = 0x3c6ef372;
   Ctx->state[3] = 0xa54ff53a;
   Ctx->state[4] = 0x510e527f;
   Ctx->state[5] = 0x9b05688c;
   Ctx->state[6] = 0x1f83d9ab;
   Ctx->state[7] = 0x5be0cd19;
}

/*****************************************************************************/
/**
*
* This function updates the SHA data before adding padding data.
*
* @param  Ctx is the context data for SHA256.
* @param  Data is the input data.
* @param  Len is size of the input data array.
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void Sha256Update(Sha256Type *Ctx, const u8 *Data, u32 Len)
{
   u32 i;

   for (i=0; i < Len; ++i) {
      Ctx->data[Ctx->datalen] = Data[i];
      Ctx->datalen++;
      if (Ctx->datalen == 64) {
	  Sha256Transform(Ctx,Ctx->data);
         DBL_INT_ADD(Ctx->bitlen[0],Ctx->bitlen[1],512);
         Ctx->datalen = 0;
      }
   }
}

/*****************************************************************************/
/**
*
* This function adds padding
*
* @param  Ctx is the context data for SHA256.
* @param  Hash is the calculated hash (256-bits).
*
* @return None.
*
* @note   None.
*
******************************************************************************/
static void Sha256Final(Sha256Type *Ctx, u8 *Hash)
{
   u32 i;

   i = Ctx->datalen;

   // Pad whatever data is left in the buffer.
   if (Ctx->datalen < 56) {
      Ctx->data[i++] = 0x80;
      while (i < 56)
         Ctx->data[i++] = 0x00;
   }
   else {
      Ctx->data[i++] = 0x80;
      while (i < 64)
         Ctx->data[i++] = 0x00;
      Sha256Transform(Ctx,Ctx->data);
      memset(Ctx->data,0,56);
   }

   // Append to the padding the total message's length in bits and transform.
   DBL_INT_ADD(Ctx->bitlen[0],Ctx->bitlen[1],Ctx->datalen * 8);
   Ctx->data[63] = Ctx->bitlen[0];
   Ctx->data[62] = Ctx->bitlen[0] >> 8;
   Ctx->data[61] = Ctx->bitlen[0] >> 16;
   Ctx->data[60] = Ctx->bitlen[0] >> 24;
   Ctx->data[59] = Ctx->bitlen[1];
   Ctx->data[58] = Ctx->bitlen[1] >> 8;
   Ctx->data[57] = Ctx->bitlen[1] >> 16;
   Ctx->data[56] = Ctx->bitlen[1] >> 24;
   Sha256Transform(Ctx,Ctx->data);

   // Since this implementation uses little endian byte ordering and SHA uses big endian,
   // reverse all the bytes when copying the final state to the output hash.
   for (i=0; i < 4; ++i) {
      Hash[i]    = (Ctx->state[0] >> (24-i*8)) & 0x000000ff;
      Hash[i+4]  = (Ctx->state[1] >> (24-i*8)) & 0x000000ff;
      Hash[i+8]  = (Ctx->state[2] >> (24-i*8)) & 0x000000ff;
      Hash[i+12] = (Ctx->state[3] >> (24-i*8)) & 0x000000ff;
      Hash[i+16] = (Ctx->state[4] >> (24-i*8)) & 0x000000ff;
      Hash[i+20] = (Ctx->state[5] >> (24-i*8)) & 0x000000ff;
      Hash[i+24] = (Ctx->state[6] >> (24-i*8)) & 0x000000ff;
      Hash[i+28] = (Ctx->state[7] >> (24-i*8)) & 0x000000ff;
   }
}
