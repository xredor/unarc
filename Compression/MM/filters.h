/*
 * filters.h
 *
 * Description:  TTA filters definitions and prototypes
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

#ifndef FILTERS_H
#define FILTERS_H

#define MAX_ORDER   32
#define BUF_SIZE    256

#ifndef M_LN2
#define M_LN2       0.69314718055994530942
#endif

#define PREDICTOR1(x, k)    ((long)((((uint64)x << k) - x) >> k))

void filters_compress   (long *data, unsigned long len, long level, long byte_size);
void filters_decompress (long *data, unsigned long len, long level, long byte_size);

#endif  /* FILTERS_H */
