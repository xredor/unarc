/*
 * ttaenc.h
 *
 * Description:  TTA main definitions and prototypes
 * Developed by: Alexander Djourik <sasha@iszf.irk.ru>
 *               Pavel Zhilin <pzh@iszf.irk.ru>
 *
 * Copyright (c) 1999-2003 Alexander Djourik. All rights reserved.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * aint with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Please see the file COPYING in this directory for full copyright
 * information.
 */

#ifndef TTAENC_H
#define TTAENC_H

#define COPYRIGHT       "Copyright (c) 2003 Alexander Djourik. All rights reserved."

#define MYNAME          "ttaenc"
#define VERSION         "2.0"
#define BUILD           "20040108"
#define FRAME_TIME      1.04489795918367346939
#define TTA_SIGN        "TTA"
#define TTA_FORMAT      '2'

#define MIN_LEVEL       1
#define MAX_LEVEL       3
#define DEF_LEVEL       2

#define COMMAND_ERROR   0
#define FORMAT_ERROR    1
#define FILE_ERROR      2
#define FIND_ERROR      3
#define CREATE_ERROR    4
#define OPEN_ERROR      5
#define WRITE_ERROR     6
#define READ_ERROR      7
#define MEMORY_ERROR    8

#define SWAP16(x) (\
(((x)&(1<< 0))?(1<<15):0) | \
(((x)&(1<< 1))?(1<<14):0) | \
(((x)&(1<< 2))?(1<<13):0) | \
(((x)&(1<< 3))?(1<<12):0) | \
(((x)&(1<< 4))?(1<<11):0) | \
(((x)&(1<< 5))?(1<<10):0) | \
(((x)&(1<< 6))?(1<< 9):0) | \
(((x)&(1<< 7))?(1<< 8):0) | \
(((x)&(1<< 8))?(1<< 7):0) | \
(((x)&(1<< 9))?(1<< 6):0) | \
(((x)&(1<<10))?(1<< 5):0) | \
(((x)&(1<<11))?(1<< 4):0) | \
(((x)&(1<<12))?(1<< 3):0) | \
(((x)&(1<<13))?(1<< 2):0) | \
(((x)&(1<<14))?(1<< 1):0) | \
(((x)&(1<<15))?(1<< 0):0))

#define LINE "------------------------------------------------------------"

void tta_error (long error, const char *name);
void *malloc1d (size_t num, size_t size);
long **malloc2d (long num, unsigned long len);

int tta_compress (int level, int skip_header, int is_float, int num_chan, int word_size, int offset, int raw_data, CALLBACK_FUNC *callback, void *auxdata);
int tta_decompress (CALLBACK_FUNC *callback, void *auxdata);

#endif  /* TTAENC_H */
