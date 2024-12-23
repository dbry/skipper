////////////////////////////////////////////////////////////////////////////
//                            **** LZW-AB ****                            //
//               Adjusted Binary LZW Compressor/Decompressor              //
//                  Copyright (c) 2016-2020 David Bryant                  //
//                           All Rights Reserved                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

#ifndef LZWLIB_H_
#define LZWLIB_H_

int lzw_compress (void (*dst)(int,void*), void *dstctx, int (*src)(void*), void *srcctx, int maxbits);
int lzw_decompress (void (*dst)(int,void*), void *dstctx, int (*src)(void*), void *srcctx);

#endif /* LZWLIB_H_ */
