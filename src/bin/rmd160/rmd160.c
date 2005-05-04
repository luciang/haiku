/********************************************************************\
 *  FILE:     rmd160.c
 *  CONTENTS: A sample C-implementation of the RIPEMD-160 hash-function.
 *  TARGET:   any computer with an ANSI C compiler
 *  AUTHOR:   Antoon Bosselaers, Dept. Electrical Eng.-ESAT/COSIC
 *  DATE:     1 March 1996       VERSION:  1.0
 **********************************************************************
 * Copyright (c) Katholieke Universiteit Leuven 1996, All Rights Reserved
 * The Katholieke Universiteit Leuven makes no representations concerning
 * either the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is" without
 * express or implied warranty of any kind. These notices must be retained
 * in any copies of any part of this documentation and/or software.
\********************************************************************/

/* header files */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rmd160.h"

/********************************************************************/
void MDinit(word *MDbuf)
/* Initialization of the 5-word MDbuf array to the magic
   initialization constants
 */
{
   MDbuf[0] = 0x67452301UL;
   MDbuf[1] = 0xefcdab89UL;
   MDbuf[2] = 0x98badcfeUL;
   MDbuf[3] = 0x10325476UL;
   MDbuf[4] = 0xc3d2e1f0UL;
}

/********************************************************************/
void MDcompress(word *MDbuf, word *X)
/* The compression function is called for every complete 64-byte
   message block. The 5-word internal state MDbuf is updated using
   message words X[0] through X[15]. The conversion from a string
   of 64 bytes to an array of 16 words using a Little-endian
   convention is the responsibility of the calling function.
*/
{
   /* make two copies of the old state */
   word aa = MDbuf[0],  bb = MDbuf[1],  cc = MDbuf[2],
        dd = MDbuf[3],  ee = MDbuf[4];
   word aaa = MDbuf[0], bbb = MDbuf[1], ccc = MDbuf[2],
        ddd = MDbuf[3], eee = MDbuf[4];

   /* round 1 */
   FF1(aa, bb, cc, dd, ee, X[ 0], 11);
   FF1(ee, aa, bb, cc, dd, X[ 1], 14);
   FF1(dd, ee, aa, bb, cc, X[ 2], 15);
   FF1(cc, dd, ee, aa, bb, X[ 3], 12);
   FF1(bb, cc, dd, ee, aa, X[ 4],  5);
   FF1(aa, bb, cc, dd, ee, X[ 5],  8);
   FF1(ee, aa, bb, cc, dd, X[ 6],  7);
   FF1(dd, ee, aa, bb, cc, X[ 7],  9);
   FF1(cc, dd, ee, aa, bb, X[ 8], 11);
   FF1(bb, cc, dd, ee, aa, X[ 9], 13);
   FF1(aa, bb, cc, dd, ee, X[10], 14);
   FF1(ee, aa, bb, cc, dd, X[11], 15);
   FF1(dd, ee, aa, bb, cc, X[12],  6);
   FF1(cc, dd, ee, aa, bb, X[13],  7);
   FF1(bb, cc, dd, ee, aa, X[14],  9);
   FF1(aa, bb, cc, dd, ee, X[15],  8);

   /* round 2 */
   FF2(ee, aa, bb, cc, dd, X[ 7],  7);
   FF2(dd, ee, aa, bb, cc, X[ 4],  6);
   FF2(cc, dd, ee, aa, bb, X[13],  8);
   FF2(bb, cc, dd, ee, aa, X[ 1], 13);
   FF2(aa, bb, cc, dd, ee, X[10], 11);
   FF2(ee, aa, bb, cc, dd, X[ 6],  9);
   FF2(dd, ee, aa, bb, cc, X[15],  7);
   FF2(cc, dd, ee, aa, bb, X[ 3], 15);
   FF2(bb, cc, dd, ee, aa, X[12],  7);
   FF2(aa, bb, cc, dd, ee, X[ 0], 12);
   FF2(ee, aa, bb, cc, dd, X[ 9], 15);
   FF2(dd, ee, aa, bb, cc, X[ 5],  9);
   FF2(cc, dd, ee, aa, bb, X[ 2], 11);
   FF2(bb, cc, dd, ee, aa, X[14],  7);
   FF2(aa, bb, cc, dd, ee, X[11], 13);
   FF2(ee, aa, bb, cc, dd, X[ 8], 12);

   /* round 3 */
   FF3(dd, ee, aa, bb, cc, X[ 3], 11);
   FF3(cc, dd, ee, aa, bb, X[10], 13);
   FF3(bb, cc, dd, ee, aa, X[14],  6);
   FF3(aa, bb, cc, dd, ee, X[ 4],  7);
   FF3(ee, aa, bb, cc, dd, X[ 9], 14);
   FF3(dd, ee, aa, bb, cc, X[15],  9);
   FF3(cc, dd, ee, aa, bb, X[ 8], 13);
   FF3(bb, cc, dd, ee, aa, X[ 1], 15);
   FF3(aa, bb, cc, dd, ee, X[ 2], 14);
   FF3(ee, aa, bb, cc, dd, X[ 7],  8);
   FF3(dd, ee, aa, bb, cc, X[ 0], 13);
   FF3(cc, dd, ee, aa, bb, X[ 6],  6);
   FF3(bb, cc, dd, ee, aa, X[13],  5);
   FF3(aa, bb, cc, dd, ee, X[11], 12);
   FF3(ee, aa, bb, cc, dd, X[ 5],  7);
   FF3(dd, ee, aa, bb, cc, X[12],  5);

   /* round 4 */
   FF4(cc, dd, ee, aa, bb, X[ 1], 11);
   FF4(bb, cc, dd, ee, aa, X[ 9], 12);
   FF4(aa, bb, cc, dd, ee, X[11], 14);
   FF4(ee, aa, bb, cc, dd, X[10], 15);
   FF4(dd, ee, aa, bb, cc, X[ 0], 14);
   FF4(cc, dd, ee, aa, bb, X[ 8], 15);
   FF4(bb, cc, dd, ee, aa, X[12],  9);
   FF4(aa, bb, cc, dd, ee, X[ 4],  8);
   FF4(ee, aa, bb, cc, dd, X[13],  9);
   FF4(dd, ee, aa, bb, cc, X[ 3], 14);
   FF4(cc, dd, ee, aa, bb, X[ 7],  5);
   FF4(bb, cc, dd, ee, aa, X[15],  6);
   FF4(aa, bb, cc, dd, ee, X[14],  8);
   FF4(ee, aa, bb, cc, dd, X[ 5],  6);
   FF4(dd, ee, aa, bb, cc, X[ 6],  5);
   FF4(cc, dd, ee, aa, bb, X[ 2], 12);

   /* round 5 */
   FF5(bb, cc, dd, ee, aa, X[ 4],  9);
   FF5(aa, bb, cc, dd, ee, X[ 0], 15);
   FF5(ee, aa, bb, cc, dd, X[ 5],  5);
   FF5(dd, ee, aa, bb, cc, X[ 9], 11);
   FF5(cc, dd, ee, aa, bb, X[ 7],  6);
   FF5(bb, cc, dd, ee, aa, X[12],  8);
   FF5(aa, bb, cc, dd, ee, X[ 2], 13);
   FF5(ee, aa, bb, cc, dd, X[10], 12);
   FF5(dd, ee, aa, bb, cc, X[14],  5);
   FF5(cc, dd, ee, aa, bb, X[ 1], 12);
   FF5(bb, cc, dd, ee, aa, X[ 3], 13);
   FF5(aa, bb, cc, dd, ee, X[ 8], 14);
   FF5(ee, aa, bb, cc, dd, X[11], 11);
   FF5(dd, ee, aa, bb, cc, X[ 6],  8);
   FF5(cc, dd, ee, aa, bb, X[15],  5);
   FF5(bb, cc, dd, ee, aa, X[13],  6);

   /* parallel round 1 */
   FFF5(aaa, bbb, ccc, ddd, eee, X[ 5],  8);
   FFF5(eee, aaa, bbb, ccc, ddd, X[14],  9);
   FFF5(ddd, eee, aaa, bbb, ccc, X[ 7],  9);
   FFF5(ccc, ddd, eee, aaa, bbb, X[ 0], 11);
   FFF5(bbb, ccc, ddd, eee, aaa, X[ 9], 13);
   FFF5(aaa, bbb, ccc, ddd, eee, X[ 2], 15);
   FFF5(eee, aaa, bbb, ccc, ddd, X[11], 15);
   FFF5(ddd, eee, aaa, bbb, ccc, X[ 4],  5);
   FFF5(ccc, ddd, eee, aaa, bbb, X[13],  7);
   FFF5(bbb, ccc, ddd, eee, aaa, X[ 6],  7);
   FFF5(aaa, bbb, ccc, ddd, eee, X[15],  8);
   FFF5(eee, aaa, bbb, ccc, ddd, X[ 8], 11);
   FFF5(ddd, eee, aaa, bbb, ccc, X[ 1], 14);
   FFF5(ccc, ddd, eee, aaa, bbb, X[10], 14);
   FFF5(bbb, ccc, ddd, eee, aaa, X[ 3], 12);
   FFF5(aaa, bbb, ccc, ddd, eee, X[12],  6);

   /* parallel round 2 */
   FFF4(eee, aaa, bbb, ccc, ddd, X[ 6],  9);
   FFF4(ddd, eee, aaa, bbb, ccc, X[11], 13);
   FFF4(ccc, ddd, eee, aaa, bbb, X[ 3], 15);
   FFF4(bbb, ccc, ddd, eee, aaa, X[ 7],  7);
   FFF4(aaa, bbb, ccc, ddd, eee, X[ 0], 12);
   FFF4(eee, aaa, bbb, ccc, ddd, X[13],  8);
   FFF4(ddd, eee, aaa, bbb, ccc, X[ 5],  9);
   FFF4(ccc, ddd, eee, aaa, bbb, X[10], 11);
   FFF4(bbb, ccc, ddd, eee, aaa, X[14],  7);
   FFF4(aaa, bbb, ccc, ddd, eee, X[15],  7);
   FFF4(eee, aaa, bbb, ccc, ddd, X[ 8], 12);
   FFF4(ddd, eee, aaa, bbb, ccc, X[12],  7);
   FFF4(ccc, ddd, eee, aaa, bbb, X[ 4],  6);
   FFF4(bbb, ccc, ddd, eee, aaa, X[ 9], 15);
   FFF4(aaa, bbb, ccc, ddd, eee, X[ 1], 13);
   FFF4(eee, aaa, bbb, ccc, ddd, X[ 2], 11);

   /* parallel round 3 */
   FFF3(ddd, eee, aaa, bbb, ccc, X[15],  9);
   FFF3(ccc, ddd, eee, aaa, bbb, X[ 5],  7);
   FFF3(bbb, ccc, ddd, eee, aaa, X[ 1], 15);
   FFF3(aaa, bbb, ccc, ddd, eee, X[ 3], 11);
   FFF3(eee, aaa, bbb, ccc, ddd, X[ 7],  8);
   FFF3(ddd, eee, aaa, bbb, ccc, X[14],  6);
   FFF3(ccc, ddd, eee, aaa, bbb, X[ 6],  6);
   FFF3(bbb, ccc, ddd, eee, aaa, X[ 9], 14);
   FFF3(aaa, bbb, ccc, ddd, eee, X[11], 12);
   FFF3(eee, aaa, bbb, ccc, ddd, X[ 8], 13);
   FFF3(ddd, eee, aaa, bbb, ccc, X[12],  5);
   FFF3(ccc, ddd, eee, aaa, bbb, X[ 2], 14);
   FFF3(bbb, ccc, ddd, eee, aaa, X[10], 13);
   FFF3(aaa, bbb, ccc, ddd, eee, X[ 0], 13);
   FFF3(eee, aaa, bbb, ccc, ddd, X[ 4],  7);
   FFF3(ddd, eee, aaa, bbb, ccc, X[13],  5);

   /* parallel round 4 */
   FFF2(ccc, ddd, eee, aaa, bbb, X[ 8], 15);
   FFF2(bbb, ccc, ddd, eee, aaa, X[ 6],  5);
   FFF2(aaa, bbb, ccc, ddd, eee, X[ 4],  8);
   FFF2(eee, aaa, bbb, ccc, ddd, X[ 1], 11);
   FFF2(ddd, eee, aaa, bbb, ccc, X[ 3], 14);
   FFF2(ccc, ddd, eee, aaa, bbb, X[11], 14);
   FFF2(bbb, ccc, ddd, eee, aaa, X[15],  6);
   FFF2(aaa, bbb, ccc, ddd, eee, X[ 0], 14);
   FFF2(eee, aaa, bbb, ccc, ddd, X[ 5],  6);
   FFF2(ddd, eee, aaa, bbb, ccc, X[12],  9);
   FFF2(ccc, ddd, eee, aaa, bbb, X[ 2], 12);
   FFF2(bbb, ccc, ddd, eee, aaa, X[13],  9);
   FFF2(aaa, bbb, ccc, ddd, eee, X[ 9], 12);
   FFF2(eee, aaa, bbb, ccc, ddd, X[ 7],  5);
   FFF2(ddd, eee, aaa, bbb, ccc, X[10], 15);
   FFF2(ccc, ddd, eee, aaa, bbb, X[14],  8);

   /* parallel round 5 */
   FFF1(bbb, ccc, ddd, eee, aaa, X[12] ,  8);
   FFF1(aaa, bbb, ccc, ddd, eee, X[15] ,  5);
   FFF1(eee, aaa, bbb, ccc, ddd, X[10] , 12);
   FFF1(ddd, eee, aaa, bbb, ccc, X[ 4] ,  9);
   FFF1(ccc, ddd, eee, aaa, bbb, X[ 1] , 12);
   FFF1(bbb, ccc, ddd, eee, aaa, X[ 5] ,  5);
   FFF1(aaa, bbb, ccc, ddd, eee, X[ 8] , 14);
   FFF1(eee, aaa, bbb, ccc, ddd, X[ 7] ,  6);
   FFF1(ddd, eee, aaa, bbb, ccc, X[ 6] ,  8);
   FFF1(ccc, ddd, eee, aaa, bbb, X[ 2] , 13);
   FFF1(bbb, ccc, ddd, eee, aaa, X[13] ,  6);
   FFF1(aaa, bbb, ccc, ddd, eee, X[14] ,  5);
   FFF1(eee, aaa, bbb, ccc, ddd, X[ 0] , 15);
   FFF1(ddd, eee, aaa, bbb, ccc, X[ 3] , 13);
   FFF1(ccc, ddd, eee, aaa, bbb, X[ 9] , 11);
   FFF1(bbb, ccc, ddd, eee, aaa, X[11] , 11);

   /* combine results into new state */
   ddd += cc + MDbuf[1];
   MDbuf[1] = MDbuf[2] + dd + eee;
   MDbuf[2] = MDbuf[3] + ee + aaa;
   MDbuf[3] = MDbuf[4] + aa + bbb;
   MDbuf[4] = MDbuf[0] + bb + ccc;
   MDbuf[0] = ddd;
}

/********************************************************************/
void MDfinish(word *MDbuf, byte *string, word lswlen, word mswlen)
/* The final value of the 5-word MDbuf array is calculated. 
   lswlen and mswlen contain, respectively, the least and most significant
   32 bits of the message bit length mod 2^64, and string is an incomplete
   block containing the (lswlen mod 512) remaining message bits.
   (In case the message is already a multiple of 512 bits, string
   is not used.) The conversion of the 5-word final state MDbuf to
   the 20-byte hash result using a Little-endian convention is the
   responsibility of the calling function.
*/
{
   size_t i, length;
   byte   mask;
   word   X[16];

   /* clear 16-word message block */
   memset(X, 0, 16*sizeof(word));

   /* copy (lswlen mod 512) bits from string into X */
   length = ((lswlen&511)+7)/8; /* number of bytes */
   mask = (lswlen&7) ? ((byte)1 << (lswlen&7)) - 1 : 0xff;
   for (i=0; i<length; i++) {
      /* byte i goes into word X[i div 4] at bit position 8*(i mod 4) */
      if (i == length-1)
         X[i>>2] ^= (word) (*string&mask) << (8*(i&3));
      else
         X[i>>2] ^= (word) *string++ << (8*(i&3));
   }

   /* append a single 1 */
   X[(lswlen>>5)&15] ^= (word)1 << (8*((lswlen>>3)&3)+7-(lswlen&7));

   if ((lswlen & 511) > 447) {
      /* length doesn't fit in this block anymore.
         Compress, and put length in the next block */
      MDcompress(MDbuf, X);
      memset(X, 0, 16*sizeof(word));
   }
  /* append length in bits*/
   X[14] = lswlen;
   X[15] = mswlen;
   MDcompress(MDbuf, X);
}

/************************ end of file rmd160.c **********************/

