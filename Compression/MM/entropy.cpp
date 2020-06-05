/*
 * entropy.c
 *
 * Description:  TTA entropy coding functions.
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

#include <stdlib.h>
#include <malloc.h>
#include "entropy.h"
#include "ttaenc.h"

#define mymax(a,b)               ((a)>(b)? (a) : (b))

const unsigned long bit_mask32[33] = {
    0x00000000, 0x00000001, 0x00000003, 0x00000007,
    0x0000000f, 0x0000001f, 0x0000003f, 0x0000007f,
    0x000000ff, 0x000001ff, 0x000003ff, 0x000007ff,
    0x00000fff, 0x00001fff, 0x00003fff, 0x00007fff,
    0x0000ffff, 0x0001ffff, 0x0003ffff, 0x0007ffff,
    0x000fffff, 0x001fffff, 0x003fffff, 0x007fffff,
    0x00ffffff, 0x01ffffff, 0x03ffffff, 0x07ffffff,
    0x0fffffff, 0x1fffffff, 0x3fffffff, 0x7fffffff,
    0xffffffff
};

const unsigned long bit_shift[40] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000,
    0x80000000, 0x80000000, 0x80000000, 0x80000000
};

const unsigned long *shift_16 = bit_shift + 4;

unsigned char *bit_array_read;
unsigned long bit_array_read_size, bit_array_read_bits;

unsigned char *bit_array_write;
unsigned long bit_array_write_size, bit_array_write_bits;

void
init_bit_array_write (void) {
    bit_array_write = (unsigned char *) malloc1d (BASE_SIZE, sizeof(char));
    bit_array_write_bits = 0;
    bit_array_write_size = BASE_SIZE;
}

void
init_bit_array_read (unsigned long size) {
    bit_array_read_size = size;
    bit_array_read = (unsigned char *) malloc1d (bit_array_read_size, sizeof(char));
    bit_array_read_bits = 0;
}

long
get_len (void) {
    return (bit_array_write_bits >> 3) + ((bit_array_write_bits & 7UL)? 1:0);
}

__inline void
put_binary (unsigned long value, unsigned long bits) {
    unsigned long fbit = bit_array_write_bits & 0x1FUL;
    unsigned long rbit = 32 - fbit;
    unsigned long pos = bit_array_write_bits >> 5;

    if ((pos << 2) + 8 > bit_array_write_size) {
        bit_array_write = (unsigned char *) realloc (bit_array_write, bit_array_write_size += STEP_SIZE);
        if (!bit_array_write) tta_error (MEMORY_ERROR, NULL);
    }
    unsigned long *s = ((unsigned long *)bit_array_write) + pos;

    *s &= bit_mask32[fbit];
    *s |= (value & bit_mask32[bits]) << fbit;
    if (bits > rbit) *(++s) = value >> rbit;

    bit_array_write_bits += bits;
}

__inline void
put_unary (unsigned long value) {
    unsigned long fbit = bit_array_write_bits & 0x1FUL;
    unsigned long rbit = 32 - fbit;
    unsigned long pos = bit_array_write_bits >> 5;

    if ((pos << 2) + value > bit_array_write_size) {
        bit_array_write = (unsigned char *) realloc (bit_array_write, bit_array_write_size += mymax(STEP_SIZE,value/8+10));
        if (!bit_array_write) tta_error (MEMORY_ERROR, NULL);
    }
    unsigned long *s = ((unsigned long *)bit_array_write) + pos;

    *s &= bit_mask32[fbit];
    if (value < rbit) *s |= (bit_mask32[value]) << fbit;
    else {
        unsigned long unary = value;
        *s++ |= (bit_mask32[rbit]) << fbit; unary -= rbit;
        for (;unary > 32; unary -= 32) *s++ = bit_mask32[32];
        if (unary) *s = bit_mask32[unary];
    }

    bit_array_write_bits += (value + 1);
}

__inline void
get_binary (unsigned long *value, unsigned long bits) {
    unsigned long fbit = bit_array_read_bits & 0x1FUL;
    unsigned long rbit = 32 - fbit;
    unsigned long pos = bit_array_read_bits >> 5;
    unsigned long *s = ((unsigned long *) bit_array_read) + pos;

    *value = 0;

    if (pos > bit_array_read_size) return;

    if (bits <= rbit)
        *value = (*s >> fbit) & bit_mask32[bits];
    else {
        *value = (*s++ >> fbit) & bit_mask32[rbit];
        *value |= (*s & bit_mask32[bits - rbit]) << rbit;
    }

    bit_array_read_bits += bits;
}

__inline void
get_unary (unsigned long *value) {
    unsigned long fbit = bit_array_read_bits & 0x1FUL;
    unsigned long rbit = 32 - fbit;
    unsigned long pos = bit_array_read_bits >> 5;
    unsigned long *s = ((unsigned long *) bit_array_read) + pos;
    unsigned long mask = 1;

    *value = 0;

    if (pos > bit_array_read_size) return;

    if ((*s >> fbit) == bit_mask32[rbit]) {
        *value += rbit; fbit = 0;
        while (*(++s) == bit_mask32[32]) *value += 32;
    }
    for (mask <<= fbit; *s & mask; mask <<= 1) (*value)++;

    bit_array_read_bits += (*value + 1);
}

void
encode_frame (long *data, unsigned long len) {
    long *p;
    unsigned long value;
    unsigned long unary, binary;

    unsigned long k;
    unsigned long k0 = 10;
    unsigned long k1 = 10;
    unsigned long sum0 = shift_16[k0];
    unsigned long sum1 = shift_16[k1];


    for (p = data; p < data + len; p++) {
        // convert sample from signed to unsigned
        value = ENC(*p);

        // encode Rice unsigned
        k = k0;

        sum0 += value - (sum0 >> 4);
        if (k0 > 0 && sum0 < shift_16[k0])
            k0--;
        else if (sum0 > shift_16[k0 + 1])
            k0++;

        if (value >= bit_shift[k]) {
            value -= bit_shift[k];
            k = k1;

            sum1 += value - (sum1 >> 4);
            if (k1 > 0 && sum1 < shift_16[k1])
            	k1--;
            else if (sum1 > shift_16[k1 + 1])
            	k1++;

            unary = 1 + (value >> k);
        } else {
            unary = 0;
        }

        // put Rice code
        if (unary>=50) {
            put_unary (50);
            put_binary (unary, 32);
        } else {
            put_unary (unary);
        }
        if (k) {
            binary = value & bit_mask32[k];
            put_binary(binary, k);
        }
    }
}

void
decode_frame (long *data, unsigned long len) {
    long *p, value;
    unsigned long unary, binary;

    unsigned long k;
    unsigned long k0 = 10;
    unsigned long k1 = 10;
    unsigned long sum0 = shift_16[k0];
    unsigned long sum1 = shift_16[k1];
    int depth;

    for (p = data; p < data + len; p++) {

	// decode Rice unsigned
        get_unary (&unary);
        if (unary==50) {
            get_binary (&unary, 32);
        }

        switch (unary) {
        case 0:  depth = 0; k = k0; break;

        default: depth = 1; k = k1;
        	 unary--;
        }

        if (k) {
            get_binary(&binary, k);
            value = (unary << k) + binary;
        } else {
            value = unary;
        }

        switch (depth) {
        case 1:
        	sum1 += value - (sum1 >> 4);
        	if (k1 > 0 && sum1 < shift_16[k1])   k1--;
        	else if (sum1 > shift_16[k1 + 1])    k1++;
        	value += bit_shift[k0];
                // no break!
        default:
        	sum0 += value - (sum0 >> 4);
        	if (k0 > 0 && sum0 < shift_16[k0])   k0--;
        	else if (sum0 > shift_16[k0 + 1])    k0++;
        }

        *p = DEC(value);
    }
}

/* eof */

