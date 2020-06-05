/*
 * entropy.h
 *
 * Description:  Entropy coding definitions and prototypes
 * Developed by: Alexander Djourik <sasha@iszf.irk.ru>
 *               Pavel Zhilin <pzh@iszf.irk.ru>
 *
 * Copyright (c) 1999-2002 Alexander Djourik. All rights reserved.
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

#ifndef ENTROPY_H
#define ENTROPY_H

#define BASE_SIZE 1024*1024
#define STEP_SIZE 1024*1024

#define ENC(x)  (((x)>0)?((x)<<1)-1:(-(x)<<1))
#define DEC(x)  (((x)&1)?(++(x)>>1):(-(x)>>1))

extern unsigned char *bit_array_read;
extern unsigned char *bit_array_write;

void init_bit_array_write (void);
void init_bit_array_read (unsigned long size);
long get_len (void);

void encode_frame (long *data, unsigned long len);
void decode_frame (long *data, unsigned long len);

#endif  /* ENTROPY_H */
