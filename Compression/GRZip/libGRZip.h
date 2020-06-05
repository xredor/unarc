/*-------------------------------------------------*/
/* GRZipII/libGRZip compressor          libGRZip.h */
/* GRZipII/libGRZip Global Header File             */
/*-------------------------------------------------*/

/*--
  This file is a part of GRZipII and/or libGRZip, a program
  and library for lossless, block-sorting data compression.

  Copyright (C) 2002-2004 Grebnov Ilya. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  Grebnov Ilya, Ivanovo, Russian Federation.
  Ilya.Grebnov@magicssoft.ru, http://magicssoft.ru/

  This program is based on (at least) the work of:
  Juergen Abel, Jon L. Bentley, Edgar Binder,
  Charles Bloom, Mike Burrows, Andrey Cadach,
  Damien Debin, Sebastian Deorowicz, Peter Fenwick,
  George Plechanov, Michael Schindler, Robert Sedgewick,
  Julian Seward, David Wheeler, Vadim Yoockin.

  For more information on these sources, see the manual.
--*/

#ifndef GRZip_GLOBAL_H
#define GRZip_GLOBAL_H

#ifdef __GNUC__
#define GINLINE   __inline__
#elif _VISUALC || __INTEL_COMPILER || __BORLANDC__
#define GINLINE   __inline
#else
#define GINLINE /* */
#endif

#define GRZ_NO_ERROR                  0
#define GRZ_NOT_ENOUGH_MEMORY        -1
#define GRZ_CRC_ERROR                -2
#define GRZ_UNEXPECTED_EOF           -3

#define GRZ_NOT_COMPRESSIBLE         -4
#define GRZ_FAST_BWT_FAILS           -5

#define GRZ_MaxBlockSize             (8*1024*1024-512)
#define GRZ_Log2MaxBlockSize         23

// Кодирование Mode: младший байт - флаги
//                   второй байт  - величина хеш-таблицы для LZP
//                   третий-четвёртый байт (без старшего бита) - мин. длина строки для LZP

#define GRZ_Enable_DeltaFlt          0x0
#define GRZ_Disable_DeltaFlt         0x1

#define GRZ_Compression_BWT          0x0
#define GRZ_Compression_ST4          0x2

#define GRZ_Compression_WFC          0x0
#define GRZ_Compression_MTF          0x4

#define GRZ_BWTSorting_Strong        0x0
#define GRZ_BWTSorting_Fast          0x8

#define Encode_LZP_MinMatchLen(Len)  ((Len)*65536)
#define Encode_LZP_HT_Size(Size)     ((Size)*256)
#define Get_LZP_MinMatchLen(Mode)    ((Mode)/65536%32767)
#define LZP_MaxMatchLen              32768
#define Get_LZP_HT_Size(Mode)        ((1<<((Mode)/256%256))-1)
#define LZP_Enabled(Mode)            ((Mode)/256)
#define Disable_LZP(Mode)            ((Mode)%256)
#define DisableAllButLZP(Mode)       ((Mode)/256*256)
#define GRZ_Disable_LZP              0x000

#endif

/*-------------------------------------------------*/
/* End                                  libGRZip.h */
/*-------------------------------------------------*/
